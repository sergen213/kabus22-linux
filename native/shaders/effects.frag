#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
// Vulkan fullscreen effect passes (mode = u.dataPass.x); OpenGL runs the same effects.glsl functions.
layout(set=0,binding=5,std430) readonly buffer Lights { vec4 lightData[]; };
layout(set=0,binding=1) uniform sampler2D inputA;
layout(set=0,binding=2) uniform sampler2D inputB;
layout(set=0,binding=3) uniform sampler2D inputC;
layout(set=0,binding=10) uniform samplerCubeShadow cubeShadowMap;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
#define K22_FLIP_Y 1
#define K22_LIGHT(i) lightData[i]
#define K22_FOG_VISIBILITY(i,x) (k22ShadowMaps&&u.dataShadowLight.w>0.0&&(i)==int(u.dataEffects.w)?k22CubeShadow(cubeShadowMap,u.dataShadowLight.xyz,x,vec3(0),k22CubeShadowNear,u.dataShadowLight.w):1.0)
#include "pbr.glsl"
#include "effects.glsl"
vec3 forwardAxis(){return u.dataCamera.xyz;}
void main(){
    int mode=k22Pass;vec2 texel=u.dataPass.zw;
    if(mode==1){float d=texture(inputA,uv).r;color=vec4(d>=1.0?u.dataRange.x:k22LinearDepth(d,u.dataCamera.w,u.dataRange.x));return;}
    if(mode==2){color=vec4(k22Ssao(inputA,uv,texel,u.dataInverseViewProjection,u.dataViewProjection,cameraPosition,forwardAxis(),k22Noise(gl_FragCoord.xy+u.dataPass.y)));return;}
    if(mode==3){
        // 4x4 depth-aware blur of the AO term, applied with multiplicative blending.
        float center=texture(inputB,uv).r,sum=0.0,weight=0.0;
        for(int y=-2;y<2;++y)for(int x=-2;x<2;++x){vec2 o=(vec2(x,y)+0.5)*texel;float w=1.0/(1.0+abs(texture(inputB,uv+o).r-center)*0.25);sum+=texture(inputA,uv+o).r*w;weight+=w;}
        color=vec4(vec3(sum/weight),1);return;
    }
    if(mode==4){float d=texture(inputA,uv).r;vec3 eye=cameraPosition;vec3 p=eye+k22ViewRay(k22NdcFromUv(uv),u.dataInverseViewProjection,eye,forwardAxis())*d;color=vec4(k22FogInscatter(eye,p,u.dataRange.yz,k22Noise(gl_FragCoord.xy+u.dataPass.y)),0);return;}
    if(mode==5){color=vec4(k22BloomPrefilter(k22BloomDown(inputA,uv,texel)),1);return;}
    if(mode==6){color=vec4(k22BloomDown(inputA,uv,texel),1);return;}
    if(mode==7){color=vec4(k22BloomUp(inputA,uv,texel),1);return;}
    if(mode==8){vec4 c=texture(inputA,uv);if(u.dataRange.w>0.0)c.rgb+=u.dataRange.w*texture(inputB,uv).rgb;color=vec4(k22PbrOn?k22ToneMap(c.rgb):c.rgb,c.a);return;}
    if(mode==9){
        // TAA: reproject with the depth captured before view-space entities, YCoCg-free RGB neighborhood clamp.
        vec3 current=texture(inputA,uv).rgb,lo=current,hi=current;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){vec3 s=texture(inputA,uv+vec2(x,y)*texel).rgb;lo=min(lo,s);hi=max(hi,s);}
        float d=texture(inputC,uv).r;vec3 p=cameraPosition+k22ViewRay(k22NdcFromUv(uv),u.dataInverseViewProjection,cameraPosition,forwardAxis())*d;
        vec4 previous=u.dataPreviousViewProjection*vec4(p,1);vec2 puv=k22UvFromNdc(previous.xy/previous.w);
        if(u.dataRange.w==0.0||previous.w<=0.0||any(lessThan(puv,vec2(0)))||any(greaterThan(puv,vec2(1)))){color=vec4(current,1);return;}
        vec3 history=clamp(texture(inputB,puv).rgb,lo,hi);color=vec4(mix(history,current,0.1),1);return;
    }
    if(mode==10){color=vec4(k22Fxaa(inputA,uv,texel),1);return;}
    color=texture(inputA,uv);
}
