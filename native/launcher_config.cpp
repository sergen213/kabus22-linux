#include "launcher_config.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include "platform.hpp"
#include <system_error>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace k22::launcher_config {
namespace fs = std::filesystem;
namespace {
#ifndef _WIN32
struct Fd { int value; explicit Fd(int fd):value(fd){} ~Fd(){if(value>=0)::close(value);} Fd(const Fd&)=delete; Fd& operator=(const Fd&)=delete; };
#endif
std::string_view trim_left(std::string_view s){while(!s.empty()&&(s.front()==' '||s.front()=='\t'))s.remove_prefix(1);return s;}
std::string_view trim_right(std::string_view s){while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\r'||s.back()=='\n'))s.remove_suffix(1);return s;}
#ifdef _WIN32
std::string win32_error(){return std::system_category().message(int(GetLastError()));}
#else
fs::path xdg(const char* variable,const char* fallback){
    const char* v=std::getenv(variable);if(v&&*v&&fs::path(v).is_absolute())return v; // GLib ignores a relative XDG_* value
    const char* home=std::getenv("HOME");if(!home||!*home)throw std::runtime_error(std::string("HOME or ")+variable+" required for launcher.ini");
    return fs::path(home)/fallback;
}
// The launcher's directory(path,true): every missing component private (0700).
void private_directories(const fs::path& path){
    fs::path at;for(const auto& part:fs::absolute(path).lexically_normal()){at/=part;if(::mkdir(at.c_str(),0700)!=0&&errno!=EEXIST)throw std::runtime_error("cannot create "+at.string()+": "+std::strerror(errno));}
}
#endif
}
std::string merge(std::string_view text,const std::vector<Edit>& edits){
    struct Line { std::string text, group, key; bool header{}, keyLine{}; };
    std::vector<Line> lines;std::string group;
    for(std::size_t start=0;start<text.size();){
        auto end=text.find('\n',start);end=end==std::string_view::npos?text.size():end+1;
        Line l{std::string(text.substr(start,end-start))};start=end;
        auto t=trim_left(trim_right(l.text));
        if(!t.empty()&&t.front()=='['&&t.back()==']'){group=std::string(t.substr(1,t.size()-2));l.header=true;}
        else if(!t.empty()&&t.front()!='#'){auto eq=t.find('=');if(eq!=std::string_view::npos){l.key=std::string(trim_right(t.substr(0,eq)));l.keyLine=true;}}
        l.group=group;lines.push_back(std::move(l));
    }
    std::vector<char> placed(edits.size());
    auto ending=[](const std::string& s){return s.ends_with("\r\n")?std::string("\r\n"):std::string("\n");};
    for(auto it=lines.begin();it!=lines.end();){
        auto e=it->keyLine?std::find_if(edits.begin(),edits.end(),[&](const Edit& d){return d.group==it->group&&d.key==it->key;}):edits.end();
        if(e==edits.end()){++it;continue;}
        auto& done=placed[std::size_t(e-edits.begin())];
        if(done||!e->value){done=true;it=lines.erase(it);continue;} // later duplicates (GKeyFile: the last one wins) and removals
        it->text=e->key+'='+*e->value+ending(it->text);done=true;++it;
    }
    for(std::size_t i=0;i<edits.size();++i){
        if(placed[i]||!edits[i].value)continue;
        const auto& e=edits[i];std::size_t at=lines.size();bool found=false;
        for(std::size_t j=0;j<lines.size();++j){if(lines[j].group!=e.group)continue;if(lines[j].header){at=j+1;found=true;}else if(lines[j].keyLine&&found)at=j+1;}
        if(!found){
            if(!lines.empty()&&!lines.back().text.ends_with('\n'))lines.back().text+='\n';
            if(!lines.empty()&&!trim_left(trim_right(lines.back().text)).empty())lines.push_back({"\n",lines.back().group});
            lines.push_back({'['+e.group+"]\n",e.group,{},true});at=lines.size();
        }
        if(at>0&&!lines[at-1].text.ends_with('\n'))lines[at-1].text+='\n';
        lines.insert(lines.begin()+std::ptrdiff_t(at),Line{e.key+'='+*e.value+'\n',e.group,e.key,false,true});
        for(std::size_t k=i+1;k<edits.size();++k)if(edits[k].group==e.group&&edits[k].key==e.key)placed[k]=true;
    }
    std::string out;for(const auto& l:lines)out+=l.text;return out;
}
std::vector<Edit> edits(const Graphics& g){
    std::vector<Edit> out{{"Launcher","renderer",g.renderer},{"Launcher","preset",g.preset}};
    if(g.window)out.push_back({"Launcher","window",*g.window});
    if(g.resolution)out.push_back({"Launcher","resolution",*g.resolution});
    for(const auto& [key,value,fallback]:g.options){
        if(key=="backend"||key=="texture_packs")continue;
        if(g.preset!="custom"&&key!="fps_limit"&&key!="seamless_doors")continue;
        // Xbox: uwp_main.cpp defaults an absent fps_limit to 120 (the console's refresh), so "original" must be written out.
#if K22_UWP
        const bool explicitValue=key=="fps_limit";
#else
        const bool explicitValue=false;
#endif
        out.push_back({"Graphics",key,value==fallback&&!explicitValue?std::nullopt:std::optional<std::string>(value)});
    }
    for(auto key:retiredGraphicsKeys)out.push_back({"Graphics",std::string(key),std::nullopt});
    return out;
}
#ifdef _WIN32
// %LOCALAPPDATA%\Kabus22\config\launcher.ini (UWP: LocalState\config); the lock beside the saves in the user data root.
fs::path path(){return platform::user_data_root()/"config"/"launcher.ini";}
fs::path lock_root(){return platform::user_data_root();}
void write(const Graphics& g,const fs::path& file,const fs::path& lockRoot){
    std::error_code created;fs::create_directories(lockRoot,created);if(created)throw std::runtime_error("cannot create "+lockRoot.string()+": "+created.message());
    // The POSIX flock(.launcher.lock): LockFileEx over the whole file, released when the handle closes.
    const auto lockPath=lockRoot/".launcher.lock";platform::FileStatus st{};
    auto lock=platform::open_file(lockPath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,OPEN_ALWAYS);
    if(!lock||!platform::file_status(lock,st)||st.directory||st.links!=1)throw std::runtime_error("unsafe launcher settings lock "+lockPath.string());
    for(int attempt=0;;++attempt){OVERLAPPED whole{};if(LockFileEx(lock.get(),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,MAXDWORD,MAXDWORD,&whole))break;if(attempt==50)throw std::runtime_error("the launcher is applying settings; launcher.ini not updated");std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    std::string bytes;bool existed=false;
    if(auto in=platform::open_file(file,GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING)){
        // The launcher's snapshot() bounds: a regular file of at most 16 KiB.
        if(!platform::file_status(in,st)||st.directory||st.size>16384)throw std::runtime_error("launcher.ini is not a bounded user-owned regular file: "+file.string());
        bytes.resize(std::size_t(st.size));std::size_t offset=0;
        while(offset<bytes.size()){auto n=platform::read_at(in,offset,bytes.data()+offset,bytes.size()-offset);if(n<=0)throw std::runtime_error("launcher.ini changed during reading");offset+=std::size_t(n);}
        existed=true;
    }else if(GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("cannot read "+file.string()+": "+win32_error());
    const auto merged=merge(bytes,edits(g));
    if(existed&&merged==bytes)return;
    if(merged.size()>16384)throw std::runtime_error("launcher.ini would exceed the launcher's 16 KiB bound");
    fs::create_directories(file.parent_path(),created);if(created)throw std::runtime_error("cannot create "+file.parent_path().string()+": "+created.message());
    // mkostemp: a new uniquely named file (CREATE_NEW) beside the target.
    fs::path temporary;platform::Handle out;
    for(unsigned attempt=0;!out&&attempt<100;++attempt){temporary=file.parent_path()/(".launcher-"+std::to_string(platform::process_id())+"-"+std::to_string(GetTickCount64()+attempt));out=platform::open_file(temporary,GENERIC_WRITE,0,CREATE_NEW);if(!out&&GetLastError()!=ERROR_FILE_EXISTS)break;}
    if(!out)throw std::runtime_error("cannot create a temporary launcher.ini: "+win32_error());
    try{
        if(!platform::write_all(out,merged.data(),merged.size()))throw std::runtime_error("cannot write launcher.ini");
        if(!platform::flush(out))throw std::runtime_error("cannot synchronize launcher.ini");out.reset();
        // MOVEFILE_WRITE_THROUGH: durable on return, the POSIX directory fsync.
        if(!platform::replace(temporary,file))throw std::runtime_error("cannot publish launcher.ini: "+win32_error());
    }catch(...){out.reset();std::error_code ignored;fs::remove(temporary,ignored);throw;}
}
#else
fs::path path(){return xdg("XDG_CONFIG_HOME",".config")/"kabus22/launcher.ini";}
fs::path lock_root(){return xdg("XDG_DATA_HOME",".local/share")/"kabus22";}
void write(const Graphics& g,const fs::path& file,const fs::path& lockRoot){
    private_directories(lockRoot);
    // installer/launcher_settings.cpp privateLock: the same file and flock, so a launcher Apply never interleaves with this.
    Fd lock(::open((lockRoot/".launcher.lock").c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600));struct stat st{};
    if(lock.value<0||::fstat(lock.value,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=::geteuid()||(st.st_mode&0077)||st.st_nlink!=1)throw std::runtime_error("unsafe launcher settings lock "+(lockRoot/".launcher.lock").string());
    for(int attempt=0;::flock(lock.value,LOCK_EX|LOCK_NB)!=0;++attempt){if(attempt==50)throw std::runtime_error("the launcher is applying settings; launcher.ini not updated");std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    std::string bytes;bool existed=false;
    if(Fd in(::open(file.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));in.value>=0){
        // The launcher's snapshot() bounds: a user-owned private regular file of at most 16 KiB.
        if(::fstat(in.value,&st)!=0||!S_ISREG(st.st_mode)||st.st_size>16384||st.st_uid!=::geteuid())throw std::runtime_error("launcher.ini is not a bounded user-owned regular file: "+file.string());
        bytes.resize(std::size_t(st.st_size));std::size_t offset=0;
        while(offset<bytes.size()){auto n=::read(in.value,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("launcher.ini changed during reading");offset+=std::size_t(n);}
        existed=true;
    }else if(errno!=ENOENT)throw std::runtime_error("cannot read "+file.string()+": "+std::strerror(errno));
    const auto merged=merge(bytes,edits(g));
    if(existed&&merged==bytes)return;
    if(merged.size()>16384)throw std::runtime_error("launcher.ini would exceed the launcher's 16 KiB bound");
    private_directories(file.parent_path());
    auto temporary=(file.parent_path()/".launcher-XXXXXX").string();Fd out(::mkostemp(temporary.data(),O_CLOEXEC));
    if(out.value<0)throw std::runtime_error("cannot create a temporary launcher.ini: "+std::string(std::strerror(errno)));
    try{
        if(::fchmod(out.value,0600)!=0)throw std::runtime_error("cannot protect launcher.ini");
        for(std::size_t offset=0;offset<merged.size();){auto n=::write(out.value,merged.data()+offset,merged.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("cannot write launcher.ini");offset+=std::size_t(n);}
        if(::fsync(out.value)!=0)throw std::runtime_error("cannot synchronize launcher.ini");
        if(::rename(temporary.c_str(),file.c_str())!=0)throw std::runtime_error("cannot publish launcher.ini: "+std::string(std::strerror(errno)));
    }catch(...){::unlink(temporary.c_str());throw;}
    Fd folder(::open(file.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));if(folder.value<0||::fsync(folder.value)!=0)throw std::runtime_error("cannot synchronize "+file.parent_path().string());
}
#endif
}
