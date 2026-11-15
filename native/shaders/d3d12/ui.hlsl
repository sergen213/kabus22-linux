// HLSL port of the OpenGL UI program (render_gl.cpp uiVS/uiFS): atlas sub-rect clamped sampling, text/overlay keys.
#include "common.hlsli"
cbuffer UI:register(b0){float2 viewport;float alphaRef;int overlay;int textKey;int cursorKey;int2 uiPadding;};
Texture2D image:register(t0);
struct UIInput{float2 position:POSITION;float2 texcoord:TEXCOORD0;float4 vertexColor:COLOR0;float4 texBounds:TEXCOORD1;};
struct UIVaryings{float4 position:SV_Position;float2 uv:TEXCOORD0;float4 tint:COLOR0;nointerpolation float4 bounds:BOUNDS;};
UIVaryings uiVS(UIInput v){UIVaryings o;o.uv=v.texcoord;o.tint=v.vertexColor;o.bounds=v.texBounds;o.position=k22Clip(float4(v.position.x/viewport.x*2-1,1-v.position.y/viewport.y*2,0,1));return o;}
float4 uiPS(UIVaryings v):SV_Target0{
    uint w,h;image.GetDimensions(w,h);float2 half_=.5/float2(w,h),lo=v.bounds.xy+half_;float4 c=image.Sample(K22_SAMPLER(0),clamp(v.uv,lo,max(lo,v.bounds.zw-half_)));float brightness=max(max(c.r,c.g),c.b);
    if((textKey&&brightness<0.001)||(overlay&&((!cursorKey&&brightness<0.001)||c.a<alphaRef)))discard;
    return c*v.tint;
}
