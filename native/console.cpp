// Native developer console, quicksave/quickload and cheats (docs/CONSOLE.md). Not part of the original game:
// everything here is inert until a key or command uses it, so the original paths stay unchanged.
#include "engine.hpp"
#include "saves.hpp"
#include "wdl.hpp"
#include <charconv>
#include <cstdio>
#include <iostream>
#include <sstream>
namespace k22 {
namespace {
struct Command {std::string_view name,usage,help;};
constexpr std::array<Command,19> commands{{
 {"help","help [COMMAND]","list the commands, or show one command's usage"},
 {"save","save","quicksave (F5): writes the next slot of the game's own save list, plus the player position"},
 {"load","load [SLOT]","quickload (F9): loads the newest slot, or kayitSLOT.sav (0-98)"},
 {"god","god","toggle invulnerability (health refilled every frame)"},
 {"noclip","noclip","toggle walking through walls and entities (player PASSABLE; gravity still applies)"},
 {"give","give health|mana|ammo|weapons|all","refill health, Inzar's mana, ammo of carried weapons, or all of Demir's weapons"},
 {"get","get NAME[i] | NAME.FIELD","print a WDL global, VAR array element or object field"},
 {"set","set NAME[i] | NAME.FIELD VALUE","write a number to a WDL global, VAR array element or object field"},
 {"levels","levels","list the campaign level IDs and WMB files (LVSL_Leveller)"},
 {"level","level ID|WMB","go to a campaign level through the original door transition (LY_HedefLevel)"},
 {"map","map ID|WMB","same as level"},
 {"pos","pos","print the player position and PAN"},
 {"tp","tp X Y Z [PAN]","teleport the player"},
 {"kill","kill","kill the player (original death and game over)"},
 {"fps","fps","toggle the frame-rate counter"},
 {"timescale","timescale [FACTOR]","print or set TIME_FACTOR (0.1-10, 1 = normal)"},
 {"clear","clear","clear the console"},
 {"character","character [original|demir|ebru|inzar]","print or set the character lock (docs/CHARACTER_LOCK.md); applies from the next level entry"},
 {"quit","quit","exit the game immediately"},
}};
// Per Aktif_Player (0 Demir, 1 Ebru, 2 Inzar): health VAR and its new-game value (Yeni_Oyun_Deger.wdl Yeni_Oyun_Kolay:12-16).
constexpr std::array<std::string_view,3> healthVars{"oyn_yaralanma","ebru_yaralanma","koru_yaralanma"};
constexpr std::array<double,3> healthMax{100,100,200};
std::string lower(std::string_view s){std::string out(s);for(char& c:out)if(c>='A'&&c<='Z')c=char(c+32);return out;}
std::optional<double> parse_number(std::string_view s){double v=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),v);if(e!=std::errc{}||p!=s.data()+s.size()||!std::isfinite(v))return std::nullopt;return v;}
std::string show(double v){char text[32];std::snprintf(text,sizeof text,"%.3f",v);std::string s(text);while(s.back()=='0')s.pop_back();if(s.back()=='.')s.pop_back();return s;}
}
ObjectId Engine::live_player()const{auto p=global("player");if(p.kind!=Value::Kind::Object||!entities.contains(p.bits))return 0;auto* o=state.get(p.bits);return o&&o->alive?p.bits:0;}
void Engine::console_print(std::string_view text){
 for(std::size_t start=0;start<=text.size();){auto end=std::min(text.find('\n',start),text.size());consoleLog_.emplace_back(text.substr(start,end-start));start=end+1;}
 while(consoleLog_.size()>200)consoleLog_.pop_front();
}
void Engine::notify(std::string text){if(text.empty())return;console_print(text);std::cerr<<"kabus22: "<<text<<'\n';toast_=std::move(text);toastUntil_=time_+3;}
// Same conditions under which the original save points and doors act: a level in play, the player in control, no menu/movie/load.
std::string Engine::gameplay_blocked(const Runtime& runtime)const{
 if(state.profile!=SemanticProfile::CampaignA6||!runtime.function("YD_OyunuKaydet"))return "these scripts have no campaign save system";
 if(number("LY_AktifLevelID")==0||number("level_yuklendi_ok")==0||number("player_morph_ok")==0||!live_player())return "no level is in play";
 if(number("LY_HedefLevel")!=0||number("Yukleme_Yapilsin")!=0||pendingLevelLoad)return "a level is loading";
 if(number("game_over_start")!=0||number("oyun_finish_start")!=0)return "the game is ending";
 if(number("freeze_mode")!=0||number("DM_Acik")!=0)return "a menu is open";
 if(movie&&media.movie_playing(movie))return "a video is playing";
 if(number(number("Aktif_Player")==2?"koruyucu_pasif":"oyuncu_pasif")!=0)return "the player is not in control";
 return {};
}
std::string Engine::saved_position()const{
 auto p=live_player();if(!p)return {};auto v=position(p);
 return std::to_string(int(number("LY_AktifLevelID")))+' '+show(v.x)+' '+show(v.y)+' '+show(v.z)+' '+show(field(p,"pan"));
}
// A slot read is a load: its sidecar restores the quicksave position and the character lock (none: original, the body follows the
// story); the loaded Aktif_Player is adopted without a switch prompt.
void Engine::read_saved_position(const std::filesystem::path& slot){
 positionRestore.reset();coop={};auto meta=saves::read_meta(slot);characterLock=meta&&meta->character>=0?meta->character:characterDefault;switchPrompt_=-1;adoptStory_=true;if(!meta||meta->position.empty())return;
 std::istringstream in(meta->position);PositionRestore r;if(in>>r.level>>r.position.x>>r.position.y>>r.position.z>>r.pan&&r.level>0)positionRestore=r;
}
// YukleMenu.wdl LV_LevelKaydeta: SLG_LevelKaydet(Aktif_SaveNo,label) -> YD_OyunuKaydet -> SLG_Kaydet, the original slot writer and
// load-menu index, with a native label instead of a save-point name (SDN_isim_dizi). save_tga's aktif_kayit_sayisi count is kept.
std::string Engine::quicksave(Runtime& runtime){
 if(auto why=gameplay_blocked(runtime);!why.empty())return "Cannot save now: "+why;
 const int slot=int(number("Aktif_SaveNo"));
 set_global("aktif_kayit_sayisi",Value::from_int(std::min(255,int(number("aktif_kayit_sayisi"))+1)));
 // Labels are 18 cp1254 characters like the SDN_* save-point names (dil_ceviri_*.wdl); Turkish only with the Turkish script set.
 std::array<Argument,2> args{{{global("Aktif_SaveNo"),{}},{new_string(state.compilerMacros.contains("defdil_tr")?"H\xFDzl\xFD Kay\xFDt       ":"Quicksave         "),{}}}};
 bool saved=false;quickSaving=true;
 try{runtime.start("SLG_LevelKaydet",args);saved=bool(runtime.start("YD_OyunuKaydet"));if(saved)runtime.start("SLG_Kaydet");}catch(...){quickSaving=false;throw;}
 quickSaving=false;
 return saved?"Game saved (slot "+std::to_string(slot)+")":"Save failed";
}
// The original in-game load is Smartmenu "main menu" (Smartmenu.wdl:208-243) -> YukleMenu -> Yukleme_Kontrol. This performs the same
// teardown without the menu, then hands the slot to Yukleme_Kontrol exactly as YUM_Kernel does (YukleMenu.wdl:241-247).
std::string Engine::quickload(Runtime& runtime,int slot){
 if(auto why=gameplay_blocked(runtime);!why.empty())return "Cannot load now: "+why;
 if(slot<0)slot=(int(number("Aktif_SaveNo"))+98)%99; // SLG_Kaydet advanced Aktif_SaveNo past the newest slot (SLG_MaxKayit 99)
 if(slot>98)return "Slot must be 0-98";
 const auto file="kayit"+std::to_string(slot)+".sav";
 if(!std::filesystem::exists(saveRoot/file)&&!std::filesystem::exists(saveRoot/"_data/saves"/file))return "No save in slot "+std::to_string(slot);
 positionRestore.reset();
 auto set=[&](std::string_view name,double value){if(state.globals.find(name)!=state.globals.end())set_global(name,Value::from_double(value));};
 for(auto [name,value]:{std::pair<std::string_view,double>{"fon_tamtam_basla",99},{"yagmur_olsunmu",0},{"yagmur_izin",0},{"yagmur_seskes",0},{"yagmur_surebasla",0},{"yagmur_sure",0},{"akt_music_idno",0},{"HaritaAcilabilirmi",0},{"display_noise_ok",0},{"cino_ekran_ok",0},{"LY_AktifLevelID",0},{"oyuncu_pasif",1},{"koruyucu_pasif",1}})set(name,value);
 auto object=[&](std::string_view name){auto v=global(name);return v.kind==Value::Kind::Object&&state.get(v.bits)?v.bits:ObjectId{};};
 if(auto camera=object("camera"))set_field(camera,"ambient",0);
 media.sound_stop_all();media.movie_stop(0);movie=0;
 for(auto name:{"oy_syh_pan1","oy_syh_pan2","noisescreen1","Gecici_AltYazi","LYT_AltYazi","kullanildi_pnl","ozel_Altdiyalog","NBM_AltYazi"})if(auto id=object(name))set_field(id,"visible",0);
 const float height=vector(global_vector("screen_size")).y;
 if(auto id=object("oy_syh_pan1"))set_field(id,"pos_y",-40*height/480);
 if(auto id=object("oy_syh_pan2"))set_field(id,"pos_y",height);
 for(auto name:{"KVM_Kapat","PEM_Kapat","BEM_Kapat"})if(runtime.function(name))runtime.start(name);
 set("YUM_Cursor",slot);set("YUM_Kafa",0);set("Yukleme_Yapilsin",1);
 return "Loading slot "+std::to_string(slot);
}
void Engine::console_tick(Runtime&){
 if(!god_&&!noclip_&&!positionRestore)return;
 const auto p=live_player();const int character=body_character();
 // Damage lowers ENERJI (SKILL1) and the player loop compares it with the health VAR; keeping both at full undoes this frame's hits.
 if(god_&&p&&field(p,"skill1")>0){const auto max=healthMax[character];if(field(p,"skill1")<max)set_field(p,"skill1",max);if(number(healthVars[character])<max)set_global(healthVars[character],Value::from_double(max));}
 if(noclip_&&p){if(p!=noclipPlayer_)noclipPlayer_=p;if(!(flags(p)&512))set_field(p,"passable",1);}
 if(positionRestore){
  auto& r=*positionRestore;const bool loaded=number("level_yuklendi_ok")!=0;const int level=int(number("LY_AktifLevelID"));
  if(!r.started){if(!loaded)r.started=true;} // LevelYukle clears level_yuklendi_ok before the level_load
  else if(loaded&&level!=0){
   if(level!=r.level||!p){positionRestore.reset();return;}
   // Re-applied until the player gets control: a LV_*_Giris that waits may still move the player after LY_AktifLevelID is set.
   set_position(p,r.position);set_field(p,"pan",r.pan);
   if(number(character==2?"koruyucu_pasif":"oyuncu_pasif")==0||++r.ticks>=180){positionRestore.reset();notify("Game loaded");}
  }
 }
}
const Input& Engine::console(Runtime& runtime,const Input& in){
 fpsTime_+=in.dt;++fpsFrames_;if(fpsTime_>=0.5){fpsText_="FPS "+std::to_string(std::lround(fpsFrames_/fpsTime_));fpsTime_=0;fpsFrames_=0;}
 auto guarded=[&](auto&& action){try{notify(action());}catch(const std::exception& e){notify(std::string("Error: ")+e.what());}};
 // The game sees no keyboard/mouse/pad input while the console is open (time keeps running), and after it closes none until every
 // key is up, so the closing key never reaches the scripts (a held Escape would open the in-game Smartmenu).
 auto neutral=[&]()->const Input&{consoleInput_=Input{};consoleInput_.dt=in.dt;consoleInput_.quit=in.quit;consoleInput_.mouseX=in.mouseX;consoleInput_.mouseY=in.mouseY;consoleInput_.joystickConnected=in.joystickConnected;return consoleInput_;};
 const bool toggle=in.pressed[SDL_SCANCODE_GRAVE]||in.pressed[SDL_SCANCODE_F10];
 if(toggle){consoleOpen_=!consoleOpen_;consoleLine_.clear();historyCursor_=consoleHistory_.size();consoleRelease_=!consoleOpen_;}
 if(!consoleOpen_){
  if(consoleRelease_){if(in.keys.any())return neutral();consoleRelease_=false;}
  // The community patch's cam.wdl polls key_f5 for its first-person camera; quicksave moves to F8 there.
  const auto saveKey=runtime.function("fps_camera_control")?SDL_SCANCODE_F8:SDL_SCANCODE_F5;
  if(in.pressed[saveKey])guarded([&]{return quicksave(runtime);});
  else if(in.pressed[SDL_SCANCODE_F9])guarded([&]{return quickload(runtime);});
  return in;
 }
 if(!toggle){ // the toggle key's own character (` or the Turkish-Q key there) arrives in the same frame
  for(unsigned char c:in.text)if(c>=32&&c<127)consoleLine_+=char(c);
  if(in.pressed[SDL_SCANCODE_BACKSPACE]&&!consoleLine_.empty())consoleLine_.pop_back();
  if(in.pressed[SDL_SCANCODE_ESCAPE]){consoleOpen_=false;consoleRelease_=true;}
  if(in.pressed[SDL_SCANCODE_UP]&&historyCursor_>0)consoleLine_=consoleHistory_[--historyCursor_];
  if(in.pressed[SDL_SCANCODE_DOWN]&&historyCursor_<consoleHistory_.size()){++historyCursor_;consoleLine_=historyCursor_<consoleHistory_.size()?consoleHistory_[historyCursor_]:std::string{};}
  if(in.pressed[SDL_SCANCODE_TAB]&&!consoleLine_.empty()&&consoleLine_.find(' ')==std::string::npos){
   const Command* match=nullptr;int count=0;for(const auto& c:commands)if(c.name.starts_with(lower(consoleLine_))){match=&c;++count;}
   if(count==1)consoleLine_=std::string(match->name)+' ';
  }
  if(in.pressed[SDL_SCANCODE_RETURN]||in.pressed[SDL_SCANCODE_KP_ENTER]){
   auto line=std::move(consoleLine_);consoleLine_.clear();console_print("] "+line);
   if(!line.empty()&&(consoleHistory_.empty()||consoleHistory_.back()!=line))consoleHistory_.push_back(line);historyCursor_=consoleHistory_.size();
   auto output=console_command(runtime,line);if(!output.empty())console_print(output);
   std::cerr<<"kabus22: console] "<<line<<'\n'<<output<<(output.empty()?"":"\n"); // terminal/scenario log
  }
 }
 return neutral();
}
std::string Engine::console_command(Runtime& runtime,std::string_view line){
 std::vector<std::string> w;{std::istringstream in{std::string(line)};for(std::string word;in>>word;)w.push_back(word);}
 if(w.empty())return {};
 const auto c=lower(w[0]);
 try{
  if(c=="help"){
   std::string out;for(const auto& command:commands)if(w.size()<2||command.name==lower(w[1]))out+=std::string(command.usage)+"  - "+std::string(command.help)+'\n';
   if(out.empty())return "unknown command '"+w[1]+"'";
   if(w.size()<2)out+="Keys: ` or F10 console, F5 quicksave (F8 with the community patch), F9 quickload";else out.pop_back();
   return out;
  }
  if(c=="save")return quicksave(runtime);
  if(c=="load"){int slot=-1;if(w.size()>1){auto n=parse_number(w[1]);if(!n||*n<0||*n>98||*n!=std::floor(*n))return "load [SLOT]: SLOT is 0-98";slot=int(*n);}return quickload(runtime,slot);}
  if(c=="clear"){consoleLog_.clear();return {};}
  if(c=="quit"){quit=true;return "Quitting";}
  if(c=="fps"){fpsOverlay_=!fpsOverlay_;return fpsOverlay_?"FPS counter on":"FPS counter off";}
  if(c=="timescale"){
   if(w.size()<2)return "timescale "+show(number("time_factor"));
   auto n=parse_number(w[1]);if(!n||*n<0.1||*n>10)return "timescale FACTOR: 0.1-10";
   set_global("time_factor",Value::from_double(*n));return "timescale "+show(*n);
  }
  if(c=="get"||c=="set"){
   if(w.size()<(c=="get"?2u:3u))return std::string(c=="get"?"get NAME[i] | NAME.FIELD":"set NAME[i] | NAME.FIELD VALUE");
   std::string name=w[1],member;std::optional<std::size_t> index;
   if(auto dot=name.find('.');dot!=std::string::npos){member=name.substr(dot+1);name.resize(dot);}
   else if(auto open=name.find('[');open!=std::string::npos){auto n=name.back()==']'?parse_number(std::string_view(name).substr(open+1,name.size()-open-2)):std::nullopt;if(!n||*n<0||*n!=std::floor(*n))return "expected NAME[INDEX]";index=std::size_t(*n);name.resize(open);}
   auto it=state.globals.find(name);if(it==state.globals.end())return "unknown WDL global "+name;
   Slot* slot=&it->second;
   if(index){auto* o=slot->value.kind==Value::Kind::Object?state.get(slot->value.bits):nullptr;if(!o||*index>=o->array.size())return w[1]+" is not an array element";slot=&o->array[*index];}
   std::optional<double> value;if(c=="set"){value=parse_number(w[2]);if(!value)return "VALUE must be a number";}
   if(!member.empty()){ // object globals, or pointer words in plain VARs
    auto id=slot->value.bits;auto* o=state.get(id);if(!o||!o->alive||o->kind==Object::Kind::String)return name+" is not a live object";
    if(value)set_field(id,member,*value);return w[1]+" = "+show(field(id,member));
   }
   if(value){if(slot->value.kind!=Value::Kind::Number)return w[1]+" is not a number";slot->value=Value::from_double(*value);}
   const auto& v=slot->value;
   if(v.kind==Value::Kind::Object){auto* o=state.get(v.bits);if(o&&o->kind==Object::Kind::String)return w[1]+" = \""+o->text+'"';return w[1]+" = object #"+std::to_string(v.bits)+(o&&!o->array.empty()?" ("+std::to_string(o->array.size())+" elements)":"");}
   if(v.kind==Value::Kind::Function)return w[1]+" = function";
   return w[1]+" = "+show(v.real());
  }
  if(c=="levels"||c=="level"||c=="map"){
   auto list=global("LVSL_Leveller");auto* table=list.kind==Value::Kind::Object?state.get(list.bits):nullptr;
   if(!table||table->array.empty())return "these scripts have no campaign level table (LVSL_Leveller)";
   auto wmb=[&](std::size_t i){auto* s=state.get(table->array[i].value.bits);return s&&s->kind==Object::Kind::String?s->text:std::string{};};
   if(c=="levels"){std::string out;for(std::size_t i=1;i<table->array.size();++i)if(auto name=wmb(i);!name.empty())out+=std::to_string(i)+' '+name+(i+1<table->array.size()?"\n":"");return out;}
   if(w.size()<2)return c+" ID|WMB";
   std::size_t id=0;if(auto n=parse_number(w[1]))id=std::size_t(std::max(0.0,*n));
   else for(std::size_t i=1;i<table->array.size();++i){auto name=lower(wmb(i));if(!name.empty()&&(name==lower(w[1])||name==lower(w[1])+".wmb")){id=i;break;}}
   if(id<1||id>=table->array.size()||wmb(id).empty())return "unknown level "+w[1]+" (see levels)";
   if(auto why=gameplay_blocked(runtime);!why.empty())return "Cannot change level now: "+why;
   set_global("LY_HedefLevel",Value::from_int(int(id)));return "Going to level "+std::to_string(id)+' '+wmb(id);
  }
  if(c=="character"){
   static constexpr std::array<std::string_view,4> choices{"original","demir","ebru","inzar"};
   if(w.size()>1){auto it=std::find(choices.begin(),choices.end(),lower(w[1]));if(it==choices.end())return "character [original|demir|ebru|inzar]";if(state.profile!=SemanticProfile::CampaignA6)return "character applies to the campaign only";set_character_lock(int(it-choices.begin())-1);}
   return "character "+std::string(choices[characterLock+1])+" (story: "+std::string(choices[story_character()+1])+")";
  }
  // Player commands below need the player entity.
  const auto p=live_player();const int character=body_character();
  if(c!="god"&&c!="noclip"&&c!="give"&&c!="pos"&&c!="tp"&&c!="kill")return "unknown command '"+w[0]+"' (try help)";
  if(!p)return "no player entity";
  // ENERJI = SKILL1 and the health/weapon VARs are the campaign scripts' (oyuncu2.wdl:42, silah.wdl); Bonus uses its own.
  if((c=="god"||c=="give"||c=="kill")&&state.profile!=SemanticProfile::CampaignA6)return c+" applies to the campaign only";
  if(c=="god"){god_=!god_;return god_?"God mode on":"God mode off";}
  if(c=="noclip"){
   noclip_=!noclip_;
   if(!noclip_){if(noclipPlayer_&&state.get(noclipPlayer_)&&state.get(noclipPlayer_)->alive)set_field(noclipPlayer_,"passable",0);noclipPlayer_=0;return "Noclip off";}
   console_tick(runtime);return "Noclip on";
  }
  if(c=="pos"){auto v=position(p);return "pos "+show(v.x)+' '+show(v.y)+' '+show(v.z)+" pan "+show(field(p,"pan"))+" level "+std::to_string(int(number("LY_AktifLevelID")))+' '+currentLevel;}
  if(c=="tp"){
   if(w.size()<4)return "tp X Y Z [PAN]";std::array<double,4> v{};
   for(std::size_t i=0;i+1<w.size()&&i<4;++i){auto n=parse_number(w[i+1]);if(!n)return "tp X Y Z [PAN]: numbers expected";v[i]=*n;}
   set_position(p,{float(v[0]),float(v[1]),float(v[2])});if(w.size()>4)set_field(p,"pan",v[3]);return "Teleported";
  }
  if(c=="kill"){god_=false;set_field(p,"skill1",0);return "Killed the player";}
  // give: original inventory paths only (silah.wdl Silah_Al / oyunici_kursun_al), never invented items.
  if(c=="give"){
   if(w.size()<2)return "give health|mana|ammo|weapons|all";
   const auto what=lower(w[1]);std::string out;
   auto array=[&](std::string_view name)->Object*{auto v=global(name);return v.kind==Value::Kind::Object?state.get(v.bits):nullptr;};
   auto element=[](Object* o,std::size_t i)->Slot*{return o&&i<o->array.size()?&o->array[i]:nullptr;};
   auto carried=[&](int weapon){auto* list=array("Silahlar");if(!list)return false;for(auto& s:list->array)if(s.value.raw()==weapon*1024)return true;return false;};
   if(what=="health"||what=="all"){const auto max=healthMax[character];set_field(p,"skill1",max);set_global(healthVars[character],Value::from_double(max));out+="health "+show(max)+'\n';}
   if(what=="mana"||what=="all"){if(state.globals.find("koruyucu_mana")!=state.globals.end()){set_global("koruyucu_mana",Value::from_int(230));out+="mana 230\n";}}
   auto* capacity=array("Silah_Kapasite");auto* magazine=array("S_Kursun_Sayisi");auto* list=array("Silahlar");
   if((what=="weapons"||what=="all")&&character==0&&list&&capacity&&magazine){ // Demir's weapons 1-7 (Silah_isimleri); Silah_Al's new-weapon branch
    int added=0;for(int weapon=1;weapon<=7;++weapon){if(carried(weapon))continue;auto count=int(number("SilahSayisi"));auto* slot=element(list,std::size_t(count));if(!slot)break;
     slot->value=Value::from_int(weapon);element(magazine,std::size_t(weapon))->value=element(capacity,std::size_t(weapon))->value;set_global("SilahSayisi",Value::from_int(count+1));++added;}
    out+="weapons +"+std::to_string(added)+'\n';
   }
   else if(what=="weapons")return "only Demir carries weapons";
   if((what=="ammo"||what=="all")&&capacity&&magazine){
    int refilled=0;for(int weapon=1;weapon<=8;++weapon){if(!carried(weapon)&&int(number("Aktif_Silah"))!=weapon)continue;auto full=element(capacity,std::size_t(weapon))->value;element(magazine,std::size_t(weapon))->value=full;
     // Grenades/mines (6, 7) live in the magazine count only (Silah_Al); others get three magazines in reserve via the pickup function.
     if(weapon!=6&&weapon!=7&&runtime.function("oyunici_kursun_al")){std::array<Argument,2> args{{{Value::from_int(weapon),{}},{Value::from_double(full.real()*3),{}}}};runtime.start("oyunici_kursun_al",args);}
     ++refilled;}
    out+="ammo refilled for "+std::to_string(refilled)+" weapons\n";
   }
   if(out.empty())return "give health|mana|ammo|weapons|all";
   out.pop_back();return out;
  }
 }catch(const std::exception& e){return std::string("error: ")+e.what();}
 return {};
}
void Engine::console_overlay(){
 const bool toast=!toast_.empty()&&time_<toastUntil_;
 if(!renderer||currentWidth_<=0||currentHeight_<=0||!(consoleOpen_||toast||fpsOverlay_))return;
 const float width=float(currentWidth_),height=float(currentHeight_);const int pixels=std::clamp(int(height/40),12,48);
 const auto& font=renderer->font_atlas("monospace",pixels);const float line=font.lineHeight*1.15f,margin=pixels*0.5f;
 // Drop shadow first (as the engine's own TTF SHADOW texts), so toasts and the FPS counter stay readable over bright scenes.
 auto text=[&](float x,float y,std::string_view s,Color color){UICommand t;t.kind=UICommand::Kind::Text;t.font=&font;t.text=s;t.rect={x+1,y+1,Renderer::text_extent(font,s).x,font.lineHeight};t.color={0,0,0,color.a};t.filter=true;ui_.push_back(t);t.rect.x=x;t.rect.y=y;t.color=color;ui_.push_back(t);};
 float top=margin;
 if(consoleOpen_){
  const float bottom=std::floor(height*0.45f);UICommand back;back.kind=UICommand::Kind::Solid;back.rect={0,0,width,bottom};back.color={0,0,0,0.78f};ui_.push_back(back);
  UICommand edge=back;edge.rect={0,bottom,width,2};edge.color={0.8f,0.6f,0.2f,0.9f};ui_.push_back(edge);
  consolePrompt_="] "+consoleLine_+'_';float y=bottom-line-margin*0.5f;text(margin,y,consolePrompt_,{1,0.9f,0.5f,1});
  for(auto it=consoleLog_.rbegin();it!=consoleLog_.rend()&&y-line>=0;++it){y-=line;text(margin,y,*it,{0.85f,0.85f,0.85f,1});}
  top=bottom+margin;
 }
 if(toast)text(std::floor((width-Renderer::text_extent(font,toast_).x)/2),top,toast_,{1,1,1,1}); // centred: the HUD owns the top-left corner
 if(fpsOverlay_)text(width-Renderer::text_extent(font,fpsText_).x-margin,top,fpsText_,{0.5f,1,0.5f,1});
 frame_.ui=ui_;
}
}
