#version 460
#extension GL_GOOGLE_include_directive : require
#ifndef K22_RAY_QUERY
#define K22_RAY_QUERY 0
#endif
#if K22_RAY_QUERY
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
#endif
layout(set=0,binding=5,std430) readonly buffer Lights { vec4 lightData[]; };
#if K22_RAY_QUERY
#include "ray_query.glsl"
#endif
#include "raster_uniform.glsl"
#define K22_LIGHT(i) lightData[i]
float pbrLightVisibility(int i);
#define K22_PBR_LIGHT_VISIBILITY(i) pbrLightVisibility(i)
#include "pbr.glsl"
layout(location=9) in vec3 worldTangent;
layout(set=0,binding=7) uniform sampler2D pbrNormal;
layout(set=0,binding=8) uniform sampler2D pbrOrm;
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=3) in vec2 lmuv;
layout(location=4) in vec3 vertexDynamicLight;
layout(location=5) in vec3 effectLight;
layout(location=6) in vec3 effectView;
layout(location=7) in vec3 vertexMaterialDiffuse;
layout(location=8) in vec3 vertexMaterialSpecular;
layout(location=10) in vec4 previousClip;
layout(location=11) flat in vec4 instanceTint;
layout(location=12) flat in vec4 instanceFlags;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D skin;
layout(set=0,binding=2) uniform sampler2D lightmap;
layout(set=0,binding=3) uniform sampler2D portalImage;
layout(set=0,binding=4) uniform sampler2D normalSkin;
// Wave-2: shadow maps (raster fallback) and denoised screen-space ray-traced signals of the primary view.
layout(set=0,binding=9) uniform sampler2DShadow sunShadowMap;
layout(set=0,binding=10) uniform samplerCubeShadow cubeShadowMap;
layout(set=0,binding=11) uniform sampler2D rtMask; // r dynamic-light visibility, g sun visibility, b ambient occlusion, a lightmap actor shadow / model baked-light visibility.
layout(set=0,binding=12) uniform sampler2D rtGi; // linear one-bounce irradiance (albedo applied here).
layout(set=0,binding=13) uniform sampler2D rtReflect; // rgb reflected radiance, a transmitted fraction.
layout(set=0,binding=14) uniform sampler2D gbufferDepth;
#define K22_FLIP_Y 1
#include "effects.glsl"
float sunMapVisibility(vec3 n){return k22SunShadow(sunShadowMap,u.dataSunShadow,worldPosition,n,u.dataEffects.z);}
float cubeMapVisibility(int i,vec3 n){return u.dataShadowLight.w>0.0&&i==int(u.dataEffects.w)?k22CubeShadow(cubeShadowMap,u.dataShadowLight.xyz,worldPosition,n,k22CubeShadowNear,u.dataShadowLight.w):1.0;}
// Shadow-map ratio for per-vertex native dynamic lighting, the same per-channel scaling as the ray-traced path.
vec3 shadowMappedNativeDynamic(vec3 p,vec3 n){
    vec3 result=vec3(0),unshadowed=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=lightData[i*5],b=lightData[i*5+1],d=lightData[i*5+2],e=lightData[i*5+3],c=lightData[i*5+4];
        if(b.a<0||c.z>0.5)continue;
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        vec3 contribution=b.rgb*abs(b.a)*attenuation*max(dot(n,direction),0);
        unshadowed+=contribution;result+=contribution*cubeMapVisibility(i,n);
    }
    result=clamp(result,0,1);unshadowed=clamp(unshadowed,0,1);
    return vertexDynamicLight*vec3(unshadowed.x>0?result.x/unshadowed.x:1,unshadowed.y>0?result.y/unshadowed.y:1,unshadowed.z>0?result.z/unshadowed.z:1);
}
// smooth_normals/tessellation (pipeline specialization): a smooth normal may face away from the camera near silhouettes
// although its facet faces it, so the G-buffer orients it by the camera-facing facet normal (computed in main).
layout(constant_id=0) const bool k22SmoothNormals=false;
vec3 facet=vec3(0);
// Primary-view fragments whose depth equals the G-buffer prepass sample use the screen-space RT signals.
bool onSignalPixel(){return (k22RtSignals&7)!=0&&abs(texelFetch(gbufferDepth,ivec2(gl_FragCoord.xy),0).r-gl_FragCoord.z)<=1e-5;}
// Other primary-view fragments trace inline rays, which are slow in fragment shaders (scratch traversal stack). Before
// the first one, a fragment is classified against the prepass depth at its pixel and the 3x3 neighbours (depthSlope: its
// screen-space depth plane), in this order:
// 1 occluded: prepass depth in front of the fragment's plane at all nine pixels. The opaque prepass surface overwrites
//   it, so no rays (1.5 ms of overdraw rays in the kriz infirmary at 1707x960). The neighbourhood keeps MSAA edge
//   fragments, which a front surface covers at the pixel centre but not at every sample, on their rays.
// 2 same surface as a neighbour's prepass pixel (MSAA edge fragments: prepass depth there continues the fragment's plane):
//   that pixel's denoised dynamic-light (rtMask.r) and sun (g) visibility.
// 3 decal: within 2 units of the prepass surface under its pixel (blood sprites on the floor, not in the prepass): that
//   surface's dynamic-light visibility; the sun keeps its ray (lightmapped surfaces carry no sun signal). A few
//   overlapping blood sprites traced 1.7 ms at 1707x960.
// 0 anything else (translucent surfaces, sky-backed pixels, no shadows=rt signal) traces.
vec2 depthSlope=vec2(0);int inlineClass=-1;vec2 surfaceVisibility=vec2(1);
bool onDecalSurface(float gd){
    if(gd>=1.0)return false;
    // The prepass point on this fragment's view ray: clip z/w (GL convention, scene.vert remaps it) equals 2 gd - 1.
    vec3 ray=worldPosition-cameraPosition;vec4 c0=viewProjection*vec4(cameraPosition,1),dc=viewProjection*vec4(ray,0);
    float g=2.0*gd-1.0,den=g*dc.w-dc.z;if(abs(den)<1e-12)return false;
    return abs((c0.z-g*c0.w)/den-1.0)*length(ray)<=2.0;
}
int classifyInline(){
    if(inlineClass>=0)return inlineClass;
    inlineClass=0;if((k22RtSignals&7)==0)return 0;
    ivec2 p=ivec2(gl_FragCoord.xy),last=textureSize(gbufferDepth,0)-1,match=ivec2(-1);float z=gl_FragCoord.z;
    float tolerance=1e-5+0.25*(abs(depthSlope.x)+abs(depthSlope.y));bool covered=true;
    for(int i=0;i<9;++i){ivec2 q=clamp(p+ivec2(i%3-1,i/3-1),ivec2(0),last);float d=texelFetch(gbufferDepth,q,0).r,plane=z+dot(depthSlope,vec2(q-p));
        covered=covered&&d+1e-5<plane;if(match.x<0&&abs(d-plane)<=tolerance)match=q;}
    if(covered)inlineClass=1;
    else if((k22RtSignals&1)==0)inlineClass=0;
    else if(match.x>=0){inlineClass=2;surfaceVisibility=texelFetch(rtMask,match,0).rg;}
    else if(onDecalSurface(texelFetch(gbufferDepth,p,0).r)){inlineClass=3;surfaceVisibility.x=texelFetch(rtMask,p,0).r;}
    return inlineClass;
}
vec4 gbufferData(vec2 coord){
    vec3 n=normalize(worldNormal);if(dot(n,cameraPosition-worldPosition)<0.0)n=-n;
    if(k22SmoothNormals){n=normalize(worldNormal);if(dot(n,facet)<0.0)n=-n;}
    vec2 o=n.xy/(abs(n.x)+abs(n.y)+abs(n.z));if(n.z<0.0)o=(1.0-abs(o.yx))*vec2(o.x>=0.0?1.0:-1.0,o.y>=0.0?1.0:-1.0);
    float flags=(hasLightmap?1.0:0.0)+(worldVertexLighting?2.0:0.0)+((unlit||effectKind!=0)?4.0:explicitMaterial?16.0:0.0)+(!portalEnabled&&reflectionWeight>0.0?8.0:0.0);
    return vec4(o,k22PbrOn&&k22RoughnessMap?texture(pbrOrm,coord).g:0.0,flags);
}
// Motion-vector pass (3): xy previous minus current UV, z current view depth (clip w), w previous view depth (<=0: behind
// the previous camera, no history). The cleared target (z=0) marks texels without a moving entity (render_policy::previous_uv).
vec4 motionData(){return vec4(vec2(previousClip.x,-previousClip.y)/previousClip.w*0.5+0.5-gl_FragCoord.xy*u.dataPass.zw,1.0/gl_FragCoord.w,previousClip.w);}
#if K22_RAY_QUERY
// A6 lights never occlude (D3D9 fixed-function lighting; static shadows live in the lightmaps): visibility rays run only
// with the optional shadows=rt (k22RtShadowMode), never on the default ray-traced path.
float directVisibility(vec3 direction,float maxDistance){
    float magnitude=length(direction);
    return k22RtShadowMode&&magnitude>0&&maxDistance>0&&classifyInline()!=1?rtVisibility(worldPosition,direction/magnitude,maxDistance):1;
}
float lightVisibility(vec3 delta,float distance){return directVisibility(delta,distance-rtLightClearance);}
// Inline ray toward native light i (its direction for directional lights) and the sun, traced once per fragment: the
// legacy light loops and the PBR specular loop ask for the same rays. Cached visibility is 8-bit, four per uint: a
// dynamically indexed float[32] cost more than it saved where few rays repeat (level5 scene pass +0.06 ms).
uint tracedLights=0u;uint lightRays[8]=uint[8](0u,0u,0u,0u,0u,0u,0u,0u);float sunRay=-1.0;
float lightRay(int i){
    if(k22RtShadowMode&&classifyInline()>=2)return surfaceVisibility.x;
    if(i<32&&(tracedLights&(1u<<i))!=0u)return float((lightRays[i>>2]>>((i&3)*8))&255u)/255.0;
    vec4 a=lightData[i*5],d=lightData[i*5+2];vec3 delta=a.xyz-worldPosition;
    float v=d.w==3?directVisibility(-d.xyz,rayMaxDistance()):lightVisibility(delta,length(delta));
    if(i<32){tracedLights|=1u<<i;lightRays[i>>2]|=uint(v*255.0+0.5)<<((i&3)*8);}
    return v;
}
float sunVisibilityRay(){
    if(k22RtShadowMode&&classifyInline()==2)return surfaceVisibility.y;
    if(sunRay<0.0)sunRay=directVisibility(sunDirection,rayMaxDistance());return sunRay;
}
vec3 visibleNativeDynamic(vec3 p,vec3 n){
    vec3 result=vec3(0),unshadowed=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=lightData[i*5],b=lightData[i*5+1],d=lightData[i*5+2],e=lightData[i*5+3],c=lightData[i*5+4];
        if(b.a<0)continue;
        if(c.z>0.5)continue;
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        float diffuse=max(dot(n,direction),0);
        vec3 contribution=b.rgb*abs(b.a)*attenuation*diffuse;
        unshadowed+=contribution;
        if(attenuation>0&&diffuse>0)result+=contribution*lightRay(i);
    }
    result=clamp(result,0,1);unshadowed=clamp(unshadowed,0,1);
    vec3 visibility=vec3(unshadowed.x>0?result.x/unshadowed.x:1,unshadowed.y>0?result.y/unshadowed.y:1,unshadowed.z>0?result.z/unshadowed.z:1);
    return vertexDynamicLight*visibility;
}
vec3 reflectedRadiance(vec3 rgb){
    if(!portalEnabled&&reflectionWeight>0&&classifyInline()!=1){
        vec3 direction=reflect(normalize(worldPosition-cameraPosition),normalize(worldNormal));
        rgb=mix(rgb,rtReflection(worldPosition,direction,rayMaxDistance(),rgb),reflectionWeight);
    }
    return rgb;
}
#endif
// Signal-pixel state of main(), shared with the PBR dynamic-light visibility hook.
bool signalShadows=false;vec4 mask=vec4(1);
// A dynamic light's PBR specular gets the visibility its diffuse term receives (signal mask, shadow cube, inline ray).
float pbrLightVisibility(int i){
    if(signalShadows)return mask.r;
    if(k22ShadowMaps)return cubeMapVisibility(i,normalize(worldNormal));
#if K22_RAY_QUERY
    if(lightData[i*5+1].a>=0)return lightRay(i);
#endif
    return 1.0;
}
vec3 enhancedBaked(vec3 p,vec3 n){
    if(hasLightmap)return vec3(0);
    vec3 result=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=lightData[i*5],b=lightData[i*5+1];
        if(b.a>=0)continue;
        vec3 delta=a.xyz-p;float distance=length(delta),attenuation=max(1-distance/max(a.w,.0001),0);
        result+=b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,delta/max(distance,.0001)),0);
    }
    return result;
}
// shadows=rt on ordinary (D3D-lit, e.g. the A6 default mat_model) materials: the light the signal mask occludes, taken off
// their Gouraud vertex lighting (scene.vert ordinaryMaterial): ambient x (1 - ambient occlusion, b), the sun x (1 - its
// visibility, g), other lights x (1 - the dynamic-light visibility, r). Unoccluded pixels keep the original lighting.
vec3 ordinaryOcclusion(vec3 p,vec3 n){
    vec3 lost=preparedAmbient.rgb*ambientLight*(1.0-mask.b);float sun=length(sunDirection);
    for(int i=0;i<lightCount;++i){
        vec4 a=lightData[i*5],b=lightData[i*5+1],d=lightData[i*5+2],e=lightData[i*5+3],c=lightData[i*5+4];
        if(b.a<0)continue;
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        float visible=c.z<=0.5?mask.r:d.w==3&&sun>0&&dot(direction,sunDirection)>0.999*sun?mask.g:1.0;
        lost+=preparedDiffuse*b.rgb*b.a*attenuation*(i==selectedLight?materialSunStrength:1)*max(dot(n,direction),0)*(1-visible);
    }
    return lost;
}
void main(){
    depthSlope=vec2(dFdx(gl_FragCoord.z),dFdy(gl_FragCoord.z)); // Uniform control flow: before any discard.
    if(u.dataDoor[0].w!=0.0){
        // X: the view line's crossing of the doorway's wall plane (render.hpp DoorPortal clip rule).
        vec3 center=u.dataDoor[0].xyz,n=u.dataDoor[1].xyz;float sC=dot(cameraPosition-center,n),sP=dot(worldPosition-center,n);
        vec3 x=cameraPosition+(worldPosition-cameraPosition)*(sC/(sC-sP))-center;
        bool inRect=abs(dot(x,u.dataDoor[2].xyz))<=u.dataDoor[1].w&&abs(dot(x,u.dataDoor[3].xyz))<=u.dataDoor[2].w;
        if(u.dataDoor[0].w==1.0?sP<1.0&&sC-sP>0.0&&inRect:!(sP<-2.0&&inRect))discard;
    }
    if(skyMask){color=vec4(1);return;}
    if(k22SmoothNormals&&k22Pass==2){facet=cross(dFdx(worldPosition),dFdy(worldPosition));if(dot(facet,cameraPosition-worldPosition)<0.0)facet=-facet;}
    vec2 coord=uv*uvScale+uvOffset;vec4 texel;
    // Sprite frame f sits at cell (f mod cols, f div cols); cols=1/uvScale.x (one row for an ordinary strip, engine.cpp load_entity_asset).
    if(spriteFrames>1){float cols=floor(1.0/uvScale.x+0.5),f0=float(frame0),f1=float(frame1);texel=mix(texture(skin,coord+vec2(mod(f0,cols)/cols,floor(f0/cols)*uvScale.y)),texture(skin,coord+vec2(mod(f1,cols)/cols,floor(f1/cols)*uvScale.y)),interpolation);}else texel=texture(skin,coord);
    // Shadow-caster (depth only), G-buffer and motion-vector prepass draws keep only the native alpha rejection below.
    if(effectKind!=0&&k22Pass!=0){color=k22Pass==2?gbufferData(coord):k22Pass==3?motionData():vec4(0);return;}
    if(effectKind!=0){
        vec3 bump=2*texture(normalSkin,uv*normalUvScale+uvOffset).rgb-1;
        vec3 reflection=normalize(2*dot(bump,effectLight)*bump-effectLight);
        float specular=skillMain.z*pow(clamp(dot(reflection,normalize(effectView)),0,1),skillMain.w);
        vec4 sunlight=effectKind==1?vec4(.9,.9,1,.9):skillExtra;
        color=(skillMain.x*preparedAmbient+skillMain.y*clamp(dot(effectLight,bump),0,1)*sunlight+vec4(specular))*texel;
        return;
    }
    bool drawOverlay=particleInstanced?instanceFlags.y!=0:overlay,drawFlare=particleInstanced?instanceFlags.x!=0:flare,drawInvertFlare=particleInstanced?instanceFlags.z!=0:invertFlare;
    if(drawOverlay&&(max(max(texel.r,texel.g),texel.b)<0.001||texel.a<0.5))discard;
    if(drawFlare&&!textureAlpha){float brightness=max(max(texel.r,texel.g),texel.b);texel.a*=drawInvertFlare?1-brightness:brightness;}
    vec4 t=texel*(particleInstanced?instanceTint:tint);t.a*=alpha;if(t.a<0.01)discard;
    if(k22Pass!=0){color=k22Pass==2?gbufferData(coord):k22Pass==3?motionData():vec4(0);return;}
#if K22_RAY_QUERY
    bool signals=onSignalPixel();signalShadows=signals&&(k22RtSignals&1)!=0;
    if(signalShadows)mask=texelFetch(rtMask,ivec2(gl_FragCoord.xy),0);
#endif
    vec3 illumination=vec3(1);
    if(!unlit&&!explicitMaterial){
        illumination=hasLightmap?2*clamp(texture(lightmap,lmuv).rgb+vec3(surfaceAmbient),0,1):vec3(materialAmbient);
#if K22_RAY_QUERY
        if(signalShadows)illumination*=mask.b;
#endif
        vec3 n=normalize(worldNormal);
        if(!hasLightmap){
#if K22_RAY_QUERY
            if(signalShadows)illumination+=vec3(max(dot(n,sunDirection),0))*mask.g;
            else if(k22ShadowMaps)illumination+=vec3(max(dot(n,sunDirection),0))*sunMapVisibility(n);
            else{float facing=max(dot(n,sunDirection),0);illumination+=vec3(facing)*(facing>0?sunVisibilityRay():1.0);}
#else
            if(k22ShadowMaps)illumination+=vec3(max(dot(n,sunDirection),0))*sunMapVisibility(n);
            else illumination+=vec3(max(dot(n,sunDirection),0));
#endif
        }
        if(worldVertexLighting){
#if K22_RAY_QUERY
            if(signalShadows)illumination+=vertexDynamicLight*mask.r+enhancedBaked(worldPosition,n)*mask.a;
            else if(k22ShadowMaps)illumination+=shadowMappedNativeDynamic(worldPosition,n)+enhancedBaked(worldPosition,n);
            else illumination+=(k22RtShadowMode?visibleNativeDynamic(worldPosition,n):vertexDynamicLight)+enhancedBaked(worldPosition,n);
#else
            if(k22ShadowMaps)illumination+=shadowMappedNativeDynamic(worldPosition,n)+enhancedBaked(worldPosition,n);
            else illumination+=vertexDynamicLight+enhancedBaked(worldPosition,n);
#endif
        }else for(int i=0;i<lightCount;++i){
            vec4 a=lightData[i*5],b=lightData[i*5+1];if((lightData[i*5+4].z>0.5&&b.a>=0&&lightData[i*5+2].w==3)||(hasLightmap&&b.a<0))continue;
            vec3 d=a.xyz-worldPosition;float distance=length(d);float attenuation=max(1-distance/max(a.w,0.0001),0);
            vec3 contribution=b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,d/max(distance,0.0001)),0);
#if K22_RAY_QUERY
            if(b.a>=0&&attenuation>0)contribution*=signalShadows?mask.r:k22ShadowMaps?cubeMapVisibility(i,n):!any(greaterThan(contribution,vec3(0)))?1.0:lightData[i*5+2].w==3?lightVisibility(d,distance):lightRay(i);
            else if(signalShadows)contribution*=mask.a; // Baked lights on models: rt_signals.comp bakedVisibility.
#else
            if(k22ShadowMaps&&b.a>=0&&attenuation>0)contribution*=cubeMapVisibility(i,n);
#endif
            illumination+=contribution;
        }
