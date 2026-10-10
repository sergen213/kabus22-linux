#include "controller.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
namespace k22 {
namespace {
static_assert(SDL_CONTROLLER_BUTTON_MAX==21,"controller.ini button names follow the SDL2 2.0.14+ button order");
constexpr std::array<std::string_view,ControllerConfig::Sources> names{"a","b","x","y","back","guide","start","leftstick","rightstick","leftshoulder","rightshoulder","dpup","dpdown","dpleft","dpright","misc1","paddle1","paddle2","paddle3","paddle4","touchpad","lefttrigger","righttrigger"};
std::string_view trim(std::string_view s){auto b=s.find_first_not_of(" \t\r");if(b==std::string_view::npos)return {};return s.substr(b,s.find_last_not_of(" \t\r")-b+1);}
std::string lower(std::string_view s){std::string out(s);for(char& c:out)if(c>='A'&&c<='Z')c=char(c+32);return out;}
}
std::array<std::uint8_t,ControllerConfig::Sources> ControllerConfig::default_buttons(){
 // Shipped options.k22 binds aim/fire/run/menu/map/spell to original buttons 1-6 (GenelAyarlar.wdl:188 GA_JTuslar).
 std::array<std::uint8_t,Sources> b{};b[LeftTrigger]=1;b[RightTrigger]=2;b[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]=3;b[SDL_CONTROLLER_BUTTON_START]=4;b[SDL_CONTROLLER_BUTTON_BACK]=5;b[SDL_CONTROLLER_BUTTON_Y]=6;
 b[SDL_CONTROLLER_BUTTON_A]=7;b[SDL_CONTROLLER_BUTTON_B]=8;b[SDL_CONTROLLER_BUTTON_X]=9;b[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]=10;b[SDL_CONTROLLER_BUTTON_LEFTSTICK]=11;b[SDL_CONTROLLER_BUTTON_RIGHTSTICK]=12;return b;
}
ControllerConfig parse_controller_config(std::string_view text,std::ostream& warnings){
 ControllerConfig c;std::string section;int line=0;
 auto bad=[&](std::string_view what){warnings<<"kabus22: controller.ini:"<<line<<": "<<what<<"; using default\n";};
 auto real=[](std::string_view v,float low,float high,float& out){float n{};auto [p,e]=std::from_chars(v.data(),v.data()+v.size(),n);if(e!=std::errc{}||p!=v.data()+v.size()||!(n>=low&&n<=high))return false;out=n;return true;};
 while(!text.empty()){
  ++line;auto end=text.find('\n');auto row=text.substr(0,end);text=end==std::string_view::npos?std::string_view{}:text.substr(end+1);
  row=trim(row.substr(0,row.find_first_of(";#")));if(row.empty())continue;
  if(row.front()=='['){section=row.back()==']'?lower(trim(row.substr(1,row.size()-2))):"";if(section!="controller"&&section!="buttons")bad("unknown section "+std::string(row));continue;}
  auto equal=row.find('=');if(equal==std::string_view::npos){bad("expected key=value");continue;}
  auto key=lower(trim(row.substr(0,equal)));auto value=lower(trim(row.substr(equal+1)));
  if(section=="controller"){
   if(key=="mode"){if(value=="original"||value=="modern")c.modern=value=="modern";else bad("mode must be original or modern");}
   else if(key=="deadzone"){if(!real(value,0,0.9f,c.deadzone))bad("deadzone must be 0.0-0.9");}
   else if(key=="sensitivity"){if(!real(value,0.25f,4,c.sensitivity))bad("sensitivity must be 0.25-4.0");}
   else if(key=="vibration"){if(value=="0"||value=="1")c.vibration=value=="1";else bad("vibration must be 0 or 1");}
   else bad("unknown key "+key);
  }else if(section=="buttons"){
   auto source=std::find(names.begin(),names.end(),key);if(source==names.end()){bad("unknown button "+key);continue;}
   int n=-1;auto [p,e]=std::from_chars(value.data(),value.data()+value.size(),n);
   if(e==std::errc{}&&p==value.data()+value.size()&&n>=0&&n<=12)c.buttons[std::size_t(source-names.begin())]=std::uint8_t(n);else bad("button "+key+" must map to 0-12");
  }else bad("key outside [Controller]/[Buttons]");
 }
 return c;
}
ControllerConfig load_controller_config(std::ostream& warnings){
 const char* xdg=std::getenv("XDG_CONFIG_HOME");const char* home=std::getenv("HOME");
 if((!xdg||!*xdg)&&(!home||!*home))return {};
 auto path=(xdg&&*xdg?std::filesystem::path(xdg):std::filesystem::path(home)/".config")/"kabus22/controller.ini";
 std::error_code error;if(!std::filesystem::is_regular_file(path,error))return {}; // Absent ⇒ original mode.
 if(std::filesystem::file_size(path,error)>65536||error){warnings<<"kabus22: controller.ini too large or unreadable; using original mode\n";return {};}
 std::ifstream in(path,std::ios::binary);std::string text(std::istreambuf_iterator<char>(in),{});
 if(!in.good()&&!in.eof()){warnings<<"kabus22: cannot read controller.ini; using original mode\n";return {};}
 return parse_controller_config(text,warnings);
}
std::array<float,2> radial_deadzone(float x,float y,float deadzone){
 float magnitude=std::hypot(x,y);if(!(magnitude>deadzone))return {0,0};
 float scale=std::min(1.f,(magnitude-deadzone)/(1-deadzone))/magnitude;return {x*scale,y*scale};
}
Vec3 apply_modern_controller(const ControllerConfig& c,Input& in,std::bitset<32>& held){
 const auto& a=in.controllerAxes;std::bitset<32> now;
 for(int s=0;s<ControllerConfig::Sources;++s){
  if(!c.buttons[s])continue;
  bool down=s<SDL_CONTROLLER_BUTTON_MAX?in.controllerButtons[s]||in.controllerPressed[s]:a[s==ControllerConfig::LeftTrigger?SDL_CONTROLLER_AXIS_TRIGGERLEFT:SDL_CONTROLLER_AXIS_TRIGGERRIGHT]>=0.5f;
  if(down)now.set(c.buttons[s]-1);
 }
 // ponytail: the mapped pad owns legacy slot 0; a second raw device stays in slot 1 only.
 in.joystickButtons[0]=now;in.joystickPressed[0]=now&~held;in.joystickReleased[0]=held&~now;held=now;
 auto [lx,ly]=radial_deadzone(a[SDL_CONTROLLER_AXIS_LEFTX],a[SDL_CONTROLLER_AXIS_LEFTY],c.deadzone);
 auto [rx,ry]=radial_deadzone(a[SDL_CONTROLLER_AXIS_RIGHTX],a[SDL_CONTROLLER_AXIS_RIGHTY],c.deadzone);(void)ry;
 float turn=std::clamp((lx+rx)*c.sensitivity,-1.f,1.f);
 // ponytail: half-deflection forward threshold keeps turning from walking; SDL +Y is down.
 float forward=ly<=-0.5f?2.f:ly>=0.5f?-2.f:0.f;
 // Standing, oyuncu2.wdl:781/1560/1593 turn only on Key_Sol/Key_Sag==1, so a partial turn did nothing there:
 // turning in place is digital past the same half threshold; while walking (:820) it stays analog.
 if(forward==0)turn=turn>=0.5f?1.f:turn<=-0.5f?-1.f:0.f;
 return {turn*2,forward,a[SDL_CONTROLLER_AXIS_TRIGGERLEFT]*100};
}
float DamageRumble::update(std::uint32_t id,double current){
 float strength=0;if(id&&id==player&&current<health)strength=current<=0?1.f:std::clamp(0.35f+float(health-current)/100.f,0.35f,1.f);
 player=id;health=current;return strength;
}
}
