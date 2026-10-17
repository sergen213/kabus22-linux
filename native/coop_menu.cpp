// 2 PLAYER main-menu item and the two-player character select (docs/COOP.md). Not in the original.
// The item sits above NEW GAME in the original stack (AnaMenu.wdl AnaMenuAc, five panels on AM_Cursor 0-4): Up from NEW GAME or
// Down from EXIT reaches it, so every original Down sequence from NEW GAME is unchanged. Presses the native menu consumes are hidden
// from the scripts (DM_TusKontrol, KEY_*) until released. The select screen draws over the menu background (movie, DMP_Arka) with
// the characters' own models as VIEW entities (silah.wdl SilahPanel precedent); both READY opens the original New Game difficulty
// menu (YeniMenuAc) with the choice pending, and YeniOyunBaslat applies it (character.cpp character_tick).
#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace k22 {
namespace {
constexpr std::array<std::string_view,3> models{"demir_2007.mdl","ebru_2006.mdl","koruyucu_2006.mdl"};
constexpr std::array<std::string_view,3> idles{"byBekle","bekle","bekle"}; // oyuncu2.wdl Oynat(Bekle): Demir's grenade stance (arms down; the grenade is a separate entity), Ebru and Inzar "bekle"
constexpr std::array<std::string_view,3> names{"DEMIR","EBRU","INZAR"};
constexpr std::array<std::string_view,5> panels{"AMP_YeniOyun","AMP_OyunYukle","AMP_Ayarlar","AMP_Kabus22","AMP_Cikis"}; // AML_Paneller
// Pad reads of the select screen. ponytail: pad 1 always reads false until the two-pad Input lands (docs/COOP.md "Controllers").
bool pad_pressed(const Input& in,int pad,SDL_GameControllerButton b){return pad==0&&in.controllerPressed[b];}
bool inside(const Rect& r,const Input& in){return r.width>0&&in.mouseX>=r.x&&in.mouseX<r.x+r.width&&in.mouseY>=r.y&&in.mouseY<r.y+r.height;}
}
void Engine::coop_tick(Runtime& runtime,const Input& in){
 if(state.profile!=SemanticProfile::CampaignA6)return;
 // Consumed presses stay hidden from the scripts until released.
 coopKeys_&=in.keys|in.pressed;coopPad_&=in.controllerButtons|in.controllerPressed;
 auto eat=[&]{coopKeys_|=in.keys|in.pressed;coopPad_|=in.controllerButtons|in.controllerPressed;};
 auto sound=[&](std::string_view name){const auto s=global(name);if(!s.bits)return;ProcessContext context;std::array<Argument,3> args{{{s,{}},{Value::from_double(number("GA_Efekt")),{}},{Value{},{}}}};builtin(runtime,context,"snd_play",args);}; // Kabusdismenu.wdl DM_TusKontrol
 auto key=[&](std::initializer_list<SDL_Scancode> keys){return std::any_of(keys.begin(),keys.end(),[&](auto k){return bool(in.pressed[k]);});};
 auto bitmap=[&](int item,bool selected){auto list=global("DMIL_AnaMenu");auto* o=list.kind==Value::Kind::Object?state.get(list.bits):nullptr;const auto i=std::size_t(int(number("GA_Dil"))*10+item*2+(selected?0:1));
  if(o&&i<o->array.size())state.require(global(panels[item]).bits).field("bmap").value=o->array[i].value;}; // AM_Kernel's own BMAP choice
 const auto first=global("AMP_YeniOyun");const auto screen=coopScreen_;
 if(screen==CoopScreen::Off&&first.kind==Value::Kind::Object&&state.get(first.bits)&&field(first.bits,"visible")!=0&&runtime.running(runtime.function("AM_Kernel"))){
  coopPending_=false; // backed out of the New Game menu
  const int cursor=std::clamp(int(number("AM_Cursor")),0,4);
  const bool up=key({SDL_SCANCODE_UP,SDL_SCANCODE_KP_8})||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_DPAD_UP),down=key({SDL_SCANCODE_DOWN,SDL_SCANCODE_KP_2})||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  const bool hover=inside(coopItemRect_,in),click=hover&&in.mousePressed[SDL_BUTTON_LEFT];
  if(!coopItem_&&((up&&cursor==0)||(down&&cursor==4)||(hover&&(in.mouseDX||in.mouseDY))||click)){coopItem_=true;if(up||down)eat();sound("DMS_Yontus");}
  else if(coopItem_&&(up||down)){coopItem_=false;const int to=up?4:0;set_global("AM_Cursor",Value::from_int(to));bitmap(to,true);eat();sound("DMS_Yontus");}
  else if(coopItem_&&key({SDL_SCANCODE_ESCAPE}))coopItem_=false; // AM_Kernel moves its cursor to EXIT
  if(coopItem_)bitmap(cursor,false);
  // DisMenuCerceve.wdl frame: AnaMenuAc's CER_Hedef spans the five buttons (Cer_hY2-Cer_hY1 = (5*DM_ButtonH+2*Cer_Kenar)*scale); one more on top.
  set_global("Cer_hY1",Value::from_double(number("Cer_hY2")-(6*32+2*number("Cer_Kenar"))*field(first.bits,"scale_y")));
  if(coopItem_&&(click||key({SDL_SCANCODE_RETURN,SDL_SCANCODE_KP_ENTER})||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_A)||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_START))){
   eat();sound("DMS_Enter");runtime.start("AnaMenuKapat");remove_entity(runtime,global("AM_KernelTimer").bits); // AM_Kernel's own exit: AnaMenuKapat(); Remove(me)
   coopScreen_=CoopScreen::Select;coopItem_=false;coopJoined_={true,false};coopReady_={};coopTime_=0;
   // The menu frame moves to the select screen as it does to every original submenu (CER_Hedef).
   const float w=float(currentWidth_),h=float(currentHeight_);std::array<Argument,4> frame{{{Value::from_double(0.08*w),{}},{Value::from_double(0.04*h),{}},{Value::from_double(0.92*w),{}},{Value::from_double(0.97*h),{}}}};runtime.start("CER_Hedef",frame);
  }
 }else if(screen==CoopScreen::Select){
  // P1: keyboard, mouse and pad 0. P2: pad 1, or on the keyboard A/D, Space (join, READY) and Backspace (back).
  const bool click=in.mousePressed[SDL_BUTTON_LEFT];
  for(int p=0;p<2;++p){
   const bool left=p?pad_pressed(in,1,SDL_CONTROLLER_BUTTON_DPAD_LEFT)||key({SDL_SCANCODE_A}):key({SDL_SCANCODE_LEFT,SDL_SCANCODE_KP_4})||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_DPAD_LEFT)||(click&&inside(coopRects_[0],in));
   const bool right=p?pad_pressed(in,1,SDL_CONTROLLER_BUTTON_DPAD_RIGHT)||key({SDL_SCANCODE_D}):key({SDL_SCANCODE_RIGHT,SDL_SCANCODE_KP_6})||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_DPAD_RIGHT)||(click&&inside(coopRects_[1],in));
   const bool readyClick=!p&&click&&inside(coopRects_[2],in);
   const bool confirm=p?pad_pressed(in,1,SDL_CONTROLLER_BUTTON_A)||pad_pressed(in,1,SDL_CONTROLLER_BUTTON_START)||key({SDL_SCANCODE_SPACE}):key({SDL_SCANCODE_RETURN,SDL_SCANCODE_KP_ENTER})||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_A)||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_START)||(readyClick&&!coopReady_[0]);
   const bool back=p?pad_pressed(in,1,SDL_CONTROLLER_BUTTON_B)||key({SDL_SCANCODE_BACKSPACE}):key({SDL_SCANCODE_ESCAPE})||pad_pressed(in,0,SDL_CONTROLLER_BUTTON_B)||(readyClick&&coopReady_[0]);
   if(!coopJoined_[p]){if(confirm){coopJoined_[p]=true;sound("DMS_Enter");}continue;}
   if(coopReady_[p]){if(back){coopReady_[p]=false;sound("DMS_Esc");}continue;}
   if(left!=right){coopPick_[p]=(coopPick_[p]+(right?1:2))%3;sound("DMS_Yontus");} // any combination, duplicates too
   if(confirm){coopReady_[p]=true;sound("DMS_Enter");}
   else if(back){sound("DMS_Esc");if(p)coopJoined_[1]=false;else coopScreen_=CoopScreen::Leaving;}
  }
  if(coopReady_[0]&&coopReady_[1])coopScreen_=CoopScreen::Starting;
  coopTime_+=float(std::clamp(in.dt,0.0,0.1));eat();
 }else if(screen!=CoopScreen::Off){
  // Leaving/Starting: the original menu opens once everything is released, so the press never reaches its kernel.
  eat();
  if(!in.keys.any()&&!in.controllerButtons.any()&&!in.mouseButtons.any()){
   coopScreen_=CoopScreen::Off;set_global("DM_Tus",Value{});
   if(screen==CoopScreen::Starting){coopPending_=true;runtime.start("YeniMenuAc");}
   else{runtime.start("AnaMenuAc");coopItem_=true;bitmap(0,false);}
  }
 }
 coop_models(runtime);
 input.keys&=~coopKeys_;input.pressed&=~coopKeys_;input.controllerButtons&=~coopPad_;input.controllerPressed&=~coopPad_;
}
// The select screen's VIEW entities: each joined player's character, idle cycle, turning slowly. Removed when the screen closes.
void Engine::coop_models(Runtime& runtime){
 for(int p=0;p<2;++p){
  auto& id=coopModels_[p];const bool show=coopScreen_!=CoopScreen::Off&&coopJoined_[p];
  if(id&&(!show||!state.get(id)||!state.get(id)->alive||state.require(id).text!=models[coopPick_[p]])){remove_entity(runtime,id);id=0;}
  if(!show)continue;
  if(!id){id=create_entity(runtime,models[coopPick_[p]],{},0);auto& o=state.require(id);o.field("view").value=Value::object(0);o.field("layer").value=Value::from_int(2);set_field(id,"visible",1);}
  const Model& m=assets.model(models[coopPick_[p]]);
  // In front of the camera: column centre at 28% / 72% of the width, feet at 76% of the height, the tallest model filling about 55%.
  const float w=float(std::max(1,currentWidth_)),h=float(std::max(1,currentHeight_)),arc=float(field(global("camera").bits,"arc")),tx=std::tan((arc>0?arc:60)*float(M_PI)/360),ty=tx*h/w;
  float tallest=1;for(auto name:models){const auto& t=assets.model(name);tallest=std::max(tallest,t.resourceMax.z-t.resourceMin.z);}
  const float x=tallest/(0.55f*2*ty),cx=p?0.72f:0.28f,feet=0.76f;
  set_position(id,{x,(1-2*cx)*x*tx,(1-2*feet)*x*ty-m.resourceMin.z});
  set_field(id,"pan",std::fmod(180+24*coopTime_+(p?40:-40),360.f));
  std::size_t begin=m.frames.size(),count=0;const auto scene=idles[coopPick_[p]];
  for(std::size_t i=0;i<m.frames.size();++i){std::string_view f=m.frames[i].name;if(f.size()>=scene.size()&&case_equal(f.substr(0,scene.size()),scene)&&(f.size()==scene.size()||std::isdigit(static_cast<unsigned char>(f[scene.size()])))){if(!count)begin=i;++count;}else if(count)break;}
  if(!count)continue;
  const float at=std::fmod(coopTime_/2,1.f)*float(count);const auto frame=std::size_t(at); // one idle cycle every 2 s
  set_field(id,"frame",double(begin+frame)+1+(at-std::floor(at)));set_field(id,"next_frame",frame+1<count?0:double(begin)+1);
 }
}
void Engine::coop_overlay(){
 coopItemRect_={};coopRects_={};
 const auto first=global("AMP_YeniOyun");
 if(coopScreen_==CoopScreen::Off){
  if(first.kind!=Value::Kind::Object||!state.get(first.bits)||!field(first.bits,"visible"))return;
  // One button height above NEW GAME, centred on the 170-wide button like its own art.
  const float sx=std::max(0.1f,float(field(first.bits,"scale_x"))),sy=std::max(0.1f,float(field(first.bits,"scale_y")));
  // The atlas capitals are smaller than the buttons' lettering: 1.15x, centred in the row.
  const float x=float(field(first.bits,"pos_x")),y=float(field(first.bits,"pos_y"))-32*sy,scale=1.15f*sy,width=draw_menu_word("2 PLAYER",0,0,scale,1,true);
  draw_menu_word("2 PLAYER",x+85*sx-width/2,y-16*(scale-sy),scale,1,false,coopItem_);coopItemRect_={x,y,170*sx,32*sy};
  return;
 }
 const float w=float(currentWidth_),h=float(currentHeight_),s=h/720;
 UICommand back;back.kind=UICommand::Kind::Solid;back.rect={0,0,w,h};back.color={0,0,0,0.55f};back.layer=1.5f;ui_.push_back(back); // under the models (layer 2)
 auto centred=[&](std::string_view word,float cx,float y,float scale,float alpha,bool lit){const float width=draw_menu_word(word,0,0,scale,1,true);draw_menu_word(word,cx-width/2,y,scale,alpha,false,lit);return width;};
 centred("2 PLAYER",w/2,0.05f*h,1.25f*s,1,true);
 for(int p=0;p<2;++p){
  const float cx=w*(p?0.72f:0.28f);
  centred(p?"PLAYER 2":"PLAYER 1",cx,0.14f*h,s,1,true);
  if(!coopJoined_[p]){centred("PRESS START",cx,0.47f*h,s,0.5f+0.5f*float(std::fmod(coopTime_,1.0f)<0.6f),false);continue;}
  const auto name=names[coopPick_[p]];const float y=0.80f*h,width=centred(name,cx,y,s,1,coopReady_[p]);
  if(!coopReady_[p]){const float arrow=draw_menu_word("<",0,0,s,1,true),gap=14*s;draw_menu_word("<",cx-width/2-gap-arrow,y,s,1,false);draw_menu_word(">",cx+width/2+gap,y,s,1,false);
   if(!p){coopRects_[0]={cx-width/2-2*gap-arrow,y,arrow+gap+width/2,32*s};coopRects_[1]={cx,y,width/2+2*gap+arrow,32*s};}}
  const float ready=centred("READY",cx,0.87f*h,s,coopReady_[p]?1:0.45f,coopReady_[p]);
  if(!p)coopRects_[2]={cx-ready/2-14*s,0.87f*h,ready+28*s,32*s};
 }
}
}
