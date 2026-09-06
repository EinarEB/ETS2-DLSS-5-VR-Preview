// SPDX-License-Identifier: MIT
// Direct driver ABI for the ETS2 preview. No NVIDIA SDK implementation is linked.
// Parameter interface and driver entry-point signatures adapted from NIGos
// dlss5-bridge, commit 5e4bccfc88d60676641ca5af7c10197dd420d144 (MIT).
// Copyright (c) 2026 NIGos; additions Copyright (c) 2026 ETS2 VR preview contributors.
// Integer/key names are the interoperation protocol. Requires Windows x64/MSVC.
#pragma once
#include <mutex>
#include <string>

using NVSDK_NGX_Result = int;
using NVSDK_NGX_Feature = int;
using NVSDK_NGX_PerfQuality_Value = int;
constexpr int NVSDK_NGX_Version_API = 0x15;
constexpr int NVSDK_NGX_ENGINE_TYPE_CUSTOM = 0;
constexpr int NVSDK_NGX_Feature_SuperSampling = 1;
constexpr int NVSDK_NGX_Result_Success = 1;
constexpr int NVSDK_NGX_Result_Fail = static_cast<int>(0xBAD00000u);
constexpr int FeedDriverUnavailable = static_cast<int>(0xBAD00007u);
inline bool NVSDK_NGX_FAILED(int r) { return r != 1; }
inline bool NVSDK_NGX_SUCCEED(int r) { return r == 1; }
constexpr int NVSDK_NGX_PerfQuality_Value_MaxPerf=0, NVSDK_NGX_PerfQuality_Value_Balanced=1,
    NVSDK_NGX_PerfQuality_Value_MaxQuality=2, NVSDK_NGX_PerfQuality_Value_UltraPerformance=3,
    NVSDK_NGX_PerfQuality_Value_UltraQuality=4, NVSDK_NGX_PerfQuality_Value_DLAA=5;
constexpr int NVSDK_NGX_DLSS_Feature_Flags_IsHDR=1, NVSDK_NGX_DLSS_Feature_Flags_MVLowRes=2,
    NVSDK_NGX_DLSS_Feature_Flags_DepthInverted=8, NVSDK_NGX_DLSS_Feature_Flags_AutoExposure=64;
using PFN_NVSDK_NGX_ProgressCallback = void(__cdecl*)(float,bool&);
struct NVSDK_NGX_Handle { unsigned int Id; };
// Declaration order is ABI-critical with MSVC's overloaded virtual methods.
struct NVSDK_NGX_Parameter {
    virtual void Set(const char*,unsigned long long)=0;
    virtual void Set(const char*,float)=0;
    virtual void Set(const char*,double)=0;
    virtual void Set(const char*,unsigned int)=0;
    virtual void Set(const char*,int)=0;
    virtual void Set(const char*,ID3D11Resource*)=0;
    virtual void Set(const char*,ID3D12Resource*)=0;
    virtual void Set(const char*,void*)=0;
    virtual int Get(const char*,unsigned long long*)const=0;
    virtual int Get(const char*,float*)const=0;
    virtual int Get(const char*,double*)const=0;
    virtual int Get(const char*,unsigned int*)const=0;
    virtual int Get(const char*,int*)const=0;
    virtual int Get(const char*,ID3D11Resource**)const=0;
    virtual int Get(const char*,ID3D12Resource**)const=0;
    virtual int Get(const char*,void**)const=0;
    virtual void Reset()=0;
};
// Local Feeder bookkeeping only, NEVER passed to the driver's opaque Init argument.
// This preview resolves model DLLs beside the copied game EXE; setup enforces it.
struct NVSDK_NGX_FeatureCommonInfo {
    struct {const wchar_t* const* Path; unsigned Length;} PathListInfo;
};
#define FEED_NGX_KEY(name,value) inline constexpr const char* NVSDK_NGX_Parameter_##name=value
FEED_NGX_KEY(SuperSampling_Available,"SuperSampling.Available");
FEED_NGX_KEY(SuperSamplingDenoising_Available,"SuperSamplingDenoising.Available");
FEED_NGX_KEY(SuperSampling_NeedsUpdatedDriver,"SuperSampling.NeedsUpdatedDriver");
FEED_NGX_KEY(SuperSampling_MinDriverVersionMajor,"SuperSampling.MinDriverVersionMajor");
FEED_NGX_KEY(SuperSampling_MinDriverVersionMinor,"SuperSampling.MinDriverVersionMinor");
FEED_NGX_KEY(DLSS_Hint_Render_Preset_DLAA,"DLSS.Hint.Render.Preset.DLAA");
FEED_NGX_KEY(DLSS_Hint_Render_Preset_UltraQuality,"DLSS.Hint.Render.Preset.UltraQuality");
FEED_NGX_KEY(DLSS_Hint_Render_Preset_Quality,"DLSS.Hint.Render.Preset.Quality");
FEED_NGX_KEY(DLSS_Hint_Render_Preset_Balanced,"DLSS.Hint.Render.Preset.Balanced");
FEED_NGX_KEY(DLSS_Hint_Render_Preset_Performance,"DLSS.Hint.Render.Preset.Performance");
FEED_NGX_KEY(DLSS_Hint_Render_Preset_UltraPerformance,"DLSS.Hint.Render.Preset.UltraPerformance");
#undef FEED_NGX_KEY

