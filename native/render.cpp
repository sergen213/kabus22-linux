#include "render.hpp"
#include "render_gl.hpp"
#include "render_vulkan.hpp"
#include "render_policy.hpp"
#include <SDL_vulkan.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <iconv.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include <vector>

namespace k22 {
namespace { using namespace render_policy; }
struct Renderer::Impl {
    SDL_Window* window{}; std::array<SDL_GameController*,Input::Pads> controllers{};
    std::array<SDL_Joystick*,2> joysticks{};
    bool quit{},sdlInitialized{}; std::uint64_t counter{};
    RenderOptions options; FT_Library freetype{};
    std::unordered_map<std::string,std::unique_ptr<FontAtlas>> fonts;
    std::unique_ptr<GLBackend> gl; std::unique_ptr<VulkanBackend> vk;
    explicit Impl(RenderOptions o):options(std::move(o)) {
        try {
            if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
            sdlInitialized=true;
            if(options.backend==RendererBackend::OpenGL){
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
            }
            const auto backendFlag=options.backend==RendererBackend::OpenGL?SDL_WINDOW_OPENGL:SDL_WINDOW_VULKAN;
            window=SDL_CreateWindow(options.title.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,options.width,options.height,backendFlag|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(options.hidden?SDL_WINDOW_HIDDEN:0));
            if(!window)throw std::runtime_error(SDL_GetError());
            if(options.backend==RendererBackend::OpenGL)gl=std::make_unique<GLBackend>(window,options);
            else vk=std::make_unique<VulkanBackend>(window,options);
            if(FT_Init_FreeType(&freetype))throw std::runtime_error("FreeType initialization failed");if(!FcInit())throw std::runtime_error("Fontconfig initialization failed");
            counter=SDL_GetPerformanceCounter();
            for(int device=0;device<SDL_NumJoysticks();++device)open_joystick(device);
            for(int device=0;device<SDL_NumJoysticks();++device)open_controller(device);
        }catch(...){destroy();throw;}
    }
    void open_joystick(int device){
        SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(device);
        for(auto*joy:joysticks)if(joy&&SDL_JoystickInstanceID(joy)==id)return;
        for(auto&joy:joysticks)if(!joy){joy=SDL_JoystickOpen(device);if(!joy)throw std::runtime_error(SDL_GetError());return;}
    }
    void open_controller(int device){
        if(!SDL_IsGameController(device))return;
        SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(device);
        for(auto*pad:controllers)if(pad&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==id)return;
        for(int slot=0;slot<Input::Pads;++slot)if(!controllers[slot]){
            auto*&pad=controllers[slot];pad=SDL_GameControllerOpen(device);
            if(pad)std::cerr<<"kabus22: game controller '"<<SDL_GameControllerName(pad)<<"' opened in slot "<<slot<<(SDL_GameControllerHasRumble(pad)?" (rumble)":"")<<'\n';
            return;
        }
    }
    void destroy(){
        vk.reset();gl.reset();
        for(auto&joy:joysticks)if(joy){SDL_JoystickClose(joy);joy=nullptr;}
        if(freetype){FT_Done_FreeType(freetype);freetype=nullptr;}
        for(auto&pad:controllers)if(pad){SDL_GameControllerClose(pad);pad=nullptr;}
        if(window){SDL_DestroyWindow(window);window=nullptr;}
        if(sdlInitialized){SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS);sdlInitialized=false;}
    }
    ~Impl(){destroy();}
    void drain(){if(gl)gl->drain();else vk->drain();}
    void invalidate(){if(gl)gl->invalidate(options.mode);else vk->invalidate();}
};
Renderer::Renderer(const RenderOptions&o):impl_(std::make_unique<Impl>(o)){set_mode(o.width,o.height,o.mode);}
Renderer::~Renderer()=default;
WindowMetrics Renderer::metrics()const{WindowMetrics m;SDL_GetWindowSize(impl_->window,&m.width,&m.height);if(impl_->gl)SDL_GL_GetDrawableSize(impl_->window,&m.drawableWidth,&m.drawableHeight);else SDL_Vulkan_GetDrawableSize(impl_->window,&m.drawableWidth,&m.drawableHeight);m.scaleX=m.width?float(m.drawableWidth)/m.width:1;m.scaleY=m.height?float(m.drawableHeight)/m.height:1;return m;}
void Renderer::set_mode(int w,int h,WindowMode mode){if(w<1||h<1)throw std::runtime_error("Window resolution must be positive");auto&i=*impl_;i.drain();if(SDL_SetWindowFullscreen(i.window,0))throw std::runtime_error(SDL_GetError());SDL_SetWindowBordered(i.window,mode==WindowMode::Borderless?SDL_FALSE:SDL_TRUE);SDL_SetWindowSize(i.window,w,h);if(mode==WindowMode::Fullscreen){SDL_DisplayMode display{};display.w=w;display.h=h;if(SDL_SetWindowDisplayMode(i.window,&display)||SDL_SetWindowFullscreen(i.window,SDL_WINDOW_FULLSCREEN))throw std::runtime_error(SDL_GetError());}else if(mode==WindowMode::Borderless){if(SDL_SetWindowFullscreen(i.window,SDL_WINDOW_FULLSCREEN_DESKTOP))throw std::runtime_error(SDL_GetError());}i.options.width=w;i.options.height=h;i.options.mode=mode;i.invalidate();}
void Renderer::set_title(std::string_view title){std::string s(title);SDL_SetWindowTitle(impl_->window,s.c_str());}
void Renderer::set_position(int x,int y){SDL_SetWindowPosition(impl_->window,x,y);}
void Renderer::set_mouse_mode(bool visible,bool relative){SDL_ShowCursor(visible?SDL_ENABLE:SDL_DISABLE);if(SDL_SetRelativeMouseMode(relative?SDL_TRUE:SDL_FALSE))throw std::runtime_error(SDL_GetError());}
void Renderer::rumble(int pad,float strength,std::uint32_t milliseconds){if(pad>=0&&pad<Input::Pads&&impl_->controllers[pad]){auto level=Uint16(std::clamp(strength,0.f,1.f)*65535);SDL_GameControllerRumble(impl_->controllers[pad],level,level,milliseconds);}}
void Renderer::request_quit(){impl_->quit=true;}
int Renderer::shader_version()const{return impl_->gl?impl_->gl->shader_version():impl_->vk->shader_version();}
PresentationState Renderer::presentation_state()const{return impl_->gl?impl_->gl->presentation_state():impl_->vk->presentation_state();}
void Renderer::purge_model(const Model*model){if(impl_->gl)impl_->gl->purge_model(model);else impl_->vk->purge_model(model);}
void Renderer::purge_image(const Image*image){if(impl_->gl)impl_->gl->purge_image(image);else impl_->vk->purge_image(image);}
void Renderer::release_scene(const Model* keep){if(impl_->gl)impl_->gl->release_scene(keep);else impl_->vk->release_scene(keep);}
void Renderer::draw(const RenderFrame&frame){auto m=metrics();if(impl_->gl)impl_->gl->draw(frame,m);else impl_->vk->draw(frame,m);}
void Renderer::capture(std::string_view path)const{auto m=metrics();if(impl_->gl)impl_->gl->capture(path,m);else impl_->vk->capture(path,m);}
std::string Renderer::description()const{return impl_->gl?impl_->gl->description():impl_->vk->description();}
std::pair<bool,std::string> Renderer::probe(RendererBackend backend,bool rayTracing){
    RenderOptions o;o.backend=backend;o.rayTracing=rayTracing;o.hidden=true;o.vsync=false;o.width=64;o.height=64;
    try{Impl impl(o);return {true,impl.gl?impl.gl->description():impl.vk->description()};}
    catch(const std::exception&e){return {false,e.what()};}
}
Vec3 Renderer::project(Vec3 position,const Camera&camera,WindowMetrics metrics){
    Rect viewport=camera.viewport;if(viewport.width<=0||viewport.height<=0)viewport={0,0,float(metrics.width),float(metrics.height)};
    if(viewport.width<=0||viewport.height<=0)throw std::runtime_error("Cannot project onto zero-sized game view");
    Matrix matrix=camera_matrix(camera,viewport.width/viewport.height);
    float x=matrix[0]*position.x+matrix[4]*position.y+matrix[8]*position.z+matrix[12];
    float y=matrix[1]*position.x+matrix[5]*position.y+matrix[9]*position.z+matrix[13];
    float w=matrix[3]*position.x+matrix[7]*position.y+matrix[11]*position.z+matrix[15];
    if(w==0)return {std::numeric_limits<float>::infinity(),std::numeric_limits<float>::infinity(),0};
    return {viewport.x+(x/w+1)*viewport.width*.5f,viewport.y+(1-y/w)*viewport.height*.5f,w};
}
std::optional<PortalSurface>Renderer::portal_surface(const RenderFrame&frame,const Camera&camera,WindowMetrics metrics){
    Rect viewport=camera.viewport;if(viewport.width<=0||viewport.height<=0)viewport={0,0,float(metrics.width),float(metrics.height)};
    if(viewport.width<=0||viewport.height<=0)throw std::runtime_error("Cannot select portal for zero-sized game view");
    Matrix projection=camera_matrix(camera,viewport.width/viewport.height);
    auto select=[&](const Model&model,const Matrix&matrix)->std::optional<PortalSurface>{
        if(model.formatVersion!=6)return {};
        bool hasMirror=false;for(const auto&material:model.materials)hasMirror|=(material.flags&0x2000)!=0;
        if(!hasMirror)return {};
        auto point=[&](Vec3 p){return transformed_point(matrix,p);};
        for(const auto&triangle:model.triangles){
            if(triangle.materialIndex>=model.materials.size()||!(model.materials[triangle.materialIndex].flags&0x2000))continue;
            std::array<Vec3,3>positions;for(int index=0;index<3;++index){if(triangle.indices[index]>=model.vertices.size())throw std::runtime_error("Portal triangle references invalid game vertex");positions[index]=point(model.vertices[triangle.indices[index]].position);}
            Vec3 local=model.vertices[triangle.indices[0]].normal,normal{};
            for(int col=0;col<3;++col){float squared=matrix[col*4]*matrix[col*4]+matrix[col*4+1]*matrix[col*4+1]+matrix[col*4+2]*matrix[col*4+2];float component=col==0?local.x:col==1?local.y:local.z;if(squared>0){normal.x+=matrix[col*4]*component/squared;normal.y+=matrix[col*4+1]*component/squared;normal.z+=matrix[col*4+2]*component/squared;}}
            normal=unit(normal);if(!camera.noCull&&dot(normal,add(camera.position,mul(positions[0],-1)))<=0)continue;
            bool outside=false;
            std::array<std::array<float,4>,3>clip{};
            for(int vertex=0;vertex<3;++vertex)for(int row=0;row<4;++row)clip[vertex][row]=projection[row]*positions[vertex].x+projection[4+row]*positions[vertex].y+projection[8+row]*positions[vertex].z+projection[12+row];
            for(int plane=0;plane<6;++plane){int axis=plane/2;bool all=true;for(const auto&p:clip)all&=(plane%2?p[axis]>p[3]:p[axis]<-p[3]);outside|=all;}
            if(!outside)return PortalSurface{positions[0],normal};
        }
        return {};
    };
    if(frame.world&&camera.drawWorld&&(camera.layers&1))if(auto surface=select(frame.world->mesh,transform({}, {}, {1,1,1})))return surface;
    for(const auto&instance:frame.instances){
        if(!instance.model||!instance.visible||instance.viewSpace||instance.alpha<=0||!(instance.layers&camera.layers)||(camera.excludeEntityId&&instance.objectId==camera.excludeEntityId))continue;
        if(auto surface=select(*instance.model,transform(instance.position,instance.angles,instance.scale)))return surface;
    }
    return {};
}
void Renderer::poll(Input&in){
    auto&i=*impl_;
    in.pressed.reset();in.released.reset();in.mousePressed.reset();in.mouseReleased.reset();
    for(auto&buttons:in.controllerPressed)buttons.reset();
    for(auto&buttons:in.controllerReleased)buttons.reset();
    for(auto&buttons:in.joystickPressed)buttons.reset();
    for(auto&buttons:in.joystickReleased)buttons.reset();
    in.mouseDX=in.mouseDY=in.wheelX=in.wheelY=0;in.text.clear();
    std::uint64_t now=SDL_GetPerformanceCounter();in.dt=double(now-i.counter)/SDL_GetPerformanceFrequency();i.counter=now;
    auto joystick_slot=[&](SDL_JoystickID id)->int{
        // Legacy joystick slots use SDL instance IDs, never transient device indices.
        for(int index=0;index<2;++index)if(i.joysticks[index]&&SDL_JoystickInstanceID(i.joysticks[index])==id)return index;
        return -1;
    };
    SDL_Event event;
    bool devicesChanged=false;
    auto controller_slot=[&](SDL_JoystickID id)->int{for(int slot=0;slot<Input::Pads;++slot)if(i.controllers[slot]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(i.controllers[slot]))==id)return slot;return -1;};
    while(SDL_PollEvent(&event)){
        switch(event.type){
        case SDL_QUIT:i.quit=true;break;
        case SDL_WINDOWEVENT:
            if(event.window.event==SDL_WINDOWEVENT_CLOSE)i.quit=true;
            if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){in.released|=in.keys;in.keys.reset();in.mouseReleased|=in.mouseButtons;in.mouseButtons.reset();}
            break;
        case SDL_KEYDOWN:case SDL_KEYUP:{
            int key=event.key.keysym.scancode;
            if(key>=0&&key<SDL_NUM_SCANCODES){bool down=event.type==SDL_KEYDOWN;in.keys.set(key,down);if(!event.key.repeat)(down?in.pressed:in.released).set(key);}
            break;
        }
        case SDL_MOUSEMOTION:
            in.mouseX=event.motion.x;in.mouseY=event.motion.y;in.mouseDX+=event.motion.xrel;in.mouseDY+=event.motion.yrel;break;
        case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:
            if(event.button.button<8){bool down=event.type==SDL_MOUSEBUTTONDOWN;in.mouseButtons.set(event.button.button,down);(down?in.mousePressed:in.mouseReleased).set(event.button.button);in.mouseX=event.button.x;in.mouseY=event.button.y;}
            break;
        case SDL_MOUSEWHEEL:{
            float direction=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1;
            in.wheelX+=event.wheel.preciseX*direction;in.wheelY+=event.wheel.preciseY*direction;break;
        }
        case SDL_TEXTINPUT:in.text+=event.text.text;break;
        case SDL_JOYDEVICEADDED:i.open_joystick(event.jdevice.which);devicesChanged=true;break;
        case SDL_JOYDEVICEREMOVED:{
            int index=joystick_slot(event.jdevice.which);
            if(index>=0){SDL_JoystickClose(i.joysticks[index]);i.joysticks[index]=nullptr;in.joystickReleased[index]|=in.joystickButtons[index];in.joystickButtons[index].reset();in.joystickAxes[index].fill(0);}
            devicesChanged=true;
            break;
        }
        case SDL_JOYBUTTONDOWN:case SDL_JOYBUTTONUP:{
            int index=joystick_slot(event.jbutton.which);
            if(index>=0&&event.jbutton.button<32){bool down=event.type==SDL_JOYBUTTONDOWN;in.joystickButtons[index].set(event.jbutton.button,down);(down?in.joystickPressed[index]:in.joystickReleased[index]).set(event.jbutton.button);}
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            i.open_controller(event.cdevice.which);break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if(int p=controller_slot(event.cdevice.which);p>=0){std::cerr<<"kabus22: game controller removed from slot "<<p<<'\n';SDL_GameControllerClose(i.controllers[p]);i.controllers[p]=nullptr;in.controllerReleased[p]|=in.controllerButtons[p];in.controllerButtons[p].reset();in.controllerAxes[p].fill(0);}
            break;
        case SDL_CONTROLLERBUTTONDOWN:case SDL_CONTROLLERBUTTONUP:
            if(int p=controller_slot(event.cbutton.which);p>=0&&event.cbutton.button<SDL_CONTROLLER_BUTTON_MAX){bool down=event.type==SDL_CONTROLLERBUTTONDOWN;in.controllerButtons[p].set(event.cbutton.button,down);(down?in.controllerPressed[p]:in.controllerReleased[p]).set(event.cbutton.button);}break;
        case SDL_CONTROLLERAXISMOTION:
            if(int p=controller_slot(event.caxis.which);p>=0&&event.caxis.axis<SDL_CONTROLLER_AXIS_MAX)in.controllerAxes[p][event.caxis.axis]=event.caxis.value/(event.caxis.value<0?32768.0f:32767.0f);break;
        default:break;
        }
    }
    if(devicesChanged)for(int device=0;device<SDL_NumJoysticks();++device){i.open_joystick(device);i.open_controller(device);}
    for(int p=0;p<Input::Pads;++p){
        auto*pad=i.controllers[p];in.controllerConnected[p]=pad!=nullptr;
        if(!pad)continue;
        for(int axis=0;axis<SDL_CONTROLLER_AXIS_MAX;++axis){int value=SDL_GameControllerGetAxis(pad,static_cast<SDL_GameControllerAxis>(axis));in.controllerAxes[p][axis]=value/(value<0?32768.0f:32767.0f);}
        for(int button=0;button<SDL_CONTROLLER_BUTTON_MAX;++button){bool down=SDL_GameControllerGetButton(pad,static_cast<SDL_GameControllerButton>(button));if(down!=in.controllerButtons[p][button]){(down?in.controllerPressed[p]:in.controllerReleased[p]).set(button);in.controllerButtons[p].set(button,down);}}
    }
    for(int index=0;index<2;++index){
        auto*joy=i.joysticks[index];in.joystickConnected[index]=joy!=nullptr;in.joystickPad[index]=-1;
        if(!joy)continue;
        for(int p=0;p<Input::Pads;++p)if(i.controllers[p]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(i.controllers[p]))==SDL_JoystickInstanceID(joy))in.joystickPad[index]=p;
        for(int axis=0;axis<6;++axis){int value=axis<SDL_JoystickNumAxes(joy)?SDL_JoystickGetAxis(joy,axis):0;in.joystickAxes[index][axis]=value/(value<0?32768.0f:32767.0f);}
        for(int button=0;button<32;++button){bool down=button<SDL_JoystickNumButtons(joy)&&SDL_JoystickGetButton(joy,button);if(down!=in.joystickButtons[index][button]){(down?in.joystickPressed[index]:in.joystickReleased[index]).set(button);in.joystickButtons[index].set(button,down);}}
    }
    int x{},y{};SDL_GetMouseState(&x,&y);if(!SDL_GetRelativeMouseMode()){in.mouseX=x;in.mouseY=y;}in.quit=i.quit;
}
Vec2 Renderer::text_extent(const FontAtlas&font,std::string_view text){float x=0,width=0;int lines=1;for(unsigned char ch:text){if(ch=='\n'){width=std::max(width,x);x=0;++lines;}else x+=font.glyphs[ch].advance;}return {std::max(width,x),lines*font.lineHeight};}
const FontAtlas&Renderer::font_atlas(std::string_view family,int pixels,bool bold,bool italic){
    if(pixels<1||pixels>512)throw std::runtime_error("Invalid native font pixel size");
    std::string key=std::string(family)+"|"+std::to_string(pixels)+"|"+std::to_string(bold)+std::to_string(italic);
    auto&i=*impl_;auto found=i.fonts.find(key);if(found!=i.fonts.end())return *found->second;
    std::string path(family);
    if(path.find('/')==std::string::npos){
        FcPattern*pattern=FcPatternCreate();if(!pattern)throw std::bad_alloc();
        FcPatternAddString(pattern,FC_FAMILY,reinterpret_cast<const FcChar8*>(path.c_str()));FcPatternAddInteger(pattern,FC_WEIGHT,bold?FC_WEIGHT_BOLD:FC_WEIGHT_REGULAR);FcPatternAddInteger(pattern,FC_SLANT,italic?FC_SLANT_ITALIC:FC_SLANT_ROMAN);
        FcConfigSubstitute(nullptr,pattern,FcMatchPattern);FcDefaultSubstitute(pattern);
        FcResult result;FcPattern*match=FcFontMatch(nullptr,pattern,&result);FcPatternDestroy(pattern);
        if(!match)throw std::runtime_error("No native font matching "+path);
        FcChar8*file{};
        if(FcPatternGetString(match,FC_FILE,0,&file)!=FcResultMatch){FcPatternDestroy(match);throw std::runtime_error("Native font has no file");}
        path=reinterpret_cast<const char*>(file);FcPatternDestroy(match);
    }
    FT_Face face{};if(FT_New_Face(i.freetype,path.c_str(),0,&face))throw std::runtime_error("Cannot load native font "+path);
    struct FaceGuard{FT_Face face;~FaceGuard(){FT_Done_Face(face);}}guard{face};
    FT_Size_RequestRec size{FT_SIZE_REQUEST_TYPE_REAL_DIM,0,pixels*64,0,0};
    if(FT_Request_Size(face,&size))throw std::runtime_error("Cannot size native font");
    auto font=std::make_unique<FontAtlas>();font->lineHeight=pixels;font->ascent=face->size->metrics.ascender/64.0f;
    std::array<std::vector<std::uint8_t>,256>bitmaps;int rowX=1,rowY=1,rowHeight=0;const int atlasWidth=1024;
    iconv_t converter=iconv_open("UTF-32LE","WINDOWS-1254");if(converter==(iconv_t)-1)throw std::runtime_error("Native Turkish code page conversion unavailable");
    for(int ch=0;ch<256;++ch){std::uint32_t cp=ch;char byte=char(ch),output[4]{};char*input=&byte,*out=output;size_t inputLength=1,outputLength=4;if(iconv(converter,&input,&inputLength,&out,&outputLength)!=size_t(-1)&&outputLength==0)std::memcpy(&cp,output,4);if(FT_Load_Char(face,cp,FT_LOAD_RENDER)){iconv_close(converter);throw std::runtime_error("Native font glyph rasterization failed");}auto&slot=*face->glyph;auto&g=font->glyphs[ch];int w=slot.bitmap.width,h=slot.bitmap.rows;if(rowX+w+1>atlasWidth){rowX=1;rowY+=rowHeight+1;rowHeight=0;}g.pixels={float(rowX),float(rowY),float(w),float(h)};g.bearing={float(slot.bitmap_left),float(slot.bitmap_top)};g.advance=slot.advance.x/64.0f;auto&bitmap=bitmaps[ch];bitmap.resize(w*h);for(int y=0;y<h;++y){const auto*src=slot.bitmap.buffer+(slot.bitmap.pitch>=0?y:h-1-y)*std::abs(slot.bitmap.pitch);if(slot.bitmap.pixel_mode==FT_PIXEL_MODE_GRAY)std::memcpy(bitmap.data()+y*w,src,w);else if(slot.bitmap.pixel_mode==FT_PIXEL_MODE_MONO)for(int x=0;x<w;++x)bitmap[y*w+x]=(src[x/8]&(0x80>>(x%8)))?255:0;else{iconv_close(converter);throw std::runtime_error("Unsupported native font bitmap format");}}rowX+=w+1;rowHeight=std::max(rowHeight,h);}
    iconv_close(converter);font->image.width=atlasWidth;font->image.height=rowY+rowHeight+1;font->image.rgba.resize(std::size_t(font->image.width)*font->image.height*4,0);for(int ch=0;ch<256;++ch){const auto&g=font->glyphs[ch];int w=g.pixels.width,h=g.pixels.height;for(int y=0;y<h;++y)for(int x=0;x<w;++x){size_t p=((size_t(g.pixels.y)+y)*atlasWidth+size_t(g.pixels.x)+x)*4;font->image.rgba[p]=font->image.rgba[p+1]=font->image.rgba[p+2]=255;font->image.rgba[p+3]=bitmaps[ch][y*w+x];}}return *i.fonts.emplace(std::move(key),std::move(font)).first->second;
}
}

