#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(set=0,binding=1) uniform sampler2D original;
layout(set=0,binding=6,std430) readonly buffer Offsets { int offsetData[]; };
layout(location=0) out vec4 color;
void main(){ivec2 size=imageSize,pixel=ivec2(gl_FragCoord.xy);int index=pixel.y*size.x+pixel.x,i=index/size.y,j=index%size.y;int sx=(offsetData[j]+i)&(size.x-1),sy=(offsetData[i]+j)&(size.y-1);int source=sx+sy*stride;color=texelFetch(original,ivec2(source%size.x,source/size.x),0);}
