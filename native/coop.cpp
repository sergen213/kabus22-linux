// Local two-player coop gameplay (docs/COOP.md). Not in the original.
// P1 is the original player: bank 0 of the per-player global banks (Runtime::enable_banks), the story, cameras, HUD scripts and
// saves follow it. P2 is a second player action created natively beside P1 after every level entry, running in bank 1: its own
// `player`, Key_*, weapon, health and hit globals. Enemies resolve, at every resume, to the bank of the live player nearest to them.
#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
namespace k22 {
namespace {
constexpr std::array<std::string_view,3> actions{"Oyuncu_Demir","Oyuncu_Ebru","oyuncu_koruyucu"};
constexpr std::array<std::string_view,3> models{"demir_2007.mdl","ebru_2006.mdl","koruyucu_2006.mdl"};
constexpr std::array<std::string_view,3> healthVars{"oyn_yaralanma","ebru_yaralanma","koru_yaralanma"};
constexpr std::array<double,3> healthMax{100,100,200}; // Yeni_Oyun_Deger.wdl Yeni_Oyun_Kolay
// Player-side scripts: a function runs in the bank of the player entity it runs on (my), else in its caller's. Their assigned
// globals (Runtime::assigned_globals) are the banked set, minus `shared` below.
constexpr std::array<std::string_view,6> playerFiles{"oyuncu2.wdl","oyuncu2_ebru.wdl","koruyucu.wdl","koru_buyuler.wdl","silah.wdl","hedef.wdl"};
// Enemy AI and its hit effects: they find "the player" only through `player` (klasik_yara.wdl:294-417, 1088-1094) and write its
// hit globals; plus every *_yara.wdl.
constexpr std::array<std::string_view,7> enemyFiles{"bond_bekci.wdl","boss.wdl","latex.wdl","seytan.wdl","kotu_koruyucu.wdl","efect2.wdl","klasik_generate.wdl"};
// Read by enemies or written for the player by other scripts, not assigned in the player files.
constexpr std::array<std::string_view,17> extra{"player","Key_Ileri","Key_Geri","Key_Sag","Key_Sol","Key_SilahKaldir","Key_Ates","Key_Kosma","Key_Menu",
 "Key_Harita","Key_Buyu","dmr_yaralan_tip","d_yaralan_source","koyucu_yaralan_tip","game_over_start","Aktif_Silah","player_morph_ok"};
// Shared by both players although the player files write them: story gates, the inventory and its ammo pool, level state, time,
// engine scratch and camera globals (docs/COOP.md "Banked globals").
constexpr std::array<std::string_view,31> shared{"oyuncu_pasif","koruyucu_pasif","Aktif_Player","time_factor","level_yuklendi_ok","LY_AktifLevelID",
 "LY_HedefLevel","Silahlar","SilahSayisi","Kursun_Sayisi","S_Kursun_Sayisi","Nesneler","NesneSayisi","freeze_mode","move_mode","move_friction",
 "trace_mode","shadow_offset","camera","fog_color","aktif_oldurme_sayisi","EnvaterAcilabilirmi","HaritaAcilabilirmi","SmartAcilabilirmi",
 "ucak_olum_ok","oyun_finish_start","cadde_yara_adet","v_dmryakalan_ok","unic_obje_dizi","boss_ebruhedefpan","result"};
// Run only for P1: HUD panels (P2's HUD is native, coop_hud) and the inventory/spell menus (P2 has no menu key).
constexpr std::array<std::string_view,7> p1Only{"PEM_Ac","PEM_Kapat","KVM_Ac","KVM_Kapat","EnvanterAcKapat","KoruyucuAcKapat","EM_Kernel"};
enum Kind : std::int8_t {WorldSide,PlayerSide,EnemySide,P1Only};
bool listed(auto& list,std::string_view s){return std::any_of(list.begin(),list.end(),[&](auto x){return case_equal(x,s);});}
}
int Engine::coop_pad(int player)const{
 const bool a=input.controllerConnected[0],b=input.controllerConnected[1];
 if(a&&b)return player;if(a!=b)return player?(a?0:1):-1;return -1;
}
ObjectId Engine::coop_body(int player)const{
 if(!player)return live_player();
 auto* o=coopP2_?state.get(coopP2_):nullptr;return o&&o->alive&&entities.contains(coopP2_)?coopP2_:0;
}
// Bank resolver (Runtime::BankResolver): docs/COOP.md "Which bank runs a function".
int Engine::coop_resolve(Runtime& runtime,FunctionId f,ObjectId my,int inherited){
 if(f>=coopKinds_.size())coopKinds_.resize(f+1,-1);
 auto& kind=coopKinds_[f];
 if(kind<0){const auto file=runtime.function_file(f);const auto name=runtime.function_name(f);
  kind=listed(p1Only,name)||name.starts_with("BEM_")||name.starts_with("bem_")?P1Only:listed(playerFiles,file)?PlayerSide:listed(enemyFiles,file)||(file.size()>9&&case_equal(file.substr(file.size()-9),"_yara.wdl"))?EnemySide:WorldSide;}
 const auto p1=[&]{auto v=runtime.bank_value(0,"player");return v.kind==Value::Kind::Object?v.bits:ObjectId{};};
 switch(kind){
  case PlayerSide:
   if(my&&my==coopP2_)return 1;if(my&&my==p1())return 0;
   return coopSpawn_>=0&&inherited==0?coopSpawn_:std::max(0,inherited); // a native spawn (coop_spawn) starts in its bank
  case P1Only:return inherited==1?Runtime::BankSkip:0;
  case EnemySide:{
   if(inherited!=Runtime::BankDynamic)return Runtime::BankDynamic;
   if(!my||!entities.contains(my))return 0;
   const Vec3 at=position(my);int best=0;double nearest=INFINITY;
   for(int b=0;b<2;++b){const ObjectId body=b?coop_body(1):p1();auto* o=body?state.get(body):nullptr;if(!o||!o->alive||!entities.contains(body)||field(body,"skill1")<=0)continue;
    const Vec3 p=position(body);const double d=double(p.x-at.x)*(p.x-at.x)+double(p.y-at.y)*(p.y-at.y)+double(p.z-at.z)*(p.z-at.z);if(d<nearest){nearest=d;best=b;}}
   return best;}
  default:return 0;
 }
}
void Engine::coop_banks(Runtime& runtime,bool on){
 if(on==coopBanks_)return;
 coopBanks_=on;coopKinds_.clear();coopOver_=coopHidden_=false;coopDown_={};coopLeash_=0;coopSettle_=0;rumbleEntity.fill(0);
 if(on)coopP2_=0;
 if(!on){ // coop ends: P2's body, gun and radio go (in the next level they are gone already), the globals are shared again
  if(const auto p2=coop_body(1)){for(auto name:{"a_Silah","a_Telsiz"})if(auto v=runtime.bank_value(1,name);v.kind==Value::Kind::Object&&v.bits&&v.bits!=runtime.bank_value(0,name).bits&&v.bits!=p2)remove_entity(runtime,v.bits);remove_entity(runtime,p2);}
  coopP2_=0;runtime.enable_banks({},{});return;}
 auto names=runtime.assigned_globals(playerFiles);for(auto n:extra)names.emplace_back(n);
 std::erase_if(names,[](const std::string& n){return listed(shared,n);});
 std::sort(names.begin(),names.end(),CaseLess{});names.erase(std::unique(names.begin(),names.end(),[](auto& a,auto& b){return case_equal(a,b);}),names.end());
 // Script variants (patch scripts) may lack a global or declare one as a type that cannot be banked: those stay shared.
 for(;;){
  try{runtime.enable_banks(names,[this,&runtime](FunctionId f,ObjectId my,int inherited){return coop_resolve(runtime,f,my,inherited);});break;}
  catch(const std::invalid_argument& e){const std::string_view what=e.what();const auto a=what.find('\''),b=what.find('\'',a+1);if(a==std::string_view::npos||b==std::string_view::npos)throw;
   const std::string bad(what.substr(a+1,b-a-1));std::cerr<<"kabus22: coop: "<<what<<"; shared\n";std::erase_if(names,[&](const std::string& n){return case_equal(n,bad);});}
 }
 // P2 joins with full health and mana (the copy is P1's; the save files keep P1's values only).
 const int c=coop.characters[1];runtime.set_bank_value(1,healthVars[c],Value::from_double(healthMax[c]));if(c==2)runtime.set_bank_value(1,"koruyucu_mana",Value::from_int(230));
 std::string list;for(auto& n:names)list+=(list.empty()?"":" ")+n;
 std::cerr<<"kabus22: coop banks ("<<names.size()<<"): "<<list<<'\n';
}
// Native reads (character.cpp configure_characters): game_over_strtr's game_over_start, Demir's/Ebru's reload key.
Value Engine::coop_read(Runtime& runtime,std::string_view call){
 if(call=="k22_coop_enter"){
  if(!coop.active||runtime.active_bank()!=1)return global("key_enter");
  const int pad=coop_pad(1);return Value::from_int(pad>=0&&(input.controllerButtons[pad][SDL_CONTROLLER_BUTTON_A]||input.controllerPressed[pad][SDL_CONTROLLER_BUTTON_A]));
 }
 const auto own=global("game_over_start");if(!coop.active||!coopBanks_)return own;
 // Game over when both players are down, when one is down and the other has no living body to respawn beside, or for a
 // scripted death while P1 still lives (dinamik_modeller.wdl:1040 plane crash). Otherwise the one down respawns (coop_play).
 // A body missing between levels is not "down": only a death loop's own game_over_start is.
 auto live=[&](int b){const auto body=coop_body(b);return body&&field(body,"skill1")>0;};
 const bool d0=own.real()!=0,d1=runtime.bank_value(1,"game_over_start").real()!=0;
 if((d0&&d1)||(d0&&(live(0)||!live(1)))||(d1&&!live(0))){coopOver_=true;return Value::from_int(1);}
 return Value{};
}
// A free spot beside ANCHOR: its own hull swept from it (USE_BOX|IGNORE_PASSABLE, the scenario planner's query), on a floor at most
// 32 units off its own; right, left, behind, closer (tunnels), then wider. ponytail: fixed candidates, the anchor's spot when all fail.
Vec3 Engine::coop_spot(ObjectId anchor){
 const Vec3 a=position(anchor);const ProcessContext ctx{0,anchor,0};
 auto floor=[&](Vec3 p)->std::optional<float>{auto h=collide({p.x,p.y,p.z+48},{p.x,p.y,p.z-400},{},4,anchor,&ctx);if(!h.hit||h.fraction<=0)return std::nullopt;return h.position.z;};
 const auto under=floor(a);if(!under)return a;const float lift=a.z-*under;
 const double pan=field(anchor,"pan")*M_PI/180;const float fx=float(std::cos(pan)),fy=float(std::sin(pan));
 static constexpr std::array<std::array<float,2>,14> tries{{{40,0},{-40,0},{0,-45},{28,0},{-28,0},{0,-30},{20,-20},{-20,-20},{0,30},{60,-30},{-60,-30},{0,-75},{0,45},{90,0}}}; // right, forward
 for(auto [r,f]:tries){
  const Vec3 c{a.x+fy*r+fx*f,a.y-fx*r+fy*f,a.z};
  if(collide({a.x,a.y,a.z+16},{c.x,c.y,c.z+16},{},4|8192,anchor,&ctx).hit)continue;
  const auto z=floor(c);if(!z||std::fabs(*z-*under)>32)continue;
  return {c.x,c.y,*z+lift};
 }
 return a;
}
// Creates player BANK's body beside ANCHOR: the character's own player action, run in that bank (it claims the bank's `player`).
// helper: Inzar's koru_efekt_bas first (her sword/energy effect entities, as character.cpp does for a locked Inzar).
ObjectId Engine::coop_spawn(Runtime& runtime,int bank,ObjectId anchor,bool helper){
 const int c=coop.characters[bank];
 if(c==0){const double w=runtime.bank_value(bank,"Aktif_Silah").real();if(w<1||w>7)runtime.set_bank_value(bank,"Aktif_Silah",Value::from_int(1));} // Silah_isimleri: 0-7
 if(runtime.bank_value(bank,healthVars[c]).real()<=0)runtime.set_bank_value(bank,healthVars[c],Value::from_double(healthMax[c]));
 if(c==2&&runtime.bank_value(bank,"koruyucu_mana").real()<=0)runtime.set_bank_value(bank,"koruyucu_mana",Value::from_int(230));
 runtime.set_bank_value(bank,"game_over_start",Value{});
 const Vec3 at=coop_spot(anchor);ObjectId id=0;
 coopSpawn_=bank;
 try{
  if(helper&&c==2)if(auto effect=runtime.function("koru_efekt_bas"))create_entity(runtime,"fish.mdl",at,effect);
  id=create_entity(runtime,models[c],at,runtime.function(actions[c]));
 }catch(...){coopSpawn_=-1;throw;}
 coopSpawn_=-1;set_field(id,"pan",field(anchor,"pan"));
 if(bank==1){coopP2_=id;coopHidden_=false;coopLast_=at;}
 return id;
}
// P2 weapon choice (no inventory or spell menu for P2): d-pad right/left cycles the shared inventory's firearms the P2 body can
// hold, the way EM_KuBiGoSMenu equips one (KuBiGoMenu.wdl:44-47: Aktif_Silah, Demir's gun model morphed to Silah_isimleri[Aktif_Silah]);
// Inzar's spells 1-6 as KoruyucuMenu.wdl KOM_Kernel sets buyu_no (every spell is selectable there).
void Engine::coop_weapon(Runtime& runtime){
 const int pad=coop_pad(1),c=coop.characters[1];if(pad<0||!coop_body(1))return;
 const bool next=input.controllerPressed[pad][SDL_CONTROLLER_BUTTON_DPAD_RIGHT],prev=input.controllerPressed[pad][SDL_CONTROLLER_BUTTON_DPAD_LEFT];
 if(next==prev)return;
 if(c==2){const int b=std::clamp(int(runtime.bank_value(1,"buyu_no").real()),1,6);runtime.set_bank_value(1,"buyu_no",Value::from_int((b-1+(next?1:5))%6+1));return;}
 if(runtime.bank_value(1,"silah_kaldir").real()!=0)return; // not while aiming
 auto list=global("Silahlar");auto* o=list.kind==Value::Kind::Object?state.get(list.bits):nullptr;if(!o)return;
 std::vector<int> owned;const auto count=std::min<std::size_t>(o->array.size(),std::size_t(std::max(0.0,number("SilahSayisi"))));
 for(std::size_t i=0;i<count;++i){const int w=int(o->array[i].value.real());if(w>=1&&(c==0?w<=7:(w<=5||w==8))&&std::find(owned.begin(),owned.end(),w)==owned.end())owned.push_back(w);} // character.cpp character_weapons
 if(owned.empty())return;
 const int cur=int(runtime.bank_value(1,"Aktif_Silah").real());const auto at=std::find(owned.begin(),owned.end(),cur);
 const int n=int(owned.size()),i=at==owned.end()?0:(int(at-owned.begin())+(next?1:n-1))%n,w=owned[std::size_t(i)];if(w==cur)return;
 runtime.set_bank_value(1,"Aktif_Silah",Value::from_int(w));
 if(c==0){auto gun=runtime.bank_value(1,"a_Silah");auto names=global("Silah_isimleri");auto* table=names.kind==Value::Kind::Object?state.get(names.bits):nullptr;
  if(gun.kind==Value::Kind::Object&&state.get(gun.bits)&&state.get(gun.bits)->alive&&table&&std::size_t(w)<table->array.size()){ProcessContext context;std::array<Argument,2> args{{{gun,{}},{table->array[std::size_t(w)].value,{}}}};builtin(runtime,context,"ent_morph",args);}}
}
// Engine::tick, after the input and UI, before the scripts run.
void Engine::coop_play(Runtime& runtime){
 coopRuntime_=&runtime;
 const bool campaign=runtime.function(actions[0])&&state.globals.contains("Key_Ileri"); // the original player scripts
 if(coopRestart_){coopRestart_=false;coop_banks(runtime,false);} // a new coop game or slot: fresh banks, P2 rejoins
 coop_banks(runtime,coop.active&&campaign);
 if(!coop.active||!campaign)return;
 auto down=[&](int b){return runtime.bank_value(b,"game_over_start").real()!=0;};
 if(coopOver_){ // both down: the original game over runs (K22.wdl game_over_strtr); coop ends with it, back in the main menu
  if(!down(0)&&number("LY_AktifLevelID")==0){coop={};coop_banks(runtime,false);}
  return;
 }
 const double dt=std::clamp(input.dt,0.0,0.25);
 // P2 input: bank 1's Key_* from P2's pad as Tuslari_Esitle (GenelAyarlar.wdl:103-119) sets bank 0's from P1's keys and
 // Joy_Takip's JOY_*: the modern mapping (controller.cpp apply_modern_controller: digital forward/back, analog turn, deadzone).
 {Input pad=input;const Vec3 force=apply_modern_controller(controller,pad,coop_pad(1),coopHeld_);
  auto jt=global("GA_JTuslar");auto* table=jt.kind==Value::Kind::Object?state.get(jt.bits):nullptr;
  auto button=[&](std::size_t j){const int n=table&&j<table->array.size()?int(table->array[j].value.real()):int(j)+1;return Value::from_int(n>=1&&n<=12&&pad.joystickButtons[0][std::size_t(n-1)]);};
  const std::array<std::pair<std::string_view,Value>,10> keys{{{"Key_Ileri",Value::from_double(std::max(0.f,force.y/2))},{"Key_Geri",Value::from_double(std::max(0.f,-force.y/2))},
   {"Key_Sag",Value::from_double(std::max(0.f,force.x/2))},{"Key_Sol",Value::from_double(std::max(0.f,-force.x/2))},{"Key_SilahKaldir",button(0)},{"Key_Ates",button(1)},
   {"Key_Kosma",button(2)},{"Key_Menu",Value{}},{"Key_Harita",Value{}},{"Key_Buyu",button(5)}}};
  for(auto& [name,value]:keys)runtime.set_bank_value(1,name,value);}
 coop_weapon(runtime);
 rumbleEntity.fill(0);if(const int pad=coop_pad(1);pad>=0)rumbleEntity[std::size_t(pad)]=coop_body(1);
 const ObjectId p1=coop_body(0),p2=coop_body(1);
 auto live=[&](ObjectId id){return id&&field(id,"skill1")>0;};
 if(!down(0)&&!down(1))coopLevelOk_=number("level_yuklendi_ok")!=0;
 // Death: the dead player's body stays down ~5 s, then that player respawns beside the living partner with full health.
 for(int b=0;b<2;++b){
  const ObjectId body=b?p2:p1,partner=b?p1:p2;
  if(!down(b)||!live(partner)||down(1-b)){coopDown_[b]=0;continue;}
  if(coopDown_[b]==0){notify(std::string("Player ")+char('1'+b)+" is down");if(coopLevelOk_)set_global("level_yuklendi_ok",Value::from_int(1));} // Demir's d_olum_loop clears it (oyuncu2.wdl:2961)
  if((coopDown_[b]+=dt)<5)continue;
  coopDown_[b]=0;const int c=coop.characters[b];
  if(c!=2)for(auto name:{"a_Silah","a_Telsiz"})if(auto v=runtime.bank_value(b,name);v.kind==Value::Kind::Object&&v.bits!=body)remove_entity(runtime,v.bits); // its gun and radio follow the dead body
  if(body)remove_entity(runtime,body);
  runtime.set_bank_value(b,healthVars[c],Value::from_double(healthMax[c]));
  coop_spawn(runtime,b,partner,false);notify(std::string("Player ")+char('1'+b)+" is back");
  return;
 }
 // P2 joins after every level entry once P1 is placed: the original LevelYukle has run its Giris placement (LV_*_Giris).
 const bool placed=live(p1)&&!down(0)&&number("LY_AktifLevelID")!=0&&number("player_morph_ok")!=0&&!pendingLevelLoad&&!positionRestore&&!runtime.running(runtime.function("LevelYukle"));
 if(!p2){coopSettle_=placed?coopSettle_+1:0;if(coopSettle_>=10){coopSettle_=0;coop_spawn(runtime,1,p1,true);}return;}
 coopSettle_=0;
 // Cutscenes hide P1 (player.invisible): P2 hides with it and lets the scene's actors through.
 if(p1){const bool hide=field(p1,"invisible")!=0;if(hide!=coopHidden_){coopHidden_=hide;set_field(p2,"invisible",hide);set_field(p2,"passable",hide);}}
 // Leash: the cameras follow P1 only; P2 more than 350 units away or fallen below the level for 2 s returns beside P1.
 if(live(p1)&&live(p2)&&!down(1)&&!coopHidden_){
  const Vec3 a=position(p1),b=position(p2);const bool lost=std::hypot(a.x-b.x,a.y-b.y)>350||b.z<a.z-250||(world&&b.z<world->mesh.min.z);
  coopLeash_=lost?coopLeash_+dt:0;if(coopLeash_>=2){coopLeash_=0;set_position(p2,coop_spot(p1));set_field(p2,"pan",field(p1,"pan"));}
 }else coopLeash_=0;
}
// P2's HUD, top-right: PlayerEnerji_Menu.wdl PEM_Ac (Demir, Ebru: energy ring and heart) or KoruViewMenu.wdl KVM_Ac (Inzar: health,
// mana, spell), mirrored from P1's top-left corner, from the same BMAPs and P2's own values. Shown while P1's HUD is.
void Engine::coop_hud(){
 if(!coop.active||!coopBanks_||!coopRuntime_||coopHidden_)return;
 const auto p2=coop_body(1);if(!p2||(number("PEM_Acik")==0&&number("KVM_Acik")==0))return;
 auto& runtime=*coopRuntime_;const int c=coop.characters[1];
 auto bmap=[&](Value v)->const Object*{auto* o=v.bits?state.get(v.bits):nullptr;return o&&o->alive&&o->kind==Object::Kind::Bmap?o:nullptr;};
 auto image_of=[&](const Object& o)->const Image&{auto cached=bitmapImages.find(o.id);return cached!=bitmapImages.end()?*cached->second:assets.image(o.text);}; // file BMAPs (engine.cpp svc_bitmap)
 auto element=[&](std::string_view list,int i)->Value{auto v=global(list);auto* o=v.kind==Value::Kind::Object?state.get(v.bits):nullptr;return o&&i>=0&&std::size_t(i)<o->array.size()?o->array[std::size_t(i)].value:Value{};};
 auto crop=[&](const Object& o,const Image& image)->Rect{auto get=[&](std::string_view n,float d){auto it=o.fields.find(n);return it==o.fields.end()?d:float(it->second.value.real());};
  Rect r{get("source_x",0),get("source_y",0),get("width",float(image.width)),get("height",float(image.height))};if(r.width<=0)r.width=float(image.width);if(r.height<=0)r.height=float(image.height);return r;};
 auto width=[&](Value v){auto* o=bmap(v);if(!o)return 0.f;return crop(*o,image_of(*o)).width;};
 // A6 004447e0 (engine.cpp update_ui): a 32-bit TGA panel bitmap blends at min(2·ALPHA, 255).
 auto draw=[&](Value v,float x,float y,float s,float alpha,bool overlay){auto* o=bmap(v);if(!o)return;const Image& image=image_of(*o);const Rect r=crop(*o,image);
  UICommand cmd;cmd.image=&image;cmd.rect={x,y,r.width*s,r.height*s};cmd.uv={r.x/image.width,r.y/image.height,r.width/image.width,r.height/image.height};cmd.color={1,1,1,image.a6AlphaFormat?std::min(1.f,2*alpha):alpha};cmd.overlay=overlay;cmd.filter=true;cmd.layer=-1;ui_.push_back(cmd);};
 // Scale: P1's HUD panels' (native display scaling included), else the art tier's basis.
 const bool hi=number("GA_Cozunurluk")>1;float s=0;
 for(auto name:{"PEMP_Enerji","KVMP_Enerji"}){auto panel=global(name);if(panel.kind==Value::Kind::Object&&state.get(panel.bits)&&field(panel.bits,"visible")!=0&&field(panel.bits,"scale_x")>0){s=float(field(panel.bits,"scale_x"));break;}}
 if(s<=0)s=float(currentHeight_)/(hi?768.f:480.f);
 const float right=float(currentWidth_);const double e=field(p2,"skill1");
 if(c!=2){
  const double health=std::clamp(e,0.0,100.0);double at=20*health/100;if(at-std::floor(at)>0.5)at+=1;
  if(health>0)coopHeart_+=float(6.25/(2+2.166*health/100)*input.dt*16);coopHeart_=std::fmod(coopHeart_,24.f);
  const auto ring=element(hi?"PEML1024_Enerji":"PEML_Enerji",int(at)),heart=element(hi?"PEML1024_Kalp":"PEML_Kalp",int(coopHeart_));
  const std::array<float,4> corner=hi?std::array<float,4>{10,10,43,32}:std::array<float,4>{6,6,27,20}; // PEME/PEMK(1024)_KoseKorXY
  const float x=right-(corner[0]+width(ring))*s;
  auto kalp=global("PEMP_Kalp");const float alpha=kalp.kind==Value::Kind::Object&&state.get(kalp.bits)&&field(kalp.bits,"transparent")!=0?float(field(kalp.bits,"alpha"))/100:1;
  draw(ring,x,corner[1]*s,s,1,false);draw(heart,x+(corner[2]-corner[0])*s,corner[3]*s,s,alpha,false);
 }else{
  const double mana=std::clamp(runtime.bank_value(1,"koruyucu_mana").real(),0.0,230.0);const int spell=int(runtime.bank_value(1,"buyu_no").real());
  const auto health=element(hi?"KVIL_Enerji":"KVIL640_Enerji",int(24*std::clamp(e,0.0,200.0)/200)),power=element(hi?"KVIL_Mana":"KVIL640_Mana",int(24*mana/230)),icon=element(hi?"KVIL_Buyu":"KVIL640_Buyu",spell-1);
  const float tier=float(number("GA_Cozunurluk"));const std::array<float,6> at=hi?std::array<float,6>{2,0,82,13,82,52}:std::array<float,6>{2,1,52-tier,9,52-tier,33}; // KVM_Ac positions
  const float x=right-(at[2]+std::max(width(health),width(power))+2)*s;
  draw(icon,x+at[0]*s,at[1]*s,s,1,true);draw(health,x+at[2]*s,at[3]*s,s,1,true);draw(power,x+at[4]*s,at[5]*s,s,1,true);
  if(mana<element("buyu_gereksinimi",spell).real())draw(global(hi?"KVI1024_delik":"KVI640_delik"),x+at[0]*s,at[1]*s,s,0.25f,true); // not enough mana for the spell
 }
}
}
