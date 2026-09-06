#pragma once
#include <array>
#include <cstdint>
#include <cmath>

// Pure policy: no driver pointers, globals, file reads or graphics calls. A
// successful Evaluate only records commands. Commit happens after delivery.
namespace ets2_nr_control {
struct Controls {
    std::array<float,6> values{}; // intensity, tone, structure, skin, MV X/Y
    std::array<unsigned,6> modes{}; // style, preset, enabled, auto-mask, UI, inverted
    bool operator==(const Controls& b)const{return values==b.values&&modes==b.modes;}
    bool valid()const{for(auto v:values)if(!std::isfinite(v))return false;return true;}
};
struct Options {
    bool enabled=true;
    float secondTone=1.0f;
    float secondStructure=1.0f;
    bool operator==(const Options& b)const{return enabled==b.enabled&&secondTone==b.secondTone&&secondStructure==b.secondStructure;}
};
struct Decision {bool valid=false,reset=false;float tone=0,structure=0;};
class Coordinator {
    Controls committed{},current{};
    Options committedOptions{},options{};
    std::array<uint64_t,4> committedCreation{},creation{};
    bool committedValid=false,active=false,pending=true,consistent=true,commonReset=false;
    unsigned expected=0,seen=0;
    uint64_t attempt=0,commits=0;
public:
    void Begin(unsigned count,bool requestedReset,Options next){
        ++attempt;active=true;expected=count;seen=0;creation={};options=next;
        consistent=(count==2||count==4)&&std::isfinite(next.secondTone)&&next.secondTone>=0&&next.secondTone<=1&&std::isfinite(next.secondStructure)&&next.secondStructure>=0&&next.secondStructure<=1;
        commonReset=requestedReset||pending||!committedValid||!(next==committedOptions);
    }
    Decision Observe(unsigned slot,const Controls& c,uint64_t identity,bool incomingReset){
        Decision d;d.tone=c.values[1];d.structure=c.values[2];d.reset=incomingReset;
        if(!active||slot>=expected||seen!=((1u<<slot)-1)||!identity||!c.valid()){
            consistent=false;pending=true;return d;
        }
        if(slot==0){
            current=c;
            commonReset=commonReset||!(c==committed)||identity!=committedCreation[0]||incomingReset;
        }else if(!(c==current)||(!commonReset&&(identity!=committedCreation[slot]||incomingReset))){
            // Earlier contexts cannot be retroactively reset. This attempt must
            // not be shown; the next complete attempt resets every active slot.
            consistent=false;pending=true;
        }
        creation[slot]=identity;seen|=1u<<slot;d.valid=true;
        if(options.enabled){d.reset=incomingReset||commonReset;if(slot>=2){d.tone*=options.secondTone;d.structure*=options.secondStructure;}}
        return d;
    }
    bool CanDeliver()const{return !active||!options.enabled||(consistent&&seen==((1u<<expected)-1));}
    void Finish(bool delivered){
        if(!active)return;
        if(delivered&&consistent&&seen==((1u<<expected)-1)){
            committed=current;committedOptions=options;committedCreation=creation;
            committedValid=true;pending=false;++commits;
        }else pending=true;
        active=false;
    }
    void Invalidate(){committedValid=false;pending=true;}
    void RejectAttempt(){consistent=false;pending=true;}
    bool Active()const{return active;}
    bool Enabled()const{return options.enabled;}
    bool CommonReset()const{return commonReset;}
    uint64_t Attempt()const{return attempt;}
    uint64_t Commits()const{return commits;}
    unsigned Seen()const{return seen;}
    const Controls& Current()const{return current;}
    const std::array<uint64_t,4>& Creation()const{return creation;}
};
}
