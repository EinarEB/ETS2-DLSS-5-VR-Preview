#pragma once

// Included after the D3D11 input helpers. This route never falls through into
// ordinary replacement: an unsupported or failed enabled composition leaves
// the runtime's original color untouched. The disabled option takes old code.
static bool ResidualPreserveOriginal(const char *reason)
{
    if (g.residual_reason != reason) {
        Log("[composite] preserving original frame: %s", reason);
        g.residual_reason = reason;
    }
    return false;
}

static bool ResidualSdrFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM ||
           (g.stereo_sdr_float&&format==DXGI_FORMAT_R16G16B16A16_FLOAT);
}

static bool EnsureResidualResources()
{
    if (g.residual_failed) return false;
    HRESULT hr = S_OK;
    if (g.residual_input_srv == nullptr) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
        view.Format = g.color_fmt;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        hr = g.dev11->CreateShaderResourceView(g.tex11[SLOT_COLOR], &view, &g.residual_input_srv);
    }
    if (SUCCEEDED(hr) && g.residual_ps == nullptr) {
        HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        auto compile = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
        ID3DBlob *shader = nullptr, *error = nullptr;
        hr = compile ? compile(kResidualSrc, sizeof(kResidualSrc) - 1, "feedresidual", nullptr, nullptr,
                               "ps_residual", "ps_5_0", 0, 0, &shader, &error) : E_NOINTERFACE;
        if (FAILED(hr)) Log("[composite] shader compile failed 0x%08X: %s", hr, error ? static_cast<const char *>(error->GetBufferPointer()) : "compiler unavailable");
        if (SUCCEEDED(hr)) hr = g.dev11->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &g.residual_ps);
        SafeRelease(shader); SafeRelease(error);
        if (compiler) FreeLibrary(compiler);
    }
    if (SUCCEEDED(hr) && g.residual_cb == nullptr) {
        D3D11_BUFFER_DESC buffer = {};
        buffer.ByteWidth = sizeof(ResidualConstants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = g.dev11->CreateBuffer(&buffer, nullptr, &g.residual_cb);
    }
    if (FAILED(hr)) {
        g.residual_failed = true;
        Log("[composite] resource setup failed 0x%08X; original frames remain visible", hr);
        return false;
    }
    return true;
}

