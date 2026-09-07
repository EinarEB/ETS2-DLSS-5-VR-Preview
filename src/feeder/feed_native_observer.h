#pragma once
#include <map>
#include <array>
#include <deque>
#include "feed_nr_control_policy.h"

// D3D11 and unowned calls remain passive. Explicit owned stereo policy can
// coordinate resets and scale pass-two tone/structure around one original invocation.
// D3D11 game calls and the owned D3D12 NR evaluations have separate provenance.
// Resource descriptors do not establish eye assignment or usable native guides.
namespace ets2_native {
using Microsoft::WRL::ComPtr;
using Eval11=NVSDK_NGX_Result(__cdecl*)(ID3D11DeviceContext*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using Eval12=NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using Create12=NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
inline thread_local int eye=-1,pass=-1;
inline thread_local uint64_t frame=0;
inline thread_local unsigned nesting11=0;
inline thread_local ID3D12GraphicsCommandList* ownedList=nullptr;
struct OwnedScope {int oldEye=eye,oldPass=pass;uint64_t oldFrame=frame;ID3D12GraphicsCommandList* oldList=ownedList;OwnedScope(int e,int p,uint64_t f,ID3D12GraphicsCommandList* l){eye=e;pass=p;frame=f;ownedList=l;}~OwnedScope(){eye=oldEye;pass=oldPass;frame=oldFrame;ownedList=oldList;}};
struct NrDisplay {bool seen=false,presetKnown=false,intensityKnown=false,styleKnown=false;unsigned preset=0,style=0,result=0;float intensity=0;uint64_t frameId=0;};
struct Hook {void* address=nullptr;void* original=nullptr;bool enabled=false;std::wstring module;};
struct Creation {const NVSDK_NGX_Parameter* parameters=nullptr;uint64_t serial=0;unsigned preset=0;bool presetKnown=false;};
struct State {
    std::mutex mutex;Hook hooks[4];ULONGLONG nextPoll=0;std::filesystem::path path;
    uint64_t nativeCalls=0,nrCalls=0,unownedNr=0;std::string native="null";
    std::deque<std::string> recentNative;
    std::array<std::string,4> settings={"null","null","null","null"};
    std::array<NrDisplay,4> display;
    std::map<uint64_t,std::string> presets;
    std::map<uint64_t,Creation> creations;uint64_t creationSerial=0;
    std::string reason="No D3D11 game NGX evaluation observed. Native guides are unavailable.";
};
inline State& Get(){static auto* s=new State;return *s;}
// Only accessed by the serialized owned Feeder rendering path.
inline ets2_nr_control::Coordinator& Controls(){static auto* c=new ets2_nr_control::Coordinator;return *c;}
inline void BeginControls(unsigned slots,bool reset,bool enabled,float secondTone,float secondStructure,float intensity){Controls().Begin(slots,reset,{enabled,secondTone,secondStructure,intensity});}
inline bool CanDeliverControls(){return Controls().CanDeliver();}
inline void FinishControls(bool delivered){Controls().Finish(delivered);}
inline bool UInt(const NVSDK_NGX_Parameter* p,const char* k,unsigned* o){__try{return p&&p->Get(k,o)==NVSDK_NGX_Result_Success;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
inline bool Int(const NVSDK_NGX_Parameter* p,const char* k,int* o){__try{return p&&p->Get(k,o)==NVSDK_NGX_Result_Success;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
inline bool Float(const NVSDK_NGX_Parameter* p,const char* k,float* o){__try{return p&&p->Get(k,o)==NVSDK_NGX_Result_Success;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
inline bool SetFloat(const NVSDK_NGX_Parameter* p,const char* k,float v){__try{if(!p)return false;const_cast<NVSDK_NGX_Parameter*>(p)->Set(k,v);float read=0;return p->Get(k,&read)==NVSDK_NGX_Result_Success&&read==v;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
inline bool SetInt(const NVSDK_NGX_Parameter* p,const char* k,int v){__try{if(!p)return false;const_cast<NVSDK_NGX_Parameter*>(p)->Set(k,v);int read=0;return p->Get(k,&read)==NVSDK_NGX_Result_Success&&read==v;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
inline bool ReadControls(const NVSDK_NGX_Parameter* p,const Creation& creation,ets2_nr_control::Controls& out,int& reset){
    if(!creation.presetKnown||!Int(p,"DLSSNR.Reset",&reset))return false;
    unsigned n=0;
    for(auto k:{"DLSSNR.Intensity","DLSSNR.LocalToneStrength","DLSSNR.LocalStructureStrength","DLSSNR.SkinStructureStrength","DLSSNR.MVecScaleX","DLSSNR.MVecScaleY"})if(!Float(p,k,&out.values[n++]))return false;
    if(!UInt(p,"DLSSNR.Style",&out.modes[0])||!UInt(p,"DLSSNR.DepthInverted",&out.modes[5]))return false;
    out.modes[1]=creation.preset;n=2;
    for(auto k:{"DLSSNR.Enabled","DLSSNR.UseAutoMask","DLSSNR.UICorrection"}){int v=0;if(!Int(p,k,&v))return false;out.modes[n++]=v?1u:0u;}
    return out.valid();
}
inline bool Resource(const NVSDK_NGX_Parameter* p,const char* k,ID3D11Resource** o){__try{return p&&p->Get(k,o)==NVSDK_NGX_Result_Success;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
inline std::string Uint(const NVSDK_NGX_Parameter* p,const char* k){unsigned v=0;return UInt(p,k,&v)?std::to_string(v):"null";}
inline std::string Sint(const NVSDK_NGX_Parameter* p,const char* k){int v=0;return Int(p,k,&v)?std::to_string(v):"null";}
inline std::string Flt(const NVSDK_NGX_Parameter* p,const char* k){float v=0;if(!Float(p,k,&v)||!std::isfinite(v))return "null";std::ostringstream s;s<<std::setprecision(9)<<v;return s.str();}
inline std::string Texture(const NVSDK_NGX_Parameter* p,const char* k){
    ID3D11Resource* raw=nullptr;if(!Resource(p,k,&raw)||!raw)return "null";
    ComPtr<ID3D11Texture2D> t;if(FAILED(raw->QueryInterface(IID_PPV_ARGS(&t))))return "null";
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);ComPtr<ID3D11Device> dev;t->GetDevice(&dev);
    std::ostringstream s;s<<"{\"resource\":"<<ets2_capture::Quote(std::to_string(reinterpret_cast<uint64_t>(raw)))<<",\"device\":"<<ets2_capture::Quote(std::to_string(reinterpret_cast<uint64_t>(dev.Get())))<<",\"width\":"<<d.Width<<",\"height\":"<<d.Height<<",\"format\":"<<unsigned(d.Format)<<",\"layers\":"<<d.ArraySize<<",\"mips\":"<<d.MipLevels<<",\"samples\":"<<d.SampleDesc.Count<<"}";return s.str();
}
inline void Observe11(ID3D11DeviceContext* ctx,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,NVSDK_NGX_Result result){
    std::ostringstream j;j<<"{\"feature\":"<<ets2_capture::Quote(std::to_string(reinterpret_cast<uint64_t>(h)))<<",\"result\":"<<unsigned(result)<<",\"immediate_context\":"<<(ctx&&ctx->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE?"true":"false")<<",\"eye_identity\":\"unverified\"";
    for(auto k:{"Color","Output","Depth","MotionVectors","ExposureTexture"})j<<','<<ets2_capture::Quote(k)<<':'<<Texture(p,k);
    for(auto k:{"Width","Height","OutWidth","OutHeight","PerfQualityValue","DLSS.Feature.Create.Flags","DLSS.Render.Subrect.Dimensions.Width","DLSS.Render.Subrect.Dimensions.Height","DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y","DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y","DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"})j<<','<<ets2_capture::Quote(k)<<':'<<Uint(p,k);
    for(auto k:{"MV.Scale.X","MV.Scale.Y","Jitter.Offset.X","Jitter.Offset.Y","DLSS.Pre.Exposure","DLSS.Exposure.Scale"})j<<','<<ets2_capture::Quote(k)<<':'<<Flt(p,k);
    j<<",\"Reset\":"<<Sint(p,"Reset")<<"}";
    auto& s=Get();std::lock_guard lock(s.mutex);++s.nativeCalls;
    s.native=j.str();s.native.pop_back();s.native+=",\"observation\":"+std::to_string(s.nativeCalls)+",\"tick_ms\":"+std::to_string(GetTickCount64())+",\"thread_id\":"+std::to_string(GetCurrentThreadId())+"}";
    s.recentNative.push_back(s.native);if(s.recentNative.size()>32)s.recentNative.pop_front();
    s.reason="Game D3D11 evaluations observed; eye mapping and native-guide validity are not established.";
}
inline void ObserveNr(const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,NVSDK_NGX_Result result,bool managed=false,float userTone=0,int userReset=0,float userStructure=0,float userIntensity=0){
    auto& s=Get();std::lock_guard lock(s.mutex);
    if(eye<0||eye>1||pass<0||pass>1){++s.unownedNr;return;}
    ++s.nrCalls;std::ostringstream j;const auto id=reinterpret_cast<uint64_t>(h);auto found=s.presets.find(id);
    auto& d=s.display[eye+2*pass];d.seen=true;d.frameId=frame;d.presetKnown=found!=s.presets.end()&&found->second!="null";d.preset=d.presetKnown?unsigned(strtoul(found->second.c_str(),nullptr,10)):0;d.intensityKnown=Float(p,"DLSSNR.Intensity",&d.intensity)&&std::isfinite(d.intensity);d.styleKnown=UInt(p,"DLSSNR.Style",&d.style);d.result=unsigned(result);
    j<<"{\"eye\":"<<eye<<",\"pass\":"<<pass+1<<",\"frame_id\":"<<frame<<",\"result\":"<<unsigned(result)<<",\"preset\":"<<(found==s.presets.end()?"null":found->second)<<",\"feature\":"<<ets2_capture::Quote(std::to_string(id));
    for(auto k:{"DLSSNR.Intensity","DLSSNR.LocalToneStrength","DLSSNR.LocalStructureStrength","DLSSNR.SkinStructureStrength","DLSSNR.MVecScaleX","DLSSNR.MVecScaleY"})j<<','<<ets2_capture::Quote(k)<<':'<<Flt(p,k);
    for(auto k:{"DLSSNR.Style","DLSSNR.DepthInverted"})j<<','<<ets2_capture::Quote(k)<<':'<<Uint(p,k);
    for(auto k:{"DLSSNR.Reset","DLSSNR.Enabled","DLSSNR.UseAutoMask","DLSSNR.UICorrection"})j<<','<<ets2_capture::Quote(k)<<':'<<Sint(p,k);
    j<<",\"observed_qpc\":"<<ets2_temporal::Qpc();
    j<<",\"managed_stereo_controls\":"<<(managed?"true":"false");
    if(managed)j<<",\"user_local_tone\":"<<userTone<<",\"user_local_structure\":"<<userStructure<<",\"user_intensity\":"<<userIntensity<<",\"incoming_reset\":"<<userReset<<",\"control_attempt\":"<<Controls().Attempt()<<",\"common_reset\":"<<(Controls().CommonReset()?"true":"false");
    j<<"}";s.settings[eye+2*pass]=j.str();
}
inline void ObserveNrNoThrow(const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,NVSDK_NGX_Result result,bool managed,float tone,int reset,float structure,float intensity){try{ObserveNr(h,p,result,managed,tone,reset,structure,intensity);}catch(...){}}
// Separate SEH-only function: restoration also runs if the original raises a
// Windows exception. No borrowed map survives this call or enters a worker.
inline NVSDK_NGX_Result EvaluateBorrowed(Eval12 original,ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb,float userTone,int userReset,float userStructure,float userIntensity){
    NVSDK_NGX_Result result=static_cast<NVSDK_NGX_Result>(0x7FFFFFFF);
    __try{result=original(list,h,p,cb);ObserveNrNoThrow(h,p,result,true,userTone,userReset,userStructure,userIntensity);}
    __finally{
        const bool toneOk=SetFloat(p,"DLSSNR.LocalToneStrength",userTone);
        const bool structureOk=SetFloat(p,"DLSSNR.LocalStructureStrength",userStructure);
        const bool resetOk=SetInt(p,"DLSSNR.Reset",userReset);
        const bool intensityOk=SetFloat(p,"DLSSNR.Intensity",userIntensity);
        if(!toneOk||!structureOk||!resetOk||!intensityOk){Controls().RejectAttempt();Log("[stereo-controls] borrowed parameter restoration failed; current processed frame will not be displayed");}
    }
    return result;
}
template<unsigned Slot> inline NVSDK_NGX_Result __cdecl EvalGame(ID3D11DeviceContext* ctx,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb){
    const bool outer=nesting11++==0;
    auto r=reinterpret_cast<Eval11>(Get().hooks[Slot].original)(ctx,h,p,cb);
    --nesting11;if(outer){try{Observe11(ctx,h,p,r);}catch(...){}}
    return r;
}
inline NVSDK_NGX_Result __cdecl EvalNeural(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb){
    bool neural=false;Creation creation{};
    {auto& s=Get();std::lock_guard lock(s.mutex);const auto id=reinterpret_cast<uint64_t>(h);neural=s.presets.count(id)!=0;auto it=s.creations.find(id);if(it!=s.creations.end())creation=it->second;}
    auto original=reinterpret_cast<Eval12>(Get().hooks[2].original);
    const bool owned=neural&&eye>=0&&eye<2&&pass>=0&&pass<2&&ownedList==list&&Controls().Active();
    if(owned&&creation.serial&&creation.parameters==p){
        ets2_nr_control::Controls incoming{};int reset=0;
        if(ReadControls(p,creation,incoming,reset)){
            const auto decision=Controls().Observe(unsigned(eye+2*pass),incoming,creation.serial,reset!=0);
            if(decision.valid&&Controls().Enabled()){
                const bool toneOk=SetFloat(p,"DLSSNR.LocalToneStrength",decision.tone);
                const bool structureOk=SetFloat(p,"DLSSNR.LocalStructureStrength",decision.structure);
                const bool resetOk=SetInt(p,"DLSSNR.Reset",decision.reset?1:0);
                const bool intensityOk=SetFloat(p,"DLSSNR.Intensity",decision.intensity);
                if(!toneOk||!structureOk||!resetOk||!intensityOk){SetFloat(p,"DLSSNR.LocalToneStrength",incoming.values[1]);SetFloat(p,"DLSSNR.LocalStructureStrength",incoming.values[2]);SetInt(p,"DLSSNR.Reset",reset);SetFloat(p,"DLSSNR.Intensity",incoming.values[0]);Controls().RejectAttempt();}
                else return EvaluateBorrowed(original,list,h,p,cb,incoming.values[1],reset,incoming.values[2],incoming.values[0]);
            }
        }else Controls().RejectAttempt();
    }else if(owned)Controls().RejectAttempt();
    auto r=original(list,h,p,cb);
    if(neural){try{ObserveNr(h,p,r);}catch(...){}}return r;
}
inline NVSDK_NGX_Result __cdecl CreateNeural(ID3D12GraphicsCommandList* list,NVSDK_NGX_Feature feature,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h){
    auto r=reinterpret_cast<Create12>(Get().hooks[3].original)(list,feature,p,h);
    if(r==NVSDK_NGX_Result_Success&&h&&*h){try{auto& s=Get();std::lock_guard lock(s.mutex);const auto id=reinterpret_cast<uint64_t>(*h);if(s.presets.size()>64){s.presets.clear();s.creations.clear();}if(unsigned(feature)==18){s.presets[id]=Uint(p,"DLSSNR.Hint.Render.Preset");Creation c;c.parameters=p;c.serial=++s.creationSerial;c.presetKnown=UInt(p,"DLSSNR.Hint.Render.Preset",&c.preset);s.creations[id]=c;}else{s.presets.erase(id);s.creations.erase(id);}}catch(...){}}
    return r;
}
inline std::string Snapshot(){auto& s=Get();std::lock_guard lock(s.mutex);std::ostringstream j;
    j<<"{\"native_d3d11_evaluations\":"<<s.nativeCalls<<",\"owned_nr_evaluations\":"<<s.nrCalls<<",\"unowned_nr_evaluations\":"<<s.unownedNr<<",\"game_hooks\":"<<int(s.hooks[0].enabled)+int(s.hooks[1].enabled)<<",\"nr_hook\":"<<(s.hooks[2].enabled?"true":"false")<<",\"native_guides_usable\":false,\"reason\":"<<ets2_capture::Quote(s.reason)<<",\"last_native_contract\":"<<s.native<<",\"actual_nr_parameters\":[";
    for(size_t i=0;i<s.settings.size();++i){if(i)j<<',';j<<s.settings[i];}
    j<<"],\"recent_native_contracts\":[";for(size_t i=0;i<s.recentNative.size();++i){if(i)j<<',';j<<s.recentNative[i];}return j.str()+"]}";
}
inline void Install(unsigned slot,HMODULE module,const char* symbol,void* callback){
    auto& s=Get();std::lock_guard lock(s.mutex);auto& hook=s.hooks[slot];if(hook.enabled||!module)return;
    void* address=reinterpret_cast<void*>(GetProcAddress(module,symbol));if(!address)return;
    // Alias exports may resolve to the same function. Never take ownership of an
    // existing hook or remove someone else's trampoline.
    for(auto& other:s.hooks)if(other.enabled&&other.address==address)return;
    HMODULE self=nullptr,owner=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(callback),&self)||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(address),&owner))return;
    auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return;
    if(!hook.address){status=MH_CreateHook(address,callback,&hook.original);if(status!=MH_OK){Log("[native-observer] %s not hooked: %s",symbol,MH_StatusToString(status));return;}hook.address=address;}
    status=MH_EnableHook(address);hook.enabled=status==MH_OK||status==MH_ERROR_ENABLED;
    wchar_t file[MAX_PATH]={};GetModuleFileNameW(owner,file,MAX_PATH);hook.module=file;
    Log("[native-observer] %s %s in %ls; game/unowned calls passive; owned stereo control policy is reported per evaluation",symbol,hook.enabled?"observing":"unavailable",file);
}
inline void Poll(HMODULE self){
    auto& s=Get();const auto now=GetTickCount64();if(now<s.nextPoll)return;s.nextPoll=now+1000;
    // Invoked only from the serialized Feeder render/runtime path, outside DllMain.
    Install(0,GetModuleHandleW(L"nvngx_dlss.dll"),"NVSDK_NGX_D3D11_EvaluateFeature",reinterpret_cast<void*>(&EvalGame<0>));
    Install(1,GetModuleHandleW(L"_nvngx.dll"),"NVSDK_NGX_D3D11_EvaluateFeature",reinterpret_cast<void*>(&EvalGame<1>));
    // Observe the public core API. Interposing the signed NR snippet itself
    // invalidates its execution in controlled tests; its code stays untouched.
    Install(2,GetModuleHandleW(L"_nvngx.dll"),"NVSDK_NGX_D3D12_EvaluateFeature",reinterpret_cast<void*>(&EvalNeural));
    Install(3,GetModuleHandleW(L"_nvngx.dll"),"NVSDK_NGX_D3D12_CreateFeature",reinterpret_cast<void*>(&CreateNeural));
    if(s.path.empty()){wchar_t path[MAX_PATH]={};GetModuleFileNameW(self,path,MAX_PATH);s.path=std::filesystem::path(path).parent_path()/L"native-input-observer.json";}
    ets2_capture::AtomicText(s.path,Snapshot()+"\n");
}
inline void Draw(uint64_t delivered,unsigned activePasses){auto& s=Get();std::lock_guard lock(s.mutex);
    ImGui::TextUnformatted("Configured input: real matched depth + estimated motion");
    ImGui::TextWrapped("Native game guides: %s",s.reason.c_str());
    ImGui::Text("Observed game D3D11 calls: %llu; owned neural evaluations: %llu",s.nativeCalls,s.nrCalls);
    if(ImGui::TreeNode("Last verified neural parameters")){
        if(!s.nrCalls)ImGui::TextWrapped("Waiting to observe the consumer's actual evaluations. Saved INI values are not treated as proof.");
        for(unsigned i=0;i<4;++i){auto& d=s.display[i];if(!d.seen||i/2>=activePasses)continue;
            ImGui::Text("%s eye, pass %u: %s",i%2?"Right":"Left",i/2+1,d.result==1?"processed":"evaluation failed");
            ImGui::Text("  Evaluated frame %llu; latest delivered frame %llu",d.frameId,delivered);
            if(d.presetKnown)ImGui::Text("  Requested preset hint %u",d.preset);else ImGui::TextUnformatted("  Preset hint not observed yet");
            if(d.intensityKnown)ImGui::Text("  Intensity %.2f",d.intensity);
            ImGui::Text("  Style: %s",!d.styleKnown?"not observed":d.style==0?"Default":d.style==1?"Natural":d.style==2?"Cinematic":"unrecognized");
        }
        ImGui::TreePop();
    }
}
}
