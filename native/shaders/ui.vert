#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 vertexColor;
layout(location=3) in vec4 texBounds;
layout(location=0) out vec2 uv;
layout(location=1) out vec4 vertexTint;
layout(location=2) flat out vec4 bounds;
void main(){uv=texcoord;vertexTint=vertexColor;bounds=texBounds;gl_Position=vec4(position/viewport*2-1,0,1);}
