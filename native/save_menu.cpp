// Save-slot picker of the pause menu's native SAVE item (docs/CONSOLE.md "Save from the in-game menu"). Not part of the original:
// it lists the original save index (kysd.sav through SLG_Yukle, the labels YukleMenu shows) and writes through Engine::save_to_slot,
// i.e. the original SLG_LevelKaydet -> YD_OyunuKaydet -> SLG_Kaydet path.
#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <cstdio>
namespace k22 {
namespace {
constexpr int maxSlots=99; // YukleMenu.wdl SLG_MaxKayit
std::string two(int n){char t[8];std::snprintf(t,sizeof t,"%02d",n);return t;} // SLG_LevelKaydet's label number: slot+1, two digits
}
void Engine::save_menu_open(Runtime& runtime){
 if(!runtime.function("SLG_Yukle")||!runtime.function("YD_OyunuKaydet")){notify("Cannot save now: these scripts have no campaign save system");return;}
 runtime.start("SLG_Yukle"); // YukleMenu.wdl:19: kysd.sav -> Aktif_SaveNo, SLG_KayitSayisi, YUMT_Buffer.String[0..count-1]; no file: unchanged
 saveNext_=std::clamp(int(number("Aktif_SaveNo")),0,maxSlots-1);saveCount_=std::clamp(int(number("SLG_KayitSayisi")),0,maxSlots);
 saveRows_.clear();
 const auto buffer=global("YUMT_Buffer");const auto* o=buffer.kind==Value::Kind::Object?state.get(buffer.bits):nullptr;
 for(int i=0;i<saveCount_;++i){
  std::string label;if(o&&std::size_t(i)<o->array.size())try{label=std::string(string(o->array[i].value));}catch(const std::exception&){}
  while(!label.empty()&&(label.back()=='\n'||label.back()=='\r'))label.pop_back();
  saveRows_.push_back(label.empty()?two(i+1)+")":std::move(label));
 }
 saveMenu_=true;saveCursor_=0;saveTop_=0;saveConfirm_=-1;
}
// Row 0 saves into the ring's next slot exactly as F5 does; another row (or row 0 with a full ring, whose next slot holds the oldest
// save) asks for confirmation first. Escape/B/right click backs out: confirmation -> list -> pause menu. Every input is eaten.
void Engine::save_menu_tick(Runtime& runtime,const Input& in){
 auto key=[&](std::initializer_list<SDL_Scancode> keys){return std::any_of(keys.begin(),keys.end(),[&](auto k){return bool(in.pressed[k]);});};
 auto inside=[&](const Rect& r){return r.width>0&&in.mouseX>=r.x&&in.mouseX<r.x+r.width&&in.mouseY>=r.y&&in.mouseY<r.y+r.height;};
 const bool yes=key({SDL_SCANCODE_RETURN,SDL_SCANCODE_KP_ENTER})||in.pad_pressed(SDL_CONTROLLER_BUTTON_A);
 const bool no=key({SDL_SCANCODE_ESCAPE})||in.pad_pressed(SDL_CONTROLLER_BUTTON_B)||in.mousePressed[SDL_BUTTON_RIGHT];
 const bool click=in.mousePressed[SDL_BUTTON_LEFT];
 auto save=[&](int row){
  const int slot=row==0?saveNext_:row-1;
  saveMenu_=false;saveConfirm_=-1;pauseItem_=PauseItem::Save;
  runtime.start("SmartAcKapat"); // the slot records the game, not the menu (save_to_slot refuses freeze_mode)
  std::string message;
  try{
   // 18 cp1254 characters like the SDN_* save-point names; Turkish only with the Turkish script set (as quicksave).
   auto error=save_to_slot(runtime,row==0?-1:slot,state.compilerMacros.contains("defdil_tr")?"Kay\xFDtl\xFD oyun       ":"Saved game        ");
   message=error.empty()?"Game saved (slot "+two(slot+1)+")":error;
  }catch(const std::exception& e){message=std::string("Error: ")+e.what();}
  runtime.start("SmartAcKapat");notify(std::move(message));
 };
 if(saveConfirm_>=0){
  if(yes||(click&&inside(saveConfirmRect_)))save(saveConfirm_);
  else if(no||click)saveConfirm_=-1;
  return;
 }
 if(no){saveMenu_=false;return;}
 const int rows=1+saveCount_,page=std::max(1,saveVisible_-1);int cursor=saveCursor_;
 if(key({SDL_SCANCODE_UP,SDL_SCANCODE_KP_8})||in.pad_pressed(SDL_CONTROLLER_BUTTON_DPAD_UP))cursor=(cursor+rows-1)%rows;
 if(key({SDL_SCANCODE_DOWN,SDL_SCANCODE_KP_2})||in.pad_pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN))cursor=(cursor+1)%rows;
 if(key({SDL_SCANCODE_PAGEUP,SDL_SCANCODE_KP_9})||in.pad_pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))cursor-=page;
 if(key({SDL_SCANCODE_PAGEDOWN,SDL_SCANCODE_KP_3})||in.pad_pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))cursor+=page;
 if(key({SDL_SCANCODE_HOME,SDL_SCANCODE_KP_7}))cursor=0;
 if(key({SDL_SCANCODE_END,SDL_SCANCODE_KP_1}))cursor=rows-1;
 if(in.wheelY!=0)cursor-=in.wheelY>0?1:-1;
 int clicked=-1;
 for(std::size_t i=0;i<saveRowRects_.size();++i)if(inside(saveRowRects_[i])){const int row=saveTop_+int(i);if(in.mouseDX||in.mouseDY||click)cursor=row;if(click)clicked=row;}
 saveCursor_=std::clamp(cursor,0,rows-1);
 if(saveCursor_<saveTop_)saveTop_=saveCursor_;else if(saveCursor_>=saveTop_+saveVisible_)saveTop_=saveCursor_-saveVisible_+1;
 if(yes||clicked>=0){
  const int row=clicked>=0?clicked:saveCursor_;
  if(row>0||saveCount_>=maxSlots)saveConfirm_=row;else save(0);
 }
}
// Drawn over the pause menu: dimmed screen, title, a scrolling list (cursor row highlighted, newest save marked) and a hint or the
// confirmation line, in the TTF font of the character prompt; save labels are the original cp1254 bytes the atlas is indexed by.
void Engine::save_menu_overlay(){
 saveRowRects_.clear();saveConfirmRect_={};
 if(!renderer||currentWidth_<=0||currentHeight_<=0)return;
 const float w=float(currentWidth_),h=float(currentHeight_),scale=h/480.f;
 const auto& font=renderer->font_atlas("serif",std::max(12,int(14*scale)),true);
 const float rowH=std::ceil(font.lineHeight*1.35f),listTop=std::floor(h*0.22f),listBottom=std::floor(h*0.80f);
 const int rows=1+saveCount_;saveVisible_=std::max(1,int((listBottom-listTop)/rowH));
 if(saveCursor_<saveTop_)saveTop_=saveCursor_;else if(saveCursor_>=saveTop_+saveVisible_)saveTop_=saveCursor_-saveVisible_+1;
 saveTop_=std::clamp(saveTop_,0,std::max(0,rows-saveVisible_));
 const std::string_view lang=language_;const bool tr=lang=="tr",de=lang=="de";
 const bool full=saveCount_>=maxSlots;const int newest=saveCount_>0?(saveNext_+maxSlots-1)%maxSlots:-1;
 const int end=std::min(rows,saveTop_+saveVisible_);
 // Every string first: the UICommands keep string_views into saveTexts_.
 saveTexts_.clear();saveTexts_.reserve(std::size_t(end-saveTop_)+4);
 saveTexts_.push_back(tr?"OYUNU KAYDET":de?"SPIEL SPEICHERN":"SAVE GAME");
 for(int row=saveTop_;row<end;++row){
  if(row==0){const auto n=two(saveNext_+1);
   saveTexts_.push_back(full?(tr?"Yeni kay\xFDt: "+n+" (en eski kay\xFDt\xFDn \xFCzerine)":de?"Neuer Spielstand: Platz "+n+" (\xFC""berschreibt den \xE4ltesten)":"New save in slot "+n+" (overwrites the oldest)")
                            :(tr?"Yeni kay\xFDt ("+n+")":de?"Neuer Spielstand (Platz "+n+")":"New save (slot "+n+")"));}
  else saveTexts_.push_back(saveRows_[std::size_t(row-1)]+(row-1==newest?(tr?"  (en yeni)":de?"  (neuester)":"  (newest)"):""));
 }
 const std::string number=saveConfirm_>=0?two((saveConfirm_==0?saveNext_:saveConfirm_-1)+1):std::string{};
 saveTexts_.push_back(saveConfirm_>=0?(tr?number+" numaral\xFD kay\xFDt\xFDn \xFCzerine yaz\xFDls\xFDn m\xFD? Enter: evet  Esc: hay\xFDr":de?"Platz "+number+" \xFC""berschreiben? Enter: ja  Esc: nein":"Overwrite slot "+number+"? Enter: yes  Esc: no")
                                    :(tr?"Enter: kaydet   Esc: geri":de?"Enter: speichern   Esc: zur\xFC""ck":"Enter: save   Esc: back"));
 saveTexts_.push_back(saveTop_>0?"\x5E":"");saveTexts_.push_back(end<rows?"v":"");
 auto solid=[&](Rect r,Color c){UICommand s;s.kind=UICommand::Kind::Solid;s.rect=r;s.color=c;s.layer=1e6f;ui_.push_back(s);};
 auto text=[&](float x,float y,std::string_view s,Color color){if(s.empty())return;UICommand t;t.kind=UICommand::Kind::Text;t.font=&font;t.text=s;t.rect={x+1,y+1,Renderer::text_extent(font,s).x,font.lineHeight};t.color={0,0,0,color.a};t.layer=1e6f;t.filter=true;ui_.push_back(t);t.rect.x=x;t.rect.y=y;t.color=color;ui_.push_back(t);};
 auto centred=[&](std::string_view s,float y,Color color){text(std::floor((w-Renderer::text_extent(font,s).x)/2),y,s,color);};
 solid({0,0,w,h},{0,0,0,0.75f});
 centred(saveTexts_[0],std::floor(h*0.12f),{0.78f,0.16f,0.12f,1});
 float column=0;for(int row=saveTop_;row<end;++row)column=std::max(column,Renderer::text_extent(font,saveTexts_[std::size_t(1+row-saveTop_)]).x);
 const float x=std::floor((w-column)/2),pad=std::ceil(10*scale);
 for(int row=saveTop_;row<end;++row){
  const float y=listTop+float(row-saveTop_)*rowH;const Rect r{x-pad,y-(rowH-font.lineHeight)/2,column+2*pad,rowH};saveRowRects_.push_back(r);
  const bool at=row==saveCursor_,confirming=row==saveConfirm_;
  if(at||confirming)solid(r,confirming?Color{0.78f,0.16f,0.12f,0.45f}:Color{1,1,1,0.14f});
  text(x,y,saveTexts_[std::size_t(1+row-saveTop_)],at?Color{1,0.85f,0.4f,1}:row==0?Color{0.75f,0.95f,0.75f,1}:Color{0.85f,0.85f,0.85f,1});
 }
 const std::size_t tail=std::size_t(1+end-saveTop_);
 centred(saveTexts_[tail+1],listTop-rowH,{0.85f,0.85f,0.85f,1});centred(saveTexts_[tail+2],listTop+float(saveVisible_)*rowH,{0.85f,0.85f,0.85f,1});
 const auto& bottom=saveTexts_[tail];const float bw=Renderer::text_extent(font,bottom).x,by=std::floor(h*0.86f);
 text(std::floor((w-bw)/2),by,bottom,saveConfirm_>=0?Color{1,0.85f,0.4f,1}:Color{0.7f,0.7f,0.7f,1});
 if(saveConfirm_>=0)saveConfirmRect_={std::floor((w-bw)/2)-pad,by-pad,bw+2*pad,font.lineHeight+2*pad};
}
}
