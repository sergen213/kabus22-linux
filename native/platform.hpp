#pragma once
// Platform services behind the engine's POSIX/Windows branches: executable path, the Windows-1254 code page, and on Windows the
// user data root, file primitives that never traverse reparse points (CreateFile2: desktop and UWP), the user locale and
// DirectWrite system font bytes. Linux keeps its XDG/HOME, iconv and fontconfig code at each call site.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

namespace k22::platform {
// Characters that split a user-supplied directory name (mods/, packs/) into path components; a name holding one could leave its
// parent folder (on Windows also a backslash component or a drive "C:").
#ifdef _WIN32
inline constexpr std::string_view pathSeparators="/\\:";
#else
inline constexpr std::string_view pathSeparators="/";
#endif
// Windows-1254 (Turkish) to Unicode by Microsoft's published table (unicode.org MAPPINGS/VENDORS/MICSFT/WINDOWS/CP1254.TXT);
// 0x81, 0x8D-0x90, 0x9D and 0x9E are undefined there and map to cp1254Undefined here.
inline constexpr char32_t cp1254Undefined=0xFFFF;
inline constexpr std::array<char16_t,32> cp1254High{0x20AC,0xFFFF,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0xFFFF,0xFFFF,0xFFFF,
    0xFFFF,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0xFFFF,0xFFFF,0x0178};
constexpr char32_t cp1254_to_unicode(std::uint8_t byte) noexcept {
    if(byte>=0x80&&byte<0xA0)return cp1254High[byte-0x80];
    switch(byte){case 0xD0:return 0x011E;case 0xDD:return 0x0130;case 0xDE:return 0x015E;case 0xF0:return 0x011F;case 0xFD:return 0x0131;case 0xFE:return 0x015F;default:return byte;}
}
inline void append_utf8(std::string& out,char32_t cp){
    if(cp<0x80)out+=char(cp);
    else if(cp<0x800){out+=char(0xC0|(cp>>6));out+=char(0x80|(cp&63));}
    else if(cp<0x10000){out+=char(0xE0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
    else{out+=char(0xF0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
}
// std::filesystem::canonical / weakly_canonical, except on UWP: the AppContainer denies the final-path query they make on the
// package's install folder and on LocalState's parents, so there the absolute, lexically normal path stands in (UWP app paths
// hold no links the app could have created). canonical's "must exist" failure is kept.
inline std::filesystem::path canonical_path(const std::filesystem::path& path,std::error_code& error){
#if K22_UWP
    auto result=std::filesystem::absolute(path,error).lexically_normal();
    if(!error&&!std::filesystem::exists(result,error)&&!error)error=std::make_error_code(std::errc::no_such_file_or_directory);
    return error?std::filesystem::path{}:result;
#else
    return std::filesystem::canonical(path,error);
#endif
}
inline std::filesystem::path canonical_path(const std::filesystem::path& path){
    std::error_code error;auto result=canonical_path(path,error);
    if(error)throw std::filesystem::filesystem_error("canonical",path,error);
    return result;
}
inline std::filesystem::path weakly_canonical_path(const std::filesystem::path& path){
#if K22_UWP
    return std::filesystem::absolute(path).lexically_normal();
#else
    return std::filesystem::weakly_canonical(path);
#endif
}
// This running binary (/proc/self/exe, GetModuleFileNameW); empty when unavailable.
std::filesystem::path executable_path();
#ifdef _WIN32
// Desktop: %LOCALAPPDATA%\Kabus22 (FOLDERID_LocalAppData). UWP: the package's LocalState folder. Saves, mods/, packs/, config/
// (controller.ini, launcher.ini) and the Bonus ygm.k22 live under it, like ${XDG_DATA_HOME}/kabus22 and ${XDG_CONFIG_HOME}/kabus22.
std::filesystem::path user_data_root();
#ifdef K22_UWP
// Xbox: the CoreWindow is always 1920x1080 and the console scales it to the TV; rendering at the HDMI output mode (3840x2160
// on a 4K TV) is native resolution. HdmiDisplayInformation of the current view; false off-console or before the view exists.
struct OutputMode { int width{}, height{}; double refresh{}; };
bool output_mode(OutputMode& out);
// Asks the console for the highest refresh rate the TV offers at the current resolution, up to `maximum` Hz (Series X: 120).
void request_refresh(double maximum);
#endif
// GetUserDefaultLocaleName as UTF-8 ("tr-TR"); empty when unavailable.
std::string user_locale_name();
class Handle {
public:
    Handle()=default;explicit Handle(void* value):value_(value){}
    ~Handle(){reset();}
    Handle(Handle&& o) noexcept:value_(o.value_){o.value_=nullptr;}
    Handle& operator=(Handle&& o) noexcept{if(this!=&o){reset();value_=o.value_;o.value_=nullptr;}return *this;}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    explicit operator bool()const{return value_!=nullptr;}
    void* get()const{return value_;}
    void reset();
private:
    void* value_{};
};
// CreateFile2 of `path` itself with FILE_FLAG_OPEN_REPARSE_POINT (plus FILE_FLAG_BACKUP_SEMANTICS when `directory`); a reparse
// point (symlink, junction, mount point) is closed again and reported as ERROR_REPARSE_POINT_ENCOUNTERED, a non-directory opened
// with `directory` as ERROR_DIRECTORY. On failure the Handle is empty and GetLastError() holds the reason.
Handle open_file(const std::filesystem::path& path,std::uint32_t access,std::uint32_t share,std::uint32_t disposition,bool directory=false);
struct FileStatus { bool directory{}; std::uint32_t links{}; std::uint64_t size{}; std::int64_t written{},changed{}; };
// GetFileInformationByHandleEx FileBasicInfo + FileStandardInfo; false (GetLastError) on failure.
bool file_status(const Handle& file,FileStatus& status);
// ReadFile at absolute `offset` through OVERLAPPED; bytes read, 0 at end of file, -1 on failure.
std::int64_t read_at(const Handle& file,std::uint64_t offset,void* data,std::size_t size);
// WriteFile until every byte is written; false on failure.
bool write_all(const Handle& file,const void* data,std::size_t size);
// FlushFileBuffers, then MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH) of the closed `temporary` over `target`.
bool flush(const Handle& file);
bool replace(const std::filesystem::path& temporary,const std::filesystem::path& target);
// GetCurrentProcessId: the POSIX getpid() of the temporary-file names and Discord activity.
unsigned long process_id();
// Client end of a local named pipe (Discord IPC \\.\pipe\discord-ipc-N) as its handle value (HandleToLong, always >= 0); -1 when
// it cannot be opened. Opened for overlapped I/O: a write gives up after 100 ms without progress; reads never block.
int named_pipe_open(const std::wstring& name);
void named_pipe_close(int pipe);
// PeekNamedPipe, then ReadFile of what is pending: bytes read, 0 when nothing is pending, -1 when the pipe closed or failed.
std::int64_t named_pipe_read(int pipe,void* data,std::size_t size);
bool named_pipe_write(int pipe,const void* data,std::size_t size);
// DirectWrite system font collection: `family` ("serif" Times New Roman, "sans-serif" Arial, "monospace" Consolas), Segoe UI when
// the family is not installed (fontconfig's best match), bold/italic by weight/style. Bytes are read through the font's
// IDWriteFontFileStream, never a path (UWP may not open %WINDIR%\Fonts); `face` is the index inside a collection file.
struct FontFile { std::vector<unsigned char> bytes; int face{}; };
FontFile system_font(std::string_view family,bool bold,bool italic);
#endif
}
