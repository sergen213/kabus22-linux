// HLSL ports of the OpenGL wave-2 screen-effect programs (render_gl.cpp tonemapFS ... fxaaFS); one entry per program.
// The linearize pass reads the scene depth buffer directly (sample 0 of the view rectangle at depthOffset) in place of
// OpenGL's depth blit into a single-sample copy.
#include "common.hlsli"
cbuffer Effect:register(b0){
    float4x4 viewProjection,inverseViewProjection;
    float3 eye;float nearPlane;
    float3 forward;float farPlane;
    float2 texel;int mode;int bloomEnabled;
    int toneMap;int2 depthOffset;
};
static const int lightCount=0;
#define K22_LIGHT(i) float4(0,0,0,0)
#include "pbr.hlsli"
#include "effects.hlsli"
#if defined(K22_LINEARIZE_MS)
Texture2DMS<float> depth:register(t0);
float4 linearizePS(K22PostVaryings v):SV_Target0{float d=depth.Load(int2(v.position.xy)+depthOffset,0).r;return d<1.0?k22LinearDepth(d,nearPlane,farPlane):0.0;}
#elif defined(K22_LINEARIZE)
Texture2D<float> depth:register(t0);
float4 linearizePS(K22PostVaryings v):SV_Target0{float d=depth.Load(int3(int2(v.position.xy)+depthOffset,0)).r;return d<1.0?k22LinearDepth(d,nearPlane,farPlane):0.0;}
#elif defined(K22_TONEMAP)
Texture2D image:register(t0);
float4 tonemapPS(K22PostVaryings v):SV_Target0{float4 c=image.Sample(K22_SAMPLER(0),v.uv);return float4(k22ToneMap(c.rgb),c.a);}
#elif defined(K22_SSAO)
Texture2D linearDepth:register(t0);
float4 ssaoPS(K22PostVaryings v):SV_Target0{return k22Ssao(linearDepth,K22_SAMPLER(0),v.uv,texel,inverseViewProjection,viewProjection,eye,forward,k22Noise(v.position.xy));}
#elif defined(K22_AO_APPLY)
Texture2D ao:register(t0);
float4 aoApplyPS(K22PostVaryings v):SV_Target0{float a=0.0;for(int i=0;i<4;++i)a+=ao.Sample(K22_SAMPLER(0),v.uv+texel*float2((i&1)!=0?0.5:-1.5,(i&2)!=0?0.5:-1.5)).r;return float4(a*0.25,a*0.25,a*0.25,1);}
#elif defined(K22_BLOOM)
Texture2D image:register(t0);
float4 bloomPS(K22PostVaryings v):SV_Target0{return float4(mode==0?k22BloomPrefilter(k22BloomDown(image,K22_SAMPLER(0),v.uv,texel)):mode==1?k22BloomDown(image,K22_SAMPLER(0),v.uv,texel):k22BloomUp(image,K22_SAMPLER(0),v.uv,texel),1);}
#elif defined(K22_COMPOSITE)
Texture2D image:register(t0);Texture2D bloom:register(t1);
float4 compositePS(K22PostVaryings v):SV_Target0{float4 c=image.Sample(K22_SAMPLER(0),v.uv);if(bloomEnabled)c.rgb+=k22BloomStrength*bloom.Sample(K22_SAMPLER(1),v.uv).rgb;return float4(toneMap?k22ToneMap(c.rgb):c.rgb,c.a);}
#elif defined(K22_FXAA)
Texture2D image:register(t0);
float4 fxaaPS(K22PostVaryings v):SV_Target0{return float4(k22Fxaa(image,K22_SAMPLER(0),v.uv,texel),image.Sample(K22_SAMPLER(0),v.uv).a);}
#endif
