#pragma once
#include <deque>

// A bounded burst of consecutive accepted Feeder callbacks, not headset frames.
// GPU copies and nonblocking readback stay on the rendering thread. CPU packets
// are retained until the burst ends; disk writing never gates the next sample.
// No hidden neural/Kernel history is serializable: the manifest says so.
namespace ets2_temporal {
using namespace ets2_capture;
struct Sample {
    Packet packet;
    uint64_t callback=0, inputQpc=0, readbackQpc=0;
};
inline uint64_t Qpc(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return uint64_t(t.QuadPart);}
inline std::string GuideSettings(reshade::api::effect_runtime* rt){
    std::ostringstream j;j<<"{";bool comma=false;
    const auto key=[&](const char* k){if(comma)j<<',';comma=true;j<<Quote(k)<<':';};
    for(auto name:{"MV_VALIDATE","VALIDATE_STATIC","STATIC_HYSTERESIS","VALIDATE_DEPTH","VALIDATE_MV","VALIDATE_LUMA"}){
        auto v=rt->find_uniform_variable("DLSS5_Feed.fx",name);key(name);
        if(v.handle){bool b=false;rt->get_uniform_value_bool(v,&b,1);j<<(b?"true":"false");}else j<<"null";
    }
    for(auto name:{"STATIC_MIN_CONTRAST","STATIC_BIAS","DEPTH_TOLERANCE","MV_CONSISTENCY","MV_SCALE","MASK_STRENGTH"}){
        auto v=rt->find_uniform_variable("DLSS5_Feed.fx",name);key(name);
        if(v.handle){float f=0;rt->get_uniform_value_float(v,&f,1);if(std::isfinite(f))j<<std::setprecision(9)<<f;else j<<"null";}else j<<"null";
    }
    return j.str()+"}";
}
inline ComPtr<ID3D11Texture2D> KernelTexture(reshade::api::effect_runtime* rt,const char* name){
    ComPtr<ID3D11Texture2D> texture;
    if(!rt)return texture;
    // Feed declares exactly the shared stitched Kernel textures. Restricting the
    // effect excludes the similarly named individual-eye pyramid textures.
    auto variable=rt->find_texture_variable("DLSS5_Feed.fx",name);
    if(!variable.handle)return texture;
    reshade::api::resource_view srv{},srgb{};rt->get_texture_binding(variable,&srv,&srgb);
    if(!srv.handle)return texture;
    auto resource=rt->get_device()->get_resource_from_view(srv);
    if(resource.handle)reinterpret_cast<ID3D11Resource*>(resource.handle)->QueryInterface(IID_PPV_ARGS(&texture));
    return texture;
}
class Recorder {
    std::mutex mutex;
    std::atomic<bool> requested{false},writing{false},writeFailed{false},writeDone{false};
    std::atomic<unsigned> written{0};
    std::filesystem::path base,folder;
    std::deque<std::unique_ptr<Sample>> gpu;
    std::vector<std::unique_ptr<Sample>> cpu;
    std::unique_ptr<Sample> current;
    bool active=false,autoRequested=false;
    unsigned target=24,requestedMax=24,submitted=0;
    uint64_t callback=0,lastCallback=0,lastFrame=0,frequency=0;
    ULONGLONG startMs=0,nextPollMs=0;
    size_t sampleBytes=0,memoryBudget=0;
    std::string token,status="Ready to record a short consecutive motion test.";
    static constexpr size_t kSampleMax=512ull*1024*1024;
    static constexpr unsigned kGpuQueue=4;
    std::string Session(bool complete,const std::string& reason) const {
        std::ostringstream j;
        j<<"{\"schema\":2,\"complete\":"<<(complete?"true":"false")
         <<",\"kind\":\"consecutive_feeder_burst\",\"requested\":"<<target
         <<",\"requested_max\":"<<requestedMax<<",\"memory_limited\":"<<(target<requestedMax?"true":"false")
         <<",\"submitted\":"<<submitted<<",\"written\":"<<written.load()
         <<",\"qpc_frequency\":"<<frequency<<",\"temporal_sequence\":"<<(complete?"true":"false")
         <<",\"consecutive_domain\":\"accepted serialized VR Feeder callbacks\""
         <<",\"openxr_frame_ids_available\":false,\"initial_neural_history_saved\":false"
         <<",\"initial_motion_history_saved\":false,\"capture_changes_pacing\":true"
         <<",\"input_planes\":[\"original\",\"depth\",\"motion\",\"mask\"]"
         <<",\"output_stage\":\"after Feed, before later ReShade effects and headset compositor\""
         <<",\"reason\":"<<Quote(reason)<<"}\n";
        return j.str();
    }
    void Fail(const std::string& reason) {
        active=false;current.reset();gpu.clear();cpu.clear();status=reason;
        if(!folder.empty())AtomicText(folder/L"sequence.json",Session(false,reason));
        Log("[temporal-capture] incomplete: %s",reason.c_str());
    }
    bool Add(ID3D11DeviceContext* ctx,ID3D11Texture2D* tex,const char* name,unsigned view=0) {
        if(!current||!tex)return false;
        auto& p=current->packet;Image im;im.name=name;tex->GetDesc(&im.desc);im.bpp=Bpp(im.desc.Format);im.viewFormat=view;
        const auto& d=im.desc;
        const size_t size=size_t(d.Width)*d.Height*im.bpp;
        if(!im.bpp||!d.Width||d.Width%2||!d.Height||d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||size>kSampleMax||p.size+size>kSampleMax)return false;
        const bool kernel=strcmp(name,"kernel_motion_uv")==0||strcmp(name,"kernel_confidence")==0;
        if(!kernel&&!p.images.empty()&&(d.Width!=p.images[0].desc.Width||d.Height!=p.images[0].desc.Height))return false;
        im.source=reinterpret_cast<uint64_t>(tex);p.size+=size;
        auto sd=d;sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;sd.MiscFlags=0;
        if(FAILED(p.device->CreateTexture2D(&sd,nullptr,&im.staging)))return false;
        ctx->CopyResource(im.staging.Get(),tex);p.images.push_back(std::move(im));return true;
    }
    static void Write(Recorder* self,std::filesystem::path path,std::vector<std::unique_ptr<Sample>> samples) {
        bool ok=true;
        for(auto& s:samples){
            auto& p=s->packet;const auto prefix="frame-"+std::to_string(p.index);
            std::ostringstream j;
            j<<"{\"schema\":2,\"complete\":true,\"index\":"<<p.index<<",\"frame_id\":"<<p.frame
             <<",\"callback_id\":"<<s->callback<<",\"input_qpc\":"<<s->inputQpc<<",\"readback_qpc\":"<<s->readbackQpc
             <<",\"layout\":\"side_by_side\",\"eyes\":[\"left\",\"right\"],\"metadata\":"<<p.metadata<<",\"images\":[";
            for(size_t i=0;i<p.images.size();++i){
                auto& im=p.images[i];const auto name=prefix+"-"+im.name+".raw";
                const auto hash=Sha256(im.bytes);
                if(hash.empty()||!AtomicWrite(path/name,im.bytes.data(),im.bytes.size())){ok=false;break;}
                if(i)j<<',';
                j<<"{\"kind\":"<<Quote(im.name)<<",\"file\":"<<Quote(name)<<",\"width\":"<<im.desc.Width
                 <<",\"height\":"<<im.desc.Height<<",\"format\":"<<unsigned(im.desc.Format)<<",\"view_format\":"<<im.viewFormat
                 <<",\"bytes_per_pixel\":"<<im.bpp<<",\"row_bytes\":"<<im.desc.Width*im.bpp<<",\"bytes\":"<<im.bytes.size()
                 <<",\"source_resource\":"<<Quote(std::to_string(im.source))<<",\"sha256\":"<<Quote(hash)<<"}";
                // Free each CPU plane after its write; no GPU references enter this thread.
                std::vector<unsigned char>().swap(im.bytes);
            }
            j<<"]}\n";
            if(!ok||!AtomicText(path/(prefix+".json"),j.str())){ok=false;break;}
            self->written.fetch_add(1);
        }
        // Publish completion even if the headset sleeps and Poll stops running.
        // No GPU objects are retained by these packets at this point.
        {std::lock_guard lock(self->mutex);
            if(ok)ok=AtomicText(path/L"sequence.json",self->Session(true,"Consecutive callbacks verified. Initial histories are not saved; replay must declare its initialization policy."));
            self->writeFailed.store(!ok);self->writeDone.store(ok);self->writing.store(false);
        }
    }
    void LaunchWriter() {
        active=false;writing=true;status="Burst captured. Saving frames; this can take a little while.";
        auto dest=folder;auto samples=std::move(cpu);
        try{
            std::thread([this,dest,samples=std::move(samples)]()mutable{
                try{Write(this,dest,std::move(samples));}
                catch(...){writeFailed=true;writing=false;}
            }).detach();
        }catch(...){writing=false;Fail("The motion capture writer could not start.");}
    }
public:
    void Initialize(HMODULE module){
        std::lock_guard lock(mutex);if(!base.empty())return;
        wchar_t path[MAX_PATH]={};if(!GetModuleFileNameW(module,path,MAX_PATH))return;
        base=std::filesystem::path(path).parent_path();LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart;
    }
    void Cancel(const char* why){std::lock_guard lock(mutex);requested=false;if(active||current||!gpu.empty())Fail(why);}
    bool Busy(){std::lock_guard lock(mutex);return active||writing;}
    bool WantsFrame(){std::lock_guard lock(mutex);return active&&submitted<target&&!current;}
    void Draw(){
        std::lock_guard lock(mutex);
        if(ImGui::Button("Record motion test"))requested=true;
        ImGui::TextWrapped("%s",status.c_str());
        if(active)ImGui::Text("Recorded %u / %u; read back %u",submitted,target,unsigned(cpu.size()));
        if(writing)ImGui::Text("Saved %u / %u",written.load(),target);
        ImGui::TextWrapped("Records up to 24 consecutive color, depth, motion and output frames for both eyes, fitting the burst to available memory. Uses several GB of memory and disk space and slows rendering. Continue looking around during the short recording burst. It does not measure normal VR performance.");
        if(!folder.empty())ImGui::TextWrapped("Saved in: %s",Utf8(folder.wstring()).c_str());
    }
    void Poll(ID3D11DeviceContext* ctx,bool otherCaptureBusy=false){
        std::lock_guard lock(mutex);++callback;if(base.empty())return;
        const auto now=GetTickCount64();
        if(writeFailed.exchange(false)){Fail("Motion capture disk write failed; partial files retained.");}
        if(writeDone.exchange(false)){
            status="Motion capture complete: consecutive inputs and outputs saved.";
            Log("[temporal-capture] %s",status.c_str());
        }
        if(now>=nextPollMs){
            nextPollMs=now+250;FILE* f=nullptr;
            if(!_wfopen_s(&f,(base/L"dlss5-temporal.request").c_str(),L"rb")&&f){char b[128]={};fread(b,1,127,f);fclose(f);if(b[0]&&token!=b){token=b;requested=true;}}
        }
        char aut[32]={};
        if(!autoRequested&&GetEnvironmentVariableA("ETS2_FEED_TEMPORAL_AFTER",aut,sizeof(aut))&&callback>=strtoull(aut,nullptr,10)){autoRequested=true;requested=true;}
        if(requested.exchange(false)&&!active&&!writing){
            if(otherCaptureBusy){status="Finish the four-frame comparison capture before starting a motion test.";return;}
            HMODULE pin=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Write),&pin)){status="Motion capture code lifetime could not be secured.";return;}
            target=24;char count[20]={};
            if(GetEnvironmentVariableA("ETS2_FEED_TEMPORAL_COUNT",count,sizeof(count)))target=std::clamp(unsigned(strtoul(count,nullptr,10)),4u,48u);
            requestedMax=target;
            SYSTEMTIME t{};GetLocalTime(&t);wchar_t name[120];swprintf_s(name,L"%04u%02u%02u-%02u%02u%02u-%lu-%llu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),now);
            folder=base/L"DLSS5-Motion-Captures"/name;std::error_code ec;std::filesystem::create_directories(folder,ec);
            if(ec){status="Could not create the motion capture folder.";return;}
            MEMORYSTATUSEX mem{};mem.dwLength=sizeof(mem);
            if(!GlobalMemoryStatusEx(&mem)){status="Could not check available memory for the motion capture.";return;}
            memoryBudget=size_t(std::min<uint64_t>(8ull*1024*1024*1024,mem.ullAvailPhys/2));
            current.reset();gpu.clear();cpu.clear();cpu.reserve(target);
            submitted=0;written=0;sampleBytes=0;lastCallback=lastFrame=0;startMs=now;active=true;writeFailed=writeDone=false;
            status="Waiting for the next complete VR evaluation to start the motion burst.";
            if(!AtomicText(folder/L"sequence.json",Session(false,"Capture in progress."))){Fail("Could not write the motion capture manifest.");return;}
            for(auto name:{L"ReShade.ini",L"ReShadeVR.ini",L"dlss5-feed.cfg"})std::filesystem::copy_file(base/name,folder/name,std::filesystem::copy_options::overwrite_existing,ec);
            Log("[temporal-capture] armed %u consecutive callbacks; memory budget %llu bytes",target,uint64_t(memoryBudget));
        }
        if(!active)return;
        if(now-startMs>60000){Fail("Motion capture timed out; no continuous sequence was completed.");return;}
        if(submitted&&submitted<target&&callback!=lastCallback+1){Fail("A Feeder callback did not deliver a capture frame. Sequence rejected rather than hiding the gap.");return;}
        while(!gpu.empty()){
            auto& s=*gpu.front();auto& p=s.packet;
            if(p.context.Get()!=ctx){Fail("Rendering context changed during the motion capture.");return;}
            BOOL ready=FALSE;HRESULT hr=ctx->GetData(p.ready.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(FAILED(hr)){Fail("GPU motion readback query failed.");return;}
            if(hr==S_FALSE||!ready)break;
            for(auto& im:p.images){
                if(!im.staging)continue;
                D3D11_MAPPED_SUBRESOURCE map{};hr=ctx->Map(im.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
                if(hr==DXGI_ERROR_WAS_STILL_DRAWING)return;
                if(FAILED(hr)){Fail("GPU motion readback map failed.");return;}
                const size_t row=size_t(im.desc.Width)*im.bpp;
                if(!map.pData||map.RowPitch<row){ctx->Unmap(im.staging.Get(),0);Fail("Invalid motion capture row layout.");return;}
                try{im.bytes.resize(row*im.desc.Height);}catch(...){ctx->Unmap(im.staging.Get(),0);Fail("Motion capture CPU allocation failed.");return;}
                for(unsigned y=0;y<im.desc.Height;++y)memcpy(im.bytes.data()+y*row,static_cast<const unsigned char*>(map.pData)+y*map.RowPitch,row);
                ctx->Unmap(im.staging.Get(),0);im.staging.Reset();
            }
            s.readbackQpc=Qpc();p.context.Reset();p.device.Reset();p.ready.Reset();
            cpu.push_back(std::move(gpu.front()));gpu.pop_front();
        }
        if(submitted==target&&gpu.empty())LaunchWriter();
    }
    bool Begin(ID3D11DeviceContext* ctx,uint64_t frame,ID3D11Texture2D* color,unsigned view,ID3D11Texture2D* depth,ID3D11Texture2D* mv,ID3D11Texture2D* mask,const std::string& metadata,reshade::api::effect_runtime* rt=nullptr){
        std::lock_guard lock(mutex);if(!active||current||submitted>=target)return false;
        if(gpu.size()>=kGpuQueue){Fail("GPU readback could not keep up within its bounded queue. Sequence rejected; reduce resolution for another capture.");return false;}
        if(submitted&&(callback!=lastCallback+1||frame!=lastFrame+1)){Fail("Nonconsecutive Feeder callback or evaluation identifiers; motion sequence rejected.");return false;}
        D3D11_TEXTURE2D_DESC desc{};if(color)color->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC dd{},md{},kd{};if(depth)depth->GetDesc(&dd);if(mv)mv->GetDesc(&md);if(mask)mask->GetDesc(&kd);
        auto raw=KernelTexture(rt,"tFlow"),confidence=KernelTexture(rt,"tConfidence");
        D3D11_TEXTURE2D_DESC rawDesc{},confDesc{};
        uint64_t extraBytes=0;
        if(raw&&confidence){
            raw->GetDesc(&rawDesc);confidence->GetDesc(&confDesc);
            const auto expectedW=2*((desc.Width/2)/8),expectedH=desc.Height/8;
            if(rawDesc.Format!=DXGI_FORMAT_R16G16_FLOAT||confDesc.Format!=DXGI_FORMAT_R16_FLOAT||
               rawDesc.Width!=expectedW||rawDesc.Height!=expectedH||confDesc.Width!=expectedW||confDesc.Height!=expectedH){
                Fail("Stitched Kernel diagnostic texture contract mismatch.");return false;
            }
            extraBytes=uint64_t(expectedW)*expectedH*6;
        }else {raw.Reset();confidence.Reset();}
        const uint64_t bytes=uint64_t(desc.Width)*desc.Height*17+extraBytes;
        const bool rgba8=desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if(!color||!depth||!mv||!mask||!rgba8||dd.Format!=DXGI_FORMAT_R32_FLOAT||md.Format!=DXGI_FORMAT_R16G16_FLOAT||kd.Format!=DXGI_FORMAT_R8_UNORM||bytes>kSampleMax||!bytes){Fail("Motion capture needs RGBA8 color, R32 depth, RG16F motion and R8 mask within the supported memory limit.");return false;}
        if(!sampleBytes){
            sampleBytes=size_t(bytes);
            const auto capacity=unsigned(memoryBudget/bytes);
            const auto fitting=capacity>kGpuQueue?capacity-kGpuQueue:0;
            if(fitting<target){
                if(fitting<8){Fail("Not enough free RAM for even an eight-frame full-resolution motion burst. Reduce resolution before recording.");return false;}
                target=fitting;
                if(!AtomicText(folder/L"sequence.json",Session(false,"Burst length reduced to fit available memory; capture in progress."))){Fail("Could not update the bounded motion capture manifest.");return false;}
                Log("[temporal-capture] using %u / %u requested frames to fit current memory budget",target,requestedMax);
            }
            const uint64_t projected=bytes*(target+kGpuQueue);
            ULARGE_INTEGER disk{};
            if(projected>memoryBudget){Fail("Not enough free RAM for this full-resolution motion burst. Reduce resolution before recording.");return false;}
            if(!GetDiskFreeSpaceExW(folder.c_str(),&disk,nullptr,nullptr)||disk.QuadPart<bytes*target+64*1024*1024){Fail("Not enough free disk space for the motion burst.");return false;}
        }else if(sampleBytes!=bytes){Fail("Motion capture image size changed during the burst.");return false;}
        current=std::make_unique<Sample>();current->callback=callback;current->inputQpc=Qpc();
        auto& p=current->packet;p.index=submitted;p.frame=frame;p.context=ctx;ctx->GetDevice(&p.device);p.metadata=metadata;
        if(!Add(ctx,color,"original",view)||!Add(ctx,depth,"depth")||!Add(ctx,mv,"motion")||!Add(ctx,mask,"mask")){Fail("Motion capture input layout or staging allocation failed.");return false;}
        if(raw&&(!Add(ctx,raw.Get(),"kernel_motion_uv")||!Add(ctx,confidence.Get(),"kernel_confidence"))){Fail("Kernel diagnostic capture allocation failed.");return false;}
        return true;
    }
    void End(ID3D11DeviceContext* ctx,bool delivered,ID3D11Texture2D* result,const std::string& extra){
        std::lock_guard lock(mutex);if(!current)return;
        if(!delivered){Fail("Neural evaluation failed during the burst. No continuous sequence claimed.");return;}
        auto& p=current->packet;
        if(!Add(ctx,result,"result",p.images.front().viewFormat)||p.size!=sampleBytes){Fail("Motion capture result layout or staging allocation failed.");return;}
        if(!extra.empty()){p.metadata.pop_back();p.metadata+=","+extra+"}";}
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
        if(FAILED(p.device->CreateQuery(&q,&p.ready))){Fail("Could not create the motion capture completion query.");return;}
        ctx->End(p.ready.Get()); // normal rendering submits the copy; no capture Flush
        lastCallback=current->callback;lastFrame=p.frame;++submitted;
        gpu.push_back(std::move(current));status="Recording consecutive inputs and outputs; keep the driving scene rendering.";
    }
};
inline Recorder& Get(){static auto* r=new Recorder;return *r;}
}
