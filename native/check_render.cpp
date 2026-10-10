#include "render.hpp"
#include "check_texture_pack.hpp"
#include "graphics_cli.hpp"
#include "render_policy.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
template<class F> void rejects(F operation,const char* message="Unsupported or modified effect was accepted") {
    bool rejected=false;
    try { operation(); } catch(const std::runtime_error&) { rejected=true; }
    if(!rejected)throw std::runtime_error(message);
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        auto pattern=(std::filesystem::temp_directory_path()/"k22-render-XXXXXX").string();
        const auto* directory=::mkdtemp(pattern.data());
        if(!directory)throw std::system_error(errno,std::generic_category(),"mkdtemp");
        path=directory;
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path,error);
    }
};

std::vector<std::uint8_t> captured_pixels(k22::Renderer&renderer,const k22::RenderFrame&frame,const std::filesystem::path&path) {
    renderer.draw(frame);renderer.capture(path.string());
    std::ifstream file(path,std::ios::binary);std::string magic;int width{},height{},maximum{};
    file>>magic>>width>>height>>maximum;
    const auto metrics=renderer.metrics();
    if(!file||magic!="P6"||maximum!=255||width!=metrics.drawableWidth||height!=metrics.drawableHeight||file.get()!='\n')
        throw std::runtime_error("Consumer capture is not current completed output");
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*3);
    file.read(reinterpret_cast<char*>(pixels.data()),std::streamsize(pixels.size()));
    if(!file)throw std::runtime_error("Consumer capture truncated");
    return pixels;
}

std::array<int,3> capture_pixel(std::span<const std::uint8_t>pixels,k22::WindowMetrics metrics,float x=.5f,float y=.5f) {
    const auto offset=(std::size_t(int(metrics.drawableHeight*y))*metrics.drawableWidth+int(metrics.drawableWidth*x))*3;
    return {pixels[offset],pixels[offset+1],pixels[offset+2]};
}

void turbulence_checks(k22::RendererBackend backend) {
    using namespace k22;
    TempDirectory captures;
    auto image=std::make_shared<Image>();image->width=image->height=16;image->sourceDepth=16;image->nativeStorageBytes=2;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
        image->rgba.insert(image->rgba.end(),{std::uint8_t(x*17),std::uint8_t(y*17),std::uint8_t((x^y)*17),255});
    Model surface;surface.formatVersion=6;
    surface.vertices={{{10,-2,-2},{-1,0,0},{0,0},{}},{{10,2,-2},{-1,0,0},{1,0},{}},
                      {{10,2,2},{-1,0,0},{1,1},{}},{{10,-2,2},{-1,0,0},{0,1},{}}};
    surface.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};surface.materials.resize(1);
    surface.materials[0].texture=image;surface.materials[0].flags=9;surface.materials[0].raw.resize(64);
    DrawInstance actor;actor.model=&surface;actor.objectId=31;actor.layers=1;actor.unlit=actor.noFog=actor.doubleSided=true;
    Camera camera;camera.widescreen=false;camera.perDrawOpacity=true;camera.noShadow=camera.noCull=true;
    UICommand ui;ui.kind=UICommand::Kind::Image;ui.image=image.get();ui.rect={0,0,64,64};ui.uv={0,0,1,1};ui.color={1,1,1,1};ui.filter=false;
    RenderFrame frame;frame.cameras=std::span(&camera,1);frame.ui=std::span(&ui,1);
    frame.mipmapping=0;frame.turbulenceRange=6;frame.turbulenceSpeed=1;frame.animationTimeSeconds=.375;
    RenderOptions options;options.backend=backend;options.rayTracing=false;options.width=options.height=64;options.samples=1;options.vsync=false;
    options.title="Kabus 22 shared native turbulence consumer";
    Renderer renderer(options);
    auto sample=[&]{return captured_pixels(renderer,frame,captures.path/"turbulence.ppm");};
    const auto original=sample();
    frame.instances=std::span(&actor,1);
    auto unchanged=[&](const char*label){
        renderer.release_scene();
        if(sample()!=original)throw std::runtime_error(std::string(label)+": excluded actor mutated shared UI bitmap");
    };
    camera.layers=2;unchanged("Off-layer turbulence");camera.layers=~0u;
    camera.excludeEntityId=actor.objectId;unchanged("Excluded-entity turbulence");camera.excludeEntityId=0;
    camera.noEntities=true;unchanged("NOENTITIES turbulence");camera.noEntities=false;
    frame.renderEntities=false;unchanged("Suppressed entity turbulence");frame.renderEntities=true;
    actor.position.x=-30;unchanged("Off-frustum turbulence");actor.position={};
    actor.visible=false;unchanged("Invisible turbulence");actor.visible=true;
    actor.alpha=0;unchanged("Zero-opacity turbulence");actor.alpha=1;
    renderer.release_scene();
    const auto warped=sample();
    if(warped==original)throw std::runtime_error("Admitted native turbulence never changed actual shared-image output");
    frame.instances={};
    if(sample()!=warped)throw std::runtime_error("Shared bitmap lost admitted native turbulence between frames");
    renderer.release_scene();
    camera.foreground=true;camera.layer=1;camera.clearColor=false;
    std::array<UICommand,2> ordered{ui,ui};
    ordered[0].rect.width=32;ordered[0].layer=0;
    ordered[1].rect={32,0,32,64};ordered[1].layer=2;
    frame.ui=ordered;frame.instances=std::span(&actor,1);
    const auto pixels=sample();const auto metrics=renderer.metrics();
    if(capture_pixel(pixels,metrics,.125f,.125f)!=capture_pixel(original,metrics,.25f,.125f)||
       capture_pixel(pixels,metrics,.625f,.125f)!=capture_pixel(warped,metrics,.25f,.125f))
        throw std::runtime_error("Native turbulence mutation did not occur between actual earlier/later UI layers");
    frame.instances={};frame.ui=std::span(&ui,1);camera.foreground=false;camera.clearColor=true;frame.mipmapping=3;
    if(sample()!=warped)throw std::runtime_error("Lazy mip generation changed retained turbulence UI pixels or target state");
}

// A6 OVERLAY keys black/low-alpha texels unless the bitmap is BMAP0x100 (32-bit TGA, 0040e2b0/0043bb10/00440e90).
void overlay_key_checks(k22::RendererBackend backend,bool rayTracing) {
    using namespace k22;
    TempDirectory captures;
    auto flat=[](std::array<std::uint8_t,4> rgba){auto image=std::make_shared<Image>();image->width=image->height=8;for(int i=0;i<64;++i)image->rgba.insert(image->rgba.end(),rgba.begin(),rgba.end());return image;};
    auto plane=[](Model&model,float x,std::shared_ptr<const Image> image){
        model.vertices={{{x,-4,-4},{-1,0,0},{0,0},{}},{{x,4,-4},{-1,0,0},{1,0},{}},{{x,4,4},{-1,0,0},{1,1},{}},{{x,-4,4},{-1,0,0},{0,1},{}}};
        model.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};model.materials.resize(1);model.materials[0].texture=std::move(image);
    };
    auto tga32=flat({0,0,0,255}),tga24=flat({0,0,0,255});tga32->sourceDepth=32;tga32->a6AlphaFormat=true;tga24->sourceDepth=24;
    Model rear,front32,front24;plane(rear,20,flat({0,0,255,255}));plane(front32,10,tga32);plane(front24,10,tga24);
    std::array<DrawInstance,2> actors{};actors[0].model=&rear;actors[0].objectId=41;actors[1].objectId=42;
    for(auto&actor:actors)actor.unlit=actor.noFog=actor.doubleSided=true;
    Camera camera;camera.widescreen=false;camera.noShadow=camera.noCull=true;
    RenderFrame frame;frame.cameras=std::span(&camera,1);frame.instances=actors;frame.mipmapping=0;
    RenderOptions options;options.backend=backend;options.rayTracing=rayTracing;options.width=options.height=64;options.samples=1;options.vsync=false;
    options.title="Kabus 22 A6 overlay key consumer";
    Renderer renderer(options);
    auto expect=[&](const char*label,Model&model,bool overlay,std::array<int,3> expected){
        actors[1].model=&model;actors[1].overlay=overlay;
        const auto pixel=capture_pixel(captured_pixels(renderer,frame,captures.path/"overlay.ppm"),renderer.metrics());
        for(unsigned i=0;i<3;++i)if(std::abs(pixel[i]-expected[i])>4)
            throw std::runtime_error(std::string(label)+": RGB"+std::to_string(pixel[0])+","+std::to_string(pixel[1])+","+std::to_string(pixel[2]));
    };
    expect("OVERLAY keys opaque black of a non-0x100 (24-bit TGA/PCX) bitmap",front24,true,{0,0,255});
    expect("OVERLAY keeps opaque black of a BMAP0x100 32-bit TGA",front32,true,{0,0,0});
    expect("Unflagged opaque black stays drawn",front24,false,{0,0,0});
}

