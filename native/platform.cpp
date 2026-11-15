#include "platform.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <algorithm>
#include <cstring>
#include <limits>
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#ifdef K22_UWP
#include <SDL.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <cstdio>
#else
#include <knownfolders.h>
#include <shlobj.h>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#endif
#else
#include <unistd.h>
#endif

namespace k22::platform {
#ifndef _WIN32
std::filesystem::path executable_path(){
    char path[4096];auto n=::readlink("/proc/self/exe",path,sizeof path-1);if(n<=0)return {};
    return std::string(path,std::size_t(n));
}
#else
namespace {
std::wstring widen(std::string_view text){
    if(text.empty())return {};
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(size<=0)return {};
    std::wstring out(std::size_t(size),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),size);return out;
}
}
std::filesystem::path executable_path(){
    std::wstring path(MAX_PATH,L'\0');
    for(;;){
        DWORD n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));if(n==0)return {};
        if(n<path.size()){path.resize(n);return path;}
        if(path.size()>=32768)return {};path.resize(path.size()*2);
    }
}
std::filesystem::path user_data_root(){
#ifdef K22_UWP
    const wchar_t* local=SDL_WinRTGetFSPathUNICODE(SDL_WINRT_PATH_LOCAL_FOLDER);
    if(!local||!*local)throw std::runtime_error(std::string("app LocalState folder unavailable: ")+SDL_GetError());
    return std::filesystem::path(local);
#else
    PWSTR local=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local))){CoTaskMemFree(local);throw std::runtime_error("%LOCALAPPDATA% (FOLDERID_LocalAppData) unavailable");}
    std::filesystem::path root(local);CoTaskMemFree(local);return root/L"Kabus22";
