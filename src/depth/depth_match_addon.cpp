// Private exact-color stereo depth experiment. See NOTICE.md for provenance.
// Public callback capture and custom semantic publication; no eye-order inference.
#include <reshade.hpp>
#include "depth_match.hpp"
#include "ets2_depth_route.hpp"
#include <mutex>
#include <memory>
#include <unordered_map>
#include <set>
#include <cstdio>
#include <cstdarg>
using Microsoft::WRL::ComPtr;
using namespace reshade::api;
namespace
{
FILE* logfile = nullptr;
std::wstring outputDirectory;
size_t logBytes = 0;
constexpr size_t LogLimit = 4 * 1024 * 1024;
unsigned previewSets = 0, rawDepthFiles = 0;
#ifdef ETS2_DEPTH_ALLOW_GENERIC_FIXTURE
constexpr bool allowGenericFixture=true;
#else
constexpr bool allowGenericFixture=false;
#endif
thread_local bool inside = false;
std::recursive_mutex routeMutex;
void Log(const char* f, ...)
{
    if (!logfile || logBytes >= LogLimit) return;
    char line[2048] = {};
    va_list args; va_start(args, f);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, f, args); va_end(args);
    const size_t bytes = strlen(line);
    if (logBytes + bytes + 96 > LogLimit)
    {
        fputs("LOG_CAP_REACHED: 4 MiB metadata budget; matching continues with the strict gate.\n", logfile);
        logBytes = LogLimit; fflush(logfile); return;
    }
    logBytes += fwrite(line, 1, bytes, logfile);
    logBytes += fwrite("\n", 1, 1, logfile); fflush(logfile);
}
struct Binding
{
    ComPtr<ID3D11Texture2D> color, depth;
    unsigned colorSub = 0, depthSub = 0, w = 0, h = 0;
    bool valid = false;
};
struct Command
{
    Binding bound;
    uint64_t draws = 0;
};
struct DeviceState
{
    ets2_route::Tracker route;
    std::unique_ptr<depth_match::Matcher> matcher;
    unsigned w = 0, h = 0, expectedW = 0, expectedH = 0;
    DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
    std::string lastReason;
    uint64_t epoch = 0, serial = 0, vrFrames = 0;
    unsigned routeDiagnostics = 0;
    bool inEffects = false, mixedScope = false, previewSaved = false;
    std::unordered_map<command_list*, Command> commands;
    std::set<effect_runtime*> runtimes;
};
// Intentionally no static registry destructor. The OS reclaims this small
// registry at process termination; normal runtime/device callbacks release its
// owned resources earlier. A static map would still destroy COM objects during
// CRT detach even if the DllMain process-termination branch skipped clear().
using StateRegistry = std::unordered_map<device*, DeviceState>;
StateRegistry& states = *new StateRegistry;
struct Guard
{
    std::unique_lock<std::recursive_mutex> lock{routeMutex};
    Guard()
    {
        inside = true;
    }
    ~Guard()
    {
        inside = false;
    }
};
bool Immediate(command_list* cl)
{
    return cl->get_device()->get_api() == device_api::d3d11 &&
           reinterpret_cast<ID3D11DeviceContext*>(cl->get_native())->GetType() ==
               D3D11_DEVICE_CONTEXT_IMMEDIATE;
}
// ReShade reports layers=0 for a non-array texture_2d RTV/DSV. Only array views
// require an explicit one-layer range. Retain the native resource before unbind.
bool TextureFromView(device* d, resource_view v, ComPtr<ID3D11Texture2D>& tex, unsigned& sub, unsigned& w,
                     unsigned& h)
{
    if (!v.handle)
        return false;
    auto r = d->get_resource_from_view(v);
    auto desc = d->get_resource_desc(r);
    auto view = d->get_resource_view_desc(v);
    if (desc.type != resource_type::texture_2d || desc.texture.levels != 1 || desc.texture.samples != 1 ||
        view.texture.first_level != 0 ||
        (view.type != resource_view_type::texture_2d && view.texture.layers != 1))
        return false;
    w = desc.texture.width;
    h = desc.texture.height;
    sub = view.texture.first_layer;
    if (sub >= desc.texture.depth_or_layers)
        return false;
    return SUCCEEDED(reinterpret_cast<ID3D11Resource*>(r.handle)->QueryInterface(IID_PPV_ARGS(&tex)));
}
Binding Describe(device* d, uint32_t n, const resource_view* rt, resource_view dv)
{
    Binding b;
    if (n != 1 || !dv.handle)
        return b;
    unsigned dw = 0, dh = 0;
    if (!TextureFromView(d, rt[0], b.color, b.colorSub, b.w, b.h) ||
        !TextureFromView(d, dv, b.depth, b.depthSub, dw, dh) || dw != b.w || dh != b.h || b.w < 128 ||
        b.h < 128)
        return b;
    D3D11_TEXTURE2D_DESC color{}, depth{};
    b.color->GetDesc(&color);
    b.depth->GetDesc(&depth);
    if ((color.Format != DXGI_FORMAT_R8G8B8A8_UNORM && color.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
         color.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS) ||
        depth_match::DepthStorage(depth.Format) == DXGI_FORMAT_UNKNOWN)
        return b;
    b.valid = true;
    return b;
}
bool Same(const Binding& a, const Binding& b)
{
    return a.color.Get() == b.color.Get() && a.depth.Get() == b.depth.Get() && a.colorSub == b.colorSub &&
           a.depthSub == b.depthSub;
}
void Capture(device* owner, DeviceState& s, command_list* cl, Command& cmd, const char* reason)
try
{
    // Co-bound AA stencil/depth is not proven scene depth. Only a separately
    // compiled synthetic fixture may use the old generic candidate heuristic.
    if(!allowGenericFixture){cmd.draws=0;return;}
    if (!cmd.draws || !cmd.bound.valid)
        return;
    auto& b = cmd.bound;
    if (!s.expectedW || b.w != s.expectedW || b.h != s.expectedH)
    {
        cmd.draws = 0;
        return;
    }
    D3D11_TEXTURE2D_DESC nativeDepth{};
    b.depth->GetDesc(&nativeDepth);
    auto format = depth_match::DepthStorage(nativeDepth.Format);
    if (!s.matcher || (!s.matcher->Count() && (b.w != s.w || b.h != s.h || format != s.depthFormat)))
    {
        s.w = b.w;
        s.h = b.h;
        s.depthFormat = format;
        s.matcher = std::make_unique<depth_match::Matcher>(
            reinterpret_cast<ID3D11Device*>(owner->get_native()),
            reinterpret_cast<ID3D11DeviceContext*>(cl->get_native()), s.w, s.h, format);
    }
    if (b.w != s.w || b.h != s.h || format != s.depthFormat)
    {
        s.mixedScope = true;
        cmd.draws = 0;
        return;
    }
    bool ok = s.matcher->Capture(b.color.Get(), b.colorSub, b.depth.Get(), b.depthSub, s.epoch, ++s.serial);
    if (s.vrFrames < 4 || s.vrFrames % 120 == 0)
        Log("CAPTURE epoch=%llu serial=%llu index=%u reason=%s draws=%llu color=%p color_sub=%u depth=%p "
            "depth_sub=%u depth_family=%u extent=%ux%u captured=%d",
            s.epoch, s.serial, s.matcher->Count(), reason, cmd.draws, b.color.Get(), b.colorSub,
            b.depth.Get(), b.depthSub, unsigned(format), s.w, s.h, ok);
    cmd.draws = 0;
}
catch (const std::exception& e)
{
    if (s.lastReason != e.what())
        Log("CAPTURE exception: %s", e.what());
    s.lastReason = e.what();
    s.mixedScope = true;
    cmd.draws = 0;
}
// D3D11 emits this callback after native OM binding. The tracked old pair still
// owns its COM resources and is copied before the application can clear/reuse it.
void Bind(command_list* cl, uint32_t n, const resource_view* rt, resource_view ds)
{
    if (inside || !Immediate(cl))
        return;
    Guard guard;
    auto* owner = cl->get_device();
    auto& s = states[owner];
    if (s.inEffects)
        return;
    s.route.SetDevice(reinterpret_cast<ID3D11Device*>(owner->get_native()));
    try {s.route.Bind(reinterpret_cast<ID3D11DeviceContext*>(cl->get_native()));}
    catch(const std::exception& e){s.route.Fail();if(s.lastReason!=e.what())Log("ETS2_ROUTE capture: %s",e.what());s.lastReason=e.what();}
    auto& cmd = s.commands[cl];
    auto next = Describe(owner, n, rt, ds);
    if (!Same(cmd.bound, next))
    {
        Capture(owner, s, cl, cmd, "attachment-change");
        cmd.draws = 0;
    }
    cmd.bound = std::move(next);
}
bool Draw(command_list* cl, uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance)
{
    if (inside || !Immediate(cl))
        return false;
    Guard guard;
    auto it = states.find(cl->get_device());
    if (it != states.end() && !it->second.inEffects)
    {
        try {it->second.route.Draw(reinterpret_cast<ID3D11DeviceContext*>(cl->get_native()),vertices,instances,firstVertex,firstInstance);}
        catch(const std::exception& e){it->second.route.Fail();if(it->second.lastReason!=e.what())Log("ETS2_ROUTE draw: %s",e.what());it->second.lastReason=e.what();}
        auto& cmd = it->second.commands[cl];
        if (cmd.bound.valid)
            ++cmd.draws;
    }
    return false;
}
bool Indexed(command_list* cl, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
{
    return Draw(cl, 0, 0, 0, 0);
}
bool ClearDepth(command_list* cl, resource_view view, const float* depth, const uint8_t*, uint32_t,
                const rect*)
{
    // A device-loss path may clear a null DSV. Do not turn the original
    // device failure into a second exception while resolving a missing view.
    if (inside || !view.handle || !depth || !Immediate(cl))
        return false;
    Guard guard;
    auto* owner = cl->get_device();
    auto it = states.find(owner);
    if (it == states.end() || it->second.inEffects)
        return false;
    auto& cmd = it->second.commands[cl];
    auto resource = owner->get_resource_from_view(view);
    try {it->second.route.ClearDepth(reinterpret_cast<ID3D11DeviceContext*>(cl->get_native()),reinterpret_cast<ID3D11Resource*>(resource.handle));}
    catch(const std::exception& e){it->second.route.Fail();if(it->second.lastReason!=e.what())Log("ETS2_ROUTE depth clear: %s",e.what());it->second.lastReason=e.what();}
    if (cmd.bound.depth.Get() == reinterpret_cast<ID3D11Texture2D*>(resource.handle))
        Capture(owner, it->second, cl, cmd, "before-depth-clear");
    return false;
}
bool ClearColor(command_list* cl, resource_view view, const float[4], uint32_t, const rect*)
{
    if (inside || !view.handle || !Immediate(cl))
        return false;
    Guard guard;
    auto* owner = cl->get_device();
    auto it = states.find(owner);
    if (it == states.end() || it->second.inEffects)
        return false;
    auto& cmd = it->second.commands[cl];
    auto resource = owner->get_resource_from_view(view);
    it->second.route.ClearColor(reinterpret_cast<ID3D11Resource*>(resource.handle));
    if (cmd.bound.color.Get() == reinterpret_cast<ID3D11Texture2D*>(resource.handle))
        Capture(owner, it->second, cl, cmd, "before-color-clear");
    return false;
}
void Publish(effect_runtime* r, ID3D11ShaderResourceView* view, bool valid)
{
    const resource_view binding{reinterpret_cast<uint64_t>(view)};
    r->update_texture_bindings("ETS2_STEREO_DEPTH", binding, binding);
    auto u = r->find_uniform_variable("DLSS5_Feed.fx", "ETS2_STEREO_DEPTH_VALID");
    if (u.handle)
        r->set_uniform_value_bool(u, &valid, 1);
    auto p = r->find_uniform_variable("SBSDepthProbe.fx", "ETS2_STEREO_DEPTH_VALID");
    if (p.handle)
        r->set_uniform_value_bool(p, &valid, 1);
    auto motion = r->find_uniform_variable("ETS2_VortStereo.fx", "ETS2_STEREO_DEPTH_VALID");
    if (motion.handle)
        r->set_uniform_value_bool(motion, &valid, 1);
}
void SaveDepth(ID3D11Device* d, ID3D11DeviceContext* c, ID3D11Texture2D* texture, uint64_t epoch)
{
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> st;
    depth_match::Check(d->CreateTexture2D(&desc, nullptr, &st), "capture depth staging");
    ets2_d3d11::PrivateState privateState;
    ets2_d3d11::Scope isolated(privateState,c);
    depth_match::Check(isolated.Status(),"capture depth isolated context");
    c->CopyResource(st.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE m{};
    depth_match::Check(c->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m), "capture depth map");
    char name[160];
    sprintf_s(name, "matched-depth-epoch-%llu-%ux%u-r32f.raw", epoch, desc.Width, desc.Height);
    FILE* f = nullptr;
    _wfopen_s(&f, (outputDirectory + std::wstring(name, name + strlen(name))).c_str(), L"wb");
    if (f)
    {
        for (unsigned y = 0; y < desc.Height; ++y)
            fwrite(static_cast<uint8_t*>(m.pData) + size_t(y) * m.RowPitch, 4, desc.Width, f);
        fclose(f);
    }
    c->Unmap(st.Get(), 0);
}
// This event precedes every effect technique. The target is still untouched SBS
// color; sampling it later would include PNG/Kernel/Feed modifications.
void Begin(effect_runtime* r, command_list* cl, resource_view rtv, resource_view)
try
{
    if (inside || r->get_hwnd() != nullptr || !Immediate(cl))
        return;
    Guard guard;
    auto* owner = r->get_device();
    auto& s = states[owner];
    s.runtimes.insert(r);
    Publish(r, nullptr, false);
    ++s.vrFrames;
    uint32_t runtimeWidth = 0, runtimeHeight = 0;
    r->get_screenshot_width_and_height(&runtimeWidth, &runtimeHeight);
    if (runtimeWidth % 2 || runtimeWidth < 256 || runtimeHeight < 128 || s.runtimes.size() != 1)
    {
        s.inEffects = true;
        return;
    }
    s.expectedW = runtimeWidth / 2;
    s.expectedH = runtimeHeight;
    s.route.SetDevice(reinterpret_cast<ID3D11Device*>(owner->get_native()));
    s.route.Extent(s.expectedW,s.expectedH);
    for (auto& [cmd, state] : s.commands)
        Capture(owner, s, cmd, state, "VR-begin");
    const bool routeCollected=s.route.Collect(reinterpret_cast<ID3D11DeviceContext*>(cl->get_native()),s.matcher,s.w,s.h,s.depthFormat,s.epoch,s.serial);
    if(routeCollected) {
        s.mixedScope=false;
        if(s.vrFrames<5||s.vrFrames%120==0)Log("ETS2_ROUTE epoch=%llu two HDR depth snapshots followed actual fullscreen UV transforms; checking final eye colors",s.epoch);
    }
    else if(s.vrFrames<5||s.vrFrames%120==0)Log("ETS2_ROUTE epoch=%llu snapshots=%u outputs=%u",s.epoch,s.route.SnapshotCount(),s.route.OutputCount());
    if(s.route.SnapshotCount() || s.vrFrames<5){
        if(s.routeDiagnostics<4 || s.vrFrames%600==0){
            s.route.SaveDiagnostics(outputDirectory,s.epoch);if(s.route.SnapshotCount())++s.routeDiagnostics;
        }
    }
    s.inEffects = true;
    if(!routeCollected&&!allowGenericFixture){
        const char* reason="current-frame ETS2 scene-depth route unavailable; generic AA depth forbidden";
        if(s.vrFrames<5||s.vrFrames%120==0||s.lastReason!=reason)Log("MATCH epoch=%llu rejected=%s",s.epoch,reason);
        s.lastReason=reason;return; // Publish(false) above remains authoritative
    }
    if (s.vrFrames == 1)
    {
        Log("MATCH epoch=%llu rejected=bootstrap epoch freshness unknown", s.epoch);
        return;
    }
    if (!s.matcher || s.mixedScope)
    {
        const char* reason = s.mixedScope ? "mixed candidate extents or depth formats" : "no candidates";
        if (s.vrFrames < 5 || s.vrFrames % 120 == 0 || s.lastReason != reason)
            Log("MATCH epoch=%llu rejected=%s", s.epoch, reason);
        s.lastReason = reason;
        return;
    }
    auto raw = owner->get_resource_from_view(rtv);
    ComPtr<ID3D11Texture2D> target;
    if (FAILED(reinterpret_cast<ID3D11Resource*>(raw.handle)->QueryInterface(IID_PPV_ARGS(&target))))
        return;
    LARGE_INTEGER a{}, b{}, freq{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&a);
    auto result = s.matcher->Match(target.Get(), s.epoch);
    QueryPerformanceCounter(&b);
    if (s.vrFrames < 5 || s.vrFrames % 120 == 0 || s.lastReason != result.reason)
    {
        Log("MATCH epoch=%llu VR=%p extent=%ux%u accepted=%d choices=%d,%d counts=%u,%u comparison_ms=%.4f "
            "reason=%s",
            s.epoch, r, s.w * 2, s.h, result.accepted, result.eye[0], result.eye[1], result.matches[0],
            result.matches[1], 1000.0 * (b.QuadPart - a.QuadPart) / freq.QuadPart, result.reason);
    }
    if (!s.previewSaved && previewSets < 1 && s.matcher->Count() && s.vrFrames > 1)
    {
        s.previewSaved = true;
        ++previewSets;
        s.matcher->SaveColorPreviews(s.epoch, outputDirectory);
        Log("DIAGNOSTIC_PREVIEWS epoch=%llu candidates=%u extent=%ux%u max_preview=160x128 only_once=1",
            s.epoch, s.matcher->Count(), s.w, s.h);
    }
    s.lastReason = result.reason;
    if (result.accepted)
    {
        Publish(r, s.matcher->View(), true);
        if (rawDepthFiles < 3 && uint64_t(s.w) * s.h <= 1048576)
        {
            ++rawDepthFiles;
            SaveDepth(reinterpret_cast<ID3D11Device*>(owner->get_native()),
                      reinterpret_cast<ID3D11DeviceContext*>(cl->get_native()), s.matcher->Output(), s.epoch);
        }
    }
}
catch (const std::exception& e)
{
    Guard guard;
    auto& s = states[r->get_device()];
    if (s.lastReason != e.what())
        Log("MATCH exception: %s", e.what());
    s.lastReason = e.what();
    s.inEffects = true;
    Publish(r, nullptr, false);
}
// Epochs follow VR effects only. A desktop present between eyes is not a boundary.
void Finish(effect_runtime* r, command_list*, resource_view, resource_view)
{
    if (inside || r->get_hwnd() != nullptr)
        return;
    Guard guard;
    auto it = states.find(r->get_device());
    if (it == states.end())
        return;
    auto& s = it->second;
    s.inEffects = false;
    ++s.epoch;
    s.route.Reset();
    s.mixedScope = false;
    if (s.matcher)
        s.matcher->Reset();
    for (auto& [cl, cmd] : s.commands)
        cmd.draws = 0;
}
void DestroyRuntime(effect_runtime* r)
{
    if (inside)
        return;
    Guard guard;
    auto it = states.find(r->get_device());
    if (it == states.end())
        return;
    auto& s = it->second;
    if (s.runtimes.erase(r))
    {
        Publish(r, nullptr, false);
        s.matcher.reset();
        s.commands.clear();
        s.inEffects = false;
        s.mixedScope = false;
        s.previewSaved = false;
        s.expectedW = s.expectedH = s.w = s.h = 0;
        s.depthFormat = DXGI_FORMAT_UNKNOWN;
        s.route.Extent(0,0);
        s.vrFrames = 0; // Recreated runtime must establish a fresh bootstrap epoch.
        ++s.epoch;
        s.lastReason.clear();
        Log("DESTROY_VR runtime=%p unbound custom depth", r);
    }
}
void DestroyCommand(command_list* cl)
{
    if (inside)
        return;
    Guard guard;
    auto it = states.find(cl->get_device());
    if (it != states.end())
        it->second.commands.erase(cl);
}
void InitPipeline(device* d,pipeline_layout,uint32_t count,const pipeline_subobject* subobjects,pipeline p)
{
    if(inside||d->get_api()!=device_api::d3d11)return;
    Guard guard;auto& s=states[d];s.route.SetDevice(reinterpret_cast<ID3D11Device*>(d->get_native()));
    for(uint32_t i=0;i<count;++i)if(subobjects[i].type==pipeline_subobject_type::pixel_shader){
        auto* code=static_cast<const shader_desc*>(subobjects[i].data);
        if(code&&code->code&&code->code_size)s.route.Pipeline(p.handle,code->code,code->code_size);
    }
}
void DestroyPipeline(device* d,pipeline p)
{
    if(inside)return;Guard guard;auto it=states.find(d);if(it!=states.end())it->second.route.DestroyPipeline(p.handle);
}
void DestroyDevice(device* d)
{
    if (inside)
        return;
    Guard guard;
    auto it = states.find(d);
    if (it == states.end())
        return;
    for (auto* r : it->second.runtimes)
        Publish(r, nullptr, false);
    states.erase(it);
    Log("DESTROY_DEVICE %p released owned depth matcher resources", d);
}
} // namespace
extern "C" __declspec(dllexport) const char* NAME = "ETS2 HDR stereo depth route";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Saves per-scene depth before reuse, follows pinned ETS2 pixel shaders using actual fullscreen UV geometry, and exact-matches final eye colors.";
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    // The state registry is already retained through process termination. Do
    // not enter another DLL or the CRT's file locks under the loader lock.
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr) return TRUE;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        if (!reshade::register_addon(module))
            return FALSE;
        std::wstring modulePath(32768, L'\0');
        const DWORD n = GetModuleFileNameW(module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        modulePath.resize(n); outputDirectory = modulePath.substr(0, modulePath.find_last_of(L'\\') + 1);
        _wfopen_s(&logfile, (outputDirectory + L"depth-match.log").c_str(), L"w");
        std::ofstream(std::filesystem::path(outputDirectory)/"depth-route-details.jsonl",std::ios::trunc);
        Log("Diagnostics: DLL-adjacent log capped at 4 MiB; one preview set and at most three small raw depth files per process.");
        Log("ETS2 HDR stereo depth route v3-dev: save reused scene depth, replay pinned fullscreen shader UVs, "
            "then require unique current-epoch final eye colors. Reversed-Z eye input; no draw-order eye inference.");
        Log("Scene provenance required=%d; generic depth fallback is %s",!allowGenericFixture,allowGenericFixture?"enabled in diagnostic fixture build":"forbidden");
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(Bind);
        reshade::register_event<reshade::addon_event::init_pipeline>(InitPipeline);
        reshade::register_event<reshade::addon_event::destroy_pipeline>(DestroyPipeline);
        reshade::register_event<reshade::addon_event::draw>(Draw);
        reshade::register_event<reshade::addon_event::draw_indexed>(Indexed);
        reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(ClearDepth);
        reshade::register_event<reshade::addon_event::clear_render_target_view>(ClearColor);
        reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(Finish);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(DestroyRuntime);
        reshade::register_event<reshade::addon_event::destroy_command_list>(DestroyCommand);
        reshade::register_event<reshade::addon_event::destroy_device>(DestroyDevice);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(Bind);
        reshade::unregister_event<reshade::addon_event::init_pipeline>(InitPipeline);
        reshade::unregister_event<reshade::addon_event::destroy_pipeline>(DestroyPipeline);
        reshade::unregister_event<reshade::addon_event::draw>(Draw);
        reshade::unregister_event<reshade::addon_event::draw_indexed>(Indexed);
        reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(ClearDepth);
        reshade::unregister_event<reshade::addon_event::clear_render_target_view>(ClearColor);
        reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);
        reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(Finish);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(DestroyRuntime);
        reshade::unregister_event<reshade::addon_event::destroy_command_list>(DestroyCommand);
        reshade::unregister_event<reshade::addon_event::destroy_device>(DestroyDevice);
        reshade::unregister_addon(module);
        if (!reserved)
        {
            // Dynamic unloading while a runtime survives must never leave its
            // custom semantic referring to a view that is about to be released.
            for (auto& [device, state] : states)
                for (auto* runtime : state.runtimes)
                    Publish(runtime, nullptr, false);
            states.clear();
        }
        if (logfile)
        {
            fclose(logfile);
            logfile = nullptr;
        }
    }
    return TRUE;
}
