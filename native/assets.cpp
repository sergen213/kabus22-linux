/*
Format descriptions and normal table adapted from libgamestudio:
Copyright (c) 2022 Felix "xq" Queißner
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "assets.hpp"
#include "media.hpp"
#include "model_gltf.hpp"
#include "pe_font.hpp"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <utility>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace k22 {
bool model_has_entity_skins(const Model& model) noexcept {
    return model.formatVersion==4||model.formatVersion==5||model.formatVersion==7||model.formatVersion==105;
}
uint32_t model_skin_index(const Model& model,int32_t selector) {
    if(model.materials.empty())throw std::runtime_error("model has no skin bank");
    return uint32_t(std::clamp<int64_t>(int64_t(selector)-1,0,int64_t(model.materials.size())-1));
}
const Material* model_material(const Model& model,uint32_t faceMaterial,uint32_t skinIndex) {
    if(model.materials.empty())return nullptr;
    return &model.materials.at(model_has_entity_skins(model)?skinIndex:faceMaterial);
}
namespace {
constexpr size_t assetLimit = 128ULL*1024*1024;
constexpr size_t fontPeLimit = 64ULL*1024*1024;
constexpr size_t cacheLimit = 1024ULL*1024*1024;
[[noreturn]] void malformed(std::string_view message) { throw std::runtime_error("asset: " + std::string(message)); }
struct ResourceReadFailure : std::runtime_error { using std::runtime_error::runtime_error; };
size_t checked_product(size_t a,size_t b) {
    if (b && a>assetLimit/b) malformed("size exceeds asset limit");
    return a*b;
}
struct Cursor {
    std::span<const uint8_t> b; size_t p{};
    size_t remaining() const { return b.size()-p; }
    std::span<const uint8_t> take(size_t n) {
        if(n>remaining()) malformed("truncated binary section");
        auto result=b.subspan(p,n);p+=n;return result;
    }
    void seek(size_t n) { if(n>b.size()) malformed("section offset outside file"); p=n; }
    void skip(size_t n) { (void)take(n); }
    uint8_t u8() { return take(1)[0]; }
    uint16_t u16() { auto s=take(2);return uint16_t(s[0])|(uint16_t(s[1])<<8); }
    uint32_t u32() { auto s=take(4);return uint32_t(s[0])|(uint32_t(s[1])<<8)|(uint32_t(s[2])<<16)|(uint32_t(s[3])<<24); }
    int32_t i32() { return std::bit_cast<int32_t>(u32()); }
    float f32() { float v=std::bit_cast<float>(u32());if(!std::isfinite(v)) malformed("non-finite numeric data");return v; }
    Vec3 vec3() { return {f32(),f32(),f32()}; }
    std::string str(size_t n) { auto s=take(n);auto z=std::find(s.begin(),s.end(),0);return {reinterpret_cast<const char*>(s.data()),size_t(z-s.begin())}; }
};
std::string key(std::string_view value) {
    std::string result;result.reserve(value.size());
    for(unsigned char c:value) {
        const char normalized=c=='\\' ? '/' : c>='A'&&c<='Z' ? char(c+32):char(c);
        if(normalized=='/'&&!result.empty()&&result.back()=='/')continue;
        result.push_back(normalized);
    }
    return result;
}
std::string safe_name(std::string_view value) {
    std::string name=key(value);
    // Ordinary Windows DOS file paths discard terminal periods and U+0020 spaces.
    while(!name.empty()&&(name.back()=='.'||name.back()==' '))name.pop_back();
    if(name.empty()||name.front()=='/') malformed("absolute or empty resource path");
    size_t pos=0;
    while(pos<name.size()) {
        auto end=name.find('/',pos);if(end==std::string::npos)end=name.size();
        auto part=std::string_view(name).substr(pos,end-pos);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ') malformed("unsafe resource path");
        for(unsigned char c:part) if(c<32||c==127||std::string_view(":<>\"|?*").find(char(c))!=std::string_view::npos) malformed("unsafe resource path character");
        pos=end+1;
    }
    if(name.back()=='/') malformed("empty resource path component");
    return name;
}
struct File {
    int fd{-1};
    File(const std::filesystem::path& root,const std::filesystem::path& path,bool stable=false) {
        auto relative=path.lexically_relative(root); if(relative.empty()||relative.is_absolute()) malformed("resource outside root");
        int parent=::open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(parent<0) {
            if(errno==ENOENT||errno==EACCES)throw ResourceReadFailure("asset: cannot open data root");
            malformed("cannot open data root");
        }
        for(auto it=relative.begin();it!=relative.end();++it) {
            if(*it==".."||*it=="."){::close(parent);malformed("resource outside root");}
            auto next=it;++next;
            int child=::openat(parent,it->c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|(next==relative.end()?(stable?O_NONBLOCK:0):O_DIRECTORY));
            if(child<0) {
                const int error=errno;::close(parent);
                if(error==ENOENT||error==EACCES)throw ResourceReadFailure("asset: cannot open resource: "+path.string());
                malformed("cannot safely open resource: "+path.string());
            }
            ::close(parent);parent=child;
        }
        fd=parent;
    }
    ~File(){if(fd>=0)::close(fd);}
    File(const File&)=delete;File&operator=(const File&)=delete;
    std::vector<uint8_t> bytes(uint64_t off,size_t size) const {
        if(size>assetLimit) malformed("resource exceeds asset limit");
        std::vector<uint8_t> result(size);size_t done=0;
        while(done<size) { auto n=::pread(fd,result.data()+done,size-done,off+done);if(n<=0)throw ResourceReadFailure("asset: truncated resource read");done+=size_t(n); }
        return result;
    }
    std::vector<uint8_t> stable_bytes(size_t limit) const {
        struct stat before{},after{};
        if(::fstat(fd,&before)<0||!S_ISREG(before.st_mode)||before.st_size<0||uint64_t(before.st_size)>limit)
            malformed("font source must be a bounded regular file");
        auto result=bytes(0,size_t(before.st_size));
        if(::fstat(fd,&after)<0||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||
           before.st_size!=after.st_size||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||
           before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||before.st_ctim.tv_sec!=after.st_ctim.tv_sec||
           before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)
            malformed("font source changed while reading");
        return result;
    }
};
uint32_t big32(std::span<const uint8_t> s) {return(uint32_t(s[0])<<24)|(uint32_t(s[1])<<16)|(uint32_t(s[2])<<8)|s[3];}
void charge(size_t& cache,size_t amount) {if(amount>cacheLimit-cache)malformed("decoded asset cache exceeds 1 GiB; release Assets before loading another game");cache+=amount;}
void source_render_links(Model& model) {
    if(model.vertexSources.size()!=model.vertices.size())malformed("inconsistent source/render vertex correspondence");
    checked_product(model.sourceVertices.size()+1,sizeof(uint32_t));
    model.sourceRenderOffsets.assign(model.sourceVertices.size()+1,0);
    for(auto source:model.vertexSources){if(source>=model.sourceVertices.size())malformed("source vertex outside original mesh");++model.sourceRenderOffsets[source+1];}
    for(size_t i=1;i<model.sourceRenderOffsets.size();++i)model.sourceRenderOffsets[i]+=model.sourceRenderOffsets[i-1];
    model.sourceRenderIndices.resize(model.vertices.size());
    auto cursor=model.sourceRenderOffsets;
    for(uint32_t i=0;i<model.vertexSources.size();++i)model.sourceRenderIndices[cursor[model.vertexSources[i]]++]=i;
}
size_t model_bytes(const Model& m) {
    size_t size=m.vertices.size()*sizeof(Vertex)+m.sourceVertices.size()*sizeof(Vertex)+m.sourceTriangles.size()*sizeof(std::array<uint32_t,3>)+m.vertexSources.size()*sizeof(uint32_t)+m.triangles.size()*sizeof(Triangle)+m.metadata.size()+m.weights.size()*sizeof(VertexWeight)+m.bones.size()*sizeof(Bone);
    size+=(m.sourceRenderOffsets.size()+m.sourceRenderIndices.size())*sizeof(uint32_t);
    for(auto&f:m.frames)size+=(f.positions.size()+f.normals.size()+f.sourcePositions.size()+f.sourceNormals.size())*sizeof(Vec3)+f.boneTransforms.size()*sizeof(std::array<float,12>);
    for(auto&x:m.materials)size+=x.raw.size();return size;
}
size_t world_bytes(const World& value) {
    size_t size=model_bytes(value.mesh)+value.collision.vertices.size()*sizeof(Vec3)+value.collision.triangles.size()*sizeof(std::array<uint32_t,3>)+value.collision.flags.size()*sizeof(uint32_t);
    size+=value.planes.size()*sizeof(WorldPlane)+value.faces.size()*sizeof(WorldFace)+value.bspNodes.size()*sizeof(WorldBspNode)+value.bspLeaves.size()*sizeof(WorldBspLeaf)+value.hulls.size()*sizeof(WorldHull)+value.bspBlocks.size()*sizeof(uint32_t)+value.lights.size()*sizeof(Light)+value.metadata.size();
    size+=value.legacyPointNodes.size()*sizeof(WorldHull);
    for(const auto&s:value.sections)size+=s.size();
    for(const auto&e:value.entities)size+=sizeof(WorldEntity)+e.metadata.size();
    for(const auto&p:value.paths){size+=sizeof(Path)+p.metadata.size()+p.nodes.size()*sizeof(PathNode)+p.edgeInfo.size()*sizeof(PathEdgeInfo)+p.edges.size()*sizeof(std::array<uint32_t,2>);for(const auto&n:p.nodes)size+=n.metadata.size()+n.edges.size()*sizeof(uint32_t);}
    return size;
}
size_t image_cache_bytes(const std::unordered_map<std::string,std::shared_ptr<const Image>>& images) {
    size_t total=0;for(const auto&[name,image]:images)total+=image->rgba.size();return total;
}

int32_t native_fp_trunc(long double value) {
    value=std::trunc(value);
    if(!std::isfinite(value)||value<std::numeric_limits<int32_t>::min()||value>std::numeric_limits<int32_t>::max())
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(value);
}
void native_lightmap_ramp(std::array<uint8_t,256>& ramp,float gamma,bool inverted) {
    // 00455180: strict identity bounds; every FSTP below rounds to float32.
    if(0x1.fd70a3d70a3d7p-1<double(gamma)&&double(gamma)<0x1.0147ae147ae14p+0) {
        for(size_t i=0;i<ramp.size();++i)ramp[i]=uint8_t(inverted?255-i:i);
        return;
    }
    volatile float exponent=static_cast<float>(1.0L/static_cast<long double>(gamma));
    volatile float x=std::bit_cast<float>(uint32_t{0x3a83126f});
    for(size_t i=0;i<ramp.size();++i) {
        if(i)x=static_cast<float>(static_cast<long double>(x)+0x1.0101010101010p-8);
        volatile float p=static_cast<float>(std::pow(double(x),double(exponent)));
        int32_t channel=std::clamp(native_fp_trunc(static_cast<long double>(p)*255.0L),int32_t{0},int32_t{255});
        ramp[i]=uint8_t(inverted?255-channel:channel);
    }
}
}

Assets::Assets(std::filesystem::path root,bool bonusProfile):root_(std::filesystem::canonical(root)),bonusProfile_(bonusProfile) {
    if(!std::filesystem::is_directory(root_))malformed("data root is not a directory");
    for(auto& entry:std::filesystem::recursive_directory_iterator(root_)) {
        if(entry.is_symlink())continue;
        if(entry.is_regular_file()) {
            auto name=safe_name(entry.path().lexically_relative(root_).generic_string());
            if(!loose_.emplace(name,entry.path()).second)malformed("case-insensitive loose resource collision: "+name);
        }
    }
}
void Assets::initialize_native_lightmap_tables(int32_t gammaRaw,bool monochrome,bool stageCombinationCapable) {
    if(nativeLightmapTablesInitialized_)return;
    // 004555e0 captures startup settings once; 00455240 uses fixed mono gamma.
    volatile float raw=static_cast<float>(gammaRaw);
    const long double q=static_cast<long double>(raw)/0x1p+10;
    volatile float gamma=static_cast<float>(stageCombinationCapable?q/0x1.8p+0:(q*0x1.8cccccccccccdp+0)/0x1.8p+0);
    native_lightmap_ramp(nativeLightmapRamp_,gamma,!stageCombinationCapable);
    native_lightmap_ramp(nativeMonoRamp_,std::bit_cast<float>(uint32_t{0x3f2aaaab}),false);
    nativeLightmapMonochrome_=monochrome;
    nativeLightmapTablesInitialized_=true;
}
uint16_t Assets::native_lightmap_color(uint16_t word) const {
    if(!nativeLightmapTablesInitialized_)throw std::logic_error("native lightmap tables are not initialized");
    // Recovered double coefficients, not conventional 255/31 and 255/63.
    constexpr double redBlue=0x1.07543b7543b75p+3,green=0x1.0326326326326p+2;
    const int32_t ir=static_cast<int32_t>(static_cast<long double>(float((word>>11)&31))*redBlue);
    const int32_t ig=static_cast<int32_t>(static_cast<long double>(float((word>>5)&63))*green);
    const int32_t ib=static_cast<int32_t>(static_cast<long double>(float(word&31))*redBlue);
    uint32_t r,g,b;
    if(nativeLightmapMonochrome_) {
        const int32_t index=static_cast<int32_t>(static_cast<long double>(float(ir+ig+ib))*0x1.51eb851eb851fp-2);
        r=g=b=nativeLightmapRamp_[index];
    } else {
        r=nativeLightmapRamp_[ir];g=nativeLightmapRamp_[ig];b=nativeLightmapRamp_[ib];
    }
    return uint16_t(((r&248)<<8)|((g&252)<<3)|(b>>3));
}
uint8_t Assets::native_lightmap_mono(uint16_t word) const {
    if(!nativeLightmapTablesInitialized_)throw std::logic_error("native lightmap tables are not initialized");
    const int32_t sum=2*((word>>11)&31)+((word>>5)&63)+2*(word&31);
    const int32_t index=-static_cast<int32_t>(static_cast<long double>(float(sum))*-0x1.5d3a519768dd4p+0);
    return nativeMonoRamp_[index];
}
bool Assets::contains(std::string_view name) const {
    auto normalized=safe_name(name);return mods_.contains(normalized)||loose_.contains(normalized)||entries_.contains(normalized);
}
std::vector<std::string> Assets::names() const {
    std::vector<std::string> result;result.reserve(mods_.size()+loose_.size()+entries_.size());
    for(const auto&[name,file]:mods_)result.push_back(name);
    for(const auto&[name,path]:loose_)if(!mods_.contains(name))result.push_back(name);
    for(const auto&[name,entry]:entries_)if(!mods_.contains(name)&&!loose_.contains(name))result.push_back(name);
    std::sort(result.begin(),result.end());return result;
}
size_t Assets::add_mod(const std::filesystem::path& dataDir) {
    auto root=std::filesystem::canonical(dataDir);std::unordered_map<std::string,std::filesystem::path> files;
    for(auto& entry:std::filesystem::recursive_directory_iterator(root)) {
        if(entry.is_symlink()||!entry.is_regular_file())continue;
        auto name=safe_name(entry.path().lexically_relative(root).generic_string());
        if(!files.emplace(name,entry.path()).second)malformed("case-insensitive mod resource collision: "+name);
    }
    // <stem>.gltf/.glb replaces <stem>.mdl; a mod shipping both (or both glTF forms) is ambiguous.
    auto replaced=[](const std::string& name)->std::string{for(std::string_view ext:{".gltf",".glb"})if(name.ends_with(ext))return name.substr(0,name.size()-ext.size())+".mdl";return {};};
    std::unordered_map<std::string,std::string> models;
    for(const auto&[name,path]:files)if(auto mdl=replaced(name);!mdl.empty()){
        if(files.contains(mdl))malformed("mod has both "+mdl+" and its glTF replacement "+name);
        if(!models.emplace(mdl,name).second)malformed("mod has two glTF replacements for "+mdl);
    }
    for(const auto&[mdl,name]:models)mods_.try_emplace(mdl,root,files.at(name));
    for(auto&[name,path]:files)mods_.try_emplace(name,root,std::move(path));
    return files.size();
}
const std::pair<std::filesystem::path,std::filesystem::path>* Assets::mod_file(std::string_view name) const {
    auto it=mods_.find(safe_name(name));return it==mods_.end()?nullptr:&it->second;
}
std::filesystem::path Assets::resolve(std::string_view name) const {
    auto normalized=safe_name(name);auto it=loose_.find(normalized);
    if(it==loose_.end())malformed("missing loose resource: "+std::string(name));return it->second;
}
void Assets::mount(std::string_view resource) {
    auto path=resolve(resource);auto mountKey=path.generic_string();
    if(mounted_.contains(mountKey))return;
    File file(root_,path);auto total=std::filesystem::file_size(path);
    auto signature=file.bytes(0,4);auto version=std::string_view(reinterpret_cast<const char*>(signature.data()),4);
    if(version!="WRS2"&&version!="WRS3")malformed("resource archive is not WRS2 or WRS3");
    const uint8_t nameKey=version=="WRS3"?0xb5:0xa5;
    uint64_t position=4;std::unordered_map<std::string,Entry> added;
    while(position<total) {
        if(total-position<40)malformed("truncated WRS header");auto h=file.bytes(position,40);position+=40;
        size_t length=0;while(length<32&&h[length])++length;
        for(size_t i=length;i<32;++i)if(h[i])malformed("nonzero WRS filename padding");
        std::string decoded;for(size_t i=0;i<length;++i)decoded.push_back(char(h[i]^nameKey));auto name=safe_name(decoded);
        uint32_t packed=big32(std::span(h).subspan(32,4)),size=big32(std::span(h).subspan(36,4));
        if(packed>total-position||packed>assetLimit||size>assetLimit||bool(packed)!=bool(size))malformed("invalid WRS member sizes");
        if(!added.emplace(name,Entry{path,position,packed,size}).second)malformed("duplicate WRS resource: "+name);
        position+=packed;
    }
    for(auto&[name,entry]:added)for(size_t at=name.find('/');at!=std::string::npos;at=name.find('/',at+1))if(added.contains(name.substr(0,at)))malformed("WRS file/directory conflict");
    // A6 0041fd20 / A7 10024860 append members; 00420120 / 10024f60 return the first match.
    for(auto&[name,entry]:added)entries_.try_emplace(name,std::move(entry));
    mounted_.insert(std::move(mountKey));
}
std::vector<uint8_t> Assets::decompress(std::span<const uint8_t> bytes,size_t expected) {
    if(expected>assetLimit||bytes.size()>assetLimit)malformed("LZSS size exceeds limit");
    std::array<uint8_t,4096> window;window.fill(' ');size_t cursor=4078,consumed=0;
    std::vector<uint8_t> result;result.reserve(expected);
    while(consumed<bytes.size()) {
        uint8_t flags=bytes[consumed++];if(consumed==bytes.size())malformed("LZSS flag without token");
        for(unsigned bit=0;bit<8&&consumed<bytes.size();++bit) {
            size_t start=0,length=1;uint8_t literal=0;bool direct=(flags&(1u<<bit))!=0;
            if(direct)literal=bytes[consumed++];else {
                if(bytes.size()-consumed<2)malformed("truncated LZSS reference");
                uint8_t lo=bytes[consumed++],hi=bytes[consumed++];start=lo|((hi&0xf0)<<4);length=(hi&15)+3;
            }
            if(length>expected-result.size())malformed("LZSS token exceeds declared output");
            for(size_t i=0;i<length;++i){uint8_t v=direct?literal:window[(start+i)&4095];result.push_back(v);window[cursor]=v;cursor=(cursor+1)&4095;}
        }
    }
    if(result.size()!=expected)malformed("LZSS output size mismatch");return result;
}
std::vector<uint8_t> Assets::read(std::string_view name) const {
    auto normalized=safe_name(name);
    if(auto it=mods_.find(normalized);it!=mods_.end()) {
        File file(it->second.first,it->second.second);return file.bytes(0,std::filesystem::file_size(it->second.second));
    }
    return read_original(normalized);
}
std::vector<uint8_t> Assets::read_original(std::string_view name) const {
    auto normalized=safe_name(name);
    if(auto it=loose_.find(normalized);it!=loose_.end()) {
        File file(root_,it->second);return file.bytes(0,std::filesystem::file_size(it->second));
    }
    auto it=entries_.find(normalized);if(it==entries_.end())malformed("missing resource: "+std::string(name));
    return read_entry(it->second);
}
std::string Assets::text(std::string_view name) const {auto data=read(name);return {reinterpret_cast<const char*>(data.data()),data.size()};}
std::vector<uint8_t> Assets::read_entry(const Entry& entry) const {
    File file(root_,entry.archive);auto packed=file.bytes(entry.offset,entry.packed);return decompress(packed,entry.size);
}
std::string Assets::archive_text(std::string_view name) const {
    auto normalized=safe_name(name);
    if(!bonusProfile_&&mods_.contains(normalized))return text(normalized); // campaign scripts are moddable; Bonus stays archive-only
    auto it=entries_.find(normalized);if(it==entries_.end())malformed("missing archived resource: "+std::string(name));
    auto data=read_entry(it->second);return {reinterpret_cast<const char*>(data.data()),data.size()};
}
std::shared_ptr<const Image> Assets::image_ptr(std::string_view name) {
    auto normalized=safe_name(name);if(auto it=images_.find(normalized);it!=images_.end())return it->second;
    std::vector<uint8_t> bytes;
    if(normalized=="engine/_a4font.pcx") {
        if(mods_.contains(normalized)) {bytes=read(normalized);validate_engine_font(bytes);}
        else if(auto it=loose_.find(normalized);it!=loose_.end()) {
            File file(root_,it->second,true);bytes=file.stable_bytes(fontPeLimit);
            validate_engine_font(bytes);
        } else if(entries_.contains(normalized)) {
            bytes=read(normalized);validate_engine_font(bytes);
        }
        else {
            for(auto executable:{"k22.exe","k22_4gb.exe"}) {
                auto it=loose_.find(executable);if(it==loose_.end())continue;
                File file(root_,it->second,true);auto pe=file.stable_bytes(fontPeLimit);
                bytes=extract_engine_font(pe);break;
            }
            if(bytes.empty())malformed("missing engine/_a4font.pcx and original user K22.exe/K22_4GB.exe");
        }
    } else bytes=read(normalized);
    auto decoded=decode_image(bytes,normalized);
    // 10012470 retains returned alpha100 but excludes runtime80000; these
    // file-created descriptors therefore do not invoke the uploader's skip bit.
    decoded.nativeTextureAlpha=bonusProfile_ && decoded.nativeTextureAlpha;
    decoded.a6AlphaFormat=!bonusProfile_&&normalized.ends_with(".tga")&&decoded.sourceDepth==32;
    if(normalized.ends_with(".dds"))decoded.nativeStorageBytes=0;
    else if(normalized.ends_with(".bmp")||normalized.ends_with(".pcx")) {
        if(decoded.sourceDepth==8)decoded.nativeStorageBytes=1;
        else if(decoded.sourceDepth==24)decoded.nativeStorageBytes=2;
    }
    else if(normalized.ends_with(".tga")&&(decoded.sourceDepth==24||decoded.sourceDepth==32))
        decoded.nativeStorageBytes=nativeAlphaDepth_<=16?uint8_t(2):uint8_t(decoded.sourceDepth/8);
    charge(cachedBytes_,decoded.rgba.size());
    auto result=std::make_shared<const Image>(std::move(decoded));images_.emplace(normalized,result);return result;
}
const Image& Assets::image(std::string_view name){return *image_ptr(name);}
const Model* Assets::find_model(std::string_view name) const {auto it=models_.find(safe_name(name));return it==models_.end()?nullptr:&it->second;}
const Model& Assets::model(std::string_view name) {return *load_model(name,true);}
const Model* Assets::load_model(std::string_view name,bool initializeShadowResources) {
    auto normalized=safe_name(name);if(auto it=models_.find(normalized);it!=models_.end())return &it->second;
    std::vector<uint8_t> bytes;
    // Direct companion read failures are source nulls; decoder/cache/security refusals remain errors.
    try {bytes=read(name);}
    catch(const ResourceReadFailure&) {if(!initializeShadowResources)return nullptr;throw;}
    catch(const std::filesystem::filesystem_error&) {if(!initializeShadowResources)return nullptr;throw;}
    const auto before=cachedBytes_,imageBefore=image_cache_bytes(images_);
    Model* loaded;
    try {
        Model value;
        const auto* mod=mods_.contains(normalized)?&mods_.at(normalized):nullptr;
        if(auto ext=mod?key(mod->second.extension().string()):std::string{};ext==".gltf"||ext==".glb") {
            // Mod glTF replacement (model_gltf.hpp): animated from the game's own model of that name.
            const auto resource=safe_name(mod->second.lexically_relative(mod->first).generic_string());
            if(!loose_.contains(normalized)&&!entries_.contains(normalized))malformed(resource+" replaces no original model "+normalized);
            value=import_gltf(bytes,resource,decode_model(read_original(normalized),name),[this](std::string_view file){return read(file);});
        } else value=decode_model(bytes,name);
        source_render_links(value);
        charge(cachedBytes_,model_bytes(value));loaded=&models_.emplace(normalized,std::move(value)).first->second;
    } catch(...) {cachedBytes_=before+image_cache_bytes(images_)-imageBefore;throw;}
    // A6 004561c0 and A7 10059dc0 birth three actual self aliases.
    loaded->shadowLods.fill(loaded);
    try {
        const auto suffix=normalized.find("_0.");
        if(initializeShadowResources&&suffix!=std::string::npos) {
            // A6 0045b810 / A7 1005ee90 use direct loads and identical conditional aliases.
            // Supplied Bonus retains capability 0x400 in every accepted edition row:
            // reverse/decompiled/runtime/planar-shadow-source/a7/edition-capability/acknex.dll.10017070.c
            auto companion=normalized;
            for(size_t i=0;i<loaded->shadowLods.size();++i) {
                companion[suffix+1]=char('1'+i);
                loaded->shadowLods[i]=contains(companion)?load_model(companion,false):nullptr;
            }
            if(!loaded->shadowLods[0]&&(loaded->shadowLods[1]||loaded->shadowLods[2]))
                loaded->shadowLods[0]=loaded;
            if(!loaded->shadowLods[1]&&loaded->shadowLods[2])
                loaded->shadowLods[1]=loaded->shadowLods[0];
        }
    } catch(...) {purge_model(normalized);throw;}
    return loaded;
}
void Assets::purge_image(std::string_view name) {
    auto normalized=safe_name(name);
    // The user's original engine font is decoded once for this Assets lifetime.
    if(normalized=="engine/_a4font.pcx")return;
    auto it=images_.find(normalized);
    if(it==images_.end())return;
    if(it->second.use_count()==1)cachedBytes_-=it->second->rgba.size();
    images_.erase(it);
}
bool Assets::purge_model(std::string_view name) {
    const auto it=models_.find(safe_name(name));if(it==models_.end())return false;
    for(auto&[key,value]:models_)
        for(auto& slot:value.shadowLods)if(slot==&it->second)slot=nullptr;
    models_.erase(it);
    size_t footprint=0;std::unordered_set<const Image*> seen;
    auto image=[&](const std::shared_ptr<const Image>& value){if(value&&seen.insert(value.get()).second)footprint+=value->rgba.size();};
    auto modelImages=[&](const Model& model){for(const auto&m:model.materials){image(m.texture);image(m.lightmap);}};
    for(const auto&[key,value]:images_)image(value);
    for(const auto&[key,value]:models_){footprint+=model_bytes(value);modelImages(value);}
    for(const auto&[key,value]:worlds_){footprint+=world_bytes(value);modelImages(value.mesh);for(const auto&lm:value.lightmaps)image(lm);}
    cachedBytes_=footprint;return true;
}
void Assets::release_level(std::string_view keepWorld,std::span<const Model* const> keepModels) {
    const auto keep=keepWorld.empty()?std::string{}:safe_name(keepWorld);
    std::erase_if(worlds_,[&](const auto& entry){return entry.first!=keep;});
    // Persistent entities' models are immutable decodes: re-decoding them on every level load cost ~85 ms (node map: addresses stay valid).
    std::unordered_set<const Model*> kept;
    for(const Model* m:keepModels)if(m&&kept.insert(m).second)for(const Model* lod:m->shadowLods)if(lod)kept.insert(lod);
    std::erase_if(models_,[&](const auto& entry){return !kept.contains(&entry.second);});
    cachedBytes_=0;
    for(const auto&[name,image]:images_)cachedBytes_+=image->rgba.size();
    for(const auto&[name,value]:models_)cachedBytes_+=model_bytes(value);
    for(const auto&[name,value]:worlds_)cachedBytes_+=world_bytes(value);
    // The kept world's prefetched entity models join the cache (node moves keep shadow LOD addresses); a name already
    // cached keeps its decode and adopted LODs that pointed at the duplicate are redirected to it.
    if(auto it=prefetchedModels_.find(keep);it!=prefetchedModels_.end()) {
        auto& pending=it->second;std::unordered_map<const Model*,const Model*> remap;std::vector<Model*> adopted;
        for(auto entry=pending.begin();entry!=pending.end();) {
            auto current=entry++;
            if(auto existing=models_.find(current->first);existing!=models_.end()){remap[&current->second]=&existing->second;continue;}
            Model* m=&current->second;models_.insert(pending.extract(current));adopted.push_back(m);cachedBytes_+=model_bytes(*m);
        }
        for(Model* m:adopted)for(auto& lod:m->shadowLods)if(auto r=remap.find(lod);r!=remap.end())lod=r->second;
    }
    prefetchedModels_.clear();
    // Finished prefetches are dropped; running ones complete in the background and are adopted or dropped later.
    std::erase_if(prefetches_,[&](const auto& entry){return entry.first!=keep&&entry.second.wait_for(std::chrono::seconds(0))==std::future_status::ready;});
}
const World& Assets::world(std::string_view name) {
    auto normalized=safe_name(name);if(auto it=worlds_.find(normalized);it!=worlds_.end())return it->second;
    if(auto it=prefetches_.find(normalized);it!=prefetches_.end()) {
        auto future=std::move(it->second);prefetches_.erase(it);auto& done=*future.get();World value=std::move(done.world);
        // Entity models/images decoded under other alpha-depth settings are not used (the main thread decodes them again).
        if(done.alphaDepth==nativeAlphaDepth_) {
            for(auto& [key,image]:done.images)if(images_.emplace(key,image).second)charge(cachedBytes_,image->rgba.size());
            prefetchedModels_[normalized]=std::move(done.models);
        }
        charge(cachedBytes_,world_bytes(value));auto&stored=worlds_.emplace(normalized,std::move(value)).first->second;stored.mesh.nativeWorld=&stored;return stored;
    }
    const auto before=cachedBytes_,imageBefore=image_cache_bytes(images_);
    try {
        auto bytes=read(name);auto value=decode_world(bytes,name);source_render_links(value.mesh);
        charge(cachedBytes_,world_bytes(value));auto&stored=worlds_.emplace(normalized,std::move(value)).first->second;stored.mesh.nativeWorld=&stored;return stored;
    } catch(...) {cachedBytes_=before+image_cache_bytes(images_)-imageBefore;throw;}
}
bool Assets::prefetch_world(std::string_view name) {
    auto normalized=safe_name(name);if(worlds_.contains(normalized))return true;
    if(auto it=prefetches_.find(normalized);it!=prefetches_.end()) {
        if(it->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
        world(name);return true;
    }
    // The worker owns a snapshot without the decoded levels (node-based maps: moving them keeps World addresses stable).
    auto worlds=std::move(worlds_);auto models=std::move(models_);auto prefetches=std::move(prefetches_);auto pending=std::move(prefetchedModels_);worlds_.clear();models_.clear();prefetches_.clear();prefetchedModels_.clear();
    auto worker=std::make_shared<Assets>(*this);
    worlds_=std::move(worlds);models_=std::move(models);prefetches_=std::move(prefetches);prefetchedModels_=std::move(pending);
    prefetches_.emplace(normalized,std::async(std::launch::async,[worker,file=std::string(name)]{
        auto bytes=worker->read(file);auto value=std::make_shared<Prefetched>();value->world=worker->decode_world(bytes,file);source_render_links(value->world.mesh);
        // The level's placed entity models and sprite images too (load_entity_asset), so the swap does not decode them; a failure is
        // left to the real load on the main thread, which reports it.
        const auto known=worker->images_;
        for(const auto& e:value->world.entities) {
            if(e.type==4||e.file.empty())continue;auto dot=e.file.find_last_of('.');if(dot==std::string::npos)continue;
            std::string ext=e.file.substr(dot);for(auto& c:ext)c=char(std::tolower((unsigned char)c));
            try{if(ext==".mdl"||ext==".hmp")worker->model(e.file);else if(ext==".pcx"||ext==".tga"||ext==".bmp"||ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".dds")worker->image_ptr(e.file);}catch(...){}
        }
        value->models=std::move(worker->models_);value->alphaDepth=worker->nativeAlphaDepth_;
        for(auto& [key,image]:worker->images_)if(!known.contains(key))value->images.emplace(key,image);
        return value;}).share());
    return false;
}
void Assets::drop_world(std::string_view name) {
    auto normalized=safe_name(name);
    if(auto it=worlds_.find(normalized);it!=worlds_.end()){cachedBytes_-=std::min(cachedBytes_,world_bytes(it->second));worlds_.erase(it);}
    if(auto it=prefetches_.find(normalized);it!=prefetches_.end()&&it->second.wait_for(std::chrono::seconds(0))==std::future_status::ready)prefetches_.erase(it);
    prefetchedModels_.erase(normalized);
}

std::shared_ptr<const Image> Assets::embedded_image(std::span<const uint8_t> bytes,size_t&position,uint32_t type,uint32_t width,uint32_t height) {
    Cursor c{bytes,position};unsigned format=type&7;
    if(format==7) {auto name=c.str(width);if(name.empty())malformed("empty external skin reference");position=c.p;return image_ptr(name);}
    if(format==6) {auto compressed=c.take(width);auto image=decode_image(compressed,"embedded.dds");image.nativeTextureAlpha=bonusProfile_ && image.nativeTextureAlpha;position=c.p;charge(cachedBytes_,image.rgba.size());return std::make_shared<const Image>(std::move(image));}
    if(!width||!height)malformed("zero texture dimension");size_t count=checked_product(width,height),bpp=format==0?1:(format==2||format==3)?2:format==4?3:format==5?4:0;
    if(!bpp)malformed("unknown embedded skin encoding");auto input=c.take(checked_product(count,bpp));
    std::vector<uint8_t> palette;
    if(format==0){
        if(contains("palette.raw")){palette=read("palette.raw");if(palette.size()!=768)malformed("indexed skin palette.raw size must be 768");}
        else{palette=read("palette.pcx");if(palette.size()<769||palette[palette.size()-769]!=12)malformed("indexed skin requires genuine PCX palette");}
    }
    Image image{width,height,std::vector<uint8_t>(checked_product(count,4)),uint32_t(bpp*8),uint8_t(bpp)};
    // 1005ef60 initializes BMAP flag0x100 for packed ARGB4444/BGRA32 skins.
    image.nativeTextureAlpha=bonusProfile_ && (format==3 || format==5);
    for(size_t i=0;i<count;++i) {
        auto*dst=image.rgba.data()+4*i;auto*src=input.data()+bpp*i;dst[3]=255;
        if(format==0){auto*p=palette.data()+palette.size()-768+size_t(src[0])*3;dst[0]=p[0];dst[1]=p[1];dst[2]=p[2];}
        else if(format==2){unsigned v=unsigned(src[0])|(unsigned(src[1])<<8);dst[0]=uint8_t(((v>>11)&31)*255/31);dst[1]=uint8_t(((v>>5)&63)*255/63);dst[2]=uint8_t((v&31)*255/31);}
        else if(format==3){unsigned v=unsigned(src[0])|(unsigned(src[1])<<8);dst[0]=uint8_t(((v>>8)&15)*17);dst[1]=uint8_t(((v>>4)&15)*17);dst[2]=uint8_t((v&15)*17);dst[3]=uint8_t((v>>12)*17);}
        else{dst[0]=src[(type&128)?0:2];dst[1]=src[1];dst[2]=src[(type&128)?2:0];if(format==5)dst[3]=src[3];}
    }
    if(type&8) {
        if(format==0||width%8||height%8)malformed("invalid mipmapped skin dimensions");
        for(unsigned shift=1;shift<=3;++shift)c.skip(checked_product(checked_product(width>>shift,height>>shift),bpp));
    }
    position=c.p;charge(cachedBytes_,image.rgba.size());return std::make_shared<const Image>(std::move(image));
}

World Assets::decode_world(std::span<const uint8_t> bytes, std::string_view name) {
    Cursor header{bytes};
    if(header.str(4)!="WMB6") malformed("unsupported world format: "+std::string(name));
    World result;result.mesh.formatVersion=6;
    std::array<std::span<const uint8_t>,17> sections;
    for(size_t i=0;i<sections.size();++i) {
        size_t offset=header.u32(), length=header.u32();
        if(length && (offset<140 || offset>bytes.size() || length>bytes.size()-offset))
            malformed("WMB6 section outside file");
        if(!length) { sections[i]={}; continue; }
        sections[i]=bytes.subspan(offset,length);
    }
    std::vector<std::pair<size_t,size_t>> sectionRanges;
    for(auto section:sections)if(!section.empty()){
        const auto offset=size_t(section.data()-bytes.data());
        sectionRanges.emplace_back(offset,offset+section.size());
    }
    std::sort(sectionRanges.begin(),sectionRanges.end());
    for(size_t i=1;i<sectionRanges.size();++i)
        if(sectionRanges[i].first<sectionRanges[i-1].second)malformed("overlapping WMB6 sections");
    for(size_t i=0;i<sections.size();++i)if(!sections[i].empty())result.sections[i].assign(sections[i].begin(),sections[i].end());
    // Keep unindexed header/gaps/trailing compiler records losslessly, without duplicating sections.
    auto metadata=[&](size_t start,size_t end){
        if(start==end)return;
        for(auto value:{uint32_t(start),uint32_t(end-start)})
            for(unsigned shift=0;shift<32;shift+=8)result.metadata.push_back(uint8_t(value>>shift));
        result.metadata.insert(result.metadata.end(),bytes.begin()+start,bytes.begin()+end);
    };
    size_t unindexed=0;
    for(auto [start,end]:sectionRanges){metadata(unindexed,start);unindexed=end;}
    metadata(unindexed,bytes.size());
    auto records=[&](size_t section,size_t stride) {
        if(sections[section].size()%stride) malformed("partial WMB6 record");
        return sections[section].size()/stride;
    };
    auto offsets=[](Cursor& c,bool allowMissing=false) {
        uint32_t count=c.u32();
        size_t table=checked_product(count,4);
        if(table>c.remaining()) malformed("truncated WMB6 offset table");
        std::vector<uint32_t> values; values.reserve(count);
        for(uint32_t i=0;i<count;++i) {
            uint32_t off=c.u32();
            if(allowMissing && off==UINT32_MAX) { values.push_back(off);continue; }
            if(off<4+table || off>=c.b.size()) malformed("invalid WMB6 record offset");
            values.push_back(off);
        }
        return values;
    };
    auto dword_color=[](uint32_t value) -> Color {
        return {float((value>>16)&255)/255,float((value>>8)&255)/255,
                float(value&255)/255,float(value>>24)/255};
    };
    auto gray=[](float value) -> Color { return {value,value,value,1}; };
    if(!sections[15].empty()) {
        Cursor objects{sections[15]}; auto entries=offsets(objects);
        auto sorted=entries; std::sort(sorted.begin(),sorted.end());
        if(std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end()) malformed("duplicate WMB6 object offset");
        for(size_t i=0;i<entries.size();++i) {
            auto next=std::upper_bound(sorted.begin(),sorted.end(),entries[i]);
            size_t end=next==sorted.end()?objects.b.size():*next;
            auto record=objects.b.subspan(entries[i],end-entries[i]);
            Cursor c{record}; uint32_t type=c.u32();
            if(type==5) {
                c.skip(12);result.sunAzimuth=c.f32();result.sunElevation=c.f32();
                result.infoFlags=c.u32();result.compilerVersion=c.f32();
                result.gamma=float(c.u8())/255;result.lightmapSize=c.u8();c.skip(2);
                if(result.lightmapSize>2) malformed("invalid WMB6 lightmap dimension");
                result.sunColor=dword_color(c.u32());result.ambient=dword_color(c.u32());
                for(auto& color:result.fogColors) color=dword_color(c.u32());
                result.fogColor=result.fogColors[0];
                constexpr float radians=3.14159265358979323846f/180;
                float az=result.sunAzimuth*radians,el=result.sunElevation*radians;
                result.sunDirection={std::cos(el)*std::cos(az),std::cos(el)*std::sin(az),std::sin(el)};
            } else if(type==2) {
                Light light;light.position=c.vec3();auto color=c.vec3();
                light.color={color.x/100,color.y/100,color.z/100,1};light.range=c.f32();light.flags=c.u32();
                result.lights.push_back(light);
            } else if(type==6) {
                Path path;path.name=c.str(20);float points=c.f32();
                if(points<0 || points>float(assetLimit/sizeof(PathNode)) || std::floor(points)!=points)
                    malformed("invalid WMB6 path point count");
                uint32_t count=static_cast<uint32_t>(points);c.skip(12);
                (void)c.u32(); // Old writers retain a control word here, not necessarily the edge count.
                size_t pointBytes=checked_product(count,36);
                if(pointBytes>c.remaining() || (c.remaining()-pointBytes)%24) malformed("truncated WMB6 path");
                size_t edgeCount=(c.remaining()-pointBytes)/24;
                checked_product(count,sizeof(PathNode));path.nodes.resize(count);
                for(auto& point:path.nodes) point.position=c.vec3();
                for(auto& point:path.nodes) for(auto& skill:point.skills) skill=c.f32();
                path.edges.reserve(edgeCount);path.edgeInfo.reserve(edgeCount);
                for(size_t edge=0;edge<edgeCount;++edge) {
                    float a=c.f32(),b=c.f32();PathEdgeInfo info;
                    info.length=c.f32();info.bezier=c.f32();info.weight=c.f32();info.skill=c.f32();
                    if(a<1 || b<1 || a>points || b>points || std::floor(a)!=a || std::floor(b)!=b)
                        malformed("WMB6 path edge outside node array");
                    info.node1=uint32_t(a)-1;info.node2=uint32_t(b)-1;
                    path.edges.push_back({info.node1,info.node2});path.edgeInfo.push_back(info);
                    path.nodes[info.node1].edges.push_back(static_cast<uint32_t>(edge));
                }
                path.metadata.assign(record.begin(),record.end());result.paths.push_back(std::move(path));
            } else {
                WorldEntity entity;entity.type=type;entity.objectIndex=static_cast<uint32_t>(i);
                if(type==1) {
                    entity.position=c.vec3();entity.angle=c.vec3();c.skip(8);entity.name=c.str(20);
                } else if(type==3 || type==7) {
                    entity.position=c.vec3();entity.angle=c.vec3();entity.scale=c.vec3();
                    // These records are native C structs, including alignment before the float skills.
                    size_t padded=type==3?136:372,packed=type==3?133:371;
                    if(record.size()<packed) malformed("truncated WMB6 entity");
                    entity.name=c.str(type==3?20:33);
                    if(type==3 && record[72]!=0) {
                        entity.file=c.str(32);c.skip(1);
                    } else {
                        entity.file=c.str(type==3?13:33);entity.action=c.str(type==3?20:33);
                    }
                    if(record.size()>=padded) c.skip(type==3?3:1);
                    for(size_t skill=0;skill<(type==3?8:20);++skill) entity.skills[skill]=c.f32();
                    // A6 00459eb0 copies both record types' flags verbatim (case 3 dword 0x20, case 7 whole record).
                    entity.flags=c.u32();
                    entity.ambient=gray(c.f32()/100);
                    if(type==7) {
                        entity.albedo=c.f32()/100;entity.pathIndex=c.u32();entity.attachedEntityIndex=c.u32();
                        entity.material=c.str(33);entity.string1=c.str(33);entity.string2=c.str(33);c.skip(33);
                    } else entity.albedo=0.5f;
                } else if(type==4) {
                    entity.position=c.vec3();entity.soundVolume=c.f32();c.skip(8);
                    entity.soundRange=float(c.u32());entity.flags=c.u32();entity.file=c.str(33);
                } else if(type==8) {
                    entity.regionMin=c.vec3();entity.regionMax=c.vec3();c.skip(8);entity.name=c.str(32);
                } else malformed("unsupported WMB6 object type");
                entity.metadata.assign(record.begin(),record.end());result.entities.push_back(std::move(entity));
            }
        }
        for(auto& entity:result.entities) if(entity.pathIndex) {
            if(entity.pathIndex>result.paths.size()) malformed("WMB6 attached path outside path list");
            entity.path=result.paths[entity.pathIndex-1].name;
        }
    }
    result.nativeLightmapStorageBytes=!(result.infoFlags&1) || sections[16].empty()?uint8_t(1):nativeColoredLightmapBytes_;
    std::vector<std::shared_ptr<const Image>> textures;
    std::vector<std::string> textureNames;
    if(!sections[2].empty()) {
        Cursor table{sections[2]};auto entries=offsets(table,bonusProfile_);
        std::vector<uint32_t> boundaries;
        if(bonusProfile_) {
            boundaries=entries;std::sort(boundaries.begin(),boundaries.end());
        }
        textures.reserve(entries.size());textureNames.reserve(entries.size());
        for(auto offset:entries) {
            if(bonusProfile_ && offset==UINT32_MAX) {
                textures.emplace_back();textureNames.emplace_back();continue;
            }
            Cursor c{table.b};
            if(bonusProfile_) {
                const auto next=std::upper_bound(boundaries.begin(),boundaries.end(),offset);
                const size_t end=next==boundaries.end() || *next==UINT32_MAX?table.b.size():*next;
                c.b=table.b.subspan(offset,end-offset);
                if(c.b.size()<40)malformed("truncated Bonus WMB6 texture header");
            } else c.seek(offset);
            auto textureName=c.str(16);
            uint32_t width=c.u32(),height=c.u32();std::array<uint32_t,4> mip;
            for(auto& value:mip) value=c.u32();
            std::shared_ptr<const Image> image;
            if(bonusProfile_) {
                // A7 10059e90: +24 is the format, +40 starts packed pixels.
                const uint32_t encoding=mip[0]==10?2:mip[0]==12?4:0;
                if(!encoding)malformed("unsupported Bonus WMB6 texture encoding");
                if(!width || !height)malformed("invalid Bonus WMB6 texture dimensions");
                const size_t bpp=encoding==2?2:3;
                size_t length=40;
                // Flags 0x10403 and uploader 1002cd4d..7a store exactly four levels.
                for(unsigned level=0;level<4;++level) {
                    const uint32_t w=width>>level,h=height>>level;
                    if(!w || !h)malformed("invalid Bonus WMB6 mip dimensions");
                    if(level && mip[level]!=length)malformed("invalid Bonus WMB6 texture mip offset");
                    const size_t size=checked_product(checked_product(w,h),bpp);
                    if(length>c.b.size() || size>c.b.size()-length)malformed("truncated Bonus WMB6 texture mip");
                    length+=size;
                }
                if(length!=c.b.size())malformed("invalid Bonus WMB6 texture record size");
                size_t cursor=40;image=embedded_image(c.b,cursor,encoding,width,height);
            } else if(!width && !height) image=image_ptr(textureName);
            else {
                if(!width || !height || mip[0]<40 || mip[0]>table.b.size()-offset) malformed("invalid WMB6 texture header");
                const size_t pixels=checked_product(width,height);
                if(mip[1]<mip[0])malformed("invalid WMB6 texture mip offsets");
                const size_t baseSize=size_t(mip[1])-mip[0];
                const uint32_t encoding=baseSize==checked_product(pixels,2)?2:baseSize==checked_product(pixels,3)?4:0;
                if(!encoding)malformed("unsupported WMB6 texture pixel stride");
                size_t pixelBytes=baseSize;
                for(size_t level=0;level<mip.size();++level) {
                    if(mip[level]>table.b.size()-offset || pixelBytes/(size_t(1)<<(2*level))>table.b.size()-offset-mip[level])
                        malformed("WMB6 mipmap outside texture section");
                }
                size_t cursor=size_t(offset)+mip[0];image=embedded_image(table.b,cursor,encoding,width,height);
            }
            textures.push_back(std::move(image));textureNames.push_back(std::move(textureName));
        }
    }
    if(!sections[16].empty()) {
        uint32_t dimension=256u<<result.lightmapSize;
        size_t pixelCount=checked_product(dimension,dimension),stride=checked_product(pixelCount,3);
        if(sections[16].size()%stride) malformed("partial WMB6 lightmap atlas");
        result.lightmaps.reserve(sections[16].size()/stride);Cursor c{sections[16]};
        while(c.remaining()) {
            auto pixels=c.take(stride);auto image=std::make_shared<Image>();image->width=image->height=dimension;image->sourceDepth=24;
            image->nativeStorageBytes=result.nativeLightmapStorageBytes==2 || result.nativeLightmapStorageBytes==3?result.nativeLightmapStorageBytes:uint8_t(255);
            image->rgba.resize(checked_product(pixelCount,4));
            for(size_t i=0;i<pixelCount;++i) {
                image->rgba[4*i]=pixels[3*i+2];image->rgba[4*i+1]=pixels[3*i+1];
                image->rgba[4*i+2]=pixels[3*i];image->rgba[4*i+3]=255;
            }
            charge(cachedBytes_,image->rgba.size());result.lightmaps.push_back(std::move(image));
        }
    }
    struct Mapping { Vec3 s,t;float offsetS{},offsetT{};uint16_t baseFlags{}; };
    std::vector<Mapping> mappings;size_t materialCount=records(6,64);mappings.reserve(materialCount);
    result.mesh.materials.reserve(materialCount);Cursor materialCursor{sections[6]};
    for(size_t i=0;i<materialCount;++i) {
        size_t start=materialCursor.p;Mapping mapping;mapping.s=materialCursor.vec3();mapping.offsetS=materialCursor.f32();
        mapping.t=materialCursor.vec3();mapping.offsetT=materialCursor.f32();uint32_t texture=materialCursor.u32();
        mapping.baseFlags=materialCursor.u16();uint32_t userFlags=materialCursor.u32();
        Material material;material.flags=uint32_t(mapping.baseFlags)|(userFlags<<16);
        material.ambient=gray(float(std::bit_cast<int8_t>(materialCursor.u8()))/100);material.albedo=gray(float(materialCursor.u8())/100);
        material.name=materialCursor.str(20);
        if(texture>=textures.size()) malformed("WMB6 material references missing texture");
        if(bonusProfile_ && !textures[texture]) {
            // A7 1005b47c..484: table[0] exactly (even null), then clear all flags.
            texture=0;material.flags=0;mapping.baseFlags=0;
        }
        material.texture=textures[texture];material.textureName=textureNames[texture];
        auto raw=sections[6].subspan(start,64);material.raw.assign(raw.begin(),raw.end());
        mappings.push_back(mapping);result.mesh.materials.push_back(std::move(material));
    }
    Cursor planeCursor{sections[1]};result.planes.reserve(records(1,20));
    while(planeCursor.remaining()) {
        WorldPlane plane;plane.normal=planeCursor.vec3();plane.distance=planeCursor.f32();plane.type=planeCursor.u32();
        result.planes.push_back(plane);
    }
    Cursor vertices{sections[3]};result.collision.vertices.reserve(records(3,12));
    while(vertices.remaining()) result.collision.vertices.push_back(vertices.vec3());
    if(!result.collision.vertices.empty()) {
        result.mesh.min=result.mesh.max=result.collision.vertices.front();
        for(auto p:result.collision.vertices) {
            result.mesh.min.x=std::min(result.mesh.min.x,p.x);result.mesh.min.y=std::min(result.mesh.min.y,p.y);result.mesh.min.z=std::min(result.mesh.min.z,p.z);
            result.mesh.max.x=std::max(result.mesh.max.x,p.x);result.mesh.max.y=std::max(result.mesh.max.y,p.y);result.mesh.max.z=std::max(result.mesh.max.z,p.z);
        }
    }
    result.mesh.sourceVertices.reserve(result.collision.vertices.size());
    for(auto position:result.collision.vertices) {Vertex vertex;vertex.position=position;result.mesh.sourceVertices.push_back(vertex);}
    std::vector<std::array<uint32_t,2>> edges;edges.reserve(records(12,8));Cursor edgeCursor{sections[12]};
    while(edgeCursor.remaining()) edges.push_back({edgeCursor.u32(),edgeCursor.u32()});
    std::vector<int32_t> edgeList;edgeList.reserve(records(13,4));Cursor edgeListCursor{sections[13]};
    while(edgeListCursor.remaining()) edgeList.push_back(edgeListCursor.i32());
    Cursor faceCursor{sections[7]};result.faces.reserve(records(7,24));
    size_t vertexCount=0,triangleCount=0;
    while(faceCursor.remaining()) {
        WorldFace face;face.legacyPlane=faceCursor.u16();face.side=faceCursor.u16();face.firstEdge=faceCursor.u32();
        face.edgeCount=faceCursor.u16();face.materialIndex=faceCursor.u16();
        for(auto& value:face.lightStyles) value=faceCursor.u8();
        face.lightmapOffset=faceCursor.i32();face.plane=faceCursor.u32();
        face.lightmapScale=face.lightStyles[0];face.lightmapIndex=face.lightStyles[1];
        face.lightmapOrigin={float(face.lightStyles[2]),float(face.lightStyles[3])};
        if(face.edgeCount<3 || face.firstEdge>edgeList.size() || face.edgeCount>edgeList.size()-face.firstEdge ||
           face.plane>=result.planes.size() || face.materialIndex>=mappings.size() || face.side>1)
            malformed("invalid WMB6 face references");
        vertexCount+=face.edgeCount;triangleCount+=face.edgeCount-2;result.faces.push_back(face);
    }
    checked_product(vertexCount,sizeof(Vertex));checked_product(triangleCount,sizeof(Triangle));
    result.mesh.vertices.reserve(vertexCount);result.mesh.triangles.reserve(triangleCount);
    result.collision.triangles.reserve(triangleCount);result.collision.flags.reserve(triangleCount);
    result.mesh.sourceTriangles.reserve(triangleCount);result.mesh.vertexSources.reserve(vertexCount);
    auto dot=[](Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;};
    auto normalize=[](Vec3 n) {float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);return length>0?Vec3{n.x/length,n.y/length,n.z/length}:Vec3{};};
    auto native_projection=[](const Vec3& p,const Vec3& axis,float offset) {
        // Native56ed0 rounds each product and these grouped sums to f32; stores prevent FMA.
        volatile float x=p.x*axis.x,y=p.y*axis.y,z=p.z*axis.z;
        volatile float xy=x+y,zo=z+offset;
        volatile float projection=xy+zo;
        return projection;
    };
    auto native_texel_bounds=[](float minimum,float maximum,float period,int32_t& origin,int32_t& extent) {
        volatile float minimumCell=minimum/period,maximumCell=maximum/period;
        double first=std::floor(double(minimumCell)),last=std::ceil(double(maximumCell));
        double nativeOrigin=first*double(period),nativeExtent=(last-first)*double(period);
        if(!std::isfinite(nativeOrigin) || !std::isfinite(nativeExtent) ||
           nativeOrigin<std::numeric_limits<int32_t>::min() || nativeOrigin>std::numeric_limits<int32_t>::max() ||
           nativeExtent<std::numeric_limits<int32_t>::min() || nativeExtent>std::numeric_limits<int32_t>::max())
            malformed("WMB6 native texel bounds outside int32 range");
        origin=static_cast<int32_t>(nativeOrigin);extent=static_cast<int32_t>(nativeExtent);
    };
    std::vector<Vec3> smoothNormals(result.collision.vertices.size());
    std::unordered_map<uint64_t,uint32_t> atlasMaterials;
    std::vector<uint32_t> polygon;
    // A7 CPU World+0x144: 1005a520 enables colored >=6 worlds; 1005b800
    // copies nonempty RGB24 section16. A6 instead uses mono section8.
    const bool nativeCpuLightmap=bonusProfile_ && result.mesh.formatVersion>=6 &&
        (result.infoFlags&1) && !sections[16].empty();
    for(size_t faceIndex=0;faceIndex<result.faces.size();++faceIndex) {
        auto& face=result.faces[faceIndex];
        polygon.clear();polygon.reserve(face.edgeCount);
        for(uint32_t j=0;j<face.edgeCount;++j) {
            int32_t signedEdge=edgeList[face.firstEdge+j];uint64_t index=signedEdge<0?uint64_t(-int64_t(signedEdge)):uint64_t(signedEdge);
            if(index>=edges.size()) malformed("WMB6 face edge outside edge array");
            auto e=edges[index];if(signedEdge<0)std::swap(e[0],e[1]);
            if(e[0]>=result.collision.vertices.size() || e[1]>=result.collision.vertices.size())
                malformed("WMB6 polygon vertex outside vertex array");
            // Compiled faces are ordered signed-edge origins. Compiler seams need not
            // share exact closing endpoints; the render polygon is closed by its fan.
            polygon.push_back(e[0]);
        }
        auto& mapping=mappings[face.materialIndex];auto& baseMaterial=result.mesh.materials[face.materialIndex];
        if(!baseMaterial.texture || !baseMaterial.texture->width || !baseMaterial.texture->height) malformed("empty WMB6 surface texture");
        Vec3 normal=result.planes[face.plane].normal;if(face.side)normal={-normal.x,-normal.y,-normal.z};normal=normalize(normal);
        float minS=std::numeric_limits<float>::max(),minT=minS;
        float nativeMinS=minS,nativeMinT=minS,nativeMaxS=-minS,nativeMaxT=-minS;
        for(auto index:polygon) {
            auto p=result.collision.vertices[index];minS=std::min(minS,dot(p,mapping.s)+mapping.offsetS);minT=std::min(minT,dot(p,mapping.t)+mapping.offsetT);
            float s=native_projection(p,mapping.s,mapping.offsetS),t=native_projection(p,mapping.t,mapping.offsetT);
            if(!std::isfinite(s) || !std::isfinite(t)) malformed("non-finite WMB6 native face projection");
            nativeMinS=std::min(nativeMinS,s);nativeMinT=std::min(nativeMinT,t);
            nativeMaxS=std::max(nativeMaxS,s);nativeMaxT=std::max(nativeMaxT,t);
        }
        face.nativeSampleShift=(result.infoFlags&1) && face.lightStyles[0]==3?3:4;
        float nativePeriod=float(uint32_t(1)<<face.nativeSampleShift);
        native_texel_bounds(nativeMinS,nativeMaxS,nativePeriod,face.texelOriginS,face.texelExtentS);
        native_texel_bounds(nativeMinT,nativeMaxT,nativePeriod,face.texelOriginT,face.texelExtentT);
        bool geometryValid=face.texelExtentS>=0 && face.texelExtentT>=0;
        uint32_t raw=mapping.baseFlags;
        face.nativeFlags=face.side?0x10000:0;
        if(raw&0x100) face.nativeFlags|=0x880;
        if(raw&4) face.nativeFlags|=0x106;
        else if((raw&0xA)==0xA) face.nativeFlags|=0x302;
        else if(raw&8) face.nativeFlags|=0x112;
        else if((raw&2) || (bonusProfile_?!nativeCpuLightmap:sections[8].empty())) face.nativeFlags|=0x102;
        else if(!geometryValid) face.nativeFlags=0x302;
        else if(face.lightmapOffset>=0 && (face.texelExtentS>100 || face.texelExtentT>100))
            face.nativeFlags|=0x40000;
        uint32_t materialIndex=face.materialIndex;bool hasLightmap=!result.lightmaps.empty() && face.lightmapOffset>=0;
        float lmScale=1,lmMinS=0,lmMinT=0,lmDimension=1;
        if(hasLightmap) {
            if(face.lightmapScale>30 || face.lightmapIndex>=result.lightmaps.size()) malformed("invalid WMB6 face lightmap");
            lmScale=float(uint32_t(1)<<face.lightmapScale);lmMinS=std::floor(minS/lmScale);lmMinT=std::floor(minT/lmScale);
            lmDimension=float(result.lightmaps[face.lightmapIndex]->width);
            uint64_t id=(uint64_t(face.materialIndex)<<32)|face.lightmapIndex;
            auto found=atlasMaterials.find(id);
            if(found==atlasMaterials.end()) {
                Material material=baseMaterial;material.lightmap=result.lightmaps[face.lightmapIndex];
                material.lightmapName=std::string(name)+"#lightmap/"+std::to_string(face.lightmapIndex);
                materialIndex=static_cast<uint32_t>(result.mesh.materials.size());result.mesh.materials.push_back(std::move(material));
                atlasMaterials.emplace(id,materialIndex);
            } else materialIndex=found->second;
        }
        uint32_t start=static_cast<uint32_t>(result.mesh.vertices.size());
        // Material insertion can reallocate; use indices rather than retaining a material reference.
        auto texture=result.mesh.materials[face.materialIndex].texture;
        for(auto index:polygon) {
            Vertex vertex;vertex.position=result.collision.vertices[index];vertex.normal=normal;
            float s=dot(vertex.position,mapping.s)+mapping.offsetS,t=dot(vertex.position,mapping.t)+mapping.offsetT;
            vertex.uv={s/float(texture->width),t/float(texture->height)};
            if(hasLightmap) vertex.lightmap_uv={(s/lmScale-lmMinS+face.lightmapOrigin.x+0.5f)/lmDimension,
                                               (t/lmScale-lmMinT+face.lightmapOrigin.y+0.5f)/lmDimension};
            result.mesh.vertices.push_back(vertex);result.mesh.vertexSources.push_back(index);
        }
        for(uint32_t j=2;j<face.edgeCount;++j) {
            // WMB6 stores clockwise fans; native draw/collision triangles use outward CCW.
            result.mesh.triangles.push_back({{start,start+j,start+j-1},materialIndex,mapping.baseFlags,static_cast<uint32_t>(faceIndex)});
            result.collision.triangles.push_back({polygon[0],polygon[j],polygon[j-1]});result.collision.flags.push_back(mapping.baseFlags);
            result.mesh.sourceTriangles.push_back({polygon[0],polygon[j-1],polygon[j]});
            if(mapping.baseFlags&16384) {
                auto a=result.collision.vertices[polygon[0]],b=result.collision.vertices[polygon[j-1]],c=result.collision.vertices[polygon[j]];
                Vec3 ab{b.x-a.x,b.y-a.y,b.z-a.z},ac{c.x-a.x,c.y-a.y,c.z-a.z};
                Vec3 cross{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
                float area=std::sqrt(dot(cross,cross));
                for(auto index:{polygon[0],polygon[j-1],polygon[j]}) {auto& n=smoothNormals[index];n.x+=normal.x*area;n.y+=normal.y*area;n.z+=normal.z*area;}
            }
        }
    }
    for(auto& n:smoothNormals)n=normalize(n);
    for(auto& triangle:result.mesh.triangles) if(triangle.flags&16384)
        for(auto index:triangle.indices) if(dot(smoothNormals[result.mesh.vertexSources[index]],smoothNormals[result.mesh.vertexSources[index]])>0)
            result.mesh.vertices[index].normal=smoothNormals[result.mesh.vertexSources[index]];
    auto packed_short=[](uint16_t word) {
        int32_t value=std::bit_cast<int16_t>(word);
        if(value>29000)value=29000+(value-29000)*1024;
        else if(value<-29000)value=-29000+(value+29000)*1024;
        return float(value);
    };
    auto bounds=[&](Cursor& c) -> Vec3 {return {packed_short(c.u16()),packed_short(c.u16()),packed_short(c.u16())};};
    Cursor bspBlocks{sections[11]};result.bspBlocks.reserve(records(11,4));
    while(bspBlocks.remaining()) {
        uint32_t face=bspBlocks.u32();if(face>=result.faces.size())malformed("WMB6 BSP block references missing face");result.bspBlocks.push_back(face);
    }
    Cursor bspNodes{sections[5]};result.bspNodes.reserve(records(5,40));
    while(bspNodes.remaining()) {
        WorldBspNode node;node.plane=bspNodes.u32();bspNodes.skip(4);node.min=bounds(bspNodes);node.max=bounds(bspNodes);bspNodes.skip(4);
        node.children={bspNodes.i32(),bspNodes.i32()};node.firstFace=bspNodes.u32();node.faceCount=bspNodes.u32();
        if(node.plane>=result.planes.size() || node.firstFace>result.faces.size() || node.faceCount>result.faces.size()-node.firstFace)
            malformed("invalid WMB6 BSP node");result.bspNodes.push_back(node);
    }
    Cursor bspLeaves{sections[10]};result.bspLeaves.reserve(records(10,36));
    while(bspLeaves.remaining()) {
        WorldBspLeaf leaf;leaf.content=bspLeaves.i32();leaf.pvsOffset=bspLeaves.i32();leaf.min=bounds(bspLeaves);leaf.max=bounds(bspLeaves);
        if(!bonusProfile_ && leaf.content<0) {
            // A6 0045af50 normalizes saved terminal contents before point conversion.
            const uint32_t raw=std::bit_cast<uint32_t>(leaf.content);
            leaf.content=std::bit_cast<int32_t>((raw&0x10)?uint32_t{0xffffff00}:raw&uint32_t{0xfffffff2});
        }
        bspLeaves.skip(8);leaf.firstBlock=bspLeaves.u32();leaf.blockCount=bspLeaves.u32();
        // Leaf zero is the legacy solid sentinel; its unused saved pointers/box/range are not geometry.
        if(!result.bspLeaves.empty()) {
            if(leaf.firstBlock>result.bspBlocks.size() || leaf.blockCount>result.bspBlocks.size()-leaf.firstBlock)
                malformed("invalid WMB6 BSP leaf block range");
            if(leaf.pvsOffset>=0 && size_t(leaf.pvsOffset)>=sections[4].size() && !sections[4].empty()) malformed("invalid WMB6 PVS offset");
        }
        result.bspLeaves.push_back(leaf);
    }
    for(auto& node:result.bspNodes) for(auto child:node.children) {
        size_t index=child<0?size_t(-int64_t(child)-1):size_t(child);
        if(index>=(child<0?result.bspLeaves.size():result.bspNodes.size())) malformed("invalid WMB6 BSP child");
    }
    Cursor hulls{sections[9]};result.hulls.reserve(records(9,16));
    while(hulls.remaining()) {
        WorldHull hull;hull.plane=hulls.u32();hulls.skip(4);hull.children={hulls.i32(),hulls.i32()};
        if(hull.plane>=result.planes.size())malformed("invalid WMB6 collision hull plane");result.hulls.push_back(hull);
    }
    for(auto& hull:result.hulls) for(auto child:hull.children)
        if(child>=0 && size_t(child)>=result.hulls.size()) malformed("invalid WMB6 collision hull child");
    if(!bonusProfile_ && !sections[14].empty()) {
        if(sections[14].size()%160)malformed("partial WMB6 legacy hull metadata");
        Cursor descriptors{sections[14].first(160)};
        // 00457980 overwrites the saved point root and zero-birth bounds.
        result.legacyHulls[0].last=static_cast<int32_t>(result.bspNodes.size())-1;
        const auto public_bound=[](int32_t value) {
            // 00456d10 stores i32 as f32; 0045b1e0 publishes reversed fixed bounds.
            volatile float native=static_cast<float>(value);
            return float(native_fp_trunc(static_cast<long double>(native)*-1024.0L))/1024.0f;
        };
        for(size_t i=1;i<result.legacyHulls.size();++i) {
            auto& hull=result.legacyHulls[i];
            descriptors.seek(0x24+i*4);hull.root=descriptors.i32();
            hull.last=static_cast<int32_t>(result.hulls.size())-1;
            if(hull.last>=0 && hull.root>=0 && hull.root>hull.last)
                malformed("WMB6 legacy hull root outside node array");
            descriptors.seek(0x40+(i-1)*12);
            const std::array<int32_t,3> minimum{descriptors.i32(),descriptors.i32(),descriptors.i32()};
            descriptors.seek(0x70+(i-1)*12);
            const std::array<int32_t,3> maximum{descriptors.i32(),descriptors.i32(),descriptors.i32()};
            hull.lower={public_bound(maximum[0]),public_bound(maximum[1]),public_bound(maximum[2])};
            hull.upper={public_bound(minimum[0]),public_bound(minimum[1]),public_bound(minimum[2])};
            // Narrow is generic unless some axis has BOTH source bounds nonzero.
            hull.generic=i==1 && (minimum[0]==0 || maximum[0]==0) &&
                (minimum[1]==0 || maximum[1]==0) && (minimum[2]==0 || maximum[2]==0);
        }
        checked_product(result.bspNodes.size(),sizeof(WorldHull));
        result.legacyPointNodes.reserve(result.bspNodes.size());
        for(size_t i=0;i<result.bspNodes.size();++i) {
            const auto& source=result.bspNodes[i];
            WorldHull node{source.plane,source.children,static_cast<uint32_t>(i)};
            for(auto& child:node.children) if(child<0) {
                child=result.bspLeaves[size_t(-int64_t(child)-1)].content;
                if(child>=0)malformed("WMB6 point hull leaf is not terminal content");
            }
            result.legacyPointNodes.push_back(node);
        }
        // Validate every node once, including unreachable nodes, without recursive loading.
        const auto validate_nodes=[](const std::vector<WorldHull>& nodes) {
            std::vector<uint32_t> incoming(nodes.size()),ready;
            ready.reserve(nodes.size());
            for(const auto& node:nodes)for(auto child:node.children)
                if(child>=0)++incoming[size_t(child)];
            for(size_t i=0;i<nodes.size();++i)if(!incoming[i])ready.push_back(static_cast<uint32_t>(i));
            for(size_t i=0;i<ready.size();++i)for(auto child:nodes[ready[i]].children)
                if(child>=0 && !--incoming[size_t(child)])ready.push_back(static_cast<uint32_t>(child));
            if(ready.size()!=nodes.size())malformed("cyclic WMB6 legacy hull nodes");
        };
        validate_nodes(result.legacyPointNodes);validate_nodes(result.hulls);
        result.legacyHullsValid=true;
        result.legacyHullBoundsPublished=!result.hulls.empty() && !result.legacyHulls[1].generic;
    }
    return result;
}
namespace {
using MdlMatrix = std::array<float,12>; // row-major affine matrix; column vectors
constexpr std::array<Vec3,162> mdl_normals{{
    {-0.525725f,0.000000f,0.850650f},
    {-0.442863f,0.238856f,0.864188f},
    {-0.295242f,0.000000f,0.955423f},
    {-0.309017f,0.500000f,0.809017f},
    {-0.162460f,0.262866f,0.951056f},
    {0.000000f,0.000000f,1.000000f},
    {0.000000f,0.850651f,0.525731f},
    {-0.147621f,0.716567f,0.681718f},
    {0.147621f,0.716567f,0.681718f},
    {0.000000f,0.525731f,0.850651f},
    {0.309017f,0.500000f,0.809017f},
    {0.525731f,0.000000f,0.850651f},
    {0.295242f,0.000000f,0.955423f},
    {0.442863f,0.238856f,0.864188f},
    {0.162460f,0.262866f,0.951056f},
    {-0.681718f,0.147621f,0.716567f},
    {-0.809017f,0.309017f,0.500000f},
    {-0.587785f,0.425325f,0.688191f},
    {-0.850651f,0.525731f,0.000000f},
    {-0.864188f,0.442863f,0.238856f},
    {-0.716567f,0.681718f,0.147621f},
    {-0.688191f,0.587785f,0.425325f},
    {-0.500000f,0.809017f,0.309017f},
    {-0.238856f,0.864188f,0.442863f},
    {-0.425325f,0.688191f,0.587785f},
    {-0.716567f,0.681718f,-0.147621f},
    {-0.500000f,0.809017f,-0.309017f},
    {-0.525731f,0.850651f,0.000000f},
    {0.000000f,0.850651f,-0.525731f},
    {-0.238856f,0.864188f,-0.442863f},
    {0.000000f,0.955423f,-0.295242f},
    {-0.262866f,0.951056f,-0.162460f},
    {0.000000f,1.000000f,0.000000f},
    {0.000000f,0.955423f,0.295242f},
    {-0.262866f,0.951056f,0.162460f},
    {0.238856f,0.864188f,0.442863f},
    {0.262866f,0.951056f,0.162460f},
    {0.500000f,0.809017f,0.309017f},
    {0.238856f,0.864188f,-0.442863f},
    {0.262866f,0.951056f,-0.162460f},
    {0.500000f,0.809017f,-0.309017f},
    {0.850651f,0.525731f,0.000000f},
    {0.716567f,0.681718f,0.147621f},
    {0.716567f,0.681718f,-0.147621f},
    {0.525731f,0.850651f,0.000000f},
    {0.425325f,0.688191f,0.587785f},
    {0.864188f,0.442863f,0.238856f},
    {0.688191f,0.587785f,0.425325f},
    {0.809017f,0.309017f,0.500000f},
    {0.681718f,0.147621f,0.716567f},
    {0.587785f,0.425325f,0.688191f},
    {0.955423f,0.295242f,0.000000f},
    {1.000000f,0.000000f,0.000000f},
    {0.951056f,0.162460f,0.262866f},
    {0.850651f,-0.525731f,0.000000f},
    {0.955423f,-0.295242f,0.000000f},
    {0.864188f,-0.442863f,0.238856f},
    {0.951056f,-0.162460f,0.262866f},
    {0.809017f,-0.309017f,0.500000f},
    {0.681718f,-0.147621f,0.716567f},
    {0.850651f,0.000000f,0.525731f},
    {0.864188f,0.442863f,-0.238856f},
    {0.809017f,0.309017f,-0.500000f},
    {0.951056f,0.162460f,-0.262866f},
    {0.525731f,0.000000f,-0.850651f},
    {0.681718f,0.147621f,-0.716567f},
    {0.681718f,-0.147621f,-0.716567f},
    {0.850651f,0.000000f,-0.525731f},
    {0.809017f,-0.309017f,-0.500000f},
    {0.864188f,-0.442863f,-0.238856f},
    {0.951056f,-0.162460f,-0.262866f},
    {0.147621f,0.716567f,-0.681718f},
    {0.309017f,0.500000f,-0.809017f},
    {0.425325f,0.688191f,-0.587785f},
    {0.442863f,0.238856f,-0.864188f},
    {0.587785f,0.425325f,-0.688191f},
    {0.688197f,0.587780f,-0.425327f},
    {-0.147621f,0.716567f,-0.681718f},
    {-0.309017f,0.500000f,-0.809017f},
    {0.000000f,0.525731f,-0.850651f},
    {-0.525731f,0.000000f,-0.850651f},
    {-0.442863f,0.238856f,-0.864188f},
    {-0.295242f,0.000000f,-0.955423f},
    {-0.162460f,0.262866f,-0.951056f},
    {0.000000f,0.000000f,-1.000000f},
    {0.295242f,0.000000f,-0.955423f},
    {0.162460f,0.262866f,-0.951056f},
    {-0.442863f,-0.238856f,-0.864188f},
    {-0.309017f,-0.500000f,-0.809017f},
    {-0.162460f,-0.262866f,-0.951056f},
    {0.000000f,-0.850651f,-0.525731f},
    {-0.147621f,-0.716567f,-0.681718f},
    {0.147621f,-0.716567f,-0.681718f},
    {0.000000f,-0.525731f,-0.850651f},
    {0.309017f,-0.500000f,-0.809017f},
    {0.442863f,-0.238856f,-0.864188f},
    {0.162460f,-0.262866f,-0.951056f},
    {0.238856f,-0.864188f,-0.442863f},
    {0.500000f,-0.809017f,-0.309017f},
    {0.425325f,-0.688191f,-0.587785f},
    {0.716567f,-0.681718f,-0.147621f},
    {0.688191f,-0.587785f,-0.425325f},
    {0.587785f,-0.425325f,-0.688191f},
    {0.000000f,-0.955423f,-0.295242f},
    {0.000000f,-1.000000f,0.000000f},
    {0.262866f,-0.951056f,-0.162460f},
    {0.000000f,-0.850651f,0.525731f},
    {0.000000f,-0.955423f,0.295242f},
    {0.238856f,-0.864188f,0.442863f},
    {0.262866f,-0.951056f,0.162460f},
    {0.500000f,-0.809017f,0.309017f},
    {0.716567f,-0.681718f,0.147621f},
    {0.525731f,-0.850651f,0.000000f},
    {-0.238856f,-0.864188f,-0.442863f},
    {-0.500000f,-0.809017f,-0.309017f},
    {-0.262866f,-0.951056f,-0.162460f},
    {-0.850651f,-0.525731f,0.000000f},
    {-0.716567f,-0.681718f,-0.147621f},
    {-0.716567f,-0.681718f,0.147621f},
    {-0.525731f,-0.850651f,0.000000f},
    {-0.500000f,-0.809017f,0.309017f},
    {-0.238856f,-0.864188f,0.442863f},
    {-0.262866f,-0.951056f,0.162460f},
    {-0.864188f,-0.442863f,0.238856f},
    {-0.809017f,-0.309017f,0.500000f},
    {-0.688191f,-0.587785f,0.425325f},
    {-0.681718f,-0.147621f,0.716567f},
    {-0.442863f,-0.238856f,0.864188f},
    {-0.587785f,-0.425325f,0.688191f},
    {-0.309017f,-0.500000f,0.809017f},
    {-0.147621f,-0.716567f,0.681718f},
    {-0.425325f,-0.688191f,0.587785f},
    {-0.162460f,-0.262866f,0.951056f},
    {0.442863f,-0.238856f,0.864188f},
    {0.162460f,-0.262866f,0.951056f},
    {0.309017f,-0.500000f,0.809017f},
    {0.147621f,-0.716567f,0.681718f},
    {0.000000f,-0.525731f,0.850651f},
    {0.425325f,-0.688191f,0.587785f},
    {0.587785f,-0.425325f,0.688191f},
    {0.688191f,-0.587785f,0.425325f},
    {-0.955423f,0.295242f,0.000000f},
    {-0.951056f,0.162460f,0.262866f},
    {-1.000000f,0.000000f,0.000000f},
    {-0.850651f,0.000000f,0.525731f},
    {-0.955423f,-0.295242f,0.000000f},
    {-0.951056f,-0.162460f,0.262866f},
    {-0.864188f,0.442863f,-0.238856f},
    {-0.951056f,0.162460f,-0.262866f},
    {-0.809017f,0.309017f,-0.500000f},
    {-0.864188f,-0.442863f,-0.238856f},
    {-0.951056f,-0.162460f,-0.262866f},
    {-0.809017f,-0.309017f,-0.500000f},
    {-0.681718f,0.147621f,-0.716567f},
    {-0.681718f,-0.147621f,-0.716567f},
    {-0.850651f,0.000000f,-0.525731f},
    {-0.688191f,0.587785f,-0.425325f},
    {-0.587785f,0.425325f,-0.688191f},
    {-0.425325f,0.688191f,-0.587785f},
    {-0.425325f,-0.688191f,-0.587785f},
    {-0.587785f,-0.425325f,-0.688191f},
    {-0.688197f,-0.587780f,-0.425327f},
}};
constexpr MdlMatrix mdl_identity{1,0,0,0, 0,1,0,0, 0,0,1,0};
Vec3 mdl_normal(uint8_t index) {
    if (index >= mdl_normals.size()) malformed("MDL normal index out of range");
    return mdl_normals[index];
}
MdlMatrix mdl_multiply(const MdlMatrix& a, const MdlMatrix& b) {
    MdlMatrix r{};
    for (size_t row=0; row<3; ++row) {
        for (size_t col=0; col<3; ++col)
            for (size_t k=0; k<3; ++k) r[row*4+col] += a[row*4+k]*b[k*4+col];
        r[row*4+3] = a[row*4+3];
        for (size_t k=0; k<3; ++k) r[row*4+3] += a[row*4+k]*b[k*4+3];
    }
    return r;
}
Vec3 mdl_transform(const MdlMatrix& m, Vec3 v, bool point) {
    return {m[0]*v.x+m[1]*v.y+m[2]*v.z+(point?m[3]:0),
            m[4]*v.x+m[5]*v.y+m[6]*v.z+(point?m[7]:0),
            m[8]*v.x+m[9]*v.y+m[10]*v.z+(point?m[11]:0)};
}
Vec3 mdl_unit(Vec3 v) {
    const float length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    if (length>0) { v.x/=length; v.y/=length; v.z/=length; }
    return v;
}
MdlMatrix mdl_normal_matrix(const MdlMatrix& m) {
    // Inverse transpose, computed once per bone rather than once per weighted vertex.
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if (std::abs(det)<1e-12f) malformed("Singular MDL bone matrix");
    return {(e*i-f*h)/det,(f*g-d*i)/det,(d*h-e*g)/det,0,
            (c*h-b*i)/det,(a*i-c*g)/det,(b*g-a*h)/det,0,
            (b*f-c*e)/det,(c*d-a*f)/det,(a*e-b*d)/det,0};
}
struct MdlSourceVertex { Vec3 position,normal; uint16_t index{65535}; };
struct MdlFace { std::array<uint32_t,3> vertex{},uv{}; uint32_t material{}; };
struct MdlSourceFrame {
    std::string name;
    std::vector<MdlSourceVertex> replacements;
    std::vector<std::pair<uint16_t,MdlMatrix>> matrices;
};
struct MdlGroup {
    std::string name;
    std::vector<Vec2> uv;
    std::vector<MdlFace> faces;
    std::vector<MdlSourceVertex> vertices;
    std::vector<MdlSourceFrame> frames;
    std::vector<std::array<uint32_t,3>> displayFaces;
    std::vector<std::array<uint32_t,2>> displayVertices;
    uint32_t materialBase{},renderBase{},sourceBase{};
    bool replacementFrames{};
};
struct MdlWeight { uint32_t group{},vertex{},bone{}; float weight{}; };
MdlSourceVertex mdl_vertex7(Cursor& c, size_t stride) {
    if (stride!=16 && stride!=26) malformed("Unsupported MDL7 vertex layout");
    MdlSourceVertex v; v.position=c.vec3(); v.index=c.u16();
    if (stride==26) v.normal=c.vec3();
    else { v.normal=mdl_normal(c.u8()); c.skip(1); }
    return v;
}
MdlSourceVertex mdl_packed(Cursor& c, uint32_t packing, Vec3 scale, Vec3 offset, bool normal=true) {
    MdlSourceVertex v;
    Vec3 raw;
    if (packing==0) raw={float(c.u8()),float(c.u8()),float(c.u8())};
    else if (packing==2) raw={float(c.u16()),float(c.u16()),float(c.u16())};
    else malformed("Unsupported MDL frame packing");
    v.position={raw.x*scale.x+offset.x,raw.y*scale.y+offset.y,raw.z*scale.z+offset.z};
    const auto index=c.u8(); if (normal) v.normal=mdl_normal(index);
    if (packing==2) c.skip(1);
    return v;
}
template<class T> void mdl_resize(std::vector<T>& v, size_t count) {
    checked_product(count,sizeof(T)); v.resize(count);
}
// A6 00426aa0 and A7 1002c580 round to powers of two with minimum eight
// (00413990 / 100597e7). One-pixel axes are valid, not UV divisors.
uint32_t mdl_texture_extent(uint32_t dimension) {
    checked_product(dimension,1);
    return std::bit_ceil(std::max(8u,dimension));
}
}

Model Assets::decode_model(std::span<const uint8_t> bytes, std::string_view sourceName) {
    Cursor c{bytes,0}; const auto magic=c.str(4);
    if (magic!="MDL4" && magic!="MDL5" && magic!="MDL7" && magic!="HMP5")
        malformed("Unsupported model format: "+std::string(sourceName));
    checked_product(bytes.size(),1);
    Model model;
    model.formatVersion=magic=="HMP5"?105u:magic=="MDL7"?7u:magic=="MDL5"?5u:4u;
    model.clockwise=true; // Preserved A6 MDL/HMP front winding; WMB render fans are normalized separately.
    // Metadata records are little-endian (original offset, byte length, payload).
    // Preserve unrepresented records without duplicating skin or geometry buffers.
    auto metadata=[&](size_t begin,size_t end) {
        if(end<begin || end>bytes.size()) malformed("Invalid MDL metadata span");
        checked_product(model.metadata.size()+end-begin+8,1);
        for(const auto value:{uint32_t(begin),uint32_t(end-begin)})
            for(unsigned shift=0;shift<32;shift+=8) model.metadata.push_back(uint8_t(value>>shift));
        model.metadata.insert(model.metadata.end(),bytes.begin()+begin,bytes.begin()+end);
    };
    std::vector<MdlGroup> groups;
    std::vector<MdlWeight> sourceWeights;
    std::array<uint32_t,2> skinDimensions{};
    auto skin = [&](uint32_t width,uint32_t height,bool dimensions,bool named,
                    size_t skinStride=28,size_t materialStride=68,size_t colorStride=16) {
        const size_t start=c.p;
        Material material; const uint32_t type=c.u32(); material.flags=type;
        if (dimensions) { width=c.u32(); height=c.u32(); }
        skinDimensions={width,height};
        if (named) { material.name=c.str(16); if(skinStride<28) malformed("Invalid MDL skin layout"); c.skip(skinStride-28); }
        const uint32_t format=type&15;
        if (format==7) {
            size_t length=0;
            while (length<c.remaining() && c.b[c.p+length]!=0) ++length;
            if (length==c.remaining()) malformed("Unterminated external MDL skin");
            material.textureName=c.str(length); c.skip(1);
            material.texture=image_ptr(material.textureName);
        } else if (format!=1 && ((width && height) || !(type&0x30))) {
            if((width && height) || format==6)
                material.texture=embedded_image(bytes,c.p,type,width,height);
            else {
                const auto encoding=type&7;
                if(encoding!=0 && encoding!=2 && encoding!=3 && encoding!=4 && encoding!=5)
                    malformed("unknown empty MDL skin encoding");
            }
        }
        if (type&16) {
            if (colorStride<16 || materialStride<4*colorStride+4) malformed("Invalid MDL material layout");
            const size_t materialStart=c.p;
            auto color=[&]() { Color out{c.f32(),c.f32(),c.f32(),c.f32()}; c.skip(colorStride-16); return out; };
            material.albedo=color(); material.ambient=color(); material.specular=color(); material.emissive=color();
            material.power=c.f32(); material.alpha=material.ambient.a;
            c.seek(materialStart+materialStride);
        }
        if (type&32) { const uint32_t length=c.u32(); c.skip(length); }
        material.raw.assign(bytes.begin()+start,bytes.begin()+c.p);
        return std::pair<Material,uint32_t>{std::move(material),format==1?width:UINT32_MAX};
    };
    if (magic=="MDL7") {
        c.u32(); const uint32_t boneCount=c.u32(),groupCount=c.u32(),dataSize=c.u32();
        const uint32_t entSize=c.u32(),medSize=c.u32();
        const size_t boneStride=c.u16(),skinStride=c.u16(),colorStride=c.u16(),materialStride=c.u16(),uvStride=c.u16(),triangleStride=c.u16(),vertexStride=c.u16(),frameVertexStride=c.u16(),matrixStride=c.u16(),frameStride=c.u16();
        metadata(0,c.p);
        if (dataSize>bytes.size() || entSize>bytes.size()-dataSize || medSize>bytes.size()-dataSize-entSize)
            malformed("Invalid MDL7 data/lump lengths");
        if (!groupCount || boneStride<16 || skinStride<28 || uvStride<8 || frameStride<24 || (triangleStride!=6 && triangleStride!=12 && triangleStride!=16 && triangleStride!=26)) malformed("Unsupported MDL7 header layout");
        if (checked_product(boneCount,boneStride)>c.remaining()) malformed("Truncated MDL7 bones");
        mdl_resize(model.bones,boneCount);
        for (auto& bone:model.bones) {
            const size_t start=c.p; const uint16_t parent=c.u16(); c.skip(2);
            bone.parent=parent==65535?-1:parent; bone.transform=mdl_identity;
            const Vec3 position=c.vec3(); bone.transform[3]=position.x; bone.transform[7]=position.y; bone.transform[11]=position.z;
            bone.name=c.str(boneStride-16); c.seek(start+boneStride);
            metadata(start,c.p);
            if (bone.parent>=int32_t(boneCount)) malformed("MDL bone parent out of range");
        }
        if (checked_product(groupCount,44)>c.remaining()) malformed("Truncated MDL7 groups");
        mdl_resize(groups,groupCount);
        std::array<uint32_t,2> bonusSkinDimensions{};
        for (uint32_t groupIndex=0; groupIndex<groupCount; ++groupIndex) {
            const size_t groupStart=c.p;
            auto& group=groups[groupIndex]; const uint8_t groupType=c.u8(),deformerCount=c.u8(); c.skip(2);
            c.u32(); group.name=c.str(16);
            const uint32_t skins=c.u32(),uvs=c.u32(),triangles=c.u32(),vertices=c.u32(),frames=c.u32();
            metadata(groupStart,c.p);
            if (groupType!=1 && groupType!='1') malformed("Unsupported MDL7 non-triangle group");
            group.materialBase=uint32_t(model.materials.size());
            std::vector<uint32_t> references; references.reserve(skins);
            if (checked_product(skins,skinStride)>c.remaining()) malformed("Truncated MDL7 skins");
            std::array<uint32_t,2> firstSkinDimensions{};
            for (uint32_t j=0;j<skins;++j) {
                auto [material,reference]=skin(0,0,true,true,skinStride,materialStride,colorStride);
                if(!j)firstSkinDimensions=skinDimensions;
                // A7 1005ef60 captures the first decoded bitmap once per model.
                if(bonusProfile_ && !bonusSkinDimensions[0] && material.texture)
                    bonusSkinDimensions={material.texture->width,material.texture->height};
                model.materials.push_back(std::move(material)); references.push_back(reference);
            }
            for (uint32_t j=0;j<skins;++j) {
                uint32_t ref=references[j],steps=0;
                while(ref!=UINT32_MAX) {
                    if(ref>=skins || ++steps>skins) malformed("Invalid MDL7 skin reference");
                    auto& target=model.materials[group.materialBase+j]; const auto& origin=model.materials[group.materialBase+ref];
                    target.texture=origin.texture; target.textureName=origin.textureName;
                    if (!(target.flags&16)) { target.albedo=origin.albedo; target.ambient=origin.ambient; target.specular=origin.specular; target.emissive=origin.emissive; target.power=origin.power; target.alpha=origin.alpha; }
                    ref=references[ref];
                }
            }
            if (checked_product(uvs,uvStride)>c.remaining()) malformed("Truncated MDL7 UVs");
            mdl_resize(group.uv,uvs);
            Vec2 uvScale{1,1};
            const auto& dimensions=bonusProfile_?bonusSkinDimensions:firstSkinDimensions;
            if(bonusProfile_?dimensions[0]!=0:skins!=0)
                uvScale={float(dimensions[0])/float(mdl_texture_extent(dimensions[0])),
                         float(dimensions[1])/float(mdl_texture_extent(dimensions[1]))};
            for (auto& uv:group.uv) { uv={c.f32()*uvScale.x,c.f32()*uvScale.y}; c.skip(uvStride-8); }
            if (checked_product(triangles,triangleStride)>c.remaining()) malformed("Truncated MDL7 triangles");
            mdl_resize(group.faces,triangles);
            for(auto& face:group.faces) {
                const size_t start=c.p;
                for(auto& index:face.vertex) index=c.u16();
                if(triangleStride>=12) for(auto& index:face.uv) {
                    index=c.u16();
                    // A7 10158a70/10158aa3/10158ad7 widen only 0xffff to absent UV.
                    if(bonusProfile_ && index==65535)index=UINT32_MAX;
                }
                // A6 005b6a80 exposes only six indices. A7 101589eb additionally
                // retains +12 as a raw face attribute, not a group-local skin.
                face.material=bonusProfile_?UINT32_MAX:0;
                if(bonusProfile_ && triangleStride>=16)face.material=c.u32();
                c.seek(start+triangleStride);
            }
            if (checked_product(vertices,vertexStride)>c.remaining()) malformed("Truncated MDL7 vertices");
            mdl_resize(group.vertices,vertices);
            for(auto& vertex:group.vertices) vertex=mdl_vertex7(c,vertexStride);
            if (checked_product(frames,frameStride)>c.remaining()) malformed("Truncated MDL7 frames");
            mdl_resize(group.frames,frames); group.replacementFrames=true;
            for(auto& frame:group.frames) {
                const size_t frameStart=c.p;
                frame.name=c.str(16); const uint32_t count=c.u32(),matrixCount=c.u32(); c.skip(frameStride-24);
                metadata(frameStart,c.p);
                if (checked_product(count,frameVertexStride)>c.remaining()) malformed("Truncated MDL7 animated vertices");
                mdl_resize(frame.replacements,count);
                for(auto& vertex:frame.replacements) { vertex=mdl_vertex7(c,frameVertexStride); if(vertex.index>=vertices) malformed("MDL animated vertex index out of range"); }
                if(matrixCount && matrixStride!=68) malformed("Unsupported MDL7 matrix layout");
                if (checked_product(matrixCount,matrixStride)>c.remaining()) malformed("Truncated MDL7 bone keys");
                frame.matrices.reserve(matrixCount);
                for(uint32_t j=0;j<matrixCount;++j) {
                    const size_t matrixStart=c.p;
                    std::array<float,16> raw{};
                    for(auto& value:raw) value=c.f32();
                    const uint16_t bone=c.u16(); c.skip(2); if(bone>=boneCount) malformed("MDL matrix bone index out of range");
                    const MdlMatrix matrix{raw[0],raw[4],raw[8],raw[12], raw[1],raw[5],raw[9],raw[13], raw[2],raw[6],raw[10],raw[14]};
                    frame.matrices.emplace_back(bone,matrix);
                    metadata(matrixStart,c.p);
                }
            }
            for(uint32_t j=0;j<deformerCount;++j) {
                const size_t start=c.p; const auto version=c.u8(),type=c.u8(); c.skip(2);
                const uint32_t targetGroup=c.u32(),elements=c.u32(),dataLength=c.u32();
                if(version!=0 || type!=0 || targetGroup>=groupCount) malformed("Unsupported MDL deformer");
                if(checked_product(elements,28)>c.remaining()) malformed("Truncated MDL deformer elements");
                for(uint32_t k=0;k<elements;++k) {
                    const uint32_t bone=c.u32(); c.skip(20); const uint32_t weights=c.u32();
                    if(bone>=boneCount || checked_product(weights,8)>c.remaining()) malformed("Invalid MDL deformer weights");
                    for(uint32_t q=0;q<weights;++q) { const uint32_t vertex=c.u32(); const float weight=c.f32(); if(!std::isfinite(weight) || weight<0) malformed("Invalid MDL vertex weight"); sourceWeights.push_back({targetGroup,vertex,bone,weight}); }
                }
                if(dataLength && dataLength<c.p-start) malformed("Invalid MDL deformer length");
                if(dataLength) c.seek(start+dataLength);
                metadata(start,c.p);
            }
        }
        if(c.p>dataSize) malformed("MDL7 data exceeds declared length");
        if(c.p<bytes.size()) metadata(c.p,bytes.size());
    } else {
        c.u32(); const Vec3 scale=c.vec3(),offset=c.vec3(); c.f32(); const Vec3 eye=c.vec3();
        const uint32_t skins=c.u32(),width=c.u32(),height=c.u32(),vertices=c.u32(),triangles=c.u32(),frames=c.u32(),uvs=c.u32();
        model.flags=c.u32(); c.u32();
        metadata(0,c.p);
        if(!skins || skins>256 || !vertices || vertices>65535 || !frames || frames>4096 ||
           (magic!="HMP5" && (!triangles || !uvs)))
            malformed("Invalid native legacy MDL record counts");
        checked_product(vertices,sizeof(MdlSourceVertex));
        groups.resize(1); auto& group=groups.front(); group.name=std::string(sourceName);
        std::array<uint32_t,2> firstSkinDimensions{width,height};
        for(uint32_t j=0;j<skins;++j) {
            auto [material,reference]=skin(width,height,magic!="MDL4",false);
            if(reference!=UINT32_MAX) malformed("Invalid legacy MDL skin reference");
            if(!j)firstSkinDimensions=skinDimensions;
            model.materials.push_back(std::move(material));
        }
        if(magic=="HMP5") {
            const float gridWidth=eye.z;
            if(!std::isfinite(gridWidth) || gridWidth<2 || gridWidth>float(vertices) || gridWidth!=std::floor(gridWidth) || !std::isfinite(eye.x) || !std::isfinite(eye.y) || eye.x==0 || eye.y==0) malformed("Invalid HMP5 dimensions");
            const uint32_t nx=uint32_t(gridWidth),ny=vertices/nx;
            if(ny<2 || checked_product(nx,ny)!=vertices) malformed("Invalid HMP5 grid size");
            model.terrainWidth=nx;model.terrainHeight=ny;model.terrainSpacing={eye.x,eye.y};
            mdl_resize(group.vertices,vertices); mdl_resize(group.uv,vertices);
            for(uint32_t y=0;y<ny;++y) for(uint32_t x=0;x<nx;++x) group.uv[y*nx+x]={float(x)/float(nx-1),float(y)/float(ny-1)};
            checked_product(checked_product(nx-1,ny-1),2*sizeof(MdlFace));
            group.faces.reserve(size_t(nx-1)*(ny-1)*2);
            for(uint32_t y=0;y<ny-1;++y) for(uint32_t x=0;x<nx-1;++x) {
                const uint32_t a=y*nx+x,b=(y+1)*nx+x,d=y*nx+x+1,e=(y+1)*nx+x+1;
                MdlFace first,second; first.vertex=first.uv={a,b,e}; second.vertex=second.uv={a,e,d}; group.faces.push_back(first); group.faces.push_back(second);
            }
            mdl_resize(group.frames,frames);
            for(auto& frame:group.frames) {
                const size_t frameStart=c.p;
                const uint32_t packing=c.u32(); if(packing!=2) malformed("Unsupported HMP5 frame packing");
                c.skip(16); frame.name=c.str(16);
                metadata(frameStart,c.p);
                if(checked_product(vertices,4)>c.remaining()) malformed("Truncated HMP5 heights");
                mdl_resize(frame.replacements,vertices);
                for(uint32_t y=0;y<ny;++y) for(uint32_t x=0;x<nx;++x) {
                    auto& v=frame.replacements[y*nx+x]; const uint16_t z=c.u16(); v.normal=mdl_normal(c.u8()); c.skip(1);
                    v.position={offset.x+float(x)*eye.x,offset.y+float(y)*eye.y,float(z)*scale.z+offset.z};
                }
            }
            group.vertices=std::move(group.frames.front().replacements);
        } else {
            if(checked_product(uvs,4)>c.remaining()) malformed("Truncated MDL UVs");
            mdl_resize(group.uv,uvs);
            const float skinWidth=float(mdl_texture_extent(firstSkinDimensions[0]));
            const float skinHeight=float(mdl_texture_extent(firstSkinDimensions[1]));
            for(auto& uv:group.uv) uv={float(int16_t(c.u16()))/skinWidth,float(int16_t(c.u16()))/skinHeight};
            if(checked_product(triangles,12)>c.remaining()) malformed("Truncated MDL triangles");
            mdl_resize(group.faces,triangles);
            for(auto& face:group.faces) {for(auto& i:face.vertex)i=c.u16();for(auto& i:face.uv)i=c.u16();}
            mdl_resize(group.frames,frames);
            for(auto& frame:group.frames) {
                const size_t frameStart=c.p;
                const uint32_t packing=c.u32();
                mdl_packed(c,packing,scale,offset,false); mdl_packed(c,packing,scale,offset,false); frame.name=c.str(16);
                metadata(frameStart,c.p);
                if(checked_product(vertices,packing==2?8:4)>c.remaining()) malformed("Truncated MDL frame vertices");
                mdl_resize(frame.replacements,vertices);
                for(auto& vertex:frame.replacements) vertex=mdl_packed(c,packing,scale,offset);
            }
            group.vertices=std::move(group.frames.front().replacements);
        }
        if(c.p<bytes.size()) metadata(c.p,bytes.size());
    }
    size_t renderCount=0,frameCount=0,triangleCount=0,sourceCount=0;
    for(auto& group:groups) {
        group.renderBase=uint32_t(renderCount);
        group.sourceBase=uint32_t(sourceCount);sourceCount+=group.vertices.size();
        if(sourceCount>UINT32_MAX) malformed("Model source index overflow");
        std::map<std::array<uint32_t,2>,uint32_t> split;
        mdl_resize(group.displayFaces,group.faces.size());
        for(size_t t=0;t<group.faces.size();++t) for(size_t corner=0;corner<3;++corner) {
            const auto& face=group.faces[t];
            const std::array<uint32_t,2> key{face.vertex[corner],group.uv.empty()?UINT32_MAX:face.uv[corner]};
            if(key[0]>=group.vertices.size() || (key[1]!=UINT32_MAX && key[1]>=group.uv.size())) malformed("MDL triangle index out of range");
            auto [entry,inserted]=split.emplace(key,uint32_t(group.displayVertices.size()));
            if(inserted) group.displayVertices.push_back(key);
            group.displayFaces[t][corner]=entry->second;
        }
        renderCount+=group.displayVertices.size();
        triangleCount+=group.faces.size(); frameCount=std::max(frameCount,group.frames.size());
        if(renderCount>UINT32_MAX) malformed("Model render index overflow");
    }
    checked_product(checked_product(frameCount,renderCount),2*sizeof(Vec3));
    checked_product(checked_product(frameCount,sourceCount),2*sizeof(Vec3));
    mdl_resize(model.vertices,renderCount); model.triangles.reserve(triangleCount); mdl_resize(model.frames,frameCount);
    mdl_resize(model.sourceVertices,sourceCount);mdl_resize(model.vertexSources,renderCount);model.sourceTriangles.reserve(triangleCount);
    for(auto& frame:model.frames) {mdl_resize(frame.positions,renderCount);mdl_resize(frame.normals,renderCount);mdl_resize(frame.sourcePositions,sourceCount);mdl_resize(frame.sourceNormals,sourceCount);}
    // Build source-indexed weights once, then copy them to each UV-split display vertex.
    std::vector<std::vector<std::vector<std::pair<uint32_t,float>>>> weights(groups.size());
    if(!model.bones.empty()) for(size_t g=0;g<groups.size();++g) mdl_resize(weights[g],groups[g].vertices.size());
    for(const auto& w:sourceWeights) {if(w.group>=groups.size() || w.vertex>=weights[w.group].size()) malformed("MDL weight vertex out of range");weights[w.group][w.vertex].emplace_back(w.bone,w.weight);}
    if(!model.bones.empty()) for(size_t g=0;g<groups.size();++g) for(size_t v=0;v<groups[g].vertices.size();++v) {
        const auto bone=groups[g].vertices[v].index;
        if(weights[g][v].empty() && bone!=65535 && !model.bones.empty()) {if(bone>=model.bones.size()) malformed("MDL vertex bone out of range");weights[g][v].emplace_back(bone,1.f);}
    }
    // Header bone xyz are parent-relative bind positions. Frame matrices are
    // model-space deformation deltas around absolute pivots, not local bind poses:
    // the official SDK example stores identity matrices for every bone at rest.
    // Keep local bind translations in Bone.transform; never subtract them again.
    std::vector<uint8_t> visit(model.bones.size());
    std::vector<uint32_t> boneOrder;boneOrder.reserve(model.bones.size());
    for(size_t b=0;b<model.bones.size();++b) {
        const size_t start=boneOrder.size();int32_t node=int32_t(b);
        while(node>=0 && visit[node]==0) {visit[node]=1;boneOrder.push_back(uint32_t(node));node=model.bones[node].parent;}
        if(node>=0 && visit[node]==1) malformed("Cyclic MDL bone hierarchy");
        std::reverse(boneOrder.begin()+start,boneOrder.end());
        for(size_t j=start;j<boneOrder.size();++j)visit[boneOrder[j]]=2;
    }
    std::vector<MdlMatrix> local(model.bones.size(),mdl_identity);
    for(size_t f=0;f<frameCount;++f) {
        auto& out=model.frames[f];
        for(const auto& group:groups) if(f<group.frames.size()) {
            const auto& frame=group.frames[f]; if(out.name.empty())out.name=frame.name;
            for(const auto& [bone,matrix]:frame.matrices)local[bone]=matrix;
        }
        std::vector<MdlMatrix> skinMatrices(local.size());
        for(const auto b:boneOrder) {
            const auto parent=model.bones[b].parent;
            skinMatrices[b]=parent<0?local[b]:mdl_multiply(skinMatrices[parent],local[b]);
        }
        std::vector<MdlMatrix> normalMatrices;normalMatrices.reserve(skinMatrices.size());
        for(const auto& matrix:skinMatrices)normalMatrices.push_back(mdl_normal_matrix(matrix));
        // Complete model-space skin matrices, row-major affine 3x4. Raw keys
        // remain lossless in metadata. Geometry below is already skinned once.
        out.boneTransforms=skinMatrices;
        for(size_t g=0;g<groups.size();++g) {
            const auto& group=groups[g];
            std::vector<MdlSourceVertex> replaced;
            const std::vector<MdlSourceVertex>* current=&group.vertices;
            if(!group.frames.empty()) {
                const auto& frame=group.frames[std::min(f,group.frames.size()-1)];
                if(group.replacementFrames && !frame.replacements.empty()) {
                    replaced=group.vertices;
                    for(const auto& vertex:frame.replacements) {replaced[vertex.index].position=vertex.position;replaced[vertex.index].normal=vertex.normal;}
                    current=&replaced;
                } else if(!group.replacementFrames && !frame.replacements.empty()) current=&frame.replacements;
            }
            for(size_t source=0;source<current->size();++source) {
                const auto& vertex=(*current)[source];Vec3 p=vertex.position,n=vertex.normal;
                if(!model.bones.empty() && !weights[g][source].empty()) {
                    p={};n={};float sum=0;
                    for(const auto& [bone,weight]:weights[g][source]) {const auto wp=mdl_transform(skinMatrices[bone],vertex.position,true),wn=mdl_transform(normalMatrices[bone],vertex.normal,false);p.x+=wp.x*weight;p.y+=wp.y*weight;p.z+=wp.z*weight;n.x+=wn.x*weight;n.y+=wn.y*weight;n.z+=wn.z*weight;sum+=weight;}
                    if(sum<=0)malformed("MDL vertex has zero total bone weight");p.x/=sum;p.y/=sum;p.z/=sum;n=mdl_unit(n);
                }
                out.sourcePositions[group.sourceBase+source]=p;out.sourceNormals[group.sourceBase+source]=n;
            }
            for(size_t v=0;v<group.displayVertices.size();++v) {
                const auto source=group.sourceBase+group.displayVertices[v][0],render=group.renderBase+uint32_t(v);
                out.positions[render]=out.sourcePositions[source];out.normals[render]=out.sourceNormals[source];
            }
        }
        model.frameNames.push_back(out.name);
    }
    for(size_t g=0;g<groups.size();++g) {
        const auto& group=groups[g];
        for(size_t source=0;source<group.vertices.size();++source) {
            auto& vertex=model.sourceVertices[group.sourceBase+source];
            vertex.position=frameCount?model.frames[0].sourcePositions[group.sourceBase+source]:group.vertices[source].position;
            vertex.normal=frameCount?model.frames[0].sourceNormals[group.sourceBase+source]:group.vertices[source].normal;
        }
        for(size_t v=0;v<group.displayVertices.size();++v) {
            const auto& display=group.displayVertices[v]; const auto source=display[0];
            const uint32_t render=group.renderBase+uint32_t(v);
            model.vertexSources[render]=group.sourceBase+source;
            auto& vertex=model.vertices[render]; vertex.position=frameCount?model.frames[0].positions[render]:group.vertices[source].position;vertex.normal=frameCount?model.frames[0].normals[render]:group.vertices[source].normal;
            if(display[1]!=UINT32_MAX) vertex.uv=group.uv[display[1]];
            if(!model.bones.empty()) for(const auto& [bone,weight]:weights[g][source]) model.weights.push_back({render,bone,weight});
        }
        for(size_t t=0;t<group.faces.size();++t) {
            const auto& face=group.faces[t]; Triangle triangle;triangle.materialIndex=face.material;
            std::array<uint32_t,3> original{group.sourceBase+face.vertex[0],group.sourceBase+face.vertex[1],group.sourceBase+face.vertex[2]};
            model.sourceTriangles.push_back(original);
            for(size_t corner=0;corner<3;++corner) triangle.indices[corner]=group.renderBase+group.displayFaces[t][corner];
            model.triangles.push_back(triangle);
        }
    }
    if(bonusProfile_ && model.formatVersion==7) {
        // 1005f1c0 marks only base-group primary faces; optional LOD data is
        // not decoded here. 1005ef60 type1 aliases share the bitmap descriptor.
        std::unordered_set<const Image*> primaryImages;
        for(const auto& triangle:model.triangles) {
            const auto attribute=std::bit_cast<int32_t>(triangle.materialIndex);
            const size_t bank=attribute<0?0:size_t(attribute);
            if(bank>=model.materials.size())continue; // Preserve unbound raw attributes.
            auto& material=model.materials[bank];
            if(material.texture && !material.nativePrimarySubset) {
                material.nativePrimarySubset=true;primaryImages.insert(material.texture.get());
            }
        }
        for(auto& material:model.materials)
            if(material.texture && primaryImages.contains(material.texture.get()))
                material.nativePrimarySubset=true;
    }
    if(!model.sourceVertices.empty()) {
        model.min=model.max=model.sourceVertices.front().position;
        auto bounds=[&](Vec3 p) {model.min.x=std::min(model.min.x,p.x);model.min.y=std::min(model.min.y,p.y);model.min.z=std::min(model.min.z,p.z);model.max.x=std::max(model.max.x,p.x);model.max.y=std::max(model.max.y,p.y);model.max.z=std::max(model.max.z,p.z);};
        for(const auto& vertex:model.sourceVertices)bounds(vertex.position);
        model.resourceMin=model.min;model.resourceMax=model.max; // sourceVertices hold frame 0 (see the frame decode above).
        for(const auto& frame:model.frames)for(const auto& p:frame.sourcePositions)bounds(p);
    }
    return model;
}
}
