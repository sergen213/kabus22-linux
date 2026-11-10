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
#include <unordered_map>
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
 // Copies and moves are the interpreter's inner loop; almost every Value is unmanaged.
 [[gnu::always_inline]] constexpr Value(const Value& v):kind(v.kind),bits(v.bits),managed(v.managed){if(managed)[[unlikely]]retain_payload(managed);}
 [[gnu::always_inline]] constexpr Value(Value&& v)noexcept:kind(v.kind),bits(v.bits),managed(v.managed){v.kind=Kind::Number;v.bits=0;v.managed=nullptr;}
 [[gnu::always_inline]] constexpr ~Value(){if(managed)[[unlikely]]release_payload(managed);}
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
// ASCII case-insensitive equality; same result as !CaseLess(a,b)&&!CaseLess(b,a) in one pass.
inline bool case_equal(std::string_view a,std::string_view b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i){auto lower=[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;};if(lower(a[i])!=lower(b[i]))return false;}return true;}
struct CaseHash {using is_transparent=void;std::size_t operator()(std::string_view s)const{std::size_t h=14695981039346656037ull;for(unsigned char c:s){h^=c>='A'&&c<='Z'?c+32:c;h*=1099511628211ull;}return h;}};
struct CaseEqual {using is_transparent=void;bool operator()(std::string_view a,std::string_view b)const{return case_equal(a,b);}};
// Interned identifier: ASCII case-folded, process-wide, never freed; 0 is "none". Globals, object fields and the interpreter key by
// Sym instead of hashing and comparing strings per access. Ids depend on first use: never store or hash them as state.
using Sym=std::uint32_t;
class Symbols {
public:
 static Sym intern(std::string_view n){auto& t=table();const auto h=CaseHash{}(n);for(std::size_t i=h&t.mask;;i=(i+1)&t.mask){const Sym s=t.slots[i];if(!s)break;if(t.hashes[s]==h&&case_equal(t.names[s],n))return s;}return t.add(n,h);}
 static Sym find(std::string_view n){auto& t=table();const auto h=CaseHash{}(n);for(std::size_t i=h&t.mask;;i=(i+1)&t.mask){const Sym s=t.slots[i];if(!s)return 0;if(t.hashes[s]==h&&case_equal(t.names[s],n))return s;}}
 static std::string_view name(Sym s){return table().names[s];} // lower case; valid for the process lifetime
 static std::uint64_t hash(Sym s){return table().hashes[s];}   // CaseHash of the name
private:
 struct Table {
  std::vector<Sym> slots=std::vector<Sym>(4096);std::size_t mask=4095;std::vector<std::string_view> names{std::string_view{}};std::vector<std::size_t> hashes{0};std::deque<std::string> storage;
  Sym add(std::string_view n,std::size_t h){
   if(names.size()>=std::numeric_limits<Sym>::max())throw std::runtime_error("WDL identifier space exhausted");
   auto& text=storage.emplace_back(n);for(auto& c:text)if(c>='A'&&c<='Z')c=char(c+32);
   const Sym s=Sym(names.size());names.push_back(text);hashes.push_back(h);
   if(names.size()*2>slots.size()){slots.assign(slots.size()*2,0);mask=slots.size()-1;for(Sym k=1;k<names.size();++k){std::size_t i=hashes[k]&mask;while(slots[i])i=(i+1)&mask;slots[i]=k;}}
   else {std::size_t i=h&mask;while(slots[i])i=(i+1)&mask;slots[i]=s;}
   return s;
  }
 };
 static Table& table(){static Table t;return t;}
};
// "name"_sym: the Sym of a literal, interned once at start-up.
template<std::size_t N> struct SymbolLiteral {char text[N]{};constexpr SymbolLiteral(const char(&s)[N]){std::copy_n(s,N,text);}};
template<SymbolLiteral L> inline const Sym symbolOf=Symbols::intern(std::string_view(L.text,sizeof L.text-1));
template<SymbolLiteral L> inline Sym operator""_sym(){return symbolOf<L>;}
// Name -> Slot, keyed by Sym. Slots live in fixed chunks, so their addresses stay stable until clear() (Refs hold Slot*); the index
// is open addressing over (sym,position). Entries cannot be erased one by one. Iteration is in insertion order; nothing may depend
// on it. A caller that reads the same name from many objects can pass a position hint: objects built by the same code hold their
// fields in the same order, so the hint usually names the entry without touching the index. Iterators yield {first: the interned
// (lower-case) name, second: the Slot}.
class Fields {
public:
 struct Entry {Slot second;Sym sym=0;std::string_view name()const{return Symbols::name(sym);}}; // name(): interned, lower case
 template<bool Const> class Iterator {
 public:
  using Owner=std::conditional_t<Const,const Fields,Fields>;
  struct Item {std::string_view first;std::conditional_t<Const,const Slot&,Slot&> second;};
  struct Arrow {Item item;const Item* operator->()const{return &item;}};
  Iterator(Owner* o,std::uint32_t i):owner_(o),index_(i){}
  template<bool Other,typename=std::enable_if_t<Const&&!Other>> Iterator(const Iterator<Other>& o):owner_(o.owner_),index_(o.index_){}
  Item operator*()const{auto& e=owner_->entry(index_);return {e.name(),e.second};}Arrow operator->()const{return {**this};}
  Sym sym()const{return owner_->entry(index_).sym;}
  Iterator& operator++(){++index_;return *this;}
  template<bool Other> bool operator==(const Iterator<Other>& o)const{return index_==o.index_;}
 private:
  template<bool> friend class Iterator;Owner* owner_;std::uint32_t index_;
 };
 using iterator=Iterator<false>;using const_iterator=Iterator<true>;
 Fields()=default;Fields(const Fields&)=delete;Fields& operator=(const Fields&)=delete;Fields(Fields&&)=default;Fields& operator=(Fields&&)=default;
 iterator begin(){return {this,0};}iterator end(){return {this,size_};}const_iterator begin()const{return {this,0};}const_iterator end()const{return {this,size_};}
 std::size_t size()const{return size_;}bool empty()const{return !size_;}
 Slot* get(Sym s){const auto i=position(s);return i==None?nullptr:&entry(i).second;}
 const Slot* get(Sym s)const{const auto i=position(s);return i==None?nullptr:&entry(i).second;}
 Slot& operator[](Sym s){const auto i=position(s);return i==None?insert(s).second:entry(i).second;}
 Entry& at(Sym s){const auto i=position(s);return i==None?insert(s):entry(i);} // created when absent, like operator[]
 Entry& at(Sym s,std::uint32_t& hint){if(hint<size_){auto& e=entry(hint);if(e.sym==s)return e;}auto i=position(s);if(i==None){i=size_;insert(s);}hint=i;return entry(i);}
 const Slot* get(Sym s,std::uint32_t& hint)const{if(hint<size_){const auto& e=entry(hint);if(e.sym==s)return &e.second;}const auto i=position(s);if(i==None)return nullptr;hint=i;return &entry(i).second;}
 iterator find(Sym s){const auto i=position(s);return {this,i==None?size_:i};}const_iterator find(Sym s)const{const auto i=position(s);return {this,i==None?size_:i};}
 // A name that was never interned cannot be a key.
 iterator find(std::string_view n){const Sym s=Symbols::find(n);return s?find(s):end();}const_iterator find(std::string_view n)const{const Sym s=Symbols::find(n);return s?find(s):end();}
 bool contains(Sym s)const{return position(s)!=None;}bool contains(std::string_view n)const{const Sym s=Symbols::find(n);return s&&position(s)!=None;}
 std::pair<iterator,bool> try_emplace(std::string_view n){const Sym s=Symbols::intern(n);auto i=position(s);if(i!=None)return {{this,i},false};insert(s);return {{this,size_-1},true};}
 void clear(){for(std::uint32_t i=0;i<size_;++i)entry(i).second=Slot{};if(size_)std::fill_n(index_.get(),std::size_t(mask_)+1,std::uint64_t{0});size_=0;}
private:
 static constexpr std::uint32_t None=~std::uint32_t{0},Chunk=32;
 Entry& entry(std::uint32_t i){return i<Chunk?first_[i]:more_[i/Chunk-1][i%Chunk];}const Entry& entry(std::uint32_t i)const{return i<Chunk?first_[i]:more_[i/Chunk-1][i%Chunk];}
 std::uint32_t home(Sym s)const{return (s*0x9E3779B1u)>>shift_;}
 std::uint32_t position(Sym s)const{if(!size_)return None;for(std::uint32_t i=home(s);;i=(i+1)&mask_){const auto v=index_[i];if(!v)return None;if(Sym(v>>32)==s)return std::uint32_t(v)-1;}}
 void place(Sym s,std::uint32_t at){std::uint32_t i=home(s);while(index_[i])i=(i+1)&mask_;index_[i]=(std::uint64_t(s)<<32)|(at+1);}
 Entry& insert(Sym s){
  if(!s)throw std::runtime_error("empty WDL identifier");
  if(!index_||(size_+1)*2>mask_+1){const std::uint32_t capacity=index_?(mask_+1)*2:8;index_=std::make_unique<std::uint64_t[]>(capacity);mask_=capacity-1;shift_=std::uint32_t(32-std::countr_zero(capacity));for(std::uint32_t i=0;i<size_;++i)place(entry(i).sym,i);}
  if(!first_)first_=std::make_unique<Entry[]>(Chunk);else if(size_==(more_.size()+1)*Chunk)more_.push_back(std::make_unique<Entry[]>(Chunk));
  auto& e=entry(size_);e.sym=s;place(s,size_);++size_;return e;
 }
 std::unique_ptr<Entry[]> first_;std::vector<std::unique_ptr<Entry[]>> more_;std::unique_ptr<std::uint64_t[]> index_;std::uint32_t mask_=0,shift_=0,size_=0;
};
// One name read from many objects in a loop: keeps the position hint between objects.
struct FieldSite {
 Sym sym;std::uint32_t hint=~std::uint32_t{0};
 Slot& operator()(Fields& f){return f.at(sym,hint).second;}            // created when absent
 const Slot* find(const Fields& f){return f.get(sym,hint);}             // null when absent
};
struct Object {
 enum class Kind { Name,String,Vector,Bmap,Sound,Font,Entity,Panel,Text,View,Material,Window,Particle,NativeText };
 Kind kind=Kind::Name;ObjectId id=0;std::string name;bool alive=true;Fields fields;std::deque<Slot> array;std::string text;std::vector<Property> properties;Slot* scalar=nullptr;
 State* state=nullptr;std::size_t retained=0;bool retired=false,managedString=false,nullStringBuffer=false;
 // Process-local storage lease (Runtime): owners = processes holding it, references = reference bindings into it.
 std::uint32_t leaseOwners=0,leaseReferences=0;bool leased=false,leaseAvailable=false,leaseScalar=false;
 Slot& field(Sym n){return fields[n];}
 Slot& field(std::string_view n){return fields[Symbols::intern(n)];}
};
struct State {
 SemanticProfile profile=SemanticProfile::CampaignA6;
 Fields globals;std::deque<Object> objects;std::unordered_map<std::string,ObjectId,CaseHash,CaseEqual> numericGlobals;std::map<std::string,std::string,CaseLess> compilerMacros;
 // Handles never alias later births; only this pointer-sized tombstone table grows with history.
 std::vector<Object*> handles,freePayloads;
 std::vector<ObjectId> uiCandidates;
 // Native numeric header binder 004174f0 shares zero-initialized slot0065f1e4 at WARN_LEVEL<2.
 Slot numericBindingFallback{};
 bool destroying=false;
 ~State(){destroying=true;globals.clear();for(auto& o:objects){o.fields.clear();o.array.clear();o.properties.clear();}}
 State()=default;State(const State&)=delete;State& operator=(const State&)=delete;
 Slot& global(Sym n){return globals[n];}
 Slot& global(std::string_view n){return globals[Symbols::intern(n)];}
 // hint: optional per-call-site field position cache (Fields::at), any value.
 Ref member(Object&,Sym,std::uint32_t* hint=nullptr);
 Ref member(ObjectId id,Sym n){return member(require(id),n);}
 Ref member(ObjectId id,std::string_view n){return member(id,Symbols::intern(n));}
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
  for(const auto& entry:o->fields)if(ref.slot==&entry.second)return true;
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
  handles[o.id-1]=nullptr;o.id=0;o.scalar=nullptr;o.fields.clear();o.array.clear();o.leased=false;
  std::string{}.swap(o.name);std::string{}.swap(o.text);std::vector<Property>{}.swap(o.properties);
  freePayloads.push_back(&o);
 }
};
// K22_STATE_DIGEST (docs/GAMEPLAY_TESTING.md): order-sensitive 64-bit combine; sum per-name terms where the container has no order.
inline std::uint64_t digest_mix(std::uint64_t h,std::uint64_t v){h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);h*=0xff51afd7ed558ccdull;h^=h>>33;return h;}
inline std::uint64_t digest_value(std::uint64_t h,const Value& v){return digest_mix(h,(std::uint64_t(v.kind)<<33)|(std::uint64_t(v.managed!=nullptr)<<32)|v.bits);}
inline std::uint64_t digest_text(std::uint64_t h,std::string_view s){std::uint64_t t=14695981039346656037ull;for(unsigned char c:s){t^=c;t*=1099511628211ull;}return digest_mix(digest_mix(h,s.size()),t);}
inline std::uint64_t digest_ref(std::uint64_t h,const Ref& r){return digest_mix(digest_mix(digest_mix(h,(std::uint64_t(r.owner)<<32)|r.bitMask),r.index*2+(r.slot!=nullptr)),CaseHash{}(r.field));}
inline void retain_payload(Object* o){++o->retained;}
inline void release_payload(Object* o){o->state->release_object(o->id);}
inline Value Value::owned(Object& o,Kind kind){Value v{kind,o.id};v.managed=&o;retain_payload(&o);return v;}
// call: the interned name of the native call being dispatched (the builtin's NAME argument is then Symbols::name(call)).
struct ProcessContext {std::uint64_t processId=0;ObjectId my=0,you=0;Sym call=0;};
}
