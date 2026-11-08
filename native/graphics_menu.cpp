// In-game graphics menu (docs/GRAPHICS_MENU.md); not in the original. The pause menu's GRAPHICS item (native/console.cpp) opens
// it over the Smartmenu, the RENDERING item of the main menu's settings screen (console.cpp settings_tick) over AYARLAR. Its rows
// are the launcher's Rendering settings (installer/launcher.cpp): renderer, preset, window, FPS
// limit and every --graphics key of render_policy::graphicsOptions, greyed where the active backend cannot run them (the same
// admission tables admit_features uses). fps_limit, seamless doors and the window apply at once; renderer keys apply 0.4 s after
// the last change through Renderer::reconfigure, or main.cpp recreates the renderer (backend switch, or reconfigure false).
// Every applied change is merged into launcher.ini (native/launcher_config.cpp), so the launcher and the next start agree.
#include "engine.hpp"
#include "graphics_cli.hpp"
#include "launcher_config.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <unistd.h>

namespace k22 {
namespace {
using render_policy::GraphicsOption;using render_policy::graphicsOptions;
constexpr std::array<std::string_view,3> backendNames{"opengl","vulkan-raster","vulkan-rt"},backendLabels{"OPENGL","VULKAN","VULKAN RT"};
constexpr std::array<std::string_view,4> presetNames{"original","enhanced","ultra","custom"};
constexpr std::array<std::string_view,3> windowNames{"windowed","fullscreen","borderless"},windowLabels{"WINDOWED","FULLSCREEN","BORDERLESS"}; // WindowMode order
enum Row : int {Renderer_,Preset,Window,Resolution,Vsync,Fps,Packs=24,Doors=25,Back=26};
struct RowInfo {std::string_view label,key;};
constexpr std::array<RowInfo,27> rows{{{"RENDERER",{}},{"PRESET",{}},{"WINDOW MODE",{}},{"RESOLUTION",{}},{"VSYNC",{}},{"FRAME LIMIT","fps_limit"},
    {"AMD FSR","fsr"},{"FRAME GENERATION","frame_generation"},{"MSAA","msaa"},{"ANISOTROPIC FILTERING","anisotropy"},{"SHADOWS","shadows"},
    {"GLOBAL ILLUMINATION","gi"},{"REFLECTIONS","reflections"},{"DENOISE","denoise"},{"BLOOM","bloom"},{"AMBIENT OCCLUSION","ssao"},{"FXAA","fxaa"},
    {"VOLUMETRIC FOG","volumetric_fog"},{"TAA","taa"},{"SHARPEN","sharpen"},{"SMOOTH NORMALS","smooth_normals"},{"TESSELLATION","tessellation"},
    {"TEXTURE UPSCALE","texture_upscale"},{"PBR MATERIALS","pbr"},{"TEXTURE PACKS",{}},{"SEAMLESS DOORS","seamless_doors"},{"BACK",{}}}};
const GraphicsOption& option(std::string_view key){
    auto it=std::find_if(graphicsOptions.begin(),graphicsOptions.end(),[&](const auto& o){return o.key==key;});
    if(it==graphicsOptions.end())throw std::runtime_error("graphics menu: unknown key "+std::string(key));return *it;
}
std::span<const std::string_view> values(const GraphicsOption& o){return {o.values.data(),o.count};}
// render_policy::GraphicsOption admission: the key on this backend, and its restricted value (shadows=rt) only where it runs.
bool admitted(const GraphicsOption& o,std::string_view value,std::string_view backend){
    const unsigned bit=render_policy::backend_bit(backend);return (o.backends&bit)&&(o.restricted.empty()||value!=o.restricted||(o.restrictedBackends&bit));
}
// Inverse of render_policy::resolve_graphics: the launcher value of one key in resolved settings (fallback = not overridden).
std::string value_of(const render_policy::GraphicsSettings& g,std::string_view key){
    const auto& f=g.features;auto b=[](bool v){return std::string(v?"1":"0");};
    if(key=="pbr")return b(g.pbr);if(key=="anisotropy")return g.anisotropy?std::to_string(g.anisotropy):std::string(option(key).fallback);
    if(key=="msaa")return g.msaa?std::to_string(g.msaa):std::string(option(key).fallback);
    if(key=="shadows")return f.shadows==ShadowMode::RayTraced?"rt":f.shadows==ShadowMode::ShadowMap?"shadowmap":"original";
    if(key=="gi")return b(f.gi);if(key=="reflections")return f.roughReflections?"rough":"original";if(key=="denoise")return b(f.denoise);
    if(key=="bloom")return b(f.bloom);if(key=="ssao")return b(f.ssao);if(key=="fxaa")return b(f.fxaa);if(key=="taa")return b(f.taa);if(key=="volumetric_fog")return b(f.fog);
    if(key=="smooth_normals")return b(f.smoothNormals);if(key=="tessellation")return b(f.tessellation);if(key=="texture_upscale")return b(f.upscale);if(key=="sharpen")return b(f.sharpen);
    if(key=="fsr")return std::string(option(key).values[f.fsr]);if(key=="frame_generation")return f.frameGeneration?"on":"off";
    if(key=="fps_limit")return g.fpsLimit==0?"original":g.fpsLimit<0?"unlimited":std::to_string(g.fpsLimit);
    if(key=="seamless_doors")return g.seamlessDoors?"on":"off";
    return std::string(option(key).fallback);
}
int fps_value(std::string_view v){return v=="original"?0:v=="unlimited"?-1:std::stoi(std::string(v));}
std::string upper(std::string_view s){std::string out;for(char c:s)out+=c=='_'?' ':c>='a'&&c<='z'?char(c-32):c;return out;}
std::string shown(std::string_view key,std::string_view v){
    // Sample counts before the generic booleans: msaa/anisotropy 1 is one sample (off), which "1"→ON used to show as ON.
    if(key=="msaa"||key=="anisotropy")return v=="1"?"OFF":std::string(v)+"X";
    if(v=="0"||v=="off")return "OFF";if(v=="1"||v=="on")return "ON";
    if(key=="fps_limit")return v=="original"?"ORIGINAL":v=="unlimited"?"UNLIMITED":std::string(v)+" FPS";
    if(key=="shadows"&&v=="shadowmap")return "SHADOW MAPS";if(key=="shadows"&&v=="rt")return "RAY TRACED";
    if(key=="reflections"&&v=="rough")return "ROUGHNESS-AWARE";
    return upper(v);
}
// `kabus22 --list-capabilities` of this very binary: its hidden-window probes run in a child process, so they never touch this
// process's GL context or SDL video state.
std::string self_capabilities(){
    char path[4096];auto n=::readlink("/proc/self/exe",path,sizeof path-1);if(n<=0)return {};
    std::string command="'";for(char c:std::string_view(path,std::size_t(n)))command+=c=='\''?std::string("'\\''"):std::string(1,c);command+="' --list-capabilities 2>/dev/null";
    std::FILE* pipe=::popen(command.c_str(),"r");if(!pipe)return {};
    std::string out;char buffer[4096];for(std::size_t k;(k=std::fread(buffer,1,sizeof buffer,pipe))>0;)out.append(buffer,k);::pclose(pipe);return out;
}
}
void Engine::configure_graphics(const RenderOptions& base,const RenderOptions& launched,std::string_view preset,std::span<const std::pair<std::string,std::string>> overrides){
    graphicsBase_=base;graphicsBase_.packs=nullptr;graphicsBase_.features={};graphicsPacks_=launched.packs;
    const auto g=render_policy::resolve_graphics(preset,overrides);
    GraphicsChoice c;c.preset=std::string(preset);c.backend=std::string(graphics_cli::backend_name(renderer?renderer->options():launched));
    for(const auto& o:graphicsOptions)if(o.key!="backend"&&o.key!="texture_packs")c.values.emplace(std::string(o.key),value_of(g,o.key));
    c.vsync=launched.vsync||g.fpsLimit<0;c.packs=launched.packs!=nullptr; // unlimited turned vsync off (graphics_cli::apply); the choice is still on
    graphicsWant_=graphicsApplied_=c;fpsLimit=g.fpsLimit;graphicsConfigured_=true;
    graphicsBackends_={-1,-1,-1};graphicsBackends_[std::size_t(std::find(backendNames.begin(),backendNames.end(),c.backend)-backendNames.begin())]=1;
}
// The launcher's command line for this choice (launcher_settings.cpp command()), resolved as graphics_cli::apply does; window and
// title stay the current ones. Texture packs come from the launch (the launcher's checked packs), never rediscovered.
RenderOptions Engine::graphics_options(const GraphicsChoice& c)const{
    RenderOptions o=graphicsBase_;graphics_cli::select_backend(o,c.backend);
    std::vector<std::pair<std::string,std::string>> overrides;
    for(const auto& [key,value]:c.values){const auto& opt=option(key);if(value==opt.fallback)continue;
        if(c.preset=="custom"?!(opt.backends&render_policy::backend_bit(c.backend)):key!="fps_limit"&&key!="seamless_doors")continue;overrides.emplace_back(key,value);}
    const auto g=render_policy::resolve_graphics(c.preset,overrides);
    if(g.anisotropy){o.anisotropy=float(g.anisotropy);o.forceAnisotropy=true;}if(g.msaa)o.samples=g.msaa;o.pbr=g.pbr;
    std::vector<std::string> notices;o.features=render_policy::admit_features(g.features,c.backend,notices);
    o.vsync=c.vsync&&g.fpsLimit>=0;o.packs=c.packs?graphicsPacks_:nullptr;
    if(currentWidth_>0&&currentHeight_>0){o.width=currentWidth_;o.height=currentHeight_;}o.mode=currentWindowMode;o.title=windowTitle_;
    return o;
}
bool Engine::graphics_applicable(int row)const{
    const auto& w=graphicsWant_;
    switch(row){
    case Renderer_:return renderer&&std::count(graphicsBackends_.begin(),graphicsBackends_.end(),1)>1;
    case Preset:case Back:return true;
    case Window:return nativeDisplay_;
    case Resolution:return nativeDisplay_&&currentWindowMode!=WindowMode::Borderless;
    case Vsync:return fps_value(w.values.at("fps_limit"))>=0; // unlimited forces vsync off (graphics_cli::apply)
    case Packs:return graphicsPacks_!=nullptr;
    case Doors:return state.profile==SemanticProfile::CampaignA6;
    case Fps:return true;
    }
    const auto key=rows[std::size_t(row)].key;const auto& o=option(key);
    if(!(o.backends&render_policy::backend_bit(w.backend)))return false;
    if(key=="frame_generation")return w.values.at("fsr")!="off"; // admit_features: frame generation needs fsr
    if(key=="taa")return w.values.at("fsr")=="off";              // FSR replaces TAA (render_vulkan.cpp)
    return true;
}
std::vector<std::pair<int,int>> Engine::graphics_resolutions()const{
    std::vector<std::pair<int,int>> r;auto add=[&](int w,int h){if(w>=1280&&h>=720&&w<=16384&&h<=16384)r.emplace_back(w,h);};
    add(currentWidth_,currentHeight_);
    if(renderer){SDL_DisplayMode d{};if(!SDL_GetDesktopDisplayMode(0,&d))add(d.w,d.h);for(int i=0,n=SDL_GetNumDisplayModes(0);i<n;++i)if(!SDL_GetDisplayMode(0,i,&d))add(d.w,d.h);}
    std::sort(r.begin(),r.end());r.erase(std::unique(r.begin(),r.end()),r.end());return r;
}
void Engine::graphics_renderer_changed(){graphicsDirty_=true;graphicsDue_=time_+0.4;}
void Engine::graphics_step(Runtime& runtime,int row,int direction,bool wrap){
    auto next=[&](int count,int index,const std::function<bool(int)>& valid){
        for(int i=index,n=0;n<count;++n){i+=direction;if(i<0||i>=count){if(!wrap)return -1;i=(i%count+count)%count;}if(i==index)return -1;if(valid(i))return i;}return -1;};
    auto& w=graphicsWant_;auto& a=graphicsApplied_;
    if(row==Renderer_){
        int i=next(3,int(std::find(backendNames.begin(),backendNames.end(),w.backend)-backendNames.begin()),[&](int k){return graphicsBackends_[std::size_t(k)]==1;});
        if(i>=0){w.backend=std::string(backendNames[std::size_t(i)]);graphics_renderer_changed();}return;
    }
    if(row==Preset){
        int i=next(4,int(std::find(presetNames.begin(),presetNames.end(),w.preset)-presetNames.begin()),[](int){return true;});if(i<0)return;
        w.preset=std::string(presetNames[std::size_t(i)]);
        // A preset is its whole option set (resolve_graphics); fps_limit and seamless doors apply under every preset and stay.
        if(w.preset!="custom"){const auto g=render_policy::resolve_graphics(w.preset,{});for(auto& [key,value]:w.values)if(key!="fps_limit"&&key!="seamless_doors")value=value_of(g,key);}
        graphics_renderer_changed();return;
    }
    if(row==Window||row==Resolution){
        int width=currentWidth_,height=currentHeight_;WindowMode mode=currentWindowMode;
        if(row==Window){int i=next(3,int(mode),[](int){return true;});if(i<0)return;mode=WindowMode(i);if(mode==WindowMode::Borderless)width=height=0;}
        else{auto list=graphics_resolutions();int at=int(std::find(list.begin(),list.end(),std::pair{width,height})-list.begin());int i=next(int(list.size()),at,[](int){return true;});if(i<0)return;std::tie(width,height)=list[std::size_t(i)];}
        try{if(!configure_display(runtime,true,true,width,height,mode))throw std::runtime_error("the native display is not available");}
        catch(const std::exception& e){notify(std::string("Display change failed: ")+e.what());return;}
        graphics_persist();return;
    }
    if(row==Vsync){w.vsync=!w.vsync;graphics_renderer_changed();return;}
    if(row==Packs){w.packs=!w.packs;graphics_renderer_changed();return;}
    if(row==Back)return;
    const auto key=rows[std::size_t(row)].key;const auto& o=option(key);const auto list=values(o);auto& value=w.values.find(key)->second;
    int i=next(int(list.size()),int(std::find(list.begin(),list.end(),value)-list.begin()),[&](int k){return admitted(o,list[std::size_t(k)],w.backend);});if(i<0)return;
    const std::string chosen(list[std::size_t(i)]);
    if(key=="fps_limit"){ // main.cpp paces by Engine::fpsLimit every frame; crossing "unlimited" changes vsync (a renderer key)
        const bool vsyncBefore=fpsLimit>=0;fpsLimit=fps_value(chosen);value=a.values.find(key)->second=chosen;if((fpsLimit>=0)!=vsyncBefore)graphics_renderer_changed();graphics_persist();return;
    }
    if(key=="seamless_doors"){
        if(door_phase()!=0){notify("Seamless doors can change when no door is opening");return;}
        seamlessDoors=chosen=="on";value=a.values.find(key)->second=chosen;
        // Off: the original LevelYukle fade; the prefetched destination (door_world) is released like a level change does.
        if(!seamlessDoors){if(door_.world&&door_.world!=world){if(renderer)renderer->purge_model(&door_.world->mesh);assets.drop_world(door_.wmb);}door_.world=nullptr;door_.wmb.clear();for(auto& g:door_.groups)g.linked=false;frame_.door={};}
        graphics_persist();return;
    }
    value=chosen;if(w.preset!="custom")w.preset="custom"; // as the launcher: individual options are the custom preset
    graphics_renderer_changed();
}
void Engine::graphics_update(){
    if(!graphicsDirty_||(graphicsOpen_&&time_<graphicsDue_))return;
    graphicsDirty_=false;if(graphicsWant_==graphicsApplied_)return;
    const auto next=graphics_options(graphicsWant_);
    if(!renderer){graphicsApplied_=graphicsWant_;graphics_persist();return;} // headless: nothing to render, the choice still persists
    if(graphicsWant_.backend==graphicsApplied_.backend){
        try{if(renderer->reconfigure(next)){graphicsApplied_=graphicsWant_;graphics_persist();notify("Graphics settings applied");return;}}
        catch(const std::exception& e){std::cerr<<"kabus22: graphics: live change failed ("<<e.what()<<"); recreating the renderer\n";}
    }
    rendererRequest_=next;graphicsPending_=graphicsWant_;
}
std::optional<RenderOptions> Engine::take_renderer_request(){auto r=std::move(rendererRequest_);rendererRequest_.reset();return r;}
void Engine::renderer_recreated(const std::string& error){
    if(error.empty()){graphicsApplied_=graphicsPending_;graphics_persist();notify("Graphics settings applied (renderer restarted: "+graphicsApplied_.backend+")");}
    else{graphicsWant_.preset=graphicsApplied_.preset;graphicsWant_.backend=graphicsApplied_.backend;graphicsWant_.vsync=graphicsApplied_.vsync;graphicsWant_.packs=graphicsApplied_.packs;
        for(auto& [key,value]:graphicsWant_.values)if(key!="fps_limit"&&key!="seamless_doors")value=graphicsApplied_.values.at(key);
        notify("Graphics change failed, previous settings restored: "+error);}
    if(!(graphicsWant_==graphicsApplied_))graphics_renderer_changed();
}
void Engine::set_renderer(Renderer* next){
    // Renderer-owned data the engine points into: TTF atlases (Renderer::Impl::fonts) cached per FONT object and referenced by this
    // frame's UI commands. Images, models and worlds are asset/engine-owned; the new renderer uploads them on first draw.
    renderer=next;systemFonts_.clear();ui_.clear();frame_.ui={};graphicsRects_.clear();
    if(!next)return;
    next->set_title(windowTitle_);
    if(state.profile==SemanticProfile::CampaignA6)next->set_mouse_mode(number("mouse_mode")!=0,number("mouse_mode")==0&&(number("enable_mouse")!=0||firstPersonActive_)); // update_input's rule
    const auto m=next->metrics();currentWidth_=m.width;currentHeight_=m.height;set_vector(global_vector("screen_size"),{float(m.width),float(m.height),0});
    if(nativeDisplay_)prepare_display();
}
void Engine::graphics_persist(){
    launcher_config::Graphics g;const auto& a=graphicsApplied_;g.preset=a.preset;g.renderer=a.backend;
    if(nativeDisplay_){g.window=std::string(windowNames[std::size_t(currentWindowMode)]);if(currentWindowMode!=WindowMode::Borderless)g.resolution=std::to_string(currentWidth_)+'x'+std::to_string(currentHeight_);}
    for(const auto& [key,value]:a.values)g.options.push_back({key,value,std::string(option(key).fallback)});
    try{launcher_config::write(g);}catch(const std::exception& e){notify(std::string("launcher.ini not updated: ")+e.what());}
}
void Engine::open_graphics_menu(GraphicsHost host){
    if(!graphicsConfigured_)configure_graphics({},renderer?renderer->options():RenderOptions{},"original",{});
    graphicsOpen_=true;graphicsHost_=host;graphicsTop_=0;graphicsRow_=Renderer_;while(!graphics_applicable(graphicsRow_))++graphicsRow_;
    if(renderer&&!graphicsProbe_.valid()&&std::count(graphicsBackends_.begin(),graphicsBackends_.end(),-1))graphicsProbe_=std::async(std::launch::async,self_capabilities);
}
int Engine::graphics_value(std::string_view key)const{
    const auto it=graphicsWant_.values.find(key);if(it==graphicsWant_.values.end())return -1;const std::string_view v=it->second;
    if(!v.empty()&&std::all_of(v.begin(),v.end(),[](char c){return c>='0'&&c<='9';}))return std::stoi(std::string(v));
    const auto list=values(option(key));return int(std::find(list.begin(),list.end(),v)-list.begin()); // named values: their index
}
bool Engine::graphics_menu_tick(Runtime& runtime,const Input& in){
    // The original screen it covers went away (the pause menu resumed, or the settings screen closed some other way).
    const bool host=graphicsHost_==GraphicsHost::Pause?number("STM_Acik")!=0:!settings_panels().empty();
    if(state.profile==SemanticProfile::CampaignA6&&!host){graphicsOpen_=false;graphics_update();return false;}
    if(graphicsProbe_.valid()&&graphicsProbe_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        const auto json=graphicsProbe_.get(); // graphics_cli::list_capabilities: "NAME":{"available":true|false,...}
        for(std::size_t i=0;i<3;++i)if(graphicsBackends_[i]<0)graphicsBackends_[i]=json.find('"'+std::string(backendNames[i])+"\":{\"available\":true")!=std::string::npos;
    }
    auto key=[&](std::initializer_list<SDL_Scancode> keys){return std::any_of(keys.begin(),keys.end(),[&](auto k){return bool(in.pressed[k]);});};
    auto sound=[&](std::string_view name){const auto s=global(name);if(!s.bits)return;ProcessContext context;std::array<Argument,3> args{{{s,{}},{Value::from_double(number("GA_Efekt")),{}},{Value{},{}}}};builtin(runtime,context,"snd_play",args);}; // Kabusdismenu.wdl DM_TusKontrol
    auto inside=[&](std::size_t i){const auto& r=i<graphicsRects_.size()?graphicsRects_[i]:Rect{};return r.width>0&&in.mouseX>=r.x&&in.mouseX<r.x+r.width&&in.mouseY>=r.y&&in.mouseY<r.y+r.height;};
    const int count=int(rows.size());
    auto close=[&]{graphicsOpen_=false;sound("DMS_Esc");graphics_update();};
    if(key({SDL_SCANCODE_ESCAPE,SDL_SCANCODE_BACKSPACE})||in.pad_pressed(SDL_CONTROLLER_BUTTON_B)||in.mousePressed[SDL_BUTTON_RIGHT]){close();return true;}
    int move=(key({SDL_SCANCODE_DOWN,SDL_SCANCODE_KP_2})||in.pad_pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN))-(key({SDL_SCANCODE_UP,SDL_SCANCODE_KP_8})||in.pad_pressed(SDL_CONTROLLER_BUTTON_DPAD_UP));
    if(in.wheelY!=0)move=in.wheelY>0?-1:1;
    if(move){int row=graphicsRow_;for(int n=0;n<count;++n){row=(row+move+count)%count;if(graphics_applicable(row))break;}if(row!=graphicsRow_){graphicsRow_=row;sound("DMS_Yontus");}}
    for(int row=0;row<count&&(in.mouseDX||in.mouseDY);++row)if(inside(std::size_t(row)*2)&&graphics_applicable(row)&&row!=graphicsRow_){graphicsRow_=row;sound("DMS_Yontus");}
    const bool left=key({SDL_SCANCODE_LEFT,SDL_SCANCODE_KP_4})||in.pad_pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT),right=key({SDL_SCANCODE_RIGHT,SDL_SCANCODE_KP_6})||in.pad_pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    bool enter=key({SDL_SCANCODE_RETURN,SDL_SCANCODE_KP_ENTER})||in.pad_pressed(SDL_CONTROLLER_BUTTON_A);int direction=right-left;
    if(in.mousePressed[SDL_BUTTON_LEFT])for(int row=0;row<count;++row){if(!inside(std::size_t(row)*2)||!graphics_applicable(row))continue;graphicsRow_=row;
        const auto& v=graphicsRects_[std::size_t(row)*2+1];if(inside(std::size_t(row)*2+1))direction=in.mouseX<v.x+v.width/2?-1:1;else enter=true;break;} // "<" half / ">" half / the label cycles
    if(!graphics_applicable(graphicsRow_))return true;
    if(graphicsRow_==Back&&(enter||direction)){close();return true;}
    if(direction||enter){const auto before=graphicsWant_;const bool doors=seamlessDoors;const int fps=fpsLimit;const auto mode=currentWindowMode;const int width=currentWidth_,height=currentHeight_;
        graphics_step(runtime,graphicsRow_,enter?1:direction,enter);
        if(!(before==graphicsWant_)||doors!=seamlessDoors||fps!=fpsLimit||mode!=currentWindowMode||width!=currentWidth_||height!=currentHeight_)sound("DMS_Enter");}
    return true;
}
void Engine::graphics_menu_overlay(){
    graphicsRects_.assign(rows.size()*2,Rect{});graphicsStrings_.clear();
    if(!renderer||!graphicsOpen_||currentWidth_<=0||currentHeight_<=0)return;
    const float w=float(currentWidth_),h=float(currentHeight_);constexpr float layer=1e6f;
    UICommand dim;dim.kind=UICommand::Kind::Solid;dim.rect={0,0,w,h};dim.color={0,0,0,0.78f};dim.layer=layer;ui_.push_back(dim);
    const auto& title=renderer->font_atlas("serif",std::clamp(int(h/22),16,96),true);const auto& font=renderer->font_atlas("sans-serif",std::clamp(int(h/44),12,64),true);
    auto text=[&](const FontAtlas& f,float x,float y,std::string s,Color color){graphicsStrings_.push_back(std::move(s));const std::string_view t=graphicsStrings_.back();
        UICommand c;c.kind=UICommand::Kind::Text;c.font=&f;c.text=t;c.rect={std::floor(x)+1,std::floor(y)+1,Renderer::text_extent(f,t).x,f.lineHeight};c.color={0,0,0,color.a};c.layer=layer;c.filter=true;ui_.push_back(c);
        c.rect.x-=1;c.rect.y-=1;c.color=color;ui_.push_back(c);return c.rect.width;};
    const Color red{0.78f,0.16f,0.12f,1},normal{0.86f,0.86f,0.86f,1},grey{0.5f,0.5f,0.5f,0.55f};
    const std::string heading="GRAPHICS";text(title,(w-Renderer::text_extent(title,heading).x)/2,h*0.06f,heading,red);
    const float line=std::floor(font.lineHeight*1.3f),top=h*0.06f+title.lineHeight*1.5f,bottom=h-font.lineHeight*3;
    const int visible=std::max(1,int((bottom-top)/line)),count=int(rows.size());
    graphicsTop_=std::clamp(graphicsTop_,std::max(0,graphicsRow_-visible+1),graphicsRow_);graphicsTop_=std::clamp(graphicsTop_,0,std::max(0,count-visible));
    const float labelX=w*0.24f,valueCenter=w*0.66f,valueWidth=w*0.30f;const auto& want=graphicsWant_;
    for(int row=graphicsTop_;row<std::min(count,graphicsTop_+visible);++row){
        const float y=top+float(row-graphicsTop_)*line;const bool on=graphics_applicable(row),selected=row==graphicsRow_;
        const Color color=!on?grey:selected?red:normal;
        if(selected){UICommand bar;bar.kind=UICommand::Kind::Solid;bar.rect={labelX-font.lineHeight,y-line*0.12f,valueCenter+valueWidth/2+font.lineHeight-(labelX-font.lineHeight),line};bar.color={1,1,1,0.08f};bar.layer=layer;ui_.push_back(bar);}
        text(font,labelX,y,std::string(rows[std::size_t(row)].label),color);
        graphicsRects_[std::size_t(row)*2]={labelX-font.lineHeight,y-line*0.12f,valueCenter+valueWidth/2-labelX+2*font.lineHeight,line};
        if(row==Back)continue;
        std::string value;const auto key=rows[std::size_t(row)].key;
        if(row==Renderer_)value=std::string(backendLabels[std::size_t(std::find(backendNames.begin(),backendNames.end(),want.backend)-backendNames.begin())]);
        else if(row==Preset)value=upper(want.preset);
        else if(row==Window)value=std::string(windowLabels[std::size_t(currentWindowMode)]);
        else if(row==Resolution)value=std::to_string(currentWidth_)+" X "+std::to_string(currentHeight_);
        else if(row==Vsync)value=want.vsync&&fps_value(want.values.at("fps_limit"))>=0?"ON":"OFF";
        else if(row==Packs)value=want.packs&&graphicsPacks_?"ON":"OFF";
        else if(row==Doors)value=seamlessDoors?"ON":"OFF";
        else{auto v=want.values.find(key)->second;if(key=="shadows"&&!admitted(option(key),v,want.backend))v="shadowmap";value=shown(key,v);} // admit_features degrades rt to shadow maps
        const float width=Renderer::text_extent(font,value).x;text(font,valueCenter-width/2,y,value,color);
        if(on){const float arrow=Renderer::text_extent(font,"<").x;text(font,valueCenter-valueWidth/2,y,"<",color);text(font,valueCenter+valueWidth/2-arrow,y,">",color);}
        graphicsRects_[std::size_t(row)*2+1]={valueCenter-valueWidth/2,y-line*0.12f,valueWidth,line};
    }
    if(graphicsTop_>0)text(font,labelX-font.lineHeight*1.5f,top,"^",normal);
    if(graphicsTop_+visible<count)text(font,labelX-font.lineHeight*1.5f,top+float(visible-1)*line,"v",normal);
    const std::string hint=graphicsDirty_||rendererRequest_?"APPLYING...":"UP/DOWN SELECT   LEFT/RIGHT CHANGE   ENTER CYCLE   ESC BACK";
    text(font,(w-Renderer::text_extent(font,hint).x)/2,h-font.lineHeight*2,hint,graphicsDirty_||rendererRequest_?red:grey);
}
}