void gl_checks(std::span<const std::byte> mix,std::span<const std::byte> sunMix,k22::RendererBackend backend=k22::RendererBackend::OpenGL,bool rayTracing=false) {
    using namespace k22;
    TempDirectory captures;
    auto flat=[](std::array<std::uint8_t,4> rgba) {
        auto image=std::make_shared<Image>();
        image->width=image->height=8;
        image->rgba.reserve(8*8*4);
        for(int pixel=0;pixel<8*8;++pixel)
            image->rgba.insert(image->rgba.end(),rgba.begin(),rgba.end());
        return image;
    };
    const auto white=flat({255,255,255,255}),normal=flat({255,128,128,255});
    const auto green=flat({0,255,0,255}),oppositeNormal=flat({0,128,128,255});
    // Native selected texture format flags can enable blending even with all-255 pixels.
    white->nativeTextureAlpha=green->nativeTextureAlpha=true;
    Model model;
    model.formatVersion=7;
    model.vertices={{{10,-8,-8},{-1,0,0},{0,0},{}},
                    {{10,8,-8},{-1,0,0},{1,0},{}},
                    {{10,8,8},{-1,0,0},{1,1},{}},
                    {{10,-8,8},{-1,0,0},{0,1},{}}};
    model.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};
    model.materials.resize(2);
    model.materials[0].texture=white;
    model.materials[1].texture=normal;
    model.frames.resize(2);
    for(const auto& vertex:model.vertices) {
        const auto p=vertex.position;
        model.frames[0].positions.push_back(p);
        model.frames[1].positions.push_back({p.x,p.z,-p.y});
        for(auto& pose:model.frames)pose.normals.push_back(vertex.normal);
    }
    ScriptMaterialState material;
    material.sourceId=101;
    material.effectGeneration=1;
    material.emissive={80,0,0,1};
    material.skins={white.get(),normal.get()};
    DrawInstance instance;
    instance.model=&model;
    instance.materialIndex=0;
    instance.doubleSided=instance.noFog=instance.unlit=true;
    Camera camera;
    camera.perDrawOpacity=true;
    RenderFrame frame;
    frame.instances=std::span(&instance,1);
    frame.cameras=std::span(&camera,1);
    frame.materials=std::span(&material,1);
    frame.ambient={0,0,0,1};
    frame.sunDirection={0,-1,0};
    // Retain every added model at a distinct address until Renderer destruction.
    Model shadowReceiver,shadowModel,overlapShadow,reverseShadow,nearShadow;
    World lightWorld;
    // Procedural WMB6 bytes follow check_assets' section/texture encoding. The
    // clockwise edge loop must reach the renderer through the real decoder.
    std::vector<std::uint8_t> worldBytes(140);
    std::copy_n("WMB6",4,worldBytes.begin());
    auto word=[&](std::size_t at,std::uint32_t value) {
        for(unsigned shift=0;shift<32;shift+=8)worldBytes.at(at+shift/8)=std::uint8_t(value>>shift);
    };
    auto real=[&](std::size_t at,float value) {word(at,std::bit_cast<std::uint32_t>(value));};
    auto section=[&](std::size_t index,std::size_t size) {
        const auto at=worldBytes.size();
        worldBytes.resize(at+size);
        word(4+index*8,std::uint32_t(at));word(8+index*8,std::uint32_t(size));
        return at;
    };
    const auto planeBytes=section(1,20);
    real(planeBytes+8,1);word(planeBytes+16,2);
    const auto textureBytes=section(2,8+40+255);
    word(textureBytes,1);word(textureBytes+4,8);
    std::copy_n("white",5,worldBytes.begin()+textureBytes+8);
    word(textureBytes+8+16,8);word(textureBytes+8+20,8);
    for(unsigned level=0;level<4;++level)
        word(textureBytes+8+24+level*4,std::array<std::uint32_t,4>{40,232,280,292}[level]);
    std::fill(worldBytes.begin()+textureBytes+8+40,worldBytes.end(),255);
    const auto vertexBytes=section(3,48);
    const std::array<Vec3,4> corners{{{-8,-8,0},{-8,8,0},{8,8,0},{8,-8,0}}};
    for(std::size_t vertex=0;vertex<corners.size();++vertex) {
        real(vertexBytes+vertex*12,corners[vertex].x);
        real(vertexBytes+vertex*12+4,corners[vertex].y);
    }
    const auto materialBytes=section(6,64);
    real(materialBytes,8);real(materialBytes+20,8);
    worldBytes[materialBytes+43]=100; // Zero ambient isolates the light consumers.
    const auto faceBytes=section(7,24);
    worldBytes[faceBytes+8]=4;
    (void)section(8,81); // 9x9 mono samples admit A6 CPU-lit source flags.
    const auto edgeBytes=section(12,32),edgeListBytes=section(13,16);
    for(unsigned edge=0;edge<4;++edge) {
        word(edgeBytes+edge*8,edge);word(edgeBytes+edge*8+4,(edge+1)%4);
        word(edgeListBytes+edge*4,edge);
    }
    const auto lightBytes=section(15,44);
    word(lightBytes,1);word(lightBytes+4,8);word(lightBytes+8,2);
    real(lightBytes+8+12,1);real(lightBytes+8+20,100);real(lightBytes+8+28,20);
    {
        std::ofstream file(captures.path/"quad.wmb",std::ios::binary);
        file.write(reinterpret_cast<const char*>(worldBytes.data()),worldBytes.size());
        if(!file)throw std::runtime_error("Cannot write decoded WMB GL fixture");
    }
    Assets worldAssets(captures.path);
    lightWorld=worldAssets.world("quad.wmb");
    if(lightWorld.faces.size()!=1||lightWorld.faces[0].nativeFlags!=0x40000||
       lightWorld.faces[0].texelExtentS!=128||lightWorld.faces[0].texelExtentT!=128||
       lightWorld.mesh.triangles.size()!=2||
       lightWorld.mesh.triangles[0].indices!=std::array<std::uint32_t,3>{0,2,1}||
       lightWorld.mesh.triangles[1].indices!=std::array<std::uint32_t,3>{0,3,2}||
       lightWorld.mesh.triangles[0].sourceFace!=0||lightWorld.mesh.triangles[1].sourceFace!=0||
       lightWorld.lights.size()!=1)
        throw std::runtime_error("Actual decoded WMB quad lost source winding, flags or serialized light");
    RenderOptions options;
    options.backend=backend;options.rayTracing=rayTracing;
    options.width=options.height=64;
    options.samples=1;
    options.vsync=false;
    options.title=backend==RendererBackend::OpenGL?"Kabus 22 GL consumer check":"Kabus 22 Vulkan raster consumer check";
    Renderer renderer(options);
    if(renderer.shader_version()!=2020)
        throw std::runtime_error("Actual renderer does not advertise shader capability 2020");
    rejects([&]{renderer.capture((captures.path/"capture.ppm").string());},
            "Capture accepted with no completed frame");
    auto shot=[&](const char* label,std::array<int,3> expected,float sampleX=.5f,float sampleY=.5f) {
        renderer.draw(frame);
        const auto path=(captures.path/"capture.ppm").string();
        renderer.capture(path);
        std::ifstream file(path,std::ios::binary);
        std::string magic;
        int width{},height{},maximum{};
        file>>magic>>width>>height>>maximum;
        const auto metrics=renderer.metrics();
        if(!file||magic!="P6"||maximum!=255||width!=metrics.drawableWidth||
           height!=metrics.drawableHeight||file.get()!='\n')
            throw std::runtime_error(std::string(label)+": invalid framebuffer PPM");
        const int pixelX=int(width*sampleX),pixelY=int(height*sampleY);
        if(pixelX<0||pixelX>=width||pixelY<0||pixelY>=height)
            throw std::runtime_error(std::string(label)+": framebuffer sample outside drawable");
        file.seekg((std::streamoff(pixelY)*width+pixelX)*3,std::ios::cur);
        std::array<unsigned char,3> pixel{};
        file.read(reinterpret_cast<char*>(pixel.data()),pixel.size());
        if(!file)throw std::runtime_error(std::string(label)+": truncated framebuffer PPM");
        for(std::size_t channel=0;channel<pixel.size();++channel)
            if(std::abs(int(pixel[channel])-expected[channel])>3)
                throw std::runtime_error(std::string(label)+": center RGB "+
                    std::to_string(pixel[0])+","+std::to_string(pixel[1])+","+
                    std::to_string(pixel[2])+"; expected "+std::to_string(expected[0])+","+
                    std::to_string(expected[1])+","+std::to_string(expected[2]));
    };
    shot("Ordinary live MATERIAL red emissive",{160,0,0});
    UICommand ui;
    ui.kind=UICommand::Kind::Solid;
    ui.rect={0,0,64,64};
    ui.color={0,0,128/255.0f,1};
    frame.ui=std::span(&ui,1);
    frame.adjustment.enabled=true;
    frame.adjustment.gammaRamp[2][128]=64*257;
    shot("Capture includes final UI composition and uploaded gamma",{0,0,64});
    frame.ui={};
    frame.adjustment.enabled=false;
    material.emissive={0,80,0,1};
    shot("Ordinary live MATERIAL green mutation",{0,160,0});
    material.alpha=0;
    shot("Ordinary MATERIAL alpha overwritten by geometry opacity",{0,160,0});
    material.emissive={};
    material.alpha=100;
    material.ambient={80,0,0,1};
    instance.unlit=false;
    instance.nativeLightScalar=127;
    shot("Live MATERIAL ambient with explicit native light cache 127",{80,0,0});
    instance.nativeLightScalar=255;
    shot("Live MATERIAL ambient with explicit native light cache 255",{160,0,0});
    struct LightContext {
        NativeEntityLightState returned{63,{0,4,12},{0,6,25}};
        const DrawInstance* instance{};
        const Camera* camera{};
        const Camera* preparedCamera{};
        bool dirty{true};
        unsigned calls{};
    } lightContext;
    lightContext.instance=&instance;
    lightContext.camera=&camera;
    camera.nativeAmbient=10;
    material.ambient={80,80,80,1};
    frame.lightContext=&lightContext;
    frame.prepareEntityLight=[](void* opaque,const DrawInstance& draw,const Camera& view) {
        auto& context=*static_cast<LightContext*>(opaque);
        if(&draw!=context.instance||&view!=context.camera||view.nativeAmbient!=10||
           draw.nativeLightScalar!=255)
            throw std::runtime_error("Entity light callback did not receive the actual draw and camera");
        ++context.calls;
        if(context.dirty) {
            context.preparedCamera=&view;
            context.dirty=false;
        }
        return context.returned;
    };
    shot("Live MATERIAL callback overrides packet scalar, BGR delta and bias",{69,52,46});
    lightContext.returned.scalar=127;
    shot("Live MATERIAL consumes changed callback state without changing packet",{109,92,86});
    material.ambient={80,80,80,1};
    lightContext.returned.scalar=63;
    shot("Live MATERIAL continues to consume callback state",{69,52,46});
    lightContext.returned.scalar=127;
    shot("Live MATERIAL continues to follow callback scalar",{109,92,86});
    material.ambient={};
    material.emissive={80,0,0,1};
    if(lightContext.calls!=4)
        throw std::runtime_error("Entity light callback did not execute once per actual eligible draw");
    // Rejected cameras must not consume the dirty actor's persistent light cache.
    auto eligibleShot=[&](const char* label,std::array<int,3> expected,unsigned calls) {
        lightContext.dirty=true;
        shot(label,expected);
        if(lightContext.dirty||lightContext.preparedCamera!=lightContext.camera||
           lightContext.calls!=calls)
            throw std::runtime_error(std::string(label)+": dirty light cache consumed by ineligible geometry");
    };
    std::array<Camera,2> eligibilityViews{camera,camera};
    eligibilityViews[0].position.x=30; // Entire quad is behind this camera.
    frame.cameras=eligibilityViews;
    lightContext.camera=&eligibilityViews[1];
    eligibleShot("Off-frustum first camera leaves light preparation to eligible camera",{160,0,0},5);
    eligibilityViews[0].position={};
    eligibilityViews[0].portalClip=true;
    eligibilityViews[0].portalPosition={20,0,0};
    eligibilityViews[0].portalNormal={1,0,0}; // All vertices lie on the rejected side.
    eligibleShot("Wholly portal-clipped first camera leaves light preparation to eligible camera",{160,0,0},6);
    eligibilityViews[0].portalClip=false;
    eligibilityViews[1].position.y=100;
    Model movedModel=model;
    for(auto& position:movedModel.frames[1].positions)position.y+=100;
    instance.model=&movedModel;
    instance.frame=instance.nextFrame=1;
    eligibleShot("Animated positions outside first view prepare only in later actual camera",{160,0,0},7);
    instance.model=&model;
    instance.frame=instance.nextFrame=0;
    instance.meshId=9;
    instance.meshRevision=1;
    instance.positions=movedModel.frames[1].positions;
    instance.normals=movedModel.frames[1].normals;
    eligibleShot("Deformed positions outside first view prepare only in later actual camera",{160,0,0},8);
    ++instance.meshRevision;
    instance.positions=model.frames[0].positions;
    lightContext.camera=&eligibilityViews[0];
    // The restored actor is visible only in the first view. Preserve that view's
    // color so the later off-frustum camera cannot erase the actual GPU consumer.
    const bool laterViewClear=eligibilityViews[1].clearColor;
    eligibilityViews[1].clearColor=false;
    eligibleShot("Same actor deformed back inside first view prepares there",{160,0,0},9);
    eligibilityViews[1].clearColor=laterViewClear;
    instance.positions=instance.normals={};
    instance.meshId=0;
    frame.cameras=std::span(&camera,1);
    lightContext.camera=&camera;
    frame.prepareEntityLight=nullptr;
    frame.lightContext=nullptr;
    material.diffuse=material.emissive={};
    material.albedoRaw=102400;
    material.ambient={80,0,0,1};
    camera.nativeAmbient=0;
    frame.sunDirection={0,-1,0};
    shot("Live MATERIAL without callback restores stored light cache",{160,0,0});
    material.emissive=material.specular=material.diffuse={};
    material.albedoRaw=102400;
    material.ambient={80,0,0,1};
    frame.sunDirection={0,-1,0};
    frame.sunColor={};
    camera.perDrawOpacity=false; // Source-special packets exist only on the A6 route.
    camera.nativeAmbient=10;
    instance.nativeFloorLight=false;
    instance.nativeSpecialColor={20,40,60}; // PUBLIC B/G/R already passed through signed SAR10.
    instance.ambient=91;
    instance.unlit=instance.bright=true;
    instance.nativeLightDelta={100,200,300};
    instance.nativeLightBias={700,800,900};
    instance.tint={0,1,0,1};
    material.ambient={127.5f,127.5f,127.5f,1};
    frame.ambient={1,0,0,1};
    camera.ambient={0,0,1,1};
    frame.prepareEntityLight=[](void* opaque,const DrawInstance& draw,const Camera& view) {
        auto& context=*static_cast<LightContext*>(opaque);
        if(&draw!=context.instance||&view!=context.camera)
            throw std::runtime_error("Source-special MATERIAL lost actual callback consumers");
        ++context.calls;
        return context.returned;
    };
    frame.lightContext=&lightContext;
    lightContext.returned={63,{9,19,29},{100,200,300}};
    shot("A6 source-special direct public BGR ignores ambient VIEW UNLIT BRIGHT DTO and tint",{60,40,20});
    lightContext.returned={0,{-100,-200,-300},{-1000,-2000,-3000}};
    instance.ambient=-120;
    instance.unlit=instance.bright=false;
    camera.nativeAmbient=120;
    frame.ambient={};
    camera.ambient={};
    shot("A6 source-special RGB remains independent of changed ordinary lighting inputs",{60,40,20});
    instance.nativeSpecialColor={0,0,0};
    shot("A6 zero source-special color does not fall back to FLOAT tint",{0,0,0});
    instance.nativeSpecialColor={20,40,60};
    frame.prepareEntityLight=nullptr;
    frame.lightContext=nullptr;
    instance.nativeFloorLight=true;
    instance.nativeSpecialColor={};
    instance.nativeLightDelta={};
    instance.nativeLightBias={};
    instance.ambient=0;
    instance.tint={1,1,1,1};
    instance.bright=false;
    camera.perDrawOpacity=true;
    camera.nativeAmbient=0;
    material.diffuse={};
    material.albedoRaw=102400;
    material.ambient={80,0,0,1};
    frame.sunDirection={0,-1,0};
    instance.unlit=true;
    shot("UNLIT live MATERIAL omits native scalar light cache",{0,0,0});
    instance.nativeLightScalar=0;
    material.ambient={};
    // Ordinary D3D vertex specular is added after the diffuse texture cascade.
    // These packets deliberately disagree with the separate literal-FX sun vector.
    const auto black=flat({0,0,0,255});
    Model shineModel=model;
    shineModel.frames.clear();
    // Original D3D VIEW camera-space +Z is world +X at zero angles.
    for(auto& vertex:shineModel.vertices)vertex.normal={1,0,0};
    shineModel.materials[0].texture=black;
    instance.model=&shineModel;
    instance.unlit=false;
    frame.bonusProfile=true;
    material.specular={255,255,255,1};
    material.power=4;
    RenderLight shineLight;
    shineLight.position={20,0,0};
    shineLight.color={.25f,.25f,.25f,1};
    shineLight.range=100;
    shineLight.attenuation={1,0,0};
    frame.lights=std::span(&shineLight,1);
    shot("Point light specular survives black texture; vertex half-vector",{44,44,44});
    material.albedoRaw=0;
    shot("Absent selected SUN leaves point-light specular unscaled even with ALBEDO zero",{44,44,44});
    material.albedoRaw=102400;
    shineLight.attenuation={2,0,0};
    shot("Point specular uses actual reciprocal hardware attenuation",{22,22,22});
    shineLight.attenuation={1,0,0};
    shineLight.type=2;
    shineLight.direction={1,0,0};
    shot("Spotlight cone rejects specular outside source phi",{0,0,0});
    shineLight.type=1;
    shineLight.range=1;
    shot("Point specular honors hardware light range",{0,0,0});
    shineLight.type=3;
    shineLight.direction={-.5f,-std::sqrt(.75f),0};
    shineLight.color={1,1,1,1};
    shot("Infinite-viewer half-vector power4 at sixty degrees is 0.5625",{143,143,143});
    shineLight.direction={-1,0,0};
    shot("Original LH VIEW infinite viewer maps zero-angle +Z to world +X",{255,255,255});
    camera.position={2,1,0};
    shot("LOCALVIEWER false is independent of finite camera position",{255,255,255});
    camera.position={};
    camera.angles.x=instance.angles.x=90;
    shineLight.direction={0,-1,0};
    shot("Original LH VIEW infinite viewer follows yaw90 world +Y",{255,255,255});
    camera.angles=instance.angles={};
    shineLight.direction={-1,0,0};
    for(auto& vertex:shineModel.vertices)vertex.normal=vertex.position.y<0?Vec3{1,0,0}:Vec3{0,1,0};
    renderer.purge_model(&shineModel);
    // The center pixel is just right of the screen midpoint: interpolated
    // vertex power is .5 + (1/64)*10*tan(75deg/2)*(4/3)/8/2.
    shot("Specular interpolates vertex powers, not interpolated-normal power",{130,130,130});
    for(auto& vertex:shineModel.vertices)vertex.normal={1,0,0};
    renderer.purge_model(&shineModel);
    const auto gray=flat({32,32,32,255});
    shineModel.materials[0].texture=gray;
    material.specular={};
    material.diffuse={255,255,255,1};
    std::array<RenderLight,2> clampLights{shineLight,shineLight};
    for(auto& light:clampLights)light.color={.75f,.75f,.75f,1};
    frame.lights=clampLights;
    shot("Ordinary diffuse sum clamps at each vertex before MODULATE2X",{64,64,64});
    frame.lights=std::span(&shineLight,1);
    material.specular={};
    material.diffuse={80,0,0,1};
    material.albedoRaw=51200;
    instance.nativeLightScalar=127;
    shineModel.materials[0].texture=white;
    shineLight.synthetic=true;
    shineLight.color={.5f,.5f,.5f,1};
    frame.sunDirection={-1,0,0};
    shot("A7 hardware sun uses original ray direction and SUN_COLOR255*SUN_LIGHT50/25500",{79,0,0});
    shineLight.direction={1,0,0};
    shot("Opposite hardware sun is dark despite positive literal FX packet",{0,0,0});
    material.diffuse={};
    material.specular={255,255,255,1};
    shineModel.materials[0].texture=black;
    shineLight.direction={-1,0,0};
    material.albedoRaw=0;
    shot("ALBEDO disables selected hardware sun specular as well as diffuse",{0,0,0});
    material.albedoRaw=51200;
    shot("Selected fixed-bank sun specular is not doubled with diffuse stage",{127,127,127});
    instance.alpha=.5f;
    shot("Separate ordinary specular is added before geometry alpha blending",{64,64,64});
    instance.alpha=1;
    instance.unlit=true;
    shot("ENTITY UNLIT retains explicit ordinary material specular",{127,127,127});
    instance.unlit=false;
    shineLight.color={.5f,.25f,0,1}; // A7 SUN_COLOR(255,127.5,0), SUN_LIGHT50 /25500.
    shot("A7 source-colored SUN specular retains RGB rather than a gray FX scalar",{127,63,0});
    shineLight.color={.5f,.5f,.5f,1};
    material.albedoRaw=-8192;
    shot("A7 fixed-bank SUN uses nonzero signed ALBEDO and divisor128",{11,11,11});
    frame.nativeFixedLightBank=false;
    shot("A7 direct SUN rejects negative ALBEDO independently of fixed-bank rule",{0,0,0});
    material.albedoRaw=51200;
    shot("A7 direct SUN uses divisor127 rather than fixed128",{128,128,128});
    frame.bonusProfile=false;
    shineLight.color={1,1,1,1}; // A6 source float32 .02 * SUN_LIGHT50, not .01.
    shot("A6 hardware SUN is gray at source 0.02 scale and sampled scalar127/255",{127,127,127});
    frame.bonusProfile=true;
    shineLight.color={.5f,.5f,.5f,1};
    frame.nativeFixedLightBank=true;
    frame.hardwareLighting=false;
    material.emissive={80,0,0,1};
    shot("D3D_TRANSFORM zero disables ordinary material lighting without alpha changes",{0,0,0});
    frame.hardwareLighting=true;
    material.emissive={};
    material.specular={};
    frame.lights={};
    frame.bonusProfile=false;
    instance.model=&model;
    instance.unlit=true;
    instance.nativeLightScalar=0;
    // The synthetic directional packet exists for ordinary MATERIAL, not legacy point lighting.
    const auto oldMaterialIndex=instance.materialIndex;
    instance.materialIndex=~0u;
    frame.materials={};
    instance.unlit=false;
    instance.ambient=-127;
    const auto oldPosition=instance.position;
    const auto oldNearClip=camera.nearClip;
    instance.position.x=-9.5f;camera.nearClip=.01f;
    frame.sunDirection={};
    RenderLight phantomSun;phantomSun.type=3;phantomSun.synthetic=true;phantomSun.range=1;
    phantomSun.color={1,1,1,1};phantomSun.intensity=1;
    frame.lights=std::span(&phantomSun,1);
    shot("Nonmaterial fragment lighting excludes synthetic directional SUN point alias",{0,0,0});
    phantomSun.type=1;
    shot("Nonmaterial fragment lighting retains an actual synthetic point record",{64,64,64});
    phantomSun.type=3;phantomSun.baked=true;phantomSun.intensity=.25f;
    shot("Nonmaterial fragment lighting retains serialized baked synthetic records",{16,16,16});
    frame.lights={};frame.materials=std::span(&material,1);
    instance.materialIndex=oldMaterialIndex;
    instance.position=oldPosition;camera.nearClip=oldNearClip;instance.unlit=true;instance.ambient=0;
    frame.sunDirection={0,-1,0};
    auto skills=[&](std::array<float,8> values) {
        for(std::size_t index=0;index<values.size();++index)
            material.skillBits[index]=std::bit_cast<std::uint32_t>(values[index]);
    };
    const auto specNormal=flat({128,128,255,255}),specColor=flat({128,64,32,255});
    specColor->nativeTextureAlpha=true;
    material.effect=Renderer::identify_effect("Normal_Mix.fx",mix);
    skills({0,0,.5f,4,0,0,0,0});
    model.materials[0].texture=specColor;
    model.materials[1].texture=specNormal;
    frame.sunDirection={1,0,0};
    shot("Literal Normal_Mix skill3 uses reflection vector and full RGBA texture modulation",{32,16,8});
    material.effect=Renderer::identify_effect("Normal_SunMix.fx",sunMix);
    skills({0,0,.5f,4,.1f,.2f,.3f,.4f});
    shot("Literal Normal_SunMix skill3 remains source-authored in every backend",{32,16,8});
    model.materials[0].texture=white;
    model.materials[1].texture=normal;
    frame.sunDirection={0,-1,0};
    material.effect=Renderer::identify_effect("Normal_Mix.fx",mix);
    skills({0,1,0,1,0,0,0,0});
    shot("Normal_Mix raw skills and source RGBA",{207,207,230});
    frame.sunDirection={0,-.25f,0};
    shot("Exact Normal_Mix keeps its normalized source light at quarter SUNLIGHT",{207,207,230});
    frame.sunDirection={0,-.5f,0};
    shot("Exact Normal_Mix is unaffected by ordinary SUNLIGHT intensity fix",{207,207,230});
    frame.sunDirection={0,-1,0};
    camera.alpha=.2f;
    camera.opacityByte=51;
    instance.alpha=.1f;
    material.alpha=0;
    shot("Normal_Mix ignores positive entity/view and MATERIAL alpha",{207,207,230});
    camera.alpha=instance.alpha=1;
    camera.opacityByte=255;
    material.alpha=100;
    material.effect=Renderer::identify_effect("Normal_SunMix.fx",sunMix);
    skills({0,1,0,1,.5f,0,0,.5f});
    shot("Normal_SunMix raw skills and source RGBA",{64,0,0});
    frame.sunDirection={0,-.25f,0};
    shot("Exact Normal_SunMix keeps source sun skills at quarter SUNLIGHT",{64,0,0});
    frame.sunDirection={0,-.5f,0};
    shot("Exact Normal_SunMix is unaffected by ordinary SUNLIGHT intensity fix",{64,0,0});
    frame.sunDirection={0,-1,0};
    camera.alpha=.2f;
    camera.opacityByte=51;
    instance.alpha=.1f;
    material.alpha=0;
    shot("Normal_SunMix ignores positive entity/view and MATERIAL alpha",{64,0,0});
    camera.alpha=instance.alpha=1;
    camera.opacityByte=255;
    material.alpha=100;
    material.effect=Renderer::identify_effect("Normal_Mix.fx",mix);
    skills({0,1,0,1,0,0,0,0});
    // Deliberately scrambled: resolve each bank by its source image, not list position.
    std::array<SkinBinding,2> bindings{{{normal.get(),normal.get(),false,0,false},
                                        {white.get(),green.get(),false,0,true}}};
    frame.skinBindings=bindings;
    shot("Color-bank replacement",{0,207,0});
    const auto moviePixels=flat({0,255,0,255}); // Pixel source has no native target alpha flag.
    bindings[1]={white.get(),moviePixels.get(),true,1,true};
    shot("Movie-like replacement retains target bitmap alpha flag independently of pixels",{0,207,0});
    bindings[1].nativeTextureAlpha=false;
    shot("Identical movie pixels with opaque target descriptor disable FX blending",{0,230,0});
    bindings[1]={white.get(),green.get(),false,0,true};
    bindings[0].image=oppositeNormal.get();
    shot("Following normal-bank replacement",{0,0,0});
    bindings[0].image=normal.get();
    bindings[1].image=white.get();
    shot("Restored color and normal banks",{207,207,230});
    model.materials[1].texture.reset();
    material.skins[1]=nullptr;
    shot("Missing normal retains this material's previously bound sampler",{207,207,230});
    model.materials[0].texture.reset();
    material.skins[0]=nullptr;
    shot("Both null samplers retain banks but current null bitmap disables blending",{230,230,255});
    material.skins[0]=white.get();
    shot("Alpha-marked MATERIAL fallback does not supply current ENTITY bitmap flag",{230,230,255});
    material.skins[0]=nullptr;
    model.materials[0].texture=white;
    material.skins[0]=white.get();
    material.sourceId=102;
    shot("Different material starts with actual GL unbound normal sampler",{0,0,0});
    material.sourceId=101;
    shot("Returning material retains only its own sampler banks",{207,207,230});
    ++material.effectGeneration;
    shot("Recompiled generation resets the missing normal sampler",{0,0,0});
    model.materials[1].texture=normal;
    material.skins[1]=normal.get();
    shot("Non-null normal updates the recompiled material bank",{207,207,230});
    model.materials[1].texture.reset();
    material.skins[1]=nullptr;
    renderer.purge_image(normal.get());
    shot("Purged GPU image clears retained normal sampler identity",{0,0,0});
    model.materials[1].texture=normal;
    material.skins[1]=normal.get();
    frame.skinBindings={};
    shot("Static animation tangent",{207,207,230});
    instance.frame=instance.nextFrame=1;
    shot("Rotated animation tangent",{0,0,0});
    // First draw uploads ordinary mutable positions/normals without needing tangents.
    instance.frame=instance.nextFrame=0;
    instance.meshId=8;
    instance.meshRevision=1;
    instance.positions=model.frames[1].positions;
    instance.normals=model.frames[1].normals;
    material.effect=EffectKind::None;
    material.emissive={80,0,0,1};
    shot("Ordinary deformed pose before first FX use",{160,0,0});
    material.emissive={};
    material.effect=Renderer::identify_effect("Normal_Mix.fx",mix);
    shot("Same override revision first FX derives rotated tangent",{0,0,0});
    renderer.purge_model(&model);
    shot("Purged model and same override identity rebuild functional FX geometry",{0,0,0});
    ++instance.meshRevision;
    instance.positions=model.frames[0].positions;
    instance.normals=model.frames[0].normals;
    shot("Reused purged override updates back to visible FX tangent",{207,207,230});
    instance.positions=instance.normals={};
    instance.meshId=0;
    instance.frame=instance.nextFrame=0;
    instance.meshId=1;
    instance.meshRevision=1;
    instance.positions=model.frames[1].positions;
    instance.normals=model.frames[1].normals;
    shot("Rotated revisioned mesh tangent",{0,0,0});
    instance.meshRevision=2;
    instance.positions=model.frames[0].positions;
    instance.normals=model.frames[0].normals;
    shot("Updated revisioned mesh tangent",{207,207,230});
    instance.positions=instance.normals={};
    instance.meshId=0;
    instance.materialIndex=~0u;
    material.effect=EffectKind::None;
    instance.tint={1,0,0,1};
    shot("A7 ordinary retained red seed",{255,0,0});
    instance.tint={0,1,0,1};
    camera.alpha=.4f;
    camera.opacityByte=0; // FLOAT view opacity must not be replaced by its cached byte.
    camera.clearColor=false;
    frame.clearScreen=false;
    shot("A7 ordinary FLOAT opacity over retained destination",{153,102,0});
    frame.instances={};
    shot("Retained color without instances or clear",{153,102,0});
    camera.clearColor=true;
    shot("Explicit view color clear",{0,0,0});
    camera.clearColor=false;
    frame.clearColor={0,0,1,1};
    renderer.release_scene();
    shot("release_scene invalidates retained color",{0,0,255});
    frame.clearColor={1,0,0,1};
    shot("Valid retained destination ignores changed seed",{0,0,255});
    camera.clearColor=true;
    shot("Resize red destination before invalidation",{255,0,0});
    camera.clearColor=false;
    frame.clearColor={0,0,1,1};
    renderer.set_mode(80,64,WindowMode::Windowed);
    shot("Resize invalidates retained color",{0,0,255});
    frame.clearColor={0,0,0,1};
    frame.clearScreen=true;
    camera.clearColor=true;
    camera.background={};
    camera.alpha=.05f;
    camera.opacityByte=12;
    instance.tint={1,1,1,1};
    material.emissive={80,0,0,1};
    material.alpha=0;
    Particle particle;
    particle.position={10,0,0};
    particle.size=16;
    particle.image=white.get();
    particle.materialIndex=0;
    particle.translucent=true;
    particle.alphaRaw=20*1024;
    frame.particles=std::span(&particle,1);
    shot("CPU particle raw20 ignores source MATERIAL and view opacity",{50,50,50});
    particle.additive=true;
    shot("CPU BRIGHT textured particle raw20",{100,100,100});
    particle.additive=false;
    particle.alphaRaw=48*1024;
    shot("CPU particle raw48",{120,120,120});
    particle.translucent=false;
    particle.alphaRaw=0;
    particle.color.a=0;
    shot("Opaque CPU particle ignores raw and color alpha",{255,255,255});
    frame.particles={};
    camera.alpha=1;
    camera.opacityByte=255;
    frame.instances=std::span(&instance,1);
    model.materials.resize(4);
    model.materials[0].texture=white;
    model.materials[1].texture=oppositeNormal;
    model.materials[2].texture=green;
    model.materials[2].nativePrimarySubset=true;
    model.materials[3].texture=normal;
    for(auto& triangle:model.triangles)triangle.materialIndex=2;
    renderer.purge_model(&model);
    instance.skinIndex=0;
    instance.unlit=true;
    shot("A7 positive MDL7 subset uses global bank2 instead of ENTITY skin0",{0,255,0});
    camera.perDrawOpacity=false;
    shot("A6 MDL7 face attribute is ignored in favor of ENTITY skin0",{255,255,255});
    camera.perDrawOpacity=true;
    model.materials[2].nativePrimarySubset=false;
    frame.clearColor={0,0,1,1};
    shot("A7 nonprimary positive MDL7 bank is skipped over actual clear",{0,0,255});
    model.materials[2].nativePrimarySubset=true;
    frame.clearColor={0,0,0,1};
    instance.materialIndex=0;
    material.sourceId=201;
    material.effectGeneration=1;
    material.emissive={};
    material.effect=Renderer::identify_effect("Normal_Mix.fx",mix);
    skills({0,1,0,1,0,0,0,0});
    // maxPrimary=2, bank1 is nonnull/nonprimary: layers=2, subset2 follows bank3.
    shot("A7 positive subset FX uses bank3, not ENTITY skin0 following bank1",{0,207,0});
    camera.perDrawOpacity=false;
    shot("A6 same FX uses ENTITY skin0 following opposite normal bank1",{0,0,0});
    camera.perDrawOpacity=true;
    for(auto& triangle:model.triangles)triangle.materialIndex=0;
    renderer.purge_model(&model);
    instance.skinIndex=2;
    material.sourceId=202;
    // maxPrimary=0: j=1 even when ENTITY selects alternate color bank2.
    shot("A7 base subset FX follows maxPrimary bank1, not selected skin2 bank3",{0,0,0});
    camera.perDrawOpacity=false;
    shot("A6 alternate ENTITY skin2 FX follows its adjacent normal bank3",{0,207,0});
    camera.perDrawOpacity=true;
    for(auto& triangle:model.triangles)triangle.materialIndex=0xffffffffu;
    renderer.purge_model(&model);
    instance.skinIndex=0;
    instance.materialIndex=~0u;
    shot("A7 raw negative MDL7 faces render normalized display bank0",{255,255,255});
    instance.materialIndex=0;
    material.sourceId=203;
    shot("A7 negative MDL7 display bank0 derives following normal from normalized MAX",{0,0,0});
    for(const auto& triangle:model.triangles)
        if(triangle.materialIndex!=0xffffffffu)
            throw std::runtime_error("Rendering changed the raw negative MDL7 face attribute");
    const auto opaqueWhite=flat({255,255,255,255});
    model.materials[0].texture=opaqueWhite;
    instance.skinIndex=0;
    material.sourceId=301;
    material.effectGeneration=1;
    material.ambient={255,255,255,1};
    skills({.15f,0,0,1,0,0,0,0});
    shot("Opaque A7 FX writes source RGB .15 without multiplying shader alpha",{38,38,38});
    model.materials[0].texture=white;
    shot("Native alpha-format flag blends FX despite all-255 bitmap alpha pixels",{6,6,6});
    model.materials[0].texture=opaqueWhite;
    shot("Current opaque bitmap overrides previously retained alpha-format sampler",{38,38,38});
    camera.alpha=.4f;
    camera.opacityByte=102;
    shot("Camera40 enables opaque FX blend using source shader alpha .15, not view .4",{6,6,6});
    camera.alpha=1;
    camera.opacityByte=255;
    model.materials[0].texture.reset();
    material.skins[0]=white.get();
    shot("Current null ENTITY bitmap leaves alpha-marked FX fallback opaque",{38,38,38});
    material.skins[0]=nullptr;
    shot("Current null bitmap leaves retained alpha-marked FX sampler opaque",{38,38,38});
    model.materials[0].texture=opaqueWhite;
    std::array<ScriptMaterialState,2> depthMaterials{material,material};
    depthMaterials[0].sourceId=302;
    depthMaterials[1].sourceId=303;
    depthMaterials[1].ambient={0,255,0,1};
    depthMaterials[1].skillBits[0]=std::bit_cast<std::uint32_t>(1.0f);
    std::array<DrawInstance,2> depthInstances{};
    for(std::size_t index=0;index<depthInstances.size();++index) {
        auto& draw=depthInstances[index];
        draw.model=&model;
        draw.materialIndex=index;
        draw.doubleSided=draw.noFog=draw.unlit=true;
        draw.position.x=index*10.0f;
    }
    frame.instances=depthInstances;
    frame.materials=depthMaterials;
    camera.alpha=.4f;
    camera.opacityByte=102;
    shot("Camera-only FX blending retains depth writes against later opaque model",{6,6,6});
    // Exercise actual A7 queue routing, not only the isolated model pass helper.
    depthMaterials[0].effect=depthMaterials[1].effect=EffectKind::None;
    depthMaterials[0].ambient=depthMaterials[1].ambient={};
    depthMaterials[0].emissive={80,0,0,1};
    depthMaterials[1].emissive={0,80,0,1};
    depthInstances[0].additive=true; // BRIGHT alone is opaque for an opaque current bitmap.
    depthInstances[1].position.x=30;
    camera.alpha=1;
    camera.opacityByte=255;
    shot("A7 additive-only opaque current bitmap occludes later overlapping model",{160,0,0});
    camera.alpha=.4f;
    camera.opacityByte=102;
    shot("A7 additive-only opaque camera40 still occludes later overlapping model",{64,0,0});
    frame.instances=std::span(depthInstances.data(),1);
    particle.position={20,0,0};
    particle.size=16;
    particle.image=green.get();
    particle.translucent=particle.additive=false;
    particle.color={1,1,1,1};
    frame.particles=std::span(&particle,1);
    shot("A7 additive-only opaque camera40 writes depth against behind CPU particle",{64,0,0});
    camera.alpha=1;
    camera.opacityByte=255;
    shot("A7 additive-only opaque full view writes depth against behind CPU particle",{160,0,0});
    // Same pixels, different current target descriptor: alpha flag disables the depth write.
    std::array<SkinBinding,1> depthBinding{{{opaqueWhite.get(),opaqueWhite.get(),false,0,true}}};
    frame.skinBindings=depthBinding;
    shot("Current alpha target metadata routes BRIGHT model without depth writes",{0,255,0});
    depthBinding[0].nativeTextureAlpha=false;
    shot("Current opaque target metadata restores actual BRIGHT depth occlusion",{160,0,0});
    frame.skinBindings={};
    camera.perDrawOpacity=false;
    shot("A6 legacy additive-only model remains no-depth-write queued",{0,255,0});
    camera.perDrawOpacity=true;
    depthInstances[0].translucent=true;
    shot("A7 explicit TRANSPARENT preserves no-depth-write BRIGHT routing",{0,255,0});
    frame.particles={};
    // Cursor variants share a borrowed image with ordinary UI, never its upload/key policy.
    frame={};
    const auto cursorMetrics=renderer.metrics();
    std::array<UICommand,2> cursorUI{};
    cursorUI[0].kind=UICommand::Kind::Solid;
    cursorUI[0].rect={0,0,float(cursorMetrics.width),float(cursorMetrics.height)};
    cursorUI[0].color={0,0,1,1};
    auto cursor=flat({0,0,0,255});
    cursor->sourceDepth=24;
    cursor->nativeStorageBytes=2;
    const auto originalCursor=cursor->rgba;
    auto& cursorCommand=cursorUI[1];
    cursorCommand.image=cursor.get();
    cursorCommand.rect={float(cursorMetrics.width/2-4),float(cursorMetrics.height/2-4),8,8};
    frame.ui=cursorUI;
    shot("Ordinary unkeyed RGB24 black remains opaque",{0,0,0});
    cursorCommand.overlay=true;
    cursorCommand.cursorOverlay=true;
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey16;
    shot("Packed cursor black reveals underlying UI",{0,0,255});
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey24;
    shot("RGB24 cursor black reveals underlying UI",{0,0,255});
    if(cursor->rgba!=originalCursor)
        throw std::runtime_error("Cursor upload mutated borrowed image pixels");
    cursorCommand.dynamicImage=true;
    auto cursorPixel=[&](std::array<std::uint8_t,4> pixel) {
        for(std::size_t offset=0;offset<cursor->rgba.size();offset+=4)
            std::copy(pixel.begin(),pixel.end(),cursor->rgba.begin()+offset);
        ++cursorCommand.imageSerial;
    };
    cursorPixel({7,6,7,255}); // Nonblack RGB24, but the recovered nonstandard packed value is zero.
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey16;
    shot("Packed near-black cursor edge is transparent",{0,0,255});
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey24;
    shot("RGB24 near-black cursor edge is opaque",{7,6,7});
    cursorPixel({0,1,0,255}); // G bit0 survives source packing, then vanishes in destination RGB555.
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey16;
    shot("Nonzero source packed green bit retains opaque black destination",{0,0,0});
    cursorPixel({17,66,131,255});
    shot("Packed cursor uses opaque A1RGB555 destination channels",{16,66,132});
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey24;
    shot("RGB24 cursor preserves opaque source channels",{17,66,131});
    cursorCommand.textureMode=UICommand::TextureMode::Default;
    cursorCommand.overlay=false;
    shot("Ordinary upload stays independent of cursor variants",{17,66,131});
    renderer.purge_image(cursor.get());
    cursorCommand.dynamicImage=false;
    cursorPixel({0,0,0,255});
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey16;
    cursorCommand.overlay=true;
    shot("Purge replaces packed cursor upload",{0,0,255});
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey24;
    shot("Purge replaces RGB24 cursor upload",{0,0,255});
    cursorCommand.textureMode=UICommand::TextureMode::Default;
    cursorCommand.overlay=false;
    shot("Purge also replaces ordinary upload",{0,0,0});
    renderer.release_scene();
    cursorPixel({255,0,0,255});
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey16;
    cursorCommand.overlay=true;
    shot("Scene release reuploads borrowed cursor",{255,0,0});
    renderer.purge_image(cursor.get());
    cursorCommand.image=nullptr;
    cursor.reset();
    cursor=flat({0,255,0,255});
    cursorCommand.image=cursor.get();
    shot("Purged image death and replacement do not retain cursor resources",{0,255,0});
    Image video=*flat({0,0,255,255});
    frame.video=&video;
    shot("Software cursor remains above final video composition",{0,255,0});
    frame.adjustment.enabled=true;
    frame.adjustment.gammaRamp[1][255]=128*257;
    shot("Software cursor participates in existing final gamma and capture",{0,128,0});
    frame.adjustment.enabled=false;
    cursor->nativeTextureAlpha=true;
    cursorCommand.textureMode=UICommand::TextureMode::Default;
    cursorCommand.dynamicImage=true;
    cursorPixel({0,0,0,255});
    shot("Explicit-alpha cursor bypasses black key while remaining above video",{0,0,0});
    cursorPixel({255,255,255,0});
    shot("Explicit-alpha cursor preserves source transparency",{0,0,255});
    cursor->nativeTextureAlpha=false;
    cursor->width=1;cursor->height=3;
    cursor->rgba={255,0,0,255,0,255,0,255,0,0,255,255};
    ++cursorCommand.imageSerial;
    cursorCommand.rect={float(cursorMetrics.width/2),float(cursorMetrics.height/2-1),1,3};
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey16;
    shot("Dynamic packed cursor resize handles unaligned two-byte rows",{0,255,0});
    cursorCommand.textureMode=UICommand::TextureMode::CursorKey24;
    shot("Dynamic RGB24 cursor resize remains independent",{0,255,0});
    // Independent A6/A7 source constants: alpha127/255, screen-area -2*CLIP_SIZE,
    // duplicate normalized depth 2^-11, and sun X .75*cos(60)/tan(60).
    renderer.release_scene();
    renderer.set_mode(64,64,WindowMode::Windowed);
    frame={};
    camera={};
    camera.position={0,0,20};
    camera.angles={0,-90,0};
    camera.farClip=100;
    camera.perDrawOpacity=true;
    frame.cameras=std::span(&camera,1);
    frame.ambient={};
    frame.sunDirection={};
    frame.shadowSun={0.21650635f,0};
    frame.shadowOffset=2;
    frame.shadowAlpha=50;
    frame.clipSize=0;
    shadowReceiver.formatVersion=7;
    shadowReceiver.vertices={{{-30,-30,0},{0,0,1},{},{}},
                             {{30,-30,0},{0,0,1},{},{}},
                             {{30,30,0},{0,0,1},{},{}},
                             {{-30,30,0},{0,0,1},{},{}}};
    shadowReceiver.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};
    shadowReceiver.materials.resize(1);
    shadowReceiver.materials[0].texture=opaqueWhite;
    DrawInstance receiver;
    receiver.model=&shadowReceiver;
    receiver.unlit=receiver.doubleSided=receiver.noFog=true;
    frame.instances=std::span(&receiver,1);
    shadowModel.formatVersion=7;
    shadowModel.vertices={{{-4,-4,4},{0,0,1},{},{}},
                          {{4,-4,4},{0,0,1},{},{}},
                          {{0,4,4},{0,0,1},{},{}}};
    shadowModel.triangles={{{0,1,2},0,0}};
    shadowModel.materials.resize(1);
    shadowModel.materials[0].texture=opaqueWhite;
    shadowModel.frames.resize(2);
    for(const auto& vertex:shadowModel.vertices) {
        shadowModel.frames[0].positions.push_back(vertex.position);
        shadowModel.frames[1].positions.push_back(
            {vertex.position.x+12,vertex.position.y,vertex.position.z});
        for(auto& pose:shadowModel.frames)pose.normals.push_back(vertex.normal);
    }
    DrawInstance caster;
    caster.model=&shadowModel;
    caster.position.x=.8660254f; // Four units above plane: cancels sx*4 at center.
    caster.visible=false;
    caster.nativeShadow={true,0,{0,0,-1}};
    frame.shadows=std::span(&caster,1);
    shot("Invisible source-selected animated triangle casts alpha127 shadow",{128,128,128});
    caster.nativeShadow.caster=false;
    shot("Noncaster packet leaves actual white receiver",{255,255,255});
    // Low-tilt sx=9 moves an off-view actor's x=40,z=4 footprint to x=4.
    // At plane z=2, depth18 gives vertical half-span10.3589143; pixel row20
    // (sampleY .3125) is x=3.7227348, safely inside the projected triangle.
    caster.position.x=40;
    caster.visible=caster.unlit=caster.doubleSided=caster.noFog=true;
    caster.tint={1,0,0,1};
    caster.nativeShadow.caster=true;
    frame.shadowSun={9,0};
    std::array<DrawInstance,2> offViewDraws{receiver,caster};
    frame.instances=offViewDraws;
    struct OffViewContext { const DrawInstance* ordinary{}; unsigned ordinaryCalls{}; } offViewContext;
    offViewContext.ordinary=&offViewDraws[1];
    frame.lightContext=&offViewContext;
    frame.prepareEntityLight=[](void* opaque,const DrawInstance& draw,const Camera&) {
        auto& state=*static_cast<OffViewContext*>(opaque);
        if(&draw==state.ordinary)++state.ordinaryCalls;
        return NativeEntityLightState{draw.nativeLightScalar,draw.nativeLightDelta,
                                     draw.nativeLightBias,draw.nativeShadow};
    };
    shot("Off-view actor casts long low-tilt shadow at independent visible receiver pixel",
         {128,128,128},.5f,.3125f);
    shot("Long projected shadow does not invent coverage at receiver center",{255,255,255});
    caster.nativeShadow.caster=false;
    shot("Disabled off-view shadow leaves independently selected receiver pixel white",
         {255,255,255},.5f,.3125f);
    if(offViewContext.ordinaryCalls)
        throw std::runtime_error("Off-view planar admission incorrectly admitted ordinary actor geometry");
    frame.prepareEntityLight=nullptr;
    frame.lightContext=nullptr;
    frame.instances=std::span(&receiver,1);
    frame.shadowSun={.21650635f,0};
    caster.position.x=.8660254f;
    caster.visible=false;
    caster.tint={1,1,1,1};
    caster.nativeShadow.caster=true;
    caster.nextFrame=1;
    caster.interpolation=.125f;
    shot("Interpolated animated planar triangle still covers receiver center",{128,128,128});
    caster.interpolation=.5f;
    shot("Interpolated planar pose moves off receiver center",{255,255,255});
    caster.frame=caster.nextFrame=1;
    caster.interpolation=0;
    shot("Selected animated frame not static mesh controls projected footprint",{255,255,255});
    caster.frame=caster.nextFrame=0;
    caster.alpha=.01f;
    frame.shadowAlpha=25;
    shot("Live shadow MATERIAL25 ignores ordinary actor alpha",{192,192,192});
    frame.shadowAlpha=50;
    camera.opacityByte=128;
    shot("Shadow alpha truncates 50 percent times prepared view byte128",{191,191,191});
    camera.opacityByte=1;
    shot("Prepared view byte1 truncates primary shadow alpha to zero",{255,255,255});
    camera.opacityByte=255;
    caster.alpha=1;
    camera.noShadow=true;
    shot("VIEW NOSHADOW suppresses shadow but retains receiver",{255,255,255});
    camera.noShadow=false;
    // Keep the background white when entity suppression removes the receiver as well.
    camera.background={1,1,1,1};
    camera.environmentOverride=true;
    camera.noEntities=true;
    shot("VIEW NOENTITIES suppresses independently queued shadow",{255,255,255});
    camera.noEntities=false;
    frame.renderEntities=false;
    shot("render_ent off suppresses independently queued shadow",{255,255,255});
    frame.renderEntities=true;
    shot("Entity suppression recovery restores actual projected triangle",{128,128,128});
    frame.shadows={}; // Engine emits no packet for a null source-selected shadow slot.
    shot("Missing selected source resource has no fallback silhouette",{255,255,255});
    frame.shadows=std::span(&caster,1);
    reverseShadow=shadowModel;
    reverseShadow.triangles[0].indices={0,2,1};
    caster.model=&reverseShadow;
    shot("CLIP_SIZE0 rejects reversed signed screen-area triangle",{255,255,255});
    frame.clipSize=10000; // Q=-20000 admits this negative-area source triangle.
    shot("Live CLIP_SIZE uses signed area rather than hardware cull",{128,128,128});
    frame.clipSize=0;
    caster.model=&shadowModel;
    caster.position.y=100;
    shot("Wholly viewport-clipped projected triangle leaves white receiver",{255,255,255});
    caster.position.y=0;
    caster.scale={4,4,1};
    shot("Partially viewport-clipped projected triangle keeps covered center",{128,128,128});
    caster.scale={1,1,1};
    caster.position.z=-4; // Keep actual selected bounds in front of the view before projection.
    camera.position.z=2.7f;
    shot("Projected plane inside camera near distance is clipped",{255,255,255});
    caster.nativeShadow.negativeNormal={.5f,0,-1};
    shot("Source XY slope shifts projected plane beyond near clipping",{128,128,128});
    caster.nativeShadow.negativeNormal.z=0;
    shot("Slope never divides projected plane by stored normal Z zero",{128,128,128});
    caster.nativeShadow.negativeNormal.z=.25f;
    shot("Slope ignores changed nonunit normal Z",{128,128,128});
    camera.position.z=20;
    caster.position.z=0;
    caster.nativeShadow.negativeNormal={0,0,-1};
    overlapShadow=shadowModel;
    overlapShadow.triangles.push_back(overlapShadow.triangles[0]);
    caster.model=&overlapShadow;
    shot("Alpha0 closer duplicate prevents coincident triangles darkening twice",{128,128,128});
    Particle depthProbe;
    depthProbe.position={0,0,2.04f};
    depthProbe.size=4;
    depthProbe.image=green.get();
    frame.particles=std::span(&depthProbe,1);
    shot("Normalized closer duplicate occludes geometry ahead of primary plane",{128,128,128});
    depthProbe.position.z=2.4f;
    shot("Geometry ahead of duplicate remains visible",{0,255,0});
    frame.particles={};
    nearShadow=shadowModel;
    // A slope makes two vertices cross nearClip while the center remains in range.
    caster.model=&nearShadow;
    caster.position.z=-4;
    caster.nativeShadow.negativeNormal={.5f,0,0};
    camera.position.z=3.5f;
    shot("Partially near-clipped projected triangle retains in-range center",{128,128,128});
    camera.position.z=20;
    caster.position.z=0;
    caster.model=&shadowModel;
    caster.nativeShadow.negativeNormal={0,0,-1};
    renderer.purge_model(&shadowModel);
    shot("Purged planar mesh reuploads its real animated geometry",{128,128,128});
    renderer.set_mode(80,64,WindowMode::Windowed);
    rejects([&]{renderer.capture((captures.path/"capture.ppm").string());},
            "Planar capture accepted stale framebuffer after resize");
    shot("Planar caster survives drawable resize",{128,128,128});
    renderer.release_scene();
    shot("Planar caster survives scene GPU release and reupload",{128,128,128});
    // Callback fixtures are isolated from all pre-existing ordinary-draw call counts.
    struct ShadowContext { const DrawInstance* draw{}; unsigned calls{}; bool caster{true}; } shadowContext;
    shadowContext.draw=&caster;
    frame.lightContext=&shadowContext;
    frame.prepareEntityLight=[](void* opaque,const DrawInstance& draw,const Camera&) {
        auto& state=*static_cast<ShadowContext*>(opaque);
        if(&draw!=state.draw)return NativeEntityLightState{};
        ++state.calls;
        return NativeEntityLightState{127,{},{},{state.caster,0,{0,0,-1}}};
    };
    shot("A6 queued caster consumes actual prepared shadow state",{128,128,128});
    if(shadowContext.calls!=1)throw std::runtime_error("A6 shadow preparation count is not one");
    shadowContext.caster=false;
    shot("A6 preparation can revoke already queued caster",{255,255,255});
    if(shadowContext.calls!=2)throw std::runtime_error("A6 revoked shadow was not prepared");
    caster.nativeShadow.caster=false;
    shadowContext.caster=true;
    shot("A6 noncaster does not become queued through invented eager preparation",{255,255,255});
    if(shadowContext.calls!=2)throw std::runtime_error("A6 noncaster prepared before queue selection");
    frame.bonusProfile=true;
    shot("A7 pass4 preparation can admit previously noncaster packet",{128,128,128});
    if(shadowContext.calls!=3)throw std::runtime_error("A7 pass4 shadow preparation count is not one");
    frame.prepareEntityLight=nullptr;
    frame.lightContext=nullptr;
    frame.bonusProfile=false;
    caster.nativeShadow.caster=true;
    frame.shadowStencilRaw=1024;
    rejects([&]{renderer.draw(frame);},"A6 unsupported stencil volume silently rendered planar");
    camera.noShadow=true;
    shot("Unsupported stencil does not reject a NOSHADOW view",{255,255,255});
    camera.noShadow=false;
    frame.bonusProfile=true;
    frame.shadowStencilRaw=-1024;
    shot("A7 negative stencil remains source planar mode",{128,128,128});
    frame.planarCapable=false;
    shot("A7 missing source edition capability suppresses planar pass",{255,255,255});
    frame.planarCapable=true;
    shot("A7 restored source edition capability restores actual shadow",{128,128,128});
    frame.shadowStencilRaw=0;
    frame.bonusProfile=false;

    // Real decoded WMB face: original reversed fan, source flags and raw64 material.
    renderer.release_scene();
    renderer.set_mode(64,64,WindowMode::Windowed);
    frame={};
    camera={};
    camera.position={0,0,20};
    camera.angles={0,-90,0};
    camera.farClip=100;
    camera.perDrawOpacity=true;
    frame.cameras=std::span(&camera,1);
    frame.world=&lightWorld;
    frame.sunDirection={};
    frame.ambient={};
    auto& worldMaterial=lightWorld.mesh.materials.at(0);
    worldMaterial.lightmap=flat({0,0,0,255});
    RenderLight localLight;
    localLight.position={0,0,1};
    localLight.range=20;
    localLight.attenuation={1,0,0};
    std::array<RenderLight,2> orderedLights{localLight,localLight};
    frame.lights=std::span(&localLight,1);
    auto centroidSample=[&](float sampleX=.5f) {
        const auto drawable=renderer.metrics();
        // Independent oracle: corner N.L=1/sqrt(129), center N.L=1,
        // linear fan barycentric weight; 20*tan(37.5)/(4/3)=11.5099048.
        const float x=11.5099048f*(2*(drawable.drawableHeight/2+.5f)/drawable.drawableHeight-1);
        const float y=11.5099048f*(2*(int(drawable.drawableWidth*sampleX)+.5f)-drawable.drawableWidth)/
                      drawable.drawableHeight;
        const float weight=1-std::max(std::abs(x),std::abs(y))/8;
        const int channel=int(255*(.08804509f+(1-.08804509f)*weight)+.5f);
        return std::array<int,3>{channel,channel,channel};
    };
    shot("LIGHTRES0 evaluates localized point light at original outer vertices",{22,22,22});
    frame.lightResRaw=1024;
    shot("LIGHTRES1 evaluates real centroid light and interpolates framebuffer",centroidSample());
    frame.lightResRaw=2048;
    shot("LIGHTRES2 is the same nonzero centroid branch, not quality level two",centroidSample());
    frame.lightResRaw=0;
    shot("LIGHTRES outer interpolation is uniform across this symmetric face",{22,22,22},.75f);
    shot("LIGHTRES toggle returns to cached original outer fan",{22,22,22});
    frame.lightResRaw=1024;
    shot("LIGHTRES centroid illumination falls toward original boundary, not uniform setting echo",
         centroidSample(.75f),.75f);
    renderer.purge_model(&lightWorld.mesh);
    shot("Purged eligible face reuploads coherent centroid and outer ranges",centroidSample());
    frame.lightResRaw=0;
    shot("Reuploaded eligible face still switches to original outer range",{22,22,22});
    orderedLights[1].type=2;
    orderedLights[1].intensity=0; // Changes the topology gate, not illumination.
    frame.lights=orderedLights;
    shot("Source point then spot overwrites LIGHTRES0 gate to centroid",centroidSample());
    std::swap(orderedLights[0],orderedLights[1]);
    shot("Source spot then point overwrites LIGHTRES0 gate back to outer",{22,22,22});
    std::swap(orderedLights[0],orderedLights[1]);
    orderedLights[1].synthetic=true;
    shot("Synthetic spot cannot overwrite original hardware topology gate",{22,22,22});
    orderedLights[1].synthetic=false;
    orderedLights[1].type=3;
    shot("Directional slot leaves preceding point topology gate unchanged",{22,22,22});
    frame.lights={};
    frame.lightResRaw=1024;
    shot("LIGHTRES without relevant lights cannot create illumination",{0,0,0});
    frame.lights=std::span(&localLight,1);
    lightWorld.faces[0].nativeFlags=0;
    renderer.purge_model(&lightWorld.mesh);
    shot("Ineligible source face cannot gain synthetic centroid tessellation",{22,22,22});
    frame.lightResRaw=0;
    shot("Ineligible face remains unchanged with LIGHTRES disabled",{22,22,22});
    lightWorld.faces[0].nativeFlags=0x40000;
    worldMaterial.flags=3; // Source WMB unlit material.
    renderer.purge_model(&lightWorld.mesh);
    shot("Unlit eligible world material bypasses localized vertex illumination",{255,255,255});
    frame.lightResRaw=1024;
    shot("Unlit source material stays unchanged across LIGHTRES toggle",{255,255,255});
    worldMaterial.flags=0;
    renderer.purge_model(&lightWorld.mesh);
    frame.bonusProfile=true;
    shot("A7 hardware LIGHTRES consumes the same qualified face topology",centroidSample());
    // RenderLight.range is already the producer's D3D radius+200, not the radius.
    localLight.position.z=202;
    localLight.range=201;
    shot("Native dynamic light outside transported D3D range stays black",{0,0,0});
    localLight.range=203;
    shot("Native dynamic light just inside transported D3D range illuminates face",{255,255,255});
    localLight.position.z=1;
    localLight.range=20;
    // A secondary WMB entity owns source face metadata independently of frame.world.
    lightWorld.mesh.nativeWorld=&lightWorld;
    DrawInstance secondaryMap;
    secondaryMap.model=&lightWorld.mesh;
    secondaryMap.doubleSided=secondaryMap.noFog=true;
    frame.world=nullptr;
    frame.instances=std::span(&secondaryMap,1);
    renderer.purge_model(&lightWorld.mesh); // Force metadata-driven upload, not primary-world cache reuse.
    shot("Secondary WMB nativeWorld metadata uploads source centroid topology",centroidSample());
    frame.lightResRaw=0;
    shot("Secondary WMB metadata retains original outer LIGHTRES0 range",{22,22,22});
    frame.lightResRaw=1024;
    shot("Secondary WMB cached geometry toggles back to real centroid illumination",centroidSample());
    frame.world=&lightWorld;
    frame.instances={};
    // Native active slots precede serialized enhanced lights. No lightmap is
    // present: red measures native vertex topology, green the baked fragment path.
    frame.bonusProfile=false;
    frame.lightResRaw=0;
    worldMaterial.lightmap.reset();
    // Native unlightmapped surface scalar is signed raw ambient + VIEW ambient +128.
    // -128 cancels that native baseline; zero Frame/Camera RGB is not its input.
    worldMaterial.raw.at(42)=0x80;
    renderer.purge_model(&lightWorld.mesh);
    RenderLight nativeRed=localLight;
    nativeRed.color={1,0,0,1};
    const auto& serialized=lightWorld.lights.at(0);
    RenderLight bakedGreen;
    bakedGreen.position=serialized.position;
    bakedGreen.color=serialized.color;
    bakedGreen.range=serialized.range;
    bakedGreen.intensity=.5f;
    bakedGreen.baked=bakedGreen.synthetic=true;
    bakedGreen.type=2; // Even a baked spot must not enable the ordered native gate.
    const auto drawable=renderer.metrics();
    const float fragmentX=11.5099048f*(2*(drawable.drawableHeight/2+.5f)/drawable.drawableHeight-1);
    const float fragmentY=11.5099048f*(2*(drawable.drawableWidth/2+.5f)-drawable.drawableWidth)/
                          drawable.drawableHeight;
    const float bakedDistance=std::sqrt(1+fragmentX*fragmentX+fragmentY*fragmentY);
    const float bakedFalloff=1-bakedDistance/20;
    const int bakedChannel=int(255*.5f*bakedFalloff*bakedFalloff/bakedDistance+.5f);
    frame.lights=std::span(&nativeRed,1);
    shot("Unlightmapped decoded WMB native-only outer baseline",{22,0,0});
    frame.lights=std::span(&bakedGreen,1);
    shot("Serialized baked-only enhancement has independently computed fragment attenuation",
         {0,bakedChannel,0});
    std::array<RenderLight,2> mixedLights{nativeRed,bakedGreen};
    frame.lights=mixedLights;
    shot("Native-before-baked retains enhancement without letting baked spot enable LIGHTRES0",
         {22,bakedChannel,0});
    frame.lightResRaw=1024;
    shot("Native-before-baked LIGHTRES1 changes only native red vertex interpolation",
         {centroidSample()[0],bakedChannel,0});
    frame.lightResRaw=0;
    RenderLight syntheticSun=bakedGreen;
    syntheticSun.baked=false;
    syntheticSun.type=3;
    syntheticSun.intensity=100;
    mixedLights[1]=syntheticSun;
    shot("Synthetic nonbaked sun is not mistaken for enhanced serialized illumination",{22,0,0});
    std::array<RenderLight,3> nativeSunBaked{nativeRed,syntheticSun,bakedGreen};
    frame.lights=nativeSunBaked;
    shot("Serialized enhancement survives native and distinct synthetic sun prefix",
         {22,bakedChannel,0});
    std::array<RenderLight,33> budgetLights;
    budgetLights.fill(bakedGreen);
    for(auto& light:budgetLights)light.intensity=0;
    budgetLights[0]=nativeRed;
    budgetLights[31]=bakedGreen;
    frame.lights=budgetLights;
    shot("Uploaded light32 contributes after retained native slot and large baked tail",
         {22,bakedChannel,0});
    budgetLights[31].intensity=0;
    budgetLights[32]=bakedGreen;
    shot("Light33 is outside actual upload budget while leading native slot still illuminates",
         {22,0,0});

    // Actual GPU fence queue: capture must neither drain nor submit another frame.
    frame={};
    const auto presentationBaseline=renderer.presentation_state();
    auto queueStateText=[](const PresentationState&state) {
        std::ostringstream text;
        text<<"{limit="<<state.limit<<",pending="<<state.pending<<",submissions="<<state.submissions
            <<",waits="<<state.waits<<",swapInterval="<<state.swapInterval<<",samples="<<state.samples<<"}";
        return text.str();
    };
    auto queuePayload=[&](const char*phase,std::int32_t raw,unsigned limit,unsigned draw,const PresentationState&batch,const PresentationState&previous,const PresentationState&current) {
        const auto metrics=renderer.metrics();std::ostringstream text;
        const auto accepted=current.submissions>=batch.submissions?current.submissions-batch.submissions:0;
        text<<" phase="<<phase<<" backend="<<(backend==RendererBackend::Vulkan?"vulkan":"opengl")
            <<" raw="<<raw<<" expected_limit="<<limit<<" draw="<<draw
            <<" expected_accepted=0..1 expected_retired_max="<<previous.pending
            <<" accepted_delta="<<(current.submissions>=previous.submissions?std::to_string(current.submissions-previous.submissions):"regressed")
            <<" retired_delta="<<(current.waits>=previous.waits?std::to_string(current.waits-previous.waits):"regressed")
            <<" expected_min_waits="<<batch.waits+(accepted>limit?accepted-limit:0)<<" initial="<<queueStateText(presentationBaseline)
            <<" batch="<<queueStateText(batch)<<" previous="<<queueStateText(previous)<<" current="<<queueStateText(current)
            <<" logical="<<metrics.width<<"x"<<metrics.height<<" drawable="<<metrics.drawableWidth<<"x"<<metrics.drawableHeight;
        return text.str();
    };
    auto queueCheck=[&](const char*phase,std::int32_t raw,unsigned limit) {
        frame.tripleBufferRaw=raw;
        const auto before=renderer.presentation_state();
        auto previous=before;
        for(unsigned draw=0;draw<5;++draw) {
            previous=renderer.presentation_state();
            renderer.draw(frame);
            const auto state=renderer.presentation_state();
            if(state.submissions<previous.submissions||state.waits<previous.waits)
                throw std::runtime_error("TRIPLEBUFFER submission/wait counters regressed"+queuePayload(phase,raw,limit,draw,before,previous,state));
            const auto accepted=state.submissions-previous.submissions;
            const auto retired=state.waits-previous.waits;
            // WSI recreation can retire the previous queue; an unavailable drawable may defer admission.
            if(state.limit!=limit||accepted>1||retired>previous.pending||
               state.pending!=previous.pending+accepted-retired||state.pending>limit||
               state.swapInterval!=presentationBaseline.swapInterval||
               state.samples!=presentationBaseline.samples)
                throw std::runtime_error("TRIPLEBUFFER did not bound actual submissions independently of MSAA/VSync"+queuePayload(phase,raw,limit,draw,before,previous,state));
        }
        const auto after=renderer.presentation_state();
        const auto accepted=after.submissions-before.submissions;
        if(accepted==0)
            throw std::runtime_error("TRIPLEBUFFER phase did not submit actual GPU work"+queuePayload(phase,raw,limit,4,before,previous,after));
        if(after.waits<before.waits+(accepted>limit?accepted-limit:0))
            throw std::runtime_error("TRIPLEBUFFER overflow did not perform real successful fence waits"+queuePayload(phase,raw,limit,4,before,previous,after));
        renderer.capture((captures.path/"capture.ppm").string());
        const auto captured=renderer.presentation_state();
        if(captured.pending!=after.pending||captured.submissions!=after.submissions||
           captured.waits!=after.waits)
            throw std::runtime_error("Framebuffer capture changed presentation fence queue");
    };
    auto drained=[&](const char* label) {
        if(renderer.presentation_state().pending!=0)
            throw std::runtime_error(std::string(label)+": GPU admission queue was not drained");
    };
    for(const auto raw:{0,1024,2048}) {
        renderer.release_scene();
        drained("Scene release");
        queueCheck("scene-release",raw,1);
    }
    renderer.set_mode(80,64,WindowMode::Windowed);
    drained("Window resize");
    queueCheck("window-resize",2048,1);
    // SDL mode discovery, not a hard-coded desktop resolution.
    SDL_DisplayMode supportedMode{};
    for(int mode=0;mode<SDL_GetNumDisplayModes(0);++mode) {
        SDL_DisplayMode candidate{};
        if(SDL_GetDisplayMode(0,mode,&candidate)==0&&candidate.w>=64&&candidate.h>=64&&
           (!supportedMode.w||std::int64_t(candidate.w)*candidate.h<
                               std::int64_t(supportedMode.w)*supportedMode.h))
            supportedMode=candidate;
    }
    if(supportedMode.w) {
        renderer.set_mode(supportedMode.w,supportedMode.h,WindowMode::Fullscreen);
        drained("Fullscreen transition");
        for(const auto raw:{0,1024,2048}) {
            renderer.release_scene();
            drained("Fullscreen scene release");
            queueCheck("exclusive-fullscreen",raw,raw==0?1:2);
        }
        renderer.set_mode(64,64,WindowMode::Borderless);
        drained("Borderless transition");
        queueCheck("borderless",2048,1);
    } else std::cout<<"GL queue fullscreen checks skipped: SDL exposes no supported display mode\n";
    renderer.set_mode(64,64,WindowMode::Windowed);
    drained("Return to window");
    queueCheck("return-window",1024,1);
    renderer.purge_model(&lightWorld.mesh);
    drained("Model purge");
    renderer.draw(frame);
    renderer.release_scene();
    drained("Final scene release");
    // The secondary renderer is destroyed with an outstanding real fence.
    {
        Renderer teardownRenderer(options);
        teardownRenderer.draw(frame);
        if(teardownRenderer.presentation_state().pending!=1)
            throw std::runtime_error("Destruction fixture did not leave a real pending submission");
    }
}