namespace feed_driver {
using Init=int(__cdecl*)(unsigned long long,const wchar_t*,ID3D12Device*,int,const void*);
using InitProject=int(__cdecl*)(const char*,int,const char*,const wchar_t*,ID3D12Device*,int,const void*);
using Allocate=int(__cdecl*)(NVSDK_NGX_Parameter**);
using Destroy=int(__cdecl*)(NVSDK_NGX_Parameter*);
using Create=int(__cdecl*)(ID3D12GraphicsCommandList*,int,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using Evaluate=int(__cdecl*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using Release=int(__cdecl*)(NVSDK_NGX_Handle*);
struct Api {
    HMODULE module=nullptr; std::wstring path; DWORD error=0; bool ready=false;
    Init init=nullptr; InitProject project=nullptr; Allocate allocate=nullptr,capabilities=nullptr;
    Destroy destroy=nullptr; Create create=nullptr;
    Evaluate evaluate=nullptr; Release release=nullptr;
};
template<class T> inline bool Resolve(HMODULE m,const char* name,T& p) {
    p=reinterpret_cast<T>(GetProcAddress(m,name)); return p!=nullptr;
}
inline Api& Get() {
    static Api* a=new Api;
    static std::once_flag once;
    std::call_once(once,[] {
        wchar_t directory[32768]={}; DWORD bytes=sizeof(directory);
        LSTATUS s=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
            L"FullPath",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,directory,&bytes);
        if(s!=ERROR_SUCCESS){a->error=s;return;}
        // A driver registration must be an absolute local path; no search-path fallback.
        if(wcslen(directory)<3 || directory[1]!=L':' || directory[2]!=L'\\'){
            a->error=ERROR_BAD_PATHNAME;return;
        }
        a->path=directory;
        if(a->path.back()!=L'\\')a->path+=L'\\';
        a->path+=L"_nvngx.dll";
        a->module=LoadLibraryExW(a->path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!a->module){a->error=GetLastError();return;}
        bool ok=true;
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_Init_Ext",a->init);
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_Init_ProjectID",a->project);
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_AllocateParameters",a->allocate);
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_GetCapabilityParameters",a->capabilities);
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_DestroyParameters",a->destroy);
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_CreateFeature",a->create);
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_EvaluateFeature",a->evaluate);
        ok &= Resolve(a->module,"NVSDK_NGX_D3D12_ReleaseFeature",a->release);
        a->ready=ok; a->error=ok?0:ERROR_PROC_NOT_FOUND;
        // Retained for the process lifetime: observers can hold pointers into this DLL.
    });
    return *a;
}
}
inline int NVSDK_NGX_D3D12_Init(unsigned long long id,const wchar_t* data,ID3D12Device* d,const NVSDK_NGX_FeatureCommonInfo*,int version) {
    auto& a=feed_driver::Get(); return a.ready?a.init(id,data,d,version,nullptr):FeedDriverUnavailable;
}
inline int NVSDK_NGX_D3D12_Init_with_ProjectID(const char* id,int engine,const char* ver,const wchar_t* data,ID3D12Device* d,const NVSDK_NGX_FeatureCommonInfo*,int version) {
    auto& a=feed_driver::Get(); return a.ready?a.project(id,engine,ver,data,d,version,nullptr):FeedDriverUnavailable;
}
inline int NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter** p) {auto& a=feed_driver::Get();if(p)*p=nullptr;return a.ready?a.allocate(p):FeedDriverUnavailable;}
inline int NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter** p) {auto& a=feed_driver::Get();if(p)*p=nullptr;return a.ready?a.capabilities(p):FeedDriverUnavailable;}
inline int NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter* p) {auto& a=feed_driver::Get();return a.ready?a.destroy(p):FeedDriverUnavailable;}
inline int NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle* h) {auto& a=feed_driver::Get();return a.ready?a.release(h):FeedDriverUnavailable;}

