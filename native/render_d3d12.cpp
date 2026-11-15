// Native Direct3D 12 backend: a port of the OpenGL backend (render_gl.cpp) that renders the same image.
// Conventions shared with native/shaders/d3d12/common.hlsli: every texture and render target keeps OpenGL's memory layout
// (row 0 is the bottom row of the GL image), so texture coordinates, gl_FragCoord and GL viewport/scissor rectangles are
// used unchanged; vertex shaders negate clip-space y (front faces flip accordingly) and map GL clip z [-w,w] to [0,w]. The
// final pass into the swap chain (presentVS) and capture skip the flip, giving the top-down image GL's glReadPixels+flip did.
// The OpenGL state machine the port follows (bound program, blend/depth/stencil/cull state, texture units per target,
// sampler objects, framebuffer, viewport/scissor, glClear honouring scissor and write masks) is emulated by `GL` below; a
// draw resolves it into a cached pipeline state, ring-allocated constants and a descriptor table.
#include "render_d3d12.hpp"
#include "platform.hpp"
#include "render_policy.hpp"
#include "texture_pack.hpp"
#include "render_d3d12_shaders.hpp"
#include <SDL_syswm.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace k22 {
namespace {
using namespace render_policy;
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char*what){
    if(SUCCEEDED(hr))return;
    char code[16];std::snprintf(code,sizeof code,"0x%08lx",static_cast<unsigned long>(hr));
    throw std::runtime_error(std::string("Direct3D 12: ")+what+" failed ("+code+")");
}
std::string narrow(const wchar_t*text){std::string out;for(;*text;++text)out+=*text<128?char(*text):'?';return out;}
constexpr D3D12_RESOURCE_STATES bufferRead=D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER|D3D12_RESOURCE_STATE_INDEX_BUFFER|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES shaderRead=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES depthRead=D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr UINT allSubresources=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
struct F2{float x{},y{};};struct F3{float x{},y{},z{};};struct F4{float x{},y{},z{},w{};};
F3 f3(Vec3 v){return {v.x,v.y,v.z};}F3 f3(Color c){return {c.r,c.g,c.b};}

// ---- Persistent CPU-only descriptors (SRV/RTV/DSV); a resource's default view lives here and is copied into tables. ----
struct CpuDescriptors {
    ID3D12Device*device{};D3D12_DESCRIPTOR_HEAP_TYPE type{};UINT increment{};static constexpr UINT pageSize=4096;
    std::vector<ComPtr<ID3D12DescriptorHeap>> pages;std::vector<UINT> freed;UINT next{};
    void init(ID3D12Device*d,D3D12_DESCRIPTOR_HEAP_TYPE t){device=d;type=t;increment=d->GetDescriptorHandleIncrementSize(t);}
    UINT allocate(){
        if(!freed.empty()){UINT slot=freed.back();freed.pop_back();return slot;}
        if(next==pages.size()*pageSize){D3D12_DESCRIPTOR_HEAP_DESC desc{type,pageSize,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> heap;check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)),"CreateDescriptorHeap");pages.push_back(heap);}
        return next++;
    }
    void release(UINT slot){freed.push_back(slot);}
    D3D12_CPU_DESCRIPTOR_HANDLE handle(UINT slot)const{auto h=pages[slot/pageSize]->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(slot%pageSize)*increment;return h;}
};
struct Device;
// GPU resource with per-subresource state tracking; destruction defers the ID3D12Resource until the GPU is past it.
struct Resource {
    Device*owner{};ComPtr<ID3D12Resource> resource;std::vector<D3D12_RESOURCE_STATES> states;UINT srvSlot{~0u};D3D12_CPU_DESCRIPTOR_HANDLE srv{};
    Resource()=default;Resource(const Resource&)=delete;Resource&operator=(const Resource&)=delete;
    virtual ~Resource();
};
struct Tex : Resource { DXGI_FORMAT format{};UINT width{},height{},mips{1},layers{1},samples{1};bool cube{},depth{},renderTarget{}; };
struct Buf : Resource { UINT64 size{};UINT stride{}; };
struct UploadPage { ComPtr<ID3D12Resource> resource;std::uint8_t*cpu{};D3D12_GPU_VIRTUAL_ADDRESS gpu{};UINT64 size{},used{}; };
struct Upload { ID3D12Resource*resource{};UINT64 offset{};std::uint8_t*cpu{};D3D12_GPU_VIRTUAL_ADDRESS gpu{}; };
struct FrameContext {
    ComPtr<ID3D12CommandAllocator> allocator;UINT64 fence{};
    std::vector<UploadPage> pages;
    std::vector<ComPtr<ID3D12DescriptorHeap>> tables;std::size_t table{},tableUsed{};
    std::vector<ComPtr<ID3D12DescriptorHeap>> transients;UINT transientUsed{}; // CPU-only pages for per-draw views, copied into tables at flush
};
// Device, queue, frame contexts, upload rings, descriptor rings and the deferred-release graveyard.
struct Device {
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;HANDLE event{};UINT64 fenceValue{};
    ComPtr<ID3D12InfoQueue> info;
    std::array<FrameContext,3> frames;std::size_t frameIndex{};std::uint64_t frameCount{};bool recording{};
    CpuDescriptors srvs,rtvs,dsvs;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;UINT tableIncrement{},samplerIncrement{};
    std::vector<UploadPage> freePages;
    std::deque<std::pair<UINT64,ComPtr<ID3D12Resource>>> graveyard;
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    static constexpr UINT64 pageBytes=8u<<20;static constexpr UINT tablePage=32768,transientPage=4096;
    std::string adapterName;D3D_FEATURE_LEVEL featureLevel{};D3D_SHADER_MODEL shaderModel{};
    ~Device(){
        try{if(queue&&fence)wait_idle();}catch(...){} // Destructors must not throw; a lost device has nothing left to wait for.
        for(auto&[value,res]:graveyard)res.Reset();
        if(event)CloseHandle(event);
    }
    void retire(ComPtr<ID3D12Resource>&&r){if(r)graveyard.emplace_back(fenceValue+1,std::move(r));}
    void collect(){const UINT64 done=fence->GetCompletedValue();while(!graveyard.empty()&&graveyard.front().first<=done)graveyard.pop_front();}
    void wait(UINT64 value){if(fence->GetCompletedValue()<value){check(fence->SetEventOnCompletion(value,event),"SetEventOnCompletion");WaitForSingleObject(event,INFINITE);}}
    void wait_idle(){wait(fenceValue);}
    FrameContext&frame(){return frames[frameIndex];}
    void print_messages(){
        if(!info)return;
        const UINT64 count=info->GetNumStoredMessages();
        for(UINT64 i=0;i<count;++i){
            SIZE_T length=0;info->GetMessage(i,nullptr,&length);std::vector<std::uint8_t> storage(length);auto*message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(SUCCEEDED(info->GetMessage(i,message,&length)))std::cerr<<"kabus22: d3d12 debug "<<(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR?"error":message->Severity==D3D12_MESSAGE_SEVERITY_WARNING?"warning":"info")<<": "<<message->pDescription<<'\n';
        }
        info->ClearStoredMessages();
    }
    Upload upload(UINT64 bytes,UINT64 alignment){
        auto&f=frame();
        auto fits=[&](UploadPage&p){UINT64 offset=(p.used+alignment-1)/alignment*alignment;return offset+bytes<=p.size?std::optional<UINT64>(offset):std::nullopt;};
        if(f.pages.empty()||!fits(f.pages.back())){
            UploadPage page;
            auto reusable=std::find_if(freePages.begin(),freePages.end(),[&](const UploadPage&p){return p.size>=bytes+alignment;});
            if(reusable!=freePages.end()){page=std::move(*reusable);freePages.erase(reusable);}
            else{
                page.size=std::max(pageBytes,(bytes+alignment+0xffff)&~UINT64(0xffff));
                D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_UPLOAD};D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_BUFFER,0,page.size,1,1,1,DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,D3D12_RESOURCE_FLAG_NONE};
                check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&page.resource)),"upload heap");
                D3D12_RANGE none{0,0};void*cpu{};check(page.resource->Map(0,&none,&cpu),"Map upload heap");page.cpu=static_cast<std::uint8_t*>(cpu);page.gpu=page.resource->GetGPUVirtualAddress();
            }
            page.used=0;f.pages.push_back(std::move(page));
        }
        auto&p=f.pages.back();const UINT64 offset=*fits(p);p.used=offset+bytes;
        return {p.resource.Get(),offset,p.cpu+offset,p.gpu+offset};
    }
    // Shader-visible descriptor ring: one page set per frame context, a fresh page (and SetDescriptorHeaps) when full.
    bool table_space(UINT count){
        auto&f=frame();
        if(f.tableUsed+std::max<UINT>(count,16)<=tablePage)return false;
        ++f.table;f.tableUsed=0;
        if(f.table==f.tables.size()){D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,tablePage,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ComPtr<ID3D12DescriptorHeap> heap;check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)),"descriptor ring");f.tables.push_back(heap);}
        bind_heaps();return true;
    }
    void bind_heaps(){auto&f=frame();ID3D12DescriptorHeap*heaps[]{f.tables[f.table].Get(),samplerHeap.Get()};list->SetDescriptorHeaps(2,heaps);}
    // Transient views live in non-shader-visible pages (no SetDescriptorHeaps involved); a frame grows its page set when full and
    // reuses it from the start once that frame context comes round again. Handles stay unique within a frame, so the table cache keyed
    // on CPU handle values never mistakes a rewritten slot for an already copied one.
    D3D12_CPU_DESCRIPTOR_HANDLE transient_view(){
        auto&f=frame();const UINT page=f.transientUsed/transientPage;
        if(page==f.transients.size()){D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,transientPage,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> heap;check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)),"transient descriptor heap");f.transients.push_back(heap);}
        auto h=f.transients[page]->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(f.transientUsed++%transientPage)*tableIncrement;return h;
    }
    void transition(Resource&r,D3D12_RESOURCE_STATES state,UINT sub=allSubresources){
        auto add=[&](UINT index){
            auto&current=r.states[index];if(current==state)return;
            // Read-state combinations only widen (a buffer read as vertices then as an SRV stays in one combined state).
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r.resource.Get();b.Transition.Subresource=r.states.size()==1?allSubresources:index;b.Transition.StateBefore=current;b.Transition.StateAfter=state;barriers.push_back(b);current=state;
        };
        if(sub!=allSubresources){add(sub);return;}
        bool uniform=std::all_of(r.states.begin(),r.states.end(),[&](auto s){return s==r.states[0];});
        if(uniform&&r.states.size()>1){if(r.states[0]==state)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r.resource.Get();b.Transition.Subresource=allSubresources;b.Transition.StateBefore=r.states[0];b.Transition.StateAfter=state;barriers.push_back(b);std::fill(r.states.begin(),r.states.end(),state);return;}
        for(UINT i=0;i<r.states.size();++i)add(i);
    }
    void flush_barriers(){if(!barriers.empty()){list->ResourceBarrier(UINT(barriers.size()),barriers.data());barriers.clear();}}
};
Resource::~Resource(){if(owner){owner->retire(std::move(resource));if(srvSlot!=~0u)owner->srvs.release(srvSlot);}}

// ---- Programs: shaders, input layout and the GL texture units each SRV register reads. ----
enum class Kind : std::uint8_t { Tex2D, Buffer, Cube, Array };
enum Program : std::uint8_t { PScene, PShadow, PUi, PPost, PPresent, PTonemap, PWarp, PSky, PCloud, PMip, PComposite, PBloom, PFxaa, PLinearize, PLinearizeMS, PSsao, PAoApply, ProgramCount };
enum class Layout : std::uint8_t { None, Scene, Shadow, Ui };
struct Unit { std::uint8_t unit;Kind kind; };
struct Shader { const void*data{};std::size_t size{}; };
template<std::size_t N> Shader shader_bytes(const unsigned char(&bytes)[N]){return {bytes,N};}
struct ProgramInfo { Shader vs,ps,gs;Layout layout{};std::vector<Unit> units; };
// Vertex streams: GL attribute locations 0/1/2+3/4/5/12/13 become slots 0..6, particle instance data (6..11) slot 7.
constexpr UINT slotPosition=0,slotNormal=1,slotUv=2,slotNextPosition=3,slotNextNormal=4,slotTangent=5,slotNextTangent=6,slotParticle=7,vertexSlots=8;

struct SamplerKey {
    std::uint8_t linear{1},mip{0},clamp{0},aniso{1}; // mip: 0 none, 1 nearest, 2 linear
    bool operator==(const SamplerKey&)const=default;
};
struct SamplerKeyHash{std::size_t operator()(const SamplerKey&k)const{return std::size_t(k.linear)|std::size_t(k.mip)<<8|std::size_t(k.clamp)<<16|std::size_t(k.aniso)<<24;}};
// Effective min filter of a native mipmapping mode (render_gl.cpp effective_mip_filter): GL_LINEAR, _MIPMAP_NEAREST, _MIPMAP_LINEAR.
std::uint8_t effective_mip_mode(double mode){return mode<1?0:mode<=2?1:2;}

struct PsoKey {
    std::uint8_t program,variant,blend,src,dst,srcAlpha,dstAlpha,colorWrite,depthTest,depthWrite,stencilTest,stencilFunc,stencilPass,stencilWrite,cull,frontCounterClockwise,samples,pad;
    DXGI_FORMAT rtv,dsv;
    bool operator==(const PsoKey&o)const{return std::memcmp(this,&o,sizeof(PsoKey))==0;}
};
struct PsoKeyHash{std::size_t operator()(const PsoKey&k)const{std::size_t h=1469598103934665603ull;auto*b=reinterpret_cast<const unsigned char*>(&k);for(std::size_t i=0;i<sizeof k;++i){h^=b[i];h*=1099511628211ull;}return h;}};

// Constant buffers, mirroring the HLSL cbuffers byte for byte (16-byte register packing respected by construction).
struct SceneView {
    Matrix viewProjection{},portalProjection{},sunShadowMatrix{};
    std::array<float,4> clipPlane{},portalViewport{},portalPlane{};
    F3 sunDirection;int lightCount{};F3 cameraPosition;int selectedLight{-1};F3 fogColor;int pbr{};F3 infiniteViewer;int toneInShader{};
    F3 doorCenter;int doorMode{};F3 doorNormal;int skyMask{};F3 doorRight;int portalEnabled{};F3 doorUp;int shadowMaps{};F3 doorSkyColor;int casterPass{};
    F2 fogRange,doorHalf;int shadowLight{-1};float sunShadowTexel{};int padding[2]{};
};
struct SceneDraw {
    Matrix model{};std::array<F4,3> normalMatrix{};
    F4 tint{1,1,1,1},preparedAmbient,skillMain,skillExtra;
    F3 preparedDiffuse;float materialPower{};F3 preparedSpecular;float materialSunStrength{};F3 preparedEmissive;float interpolation{};F3 ambientLight;float normalOrientation{1};
    F2 uvScale{1,1},uvOffset;F2 normalUvScale{1,1};float alpha{1},materialAmbient{};
    float surfaceAmbient{},reflectionWeight{},legacyRoughness{};int frame0{};
    int frame1{},vertexCount{},spriteFrames{1},effectKind{};
    int firstOverride{},nextOverride{},firstNormalOverride{},nextNormalOverride{};
    int particleInstanced{},worldVertexLighting{},hasLightmap{},explicitMaterial{};
    int unlit{},flare{},overlay{},invertFlare{};
    int textureAlpha{},noFog{},doorSky{},roughnessMap{};
};
struct ShadowCB {
    Matrix model{},projection{},viewProjection{};std::array<float,4> clipPlane{};
    F3 cameraPosition;float areaThreshold{};F3 fogColor;float alpha{};F2 fogRange,viewport;
    int frame0{},frame1{},vertexCount{};float interpolation{};int firstOverride{},nextOverride{},reversed{},noFog{};
};
struct UiCB { F2 viewport;float alphaRef{};int overlay{},textKey{},cursorKey{};int padding[2]{}; };
struct PostCB { int enabled{};int padding[3]{}; };
struct WarpCB { int size[2]{};int stride{};int padding{}; };
struct SkyCB { std::array<F4,3> basis{};F2 cone,scroll;float curve{},clip{};int cube{},blend{};int layer{};int padding[3]{}; };
struct CloudCB { int level{},layer{},convertAlpha{},padding{}; };
struct MipCB { F2 inverseSize;F2 padding; };
struct EffectCB { Matrix viewProjection{},inverseViewProjection{};F3 eye;float nearPlane{};F3 forward;float farPlane{};F2 texel;int mode{},bloomEnabled{},toneMap{};int depthOffset[2]{};int padding{}; };
static_assert(sizeof(SceneView)%16==0&&sizeof(SceneDraw)%16==0&&sizeof(ShadowCB)%16==0&&sizeof(SkyCB)%16==0&&sizeof(EffectCB)%16==0);
// A CPU constant block; uploaded to the frame's ring only when its bytes changed since the last upload this frame.
struct CbSlot { const void*data{};std::size_t size{};std::vector<std::uint8_t> last;D3D12_GPU_VIRTUAL_ADDRESS va{};bool valid{}; };

struct Binding { std::shared_ptr<Resource> resource;D3D12_CPU_DESCRIPTOR_HANDLE srv{};UINT sub{allSubresources};D3D12_RESOURCE_STATES state{shaderRead}; };
// glBindFramebuffer target: colour (mip/slice) and depth-stencil attachments.
struct Fbo { std::shared_ptr<Tex> color;UINT colorMip{},colorSlice{};std::shared_ptr<Tex> depth;UINT depthSlice{}; };
struct VertexView { std::shared_ptr<Buf> buffer;D3D12_VERTEX_BUFFER_VIEW view{};bool upload{}; };

