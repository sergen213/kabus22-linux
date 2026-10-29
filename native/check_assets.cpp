#include "assets.hpp"
#include "mods.hpp"
#include "model_gltf.hpp"
#include <memory>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <unistd.h>

namespace {
void tiny_archive(const std::filesystem::path& path,std::string_view member,std::string_view value,unsigned version=2) {
    if(member.size()>32)throw std::runtime_error("check archive member name too long");
    if(version!=2&&version!=3)throw std::runtime_error("unsupported check archive version");
    std::ofstream out(path,std::ios::binary);out.write(version==2?"WRS2":"WRS3",4);
    char header[40]{};
    for(size_t i=0;i<member.size();++i)header[i]=char(static_cast<unsigned char>(member[i])^(version==2?0xa5:0xb5));
    const auto packed=uint32_t(value.size()+(value.size()+7)/8),size=uint32_t(value.size());
    for(unsigned i=0;i<4;++i){header[32+i]=char(packed>>(24-8*i));header[36+i]=char(size>>(24-8*i));}
    out.write(header,sizeof(header));
    for(size_t at=0;at<value.size();at+=8) {
        out.put(char(0xff));out.write(value.data()+at,std::min(size_t{8},value.size()-at));
    }
    if(!out)throw std::runtime_error("cannot write check archive");
}
void tiny_archive(const std::filesystem::path& path,std::string_view member,char value,unsigned version=2) {
    tiny_archive(path,member,std::string_view(&value,1),version);
}
template<class Exception=std::runtime_error,class F> void rejects(F operation) {
    bool rejected=false;try{operation();}catch(const Exception&){rejected=true;}
    if(!rejected)throw std::runtime_error("malformed resource operation accepted");
}
}

