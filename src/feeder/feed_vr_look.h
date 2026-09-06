#pragma once

// The desktop menu queues a bounded choice. Only the successful VR presentation
// thread changes its own ReShade preset; the desktop runtime remains separate.
static const wchar_t* kVrLookFiles[]={L"ETS2_VR_Preview_Clean.ini",L"ETS2_VR_Preview_Cooler.ini",L"ETS2_VR_Preview_ColdGrade.ini"};
static const char* kVrLookLabels[]={"No extra grading","Cooler color","Cold grade (contrast + color)"};
static volatile LONG g_vr_look_pending=-1,g_vr_look_active=-1,g_vr_look_status=0;
static volatile LONG g_vr_image_request=0,g_vr_image_status=0;
static unsigned g_vr_image_wait=0;
static SRWLOCK g_vr_image_lock=SRWLOCK_INIT;
static char g_vr_image_saved[2048]{};
static char g_vr_image_expected[96]{};
static volatile LONG64 g_vr_image_started=0;
static unsigned g_vr_image_serial=0;
static void RefreshVrImageTimeout(){
    if(InterlockedCompareExchange(&g_vr_image_status,0,0)==2&&GetTickCount64()-ULONGLONG(InterlockedCompareExchange64(&g_vr_image_started,0,0))>20000)
        InterlockedCompareExchange(&g_vr_image_status,4,2);
}

extern "C" __declspec(dllexport) BOOL ETS2_RequestVRFinalImage(){
    AcquireSRWLockExclusive(&g_vr_image_lock);
    const LONG status=InterlockedCompareExchange(&g_vr_image_status,0,0);
    if(status==1||status==2){ReleaseSRWLockExclusive(&g_vr_image_lock);return FALSE;}
    g_vr_image_expected[0]=0;
    InterlockedExchange(&g_vr_image_status,1);InterlockedExchange(&g_vr_image_request,1);
    ReleaseSRWLockExclusive(&g_vr_image_lock);return TRUE;
}
static void SavePendingVrImage(reshade::api::effect_runtime* rt,bool delivered,bool lookChanged){
    if(InterlockedExchange(&g_vr_image_request,0))g_vr_image_wait=30;
    RefreshVrImageTimeout();
    if(!g_vr_image_wait)return;
    if(!delivered||lookChanged){g_vr_image_wait=30;return;}
    if(--g_vr_image_wait)return;
    char postfix[96]{};const auto started=GetTickCount64();InterlockedExchange64(&g_vr_image_started,LONG64(started));sprintf_s(postfix,"ETS2-VR-Final-%llu-%u",started,++g_vr_image_serial);
    AcquireSRWLockExclusive(&g_vr_image_lock);strcpy_s(g_vr_image_expected,postfix);
    InterlockedExchange(&g_vr_image_status,2);
    ReleaseSRWLockExclusive(&g_vr_image_lock);
    // reshade_present is after all effects; this is the combined eye image
    // before the headset compositor, including optional post-neural grading.
    rt->save_screenshot(postfix);
    Log("[VR image] final stereo screenshot requested: %s",postfix);
}
static void OnFinalVrImageSaved(reshade::api::effect_runtime*,const char* path){
    if(!path||!strstr(path,"ETS2-VR-Final-"))return;
    AcquireSRWLockExclusive(&g_vr_image_lock);
    const char* filename=path;for(const char* p=path;*p;++p)if(*p=='\\'||*p=='/')filename=p+1;
    const char* tag=g_vr_image_expected[0]?strstr(filename,g_vr_image_expected):nullptr;
    const char tail=tag?tag[strlen(g_vr_image_expected)]:0;
    const bool current=tag&&(tail==0||tail=='.'||tail==' '||tail=='_'||tail=='-');
    if(current){strncpy_s(g_vr_image_saved,path,_TRUNCATE);InterlockedExchange(&g_vr_image_status,3);}
    ReleaseSRWLockExclusive(&g_vr_image_lock);
    Log("[VR image] final stereo screenshot saved: %s",path);
}
static void DrawFinalVrImage(){
    RefreshVrImageTimeout();
    const LONG before=InterlockedCompareExchange(&g_vr_image_status,0,0);
    ImGui::BeginDisabled(before==1||before==2);
    if(ImGui::Button("Save final VR image"))ETS2_RequestVRFinalImage();
    ImGui::EndDisabled();
    ImGui::TextWrapped("Saves both eyes after all ReShade effects, including the color look. Waits for 30 rendered VR frames; saving can briefly slow rendering.");
    const LONG status=InterlockedCompareExchange(&g_vr_image_status,0,0);
    if(status==1)ImGui::TextUnformatted("Waiting for 30 uninterrupted VR frames.");
    if(status==2)ImGui::TextUnformatted("Image requested; ReShade saves it in the background.");
    if(status==3){char path[2048]{};AcquireSRWLockShared(&g_vr_image_lock);strcpy_s(path,g_vr_image_saved);ReleaseSRWLockShared(&g_vr_image_lock);ImGui::TextWrapped("Saved: %s",path);}
    if(status==4)ImGui::TextWrapped("No saved file confirmed after 20 seconds. Check ReShade's screenshot folder and free disk space, then retry if needed.");
}

