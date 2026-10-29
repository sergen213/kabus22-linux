#include "engine.hpp"
#include "wdl.hpp"
#include "saves.hpp"
#include "scenario.hpp"
#include "graphics_cli.hpp"
#include "mods.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <optional>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <libavutil/log.h>
}
namespace {
std::string lower(std::string_view text){std::string out;out.reserve(text.size());for(unsigned char c:text)out.push_back(c=='\\'?'/':c>='A'&&c<='Z'?char(c+32):char(c));return out;}
std::optional<std::string> private_bonus_locale(){
 const auto* xdg=std::getenv("XDG_DATA_HOME");const auto* home=std::getenv("HOME");
 if((!xdg||!*xdg)&&(!home||!*home))throw std::runtime_error("HOME or XDG_DATA_HOME required for private Bonus locale");
 auto root=(xdg&&*xdg?std::filesystem::path(xdg):std::filesystem::path(home)/".local/share")/"kabus22";
 root=std::filesystem::absolute(root).lexically_normal();
 int folder=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(folder<0)throw std::runtime_error("cannot open private Bonus locale root");
 for(const auto& part:root.relative_path()){
  int next=::openat(folder,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);auto error=errno;::close(folder);
  if(next<0){if(error==ENOENT)return std::nullopt;throw std::runtime_error("unsafe or unreadable private Bonus locale directory");}
  folder=next;
 }
 int file=::openat(folder,"ygm.k22",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);auto error=errno;::close(folder);
 if(file<0){if(error==ENOENT)return std::nullopt;throw std::runtime_error("unsafe or unreadable private Bonus locale");}
 struct stat st{};bool valid=::fstat(file,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==::geteuid()&&!(st.st_mode&0022)&&st.st_nlink==1&&st.st_size>=3&&st.st_size<=16;
 if(!valid){::close(file);throw std::runtime_error("private Bonus locale is not a bounded user-owned regular file");}
 char bytes[17]{};ssize_t count;do{count=::read(file,bytes,sizeof(bytes));}while(count<0&&errno==EINTR);::close(file);
 if(count!=st.st_size)throw std::runtime_error("private Bonus locale changed during reading");
 // Bonus launcher 0x52973/0x529de: 111 -> tr, 222 -> de, anything else (its own 000) -> en.
 return std::string(bytes,std::size_t(count));
}
std::pair<int,int> resolution(std::string_view text){
 auto split=text.find('x');if(split==std::string_view::npos||text.find('x',split+1)!=std::string_view::npos)throw std::runtime_error("--resolution requires WIDTHxHEIGHT");
 int width=0,height=0;auto component=[](std::string_view value,int& result){auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);return !value.empty()&&error==std::errc{}&&end==value.data()+value.size();};
 if(!component(text.substr(0,split),width)||!component(text.substr(split+1),height)||width<1280||height<720||width>16384||height>16384)throw std::runtime_error("--resolution requires1280x720 through16384x16384");
 return {width,height};
}
void help(){std::cout<<"kabus22 native engine\nUsage: kabus22 --data GAME_ROOT [--mode campaign|bonus] [--script ENTRY] [--display native|authored] [--resolution WIDTHxHEIGHT] [--window-mode windowed|fullscreen|borderless] [--language tr|en|de|es|ru|pl] [--audio-language tr|en] [--camera original|first-person] [--character original|demir|ebru|inzar] [--define SYMBOL] [--renderer opengl|vulkan] [--ray-tracing on|off] [--headless] [--frames N] [--capture FILE]\nOriginal auto-selected campaign defaults to native desktop-sized borderless widescreen. Native resolution minimum1280x720, maximum16384x16384; borderless always uses desktop dimensions. --display authored restores original resolution services. Custom --script, patched campaign and Bonus retain authored display behavior.\nOpenGL is the default. Vulkan defaults to hardware ray tracing; --renderer vulkan --ray-tracing off selects raster only. Requested unsupported Vulkan/RT fails explicitly, without fallback.\nDefault mode is campaign. Bonus defaults to K22_mode.wdl, remembered ygm.k22 locale (otherwise en), and defanti_0; --define defanti_0|1|2|3 selects Bonus AA.\n--language es|ru|pl selects the community patch's DD_Dil 3-5 and requires its installed language data (K22Widescreen.wrs, _data/language/spanish|russian|polish).\n--camera first-person: optional community-patch 1.8.5 style first-person camera for Demir and Ebru on the original campaign scripts (default original; patched scripts keep their own F5 camera).\nReads the supplied original game data/scripts; uses private XDG saves. No proprietary assets are embedded.\n"
 "kabus22 --import-saves WINDOWS_GAME_DIR [--data GAME_ROOT] imports a validated original Windows save set (kayit*.sav, kysd.sav, ftz.k22) into the private save root, rotating replaced files into backups/.\n"
 "Testing: --scenario FILE [--scenario-out DIR] runs a scripted gameplay scenario (docs/GAMEPLAY_TESTING.md) with a fixed 1/60 s step, headless or rendered; exit code 0 only if every step passed.\n"
 "Graphics: kabus22 --list-capabilities prints backend/option/texture-pack JSON without game data. kabus22 --dump-textures DIR --data GAME_ROOT [--mode campaign|bonus] writes every decodable original texture as DIR/<key>.png plus DIR/index.tsv.\n"
 "  --preset original|enhanced|ultra|custom (absent = original), --graphics KEY=VALUE (repeatable; backend, pbr, anisotropy, msaa, texture_packs, fps_limit=original|30..240|unlimited, ... see --list-capabilities), --texture-pack NAME (repeatable, priority order; ${XDG_DATA_HOME:-~/.local/share}/kabus22/packs/NAME). Preset backends degrade with a notice; explicit --renderer/--ray-tracing/--graphics backend fail hard.\n"
 "Seamless doors: --graphics seamless_doors=on|off (default on). kabus22 --door-table LEVELS.tsv --data GAME_ROOT prints every LY_Kapi doorway and whether it is seamless (docs/SEAMLESS_DOORS.md).\n"
 "Controller: ${XDG_CONFIG_HOME:-~/.config}/kabus22/controller.ini mode=original|modern (absent = original).\n"
 "Mods (docs/MODDING.md): --mod ID (repeatable, priority order; ${XDG_DATA_HOME:-~/.local/share}/kabus22/mods/ID) replaces or adds game files by name. kabus22 --export-files OUT --data GAME_ROOT [--mode campaign|bonus] writes every original game file as OUT/<name>, the layout of a mod's data/.\n";}
// Start-up archive first, then RESOURCE order: patch exe K22Widescreen.wrs -> k22.wrs -> kaynak.k22; ISO K22.wrs -> kaynak.k22.
std::vector<std::string> mount_archives(k22::Assets& assets,bool bonus){
 std::vector<std::string> mounted;if(bonus)mounted.push_back("K22_mode.wrs");else for(auto name:{"K22Widescreen.wrs","K22.wrs","Kaynak.k22"})if(assets.contains(name))mounted.push_back(name);
 for(auto& name:mounted)assets.mount(name);return mounted;
}
int export_files(int argc,char** argv){
 std::filesystem::path out,data;std::string mode="campaign";
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));if(a=="--export-files")out=argv[++i];else if(a=="--data")data=argv[++i];else if(a=="--mode")mode=argv[++i];else throw std::runtime_error("--export-files accepts only --data GAME_ROOT and --mode campaign|bonus");}
 if(data.empty())throw std::runtime_error("--data GAME_ROOT is required");if(mode!="campaign"&&mode!="bonus")throw std::runtime_error("unsupported mode; choose campaign or bonus");
 k22::Assets assets(data,mode=="bonus");auto archives=mount_archives(assets,mode=="bonus");
 out=std::filesystem::weakly_canonical(std::filesystem::absolute(out));auto inside=out.lexically_relative(assets.root());
 if(!inside.empty()&&*inside.begin()!="..")throw std::runtime_error("--export-files OUT must not be inside GAME_ROOT");
 if(std::filesystem::exists(out)&&!(std::filesystem::is_directory(out)&&std::filesystem::is_empty(out)))throw std::runtime_error("--export-files OUT must not exist or be an empty directory");
 std::size_t count=0;
 for(const auto& name:assets.names()){
  if(std::any_of(archives.begin(),archives.end(),[&](auto& a){return lower(a)==name;}))continue; // the mounted archives themselves are not replaceable files
  auto bytes=assets.read(name);auto path=out/name;std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path,std::ios::binary);if(!file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("cannot write "+path.string());++count;
 }
 std::cout<<"Exported "<<count<<" game files to "<<out.string()<<'\n';return 0;
}
int import_saves(int argc,char** argv){
 std::filesystem::path source,data;
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));if(a=="--import-saves")source=argv[++i];else if(a=="--data")data=argv[++i];else throw std::runtime_error("--import-saves accepts only --data GAME_ROOT");}
 // Patched K22Widescreen scripts read slots from _data/saves; the original K22.wdl reads them beside the game.
 bool patched=false;if(!data.empty()){k22::Assets probe(data,false);patched=probe.contains("K22Widescreen.wrs");}
 const auto root=k22::saves::root();const auto slots=patched?std::filesystem::path("_data/saves"):std::filesystem::path{};
 auto result=k22::saves::import_windows(source,root,slots,{k22::saves::MetaFormat,K22_VERSION,k22::saves::now_utc(),"","","import:"+std::filesystem::absolute(source).lexically_normal().string(),""});
 for(const auto& name:result.backedUp)std::cout<<"backup "<<(root/"backups"/name).string()<<".1\n";
 for(const auto& name:result.imported)std::cout<<"imported "<<(root/name).string()<<'\n';
 std::cout<<"Imported "<<result.imported.size()<<" original save files from "<<result.source.string()<<'\n';return 0;
}
}
int main(int argc,char** argv){try{
 av_log_set_level(AV_LOG_ERROR); // FFmpeg warnings about the original media (vorbis timestamps, wmv3 extra data) flood the logs; keep errors.
 std::filesystem::path data,entry,capture;bool headless=false;std::uint64_t frames=0;bool frameLimit=false;std::string mode="campaign",language,audioLanguage,rendererName="opengl",rayTracing,display="native";std::vector<std::string> defines;int displayWidth=0,displayHeight=0;k22::WindowMode windowMode=k22::WindowMode::Borderless;bool displayDimensions=false,displayMode=false;
 for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--import-saves")return import_saves(argc,argv);
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--list-capabilities"){if(argc!=2)throw std::runtime_error("--list-capabilities takes no other options");return k22::graphics_cli::list_capabilities();}if(a=="--dump-textures")return k22::graphics_cli::dump(argc,argv);if(a=="--door-table")return k22::door_table(argc,argv);if(a=="--export-files")return export_files(argc,argv);}
 auto graphics=k22::graphics_cli::extract(argc,argv);
 std::filesystem::path scenarioFile,scenarioOut=".";{int kept=1;for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--scenario"||a=="--scenario-out"){if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));(a=="--scenario"?scenarioFile:scenarioOut)=argv[++i];continue;}argv[kept++]=argv[i];}argc=kept;}
 int character=-1;{int kept=1;for(int i=1;i<argc;++i){if(std::string_view(argv[i])=="--character"){if(i+1>=argc)throw std::runtime_error("missing value for --character");const std::string_view c=argv[++i];character=c=="demir"?0:c=="ebru"?1:c=="inzar"?2:c=="original"?-1:-2;if(character<-1)throw std::runtime_error("--character requires original, demir, ebru or inzar");continue;}argv[kept++]=argv[i];}argc=kept;}
 std::string cameraName="original";{int kept=1;for(int i=1;i<argc;++i){if(std::string_view(argv[i])=="--camera"){if(i+1>=argc)throw std::runtime_error("missing value for --camera");cameraName=argv[++i];if(cameraName!="original"&&cameraName!="first-person")throw std::runtime_error("--camera requires original or first-person");continue;}argv[kept++]=argv[i];}argc=kept;}
 std::vector<std::string> mods;{int kept=1;for(int i=1;i<argc;++i){if(std::string_view(argv[i])=="--mod"){if(i+1>=argc)throw std::runtime_error("missing value for --mod");std::string id=argv[++i];if(!k22::mod_id(id))throw std::runtime_error("--mod requires a mod directory name");if(std::find(mods.begin(),mods.end(),id)!=mods.end())throw std::runtime_error("--mod "+id+" given twice");mods.push_back(std::move(id));continue;}argv[kept++]=argv[i];}argc=kept;}
 for(int i=1;i<argc;++i){std::string_view a=argv[i];auto value=[&](){if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));return std::string(argv[++i]);};if(a=="--help"||a=="-h"){help();return 0;}if(a=="--version"){std::cout<<"kabus22 native engine " K22_VERSION "\n";return 0;}if(a=="--data")data=value();else if(a=="--mode")mode=value();else if(a=="--script")entry=value();else if(a=="--display"){display=value();if(display!="native"&&display!="authored")throw std::runtime_error("--display requires native or authored");}else if(a=="--resolution"){auto [w,h]=resolution(value());displayWidth=w;displayHeight=h;displayDimensions=true;}else if(a=="--window-mode"){auto name=value();if(name=="windowed")windowMode=k22::WindowMode::Windowed;else if(name=="fullscreen")windowMode=k22::WindowMode::Fullscreen;else if(name=="borderless")windowMode=k22::WindowMode::Borderless;else throw std::runtime_error("--window-mode requires windowed, fullscreen or borderless");displayMode=true;}else if(a=="--language")language=value();else if(a=="--audio-language")audioLanguage=value();else if(a=="--define")defines.push_back(value());else if(a=="--renderer")rendererName=value();else if(a=="--ray-tracing"){rayTracing=value();if(rayTracing!="on"&&rayTracing!="off")throw std::runtime_error("--ray-tracing requires on or off");}else if(a=="--capture")capture=value();else if(a=="--headless")headless=true;else if(a=="--frames"){auto n=value();auto [p,e]=std::from_chars(n.data(),n.data()+n.size(),frames);if(e!=std::errc{}||p!=n.data()+n.size())throw std::runtime_error("invalid frame limit");frameLimit=true;}else throw std::runtime_error("unknown option "+std::string(a));}
 if(display=="authored"&&(displayDimensions||displayMode))throw std::runtime_error("--resolution and --window-mode require --display native");
 if(data.empty())throw std::runtime_error("--data GAME_ROOT is required");if(headless&&!capture.empty())throw std::runtime_error("--capture requires actual renderer output, not --headless");
 if(rendererName!="opengl"&&rendererName!="vulkan")throw std::runtime_error("unsupported renderer; choose opengl or vulkan");
 if(!rayTracing.empty()&&rendererName!="vulkan")throw std::runtime_error("--ray-tracing applies only to --renderer vulkan");
 if(headless&&rendererName=="vulkan")throw std::runtime_error("--renderer vulkan requires a real window, not --headless");
 if(mode!="campaign"&&mode!="bonus")throw std::runtime_error("unsupported mode; choose campaign or bonus");const bool bonus=mode=="bonus";
 const bool explicitLanguage=!language.empty();
 k22::Assets assets(data,bonus);
 for(const auto& id:mods){
  auto mod=k22::load_mod(k22::mods_root()/id);if(!mod.valid)throw std::runtime_error("mod "+id+" is invalid: "+mod.detail);
  std::size_t files=0;try{files=assets.add_mod(mod.path/"data");}catch(const std::exception& e){throw std::runtime_error("mod "+id+" is invalid: "+e.what());}
  std::cerr<<"kabus22: mod "<<id<<": "<<files<<" files\n";
 }
 if(language.empty()&&bonus){language="en";auto marker=private_bonus_locale();if(!marker&&assets.contains("ygm.k22"))marker=assets.text("ygm.k22");if(marker){auto remembered=std::string_view(*marker).substr(0,marker->find_first_of("\r\n"));if(remembered=="111")language="tr";else if(remembered=="222")language="de";}}
 if(language.empty()){const char* locale=std::getenv("LC_ALL");if(!locale||!*locale)locale=std::getenv("LC_MESSAGES");if(!locale||!*locale)locale=std::getenv("LANG");auto name=locale?lower(locale):std::string{};language=name.starts_with("tr")?"tr":name.starts_with("de")?"de":"en";}if(audioLanguage.empty())audioLanguage=language=="tr"?"tr":"en";
 // Patch DilMenu adds es/ru/pl (DD_Dil 3-5); they exist only with its scripts and _data/language/<folder> files.
 if(language!="tr"&&language!="en"&&language!="de"){
  static constexpr std::array<std::pair<std::string_view,std::string_view>,3> patchLanguages{{{"es","spanish"},{"ru","russian"},{"pl","polish"}}};
  auto found=std::find_if(patchLanguages.begin(),patchLanguages.end(),[&](const auto& p){return p.first==language;});
  if(found==patchLanguages.end())throw std::runtime_error("unsupported text language "+language+"; choose tr, en or de (es, ru, pl need the community patch language data)");
  const auto file="_data/language/"+std::string(found->second)+"/subtitle/general.txt";
  if(bonus||!assets.contains("K22Widescreen.wrs")||!assets.contains(file))throw std::runtime_error("--language "+language+" requires the installed community patch language data (K22Widescreen.wrs and "+file+"); this installation supports tr, en, de");
 }
 if(audioLanguage!="tr"&&audioLanguage!="en")throw std::runtime_error("unsupported audio language");
 int bonusAnti=-1;
 for(auto& d:defines){if(d.empty()||d.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos)throw std::runtime_error("invalid compiler definition");if(!bonus)continue;d=lower(d);if(d=="defskor_d3d")throw std::runtime_error("Bonus native mode is offline; defskor_d3d is not supported");if(d.starts_with("defdil_")&&d!="defdil_"+language)throw std::runtime_error("Bonus locale define conflicts with selected language");if(d.starts_with("defanti_")){if(d.size()!=9||d.back()<'0'||d.back()>'3')throw std::runtime_error("Bonus AA define must be defanti_0, defanti_1, defanti_2 or defanti_3");int option=d.back()-'0';if(bonusAnti>=0&&bonusAnti!=option)throw std::runtime_error("conflicting Bonus AA defines");bonusAnti=option;}}
 if(bonusAnti<0)bonusAnti=0;
 mount_archives(assets,bonus);
 std::unordered_map<std::string,std::filesystem::path> sourceFiles;std::string entryName;
 if(!entry.empty()&&(!bonus||entry.is_absolute())&&std::filesystem::is_regular_file(entry)){entry=std::filesystem::canonical(entry);auto sourceRoot=entry.parent_path();for(const auto& f:std::filesystem::recursive_directory_iterator(sourceRoot)){if(f.is_symlink())continue;if(f.is_regular_file()&&lower(f.path().extension().string())==".wdl")sourceFiles.emplace(lower(f.path().lexically_relative(sourceRoot).generic_string()),f.path());}entryName=entry.filename().string();}
 else if(bonus)entryName=entry.empty()?"K22_mode.wdl":entry.generic_string();
 else if(!entry.empty())entryName=entry.generic_string();else if(assets.contains("K22Widescreen.wdl"))entryName="K22Widescreen.wdl";else if(assets.contains("K22.wdl"))entryName="K22.wdl";else throw std::runtime_error("no full game WDL entry in supplied game data; provide --script ENTRY");
 // A6 00420120: .WDL is archive-only (0062119c set in ISO and patch executables); only an explicit --script may be loose.
 auto source=[&](std::string_view name){auto key=lower(name);auto it=sourceFiles.find(key);if(it!=sourceFiles.end()){std::ifstream in(it->second,std::ios::binary);if(!in)throw std::runtime_error("cannot read script "+it->second.string());auto size=std::filesystem::file_size(it->second);if(size>16*1024*1024)throw std::runtime_error("script exceeds16MiB safety bound");std::string text(size,'\0');if(size&&!in.read(text.data(),std::streamsize(size)))throw std::runtime_error("truncated script read");return text;}return bonus||entry.empty()?assets.archive_text(name):assets.text(name);};
 // K22.wdl original:580-584 / ISO:649-653: legacy campaign gate, not a D3D backend declaration.
 k22::RenderOptions renderOptions;renderOptions.backend=rendererName=="vulkan"?k22::RendererBackend::Vulkan:k22::RendererBackend::OpenGL;renderOptions.rayTracing=rayTracing!="off";if(bonus){constexpr int samples[]={0,1,4,9};renderOptions.samples=samples[bonusAnti];}
 k22::graphics_cli::apply(renderOptions,graphics);
 std::unique_ptr<k22::Renderer> renderer;if(!headless)renderer=k22::graphics_cli::create(renderOptions,graphics);k22::Media media(assets,!headless);k22::State state;if(bonus)state.profile=k22::SemanticProfile::BonusA7;k22::Engine engine(assets,renderer.get(),media,state);
 engine.controller=k22::load_controller_config(std::cerr);if(engine.controller.modern)std::cerr<<"kabus22: controller mode modern (deadzone "<<engine.controller.deadzone<<", sensitivity "<<engine.controller.sensitivity<<", vibration "<<engine.controller.vibration<<")\n";
 if(bonus){state.compilerMacros.emplace("defmode_d3d","1");state.compilerMacros.emplace("defanti_"+std::to_string(bonusAnti),"1");}else{state.compilerMacros.emplace("defiae_d3d","1");state.compilerMacros.emplace("defses_"+audioLanguage,"1");}
 state.compilerMacros.emplace("defdil_"+language,"1");for(auto& d:defines)state.compilerMacros.insert_or_assign(d,"1");k22::Runtime runtime(state,[&](auto& r,auto& p,auto n,auto args){return engine.builtin(r,p,n,args);});runtime.load(entryName,source,[&](std::string_view name){assets.mount(name);});
 engine.initialize_plugins(runtime);engine.configure_characters(runtime,character);
 if(!bonus)engine.configure_language(runtime,language,explicitLanguage);
 if(cameraName=="first-person"){if(bonus||lower(entryName)=="k22widescreen.wdl")throw std::runtime_error("--camera first-person applies to the original campaign scripts (patched scripts provide their own F5 camera)");engine.cameraMode=k22::Engine::CameraMode::FirstPerson;}
 engine.seamlessDoors=graphics.seamlessDoors&&!bonus; // --graphics seamless_doors=on|off (docs/SEAMLESS_DOORS.md)
 const bool originalCampaign=!bonus&&entry.empty()&&lower(entryName)=="k22.wdl";
 if((!headless||displayDimensions)&&originalCampaign){const bool admitted=engine.configure_display(runtime,true,display=="native",displayWidth,displayHeight,windowMode);if(display=="native"&&(displayDimensions||displayMode)&&!admitted)throw std::runtime_error("native display overrides require the original campaign resolution/UI function graph");}
 else if(displayDimensions||displayMode)throw std::runtime_error("native display overrides require the auto-selected original campaign (headless also requires explicit resolution)");
 // Scenario determinism: a fixed random() seed and, unless "realtime on", media time stepped by the frame (docs/GAMEPLAY_TESTING.md).
 std::optional<k22::Scenario> scenario;if(!scenarioFile.empty()){scenario.emplace(scenarioFile,scenarioOut);engine.random.seed(scenario->seed());engine.fixedRandom=true;if(!scenario->realtime())media.advance_clock(0);}
 const auto& pm=runtime.metrics();std::cout<<"Parsed "<<pm.files<<" WDL files, "<<pm.functions<<" functions, "<<pm.statements<<" statements\n";for(auto& d:runtime.diagnostics())std::cerr<<"WDL: "<<d<<'\n';runtime.bootstrap();
 k22::Input input;std::uint64_t count=0;
 auto frameStart=renderer||scenario?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 const double step=graphics.fpsLimit?double(k22::render_policy::frame_period_raw(graphics.fpsLimit,0))/1024000:1.0/60;
 while(!engine.quit&&(!frameLimit||count<frames)){
  // Scenarios use a fixed step (1/60 s, or the --graphics fps_limit period); "realtime on" paces it to the wall clock so movies/sounds keep their timing.
  // Seamless doors: held (unpresented) load ticks run back to back, so the swap costs their CPU time, not frame periods.
  if((scenario?scenario->realtime():renderer!=nullptr)&&!engine.hold_presentation()){
   // A6 00414650: floor(1000*1024*1024 / max(FPS_MAX raw,1024)) raw milliseconds, unless fps_limit overrides it.
   // Frame generation presents two frames per rendered frame: a numeric fps_limit caps the presented rate (renders at half
   // of it); the script's own fps_max (original) keeps pacing the rendered game ticks.
   const auto periodRaw=(scenario&&!graphics.fpsLimit?1000LL*1024*1024/(60*1024):k22::render_policy::frame_period_raw(graphics.fpsLimit,engine.global("fps_max").raw()))*(graphics.fpsLimit>0&&renderer&&renderer->presentation_state().frameGeneration?2:1);
   const auto deadline=frameStart+std::chrono::nanoseconds(periodRaw*1'000'000/1024);
   if(std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_until(deadline);
   // Anchor to the actual start, not completion or an overdue scheduled start.
   frameStart=std::chrono::steady_clock::now();
  }
  // K22_FRAME_TIMING=FILE: one line per frame, microseconds: frame poll+scenario tick draw finish_frame, then live particles, door phase, held (seamless doors).
  static std::FILE* timingLog=[]{const char* p=std::getenv("K22_FRAME_TIMING");return p&&*p?std::fopen(p,"w"):nullptr;}();
  const auto t0=timingLog?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
  if(renderer)renderer->poll(input);else input.dt=1.0/60;
  if(scenario){input.dt=step;if(!scenario->step(engine,runtime,renderer.get(),input,count))break;if(scenario->realtime())media.real_clock();else media.advance_clock(step);}
  const auto t1=timingLog?std::chrono::steady_clock::now():t0;
  engine.tick(runtime,engine.skip_intro(runtime,engine.console(runtime,input)));
  const auto t2=timingLog?std::chrono::steady_clock::now():t0;
  // Seamless doors: the frames between the destination's Level_Load and its Giris placement are not presented.
  if(renderer&&!engine.hold_presentation())renderer->draw(engine.frame());
  const auto t3=timingLog?std::chrono::steady_clock::now():t0;
  engine.finish_frame(runtime);++count;
  if(timingLog){const auto t4=std::chrono::steady_clock::now();auto us=[](auto d){return (long long)std::chrono::duration_cast<std::chrono::microseconds>(d).count();};std::fprintf(timingLog,"%llu %lld %lld %lld %lld %zu %d %d\n",(unsigned long long)count-1,us(t1-t0),us(t2-t1),us(t3-t2),us(t4-t3),engine.activeParticles.size(),engine.door_phase(),int(engine.hold_presentation()));}
 }
 if(renderer&&!capture.empty())renderer->capture(capture.string());std::cout<<"Native frames="<<count<<" live_processes="<<runtime.processes()<<" entities="<<engine.entities.size()<<" save_root="<<engine.saveRoot.string()<<'\n';return scenario?scenario->finish(std::cout,count,engine.quit):0;
 }catch(const std::exception& e){std::cerr<<"kabus22: "<<e.what()<<'\n';return 1;}}