void rt_checks() {
    using namespace k22;
    TempDirectory captures;
    auto skin=std::make_shared<Image>();skin->width=skin->height=8;skin->rgba.assign(8*8*4,255);
    auto blockerSkin=std::make_shared<Image>(*skin);
    auto opaqueAlpha=std::make_shared<Image>(*skin);
    for(std::size_t index=3;index<opaqueAlpha->rgba.size();index+=4)opaqueAlpha->rgba[index]=128;
    auto cutout=std::make_shared<Image>(*skin);
    for(std::size_t index=3;index<cutout->rgba.size();index+=4)cutout->rgba[index]=0;
    Model receiver,blocker;
    receiver.formatVersion=blocker.formatVersion=4;
    receiver.vertices={{{10,-4,-4},{-1,0,0},{0,0},{}},{{10,4,-4},{-1,0,0},{1,0},{}},
                       {{10,4,4},{-1,0,0},{1,1},{}},{{10,-4,4},{-1,0,0},{0,1},{}}};
    receiver.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};
    receiver.materials.resize(1);receiver.materials[0].texture=skin;
    blocker.vertices={{{8,-1,1},{-1,0,0},{0,0},{}},{{8,1,1},{-1,0,0},{1,0},{}},
                      {{8,1,3},{-1,0,0},{1,1},{}},{{8,-1,3},{-1,0,0},{0,1},{}}};
    blocker.triangles=receiver.triangles;blocker.materials.resize(1);blocker.materials[0].texture=blockerSkin;
    blocker.frames.resize(2);
    for(const auto&vertex:blocker.vertices){
        blocker.frames[0].positions.push_back(vertex.position);
        auto next=vertex.position;next.y+=8;blocker.frames[1].positions.push_back(next);
        for(auto&pose:blocker.frames)pose.normals.push_back(vertex.normal);
    }
    std::array<DrawInstance,3> actors{};
    actors[0].model=&receiver;actors[0].objectId=100;
    actors[1].model=actors[2].model=&blocker;actors[1].objectId=101;actors[2].objectId=102;
    actors[1].nextFrame=actors[2].nextFrame=1;
    actors[1].overlay=actors[2].overlay=true;
    for(auto&actor:actors)actor.doubleSided=actor.noFog=true;
    Camera camera;camera.nativeAmbient=-127;camera.widescreen=false;camera.noShadow=true;camera.perDrawOpacity=true;
    RenderFrame frame;frame.cameras=std::span(&camera,1);frame.sunDirection={-1,0,1};
    frame.instances=std::span(actors).first(1);
    RenderOptions options;options.backend=RendererBackend::Vulkan;options.rayTracing=true;options.features.shadows=ShadowMode::RayTraced;
    options.width=options.height=64;options.samples=1;options.vsync=false;
    options.title="Kabus 22 hardware ray-query consumer check";
    Renderer renderer(options);
    auto sampleRenderer=[&](Renderer& target) {
        target.draw(frame);
        const auto path=captures.path/"ray.ppm";target.capture(path.string());
        std::ifstream file(path,std::ios::binary);std::string magic;int width{},height{},maximum{};
        file>>magic>>width>>height>>maximum;
        const auto metrics=target.metrics();
        if(!file||magic!="P6"||maximum!=255||width!=metrics.drawableWidth||
           height!=metrics.drawableHeight||file.get()!='\n')throw std::runtime_error("RT capture is not current completed output");
        file.seekg((std::streamoff(height/2)*width+width/2)*3,std::ios::cur);
        std::array<unsigned char,3>pixel{};file.read(reinterpret_cast<char*>(pixel.data()),pixel.size());
        if(!file)throw std::runtime_error("RT capture truncated");
        return int(pixel[0])+int(pixel[1])+int(pixel[2]);
    };
    auto sample=[&]{return sampleRenderer(renderer);};
    const int lit=sample();
    if(lit<450)throw std::runtime_error("RT visibility fixture receiver was not lit");
    frame.instances=std::span(actors).first(2);
    const int shadow=sample();
    if(lit-shadow<120)throw std::runtime_error("Off-center world occluder did not change actual ray-query shading");
    {
        auto rasterOptions=options;rasterOptions.rayTracing=false;
        rasterOptions.title="Kabus 22 Vulkan ray-query raster reference";
        Renderer rasterReference(rasterOptions);
        const int raster=sampleRenderer(rasterReference);
        if(std::abs(raster-lit)>18||raster-shadow<120)
            throw std::runtime_error("Hardware RT-on shading did not differ from actual Vulkan RT-off");
        // A6 lights never occlude: without shadows=rt the ray-traced backend lights exactly like raster.
        auto plainOptions=options;plainOptions.features={};plainOptions.title="Kabus 22 default ray-query lighting";
        Renderer plain(plainOptions);
        if(sampleRenderer(plain)!=raster)throw std::runtime_error("Default ray tracing occluded original unshadowed lighting");
    }
    actors[1].position={-6,0,6};
    if(std::abs(sample()-shadow)>18)throw std::runtime_error("Off-frustum world actor was absent from hardware visibility");
    actors[1].position={};
    std::array<SkinBinding,1> opaqueBinding{{{blockerSkin.get(),opaqueAlpha.get(),false,0,false}}};
    frame.skinBindings=opaqueBinding;
    if(std::abs(sample()-shadow)>18)throw std::runtime_error("Native nonblended bitmap alpha incorrectly transmitted hardware rays");
    frame.skinBindings={};
    auto restored=[&](const char*label){
        if(std::abs(sample()-lit)>18)throw std::runtime_error(std::string(label)+": stale or opaque ray hit");
    };
    actors[1].interpolation=.5f;
    restored("GPU interpolated display pose moves shadow out of ray");
    actors[1].interpolation=1;
    restored("GPU next endpoint moves shadow out of ray");
    actors[2].position.y=-8;actors[2].interpolation=0;
    frame.instances=actors;
    restored("Two independently posed actors sharing one model");
    actors[2].interpolation=1;
    if(lit-sample()<120)throw std::runtime_error("Shared-model second animated actor did not have distinct BLAS pose");
    std::array<SkinBinding,1> skins{{{blockerSkin.get(),cutout.get(),false,0,true}}};
    frame.skinBindings=skins;
    restored("Transparent overlay candidate must not occlude hardware rays");
    frame.skinBindings={};frame.instances=std::span(actors).first(2);
    actors[1].interpolation=0;
    std::vector<Vec3> overridePositions=blocker.frames[1].positions;
    actors[1].positions=overridePositions;actors[1].meshId=1001;actors[1].meshRevision=1;
    restored("Revision-keyed mutable pose reaches actual BLAS");
    overridePositions=blocker.frames[0].positions;actors[1].positions=overridePositions;++actors[1].meshRevision;
    if(lit-sample()<120)throw std::runtime_error("Changed mutable pose revision retained stale hardware visibility");
    frame.instances=std::span(actors).first(1);renderer.purge_model(&blocker);
    restored("Purged actor must leave no hardware ray occluder");
}

