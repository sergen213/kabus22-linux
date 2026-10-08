#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_uniform.glsl"
layout(triangles) in;
layout(triangle_strip,max_vertices=42) out;
layout(location=0) in float planeDistance[];
layout(location=1) in vec3 projectedPosition[];
layout(location=0) flat out float opacity;
layout(location=1) out vec3 shadowPosition;
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
        // Original source screen Y runs down; area is evaluated before Vulkan conversion.
        float area=-((p[0].y-p[1].y)*(p[0].x-p[2].x)-(p[0].x-p[1].x)*(p[0].y-p[2].y));
        if(reversed?area>areaThreshold:area<areaThreshold)continue;
        for(int duplicate=0;duplicate<2;++duplicate){
            opacity=duplicate==0?alpha:0;
            for(int j=0;j<3;++j){
                gl_Position=triangle[j];shadowPosition=positions[j==0?0:j==1?i:i+1];
                gl_Position.y=-gl_Position.y;
                gl_Position.z=(gl_Position.z+gl_Position.w)*.5;
                if(duplicate!=0)gl_Position.z-=0.00048828125*gl_Position.w;
                EmitVertex();
            }
            EndPrimitive();
        }
    }
}
