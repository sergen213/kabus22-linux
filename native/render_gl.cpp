#include "render_gl.hpp"
#include "render_policy.hpp"
#include "render_pbr_glsl.hpp"
#include "render_effects_glsl.hpp"
#include "texture_pack.hpp"
#include <epoxy/gl.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace k22 {
namespace {
using namespace render_policy;
GLuint shader(GLenum type,const char*source) {
    GLuint id=glCreateShader(type); glShaderSource(id,1,&source,nullptr); glCompileShader(id);
    GLint ok{};glGetShaderiv(id,GL_COMPILE_STATUS,&ok); if(!ok) {GLint n{};glGetShaderiv(id,GL_INFO_LOG_LENGTH,&n);std::string log(n,'\0');glGetShaderInfoLog(id,n,nullptr,log.data());glDeleteShader(id);throw std::runtime_error("OpenGL shader: "+log);} return id;
}
GLuint program(const char*vs,const char*fs,const char*gs=nullptr) {
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs),g=gs?shader(GL_GEOMETRY_SHADER,gs):0,p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);if(g)glAttachShader(p,g);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);if(g)glDeleteShader(g);
    GLint ok{};glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){GLint n{};glGetProgramiv(p,GL_INFO_LOG_LENGTH,&n);std::string log(n,'\0');glGetProgramInfoLog(p,n,nullptr,log.data());glDeleteProgram(p);throw std::runtime_error("OpenGL link: "+log);}return p;
}
// Prefixed with "#version 330 core\n" (plus "#define K22_SHADOW_MAPS 1\n" when shadow maps are on).
const char* sceneVS=R"(
layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec2 texcoord;layout(location=3) in vec2 lightcoord;
layout(location=4) in vec3 nextPosition;layout(location=5) in vec3 nextNormal;
layout(location=12) in vec3 tangent;layout(location=13) in vec3 nextTangent;
layout(location=6) in mat4 particleModel;layout(location=10) in vec4 particleColor;layout(location=11) in vec3 particleFlags;
uniform mat4 model,viewProjection;uniform mat3 normalMatrix;uniform vec4 clipPlane;uniform samplerBuffer poses;uniform float normalOrientation;
uniform int frame0,frame1,vertexCount;uniform float interpolation;uniform bool firstOverride,nextOverride,firstNormalOverride,nextNormalOverride;
uniform bool particleInstanced;
uniform int effectKind;uniform vec3 sunDirection,cameraPosition;uniform vec4 skillExtra;
out vec3 effectLight,effectView;
out vec3 worldPosition,worldNormal,worldTangent;out vec2 uv,lmuv;
uniform samplerBuffer lights;uniform int lightCount;uniform bool worldVertexLighting,hasLightmap;
out vec3 vertexDynamicLight;
uniform bool explicitMaterial;uniform vec4 preparedAmbient;uniform vec3 preparedDiffuse,preparedSpecular,preparedEmissive,ambientLight,infiniteViewer;
uniform float materialPower,materialSunStrength;uniform int selectedLight;
out vec3 vertexMaterialDiffuse,vertexMaterialSpecular;
#ifdef K22_SHADOW_MAPS
uniform int shadowLight;out vec3 vertexDynamicRaw,vertexShadowLight; // Unclamped sum and the shadow light's own term.
#endif
vec3 nativeDynamic(vec3 p,vec3 n){
    vec3 result=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=texelFetch(lights,i*5),b=texelFetch(lights,i*5+1),d=texelFetch(lights,i*5+2),e=texelFetch(lights,i*5+3),c=texelFetch(lights,i*5+4);
        if(b.a<0)continue;
        if(c.z>0.5)continue; // Source sun is consumed separately; serialized baked lights are an enhanced fragment path.
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        result+=b.rgb*abs(b.a)*attenuation*max(dot(n,direction),0);
#ifdef K22_SHADOW_MAPS
        if(i==shadowLight)vertexShadowLight=b.rgb*abs(b.a)*attenuation*max(dot(n,direction),0);
#endif
    }
#ifdef K22_SHADOW_MAPS
    vertexDynamicRaw=result;
#endif
    return clamp(result,0,1);
}
void ordinaryMaterial(vec3 p,vec3 n,out vec3 diffuse,out vec3 specular){
    diffuse=preparedEmissive+preparedAmbient.rgb*ambientLight;specular=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=texelFetch(lights,i*5),b=texelFetch(lights,i*5+1),d=texelFetch(lights,i*5+2),e=texelFetch(lights,i*5+3),c=texelFetch(lights,i*5+4);
        if(b.a<0)continue;
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        vec3 light=b.rgb*b.a*attenuation*(i==selectedLight?materialSunStrength:1);
        diffuse+=preparedDiffuse*light*max(dot(n,direction),0);
        vec3 halfVector=direction+infiniteViewer;float magnitude=length(halfVector);
        if(any(notEqual(preparedSpecular,vec3(0)))){
            float power=magnitude>0?(materialPower==0?1:pow(max(dot(n,halfVector/magnitude),0),materialPower)):0;
            specular+=preparedSpecular*light*power;
        }
    }
    diffuse=clamp(diffuse,0,1);specular=clamp(specular,0,1);
}
out vec4 instanceColor;flat out vec3 instanceFlags;
void main(){
    instanceColor=vec4(1);instanceFlags=vec3(0);
    vertexDynamicLight=vec3(0);vertexMaterialDiffuse=vec3(0);vertexMaterialSpecular=vec3(0);
    worldTangent=vec3(0);
#ifdef K22_SHADOW_MAPS
    vertexDynamicRaw=vertexShadowLight=vec3(0);
#endif
    if(particleInstanced){vec4 w=particleModel*vec4(position,1);worldPosition=w.xyz;worldNormal=vec3(1,0,0);uv=texcoord;lmuv=lightcoord;instanceColor=particleColor;instanceFlags=particleFlags;gl_Position=viewProjection*w;gl_ClipDistance[0]=dot(clipPlane,w);return;}
    vec3 p0=position,n0=normal,p1=position,n1=normal,t0=tangent,t1=tangent;
    if(vertexCount>0){int a=frame0*vertexCount*3+gl_VertexID,b=frame1*vertexCount*3+gl_VertexID;p0=texelFetch(poses,a).xyz;p1=texelFetch(poses,b).xyz;n0=texelFetch(poses,a+vertexCount).xyz;n1=texelFetch(poses,b+vertexCount).xyz;t0=texelFetch(poses,a+vertexCount*2).xyz;t1=texelFetch(poses,b+vertexCount*2).xyz;}
    if(firstOverride)p0=position;if(firstNormalOverride)n0=normal;if(firstOverride||firstNormalOverride)t0=tangent;
    if(nextOverride)p1=nextPosition;if(nextNormalOverride)n1=nextNormal;if(nextOverride||nextNormalOverride)t1=nextTangent;
    vec4 w=model*vec4(mix(p0,p1,interpolation),1);worldPosition=w.xyz;worldNormal=normalize(normalMatrix*mix(n0,n1,interpolation))*normalOrientation;worldTangent=mat3(model)*mix(t0,t1,interpolation);
    if(worldVertexLighting)vertexDynamicLight=nativeDynamic(worldPosition,worldNormal);
    if(explicitMaterial&&effectKind==0)ordinaryMaterial(worldPosition,worldNormal,vertexMaterialDiffuse,vertexMaterialSpecular);
    if(effectKind!=0){vec3 localN=mix(n0,n1,interpolation),localT=mix(t0,t1,interpolation);mat3 basis=transpose(mat3(mat3(model)*localT,mat3(model)*cross(localT,localN),mat3(model)*localN));effectLight=normalize(basis*-(sunDirection+(effectKind==1?skillExtra.xyz:vec3(0))));effectView=normalize(basis*(cameraPosition-w.xyz));}
    uv=texcoord;lmuv=lightcoord;gl_Position=viewProjection*w;gl_ClipDistance[0]=dot(clipPlane,w);
})";
const char* sceneFS=R"(
in vec3 worldPosition,worldNormal,worldTangent;in vec2 uv,lmuv;out vec4 color;
uniform bool pbr,toneInShader,roughnessMap;uniform float legacyRoughness,normalOrientation;uniform sampler2D pbrNormal,pbrOrm;
in vec3 vertexDynamicLight;uniform bool worldVertexLighting;
in vec3 vertexMaterialDiffuse,vertexMaterialSpecular;
in vec4 instanceColor;flat in vec3 instanceFlags;uniform bool particleInstanced;
uniform sampler2D skin,lightmap;uniform int spriteFrames,frame0,frame1;uniform float interpolation;uniform bool hasLightmap,unlit,flare,overlay,invertFlare,textureAlpha,noFog;
uniform vec4 tint;uniform vec3 sunDirection,cameraPosition,fogColor;uniform vec2 uvScale,uvOffset,fogRange;uniform float alpha,materialAmbient,surfaceAmbient;uniform bool skyMask;
uniform sampler2D portalImage;uniform mat4 portalProjection;uniform vec4 portalViewport,portalPlane;uniform bool portalEnabled;uniform float reflectionWeight;
in vec3 effectLight,effectView;uniform int effectKind;uniform sampler2D normalSkin;uniform vec2 normalUvScale;
uniform vec4 skillMain,skillExtra,preparedAmbient;uniform bool explicitMaterial;
#ifdef K22_SHADOW_MAPS
// Sun depth map plus one point/spot depth cube (shadowLight, -1 none); casterPass renders depth only.
uniform bool shadowMaps,casterPass;uniform int shadowLight;uniform mat4 sunShadowMatrix;uniform float sunShadowTexel;uniform sampler2DShadow sunShadowMap;uniform samplerCubeShadow cubeShadowMap;
in vec3 vertexDynamicRaw,vertexShadowLight;
float lightShadow(int i,vec3 p,vec3 n){if(!shadowMaps||i!=shadowLight)return 1.0;vec4 a=texelFetch(lights,i*5);return k22CubeShadow(cubeShadowMap,a.xyz,p,n,k22CubeShadowNear,a.w);}
// Vertex nativeDynamic with the shadow light's interpolated term occluded per fragment; scales the Gouraud result per
// channel (as the ray-traced visibleNativeDynamic does).
vec3 shadowedDynamic(vec3 p,vec3 n){
    if(!shadowMaps||shadowLight<0||all(lessThanEqual(vertexShadowLight,vec3(0))))return vertexDynamicLight;
    vec3 unshadowed=clamp(vertexDynamicRaw,0,1),result=clamp(vertexDynamicRaw-vertexShadowLight*(1-lightShadow(shadowLight,p,n)),0,1);
    return vertexDynamicLight*vec3(unshadowed.x>0?result.x/unshadowed.x:1,unshadowed.y>0?result.y/unshadowed.y:1,unshadowed.z>0?result.z/unshadowed.z:1);
}
#define K22_SUN_SHADOW(p,n) (shadowMaps&&!hasLightmap?k22SunShadow(sunShadowMap,sunShadowMatrix,p,n,sunShadowTexel):1.0)
#define K22_LIGHT_SHADOW(i,p,n) lightShadow(i,p,n)
#define K22_DYNAMIC(p,n) shadowedDynamic(p,n)
#else
#define K22_SUN_SHADOW(p,n) 1.0
#define K22_LIGHT_SHADOW(i,p,n) 1.0
#define K22_DYNAMIC(p,n) vertexDynamicLight
#endif
vec3 enhancedBaked(vec3 p,vec3 n){
    if(hasLightmap)return vec3(0);
    vec3 result=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=texelFetch(lights,i*5),b=texelFetch(lights,i*5+1);
        if(b.a>=0)continue; // Only serialized baked records, independent of native slot order.
        vec3 delta=a.xyz-p;float distance=length(delta),attenuation=max(1-distance/max(a.w,.0001),0);
        result+=b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,delta/max(distance,.0001)),0);
    }
    return result;
}
void main(){
    if(skyMask){color=vec4(1);return;}
    vec2 coord=uv*uvScale+uvOffset;vec4 texel;
    if(spriteFrames>1){float cols=floor(1.0/uvScale.x+0.5),f0=float(frame0),f1=float(frame1);texel=mix(texture(skin,coord+vec2(mod(f0,cols)/cols,floor(f0/cols)*uvScale.y)),texture(skin,coord+vec2(mod(f1,cols)/cols,floor(f1/cols)*uvScale.y)),interpolation);}else texel=texture(skin,coord); // sprite grid cells (engine.cpp load_entity_asset)
    if(effectKind!=0){
        vec3 bump=2*texture(normalSkin,uv*normalUvScale+uvOffset).rgb-1;
        vec3 reflection=normalize(2*dot(bump,effectLight)*bump-effectLight);
        float specular=skillMain.z*pow(clamp(dot(reflection,normalize(effectView)),0,1),skillMain.w);
        vec4 sunlight=effectKind==1?vec4(.9,.9,1,.9):skillExtra;
        color=(skillMain.x*preparedAmbient+skillMain.y*clamp(dot(effectLight,bump),0,1)*sunlight+vec4(specular))*texel;
        return;
    }
    bool useOverlay=particleInstanced?instanceFlags.y>0.5:overlay,useFlare=particleInstanced?instanceFlags.x>0.5:flare,useInversion=particleInstanced?instanceFlags.z>0.5:invertFlare;
    if(useOverlay&&(max(max(texel.r,texel.g),texel.b)<0.001||texel.a<0.5))discard;
    if(useFlare&&!textureAlpha){float brightness=max(max(texel.r,texel.g),texel.b);texel.a*=useInversion?1-brightness:brightness;}
    vec4 t=texel*tint*instanceColor;t.a*=alpha;if(t.a<0.01)discard;
#ifdef K22_SHADOW_MAPS
    if(casterPass){color=vec4(0);return;}
#endif
    vec3 illumination=vec3(1);float sunVisibility=1.0;
    if(!unlit&&!explicitMaterial){
        illumination=hasLightmap?2*clamp(texture(lightmap,lmuv).rgb+vec3(surfaceAmbient),0,1):vec3(materialAmbient);
        vec3 n=normalize(worldNormal);sunVisibility=K22_SUN_SHADOW(worldPosition,n);if(!hasLightmap)illumination+=vec3(max(dot(n,sunDirection),0))*sunVisibility;
        if(worldVertexLighting)illumination+=K22_DYNAMIC(worldPosition,n)+enhancedBaked(worldPosition,n);
        else for(int i=0;i<lightCount;++i){vec4 a=texelFetch(lights,i*5),b=texelFetch(lights,i*5+1);if((texelFetch(lights,i*5+4).z>0.5&&b.a>=0&&texelFetch(lights,i*5+2).w==3)||(hasLightmap&&b.a<0))continue;vec3 d=a.xyz-worldPosition;float distance=length(d);float attenuation=max(1-distance/max(a.w,0.0001),0);illumination+=b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,d/max(distance,0.0001)),0)*K22_LIGHT_SHADOW(i,worldPosition,n);}
    }
    vec3 rgb=explicitMaterial?clamp(clamp(t.rgb*vertexMaterialDiffuse*2,0,1)+vertexMaterialSpecular,0,1):t.rgb*illumination;
    if(pbr&&!unlit&&!explicitMaterial&&!particleInstanced)rgb=k22Pbr(t.rgb,illumination,worldPosition,normalize(worldNormal),worldTangent,normalOrientation,texture(pbrNormal,coord).rgb,texture(pbrOrm,coord),roughnessMap,legacyRoughness,cameraPosition,sunDirection,sunVisibility,!hasLightmap);
    if(!noFog&&fogRange.y>fogRange.x){float f=clamp((length(worldPosition-cameraPosition)-fogRange.x)/(fogRange.y-fogRange.x),0,1);rgb=mix(rgb,fogColor,f);}
    if(portalEnabled&&reflectionWeight>0&&abs(dot(portalPlane,vec4(worldPosition,1)))<0.1){
        vec4 reflected=portalProjection*vec4(worldPosition,1);
        if(reflected.w>0){vec2 coord=(reflected.xy/reflected.w*.5+.5)*portalViewport.zw+portalViewport.xy;rgb=mix(rgb,texture(portalImage,coord).rgb,reflectionWeight);}
    }
    if(pbr&&toneInShader)rgb=k22ToneMap(rgb);
    color=vec4(rgb,t.a);
})";
const char* shadowVS=R"(#version 330 core
layout(location=0) in vec3 position;layout(location=4) in vec3 nextPosition;
uniform mat4 model,projection,viewProjection;uniform samplerBuffer poses;
uniform int frame0,frame1,vertexCount;uniform float interpolation;uniform bool firstOverride,nextOverride;
uniform vec4 clipPlane;
out float planeDistance;
out vec3 projectedPosition;
void main(){
    vec3 p0=position,p1=position;
    if(vertexCount>0){p0=texelFetch(poses,frame0*vertexCount*3+gl_VertexID).xyz;p1=texelFetch(poses,frame1*vertexCount*3+gl_VertexID).xyz;}
    if(firstOverride)p0=position;if(nextOverride)p1=nextPosition;
    vec4 p=projection*model*vec4(mix(p0,p1,interpolation),1);
    gl_Position=viewProjection*p;planeDistance=dot(clipPlane,p);projectedPosition=p.xyz;
})";
const char* shadowGS=R"(#version 330 core
layout(triangles) in;layout(triangle_strip,max_vertices=42) out;
in float planeDistance[];in vec3 projectedPosition[];flat out float opacity;out vec3 shadowPosition;
uniform vec2 viewport;uniform float areaThreshold,alpha;uniform bool reversed;
float distanceToPlane(vec4 p,int plane){
    if(plane==0)return p.z+p.w;
    if(plane==1)return p.x+p.w;if(plane==2)return p.w-p.x;
    if(plane==3)return p.y+p.w;return p.w-p.y;
}
void main(){
    vec4 polygon[10],scratch[10];float pd[10],sd[10];vec3 positions[10],sp[10];int count=3;
    for(int i=0;i<3;++i){polygon[i]=gl_in[i].gl_Position;pd[i]=planeDistance[i];positions[i]=projectedPosition[i];}
    for(int plane=0;plane<6&&count>0;++plane){
        int outputCount=0;
        for(int i=0;i<count;++i){
            int j=(i+count-1)%count;float a=plane==5?pd[j]:distanceToPlane(polygon[j],plane),b=plane==5?pd[i]:distanceToPlane(polygon[i],plane);
            if((a>=0)!=(b>=0)){float t=a/(a-b);scratch[outputCount]=mix(polygon[j],polygon[i],t);sd[outputCount]=mix(pd[j],pd[i],t);sp[outputCount++]=mix(positions[j],positions[i],t);}
            if(b>=0){scratch[outputCount]=polygon[i];sd[outputCount]=pd[i];sp[outputCount++]=positions[i];}
        }
        count=outputCount;for(int i=0;i<count;++i){polygon[i]=scratch[i];pd[i]=sd[i];positions[i]=sp[i];}
    }
    for(int i=1;i+1<count;++i){
        vec4 triangle[3];triangle[0]=polygon[0];triangle[1]=polygon[i];triangle[2]=polygon[i+1];
        vec2 p[3];for(int j=0;j<3;++j)p[j]=triangle[j].xy/triangle[j].w*viewport*.5;
        // Source screen Y runs down, unlike OpenGL NDC.
        float area=-((p[0].y-p[1].y)*(p[0].x-p[2].x)-(p[0].x-p[1].x)*(p[0].y-p[2].y));
        if(reversed?area>areaThreshold:area<areaThreshold)continue;
        for(int duplicate=0;duplicate<2;++duplicate){
            opacity=duplicate==0?alpha:0;
            for(int j=0;j<3;++j){gl_Position=triangle[j];shadowPosition=positions[j==0?0:j==1?i:i+1];if(duplicate!=0)gl_Position.z-=0.0009765625*gl_Position.w;EmitVertex();}
            EndPrimitive();
        }
    }
})";
const char* shadowFS=R"(#version 330 core
flat in float opacity;in vec3 shadowPosition;out vec4 color;
uniform vec3 cameraPosition,fogColor;uniform vec2 fogRange;uniform bool noFog;
void main(){float fog=!noFog&&fogRange.y>fogRange.x?clamp((length(shadowPosition-cameraPosition)-fogRange.x)/(fogRange.y-fogRange.x),0,1):0;color=vec4(fogColor*fog,opacity);}
)";
const char* uiVS=R"(#version 330 core
layout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;layout(location=2) in vec4 vertexColor;layout(location=3) in vec4 texBounds;
uniform vec2 viewport;out vec2 uv;out vec4 tint;flat out vec4 bounds;void main(){uv=texcoord;tint=vertexColor;bounds=texBounds;gl_Position=vec4(position.x/viewport.x*2-1,1-position.y/viewport.y*2,0,1);})";
// UI bitmaps are sub-rects of shared atlas textures: sample clamped to the sub-rect's edge texel centres, as if it were its own
// CLAMP_TO_EDGE texture. Otherwise MSAA edge pixels (evaluated at the pixel centre outside the quad), edge ties and linear
// FILTER reach the neighbouring atlas texels (the white separator rows of Enerji_MenuBar.tga/Enerji_Kalp.tga).
const char* uiFS=R"(#version 330 core
in vec2 uv;in vec4 tint;flat in vec4 bounds;uniform sampler2D image;uniform bool overlay,textKey,cursorKey;uniform float alphaRef;out vec4 color;void main(){vec2 h=.5/vec2(textureSize(image,0)),lo=bounds.xy+h;vec4 c=texture(image,clamp(uv,lo,max(lo,bounds.zw-h)));float brightness=max(max(c.r,c.g),c.b);if((textKey&&brightness<0.001)||(overlay&&((!cursorKey&&brightness<0.001)||c.a<alphaRef)))discard;color=c*tint;})";
const char* postVS=R"(#version 330 core
out vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2-1,0,1);})";
const char* postFS=R"(#version 330 core
in vec2 uv;out vec4 color;uniform sampler2D image,ramp;uniform bool enabled;void main(){vec4 c=texture(image,uv);if(enabled){ivec3 index=ivec3(clamp(c.rgb,0,1)*255+0.5);c.rgb=vec3(texelFetch(ramp,ivec2(index.r,0),0).r,texelFetch(ramp,ivec2(index.g,1),0).r,texelFetch(ramp,ivec2(index.b,2),0).r);}color=c;})";
const char* warpFS=R"(#version 330 core
uniform sampler2D original;uniform isamplerBuffer offsets;uniform ivec2 size;uniform int stride;out vec4 color;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);int index=pixel.y*size.x+pixel.x,i=index/size.y,j=index%size.y;int sx=(texelFetch(offsets,j).r+i)&(size.x-1),sy=(texelFetch(offsets,i).r+j)&(size.y-1);int source=sx+sy*stride;color=texelFetch(original,ivec2(source%size.x,source/size.x),0);})";
const char* skyFS=R"(#version 330 core
in vec2 uv;uniform sampler2DArray image;uniform mat3 basis;uniform vec2 cone,scroll;uniform bool cube,blend;uniform float curve,clip;uniform int layer;out vec4 color;
void main(){
    vec2 screen=vec2(uv.x-.5,.5-uv.y);vec3 ray=basis*vec3(1,-screen.x*cone.x,-screen.y*cone.y);vec2 coord;int face=layer;float fade=1;
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
})";
const char* cloudFS=R"(#version 330 core
uniform sampler2DArray original;uniform int level,layer;uniform bool convertAlpha;out vec4 color;
void main(){vec4 c=texelFetch(original,ivec3(ivec2(gl_FragCoord.xy),layer),level);if(convertAlpha){ivec3 rgb=ivec3(c.rgb*255+.5)>>4;int a=min(15,int(float(rgb.r+rgb.g+rgb.b)*(24.0/45.0)));c=vec4(vec3(rgb)/15,float(a)/15);}color=c;})";
const char* sceneFSPrelude="#version 330 core\nuniform samplerBuffer lights;uniform int lightCount;\n#define K22_LIGHT(i) texelFetch(lights,i)\n";
const char* tonemapFSPrelude="#version 330 core\n#define K22_LIGHT(i) vec4(0)\nconst int lightCount=0;\n";
const char* tonemapFS="in vec2 uv;out vec4 color;uniform sampler2D image;void main(){vec4 c=texture(image,uv);color=vec4(k22ToneMap(c.rgb),c.a);}";
// Wave-2 screen effects (effects.glsl), built on tonemapFSPrelude+pbrGLSL+effectsGLSL; the fog program reads the scene lights.
const char* linearizeFS="in vec2 uv;out vec4 color;uniform sampler2D depth;uniform float nearPlane,farPlane;void main(){float d=texelFetch(depth,ivec2(gl_FragCoord.xy),0).r;color=vec4(d<1.0?k22LinearDepth(d,nearPlane,farPlane):0.0);}";
const char* ssaoFS="in vec2 uv;out vec4 color;uniform sampler2D linearDepth;uniform mat4 viewProjection,inverseViewProjection;uniform vec3 eye,forward;uniform vec2 texel;void main(){color=vec4(k22Ssao(linearDepth,uv,texel,inverseViewProjection,viewProjection,eye,forward,k22Noise(gl_FragCoord.xy)));}";
// 4x4 box (four bilinear taps) over the noisy AO, multiplied into the view color by the blend.
const char* aoApplyFS="in vec2 uv;out vec4 color;uniform sampler2D ao;uniform vec2 texel;void main(){float a=0.0;for(int i=0;i<4;++i)a+=texture(ao,uv+texel*vec2((i&1)!=0?0.5:-1.5,(i&2)!=0?0.5:-1.5)).r;color=vec4(vec3(a*0.25),1);}";
const char* fogFSPrelude="#version 330 core\nuniform samplerBuffer lights;uniform int lightCount;\n#define K22_LIGHT(i) texelFetch(lights,i)\nfloat fogVisibility(int i,vec3 x);\n#define K22_FOG_VISIBILITY(i,x) fogVisibility(i,x)\n";
const char* fogFS="in vec2 uv;out vec4 color;uniform sampler2D linearDepth;uniform mat4 inverseViewProjection;uniform vec3 eye,forward;uniform vec2 fogRange;uniform float farPlane;uniform bool shadowMaps;uniform int shadowLight;uniform samplerCubeShadow cubeShadowMap;\n"
    "float fogVisibility(int i,vec3 x){if(!shadowMaps||i!=shadowLight)return 1.0;vec4 a=K22_LIGHT(i*5);return k22CubeShadow(cubeShadowMap,a.xyz,x,vec3(0),k22CubeShadowNear,a.w);}\n"
    "void main(){float d=texture(linearDepth,uv).r;if(d<=0.0)d=farPlane;color=vec4(k22FogInscatter(eye,eye+k22ViewRay(k22NdcFromUv(uv),inverseViewProjection,eye,forward)*d,fogRange,k22Noise(gl_FragCoord.xy)),0);}";
