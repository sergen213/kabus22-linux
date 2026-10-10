#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
// Vulkan fullscreen effect passes (mode = u.dataPass.x); OpenGL runs the same effects.glsl functions.
layout(set=0,binding=5,std430) readonly buffer Lights { vec4 lightData[]; };
layout(set=0,binding=1) uniform sampler2D inputA;
layout(set=0,binding=2) uniform sampler2D inputB;
layout(set=0,binding=3) uniform sampler2D inputC;
layout(set=0,binding=4) uniform sampler2D inputD;
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
    if(mode==4){float d=texture(inputA,uv).r;vec3 eye=cameraPosition;vec3 p=eye+k22ViewRay(k22NdcFromUv(uv),u.dataInverseViewProjection,eye,forwardAxis())*d;color=vec4(k22FogInscatter(eye,p,u.dataRange.yz,k22Noise(gl_FragCoord.xy+u.dataPass.y)),d);return;}
    if(mode==14){
        // Half-resolution fog (mode 4, alpha = its sample's linear depth): bilinear weights damped by relative depth
        // difference so fog does not bleed across silhouettes; the closest-depth texel when every neighbour differs.
        vec2 size=vec2(textureSize(inputA,0)),p=uv*size-0.5,f=fract(p);ivec2 b=ivec2(floor(p)),last=ivec2(size)-1;float d=texture(inputB,uv).r;
        vec3 sum=vec3(0),best=vec3(0);float weight=0.0,closest=1e30;
        for(int i=0;i<4;++i){ivec2 o=ivec2(i&1,i>>1);vec4 s=texelFetch(inputA,clamp(b+o,ivec2(0),last),0);float dz=abs(s.a-d);
            float w=(o.x==1?f.x:1.0-f.x)*(o.y==1?f.y:1.0-f.y)*exp(-dz/(0.05*max(d,1e-3)));sum+=w*s.rgb;weight+=w;if(dz<closest){closest=dz;best=s.rgb;}}
        color=vec4(weight>1e-4?sum/weight:best,0);return;
    }
    if(mode==5){color=vec4(k22BloomPrefilter(k22BloomDown(inputA,uv,texel)),1);return;}
    if(mode==6){color=vec4(k22BloomDown(inputA,uv,texel),1);return;}
    if(mode==7){color=vec4(k22BloomUp(inputA,uv,texel),1);return;}
    if(mode==8){vec4 c=texture(inputA,uv);if(u.dataRange.w>0.0)c.rgb+=u.dataRange.w*texture(inputB,uv).rgb;color=vec4(k22PbrOn?k22ToneMap(c.rgb):c.rgb,c.a);return;}
    if(mode==9){
        // TAA (render_policy::motion_visible/variance_clip): moving entities reproject with their motion vectors (nearest
        // motion sample of the 3x3 neighborhood unless the scene depth hides it), the rest through the depth captured
        // before view-space entities; the history is clipped to the neighborhood's YCoCg mean +- 1.25 sigma.
        const mat3 toYCoCg=mat3(0.25,0.5,-0.25,0.5,0.0,0.5,0.25,-0.5,-0.25),fromYCoCg=mat3(1,1,1,1,0,-1,-1,1,-1);
        vec3 current=toYCoCg*texture(inputA,uv).rgb,m1=vec3(0),m2=vec3(0);vec4 motion=vec4(0);
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){vec2 o=uv+vec2(x,y)*texel;vec3 s=toYCoCg*texture(inputA,o).rgb;m1+=s;m2+=s*s;vec4 v=texture(inputD,o);if(v.z>0.0&&(motion.z==0.0||v.z<motion.z))motion=v;}
        float d=texture(inputC,uv).r;vec2 puv=vec2(-1);
        if(motion.z>0.0&&motion.z<=d*1.03){if(motion.w>0.0)puv=uv+motion.xy;}
        else{vec3 p=cameraPosition+k22ViewRay(k22NdcFromUv(uv),u.dataInverseViewProjection,cameraPosition,forwardAxis())*d;vec4 previous=u.dataPreviousViewProjection*vec4(p,1);if(previous.w>0.0)puv=k22UvFromNdc(previous.xy/previous.w);}
        if(u.dataRange.w==0.0||any(lessThan(puv,vec2(0)))||any(greaterThan(puv,vec2(1)))){color=vec4(fromYCoCg*current,1);return;}
        vec3 mean=m1/9.0,sigma=sqrt(max(m2/9.0-mean*mean,0.0)),history=toYCoCg*texture(inputB,puv).rgb-mean;
        vec3 e=abs(history)/(sigma*1.25+1e-4);float m=max(e.x,max(e.y,e.z));if(m>1.0)history/=m;
        color=vec4(fromYCoCg*mix(mean+history,current,0.1),1);return;
    }
    if(mode==10){color=vec4(k22Fxaa(inputA,uv,texel),1);return;}
    if(mode==13){
        // FSR motion: unjittered previous-minus-current UV. Entity vectors (scene pass 3, previous unjittered minus this
        // jittered pixel) where the scene depth agrees, as in TAA; the camera reprojection of the depth elsewhere.
        float d=texture(inputA,uv).r;vec3 p=cameraPosition+k22ViewRay(k22NdcFromUv(uv),u.dataInverseViewProjection,cameraPosition,forwardAxis())*d;
        vec4 c=u.dataViewProjection*vec4(p,1),q=u.dataPreviousViewProjection*vec4(p,1),m=texture(inputB,uv);vec2 current=k22UvFromNdc(c.xy/c.w);
        color=vec4(m.z>0.0&&m.z<=d*1.03?m.xy+uv-current:q.w>0.0?k22UvFromNdc(q.xy/q.w)-current:vec2(0),0,1);return;
    }
    if(mode==12){
        // sharpen: AMD FidelityFX CAS (contrast-adaptive sharpening) on the final LDR image; sharpness 0.4 (0..1).
        // Per-channel amount shrinks where the 3x3 cross is already contrasty or near clipping, so no halos.
        vec4 e=texture(inputA,uv);
        vec3 b=texture(inputA,uv+vec2(0,-1)*texel).rgb,d=texture(inputA,uv+vec2(-1,0)*texel).rgb,f=texture(inputA,uv+vec2(1,0)*texel).rgb,h=texture(inputA,uv+vec2(0,1)*texel).rgb;
        vec3 a=texture(inputA,uv+vec2(-1,-1)*texel).rgb,c=texture(inputA,uv+vec2(1,-1)*texel).rgb,g=texture(inputA,uv+vec2(-1,1)*texel).rgb,i=texture(inputA,uv+vec2(1,1)*texel).rgb;
        a=clamp(a,0.0,1.0);b=clamp(b,0.0,1.0);c=clamp(c,0.0,1.0);d=clamp(d,0.0,1.0);f=clamp(f,0.0,1.0);g=clamp(g,0.0,1.0);h=clamp(h,0.0,1.0);i=clamp(i,0.0,1.0);vec3 m=clamp(e.rgb,0.0,1.0);
        vec3 lo=min(min(min(d,m),min(f,b)),h),hi=max(max(max(d,m),max(f,b)),h);
        lo+=min(lo,min(min(a,c),min(g,i)));hi+=max(hi,max(max(a,c),max(g,i)));
        vec3 amp=sqrt(clamp(min(lo,2.0-hi)/max(hi,vec3(1e-5)),0.0,1.0));
        vec3 w=amp*(-1.0/mix(8.0,5.0,0.4));
        color=vec4(clamp((b*w+d*w+f*w+h*w+m)/(1.0+4.0*w),0.0,1.0),e.a);return;
    }
    color=texture(inputA,uv);
}
