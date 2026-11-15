// HLSL port of the OpenGL planar shadow program (render_gl.cpp shadowVS/shadowGS/shadowFS). Clipping and the screen-area
// test run in GL clip space; emitted vertices are converted with k22Clip.
#include "common.hlsli"
cbuffer Shadow:register(b0){
    float4x4 model,projection,viewProjection;
    float4 clipPlane;
    float3 cameraPosition;float areaThreshold;
    float3 fogColor;float alpha;
    float2 fogRange,viewport;
    int frame0;int frame1;int vertexCount;float interpolation;
    int firstOverride;int nextOverride;int reversed;int noFog;
};
StructuredBuffer<float3> poses:register(t0);
struct ShadowInput{float3 position:POSITION;float3 nextPosition:TEXCOORD2;uint vertexID:SV_VertexID;};
struct ShadowVertex{float4 position:CLIPPOS;float planeDistance:PLANE;float3 projectedPosition:PROJECTED;};
struct ShadowVaryings{float4 position:SV_Position;nointerpolation float opacity:OPACITY;float3 shadowPosition:SHADOWPOS;};
ShadowVertex shadowVS(ShadowInput v){
    float3 p0=v.position,p1=v.position;
    if(vertexCount>0){p0=poses[frame0*vertexCount*3+int(v.vertexID)];p1=poses[frame1*vertexCount*3+int(v.vertexID)];}
    if(firstOverride)p0=v.position;if(nextOverride)p1=v.nextPosition;
    float4 p=mul(projection,mul(model,float4(lerp(p0,p1,interpolation),1)));
    ShadowVertex o;o.position=mul(viewProjection,p);o.planeDistance=dot(clipPlane,p);o.projectedPosition=p.xyz;return o;
}
float distanceToPlane(float4 p,int plane){
    if(plane==0)return p.z+p.w;
    if(plane==1)return p.x+p.w;if(plane==2)return p.w-p.x;
    if(plane==3)return p.y+p.w;return p.w-p.y;
}
[maxvertexcount(42)]
void shadowGS(triangle ShadowVertex input[3],inout TriangleStream<ShadowVaryings> stream){
    float4 polygon[10],scratch[10];float pd[10],sd[10];float3 positions[10],sp[10];int count=3;
    for(int i=0;i<3;++i){polygon[i]=input[i].position;pd[i]=input[i].planeDistance;positions[i]=input[i].projectedPosition;}
    for(int plane=0;plane<6&&count>0;++plane){
        int outputCount=0;
        for(int i=0;i<count;++i){
            int j=(i+count-1)%count;float a=plane==5?pd[j]:distanceToPlane(polygon[j],plane),b=plane==5?pd[i]:distanceToPlane(polygon[i],plane);
            if((a>=0)!=(b>=0)){float t=a/(a-b);scratch[outputCount]=lerp(polygon[j],polygon[i],t);sd[outputCount]=lerp(pd[j],pd[i],t);sp[outputCount++]=lerp(positions[j],positions[i],t);}
            if(b>=0){scratch[outputCount]=polygon[i];sd[outputCount]=pd[i];sp[outputCount++]=positions[i];}
        }
        count=outputCount;for(int k=0;k<count;++k){polygon[k]=scratch[k];pd[k]=sd[k];positions[k]=sp[k];}
    }
    for(int i=1;i+1<count;++i){
        float4 triangleVertices[3]={polygon[0],polygon[i],polygon[i+1]};
        float2 p[3];for(int j=0;j<3;++j)p[j]=triangleVertices[j].xy/triangleVertices[j].w*viewport*.5;
        float area=-((p[0].y-p[1].y)*(p[0].x-p[2].x)-(p[0].x-p[1].x)*(p[0].y-p[2].y));
        if(reversed?area>areaThreshold:area<areaThreshold)continue;
        for(int duplicate=0;duplicate<2;++duplicate){
            for(int j=0;j<3;++j){ShadowVaryings o;float4 position=triangleVertices[j];if(duplicate!=0)position.z-=0.0009765625*position.w;o.position=k22Clip(position);o.opacity=duplicate==0?alpha:0;o.shadowPosition=positions[j==0?0:j==1?i:i+1];stream.Append(o);}
            stream.RestartStrip();
        }
    }
}
float4 shadowPS(ShadowVaryings v):SV_Target0{float fog=!noFog&&fogRange.y>fogRange.x?clamp((length(v.shadowPosition-cameraPosition)-fogRange.x)/(fogRange.y-fogRange.x),0,1):0;return float4(fogColor*fog,v.opacity);}
