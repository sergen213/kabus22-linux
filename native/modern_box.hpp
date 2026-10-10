#pragma once
#include "assets.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>

namespace k22::modern_box {
// A6 00585f60 / A7 100b2680's USE_BOX mesh primitive is an oriented,
// affine-normalized sphere, not a rectangular SAT box. These are the source
// 0059f9a0 and 100d0890 moving feature rules, not the point query.
inline Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline long double dot(Vec3 a,Vec3 b){return (static_cast<long double>(a.y)*b.y+static_cast<long double>(a.z)*b.z)+static_cast<long double>(a.x)*b.x;}
inline Vec3 cross(Vec3 a,Vec3 b){return {float(static_cast<long double>(a.y)*b.z-static_cast<long double>(a.z)*b.y),float(static_cast<long double>(a.z)*b.x-static_cast<long double>(a.x)*b.z),float(static_cast<long double>(a.x)*b.y-static_cast<long double>(a.y)*b.x)};}
inline Vec3 unit(Vec3 a){const auto squared=dot(a,a);return squared!=0?mul(a,float(1/std::sqrt(squared))):a;}
inline bool inside(Vec3 p,Vec3 a,Vec3 b,Vec3 c){
    // 0059f7b0/100d03e0 tests float sign bits; the opposite edge is strict.
    const auto u=sub(b,a),v=sub(c,a),q=sub(p,a);
    const float uu=float(dot(u,u)),uv=float(dot(u,v)),vv=float(dot(v,v));
    const float qu=float(dot(q,u)),qv=float(dot(q,v));
    const float x=float(static_cast<long double>(qu)*vv-static_cast<long double>(qv)*uv);
    const float y=float(static_cast<long double>(qv)*uu-static_cast<long double>(qu)*uv);
    const float z=float((static_cast<long double>(x)+y)-(static_cast<long double>(vv)*uu-static_cast<long double>(uv)*uv));
    return (~(std::bit_cast<unsigned>(x)|std::bit_cast<unsigned>(y))&std::bit_cast<unsigned>(z)&0x80000000u)!=0;
}
inline bool root(float a,float b,float c,float limit,bool bonus,float& result){
    // 0059f8d0 / 100d0500. Zero A and strict -.01/limit comparisons are
    // intentional; no epsilon, discriminant clamp or initial-overlap shortcut.
    const float discriminant=float(static_cast<long double>(b)*b-static_cast<long double>(a)*c*4);
    if(!(discriminant>=0))return false;
    const auto square=std::sqrt(static_cast<long double>(discriminant));
    float first=float((-static_cast<long double>(b)-square)/(static_cast<long double>(a)+a));
    float second=float((square-b)/(static_cast<long double>(a)+a));
    if(bonus&&!(first>=0||second>=0))return false;
    if(second<first)std::swap(first,second);
    constexpr float lower=std::bit_cast<float>(0xbc23d70au);
    if(first>lower&&first<limit){result=first;return true;}
    if(second>lower&&second<limit){result=second;return true;}
    return false;
}
struct Contact {float fraction{},scalar{};Vec3 position{},normal{};bool initialOverlap{};};
struct Query {
    Vec3 center,half,delta,direction,velocity;
    std::array<Vec3,3> axes,rows;
    Vec3 translation;
    float length,buffer;
    bool bonus;
    // Moving USE_BOX mesh primitive only. The original stationary packet's
    // reference direction is uninitialized in the public query wrappers.
    Query(Vec3 origin,const std::array<Vec3,3>& orientation,Vec3 extents,Vec3 displacement,float margin,bool a7):center(origin),half(extents),delta(displacement),axes(orientation),buffer(margin),bonus(a7){
        // 005a0b00/100d1690: inverse rigid orientation, then reciprocal half
        // extents. Form one affine matrix before transforming mesh vertices.
        const std::array h{half.x,half.y,half.z};
        for(unsigned i=0;i<3;++i)rows[i]=mul(axes[i],1.f/h[i]);
        translation={float(-dot(rows[0],center)),float(-dot(rows[1],center)),float(-dot(rows[2],center))};
        velocity={float(dot(rows[0],delta)),float(dot(rows[1],delta)),float(dot(rows[2],delta))};
        length=float(std::sqrt(dot(delta,delta)));direction=unit(delta);
    }
    Vec3 local(Vec3 p)const{return {float(dot(rows[0],p)+translation.x),float(dot(rows[1],p)+translation.y),float(dot(rows[2],p)+translation.z)};}
    Vec3 world(Vec3 p)const{
        const auto x=mul(axes[0],p.x*half.x),y=mul(axes[1],p.y*half.y),z=mul(axes[2],p.z*half.z);
        return {float(((static_cast<long double>(x.x)+z.x)+y.x)+center.x),float(((static_cast<long double>(x.y)+z.y)+y.y)+center.y),float(((static_cast<long double>(x.z)+z.z)+y.z)+center.z)};
    }
    Vec3 worldNormal(Vec3 n)const{
        // 0059f5b0/100d01e0 uses the cofactor products, not R*n or SCALE.
        const Vec3 cofactor{float(static_cast<long double>(n.x)*half.y*half.z),float(static_cast<long double>(n.y)*half.x*half.z),float(static_cast<long double>(half.y)*half.x*n.z)};
        const auto x=mul(axes[0],cofactor.x),y=mul(axes[1],cofactor.y),z=mul(axes[2],cofactor.z);
        return unit({float((static_cast<long double>(z.x)+y.x)+x.x),float((static_cast<long double>(z.y)+y.y)+x.y),float((static_cast<long double>(z.z)+y.z)+x.z)});
    }
    Vec3 broadExtent()const{return {std::abs(axes[0].x)*half.x+std::abs(axes[1].x)*half.y+std::abs(axes[2].x)*half.z,std::abs(axes[0].y)*half.x+std::abs(axes[1].y)*half.y+std::abs(axes[2].y)*half.z,std::abs(axes[0].z)*half.x+std::abs(axes[1].z)*half.y+std::abs(axes[2].z)*half.z};}
    std::optional<Contact> triangle(Vec3 worldA,Vec3 worldB,Vec3 worldC,const Vec3& retainedA,const Vec3& retainedB,const Vec3& retainedC)const{
        const Vec3 a=local(worldA),b=local(worldB),c=local(worldC);
        const auto normal=unit(cross(sub(b,a),sub(c,a)));
        const float constant=float(-dot(a,normal));
        if(!(dot(normal,unit(velocity))<0))return {}; // One-sided source face admission.
        const float speed=float(dot(normal,velocity));
        const long double distance=constant;
        if(bonus&&std::abs(distance)>1&&std::abs(distance+speed)>1&&distance*(distance+speed)>0)return {};
        long double incoming=0;bool embedded=speed==0;
        if(embedded){if(!bonus&&std::abs(distance)>=1)return {};}
        else {
            incoming=(-1-distance)/speed;auto outgoing=(1-distance)/speed;
            if(outgoing<incoming)std::swap(incoming,outgoing);
            if(bonus){if(incoming>1)return {};}
            else {if(incoming>1||outgoing<0)return {};incoming=std::clamp(incoming,0.L,1.L);}
        }
        float time=1;Vec3 point{};bool found=false;
        if(bonus&&incoming<0){
            point=mul(normal,-float(distance));
            if(inside(point,a,b,c)){time=float(distance-1);found=true;}
            else for(Vec3 p:{a,b,c})if(dot(sub(p,velocity),sub(p,velocity))<=dot(p,p)&&dot(p,p)<=1){point=p;time=float(std::sqrt(dot(p,p))-1);found=true;break;}
        }else if(!embedded){
            const auto p=add(mul(normal,-1),mul(velocity,float(incoming)));
            if(inside(p,a,b,c)){point=p;time=float(incoming);found=true;}
        }
        if(!found){
            const float velocitySquared=float(dot(velocity,velocity));
            // Source order: A,B,C vertices, then AB,BC,CA edges. Roots use a
            // strict best-time comparison, so equal-time later features lose.
            for(Vec3 p:{a,b,c}){float t;const auto q=mul(p,-1);if(root(velocitySquared,float(dot(q,velocity)*2),float(dot(p,p)-1),time,bonus,t)){time=t;point=p;found=true;}}
            const std::array points{a,b,c};
            for(unsigned i=0;i<3;++i){const auto p=points[i],edge=sub(points[(i+1)%3],p),q=mul(p,-1);const float ee=float(dot(edge,edge)),ev=float(dot(edge,velocity)),eq=float(dot(q,edge));float t;
                const float aa=float(static_cast<long double>(ev)*ev-static_cast<long double>(ee)*velocitySquared);
                const auto twice=mul(velocity,2);
                const float bb=float(dot(q,twice)*ee-(static_cast<long double>(eq)*ev+static_cast<long double>(eq)*ev));
                const float cc=float(static_cast<long double>(eq)*eq+(1-dot(q,q))*ee);
                if(!root(aa,bb,cc,time,bonus,t))continue;
                const float s=float((static_cast<long double>(ev)*t-eq)/ee);
                if(s>=0&&s<=1){time=t;point=add(p,mul(edge,s));found=true;}
            }
        }
        if(!found)return {};
        // 100d1160 keeps the pre-transform vertex pointers and at
        // 100d12a1..12c8 publishes their face normal directly for negative
        // overlaps, without candidate pose. Ordinary/A6 normals stay radial.
        const bool planeNormal=bonus&&static_cast<double>(time)<std::bit_cast<double>(0xbf50624dd2f1a9fcull);
        Vec3 resultNormal;
        if(planeNormal){
            // 100e4170 keeps the reciprocal in x87 until the three final
            // float stores; no intermediate float-rounded reciprocal.
            const auto n=cross(sub(retainedB,retainedA),sub(retainedC,retainedA));
            const auto squared=(static_cast<long double>(n.x)*n.x+static_cast<long double>(n.y)*n.y)+static_cast<long double>(n.z)*n.z;
            const auto reciprocal=squared!=0?1/std::sqrt(squared):1;
            resultNormal={float(n.x*reciprocal),float(n.y*reciprocal),float(n.z*reciprocal)};
        }else resultNormal=worldNormal(sub(mul(velocity,time),point));
        const float travel=length*time,backoff=bonus?buffer:std::min(travel,buffer);
        const float scalar=travel-backoff;
        const auto position=sub(world(point),mul(direction,backoff));
        return Contact{length!=0?std::max(0.f,scalar/length):0,scalar,position,resultNormal,time<=0};
    }
};
}
