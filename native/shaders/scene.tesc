#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
// tessellation=1: MDL model triangles become PN patches (scene.tese). Every scene.vert varying passes through unchanged.
layout(vertices=3) out;
layout(location=0) in vec3 inPosition[];
layout(location=1) in vec3 inNormal[];
layout(location=2) in vec2 inUv[];
layout(location=3) in vec2 inLightUv[];
layout(location=4) in vec3 inDynamicLight[];
layout(location=5) in vec3 inEffectLight[];
layout(location=6) in vec3 inEffectView[];
layout(location=7) in vec3 inMaterialDiffuse[];
layout(location=8) in vec3 inMaterialSpecular[];
layout(location=9) in vec3 inTangent[];
layout(location=10) in vec4 inPreviousClip[];
layout(location=0) out vec3 outPosition[];
layout(location=1) out vec3 outNormal[];
layout(location=2) out vec2 outUv[];
layout(location=3) out vec2 outLightUv[];
layout(location=4) out vec3 outDynamicLight[];
layout(location=5) out vec3 outEffectLight[];
layout(location=6) out vec3 outEffectView[];
layout(location=7) out vec3 outMaterialDiffuse[];
layout(location=8) out vec3 outMaterialSpecular[];
layout(location=9) out vec3 outTangent[];
layout(location=10) out vec4 outPreviousClip[];
// Distance-adaptive level from the edge alone (shared edges get equal levels: no cracks): about one segment per two
// world units of edge at 256 units from the camera, clamped to 1..8, and flat beyond 2000 units.
float edgeLevel(vec3 a,vec3 b){float d=distance((a+b)*0.5,cameraPosition);return d>2000.0?1.0:clamp(distance(a,b)*128.0/max(d,1.0),1.0,8.0);}
void main(){
    #define I gl_InvocationID
    outPosition[I]=inPosition[I];outNormal[I]=inNormal[I];outUv[I]=inUv[I];outLightUv[I]=inLightUv[I];outDynamicLight[I]=inDynamicLight[I];
    outEffectLight[I]=inEffectLight[I];outEffectView[I]=inEffectView[I];outMaterialDiffuse[I]=inMaterialDiffuse[I];outMaterialSpecular[I]=inMaterialSpecular[I];outTangent[I]=inTangent[I];outPreviousClip[I]=inPreviousClip[I];
    if(I==0){
        // Outer level k is the edge opposite corner k.
        gl_TessLevelOuter[0]=edgeLevel(inPosition[1],inPosition[2]);gl_TessLevelOuter[1]=edgeLevel(inPosition[2],inPosition[0]);gl_TessLevelOuter[2]=edgeLevel(inPosition[0],inPosition[1]);
        gl_TessLevelInner[0]=max(gl_TessLevelOuter[0],max(gl_TessLevelOuter[1],gl_TessLevelOuter[2]));
    }
}
