#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <map>
#include <fstream>
#include <filesystem>
#include <string>
#include <cstdint>

// Optional bounded diagnostic. No query-induced wait, flush, spin or clock
// calibration. D3D12 list time is nested in handoff time, never added to it.
namespace ets2_profile {
using Microsoft::WRL::ComPtr;
enum Marker:unsigned {InputStart,InputDone,ReturnReady,FilterStart,FilterDone,OutputDone,MarkerCount};
struct Slot {
    ComPtr<ID3D11Query> disjoint;
    std::array<ComPtr<ID3D11Query>,MarkerCount> points;
    std::array<uint64_t,MarkerCount> ticks{};
    uint64_t id=0,frame=0,generation=0,frequency=0;
    unsigned mask=0,steady=0;
    bool pending=false,ready=false,valid=false,success=false,reset=false;
    std::string configuration;
    int64_t cpuStart=0,cpuEnd=0;
};
class Profiler {
    ComPtr<ID3D11Device> device;
    std::array<Slot,8> slots;
    std::map<uint64_t,double> neural;
    std::ofstream output;
    int active=-1;
    unsigned rows=0,drops=0,steady=0;
    uint64_t previousId=0,generation=0;
    std::string previousConfiguration;
    bool configured=false,enabled=false,failed=false;
    LARGE_INTEGER cpuFrequency{};
    bool Ready(Slot& s,ID3D11DeviceContext* c){
        if(s.ready)return true;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT q{};
        HRESULT hr=c->GetData(s.disjoint.Get(),&q,sizeof(q),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr==S_FALSE)return false;
        if(FAILED(hr)||q.Disjoint||!q.Frequency){s.ready=true;s.valid=false;return true;}
        for(unsigned i=0;i<MarkerCount;++i)if(s.mask&(1u<<i)){
            hr=c->GetData(s.points[i].Get(),&s.ticks[i],sizeof(uint64_t),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(hr==S_FALSE)return false;
            if(FAILED(hr)){s.ready=true;s.valid=false;return true;}
        }
        s.frequency=q.Frequency;s.ready=true;
        const unsigned required=(1u<<InputStart)|(1u<<InputDone)|(1u<<ReturnReady)|(1u<<OutputDone);
        s.valid=(s.mask&required)==required&&s.ticks[InputStart]<=s.ticks[InputDone]&&s.ticks[InputDone]<=s.ticks[ReturnReady]&&s.ticks[ReturnReady]<=s.ticks[OutputDone];
        if(s.mask&(1u<<FilterStart))s.valid=s.valid&&(s.mask&(1u<<FilterDone))&&s.ticks[ReturnReady]<=s.ticks[FilterStart]&&s.ticks[FilterStart]<=s.ticks[FilterDone]&&s.ticks[FilterDone]<=s.ticks[OutputDone];
        return true;
    }
    static double Span(const Slot& s,Marker a,Marker b){return double(s.ticks[b]-s.ticks[a])*1000.0/double(s.frequency);}
    void Emit(Slot& s){
        auto nr=neural.find(s.id);
        output<<"{\"epoch\":"<<s.id<<",\"frame\":"<<s.frame<<",\"generation\":"<<s.generation<<",\"successful_delivery\":"<<(s.success?"true":"false")<<",\"common_reset\":"<<(s.reset?"true":"false")<<",\"consecutive_stable_deliveries\":"<<s.steady<<",\"timestamps_valid\":"<<(s.valid?"true":"false")<<",\"dropped_measurements_total\":"<<drops<<",\"configuration\":"<<s.configuration;
        output<<",\"cpu_stage_wall_ms\":"<<double(s.cpuEnd-s.cpuStart)*1000.0/double(cpuFrequency.QuadPart);
        if(s.valid){output<<",\"input_gpu_ms\":"<<Span(s,InputStart,InputDone)<<",\"handoff_elapsed_ms\":"<<Span(s,InputDone,ReturnReady)<<",\"output_gpu_ms\":"<<Span(s,ReturnReady,OutputDone)<<",\"feeder_path_elapsed_ms\":"<<Span(s,InputStart,OutputDone);if(s.mask&(1u<<FilterStart))output<<",\"filter_gpu_ms\":"<<Span(s,FilterStart,FilterDone);else output<<",\"filter_gpu_ms\":null";}
        output<<",\"neural_list_gpu_ms\":";if(nr!=neural.end()){output<<nr->second;neural.erase(nr);}else output<<"null";
        output<<"}\n";if(++rows%60==0)output.flush();s.pending=false;
    }
public:
    bool Enabled(){if(!configured){configured=true;char value[8]{};enabled=GetEnvironmentVariableA("ETS2_FEED_PROFILE",value,sizeof(value))&&value[0]=='1';QueryPerformanceFrequency(&cpuFrequency);}return enabled&&!failed;}
    uint64_t ActiveId()const{return active>=0?slots[active].id:0;}
    void NeuralTime(uint64_t id,double ms){if(id&&Enabled()&&neural.size()<32)neural[id]=ms;}
    void Poll(ID3D11DeviceContext* c){
        if(!Enabled())return;
        for(auto& s:slots)if(s.pending&&Ready(s,c)){
            if(!s.success||!s.valid||neural.count(s.id))Emit(s);
        }
    }
    void Release(){
        if(output.is_open()){output.flush();output.close();}
        slots={};neural.clear();device.Reset();active=-1;previousId=0;steady=0;previousConfiguration.clear();++generation;
    }
    void Begin(ID3D11DeviceContext* c,uint64_t id,uint64_t frame,const char* logPath){
        if(!Enabled())return;
        if(active>=0)Finish(c,false,true,"{}");
        ComPtr<ID3D11Device> d;c->GetDevice(&d);
        if(device.Get()!=d.Get()){
            Release();device=d;
            for(auto& s:slots){D3D11_QUERY_DESC q{};q.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;if(FAILED(d->CreateQuery(&q,&s.disjoint))){failed=true;return;}q.Query=D3D11_QUERY_TIMESTAMP;for(auto& p:s.points)if(FAILED(d->CreateQuery(&q,&p))){failed=true;return;}}
            output.open(std::filesystem::path(logPath).parent_path()/"feeder-profile.jsonl",std::ios::app);
            if(!output){failed=true;return;}
            output<<"{\"type\":\"profile_header\",\"schema\":1,\"settling_deliveries\":120,\"capacity\":4000,\"semantics\":\"D3D12 list and filter intervals are nested; no query waits or flushes; excludes Kernel, depth route, later effects and XR presentation\"}\n";output.flush();
        }
        Poll(c);if(rows>=4000)return;
        for(unsigned i=0;i<slots.size();++i)if(!slots[i].pending){active=int(i);break;}
        if(active<0){++drops;return;}
        auto& s=slots[active];s.id=id;s.frame=frame;s.mask=0;s.pending=false;s.ready=s.valid=s.success=s.reset=false;s.configuration="{}";
        if(previousId+1!=id){steady=0;++generation;}
        s.generation=generation;c->Begin(s.disjoint.Get());Mark(c,InputStart);LARGE_INTEGER now;QueryPerformanceCounter(&now);s.cpuStart=now.QuadPart;
    }
    void Mark(ID3D11DeviceContext* c,Marker marker){
        if(active<0)return;
        auto& s=slots[active];if(s.mask&(1u<<marker))return;
        c->End(s.points[marker].Get());s.mask|=1u<<marker;
    }
    void Finish(ID3D11DeviceContext* c,bool success,bool reset,const std::string& configuration){
        if(active<0)return;
        auto& s=slots[active];LARGE_INTEGER now;QueryPerformanceCounter(&now);s.cpuEnd=now.QuadPart;
        c->End(s.disjoint.Get());s.pending=true;s.success=success;s.reset=reset;s.configuration=configuration;
        if(!success||reset||configuration!=previousConfiguration){steady=0;++generation;}
        if(success&&!reset)++steady;
        s.steady=steady;s.generation=generation;previousId=s.id;previousConfiguration=configuration;active=-1;
    }
};
}
