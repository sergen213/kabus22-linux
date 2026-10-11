#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) in vec2 uv;
layout(location=1) in vec4 vertexTint;
layout(location=2) flat in vec4 bounds;
layout(set=0,binding=1) uniform sampler2D image;
layout(location=0) out vec4 color;
// UI bitmaps are sub-rects of shared atlas textures: sample clamped to the sub-rect's edge texel centres, as if it were its own
// CLAMP_TO_EDGE texture. Otherwise MSAA edge pixels (evaluated at the pixel centre outside the quad), edge ties and linear
// FILTER reach the neighbouring atlas texels (the white separator rows of Enerji_MenuBar.tga/Enerji_Kalp.tga).
void main(){vec2 h=.5/vec2(textureSize(image,0)),lo=bounds.xy+h;vec4 c=texture(image,clamp(uv,lo,max(lo,bounds.zw-h)));float brightness=max(max(c.r,c.g),c.b);if((textKey&&brightness<0.001)||(uiOverlay&&((!cursorKey&&brightness<0.001)||c.a<alphaRef)))discard;color=c*vertexTint;}
