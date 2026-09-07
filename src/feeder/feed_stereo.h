#pragma once

// Private experimental D3D11 SBS adapter. Inputs are already in full-image PIXEL
// units, so cropping does not rescale motion. Each eye has its own SR handle,
// parameter object, color/depth/motion/mask resources and output resource. The
// classic consumer deduplicates by output pointer and maintains NR state by SR
// handle. Its scratch images are shared, hence equal eye sizes and sequential
// dispatch on the same command list are essential. No headset validation implied.
#include "feed_carrier.h"
#include "stereo_quality.h"

struct StereoEye {
    NVSDK_NGX_Handle *feature = nullptr;
    NVSDK_NGX_Parameter *params = nullptr;
    ID3D12Resource *tex[SLOT_COUNT] = {};
};
static struct StereoState {
    StereoEye eye[4];
    unsigned eye_count = 2;
    bool copy_carrier = false;
    UINT width = 0, height = 0;
    UINT crop_x = 0, crop_y = 0;     // centred square position (both eyes) before alignment
    UINT crop_x_eye[2] = {0, 0};     // per-eye square x; equal to crop_x unless aligned on far content
    int crop_percent = 0;
    bool active = false;
    NVSDK_NGX_Handle *pending_feature[4] = {};
    NVSDK_NGX_Parameter *pending_params[4] = {};
    UINT64 pending_fence = 0;
} g_stereo;

static bool StereoResourcesOwned()
{
    return g_stereo.active || g_stereo.width != 0 || g_stereo.pending_fence != 0 ||
        g_stereo.pending_feature[0] || g_stereo.pending_feature[1] ||
        g_stereo.pending_params[0] || g_stereo.pending_params[1] ||
        g_stereo.pending_feature[2] || g_stereo.pending_feature[3] ||
        g_stereo.pending_params[2] || g_stereo.pending_params[3];
}

static bool StereoRetirementPending()
{
    if (g.dev12 && FAILED(g.dev12->GetDeviceRemovedReason())) return false;
    if (g.unfenced_submission) return true;
    if (!g_stereo.pending_fence) return false;
    return !g.fence12 || g.fence12->GetCompletedValue() < g_stereo.pending_fence;
}

static void StereoDestroyParameters(NVSDK_NGX_Parameter *params)
{
    if (!params || g_ngx_dying) return;
    __try { NVSDK_NGX_D3D12_DestroyParameters(params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("[stereo] DestroyParameters exception 0x%08X (ignored)", GetExceptionCode()); }
}

static void StereoReleaseModel(NVSDK_NGX_Handle *feature, NVSDK_NGX_Parameter *params)
{
    if (!g_ngx_dying) SafeReleaseFeature(feature);
    StereoDestroyParameters(params);
}

// Caller drains the private queue before calling this, as for ordinary resources.
static void StereoRelease()
{
    if (StereoRetirementPending()) return; // session owner also preserves all dependencies
    g.unfenced_submission = false;
    for (unsigned eye = 0; eye < 4; ++eye) {
        StereoReleaseModel(g_stereo.pending_feature[eye], g_stereo.pending_params[eye]);
        g_stereo.pending_feature[eye] = nullptr; g_stereo.pending_params[eye] = nullptr;
    }
    g_stereo.pending_fence = 0;
    for (auto &eye : g_stereo.eye) {
        if (g.feature == eye.feature) g.feature = nullptr; // borrowed identity only
        auto *feature = eye.feature; auto *params = eye.params;
        eye.feature = nullptr; eye.params = nullptr;
        StereoReleaseModel(feature, params);
        for (auto *&tex : eye.tex) SafeRelease(tex);
    }
    CarrierRemove();
    g_stereo.eye_count = 2; g_stereo.copy_carrier = false;
    g_stereo.active = false;
    g_stereo.width = g_stereo.height = 0;
    g_stereo.crop_x = g_stereo.crop_y = 0; g_stereo.crop_percent = 0;
    g_stereo.crop_x_eye[0] = g_stereo.crop_x_eye[1] = 0;
}

static NVSDK_NGX_Result StereoCreateGuarded(NVSDK_NGX_Handle **handle, NVSDK_NGX_Parameter *params,
                                          NVSDK_NGX_DLSS_Create_Params *cp, DWORD *code)
{
    __try { return NGX_D3D12_CREATE_DLSS_EXT(g.list, 1, 1, handle, params, cp); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7FFFFFFF); }
}

