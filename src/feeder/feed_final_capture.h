#pragma once
#ifndef ETS2_FEED_SEQUENCE_REPLAY
#error Final effect sequence capture belongs only to the offline replay build.
#endif

// Deliberately blocking diagnostic, never included in the installed build.
// Records the target AFTER all ReShade techniques, tied to the successful
// replay input index. Original Feeder captures retain their earlier meaning.
namespace ets2_final_capture {
using Microsoft::WRL::ComPtr;
inline std::string LookSnapshot(reshade::api::effect_runtime* rt){
    std::ostringstream j;j<<'{';bool comma=false;
    auto key=[&](const char* name){if(comma)j<<',';comma=true;j<<ets2_capture::Quote(name)<<':';};
    for(auto pair:{std::make_pair("PD80_04_Contrast_Brightness_Saturation.fx","prod80_04_ContrastBrightnessSaturation"),std::make_pair("PD80_04_Color_Temperature.fx","prod80_04_ColorTemperature")}){
        auto tech=rt->find_technique(pair.first,pair.second);key(pair.second);j<<((tech.handle&&rt->get_technique_state(tech))?"true":"false");
    }
    for(auto name:{"enable_dither","enable_depth","display_depth"}){auto v=rt->find_uniform_variable("PD80_04_Contrast_Brightness_Saturation.fx",name);key(name);if(v.handle){bool b=false;rt->get_uniform_value_bool(v,&b,1);j<<(b?"true":"false");}else j<<"null";}
    for(auto name:{"contrast","brightness","saturation","vibrance"}){auto v=rt->find_uniform_variable("PD80_04_Contrast_Brightness_Saturation.fx",name);key(name);if(v.handle){float f=0;rt->get_uniform_value_float(v,&f,1);if(std::isfinite(f))j<<f;else j<<"null";}else j<<"null";}
    for(auto name:{"kMix","LumPreservation"}){auto v=rt->find_uniform_variable("PD80_04_Color_Temperature.fx",name);key(name);if(v.handle){float f=0;rt->get_uniform_value_float(v,&f,1);if(std::isfinite(f))j<<f;else j<<"null";}else j<<"null";}
    auto v=rt->find_uniform_variable("PD80_04_Color_Temperature.fx","Kelvin");key("Kelvin");if(v.handle){unsigned k=0;rt->get_uniform_value_uint(v,&k,1);j<<k;}else j<<"null";
    return j.str()+"}";
}
struct Capture {
    bool configured=false,enabled=false,failed=false;
    unsigned written=0;
    std::filesystem::path folder;
    ComPtr<ID3D11Texture2D> staging;
    std::vector<unsigned char> pixels;
    std::ostringstream frames;
    bool Enabled(){if(!configured){configured=true;char v[8]{};enabled=GetEnvironmentVariableA("ETS2_CAPTURE_FINAL_EFFECTS",v,sizeof(v))&&v[0]=='1';}return enabled&&!failed;}
    void Run(reshade::api::effect_runtime* rt,reshade::api::command_list* list,reshade::api::resource_view rtv){
        if(!Enabled()||rt!=g.runtime||rt->get_hwnd()!=nullptr||rt->get_device()->get_api()!=reshade::api::device_api::d3d11)return;
        auto& replay=ets2_sequence_replay::Get();
        if(!replay.loaded||!replay.index||replay.index==written)return;
        if(replay.index!=written+1)throw std::runtime_error("final capture missed a successful replay delivery");
        auto* ctx=reinterpret_cast<ID3D11DeviceContext*>(list->get_native());
        auto* view=reinterpret_cast<ID3D11RenderTargetView*>(rtv.handle);
        if(!ctx||!view)throw std::runtime_error("final capture target missing");
        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;
        if(FAILED(resource.As(&texture)))throw std::runtime_error("final capture is not a 2D texture");
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);D3D11_RENDER_TARGET_VIEW_DESC vd{};view->GetDesc(&vd);
        if(desc.Width!=replay.width||desc.Height!=replay.height||desc.ArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||
           (desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))throw std::runtime_error("final capture texture contract differs");
        if(folder.empty()){
            folder=std::filesystem::path(g_log_path).parent_path()/"Final-Effects";std::filesystem::create_directory(folder);
            if(std::filesystem::exists(folder/"sequence.json"))throw std::runtime_error("final capture refuses to overwrite a prior sequence");
            auto copy=desc;copy.Usage=D3D11_USAGE_STAGING;copy.BindFlags=copy.MiscFlags=0;copy.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Device> device;ctx->GetDevice(&device);
            if(FAILED(device->CreateTexture2D(&copy,nullptr,&staging)))throw std::runtime_error("final capture staging allocation failed");
            pixels.resize(size_t(desc.Width)*desc.Height*4);
        }
        {
            ets2_d3d11::Scope scope(g_private11,ctx);if(!scope)throw std::runtime_error("final capture isolation unavailable");
            ctx->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            if(FAILED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))throw std::runtime_error("final capture Map failed");
            for(UINT y=0;y<desc.Height;++y)memcpy(pixels.data()+size_t(y)*desc.Width*4,static_cast<const unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(desc.Width)*4);
            ctx->Unmap(staging.Get(),0);
        }
        const auto name="frame-"+std::to_string(written)+".rgba";
        if(!ets2_capture::AtomicWrite(folder/name,pixels.data(),pixels.size()))throw std::runtime_error("final capture image write failed");
        const auto hash=ets2_capture::Sha256(pixels);if(hash.size()!=64)throw std::runtime_error("final capture hash failed");
        if(written)frames<<',';
        frames<<"{\"index\":"<<written<<",\"delivered_frame\":"<<g.frames_done<<",\"source_frame\":"<<replay.frames[written].sourceFrame<<",\"file\":"<<ets2_capture::Quote(name)<<",\"sha256\":"<<ets2_capture::Quote(hash)<<",\"bytes\":"<<pixels.size()<<",\"format\":"<<unsigned(desc.Format)<<",\"view_format\":"<<unsigned(vd.Format)<<",\"look_controls\":"<<LookSnapshot(rt)<<"}";
        ++written;std::ostringstream manifest;
        manifest<<"{\"schema\":1,\"complete\":"<<(written==replay.count?"true":"false")<<",\"requested\":"<<replay.count<<",\"written\":"<<written<<",\"width\":"<<desc.Width<<",\"height\":"<<desc.Height<<",\"output_stage\":\"after all ReShade effects, before overlay and headset compositor\",\"blocking_capture_changes_pacing\":true,\"frames\":["<<frames.str()<<"]}\n";
        if(!ets2_capture::AtomicText(folder/"sequence.json",manifest.str()))throw std::runtime_error("final capture manifest failed");
    }
};
inline Capture& Get(){static auto* capture=new Capture;return *capture;}
}
static void OnFinishEffectsForSequence(reshade::api::effect_runtime* rt,reshade::api::command_list* list,reshade::api::resource_view rtv,reshade::api::resource_view){
    if(!FeedEnter())return;
    try{ets2_final_capture::Get().Run(rt,list,rtv);}catch(const std::exception& e){ets2_final_capture::Get().failed=true;Log("[final-effects-capture] rejected: %s",e.what());}
    FeedLeave();
}
