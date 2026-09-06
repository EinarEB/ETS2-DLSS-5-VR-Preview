// CPU-only: no Windows key reads, renderer, model or graphics device.
#include "../src/feeder/feed_preview_blend.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <thread>
#include <atomic>

static unsigned checks=0;
static void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
int main(){try{
    ets2_preview::BlendControl control;
    auto initial=control.Latch();
    check(initial.value==1,"default full blend");
    check(!control.Key(true,145,true,1),"startup held must not trigger");
    check(!control.Key(true,145,false,1),"release arms without toggling");
    check(control.Key(true,145,true,1),"first eligible press");
    auto hidden=control.Latch();check(hidden.value==0,"full becomes hidden");
    check(!control.Key(true,145,true,1),"held key never repeats");
    control.Configure(.65f);check(control.Latch().value==0,"config reload cannot undo comparison");
    check(control.Status().deliveredFrame==0,"selection is not delivery");
    control.Delivered(initial,2,100);
    auto old=control.Status();check(old.selected.serial!=old.delivered.serial,"old frame remains distinct from new selection");
    check(old.delivered.value==1&&old.selected.value==0,"old delivery uses its actual blend");
    control.Delivered(hidden,3,110);check(control.Status().delivered.serial==hidden.serial,"matching frame acknowledgment");
    control.Key(true,145,false,1);control.Key(true,145,true,1);
    check(control.Latch().value==1,"returns to100 rather than saved partial blend");
    check(!control.Key(false,145,false,1),"focus loss");
    check(!control.Key(true,145,true,1),"focus return while held");
    control.Key(true,145,false,1);check(control.Key(true,145,true,1),"release after refocus enables next press");
    control.Key(true,145,false,1);check(!control.Key(true,122,true,1),"binding changed while held");
    control.Key(true,122,false,1);check(!control.Key(true,122,true,2),"runtime changed while held");
    control.Key(true,122,false,2);control.Disarm();check(!control.Key(true,122,true,2),"destroy/rebind disarms");
    control.Key(true,122,false,2);check(!control.Key(true,0,true,2),"disabled key cannot act");
    control.Manual(.4f);check(control.Latch().value==.4f&&!control.Status().temporary,"slider clears session override");
    control.Key(true,145,false,2);control.Key(true,145,true,2);check(control.Latch().value==0,"partial blend toggles to0");
    control.Manual(2);check(control.Latch().value==0,"invalid slider ignored");
    control.Configure(std::numeric_limits<float>::quiet_NaN());check(control.Latch().value==0,"NaN ignored");
    control.Manual(.75f);auto frame=control.Latch();control.Manual(.25f);control.Delivered(frame,4,120);
    check(control.Status().delivered.value==.75f,"mid-frame UI edit cannot alter latched delivery");
    control.ResetRuntime();check(control.Status().deliveredFrame==0,"runtime recreation invalidates delivery");
    control.Delivered(frame,100,150);check(control.Status().deliveredFrame==0,"stale runtime cannot acknowledge a new runtime");
    check(control.Latch().value==.25f,"runtime recreation preserves selected blend");
    // Exercise the actual mutex-protected API with concurrent UI/status/render callers.
    std::atomic<bool> valid=true;
    std::thread ui([&]{for(int i=0;i<20000;i++)control.Manual(float(i%101)/100);});
    std::thread render([&]{for(int i=0;i<20000;i++){auto v=control.Latch();control.Delivered(v,i+5,i+130);if(v.value<0||v.value>1)valid=false;}});
    for(int i=0;i<20000;i++){auto s=control.Status();if(s.selected.value<0||s.selected.value>1||s.delivered.value<0||s.delivered.value>1)valid=false;}
    ui.join();render.join();check(valid,"concurrent state snapshots stay valid");
    printf("{\"passed\":%u,\"gpu_used\":false,\"game_launched\":false}\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAILED: %s\n",e.what());return 1;}}
