#include "mods.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace k22 {
namespace fs=std::filesystem;
fs::path mods_root(){
    const char* xdg=std::getenv("XDG_DATA_HOME");
    if(xdg&&*xdg&&fs::path(xdg).is_absolute())return fs::path(xdg)/"kabus22/mods";
    const char* home=std::getenv("HOME");
    if(!home||!*home)throw std::runtime_error("HOME or XDG_DATA_HOME required for mods");
    return fs::path(home)/".local/share/kabus22/mods";
}
bool mod_id(std::string_view s){return !s.empty()&&s.size()<=128&&s!="."&&s!=".."&&s.front()!='-'&&std::all_of(s.begin(),s.end(),[](char c){return static_cast<unsigned char>(c)>=0x20&&c!=0x7f&&c!='/';});}
ModInfo load_mod(const fs::path& directory){
    ModInfo mod;mod.id=directory.filename().string();mod.path=directory;std::error_code ec;
    if(!mod_id(mod.id)){mod.detail="invalid mod directory name";return mod;}
    if(!fs::is_directory(fs::symlink_status(directory,ec))){mod.detail="missing directory";return mod;}
    const auto manifest=directory/"mod.txt";
    if(!fs::is_regular_file(fs::symlink_status(manifest,ec))){mod.detail="missing mod.txt (regular file, not symlink)";return mod;}
    if(fs::file_size(manifest,ec)>65536){mod.detail="mod.txt exceeds 64 KiB";return mod;}
    std::ifstream in(manifest,std::ios::binary);std::string text{std::istreambuf_iterator<char>(in),{}};
    if(!in.eof()&&in.fail()){mod.detail="cannot read mod.txt";return mod;}
    bool seen[4]{};std::size_t number=0;
    for(std::size_t start=0;start<=text.size();){
        auto end=std::min(text.find('\n',start),text.size());std::string_view line(text.data()+start,end-start);start=end+1;++number;
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        auto bad=[&](std::string reason){mod.detail="mod.txt line "+std::to_string(number)+": "+reason;return mod;};
        if(number==1){if(line!="kabus22-mod 1")return bad("expected header 'kabus22-mod 1'");continue;}
        if(line.empty())continue;
        auto tab=line.find('\t');if(tab==line.npos)return bad("expected key<TAB>value");
        auto k=line.substr(0,tab);auto value=std::string(line.substr(tab+1));
        static constexpr std::string_view keys[]{"name","version","author","description"};
        auto i=std::size_t(std::find(std::begin(keys),std::end(keys),k)-std::begin(keys));
        if(i==4)return bad("unknown key '"+std::string(k)+"'");
        if(seen[i])return bad("duplicate "+std::string(k));seen[i]=true;
        (i==0?mod.name:i==1?mod.version:i==2?mod.author:mod.description)=std::move(value);
    }
    if(mod.name.empty()){mod.detail="mod.txt: missing or empty name";return mod;}
    if(!fs::is_directory(fs::symlink_status(directory/"data",ec))){mod.detail="missing data/ directory";return mod;}
    mod.valid=true;return mod;
}
}