// mode 0: prefiltered 4x4 downsample of the HDR scene, 1: downsample, 2: tent upsample (additively blended).
const char* bloomFS="in vec2 uv;out vec4 color;uniform sampler2D image;uniform vec2 texel;uniform int mode;void main(){color=vec4(mode==0?k22BloomPrefilter(k22BloomDown(image,uv,texel)):mode==1?k22BloomDown(image,uv,texel):k22BloomUp(image,uv,texel),1);}";
const char* compositeFS="in vec2 uv;out vec4 color;uniform sampler2D image,bloom;uniform bool bloomEnabled,toneMap;void main(){vec4 c=texture(image,uv);if(bloomEnabled)c.rgb+=k22BloomStrength*texture(bloom,uv).rgb;color=vec4(toneMap?k22ToneMap(c.rgb):c.rgb,c.a);}";
const char* fxaaFS="in vec2 uv;out vec4 color;uniform sampler2D image;uniform vec2 texel;void main(){color=vec4(k22Fxaa(image,uv,texel),texture(image,uv).a);}";
struct UIVertex { float x,y,u,v,r,g,b,a,u0,v0,u1,v1; }; // u0..v1: the sampled sub-rect (min/max texture coordinates).
struct Batch { GLuint first{},count{};std::uint32_t material{};GLuint centroidFirst{},centroidCount{}; };
struct MeshGPU { GLuint vao{},vertices{},indices{},tangents{},poseBuffer{},poses{};std::vector<Batch> batches;int frameCount{},primaryMax{},skinLayers{};Bounds bounds;std::vector<Bounds> frameBounds; };
// width/height stay the original image's; pack replacements change only GPU storage, never UV policy.
struct TextureGPU {GLuint id{};std::uint32_t width{},height{};std::uint64_t serial{};bool dynamic{},alpha{},mipsValid{};GLuint normal{},orm{};bool roughness{};};
struct MeshOverride {
    GLuint positions{},normals{},tangents{};
    std::uint64_t revision{~0ull},tangentRevision{~0ull},boundsRevision{~0ull};
    std::size_t count{},normalCount{},boundsCount{};
    std::uint32_t frame{~0u};
    const Model*model{},*boundsModel{};
    std::vector<Vec3>tangentData;
    Bounds bounds;
    bool tangentReady{};
};
Bounds mesh_override_bounds(MeshOverride&data,const Model&model,std::uint64_t revision,std::span<const Vec3>positions){
    if(data.boundsModel!=&model||data.boundsRevision!=revision||data.boundsCount!=positions.size()){
        data.bounds=position_bounds(positions);data.boundsModel=&model;data.boundsRevision=revision;data.boundsCount=positions.size();
    }
    return data.bounds;
}
bool override_tangents_needed(const MeshOverride&data,EffectKind effect,std::uint64_t revision){
    return effect!=EffectKind::None&&(!data.tangentReady||data.tangentRevision!=revision);
}
void set_override_pose(MeshOverride&data,const Model&model,std::uint64_t revision,std::size_t count,std::size_t normalCount,std::uint32_t frame){
    data.tangentReady=false;data.revision=revision;data.count=count;data.normalCount=normalCount;data.frame=frame;data.model=&model;
}
void release_override(MeshOverride&data){
    if(data.positions)glDeleteBuffers(1,&data.positions);
    if(data.normals)glDeleteBuffers(1,&data.normals);
    if(data.tangents)glDeleteBuffers(1,&data.tangents);
}
void purge_model_overrides(std::unordered_map<std::uint32_t,MeshOverride>&overrides,const Model*model){
    for(auto it=overrides.begin();it!=overrides.end();){
        if(it->second.model==model||it->second.boundsModel==model){release_override(it->second);it=overrides.erase(it);}
        else ++it;
    }
}
struct EffectBank { GLuint texture{};std::uint32_t width{},height{};bool dynamic{},alpha{}; };
struct EffectBindings { std::uint64_t generation{~0ull};EffectKind kind{EffectKind::None};std::array<EffectBank,2> banks{}; };
struct ParticleGPU { Matrix model; Color color; std::array<float,3> flags; };
struct WarpGPU {GLuint texture{},offsetBuffer{},offsetTexture{};std::uint32_t width{},height{};std::uint64_t serial{~0ull};int phase{-1};float range{};bool mipsValid{};};
struct SkyGPU {GLuint texture{};std::uint32_t frames{};};
GLint effective_mip_filter(double mode){return mode<1?GL_LINEAR:mode<=2?GL_LINEAR_MIPMAP_NEAREST:GL_LINEAR_MIPMAP_LINEAR;}
// Single-level clamped 2D render target with its own framebuffer (depth attachments draw no color).
void render_target(GLuint&fbo,GLuint&texture,GLint format,GLenum pixels,GLenum type,int w,int h,GLenum attachment=GL_COLOR_ATTACHMENT0){
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,pixels,type,nullptr);
    for(auto [name,value]:{std::pair{GL_TEXTURE_MIN_FILTER,GL_NEAREST},{GL_TEXTURE_MAG_FILTER,GL_NEAREST},{GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE},{GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE}})glTexParameteri(GL_TEXTURE_2D,name,value);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,attachment,GL_TEXTURE_2D,texture,0);
    if(attachment!=GL_COLOR_ATTACHMENT0){glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);}
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete OpenGL effect framebuffer");
}
bool blended(const DrawInstance&d){bool transparent=d.alpha<1||d.additive||d.translucent||d.flare;if(d.model)for(const auto&material:d.model->materials)transparent|=material.alpha<1;return transparent;}
GLint uniform(GLuint program,const char*name){return glGetUniformLocation(program,name);}
} // namespace
struct GLBackend::Impl {
    SDL_Window* window{};SDL_GLContext context{};bool portalPass{};
    RenderOptions options;GLuint scene{},ui{},post{},white{},uiVao{},uiVbo{},postVao{},lightBuffer{},lightTexture{},gammaTexture{},sceneSampler{},uiNearestSampler{},uiLinearSampler{};
    GLuint particleVbo{},lightmapSampler{},dynamicSceneSampler{};GLint maxTextureSize{};
    GLuint msaaFbo{},msaaColor{},msaaDepth{},resolveFbo{},resolveColor{};int targetWidth{},targetHeight{},samples{1};float preferredAnisotropy{1},anisotropy{1};
    GLint minificationFilter{GL_LINEAR_MIPMAP_LINEAR};bool mipsEnabled{true};
    GLuint alphaFbo{},alphaColor{},alphaDepth{};
    GLuint portalFbo{},portalColor{},portalDepth{};
    // PBR/composite (composite_enabled): background views render to an MSAA RGBA16F target, resolved, bloomed, tone mapped
    // (PBR only) and FXAA'd into the LDR scene before UI/foreground.
    GLuint hdrFbo{},hdrColor{},hdrDepth{},hdrResolveFbo{},hdrResolveColor{},tonemapProgram{},flatNormal{},defaultOrm{};GLint tonemapImage{};
    bool composite{},shadowMapsOn{},shadowsReady{},casterPass{};int shadowLight{-1};
    // Shadow casters: the world's ~thousands of WMB batches reduce to one multi-draw per alpha-rejection state, grouped by
    // the first caster pass of a frame and replayed by the other faces (casterWorld marks the world draw).
    struct CasterGroup{GLuint texture,sampler;bool textureAlpha;Color tint;float alpha;Vec2 uvScale,uvOffset;std::vector<GLsizei>counts;std::vector<const void*>offsets;};
    std::vector<CasterGroup> worldCasters;bool casterWorld{},worldCastersReady{};
    void draw_world_casters(){
        for(const auto&g:worldCasters){
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g.texture);glBindSampler(0,g.sampler);glUniform1i(su.textureAlpha,g.textureAlpha);
            glUniform4f(su.tint,g.tint.r,g.tint.g,g.tint.b,g.tint.a);glUniform1f(su.alpha,g.alpha);glUniform2f(su.uvScale,g.uvScale.x,g.uvScale.y);glUniform2f(su.uvOffset,g.uvOffset.x,g.uvOffset.y);
            glMultiDrawElements(GL_TRIANGLES,g.counts.data(),GL_UNSIGNED_INT,g.offsets.data(),GLsizei(g.counts.size()));
        }
    }
    GLuint compositeProgram{},bloomProgram{},fxaaProgram{},linearizeProgram{},ssaoProgram{},aoApplyProgram{},fogProgram{};
    std::array<GLuint,5> bloomFbo{},bloomTexture{};GLuint ldrFbo{},ldrColor{};
    // Primary-view screen effects (SSAO, volumetric fog), sized to that view.
    GLuint effectDepthFbo{},effectDepth{},linearFbo{},linearDepth{},aoFbo{},aoTexture{};int effectWidth{},effectHeight{};
    GLuint sunShadowFbo{},sunShadowMap{},cubeShadowFbo{},cubeShadowMap{};
    bool retainedColorValid{},portalColorValid{},finalFrameValid{};
    GLuint videoTexture{};int videoWidth{},videoHeight{};std::uint64_t videoSerial{~0ull};
    std::unordered_map<std::uint32_t,EffectBindings> effectBindings;
    std::unordered_map<const Model*,MeshGPU> meshes;
    std::unordered_map<std::uint32_t,MeshOverride> overrides;
    ColorAdjustment gamma{};
    bool gammaUploaded{};
    std::unordered_map<const Image*,std::array<TextureGPU,3>> textures;
    std::unordered_map<const Image*,WarpGPU> warps;
    std::unordered_set<const Image*> turbulent; // Never pack-replaced: the native warp table addresses original texels.
    std::vector<std::int32_t> turbulenceOffsets;
    const RenderFrame* currentFrame{};
    Matrix viewProjection{};
    std::array<float,4> geometryClipPlane{};
    GLuint warpProgram{},warpFbo{};GLint warpOriginal{},warpOffsets{},warpSize{},warpStride{};
    std::unordered_map<const Image*,SkyGPU> skies;
    bool maskPass{};
    GLuint shadowProgram{};
    struct ShadowUniforms {GLint model,projection,vp,poses,frame0,frame1,vertexCount,interpolation,firstOverride,nextOverride,clipPlane,viewport,areaThreshold,alpha,reversed,cameraPosition,fogColor,fogRange,noFog;} shu{};
    std::array<GLsync,2> completion{};unsigned completionFront{},completionCount{},completionLimit{1};
    std::uint64_t completionSubmissions{},completionWaits{};
    int requestedSwapInterval{-1};
    bool centroidLighting{};
    GLuint skyProgram{},cloudProgram{},skyClampSampler{};
    struct SkyUniforms{GLint image,basis,cone,scroll,cube,blend,curve,clip,layer;}skyUniforms{};
    GLint cloudOriginal{},cloudLevel{},cloudLayer{},cloudAlpha{};
    std::vector<UIVertex> uiVertices;
    std::size_t uiCapacity{};
    std::vector<std::array<float,4>> lightData;
    std::size_t lightCapacity{};
    std::vector<Vec2> lightCones;
    std::vector<const DrawInstance*> shadowOrder;
    std::vector<const DrawInstance*> transparent;
    std::vector<const Particle*> particleOrder;
    std::vector<ParticleGPU> particleData;
    std::size_t particleCapacity{};
    struct SceneUniforms {
        GLint model,normalMatrix,vp,poses,frame0,frame1,vertexCount,interpolation,skin,lightmap,lights,lightCount,spriteFrames;
        GLint firstOverride,nextOverride,firstNormalOverride,nextNormalOverride,clipPlane;
        GLint portalImage,portalProjection,portalViewport,portalPlane,portalEnabled,reflectionWeight;
        GLint hasLightmap,unlit,flare,overlay,invertFlare,textureAlpha,tint,sunDirection,cameraPosition,fogColor,uvScale,uvOffset,fogRange,alpha,materialAmbient;
        GLint normalOrientation,noFog;
        GLint particleInstanced;
        GLint surfaceAmbient,skyMask;
        GLint effectKind,normalSkin,normalUvScale,skillMain,skillExtra,preparedAmbient,explicitMaterial,preparedDiffuse,preparedSpecular,preparedEmissive,materialPower,ambientLight,materialSunStrength;
        GLint worldVertexLighting;
        GLint infiniteViewer,selectedLight;
        GLint pbr,toneInShader,roughnessMap,legacyRoughness,pbrNormal,pbrOrm;
        GLint shadowMaps,casterPass,shadowLight,sunShadowMatrix,sunShadowTexel,sunShadowMap,cubeShadowMap;
    } su{};
    GLint uiViewport{},uiImage{},uiOverlay{},uiTextKey{},uiCursorKey{},uiAlphaRef{},postImage{},postRamp{},postEnabled{};
    explicit Impl(SDL_Window* w,RenderOptions o):window(w),options(std::move(o)) {
        try {
            context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
            if(!epoxy_is_desktop_gl()||epoxy_gl_version()<33)throw std::runtime_error("Kabus22 requires desktop OpenGL3.3");
            if(SDL_GL_SetSwapInterval(options.vsync?1:0)!=0)std::cerr<<"kabus22: warning: SDL_GL_SetSwapInterval("<<(options.vsync?1:0)<<") failed: "<<SDL_GetError()<<'\n';
            requestedSwapInterval=options.vsync?1:0;
            std::cerr<<"kabus22: GL_VENDOR="<<reinterpret_cast<const char*>(glGetString(GL_VENDOR))<<"\nGL_RENDERER="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\nGL_VERSION="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<"\nSDL_GL_GetSwapInterval="<<SDL_GL_GetSwapInterval()<<'\n';
            GLint maxSamples{};glGetIntegerv(GL_MAX_SAMPLES,&maxSamples);samples=std::clamp(options.samples,1,maxSamples);
            if(options.samples>maxSamples)std::cerr<<"kabus22: graphics: MSAA "<<options.samples<<" unsupported, using "<<maxSamples<<'\n';
            glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTextureSize);
            if(epoxy_gl_version()>=46||epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic")||epoxy_has_gl_extension("GL_ARB_texture_filter_anisotropic")){GLfloat maximum{};glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&maximum);preferredAnisotropy=anisotropy=std::clamp(options.anisotropy,1.0f,maximum);if(options.anisotropy>maximum)std::cerr<<"kabus22: graphics: anisotropy "<<options.anisotropy<<" unsupported, using "<<maximum<<'\n';}
            else if(options.forceAnisotropy&&options.anisotropy>1)std::cerr<<"kabus22: graphics: anisotropic filtering unsupported by this OpenGL context; disabled\n";
            const auto&fx=options.features;composite=composite_enabled(options.pbr,fx);shadowMapsOn=shadow_maps_enabled(fx);
            const std::string shadowDefine=shadowMapsOn?"#define K22_SHADOW_MAPS 1\n":"";
            scene=program(("#version 330 core\n"+shadowDefine+sceneVS).c_str(),(std::string(sceneFSPrelude)+(shadowMapsOn?shadowDefine+effectsGLSL+"float lightShadow(int i,vec3 p,vec3 n);\n#define K22_PBR_LIGHT_VISIBILITY(i) lightShadow(i,p,Ng)\n":std::string())+pbrGLSL+sceneFS).c_str());ui=program(uiVS,uiFS);post=program(postVS,postFS);
            tonemapProgram=program(postVS,(std::string(tonemapFSPrelude)+pbrGLSL+tonemapFS).c_str());tonemapImage=glGetUniformLocation(tonemapProgram,"image");
            auto effect=[&](const char*fs){return program(postVS,(std::string(tonemapFSPrelude)+pbrGLSL+effectsGLSL+fs).c_str());};
            if(composite&&(fx.bloom||!options.pbr))compositeProgram=effect(compositeFS);
            if(fx.bloom)bloomProgram=effect(bloomFS);
            if(fx.fxaa)fxaaProgram=effect(fxaaFS);
            if(fx.ssao||fx.fog)linearizeProgram=effect(linearizeFS);
            if(fx.ssao){ssaoProgram=effect(ssaoFS);aoApplyProgram=effect(aoApplyFS);}
            if(fx.fog)fogProgram=program(postVS,(std::string(fogFSPrelude)+effectsGLSL+fogFS).c_str());
            shadowProgram=program(shadowVS,shadowFS,shadowGS);
#define S(field,name) shu.field=glGetUniformLocation(shadowProgram,name)
            S(model,"model");S(projection,"projection");S(vp,"viewProjection");S(poses,"poses");S(frame0,"frame0");S(frame1,"frame1");S(vertexCount,"vertexCount");S(interpolation,"interpolation");S(firstOverride,"firstOverride");S(nextOverride,"nextOverride");S(clipPlane,"clipPlane");S(viewport,"viewport");S(areaThreshold,"areaThreshold");S(alpha,"alpha");S(reversed,"reversed");
            S(cameraPosition,"cameraPosition");S(fogColor,"fogColor");S(fogRange,"fogRange");S(noFog,"noFog");
#undef S
            warpProgram=program(postVS,warpFS);warpOriginal=glGetUniformLocation(warpProgram,"original");warpOffsets=glGetUniformLocation(warpProgram,"offsets");warpSize=glGetUniformLocation(warpProgram,"size");warpStride=glGetUniformLocation(warpProgram,"stride");
            skyProgram=program(postVS,skyFS);cloudProgram=program(postVS,cloudFS);
            skyUniforms={glGetUniformLocation(skyProgram,"image"),glGetUniformLocation(skyProgram,"basis"),glGetUniformLocation(skyProgram,"cone"),glGetUniformLocation(skyProgram,"scroll"),glGetUniformLocation(skyProgram,"cube"),glGetUniformLocation(skyProgram,"blend"),glGetUniformLocation(skyProgram,"curve"),glGetUniformLocation(skyProgram,"clip"),glGetUniformLocation(skyProgram,"layer")};
            cloudOriginal=glGetUniformLocation(cloudProgram,"original");cloudLevel=glGetUniformLocation(cloudProgram,"level");cloudLayer=glGetUniformLocation(cloudProgram,"layer");cloudAlpha=glGetUniformLocation(cloudProgram,"convertAlpha");
#define U(field,name) su.field=glGetUniformLocation(scene,name)
            U(model,"model");U(vp,"viewProjection");U(poses,"poses");U(frame0,"frame0");U(frame1,"frame1");U(vertexCount,"vertexCount");U(interpolation,"interpolation");U(skin,"skin");U(lightmap,"lightmap");U(lights,"lights");U(lightCount,"lightCount");U(hasLightmap,"hasLightmap");U(unlit,"unlit");U(tint,"tint");U(sunDirection,"sunDirection");U(cameraPosition,"cameraPosition");U(fogColor,"fogColor");U(uvScale,"uvScale");U(uvOffset,"uvOffset");U(fogRange,"fogRange");U(alpha,"alpha");U(materialAmbient,"materialAmbient");
            U(normalMatrix,"normalMatrix");U(flare,"flare");U(overlay,"overlay");
            U(spriteFrames,"spriteFrames");U(invertFlare,"invertFlare");U(textureAlpha,"textureAlpha");
            U(firstOverride,"firstOverride");U(nextOverride,"nextOverride");U(firstNormalOverride,"firstNormalOverride");U(nextNormalOverride,"nextNormalOverride");U(clipPlane,"clipPlane");
            U(portalImage,"portalImage");U(portalProjection,"portalProjection");U(portalViewport,"portalViewport");U(portalPlane,"portalPlane");U(portalEnabled,"portalEnabled");U(reflectionWeight,"reflectionWeight");
            U(normalOrientation,"normalOrientation");U(noFog,"noFog");
            U(particleInstanced,"particleInstanced");
            U(surfaceAmbient,"surfaceAmbient");U(skyMask,"skyMask");
            U(effectKind,"effectKind");U(normalSkin,"normalSkin");U(normalUvScale,"normalUvScale");U(skillMain,"skillMain");U(skillExtra,"skillExtra");U(preparedAmbient,"preparedAmbient");U(explicitMaterial,"explicitMaterial");U(preparedDiffuse,"preparedDiffuse");U(preparedSpecular,"preparedSpecular");U(preparedEmissive,"preparedEmissive");U(materialPower,"materialPower");
            U(ambientLight,"ambientLight");U(materialSunStrength,"materialSunStrength");
            U(worldVertexLighting,"worldVertexLighting");
            U(infiniteViewer,"infiniteViewer");U(selectedLight,"selectedLight");
            U(pbr,"pbr");U(toneInShader,"toneInShader");U(roughnessMap,"roughnessMap");U(legacyRoughness,"legacyRoughness");U(pbrNormal,"pbrNormal");U(pbrOrm,"pbrOrm");
            U(shadowMaps,"shadowMaps");U(casterPass,"casterPass");U(shadowLight,"shadowLight");U(sunShadowMatrix,"sunShadowMatrix");U(sunShadowTexel,"sunShadowTexel");U(sunShadowMap,"sunShadowMap");U(cubeShadowMap,"cubeShadowMap");
#undef U
            if(shadowMapsOn){glUseProgram(scene);glUniform1i(su.sunShadowMap,8);glUniform1i(su.cubeShadowMap,9);} // Distinct units from the 2D samplers.
            uiViewport=glGetUniformLocation(ui,"viewport");uiImage=glGetUniformLocation(ui,"image");uiOverlay=glGetUniformLocation(ui,"overlay");uiAlphaRef=glGetUniformLocation(ui,"alphaRef");postImage=glGetUniformLocation(post,"image");postRamp=glGetUniformLocation(post,"ramp");postEnabled=glGetUniformLocation(post,"enabled");
            uiTextKey=glGetUniformLocation(ui,"textKey");
            uiCursorKey=glGetUniformLocation(ui,"cursorKey");
            glGenTextures(1,&white);glBindTexture(GL_TEXTURE_2D,white);const std::uint8_t pixel[4]={255,255,255,255};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            for(auto [id,value]:{std::pair{&flatNormal,std::array<std::uint8_t,4>{128,128,255,255}},std::pair{&defaultOrm,std::array<std::uint8_t,4>{255,255,0,255}}}){glGenTextures(1,id);glBindTexture(GL_TEXTURE_2D,*id);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,value.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
            glGenVertexArrays(1,&uiVao);glGenBuffers(1,&uiVbo);glBindVertexArray(uiVao);glBindBuffer(GL_ARRAY_BUFFER,uiVbo);
            glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(UIVertex),(void*)0);glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(UIVertex),(void*)(2*sizeof(float)));glEnableVertexAttribArray(2);glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(UIVertex),(void*)(4*sizeof(float)));glEnableVertexAttribArray(3);glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,sizeof(UIVertex),(void*)(8*sizeof(float)));
            glGenVertexArrays(1,&postVao);glGenBuffers(1,&lightBuffer);glGenTextures(1,&lightTexture);glBindTexture(GL_TEXTURE_BUFFER,lightTexture);glBindBuffer(GL_TEXTURE_BUFFER,lightBuffer);glBufferData(GL_TEXTURE_BUFFER,sizeof(float)*8,nullptr,GL_STREAM_DRAW);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,lightBuffer);
            glGenTextures(1,&gammaTexture);glBindTexture(GL_TEXTURE_2D,gammaTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_R16,256,3,0,GL_RED,GL_UNSIGNED_SHORT,gamma.gammaRamp.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glGenSamplers(1,&sceneSampler);glSamplerParameteri(sceneSampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glSamplerParameteri(sceneSampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);if(anisotropy>1)glSamplerParameterf(sceneSampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,anisotropy);
            glGenSamplers(1,&lightmapSampler);glSamplerParameteri(lightmapSampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glSamplerParameteri(lightmapSampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glSamplerParameteri(lightmapSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(lightmapSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);if(anisotropy>1)glSamplerParameterf(lightmapSampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,anisotropy);
            glGenSamplers(1,&dynamicSceneSampler);glSamplerParameteri(dynamicSceneSampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glSamplerParameteri(dynamicSceneSampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);if(anisotropy>1)glSamplerParameterf(dynamicSceneSampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,anisotropy);
            glGenSamplers(1,&skyClampSampler);glSamplerParameteri(skyClampSampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glSamplerParameteri(skyClampSampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glSamplerParameteri(skyClampSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(skyClampSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);if(anisotropy>1)glSamplerParameterf(skyClampSampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,anisotropy);
            glGenBuffers(1,&particleVbo);particleOrder.reserve(1024);particleData.reserve(1024);
            glGenSamplers(1,&uiNearestSampler);glSamplerParameteri(uiNearestSampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glSamplerParameteri(uiNearestSampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glSamplerParameteri(uiNearestSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(uiNearestSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glGenSamplers(1,&uiLinearSampler);glSamplerParameteri(uiLinearSampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glSamplerParameteri(uiLinearSampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glSamplerParameteri(uiLinearSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(uiLinearSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            uiVertices.reserve(4096);lightData.reserve(128);transparent.reserve(256);
        }catch(...){destroy();throw;}
    }
    bool retire_completion(){
        if(!completionCount)return true;
        auto&fence=completion[completionFront];GLenum status;
        do {status=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000ull);}while(status==GL_TIMEOUT_EXPIRED);
        glDeleteSync(fence);fence=nullptr;completionFront=(completionFront+1)%completion.size();--completionCount;
        if(status==GL_WAIT_FAILED)return false;
        ++completionWaits;return true;
    }
    void drain_completion(bool fail=true){
        while(completionCount)if(!retire_completion()){
            if(fail)throw std::runtime_error("OpenGL complete-frame fence wait failed");
            std::cerr<<"kabus22: complete-frame fence wait failed during teardown\n";
        }
    }
    void admit_frame(std::int32_t raw,bool bonus){
        completionLimit=presentation_limit(options.mode,raw);
        while(completionCount>=completionLimit)if(!retire_completion())throw std::runtime_error("OpenGL complete-frame fence wait failed");
        const int interval=presentation_interval(options.mode,raw,bonus,options.vsync);
        if(interval!=requestedSwapInterval){
            requestedSwapInterval=interval;
            if(SDL_GL_SetSwapInterval(interval)!=0)std::cerr<<"kabus22: warning: SDL_GL_SetSwapInterval("<<interval<<") failed: "<<SDL_GetError()<<'\n';
        }
    }
    void complete_frame(){
        auto&fence=completion[(completionFront+completionCount)%completion.size()];
        fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)throw std::runtime_error("Cannot fence completed OpenGL frame");
        ++completionCount;++completionSubmissions;glFlush();
    }
    void destroy() {
        if(context){SDL_GL_MakeCurrent(window,context);drain_completion(false);for(auto&[key,m]:meshes){glDeleteVertexArrays(1,&m.vao);glDeleteBuffers(1,&m.vertices);glDeleteBuffers(1,&m.indices);glDeleteBuffers(1,&m.tangents);glDeleteBuffers(1,&m.poseBuffer);glDeleteTextures(1,&m.poses);}for(auto&[key,variants]:textures)for(auto&t:variants){glDeleteTextures(1,&t.id);glDeleteTextures(1,&t.normal);glDeleteTextures(1,&t.orm);}
            for(auto&[key,m]:overrides){glDeleteBuffers(1,&m.positions);glDeleteBuffers(1,&m.normals);glDeleteBuffers(1,&m.tangents);}glDeleteTextures(1,&gammaTexture);glDeleteSamplers(1,&sceneSampler);glDeleteSamplers(1,&uiNearestSampler);glDeleteSamplers(1,&uiLinearSampler);
            glDeleteBuffers(1,&particleVbo);glDeleteSamplers(1,&lightmapSampler);
            glDeleteSamplers(1,&dynamicSceneSampler);
            for(auto&[image,warp]:warps){glDeleteTextures(1,&warp.texture);glDeleteTextures(1,&warp.offsetTexture);glDeleteBuffers(1,&warp.offsetBuffer);}glDeleteProgram(warpProgram);glDeleteFramebuffers(1,&warpFbo);
            for(auto&[image,sky]:skies)glDeleteTextures(1,&sky.texture);glDeleteProgram(skyProgram);glDeleteProgram(cloudProgram);glDeleteSamplers(1,&skyClampSampler);
            glDeleteProgram(shadowProgram);
            glDeleteProgram(tonemapProgram);glDeleteTextures(1,&flatNormal);glDeleteTextures(1,&defaultOrm);
            for(GLuint p:{compositeProgram,bloomProgram,fxaaProgram,linearizeProgram,ssaoProgram,aoApplyProgram,fogProgram})glDeleteProgram(p);
            glDeleteFramebuffers(1,&sunShadowFbo);glDeleteTextures(1,&sunShadowMap);glDeleteFramebuffers(1,&cubeShadowFbo);glDeleteTextures(1,&cubeShadowMap);
            glDeleteProgram(scene);glDeleteProgram(ui);glDeleteProgram(post);glDeleteTextures(1,&white);glDeleteTextures(1,&videoTexture);glDeleteVertexArrays(1,&uiVao);glDeleteVertexArrays(1,&postVao);glDeleteBuffers(1,&uiVbo);glDeleteBuffers(1,&lightBuffer);glDeleteTextures(1,&lightTexture);free_targets();SDL_GL_DeleteContext(context);context=nullptr;}
    }
    ~Impl(){destroy();}
    void configure_sampling(double mipmapping,double enabledAnisotropy){
        GLint filter=effective_mip_filter(mipmapping);
        if(filter!=minificationFilter){
            for(GLuint sampler:{sceneSampler,lightmapSampler,skyClampSampler})glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,filter);
            minificationFilter=filter;mipsEnabled=filter!=GL_LINEAR;
        }
        float factor=effective_anisotropy(options.forceAnisotropy?1:enabledAnisotropy,preferredAnisotropy);
        if(factor!=anisotropy){
            if(preferredAnisotropy>1)for(GLuint sampler:{sceneSampler,lightmapSampler,skyClampSampler,dynamicSceneSampler})glSamplerParameterf(sampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,factor);
            anisotropy=factor;
        }
    }
    void free_targets(){
        drain_completion();
        glDeleteFramebuffers(1,&msaaFbo);glDeleteRenderbuffers(1,&msaaColor);glDeleteRenderbuffers(1,&msaaDepth);glDeleteFramebuffers(1,&resolveFbo);glDeleteTextures(1,&resolveColor);
        glDeleteFramebuffers(1,&alphaFbo);glDeleteTextures(1,&alphaColor);glDeleteRenderbuffers(1,&alphaDepth);
        glDeleteFramebuffers(1,&portalFbo);glDeleteTextures(1,&portalColor);glDeleteRenderbuffers(1,&portalDepth);portalFbo=portalColor=portalDepth=0;
        glDeleteFramebuffers(1,&hdrFbo);glDeleteRenderbuffers(1,&hdrColor);glDeleteRenderbuffers(1,&hdrDepth);glDeleteFramebuffers(1,&hdrResolveFbo);glDeleteTextures(1,&hdrResolveColor);hdrFbo=hdrColor=hdrDepth=hdrResolveFbo=hdrResolveColor=0;
        glDeleteFramebuffers(5,bloomFbo.data());glDeleteTextures(5,bloomTexture.data());bloomFbo={};bloomTexture={};glDeleteFramebuffers(1,&ldrFbo);glDeleteTextures(1,&ldrColor);ldrFbo=ldrColor=0;
        free_effects();
        msaaFbo=msaaColor=msaaDepth=resolveFbo=resolveColor=alphaFbo=alphaColor=alphaDepth=0;
        retainedColorValid=portalColorValid=finalFrameValid=false;targetWidth=targetHeight=0;
    }
    void free_effects(){
        for(GLuint*fbo:{&effectDepthFbo,&linearFbo,&aoFbo})glDeleteFramebuffers(1,fbo);
        for(GLuint*texture:{&effectDepth,&linearDepth,&aoTexture})glDeleteTextures(1,texture);
        effectDepthFbo=linearFbo=aoFbo=effectDepth=linearDepth=aoTexture=0;effectWidth=effectHeight=0;
    }
    void targets(int w,int h){if(w==targetWidth&&h==targetHeight)return;free_targets();targetWidth=w;targetHeight=h;
        glGenFramebuffers(1,&msaaFbo);glBindFramebuffer(GL_FRAMEBUFFER,msaaFbo);glGenRenderbuffers(1,&msaaColor);glBindRenderbuffer(GL_RENDERBUFFER,msaaColor);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,msaaColor);glGenRenderbuffers(1,&msaaDepth);glBindRenderbuffer(GL_RENDERBUFFER,msaaDepth);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH24_STENCIL8,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,msaaDepth);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete multisample game framebuffer");
        glGenFramebuffers(1,&resolveFbo);glBindFramebuffer(GL_FRAMEBUFFER,resolveFbo);glGenTextures(1,&resolveColor);glBindTexture(GL_TEXTURE_2D,resolveColor);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,resolveColor,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete game resolve framebuffer");
        if(!composite)return;
        glGenFramebuffers(1,&hdrFbo);glBindFramebuffer(GL_FRAMEBUFFER,hdrFbo);glGenRenderbuffers(1,&hdrColor);glBindRenderbuffer(GL_RENDERBUFFER,hdrColor);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA16F,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,hdrColor);glGenRenderbuffers(1,&hdrDepth);glBindRenderbuffer(GL_RENDERBUFFER,hdrDepth);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH24_STENCIL8,w,h);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,hdrDepth);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete HDR game framebuffer");
        glGenFramebuffers(1,&hdrResolveFbo);glBindFramebuffer(GL_FRAMEBUFFER,hdrResolveFbo);glGenTextures(1,&hdrResolveColor);glBindTexture(GL_TEXTURE_2D,hdrResolveColor);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_HALF_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,hdrResolveColor,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete HDR resolve framebuffer");
        if(options.features.bloom)for(int level=0;level<5;++level)render_target(bloomFbo[level],bloomTexture[level],GL_RGBA16F,GL_RGBA,GL_HALF_FLOAT,std::max(1,w>>(level+1)),std::max(1,h>>(level+1)));
        if(options.features.fxaa)render_target(ldrFbo,ldrColor,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,w,h);
    }
    // Shadow maps for the primary view: sun depth map and the shadow light's depth cube. Casters reuse the scene program
    // and draw_instance (poses, overrides, alpha-test discard) depth-only, back faces culled from the light: world plus opaque, non-view entities.
    void shadow_pass(const RenderFrame&f,const Camera&camera,float aspect){
        if(!sunShadowFbo){
            auto compare=[](GLenum target){for(auto [name,value]:{std::pair{GL_TEXTURE_MIN_FILTER,GL_LINEAR},{GL_TEXTURE_MAG_FILTER,GL_LINEAR},{GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE},{GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE},{GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE},{GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE},{GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL}})glTexParameteri(target,name,value);};
            render_target(sunShadowFbo,sunShadowMap,GL_DEPTH_COMPONENT32F,GL_DEPTH_COMPONENT,GL_FLOAT,sunShadowResolution,sunShadowResolution,GL_DEPTH_ATTACHMENT);compare(GL_TEXTURE_2D);
            glGenTextures(1,&cubeShadowMap);glBindTexture(GL_TEXTURE_CUBE_MAP,cubeShadowMap);
            for(int face=0;face<6;++face)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_DEPTH_COMPONENT32F,cubeShadowResolution,cubeShadowResolution,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
            compare(GL_TEXTURE_CUBE_MAP);glGenFramebuffers(1,&cubeShadowFbo);glBindFramebuffer(GL_FRAMEBUFFER,cubeShadowFbo);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
        }
        const Matrix sun=sun_shadow_matrix(camera,aspect,f.sunDirection,f.world?mesh(f.world->mesh).bounds:Bounds{},sunShadowDistance,sunShadowResolution);
        // Lights often sit inside lamp/pole geometry: the cube near plane (cubeShadowNear) clips those fixtures out.
        shadowLight=shadow_light_index(f.lights,camera.position);
        glUseProgram(scene);glUniform1i(su.casterPass,1);glUniform1i(su.shadowMaps,0);glUniform1i(su.lightCount,0);glUniform1i(su.skyMask,0);
        glUniform1i(su.skin,0);glUniform1i(su.lightmap,1);glUniform1i(su.poses,2);glUniform1i(su.pbr,0);glBindSampler(0,sceneSampler);glBindSampler(1,lightmapSampler);
        glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE9);glBindTexture(GL_TEXTURE_CUBE_MAP,0);
        geometryClipPlane={};glUniform4fv(su.clipPlane,1,geometryClipPlane.data());portalPass=false;casterPass=true;worldCasters.clear();worldCastersReady=false;
        glDisable(GL_SCISSOR_TEST);glDisable(GL_CLIP_DISTANCE0);glDisable(GL_BLEND);glDisable(GL_STENCIL_TEST);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glCullFace(GL_BACK);glDepthMask(GL_TRUE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        auto casters=[&](const Matrix&vp,int size){
            glViewport(0,0,size,size);glClear(GL_DEPTH_BUFFER_BIT);viewProjection=vp;glUniformMatrix4fv(su.vp,1,GL_FALSE,vp.data());
            if(camera.drawWorld&&f.world&&(camera.layers&1)){DrawInstance world;world.model=&f.world->mesh;casterWorld=true;draw_instance(world,f.skinBindings,&camera);casterWorld=false;}
            if(f.renderEntities&&!camera.noEntities)for(const auto&d:f.instances){
                if(!d.visible||!d.model||d.viewSpace||d.billboard||!(d.layers&camera.layers)||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
                if(camera.perDrawOpacity&&model_has_entity_skins(*d.model)){if(model_queue(d,f.skinBindings)&1)draw_instance(d,f.skinBindings,&camera,true,0);}
                else if(!blended(d))draw_instance(d,f.skinBindings,&camera);
            }
        };
        glBindFramebuffer(GL_FRAMEBUFFER,sunShadowFbo);casters(sun,sunShadowResolution);
        if(shadowLight>=0){
            const auto&light=f.lights[shadowLight];glBindFramebuffer(GL_FRAMEBUFFER,cubeShadowFbo);
            for(int face=0;face<6;++face){glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,cubeShadowMap,0);casters(cube_face_matrix(light.position,face,cubeShadowNear,light.range),cubeShadowResolution);}
        }
        casterPass=false;glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_BLEND);
        // Ortho rows are scaled by 1/radius: one shadow texel spans 2*radius/resolution world units.
        const float radius=1/std::sqrt(sun[0]*sun[0]+sun[4]*sun[4]+sun[8]*sun[8]);
        glUniform1i(su.casterPass,0);glUniform1i(su.shadowMaps,1);glUniform1i(su.shadowLight,shadowLight);glUniformMatrix4fv(su.sunShadowMatrix,1,GL_FALSE,sun.data());glUniform1f(su.sunShadowTexel,2*radius/sunShadowResolution);
        glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,sunShadowMap);glBindSampler(8,0);glActiveTexture(GL_TEXTURE9);glBindTexture(GL_TEXTURE_CUBE_MAP,cubeShadowMap);glBindSampler(9,0);
        shadowsReady=true;
    }
    // Primary-view SSAO and volumetric fog, before view-space entities clear depth. Depth is copied to a single-sample
    // texture and linearized; AO multiplies into the view color, fog in-scattering adds to it (HDR before tone mapping).
    void screen_effects(const RenderFrame&f,const Camera&camera,int x,int y,int w,int h){
        const auto&fx=options.features;GLint destination{};glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&destination);
        if(effectWidth!=w||effectHeight!=h){
            free_effects();effectWidth=w;effectHeight=h;
            render_target(effectDepthFbo,effectDepth,GL_DEPTH24_STENCIL8,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,w,h,GL_DEPTH_STENCIL_ATTACHMENT);
            render_target(linearFbo,linearDepth,GL_R32F,GL_RED,GL_FLOAT,w,h);
            if(fx.ssao)render_target(aoFbo,aoTexture,GL_R8,GL_RED,GL_UNSIGNED_BYTE,w,h);
        }
        const bool clip=glIsEnabled(GL_CLIP_DISTANCE0);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_CLIP_DISTANCE0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,destination);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,effectDepthFbo);glBlitFramebuffer(x,y,x+w,y+h,0,0,w,h,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
        glBindVertexArray(postVao);glBindFramebuffer(GL_FRAMEBUFFER,linearFbo);glViewport(0,0,w,h);glUseProgram(linearizeProgram);
        glActiveTexture(GL_TEXTURE10);glBindTexture(GL_TEXTURE_2D,effectDepth);glBindSampler(10,uiNearestSampler);
        glUniform1i(uniform(linearizeProgram,"depth"),10);glUniform1f(uniform(linearizeProgram,"nearPlane"),camera.nearClip);glUniform1f(uniform(linearizeProgram,"farPlane"),camera.farClip);glDrawArrays(GL_TRIANGLES,0,3);
        glBindTexture(GL_TEXTURE_2D,linearDepth);
        const Matrix inverseVP=inverse(viewProjection),basis=transform({},camera.angles,{1,1,1});
        auto view=[&](GLuint p){glUseProgram(p);glUniform1i(uniform(p,"linearDepth"),10);glUniformMatrix4fv(uniform(p,"inverseViewProjection"),1,GL_FALSE,inverseVP.data());glUniform3f(uniform(p,"eye"),camera.position.x,camera.position.y,camera.position.z);glUniform3f(uniform(p,"forward"),basis[0],basis[1],basis[2]);};
        if(fx.ssao){
            glBindFramebuffer(GL_FRAMEBUFFER,aoFbo);view(ssaoProgram);glUniformMatrix4fv(uniform(ssaoProgram,"viewProjection"),1,GL_FALSE,viewProjection.data());glUniform2f(uniform(ssaoProgram,"texel"),1.f/w,1.f/h);glDrawArrays(GL_TRIANGLES,0,3);
            glBindFramebuffer(GL_FRAMEBUFFER,destination);glViewport(x,y,w,h);glUseProgram(aoApplyProgram);glActiveTexture(GL_TEXTURE11);glBindTexture(GL_TEXTURE_2D,aoTexture);glBindSampler(11,uiLinearSampler);
            glUniform1i(uniform(aoApplyProgram,"ao"),11);glUniform2f(uniform(aoApplyProgram,"texel"),1.f/w,1.f/h);glEnable(GL_BLEND);glBlendFuncSeparate(GL_ZERO,GL_SRC_COLOR,GL_ZERO,GL_ONE);glDrawArrays(GL_TRIANGLES,0,3);glDisable(GL_BLEND);
        }
        if(fx.fog){
            glBindFramebuffer(GL_FRAMEBUFFER,destination);glViewport(x,y,w,h);view(fogProgram);
            glUniform1i(uniform(fogProgram,"lights"),3);glUniform1i(uniform(fogProgram,"lightCount"),lightData.size()/5);glUniform1i(uniform(fogProgram,"cubeShadowMap"),9);
            glUniform1i(uniform(fogProgram,"shadowMaps"),shadowsReady&&shadowLight>=0);glUniform1i(uniform(fogProgram,"shadowLight"),shadowLight);glUniform1f(uniform(fogProgram,"farPlane"),camera.farClip);
            glUniform2f(uniform(fogProgram,"fogRange"),camera.environmentOverride?camera.fogStart:f.fogStart,camera.environmentOverride?camera.fogEnd:f.fogEnd);
            glEnable(GL_BLEND);glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ZERO,GL_ONE);glDrawArrays(GL_TRIANGLES,0,3);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,destination);glViewport(x,y,w,h);glEnable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);if(clip)glEnable(GL_CLIP_DISTANCE0);glUseProgram(scene);
    }
    // Background composite: HDR resolve, bloom, tone map (PBR only, else identity) and FXAA into the LDR scene target.
    void composite_pass(int w,int h){
        const auto&fx=options.features;post_state();
        glBindFramebuffer(GL_READ_FRAMEBUFFER,hdrFbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,hdrResolveFbo);glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,msaaFbo);glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT,GL_NEAREST);
        glBindVertexArray(postVao);glActiveTexture(GL_TEXTURE0);
        if(fx.bloom){
            // Prefiltered half-resolution level, four more downsamples, then tent upsamples accumulated back into level 0.
            glUseProgram(bloomProgram);glUniform1i(uniform(bloomProgram,"image"),0);glBindSampler(0,uiLinearSampler);
            auto size=[&](int level){return std::array<int,2>{std::max(1,w>>(level+1)),std::max(1,h>>(level+1))};};
            auto pass=[&](int target,int source,int mode){
                auto [tw,th]=size(target);auto [sw,sh]=source<0?std::array<int,2>{w,h}:size(source);
                glBindFramebuffer(GL_FRAMEBUFFER,bloomFbo[target]);glViewport(0,0,tw,th);glBindTexture(GL_TEXTURE_2D,source<0?hdrResolveColor:bloomTexture[source]);
                glUniform2f(uniform(bloomProgram,"texel"),1.f/sw,1.f/sh);glUniform1i(uniform(bloomProgram,"mode"),mode);glDrawArrays(GL_TRIANGLES,0,3);
            };
            pass(0,-1,0);for(int level=1;level<5;++level)pass(level,level-1,1);
            glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);for(int level=3;level>=0;--level)pass(level,level+1,2);glDisable(GL_BLEND);glViewport(0,0,w,h);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,fx.fxaa?ldrFbo:msaaFbo);glBindTexture(GL_TEXTURE_2D,hdrResolveColor);glBindSampler(0,uiNearestSampler);
        if(compositeProgram){
            glUseProgram(compositeProgram);glUniform1i(uniform(compositeProgram,"image"),0);glUniform1i(uniform(compositeProgram,"bloom"),1);glUniform1i(uniform(compositeProgram,"bloomEnabled"),fx.bloom);glUniform1i(uniform(compositeProgram,"toneMap"),options.pbr);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,fx.bloom?bloomTexture[0]:white);glBindSampler(1,uiLinearSampler);
        }else{glUseProgram(tonemapProgram);glUniform1i(tonemapImage,0);}
        glDrawArrays(GL_TRIANGLES,0,3);
        if(fx.fxaa){
            glBindFramebuffer(GL_FRAMEBUFFER,msaaFbo);glUseProgram(fxaaProgram);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,ldrColor);glBindSampler(0,uiLinearSampler);
            glUniform1i(uniform(fxaaProgram,"image"),0);glUniform2f(uniform(fxaaProgram,"texel"),1.f/w,1.f/h);glDrawArrays(GL_TRIANGLES,0,3);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,msaaFbo);
    }
    void alpha_target(){
        if(alphaFbo)return;
        glGenFramebuffers(1,&alphaFbo);glBindFramebuffer(GL_FRAMEBUFFER,alphaFbo);glGenTextures(1,&alphaColor);glBindTexture(GL_TEXTURE_2D,alphaColor);
        glTexImage2D(GL_TEXTURE_2D,0,options.pbr?GL_RGBA16F:GL_RGBA8,targetWidth,targetHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,alphaColor,0);
        glGenRenderbuffers(1,&alphaDepth);glBindRenderbuffer(GL_RENDERBUFFER,alphaDepth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,targetWidth,targetHeight);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,alphaDepth);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete translucent game-view framebuffer");
    }
    void portal_target(){
        if(portalFbo)return;
        glGenFramebuffers(1,&portalFbo);glBindFramebuffer(GL_FRAMEBUFFER,portalFbo);glGenTextures(1,&portalColor);glBindTexture(GL_TEXTURE_2D,portalColor);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,targetWidth,targetHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,portalColor,0);
        glGenRenderbuffers(1,&portalDepth);glBindRenderbuffer(GL_RENDERBUFFER,portalDepth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,targetWidth,targetHeight);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,portalDepth);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete reflected game-view framebuffer");
    }
    GLuint sampled_texture(const Image*image,TextureGPU&gpu){
        if(image->nativeStorageBytes==2){
            auto found=warps.find(image);
            if(found!=warps.end()){
                auto&w=found->second;
                if(w.serial==gpu.serial&&w.width==gpu.width&&w.height==gpu.height){
                    if(mipsEnabled&&!gpu.dynamic&&!w.mipsValid){glBindTexture(GL_TEXTURE_2D,w.texture);glGenerateMipmap(GL_TEXTURE_2D);w.mipsValid=true;}
                    return w.texture;
                }
            }
        }
        if(mipsEnabled&&!gpu.dynamic&&!gpu.mipsValid){glBindTexture(GL_TEXTURE_2D,gpu.id);glGenerateMipmap(GL_TEXTURE_2D);gpu.mipsValid=true;}
        return gpu.id;
    }
    void upload_texture(const Image&image,UICommand::TextureMode mode,bool resize){
        const void*pixels=image.rgba.data();GLenum type=GL_UNSIGNED_BYTE;GLint format=GL_RGBA8;
        std::vector<std::uint16_t> packed;
        std::vector<std::uint8_t> keyed;
        if(mode==UICommand::TextureMode::CursorKey16){
            packed.resize(image.rgba.size()/4);
            for(std::size_t i=0;i<packed.size();++i){
                const auto*p=image.rgba.data()+i*4;
                // Original RGB24 -> unusual source R5G6B5, black key BEFORE A1RGB555 conversion.
                const std::uint16_t source=(p[0]>>3)<<11|(p[1]>>3)<<6|(p[1]&1)<<5|(p[2]>>3);
                packed[i]=source?((source&0xffc0)|((source&31)<<1)|1):0;
            }
            pixels=packed.data();type=GL_UNSIGNED_SHORT_5_5_5_1;format=GL_RGB5_A1;
        }else if(mode==UICommand::TextureMode::CursorKey24){
            keyed=image.rgba;
            for(std::size_t i=0;i<keyed.size();i+=4)keyed[i+3]=(keyed[i]|keyed[i+1]|keyed[i+2])?255:0;
            pixels=keyed.data();
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        if(resize)glTexImage2D(GL_TEXTURE_2D,0,format,image.width,image.height,0,GL_RGBA,type,pixels);
        else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,image.width,image.height,GL_RGBA,type,pixels);
    }
    GLuint texture(const Image*image,bool dynamic=false,std::uint64_t serial=0,bool sampled=true,UICommand::TextureMode mode=UICommand::TextureMode::Default){
        if(!image)return white;
        if(image->width>static_cast<std::uint32_t>(maxTextureSize)||image->height>static_cast<std::uint32_t>(maxTextureSize))throw std::runtime_error("Game image exceeds GPU texture size");
        auto&gpu=textures[image][static_cast<std::size_t>(mode)];
        if(gpu.id){
            if(dynamic&&(gpu.serial!=serial||gpu.width!=image->width||gpu.height!=image->height||!gpu.dynamic)){
                check_image(*image);glBindTexture(GL_TEXTURE_2D,gpu.id);
                upload_texture(*image,mode,gpu.width!=image->width||gpu.height!=image->height);
                gpu.serial=serial;gpu.width=image->width;gpu.height=image->height;gpu.dynamic=true;gpu.alpha=true;gpu.mipsValid=false;
            }
            return sampled&&mode==UICommand::TextureMode::Default?sampled_texture(image,gpu):gpu.id;
        }
        check_image(*image);GLuint id{};glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
        // Packs replace immutable Default-mode images only; turbulent surfaces keep their native source for the CPU-table warp.
        auto pack=options.packs&&!dynamic&&mode==UICommand::TextureMode::Default&&!turbulent.contains(image)?options.packs->find(*image):std::nullopt;
        if(pack&&pack->albedo&&(pack->albedo->width>static_cast<std::uint32_t>(maxTextureSize)||pack->albedo->height>static_cast<std::uint32_t>(maxTextureSize))){std::cerr<<"kabus22: texture pack: "<<texture_key_hex(texture_key(*image))<<" exceeds GPU texture size; using original\n";pack.reset();}
        upload_texture(pack&&pack->albedo?*pack->albedo:*image,mode,true);
        bool mipsValid=mipsEnabled&&!dynamic&&mode==UICommand::TextureMode::Default;if(mipsValid)glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        bool alpha=dynamic||mode!=UICommand::TextureMode::Default;for(std::size_t index=3;!alpha&&index<image->rgba.size();index+=4)alpha=image->rgba[index]!=255;
        auto map=[&](const std::shared_ptr<const Image>&source)->GLuint{
            if(!source)return 0;GLuint map{};glGenTextures(1,&map);glBindTexture(GL_TEXTURE_2D,map);upload_texture(*source,mode,true);glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);glBindTexture(GL_TEXTURE_2D,id);return map;
        };
        gpu={id,image->width,image->height,serial,dynamic,alpha,mipsValid,pack?map(pack->normal):0,pack?map(pack->orm):0,pack&&pack->roughness};return sampled&&mode==UICommand::TextureMode::Default?sampled_texture(image,gpu):gpu.id;
    }
    void prepare_turbulence(const Image*image,bool dynamic,std::uint64_t serial,GLuint vao){
        if(!image)throw std::runtime_error("Turbulent game surface has no decoded bitmap");
        if(image->nativeStorageBytes==255)throw std::runtime_error("Native turbulent bitmap CPU storage is unavailable");
        if(image->nativeStorageBytes!=2)return;
        turbulent.insert(image);glActiveTexture(GL_TEXTURE0);texture(image,dynamic,serial,false);const auto&original=textures.at(image)[0];
        if(image->height>image->width)throw std::runtime_error("Turbulent bitmap rows exceed native displacement table");
        if(image->width==1)return;
        const auto&frame=*currentFrame;
        int phase=static_cast<int>(static_cast<std::int64_t>(double(frame.turbulenceSpeed)*image->width*frame.animationTimeSeconds)&(image->width-1));
        auto&warp=warps[image];bool resized=warp.width!=image->width||warp.height!=image->height;
        if(!resized&&warp.phase==phase&&warp.range==frame.turbulenceRange&&warp.serial==original.serial)return;
        GLint framebuffer{},viewport[4]{};glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_VIEWPORT,viewport);
        constexpr std::array<GLenum,6>capabilities{GL_DEPTH_TEST,GL_CULL_FACE,GL_BLEND,GL_SCISSOR_TEST,GL_CLIP_DISTANCE0,GL_STENCIL_TEST};std::array<GLboolean,6>enabled{};
        for(std::size_t index=0;index<capabilities.size();++index){enabled[index]=glIsEnabled(capabilities[index]);glDisable(capabilities[index]);}
        if(!warpFbo)glGenFramebuffers(1,&warpFbo);
        if(!warp.texture)glGenTextures(1,&warp.texture);
        glBindTexture(GL_TEXTURE_2D,warp.texture);
        if(resized){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image->width,image->height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);}
        if(!warp.offsetBuffer){glGenBuffers(1,&warp.offsetBuffer);glGenTextures(1,&warp.offsetTexture);}
        glBindBuffer(GL_TEXTURE_BUFFER,warp.offsetBuffer);if(resized)glBufferData(GL_TEXTURE_BUFFER,image->width*sizeof(std::int32_t),nullptr,GL_DYNAMIC_DRAW);
        turbulenceOffsets.resize(image->width);float amplitude=static_cast<float>(double(frame.turbulenceRange)*image->width*0.05),angleScale=static_cast<float>(6.28318/(image->width-1));
        for(std::uint32_t index=0;index<image->width;++index)turbulenceOffsets[index]=turbulence_offset(phase+index,amplitude,angleScale);
        glBufferSubData(GL_TEXTURE_BUFFER,0,turbulenceOffsets.size()*sizeof(std::int32_t),turbulenceOffsets.data());glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_BUFFER,warp.offsetTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_R32I,warp.offsetBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,warpFbo);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,warp.texture,0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete native turbulence framebuffer");
        glViewport(0,0,image->width,image->height);glUseProgram(warpProgram);glUniform1i(warpOriginal,0);glUniform1i(warpOffsets,1);glUniform2i(warpSize,image->width,image->height);glUniform1i(warpStride,1u<<(std::bit_width(image->width)-1));
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,original.id);glBindSampler(0,dynamicSceneSampler);glBindVertexArray(postVao);glDrawArrays(GL_TRIANGLES,0,3);
        warp.mipsValid=mipsEnabled&&!original.dynamic;if(warp.mipsValid){glBindTexture(GL_TEXTURE_2D,warp.texture);glGenerateMipmap(GL_TEXTURE_2D);}warp.width=image->width;warp.height=image->height;warp.serial=original.serial;warp.phase=phase;warp.range=frame.turbulenceRange;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        for(std::size_t index=0;index<capabilities.size();++index)if(enabled[index])glEnable(capabilities[index]);
        glUseProgram(scene);glBindVertexArray(vao);
    }
    MeshGPU& mesh(const Model&model){auto it=meshes.find(&model);if(it!=meshes.end())return it->second;if(model.vertices.empty()&& !model.triangles.empty())throw std::runtime_error("Game mesh has triangles without vertices");
        std::vector<Vertex>centroidVertices;std::vector<Triangle>centroidTriangles;
        centroid_geometry(model,currentFrame&&currentFrame->world&&(&currentFrame->world->mesh==&model)?currentFrame->world:model.nativeWorld,centroidVertices,centroidTriangles);
        const auto vertices=centroidVertices.empty()?std::span<const Vertex>(model.vertices):std::span<const Vertex>(centroidVertices);
        MeshGPU gpu;glGenVertexArrays(1,&gpu.vao);glGenBuffers(1,&gpu.vertices);glGenBuffers(1,&gpu.indices);glBindVertexArray(gpu.vao);glBindBuffer(GL_ARRAY_BUFFER,gpu.vertices);glBufferData(GL_ARRAY_BUFFER,vertices.size_bytes(),vertices.data(),GL_STATIC_DRAW);
        for(const auto&vertex:model.vertices)include_point(gpu.bounds,vertex.position);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,position));glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,normal));glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,uv));glEnableVertexAttribArray(3);glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,lightmap_uv));
        if(model.triangles.size()>static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()/3)||centroidTriangles.size()>static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()/3))throw std::runtime_error("Game mesh exceeds OpenGL index capacity");
        gpu.batches.resize(std::max<std::size_t>(model.materials.size(),1));
        for(const auto&t:model.triangles){auto material=display_material_index(model.formatVersion,t.materialIndex);if(material>=gpu.batches.size())throw std::runtime_error("Game triangle references invalid material");for(auto index:t.indices)if(index>=model.vertices.size())throw std::runtime_error("Game triangle references invalid vertex");gpu.batches[material].count+=3;}
        for(const auto&t:centroidTriangles)gpu.batches[display_material_index(model.formatVersion,t.materialIndex)].centroidCount+=3;
        GLuint indexCount=0;
        for(std::size_t material=0;material<gpu.batches.size();++material){auto&batch=gpu.batches[material];GLuint count=batch.count,centroidCount=batch.centroidCount;batch={indexCount,0,static_cast<std::uint32_t>(material),0,centroidCount};indexCount+=count;}
        for(auto&batch:gpu.batches){const GLuint count=batch.centroidCount;batch.centroidFirst=indexCount;batch.centroidCount=0;indexCount+=count;}
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu.indices);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indexCount*sizeof(std::uint32_t),nullptr,GL_STATIC_DRAW);
        if(indexCount){
            auto*indices=static_cast<std::uint32_t*>(glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,0,indexCount*sizeof(std::uint32_t),GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT));
            if(!indices)throw std::runtime_error("Cannot map game mesh index buffer");
            for(const auto&t:model.triangles){auto&batch=gpu.batches[display_material_index(model.formatVersion,t.materialIndex)];for(auto index:t.indices)indices[batch.first+batch.count++]=index;}
            for(const auto&t:centroidTriangles){auto&batch=gpu.batches[display_material_index(model.formatVersion,t.materialIndex)];for(auto index:t.indices)indices[batch.centroidFirst+batch.centroidCount++]=index;}
            if(!glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER))throw std::runtime_error("Game mesh index upload failed");
        }
        gpu.batches.erase(std::remove_if(gpu.batches.begin(),gpu.batches.end(),[](const Batch&b){return !b.count;}),gpu.batches.end());
        if(model.formatVersion==7){gpu.primaryMax=static_cast<std::int16_t>(gpu.batches.empty()?0:gpu.batches.back().material);gpu.skinLayers=native_skin_layers(model,gpu.primaryMax);}
        std::vector<Vec3>tangents(model.vertices.size());derive_tangents(model,{},{},tangents);
        if(!centroidVertices.empty()){
            tangents.resize(centroidVertices.size());
            for(const auto&t:centroidTriangles)if(t.indices[0]>=model.vertices.size())tangents[t.indices[0]]=add(tangents[t.indices[0]],tangents[t.indices[1]]);
            for(std::size_t i=model.vertices.size();i<tangents.size();++i)tangents[i]=unit(tangents[i]);
        }
        glGenBuffers(1,&gpu.tangents);glBindBuffer(GL_ARRAY_BUFFER,gpu.tangents);glBufferData(GL_ARRAY_BUFFER,tangents.size()*sizeof(Vec3),tangents.data(),GL_STATIC_DRAW);
        for(int attribute:{12,13}){glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,3,GL_FLOAT,GL_FALSE,sizeof(Vec3),nullptr);}
        if(!model.frames.empty()){
            if(model.vertices.empty())throw std::runtime_error("Animation frame has no model vertices");
            for(const auto&f:model.frames)if(f.positions.size()!=model.vertices.size()||(!f.normals.empty()&&f.normals.size()!=model.vertices.size()))throw std::runtime_error("Animation frame must supply complete decoded vertex pose");
            gpu.frameBounds.reserve(model.frames.size());for(const auto&frame:model.frames)gpu.frameBounds.push_back(position_bounds(frame.positions));
            GLint maxTexels{};glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&maxTexels);std::size_t texels=model.frames.size()*model.vertices.size()*3;
            if(texels>static_cast<std::size_t>(maxTexels))throw std::runtime_error("Game model animation exceeds GPU texture buffer capacity");
            static_assert(sizeof(Vec3)==3*sizeof(float));glGenBuffers(1,&gpu.poseBuffer);glBindBuffer(GL_TEXTURE_BUFFER,gpu.poseBuffer);glBufferData(GL_TEXTURE_BUFFER,texels*sizeof(Vec3),nullptr,GL_STATIC_DRAW);
            auto*poses=static_cast<Vec3*>(glMapBufferRange(GL_TEXTURE_BUFFER,0,texels*sizeof(Vec3),GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT));
            if(!poses)throw std::runtime_error("Cannot map game model animation buffer");
            for(const auto&f:model.frames){std::memcpy(poses,f.positions.data(),model.vertices.size()*sizeof(Vec3));poses+=model.vertices.size();if(f.normals.empty()){for(const auto&vertex:model.vertices)*poses++=vertex.normal;}else{std::memcpy(poses,f.normals.data(),model.vertices.size()*sizeof(Vec3));poses+=model.vertices.size();}derive_tangents(model,f.positions,f.normals,tangents);std::memcpy(poses,tangents.data(),tangents.size()*sizeof(Vec3));poses+=tangents.size();}
            if(!glUnmapBuffer(GL_TEXTURE_BUFFER))throw std::runtime_error("Game model animation upload failed");
            glGenTextures(1,&gpu.poses);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_BUFFER,gpu.poses);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGB32F,gpu.poseBuffer);gpu.frameCount=model.frames.size();
        }
        return meshes.emplace(&model,std::move(gpu)).first->second;
    }
    SkyGPU& sky_texture(const Image&sourceImage){
        auto found=skies.find(&sourceImage);if(found!=skies.end())return found->second;
        auto pack=options.packs?options.packs->find(sourceImage):std::nullopt;
        const Image&image=pack&&pack->albedo?*pack->albedo:sourceImage;
        check_image(image);std::uint32_t frames=image.width==image.height*6?6:image.width==image.height*2?2:0;
        if(!frames)throw std::runtime_error("Bitmap is not a native sky strip");
        if(image.width/frames>static_cast<std::uint32_t>(maxTextureSize)||image.height>static_cast<std::uint32_t>(maxTextureSize))throw std::runtime_error("Sky face exceeds GPU texture size");
        if(frames==2&&!image.sourceDepth&&image.nativeStorageBytes==255)throw std::runtime_error("Native cloud bitmap storage depth is unavailable");
        std::uint32_t width=image.width/frames,height=image.height;bool convert=frames==2&&((image.sourceDepth>0&&image.sourceDepth<=16)||image.nativeStorageBytes==2);
        GLint framebuffer{},viewport[4]{};glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_VIEWPORT,viewport);
        constexpr std::array<GLenum,6>capabilities{GL_DEPTH_TEST,GL_CULL_FACE,GL_BLEND,GL_SCISSOR_TEST,GL_CLIP_DISTANCE0,GL_STENCIL_TEST};std::array<GLboolean,6>enabled{};
        for(std::size_t index=0;index<capabilities.size();++index){enabled[index]=glIsEnabled(capabilities[index]);glDisable(capabilities[index]);}
        SkyGPU sky;sky.frames=frames;GLuint original{};glGenTextures(1,&original);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,original);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,width,height,frames,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,image.width);
        for(std::uint32_t layer=0;layer<frames;++layer)glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,layer,width,height,1,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data()+layer*width*4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        sky.texture=original;
        if(convert){
            glGenTextures(1,&sky.texture);glBindTexture(GL_TEXTURE_2D_ARRAY,sky.texture);
            for(std::uint32_t w=width,h=height,level=0;;++level){glTexImage3D(GL_TEXTURE_2D_ARRAY,level,GL_RGBA8,w,h,frames,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);if(w==1&&h==1)break;w=std::max(w/2,1u);h=std::max(h/2,1u);}
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            if(!warpFbo)glGenFramebuffers(1,&warpFbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,warpFbo);glUseProgram(cloudProgram);glBindVertexArray(postVao);
            glBindTexture(GL_TEXTURE_2D_ARRAY,original);glBindSampler(0,dynamicSceneSampler);glUniform1i(cloudOriginal,0);
            for(std::uint32_t w=width,h=height,level=0;;++level){
                glViewport(0,0,w,h);glUniform1i(cloudLevel,level);
                for(std::uint32_t layer=0;layer<frames;++layer){
                    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,sky.texture,level,layer);if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete native cloud conversion framebuffer");
                    glUniform1i(cloudLayer,layer);glUniform1i(cloudAlpha,layer>0);glDrawArrays(GL_TRIANGLES,0,3);
                }
                if(w==1&&h==1)break;w=std::max(w/2,1u);h=std::max(h/2,1u);
            }
            glDeleteTextures(1,&original);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);for(std::size_t index=0;index<capabilities.size();++index)if(enabled[index])glEnable(capabilities[index]);
        glUseProgram(scene);return skies.emplace(&sourceImage,sky).first->second;
    }
    void draw_sky(const RenderFrame&frame,const Camera&camera,int width,int height){
        if(!camera.drawWorld||!frame.world||!(camera.layers&1))return;
        const Image*cube=nullptr,*dome=nullptr;
        for(const auto&material:frame.world->mesh.materials)if((material.flags&15)==5){
            if(!material.texture)throw std::runtime_error("SKY surface has no decoded native bitmap");
            const auto&image=*material.texture;if(image.width==image.height*6)cube=&image;else if(image.width==image.height*2)dome=&image;
        }
        const Image*image=cube?cube:dome;if(!image)return;
        auto&sky=sky_texture(*image);
        glUseProgram(scene);glUniform1i(su.skyMask,1);maskPass=true;glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDepthMask(GL_FALSE);
        glEnable(GL_STENCIL_TEST);glStencilMask(255);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);glStencilFunc(GL_ALWAYS,1,255);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
        DrawInstance world;world.model=&frame.world->mesh;draw_instance(world,frame.skinBindings,&camera);
        if(frame.renderEntities&&!camera.noEntities)for(const auto&instance:frame.instances)if(!instance.viewSpace&&(instance.layers&camera.layers)&&(!camera.excludeEntityId||instance.objectId!=camera.excludeEntityId))draw_instance(instance,frame.skinBindings,&camera);
        maskPass=false;glUniform1i(su.skyMask,0);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glStencilMask(0);glStencilFunc(GL_EQUAL,1,255);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
        bool clipEnabled=glIsEnabled(GL_CLIP_DISTANCE0);glDisable(GL_CLIP_DISTANCE0);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(skyProgram);glBindVertexArray(postVao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,sky.texture);glBindSampler(0,cube?skyClampSampler:sceneSampler);
        Matrix rotation=transform({},camera.angles,{1,1,1});std::array<float,9>basis{rotation[0],rotation[1],rotation[2],rotation[4],rotation[5],rotation[6],rotation[8],rotation[9],rotation[10]};
        auto&u=skyUniforms;glUniform1i(u.image,0);glUniformMatrix3fv(u.basis,1,GL_FALSE,basis.data());glUniform1i(u.cube,cube!=nullptr);glUniform1i(u.blend,frame.skyBlend);
        float horizontal=camera.horizontalFov<0?-1:1,vertical=camera.pixelAspect<0?-1:1;
        if(cube){float tangent=std::tan(rad(std::abs(camera.horizontalFov))*.5f)/(camera.widescreen?camera.referenceAspect:float(width)/height)*std::abs(camera.pixelAspect);horizontal*=2*tangent*float(width)/height/std::abs(camera.pixelAspect);vertical*=2*tangent;}
        glUniform2f(u.cone,horizontal,vertical);glUniform1f(u.curve,frame.skyCurve*4);
        float argument=frame.skyClip*0.01745329238474369f;glUniform1f(u.clip,static_cast<float>(double(static_cast<float>(std::sin(double(argument))))-0.1));
        double time=frame.animationTimeSeconds;float clock=static_cast<float>((time-static_cast<std::int32_t>(time/1024.0-1024.0))*65536.0);
        for(std::uint32_t layer=0;layer<(cube?1u:sky.frames);++layer){
            glUniform1i(u.layer,layer);glUniform2f(u.scroll,frame.skySpeed.x*(layer+1)*clock/(double(image->width/sky.frames)*65536),frame.skySpeed.y*(layer+1)*clock/(double(image->height)*65536));glDrawArrays(GL_TRIANGLES,0,3);
        }
        glDisable(GL_STENCIL_TEST);glStencilMask(255);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);if(clipEnabled)glEnable(GL_CLIP_DISTANCE0);glUseProgram(scene);glBindSampler(0,sceneSampler);
    }
    void model_uniform(const Matrix&m){
        glUniformMatrix4fv(su.model,1,GL_FALSE,m.data());
        std::array<float,9>normal{};
        for(int col=0;col<3;++col){float squared=m[col*4]*m[col*4]+m[col*4+1]*m[col*4+1]+m[col*4+2]*m[col*4+2];for(int row=0;row<3;++row)normal[col*3+row]=squared>0?m[col*4+row]/squared:0;}
        glUniformMatrix3fv(su.normalMatrix,1,GL_FALSE,normal.data());
    }
    void material_uniforms(const ScriptMaterialState*material){
        glUniform1i(su.explicitMaterial,material&&currentFrame&&currentFrame->hardwareLighting);glUniform1i(su.effectKind,material?static_cast<int>(material->effect):0);
        if(!material)return;
        constexpr float channel=1.0f/255;
        glUniform4f(su.preparedAmbient,material->ambient.r*channel,material->ambient.g*channel,material->ambient.b*channel,1);
        glUniform3f(su.preparedDiffuse,material->diffuse.r*channel,material->diffuse.g*channel,material->diffuse.b*channel);
        glUniform3f(su.preparedSpecular,material->specular.r*channel,material->specular.g*channel,material->specular.b*channel);
        glUniform3f(su.preparedEmissive,material->emissive.r*channel,material->emissive.g*channel,material->emissive.b*channel);
        glUniform1f(su.materialPower,material->power);
        if(material->effect!=EffectKind::None){
            std::array<float,8>skills;for(std::size_t index=0;index<skills.size();++index)skills[index]=std::bit_cast<float>(material->skillBits[index]);
            glUniform4fv(su.skillMain,1,skills.data());glUniform4fv(su.skillExtra,1,skills.data()+4);glUniform1i(su.normalSkin,5);
        }
    }
    void forget_effect_texture(GLuint texture){
        if(!texture)return;
        for(auto&[id,state]:effectBindings)for(auto&bank:state.banks)if(bank.texture==texture)bank={};
    }
    EffectBindings& effect_banks(const ScriptMaterialState&material,const SkinBinding&color,const SkinBinding&normal){
        auto&state=effectBindings[material.sourceId];
        if(state.generation!=material.effectGeneration||state.kind!=material.effect)state={material.effectGeneration,material.effect,{}};
        const std::array<const SkinBinding*,2>bindings{&color,&normal};
        for(std::size_t index=0;index<2;++index){
            glActiveTexture(index==0?GL_TEXTURE0:GL_TEXTURE5);
            auto&bank=state.banks[index];const auto&binding=*bindings[index];
            if(binding.image){GLuint id=texture(binding.image,binding.dynamic,binding.serial);const auto&gpu=textures.at(binding.image)[0];bank={id,gpu.width,gpu.height,gpu.dynamic,gpu.alpha};}
            // Original binder skips NULL parameters. Retention is scoped to this material/effect generation.
            glBindTexture(GL_TEXTURE_2D,bank.texture);glBindSampler(index==0?0:5,bank.dynamic?uiLinearSampler:lightmapSampler);
        }
        return state;
    }
    unsigned model_queue(const DrawInstance&d,std::span<const SkinBinding>bindings){
        unsigned queues=0;
        for(const auto&batch:mesh(*d.model).batches){
            const bool subsets=d.model->formatVersion==7;
            const Material*mat=subsets&&batch.material>0?&d.model->materials[batch.material]:model_material(*d.model,batch.material,d.skinIndex);
            if(subsets&&batch.material>0&&(!mat->texture||!mat->nativePrimarySubset))continue;
            auto skin=resolved_skin(mat&&mat->texture?mat->texture.get():nullptr,bindings);
            queues|=model_translucent(d,skin.nativeTextureAlpha)?2u:1u;
        }
        return queues;
    }
    void bind_override(const DrawInstance&d,const MeshGPU&gpu,EffectKind liveEffectKind,std::uint32_t id,std::uint64_t revision,std::span<const Vec3>positions,std::span<const Vec3>normals,int positionAttribute,int normalAttribute,int tangentAttribute,std::uint32_t frameIndex){
        if(positions.empty()&&normals.empty())return;
        if(!id||(!positions.empty()&&positions.size()!=d.model->vertices.size())||(!normals.empty()&&normals.size()!=d.model->vertices.size()))throw std::runtime_error("Invalid mutable entity frame override");
        auto&data=overrides[id];
        if(data.revision!=revision||data.count!=positions.size()||data.normalCount!=normals.size()||data.frame!=frameIndex||data.model!=d.model){
            if(!positions.empty()){if(!data.positions)glGenBuffers(1,&data.positions);glBindBuffer(GL_ARRAY_BUFFER,data.positions);if(data.count!=positions.size())glBufferData(GL_ARRAY_BUFFER,positions.size_bytes(),positions.data(),GL_DYNAMIC_DRAW);else glBufferSubData(GL_ARRAY_BUFFER,0,positions.size_bytes(),positions.data());}
            if(!normals.empty()){if(!data.normals)glGenBuffers(1,&data.normals);glBindBuffer(GL_ARRAY_BUFFER,data.normals);if(data.normalCount!=normals.size())glBufferData(GL_ARRAY_BUFFER,normals.size_bytes(),normals.data(),GL_DYNAMIC_DRAW);else glBufferSubData(GL_ARRAY_BUFFER,0,normals.size_bytes(),normals.data());}
            set_override_pose(data,*d.model,revision,positions.size(),normals.size(),frameIndex);
        }
        if(override_tangents_needed(data,liveEffectKind,revision)){
            auto posePositions=positions,poseNormals=normals;
            if(gpu.frameCount){const auto&frame=d.model->frames[frameIndex];if(posePositions.empty())posePositions=frame.positions;if(poseNormals.empty())poseNormals=frame.normals;}
            std::size_t oldSize=data.tangentData.size();data.tangentData.resize(d.model->vertices.size());derive_tangents(*d.model,posePositions,poseNormals,data.tangentData);
            if(!data.tangents)glGenBuffers(1,&data.tangents);glBindBuffer(GL_ARRAY_BUFFER,data.tangents);if(oldSize!=data.tangentData.size())glBufferData(GL_ARRAY_BUFFER,data.tangentData.size()*sizeof(Vec3),data.tangentData.data(),GL_DYNAMIC_DRAW);else glBufferSubData(GL_ARRAY_BUFFER,0,data.tangentData.size()*sizeof(Vec3),data.tangentData.data());
            data.tangentRevision=revision;data.tangentReady=true;
        }
        if(!positions.empty()){glBindBuffer(GL_ARRAY_BUFFER,data.positions);glVertexAttribPointer(positionAttribute,3,GL_FLOAT,GL_FALSE,sizeof(Vec3),nullptr);}
        if(!normals.empty()){glBindBuffer(GL_ARRAY_BUFFER,data.normals);glVertexAttribPointer(normalAttribute,3,GL_FLOAT,GL_FALSE,sizeof(Vec3),nullptr);}
        if(liveEffectKind!=EffectKind::None){glBindBuffer(GL_ARRAY_BUFFER,data.tangents);glVertexAttribPointer(tangentAttribute,3,GL_FLOAT,GL_FALSE,sizeof(Vec3),nullptr);}
    }
    void draw_instance(const DrawInstance&d,std::span<const SkinBinding> skinBindings,const Camera*camera=nullptr,bool depthWrite=true,int nativeQueue=-1){
        if((!d.visible&&!d.shadowRequested)||!d.model||d.scale.x==0||d.scale.y==0||d.scale.z==0||(maskPass&&(!d.visible||d.model->formatVersion!=6)))return;
        auto&gpu=mesh(*d.model);Vec3 angles=d.angles;
        if(gpu.frameCount&&(d.frame>=static_cast<std::uint32_t>(gpu.frameCount)||d.nextFrame>=static_cast<std::uint32_t>(gpu.frameCount)))throw std::runtime_error("Entity animation index outside model");
        if(d.billboard&&camera)angles=d.viewSpace?Vec3{180,0,d.angles.z}:Vec3{camera->angles.x+180,-camera->angles.y,d.angles.z-camera->angles.z};
        Matrix m=transform(d.position,angles,d.scale);
        if(d.viewSpace&&camera)m=product(transform(camera->position,camera->angles,{1,1,1}),m);
        auto poseBounds=[&](std::uint32_t id,std::uint64_t revision,std::span<const Vec3>positions,std::uint32_t frame){
            if(positions.empty())return gpu.frameCount?gpu.frameBounds[frame]:gpu.bounds;
            if(!id||positions.size()!=d.model->vertices.size())throw std::runtime_error("Invalid mutable entity position override");
            return mesh_override_bounds(overrides[id],*d.model,revision,positions);
        };
        if(gpu.batches.empty())return;
        if(camera){
            auto first=poseBounds(d.meshId,d.meshRevision,d.positions,d.frame);
            // Without animation texels, the shader initializes both endpoints from attribute0.
            auto next=!gpu.frameCount&&d.nextPositions.empty()?first:poseBounds(d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextFrame);
            if(!geometry_eligible(interpolated_bounds(first,next,d.interpolation),m,viewProjection,geometryClipPlane))return;
        }
        const auto light=!maskPass&&!casterPass&&camera&&currentFrame&&currentFrame->prepareEntityLight?currentFrame->prepareEntityLight(currentFrame->lightContext,d,*camera):NativeEntityLightState{d.nativeLightScalar,d.nativeLightDelta,d.nativeLightBias};
        if(!d.visible)return;
        const auto*live=script_material(currentFrame,d.materialIndex);
        if(d.alpha<=0&&(!live||live->effect==EffectKind::None))return;
        glUniform1i(su.particleInstanced,0);material_uniforms(live);
        const EffectKind liveEffectKind=live?live->effect:EffectKind::None;
        model_uniform(m);glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER,gpu.vertices);
        for(int attribute:{0,4}){glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,position));}
        for(int attribute:{1,5}){glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,normal));}
        glBindBuffer(GL_ARRAY_BUFFER,gpu.tangents);for(int attribute:{12,13}){glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,3,GL_FLOAT,GL_FALSE,sizeof(Vec3),nullptr);}
        // PBR normal mapping consumes the same per-pose tangent stream as effect materials.
        const EffectKind tangentKind=options.pbr&&liveEffectKind==EffectKind::None?EffectKind::NormalMix:liveEffectKind;
        bind_override(d,gpu,tangentKind,d.meshId,d.meshRevision,d.positions,d.normals,0,1,12,d.frame);
        bind_override(d,gpu,tangentKind,d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextNormals,4,5,13,d.nextFrame);
        glUniform1i(su.firstOverride,!d.positions.empty());glUniform1i(su.nextOverride,!d.nextPositions.empty());
        glUniform1i(su.firstNormalOverride,!d.normals.empty());glUniform1i(su.nextNormalOverride,!d.nextNormals.empty());
        glUniform1f(su.interpolation,std::clamp(d.interpolation,0.0f,1.0f));
        glUniform1i(su.vertexCount,gpu.frameCount?d.model->vertices.size():0);
        if(gpu.frameCount){if(d.frame>=static_cast<std::uint32_t>(gpu.frameCount)||d.nextFrame>=static_cast<std::uint32_t>(gpu.frameCount))throw std::runtime_error("Entity animation frame index outside model");glUniform1i(su.frame0,d.frame);glUniform1i(su.frame1,d.nextFrame);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_BUFFER,gpu.poses);}
        glUniform1i(su.unlit,d.unlit);if(d.doubleSided||d.billboard||(camera&&camera->noCull))glDisable(GL_CULL_FACE);else glEnable(GL_CULL_FACE);glBlendFuncSeparate(GL_SRC_ALPHA,d.additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        // Shadow casters cull back faces as seen from the light (one-sided BSP faces, lights inside fixtures); camera mirroring does not apply.
        bool flipped=camera&&!casterPass&&((camera->horizontalFov<0)!=(camera->pixelAspect<0));flipped^=d.model->clockwise;flipped^=d.scale.x*d.scale.y*d.scale.z<0;glFrontFace(flipped?GL_CW:GL_CCW);
        glUniform1f(su.normalOrientation,d.model->clockwise?-1:1);glUniform1i(su.noFog,d.noFog);
        glUniform1i(su.flare,d.flare);glUniform1i(su.overlay,d.overlay);
        glUniform1i(su.invertFlare,d.invertFlare);glUniform1i(su.spriteFrames,d.spriteFrames);
        if(d.spriteFrames>1){
            if(d.frame>=d.spriteFrames||d.nextFrame>=d.spriteFrames)throw std::runtime_error("Sprite frame index outside texture strip");
            glUniform1i(su.frame0,d.frame);glUniform1i(su.frame1,d.nextFrame);glUniform1f(su.interpolation,std::clamp(d.interpolation,0.0f,1.0f));
        }
        const bool nativeSubsets=camera&&camera->perDrawOpacity&&d.model->formatVersion==7;
        const bool nativeModelPass=camera&&camera->perDrawOpacity&&model_has_entity_skins(*d.model)&&!maskPass;
        const bool recordCasters=casterPass&&casterWorld&&!worldCastersReady;bool cacheable=true;
        if(casterPass&&casterWorld&&worldCastersReady){draw_world_casters();return;}
        for(const auto&b:gpu.batches){
            const Material*mat=nativeSubsets&&b.material>0?&d.model->materials[b.material]:model_material(*d.model,b.material,d.skinIndex);
            if(nativeSubsets&&b.material>0&&(!mat->texture||!mat->nativePrimarySubset))continue;
            bool worldSurface=d.model->formatVersion==6;int type=mat?mat->flags&15:0;
            if(maskPass?(!worldSurface||!mat||type!=5):(worldSurface&&mat&&(type==5||type==11||(portalPass&&(mat->flags&0x2000)))))continue;
            if(worldSurface&&mat&&mat->raw.size()!=64)throw std::runtime_error("WMB material lacks native surface metadata");
            if(maskPass){glDrawElements(GL_TRIANGLES,b.count,GL_UNSIGNED_INT,reinterpret_cast<void*>(std::uintptr_t(b.first)*sizeof(std::uint32_t)));continue;}
            int nativeAmbient=worldSurface&&mat?static_cast<std::int8_t>(mat->raw[42]):0,nativeAlbedo=worldSurface&&mat?static_cast<std::int8_t>(mat->raw[43]):0;
            float reflection=worldSurface&&mat&&(mat->flags&0x2000)?surface_reflection(nativeAlbedo,d.reflectivity,d.objectId!=0):0;
            glUniform1f(su.reflectionWeight,reflection);glUniform1f(su.surfaceAmbient,surface_ambient(nativeAmbient,camera?static_cast<int>(camera->nativeAmbient):0,d.objectId!=0));
            glUniform1i(su.unlit,d.unlit||(currentFrame&&!currentFrame->hardwareLighting)||(worldSurface&&mat&&(mat->flags&15)==3));
            if(mat&&!mat->textureName.empty()&&!mat->texture)throw std::runtime_error("Unresolved game skin: "+mat->textureName);
            if(mat&&!mat->lightmapName.empty()&&!mat->lightmap)throw std::runtime_error("Unresolved game lightmap: "+mat->lightmapName);
            const auto entitySkin=resolved_skin(mat&&mat->texture?mat->texture.get():nullptr,skinBindings);
            if(nativeModelPass&&nativeQueue>=0&&model_translucent(d,entitySkin.nativeTextureAlpha)!=(nativeQueue==1))continue;
            if(nativeModelPass){
                // Native ordinary state precedes effect mtlSkin fallback and NONNULL-only sampler retention.
                const auto pass=model_pass_state(d,*camera,entitySkin.nativeTextureAlpha,depthWrite);
                if(pass.blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA,pass.additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);glDepthMask(pass.depthWrite);
            }
            const auto skin=entitySkin.image?entitySkin:resolved_skin(live?live->skins[0]:nullptr,skinBindings);
            const Image*image=skin.image;bool dynamicSkin=skin.dynamic;std::uint64_t skinSerial=skin.serial;
            glUniform1i(su.overlay,overlay_keyed(d,image));
            if(casterPass&&!(live&&live->effect!=EffectKind::None)){
                // Depth-only caster: only the alpha-rejection inputs (texel, tint, opacity, UV) reach the shader.
                const GLuint tex=texture(image,dynamicSkin,skinSerial),sampler=image&&textures.at(image)[0].dynamic?dynamicSceneSampler:sceneSampler;const bool textureAlpha=image&&textures.at(image)[0].alpha;
                Color t=live?Color{1,1,1,d.tint.a}:d.tint;if(mat&&!worldSurface){t.r*=mat->albedo.r;t.g*=mat->albedo.g;t.b*=mat->albedo.b;t.a*=mat->albedo.a;}
                const float opacity=draw_opacity(d.alpha*(mat?mat->alpha:1),camera);
                const Vec2 uvScale=model_uv_scale(d.model->formatVersion,mat?mat->uvScale:Vec2{1,1},image),uvOffset=mat?mat->uvOffset:Vec2{};
                const bool centroid=centroidLighting&&b.centroidCount;const GLsizei count=centroid?b.centroidCount:b.count;
                const void*offset=reinterpret_cast<void*>(std::uintptr_t(centroid?b.centroidFirst:b.first)*sizeof(std::uint32_t));
                if(recordCasters){
                    auto same=[&](const CasterGroup&g){return g.texture==tex&&g.sampler==sampler&&g.textureAlpha==textureAlpha&&g.tint.r==t.r&&g.tint.g==t.g&&g.tint.b==t.b&&g.tint.a==t.a&&g.alpha==opacity&&g.uvScale.x==uvScale.x&&g.uvScale.y==uvScale.y&&g.uvOffset.x==uvOffset.x&&g.uvOffset.y==uvOffset.y;};
                    auto group=std::find_if(worldCasters.begin(),worldCasters.end(),same);
                    if(group==worldCasters.end())group=worldCasters.insert(worldCasters.end(),CasterGroup{tex,sampler,textureAlpha,t,opacity,uvScale,uvOffset,{},{}});
                    group->counts.push_back(count);group->offsets.push_back(offset);continue;
                }
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);glBindSampler(0,sampler);glUniform1i(su.textureAlpha,textureAlpha);
                glUniform4f(su.tint,t.r,t.g,t.b,t.a);glUniform1f(su.alpha,opacity);glUniform2f(su.uvScale,uvScale.x,uvScale.y);glUniform2f(su.uvOffset,uvOffset.x,uvOffset.y);
                glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,offset);
                continue;
            }
            if(recordCasters)cacheable=false;
            if(worldSurface&&mat&&(mat->flags&15)==9)prepare_turbulence(image,dynamicSkin,skinSerial,gpu.vao);
            bool effect=live&&live->effect!=EffectKind::None;EffectBindings*retained=nullptr;
            if(effect){
                const Image*normalImage=nullptr;
                if(model_has_entity_skins(*d.model)){
                    int following=nativeSubsets?gpu.primaryMax+1:static_cast<int>(d.skinIndex)+1;
                    if(following>=0&&std::size_t(following)<d.model->materials.size()){
                        if(nativeSubsets&&gpu.skinLayers>=2)following=static_cast<int>(b.material)+1;
                        if(std::size_t(following)<d.model->materials.size())normalImage=d.model->materials[following].texture.get();
                    }
                }
                if(!normalImage)normalImage=live->skins[1];
                auto normal=resolved_skin(normalImage,skinBindings);retained=&effect_banks(*live,skin,normal);
                glUniform1i(su.textureAlpha,retained->banks[0].alpha);
            }else{
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture(image,dynamicSkin,skinSerial));glBindSampler(0,image&&textures.at(image)[0].dynamic?dynamicSceneSampler:sceneSampler);
                glUniform1i(su.textureAlpha,image&&textures.at(image)[0].alpha);
                if(options.pbr){
                    const TextureGPU*maps=image?&textures.at(image)[0]:nullptr;
                    glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,maps&&maps->normal?maps->normal:flatNormal);glBindSampler(6,sceneSampler);
                    glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,maps&&maps->orm?maps->orm:defaultOrm);glBindSampler(7,sceneSampler);
                    glUniform1i(su.roughnessMap,maps&&maps->roughness);glUniform1f(su.legacyRoughness,legacy_roughness(mat?mat->power:0));
                }
            }
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,texture(mat?mat->lightmap.get():nullptr));glUniform1i(su.hasLightmap,mat&&mat->lightmap);
            glUniform1i(su.worldVertexLighting,worldSurface&&!d.unlit&&type!=3&&!maskPass);
            Color t=live?Color{1,1,1,d.tint.a}:d.tint;if(mat&&!worldSurface){t.r*=mat->albedo.r;t.g*=mat->albedo.g;t.b*=mat->albedo.b;t.a*=mat->albedo.a;}
            // Native ordinary draws overwrite MATERIAL diffuse alpha with entity/view opacity.
            glUniform4f(su.tint,t.r,t.g,t.b,t.a);glUniform1f(su.alpha,draw_opacity(d.alpha*(mat?mat->alpha:1),camera));
            int viewAmbient=camera?static_cast<int>(camera->nativeAmbient):0;
            glUniform1f(su.materialAmbient,worldSurface?surface_light(nativeAmbient,viewAmbient,d.objectId!=0)/255.0f:model_ambient(d.ambient,viewAmbient));
            if(live&&!effect){auto ambient=material_ambient_light(d,camera,&light);glUniform3f(su.ambientLight,ambient.r,ambient.g,ambient.b);glUniform1f(su.materialSunStrength,material_light_strength(live->albedoRaw,light.scalar,currentFrame&&currentFrame->bonusProfile,!currentFrame||currentFrame->nativeFixedLightBank));}
            Vec2 baseScale=mat?mat->uvScale:Vec2{1,1};
            Vec2 uvScale=retained&&retained->banks[0].texture?model_uv_scale(d.model->formatVersion,baseScale,retained->banks[0].width,retained->banks[0].height):model_uv_scale(d.model->formatVersion,baseScale,image);
            glUniform2f(su.uvScale,uvScale.x,uvScale.y);glUniform2f(su.uvOffset,mat?mat->uvOffset.x:0,mat?mat->uvOffset.y:0);
            if(retained){const auto&bank=retained->banks[1];auto scale=bank.texture?model_uv_scale(d.model->formatVersion,baseScale,bank.width,bank.height):baseScale;glUniform2f(su.normalUvScale,scale.x,scale.y);}
            const bool centroid=centroidLighting&&b.centroidCount;
            glDrawElements(GL_TRIANGLES,centroid?b.centroidCount:b.count,GL_UNSIGNED_INT,reinterpret_cast<void*>(std::uintptr_t(centroid?b.centroidFirst:b.first)*sizeof(std::uint32_t)));
        }
        if(recordCasters){draw_world_casters();worldCastersReady=cacheable;if(!cacheable)worldCasters.clear();}
        if(nativeModelPass){glEnable(GL_BLEND);glDepthMask(depthWrite);}
    }
    void draw_planar(const DrawInstance&d,const Camera&camera,int width,int height){
        const auto&frame=*currentFrame;
        if(!d.model||(d.model->formatVersion!=4&&d.model->formatVersion!=5&&d.model->formatVersion!=7)||d.viewSpace||d.scale.x==0||d.scale.y==0||d.scale.z==0)return;
        auto&gpu=mesh(*d.model);if(gpu.batches.empty())return;
        if(gpu.frameCount&&(d.frame>=std::uint32_t(gpu.frameCount)||d.nextFrame>=std::uint32_t(gpu.frameCount)))throw std::runtime_error("Shadow animation index outside selected source model");
        auto bounds=[&](std::uint32_t id,std::uint64_t revision,std::span<const Vec3>positions,std::uint32_t frameIndex){
            if(positions.empty())return gpu.frameCount?gpu.frameBounds[frameIndex]:gpu.bounds;
            if(!id||positions.size()!=d.model->vertices.size())throw std::runtime_error("Invalid mutable shadow position override");
            return mesh_override_bounds(overrides[id],*d.model,revision,positions);
        };
        const auto first=bounds(d.meshId,d.meshRevision,d.positions,d.frame);
        const auto next=!gpu.frameCount&&d.nextPositions.empty()?first:bounds(d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextFrame);
        const auto poseBounds=interpolated_bounds(first,next,d.interpolation);
        const auto model=transform(d.position,d.angles,d.scale);
        const auto shadow=frame.prepareEntityLight?frame.prepareEntityLight(frame.lightContext,d,camera).shadow:d.nativeShadow;
        if(!shadow.caster)return;
        const auto projection=planar_projection(d,shadow,frame);
        if(!geometry_eligible(poseBounds,product(projection,model),viewProjection,geometryClipPlane))return;
        glUseProgram(shadowProgram);glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER,gpu.vertices);
        for(int attribute:{0,4}){glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,position));}
        bind_override(d,gpu,EffectKind::None,d.meshId,d.meshRevision,d.positions,d.normals,0,1,12,d.frame);
        bind_override(d,gpu,EffectKind::None,d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextNormals,4,5,13,d.nextFrame);
        glUniformMatrix4fv(shu.model,1,GL_FALSE,model.data());glUniformMatrix4fv(shu.projection,1,GL_FALSE,projection.data());glUniformMatrix4fv(shu.vp,1,GL_FALSE,viewProjection.data());
        glUniform1i(shu.poses,2);glUniform1i(shu.vertexCount,gpu.frameCount?d.model->vertices.size():0);glUniform1i(shu.frame0,d.frame);glUniform1i(shu.frame1,d.nextFrame);
        glUniform1f(shu.interpolation,std::clamp(d.interpolation,0.f,1.f));glUniform1i(shu.firstOverride,!d.positions.empty());glUniform1i(shu.nextOverride,!d.nextPositions.empty());
        if(gpu.frameCount){glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_BUFFER,gpu.poses);}
        glUniform4fv(shu.clipPlane,1,geometryClipPlane.data());glUniform2f(shu.viewport,width,height);glUniform1f(shu.areaThreshold,-2.f*frame.clipSize);
        const float materialPercent=frame.shadowAlpha*0.01f;
        const auto alphaByte=std::uint8_t(std::int64_t(std::trunc(materialPercent*camera.opacityByte)));
        glUniform1f(shu.alpha,alphaByte/255.f);
        const auto fog=camera.environmentOverride?camera.fogColor:frame.fogColor;
        glUniform3f(shu.cameraPosition,camera.position.x,camera.position.y,camera.position.z);glUniform3f(shu.fogColor,fog.r,fog.g,fog.b);
        glUniform2f(shu.fogRange,camera.environmentOverride?camera.fogStart:frame.fogStart,camera.environmentOverride?camera.fogEnd:frame.fogEnd);glUniform1i(shu.noFog,d.noFog);
        bool reversed=(camera.horizontalFov<0)!=(camera.pixelAspect<0);reversed^=d.model->clockwise;reversed^=d.scale.x*d.scale.y*d.scale.z<0;glUniform1i(shu.reversed,reversed);
        glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        // The geometry shader emits each primary and its alpha-zero closer duplicate together.
        const bool clipEnabled=glIsEnabled(GL_CLIP_DISTANCE0);glDisable(GL_CLIP_DISTANCE0);
        for(const auto&batch:gpu.batches)glDrawElements(GL_TRIANGLES,batch.count,GL_UNSIGNED_INT,reinterpret_cast<void*>(std::uintptr_t(batch.first)*sizeof(std::uint32_t)));
        if(clipEnabled)glEnable(GL_CLIP_DISTANCE0);glUseProgram(scene);
    }
    void post_state(){
        glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_CLIP_DISTANCE0);glViewport(0,0,targetWidth,targetHeight);
    }
    void post_draw(){
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glUseProgram(post);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,resolveColor);glBindSampler(0,uiLinearSampler);
        glUniform1i(postImage,0);glUniform1i(postRamp,1);glUniform1i(postEnabled,gamma.enabled);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,gammaTexture);glBindSampler(1,uiNearestSampler);
        glBindVertexArray(postVao);glDrawArrays(GL_TRIANGLES,0,3);
    }
    void quad(Rect r,Rect uv,Color c,float angle=0){float cx=r.x+r.width*.5f,cy=r.y+r.height*.5f,cs=std::cos(rad(angle)),sn=std::sin(rad(angle));const float u0=std::min(uv.x,uv.x+uv.width),v0=std::min(uv.y,uv.y+uv.height),u1=std::max(uv.x,uv.x+uv.width),v1=std::max(uv.y,uv.y+uv.height);UIVertex v[4];for(int i=0;i<4;++i){float x=(i==1||i==2?r.width:0)-r.width*.5f,y=(i>=2?r.height:0)-r.height*.5f;v[i]={cx+x*cs-y*sn,cy+x*sn+y*cs,uv.x+(i==1||i==2?uv.width:0),uv.y+(i>=2?uv.height:0),c.r,c.g,c.b,c.a,u0,v0,u1,v1};}for(int i:{0,1,2,0,2,3})uiVertices.push_back(v[i]);}
    void flush_ui(GLuint image){
        if(uiVertices.empty())return;glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,image);glBindVertexArray(uiVao);glBindBuffer(GL_ARRAY_BUFFER,uiVbo);
        if(uiVertices.size()>uiCapacity){uiCapacity=std::max<std::size_t>(uiVertices.size(),4096);glBufferData(GL_ARRAY_BUFFER,uiCapacity*sizeof(UIVertex),nullptr,GL_STREAM_DRAW);}
        glBufferSubData(GL_ARRAY_BUFFER,0,uiVertices.size()*sizeof(UIVertex),uiVertices.data());glDrawArrays(GL_TRIANGLES,0,uiVertices.size());uiVertices.clear();
    }
    void ui_command(const UICommand&c){
        glBlendFunc(GL_SRC_ALPHA,c.additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
        glBindSampler(0,c.filter?uiLinearSampler:uiNearestSampler);glUniform1i(uiOverlay,c.overlay);glUniform1f(uiAlphaRef,c.alphaRef);
        glUniform1i(uiTextKey,c.kind==UICommand::Kind::Text);
        glUniform1i(uiCursorKey,c.cursorOverlay||c.textureMode!=UICommand::TextureMode::Default);
        uiVertices.clear();GLuint image=white;
        if(c.kind==UICommand::Kind::Image){if(!c.image)throw std::runtime_error("UI image command has no decoded game image");image=texture(c.image,c.dynamicImage,c.imageSerial,true,c.textureMode);quad(c.rect,c.uv,c.color,c.angle);}
        else if(c.kind==UICommand::Kind::Solid)quad(c.rect,{0,0,1,1},c.color,c.angle);
        else{
            if(!c.font)throw std::runtime_error("UI text command has no font atlas");
            image=texture(&c.font->image);float x=c.rect.x,y=c.rect.y;
            for(unsigned char ch:c.text){
                if(ch=='\n'){x=c.rect.x;y+=c.font->lineHeight*c.scaleY;continue;}
                const auto&g=c.font->glyphs[ch];
                if(g.pixels.width&&g.pixels.height)quad({x+g.bearing.x*c.scaleX,y+(c.font->ascent-g.bearing.y)*c.scaleY,g.pixels.width*c.scaleX,g.pixels.height*c.scaleY},{g.pixels.x/c.font->image.width,g.pixels.y/c.font->image.height,g.pixels.width/c.font->image.width,g.pixels.height/c.font->image.height},c.color);
                x+=(g.advance+c.advanceDelta)*c.scaleX;
            }
        }
        flush_ui(image);
    }
};
GLBackend::GLBackend(SDL_Window*w,const RenderOptions&o):impl_(std::make_unique<Impl>(w,o)){}
GLBackend::~GLBackend()=default;
void GLBackend::drain(){impl_->drain_completion();}
void GLBackend::invalidate(WindowMode mode){impl_->options.mode=mode;impl_->finalFrameValid=false;}
int GLBackend::shader_version()const{return impl_->scene&&impl_->su.effectKind>=0&&impl_->su.normalSkin>=0&&impl_->su.skillMain>=0&&impl_->su.skillExtra>=0&&impl_->su.preparedAmbient>=0?2020:0;}
std::string GLBackend::description()const{return std::string("OpenGL ")+reinterpret_cast<const char*>(glGetString(GL_VERSION))+"; "+reinterpret_cast<const char*>(glGetString(GL_RENDERER));}
PresentationState GLBackend::presentation_state()const{const auto&i=*impl_;return {i.completionLimit,i.completionCount,i.completionSubmissions,i.completionWaits,SDL_GL_GetSwapInterval(),i.samples};}
void GLBackend::purge_model(const Model*model){
    auto&i=*impl_;i.drain_completion();auto found=i.meshes.find(model);
    if(found!=i.meshes.end()){auto&m=found->second;glDeleteVertexArrays(1,&m.vao);glDeleteBuffers(1,&m.vertices);glDeleteBuffers(1,&m.indices);glDeleteBuffers(1,&m.tangents);glDeleteBuffers(1,&m.poseBuffer);glDeleteTextures(1,&m.poses);i.meshes.erase(found);}
    purge_model_overrides(i.overrides,model);
    if(model)for(const auto&material:model->materials){
        if(material.texture&&material.texture.use_count()==1)purge_image(material.texture.get());
        if(material.lightmap&&material.lightmap.use_count()==1)purge_image(material.lightmap.get());
    }
}
void GLBackend::release_scene(){
    auto&i=*impl_;
    i.drain_completion();
    i.retainedColorValid=i.portalColorValid=false;
    for(auto&[key,m]:i.meshes){glDeleteVertexArrays(1,&m.vao);glDeleteBuffers(1,&m.vertices);glDeleteBuffers(1,&m.indices);glDeleteBuffers(1,&m.tangents);glDeleteBuffers(1,&m.poseBuffer);glDeleteTextures(1,&m.poses);}
    i.meshes.clear();
    for(auto&[key,m]:i.overrides)release_override(m);
    i.overrides.clear();
    i.lightData.clear();
    i.effectBindings.clear();
    for(auto&[key,variants]:i.textures)for(auto&texture:variants){glDeleteTextures(1,&texture.id);glDeleteTextures(1,&texture.normal);glDeleteTextures(1,&texture.orm);}
    i.textures.clear();
    for(auto&[image,warp]:i.warps){glDeleteTextures(1,&warp.texture);glDeleteTextures(1,&warp.offsetTexture);glDeleteBuffers(1,&warp.offsetBuffer);}i.warps.clear();i.turbulent.clear();
    for(auto&[image,sky]:i.skies)glDeleteTextures(1,&sky.texture);i.skies.clear();
}
void GLBackend::purge_image(const Image*image){
    auto&i=*impl_;auto it=i.textures.find(image);
    if(it!=i.textures.end()){for(auto&texture:it->second){i.forget_effect_texture(texture.id);glDeleteTextures(1,&texture.id);glDeleteTextures(1,&texture.normal);glDeleteTextures(1,&texture.orm);}i.textures.erase(it);}
    auto warp=i.warps.find(image);if(warp!=i.warps.end()){i.forget_effect_texture(warp->second.texture);glDeleteTextures(1,&warp->second.texture);glDeleteTextures(1,&warp->second.offsetTexture);glDeleteBuffers(1,&warp->second.offsetBuffer);i.warps.erase(warp);}
    auto sky=i.skies.find(image);if(sky!=i.skies.end()){glDeleteTextures(1,&sky->second.texture);i.skies.erase(sky);}
    i.turbulent.erase(image);
}
void Renderer::boundary_check(){
    if(effective_mip_filter(-1)!=GL_LINEAR||effective_mip_filter(0)!=GL_LINEAR||effective_mip_filter(1-1.0/1024)!=GL_LINEAR||
       effective_mip_filter(1)!=GL_LINEAR_MIPMAP_NEAREST||effective_mip_filter(1+1.0/1024)!=GL_LINEAR_MIPMAP_NEAREST||
       effective_mip_filter(2)!=GL_LINEAR_MIPMAP_NEAREST||effective_mip_filter(2+1.0/1024)!=GL_LINEAR_MIPMAP_LINEAR||
       effective_mip_filter(3)!=GL_LINEAR_MIPMAP_LINEAR)throw std::logic_error("Native mipmapping mode regression");
    if(effective_anisotropy(0,8)!=1||effective_anisotropy(2,8)!=8||effective_anisotropy(-1,8)!=8||effective_anisotropy(2,1)!=1)throw std::logic_error("Native anisotropy enable regression");
    Image fish;fish.width=4;fish.height=1;
    Image npot;npot.width=3;npot.height=5;
    Image alternate;alternate.width=12;alternate.height=9;
    const Vec2 materialScale{2,-3},offset{0.25f,0.125f},nativeUV{0.25f,0.5f};
    auto close=[](float a,float b){return std::abs(a-b)<0.000001f;};
    for(std::uint32_t format:{4u,5u,7u}){
        auto scale=model_uv_scale(format,materialScale,&fish);
        if(scale.x!=4||scale.y!=-24)throw std::logic_error("Native 4x1 model skin UV regression");
        scale=model_uv_scale(format,materialScale,&npot);
        if(!close(nativeUV.x*scale.x+offset.x,19/12.0f)||!close(nativeUV.y*scale.y+offset.y,-91/40.0f))throw std::logic_error("Native NPOT model skin/material UV regression");
        scale=model_uv_scale(format,materialScale,&alternate);
        if(!close(nativeUV.x*scale.x+offset.x,11/12.0f)||!close(nativeUV.y*scale.y+offset.y,-61/24.0f))throw std::logic_error("Native replacement skin UV regression");
    }
    auto mdl7Scale=model_uv_scale(7,{1,1},&npot);
    if(!close((3/16.0f)*mdl7Scale.x,0.5f)||!close((5/32.0f)*mdl7Scale.y,0.25f))throw std::logic_error("Native MDL7 normalized UV composition regression");
    for(std::uint32_t format:{0u,6u,105u}){
        auto scale=model_uv_scale(format,materialScale,&fish);
        if(scale.x!=materialScale.x||scale.y!=materialScale.y)throw std::logic_error("Non-model UV scale regression");
    }
    auto noSkinScale=model_uv_scale(4,materialScale,nullptr);
    if(noSkinScale.x!=materialScale.x||noSkinScale.y!=materialScale.y)throw std::logic_error("Untextured model UV scale regression");
    alternate.width=336;alternate.height=220;
    auto movieScale=model_uv_scale(5,materialScale,&alternate);
    if(!close(movieScale.x,64/21.0f)||!close(movieScale.y,-192/55.0f))throw std::logic_error("Native resized movie skin UV regression");
    alternate.width=32768;alternate.height=8;
    auto powScale=model_uv_scale(4,materialScale,&alternate);
    if(powScale.x!=materialScale.x||powScale.y!=materialScale.y)throw std::logic_error("Native power-of-two model UV regression");
    for(auto size:{std::array<std::uint32_t,2>{0,1},{1,0},{32769,1},{1,32769}}){
        alternate.width=size[0];alternate.height=size[1];bool rejected=false;
        try{model_uv_scale(4,materialScale,&alternate);}catch(const std::runtime_error&){rejected=true;}
        if(!rejected)throw std::logic_error("Invalid model skin UV dimensions accepted");
    }
    Camera camera;WindowMetrics metrics{1280,720,2560,1440,2,2};
    auto center=project({100,0,0},camera,metrics),left=project({100,10,0},camera,metrics),up=project({100,0,10},camera,metrics);
    if(std::abs(center.x-640)>0.001f||std::abs(center.y-360)>0.001f||left.x>=center.x||up.y>=center.y)throw std::logic_error("Z-up projection regression");
    camera.horizontalFov=-camera.horizontalFov;if(project({100,10,0},camera,metrics).x<=center.x)throw std::logic_error("Horizontal mirror regression");
    camera.pixelAspect=-1;if(project({100,0,10},camera,metrics).y<=center.y)throw std::logic_error("Vertical mirror regression");
    if(project({-100,0,0},camera,metrics).z>=0)throw std::logic_error("Behind-camera projection regression");
    camera.horizontalFov=75;camera.pixelAspect=1;auto wide=project({100,0,10},camera,metrics);metrics.width=960;
    if(std::abs(project({100,0,10},camera,metrics).y-wide.y)>0.001f)throw std::logic_error("Widescreen vertical FOV regression");
    FontAtlas font;font.lineHeight=12;for(auto&glyph:font.glyphs)glyph.advance=8;
    auto extent=text_extent(font,"AB\nC");if(extent.x!=16||extent.y!=24||text_extent(font,"A\rB").x!=24)throw std::logic_error("Legacy LF-only text measurement regression");
    float amplitude=32*0.05f,angleScale=static_cast<float>(6.28318/31);if(turbulence_offset(0,amplitude,angleScale)!=1||turbulence_offset(8,amplitude,angleScale)!=3||turbulence_offset(24,amplitude,angleScale)!=0)throw std::logic_error("Native turbulence displacement regression");
    if(surface_ambient(0,0,false)!=0||std::abs(surface_ambient(0,0,true)-63/255.0f)>0.000001f||surface_ambient(50,0,false)!=50/255.0f)throw std::logic_error("Native lightmap ambient regression");
    if(model_ambient(0,0)!=127/255.0f||model_ambient(53,0)!=180/255.0f||model_ambient(-0.1f,0)!=126/255.0f||model_ambient(100,100)!=254/255.0f)throw std::logic_error("Native neutral model ambient regression");
    if(surface_reflection(0,1,false)!=0||std::abs(surface_reflection(100,1,false)-254/255.0f)>0.000001f||std::abs(surface_reflection(100,5.1f,true)-20/255.0f)>0.000001f)throw std::logic_error("Native reflection byte-wrap regression");
    Matrix skyBasis=transform({},Vec3{0,90,0},{1,1,1});
    if(std::abs(skyBasis[0])>0.000001f||std::abs(skyBasis[2]-1)>0.000001f||std::abs(skyBasis[5]-1)>0.000001f||std::abs(skyBasis[8]+1)>0.000001f)throw std::logic_error("Native Z-up sky orientation regression");
    {
    auto require=[](bool condition,const char*message){if(!condition)throw std::runtime_error(message);};
    auto near=[](float actual,float expected){return std::abs(actual-expected)<0.00001f;};
    auto rejects=[&](auto operation){
        bool rejected=false;try{operation();}catch(const std::runtime_error&){rejected=true;}
        require(rejected,"Renderer accepted invalid material or tangent data");
    };
    std::array<ScriptMaterialState,1>materials{};
    RenderFrame frame;frame.materials=materials;
    require(script_material(&frame,~0u)==nullptr,"Absent script material lost its null distinction");
    require(script_material(&frame,0)==&materials[0],"Renderer material lookup did not consume live packet");
    materials[0].alpha=37;materials[0].ambient={255,40,20,1};
    materials[0].skillBits[0]=std::bit_cast<std::uint32_t>(0.625f);
    require(script_material(&frame,0)->alpha==37&&script_material(&frame,0)->ambient.g==40&&
        std::bit_cast<float>(script_material(&frame,0)->skillBits[0])==0.625f,"Live material changes or packed float bits were lost");
    rejects([&]{script_material(&frame,1);});
    rejects([&]{script_material(nullptr,0);});
    {
    DrawInstance passDraw;Camera passCamera;passCamera.perDrawOpacity=true;
    auto state=model_pass_state(passDraw,passCamera,false,true);
    require(!state.blend&&!state.additive&&state.depthWrite,"Opaque native model did not disable blending and retain depth writes");
    passDraw.alpha=passCamera.alpha=0.999f;
    state=model_pass_state(passDraw,passCamera,false,true);
    require(!state.blend&&state.depthWrite,"Native model pass threshold became inclusive at .999");
    passDraw.alpha=0.998f;passCamera.alpha=1;
    state=model_pass_state(passDraw,passCamera,false,true);
    require(state.blend&&!state.additive&&!state.depthWrite,"Entity opacity did not select translucent native model state");
    passDraw.alpha=1;passCamera.alpha=0.998f;
    state=model_pass_state(passDraw,passCamera,false,true);
    require(state.blend&&!state.additive&&state.depthWrite,"View-only opacity disabled opaque model depth writes");
    passCamera.alpha=1;
    state=model_pass_state(passDraw,passCamera,true,true);
    require(state.blend&&!state.depthWrite,"Native bitmap alpha-format flag did not select translucent state");
    passDraw.translucent=true;
    state=model_pass_state(passDraw,passCamera,false,true);
    require(state.blend&&!state.depthWrite,"Explicit TRANSLUCENT did not select native model state");
    passDraw.translucent=false;passDraw.additive=true;
    state=model_pass_state(passDraw,passCamera,false,true);
    require(!state.blend&&!state.additive&&state.depthWrite,"BRIGHT alone made an opaque model additive");
    passDraw.alpha=.5f;
    state=model_pass_state(passDraw,passCamera,false,true);
    require(state.blend&&state.additive&&!state.depthWrite,"Translucent BRIGHT lost its full-view-byte additive gate");
    passCamera.opacityByte=254;
    state=model_pass_state(passDraw,passCamera,false,true);
    require(state.blend&&!state.additive&&!state.depthWrite,"Nonfull view BYTE did not suppress native additive state");
    passDraw={};passCamera={};
    state=model_pass_state(passDraw,passCamera,false,false);
    require(!state.blend&&!state.depthWrite,"Opaque model overrode caller-disabled depth writes");
    Image opaqueBitmap,alphaBitmap;alphaBitmap.nativeTextureAlpha=true;
    std::array<SkinBinding,1> replacement{{{&opaqueBitmap,&alphaBitmap,false,0,true}}};
    const auto current=resolved_skin(&opaqueBitmap,replacement);
    state=model_pass_state(passDraw,passCamera,current.nativeTextureAlpha,true);
    require(state.blend&&!state.depthWrite,"Current replacement bitmap metadata did not select model pass state");
    replacement[0].image=&opaqueBitmap;
    state=model_pass_state(passDraw,passCamera,resolved_skin(&opaqueBitmap,replacement).nativeTextureAlpha,true);
    require(state.blend&&!state.depthWrite,"Movie pixel substitution replaced the current target descriptor alpha");
    const auto absent=resolved_skin(nullptr,replacement);
    state=model_pass_state(passDraw,passCamera,absent.nativeTextureAlpha,true);
    require(!state.blend&&state.depthWrite,"Null current bitmap borrowed alpha state from a possible retained or MATERIAL bank");
    }


    {
    DrawInstance special;special.nativeFloorLight=false;special.nativeSpecialColor={255,255,255};
    special.ambient=90;special.unlit=true;special.bright=true;special.nativeLightScalar=200;
    special.nativeLightDelta={30,40,50};special.nativeLightBias={300,400,500};
    Camera view;view.nativeAmbient=127;
    NativeEntityLightState cached{127,{100,110,120},{1000,2000,3000}};
    auto color=material_ambient_light(special,&view,&cached);
    require(color.r==1&&color.g==1&&color.b==1&&color.a==1,"Special entity color used ambient recipe or cache instead of direct packing");
    special.nativeSpecialColor={128,128,128};color=material_ambient_light(special,&view,&cached);
    require(near(color.r,128.0f/255)&&near(color.g,128.0f/255)&&near(color.b,128.0f/255),"Special declaration birth128 was replaced with zero or255");
    special.nativeSpecialColor={1,2,3};color=material_ambient_light(special,&view,&cached);
    require(near(color.r,3.0f/255)&&near(color.g,2.0f/255)&&near(color.b,1.0f/255),"Already SAR10 special color was shifted again or reordered");
    special.nativeSpecialColor={256,0,1};color=material_ambient_light(special,&view,&cached);
    require(near(color.r,1.0f/255)&&near(color.g,1.0f/255)&&color.b==0,"Special color out-of-range OR contamination was clamped or rescaled");
    special.nativeSpecialColor={-1,0,0};color=material_ambient_light(special,nullptr);
    require(color.r==1&&color.g==1&&color.b==1,"Negative special color packing lost unsigned OR contamination");
    require(near(material_light_strength(51200,cached.scalar,false,true),127.0f/255),"Special color changed independent DTO ALBEDO strength");
    }
    {
    Bounds bounds;include_point(bounds,{-1,-1,-1});include_point(bounds,{1,1,1});
    Camera view;view.farClip=100;Matrix vp=camera_matrix(view,4.0f/3);
    const std::array<float,4> noPlane{};
    require(geometry_eligible(bounds,transform({10,0,0},{},{1,1,1}),vp,noPlane),"Visible transformed bounds were culled");
    for(Vec3 outside:std::array<Vec3,4>{{{-10,0,0},{102,0,0},{10,100,0},{10,0,100}}})
        require(!geometry_eligible(bounds,transform(outside,{},{1,1,1}),vp,noPlane),"Wholly clipped geometry reached light preparation");
    Bounds crossing;include_point(crossing,{-2,-200,-200});include_point(crossing,{2,200,200});
    require(geometry_eligible(crossing,transform({1,0,0},{},{1,1,1}),vp,noPlane),"Frustum-spanning geometry was culled because its corners or origin were outside");
    const std::array<float,4> portal{0,1,0,0};
    require(!geometry_eligible(bounds,transform({10,-2,0},{},{1,1,1}),vp,portal),"Portal rejected half-space reached light preparation");
    require(geometry_eligible(bounds,transform({10,-1,0},{},{1,1,1}),vp,portal),"Portal plane contact was not conservatively retained");
    require(geometry_eligible(bounds,transform({10,0,0},{0,0,45},{1,-2,3}),vp,portal),"Rotated negative/nonuniform scale bounds were culled");
    Bounds next;include_point(next,{20,0,0});include_point(next,{22,2,2});
    auto mixed=interpolated_bounds(bounds,next,.5f);
    require(near(mixed.minimum.x,9.5f)&&near(mixed.maximum.x,11.5f),"Animation eligibility did not interpolate endpoint bounds");
    Model model;std::array<Vec3,2>deformed{{{5,0,0},{7,1,1}}};MeshOverride cached;
    auto first=mesh_override_bounds(cached,model,4,deformed);deformed[0].x=9;
    require(mesh_override_bounds(cached,model,4,deformed).minimum.x==first.minimum.x,"Unchanged override revision recopied bounds");
    require(mesh_override_bounds(cached,model,5,deformed).minimum.x==7,"Changed override revision retained stale geometry bounds");
    set_override_pose(cached,model,5,deformed.size(),0,0);
    require(!override_tangents_needed(cached,EffectKind::None,5)&&override_tangents_needed(cached,EffectKind::NormalMix,5),
        "Ordinary override eagerly prepared tangents or same-revision FX failed to request them");
    cached.tangentReady=true;cached.tangentRevision=5;
    require(!override_tangents_needed(cached,EffectKind::NormalMix,5)&&override_tangents_needed(cached,EffectKind::NormalMix,6),
        "Override tangent readiness did not track independent revision");
    require(cached.tangentData.empty(),"Ordinary bounds or tangent-readiness path allocated derived CPU tangents");
    set_override_pose(cached,model,6,deformed.size(),0,0);
    require(!override_tangents_needed(cached,EffectKind::None,6)&&override_tangents_needed(cached,EffectKind::NormalMix,6),
        "Next ordinary revision retained ready tangents");
    Model other;std::unordered_map<std::uint32_t,MeshOverride>owned;
    owned[1].model=&model;owned[1].tangentData.resize(6);
    owned[2].model=&other;owned[2].tangentData.resize(3);
    owned[3].boundsModel=&model;
    purge_model_overrides(owned,&model);
    require(owned.size()==1&&owned.contains(2)&&owned.at(2).tangentData.size()==3,
        "Model purge retained matching override CPU resources or erased another model");
    }
    {
    auto require=[](bool condition,const char*message){if(!condition)throw std::runtime_error(message);};
    DrawInstance caster;caster.position={3,4,8};NativeShadowState plane{true,2,{.5f,-.25f,-1}};
    RenderFrame frame;frame.shadowSun={.21650635f,0};frame.shadowOffset=2;
    const Vec3 vertex{5,7,10};const auto projected=transformed_point(planar_projection(caster,plane,frame),vertex);
    const float x=5-.21650635f*8,y=7,z=4+(x-3)*.5f-(y-4)*.25f;
    require(std::abs(projected.x-x)<.00001f&&std::abs(projected.y-y)<.00001f&&std::abs(projected.z-z)<.00001f,"Source planar affine rows changed");
    plane.negativeNormal.z=0;const auto zeroZ=transformed_point(planar_projection(caster,plane,frame),vertex);
    require(projected.x==zeroZ.x&&projected.y==zeroZ.y&&projected.z==zeroZ.z,"Planar projection divided by receiver normal Z");
    std::array<RenderLight,3>lights{};lights[0].type=2;lights[1].type=1;lights[2].type=3;frame.lights=lights;
    require(!centroid_lighting(frame),"Point after spot did not overwrite LIGHTRES0 gate");
    lights[1].type=2;require(centroid_lighting(frame),"Last spot did not enable LIGHTRES0 gate");
    lights[1].synthetic=true;lights[0].type=1;require(!centroid_lighting(frame),"Synthetic light changed source topology gate");
    frame.lightResRaw=-1;require(centroid_lighting(frame),"Nonzero raw LIGHTRES did not enable point gate");
    frame.lights={};require(!centroid_lighting(frame),"LIGHTRES enabled without a source point/spot light");
    for(auto raw:{0,1,1024,2048,-1}){
        require(presentation_limit(WindowMode::Windowed,raw)==1&&presentation_limit(WindowMode::Borderless,raw)==1,"Window requested fullscreen completion depth");
        require(presentation_limit(WindowMode::Fullscreen,raw)==unsigned(raw==0?1:2),"TRIPLEBUFFER raw value was treated as a count");
        require(presentation_interval(WindowMode::Fullscreen,raw,false,true)==(raw==0?1:0),"A6 fullscreen source interval coupling changed");
        require(presentation_interval(WindowMode::Fullscreen,raw,true,true)==1&&presentation_interval(WindowMode::Windowed,raw,false,true)==1&&presentation_interval(WindowMode::Borderless,raw,false,true)==1,"Independent A7/window swap interval changed");
        require(presentation_interval(WindowMode::Fullscreen,raw,false,false)==0&&presentation_interval(WindowMode::Fullscreen,raw,true,false)==0,"Explicit native no-vsync override was lost");
    }
    World world;world.faces.resize(1);world.faces[0].nativeFlags=0x40000;
    world.mesh.vertices={{{-4,-2,0},{0,0,1},{0,0},{0,0}},{{4,-2,0},{0,0,1},{1,0},{1,0}},{{4,2,0},{0,0,1},{1,1},{1,1}},{{-4,2,0},{0,0,1},{0,1},{0,1}}};
    world.mesh.triangles={{{0,2,1},0,0,0},{{0,3,2},0,0,0}};
    std::vector<Vertex>vertices;std::vector<Triangle>triangles;centroid_geometry(world.mesh,&world,vertices,triangles);
    require(vertices.size()==5&&triangles.size()==4&&world.mesh.vertices.size()==4&&world.mesh.triangles.size()==2,"Centroid cache mutated original outer fan");
    require(vertices[4].position.x==0&&vertices[4].position.y==0&&vertices[4].uv.x==.5f&&vertices[4].lightmap_uv.y==.5f,"Native centroid is not boundary attribute average");
    for(std::size_t i=0;i<4;++i){
        require(triangles[i].indices==std::array<std::uint32_t,3>{4,std::uint32_t((i+1)%4),std::uint32_t(i)},"Centroid fan did not retain decoded boundary winding");
        const auto&t=triangles[i];const auto a=vertices[t.indices[0]].position,b=vertices[t.indices[1]].position,c=vertices[t.indices[2]].position;
        require(cross(add(b,mul(a,-1)),add(c,mul(a,-1))).z<0,"Centroid triangle reversed decoded outer fan orientation");
    }
    vertices.clear();triangles.clear();world.faces[0].nativeFlags=0;centroid_geometry(world.mesh,&world,vertices,triangles);
    require(vertices.empty()&&triangles.empty(),"Unqualified face received centroid geometry");
    }
    DrawInstance lit;lit.nativeLightScalar=100;lit.nativeLightDelta={10,20,30};lit.nativeLightBias={1,2,3};
    lit.ambient=20.9f;
    Camera lightView;lightView.nativeAmbient=10.9f;
    auto lighting=material_ambient_light(lit,&lightView);
    require(near(lighting.r,163.0f/255)&&near(lighting.g,152.0f/255)&&near(lighting.b,141.0f/255),
        "Material ambient lost sampled BGR cache, explicit bias or integer entity/view ambient");
    const NativeEntityLightState returnedLight{40,{2,4,6},{3,5,7}};
    lighting=material_ambient_light(lit,&lightView,&returnedLight);
    require(near(lighting.r,83.0f/255)&&near(lighting.g,79.0f/255)&&near(lighting.b,75.0f/255),
        "Returned entity light DTO did not override stored scalar, BGR delta and BGR bias");
    require(near(material_light_strength(51200,returnedLight.scalar,false,true),40.0f/255)&&
        near(material_light_strength(51200,returnedLight.scalar,true,true),40.0f/128)&&
        !near(material_light_strength(51200,returnedLight.scalar,true,true),material_light_strength(51200,lit.nativeLightScalar,true,true)),
        "MATERIAL ALBEDO strength lost the same returned entity light sample");
    lit.unlit=true;lighting=material_ambient_light(lit,&lightView);
    require(near(lighting.r,63.0f/255)&&near(lighting.g,52.0f/255)&&near(lighting.b,41.0f/255),
        "UNLIT material retained the sampled scalar or discarded explicit BGR bias");
    lit.bright=true;lighting=material_ambient_light(lit,&lightView);
    require(near(lighting.r,190.0f/255)&&near(lighting.g,179.0f/255)&&near(lighting.b,168.0f/255),
        "UNLIT BRIGHT material omitted its source 127 scalar");
    lit={};lit.nativeLightScalar=200;lit.nativeLightDelta={0,100,200};
    lighting=material_ambient_light(lit,nullptr);
    require(lighting.r==1&&near(lighting.g,191.0f/255)&&near(lighting.b,127.0f/255),
        "Material ambient did not uniformly rescale overbright channels before native byte packing");
    lit={};lit.nativeLightScalar=100;lightView.nativeAmbient=-20;
    require(near(material_ambient_light(lit,&lightView).r,100.0f/255),"A6 material view ambient failed its nonnegative clamp");
    lightView.perDrawOpacity=true;
    require(near(material_ambient_light(lit,&lightView).r,80.0f/255),"A7 material view ambient lost its signed contribution");
    lit.nativeLightScalar=200;lit.ambient=32768;lightView.nativeAmbient=0;
    require(near(material_ambient_light(lit,&lightView).r,73.0f/255),"Material entity ambient lost source signed-short conversion");
    require(near(material_light_strength(51200,127,false,true),127.0f/255)&&
        near(material_light_strength(51200,127,true,true),127.0f/128)&&material_light_strength(51200,127,true,false)==1,
        "Material light strength conflated A6 255, A7 fixed128 and linked/direct127 denominators");
    require(material_light_strength(0,255,false,true)==0&&material_light_strength(0,255,true,true)==0&&
        near(material_light_strength(-8192,127,false,true),11.0f/255)&&
        near(material_light_strength(-8192,127,true,true),11.0f/128)&&material_light_strength(-8192,127,true,false)==0,
        "Material light strength lost source-specific signed ALBEDO enable gates");
    require(near(material_light_strength(501,127,false,true),27.0f/255)&&
        near(material_light_strength(512,127,false,true),28.0f/255),"Material light strength rounded its doubled raw ALBEDO");
    require(near(material_light_strength(INT32_MAX,127,false,true),26.0f/255)&&
        near(material_light_strength(INT32_MIN,127,false,true),27.0f/255),"Material ALBEDO lost original wrapping multiply before arithmetic SAR10");
    std::array<RenderLight,3> selectedLights{};
    selectedLights[0].baked=selectedLights[0].synthetic=true;
    selectedLights[0].type=3;
    selectedLights[2].synthetic=true;selectedLights[2].type=3;
    RenderFrame selectedFrame;selectedFrame.lights=selectedLights;
    require(material_selected_light(selectedFrame)==2,"Selected SUN identity followed compact slot0 or serialized baked light");
    selectedFrame.lights=std::span(selectedLights).first(2);
    require(material_selected_light(selectedFrame)==-1,"Absent SUN silently selected an actor or baked light");

    Image colorSource,normalSource,colorReplacement,normalReplacement;
    colorReplacement.width=30;colorReplacement.height=18;
    normalReplacement.width=17;normalReplacement.height=9;
    std::array<SkinBinding,2>bindings{{{&normalSource,&normalReplacement,true,29},{&colorSource,&colorReplacement,true,13}}};
    auto color=resolved_skin(&colorSource,bindings),normal=resolved_skin(&normalSource,bindings);
    require(color.source==&colorSource&&color.image==&colorReplacement&&color.dynamic&&color.serial==13&&
        normal.source==&normalSource&&normal.image==&normalReplacement&&normal.dynamic&&normal.serial==29,
        "Two skin banks lost independent replacement source identity");
    auto colorUV=model_uv_scale(7,{1,1},color.image),normalUV=model_uv_scale(7,{1,1},normal.image);
    require(near(colorUV.x,32.0f/30)&&near(colorUV.y,32.0f/18)&&near(normalUV.x,32.0f/17)&&near(normalUV.y,16.0f/9),
        "Effect banks did not compensate UVs for each selected replacement image");
    require(resolved_skin(&normalSource,{}).image==&normalSource&&resolved_skin(nullptr,bindings).image==nullptr,
        "Missing normal bank acquired a fabricated texture");

    for(std::uint32_t raw:{0u,1u,2u,0x7fffffffu,0x80000000u,0xffffffffu}){
        require(display_material_index(7,raw)==(std::bit_cast<std::int32_t>(raw)<0?0u:raw),
            "MDL7 display batching lost negative-face normalization or changed positive faces");
        for(std::uint32_t format:{0u,4u,5u,6u,105u})
            require(display_material_index(format,raw)==raw,
                "Non-MDL7 display batching changed a raw face material index");
    }

    Model banks;banks.formatVersion=7;banks.materials.resize(5);
    auto bankImage=std::make_shared<Image>();
    require(native_skin_layers(banks,4)==0,"Null MDL7 banks fabricated skin layers");
    banks.materials[0].texture=bankImage;banks.materials[1].texture=bankImage;
    require(native_skin_layers(banks,1)==0&&native_skin_layers(banks,2)==2,
        "MDL7 layer threshold must be strictly maximum greater than bank1");
    banks.materials[1].nativePrimarySubset=true;
    require(native_skin_layers(banks,2)==0,"Alias-produced primary flag was ignored for bank1");
    banks.materials[1].nativePrimarySubset=false;
    require(native_skin_layers(banks,2)==2,"Alias image identity replaced its own nonprimary eligibility flag");
    banks.materials[1].texture.reset();
    require(native_skin_layers(banks,2)==0,"Null nonprimary bank1 fabricated a second skin layer");
    banks.materials[2].texture=bankImage;
    require(native_skin_layers(banks,2)==0&&native_skin_layers(banks,3)==3,
        "MDL7 bank2 layer threshold lost its strict maximum comparison");
    banks.materials[2].nativePrimarySubset=true;
    require(native_skin_layers(banks,3)==0,"Primary bank2 was reclassified as an auxiliary skin layer");
    banks.materials[2].texture.reset();banks.materials[3].texture=bankImage;
    require(native_skin_layers(banks,3)==0&&native_skin_layers(banks,4)==4,
        "MDL7 bank3 layer threshold lost its strict maximum comparison");
    banks.materials[3].nativePrimarySubset=true;
    require(native_skin_layers(banks,4)==0,"Primary bank3 was reclassified as an auxiliary skin layer");
    banks.materials[3].texture.reset();banks.materials[4].texture=bankImage;
    require(native_skin_layers(banks,5)==0,"MDL7 layer scan incorrectly included bank4");
    banks.materials[1].texture=banks.materials[2].texture=banks.materials[3].texture=bankImage;
    banks.materials[2].nativePrimarySubset=banks.materials[3].nativePrimarySubset=false;
    require(native_skin_layers(banks,4)==4,"MDL7 layer scan did not retain the last eligible bank");
    banks.materials.resize(2);
    require(native_skin_layers(banks,4)==2,"MDL7 layer scan did not respect the material bank count");
    banks.materials.clear();
    require(native_skin_layers(banks,4)==0,"Empty MDL7 bank table fabricated skin layers");

    Model model;
    model.vertices={{{0,0,0},{0,0,2},{0,0},{}},{{1,0,0},{0,0,2},{1,0},{}},{{0,1,0},{0,0,2},{0,1},{}},
        {{0,0,0},{0,0,2},{0,0},{}},{{1,0,0},{0,0,2},{0,1},{}},{{0,1,0},{0,0,2},{1,0},{}}};
    model.triangles={{{0,1,2},0,0},{{3,4,5},0,0}};
    model.vertexSources={0,1,2,0,1,2};
    std::array<Vec3,6>tangents{};
    derive_tangents(model,{},{},tangents);
    require(near(tangents[0].x,1)&&near(tangents[0].y,0)&&near(tangents[3].x,0)&&near(tangents[3].y,1),
        "Display-index UV seam tangent bases were merged");
    AnimationFrame pose;pose.positions={{0,0,0},{0,1,0},{-1,0,0},{0,0,0},{0,1,0},{-1,0,0}};
    pose.normals.assign(6,{0,0,3});
    model.frames.push_back(pose);
    derive_tangents(model,model.frames[0].positions,model.frames[0].normals,tangents);
    require(near(tangents[0].x,0)&&near(tangents[0].y,1)&&near(tangents[3].x,-1)&&near(tangents[3].y,0),
        "Animated tangent generation retained the static pose");
    std::array<Vec3,6>deformed{{{0,0,0},{1,0,1},{0,1,0},{0,0,0},{1,0,1},{0,1,0}}};
    std::array<Vec3,6>normals;normals.fill({-1,0,1});
    derive_tangents(model,deformed,normals,tangents);
    const float diagonal=std::sqrt(0.5f);
    require(near(tangents[0].x,diagonal)&&near(tangents[0].z,diagonal)&&near(dot(tangents[0],normals[0]),0),
        "Deformed tangent generation ignored supplied positions or normals");
    normals.fill({1,1,0});derive_tangents(model,{},normals,tangents);
    require(near(tangents[0].x,diagonal)&&near(tangents[0].y,-diagonal)&&near(tangents[0].z,0),
        "Normal-only mesh override did not project its tangent basis");
    rejects([&]{derive_tangents(model,std::span(deformed).first(5),{},tangents);});
    rejects([&]{derive_tangents(model,{},std::span(normals).first(5),tangents);});
    rejects([&]{derive_tangents(model,{},{},std::span(tangents).first(5));});
    for(auto&vertex:model.vertices)vertex.uv={0,0};
    derive_tangents(model,{},{},tangents);
    for(auto tangent:tangents)require(dot(tangent,tangent)==0,"UV-degenerate tangent invented a normal-map basis");
    model.triangles[0].indices[2]=6;
    rejects([&]{derive_tangents(model,{},{},tangents);});

    Camera camera;camera.perDrawOpacity=true;camera.alpha=0.4f;camera.opacityByte=101;
    require(draw_opacity(1,&camera)==0.4f&&draw_opacity(0.2f,&camera)==0.2f,
        "Geometry opacity did not take the entity/view FLOAT minimum");
    camera.opacityByte=0;
    require(draw_opacity(1,&camera)==0.4f,"Geometry FLOAT opacity was confused with view BYTE opacity");
    camera.alpha=1;camera.opacityByte=254;
    require(draw_opacity(1,&camera)==1,"Geometry full FLOAT opacity was quantized to the view BYTE");
    camera.alpha=0;
    require(draw_opacity(1,&camera)==0,"Zero-opacity view did not suppress geometry");
    camera.perDrawOpacity=false;
    require(draw_opacity(0.8f,&camera)==0.8f&&draw_opacity(0.8f,nullptr)==0.8f,
        "Disabled per-draw opacity or camera-free draw changed entity opacity");
    require(near(particle_alpha(20*1024,true,false,true),50.0f/255)&&
        near(particle_alpha(48*1024,true,false,true),120.0f/255)&&
        near(particle_alpha(100*1024,true,false,true),250.0f/255),
        "Translucent particle alpha lost its native fixed-point quantization");
    require(near(particle_alpha(20*1024,true,true,true),100.0f/255)&&particle_alpha(100*1024,true,true,true)==1,
        "Textured bright particle alpha lost its doubled native conversion");
    require(near(particle_alpha(20*1024,true,true,false),50.0f/255)&&
        near(particle_alpha(100*1024,true,true,false),250.0f/255),
        "Untextured bright particle incorrectly received the texture-only doubled alpha");
    require(particle_alpha(0,false,false,true)==1&&particle_alpha(20*1024,false,true,false)==1&&
        particle_alpha(-20*1024,true,false,true)==0&&particle_alpha(200*1024,true,false,true)==1&&
        particle_alpha(std::numeric_limits<std::int32_t>::max(),true,false,true)==1&&
        particle_alpha(std::numeric_limits<std::int32_t>::min(),true,false,true)==0,
        "Opaque particle alpha or translucent byte clamping regressed");
    }
}
void GLBackend::draw(const RenderFrame&f,WindowMetrics m){
    auto&i=*impl_;if(m.drawableWidth<=0||m.drawableHeight<=0)return;
    i.finalFrameValid=false;
    i.currentFrame=&f;
    struct PacketScope { Impl& owner; ~PacketScope(){owner.currentFrame=nullptr;} } packetScope{i};
    if(f.world&&!f.hardwareLighting)throw std::runtime_error("Legacy software world-light topology is not implemented by the native OpenGL hardware path");
    i.admit_frame(f.tripleBufferRaw,f.bonusProfile);i.centroidLighting=centroid_lighting(f);
    i.configure_sampling(f.mipmapping,f.anisotropy);
    i.targets(m.drawableWidth,m.drawableHeight);
    // Primary view: first full-window background world view. Shadow maps, SSAO and volumetric fog follow it.
    const Camera*primary=nullptr;
    for(const auto&camera:f.cameras)if(!camera.foreground&&!camera.portalTarget&&camera.drawWorld){
        const Rect v=camera.viewport;if(v.width<=0||v.height<=0||(v.x<=0&&v.y<=0&&v.x+v.width>=m.width&&v.y+v.height>=m.height)){primary=&camera;break;}
    }
    // Shadow maps render before the scene targets are bound and cleared (switching away from a fresh MSAA clear is costly).
    i.shadowsReady=false;i.shadowLight=-1;
    if(i.shadowMapsOn){
        if(primary){Rect v=primary->viewport;if(v.width<=0||v.height<=0)v={0,0,float(m.width),float(m.height)};i.shadow_pass(f,*primary,float(std::lround(v.width*m.scaleX))/float(std::lround(v.height*m.scaleY)));}
        else{glUseProgram(i.scene);glUniform1i(i.su.shadowMaps,0);}
    }
    glBindFramebuffer(GL_FRAMEBUFFER,i.composite?i.hdrFbo:i.msaaFbo);
    // PBR background views composite through the tone curve; seed clears with its inverse so untouched pixels round-trip.
    auto seed=[&](Color c){return i.options.pbr?inverse_tone_map(c):c;};
    glViewport(0,0,m.drawableWidth,m.drawableHeight);glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);glStencilMask(255);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearDepth(1);glClearStencil(0);
    bool nativeCameraPasses=std::any_of(f.cameras.begin(),f.cameras.end(),[](const Camera&camera){return camera.perDrawOpacity;});
    if(!nativeCameraPasses||!i.retainedColorValid){
        // Deterministic native seed; original D3D DISCARD retention is unspecified.
        const auto clearColor=seed(f.clearColor);glClearColor(clearColor.r,clearColor.g,clearColor.b,clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);i.retainedColorValid=true;
    }
    if(nativeCameraPasses&&f.clearScreen){
        const Camera*firstCamera=nullptr;
        for(const auto&camera:f.cameras)if(!camera.foreground&&!camera.portalTarget){firstCamera=&camera;break;}
        if(!firstCamera)for(const auto&camera:f.cameras)if(camera.foreground&&!camera.portalTarget){firstCamera=&camera;break;}
        Rect viewport=firstCamera?firstCamera->viewport:Rect{};
        bool fullScreen=firstCamera&&(viewport.width<=0||viewport.height<=0||(viewport.x<=0&&viewport.y<=0&&viewport.x+viewport.width>=m.width&&viewport.y+viewport.height>=m.height));
        if(!fullScreen){
            // The recovered outer clear covers the screen, not just viewport margins.
            const auto screen=seed(f.screenColor);glClearColor(screen.r,screen.g,screen.b,screen.a);glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    glEnable(GL_MULTISAMPLE);glDepthFunc(GL_LEQUAL);glCullFace(GL_BACK);
    // Engine packets retain native source-slot order first, then enhanced baked lights.
    // Preserve that priority within the existing 32-light shader upload budget.
    const auto uploadedLightCount=std::min<std::size_t>(f.lights.size(),32);
    const int selectedLight=material_selected_light(f);
    bool changed=i.lightData.size()!=uploadedLightCount*5;
    i.lightData.resize(uploadedLightCount*5);
    i.lightCones.resize(uploadedLightCount,{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::quiet_NaN()});
    for(std::size_t index=0;index<uploadedLightCount;++index){
        const auto&l=f.lights[index];
        auto cone=i.lightData[index*5+4];
        if(l.type==2&&(i.lightCones[index].x!=l.theta||i.lightCones[index].y!=l.phi)){
            i.lightCones[index]={l.theta,l.phi};cone[0]=std::cos(rad(l.theta)*.5f);cone[1]=std::cos(rad(l.phi)*.5f);
        }
        const std::array<std::array<float,4>,5>data{{
            {l.position.x,l.position.y,l.position.z,l.range},
            {l.color.r,l.color.g,l.color.b,l.baked?-l.intensity:l.intensity},
            {l.direction.x,l.direction.y,l.direction.z,float(l.type)},
            {l.attenuation.x,l.attenuation.y,l.attenuation.z,l.falloff},
            {cone[0],cone[1],l.synthetic?1.f:0.f,0}}};
        for(std::size_t word=0;word<data.size();++word){auto&cached=i.lightData[index*5+word];if(cached!=data[word]){cached=data[word];changed=true;}}
    }
    glBindBuffer(GL_TEXTURE_BUFFER,i.lightBuffer);
    if(i.lightData.size()>i.lightCapacity){i.lightCapacity=std::max<std::size_t>(i.lightData.size(),64);glBufferData(GL_TEXTURE_BUFFER,i.lightCapacity*sizeof(i.lightData[0]),nullptr,GL_DYNAMIC_DRAW);changed=true;}
    if(changed&&!i.lightData.empty())glBufferSubData(GL_TEXTURE_BUFFER,0,i.lightData.size()*sizeof(i.lightData[0]),i.lightData.data());
    auto ui_state=[&]{
        glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_CLIP_DISTANCE0);glEnable(GL_BLEND);
        glViewport(0,0,m.drawableWidth,m.drawableHeight);glUseProgram(i.ui);glUniform2f(i.uiViewport,m.width,m.height);glUniform1i(i.uiImage,0);
        glUniform1i(i.uiTextKey,0);
        glUniform1i(i.uiCursorKey,0);
    };
    auto draw_ui=[&](const UICommand&command){
        ui_state();
        if(command.clip.width>0&&command.clip.height>0){glEnable(GL_SCISSOR_TEST);glScissor(std::lround(command.clip.x*m.scaleX),std::lround((m.height-command.clip.y-command.clip.height)*m.scaleY),std::lround(command.clip.width*m.scaleX),std::lround(command.clip.height*m.scaleY));}
        i.ui_command(command);
    };
    const auto&fx=i.options.features;
    auto draw_camera=[&](auto&self,const Camera&camera,bool targetOnly,const PortalSurface*targetPlane)->void{
        Rect v=camera.viewport;if(v.width<=0||v.height<=0)v={0,0,float(m.width),float(m.height)};
        int x=std::lround(v.x*m.scaleX),y=std::lround((m.height-v.y-v.height)*m.scaleY),w=std::lround(v.width*m.scaleX),h=std::lround(v.height*m.scaleY);
        if(w<=0||h<=0||(!camera.perDrawOpacity&&camera.alpha<=0))return;
        const bool hdrView=i.composite&&!camera.foreground&&!targetOnly;const GLuint destination=hdrView?i.hdrFbo:i.msaaFbo;
        auto seeded=[&](Color c){return hdrView&&i.options.pbr?inverse_tone_map(c):c;};
        const Camera*portalCamera=nullptr;std::optional<PortalSurface>surface;
        if(!targetOnly&&camera.portalId){
            for(const auto&candidate:f.cameras)if(candidate.sourceId==camera.portalId){portalCamera=&candidate;break;}
            if(!portalCamera)throw std::runtime_error("Attached portal view is absent from game frame");
            surface=Renderer::portal_surface(f,camera,m);
            if(surface){
                i.portal_target();
                self(self,*portalCamera,true,&*surface);
                glBindFramebuffer(GL_FRAMEBUFFER,destination);
            }
        }
        bool translucent=!targetOnly&&!camera.perDrawOpacity&&camera.alpha<1;
        if(targetOnly){
            i.portal_target();glBindFramebuffer(GL_FRAMEBUFFER,i.portalFbo);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glStencilMask(255);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            if(!camera.perDrawOpacity||!i.portalColorValid){
                Color background=camera.environmentOverride?camera.background:f.clearColor;glClearColor(background.r,background.g,background.b,background.a);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);i.portalColorValid=true;
            }
        }
        if(translucent){
            i.alpha_target();glBindFramebuffer(GL_FRAMEBUFFER,i.alphaFbo);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);
            Color background=seeded(camera.foreground?Color{0,0,0,0}:(camera.environmentOverride?camera.background:f.clearColor));
            glClearColor(background.r,background.g,background.b,background.a);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        }
        glUseProgram(i.scene);glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);glDepthMask(GL_TRUE);
        glUniform1i(i.su.pbr,i.options.pbr);glUniform1i(i.su.toneInShader,i.options.pbr&&!hdrView);glUniform1i(i.su.pbrNormal,6);glUniform1i(i.su.pbrOrm,7);
        i.portalPass=targetOnly;
        glBindSampler(0,i.sceneSampler);glBindSampler(1,i.lightmapSampler);
        glFrontFace((camera.horizontalFov<0) != (camera.pixelAspect<0)?GL_CW:GL_CCW);
        glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_BUFFER,i.lightTexture);
        glUniform1i(i.su.skin,0);glUniform1i(i.su.lightmap,1);glUniform1i(i.su.poses,2);glUniform1i(i.su.lights,3);glUniform1i(i.su.lightCount,i.lightData.size()/5);
        glUniform1i(i.su.portalImage,4);glUniform1i(i.su.portalEnabled,surface.has_value());
        if(surface){
            Rect portalViewport=portalCamera->viewport;if(portalViewport.width<=0||portalViewport.height<=0)portalViewport={0,0,float(m.width),float(m.height)};
            Matrix reflected=camera_matrix(*portalCamera,portalViewport.width/portalViewport.height);
            glUniformMatrix4fv(i.su.portalProjection,1,GL_FALSE,reflected.data());glUniform4f(i.su.portalViewport,portalViewport.x/m.width,(m.height-portalViewport.y-portalViewport.height)/m.height,portalViewport.width/m.width,portalViewport.height/m.height);
            glUniform4f(i.su.portalPlane,surface->normal.x,surface->normal.y,surface->normal.z,-dot(surface->normal,surface->position));
            glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,i.portalColor);glBindSampler(4,i.uiLinearSampler);
        }
        i.geometryClipPlane={};
        if(camera.portalClip){
            Vec3 position=targetPlane?targetPlane->position:camera.portalPosition,normal=targetPlane?targetPlane->normal:camera.portalNormal;
            if(dot(normal,normal)==0)throw std::runtime_error("Portal clip view has no real surface plane");
            normal=unit(normal);
            i.geometryClipPlane={normal.x,normal.y,normal.z,-dot(normal,position)};
            glEnable(GL_CLIP_DISTANCE0);
        }else glDisable(GL_CLIP_DISTANCE0);
        // Every camera supplies the same plane to CPU eligibility and the actual scene shader.
        glUniform4fv(i.su.clipPlane,1,i.geometryClipPlane.data());
        if(targetOnly){glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,i.white);}
        Color fog=camera.environmentOverride?camera.fogColor:f.fogColor;
        glUniform3f(i.su.sunDirection,f.sunDirection.x,f.sunDirection.y,f.sunDirection.z);
        glUniform3f(i.su.fogColor,fog.r,fog.g,fog.b);glUniform2f(i.su.fogRange,camera.environmentOverride?camera.fogStart:f.fogStart,camera.environmentOverride?camera.fogEnd:f.fogEnd);
        glViewport(x,y,w,h);glEnable(GL_SCISSOR_TEST);glScissor(x,y,w,h);
        if(camera.perDrawOpacity){
            GLbitfield clear=(camera.clearColor?GL_COLOR_BUFFER_BIT:0)|(camera.clearDepth?GL_DEPTH_BUFFER_BIT:0)|(camera.clearStencil?GL_STENCIL_BUFFER_BIT:0);
            Color background=seeded(camera.environmentOverride?camera.background:f.clearColor);
            glClearColor(background.r,background.g,background.b,background.a);glStencilMask(255);
            if(clear)glClear(clear);
        }else if(camera.clearDepth)glClear(GL_DEPTH_BUFFER_BIT);
        i.viewProjection=camera_matrix(camera,float(w)/h);glUniformMatrix4fv(i.su.vp,1,GL_FALSE,i.viewProjection.data());glUniform3f(i.su.cameraPosition,camera.position.x,camera.position.y,camera.position.z);
        // Original D3D VIEW is LH: LOCALVIEWER=false +Z is world forward, not GL VIEW +Z.
        auto basis=transform({},camera.angles,{1,1,1});glUniform3f(i.su.infiniteViewer,basis[0],basis[1],basis[2]);glUniform1i(i.su.selectedLight,selectedLight);
        // A6 queue membership observes the existing cache before this view's ordinary updater.
        i.shadowOrder.clear();
        if(camera.drawWorld&&!camera.noShadow&&!camera.noEntities&&f.renderEntities&&(!f.bonusProfile||f.planarCapable)&&!f.shadows.empty()){
            if(f.bonusProfile?f.shadowStencilRaw>0:f.shadowStencilRaw!=0)throw std::runtime_error("Nondefault source stencil-volume shadows are not implemented; native OpenGL supports the recovered planar route");
            for(const auto&d:f.shadows){
                if(!(d.layers&camera.layers)||(d.shadowViewId&&d.shadowViewId!=camera.sourceId)||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
                const auto cached=f.cachedEntityShadow?f.cachedEntityShadow(f.lightContext,d):d.nativeShadow;
                if(f.bonusProfile||cached.caster)i.shadowOrder.push_back(&d);
            }
            std::sort(i.shadowOrder.begin(),i.shadowOrder.end(),[&](const DrawInstance*a,const DrawInstance*b){
                Vec3 da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));if(!f.shadowSortVertical)da.z=db.z=0;return dot(da,da)>dot(db,db);
            });
        }
        glUniform1i(i.su.skyMask,0);i.draw_sky(f,camera,w,h);
        if(camera.drawWorld&&f.world&&(camera.layers&1)){DrawInstance world;world.model=&f.world->mesh;i.draw_instance(world,f.skinBindings,&camera);}
        i.transparent.clear();
        for(const auto&d:f.instances){
            if(camera.noEntities||!f.renderEntities||!(d.layers&camera.layers)||d.viewSpace||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
            if(camera.perDrawOpacity&&d.model&&model_has_entity_skins(*d.model)){
                if(d.scale.x==0||d.scale.y==0||d.scale.z==0)continue;
                if(!d.visible){i.draw_instance(d,f.skinBindings,&camera);continue;}
                const unsigned queues=i.model_queue(d,f.skinBindings);
                if(queues&1)i.draw_instance(d,f.skinBindings,&camera,true,0);
                if(queues&2)i.transparent.push_back(&d);
                continue;
            }
            if(blended(d))i.transparent.push_back(&d);else i.draw_instance(d,f.skinBindings,&camera);
        }
        std::sort(i.transparent.begin(),i.transparent.end(),[&](const DrawInstance*a,const DrawInstance*b){Vec3 da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));return dot(da,da)>dot(db,db);});
        glDepthMask(GL_FALSE);for(const auto*d:i.transparent){
            bool native=camera.perDrawOpacity&&d->model&&model_has_entity_skins(*d->model);
            i.draw_instance(*d,f.skinBindings,&camera,false,native?1:-1);
        }
        for(const auto*d:i.shadowOrder)i.draw_planar(*d,camera,w,h);
        glDepthMask(GL_FALSE); // Preserve the existing CPU-particle no-depth-write pass after shadow duplicates.
        if(camera.drawWorld&&!f.particles.empty()){
            // A6/A7 CPU quads ignore source MATERIAL and camera opacity; do not tint MAT_PARTICLE.
            i.material_uniforms(nullptr);
            glDisable(GL_CULL_FACE);glUniform1i(i.su.particleInstanced,1);glUniform1i(i.su.spriteFrames,1);glUniform1i(i.su.unlit,1);glUniform1i(i.su.hasLightmap,0);
            glUniform2f(i.su.uvScale,1,1);glUniform2f(i.su.uvOffset,0,0);glUniform1f(i.su.alpha,1);glUniform1f(i.su.reflectionWeight,0);glUniform1i(i.su.noFog,0);glUniform4f(i.su.tint,1,1,1,1);
            Matrix basis=transform({},camera.angles,{1,1,1});Vec3 right{-basis[4],-basis[5],-basis[6]},up{basis[8],basis[9],basis[10]},forward{basis[0],basis[1],basis[2]};
            static const Model quad=[] {Model q;q.vertices={{{0,-.5f,-.5f},{1,0,0},{0,1},{}},{{0,.5f,-.5f},{1,0,0},{1,1},{}},{{0,.5f,.5f},{1,0,0},{1,0},{}},{{0,-.5f,.5f},{1,0,0},{0,0},{}}};q.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};return q;}();
            auto&gpu=i.mesh(quad);glBindVertexArray(gpu.vao);
            i.particleOrder.clear();i.particleData.clear();
            for(const auto&p:f.particles)if(p.size>0&&(!p.translucent||p.alphaRaw>0)&&p.scaleZ!=0)i.particleOrder.push_back(&p);
            std::sort(i.particleOrder.begin(),i.particleOrder.end(),[&](const Particle*a,const Particle*b){
                if(a->additive!=b->additive)return !a->additive;
                if(a->additive&&a->image!=b->image)return std::less<const Image*>{}(a->image,b->image);
                Vec3 da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));return dot(da,da)>dot(db,db);
            });
            for(const auto*particle:i.particleOrder){
                const auto&p=*particle;float c=std::cos(rad(p.angle)),s=std::sin(rad(p.angle));
                Vec3 r=add(mul(right,c),mul(up,s)),u=add(mul(up,c),mul(right,-s)),position=p.position;float length=p.size*std::abs(p.scaleZ);
                if((p.beam||p.streak)&&dot(p.velocity,p.velocity)>0){
                    Vec3 projected=add(p.velocity,mul(forward,-dot(p.velocity,forward)));
                    if(dot(projected,projected)>0.000001f){u=unit(projected);r=unit(cross(u,forward));}
                    float speed=std::sqrt(dot(p.velocity,p.velocity));if(p.streak)length+=speed;
                    if(p.beam){u=unit(p.velocity);Vec3 side=cross(u,add(camera.position,mul(p.position,-1)));r=dot(side,side)>0.000001f?unit(side):right;length+=speed;position=add(position,mul(p.velocity,-.5f));}
                }
                Vec3 n=unit(cross(r,u));Matrix model{n.x,n.y,n.z,0,r.x*p.size,r.y*p.size,r.z*p.size,0,u.x*length,u.y*length,u.z*length,0,position.x,position.y,position.z,1};
                Color color=p.color;color.a=particle_alpha(p.alphaRaw,p.translucent,p.additive,p.image!=nullptr);
                i.particleData.push_back({model,color,{p.flare?1.0f:0.0f,p.overlay?1.0f:0.0f,p.invertFlare?1.0f:0.0f}});
            }
            glBindBuffer(GL_ARRAY_BUFFER,i.particleVbo);
            if(i.particleData.size()>i.particleCapacity){i.particleCapacity=std::max<std::size_t>(i.particleData.size(),1024);glBufferData(GL_ARRAY_BUFFER,i.particleCapacity*sizeof(ParticleGPU),nullptr,GL_STREAM_DRAW);}
            if(!i.particleData.empty())glBufferSubData(GL_ARRAY_BUFFER,0,i.particleData.size()*sizeof(ParticleGPU),i.particleData.data());
            for(std::size_t first=0;first<i.particleOrder.size();){
                const auto&particle=*i.particleOrder[first];std::size_t end=first+1;
                while(end<i.particleOrder.size()&&i.particleOrder[end]->image==particle.image&&i.particleOrder[end]->additive==particle.additive)++end;
                for(int column=0;column<4;++column){int attribute=6+column;glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,4,GL_FLOAT,GL_FALSE,sizeof(ParticleGPU),reinterpret_cast<void*>(first*sizeof(ParticleGPU)+offsetof(ParticleGPU,model)+column*4*sizeof(float)));glVertexAttribDivisor(attribute,1);}
                glEnableVertexAttribArray(10);glVertexAttribPointer(10,4,GL_FLOAT,GL_FALSE,sizeof(ParticleGPU),reinterpret_cast<void*>(first*sizeof(ParticleGPU)+offsetof(ParticleGPU,color)));glVertexAttribDivisor(10,1);
                glEnableVertexAttribArray(11);glVertexAttribPointer(11,3,GL_FLOAT,GL_FALSE,sizeof(ParticleGPU),reinterpret_cast<void*>(first*sizeof(ParticleGPU)+offsetof(ParticleGPU,flags)));glVertexAttribDivisor(11,1);
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,i.texture(particle.image));glBindSampler(0,particle.image&&i.textures.at(particle.image)[0].dynamic?i.dynamicSceneSampler:i.sceneSampler);glUniform1i(i.su.textureAlpha,particle.image&&i.textures.at(particle.image)[0].alpha);
                glBlendFuncSeparate(GL_SRC_ALPHA,particle.additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr,end-first);first=end;
            }
        }
        if(!targetOnly&&&camera==primary&&(fx.ssao||fx.fog))i.screen_effects(f,camera,x,y,w,h);
        bool hasViewEntities=false;
        if(f.renderEntities&&!camera.noEntities)for(const auto&d:f.instances)if(!targetOnly&&d.viewSpace&&(d.layers&camera.layers)&&d.visible&&(!camera.excludeEntityId||d.objectId!=camera.excludeEntityId)){hasViewEntities=true;break;}
        if(hasViewEntities){
            glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);glUniform2f(i.su.fogRange,0,0);
            for(const auto&d:f.instances)if(d.viewSpace&&(d.layers&camera.layers)&&(!camera.excludeEntityId||d.objectId!=camera.excludeEntityId))i.draw_instance(d,f.skinBindings,&camera);
        }
        glDepthMask(GL_TRUE);
        if(translucent){
            glBindFramebuffer(GL_FRAMEBUFFER,destination);ui_state();glUniform1i(i.uiOverlay,0);glBindSampler(0,i.uiLinearSampler);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
            i.uiVertices.clear();i.quad(v,{v.x/m.width,1-v.y/m.height,v.width/m.width,-v.height/m.height},{camera.alpha,camera.alpha,camera.alpha,camera.alpha});
            i.flush_ui(i.alphaColor);
        }
    };
    // A6 panels/text and view entities are foreground regardless of signed layer.
    for(const auto&camera:f.cameras)if(!camera.foreground&&!camera.portalTarget)draw_camera(draw_camera,camera,false,nullptr);
    if(i.composite)i.composite_pass(m.drawableWidth,m.drawableHeight);
    std::size_t cameraIndex=0,uiIndex=0;
    std::size_t cursorIndex=f.ui.size();
    while(cameraIndex<f.cameras.size()||uiIndex<f.ui.size()){
        while(cameraIndex<f.cameras.size()&&(!f.cameras[cameraIndex].foreground||f.cameras[cameraIndex].portalTarget))++cameraIndex;
        if(cameraIndex<f.cameras.size()&&(uiIndex==f.ui.size()||f.cameras[cameraIndex].layer<=f.ui[uiIndex].layer))draw_camera(draw_camera,f.cameras[cameraIndex++],false,nullptr);
        else if(uiIndex<f.ui.size()){const auto&command=f.ui[uiIndex++];if(command.cursorOverlay){if(cursorIndex==f.ui.size())cursorIndex=uiIndex-1;}else draw_ui(command);}
    }
    if(f.video){
        ui_state();glUniform1i(i.uiOverlay,0);glBindSampler(0,i.uiLinearSampler);
        check_image(*f.video);if(!i.videoTexture)glGenTextures(1,&i.videoTexture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,i.videoTexture);
        if(i.videoWidth!=int(f.video->width)||i.videoHeight!=int(f.video->height)){
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,f.video->width,f.video->height,0,GL_RGBA,GL_UNSIGNED_BYTE,f.video->rgba.data());i.videoWidth=f.video->width;i.videoHeight=f.video->height;i.videoSerial=f.videoSerial;
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        }else if(i.videoSerial!=f.videoSerial){glTexSubImage2D(GL_TEXTURE_2D,0,0,0,f.video->width,f.video->height,GL_RGBA,GL_UNSIGNED_BYTE,f.video->rgba.data());i.videoSerial=f.videoSerial;}
        Rect v=f.videoRect;
        if(v.width<=0||v.height<=0){float scale=std::min(float(m.width)/f.video->width,float(m.height)/f.video->height);v={(m.width-f.video->width*scale)/2,(m.height-f.video->height*scale)/2,f.video->width*scale,f.video->height*scale};}
        i.uiVertices.clear();i.quad(v,{0,0,1,1},{1,1,1,1});glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);i.flush_ui(i.videoTexture);
    }
    // Source cursor is appended to the completed scene/UI, after every foreground view and video.
    for(;cursorIndex<f.ui.size();++cursorIndex)if(f.ui[cursorIndex].cursorOverlay)draw_ui(f.ui[cursorIndex]);
    i.post_state();
    glBindFramebuffer(GL_READ_FRAMEBUFFER,i.msaaFbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,i.resolveFbo);glBlitFramebuffer(0,0,m.drawableWidth,m.drawableHeight,0,0,m.drawableWidth,m.drawableHeight,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    if(f.adjustment.enabled&&(!i.gammaUploaded||i.gamma.gammaRamp!=f.adjustment.gammaRamp)){
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,i.gammaTexture);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,3,GL_RED,GL_UNSIGNED_SHORT,f.adjustment.gammaRamp.data());i.gamma=f.adjustment;i.gammaUploaded=true;
    }
    i.gamma.enabled=f.adjustment.enabled;
    i.post_draw();SDL_GL_SwapWindow(i.window);i.complete_frame();i.finalFrameValid=true;
}
void GLBackend::capture(std::string_view path,WindowMetrics m)const{
    auto&i=*impl_;
    if(m.drawableWidth<=0||m.drawableHeight<=0)throw std::runtime_error("Cannot capture minimized game window");
    if(!i.finalFrameValid||m.drawableWidth!=i.targetWidth||m.drawableHeight!=i.targetHeight)
        throw std::runtime_error("Cannot capture game window before a completed frame at its current drawable size");
    std::vector<std::uint8_t>pixels(size_t(m.drawableWidth)*m.drawableHeight*3);
    // Post-swap front/back contents are not portable. Reuse the actual final pass before reading.
    i.post_state();i.post_draw();glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,m.drawableWidth,m.drawableHeight,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream file(std::string(path),std::ios::binary);
    if(!file)throw std::runtime_error("Cannot create game framebuffer capture");
    file<<"P6\n"<<m.drawableWidth<<' '<<m.drawableHeight<<"\n255\n";
    for(int y=m.drawableHeight-1;y>=0;--y)
        file.write(reinterpret_cast<const char*>(pixels.data()+size_t(y)*m.drawableWidth*3),size_t(m.drawableWidth)*3);
    if(!file)throw std::runtime_error("Cannot write game framebuffer capture");
}
}
