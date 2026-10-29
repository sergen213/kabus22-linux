#include "model_gltf.hpp"
#include "media.hpp"
#include "render_policy.hpp"
#include "texture_pack.hpp"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace k22 {
namespace {
[[noreturn]] void fail(std::string_view m){throw std::runtime_error("glTF: "+std::string(m));}
Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 operator*(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 unit(Vec3 a,Vec3 fallback={0,0,1}){const float l=std::sqrt(dot(a,a));return l>1e-12f?a*(1/l):fallback;}
// A6 is right-handed Z-up; its triangles are counter-clockwise there (clockwise only after D3D's y/z swap), as glTF wants.
Vec3 to_gltf(Vec3 a){return {a.x,a.z,-a.y};}
Vec3 from_gltf(Vec3 g){return {g.x,-g.z,g.y};}
std::string number(float v){char b[32];auto r=std::to_chars(b,b+sizeof b,v);return {b,r.ptr};}
// JSON string; bytes >= 0x80 (original cp1254 names) are taken as Latin-1 so the file stays valid UTF-8.
std::string quote(std::string_view s){
    std::string out="\"";
    for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<0x20||c>=0x7f){char b[8];std::snprintf(b,sizeof b,"\\u%04x",c);out+=b;}else out+=char(c);}
    return out+'"';
}
// Smooth (area-weighted) normals; triangles in outward (counter-clockwise) order.
std::vector<Vec3> smooth_normals(const std::vector<Vec3>& p,const std::vector<std::array<uint32_t,3>>& triangles){
    std::vector<Vec3> n(p.size());
    for(const auto& t:triangles){const auto f=cross(p[t[1]]-p[t[0]],p[t[2]]-p[t[0]]);for(auto i:t)n[i]=n[i]+f;}
    for(auto& v:n)v=unit(v);
    return n;
}
// Closest point on triangle abc to p as barycentric weights (Ericson, Real-Time Collision Detection 5.1.5).
std::array<float,3> closest_barycentric(Vec3 p,Vec3 a,Vec3 b,Vec3 c){
    const Vec3 ab=b-a,ac=c-a,ap=p-a;const float d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return {1,0,0};
    const Vec3 bp=p-b;const float d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return {0,1,0};
    const float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0){const float v=d1/(d1-d3);return {1-v,v,0};}
    const Vec3 cp=p-c;const float d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return {0,0,1};
    const float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0){const float w=d2/(d2-d6);return {1-w,0,w};}
    const float va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0){const float w=(d4-d3)/((d4-d3)+(d5-d6));return {0,1-w,w};}
    const float denom=1/(va+vb+vc),v=vb*denom,w=vc*denom;return {1-v-w,v,w};
}
// Wrap deform (after cutscene-retarget d165ef4): each new rest point binds to its k nearest original rest triangles facing it
// (searched again within two vertex rings of the nearest one) at the closest point, offset in that point's frame (normal,
// first edge), inverse-distance² weights; every original frame rebuilds it. A point on an original triangle follows it.
// ponytail: no tear repair (d165ef4 rebinds torn edges); add it when replacement meshes stray far from the original surface.
std::vector<std::vector<Vec3>> wrap_deform(const Model& original,const std::vector<Vec3>& points,const std::vector<Vec3>& pointNormals){
    const auto& tris=original.sourceTriangles;const auto& rest=original.frames[0].sourcePositions;
    const size_t frames=original.frames.size();
    // Outward triangle orientation: the side the original's own vertex normals point to.
    auto face=[&](const std::vector<Vec3>& p,uint32_t t){const auto& x=tris[t];return cross(p[x[1]]-p[x[0]],p[x[2]]-p[x[0]]);};
    std::vector<float> orient(tris.size(),1);
    for(uint32_t t=0;t<tris.size();++t){const auto& x=tris[t];const auto& n=original.frames[0].sourceNormals;if(dot(face(rest,t),n[x[0]]+n[x[1]]+n[x[2]])<0)orient[t]=-1;}
    auto frameAt=[&](size_t f,uint32_t t,const std::array<float,3>& w,Vec3& o,Vec3& tx,Vec3& ty,Vec3& tz){
        const auto& p=original.frames[f].sourcePositions;const auto& n=original.frames[f].sourceNormals;const auto& x=tris[t];
        o=p[x[0]]*w[0]+p[x[1]]*w[1]+p[x[2]]*w[2];
        const Vec3 fn=unit(face(p,t)*orient[t]);tz=unit(n[x[0]]*w[0]+n[x[1]]*w[1]+n[x[2]]*w[2],fn);
        if(dot(tz,fn)<0)tz=fn; // flat-shaded or unset MDL normals
        const Vec3 e=p[x[1]]-p[x[0]];tx=unit(e-tz*dot(e,tz),unit(cross(tz,{0,0,1}),{1,0,0}));ty=cross(tz,tx);
    };
    Vec3 lo=rest[0],hi=rest[0];for(auto v:rest){lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
    const float size=std::sqrt(dot(hi-lo,hi-lo)),floor2=std::max(1e-12f,(size*1e-3f)*(size*1e-3f));
    std::vector<char> usable(tris.size());for(uint32_t t=0;t<tris.size();++t)usable[t]=std::sqrt(dot(face(rest,t),face(rest,t)))>size*size*1e-9f;
    std::vector<std::vector<uint32_t>> vertexTriangles(rest.size());for(uint32_t t=0;t<tris.size();++t)for(auto i:tris[t])vertexTriangles[i].push_back(t);
    constexpr size_t k=4;
    struct Bind{uint32_t triangle{};std::array<float,3> bary{};Vec3 offset;float weight{};};
    std::vector<std::array<Bind,k>> binds(points.size());std::vector<uint32_t> counts(points.size());
    std::vector<uint32_t> all(tris.size()),patch,seen(tris.size(),~0u);for(uint32_t t=0;t<all.size();++t)all[t]=t;
    for(uint32_t v=0;v<points.size();++v){
        std::array<std::pair<float,uint32_t>,k> best;
        auto nearest=[&](const std::vector<uint32_t>& list){
            best.fill({std::numeric_limits<float>::max(),0});
            for(int pass=0;pass<2&&best[0].first==std::numeric_limits<float>::max();++pass)for(auto t:list){
                if(!usable[t]||(pass==0&&dot(face(rest,t)*orient[t],pointNormals[v])<0))continue; // facing away (second pass: any)
                const auto& x=tris[t];const auto w=closest_barycentric(points[v],rest[x[0]],rest[x[1]],rest[x[2]]);
                const Vec3 q=rest[x[0]]*w[0]+rest[x[1]]*w[1]+rest[x[2]]*w[2];const float d2=dot(points[v]-q,points[v]-q);
                if(d2>=best[k-1].first)continue;
                size_t i=k-1;while(i&&best[i-1].first>d2){best[i]=best[i-1];--i;}
                best[i]={d2,t};
            }
        };
        nearest(all);
        if(best[0].first==std::numeric_limits<float>::max())fail("original model has no usable triangles to follow");
        patch.assign(1,best[0].second);seen[best[0].second]=v;
        for(int ring=0;ring<2;++ring)for(size_t end=patch.size(),i=0;i<end;++i)for(auto c:tris[patch[i]])for(auto t:vertexTriangles[c])if(seen[t]!=v){seen[t]=v;patch.push_back(t);}
        nearest(patch);
        float total=0;
        for(const auto& [d2,t]:best){
            if(d2==std::numeric_limits<float>::max())break;
            const auto& x=tris[t];Bind b{t,closest_barycentric(points[v],rest[x[0]],rest[x[1]],rest[x[2]]),{},1/(d2+floor2)};
            Vec3 o,tx,ty,tz;frameAt(0,t,b.bary,o,tx,ty,tz);const Vec3 d=points[v]-o;b.offset={dot(d,tx),dot(d,ty),dot(d,tz)};
            total+=b.weight;binds[v][counts[v]++]=b;
        }
        for(uint32_t i=0;i<counts[v];++i)binds[v][i].weight/=total;
    }
    std::vector<std::vector<Vec3>> out(frames,std::vector<Vec3>(points.size()));
    for(size_t f=0;f<frames;++f)for(size_t v=0;v<points.size();++v){
        Vec3 sum{};for(uint32_t i=0;i<counts[v];++i){const auto& b=binds[v][i];Vec3 o,tx,ty,tz;frameAt(f,b.triangle,b.bary,o,tx,ty,tz);sum=sum+(o+tx*b.offset.x+ty*b.offset.y+tz*b.offset.z)*b.weight;}
        out[f][v]=sum;
    }
    return out;
}
struct Reader{const std::function<std::vector<uint8_t>(std::string_view)>* read;std::string error;};
cgltf_result read_file(const cgltf_memory_options*,const cgltf_file_options* options,const char* path,cgltf_size* size,void** data){
    auto& reader=*static_cast<Reader*>(options->user_data);
    try{
        auto bytes=(*reader.read)(path);if(*size&&bytes.size()<*size){reader.error=std::string(path)+" is shorter than its declared byteLength";return cgltf_result_data_too_short;}
        void* copy=std::malloc(std::max<size_t>(1,bytes.size()));if(!copy)return cgltf_result_out_of_memory;
        std::memcpy(copy,bytes.data(),bytes.size());*size=bytes.size();*data=copy;return cgltf_result_success;
    }catch(const std::exception& e){reader.error=e.what();return cgltf_result_file_not_found;}
}
void release_file(const cgltf_memory_options*,const cgltf_file_options*,void* data){std::free(data);}
}

