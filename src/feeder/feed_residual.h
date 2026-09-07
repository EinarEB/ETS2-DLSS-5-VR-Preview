#pragma once
#include <cstdint>

// Experimental SDR composition. The difference includes DLAA and classic NR.
// t0 final reduced pipeline output, t1 untouched native original, t2 stored reduced input.
// b0 and s0 must be restored by the caller, along with all three SRV slots.
struct ResidualConstants {
    float strength;      // 0..1; zero is an exact original-image branch
    uint32_t enabled;   // zero is legacy bilinear, solely for off-path validation
    uint32_t stereo;    // equal-width, even SBS; zero is an ordinary mono image
    uint32_t stabilized; // t0 is already a residual when nonzero
    float crop_left[4]={};  // left eye square: min x, min y, max x, max y in eye-local uv
    float crop_right[4]={}; // right eye square; differs from the left one when the squares are aligned on far content
    float crop_feather=0; uint32_t cropped=0; float padding[2]={};
};
static_assert(sizeof(ResidualConstants) == 64);

static const char kResidualSrc[] = R"HLSL(
Texture2D<float4> low_output : register(t0);
Texture2D<float4> original : register(t1);
Texture2D<float4> low_input : register(t2);
SamplerState linear_smp : register(s0);
cbuffer ResidualConstants : register(b0) {
    float strength; uint enabled; uint stereo; uint stabilized;
    float4 crop_left;
    float4 crop_right;
    float crop_feather; uint cropped; float2 padding;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float3 ResidualTap(int2 p, int first_x, int last_x, int height)
{
    p = clamp(p, int2(first_x, 0), int2(last_x, height - 1));
    float3 result=low_output.Load(int3(p, 0)).rgb;
    return stabilized!=0?result:result-low_input.Load(int3(p, 0)).rgb;
}

float4 ps_residual(VSOut i) : SV_Target
{
    // Actual pre-existing mono bilinear expression. Product integration should
    // retain the entire old bilinear/FSR/RCAS route when the option is disabled.
    if (enabled == 0)
        return float4(low_output.Sample(linear_smp, i.uv).rgb, 1.0);

    int2 pixel = int2(i.pos.xy);
    float4 native = original.Load(int3(pixel, 0));
    if (!isfinite(strength) || strength <= 0.0)
        return native;

    uint nw, nh, iw, ih, ow, oh;
    original.GetDimensions(nw, nh);
    low_input.GetDimensions(iw, ih);
    low_output.GetDimensions(ow, oh);
    if (iw == 0 || ih == 0 || iw != ow || ih != oh || iw > nw || ih > nh)
        return native;
    if (stereo != 0 && (iw < 2 || nw < 2 || (iw & 1) != 0 || (nw & 1) != 0))
        return native;

    float coverage=1.0;
    if(cropped!=0) {
        if(stereo==0)return native;
        int ew=int(nw/2);int eye=pixel.x>=ew?1:0;
        float2 uv=(float2(pixel-int2(eye*ew,0))+.5)/float2(ew,nh);
        float4 crop_bounds=eye==0?crop_left:crop_right;
        float2 edge=min(uv-crop_bounds.xy,crop_bounds.zw-uv)*float2(iw/2,ih);
        if(any(edge<=0))return native;
        coverage=smoothstep(0.0,max(crop_feather,1.0),min(edge.x,edge.y));
    }

    float3 delta;
    if (iw == nw && ih == nh) {
        delta = ResidualTap(pixel,0,int(iw)-1,int(ih));
    } else {
        // Compute bilinear weights in eye-local coordinates. This avoids large
        // packed-coordinate rounding differences between two otherwise identical eyes.
        int out_eye_width = stereo != 0 ? int(nw / 2) : int(nw);
        int in_eye_width = stereo != 0 ? int(iw / 2) : int(iw);
        int eye = stereo != 0 && pixel.x >= out_eye_width ? 1 : 0;
        int2 local_pixel = pixel - int2(eye * out_eye_width, 0);
        float2 phase = (float2(local_pixel) + 0.5) *
            (float2(in_eye_width, ih) / float2(out_eye_width, nh)) - 0.5;
        int2 base = int2(floor(phase));
        float2 weight = frac(phase);
        int first_x = eye * in_eye_width;
        int last_x = first_x + in_eye_width - 1;
        base.x += first_x;
        float3 a = ResidualTap(base, first_x, last_x, int(ih));
        float3 b = ResidualTap(base + int2(1, 0), first_x, last_x, int(ih));
        float3 c = ResidualTap(base + int2(0, 1), first_x, last_x, int(ih));
        float3 d = ResidualTap(base + int2(1, 1), first_x, last_x, int(ih));
        delta = lerp(lerp(a, b, weight.x), lerp(c, d, weight.x), weight.y);
    }
    if (!all(isfinite(delta)))
        return native;
    if (all(delta == 0.0))
        return native;

    // Explicit SDR code-value composition and per-channel clipping. No HDR proxy,
    // gamma conversion, temporal accumulator, FSR, or sharpening is implied.
    return float4(saturate(native.rgb + saturate(strength) * coverage * delta), native.a);
}
)HLSL";