struct UIVertex { float x,y,u,v,r,g,b,a,u0,v0,u1,v1; };
struct Batch { UINT first{},count{};std::uint32_t material{};UINT centroidFirst{},centroidCount{}; };
struct MeshGPU { std::shared_ptr<Buf> vertices,indices,tangents,poses;std::vector<Batch> batches;int frameCount{},primaryMax{},skinLayers{};Bounds bounds;std::vector<Bounds> frameBounds; };
struct TextureGPU { std::shared_ptr<Tex> id;std::uint32_t width{},height{};std::uint64_t serial{};bool dynamic{},alpha{},mipsValid{};std::shared_ptr<Tex> normal,orm;bool roughness{}; };
struct MeshOverride {
    std::shared_ptr<Buf> positions,normals,tangents;
    std::uint64_t revision{~0ull},tangentRevision{~0ull},boundsRevision{~0ull};
    std::size_t count{},normalCount{},boundsCount{};
    std::uint32_t frame{~0u};
    const Model*model{},*boundsModel{};
    std::vector<Vec3> tangentData;
    Bounds bounds;
    bool tangentReady{};
};
Bounds mesh_override_bounds(MeshOverride&data,const Model&model,std::uint64_t revision,std::span<const Vec3>positions){
    if(data.boundsModel!=&model||data.boundsRevision!=revision||data.boundsCount!=positions.size()){data.bounds=position_bounds(positions);data.boundsModel=&model;data.boundsRevision=revision;data.boundsCount=positions.size();}
    return data.bounds;
}
bool override_tangents_needed(const MeshOverride&data,EffectKind effect,std::uint64_t revision){return effect!=EffectKind::None&&(!data.tangentReady||data.tangentRevision!=revision);}
void set_override_pose(MeshOverride&data,const Model&model,std::uint64_t revision,std::size_t count,std::size_t normalCount,std::uint32_t frame){data.tangentReady=false;data.revision=revision;data.count=count;data.normalCount=normalCount;data.frame=frame;data.model=&model;}
void purge_model_overrides(std::unordered_map<std::uint32_t,MeshOverride>&overrides,const Model*model){
    for(auto it=overrides.begin();it!=overrides.end();){if(it->second.model==model||it->second.boundsModel==model)it=overrides.erase(it);else ++it;}
}
struct EffectBank { std::shared_ptr<Tex> texture;std::uint32_t width{},height{};bool dynamic{},alpha{}; };
struct EffectBindings { std::uint64_t generation{~0ull};EffectKind kind{EffectKind::None};std::array<EffectBank,2> banks{}; };
struct ParticleGPU { Matrix model;Color color;std::array<float,3> flags; };
struct WarpGPU { std::shared_ptr<Tex> texture;std::uint32_t width{},height{};std::uint64_t serial{~0ull};int phase{-1};float range{};bool mipsValid{}; };
struct SkyGPU { std::shared_ptr<Tex> texture;std::uint32_t frames{}; };
bool blended(const DrawInstance&d){bool transparent=d.alpha<1||d.additive||d.translucent||d.flare;if(d.model)for(const auto&material:d.model->materials)transparent|=material.alpha<1;return transparent;}
UINT mip_count(UINT w,UINT h){return UINT(std::bit_width(std::max(w,h)));}
} // namespace
struct D3D12Backend::Impl {
    Device dev; // First member: destroyed last, after every resource below has been retired into its graveyard.
    SDL_Window*window{};RenderOptions options;bool portalPass{};
    ComPtr<IDXGISwapChain3> swapchain;bool tearing{};UINT swapWidth{},swapHeight{};std::vector<std::shared_ptr<Tex>> backBuffers;
    ComPtr<ID3D12RootSignature> root;
    std::array<ProgramInfo,ProgramCount> programs;
    std::unordered_map<PsoKey,ComPtr<ID3D12PipelineState>,PsoKeyHash> psos;
    std::unordered_map<SamplerKey,UINT,SamplerKeyHash> samplerSlots;
    UINT maxSamples{1};int samples{1},actualSamples{1};float preferredAnisotropy{1},anisotropy{1},maxAnisotropy{16};bool b5g5r5a1{};
    std::uint8_t mipMode{2};bool mipsEnabled{true};
    static constexpr int maxTextureSize=D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    // ---- Emulated OpenGL state (see file comment). ----
    struct GL {
        Program program{PScene};
        bool blend{};std::uint8_t src{D3D12_BLEND_ONE},dst{D3D12_BLEND_ZERO},srcAlpha{D3D12_BLEND_ONE},dstAlpha{D3D12_BLEND_ZERO};
        bool colorMask{true},depthTest{},depthMask{true},stencilTest{};std::uint8_t stencilFunc{D3D12_COMPARISON_FUNC_ALWAYS},stencilPass{D3D12_STENCIL_OP_KEEP},stencilWrite{255};UINT stencilRef{};
        bool cull{},frontCCW{true};bool scissorTest{};int scissor[4]{},viewport[4]{};
        const Fbo*fbo{};
        std::array<Binding,16> tex2d,buffers,cubes,arrays;std::array<std::uint8_t,16> samplers{}; // GL sampler objects bound per unit (SamplerId)
        Color clearColor{0,0,0,0};
    } gl;
    std::array<VertexView,vertexSlots> vb;std::shared_ptr<Buf> ib;
    struct Applied {
        bool valid{};ID3D12Resource*color{},*depth{};UINT colorMip{},colorSlice{},depthSlice{};D3D12_CPU_DESCRIPTOR_HANDLE rtv{},dsv{};
        ID3D12PipelineState*pso{};D3D12_VIEWPORT viewport{};D3D12_RECT scissor{};UINT stencilRef{~0u};D3D12_GPU_VIRTUAL_ADDRESS cb0{},cb1{};
        std::array<UINT,16> samplerIndex{};bool samplersValid{};std::array<SIZE_T,16> table{};std::size_t tableCount{};D3D12_GPU_DESCRIPTOR_HANDLE tableGpu{};bool tableValid{};
        std::array<D3D12_VERTEX_BUFFER_VIEW,vertexSlots> vbs{};D3D12_INDEX_BUFFER_VIEW ib{};bool topology{};
    } applied;
    SceneView sv;SceneDraw sd;ShadowCB sh;UiCB uiCb;PostCB postCb;WarpCB warpCb;SkyCB skyCb;CloudCB cloudCb;MipCB mipCb;EffectCB fx;
    CbSlot svSlot{&sv,sizeof sv},sdSlot{&sd,sizeof sd},shSlot{&sh,sizeof sh},uiSlot{&uiCb,sizeof uiCb},postSlot{&postCb,sizeof postCb},warpSlot{&warpCb,sizeof warpCb},skySlot{&skyCb,sizeof skyCb},cloudSlot{&cloudCb,sizeof cloudCb},mipSlot{&mipCb,sizeof mipCb},fxSlot{&fx,sizeof fx};
    std::array<std::pair<CbSlot*,CbSlot*>,ProgramCount> programCbs{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4> nullSrv{};std::shared_ptr<Buf> dummyInstance;
    UINT rtvSlot{},dsvSlot{};
    // ---- Device, swap chain and pipeline objects. ----
    void create_device(bool debug){
#if !K22_UWP
        if(debug){
            ComPtr<ID3D12Debug> layer;
            if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)))){layer->EnableDebugLayer();ComPtr<ID3D12Debug1> validation;if(SUCCEEDED(layer.As(&validation)))validation->SetEnableGPUBasedValidation(TRUE);std::cerr<<"kabus22: d3d12 debug layer and GPU-based validation enabled\n";}
            else{std::cerr<<"kabus22: warning: K22_D3D12_DEBUG=1 but the Direct3D 12 debug layer is not installed\n";debug=false;}
        }
#endif
        check(CreateDXGIFactory2(debug?DXGI_CREATE_FACTORY_DEBUG:0,IID_PPV_ARGS(&dev.factory)),"CreateDXGIFactory2");
        auto tryAdapter=[&](IDXGIAdapter1*a){DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);if(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)return false;if(FAILED(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev.device))))return false;dev.adapter=a;dev.adapterName=narrow(d.Description);return true;};
        ComPtr<IDXGIFactory6> ordered;ComPtr<IDXGIAdapter1> adapter;
        if(SUCCEEDED(dev.factory.As(&ordered))){for(UINT i=0;!dev.device&&ordered->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i)tryAdapter(adapter.Get());}
        else for(UINT i=0;!dev.device&&dev.factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i)tryAdapter(adapter.Get());
        if(!dev.device)throw std::runtime_error("No Direct3D 12 hardware adapter with feature level 11_0");
        static const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl{UINT(std::size(levels)),levels,D3D_FEATURE_LEVEL_11_0};dev.featureLevel=SUCCEEDED(dev.device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,&fl,sizeof fl))?fl.MaxSupportedFeatureLevel:D3D_FEATURE_LEVEL_11_0;
        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_0};
        if(FAILED(dev.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&sm,sizeof sm))||sm.HighestShaderModel<D3D_SHADER_MODEL_6_0)throw std::runtime_error("Direct3D 12 device "+dev.adapterName+" lacks shader model 6.0");
        dev.shaderModel=sm.HighestShaderModel;
        if(debug&&SUCCEEDED(dev.device.As(&dev.info))){dev.info->SetMuteDebugOutput(FALSE);D3D12_MESSAGE_SEVERITY quiet[]{D3D12_MESSAGE_SEVERITY_INFO,D3D12_MESSAGE_SEVERITY_MESSAGE};D3D12_MESSAGE_ID muted[]{D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED};D3D12_INFO_QUEUE_FILTER filter{};filter.DenyList.NumSeverities=UINT(std::size(quiet));filter.DenyList.pSeverityList=quiet;filter.DenyList.NumIDs=UINT(std::size(muted));filter.DenyList.pIDList=muted;dev.info->PushStorageFilter(&filter);}
        D3D12_COMMAND_QUEUE_DESC queue{D3D12_COMMAND_LIST_TYPE_DIRECT,0,D3D12_COMMAND_QUEUE_FLAG_NONE,0};check(dev.device->CreateCommandQueue(&queue,IID_PPV_ARGS(&dev.queue)),"CreateCommandQueue");
        for(auto&f:dev.frames)check(dev.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&f.allocator)),"CreateCommandAllocator");
        check(dev.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,dev.frames[0].allocator.Get(),nullptr,IID_PPV_ARGS(&dev.list)),"CreateCommandList");dev.list->Close();
        check(dev.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&dev.fence)),"CreateFence");dev.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!dev.event)throw std::runtime_error("Direct3D 12 fence event creation failed");
        dev.srvs.init(dev.device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);dev.rtvs.init(dev.device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_RTV);dev.dsvs.init(dev.device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        {D3D12_DESCRIPTOR_HEAP_DESC d{D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,256,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};check(dev.device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&dev.samplerHeap)),"sampler heap");}
        dev.tableIncrement=dev.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);dev.samplerIncrement=dev.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        rtvSlot=dev.rtvs.allocate();dsvSlot=dev.dsvs.allocate();
        D3D12_FEATURE_DATA_FORMAT_SUPPORT b5{DXGI_FORMAT_B5G5R5A1_UNORM};b5g5r5a1=SUCCEEDED(dev.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&b5,sizeof b5))&&(b5.Support1&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)&&(b5.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D);
        auto msaa=[&](UINT count){for(DXGI_FORMAT f:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_D24_UNORM_S8_UINT}){D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{f,count,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};if(FAILED(dev.device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&q,sizeof q))||!q.NumQualityLevels)return false;}return true;};
        for(UINT count=1;count<=32;++count)if(msaa(count))maxSamples=count;
    }
    bool sample_count_supported(UINT count){for(DXGI_FORMAT f:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_D24_UNORM_S8_UINT}){D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{f,count,D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,0};if(FAILED(dev.device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&q,sizeof q))||!q.NumQualityLevels)return false;}return true;}
    // Like a GL multisample renderbuffer: the smallest supported count not below the requested one.
    UINT storage_samples(int requested){for(UINT count=UINT(std::max(requested,1));count<=maxSamples;++count)if(sample_count_supported(count))return count;return maxSamples;}
    void create_swapchain(){
        SDL_SysWMinfo info;SDL_VERSION(&info.version);if(!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error(std::string("SDL_GetWindowWMInfo: ")+SDL_GetError());
        int w{},h{};SDL_GetWindowSizeInPixels(window,&w,&h);
#if K22_UWP
        if(platform::OutputMode o;platform::output_mode(o)){w=o.width;h=o.height;} // Renderer::metrics: the TV's resolution, not the 1080p CoreWindow
#endif
        swapWidth=UINT(std::max(w,1));swapHeight=UINT(std::max(h,1));
        ComPtr<IDXGIFactory5> f5;BOOL allow=FALSE;tearing=SUCCEEDED(dev.factory.As(&f5))&&SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&allow,sizeof allow))&&allow;
        DXGI_SWAP_CHAIN_DESC1 desc{swapWidth,swapHeight,DXGI_FORMAT_R8G8B8A8_UNORM,FALSE,{1,0},DXGI_USAGE_RENDER_TARGET_OUTPUT,3,DXGI_SCALING_STRETCH,DXGI_SWAP_EFFECT_FLIP_DISCARD,DXGI_ALPHA_MODE_IGNORE,tearing?UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING):0u};
        ComPtr<IDXGISwapChain1> chain;
#if K22_UWP
        if(info.subsystem!=SDL_SYSWM_WINRT)throw std::runtime_error("Direct3D 12 on UWP requires an SDL WinRT CoreWindow");
        check(dev.factory->CreateSwapChainForCoreWindow(dev.queue.Get(),reinterpret_cast<IUnknown*>(info.info.winrt.window),&desc,nullptr,&chain),"CreateSwapChainForCoreWindow");
#else
        if(info.subsystem!=SDL_SYSWM_WINDOWS)throw std::runtime_error("Direct3D 12 requires an SDL Win32 window");
        check(dev.factory->CreateSwapChainForHwnd(dev.queue.Get(),info.info.win.window,&desc,nullptr,nullptr,&chain),"CreateSwapChainForHwnd");
        dev.factory->MakeWindowAssociation(info.info.win.window,DXGI_MWA_NO_ALT_ENTER);
