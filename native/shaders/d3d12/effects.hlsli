// HLSL port of native/shaders/effects.glsl (K22_FLIP_Y=0: the backend stores render targets in OpenGL row order).
// Single-level effect targets are sampled at LOD 0 (their GL samplers use LINEAR or NEAREST for both min and mag).
#ifndef K22_EFFECTS_HLSLI
#define K22_EFFECTS_HLSLI
float k22Luma(float3 c){return dot(c,float3(0.299,0.587,0.114));}
float2 k22NdcFromUv(float2 uv){return uv*2.0-1.0;}
float2 k22UvFromNdc(float2 n){return n*0.5+0.5;}
float k22LinearDepth(float depth,float n,float f){float z=depth*2.0-1.0;return 2.0*n*f/(f+n-z*(f-n));}
float3 k22ViewRay(float2 ndc,float4x4 inverseViewProjection,float3 eye,float3 forward){float4 p=mul(inverseViewProjection,float4(ndc,1.0,1.0));float3 r=p.xyz/p.w-eye;return r/dot(r,forward);}
float k22Noise(float2 pixel){return frac(52.9829189*frac(dot(pixel,float2(0.06711056,0.00583715))));}
#define K22_TEX(t,s,uv) (t).SampleLevel(s,uv,0)
static const int k22AoSamples=12;static const float k22AoRadius=20.0,k22AoStrength=0.65;
float k22Ssao(Texture2D linearDepth,SamplerState s,float2 uv,float2 texel,float4x4 inverseViewProjection,float4x4 vp,float3 eye,float3 forward,float noise){
    float d=K22_TEX(linearDepth,s,uv).r;if(d<=0.0)return 1.0;
    float3 p=eye+k22ViewRay(k22NdcFromUv(uv),inverseViewProjection,eye,forward)*d;
    float3 px=eye+k22ViewRay(k22NdcFromUv(uv+float2(texel.x,0)),inverseViewProjection,eye,forward)*K22_TEX(linearDepth,s,uv+float2(texel.x,0)).r;
    float3 nx=eye+k22ViewRay(k22NdcFromUv(uv-float2(texel.x,0)),inverseViewProjection,eye,forward)*K22_TEX(linearDepth,s,uv-float2(texel.x,0)).r;
    float3 py=eye+k22ViewRay(k22NdcFromUv(uv+float2(0,texel.y)),inverseViewProjection,eye,forward)*K22_TEX(linearDepth,s,uv+float2(0,texel.y)).r;
    float3 ny=eye+k22ViewRay(k22NdcFromUv(uv-float2(0,texel.y)),inverseViewProjection,eye,forward)*K22_TEX(linearDepth,s,uv-float2(0,texel.y)).r;
    float3 dx=abs(px.x+px.y+px.z-p.x-p.y-p.z)<abs(nx.x+nx.y+nx.z-p.x-p.y-p.z)?px-p:p-nx,dy=abs(py.x+py.y+py.z-p.x-p.y-p.z)<abs(ny.x+ny.y+ny.z-p.x-p.y-p.z)?py-p:p-ny;
    float3 n=cross(dx,dy);if(dot(n,n)<1e-12)return 1.0;n=normalize(n);if(dot(n,eye-p)<0.0)n=-n;
    float3 t=normalize(abs(n.z)<0.9?cross(n,float3(0,0,1)):cross(n,float3(1,0,0))),b=cross(n,t);
    float occlusion=0.0,angle=noise*6.2831853;
    for(int i=0;i<k22AoSamples;++i){
        float s2=(float(i)+0.5)/float(k22AoSamples),r=k22AoRadius*lerp(0.1,1.0,s2*s2),a=angle+float(i)*2.3999632;
        float z=sqrt(1.0-s2),xy=sqrt(s2);float3 q=p+(t*cos(a)*xy+b*sin(a)*xy+n*z)*r+n*0.5;
        float4 c=mul(vp,float4(q,1.0));if(c.w<=0.0)continue;float2 sampleUv=k22UvFromNdc(c.xy/c.w);
        if(any(sampleUv<0)||any(sampleUv>1))continue;
        float sceneDepth=K22_TEX(linearDepth,s,sampleUv).r,sampleDepth=dot(q-eye,forward);
        if(sceneDepth>0.0&&sceneDepth<sampleDepth-0.5)occlusion+=clamp(k22AoRadius/abs(d-sceneDepth),0.0,1.0);
    }
    return 1.0-k22AoStrength*occlusion/float(k22AoSamples);
}
static const float k22BloomThreshold=0.7,k22BloomKnee=0.2,k22BloomStrength=0.15;
float3 k22BloomPrefilter(float3 c){float b=max(c.r,max(c.g,c.b)),s=clamp(b-k22BloomThreshold+k22BloomKnee,0.0,2.0*k22BloomKnee);s=s*s/(4.0*k22BloomKnee);return c*max(s,b-k22BloomThreshold)/max(b,1e-4);}
float3 k22BloomDown(Texture2D image,SamplerState s,float2 uv,float2 texel){return 0.25*(K22_TEX(image,s,uv+texel*float2(-1,-1)).rgb+K22_TEX(image,s,uv+texel*float2(1,-1)).rgb+K22_TEX(image,s,uv+texel*float2(-1,1)).rgb+K22_TEX(image,s,uv+texel*float2(1,1)).rgb);}
float3 k22BloomUp(Texture2D image,SamplerState s,float2 uv,float2 texel){
    float3 r=4.0*K22_TEX(image,s,uv).rgb;
    r+=2.0*(K22_TEX(image,s,uv+float2(texel.x,0)).rgb+K22_TEX(image,s,uv-float2(texel.x,0)).rgb+K22_TEX(image,s,uv+float2(0,texel.y)).rgb+K22_TEX(image,s,uv-float2(0,texel.y)).rgb);
    r+=K22_TEX(image,s,uv+texel).rgb+K22_TEX(image,s,uv-texel).rgb+K22_TEX(image,s,uv+float2(texel.x,-texel.y)).rgb+K22_TEX(image,s,uv+float2(-texel.x,texel.y)).rgb;
    return r/16.0;
}
float3 k22Fxaa(Texture2D image,SamplerState s,float2 uv,float2 texel){
    float3 rgbM=K22_TEX(image,s,uv).rgb;float lM=k22Luma(rgbM);
    float lN=k22Luma(K22_TEX(image,s,uv+float2(0,-texel.y)).rgb),lS=k22Luma(K22_TEX(image,s,uv+float2(0,texel.y)).rgb),lE=k22Luma(K22_TEX(image,s,uv+float2(texel.x,0)).rgb),lW=k22Luma(K22_TEX(image,s,uv+float2(-texel.x,0)).rgb);
    float lMax=max(lM,max(max(lN,lS),max(lE,lW))),lMin=min(lM,min(min(lN,lS),min(lE,lW))),range=lMax-lMin;
    if(range<max(0.0312,lMax*0.125))return rgbM;
    float lNW=k22Luma(K22_TEX(image,s,uv+float2(-texel.x,-texel.y)).rgb),lNE=k22Luma(K22_TEX(image,s,uv+float2(texel.x,-texel.y)).rgb),lSW=k22Luma(K22_TEX(image,s,uv+float2(-texel.x,texel.y)).rgb),lSE=k22Luma(K22_TEX(image,s,uv+texel).rgb);
    float horizontal=abs(lNW+lNE-2.0*lN)+2.0*abs(lW+lE-2.0*lM)+abs(lSW+lSE-2.0*lS),vertical=abs(lNW+lSW-2.0*lW)+2.0*abs(lN+lS-2.0*lM)+abs(lNE+lSE-2.0*lE);
    bool isHorizontal=horizontal>=vertical;float l1=isHorizontal?lN:lW,l2=isHorizontal?lS:lE,g1=abs(l1-lM),g2=abs(l2-lM);
    float stepLen=isHorizontal?texel.y:texel.x;float lLocal;float gScaled;
    if(g1>=g2){stepLen=-stepLen;lLocal=0.5*(l1+lM);gScaled=0.25*g1;}else{lLocal=0.5*(l2+lM);gScaled=0.25*g2;}
    float2 edge=uv;if(isHorizontal)edge.y+=0.5*stepLen;else edge.x+=0.5*stepLen;
    float2 dir=isHorizontal?float2(texel.x,0):float2(0,texel.y);
    float2 p1=edge-dir,p2=edge+dir;float e1=k22Luma(K22_TEX(image,s,p1).rgb)-lLocal,e2=k22Luma(K22_TEX(image,s,p2).rgb)-lLocal;bool r1=abs(e1)>=gScaled,r2=abs(e2)>=gScaled;
    for(int i=0;i<12&&!(r1&&r2);++i){float q=i<4?1.0:i<8?1.5:2.0;if(!r1){p1-=dir*q;e1=k22Luma(K22_TEX(image,s,p1).rgb)-lLocal;r1=abs(e1)>=gScaled;}if(!r2){p2+=dir*q;e2=k22Luma(K22_TEX(image,s,p2).rgb)-lLocal;r2=abs(e2)>=gScaled;}}
    float d1=isHorizontal?uv.x-p1.x:uv.y-p1.y,d2=isHorizontal?p2.x-uv.x:p2.y-uv.y;bool nearer1=d1<d2;float dMin=min(d1,d2),span=d1+d2;
    bool correct=((nearer1?e1:e2)<0.0)!=(lM-lLocal<0.0);float offset=correct?0.5-dMin/span:0.0;
    float average=(2.0*(lN+lS+lE+lW)+lNW+lNE+lSW+lSE)/12.0,sub=clamp(abs(average-lM)/range,0.0,1.0);sub=(-2.0*sub+3.0)*sub*sub;offset=max(offset,sub*sub*0.75);
    float2 target=uv;if(isHorizontal)target.y+=offset*stepLen;else target.x+=offset*stepLen;return K22_TEX(image,s,target).rgb;
}
float k22SunShadow(Texture2D<float> map,float4x4 matrix,float3 p,float3 n,float texelWorld){
    float4 c=mul(matrix,float4(p+n*(1.5*texelWorld),1.0));float3 q=c.xyz/c.w;float2 uv=k22UvFromNdc(q.xy);
    if(any(uv<0)||any(uv>1)||q.z>1.0)return 1.0;
    uint w,h;map.GetDimensions(w,h);float2 texel=1.0/float2(w,h);float ref=q.z*0.5+0.5-0.0005,sum=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)sum+=map.SampleCmpLevelZero(k22ShadowCompare,uv+float2(x,y)*texel,ref);
    return sum/9.0;
}
static const float k22CubeShadowNear=16.0;
float k22CubeShadow(TextureCube<float> map,float3 light,float3 p,float3 n,float nearPlane,float farPlane){
    float3 d=p-light;float distance=length(d);if(distance<=nearPlane||distance>=farPlane)return 1.0;
    uint w,h;map.GetDimensions(w,h);float texelWorld=2.0*distance/float(w);d=p+n*(2.0*texelWorld)-light;
    float3 t=normalize(abs(d.z)<0.9*length(d)?cross(d,float3(0,0,1)):cross(d,float3(1,0,0))),b=normalize(cross(d,t));
    float sum=0.0;
    for(int i=0;i<4;++i){float3 s=d+(t*(i==0?1.0:i==1?-1.0:0.0)+b*(i==2?1.0:i==3?-1.0:0.0))*texelWorld;float3 a=abs(s);float z=max(a.x,max(a.y,a.z));
        float ref=((farPlane+nearPlane)/(farPlane-nearPlane)-2.0*farPlane*nearPlane/((farPlane-nearPlane)*z))*0.5+0.5;sum+=map.SampleCmpLevelZero(k22ShadowCompare,s,ref-0.0002);}
    return sum*0.25;
}
#endif