std::filesystem::path export_gltf(const Model& m,const std::filesystem::path& dir,std::string_view stem){
    if(m.vertices.empty()||m.triangles.empty())fail("model has no triangles");
    std::filesystem::create_directories(dir);
    std::vector<uint8_t> bin;std::string views,accessors,primitives,images,textures,materials;size_t viewCount=0,accessorCount=0;
    auto comma=[](std::string& s){if(!s.empty())s+=',';};
    auto view=[&](const void* data,size_t bytes,bool indices){
        while(bin.size()%4)bin.push_back(0);comma(views);
        views+="{\"buffer\":0,\"byteOffset\":"+std::to_string(bin.size())+",\"byteLength\":"+std::to_string(bytes)+",\"target\":"+(indices?"34963":"34962")+"}";
        auto p=static_cast<const uint8_t*>(data);bin.insert(bin.end(),p,p+bytes);return viewCount++;
    };
    auto vec3s=[&](const std::vector<Vec3>& v,bool bounds){
        const auto bv=view(v.data(),v.size()*sizeof(Vec3),false);comma(accessors);
        accessors+="{\"bufferView\":"+std::to_string(bv)+",\"componentType\":5126,\"count\":"+std::to_string(v.size())+",\"type\":\"VEC3\"";
        if(bounds){Vec3 lo=v[0],hi=v[0];for(auto p:v){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
            accessors+=",\"min\":["+number(lo.x)+','+number(lo.y)+','+number(lo.z)+"],\"max\":["+number(hi.x)+','+number(hi.y)+','+number(hi.z)+']';}
        accessors+='}';return accessorCount++;
    };
    // Skin images, written once per distinct bitmap.
    std::unordered_map<const Image*,size_t> written;
    for(size_t i=0;i<m.materials.size();++i){
        const auto& mat=m.materials[i];comma(materials);
        materials+="{\"name\":\"skin"+std::to_string(i+1)+"\",\"pbrMetallicRoughness\":{\"metallicFactor\":0,\"roughnessFactor\":1";
        if(mat.texture){
            auto [it,fresh]=written.try_emplace(mat.texture.get(),written.size());
            if(fresh){
                const auto file=std::string(stem)+"_skin"+std::to_string(i+1)+".png";const auto png=encode_png(*mat.texture);
                std::ofstream out(dir/file,std::ios::binary);if(!out.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size())))fail("cannot write "+(dir/file).string());
                comma(images);images+="{\"uri\":"+quote(file)+'}';comma(textures);textures+="{\"sampler\":0,\"source\":"+std::to_string(it->second)+'}';
            }
            materials+=",\"baseColorTexture\":{\"index\":"+std::to_string(it->second)+'}';
        }
        materials+="}}";
    }
    std::map<uint32_t,std::vector<uint32_t>> groups;
    for(uint32_t t=0;t<m.triangles.size();++t){auto mat=render_policy::display_material_index(m.formatVersion,m.triangles[t].materialIndex);groups[m.materials.empty()?0:std::min<uint32_t>(mat,uint32_t(m.materials.size()-1))].push_back(t);}
    for(const auto& [mat,list]:groups){
        std::vector<uint32_t> used;for(auto t:list)for(auto i:m.triangles[t].indices)used.push_back(i);
        std::ranges::sort(used);used.erase(std::unique(used.begin(),used.end()),used.end());
        std::vector<uint32_t> local(m.vertices.size(),~0u);for(uint32_t i=0;i<used.size();++i)local[used[i]]=i;
        const Material* material=m.materials.empty()?nullptr:&m.materials[mat];
        const Vec2 scale=render_policy::model_uv_scale(m.formatVersion,material?material->uvScale:Vec2{1,1},material?material->texture.get():nullptr),offset=material?material->uvOffset:Vec2{};
        std::vector<Vec3> base,normals;std::vector<Vec2> uv;
        for(auto r:used){base.push_back(to_gltf(m.vertices[r].position));normals.push_back(unit(to_gltf(m.vertices[r].normal)));const auto t=m.vertices[r].uv;uv.push_back({t.x*scale.x+offset.x,t.y*scale.y+offset.y});}
        std::vector<uint32_t> indices;for(auto t:list){for(auto i:m.triangles[t].indices)indices.push_back(local[i]);}
        const auto position=vec3s(base,true),normal=vec3s(normals,false);
        const auto uvView=view(uv.data(),uv.size()*sizeof(Vec2),false);comma(accessors);
        accessors+="{\"bufferView\":"+std::to_string(uvView)+",\"componentType\":5126,\"count\":"+std::to_string(uv.size())+",\"type\":\"VEC2\"}";const auto texcoord=accessorCount++;
        const auto indexView=view(indices.data(),indices.size()*4,true);comma(accessors);
        accessors+="{\"bufferView\":"+std::to_string(indexView)+",\"componentType\":5125,\"count\":"+std::to_string(indices.size())+",\"type\":\"SCALAR\"}";const auto index=accessorCount++;
        std::string targets;
        for(const auto& frame:m.frames){
            std::vector<Vec3> delta;delta.reserve(used.size());for(size_t i=0;i<used.size();++i)delta.push_back(to_gltf(frame.positions[used[i]])-base[i]);
            comma(targets);targets+="{\"POSITION\":"+std::to_string(vec3s(delta,true))+'}';
        }
        comma(primitives);
        primitives+="{\"attributes\":{\"POSITION\":"+std::to_string(position)+",\"NORMAL\":"+std::to_string(normal)+",\"TEXCOORD_0\":"+std::to_string(texcoord)+"},\"indices\":"+std::to_string(index);
        if(!m.materials.empty())primitives+=",\"material\":"+std::to_string(mat);
        if(!targets.empty())primitives+=",\"targets\":["+targets+']';
        primitives+='}';
    }
    std::string names;for(const auto& frame:m.frames){comma(names);names+=quote(frame.name);}
    const auto binName=std::string(stem)+".bin";
    std::string json="{\"asset\":{\"version\":\"2.0\",\"generator\":\"kabus22 " K22_VERSION " (1 unit = 1 A6 quant; A6 x,y,z = glTF x,-z,y)\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"name\":"+quote(stem)+",\"mesh\":0}],\"meshes\":[{\"name\":"+quote(stem)+",\"primitives\":["+primitives+"]"+(m.frames.empty()?"":",\"extras\":{\"targetNames\":["+names+"]}")+"}],"
        "\"accessors\":["+accessors+"],\"bufferViews\":["+views+"],\"buffers\":[{\"uri\":"+quote(binName)+",\"byteLength\":"+std::to_string(bin.size())+"}]";
    if(!materials.empty())json+=",\"materials\":["+materials+']';
    if(!images.empty())json+=",\"images\":["+images+"],\"textures\":["+textures+"],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}]";
    json+="}\n";
    auto put=[&](const std::filesystem::path& p,const void* data,size_t size){std::ofstream out(p,std::ios::binary);if(!out.write(static_cast<const char*>(data),std::streamsize(size)))fail("cannot write "+p.string());};
    put(dir/binName,bin.data(),bin.size());const auto path=dir/(std::string(stem)+".gltf");put(path,json.data(),json.size());
    return path;
}

