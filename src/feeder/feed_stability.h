#pragma once
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <memory>
#include "feed_d3d11_context.h"

// Optional SDR temporal processing of the neural residual only. No writes to
// game color, NGX inputs, NGX outputs or the model's own history. Each eye is
// bounded independently, including every bilinear and neighborhood tap. The
// caller must prove raw reversed-Z depth; the 2% code-space gate is not a
// metric-distance test and must not be reused for ordinary non-reversed depth.
//
// Band split (preview-next): the residual is divided into a low band (8x8 cell
// mean, filtered at 1/8 resolution with its own history weight and no
// neighbourhood clamp, then averaged with the other eye where both eyes
// processed the same far content) and the remaining high band, which keeps the
// original tight neighbourhood clamp. With the split disabled the shader
// reproduces the previous filter exactly.
namespace ets2_stability {
using Microsoft::WRL::ComPtr;
struct Settings {
    float strengthHigh = 0;   // history weight of the high band (the old single strength)
    float strengthLow = 0;    // history weight of the low band at 1/8 resolution
    float crossEye = 0;       // blend toward the two eyes' mean low band on matched far cells
    int shiftPixels = 0;      // far-content offset right eye minus left eye, in work pixels
    bool bandSplit = false;
    float cabDepth = .5f;     // raw depth above this is the cabin band (excluded from cross-eye)
    unsigned crop[2][4] = {}; // per eye x0,y0,x1,y1 of the processed square in eye-local work pixels
    float gainLow = 1;        // multiplies the low (tone) band of the output; needs bandSplit
    float gainHigh = 1;       // multiplies the high (detail) band of the output; needs bandSplit
    float gainNear = 1;       // multiplies the whole output where raw depth is above cabDepth (the cab)
    bool operator==(const Settings&) const = default;
    // Gains act after both histories, so changing them keeps the history usable.
    bool HistoryCompatible(const Settings& o) const { Settings a = *this, b = o; a.gainLow = b.gainLow = a.gainHigh = b.gainHigh = a.gainNear = b.gainNear = 1; return a == b; }
};
struct Constants {
    uint32_t width, height, eyeWidth, historyValid;
    float strengthHigh, colorTolerance, depthTolerance, depthFloor;
    float boundsExpansion, strengthLow, crossEye, cabDepth;
    int32_t shiftCells; uint32_t lowEyeWidth, lowWidth, lowHeight;
    uint32_t bandSplit, lowHistoryValid; float gainLow, gainHigh;
    uint32_t cropLeft[4];
    uint32_t cropRight[4];
    float gainNear, gainPad0, gainPad1, gainPad2;
};
static_assert(sizeof(Constants) == 128);
inline constexpr char Prefix[] = R"HLSL(
cbuffer C:register(b0) {
    uint width,height,eyeWidth,historyValid;
    float strengthHigh,colorTolerance,depthTolerance,depthFloor;
    float boundsExpansion,strengthLow,crossEye,cabDepth;
    int shiftCells; uint lowEyeWidth,lowWidth,lowHeight;
    uint bandSplit,lowHistoryValid; float gainLow,gainHigh;
    uint4 cropLeft;
    uint4 cropRight;
    float gainNear,gainPad0,gainPad1,gainPad2;
};
bool sameDepth(float a,float b) {
    return isfinite(a)&&isfinite(b)&&a>=0&&a<=1&&b>=0&&b<=1&&abs(a-b)<=depthTolerance*max(abs(a),abs(b))+depthFloor;
}
int2 clampLow(int2 c,int first) { return clamp(c,int2(first,0),int2(first+int(lowEyeWidth)-1,int(lowHeight)-1)); }
)HLSL";
inline constexpr char DownsampleSource[] = R"HLSL(
Texture2D<float4> currentOutput:register(t0);
Texture2D<float4> currentInput:register(t1);
Texture2D<float> currentDepth:register(t2);
Texture2D<float2> currentMotion:register(t3);
RWTexture2D<float4> outLowRaw:register(u0);
RWTexture2D<float2> outLowMotion:register(u1);
RWTexture2D<float> outLowDepth:register(u2);
groupshared float3 gResidual[64];
groupshared float2 gMotion[64];
groupshared float gDepth[64];
groupshared uint gCount[64];
// One thread group per 8x8 cell: every thread reads one pixel, the group reduces.
[numthreads(8,8,1)]
void downsample(uint3 gid:SV_GroupID,uint3 tid:SV_GroupThreadID,uint gi:SV_GroupIndex) {
    uint eye=gid.x/lowEyeWidth; int cx=int(gid.x)-int(eye*lowEyeWidth); int first=int(eye*eyeWidth);
    int2 p=int2(first+cx*8+int(tid.x),int(gid.y)*8+int(tid.y));
    bool valid=gid.x<lowWidth&&gid.y<lowHeight&&eye<2&&p.x<first+int(eyeWidth)&&p.y<int(height);
    float3 d=0; float2 m=0; float z=0;
    if(valid) {
        d=currentOutput.Load(int3(p,0)).rgb-currentInput.Load(int3(p,0)).rgb; d=all(isfinite(d))?d:0;
        m=currentMotion.Load(int3(p,0)); m=all(isfinite(m))?m:0;
        z=currentDepth.Load(int3(p,0));
    }
    gResidual[gi]=d; gMotion[gi]=m; gDepth[gi]=z; gCount[gi]=valid?1u:0u;
    GroupMemoryBarrierWithGroupSync();
    [unroll]for(uint s=32;s>0;s>>=1) {
        if(gi<s){gResidual[gi]+=gResidual[gi+s];gMotion[gi]+=gMotion[gi+s];gDepth[gi]=max(gDepth[gi],gDepth[gi+s]);gCount[gi]+=gCount[gi+s];}
        GroupMemoryBarrierWithGroupSync();
    }
    if(gi==0&&gid.x<lowWidth&&gid.y<lowHeight) {
        float n=float(gCount[0]); uint4 crop=eye==0?cropLeft:cropRight; int y0=int(gid.y)*8;
        // Coverage: the whole cell lies inside this eye's processed square.
        bool covered=cx*8>=int(crop.x)&&y0>=int(crop.y)&&cx*8+8<=int(crop.z)&&y0+8<=int(crop.w);
        outLowRaw[gid.xy]=float4(n>0?gResidual[0]/n:0,covered?1.0:0.0);
        outLowMotion[gid.xy]=(n>0?gMotion[0]/n:0)/8.0;
        outLowDepth[gid.xy]=gDepth[0];
    }
}
)HLSL";
inline constexpr char LowTemporalSource[] = R"HLSL(
Texture2D<float4> lowRaw:register(t7);
Texture2D<float2> lowMotion:register(t9);
Texture2D<float> lowDepth:register(t10);
Texture2D<float4> previousLow:register(t11);
Texture2D<float> previousLowDepth:register(t12);
RWTexture2D<float4> outLowTemp:register(u0);
[numthreads(8,8,1)]
void lowTemporal(uint3 id:SV_DispatchThreadID) {
    if(id.x>=lowWidth||id.y>=lowHeight)return;
    float4 r=lowRaw.Load(int3(id.xy,0)); uint eye=id.x/lowEyeWidth; int first=int(eye*lowEyeWidth);
    float3 result=r.rgb;
    if(r.a>0&&lowHistoryValid!=0&&strengthLow>0) {
        float2 m=lowMotion.Load(int3(id.xy,0)); float2 prev=float2(id.xy)+m;
        bool inside=all(isfinite(m))&&prev.x>=first&&prev.x<=first+int(lowEyeWidth)-1&&prev.y>=0&&prev.y<=int(lowHeight)-1;
        if(inside) {
            int2 b=int2(floor(prev)); float2 f=frac(prev);
            int2 a=clampLow(b,first),bb=clampLow(b+int2(1,0),first),c=clampLow(b+int2(0,1),first),d=clampLow(b+1,first);
            float4 pa=previousLow.Load(int3(a,0)),pb=previousLow.Load(int3(bb,0)),pc=previousLow.Load(int3(c,0)),pd=previousLow.Load(int3(d,0));
            float depth=lowDepth.Load(int3(id.xy,0));
            // Every contributing history cell must be the same surface and a processed cell.
            bool depthOK=((1-f.x)*(1-f.y)<=1e-4||sameDepth(depth,previousLowDepth.Load(int3(a,0))))
                       &&(f.x*(1-f.y)<=1e-4||sameDepth(depth,previousLowDepth.Load(int3(bb,0))))
                       &&((1-f.x)*f.y<=1e-4||sameDepth(depth,previousLowDepth.Load(int3(c,0))))
                       &&(f.x*f.y<=1e-4||sameDepth(depth,previousLowDepth.Load(int3(d,0))));
            bool covered=min(min(pa.a,pb.a),min(pc.a,pd.a))>0.5;
            float3 pr=lerp(lerp(pa.rgb,pb.rgb,f.x),lerp(pc.rgb,pd.rgb,f.x),f.y);
            if(depthOK&&covered&&all(isfinite(pr)))result=lerp(r.rgb,pr,strengthLow);
        }
    }
    outLowTemp[id.xy]=float4(result,r.a);
}
)HLSL";
inline constexpr char LowCrossSource[] = R"HLSL(
Texture2D<float> lowDepth:register(t10);
Texture2D<float4> lowTemp:register(t13);
RWTexture2D<float4> outLowFinal:register(u0);
[numthreads(8,8,1)]
void lowCross(uint3 id:SV_DispatchThreadID) {
    if(id.x>=lowWidth||id.y>=lowHeight)return;
    float4 r=lowTemp.Load(int3(id.xy,0)); uint eye=id.x/lowEyeWidth; int cx=int(id.x)-int(eye*lowEyeWidth);
    float3 result=r.rgb;
    if(crossEye>0&&r.a>0) {
        // Far content sits shiftCells further along x in the right eye than in the left.
        int ox=eye==0?cx+shiftCells:cx-shiftCells;
        if(ox>=0&&ox<int(lowEyeWidth)) {
            int2 o=int2(int((1-eye)*lowEyeWidth)+ox,int(id.y));
            float4 other=lowTemp.Load(int3(o,0));
            if(other.a>0&&lowDepth.Load(int3(id.xy,0))<=cabDepth&&lowDepth.Load(int3(o,0))<=cabDepth&&all(isfinite(other.rgb)))
                result=lerp(result,0.5*(result+other.rgb),crossEye);
        }
    }
    outLowFinal[id.xy]=float4(result,r.a);
}
)HLSL";
inline constexpr char StabilizeSource[] = R"HLSL(
Texture2D<float4> currentOutput:register(t0);
Texture2D<float4> currentInput:register(t1);
Texture2D<float> currentDepth:register(t2);
Texture2D<float2> currentMotion:register(t3);
Texture2D<float4> previousHigh:register(t4);
Texture2D<float4> previousInput:register(t5);
Texture2D<float> previousDepth:register(t6);
Texture2D<float4> lowRaw:register(t7);
Texture2D<float4> lowFinal:register(t8);
RWTexture2D<float4> outResidual:register(u0);
RWTexture2D<float4> outHigh:register(u1);
int2 clampEye(int2 p,int first) { return clamp(p,int2(first,0),int2(first+int(eyeWidth)-1,int(height)-1)); }
float3 residual(int2 p,int first) {
    p=clampEye(p,first);
    float3 d=currentOutput.Load(int3(p,0)).rgb-currentInput.Load(int3(p,0)).rgb;
    return all(isfinite(d))?d:0;
}
// Bilinear reconstruction of a 1/8 field at a work pixel, bounded to the pixel's eye.
float3 lowRawAt(int2 p,int first,uint eye) {
    float2 c=(float2(p-int2(first,0))+0.5)/8.0-0.5; int2 b=int2(floor(c)); float2 f=frac(c); int lf=int(eye*lowEyeWidth);
    int2 a=clampLow(b+int2(lf,0),lf),bb=clampLow(b+int2(lf+1,0),lf),cc=clampLow(b+int2(lf,1),lf),d=clampLow(b+int2(lf+1,1),lf);
    return lerp(lerp(lowRaw.Load(int3(a,0)).rgb,lowRaw.Load(int3(bb,0)).rgb,f.x),lerp(lowRaw.Load(int3(cc,0)).rgb,lowRaw.Load(int3(d,0)).rgb,f.x),f.y);
}
float3 lowFinalAt(int2 p,int first,uint eye) {
    float2 c=(float2(p-int2(first,0))+0.5)/8.0-0.5; int2 b=int2(floor(c)); float2 f=frac(c); int lf=int(eye*lowEyeWidth);
    int2 a=clampLow(b+int2(lf,0),lf),bb=clampLow(b+int2(lf+1,0),lf),cc=clampLow(b+int2(lf,1),lf),d=clampLow(b+int2(lf+1,1),lf);
    return lerp(lerp(lowFinal.Load(int3(a,0)).rgb,lowFinal.Load(int3(bb,0)).rgb,f.x),lerp(lowFinal.Load(int3(cc,0)).rgb,lowFinal.Load(int3(d,0)).rgb,f.x),f.y);
}
[numthreads(8,8,1)]
void stabilize(uint3 dispatch:SV_DispatchThreadID) {
    if(dispatch.x>=width||dispatch.y>=height)return;
    int2 p=int2(dispatch.xy); uint eye=dispatch.x/eyeWidth; int first=int(eye*eyeWidth);
    float3 raw=residual(p,first);
    if(all(raw==0)){outResidual[p]=0;outHigh[p]=0;return;}
    // The low band varies over 8 px cells, so the pixel's own value serves its 1 px neighbours too.
    float3 lowR=bandSplit!=0?lowRawAt(p,first,eye):0;
    float3 hraw=raw-lowR, result=hraw;
    float depth=currentDepth.Load(int3(p,0));
    float2 motion=currentMotion.Load(int3(p,0));
    float2 prev=float2(p)+motion;
    bool inside=all(isfinite(motion))&&prev.x>=first&&prev.x<=first+int(eyeWidth)-1&&prev.y>=0&&prev.y<=int(height)-1;
    if(historyValid!=0&&inside&&strengthHigh>0) {
        int2 base=int2(floor(prev));float2 f=frac(prev);
        int2 a=clampEye(base,first),b=clampEye(base+int2(1,0),first),c=clampEye(base+int2(0,1),first),d=clampEye(base+1,first);
        float3 pc=lerp(lerp(previousInput.Load(int3(a,0)).rgb,previousInput.Load(int3(b,0)).rgb,f.x),
                       lerp(previousInput.Load(int3(c,0)).rgb,previousInput.Load(int3(d,0)).rgb,f.x),f.y);
        float3 error=abs(currentInput.Load(int3(p,0)).rgb-pc);
        float agreement=saturate(1-max(error.x,max(error.y,error.z))/colorTolerance);
        // Do not interpolate unrelated foreground/background depth into a
        // plausible number. Every contributing history footprint must agree.
        bool depthOK=((1-f.x)*(1-f.y)<=1e-4||sameDepth(depth,previousDepth.Load(int3(a,0))))
                  &&(f.x*(1-f.y)<=1e-4||sameDepth(depth,previousDepth.Load(int3(b,0))))
                  &&((1-f.x)*f.y<=1e-4||sameDepth(depth,previousDepth.Load(int3(c,0))))
                  &&(f.x*f.y<=1e-4||sameDepth(depth,previousDepth.Load(int3(d,0))));
        if(depthOK&&agreement>0) {
            float3 pr=lerp(lerp(previousHigh.Load(int3(a,0)).rgb,previousHigh.Load(int3(b,0)).rgb,f.x),
                           lerp(previousHigh.Load(int3(c,0)).rgb,previousHigh.Load(int3(d,0)).rgb,f.x),f.y);
            float3 low=hraw,high=hraw;
            const int2 offsets[4]={int2(-1,0),int2(1,0),int2(0,-1),int2(0,1)};
            [unroll]for(uint i=0;i<4;++i){float3 r=residual(p+offsets[i],first)-lowR;low=min(low,r);high=max(high,r);}
            if(all(isfinite(pr)))result=lerp(hraw,clamp(pr,low-boundsExpansion,high+boundsExpansion),strengthHigh*agreement);
        }
    }
    outHigh[p]=float4(result,0);
    // Gains act on the output only; both histories store the plain bands, so a gain change never compounds.
    float3 edit=bandSplit!=0?result*gainHigh+lowFinalAt(p,first,eye)*gainLow:result;
    outResidual[p]=float4(edit*(depth>cabDepth?gainNear:1.0),0);
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
    ComPtr<ID3D11ComputeShader> downsample,lowTemporal,lowCross,stabilize;
    ComPtr<ID3D11Buffer> cb;
    ets2_d3d11::PrivateState isolatedState;
    Image highHistory[2],fullOut,previousInput,previousDepth;
    Image lowRaw,lowMotion,lowDepth,lowTemp,lowHistory[2],previousLowDepth;
    ComPtr<ID3D11ShaderResourceView> inputView,outputView,depthView,motionView;
    ID3D11Texture2D* inputs[4]{}; // identities only; SRVs retain underlying resources
    UINT width=0,height=0,current=0,lowW=0,lowH=0,lowEyeW=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool valid=false,lowValid=false,usedHistory=false;
    uint64_t lastEpoch=0,lastTime=0;
    Settings lastSettings;
    bool haveSettings=false;
    static ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* dev,const std::string& source,const char* entry){
        HMODULE library=LoadLibraryW(L"d3dcompiler_47.dll");
        auto compile=library?reinterpret_cast<pD3DCompile>(GetProcAddress(library,"D3DCompile")):nullptr;
        ComPtr<ID3DBlob> bytecode,error;
        HRESULT hr=compile?compile(source.data(),source.size(),"ETS2 neural residual stability",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&error):E_NOINTERFACE;
        std::string compilerError;
        if(FAILED(hr))compilerError=error?static_cast<const char*>(error->GetBufferPointer()):"stability compiler unavailable";
        ComPtr<ID3D11ComputeShader> shader;
        if(SUCCEEDED(hr))hr=dev->CreateComputeShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,&shader);
        if(library)FreeLibrary(library);
        if(!compilerError.empty())throw std::runtime_error(std::string(entry)+": "+compilerError);
        Check(hr,entry);
        return shader;
    }
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
            lowEyeW=(width/2+7)/8;lowW=2*lowEyeW;lowH=(height+7)/8;
            const std::string prefix(Prefix);
            downsample=Compile(device.Get(),prefix+DownsampleSource,"downsample");
            lowTemporal=Compile(device.Get(),prefix+LowTemporalSource,"lowTemporal");
            lowCross=Compile(device.Get(),prefix+LowCrossSource,"lowCross");
            stabilize=Compile(device.Get(),prefix+StabilizeSource,"stabilize");
            D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            Check(device->CreateBuffer(&b,nullptr,&cb),"stability constants");
            for(auto& r:highHistory)r=MakeImage(device.Get(),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            fullOut=MakeImage(device.Get(),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            previousInput=MakeImage(device.Get(),width,height,format,false);previousDepth=MakeImage(device.Get(),width,height,DXGI_FORMAT_R32_FLOAT,false);
            lowRaw=MakeImage(device.Get(),lowW,lowH,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            lowMotion=MakeImage(device.Get(),lowW,lowH,DXGI_FORMAT_R16G16_FLOAT,true);
            lowDepth=MakeImage(device.Get(),lowW,lowH,DXGI_FORMAT_R32_FLOAT,true);
            lowTemp=MakeImage(device.Get(),lowW,lowH,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            for(auto& r:lowHistory)r=MakeImage(device.Get(),lowW,lowH,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            previousLowDepth=MakeImage(device.Get(),lowW,lowH,DXGI_FORMAT_R32_FLOAT,false);
        }
        ID3D11Texture2D* resources[]={input,output,depth,motion};
        ComPtr<ID3D11ShaderResourceView>* views[]={std::addressof(inputView),std::addressof(outputView),std::addressof(depthView),std::addressof(motionView)};
        for(unsigned i=0;i<4;++i)if(inputs[i]!=resources[i]){
            Check(device->CreateShaderResourceView(resources[i],nullptr,views[i]->ReleaseAndGetAddressOf()),"stability current view");
            inputs[i]=resources[i];valid=false;lowValid=false;
        }
    }
public:
    void Invalidate(){valid=false;lowValid=false;usedHistory=false;}
    void Release(){
        Invalidate();for(auto& r:highHistory)r={};fullOut={};previousInput={};previousDepth={};
        lowRaw={};lowMotion={};lowDepth={};lowTemp={};for(auto& r:lowHistory)r={};previousLowDepth={};
        downsample.Reset();lowTemporal.Reset();lowCross.Reset();stabilize.Reset();cb.Reset();isolatedState.Release();
        inputView.Reset();outputView.Reset();depthView.Reset();motionView.Reset();device.Reset();
        for(auto*& p:inputs)p=nullptr;width=height=current=lowW=lowH=lowEyeW=0;format=DXGI_FORMAT_UNKNOWN;lastEpoch=lastTime=0;haveSettings=false;
    }
    bool UsedHistory()const{return usedHistory;}
    // Legacy entry: the previous single-strength filter without a band split.
    ID3D11ShaderResourceView* Apply(ID3D11DeviceContext* context,ID3D11Texture2D* input,ID3D11Texture2D* output,ID3D11Texture2D* depth,ID3D11Texture2D* motion,uint64_t epoch,bool reset,float strength){
        Settings s;s.strengthHigh=strength;
        return Apply(context,input,output,depth,motion,epoch,reset,s);
    }
    ID3D11ShaderResourceView* Apply(ID3D11DeviceContext* context,ID3D11Texture2D* input,ID3D11Texture2D* output,ID3D11Texture2D* depth,ID3D11Texture2D* motion,uint64_t epoch,bool reset,const Settings& s){
        const bool anyWork=s.bandSplit||s.strengthHigh>0;
        if(!context||!input||!output||!depth||!motion||!anyWork||!std::isfinite(s.strengthHigh)||s.strengthHigh<0||s.strengthHigh>.9f||
           !std::isfinite(s.strengthLow)||s.strengthLow<0||s.strengthLow>.95f||!std::isfinite(s.crossEye)||s.crossEye<0||s.crossEye>1||!std::isfinite(s.cabDepth)||
           !std::isfinite(s.gainLow)||s.gainLow<0||s.gainLow>4||!std::isfinite(s.gainHigh)||s.gainHigh<0||s.gainHigh>4||!std::isfinite(s.gainNear)||s.gainNear<0||s.gainNear>4){
            Invalidate();
            throw std::runtime_error("invalid stability invocation");
        }
        try{
            Prepare(context,input,output,depth,motion);
            const auto now=GetTickCount64();
            const bool continuous=!reset&&epoch==lastEpoch+1&&now-lastTime<=250&&haveSettings&&s.HistoryCompatible(lastSettings);
            usedHistory=valid&&continuous;
            const bool lowHistoryOk=lowValid&&continuous&&s.bandSplit;
            const UINT next=1-current;
            Constants constants{};
            constants.width=width;constants.height=height;constants.eyeWidth=width/2;constants.historyValid=usedHistory?1u:0u;
            constants.strengthHigh=s.strengthHigh;constants.colorTolerance=12.0f/255.0f;constants.depthTolerance=.02f;constants.depthFloor=1e-5f;
            constants.boundsExpansion=2.0f/255.0f;constants.strengthLow=s.strengthLow;constants.crossEye=s.crossEye;constants.cabDepth=s.cabDepth;
            constants.shiftCells=int32_t(std::lround(double(s.shiftPixels)/8.0));constants.lowEyeWidth=lowEyeW;constants.lowWidth=lowW;constants.lowHeight=lowH;
            constants.bandSplit=s.bandSplit?1u:0u;constants.lowHistoryValid=lowHistoryOk?1u:0u;
            constants.gainLow=s.gainLow;constants.gainHigh=s.gainHigh;constants.gainNear=s.gainNear;
            for(unsigned i=0;i<4;++i){constants.cropLeft[i]=s.crop[0][i];constants.cropRight[i]=s.crop[1][i];}
            {
                ets2_d3d11::Scope restore(isolatedState,context);
                Check(restore.Status(),"stability isolated context");
                context->UpdateSubresource(cb.Get(),0,nullptr,&constants,0,0);
                ID3D11Buffer* constant=cb.Get();context->CSSetConstantBuffers(0,1,&constant);
                ID3D11ShaderResourceView* none[16]{};ID3D11UnorderedAccessView* noUav[4]{};const UINT keep[4]={UINT(-1),UINT(-1),UINT(-1),UINT(-1)};
                const UINT groupsX=(width+7)/8,groupsY=(height+7)/8,lowGroupsX=(lowW+7)/8,lowGroupsY=(lowH+7)/8;
                if(s.bandSplit){
                    // 1. cell means of the residual, cell motion and nearest depth
                    ID3D11ShaderResourceView* in[4]={outputView.Get(),inputView.Get(),depthView.Get(),motionView.Get()};
                    for(auto* v:in)if(!v)throw std::runtime_error("stability required view unavailable");
                    context->CSSetShaderResources(0,4,in);
                    ID3D11UnorderedAccessView* out[3]={lowRaw.uav.Get(),lowMotion.uav.Get(),lowDepth.uav.Get()};
                    context->CSSetUnorderedAccessViews(0,3,out,keep);
                    context->CSSetShader(downsample.Get(),nullptr,0);
                    context->Dispatch(lowW,lowH,1); // one group per cell
                    context->CSSetUnorderedAccessViews(0,3,noUav,keep);
                    // 2. low band history
                    ID3D11ShaderResourceView* temporal[6]={lowRaw.srv.Get(),nullptr,lowMotion.srv.Get(),lowDepth.srv.Get(),lowHistory[current].srv.Get(),previousLowDepth.srv.Get()};
                    context->CSSetShaderResources(7,6,temporal);
                    ID3D11UnorderedAccessView* temp[1]={lowTemp.uav.Get()};
                    context->CSSetUnorderedAccessViews(0,1,temp,keep);
                    context->CSSetShader(lowTemporal.Get(),nullptr,0);
                    context->Dispatch(lowGroupsX,lowGroupsY,1);
                    context->CSSetUnorderedAccessViews(0,1,noUav,keep);
                    // 3. cross-eye average of the low band on matched far cells
                    ID3D11ShaderResourceView* cross[1]={lowTemp.srv.Get()};
                    context->CSSetShaderResources(13,1,cross);
                    ID3D11UnorderedAccessView* fin[1]={lowHistory[next].uav.Get()};
                    context->CSSetUnorderedAccessViews(0,1,fin,keep);
                    context->CSSetShader(lowCross.Get(),nullptr,0);
                    context->Dispatch(lowGroupsX,lowGroupsY,1);
                    context->CSSetUnorderedAccessViews(0,1,noUav,keep);
                    context->CSSetShaderResources(7,7,none);
                }
                // 4. high band (or the whole residual without the split) at work resolution
                ID3D11ShaderResourceView* views[9]={outputView.Get(),inputView.Get(),depthView.Get(),motionView.Get(),highHistory[current].srv.Get(),previousInput.srv.Get(),previousDepth.srv.Get(),lowRaw.srv.Get(),lowHistory[next].srv.Get()};
                for(unsigned i=0;i<7;++i)if(!views[i])throw std::runtime_error("stability required view unavailable");
                context->CSSetShaderResources(0,9,views);
                ID3D11UnorderedAccessView* targets[2]={fullOut.uav.Get(),highHistory[next].uav.Get()};
                context->CSSetUnorderedAccessViews(0,2,targets,keep);
                context->CSSetShader(stabilize.Get(),nullptr,0);
                context->Dispatch(groupsX,groupsY,1);
                context->CSSetUnorderedAccessViews(0,2,noUav,keep);
                context->CSSetShaderResources(0,9,none);
                context->CopyResource(previousInput.texture.Get(),input);context->CopyResource(previousDepth.texture.Get(),depth);
                if(s.bandSplit)context->CopyResource(previousLowDepth.texture.Get(),lowDepth.texture.Get());
            }
            current=next;valid=true;lowValid=s.bandSplit;lastEpoch=epoch;lastTime=now;lastSettings=s;haveSettings=true;
            return fullOut.srv.Get();
        }catch(...){Release();throw;}
    }
};
} // namespace ets2_stability