#if K22_RAY_QUERY
        if(signals&&(k22RtSignals&2)!=0)illumination=k22Gamma(k22Linear(illumination)+texelFetch(rtGi,ivec2(gl_FragCoord.xy),0).rgb);
#endif
    }
    vec3 materialDiffuse=vertexMaterialDiffuse;
#if K22_RAY_QUERY
    if(signalShadows&&explicitMaterial&&!unlit)materialDiffuse=max(materialDiffuse-ordinaryOcclusion(worldPosition,normalize(worldNormal)),0);
#endif
    vec3 rgb=explicitMaterial?clamp(clamp(t.rgb*materialDiffuse*2,0,1)+vertexMaterialSpecular,0,1):t.rgb*illumination;
    if(k22PbrOn&&!unlit&&!explicitMaterial){
#if K22_RAY_QUERY
        float sunVisibility=hasLightmap?1.0:signalShadows?mask.g:k22ShadowMaps?sunMapVisibility(normalize(worldNormal)):sunVisibilityRay();
#else
        float sunVisibility=k22ShadowMaps&&!hasLightmap?sunMapVisibility(normalize(worldNormal)):1.0;
#endif
        rgb=k22Pbr(t.rgb,illumination,worldPosition,normalize(worldNormal),worldTangent,normalOrientation,texture(pbrNormal,coord).rgb,texture(pbrOrm,coord),k22RoughnessMap,k22LegacyRoughness,cameraPosition,sunDirection,sunVisibility,!hasLightmap);
    }
