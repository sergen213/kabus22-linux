#include "saves.hpp"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <unistd.h>
namespace k22::saves {
namespace {
std::string lower(std::string_view s){std::string out(s);for(char& c:out)if(c>='A'&&c<='Z')c=char(c+32);return out;}
std::vector<std::uint8_t> read_bounded(const fs::path& path,std::uintmax_t limit){
 std::error_code error;auto size=fs::file_size(path,error);if(error||size>limit)throw std::runtime_error("unreadable or oversized save file: "+path.string());
 std::vector<std::uint8_t> bytes(size);std::ifstream in(path,std::ios::binary);if(!in||(size&&!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size))))throw std::runtime_error("cannot read save file: "+path.string());return bytes;
}
}
fs::path root(){
 auto* base=std::getenv("XDG_DATA_HOME");if(base&&*base)return fs::path(base)/"kabus22";
 auto* home=std::getenv("HOME");if(!home||!*home)throw std::runtime_error("HOME or XDG_DATA_HOME required for private saves");return fs::path(home)/".local/share/kabus22";
}
void write_atomic(const fs::path& target,std::span<const std::uint8_t> bytes){
 fs::create_directories(target.parent_path());
 auto temporary=target;temporary+=".tmp."+std::to_string(::getpid());int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)throw std::runtime_error("cannot write private file: "+temporary.string());
 std::size_t offset=0;while(offset<bytes.size()){auto written=::write(fd,bytes.data()+offset,bytes.size()-offset);if(written>0){offset+=std::size_t(written);continue;}if(written<0&&errno==EINTR)continue;::close(fd);::unlink(temporary.c_str());throw std::runtime_error("private file write failed: "+temporary.string());}
 if(::fsync(fd)!=0){::close(fd);::unlink(temporary.c_str());throw std::runtime_error("private file sync failed: "+temporary.string());}if(::close(fd)!=0){::unlink(temporary.c_str());throw std::runtime_error("private file close failed: "+temporary.string());}
 fs::rename(temporary,target);
 int dir=::open(target.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir<0||::fsync(dir)!=0){if(dir>=0)::close(dir);throw std::runtime_error("private directory sync failed: "+target.parent_path().string());}::close(dir);
}
int slot_number(std::string_view name){
 auto n=lower(name);if(!n.starts_with("kayit")||!n.ends_with(".sav")||n.size()<10||n.size()>11)return -1;auto digits=std::string_view(n).substr(5,n.size()-9);
 if(digits.size()>1&&digits.front()=='0')return -1;int slot=-1;auto [p,e]=std::from_chars(digits.data(),digits.data()+digits.size(),slot);return e==std::errc{}&&p==digits.data()+digits.size()&&slot>=0&&slot<99?slot:-1;
}
bool rotated(std::string_view name){return slot_number(name)>=0||lower(name)=="kysd.sav";}
void backup(const fs::path& root,const fs::path& relative,int keep){
 auto source=root/relative;if(!fs::is_regular_file(fs::symlink_status(source)))return;
 auto base=root/"backups"/relative;auto numbered=[&](int n){auto p=base;p+="."+std::to_string(n);return p;};
 std::error_code ignored;fs::remove(numbered(keep),ignored);
 for(int n=keep-1;n>=1;--n)if(fs::exists(fs::symlink_status(numbered(n))))fs::rename(numbered(n),numbered(n+1));
 write_atomic(numbered(1),read_bounded(source,64*1024*1024));
}
std::string now_utc(){auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};::gmtime_r(&t,&tm);char text[32];std::strftime(text,sizeof text,"%Y-%m-%dT%H:%M:%SZ",&tm);return text;}
std::string fnv1a64(std::span<const std::uint8_t> bytes){std::uint64_t h=0xcbf29ce484222325ull;for(auto b:bytes){h^=b;h*=0x100000001b3ull;}char text[17];std::snprintf(text,sizeof text,"%016llx",static_cast<unsigned long long>(h));return text;}
std::string format_meta(const Meta& m){
 auto clean=[](std::string v){for(char& c:v)if(c=='\n'||c=='\r')c=' ';return v;};
 return "kabus22-save-meta "+std::to_string(m.format)+"\nengine="+clean(m.engine)+"\nwritten="+clean(m.written)+"\nentry="+clean(m.entry)+"\nscript="+clean(m.script)+"\norigin="+clean(m.origin)+"\n"+(m.position.empty()?"":"position="+clean(m.position)+"\n");
}
std::optional<Meta> parse_meta(std::string_view text){
 constexpr std::string_view magic="kabus22-save-meta ";Meta m;if(!text.starts_with(magic))return std::nullopt;
 auto line=text.substr(magic.size(),text.find('\n')-magic.size());auto [p,e]=std::from_chars(line.data(),line.data()+line.size(),m.format);if(e!=std::errc{}||p!=line.data()+line.size()||m.format<1)return std::nullopt;
 text=text.find('\n')==std::string_view::npos?std::string_view{}:text.substr(text.find('\n')+1);
 while(!text.empty()){auto end=text.find('\n');auto row=text.substr(0,end);text=end==std::string_view::npos?std::string_view{}:text.substr(end+1);if(row.empty())continue;auto eq=row.find('=');if(eq==std::string_view::npos)return std::nullopt;auto key=row.substr(0,eq);std::string value(row.substr(eq+1));
  if(key=="engine")m.engine=value;else if(key=="written")m.written=value;else if(key=="entry")m.entry=value;else if(key=="script")m.script=value;else if(key=="origin")m.origin=value;else if(key=="position")m.position=value;} // Unknown keys: forward-compatible.
 return m;
}
void write_meta(const fs::path& save,const Meta& m){auto text=format_meta(m);auto path=save;path+=".meta";write_atomic(path,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));}
std::optional<Meta> read_meta(const fs::path& save){auto path=save;path+=".meta";if(!fs::is_regular_file(fs::symlink_status(path)))return std::nullopt;auto bytes=read_bounded(path,4096);return parse_meta(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}
bool migrate_meta(const fs::path& save,const Meta& current,std::ostream& warnings){
 auto path=save;path+=".meta";Meta m;m.format=0;
 if(fs::exists(fs::symlink_status(path))){auto bytes=read_bounded(path,4096);auto parsed=parse_meta(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));if(!parsed){warnings<<"kabus22: malformed save metadata left untouched: "<<path.string()<<'\n';return false;}m=*parsed;}
 if(m.format>MetaFormat){warnings<<"kabus22: save metadata format "<<m.format<<" is newer than "<<MetaFormat<<"; left untouched: "<<path.string()<<'\n';return false;}
 switch(m.format){
 case 0:m={MetaFormat,current.engine,now_utc(),"","","legacy",""};write_meta(save,m);[[fallthrough]]; // Original/unknown writer: identity unknown, never guessed.
 case MetaFormat:break;
 }
 return true;
}
std::string validate(std::string_view name,std::span<const std::uint8_t> b){
 auto n=lower(name);
 if(slot_number(n)>=0){if(b.size()!=2030)return "slot payload must be exactly 2030 bytes";if(b[0]!=100)return "save_versiyon byte must be 100 (K22.wdl save_versiyon)";return {};}
 if(n=="ftz.k22"){if(b.size()!=3||b[1]>23||b[2]>59)return "play-time record must be day,hour<24,minute<60";return {};}
 if(n!="kysd.sav")return "not an original save-state file";
 std::string_view text(reinterpret_cast<const char*>(b.data()),b.size());int active=-1,count=-1;
 auto number=[&](int& out){auto end=text.find(' ');if(end==std::string_view::npos)return false;auto [p,e]=std::from_chars(text.data(),text.data()+end,out);text.remove_prefix(end+1);return e==std::errc{}&&p==text.data()-1;};
 if(!number(active)||!number(count)||active<0||active>=99||count<0||count>99)return "index header must be 'active count ' with active<99, count<=99";
 int lines=0;for(char c:text){if(c=='\0')return "index labels contain NUL";lines+=c=='\n';}
 if(lines!=count||(!text.empty()&&text.back()!='\n'))return "index label count does not match its header";
 return {};
}
ImportResult import_windows(const fs::path& game,const fs::path& root,const fs::path& slotDirectory,const Meta& meta){
 auto scan=[](const fs::path& dir){std::map<std::string,fs::path> found;std::error_code error;if(!fs::is_directory(dir,error))return found;
  for(const auto& entry:fs::directory_iterator(dir,error)){if(!entry.is_regular_file()||entry.is_symlink())continue;auto name=lower(entry.path().filename().string());if(rotated(name))found.emplace(name,entry.path());}return found;};
 // Original scripts keep slots beside K22.exe; the K22Widescreen patch moves them to _data/saves.
 auto direct=scan(game),patched=scan(game/"_data"/"saves");
 auto newest=[](const std::map<std::string,fs::path>& set){return set.contains("kysd.sav")?fs::last_write_time(set.at("kysd.sav")):fs::file_time_type::min();};
 if(!direct.contains("kysd.sav")&&!patched.contains("kysd.sav"))throw std::runtime_error("no kysd.sav save index in "+game.string()+" or its _data/saves");
 const bool fromPatched=patched.contains("kysd.sav")&&(!direct.contains("kysd.sav")||newest(patched)>newest(direct));const auto& chosen=fromPatched?patched:direct;
 ImportResult result;result.source=chosen.at("kysd.sav").parent_path();auto slotMeta=meta;slotMeta.entry=fromPatched?"K22Widescreen.wdl":"K22.wdl"; // Writer family is proven by the layout; its script bytes are not.
 std::map<std::string,std::vector<std::uint8_t>> payload;
 for(const auto& [name,path]:chosen){auto bytes=read_bounded(path,65536);if(auto why=validate(name,bytes);!why.empty())throw std::runtime_error("refusing import, "+path.string()+": "+why);payload.emplace(name,std::move(bytes));}
 {const auto& bytes=payload.at("kysd.sav");std::string index(bytes.begin(),bytes.end());int active=0,count=0;std::sscanf(index.c_str(),"%d %d",&active,&count); // validate() proved the header.
  for(int slot=0;slot<count;++slot)if(!payload.contains("kayit"+std::to_string(slot)+".sav"))throw std::runtime_error("refusing import: kysd.sav lists slot "+std::to_string(slot+1)+" but kayit"+std::to_string(slot)+".sav is missing");}
 auto ftz=fs::path{};for(const auto& entry:fs::directory_iterator(game))if(entry.is_regular_file()&&!entry.is_symlink()&&lower(entry.path().filename().string())=="ftz.k22")ftz=entry.path();
 if(!ftz.empty()){auto bytes=read_bounded(ftz,64);if(auto why=validate("ftz.k22",bytes);!why.empty())throw std::runtime_error("refusing import, "+ftz.string()+": "+why);payload.emplace("ftz.k22",std::move(bytes));}
 // Slots first, then the index that references them, then play time.
 auto order=[](const std::string& n){return n=="kysd.sav"?1:n=="ftz.k22"?2:0;};std::vector<std::string> names;for(const auto& [n,_]:payload)names.push_back(n);
 std::stable_sort(names.begin(),names.end(),[&](const auto& a,const auto& b){return order(a)<order(b);});
 for(const auto& name:names){
  auto relative=name=="ftz.k22"?fs::path(name):slotDirectory/name;auto target=root/relative;
  if(fs::exists(fs::symlink_status(target))){if(!fs::is_regular_file(fs::symlink_status(target)))throw std::runtime_error("refusing to replace non-regular "+target.string());backup(root,relative);result.backedUp.push_back(relative.generic_string());}
  write_atomic(target,payload.at(name));if(slot_number(name)>=0)write_meta(target,slotMeta);result.imported.push_back(relative.generic_string());
 }
 return result;
}
}
