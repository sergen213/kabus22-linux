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
inline int material_selected_light(const RenderFrame&frame){
    for(std::size_t i=0;i<std::min<std::size_t>(frame.lights.size(),32);++i){
        const auto&light=frame.lights[i];if(light.synthetic&&!light.baked&&light.type==3)return static_cast<int>(i);
    }
    return -1;
}
inline float material_light_strength(std::int32_t albedoRaw,int sample,bool a7,bool fixedBank){
    if(a7&&!fixedBank?albedoRaw<=0:albedoRaw==0)return 0;
    const auto doubled=std::bit_cast<std::int32_t>(std::uint32_t(albedoRaw)*2u)>>10;
    return std::clamp((doubled-100+sample)/(a7?(fixedBank?128.0f:127.0f):255.0f),0.0f,1.0f);
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
// A6 OVERLAY keying (0043bb10 key format 0x200, 0043ee40/00440e90 alpha test) is skipped for BMAP 0x100 (32-bit TGA, 0040e2b0).
inline bool overlay_keyed(const DrawInstance&draw,const Image*image){return draw.overlay&&!(image&&image->a6AlphaFormat);}
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
// PBR/HDR calibration. Scene values stay in the game's gamma-2.2 space; the tone curve acts on linear
// values and is identity up to the knee (0.6 linear, 0.793 gamma), so dark/mid scenes are unchanged.
// Above the knee a Reinhard shoulder compresses the excess toward 1 (C1-continuous, strictly monotonic).
constexpr float toneKnee=0.6f,toneGamma=2.2f;
inline float tone_linear(float x){if(x<=toneKnee)return std::max(x,0.f);float t=(x-toneKnee)/(1-toneKnee);return toneKnee+(1-toneKnee)*t/(1+t);}
inline float tone_map(float gammaValue){return std::pow(tone_linear(std::pow(std::max(gammaValue,0.f),toneGamma)),1/toneGamma);}
// Exact inverse for seeding the HDR target with already-displayed LDR content; 1.0 is held just below the asymptote.
inline float inverse_tone_map(float gammaValue){
    float y=std::pow(std::clamp(gammaValue,0.f,1-0.25f/255),toneGamma);if(y<=toneKnee)return std::pow(y,1/toneGamma);
    float s=(y-toneKnee)/(1-toneKnee);return std::pow(toneKnee+(1-toneKnee)*s/(1-s),1/toneGamma);
}
inline Color inverse_tone_map(Color c){return {inverse_tone_map(c.r),inverse_tone_map(c.g),inverse_tone_map(c.b),c.a};}
// Blinn-Phong power to GGX roughness (Walter et al. alpha=sqrt(2/(n+2)), roughness=sqrt(alpha)); legacy matte default.
inline float legacy_roughness(float power){return power>0?std::clamp(std::sqrt(std::sqrt(2/(power+2))),0.05f,1.f):0.8f;}
// Backend bits for option availability: OpenGL, Vulkan raster, Vulkan ray query.
constexpr unsigned backendOpenGL=1,backendRaster=2,backendRT=4,backendVulkan=6,backendAll=7;
inline unsigned backend_bit(std::string_view backend){return backend=="opengl"?backendOpenGL:backend=="vulkan-raster"?backendRaster:backend=="vulkan-rt"?backendRT:0;}
// `restricted` names one value that only works on `restrictedBackends` (shadows=rt).
struct GraphicsOption { std::string_view key,label,fallback; std::array<std::string_view,5> values; std::size_t count; unsigned backends{backendAll}; std::string_view restricted{}; unsigned restrictedBackends{}; };
// Every implemented --graphics key. Backend availability is probed separately.
inline constexpr std::array<GraphicsOption,18> graphicsOptions{{
    {"backend","Rendering backend","opengl",{"opengl","vulkan-raster","vulkan-rt"},3},
    {"pbr","PBR materials (HDR + tone mapping)","0",{"0","1"},2},
    {"anisotropy","Anisotropic filtering","8",{"1","2","4","8","16"},5},
    {"msaa","MSAA samples","4",{"1","2","4","8"},4},
    {"texture_packs","HD texture packs","0",{"0","1"},2},
    {"shadows","Shadows (original, shadow maps, ray traced)","original",{"original","shadowmap","rt"},3,backendAll,"rt",backendRT},
    {"gi","Ray-traced global illumination","0",{"0","1"},2,backendRT},
    {"reflections","Ray-traced reflections (original mirror, roughness-aware)","original",{"original","rough"},2,backendRT},
    {"denoise","Temporal/spatial ray-tracing denoiser","0",{"0","1"},2,backendRT},
    {"bloom","Bloom","0",{"0","1"},2},
    {"ssao","Screen-space ambient occlusion","0",{"0","1"},2},
    {"fxaa","FXAA anti-aliasing","0",{"0","1"},2},
    {"taa","Temporal anti-aliasing","0",{"0","1"},2,backendVulkan},
    {"volumetric_fog","Volumetric fog light scattering","0",{"0","1"},2},
    {"smooth_normals","Smooth vertex normals (raster and ray tracing)","0",{"0","1"},2,backendVulkan},
    {"tessellation","PN-triangle tessellation of models","0",{"0","1"},2,backendVulkan},
    {"texture_upscale","Edge-aware 2x texture upscaling","0",{"0","1"},2,backendVulkan},
    {"sharpen","Contrast-adaptive sharpening","0",{"0","1"},2,backendVulkan}}};
struct GraphicsSettings {
    std::string backend{"opengl"};
    bool pbr{},texturePacks{},explicitPacks{};
    int anisotropy{},msaa{}; // 0 keeps the legacy/script-controlled default exactly.
    GraphicsFeatures features;
};
inline GraphicsSettings resolve_graphics(std::string_view preset,std::span<const std::pair<std::string,std::string>> overrides){
    GraphicsSettings g;auto&f=g.features;
    if(preset=="enhanced"){g.backend="vulkan-raster";g.anisotropy=16;g.msaa=4;g.texturePacks=true;f.shadows=ShadowMode::ShadowMap;f.ssao=f.fxaa=true;f.smoothNormals=f.tessellation=f.upscale=f.sharpen=true;}
    else if(preset=="ultra"){g.backend="vulkan-rt";g.anisotropy=16;g.msaa=4;g.texturePacks=true;g.pbr=true;f.shadows=ShadowMode::RayTraced;f.gi=f.roughReflections=f.denoise=f.bloom=f.taa=f.fog=true;f.smoothNormals=f.tessellation=f.upscale=f.sharpen=true;}
    else if(preset!="original"&&preset!="custom")throw std::runtime_error("--preset requires original, enhanced, ultra or custom");
    for(const auto&[key,value]:overrides){
        auto option=std::find_if(graphicsOptions.begin(),graphicsOptions.end(),[&](const auto&o){return o.key==key;});
        if(option==graphicsOptions.end())throw std::runtime_error("unknown --graphics key: "+key);
        if(std::find(option->values.begin(),option->values.begin()+option->count,value)==option->values.begin()+option->count)throw std::runtime_error("unsupported --graphics value: "+key+"="+value);
        const bool on=value=="1";
        if(key=="backend")g.backend=value;else if(key=="pbr")g.pbr=on;else if(key=="anisotropy")g.anisotropy=std::stoi(value);
        else if(key=="msaa")g.msaa=std::stoi(value);else if(key=="texture_packs"){g.texturePacks=on;g.explicitPacks=true;}
        else if(key=="shadows")f.shadows=value=="rt"?ShadowMode::RayTraced:value=="shadowmap"?ShadowMode::ShadowMap:ShadowMode::Original;
        else if(key=="gi")f.gi=on;else if(key=="reflections")f.roughReflections=value=="rough";else if(key=="denoise")f.denoise=on;
        else if(key=="bloom")f.bloom=on;else if(key=="ssao")f.ssao=on;else if(key=="fxaa")f.fxaa=on;else if(key=="taa")f.taa=on;
        else if(key=="smooth_normals")f.smoothNormals=on;else if(key=="tessellation")f.tessellation=on;else if(key=="texture_upscale")f.upscale=on;else if(key=="sharpen")f.sharpen=on;else f.fog=on;
    }
    return g;
}
// Drops features the backend cannot run, one stderr-ready notice each. Ray-traced shadows degrade to shadow maps.
inline GraphicsFeatures admit_features(GraphicsFeatures f,std::string_view backend,std::vector<std::string>&notices){
    const unsigned bit=backend_bit(backend);
    auto drop=[&](bool&enabled,std::string_view key,unsigned backends){if(enabled&&!(bit&backends)){enabled=false;notices.push_back(std::string(key)+" requires "+(backends==backendRT?"vulkan-rt":"Vulkan")+"; disabling it on "+std::string(backend));}};
    if(f.shadows==ShadowMode::RayTraced&&!(bit&backendRT)){f.shadows=ShadowMode::ShadowMap;notices.push_back("shadows=rt requires vulkan-rt; using shadows=shadowmap on "+std::string(backend));}
    drop(f.gi,"gi",backendRT);drop(f.roughReflections,"reflections=rough",backendRT);drop(f.denoise,"denoise",backendRT);drop(f.taa,"taa",backendVulkan);
    drop(f.smoothNormals,"smooth_normals",backendVulkan);drop(f.tessellation,"tessellation",backendVulkan);drop(f.upscale,"texture_upscale",backendVulkan);drop(f.sharpen,"sharpen",backendVulkan);
    return f;
}
// Preset-selected backends degrade along this chain with a stderr notice; explicit choices never do.
inline std::string_view fallback_backend(std::string_view backend){return backend=="vulkan-rt"?"vulkan-raster":backend=="vulkan-raster"?"opengl":"";}
// ---- Wave-2 effect helpers shared by OpenGL and Vulkan (CPU-tested in k22-render-check --policy). ----
// Background views go through an offscreen composite (resolve, bloom, tone map, TAA, FXAA, sharpen) only when needed.
inline bool composite_enabled(bool pbr,const GraphicsFeatures&f){return pbr||f.bloom||f.fxaa||f.taa||f.sharpen;}
// Only shadows=shadowmap renders depth maps; volumetric fog reuses them then, and is unshadowed under shadows=rt
// (no 7-pass depth render just for fog).
inline bool shadow_maps_enabled(const GraphicsFeatures&f){return f.shadows==ShadowMode::ShadowMap;}
inline Matrix inverse(const Matrix&m){
    Matrix r{};
    r[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
    r[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
    r[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
    r[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
    r[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
    r[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
    r[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
    r[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
    r[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
    r[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
    r[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
    r[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
    r[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
    r[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
    r[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
    r[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
    float det=m[0]*r[0]+m[1]*r[4]+m[2]*r[8]+m[3]*r[12];if(det==0||!std::isfinite(det))throw std::runtime_error("Singular renderer matrix");
    for(auto&v:r)v/=det;return r;
}
// Clip-space point (GL conventions, z in [-1,1]) for a world point; w-divided.
inline Vec3 project_ndc(const Matrix&m,Vec3 p){float w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];auto q=transformed_point(m,p);return {q.x/w,q.y/w,q.z/w};}
inline Matrix look_at(Vec3 eye,Vec3 forward,Vec3 up){
    Vec3 f=unit(forward),s=unit(cross(f,up)),u=cross(s,f);
    return {s.x,u.x,-f.x,0,s.y,u.y,-f.y,0,s.z,u.z,-f.z,0,-dot(s,eye),-dot(u,eye),dot(f,eye),1};
}
inline Matrix orthographic(float r,float nearPlane,float farPlane){return {1/r,0,0,0,0,1/r,0,0,0,0,-2/(farPlane-nearPlane),0,0,0,-(farPlane+nearPlane)/(farPlane-nearPlane),1};}
inline Matrix perspective90(float nearPlane,float farPlane){return {1,0,0,0,0,1,0,0,0,0,-(farPlane+nearPlane)/(farPlane-nearPlane),-1,0,0,-2*farPlane*nearPlane/(farPlane-nearPlane),0};}
// Directional (SUN) shadow map: bounding sphere of the camera frustum up to `distance`, depth range widened to every
// world caster, center snapped to whole shadow texels so the map does not shimmer while the camera moves.
inline Matrix sun_shadow_matrix(const Camera&camera,float aspect,Vec3 sunDirection,const Bounds&world,float distance,unsigned resolution){
    Camera slice=camera;slice.farClip=std::min(camera.farClip,std::max(distance,camera.nearClip*2));
    auto inverseVP=inverse(camera_matrix(slice,aspect));std::array<Vec3,8>corners;Vec3 center{};
    for(int i=0;i<8;++i){corners[i]=project_ndc(inverseVP,{i&1?1.f:-1.f,i&2?1.f:-1.f,i&4?1.f:-1.f});center=add(center,mul(corners[i],1/8.f));}
    float radius=0;for(auto c:corners){auto d=add(c,mul(center,-1));radius=std::max(radius,std::sqrt(dot(d,d)));}
    radius=std::ceil(radius);Vec3 toward=unit(sunDirection),up=std::abs(toward.z)>0.99f?Vec3{1,0,0}:Vec3{0,0,1};
    auto view=look_at({},mul(toward,-1),up);float texel=2*radius/float(resolution);
    auto local=transformed_point(view,center);local.x=std::floor(local.x/texel)*texel;local.y=std::floor(local.y/texel)*texel;
    float nearest=local.z+radius,farthest=local.z-radius; // View space looks down -Z.
    if(!world.empty&&world.finite)for(int i=0;i<8;++i){auto p=transformed_point(view,{i&1?world.maximum.x:world.minimum.x,i&2?world.maximum.y:world.minimum.y,i&4?world.maximum.z:world.minimum.z});nearest=std::max(nearest,p.z);farthest=std::min(farthest,p.z);}
    Matrix shift{1,0,0,0,0,1,0,0,0,0,1,0,-local.x,-local.y,0,1};
    return product(orthographic(radius,-nearest-1,-farthest+1),product(shift,view));
}
// GL cube-map face order/orientation (+X,-X,+Y,-Y,+Z,-Z); the face image at texel (s,t) stores the GL convention.
inline Matrix cube_face_matrix(Vec3 position,int face,float nearPlane,float farPlane){
    static constexpr std::array<std::array<float,6>,6> axes{{{1,0,0,0,-1,0},{-1,0,0,0,-1,0},{0,1,0,0,0,1},{0,-1,0,0,0,-1},{0,0,1,0,-1,0},{0,0,-1,0,-1,0}}};
    const auto&a=axes.at(std::size_t(face));return product(perspective90(nearPlane,farPlane),look_at(position,{a[0],a[1],a[2]},{a[3],a[4],a[5]}));
}
// The one point/spot light that gets a cube shadow map: strongest eligible dynamic light at the camera
// (its range must exceed twice the cube near plane, which keeps light fixtures from occluding their own light).
constexpr float cubeShadowNear=16;
inline int shadow_light_index(std::span<const RenderLight>lights,Vec3 eye){
    int best=-1;float bestScore=0;
    for(std::size_t i=0;i<std::min<std::size_t>(lights.size(),32);++i){
        const auto&l=lights[i];if(l.baked||l.synthetic||(l.type!=1&&l.type!=2)||!(l.range>2*cubeShadowNear)||!(l.intensity>0))continue;
        auto d=add(l.position,mul(eye,-1));float distance=std::sqrt(dot(d,d));
        float score=l.intensity*(l.color.r+l.color.g+l.color.b)/(1+distance/l.range);
        if(score>bestScore){bestScore=score;best=int(i);}
    }
    return best;
}
constexpr unsigned sunShadowResolution=2048,cubeShadowResolution=1024;constexpr float sunShadowDistance=2048;
// TAA: 8-sample Halton(2,3) sub-pixel offsets in pixels, applied in clip space.
inline float halton(std::uint32_t index,std::uint32_t base){float f=1,r=0;for(;index;index/=base){f/=float(base);r+=f*float(index%base);}return r;}
inline Vec2 taa_jitter(std::uint64_t frame){auto i=std::uint32_t(frame%8)+1;return {halton(i,2)-.5f,halton(i,3)-.5f};}
inline Matrix jittered(Matrix vp,Vec2 pixels,float width,float height){
    const float dx=2*pixels.x/width,dy=2*pixels.y/height;for(int c=0;c<4;++c){vp[c*4]+=dx*vp[c*4+3];vp[c*4+1]+=dy*vp[c*4+3];}return vp;
}
// Temporal accumulation (TAA and ray-traced denoiser, mirrored in the shaders): exponential history with a
// 1/(n+1) ramp, rejected on disocclusion (relative view-depth change or normal change).
constexpr float historyDepthTolerance=0.05f,historyNormalTolerance=0.9f;
inline float temporal_weight(float historyLength,float minimum){return std::max(1/(std::max(historyLength,0.f)+1),minimum);}
inline bool history_valid(float viewDepth,float previousViewDepth,float normalDot){
    return viewDepth>0&&previousViewDepth>0&&std::abs(viewDepth-previousViewDepth)<=historyDepthTolerance*std::max(viewDepth,previousViewDepth)&&normalDot>=historyNormalTolerance;
}
// B3-spline a-trous kernel taps (1,4,6,4,1)/16 per axis.
inline float atrous_tap(int offset){constexpr std::array<float,5>taps{1/16.f,4/16.f,6/16.f,4/16.f,1/16.f};return taps.at(std::size_t(std::clamp(offset,-2,2)+2));}
}
