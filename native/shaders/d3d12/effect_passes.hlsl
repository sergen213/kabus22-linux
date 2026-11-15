// HLSL ports of the OpenGL wave-2 screen-effect programs (render_gl.cpp tonemapFS ... fxaaFS); one entry per program.
// The linearize pass reads the scene depth buffer directly (sample 0 of the view rectangle at depthOffset) in place of
// OpenGL's depth blit into a single-sample copy.
#include "common.hlsli"
cbuffer Effect:register(b0){
    float4x4 viewProjection,inverseViewProjection;
    float3 eye;float nearPlane;
    float3 forward;float farPlane;
    float2 texel,fogRange;
    int mode;int bloomEnabled;int toneMap;int lightCount;
    int shadowMaps;int shadowLight;int2 depthOffset;
};
#if defined(K22_FOG)
StructuredBuffer<float4> lights:register(t1);
#define K22_LIGHT(i) lights[i]
TextureCube<float> cubeShadowMap:register(t2);
float fogVisibility(int i,float3 x);
#define K22_FOG_VISIBILITY(i,x) fogVisibility(i,x)
#include "effects.hlsli"
float fogVisibility(int i,float3 x){if(!shadowMaps||i!=shadowLight)return 1.0;float4 a=K22_LIGHT(i*5);return k22CubeShadow(cubeShadowMap,a.xyz,x,float3(0,0,0),k22CubeShadowNear,a.w);}
Texture2D linearDepth:register(t0);
float4 fogPS(K22PostVaryings v):SV_Target0{float d=linearDepth.SampleLevel(K22_SAMPLER(0),v.uv,0).r;if(d<=0.0)d=farPlane;return float4(k22FogInscatter(eye,eye+k22ViewRay(k22NdcFromUv(v.uv),inverseViewProjection,eye,forward)*d,fogRange,k22Noise(v.position.xy)),d);}
#else
static const int lightCount_=0;
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
#elif defined(K22_FOG_UPSAMPLE)
Texture2D fog:register(t0);Texture2D linearDepth:register(t1);
float4 fogUpsamplePS(K22PostVaryings v):SV_Target0{
    uint fw,fh;fog.GetDimensions(fw,fh);float2 size=float2(fw,fh),p=v.uv*size-0.5,f=frac(p);int2 b=int2(floor(p)),last=int2(size)-1;float d=linearDepth.Sample(K22_SAMPLER(1),v.uv).r;if(d<=0.0)d=farPlane;
    float3 sum=0,best=0;float weight=0.0,closest=1e30;
    for(int i=0;i<4;++i){int2 o=int2(i&1,i>>1);float4 s=fog.Load(int3(clamp(b+o,int2(0,0),last),0));float dz=abs(s.a-d);
        float w=(o.x==1?f.x:1.0-f.x)*(o.y==1?f.y:1.0-f.y)*exp(-dz/(0.05*max(d,1e-3)));sum+=w*s.rgb;weight+=w;if(dz<closest){closest=dz;best=s.rgb;}}
    return float4(weight>1e-4?sum/weight:best,0);
}
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
#endif
