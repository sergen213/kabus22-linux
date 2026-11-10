#include "wdl.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <deque>
#include <limits>
#include <set>
#include <unordered_map>
namespace k22 {
namespace {
#include "native_calls.inc"
bool native_call_name(SemanticProfile profile,std::string_view name){
 auto contains=[&](std::span<const std::string_view> names){return std::binary_search(names.begin(),names.end(),name);};
 auto specific=profile==SemanticProfile::BonusA7?std::span<const std::string_view>(bonusNativeCalls):std::span<const std::string_view>(campaignNativeCalls);
 return contains(commonNativeCalls)||contains(specific);
}
struct Location {std::string file;std::size_t line=1,column=1;std::string str()const{return file+":"+std::to_string(line)+":"+std::to_string(column);}};
struct Token {enum Kind{Word,Number,String,Punct,End} kind=End;std::string text;Location loc;bool syntax(std::string_view s)const{return (kind==Word||kind==Punct)&&text==s;}};
std::string_view paired_operator(char first,char second){static constexpr std::string_view pairs[]={"==","!=","<=",">=","&&","||","++","--","+=","-=","*=","/=","%=","&=","|=","^=","<<",">>","->"};for(auto pair:pairs)if(pair[0]==first&&pair[1]==second)return pair;return {};}
struct WdlError:std::runtime_error{using std::runtime_error::runtime_error;};
[[noreturn]] void fail(const Location& l,std::string_view message){throw WdlError(l.str()+": "+std::string(message));}
bool numeric_head(std::string_view source,std::size_t begin){
 std::size_t end=begin;while(end<source.size()&&(std::isalnum(static_cast<unsigned char>(source[end]))||source[end]=='_'||static_cast<unsigned char>(source[end])>=128))++end;auto head=source.substr(begin,end-begin);if(head.empty())return false;
 if(head.size()>2&&head[0]=='0'&&(head[1]=='x'||head[1]=='X'))return std::all_of(head.begin()+2,head.end(),[](unsigned char c){return std::isxdigit(c);});
 double value=0;auto converted=std::from_chars(head.data(),head.data()+head.size(),value);if(converted.ptr==head.data()+head.size())return true;
 if((head.back()=='e'||head.back()=='E')&&end+1<source.size()&&(source[end]=='+'||source[end]=='-')&&std::isdigit(static_cast<unsigned char>(source[end+1]))){head.remove_suffix(1);if(!head.empty()){converted=std::from_chars(head.data(),head.data()+head.size(),value);return converted.ptr==head.data()+head.size();}}
 return false;
}
bool declaration_type(std::string_view s);
bool filename_position(const std::vector<Token>& tokens){
 if(tokens.empty())return true;
 const auto& previous=tokens.back();
 if(previous.kind==Token::Word){
  if(previous.text=="include"||previous.text=="resource"||previous.text=="return")return true;
  return tokens.size()>1&&tokens[tokens.size()-2].kind==Token::Word&&declaration_type(tokens[tokens.size()-2].text);
 }
 if(previous.kind==Token::Number||previous.kind==Token::String)return false;
 return previous.text!=")"&&previous.text!="]"&&previous.text!="++"&&previous.text!="--";
}
std::vector<Token> lex(std::string_view s,std::string_view file,ParseMetrics& metrics){
 std::vector<Token> out;std::size_t i=0,line=1,col=1;auto advance=[&](){if(s[i++]=='\n'){++line;col=1;}else ++col;};
 while(i<s.size()){
  if(std::isspace(static_cast<unsigned char>(s[i]))){advance();continue;}
  if(s[i]=='/'&&i+1<s.size()&&s[i+1]=='/'){while(i<s.size()&&s[i]!='\n')advance();continue;}
  if(s[i]=='/'&&i+1<s.size()&&s[i+1]=='*'){Location l{std::string(file),line,col};advance();advance();while(i+1<s.size()&&!(s[i]=='*'&&s[i+1]=='/'))advance();if(i+1>=s.size())fail(l,"unterminated comment");advance();advance();continue;}
  Location l{std::string(file),line,col};Token t;t.loc=l;char c=s[i];
  // Native macro lookup matches a complete token, not the stem of an angle filename.
  if(c=='<'&&filename_position(out)){
   auto end=s.find('>',i+1);
   if(end!=std::string_view::npos){
    auto filename=s.substr(i+1,end-i-1);
    if(!filename.empty()&&filename.find_first_of(" \t\r\n\f\v(){}[],;<>\"'=&|!^~")==std::string_view::npos){
     t.kind=Token::Punct;t.text="<";advance();out.push_back(std::move(t));
     Token name{Token::Word,std::string(filename),{std::string(file),line,col}};
     while(i<end)advance();
     out.push_back(std::move(name));out.push_back({Token::Punct,">",{std::string(file),line,col}});advance();continue;
    }
   }
  }
  if(c=='"'||c=='\''){t.kind=Token::String;advance();while(i<s.size()&&s[i]!=c){if(s[i]=='\\'&&i+1<s.size()){auto next=s[i+1];if(next==c||next=='\\'||next=='n'||next=='r'||next=='t'){advance();t.text+=next=='n'?'\n':next=='r'?'\r':next=='t'?'\t':next;advance();continue;}}t.text+=s[i];advance();}if(i==s.size())fail(l,"unterminated string");advance();}
  else if(std::isalpha(static_cast<unsigned char>(c))||c=='_'||static_cast<unsigned char>(c)>=128||(std::isdigit(static_cast<unsigned char>(c))&&!numeric_head(s,i))){t.kind=Token::Word;while(i<s.size()&&(std::isalnum(static_cast<unsigned char>(s[i]))||s[i]=='_'||static_cast<unsigned char>(s[i])>=128)){char letter=s[i];t.text+=letter>='A'&&letter<='Z'?char(letter+32):letter;advance();}}
  else if(std::isdigit(static_cast<unsigned char>(c))||(c=='.'&&i+1<s.size()&&std::isdigit(static_cast<unsigned char>(s[i+1]))&&numeric_head(s,i+1))){t.kind=Token::Number;bool exponent=false;while(i<s.size()){char n=s[i];if(std::isalnum(static_cast<unsigned char>(n))||n=='.'){t.text+=n;advance();exponent=n=='e'||n=='E';}else if((n=='+'||n=='-')&&exponent){t.text+=n;advance();exponent=false;}else break;}}
  else {t.kind=Token::Punct;t.text+=c;advance();if(i<s.size()){auto pair=paired_operator(c,s[i]);if(!pair.empty()){t.text=pair;advance();}}}
  out.push_back(std::move(t));
 }
 metrics.lines+=s.empty()?0:line-(s.back()=='\n'?1:0);metrics.tokens+=out.size();out.push_back({Token::End,"",{std::string(file),line,col}});return out;
}
enum class Op:std::uint8_t{None,Add,Sub,Mul,Div,Mod,Eq,Ne,Lt,Gt,Le,Ge,And,Or,BitAnd,BitOr,BitXor,Shl,Shr,Plus,Neg,Not,BitNot,Increment,Reference};
// kind/op/args are the program. The second line is derived: link() after op is set (identifier, operator), Runtime::Impl::layout for
// names in function code (local/binding slot), and per-site caches filled at evaluation (call kind, global slot).
struct Expr {enum Kind{Literal,Name,Unary,Binary,Call,Member,Index,Conditional,List}kind=Literal;enum Dispatch{Dynamic,Native,Script}dispatch=Dynamic;Location loc;std::string op;Value value;std::vector<std::unique_ptr<Expr>> args;
 enum Resolution:std::uint8_t{Global,Zero,One,Local,Binding};enum Special:std::uint8_t{Unlinked,Ordinary,Wait,Waitt,Sleep,ProcLate,ProcKill};
 Sym sym=0;Op opc=Op::None;bool assign=false,post=false,decrement=false,vectorMember=false;std::uint8_t component=3;Resolution resolution=Global;std::uint32_t slot=0;
 mutable Special special=Unlinked;mutable Slot* global=nullptr;mutable ObjectId numericView=0;mutable std::uint32_t fieldHint=~std::uint32_t{0};mutable std::size_t numericEpoch=0;
};
using E=std::unique_ptr<Expr>;
void link(Expr& e){
 const std::string_view s=e.op;
 if(e.kind==Expr::Name){e.sym=Symbols::intern(s);e.resolution=s=="null"||s=="off"||s=="false"?Expr::Zero:s=="on"||s=="true"?Expr::One:Expr::Global;}
 else if(e.kind==Expr::Member){e.sym=Symbols::intern(s);e.component=s=="x"||s=="pan"?0:s=="y"||s=="tilt"?1:s=="z"||s=="roll"?2:3;e.vectorMember=e.component<3||s=="blue"||s=="green"||s=="red";}
 else if(e.kind==Expr::Unary){e.opc=s=="&"||s=="*"?Op::Reference:s=="+"?Op::Plus:s=="-"?Op::Neg:s=="!"?Op::Not:s=="~"?Op::BitNot:Op::Increment;e.decrement=s=="--"||s=="post--";e.post=s.starts_with("post");}
 else if(e.kind==Expr::Binary){
  e.assign=s=="="||(s.size()==2&&s[1]=='='&&s!="=="&&s!="!="&&s!="<="&&s!=">=");const auto base=e.assign?s.substr(0,1):s;
  e.opc=base=="+"?Op::Add:base=="-"?Op::Sub:base=="*"?Op::Mul:base=="/"?Op::Div:base=="%"?Op::Mod:base=="=="?Op::Eq:base=="!="?Op::Ne:base=="<"?Op::Lt:base==">"?Op::Gt:base=="<="?Op::Le:base==">="?Op::Ge:base=="&&"?Op::And:base=="||"?Op::Or:base=="&"?Op::BitAnd:base=="|"?Op::BitOr:base=="^"?Op::BitXor:base=="<<"?Op::Shl:base==">>"?Op::Shr:Op::None;
 }
}
// The native call NAME() that reroute_reads and friends substitute.
E native_call(const Location& loc,std::string_view name){auto c=std::make_unique<Expr>();c->kind=Expr::Call;c->dispatch=Expr::Native;c->loc=loc;auto n=std::make_unique<Expr>();n->kind=Expr::Name;n->loc=loc;n->op=std::string(name);link(*n);c->args.push_back(std::move(n));return c;}
struct Instruction {enum Kind{Eval,Jump,Branch,Declare,Return,Late}kind=Eval;E expr;std::size_t target=0;std::string name;std::size_t size=0;Location loc;std::uint32_t slot=0;};
struct Parameter {std::string name;bool reference=false;};
// One step of building a process frame, in the order the storage objects are created (object ids are script-visible).
struct FrameStep {enum Kind:std::uint8_t{Value,Reference,Argument,Local}kind=Value;std::uint32_t source=0,slot=0;std::size_t size=3;};
struct Function {std::string name;Location loc;std::vector<Parameter> params;std::vector<std::pair<std::string,std::size_t>> localDefinitions;std::vector<Instruction> code;std::map<std::string,std::size_t,CaseLess> labels;std::vector<std::pair<std::size_t,std::string>> gotos;std::uint32_t schedulerFlags=0;bool starter=false,defined=false,external=false;
 // Frame layout (Runtime::Impl::layout): localSyms[slot] names Process::locals[slot] (0 my, 1 you, then value parameters, _arg1.._arg4,
 // declared locals); bindingSyms[i] names Process::bindings[i] (reference parameters); bindingOrder lists them in CaseLess name order.
 std::vector<Sym> localSyms,bindingSyms;std::vector<std::uint32_t> bindingOrder;std::vector<FrameStep> frame;bool laidOut=false;
};
struct Declaration {std::string type,name;Location loc;bool pointer=false;E size,init;std::vector<std::pair<std::string,std::vector<E>>> properties;ObjectId object=0;Slot* pointerCell=nullptr;};
struct LocalSnapshot {ObjectId object;std::size_t offset,count;};
// locals[slot]: the leased storage object of that local and its value cell &array[0] (stable while leased: the array is never
// emptied); bindings[i]: the reference argument.
struct Local {Object* object=nullptr;Slot* cell=nullptr;};
struct Process {ProcessContext context;std::array<ObjectId,2> retainedContext{};FunctionId function=0;std::uint32_t schedulerFlags=0;std::size_t pc=0;std::vector<Local> locals;std::vector<Argument> bindings;std::vector<ObjectId> ownedLocals;std::vector<LocalSnapshot> snapshots;std::vector<Value> savedLocals;std::int32_t countdown=0;int bank=0;bool yielded=false,cancelled=false,noContinuation=false,running=false;Value result;};
std::string_view function_file_name(const Function& f){std::string_view n=f.loc.file;return n.substr(n.find_last_of("/\\")+1);}
int precedence(std::string_view s){if(s=="="||s=="+="||s=="-="||s=="*="||s=="/="||s=="%="||s=="|="||s=="&="||s=="^=")return 1;if(s=="?")return 2;if(s=="||")return 3;if(s=="&&")return 4;if(s=="|")return 5;if(s=="^")return 6;if(s=="&")return 7;if(s=="=="||s=="!=")return 8;if(s=="<"||s==">"||s=="<="||s==">=")return 9;if(s=="<<"||s==">>")return 10;if(s=="+"||s=="-")return 11;if(s=="*"||s=="/"||s=="%")return 12;return 0;}
bool declaration_type(std::string_view s){return s=="var"||s=="var_nsave"||s=="string"||s=="bmap"||s=="sound"||s=="font"||s=="entity"||s=="panel"||s=="text"||s=="view"||s=="material"||s=="window"||s=="vector";}
bool registered_pointer_type(std::string_view s){return s=="entity"||s=="panel"||s=="view"||s=="string"||s=="bmap"||s=="material"||s=="function";}
Object::Kind object_kind(std::string_view s){if(s=="string")return Object::Kind::String;if(s=="bmap")return Object::Kind::Bmap;if(s=="sound")return Object::Kind::Sound;if(s=="font")return Object::Kind::Font;if(s=="entity")return Object::Kind::Entity;if(s=="panel")return Object::Kind::Panel;if(s=="text")return Object::Kind::Text;if(s=="view")return Object::Kind::View;if(s=="material")return Object::Kind::Material;if(s=="window")return Object::Kind::Window;return Object::Kind::Vector;}
}
struct Runtime::Impl {
 Runtime& runtime;State& state;Builtin builtin;Source source;Resource resource;ParseMetrics metrics;std::vector<std::string> diagnostics;
 std::vector<Token> tokens;std::size_t pos=0;std::map<std::string,std::vector<Token>,CaseLess> macros;std::size_t includeDepth=0;bool compactExpressions=false;CompilerSettings compilerSettings;
 std::deque<Function> functions;std::map<std::string,FunctionId,CaseLess> names;std::vector<Declaration> declarations;std::vector<E> initializers;
 bool suppressContinuations=false;
 // Coop banks: SLOT entries swap the global's Value with VALUE (the other bank's); OBJECT entries swap array elements or text.
 struct Banked {Slot* slot=nullptr;ObjectId object=0;bool text=false,pointer=false;Value value;std::vector<Value> values;std::string string;};
 std::vector<Banked> banked;std::unordered_map<std::string,std::size_t,CaseHash,CaseEqual> bankIndex;BankResolver resolver;int liveBank=0;
 void use_bank(int bank){
  if(bank==liveBank)return;
  for(auto& b:banked){if(b.slot)std::swap(b.slot->value,b.value);else if(b.text)state.require(b.object).text.swap(b.string);else{auto& a=state.require(b.object).array;for(std::size_t i=0;i<b.values.size();++i)std::swap(a[i].value,b.values[i]);}}
  liveBank=bank;
 }
 void disable_banks(){use_bank(0);banked.clear();bankIndex.clear();resolver={};}
 std::deque<std::unique_ptr<Process>> queued;std::deque<std::unique_ptr<Process>>* pendingQueue=nullptr;Process* active=nullptr;std::uint64_t nextProcess=1;std::int32_t time=1024;std::size_t callDepth=0;
 // Local storage objects are pooled: the lease lives in the Object (leased/leaseOwners/...), leasedLocals lists them, freeLocals is the LIFO pool.
 std::vector<Object*> leasedLocals;std::vector<ObjectId> freeLocals;std::vector<std::unique_ptr<Process>> spareFrames;
 Impl(Runtime& r,State& s,Builtin b):runtime(r),state(s),builtin(std::move(b)){functions.emplace_back();}
 ~Impl(){for(auto& p:queued)if(p)release(*p);clear_layouts();for(auto* o:leasedLocals)o->leased=false;}
 void clear_layouts(){for(auto& d:declarations)if(d.object)if(auto* object=state.get(d.object)){object->properties.clear();object->alive=false;}}
 const Token& peek(std::size_t n=0)const{return tokens.at(std::min(pos+n,tokens.size()-1));}
 std::string_view expression_spelling()const{const auto& first=peek();if(compactExpressions&&first.kind==Token::Punct&&first.text.size()==1){const auto& second=peek(1);if(second.kind==Token::Punct&&!second.text.empty()){auto pair=paired_operator(first.text[0],second.text[0]);if(!pair.empty())return pair;}}return first.text;}
 void advance_spelling(){bool joined=expression_spelling()!=peek().text;++pos;if(joined){auto& rest=tokens.at(pos);if(rest.text.size()==1)++pos;else{rest.text.erase(0,1);++rest.loc.column;}}}
 bool is(std::string_view s)const{return (peek().kind==Token::Word||peek().kind==Token::Punct)&&expression_spelling()==s;}bool take(std::string_view s){if(is(s)){advance_spelling();return true;}return false;}
 Token consume(){return tokens.at(pos++);}void expect(std::string_view s){if(!take(s))fail(peek().loc,"expected '"+std::string(s)+"', got '"+peek().text+"'");}
 std::string identifier(){if(peek().kind!=Token::Word)fail(peek().loc,"expected identifier");return consume().text;}
 Token expression_token(){auto spelling=expression_spelling();auto t=peek();if(t.text!=spelling)t.text=spelling;advance_spelling();if(compactExpressions&&(t.kind==Token::Word||t.kind==Token::Number))while(peek().kind==Token::Word||peek().kind==Token::Number)t.text+=consume().text;return t;}
 std::string expression_identifier(){if(peek().kind!=Token::Word)fail(peek().loc,"expected identifier");return expression_token().text;}
 static bool scalar_storage(const Object& o){return o.leased&&o.leaseScalar;}
 FunctionId ensure_function(std::string name){auto it=names.find(name);if(it!=names.end())return it->second;FunctionId id=FunctionId(functions.size());functions.emplace_back();functions.back().name=name;names.emplace(std::move(name),id);return id;}
 void append_expanded(const Token& t,std::vector<Token>& out,int depth=0){if(depth>64)fail(t.loc,"recursive DEFINE expansion");auto m=macros.find(t.text);if(t.kind==Token::Word&&m!=macros.end()){if(m->second.size()==1&&m->second[0].kind==Token::Word&&m->second[0].text==t.text){out.push_back(t);return;}for(auto v:m->second){v.loc=t.loc;append_expanded(v,out,depth+1);}}else out.push_back(t);}
 void preprocess(std::string_view name,std::string_view text){
  if(++includeDepth>256)throw std::runtime_error("WDL include recursion exceeds 256 at "+std::string(name));
  auto ts=lex(text,name,metrics);std::vector<bool> parents;bool enabled=true;std::size_t i=0;
  while(ts[i].kind!=Token::End){auto t=ts[i++];if(t.syntax("#"))continue;
   if(t.syntax("ifdef")||t.syntax("ifndef")){if(ts[i].kind!=Token::Word)fail(t.loc,"expected conditional name");bool defined=macros.contains(ts[i].text);++i;parents.push_back(enabled);enabled=enabled&&(t.text=="ifdef"?defined:!defined);if(ts[i].syntax(";"))++i;continue;}
   if(t.syntax("ifelse")){if(parents.empty())fail(t.loc,"conditional else without IFDEF");enabled=parents.back()&&!enabled;if(ts[i].syntax(";"))++i;continue;}
   if(t.syntax("endif")){if(parents.empty())fail(t.loc,"ENDIF without IFDEF");enabled=parents.back();parents.pop_back();if(ts[i].syntax(";"))++i;continue;}
   if(!enabled)continue;
   if(t.syntax("define")){if(ts[i].kind!=Token::Word)fail(t.loc,"expected DEFINE name");auto key=ts[i++].text;if(ts[i].syntax(",")||ts[i].syntax("="))++i;std::vector<Token> replacement;while(ts[i].kind!=Token::End&&!ts[i].syntax(";"))replacement.push_back(ts[i++]);if(!ts[i].syntax(";"))fail(t.loc,"unterminated DEFINE");++i;if(state.compilerMacros.contains(key)){diagnostics.push_back(t.loc.str()+": preserving protected compiler DEFINE "+key);continue;}if(macros.contains(key))diagnostics.push_back(t.loc.str()+": redefining DEFINE "+key);macros[key]=std::move(replacement);continue;}
   if(t.syntax("include")||t.syntax("resource")){std::string file;if(ts[i].kind==Token::String)file=ts[i++].text;else if(ts[i].syntax("<")){++i;while(ts[i].kind!=Token::End&&!ts[i].syntax(">"))file+=ts[i++].text;if(!ts[i++].syntax(">"))fail(t.loc,"unterminated filename");}else fail(t.loc,"expected resource filename");if(ts[i].syntax(";"))++i;
    try{if(t.text=="resource"){if(resource)resource(file);else diagnostics.push_back(t.loc.str()+": resource "+file);}
    else {if(!source)fail(t.loc,"include requires source resolver");++metrics.files;preprocess(file,source(file));}}catch(const WdlError&){throw;}catch(const std::exception& ex){fail(t.loc,ex.what());}continue;
   }
   append_expanded(t,tokens);
  }
  if(!parents.empty())fail(ts.back().loc,"unterminated IFDEF");
  --includeDepth;
 }
 E node(Expr::Kind kind,const Location& loc,std::string op={}){++metrics.expressions;auto e=std::make_unique<Expr>();e->kind=kind;e->loc=loc;e->op=std::move(op);link(*e);return e;}
 E expression(int min=1){
  Token t=expression_token();E e;
  if(t.kind==Token::Number){e=node(Expr::Literal,t.loc);try{std::size_t consumed=0;if(t.text.starts_with("0x")||t.text.starts_with("0X"))e->value=Value::from_int(std::stoul(t.text,&consumed,16));else e->value=Value::from_double(std::stod(t.text,&consumed));if(consumed!=t.text.size())fail(t.loc,"invalid numeric literal '"+t.text+"'");}catch(const std::exception&){fail(t.loc,"invalid numeric literal '"+t.text+"'");}}
  else if(t.kind==Token::String){e=node(Expr::Literal,t.loc);auto& o=state.create(Object::Kind::String);o.text=t.text;e->value=Value::object(o.id);}
  else if(t.text=="<"){std::string s;while(!is(">")&&peek().kind!=Token::End)s+=consume().text;expect(">");e=node(Expr::Literal,t.loc);auto& o=state.create(Object::Kind::String);o.text=s;e->value=Value::object(o.id);}
  else if(t.text=="("){e=expression();if(!is(")")){auto list=node(Expr::List,t.loc);list->args.push_back(std::move(e));while(!is(")")){take(",");if(is(")"))break;if(peek().kind==Token::End)fail(t.loc,"unterminated initializer list");list->args.push_back(expression());}e=std::move(list);}expect(")");}
  else if(t.text=="+"||t.text=="-"||t.text=="!"||t.text=="~"||t.text=="++"||t.text=="--"||t.text=="&"||t.text=="*"){e=node(Expr::Unary,t.loc,t.text);e->args.push_back(expression(13));}
  else if(t.kind==Token::Word)e=node(Expr::Name,t.loc,t.text);
  else fail(t.loc,"expected expression, got '"+t.text+"'");
  for(;;){
   // A6 0040a300 / A7 1000bf40 resolve native descriptors before data/local/script symbols.
   if(take("(")){auto call=node(Expr::Call,e->loc);if(e->kind==Expr::Name&&native_call_name(state.profile,e->op))call->dispatch=Expr::Native;call->args.push_back(std::move(e));if(!take(")")){do{call->args.push_back(expression());}while(take(","));expect(")");}e=std::move(call);continue;}
   if(take(".")||take("->")){auto m=node(Expr::Member,e->loc,expression_identifier());m->args.push_back(std::move(e));e=std::move(m);continue;}
   if(take("[")){auto m=node(Expr::Index,e->loc);m->args.push_back(std::move(e));m->args.push_back(expression());expect("]");e=std::move(m);continue;}
   if(is("++")||is("--")){auto m=node(Expr::Unary,e->loc,"post"+expression_token().text);m->args.push_back(std::move(e));e=std::move(m);continue;}
   int p=peek().kind==Token::Punct?precedence(expression_spelling()):0;if(p<min||!p)break;auto op=expression_token();if(op.text=="?"){auto c=node(Expr::Conditional,op.loc);c->args.push_back(std::move(e));c->args.push_back(expression());expect(":");c->args.push_back(expression(p));e=std::move(c);continue;}
   auto b=node(Expr::Binary,op.loc,op.text);b->args.push_back(std::move(e));b->args.push_back(expression(p+(p==1?0:1)));e=std::move(b);
  }return e;
 }
 std::size_t emit(Function& f,Instruction::Kind kind,E e={},std::size_t target=0){Instruction i;i.kind=kind;i.expr=std::move(e);i.target=target;if(i.expr)i.loc=i.expr->loc;f.code.push_back(std::move(i));return f.code.size()-1;}
 struct Loop {std::vector<std::size_t> breaks,continues;};std::vector<Loop> loops;
 void statement(Function& f){++metrics.statements;
  // 0041a520 drops commas before statement expression mode; expression argument commas remain significant.
  while(take(",")){}
  if(take(";"))return;if(take("{")){while(!take("}")){if(take(","))continue;if(peek().kind==Token::End)fail(peek().loc,"unterminated block");statement(f);}return;}
  if(peek().kind==Token::Word&&peek(1).syntax(":")){auto name=identifier();expect(":");if(!f.labels.emplace(name,f.code.size()).second)fail(peek().loc,"duplicate label "+name);return;}
  if(take("goto")){bool paren=take("(");auto name=identifier();if(paren)expect(")");f.gotos.emplace_back(emit(f,Instruction::Jump),name);expect(";");return;}
  if(is("exit")&&peek(1).syntax(";")){auto t=consume();auto call=node(Expr::Call,t.loc);call->args.push_back(node(Expr::Name,t.loc,"exit"));emit(f,Instruction::Eval,std::move(call));expect(";");return;}
  // Legacy REMOVE's descriptor takes one operand (0061b468/00402d60); do not compact its command and argument.
  if(is("remove")&&!peek(1).syntax("(")){auto t=consume();auto call=node(Expr::Call,t.loc);call->dispatch=Expr::Native;call->args.push_back(node(Expr::Name,t.loc,"remove"));call->args.push_back(expression());emit(f,Instruction::Eval,std::move(call));expect(";");return;}
  if(take("if")){expect("(");auto c=expression();expect(")");auto branch=emit(f,Instruction::Branch,std::move(c));statement(f);if(take("else")){auto j=emit(f,Instruction::Jump);f.code[branch].target=f.code.size();statement(f);f.code[j].target=f.code.size();}else f.code[branch].target=f.code.size();return;}
  if(take("while")){auto start=f.code.size();expect("(");auto c=expression();expect(")");auto b=emit(f,Instruction::Branch,std::move(c));loops.emplace_back();statement(f);emit(f,Instruction::Jump,{},start);finish_loop(f,start);f.code[b].target=f.code.size();return;}
  if(take("do")){auto start=f.code.size();loops.emplace_back();statement(f);expect("while");expect("(");auto c=expression();expect(")");take(";");auto cont=f.code.size();auto b=emit(f,Instruction::Branch,std::move(c));emit(f,Instruction::Jump,{},start);finish_loop(f,cont);f.code[b].target=f.code.size();return;}
  if(take("for")){expect("(");if(!is(";")){if(declaration_type(peek().text))local(f);else{emit(f,Instruction::Eval,expression());expect(";");}}else expect(";");auto start=f.code.size();E cond;if(!is(";"))cond=expression();expect(";");E step;if(!is(")"))step=expression();expect(")");std::size_t b=std::numeric_limits<std::size_t>::max();if(cond)b=emit(f,Instruction::Branch,std::move(cond));loops.emplace_back();statement(f);auto cont=f.code.size();if(step)emit(f,Instruction::Eval,std::move(step));emit(f,Instruction::Jump,{},start);finish_loop(f,cont);if(b!=std::numeric_limits<std::size_t>::max())f.code[b].target=f.code.size();return;}
  if(take("break")){if(loops.empty())fail(peek().loc,"break outside loop");loops.back().breaks.push_back(emit(f,Instruction::Jump));expect(";");return;}
  if(take("continue")){if(loops.empty())fail(peek().loc,"continue outside loop");loops.back().continues.push_back(emit(f,Instruction::Jump));expect(";");return;}
  if(take("return")){E e;if(!is(";"))e=expression();emit(f,Instruction::Return,std::move(e));expect(";");return;}
  if(take("proc_late")){if(take("("))expect(")");emit(f,Instruction::Late);expect(";");return;}
  if(peek().kind==Token::Word&&declaration_type(peek().text)){local(f);return;}
  emit(f,Instruction::Eval,expression());expect(";");
 }
 void finish_loop(Function& f,std::size_t cont){auto loop=std::move(loops.back());loops.pop_back();for(auto p:loop.breaks)f.code[p].target=f.code.size();for(auto p:loop.continues)f.code[p].target=cont;}
 void local(Function& f){auto type=consume();bool ptr=take("*");do{auto name=identifier();E size,init;if(take("[")){size=expression();expect("]");}if(take("=")){init=expression();if(size&&!is(";")){auto list=node(Expr::List,type.loc);list->args.push_back(std::move(init));while(!is(";")){take(",");if(is(";"))break;list->args.push_back(expression());}init=std::move(list);}}auto n=emit(f,Instruction::Declare,std::move(init));f.code[n].name=name;f.code[n].loc=type.loc;if(size){if(size->kind!=Expr::Literal)fail(size->loc,"local array size must be constant");auto count=size->value.integer();if(count<0)fail(size->loc,"negative array size");f.code[n].size=std::size_t(count);}else if(type.text=="vector")f.code[n].size=3;else if(!ptr&&type.text!="var"&&type.text!="var_nsave")fail(type.loc,"unsupported value local declaration "+type.text);}while(take(","));expect(";");}
 void parse(){
  while(peek().kind!=Token::End){if(take(";")||take(","))continue;auto t=peek();
   if(t.syntax("plugindir")||t.syntax("bind")||t.syntax("level")){consume();take("=");std::string file;if(peek().kind==Token::String)file=consume().text;else if(take("<")){while(!is(">")&&peek().kind!=Token::End)file+=consume().text;expect(">");}else fail(t.loc,"expected compiler filename");expect(";");++metrics.directives;if(t.text=="plugindir"){if(file.size()>126)fail(t.loc,"PLUGINDIR exceeds 126 bytes");compilerSettings.plugin_directory=std::move(file);}else if(t.text=="bind")compilerSettings.bindings.push_back(std::move(file));else compilerSettings.levels.push_back(std::move(file));continue;}
   if(t.syntax("function")||t.syntax("action")||t.syntax("starter")||t.syntax("dllfunction")){
    consume();bool ptr=take("*");auto name=identifier();if(ptr){Declaration d;d.type="function";d.pointer=true;d.name=name;d.loc=t.loc;if(take("="))d.init=expression();expect(";");declarations.push_back(std::move(d));++metrics.declarations;continue;}
    auto id=ensure_function(name);auto& f=functions[id];std::vector<Parameter> params;
    if(take("(")){if(!take(")")){do{if(peek().kind==Token::Word&&declaration_type(peek().text))consume();take("*");bool ref=take("&");params.push_back({identifier(),ref});}while(take(","));expect(")");}}
    if(take(";")){if(!f.defined){f.params=params;f.loc=t.loc;f.external=t.text=="dllfunction";}continue;}
    if(t.text=="dllfunction")fail(t.loc,"DLL function body is invalid");
    if(f.defined){diagnostics.push_back(t.loc.str()+": replacing function '"+name+"' defined at "+f.loc.str()+" (A6 compiler 00403a00 frees previous body)");f.code.clear();f.localDefinitions.clear();f.labels.clear();f.gotos.clear();f.schedulerFlags=0;}
    f.defined=true;f.loc=t.loc;f.params=std::move(params);f.starter=t.text=="starter";++metrics.functions;compactExpressions=true;statement(f);compactExpressions=false;for(auto& i:f.code)if(i.kind==Instruction::Declare&&std::none_of(f.localDefinitions.begin(),f.localDefinitions.end(),[&](auto& local){return local.first==i.name;}))f.localDefinitions.emplace_back(i.name,i.size?i.size:3);for(auto& [pc,label]:f.gotos){auto l=f.labels.find(label);if(l==f.labels.end())fail(t.loc,"undefined label "+label);f.code[pc].target=l->second;}continue;
   }
   if(t.kind==Token::Word&&declaration_type(t.text)){consume();bool ptr=take("*");do{Declaration d;d.type=t.text;d.pointer=ptr;d.loc=t.loc;d.name=identifier();if(take("[")){d.size=expression();expect("]");}
    bool hasInitializer=take("=");if(!hasInitializer&&(d.type=="font"||d.type=="bmap"||d.type=="sound"||d.type=="string")&&is(",")&&(peek(1).kind==Token::String||peek(1).kind==Token::Number||peek(1).syntax("<")))hasInitializer=take(",");if(!hasInitializer&&!d.pointer&&(peek().kind==Token::String||is("<")||peek().kind==Token::Number||((d.type=="var"||d.type=="var_nsave")&&(is("+")||is("-"))&&peek(1).kind==Token::Number)))hasInitializer=true;
    // Numeric declarations skip punctuation (0041a0e0); ';' ends their value list (0041a520), even before ')'.
    if(d.size&&!d.pointer&&(d.type=="var"||d.type=="var_nsave")&&(hasInitializer||is("("))){d.init=node(Expr::List,t.loc);while(!is(";")){if(take("(")||take(")")||take(","))continue;if(peek().kind==Token::End)fail(t.loc,"unterminated initializer");d.init->args.push_back(expression(13));}expect(";");while(take(")")){}}
    else if(hasInitializer){d.init=expression();if((d.size||d.type=="font"||d.type=="bmap")&&!is(";")&&!is("}")){auto list=node(Expr::List,t.loc);list->args.push_back(std::move(d.init));while(!is(";")&&!is("}")){take(",");if(is(";")||is("}"))break;if(peek().kind==Token::End)fail(t.loc,"unterminated initializer");list->args.push_back(expression());}d.init=std::move(list);}}
    // Declaration parameters use the punctuation-skipping 0041a0e0 reader; whitespace also separates arguments.
    else if(take("{")){while(!take("}")){auto key=identifier();take("=");std::vector<E> args;while(!is(";")){if(take(","))continue;auto e=expression();bool enumArgument=(key=="mode"&&args.empty())||(key=="picture"&&args.size()==1)||(key=="frame"&&args.empty())||(key=="button"&&args.size()<2);if(d.type=="window"&&e->kind==Expr::Name&&enumArgument){auto& o=state.create(Object::Kind::Name);o.text=e->op;e->kind=Expr::Literal;e->value=Value::object(o.id);e->op.clear();}args.push_back(std::move(e));}expect(";");d.properties.emplace_back(std::move(key),std::move(args));}take(";");}
    ++metrics.declarations;declarations.push_back(std::move(d));}while(take(","));take(";");continue;}
   // Only recovered type-6 callback slots allow top-level NAME FUNCTION/NULL bindings without '='.
   if(auto slot=state.globals.find(t.text);t.kind==Token::Word&&slot!=state.globals.end()&&slot->second.callback&&(peek(1).kind==Token::Word||peek(1).syntax("="))){consume();take("=");auto binding=node(Expr::Binary,t.loc,"=");binding->args.push_back(node(Expr::Name,t.loc,t.text));binding->args.push_back(expression());initializers.push_back(std::move(binding));expect(";");continue;}
   initializers.push_back(expression());expect(";");
  }
 }
 // A variable's cell reads through to element 0 of the vector/array object it names.
 Argument slot_argument(Slot& slot){if(slot.value.kind==Value::Kind::Object){auto* o=state.get(slot.value.bits);if(o&&o->kind==Object::Kind::Vector&&!o->array.empty())return {o->array[0].value,{&o->array[0],o->id,{},0}};}return {slot.value,{&slot,slot.value.kind==Value::Kind::Object?slot.value.bits:0}};}
 // Globals are never erased, so a found Slot* stays valid for the Expr's lifetime. numericGlobals only grows: its size is the epoch.
 Slot* global_slot(const Expr& e){
  if(!e.global){e.global=state.globals.get(e.sym);if(!e.global)return nullptr;}
  if(e.numericEpoch!=state.numericGlobals.size()+1){auto view=state.numericGlobals.find(e.op);e.numericView=view==state.numericGlobals.end()?0:view->second;e.numericEpoch=state.numericGlobals.size()+1;}
  return e.global;
 }
 Argument name_argument(const Expr& e,Process* p){
  switch(e.resolution){
   case Expr::Zero:return {Value{},{}};
   case Expr::One:return {Value::from_int(1),{}};
   case Expr::Local:if(p){const auto& local=p->locals[e.slot];return {local.cell->value,{local.cell,local.object->id,{},0}};}break;
   case Expr::Binding:if(p){auto& bound=p->bindings[e.slot];return {bound.current(),bound.ref};}break;
   case Expr::Global:break;
  }
  if(auto* slot=global_slot(e)){if(e.numericView)return {slot->value,{slot,e.numericView,"x"}};return slot_argument(*slot);}
  auto fn=names.find(e.op);if(fn!=names.end())return {Value::function(fn->second),{}};
  fail(e.loc,"unknown symbol '"+e.op+"'");
 }
 static Value raw_value(std::uint32_t v){return Value::from_raw(std::bit_cast<std::int32_t>(v));}
 Value apply(const Expr& e,const Value& x,const Value& y){
  switch(e.opc){
   case Op::Add:return raw_value(x.bits+y.bits);
   case Op::Sub:return raw_value(x.bits-y.bits);
   case Op::Mul:{std::int64_t n=std::int64_t(x.raw())*y.raw();if(n<0&&n>=-4294967296LL)n+=1023;return raw_value(std::uint32_t(n>>10));}
   case Op::Div:{if(!y.raw())fail(e.loc,"fixed-point division by zero");std::int64_t n=std::int64_t(x.raw())*1024/y.raw();if(n<std::numeric_limits<std::int32_t>::min()||n>std::numeric_limits<std::int32_t>::max())fail(e.loc,"fixed-point division overflow");return Value::from_raw(std::int32_t(n));}
   case Op::Mod:{if(!y.raw())fail(e.loc,"fixed-point remainder by zero");std::int32_t left=x.raw();if(left<0){std::int64_t q=0xF424000000LL/y.raw();if(q<std::numeric_limits<std::int32_t>::min()||q>std::numeric_limits<std::int32_t>::max())fail(e.loc,"legacy negative remainder adjustment overflow");auto count=std::uint32_t((std::int32_t(q)>>10)+1);left=std::bit_cast<std::int32_t>(std::uint32_t(left)+count*y.bits);}if(left==std::numeric_limits<std::int32_t>::min()&&y.raw()==-1)fail(e.loc,"fixed-point remainder overflow");return Value::from_raw(left%y.raw());}
   case Op::Eq:return Value::from_int(x.bits==y.bits);
   case Op::Ne:return Value::from_int(x.bits!=y.bits);
   case Op::Lt:return Value::from_int(x.raw()<y.raw());
   case Op::Gt:return Value::from_int(x.raw()>y.raw());
   case Op::Le:return Value::from_int(x.raw()<=y.raw());
   case Op::Ge:return Value::from_int(x.raw()>=y.raw());
   case Op::And:return Value::from_int(bool(x)&&bool(y));
   case Op::Or:return Value::from_int(bool(x)||bool(y));
   case Op::BitAnd:return raw_value(x.bits&y.bits);
   case Op::BitOr:return raw_value(x.bits|y.bits);
   case Op::BitXor:return raw_value(x.bits^y.bits);
   case Op::Shl:return raw_value(x.bits<<(y.integer()&31));
   case Op::Shr:return Value::from_raw(x.raw()>>(y.integer()&31));
   default:break;
  }
  fail(e.loc,"unknown operator "+(e.assign?e.op.substr(0,1):e.op));
 }
 // VARIABLE.member where the variable (a local's value cell or a plain global) holds a handle to an object that is neither a
 // vector nor a TEXT: the member is state.member(object) with none of the vector/skill/string cases. Null: take the general path.
 Object* handle_object(const Expr& base,Process* p){
  if(base.kind!=Expr::Name)return nullptr;
  const Value* v;
  if(base.resolution==Expr::Local){if(!p)return nullptr;const auto& local=p->locals[base.slot];if(!scalar_storage(*local.object))return nullptr;v=&local.cell->value;}
  else if(base.resolution==Expr::Global){const Slot* slot=global_slot(base);if(!slot||base.numericView)return nullptr;v=&slot->value;}
  else return nullptr;
  if(v->kind!=Value::Kind::Object)return nullptr;
  Object* o=state.get(v->bits);return o&&o->kind!=Object::Kind::Vector&&o->kind!=Object::Kind::Text?o:nullptr;
 }
 // value(e,p) is evaluate(e,p).current() without building the reference: operands of pure operators and statement results take it.
 // Both operands are always evaluated (&& and || do not short-circuit); only % evaluates its right operand first.
 Value value(const Expr& e,Process* p){
  switch(e.kind){
   case Expr::Literal:return e.value;
   case Expr::Name:
    if(e.resolution==Expr::Local){if(p)return p->locals[e.slot].cell->value;}
    else if(e.resolution==Expr::Global){if(auto* slot=global_slot(e)){if(!e.numericView&&slot->value.kind==Value::Kind::Object){auto* o=state.get(slot->value.bits);if(o&&o->kind==Object::Kind::Vector&&!o->array.empty())return o->array[0].value;}return slot->value;}}
    break;
   case Expr::Unary:
    switch(e.opc){
     case Op::Plus:return value(*e.args[0],p);
     case Op::Neg:return raw_value(0u-value(*e.args[0],p).bits);
     case Op::Not:return Value::from_int(!bool(value(*e.args[0],p)));
     case Op::BitNot:return raw_value(~value(*e.args[0],p).bits);
     default:break;
    }
    break;
   case Expr::Binary:
    if(!e.assign){
     if(e.opc==Op::Mod){const Value y=value(*e.args[1],p);const Value x=value(*e.args[0],p);return apply(e,x,y);}
     const Value x=value(*e.args[0],p);const Value y=value(*e.args[1],p);return apply(e,x,y);
    }
    break;
   case Expr::Conditional:return value(*e.args[0],p)?value(*e.args[1],p):value(*e.args[2],p);
   case Expr::Member:if(Object* o=handle_object(*e.args[0],p)){const auto ref=state.member(*o,e.sym,&e.fieldHint);return ref.slot?ref.get():Value::object(o->id);}break;
   default:break;
  }
  return evaluate(e,p).current();
 }
 // Call arguments: up to 8 live on the C stack, constructed only as far as used.
 struct ArgumentBuffer {
  alignas(Argument) unsigned char raw[8*sizeof(Argument)];std::vector<Argument> large;Argument* data;std::size_t count;
  explicit ArgumentBuffer(std::size_t n):count(n){if(n>8){large.resize(n);data=large.data();}else{data=reinterpret_cast<Argument*>(raw);for(std::size_t i=0;i<n;++i)new(data+i)Argument();}}
  ~ArgumentBuffer(){if(large.empty())for(std::size_t i=0;i<count;++i)data[i].~Argument();}
  ArgumentBuffer(const ArgumentBuffer&)=delete;ArgumentBuffer& operator=(const ArgumentBuffer&)=delete;
 };
 static void link_call(const Expr& e){
  const Expr& target=*e.args[0];const std::string_view name=target.kind==Expr::Name?std::string_view(target.op):std::string_view{};
  e.special=name=="wait"?Expr::Wait:name=="waitt"?Expr::Waitt:name=="sleep"?Expr::Sleep:name=="proc_late"?Expr::ProcLate:name=="proc_kill"?Expr::ProcKill:Expr::Ordinary;
 }
 Argument evaluate(const Expr& e,Process* p=nullptr){
  auto result=[](Value v,Ref ref={}){return Argument{v,ref};};
  auto arg=[&](std::size_t i){return evaluate(*e.args.at(i),p);};
  try{switch(e.kind){
   case Expr::Literal:return result(e.value);
   case Expr::Name:return name_argument(e,p);
   case Expr::Member:{
    if(Object* o=handle_object(*e.args.at(0),p)){const auto ref=state.member(*o,e.sym,&e.fieldHint);return result(ref.slot?ref.get():Value::object(o->id),ref);}
    auto a=arg(0);auto v=a.current();ObjectId id=v.kind==Value::Kind::Object?v.bits:a.ref.owner;auto* owner=state.get(a.ref.owner);const bool vectorOwner=owner&&owner->kind==Object::Kind::Vector;if(vectorOwner&&scalar_storage(*owner)&&v.kind!=Value::Kind::Object&&!e.vectorMember)fail(e.loc,"member '"+e.op+"' on non-object parameter");if(vectorOwner&&e.args[0]->kind==Expr::Name&&(!scalar_storage(*owner)||v.kind!=Value::Kind::Object))id=owner->id;if(!id)fail(e.loc,"member '"+e.op+"' on null/non-object");auto& o=state.require(id);
    const bool component=e.component<3;const std::size_t componentIndex=component?e.component:2;
    if(component&&a.ref.owner==id&&a.ref.field.starts_with("skill")){int start=0;auto suffix=a.ref.field.substr(5);auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),start);if(parsed.ec!=std::errc{})fail(e.loc,"invalid skill vector base");auto ref=state.member(id,"skill"+std::to_string(start+componentIndex));return result(ref.get(),ref);}
    if(o.kind==Object::Kind::Text&&e.sym=="string"_sym){auto index=o.field("index"_sym).value.integer();if(index<1||std::size_t(index)>o.array.size())return result(Value{}, {nullptr,o.id,"string"});auto n=std::size_t(index-1);return result(o.array[n].value,{&o.array[n],o.id,"string",n});}
    if(o.kind==Object::Kind::Vector&&!o.scalar&&component){
     std::size_t n=(a.ref.owner==id?a.ref.index:0)+componentIndex;if(n>=o.array.size())fail(e.loc,"vector component out of bounds");return result(o.array[n].value,{&o.array[n],o.id,e.op,n});
    }
    auto ref=state.member(o.id,e.sym);return result(ref.slot?ref.get():Value::object(o.id),ref);
   }
   case Expr::Index:{
    auto base=arg(0);auto a=base.current();auto n=value(*e.args.at(1),p).integer();ObjectId id=a.kind==Value::Kind::Object?a.bits:base.ref.owner;
    auto* owner=state.get(base.ref.owner);if(owner&&(owner->kind==Object::Kind::Text||(owner->kind==Object::Kind::Vector&&(!scalar_storage(*owner)||a.kind!=Value::Kind::Object))))id=owner->id;
    if(!id||n<0)fail(e.loc,"invalid array reference/index");auto& o=state.require(id);
    if((o.scalar||(o.kind!=Object::Kind::Vector&&o.kind!=Object::Kind::Text))&&base.ref.owner==id&&!base.ref.field.empty()){
     auto field=base.ref.field;std::string next;
     if(field.starts_with("skill")){int start=0;auto suffix=field.substr(5);auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),start);if(parsed.ec!=std::errc{})fail(e.loc,"invalid skill vector base");next="skill"+std::to_string(start+n);}
     else {std::array<std::string_view,3> fields;if(field=="x"||field=="pos")fields={"x","y","z"};else if(field=="pan"||field=="angle")fields={"pan","tilt","roll"};else if(field=="scale_x"||field=="scale")fields={"scale_x","scale_y","scale_z"};else if(field=="blue"||field=="color")fields={"blue","green","red"};else if(field=="vel_x"||field=="vel")fields={"vel_x","vel_y","vel_z"};else fail(e.loc,"not an indexed member reference");if(n>=3)fail(e.loc,"member vector index out of bounds");next=fields[std::size_t(n)];}
     auto ref=state.member(id,next);return result(ref.get(),ref);
    }
    std::size_t offset=base.ref.owner==id&&o.kind!=Object::Kind::Text?base.ref.index:0;if(offset+std::size_t(n)>=o.array.size())fail(e.loc,"array index "+std::to_string(n)+" out of bounds "+std::to_string(o.array.size()));auto& slot=o.array[offset+std::size_t(n)];return result(slot.value,{&slot,o.id,{},offset+std::size_t(n)});
   }
   case Expr::List:fail(e.loc,"tuple used outside vector/array initializer");
   case Expr::Conditional:{auto c=value(*e.args.at(0),p);return c?arg(1):arg(2);}
   case Expr::Unary:{
    if(e.opc==Op::Reference){auto a=arg(0);if(!a.ref)fail(e.loc,"reference operator on non-reference");return a;}
    if(e.opc!=Op::Increment)return result(value(e,p));
    auto a=arg(0);auto v=a.current();
    if(!a.ref.slot)fail(e.loc,"increment requires writable reference");auto next=raw_value(v.bits+(e.decrement?0u-1024u:1024u));a.ref.set(next);return e.post?result(v):result(next,a.ref);
   }
   case Expr::Binary:{
    if(!e.assign)return result(value(e,p));
    auto a=arg(0);
    if(e.opc==Op::None){const Value y=value(*e.args[1],p);if(!a.ref.slot)fail(e.loc,"assignment requires writable reference");a.ref.set(y);return result(y,a.ref);}
    const Value x=a.current();const Value y=value(*e.args[1],p);const Value v=apply(e,x,y);
    if(!a.ref.slot)fail(e.loc,"compound assignment requires writable reference");
    a.ref.set(v);return result(v,a.ref);
   }
   case Expr::Call:{
    if(e.args.size()>65)fail(e.loc,"call exceeds 64 arguments");
    const Expr& target=*e.args.at(0);if(e.special==Expr::Unlinked)link_call(e);
    ArgumentBuffer buffer(e.args.size()-1);Argument* args=buffer.data;
    for(std::size_t i=e.args.size();i-->1;){auto& value=args[i-1];value=arg(i);value.value=value.current();value.captured=true;}
    std::span<const Argument> values(args,e.args.size()-1);std::string_view name=target.kind==Expr::Name?target.op:std::string_view{};
    if(p){p->context.my=context_slot(*p,0).value.bits;p->context.you=context_slot(*p,1).value.bits;}
    if(e.special==Expr::Wait||e.special==Expr::Waitt||e.special==Expr::Sleep){
     if(!p)fail(e.loc,"wait in declaration initializer");if(values.size()!=1)fail(e.loc,"wait requires one argument");auto n=values[0].current().raw();
     auto* timeSlot=state.globals.get("time"_sym);auto t=timeSlot?timeSlot->value.raw():time;
     Value returned;
     if(e.special==Expr::Wait){auto clamped=std::clamp(n,1024,1048575);p->countdown=clamped-1024;returned=Value::from_raw(clamped);}
     else {if(e.special==Expr::Sleep){n=std::bit_cast<std::int32_t>(std::uint32_t(n)<<4);if(n<1024&&n/2<t)return result(Value{});}auto chosen=n<t?t:std::min(n,1048575);p->countdown=std::bit_cast<std::int32_t>(std::uint32_t(t)-std::uint32_t(chosen));returned=Value::from_raw(chosen);}
     capture(*p);p->yielded=true;return result(returned);
    }
    if(e.special==Expr::ProcLate){if(!p)fail(e.loc,"proc_late outside process");functions[p->function].schedulerFlags|=2;return result(Value{});}
    if(e.special==Expr::ProcKill){
     if(!p||values.size()!=1)fail(e.loc,"proc_kill requires current process and mode");auto mode=values[0].current().raw()>>10;
     auto cancel=[&](auto& queue){for(auto& q:queue)if(q&&!q->running&&(((mode&1)&&p->context.my&&q->context.my==p->context.my)||((mode&2)&&p->context.you&&q->context.my==p->context.you)||((mode&4)&&q->function==p->function)))q->cancelled=true;};cancel(queued);if(pendingQueue)cancel(*pendingQueue);
     if(mode&8)suppressContinuations=true;return result(Value{});
    }
    Value targetValue;bool resolved=e.dispatch==Expr::Script;
    if(e.dispatch==Expr::Script)targetValue=e.value;
    else if(e.dispatch==Expr::Dynamic){
     auto local=[&]{const auto& f=functions[p->function];return std::find(f.localSyms.begin(),f.localSyms.end(),target.sym)!=f.localSyms.end()||std::find(f.bindingSyms.begin(),f.bindingSyms.end(),target.sym)!=f.bindingSyms.end();};
     resolved=name.empty()||state.globals.contains(target.sym)||(p&&local())||names.contains(name);
     if(resolved)targetValue=arg(0).current();
    }
    // A6 indirect adapter 00412220 returns raw zero for NULL, after argument evaluation.
    if(resolved&&!targetValue.bits)return result(Value{});
    if(resolved&&targetValue.kind!=Value::Kind::Function)fail(e.loc,"call target is not a defined function");
    if(targetValue.kind==Value::Kind::Function&&targetValue.bits){if(targetValue.bits>=functions.size())fail(e.loc,"invalid function pointer");auto& called=functions[targetValue.bits];if(called.defined){auto v=start(targetValue.bits,values,p?p->context.my:0,p?p->context.you:0);return result(v);}if(!called.external)fail(e.loc,"call target '"+called.name+"' has no definition");name=called.name;}
    if(name.empty())fail(e.loc,"call target is not a defined function");
    ProcessContext temporary;auto& context=p?p->context:temporary;
    context.call=target.kind==Expr::Name&&name.data()==target.op.data()?target.sym:Symbols::intern(name);
    auto value=builtin(runtime,context,Symbols::name(context.call),values);if(p){context_slot(*p,0).value=Value::object(context.my);context_slot(*p,1).value=Value::object(context.you);}return result(value);
   }
  }fail(e.loc,"invalid expression");}catch(const WdlError&){throw;}catch(const std::exception& ex){fail(e.loc,ex.what());}
 }
 static Value property_value(void* self,const void* expression){return static_cast<Impl*>(self)->evaluate(*static_cast<const Expr*>(expression)).current();}
 Argument deferred_property(const Expr& e){Argument a;a.evaluatorContext=this;a.expression=&e;a.evaluator=&Impl::property_value;return a;}
 Argument numeric_binding_fallback(const Expr& e){
  auto warning=state.globals.find("warn_level");
  if(warning==state.globals.end())fail(e.loc,"numeric header resolution requires native WARN_LEVEL");
  if(warning->second.value.raw()>=2048)fail(e.loc,"unresolved numeric widget target '"+e.op+"'");
  // 004174f0 clears indirection flags, then binds the same private DWORD0065f1e4.
  return {state.numericBindingFallback.value,{&state.numericBindingFallback}};
 }
 Argument numeric_binding(Expr& e){
  const Expr* root=&e;while(root->kind==Expr::Member||root->kind==Expr::Index)root=root->args[0].get();
  if(root->kind==Expr::Name&&!state.globals.contains(root->op)&&!names.contains(root->op)&&root->op!="null"&&root->op!="off"&&root->op!="on"&&root->op!="true"&&root->op!="false")return numeric_binding_fallback(*root);
  if(e.kind==Expr::Name){auto slot=state.globals.find(e.op);if(slot!=state.globals.end()&&slot->second.callback)return numeric_binding_fallback(e);
   for(const auto& declaration:declarations)if(declaration.name==e.op&&declaration.type!="var"&&declaration.type!="var_nsave"&&declaration.type!="vector")return numeric_binding_fallback(e);
  }
  if(e.kind!=Expr::Name&&e.kind!=Expr::Literal){
   // Unknown executable expressions remain errors; only reference resolution gets the header policy.
   validate_expression(e,{});
   if(root->kind==Expr::Name&&(e.kind==Expr::Member||e.kind==Expr::Index))
    for(const auto& declaration:declarations)if(declaration.name==root->op&&declaration.pointer)return deferred_property(e);
   if(e.kind!=Expr::Member&&e.kind!=Expr::Index)return deferred_property(e);
  }
  auto a=evaluate(e);if(a.current().kind==Value::Kind::Number)return a;return numeric_binding_fallback(e);
 }
 Ref declaration_member(ObjectId id,std::string_view name,const Location& location){try{return state.member(id,name);}catch(const std::exception& ex){fail(location,ex.what());}}
 void initialize(){
  for(auto& d:declarations){
   if(d.type=="window"){diagnostics.push_back(d.loc.str()+": obsolete WINDOW '"+d.name+"' parsed without object construction (00419c20/00419c40)");continue;}
   auto& slot=state.global(d.name);
   if(d.pointer&&!d.size&&registered_pointer_type(d.type))d.pointerCell=&slot; // A6 0041efb0/0041f090: persistent registered cells, not generic VAR storage.
   if(d.pointer||d.type=="function"){slot.value={};continue;}
   if(d.type=="var"||d.type=="var_nsave"){
    if(d.size){auto n=evaluate(*d.size).current().integer();if(n<0||n>1048576)fail(d.loc,"invalid array size");auto& o=state.create(Object::Kind::Vector,d.name);o.array.resize(std::size_t(n));d.object=o.id;slot.value=Value::object(o.id);}else {slot.value={};if(!state.numericGlobals.contains(d.name)){auto& o=state.create(Object::Kind::Vector,d.name);o.scalar=&slot;o.field("y");o.field("z");state.numericGlobals.emplace(d.name,o.id);}}
   }else{auto& o=state.create(object_kind(d.type),d.name);d.object=o.id;if(d.type=="vector")o.array.resize(3);slot.value=Value::object(o.id);
    if(d.type=="string"&&d.size){auto n=evaluate(*d.size).current().integer();if(n<0||n>1048576)fail(d.loc,"invalid string capacity");o.text.reserve(std::size_t(n));}
    if(d.type=="panel"||d.type=="text"||d.type=="view"||d.type=="entity"){for(auto key:{"scale_x","scale_y","scale_z"})o.field(key).value=Value::from_int(1);o.field("alpha").value=Value::from_int(100);for(auto key:{"red","green","blue"})o.field(key).value=Value::from_int(d.type=="entity"?128:255);o.field("flags").value={};if(d.type=="entity")o.field("albedo").value=Value::from_int(100);if(d.type=="text"){o.field("size_y").value=Value::from_int(1000);o.field("index").value=Value::from_int(1);}if(d.type=="view"){o.field("arc").value=Value::from_int(60);o.field("clip_near").value=Value::from_int(1);o.field("clip_far").value=Value::from_int(10000);}}
   }
  }
  validate_functions();
  for(auto& d:declarations){if(d.type=="window")continue;auto& slot=state.global(d.name);if(d.init){
    if(d.init->kind==Expr::List){if(!d.object)fail(d.loc,"tuple initializer requires array/vector");auto& o=state.require(d.object);
     if(o.kind==Object::Kind::Font||o.kind==Object::Kind::Bmap){Property prop;prop.name=d.type;for(auto& e:d.init->args)prop.arguments.push_back(evaluate(*e));if(!prop.arguments.empty()){auto v=prop.arguments[0].current();if(v.kind!=Value::Kind::Object)fail(d.loc,"asset requires filename/family string");o.text=state.require(v.bits).text;}
      if(o.kind==Object::Kind::Font){if(prop.arguments.size()>1)o.field("style").value=prop.arguments[1].current();if(prop.arguments.size()>2)o.field("size").value=prop.arguments[2].current();}
      else {const char* fields[]={"source_x","source_y","width","height"};for(std::size_t n=1;n<prop.arguments.size()&&n<=4;++n)o.field(fields[n-1]).value=prop.arguments[n].current();}
      o.properties.push_back(std::move(prop));}
     else {if(d.init->args.size()>o.array.size())fail(d.loc,"too many array initializers");for(std::size_t i=0;i<d.init->args.size();++i)o.array[i].value=evaluate(*d.init->args[i]).current();}}
    else {Value v;if((d.type=="var"||d.type=="var_nsave")&&!d.size&&d.init->kind==Expr::Name&&d.init->op!="off"&&d.init->op!="on"&&d.init->op!="null"){diagnostics.push_back(d.init->loc.str()+": legacy scalar initializer atof('"+d.init->op+"') = 0 (00420990/0041a1b0 mode 0)");v={};}else v=evaluate(*d.init).current();
     if(d.object&&d.type!="var"&&d.type!="var_nsave"){auto& o=state.require(d.object);if(v.kind==Value::Kind::Object){auto& src=state.require(v.bits);o.text=src.text;}else o.field("value").value=v;}else if(d.object){auto& o=state.require(d.object);if(o.array.empty())fail(d.loc,"initializer for zero-length array");o.array[0].value=v;}else slot.value=v;}
   }
   if(d.object){auto& o=state.require(d.object);for(auto& [name,exprs]:d.properties){Property property;property.name=name;
     if(name=="flags"){for(auto& e:exprs){if(e->kind==Expr::Name){auto ref=declaration_member(o.id,e->op,e->loc);if(!ref.bitMask)fail(e->loc,"unknown declaration flag '"+e->op+"'");ref.set(Value::from_int(1));property.arguments.push_back({Value::from_raw(std::bit_cast<std::int32_t>(ref.bitMask)),{}});}else {auto a=evaluate(*e);o.field("flags").value.bits|=a.current().bits;property.arguments.push_back(a);}}}
     else {for(std::size_t i=0;i<exprs.size();++i){auto& e=exprs[i];
       // 00418820 calls 004174f0 for DIGITS, numeric-range sliders and WINDOW offsets.
       bool numeric=o.kind==Object::Kind::Panel&&((name=="digits"&&i==5)||((name=="hslider"||name=="vslider")&&exprs.size()>=7&&i==6)||(name=="window"&&(i==5||i==6)));
       if(numeric)property.arguments.push_back(numeric_binding(*e));
       else if((name=="digits"||name=="button"||name=="window"||name=="string"||name=="strings")&&e->kind!=Expr::Literal&&e->kind!=Expr::Name)property.arguments.push_back(deferred_property(*e));
       else property.arguments.push_back(evaluate(*e));
      }if(property.arguments.size()==1){auto ref=declaration_member(o.id,name,exprs.front()->loc);if(ref.slot)ref.set(property.arguments[0].current());}}
     if(o.kind==Object::Kind::Text&&name=="strings"){if(property.arguments.size()==1&&property.arguments[0].current().kind==Value::Kind::Number){auto n=property.arguments[0].current().integer();if(n<0||n>1048576)fail(d.loc,"invalid text string count");o.array.resize(std::size_t(n));for(auto& s:o.array)s.value=state.make_string("");}else{for(auto& a:property.arguments)o.array.push_back({a.current()});}}
     if(o.kind==Object::Kind::Text&&name=="string"){if(property.arguments.size()>o.array.size())o.array.resize(property.arguments.size());for(std::size_t n=0;n<property.arguments.size();++n)o.array[n].value=property.arguments[n].current();}
     o.properties.push_back(std::move(property));
    }}
  }
  for(auto& e:initializers)evaluate(*e);
 }
 void validate_expression(Expr& e,const std::set<std::string,CaseLess>& locals,bool callTarget=false,const Function* f=nullptr){
  if(e.kind==Expr::Name&&!callTarget&&!locals.contains(e.op)&&!state.globals.contains(e.op)&&!names.contains(e.op)&&e.op!="null"&&e.op!="off"&&e.op!="on"&&e.op!="true"&&e.op!="false")fail(e.loc,"unresolved symbol '"+e.op+"'");
  if(e.kind==Expr::Name&&!callTarget&&!locals.contains(e.op)&&!state.globals.contains(e.op)){auto f=names.find(e.op);if(f!=names.end()&&functions[f->second].defined){e.kind=Expr::Literal;e.value=Value::function(f->second);}}
  if(e.kind==Expr::Call&&e.dispatch==Expr::Dynamic&&e.args[0]->kind==Expr::Name){auto& name=e.args[0]->op;if(!locals.contains(name)&&!state.globals.contains(name)){auto f=names.find(name);if(f!=names.end()&&functions[f->second].defined){e.dispatch=Expr::Script;e.value=Value::function(f->second);}else e.dispatch=Expr::Native;}}
  // Evaluation order with a process: ME/YOUR, reference parameters, locals; everything else is a global or function name.
  if(e.kind==Expr::Name&&f&&e.resolution==Expr::Global){
   if(e.op=="me"||e.op=="your"){e.resolution=Expr::Local;e.slot=e.op=="me"?0:1;}
   else if(auto b=std::find(f->bindingSyms.begin(),f->bindingSyms.end(),e.sym);b!=f->bindingSyms.end()){e.resolution=Expr::Binding;e.slot=std::uint32_t(b-f->bindingSyms.begin());}
   else if(auto l=std::find(f->localSyms.begin(),f->localSyms.end(),e.sym);l!=f->localSyms.end()){e.resolution=Expr::Local;e.slot=std::uint32_t(l-f->localSyms.begin());}
  }
  for(std::size_t i=0;i<e.args.size();++i)validate_expression(*e.args[i],locals,e.kind==Expr::Call&&i==0,f);
 }
 void layout(Function& f){
  f.localSyms={"my"_sym,"you"_sym};f.bindingSyms.clear();f.bindingOrder.clear();f.frame.clear();
  auto slot=[&](Sym s){auto found=std::find(f.localSyms.begin(),f.localSyms.end(),s);if(found==f.localSyms.end()){f.localSyms.push_back(s);return std::uint32_t(f.localSyms.size()-1);}return std::uint32_t(found-f.localSyms.begin());};
  for(std::uint32_t i=0;i<f.params.size();++i){const Sym s=Symbols::intern(f.params[i].name);
   if(!f.params[i].reference){f.frame.push_back({FrameStep::Value,i,slot(s),3});continue;}
   auto found=std::find(f.bindingSyms.begin(),f.bindingSyms.end(),s);if(found==f.bindingSyms.end()){f.bindingSyms.push_back(s);found=f.bindingSyms.end()-1;}
   f.frame.push_back({FrameStep::Reference,i,std::uint32_t(found-f.bindingSyms.begin()),0});
  }
  static const std::array<Sym,4> argSyms{"_arg1"_sym,"_arg2"_sym,"_arg3"_sym,"_arg4"_sym};for(std::uint32_t i=0;i<4;++i)f.frame.push_back({FrameStep::Argument,i,slot(argSyms[i]),3});
  for(auto& [name,size]:f.localDefinitions){const Sym s=Symbols::intern(name);if(std::find(f.localSyms.begin(),f.localSyms.end(),s)==f.localSyms.end())f.frame.push_back({FrameStep::Local,0,slot(s),size});}
  for(auto& i:f.code)if(i.kind==Instruction::Declare)i.slot=slot(Symbols::intern(i.name));
  f.bindingOrder.resize(f.bindingSyms.size());for(std::uint32_t i=0;i<f.bindingOrder.size();++i)f.bindingOrder[i]=i;
  std::sort(f.bindingOrder.begin(),f.bindingOrder.end(),[&](auto a,auto b){return CaseLess{}(Symbols::name(f.bindingSyms[a]),Symbols::name(f.bindingSyms[b]));});
  f.laidOut=true;
 }
 void validate_functions(){for(FunctionId id=1;id<functions.size();++id){auto& f=functions[id];if(!f.defined&&!f.external)fail(f.loc,"function prototype '"+f.name+"' has no definition");if(!f.defined)continue;std::set<std::string,CaseLess> locals={"my","you","me","your","_arg1","_arg2","_arg3","_arg4"};for(auto& p:f.params)locals.insert(p.name);for(auto& local:f.localDefinitions)locals.insert(local.first);layout(f);for(auto& i:f.code)if(i.expr)validate_expression(*i.expr,locals,false,&f);}}
 void recycle(Object& object){if(object.leased&&!object.leaseOwners&&!object.leaseReferences&&!object.leaseAvailable){object.leaseAvailable=true;for(auto& slot:object.array)slot.value={};object.alive=false;freeLocals.push_back(object.id);}}
 void hold_reference(const Argument& arg){auto* o=state.get(arg.ref.owner);if(o&&o->leased)++o->leaseReferences;else state.retain_reference(arg.ref);}
 // Bindings are released in CaseLess name order and locals in creation order: recycling order decides which storage object a later local gets.
 void release(Process& p){const auto& order=functions[p.function].bindingOrder;for(auto i:order){if(i>=p.bindings.size())continue;const auto& arg=p.bindings[i];auto* o=state.get(arg.ref.owner);if(o&&o->leased){--o->leaseReferences;recycle(*o);}else state.release_reference(arg.ref);}for(auto id:p.ownedLocals){auto& o=state.require(id);--o.leaseOwners;recycle(o);}for(auto id:p.retainedContext)state.release_object(id);p.retainedContext={};p.ownedLocals.clear();p.bindings.clear();}
 Object& local_storage(Process& p,Sym name,std::size_t size,bool scalarValue=false){
  Object* object;if(freeLocals.empty()){object=&state.create(Object::Kind::Vector,std::string(Symbols::name(name)));object->leased=true;object->leaseOwners=object->leaseReferences=0;leasedLocals.push_back(object);}else{object=&state.require(freeLocals.back());freeLocals.pop_back();}
  object->leaseAvailable=false;object->leaseScalar=scalarValue;++object->leaseOwners;object->alive=true;if(object->array.size()!=size)object->array.resize(size);for(auto& slot:object->array)slot.value={};p.ownedLocals.push_back(object->id);return *object;
 }
 Slot& context_slot(Process& p,std::size_t which){return *p.locals[which].cell;} // 0 my, 1 you
 void clear_context(Process& p,std::size_t which){(which==0?p.context.my:p.context.you)=0;auto id=p.locals[which].object->id;context_slot(p,which).value=Value::object(0);for(auto& snapshot:p.snapshots)if(snapshot.object==id)p.savedLocals[snapshot.offset]=Value::object(0);}
 void clear_you(Process& p){clear_context(p,1);}
 // Returns false when the bank resolver skips the call (nothing allocated).
 bool prepare(Process& p,FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){
  if(!id||id>=functions.size()||!functions[id].defined)throw std::runtime_error("undefined WDL function "+std::to_string(id));
  if(resolver){p.bank=resolver(id,my,liveBank);if(p.bank==Runtime::BankSkip)return false;}
  p.context={nextProcess++,my,you};p.retainedContext={my,you};for(auto context:p.retainedContext)state.retain_object(context);p.function=id;
  p.schedulerFlags=functions[id].schedulerFlags;
  auto& f=functions[id];if(!f.laidOut)layout(f);p.ownedLocals.reserve(f.params.size()+6+f.localDefinitions.size());p.locals.assign(f.localSyms.size(),Local{});p.bindings.assign(f.bindingSyms.size(),Argument{});
  auto value_slot=[&](std::uint32_t slot,Value value){auto& object=local_storage(p,f.localSyms[slot],3,true);auto& cell=object.array[0];cell.value=std::move(value);p.locals[slot]={&object,&cell};};
  try{value_slot(0,Value::object(my));value_slot(1,Value::object(you));
   for(const auto& step:f.frame)switch(step.kind){
    case FrameStep::Value:value_slot(step.slot,step.source<args.size()?args[step.source].current():Value{});break;
    case FrameStep::Reference:{auto a=step.source<args.size()?args[step.source]:Argument{};if(!a.ref)fail(f.loc,"reference parameter "+f.params[step.source].name+" requires lvalue");a.captured=false;p.bindings[step.slot]=a;hold_reference(a);break;}
    case FrameStep::Argument:value_slot(step.slot,step.source<args.size()?args[step.source].current():Value{});break;
    case FrameStep::Local:{auto& object=local_storage(p,f.localSyms[step.slot],step.size);p.locals[step.slot]={&object,&object.array[0]};break;}
   }
  }catch(...){release(p);throw;}
  return true;
 }
 std::unique_ptr<Process> frame(FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){auto p=std::make_unique<Process>();if(!prepare(*p,id,args,my,you))return {};return p;}
 void capture(const Process& p){
  if(suppressContinuations||p.noContinuation)return;auto snapshot=std::make_unique<Process>(p);snapshot->running=false;snapshot->yielded=false;snapshot->snapshots.clear();snapshot->savedLocals.clear();
  // A7 100145c0 copies function+0x1c into each newly allocated continuation.
  snapshot->schedulerFlags=functions[p.function].schedulerFlags;
  snapshot->retainedContext={snapshot->context.my,snapshot->context.you};for(auto context:snapshot->retainedContext)state.retain_object(context);
  std::size_t count=0;for(auto id:snapshot->ownedLocals)count+=state.require(id).array.size();snapshot->snapshots.reserve(snapshot->ownedLocals.size());snapshot->savedLocals.reserve(count);
  for(auto id:snapshot->ownedLocals){auto& object=state.require(id);snapshot->snapshots.push_back({id,snapshot->savedLocals.size(),object.array.size()});for(auto& slot:object.array)snapshot->savedLocals.push_back(slot.value);++object.leaseOwners;}
  for(auto& arg:snapshot->bindings)hold_reference(arg);try{queued.push_back(std::move(snapshot));}catch(...){if(snapshot)release(*snapshot);throw;}
 }
 // start() frames are reused (one per call depth): a script call or particle callback allocates nothing once warm.
 struct Frame {
  Impl& impl;std::unique_ptr<Process> process;
  explicit Frame(Impl& i):impl(i){if(impl.spareFrames.empty())process=std::make_unique<Process>();else{process=std::move(impl.spareFrames.back());impl.spareFrames.pop_back();}}
  ~Frame(){auto& p=*process;p.context={};p.function=0;p.schedulerFlags=0;p.pc=0;p.locals.clear();p.bindings.clear();p.ownedLocals.clear();p.snapshots.clear();p.savedLocals.clear();p.countdown=0;p.bank=0;p.yielded=p.cancelled=p.noContinuation=p.running=false;p.result={};try{impl.spareFrames.push_back(std::move(process));}catch(...){}}
 };
 Value start(FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){
  if(++callDepth>256){--callDepth;throw std::runtime_error("WDL call stack exceeds 256");}
  Frame frame(*this);Process& p=*frame.process;try{if(!prepare(p,id,args,my,you)){--callDepth;return {};}run(p);}catch(...){release(p);--callDepth;throw;}--callDepth;auto value=p.result;release(p);return value;
 }
 void run(Process& p){
  for(auto& snapshot:p.snapshots){auto& object=state.require(snapshot.object);for(std::size_t i=0;i<snapshot.count;++i)object.array[i].value=p.savedLocals[snapshot.offset+i];}p.snapshots.clear();p.savedLocals.clear();
  const int previousBank=liveBank;if(resolver)use_bank((p.bank==Runtime::BankDynamic?resolver(p.function,p.context.my,Runtime::BankDynamic):p.bank)==1?1:0);
  auto* previous=active;active=&p;auto& f=functions[p.function];p.yielded=false;p.running=true;
  try{while(p.pc<f.code.size()&&!p.yielded&&!p.cancelled){auto& i=f.code[p.pc++];switch(i.kind){
   case Instruction::Eval:p.result=value(*i.expr,&p);break;
   case Instruction::Jump:p.pc=i.target;break;
   // A6 interpreter 00410a80 case5 stores the condition into accumulator 006445b4 before jumping; falling off the end returns it (:845).
   case Instruction::Branch:p.result=value(*i.expr,&p);if(!p.result)p.pc=i.target;break;
   case Instruction::Return:if(i.expr)p.result=value(*i.expr,&p);p.pc=f.code.size();break;
   case Instruction::Late:f.schedulerFlags|=2;break;
   case Instruction::Declare:{auto initial=i.expr&&i.expr->kind!=Expr::List?value(*i.expr,&p):Value{};auto* object=p.locals[i.slot].object;if(std::find(p.ownedLocals.begin(),p.ownedLocals.end(),object->id)==p.ownedLocals.end()){object=&local_storage(p,f.localSyms[i.slot],i.size?i.size:3);p.locals[i.slot]={object,&object->array[0]};}auto& o=*object;for(auto& s:o.array)s.value={};if(i.expr&&i.expr->kind==Expr::List){if(i.expr->args.size()>o.array.size())fail(i.loc,"too many local array initializers");for(std::size_t n=0;n<i.expr->args.size();++n)o.array[n].value=value(*i.expr->args[n],&p);}else if(!o.array.empty())o.array[0].value=initial;break;}
  }}p.context.my=context_slot(p,0).value.bits;p.context.you=context_slot(p,1).value.bits;}catch(...){p.running=false;active=previous;if(resolver)use_bank(previousBank);throw;}p.running=false;active=previous;if(resolver)use_bank(previousBank);
 }
 void tick(std::int32_t raw,std::int32_t freeze){
  const bool bonus=state.profile==SemanticProfile::BonusA7;
  const auto freezeGlobal=state.globals.find("freeze_mode"_sym);
  // A6 004120ec and A7 10016710 reread the source cell for every eligible record.
  // Standalone callers without registered globals retain the supplied freeze value.
  const auto freezeValue=[&](){return freezeGlobal==state.globals.end()?freeze:freezeGlobal->second.value.raw();};
  suppressContinuations=false;time=raw;state.global("time"_sym).value=Value::from_raw(raw);if(freezeValue()>=2048)return;
  auto pending=std::move(queued);queued.clear();pendingQueue=&pending;
  try{for(int pass=0;pass<(bonus?3:2);++pass)for(auto& p:pending){if(!p)continue;if(p->cancelled){release(*p);p.reset();continue;}
   const auto flags=bonus?p->schedulerFlags:functions[p->function].schedulerFlags;
   // A7 10016710: bit1 first, neither bit1/bit2 second, bit2 third.
   if(bonus?(pass==0?!(flags&1):pass==1?(flags&3)!=0:!(flags&2)):bool(flags&2)!=bool(pass))continue;
   const auto currentFreeze=freezeValue();
   if(currentFreeze>=2048||(currentFreeze>=1024&&p->context.my)){queued.push_back(std::move(p));continue;}
   auto c=p->countdown;if(c>=1024){p->countdown=c-1024;queued.push_back(std::move(p));continue;}
   if(c<=-std::min(raw,1024)){p->countdown=std::int32_t(std::min(std::int64_t(c)+raw,std::int64_t(0)));queued.push_back(std::move(p));continue;}
   run(*p);release(*p);p.reset();
  }}catch(...){for(auto& p:pending)if(p)release(*p);pendingQueue=nullptr;throw;}
  for(auto& p:pending)if(p)release(*p);pendingQueue=nullptr;
 }
 void load(std::string_view entry,std::string_view text,Source s,Resource r){
  for(auto& p:queued)if(p)release(*p);
  disable_banks();clear_layouts();
  source=std::move(s);resource=std::move(r);metrics={};tokens.clear();macros.clear();declarations.clear();initializers.clear();functions.clear();functions.emplace_back();names.clear();queued.clear();diagnostics.clear();pos=0;includeDepth=0;compactExpressions=false;compilerSettings={};compilerSettings.entry=entry;suppressContinuations=false;
  for(auto& [name,value]:state.compilerMacros){ParseMetrics scratch;auto ts=lex(value,"<compiler DEFINE "+name+">",scratch);ts.pop_back();macros.emplace(name,std::move(ts));}
  ++metrics.files;preprocess(entry,text);tokens.push_back({Token::End,"",{std::string(entry),metrics.lines,1}});parse();initialize();
 }
};
Runtime::Runtime(State& state,Builtin builtin):impl_(std::make_unique<Impl>(*this,state,std::move(builtin))){}
Runtime::~Runtime()=default;
void Runtime::load(std::string_view entry,Source source,Resource resource){auto text=source(entry);impl_->load(entry,text,std::move(source),std::move(resource));}
void Runtime::load_text(std::string_view name,std::string_view text){impl_->load(name,text,{},{});}
Value Runtime::start(FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){return impl_->start(id,args,my,you);}
Value Runtime::start(std::string_view name,std::span<const Argument> args,ObjectId my,ObjectId you){auto id=function(name);if(!id)throw std::runtime_error("unknown WDL function "+std::string(name));return start(id,args,my,you);}
void Runtime::enqueue(FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){if(auto p=impl_->frame(id,args,my,you))impl_->queued.push_back(std::move(p));}
void Runtime::bootstrap(){auto main=function("main");if(!main)throw std::runtime_error("WDL entry has no MAIN");enqueue(main);for(std::size_t id=impl_->functions.size();id-->1;)if(impl_->functions[id].starter&&impl_->functions[id].defined)enqueue(FunctionId(id));}
void Runtime::tick(std::int32_t raw,std::int32_t freeze){impl_->tick(raw,freeze);}
void Runtime::kill_for_entity(ObjectId id){
 if(!id)return;
 // Preserve native universal cancellation/safety; original 00431920 gates cancellation too.
 // Only the missing registered-cell operation below adopts that A6 event-mask gate.
 auto cancel=[&](auto& queue){for(auto& p:queue)if(p){if(p->context.my==id){if(p->running)p->noContinuation=true;else p->cancelled=true;}if(p->context.you==id)impl_->clear_you(*p);}};
 cancel(impl_->queued);if(impl_->pendingQueue)cancel(*impl_->pendingQueue);
 if(impl_->active){if(impl_->active->context.my==id)impl_->active->noContinuation=true;if(impl_->active->context.you==id)impl_->clear_you(*impl_->active);}
 // A6 00431976/00431986: registered pointer NULL follows cancellation, before retirement.
 // This source gate is the whole event mask; it does not establish A7 cleanup.
 const auto* object=impl_->state.get(id);
 if(impl_->state.profile!=SemanticProfile::CampaignA6||!object||object->kind!=Object::Kind::Entity)return;
 const auto mask=object->fields.find("event_flags");if(mask==object->fields.end()||!mask->second.value.bits)return;
 for(auto& declaration:impl_->declarations)if(auto* cell=declaration.pointerCell;cell&&cell->value.kind==Value::Kind::Object&&cell->value.bits==id)cell->value={};
 for(auto& b:impl_->banked)if(b.pointer&&b.value.kind==Value::Kind::Object&&b.value.bits==id)b.value={};
 if(impl_->active&&impl_->active->context.my==id)impl_->clear_context(*impl_->active,0);
}
void Runtime::enable_banks(std::span<const std::string> names,BankResolver resolver){
 auto& m=*impl_;m.disable_banks();if(names.empty())return;if(!resolver)throw std::invalid_argument("enable_banks requires a resolver");
 for(const auto& name:names){
  if(m.bankIndex.contains(name))continue;auto global=m.state.globals.find(name);if(global==m.state.globals.end())throw std::invalid_argument("bank global '"+name+"' is not a global");
  auto d=std::find_if(m.declarations.begin(),m.declarations.end(),[&](auto& d){return case_equal(d.name,name);});Impl::Banked b;
  if(d==m.declarations.end()||d->pointer||d->type=="function"||((d->type=="var"||d->type=="var_nsave")&&!d->size)){b.slot=&global->second;b.pointer=d!=m.declarations.end()&&d->pointerCell;b.value=global->second.value;}
  else if(d->object&&d->type=="string"){b.object=d->object;b.text=true;b.string=m.state.require(d->object).text;}
  else if(d->object&&(d->type=="var"||d->type=="var_nsave"||d->type=="vector")){b.object=d->object;for(auto& s:m.state.require(d->object).array)b.values.push_back(s.value);}
  else {m.disable_banks();throw std::invalid_argument("bank global '"+name+"' has unsupported type "+d->type);}
  m.bankIndex.emplace(name,m.banked.size());m.banked.push_back(std::move(b));
 }
 m.resolver=std::move(resolver);
}
int Runtime::active_bank()const{return impl_->liveBank;}
Value Runtime::bank_value(int bank,std::string_view global)const{
 auto& m=*impl_;if(auto it=m.bankIndex.find(global);it!=m.bankIndex.end()&&m.banked[it->second].slot&&bank!=m.liveBank)return m.banked[it->second].value;
 auto g=m.state.globals.find(global);return g==m.state.globals.end()?Value{}:g->second.value;
}
void Runtime::set_bank_value(int bank,std::string_view global,Value v){
 auto& m=*impl_;if(auto it=m.bankIndex.find(global);it!=m.bankIndex.end()&&m.banked[it->second].slot&&bank!=m.liveBank){m.banked[it->second].value=std::move(v);return;}
 auto g=m.state.globals.find(global);if(g==m.state.globals.end())throw std::invalid_argument("unknown global '"+std::string(global)+"'");g->second.value=std::move(v);
}
std::vector<std::string> Runtime::assigned_globals(std::span<const std::string_view> files)const{
 const auto& m=*impl_;std::set<std::string,CaseLess> out;
 // Non-pointer storage whose elements/components/text a member, index or vec_/str_ write changes in place.
 auto storage=[&](std::string_view n){return std::any_of(m.declarations.begin(),m.declarations.end(),[&](auto& d){return case_equal(d.name,n)&&!d.pointer&&(d.type=="var"||d.type=="var_nsave"||d.type=="vector"||d.type=="string");});};
 static constexpr std::string_view readers[]={"str_cmp","str_cmpi","str_cmpni","str_create","str_cursor","str_len","str_remove","str_stri","str_to_asc","str_to_num","vec_dist","vec_dot","vec_length","vec_to_mesh","vec_to_uv"};
 auto writes_first=[&](std::string_view n){return (n.starts_with("vec_")||n.starts_with("str_"))&&std::none_of(std::begin(readers),std::end(readers),[&](auto r){return case_equal(r,n);});};
 for(const auto& f:m.functions){
  if(!f.defined||std::none_of(files.begin(),files.end(),[&](auto x){return case_equal(x,function_file_name(f));}))continue;
  std::set<std::string,CaseLess> locals={"my","you","me","your","_arg1","_arg2","_arg3","_arg4"};for(auto& p:f.params)locals.insert(p.name);for(auto& l:f.localDefinitions)locals.insert(l.first);
  auto global=[&](const Expr& e){return e.kind==Expr::Name&&!locals.contains(e.op)&&m.state.globals.contains(e.op);};
  std::function<void(const Expr&)> walk=[&](const Expr& e){
   const Expr* target=nullptr;
   if((e.kind==Expr::Binary&&precedence(e.op)==1)||(e.kind==Expr::Unary&&(e.op.ends_with("++")||e.op.ends_with("--"))))target=e.args[0].get();
   else if(e.kind==Expr::Call&&e.args.size()>1&&e.args[0]->kind==Expr::Name&&writes_first(e.args[0]->op)&&global(*e.args[1])&&storage(e.args[1]->op))out.insert(e.args[1]->op);
   if(target){if(global(*target))out.insert(target->op);else if((target->kind==Expr::Member||target->kind==Expr::Index)&&global(*target->args[0])&&storage(target->args[0]->op))out.insert(target->args[0]->op);}
   for(auto& a:e.args)if(a)walk(*a);
  };
  for(auto& i:f.code)if(i.expr)walk(*i.expr);
 }
 return {out.begin(),out.end()};
}
std::string_view Runtime::function_file(FunctionId id)const{return id&&id<impl_->functions.size()?function_file_name(impl_->functions[id]):std::string_view{};}
std::string_view Runtime::function_name(FunctionId id)const{return id&&id<impl_->functions.size()?std::string_view(impl_->functions[id].name):std::string_view{};}
void Runtime::late_current(){if(!impl_->active)throw std::runtime_error("proc_late outside current process");impl_->functions[impl_->active->function].schedulerFlags|=2;}
State& Runtime::state(){return impl_->state;}
FunctionId Runtime::function(std::string_view name)const{auto it=impl_->names.find(name);return it==impl_->names.end()?0:it->second;}
bool Runtime::external_import(std::string_view name)const{auto it=impl_->names.find(name);return it!=impl_->names.end()&&impl_->functions[it->second].external&&!impl_->functions[it->second].defined;}
const std::vector<std::string>& Runtime::diagnostics()const{return impl_->diagnostics;}
const ParseMetrics& Runtime::metrics()const{return impl_->metrics;}
std::size_t Runtime::processes()const{return std::count_if(impl_->queued.begin(),impl_->queued.end(),[](auto& p){return !p->cancelled;});}
bool Runtime::running(FunctionId id)const{return id&&std::any_of(impl_->queued.begin(),impl_->queued.end(),[id](auto& p){return !p->cancelled&&p->function==id;});}
const Runtime::CompilerSettings& Runtime::compiler_settings()const{return impl_->compilerSettings;}
std::uint64_t Runtime::digest()const{
 const auto& m=*impl_;std::uint64_t h=digest_mix(m.nextProcess,(std::uint64_t(std::uint32_t(m.time))<<8)|std::uint64_t(m.liveBank*2+int(m.suppressContinuations)));
 for(const auto& f:m.functions)h=digest_mix(h,f.schedulerFlags);
 for(const auto& p:m.queued){
  if(!p){h=digest_mix(h,0);continue;}
  h=digest_mix(h,p->context.processId);h=digest_mix(h,(std::uint64_t(p->context.my)<<32)|p->context.you);h=digest_mix(h,(std::uint64_t(p->retainedContext[0])<<32)|p->retainedContext[1]);
  h=digest_mix(h,(std::uint64_t(p->function)<<32)|p->schedulerFlags);h=digest_mix(h,p->pc);h=digest_mix(h,(std::uint64_t(std::uint32_t(p->countdown))<<8)|std::uint64_t(p->bank+2)<<4|std::uint64_t(p->yielded*8+p->cancelled*4+p->noContinuation*2+p->running));
  const auto& f=m.functions[p->function];
  std::uint64_t locals=0;for(std::size_t i=0;i<p->locals.size();++i)locals+=digest_mix(digest_value(Symbols::hash(f.localSyms[i]),Value::object(p->locals[i].object->id)),0);h=digest_mix(h,locals);
  std::uint64_t bindings=0;for(std::size_t i=0;i<p->bindings.size();++i){const auto& arg=p->bindings[i];bindings+=digest_mix(digest_ref(digest_value(Symbols::hash(f.bindingSyms[i]),arg.value),arg.ref),arg.captured);}h=digest_mix(h,bindings);
  for(auto id:p->ownedLocals)h=digest_mix(h,id);
  for(const auto& s:p->snapshots)h=digest_mix(digest_mix(h,(std::uint64_t(s.object)<<32)|s.count),s.offset);
  for(const auto& v:p->savedLocals)h=digest_value(h,v);
  h=digest_value(h,p->result);
 }
 std::uint64_t leases=0;for(const auto* o:m.leasedLocals)leases+=digest_mix(digest_mix(o->id,o->leaseOwners),std::uint64_t(o->leaseReferences)*4+o->leaseAvailable*2+o->leaseScalar);h=digest_mix(h,leases);
 for(auto id:m.freeLocals)h=digest_mix(h,id);
 for(const auto& b:m.banked){h=digest_value(digest_mix(h,(std::uint64_t(b.object)<<2)|std::uint64_t(b.text*2+b.pointer)),b.value);for(const auto& v:b.values)h=digest_value(h,v);h=digest_text(h,b.string);}
 return h;
}
std::size_t Runtime::reroute_reads(std::string_view global,std::string_view call,std::span<const std::string_view> functions,std::span<const std::string_view> arrays,std::span<const std::string_view> files){
 std::size_t count=0;
 auto assignment=[](const Expr& e){return (e.kind==Expr::Binary&&precedence(e.op)==1)||(e.kind==Expr::Unary&&(e.op.ends_with("++")||e.op.ends_with("--")));};
 std::function<void(E&,bool,bool)> walk=[&](E& e,bool all,bool target){
  if(!e)return;
  if(e->kind==Expr::Name&&all&&!target&&case_equal(e->op,global)){e=native_call(e->loc,call);++count;return;}
  e->special=Expr::Unlinked;
  const bool indexed=e->kind==Expr::Index&&e->args[0]->kind==Expr::Name&&std::any_of(arrays.begin(),arrays.end(),[&](auto a){return case_equal(a,e->args[0]->op);});
  for(std::size_t i=0;i<e->args.size();++i)walk(e->args[i],all||(indexed&&i==1),i==0&&assignment(*e));
 };
 auto in_file=[&](const Function& f){return std::any_of(files.begin(),files.end(),[&](auto x){return case_equal(x,function_file_name(f));});};
 for(auto& f:impl_->functions){const bool all=in_file(f)||std::any_of(functions.begin(),functions.end(),[&](auto n){return case_equal(n,f.name);});for(auto& ins:f.code)walk(ins.expr,all,false);}
 return count;
}
std::size_t Runtime::reroute_assigned_read(std::string_view function,std::string_view field,std::string_view global,std::string_view call){
 std::size_t count=0;
 std::function<void(E&)> walk=[&](E& e){
  if(!e)return;
  if(e->kind==Expr::Binary&&e->op=="="&&e->args[0]->kind==Expr::Member&&case_equal(e->args[0]->op,field)&&e->args[1]->kind==Expr::Name&&case_equal(e->args[1]->op,global)){
   auto& r=e->args[1];r=native_call(r->loc,call);++count;return;}
  for(auto& a:e->args)walk(a);
 };
 for(auto& f:impl_->functions)if(case_equal(f.name,function))for(auto& ins:f.code)walk(ins.expr);
 return count;
}
std::size_t Runtime::wrap_assigned(std::string_view field,std::string_view call,std::span<const std::string_view> files){
 std::size_t count=0;
 std::function<void(E&)> walk=[&](E& e){
  if(!e)return;
  if(e->kind==Expr::Binary&&e->op=="="&&e->args[0]->kind==Expr::Member&&case_equal(e->args[0]->op,field)){
   auto& r=e->args[1];auto c=native_call(r->loc,call);c->args.push_back(std::move(r));r=std::move(c);++count;return;}
  for(auto& a:e->args)walk(a);
 };
 for(auto& f:impl_->functions)if(std::any_of(files.begin(),files.end(),[&](auto x){return case_equal(x,function_file_name(f));}))for(auto& ins:f.code)walk(ins.expr);
 return count;
}
}