Model import_gltf(std::span<const uint8_t> bytes,std::string_view name,const Model& original,const std::function<std::vector<uint8_t>(std::string_view)>& read){
    const std::string resource(name);
    Reader reader{&read,{}};cgltf_options options{};options.file.read=read_file;options.file.release=release_file;options.file.user_data=&reader;
    cgltf_data* raw=nullptr;
    auto check=[&](cgltf_result r,std::string_view step){if(r!=cgltf_result_success)fail(resource+": "+std::string(step)+" failed (cgltf error "+std::to_string(int(r))+(reader.error.empty()?"":", "+reader.error)+')');};
    check(cgltf_parse(&options,bytes.data(),bytes.size(),&raw),"parse");
    std::unique_ptr<cgltf_data,void(*)(cgltf_data*)> data(raw,cgltf_free);
    check(cgltf_load_buffers(&options,data.get(),resource.c_str()),"loading buffers");
    check(cgltf_validate(data.get()),"validation");
    const size_t frames=original.frames.size();
    Model model;model.formatVersion=original.formatVersion;model.flags=original.flags;model.metadata=original.metadata;model.clockwise=original.clockwise;
    model.resourceMin=original.resourceMin;model.resourceMax=original.resourceMax;model.bones=original.bones;model.frameNames=original.frameNames;
    model.materials=original.materials;
    // Materials: "skinN" (N = 1..original skins) replaces skin N's bitmap, keeping it when untextured; others are appended skins.
    std::unordered_map<const cgltf_image*,std::shared_ptr<const Image>> decoded;
    auto image=[&](const cgltf_image* img)->std::shared_ptr<const Image>{
        if(auto it=decoded.find(img);it!=decoded.end())return it->second;
        std::vector<uint8_t> file;std::string kind=img->mime_type?img->mime_type:"";
        if(img->buffer_view){const auto* p=cgltf_buffer_view_data(img->buffer_view);if(!p)fail(resource+": image without data");file.assign(p,p+img->buffer_view->size);}
        else if(img->uri&&std::string_view(img->uri).starts_with("data:"))fail(resource+": data: URI images are not supported; use a file or GLB");
        else if(img->uri){std::string uri=img->uri;cgltf_decode_uri(uri.data());uri.resize(std::strlen(uri.c_str()));auto slash=resource.rfind('/');file=read((slash==std::string::npos?"":resource.substr(0,slash+1))+uri);kind=uri;}
        else fail(resource+": image without source");
        const bool jpeg=kind.ends_with("jpeg")||kind.ends_with(".jpg")||kind.ends_with(".JPG");
        auto result=std::make_shared<const Image>(decode_image(file,jpeg?"skin.jpg":"skin.png"));decoded.emplace(img,result);return result;
    };
    std::unordered_map<const cgltf_material*,uint32_t> slots;
    auto slot=[&](const cgltf_material* mat)->uint32_t{
        if(!mat)return 0;
        if(auto it=slots.find(mat);it!=slots.end())return it->second;
        const cgltf_texture* tex=mat->has_pbr_metallic_roughness?mat->pbr_metallic_roughness.base_color_texture.texture:nullptr;
        std::shared_ptr<const Image> texture=tex&&tex->image?image(tex->image):nullptr;
        uint32_t index=~0u;const std::string_view n=mat->name?mat->name:"";
        if(n.starts_with("skin")){uint32_t s=0;auto r=std::from_chars(n.data()+4,n.data()+n.size(),s);if(r.ec==std::errc{}&&r.ptr==n.data()+n.size()&&s>=1&&s<=original.materials.size())index=s-1;}
        if(index==~0u){
            Material added=original.materials.empty()?Material{}:original.materials[0];added.name=std::string(n);added.textureName.clear();added.raw.clear();
            if(!texture){Image white;white.width=white.height=1;white.rgba={255,255,255,255};texture=std::make_shared<const Image>(std::move(white));}
            index=uint32_t(model.materials.size());model.materials.push_back(std::move(added));
        }
        if(texture){model.materials[index].texture=texture;model.materials[index].textureName=n.empty()?resource:std::string(n);}
        slots.emplace(mat,index);return index;
    };
    // Every mesh node, world transform applied. Morph targets animate when every primitive has one per original frame.
    std::vector<Vec3> rest;std::vector<std::vector<Vec3>> targetFrames;std::vector<Vec2> uvs;std::vector<std::array<uint32_t,3>> outward;std::vector<uint32_t> triangleMaterials;
    bool useTargets=frames>0;size_t primitivesSeen=0;
    for(size_t ni=0;ni<data->nodes_count;++ni){
        const cgltf_node& node=data->nodes[ni];if(!node.mesh)continue;
        float world[16];cgltf_node_transform_world(&node,world);
        auto point=[&](const float* g,bool affine){const float w=affine?1:0;return from_gltf({world[0]*g[0]+world[4]*g[1]+world[8]*g[2]+world[12]*w,world[1]*g[0]+world[5]*g[1]+world[9]*g[2]+world[13]*w,world[2]*g[0]+world[6]*g[1]+world[10]*g[2]+world[14]*w});};
        for(size_t pi=0;pi<node.mesh->primitives_count;++pi){
            const cgltf_primitive& prim=node.mesh->primitives[pi];
            if(prim.type!=cgltf_primitive_type_triangles)fail(resource+": only triangle primitives are supported");
            const cgltf_accessor* position=nullptr;const cgltf_accessor* texcoord=nullptr;
            for(size_t a=0;a<prim.attributes_count;++a){const auto& at=prim.attributes[a];if(at.type==cgltf_attribute_type_position)position=at.data;else if(at.type==cgltf_attribute_type_texcoord&&at.index==0)texcoord=at.data;}
            if(!position)fail(resource+": primitive without POSITION");
            const size_t first=rest.size(),count=position->count;
            if(count>1000000||rest.size()+count>1000000)fail(resource+": more than 1000000 vertices");
            ++primitivesSeen;
            const uint32_t material=slot(prim.material);
            const Material* mat=model.materials.empty()?nullptr:&model.materials[material];
            const Vec2 scale=render_policy::model_uv_scale(model.formatVersion,mat?mat->uvScale:Vec2{1,1},mat?mat->texture.get():nullptr),offset=mat?mat->uvOffset:Vec2{};
            std::vector<float> p(count*3),t(count*2);cgltf_accessor_unpack_floats(position,p.data(),p.size());if(texcoord){if(texcoord->count!=count)fail(resource+": TEXCOORD_0 count");cgltf_accessor_unpack_floats(texcoord,t.data(),t.size());}
            for(size_t v=0;v<count;++v){rest.push_back(point(&p[v*3],true));uvs.push_back({(t[v*2]-offset.x)/scale.x,(t[v*2+1]-offset.y)/scale.y});}
            const size_t indexCount=prim.indices?prim.indices->count:count;if(indexCount%3)fail(resource+": index count is not a multiple of 3");
            for(size_t i=0;i<indexCount;i+=3){
                std::array<uint32_t,3> tri;for(size_t c=0;c<3;++c){const size_t x=prim.indices?cgltf_accessor_read_index(prim.indices,i+c):i+c;if(x>=count)fail(resource+": index out of range");tri[c]=uint32_t(first+x);}
                outward.push_back(tri);triangleMaterials.push_back(material);
            }
            const cgltf_accessor* targets[4096]{};size_t found=0;
            for(size_t ti=0;ti<prim.targets_count&&ti<4096;++ti)for(size_t a=0;a<prim.targets[ti].attributes_count;++a)if(prim.targets[ti].attributes[a].type==cgltf_attribute_type_position){targets[ti]=prim.targets[ti].attributes[a].data;++found;}
            if(prim.targets_count!=frames||found!=frames)useTargets=false;
            if(!useTargets)continue;
            if(count*frames>64'000'000)fail(resource+": morph targets exceed 64M vertex frames");
            targetFrames.resize(frames);std::vector<float> d(count*3);
            for(size_t f=0;f<frames;++f){
                if(targets[f]->count!=count)fail(resource+": morph target count");
                cgltf_accessor_unpack_floats(targets[f],d.data(),d.size());
                for(size_t v=0;v<count;++v){const float g[3]{p[v*3]+d[v*3],p[v*3+1]+d[v*3+1],p[v*3+2]+d[v*3+2]};targetFrames[f].push_back(point(g,true));}
            }
        }
    }
    if(!primitivesSeen||outward.empty())fail(resource+": no triangle mesh");
    if(outward.size()>20000)std::cerr<<"kabus22: "<<resource<<": "<<outward.size()<<" triangles (above 20000; the originals have at most a few thousand)\n";
    if(!useTargets)targetFrames.clear();
    // Weld render vertices into source vertices: equal rest position (and, with targets, equal in every frame).
    const size_t n=rest.size();std::vector<uint32_t> source(n);std::vector<uint32_t> representative;
    {
        std::unordered_map<uint64_t,std::vector<uint32_t>> buckets;
        for(uint32_t v=0;v<n;++v){
            const uint64_t key=(uint64_t(std::bit_cast<uint32_t>(rest[v].x))*0x9e3779b97f4a7c15ull)^(uint64_t(std::bit_cast<uint32_t>(rest[v].y))<<21)^std::bit_cast<uint32_t>(rest[v].z);
            auto& bucket=buckets[key];uint32_t match=~0u;
            for(auto s:bucket){const auto r=representative[s];
                bool same=rest[r].x==rest[v].x&&rest[r].y==rest[v].y&&rest[r].z==rest[v].z;
                for(size_t f=0;same&&f<targetFrames.size();++f){const auto a=targetFrames[f][r],b=targetFrames[f][v];same=a.x==b.x&&a.y==b.y&&a.z==b.z;}
                if(same){match=s;break;}}
            if(match==~0u){match=uint32_t(representative.size());representative.push_back(v);bucket.push_back(match);}
            source[v]=match;
        }
    }
    const size_t sources=representative.size();
    std::vector<std::array<uint32_t,3>> sourceOutward;sourceOutward.reserve(outward.size());
    for(const auto& t:outward)sourceOutward.push_back({source[t[0]],source[t[1]],source[t[2]]});
    std::vector<Vec3> sourceRest(sources);for(size_t s=0;s<sources;++s)sourceRest[s]=rest[representative[s]];
    std::vector<std::vector<Vec3>> sourceFrames;
    if(useTargets){sourceFrames.assign(frames,std::vector<Vec3>(sources));for(size_t f=0;f<frames;++f)for(size_t s=0;s<sources;++s)sourceFrames[f][s]=targetFrames[f][representative[s]];}
    else if(frames){
        if(original.sourceTriangles.empty()||original.frames[0].sourcePositions.size()!=original.sourceVertices.size())fail(resource+": original model has no source mesh to follow");
        sourceFrames=wrap_deform(original,sourceRest,smooth_normals(sourceRest,sourceOutward));
    }
    for(uint32_t t=0;t<outward.size();++t){
        Triangle tri;tri.indices=outward[t];tri.materialIndex=triangleMaterials[t];model.triangles.push_back(tri);
        model.sourceTriangles.push_back(sourceOutward[t]);
    }
    model.vertexSources=source;
    model.frames.resize(frames);
    for(size_t f=0;f<frames;++f){
        auto& out=model.frames[f];out.name=original.frames[f].name;out.boneTransforms=original.frames[f].boneTransforms;
        out.sourcePositions=std::move(sourceFrames[f]);out.sourceNormals=smooth_normals(out.sourcePositions,sourceOutward);
        out.positions.resize(n);out.normals.resize(n);for(size_t v=0;v<n;++v){out.positions[v]=out.sourcePositions[source[v]];out.normals[v]=out.sourceNormals[source[v]];}
    }
    const auto restNormals=frames?model.frames[0].sourceNormals:smooth_normals(sourceRest,sourceOutward);
    model.sourceVertices.resize(sources);
    for(size_t s=0;s<sources;++s){model.sourceVertices[s].position=frames?model.frames[0].sourcePositions[s]:sourceRest[s];model.sourceVertices[s].normal=restNormals[s];}
    model.vertices.resize(n);
    for(size_t v=0;v<n;++v){model.vertices[v].position=model.sourceVertices[source[v]].position;model.vertices[v].normal=restNormals[source[v]];model.vertices[v].uv=uvs[v];}
    model.min=model.max=model.sourceVertices.front().position;
    auto bounds=[&](Vec3 p){model.min={std::min(model.min.x,p.x),std::min(model.min.y,p.y),std::min(model.min.z,p.z)};model.max={std::max(model.max.x,p.x),std::max(model.max.y,p.y),std::max(model.max.z,p.z)};};
    for(const auto& v:model.sourceVertices)bounds(v.position);
    for(const auto& f:model.frames)for(auto p:f.sourcePositions)bounds(p);
    return model;
}
}
