// Native seamless doors (docs/SEAMLESS_DOORS.md). Not in the original: with seamless_doors=off (or an ineligible transition)
// every function here is inert and the original LevelYonetim.wdl LevelYukle fade runs unchanged. With it on, an LY_Kapi scan
// opens a doorway preview of the prefetched destination WMB; crossing it hands LY_HedefLevel back to the original
// LY_YuklemeTakip -> LevelYukle (Acikmi, Cikis, resets, Level_Load, Giris, map/save bookkeeping) while the LYP_Fade panel is
// hidden, its two wait loops are cut short, and the frames between Level_Load and the Giris placement are not presented.
#include "engine.hpp"
#include "render_policy.hpp"
#include "wdl.hpp"
#include <cmath>
#include <regex>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
namespace k22 {
using render_policy::add;using render_policy::mul;using render_policy::dot;using render_policy::cross;using render_policy::unit;
namespace {
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
bool same_name(std::string_view a,std::string_view b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return std::tolower((unsigned char)x)==std::tolower((unsigned char)y);});}
bool door_action(std::string_view action){return same_name(action,"LY_Kapi");}
Vec3 closest_on_triangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c){ // Ericson, Real-Time Collision Detection 5.1.5
    Vec3 ab=sub(b,a),ac=sub(c,a),ap=sub(p,a);float d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return a;
    Vec3 bp=sub(p,b);float d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return b;
    float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return add(a,mul(ab,d1/(d1-d3)));
    Vec3 cp=sub(p,c);float d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return c;
    float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return add(a,mul(ac,d2/(d2-d6)));
    float va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return add(b,mul(sub(c,b),(d4-d3)/((d4-d3)+(d5-d6))));
    float den=1/(va+vb+vc);return add(a,add(mul(ab,vb*den),mul(ac,vc*den)));
}
// Nearest collision hit along o+t*d, 0<t<limit (Moller-Trumbore over the whole collision mesh; doors are few).
std::optional<float> ray(const CollisionMesh& m,Vec3 o,Vec3 d,float limit){
    std::optional<float> best;
    for(const auto& t:m.triangles){Vec3 a=m.vertices[t[0]],e1=sub(m.vertices[t[1]],a),e2=sub(m.vertices[t[2]],a),h=cross(d,e2);float det=dot(e1,h);if(std::abs(det)<1e-6f)continue;
        float inv=1/det;Vec3 s=sub(o,a);float u=dot(s,h)*inv;if(u<0||u>1)continue;Vec3 q=cross(s,e1);float v=dot(d,q)*inv;if(v<0||u+v>1)continue;
        float hit=dot(e2,q)*inv;if(hit>0&&hit<limit&&(!best||hit<*best))best=hit;}
    return best;
}
std::optional<float> floor_below(const CollisionMesh& m,Vec3 p){auto t=ray(m,p,{0,0,-1},200);return t?std::optional<float>(p.z-*t):std::nullopt;}
}
std::optional<DoorSide> door_side(const World& w,std::span<const Vec3> triggers,const Vec3* room){
    if(triggers.empty())return {};
    Vec3 c{};for(Vec3 t:triggers)c=add(c,t);c=mul(c,1.f/float(triggers.size()));
    const auto& m=w.collision;float best=40;Vec3 q{},n{};
    for(const auto& t:m.triangles){Vec3 a=m.vertices[t[0]],b=m.vertices[t[1]],cc=m.vertices[t[2]],fn=cross(sub(b,a),sub(cc,a));float l=std::sqrt(dot(fn,fn));if(l<1e-6f)continue;fn=mul(fn,1/l);if(std::abs(fn.z)>.3f)continue;
        Vec3 p=closest_on_triangle(c,a,b,cc),d=sub(c,p);float dist=std::sqrt(dot(d,d));if(dist<best){best=dist;q=p;n=fn;}}
    if(best>=40)return {};
    n.z=0;n=unit(n);if(dot(sub(c,q),n)<0)n=mul(n,-1);
    // The room is the side with a floor (a trigger may stand outside the wall it is scanned through, e.g. Ev3); with a floor on
    // both sides, the side of `room` (a spot known to be in it: the Giris arrival at this door), else the trigger's (e.g. Tnel24 -> 40).
    Vec3 probe{q.x,q.y,c.z};auto front=floor_below(m,add(probe,mul(n,24))),back=floor_below(m,add(probe,mul(n,-24)));
    if((!front&&back)||(front&&back&&room&&dot(sub(*room,q),n)<0)){n=mul(n,-1);std::swap(front,back);}
    if(!front)return {};
    // Measure from the room-facing surface: a trigger behind the wall found its far face.
    if(auto hit=ray(m,add(probe,mul(n,24)),mul(n,-1),48))q=add(add(probe,mul(n,24)),mul(n,-*hit));
    DoorSide side;side.normal=n;side.right={n.y,-n.x,0};side.floor=*front;
    float uc=0;for(Vec3 t:triggers)uc+=dot(sub(t,q),side.right);uc/=float(triggers.size());
    // Door face: the narrowest door-sized material patch coplanar with the wall around the trigger (dormet4, doryes, koDOOR...),
    // on the room side of the measured surface (a door slab's far face, e.g. Ev3 odadoor, lies behind it).
    struct Patch{float lo=1e9,hi=-1e9,zl=1e9,zh=-1e9,s=0;int count=0;};std::unordered_map<std::uint32_t,Patch> patches;
    const auto& mesh=w.mesh;
    for(const auto& t:mesh.triangles){Vec3 a=mesh.vertices[t.indices[0]].position,b=mesh.vertices[t.indices[1]].position,cc=mesh.vertices[t.indices[2]].position;
        Vec3 tn=unit(cross(sub(b,a),sub(cc,a)));if(std::abs(dot(tn,n))<.95f)continue;float s=dot(sub(mul(add(a,add(b,cc)),1/3.f),q),n);if(s<-1||s>8)continue;
        Patch p;for(Vec3 v:{a,b,cc}){float u=dot(sub(v,q),side.right);p.lo=std::min(p.lo,u);p.hi=std::max(p.hi,u);p.zl=std::min(p.zl,v.z);p.zh=std::max(p.zh,v.z);}
        if(p.hi<uc-120||p.lo>uc+120||p.zh<side.floor-4||p.zl>side.floor+240)continue;
        auto& g=patches[t.materialIndex];g.lo=std::min(g.lo,p.lo);g.hi=std::max(g.hi,p.hi);g.zl=std::min(g.zl,p.zl);g.zh=std::max(g.zh,p.zh);g.s+=s;++g.count;}
    const Patch* door=nullptr;
    for(const auto& [material,g]:patches){float width=g.hi-g.lo,height=g.zh-g.zl;
        if(width<24||width>200||height<48||height>240||g.zl>side.floor+12||g.lo<uc-120||g.hi>uc+120||uc<g.lo-12||uc>g.hi+12)continue;
        if(!door||width<door->hi-door->lo)door=&g;}
    if(door){side.center=add(add(q,mul(n,door->s/float(door->count))),mul(side.right,(door->lo+door->hi)/2));side.center.z=(door->zl+door->zh)/2;side.halfWidth=(door->hi-door->lo)/2;side.halfHeight=(door->zh-door->zl)/2;}
    else{float lo=1e9,hi=-1e9;for(Vec3 t:triggers){float u=dot(sub(t,q),side.right);lo=std::min(lo,u);hi=std::max(hi,u);}
        side.center=add(q,mul(side.right,uc));side.center.z=side.floor+44;side.halfWidth=std::clamp((hi-lo)/2+12,22.f,48.f);side.halfHeight=44;}
    return side;
}
std::vector<std::vector<Vec3>> door_clusters(const World& w,int target){
    std::vector<std::vector<Vec3>> clusters;
    for(const auto& e:w.entities){if(!door_action(e.action)||int(e.skills[0])!=target)continue;
        auto near=[&](const std::vector<Vec3>& c){for(Vec3 p:c)if(std::hypot(p.x-e.position.x,p.y-e.position.y)<=48&&std::abs(p.z-e.position.z)<=48)return true;return false;};
        auto found=std::find_if(clusters.begin(),clusters.end(),near);if(found==clusters.end())clusters.push_back({e.position});else found->push_back(e.position);}
    return clusters;
}
std::optional<DoorLink> door_link(const World& a,std::span<const Vec3> triggers,const World& b,int from,const std::array<float,4>* arrival,const Vec3* room){
    auto sa=door_side(a,triggers,room);if(!sa)return {};
    // B's doorway: its LY_Kapi back to A (the one nearest the Giris arrival when known), else the wall behind the arrival.
    auto clusters=door_clusters(b,from);std::vector<Vec3> anchor;
    const Vec3 at=arrival?Vec3{(*arrival)[0],(*arrival)[1],(*arrival)[2]}:Vec3{};
    if(arrival){
        float best=160;
        for(auto& c:clusters){Vec3 m{};for(Vec3 p:c)m=add(m,p);m=mul(m,1.f/float(c.size()));float d=std::hypot(m.x-at.x,m.y-at.y);if(d<best){best=d;anchor=c;}}
        if(anchor.empty()){
            const float pan=render_policy::rad((*arrival)[3]);const Vec3 back{-std::cos(pan),-std::sin(pan),0};float hit=160;const auto& m=b.collision;
            for(const auto& t:m.triangles){Vec3 v0=m.vertices[t[0]],e1=sub(m.vertices[t[1]],v0),e2=sub(m.vertices[t[2]],v0),h=cross(back,e2);float det=dot(e1,h);if(std::abs(det)<1e-6f)continue;
                float inv=1/det;Vec3 s=sub(at,v0);float u=dot(s,h)*inv;if(u<0||u>1)continue;Vec3 q=cross(s,e1);float v=dot(back,q)*inv;if(v<0||u+v>1)continue;float d=dot(e2,q)*inv;if(d>0&&d<hit)hit=d;}
            if(hit>=160||hit<10)return {};
            anchor.push_back(add(at,mul(back,hit-10)));
        }
    }
    else if(clusters.size()==1)anchor=clusters[0];
    else return {}; // no or ambiguous return door
    auto sb=door_side(b,anchor,arrival?&at:nullptr);if(!sb)return {};
    // Walking through A's door (-nA) continues away from B's door into B's room (+nB): R*nB=-nA; door floor points coincide.
    const float pan=float(std::remainder((std::atan2(-sa->normal.y,-sa->normal.x)-std::atan2(sb->normal.y,sb->normal.x))*180/render_policy::pi,360.0));
    const float cp=std::cos(render_policy::rad(pan)),sp=std::sin(render_policy::rad(pan));
    Vec3 bottomA{sa->center.x,sa->center.y,sa->center.z-sa->halfHeight},bottomB{sb->center.x,sb->center.y,sb->center.z-sb->halfHeight};
    Vec3 rotated{cp*bottomB.x-sp*bottomB.y,sp*bottomB.x+cp*bottomB.y,bottomB.z};
    return DoorLink{*sa,*sb,sub(bottomA,rotated),{pan,0,0}};
}
// The original LV_*_Giris arrival for a transition from `from`: the VAR array of its
// "if(_EskiLevel==from){vec_set(player.x,VAR);player.pan=VAR[3];}" branch (first one, e.g. LV_Ev2.wdl LY_Ev2_G99), read live.
std::optional<std::array<float,4>> Engine::door_arrival(Runtime& runtime,int to,int from){
    if(!girisParsed_){
        girisParsed_=true;static const std::regex branch(R"(_eskilevel\s*==\s*0*(\d+)\s*\)\s*\{[^{}]*?vec_set\s*\(\s*player\.x\s*,\s*(\w+)\s*\))");
        for(const auto& file:assets.names()){if(!file.ends_with(".wdl"))continue;std::string text=assets.text(file);for(auto& c:text)c=char(std::tolower((unsigned char)c));
            for(std::size_t at=text.find("_giris(");at!=std::string::npos;at=text.find("_giris(",at+1)){
                const auto begin=text.find_last_not_of("abcdefghijklmnopqrstuvwxyz0123456789_",at)+1,keyword=text.find_last_not_of(" \t",begin-1); // "function NAME_giris("
                if(begin==0||keyword==std::string::npos||keyword<7||text.compare(keyword-7,8,"function")!=0)continue;
                auto name=text.substr(begin,at+6-begin);auto open=text.find('{',at);if(open==std::string::npos)continue;
                std::size_t close=open;for(int depth=0;close<text.size();++close){if(text[close]=='{')++depth;else if(text[close]=='}'&&--depth==0)break;}
                auto& list=girisPlacements_[name];
                for(std::sregex_iterator m(text.begin()+std::ptrdiff_t(open),text.begin()+std::ptrdiff_t(close),branch),end;m!=end;++m)list.emplace_back(std::stoi((*m)[1]),(*m)[2]);
            }}
    }
    auto hooks=global("LVPL_Giris");auto* table=hooks.kind==Value::Kind::Object?state.get(hooks.bits):nullptr;
    if(!table||to<1||std::size_t(to)>=table->array.size()||table->array[std::size_t(to)].value.kind!=Value::Kind::Function)return {};
    const auto hook=table->array[std::size_t(to)].value.bits;
    for(const auto& [name,list]:girisPlacements_){if(runtime.function(name)!=hook)continue;
        for(const auto& [level,var]:list){if(level!=from)continue;auto v=global(var);auto* o=v.kind==Value::Kind::Object?state.get(v.bits):nullptr;
            if(!o||o->array.size()<4)return {};return std::array<float,4>{float(o->array[0].value.real()),float(o->array[1].value.real()),float(o->array[2].value.real()),float(o->array[3].value.real())};}
        return {};}
    return {};
}
// LevelYonetim.wdl LevelYukle (and LY_Tarama's 62/64 mapping): transitions with stair waits/sounds, movies, kararma panels or
// an entry that keeps the screen dark keep the original fade. Evaluated with the live flags the script itself tests.
bool Engine::door_scripted(int from,int to)const{
    auto v=[&](std::string_view name){return int(number(name));};
    auto pair=[&](int a,int b){return from==a&&to==b;};
    for(auto [a,b]:{std::pair{15,10},{7,10},{38,39},{65,60},{10,15},{10,7},{39,38},{60,65},{67,68},{68,67},{83,84},{84,83},{8,55},{64,39},{42,84},{62,65}})if(pair(a,b))return true;
    if((v("oyun_ilk_basladi")==1&&to==1)||(v("aktif_player")==0&&pair(30,17)&&v("v_korufinal_ok")==0)||(pair(2,25)&&v("v_doktor_ok")==0)
       ||(pair(93,70)&&v("v_demirkurtul_oldumu")==1&&v("v_ebrumefta_ok")==0))return true;
    return (to==27&&v("v_ebru_ev_ok")==0)||(to==98&&v("aktif_player")==2)||(to==31&&v("v_kizgiris_ok")==0)||to==62||to==64||from==62||from==64
        ||(to==38&&v("v_kuyugir_ok")==0)||to==103||to==101;
}
std::string Engine::level_wmb(int id)const{
    auto list=global("LVSL_Leveller");auto* table=list.kind==Value::Kind::Object?state.get(list.bits):nullptr;
    if(!table||id<=0||std::size_t(id)>=table->array.size())return {};
    auto* s=state.get(table->array[std::size_t(id)].value.bits);return s&&s->kind==Object::Kind::String?s->text:std::string{};
}
// LevelYonetim.wdl LY_Tarama's refusals (on use it shows LV_HataGoster 11/38/39), from the live values it tests, for the group's
// live LY_Kapi triggers: a FLAG1 door in Inzar's story while cadde_yara_adet[aktif_level]>0, else Demir to 102 and Inzar to 38.
bool Engine::door_refused(Runtime& runtime,const DoorGroup& g)const{
    const FunctionId kapi=runtime.function("LY_Kapi");const int who=int(number("Aktif_Player"));
    auto counts=global("cadde_yara_adet");auto* list=counts.kind==Value::Kind::Object?state.get(counts.bits):nullptr;const auto level=std::size_t(std::max(0.0,number("aktif_level")));
    const bool enemies=list&&level<list->array.size()&&list->array[level].value.real()>0;
    for(const auto& entry:entities){const ObjectId id=entry.first;auto* o=state.get(id);if(!o||!o->alive||callback(id,"action1")!=kapi)continue;
        const Vec3 p=position(id);const int to=int(field(id,"skill1"));if(to!=g.target||std::hypot(p.x-g.centroid.x,p.y-g.centroid.y)>64)continue;
        if(field(id,"FLAG1")!=0&&who==2){if(enemies)return true;}
        else if((who==0&&to==102)||(who==2&&to==38))return true;
    }
    return false;
}
void Engine::door_reset(){door_.phase=DoorPhase::Idle;door_.open=0;door_.openTime=0;door_.ticks=0;door_.group=~std::size_t{};door_.used=false;door_.ahead={INFINITY,INFINITY};fadeHidden_=false;}
void Engine::door_level(){
    // Door groups of the level just loaded (static WMB data). A prefetched destination that became this level is not dropped.
    door_.groups.clear();door_.groupsWorld=world;if(!world)return;
    for(const auto& e:world->entities){if(!door_action(e.action)||e.skills[0]<1)continue;int target=int(e.skills[0]);
        auto found=std::find_if(door_.groups.begin(),door_.groups.end(),[&](const DoorGroup& g){if(g.target!=target)return false;for(Vec3 p:g.triggers)if(std::hypot(p.x-e.position.x,p.y-e.position.y)<=48&&std::abs(p.z-e.position.z)<=48)return true;return false;});
        if(found==door_.groups.end())door_.groups.push_back({target,{e.position}});else found->triggers.push_back(e.position);}
    for(auto& g:door_.groups){Vec3 c{};for(Vec3 p:g.triggers)c=add(c,p);g.centroid=mul(c,1.f/float(g.triggers.size()));}
}
// Destination of a door group, decoded (adopting a finished prefetch); null while a prefetch is still running and !wait.
const World* Engine::door_world(Runtime& runtime,DoorGroup& g,bool wait){
    auto wmb=level_wmb(g.target);if(wmb.empty())return nullptr;
    if(!same_name(wmb,door_.wmb)){
        // One prefetched destination at a time: retire the previous one unless it is the level itself.
        if(door_.world&&door_.world!=world){if(renderer)renderer->purge_model(&door_.world->mesh);assets.drop_world(door_.wmb);}
        for(auto& other:door_.groups)other.linked=false;
        door_.world=nullptr;door_.wmb=wmb;
    }
    if(!door_.world){if(!wait&&!assets.prefetch_world(wmb))return nullptr;door_.world=&assets.world(wmb);}
    if(!g.linked){const int from=int(number("LY_AktifLevelID"));auto arrival=door_arrival(runtime,g.target,from);
        // This level's own Giris arrival from the destination stands in front of this door: it tells the room side.
        auto back=door_arrival(runtime,from,g.target);std::optional<Vec3> room;if(back&&std::hypot((*back)[0]-g.centroid.x,(*back)[1]-g.centroid.y)<160)room=Vec3{(*back)[0],(*back)[1],(*back)[2]};
        g.link=door_link(*world,g.triggers,*door_.world,from,arrival?&*arrival:nullptr,room?&*room:nullptr);g.linked=true;}
    return door_.world;
}
void Engine::door_scan(Runtime& runtime,ObjectId trigger){
    if(!seamlessDoors||state.profile!=SemanticProfile::CampaignA6||!world)return;
    auto* o=state.get(trigger);if(!o||!o->alive)return;auto action=o->field("action1").value;
    if(action.kind!=Value::Kind::Function||action.bits!=runtime.function("LY_Kapi"))return;
    const int target=int(number("LY_HedefLevel")),from=int(number("LY_AktifLevelID"));if(target<1||from<1)return;
    if(door_.groupsWorld!=world)door_level();
    const Vec3 p=position(trigger);std::size_t index=~std::size_t{};float best=64;
    for(std::size_t i=0;i<door_.groups.size();++i){const auto& g=door_.groups[i];float d=std::hypot(g.centroid.x-p.x,g.centroid.y-p.y);if(g.target==target&&d<best){best=d;index=i;}}
    if(index==~std::size_t{})return;
    if(door_.phase==DoorPhase::Opening&&door_.group==index){set_global("LY_HedefLevel",Value{});door_.used=true;return;} // repeated scans of the open door
    if(door_.phase!=DoorPhase::Idle&&door_.phase!=DoorPhase::Opening)return;
    if(door_scripted(from,target))return;
    auto& g=door_.groups[index];if(!door_world(runtime,g,true)||!g.link)return; // no sane transform: the original fade
    set_global("LY_HedefLevel",Value{});door_reset();door_.phase=DoorPhase::Opening;door_.group=index;door_.from=from;door_.used=true;
}
void Engine::door_tick(Runtime& runtime){
    if(!seamlessDoors||state.profile!=SemanticProfile::CampaignA6)return;
    const double dt=std::clamp(input.dt,0.0,.1);
    auto fade=global("LYP_Fade");const ObjectId fadeId=fade.kind==Value::Kind::Object?fade.bits:0;
    const ObjectId player=live_player();
    // Coop (docs/COOP.md): either body opens and crosses a door; the load acts on P1 like the original use by P2.
    const std::array<ObjectId,2> bodies{player,coop_body(1)};
    auto facing=[&](ObjectId b){const float pan=render_policy::rad(float(field(b,"pan")));return Vec3{std::cos(pan),std::sin(pan),0};};
    switch(door_.phase){
    case DoorPhase::Idle:{
        if(!world||!player)break;if(door_.groupsWorld!=world)door_level();
        const int from=int(number("LY_AktifLevelID"));
        // Proactive opening (not in the original; the use key keeps working, door_scan): a body within 96 quants of an armed
        // eligible doorway, in front of it and facing it (60 degrees) opens it when LY_Tarama would let the use key. A door is armed
        // once every body was more than 128 quants away while the level is in play, so the arrival's return door stays closed.
        if(gameplay_blocked(runtime).empty()){
            auto reach=[&](const DoorGroup& g,ObjectId b){const Vec3 at=position(b);Vec3 c=g.centroid;float d=std::hypot(at.x-c.x,at.y-c.y);
                if(g.linked&&g.link){const auto& s=g.link->a;const Vec3 r=sub(at,s.center);const float ahead=dot(r,s.normal);if(ahead< -8)return INFINITY;
                    d=std::hypot(ahead,std::max(0.f,std::abs(dot(r,s.right))-s.halfWidth));c=s.center;}
                const Vec3 to{c.x-at.x,c.y-at.y,0};const float l=std::hypot(to.x,to.y);return l<1||dot(facing(b),to)>=.5f*l?d:INFINITY;};
            DoorGroup* best=nullptr;float bestReach=96;
            for(auto& g:door_.groups){const bool eligible=g.armed&&!door_scripted(from,g.target);bool far=true;
                for(ObjectId b:bodies){if(!b)continue;const Vec3 at=position(b);if(std::hypot(at.x-g.centroid.x,at.y-g.centroid.y)<=128)far=false;
                    if(eligible)if(float r=reach(g,b);r<bestReach){bestReach=r;best=&g;}}
                if(far)g.armed=true;}
            if(best){const auto index=std::size_t(best-door_.groups.data());door_.warm=index;
                if(door_world(runtime,*best,false)&&best->link&&!door_refused(runtime,*best)){door_reset();door_.phase=DoorPhase::Opening;door_.group=index;door_.from=from;}
                break;} // still decoding, an ineligible (fade) door or refused: closed, the use key does what it did
        }
        if(++door_.ticks%15)break; // prefetch the nearest door's destination in the background
        const Vec3 at=position(player);DoorGroup* nearest=nullptr;float best=320;
        for(auto& g:door_.groups){float d=std::hypot(g.centroid.x-at.x,g.centroid.y-at.y);if(d<best&&!door_scripted(from,g.target)){best=d;nearest=&g;}}
        door_.warm=nearest?nearest-door_.groups.data():~std::size_t{};
        if(nearest)door_world(runtime,*nearest,false);
        break;}
    case DoorPhase::Opening:{
        auto& g=door_.groups[door_.group];
        if(!player||!g.link||number("LY_HedefLevel")>0||world!=door_.groupsWorld||number("LY_AktifLevelID")!=door_.from){door_reset();break;}
        const auto& side=g.link->a;bool near=false,walkIn=false;
        for(std::size_t i=0;i<bodies.size();++i){if(!bodies[i])continue;const Vec3 at=sub(position(bodies[i]),side.center);if(std::hypot(at.x,at.y)<=140)near=true;
            const float ahead=dot(at,side.normal),last=std::exchange(door_.ahead[i],ahead);if(std::abs(dot(at,side.right))>=side.halfWidth+8)continue;
            // Walking into the doorway. Opened by approach: facing into it (not brushing past a jamb) and moving in (5+ quants/s), or at the door
            // face (a body stops 16 quants from a wall), so standing in front of it and turning around does not cross.
            if(door_.used?ahead<28:dot(facing(bodies[i]),side.normal)< -.3f&&(ahead<20||(ahead<28&&ahead<last-5*float(dt))))walkIn=true;}
        if(!near){door_reset();break;} // every body walked away: the door closes, nothing else happened
        door_.open=float(std::min(1.0,door_.open+dt/.4));if(door_.open>=1)door_.openTime+=float(dt);
        // Walking into the doorway crosses; after the use key, standing in front of it goes through after a moment, like the original use.
        if(door_.open>=1&&(walkIn||(door_.used&&door_.openTime>=.6f))){
            if(!door_.used){if(!gameplay_blocked(runtime).empty())break; // a scene took control: no crossing
                if(door_refused(runtime,g)){g.armed=false;door_reset();break;}} // e.g. enemies came back while it was open
            set_global("LY_HedefLevel",Value::from_int(g.target));door_.phase=DoorPhase::Crossing;door_.ticks=0;fadeHidden_=true;}
        break;}
    case DoorPhase::Crossing:
        // LevelYukle's fade-out loop (LYP_Fade.Alpha+=13*time until 100) ends on its next test.
        if(fadeId&&field(fadeId,"visible")&&field(fadeId,"alpha")<100)set_field(fadeId,"alpha",100);
        // Acikmi refused (LV_HataGoster) or the scripts never loaded: the door closes and waits for the bodies to leave.
        if(++door_.ticks>240){if(world==door_.groupsWorld&&door_.group<door_.groups.size())door_.groups[door_.group].armed=false;door_reset();}
        break;
    case DoorPhase::Holding:
        // LV_*_Giris placed the player and LevelYukle set LY_AktifLevelID; the fade-in loop (Alpha-=12.5*time) ends at once.
        // One more tick lets the destination's camera action follow the placed player before frames are presented again.
        if(number("LY_AktifLevelID")==door_.target||++door_.ticks>90){if(fadeId)set_field(fadeId,"alpha",0);if(door_.held++>=1){door_.phase=DoorPhase::Revealing;door_.ticks=0;}}
        break;
    case DoorPhase::Revealing:
        if(fadeId&&field(fadeId,"visible")&&field(fadeId,"alpha")>0)set_field(fadeId,"alpha",0);
        if((fadeId&&!field(fadeId,"visible"))||++door_.ticks>180){++doorSwaps;door_reset();}
        break;
    }
}
// load_level hook, before the old level is released: the crossed door's destination (or any other prefetched destination the
// original path loads) keeps its decoded world and GPU mesh; a crossing also holds frames until Giris.
const World* Engine::door_load(std::string_view level){
    const bool crossing=door_.phase==DoorPhase::Crossing;
    if(door_.world&&(crossing||door_.world!=world)&&same_name(level,door_.wmb)){
        const int target=crossing?door_.groups[door_.group].target:0;door_reset();
        if(crossing){door_.phase=DoorPhase::Holding;door_.target=target;door_.held=0;fadeHidden_=true;}
        return door_.world;
    }
    door_reset();door_.world=nullptr;door_.wmb.clear();return nullptr; // released with the level
}
void Engine::door_frame(){
    frame_.door={};
    if(!seamlessDoors||!world||door_.groupsWorld!=world||!door_.world)return;
    std::size_t index=door_.phase==DoorPhase::Opening||door_.phase==DoorPhase::Crossing?door_.group:door_.phase==DoorPhase::Idle?door_.warm:~std::size_t{};
    if(index>=door_.groups.size())return;const auto& g=door_.groups[index];if(!g.linked||!g.link||!same_name(level_wmb(g.target),door_.wmb))return;
    const auto& side=g.link->a;auto& d=frame_.door;d.world=door_.world;d.position=g.link->position;d.angles=g.link->angles;
    d.normal=side.normal;d.right=side.right;d.up={0,0,1};d.halfHeight=side.halfHeight;
    // The doorway opens from its left jamb like a door leaf (smoothstep over 0.4 s); idle prefetch only uploads (closed).
    const float open=door_.phase==DoorPhase::Idle?0:door_.phase==DoorPhase::Crossing?1:door_.open*door_.open*(3-2*door_.open);
    d.halfWidth=side.halfWidth*open;d.center=add(side.center,mul(side.right,side.halfWidth*(open-1)));
}
int Engine::door_phase()const{return int(door_.phase);}
// kabus22 --door-table LEVELS.tsv --data GAME_ROOT: the static eligibility of every LY_Kapi doorway (docs/SEAMLESS_DOORS.md).
// LEVELS.tsv rows are "id<TAB>wmb..." (tests/levels/levels.tsv). Flag-dependent LevelYukle cases are reported as conditional.
int door_table(int argc,char** argv){
    std::filesystem::path table,data;
    for(int i=1;i<argc;++i){std::string_view a=argv[i];if(i+1>=argc)throw std::runtime_error("missing value for "+std::string(a));
        if(a=="--door-table")table=argv[++i];else if(a=="--data")data=argv[++i];else throw std::runtime_error("--door-table accepts only --data GAME_ROOT");}
    if(data.empty())throw std::runtime_error("--door-table requires --data GAME_ROOT");
    std::map<int,std::string> levels;{std::ifstream in(table);if(!in)throw std::runtime_error("cannot read "+table.string());std::string line;
        while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::istringstream row(line);int id;std::string wmb;if(row>>id>>wmb)levels[id]=wmb;}}
    Assets assets(data);for(auto name:{"K22Widescreen.wrs","K22.wrs","Kaynak.k22"})if(assets.contains(name))assets.mount(name);
    auto scripted=[](int from,int to)->std::string_view{
        for(auto [a,b]:{std::pair{15,10},{7,10},{38,39},{65,60},{10,15},{10,7},{39,38},{60,65},{67,68},{68,67},{83,84},{84,83}})if(from==a&&to==b)return "stairs";
        for(auto [a,b]:{std::pair{8,55},{64,39},{42,84}})if(from==a&&to==b)return "kararma panel";
        if((from==62&&to==65)||to==62||to==64||from==62||from==64||to==101||to==103)return "movie/cutscene";
        if(to==1)return "conditional: movie after New Game";if(from==30&&to==17)return "conditional: movie (Demir, v_korufinal_ok)";if(from==2&&to==25)return "conditional: movie (v_doktor_ok)";
        if(from==93&&to==70)return "conditional: movie (v_ebrumefta_ok)";if(to==27)return "conditional: dark entry (v_ebru_ev_ok)";if(to==31)return "conditional: dark entry (v_kizgiris_ok)";
        if(to==38)return "conditional: dark entry (v_kuyugir_ok)";if(to==98)return "conditional: dark entry (Inzar)";return {};};
    std::cout<<"from\tfrom_wmb\tto\tto_wmb\tresult\tpan\tdoor_w\tdoor_h\n";
    for(const auto& [from,fromWmb]:levels){
        assets.release_level();const World& a=assets.world(fromWmb);std::vector<std::pair<int,std::vector<Vec3>>> doors;
        std::set<int> targets;for(const auto& e:a.entities)if(door_action(e.action)&&e.skills[0]>=1)targets.insert(int(e.skills[0]));
        for(int to:targets)for(auto& c:door_clusters(a,to))doors.emplace_back(to,std::move(c));
        for(const auto& [to,triggers]:doors){
            std::string result;std::optional<DoorLink> link;auto toWmb=levels.contains(to)?levels[to]:std::string{};
            auto sa=door_side(a,triggers);
            if(toWmb.empty())result="fade: no level";
            else if(auto why=scripted(from,to);!why.empty()&&!why.starts_with("conditional"))result="fade: "+std::string(why);
            else if(!sa)result="fade: no wall/floor at the trigger";
            else{const World& b=assets.world(toWmb);auto back=door_clusters(b,from);
                if(back.empty())result="fade: no return door";else if(back.size()>1)result="fade: "+std::to_string(back.size())+" return doors";
                else if(!door_side(b,back[0]))result="fade: no wall/floor at the return door";
                else{link=door_link(a,triggers,b,from,nullptr);result=why.empty()?"seamless":"seamless; "+std::string(why);}
                if(&b!=&a)assets.drop_world(toWmb);}
            std::cout<<from<<'\t'<<fromWmb<<'\t'<<to<<'\t'<<toWmb<<'\t'<<result<<'\t'<<(link?std::lround(link->angles.x):0)<<'\t'<<(sa?std::lround(2*sa->halfWidth):0)<<'\t'<<(sa?std::lround(2*sa->halfHeight):0)<<'\n';
        }
    }
    return 0;
}
}