#endif
}
std::string user_locale_name(){
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};int n=GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH);if(n<=1)return {};
    std::string out;for(int i=0;i<n-1;++i)out+=name[i]<0x80?char(name[i]):'?';return out;
}
#ifdef K22_UWP
namespace {
winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation hdmi(){
    try{return winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();}catch(const winrt::hresult_error&){return nullptr;}
}
}
bool output_mode(OutputMode& out){
    static OutputMode cached{};
    if(!cached.width){
        auto info=hdmi();if(!info)return false;
        try{auto m=info.GetCurrentDisplayMode();if(!m)return false;cached={int(m.ResolutionWidthInRawPixels()),int(m.ResolutionHeightInRawPixels()),m.RefreshRate()};}
        catch(const winrt::hresult_error&){return false;}
        if(cached.width<=0||cached.height<=0){cached={};return false;}
    }
    out=cached;return true;
}
void request_refresh(double maximum){
    auto info=hdmi();if(!info)return;
    try{
        const auto current=info.GetCurrentDisplayMode();if(!current)return;
        winrt::Windows::Graphics::Display::Core::HdmiDisplayMode best{nullptr};
        for(const auto& m:info.GetSupportedDisplayModes()){
            if(m.ResolutionWidthInRawPixels()!=current.ResolutionWidthInRawPixels()||m.ResolutionHeightInRawPixels()!=current.ResolutionHeightInRawPixels()||
               m.ColorSpace()!=current.ColorSpace()||m.BitsPerPixel()!=current.BitsPerPixel()||m.StereoEnabled()||m.RefreshRate()>maximum+0.5)continue;
            if(!best||m.RefreshRate()>best.RefreshRate())best=m;
        }
        if(!best||best.RefreshRate()<=current.RefreshRate()+0.5){std::fprintf(stderr,"kabus22: display %ux%u at %.2f Hz (highest the TV offers up to %.0f Hz)\n",current.ResolutionWidthInRawPixels(),current.ResolutionHeightInRawPixels(),current.RefreshRate(),maximum);return;}
        std::fprintf(stderr,"kabus22: display %ux%u: switching %.2f Hz -> %.2f Hz\n",best.ResolutionWidthInRawPixels(),best.ResolutionHeightInRawPixels(),current.RefreshRate(),best.RefreshRate());
        // Asynchronous: blocking on the view thread is not allowed; the swap chain keeps its size, only the refresh changes.
        info.RequestSetCurrentDisplayModeAsync(best).Completed([](const auto& operation,auto){try{if(!operation.GetResults())std::fprintf(stderr,"kabus22: display mode change refused\n");}catch(const winrt::hresult_error& e){std::fprintf(stderr,"kabus22: display mode change failed: %ls\n",e.message().c_str());}});
    }catch(const winrt::hresult_error& e){std::fprintf(stderr,"kabus22: display modes unavailable: %ls\n",e.message().c_str());}
}
#endif
void Handle::reset(){if(value_){CloseHandle(value_);value_=nullptr;}}
Handle open_file(const std::filesystem::path& path,std::uint32_t access,std::uint32_t share,std::uint32_t disposition,bool directory){
    CREATEFILE2_EXTENDED_PARAMETERS parameters{};parameters.dwSize=sizeof parameters;parameters.dwFileAttributes=FILE_ATTRIBUTE_NORMAL;
    parameters.dwFileFlags=FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0);
    HANDLE raw=CreateFile2(path.c_str(),access,share,disposition,&parameters);if(raw==INVALID_HANDLE_VALUE)return {};
    Handle file(raw);FILE_ATTRIBUTE_TAG_INFO tag{};
    auto fail=[&](DWORD error){file.reset();SetLastError(error);return Handle{};};
    if(!GetFileInformationByHandleEx(raw,FileAttributeTagInfo,&tag,sizeof tag))return fail(GetLastError());
    if(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)return fail(ERROR_REPARSE_POINT_ENCOUNTERED);
    if(directory&&!(tag.FileAttributes&FILE_ATTRIBUTE_DIRECTORY))return fail(ERROR_DIRECTORY);
    return file;
}
bool file_status(const Handle& file,FileStatus& status){
    FILE_BASIC_INFO basic{};FILE_STANDARD_INFO standard{};
    if(!GetFileInformationByHandleEx(file.get(),FileBasicInfo,&basic,sizeof basic)||!GetFileInformationByHandleEx(file.get(),FileStandardInfo,&standard,sizeof standard))return false;
    status.directory=standard.Directory||(basic.FileAttributes&FILE_ATTRIBUTE_DIRECTORY);status.links=standard.NumberOfLinks;
    status.size=std::uint64_t(standard.EndOfFile.QuadPart);status.written=basic.LastWriteTime.QuadPart;status.changed=basic.ChangeTime.QuadPart;
    return standard.EndOfFile.QuadPart>=0;
}
std::int64_t read_at(const Handle& file,std::uint64_t offset,void* data,std::size_t size){
    OVERLAPPED at{};at.Offset=DWORD(offset);at.OffsetHigh=DWORD(offset>>32);DWORD n=0;
    if(!ReadFile(file.get(),data,DWORD(std::min<std::size_t>(size,std::numeric_limits<DWORD>::max())),&n,&at))return GetLastError()==ERROR_HANDLE_EOF?0:-1;
    return n;
}
bool write_all(const Handle& file,const void* data,std::size_t size){
    auto* p=static_cast<const unsigned char*>(data);
    while(size){DWORD n=0;if(!WriteFile(file.get(),p,DWORD(std::min<std::size_t>(size,1u<<30)),&n,nullptr)||n==0)return false;p+=n;size-=n;}
    return true;
}
bool flush(const Handle& file){return FlushFileBuffers(file.get())!=0;}
bool replace(const std::filesystem::path& temporary,const std::filesystem::path& target){
    return MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
unsigned long process_id(){return GetCurrentProcessId();}
int named_pipe_open(const std::wstring& name){
    CREATEFILE2_EXTENDED_PARAMETERS parameters{};parameters.dwSize=sizeof parameters;parameters.dwSecurityQosFlags=SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION;
    parameters.dwFileFlags=FILE_FLAG_OVERLAPPED; // bounded writes (named_pipe_write)
    HANDLE pipe=CreateFile2(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,OPEN_EXISTING,&parameters);if(pipe==INVALID_HANDLE_VALUE)return -1;
    return int(HandleToLong(pipe));
}
void named_pipe_close(int pipe){if(pipe>=0)CloseHandle(LongToHandle(pipe));}
std::int64_t named_pipe_read(int pipe,void* data,std::size_t size){
    HANDLE h=LongToHandle(pipe);DWORD pending=0;if(!PeekNamedPipe(h,nullptr,0,nullptr,&pending,nullptr))return -1;if(pending==0)return 0;
    Handle event(CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_ALL_ACCESS));if(!event)return -1;
    OVERLAPPED io{};io.hEvent=event.get();DWORD n=0; // the pending bytes are already in the pipe: completes without blocking
    if(!ReadFile(h,data,DWORD(std::min(size,std::size_t(pending))),&n,&io)&&(GetLastError()!=ERROR_IO_PENDING||!GetOverlappedResult(h,&io,&n,TRUE)))return -1;
    return n==0?-1:std::int64_t(n);
}
// Like the POSIX path's non-blocking send with a 100 ms poll: a write that makes no progress for 100 ms is cancelled and fails,
// so a stalled Discord client never stalls the game thread.
bool named_pipe_write(int pipe,const void* data,std::size_t size){
    HANDLE h=LongToHandle(pipe);Handle event(CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_ALL_ACCESS));if(!event)return false;
    auto* p=static_cast<const unsigned char*>(data);
    while(size){
        OVERLAPPED io{};io.hEvent=event.get();DWORD n=0;
        if(!WriteFile(h,p,DWORD(std::min<std::size_t>(size,1u<<20)),&n,&io)){
            if(GetLastError()!=ERROR_IO_PENDING)return false;
            if(WaitForSingleObject(io.hEvent,100)!=WAIT_OBJECT_0){CancelIoEx(h,&io);GetOverlappedResult(h,&io,&n,TRUE);return false;}
            if(!GetOverlappedResult(h,&io,&n,FALSE))return false;
        }
        if(n==0)return false;p+=n;size-=n;
    }
    return true;
}
FontFile system_font(std::string_view family,bool bold,bool italic){
    using Microsoft::WRL::ComPtr;
    auto check=[&](HRESULT result,const char* step){if(FAILED(result))throw std::runtime_error("DirectWrite "+std::string(step)+" failed for native font "+std::string(family));};
    ComPtr<IDWriteFactory> factory;check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf())),"factory creation");
    ComPtr<IDWriteFontCollection> collection;check(factory->GetSystemFontCollection(&collection,FALSE),"system font collection");
    const std::wstring name=family=="serif"?L"Times New Roman":family=="sans-serif"?L"Arial":family=="monospace"?L"Consolas":widen(family);
    UINT32 index=0;BOOL exists=FALSE;
    if(!name.empty())check(collection->FindFamilyName(name.c_str(),&index,&exists),"family lookup");
    if(!exists)check(collection->FindFamilyName(L"Segoe UI",&index,&exists),"Segoe UI lookup");
    if(!exists)throw std::runtime_error("No native font matching "+std::string(family));
    ComPtr<IDWriteFontFamily> fonts;check(collection->GetFontFamily(index,&fonts),"family access");
    ComPtr<IDWriteFont> font;check(fonts->GetFirstMatchingFont(bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_REGULAR,DWRITE_FONT_STRETCH_NORMAL,italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,&font),"style match");
    ComPtr<IDWriteFontFace> face;check(font->CreateFontFace(&face),"face creation");
    UINT32 files=0;check(face->GetFiles(&files,nullptr),"file count");if(files==0)throw std::runtime_error("Native font has no file: "+std::string(family));
    std::vector<IDWriteFontFile*> list(files);check(face->GetFiles(&files,list.data()),"file access");
    ComPtr<IDWriteFontFile> file;file.Attach(list[0]);for(UINT32 i=1;i<files;++i)list[i]->Release();
    const void* key=nullptr;UINT32 keySize=0;check(file->GetReferenceKey(&key,&keySize),"reference key");
    ComPtr<IDWriteFontFileLoader> loader;check(file->GetLoader(&loader),"loader access");
    ComPtr<IDWriteFontFileStream> stream;check(loader->CreateStreamFromKey(key,keySize,&stream),"stream creation");
    UINT64 size=0;check(stream->GetFileSize(&size),"size query");
    if(size==0||size>256ull*1024*1024)throw std::runtime_error("Native font file size out of bounds: "+std::string(family));
    FontFile result;result.bytes.resize(std::size_t(size));
    const void* fragment=nullptr;void* context=nullptr;check(stream->ReadFileFragment(&fragment,0,size,&context),"read");
    std::memcpy(result.bytes.data(),fragment,std::size_t(size));stream->ReleaseFileFragment(context);
    result.face=int(face->GetIndex());return result;
}
#endif
}
