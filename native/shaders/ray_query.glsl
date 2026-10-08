#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
layout(set=1,binding=0) uniform accelerationStructureEXT rtAcceleration;
struct RtInstance { uint64_t vertices; uint64_t indices; uint materialBase; uint padding; vec4 clipPlane; };
struct RtMaterial {
    vec4 uvTransform; vec4 tint; vec4 factors;
    uvec4 metadata; uvec4 frames;
    vec4 normalUvTransform; vec4 ambient; vec4 skillMain; vec4 skillExtra;
    vec4 sunDirection; vec4 cameraPosition;
};
layout(set=1,binding=1,std430) readonly buffer RtInstances { RtInstance rtInstances[]; };
layout(set=1,binding=2,std430) readonly buffer RtMaterials { RtMaterial rtMaterials[]; };
layout(set=1,binding=3) uniform sampler2D rtTextures[1024];
layout(buffer_reference,std430,buffer_reference_align=4) readonly buffer RtVertices { float words[]; };
layout(buffer_reference,std430,buffer_reference_align=4) readonly buffer RtIndices { uint words[]; };
vec3 rtPosition(RtVertices v,uint i){uint a=i*13u;return vec3(v.words[a],v.words[a+1u],v.words[a+2u]);}
vec3 rtNormal(RtVertices v,uint i){uint a=i*13u+3u;return vec3(v.words[a],v.words[a+1u],v.words[a+2u]);}
vec3 rtTangent(RtVertices v,uint i){uint a=i*13u+10u;return vec3(v.words[a],v.words[a+1u],v.words[a+2u]);}
vec2 rtUv(RtVertices v,uint i){uint a=i*13u+6u;return vec2(v.words[a],v.words[a+1u]);}
vec3 rtSafeNormal(vec3 v){float s=dot(v,v);return s>0.0?v*inversesqrt(s):vec3(0);}
vec4 rtMaterialHit(uint instanceIndex,uint geometryIndex,uint triangle,bool front,vec2 bary,mat4x3 transform){
    RtInstance instance=rtInstances[instanceIndex];
    RtMaterial m=rtMaterials[instance.materialBase+geometryIndex];
    if((m.metadata.x&16u)==0u && front==((m.metadata.x&32u)!=0u))return vec4(0);
    RtIndices indices=RtIndices(instance.indices);RtVertices vertices=RtVertices(instance.vertices);
    uint a=indices.words[m.frames.z+triangle*3u],b=indices.words[m.frames.z+triangle*3u+1u],c=indices.words[m.frames.z+triangle*3u+2u];
    vec3 w=vec3(1.0-bary.x-bary.y,bary);
    vec3 worldHit=transform*vec4(rtPosition(vertices,a)*w.x+rtPosition(vertices,b)*w.y+rtPosition(vertices,c)*w.z,1);
    if(dot(instance.clipPlane,vec4(worldHit,1))<0.0)return vec4(0);
    vec2 uv=rtUv(vertices,a)*w.x+rtUv(vertices,b)*w.y+rtUv(vertices,c)*w.z;
    vec2 coord=uv*m.uvTransform.xy+m.uvTransform.zw;
    vec4 texel;
    if(m.metadata.w>1u){float strip=float(m.metadata.w);texel=mix(textureLod(rtTextures[nonuniformEXT(m.metadata.y)],coord+vec2(float(m.frames.x)/strip,0),0),textureLod(rtTextures[nonuniformEXT(m.metadata.y)],coord+vec2(float(m.frames.y)/strip,0),0),m.factors.y);}
    else texel=textureLod(rtTextures[nonuniformEXT(m.metadata.y)],coord,0);
    if((m.metadata.x&64u)!=0u){
        vec3 light=vec3(0),view=vec3(0);uint ids[3]=uint[3](a,b,c);
        for(uint j=0u;j<3u;++j){uint id=ids[j];vec3 n=rtNormal(vertices,id),t=rtTangent(vertices,id);mat3 basis=transpose(mat3(mat3(transform)*t,mat3(transform)*cross(t,n),mat3(transform)*n));vec3 p=transform*vec4(rtPosition(vertices,id),1);light+=rtSafeNormal(basis*-(m.sunDirection.xyz+((m.metadata.x&128u)==0u?m.skillExtra.xyz:vec3(0))))*w[j];view+=rtSafeNormal(basis*(m.cameraPosition.xyz-p))*w[j];}
        vec3 bump=2.0*textureLod(rtTextures[nonuniformEXT(m.metadata.z)],uv*m.normalUvTransform.xy+m.normalUvTransform.zw,0).rgb-1.0;
        vec3 reflected=rtSafeNormal(2.0*dot(bump,light)*bump-light);
        float specular=m.skillMain.z*pow(clamp(dot(reflected,rtSafeNormal(view)),0.0,1.0),m.skillMain.w);
        vec4 sunlight=(m.metadata.x&128u)==0u?vec4(.9,.9,1,.9):m.skillExtra;
        vec4 result=(m.skillMain.x*m.ambient+m.skillMain.y*clamp(dot(light,bump),0.0,1.0)*sunlight+vec4(specular))*texel;
        // FX has no fragment discard; opaque native passes ignore its output alpha.
        if((m.metadata.x&1024u)==0u)result.a=1.0;
        return result;
    }
    if((m.metadata.x&1u)!=0u&&(max(max(texel.r,texel.g),texel.b)<.001||texel.a<.5))return vec4(0);
    if((m.metadata.x&2u)!=0u&&(m.metadata.x&8u)==0u){float brightness=max(max(texel.r,texel.g),texel.b);texel.a*=((m.metadata.x&4u)!=0u)?1.0-brightness:brightness;}
    vec4 result=texel*m.tint;result.a*=m.factors.x;
    if(result.a<.01)return vec4(0);
    if((m.metadata.x&1024u)==0u)result.a=1.0;
    return result;
}
// The committed operand of SPIR-V query getters must remain a compile-time constant.
#define rtHitColor(q,c) rtMaterialHit(rayQueryGetIntersectionInstanceCustomIndexEXT(q,c),rayQueryGetIntersectionGeometryIndexEXT(q,c),rayQueryGetIntersectionPrimitiveIndexEXT(q,c),rayQueryGetIntersectionFrontFaceEXT(q,c),rayQueryGetIntersectionBarycentricsEXT(q,c),rayQueryGetIntersectionObjectToWorldEXT(q,c))
float rtVisibility(vec3 origin,vec3 direction,float maxDistance){
    if(maxDistance<=.001||dot(direction,direction)==0.0)return 1.0;
    rayQueryEXT q;rayQueryInitializeEXT(q,rtAcceleration,gl_RayFlagsNoneEXT,255,origin,.001,normalize(direction),maxDistance);
    float visibility=1.0;
    while(rayQueryProceedEXT(q))if(rayQueryGetIntersectionTypeEXT(q,false)==gl_RayQueryCandidateIntersectionTriangleEXT){vec4 color=rtHitColor(q,false);if(color.a>0.0){visibility*=1.0-clamp(color.a,0.0,1.0);if(visibility<.01){rayQueryTerminateEXT(q);return 0.0;}}}
    return visibility;
}
vec3 rtReflection(vec3 origin,vec3 direction,float maxDistance,vec3 fallback){
    if(maxDistance<=.001||dot(direction,direction)==0.0)return fallback;
    vec3 radiance=vec3(0);float visibility=1.0,minDistance=.001;
    direction=normalize(direction);
    // A straight ray intersects each display triangle at most once: no arbitrary layer cap.
    for(uint layer=0u;layer<rtInstances[0].padding&&minDistance<=maxDistance;++layer){
        rayQueryEXT q;rayQueryInitializeEXT(q,rtAcceleration,gl_RayFlagsNoneEXT,255,origin,minDistance,direction,maxDistance);
        while(rayQueryProceedEXT(q))if(rayQueryGetIntersectionTypeEXT(q,false)==gl_RayQueryCandidateIntersectionTriangleEXT){vec4 color=rtHitColor(q,false);if(color.a>0.0)rayQueryConfirmIntersectionEXT(q);}
        if(rayQueryGetIntersectionTypeEXT(q,true)==gl_RayQueryCommittedIntersectionNoneEXT)break;
        vec4 color=rtHitColor(q,true);float alpha=clamp(color.a,0.0,1.0);
        uint flags=rtMaterials[rtInstances[rayQueryGetIntersectionInstanceCustomIndexEXT(q,true)].materialBase+rayQueryGetIntersectionGeometryIndexEXT(q,true)].metadata.x;
        radiance+=visibility*alpha*color.rgb;
        // Native additive blending adds radiance without obscuring the geometry behind it.
        if((flags&(512u|1024u))!=(512u|1024u))visibility*=1.0-alpha;
        if(visibility<=0.0)return radiance;
        // Advance one representable distance, not a scene-unit epsilon that can skip thin layers.
        minDistance=uintBitsToFloat(floatBitsToUint(rayQueryGetIntersectionTEXT(q,true))+1u);
    }
    return radiance+visibility*fallback;
}
