// Shared by every Direct3D 12 shader (native/render_d3d12.cpp). The backend keeps OpenGL's memory layout: texture and
// render-target row 0 is the bottom row of the OpenGL image, so texture coordinates, gl_FragCoord (SV_Position) and
// viewport/scissor rows stay identical; only the vertex output is converted from GL clip space (k22Clip).
#ifndef K22_COMMON_HLSLI
#define K22_COMMON_HLSLI
// GL clip space -> D3D: NDC y is negated (storage rows equal GL window rows), z [-w,w] -> [0,w] (same window depth).
float4 k22Clip(float4 p){return float4(p.x,-p.y,(p.z+p.w)*0.5,p.w);}
// Root signature: sampler table s0.. (backend sampler cache) indexed per SRV register by root constants b2; static
// samplers s0/s1 space1 are the shadow-map comparison (GL texture parameters LINEAR, CLAMP, LEQUAL) and the mip builder's.
SamplerState k22Samplers[256]:register(s0);
SamplerComparisonState k22ShadowCompare:register(s0,space1);
SamplerState k22LinearClamp:register(s1,space1);
cbuffer K22SamplerTable:register(b2){uint4 k22SamplerIndex[4];}
#define K22_SAMPLER(r) k22Samplers[k22SamplerIndex[(r)>>2][(r)&3]]
float k22Mod(float x,float y){return x-y*floor(x/y);}
struct K22PostVaryings{float4 position:SV_Position;float2 uv:TEXCOORD0;};
#endif
