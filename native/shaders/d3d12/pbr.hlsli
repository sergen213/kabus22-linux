// HLSL port of native/shaders/pbr.glsl (same math and constants). Includer defines K22_LIGHT(i) and lightCount, and
// optionally K22_PBR_LIGHT_VISIBILITY(i).
#ifndef K22_PBR_HLSLI
#define K22_PBR_HLSLI
static const float k22Knee=0.6;
float3 k22Linear(float3 c){return pow(max(c,0),2.2);}
float3 k22Gamma(float3 c){return pow(max(c,0),1.0/2.2);}
float3 k22ToneMap(float3 g){float3 x=k22Linear(g),t=max(x-k22Knee,0)/(1.0-k22Knee);return k22Gamma(min(x,k22Knee)+(1.0-k22Knee)*t/(1.0+t));}
float3 k22InverseToneMap(float3 g){float3 y=k22Linear(clamp(g,0,1.0-0.25/255.0)),s=max(y-k22Knee,0)/(1.0-k22Knee);return k22Gamma(min(y,k22Knee)+(1.0-k22Knee)*s/(1.0-s));}
float k22Ggx(float NdotH,float a){float a2=a*a,d=NdotH*NdotH*(a2-1.0)+1.0;return a2/(3.14159265*d*d);}
float k22Smith(float NdotV,float NdotL,float r){float k=(r+1.0)*(r+1.0)/8.0;return NdotV/(NdotV*(1.0-k)+k)*NdotL/(NdotL*(1.0-k)+k);}
float2 k22EnvBrdf(float r,float NdotV){float4 q=r*float4(-1,-0.0275,-0.572,0.022)+float4(1,0.0425,1.04,-0.04);float a=min(q.x*q.x,exp2(-9.28*NdotV))*q.x+q.y;return float2(-1.04,1.04)*a+q.zw;}
float3 k22Direct(float3 N,float3 V,float3 L,float3 radiance,float3 F0,float r){
    float NdotL=dot(N,L);if(NdotL<=0.0)return 0;
    float3 H=normalize(L+V);float NdotV=max(dot(N,V),1e-4),NdotH=max(dot(N,H),0.0);
    float3 F=F0+(1.0-F0)*pow(1.0-max(dot(H,V),0.0),5.0);
    return k22Ggx(NdotH,r*r)*k22Smith(NdotV,NdotL,r)*F/(4.0*NdotV)*radiance;
}
float3 k22DirectCapped(float3 N,float3 V,float3 L,float3 radiance,float3 F0,float r){return min(k22Direct(N,V,L,radiance,F0,r),radiance);}
float3 k22Pbr(float3 base,float3 illumination,float3 p,float3 Ng,float3 T,float orientation,float3 normalSample,float4 orm,bool roughnessMap,float legacyRoughness,float3 eye,float3 sunDir,float sunVisibility,bool useSun){
    float3 N=Ng;
    if(dot(T,T)>1e-8){float3 t=T-Ng*dot(Ng,T);if(dot(t,t)>1e-8){t=normalize(t);float3 m=normalSample*2.0-1.0;N=normalize(t*m.x+cross(Ng,t)*orientation*m.y+Ng*m.z);}}
    float ao=orm.r,roughness=clamp(roughnessMap?orm.g:legacyRoughness,0.05,1.0),metallic=orm.b;
    float3 albedo=k22Linear(base),irradiance=k22Linear(illumination),V=normalize(eye-p);
    float NdotV=max(dot(N,V),1e-4),bump=0.5+0.5*dot(N,Ng);bump*=bump;
    float3 F0=lerp(0.04,albedo,metallic),Fr=F0+(max(1.0-roughness,F0)-F0)*pow(1.0-NdotV,5.0);
    float2 env=k22EnvBrdf(roughness,NdotV);
    float3 color=albedo*irradiance*ao*((1.0-Fr)*(1.0-metallic)*bump+(F0*env.x+env.y));
    if(useSun)color+=k22DirectCapped(N,V,normalize(sunDir),sunVisibility,F0,roughness);
    for(int i=0;i<lightCount;++i){
        float4 a=K22_LIGHT(i*5),b=K22_LIGHT(i*5+1),d=K22_LIGHT(i*5+2),e=K22_LIGHT(i*5+3),c=K22_LIGHT(i*5+4);
        if(b.a<0.0||c.z>0.5)continue;
        float3 delta=a.xyz-p;float distance=length(delta);
        float3 L=d.w==3.0?-d.xyz:delta/max(distance,0.0001);
        float attenuation=d.w==3.0?1.0:(distance<=a.w?1.0/max(e.x+e.y*distance+e.z*distance*distance,0.0001):0.0);
        if(d.w==2.0){float rho=dot(-L,d.xyz);attenuation*=rho>=c.x?1.0:rho<=c.y?0.0:pow((rho-c.y)/max(c.x-c.y,0.0001),e.w);}
        if(attenuation>0.0&&dot(N,L)>0.0){float3 radiance=k22Linear(clamp(b.rgb*b.a*attenuation,0.0,1.0));
#ifdef K22_PBR_LIGHT_VISIBILITY
            radiance*=K22_PBR_LIGHT_VISIBILITY(i);
#endif
            color+=k22DirectCapped(N,V,L,radiance,F0,roughness);}
    }
    return k22Gamma(color);
}
#endif
