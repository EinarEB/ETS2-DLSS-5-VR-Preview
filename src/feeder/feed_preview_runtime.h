// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
#pragma once
#include "feed_preview_blend.h"

static ets2_preview::BlendControl g_preview_blend;
static ets2_preview::BlendSelection g_preview_frame;
static float g_frame_work_mix = 1.0f; // Serialized render path; never read by the overlay.

static bool IsPreviewRuntime(reshade::api::effect_runtime* rt) {
    return rt && rt == g.runtime && !rt->get_hwnd() &&
        g_cfg.stereo_mode == 1 && g_cfg.work_composite == 1;
}
static void LatchPreviewBlend(reshade::api::effect_runtime* rt) {
    g_preview_blend.Configure(g_cfg.work_mix);
    g_preview_frame = g_preview_blend.Latch();
    g_frame_work_mix = IsPreviewRuntime(rt) ? g_preview_frame.value : g_cfg.work_mix;
}
static void PollPreviewKey(reshade::api::effect_runtime* rt) {
    g_preview_blend.Configure(g_cfg.work_mix);
    bool eligible = IsPreviewRuntime(rt) && g_cfg.preview_toggle_key != 0;
    DWORD pid = 0;
    if (eligible) {
        const HWND foreground = GetForegroundWindow();
        if (foreground) GetWindowThreadProcessId(foreground, &pid);
        eligible = pid == GetCurrentProcessId();
    }
    const bool down = eligible && (GetAsyncKeyState(g_cfg.preview_toggle_key) & 0x8000) != 0;
    if (g_preview_blend.Key(eligible, g_cfg.preview_toggle_key, down, reinterpret_cast<uintptr_t>(rt)))
        Log("[preview] comparison selected %.0f%%; neural processing remains active", g_preview_blend.Latch().value * 100);
}
static void PublishPreviewStatus(reshade::api::effect_runtime* rt, bool deliveredNow) {
    if (!IsPreviewRuntime(rt)) return;
    static ULONGLONG nextWrite = 0;
    const auto now = GetTickCount64();
    if (now < nextWrite) return;
    nextWrite = now + 500;
    // Unicode path and atomic replacement prevent the launcher reading half a status.
    wchar_t module[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_self, module, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    const auto target = std::filesystem::path(module).parent_path() / L"preview-status.json";
    const auto temporary = target.wstring() + L".part";
    const auto state = g_preview_blend.Status();
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return;
    const uint64_t session = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") || !file) return;
    const char* reason = PreviewNeedsRestart() ? "restart_required" : g.disabled ? "processing_failed" : !g_cfg.enabled || g_cfg.mode == 0 ?
        "processing_disabled" : deliveredNow ? "delivered" : "waiting_for_neural_frame";
    const int written = fprintf(file,
        "{\"schema\":1,\"pid\":%lu,\"session\":\"%llu\",\"tick\":%llu,"
        "\"runtime_generation\":%llu,\"selected_blend\":%.6f,\"selected_serial\":%llu,\"temporary\":%s,"
        "\"delivered_blend\":%.6f,\"delivered_serial\":%llu,\"delivered_frame\":%llu,"
        "\"delivered_tick\":%llu,\"delivery_this_present\":%s,\"reason\":\"%s\",\"key\":%d}\n",
        GetCurrentProcessId(), session, now, state.selected.generation, state.selected.value, state.selected.serial,
        state.temporary ? "true" : "false", state.delivered.value, state.delivered.serial,
        state.deliveredFrame, state.deliveredTick, deliveredNow ? "true" : "false", reason,
        g_cfg.preview_toggle_key);
    const int closed = fclose(file);
    if (written > 0 && closed == 0)
        MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
}
