#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec2 lightcoord;
layout(location=4) in vec3 tangent;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 uv;
layout(location=3) out vec2 lmuv;
layout(location=4) out vec3 vertexDynamicLight;
layout(location=5) out vec3 effectLight;
layout(location=6) out vec3 effectView;
layout(location=7) out vec3 vertexMaterialDiffuse;
layout(location=8) out vec3 vertexMaterialSpecular;
layout(location=9) out vec3 worldTangent;
layout(location=10) out vec4 previousClip; // Motion-vector pass (3): previous-frame clip position.
layout(set=0,binding=5,std430) readonly buffer Lights { vec4 lightData[]; };
// Pass 3 with dataPass.y set: the previous frame's GPU-deformed pose (PackedVertex, 13 floats per vertex).
layout(set=0,binding=6,std430) readonly buffer PreviousPose { float previousPose[]; };
// Shadow-caster pass (pass 1): the face view-projection is pushed per face; caster descriptor sets are shared by all faces.
layout(push_constant) uniform CasterPush { mat4 casterViewProjection; } casterPush;
vec3 nativeDynamic(vec3 p,vec3 n){
    vec3 result=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=lightData[i*5],b=lightData[i*5+1],d=lightData[i*5+2],e=lightData[i*5+3],c=lightData[i*5+4];
        if(b.a<0)continue;
        if(c.z>0.5)continue;
        vec3 delta=a.xyz-p;float distance=length(delta);
        vec3 direction=d.w==3?-d.xyz:delta/max(distance,.0001);
        float attenuation=d.w==3?1:(distance<=a.w?1/max(e.x+e.y*distance+e.z*distance*distance,.0001):0);
        if(d.w==2){float rho=dot(-direction,d.xyz);attenuation*=rho>=c.x?1:rho<=c.y?0:pow((rho-c.y)/max(c.x-c.y,.0001),e.w);}
        result+=b.rgb*abs(b.a)*attenuation*max(dot(n,direction),0);
    }
    return clamp(result,0,1);
}
void ordinaryMaterial(vec3 p,vec3 n,out vec3 diffuse,out vec3 specular){
    diffuse=preparedEmissive+preparedAmbient.rgb*ambientLight;specular=vec3(0);
    for(int i=0;i<lightCount;++i){
        vec4 a=lightData[i*5],b=lightData[i*5+1],d=lightData[i*5+2],e=lightData[i*5+3],c=lightData[i*5+4];
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
void main(){
    vertexDynamicLight=vec3(0);vertexMaterialDiffuse=vec3(0);vertexMaterialSpecular=vec3(0);effectLight=vec3(0);effectView=vec3(0);
    vec4 w=model*vec4(position,1);worldPosition=w.xyz;worldNormal=normalize(normalMatrix*normal)*normalOrientation;worldTangent=mat3(model)*tangent;
    if(worldVertexLighting)vertexDynamicLight=nativeDynamic(worldPosition,worldNormal);
    if(explicitMaterial&&effectKind==0)ordinaryMaterial(worldPosition,worldNormal,vertexMaterialDiffuse,vertexMaterialSpecular);
    if(effectKind!=0){mat3 basis=transpose(mat3(mat3(model)*tangent,mat3(model)*cross(tangent,normal),mat3(model)*normal));effectLight=normalize(basis*-(sunDirection+(effectKind==1?skillExtra.xyz:vec3(0))));effectView=normalize(basis*(cameraPosition-w.xyz));}
    uv=texcoord;lmuv=lightcoord;
    int base=gl_VertexIndex*13;previousClip=k22Pass==3?u.dataPreviousViewProjection*(u.dataPreviousModel*vec4(u.dataPass.y!=0?vec3(previousPose[base],previousPose[base+1],previousPose[base+2]):position,1)):vec4(0);
    gl_Position=(k22Pass==1?casterPush.casterViewProjection:viewProjection)*w;
    gl_Position.y=-gl_Position.y;
    gl_Position.z=(gl_Position.z+gl_Position.w)*.5;
    gl_ClipDistance[0]=dot(clipPlane,w);
}
