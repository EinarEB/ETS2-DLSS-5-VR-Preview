#pragma once
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <memory>
#include "feed_d3d11_context.h"

// Optional SDR temporal processing of the neural residual only. No writes to
// game color, NGX inputs, NGX outputs or the model's own history. Each eye is
// bounded independently, including every bilinear and neighborhood tap. The
// caller must prove raw reversed-Z depth; the 2% code-space gate is not a
// metric-distance test and must not be reused for ordinary non-reversed depth.
namespace ets2_stability {
using Microsoft::WRL::ComPtr;
struct Constants {
    uint32_t width,height,eyeWidth,historyValid;
    float strength,colorTolerance,depthTolerance,depthFloor;
    float boundsExpansion,pad[3];
};
static_assert(sizeof(Constants)==48);
inline constexpr char Shader[]=R"HLSL(
Texture2D<float4> currentOutput:register(t0);
Texture2D<float4> currentInput:register(t1);
Texture2D<float> currentDepth:register(t2);
Texture2D<float2> currentMotion:register(t3);
Texture2D<float4> previousResidual:register(t4);
Texture2D<float4> previousInput:register(t5);
Texture2D<float> previousDepth:register(t6);
RWTexture2D<float4> nextResidual:register(u0);
cbuffer C:register(b0) {
    uint width,height,eyeWidth,historyValid;
    float strength,colorTolerance,depthTolerance,depthFloor;
    float boundsExpansion; float3 unused;
};
int2 clampEye(int2 p,int first) { return clamp(p,int2(first,0),int2(first+int(eyeWidth)-1,int(height)-1)); }
float3 residual(int2 p,int first) {
    p=clampEye(p,first);
    float3 d=currentOutput.Load(int3(p,0)).rgb-currentInput.Load(int3(p,0)).rgb;
    return all(isfinite(d))?d:0;
}
bool sameDepth(float a,float b) {
    return isfinite(a)&&isfinite(b)&&a>=0&&a<=1&&b>=0&&b<=1&&abs(a-b)<=depthTolerance*max(abs(a),abs(b))+depthFloor;
}
[numthreads(8,8,1)]
void stabilize(uint3 dispatch:SV_DispatchThreadID) {
    if(dispatch.x>=width||dispatch.y>=height)return;
    int2 p=int2(dispatch.xy);int first=(p.x/int(eyeWidth))*int(eyeWidth);
    float3 raw=residual(p,first), result=raw;
    if(all(raw==0)){nextResidual[p]=0;return;}
    float2 motion=currentMotion.Load(int3(p,0));
    float2 prev=float2(p)+motion;
    bool inside=all(isfinite(motion))&&prev.x>=first&&prev.x<=first+int(eyeWidth)-1&&prev.y>=0&&prev.y<=int(height)-1;
    if(historyValid!=0&&inside&&strength>0) {
        int2 base=int2(floor(prev));float2 f=frac(prev);
        int2 a=clampEye(base,first),b=clampEye(base+int2(1,0),first),c=clampEye(base+int2(0,1),first),d=clampEye(base+1,first);
        float3 pc=lerp(lerp(previousInput.Load(int3(a,0)).rgb,previousInput.Load(int3(b,0)).rgb,f.x),
                       lerp(previousInput.Load(int3(c,0)).rgb,previousInput.Load(int3(d,0)).rgb,f.x),f.y);
        float depth=currentDepth.Load(int3(p,0));
        float3 error=abs(currentInput.Load(int3(p,0)).rgb-pc);
        float agreement=saturate(1-max(error.x,max(error.y,error.z))/colorTolerance);
        // Do not interpolate unrelated foreground/background depth into a
        // plausible number. Every contributing history footprint must agree.
        bool depthOK=((1-f.x)*(1-f.y)<=1e-4||sameDepth(depth,previousDepth.Load(int3(a,0))))
                  &&(f.x*(1-f.y)<=1e-4||sameDepth(depth,previousDepth.Load(int3(b,0))))
                  &&((1-f.x)*f.y<=1e-4||sameDepth(depth,previousDepth.Load(int3(c,0))))
                  &&(f.x*f.y<=1e-4||sameDepth(depth,previousDepth.Load(int3(d,0))));
        if(depthOK&&agreement>0) {
            float3 pr=lerp(lerp(previousResidual.Load(int3(a,0)).rgb,previousResidual.Load(int3(b,0)).rgb,f.x),
                           lerp(previousResidual.Load(int3(c,0)).rgb,previousResidual.Load(int3(d,0)).rgb,f.x),f.y);
            float3 low=raw,high=raw;
            const int2 offsets[4]={int2(-1,0),int2(1,0),int2(0,-1),int2(0,1)};
            [unroll]for(uint i=0;i<4;++i){float3 r=residual(p+offsets[i],first);low=min(low,r);high=max(high,r);}
            if(all(isfinite(pr)))result=lerp(raw,clamp(pr,low-boundsExpansion,high+boundsExpansion),strength*agreement);
        }
    }
    nextResidual[p]=float4(result,0);
}
)HLSL";

