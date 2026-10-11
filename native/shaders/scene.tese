#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
// Curved PN triangles (Vlachos et al. 2001) over world-space vertices and smoothed normals; everything else is linear.
// Vulkan's upper-left tessellation domain origin: `cw` keeps the input triangle's winding (culling unchanged).
layout(triangles,fractional_odd_spacing,cw) in;
layout(location=0) in vec3 P[];
layout(location=1) in vec3 N[];
layout(location=2) in vec2 inUv[];
layout(location=3) in vec2 inLightUv[];
layout(location=4) in vec3 inDynamicLight[];
layout(location=5) in vec3 inEffectLight[];
layout(location=6) in vec3 inEffectView[];
layout(location=7) in vec3 inMaterialDiffuse[];
layout(location=8) in vec3 inMaterialSpecular[];
layout(location=9) in vec3 inTangent[];
layout(location=10) in vec4 inPreviousClip[];
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 uv;
layout(location=3) out vec2 lmuv;
layout(location=4) out vec3 vertexDynamicLight;
layout(location=5) out vec3 effectLight;
layout(location=6) out vec3 effectView;
layout(location=7) out vec3 vertexMaterialDiffuse;
layout(location=8) out vec3 vertexMaterialSpecular;
layout(location=11) flat out vec4 instanceTint;  // scene.frag interface; only particleInstanced draws (never tessellated) read it.
layout(location=12) flat out vec4 instanceFlags;
layout(location=9) out vec3 worldTangent;
layout(location=10) out vec4 previousClip;
layout(push_constant) uniform CasterPush { mat4 casterViewProjection; } casterPush;
// Edge control point next to corner a toward corner b: a third of the way, projected onto a's tangent plane. The
// projection is invariant to the normal's sign, so mirrored (normalOrientation -1) models bulge the same way.
// k fades the projection out where a corner's shared normal misrepresents an adjacent face (welded box/plate corners
// would inflate into pillows): MDL models carry that PN weight in lightmap_uv.x (render_policy smooth_normals). An
// edge uses the smaller weight of its two corners, so both triangles sharing it agree (no cracks).
vec3 edgePoint(vec3 a,vec3 b,vec3 n,float k){return (2.0*a+b-k*dot(b-a,n)*n)/3.0;}
vec3 edgeNormal(vec3 a,vec3 b,vec3 na,vec3 nb){vec3 e=b-a;float l=dot(e,e);vec3 h=na+nb-(l>0.0?2.0*dot(e,na+nb)/l:0.0)*e;return dot(h,h)>0.0?normalize(h):na;}
void main(){
    instanceTint=tint;instanceFlags=vec4(0);
    vec3 w=gl_TessCoord;
    #define LINEAR(V) (V[0]*w.x+V[1]*w.y+V[2]*w.z)
    uv=LINEAR(inUv);lmuv=LINEAR(inLightUv);vertexDynamicLight=LINEAR(inDynamicLight);effectLight=LINEAR(inEffectLight);effectView=LINEAR(inEffectView);
    vertexMaterialDiffuse=LINEAR(inMaterialDiffuse);vertexMaterialSpecular=LINEAR(inMaterialSpecular);worldTangent=LINEAR(inTangent);
    vec3 n0=normalize(N[0]),n1=normalize(N[1]),n2=normalize(N[2]);
    float k01=min(inLightUv[0].x,inLightUv[1].x),k12=min(inLightUv[1].x,inLightUv[2].x),k20=min(inLightUv[2].x,inLightUv[0].x);
    vec3 b210=edgePoint(P[0],P[1],n0,k01),b120=edgePoint(P[1],P[0],n1,k01),b021=edgePoint(P[1],P[2],n1,k12),b012=edgePoint(P[2],P[1],n2,k12),b102=edgePoint(P[2],P[0],n2,k20),b201=edgePoint(P[0],P[2],n0,k20);
    vec3 e=(b210+b120+b021+b012+b102+b201)/6.0,b111=e+(e-(P[0]+P[1]+P[2])/3.0)*0.5;
    float wa=w.x,wb=w.y,wc=w.z; // `u` is the uniform block (raster_uniform.glsl).
    precise vec3 p=P[0]*wa*wa*wa+P[1]*wb*wb*wb+P[2]*wc*wc*wc+3.0*(b210*wa*wa*wb+b120*wa*wb*wb+b021*wb*wb*wc+b012*wb*wc*wc+b102*wc*wc*wa+b201*wc*wa*wa)+6.0*b111*wa*wb*wc;
    worldPosition=p;
    // Motion pass: clip space is linear in position, so carry the PN bulge into the previous frame unchanged.
    previousClip=k22Pass==3?LINEAR(inPreviousClip)+u.dataPreviousViewProjection*vec4(p-LINEAR(P),0):vec4(0);
    worldNormal=normalize(n0*wa*wa+n1*wb*wb+n2*wc*wc+edgeNormal(P[0],P[1],n0,n1)*wa*wb+edgeNormal(P[1],P[2],n1,n2)*wb*wc+edgeNormal(P[2],P[0],n2,n0)*wc*wa);
    gl_Position=(k22Pass==1?casterPush.casterViewProjection:viewProjection)*vec4(p,1);
    gl_Position.y=-gl_Position.y;
    gl_Position.z=(gl_Position.z+gl_Position.w)*.5;
    gl_ClipDistance[0]=dot(clipPlane,vec4(p,1));
}