static bool CompositeOutputToBackbuffer(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv)
{
    if(g_cfg.stereo_stability<=0)g_stability.Invalidate();
    if (g.hdr || !ResidualSdrFormat(g.color_fmt) || !ResidualSdrFormat(g.output_fmt)) {
        ResidualPreserveOriginal("work_composite requires SDR RGBA8/BGRA8 UNORM color"); return false;
    }
    if (g_cfg.work_upscale == 2 || g.sr_active || g.jitter_x != 0.0f || g.jitter_y != 0.0f) {
        ResidualPreserveOriginal("work_composite does not support synthetic-jitter Super Resolution"); return false;
    }
    if (!rtv || !g.dev11 || !g.tex11[SLOT_COLOR] || !g.output_srv || !g.blit_vs) {
        ResidualPreserveOriginal("required D3D11 color resources are unavailable"); return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC target_view = {};
    rtv->GetDesc(&target_view);
    if (target_view.Format != TypedColorFormat(g.bb_fmt) || target_view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || target_view.Texture2D.MipSlice != 0) {
        ResidualPreserveOriginal("destination must be the matching ordinary UNORM RTV at mip 0 (not the sRGB view)"); return false;
    }
    if (g.width == 0 || g.height == 0 || g.width > g.backbuffer_width || g.height > g.backbuffer_height ||
        g.output_width != g.width || g.output_height != g.height ||
        (g_cfg.stereo_mode && ((g.width & 1) || (g.backbuffer_width & 1) || g.width < 2))) {
        ResidualPreserveOriginal("invalid native/work geometry for equal-eye residual composition"); return false;
    }
    if (!std::isfinite(g_frame_work_mix) || (g_frame_work_mix <= 0.0f && g_cfg.stereo_stability <= 0)) {
        // All transport/model work has still happened. Do not even touch bindings,
        // alpha, a color conversion, or the already intact native runtime image.
        g_stability.Invalidate();ResidualPreserveOriginal("comparison blend is zero; neural processing stays active"); return true;
    }
    if (!EnsureResidualResources()) {
        ResidualPreserveOriginal("residual shader/input view/buffer setup failed"); return false;
    }
    const bool reduced = g.width != g.backbuffer_width || g.height != g.backbuffer_height;
    ID3D11ShaderResourceView *native = (reduced||g.stereo_sdr_float) ? g.color_stage_srv : g.residual_input_srv;
    if (!native) { ResidualPreserveOriginal("untouched native color copy is unavailable"); return false; }

    ID3D11Resource *target = nullptr, *original = nullptr, *input = nullptr, *output = nullptr;
    rtv->GetResource(&target); native->GetResource(&original);
    g.residual_input_srv->GetResource(&input); g.output_srv->GetResource(&output);
    ID3D11Texture2D *target_texture = nullptr;
    const HRESULT target_hr = target ? target->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&target_texture)) : E_POINTER;
    D3D11_TEXTURE2D_DESC target_desc = {};
    if (target_texture) target_texture->GetDesc(&target_desc);
    const bool valid_target = SUCCEEDED(target_hr) && target != original && target != input && target != output &&
        target_desc.Width == g.backbuffer_width && target_desc.Height == g.backbuffer_height &&
        target_desc.ArraySize == 1 && target_desc.SampleDesc.Count == 1;
    SafeRelease(target_texture); SafeRelease(target); SafeRelease(original); SafeRelease(input); SafeRelease(output);
    if (!valid_target) { ResidualPreserveOriginal("destination aliases an input or has an unexpected extent/layer layout"); return false; }

    ID3D11ShaderResourceView* processed=g.output_srv;
    if(g_cfg.stereo_stability>0){
        if(!g_cfg.stereo_mode||!g_cfg.stereo_depth_required||!g.depth_reversed||!g_stereo.active||!g_stereo.copy_carrier){
            g_stability.Invalidate();ResidualPreserveOriginal("neural stability requires the verified reversed-depth stereo route with preserved game color");return false;
        }
        try{
            g_profile11.Mark(ctx,ets2_profile::FilterStart);
            processed=g_stability.Apply(ctx,g.tex11[SLOT_COLOR],g.tex11[SLOT_OUTPUT],g.tex11[SLOT_DEPTH],g.tex11[SLOT_MV],g_stability_epoch,ets2_native::Controls().CommonReset(),g_cfg.stereo_stability);
            g_profile11.Mark(ctx,ets2_profile::FilterDone);
            g_stability_applied=true;
        }catch(const std::exception& error){
            Log("[stability] %s; current original retained",error.what());
            ResidualPreserveOriginal("neural stability setup or execution failed");return false;
        }
    }

    // Keep the neural-edit history warm while comparing. The native target is
    // still untouched: stabilization only writes its own private textures.
    if (g_frame_work_mix <= 0.0f) return true;

    if (!g.residual_announced || g.residual_reason != nullptr) {
        Log("[composite] preserving original detail: native %ux%u, stored input/output %ux%u, mix %.3f, SBS=%d; combined DLAA+NR difference, native alpha, no FSR/RCAS",
            g.backbuffer_width, g.backbuffer_height, g.width, g.height, g_frame_work_mix, g_cfg.stereo_mode);
        g.residual_announced = true; g.residual_reason = nullptr;
    }

    ets2_d3d11::Scope isolation(g_private11,ctx);
    if(!isolation){g_stability.Invalidate();return ResidualPreserveOriginal("complete D3D11 context isolation unavailable");}
    ResidualConstants constants = {g_frame_work_mix, 1, static_cast<uint32_t>(g_cfg.stereo_mode != 0),g_stability_applied?1u:0u};
    if(g_stereo.active&&g_stereo.crop_percent){
        constants.cropped=1;
        constants.crop_min_x=float(g_stereo.crop_x)/float(g.width/2);
        constants.crop_min_y=float(g_stereo.crop_y)/float(g.height);
        constants.crop_max_x=float(g_stereo.crop_x+g_stereo.width)/float(g.width/2);
        constants.crop_max_y=float(g_stereo.crop_y+g_stereo.height)/float(g.height);
        constants.crop_feather=float(std::min(g_stereo.width,g_stereo.height))*.08f;
    }
    ctx->UpdateSubresource(g.residual_cb, 0, nullptr, &constants, 0, 0);
    ctx->PSSetConstantBuffers(0, 1, &g.residual_cb);
    D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(g.backbuffer_width), static_cast<float>(g.backbuffer_height), 0, 1};
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(nullptr, 0); ctx->RSSetState(nullptr);
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g.blit_vs, nullptr, 0); ctx->PSSetShader(g.residual_ps, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0); ctx->HSSetShader(nullptr, nullptr, 0); ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->PSSetSamplers(0, 1, &g.blit_sampler);
    // The private context block has cleared caller output aliases; the scope
    // restores their complete state after this draw.
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ID3D11ShaderResourceView *sources[] = {processed, native, g.residual_input_srv};
    ctx->PSSetShaderResources(0, 3, sources); ctx->Draw(3, 0);

    return true;
}
