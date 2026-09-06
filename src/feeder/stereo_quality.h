// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
namespace ets2_quality {
struct Preset {const char* name;int passes,resolution,crop;};
inline constexpr Preset presets[]={{"Low",1,50,60},{"Medium",1,65,75},{"High",2,80,90},{"Ultra",2,100,0}};
inline int Match(int passes,int resolution,int crop){for(int i=0;i<4;++i)if(presets[i].passes==passes&&presets[i].resolution==resolution&&presets[i].crop==crop)return i;return -1;}
struct Crop {unsigned x=0,y=0,width=0,height=0;};
inline Crop Region(unsigned eyeWidth,unsigned height,int percent){
    Crop c{0,0,eyeWidth,height};
    if(percent==0)return c;
    if(percent<40||percent>100||eyeWidth<64||height<64)return {};
    unsigned side=std::min(eyeWidth,height)*unsigned(percent)/100;
    side=std::max(32u,side&~7u);
    c={ (eyeWidth-side)/2,(height-side)/2,side,side };
    return c;
}
}