// Local input descriptions; only the parameter interface crosses the driver ABI.
struct NVSDK_NGX_DLSS_Create_Params {
    struct {unsigned InWidth,InHeight,InTargetWidth,InTargetHeight;int InPerfQualityValue;} Feature;
    int InFeatureCreateFlags; bool InEnableOutputSubrects;
};
struct NVSDK_NGX_D3D12_DLSS_Eval_Params {
    struct {ID3D12Resource* pInColor;ID3D12Resource* pInOutput;float InSharpness;} Feature;
    ID3D12Resource *pInDepth,*pInMotionVectors,*pInBiasCurrentColorMask;
    float InJitterOffsetX,InJitterOffsetY; struct{unsigned Width,Height;} InRenderSubrectDimensions;
    int InReset; float InMVScaleX,InMVScaleY,InPreExposure,InExposureScale;
};
inline int NGX_D3D12_CREATE_DLSS_EXT(ID3D12GraphicsCommandList* l,unsigned creation,unsigned visible,NVSDK_NGX_Handle** h,NVSDK_NGX_Parameter* p,NVSDK_NGX_DLSS_Create_Params* c) {
    auto& a=feed_driver::Get(); if(!a.ready)return FeedDriverUnavailable;
    if(!l||!h||!p||!c)return static_cast<int>(0xBAD00005u);
    *h=nullptr;
    p->Set("Width",c->Feature.InWidth);p->Set("Height",c->Feature.InHeight);
    p->Set("OutWidth",c->Feature.InTargetWidth);p->Set("OutHeight",c->Feature.InTargetHeight);
    p->Set("PerfQualityValue",c->Feature.InPerfQualityValue);
    p->Set("DLSS.Feature.Create.Flags",c->InFeatureCreateFlags);
    p->Set("DLSS.Enable.Output.Subrects",unsigned(c->InEnableOutputSubrects));
    p->Set("CreationNodeMask",creation);p->Set("VisibilityNodeMask",visible);p->Set("RTXValue",0);
    return a.create(l,1,p,h);
}
inline int NGX_D3D12_EVALUATE_DLSS_EXT(ID3D12GraphicsCommandList* l,const NVSDK_NGX_Handle* h,NVSDK_NGX_Parameter* p,NVSDK_NGX_D3D12_DLSS_Eval_Params* e) {
    auto& a=feed_driver::Get(); if(!a.ready)return FeedDriverUnavailable;
    if(!l||!h||!p||!e)return static_cast<int>(0xBAD00005u);
    p->Set("Color",e->Feature.pInColor);p->Set("Output",e->Feature.pInOutput);
    p->Set("Depth",e->pInDepth);p->Set("MotionVectors",e->pInMotionVectors);
    p->Set("DLSS.Input.Bias.Current.Color.Mask",e->pInBiasCurrentColorMask);
    p->Set("Jitter.Offset.X",e->InJitterOffsetX);p->Set("Jitter.Offset.Y",e->InJitterOffsetY);
    p->Set("MV.Scale.X",e->InMVScaleX);p->Set("MV.Scale.Y",e->InMVScaleY);
    p->Set("Sharpness",e->Feature.InSharpness);p->Set("Reset",e->InReset);
    p->Set("DLSS.Pre.Exposure",e->InPreExposure);p->Set("DLSS.Exposure.Scale",e->InExposureScale);
    p->Set("DLSS.Render.Subrect.Dimensions.Width",e->InRenderSubrectDimensions.Width);
    p->Set("DLSS.Render.Subrect.Dimensions.Height",e->InRenderSubrectDimensions.Height);
    for(const char* k:{"DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y",
        "DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y",
        "DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y",
        "DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"})p->Set(k,0u);
    return a.evaluate(l,h,p,nullptr);
}
// The public preview uses equal-resolution stereo DLAA carriers only. Generic
// SR optimal-settings negotiation is deliberately unavailable in this variant.
inline int NGX_DLSS_GET_OPTIMAL_SETTINGS(NVSDK_NGX_Parameter*,unsigned,unsigned,int,
    unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,float*) {
    return static_cast<int>(0xBAD00012u);
}
