// Shared by the OpenGL 3.3 scene shader (embedded at configure time) and the Vulkan scene shaders.
// Includer defines K22_LIGHT(i) (vec4 light record word) and int lightCount before including; optionally
// K22_PBR_LIGHT_VISIBILITY(i), the shadow visibility of dynamic light i applied to its specular term.
// Scene colors stay in the game's gamma-2.2 space; lighting math runs on linearized values.
#ifndef K22_PBR_GLSL
#define K22_PBR_GLSL
const float k22Knee=0.6;
vec3 k22Linear(vec3 c){return pow(max(c,vec3(0)),vec3(2.2));}
vec3 k22Gamma(vec3 c){return pow(max(c,vec3(0)),vec3(1.0/2.2));}
// Identity below the linear knee, Reinhard shoulder above (render_policy::tone_map).
vec3 k22ToneMap(vec3 g){vec3 x=k22Linear(g),t=max(x-k22Knee,vec3(0))/(1.0-k22Knee);return k22Gamma(min(x,vec3(k22Knee))+(1.0-k22Knee)*t/(1.0+t));}
vec3 k22InverseToneMap(vec3 g){vec3 y=k22Linear(clamp(g,vec3(0),vec3(1.0-0.25/255.0))),s=max(y-k22Knee,vec3(0))/(1.0-k22Knee);return k22Gamma(min(y,vec3(k22Knee))+(1.0-k22Knee)*s/(1.0-s));}
float k22Ggx(float NdotH,float a){float a2=a*a,d=NdotH*NdotH*(a2-1.0)+1.0;return a2/(3.14159265*d*d);}
float k22Smith(float NdotV,float NdotL,float r){float k=(r+1.0)*(r+1.0)/8.0;return NdotV/(NdotV*(1.0-k)+k)*NdotL/(NdotL*(1.0-k)+k);}
// Karis' analytic split-sum environment BRDF.
vec2 k22EnvBrdf(float r,float NdotV){vec4 q=r*vec4(-1,-0.0275,-0.572,0.022)+vec4(1,0.0425,1.04,-0.04);float a=min(q.x*q.x,exp2(-9.28*NdotV))*q.x+q.y;return vec2(-1.04,1.04)*a+q.zw;}
vec3 k22Direct(vec3 N,vec3 V,vec3 L,vec3 radiance,vec3 F0,float r){
    float NdotL=dot(N,L);if(NdotL<=0.0)return vec3(0);
    vec3 H=normalize(L+V);float NdotV=max(dot(N,V),1e-4),NdotH=max(dot(N,H),0.0);
    vec3 F=F0+(1.0-F0)*pow(1.0-max(dot(H,V),0.0),5.0);
    return k22Ggx(NdotH,r*r)*k22Smith(NdotV,NdotL,r)*F/(4.0*NdotV)*radiance;
}
// Legacy dynamic light terms saturate at 1; cap specular radiance the same way so highlights never exceed the light.
vec3 k22DirectCapped(vec3 N,vec3 V,vec3 L,vec3 radiance,vec3 F0,float r){return min(k22Direct(N,V,L,radiance,F0,r),radiance);}
// base/illumination: legacy gamma-space texel and lighting multiplier. Diffuse keeps the legacy product exactly
// for flat normals, metallic 0 and AO 1 apart from the (1-F) energy split. IBL is ambient-only: the environment
// radiance seen in reflections is approximated by the local diffuse exitance albedo*irradiance (no sky cubemap),
// so black surfaces stay black and dielectrics conserve the legacy brightness.
vec3 k22Pbr(vec3 base,vec3 illumination,vec3 p,vec3 Ng,vec3 T,float orientation,vec3 normalSample,vec4 orm,bool roughnessMap,float legacyRoughness,vec3 eye,vec3 sunDir,float sunVisibility,bool useSun){
    vec3 N=Ng;
    if(dot(T,T)>1e-8){vec3 t=T-Ng*dot(Ng,T);if(dot(t,t)>1e-8){t=normalize(t);vec3 m=normalSample*2.0-1.0;N=normalize(t*m.x+cross(Ng,t)*orientation*m.y+Ng*m.z);}}
    float ao=orm.r,roughness=clamp(roughnessMap?orm.g:legacyRoughness,0.05,1.0),metallic=orm.b;
    vec3 albedo=k22Linear(base),irradiance=k22Linear(illumination),V=normalize(eye-p);
    float NdotV=max(dot(N,V),1e-4),bump=0.5+0.5*dot(N,Ng);bump*=bump;
    vec3 F0=mix(vec3(0.04),albedo,metallic),Fr=F0+(max(vec3(1.0-roughness),F0)-F0)*pow(1.0-NdotV,5.0);
    vec2 env=k22EnvBrdf(roughness,NdotV);
    vec3 color=albedo*irradiance*ao*((1.0-Fr)*(1.0-metallic)*bump+(F0*env.x+env.y));
    if(useSun)color+=k22DirectCapped(N,V,normalize(sunDir),vec3(sunVisibility),F0,roughness);
    for(int i=0;i<lightCount;++i){
        vec4 a=K22_LIGHT(i*5),b=K22_LIGHT(i*5+1),d=K22_LIGHT(i*5+2),e=K22_LIGHT(i*5+3),c=K22_LIGHT(i*5+4);
        if(b.a<0.0||c.z>0.5)continue;
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 L=d.w==3.0?-d.xyz:delta/max(distance,0.0001);
        float attenuation=d.w==3.0?1.0:(distance<=a.w?1.0/max(e.x+e.y*distance+e.z*distance*distance,0.0001):0.0);
        if(d.w==2.0){float rho=dot(-L,d.xyz);attenuation*=rho>=c.x?1.0:rho<=c.y?0.0:pow((rho-c.y)/max(c.x-c.y,0.0001),e.w);}
        if(attenuation>0.0&&dot(N,L)>0.0){vec3 radiance=k22Linear(clamp(b.rgb*b.a*attenuation,0.0,1.0)); // k22Direct is 0 for N.L <= 0: no visibility query.
#ifdef K22_PBR_LIGHT_VISIBILITY
            radiance*=K22_PBR_LIGHT_VISIBILITY(i);
#endif
            color+=k22DirectCapped(N,V,L,radiance,F0,roughness);}
    }
    return k22Gamma(color);
}
#endif
