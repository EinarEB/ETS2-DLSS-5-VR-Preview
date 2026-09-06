// The completed desktop image must be presented with a desktop-sized raster area.
// Snowymoon is downstream of ReShade. Preserve caller state around that call;
// never cast an OpenXR runtime to IDXGISwapChain and never alter eye textures.
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <reshade.hpp>
#include <MinHook.h>
#include <mutex>
#include <vector>
#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>
using Microsoft::WRL::ComPtr;
extern "C" __declspec(dllexport) const char* NAME="ETS2 monitor presentation";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Preserves desktop proportions around downstream presentation; headset textures are untouched.";
static HMODULE self;
static std::mutex mutex;
static std::filesystem::path logPath;
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
static PresentFn originalPresent=nullptr;
static Present1Fn originalPresent1=nullptr;
static void *presentTarget=nullptr,*present1Target=nullptr;
struct Chain { reshade::api::effect_runtime* runtime; IUnknown* identity; ComPtr<ID3D11Device> device; unsigned samples=0; };
// Destroy(runtime) releases normal ownership. Avoid automatic COM destruction
// under the loader lock when Windows is terminating the whole process.
static auto& chains=*new std::vector<Chain>;
static void Log(const std::string& s){std::ofstream(logPath,std::ios::app)<<s<<'\n';}
class RasterScope {
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Multithread> multithread;
    D3D11_VIEWPORT oldViews[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    D3D11_RECT oldRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewCount=16,rectCount=16;
    bool entered=false,changed=false;
    std::string diagnostic;
public:
    RasterScope(IDXGISwapChain* chain){
        ComPtr<ID3D11Device> device;bool sample=false;
        ComPtr<IUnknown> identity;if(FAILED(chain->QueryInterface(IID_PPV_ARGS(&identity))))return;
        {std::lock_guard<std::mutex> hold(mutex);
            auto found=std::find_if(chains.begin(),chains.end(),[&](const Chain& c){return c.identity==identity.Get();});
            if(found==chains.end())return;
            device=found->device;sample=(++found->samples<=3||found->samples%600==0);
        }
        device->GetImmediateContext(&context);
        if(!context||FAILED(context.As(&multithread))||!multithread->GetMultithreadProtected())return;
        ComPtr<ID3D11Texture2D> buffer;
        if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))))return;
        D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);
        if(!desc.Width||!desc.Height)return;
        DXGI_SWAP_CHAIN_DESC swap{};if(FAILED(chain->GetDesc(&swap)))return;
        // Present can wait for the window thread. Never hold its device lock
        // when that is another thread: that thread may need the same lock.
        const DWORD windowThread=GetWindowThreadProcessId(swap.OutputWindow,nullptr);
        if(!windowThread||windowThread!=GetCurrentThreadId()){
            if(sample){std::lock_guard<std::mutex> hold(mutex);Log("Monitor correction skipped: Present does not own the desktop window thread; headset unchanged");}
            return;
        }
        ComPtr<IDXGISwapChain2> sc2;UINT sw=0,sh=0;DXGI_MATRIX_3X2_F m{};
        HRESULT source=E_NOINTERFACE,matrix=E_NOINTERFACE;
        if(sample){
            if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&sc2)))){source=sc2->GetSourceSize(&sw,&sh);matrix=sc2->GetMatrixTransform(&m);}
        }
        multithread->Enter();entered=true;
        context->RSGetViewports(&viewCount,oldViews);context->RSGetScissorRects(&rectCount,oldRects);
        if(sample){
            char text[640]{};sprintf_s(text,"desktop=%ux%u windowed=%u viewports=%u first=%.1f,%.1f,%.1f,%.1f scissor=%u first=%ld,%ld,%ld,%ld source_hr=%08x source=%ux%u matrix_hr=%08x matrix=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",desc.Width,desc.Height,swap.Windowed,viewCount,oldViews[0].TopLeftX,oldViews[0].TopLeftY,oldViews[0].Width,oldViews[0].Height,rectCount,oldRects[0].left,oldRects[0].top,oldRects[0].right,oldRects[0].bottom,unsigned(source),sw,sh,unsigned(matrix),m._11,m._12,m._21,m._22,m._31,m._32);
            diagnostic=text;
        }
        D3D11_VIEWPORT full{0,0,float(desc.Width),float(desc.Height),0,1};
        D3D11_RECT rect{0,0,LONG(desc.Width),LONG(desc.Height)};
        context->RSSetViewports(1,&full);context->RSSetScissorRects(1,&rect);changed=true;
    }
    ~RasterScope(){
        if(changed){context->RSSetViewports(viewCount,oldViews);context->RSSetScissorRects(rectCount,oldRects);}
        if(entered)multithread->Leave();
        if(!diagnostic.empty()){std::lock_guard<std::mutex> hold(mutex);Log(diagnostic);}
    }
};
static HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain,UINT sync,UINT flags){
    if(flags&DXGI_PRESENT_TEST)return originalPresent(chain,sync,flags);
    RasterScope guard(chain);return originalPresent(chain,sync,flags);
}
static HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* chain,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters){
    if(flags&DXGI_PRESENT_TEST)return originalPresent1(chain,sync,flags,parameters);
    RasterScope guard(chain);return originalPresent1(chain,sync,flags,parameters);
}
static void Init(reshade::api::effect_runtime* rt){
    if(!rt->get_hwnd()||rt->get_device()->get_api()!=reshade::api::device_api::d3d11)return;
    wchar_t windowClass[64]{};GetClassNameW(HWND(rt->get_hwnd()),windowClass,64);
    if(wcscmp(windowClass,L"prism3d")!=0)return;
    auto* native=reinterpret_cast<IDXGISwapChain*>(rt->get_native());
    ComPtr<ID3D11Device> device;if(!native||FAILED(native->GetDevice(IID_PPV_ARGS(&device))))return;
    ComPtr<IUnknown> identity;if(FAILED(native->QueryInterface(IID_PPV_ARGS(&identity))))return;
    std::lock_guard<std::mutex> hold(mutex);
    if(std::any_of(chains.begin(),chains.end(),[&](const Chain& c){return c.runtime==rt;}))return;
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){Log("MinHook initialization failed; monitor unchanged");return;}
    HMODULE pinned{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Init),&pinned)){Log("Module pin failed; monitor unchanged");return;}
    auto** table=*reinterpret_cast<void***>(native);
    if(!presentTarget){
        const auto status=MH_CreateHook(table[8],&Present,reinterpret_cast<void**>(&originalPresent));
        if(status!=MH_OK){Log("Present hook unavailable: "+std::to_string(status));return;}
        if(MH_EnableHook(table[8])!=MH_OK){MH_RemoveHook(table[8]);originalPresent=nullptr;Log("Present enable failed");return;}
        presentTarget=table[8];
    }else if(presentTarget!=table[8]){Log("Different desktop Present target rejected");return;}
    ComPtr<IDXGISwapChain1> sc1;
    if(SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&sc1)))){
        auto** v1=*reinterpret_cast<void***>(sc1.Get());
        if(!present1Target){
            if(MH_CreateHook(v1[22],&Present1,reinterpret_cast<void**>(&originalPresent1))!=MH_OK){Log("Present1 hook unavailable");return;}
            if(MH_EnableHook(v1[22])!=MH_OK){MH_RemoveHook(v1[22]);originalPresent1=nullptr;Log("Present1 enable failed");return;}
            present1Target=v1[22];
        }else if(present1Target!=v1[22]){Log("Different desktop Present1 target rejected");return;}
    }
    chains.push_back({rt,identity.Get(),device,0});
    // The hook can be active inside a downstream Present during shutdown.
    // Keep the tiny module mapped until process teardown.
    Log("Registered desktop presentation guard");
}
static void Destroy(reshade::api::effect_runtime* rt){
    std::lock_guard<std::mutex> hold(mutex);
    chains.erase(std::remove_if(chains.begin(),chains.end(),[&](const Chain& c){return c.runtime==rt;}),chains.end());
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID reserved){
    if(reason==DLL_PROCESS_DETACH&&reserved!=nullptr)return TRUE;
    if(reason==DLL_PROCESS_ATTACH){
        self=module;wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);logPath=std::filesystem::path(path).parent_path()/"ets2-monitor.log";
        if(!reshade::register_addon(module))return FALSE;
        reshade::register_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
    }else if(reason==DLL_PROCESS_DETACH){
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
        reshade::unregister_addon(module);
    }return TRUE;
}
