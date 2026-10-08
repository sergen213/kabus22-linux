#version 460
#extension GL_GOOGLE_include_directive : require
#ifndef K22_RAY_QUERY
#define K22_RAY_QUERY 0
#endif
#if K22_RAY_QUERY
#include "ray_query.glsl"
#endif
#include "raster_uniform.glsl"
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=3) in vec2 lmuv;
layout(location=4) in vec3 vertexDynamicLight;
layout(location=5) in vec3 effectLight;
layout(location=6) in vec3 effectView;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D skin;
layout(set=0,binding=2) uniform sampler2D lightmap;
layout(set=0,binding=3) uniform sampler2D portalImage;
layout(set=0,binding=4) uniform sampler2D normalSkin;
layout(set=0,binding=5,std430) readonly buffer Lights { vec4 lightData[]; };
#if K22_RAY_QUERY
float directVisibility(vec3 direction,float maxDistance){
    float magnitude=length(direction);
    return magnitude>0&&maxDistance>0?rtVisibility(worldPosition,direction/magnitude,maxDistance):1;
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
        if(attenuation>0&&diffuse>0)result+=contribution*directVisibility(direction,d.w==3?rayMaxDistance():distance);
    }
    result=clamp(result,0,1);unshadowed=clamp(unshadowed,0,1);
    vec3 visibility=vec3(unshadowed.x>0?result.x/unshadowed.x:1,unshadowed.y>0?result.y/unshadowed.y:1,unshadowed.z>0?result.z/unshadowed.z:1);
    return vertexDynamicLight*visibility;
}
vec3 reflectedRadiance(vec3 rgb){
    if(!portalEnabled&&reflectionWeight>0){
        vec3 direction=reflect(normalize(worldPosition-cameraPosition),normalize(worldNormal));
        rgb=mix(rgb,rtReflection(worldPosition,direction,rayMaxDistance(),rgb),reflectionWeight);
    }
    return rgb;
}
#endif
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
void main(){
    if(skyMask){color=vec4(1);return;}
    vec2 coord=uv*uvScale+uvOffset;vec4 texel;
    if(spriteFrames>1)texel=mix(texture(skin,coord+vec2(float(frame0)/spriteFrames,0)),texture(skin,coord+vec2(float(frame1)/spriteFrames,0)),interpolation);else texel=texture(skin,coord);
    if(effectKind!=0){
        vec3 bump=2*texture(normalSkin,uv*normalUvScale+uvOffset).rgb-1;
        vec3 reflection=normalize(2*dot(bump,effectLight)*bump-effectLight);
        float specular=skillMain.z*pow(clamp(dot(reflection,normalize(effectView)),0,1),skillMain.w);
        vec4 sunlight=effectKind==1?vec4(.9,.9,1,.9):skillExtra;
        color=(skillMain.x*preparedAmbient+skillMain.y*clamp(dot(effectLight,bump),0,1)*sunlight+vec4(specular))*texel;
#if K22_RAY_QUERY
        float visibility=directVisibility(-(sunDirection+(effectKind==1?skillExtra.xyz:vec3(0))),rayMaxDistance());
        color.rgb=(skillMain.x*preparedAmbient.rgb+(skillMain.y*clamp(dot(effectLight,bump),0,1)*sunlight.rgb+vec3(specular))*visibility)*texel.rgb;
        color.rgb=reflectedRadiance(color.rgb);
#endif
        return;
    }
    if(overlay&&(max(max(texel.r,texel.g),texel.b)<0.001||texel.a<0.5))discard;
    if(flare&&!textureAlpha){float brightness=max(max(texel.r,texel.g),texel.b);texel.a*=invertFlare?1-brightness:brightness;}
    vec4 t=texel*tint;t.a*=alpha;if(t.a<0.01)discard;
    vec3 illumination=vec3(1);
    if(!unlit&&!explicitMaterial){
        illumination=hasLightmap?2*clamp(texture(lightmap,lmuv).rgb+vec3(surfaceAmbient),0,1):vec3(materialAmbient);
        vec3 n=normalize(worldNormal);
        if(!hasLightmap){
#if K22_RAY_QUERY
            illumination+=vec3(max(dot(n,sunDirection),0))*directVisibility(sunDirection,rayMaxDistance());
#else
            illumination+=vec3(max(dot(n,sunDirection),0));
#endif
        }
        if(worldVertexLighting){
#if K22_RAY_QUERY
            illumination+=visibleNativeDynamic(worldPosition,n)+enhancedBaked(worldPosition,n);
#else
            illumination+=vertexDynamicLight+enhancedBaked(worldPosition,n);
#endif
        }else for(int i=0;i<lightCount;++i){
            vec4 a=lightData[i*5],b=lightData[i*5+1];if(hasLightmap&&b.a<0)continue;
            vec3 d=a.xyz-worldPosition;float distance=length(d);float attenuation=max(1-distance/max(a.w,0.0001),0);
            vec3 contribution=b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,d/max(distance,0.0001)),0);
#if K22_RAY_QUERY
            if(b.a>=0&&attenuation>0)contribution*=directVisibility(d,distance);
#endif
            illumination+=contribution;
        }
    }
    if(explicitMaterial){
        float sunIntensity=length(sunDirection);
        vec3 n=normalize(worldNormal),light=sunIntensity>0?sunDirection/sunIntensity:vec3(0),view=normalize(cameraPosition-worldPosition);
        float diffuse=max(dot(n,light),0),specular=sunIntensity>0&&any(notEqual(preparedSpecular,vec3(0)))?pow(max(dot(reflect(-light,n),view),0),materialPower):0;
        illumination=preparedEmissive+preparedAmbient.rgb*ambientLight;
        vec3 sunlight=(preparedDiffuse*diffuse+preparedSpecular*specular)*(materialSunStrength*sunIntensity);
#if K22_RAY_QUERY
        sunlight*=directVisibility(light,rayMaxDistance());
#endif
        illumination+=sunlight;
        if(worldVertexLighting){
#if K22_RAY_QUERY
            illumination+=preparedDiffuse*(visibleNativeDynamic(worldPosition,n)+enhancedBaked(worldPosition,n));
#else
            illumination+=preparedDiffuse*(vertexDynamicLight+enhancedBaked(worldPosition,n));
#endif
        }else for(int i=0;i<lightCount;++i){
            vec4 a=lightData[i*5],b=lightData[i*5+1];if(hasLightmap&&b.a<0)continue;
            vec3 d=a.xyz-worldPosition;float distance=length(d),attenuation=max(1-distance/max(a.w,.0001),0);
            vec3 contribution=preparedDiffuse*b.rgb*abs(b.a)*attenuation*attenuation*max(dot(n,d/max(distance,.0001)),0);
#if K22_RAY_QUERY
            if(b.a>=0&&attenuation>0)contribution*=directVisibility(d,distance);
#endif
            illumination+=contribution;
        }
    }
    vec3 rgb=t.rgb*illumination*(explicitMaterial?2:1);
    if(!noFog&&fogRange.y>fogRange.x){float f=clamp((length(worldPosition-cameraPosition)-fogRange.x)/(fogRange.y-fogRange.x),0,1);rgb=mix(rgb,fogColor,f);}
    if(portalEnabled&&reflectionWeight>0&&abs(dot(portalPlane,vec4(worldPosition,1)))<0.1){
        vec4 reflected=portalProjection*vec4(worldPosition,1);
        if(reflected.w>0){vec2 coord=vec2(reflected.x/reflected.w*.5+.5,.5-reflected.y/reflected.w*.5)*portalViewport.zw+portalViewport.xy;rgb=mix(rgb,texture(portalImage,coord).rgb,reflectionWeight);}
    }
#if K22_RAY_QUERY
    rgb=reflectedRadiance(rgb);
#endif
    color=vec4(rgb,t.a);
}
