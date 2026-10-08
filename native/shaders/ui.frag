#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) in vec2 uv;
layout(location=1) in vec4 vertexTint;
layout(set=0,binding=1) uniform sampler2D image;
layout(location=0) out vec4 color;
void main(){vec4 c=texture(image,uv);float brightness=max(max(c.r,c.g),c.b);if((textKey&&brightness<0.001)||(uiOverlay&&((!cursorKey&&brightness<0.001)||c.a<alphaRef)))discard;color=c*vertexTint;}
