#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(set=0,binding=1) uniform sampler2DArray original;
layout(location=0) out vec4 color;
void main(){vec4 c=texelFetch(original,ivec3(ivec2(gl_FragCoord.xy),layer),level);if(convertAlpha){ivec3 rgb=ivec3(c.rgb*255+.5)>>4;int a=min(15,int(float(rgb.r+rgb.g+rgb.b)*(24.0/45.0)));c=vec4(vec3(rgb)/15,float(a)/15);}color=c;}