#if K22_RAY_QUERY
    // Actor shadows (rt_signals.comp actorShadow) darken the lit surface like the planar shadow they replace (alpha over it).
    if(signalShadows&&hasLightmap)rgb*=mask.a;
#endif
    if(!noFog&&fogRange.y>fogRange.x){float f=clamp((length(worldPosition-cameraPosition)-fogRange.x)/(fogRange.y-fogRange.x),0,1);rgb=mix(rgb,fogColor,f);}
    if(portalEnabled&&reflectionWeight>0&&abs(dot(portalPlane,vec4(worldPosition,1)))<0.1){
        vec4 reflected=portalProjection*vec4(worldPosition,1);
        if(reflected.w>0){vec2 coord=vec2(reflected.x/reflected.w*.5+.5,.5-reflected.y/reflected.w*.5)*portalViewport.zw+portalViewport.xy;rgb=mix(rgb,texture(portalImage,coord).rgb,reflectionWeight);}
    }
#if K22_RAY_QUERY
    if(signals&&(k22RtSignals&4)!=0){if(!portalEnabled&&reflectionWeight>0){vec4 r=texelFetch(rtReflect,ivec2(gl_FragCoord.xy),0);rgb=mix(rgb,r.rgb+r.a*rgb,reflectionWeight);}}
    else rgb=reflectedRadiance(rgb);
#endif
    if(k22PbrOn&&k22ToneInShader)rgb=k22ToneMap(rgb);
    color=vec4(rgb,t.a);
}
