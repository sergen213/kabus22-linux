#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
namespace {
std::string lower(std::string_view text){std::string out;out.reserve(text.size());for(unsigned char c:text)out.push_back(c=='\\'?'/':c>='A'&&c<='Z'?char(c+32):char(c));return out;}
void help(){std::cout<<"kabus22 native engine\nUsage: kabus22 --data GAME_ROOT [--mode campaign|bonus] [--script ENTRY] [--language tr|en|de] [--audio-language tr|en] [--define SYMBOL] [--renderer opengl|vulkan] [--ray-tracing on|off] [--headless] [--frames N] [--capture FILE]\nOpenGL is the default. Vulkan defaults to hardware ray tracing; --renderer vulkan --ray-tracing off selects raster only. Requested unsupported Vulkan/RT fails explicitly, without fallback.\nDefault mode is campaign. Bonus defaults to K22_mode.wdl, remembered ygm.k22 locale (otherwise en), and defanti_0; --define defanti_0|1|2|3 selects Bonus AA.\nReads the supplied original game data/scripts; uses private XDG saves. No proprietary assets are embedded.\n";}
}
int main(int argc,char** argv){try{
 std::filesystem::path data,entry,capture;bool headless=false;std::uint64_t frames=0;bool frameLimit=false;std::string mode="campaign",language,audioLanguage,rendererName="opengl",rayTracing;std::vector<std::string> defines;
 for(int i=1;i<argc;++i){std::string_view a=argv[i];auto value=[&](){if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));return std::string(argv[++i]);};if(a=="--help"||a=="-h"){help();return 0;}if(a=="--version"){std::cout<<"kabus22 native engine 0.1\n";return 0;}if(a=="--data")data=value();else if(a=="--mode")mode=value();else if(a=="--script")entry=value();else if(a=="--language")language=value();else if(a=="--audio-language")audioLanguage=value();else if(a=="--define")defines.push_back(value());else if(a=="--renderer")rendererName=value();else if(a=="--ray-tracing"){rayTracing=value();if(rayTracing!="on"&&rayTracing!="off")throw std::runtime_error("--ray-tracing requires on or off");}else if(a=="--capture")capture=value();else if(a=="--headless")headless=true;else if(a=="--frames"){auto n=value();auto [p,e]=std::from_chars(n.data(),n.data()+n.size(),frames);if(e!=std::errc{}||p!=n.data()+n.size())throw std::runtime_error("invalid frame limit");frameLimit=true;}else throw std::runtime_error("unknown option "+std::string(a));}
 if(data.empty())throw std::runtime_error("--data GAME_ROOT is required");if(headless&&!capture.empty())throw std::runtime_error("--capture requires actual renderer output, not --headless");
 if(rendererName!="opengl"&&rendererName!="vulkan")throw std::runtime_error("unsupported renderer; choose opengl or vulkan");
 if(!rayTracing.empty()&&rendererName!="vulkan")throw std::runtime_error("--ray-tracing applies only to --renderer vulkan");
 if(headless&&rendererName=="vulkan")throw std::runtime_error("--renderer vulkan requires a real window, not --headless");
 if(mode!="campaign"&&mode!="bonus")throw std::runtime_error("unsupported mode; choose campaign or bonus");const bool bonus=mode=="bonus";
 const bool explicitLanguage=!language.empty();
 k22::Assets assets(data,bonus);
 if(language.empty()&&bonus){language="en";if(assets.contains("ygm.k22")){auto marker=assets.text("ygm.k22");auto remembered=std::string_view(marker).substr(0,marker.find_first_of("\r\n"));if(remembered=="111")language="tr";else if(remembered=="222")language="de";}}
 if(language.empty()){const char* locale=std::getenv("LC_ALL");if(!locale||!*locale)locale=std::getenv("LC_MESSAGES");if(!locale||!*locale)locale=std::getenv("LANG");auto name=locale?lower(locale):std::string{};language=name.starts_with("tr")?"tr":name.starts_with("de")?"de":"en";}if(audioLanguage.empty())audioLanguage=language=="tr"?"tr":"en";
 if(language!="tr"&&language!="en"&&language!="de")throw std::runtime_error("unsupported text language");if(audioLanguage!="tr"&&audioLanguage!="en")throw std::runtime_error("unsupported audio language");
 int bonusAnti=-1;
 for(auto& d:defines){if(d.empty()||d.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos)throw std::runtime_error("invalid compiler definition");if(!bonus)continue;d=lower(d);if(d=="defskor_d3d")throw std::runtime_error("Bonus native mode is offline; defskor_d3d is not supported");if(d.starts_with("defdil_")&&d!="defdil_"+language)throw std::runtime_error("Bonus locale define conflicts with selected language");if(d.starts_with("defanti_")){if(d.size()!=9||d.back()<'0'||d.back()>'3')throw std::runtime_error("Bonus AA define must be defanti_0, defanti_1, defanti_2 or defanti_3");int option=d.back()-'0';if(bonusAnti>=0&&bonusAnti!=option)throw std::runtime_error("conflicting Bonus AA defines");bonusAnti=option;}}
 if(bonusAnti<0)bonusAnti=0;
 if(bonus)assets.mount("K22_mode.wrs");else for(auto name:{"K22.wrs","Kaynak.k22","K22Widescreen.wrs"})if(assets.contains(name))assets.mount(name);
 std::unordered_map<std::string,std::filesystem::path> sourceFiles;std::string entryName;
 if(!entry.empty()&&(!bonus||entry.is_absolute())&&std::filesystem::is_regular_file(entry)){entry=std::filesystem::canonical(entry);auto sourceRoot=entry.parent_path();for(const auto& f:std::filesystem::recursive_directory_iterator(sourceRoot)){if(f.is_symlink())continue;if(f.is_regular_file()&&lower(f.path().extension().string())==".wdl")sourceFiles.emplace(lower(f.path().lexically_relative(sourceRoot).generic_string()),f.path());}entryName=entry.filename().string();}
 else if(bonus)entryName=entry.empty()?"K22_mode.wdl":entry.generic_string();
 else if(!entry.empty())entryName=entry.generic_string();else if(assets.contains("K22Widescreen.wdl"))entryName="K22Widescreen.wdl";else if(assets.contains("K22.wdl"))entryName="K22.wdl";else throw std::runtime_error("no full game WDL entry in supplied game data; provide --script ENTRY");
 auto source=[&](std::string_view name){auto key=lower(name);auto it=sourceFiles.find(key);if(it!=sourceFiles.end()){std::ifstream in(it->second,std::ios::binary);if(!in)throw std::runtime_error("cannot read script "+it->second.string());auto size=std::filesystem::file_size(it->second);if(size>16*1024*1024)throw std::runtime_error("script exceeds16MiB safety bound");std::string text(size,'\0');if(size&&!in.read(text.data(),std::streamsize(size)))throw std::runtime_error("truncated script read");return text;}return bonus?assets.archive_text(name):assets.text(name);};
 // K22.wdl original:580-584 / ISO:649-653: legacy campaign gate, not a D3D backend declaration.
 k22::RenderOptions renderOptions;renderOptions.backend=rendererName=="vulkan"?k22::RendererBackend::Vulkan:k22::RendererBackend::OpenGL;renderOptions.rayTracing=rayTracing!="off";if(bonus){constexpr int samples[]={0,1,4,9};renderOptions.samples=samples[bonusAnti];}
 std::unique_ptr<k22::Renderer> renderer;if(!headless)renderer=std::make_unique<k22::Renderer>(renderOptions);k22::Media media(assets,!headless);k22::State state;if(bonus)state.profile=k22::SemanticProfile::BonusA7;k22::Engine engine(assets,renderer.get(),media,state);
 if(bonus){state.compilerMacros.emplace("defmode_d3d","1");state.compilerMacros.emplace("defanti_"+std::to_string(bonusAnti),"1");}else{state.compilerMacros.emplace("defiae_d3d","1");state.compilerMacros.emplace("defses_"+audioLanguage,"1");}
 state.compilerMacros.emplace("defdil_"+language,"1");for(auto& d:defines)state.compilerMacros.insert_or_assign(d,"1");k22::Runtime runtime(state,[&](auto& r,auto& p,auto n,auto args){return engine.builtin(r,p,n,args);});runtime.load(entryName,source,[&](std::string_view name){assets.mount(name);});
 engine.initialize_plugins(runtime);
 if(!bonus)engine.configure_language(runtime,language,explicitLanguage);
 const auto& pm=runtime.metrics();std::cout<<"Parsed "<<pm.files<<" WDL files, "<<pm.functions<<" functions, "<<pm.statements<<" statements\n";for(auto& d:runtime.diagnostics())std::cerr<<"WDL: "<<d<<'\n';runtime.bootstrap();
 k22::Input input;std::uint64_t count=0;
 auto frameStart=renderer?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 while(!engine.quit&&(!frameLimit||count<frames)){
  if(renderer){
   // A6 00414650: floor(1000*1024*1024 / max(FPS_MAX raw,1024)) raw milliseconds.
   const auto fpsRaw=std::max<std::int32_t>(1024,engine.global("fps_max").raw());
   const auto periodRaw=1000LL*1024*1024/fpsRaw;
   const auto deadline=frameStart+std::chrono::nanoseconds(periodRaw*1'000'000/1024);
   if(std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_until(deadline);
   // Anchor to the actual start, not completion or an overdue scheduled start.
   frameStart=std::chrono::steady_clock::now();
   renderer->poll(input);
  }else input.dt=1.0/60;
  engine.tick(runtime,input);
  if(renderer)renderer->draw(engine.frame());
  engine.finish_frame(runtime);++count;
 }
 if(renderer&&!capture.empty())renderer->capture(capture.string());std::cout<<"Native frames="<<count<<" live_processes="<<runtime.processes()<<" entities="<<engine.entities.size()<<" save_root="<<engine.saveRoot.string()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"kabus22: "<<e.what()<<'\n';return 1;}}
