#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) in vec3 position;
layout(location=0) out float planeDistance;
layout(location=1) out vec3 projectedPosition;
void main(){
    vec4 p=projection*model*vec4(position,1);
    // Keep OpenGL clip space until the geometry shader finishes source clipping.
    gl_Position=viewProjection*p;planeDistance=dot(clipPlane,p);projectedPosition=p.xyz;
}
