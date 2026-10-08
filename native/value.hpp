#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace k22 {
using ObjectId=std::uint32_t; using FunctionId=std::uint32_t;
enum class SemanticProfile { CampaignA6,BonusA7 };
struct Object;struct State;
inline void retain_payload(Object*);
inline void release_payload(Object*);
struct Value {
 enum class Kind { Number,Object,Function,NativeText }; Kind kind=Kind::Number; std::uint32_t bits=0;
 // Only dynamic STRING/native byte returns own payloads. State must outlive its Values, like Refs.
 Object* managed=nullptr;
 constexpr Value()=default;
 constexpr Value(Kind k,std::uint32_t b):kind(k),bits(b){}
 constexpr Value(const Value& v):kind(v.kind),bits(v.bits),managed(v.managed){if(managed)retain_payload(managed);}
 constexpr Value(Value&& v)noexcept:kind(v.kind),bits(v.bits),managed(v.managed){v.kind=Kind::Number;v.bits=0;v.managed=nullptr;}
 constexpr ~Value(){if(managed)release_payload(managed);}
 constexpr Value& operator=(const Value& v){if(this!=&v){const auto k=v.kind;const auto b=v.bits;auto* p=v.managed;if(p)retain_payload(p);if(managed)release_payload(managed);kind=k;bits=b;managed=p;}return *this;}
 constexpr Value& operator=(Value&& v)noexcept{if(this!=&v){const auto k=v.kind;const auto b=v.bits;auto* p=v.managed;v.kind=Kind::Number;v.bits=0;v.managed=nullptr;if(managed)release_payload(managed);kind=k;bits=b;managed=p;}return *this;}
 static Value owned(Object&,Kind);
 static constexpr Value from_raw(std::int32_t n){return {Kind::Number,std::bit_cast<std::uint32_t>(n)};}
 static constexpr Value from_int(std::int32_t n){return {Kind::Number,std::uint32_t(n)*1024u};}
 // Legacy conversion truncates toward zero; arithmetic wraps modulo 2^32.
 static Value from_double(double n){const double scaled=std::ldexp(n,10),limit=std::ldexp(1.0,63);if(!std::isfinite(scaled)||scaled< -limit||scaled>=limit)throw std::runtime_error("WDL number outside recovered conversion range");auto raw=static_cast<std::int64_t>(scaled);return from_raw(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(raw)));}
 static constexpr Value object(ObjectId id){return {Kind::Object,id};}
 static constexpr Value function(FunctionId id){return {Kind::Function,id};}
 constexpr std::int32_t raw()const{return std::bit_cast<std::int32_t>(bits);}
 double real()const{return kind==Kind::Number?double(raw())/1024:double(bits);}
 constexpr std::int32_t integer()const{return kind==Kind::Number?raw()/1024:std::int32_t(bits);}
 constexpr explicit operator bool()const{return bits!=0;}
};
struct Slot {Value value;bool callback=false;};
struct Ref {Slot* slot=nullptr;ObjectId owner=0;std::string_view field;std::size_t index=0;std::uint32_t bitMask=0;
 explicit operator bool()const{return slot!=nullptr||owner!=0;}
 Value get()const {if(!slot)return {};return bitMask?Value::from_int((slot->value.bits&bitMask)!=0):slot->value;}
 void set(Value v)const {if(!slot)throw std::runtime_error("invalid WDL reference");if(!bitMask){slot->value=std::move(v);return;}auto n=slot->value.bits;slot->value=Value::from_raw(std::bit_cast<std::int32_t>(v.bits?n|bitMask:n&~bitMask));}
};
struct Argument {Value value;Ref ref;void* evaluatorContext=nullptr;const void* expression=nullptr;Value(*evaluator)(void*,const void*)=nullptr;bool captured=false;Value current()const{return captured?value:evaluator?evaluator(evaluatorContext,expression):ref.slot?ref.get():value;}};
struct Property {std::string name;std::vector<Argument> arguments;};
struct CaseLess {using is_transparent=void;bool operator()(std::string_view a,std::string_view b)const{std::size_t i=0;for(;i<a.size()&&i<b.size();++i){auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;};auto x=lower(a[i]),y=lower(b[i]);if(x!=y)return x<y;}return a.size()<b.size();}};
using Fields=std::map<std::string,Slot,CaseLess>;
struct Object {
 enum class Kind { Name,String,Vector,Bmap,Sound,Font,Entity,Panel,Text,View,Material,Window,Particle,NativeText };
 Kind kind=Kind::Name;ObjectId id=0;std::string name;bool alive=true;Fields fields;std::deque<Slot> array;std::string text;std::vector<Property> properties;Slot* scalar=nullptr;
 State* state=nullptr;std::size_t retained=0;bool retired=false,managedString=false,nullStringBuffer=false;
 Slot& field(std::string_view n){auto it=fields.find(n);if(it!=fields.end())return it->second;return fields.try_emplace(std::string(n)).first->second;}
};
struct State {
 SemanticProfile profile=SemanticProfile::CampaignA6;
 Fields globals;std::deque<Object> objects;std::map<std::string,ObjectId,CaseLess> numericGlobals;std::map<std::string,std::string,CaseLess> compilerMacros;
 // Handles never alias later births; only this pointer-sized tombstone table grows with history.
 std::vector<Object*> handles,freePayloads;
 std::vector<ObjectId> uiCandidates;
 // Native numeric header binder 004174f0 shares zero-initialized slot0065f1e4 at WARN_LEVEL<2.
 Slot numericBindingFallback{};
 bool destroying=false;
 ~State(){destroying=true;globals.clear();for(auto& o:objects){o.fields.clear();o.array.clear();o.properties.clear();}}
 State()=default;State(const State&)=delete;State& operator=(const State&)=delete;
 Slot& global(std::string_view n){auto it=globals.find(n);if(it!=globals.end())return it->second;return globals.try_emplace(std::string(n)).first->second;}
 Ref member(ObjectId,std::string_view);
 Object& create(Object::Kind kind,std::string name={}){
  if(handles.size()==std::numeric_limits<ObjectId>::max())throw std::runtime_error("WDL object handle space exhausted");
  Object* o;if(freePayloads.empty()){objects.emplace_back();o=&objects.back();}else{o=freePayloads.back();freePayloads.pop_back();}
  o->kind=kind;o->id=ObjectId(handles.size()+1);o->name=std::move(name);o->alive=true;o->retired=false;o->managedString=false;o->nullStringBuffer=false;o->state=this;
  // Native VIEW constructor 004210d0 stores raw0x400 at public ASPECT(+0x4c).
  if(kind==Object::Kind::View)o->field("aspect").value=Value::from_int(1);
  // A6 MATERIAL declarations 004159d0 call 00415390; Bonus 10019d20/10019ca0
  // has the same public stores. Unstored fields retain the zero-filled birth state.
  if(kind==Object::Kind::Material){
   for(auto field:{"ambient_blue","ambient_green","ambient_red","diffuse_blue","diffuse_green","diffuse_red","map_blue","map_green","map_red"})o->field(field).value=Value::from_int(255);
   o->field("alpha").value=Value::from_int(100);o->field("albedo").value=Value::from_raw(0xc8df);o->field("cycle").value=Value::from_int(480);
  }
  handles.push_back(o);
  if(kind==Object::Kind::View||kind==Object::Kind::Panel||kind==Object::Kind::Text)uiCandidates.push_back(o->id);
  return *o;
 }
 Object* get(ObjectId id){return id&&id<=handles.size()?handles[id-1]:nullptr;}
 const Object* get(ObjectId id)const{return id&&id<=handles.size()?handles[id-1]:nullptr;}
 Object& require(ObjectId id){auto* o=get(id);if(!o)throw std::runtime_error("invalid WDL object handle");return *o;}
 void retain_object(ObjectId id){if(auto* o=get(id))++o->retained;}
 void release_object(ObjectId id){if(auto* o=get(id)){if(!o->retained)throw std::runtime_error("unbalanced WDL object lease");--o->retained;if(!destroying&&o->retired&&!o->retained)reclaim(*o);}}
 bool owns_reference(const Ref& ref)const {
  const auto* o=get(ref.owner);if(!o||!ref.slot)return false;
  if(ref.slot==o->scalar)return true;
  if(ref.index<o->array.size()&&ref.slot==&o->array[ref.index])return true;
  for(const auto& [name,slot]:o->fields)if(ref.slot==&slot)return true;
  return false;
 }
 bool retain_reference(const Ref& ref){if(!owns_reference(ref))return false;retain_object(ref.owner);return true;}
 void release_reference(const Ref& ref){if(owns_reference(ref))release_object(ref.owner);}
 // Native TXT/foreign returns leak until teardown; host aliases retain their identity without that leak.
 Value make_string(std::string text,bool nativeBytes=false){auto& o=create(nativeBytes?Object::Kind::NativeText:Object::Kind::String);o.text=std::move(text);o.managedString=true;o.retired=true;return Value::owned(o,nativeBytes?Value::Kind::NativeText:Value::Kind::Object);}
 Value object_value(ObjectId id){auto& o=require(id);return o.managedString?Value::owned(o,o.kind==Object::Kind::NativeText?Value::Kind::NativeText:Value::Kind::Object):Value::object(id);}
 void retire(ObjectId id){if(auto* o=get(id)){o->alive=false;o->retired=true;if(o->kind==Object::Kind::View||o->kind==Object::Kind::Panel||o->kind==Object::Kind::Text)std::erase(uiCandidates,id);if(!o->retained)reclaim(*o);}}
private:
 void reclaim(Object& o){
  handles[o.id-1]=nullptr;o.id=0;o.scalar=nullptr;o.fields.clear();o.array.clear();
  std::string{}.swap(o.name);std::string{}.swap(o.text);std::vector<Property>{}.swap(o.properties);
  freePayloads.push_back(&o);
 }
};
inline void retain_payload(Object* o){++o->retained;}
inline void release_payload(Object* o){o->state->release_object(o->id);}
inline Value Value::owned(Object& o,Kind kind){Value v{kind,o.id};v.managed=&o;retain_payload(&o);return v;}
struct ProcessContext {std::uint64_t processId=0;ObjectId my=0,you=0;};
}
