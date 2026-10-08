#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) flat in float opacity;
layout(location=1) in vec3 shadowPosition;
layout(location=0) out vec4 color;
void main(){float fog=!noFog&&fogRange.y>fogRange.x?clamp((length(shadowPosition-cameraPosition)-fogRange.x)/(fogRange.y-fogRange.x),0,1):0;color=vec4(fogColor*fog,opacity);}
