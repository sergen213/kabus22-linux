// HLSL port of the OpenGL scene program (render_gl.cpp sceneVS/sceneFS). Variants: K22_SHADOW_MAPS (shadows=shadowmap),
// K22_CASTER (depth-only shadow caster pass: no colour target, ends after the alpha rejection).
#include "common.hlsli"
cbuffer SceneView:register(b0){
    float4x4 viewProjection,portalProjection,sunShadowMatrix;
    float4 clipPlane,portalViewport,portalPlane;
    float3 sunDirection;int lightCount;
    float3 cameraPosition;int selectedLight;
    float3 fogColor;int pbr;
    float3 infiniteViewer;int toneInShader;
    float3 doorCenter;int doorMode;
    float3 doorNormal;int skyMask;
    float3 doorRight;int portalEnabled;
    float3 doorUp;int shadowMaps;
    float3 doorSkyColor;int casterPass;
    float2 fogRange,doorHalf;
    int shadowLight;float sunShadowTexel;int2 viewPadding;
};
cbuffer SceneDraw:register(b1){
    float4x4 model;
    float4 normalMatrix0,normalMatrix1,normalMatrix2;
    float4 tint,preparedAmbient,skillMain,skillExtra;
    float3 preparedDiffuse;float materialPower;
    float3 preparedSpecular;float materialSunStrength;
    float3 preparedEmissive;float interpolation;
    float3 ambientLight;float normalOrientation;
    float2 uvScale,uvOffset;
    float2 normalUvScale;float alpha;float materialAmbient;
    float surfaceAmbient;float reflectionWeight;float legacyRoughness;int frame0;
    int frame1;int vertexCount;int spriteFrames;int effectKind;
    int firstOverride;int nextOverride;int firstNormalOverride;int nextNormalOverride;
    int particleInstanced;int worldVertexLighting;int hasLightmap;int explicitMaterial;
    int unlit;int flare;int overlay;int invertFlare;
    int textureAlpha;int noFog;int doorSky;int roughnessMap;
};
StructuredBuffer<float3> poses:register(t2);
StructuredBuffer<float4> lights:register(t3);
#define K22_LIGHT(i) lights[i]
struct SceneInput{
    float3 position:POSITION;float3 normal:NORMAL;float2 texcoord:TEXCOORD0;float2 lightcoord:TEXCOORD1;
    float3 nextPosition:TEXCOORD2;float3 nextNormal:TEXCOORD3;float3 tangent:TEXCOORD4;float3 nextTangent:TEXCOORD5;
    float4 particleModel0:TEXCOORD6;float4 particleModel1:TEXCOORD7;float4 particleModel2:TEXCOORD8;float4 particleModel3:TEXCOORD9;
    float4 particleColor:COLOR0;float3 particleFlags:TEXCOORD10;
    uint vertexID:SV_VertexID;
};
struct SceneVaryings{
    float4 position:SV_Position;float clip:SV_ClipDistance0;
    float3 worldPosition:WORLDPOS;float3 worldNormal:NORMAL;float3 worldTangent:TANGENT;float2 uv:TEXCOORD0;float2 lmuv:TEXCOORD1;
    float3 vertexDynamicLight:LIGHT0;float3 vertexMaterialDiffuse:LIGHT1;float3 vertexMaterialSpecular:LIGHT2;
    float3 effectLight:EFFECT0;float3 effectView:EFFECT1;
    float4 instanceColor:COLOR0;nointerpolation float3 instanceFlags:FLAGS;
#ifdef K22_SHADOW_MAPS
    float3 vertexDynamicRaw:LIGHT3;float3 vertexShadowLight:LIGHT4;
#endif
};
float3 nativeDynamic(float3 p,float3 n,inout SceneVaryings o){
    float3 result=0;
    for(int i=0;i<lightCount;++i){
        float4 a=lights[i*5],b=lights[i*5+1],d=lights[i*5+2],e=lights[i*5+3],c=lights[i*5+4];
        if(b.a<0)continue;
        if(c.z>0.5)continue;
        float3 delta=a.xyz-p;float distance=length(delta);
        float3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        result+=b.rgb*abs(b.a)*attenuation*max(dot(n,direction),0);
#ifdef K22_SHADOW_MAPS
        if(i==shadowLight)o.vertexShadowLight=b.rgb*abs(b.a)*attenuation*max(dot(n,direction),0);
#endif
    }
#ifdef K22_SHADOW_MAPS
    o.vertexDynamicRaw=result;
#endif
    return clamp(result,0,1);
}
void ordinaryMaterial(float3 p,float3 n,out float3 diffuse,out float3 specular){
    diffuse=preparedEmissive+preparedAmbient.rgb*ambientLight;specular=0;
    for(int i=0;i<lightCount;++i){
        float4 a=lights[i*5],b=lights[i*5+1],d=lights[i*5+2],e=lights[i*5+3],c=lights[i*5+4];
        if(b.a<0)continue;
        float3 delta=a.xyz-p;float distance=length(delta);
        float3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        float3 light=b.rgb*b.a*attenuation*(i==selectedLight?materialSunStrength:1);
        diffuse+=preparedDiffuse*light*max(dot(n,direction),0);
        float3 halfVector=direction+infiniteViewer;float magnitude=length(halfVector);
        if(any(preparedSpecular!=0)){
            float power=magnitude>0?(materialPower==0?1:pow(max(dot(n,halfVector/magnitude),0),materialPower)):0;
            specular+=preparedSpecular*light*power;
        }
    }
    diffuse=clamp(diffuse,0,1);specular=clamp(specular,0,1);
}
float3 normalTransform(float3 v){return normalMatrix0.xyz*v.x+normalMatrix1.xyz*v.y+normalMatrix2.xyz*v.z;}
SceneVaryings sceneVS(SceneInput v){
    SceneVaryings o=(SceneVaryings)0;
    o.instanceColor=1;o.instanceFlags=0;
    if(particleInstanced){float4 w=v.particleModel0*v.position.x+v.particleModel1*v.position.y+v.particleModel2*v.position.z+v.particleModel3;o.worldPosition=w.xyz;o.worldNormal=float3(1,0,0);o.uv=v.texcoord;o.lmuv=v.lightcoord;o.instanceColor=v.particleColor;o.instanceFlags=v.particleFlags;o.position=k22Clip(mul(viewProjection,w));o.clip=dot(clipPlane,w);return o;}
    float3 p0=v.position,n0=v.normal,p1=v.position,n1=v.normal,t0=v.tangent,t1=v.tangent;
    if(vertexCount>0){int a=frame0*vertexCount*3+int(v.vertexID),b=frame1*vertexCount*3+int(v.vertexID);p0=poses[a];p1=poses[b];n0=poses[a+vertexCount];n1=poses[b+vertexCount];t0=poses[a+vertexCount*2];t1=poses[b+vertexCount*2];}
    if(firstOverride)p0=v.position;if(firstNormalOverride)n0=v.normal;if(firstOverride||firstNormalOverride)t0=v.tangent;
    if(nextOverride)p1=v.nextPosition;if(nextNormalOverride)n1=v.nextNormal;if(nextOverride||nextNormalOverride)t1=v.nextTangent;
    float4 w=mul(model,float4(lerp(p0,p1,interpolation),1));o.worldPosition=w.xyz;o.worldNormal=normalize(normalTransform(lerp(n0,n1,interpolation)))*normalOrientation;o.worldTangent=mul((float3x3)model,lerp(t0,t1,interpolation));
    if(worldVertexLighting)o.vertexDynamicLight=nativeDynamic(o.worldPosition,o.worldNormal,o);
    if(explicitMaterial&&effectKind==0)ordinaryMaterial(o.worldPosition,o.worldNormal,o.vertexMaterialDiffuse,o.vertexMaterialSpecular);
    if(effectKind!=0){float3 localN=lerp(n0,n1,interpolation),localT=lerp(t0,t1,interpolation);float3x3 m3=(float3x3)model;float3x3 basis=float3x3(mul(m3,localT),mul(m3,cross(localT,localN)),mul(m3,localN));o.effectLight=normalize(mul(basis,-(sunDirection+(effectKind==1?skillExtra.xyz:float3(0,0,0)))));o.effectView=normalize(mul(basis,cameraPosition-w.xyz));}
    o.uv=v.texcoord;o.lmuv=v.lightcoord;o.position=k22Clip(mul(viewProjection,w));o.clip=dot(clipPlane,w);
    return o;
}
Texture2D skin:register(t0);Texture2D lightmap:register(t1);Texture2D portalImage:register(t4);Texture2D normalSkin:register(t5);Texture2D pbrNormal:register(t6);Texture2D pbrOrm:register(t7);
#ifdef K22_SHADOW_MAPS
#include "effects.hlsli"
Texture2D<float> sunShadowMap:register(t8);TextureCube<float> cubeShadowMap:register(t9);
float lightShadow(int i,float3 p,float3 n){if(!shadowMaps||i!=shadowLight)return 1.0;float4 a=lights[i*5];return k22CubeShadow(cubeShadowMap,a.xyz,p,n,k22CubeShadowNear,a.w);}
float3 shadowedDynamic(SceneVaryings v,float3 p,float3 n){
    if(!shadowMaps||shadowLight<0||all(v.vertexShadowLight<=0))return v.vertexDynamicLight;
    float3 unshadowed=clamp(v.vertexDynamicRaw,0,1),result=clamp(v.vertexDynamicRaw-v.vertexShadowLight*(1-lightShadow(shadowLight,p,n)),0,1);
    return v.vertexDynamicLight*float3(unshadowed.x>0?result.x/unshadowed.x:1,unshadowed.y>0?result.y/unshadowed.y:1,unshadowed.z>0?result.z/unshadowed.z:1);
}
#define K22_PBR_LIGHT_VISIBILITY(i) lightShadow(i,p,Ng)
#define K22_SUN_SHADOW(p,n) (shadowMaps&&!hasLightmap?k22SunShadow(sunShadowMap,sunShadowMatrix,p,n,sunShadowTexel):1.0)
#define K22_LIGHT_SHADOW(i,p,n) lightShadow(i,p,n)
#define K22_DYNAMIC(p,n) shadowedDynamic(v,p,n)
#else
#define K22_SUN_SHADOW(p,n) 1.0
#define K22_LIGHT_SHADOW(i,p,n) 1.0
#define K22_DYNAMIC(p,n) v.vertexDynamicLight
#endif
#include "pbr.hlsli"
float3 enhancedBaked(float3 p,float3 n){
    if(hasLightmap)return 0;
    float3 result=0;
    for(int i=0;i<lightCount;++i){
        float4 a=lights[i*5],b=lights[i*5+1];
        if(b.a>=0)continue;
        float3 delta=a.xyz-p;float distance=length(delta),attenuation=max(1-distance/max(a.w,.0001),0);
        result+=b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,delta/max(distance,.0001)),0);
    }
    return result;
}
#ifdef K22_CASTER
void sceneCasterPS(SceneVaryings v)
#else
float4 scenePS(SceneVaryings v):SV_Target0
#endif
{
    if(doorMode!=0){
        float sC=dot(cameraPosition-doorCenter,doorNormal),sP=dot(v.worldPosition-doorCenter,doorNormal);float3 d=cameraPosition+(v.worldPosition-cameraPosition)*(sC/(sC-sP))-doorCenter;
        bool inside=sC-sP>0&&abs(dot(d,doorRight))<=doorHalf.x&&abs(dot(d,doorUp))<=doorHalf.y;
        if(doorMode==1?sP<1&&inside:!(sP<-2&&inside))discard;
    }
#ifdef K22_CASTER
#define K22_RETURN(c) return
#else
#define K22_RETURN(c) return c
#endif
    if(skyMask)K22_RETURN(float4(1,1,1,1));
    if(doorSky)K22_RETURN(float4(doorSkyColor,1));
    float2 coord=v.uv*uvScale+uvOffset;float4 texel;
    if(spriteFrames>1){float cols=floor(1.0/uvScale.x+0.5),f0=float(frame0),f1=float(frame1);texel=lerp(skin.Sample(K22_SAMPLER(0),coord+float2(k22Mod(f0,cols)/cols,floor(f0/cols)*uvScale.y)),skin.Sample(K22_SAMPLER(0),coord+float2(k22Mod(f1,cols)/cols,floor(f1/cols)*uvScale.y)),interpolation);}else texel=skin.Sample(K22_SAMPLER(0),coord);
    if(effectKind!=0){
        float3 bump=2*normalSkin.Sample(K22_SAMPLER(5),v.uv*normalUvScale+uvOffset).rgb-1;
        float3 reflection=normalize(2*dot(bump,v.effectLight)*bump-v.effectLight);
        float specular=skillMain.z*pow(clamp(dot(reflection,normalize(v.effectView)),0,1),skillMain.w);
        float4 sunlight=effectKind==1?float4(.9,.9,1,.9):skillExtra;
        K22_RETURN((skillMain.x*preparedAmbient+skillMain.y*clamp(dot(v.effectLight,bump),0,1)*sunlight+specular)*texel);
    }
    bool useOverlay=particleInstanced?v.instanceFlags.y>0.5:overlay,useFlare=particleInstanced?v.instanceFlags.x>0.5:flare,useInversion=particleInstanced?v.instanceFlags.z>0.5:invertFlare;
    if(useOverlay&&(max(max(texel.r,texel.g),texel.b)<0.001||texel.a<0.5))discard;
    if(useFlare&&!textureAlpha){float brightness=max(max(texel.r,texel.g),texel.b);texel.a*=useInversion?1-brightness:brightness;}
    float4 t=texel*tint*v.instanceColor;t.a*=alpha;if(t.a<0.01)discard;
#ifdef K22_CASTER
    return;
#else
    float3 illumination=1;float sunVisibility=1.0;
    if(!unlit&&!explicitMaterial){
        illumination=hasLightmap?2*clamp(lightmap.Sample(K22_SAMPLER(1),v.lmuv).rgb+surfaceAmbient,0,1):float3(materialAmbient,materialAmbient,materialAmbient);
        float3 n=normalize(v.worldNormal);sunVisibility=K22_SUN_SHADOW(v.worldPosition,n);if(!hasLightmap)illumination+=max(dot(n,sunDirection),0)*sunVisibility;
        if(worldVertexLighting)illumination+=K22_DYNAMIC(v.worldPosition,n)+enhancedBaked(v.worldPosition,n);
        else for(int i=0;i<lightCount;++i){float4 a=lights[i*5],b=lights[i*5+1];if((lights[i*5+4].z>0.5&&b.a>=0&&lights[i*5+2].w==3)||(hasLightmap&&b.a<0))continue;float3 d=a.xyz-v.worldPosition;float distance=length(d);float attenuation=max(1-distance/max(a.w,0.0001),0);illumination+=b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,d/max(distance,0.0001)),0)*K22_LIGHT_SHADOW(i,v.worldPosition,n);}
    }
    float3 rgb=explicitMaterial?clamp(clamp(t.rgb*v.vertexMaterialDiffuse*2,0,1)+v.vertexMaterialSpecular,0,1):t.rgb*illumination;
    if(pbr&&!unlit&&!explicitMaterial&&!particleInstanced)rgb=k22Pbr(t.rgb,illumination,v.worldPosition,normalize(v.worldNormal),v.worldTangent,normalOrientation,pbrNormal.Sample(K22_SAMPLER(6),coord).rgb,pbrOrm.Sample(K22_SAMPLER(7),coord),roughnessMap!=0,legacyRoughness,cameraPosition,sunDirection,sunVisibility,!hasLightmap);
    if(!noFog&&fogRange.y>fogRange.x){float f=clamp((length(v.worldPosition-cameraPosition)-fogRange.x)/(fogRange.y-fogRange.x),0,1);rgb=lerp(rgb,fogColor,f);}
    if(portalEnabled&&reflectionWeight>0&&abs(dot(portalPlane,float4(v.worldPosition,1)))<0.1){
        float4 reflected=mul(portalProjection,float4(v.worldPosition,1));
        if(reflected.w>0){float2 pc=(reflected.xy/reflected.w*.5+.5)*portalViewport.zw+portalViewport.xy;rgb=lerp(rgb,portalImage.Sample(K22_SAMPLER(4),pc).rgb,reflectionWeight);}
    }
    if(pbr&&toneInShader)rgb=k22ToneMap(rgb);
    return float4(rgb,t.a);
#endif
}
