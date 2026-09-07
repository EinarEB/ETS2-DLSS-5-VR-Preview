#pragma once
#include <d3d11_1.h>
#include <bcrypt.h>
#include <map>
#include <memory>
#include <functional>
#include <sstream>
#include <set>
#include <cstdlib>
#include "ets2_rain_depth.hpp"

// Game-specific route observed in four real ETS2 VR frames. Save depth while
// co-bound with the final HDR scene, then replay ONLY depth into owned targets
// through the game's actual fullscreen geometry and vertex constants. Color
// is never replaced here; final eye order still requires exact pixel matching.
namespace ets2_route {
using Microsoft::WRL::ComPtr;
enum class Shader { unknown, tone, copy, ui, pre_taa, taa, post_taa };
inline std::string Hash(const void* code, size_t size) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    unsigned char hash[32]{};
    const auto status=BCryptHash(algorithm,nullptr,0,(PUCHAR)code,(ULONG)size,hash,sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)return {};
    static const char hex[]="0123456789abcdef";
    std::string text; for(auto b:hash){text+=hex[b>>4];text+=hex[b&15];}return text;
}
inline Shader Classify(const void* code,size_t size) {
    if(size!=5484 && size!=5596 && size!=6012 && size!=6124 && size!=292 && size!=424 &&
       size!=600 && size!=9424 && size!=9740 && size!=9896 && size!=9580 && size!=1636 && size!=3616 && size!=4144 && size!=4256 && size!=7496 && size!=9240)return Shader::unknown;
    const auto hash=Hash(code,size);
    if(RainToneMode(hash))return Shader::tone;
    if(hash=="f98057b680db40ab5f7e0659937fea9189454a690ac2cda59208a8c27200b6f7")return Shader::tone;
    // Exact game-archive variants: shaft adds a same-UV light sample, col adds
    // pointwise RGB correction. The t0 coordinates and existing DRR sampling
    // branch are unchanged; replay still follows the actual bound vertex data.
    // Unreviewed DOF/distortion variants remain rejected. The two explicit rain
    // variants above use a separate coordinate replay, never this plain route.
    if(hash=="879c35aee7c1979dedc90a6ac99be39de3d0eec3c72ecfdd9e204d46e0fc5e8b")return Shader::tone;
    if(hash=="25bba17c86455f666702803c2b80a4f8385fe2fb63e5fdc5e226e5b473c265c4")return Shader::tone;
    if(hash=="b10fcba08c19508c03419bf9ab273eb690e3350f8260e6b0d1bf2c8b2d16420f")return Shader::tone;
    if(hash=="f8262196e3fe80dae9b7e8e10d635120e4c454b60f6b45b58bec38ad6731c22d")return Shader::copy;
    if(hash=="e10bb0dc2040752eb6b580ea60adc8842ae5f097513b03c6c7a5dab7d0c06925")return Shader::ui;
    if(hash=="14055a3b377bd037828f1b2b565194492e70ca7551eb19e023c77f324622f02c")return Shader::tone;
    // Exact non-DRR col variant adds pointwise RGB arithmetic only.
    if(hash=="42ab205e58a5590828574f91b1d56e86b11080024f2545439aa1bf659533cca9")return Shader::tone;
    // Exact non-DRR col+shaft variant observed in the 02:00 game scene.
    // Scene, bloom and shaft samples all use unchanged TEXCOORD0.xy;
    // exposure/color arithmetic does not move the scene's depth coordinates.
    if(hash=="e60c7f232bb8ac1a008d4e36a2d8de932f2a08657af57a6c73da0c246c643d77")return Shader::tone;
    if(hash=="72ed8c252903b0c5f6a81a101432e73415401fa44b332e257d2be32c4eba44de")return Shader::pre_taa;
    if(hash=="b5288c9497cc816f8ff8d3e3c046921b6beb5b5cf6425a0979386002daea2bb4")return Shader::post_taa;
    // Ordinary stock TAA variants share current t1 and CB0[8].xy unjitter.
    // Debug variants have a different CB layout and remain unsupported.
    if(hash=="414b3cf32769472d606cb3bb979fa0d272df96a8d96ab47ce7e362d4effc0874"||
       hash=="f0fb529590d289cf76cd2ac0539bb8b27cece7788c2e143f746ad4f2cb457fc7"||
       hash=="88c696a72d1d01bd86bad5237f8433e0facbaef3fc0810353dc87285924af15e"||
       hash=="ee6add879d2288ea7cbdb76a2881b8350a76845842b743eb19ad3f02918f18e5"||
       hash=="cf6590b6305ce610a2c1ba540b60a5883e0e09e566286858f52915c206a2ee05"||
       hash=="d79cca79b6db1650a4d6d22f9e3c73d1d4489166da3d6747b76fd7c30dd5d2bb")return Shader::taa;
    return Shader::unknown;
}
struct Texture {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
};
struct GraphicsSave {
    ID3D11DeviceContext* c;
    ID3D11RenderTargetView* rt[8]{};
    ID3D11DepthStencilView* ds=nullptr;
    ID3D11UnorderedAccessView* uav[64]{};
    UINT slots=8,rtCount=8;
    ComPtr<ID3D11PixelShader> ps;
    ID3D11ClassInstance* instances[256]{};UINT instanceCount=256;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask=0;
    ComPtr<ID3D11DepthStencilState> depth;UINT stencil=0;
    explicit GraphicsSave(ID3D11DeviceContext* ctx):c(ctx) {
        ComPtr<ID3D11Device> d;c->GetDevice(&d);if(d->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1)slots=64;
        c->OMGetRenderTargets(8,rt,&ds);
        while(rtCount && !rt[rtCount-1])--rtCount;
        c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,slots,uav);
        c->PSGetShader(&ps,instances,&instanceCount);
        c->PSGetShaderResources(0,1,&srv);c->PSGetSamplers(0,1,&sampler);
        c->OMGetBlendState(&blend,factors,&mask);c->OMGetDepthStencilState(&depth,&stencil);
    }
    void UnbindOutputs(){ID3D11UnorderedAccessView* none[64]{};UINT keep[64];for(auto& n:keep)n=UINT(-1);c->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,slots,none,keep);}
    ~GraphicsSave(){
        UnbindOutputs();
        UINT keep[64];for(auto& n:keep)n=UINT(-1);
        // Render interposers can track OMSetRenderTargets independently of the
        // combined UAV setter. Restore the same binding through that entry point
        // before restoring the complete UAV state and preserving its counters.
        c->OMSetRenderTargets(rtCount,rt,ds);
        c->OMSetRenderTargetsAndUnorderedAccessViews(rtCount,rt,ds,rtCount,slots-rtCount,uav+rtCount,keep+rtCount);
        c->OMSetBlendState(blend.Get(),factors,mask);c->OMSetDepthStencilState(depth.Get(),stencil);
        c->PSSetShader(ps.Get(),instances,instanceCount);
        auto* restoreSRV=srv.Get();c->PSSetShaderResources(0,1,&restoreSRV);auto* s=sampler.Get();c->PSSetSamplers(0,1,&s);
        for(auto* v:rt)if(v)v->Release();if(ds)ds->Release();for(auto* v:uav)if(v)v->Release();
        for(UINT i=0;i<instanceCount;++i)if(instances[i])instances[i]->Release();
    }
};
class Tracker {
    struct Snapshot {ComPtr<ID3D11Texture2D> color;Texture depth;bool valid=false,colorCurrent=false;};
    struct Link {ComPtr<ID3D11Texture2D> color;Texture depth;int source=-1,stage=0;bool valid=false;};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11PixelShader> depthPS,taaDepthPS;
    ComPtr<ID3D11PixelShader> rainDepthPS[2];
    ComPtr<ID3D11SamplerState> pointSampler;
    // Snowymoon A/B/C can render an extra full-size HDR scene for each eye.
    // Retain those independent candidates until the verified final routes
    // select their actual sources. Capacity is not an eye-order assumption.
    static constexpr size_t MaxSceneSnapshots=4;
    std::array<Snapshot,MaxSceneSnapshots> snapshots;
    std::array<Link,8> links;
    ComPtr<ID3D11Texture2D> boundColor,boundDepth;
    bool boundHDR=false,broken=false;
    unsigned draws=0,w=0,h=0;
    struct ShaderInfo {Shader kind=Shader::unknown;std::string hash;size_t size=0;std::vector<unsigned char> bytes;};
    std::map<uint64_t,ShaderInfo> shaders;
    size_t cachedShaderBytes=0,diagnosticBytes=0;
    unsigned routeDraws[7]{},replays[2]{},invalidations=0,clears=0;
    std::map<std::string,unsigned> reasons;
    std::vector<std::string> events;
    std::string replayFailure;
    uint64_t currentPipeline=0,sequence=0;
    void Event(const char* reason,ID3D11Texture2D* source=nullptr,unsigned vertices=0,unsigned first=0) {
        ++reasons[reason];
        if(events.size()>=32)return;
        std::ostringstream j;j<<"{\"sequence\":"<<sequence<<",\"reason\":\""<<reason<<"\",\"color\":"<<reinterpret_cast<uint64_t>(boundColor.Get())<<",\"depth\":"<<reinterpret_cast<uint64_t>(boundDepth.Get())<<",\"source\":"<<reinterpret_cast<uint64_t>(source)<<",\"ps\":"<<currentPipeline<<",\"vertices\":"<<vertices<<",\"first\":"<<first<<"}";events.push_back(j.str());
    }
    bool RejectReplay(const char* reason){replayFailure=reason;return false;}
    Texture Make(DXGI_FORMAT format,bool output) {
        Texture t;D3D11_TEXTURE2D_DESC td{};
        td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=format;td.SampleDesc.Count=1;
        td.BindFlags=D3D11_BIND_SHADER_RESOURCE|(output?D3D11_BIND_RENDER_TARGET:0);
        depth_match::Check(device->CreateTexture2D(&td,nullptr,&t.tex),"route texture");
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
        sv.Format=output?DXGI_FORMAT_R32_FLOAT:depth_match::DepthViewFormat(format);
        depth_match::Check(device->CreateShaderResourceView(t.tex.Get(),&sv,&t.srv),"route SRV");
        if(output)depth_match::Check(device->CreateRenderTargetView(t.tex.Get(),nullptr,&t.rtv),"route RTV");
        return t;
    }
    bool Extent(ID3D11Texture2D* t) const {
        if(!t||!w||!h)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
        return d.Width==w&&d.Height==h&&d.MipLevels==1&&d.ArraySize==1&&d.SampleDesc.Count==1;
    }
    static ComPtr<ID3D11Texture2D> Resource(ID3D11View* view) {
        ComPtr<ID3D11Texture2D> t; if(view){ComPtr<ID3D11Resource> r;view->GetResource(&r);r.As(&t);}return t;
    }
    void SnapshotBound(ID3D11DeviceContext* c) {
        if(!boundHDR||!draws||!Extent(boundColor.Get())||!Extent(boundDepth.Get()))return;
        draws=0;
        ComPtr<ID3D11Predicate> predicate;BOOL predicateValue=FALSE;c->GetPredication(&predicate,&predicateValue);
        if(predicate){broken=true;Event("predicated_snapshot_not_proven");return;}
        Snapshot* found=nullptr;
        for(auto& s:snapshots)if(s.valid&&s.color.Get()==boundColor.Get()){found=&s;break;}
        if(found){
            const int identity=int(found-snapshots.data());
            for(const auto& link:links)if(link.valid&&link.source==identity){broken=true;Event("scene_snapshot_rewritten_after_use");return;}
        }
        if(!found)for(auto& s:snapshots)if(!s.valid){found=&s;break;}
        if(!found){broken=true;Event("snapshot_overflow");return;}
        D3D11_TEXTURE2D_DESC desc{};boundDepth->GetDesc(&desc);auto fmt=depth_match::DepthStorage(desc.Format);
        if(fmt==DXGI_FORMAT_UNKNOWN){broken=true;Event("unsupported_depth_format");return;}
        D3D11_TEXTURE2D_DESC old{};if(found->depth.tex)found->depth.tex->GetDesc(&old);
        if(!found->depth.tex||old.Format!=fmt)found->depth=Make(fmt,false);
        c->CopyResource(found->depth.tex.Get(),boundDepth.Get());found->color=boundColor;found->valid=found->colorCurrent=true;Event("snapshot_saved");
    }
    bool Replay(ID3D11DeviceContext* c,ID3D11ShaderResourceView* source,Texture& dest,const std::function<void()>& draw,bool taa=false,int rain=0) {
        UINT count=16;D3D11_VIEWPORT vp[16]{};c->RSGetViewports(&count,vp);
        if(count!=1||vp[0].TopLeftX!=0||vp[0].TopLeftY!=0||vp[0].Width!=float(w)||vp[0].Height!=float(h))return RejectReplay("viewport_mismatch");
        // Unusual geometry stages/stream output could have side effects on a replay.
        ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
        c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);
        ID3D11Buffer* so[4]{};c->SOGetTargets(4,so);bool hasSO=false;for(auto* b:so)if(b){hasSO=true;b->Release();}
        if(gs)return RejectReplay("geometry_shader_bound");if(hs)return RejectReplay("hull_shader_bound");if(ds)return RejectReplay("domain_shader_bound");if(hasSO)return RejectReplay("stream_output_bound");
        ComPtr<ID3D11Predicate> predicate;BOOL predicateValue=FALSE;c->GetPredication(&predicate,&predicateValue);
        if(predicate)return RejectReplay("predicated_draw_not_proven");
        if(rain){
            ComPtr<ID3D11DeviceContext1> c1;ComPtr<ID3D11Buffer> constants;UINT first=0,countConstants=0;
            if(FAILED(c->QueryInterface(IID_PPV_ARGS(&c1))))return RejectReplay("rain_constant_slice_unavailable");
            c1->PSGetConstantBuffers1(0,1,&constants,&first,&countConstants);
            D3D11_BUFFER_DESC bd{};if(constants)constants->GetDesc(&bd);const UINT required=rain==1?23:24;
            if(!constants||countConstants<required||(uint64_t(first)+required)*16>bd.ByteWidth)return RejectReplay("rain_constant_slice_invalid");
            for(UINT slot:{2u,12u,14u}){
                ComPtr<ID3D11ShaderResourceView> view;c->PSGetShaderResources(slot,1,&view);
                auto texture=Resource(view.Get());D3D11_SHADER_RESOURCE_VIEW_DESC vd{};if(view)view->GetDesc(&vd);
                if(!texture||!Extent(texture.Get())||vd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||
                   vd.Texture2D.MostDetailedMip!=0||vd.Texture2D.MipLevels!=1)return RejectReplay("rain_coordinate_resource_unproven");
            }
            ComPtr<ID3D11SamplerState> sampler;c->PSGetSamplers(2,1,&sampler);
            if(!sampler)return RejectReplay("rain_distortion_sampler_missing");
        }
        if(taa){
            ComPtr<ID3D11DeviceContext1> c1;ComPtr<ID3D11Buffer> constants;UINT first=0,countConstants=0;
            if(FAILED(c->QueryInterface(IID_PPV_ARGS(&c1))))return RejectReplay("TAA_constant_slice_unavailable");
            c1->PSGetConstantBuffers1(0,1,&constants,&first,&countConstants);
            D3D11_BUFFER_DESC bd{};if(constants)constants->GetDesc(&bd);
            if(!constants||countConstants<14||uint64_t(first+14)*16>bd.ByteWidth)return RejectReplay("TAA_constant_slice_invalid");
        }
        if(!depthPS){
            const char* code="Texture2D<float> Depth:register(t0); SamplerState Point:register(s0); float main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {return Depth.SampleLevel(Point,uv,0);}";
            ComPtr<ID3DBlob> blob,error;
            depth_match::Check(D3DCompile(code,strlen(code),"ets2-depth-route",nullptr,nullptr,"main","ps_5_0",0,0,&blob,&error),"route shader");
            depth_match::Check(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&depthPS),"route PS");
            D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
            sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
            depth_match::Check(device->CreateSamplerState(&sd,&pointSampler),"route sampler");
        }
        if(!dest.tex)dest=Make(DXGI_FORMAT_R32_FLOAT,true);
        if(taa&&!taaDepthPS){
            // Keep the game's active PS CB0 slice bound. Its unjitter offset
            // maps nominal TAA output geometry to the current scene sample.
            // No previous/packed TAA depth is substituted for raw scene depth.
            const char* code="Texture2D<float> Depth:register(t0); SamplerState Point:register(s0); cbuffer Taa:register(b0){float4 data[14];} float main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {return Depth.SampleLevel(Point,uv+data[8].xy,0);}";
            ComPtr<ID3DBlob> blob,error;depth_match::Check(D3DCompile(code,strlen(code),"ets2-taa-current-depth",nullptr,nullptr,"main","ps_5_0",0,0,&blob,&error),"TAA depth shader");
            depth_match::Check(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&taaDepthPS),"TAA depth PS");
        }
        if(rain&&!rainDepthPS[rain-1]){
            const D3D_SHADER_MACRO defines[]={{"RAIN_CB",rain==1?"12":"13"},{nullptr,nullptr}};
            const char* code=RainDepthShader();ComPtr<ID3DBlob> blob,error;
            depth_match::Check(D3DCompile(code,strlen(code),"ets2-rain-nominal-depth",defines,nullptr,"main","ps_5_0",0,0,&blob,&error),"rain depth shader");
            depth_match::Check(device->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&rainDepthPS[rain-1]),"rain depth PS");
        }
        GraphicsSave save(c);save.UnbindOutputs();
        // The recorded ETS2 eye depth is reversed Z and clears to zero.
        float farDepth[4]={0,0,0,0};c->ClearRenderTargetView(dest.rtv.Get(),farDepth);
        auto* r=dest.rtv.Get();c->OMSetRenderTargets(1,&r,nullptr);c->OMSetBlendState(nullptr,nullptr,UINT(-1));c->OMSetDepthStencilState(nullptr,0);
        c->PSSetShader(rain?rainDepthPS[rain-1].Get():taa?taaDepthPS.Get():depthPS.Get(),nullptr,0);c->PSSetShaderResources(0,1,&source);
        auto* sampler=pointSampler.Get();c->PSSetSamplers(0,1,&sampler);
        draw();return true;
    }
