#pragma once
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")

// GPU operations remain on the rendering thread. A detached, code-pinned writer
// receives CPU bytes only. One pending GPU packet and one writer at most; a new
// packet waits for the previous writer, so diagnostic frames need not be adjacent.
namespace ets2_capture {
using Microsoft::WRL::ComPtr;
inline std::string Quote(const std::string& s) {
    std::string o="\"";for(unsigned char c:s){if(c=='\\'||c=='\"'){o+='\\';o+=char(c);}else if(c=='\n')o+="\\n";else if(c=='\r')o+="\\r";else if(c=='\t')o+="\\t";else if(c<32){char b[8];sprintf_s(b,"\\u%04x",c);o+=b;}else o+=char(c);}return o+'\"';
}
inline std::string Utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string o(n,0);
    if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),o.data(),n,nullptr,nullptr);return o;
}
inline std::string ProcessName(){wchar_t p[MAX_PATH]={};GetModuleFileNameW(nullptr,p,MAX_PATH);return Utf8(std::filesystem::path(p).filename().wstring());}
inline unsigned Bpp(DXGI_FORMAT f) {
    switch(f){case DXGI_FORMAT_R8_UNORM:return 1;
    case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R16_UNORM:return 2;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R32G32_FLOAT:return 8;default:return 0;}
}
inline bool AtomicWrite(const std::filesystem::path& path,const void* data,size_t size) {
    auto tmp=path;tmp+=L".part";FILE* f=nullptr;
    if(_wfopen_s(&f,tmp.c_str(),L"wb")||!f)return false;
    bool ok=fwrite(data,1,size,f)==size;ok=fflush(f)==0&&ok;ok=fclose(f)==0&&ok;
    if(!ok)return false;
    return MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
inline bool AtomicText(const std::filesystem::path& p,const std::string& s){return AtomicWrite(p,s.data(),s.size());}
inline std::string Sha256(const std::vector<unsigned char>& data) {
    BCRYPT_ALG_HANDLE a=nullptr;BCRYPT_HASH_HANDLE h=nullptr;DWORD n=0,size=0;std::string out;
    if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return out;
    if(BCryptGetProperty(a,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&n,0)>=0){
        std::vector<unsigned char> obj(size);unsigned char hash[32];
        if(BCryptCreateHash(a,&h,obj.data(),size,nullptr,0,0)>=0){
            if(BCryptHashData(h,const_cast<PUCHAR>(data.data()),ULONG(data.size()),0)>=0&&BCryptFinishHash(h,hash,32,0)>=0){char b[3];for(auto c:hash){sprintf_s(b,"%02x",c);out+=b;}}
            BCryptDestroyHash(h);
        }
    }BCryptCloseAlgorithmProvider(a,0);return out;
}
struct Image {
    std::string name;D3D11_TEXTURE2D_DESC desc{};unsigned bpp=0,viewFormat=0;
    uint64_t source=0;ComPtr<ID3D11Texture2D> staging;std::vector<unsigned char> bytes;
};
struct Packet {
    unsigned index=0;uint64_t frame=0;bool comparison=true;std::vector<Image> images;std::string metadata;
    ComPtr<ID3D11Query> ready;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ULONGLONG started=0;size_t size=0;bool ended=false;
};
class Recorder {
    std::mutex mutex;
    std::atomic<bool> requested{false},writerBusy{false},writerFailed{false};
    std::atomic<unsigned> written{0};
    std::atomic<bool> blockedWritten{false};
    std::filesystem::path base,folder;std::unique_ptr<Packet> pending;
    bool active=false,autoRequested=false;unsigned target=4,planeMask=1023;uint64_t previousFrame=0;
    ULONGLONG started=0,lastRequestPoll=0;std::string status="Ready to capture four complete stereo frames.";
    // Native 5008x2600 SDR-FP16 diagnostics require 42 bytes per pixel
    // across inputs, output and work planes: 546,873,600 bytes (~522 MiB).
    // Keep one bounded packet/writer, with room for this supported VR extent.
    std::string requestToken;size_t budget=640ull*1024*1024;
    static constexpr unsigned kTimeoutMs=60000;
    void Fail(const std::string& reason) {
        if(pending)Log("[capture] failed packet index=%u frame=%llu comparison=%u ended=%u age_ms=%llu copies=%llu bytes=%llu context=%p device=%p",pending->index,pending->frame,unsigned(pending->comparison),unsigned(pending->ended),GetTickCount64()-pending->started,uint64_t(pending->images.size()),uint64_t(pending->size),pending->context.Get(),pending->device.Get());
        pending.reset();active=false;status=reason;
        if(!folder.empty())AtomicText(folder/L"session.json","{\"schema\":1,\"complete\":false,\"requested\":"+std::to_string(target)+",\"written\":"+std::to_string(written.load())+",\"reason\":"+Quote(reason)+"}\n");
        Log("[capture] incomplete: %s",reason.c_str());
    }
    bool Add(ID3D11DeviceContext* ctx,ID3D11Texture2D* tex,const char* name,unsigned view=0) {
        if(planeMask!=1023){
            const char* names[]={"original","depth","motion","mask","result","work-color","work-depth","work-motion","work-result","work-mask"};
            for(unsigned i=0;i<10;++i)if(strcmp(name,names[i])==0&&!(planeMask&(1u<<i)))return true;
        }
        if(!pending||!tex)return false;Image im;im.name=name;tex->GetDesc(&im.desc);im.bpp=Bpp(im.desc.Format);im.viewFormat=view;
        ComPtr<ID3D11Device> dev;tex->GetDevice(&dev);
        const uint64_t size=uint64_t(im.desc.Width)*im.desc.Height*im.bpp;
        // ReShade may return its proxy from Texture::GetDevice but the native
        // device from the unwrapped context. All input textures come from the
        // already-validated Feeder device; use that device to make staging.
        if(!im.bpp||!im.desc.Width||im.desc.Width%2||!im.desc.Height||im.desc.ArraySize!=1||im.desc.MipLevels!=1||im.desc.SampleDesc.Count!=1||size>budget||pending->size+size>budget){
            Log("[capture] input rejected kind=%s devices=%p/%p extent=%ux%u format=%u layers=%u mips=%u samples=%u bytes=%llu total=%llu",name,dev.Get(),pending->device.Get(),im.desc.Width,im.desc.Height,unsigned(im.desc.Format),im.desc.ArraySize,im.desc.MipLevels,im.desc.SampleDesc.Count,size,uint64_t(pending->size+size));return false;
        }
        im.source=reinterpret_cast<uint64_t>(tex);pending->size+=size;
        auto sd=im.desc;sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;sd.MiscFlags=0;
        const HRESULT created=pending->device->CreateTexture2D(&sd,nullptr,&im.staging);
        Log("[capture] copy=%llu kind=%s source=%p owner=%p staging_device=%p context=%p extent=%ux%u format=%u binds=0x%X misc=0x%X staging=0x%08X",uint64_t(pending->images.size()),name,tex,dev.Get(),pending->device.Get(),ctx,im.desc.Width,im.desc.Height,unsigned(im.desc.Format),im.desc.BindFlags,im.desc.MiscFlags,created);
        if(FAILED(created)){FeedCaptureGpuDiagnostic("create staging",created);return false;}
        D3D11_TEXTURE2D_DESC actual{};im.staging->GetDesc(&actual);
        if(memcmp(&actual,&sd,sizeof(sd))!=0){
            Log("[capture] staging descriptor changed kind=%s extent=%ux%u format=%u usage=%u binds=0x%X cpu=0x%X misc=0x%X samples=%u/%u mips=%u layers=%u",name,actual.Width,actual.Height,unsigned(actual.Format),unsigned(actual.Usage),actual.BindFlags,actual.CPUAccessFlags,actual.MiscFlags,actual.SampleDesc.Count,actual.SampleDesc.Quality,actual.MipLevels,actual.ArraySize);
            return false;
        }
        ctx->CopyResource(im.staging.Get(),tex);pending->images.push_back(std::move(im));return true;
    }
    static void WritePacket(Recorder* self,std::filesystem::path path,std::unique_ptr<Packet> packet) {
        bool ok=true;const std::string prefix=packet->comparison?("frame-"+std::to_string(packet->index)):"blocked-frame";
        std::ostringstream json;json<<"{\"schema\":1,\"complete\":"<<(packet->comparison?"true":"false")<<",\"comparison\":"<<(packet->comparison?"true":"false")<<",\"index\":"<<packet->index<<",\"frame_id\":"<<packet->frame<<",\"layout\":\"side_by_side\",\"eyes\":[\"left\",\"right\"],\"metadata\":"<<packet->metadata<<",\"images\":[";
        for(size_t i=0;i<packet->images.size();++i){auto& im=packet->images[i];std::string name=prefix+"-"+im.name+".raw";
            std::string hash=Sha256(im.bytes);if(hash.empty()||!AtomicWrite(path/name,im.bytes.data(),im.bytes.size())){ok=false;break;}
            if(i)json<<',';json<<"{\"kind\":"<<Quote(im.name)<<",\"file\":"<<Quote(name)<<",\"width\":"<<im.desc.Width<<",\"height\":"<<im.desc.Height<<",\"format\":"<<unsigned(im.desc.Format)<<",\"view_format\":"<<im.viewFormat<<",\"bytes_per_pixel\":"<<im.bpp<<",\"row_bytes\":"<<im.desc.Width*im.bpp<<",\"bytes\":"<<im.bytes.size()<<",\"source_resource\":"<<Quote(std::to_string(im.source))<<",\"sha256\":"<<Quote(hash)<<"}";
        }
        json<<"]}\n";
        auto text=json.str();
        if(self->planeMask!=1023){const auto p=text.find("\"complete\":true");if(p!=std::string::npos)text.replace(p,15,"\"complete\":false");text.insert(1,"\"diagnostic_copy_mask\":"+std::to_string(self->planeMask)+",");}
        if(ok)ok=AtomicText(path/(prefix+".json"),text);
        if(ok){if(packet->comparison)self->written.fetch_add(1);else self->blockedWritten.store(true);}else self->writerFailed.store(true);
        self->writerBusy.store(false);
    }
public:
    void Initialize(HMODULE module) {
        std::lock_guard lock(mutex);if(!base.empty())return;
        wchar_t path[MAX_PATH]={};if(!GetModuleFileNameW(module,path,MAX_PATH))return;
        base=std::filesystem::path(path).parent_path();
        // A launcher request from a previous game process is already consumed.
        // Only a new token or a new in-game button press starts a capture here.
        FILE* old=nullptr;
        if(_wfopen_s(&old,(base/L"dlss5-capture.request").c_str(),L"rb")==0&&old){
            char token[128]={};fread(token,1,127,old);fclose(old);requestToken=token;
        }
        char mask[24]={};if(GetEnvironmentVariableA("ETS2_FEED_CAPTURE_PLANE_MASK",mask,sizeof(mask))){char* end=nullptr;const auto value=strtoul(mask,&end,0);if(end&&!*end&&value<=1023)planeMask=unsigned(value);}
    }
    void Request(){requested.store(true);}
    void Cancel(const char* why){std::lock_guard lock(mutex);requested.store(false);if(active||pending)Fail(why);}
    void Draw() {
        std::lock_guard lock(mutex);
        if(ImGui::Button("Capture comparison frames"))requested.store(true);
        ImGui::TextWrapped("%s",status.c_str());
        if(planeMask!=1023)ImGui::Text("LAB DIAGNOSTIC: plane mask %u; full comparisons disabled",planeMask);
        if(active)ImGui::Text("Saved %u / %u complete frames",written.load(),target);
        if(!folder.empty())ImGui::TextWrapped("Saved in: %s",Utf8(folder.wstring()).c_str());
        ImGui::TextWrapped("Saves color, matched depth, supplied motion and neural output for both eyes. If depth is missing, saves the original image and failure details instead. Capture may briefly slow rendering.");
    }
    bool WantsFrame(){std::lock_guard lock(mutex);return active&&!pending&&!writerBusy&&written<target;}
    bool Busy(){std::lock_guard lock(mutex);return active||pending||writerBusy;}
    void Poll(ID3D11DeviceContext* ctx,uint64_t frame) {
        std::lock_guard lock(mutex);if(base.empty())return;
        const auto now=GetTickCount64();
        if(now-lastRequestPoll>250){lastRequestPoll=now;
            FILE* f=nullptr;auto request=base/L"dlss5-capture.request";
            if(_wfopen_s(&f,request.c_str(),L"rb")==0&&f){char token[128]={};fread(token,1,127,f);fclose(f);if(token[0]&&requestToken!=token){requestToken=token;requested.store(true);}}
        }
        char aut[24]={};if(!autoRequested&&GetEnvironmentVariableA("ETS2_FEED_CAPTURE_AFTER",aut,sizeof(aut))&&frame>=strtoull(aut,nullptr,10)){autoRequested=true;requested.store(true);}
        if(requested.exchange(false)&&!active&&!writerBusy){
            if(base.empty())return;HMODULE pin=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&WritePacket),&pin)){status="Capture unavailable: code lifetime could not be secured.";return;}
            SYSTEMTIME t;GetLocalTime(&t);wchar_t name[120];swprintf_s(name,L"%04u%02u%02u-%02u%02u%02u-%lu-%llu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),now);
            folder=base/L"DLSS5-Captures"/name;std::error_code ec;std::filesystem::create_directories(folder,ec);
            if(ec){status="Could not create the capture folder.";return;}
            written=0;writerFailed=false;blockedWritten=false;started=now;previousFrame=0;active=true;status="Waiting for a complete VR evaluation; keep the driving scene rendering.";
            if(!AtomicText(folder/L"session.json","{\"schema\":1,\"complete\":false,\"requested\":4,\"written\":0,\"reason\":\"capture in progress\"}\n")){Fail("Could not write the capture manifest.");return;}
            // Configuration snapshots are evidence of saved settings, not a claim
            // that an independently controlled consumer has applied them live.
            for(auto name:{L"ReShade.ini",L"ReShadeVR.ini",L"dlss5-feed.cfg"})std::filesystem::copy_file(base/name,folder/name,std::filesystem::copy_options::overwrite_existing,ec);
            Log("[capture] requested four paired frames in %ls",folder.c_str());
        }
        if(!active)return;
        if(blockedWritten&&!writerBusy){
            active=false;status="Neural rendering was blocked. Original stereo image and depth-route diagnostics saved; no neural comparison was produced.";
            AtomicText(folder/L"session.json","{\"schema\":1,\"complete\":false,\"requested\":4,\"written\":"+std::to_string(written.load())+",\"diagnostic_saved\":true,\"reason\":\"Verified stereo depth unavailable; original image retained for diagnosis.\"}\n");
            std::error_code ec;for(auto name:{L"depth-match.log",L"depth-route-details.jsonl",L"native-input-observer.json"})std::filesystem::copy_file(base/name,folder/name,std::filesystem::copy_options::overwrite_existing,ec);
            Log("[capture] blocked original saved; no neural comparison claimed");return;
        }
        if(writerFailed){Fail("A capture file could not be written; partial files retained.");return;}
        if(written==target&&!writerBusy){
            if(planeMask!=1023){
                AtomicText(folder/L"session.json","{\"schema\":1,\"complete\":false,\"diagnostic_complete\":true,\"diagnostic_copy_mask\":"+std::to_string(planeMask)+",\"requested\":4,\"written\":4}\n");
                active=false;status="Diagnostic copy experiment complete; this is not a full comparison capture.";Log("[capture] diagnostic mask=%u completed",planeMask);return;
            }
            if(!AtomicText(folder/L"session.json","{\"schema\":1,\"complete\":true,\"requested\":4,\"written\":4,\"temporal_sequence\":false,\"note\":\"Frame IDs record gaps; prior neural history is not included.\"}\n")){Fail("Frames were written, but the completion manifest could not be saved.");return;}
            active=false;status="Capture complete: four full stereo frames saved.";
            Log("[capture] complete: four paired frames saved");return;
        }
        if(now-started>kTimeoutMs){Fail("Capture timed out before four complete frames; partial files retained.");return;}
        if(!pending||!pending->ended)return;
        if(ctx!=pending->context.Get()){Fail("Rendering context changed during capture.");return;}
        BOOL done=FALSE;HRESULT hr=ctx->GetData(pending->ready.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(FAILED(hr)){FeedCaptureGpuDiagnostic("GetData EVENT",hr);Fail("GPU capture completion failed.");return;}
        if(hr==S_FALSE||!done)return;
        for(auto& im:pending->images){
            if(!im.staging)continue; // an earlier map may have completed before a later nonblocking map
            D3D11_MAPPED_SUBRESOURCE map{};hr=ctx->Map(im.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
            if(hr==DXGI_ERROR_WAS_STILL_DRAWING)return;
            if(FAILED(hr)){FeedCaptureGpuDiagnostic(im.name.c_str(),hr);Fail("GPU capture readback failed.");return;}
            const size_t row=size_t(im.desc.Width)*im.bpp;
            if(!map.pData||map.RowPitch<row){ctx->Unmap(im.staging.Get(),0);Fail("GPU capture row layout was invalid.");return;}
            try{im.bytes.resize(row*im.desc.Height);}catch(...){ctx->Unmap(im.staging.Get(),0);Fail("Capture CPU memory allocation failed.");return;}
            for(unsigned y=0;y<im.desc.Height;++y)memcpy(im.bytes.data()+y*row,static_cast<const unsigned char*>(map.pData)+y*map.RowPitch,row);
            ctx->Unmap(im.staging.Get(),0);im.staging.Reset();
        }
        pending->ready.Reset();pending->context.Reset();pending->device.Reset();
        writerBusy=true;auto job=std::move(pending);auto dest=folder;
        try{std::thread([this,dest,packet=std::move(job)]()mutable{
            try{WritePacket(this,dest,std::move(packet));}catch(...){writerFailed=true;writerBusy=false;}
        }).detach();}catch(...){writerFailed=true;writerBusy=false;Fail("Capture writer could not start.");}
        status="Writing complete stereo frame to disk.";
    }
    bool Begin(ID3D11DeviceContext* ctx,uint64_t frame,ID3D11Texture2D* color,unsigned colorView,ID3D11Texture2D* depth,ID3D11Texture2D* motion,ID3D11Texture2D* mask,std::string metadata) {
        std::lock_guard lock(mutex);if(!active||pending||writerBusy||written>=target||frame==previousFrame)return false;
        pending=std::make_unique<Packet>();pending->index=written;pending->frame=frame;pending->started=GetTickCount64();pending->context=ctx;ctx->GetDevice(&pending->device);pending->metadata=std::move(metadata);
        ULARGE_INTEGER free{};
        // The per-packet RAM ceiling is not the disk size: lower-resolution
        // presets and selected diagnostic planes need substantially fewer bytes.
        // End checks the exact complete packet before any raw files are written.
        if(!GetDiskFreeSpaceExW(folder.c_str(),&free,nullptr,nullptr)||free.QuadPart<64*1024*1024){Fail("Insufficient free space for the capture request.");return false;}
        if(!Add(ctx,color,"original",colorView)||!Add(ctx,depth,"depth")||!Add(ctx,motion,"motion")||(mask&&!Add(ctx,mask,"mask"))){Fail("Capture inputs have an unsupported layout, format, device or memory requirement.");return false;}
        previousFrame=frame;status="Copying a full stereo frame.";return true;
    }
    void Blocked(ID3D11DeviceContext* ctx,uint64_t frame,ID3D11Texture2D* color,unsigned colorView,const std::string& metadata) {
        std::lock_guard lock(mutex);if(!active||pending||writerBusy||blockedWritten)return;
        pending=std::make_unique<Packet>();pending->comparison=false;pending->frame=frame;pending->started=GetTickCount64();pending->context=ctx;ctx->GetDevice(&pending->device);pending->metadata=metadata;
        ULARGE_INTEGER free{};
        if(!GetDiskFreeSpaceExW(folder.c_str(),&free,nullptr,nullptr)||free.QuadPart<budget+64*1024*1024){Fail("Insufficient free space for the original-image diagnostic.");return;}
        if(!Add(ctx,color,"original",colorView)){Fail("Original image could not be captured with a supported layout.");return;}
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
        if(FAILED(pending->device->CreateQuery(&q,&pending->ready))){Fail("Original-image completion query could not be created.");return;}
        ctx->End(pending->ready.Get());pending->ended=true;status="Depth missing: saving the original stereo image and failure details.";
    }
    void End(ID3D11DeviceContext* ctx,bool delivered,ID3D11Texture2D* result,ID3D11Texture2D* workColor,ID3D11Texture2D* workDepth,ID3D11Texture2D* workMotion,ID3D11Texture2D* workResult,ID3D11Texture2D* workMask,const std::string& extra) {
        std::lock_guard lock(mutex);if(!pending||pending->ended)return;
        if(!delivered){Fail("Neural evaluation did not deliver this frame; no paired result was recorded.");return;}
        const auto resultView=pending->images.empty()?0:pending->images.front().viewFormat;
        if(!Add(ctx,result,"result",resultView)||!Add(ctx,workColor,"work-color")||!Add(ctx,workDepth,"work-depth")||!Add(ctx,workMotion,"work-motion")||!Add(ctx,workResult,"work-result")||(workMask&&!Add(ctx,workMask,"work-mask"))){Fail("Capture work/output resources exceeded the supported format or memory budget.");return;}
        ULARGE_INTEGER free{};
        const uint64_t remaining=uint64_t(pending->size)*(target-written.load())+64ull*1024*1024;
        if(!GetDiskFreeSpaceExW(folder.c_str(),&free,nullptr,nullptr)||free.QuadPart<remaining){Fail("Insufficient free space for the remaining comparison frames.");return;}
        if(!extra.empty()){pending->metadata.pop_back();pending->metadata+=","+extra+"}";}
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
        if(FAILED(pending->device->CreateQuery(&q,&pending->ready))){Fail("Capture completion query could not be created.");return;}
        // The normal frame submits this work after caller state is restored.
        // Do not introduce an extra Flush while the capture state is active.
        ctx->End(pending->ready.Get());pending->ended=true;
        FeedCaptureGpuDiagnostic("EVENT submitted",S_OK);
    }
};
// Explicitly retain synchronization state until process exit. Normal runtime
// destruction releases pending GPU resources; worker threads never use them.
inline Recorder& Get(){static auto* recorder=new Recorder;return *recorder;}
}
