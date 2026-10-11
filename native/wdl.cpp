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
struct Expr {enum Kind{Literal,Name,Unary,Binary,Call,Member,Index,Conditional,List}kind=Literal;enum Dispatch{Dynamic,Native,Script}dispatch=Dynamic;Location loc;std::string op;Value value;std::vector<std::unique_ptr<Expr>> args;};
using E=std::unique_ptr<Expr>;
struct Instruction {enum Kind{Eval,Jump,Branch,Declare,Return,Late}kind=Eval;E expr;std::size_t target=0;std::string name;std::size_t size=0;Location loc;};
struct Parameter {std::string name;bool reference=false;};
struct Function {std::string name;Location loc;std::vector<Parameter> params;std::vector<std::pair<std::string,std::size_t>> localDefinitions;std::vector<Instruction> code;std::map<std::string,std::size_t,CaseLess> labels;std::vector<std::pair<std::size_t,std::string>> gotos;std::uint32_t schedulerFlags=0;bool starter=false,defined=false,external=false;};
struct Declaration {std::string type,name;Location loc;bool pointer=false;E size,init;std::vector<std::pair<std::string,std::vector<E>>> properties;ObjectId object=0;Slot* pointerCell=nullptr;};
struct LocalSnapshot {ObjectId object;std::size_t offset,count;};
struct Process {ProcessContext context;std::array<ObjectId,2> retainedContext{};FunctionId function=0;std::uint32_t schedulerFlags=0;std::size_t pc=0;Fields locals;std::map<std::string,Argument,CaseLess> bindings;std::vector<ObjectId> ownedLocals;std::vector<LocalSnapshot> snapshots;std::vector<Value> savedLocals;std::int32_t countdown=0;bool yielded=false,cancelled=false,noContinuation=false,running=false;Value result;};
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
 std::deque<std::unique_ptr<Process>> queued;std::deque<std::unique_ptr<Process>>* pendingQueue=nullptr;Process* active=nullptr;std::uint64_t nextProcess=1;std::int32_t time=1024;std::size_t callDepth=0;
 struct LocalLease {std::size_t owners=0,references=0;bool available=false,scalarValue=false;};std::unordered_map<ObjectId,LocalLease> localLeases;std::vector<ObjectId> freeLocals;
 Impl(Runtime& r,State& s,Builtin b):runtime(r),state(s),builtin(std::move(b)){functions.emplace_back();}
 ~Impl(){for(auto& p:queued)if(p)release(*p);clear_layouts();}
 void clear_layouts(){for(auto& d:declarations)if(d.object)if(auto* object=state.get(d.object)){object->properties.clear();object->alive=false;}}
 const Token& peek(std::size_t n=0)const{return tokens.at(std::min(pos+n,tokens.size()-1));}
 std::string_view expression_spelling()const{const auto& first=peek();if(compactExpressions&&first.kind==Token::Punct&&first.text.size()==1){const auto& second=peek(1);if(second.kind==Token::Punct&&!second.text.empty()){auto pair=paired_operator(first.text[0],second.text[0]);if(!pair.empty())return pair;}}return first.text;}
 void advance_spelling(){bool joined=expression_spelling()!=peek().text;++pos;if(joined){auto& rest=tokens.at(pos);if(rest.text.size()==1)++pos;else{rest.text.erase(0,1);++rest.loc.column;}}}
 bool is(std::string_view s)const{return (peek().kind==Token::Word||peek().kind==Token::Punct)&&expression_spelling()==s;}bool take(std::string_view s){if(is(s)){advance_spelling();return true;}return false;}
 Token consume(){return tokens.at(pos++);}void expect(std::string_view s){if(!take(s))fail(peek().loc,"expected '"+std::string(s)+"', got '"+peek().text+"'");}
 std::string identifier(){if(peek().kind!=Token::Word)fail(peek().loc,"expected identifier");return consume().text;}
 Token expression_token(){auto spelling=expression_spelling();auto t=peek();if(t.text!=spelling)t.text=spelling;advance_spelling();if(compactExpressions&&(t.kind==Token::Word||t.kind==Token::Number))while(peek().kind==Token::Word||peek().kind==Token::Number)t.text+=consume().text;return t;}
 std::string expression_identifier(){if(peek().kind!=Token::Word)fail(peek().loc,"expected identifier");return expression_token().text;}
 bool scalar_storage(ObjectId id)const{auto found=localLeases.find(id);return found!=localLeases.end()&&found->second.scalarValue;}
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
 E node(Expr::Kind kind,const Location& loc,std::string op={}){++metrics.expressions;auto e=std::make_unique<Expr>();e->kind=kind;e->loc=loc;e->op=std::move(op);return e;}
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
 Argument evaluate(const Expr& e,Process* p=nullptr){
  auto result=[](Value v,Ref ref={}){return Argument{v,ref};};
  auto arg=[&](std::size_t i){return evaluate(*e.args.at(i),p);};
  const std::string_view opv=e.op; // size-first compares; std::string==const char* is strlen+memcmp
  try{switch(e.kind){
   case Expr::Literal:return result(e.value);
   case Expr::Name:{
    if(opv=="null"||opv=="off"||opv=="false")return result(Value{});
    if(opv=="on"||opv=="true")return result(Value::from_int(1));
    auto slotArgument=[&](Slot& slot)->Argument{if(slot.value.kind==Value::Kind::Object){auto* o=state.get(slot.value.bits);if(o&&o->kind==Object::Kind::Vector&&!o->array.empty())return {o->array[0].value,{&o->array[0],o->id,{},0}};}return {slot.value,{&slot,slot.value.kind==Value::Kind::Object?slot.value.bits:0}};};
    if(p){if(opv=="me"||opv=="your")return slotArgument(p->locals.at(opv=="me"?"my":"you"));auto bound=p->bindings.find(e.op);if(bound!=p->bindings.end())return {bound->second.current(),bound->second.ref};auto it=p->locals.find(e.op);if(it!=p->locals.end())return slotArgument(it->second);}
    auto it=state.globals.find(e.op);if(it!=state.globals.end()){auto view=state.numericGlobals.find(e.op);if(view!=state.numericGlobals.end())return result(it->second.value,{&it->second,view->second,"x"});return slotArgument(it->second);}
    auto fn=names.find(e.op);if(fn!=names.end())return result(Value::function(fn->second));
    fail(e.loc,"unknown symbol '"+e.op+"'");
   }
   case Expr::Member:{
    auto a=arg(0);auto v=a.current();ObjectId id=v.kind==Value::Kind::Object?v.bits:a.ref.owner;auto* owner=state.get(a.ref.owner);if(owner&&owner->kind==Object::Kind::Vector&&scalar_storage(owner->id)&&v.kind!=Value::Kind::Object&&opv!="x"&&opv!="y"&&opv!="z"&&opv!="pan"&&opv!="tilt"&&opv!="roll"&&opv!="blue"&&opv!="green"&&opv!="red")fail(e.loc,"member '"+e.op+"' on non-object parameter");if(owner&&owner->kind==Object::Kind::Vector&&e.args[0]->kind==Expr::Name&&(!scalar_storage(owner->id)||v.kind!=Value::Kind::Object))id=owner->id;if(!id)fail(e.loc,"member '"+e.op+"' on null/non-object");auto& o=state.require(id);
    bool component=opv=="x"||opv=="y"||opv=="z"||opv=="pan"||opv=="tilt"||opv=="roll";std::size_t componentIndex=opv=="x"||opv=="pan"?0:opv=="y"||opv=="tilt"?1:2;
    if(component&&a.ref.owner==id&&a.ref.field.starts_with("skill")){int start=0;auto suffix=a.ref.field.substr(5);auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),start);if(parsed.ec!=std::errc{})fail(e.loc,"invalid skill vector base");auto ref=state.member(id,"skill"+std::to_string(start+componentIndex));return result(ref.get(),ref);}
    if(o.kind==Object::Kind::Text&&opv=="string"){auto index=o.field("index").value.integer();if(index<1||std::size_t(index)>o.array.size())return result(Value{}, {nullptr,o.id,"string"});auto n=std::size_t(index-1);return result(o.array[n].value,{&o.array[n],o.id,"string",n});}
    if(o.kind==Object::Kind::Vector&&!o.scalar&&component){
     std::size_t n=(a.ref.owner==id?a.ref.index:0)+componentIndex;if(n>=o.array.size())fail(e.loc,"vector component out of bounds");return result(o.array[n].value,{&o.array[n],o.id,e.op,n});
    }
    auto ref=state.member(o.id,e.op);return result(ref.slot?ref.get():Value::object(o.id),ref);
   }
   case Expr::Index:{
    auto base=arg(0);auto a=base.current();auto n=arg(1).current().integer();ObjectId id=a.kind==Value::Kind::Object?a.bits:base.ref.owner;
    auto* owner=state.get(base.ref.owner);if(owner&&(owner->kind==Object::Kind::Text||(owner->kind==Object::Kind::Vector&&(!scalar_storage(owner->id)||a.kind!=Value::Kind::Object))))id=owner->id;
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
   case Expr::Conditional:{auto c=arg(0).current();return c?arg(1):arg(2);}
   case Expr::Unary:{
    auto a=arg(0);auto v=a.current();
    if(opv=="&"||opv=="*"){if(!a.ref)fail(e.loc,"reference operator on non-reference");return a;}
    if(opv=="+" )return result(v);
    if(opv=="-")return result(Value::from_raw(std::bit_cast<std::int32_t>(0u-v.bits)));
    if(opv=="!")return result(Value::from_int(!bool(v)));
    if(opv=="~")return result(Value::from_raw(std::bit_cast<std::int32_t>(~v.bits)));
    if(!a.ref.slot)fail(e.loc,"increment requires writable reference");bool dec=opv=="--"||opv=="post--";auto next=Value::from_raw(std::bit_cast<std::int32_t>(v.bits+(dec?0u-1024u:1024u)));a.ref.set(next);return opv.starts_with("post")?result(v):result(next,a.ref);
   }
   case Expr::Binary:{
    Argument a,b;Value x,y;if(opv=="%"){b=arg(1);y=b.current();a=arg(0);x=a.current();}else{a=arg(0);x=a.current();b=arg(1);y=b.current();}std::string_view op=e.op;bool assign=op=="="||(op.size()==2&&op[1]=='='&&op!="=="&&op!="!="&&op!="<="&&op!=">=");
    if(op=="="){if(!a.ref.slot)fail(e.loc,"assignment requires writable reference");a.ref.set(y);return result(y,a.ref);}
    if(assign)op=op.substr(0,1);
    auto raw=[](std::uint32_t v){return Value::from_raw(std::bit_cast<std::int32_t>(v));};Value v;
    if(op=="+")v=raw(x.bits+y.bits);else if(op=="-")v=raw(x.bits-y.bits);
    else if(op=="*"){std::int64_t n=std::int64_t(x.raw())*y.raw();if(n<0&&n>=-4294967296LL)n+=1023;v=raw(std::uint32_t(n>>10));}
    else if(op=="/"){if(!y.raw())fail(e.loc,"fixed-point division by zero");std::int64_t n=std::int64_t(x.raw())*1024/y.raw();if(n<std::numeric_limits<std::int32_t>::min()||n>std::numeric_limits<std::int32_t>::max())fail(e.loc,"fixed-point division overflow");v=Value::from_raw(std::int32_t(n));}
    else if(op=="%"){if(!y.raw())fail(e.loc,"fixed-point remainder by zero");std::int32_t left=x.raw();if(left<0){std::int64_t q=0xF424000000LL/y.raw();if(q<std::numeric_limits<std::int32_t>::min()||q>std::numeric_limits<std::int32_t>::max())fail(e.loc,"legacy negative remainder adjustment overflow");auto count=std::uint32_t((std::int32_t(q)>>10)+1);left=std::bit_cast<std::int32_t>(std::uint32_t(left)+count*y.bits);}if(left==std::numeric_limits<std::int32_t>::min()&&y.raw()==-1)fail(e.loc,"fixed-point remainder overflow");v=Value::from_raw(left%y.raw());}
    else if(op=="==")v=Value::from_int(x.bits==y.bits);else if(op=="!=")v=Value::from_int(x.bits!=y.bits);
    else if(op=="<")v=Value::from_int(x.raw()<y.raw());else if(op==">")v=Value::from_int(x.raw()>y.raw());else if(op=="<=")v=Value::from_int(x.raw()<=y.raw());else if(op==">=")v=Value::from_int(x.raw()>=y.raw());
    else if(op=="&&")v=Value::from_int(bool(x)&&bool(y));else if(op=="||")v=Value::from_int(bool(x)||bool(y));
    else if(op=="&")v=raw(x.bits&y.bits);else if(op=="|")v=raw(x.bits|y.bits);else if(op=="^")v=raw(x.bits^y.bits);
    else if(op=="<<")v=raw(x.bits<<(y.integer()&31));else if(op==">>")v=Value::from_raw(x.raw()>>(y.integer()&31));else fail(e.loc,"unknown operator "+std::string(op));
    if(assign){if(!a.ref.slot)fail(e.loc,"compound assignment requires writable reference");a.ref.set(v);return result(v,a.ref);}return result(v);
   }
   case Expr::Call:{
    if(e.args.size()>65)fail(e.loc,"call exceeds 64 arguments");
    // Most calls pass few arguments: avoid constructing/destroying 64 Arguments (~6 KB) per call expression.
    std::array<Argument,8> small;std::vector<Argument> large;Argument* args=small.data();if(e.args.size()-1>small.size()){large.resize(e.args.size()-1);args=large.data();}
    for(std::size_t i=e.args.size();i-->1;){auto& value=args[i-1];value=arg(i);value.value=value.current();value.captured=true;}
    std::span<const Argument> values(args,e.args.size()-1);const Expr& target=*e.args[0];std::string_view name=target.kind==Expr::Name?target.op:std::string_view{};
    if(p){p->context.my=context_slot(*p,"my").value.bits;p->context.you=context_slot(*p,"you").value.bits;}
    if(name=="wait"||name=="waitt"||name=="sleep"){
     if(!p)fail(e.loc,"wait in declaration initializer");if(values.size()!=1)fail(e.loc,"wait requires one argument");auto n=values[0].current().raw();
     auto it=state.globals.find("time");auto t=it==state.globals.end()?time:it->second.value.raw();
     Value returned;
     if(name=="wait"){auto clamped=std::clamp(n,1024,1048575);p->countdown=clamped-1024;returned=Value::from_raw(clamped);}
     else {if(name=="sleep"){n=std::bit_cast<std::int32_t>(std::uint32_t(n)<<4);if(n<1024&&n/2<t)return result(Value{});}auto chosen=n<t?t:std::min(n,1048575);p->countdown=std::bit_cast<std::int32_t>(std::uint32_t(t)-std::uint32_t(chosen));returned=Value::from_raw(chosen);}
     capture(*p);p->yielded=true;return result(returned);
    }
    if(name=="proc_late"){if(!p)fail(e.loc,"proc_late outside process");functions[p->function].schedulerFlags|=2;return result(Value{});}
    if(name=="proc_kill"){
     if(!p||values.size()!=1)fail(e.loc,"proc_kill requires current process and mode");auto mode=values[0].current().raw()>>10;
     auto cancel=[&](auto& queue){for(auto& q:queue)if(q&&!q->running&&(((mode&1)&&p->context.my&&q->context.my==p->context.my)||((mode&2)&&p->context.you&&q->context.my==p->context.you)||((mode&4)&&q->function==p->function)))q->cancelled=true;};cancel(queued);if(pendingQueue)cancel(*pendingQueue);
     if(mode&8)suppressContinuations=true;return result(Value{});
    }
    Value targetValue;bool resolved=e.dispatch==Expr::Script;
    if(e.dispatch==Expr::Script)targetValue=e.value;
    else if(e.dispatch==Expr::Dynamic){
     resolved=name.empty()||state.globals.contains(name)||(p&&(p->locals.contains(name)||p->bindings.contains(name)))||names.contains(name);
     if(resolved)targetValue=arg(0).current();
    }
    // A6 indirect adapter 00412220 returns raw zero for NULL, after argument evaluation.
    if(resolved&&!targetValue.bits)return result(Value{});
    if(resolved&&targetValue.kind!=Value::Kind::Function)fail(e.loc,"call target is not a defined function");
    if(targetValue.kind==Value::Kind::Function&&targetValue.bits){if(targetValue.bits>=functions.size())fail(e.loc,"invalid function pointer");auto& called=functions[targetValue.bits];if(called.defined){auto v=start(targetValue.bits,values,p?p->context.my:0,p?p->context.you:0);return result(v);}if(!called.external)fail(e.loc,"call target '"+called.name+"' has no definition");name=called.name;}
    if(name.empty())fail(e.loc,"call target is not a defined function");
    ProcessContext temporary;auto& context=p?p->context:temporary;
    auto value=builtin(runtime,context,name,values);if(p){context_slot(*p,"my").value=Value::object(context.my);context_slot(*p,"you").value=Value::object(context.you);}return result(value);
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
 void validate_expression(Expr& e,const std::set<std::string,CaseLess>& locals,bool callTarget=false){
  if(e.kind==Expr::Name&&!callTarget&&!locals.contains(e.op)&&!state.globals.contains(e.op)&&!names.contains(e.op)&&e.op!="null"&&e.op!="off"&&e.op!="on"&&e.op!="true"&&e.op!="false")fail(e.loc,"unresolved symbol '"+e.op+"'");
  if(e.kind==Expr::Name&&!callTarget&&!locals.contains(e.op)&&!state.globals.contains(e.op)){auto f=names.find(e.op);if(f!=names.end()&&functions[f->second].defined){e.kind=Expr::Literal;e.value=Value::function(f->second);}}
  if(e.kind==Expr::Call&&e.dispatch==Expr::Dynamic&&e.args[0]->kind==Expr::Name){auto& name=e.args[0]->op;if(!locals.contains(name)&&!state.globals.contains(name)){auto f=names.find(name);if(f!=names.end()&&functions[f->second].defined){e.dispatch=Expr::Script;e.value=Value::function(f->second);}else e.dispatch=Expr::Native;}}
  for(std::size_t i=0;i<e.args.size();++i)validate_expression(*e.args[i],locals,e.kind==Expr::Call&&i==0);
 }
 void validate_functions(){for(FunctionId id=1;id<functions.size();++id){auto& f=functions[id];if(!f.defined&&!f.external)fail(f.loc,"function prototype '"+f.name+"' has no definition");if(!f.defined)continue;std::set<std::string,CaseLess> locals={"my","you","me","your","_arg1","_arg2","_arg3","_arg4"};for(auto& p:f.params)locals.insert(p.name);for(auto& local:f.localDefinitions)locals.insert(local.first);for(auto& i:f.code)if(i.expr)validate_expression(*i.expr,locals);}}
 void recycle(ObjectId id){auto it=localLeases.find(id);if(it!=localLeases.end()&&!it->second.owners&&!it->second.references&&!it->second.available){it->second.available=true;auto& object=state.require(id);for(auto& slot:object.array)slot.value={};object.alive=false;freeLocals.push_back(id);}}
 void hold_reference(const Argument& arg){auto it=localLeases.find(arg.ref.owner);if(it!=localLeases.end())++it->second.references;else state.retain_reference(arg.ref);}
 void release(Process& p){for(auto& [name,arg]:p.bindings){auto it=localLeases.find(arg.ref.owner);if(it!=localLeases.end()){--it->second.references;recycle(it->first);}else state.release_reference(arg.ref);}for(auto id:p.ownedLocals){--localLeases.at(id).owners;recycle(id);}for(auto id:p.retainedContext)state.release_object(id);p.retainedContext={};p.ownedLocals.clear();p.bindings.clear();}
 Object& local_storage(Process& p,std::string_view name,std::size_t size,bool scalarValue=false){
  ObjectId id;if(freeLocals.empty()){auto& object=state.create(Object::Kind::Vector,std::string(name));id=object.id;localLeases.emplace(id,LocalLease{});}else{id=freeLocals.back();freeLocals.pop_back();}
  auto& lease=localLeases.at(id);lease.available=false;lease.scalarValue=scalarValue;++lease.owners;auto& object=state.require(id);object.alive=true;object.array.resize(size);for(auto& slot:object.array)slot.value={};p.ownedLocals.push_back(id);return object;
 }
 Slot& context_slot(Process& p,const char* name){return state.require(p.locals.at(name).value.bits).array[0];}
 void clear_context(Process& p,const char* name){(std::string_view(name)=="my"?p.context.my:p.context.you)=0;auto id=p.locals.at(name).value.bits;context_slot(p,name).value=Value::object(0);for(auto& snapshot:p.snapshots)if(snapshot.object==id)p.savedLocals[snapshot.offset]=Value::object(0);}
 void clear_you(Process& p){clear_context(p,"you");}
 void prepare(Process& p,FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){
  if(!id||id>=functions.size()||!functions[id].defined)throw std::runtime_error("undefined WDL function "+std::to_string(id));
  p.context={nextProcess++,my,you};p.retainedContext={my,you};for(auto context:p.retainedContext)state.retain_object(context);p.function=id;
  p.schedulerFlags=functions[id].schedulerFlags;
  auto& f=functions[id];p.ownedLocals.reserve(f.params.size()+6+f.localDefinitions.size());auto value_slot=[&](std::string_view name,Value value){auto& object=local_storage(p,name,3,true);object.array[0].value=value;p.locals[std::string(name)].value=Value::object(object.id);};
  try{value_slot("my",Value::object(my));value_slot("you",Value::object(you));for(std::size_t i=0;i<f.params.size();++i){auto& parameter=f.params[i];auto a=i<args.size()?args[i]:Argument{};if(parameter.reference){if(!a.ref)fail(f.loc,"reference parameter "+parameter.name+" requires lvalue");a.captured=false;p.bindings[parameter.name]=a;hold_reference(a);}else value_slot(parameter.name,a.current());}
   static constexpr std::array<std::string_view,4> argNames{"_arg1","_arg2","_arg3","_arg4"};for(std::size_t i=0;i<4;++i)value_slot(argNames[i],i<args.size()?args[i].current():Value{});
   for(auto& [name,size]:f.localDefinitions)if(!p.locals.contains(name)){auto& object=local_storage(p,name,size);p.locals[name].value=Value::object(object.id);}
  }catch(...){release(p);throw;}
 }
 std::unique_ptr<Process> frame(FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){auto p=std::make_unique<Process>();prepare(*p,id,args,my,you);return p;}
 void capture(const Process& p){
  if(suppressContinuations||p.noContinuation)return;auto snapshot=std::make_unique<Process>(p);snapshot->running=false;snapshot->yielded=false;snapshot->snapshots.clear();snapshot->savedLocals.clear();
  // A7 100145c0 copies function+0x1c into each newly allocated continuation.
  snapshot->schedulerFlags=functions[p.function].schedulerFlags;
  snapshot->retainedContext={snapshot->context.my,snapshot->context.you};for(auto context:snapshot->retainedContext)state.retain_object(context);
  std::size_t count=0;for(auto id:snapshot->ownedLocals)count+=state.require(id).array.size();snapshot->snapshots.reserve(snapshot->ownedLocals.size());snapshot->savedLocals.reserve(count);
  for(auto id:snapshot->ownedLocals){auto& object=state.require(id);snapshot->snapshots.push_back({id,snapshot->savedLocals.size(),object.array.size()});for(auto& slot:object.array)snapshot->savedLocals.push_back(slot.value);++localLeases.at(id).owners;}
  for(auto& [name,arg]:snapshot->bindings)hold_reference(arg);try{queued.push_back(std::move(snapshot));}catch(...){if(snapshot)release(*snapshot);throw;}
 }
 Value start(FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){
  if(++callDepth>256){--callDepth;throw std::runtime_error("WDL call stack exceeds 256");}
  Process p;try{prepare(p,id,args,my,you);run(p);}catch(...){release(p);--callDepth;throw;}--callDepth;auto value=p.result;release(p);return value;
 }
 void run(Process& p){
  for(auto& snapshot:p.snapshots){auto& object=state.require(snapshot.object);for(std::size_t i=0;i<snapshot.count;++i)object.array[i].value=p.savedLocals[snapshot.offset+i];}p.snapshots.clear();p.savedLocals.clear();
  auto* previous=active;active=&p;auto& f=functions[p.function];p.yielded=false;p.running=true;
  try{while(p.pc<f.code.size()&&!p.yielded&&!p.cancelled){auto& i=f.code[p.pc++];switch(i.kind){
   case Instruction::Eval:p.result=evaluate(*i.expr,&p).current();break;
   case Instruction::Jump:p.pc=i.target;break;
   // A6 interpreter 00410a80 case5 stores the condition into accumulator 006445b4 before jumping; falling off the end returns it (:845).
   case Instruction::Branch:p.result=evaluate(*i.expr,&p).current();if(!p.result)p.pc=i.target;break;
   case Instruction::Return:if(i.expr)p.result=evaluate(*i.expr,&p).current();p.pc=f.code.size();break;
   case Instruction::Late:f.schedulerFlags|=2;break;
   case Instruction::Declare:{auto& slot=p.locals[i.name];auto value=i.expr&&i.expr->kind!=Expr::List?evaluate(*i.expr,&p).current():Value{};auto* object=slot.value.kind==Value::Kind::Object?state.get(slot.value.bits):nullptr;if(!object||!localLeases.contains(object->id)||std::find(p.ownedLocals.begin(),p.ownedLocals.end(),object->id)==p.ownedLocals.end())object=&local_storage(p,i.name,i.size?i.size:3);auto& o=*object;slot.value=Value::object(o.id);for(auto& s:o.array)s.value={};if(i.expr&&i.expr->kind==Expr::List){if(i.expr->args.size()>o.array.size())fail(i.loc,"too many local array initializers");for(std::size_t n=0;n<i.expr->args.size();++n)o.array[n].value=evaluate(*i.expr->args[n],&p).current();}else if(!o.array.empty())o.array[0].value=value;break;}
  }}p.context.my=context_slot(p,"my").value.bits;p.context.you=context_slot(p,"you").value.bits;}catch(...){p.running=false;active=previous;throw;}p.running=false;active=previous;
 }
 void tick(std::int32_t raw,std::int32_t freeze){
  const bool bonus=state.profile==SemanticProfile::BonusA7;
  const auto freezeGlobal=state.globals.find("freeze_mode");
  // A6 004120ec and A7 10016710 reread the source cell for every eligible record.
  // Standalone callers without registered globals retain the supplied freeze value.
  const auto freezeValue=[&](){return freezeGlobal==state.globals.end()?freeze:freezeGlobal->second.value.raw();};
  suppressContinuations=false;time=raw;state.global("time").value=Value::from_raw(raw);if(freezeValue()>=2048)return;
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
  clear_layouts();
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
void Runtime::enqueue(FunctionId id,std::span<const Argument> args,ObjectId my,ObjectId you){impl_->queued.push_back(impl_->frame(id,args,my,you));}
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
 if(impl_->active&&impl_->active->context.my==id)impl_->clear_context(*impl_->active,"my");
}
void Runtime::late_current(){if(!impl_->active)throw std::runtime_error("proc_late outside current process");impl_->functions[impl_->active->function].schedulerFlags|=2;}
State& Runtime::state(){return impl_->state;}
FunctionId Runtime::function(std::string_view name)const{auto it=impl_->names.find(name);return it==impl_->names.end()?0:it->second;}
bool Runtime::external_import(std::string_view name)const{auto it=impl_->names.find(name);return it!=impl_->names.end()&&impl_->functions[it->second].external&&!impl_->functions[it->second].defined;}
const std::vector<std::string>& Runtime::diagnostics()const{return impl_->diagnostics;}
const ParseMetrics& Runtime::metrics()const{return impl_->metrics;}
std::size_t Runtime::processes()const{return std::count_if(impl_->queued.begin(),impl_->queued.end(),[](auto& p){return !p->cancelled;});}
bool Runtime::running(FunctionId id)const{return id&&std::any_of(impl_->queued.begin(),impl_->queued.end(),[id](auto& p){return !p->cancelled&&p->function==id;});}
const Runtime::CompilerSettings& Runtime::compiler_settings()const{return impl_->compilerSettings;}
std::size_t Runtime::reroute_reads(std::string_view global,std::string_view call,std::span<const std::string_view> functions,std::span<const std::string_view> arrays){
 std::size_t count=0;
 auto assignment=[](const Expr& e){return (e.kind==Expr::Binary&&precedence(e.op)==1)||(e.kind==Expr::Unary&&(e.op.ends_with("++")||e.op.ends_with("--")));};
 std::function<void(E&,bool,bool)> walk=[&](E& e,bool all,bool target){
  if(!e)return;
  if(e->kind==Expr::Name&&all&&!target&&case_equal(e->op,global)){auto c=std::make_unique<Expr>();c->kind=Expr::Call;c->dispatch=Expr::Native;c->loc=e->loc;auto n=std::make_unique<Expr>();n->kind=Expr::Name;n->loc=e->loc;n->op=std::string(call);c->args.push_back(std::move(n));e=std::move(c);++count;return;}
  const bool indexed=e->kind==Expr::Index&&e->args[0]->kind==Expr::Name&&std::any_of(arrays.begin(),arrays.end(),[&](auto a){return case_equal(a,e->args[0]->op);});
  for(std::size_t i=0;i<e->args.size();++i)walk(e->args[i],all||(indexed&&i==1),i==0&&assignment(*e));
 };
 for(auto& f:impl_->functions){const bool all=std::any_of(functions.begin(),functions.end(),[&](auto n){return case_equal(n,f.name);});for(auto& ins:f.code)walk(ins.expr,all,false);}
 return count;
}
}