#endif
        check(chain.As(&swapchain),"IDXGISwapChain3");acquire_back_buffers();
    }
    void acquire_back_buffers(){
        backBuffers.clear();
        for(UINT i=0;i<3;++i){auto t=std::make_shared<Tex>();check(swapchain->GetBuffer(i,IID_PPV_ARGS(&t->resource)),"swap chain buffer");t->format=DXGI_FORMAT_R8G8B8A8_UNORM;t->width=swapWidth;t->height=swapHeight;t->renderTarget=true;t->states={D3D12_RESOURCE_STATE_PRESENT};backBuffers.push_back(t);}
    }
    void resize_swapchain(UINT w,UINT h){
        drain_completion();dev.wait_idle();dev.collect();backFbo={};backBuffers.clear();applied={}; // ResizeBuffers fails while any back-buffer reference survives
        check(swapchain->ResizeBuffers(3,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,tearing?UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING):0u),"ResizeBuffers");swapWidth=w;swapHeight=h;acquire_back_buffers();
    }
    void create_root_signature(){
        D3D12_DESCRIPTOR_RANGE srvRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,16,0,0,0},samplerRange{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,256,0,0,0};
        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[0].Descriptor={0,0};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[1].Descriptor={1,0};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={2,0,16};params[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[3].DescriptorTable={1,&srvRange};params[3].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[4].DescriptorTable={1,&samplerRange};params[4].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_STATIC_SAMPLER_DESC fixed[2]{};
        fixed[0]={D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,0,1,D3D12_COMPARISON_FUNC_LESS_EQUAL,D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,0,0,0,1,D3D12_SHADER_VISIBILITY_ALL};
        fixed[1]={D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,0,1,D3D12_COMPARISON_FUNC_NEVER,D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,0,0,1,1,D3D12_SHADER_VISIBILITY_ALL};
        D3D12_ROOT_SIGNATURE_DESC desc{5,params,2,fixed,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> blob,error;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)))throw std::runtime_error(std::string("Direct3D 12 root signature: ")+(error?static_cast<const char*>(error->GetBufferPointer()):"serialization failed"));
        check(dev.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"CreateRootSignature");
    }
    void create_programs(){
        using namespace d3d12::dxil;using K=Kind;
        auto set=[&](Program p,Shader vs,Shader ps,Layout layout,std::vector<Unit> units,CbSlot*cb0,CbSlot*cb1=nullptr,Shader gs={}){programs[p]={vs,ps,gs,layout,std::move(units)};programCbs[p]={cb0,cb1};};
        const Shader post=shader_bytes(postVS);
        set(PScene,shadowMapsOn?shader_bytes(sceneShadowVS):shader_bytes(sceneVS),shadowMapsOn?shader_bytes(sceneShadowPS):shader_bytes(scenePS),Layout::Scene,{{0,K::Tex2D},{1,K::Tex2D},{2,K::Buffer},{3,K::Buffer},{4,K::Tex2D},{5,K::Tex2D},{6,K::Tex2D},{7,K::Tex2D},{8,K::Tex2D},{9,K::Cube}},&svSlot,&sdSlot);
        set(PShadow,shader_bytes(shadowVS),shader_bytes(shadowPS),Layout::Shadow,{{2,K::Buffer}},&shSlot,nullptr,shader_bytes(shadowGS));
        set(PUi,shader_bytes(uiVS),shader_bytes(uiPS),Layout::Ui,{{0,K::Tex2D}},&uiSlot);
        set(PPost,post,shader_bytes(postPS),Layout::None,{{0,K::Tex2D},{1,K::Tex2D}},&postSlot);
        set(PPresent,shader_bytes(presentVS),shader_bytes(postPS),Layout::None,{{0,K::Tex2D},{1,K::Tex2D}},&postSlot);
        set(PTonemap,post,shader_bytes(tonemapPS),Layout::None,{{0,K::Tex2D}},&fxSlot);
        set(PWarp,post,shader_bytes(warpPS),Layout::None,{{0,K::Tex2D},{1,K::Buffer}},&warpSlot);
        set(PSky,post,shader_bytes(skyPS),Layout::None,{{0,K::Array}},&skySlot);
        set(PCloud,post,shader_bytes(cloudPS),Layout::None,{{0,K::Array}},&cloudSlot);
        set(PMip,post,shader_bytes(mipPS),Layout::None,{{0,K::Array}},&mipSlot);
        set(PComposite,post,shader_bytes(compositePS),Layout::None,{{0,K::Tex2D},{1,K::Tex2D}},&fxSlot);
        set(PBloom,post,shader_bytes(bloomPS),Layout::None,{{0,K::Tex2D}},&fxSlot);
        set(PFxaa,post,shader_bytes(fxaaPS),Layout::None,{{0,K::Tex2D}},&fxSlot);
        set(PLinearize,post,shader_bytes(linearizePS),Layout::None,{{10,K::Tex2D}},&fxSlot);
        set(PLinearizeMS,post,shader_bytes(linearizeMSPS),Layout::None,{{10,K::Tex2D}},&fxSlot);
        set(PSsao,post,shader_bytes(ssaoPS),Layout::None,{{10,K::Tex2D}},&fxSlot);
        set(PAoApply,post,shader_bytes(aoApplyPS),Layout::None,{{11,K::Tex2D}},&fxSlot);
    }
    void create_null_views(){
        for(int kind=0;kind<4;++kind){
            nullSrv[kind]=dev.srvs.handle(dev.srvs.allocate());D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            if(kind==int(Kind::Tex2D)){d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;}
            else if(kind==int(Kind::Buffer)){d.Format=DXGI_FORMAT_UNKNOWN;d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;d.Buffer.NumElements=1;d.Buffer.StructureByteStride=16;}
            else if(kind==int(Kind::Cube)){d.Format=DXGI_FORMAT_R32_FLOAT;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;d.TextureCube.MipLevels=1;}
            else{d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;d.Texture2DArray.MipLevels=1;d.Texture2DArray.ArraySize=1;}
            dev.device->CreateShaderResourceView(nullptr,&d,nullSrv[kind]);
        }
        for(auto*units:{&gl.tex2d,&gl.buffers,&gl.cubes,&gl.arrays})for(auto&b:*units)b.srv=nullSrv[units==&gl.tex2d?0:units==&gl.buffers?1:units==&gl.cubes?2:3];
    }
    // ---- Command recording and submission. ----
    void begin_commands(){
        if(dev.recording)return;
        dev.frameIndex=std::size_t(dev.frameCount%dev.frames.size());auto&f=dev.frame();dev.wait(f.fence);dev.collect();
        check(f.allocator->Reset(),"command allocator reset");check(dev.list->Reset(f.allocator.Get(),nullptr),"command list reset");
        for(auto&p:f.pages)dev.freePages.push_back(std::move(p));f.pages.clear();
        if(f.tables.empty()){D3D12_DESCRIPTOR_HEAP_DESC d{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,Device::tablePage,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ComPtr<ID3D12DescriptorHeap> heap;check(dev.device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&heap)),"descriptor ring");f.tables.push_back(heap);}
        f.table=0;f.tableUsed=0;f.transientUsed=0;
        dev.bind_heaps();dev.list->SetGraphicsRootSignature(root.Get());dev.list->SetGraphicsRootDescriptorTable(4,dev.samplerHeap->GetGPUDescriptorHandleForHeapStart());
        applied={};for(auto*slot:{&svSlot,&sdSlot,&shSlot,&uiSlot,&postSlot,&warpSlot,&skySlot,&cloudSlot,&mipSlot,&fxSlot})slot->valid=false;
        dev.recording=true;
    }
    void execute(){
        dev.flush_barriers();check(dev.list->Close(),"command list close");dev.recording=false;
        ID3D12CommandList*lists[]{dev.list.Get()};dev.queue->ExecuteCommandLists(1,lists);
    }
    UINT64 signal(){++dev.fenceValue;check(dev.queue->Signal(dev.fence.Get(),dev.fenceValue),"queue signal");dev.frame().fence=dev.fenceValue;++dev.frameCount;dev.print_messages();return dev.fenceValue;}
    // Presentation limit (render_policy::presentation_limit) over frame fences, as GLBackend's completion syncs.
    std::deque<UINT64> completion;unsigned completionLimit{1};std::uint64_t completionSubmissions{},completionWaits{};int requestedSwapInterval{-1};
    void retire_completion(){dev.wait(completion.front());completion.pop_front();++completionWaits;}
    void drain_completion(){while(!completion.empty())retire_completion();}
    void admit_frame(std::int32_t raw,bool bonus){completionLimit=presentation_limit(options.mode,raw);requestedSwapInterval=presentation_interval(options.mode,raw,bonus,options.vsync);}
    void await_submission(){while(completion.size()>=completionLimit)retire_completion();}
    // ---- Resources. ----
    static DXGI_FORMAT dsv_format(DXGI_FORMAT f){return f==DXGI_FORMAT_R24G8_TYPELESS?DXGI_FORMAT_D24_UNORM_S8_UINT:DXGI_FORMAT_D32_FLOAT;}
    void allocate_texture(Tex&t,DXGI_FORMAT format,UINT w,UINT h,UINT mips,UINT layers,UINT samples,bool rt,bool cube=false){
        dev.retire(std::move(t.resource));
        t.owner=&dev;t.format=format;t.width=w;t.height=h;t.mips=mips;t.layers=layers;t.samples=samples;t.cube=cube;t.depth=format==DXGI_FORMAT_R24G8_TYPELESS||format==DXGI_FORMAT_R32_TYPELESS;t.renderTarget=rt;
        D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_TEXTURE2D,0,w,h,UINT16(layers),UINT16(mips),format,{samples,0},D3D12_TEXTURE_LAYOUT_UNKNOWN,t.depth?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:rt?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE};
        const D3D12_RESOURCE_STATES initial=t.depth?D3D12_RESOURCE_STATE_DEPTH_WRITE:rt?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_COPY_DEST;
        D3D12_CLEAR_VALUE clear{};clear.Format=dsv_format(format);clear.DepthStencil={1,0};D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
        check(dev.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,initial,t.depth?&clear:nullptr,IID_PPV_ARGS(&t.resource)),"texture creation");
        t.states.assign(std::size_t(mips)*layers,initial);
        if(t.srvSlot==~0u){t.srvSlot=dev.srvs.allocate();t.srv=dev.srvs.handle(t.srvSlot);}
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Format=format==DXGI_FORMAT_R24G8_TYPELESS?DXGI_FORMAT_R24_UNORM_X8_TYPELESS:format==DXGI_FORMAT_R32_TYPELESS?DXGI_FORMAT_R32_FLOAT:format;
        if(samples>1)v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DMS;
        else if(cube){v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;v.TextureCube.MipLevels=mips;}
        else if(layers>1){v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray.MipLevels=mips;v.Texture2DArray.ArraySize=layers;}
        else{v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=mips;}
        dev.device->CreateShaderResourceView(t.resource.Get(),&v,t.srv);
    }
    std::shared_ptr<Tex> create_texture(DXGI_FORMAT format,UINT w,UINT h,UINT mips=1,UINT layers=1,UINT samples=1,bool rt=false,bool cube=false){auto t=std::make_shared<Tex>();allocate_texture(*t,format,w,h,mips,layers,samples,rt,cube);return t;}
    // One subresource from tightly packed rows (srcPitch bytes apart), recorded into the current command list.
    void upload_subresource(Tex&t,UINT mip,UINT slice,const void*data,UINT rowBytes,UINT srcPitch){
        const UINT sub=mip+slice*t.mips;const auto desc=t.resource->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows{};UINT64 rowSize{},total{};
        dev.device->GetCopyableFootprints(&desc,sub,1,0,&fp,&rows,&rowSize,&total);
        auto up=dev.upload(total,D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        for(UINT r=0;r<rows;++r)std::memcpy(up.cpu+std::size_t(r)*fp.Footprint.RowPitch,static_cast<const std::uint8_t*>(data)+std::size_t(r)*srcPitch,rowBytes);
        fp.Offset=up.offset;dev.transition(t,D3D12_RESOURCE_STATE_COPY_DEST,sub);dev.flush_barriers();
        D3D12_TEXTURE_COPY_LOCATION dst{t.resource.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}},src{up.resource,D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}};dst.SubresourceIndex=sub;src.PlacedFootprint=fp;
        dev.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    }
    std::shared_ptr<Buf> create_buffer(UINT64 size,UINT stride=0){
        auto b=std::make_shared<Buf>();b->owner=&dev;b->size=std::max<UINT64>(size,4);b->stride=stride;
        D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_BUFFER,0,b->size,1,1,1,DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,D3D12_RESOURCE_FLAG_NONE};
        check(dev.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&b->resource)),"buffer creation");b->states={D3D12_RESOURCE_STATE_COMMON};
        if(stride){b->srvSlot=dev.srvs.allocate();b->srv=dev.srvs.handle(b->srvSlot);D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Format=DXGI_FORMAT_UNKNOWN;v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;v.Buffer.NumElements=UINT(b->size/stride);v.Buffer.StructureByteStride=stride;dev.device->CreateShaderResourceView(b->resource.Get(),&v,b->srv);}
        return b;
    }
    void upload_buffer(Buf&b,const void*data,UINT64 bytes){
        if(!bytes)return;auto up=dev.upload(bytes,16);std::memcpy(up.cpu,data,std::size_t(bytes));
        dev.transition(b,D3D12_RESOURCE_STATE_COPY_DEST);dev.flush_barriers();dev.list->CopyBufferRegion(b.resource.Get(),0,up.resource,up.offset,bytes);
    }
    std::shared_ptr<Buf> static_buffer(const void*data,UINT64 bytes,UINT stride=0){auto b=create_buffer(bytes,stride);upload_buffer(*b,data,bytes);return b;}
    UINT sampler(const SamplerKey&k){
        auto found=samplerSlots.find(k);if(found!=samplerSlots.end())return found->second;
        const UINT slot=UINT(samplerSlots.size());if(slot>=256)throw std::runtime_error("Direct3D 12 sampler cache exhausted");
        D3D12_SAMPLER_DESC d{};const auto address=k.clamp?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;d.AddressU=d.AddressV=d.AddressW=address;d.MaxAnisotropy=1;d.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
        d.Filter=!k.linear?D3D12_FILTER_MIN_MAG_MIP_POINT:k.aniso>1?D3D12_FILTER_ANISOTROPIC:k.mip==2?D3D12_FILTER_MIN_MAG_MIP_LINEAR:D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        if(k.aniso>1&&k.linear)d.MaxAnisotropy=k.aniso;
        d.MinLOD=0;d.MaxLOD=k.mip?D3D12_FLOAT32_MAX:0;
        auto h=dev.samplerHeap->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(slot)*dev.samplerIncrement;dev.device->CreateSampler(&d,h);
        return samplerSlots[k]=slot;
    }
    // ---- The emulated GL calls. ----
    std::array<Binding,16>&units(Kind k){return k==Kind::Tex2D?gl.tex2d:k==Kind::Buffer?gl.buffers:k==Kind::Cube?gl.cubes:gl.arrays;}
    void use(Program p){gl.program=p;}
    void bind(int unit,const std::shared_ptr<Tex>&t,Kind k=Kind::Tex2D){auto&b=units(k)[unit];b.resource=t;b.srv=t?t->srv:nullSrv[int(k)];b.sub=allSubresources;b.state=t&&t->depth?depthRead:shaderRead;}
    void bind_buffer(int unit,const std::shared_ptr<Buf>&buffer){auto&b=gl.buffers[unit];b.resource=buffer;b.srv=buffer?buffer->srv:nullSrv[int(Kind::Buffer)];b.sub=allSubresources;b.state=bufferRead;}
    void bind_view(int unit,Kind k,std::shared_ptr<Resource> r,D3D12_CPU_DESCRIPTOR_HANDLE view,UINT sub,D3D12_RESOURCE_STATES state){auto&b=units(k)[unit];b.resource=std::move(r);b.srv=view;b.sub=sub;b.state=state;}
    void bind_sampler(int unit,std::uint8_t id){gl.samplers[unit]=id;}
    // The GL sampler objects of render_gl.cpp; their filter state follows configure_sampling, so units hold ids.
    enum SamplerId : std::uint8_t { SceneSampler, LightmapSampler, DynamicSceneSampler, SkyClampSampler, UiNearestSampler, UiLinearSampler };
    SamplerKey sampler_key(std::uint8_t id)const{
        const auto a=std::uint8_t(std::clamp<long>(std::lround(anisotropy),1,16));
        switch(id){case SceneSampler:return {1,mipMode,0,a};case LightmapSampler:case SkyClampSampler:return {1,mipMode,1,a};case DynamicSceneSampler:return {1,0,0,a};case UiNearestSampler:return {0,0,1,1};default:return {1,0,1,1};}
    }
    void blend_func(D3D12_BLEND s,D3D12_BLEND d){gl.src=gl.srcAlpha=std::uint8_t(s);gl.dst=gl.dstAlpha=std::uint8_t(d);}
    void blend_func_separate(D3D12_BLEND s,D3D12_BLEND d,D3D12_BLEND sa,D3D12_BLEND da){gl.src=std::uint8_t(s);gl.dst=std::uint8_t(d);gl.srcAlpha=std::uint8_t(sa);gl.dstAlpha=std::uint8_t(da);}
    void viewport(int x,int y,int w,int h){gl.viewport[0]=x;gl.viewport[1]=y;gl.viewport[2]=w;gl.viewport[3]=h;}
    void scissor(int x,int y,int w,int h){gl.scissor[0]=x;gl.scissor[1]=y;gl.scissor[2]=w;gl.scissor[3]=h;}
    void framebuffer(const Fbo&f){gl.fbo=&f;}
    void vertex_buffer(UINT slot,const std::shared_ptr<Buf>&b,UINT64 offset,UINT stride){auto&v=vb[slot];v.buffer=b;v.upload=false;v.view={b->resource->GetGPUVirtualAddress()+offset,UINT(b->size-offset),stride};}
    static UINT subresource(const Tex&t,UINT mip,UINT slice){return mip+slice*t.mips;}
    std::pair<UINT,UINT> target_size()const{const auto&f=*gl.fbo;if(f.color)return {std::max(1u,f.color->width>>f.colorMip),std::max(1u,f.color->height>>f.colorMip)};return {f.depth->width,f.depth->height};}
    void apply_target(){
        const Fbo&f=*gl.fbo;Tex*color=f.color.get();Tex*depth=f.depth.get();
        if(color)dev.transition(*color,D3D12_RESOURCE_STATE_RENDER_TARGET,color->states.size()==1?allSubresources:subresource(*color,f.colorMip,f.colorSlice));
        if(depth)dev.transition(*depth,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        auto*cr=color?color->resource.Get():nullptr;auto*dr=depth?depth->resource.Get():nullptr;
        if(applied.valid&&applied.color==cr&&applied.depth==dr&&applied.colorMip==f.colorMip&&applied.colorSlice==f.colorSlice&&applied.depthSlice==f.depthSlice)return;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv=dev.rtvs.handle(rtvSlot),dsv=dev.dsvs.handle(dsvSlot);
        if(color){D3D12_RENDER_TARGET_VIEW_DESC v{};v.Format=color->format;if(color->samples>1)v.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DMS;else{v.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray={f.colorMip,f.colorSlice,1,0};}dev.device->CreateRenderTargetView(color->resource.Get(),&v,rtv);}
        if(depth){D3D12_DEPTH_STENCIL_VIEW_DESC v{};v.Format=dsv_format(depth->format);if(depth->samples>1)v.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DMS;else{v.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray={0,f.depthSlice,1};}dev.device->CreateDepthStencilView(depth->resource.Get(),&v,dsv);}
        dev.list->OMSetRenderTargets(color?1:0,color?&rtv:nullptr,FALSE,depth?&dsv:nullptr);
        applied.valid=true;applied.color=cr;applied.depth=dr;applied.colorMip=f.colorMip;applied.colorSlice=f.colorSlice;applied.depthSlice=f.depthSlice;applied.rtv=rtv;applied.dsv=dsv;
    }
    // glClear: honours the scissor test and the colour/depth/stencil write masks.
    void clear(bool color,bool depth,bool stencil){
        apply_target();dev.flush_barriers();const Fbo&f=*gl.fbo;auto [w,h]=target_size();
        D3D12_RECT rect{0,0,LONG(w),LONG(h)};if(gl.scissorTest)rect={std::max<LONG>(0,gl.scissor[0]),std::max<LONG>(0,gl.scissor[1]),std::min<LONG>(LONG(w),gl.scissor[0]+gl.scissor[2]),std::min<LONG>(LONG(h),gl.scissor[1]+gl.scissor[3])};
        if(rect.right<=rect.left||rect.bottom<=rect.top)return;
        if(color&&gl.colorMask&&f.color){const float c[4]{gl.clearColor.r,gl.clearColor.g,gl.clearColor.b,gl.clearColor.a};dev.list->ClearRenderTargetView(applied.rtv,c,1,&rect);}
        D3D12_CLEAR_FLAGS flags{};if(depth&&gl.depthMask)flags|=D3D12_CLEAR_FLAG_DEPTH;if(stencil&&gl.stencilWrite&&f.depth&&f.depth->format==DXGI_FORMAT_R24G8_TYPELESS)flags|=D3D12_CLEAR_FLAG_STENCIL;
        if(flags&&f.depth)dev.list->ClearDepthStencilView(applied.dsv,flags,1,0,1,&rect);
    }
    ID3D12PipelineState*pipeline(const PsoKey&k){
        auto found=psos.find(k);if(found!=psos.end())return found->second.Get();
        const auto&p=programs[k.program];using namespace d3d12::dxil;
        const Shader ps=k.variant?shader_bytes(sceneCasterPS):p.ps;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.VS={p.vs.data,p.vs.size};d.PS={ps.data,ps.size};if(p.gs.data)d.GS={p.gs.data,p.gs.size};
        auto&rt=d.BlendState.RenderTarget[0];rt.BlendEnable=k.blend;rt.SrcBlend=D3D12_BLEND(k.src);rt.DestBlend=D3D12_BLEND(k.dst);rt.BlendOp=D3D12_BLEND_OP_ADD;rt.SrcBlendAlpha=D3D12_BLEND(k.srcAlpha);rt.DestBlendAlpha=D3D12_BLEND(k.dstAlpha);rt.BlendOpAlpha=D3D12_BLEND_OP_ADD;rt.LogicOp=D3D12_LOGIC_OP_NOOP;rt.RenderTargetWriteMask=k.colorWrite?D3D12_COLOR_WRITE_ENABLE_ALL:0;
        d.SampleMask=UINT_MAX;
        d.RasterizerState={D3D12_FILL_MODE_SOLID,k.cull?D3D12_CULL_MODE_BACK:D3D12_CULL_MODE_NONE,BOOL(k.frontCounterClockwise),0,0,0,TRUE,FALSE,FALSE,0,D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
        const D3D12_DEPTH_STENCILOP_DESC face{D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP(k.stencilPass),D3D12_COMPARISON_FUNC(k.stencilFunc)};
        d.DepthStencilState={BOOL(k.depthTest),k.depthWrite?D3D12_DEPTH_WRITE_MASK_ALL:D3D12_DEPTH_WRITE_MASK_ZERO,D3D12_COMPARISON_FUNC_LESS_EQUAL,BOOL(k.stencilTest),0xff,k.stencilWrite,face,face};
        static const D3D12_INPUT_ELEMENT_DESC scene[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,slotPosition,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,slotNormal,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,slotUv,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,slotUv,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",2,DXGI_FORMAT_R32G32B32_FLOAT,slotNextPosition,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",3,DXGI_FORMAT_R32G32B32_FLOAT,slotNextNormal,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",4,DXGI_FORMAT_R32G32B32_FLOAT,slotTangent,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",5,DXGI_FORMAT_R32G32B32_FLOAT,slotNextTangent,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",6,DXGI_FORMAT_R32G32B32A32_FLOAT,slotParticle,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},{"TEXCOORD",7,DXGI_FORMAT_R32G32B32A32_FLOAT,slotParticle,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
            {"TEXCOORD",8,DXGI_FORMAT_R32G32B32A32_FLOAT,slotParticle,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},{"TEXCOORD",9,DXGI_FORMAT_R32G32B32A32_FLOAT,slotParticle,48,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,slotParticle,offsetof(ParticleGPU,color),D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},{"TEXCOORD",10,DXGI_FORMAT_R32G32B32_FLOAT,slotParticle,offsetof(ParticleGPU,flags),D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
        static const D3D12_INPUT_ELEMENT_DESC shadow[]{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,slotPosition,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",2,DXGI_FORMAT_R32G32B32_FLOAT,slotNextPosition,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        static const D3D12_INPUT_ELEMENT_DESC ui[]{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        if(p.layout==Layout::Scene)d.InputLayout={scene,UINT(std::size(scene))};else if(p.layout==Layout::Shadow)d.InputLayout={shadow,UINT(std::size(shadow))};else if(p.layout==Layout::Ui)d.InputLayout={ui,UINT(std::size(ui))};
        d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;d.NumRenderTargets=k.rtv!=DXGI_FORMAT_UNKNOWN;d.RTVFormats[0]=k.rtv;d.DSVFormat=k.dsv;d.SampleDesc={k.samples,0};
        ComPtr<ID3D12PipelineState> pso;check(dev.device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&pso)),"CreateGraphicsPipelineState");
        return (psos[k]=pso).Get();
    }
    D3D12_GPU_VIRTUAL_ADDRESS constants(CbSlot&slot){
        if(slot.valid&&std::memcmp(slot.last.data(),slot.data,slot.size)==0)return slot.va;
        auto up=dev.upload((slot.size+255)&~std::size_t(255),D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);std::memcpy(up.cpu,slot.data,slot.size);
        slot.last.assign(static_cast<const std::uint8_t*>(slot.data),static_cast<const std::uint8_t*>(slot.data)+slot.size);slot.va=up.gpu;slot.valid=true;return up.gpu;
    }
    // Resolves the emulated state for one draw.
    void flush(){
        const auto&p=programs[gl.program];const Fbo&f=*gl.fbo;
        for(const auto&u:p.units){auto&b=units(u.kind)[u.unit];if(b.resource)dev.transition(*b.resource,b.state,b.sub);}
        if(p.layout!=Layout::None){for(auto&v:vb)if(v.buffer)dev.transition(*v.buffer,bufferRead);if(ib)dev.transition(*ib,bufferRead);}
        apply_target();dev.flush_barriers();
        Tex*color=f.color.get();Tex*depth=f.depth.get();
        PsoKey k;std::memset(&k,0,sizeof k);
        k.program=gl.program;k.variant=gl.program==PScene&&!color;k.blend=gl.blend&&color;k.src=gl.src;k.dst=gl.dst;k.srcAlpha=gl.srcAlpha;k.dstAlpha=gl.dstAlpha;k.colorWrite=gl.colorMask;
        if(!k.blend)k.src=k.srcAlpha=D3D12_BLEND_ONE,k.dst=k.dstAlpha=D3D12_BLEND_ZERO;
        k.depthTest=gl.depthTest&&depth;k.depthWrite=k.depthTest&&gl.depthMask;k.stencilTest=gl.stencilTest&&depth&&depth->format==DXGI_FORMAT_R24G8_TYPELESS;
        k.stencilFunc=k.stencilTest?gl.stencilFunc:D3D12_COMPARISON_FUNC_ALWAYS;k.stencilPass=k.stencilTest?gl.stencilPass:D3D12_STENCIL_OP_KEEP;k.stencilWrite=k.stencilTest?gl.stencilWrite:0;
        k.cull=gl.cull;k.frontCounterClockwise=gl.cull&&!gl.frontCCW; // y-flipped clip space mirrors GL winding
        k.samples=std::uint8_t(color?color->samples:depth?depth->samples:1);k.rtv=color?color->format:DXGI_FORMAT_UNKNOWN;k.dsv=depth?dsv_format(depth->format):DXGI_FORMAT_UNKNOWN;
        auto*pso=pipeline(k);if(pso!=applied.pso){dev.list->SetPipelineState(pso);applied.pso=pso;}
        if(!applied.topology){dev.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);applied.topology=true;}
        auto [tw,th]=target_size();
        D3D12_VIEWPORT viewport{float(gl.viewport[0]),float(gl.viewport[1]),float(gl.viewport[2]),float(gl.viewport[3]),0,1};
        D3D12_RECT scissor{0,0,LONG(tw),LONG(th)};if(gl.scissorTest)scissor={std::max<LONG>(0,gl.scissor[0]),std::max<LONG>(0,gl.scissor[1]),std::max<LONG>(0,gl.scissor[0]+gl.scissor[2]),std::max<LONG>(0,gl.scissor[1]+gl.scissor[3])};
        if(std::memcmp(&viewport,&applied.viewport,sizeof viewport)){dev.list->RSSetViewports(1,&viewport);applied.viewport=viewport;}
        if(std::memcmp(&scissor,&applied.scissor,sizeof scissor)){dev.list->RSSetScissorRects(1,&scissor);applied.scissor=scissor;}
        if(k.stencilTest&&applied.stencilRef!=gl.stencilRef){dev.list->OMSetStencilRef(gl.stencilRef);applied.stencilRef=gl.stencilRef;}
        auto [cb0,cb1]=programCbs[gl.program];
        if(cb0){auto va=constants(*cb0);if(va!=applied.cb0){dev.list->SetGraphicsRootConstantBufferView(0,va);applied.cb0=va;}}
        if(cb1){auto va=constants(*cb1);if(va!=applied.cb1){dev.list->SetGraphicsRootConstantBufferView(1,va);applied.cb1=va;}}
        std::array<UINT,16> samplerIndex{};std::array<SIZE_T,16> table{};
        for(std::size_t r=0;r<p.units.size();++r){const auto&u=p.units[r];samplerIndex[r]=sampler(sampler_key(gl.samplers[u.unit]));table[r]=units(u.kind)[u.unit].srv.ptr;}
        if(!applied.samplersValid||samplerIndex!=applied.samplerIndex){dev.list->SetGraphicsRoot32BitConstants(2,16,samplerIndex.data(),0);applied.samplerIndex=samplerIndex;applied.samplersValid=true;}
        if(!applied.tableValid||applied.tableCount!=p.units.size()||table!=applied.table){
            const UINT count=UINT(p.units.size());
            if(dev.table_space(count))dev.list->SetGraphicsRootDescriptorTable(4,dev.samplerHeap->GetGPUDescriptorHandleForHeapStart());
            auto&fr=dev.frame();auto*heap=fr.tables[fr.table].Get();auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto gpu=heap->GetGPUDescriptorHandleForHeapStart();
            cpu.ptr+=SIZE_T(fr.tableUsed)*dev.tableIncrement;gpu.ptr+=UINT64(fr.tableUsed)*dev.tableIncrement;fr.tableUsed+=std::max<UINT>(count,1);
            for(UINT r=0;r<count;++r){D3D12_CPU_DESCRIPTOR_HANDLE dst{cpu.ptr+SIZE_T(r)*dev.tableIncrement},src{table[r]};dev.device->CopyDescriptorsSimple(1,dst,src,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);}
            dev.list->SetGraphicsRootDescriptorTable(3,gpu);applied.table=table;applied.tableCount=count;applied.tableGpu=gpu;applied.tableValid=true;
        }
        if(p.layout!=Layout::None){
            std::array<D3D12_VERTEX_BUFFER_VIEW,vertexSlots> views{};for(UINT s=0;s<vertexSlots;++s)views[s]=vb[s].view;
            if(std::memcmp(views.data(),applied.vbs.data(),sizeof views)){dev.list->IASetVertexBuffers(0,vertexSlots,views.data());applied.vbs=views;}
            if(ib){D3D12_INDEX_BUFFER_VIEW view{ib->resource->GetGPUVirtualAddress(),UINT(ib->size),DXGI_FORMAT_R32_UINT};if(std::memcmp(&view,&applied.ib,sizeof view)){dev.list->IASetIndexBuffer(&view);applied.ib=view;}}
        }
    }
    void draw_arrays(UINT count,UINT instances=1){flush();dev.list->DrawInstanced(count,instances,0,0);}
    void draw_elements(UINT count,UINT first,UINT instances=1){if(!count)return;flush();dev.list->DrawIndexedInstanced(count,instances,first,0,0);}
    // Fullscreen/transient helper passes save and restore the emulated state like the GL code's glGet/glEnable dance.
    struct Saved { GL gl;std::array<VertexView,vertexSlots> vb;std::shared_ptr<Buf> ib; };
    Saved save()const{return {gl,vb,ib};}
    void restore(Saved&&s){gl=std::move(s.gl);vb=std::move(s.vb);ib=std::move(s.ib);}
    // glGenerateMipmap for every array slice of a mipmapped colour texture.
    void generate_mips(const std::shared_ptr<Tex>&t){
        if(t->mips<2)return;
        auto saved=save();
        use(PMip);gl.blend=gl.depthTest=gl.stencilTest=gl.cull=gl.scissorTest=false;gl.colorMask=true;
        for(UINT slice=0;slice<t->layers;++slice)for(UINT level=1;level<t->mips;++level){
            Fbo f{t,level,slice};framebuffer(f);const UINT w=std::max(1u,t->width>>level),h=std::max(1u,t->height>>level);viewport(0,0,int(w),int(h));
            auto view=dev.transient_view();D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Format=t->format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;v.Texture2DArray={level-1,1,slice,1,0,0};dev.device->CreateShaderResourceView(t->resource.Get(),&v,view);
            bind_view(0,Kind::Array,t,view,subresource(*t,level-1,slice),shaderRead);mipCb.inverseSize={1.f/float(w),1.f/float(h)};draw_arrays(3);
        }
        restore(std::move(saved));
    }
    // ---- Port of GLBackend::Impl (same names; GL calls replaced by the emulation above). ----
    std::shared_ptr<Tex> white,zero,flatNormal,defaultOrm,gammaTexture,videoTexture;int videoWidth{},videoHeight{};std::uint64_t videoSerial{~0ull};
    std::shared_ptr<Tex> msaaColor,msaaDepth,resolveColor,alphaColor,alphaDepth,portalColor,portalDepth,hdrColor,hdrDepth,hdrResolveColor,ldrColor,linearDepth,aoTexture,sunShadowMap,cubeShadowMap,captureColor;
    std::array<std::shared_ptr<Tex>,5> bloomTexture;
    Fbo msaaFbo,resolveFbo,alphaFbo,portalFbo,hdrFbo,ldrFbo,linearFbo,aoFbo,sunShadowFbo,backFbo,captureFbo;std::array<Fbo,5> bloomFbo;std::array<Fbo,6> cubeShadowFbo;
    int targetWidth{},targetHeight{};
    bool composite{},compositeBlend{},shadowMapsOn{},shadowsReady{},casterPass{};int shadowLight{-1};
    struct CasterGroup{std::shared_ptr<Tex> texture;std::uint8_t sampler;bool textureAlpha;Color tint;float alpha;Vec2 uvScale,uvOffset;std::vector<UINT> counts,firsts;};
    std::vector<CasterGroup> worldCasters;bool casterWorld{},worldCastersReady{};
    void draw_world_casters(){
        for(const auto&g:worldCasters){
            bind(0,g.texture);bind_sampler(0,g.sampler);sd.textureAlpha=g.textureAlpha;
            sd.tint={g.tint.r,g.tint.g,g.tint.b,g.tint.a};sd.alpha=g.alpha;sd.uvScale={g.uvScale.x,g.uvScale.y};sd.uvOffset={g.uvOffset.x,g.uvOffset.y};
            for(std::size_t i=0;i<g.counts.size();++i)draw_elements(g.counts[i],g.firsts[i]);
        }
    }
    int effectWidth{},effectHeight{};
    bool retainedColorValid{},portalColorValid{},finalFrameValid{};
    std::unordered_map<std::uint32_t,EffectBindings> effectBindings;
    std::unordered_map<const Model*,MeshGPU> meshes;
    std::unordered_map<std::uint32_t,MeshOverride> overrides;
    ColorAdjustment gamma{};bool gammaUploaded{};
    std::unordered_map<const Image*,std::array<TextureGPU,3>> textures;
    std::unordered_map<const Image*,WarpGPU> warps;
    std::unordered_set<const Image*> turbulent; // Never pack-replaced: the native warp table addresses original texels.
    std::vector<std::int32_t> turbulenceOffsets;
    const RenderFrame*currentFrame{};
    Matrix viewProjection{};
    std::array<float,4> geometryClipPlane{};
    std::unordered_map<const Image*,SkyGPU> skies;
    bool maskPass{},doorPass{},centroidLighting{};
    std::vector<UIVertex> uiVertices;
    std::vector<std::array<float,4>> lightData;std::vector<Vec2> lightCones;UINT lightsSlot{};D3D12_CPU_DESCRIPTOR_HANDLE lightsSrv{};
    std::vector<const DrawInstance*> shadowOrder,transparent;
    std::vector<const Particle*> particleOrder;std::vector<ParticleGPU> particleData;
    explicit Impl(SDL_Window*w,RenderOptions o):window(w),options(std::move(o)){
        bool debug=false;
#if !K22_UWP
        if(const char*e=std::getenv("K22_D3D12_DEBUG");e&&*e=='1')debug=true;
#endif
        create_device(debug);
        std::cerr<<"kabus22: D3D12 adapter="<<dev.adapterName<<"\nD3D12 feature level="<<feature_level_name()<<" shader model="<<shader_model_name()<<'\n';
        samples=std::clamp(options.samples,1,int(maxSamples));
        if(options.samples>int(maxSamples))std::cerr<<"kabus22: graphics: MSAA "<<options.samples<<" unsupported, using "<<maxSamples<<'\n';
        options.samples=samples;
        preferredAnisotropy=anisotropy=std::clamp(options.anisotropy,1.0f,maxAnisotropy);if(options.anisotropy>maxAnisotropy)std::cerr<<"kabus22: graphics: anisotropy "<<options.anisotropy<<" unsupported, using "<<maxAnisotropy<<'\n';options.anisotropy=preferredAnisotropy;
        const auto&fx=options.features;composite=composite_enabled(options.pbr,fx);compositeBlend=composite&&(fx.bloom||!options.pbr);shadowMapsOn=shadow_maps_enabled(fx);
        create_root_signature();create_programs();create_null_views();create_swapchain();
        requestedSwapInterval=options.vsync?1:0;
        lightsSlot=dev.srvs.allocate();lightsSrv=dev.srvs.handle(lightsSlot);
        begin_commands();
        auto pixel=[&](std::array<std::uint8_t,4> v){auto t=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,1,1);upload_subresource(*t,0,0,v.data(),4,4);return t;};
        white=pixel({255,255,255,255});zero=pixel({0,0,0,255});flatNormal=pixel({128,128,255,255});defaultOrm=pixel({255,255,0,255});
        gammaTexture=create_texture(DXGI_FORMAT_R16_UNORM,256,3);upload_subresource(*gammaTexture,0,0,gamma.gammaRamp.data(),512,512);
        const ParticleGPU none{};dummyInstance=static_buffer(&none,sizeof none);
        execute();signal();dev.wait_idle();
        uiVertices.reserve(4096);lightData.reserve(128);transparent.reserve(256);particleOrder.reserve(1024);particleData.reserve(1024);
    }
    ~Impl(){try{if(dev.queue&&dev.fence){if(dev.recording)execute(),signal();dev.wait_idle();}}catch(...){}} // Destructors must not throw (std::terminate)
    std::string feature_level_name()const{auto l=unsigned(dev.featureLevel);return std::to_string(l>>12)+"_"+std::to_string((l>>8)&15);}
    std::string shader_model_name()const{auto s=unsigned(dev.shaderModel);return std::to_string(s>>4)+"."+std::to_string(s&15);}
    void configure_sampling(double mipmapping,double enabledAnisotropy){
        const auto mode=effective_mip_mode(mipmapping);if(mode!=mipMode){mipMode=mode;mipsEnabled=mode!=0;}
        anisotropy=effective_anisotropy(options.forceAnisotropy?1:enabledAnisotropy,preferredAnisotropy);
    }
    void free_targets(){
        drain_completion();
        for(auto*t:{&msaaColor,&msaaDepth,&resolveColor,&alphaColor,&alphaDepth,&portalColor,&portalDepth,&hdrColor,&hdrDepth,&hdrResolveColor,&ldrColor})t->reset();
        for(auto&t:bloomTexture)t.reset();for(auto*f:{&msaaFbo,&resolveFbo,&alphaFbo,&portalFbo,&hdrFbo,&ldrFbo})*f={};bloomFbo={};
        free_effects();
        retainedColorValid=portalColorValid=finalFrameValid=false;targetWidth=targetHeight=0;
    }
    void free_effects(){linearDepth.reset();aoTexture.reset();linearFbo=aoFbo={};effectWidth=effectHeight=0;}
    // Renderer::reconfigure. Only shadow maps on<->off needs a new device, as GLBackend's compiled-in scene variant.
    bool reconfigure(RenderOptions next,bool&reloadScene){
        if(shadow_maps_enabled(next.features)!=shadowMapsOn)return false;
        next.samples=std::clamp(next.samples,1,int(maxSamples));next.anisotropy=std::clamp(next.anisotropy,1.f,maxAnisotropy);
        drain_completion();
        const auto&a=options.features,&b=next.features;
        const bool rebuildTargets=next.samples!=samples||next.pbr!=options.pbr||a.bloom!=b.bloom||a.fxaa!=b.fxaa||composite_enabled(next.pbr,b)!=composite;
        const bool rebuildEffects=a.ssao!=b.ssao;
        reloadScene=next.pbr!=options.pbr||next.packs!=options.packs;
        if(next.anisotropy!=preferredAnisotropy||next.forceAnisotropy!=options.forceAnisotropy){preferredAnisotropy=next.anisotropy;anisotropy=0;}
        options=next;samples=next.samples;composite=composite_enabled(options.pbr,options.features);compositeBlend=composite&&(options.features.bloom||!options.pbr);
        if(rebuildTargets)free_targets();else if(rebuildEffects)free_effects();
        finalFrameValid=retainedColorValid=portalColorValid=false;
        return true;
    }
    void targets(int w,int h){if(w==targetWidth&&h==targetHeight)return;free_targets();targetWidth=w;targetHeight=h;
        actualSamples=int(storage_samples(samples));const UINT s=UINT(actualSamples);
        msaaColor=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,w,h,1,1,s,true);msaaDepth=create_texture(DXGI_FORMAT_R24G8_TYPELESS,w,h,1,1,s);msaaFbo={msaaColor,0,0,msaaDepth};
        resolveColor=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,w,h,1,1,1,true);resolveFbo={resolveColor};
        if(!composite)return;
        hdrColor=create_texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,1,1,s,true);hdrDepth=create_texture(DXGI_FORMAT_R24G8_TYPELESS,w,h,1,1,s);hdrFbo={hdrColor,0,0,hdrDepth};
        hdrResolveColor=create_texture(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,1,1,1,true);
        if(options.features.bloom)for(int level=0;level<5;++level){bloomTexture[level]=create_texture(DXGI_FORMAT_R16G16B16A16_FLOAT,UINT(std::max(1,w>>(level+1))),UINT(std::max(1,h>>(level+1))),1,1,1,true);bloomFbo[level]={bloomTexture[level]};}
        if(options.features.fxaa){ldrColor=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,w,h,1,1,1,true);ldrFbo={ldrColor};}
    }
    // glBlitFramebuffer colour resolve/copy and same-format depth-stencil copy.
    void resolve(Tex&src,Tex&dst){
        if(src.samples>1){dev.transition(src,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);dev.transition(dst,D3D12_RESOURCE_STATE_RESOLVE_DEST);dev.flush_barriers();dev.list->ResolveSubresource(dst.resource.Get(),0,src.resource.Get(),0,dst.format);}
        else copy(src,dst);
    }
    void copy(Tex&src,Tex&dst){dev.transition(src,D3D12_RESOURCE_STATE_COPY_SOURCE);dev.transition(dst,D3D12_RESOURCE_STATE_COPY_DEST);dev.flush_barriers();dev.list->CopyResource(dst.resource.Get(),src.resource.Get());}
    void shadow_pass(const RenderFrame&f,const Camera&camera,float aspect){
        if(!sunShadowMap){
            sunShadowMap=create_texture(DXGI_FORMAT_R32_TYPELESS,sunShadowResolution,sunShadowResolution);sunShadowFbo={nullptr,0,0,sunShadowMap};
            cubeShadowMap=create_texture(DXGI_FORMAT_R32_TYPELESS,cubeShadowResolution,cubeShadowResolution,1,6,1,false,true);for(UINT face=0;face<6;++face)cubeShadowFbo[face]={nullptr,0,0,cubeShadowMap,face};
        }
        const Matrix sun=sun_shadow_matrix(camera,aspect,f.sunDirection,f.world?mesh(f.world->mesh).bounds:Bounds{},sunShadowDistance,sunShadowResolution);
        shadowLight=shadow_light_index(f.lights,camera.position);
        use(PScene);sv.casterPass=1;sv.shadowMaps=0;sv.lightCount=0;sv.skyMask=0;sv.doorMode=0;sv.pbr=0;
        bind_sampler(0,SceneSampler);bind_sampler(1,LightmapSampler);
        bind(8,nullptr);bind(9,nullptr,Kind::Cube);
        geometryClipPlane={};sv.clipPlane=geometryClipPlane;portalPass=false;casterPass=true;worldCasters.clear();worldCastersReady=false;
        gl.scissorTest=false;gl.blend=false;gl.stencilTest=false;gl.depthTest=true;gl.depthMask=true;gl.colorMask=false;
        auto casters=[&](const Matrix&vp,int size){
            viewport(0,0,size,size);clear(false,true,false);viewProjection=vp;sv.viewProjection=vp;
            if(camera.drawWorld&&f.world&&(camera.layers&1)){DrawInstance world;world.model=&f.world->mesh;casterWorld=true;draw_instance(world,f.skinBindings,&camera);casterWorld=false;}
            if(f.renderEntities&&!camera.noEntities)for(const auto&d:f.instances){
                if(!d.visible||!d.model||d.viewSpace||d.billboard||!(d.layers&camera.layers)||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
                if(camera.perDrawOpacity&&model_has_entity_skins(*d.model)){if(model_queue(d,f.skinBindings)&1)draw_instance(d,f.skinBindings,&camera,true,0);}
                else if(!blended(d))draw_instance(d,f.skinBindings,&camera);
            }
        };
        framebuffer(sunShadowFbo);casters(sun,int(sunShadowResolution));
        if(shadowLight>=0){
            const auto&light=f.lights[shadowLight];
            for(int face=0;face<6;++face){framebuffer(cubeShadowFbo[face]);casters(cube_face_matrix(light.position,face,cubeShadowNear,light.range),int(cubeShadowResolution));}
        }
        casterPass=false;gl.colorMask=true;gl.blend=true;
        const float radius=1/std::sqrt(sun[0]*sun[0]+sun[4]*sun[4]+sun[8]*sun[8]);
        sv.casterPass=0;sv.shadowMaps=1;sv.shadowLight=shadowLight;sv.sunShadowMatrix=sun;sv.sunShadowTexel=2*radius/sunShadowResolution;
        bind(8,sunShadowMap);bind(9,cubeShadowMap,Kind::Cube);
        shadowsReady=true;
    }
    // Primary-view SSAO, before view-space entities clear depth (render_gl.cpp screen_effects). The linearize pass reads
    // the view rectangle of the bound depth buffer directly instead of a blitted single-sample copy.
    void screen_effects(const Camera&camera,int x,int y,int w,int h){
        const Fbo*destination=gl.fbo;
        if(effectWidth!=w||effectHeight!=h){
            free_effects();effectWidth=w;effectHeight=h;
            linearDepth=create_texture(DXGI_FORMAT_R32_FLOAT,UINT(w),UINT(h),1,1,1,true);linearFbo={linearDepth};
            aoTexture=create_texture(DXGI_FORMAT_R8_UNORM,UINT(w),UINT(h),1,1,1,true);aoFbo={aoTexture};
        }
        gl.scissorTest=false;gl.depthTest=false;gl.blend=false;gl.cull=false;
        framebuffer(linearFbo);viewport(0,0,w,h);use(destination->depth->samples>1?PLinearizeMS:PLinearize);
        bind(10,destination->depth);bind_sampler(10,UiNearestSampler);
        fx.nearPlane=camera.nearClip;fx.farPlane=camera.farClip;fx.depthOffset[0]=x;fx.depthOffset[1]=y;draw_arrays(3);
        bind(10,linearDepth);
        const Matrix inverseVP=inverse(viewProjection),basis=transform({},camera.angles,{1,1,1});
        framebuffer(aoFbo);use(PSsao);fx.inverseViewProjection=inverseVP;fx.eye=f3(camera.position);fx.forward={basis[0],basis[1],basis[2]};fx.viewProjection=viewProjection;fx.texel={1.f/w,1.f/h};draw_arrays(3);
        framebuffer(*destination);viewport(x,y,w,h);use(PAoApply);bind(11,aoTexture);bind_sampler(11,UiLinearSampler);
        fx.texel={1.f/w,1.f/h};gl.blend=true;blend_func_separate(D3D12_BLEND_ZERO,D3D12_BLEND_SRC_COLOR,D3D12_BLEND_ZERO,D3D12_BLEND_ONE);draw_arrays(3);gl.blend=false;
        framebuffer(*destination);viewport(x,y,w,h);gl.scissorTest=true;gl.depthTest=true;gl.blend=true;use(PScene);
    }
    // Background composite: HDR resolve, bloom, tone map (PBR only, else identity) and FXAA into the LDR scene target.
    void composite_pass(int w,int h){
        const auto&fxs=options.features;post_state();
        resolve(*hdrColor,*hdrResolveColor);copy(*hdrDepth,*msaaDepth);
        if(fxs.bloom){
            use(PBloom);bind_sampler(0,UiLinearSampler);
            auto size=[&](int level){return std::array<int,2>{std::max(1,w>>(level+1)),std::max(1,h>>(level+1))};};
            auto pass=[&](int target,int source,int mode){
                auto [tw,th]=size(target);auto [sw,sh2]=source<0?std::array<int,2>{w,h}:size(source);
                framebuffer(bloomFbo[target]);viewport(0,0,tw,th);bind(0,source<0?hdrResolveColor:bloomTexture[source]);
                fx.texel={1.f/sw,1.f/sh2};fx.mode=mode;draw_arrays(3);
            };
            pass(0,-1,0);for(int level=1;level<5;++level)pass(level,level-1,1);
            gl.blend=true;blend_func(D3D12_BLEND_ONE,D3D12_BLEND_ONE);for(int level=3;level>=0;--level)pass(level,level+1,2);gl.blend=false;viewport(0,0,w,h);
        }
        framebuffer(fxs.fxaa?ldrFbo:msaaFbo);bind(0,hdrResolveColor);bind_sampler(0,UiNearestSampler);
        if(compositeBlend){use(PComposite);fx.bloomEnabled=fxs.bloom;fx.toneMap=options.pbr;bind(1,fxs.bloom?bloomTexture[0]:white);bind_sampler(1,UiLinearSampler);}
        else use(PTonemap);
        draw_arrays(3);
        if(fxs.fxaa){framebuffer(msaaFbo);use(PFxaa);bind(0,ldrColor);bind_sampler(0,UiLinearSampler);fx.texel={1.f/w,1.f/h};draw_arrays(3);}
        framebuffer(msaaFbo);
    }
    void alpha_target(){
        if(alphaColor)return;
        alphaColor=create_texture(options.pbr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM,UINT(targetWidth),UINT(targetHeight),1,1,1,true);alphaDepth=create_texture(DXGI_FORMAT_R24G8_TYPELESS,UINT(targetWidth),UINT(targetHeight));alphaFbo={alphaColor,0,0,alphaDepth};
    }
    void portal_target(){
        if(portalColor)return;
        portalColor=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,UINT(targetWidth),UINT(targetHeight),1,1,1,true);portalDepth=create_texture(DXGI_FORMAT_R24G8_TYPELESS,UINT(targetWidth),UINT(targetHeight));portalFbo={portalColor,0,0,portalDepth};
    }
    std::shared_ptr<Tex> sampled_texture(const Image*image,TextureGPU&gpu){
        if(image->nativeStorageBytes==2){
            auto found=warps.find(image);
            if(found!=warps.end()){
                auto&w=found->second;
                if(w.serial==gpu.serial&&w.width==gpu.width&&w.height==gpu.height){if(mipsEnabled&&!gpu.dynamic&&!w.mipsValid){generate_mips(w.texture);w.mipsValid=true;}return w.texture;}
            }
        }
        if(mipsEnabled&&!gpu.dynamic&&!gpu.mipsValid){generate_mips(gpu.id);gpu.mipsValid=true;}
        return gpu.id;
    }
    DXGI_FORMAT texture_format(UICommand::TextureMode mode)const{return mode==UICommand::TextureMode::CursorKey16&&b5g5r5a1?DXGI_FORMAT_B5G5R5A1_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;}
    void upload_image(Tex&t,const Image&image,UICommand::TextureMode mode){
        if(mode==UICommand::TextureMode::CursorKey16){
            std::vector<std::uint16_t> packed(image.rgba.size()/4);
            for(std::size_t i=0;i<packed.size();++i){
                const auto*p=image.rgba.data()+i*4;
                // Original RGB24 -> unusual source R5G6B5, black key BEFORE A1RGB555 conversion (GL_UNSIGNED_SHORT_5_5_5_1 bit order).
                const std::uint16_t source=std::uint16_t((p[0]>>3)<<11|(p[1]>>3)<<6|(p[1]&1)<<5|(p[2]>>3));
                packed[i]=source?std::uint16_t((source&0xffc0)|((source&31)<<1)|1):0;
            }
            if(t.format==DXGI_FORMAT_B5G5R5A1_UNORM){
                for(auto&v:packed)v=std::uint16_t(((v>>1)&31)|(((v>>6)&31)<<5)|(((v>>11)&31)<<10)|((v&1)<<15));
                upload_subresource(t,0,0,packed.data(),image.width*2,image.width*2);
            }else{
                std::vector<std::uint8_t> expanded(image.rgba.size());auto unorm=[](unsigned c){return std::uint8_t((c*255+15)/31);};
                for(std::size_t i=0;i<packed.size();++i){const auto v=packed[i];expanded[i*4]=unorm((v>>11)&31);expanded[i*4+1]=unorm((v>>6)&31);expanded[i*4+2]=unorm((v>>1)&31);expanded[i*4+3]=(v&1)?255:0;}
                upload_subresource(t,0,0,expanded.data(),image.width*4,image.width*4);
            }
        }else if(mode==UICommand::TextureMode::CursorKey24){
            std::vector<std::uint8_t> keyed=image.rgba;
            for(std::size_t i=0;i<keyed.size();i+=4)keyed[i+3]=(keyed[i]|keyed[i+1]|keyed[i+2])?255:0;
            upload_subresource(t,0,0,keyed.data(),image.width*4,image.width*4);
        }else upload_subresource(t,0,0,image.rgba.data(),image.width*4,image.width*4);
    }
    std::shared_ptr<Tex> texture(const Image*image,bool dynamic=false,std::uint64_t serial=0,bool sampled=true,UICommand::TextureMode mode=UICommand::TextureMode::Default){
        if(!image)return white;
        if(image->width>std::uint32_t(maxTextureSize)||image->height>std::uint32_t(maxTextureSize))throw std::runtime_error("Game image exceeds GPU texture size");
        auto&gpu=textures[image][static_cast<std::size_t>(mode)];
        if(gpu.id){
            if(dynamic&&(gpu.serial!=serial||gpu.width!=image->width||gpu.height!=image->height||!gpu.dynamic)){
                check_image(*image);
                if(gpu.width!=image->width||gpu.height!=image->height)allocate_texture(*gpu.id,texture_format(mode),image->width,image->height,1,1,1,false);
                upload_image(*gpu.id,*image,mode);
                gpu.serial=serial;gpu.width=image->width;gpu.height=image->height;gpu.dynamic=true;gpu.alpha=true;gpu.mipsValid=false;
            }
            return sampled&&mode==UICommand::TextureMode::Default?sampled_texture(image,gpu):gpu.id;
        }
        check_image(*image);
        auto pack=options.packs&&!dynamic&&mode==UICommand::TextureMode::Default&&!turbulent.contains(image)?options.packs->find(*image):std::nullopt;
        if(pack&&pack->albedo&&(pack->albedo->width>std::uint32_t(maxTextureSize)||pack->albedo->height>std::uint32_t(maxTextureSize))){std::cerr<<"kabus22: texture pack: "<<texture_key_hex(texture_key(*image))<<" exceeds GPU texture size; using original\n";pack.reset();}
        const Image&source=pack&&pack->albedo?*pack->albedo:*image;
        const bool mipmapped=!dynamic&&mode==UICommand::TextureMode::Default; // GL generates these lazily once mipmapping is on
        auto id=create_texture(texture_format(mode),source.width,source.height,mipmapped?mip_count(source.width,source.height):1,1,1,mipmapped);
        upload_image(*id,source,mode);
        const bool mipsValid=mipsEnabled&&mipmapped;if(mipsValid)generate_mips(id);
        bool alpha=dynamic||mode!=UICommand::TextureMode::Default;for(std::size_t index=3;!alpha&&index<image->rgba.size();index+=4)alpha=image->rgba[index]!=255;
        auto map=[&](const std::shared_ptr<const Image>&m)->std::shared_ptr<Tex>{
            if(!m)return nullptr;auto t=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,m->width,m->height,mip_count(m->width,m->height),1,1,true);upload_image(*t,*m,mode);generate_mips(t);return t;
        };
        gpu={id,image->width,image->height,serial,dynamic,alpha,mipsValid,pack?map(pack->normal):nullptr,pack?map(pack->orm):nullptr,pack&&pack->roughness};
        return sampled&&mode==UICommand::TextureMode::Default?sampled_texture(image,gpu):gpu.id;
    }
    void prepare_turbulence(const Image*image,bool dynamic,std::uint64_t serial){
        if(!image)throw std::runtime_error("Turbulent game surface has no decoded bitmap");
        if(image->nativeStorageBytes==255)throw std::runtime_error("Native turbulent bitmap CPU storage is unavailable");
        if(image->nativeStorageBytes!=2)return;
        turbulent.insert(image);texture(image,dynamic,serial,false);const auto&original=textures.at(image)[0];
        if(image->height>image->width)throw std::runtime_error("Turbulent bitmap rows exceed native displacement table");
        if(image->width==1)return;
        const auto&frame=*currentFrame;
        int phase=static_cast<int>(static_cast<std::int64_t>(double(frame.turbulenceSpeed)*image->width*frame.animationTimeSeconds)&(image->width-1));
        auto&warp=warps[image];bool resized=warp.width!=image->width||warp.height!=image->height;
        if(!resized&&warp.phase==phase&&warp.range==frame.turbulenceRange&&warp.serial==original.serial)return;
        auto saved=save();
        gl.depthTest=gl.cull=gl.blend=gl.scissorTest=gl.stencilTest=false;gl.colorMask=true;
        if(!warp.texture)warp.texture=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,image->width,image->height,mip_count(image->width,image->height),1,1,true);
        else if(resized)allocate_texture(*warp.texture,DXGI_FORMAT_R8G8B8A8_UNORM,image->width,image->height,mip_count(image->width,image->height),1,1,true);
        turbulenceOffsets.resize(image->width);float amplitude=static_cast<float>(double(frame.turbulenceRange)*image->width*0.05),angleScale=static_cast<float>(6.28318/(image->width-1));
        for(std::uint32_t index=0;index<image->width;++index)turbulenceOffsets[index]=turbulence_offset(phase+index,amplitude,angleScale);
        auto up=dev.upload(turbulenceOffsets.size()*sizeof(std::int32_t),16);std::memcpy(up.cpu,turbulenceOffsets.data(),turbulenceOffsets.size()*sizeof(std::int32_t));
        auto offsets=dev.transient_view();D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Format=DXGI_FORMAT_UNKNOWN;v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;v.Buffer.FirstElement=up.offset/4;v.Buffer.NumElements=UINT(turbulenceOffsets.size());v.Buffer.StructureByteStride=4;dev.device->CreateShaderResourceView(up.resource,&v,offsets);
        bind_view(1,Kind::Buffer,nullptr,offsets,allSubresources,bufferRead);
        Fbo target{warp.texture};framebuffer(target);viewport(0,0,int(image->width),int(image->height));use(PWarp);
        warpCb.size[0]=int(image->width);warpCb.size[1]=int(image->height);warpCb.stride=int(1u<<(std::bit_width(image->width)-1));
        bind(0,original.id);bind_sampler(0,DynamicSceneSampler);draw_arrays(3);
        warp.mipsValid=mipsEnabled&&!original.dynamic;if(warp.mipsValid)generate_mips(warp.texture);warp.width=image->width;warp.height=image->height;warp.serial=original.serial;warp.phase=phase;warp.range=frame.turbulenceRange;
        restore(std::move(saved));
    }
    MeshGPU& mesh(const Model&model){auto it=meshes.find(&model);if(it!=meshes.end())return it->second;if(model.vertices.empty()&&!model.triangles.empty())throw std::runtime_error("Game mesh has triangles without vertices");
        std::vector<Vertex>centroidVertices;std::vector<Triangle>centroidTriangles;
        centroid_geometry(model,currentFrame&&currentFrame->world&&(&currentFrame->world->mesh==&model)?currentFrame->world:model.nativeWorld,centroidVertices,centroidTriangles);
        const auto vertices=centroidVertices.empty()?std::span<const Vertex>(model.vertices):std::span<const Vertex>(centroidVertices);
        MeshGPU gpu;gpu.vertices=static_buffer(vertices.data(),vertices.size_bytes());
        for(const auto&vertex:model.vertices)include_point(gpu.bounds,vertex.position);
        if(model.triangles.size()>std::size_t(std::numeric_limits<std::int32_t>::max()/3)||centroidTriangles.size()>std::size_t(std::numeric_limits<std::int32_t>::max()/3))throw std::runtime_error("Game mesh exceeds Direct3D 12 index capacity");
        gpu.batches.resize(std::max<std::size_t>(model.materials.size(),1));
        for(const auto&t:model.triangles){auto material=display_material_index(model.formatVersion,t.materialIndex);if(material>=gpu.batches.size())throw std::runtime_error("Game triangle references invalid material");for(auto index:t.indices)if(index>=model.vertices.size())throw std::runtime_error("Game triangle references invalid vertex");gpu.batches[material].count+=3;}
        for(const auto&t:centroidTriangles)gpu.batches[display_material_index(model.formatVersion,t.materialIndex)].centroidCount+=3;
        UINT indexCount=0;
        for(std::size_t material=0;material<gpu.batches.size();++material){auto&batch=gpu.batches[material];UINT count=batch.count,centroidCount=batch.centroidCount;batch={indexCount,0,static_cast<std::uint32_t>(material),0,centroidCount};indexCount+=count;}
        for(auto&batch:gpu.batches){const UINT count=batch.centroidCount;batch.centroidFirst=indexCount;batch.centroidCount=0;indexCount+=count;}
        std::vector<std::uint32_t> indices(indexCount);
        for(const auto&t:model.triangles){auto&batch=gpu.batches[display_material_index(model.formatVersion,t.materialIndex)];for(auto index:t.indices)indices[batch.first+batch.count++]=index;}
        for(const auto&t:centroidTriangles){auto&batch=gpu.batches[display_material_index(model.formatVersion,t.materialIndex)];for(auto index:t.indices)indices[batch.centroidFirst+batch.centroidCount++]=index;}
        gpu.indices=static_buffer(indices.data(),indices.size()*sizeof(std::uint32_t));
        gpu.batches.erase(std::remove_if(gpu.batches.begin(),gpu.batches.end(),[](const Batch&b){return !b.count;}),gpu.batches.end());
        if(model.formatVersion==7){gpu.primaryMax=static_cast<std::int16_t>(gpu.batches.empty()?0:gpu.batches.back().material);gpu.skinLayers=native_skin_layers(model,gpu.primaryMax);}
        std::vector<Vec3>tangents(model.vertices.size());derive_tangents(model,{},{},tangents);
        if(!centroidVertices.empty()){
            tangents.resize(centroidVertices.size());
            for(const auto&t:centroidTriangles)if(t.indices[0]>=model.vertices.size())tangents[t.indices[0]]=add(tangents[t.indices[0]],tangents[t.indices[1]]);
            for(std::size_t i=model.vertices.size();i<tangents.size();++i)tangents[i]=unit(tangents[i]);
        }
        gpu.tangents=static_buffer(tangents.data(),tangents.size()*sizeof(Vec3));
        if(!model.frames.empty()){
            if(model.vertices.empty())throw std::runtime_error("Animation frame has no model vertices");
            for(const auto&f:model.frames)if(f.positions.size()!=model.vertices.size()||(!f.normals.empty()&&f.normals.size()!=model.vertices.size()))throw std::runtime_error("Animation frame must supply complete decoded vertex pose");
            gpu.frameBounds.reserve(model.frames.size());for(const auto&frame:model.frames)gpu.frameBounds.push_back(position_bounds(frame.positions));
            const std::size_t texels=model.frames.size()*model.vertices.size()*3;
            if(texels>std::size_t(1)<<D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP)throw std::runtime_error("Game model animation exceeds GPU buffer capacity");
            static_assert(sizeof(Vec3)==3*sizeof(float));std::vector<Vec3> poses;poses.reserve(texels);
            for(const auto&f:model.frames){poses.insert(poses.end(),f.positions.begin(),f.positions.end());if(f.normals.empty()){for(const auto&vertex:model.vertices)poses.push_back(vertex.normal);}else poses.insert(poses.end(),f.normals.begin(),f.normals.end());derive_tangents(model,f.positions,f.normals,tangents);poses.insert(poses.end(),tangents.begin(),tangents.begin()+std::ptrdiff_t(model.vertices.size()));}
            gpu.poses=static_buffer(poses.data(),poses.size()*sizeof(Vec3),sizeof(Vec3));gpu.frameCount=int(model.frames.size());
        }
        return meshes.emplace(&model,std::move(gpu)).first->second;
    }
    SkyGPU& sky_texture(const Image&sourceImage){
        auto found=skies.find(&sourceImage);if(found!=skies.end())return found->second;
        auto pack=options.packs?options.packs->find(sourceImage):std::nullopt;
        const Image&image=pack&&pack->albedo?*pack->albedo:sourceImage;
        check_image(image);std::uint32_t frames=image.width==image.height*6?6:image.width==image.height*2?2:0;
        if(!frames)throw std::runtime_error("Bitmap is not a native sky strip");
        if(image.width/frames>std::uint32_t(maxTextureSize)||image.height>std::uint32_t(maxTextureSize))throw std::runtime_error("Sky face exceeds GPU texture size");
        if(frames==2&&!image.sourceDepth&&image.nativeStorageBytes==255)throw std::runtime_error("Native cloud bitmap storage depth is unavailable");
        std::uint32_t width=image.width/frames,height=image.height;bool convert=frames==2&&((image.sourceDepth>0&&image.sourceDepth<=16)||image.nativeStorageBytes==2);
        auto saved=save();gl.depthTest=gl.cull=gl.blend=gl.scissorTest=gl.stencilTest=false;gl.colorMask=true;
        const UINT mips=mip_count(width,height);
        SkyGPU sky;sky.frames=frames;auto original=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,width,height,mips,frames,1,true);
        for(std::uint32_t layer=0;layer<frames;++layer)upload_subresource(*original,0,layer,image.rgba.data()+std::size_t(layer)*width*4,width*4,image.width*4);
        generate_mips(original);sky.texture=original;
        if(convert){
            sky.texture=create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,width,height,mips,frames,1,true);
            use(PCloud);bind(0,original,Kind::Array);bind_sampler(0,DynamicSceneSampler);
            for(std::uint32_t w=width,h=height,level=0;;++level){
                viewport(0,0,int(w),int(h));cloudCb.level=int(level);
                for(std::uint32_t layer=0;layer<frames;++layer){Fbo target{sky.texture,level,layer};framebuffer(target);cloudCb.layer=int(layer);cloudCb.convertAlpha=layer>0;draw_arrays(3);}
                if(w==1&&h==1)break;w=std::max(w/2,1u);h=std::max(h/2,1u);
            }
        }
        restore(std::move(saved));
        return skies.emplace(&sourceImage,sky).first->second;
    }
    void draw_sky(const RenderFrame&frame,const Camera&camera,int width,int height){
        if(!camera.drawWorld||!frame.world||!(camera.layers&1))return;
        const Image*cube=nullptr,*dome=nullptr;
        for(const auto&material:frame.world->mesh.materials)if((material.flags&15)==5){
            if(!material.texture)throw std::runtime_error("SKY surface has no decoded native bitmap");
            const auto&image=*material.texture;if(image.width==image.height*6)cube=&image;else if(image.width==image.height*2)dome=&image;
        }
        const Image*image=cube?cube:dome;if(!image)return;
        auto&sky=sky_texture(*image);
        use(PScene);sv.skyMask=1;maskPass=true;gl.colorMask=false;gl.depthMask=false;
        gl.stencilTest=true;gl.stencilWrite=255;clear(false,false,true);gl.stencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;gl.stencilRef=1;gl.stencilPass=D3D12_STENCIL_OP_REPLACE;
        DrawInstance world;world.model=&frame.world->mesh;draw_instance(world,frame.skinBindings,&camera);
        if(frame.renderEntities&&!camera.noEntities)for(const auto&instance:frame.instances)if(!instance.viewSpace&&(instance.layers&camera.layers)&&(!camera.excludeEntityId||instance.objectId!=camera.excludeEntityId))draw_instance(instance,frame.skinBindings,&camera);
        maskPass=false;sv.skyMask=0;gl.colorMask=true;gl.stencilWrite=0;gl.stencilFunc=D3D12_COMPARISON_FUNC_EQUAL;gl.stencilPass=D3D12_STENCIL_OP_KEEP;
        gl.depthTest=false;gl.cull=false;gl.blend=true;blend_func_separate(D3D12_BLEND_SRC_ALPHA,D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_ONE,D3D12_BLEND_INV_SRC_ALPHA);
        use(PSky);bind(0,sky.texture,Kind::Array);bind_sampler(0,cube?SkyClampSampler:SceneSampler);
        Matrix rotation=transform({},camera.angles,{1,1,1});skyCb.basis={F4{rotation[0],rotation[1],rotation[2],0},F4{rotation[4],rotation[5],rotation[6],0},F4{rotation[8],rotation[9],rotation[10],0}};
        skyCb.cube=cube!=nullptr;skyCb.blend=frame.skyBlend;
        float horizontal=camera.horizontalFov<0?-1.f:1.f,vertical=camera.pixelAspect<0?-1.f:1.f;
        if(cube){float tangent=std::tan(rad(std::abs(camera.horizontalFov))*.5f)/(camera.widescreen?camera.referenceAspect:float(width)/height)*std::abs(camera.pixelAspect);horizontal*=2*tangent*float(width)/height/std::abs(camera.pixelAspect);vertical*=2*tangent;}
        skyCb.cone={horizontal,vertical};skyCb.curve=frame.skyCurve*4;
        float argument=frame.skyClip*0.01745329238474369f;skyCb.clip=static_cast<float>(double(static_cast<float>(std::sin(double(argument))))-0.1);
        double time=frame.animationTimeSeconds;float clock=static_cast<float>((time-static_cast<std::int32_t>(time/1024.0-1024.0))*65536.0);
        for(std::uint32_t layer=0;layer<(cube?1u:sky.frames);++layer){
            skyCb.layer=int(layer);skyCb.scroll={float(frame.skySpeed.x*(layer+1)*clock/(double(image->width/sky.frames)*65536)),float(frame.skySpeed.y*(layer+1)*clock/(double(image->height)*65536))};draw_arrays(3);
        }
        gl.stencilTest=false;gl.stencilWrite=255;gl.depthMask=true;gl.depthTest=true;use(PScene);bind_sampler(0,SceneSampler);
    }
    void prepare_door(const Model&model){
        mesh(model);
        for(const auto&mat:model.materials){
            const Image*image=mat.texture.get();const int type=mat.flags&15;
            if(type==5){if(image&&(image->width==image->height*6||image->width==image->height*2))sky_texture(*image);continue;}
            if(type!=9)texture(image);
            texture(mat.lightmap.get());
        }
    }
    bool door_view(const RenderFrame&f,const Camera&camera,bool eligible){
        const auto&d=f.door;
        const bool active=eligible&&d.world&&camera.doorPortal&&d.halfWidth>0&&d.halfHeight>0&&dot(add(camera.position,mul(d.center,-1)),d.normal)>0;
        sv.doorMode=active;
        if(active){sv.doorCenter=f3(d.center);sv.doorNormal=f3(d.normal);sv.doorRight=f3(d.right);sv.doorUp=f3(d.up);sv.doorHalf={d.halfWidth,d.halfHeight};}
        return active;
    }
    void draw_door(const RenderFrame&f,const Camera&camera,Color skyColor,bool portalEnabled){
        DrawInstance door;door.model=&f.door.world->mesh;door.position=f.door.position;door.angles=f.door.angles;
        sv.doorMode=2;sv.doorSkyColor=f3(skyColor);sv.lightCount=0;sv.portalEnabled=0;
        if(shadowMapsOn)sv.shadowMaps=0;
        doorPass=true;draw_instance(door,f.skinBindings,&camera);doorPass=false;
        sv.doorMode=1;sv.lightCount=int(lightData.size()/5);sv.portalEnabled=portalEnabled;
        if(shadowMapsOn)sv.shadowMaps=shadowsReady;
    }
    void model_uniform(const Matrix&m){
        sd.model=m;
        for(int col=0;col<3;++col){float squared=m[col*4]*m[col*4]+m[col*4+1]*m[col*4+1]+m[col*4+2]*m[col*4+2];sd.normalMatrix[col]={squared>0?m[col*4]/squared:0,squared>0?m[col*4+1]/squared:0,squared>0?m[col*4+2]/squared:0,0};}
    }
    void material_uniforms(const ScriptMaterialState*material){
        sd.explicitMaterial=material&&currentFrame&&currentFrame->hardwareLighting;sd.effectKind=material?static_cast<int>(material->effect):0;
        if(!material)return;
        constexpr float channel=1.0f/255;
        sd.preparedAmbient={material->ambient.r*channel,material->ambient.g*channel,material->ambient.b*channel,1};
        sd.preparedDiffuse={material->diffuse.r*channel,material->diffuse.g*channel,material->diffuse.b*channel};
        sd.preparedSpecular={material->specular.r*channel,material->specular.g*channel,material->specular.b*channel};
        sd.preparedEmissive={material->emissive.r*channel,material->emissive.g*channel,material->emissive.b*channel};
        sd.materialPower=material->power;
        if(material->effect!=EffectKind::None){
            std::array<float,8>skills;for(std::size_t index=0;index<skills.size();++index)skills[index]=std::bit_cast<float>(material->skillBits[index]);
            sd.skillMain={skills[0],skills[1],skills[2],skills[3]};sd.skillExtra={skills[4],skills[5],skills[6],skills[7]};
        }
    }
    void forget_effect_texture(const std::shared_ptr<Tex>&t){
        if(!t)return;
        for(auto&[id,state]:effectBindings)for(auto&bank:state.banks)if(bank.texture==t)bank={};
    }
    EffectBindings& effect_banks(const ScriptMaterialState&material,const SkinBinding&color,const SkinBinding&normal){
        auto&state=effectBindings[material.sourceId];
        if(state.generation!=material.effectGeneration||state.kind!=material.effect)state={material.effectGeneration,material.effect,{}};
        const std::array<const SkinBinding*,2>bindings{&color,&normal};
        for(std::size_t index=0;index<2;++index){
            auto&bank=state.banks[index];const auto&binding=*bindings[index];
            if(binding.image){auto id=texture(binding.image,binding.dynamic,binding.serial);const auto&gpu=textures.at(binding.image)[0];bank={id,gpu.width,gpu.height,gpu.dynamic,gpu.alpha};}
            // Original binder skips NULL parameters; GL texture object 0 samples as opaque black.
            const int unit=index==0?0:5;bind(unit,bank.texture?bank.texture:zero);bind_sampler(unit,bank.dynamic?UiLinearSampler:LightmapSampler);
        }
        return state;
    }
    unsigned model_queue(const DrawInstance&d,std::span<const SkinBinding>bindings){
        unsigned queues=0;
        for(const auto&batch:mesh(*d.model).batches){
            const bool subsets=d.model->formatVersion==7;
            const Material*mat=subsets&&batch.material>0?&d.model->materials[batch.material]:model_material(*d.model,batch.material,d.skinIndex);
            if(subsets&&batch.material>0&&(!mat->texture||!mat->nativePrimarySubset))continue;
            auto skin=resolved_skin(mat&&mat->texture?mat->texture.get():nullptr,bindings);
            queues|=model_translucent(d,skin.nativeTextureAlpha)?2u:1u;
        }
        return queues;
    }
    // GL default_attributes: the mesh's own streams (both interpolation endpoints from the vertex buffer).
    void default_attributes(MeshGPU&gpu){
        vertex_buffer(slotPosition,gpu.vertices,offsetof(Vertex,position),sizeof(Vertex));vertex_buffer(slotNormal,gpu.vertices,offsetof(Vertex,normal),sizeof(Vertex));vertex_buffer(slotUv,gpu.vertices,offsetof(Vertex,uv),sizeof(Vertex));
        vertex_buffer(slotNextPosition,gpu.vertices,offsetof(Vertex,position),sizeof(Vertex));vertex_buffer(slotNextNormal,gpu.vertices,offsetof(Vertex,normal),sizeof(Vertex));
        vertex_buffer(slotTangent,gpu.tangents,0,sizeof(Vec3));vertex_buffer(slotNextTangent,gpu.tangents,0,sizeof(Vec3));
        vertex_buffer(slotParticle,dummyInstance,0,sizeof(ParticleGPU));ib=gpu.indices;
    }
    void update_buffer(std::shared_ptr<Buf>&buffer,std::span<const Vec3> data){
        if(!buffer||buffer->size!=std::max<UINT64>(data.size_bytes(),4))buffer=create_buffer(data.size_bytes());
        upload_buffer(*buffer,data.data(),data.size_bytes());
    }
    void bind_override(const DrawInstance&d,MeshGPU&gpu,EffectKind liveEffectKind,std::uint32_t id,std::uint64_t revision,std::span<const Vec3>positions,std::span<const Vec3>normals,UINT positionSlot,UINT normalSlot,UINT tangentSlot,std::uint32_t frameIndex){
        if(positions.empty()&&normals.empty())return;
        if(!id||(!positions.empty()&&positions.size()!=d.model->vertices.size())||(!normals.empty()&&normals.size()!=d.model->vertices.size()))throw std::runtime_error("Invalid mutable entity frame override");
        auto&data=overrides[id];
        if(data.revision!=revision||data.count!=positions.size()||data.normalCount!=normals.size()||data.frame!=frameIndex||data.model!=d.model){
            if(!positions.empty())update_buffer(data.positions,positions);
            if(!normals.empty())update_buffer(data.normals,normals);
            set_override_pose(data,*d.model,revision,positions.size(),normals.size(),frameIndex);
        }
        if(override_tangents_needed(data,liveEffectKind,revision)){
            auto posePositions=positions,poseNormals=normals;
            if(gpu.frameCount){const auto&frame=d.model->frames[frameIndex];if(posePositions.empty())posePositions=frame.positions;if(poseNormals.empty())poseNormals=frame.normals;}
            data.tangentData.resize(d.model->vertices.size());derive_tangents(*d.model,posePositions,poseNormals,data.tangentData);
            update_buffer(data.tangents,data.tangentData);
            data.tangentRevision=revision;data.tangentReady=true;
        }
        if(!positions.empty())vertex_buffer(positionSlot,data.positions,0,sizeof(Vec3));
        if(!normals.empty())vertex_buffer(normalSlot,data.normals,0,sizeof(Vec3));
        if(liveEffectKind!=EffectKind::None)vertex_buffer(tangentSlot,data.tangents,0,sizeof(Vec3));
    }
    void draw_instance(const DrawInstance&d,std::span<const SkinBinding> skinBindings,const Camera*camera=nullptr,bool depthWrite=true,int nativeQueue=-1){
        if((!d.visible&&!d.shadowRequested)||!d.model||d.scale.x==0||d.scale.y==0||d.scale.z==0||(maskPass&&(!d.visible||d.model->formatVersion!=6)))return;
        auto&gpu=mesh(*d.model);Vec3 angles=d.angles;
        if(gpu.frameCount&&(d.frame>=static_cast<std::uint32_t>(gpu.frameCount)||d.nextFrame>=static_cast<std::uint32_t>(gpu.frameCount)))throw std::runtime_error("Entity animation index outside model");
        if(d.billboard&&camera)angles=d.viewSpace?Vec3{180,0,d.angles.z}:Vec3{camera->angles.x+180,-camera->angles.y,d.angles.z-camera->angles.z};
        Matrix m=transform(d.position,angles,d.scale);
        if(d.viewSpace&&camera)m=product(transform(camera->position,camera->angles,{1,1,1}),m);
        auto poseBounds=[&](std::uint32_t id,std::uint64_t revision,std::span<const Vec3>positions,std::uint32_t frame){
            if(positions.empty())return gpu.frameCount?gpu.frameBounds[frame]:gpu.bounds;
            if(!id||positions.size()!=d.model->vertices.size())throw std::runtime_error("Invalid mutable entity position override");
            return mesh_override_bounds(overrides[id],*d.model,revision,positions);
        };
        if(gpu.batches.empty())return;
        if(camera){
            auto first=poseBounds(d.meshId,d.meshRevision,d.positions,d.frame);
            auto next=!gpu.frameCount&&d.nextPositions.empty()?first:poseBounds(d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextFrame);
            if(!geometry_eligible(interpolated_bounds(first,next,d.interpolation),m,viewProjection,geometryClipPlane))return;
        }
        const auto light=!maskPass&&!casterPass&&camera&&currentFrame&&currentFrame->prepareEntityLight?currentFrame->prepareEntityLight(currentFrame->lightContext,d,*camera):NativeEntityLightState{d.nativeLightScalar,d.nativeLightDelta,d.nativeLightBias};
        if(!d.visible)return;
        const auto*live=script_material(currentFrame,d.materialIndex);
        if(d.alpha<=0&&(!live||live->effect==EffectKind::None))return;
        sd.particleInstanced=0;material_uniforms(live);
        const EffectKind liveEffectKind=live?live->effect:EffectKind::None;
        model_uniform(m);default_attributes(gpu);
        const EffectKind tangentKind=options.pbr&&liveEffectKind==EffectKind::None?EffectKind::NormalMix:liveEffectKind;
        bind_override(d,gpu,tangentKind,d.meshId,d.meshRevision,d.positions,d.normals,slotPosition,slotNormal,slotTangent,d.frame);
        bind_override(d,gpu,tangentKind,d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextNormals,slotNextPosition,slotNextNormal,slotNextTangent,d.nextFrame);
        sd.firstOverride=!d.positions.empty();sd.nextOverride=!d.nextPositions.empty();sd.firstNormalOverride=!d.normals.empty();sd.nextNormalOverride=!d.nextNormals.empty();
        sd.interpolation=std::clamp(d.interpolation,0.0f,1.0f);
        sd.vertexCount=gpu.frameCount?int(d.model->vertices.size()):0;
        if(gpu.frameCount){if(d.frame>=static_cast<std::uint32_t>(gpu.frameCount)||d.nextFrame>=static_cast<std::uint32_t>(gpu.frameCount))throw std::runtime_error("Entity animation frame index outside model");sd.frame0=int(d.frame);sd.frame1=int(d.nextFrame);bind_buffer(2,gpu.poses);}
        sd.unlit=d.unlit;gl.cull=!(d.doubleSided||d.billboard||(camera&&camera->noCull));blend_func_separate(D3D12_BLEND_SRC_ALPHA,d.additive?D3D12_BLEND_ONE:D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_ONE,D3D12_BLEND_INV_SRC_ALPHA);
        bool flipped=camera&&!casterPass&&((camera->horizontalFov<0)!=(camera->pixelAspect<0));flipped^=d.model->clockwise;flipped^=d.scale.x*d.scale.y*d.scale.z<0;gl.frontCCW=!flipped;
        sd.normalOrientation=d.model->clockwise?-1.f:1.f;sd.noFog=d.noFog;
        sd.flare=d.flare;sd.overlay=d.overlay;
        sd.invertFlare=d.invertFlare;sd.spriteFrames=int(d.spriteFrames);
        if(d.spriteFrames>1){
            if(d.frame>=d.spriteFrames||d.nextFrame>=d.spriteFrames)throw std::runtime_error("Sprite frame index outside texture strip");
            sd.frame0=int(d.frame);sd.frame1=int(d.nextFrame);sd.interpolation=std::clamp(d.interpolation,0.0f,1.0f);
        }
        const bool nativeSubsets=camera&&camera->perDrawOpacity&&d.model->formatVersion==7;
        const bool nativeModelPass=camera&&camera->perDrawOpacity&&model_has_entity_skins(*d.model)&&!maskPass;
        const bool recordCasters=casterPass&&casterWorld&&!worldCastersReady;bool cacheable=true;
        if(casterPass&&casterWorld&&worldCastersReady){draw_world_casters();return;}
        for(const auto&b:gpu.batches){
            const Material*mat=nativeSubsets&&b.material>0?&d.model->materials[b.material]:model_material(*d.model,b.material,d.skinIndex);
            if(nativeSubsets&&b.material>0&&(!mat->texture||!mat->nativePrimarySubset))continue;
            bool worldSurface=d.model->formatVersion==6;int type=mat?mat->flags&15:0;
            if(maskPass?(!worldSurface||!mat||type!=5):(worldSurface&&mat&&((type==5&&!doorPass)||type==11||(portalPass&&(mat->flags&0x2000)))))continue;
            if(worldSurface&&mat&&mat->raw.size()!=64)throw std::runtime_error("WMB material lacks native surface metadata");
            if(maskPass){draw_elements(b.count,b.first);continue;}
            if(doorPass&&type==5){sd.doorSky=1;draw_elements(b.count,b.first);sd.doorSky=0;continue;}
            int nativeAmbient=worldSurface&&mat?static_cast<std::int8_t>(mat->raw[42]):0,nativeAlbedo=worldSurface&&mat?static_cast<std::int8_t>(mat->raw[43]):0;
            float reflection=worldSurface&&mat&&(mat->flags&0x2000)?surface_reflection(nativeAlbedo,d.reflectivity,d.objectId!=0):0;
            sd.reflectionWeight=reflection;sd.surfaceAmbient=surface_ambient(nativeAmbient,camera?static_cast<int>(camera->nativeAmbient):0,d.objectId!=0);
            sd.unlit=d.unlit||(currentFrame&&!currentFrame->hardwareLighting)||(worldSurface&&mat&&(mat->flags&15)==3);
            if(mat&&!mat->textureName.empty()&&!mat->texture)throw std::runtime_error("Unresolved game skin: "+mat->textureName);
            if(mat&&!mat->lightmapName.empty()&&!mat->lightmap)throw std::runtime_error("Unresolved game lightmap: "+mat->lightmapName);
            const auto entitySkin=resolved_skin(mat&&mat->texture?mat->texture.get():nullptr,skinBindings);
            if(nativeModelPass&&nativeQueue>=0&&model_translucent(d,entitySkin.nativeTextureAlpha)!=(nativeQueue==1))continue;
            if(nativeModelPass){
                const auto pass=model_pass_state(d,*camera,entitySkin.nativeTextureAlpha,depthWrite);
                gl.blend=pass.blend;blend_func(D3D12_BLEND_SRC_ALPHA,pass.additive?D3D12_BLEND_ONE:D3D12_BLEND_INV_SRC_ALPHA);gl.depthMask=pass.depthWrite;
            }
            const auto skin=entitySkin.image?entitySkin:resolved_skin(live?live->skins[0]:nullptr,skinBindings);
            const Image*image=skin.image;bool dynamicSkin=skin.dynamic;std::uint64_t skinSerial=skin.serial;
            sd.overlay=overlay_keyed(d,image);
            if(casterPass&&!(live&&live->effect!=EffectKind::None)){
                const auto tex=texture(image,dynamicSkin,skinSerial);const std::uint8_t samplerId=image&&textures.at(image)[0].dynamic?DynamicSceneSampler:SceneSampler;const bool textureAlpha=image&&textures.at(image)[0].alpha;
                Color t=live?Color{1,1,1,d.tint.a}:d.tint;if(mat&&!worldSurface){t.r*=mat->albedo.r;t.g*=mat->albedo.g;t.b*=mat->albedo.b;t.a*=mat->albedo.a;}
                const float opacity=draw_opacity(d.alpha*(mat?mat->alpha:1),camera);
                const Vec2 uvScale=model_uv_scale(d.model->formatVersion,mat?mat->uvScale:Vec2{1,1},image),uvOffset=mat?mat->uvOffset:Vec2{};
                const bool centroid=centroidLighting&&b.centroidCount;const UINT count=centroid?b.centroidCount:b.count,first=centroid?b.centroidFirst:b.first;
                if(recordCasters){
                    auto same=[&](const CasterGroup&g){return g.texture==tex&&g.sampler==samplerId&&g.textureAlpha==textureAlpha&&g.tint.r==t.r&&g.tint.g==t.g&&g.tint.b==t.b&&g.tint.a==t.a&&g.alpha==opacity&&g.uvScale.x==uvScale.x&&g.uvScale.y==uvScale.y&&g.uvOffset.x==uvOffset.x&&g.uvOffset.y==uvOffset.y;};
                    auto group=std::find_if(worldCasters.begin(),worldCasters.end(),same);
                    if(group==worldCasters.end())group=worldCasters.insert(worldCasters.end(),CasterGroup{tex,samplerId,textureAlpha,t,opacity,uvScale,uvOffset,{},{}});
                    group->counts.push_back(count);group->firsts.push_back(first);continue;
                }
                bind(0,tex);bind_sampler(0,samplerId);sd.textureAlpha=textureAlpha;
                sd.tint={t.r,t.g,t.b,t.a};sd.alpha=opacity;sd.uvScale={uvScale.x,uvScale.y};sd.uvOffset={uvOffset.x,uvOffset.y};
                draw_elements(count,first);
                continue;
            }
            if(recordCasters)cacheable=false;
            if(worldSurface&&mat&&(mat->flags&15)==9)prepare_turbulence(image,dynamicSkin,skinSerial);
            bool effect=live&&live->effect!=EffectKind::None;EffectBindings*retained=nullptr;
            if(effect){
                const Image*normalImage=nullptr;
                if(model_has_entity_skins(*d.model)){
                    int following=nativeSubsets?gpu.primaryMax+1:static_cast<int>(d.skinIndex)+1;
                    if(following>=0&&std::size_t(following)<d.model->materials.size()){
                        if(nativeSubsets&&gpu.skinLayers>=2)following=static_cast<int>(b.material)+1;
                        if(std::size_t(following)<d.model->materials.size())normalImage=d.model->materials[following].texture.get();
                    }
                }
                if(!normalImage)normalImage=live->skins[1];
                auto normal=resolved_skin(normalImage,skinBindings);retained=&effect_banks(*live,skin,normal);
                sd.textureAlpha=retained->banks[0].alpha;
            }else{
                bind(0,texture(image,dynamicSkin,skinSerial));bind_sampler(0,image&&textures.at(image)[0].dynamic?DynamicSceneSampler:SceneSampler);
                sd.textureAlpha=image&&textures.at(image)[0].alpha;
                if(options.pbr){
                    const TextureGPU*maps=image?&textures.at(image)[0]:nullptr;
                    bind(6,maps&&maps->normal?maps->normal:flatNormal);bind_sampler(6,SceneSampler);
                    bind(7,maps&&maps->orm?maps->orm:defaultOrm);bind_sampler(7,SceneSampler);
                    sd.roughnessMap=maps&&maps->roughness;sd.legacyRoughness=legacy_roughness(mat?mat->power:0);
                }
            }
            bind(1,texture(mat?mat->lightmap.get():nullptr));sd.hasLightmap=mat&&mat->lightmap;
            sd.worldVertexLighting=worldSurface&&!d.unlit&&type!=3&&!maskPass;
            Color t=live?Color{1,1,1,d.tint.a}:d.tint;if(mat&&!worldSurface){t.r*=mat->albedo.r;t.g*=mat->albedo.g;t.b*=mat->albedo.b;t.a*=mat->albedo.a;}
            sd.tint={t.r,t.g,t.b,t.a};sd.alpha=draw_opacity(d.alpha*(mat?mat->alpha:1),camera);
            int viewAmbient=camera?static_cast<int>(camera->nativeAmbient):0;
            sd.materialAmbient=worldSurface?surface_light(nativeAmbient,viewAmbient,d.objectId!=0)/255.0f:model_ambient(d.ambient,viewAmbient);
            if(live&&!effect){auto ambient=material_ambient_light(d,camera,&light);sd.ambientLight=f3(ambient);sd.materialSunStrength=material_light_strength(live->albedoRaw,light.scalar,currentFrame&&currentFrame->bonusProfile,!currentFrame||currentFrame->nativeFixedLightBank);}
            Vec2 baseScale=mat?mat->uvScale:Vec2{1,1};
            Vec2 uvScale=retained&&retained->banks[0].texture?model_uv_scale(d.model->formatVersion,baseScale,retained->banks[0].width,retained->banks[0].height):model_uv_scale(d.model->formatVersion,baseScale,image);
            sd.uvScale={uvScale.x,uvScale.y};sd.uvOffset={mat?mat->uvOffset.x:0,mat?mat->uvOffset.y:0};
            if(retained){const auto&bank=retained->banks[1];auto scale=bank.texture?model_uv_scale(d.model->formatVersion,baseScale,bank.width,bank.height):baseScale;sd.normalUvScale={scale.x,scale.y};}
            const bool centroid=centroidLighting&&b.centroidCount;
            draw_elements(centroid?b.centroidCount:b.count,centroid?b.centroidFirst:b.first);
        }
        if(recordCasters){draw_world_casters();worldCastersReady=cacheable;if(!cacheable)worldCasters.clear();}
        if(nativeModelPass){gl.blend=true;gl.depthMask=depthWrite;}
    }
    void draw_planar(const DrawInstance&d,const Camera&camera,int width,int height){
        const auto&frame=*currentFrame;
        if(!d.model||(d.model->formatVersion!=4&&d.model->formatVersion!=5&&d.model->formatVersion!=7)||d.viewSpace||d.scale.x==0||d.scale.y==0||d.scale.z==0)return;
        auto&gpu=mesh(*d.model);if(gpu.batches.empty())return;
        if(gpu.frameCount&&(d.frame>=std::uint32_t(gpu.frameCount)||d.nextFrame>=std::uint32_t(gpu.frameCount)))throw std::runtime_error("Shadow animation index outside selected source model");
        auto bounds=[&](std::uint32_t id,std::uint64_t revision,std::span<const Vec3>positions,std::uint32_t frameIndex){
            if(positions.empty())return gpu.frameCount?gpu.frameBounds[frameIndex]:gpu.bounds;
            if(!id||positions.size()!=d.model->vertices.size())throw std::runtime_error("Invalid mutable shadow position override");
            return mesh_override_bounds(overrides[id],*d.model,revision,positions);
        };
        const auto first=bounds(d.meshId,d.meshRevision,d.positions,d.frame);
        const auto next=!gpu.frameCount&&d.nextPositions.empty()?first:bounds(d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextFrame);
        const auto poseBounds=interpolated_bounds(first,next,d.interpolation);
        const auto model=transform(d.position,d.angles,d.scale);
        const auto shadow=frame.prepareEntityLight?frame.prepareEntityLight(frame.lightContext,d,camera).shadow:d.nativeShadow;
        if(!shadow.caster)return;
        const auto projection=planar_projection(d,shadow,frame);
        if(!geometry_eligible(poseBounds,product(projection,model),viewProjection,geometryClipPlane))return;
        use(PShadow);default_attributes(gpu);
        bind_override(d,gpu,EffectKind::None,d.meshId,d.meshRevision,d.positions,d.normals,slotPosition,slotNormal,slotTangent,d.frame);
        bind_override(d,gpu,EffectKind::None,d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextNormals,slotNextPosition,slotNextNormal,slotNextTangent,d.nextFrame);
        sh.model=model;sh.projection=projection;sh.viewProjection=viewProjection;
        sh.vertexCount=gpu.frameCount?int(d.model->vertices.size()):0;sh.frame0=int(d.frame);sh.frame1=int(d.nextFrame);
        sh.interpolation=std::clamp(d.interpolation,0.f,1.f);sh.firstOverride=!d.positions.empty();sh.nextOverride=!d.nextPositions.empty();
        if(gpu.frameCount)bind_buffer(2,gpu.poses);
        sh.clipPlane=geometryClipPlane;sh.viewport={float(width),float(height)};sh.areaThreshold=-2.f*frame.clipSize;
        const float materialPercent=frame.shadowAlpha*0.01f;
        const auto alphaByte=std::uint8_t(std::int64_t(std::trunc(materialPercent*camera.opacityByte)));
        sh.alpha=alphaByte/255.f;
        const auto fog=camera.environmentOverride?camera.fogColor:frame.fogColor;
        sh.cameraPosition=f3(camera.position);sh.fogColor=f3(fog);
        sh.fogRange={camera.environmentOverride?camera.fogStart:frame.fogStart,camera.environmentOverride?camera.fogEnd:frame.fogEnd};sh.noFog=d.noFog;
        bool reversed=(camera.horizontalFov<0)!=(camera.pixelAspect<0);reversed^=d.model->clockwise;reversed^=d.scale.x*d.scale.y*d.scale.z<0;sh.reversed=reversed;
        gl.cull=false;gl.depthTest=true;gl.depthMask=true;gl.blend=true;blend_func_separate(D3D12_BLEND_SRC_ALPHA,D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_ONE,D3D12_BLEND_INV_SRC_ALPHA);
        for(const auto&batch:gpu.batches)draw_elements(batch.count,batch.first);
        use(PScene);
    }
    void post_state(){gl.scissorTest=false;gl.blend=false;gl.depthTest=false;gl.cull=false;viewport(0,0,targetWidth,targetHeight);}
    // Final pass (gamma ramp) from the resolved frame into `target` without the row flip: the top-down presented image.
    void post_draw(const Fbo&target){
        framebuffer(target);use(PPresent);
        bind(0,resolveColor);bind_sampler(0,UiLinearSampler);postCb.enabled=gamma.enabled;
        bind(1,gammaTexture);bind_sampler(1,UiNearestSampler);
        draw_arrays(3);
    }
    void quad(Rect r,Rect uv,Color c,float angle=0){float cx=r.x+r.width*.5f,cy=r.y+r.height*.5f,cs=std::cos(rad(angle)),sn=std::sin(rad(angle));const float u0=std::min(uv.x,uv.x+uv.width),v0=std::min(uv.y,uv.y+uv.height),u1=std::max(uv.x,uv.x+uv.width),v1=std::max(uv.y,uv.y+uv.height);UIVertex v[4];for(int i=0;i<4;++i){float x=(i==1||i==2?r.width:0)-r.width*.5f,y=(i>=2?r.height:0)-r.height*.5f;v[i]={cx+x*cs-y*sn,cy+x*sn+y*cs,uv.x+(i==1||i==2?uv.width:0),uv.y+(i>=2?uv.height:0),c.r,c.g,c.b,c.a,u0,v0,u1,v1};}for(int i:{0,1,2,0,2,3})uiVertices.push_back(v[i]);}
    void flush_ui(const std::shared_ptr<Tex>&image){
        if(uiVertices.empty())return;bind(0,image);
        const UINT bytes=UINT(uiVertices.size()*sizeof(UIVertex));auto up=dev.upload(bytes,16);std::memcpy(up.cpu,uiVertices.data(),bytes);
        auto&v=vb[0];v.buffer.reset();v.upload=true;v.view={up.gpu,bytes,sizeof(UIVertex)};
        draw_arrays(UINT(uiVertices.size()));uiVertices.clear();
    }
    void ui_command(const UICommand&c){
        blend_func(D3D12_BLEND_SRC_ALPHA,c.additive?D3D12_BLEND_ONE:D3D12_BLEND_INV_SRC_ALPHA);
        bind_sampler(0,c.filter?UiLinearSampler:UiNearestSampler);uiCb.overlay=c.overlay;uiCb.alphaRef=c.alphaRef;
        uiCb.textKey=c.kind==UICommand::Kind::Text;
        uiCb.cursorKey=c.cursorOverlay||c.textureMode!=UICommand::TextureMode::Default;
        uiVertices.clear();std::shared_ptr<Tex> image=white;
        if(c.kind==UICommand::Kind::Image){if(!c.image)throw std::runtime_error("UI image command has no decoded game image");image=texture(c.image,c.dynamicImage,c.imageSerial,true,c.textureMode);quad(c.rect,c.uv,c.color,c.angle);}
        else if(c.kind==UICommand::Kind::Solid)quad(c.rect,{0,0,1,1},c.color,c.angle);
        else{
            if(!c.font)throw std::runtime_error("UI text command has no font atlas");
            image=texture(&c.font->image);float x=c.rect.x,y=c.rect.y;
            for(unsigned char ch:c.text){
                if(ch=='\n'){x=c.rect.x;y+=c.font->lineHeight*c.scaleY;continue;}
                const auto&g=c.font->glyphs[ch];
                if(g.pixels.width&&g.pixels.height)quad({x+g.bearing.x*c.scaleX,y+(c.font->ascent-g.bearing.y)*c.scaleY,g.pixels.width*c.scaleX,g.pixels.height*c.scaleY},{g.pixels.x/c.font->image.width,g.pixels.y/c.font->image.height,g.pixels.width/c.font->image.width,g.pixels.height/c.font->image.height},c.color);
                x+=(g.advance+c.advanceDelta)*c.scaleX;
            }
        }
        flush_ui(image);
    }
    void upload_lights(){
        const std::size_t count=std::max<std::size_t>(lightData.size(),1);auto up=dev.upload(count*16,16);
        if(lightData.empty())std::memset(up.cpu,0,16);else std::memcpy(up.cpu,lightData.data(),lightData.size()*16);
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Format=DXGI_FORMAT_UNKNOWN;v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;v.Buffer.FirstElement=up.offset/16;v.Buffer.NumElements=UINT(count);v.Buffer.StructureByteStride=16;
        dev.device->CreateShaderResourceView(up.resource,&v,lightsSrv);
    }
};
D3D12Backend::D3D12Backend(SDL_Window*w,const RenderOptions&o):impl_(std::make_unique<Impl>(w,o)){}
D3D12Backend::~D3D12Backend()=default;
void D3D12Backend::drain(){impl_->drain_completion();}
void D3D12Backend::invalidate(WindowMode mode){impl_->options.mode=mode;impl_->finalFrameValid=false;}
int D3D12Backend::shader_version()const{return 2020;} // Effect materials (NormalMix/NormalSunMix) are compiled into the scene shaders.
std::string D3D12Backend::description()const{const auto&i=*impl_;return "Direct3D 12 feature level "+i.feature_level_name()+", shader model "+i.shader_model_name()+"; "+i.dev.adapterName;}
PresentationState D3D12Backend::presentation_state()const{const auto&i=*impl_;return {i.completionLimit,unsigned(i.completion.size()),i.completionSubmissions,i.completionWaits,i.requestedSwapInterval<0?(i.options.vsync?1:0):i.requestedSwapInterval,i.targetWidth?i.actualSamples:i.samples};}
const RenderOptions& D3D12Backend::options()const{return impl_->options;}
bool D3D12Backend::reconfigure(const RenderOptions&next){bool reload{};if(!impl_->reconfigure(next,reload))return false;if(reload)release_scene();return true;}
void D3D12Backend::purge_model(const Model*model){
    auto&i=*impl_;i.drain_completion();i.meshes.erase(model);
    purge_model_overrides(i.overrides,model);
    if(model)for(const auto&material:model->materials){
        if(material.texture&&material.texture.use_count()==1)purge_image(material.texture.get());
        if(material.lightmap&&material.lightmap.use_count()==1)purge_image(material.lightmap.get());
    }
}
void D3D12Backend::release_scene(std::span<const Model* const> keep){
    auto&i=*impl_;
    i.drain_completion();
    i.retainedColorValid=i.portalColorValid=false;
    std::unordered_set<const Image*> kept;
    for(const Model*model:keep)if(model)for(const auto&material:model->materials){kept.insert(material.texture.get());kept.insert(material.lightmap.get());}
    std::erase_if(i.meshes,[&](auto&entry){return std::find(keep.begin(),keep.end(),entry.first)==keep.end();});
    i.overrides.clear();
    i.lightData.clear();
    i.effectBindings.clear();
    std::erase_if(i.textures,[&](auto&entry){return !kept.contains(entry.first);});
    std::erase_if(i.warps,[&](auto&entry){return !kept.contains(entry.first);});
    std::erase_if(i.turbulent,[&](const Image*image){return !kept.contains(image);});
    std::erase_if(i.skies,[&](auto&entry){return !kept.contains(entry.first);});
}
void D3D12Backend::purge_image(const Image*image){
    auto&i=*impl_;auto it=i.textures.find(image);
    if(it!=i.textures.end()){for(auto&texture:it->second)i.forget_effect_texture(texture.id);i.textures.erase(it);}
    auto warp=i.warps.find(image);if(warp!=i.warps.end()){i.forget_effect_texture(warp->second.texture);i.warps.erase(warp);}
    i.skies.erase(image);
    i.turbulent.erase(image);
}
void D3D12Backend::draw(const RenderFrame&f,WindowMetrics m){
    auto&i=*impl_;if(m.drawableWidth<=0||m.drawableHeight<=0)return;
    i.finalFrameValid=false;
    i.currentFrame=&f;
    struct PacketScope { Impl& owner; ~PacketScope(){owner.currentFrame=nullptr;} } packetScope{i};
    if(f.world&&!f.hardwareLighting)throw std::runtime_error("Legacy software world-light topology is not implemented by the native Direct3D 12 hardware path");
    if(UINT(m.drawableWidth)!=i.swapWidth||UINT(m.drawableHeight)!=i.swapHeight)i.resize_swapchain(UINT(m.drawableWidth),UINT(m.drawableHeight));
    i.begin_commands();
    i.admit_frame(f.tripleBufferRaw,f.bonusProfile);i.centroidLighting=centroid_lighting(f);
    i.configure_sampling(f.mipmapping,f.anisotropy);
    i.targets(m.drawableWidth,m.drawableHeight);
    if(f.door.world)i.prepare_door(f.door.world->mesh);
    const Camera*primary=nullptr;
    for(const auto&camera:f.cameras)if(!camera.foreground&&!camera.portalTarget&&camera.drawWorld){
        const Rect v=camera.viewport;if(v.width<=0||v.height<=0||(v.x<=0&&v.y<=0&&v.x+v.width>=m.width&&v.y+v.height>=m.height)){primary=&camera;break;}
    }
    i.shadowsReady=false;i.shadowLight=-1;
    if(i.shadowMapsOn){
        if(primary){Rect v=primary->viewport;if(v.width<=0||v.height<=0)v={0,0,float(m.width),float(m.height)};i.shadow_pass(f,*primary,float(std::lround(v.width*m.scaleX))/float(std::lround(v.height*m.scaleY)));}
        else{i.use(PScene);i.sv.shadowMaps=0;}
    }
    i.framebuffer(i.composite?i.hdrFbo:i.msaaFbo);
    auto seed=[&](Color c){return i.options.pbr?inverse_tone_map(c):c;};
    i.viewport(0,0,m.drawableWidth,m.drawableHeight);i.gl.scissorTest=false;
    i.gl.depthMask=true;i.gl.stencilWrite=255;i.gl.colorMask=true;
    bool nativeCameraPasses=std::any_of(f.cameras.begin(),f.cameras.end(),[](const Camera&camera){return camera.perDrawOpacity;});
    if(!nativeCameraPasses||!i.retainedColorValid){
        i.gl.clearColor=seed(f.clearColor);i.clear(true,true,true);i.retainedColorValid=true;
    }
    if(nativeCameraPasses&&f.clearScreen){
        const Camera*firstCamera=nullptr;
        for(const auto&camera:f.cameras)if(!camera.foreground&&!camera.portalTarget){firstCamera=&camera;break;}
        if(!firstCamera)for(const auto&camera:f.cameras)if(camera.foreground&&!camera.portalTarget){firstCamera=&camera;break;}
        Rect viewport=firstCamera?firstCamera->viewport:Rect{};
        bool fullScreen=firstCamera&&(viewport.width<=0||viewport.height<=0||(viewport.x<=0&&viewport.y<=0&&viewport.x+viewport.width>=m.width&&viewport.y+viewport.height>=m.height));
        if(!fullScreen){i.gl.clearColor=seed(f.screenColor);i.clear(true,false,false);}
    }
    // Engine packets retain native source-slot order first, then enhanced baked lights, within the 32-light upload budget.
    const auto uploadedLightCount=std::min<std::size_t>(f.lights.size(),32);
    const int selectedLight=material_selected_light(f);
    i.lightData.resize(uploadedLightCount*5);
    i.lightCones.resize(uploadedLightCount,{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::quiet_NaN()});
    for(std::size_t index=0;index<uploadedLightCount;++index){
        const auto&l=f.lights[index];
        auto cone=i.lightData[index*5+4];
        if(l.type==2&&(i.lightCones[index].x!=l.theta||i.lightCones[index].y!=l.phi)){
            i.lightCones[index]={l.theta,l.phi};cone[0]=std::cos(rad(l.theta)*.5f);cone[1]=std::cos(rad(l.phi)*.5f);
        }
        const std::array<std::array<float,4>,5>data{{
            {l.position.x,l.position.y,l.position.z,l.range},
            {l.color.r,l.color.g,l.color.b,l.baked?-l.intensity:l.intensity},
            {l.direction.x,l.direction.y,l.direction.z,float(l.type)},
            {l.attenuation.x,l.attenuation.y,l.attenuation.z,l.falloff},
            {cone[0],cone[1],l.synthetic?1.f:0.f,0}}};
        for(std::size_t word=0;word<data.size();++word)i.lightData[index*5+word]=data[word];
    }
    i.upload_lights();
    auto ui_state=[&]{
        i.gl.depthTest=false;i.gl.cull=false;i.gl.scissorTest=false;i.gl.blend=true;
        i.viewport(0,0,m.drawableWidth,m.drawableHeight);i.use(PUi);i.uiCb.viewport={float(m.width),float(m.height)};
        i.uiCb.textKey=0;i.uiCb.cursorKey=0;
    };
    auto draw_ui=[&](const UICommand&command){
        ui_state();
        if(command.clip.width>0&&command.clip.height>0){i.gl.scissorTest=true;i.scissor(int(std::lround(command.clip.x*m.scaleX)),int(std::lround((m.height-command.clip.y-command.clip.height)*m.scaleY)),int(std::lround(command.clip.width*m.scaleX)),int(std::lround(command.clip.height*m.scaleY)));}
        i.ui_command(command);
    };
    const auto&fx=i.options.features;
    auto draw_camera=[&](auto&self,const Camera&camera,bool targetOnly,const PortalSurface*targetPlane)->void{
        Rect v=camera.viewport;if(v.width<=0||v.height<=0)v={0,0,float(m.width),float(m.height)};
        int x=int(std::lround(v.x*m.scaleX)),y=int(std::lround((m.height-v.y-v.height)*m.scaleY)),w=int(std::lround(v.width*m.scaleX)),h=int(std::lround(v.height*m.scaleY));
        if(w<=0||h<=0||(!camera.perDrawOpacity&&camera.alpha<=0))return;
        const bool hdrView=i.composite&&!camera.foreground&&!targetOnly;const Fbo&destination=hdrView?i.hdrFbo:i.msaaFbo;
        auto seeded=[&](Color c){return hdrView&&i.options.pbr?inverse_tone_map(c):c;};
        const Camera*portalCamera=nullptr;std::optional<PortalSurface>surface;
        if(!targetOnly&&camera.portalId){
            for(const auto&candidate:f.cameras)if(candidate.sourceId==camera.portalId){portalCamera=&candidate;break;}
            if(!portalCamera)throw std::runtime_error("Attached portal view is absent from game frame");
            surface=Renderer::portal_surface(f,camera,m);
            if(surface){i.portal_target();self(self,*portalCamera,true,&*surface);i.framebuffer(destination);}
        }
        bool translucent=!targetOnly&&!camera.perDrawOpacity&&camera.alpha<1;
        if(targetOnly){
            i.portal_target();i.framebuffer(i.portalFbo);i.gl.scissorTest=false;i.gl.depthMask=true;i.gl.stencilWrite=255;i.gl.colorMask=true;
            if(!camera.perDrawOpacity||!i.portalColorValid){i.gl.clearColor=camera.environmentOverride?camera.background:f.clearColor;i.clear(true,true,true);i.portalColorValid=true;}
        }
        if(translucent){
            i.alpha_target();i.framebuffer(i.alphaFbo);i.gl.scissorTest=false;i.gl.depthMask=true;
            i.gl.clearColor=seeded(camera.foreground?Color{0,0,0,0}:(camera.environmentOverride?camera.background:f.clearColor));
            i.clear(true,true,true);
        }
        i.use(PScene);i.gl.depthTest=true;i.gl.blend=true;i.gl.depthMask=true;
        i.sv.pbr=i.options.pbr;i.sv.toneInShader=i.options.pbr&&!hdrView;
        i.portalPass=targetOnly;
        i.bind_sampler(0,Impl::SceneSampler);i.bind_sampler(1,Impl::LightmapSampler);
        i.gl.frontCCW=!((camera.horizontalFov<0)!=(camera.pixelAspect<0));
        i.bind_view(3,Kind::Buffer,nullptr,i.lightsSrv,allSubresources,bufferRead);i.sv.lightCount=int(i.lightData.size()/5);
        i.sv.portalEnabled=surface.has_value();
        if(surface){
            Rect portalViewport=portalCamera->viewport;if(portalViewport.width<=0||portalViewport.height<=0)portalViewport={0,0,float(m.width),float(m.height)};
            i.sv.portalProjection=camera_matrix(*portalCamera,portalViewport.width/portalViewport.height);
            i.sv.portalViewport={portalViewport.x/m.width,(m.height-portalViewport.y-portalViewport.height)/m.height,portalViewport.width/m.width,portalViewport.height/m.height};
            i.sv.portalPlane={surface->normal.x,surface->normal.y,surface->normal.z,-dot(surface->normal,surface->position)};
            i.bind(4,i.portalColor);i.bind_sampler(4,Impl::UiLinearSampler);
        }
        i.geometryClipPlane={};
        if(camera.portalClip){
            Vec3 position=targetPlane?targetPlane->position:camera.portalPosition,normal=targetPlane?targetPlane->normal:camera.portalNormal;
            if(dot(normal,normal)==0)throw std::runtime_error("Portal clip view has no real surface plane");
            normal=unit(normal);
            i.geometryClipPlane={normal.x,normal.y,normal.z,-dot(normal,position)};
        }
        i.sv.clipPlane=i.geometryClipPlane;
        if(targetOnly)i.bind(4,i.white);
        Color fog=camera.environmentOverride?camera.fogColor:f.fogColor;
        i.sv.sunDirection=f3(f.sunDirection);
        i.sv.fogColor=f3(fog);i.sv.fogRange={camera.environmentOverride?camera.fogStart:f.fogStart,camera.environmentOverride?camera.fogEnd:f.fogEnd};
        i.viewport(x,y,w,h);i.gl.scissorTest=true;i.scissor(x,y,w,h);
        if(camera.perDrawOpacity){
            i.gl.clearColor=seeded(camera.environmentOverride?camera.background:f.clearColor);i.gl.stencilWrite=255;
            if(camera.clearColor||camera.clearDepth||camera.clearStencil)i.clear(camera.clearColor,camera.clearDepth,camera.clearStencil);
        }else if(camera.clearDepth)i.clear(false,true,false);
        i.viewProjection=camera_matrix(camera,float(w)/h);i.sv.viewProjection=i.viewProjection;i.sv.cameraPosition=f3(camera.position);
        auto basis=transform({},camera.angles,{1,1,1});i.sv.infiniteViewer={basis[0],basis[1],basis[2]};i.sv.selectedLight=selectedLight;
        i.shadowOrder.clear();
        if(camera.drawWorld&&!camera.noShadow&&!camera.noEntities&&f.renderEntities&&(!f.bonusProfile||f.planarCapable)&&!f.shadows.empty()){
            if(f.bonusProfile?f.shadowStencilRaw>0:f.shadowStencilRaw!=0)throw std::runtime_error("Nondefault source stencil-volume shadows are not implemented; native Direct3D 12 supports the recovered planar route");
            for(const auto&d:f.shadows){
                if(!(d.layers&camera.layers)||(d.shadowViewId&&d.shadowViewId!=camera.sourceId)||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
                const auto cached=f.cachedEntityShadow?f.cachedEntityShadow(f.lightContext,d):d.nativeShadow;
                if(f.bonusProfile||cached.caster)i.shadowOrder.push_back(&d);
            }
            std::sort(i.shadowOrder.begin(),i.shadowOrder.end(),[&](const DrawInstance*a,const DrawInstance*b){
                Vec3 da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));if(!f.shadowSortVertical)da.z=db.z=0;return dot(da,da)>dot(db,db);
            });
        }
        i.sv.skyMask=0;const bool door=i.door_view(f,camera,!targetOnly);i.draw_sky(f,camera,w,h);
        if(camera.drawWorld&&f.world&&(camera.layers&1)){DrawInstance world;world.model=&f.world->mesh;i.draw_instance(world,f.skinBindings,&camera);}
        if(door&&camera.drawWorld&&(camera.layers&1)){
            const Vec2 range{camera.environmentOverride?camera.fogStart:f.fogStart,camera.environmentOverride?camera.fogEnd:f.fogEnd};
            i.draw_door(f,camera,seeded(range.y>range.x?fog:camera.environmentOverride?camera.background:f.clearColor),surface.has_value());
        }
        i.transparent.clear();
        for(const auto&d:f.instances){
            if(camera.noEntities||!f.renderEntities||!(d.layers&camera.layers)||d.viewSpace||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
            if(camera.perDrawOpacity&&d.model&&model_has_entity_skins(*d.model)){
                if(d.scale.x==0||d.scale.y==0||d.scale.z==0)continue;
                if(!d.visible){i.draw_instance(d,f.skinBindings,&camera);continue;}
                const unsigned queues=i.model_queue(d,f.skinBindings);
                if(queues&1)i.draw_instance(d,f.skinBindings,&camera,true,0);
                if(queues&2)i.transparent.push_back(&d);
                continue;
            }
            if(blended(d))i.transparent.push_back(&d);else i.draw_instance(d,f.skinBindings,&camera);
        }
        std::sort(i.transparent.begin(),i.transparent.end(),[&](const DrawInstance*a,const DrawInstance*b){Vec3 da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));return dot(da,da)>dot(db,db);});
        i.gl.depthMask=false;for(const auto*d:i.transparent){
            bool native=camera.perDrawOpacity&&d->model&&model_has_entity_skins(*d->model);
            i.draw_instance(*d,f.skinBindings,&camera,false,native?1:-1);
        }
        for(const auto*d:i.shadowOrder)i.draw_planar(*d,camera,w,h);
        i.gl.depthMask=false;
        if(camera.drawWorld&&!f.particles.empty()){
            i.material_uniforms(nullptr);
            i.gl.cull=false;i.sd.particleInstanced=1;i.sd.spriteFrames=1;i.sd.unlit=1;i.sd.hasLightmap=0;
            i.sd.uvScale={1,1};i.sd.uvOffset={0,0};i.sd.alpha=1;i.sd.reflectionWeight=0;i.sd.noFog=0;i.sd.tint={1,1,1,1};
            Matrix basis=transform({},camera.angles,{1,1,1});Vec3 right{-basis[4],-basis[5],-basis[6]},up{basis[8],basis[9],basis[10]},forward{basis[0],basis[1],basis[2]};
            static const Model quad=[] {Model q;q.vertices={{{0,-.5f,-.5f},{1,0,0},{0,1},{}},{{0,.5f,-.5f},{1,0,0},{1,1},{}},{{0,.5f,.5f},{1,0,0},{1,0},{}},{{0,-.5f,.5f},{1,0,0},{0,0},{}}};q.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};return q;}();
            auto&gpu=i.mesh(quad);i.default_attributes(gpu);
            i.particleOrder.clear();i.particleData.clear();
            for(const auto&p:f.particles)if(p.size>0&&(!p.translucent||p.alphaRaw>0)&&p.scaleZ!=0)i.particleOrder.push_back(&p);
            std::sort(i.particleOrder.begin(),i.particleOrder.end(),[&](const Particle*a,const Particle*b){
                if(a->additive!=b->additive)return !a->additive;
                if(a->additive&&a->image!=b->image)return std::less<const Image*>{}(a->image,b->image);
                Vec3 da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));return dot(da,da)>dot(db,db);
            });
            for(const auto*particle:i.particleOrder){
                const auto&p=*particle;float c=std::cos(rad(p.angle)),s=std::sin(rad(p.angle));
                Vec3 r=add(mul(right,c),mul(up,s)),u=add(mul(up,c),mul(right,-s)),position=p.position;float length=p.size*std::abs(p.scaleZ);
                if((p.beam||p.streak)&&dot(p.velocity,p.velocity)>0){
                    Vec3 projected=add(p.velocity,mul(forward,-dot(p.velocity,forward)));
                    if(dot(projected,projected)>0.000001f){u=unit(projected);r=unit(cross(u,forward));}
                    float speed=std::sqrt(dot(p.velocity,p.velocity));if(p.streak)length+=speed;
                    if(p.beam){u=unit(p.velocity);Vec3 side=cross(u,add(camera.position,mul(p.position,-1)));r=dot(side,side)>0.000001f?unit(side):right;length+=speed;position=add(position,mul(p.velocity,-.5f));}
                }
                Vec3 n=unit(cross(r,u));Matrix model{n.x,n.y,n.z,0,r.x*p.size,r.y*p.size,r.z*p.size,0,u.x*length,u.y*length,u.z*length,0,position.x,position.y,position.z,1};
                Color color=p.color;color.a=particle_alpha(p.alphaRaw,p.translucent,p.additive,p.image!=nullptr);
                i.particleData.push_back({model,color,{p.flare?1.0f:0.0f,p.overlay?1.0f:0.0f,p.invertFlare?1.0f:0.0f}});
            }
            Upload instances{};if(!i.particleData.empty()){instances=i.dev.upload(i.particleData.size()*sizeof(ParticleGPU),16);std::memcpy(instances.cpu,i.particleData.data(),i.particleData.size()*sizeof(ParticleGPU));}
            for(std::size_t first=0;first<i.particleOrder.size();){
                const auto&particle=*i.particleOrder[first];std::size_t end=first+1;
                while(end<i.particleOrder.size()&&i.particleOrder[end]->image==particle.image&&i.particleOrder[end]->additive==particle.additive)++end;
                auto&slot=i.vb[slotParticle];slot.buffer.reset();slot.upload=true;slot.view={instances.gpu+first*sizeof(ParticleGPU),UINT((end-first)*sizeof(ParticleGPU)),sizeof(ParticleGPU)};
                i.bind(0,i.texture(particle.image));i.bind_sampler(0,particle.image&&i.textures.at(particle.image)[0].dynamic?Impl::DynamicSceneSampler:Impl::SceneSampler);i.sd.textureAlpha=particle.image&&i.textures.at(particle.image)[0].alpha;
                i.blend_func_separate(D3D12_BLEND_SRC_ALPHA,particle.additive?D3D12_BLEND_ONE:D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_ONE,D3D12_BLEND_INV_SRC_ALPHA);i.draw_elements(6,0,UINT(end-first));first=end;
            }
            i.vertex_buffer(slotParticle,i.dummyInstance,0,sizeof(ParticleGPU));
        }
        if(!targetOnly&&&camera==primary&&fx.ssao)i.screen_effects(camera,x,y,w,h);
        bool hasViewEntities=false;
        if(f.renderEntities&&!camera.noEntities)for(const auto&d:f.instances)if(!targetOnly&&d.viewSpace&&(d.layers&camera.layers)&&d.visible&&(!camera.excludeEntityId||d.objectId!=camera.excludeEntityId)){hasViewEntities=true;break;}
        if(hasViewEntities){
            i.gl.depthMask=true;i.clear(false,true,false);i.sv.fogRange={0,0};i.sv.doorMode=0;
            for(const auto&d:f.instances)if(d.viewSpace&&(d.layers&camera.layers)&&(!camera.excludeEntityId||d.objectId!=camera.excludeEntityId))i.draw_instance(d,f.skinBindings,&camera);
        }
        i.gl.depthMask=true;
        if(translucent){
            i.framebuffer(destination);ui_state();i.uiCb.overlay=0;i.bind_sampler(0,Impl::UiLinearSampler);i.blend_func(D3D12_BLEND_ONE,D3D12_BLEND_INV_SRC_ALPHA);
            i.uiVertices.clear();i.quad(v,{v.x/m.width,1-v.y/m.height,v.width/m.width,-v.height/m.height},{camera.alpha,camera.alpha,camera.alpha,camera.alpha});
            i.flush_ui(i.alphaColor);
        }
    };
    for(const auto&camera:f.cameras)if(!camera.foreground&&!camera.portalTarget)draw_camera(draw_camera,camera,false,nullptr);
    if(i.composite)i.composite_pass(m.drawableWidth,m.drawableHeight);
    std::size_t cameraIndex=0,uiIndex=0;
    std::size_t cursorIndex=f.ui.size();
    while(cameraIndex<f.cameras.size()||uiIndex<f.ui.size()){
        while(cameraIndex<f.cameras.size()&&(!f.cameras[cameraIndex].foreground||f.cameras[cameraIndex].portalTarget))++cameraIndex;
        if(cameraIndex<f.cameras.size()&&(uiIndex==f.ui.size()||f.cameras[cameraIndex].layer<=f.ui[uiIndex].layer))draw_camera(draw_camera,f.cameras[cameraIndex++],false,nullptr);
        else if(uiIndex<f.ui.size()){const auto&command=f.ui[uiIndex++];if(command.cursorOverlay){if(cursorIndex==f.ui.size())cursorIndex=uiIndex-1;}else draw_ui(command);}
    }
    if(f.video){
        ui_state();i.uiCb.overlay=0;i.bind_sampler(0,Impl::UiLinearSampler);
        check_image(*f.video);
        if(i.videoWidth!=int(f.video->width)||i.videoHeight!=int(f.video->height)){
            i.videoTexture=i.create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,f.video->width,f.video->height);i.upload_subresource(*i.videoTexture,0,0,f.video->rgba.data(),f.video->width*4,f.video->width*4);i.videoWidth=int(f.video->width);i.videoHeight=int(f.video->height);i.videoSerial=f.videoSerial;
        }else if(i.videoSerial!=f.videoSerial){i.upload_subresource(*i.videoTexture,0,0,f.video->rgba.data(),f.video->width*4,f.video->width*4);i.videoSerial=f.videoSerial;}
        Rect v=f.videoRect;
        if(v.width<=0||v.height<=0){float scale=std::min(float(m.width)/f.video->width,float(m.height)/f.video->height);v={(m.width-f.video->width*scale)/2,(m.height-f.video->height*scale)/2,f.video->width*scale,f.video->height*scale};}
        i.uiVertices.clear();i.quad(v,{0,0,1,1},{1,1,1,1});i.blend_func(D3D12_BLEND_SRC_ALPHA,D3D12_BLEND_INV_SRC_ALPHA);i.flush_ui(i.videoTexture);
    }
    for(;cursorIndex<f.ui.size();++cursorIndex)if(f.ui[cursorIndex].cursorOverlay)draw_ui(f.ui[cursorIndex]);
    i.post_state();
    i.resolve(*i.msaaColor,*i.resolveColor);
    if(f.adjustment.enabled&&(!i.gammaUploaded||i.gamma.gammaRamp!=f.adjustment.gammaRamp)){
        i.upload_subresource(*i.gammaTexture,0,0,f.adjustment.gammaRamp.data(),512,512);i.gamma=f.adjustment;i.gammaUploaded=true;
    }
    i.gamma.enabled=f.adjustment.enabled;
    auto&back=i.backBuffers[i.swapchain->GetCurrentBackBufferIndex()];i.backFbo={back};
    i.post_draw(i.backFbo);i.dev.transition(*back,D3D12_RESOURCE_STATE_PRESENT);i.backFbo={}; // Only backBuffers may hold swap-chain buffers between frames (ResizeBuffers)
    i.await_submission();i.execute();
    const UINT interval=UINT(i.requestedSwapInterval);
    const HRESULT presented=i.swapchain->Present(interval,interval==0&&i.tearing?DXGI_PRESENT_ALLOW_TEARING:0);
    if(presented==DXGI_ERROR_DEVICE_REMOVED||presented==DXGI_ERROR_DEVICE_RESET)check(i.dev.device->GetDeviceRemovedReason(),"Present (device removed)");
    check(presented,"Present");
    i.completion.push_back(i.signal());++i.completionSubmissions;i.finalFrameValid=true;
}
void D3D12Backend::capture(std::string_view path,WindowMetrics m)const{
    auto&i=*impl_;
    if(m.drawableWidth<=0||m.drawableHeight<=0)throw std::runtime_error("Cannot capture minimized game window");
    if(!i.finalFrameValid||m.drawableWidth!=i.targetWidth||m.drawableHeight!=i.targetHeight)
        throw std::runtime_error("Cannot capture game window before a completed frame at its current drawable size");
    // Re-run the final pass into an offscreen target (post-present swap-chain contents are discarded) and read it back.
    i.drain_completion();i.dev.wait_idle();i.begin_commands();
    if(!i.captureColor||int(i.captureColor->width)!=m.drawableWidth||int(i.captureColor->height)!=m.drawableHeight){i.captureColor=i.create_texture(DXGI_FORMAT_R8G8B8A8_UNORM,UINT(m.drawableWidth),UINT(m.drawableHeight),1,1,1,true);i.captureFbo={i.captureColor};}
    i.post_state();i.post_draw(i.captureFbo);
    const auto desc=i.captureColor->resource->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows{};UINT64 rowSize{},total{};i.dev.device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&rowSize,&total);
    ComPtr<ID3D12Resource> readback;D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK};D3D12_RESOURCE_DESC buffer{D3D12_RESOURCE_DIMENSION_BUFFER,0,total,1,1,1,DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,D3D12_RESOURCE_FLAG_NONE};
    check(i.dev.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"capture readback buffer");
    i.dev.transition(*i.captureColor,D3D12_RESOURCE_STATE_COPY_SOURCE);i.dev.flush_barriers();
    D3D12_TEXTURE_COPY_LOCATION dst{readback.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{}},src{i.captureColor->resource.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};dst.PlacedFootprint=fp;src.SubresourceIndex=0;
    i.dev.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    i.execute();i.dev.wait(i.signal());
    void*mapped{};D3D12_RANGE range{0,SIZE_T(total)};check(readback->Map(0,&range,&mapped),"capture map");
    std::vector<std::uint8_t> row(std::size_t(m.drawableWidth)*3);
    std::ofstream file(std::string(path),std::ios::binary);
    if(!file){D3D12_RANGE none{0,0};readback->Unmap(0,&none);throw std::runtime_error("Cannot create game framebuffer capture");}
    file<<"P6\n"<<m.drawableWidth<<' '<<m.drawableHeight<<"\n255\n";
    for(int y=0;y<m.drawableHeight;++y){
        const auto*p=static_cast<const std::uint8_t*>(mapped)+fp.Offset+std::size_t(y)*fp.Footprint.RowPitch;
        for(int x=0;x<m.drawableWidth;++x){row[std::size_t(x)*3]=p[x*4];row[std::size_t(x)*3+1]=p[x*4+1];row[std::size_t(x)*3+2]=p[x*4+2];}
        file.write(reinterpret_cast<const char*>(row.data()),std::streamsize(row.size()));
    }
    D3D12_RANGE none{0,0};readback->Unmap(0,&none);
    if(!file)throw std::runtime_error("Cannot write game framebuffer capture");
}
}
