#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D image;
layout(set=0,binding=2) uniform sampler2D ramp;
void main(){vec4 c=texture(image,uv);if(enabled){ivec3 index=ivec3(clamp(c.rgb,0,1)*255+0.5);c.rgb=vec3(texelFetch(ramp,ivec2(index.r,0),0).r,texelFetch(ramp,ivec2(index.g,1),0).r,texelFetch(ramp,ivec2(index.b,2),0).r);}color=c;}
