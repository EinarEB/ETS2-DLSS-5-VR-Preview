#pragma once
// Offline fixture only. Never define ETS2_FEED_SEQUENCE_REPLAY in a game build.
// Memory mapping bounds resident input memory and avoids per-frame disk parsing.
#include <array>
#include <stdexcept>
namespace ets2_sequence_replay {
struct Frame {uint64_t sourceFrame=0,callback=0,qpc=0,frequency=0;unsigned flags=0;std::array<const unsigned char*,4> planes{};};
struct Data {
    HANDLE file=INVALID_HANDLE_VALUE,mapping=nullptr;const unsigned char* bytes=nullptr;
    uint64_t size=0;unsigned width=0,height=0,count=0,index=0;bool loaded=false,exhausted=false;
    std::vector<Frame> frames;
    ~Data(){if(bytes)UnmapViewOfFile(bytes);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
};
inline Data& Get(){static auto* d=new Data;return *d;}
template<class T> T Read(const Data& d,uint64_t& at){if(at>d.size||sizeof(T)>d.size-at)throw std::runtime_error("Truncated sequence header");T t{};memcpy(&t,d.bytes+at,sizeof(T));at+=sizeof(T);return t;}
inline void Load(Data& d){
    wchar_t path[32768]={};if(!GetEnvironmentVariableW(L"ETS2_REPLAY_SEQUENCE",path,32768))throw std::runtime_error("Offline sequence path missing");
    d.file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    LARGE_INTEGER size{};
    if(d.file==INVALID_HANDLE_VALUE||!GetFileSizeEx(d.file,&size)||size.QuadPart<32||size.QuadPart>16ll*1024*1024*1024)throw std::runtime_error("Invalid sequence file size");
    d.size=size.QuadPart;d.mapping=CreateFileMappingW(d.file,nullptr,PAGE_READONLY,0,0,nullptr);
    if(!d.mapping||(d.bytes=static_cast<const unsigned char*>(MapViewOfFile(d.mapping,FILE_MAP_READ,0,0,0)))==nullptr)throw std::runtime_error("Sequence file mapping failed");
    if(memcmp(d.bytes,"E2SEQ02",8))throw std::runtime_error("Invalid sequence magic");
    uint64_t at=8;d.width=Read<unsigned>(d,at);d.height=Read<unsigned>(d,at);d.count=Read<unsigned>(d,at);
    const auto planes=Read<unsigned>(d,at),version=Read<unsigned>(d,at),header=Read<unsigned>(d,at);
    if(!d.width||d.width%2||!d.height||uint64_t(d.width)*d.height>32ull*1024*1024||d.count<2||d.count>48||planes!=4||version!=2||header!=32)throw std::runtime_error("Invalid sequence dimensions/version/count");
    unsigned bpp[]={4,4,4,1};const uint64_t frameBytes=uint64_t(d.width)*d.height*13;
    if(d.size!=32+uint64_t(d.count)*(40+frameBytes))throw std::runtime_error("Sequence byte count does not match its declared frames");
    for(unsigned n=0;n<d.count;++n){
        Frame f;f.sourceFrame=Read<uint64_t>(d,at);f.callback=Read<uint64_t>(d,at);f.qpc=Read<uint64_t>(d,at);f.frequency=Read<uint64_t>(d,at);f.flags=Read<unsigned>(d,at);
        const auto size=Read<unsigned>(d,at);
        if(size!=frameBytes||!f.frequency||f.flags>1)throw std::runtime_error("Invalid frame header");
        if(n){const auto& p=d.frames.back();if(f.sourceFrame!=p.sourceFrame+1||f.callback!=p.callback+1||f.qpc<=p.qpc||f.frequency!=p.frequency)throw std::runtime_error("Nonconsecutive recorded frame identifiers or timestamps");}
        for(unsigned plane=0;plane<4;++plane){f.planes[plane]=d.bytes+at;at+=uint64_t(d.width)*d.height*bpp[plane];}
        d.frames.push_back(f);
    }
    d.loaded=true;Log("[sequence-replay] loaded %u verified-layout frames %ux%u; reset at start/recorded common resets, then preserve history; original hidden history unavailable",d.count,d.width,d.height);
}
inline bool Apply(ID3D11DeviceContext* ctx,ID3D11Texture2D* color,ID3D11Texture2D* depth,ID3D11Texture2D* mv,ID3D11Texture2D* mask,bool& reset){
    auto& d=Get();if(!d.loaded)Load(d);
    if(d.index>=d.count){if(!d.exhausted){Log("[sequence-replay] exhausted after %u inputs; further evaluations skipped",d.count);d.exhausted=true;}return false;}
    ID3D11Texture2D* textures[]={color,depth,mv,mask};unsigned bpp[]={4,4,4,1};
    for(unsigned i=0;i<4;++i){
        if(!textures[i])throw std::runtime_error("Missing sequence destination texture");
        D3D11_TEXTURE2D_DESC t{};textures[i]->GetDesc(&t);
        const bool format=i==0?(t.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||t.Format==DXGI_FORMAT_R8G8B8A8_UNORM||t.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB):i==1?t.Format==DXGI_FORMAT_R32_FLOAT:i==2?t.Format==DXGI_FORMAT_R16G16_FLOAT:t.Format==DXGI_FORMAT_R8_UNORM;
        if(!format||t.Width!=d.width||t.Height!=d.height||t.ArraySize!=1||t.MipLevels!=1||t.SampleDesc.Count!=1)throw std::runtime_error("Sequence texture contract mismatch");
    }
    const auto& f=d.frames[d.index];reset=d.index==0||(f.flags&1)!=0;
    for(unsigned i=0;i<4;++i)ctx->UpdateSubresource(textures[i],0,nullptr,f.planes[i],d.width*bpp[i],0);
    return true;
}
inline void Delivered(){auto& d=Get();if(d.loaded&&d.index<d.count){Log("[sequence-replay] input index=%u source_frame=%llu delivered",d.index,d.frames[d.index].sourceFrame);++d.index;}}
inline std::string Metadata(){auto& d=Get();if(!d.loaded||d.index>=d.count)return "null";const auto& f=d.frames[d.index];std::ostringstream j;j<<"{\"index\":"<<d.index<<",\"source_frame\":"<<f.sourceFrame<<",\"source_callback\":"<<f.callback<<",\"source_qpc\":"<<f.qpc<<",\"qpc_frequency\":"<<f.frequency<<",\"recorded_reset\":"<<((f.flags&1)?"true":"false")<<",\"initial_history_saved\":false}";return j.str();}
}
