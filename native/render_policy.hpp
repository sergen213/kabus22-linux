#pragma once
#include "render.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace k22::render_policy {
constexpr float pi=3.14159265358979323846f;
inline float rad(float v) { return v*pi/180; }
inline Vec3 add(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 mul(Vec3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline Vec3 unit(Vec3 a) { float l=std::sqrt(dot(a,a)); return l>0?mul(a,1/l):Vec3{0,0,1}; }
using Matrix=std::array<float,16>;
inline Vec3 transformed_point(const Matrix&m,Vec3 p){return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};}
inline Matrix product(const Matrix&a,const Matrix&b) {
    Matrix c{}; for(int j=0;j<4;++j) for(int i=0;i<4;++i) for(int k=0;k<4;++k) c[j*4+i]+=a[k*4+i]*b[j*4+k]; return c;
}
inline Matrix transform(Vec3 p,Vec3 a,Vec3 s) {
    float cp=std::cos(rad(a.x)),sp=std::sin(rad(a.x)),ct=std::cos(rad(a.y)),st=std::sin(rad(a.y)),cr=std::cos(rad(a.z)),sr=std::sin(rad(a.z));
    Vec3 x{cp*ct,sp*ct,st},y{-sp,cp,0},z=cross(x,y);
    Vec3 yr=add(mul(y,cr),mul(z,sr)),zr=add(mul(z,cr),mul(y,-sr));
    return {x.x*s.x,x.y*s.x,x.z*s.x,0,yr.x*s.y,yr.y*s.y,yr.z*s.y,0,zr.x*s.z,zr.y*s.z,zr.z*s.z,0,p.x,p.y,p.z,1};
}
inline Matrix camera_matrix(const Camera&c,float aspect) {
    Matrix basis=transform(c.position,c.angles,{1,1,1});
    Vec3 f{basis[0],basis[1],basis[2]},r{-basis[4],-basis[5],-basis[6]},u{basis[8],basis[9],basis[10]};
    Matrix view{r.x,u.x,-f.x,0,r.y,u.y,-f.y,0,r.z,u.z,-f.z,0,-dot(r,c.position),-dot(u,c.position),dot(f,c.position),1};
    if(!(aspect>0&&c.nearClip>0&&c.farClip>c.nearClip&&std::abs(c.horizontalFov)>0&&std::abs(c.horizontalFov)<179&&c.referenceAspect>0&&c.pixelAspect!=0)) throw std::runtime_error("Invalid game camera projection");
    float horizontal=std::tan(rad(std::abs(c.horizontalFov))*0.5f);
    float vertical=horizontal/(c.widescreen?c.referenceAspect:aspect)*std::abs(c.pixelAspect);
    Matrix projection{std::abs(c.pixelAspect)/(vertical*aspect),0,0,0,0,1/vertical,0,0,0,0,-(c.farClip+c.nearClip)/(c.farClip-c.nearClip),-1,0,0,-2*c.farClip*c.nearClip/(c.farClip-c.nearClip),0};
    if(c.horizontalFov<0)projection[0]=-projection[0];
    if(c.pixelAspect<0)projection[5]=-projection[5];
    return product(projection,view);
}
struct Bounds {
    Vec3 minimum{},maximum{};
    bool empty{true},finite{true};
};
inline void include_point(Bounds&bounds,Vec3 point){
    bounds.finite&=std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z);
    if(bounds.empty){bounds.minimum=bounds.maximum=point;bounds.empty=false;return;}
    bounds.minimum={std::min(bounds.minimum.x,point.x),std::min(bounds.minimum.y,point.y),std::min(bounds.minimum.z,point.z)};
    bounds.maximum={std::max(bounds.maximum.x,point.x),std::max(bounds.maximum.y,point.y),std::max(bounds.maximum.z,point.z)};
}
inline Bounds position_bounds(std::span<const Vec3>positions){
    Bounds bounds;for(auto point:positions)include_point(bounds,point);return bounds;
}
inline Bounds interpolated_bounds(const Bounds&first,const Bounds&next,float interpolation){
    float t=std::clamp(interpolation,0.0f,1.0f);
    if(t==0)return first;if(t==1)return next;
    return {add(mul(first.minimum,1-t),mul(next.minimum,t)),add(mul(first.maximum,1-t),mul(next.maximum,t)),first.empty||next.empty,first.finite&&next.finite};
}
inline bool geometry_eligible(const Bounds&bounds,const Matrix&model,const Matrix&vp,const std::array<float,4>&portalPlane){
    if(bounds.empty)return false;if(!bounds.finite)return true;
    Matrix clip=product(vp,model);
    auto outside=[&](std::array<float,4>plane){
        float maximum=plane[3],magnitude=std::abs(plane[3]);
        const std::array<float,3>low{bounds.minimum.x,bounds.minimum.y,bounds.minimum.z},high{bounds.maximum.x,bounds.maximum.y,bounds.maximum.z};
        for(int axis=0;axis<3;++axis){maximum+=plane[axis]*(plane[axis]>=0?high[axis]:low[axis]);magnitude+=std::abs(plane[axis])*std::max(std::abs(low[axis]),std::abs(high[axis]));}
        // Retain plane contacts despite CPU/GPU float operation ordering.
        return maximum<-0.00001f*std::max(magnitude,1.0f);
    };
    for(int axis=0;axis<3;++axis)for(float sign:{-1.0f,1.0f}){
        std::array<float,4>plane;for(int col=0;col<4;++col)plane[col]=clip[col*4+3]+sign*clip[col*4+axis];
        if(outside(plane))return false;
    }
    std::array<float,4>localPortal{};
    for(int col=0;col<4;++col)for(int row=0;row<4;++row)localPortal[col]+=portalPlane[row]*model[col*4+row];
    return !outside(localPortal);
}
inline Matrix planar_projection(const DrawInstance&d,const NativeShadowState&shadow,const RenderFrame&frame){
    const auto s=frame.shadowSun;const auto n=shadow.negativeNormal;const float h=shadow.planeZ;
    return {1,0,n.x,0,0,1,n.y,0,-s.x,-s.y,-s.x*n.x-s.y*n.y,0,
        s.x*h,s.y*h,frame.shadowOffset+h-(d.position.x-s.x*h)*n.x-(d.position.y-s.y*h)*n.y,1};
}
inline bool centroid_lighting(const RenderFrame&frame){
    bool enabled=false;
    for(const auto&light:frame.lights)if(!light.synthetic&&(light.type==1||light.type==2))enabled=frame.lightResRaw!=0||light.type==2;
    return enabled;
}
inline unsigned presentation_limit(WindowMode mode,std::int32_t raw){return mode==WindowMode::Fullscreen&&raw!=0?2u:1u;}
inline int presentation_interval(WindowMode mode,std::int32_t raw,bool bonus,bool vsync){return vsync&&!(mode==WindowMode::Fullscreen&&!bonus&&raw!=0)?1:0;}
inline void centroid_geometry(const Model&model,const World*world,std::vector<Vertex>&vertices,std::vector<Triangle>&triangles){
    if(!world||&world->mesh!=&model)return;
    struct Fan {std::vector<std::uint32_t> boundary;std::uint32_t center{};bool emitted{};};
    std::unordered_map<std::uint32_t,Fan>fans;
    for(const auto&t:model.triangles){
        if(t.sourceFace>=world->faces.size()||!(world->faces[t.sourceFace].nativeFlags&0x40000u))continue;
        auto&boundary=fans[t.sourceFace].boundary;
        if(boundary.empty())boundary={t.indices[0],t.indices[2],t.indices[1]};
        else {
            if(t.indices[0]!=boundary.front()||t.indices[2]!=boundary.back())throw std::runtime_error("Native world face is not an original outer fan");
            boundary.push_back(t.indices[1]);
        }
    }
    if(fans.empty())return;
    vertices=model.vertices;
    for(auto&[face,fan]:fans){
        Vertex center{};
        for(auto index:fan.boundary){
            if(index>=model.vertices.size())throw std::runtime_error("Native world face references invalid vertex");
            const auto&v=model.vertices[index];center.position=add(center.position,v.position);center.normal=add(center.normal,v.normal);
            center.uv.x+=v.uv.x;center.uv.y+=v.uv.y;center.lightmap_uv.x+=v.lightmap_uv.x;center.lightmap_uv.y+=v.lightmap_uv.y;
        }
        const float inverse=1.f/fan.boundary.size();center.position=mul(center.position,inverse);center.normal=mul(center.normal,inverse);
        center.uv.x*=inverse;center.uv.y*=inverse;center.lightmap_uv.x*=inverse;center.lightmap_uv.y*=inverse;
        fan.center=vertices.size();vertices.push_back(center);
    }
    triangles.reserve(model.triangles.size()+fans.size()*2);
    for(const auto&t:model.triangles){
        auto found=fans.find(t.sourceFace);if(found==fans.end()){triangles.push_back(t);continue;}
        auto&fan=found->second;if(fan.emitted)continue;fan.emitted=true;
        for(std::size_t i=0;i<fan.boundary.size();++i)triangles.push_back({{fan.center,fan.boundary[(i+1)%fan.boundary.size()],fan.boundary[i]},t.materialIndex,t.flags,t.sourceFace});
    }
}
inline int surface_light(int ambient,int view,bool map){
    int scalar=ambient+std::clamp(view,0,127)+128;if(scalar<0)scalar+=(-scalar)>>1;if(scalar>255)scalar-=(scalar-255)>>1;
    return std::clamp(scalar,0,255)+(map?63:0);
}
inline float surface_ambient(int ambient,int view,bool map){return std::clamp(surface_light(ambient,view,map)-128,0,255)/255.0f;}
inline float model_ambient(float ambient,int view){return (127+std::clamp(static_cast<int>(std::floor(ambient))+view,-127,127))/255.0f;}
inline Color material_ambient_light(const DrawInstance&draw,const Camera*camera,const NativeEntityLightState*light=nullptr){
    if(!draw.nativeFloorLight){
        const auto&color=draw.nativeSpecialColor;
        auto packed=(std::uint32_t(color[2])<<16)|(std::uint32_t(color[1])<<8)|std::uint32_t(color[0]);
        return {float((packed>>16)&255)/255,float((packed>>8)&255)/255,float(packed&255)/255,1};
    }
    int view=camera?static_cast<int>(std::floor(camera->nativeAmbient)):0;
    if(!camera||!camera->perDrawOpacity)view=std::clamp(view,0,127);
    int entity=static_cast<std::int16_t>(static_cast<int>(std::floor(draw.ambient)));
    int scalar=std::clamp(entity+view,-127,127)+(draw.unlit?(draw.bright?127:0):(light?light->scalar:draw.nativeLightScalar));
    const auto&delta=light?light->delta:draw.nativeLightDelta;const auto&bias=light?light->bias:draw.nativeLightBias;
    std::array<int,3>channels;for(std::size_t index=0;index<3;++index)channels[index]=delta[index]+bias[index]+scalar;
    int maximum=std::max({channels[0],channels[1],channels[2]});
    if(maximum>255){float scale=255.0f/maximum;for(auto&channel:channels)channel=static_cast<int>(channel*scale);}
    auto packed=(std::uint32_t(channels[2])<<16)|(std::uint32_t(channels[1])<<8)|std::uint32_t(channels[0]);
    return {float((packed>>16)&255)/255,float((packed>>8)&255)/255,float(packed&255)/255,1};
}
inline float material_light_strength(float albedo,int sample,bool a7){
    if(a7?albedo<=0:albedo==0)return 0;
    return std::clamp((std::floor(albedo*2)-100+sample)/(a7?127.0f:255.0f),0.0f,1.0f);
}
inline int native_skin_layers(const Model&model,int maximum){
    int layers=0;
    for(int bank=1;bank<4&&std::size_t(bank)<model.materials.size();++bank)
        if(maximum>bank&&model.materials[bank].texture&&!model.materials[bank].nativePrimarySubset)layers=bank+1;
    return layers;
}
inline std::uint32_t display_material_index(std::uint32_t format,std::uint32_t index){
    return format==7&&std::bit_cast<std::int32_t>(index)<0?0:index;
}
struct ModelPassState { bool blend{},additive{},depthWrite{true}; };
inline bool model_translucent(const DrawInstance&draw,bool bitmapAlpha){
    return draw.translucent||draw.alpha<0.999f||bitmapAlpha;
}
inline ModelPassState model_pass_state(const DrawInstance&draw,const Camera&camera,bool bitmapAlpha,bool depthWrite){
    const bool translucent=model_translucent(draw,bitmapAlpha);
    return {translucent||camera.alpha<0.999f,translucent&&draw.additive&&camera.opacityByte==255,depthWrite&&!translucent};
}
inline float surface_reflection(int percent,float reflectivity,bool map){
    long double strength=map?static_cast<long double>(std::lround(reflectivity*50))*static_cast<long double>(0.051):static_cast<long double>(2.55);
    auto opacity=static_cast<std::uint8_t>(255-static_cast<std::int64_t>(percent*strength));return (255-opacity)/255.0f;
}
inline std::int32_t turbulence_offset(int coordinate,float amplitude,float angleScale){
    float argument=static_cast<float>(double(coordinate)*angleScale),wave=static_cast<float>(std::sin(double(argument)));
    return static_cast<std::int32_t>(static_cast<std::int64_t>((double(wave)+1.0)*amplitude));
}
inline float effective_anisotropy(double enabled,float preferred){return enabled!=0?preferred:1.0f;}
inline void check_image(const Image&i) {
    if(i.width==0||i.height==0||i.width>32768||i.height>32768||std::uint64_t(i.width)*i.height*4!=i.rgba.size())throw std::runtime_error("Invalid RGBA image supplied to renderer");
}
inline Vec2 model_uv_scale(std::uint32_t format,Vec2 scale,std::uint32_t width,std::uint32_t height){
    if(format!=4&&format!=5&&format!=7)return scale;
    if(width==0||height==0||width>32768||height>32768)throw std::runtime_error("Invalid model skin dimensions supplied to renderer");
    // Native model UVs address min-8 power-of-two skins; GL uploads the selected raw image.
    scale.x*=float(std::bit_ceil(std::max(8u,width)))/width;
    scale.y*=float(std::bit_ceil(std::max(8u,height)))/height;
    return scale;
}
inline Vec2 model_uv_scale(std::uint32_t format,Vec2 scale,const Image*image){return image?model_uv_scale(format,scale,image->width,image->height):scale;}
inline void derive_tangents(const Model&model,std::span<const Vec3>positions,std::span<const Vec3>normals,std::span<Vec3>out){
    if(out.size()!=model.vertices.size()||(!positions.empty()&&positions.size()!=out.size())||(!normals.empty()&&normals.size()!=out.size()))throw std::runtime_error("Invalid tangent pose");
    std::fill(out.begin(),out.end(),Vec3{});
    auto point=[&](std::uint32_t index){return positions.empty()?model.vertices[index].position:positions[index];};
    for(const auto&triangle:model.triangles){
        auto a=triangle.indices[0],b=triangle.indices[1],c=triangle.indices[2];
        if(a>=out.size()||b>=out.size()||c>=out.size())throw std::runtime_error("Tangent triangle references invalid display vertex");
        Vec3 e1=add(point(b),mul(point(a),-1)),e2=add(point(c),mul(point(a),-1));
        Vec2 u=model.vertices[a].uv,v=model.vertices[b].uv,w=model.vertices[c].uv;
        float determinant=(v.x-u.x)*(w.y-u.y)-(w.x-u.x)*(v.y-u.y);
        if(determinant==0)continue;
        Vec3 tangent=mul(add(mul(e1,w.y-u.y),mul(e2,-(v.y-u.y))),1/determinant);
        for(auto index:triangle.indices)out[index]=add(out[index],tangent);
    }
    // Native policy: UV-degenerate vertices retain zero; never substitute a made-up normal-map basis.
    for(std::size_t index=0;index<out.size();++index){Vec3 n=normals.empty()?model.vertices[index].normal:normals[index];float squared=dot(n,n);Vec3 t=out[index];if(squared>0)t=add(t,mul(n,-dot(n,t)/squared));float length=std::sqrt(dot(t,t));out[index]=length>0?mul(t,1/length):Vec3{};}
}
inline const ScriptMaterialState* script_material(const RenderFrame*frame,std::uint32_t index){
    if(index==~0u)return nullptr;
    if(!frame||index>=frame->materials.size())throw std::runtime_error("Invalid live renderer material index");
    return &frame->materials[index];
}
inline SkinBinding resolved_skin(const Image*image,std::span<const SkinBinding>bindings){
    if(!image)return {};
    for(const auto&binding:bindings)if(binding.source==image)return binding;
    return {image,image,false,0,image->nativeTextureAlpha};
}
inline float draw_opacity(float entity,const Camera*camera){
    return camera&&camera->perDrawOpacity?std::min(entity,camera->alpha):entity;
}
inline float particle_alpha(std::int32_t alphaRaw,bool translucent,bool additive,bool textured){
    if(!translucent)return 1;
    return std::clamp<std::int64_t>((std::int64_t(alphaRaw)*10)>>(textured&&additive?11:12),0,255)/255.0f;
}
}
