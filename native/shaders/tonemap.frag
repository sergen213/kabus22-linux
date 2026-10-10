#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
#define K22_LIGHT(i) vec4(0)
#include "pbr.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D image;
void main(){vec4 c=texture(image,uv);color=vec4(k22ToneMap(c.rgb),c.a);}