extern "C" {
#include <libavutil/sha.h>
#include <libavutil/mem.h>
}

namespace k22 {
EffectKind Renderer::identify_effect(std::string_view name,std::span<const std::byte>source){
    auto matches=[&](std::string_view expected){
        if(name.size()!=expected.size())return false;
        for(std::size_t index=0;index<name.size();++index){
            unsigned char character=name[index];
            if(character>='A'&&character<='Z')character+='a'-'A';
            if(character!=expected[index])return false;
        }
        return true;
    };
    constexpr std::array<std::uint8_t,32>mixHash{
        0xd3,0xfc,0xbb,0x34,0x9f,0x1e,0x65,0x25,0xb8,0x0b,0x9c,0x60,0x4c,0x06,0x51,0xd2,
        0xea,0x47,0xb6,0xb6,0xac,0xd4,0x8a,0x17,0x38,0x31,0xab,0x62,0x0e,0x0f,0xb4,0x99};
    constexpr std::array<std::uint8_t,32>sunMixHash{
        0xcd,0xff,0xda,0x3a,0x5c,0x12,0xa4,0x82,0x14,0x26,0x0b,0xd4,0xbc,0xf9,0xe8,0x56,
        0xad,0x01,0xc5,0xe6,0xa2,0xae,0x3c,0x23,0x1d,0x04,0x00,0xa3,0x2c,0x18,0x81,0xb8};
    EffectKind kind;
    if(matches("normal_mix.fx"))kind=EffectKind::NormalMix;
    else if(matches("normal_sunmix.fx"))kind=EffectKind::NormalSunMix;
    else throw std::runtime_error("Unsupported native material effect resource: "+std::string(name));
    // AVSHA is opaque; retain one library-allocated context per calling thread.
    thread_local std::unique_ptr<AVSHA,decltype(&av_free)>context(av_sha_alloc(),av_free);
    if(!context)throw std::bad_alloc();
    if(av_sha_init(context.get(),256)!=0)throw std::runtime_error("Cannot initialize material effect SHA256");
    if(!source.empty())av_sha_update(context.get(),reinterpret_cast<const std::uint8_t*>(source.data()),source.size());
    std::array<std::uint8_t,32>digest{};
    av_sha_final(context.get(),digest.data());
    if(digest!=(kind==EffectKind::NormalMix?mixHash:sunMixHash))
        throw std::runtime_error("Native material effect source does not match recovered SHA256: "+std::string(name));
    return kind;
}

}
