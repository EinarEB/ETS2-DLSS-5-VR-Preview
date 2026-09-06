#pragma once
static const char kSbsBlitSrc[] = R"HLSL(Texture2D<float4> src_color : register(t0);
Texture2D<float2> src_mv : register(t1);
Texture2D<float> src_depth : register(t2);
Texture2D<float> src_mask : register(t3);
SamplerState linear_smp : register(s0);
SamplerState point_smp : register(s1);
cbuffer ResampleConstants : register(b0) { float2 mv_scale; float2 jitter_uv; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs(uint id : SV_VertexID) { VSOut o; float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv; o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
float4 ps(VSOut i) : SV_Target { return float4(src_color.Sample(linear_smp, i.uv).rgb, 1.0); }
struct ResampleOut { float4 color : SV_Target0; float2 mv : SV_Target1; float depth : SV_Target2; float mask : SV_Target3; };
ResampleOut ps_resample(VSOut i) { ResampleOut o; float2 uv = i.uv + jitter_uv;
  o.color = src_color.SampleLevel(linear_smp, uv, 0);
  o.mv = src_mv.SampleLevel(point_smp, uv, 0) * mv_scale;
  o.depth = src_depth.SampleLevel(point_smp, uv, 0);
  o.mask = src_mask.SampleLevel(point_smp, uv, 0); return o; }

// Separate entry point; the original mono ps and resample entry points are unchanged.
// Requires an even-width source and even-width full-output viewport at origin 0.
float4 ps_sbs(VSOut i) : SV_Target {
    uint width, height; src_color.GetDimensions(width, height);
    float eye = i.uv.x >= 0.5 ? 1.0 : 0.0;
    float half_texel = 0.5 / float(width);
    float2 uv = i.uv;
    uv.x = clamp(uv.x, eye * 0.5 + half_texel, (eye + 1.0) * 0.5 - half_texel);
    return float4(src_color.SampleLevel(linear_smp, uv, 0).rgb, 1.0);
}
)HLSL";
