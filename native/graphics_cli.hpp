#pragma once
// Contract D graphics command line: --list-capabilities, --dump-textures, --preset, --graphics, --texture-pack.
#include "render.hpp"
#include "render_policy.hpp"
#include "texture_pack.hpp"
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace k22::graphics_cli {
struct Request {
    bool active{},rendererFlag{},backendOverride{};
    std::string preset{"original"};
    std::vector<std::pair<std::string,std::string>> overrides;
    std::vector<std::string> packs;
    bool presetBackend{}; // Backend chosen by preset, not by the user: may degrade with a notice.
    int fpsLimit{}; // Resolved fps_limit for main.cpp pacing: 0 = script fps_max, -1 = unlimited.
};
inline std::string json(std::string_view text){
    std::string out="\"";
    for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<0x20){static const char*hex="0123456789abcdef";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}
    return out+'"';
}
// Removes graphics options from argv so the engine parser never sees them; records explicit backend choices.
inline Request extract(int& argc,char** argv){
    Request r;int kept=1;
    for(int i=1;i<argc;++i){
        std::string_view a=argv[i];
        auto value=[&]{if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));return std::string(argv[++i]);};
        if(a=="--preset"){r.preset=value();r.active=true;}
        else if(a=="--graphics"){auto v=value();auto eq=v.find('=');if(eq==std::string::npos||!eq)throw std::runtime_error("--graphics requires KEY=VALUE");r.overrides.emplace_back(v.substr(0,eq),v.substr(eq+1));r.active=true;}
        else if(a=="--texture-pack"){auto v=value();if(v.empty()||v.find('/')!=std::string::npos||v=="."||v=="..")throw std::runtime_error("--texture-pack requires a pack directory name");r.packs.push_back(v);r.active=true;}
        else{if(a=="--renderer"||a=="--ray-tracing")r.rendererFlag=true;argv[kept++]=argv[i];}
    }
    argc=kept;argv[argc]=nullptr;
    for(auto&[key,value]:r.overrides)if(key=="backend")r.backendOverride=true;
    if(r.rendererFlag&&r.backendOverride)throw std::runtime_error("--graphics backend conflicts with --renderer/--ray-tracing");
    return r;
}
inline void select_backend(RenderOptions&o,std::string_view backend){o.backend=backend=="opengl"?RendererBackend::OpenGL:RendererBackend::Vulkan;o.rayTracing=backend=="vulkan-rt";}
inline std::string_view backend_name(const RenderOptions&o){return o.backend==RendererBackend::OpenGL?"opengl":o.rayTracing?"vulkan-rt":"vulkan-raster";}
// No graphics option keeps RenderOptions untouched, so default behavior is exactly the original path.
inline void apply(RenderOptions&o,Request&r){
    if(!r.active)return;
    auto g=render_policy::resolve_graphics(r.preset,r.overrides);
    // Unlimited also turns vsync off; numeric limits keep it, so the display refresh still caps them.
    r.fpsLimit=g.fpsLimit;if(g.fpsLimit<0)o.vsync=false;
    if(!r.packs.empty()){if(g.explicitPacks&&!g.texturePacks)throw std::runtime_error("--texture-pack conflicts with --graphics texture_packs=0");g.texturePacks=true;}
    if(r.backendOverride)select_backend(o,g.backend);
    else if(!r.rendererFlag&&(r.preset=="enhanced"||r.preset=="ultra")){select_backend(o,g.backend);r.presetBackend=true;}
    if(g.anisotropy){o.anisotropy=float(g.anisotropy);o.forceAnisotropy=true;}
    if(g.msaa)o.samples=g.msaa;
    o.pbr=g.pbr;o.features=g.features;
    if(!g.texturePacks)return;
    std::vector<TexturePack> packs;
    if(!r.packs.empty())for(auto&name:r.packs){auto pack=load_texture_pack(texture_pack_root()/name);if(!pack.valid)throw std::runtime_error("texture pack "+name+" is invalid: "+pack.detail);packs.push_back(std::move(pack));}
    else for(auto&pack:discover_texture_packs()){if(pack.valid)packs.push_back(std::move(pack));else std::cerr<<"kabus22: graphics: skipping invalid texture pack "<<pack.name<<": "<<pack.detail<<'\n';}
    if(packs.empty()){std::cerr<<"kabus22: graphics: no valid texture packs installed in "<<texture_pack_root().string()<<"; using original textures\n";return;}
    for(auto&pack:packs)std::cerr<<"kabus22: graphics: texture pack "<<pack.name<<" ("<<pack.detail<<")\n";
    o.packs=std::make_shared<const TexturePackSet>(std::move(packs));
}
// Preset-chosen backends degrade vulkan-rt -> vulkan-raster -> opengl; explicit choices fail hard.
// Each attempt admits only the features its backend can run and reports every dropped feature.
inline std::unique_ptr<Renderer> create(RenderOptions o,const Request&r){
    const auto requested=o.features;
    for(;;){
        std::vector<std::string> notices;o.features=render_policy::admit_features(requested,backend_name(o),notices);
        try{auto renderer=std::make_unique<Renderer>(o);for(auto&n:notices)std::cerr<<"kabus22: graphics: "<<n<<'\n';return renderer;}
        catch(const std::exception&e){
            auto next=render_policy::fallback_backend(backend_name(o));
            if(!r.presetBackend||next.empty())throw;
            std::cerr<<"kabus22: graphics: "<<backend_name(o)<<" unavailable ("<<e.what()<<"); disabling it, using "<<next<<'\n';
            select_backend(o,next);
        }
    }
}
inline int list_capabilities(){
    std::string out="{\"version\":"+json(K22_VERSION)+",\"backends\":{";
    const std::array<std::pair<const char*,std::pair<RendererBackend,bool>>,3> backends{{{"opengl",{RendererBackend::OpenGL,false}},{"vulkan-raster",{RendererBackend::Vulkan,false}},{"vulkan-rt",{RendererBackend::Vulkan,true}}}};
    for(std::size_t i=0;i<backends.size();++i){auto [available,detail]=Renderer::probe(backends[i].second.first,backends[i].second.second);out+=(i?",":"")+json(backends[i].first)+":{\"available\":"+(available?"true":"false")+",\"detail\":"+json(detail)+"}";}
    out+="},\"presets\":[\"original\",\"enhanced\",\"ultra\",\"custom\"],\"options\":[";
    for(std::size_t i=0;i<render_policy::graphicsOptions.size();++i){
        const auto&o=render_policy::graphicsOptions[i];out+=std::string(i?",":"")+"{\"key\":"+json(o.key)+",\"values\":[";
        for(std::size_t v=0;v<o.count;++v)out+=(v?",":"")+json(o.values[v]);
        out+="],\"default\":"+json(o.fallback)+",\"backends\":[";bool firstBackend=true;
        for(auto [bit,name]:{std::pair{render_policy::backendOpenGL,"opengl"},{render_policy::backendRaster,"vulkan-raster"},{render_policy::backendRT,"vulkan-rt"}})if(o.backends&bit){out+=std::string(firstBackend?"":",")+json(name);firstBackend=false;}
        out+="]";
        if(!o.restricted.empty()){out+=",\"value_backends\":{"+json(o.restricted)+":[";firstBackend=true;for(auto [bit,name]:{std::pair{render_policy::backendOpenGL,"opengl"},{render_policy::backendRaster,"vulkan-raster"},{render_policy::backendRT,"vulkan-rt"}})if(o.restrictedBackends&bit){out+=std::string(firstBackend?"":",")+json(name);firstBackend=false;}out+="]}";}
        out+=",\"label\":"+json(o.label)+"}";
    }
    out+="],\"texture_packs\":[";bool first=true;
    for(auto&pack:discover_texture_packs()){out+=std::string(first?"":",")+"{\"name\":"+json(pack.name)+",\"display_name\":"+json(pack.displayName)+",\"path\":"+json(pack.path.string())+",\"valid\":"+(pack.valid?"true":"false")+",\"detail\":"+json(pack.detail)+"}";first=false;}
    std::cout<<out<<"]}\n";return 0;
}
inline int dump(int argc,char** argv){
    std::filesystem::path output,data;bool bonus=false;
    for(int i=1;i<argc;++i){std::string_view a=argv[i];if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));
        if(a=="--dump-textures")output=argv[++i];else if(a=="--data")data=argv[++i];
        else if(a=="--mode"){std::string_view m=argv[++i];if(m!="campaign"&&m!="bonus")throw std::runtime_error("unsupported mode; choose campaign or bonus");bonus=m=="bonus";}
        else throw std::runtime_error("--dump-textures accepts only --data GAME_ROOT and --mode campaign|bonus");}
    if(data.empty())throw std::runtime_error("--dump-textures requires --data GAME_ROOT");
    auto count=dump_textures(data,bonus,output);std::cout<<"Dumped "<<count<<" textures to "<<output.string()<<'\n';return 0;
}
}