void rt_reflection_checks(std::span<const std::byte> mix,std::span<const std::byte> sunMix) {
    using namespace k22;
    TempDirectory captures;
    auto flat=[](std::array<std::uint8_t,4> color,bool nativeAlpha=false) {
        auto image=std::make_shared<Image>();image->width=image->height=16;image->nativeTextureAlpha=nativeAlpha;
        for(unsigned i=0;i<256;++i)image->rgba.insert(image->rgba.end(),color.begin(),color.end());
        return image;
    };
    auto black=flat({0,0,0,255}),blue=flat({0,0,255,255}),green=flat({0,255,0,255});
    auto redHalf=flat({255,0,0,128},true),greenHalf=flat({0,255,0,128},true);
    auto redOpaque=flat({255,0,0,255}),redOpaqueTarget=flat({255,0,0,128}),cutout=flat({255,0,0,0},true);
    auto pattern=std::make_shared<Image>();pattern->width=pattern->height=16;pattern->sourceDepth=16;pattern->nativeStorageBytes=2;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
        pattern->rgba.insert(pattern->rgba.end(),{std::uint8_t(x*17),std::uint8_t(y*17),std::uint8_t((x^y)*17),255});
    auto plane=[](Model&model,std::shared_ptr<const Image>image){
        model.formatVersion=4;
        model.vertices={{{0,-4,-4},{-1,0,0},{0,0},{}},{{0,4,-4},{-1,0,0},{1,0},{}},
                        {{0,4,4},{-1,0,0},{1,1},{}},{{0,-4,4},{-1,0,0},{0,1},{}}};
        model.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};model.materials.resize(1);model.materials[0].texture=std::move(image);
    };
    World mirror;Model front,middle,rear,turbulent;
    plane(mirror.mesh,black);mirror.mesh.formatVersion=6;
    for(auto&vertex:mirror.mesh.vertices)vertex.position.x=10;
    mirror.mesh.materials[0].flags=0x2000|3;mirror.mesh.materials[0].raw.resize(64);mirror.mesh.materials[0].raw[43]=100;
    plane(front,redHalf);plane(middle,greenHalf);plane(rear,blue);plane(turbulent,pattern);
    turbulent.formatVersion=6;turbulent.materials[0].flags=9;turbulent.materials[0].raw.resize(64);
    for(auto&vertex:turbulent.vertices){vertex.position.x=10;vertex.position.y=6+vertex.position.y*.25f;vertex.position.z*=.25f;}
    std::array<DrawInstance,3> layers{};
    layers[0].model=&front;layers[0].position.x=-5;layers[0].objectId=201;
    layers[1].model=&middle;layers[1].position.x=-7.5f;layers[1].objectId=202;
    layers[2].model=&rear;layers[2].position.x=-10;layers[2].objectId=203;
    for(auto&actor:layers)actor.unlit=actor.noFog=actor.doubleSided=true;
    Camera camera;camera.widescreen=false;camera.perDrawOpacity=true;camera.noShadow=camera.noCull=true;
    RenderFrame frame;frame.cameras=std::span(&camera,1);frame.world=&mirror;frame.sunDirection={};
    frame.instances=std::span(layers).last(1);frame.mipmapping=0;
    RenderOptions options;options.backend=RendererBackend::Vulkan;options.rayTracing=true;
    options.width=options.height=64;options.samples=1;options.vsync=false;options.title="Kabus 22 hardware reflected alpha consumer";
    Renderer renderer(options);
    auto sample=[&]{return capture_pixel(captured_pixels(renderer,frame,captures.path/"reflection.ppm"),renderer.metrics());};
    auto expect=[&](const char*label,std::array<int,3>expected){
        const auto actual=sample();
        for(unsigned i=0;i<3;++i)if(std::abs(actual[i]-expected[i])>4)
            throw std::runtime_error(std::string(label)+": RGB"+std::to_string(actual[0])+","+std::to_string(actual[1])+","+std::to_string(actual[2]));
    };
    expect("Opaque real geometry behind camera is reflected",{0,0,254});
    {
        auto rasterOptions=options;rasterOptions.rayTracing=false;Renderer raster(rasterOptions);
        const auto pixel=capture_pixel(captured_pixels(raster,frame,captures.path/"reflection-raster.ppm"),raster.metrics());
        if(pixel!=std::array<int,3>{0,0,0})throw std::runtime_error("Reflection fixture raster-only fallback is not black");
    }
    {
        // The signal path (roughness 0 without packs) reflects the same unlit original mirror (type 3) as the inline path.
        auto roughOptions=options;roughOptions.features.roughReflections=true;roughOptions.title="Kabus 22 rough reflection signal consumer";
        Renderer rough(roughOptions);
        const auto pixel=capture_pixel(captured_pixels(rough,frame,captures.path/"reflection-rough.ppm"),rough.metrics());
        if(pixel[0]>4||pixel[1]>4||std::abs(pixel[2]-254)>4)throw std::runtime_error("reflections=rough skipped an unlit original mirror");
    }
    ScriptMaterialState reflectedMaterial;
    reflectedMaterial.sourceId=401;
    reflectedMaterial.albedoRaw=102400;
    frame.materials=std::span(&reflectedMaterial,1);
    layers[2].materialIndex=0;
    rear.materials[0].texture=flat({255,255,255,255});
    {
        struct ReflectedLightContext {
            NativeEntityLightState returned{255,{},{}};
            const Camera* admitted{};
            const Camera* forbidden{};
            unsigned admittedCalls{},forbiddenCalls{};
        } context;
        const auto savedRear=layers[2];
        layers[2].unlit=false;layers[2].nativeFloorLight=true;layers[2].nativeLightScalar=0;
        layers[2].ambient=0;layers[2].nativeLightDelta={};layers[2].nativeLightBias={};
        reflectedMaterial.ambient={80,80,80,1};
        std::array<Camera,2> views{camera,camera};
        views[0].position={-20,0,0};views[0].drawWorld=false;
        context.admitted=&views[0];context.forbidden=&views[1];
        frame.cameras=views;frame.lightContext=&context;
        frame.prepareEntityLight=[](void* opaque,const DrawInstance& draw,const Camera& view){
            auto& state=*static_cast<ReflectedLightContext*>(opaque);
            if(draw.objectId!=203)return NativeEntityLightState{draw.nativeLightScalar,draw.nativeLightDelta,draw.nativeLightBias};
            if(&view==state.admitted)++state.admittedCalls;
            else if(&view==state.forbidden)++state.forbiddenCalls;
            else throw std::runtime_error("Reflected actor was prepared by an unrelated camera");
            return state.returned;
        };
        expect("Later camera reflection consumes admitted scalar255 rather than packet scalar0",{160,160,160});
        if(context.admittedCalls!=1||context.forbiddenCalls||layers[2].nativeLightScalar!=0)
            throw std::runtime_error("Reflected light preparation duplicated callbacks, warmed an off-frustum actor or changed its packet");
        context.returned={63,{0,4,12},{0,6,25}};context.admittedCalls=context.forbiddenCalls=0;
        expect("Later reflection consumes complete current scalar and BGR delta/bias DTO",{63,46,40});
        if(context.admittedCalls!=1||context.forbiddenCalls)
            throw std::runtime_error("Changed reflected DTO added speculative off-frustum preparation");
        frame.cameras=std::span(&camera,1);context.admitted=&camera;context.forbidden=nullptr;
        context.admittedCalls=context.forbiddenCalls=0;
        expect("A new frame without an admitted actor uses its packet, not last frame's DTO",{0,0,0});
        if(context.admittedCalls||context.forbiddenCalls)
            throw std::runtime_error("Off-frustum ray candidate consumed an entity-light callback");
        // The center +X ray reflects along -Y. The actor is visible at 45 degrees,
        // admitted after the mirror, but does not cover the center mirror pixel.
        constexpr float diagonal=.7071067811865475f;
        for(auto& vertex:mirror.mesh.vertices){
            vertex.position={10-vertex.position.y*diagonal,vertex.position.y*diagonal,vertex.position.z};
            vertex.normal={-diagonal,-diagonal,0};
        }
        renderer.purge_model(&mirror.mesh);
        layers[2].position={10,-10,0};layers[2].angles.x=-90;
        const auto oldFov=camera.horizontalFov;camera.horizontalFov=120;
        context.returned={255,{},{}};context.admittedCalls=context.forbiddenCalls=0;
        expect("Same-camera reflection uses its later admitted actor's prepared DTO",{160,160,160});
        if(context.admittedCalls!=1||context.forbiddenCalls)
            throw std::runtime_error("Same-camera preparation and replay called the actor provider more than once");
        camera.horizontalFov=oldFov;
        for(auto& vertex:mirror.mesh.vertices){
            vertex.position={10,vertex.position.y/diagonal,vertex.position.z};
            vertex.normal={-1,0,0};
        }
        renderer.purge_model(&mirror.mesh);
        layers[2]=savedRear;reflectedMaterial.ambient={};
        frame.lightContext=nullptr;frame.prepareEntityLight=nullptr;
    }
    reflectedMaterial.emissive={40,20,10,1};
    expect("Reflected ordinary MATERIAL retains prepared emissive and MODULATE2X",{80,40,20});
    rear.materials[0].texture=black;
    reflectedMaterial.emissive={};
    reflectedMaterial.specular={255,255,255,1};
    reflectedMaterial.power=4;
    for(auto& vertex:rear.vertices)vertex.normal={1,0,0};
    renderer.purge_model(&rear);
    RenderLight shineLight;
    shineLight.type=3;shineLight.direction={-1,0,0};shineLight.color={.25f,.25f,.25f,1};
    frame.lights=std::span(&shineLight,1);
    expect("Reflected ordinary specular adds white after black diffuse texture",{64,64,64});
    for(unsigned i=1;i<rear.vertices.size();++i)rear.vertices[i].normal={0,1,0};
    renderer.purge_model(&rear);
    expect("Reflected ordinary interpolates vertex specular rather than hit-normal power4",{32,32,32});
    for(auto& vertex:rear.vertices)vertex.normal={1,0,0};
    renderer.purge_model(&rear);
    shineLight.direction={-.5f,-.8660254038f,0};
    expect("Reflected ordinary LOCALVIEWER0 uses halfway-vector power4",{36,36,36});
    shineLight.direction={-1,0,0};shineLight.type=2;shineLight.position={100,0,0};
    shineLight.range=1000;shineLight.attenuation={1,0,0};
    expect("Reflected ordinary admitted spotlight contributes specular",{64,64,64});
    shineLight.type=1;
    expect("Reflected ordinary point light contributes specular with sun disabled",{64,64,64});
    frame.lights={};
    for(auto& vertex:rear.vertices)vertex.normal={-1,0,0};
    renderer.purge_model(&rear);
    reflectedMaterial.effect=Renderer::identify_effect("Normal_Mix.fx",mix);
    reflectedMaterial.effectGeneration=1;
    reflectedMaterial.specular={};
    rear.materials[0].texture=flat({128,64,32,255});
    rear.materials.resize(2);rear.materials[1].texture=flat({128,128,0,255});
    frame.sunDirection={-1,0,0};
    auto reflectedSkills=[&](std::array<float,8> values){for(unsigned i=0;i<8;++i)reflectedMaterial.skillBits[i]=std::bit_cast<std::uint32_t>(values[i]);};
    reflectedSkills({0,0,.5f,1,0,0,0,0});
    expect("Reflected literal Normal_Mix nonzero specular texture-modulates source RGB",{64,32,16});
    reflectedMaterial.effect=Renderer::identify_effect("Normal_SunMix.fx",sunMix);
    ++reflectedMaterial.effectGeneration;
    expect("Reflected literal Normal_SunMix retains nonzero authored specular",{64,32,16});
    rear.materials[0].texture=flat({128,64,32,255},true);
    expect("Reflected literal FX full RGBA specular .5 composites alpha .5",{32,16,8});
    frame.materials={};frame.sunDirection={};layers[2].materialIndex=~0u;
    rear.materials.resize(1);rear.materials[0].texture=blue;
    frame.instances=layers;
    expect("Two translucent reflected layers reveal actual opaque rear",{128,64,63});
    std::array<DrawInstance,2> pair{layers[0],layers[2]};frame.instances=pair;
    expect("Translucent reflected foreground composites actual rear geometry",{128,0,127});
    std::array<SkinBinding,1> binding{{{blue.get(),green.get(),false,0,false}}};frame.skinBindings=binding;
    expect("Changing actual rear geometry changes transmitted reflection",{128,127,0});
    binding[0]={redHalf.get(),redOpaque.get(),false,0,false};
    expect("Opaque reflected foreground stops traversal",{254,0,0});
    binding[0]={redHalf.get(),redOpaqueTarget.get(),false,0,false};
    expect("Nonblended native alpha reflects an opaque surface",{254,0,0});
    binding[0]={redHalf.get(),cutout.get(),false,0,true};pair[0].overlay=true;
    expect("Cutout reflected foreground reveals actual rear",{0,0,254});
    frame.skinBindings={};pair[0].overlay=false;pair[0].additive=true;
    expect("Native additive reflected layer does not consume rear visibility",{128,0,254});
    std::array<DrawInstance,3> ordered{};
    ordered[0].model=&turbulent;ordered[0].objectId=204;
    ordered[1].model=&mirror.mesh;ordered[2]=layers[2];
    for(auto&actor:ordered)actor.unlit=actor.noFog=actor.doubleSided=true;
    frame.world=nullptr;binding[0]={blue.get(),pattern.get(),false,0,false};frame.skinBindings=binding;
    frame.turbulenceRange=6;frame.turbulenceSpeed=1;frame.animationTimeSeconds=.375;
    frame.instances=std::span(ordered).subspan(1);
    renderer.release_scene();const auto original=sample();
    frame.instances=ordered;const auto fresh=sample();
    frame.instances=std::span(ordered).subspan(1);const auto retained=sample();
    if(fresh==original)throw std::runtime_error("Admitted same-camera warp did not reach actual reflected material");
    for(unsigned i=0;i<3;++i)if(std::abs(fresh[i]-retained[i])>4)
        throw std::runtime_error("Hardware reflection sampled stale pre-warp descriptors instead of current display texture");
}
}

