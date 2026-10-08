#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) in vec2 uv;
layout(set=0,binding=1) uniform sampler2DArray image;
layout(location=0) out vec4 color;
void main(){
    vec2 screen=vec2(uv.x-.5,uv.y-.5);vec3 ray=normalMatrix*vec3(1,-screen.x*cone.x,-screen.y*cone.y);vec2 coord;int face=layer;float fade=1;
    if(cube){
        vec3 a=abs(ray);float m=max(max(a.x,a.y),a.z);
        if(a.x>=a.y&&a.x>=a.z){face=ray.x<0?0:2;coord=vec2(ray.x<0?ray.y:-ray.y,-ray.z)/m;}
        else if(a.y>=a.z){face=ray.y>0?1:3;coord=vec2(ray.y>0?ray.x:-ray.x,-ray.z)/m;}
        else{face=ray.z<0?4:5;coord=vec2(ray.x,ray.z<0?-ray.y:ray.y)/m;}
        coord=coord*.5+.5;
    }else{
        ray.z*=curve;float magnitude=length(ray);vec3 n=magnitude>0?ray/magnitude:vec3(0);if(n.z<clip)discard;
        coord=(curve*.5)*n.xy+scroll;if(blend)fade=clamp(n.z-clip,0,1);
    }
    color=texture(image,vec3(coord,face));color.a*=floor(fade*255)/255;
}