public:
    void Fail(){broken=true;for(auto& l:links)l.valid=false;}
    unsigned SnapshotCount()const{unsigned n=0;for(auto& s:snapshots)n+=s.valid;return n;}
    unsigned OutputCount()const{unsigned n=0;for(auto& l:links)n+=l.valid&&l.stage==2;return n;}
    void SetDevice(ID3D11Device* d){if(!device)device=d;}
    void Pipeline(uint64_t p,const void* code,size_t size){
        DestroyPipeline(p);if(shaders.size()>=8192||size>1024*1024)return;
        ShaderInfo info;info.kind=Classify(code,size);info.hash=Hash(code,size);info.size=size;
        if(size<=16384&&cachedShaderBytes+size<=16*1024*1024){info.bytes.assign(static_cast<const unsigned char*>(code),static_cast<const unsigned char*>(code)+size);cachedShaderBytes+=size;}
        shaders[p]=std::move(info);
    }
    void DestroyPipeline(uint64_t p){auto it=shaders.find(p);if(it!=shaders.end()){cachedShaderBytes-=it->second.bytes.size();shaders.erase(it);}}
    void Extent(unsigned width,unsigned height){
        if(w==width&&h==height)return;
        w=width;h=height;snapshots={};links={};Reset();boundColor.Reset();boundDepth.Reset();boundHDR=false;
        if(!w||!h){depthPS.Reset();taaDepthPS.Reset();for(auto& shader:rainDepthPS)shader.Reset();pointSampler.Reset();device.Reset();}
    }
    void Reset(){for(auto& s:snapshots){s.valid=false;s.color.Reset();}for(auto& l:links){l.valid=false;l.color.Reset();}draws=0;broken=false;sequence=currentPipeline=0;std::fill(std::begin(routeDraws),std::end(routeDraws),0);std::fill(std::begin(replays),std::end(replays),0);invalidations=clears=0;reasons.clear();events.clear();}
    void Bind(ID3D11DeviceContext* c){
        ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> ds;
        c->OMGetRenderTargets(1,&rt,&ds);auto color=Resource(rt.Get());auto depth=Resource(ds.Get());
        ID3D11RenderTargetView* all[8]{};c->OMGetRenderTargets(8,all,nullptr);
        std::array<ComPtr<ID3D11Texture2D>,7> additional;
        bool one=true;for(unsigned i=0;i<8;++i){if(i&&all[i]){one=false;additional[i-1]=Resource(all[i]);}if(all[i])all[i]->Release();}
        bool hdr=false;
        if(Extent(color.Get())&&Extent(depth.Get())){
            D3D11_TEXTURE2D_DESC cd{};color->GetDesc(&cd);
            hdr=one&&cd.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        }
        const bool changed=color.Get()!=boundColor.Get()||depth.Get()!=boundDepth.Get()||hdr!=boundHDR;
        if(changed)SnapshotBound(c);
        // Only RT0 is followed. Binding an existing tracked color as an
        // additional writable output conservatively revokes its association,
        // even if that particular draw later masks writes or never executes.
        for(const auto& target:additional)if(target){
            for(auto& s:snapshots)if(s.color.Get()==target.Get())s.colorCurrent=false;
            for(auto& l:links)if(l.valid&&l.color.Get()==target.Get()){l.valid=false;Event("tracked_color_bound_as_additional_output",target.Get());}
        }
        if(!changed)return;
        boundColor=std::move(color);boundDepth=std::move(depth);boundHDR=hdr;draws=0;
    }
    void ClearDepth(ID3D11DeviceContext* c,ID3D11Resource* r){if(boundDepth.Get()==r)SnapshotBound(c);}
    void ClearColor(ID3D11Resource* r){
        // A consumed snapshot's depth is immutable for this epoch even when
        // the original color is reused as TAA scratch. Direct color lookup is
        // invalidated; derived current-frame links keep their source identity.
        for(auto& s:snapshots)if(s.color.Get()==r)s.colorCurrent=false;
        if(boundColor.Get()==r)draws=0;
        for(auto& l:links)if(l.color.Get()==r){if(l.valid){Event("derived_color_cleared",reinterpret_cast<ID3D11Texture2D*>(r),unsigned(l.stage),0);if(l.stage==2)++clears;}l.valid=false;}
    }
    void Draw(ID3D11DeviceContext* c,unsigned vertices,unsigned instances,unsigned firstVertex,unsigned firstInstance){
        ++sequence;
        if(boundHDR){++draws;return;}
        if(!Extent(boundColor.Get()))return;
        Link* destination=nullptr;
        for(auto& l:links)if(l.valid&&l.color.Get()==boundColor.Get()){destination=&l;break;}
        ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);
        auto found=shaders.find(reinterpret_cast<uint64_t>(ps.Get()));
        auto kind=found==shaders.end()?Shader::unknown:found->second.kind;currentPipeline=reinterpret_cast<uint64_t>(ps.Get());
        ++routeDraws[unsigned(kind)];
        bool trackedCurrent=false;for(const auto& s:snapshots)trackedCurrent=trackedCurrent||(s.valid&&s.colorCurrent&&s.color.Get()==boundColor.Get());
        UINT writeMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(destination||trackedCurrent||kind!=Shader::unknown){
            ComPtr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT sampleMask=0;c->OMGetBlendState(&blend,factors,&sampleMask);
            if(blend){D3D11_BLEND_DESC bd{};blend->GetDesc(&bd);writeMask=bd.RenderTarget[0].RenderTargetWriteMask;}
            // The stock post-tone indexed pass has RT0 writes disabled. It
            // cannot invalidate scene color, regardless of its depth/stencil use.
            if(writeMask==0){if(destination)Event("color_writes_disabled_preserved");return;}
        }
        for(auto& s:snapshots)if(s.color.Get()==boundColor.Get())s.colorCurrent=false;
        // Known UI PS overlays color; it does not remap the scene underneath.
        if(kind==Shader::ui && destination && destination->stage==2)return;
        if(destination&&kind==Shader::ui)Event("UI_invalidates_intermediate",nullptr,vertices,firstVertex);
        if(destination){Event("derived_color_draw_overwrite",nullptr,vertices,unsigned(destination->stage));if(destination->stage==2)++invalidations;destination->valid=false;}
        if(kind==Shader::unknown||kind==Shader::ui){if(!boundDepth&&vertices>=3&&vertices<=6)Event(found==shaders.end()?"shader_not_registered":"unknown_fullscreen_shader",nullptr,vertices,firstVertex);return;}
        if(writeMask!=D3D11_COLOR_WRITE_ENABLE_ALL){Event("partial_color_write_route_unsupported");return;}
        if(vertices>6||vertices<3||instances!=1||firstInstance!=0){Event("draw_arguments",nullptr,vertices,firstVertex);return;}
        if(boundDepth){Event("depth_still_bound",nullptr,vertices,firstVertex);return;}
        const UINT sourceSlot=(kind==Shader::taa||kind==Shader::post_taa)?1:0;
        ComPtr<ID3D11ShaderResourceView> sourceView;c->PSGetShaderResources(sourceSlot,1,&sourceView);
        auto source=Resource(sourceView.Get());if(!Extent(source.Get())||source.Get()==boundColor.Get()){Event("source_extent_or_alias",source.Get(),vertices,firstVertex);return;}
        D3D11_SHADER_RESOURCE_VIEW_DESC sourceDesc{};sourceView->GetDesc(&sourceDesc);
        if(sourceDesc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||sourceDesc.Texture2D.MostDetailedMip!=0||sourceDesc.Texture2D.MipLevels!=1){Event("source_view_subrange");return;}
        if(kind==Shader::taa){
            ID3D11RenderTargetView* views[8]{};c->OMGetRenderTargets(8,views,nullptr);
            auto history=Resource(views[1]);bool layout=views[0]&&views[1]&&Extent(history.Get())&&history.Get()!=boundColor.Get()&&history.Get()!=source.Get();
            for(unsigned i=2;i<8;++i)layout=layout&&!views[i];for(auto* view:views)if(view)view->Release();
            D3D11_TEXTURE2D_DESC hd{},cd{};if(history)history->GetDesc(&hd);boundColor->GetDesc(&cd);
            layout=layout&&hd.Format==DXGI_FORMAT_R16G16B16A16_FLOAT&&cd.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
            if(!layout){Event("TAA_two_output_layout");return;}
        }
        ID3D11ShaderResourceView* depth=nullptr;int identity=-1;
        if(kind==Shader::tone||kind==Shader::pre_taa){for(unsigned i=0;i<snapshots.size();++i)if(snapshots[i].valid&&snapshots[i].colorCurrent&&snapshots[i].color.Get()==source.Get()){depth=snapshots[i].depth.srv.Get();identity=int(i);}}
        const int required=kind==Shader::copy?1:kind==Shader::taa?3:kind==Shader::post_taa?4:kind==Shader::tone?5:-1;
        // With luma sharpening disabled, stock TAA's invert variant goes
        // directly into final tone. Both paths have the same nominal grid.
        if(!depth&&required>=0)for(auto& l:links)if(l.valid&&(l.stage==required||(kind==Shader::tone&&l.stage==4))&&l.color.Get()==source.Get()){depth=l.depth.srv.Get();identity=l.source;}
        if(!depth){Event("missing_current_scene_depth_link",source.Get(),vertices,firstVertex);return;}
        if(!destination)for(auto& l:links)if(!l.valid){destination=&l;break;}
        if(!destination){broken=true;Event("link_overflow");return;}
        const int rain=kind==Shader::tone?RainToneMode(found->second.hash):0;
        if(Replay(c,depth,destination->depth,[&]{c->DrawInstanced(vertices,instances,firstVertex,firstInstance);},kind==Shader::taa,rain)){
            destination->color=boundColor;destination->source=identity;destination->stage=kind==Shader::tone?1:kind==Shader::copy?2:kind==Shader::pre_taa?3:kind==Shader::taa?4:5;destination->valid=true;
            if(kind==Shader::tone||kind==Shader::copy)++replays[kind==Shader::tone?0:1];
            Event(rain?"rain_tonemap_nominal_depth":kind==Shader::tone?"tonemap_replayed":kind==Shader::copy?"copy_replayed":kind==Shader::pre_taa?"pre_TAA_current_depth":kind==Shader::taa?"TAA_unjittered_current_depth":"post_TAA_nominal_depth",source.Get(),vertices,firstVertex);
        }
        else Event(replayFailure.c_str(),source.Get(),vertices,firstVertex);
    }
    std::string Diagnostics(uint64_t epoch) const {
        std::ostringstream j;j<<"{\"epoch\":"<<epoch<<",\"snapshots\":"<<SnapshotCount()<<",\"outputs\":"<<OutputCount()<<",\"broken\":"<<broken<<",\"shaders_registered\":"<<shaders.size()<<",\"draws_by_kind\":[";
        for(unsigned i=0;i<7;++i){if(i)j<<',';j<<routeDraws[i];}
        j<<"],\"replays\":["<<replays[0]<<','<<replays[1]<<"],\"final_invalidations\":"<<invalidations<<",\"final_clears\":"<<clears<<",\"reasons\":{";
        bool comma=false;for(auto& [reason,n]:reasons){if(comma)j<<',';comma=true;j<<'"'<<reason<<"\":"<<n;}j<<"},\"snapshots_detail\":[";
        comma=false;for(auto& s:snapshots)if(s.valid){if(comma)j<<',';comma=true;j<<reinterpret_cast<uint64_t>(s.color.Get());}
        j<<"],\"events\":[";comma=false;for(auto& e:events){if(comma)j<<',';comma=true;j<<e;}j<<"],\"fullscreen_shaders\":[";
        comma=false;std::set<uint64_t> selected;for(auto& e:events){const auto begin=e.find("\"ps\":");if(begin!=std::string::npos)selected.insert(std::strtoull(e.c_str()+begin+5,nullptr,10));}
        for(auto id:selected){auto it=shaders.find(id);if(it==shaders.end())continue;if(comma)j<<',';comma=true;j<<"{\"ps\":"<<id<<",\"kind\":"<<unsigned(it->second.kind)<<",\"bytes\":"<<it->second.size<<",\"sha256\":\""<<it->second.hash<<"\"}";}
        j<<"]}";return j.str();
    }
    void SaveDiagnostics(const std::wstring& directory,uint64_t epoch) {
        const auto data=Diagnostics(epoch);if(diagnosticBytes+data.size()>4*1024*1024)return;
        std::ofstream out(std::filesystem::path(directory)/"depth-route-details.jsonl",std::ios::app);out<<data<<'\n';diagnosticBytes+=data.size();
        std::set<uint64_t> selected;for(auto& e:events){const auto begin=e.find("\"ps\":");if(begin!=std::string::npos)selected.insert(std::strtoull(e.c_str()+begin+5,nullptr,10));}
        auto path=std::filesystem::path(directory)/"depth-route-shaders";std::error_code ec;std::filesystem::create_directories(path,ec);
        for(auto id:selected){auto it=shaders.find(id);if(it==shaders.end()||it->second.bytes.empty())continue;auto f=path/(it->second.hash+".dxbc");if(std::filesystem::exists(f))continue;std::ofstream shader(f,std::ios::binary);shader.write(reinterpret_cast<const char*>(it->second.bytes.data()),it->second.bytes.size());}
    }
    bool Collect(ID3D11DeviceContext* c,std::unique_ptr<depth_match::Matcher>& matcher,unsigned& mw,unsigned& mh,DXGI_FORMAT& df,uint64_t epoch,uint64_t& serial){
        SnapshotBound(c);if(broken)return false;
        std::vector<Link*> outputs;for(auto& l:links)if(l.valid&&l.stage==2)outputs.push_back(&l);
        if(outputs.size()!=2||outputs[0]->source==outputs[1]->source)return false;
        if(!matcher||mw!=w||mh!=h||df!=DXGI_FORMAT_R32_TYPELESS){matcher=std::make_unique<depth_match::Matcher>(device.Get(),c,w,h,DXGI_FORMAT_R32_FLOAT);mw=w;mh=h;df=DXGI_FORMAT_R32_TYPELESS;}
        matcher->Reset();
        // The final eye color rotates through the swapchain; the HDR scene target the
        // depth came from keeps its role between frames and names the candidate.
        for(auto* l:outputs){
            const bool sourced=l->source>=0&&size_t(l->source)<snapshots.size()&&snapshots[size_t(l->source)].color;
            const uint64_t identity=sourced?reinterpret_cast<uint64_t>(snapshots[size_t(l->source)].color.Get()):0;
            if(!matcher->Capture(l->color.Get(),0,l->depth.tex.Get(),0,epoch,++serial,identity))return false;
        }
        return true;
    }
};
}
