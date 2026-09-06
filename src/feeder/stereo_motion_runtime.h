// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
#pragma once
#include "stereo_motion_history.h"
#include <mutex>
static std::mutex g_motion_mutex;
static ets2_motion::HistoryGate g_motion_history;
static constexpr const char* kStereoMotionEffect="ETS2_VortStereo.fx";
static void SetMotionReady(reshade::api::effect_runtime* rt,bool ready) {
    auto u=rt->find_uniform_variable(kEffectFile,"ETS2_MOTION_HISTORY_VALID");
    if(u.handle)rt->set_uniform_value_bool(u,&ready,1);
}
static void MotionProviderCompleted(reshade::api::effect_runtime* rt,reshade::api::effect_technique tech) {
    if(rt->get_hwnd()!=nullptr)return;
    const auto provider=rt->find_technique(kStereoMotionEffect,"ETS2_VortStereo");
    if(!provider.handle||provider.handle!=tech.handle)return;
    const auto frame=rt->find_uniform_variable(kStereoMotionEffect,"ETS2_VORT_FRAME");
    const auto depth=rt->find_uniform_variable(kStereoMotionEffect,"ETS2_STEREO_DEPTH_VALID");
    const auto reset=rt->find_uniform_variable(kStereoMotionEffect,"ETS2_VORT_RESET");
    uint32_t index=0;bool valid=false,restart=false;
    if(frame.handle)rt->get_uniform_value_uint(frame,&index,1);
    if(depth.handle)rt->get_uniform_value_bool(depth,&valid,1);
    if(reset.handle)rt->get_uniform_value_bool(reset,&restart,1);
    std::lock_guard<std::mutex> lock(g_motion_mutex);
    const bool ready=g_motion_history.Observe(reinterpret_cast<uintptr_t>(rt),provider.handle,index,valid&&frame.handle,restart);
    SetMotionReady(rt,ready);
}
static void MotionRuntimePresented(reshade::api::effect_runtime* rt) {
    if(rt->get_hwnd()!=nullptr)return;
    std::lock_guard<std::mutex> lock(g_motion_mutex);
    g_motion_history.Present(reinterpret_cast<uintptr_t>(rt));SetMotionReady(rt,false);
}
static void ResetMotionRuntime(reshade::api::effect_runtime* rt) {
    std::lock_guard<std::mutex> lock(g_motion_mutex);
    g_motion_history.Reset(reinterpret_cast<uintptr_t>(rt));SetMotionReady(rt,false);
}
