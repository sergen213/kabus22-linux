// Character lock: play the campaign as a chosen character (docs/GAMEPLAY_SYSTEMS.md §1, docs/CHARACTER_LOCK.md). Not in the original.
// The story keeps its own Aktif_Player (cinematics, level entries, item/door/puzzle gates and saves run exactly as written); only the
// reads that concern the playable body (player actions, footstep vertices, model height, weapon/HUD/menu, auto-aim, kill rewards)
// see the locked character. characterLock -1 (default) reroutes nothing: every read returns Aktif_Player, the original behaviour.
#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <array>
namespace k22 {
namespace {
constexpr std::array<std::string_view,3> playerActions{"Oyuncu_Demir","Oyuncu_Ebru","oyuncu_koruyucu"};
constexpr std::array<std::string_view,3> names{"DEMIR","EBRU","INZAR"};
// docs/CHARACTER_LOCK.md "Body reads": every Aktif_Player read in these functions (ISO and patch scripts share the names).
constexpr std::array<std::string_view,14> bodyFunctions{
 "Oyuncu_Demir","Oyuncu_Ebru","oyuncu_koruyucu","koru_efekt_bas","adimsesi_turu","game_over_strtr","Dusman_Tarama",
 "duma_Ol","Zipleyen_Ol","babayaro_Ol","Zipleyen_Yaralan","tz42_isin_strtr","laser_parti_ctrl","SLG_LevelKaydet"};
constexpr std::array<std::string_view,1> bodyArrays{"player_yuksekli"}; // LV_*_Giris: player.z-=player_yuksekli[Aktif_Player]
constexpr std::array<std::string_view,1> menuFunctions{"EM_Kernel"};      // M: item inventory or Inzar's spell menu
constexpr std::array<std::string_view,1> weaponFunctions{"EM_KuBiGoSMenu"}; // weapon chosen: Demir morphs his gun model
// Control gate: Demir's and Ebru's actions stop on oyuncu_pasif, Inzar's (and her spells) on koruyucu_pasif. The story scripts hold
// and release the story character's gate (Lain and Seytan_k release only koruyucu_pasif, the final boss only oyuncu_pasif), so a
// locked body reads the story's gate instead of its own. No body action writes its gate.
constexpr std::array<std::string_view,2> demirEbruFiles{"oyuncu2.wdl","oyuncu2_ebru.wdl"};
constexpr std::array<std::string_view,2> inzarFiles{"koruyucu.wdl","koru_buyuler.wdl"};
constexpr std::array<std::string_view,2> posingFunctions{"oy_Oynat","Hareket_Hizi"};
// Glyph cells of the original language-independent menu atlas (TusYonetim.wdl TYI_Tus*): 170x32 cells, one key label each.
struct Cell {char c;int x,y;};
constexpr std::array<Cell,28> cells{{{'Q',0,320},{'W',0,352},{'E',0,384},{'R',0,416},{'T',0,448},{'Y',0,480},{'U',0,512},{'I',0,544},{'O',0,576},{'P',0,608},
 {'A',0,704},{'S',0,736},{'D',0,768},{'F',0,800},{'G',0,832},{'H',0,864},{'J',0,896},{'K',0,928},{'L',0,960},
 {'Z',170,0},{'X',170,32},{'C',170,64},{'V',170,96},{'B',170,128},{'N',170,160},{'M',170,192},{'<',170,704},{'>',170,736}}};
}
int Engine::story_character()const{return std::clamp(int(number("Aktif_Player")),0,2);}
int Engine::body_character()const{return characterLock>=0?characterLock:story_character();}
// Inzar's body opens the item inventory outside her own story (items are needed there) and her spell menu inside it; a gun body
// never gets the spell menu. Unlocked: Aktif_Player.
// A weapon Demir cannot hold (Ebru's magnum) is not morphed onto his gun; character_weapons swaps it for the pistol.
int Engine::weapon_character()const{const int b=body_character(),w=int(number("Aktif_Silah"));return b==0&&(w<1||w>7)?-1:b;}
int Engine::menu_character()const{const int s=story_character();return characterLock>=0&&characterLock!=2&&s==2?characterLock:s;}
void Engine::configure_characters(Runtime& runtime,int defaultLock){
 if(state.profile==SemanticProfile::BonusA7)return;
 characterDefault=std::clamp(defaultLock,-1,2);newGameChoice_=characterDefault+1;
 runtime.reroute_reads("Aktif_Player","k22_body_player",bodyFunctions,bodyArrays);
 runtime.reroute_reads("Aktif_Player","k22_menu_player",menuFunctions);
 runtime.reroute_reads("Aktif_Player","k22_weapon_player",weaponFunctions);
 runtime.reroute_reads("oyuncu_pasif","k22_body_oyuncu_pasif",{},{},demirEbruFiles);
 runtime.reroute_reads("koruyucu_pasif","k22_body_koruyucu_pasif",{},{},inzarFiles);
 // Lain's hit reaction (kotu_koruyucu.wdl Kotukoru_Yaralan:5291) branches on the attack kind in skill3: Inzar's sword and spell
 // kinds and firearm 2. Ates_Et (silah.wdl:680) records the firearm number there, so any other gun fell through every branch and
 // ended Lain's AI with its Enerji below 0 (no Kotukoru_Ol, no progression). A locked game records Lain's bullet hits as kind 2.
 runtime.reroute_assigned_read("Ates_Et","skill3","aktif_silah","k22_bullet_hit_kind");
 // Demir's story scenes pose the player through oy_Oynat and Hareket_Hizi (oyuncu2.wdl:540/396: t_Silah.String[Aktif_Silah], 8
 // strings; Action_Zaman[Aktif_Silah*20+n], 160), called from cinematic.wdl, BUL_Saat.wdl and kotu_koruyucu.wdl buyukes_koruses.
 // An Ebru body there may still hold her magnum (8): those reads see the pistol (1).
 runtime.reroute_reads("Aktif_Silah","k22_pose_weapon",posingFunctions);
}
void Engine::set_character_lock(int character){characterLock=std::clamp(character,-1,2);storyAtLevel_=story_character();adoptStory_=false;switchPrompt_=-1;}
// load_level, before the placed actions start. The entity that would become the player for the story character runs the locked
// character's action instead; a placed action of the locked character that the story does not use gets the story character's
// action, whose guard (a body read) then removes it, as the original does for every unused player action.
void Engine::apply_character_lock(Runtime& runtime,std::vector<std::pair<ObjectId,FunctionId>>& actions){
 if(adoptStory_||storyAtLevel_<0){storyAtLevel_=story_character();adoptStory_=false;}
 if(characterLock<0)return;
 std::array<FunctionId,3> fn{};for(int i=0;i<3;++i)fn[i]=runtime.function(playerActions[i]);
 const auto effect=runtime.function("koru_efekt_bas");const int s=story_character(),b=characterLock;
 const bool effectPlaced=std::any_of(actions.begin(),actions.end(),[&](auto& a){return a.second==effect;});
 std::optional<ObjectId> body;
 for(auto& [id,action]:actions){
  const int a=int(std::find(fn.begin(),fn.end(),action)-fn.begin());if(a>2||(a==b&&a==s))continue;
  if(a==s){action=fn[b];body=id;}else if(a==b)action=fn[s];else continue;
  state.require(id).field("action1").value=Value::function(action);
 }
 // oyuncu_koruyucu waits for koru_efektbas_ok, which only the WMB-placed koru_efekt_bas sets (koruyucu.wdl:539-567).
 // It goes first in the queue: until oyuncu_koruyucu sets player, level scripts that poll player.x would read NULL.
 if(body&&b==2&&!effectPlaced&&effect){auto id=create_entity(runtime,"fish.mdl",position(*body),0);state.require(id).field("action1").value=Value::function(effect);actions.insert(actions.begin(),{id,effect});}
 // Ebru's action selects her magnum (Aktif_Silah 8) at every spawn; outside her story she keeps the firearm the story carried.
 if(body&&b==1){const int w=int(number("Aktif_Silah"));ebruWeapon_=w>=1&&w<=5?w:0;}
 if(body&&b==0)character_weapons(runtime,true); // before his action's Silah_Ekle reads Aktif_Silah
 // A bullet hit (silah.wdl Ates_Et:651-658) reads the blood table kan_mertip[3*(you.skill2-1)+n] by enemy type, skill2 1-6. Every
 // enemy action sets its type except Lain's and Seytan's, which only Inzar fights in the story: a gun body would index -2. All six
 // kan_mertip rows are equal, so they get type 1 (skill2 is otherwise only compared with 4: hedef.wdl:78, oyuncu2.wdl:628).
 if(b!=2){const auto lain=runtime.function("kotukoru_ai"),devil=runtime.function("devil_ai");
  for(auto& [id,action]:actions)if(action&&(action==lain||action==devil))if(auto& t=state.require(id).field("skill2").value;t.raw()==0)t=Value::from_int(1);}
}
// Weapons a locked body can hold (every frame while locked). Demir's tables (Silah_isimleri, t_Silah) stop at weapon 7: in Ebru's
// inventory he fires her magnum rounds from his pistol. Ebru's model has one pose set; she fires the story's firearms 1-5 or her
// magnum through it (silah.wdl Ates_Et is generic) but cannot throw grenades or lay mines (6, 7: Demir's action only). Inzar fights
// with her sword and spells and never reads Aktif_Silah.
void Engine::character_weapons(Runtime& runtime,bool spawning){
 if(characterLock<0||characterLock==2)return;
 // Only once the locked body plays: right after a console change the old body runs on until the next level entry.
 const auto p=live_player();if(!spawning&&(!p||callback(p,"action1")!=runtime.function(playerActions[characterLock])))return;
 const int w=int(number("Aktif_Silah"));
 auto slot=[&](std::string_view name,std::size_t i)->Slot*{auto v=global(name);auto* o=v.kind==Value::Kind::Object?state.get(v.bits):nullptr;return o&&i<o->array.size()?&o->array[i]:nullptr;};
 auto rounds=[&](int weapon){auto* m=slot("S_Kursun_Sayisi",std::size_t(weapon));auto* r=slot("Kursun_Sayisi",std::size_t(weapon));return (m?m->value.raw():0)+(r?r->value.raw():0);};
 if(characterLock==0&&story_character()!=0&&(w<1||w>7)){
  auto *m8=slot("S_Kursun_Sayisi",8),*r8=slot("Kursun_Sayisi",8),*m1=slot("S_Kursun_Sayisi",1),*r1=slot("Kursun_Sayisi",1);
  if(w==8&&m8&&r8&&m1&&r1&&rounds(1)==0){m1->value=m8->value;r1->value=r8->value;m8->value=Value{};r8->value=Value{};}
  set_global("Aktif_Silah",Value::from_int(1));
  auto gun=global("a_silah");auto names=slot("Silah_isimleri",1);
  if(gun.kind==Value::Kind::Object&&state.get(gun.bits)&&state.get(gun.bits)->alive&&names){ProcessContext context;std::array<Argument,2> args{{{gun,{}},{names->value,{}}}};builtin(runtime,context,"ent_morph",args);}
 }
 if(characterLock==1&&story_character()!=1){
  if(w>=1&&w<=5)ebruWeapon_=w;
  else if((w==8&&ebruWeapon_&&rounds(8)==0)||w==0||w==6||w==7)set_global("Aktif_Silah",Value::from_int(ebruWeapon_?ebruWeapon_:8));
 }
}
// Top of Engine::tick. A story switch (a script writes another Aktif_Player during play) asks whether to keep the locked body or
// to follow the story; the scripts are paused until the answer. Loads and New Game adopt the story character silently.
bool Engine::character_tick(Runtime& runtime,const Input& in){
 if(auto start=runtime.function("YeniOyunBaslat");runtime.running(start)){
  if(newGameMenu_){set_character_lock(newGameChoice_-1);newGameMenu_=false;}
  adoptStory_=true;
 }
 const auto menuPanel=global("YMP_Normal");newGameMenu_=menuPanel.kind==Value::Kind::Object&&state.get(menuPanel.bits)&&field(menuPanel.bits,"visible")!=0;
 if(newGameMenu_){
  const bool left=in.pressed[SDL_SCANCODE_LEFT]||in.pressed[SDL_SCANCODE_KP_4]||in.controllerPressed[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
  const bool right=in.pressed[SDL_SCANCODE_RIGHT]||in.pressed[SDL_SCANCODE_KP_6]||in.controllerPressed[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];
  int clicked=0;if(in.mousePressed[SDL_BUTTON_LEFT])for(int i=0;i<2;++i)if(menuArrows_[i].width>0&&in.mouseX>=menuArrows_[i].x&&in.mouseX<menuArrows_[i].x+menuArrows_[i].width&&in.mouseY>=menuArrows_[i].y&&in.mouseY<menuArrows_[i].y+menuArrows_[i].height)clicked=i?1:-1;
  const int step=(right?1:0)-(left?1:0)+clicked;if(step)newGameChoice_=(newGameChoice_+4+step)%4;
 }
 if(switchPrompt_<0){
  character_weapons(runtime,false);
  if(characterLock<0||adoptStory_||storyAtLevel_<0)return false;
  const int s=story_character();if(s==storyAtLevel_)return false;
  storyAtLevel_=s;if(s==characterLock)return false;
  switchPrompt_=s;promptCursor_=0;promptRelease_=false;promptArmed_=false;notify(std::string("Story switches to ")+std::string(names[s]));
 }
 // Keys held when the question opens (walking) are ignored until released; the answering key is held back from the game too.
 const bool held=in.keys.any()||in.mouseButtons.any()||in.controllerButtons.any()||in.joystickButtons[0].any()||in.joystickButtons[1].any();
 if(promptRelease_){if(held)return true;switchPrompt_=-1;promptRelease_=false;return true;}
 if(!promptArmed_){promptArmed_=!held;return true;}
 if(in.pressed[SDL_SCANCODE_UP]||in.pressed[SDL_SCANCODE_DOWN]||in.pressed[SDL_SCANCODE_KP_8]||in.pressed[SDL_SCANCODE_KP_2]||in.controllerPressed[SDL_CONTROLLER_BUTTON_DPAD_UP]||in.controllerPressed[SDL_CONTROLLER_BUTTON_DPAD_DOWN])promptCursor_^=1;
 int chosen=-1;
 for(int i=0;i<2;++i){const auto& r=promptRows_[i];if(r.width>0&&in.mouseX>=r.x&&in.mouseX<r.x+r.width&&in.mouseY>=r.y&&in.mouseY<r.y+r.height){if(in.mouseDX||in.mouseDY)promptCursor_=i;if(in.mousePressed[SDL_BUTTON_LEFT])chosen=i;}}
 if(in.pressed[SDL_SCANCODE_RETURN]||in.pressed[SDL_SCANCODE_KP_ENTER]||in.controllerPressed[SDL_CONTROLLER_BUTTON_A]||in.controllerPressed[SDL_CONTROLLER_BUTTON_START]||in.joystickPressed[0][0]||in.joystickPressed[1][0])chosen=promptCursor_;
 if(chosen>=0){if(chosen==1)characterLock=switchPrompt_;notify(std::string("Playing as ")+std::string(names[characterLock]));promptRelease_=true;}
 return true;
}
// Glyphs cut from the original menu atlas (menu_dildenbagimsiz.tga, same font and colour as the menu buttons).
float Engine::draw_menu_word(std::string_view word,float x,float y,float scale,float alpha,bool measure){
 if(!menuAtlas_){if(!assets.contains("menu_dildenbagimsiz.tga"))return 0;menuAtlas_=assets.image_ptr("menu_dildenbagimsiz.tga");
  for(const auto& c:cells){int x0=c.x+170,x1=c.x,y0=c.y+32,y1=c.y;for(int py=c.y;py<c.y+32;++py)for(int px=c.x;px<c.x+170;++px)if(menuAtlas_->rgba[(std::size_t(py)*menuAtlas_->width+px)*4+3]>24){x0=std::min(x0,px);x1=std::max(x1,px+1);y0=std::min(y0,py);y1=std::max(y1,py+1);}
   menuGlyphs_[std::uint8_t(c.c)]={float(x0),float(c.y),float(std::max(0,x1-x0)),32};}}
 float pen=x;
 for(char ch:word){
  if(ch==' '){pen+=9*scale;continue;}
  const auto& g=menuGlyphs_[std::uint8_t(ch)];if(g.width<=0)continue;
  if(!measure){UICommand c;c.image=menuAtlas_.get();c.rect={pen,y,g.width*scale,g.height*scale};c.uv={g.x/menuAtlas_->width,g.y/menuAtlas_->height,g.width/menuAtlas_->width,g.height/menuAtlas_->height};c.color={1,1,1,alpha};c.layer=1e6f;c.filter=true;ui_.push_back(c);}
  pen+=(g.width+1)*scale;
 }
 return pen-x;
}
void Engine::character_overlay(){
 menuArrows_={};promptRows_={};
 if(!renderer||currentWidth_<=0)return;
 if(newGameMenu_&&switchPrompt_<0){
  // One button height above NORMAL (YeniMenu.wdl YeniMenuAc), at the menu's own scale: "< ORIGINAL >".
  const auto panel=global("YMP_Normal").bits;const float sx=std::max(0.1f,float(field(panel,"scale_x"))),sy=std::max(0.1f,float(field(panel,"scale_y")));
  static constexpr std::array<std::string_view,4> choices{"ORIGINAL","DEMIR","EBRU","INZAR"};
  const auto word=choices[newGameChoice_];const float cx=float(field(panel,"pos_x"))+85*sx,y=float(field(panel,"pos_y"))-40*sy;
  const float arrow=draw_menu_word("<",0,0,sy,1,true),gap=14*sy,width=draw_menu_word(word,0,0,sy,1,true);
  const float left=cx-width/2-gap-arrow,right=cx+width/2+gap;
  draw_menu_word("<",left,y,sy,1,false);draw_menu_word(word,cx-width/2,y,sy,1,false);draw_menu_word(">",right,y,sy,1,false);
  menuArrows_[0]={left-gap,y,arrow+gap+width/2,32*sy};menuArrows_[1]={cx,y,width/2+gap+arrow+gap,32*sy};
 }
 if(switchPrompt_>=0){
  const float w=float(currentWidth_),h=float(currentHeight_),scale=h/480.f;
  UICommand back;back.kind=UICommand::Kind::Solid;back.rect={0,0,w,h};back.color={0,0,0,0.6f};back.layer=1e6f;ui_.push_back(back);
  const auto& font=renderer->font_atlas("serif",std::max(12,int(14*scale)),true);
  const std::string_view lang=language_;
  const std::string title=lang=="tr"?"Hikaye "+std::string(names[switchPrompt_])+" ile devam ediyor. Karakter:":lang=="de"?"Die Geschichte wechselt zu "+std::string(names[switchPrompt_])+". Weiter als:":"The story switches to "+std::string(names[switchPrompt_])+". Continue as:";
  const float tw=Renderer::text_extent(font,title).x,ty=h*0.40f;
  promptTitle_=title;
  for(int pass=0;pass<2;++pass){UICommand t;t.kind=UICommand::Kind::Text;t.font=&font;t.text=promptTitle_;t.rect={std::floor((w-tw)/2)+(pass?0:1),ty+(pass?0:1),tw,font.lineHeight};t.color=pass?Color{0.78f,0.16f,0.12f,1}:Color{0,0,0,1};t.layer=1e6f;t.filter=true;ui_.push_back(t);}
  const std::array<int,2> rows{characterLock,switchPrompt_};
  for(int i=0;i<2;++i){const auto word=names[rows[i]];const float width=draw_menu_word(word,0,0,scale,1,true),y=ty+font.lineHeight*1.6f+i*36*scale;
   draw_menu_word(word,(w-width)/2,y,scale,i==promptCursor_?1.f:0.4f,false);if(i==promptCursor_){const float a=draw_menu_word(">",0,0,scale,1,true);draw_menu_word(">",(w-width)/2-a-12*scale,y,scale,1,false);}
   promptRows_[i]={(w-width)/2-40*scale,y,width+80*scale,32*scale};}
 }
 frame_.ui=ui_; // make_frame published ui_ before these commands were added (console_overlay does the same)
}
}