static NVSDK_NGX_Result StereoEvaluateGuarded(StereoEye *eye, NVSDK_NGX_D3D12_DLSS_Eval_Params *ep, DWORD *code)
{
    if(g_stereo.copy_carrier){
        if(!CarrierInstall())return static_cast<NVSDK_NGX_Result>(0xBAD00001);
        carrierList=g.list;carrierInput=eye->tex[SLOT_COLOR];carrierOutput=eye->tex[SLOT_OUTPUT];
    }
    __try { return NGX_D3D12_EVALUATE_DLSS_EXT(g.list, eye->feature, eye->params, ep); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7FFFFFFF); }
}

static bool StereoWaitForCreate(UINT64 value)
{
    if (value == 0) return false;
    if (g.fence12->GetCompletedValue() >= value) return true;
    ResetEvent(g.fence_event);
    if (FAILED(g.fence12->SetEventOnCompletion(value, g.fence_event)) ||
        WaitForSingleObject(g.fence_event, 4000) != WAIT_OBJECT_0 ||
        g.fence12->GetCompletedValue() < value) {
        FeedDisable("the stereo feature creation did not finish within 4 seconds");
        return false;
    }
    return true;
}

static bool StereoCreateModels(bool inverted, bool *crashed)
{
    if (StereoRetirementPending()) {
        FeedDisable("a previous stereo create is still on the GPU; resources are retained until retirement");
        return false;
    }
    if (g_stereo.pending_fence) {
        // A manual retry is possible after the timed-out work eventually retires.
        // Retire that quarantined generation before creating another one.
        for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
            StereoReleaseModel(g_stereo.pending_feature[eye], g_stereo.pending_params[eye]);
            g_stereo.pending_feature[eye] = nullptr; g_stereo.pending_params[eye] = nullptr;
        }
        g_stereo.pending_fence = 0;
    }
    NVSDK_NGX_Handle *next_feature[4] = {};
    NVSDK_NGX_Parameter *next_params[4] = {};
    if (crashed) *crashed = false;
    bool ok = true;
    for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
        const auto result = NVSDK_NGX_D3D12_AllocateParameters(&next_params[eye]);
        if (NVSDK_NGX_FAILED(result) || !next_params[eye]) {
            Log("[stereo] eye %u parameter allocation failed 0x%08X", eye, result);
            ok = false; break;
        }
        if (g_cfg.preset > 0)
            next_params[eye]->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, static_cast<unsigned>(g_cfg.preset));
    }
    NVSDK_NGX_DLSS_Create_Params cp = {};
    cp.Feature.InWidth = cp.Feature.InTargetWidth = g_stereo.width;
    cp.Feature.InHeight = cp.Feature.InTargetHeight = g_stereo.height;
    cp.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
    int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if (inverted) flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (g.hdr) flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if (g_cfg.flags >= 0) flags = g_cfg.flags;
    cp.InFeatureCreateFlags = flags;
    g.create_flags = flags;

    if (ok) ok = BeginCommands();
    if (ok) {
        bool aborted = false;
        for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
            DWORD code = 0;
            const auto result = StereoCreateGuarded(&next_feature[eye], next_params[eye], &cp, &code);
            Log("[stereo] create eye=%u extent=%ux%u SRhandle=%p result=0x%08X exception=0x%08X",
                eye, g_stereo.width, g_stereo.height, next_feature[eye], result, code);
            if (code != 0) {
                next_feature[eye] = nullptr; // an exception may leave an invalid partial handle
                AbortCommands(); aborted = true; ok = false;
                if (crashed) *crashed = true;
                break;
            }
            if (NVSDK_NGX_FAILED(result) || !next_feature[eye]) { ok = false; break; }
        }
        if (!aborted) {
            const UINT64 value = EndCommands();
            if (!StereoWaitForCreate(value)) {
                ok = false;
                if (SUCCEEDED(g.dev12->GetDeviceRemovedReason()) &&
                    (g.unfenced_submission || (value != 0 && g.fence12->GetCompletedValue() < value))) {
                    // Retain both new models AND the whole session after a submitted
                    // timeout. A CPU timeout does not mean the GPU stopped using them.
                    g_stereo.pending_fence = value;
                    for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
                        g_stereo.pending_feature[eye] = next_feature[eye]; next_feature[eye] = nullptr;
                        g_stereo.pending_params[eye] = next_params[eye]; next_params[eye] = nullptr;
                    }
                    Log("[stereo] submitted create fence=%llu not retired; quarantined models and session", value);
                }
            }
        }
    }
    if (!ok) {
        // No working model is discarded on a failed same-size recreation.
        for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) StereoReleaseModel(next_feature[eye], next_params[eye]);
        return false;
    }
    for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
        auto *old_feature = g_stereo.eye[eye].feature;
        auto *old_params = g_stereo.eye[eye].params;
        g_stereo.eye[eye].feature = next_feature[eye];
        g_stereo.eye[eye].params = next_params[eye];
        StereoReleaseModel(old_feature, old_params);
    }
    g.feature = g_stereo.eye[0].feature; // preserve existing ready/missing checks; owned above
    g_stereo.active = true;
    g.need_reset = true; g.frame_ready = true; g.create_fail_count = 0;
    Log("[stereo] %u independent %ux%u carrier features ready; %u neural pass(es) per eye, input=%s; distinct outputs and histories", g_stereo.eye_count, g_stereo.width, g_stereo.height, g_stereo.eye_count/2, g_stereo.copy_carrier ? "preserved game color" : "legacy DLAA");
    return true;
}

