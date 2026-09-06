#pragma once
// Replace only the SR computation nested inside this adapter's synchronous,
// equal-size eye evaluation. The classic consumer then performs NR normally.
using CarrierEval = NVSDK_NGX_Result(__cdecl *)(ID3D12GraphicsCommandList*, const void*, const void*, void*);
static CarrierEval carrierOriginal = nullptr;
static void* carrierTarget = nullptr;
static HMODULE carrierModule = nullptr;
static bool carrierEnabled = false;
static thread_local ID3D12GraphicsCommandList* carrierList = nullptr;
static thread_local ID3D12Resource* carrierInput = nullptr;
static thread_local ID3D12Resource* carrierOutput = nullptr;
static unsigned long long carrierCopies = 0;
static NVSDK_NGX_Result __cdecl CarrierCopy(ID3D12GraphicsCommandList* list,const void* handle,const void* params,void* progress)
{
    if(list==carrierList && carrierInput && carrierOutput){
        Barrier(carrierInput,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(carrierOutput,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(carrierOutput,carrierInput);
        Barrier(carrierInput,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(carrierOutput,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ++carrierCopies;
        return NVSDK_NGX_Result_Success;
    }
    return carrierOriginal(list,handle,params,progress);
}
static bool CarrierInstall()
{
    if(carrierTarget){
        if(carrierEnabled)return true;
        const auto status=MH_EnableHook(carrierTarget);
        carrierEnabled=status==MH_OK || status==MH_ERROR_ENABLED;
        return carrierEnabled;
    }
    HMODULE module=GetModuleHandleW(L"nvngx_dlss.dll");
    auto* target=module?reinterpret_cast<void*>(GetProcAddress(module,"NVSDK_NGX_D3D12_EvaluateFeature")):nullptr;
    if(!target){Log("[carrier-copy] SR snippet export unavailable");return false;}
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(target),&carrierModule))return false;
    // If detour removal ever fails, its callback/trampoline must remain callable
    // even after ReShade unloads add-ons. This retains code, not GPU resources.
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&CarrierCopy),&self)){
        FreeLibrary(carrierModule);carrierModule=nullptr;return false;
    }
    MH_STATUS status=MH_Initialize();
    if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED){FreeLibrary(carrierModule);carrierModule=nullptr;return false;}
    status=MH_CreateHook(target,reinterpret_cast<void*>(&CarrierCopy),reinterpret_cast<void**>(&carrierOriginal));
    if(status!=MH_OK){
        // Never remove a hook that this call did not create.
        FreeLibrary(carrierModule);carrierModule=nullptr;carrierOriginal=nullptr;
        Log("[carrier-copy] create failed: %s",MH_StatusToString(status));return false;
    }
    carrierTarget=target;
    status=MH_EnableHook(target);
    carrierEnabled=status==MH_OK || status==MH_ERROR_ENABLED;
    if(!carrierEnabled){
        Log("[carrier-copy] enable failed: %s; owned detour and both code modules retained",MH_StatusToString(status));return false;
    }
    wchar_t path[MAX_PATH]={};GetModuleFileNameW(module,path,MAX_PATH);
    Log("[carrier-copy] scoped SR copy installed at %p in %ls; scope is owned list + thread + eye resources",target,path);
    return true;
}
static void CarrierRemove()
{
    carrierList=nullptr;carrierInput=nullptr;carrierOutput=nullptr;
    if(!carrierTarget)return;
    // An unrelated NGX caller can already be executing this detour. Retain the
    // forwarding trampoline and both code images until process exit instead of
    // racing that caller during graphics-session teardown. TLS is disarmed and
    // no game/GPU resource is retained here. This add-on requires a full process
    // restart for binary reloads; a session may rebuild with fresh eye resources.
    Log("[carrier-copy] owned inputs released after %llu eye copies; forwarding hook retained until process exit",carrierCopies);
}
