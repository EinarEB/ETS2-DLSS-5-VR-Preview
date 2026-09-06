#pragma once
// Included ONLY by the offline fixture build. Never present in the installed DLL.
#include <fstream>
#include <array>
#include <stdexcept>
namespace ets2_packet_replay {
struct Data {unsigned width=0,height=0;std::array<std::vector<unsigned char>,4> planes;bool loaded=false;};
inline Data& Get(){static auto* data=new Data;return *data;}
inline void Apply(ID3D11DeviceContext* ctx,ID3D11Texture2D* color,ID3D11Texture2D* depth,ID3D11Texture2D* mv,ID3D11Texture2D* mask){
    auto& d=Get();if(!d.loaded){
        wchar_t path[32768]={};if(!GetEnvironmentVariableW(L"ETS2_REPLAY_PACKET",path,32768))throw std::runtime_error("Offline fixture needs a verified packet path");
        std::ifstream f(std::filesystem::path(path),std::ios::binary);char magic[8]={};unsigned header[3]={};f.read(magic,8);f.read(reinterpret_cast<char*>(header),12);
        if(!f||memcmp(magic,"E2CAP01",8)||!header[0]||header[0]%2||!header[1]||header[2]!=1||uint64_t(header[0])*header[1]>32ull*1024*1024)throw std::runtime_error("Invalid replay packet header");
        d.width=header[0];d.height=header[1];unsigned bpp[]={4,4,4,1};
        for(unsigned i=0;i<4;++i){d.planes[i].resize(size_t(d.width)*d.height*bpp[i]);f.read(reinterpret_cast<char*>(d.planes[i].data()),d.planes[i].size());if(!f)throw std::runtime_error("Incomplete replay plane");}
        if(f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Replay packet has trailing data");
        d.loaded=true;Log("[packet-replay] verified input dimensions %ux%u; static frozen input; resets follow observed controls, not original history; not temporal replay",d.width,d.height);
    }
    ID3D11Texture2D* inputs[]={color,depth,mv,mask};unsigned bpp[]={4,4,4,1};
    for(unsigned i=0;i<4;++i){if(!inputs[i])throw std::runtime_error("Missing replay input resource");D3D11_TEXTURE2D_DESC td{};inputs[i]->GetDesc(&td);
        bool format=i==0?(td.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||td.Format==DXGI_FORMAT_R8G8B8A8_UNORM||td.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB):i==1?td.Format==DXGI_FORMAT_R32_FLOAT:i==2?td.Format==DXGI_FORMAT_R16G16_FLOAT:td.Format==DXGI_FORMAT_R8_UNORM;
        if(!format||td.Width!=d.width||td.Height!=d.height||td.ArraySize!=1||td.SampleDesc.Count!=1)throw std::runtime_error("Replay resource contract does not match the captured packet");
    }
    for(unsigned i=0;i<4;++i)ctx->UpdateSubresource(inputs[i],0,nullptr,d.planes[i].data(),d.width*bpp[i],0);
}
}
