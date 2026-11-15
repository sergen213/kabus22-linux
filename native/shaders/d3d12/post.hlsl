// HLSL ports of the OpenGL fullscreen programs (render_gl.cpp postVS, postFS, warpFS, skyFS, cloudFS) plus the mip-level
// builder that replaces glGenerateMipmap. presentVS draws the final pass into the swap chain without the row flip.
#include "common.hlsli"
K22PostVaryings postVS(uint id:SV_VertexID){float2 p=float2((id<<1)&2,id&2);K22PostVaryings o;o.uv=p;o.position=k22Clip(float4(p*2-1,0,1));return o;}
K22PostVaryings presentVS(uint id:SV_VertexID){float2 p=float2((id<<1)&2,id&2);K22PostVaryings o;o.uv=p;o.position=float4(p*2-1,0.5,1);return o;}
#if defined(K22_POST)
cbuffer Post:register(b0){int enabled;int3 postPadding;};
Texture2D image:register(t0);Texture2D<float> ramp:register(t1);
float4 postPS(K22PostVaryings v):SV_Target0{float4 c=image.Sample(K22_SAMPLER(0),v.uv);if(enabled){int3 index=int3(clamp(c.rgb,0,1)*255+0.5);c.rgb=float3(ramp.Load(int3(index.r,0,0)),ramp.Load(int3(index.g,1,0)),ramp.Load(int3(index.b,2,0)));}return c;}
#elif defined(K22_WARP)
cbuffer Warp:register(b0){int2 size;int stride;int warpPadding;};
Texture2D original:register(t0);StructuredBuffer<int> offsets:register(t1);
float4 warpPS(K22PostVaryings v):SV_Target0{int2 pixel=int2(v.position.xy);int index=pixel.y*size.x+pixel.x,i=index/size.y,j=index%size.y;int sx=(offsets[j]+i)&(size.x-1),sy=(offsets[i]+j)&(size.y-1);int source=sx+sy*stride;return original.Load(int3(source%size.x,source/size.x,0));}
#elif defined(K22_SKY)
cbuffer Sky:register(b0){float4 basis0,basis1,basis2;float2 cone,scroll;float curve,clipValue;int cube,blend;int layer;int3 skyPadding;};
Texture2DArray image:register(t0);
float4 skyPS(K22PostVaryings v):SV_Target0{
    float2 screen=float2(v.uv.x-.5,.5-v.uv.y);float3 local=float3(1,-screen.x*cone.x,-screen.y*cone.y);float3 ray=basis0.xyz*local.x+basis1.xyz*local.y+basis2.xyz*local.z;float2 coord;int face=layer;float fade=1;
    if(cube){
        float3 a=abs(ray);float m=max(max(a.x,a.y),a.z);
        if(a.x>=a.y&&a.x>=a.z){face=ray.x<0?0:2;coord=float2(ray.x<0?ray.y:-ray.y,-ray.z)/m;}
        else if(a.y>=a.z){face=ray.y>0?1:3;coord=float2(ray.y>0?ray.x:-ray.x,-ray.z)/m;}
        else{face=ray.z<0?4:5;coord=float2(ray.x,ray.z<0?-ray.y:ray.y)/m;}
        coord=coord*.5+.5;
    }else{
        ray.z*=curve;float magnitude=length(ray);float3 n=magnitude>0?ray/magnitude:float3(0,0,0);if(n.z<clipValue)discard;
        coord=(curve*.5)*n.xy+scroll;if(blend)fade=clamp(n.z-clipValue,0,1);
    }
    float4 color=image.Sample(K22_SAMPLER(0),float3(coord,face));color.a*=floor(fade*255)/255;return color;
}
#elif defined(K22_CLOUD)
cbuffer Cloud:register(b0){int level;int layer;int convertAlpha;int cloudPadding;};
Texture2DArray original:register(t0);
float4 cloudPS(K22PostVaryings v):SV_Target0{float4 c=original.Load(int4(int2(v.position.xy),layer,level));if(convertAlpha){int3 rgb=int3(c.rgb*255+.5)>>4;int a=min(15,int(float(rgb.r+rgb.g+rgb.b)*(24.0/45.0)));c=float4(float3(rgb)/15,float(a)/15);}return c;}
#elif defined(K22_MIP)
// One mip level from the previous one (single-level, single-slice view): the area-weighted box over the destination texel's
// source footprint (the 2x2 box for even sizes, up to 3x3 weighted taps for odd ones), closest to NVIDIA/AMD glGenerateMipmap.
cbuffer Mip:register(b0){float2 inverseSize;float2 mipPadding;};
Texture2DArray source:register(t0);
float4 mipPS(K22PostVaryings v):SV_Target0{
    uint w,h,n;source.GetDimensions(w,h,n);const float2 ratio=float2(w,h)*inverseSize,start=floor(v.position.xy)*ratio,end=start+ratio;float4 sum=0;
    for(int y=0;y<3;++y){const float y0=floor(start.y)+y,wy=min(end.y,y0+1)-max(start.y,y0);if(wy<=0)continue;
        for(int x=0;x<3;++x){const float x0=floor(start.x)+x,wx=min(end.x,x0+1)-max(start.x,x0);if(wx<=0)continue;sum+=wx*wy*source.Load(int4(min(int(x0),int(w)-1),min(int(y0),int(h)-1),0,0));}}
    return sum/(ratio.x*ratio.y);
}
#endif