static bool StereoBuild(UINT width, UINT height, bool inverted, bool *crashed)
{
    if (!g.dev11 || width < 2 || (width & 1) || g_cfg.work_upscale == 2 || g.sr_active || g_chicken_present || g_renodx_lazy) {
        FeedDisable("experimental stereo mode requires D3D11, equal side-by-side eyes, ordinary DLAA work scaling and the classic consumer");
        return false;
    }
    const auto region=ets2_quality::Region(width/2,height,g_cfg.stereo_crop);
    if(!region.width||!region.height||(g_cfg.stereo_crop&&(!g_cfg.work_composite||g.color_fmt!=g.output_fmt))){
        FeedDisable("square cropping requires matching SDR color and native-detail composition");return false;
    }
    g_stereo.width=region.width;g_stereo.height=region.height;
    g_stereo.crop_x=region.x;g_stereo.crop_y=region.y;g_stereo.crop_percent=g_cfg.stereo_crop;
    g_stereo.crop_x_eye[0]=g_stereo.crop_x_eye[1]=region.x;
    int shiftWork=0;
    if(g_cfg.stereo_crop&&g_cfg.stereo_crop_align&&g_cfg.stereo_eye_shift&&g.backbuffer_width){
        // The eye buffers are asymmetric frusta: far content sits shiftWork pixels further
        // along x in the right eye. Move each square by half of that, within its eye, so the
        // two squares cover the same distant content instead of the same screen position.
        shiftWork=int(std::lround(double(g_cfg.stereo_eye_shift)*double(width/2)/double(g.backbuffer_width/2)));
        const int maxX=int(width/2)-int(region.width);
        const int left=std::clamp(int(region.x)-shiftWork/2,0,maxX);
        const int right=std::clamp(int(region.x)+(shiftWork-shiftWork/2),0,maxX);
        g_stereo.crop_x_eye[0]=UINT(left);g_stereo.crop_x_eye[1]=UINT(right);
    }
    Log("[stereo-crop] active=%d%% full-eye=%ux%u region=%u,%u %ux%u; squares left x=%u right x=%u (far-content shift %d work px); motion remains in work pixels; original periphery preserved",g_stereo.crop_percent,width/2,height,region.x,region.y,region.width,region.height,g_stereo.crop_x_eye[0],g_stereo.crop_x_eye[1],shiftWork);
    g_stereo.eye_count = 2 * static_cast<unsigned>(g_cfg.stereo_passes);
    g_stereo.copy_carrier = g_cfg.stereo_carrier_copy != 0;
    if(g_stereo.eye_count>2 && g.color_fmt!=g.output_fmt){
        FeedDisable("two neural passes require matching input and output color formats");return false;
    }
    if(g_stereo.copy_carrier && (g.color_fmt != g.output_fmt || g.hdr ||
       (g.color_fmt != DXGI_FORMAT_R8G8B8A8_UNORM && g.color_fmt != DXGI_FORMAT_B8G8R8A8_UNORM &&
        !(g.stereo_sdr_float&&g.color_fmt==DXGI_FORMAT_R16G16B16A16_FLOAT)))){
        FeedDisable("preserved-input stereo requires equal-size SDR color resources");return false;
    }
    D3D12_HEAP_PROPERTIES heap = {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
        for (unsigned slot = 0; slot < SLOT_COUNT; ++slot) {
            auto desc = g.tex12[slot]->GetDesc();
            desc.Width = g_stereo.width;
            desc.Height = g_stereo.height;
            desc.Flags = slot == SLOT_OUTPUT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
            const auto state = slot == SLOT_OUTPUT ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            const HRESULT hr = g.dev12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                __uuidof(ID3D12Resource), reinterpret_cast<void **>(&g_stereo.eye[eye].tex[slot]));
            if (FAILED(hr)) {
                Log("[stereo] eye=%u slot=%s allocation failed 0x%08X", eye, kSlotName[slot], hr);
                StereoRelease(); return false;
            }
            wchar_t name[96]; _snwprintf_s(name, _TRUNCATE, L"feed stereo eye %u slot %u", eye, slot);
            g_stereo.eye[eye].tex[slot]->SetName(name);
        }
    }
    return StereoCreateModels(inverted, crashed);
}