// Asset-free Contract D/E policy: presets, CLI extraction, fallback, tangents, tone curve.
void graphics_policy_checks() {
    using namespace k22;using namespace k22::render_policy;
    auto require=[](bool ok,const char*what){if(!ok)throw std::runtime_error(std::string("graphics policy: ")+what);};
    texture_pack_checks();
    {Image tga32,other;tga32.a6AlphaFormat=true;DrawInstance overlay;overlay.overlay=true;DrawInstance plain;
     require(overlay_keyed(overlay,&other)&&overlay_keyed(overlay,nullptr)&&!overlay_keyed(overlay,&tga32)&&!overlay_keyed(plain,&other),"A6 OVERLAY key skips only BMAP0x100 bitmaps");}
    auto none=resolve_graphics("original",{});
    require(none.backend=="opengl"&&!none.pbr&&!none.texturePacks&&!none.anisotropy&&!none.msaa,"original preset is the legacy default");
    auto custom=resolve_graphics("custom",{});require(custom.backend=="opengl"&&!custom.pbr&&!custom.anisotropy,"custom base is original");
    auto enhanced=resolve_graphics("enhanced",{});require(enhanced.backend=="vulkan-raster"&&enhanced.anisotropy==16&&enhanced.msaa==4&&enhanced.texturePacks&&!enhanced.pbr,"enhanced preset");
    auto ultra=resolve_graphics("ultra",{});require(ultra.backend=="vulkan-rt"&&ultra.pbr&&ultra.texturePacks&&ultra.anisotropy==16,"ultra preset");
    const std::vector<std::pair<std::string,std::string>> overrides{{"pbr","1"},{"msaa","8"},{"anisotropy","2"},{"texture_packs","0"},{"backend","vulkan-raster"}};
    auto o=resolve_graphics("ultra",overrides);require(o.pbr&&o.msaa==8&&o.anisotropy==2&&!o.texturePacks&&o.explicitPacks&&o.backend=="vulkan-raster","--graphics overrides preset");
    for(auto bad:{std::pair<std::string,std::string>{"blur","1"},{"pbr","2"},{"msaa","3"},{"anisotropy","32"},{"backend","d3d9"},{"shadows","on"},{"gi","2"},{"reflections","1"},{"taa","yes"}}){const std::vector<std::pair<std::string,std::string>>one{bad};rejects([&]{resolve_graphics("custom",one);},"unknown --graphics key/value accepted");}
    // Wave-2 features: off by default, preset contents, explicit overrides, per-backend admission with notices.
    require(none.features==GraphicsFeatures{}&&custom.features==GraphicsFeatures{},"original/custom enable no wave-2 effect");
    {auto&f=enhanced.features;require(f.shadows==ShadowMode::ShadowMap&&f.ssao&&f.fxaa&&!f.gi&&!f.roughReflections&&!f.denoise&&!f.bloom&&!f.taa&&!f.fog&&f.smoothNormals&&f.tessellation&&f.upscale&&f.sharpen,"enhanced = shadow maps + SSAO + FXAA + Vulkan upgrades");}
    {auto&f=ultra.features;require(f.shadows==ShadowMode::RayTraced&&f.gi&&f.roughReflections&&f.denoise&&f.bloom&&f.taa&&f.fog&&!f.ssao&&!f.fxaa&&f.smoothNormals&&f.tessellation&&f.upscale&&f.sharpen,"ultra = RT shadows/GI/reflections + denoise + bloom + TAA + volumetric fog + Vulkan upgrades");}
    {const std::vector<std::pair<std::string,std::string>>set{{"shadows","rt"},{"gi","1"},{"reflections","rough"},{"bloom","1"},{"volumetric_fog","1"},{"shadows","shadowmap"},{"ssao","1"},{"fxaa","1"},{"taa","1"},{"denoise","1"}};auto f=resolve_graphics("custom",set).features;
     require(f.shadows==ShadowMode::ShadowMap&&f.gi&&f.roughReflections&&f.bloom&&f.fog&&f.ssao&&f.fxaa&&f.taa&&f.denoise,"later --graphics keys win");
     auto g=resolve_graphics("ultra",std::vector<std::pair<std::string,std::string>>{{"gi","0"},{"shadows","original"},{"taa","0"}}).features;require(!g.gi&&g.shadows==ShadowMode::Original&&!g.taa&&g.bloom,"overrides disable preset effects");}
    {std::vector<std::string>notes;auto f=admit_features(ultra.features,"opengl",notes);
     require(f.shadows==ShadowMode::ShadowMap&&!f.gi&&!f.roughReflections&&!f.denoise&&!f.taa&&f.bloom&&f.fog&&!f.smoothNormals&&!f.tessellation&&!f.upscale&&!f.sharpen&&notes.size()==9,"OpenGL admission: RT shadows fall back to shadow maps, RT/TAA/Vulkan upgrades dropped with notices");
     notes.clear();f=admit_features(ultra.features,"vulkan-raster",notes);require(f.shadows==ShadowMode::ShadowMap&&f.taa&&!f.gi&&notes.size()==4,"raster admission keeps TAA");
     notes.clear();require(admit_features(ultra.features,"vulkan-rt",notes)==ultra.features&&notes.empty(),"RT admits everything silently");
     notes.clear();f=admit_features(enhanced.features,"opengl",notes);require(f.ssao&&f.fxaa&&f.shadows==ShadowMode::ShadowMap&&!f.smoothNormals&&notes.size()==4,"enhanced effects run on every backend; Vulkan upgrades drop on OpenGL");}
    for(auto&option:graphicsOptions){const bool rtOnly=option.key=="gi"||option.key=="reflections"||option.key=="denoise";const bool vulkanOnly=option.key=="taa"||option.key=="smooth_normals"||option.key=="tessellation"||option.key=="texture_upscale"||option.key=="sharpen";require(option.backends==(rtOnly?backendRT:vulkanOnly?backendVulkan:backendAll),"capability backends match implementations");}
    require(!composite_enabled(false,{})&&composite_enabled(true,{})&&composite_enabled(false,{.bloom=true})&&composite_enabled(false,{.taa=true})&&!composite_enabled(false,{.ssao=true}),"composite only when an effect needs it");
    require(!shadow_maps_enabled({})&&shadow_maps_enabled({.shadows=ShadowMode::ShadowMap})&&!shadow_maps_enabled({.shadows=ShadowMode::RayTraced})&&!shadow_maps_enabled({.shadows=ShadowMode::RayTraced,.fog=true}),"shadow maps only for shadows=shadowmap (RT fog does not render them)");
    {Camera c;c.position={100,-50,30};c.angles={30,-10,0};c.nearClip=1;c.farClip=8000;const float aspect=16/9.f;auto vp=camera_matrix(c,aspect);auto iv=inverse(vp);auto id=product(vp,iv);
     for(int i=0;i<16;++i)require(std::abs(id[i]-(i%5==0?1.f:0.f))<1e-3f,"matrix inverse");
     Bounds world{{-3000,-3000,-500},{3000,3000,800},false,true};auto sun=sun_shadow_matrix(c,aspect,unit({0.3f,0.2f,1}),world,sunShadowDistance,sunShadowResolution);
     Camera slice=c;slice.farClip=sunShadowDistance;auto sliceInverse=inverse(camera_matrix(slice,aspect));
     for(int i=0;i<8;++i){auto p=project_ndc(sun,project_ndc(sliceInverse,{i&1?1.f:-1.f,i&2?1.f:-1.f,i&4?1.f:-1.f}));require(std::abs(p.x)<=1&&std::abs(p.y)<=1&&std::abs(p.z)<=1,"sun shadow covers the camera slice");}
     for(int i=0;i<8;++i){auto p=project_ndc(sun,{i&1?3000.f:-3000.f,i&2?3000.f:-3000.f,i&4?800.f:-500.f});require(p.z<=1,"sun shadow depth range keeps every caster toward the sun");}
     c.position={100.4f,-50,30};auto moved=sun_shadow_matrix(c,aspect,unit({0.3f,0.2f,1}),world,sunShadowDistance,sunShadowResolution);
     auto a=project_ndc(sun,{0,0,0}),b=project_ndc(moved,{0,0,0});float texels=std::abs(a.x-b.x)*sunShadowResolution/2;require(std::abs(texels-std::round(texels))<1e-2f,"sun shadow snaps to whole texels");}
    {Vec3 l{10,20,30};const std::array<Vec3,6>dirs{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
     for(int face=0;face<6;++face){auto p=project_ndc(cube_face_matrix(l,face,1,500),add(l,mul(dirs[face],100)));require(std::abs(p.x)<1e-4f&&std::abs(p.y)<1e-4f&&p.z>-1&&p.z<1,"cube face looks down its axis");}
     auto up=project_ndc(cube_face_matrix(l,0,1,500),add(l,Vec3{100,50,0}));require(up.y<0,"GL cube +X face has world +Y at texel row 0 (NDC -Y)");}
    {std::vector<RenderLight>lights(5);lights[0].baked=true;lights[1].synthetic=true;lights[2].type=3;lights[3].position={1000,0,0};lights[4].position={50,0,0};lights[4].range=200;
     require(shadow_light_index(lights,{0,0,0})==4,"cube shadow follows the strongest nearby dynamic light");lights.resize(3);require(shadow_light_index(lights,{0,0,0})==-1,"no eligible shadow light");}
    {std::array<Vec2,8>seen{};for(int i=0;i<8;++i){seen[i]=taa_jitter(i);require(std::abs(seen[i].x)<=.5f&&std::abs(seen[i].y)<=.5f,"jitter within one pixel");for(int j=0;j<i;++j)require(seen[i].x!=seen[j].x||seen[i].y!=seen[j].y,"8 distinct jitter offsets");}
     require(taa_jitter(8).x==seen[0].x&&std::abs(halton(1,2)-.5f)<1e-6f&&std::abs(halton(2,3)-2/3.f)<1e-6f&&std::abs(halton(4,3)-4/9.f)<1e-6f,"Halton(2,3) sequence");
     Camera c;auto vp=camera_matrix(c,16/9.f);auto j=jittered(vp,{.25f,-.5f},1920,1080);Vec3 p{200,30,10};auto a=project_ndc(vp,p),b=project_ndc(j,p);require(std::abs(b.x-a.x-.5f/1920)<1e-5f&&std::abs(b.y-a.y+1.f/1080)<1e-5f&&std::abs(b.z-a.z)<1e-6f,"jitter moves clip XY by whole sub-pixel offsets only");}
    require(temporal_weight(0,.1f)==1&&temporal_weight(3,.1f)==.25f&&temporal_weight(100,.1f)==.1f,"temporal blend ramps 1/(n+1) to its floor");
    require(history_valid(100,103,.95f)&&!history_valid(100,110,.95f)&&!history_valid(100,100,.5f)&&!history_valid(0,100,1),"history rejects disocclusion");
    {float sum=0;for(int i=-2;i<=2;++i)sum+=atrous_tap(i);require(std::abs(sum-1)<1e-6f&&atrous_tap(0)==6/16.f,"a-trous B3 kernel normalized");}
    rejects([&]{resolve_graphics("max",{});},"unknown preset accepted");
    require(fallback_backend("vulkan-rt")=="vulkan-raster"&&fallback_backend("vulkan-raster")=="opengl"&&fallback_backend("opengl").empty(),"preset backend degradation chain");
    auto argv_of=[](std::vector<std::string>&storage){std::vector<char*>argv;for(auto&s:storage)argv.push_back(s.data());argv.push_back(nullptr);return argv;};
    {std::vector<std::string>args{"kabus22","--data","x","--preset","ultra","--graphics","msaa=2","--texture-pack","hd","--frames","3"};auto argv=argv_of(args);int argc=int(args.size());
     auto r=graphics_cli::extract(argc,argv.data());require(argc==5&&std::string_view(argv[3])=="--frames"&&!argv[5]&&r.active&&r.preset=="ultra"&&r.packs==std::vector<std::string>{"hd"}&&!r.rendererFlag,"extract removes graphics options");}
    {std::vector<std::string>args{"kabus22","--renderer","vulkan","--graphics","backend=opengl"};auto argv=argv_of(args);int argc=int(args.size());rejects([&]{graphics_cli::extract(argc,argv.data());},"--renderer with --graphics backend accepted");}
    {std::vector<std::string>args{"kabus22","--texture-pack","../x"};auto argv=argv_of(args);int argc=int(args.size());rejects([&]{graphics_cli::extract(argc,argv.data());},"pack path accepted as name");}
    TempDirectory xdg;const char*previous=std::getenv("XDG_DATA_HOME");std::string saved=previous?previous:"";::setenv("XDG_DATA_HOME",xdg.path.c_str(),1);
    {RenderOptions defaults,options;graphics_cli::Request r;graphics_cli::apply(options,r);
     require(options.backend==defaults.backend&&options.rayTracing==defaults.rayTracing&&options.samples==defaults.samples&&options.anisotropy==defaults.anisotropy&&!options.pbr&&!options.forceAnisotropy&&!options.packs,"no graphics flags keeps RenderOptions exactly");}
    {RenderOptions options;graphics_cli::Request r;r.active=true;r.preset="ultra";graphics_cli::apply(options,r);
     require(options.backend==RendererBackend::Vulkan&&options.rayTracing&&options.pbr&&options.forceAnisotropy&&options.anisotropy==16&&options.samples==4&&r.presetBackend&&!options.packs,"ultra applies backend/PBR and auto-disables absent packs");}
    {RenderOptions options;graphics_cli::Request r;r.active=true;r.preset="ultra";r.rendererFlag=true;graphics_cli::apply(options,r);
     require(options.backend==RendererBackend::OpenGL&&!r.presetBackend&&options.pbr,"explicit --renderer overrides preset backend");}
    {RenderOptions options;graphics_cli::Request r;r.active=true;r.packs={"missing"};rejects([&]{graphics_cli::apply(options,r);},"missing named pack accepted");}
    {RenderOptions options;graphics_cli::Request r;r.active=true;r.packs={"x"};r.overrides={{"texture_packs","0"}};rejects([&]{graphics_cli::apply(options,r);},"--texture-pack with texture_packs=0 accepted");}
    if(previous)::setenv("XDG_DATA_HOME",saved.c_str(),1);else ::unsetenv("XDG_DATA_HOME");
    // Tangents follow +U; UV-degenerate triangles keep zero (no invented basis).
    Model quad;quad.vertices={{{0,0,0},{0,0,1},{0,0},{}},{{2,0,0},{0,0,1},{1,0},{}},{{2,2,0},{0,0,1},{1,1},{}},{{0,2,0},{0,0,1},{0,1},{}}};quad.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};
    std::vector<Vec3>tangents(4);derive_tangents(quad,{},{},tangents);
    for(auto t:tangents)require(std::abs(t.x-1)<1e-6f&&std::abs(t.y)<1e-6f&&std::abs(t.z)<1e-6f,"tangent follows +U");
    Model flat=quad;for(auto&v:flat.vertices)v.uv={0.5f,0.5f};derive_tangents(flat,{},{},tangents);
    for(auto t:tangents)require(t.x==0&&t.y==0&&t.z==0,"UV-degenerate tangent stays zero");
    // Tone curve: identity through the knee (dark scenes unchanged), strictly monotonic, bounded, exact 8-bit inverse.
    float last=-1;for(int i=0;i<=4000;++i){float x=i/1000.f,y=tone_map(x);require(y>last&&y<1,"tone map monotonic and bounded");last=y;}
    for(int v=0;v<=202;++v)require(std::abs(tone_map(v/255.f)-v/255.f)<1e-6f,"tone map preserves values below the knee");
    require(tone_map(1)<1&&tone_map(1)>0.85f&&tone_map(100)<1,"tone map compresses highlights");
    for(int v=0;v<=255;++v)require(std::lround(tone_map(inverse_tone_map(v/255.f))*255)==v,"inverse tone map round-trips 8-bit seeds");
    require(legacy_roughness(0)==0.8f&&legacy_roughness(1)>legacy_roughness(16)&&legacy_roughness(16)>legacy_roughness(256)&&legacy_roughness(256)>=0.05f,"legacy roughness from specular power");
}
int main(int argc,char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--policy"){graphics_policy_checks();std::cout<<"Graphics preset/pack/tangent/tone-map policy checks passed\n";return 0;}
    if(argc!=2&&argc!=3)throw std::runtime_error("Usage: k22-render-check <recovered FX directory> [--gl|--vulkan|--vulkan-rt] | --policy");
    const auto mode=argc==3?std::string_view(argv[2]):std::string_view{};
    if(!mode.empty()&&mode!="--gl"&&mode!="--vulkan"&&mode!="--vulkan-rt")
        throw std::runtime_error("Unknown render check option: "+std::string(mode));
    k22::Assets effects(argv[1]);
    auto mix=effects.read("Normal_Mix.fx");
    auto sunMix=effects.read("Normal_SunMix.fx");
    auto bytes=[](const auto& data){return std::as_bytes(std::span(data));};
    using k22::Renderer;
    using k22::EffectKind;
    if(Renderer::identify_effect("Normal_Mix.fx",bytes(mix))!=EffectKind::NormalMix ||
       Renderer::identify_effect("Normal_SunMix.fx",bytes(sunMix))!=EffectKind::NormalSunMix ||
       Renderer::identify_effect("nOrMaL_mIx.FX",bytes(mix))!=EffectKind::NormalMix)
        throw std::runtime_error("Canonical recovered effect classification failed");
    rejects([&]{Renderer::identify_effect("Normal_Mix.fx",{});});
    rejects([&]{Renderer::identify_effect("Normal_Mix.fx",bytes(sunMix));});
    rejects([&]{Renderer::identify_effect("Normal_SunMix.fx",bytes(mix));});
    for(auto name:{"Other.fx","path/Normal_Mix.fx","Normal_Mix.fx.bak","Normal_Mix.fx "})
        rejects([&]{Renderer::identify_effect(name,bytes(mix));});
    rejects([&]{Renderer::identify_effect(std::string_view("Normal_Mix.fx\0",14),bytes(mix));});
    for(auto* source:{&mix,&sunMix}) {
        const auto name=source==&mix?"Normal_Mix.fx":"Normal_SunMix.fx";
        for(auto index:{std::size_t{0},source->size()/2,source->size()-1}) {
            (*source)[index]^=1;
            rejects([&]{Renderer::identify_effect(name,bytes(*source));});
            (*source)[index]^=1;
        }
        rejects([&]{Renderer::identify_effect(name,bytes(*source).first(source->size()-1));});
        source->push_back(0);
        rejects([&]{Renderer::identify_effect(name,bytes(*source));});
        source->pop_back();
    }
    Renderer::boundary_check();
    std::cout<<"Renderer material/effect boundary checks passed\n";
    if(argc==3) {
        if(mode=="--vulkan-rt"){
            gl_checks(bytes(mix),bytes(sunMix),k22::RendererBackend::Vulkan,true);
            rt_checks();rt_reflection_checks(bytes(mix),bytes(sunMix));overlay_key_checks(k22::RendererBackend::Vulkan,true);
            std::cout<<"Actual Vulkan hardware ray-query visibility/animation/alpha/reflection/turbulence consumer checks passed\n";
        }else{
            const auto backend=mode=="--vulkan"?k22::RendererBackend::Vulkan:k22::RendererBackend::OpenGL;
            gl_checks(bytes(mix),bytes(sunMix),backend);turbulence_checks(backend);overlay_key_checks(backend,false);
            std::cout<<"Actual "<<(mode=="--vulkan"?"Vulkan raster":"GL")<<" material/effect/opacity/particle/turbulence consumer checks passed\n";
        }
    }
}