inline void Check(HRESULT hr,const char* label){if(FAILED(hr))throw std::runtime_error(std::string(label)+" failed (HRESULT "+std::to_string(uint32_t(hr))+")");}
struct Image {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};
inline Image MakeImage(ID3D11Device* device,UINT w,UINT h,DXGI_FORMAT format,bool unordered){
    Image i;D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=format;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(unordered?D3D11_BIND_UNORDERED_ACCESS:0);
    Check(device->CreateTexture2D(&d,nullptr,&i.texture),"stability texture");
    Check(device->CreateShaderResourceView(i.texture.Get(),nullptr,&i.srv),"stability SRV");
    if(unordered)Check(device->CreateUnorderedAccessView(i.texture.Get(),nullptr,&i.uav),"stability UAV");
    return i;
}
class Filter {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> cb;
    ets2_d3d11::PrivateState isolatedState;
    Image residuals[2],previousInput,previousDepth;
    ComPtr<ID3D11ShaderResourceView> inputView,outputView,depthView,motionView;
    ID3D11Texture2D* inputs[4]{}; // identities only; SRVs retain underlying resources
    UINT width=0,height=0,current=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool valid=false,usedHistory=false;
    uint64_t lastEpoch=0,lastTime=0;
    float lastStrength=-1;
    void Prepare(ID3D11DeviceContext* context,ID3D11Texture2D* input,ID3D11Texture2D* output,ID3D11Texture2D* depth,ID3D11Texture2D* motion){
        ComPtr<ID3D11Device> next;context->GetDevice(&next);
        D3D11_TEXTURE2D_DESC d{},o{},z{},m{};input->GetDesc(&d);output->GetDesc(&o);depth->GetDesc(&z);motion->GetDesc(&m);
        if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||!d.Width||(d.Width&1)||!d.Height||d.SampleDesc.Count!=1||d.ArraySize!=1||d.MipLevels!=1)
            throw std::runtime_error("stability requires an immediate context and equal single-sample eyes");
        for(const auto& q:{o,z,m})if(q.Width!=d.Width||q.Height!=d.Height||q.ArraySize!=1||q.SampleDesc.Count!=1||q.MipLevels!=1)
            throw std::runtime_error("stability input extents differ");
        if((d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)||o.Format!=d.Format||z.Format!=DXGI_FORMAT_R32_FLOAT||m.Format!=DXGI_FORMAT_R16G16_FLOAT)
            throw std::runtime_error("unsupported stability input formats");
        const bool rebuild=device.Get()!=next.Get()||width!=d.Width||height!=d.Height||format!=d.Format;
        if(rebuild){
            Release();device=next;width=d.Width;height=d.Height;format=d.Format;
            HMODULE library=LoadLibraryW(L"d3dcompiler_47.dll");
            auto compile=library?reinterpret_cast<pD3DCompile>(GetProcAddress(library,"D3DCompile")):nullptr;
            ComPtr<ID3DBlob> bytecode,error;
            HRESULT hr=compile?compile(Shader,sizeof(Shader)-1,"ETS2 neural residual stability",nullptr,nullptr,"stabilize","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&error):E_NOINTERFACE;
            std::string compilerError;
            if(FAILED(hr))compilerError=error?static_cast<const char*>(error->GetBufferPointer()):"stability compiler unavailable";
            if(SUCCEEDED(hr))hr=device->CreateComputeShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,&shader);
            bytecode.Reset();error.Reset();if(library)FreeLibrary(library);
            if(!compilerError.empty())throw std::runtime_error(compilerError);
            Check(hr,"stability shader");
            D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            Check(device->CreateBuffer(&b,nullptr,&cb),"stability constants");
            for(auto& r:residuals)r=MakeImage(device.Get(),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            previousInput=MakeImage(device.Get(),width,height,format,false);previousDepth=MakeImage(device.Get(),width,height,DXGI_FORMAT_R32_FLOAT,false);
        }
        ID3D11Texture2D* resources[]={input,output,depth,motion};
        ComPtr<ID3D11ShaderResourceView>* views[]={std::addressof(inputView),std::addressof(outputView),std::addressof(depthView),std::addressof(motionView)};
        for(unsigned i=0;i<4;++i)if(inputs[i]!=resources[i]){
            Check(device->CreateShaderResourceView(resources[i],nullptr,views[i]->ReleaseAndGetAddressOf()),"stability current view");
            inputs[i]=resources[i];valid=false;
        }
    }
public:
    void Invalidate(){valid=false;usedHistory=false;}
    void Release(){
        Invalidate();for(auto& r:residuals)r={};previousInput={};previousDepth={};
        shader.Reset();cb.Reset();isolatedState.Release();inputView.Reset();outputView.Reset();depthView.Reset();motionView.Reset();device.Reset();
        for(auto*& p:inputs)p=nullptr;width=height=current=0;format=DXGI_FORMAT_UNKNOWN;lastEpoch=lastTime=0;lastStrength=-1;
    }
    bool UsedHistory()const{return usedHistory;}
    ID3D11ShaderResourceView* Apply(ID3D11DeviceContext* context,ID3D11Texture2D* input,ID3D11Texture2D* output,ID3D11Texture2D* depth,ID3D11Texture2D* motion,uint64_t epoch,bool reset,float strength){
        if(!context||!input||!output||!depth||!motion||!std::isfinite(strength)||strength<=0||strength>.9f){
            Invalidate();
            throw std::runtime_error("invalid stability invocation");
        }
        try{
            Prepare(context,input,output,depth,motion);
            const auto now=GetTickCount64();
            usedHistory=valid&&!reset&&epoch==lastEpoch+1&&now-lastTime<=250&&lastStrength==strength;
            const UINT next=1-current;
            const Constants constants={width,height,width/2,usedHistory?1u:0u,strength,12.0f/255.0f,.02f,1e-5f,2.0f/255.0f,{0,0,0}};
            {
                ets2_d3d11::Scope restore(isolatedState,context);
                Check(restore.Status(),"stability isolated context");
                context->UpdateSubresource(cb.Get(),0,nullptr,&constants,0,0);
                ID3D11Buffer* constant=cb.Get();context->CSSetConstantBuffers(0,1,&constant);
#ifdef ETS2_STABILITY_DIAGNOSTIC
                ComPtr<ID3D11Buffer> checkCb;UINT first=0,count=0;ComPtr<ID3D11DeviceContext1> c1;context->QueryInterface(IID_PPV_ARGS(&c1));
                c1->CSGetConstantBuffers1(0,1,&checkCb,&first,&count);
                fprintf(stderr,"frame %llu geometry %u %u %u history %u constant %p observed %p range %u %u\n",epoch,constants.width,constants.height,constants.eyeWidth,constants.historyValid,constant,checkCb.Get(),first,count);
#endif
                context->CSSetShader(shader.Get(),nullptr,0);
                ID3D11ShaderResourceView* views[]={outputView.Get(),inputView.Get(),depthView.Get(),motionView.Get(),residuals[current].srv.Get(),previousInput.srv.Get(),previousDepth.srv.Get()};
                for(auto* view:views)if(!view)throw std::runtime_error("stability required view unavailable");
                context->CSSetShaderResources(0,7,views);
                ID3D11UnorderedAccessView* target=residuals[next].uav.Get();UINT keep=UINT(-1);
                context->CSSetUnorderedAccessViews(0,1,&target,&keep);
#ifdef ETS2_STABILITY_DIAGNOSTIC
                ID3D11ShaderResourceView* checked[7]{};context->CSGetShaderResources(0,7,checked);
                for(unsigned i=0;i<7;++i){if(checked[i]!=views[i])fprintf(stderr,"SRV%u expected %p actual %p\n",i,views[i],checked[i]);if(checked[i])checked[i]->Release();}
#endif
                context->Dispatch((width+7)/8,(height+7)/8,1);
                ID3D11ShaderResourceView* none[7]{};context->CSSetShaderResources(0,7,none);
                context->CopyResource(previousInput.texture.Get(),input);context->CopyResource(previousDepth.texture.Get(),depth);
            }
            current=next;valid=true;lastEpoch=epoch;lastTime=now;lastStrength=strength;
            return residuals[current].srv.Get();
        }catch(...){Release();throw;}
    }
};
} // namespace ets2_stability