static std::filesystem::path VrLookPath(unsigned choice){
    if(choice>=3)return {};
    wchar_t module[MAX_PATH]{};const DWORD n=GetModuleFileNameW(g_self,module,MAX_PATH);
    if(!n||n>=MAX_PATH)return {};
    return std::filesystem::path(module).parent_path()/L"Reshade presets"/kVrLookFiles[choice];
}
extern "C" __declspec(dllexport) BOOL ETS2_RequestVRColorLook(unsigned choice){
    if(choice>=3)return FALSE;
    const auto path=VrLookPath(choice);std::error_code error;
    if(path.empty()||!std::filesystem::is_regular_file(path,error)){InterlockedExchange(&g_vr_look_status,3);return FALSE;}
    InterlockedExchange(&g_vr_look_pending,LONG(choice));InterlockedExchange(&g_vr_look_status,1);return TRUE;
}
static void RefreshVrLook(reshade::api::effect_runtime* rt){
    char current[2048]{};rt->get_current_preset_path(current);
    const auto file=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(current))).filename().wstring();LONG choice=-1;
    for(unsigned i=0;i<3;++i)if(_wcsicmp(file.c_str(),kVrLookFiles[i])==0)choice=LONG(i);
    InterlockedExchange(&g_vr_look_active,choice);
}
static void ApplyPendingVrLook(reshade::api::effect_runtime* rt,LONG choice){
    if(choice<0){static unsigned poll=0;if(++poll%30==1)RefreshVrLook(rt);return;}
    const auto path=VrLookPath(unsigned(choice));std::error_code error;
    if(path.empty()||!std::filesystem::is_regular_file(path,error)){InterlockedExchange(&g_vr_look_status,3);return;}
    const auto utf8=path.u8string();const std::string name(reinterpret_cast<const char*>(utf8.data()),utf8.size());
    rt->set_current_preset_path(name.c_str());RefreshVrLook(rt);
    const bool selected=InterlockedCompareExchange(&g_vr_look_active,0,0)==choice;
    InterlockedExchange(&g_vr_look_status,selected?2:4);
    Log("[VR look] requested %s: preset selection %s; shaders may still be loading",kVrLookLabels[choice],selected?"accepted":"not accepted");
}
static void DrawVrLook(){
    const LONG current=InterlockedCompareExchange(&g_vr_look_active,0,0);
    if(ImGui::BeginCombo("VR color look",current>=0&&current<3?kVrLookLabels[current]:"Current custom preset")){
        for(unsigned i=0;i<3;++i)if(ImGui::Selectable(kVrLookLabels[i],current==LONG(i)))ETS2_RequestVRColorLook(i);
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("This selector changes the headset look, even from the monitor menu. The Home tab on the monitor selects desktop shaders instead. Both cool looks run after neural rendering; bloom and animated dithering stay off.");
    const LONG status=InterlockedCompareExchange(&g_vr_look_status,0,0);
    if(status==1)ImGui::TextWrapped("Queued for the next rendered VR frame.");
    if(status==3)ImGui::TextWrapped("The selected VR preview color preset is missing from Reshade presets.");
    if(status==4)ImGui::TextWrapped("ReShade did not accept that preset. The current look is retained.");
}
