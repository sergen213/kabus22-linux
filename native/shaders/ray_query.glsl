layout(set=1,binding=0) uniform accelerationStructureEXT rtAcceleration;
struct RtInstance { uint64_t vertices; uint64_t indices; uint materialBase; uint padding; vec4 clipPlane; };
struct RtMaterial {
    vec4 uvTransform; vec4 tint; vec4 factors;
    uvec4 metadata; uvec4 frames;
    vec4 normalUvTransform; vec4 ambient; vec4 skillMain; vec4 skillExtra;
    vec4 sunDirection; vec4 cameraPosition;
    vec4 preparedAmbient; vec4 preparedDiffuse; vec4 preparedSpecular; vec4 preparedEmissive;
    vec4 ambientLight; vec4 viewer; ivec4 lightState;
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
void rtOrdinaryVertex(RtMaterial m,vec3 p,vec3 n,out vec3 diffuse,out vec3 specular){
    diffuse=m.preparedEmissive.rgb+m.preparedAmbient.rgb*m.ambientLight.rgb;
    specular=vec3(0);
    for(int i=0;i<m.lightState.x;++i){
        vec4 a=lightData[i*5],b=lightData[i*5+1],d=lightData[i*5+2],e=lightData[i*5+3],c=lightData[i*5+4];
        if(b.a<0)continue;
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        float strength=i==m.lightState.y?m.ambientLight.w:1;
        vec3 light=b.rgb*abs(b.a)*attenuation*strength;
        float incidence=max(dot(n,direction),0);
        diffuse+=m.preparedDiffuse.rgb*light*incidence;
        if(any(notEqual(m.preparedSpecular.rgb,vec3(0)))){
            vec3 halfway=rtSafeNormal(direction+m.viewer.xyz);
            float shine=dot(halfway,halfway)>0?(m.preparedSpecular.w==0?1:pow(max(dot(n,halfway),0),m.preparedSpecular.w)):0;
            specular+=m.preparedSpecular.rgb*light*shine;
        }
    }
    diffuse=clamp(diffuse,0,1);specular=clamp(specular,0,1);
}
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
    if(m.metadata.w>1u){float cols=floor(1.0/m.uvTransform.x+0.5),f0=float(m.frames.x),f1=float(m.frames.y);texel=mix(textureLod(rtTextures[nonuniformEXT(m.metadata.y)],coord+vec2(mod(f0,cols)/cols,floor(f0/cols)*m.uvTransform.y),0),textureLod(rtTextures[nonuniformEXT(m.metadata.y)],coord+vec2(mod(f1,cols)/cols,floor(f1/cols)*m.uvTransform.y),0),m.factors.y);} // sprite grid cells, see scene.frag
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
    if((m.metadata.x&2048u)!=0u){
        vec3 diffuse=vec3(0),specular=vec3(0);uint ids[3]=uint[3](a,b,c);
        mat3 normalTransform=mat3(transform);
        for(int j=0;j<3;++j){float squared=dot(normalTransform[j],normalTransform[j]);normalTransform[j]=squared>0?normalTransform[j]/squared:vec3(0);}
        float orientation=(m.metadata.x&32u)!=0u?-1:1;
        for(uint j=0u;j<3u;++j){
            vec3 vertexDiffuse,vertexSpecular;
            rtOrdinaryVertex(m,transform*vec4(rtPosition(vertices,ids[j]),1),rtSafeNormal(normalTransform*rtNormal(vertices,ids[j]))*orientation,vertexDiffuse,vertexSpecular);
            diffuse+=vertexDiffuse*w[j];specular+=vertexSpecular*w[j];
        }
        // D3D9 MODULATE2X applies only to textured diffuse; vertex specular is added afterward.
        result.rgb=clamp(clamp(result.rgb*diffuse*2,0,1)+specular,0,1);
    }
    if(result.a<.01)return vec4(0);
    if((m.metadata.x&1024u)==0u)result.a=1.0;
    return result;
}
bool rtAdditive(uint instanceIndex,uint geometryIndex){return (rtMaterials[rtInstances[instanceIndex].materialBase+geometryIndex].metadata.x&(512u|1024u))==(512u|1024u);}
// The committed operand of SPIR-V query getters must remain a compile-time constant.
#define rtHitColor(q,c) rtMaterialHit(rayQueryGetIntersectionInstanceCustomIndexEXT(q,c),rayQueryGetIntersectionGeometryIndexEXT(q,c),rayQueryGetIntersectionPrimitiveIndexEXT(q,c),rayQueryGetIntersectionFrontFaceEXT(q,c),rayQueryGetIntersectionBarycentricsEXT(q,c),rayQueryGetIntersectionObjectToWorldEXT(q,c))
// Occluders are judged as seen from the light, like the back-face-culled shadow-map caster pass: the ray runs from the
// receiver toward the light, so a one-sided face blocks only when its front faces the light (the ray meets its back).
// One-sided BSP faces that are invisible from the light side therefore pass light, and a light inside its own closed
// emitter model is not self-occluded. Native additive (glow) surfaces add light and never block it.
// Lights often sit inside lamp/pole/emitter fixtures: as with the shadow cube near plane (render_policy::cubeShadowNear),
// geometry closer than this to a point or spot light does not occlude it.
const float rtLightClearance=16.0;
float rtVisibility(vec3 origin,vec3 direction,float maxDistance){
    if(maxDistance<=.001||dot(direction,direction)==0.0)return 1.0;
    rayQueryEXT q;rayQueryInitializeEXT(q,rtAcceleration,gl_RayFlagsNoneEXT,255,origin,.001,normalize(direction),maxDistance);
    float visibility=1.0;
    while(rayQueryProceedEXT(q))if(rayQueryGetIntersectionTypeEXT(q,false)==gl_RayQueryCandidateIntersectionTriangleEXT&&!rtAdditive(rayQueryGetIntersectionInstanceCustomIndexEXT(q,false),rayQueryGetIntersectionGeometryIndexEXT(q,false))){vec4 color=rtMaterialHit(rayQueryGetIntersectionInstanceCustomIndexEXT(q,false),rayQueryGetIntersectionGeometryIndexEXT(q,false),rayQueryGetIntersectionPrimitiveIndexEXT(q,false),!rayQueryGetIntersectionFrontFaceEXT(q,false),rayQueryGetIntersectionBarycentricsEXT(q,false),rayQueryGetIntersectionObjectToWorldEXT(q,false));if(color.a>0.0){visibility*=1.0-clamp(color.a,0.0,1.0);if(visibility<.01){rayQueryTerminateEXT(q);return 0.0;}}}
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