int main(int argc,char** argv) {
    using k22::Assets;
    const std::array<uint8_t,4> literals{7,'A','B','C'};
    if(Assets::decompress(literals,3)!=std::vector<uint8_t>{'A','B','C'})throw std::runtime_error("literal decoding");
    // Absolute ring reference starts at the literal cursor and overlaps itself.
    const std::array<uint8_t,4> overlap{1,'A',0xee,0xf2};
    if(Assets::decompress(overlap,6)!=std::vector<uint8_t>(6,'A'))throw std::runtime_error("overlapping ring reference");
    auto reject=[](std::span<const uint8_t> bytes,size_t size){
        bool rejected=false;try{(void)Assets::decompress(bytes,size);}catch(const std::runtime_error&){rejected=true;}
        if(!rejected)throw std::runtime_error("malformed LZSS accepted");
    };
    reject(std::array<uint8_t,1>{0},0);
    reject(std::array<uint8_t,2>{0,0},3);
    reject(overlap,5);
    reject(literals,4);
    reject(literals,2);
    reject({},129ULL*1024*1024);
    const auto root=std::filesystem::temp_directory_path()/("k22-assets-check-"+std::to_string(::getpid()));
    if(!std::filesystem::create_directory(root))throw std::runtime_error("cannot create exclusive check directory");
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::filesystem::remove_all(path);} } cleanup{root};
    std::array<uint8_t,140> badWorld{};badWorld[0]='W';badWorld[1]='M';badWorld[2]='B';badWorld[3]='6';
    // First section claims an offset outside the actual140-byte header.
    badWorld[4]=255;badWorld[8]=1;
    {std::ofstream file(root/"Bad.WMB",std::ios::binary);file.write(reinterpret_cast<const char*>(badWorld.data()),badWorld.size());}
    Assets assets(root);
    if(!assets.contains("bAD.wmb"))throw std::runtime_error("case-insensitive loose lookup");
    bool rejected=false;try{(void)assets.world("bad.wmb");}catch(const std::runtime_error&){rejected=true;}
    if(!rejected)throw std::runtime_error("out-of-range WMB section accepted");
    rejected=false;try{(void)assets.read("../Bad.WMB");}catch(const std::runtime_error&){rejected=true;}
    if(!rejected)throw std::runtime_error("escaping loose resource accepted");
    {
        for(auto text:{"tr","en","de"})tiny_archive(root/(std::string("yazi_")+text+".k22"),"text.txt",text[0]);
        for(auto audio:{"tr","en"})tiny_archive(root/(std::string("ses_")+audio+".k22"),"audio.txt",audio[0]);
        tiny_archive(root/"patch.k22","text.txt",'p');
        tiny_archive(root/"paths.k22","dir//text.txt",'s');
        tiny_archive(root/"spaces.k22","dir/two words.txt",'i');
        tiny_archive(root/"mode.wrs","mode.txt",'m',3);
        tiny_archive(root/"mode_patch.wrs","mode.txt",'p',3);
        {std::ofstream broken(root/"unknown.wrs",std::ios::binary);broken.write("WRS4",4);}
        Assets locale(root);
        rejects([&]{locale.mount("turkce.k22");});
        locale.mount("mode.wrs");
        if(locale.text("mode.txt")!="m")throw std::runtime_error("WRS3 archive member decoding");
        // A6 0041fd20 appends every member; 00420120 returns the first match, so a later archive never replaces it.
        locale.mount("mode_patch.wrs");locale.mount("mode.wrs");
        if(locale.text("mode.txt")!="m")throw std::runtime_error("campaign later archive replaced earlier member");
        Assets bonus(root,true);bonus.mount("mode.wrs");bonus.mount("mode_patch.wrs");bonus.mount("mode.wrs");
        if(bonus.text("mode.txt")!="m")throw std::runtime_error("Bonus later archive replaced earlier member");
        Assets patched(root);patched.mount("mode_patch.wrs");patched.mount("mode.wrs");
        if(patched.text("mode.txt")!="p")throw std::runtime_error("first-mounted (start-up) archive lost priority");
        rejects([&]{locale.mount("unknown.wrs");});
        locale.mount("YAZI_EN.K22");locale.mount("ses_tr.k22");
        if(locale.text("text.txt")!="e"||locale.text("audio.txt")!="t")throw std::runtime_error("explicit independent locale archives");
        rejects([&]{(void)locale.read("multi_lang.wdl");});
        rejects([&]{locale.mount("language_english.wrs");});
        locale.mount("patch.k22");locale.mount("yazi_en.k22");
        if(locale.text("text.txt")!="e")throw std::runtime_error("later archive replaced first-mounted member");
        locale.mount("paths.k22");
        if(locale.text("dir////text.txt")!="s"||locale.text("dir\\text.txt")!="s")throw std::runtime_error("repeated archive member separators");
        if(locale.text("dir/text.txt . . ")!="s")throw std::runtime_error("terminal DOS filename spaces and periods");
        locale.mount("spaces.k22");
        if(locale.text("dir/two words.txt . . ")!="i")throw std::runtime_error("interior filename space changed");
        rejects([&]{(void)locale.read("dir/twowords.txt");});
        for(std::string_view name:std::array<std::string_view,12>{"dir/../text.txt","dir//../text.txt","dir\\..\\text.txt","text\n.txt","text\x7f.txt",std::string_view("text\0.txt",9),".",".. ","sub/.. . ","text.txt\t","text.txt \t","dir/\ttext.txt"}) {
            rejects([&]{(void)locale.read(name);});
            rejects([&]{(void)locale.archive_text(name);});
            rejects([&]{locale.mount(name);});
        }
        tiny_archive(root/"turkce.k22","text.txt",'o');
        Assets original(root);original.mount("TURKCE.K22");
        if(original.text("text.txt")!="o"||original.contains("audio.txt"))throw std::runtime_error("actual turkce archive changed");
        std::filesystem::remove(root/"turkce.k22");
        Assets incomplete(root);rejects([&]{incomplete.mount("turkce.k22");});
        {std::ofstream broken(root/"ses_tr.k22",std::ios::binary);broken.write("RIFF",4);}
        Assets corrupt(root);rejects([&]{corrupt.mount("ses_tr.k22");});
    }
    {   // Mods (docs/MODDING.md): data/ files precede loose files and archives; the first added mod wins.
        const auto game=std::filesystem::path(root.string()+"-mods");if(!std::filesystem::create_directory(game))throw std::runtime_error("cannot create mod check directory");
        Cleanup modCleanup{game};const auto data=game/"root";std::filesystem::create_directories(data);
        auto put=[](const std::filesystem::path& p,std::string_view v){std::filesystem::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<v;};
        tiny_archive(data/"a.wrs","packed.txt","archive",3);put(data/"Loose.txt","loose");
        put(game/"one/mod.txt","kabus22-mod 1\r\nname\tOne\r\nversion\t1.0\n\nauthor\tme\ndescription\td\n");put(game/"one/readme.txt","r");
        put(game/"one/data/PACKED.txt","one");put(game/"one/data/loose.txt","one-loose");put(game/"one/data/Sub/New.txt","new");
        put(game/"two/mod.txt","kabus22-mod 1\nname\tTwo");put(game/"two/data/packed.txt","two");put(game/"two/data/only2.txt","2");
        std::filesystem::create_symlink(data/"Loose.txt",game/"two/data/link.txt");
        Assets plain(data);plain.mount("a.wrs");const auto before=plain.names();
        if(plain.text("packed.txt")!="archive"||plain.text("loose.txt")!="loose"||before.size()!=3)throw std::runtime_error("no-mod resources changed");
        auto one=k22::load_mod(game/"one");
        if(!one.valid||one.id!="one"||one.name!="One"||one.version!="1.0"||one.author!="me"||one.description!="d"||!k22::load_mod(game/"two").valid)throw std::runtime_error("valid mod.txt rejected: "+one.detail);
        Assets modded(data);
        if(modded.add_mod(game/"one/data")!=3||modded.add_mod(game/"two/data")!=2)throw std::runtime_error("mod file count (symlink counted?)");
        modded.mount("a.wrs");
        if(modded.text("packed.txt")!="one"||modded.text("LOOSE.TXT")!="one-loose"||modded.text("sub\\new.txt")!="new"||modded.text("only2.txt")!="2"||modded.archive_text("packed.txt")!="one")throw std::runtime_error("mod overlay priority");
        if(modded.contains("link.txt")||modded.contains("readme.txt")||modded.contains("mod.txt")||!modded.contains("Sub/New.txt"))throw std::runtime_error("mod data/ visibility");
        auto names=modded.names();
        if(names.size()!=before.size()+2||!std::binary_search(names.begin(),names.end(),std::string("sub/new.txt"))||!std::binary_search(names.begin(),names.end(),std::string("only2.txt")))throw std::runtime_error("mod names");
        Assets bonus(data,true);bonus.add_mod(game/"one/data");bonus.mount("a.wrs");
        if(bonus.archive_text("packed.txt")!="archive"||bonus.text("packed.txt")!="one")throw std::runtime_error("Bonus scripts must stay archive-only");
        put(game/"dup/data/A.txt","a");put(game/"dup/data/a.TXT","b");
        rejects([&]{Assets(data).add_mod(game/"dup/data");});
        std::filesystem::create_directories(game/"bad/data");
        auto invalid=[&](std::string_view manifest,std::string_view detail){
            put(game/"bad/mod.txt",manifest);auto m=k22::load_mod(game/"bad");
            if(m.valid||m.detail.find(detail)==std::string::npos)throw std::runtime_error("invalid mod.txt accepted or misreported: "+std::string(manifest)+" -> "+m.detail);
        };
        invalid("kabus22-mod 2\nname\tx\n","header");invalid("","header");invalid("kabus22-mod 1\n","name");invalid("kabus22-mod 1\nname\t\n","name");
        invalid("kabus22-mod 1\nname\ta\nname\tb\n","duplicate name");invalid("kabus22-mod 1\nname\ta\nlicense\tb\n","unknown key");invalid("kabus22-mod 1\nname a\n","key<TAB>value");
        put(game/"bad/mod.txt","kabus22-mod 1\nname\tok\n");if(!k22::load_mod(game/"bad").valid)throw std::runtime_error("minimal mod rejected");
        std::filesystem::remove(game/"bad/data");if(k22::load_mod(game/"bad").detail!="missing data/ directory")throw std::runtime_error("mod without data/ accepted");
        std::filesystem::create_directories(game/"link/data");std::filesystem::create_symlink(game/"bad/mod.txt",game/"link/mod.txt");
        if(k22::load_mod(game/"link").valid||k22::load_mod(game/"missing").valid||k22::load_mod(game/"-x").valid)throw std::runtime_error("symlinked/missing/invalid-id mod accepted");
        const char* xdg=std::getenv("XDG_DATA_HOME");const std::string saved=xdg?xdg:"";
        ::setenv("XDG_DATA_HOME","/xdg",1);if(k22::mods_root()!="/xdg/kabus22/mods")throw std::runtime_error("mods root");
        ::setenv("XDG_DATA_HOME","relative",1);if(k22::mods_root().string().starts_with("relative"))throw std::runtime_error("relative XDG_DATA_HOME used");
        if(xdg)::setenv("XDG_DATA_HOME",saved.c_str(),1);else ::unsetenv("XDG_DATA_HOME");
    }
    {   // Mod glTF model replacement (model_gltf.hpp): round trip, custom morph targets, wrap deform, .mdl+.gltf rejection.
        const auto game=std::filesystem::path(root.string()+"-gltf");if(!std::filesystem::create_directory(game))throw std::runtime_error("cannot create glTF check directory");
        Cleanup gltfCleanup{game};const auto data=game/"root";std::filesystem::create_directories(data);
        // MDL5 quad, 2x2 BGR skin, two byte-packed frames: frame 1 moves vertex 1 (triangle 0,1,2 only).
        std::vector<uint8_t> mdl;auto word=[&](uint32_t v){for(unsigned s=0;s<32;s+=8)mdl.push_back(uint8_t(v>>s));};
        mdl.insert(mdl.end(),{'M','D','L','5'});word(5);
        for(float v:{1.f,1.f,1.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f})word(std::bit_cast<uint32_t>(v));
        for(uint32_t v:{1u,2u,2u,4u,2u,2u,4u,0u,0u})word(v);
        word(4);word(2);word(2);for(uint8_t p=0;p<4;++p)mdl.insert(mdl.end(),{uint8_t(10+p),uint8_t(20+p),uint8_t(30+p)});
        for(int16_t v:{0,0,2,0,2,2,0,2})mdl.insert(mdl.end(),{uint8_t(v),uint8_t(uint16_t(v)>>8)});
        for(uint16_t v:{0,1,2,0,1,2,0,2,3,0,2,3})mdl.insert(mdl.end(),{uint8_t(v),uint8_t(v>>8)});
        for(int f=0;f<2;++f){
            word(0);mdl.insert(mdl.end(),{0,0,0,0,20,10,3,0});mdl.push_back('a');mdl.push_back(uint8_t('0'+f));mdl.resize(mdl.size()+14);
            const std::array<std::array<uint8_t,3>,4> v{{{0,0,0},{uint8_t(f?20:10),uint8_t(f?5:0),uint8_t(f?3:0)},{10,10,0},{0,10,0}}};
            for(const auto& p:v)mdl.insert(mdl.end(),{p[0],p[1],p[2],0});
        }
        {std::ofstream(data/"quad.mdl",std::ios::binary).write(reinterpret_cast<const char*>(mdl.data()),std::streamsize(mdl.size()));}
        Assets plain(data);const auto& original=plain.model("quad.mdl");
        if(original.frames.size()!=2||original.vertices.size()!=4||!original.materials[0].texture)throw std::runtime_error("glTF check MDL5 fixture");
        auto close=[](k22::Vec3 a,k22::Vec3 b,float e){return std::abs(a.x-b.x)<=e&&std::abs(a.y-b.y)<=e&&std::abs(a.z-b.z)<=e;};
        std::vector<std::unique_ptr<Assets>> keep;
        auto replaced=[&](std::string_view mod,const k22::Model& model)->const k22::Model&{
            k22::export_gltf(model,game/mod/"data","quad");
            keep.push_back(std::make_unique<Assets>(data));keep.back()->add_mod(game/mod/"data");
            const auto& result=keep.back()->model("quad.mdl");
            if(result.frames.size()!=2||result.frameNames!=original.frameNames||result.resourceMin.x!=original.resourceMin.x||result.resourceMax.z!=original.resourceMax.z||result.formatVersion!=5)
                throw std::runtime_error("glTF replacement must keep the original's frames, names and collision box");
            return result;
        };
        // Unmodified export imports as the original: positions per frame, triangles, UVs, skin pixels.
        const auto& back=replaced("rt",original);
        if(back.vertices.size()!=4||back.triangles.size()!=2||back.materials.size()!=1||back.materials[0].texture->rgba!=original.materials[0].texture->rgba)throw std::runtime_error("glTF round trip mesh/material");
        for(size_t t=0;t<2;++t)if(back.triangles[t].indices!=original.triangles[t].indices)throw std::runtime_error("glTF round trip triangles");
        for(size_t f=0;f<2;++f)for(size_t i=0;i<4;++i)if(!close(back.frames[f].positions[i],original.frames[f].positions[i],1e-4f))throw std::runtime_error("glTF round trip frame positions");
        for(size_t i=0;i<4;++i)if(std::abs(back.vertices[i].uv.x-original.vertices[i].uv.x)>1e-5f||std::abs(back.vertices[i].uv.y-original.vertices[i].uv.y)>1e-5f)throw std::runtime_error("glTF round trip UVs");
        // Same frame count as morph targets: the glTF's own animation is used.
        k22::Model moved=original;
        for(size_t f=0;f<2;++f)for(size_t i=0;i<4;++i)moved.frames[f].positions[i].z+=1+float(f)+float(i);
        for(size_t i=0;i<4;++i)moved.vertices[i].position=moved.frames[0].positions[i];
        const auto& custom=replaced("targets",moved);
        for(size_t f=0;f<2;++f)for(size_t i=0;i<4;++i)if(!close(custom.frames[f].positions[i],moved.frames[f].positions[i],1e-4f))throw std::runtime_error("glTF morph targets not used as the animation");
        // Different topology (centroid split of triangle 0), one morph target: wrap deform onto the original's frames.
        k22::Model split=original;split.frames.resize(1);
        const auto tri=original.triangles[0].indices;k22::Vertex centre{};
        for(auto i:tri){const auto& v=original.vertices[i];centre.position={centre.position.x+v.position.x/3,centre.position.y+v.position.y/3,centre.position.z+v.position.z/3};centre.uv={centre.uv.x+v.uv.x/3,centre.uv.y+v.uv.y/3};centre.normal=v.normal;}
        split.vertices.push_back(centre);split.frames[0].positions.push_back(centre.position);
        split.triangles[0].indices={tri[0],tri[1],4};split.triangles.push_back(split.triangles[0]);split.triangles.back().indices={tri[1],tri[2],4};
        split.triangles.push_back(split.triangles[0]);split.triangles.back().indices={tri[2],tri[0],4};
        const auto& wrapped=replaced("wrap",split);
        if(wrapped.triangles.size()!=4||wrapped.vertices.size()!=5)throw std::runtime_error("glTF wrap mesh");
        k22::Vec3 expected{};for(auto i:tri){const auto p=original.frames[1].positions[i];expected={expected.x+p.x/3,expected.y+p.y/3,expected.z+p.z/3};}
        bool followed=false;for(size_t i=0;i<5;++i)if(close(wrapped.vertices[i].position,centre.position,1e-4f))followed=close(wrapped.frames[1].positions[i],expected,1e-2f);
        if(!followed)throw std::runtime_error("wrap-deformed vertex does not follow its original triangle");
        for(size_t i=0;i<4;++i)if(!close(wrapped.frames[1].positions[i],original.frames[1].positions[i],1e-2f))throw std::runtime_error("wrap moved original corners");
        // A mod with both quad.mdl and quad.gltf (or .gltf and .glb) is invalid.
        auto put=[](const std::filesystem::path& p){std::filesystem::create_directories(p.parent_path());std::ofstream(p)<<'x';};
        put(game/"both/data/sub/quad.mdl");put(game/"both/data/Sub/QUAD.gltf");put(game/"forms/data/quad.gltf");put(game/"forms/data/quad.glb");
        rejects([&]{Assets(data).add_mod(game/"both/data");});rejects([&]{Assets(data).add_mod(game/"forms/data");});
    }
    {
        constexpr std::string_view first="var source=1;",second="var source=2;",loose="var source=3;";
        tiny_archive(root/"script-main.wrs","entry.wdl",first,3);
        tiny_archive(root/"script-patch.k22","entry.wdl",second);
        {std::ofstream file(root/"entry.wdl",std::ios::binary);file.write(loose.data(),loose.size());}
        {std::ofstream file(root/"loose-only.wdl");file<<"var source=4;";}
        Assets campaign(root),bonus(root,true);
        for(auto* source:{&campaign,&bonus}) {
            source->mount("script-main.wrs");source->mount("script-patch.k22");source->mount("script-main.wrs");
            if(source->text("ENTRY.WDL")!=loose)throw std::runtime_error("ordinary text lost loose precedence");
            rejects([&]{(void)source->archive_text("loose-only.wdl");});
        }
        // A6 00420120 .WDL with 0062119c!=0 (ISO 0x70aa, patch 0xdf02) is archive-only, first match.
        if(campaign.archive_text("ENTRY.WDL")!=first||bonus.archive_text("entry.wdl . ")!=first)
            throw std::runtime_error("archive-only WDL source or duplicate archive priority changed");
    }
    {
        const auto missingRoot=root/"missing-font";
        std::filesystem::create_directory(missingRoot);
        Assets missing(missingRoot);rejects([&]{(void)missing.image_ptr("engine/_a4font.pcx");});
        const auto peRoot=root/"malformed-pe";
        std::filesystem::create_directory(peRoot);
        {std::ofstream broken(peRoot/"K22.exe",std::ios::binary);broken.write("MZ",2);}
        Assets malformedPe(peRoot);rejects([&]{(void)malformedPe.image_ptr("engine/_a4font.pcx");});
        const auto pcxRoot=root/"malformed-installed-font";
        std::filesystem::create_directories(pcxRoot/"engine");
        {std::ofstream broken(pcxRoot/"engine/_a4font.pcx",std::ios::binary);broken.write("not a PCX",9);}
        Assets malformedPcx(pcxRoot);
        rejects([&]{
            try{(void)malformedPcx.image_ptr("engine/_a4font.pcx");}
            catch(const std::runtime_error& error){
                if(std::string_view(error.what()).find("engine font")==std::string_view::npos)
                    throw std::logic_error("malformed installed PCX attempted executable fallback");
                throw;
            }
        });
    }
    {
        const auto tgaRoot=root/"alpha-depth";
        std::filesystem::create_directory(tgaRoot);
        for(unsigned depth:{24u,32u}) {
            std::array<uint8_t,22> bytes{};
            bytes[2]=2;bytes[12]=1;bytes[14]=1;bytes[16]=depth;bytes[17]=depth==32?8:0;
            bytes[18]=1;bytes[19]=2;bytes[20]=3;bytes[21]=255;
            std::ofstream file(tgaRoot/(std::to_string(depth)+".tga"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()),18+depth/8);
            if(!file)throw std::runtime_error("cannot write check TGA");
            if(depth==32) {
                bytes[17]=0;
                std::ofstream noAttributes(tgaRoot/"32-no-attributes.tga",std::ios::binary);
                noAttributes.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                if(!noAttributes)throw std::runtime_error("cannot write opaque no-attributes TGA");
            }
        }
        Assets high(tgaRoot);
        const auto high24=high.image_ptr("24.tga"),high32=high.image_ptr("32.tga");
        if(high24->nativeStorageBytes!=3||high32->nativeStorageBytes!=4||
           high24->nativeTextureAlpha||high32->nativeTextureAlpha)
            throw std::runtime_error("default/A6 TGA native alpha depth");
        // A6 0040e2b0 sets BMAP0x100 for every 4-byte TGA, including fully opaque ones; never for 24-bit TGA.
        if(high24->a6AlphaFormat||!high32->a6AlphaFormat||!high.image_ptr("32-no-attributes.tga")->a6AlphaFormat)
            throw std::runtime_error("A6 TGA32 BMAP0x100 source flag");
        high.set_native_alpha_depth(16);
        if(high.image_ptr("24.tga")!=high24||high.image_ptr("32.tga")!=high32||
           high24->nativeStorageBytes!=3||high32->nativeStorageBytes!=4)
            throw std::runtime_error("alpha depth setter changed cached TGA");
        Assets low(tgaRoot);low.set_native_alpha_depth(16);
        const auto low24=low.image_ptr("24.tga"),low32=low.image_ptr("32.tga");
        if(low24->nativeStorageBytes!=2||low32->nativeStorageBytes!=2)throw std::runtime_error("16-bit TGA native alpha depth");
        low.set_native_alpha_depth(32);
        if(low.image_ptr("24.tga")!=low24||low.image_ptr("32.tga")!=low32||
           low24->nativeStorageBytes!=2||low32->nativeStorageBytes!=2)
            throw std::runtime_error("alpha depth setter expanded cached TGA");
        Assets fractional(tgaRoot);fractional.set_native_alpha_depth(16.25);
        if(fractional.image_ptr("24.tga")->nativeStorageBytes!=3||fractional.image_ptr("32.tga")->nativeStorageBytes!=4)
            throw std::runtime_error("fractional TGA native alpha depth truncated");
        for(double depth:{16.0,16.25,32.0}) {
            Assets bonus(tgaRoot,true);bonus.set_native_alpha_depth(depth);
            const auto rgb=bonus.image_ptr("24.tga"),argb=bonus.image_ptr("32.tga");
            const auto noAttributes=bonus.image_ptr("32-no-attributes.tga");
            if(rgb->nativeTextureAlpha || !argb->nativeTextureAlpha || !noAttributes->nativeTextureAlpha || argb->a6AlphaFormat ||
               argb->nativeStorageBytes!=(depth<=16?2:4) || noAttributes->nativeStorageBytes!=(depth<=16?2:4))
                throw std::runtime_error("A7 TGA32 source alpha flag/depth/attribute contract");
            if(argb->rgba[3]!=255 || noAttributes->rgba[3]!=255)
                throw std::runtime_error("opaque TGA alpha regression pixels changed");
            bonus.set_native_alpha_depth(depth<=16?32:16);
            if(bonus.image_ptr("32.tga")!=argb || bonus.image_ptr("32-no-attributes.tga")!=noAttributes ||
               !argb->nativeTextureAlpha || !noAttributes->nativeTextureAlpha)
                throw std::runtime_error("alpha depth change reclassified cached A7 TGA");
        }
    }
    {
        // Genuine MDL5 records: header, first embedded BGR skin, signed UVs,
        // triangle vertex/UV indices, then one packed frame with bounding records.
        auto mdl5=[](uint32_t width,uint32_t height,uint32_t globalWidth,uint32_t globalHeight) {
            std::vector<uint8_t> bytes;
            auto word=[&](uint32_t value) {
                for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(uint8_t(value>>shift));
            };
            auto real=[&](float value){word(std::bit_cast<uint32_t>(value));};
            auto shortWord=[&](int16_t value) {
                const auto bits=uint16_t(value);
                bytes.push_back(uint8_t(bits));bytes.push_back(uint8_t(bits>>8));
            };
            bytes.insert(bytes.end(),{'M','D','L','5'});word(5);
            for(float value:{2.f,3.f,4.f,10.f,20.f,30.f,0.f,0.f,0.f,0.f})real(value);
            for(uint32_t value:{1u,globalWidth,globalHeight,3u,1u,1u,3u,0u,0u})word(value);
            word(4);word(width);word(height);
            for(uint32_t pixel=0;pixel<width*height;++pixel)bytes.insert(bytes.end(),{1,2,3});
            for(int16_t value:std::array<int16_t,6>{-1,0,8,4,16,-8})shortWord(value);
            for(int16_t value:std::array<int16_t,6>{0,1,2,0,1,2})shortWord(value);
            word(0); // Byte-packed XYZ and normal index.
            bytes.insert(bytes.end(),{1,2,3,0,7,8,9,0}); // Frame bounds.
            const size_t name=bytes.size();bytes.resize(name+16);bytes[name]='u';bytes[name+1]='v';
            bytes.insert(bytes.end(),{1,2,3,0,4,5,6,0,7,8,9,0});
            return bytes;
        };
        auto save=[&](std::string_view name,const std::vector<uint8_t>& bytes) {
            std::ofstream file(root/std::string(name),std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            if(!file)throw std::runtime_error("cannot write MDL5 UV check model");
        };
        const std::array<k22::Vec2,3> expectedUv{{{-.125f,0.f},{1.f,.5f},{2.f,-1.f}}};
        const std::array<k22::Vec3,3> expectedPositions{{{12.f,26.f,42.f},{18.f,35.f,54.f},{24.f,44.f,66.f}}};
        for(const auto dimensions:std::array<std::array<uint32_t,4>,3>{{{1,1,0,0},{3,5,64,32},{0,0,64,32}}}) {
            const auto name=dimensions[0]==1?"uv-1x1.mdl":dimensions[0]==3?"uv-3x5.mdl":"uv-empty.mdl";
            save(name,mdl5(dimensions[0],dimensions[1],dimensions[2],dimensions[3]));
            Assets models(root);
            const auto& model=models.model(name);
            if(model.formatVersion!=5||model.vertices.size()!=3||model.sourceVertices.size()!=3||
               model.triangles.size()!=1||model.sourceTriangles.size()!=1||model.frames.size()!=1||
               model.frames[0].name!="uv"||model.frames[0].positions.size()!=3||
               model.frames[0].sourcePositions.size()!=3||model.materials.size()!=1)
                throw std::runtime_error("MDL5 UV fixture geometry/frame records changed");
            if(model.triangles[0].indices!=std::array<uint32_t,3>{0,1,2}||
               model.sourceTriangles[0]!=std::array<uint32_t,3>{0,1,2}||model.triangles[0].materialIndex!=0)
                throw std::runtime_error("MDL5 UV fixture triangle/material binding changed");
            const auto& material=model.materials[0];
            if(material.uvScale.x!=1.f||material.uvScale.y!=1.f)
                throw std::runtime_error("MDL5 source material UV scale changed");
            if(dimensions[0]==0) {
                if(material.texture)throw std::runtime_error("empty MDL5 skin allocated an image");
            } else {
                if(!material.texture||material.texture->width!=dimensions[0]||material.texture->height!=dimensions[1]||
                   material.texture->rgba.size()!=4*dimensions[0]*dimensions[1])
                    throw std::runtime_error("MDL5 embedded skin dimensions changed");
                for(size_t pixel=0;pixel<material.texture->rgba.size();pixel+=4)
                    if(material.texture->rgba[pixel]!=3||material.texture->rgba[pixel+1]!=2||
                       material.texture->rgba[pixel+2]!=1||material.texture->rgba[pixel+3]!=255)
                        throw std::runtime_error("MDL5 embedded BGR skin pixels changed");
            }
            for(size_t i=0;i<3;++i) {
                const auto uv=model.vertices[i].uv;
                if(uv.x!=expectedUv[i].x||uv.y!=expectedUv[i].y)
                    throw std::runtime_error("MDL5 signed UVs must use padded first-skin dimensions");
                for(const auto position:{model.vertices[i].position,model.sourceVertices[i].position,
                                         model.frames[0].positions[i],model.frames[0].sourcePositions[i]})
                    if(position.x!=expectedPositions[i].x||position.y!=expectedPositions[i].y||position.z!=expectedPositions[i].z)
                        throw std::runtime_error("MDL5 UV normalization changed frame positions");
            }
        }
        auto truncated=mdl5(1,1,0,0);
        const size_t uvOffset=84+12+3; // Header and complete 1x1 BGR skin.
        truncated.resize(uvOffset+11); // Three signed-short pairs require twelve bytes.
        save("uv-truncated.mdl",truncated);
        Assets truncatedModels(root);
        rejects([&]{(void)truncatedModels.model("uv-truncated.mdl");});
        auto badIndex=mdl5(1,1,0,0);
        badIndex[uvOffset+12+6]=3; // First triangle UV index, beyond the three UV records.
        save("uv-index.mdl",badIndex);
        Assets indexedModels(root);
        rejects([&]{(void)indexedModels.model("uv-index.mdl");});
        auto unknownEmpty=mdl5(0,0,0,0);unknownEmpty[84]=15;
        save("uv-unknown-empty.mdl",unknownEmpty);
        auto noSkin=mdl5(0,0,0,0);noSkin[48]=0;
        noSkin.erase(noSkin.begin()+84,noSkin.begin()+96);
        save("uv-no-skin.mdl",noSkin);
        Assets malformedModels(root);
        rejects([&]{(void)malformedModels.model("uv-unknown-empty.mdl");});
        rejects([&]{(void)malformedModels.model("uv-no-skin.mdl");});
    }
    {
        std::vector<uint8_t> bytes;
        auto word=[&](uint32_t value){for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(uint8_t(value>>shift));};
        auto shortWord=[&](uint16_t value){bytes.push_back(uint8_t(value));bytes.push_back(uint8_t(value>>8));};
        auto real=[&](float value){word(std::bit_cast<uint32_t>(value));};
        auto name=[&](std::string_view value){const auto start=bytes.size();bytes.resize(start+16);std::copy(value.begin(),value.end(),bytes.begin()+start);};
        // Reuse one procedural MDL7 fixture for trailing face fields and a
        // zero-skin group; only serialized image records belong to the bank.
        bytes.insert(bytes.end(),{'M','D','L','7'});
        for(uint32_t value:{7u,0u,2u,0u,0u,0u})word(value);
        for(uint16_t value:std::array<uint16_t,10>{16,28,16,68,8,26,16,16,68,24})shortWord(value);
        const std::array<k22::Vec2,3> input{{{-.125f,0.f},{1.f,.5f},{2.f,-1.f}}};
        size_t firstSkinStart{},firstSkinEnd{},firstSkinCount{};
        std::array<size_t,2> faceAttributes{};
        for(unsigned group=0;group<2;++group) {
            bytes.insert(bytes.end(),{1,0,0,0});word(0);name("uv-group");
            if(!group)firstSkinCount=bytes.size();
            for(uint32_t value:{group?0u:1u,3u,1u,3u,1u})word(value);
            if(!group) {
                firstSkinStart=bytes.size();
                word(4);word(3);word(5);name("uv-skin");
                for(unsigned pixel=0;pixel<15;++pixel)bytes.insert(bytes.end(),{1,2,3});
                firstSkinEnd=bytes.size();
            }
            for(const auto uv:input){real(uv.x);real(uv.y);}
            for(uint16_t value:std::array<uint16_t,6>{0,1,2,0,1,2})shortWord(value);
            faceAttributes[group]=bytes.size();
            word(group?2u:0x12345678u);
            bytes.insert(bytes.end(),10,0xff); // Neither reader exposes secondary UV/material fields.
            for(unsigned vertex=0;vertex<3;++vertex) {
                real(float(vertex));real(float(vertex+1));real(float(vertex+2));
                shortWord(65535);bytes.push_back(0);bytes.push_back(0);
            }
            name("uv-frame");word(0);word(0);
        }
        const auto size=uint32_t(bytes.size());
        for(unsigned shift=0;shift<32;shift+=8)bytes[16+shift/8]=uint8_t(size>>shift);
        {std::ofstream file(root/"uv-float.mdl",std::ios::binary);
         file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
         if(!file)throw std::runtime_error("cannot write MDL7 float UV check model");}
        Assets models(root);const auto& model=models.model("uv-float.mdl");
        if(model.formatVersion!=7||model.vertices.size()!=6||model.triangles.size()!=2||model.frames.size()!=1||
           model.materials.size()!=1||!model.materials[0].texture||
           model.materials[0].texture->width!=3||model.materials[0].texture->height!=5||
           model.materials[0].uvScale.x!=1.f||model.materials[0].uvScale.y!=1.f)
            throw std::runtime_error("MDL7 float UV fixture skin/layout mismatch");
        for(const auto& triangle:model.triangles)
            if(triangle.materialIndex!=0)throw std::runtime_error("A6 MDL7 trailing face attribute was consumed");
        for(size_t i=0;i<input.size();++i) {
            const auto uv=model.vertices[i].uv;
            if(uv.x!=input[i].x*(3.f/8.f)||uv.y!=input[i].y*(5.f/8.f))
                throw std::runtime_error("MDL7 float UV padding must be applied exactly once");
        }
        Assets bonusModels(root,true);const auto& bonusModel=bonusModels.model("uv-float.mdl");
        if(bonusModel.materials.size()!=1 || bonusModel.triangles.size()!=2 ||
           bonusModel.triangles[0].materialIndex!=0x12345678 || bonusModel.triangles[1].materialIndex!=2)
            throw std::runtime_error("Bonus raw face attributes were discarded, remapped or bank-validated");
        if(model.materials[0].nativePrimarySubset || bonusModel.materials[0].nativePrimarySubset)
            throw std::runtime_error("A6 or unbound Bonus face invented a primary-subset mark");
        for(size_t vertex=0;vertex<bonusModel.vertices.size();++vertex) {
            const auto uv=bonusModel.vertices[vertex].uv,source=input[vertex%3];
            if(uv.x!=source.x*(3.f/8.f) || uv.y!=source.y*(5.f/8.f))
                throw std::runtime_error("Bonus zero-skin group lost model-wide first-bitmap UV ratio");
        }
        for(const auto& triangle:bonusModel.triangles)
            if(k22::model_material(bonusModel,triangle.materialIndex,0)!=&bonusModel.materials[0])
                throw std::runtime_error("model-wide image-bank helper used raw face attributes");
        auto put=[](std::vector<uint8_t>& value,size_t at,uint32_t word) {
            for(unsigned shift=0;shift<32;shift+=8)value.at(at+shift/8)=uint8_t(word>>shift);
        };
        // Reuse the same procedural image: bank1 references bank0, bank2 is a
        // distinct present bitmap with identical serialized encoding/pixels.
        std::vector<uint8_t> extraBanks(28);put(extraBanks,0,1);
        extraBanks.insert(extraBanks.end(),bytes.begin()+firstSkinStart,bytes.begin()+firstSkinEnd);
        auto aliased=bytes;
        aliased.insert(aliased.begin()+firstSkinEnd,extraBanks.begin(),extraBanks.end());
        put(aliased,firstSkinCount,3);put(aliased,16,uint32_t(aliased.size()));
        const size_t firstAttribute=faceAttributes[0]+extraBanks.size(),secondAttribute=faceAttributes[1]+extraBanks.size();
        put(aliased,firstAttribute,1);put(aliased,secondAttribute,0x12345678);
        auto negative=aliased;put(negative,firstAttribute,UINT32_MAX);
        auto save=[&](std::string_view name,const std::vector<uint8_t>& value) {
            std::ofstream file(root/std::string(name),std::ios::binary);
            file.write(reinterpret_cast<const char*>(value.data()),value.size());
            if(!file)throw std::runtime_error("cannot write MDL7 primary-subset check model");
        };
        save("uv-alias.mdl",aliased);save("uv-negative.mdl",negative);
        Assets primary(root,true),legacyPrimary(root);
        for(const auto filename:{"uv-alias.mdl","uv-negative.mdl"}) {
            const auto& marked=primary.model(filename);
            if(marked.materials.size()!=3 || marked.materials[0].texture!=marked.materials[1].texture ||
               !marked.materials[0].nativePrimarySubset || !marked.materials[1].nativePrimarySubset ||
               marked.materials[2].nativePrimarySubset || marked.materials[0].flags!=marked.materials[2].flags ||
               marked.triangles[0].materialIndex!=(std::string_view(filename)=="uv-negative.mdl"?UINT32_MAX:1u))
                throw std::runtime_error("Bonus bitmap-identity primary mark or negative/raw attribute contract");
            for(const auto& material:legacyPrimary.model(filename).materials)
                if(material.nativePrimarySubset)throw std::runtime_error("A7 primary-subset mark leaked into A6");
        }
        for(uint32_t format:{2u,3u,5u}) {
            std::vector<uint8_t> pixels;
            for(unsigned pixel=0;pixel<15;++pixel) {
                if(format==2)pixels.insert(pixels.end(),{0,0xf8});
                else if(format==3)pixels.insert(pixels.end(),{0xff,0xff});
                else pixels.insert(pixels.end(),{1,2,3,255});
            }
            auto alphaModel=bytes;
            alphaModel.erase(alphaModel.begin()+firstSkinStart+28,alphaModel.begin()+firstSkinEnd);
            alphaModel.insert(alphaModel.begin()+firstSkinStart+28,pixels.begin(),pixels.end());
            put(alphaModel,firstSkinStart,format);put(alphaModel,16,uint32_t(alphaModel.size()));
            const auto filename="uv-alpha-"+std::to_string(format)+".mdl";save(filename,alphaModel);
            Assets source(root,true),a6Source(root);source.set_native_alpha_depth(16);
            const auto bitmap=source.model(filename).materials.at(0).texture;
            if(!bitmap || bitmap->nativeTextureAlpha!=(format==3 || format==5) ||
               a6Source.model(filename).materials.at(0).texture->nativeTextureAlpha)
                throw std::runtime_error("packed source-format alpha classification/profile mismatch");
            for(size_t pixel=3;pixel<bitmap->rgba.size();pixel+=4)
                if(bitmap->rgba[pixel]!=255)throw std::runtime_error("opaque packed alpha regression pixels changed");
            source.set_native_alpha_depth(32);
            if(source.model(filename).materials.at(0).texture!=bitmap ||
               bitmap->nativeTextureAlpha!=(format==3 || format==5))
                throw std::runtime_error("alpha depth change reclassified cached packed bitmap");
        }
    }
    {
        auto put=[](std::vector<uint8_t>& bytes,size_t at,uint32_t value) {
            for(unsigned shift=0;shift<32;shift+=8)bytes.at(at+shift/8)=uint8_t(value>>shift);
        };
        auto record=[&](uint32_t type,uint32_t width,uint32_t height) {
            std::vector<uint8_t> bytes(40);
            const std::string name=type==10?"packed565":"packed24";
            std::copy(name.begin(),name.end(),bytes.begin());
            put(bytes,16,width);put(bytes,20,height);put(bytes,24,type);
            const unsigned bpp=type==10?2:3;
            for(unsigned level=0;level<4;++level) {
                if(level)put(bytes,24+4*level,uint32_t(bytes.size()));
                for(size_t pixel=0;pixel<size_t(width>>level)*(height>>level);++pixel) {
                    if(bpp==2) {
                        const uint16_t color=level?uint16_t(level):
                            std::array<uint16_t,4>{0xf800,0x07e0,0x001f,0xffff}[pixel%4];
                        bytes.push_back(uint8_t(color));bytes.push_back(uint8_t(color>>8));
                    } else bytes.insert(bytes.end(),{uint8_t(1+level),uint8_t(2+level),uint8_t(3+level)});
                }
            }
            return bytes;
        };
        // Procedural pixels, not a duplicate proprietary fixture. Index order is
        // reversed and contains the original missing-bitmap sentinel.
        auto world=[&](const std::vector<uint8_t>& first,const std::vector<uint8_t>& second) {
            const size_t sectionBytes=16+first.size()+second.size(),materials=140+sectionBytes;
            std::vector<uint8_t> bytes(materials+128);
            std::copy_n("WMB6",4,bytes.begin());
            put(bytes,20,140);put(bytes,24,uint32_t(sectionBytes));
            put(bytes,52,uint32_t(materials));put(bytes,56,128);
            put(bytes,140,3);put(bytes,144,uint32_t(16+first.size()));
            put(bytes,148,UINT32_MAX);put(bytes,152,16);
            std::copy(first.begin(),first.end(),bytes.begin()+156);
            std::copy(second.begin(),second.end(),bytes.begin()+156+first.size());
            put(bytes,materials+32,2);put(bytes,materials+64+32,0);
            return bytes;
        };
        unsigned serial=0;
        auto save=[&](const std::vector<uint8_t>& bytes) {
            const auto name="texture-"+std::to_string(serial++)+".wmb";
            std::ofstream file(root/name,std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            if(!file)throw std::runtime_error("cannot write texture check world");
            return name;
        };
        const auto rgb565=record(10,8,8),bgr24=record(12,24,16);
        const auto valid=save(world(rgb565,bgr24));
        Assets bonus(root,true);const auto& decoded=bonus.world(valid);
        const auto& packed=decoded.mesh.materials.at(0);
        const auto& raw=decoded.mesh.materials.at(1);
        if(packed.textureName!="packed565"||raw.textureName!="packed24"||
           !packed.texture||!raw.texture||packed.texture->width!=8||packed.texture->height!=8||
           packed.texture->sourceDepth!=16||packed.texture->nativeStorageBytes!=2||
           raw.texture->width!=24||raw.texture->height!=16||
           raw.texture->sourceDepth!=24||raw.texture->nativeStorageBytes!=3)
            throw std::runtime_error("Bonus packed texture record/index/dimension contract");
        const std::array<std::array<uint8_t,4>,4> colors{{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}}};
        for(size_t pixel=0;pixel<64;++pixel)
            if(!std::equal(colors[pixel%4].begin(),colors[pixel%4].end(),packed.texture->rgba.begin()+4*pixel))
                throw std::runtime_error("Bonus RGB565 packed pixels");
        for(size_t pixel=0;pixel<24*16;++pixel) {
            const auto* color=raw.texture->rgba.data()+4*pixel;
            if(color[0]!=3||color[1]!=2||color[2]!=1||color[3]!=255)
                throw std::runtime_error("Bonus little-endian BGR24 packed pixels");
        }
        const auto oddName=save(world(rgb565,record(12,19,17)));Assets odd(root,true);
        const auto& oddImage=*odd.world(oddName).mesh.materials.at(1).texture;
        if(oddImage.width!=19 || oddImage.height!=17 || oddImage.rgba.size()!=19*17*4)
            throw std::runtime_error("Bonus mip dimensions must halve independently without padding");
        auto rejectWorld=[&](const std::vector<uint8_t>& bytes,bool profile=true) {
            const auto name=save(bytes);Assets source(root,profile);
            rejects([&]{(void)source.world(name);});
        };
        auto campaignReject=world(rgb565,bgr24);put(campaignReject,148,16);
        rejectWorld(campaignReject,false);
        // Every stored level is bounded by its record, not the following texture.
        for(unsigned level=0;level<4;++level) {
            auto truncated=rgb565;size_t end=40;
            for(unsigned mip=0;mip<=level;++mip)end+=size_t(8>>mip)*(8>>mip)*2;
            truncated.resize(end-1);rejectWorld(world(truncated,bgr24));
            auto truncated24=bgr24;end=40;
            for(unsigned mip=0;mip<=level;++mip)end+=size_t(24>>mip)*(16>>mip)*3;
            truncated24.resize(end-1);rejectWorld(world(rgb565,truncated24));
        }
        auto shortHeader=rgb565;shortHeader.resize(39);rejectWorld(world(shortHeader,bgr24));
        for(unsigned level=1;level<4;++level) {
            auto misaligned=rgb565;
            const size_t at=24+4*level;misaligned[at]^=1;
            rejectWorld(world(misaligned,bgr24));
        }
        auto padded=rgb565;padded.push_back(0);rejectWorld(world(padded,bgr24));
        for(uint32_t type:{2u,6u,8u,40u}) {
            auto unsupported=rgb565;put(unsupported,24,type);
            rejectWorld(world(unsupported,bgr24));
        }
        auto small=rgb565;put(small,16,7);rejectWorld(world(small,bgr24));
        auto overflow=rgb565;put(overflow,16,UINT32_MAX);rejectWorld(world(overflow,bgr24));
        auto badOffset=world(rgb565,bgr24);put(badOffset,152,15);rejectWorld(badOffset);
        auto missing=world(rgb565,bgr24);put(missing,140+16+rgb565.size()+bgr24.size()+32,3);rejectWorld(missing);
        // The same header word remains an offset for the default A6 profile.
        auto legacy565=rgb565,legacy24=bgr24;put(legacy565,24,40);put(legacy24,24,40);
        auto legacyBytes=world(legacy565,legacy24);put(legacyBytes,148,16);
        const auto campaignName=save(legacyBytes);Assets campaign(root);
        const auto& old=campaign.world(campaignName);
        if(old.mesh.materials.at(0).texture->rgba!=packed.texture->rgba||
           old.mesh.materials.at(1).texture->rgba!=raw.texture->rgba)
            throw std::runtime_error("A6 legacy offset texture interpretation changed");
        rejectWorld(world(legacy565,legacy24));
        auto faceWorld=[&](const std::vector<uint8_t>& first,const std::vector<uint8_t>& second,
                           uint32_t infoFlags,bool rgbAtlas,bool monoSamples,uint16_t materialFlags) {
            auto bytes=world(first,second);
            const size_t material=bytes.size()-128;
            put(bytes,148,16); // Present unused texture slot is valid in both profiles.
            put(bytes,material,std::bit_cast<uint32_t>(1.f));
            put(bytes,material+20,std::bit_cast<uint32_t>(1.f));
            bytes[material+36]=uint8_t(materialFlags);bytes[material+37]=uint8_t(materialFlags>>8);
            auto section=[&](size_t index,size_t size) {
                const size_t at=bytes.size();bytes.resize(at+size);
                put(bytes,4+index*8,uint32_t(at));put(bytes,8+index*8,uint32_t(size));return at;
            };
            const auto plane=section(1,20);
            put(bytes,plane+8,std::bit_cast<uint32_t>(1.f));put(bytes,plane+16,2);
            const auto vertices=section(3,36);
            put(bytes,vertices+12,std::bit_cast<uint32_t>(32.f));
            put(bytes,vertices+28,std::bit_cast<uint32_t>(32.f));
            const auto edges=section(12,24),edgeList=section(13,12);
            for(unsigned edge=0;edge<3;++edge) {
                put(bytes,edges+edge*8,edge);put(bytes,edges+edge*8+4,(edge+1)%3);
                put(bytes,edgeList+edge*4,edge);
            }
            const auto face=section(7,24);bytes[face+8]=3;bytes[face+12]=4;
            const auto info=section(15,68);
            put(bytes,info,1);put(bytes,info+4,8);put(bytes,info+8,5);
            put(bytes,info+8+24,infoFlags);put(bytes,info+8+28,std::bit_cast<uint32_t>(6.355f));
            if(monoSamples)(void)section(8,9); // 3x3 legacy mono face samples.
            if(rgbAtlas)(void)section(16,256*256*3);
            return bytes;
        };
        auto classify=[&](bool profile,uint32_t infoFlags,bool rgbAtlas,bool monoSamples,
                          uint16_t materialFlags,bool unlit) {
            const auto name=save(faceWorld(profile?rgb565:legacy565,profile?bgr24:legacy24,
                                          infoFlags,rgbAtlas,monoSamples,materialFlags));
            Assets source(root,profile);const auto& surface=source.world(name);
            if(surface.faces.size()!=1 || surface.mesh.triangles.size()!=1 ||
               bool(surface.faces[0].nativeFlags&0x100)!=unlit)
                throw std::runtime_error("profile-bound CPU lightmap face classification mismatch");
        };
        classify(true,1,true,false,0,false); // A7 is lit without legacy section8.
        classify(true,0,true,false,0,true);  // Stored atlas does not override disabled CPU copy.
        classify(true,1,false,false,0,true);
        classify(true,1,false,true,0,true);  // Mono samples cannot enable A7 World144.
        classify(true,1,true,false,2,true);  // Serialized material flag2 still forces unlit.
        classify(false,1,true,false,0,true); // A6 still depends only on section8 here.
        classify(false,1,true,true,0,false);
        classify(false,1,false,true,0,false);
        // A7 1005b47c..484 binds texture table[0], not the first physical
        // record, and clears produced flags before face/triangle classification.
        for(uint16_t flags:{uint16_t(64),uint16_t(0x14e)}) {
            auto bytes=faceWorld(rgb565,bgr24,1,true,false,flags);
            const size_t material=140+16+rgb565.size()+bgr24.size();
            put(bytes,148,UINT32_MAX);put(bytes,material+32,1);
            put(bytes,material+36,0xa5a50000u|flags);
            bytes[material+42]=25;bytes[material+43]=75;
            const auto name=save(bytes);Assets source(root,true);
            const auto& surface=source.world(name);
            const auto& fallback=surface.mesh.materials.at(0);
            const auto& bound=surface.mesh.materials.at(surface.mesh.triangles.at(0).materialIndex);
            if(fallback.flags || fallback.texture!=surface.mesh.materials.at(1).texture ||
               fallback.textureName!="packed24" || !fallback.texture ||
               fallback.texture->width!=24 || fallback.texture->height!=16 ||
               fallback.raw.at(36)!=uint8_t(flags) || fallback.raw.at(37)!=uint8_t(flags>>8) ||
               fallback.raw.at(38)!=0xa5 || fallback.raw.at(39)!=0xa5 ||
               fallback.ambient.r!=0.25f || fallback.albedo.r!=0.75f ||
               bound.texture!=fallback.texture || bound.flags || !bound.lightmap ||
               surface.faces.at(0).nativeFlags || surface.mesh.triangles.at(0).flags ||
               surface.collision.flags.at(0) ||
               surface.mesh.vertices.at(1).uv.x!=32.f/24.f ||
               surface.mesh.vertices.at(2).uv.y!=2.f)
                throw std::runtime_error("Bonus missing texture fallback descriptor/face classification");
        }
        classify(true,1,true,false,0x14e,true); // The same present material is unlit.
        auto nullFirst=world(rgb565,bgr24);
        const size_t material=nullFirst.size()-128;
        put(nullFirst,144,UINT32_MAX);put(nullFirst,148,uint32_t(16+rgb565.size()));
        put(nullFirst,material+32,0);
        put(nullFirst,material+36,64);
        const auto nullName=save(nullFirst);Assets nullSource(root,true);
        const auto& nullMaterial=nullSource.world(nullName).mesh.materials.at(0);
        if(nullMaterial.texture || !nullMaterial.textureName.empty() || nullMaterial.flags)
            throw std::runtime_error("Bonus fallback must not search past null texture table[0]");
    }
    {
        Assets colorTables(root);
        rejects<std::logic_error>([&]{(void)colorTables.native_lightmap_color(0);});
        rejects<std::logic_error>([&]{(void)colorTables.native_lightmap_mono(0);});
        colorTables.initialize_native_lightmap_tables(1536,false,true);
        const std::array<uint16_t,6> words{0,0xf800,0x07e0,0x001f,0xffff,0x1234};
        for(uint16_t word:words)
            if(colorTables.native_lightmap_color(word)!=word)throw std::runtime_error("identity RGB565 lightmap ramp mismatch");
        if(colorTables.native_lightmap_mono(0)!=0||colorTables.native_lightmap_mono(0xffff)!=255)
            throw std::runtime_error("fixed monochrome lightmap endpoints mismatch");
        std::array<uint8_t,6> capturedMono{};
        for(size_t i=0;i<words.size();++i)capturedMono[i]=colorTables.native_lightmap_mono(words[i]);
        colorTables.initialize_native_lightmap_tables(0,true,false);
        for(size_t i=0;i<words.size();++i)
            if(colorTables.native_lightmap_color(words[i])!=words[i]||colorTables.native_lightmap_mono(words[i])!=capturedMono[i])
                throw std::runtime_error("native lightmap tables recaptured initialization");
        Assets monoTables(root);monoTables.initialize_native_lightmap_tables(1536,true,true);
        if(monoTables.native_lightmap_color(0x0060)!=0||monoTables.native_lightmap_color(0xf800)!=0x52aa)
            throw std::runtime_error("native monochrome .33 RGB565 ramp mismatch");
    }
    {
        const std::array<std::string,5> files{
            "shasi100t2.MDL","short123.MDL","border123.MDL",
            std::string(28,'l')+".MDL",std::string(29,'m')+".MDL"};
        const std::string shortAction(20,'s'),modernAction(33,'a');
        for(size_t i=0;i<4;++i)tiny_archive(root/("entity"+std::to_string(i)+".k22"),files[i],char('0'+i));
        {std::ofstream file(root/files[4],std::ios::binary);file.put('4');}
        for(bool padded:{false,true}) {
            const size_t legacySize=padded?136:133,modernSize=padded?372:371;
            std::vector<uint8_t> bytes(140+24+4*legacySize+modernSize);
            auto word=[&](size_t offset,uint32_t value) {
                for(unsigned shift=0;shift<32;shift+=8)bytes[offset+shift/8]=uint8_t(value>>shift);
            };
            auto text=[&](size_t offset,std::string_view value) {
                for(size_t i=0;i<value.size();++i)bytes[offset+i]=uint8_t(value[i]);
            };
            text(0,"WMB6");word(124,140);word(128,uint32_t(bytes.size()-140));word(140,5);
            size_t offset=164;
            for(size_t i=0;i<files.size();++i) {
                const bool modern=i==4;
                word(144+4*i,uint32_t(offset-140));word(offset,modern?7:3);
                text(offset+40,std::string(modern?33:20,'n'));
                text(offset+(modern?73:60),files[i]);
                if(i==1)text(offset+73,shortAction);
                if(modern)text(offset+106,modernAction);
                if(i==3)bytes[offset+92]='!'; // Native copy cap excludes the nonzero 33rd byte.
                const size_t skills=offset+(modern?(padded?140:139):(padded?96:93));
                const size_t count=modern?20:8;
                for(size_t skill=0;skill<count;++skill)
                    word(skills+4*skill,std::bit_cast<uint32_t>(float(100*i+skill+1)));
                word(skills+4*count,1);word(skills+4*count+4,std::bit_cast<uint32_t>(50.0f));
                offset+=modern?modernSize:legacySize;
            }
            const auto name=padded?"entity-padded.wmb":"entity-packed.wmb";
            {std::ofstream file(root/name,std::ios::binary);
             file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
             if(!file)throw std::runtime_error("cannot write entity check world");}
            Assets entities(root);
            for(size_t i=0;i<4;++i)entities.mount("entity"+std::to_string(i)+".k22");
            const auto& world=entities.world(name);
            if(world.entities.size()!=files.size())throw std::runtime_error("entity record boundaries changed");
            for(size_t i=0;i<files.size();++i) {
                const auto& entity=world.entities[i];const bool modern=i==4;
                const std::string_view action=modern?std::string_view(modernAction):i==1?std::string_view(shortAction):std::string_view{};
                if(entity.type!=(modern?7u:3u)||entity.name!=std::string(modern?33:20,'n')||
                   entity.file!=files[i]||entity.action!=action||entity.flags!=1||entity.ambient.r!=0.5f||
                   entity.metadata.size()!=(modern?modernSize:legacySize))
                    throw std::runtime_error("entity filename/action or packed/padded fields changed");
                for(size_t skill=0;skill<(modern?20u:8u);++skill)
                    if(entity.skills[skill]!=float(100*i+skill+1))throw std::runtime_error("entity skill boundary changed");
                if(!modern&&(entity.metadata[72]!=0)!=(i!=1))
                    throw std::runtime_error("legacy entity filename branch fixture changed");
                if(entities.read(entity.file)!=std::vector<uint8_t>{uint8_t('0'+i)})
                    throw std::runtime_error("decoded entity filename failed source resource lookup");
            }
        }
    }
    if(argc==2) {
        Assets game(argv[1]);game.mount("Kaynak.k22");
        game.set_native_shadow_depth(24,true);
        const auto font=game.image_ptr("engine/_a4font.pcx");
        if(font->width!=192||font->height!=72)throw std::runtime_error("actual engine font dimensions mismatch");
        if(font->sourceDepth!=24)throw std::runtime_error("actual engine font source depth mismatch");
        if(font->nativeStorageBytes!=2)throw std::runtime_error("actual engine font native storage mismatch");
        if(game.image_ptr("engine/_a4font.pcx")!=font)throw std::runtime_error("engine font shared image cache mismatch");
        game.purge_image("engine/_a4font.pcx");
        if(game.image_ptr("engine/_a4font.pcx")!=font)throw std::runtime_error("engine font lifetime cache mismatch");
        auto model=[&](const char*name,size_t vertices,size_t rendered,size_t triangles,size_t frames){
            const auto&asset=game.model(name);
            if(asset.sourceVertices.size()!=vertices||asset.vertices.size()!=rendered||asset.triangles.size()!=triangles||asset.frames.size()!=frames)
                throw std::runtime_error(std::string("actual asset count mismatch: ")+name);
            std::cout<<name<<": source="<<vertices<<" rendered="<<rendered<<" triangles="<<triangles<<" frames="<<frames<<'\n';
        };
        model("et_ari.MDL",145,161,286,11);model("yasin_deniz.mdl",1454,1454,2683,1);
        model("bullet.mdl",10,8,12,1);model("yr.hmp",1089,1089,2048,1);
        const auto& menu=game.world("level0.wmb");
        if(menu.mesh.sourceVertices.size()!=8||menu.mesh.vertices.size()!=24||menu.mesh.triangles.size()!=12||menu.entities.size()!=0)
            throw std::runtime_error("actual menu world count mismatch");
        const auto& level=game.world("Start4.WMB");
        if(level.mesh.sourceVertices.size()!=15906||level.mesh.vertices.size()!=55462||level.mesh.triangles.size()!=31788||
           level.entities.size()!=184||level.lights.size()!=99||level.paths.size()!=1||level.paths[0].nodes.size()!=160||
           level.paths[0].edges.size()!=159||level.lightmaps.size()!=45)
            throw std::runtime_error("actual New Game world count mismatch");
        std::unordered_set<std::string> longModels,campaignModels;
        auto campaignModel=[&](std::string_view name) {
            const auto& asset=game.model(name);
            if(asset.sourceVertices.empty()||asset.vertices.empty()||asset.triangles.empty()||asset.frames.empty())
                throw std::runtime_error("actual campaign model decoded empty: "+std::string(name));
            game.purge_model(name);
        };
        for(const auto& entity:level.entities) {
            if(entity.type!=3&&entity.type!=7)continue;
            if(!game.contains(entity.file))throw std::runtime_error("actual world entity resource missing: "+entity.file);
            auto key=entity.file;
            for(auto& character:key)if(character>='A'&&character<='Z')character=char(character-'A'+'a');
            if(key.ends_with(".mdl")&&campaignModels.insert(key).second)campaignModel(entity.file);
            if(entity.type==3&&entity.metadata[72]!=0) {
                if(!entity.action.empty())throw std::runtime_error("actual legacy long filename retained action");
                if(entity.file.size()>13)longModels.insert(entity.file);
            }
        }
        if(longModels.size()!=5)throw std::runtime_error("actual legacy long filename model coverage changed");
        if(campaignModels.size()!=51)throw std::runtime_error("actual campaign entity model coverage changed");
        for(const auto name:{"demir_2007.mdl","tabanca2005.mdl","telsiz2005.mdl","koruyucu_2006.mdl"})campaignModel(name);
        {
            const auto& fish=game.model("fish.mdl");
            if(fish.sourceVertices.size()!=6||fish.triangles.size()!=8||fish.frames.size()!=1||
               fish.materials.empty()||!fish.materials[0].texture||
               fish.materials[0].texture->width!=4||fish.materials[0].texture->height!=1||
               fish.materials[0].uvScale.x!=1.f||fish.materials[0].uvScale.y!=1.f)
                throw std::runtime_error("actual fish native skin/geometry contract mismatch");
            for(const auto& vertex:fish.vertices)
                if(vertex.uv.x<0.f||vertex.uv.x>3.f/8.f||vertex.uv.x*8.f!=float(int(vertex.uv.x*8.f))||vertex.uv.y!=0.f)
                    throw std::runtime_error("actual fish native UV padding mismatch");
        }
        game.purge_model("fish.mdl");
        for(const auto* world:{&menu,&level}) {
            for(const auto& triangle:world->mesh.triangles) {
                if(triangle.sourceFace>=world->faces.size()||triangle.materialIndex>=world->mesh.materials.size())
                    throw std::runtime_error("actual world triangle source face/material reference mismatch");
                if(world->faces[triangle.sourceFace].materialIndex>=world->mesh.materials.size())
                    throw std::runtime_error("actual world source face material reference mismatch");
            }
            for(const auto& face:world->faces) {
                const unsigned shift=(world->infoFlags&1)&&face.lightStyles[0]==3?3:4;
                const int grid=1<<shift;
                if(face.nativeSampleShift!=shift||face.texelExtentS<0||face.texelExtentT<0||
                   face.texelOriginS%grid||face.texelOriginT%grid||face.texelExtentS%grid||face.texelExtentT%grid)
                    throw std::runtime_error("actual world native face texel grid mismatch");
            }
        }
        for(const auto& lightmap:level.lightmaps)
            if(!lightmap||lightmap->sourceDepth!=24||
               lightmap->nativeStorageBytes!=(level.nativeLightmapStorageBytes==3?3:255))
                throw std::runtime_error("actual New Game lightmap depth/storage mismatch");
        auto shadowPolicy=[&](double depth,bool capable,uint8_t coloredBytes) {
            Assets policy(argv[1]);policy.mount("Kaynak.k22");policy.set_native_shadow_depth(depth,capable);
            const auto& world=policy.world("Start4.WMB");
            const bool colored=(world.infoFlags&1)&&!world.sections[16].empty();
            const uint8_t expected=colored?coloredBytes:1;
            if(world.nativeLightmapStorageBytes!=expected)throw std::runtime_error("actual world shadow depth/capability policy mismatch");
            for(const auto& image:world.lightmaps)
                if(!image||image->sourceDepth!=24||image->nativeStorageBytes!=(colored?coloredBytes:255))
                    throw std::runtime_error("actual world shadow image policy mismatch");
            const auto cachedImages=world.lightmaps;
            policy.set_native_shadow_depth(coloredBytes==3?16:24,coloredBytes==3?false:true);
            if(&policy.world("Start4.WMB")!=&world||world.nativeLightmapStorageBytes!=expected||world.lightmaps!=cachedImages)
                throw std::runtime_error("shadow policy setter changed cached world");
            for(const auto& image:world.lightmaps)
                if(image->nativeStorageBytes!=(colored?coloredBytes:255))
                    throw std::runtime_error("shadow policy setter changed cached lightmap storage");
        };
        shadowPolicy(16,true,2);shadowPolicy(24,false,2);
        shadowPolicy(24,true,3);shadowPolicy(23.75,true,2);
        std::cout<<"Start4.WMB: source=15906 rendered=55462 triangles=31788 entities=184 lights=99 path=160/159 lightmaps=45\n";
    }
    if(argc==3 && std::string_view(argv[2])=="--bonus") {
        Assets bonus(argv[1],true);bonus.mount("K22_mode.wrs");
        auto model=[&](std::string_view name,size_t banks,size_t vertices,size_t triangles) {
            const auto& asset=bonus.model(name);
            if(asset.formatVersion!=7 || asset.materials.size()!=banks ||
               asset.sourceVertices.size()!=vertices || asset.triangles.size()!=triangles)
                throw std::runtime_error("actual Bonus model bank/geometry mismatch: "+std::string(name));
            for(const auto& material:asset.materials)
                if(!material.texture)throw std::runtime_error("actual Bonus serialized bank lost its image: "+std::string(name));
            for(size_t bank=0;bank<asset.materials.size();++bank) {
                const bool expected=bank==0 || (name=="parca_et11.mdl" && bank==2);
                if(asset.materials[bank].nativePrimarySubset!=expected)
                    throw std::runtime_error("actual Bonus primary-subset descriptor mark mismatch: "+std::string(name));
            }
            bonus.purge_model(name);
        };
        model("parca_et11.mdl",4,44,80);
        model("parca_et7.mdl",2,54,94);
        model("yer_st4.mdl",2,69,91);
        {
            // Authentic MDL7: populated UV table, but every face has 0xffff absent-UV indices.
            const auto& box=bonus.model("atom_kutu.MDL");
            const std::array<std::array<uint32_t,3>,12> faces{{
                {4,0,5},{5,0,1},{5,1,6},{6,1,2},{6,2,7},{7,2,3},
                {7,3,4},{4,3,0},{7,4,6},{6,4,5},{0,3,1},{1,3,2}}};
            const std::array<std::array<uint32_t,3>,8> positions{{
                {0xc21dad30,0xc21d5dd3,0x421d65ed},{0x421d16d8,0xc21d5dd3,0x421d65f0},
                {0x421d16d8,0x421d6631,0x421d65f0},{0xc21dad2f,0x421d6631,0x421d65f0},
                {0xc21dad30,0xc21d5dd3,0xc21d5e16},{0x421d16d5,0xc21d5dd3,0xc21d5e16},
                {0x421d16d8,0x421d6631,0xc21d5e14},{0xc21dad2f,0x421d6631,0xc21d5e14}}};
            if(box.sourceVertices.size()!=8 || box.vertices.size()!=8 || box.triangles.size()!=12 ||
               box.sourceTriangles.size()!=12 || box.frames.size()!=1 || box.frames[0].name!="Frame0" ||
               box.frames[0].sourcePositions.size()!=8 || box.frames[0].positions.size()!=8 ||
               box.vertexSources.size()!=8)
                throw std::runtime_error("actual Bonus absent-UV box geometry/frame layout mismatch");
            auto exactPosition=[&](k22::Vec3 position,uint32_t source) {
                if(std::array<uint32_t,3>{std::bit_cast<uint32_t>(position.x),
                   std::bit_cast<uint32_t>(position.y),std::bit_cast<uint32_t>(position.z)}!=positions.at(source))
                    throw std::runtime_error("actual Bonus absent-UV box position/asymmetry changed");
            };
            for(uint32_t source=0;source<8;++source) {
                exactPosition(box.sourceVertices[source].position,source);
                exactPosition(box.frames[0].sourcePositions[source],source);
            }
            for(size_t render=0;render<8;++render) {
                exactPosition(box.vertices[render].position,box.vertexSources[render]);
                exactPosition(box.frames[0].positions[render],box.vertexSources[render]);
                if(box.vertices[render].uv.x!=0 || box.vertices[render].uv.y!=0)
                    throw std::runtime_error("actual Bonus absent-UV face acquired texture coordinates");
            }
            for(size_t face=0;face<faces.size();++face) {
                if(box.sourceTriangles[face]!=faces[face] || box.triangles[face].materialIndex!=0)
                    throw std::runtime_error("actual Bonus absent-UV box topology/attribute changed");
                for(size_t corner=0;corner<3;++corner) {
                    const auto render=box.triangles[face].indices[corner];
                    if(render>=box.vertices.size() || box.vertexSources[render]!=faces[face][corner])
                        throw std::runtime_error("actual Bonus absent-UV render triangle lost source geometry");
                }
            }
            bonus.purge_model("atom_kutu.MDL");
        }
        const auto& explosion=bonus.model("bonus_kutu_patla.mdl");
        if(explosion.materials.size()!=1 || !explosion.materials[0].texture ||
           !explosion.materials[0].texture->nativeTextureAlpha ||
           explosion.materials[0].texture->sourceDepth!=32 || explosion.materials[0].texture->nativeStorageBytes!=4)
            throw std::runtime_error("actual Bonus ordinary BGRA explosion alpha-format producer mismatch");
        bonus.purge_model("bonus_kutu_patla.mdl");
        for(const auto name:{"Expl5+10.tga","Expl4+10.tga","atom160+30.tga"}) {
            const auto image=bonus.image_ptr(name);
            if(image->nativeTextureAlpha!=(image->sourceDepth==32))
                throw std::runtime_error("actual Bonus TGA source-depth alpha flag mismatch");
        }
        bool sawSky=false;
        for(const auto name:{"bos.wmb","basla.wmb","silahbox.wmb"}) {
            const auto& world=bonus.world(name);
            if(world.entities.size()!=(std::string_view(name)=="basla.wmb"?330u:0u))
                throw std::runtime_error("actual Bonus world entity count mismatch");
            for(const auto& material:world.mesh.materials) {
                if(!material.texture)throw std::runtime_error("actual Bonus world texture missing");
                const auto& image=*material.texture;
                if(image.nativeTextureAlpha)throw std::runtime_error("actual Bonus RGB565/BGR24 world acquired alpha-format flag");
                if(material.textureName=="skycubenice1+6") {
                    sawSky=true;
                    if(image.width!=3072 || image.height!=512 || image.sourceDepth!=24 || image.nativeStorageBytes!=3)
                        throw std::runtime_error("actual Bonus non-power-of-two BGR24 sky mismatch");
                } else if(image.sourceDepth!=16 || image.nativeStorageBytes!=2)
                    throw std::runtime_error("actual Bonus RGB565 world texture mismatch");
            }
        }
        if(!sawSky)throw std::runtime_error("actual Bonus sky texture consumer not covered");
    }
    std::cout<<"native asset boundaries passed\n";
}