static bool StereoRecreate(bool inverted)
{
    bool crashed = false;
    if (StereoCreateModels(inverted, &crashed)) return true;
    g.need_reset = true;
    if (g.disabled || StereoRetirementPending()) { g.frame_ready = false; return false; }
    if (g_stereo.active) {
        g.feature = g_stereo.eye[0].feature;
        g.frame_ready = true; g.warmup_done = true;
        Log("[stereo] recreation failed%s; retained both previous features", crashed ? " with caught exception" : "");
        return true;
    }
    return false;
}

static void StereoCopyRegion(ID3D12Resource *dst, UINT dst_x, UINT dst_y, ID3D12Resource *src, UINT src_x, UINT src_y, UINT width, UINT height)
{
    D3D12_TEXTURE_COPY_LOCATION to = {}, from = {};
    to.pResource = dst; from.pResource = src;
    to.Type = from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_BOX box = {src_x, src_y, 0, src_x + width, src_y + height, 1};
    g.list->CopyTextureRegion(&to, dst_x, dst_y, 0, &from, &box);
}

static NVSDK_NGX_Result StereoEvaluate(NVSDK_NGX_D3D12_DLSS_Eval_Params *full, DWORD *code)
{
    *code = 0;
    LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
    if(g_stereo.crop_percent){
        // Initialize every untouched output pixel to its exact corresponding input.
        // Native-detail composition therefore has zero edit outside the square.
        Barrier(g.tex12[SLOT_COLOR],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(g.tex12[SLOT_OUTPUT],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        g.list->CopyResource(g.tex12[SLOT_OUTPUT],g.tex12[SLOT_COLOR]);
        Barrier(g.tex12[SLOT_COLOR],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(g.tex12[SLOT_OUTPUT],D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    const unsigned slots[] = {SLOT_COLOR, SLOT_DEPTH, SLOT_MV, SLOT_MASK};
    for (unsigned slot : slots) {
        if (slot == SLOT_MASK && !g.mask_ok) continue;
        Barrier(g.tex12[slot], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
            if(slot==SLOT_COLOR && eye>=2)continue;
            auto *tex = g_stereo.eye[eye].tex[slot];
            Barrier(tex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            StereoCopyRegion(tex, 0, 0, g.tex12[slot], (eye % 2) * (g.width/2)+g_stereo.crop_x_eye[eye % 2], g_stereo.crop_y, g_stereo.width, g_stereo.height);
            Barrier(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        Barrier(g.tex12[slot], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Success;
    ets2_native::BeginControls(g_stereo.eye_count,full->InReset!=0,g_cfg.stereo_controls!=0,g_cfg.stereo_second_tone,g_cfg.stereo_second_structure);
    for (unsigned eye = 0; eye < g_stereo.eye_count; ++eye) {
        if(eye>=2){
            auto* previous=g_stereo.eye[eye-2].tex[SLOT_OUTPUT];
            auto* next=g_stereo.eye[eye].tex[SLOT_COLOR];
            Barrier(previous,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(next,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            g.list->CopyResource(next,previous);
            Barrier(previous,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(next,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        auto ep = *full;
        ep.Feature.pInColor = g_stereo.eye[eye].tex[SLOT_COLOR];
        ep.Feature.pInOutput = g_stereo.eye[eye].tex[SLOT_OUTPUT];
        ep.pInDepth = g_stereo.eye[eye].tex[SLOT_DEPTH];
        ep.pInMotionVectors = g_stereo.eye[eye].tex[SLOT_MV];
        ep.pInBiasCurrentColorMask = g.mask_ok ? g_stereo.eye[eye].tex[SLOT_MASK] : nullptr;
        ep.InRenderSubrectDimensions.Width = g_stereo.width;
        ep.InRenderSubrectDimensions.Height = g_stereo.height;
        // The copied motion remains in pixels. InReset intentionally reaches BOTH calls.
        const auto beforeCopies=carrierCopies;
        ets2_native::OwnedScope observedEye(int(eye%2),int(eye/2),g.frames_done+1,g.list);
        result = StereoEvaluateGuarded(&g_stereo.eye[eye], &ep, code);
        carrierList=nullptr;carrierInput=nullptr;carrierOutput=nullptr;
        if(g_stereo.copy_carrier && carrierCopies!=beforeCopies+1){
            FeedDisable("the preserved-input carrier did not execute exactly once; restart before resuming");
            result=static_cast<NVSDK_NGX_Result>(0xBAD00001);
        }
        if (g.frames_done < 3 || *code != 0 || NVSDK_NGX_FAILED(result))
            Log("[stereo] eval eye=%u SRhandle=%p output=%p reset=%d result=0x%08X exception=0x%08X pass=%u", eye % 2,
                g_stereo.eye[eye].feature, ep.Feature.pInOutput, ep.InReset, result, *code, eye/2+1);
        if (*code != 0 || NVSDK_NGX_FAILED(result)) { g.need_reset = true; break; }
    }
    if (*code == 0 && NVSDK_NGX_SUCCEED(result)) {
        Barrier(g.tex12[SLOT_OUTPUT], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        for (unsigned eye = 0; eye < 2; ++eye) {
            auto *output = g_stereo.eye[g_stereo.eye_count-2+eye].tex[SLOT_OUTPUT];
            Barrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            StereoCopyRegion(g.tex12[SLOT_OUTPUT], eye * (g.width/2)+g_stereo.crop_x_eye[eye], g_stereo.crop_y, output, 0, 0, g_stereo.width, g_stereo.height);
            Barrier(output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        Barrier(g.tex12[SLOT_OUTPUT], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    QueryPerformanceCounter(&end); g_last_eval_ticks = end.QuadPart - begin.QuadPart;
    return result;
}
