#pragma once
#include "feed_test_compare.h"

static TestCompareInput ReadTestCompareInput(reshade::api::effect_runtime *rt)
{
    TestCompareInput input;
    input.enabled = g_cfg.test_compare_hotkey != 0;
    input.windowless = rt == g.runtime && rt->get_hwnd() == nullptr;
    if (!input.enabled || !input.windowless) return input;
    DWORD foreground_pid = 0;
    const HWND foreground = GetForegroundWindow();
    if (foreground) GetWindowThreadProcessId(foreground, &foreground_pid);
    input.foreground = foreground_pid == GetCurrentProcessId();
    if (!input.foreground) return input;
    input.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    input.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    input.n = (GetAsyncKeyState('N') & 0x8000) != 0;
    return input;
}

// Called only while holding FeedEnter, before the verified-depth gate. Comparing
// keeps every feature/resource alive; returning resets both temporal histories.
static bool TestComparisonSkipsFeed(reshade::api::effect_runtime *rt)
{
    static TestCompareState state;
    static ULONGLONG next_poll = 0;
    if (!g_cfg.test_compare_hotkey && !state.original) {
        state = {};
        return false;
    }
    if (state.original) PollBlockedFeedConfig(next_poll, GetTickCount64());
    const TestCompareInput input = ReadTestCompareInput(rt);
    const TestCompareChange change = state.Update(input);
    if (change == TestCompareChange::original_on)
        Log("[test] original image comparison ON");
    else if (change == TestCompareChange::original_off) {
        Log("[test] original image comparison OFF");
        g.need_reset = true;
    }
    if (!state.original) next_poll = 0;
    return state.original && input.windowless;
}
