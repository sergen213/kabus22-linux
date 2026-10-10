// Wave-2 effect math shared by the OpenGL 3.3 shaders (embedded at configure time) and the Vulkan shaders.
// Conventions: matrices are GL clip space (z in [-1,1]); depth samples are window depth in [0,1];
// "linear depth" is the positive view distance along the camera forward axis. Colors are the game's
// gamma-space values (HDR when PBR is on). K22_FLIP_Y=1 (Vulkan) means texture row 0 is NDC +Y.
#ifndef K22_EFFECTS_GLSL
#define K22_EFFECTS_GLSL
#ifndef K22_FLIP_Y
#define K22_FLIP_Y 0
#endif
float k22Luma(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}
vec2 k22NdcFromUv(vec2 uv){vec2 n=uv*2.0-1.0;
#if K22_FLIP_Y
    n.y=-n.y;
#endif
    return n;}
vec2 k22UvFromNdc(vec2 n){
#if K22_FLIP_Y
    n.y=-n.y;
#endif
    return n*0.5+0.5;}
float k22LinearDepth(float depth,float n,float f){float z=depth*2.0-1.0;return 2.0*n*f/(f+n-z*(f-n));}
// World-space ray through a pixel, scaled so its component along the camera forward axis is 1.
vec3 k22ViewRay(vec2 ndc,mat4 inverseViewProjection,vec3 eye,vec3 forward){vec4 p=inverseViewProjection*vec4(ndc,1.0,1.0);vec3 r=p.xyz/p.w-eye;return r/dot(r,forward);}
// Jimenez interleaved gradient noise in [0,1).
float k22Noise(vec2 pixel){return fract(52.9829189*fract(dot(pixel,vec2(0.06711056,0.00583715))));}
// ---- SSAO: hemisphere samples around the depth-reconstructed normal; returns ambient visibility in [0,1]. ----
const int k22AoSamples=12;const float k22AoRadius=20.0,k22AoStrength=0.65;
float k22Ssao(sampler2D linearDepth,vec2 uv,vec2 texel,mat4 inverseViewProjection,mat4 vp,vec3 eye,vec3 forward,float noise){
    float d=texture(linearDepth,uv).r;if(d<=0.0)return 1.0;
    vec3 p=eye+k22ViewRay(k22NdcFromUv(uv),inverseViewProjection,eye,forward)*d;
    vec3 px=eye+k22ViewRay(k22NdcFromUv(uv+vec2(texel.x,0)),inverseViewProjection,eye,forward)*texture(linearDepth,uv+vec2(texel.x,0)).r;
    vec3 nx=eye+k22ViewRay(k22NdcFromUv(uv-vec2(texel.x,0)),inverseViewProjection,eye,forward)*texture(linearDepth,uv-vec2(texel.x,0)).r;
    vec3 py=eye+k22ViewRay(k22NdcFromUv(uv+vec2(0,texel.y)),inverseViewProjection,eye,forward)*texture(linearDepth,uv+vec2(0,texel.y)).r;
    vec3 ny=eye+k22ViewRay(k22NdcFromUv(uv-vec2(0,texel.y)),inverseViewProjection,eye,forward)*texture(linearDepth,uv-vec2(0,texel.y)).r;
    vec3 dx=abs(px.x+px.y+px.z-p.x-p.y-p.z)<abs(nx.x+nx.y+nx.z-p.x-p.y-p.z)?px-p:p-nx,dy=abs(py.x+py.y+py.z-p.x-p.y-p.z)<abs(ny.x+ny.y+ny.z-p.x-p.y-p.z)?py-p:p-ny;
    vec3 n=cross(dx,dy);if(dot(n,n)<1e-12)return 1.0;n=normalize(n);if(dot(n,eye-p)<0.0)n=-n;
    vec3 t=normalize(abs(n.z)<0.9?cross(n,vec3(0,0,1)):cross(n,vec3(1,0,0))),b=cross(n,t);
    float occlusion=0.0,angle=noise*6.2831853;
    for(int i=0;i<k22AoSamples;++i){
        float s=(float(i)+0.5)/float(k22AoSamples),r=k22AoRadius*mix(0.1,1.0,s*s),a=angle+float(i)*2.3999632;
        float z=sqrt(1.0-s),xy=sqrt(s);vec3 q=p+(t*cos(a)*xy+b*sin(a)*xy+n*z)*r+n*0.5;
        vec4 c=vp*vec4(q,1.0);if(c.w<=0.0)continue;vec2 sampleUv=k22UvFromNdc(c.xy/c.w);
        if(any(lessThan(sampleUv,vec2(0)))||any(greaterThan(sampleUv,vec2(1))))continue;
        float sceneDepth=texture(linearDepth,sampleUv).r,sampleDepth=dot(q-eye,forward);
        if(sceneDepth>0.0&&sceneDepth<sampleDepth-0.5)occlusion+=clamp(k22AoRadius/abs(d-sceneDepth),0.0,1.0);
    }
    return 1.0-k22AoStrength*occlusion/float(k22AoSamples);
}
// ---- Bloom: soft-knee threshold in display (gamma) space, conservative additive strength. ----
const float k22BloomThreshold=0.7,k22BloomKnee=0.2,k22BloomStrength=0.15;
vec3 k22BloomPrefilter(vec3 c){float b=max(c.r,max(c.g,c.b)),s=clamp(b-k22BloomThreshold+k22BloomKnee,0.0,2.0*k22BloomKnee);s=s*s/(4.0*k22BloomKnee);return c*max(s,b-k22BloomThreshold)/max(b,1e-4);}
vec3 k22BloomDown(sampler2D image,vec2 uv,vec2 texel){return 0.25*(texture(image,uv+texel*vec2(-1,-1)).rgb+texture(image,uv+texel*vec2(1,-1)).rgb+texture(image,uv+texel*vec2(-1,1)).rgb+texture(image,uv+texel*vec2(1,1)).rgb);}
vec3 k22BloomUp(sampler2D image,vec2 uv,vec2 texel){
    vec3 s=4.0*texture(image,uv).rgb;
    s+=2.0*(texture(image,uv+vec2(texel.x,0)).rgb+texture(image,uv-vec2(texel.x,0)).rgb+texture(image,uv+vec2(0,texel.y)).rgb+texture(image,uv-vec2(0,texel.y)).rgb);
    s+=texture(image,uv+texel).rgb+texture(image,uv-texel).rgb+texture(image,uv+vec2(texel.x,-texel.y)).rgb+texture(image,uv+vec2(-texel.x,texel.y)).rgb;
    return s/16.0;
}
// ---- FXAA (Lottes 3.11 quality-style edge search, 12 steps) on display-space LDR color. ----
vec3 k22Fxaa(sampler2D image,vec2 uv,vec2 texel){
    vec3 rgbM=texture(image,uv).rgb;float lM=k22Luma(rgbM);
    float lN=k22Luma(texture(image,uv+vec2(0,-texel.y)).rgb),lS=k22Luma(texture(image,uv+vec2(0,texel.y)).rgb),lE=k22Luma(texture(image,uv+vec2(texel.x,0)).rgb),lW=k22Luma(texture(image,uv+vec2(-texel.x,0)).rgb);
    float lMax=max(lM,max(max(lN,lS),max(lE,lW))),lMin=min(lM,min(min(lN,lS),min(lE,lW))),range=lMax-lMin;
    if(range<max(0.0312,lMax*0.125))return rgbM;
    float lNW=k22Luma(texture(image,uv+vec2(-texel.x,-texel.y)).rgb),lNE=k22Luma(texture(image,uv+vec2(texel.x,-texel.y)).rgb),lSW=k22Luma(texture(image,uv+vec2(-texel.x,texel.y)).rgb),lSE=k22Luma(texture(image,uv+texel).rgb);
    float horizontal=abs(lNW+lNE-2.0*lN)+2.0*abs(lW+lE-2.0*lM)+abs(lSW+lSE-2.0*lS),vertical=abs(lNW+lSW-2.0*lW)+2.0*abs(lN+lS-2.0*lM)+abs(lNE+lSE-2.0*lE);
    bool isHorizontal=horizontal>=vertical;float l1=isHorizontal?lN:lW,l2=isHorizontal?lS:lE,g1=abs(l1-lM),g2=abs(l2-lM);
    float stepLen=isHorizontal?texel.y:texel.x;float lLocal;float gScaled;
    if(g1>=g2){stepLen=-stepLen;lLocal=0.5*(l1+lM);gScaled=0.25*g1;}else{lLocal=0.5*(l2+lM);gScaled=0.25*g2;}
    vec2 edge=uv;if(isHorizontal)edge.y+=0.5*stepLen;else edge.x+=0.5*stepLen;
    vec2 dir=isHorizontal?vec2(texel.x,0):vec2(0,texel.y);
    vec2 p1=edge-dir,p2=edge+dir;float e1=k22Luma(texture(image,p1).rgb)-lLocal,e2=k22Luma(texture(image,p2).rgb)-lLocal;bool r1=abs(e1)>=gScaled,r2=abs(e2)>=gScaled;
    for(int i=0;i<12&&!(r1&&r2);++i){float q=i<4?1.0:i<8?1.5:2.0;if(!r1){p1-=dir*q;e1=k22Luma(texture(image,p1).rgb)-lLocal;r1=abs(e1)>=gScaled;}if(!r2){p2+=dir*q;e2=k22Luma(texture(image,p2).rgb)-lLocal;r2=abs(e2)>=gScaled;}}
    float d1=isHorizontal?uv.x-p1.x:uv.y-p1.y,d2=isHorizontal?p2.x-uv.x:p2.y-uv.y;bool nearer1=d1<d2;float dMin=min(d1,d2),span=d1+d2;
    bool correct=((nearer1?e1:e2)<0.0)!=(lM-lLocal<0.0);float offset=correct?0.5-dMin/span:0.0;
    float average=(2.0*(lN+lS+lE+lW)+lNW+lNE+lSW+lSE)/12.0,sub=clamp(abs(average-lM)/range,0.0,1.0);sub=(-2.0*sub+3.0)*sub*sub;offset=max(offset,sub*sub*0.75);
    vec2 target=uv;if(isHorizontal)target.y+=offset*stepLen;else target.x+=offset*stepLen;return texture(image,target).rgb;
}
// ---- Shadow maps (raster fallback). Sun: orthographic depth map, 3x3 PCF. Point/spot: GL-convention depth cube
// whose 90-degree faces share one perspective, so the reference depth follows from the major-axis distance.
float k22SunShadow(sampler2DShadow map,mat4 matrix,vec3 p,vec3 n,float texelWorld){
    vec4 c=matrix*vec4(p+n*(1.5*texelWorld),1.0);vec3 q=c.xyz/c.w;vec2 uv=k22UvFromNdc(q.xy);
    if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1)))||q.z>1.0)return 1.0;
    vec2 texel=1.0/vec2(textureSize(map,0));float ref=q.z*0.5+0.5-0.0005,sum=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)sum+=texture(map,vec3(uv+vec2(x,y)*texel,ref));
    return sum/9.0;
}
// Cube shadow near plane (render_policy::cubeShadowNear): fixtures around a light do not occlude it.
const float k22CubeShadowNear=16.0;
float k22CubeShadow(samplerCubeShadow map,vec3 light,vec3 p,vec3 n,float nearPlane,float farPlane){
    vec3 d=p-light;float distance=length(d);if(distance<=nearPlane||distance>=farPlane)return 1.0;
    float texelWorld=2.0*distance/float(textureSize(map,0).x);d=p+n*(2.0*texelWorld)-light;
    vec3 t=normalize(abs(d.z)<0.9*length(d)?cross(d,vec3(0,0,1)):cross(d,vec3(1,0,0))),b=normalize(cross(d,t));
    float sum=0.0;
    for(int i=0;i<4;++i){vec3 s=d+(t*(i==0?1.0:i==1?-1.0:0.0)+b*(i==2?1.0:i==3?-1.0:0.0))*texelWorld;vec3 a=abs(s);float z=max(a.x,max(a.y,a.z));
        float ref=((farPlane+nearPlane)/(farPlane-nearPlane)-2.0*farPlane*nearPlane/((farPlane-nearPlane)*z))*0.5+0.5;sum+=texture(map,vec4(s,ref-0.0002));}
    return sum*0.25;
}
// ---- Volumetric fog: single-scattering of the scene lights along the view ray (Henyey-Greenstein g=0.25).
// The medium follows the level fog (extinction 3/(end-start) beyond fog start) or, when the level has no fog,
// a faint haze. Only in-scattered light is added: the original linear fog blend stays untouched.
const int k22FogSteps=24;const float k22FogHaze=0.00015,k22FogMaxDistance=3000.0,k22FogG=0.25,k22FogHalo=0.1;
float k22FogDensity(float distance,vec2 fogSpan){return fogSpan.y>fogSpan.x?(distance>fogSpan.x?3.0/(fogSpan.y-fogSpan.x):0.0):k22FogHaze;}
// Normalized so an isotropic medium scatters with weight 1.
float k22Phase(float cosine){float g2=k22FogG*k22FogG;return (1.0-g2)/pow(1.0+g2-2.0*k22FogG*cosine,1.5);}
// Includer defines K22_LIGHT(i), int lightCount and K22_FOG_VISIBILITY(i,x) (1.0 when unshadowed), as for pbr.glsl.
// Scattered radiance uses the light's own falloff times a halo term 1/(1+(d/(0.15 range))^2) and a (1-d/range)^2 window:
// native lights are constant inside their range, which alone would turn the haze into a uniform veil.
#ifdef K22_FOG_VISIBILITY
vec3 k22FogInscatter(vec3 eye,vec3 surface,vec2 fogSpan,float noise){
    vec3 ray=surface-eye;float total=min(length(ray),k22FogMaxDistance);if(total<=0.0)return vec3(0);vec3 dir=normalize(ray);
    float ds=total/float(k22FogSteps),transmittance=1.0;vec3 result=vec3(0);
    for(int s=0;s<k22FogSteps;++s){
        float t=(float(s)+noise)*ds,sigma=k22FogDensity(t,fogSpan);if(sigma<=0.0)continue;vec3 x=eye+dir*t;vec3 radiance=vec3(0);
        for(int i=0;i<lightCount;++i){
            vec4 a=K22_LIGHT(i*5),b=K22_LIGHT(i*5+1),d=K22_LIGHT(i*5+2),e=K22_LIGHT(i*5+3),c=K22_LIGHT(i*5+4);
            if(c.z>0.5||d.w==3.0)continue;
            vec3 delta=a.xyz-x;float distance=length(delta);if(distance>a.w)continue;vec3 L=delta/max(distance,0.0001);
            float window=max(1.0-distance/max(a.w,0.0001),0.0),halo=distance/max(k22FogHalo*a.w,0.0001),attenuation=window*window/(1.0+halo*halo)*(b.a<0.0?1.0:1.0/max(e.x+e.y*distance+e.z*distance*distance,0.0001));
            if(d.w==2.0){float rho=dot(-L,d.xyz);attenuation*=rho>=c.x?1.0:rho<=c.y?0.0:pow((rho-c.y)/max(c.x-c.y,0.0001),e.w);}
            if(attenuation>0.0)radiance+=clamp(b.rgb*abs(b.a)*attenuation,0.0,1.0)*k22Phase(dot(dir,-L))*K22_FOG_VISIBILITY(i,x);
        }
        result+=transmittance*sigma*ds*radiance;transmittance*=exp(-sigma*ds);
    }
    return result;
}
#endif
#endif
