#include "launcher_settings.hpp"
#include "layout.hpp"
#include <glib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace k22::launcher {
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
struct Fd {
    int value;
    explicit Fd(int fd): value(fd) { require(fd >= 0, std::string("Cannot open settings path: ") + std::strerror(errno)); }
    ~Fd() { ::close(value); }
    Fd(const Fd&) = delete;
};
fs::path checkedAbsolute(const fs::path& path) { require(!path.empty(), "Empty settings path"); return fs::absolute(path).lexically_normal(); }
// Open every component without following links, including paths outside the installation.
int directory(const fs::path& path, bool create = false) {
    auto full = checkedAbsolute(path);
    int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    require(current >= 0, "Cannot open filesystem root");
    try {
        for (const auto& component : full.relative_path()) {
            if(component.empty())continue; // A trailing separator denotes this directory, not an empty child.
            if (create && ::mkdirat(current, component.c_str(), 0700) != 0 && errno != EEXIST)
                throw std::runtime_error("Cannot create private folder: " + full.string());
            const int next = ::openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            require(next >= 0, "Missing or unsafe directory: " + full.string());
            ::close(current); current = next;
            struct stat st{};
            require(::fstat(current, &st) == 0, "Cannot inspect directory");
            require(!(st.st_mode & 0022) || ((st.st_mode & S_ISVTX) && (st.st_uid == 0 || st.st_uid == ::geteuid())), "Unsafe writable directory: " + full.string());
        }
        return current;
    } catch (...) { ::close(current); throw; }
}
Snapshot snapshot(const fs::path& path, bool privateFile = false) {
    Snapshot result; result.path = checkedAbsolute(path);
    // A not-yet-created private directory is permitted, but never a dangling link.
    fs::path ancestor = result.path.parent_path();
    while (!fs::exists(ancestor)) {
        require(!fs::is_symlink(fs::symlink_status(ancestor)), "Unsafe settings directory link");
        ancestor = ancestor.parent_path();
    }
    { Fd parent(directory(ancestor)); }
    if (ancestor != result.path.parent_path()) return result;
    Fd parent(directory(result.path.parent_path()));
    int raw = ::openat(parent.value, result.path.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (raw < 0 && errno == ENOENT) return result;
    Fd file(raw); struct stat st{};
    require(::fstat(file.value, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 && st.st_size <= 16384, "Settings file is not a bounded regular file: " + result.path.string());
    if (privateFile) require(st.st_uid == ::geteuid() && !(st.st_mode & 0022) && st.st_nlink == 1, "Settings file is not private and user-owned: " + result.path.string());
    std::string bytes(static_cast<std::size_t>(st.st_size), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto count = ::read(file.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        require(count > 0, "Settings file changed during reading"); offset += static_cast<std::size_t>(count);
    }
    char extra; require(::read(file.value, &extra, 1) == 0, "Settings file grew during reading");
    struct stat after{};
    require(::fstat(file.value, &after) == 0 && st.st_size == after.st_size && st.st_mtim.tv_sec == after.st_mtim.tv_sec && st.st_mtim.tv_nsec == after.st_mtim.tv_nsec, "Settings file changed during reading");
    result.bytes = std::move(bytes); result.device = st.st_dev; result.inode = st.st_ino;
    result.seconds = st.st_mtim.tv_sec; result.nanos = st.st_mtim.tv_nsec;
    return result;
}
void unchanged(const Snapshot& expected, bool privateFile) {
    auto now = snapshot(expected.path, privateFile);
    require(now.bytes == expected.bytes && now.device == expected.device && now.inode == expected.inode && now.seconds == expected.seconds && now.nanos == expected.nanos,
        "Settings changed outside the launcher; reload before applying: " + expected.path.string());
}
void atomicWrite(Snapshot& expected, const std::string& bytes) {
    if (expected.bytes && *expected.bytes == bytes) return;
    Fd parent(directory(expected.path.parent_path(), true)); struct stat st{};
    require(::fstat(parent.value, &st) == 0 && st.st_uid == ::geteuid() && !(st.st_mode & 0022), "Settings destination is not private and user-owned");
    unchanged(expected, true);
    auto temporary = (expected.path.parent_path()/".launcher-XXXXXX").string();
    Fd file(::mkostemp(temporary.data(), O_CLOEXEC));
    try {
        require(::fchmod(file.value, 0600) == 0, "Cannot protect settings file");
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            auto count = ::write(file.value, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            require(count > 0, "Cannot write settings file"); offset += static_cast<std::size_t>(count);
        }
        require(::fsync(file.value) == 0, "Cannot synchronize settings file");
        unchanged(expected, true);
        require(::renameat(parent.value, fs::path(temporary).filename().c_str(), parent.value, expected.path.filename().c_str()) == 0, "Cannot atomically publish settings");
        require(::fsync(parent.value) == 0, "Cannot synchronize settings folder");
        expected = snapshot(expected.path, true);
    } catch (...) { ::unlink(temporary.c_str()); throw; }
}
std::string effective(const Snapshot& user, const Snapshot& supplied) {
    require(user.bytes || supplied.bytes, "Supplied settings are missing: " + supplied.path.string());
    return user.bytes ? *user.bytes : *supplied.bytes;
}
std::vector<unsigned char> campaignBytes(const std::string& bytes) { return {bytes.begin(), bytes.end()}; }
int integer(std::string_view value) {
    int result = 0; auto [end, ec] = std::from_chars(value.data(), value.data()+value.size(), result);
    require(!value.empty() && ec == std::errc{} && end == value.data()+value.size(), "Invalid integer in Bonus settings"); return result;
}
std::string encodeBonus(const std::array<int,5>& values) {
    std::string result; for (int value : values) result += std::to_string(value) + "\n"; return result;
}
using KeyFile = std::unique_ptr<GKeyFile, decltype(&g_key_file_free)>;
using json = nlohmann::json;
void raise(GError* error, const char* fallback) { std::string message = error ? error->message : fallback; if (error) g_error_free(error); throw std::runtime_error(message); }
KeyFile keyFile(const std::string& bytes) {
    KeyFile key(g_key_file_new(), g_key_file_free); GError* error = nullptr;
    if (!g_key_file_load_from_data(key.get(), bytes.data(), bytes.size(), G_KEY_FILE_NONE, &error)) raise(error, "Cannot parse settings file");
    return key;
}
std::string keyText(GKeyFile* key, const char* group, const char* name) {
    GError* error = nullptr; gchar* value = g_key_file_get_string(key, group, name, &error);
    if (error) raise(error, "Invalid settings value");
    std::string result = value; g_free(value); return result;
}
std::string keyData(GKeyFile* key) { gsize size=0; gchar* data=g_key_file_to_data(key, &size, nullptr); std::string result(data,size); g_free(data); return result; }
// Option keys/values become argv entries; keep them to a conservative token alphabet.
bool token(std::string_view s) { return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](char c){ return g_ascii_isalnum(c) || c=='_' || c=='-' || c=='.'; }); }
bool packName(std::string_view s) { return !s.empty() && s.size() <= 128 && s != "." && s != ".." && s.front() != '-' && std::all_of(s.begin(), s.end(), [](char c){ return static_cast<unsigned char>(c) >= 0x20 && c != 0x7f && c != '/'; }); }
Preferences parsePreferences(const std::string& bytes, Preferences p = Preferences{}) {
    auto key = keyFile(bytes); GError* error = nullptr;
    auto text = [&](const char* name, std::string& destination) { if (g_key_file_has_key(key.get(), "Launcher", name, nullptr)) destination = keyText(key.get(), "Launcher", name); };
    text("mode", p.mode); text("renderer", p.renderer); text("display", p.display); text("window", p.window);
    text("resolution", p.resolution); text("language", p.language); text("audio-language", p.audioLanguage);
    text("bonus-language", p.bonusLanguage); text("preset", p.preset); text("camera", p.camera);
    if (g_key_file_has_key(key.get(), "Launcher", "bonus-aa", nullptr)) {
        p.bonusAA = g_key_file_get_integer(key.get(), "Launcher", "bonus-aa", &error);
        if (error) raise(error, "Invalid Bonus AA");
    }
    if (g_key_file_has_key(key.get(), "Launcher", "texture-packs", nullptr)) {
        gsize count = 0; gchar** list = g_key_file_get_string_list(key.get(), "Launcher", "texture-packs", &count, &error);
        if (error) raise(error, "Invalid texture pack list");
        p.packs.assign(list, list + count); g_strfreev(list);
    }
    p.graphics.clear();
    if (g_key_file_has_group(key.get(), "Graphics")) {
        gchar** names = g_key_file_get_keys(key.get(), "Graphics", nullptr, nullptr);
        for (auto** name = names; name && *name; ++name) p.graphics.emplace_back(*name, keyText(key.get(), "Graphics", *name));
        g_strfreev(names);
    }
    validatePreferences(p); return p;
}
std::string encodePreferences(const Preferences& p) {
    KeyFile key(g_key_file_new(), g_key_file_free);
    const std::pair<const char*,const std::string*> fields[]={{"mode",&p.mode},{"renderer",&p.renderer},{"display",&p.display},{"window",&p.window},{"resolution",&p.resolution},{"language",&p.language},{"audio-language",&p.audioLanguage},{"bonus-language",&p.bonusLanguage},{"preset",&p.preset},{"camera",&p.camera}};
    for (const auto& [name,value] : fields)
        g_key_file_set_string(key.get(), "Launcher", name, value->c_str());
    g_key_file_set_integer(key.get(), "Launcher", "bonus-aa", p.bonusAA);
    std::vector<const char*> packs; for (const auto& pack : p.packs) packs.push_back(pack.c_str());
    if (!packs.empty()) g_key_file_set_string_list(key.get(), "Launcher", "texture-packs", packs.data(), packs.size());
    for (const auto& [name,value] : p.graphics) g_key_file_set_string(key.get(), "Graphics", name.c_str(), value.c_str());
    return keyData(key.get());
}
template<class F> auto parsedJson(const std::string& text, const char* what, F&& read) {
    try { auto value = json::parse(text); require(value.is_object(), std::string(what) + " output is not a JSON object"); return read(value); }
    catch (const json::exception& error) { throw std::runtime_error(std::string("Malformed ") + what + " output: " + error.what()); }
}
std::string optionalText(const json& j, const char* name) { return j.contains(name) && !j[name].is_null() ? j[name].get<std::string>() : std::string(); }
std::vector<std::string> texts(const json& j) { return j.get<std::vector<std::string>>(); }
}
void binary(const fs::path& path) {
    Fd parent(directory(path.parent_path())); Fd file(::openat(parent.value,path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    struct stat st{}; Elf64_Ehdr header{};
    require(::fstat(file.value,&st)==0 && S_ISREG(st.st_mode) && st.st_uid==::geteuid() && !(st.st_mode&0022) && (st.st_mode&0111), "Program must be a user-owned executable regular file: " + path.string());
    require(::read(file.value,&header,sizeof header)==sizeof header && std::memcmp(header.e_ident,ELFMAG,SELFMAG)==0 && header.e_ident[EI_CLASS]==ELFCLASS64 && header.e_ident[EI_DATA]==ELFDATA2LSB && header.e_machine==EM_X86_64 && (header.e_type==ET_DYN||header.e_type==ET_EXEC), "Not a native Linux x86-64 ELF: " + path.string());
}
namespace {
int privateLock(const fs::path& root) {
    Fd parent(directory(root,true));struct stat st{};
    require(::fstat(parent.value,&st)==0&&st.st_uid==::geteuid()&&!(st.st_mode&0022),"Settings lock directory is not private and user-owned");
    int fd=::openat(parent.value,".launcher.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600);
    require(fd>=0,"Cannot open private launcher settings lock");
    if(::fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=::geteuid()||(st.st_mode&0077)||st.st_nlink!=1){::close(fd);throw std::runtime_error("Unsafe launcher settings lock");}
    if(::flock(fd,LOCK_EX|LOCK_NB)!=0){::close(fd);throw std::runtime_error("Another launcher is applying settings; retry after it finishes");}
    return fd;
}
bool originalEntry(const fs::path& game) {
    return !fs::exists(game/"K22Widescreen.wdl")&&!fs::exists(game/"K22Widescreen.wrs")&&
        (fs::is_regular_file(game/"K22.wdl")||fs::is_regular_file(game/"K22.wrs"));
}
// Patch DD_Dil / options.k22 byte 32 order (DilMenu.wdl, dil_degistir.wdl).
constexpr const char* languageNames[]={"tr","en","de","es","ru","pl"};
int languageIndex(const std::string& language){for(int i=0;i<6;++i)if(language==languageNames[i])return i;return -1;}
std::string savedCampaignLanguage(const std::vector<unsigned char>& bytes,const std::string& fallback,const std::vector<std::string>& languages) {
    if(bytes.size()!=32)return fallback;
    return bytes[31]<6&&std::count(languages.begin(),languages.end(),languageNames[bytes[31]])?languageNames[bytes[31]]:"saved";
}
}
std::vector<std::string> campaignLanguages(const fs::path& game) {
    std::vector<std::string> result{"tr","en","de"};
    if(fs::is_regular_file(game/"K22Widescreen.wrs"))for(auto [code,folder]:{std::pair{"es","spanish"},{"ru","russian"},{"pl","polish"}})
        if(fs::is_regular_file(game/"_data/language"/folder/"subtitle/general.txt"))result.push_back(code);
    return result;
}
std::string bonusMarkerLanguage(std::string_view marker) {
    auto line=marker.substr(0,marker.find_first_of("\r\n"));return line=="111"?"tr":line=="222"?"de":"en";
}
std::string bonusMarker(const std::string& language) { return language=="tr"?"111":language=="de"?"222":"000"; }
std::span<const Key> keys() {
    static constexpr Key result[] = {{2,"1"},{3,"2"},{4,"3"},{5,"4"},{6,"5"},{7,"6"},{8,"7"},{9,"8"},{10,"9"},{11,"0"},
        {16,"Q"},{17,"W"},{18,"E"},{19,"R"},{20,"T"},{21,"Y"},{22,"U"},{23,"I"},{24,"O"},{25,"P"},
        {29,"Left Ctrl"},{30,"A"},{31,"S"},{32,"D"},{33,"F"},{34,"G"},{35,"H"},{36,"J"},{37,"K"},{38,"L"},
        {42,"Left Shift"},{44,"Z"},{45,"X"},{46,"C"},{47,"V"},{48,"B"},{49,"N"},{50,"M"},{54,"Right Shift"},{56,"Left Alt"},
        {59,"F1"},{60,"F2"},{61,"F3"},{62,"F4"},{63,"F5"},{64,"F6"},{65,"F7"},{66,"F8"},{67,"F9"},{68,"F10"},{71,"Home"},{72,"Up"},{73,"Page Up"},{75,"Left"},{77,"Right"},{79,"End"},{80,"Down"},{81,"Page Down"},{82,"Insert"},{83,"Delete"},{87,"F11"},{88,"F12"}};
    return result;
}
void validateCampaign(const std::vector<unsigned char>& b) {
    require(b.size()==31 || b.size()==32, "options.k22 must contain exactly 31 or 32 bytes; malformed file preserved");
    const int maxima[]={3,1,1,1,1,1,1,1,100,200,160,200,100,100,100};
    for (int i=0;i<15;++i) require(b[i]<=maxima[i], "Invalid campaign option byte " + std::to_string(i));
    for (int i=15;i<25;++i) {
        require(std::any_of(keys().begin(),keys().end(),[&](const Key& k){return k.code==b[i];}), "Unsupported or reserved campaign keyboard binding");
        require(std::find(b.begin()+15,b.begin()+i,b[i])==b.begin()+i, "Duplicate campaign keyboard binding");
    }
    for (int i=25;i<31;++i) require(b[i]>=1&&b[i]<=12,"Joystick button must be 1 through 12");
    if(b.size()==32) require(b[31]<=5,"Invalid patched campaign language byte");
}
std::array<int,5> decodeBonus(const std::string& bytes) {
    require(bytes.size()<=256,"Bonus settings exceed bound"); std::array<int,5> result{}; std::size_t start=0;
    for(int i=0;i<5;++i) {
        auto end=bytes.find('\n',start); if(end==std::string::npos) end=bytes.size();
        auto line=std::string_view(bytes).substr(start,end-start); if(!line.empty()&&line.back()=='\r') line.remove_suffix(1);
        result[i]=integer(line); start=end<bytes.size()?end+1:end;
        if(i<4) require(end<bytes.size(),"Bonus settings require five lines");
    }
    require(start==bytes.size(),"Bonus settings contain trailing data");
    require(result[0]>=0&&result[0]<=3&&result[1]>=0&&result[1]<=2&&result[2]>=0&&result[2]<=1&&result[3]>=0&&result[3]<=1,"Invalid Bonus resolution/shader/anisotropy/music setting");
    // Brightness is consumed as an offset from 50, not an independently recovered Windows UI range.
    require(result[4]>=-2097102&&result[4]<=2097151,"Bonus brightness exceeds native fixed-point range");
    return result;
}
void validatePreferences(const Preferences& p) {
    require(p.mode=="campaign"||p.mode=="bonus","Invalid launcher mode");
    require(p.renderer=="opengl"||p.renderer=="vulkan-raster"||p.renderer=="vulkan-rt","Invalid renderer choice");
    require(p.display=="native"||p.display=="authored","Invalid display choice");
    require(p.window=="windowed"||p.window=="fullscreen"||p.window=="borderless","Invalid window mode");
    require(languageIndex(p.language)>=0||p.language=="saved","Invalid text language");
    require(p.camera=="original"||p.camera=="first-person","Invalid camera choice");
    require(p.audioLanguage=="tr"||p.audioLanguage=="en","Invalid voice language");
    require(p.bonusLanguage=="tr"||p.bonusLanguage=="en"||p.bonusLanguage=="de","Invalid Bonus text language");
    require(p.bonusAA>=0&&p.bonusAA<=3,"Invalid Bonus AA choice");
    if(!p.resolution.empty()) {
        auto split=p.resolution.find('x'); require(split!=std::string::npos&&p.resolution.find('x',split+1)==std::string::npos,"Resolution must be WIDTHxHEIGHT or empty for desktop");
        int w=integer(std::string_view(p.resolution).substr(0,split)), h=integer(std::string_view(p.resolution).substr(split+1));
        require(w>=1280&&h>=720&&w<=16384&&h<=16384,"Native resolution must be 1280x720 through 16384x16384");
    }
    require(p.preset=="original"||p.preset=="enhanced"||p.preset=="ultra"||p.preset=="custom","Invalid graphics preset");
    for(auto it=p.graphics.begin();it!=p.graphics.end();++it)
        require(token(it->first)&&token(it->second)&&std::none_of(p.graphics.begin(),it,[&](const auto& o){return o.first==it->first;}),"Invalid or duplicate custom graphics option");
    for(auto it=p.packs.begin();it!=p.packs.end();++it) require(packName(*it)&&std::find(p.packs.begin(),it,*it)==it,"Invalid or duplicate texture pack name");
}
bool Capabilities::available(std::string_view backend) const {
    auto found=std::find_if(backends.begin(),backends.end(),[&](const auto& b){return b.first==backend;});return found!=backends.end()&&found->second.first;
}
std::span<const char* const> controllerButtons() {
    static constexpr const char* names[]={"a","b","x","y","leftshoulder","rightshoulder","lefttrigger","righttrigger","back","start","guide","leftstick","rightstick","dpup","dpdown","dpleft","dpright","misc1","paddle1","paddle2","paddle3","paddle4","touchpad"};
    return names;
}
// Engine defaults (native/controller.cpp): shipped options.k22 actions aim=1 fire=2 run=3 menu=4 map=5 spell=6; dpad moves when unmapped.
Controller defaultController() {
    Controller c; constexpr int mapped[]={7,8,9,6,3,10,1,2,5,4,0,11,12,0,0,0,0,0,0,0,0,0,0};
    for(std::size_t i=0;i<controllerButtons().size();++i)c.buttons.emplace_back(controllerButtons()[i],mapped[i]);
    return c;
}
void validateController(const Controller& c) {
    require(c.mode=="original"||c.mode=="modern","Controller mode must be original or modern");
    require(std::isfinite(c.deadzone)&&c.deadzone>=0&&c.deadzone<=0.9,"Controller deadzone must be 0.0 through 0.9");
    require(std::isfinite(c.sensitivity)&&c.sensitivity>=0.25&&c.sensitivity<=4,"Controller sensitivity must be 0.25 through 4.0");
    require(c.buttons.size()==controllerButtons().size(),"Controller button table is incomplete");
    for(std::size_t i=0;i<c.buttons.size();++i)require(c.buttons[i].first==controllerButtons()[i]&&c.buttons[i].second>=0&&c.buttons[i].second<=12,"Controller buttons map to original joystick buttons 1 through 12 (0 = unmapped)");
}
// Mirrors the engine's reader: ';'/'#' comments, case-insensitive names, bad entries reported and defaulted, never fatal.
Controller parseController(const std::string& bytes, std::vector<std::string>& warnings) {
    Controller c=defaultController();std::string section;std::size_t start=0;
    auto trim=[](std::string_view s){while(!s.empty()&&g_ascii_isspace(s.front()))s.remove_prefix(1);while(!s.empty()&&g_ascii_isspace(s.back()))s.remove_suffix(1);return s;};
    auto lower=[](std::string_view s){std::string r(s);for(auto& ch:r)ch=g_ascii_tolower(ch);return r;};
    while(start<bytes.size()){
        auto end=bytes.find('\n',start);if(end==std::string::npos)end=bytes.size();
        auto line=std::string_view(bytes).substr(start,end-start);start=end+1;
        line=trim(line.substr(0,line.find_first_of(";#")));if(line.empty())continue;
        if(line.front()=='['&&line.back()==']'){section=lower(trim(line.substr(1,line.size()-2)));if(section!="controller"&&section!="buttons")warnings.push_back("controller.ini unknown section ["+section+"] ignored");continue;}
        auto eq=line.find('=');if(eq==std::string_view::npos){warnings.push_back("controller.ini line without '=' ignored: "+std::string(line));continue;}
        auto name=lower(trim(line.substr(0,eq)));auto value=std::string(trim(line.substr(eq+1)));
        auto real=[&](double& slot,double low,double high){double v=0;auto [e,ec]=std::from_chars(value.data(),value.data()+value.size(),v);
            if(!value.empty()&&ec==std::errc{}&&e==value.data()+value.size()&&std::isfinite(v)&&v>=low&&v<=high)slot=v;else warnings.push_back("controller.ini "+name+"="+value+" rejected; default used");};
        if(section=="buttons"){
            auto slot=std::find_if(c.buttons.begin(),c.buttons.end(),[&](const auto& b){return b.first==name;});int v=-1;auto [e,ec]=std::from_chars(value.data(),value.data()+value.size(),v);
            if(slot!=c.buttons.end()&&ec==std::errc{}&&e==value.data()+value.size()&&v>=0&&v<=12)slot->second=v;else warnings.push_back("controller.ini button "+name+"="+value+" rejected; default used");
        }
        else if(section!="controller")continue;
        else if(name=="mode"){if(value=="original"||value=="modern")c.mode=value;else warnings.push_back("controller.ini mode="+value+" rejected; original used");}
        else if(name=="vibration"){if(value=="0"||value=="1")c.vibration=value=="1";else warnings.push_back("controller.ini vibration="+value+" rejected; default used");}
        else if(name=="deadzone")real(c.deadzone,0,0.9);
        else if(name=="sensitivity")real(c.sensitivity,0.25,4);
        else warnings.push_back("controller.ini unknown key "+name+" ignored");
    }
    return c;
}
std::string encodeController(const Controller& c) {
    validateController(c);
    auto shortest=[](double v){char buffer[32];auto [end,ec]=std::to_chars(buffer,buffer+sizeof buffer,v);return std::string(buffer,end);};
    std::string out="[Controller]\nmode="+c.mode+"\ndeadzone="+shortest(c.deadzone)+"\nsensitivity="+shortest(c.sensitivity)+"\nvibration="+(c.vibration?"1":"0")+"\n\n[Buttons]\n";
    for(const auto& [name,value]:c.buttons)out+=name+"="+std::to_string(value)+"\n";
    return out;
}
void validateUpdates(const Updates& u) {
    auto slash=u.repository.find('/');auto part=[](std::string_view s){return !s.empty()&&s.size()<=100&&s!="."&&s!=".."&&std::all_of(s.begin(),s.end(),[](char c){return g_ascii_isalnum(c)||c=='-'||c=='_'||c=='.';});};
    require(slash!=std::string::npos&&part(std::string_view(u.repository).substr(0,slash))&&part(std::string_view(u.repository).substr(slash+1)),"Update repository must be OWNER/NAME");
    require(u.channel=="stable"||u.channel=="experimental","Update channel must be stable or experimental");
}
Updates parseUpdates(const std::string& bytes, std::vector<std::string>& warnings) {
    Updates u,d;KeyFile key(g_key_file_new(),g_key_file_free);
    if(!g_key_file_load_from_data(key.get(),bytes.data(),bytes.size(),G_KEY_FILE_NONE,nullptr)){warnings.push_back("updates.ini is malformed; defaults used");return u;}
    if(g_key_file_has_key(key.get(),"Updates","repository",nullptr))u.repository=keyText(key.get(),"Updates","repository");
    if(g_key_file_has_key(key.get(),"Updates","channel",nullptr))u.channel=keyText(key.get(),"Updates","channel");
    try{validateUpdates({u.repository,d.channel});}catch(const std::exception& e){warnings.push_back(std::string("updates.ini: ")+e.what()+"; default used");u.repository=d.repository;}
    try{validateUpdates({d.repository,u.channel});}catch(const std::exception& e){warnings.push_back(std::string("updates.ini: ")+e.what()+"; default used");u.channel=d.channel;}
    return u;
}
std::string encodeUpdates(const Updates& u) { validateUpdates(u); return "[Updates]\nrepository="+u.repository+"\nchannel="+u.channel+"\n"; }
Capabilities parseCapabilities(const std::string& text) {
    return parsedJson(text,"--list-capabilities",[](const json& j){
        Capabilities c;c.version=j.at("version").get<std::string>();
        for(const auto& [name,b]:j.at("backends").items()){require(name=="opengl"||name=="vulkan-raster"||name=="vulkan-rt","Unknown backend in capabilities: "+name);c.backends.push_back({name,{b.at("available").get<bool>(),optionalText(b,"detail")}});}
        c.presets=texts(j.at("presets"));for(const auto& p:c.presets)require(p=="original"||p=="enhanced"||p=="ultra"||p=="custom","Unknown preset in capabilities: "+p);
        for(const auto& o:j.at("options")){
            Option option{o.at("key").get<std::string>(),optionalText(o,"label"),o.at("default").get<std::string>(),texts(o.at("values")),texts(o.at("backends"))};
            require(token(option.key)&&!option.values.empty()&&std::all_of(option.values.begin(),option.values.end(),token)&&std::count(option.values.begin(),option.values.end(),option.fallback)==1,"Invalid capability option: "+option.key);
            for(const auto& b:option.backends)require(b=="opengl"||b=="vulkan-raster"||b=="vulkan-rt","Unknown backend for option "+option.key);
            if(option.label.empty())option.label=option.key;
            c.options.push_back(std::move(option));
        }
        if(j.contains("texture_packs"))for(const auto& p:j.at("texture_packs")){
            Pack pack{p.at("name").get<std::string>(),optionalText(p,"path"),optionalText(p,"detail"),p.at("valid").get<bool>(),optionalText(p,"display_name")};
            require(packName(pack.name),"Invalid texture pack name in capabilities");c.packs.push_back(std::move(pack));
        }
        return c;
    });
}
Verify parseVerify(const std::string& text) {
    return parsedJson(text,"--verify",[](const json& j){return Verify{j.at("ok").get<bool>(),j.at("checked").get<long long>(),texts(j.at("missing")),texts(j.at("modified"))};});
}
UpdateCheck parseCheck(const std::string& text) {
    return parsedJson(text,"--check",[](const json& j){
        return UpdateCheck{j.at("current").get<std::string>(),optionalText(j,"latest"),optionalText(j,"channel"),optionalText(j,"notes"),optionalText(j,"published"),optionalText(j,"repository"),j.at("update_available").get<bool>()};
    });
}
UpdateStatus parseStatus(const std::string& text) {
    return parsedJson(text,"--status",[](const json& j){return UpdateStatus{optionalText(j,"active"),optionalText(j,"previous"),texts(j.at("installed"))};});
}
std::optional<std::pair<int,std::string>> progressLine(std::string_view line) {
    if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
    if(!line.starts_with("progress "))return std::nullopt;
    line.remove_prefix(9);
    int value=-1;auto [end,ec]=std::from_chars(line.data(),line.data()+line.size(),value);
    if(ec!=std::errc{}||value<0||value>100)return std::nullopt;
    std::string_view rest(end,line.data()+line.size()-end);
    if(rest.empty())return std::pair{value,std::string()};
    if(rest.front()!=' ')return std::nullopt;
    return std::pair{value,std::string(rest.substr(1))};
}
fs::path logDirectory() { return fs::path(g_get_user_state_dir())/"kabus22/logs"; }
std::vector<fs::path> listLogs(const fs::path& folder) {
    std::vector<fs::path> logs;std::error_code error;
    for(const auto& entry:fs::directory_iterator(folder,error)){auto name=entry.path().filename().string();if(entry.is_regular_file()&&!entry.is_symlink()&&name.ends_with(".log")&&name.find('-')!=std::string::npos)logs.push_back(entry.path());}
    // Names are kind-YYYYmmdd-HHMMSS-micro.log: order by timestamp, oldest first.
    std::sort(logs.begin(),logs.end(),[](const fs::path& a,const fs::path& b){auto x=a.filename().string(),y=b.filename().string();return std::pair(x.substr(x.find('-')),x)<std::pair(y.substr(y.find('-')),y);});
    return logs;
}
std::pair<fs::path,int> openLog(const fs::path& folder, std::string_view kind, std::size_t keep) {
    require(token(kind)&&kind.find('-')==std::string_view::npos&&keep>0,"Invalid log kind");
    Fd dir(directory(folder,true));struct stat st{};
    require(::fstat(dir.value,&st)==0&&st.st_uid==::geteuid()&&!(st.st_mode&0022),"Log folder is not private and user-owned: "+folder.string());
    std::vector<fs::path> same;for(auto& log:listLogs(folder))if(log.filename().string().starts_with(std::string(kind)+"-"))same.push_back(log);
    for(std::size_t i=0;i+keep<=same.size();++i)::unlinkat(dir.value,same[i].filename().c_str(),0);
    GDateTime* now=g_date_time_new_now_local();gchar* stamp=g_date_time_format(now,"%Y%m%d-%H%M%S-%f");std::string name=std::string(kind)+"-"+stamp+".log";g_free(stamp);g_date_time_unref(now);
    int fd=::openat(dir.value,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    require(fd>=0,"Cannot create log "+(folder/name).string()+": "+std::strerror(errno));
    return {folder/name,fd};
}
fs::path defaultRoot() { return fs::path(g_get_user_data_dir())/"kabus22"; }
// Versioned installs run from versions/<active>/bin; never follow the compat bin link for executable admission.
fs::path binDirectory(const fs::path& root) { auto active=layout::active_version(root); return active.empty()?root/"bin":root/"versions"/active/"bin"; }
Settings load(const fs::path& root, const fs::path& executable) {
    Settings s; s.root=checkedAbsolute(root); s.engine=executable.empty()?binDirectory(s.root)/"kabus22":checkedAbsolute(executable);
    Fd folder(directory(s.root)); struct stat st{};
    require(::fstat(folder.value,&st)==0&&st.st_uid==::geteuid()&&!(st.st_mode&0022),"Installation must be private and user-owned");
    require(snapshot(s.root/".kabus22-owned").bytes==std::optional<std::string>("Kabus22 native installation 1\n"),"Not a Kabus22 native installation (ownership marker missing)");
    binary(s.engine); { Fd game(directory(s.root/"game")); }
    s.privateRoot=defaultRoot(); s.configFile=fs::path(g_get_user_config_dir())/"kabus22/launcher.ini";
    s.campaignPrivate=snapshot(s.privateRoot/"options.k22",true); s.campaignSource=snapshot(s.root/"game/options.k22");
    s.campaign=campaignBytes(effective(s.campaignPrivate,s.campaignSource)); validateCampaign(s.campaign);
    require(s.campaignSource.bytes.has_value(),"Supplied campaign defaults missing"); s.defaults=campaignBytes(*s.campaignSource.bytes); validateCampaign(s.defaults);
    s.originalCampaign=originalEntry(s.root/"game");s.languages=campaignLanguages(s.root/"game");
    s.bonusAvailable=fs::is_regular_file(s.root/"game/Kabus 22 Mode/K22_mode.wrs");
    if(s.bonusAvailable) {
        s.bonusPrivate=snapshot(s.privateRoot/"mode_ayarlar.k22",true); s.bonusSource=snapshot(s.root/"game/Kabus 22 Mode/mode_ayarlar.k22");
        s.bonus=decodeBonus(effective(s.bonusPrivate,s.bonusSource)); require(s.bonusSource.bytes.has_value(),"Supplied Bonus defaults missing"); s.bonusDefaults=decodeBonus(*s.bonusSource.bytes);
        s.localePrivate=snapshot(s.privateRoot/"ygm.k22",true); s.localeSource=snapshot(s.root/"game/Kabus 22 Mode/ygm.k22");
        // Bonus launcher (entry-contract.txt): rms0/1/2 = 000/111/222; 111 -> tr, 222 -> de, anything else -> en.
        s.preferences.bonusLanguage=bonusMarkerLanguage(effective(s.localePrivate,s.localeSource));
    }
    const char* locale=g_get_language_names()[0]; std::string language=locale?locale:"en";
    s.preferences.language=language.starts_with("tr")?"tr":language.starts_with("de")?"de":"en";
    s.preferences.audioLanguage=s.preferences.language=="tr"?"tr":"en";
    s.config=snapshot(s.configFile,true); if(s.config.bytes) s.preferences=parsePreferences(*s.config.bytes,s.preferences);
    if(s.preferences.language!="saved"&&!std::count(s.languages.begin(),s.languages.end(),s.preferences.language))s.preferences.language=language.starts_with("tr")?"tr":language.starts_with("de")?"de":"en";
    s.preferences.language=savedCampaignLanguage(s.campaign,s.preferences.language,s.languages);
    s.initialLanguage=s.preferences.language;
    s.controllerFile=snapshot(s.configFile.parent_path()/"controller.ini",true); s.controller=s.controllerFile.bytes?parseController(*s.controllerFile.bytes,s.warnings):defaultController();
    s.updatesFile=snapshot(s.configFile.parent_path()/"updates.ini",true); if(s.updatesFile.bytes) s.updates=parseUpdates(*s.updatesFile.bytes,s.warnings);
    return s;
}
void saveUpdates(Settings& s) {
    validateUpdates(s.updates); Fd lock(privateLock(s.privateRoot)); unchanged(s.updatesFile,true);
    if(!s.updatesFile.bytes&&s.updates==Updates{}) return;
    std::vector<std::string> ignored; if(s.updatesFile.bytes&&parseUpdates(*s.updatesFile.bytes,ignored)==s.updates) return;
    atomicWrite(s.updatesFile,encodeUpdates(s.updates));
}
void save(Settings& s) {
    validateCampaign(s.campaign); validatePreferences(s.preferences);
    if(s.bonusAvailable) (void)decodeBonus(encodeBonus(s.bonus));
    require(s.preferences.mode!="bonus"||s.bonusAvailable,"Bonus data is not installed");
    Fd lock(privateLock(s.privateRoot));
    // Check every source before any publication, so stale game edits cannot be overwritten.
    for(const auto* expected:{&s.campaignPrivate,&s.config,&s.controllerFile,&s.updatesFile}) unchanged(*expected,true);
    unchanged(s.campaignSource,false);
    if(s.bonusAvailable) { unchanged(s.bonusPrivate,true); unchanged(s.bonusSource,false); unchanged(s.localePrivate,true); unchanged(s.localeSource,false); }
    if(s.campaign.size()==32&&s.preferences.language!=s.initialLanguage&&s.preferences.language!="saved")
        s.campaign[31]=static_cast<unsigned char>(languageIndex(s.preferences.language));
    const std::string bytes(s.campaign.begin(),s.campaign.end());
    if(bytes!=effective(s.campaignPrivate,s.campaignSource)) atomicWrite(s.campaignPrivate,bytes);
    if(s.bonusAvailable) {
        if(s.bonus!=decodeBonus(effective(s.bonusPrivate,s.bonusSource))) atomicWrite(s.bonusPrivate,encodeBonus(s.bonus));
        // Exact original rms*.txt markers (3 bytes, no newline).
        if(bonusMarkerLanguage(effective(s.localePrivate,s.localeSource))!=s.preferences.bonusLanguage) atomicWrite(s.localePrivate,bonusMarker(s.preferences.bonusLanguage));
    }
    atomicWrite(s.config,encodePreferences(s.preferences));
    // An absent controller.ini means original mode; rewrite only on a real change so user comments survive.
    std::vector<std::string> ignored; validateController(s.controller); validateUpdates(s.updates);
    if(s.controller!=(s.controllerFile.bytes?parseController(*s.controllerFile.bytes,ignored):defaultController())) atomicWrite(s.controllerFile,encodeController(s.controller));
    if((s.updatesFile.bytes||s.updates!=Updates{})&&s.updates!=(s.updatesFile.bytes?parseUpdates(*s.updatesFile.bytes,ignored):Updates{})) atomicWrite(s.updatesFile,encodeUpdates(s.updates));
}
std::vector<std::string> command(const Settings& s, const std::optional<unsigned long long>& frames) {
    validatePreferences(s.preferences); binary(s.engine); const auto& p=s.preferences;
    require(p.mode!="bonus"||s.bonusAvailable,"Bonus data is not installed");
    const fs::path data=s.root/"game"/(p.mode=="bonus"?"Kabus 22 Mode":""); { Fd folder(directory(data)); }
    std::vector<std::string> args={s.engine.string(),"--data",data.string(),"--mode",p.mode};
    if(p.mode=="bonus")args.insert(args.end(),{"--language",p.bonusLanguage});
    else if(p.language!="saved"&&(s.campaign.size()!=32||p.language!=s.initialLanguage)){
        require(std::count(s.languages.begin(),s.languages.end(),p.language)==1,"Text language "+p.language+" needs the community patch language data, which is not installed");
        args.insert(args.end(),{"--language",p.language});
    }
    if(p.mode=="campaign") { args.insert(args.end(),{"--audio-language",p.audioLanguage});
        if(s.originalCampaign) { args.insert(args.end(),{"--display",p.display});
            if(p.display=="native") { args.insert(args.end(),{"--window-mode",p.window}); if(!p.resolution.empty()&&p.window!="borderless") args.insert(args.end(),{"--resolution",p.resolution}); }
            else if(p.window!="windowed") args.insert(args.end(),{"--define","deftam_d3d"});
        }
        if(s.originalCampaign&&p.camera!="original") args.insert(args.end(),{"--camera",p.camera});
    } else args.insert(args.end(),{"--define","defanti_"+std::to_string(p.bonusAA)});
    if(p.renderer=="opengl") args.insert(args.end(),{"--renderer","opengl"});
    else args.insert(args.end(),{"--renderer","vulkan","--ray-tracing",p.renderer=="vulkan-rt"?"on":"off"});
    if(s.capabilities) {
        const auto& c=*s.capabilities;bool packs=false;
        if(p.preset!="original"){require(std::count(c.presets.begin(),c.presets.end(),p.preset)==1,"Graphics preset "+p.preset+" is not offered by this engine; choose another on Rendering");args.insert(args.end(),{"--preset",p.preset});}
        // backend and texture_packs have dedicated controls; stale or inapplicable custom values fall back to the engine default.
        if(p.preset=="custom")for(const auto& [key,value]:p.graphics){
            auto o=std::find_if(c.options.begin(),c.options.end(),[&](const Option& o){return o.key==key;});
            if(o==c.options.end()||key=="backend"||key=="texture_packs"||value==o->fallback||std::count(o->values.begin(),o->values.end(),value)!=1||std::count(o->backends.begin(),o->backends.end(),p.renderer)!=1)continue;
            args.insert(args.end(),{"--graphics",key+"="+value});
        }
        for(const auto& name:p.packs)if(std::any_of(c.packs.begin(),c.packs.end(),[&](const Pack& k){return k.valid&&k.name==name;})){args.insert(args.end(),{"--texture-pack",name});packs=true;}
        // Explicit packs imply texture_packs=1; with none checked, keep enhanced/ultra from auto-loading every installed pack.
        if(!packs&&(p.preset=="enhanced"||p.preset=="ultra")&&std::any_of(c.options.begin(),c.options.end(),[](const Option& o){return o.key=="texture_packs";}))args.insert(args.end(),{"--graphics","texture_packs=0"});
    }
    if(frames) args.insert(args.end(),{"--frames",std::to_string(*frames)});
    return args;
}
void selfCheck() {
    const std::vector<unsigned char> original={2,1,0,0,1,0,1,0,50,100,80,130,65,90,100,72,80,77,75,31,32,30,50,35,16,1,2,3,4,5,6};
    validateCampaign(original); auto patched=original; patched.push_back(5); validateCampaign(patched);
    for(int code:{66,67,68}){auto rebound=original;rebound[15]=code;validateCampaign(rebound);}
    auto rejects=[](auto action) { bool rejected=false; try{action();}catch(const std::exception&){rejected=true;} require(rejected,"Invalid settings accepted by self-check"); };
    for(int failure=0;failure<5;++failure) { auto invalid=original; if(failure==0) invalid.pop_back(); if(failure==1) invalid[9]=201; if(failure==2) invalid[15]=28; if(failure==3) invalid[16]=invalid[15]; if(failure==4) invalid[25]=0; rejects([&]{validateCampaign(invalid);}); }
    auto bonus=decodeBonus("2\r\n2\r\n0\r\n1\r\n50\r\n"); require(decodeBonus(encodeBonus(bonus))==bonus,"Bonus roundtrip regression");
    for(const std::string invalid:{"2\n2\n0\n1","4\n2\n0\n1\n50\n","2\n2\n0\n1\n50\nextra","2\n2\n0\n1\n50junk\n"}) rejects([&]{decodeBonus(invalid);});
    Preferences p; p.renderer="vulkan-raster"; p.resolution="2560x1440"; require(parsePreferences(encodePreferences(p)).renderer==p.renderer,"Preference roundtrip regression");
    rejects([&]{parsePreferences("[Launcher]\nrenderer=unknown\n");});
    rejects([&]{parsePreferences("[Launcher]\nbonus-aa=not-a-number\n");});
    // Exercise real private publication, symlink refusal and stale-update admission, not UI wiring.
    GError* error=nullptr; gchar* name=g_dir_make_tmp("kabus22-launcher-check-XXXXXX",&error);
    if(!name) { std::string message=error?error->message:"Cannot create self-check folder"; if(error)g_error_free(error); throw std::runtime_error(message); }
    fs::path scratch=name; g_free(name);
    try {
        auto expected=snapshot(scratch/"settings",true); atomicWrite(expected,"first"); require(expected.bytes==std::optional<std::string>("first"),"Atomic settings publication regression");
        auto stale=expected; atomicWrite(expected,"second"); rejects([&]{atomicWrite(stale,"overwrite");});
        require(::symlink("settings",(scratch/"link").c_str())==0,"Cannot create link self-check"); rejects([&]{snapshot(scratch/"link",true);});
        struct stat st{}; require(::stat((scratch/"settings").c_str(),&st)==0&&(st.st_mode&0777)==0600,"Settings permission regression");
        fs::create_directory(scratch/"game");
        {Fd plain(directory(scratch/"game"));Fd trailing(directory(scratch/"game"/""));struct stat a{},b{};
            require(::fstat(plain.value,&a)==0&&::fstat(trailing.value,&b)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino,"Trailing directory separator admission regression");}
        require(::symlink("game",(scratch/"linked-game").c_str())==0,"Cannot create directory link self-check");
        rejects([&]{Fd unsafe(directory(scratch/"linked-game"/""));});
        require(::mkfifo((scratch/"engine-fifo").c_str(),0700)==0,"Cannot create executable FIFO self-check");
        rejects([&]{binary(scratch/"engine-fifo");});
        {Fd held(privateLock(scratch));auto pid=::fork();require(pid>=0,"Cannot fork lock self-check");
            if(pid==0){::close(held.value);bool refused=false;try{Fd competing(privateLock(scratch));}catch(const std::exception&){refused=true;}::_exit(refused?0:1);}
            int status=0;pid_t waited;do{waited=::waitpid(pid,&status,0);}while(waited<0&&errno==EINTR);
            require(waited==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"Interprocess settings lock admitted a competing writer");}
        {Fd released(privateLock(scratch));}
        auto entry=snapshot(scratch/"game/K22.wrs");atomicWrite(entry,"entry");
        // Copy the engine stand-in into the private scratch tree: the build tree's ancestors may be legitimately group-writable.
        fs::copy_file("/proc/self/exe",scratch/"kabus22");
        Settings launch;launch.root=scratch;launch.engine=scratch/"kabus22";launch.campaign=patched;launch.originalCampaign=originalEntry(scratch/"game");
        require(launch.originalCampaign,"Private 32-byte options disabled original installed entry");
        auto args=command(launch);require(std::find(args.begin(),args.end(),"--window-mode")!=args.end(),"Original native display arguments omitted with 32-byte options");
        const std::vector<std::string> patchLanguages{"tr","en","de","es","ru","pl"};
        for(bool installed:{false,true})for(int index=0;index<=5;++index){
            launch.languages=installed?patchLanguages:std::vector<std::string>{"tr","en","de"};
            launch.campaign[31]=index;launch.preferences.language=savedCampaignLanguage(launch.campaign,"en",launch.languages);launch.initialLanguage=launch.preferences.language;
            constexpr const char* expectedLanguage[]={"tr","en","de","saved","saved","saved"};
            require(launch.preferences.language==(installed?patchLanguages[index]:std::string(expectedLanguage[index])),"Saved patch language initialization regression");
            args=command(launch);require(std::find(args.begin(),args.end(),"--language")==args.end(),"Saved patch locale was overridden without a user change");
        }
        launch.preferences.language="tr";args=command(launch);auto languageArg=std::find(args.begin(),args.end(),"--language");
        require(languageArg!=args.end()&&*std::next(languageArg)=="tr","Intentional patch language selection omitted");
        launch.preferences.language="ru";args=command(launch);languageArg=std::find(args.begin(),args.end(),"--language");
        require(languageArg!=args.end()&&*std::next(languageArg)=="ru","Installed patch language es/ru/pl selection omitted");
        launch.languages={"tr","en","de"};rejects([&]{command(launch);});launch.preferences.language="tr";
        // Patch language data discovery: K22Widescreen.wrs plus _data/language/<folder>/subtitle/general.txt per language.
        fs::create_directories(scratch/"patched/_data/language/polish/subtitle");fs::create_directories(scratch/"patched/_data/language/spanish");
        {auto general=snapshot(scratch/"patched/_data/language/polish/subtitle/general.txt");atomicWrite(general,"x");}
        require(campaignLanguages(scratch/"patched")==std::vector<std::string>{"tr","en","de"},"Patch languages admitted without K22Widescreen.wrs");
        {auto archive=snapshot(scratch/"patched/K22Widescreen.wrs");atomicWrite(archive,"WRS2");}
        require(campaignLanguages(scratch/"patched")==std::vector<std::string>{"tr","en","de","pl"},"Patch language data discovery regression");
        // Optional first-person camera: original campaign scripts only, persisted, absent by default.
        auto hasCamera=[&]{auto all=command(launch);auto at=std::find(all.begin(),all.end(),"--camera");return at!=all.end()&&*std::next(at)=="first-person";};
        require(!hasCamera(),"Default launch requested a camera mode");launch.preferences.camera="first-person";require(hasCamera(),"First-person camera argument omitted");
        launch.originalCampaign=false;require(!hasCamera(),"Camera option sent to patched scripts");launch.originalCampaign=true;
        launch.preferences.mode="bonus";launch.bonusAvailable=true;fs::create_directories(scratch/"game/Kabus 22 Mode");require(!hasCamera(),"Camera option sent to Bonus");
        launch.preferences.mode="campaign";launch.preferences.camera="original";launch.bonusAvailable=false;
        {Preferences camera;camera.camera="first-person";require(parsePreferences(encodePreferences(camera)).camera=="first-person","Camera preference roundtrip regression");}
        rejects([&]{parsePreferences("[Launcher]\ncamera=fps\n");});rejects([&]{parsePreferences("[Launcher]\nlanguage=fr\n");});
        // Bonus launcher ygm.k22 (entry-contract.txt rms0/1/2 = 000/111/222; consumer 111 tr, 222 de, else en).
        require(bonusMarkerLanguage("000")=="en"&&bonusMarkerLanguage("111")=="tr"&&bonusMarkerLanguage("222\r\n")=="de"&&bonusMarkerLanguage("333")=="en"&&bonusMarkerLanguage("")=="en","Bonus locale marker mapping regression");
        require(bonusMarker("en")=="000"&&bonusMarker("tr")=="111"&&bonusMarker("de")=="222","Bonus locale marker encoding regression");
        // Contract D capability JSON and preset/graphics/pack argument composition.
        const std::string caps=R"({"version":"0.2.0","backends":{"opengl":{"available":true,"detail":"Mesa"},"vulkan-raster":{"available":true,"detail":"RADV"},"vulkan-rt":{"available":false,"detail":"no ray query"}},"presets":["original","enhanced","ultra","custom"],)"
            R"("options":[{"key":"backend","values":["opengl","vulkan-raster","vulkan-rt"],"default":"opengl","backends":["opengl","vulkan-raster","vulkan-rt"],"label":"Backend"},{"key":"pbr","values":["0","1"],"default":"0","backends":["opengl","vulkan-raster","vulkan-rt"],"label":"PBR materials"},)"
            R"({"key":"msaa","values":["1","2","4","8"],"default":"4","backends":["opengl","vulkan-raster","vulkan-rt"],"label":"MSAA"},{"key":"rt_only","values":["0","1"],"default":"0","backends":["vulkan-rt"],"label":"RT only"},)"
            R"({"key":"texture_packs","values":["0","1"],"default":"0","backends":["opengl","vulkan-raster","vulkan-rt"],"label":"Texture packs"}],)"
            R"("texture_packs":[{"name":"hd pack","display_name":"HD Pack","path":"/packs/hd pack","valid":true,"detail":"ok"},{"name":"broken","display_name":"","path":"/packs/broken","valid":false,"detail":"bad manifest"}]})";
        auto capabilities=parseCapabilities(caps);
        require(capabilities.version=="0.2.0"&&capabilities.available("opengl")&&!capabilities.available("vulkan-rt")&&!capabilities.available("missing")&&capabilities.options.size()==5&&capabilities.options[2].fallback=="4"&&capabilities.packs.size()==2&&capabilities.packs[0].display=="HD Pack"&&!capabilities.packs[1].valid,"Capabilities parsing regression");
        for(const std::string invalid:{"not json","[]","{}",R"({"version":"x","backends":{"d3d9":{"available":true}},"presets":[],"options":[]})",
            R"({"version":"x","backends":{},"presets":["original"],"options":[{"key":"pbr","values":["0","1"],"default":"2","backends":["opengl"]}]})",
            R"({"version":"x","backends":{},"presets":["original"],"options":[{"key":"p r","values":["0"],"default":"0","backends":["opengl"]}]})",
            R"({"version":"x","backends":{},"presets":["original"],"options":[],"texture_packs":[{"name":"../up","valid":true}]})"}) rejects([&]{parseCapabilities(invalid);});
        auto tail=[&]{auto all=command(launch);return std::vector<std::string>(std::find(all.begin(),all.end(),"--renderer"),all.end());};
        using Args=std::vector<std::string>;
        launch.preferences.preset="ultra";launch.preferences.packs={"hd pack"};require(tail()==Args{"--renderer","opengl"},"Graphics arguments emitted to an engine without capabilities");
        launch.capabilities=capabilities;launch.preferences.preset="original";launch.preferences.packs.clear();require(tail()==Args{"--renderer","opengl"},"Original preset changed the legacy command line");
        launch.preferences.preset="ultra";require(tail()==Args{"--renderer","opengl","--preset","ultra","--graphics","texture_packs=0"},"Unchecked packs did not stay disabled under a preset");
        launch.preferences.preset="custom";launch.preferences.graphics={{"pbr","1"},{"msaa","4"},{"rt_only","1"},{"backend","vulkan-rt"},{"stale","1"},{"texture_packs","0"}};launch.preferences.packs={"broken","hd pack","absent"};
        require(tail()==Args{"--renderer","opengl","--preset","custom","--graphics","pbr=1","--texture-pack","hd pack"},"Custom graphics argument composition regression");
        launch.preferences.renderer="vulkan-rt";require(tail()==Args{"--renderer","vulkan","--ray-tracing","on","--preset","custom","--graphics","pbr=1","--graphics","rt_only=1","--texture-pack","hd pack"},"Ray-tracing option composition regression");
        launch.capabilities->presets={"original","custom"};launch.preferences.preset="ultra";rejects([&]{command(launch);});
        launch.capabilities.reset();launch.preferences=Preferences{};launch.preferences.language="tr";
        Preferences graphics;graphics.preset="custom";graphics.graphics={{"pbr","1"},{"msaa","8"}};graphics.packs={"hd pack","semi;colon"};
        auto back=parsePreferences(encodePreferences(graphics));require(back.preset==graphics.preset&&back.graphics==graphics.graphics&&back.packs==graphics.packs,"Graphics preference roundtrip regression");
        rejects([&]{parsePreferences("[Launcher]\npreset=extreme\n");});rejects([&]{parsePreferences("[Launcher]\ntexture-packs=../x;\n");});rejects([&]{parsePreferences("[Graphics]\npbr=1 2\n");});
        // Contract F controller.ini: exact roundtrip, engine-equivalent lenient reading, strict writing.
        std::vector<std::string> warnings;auto pad=defaultController();require(parseController(encodeController(pad),warnings)==pad&&warnings.empty(),"Default controller roundtrip regression");
        pad.mode="modern";pad.deadzone=0.25;pad.sensitivity=1.5;pad.vibration=false;pad.buttons[0].second=0;pad.buttons[13].second=4;
        require(parseController(encodeController(pad),warnings)==pad&&warnings.empty(),"Modern controller roundtrip regression");
        auto lenient=parseController("; comment\n[Controller]\nMode = modern ; inline\ndeadzone=0.95\nsensitivity=abc\nvibration=2\nfoo=1\n[Buttons]\nA=3\nlefttrigger=13\nnosuch=1\n[Other]\nx=1\n",warnings);
        require(lenient.mode=="modern"&&lenient.deadzone==0.15&&lenient.sensitivity==1.0&&lenient.vibration&&lenient.buttons[0].second==3&&lenient.buttons[6].second==1&&warnings.size()==7,"Controller rejection/default regression");
        for(int failure=0;failure<4;++failure){auto bad=defaultController();if(failure==0)bad.deadzone=1;if(failure==1)bad.sensitivity=0.1;if(failure==2)bad.buttons[3].second=13;if(failure==3)bad.mode="xinput";rejects([&]{encodeController(bad);});}
        // Contract B updates.ini.
        warnings.clear();Updates fork{"someone/kabus22-fork","experimental"};require(parseUpdates(encodeUpdates(fork),warnings)==fork&&warnings.empty(),"Updates roundtrip regression");
        require(parseUpdates("[Updates]\nrepository=no-slash\nchannel=beta\n",warnings)==Updates{}&&warnings.size()==2,"Invalid updates.ini accepted");
        for(const Updates& bad:{Updates{"a/b/c","stable"},Updates{"../x","stable"},Updates{"a/b","nightly"}})rejects([&]{encodeUpdates(bad);});
        // Contract C / B JSON outputs and progress lines.
        auto verify=parseVerify(R"({"ok":false,"checked":520,"missing":["game/K22.wrs"],"modified":["game/a b.wdl"]})");
        require(!verify.ok&&verify.checked==520&&verify.missing==Args{"game/K22.wrs"}&&verify.modified==Args{"game/a b.wdl"},"Verify JSON regression");rejects([&]{parseVerify(R"({"ok":"yes","checked":1,"missing":[],"modified":[]})");});
        auto check=parseCheck(R"({"current":"0.2.0","latest":"0.2.1","channel":"stable","update_available":true,"notes":"# Notes\n- fixed","published":"2026-10-09T10:00:00Z","repository":"sergen213/kabus22-linux"})");
        require(check.available&&check.latest=="0.2.1"&&check.notes=="# Notes\n- fixed"&&check.published=="2026-10-09T10:00:00Z","Update check JSON regression");
        require(!parseCheck(R"({"current":"0.2.0","latest":null,"channel":"experimental","update_available":false,"repository":"a/b"})").available,"Update check without release regression");rejects([&]{parseCheck(R"({"latest":"0.2.1"})");});
        auto status=parseStatus(R"({"active":"0.2.0","previous":"0.1.1","installed":["0.1.1","0.2.0"]})");require(status.active=="0.2.0"&&status.previous=="0.1.1"&&status.installed.size()==2,"Update status JSON regression");
        require(parseStatus(R"({"active":"","previous":null,"installed":[]})").previous.empty(),"Flat status JSON regression");
        require(progressLine("progress 42 Extracting game")==std::pair{42,std::string("Extracting game")}&&progressLine("progress 100")==std::pair{100,std::string()}&&progressLine("progress 7 msg\r")==std::pair{7,std::string("msg")},"Progress line parsing regression");
        for(auto bad:{"progress 101 x","progress -1 x","progress 5x","progressing 5","Progress 5","progress  5 x","42"})require(!progressLine(bad),"Malformed progress line accepted");
        // Log rotation keeps the newest ten per kind, private, without touching other kinds.
        fs::path newest;for(int i=0;i<12;++i){auto [path,fd]=openLog(scratch/"state/logs","engine");::close(fd);newest=path;if(i<2){auto [p,f]=openLog(scratch/"state/logs","setup");::close(f);}g_usleep(1000);}
        auto logs=listLogs(scratch/"state/logs");auto kind=[&](const char* k){return std::count_if(logs.begin(),logs.end(),[&](const fs::path& p){return p.filename().string().starts_with(k);});};
        require(logs.size()==12&&kind("engine-")==10&&kind("setup-")==2&&logs.back()==newest,"Log rotation regression");
        {struct stat ls{};require(::stat(newest.c_str(),&ls)==0&&(ls.st_mode&0777)==0600,"Log permission regression");}
        rejects([&]{openLog(scratch/"state/logs","../x");});
        auto patchEntry=snapshot(scratch/"game/K22Widescreen.wdl");atomicWrite(patchEntry,"patched entry");
        require(!originalEntry(scratch/"game"),"Patched entry admitted original display controls");
        auto sourceOptions=snapshot(scratch/"game/options.k22");atomicWrite(sourceOptions,std::string(patched.begin(),patched.end()));
        Settings first;first.privateRoot=scratch/"private";first.campaign=patched;first.campaignSource=sourceOptions;first.campaignPrivate=snapshot(first.privateRoot/"options.k22",true);first.config=snapshot(first.privateRoot/"launcher.ini",true);first.preferences.language="saved";first.initialLanguage="saved";
        first.controllerFile=snapshot(first.privateRoot/"controller.ini",true);first.updatesFile=snapshot(first.privateRoot/"updates.ini",true);first.controller=defaultController();
        auto second=first;first.campaign[12]=75;save(first);second.campaign[12]=80;
        rejects([&]{save(second);});
        require(static_cast<unsigned char>((*first.campaignPrivate.bytes)[31])==5,"Unrelated Apply changed saved unsupported patch locale");
        require(!fs::exists(first.privateRoot/"controller.ini")&&!fs::exists(first.privateRoot/"updates.ini"),"Default controller/update settings created files");
        first.preferences.language="tr";save(first);
        require(static_cast<unsigned char>((*first.campaignPrivate.bytes)[31])==0,"Intentional patch locale Apply was not persisted");
        first.controller=pad;first.updates=fork;save(first);warnings.clear();
        require(first.controllerFile.bytes&&parseController(*first.controllerFile.bytes,warnings)==pad&&first.updatesFile.bytes==encodeUpdates(fork),"Controller/update settings were not persisted");
        // Comments in a user-edited controller.ini survive an Apply that does not change it.
        auto edited=encodeController(pad)+"; user note\n";atomicWrite(first.controllerFile,edited);save(first);require(first.controllerFile.bytes==edited,"Unchanged controller.ini was rewritten");
        first.updates.channel="stable";saveUpdates(first);require(first.updatesFile.bytes==encodeUpdates(first.updates),"saveUpdates regression");
        fs::remove_all(scratch);
    } catch(...) { fs::remove_all(scratch); throw; }
}
}
