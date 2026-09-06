#pragma once

// Local ETS2 investigation: metadata only. The active-runtime callback owns the
// feed lock. No native OpenXR handle is queried or interpreted as a COM object.
// Sampling every 300 fed frames keeps enumeration and log I/O off the hot path.
static void LogStereoDepthContract(reshade::api::effect_runtime *rt,
                                  reshade::api::resource_view color_rtv)
{
    using namespace reshade::api;
    static effect_runtime *last_runtime = nullptr;
    static unsigned frames_until_sample = 0;
    if (!rt || !color_rtv.handle) return;
    if (rt != last_runtime) {
        last_runtime = rt;
        frames_until_sample = 0;
    }
    if (frames_until_sample != 0) {
        --frames_until_sample;
        return;
    }
    frames_until_sample = 299;

    device *dev = rt->get_device();
    if (!dev) return;
    const resource color = dev->get_resource_from_view(color_rtv);
    if (!color.handle) return;
    const resource_desc cd = dev->get_resource_desc(color);
    if (cd.type != resource_type::texture_2d) {
        Log("[stereo-diag] runtime=%p color resource type=%u; no 2D comparison", (void *)rt, (unsigned)cd.type);
        return;
    }
    Log("[stereo-diag] runtime=%p hwnd=%p api=%u color=%llx %ux%u layers=%u format=%u samples=%u; metadata does not establish eye alignment",
        (void *)rt, rt->get_hwnd(), (unsigned)dev->get_api(),
        (unsigned long long)color.handle, cd.texture.width, cd.texture.height,
        (unsigned)cd.texture.depth_or_layers, (unsigned)cd.texture.format,
        (unsigned)cd.texture.samples);

    unsigned found = 0;
    rt->enumerate_texture_variables("DLSS5_Feed.fx", [&](effect_runtime *runtime, effect_texture_variable variable) {
        char name[256] = {};
        size_t name_size = sizeof(name);
        runtime->get_texture_variable_name(variable, name, &name_size);
        name[sizeof(name) - 1] = '\0';
        if (strstr(name, "DepthBufferTex") == nullptr &&
            strstr(name, "DLSS5_RawDepthDiagnostic") == nullptr)
            return;
        ++found;
        resource_view srv = {}, srgb = {};
        runtime->get_texture_binding(variable, &srv, &srgb);
        if (!srv.handle) {
            Log("[stereo-diag] raw depth variable '%s' has no binding", name);
            return;
        }
        const resource res = dev->get_resource_from_view(srv);
        if (!res.handle) {
            Log("[stereo-diag] raw depth variable '%s' binding has no resource", name);
            return;
        }
        const resource_desc desc = dev->get_resource_desc(res);
        const resource_view_desc view = dev->get_resource_view_desc(srv);
        if (desc.type != resource_type::texture_2d) {
            Log("[stereo-diag] raw depth name='%s' resource type=%u; no 2D comparison", name, (unsigned)desc.type);
            return;
        }
        const char *shape = "extent differs from color; layout unknown";
        if (desc.texture.width == cd.texture.width && desc.texture.height == cd.texture.height)
            shape = "same extent as color; stereo content still unverified";
        else if (cd.texture.width % 2u == 0 && desc.texture.width == cd.texture.width / 2u && desc.texture.height == cd.texture.height)
            shape = desc.texture.depth_or_layers >= 2
                ? "half-width array candidate; both layer contents require inspection"
                : "half-width single-layer resource; a second eye is not exposed by this resource";
        Log("[stereo-diag] raw depth name='%s' srv=%llx resource=%llx %ux%u layers=%u levels=%u samples=%u format=%u usage=%x; view type=%u format=%u mip=%u count=%u layer=%u count=%u; %s",
            name, (unsigned long long)srv.handle, (unsigned long long)res.handle,
            desc.texture.width, desc.texture.height,
            (unsigned)desc.texture.depth_or_layers, (unsigned)desc.texture.levels,
            (unsigned)desc.texture.samples, (unsigned)desc.texture.format,
            (unsigned)desc.usage, (unsigned)view.type, (unsigned)view.format,
            view.texture.first_level, view.texture.levels,
            view.texture.first_layer, view.texture.layers, shape);
    });
    if (found == 0)
        Log("[stereo-diag] raw DEPTH semantic variable not exposed by DLSS5_Feed.fx; no depth-layout conclusion possible");
}
