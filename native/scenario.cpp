#include "scenario.hpp"
#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <queue>
#include <unordered_map>
#include <map>
#include <ostream>
#include <sstream>
namespace k22 {
namespace {
std::string lower(std::string_view s){std::string out(s);for(auto& c:out)if(c>='A'&&c<='Z')c=char(c+32);return out;}
std::vector<std::string> words(std::string_view line){std::vector<std::string> out;std::istringstream in{std::string(line)};for(std::string w;in>>w;)out.push_back(w);return out;}
std::string join(const std::vector<std::string>& w,std::size_t from,std::size_t to){std::string out;for(auto i=from;i<to;++i){if(!out.empty())out+=' ';out+=w[i];}return out;}
long number(const std::string& text,int line){long n=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),n);if(error!=std::errc{}||end!=text.data()+text.size())throw std::runtime_error("scenario line "+std::to_string(line)+": expected integer, got '"+text+"'");return n;}
int scancode(const std::string& name,int line){std::string sdl=name;for(auto& c:sdl)if(c=='_')c=' ';auto code=SDL_GetScancodeFromName(sdl.c_str());if(code==SDL_SCANCODE_UNKNOWN)throw std::runtime_error("scenario line "+std::to_string(line)+": unknown SDL scancode name '"+name+"'");return code;}
enum class Op {Frames,Key,Mouse,MouseButton,JoyButton,JoyAxis,PadButton,PadAxis,PressUntil,Wait,Assert,Store,Set,Find,List,Capture,Print,Realtime,WalkTo,TurnTo,ExpectExit,Type};
struct Step {Op op;int line=0;std::string source,name,condition;int key=0,index=0;long count=0;float x=0,y=0;bool down=false;float radius=0;std::vector<float> path;};
struct Val {bool text=false;double number=0;std::string string;};
// WMB placement and ent_create both store the action in ACTION1 (engine.cpp load_level/create_entity).
bool runs(const Engine& e,ObjectId id,FunctionId action){auto* o=e.state.get(id);return action&&o&&o->alive&&e.callback(id,"action1")==action;}
// Expressions: terms joined by + or -, compared with == != < <= > >=, combined with && (binds tighter) and ||.
// Terms: NUMBER, "TEXT", GLOBAL, GLOBAL[i], GLOBAL.field, GLOBAL[i].field, @stored, @stored.field, $level, $frame, $count(ACTION), $dist(@stored), $facing(@stored).
struct Eval {
 const Engine& engine;const Runtime& runtime;const std::map<std::string,Val>& stored;std::uint64_t frame;std::string_view s;std::size_t p=0;
 [[noreturn]] void fail(const std::string& why){throw std::runtime_error(why+" at '"+std::string(s.substr(std::min(p,s.size())))+"'");}
 void space(){while(p<s.size()&&s[p]==' ')++p;}
 bool take(std::string_view t){space();if(s.substr(p,t.size())!=t)return false;p+=t.size();return true;}
 std::string ident(){space();auto b=p;while(p<s.size()&&(std::isalnum(static_cast<unsigned char>(s[p]))||s[p]=='_'))++p;if(b==p)fail("expected identifier");return std::string(s.substr(b,p-b));}
 const Object* object(ObjectId id)const{return engine.state.get(id);}
 double field(ObjectId id,const std::string& name)const{if(name=="alive"){auto* o=object(id);return o&&o->alive?1:0;}return engine.field(id,name);}
 ObjectId player()const{auto v=engine.global("player");return v.kind==Value::Kind::Object?v.bits:0;}
 double dist(Vec3 b)const{auto a=engine.position(player());return std::sqrt(double(a.x-b.x)*(a.x-b.x)+double(a.y-b.y)*(a.y-b.y)+double(a.z-b.z)*(a.z-b.z));}
 double dist(ObjectId id)const{return dist(engine.position(id));}
 // Signed degrees from player PAN to the target (positive: turn left, A6 PAN grows counter-clockwise).
 double facing(Vec3 b)const{auto a=engine.position(player());auto d=std::atan2(double(b.y-a.y),double(b.x-a.x))*180/M_PI-engine.field(player(),"pan");return std::remainder(d,360.0);}
 double facing(ObjectId id)const{return facing(engine.position(id));}
 Val stored_term(){auto name=ident();auto it=stored.find(name);if(it==stored.end())fail("unknown stored value @"+name);if(take(".")){if(it->second.text)fail("@"+name+" is text");return {false,field(ObjectId(it->second.number),ident())};}return it->second;}
 Val term(){
  space();if(p>=s.size())fail("expected term");
  if(s[p]=='"'){auto end=s.find('"',p+1);if(end==std::string_view::npos)fail("unterminated text");Val v{true,0,std::string(s.substr(p+1,end-p-1))};p=end+1;return v;}
  if(std::isdigit(static_cast<unsigned char>(s[p]))||((s[p]=='-'||s[p]=='.')&&p+1<s.size()&&std::isdigit(static_cast<unsigned char>(s[p+1])))){char* end=nullptr;std::string rest(s.substr(p));double n=std::strtod(rest.c_str(),&end);p+=std::size_t(end-rest.c_str());return {false,n,{}};}
  if(take("@"))return stored_term();
  if(take("$")){auto name=lower(ident());
   if(name=="level")return {true,0,lower(engine.currentLevel)};if(name=="frame")return {false,double(frame),{}};
   if(name=="levelminz"||name=="levelmaxz"){if(!engine.world)fail("no level loaded");return {false,name=="levelminz"?engine.world->mesh.min.z:engine.world->mesh.max.z,{}};} // WMB geometry bounds
   // Seamless doors (docs/SEAMLESS_DOORS.md): phase 0 idle/1 opening/2 crossing/3 holding/4 revealing, completed swaps, presented LYP_Fade frames.
   if(name=="door")return {false,double(engine.door_phase()),{}};if(name=="doorswaps")return {false,double(engine.doorSwaps),{}};if(name=="fadeframes")return {false,double(engine.fadeFrames),{}};
   if(name=="movie")return {false,engine.movie&&engine.media.movie_playing(engine.movie)?1.0:0.0,{}}; // full-screen media_play (no target bitmap)
   if(name=="character")return {false,double(engine.characterLock),{}}; // native character lock: -1 original, 0 Demir, 1 Ebru, 2 Inzar
   if(name=="prompt")return {false,double(engine.character_prompt()),{}}; // story-switch prompt: the story's character, -1 when closed
   if(name=="health"){static constexpr std::string_view vars[]{"oyn_yaralanma","ebru_yaralanma","koru_yaralanma"};return {false,engine.number(vars[engine.body_character()]),{}};} // the played body's health variable
   if(name=="movieframe")return {false,engine.movie?engine.media.movie_status(engine.movie):0.0,{}}; // media_playing: 1-based decoded frame, 0 when stopped
   if(name=="wall"){static const auto start=std::chrono::steady_clock::now();return {false,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),{}};} // seconds since first $wall read
   if(name=="count"){if(!take("("))fail("expected (");auto action=runtime.function(ident());if(!take(")"))fail("expected )");if(!action)fail("unknown action");int n=0;for(const auto& [id,data]:engine.entities)n+=runs(engine,id,action);return {false,double(n),{}};}
   if(name=="dist"||name=="facing"){if(!take("(")||!take("@"))fail("expected (@name)");auto v=stored_term();if(!take(")"))fail("expected )");auto id=ObjectId(v.number);return {false,name=="dist"?dist(id):facing(id),{}};}
   fail("unknown $"+name);}
  auto name=ident();auto it=engine.state.globals.find(name);if(it==engine.state.globals.end())fail("unknown WDL global "+name);Value v=it->second.value;
  if(take("[")){auto i=sum();if(i.text||i.number<0)fail("bad index");const auto index=std::size_t(i.number);if(!take("]"))fail("expected ]");auto* o=v.kind==Value::Kind::Object?object(v.bits):nullptr;if(!o||index>=o->array.size())fail(name+"["+std::to_string(index)+"] is not an array element");v=o->array[index].value;}
  // WDL VAR arrays carry pointers as raw words (Number bits), globals as Object handles.
  if(take("."))return {false,field(v.bits,ident()),{}};
  if(v.kind==Value::Kind::Object)if(auto* o=object(v.bits);o&&o->kind==Object::Kind::String)return {true,0,o->text};
  return {false,v.real(),{}};
 }
 Val sum(){auto v=term();for(;;){bool plus=take("+");if(!plus&&!(p<s.size()&&(space(),s.substr(p,1)=="-")))return v;if(!plus)++p;auto w=term();if(v.text||w.text)fail("arithmetic on text");v.number+=plus?w.number:-w.number;}}
 bool compare(){auto a=sum();for(auto op:{"==","!=","<=",">=","<",">"}){if(!take(op))continue;auto b=sum();std::string_view o=op;if(a.text!=b.text)fail("comparing text with number");
   if(a.text){bool eq=lower(a.string)==lower(b.string);if(o=="==")return eq;if(o=="!=")return !eq;fail("text supports only == and !=");}
   bool eq=std::fabs(a.number-b.number)<0.0005;if(o=="==")return eq;if(o=="!=")return !eq;if(o=="<=")return a.number<=b.number||eq;if(o==">=")return a.number>=b.number||eq;if(o=="<")return a.number<b.number&&!eq;return a.number>b.number&&!eq;}
  return a.text?!a.string.empty():a.number!=0;}
 bool all(){bool v=compare();while(take("&&")){bool w=compare();v=v&&w;}return v;}
 bool condition(){bool v=all();while(take("||")){bool w=all();v=v||w;}space();if(p!=s.size())fail("unexpected text");return v;}
 Val value(){auto v=sum();space();if(p!=s.size())fail("unexpected text");return v;}
};
std::string show(const Val& v){if(v.text)return '"'+v.string+'"';std::ostringstream o;o<<v.number;return o.str();}
// walk-to ... path: grid A* (24-unit cells) over the A6 query the player's own ent_move uses (Engine::collide with USE_BOX, i.e. the
// player's NARROW/FAT hull, plus IGNORE_PASSABLE). Cell height = floor under the cell (point trace down) + the player's height above
// its own floor; an edge is a hull sweep between cell centres, raised 16 units for the scripted step-up (dmr_yercekimi lifts the
// player onto stairs; a sweep at floor level stops at every riser), and may climb or drop at most 32. Cells are keyed by XY and a 48-unit
// height band, so stacked floors (stairwells, galleries) are separate nodes. An entity goal (level=true) must also be reached within
// 96 units of height. Read-only queries, no gameplay effect.
// ponytail: 8-neighbour grid, 60000-cell cap, entities as they stand at planning time; a stuck walk re-plans (moving blockers).
std::vector<float> plan(Engine& e,ObjectId player,Vec3 from,Vec3 goal,float radius,bool level,std::string& why){
 constexpr float cell=24;const ProcessContext ctx{0,player,0};
 auto floorAt=[&](float x,float y,float z)->std::optional<float>{auto h=e.collide({x,y,z+48},{x,y,z-400},{},4,player,&ctx);if(!h.hit||h.fraction<=0)return std::nullopt;return h.position.z;};
 auto clear=[&](Vec3 a,Vec3 b){a.z+=16;b.z+=16;return !e.collide(a,b,{},4|8192,player,&ctx).hit;};
 auto flat=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);};
 const auto under=floorAt(from.x,from.y,from.z);if(!under){why="no floor under the player";return {};}
 const float lift=from.z-*under;
 struct Node{Vec3 p;float g;std::int64_t parent;int ix,iy;bool closed=false;};
 // 21 bits per axis, non-negative, so -1 stays the "no parent/not found" sentinel
 auto key=[&](std::int64_t x,std::int64_t y,float z){const auto b=std::int64_t(std::floor((z-from.z)/48));return ((x+(1<<20))<<42)|((y+(1<<20))<<21)|(b+(1<<20));};
 auto togo=[&](Vec3 p){return level?std::hypot(flat(p,goal),p.z-goal.z):flat(p,goal);};
 std::unordered_map<std::int64_t,Node> nodes;using Open=std::pair<float,std::int64_t>;std::priority_queue<Open,std::vector<Open>,std::greater<>> open;
 nodes[key(0,0,from.z)]={from,0,-1,0,0};open.push({togo(from),key(0,0,from.z)});std::int64_t found=-1,fallback=-1;float nearestD=togo(from),fallbackD=INFINITY;Vec3 nearestP=from;
 for(int expanded=0;!open.empty()&&expanded<60000;){
  const auto k=open.top().second;open.pop();auto& n=nodes[k];if(n.closed)continue;n.closed=true;++expanded;const Node cur=n;
  if(const float d=flat(cur.p,goal);d<radius&&(!level||std::fabs(cur.p.z-goal.z)<96)){found=k;break;}else if(const float t=togo(cur.p);t<nearestD){nearestD=t;nearestP=cur.p;}
  // The grid is anchored at the player, so its nearest cell can miss RADIUS by a few units; a cell within one more cell whose straight
  // sweep reaches halfway into RADIUS (toward the goal, which may itself touch a wall) is used when nothing inside RADIUS is reachable.
  if(const float d=flat(cur.p,goal);d<radius+cell&&d<fallbackD&&(!level||std::fabs(cur.p.z-goal.z)<96)){const float s=radius*.5f/d;
   if(clear(cur.p,{goal.x+(cur.p.x-goal.x)*s,goal.y+(cur.p.y-goal.y)*s,cur.p.z})){fallback=k;fallbackD=d;}}
  const std::int64_t ix=cur.ix,iy=cur.iy;
  for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){if(!dx&&!dy)continue;
   const float x=from.x+float(ix+dx)*cell,y=from.y+float(iy+dy)*cell;const auto fz=floorAt(x,y,cur.p.z-lift);
   if(!fz||std::fabs(*fz+lift-cur.p.z)>32)continue;
   const Vec3 p{x,y,*fz+lift};const auto nk=key(ix+dx,iy+dy,p.z);auto it=nodes.find(nk);if(it!=nodes.end()&&it->second.closed)continue;
   const float g=cur.g+std::hypot(flat(cur.p,p),p.z-cur.p.z);if(it!=nodes.end()&&it->second.g<=g)continue;if(!clear(cur.p,p))continue;
   nodes[nk]={p,g,k,int(ix+dx),int(iy+dy)};open.push({g+togo(p),nk});}
 }
 if(found<0)found=fallback;
 if(found<0){why="no hull path; nearest reachable cell ("+std::to_string(int(nearestP.x))+","+std::to_string(int(nearestP.y))+","+std::to_string(int(nearestP.z))+") is "+std::to_string(int(nearestD))+" from the goal ("+std::to_string(nodes.size())+" cells)";return {};}
 std::vector<Vec3> cells;for(auto k=found;k>=0;k=nodes[k].parent)cells.push_back(nodes[k].p);std::reverse(cells.begin(),cells.end());
 std::vector<float> out;for(std::size_t i=0;i+1<cells.size();){auto j=cells.size()-1;while(j>i+1&&!clear(cells[i],cells[j]))--j;out.push_back(cells[j].x);out.push_back(cells[j].y);i=j;}
 out.push_back(goal.x);out.push_back(goal.y); // final leg: the goal itself (the last cell is within RADIUS of it, or a clear sweep away)
 return out;
}
}
struct Scenario::Impl {
 std::filesystem::path file,output;std::vector<Step> steps;std::size_t pc=0;long progress=0;bool aborted=false,realtime=true,livePad=false;std::uint32_t seed=0;
 std::string text; // "type": SDL text input delivered with the next applied frame
 double best=0;long lastImprove=0;int escape=0,escapes=0,replans=0;std::size_t leg=0; // walk-to/route progress
 int passed=0,failed=0,skipped=0;std::ostringstream log;std::map<std::string,Val> stored;
 std::bitset<SDL_NUM_SCANCODES> keys,priorKeys;std::bitset<8> mouse,priorMouse;float mouseX=0,mouseY=0,priorX=0,priorY=0;
 std::array<std::bitset<32>,2> joy,priorJoy;std::array<std::array<float,6>,2> axes{};std::array<bool,2> joyUsed{};
 std::array<std::bitset<SDL_CONTROLLER_BUTTON_MAX>,Input::Pads> pad,priorPad;std::array<std::array<float,SDL_CONTROLLER_AXIS_MAX>,Input::Pads> padAxes{};std::array<bool,Input::Pads> padUsed{};
 // Trailing "timeout N" (frames) applies to wait/press-until.
 static long timeout(std::vector<std::string>& w,int line,long fallback){if(w.size()>=2&&w[w.size()-2]=="timeout"){auto n=number(w.back(),line);w.resize(w.size()-2);return n;}return fallback;}
 void add(Step s){steps.push_back(std::move(s));}
 void press(int key,int line,const std::string& source,long hold){add({Op::Key,line,source,{},{},key,0,0,0,0,true});add({Op::Frames,line,source,{},{},0,0,hold});add({Op::Key,line,source,{},{},key,0,0,0,0,false});add({Op::Frames,line,source,{},{},0,0,3});}
 void parse(const std::string& raw,int line){
  auto text=raw.substr(0,raw.find('#'));auto w=words(text);if(w.empty())return;auto& c=w[0];auto need=[&](std::size_t n){if(w.size()<n)throw std::runtime_error("scenario line "+std::to_string(line)+": too few arguments for "+c);};
  Step s{Op::Frames,line,join(w,0,w.size())};
  if(c=="frames"){need(2);s.count=number(w[1],line);}
  else if(c=="down"||c=="up"){need(2);s.op=Op::Key;s.key=scancode(w[1],line);s.down=c=="down";}
  else if(c=="press"){need(2);press(scancode(w[1],line),line,s.source,w.size()>2?number(w[2],line):3);return;}
  else if(c=="mouse"){need(3);s.op=Op::Mouse;s.x=float(number(w[1],line));s.y=float(number(w[2],line));}
  else if(c=="mouse-down"||c=="mouse-up"){need(2);s.op=Op::MouseButton;s.key=int(number(w[1],line));s.down=c=="mouse-down";if(s.key<1||s.key>7)throw std::runtime_error("scenario line "+std::to_string(line)+": mouse button 1..7");}
  else if(c=="joy"){need(5);s.index=int(number(w[1],line));if(s.index<0||s.index>1)throw std::runtime_error("scenario line "+std::to_string(line)+": joystick 0 or 1");s.key=int(number(w[3],line));
   if(w[2]=="button"){s.op=Op::JoyButton;s.down=w[4]=="down";if(s.key<0||s.key>31||(w[4]!="down"&&w[4]!="up"))throw std::runtime_error("scenario line "+std::to_string(line)+": joy N button 0..31 down|up");}
   else if(w[2]=="axis"){s.op=Op::JoyAxis;s.x=std::stof(w[4]);if(s.key<0||s.key>5||std::fabs(s.x)>1)throw std::runtime_error("scenario line "+std::to_string(line)+": joy N axis 0..5 -1..1");}
   else throw std::runtime_error("scenario line "+std::to_string(line)+": joy N button|axis");}
  else if(c=="press-until"){s.count=timeout(w,line,600);need(3);s.op=Op::PressUntil;s.key=scancode(w[1],line);s.condition=join(w,2,w.size());}
  else if(c=="wait"){s.count=timeout(w,line,600);need(3);if(w[1]!="until")throw std::runtime_error("scenario line "+std::to_string(line)+": wait until CONDITION [timeout N]");s.op=Op::Wait;s.condition=join(w,2,w.size());}
  else if(c=="assert"){need(2);s.op=Op::Assert;s.condition=join(w,1,w.size());}
  // expect-exit [timeout N]: the original scripts end the process (CikisMenu.wdl Exit); must be the last step.
  else if(c=="expect-exit"){s.count=timeout(w,line,600);s.op=Op::ExpectExit;}
  else if(c=="store"){need(3);s.op=Op::Store;s.name=w[1];s.condition=join(w,2,w.size());}
  // set GLOBAL|GLOBAL[i] EXPR: writes a WDL VAR (or VAR array element) as a number, like a script assignment.
  else if(c=="set"){need(3);s.op=Op::Set;s.name=w[1];s.condition=join(w,2,w.size());}
  else if(c=="find"){need(3);s.op=Op::Find;s.name=w[1];s.condition=w[2];}
  // list ACTION: log every live entity running ACTION (id, position, skill1-3, distance) for route discovery.
  // list near R: every live entity within R of the player, with its file and PASSABLE flag (finds action-less blockers).
  else if(c=="list"){need(2);s.op=Op::List;s.condition=w[1];if(w[1]=="near"){need(3);s.radius=std::stof(w[2]);}}
  // walk-to @entity|X Y RADIUS [path] [run] [timeout N]: greedy steering with the default arrow bindings (GenelAyarlar.wdl GA_Tuslar), A held when "run".
  // "path" plans a hull route first (plan() above) and walks it as waypoints, re-planning when stuck.
  // turn-to @entity|X Y [DEGREES=8] [timeout N]: Left/Right only, until the player faces the target.
  else if(c=="walk-to"||c=="turn-to"){const bool turn=c=="turn-to";s.count=timeout(w,line,turn?300:1200);
   while(!turn&&w.size()>3&&(w.back()=="run"||w.back()=="path")){if(w.back()=="run")s.down=true;else s.index=1;w.pop_back();}
   if(turn&&w.size()>=2&&(w.size()==2||(w.size()==3&&w[1].front()!='@')))w.push_back("8");need(3);s.op=turn?Op::TurnTo:Op::WalkTo;
   if(w[1].front()=='@'){if(w.size()!=3)throw std::runtime_error("scenario line "+std::to_string(line)+": "+c+" @name RADIUS");s.name=w[1].substr(1);}
   else{if(w.size()!=4)throw std::runtime_error("scenario line "+std::to_string(line)+": "+c+" X Y RADIUS");s.x=std::stof(w[1]);s.y=std::stof(w[2]);}
   s.radius=std::stof(w.back());}
  // route RADIUS X1 Y1 X2 Y2 ... [run] [timeout N]: walk-to through waypoints; when stuck it re-anchors on the nearest
  // waypoint (behind or ahead), so a random knock-back or a sidestep past an obstacle resumes the route from where the player is.
  else if(c=="route"){s.count=timeout(w,line,3600);if(w.back()=="run"){s.down=true;w.pop_back();}if(w.size()<4||w.size()%2)throw std::runtime_error("scenario line "+std::to_string(line)+": route RADIUS X1 Y1 [X2 Y2 ...] [run] [timeout N]");
   s.op=Op::WalkTo;s.radius=std::stof(w[1]);for(std::size_t i=2;i<w.size();++i)s.path.push_back(std::stof(w[i]));}
  else if(c=="capture"){need(2);s.op=Op::Capture;s.name=w[1];if(s.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")!=std::string::npos||s.name.front()=='.')throw std::runtime_error("scenario line "+std::to_string(line)+": capture name must be a plain file stem");}
  else if(c=="print"){need(2);s.op=Op::Print;s.condition=join(w,1,w.size());}
  // type TEXT: SDL text input (Input::text) for the next frame, e.g. a native console command line before "press Return".
  else if(c=="type"){need(2);s.op=Op::Type;s.name=join(w,1,w.size());}
  // A leading realtime (before any step) also covers script bootstrap (main.cpp media clock).
  else if(c=="realtime"){need(2);s.op=Op::Realtime;s.down=w[1]=="on";if(w[1]!="on"&&w[1]!="off")throw std::runtime_error("scenario line "+std::to_string(line)+": realtime on|off");if(steps.empty()){realtime=s.down;return;}}
  // seed N: applies before the scripts start, wherever it appears (main.cpp).
  else if(c=="seed"){need(2);auto n=number(w[1],line);if(n<0||n>0xffffffffL)throw std::runtime_error("scenario line "+std::to_string(line)+": seed 0..4294967295");seed=std::uint32_t(n);return;}
  // pad live: the renderer's real game controller/joystick input passes through (live pad tests, tools/virtual-gamepad); whole file.
  // pad N down|up|press BUTTON [FRAMES], pad N axis AXIS V: game controller slot N (0/1), SDL names (a, start, dpup, leftx, righttrigger…).
  else if(c=="pad"){need(2);if(w[1]=="live"){livePad=true;return;}
   auto bad=[&]{return std::runtime_error("scenario line "+std::to_string(line)+": pad live | pad N down|up|press BUTTON [FRAMES] | pad N axis AXIS -1..1");};
   need(4);s.index=int(number(w[1],line));if(s.index<0||s.index>=Input::Pads)throw std::runtime_error("scenario line "+std::to_string(line)+": pad 0 or 1");
   if(w[2]=="axis"){need(5);s.op=Op::PadAxis;s.key=SDL_GameControllerGetAxisFromString(w[3].c_str());s.x=std::stof(w[4]);if(s.key==SDL_CONTROLLER_AXIS_INVALID||std::fabs(s.x)>1)throw bad();}
   else{s.op=Op::PadButton;s.key=SDL_GameControllerGetButtonFromString(w[3].c_str());if(s.key==SDL_CONTROLLER_BUTTON_INVALID)throw bad();
    if(w[2]=="press"){s.down=true;add(s);add({Op::Frames,line,s.source,{},{},0,0,w.size()>4?number(w[4],line):3});s.down=false;add(s);add({Op::Frames,line,s.source,{},{},0,0,3});return;}
    if(w[2]!="down"&&w[2]!="up")throw bad();s.down=w[2]=="down";}}
  // Drives the original YukleMenu (YukleMenu.wdl:169-343) from the main menu with ordinary keys; slot N reads kayitN.sav.
  else if(c=="load-save"){need(2);auto n=std::to_string(number(w[1],line));
   for(auto expanded:{"wait until ParlaklikMenuAcik == 1 || AMP_OyunYukle.visible == 1 timeout 7200","press-until Return ParlaklikMenuAcik == 0 timeout 600","wait until AMP_OyunYukle.visible == 1 timeout 1200","press-until Down AM_Cursor == 1 timeout 600","press-until Return YUMP_Cursor.visible == 1 timeout 600"})parse(expanded,line);
   parse("press-until Down YUM_Cursor+YUM_Kafa >= "+n+" timeout 3600",line);parse("press-until Up YUM_Cursor+YUM_Kafa <= "+n+" timeout 3600",line);parse("assert YUM_Cursor+YUM_Kafa == "+n,line);parse("press-until Return YUMP_Cursor.visible == 0 timeout 600",line);return;}
  else throw std::runtime_error("scenario line "+std::to_string(line)+": unknown command '"+c+"'");
  add(std::move(s));
 }
 void report(const Step& s,const char* result,std::uint64_t frame,const std::string& detail={}){log.str("");log<<"scenario: line "<<s.line<<' '<<result<<" frame="<<frame<<' '<<s.source;if(!detail.empty())log<<" -- "<<detail;std::fprintf(stdout,"%s\n",log.str().c_str());std::fflush(stdout);}
 void next(){++pc;progress=0;escape=0;leg=0;replans=0;}
 void apply(Input& in){
  in.keys=keys;in.pressed=keys&~priorKeys;in.released=priorKeys&~keys;priorKeys=keys;
  in.mouseButtons=mouse;in.mousePressed=mouse&~priorMouse;in.mouseReleased=priorMouse&~mouse;priorMouse=mouse;
  in.mouseX=mouseX;in.mouseY=mouseY;in.mouseDX=mouseX-priorX;in.mouseDY=mouseY-priorY;priorX=mouseX;priorY=mouseY;in.wheelX=in.wheelY=0;in.text=std::move(text);text.clear();
  if(livePad)return;
  for(int p=0;p<Input::Pads;++p){in.controllerConnected[p]=padUsed[p];in.controllerButtons[p]=pad[p];in.controllerPressed[p]=pad[p]&~priorPad[p];in.controllerReleased[p]=priorPad[p]&~pad[p];priorPad[p]=pad[p];in.controllerAxes[p]=padAxes[p];}
  for(int j=0;j<2;++j){in.joystickConnected[j]=joyUsed[j];in.joystickButtons[j]=joy[j];in.joystickPressed[j]=joy[j]&~priorJoy[j];in.joystickReleased[j]=priorJoy[j]&~joy[j];priorJoy[j]=joy[j];in.joystickAxes[j]=axes[j];}
 }
};
Scenario::Scenario(const std::filesystem::path& file,std::filesystem::path output):impl_(std::make_unique<Impl>()){
 impl_->file=file;impl_->output=std::move(output);std::ifstream in(file);if(!in)throw std::runtime_error("cannot read scenario "+file.string());
 int line=0;for(std::string text;std::getline(in,text);){++line;if(!text.empty()&&text.back()=='\r')text.pop_back();impl_->parse(text,line);}
 if(impl_->steps.empty())throw std::runtime_error("scenario has no steps: "+file.string());
 std::filesystem::create_directories(impl_->output);
}
Scenario::~Scenario()=default;
bool Scenario::realtime()const{return impl_->realtime;}
std::uint32_t Scenario::seed()const{return impl_->seed;}
bool Scenario::step(Engine& engine,Runtime& runtime,Renderer* renderer,Input& in,std::uint64_t frame){
 auto& s=*impl_;
 auto eval=[&](const std::string& text){return Eval{engine,runtime,s.stored,frame,text};};
 // Condition errors (unknown global, bad syntax) fail the step; waits abort because later steps depend on them.
 auto check=[&](const Step& step,bool& result)->bool{try{result=eval(step.condition).condition();return true;}catch(const std::exception& e){++s.failed;s.report(step,"FAIL",frame,e.what());return false;}};
 while(!s.aborted&&s.pc<s.steps.size()){
  auto& step=s.steps[s.pc];
  switch(step.op){
   case Op::Frames:if(s.progress++<step.count){s.apply(in);return true;}s.next();break;
   case Op::Key:s.keys.set(std::size_t(step.key),step.down);s.next();break;
   case Op::Mouse:s.mouseX=step.x;s.mouseY=step.y;s.next();break;
   case Op::MouseButton:s.mouse.set(std::size_t(step.key),step.down);s.next();break;
   case Op::Type:s.text+=step.name;s.next();break;
   case Op::JoyButton:s.joyUsed[step.index]=true;s.joy[step.index].set(std::size_t(step.key),step.down);s.next();break;
   case Op::JoyAxis:s.joyUsed[step.index]=true;s.axes[step.index][step.key]=step.x;s.next();break;
   case Op::PadButton:s.padUsed[step.index]=true;s.pad[step.index].set(std::size_t(step.key),step.down);s.next();break;
   case Op::PadAxis:s.padUsed[step.index]=true;s.padAxes[step.index][step.key]=step.x;s.next();break;
   case Op::Realtime:s.realtime=step.down;s.next();break;
   case Op::Wait:case Op::PressUntil:{
    // press-until: check, hold 3 frames, release 3 frames, repeat.
    const bool press=step.op==Op::PressUntil;const long phase=press?s.progress%6:0;
    if(phase==0){bool ok=false;if(!check(step,ok)){s.aborted=true;break;}
     if(ok){++s.passed;s.report(step,"PASS",frame,"after "+std::to_string(s.progress)+" frames");s.next();break;}
     if(s.progress>=step.count){++s.failed;s.report(step,"FAIL",frame,"timeout after "+std::to_string(s.progress)+" frames");s.aborted=true;break;}
     if(press)s.keys.set(std::size_t(step.key));}
    if(press&&phase==3)s.keys.reset(std::size_t(step.key));
    ++s.progress;s.apply(in);return true;}
   case Op::Assert:{bool ok=false;if(check(step,ok)){if(ok)++s.passed;else ++s.failed;s.report(step,ok?"PASS":"FAIL",frame);}s.next();break;}
   case Op::Store:case Op::Print:try{auto v=eval(step.condition).value();if(step.op==Op::Store)s.stored[step.name]=v;s.report(step,"INFO",frame,show(v));}catch(const std::exception& e){++s.failed;s.report(step,"FAIL",frame,e.what());}s.next();break;
   case Op::Set:try{
    auto v=eval(step.condition).value();if(v.text)throw std::runtime_error("set takes a number");
    auto name=step.name;std::optional<std::size_t> index;if(auto b=name.find('[');b!=std::string::npos){if(name.back()!=']')throw std::runtime_error("expected NAME[i]");index=number(name.substr(b+1,name.size()-b-2),step.line);name.resize(b);}
    auto it=engine.state.globals.find(name);if(it==engine.state.globals.end())throw std::runtime_error("unknown WDL global "+name);
    if(!index){if(it->second.value.kind!=Value::Kind::Number)throw std::runtime_error(name+" is not a number");it->second.value=Value::from_double(v.number);}
    else{auto* o=it->second.value.kind==Value::Kind::Object?engine.state.get(it->second.value.bits):nullptr;if(!o||*index>=o->array.size())throw std::runtime_error(step.name+" is not an array element");o->array[*index].value=Value::from_double(v.number);}
    ++s.passed;s.report(step,"PASS",frame,show(v));}catch(const std::exception& e){++s.failed;s.report(step,"FAIL",frame,e.what());}s.next();break;
   case Op::WalkTo:case Op::TurnTo:{
    Eval at{engine,runtime,s.stored,frame,{}};const auto up=SDL_SCANCODE_UP,left=SDL_SCANCODE_LEFT,right=SDL_SCANCODE_RIGHT,run=SDL_SCANCODE_A;
    auto release=[&]{for(auto k:{up,left,right,run})s.keys.reset(k);};
    Vec3 goal{step.x,step.y,0};
    if(!step.name.empty()){auto it=s.stored.find(step.name);if(it==s.stored.end()||it->second.text){++s.failed;s.report(step,"FAIL",frame,"unknown stored entity @"+step.name);s.aborted=true;break;}goal=engine.position(ObjectId(it->second.number));}
    if(!at.player()){++s.failed;s.report(step,"FAIL",frame,"no player");s.aborted=true;break;}
    const auto pos=engine.position(at.player());
    const auto where=" at ("+std::to_string(int(pos.x))+","+std::to_string(int(pos.y))+","+std::to_string(int(pos.z))+")";
    const bool planned=step.index==1,turn=step.op==Op::TurnTo;
    auto replan=[&]{std::string why;step.path=plan(engine,at.player(),pos,goal,step.radius,!step.name.empty(),why);s.leg=0;s.best=INFINITY;s.lastImprove=s.progress;
     if(step.path.empty()){++s.failed;s.report(step,"FAIL",frame,why+where);s.aborted=true;return false;}
     std::string via;for(std::size_t i=0;i+1<step.path.size();i+=2)via+=" ("+std::to_string(int(step.path[i]))+","+std::to_string(int(step.path[i+1]))+")";
     s.report(step,"INFO",frame,"planned "+std::to_string(step.path.size()/2)+" waypoints"+where+" via"+via);return true;};
    if(planned&&s.progress==0&&!replan())break;
    const bool last=step.path.empty()||2*(s.leg+1)>=step.path.size();
    Vec3 target=goal;if(!step.path.empty()&&!(last&&!step.name.empty()))target={step.path[2*s.leg],step.path[2*s.leg+1],0};
    target.z=pos.z;const double d=at.dist(target),f=at.facing(target);
    const bool done=turn?std::fabs(f)<step.radius:d<(planned&&!last?16:step.radius);const double band=turn?step.radius:10;
    if(done&&!last){++s.leg;s.best=INFINITY;s.lastImprove=s.progress;s.report(step,"INFO",frame,"leg "+std::to_string(s.leg)+where);++s.progress;s.apply(in);return true;}
    if(done){release();++s.passed;s.report(step,"PASS",frame,"after "+std::to_string(s.progress)+" frames"+where);s.next();break;}
    if(s.progress>=step.count){release();++s.failed;s.report(step,"FAIL",frame,"timeout, distance "+std::to_string(int(d))+" facing "+std::to_string(int(f))+where);s.aborted=true;break;}
    // ponytail: greedy steering plus a blind sidestep (alternating sides) after 40 frames without 2 units of progress; "path" re-plans (5x max), routes re-anchor.
    // Only frames with Up held count as "no progress" (Inzar turns in place slowly), and normal steering gets its 40 frames after each sidestep.
    // The sidestep starts with 10 frames of no keys: Inzar's action (koruyucu.wdl) can stall in a stance while a key stays held (frame and
    // position frozen until Up is released), so a stuck body first gets every key released.
    // Frame counts are 1/60 s frames, scaled to the step (fps_limit), so a turn in place is not taken for "stuck" at high rates.
    const auto frames60=[&](double n){return long(std::lround(n/(60*in.dt)));};
    if(!turn){if(s.progress==0||d<s.best-2){s.best=d;s.lastImprove=s.progress;}if(!s.escape&&s.progress-s.lastImprove>frames60(40)){s.escape=int(frames60(55));++s.escapes;s.lastImprove=s.progress;
     if(planned){if(s.replans++<5&&!replan())break;}
     else if(!step.path.empty()){auto nearest=s.leg;for(std::size_t i=0;2*i<step.path.size();++i)if(at.dist(Vec3{step.path[2*i],step.path[2*i+1],pos.z})<at.dist(Vec3{step.path[2*nearest],step.path[2*nearest+1],pos.z}))nearest=i;if(nearest!=s.leg){s.leg=nearest;s.best=INFINITY;s.report(step,"INFO",frame,"stuck, re-anchored on leg "+std::to_string(s.leg)+where);}}}}
    if(s.escape){if(!--s.escape)s.lastImprove=s.progress;const bool idle=s.escape>=frames60(45),steer=!idle&&s.escape>=frames60(30),side=s.escapes%2;s.keys.set(left,steer&&side);s.keys.set(right,steer&&!side);s.keys.set(up,!idle);s.keys.set(run,!idle&&step.down);}
    else{s.keys.set(left,f>band);s.keys.set(right,f< -band);s.keys.set(up,!turn&&std::fabs(f)<60);s.keys.set(run,!turn&&step.down);if(!s.keys[up])s.lastImprove=s.progress;}
    ++s.progress;s.apply(in);return true;}
   case Op::Find:{
    // find NAME enemy: the nearest visible live Dusmanmi (SKILL11 1, klasik_yara.wdl:61) with Enerji (SKILL1) above 0.
    const bool enemy=step.condition=="enemy";auto action=enemy?FunctionId{}:runtime.function(step.condition);ObjectId best=0;double bestDistance=0;Eval near{engine,runtime,s.stored,frame,{}};
    for(const auto& [id,data]:engine.entities){const auto* o=engine.state.get(id);
     if(enemy?!(o&&o->alive&&id!=near.player()&&engine.field(id,"skill11")==1&&engine.field(id,"skill1")>0&&engine.field(id,"invisible")==0):!runs(engine,id,action))continue;
     auto d=near.player()?near.dist(id):0;if(!best||d<bestDistance){best=id;bestDistance=d;}}
    if(!best){++s.failed;s.report(step,"FAIL",frame,enemy?"no live enemy":action?"no live entity runs this action":"unknown action");s.aborted=true;break;}
    s.stored[step.name]={false,double(best),{}};++s.passed;s.report(step,"PASS",frame,"entity "+std::to_string(best)+" at distance "+std::to_string(int(bestDistance)));s.next();break;}
   case Op::List:{
    const bool nearby=step.condition=="near";auto action=nearby?FunctionId{}:runtime.function(step.condition);Eval near{engine,runtime,s.stored,frame,{}};std::ostringstream o;int n=0;
    for(const auto& [id,data]:engine.entities){const auto* obj=engine.state.get(id);const double d=near.player()?near.dist(id):0;if(nearby?!(obj&&obj->alive&&id!=near.player()&&d<step.radius):!runs(engine,id,action))continue;auto p=engine.position(id);o<<" ["<<id<<" ("<<int(p.x)<<','<<int(p.y)<<','<<int(p.z)<<")";if(nearby){o<<' '<<data.file;for(auto f:{"passable","polygon","narrow","fat","min_x","max_x","min_y","max_y","min_z","max_z","pan","tilt","roll","invisible","transparent","overlay","bright","flare","unlit","oriented","alpha","ambient","floor_dist"})o<<' '<<f<<'='<<engine.field(id,f);}else o<<" s1="<<engine.field(id,"skill1")<<" s2="<<engine.field(id,"skill2")<<" s3="<<engine.field(id,"skill3");o<<" d="<<int(d)<<']';++n;}
    s.report(step,"INFO",frame,std::to_string(n)+o.str());s.next();break;}
   case Op::ExpectExit:if(s.progress++<step.count){s.apply(in);return true;}++s.failed;s.report(step,"FAIL",frame,"engine still running");s.aborted=true;break;
   case Op::Capture:
    if(!renderer||frame==0){++s.skipped;s.report(step,"SKIP",frame,renderer?"no completed frame":"headless");}
    else{auto path=(s.output/(step.name+".ppm")).string();renderer->capture(path);++s.passed;s.report(step,"PASS",frame,path);}
    s.next();break;
  }
 }
 s.apply(in);return false;
}
int Scenario::finish(std::ostream& out,std::uint64_t frames,bool quit){
 auto& s=*impl_;
 // The exit may land inside the key/frame steps of the press that triggered it.
 if(quit&&!s.aborted&&!s.steps.empty()&&s.steps.back().op==Op::ExpectExit&&std::all_of(s.steps.begin()+std::ptrdiff_t(std::min(s.pc,s.steps.size())),s.steps.end()-1,[](const Step& x){return x.op==Op::Frames||x.op==Op::Key;})){++s.passed;s.report(s.steps.back(),"PASS",frames,"engine exited");s.pc=s.steps.size();}
 const auto notRun=s.aborted?s.steps.size()-s.pc:s.pc<s.steps.size()?s.steps.size()-s.pc:0;const bool ok=!s.failed&&!notRun;
 out<<"scenario: "<<s.file.filename().string()<<' '<<(ok?"PASS":"FAIL")<<" passed="<<s.passed<<" failed="<<s.failed<<" skipped="<<s.skipped<<" not_run="<<notRun<<" frames="<<frames<<'\n';
 return ok?0:1;
}
}
