#include "render_vulkan.hpp"
#include "render_policy.hpp"
#include "render_vulkan_spirv.hpp"
#include "texture_pack.hpp"
#include <cstdio>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace k22::vk22 {
using namespace render_policy;
namespace {
using F4=std::array<float,4>;
struct alignas(16) Uniform {
    Matrix model{},viewProjection{},normalMatrix{},portalProjection{},projection{};
    F4 clipPlane{},portalViewport{},portalPlane{},tint{1,1,1,1},sunDirection{},cameraPosition{},fogColor{},uvTransform{1,1,0,0},fogParameters{},normalUv{1,1,0,1},skillMain{},skillExtra{},preparedAmbient{},preparedDiffuse{},preparedSpecular{},preparedEmissive{},ambientLight{},viewport{},skyConeScroll{},skyParameters{};
    std::array<std::array<std::int32_t,4>,4> flags{};
    std::array<F4,3> parameters{};
    F4 pbr{}; // raster_uniform.glsl dataPbr.
    Matrix sunShadow{},inverseViewProjection{},previousViewProjection{};
    F4 shadowLight{},effects{},pass{},camera{},range{}; // raster_uniform.glsl wave-2 block; zero keeps the original path.
    Matrix previousModel{}; // Motion-vector pass (3) only.
    std::array<F4,4> door{}; // raster_uniform.glsl dataDoor (seamless door clip); zero keeps the original path.
};
// Compute passes of the ray-traced screen-space signals (rt_signals.comp / rt_filter.comp).
struct alignas(16) ComputeUniform { Matrix inverseViewProjection{},previousViewProjection{}; F4 eye{},sun{},params{},camera{}; std::array<std::int32_t,4> config{}; F4 actor{},planar{}; };
F4 vec(Vec3 v,float w=0){return {v.x,v.y,v.z,w};}
F4 vec(Color c){return {c.r,c.g,c.b,c.a};}
struct UIVertex{float x,y,u,v,r,g,b,a,u0,v0,u1,v1;}; // u0..v1: the sampled sub-rect (min/max texture coordinates), ui.frag clamps to it.
struct Batch{std::uint32_t first{},count{},material{},centroidFirst{},centroidCount{};};
struct Mesh { Buffer indices;std::vector<Batch>batches;std::vector<Vertex>expanded;std::vector<Vec3>tangents;Bounds bounds;std::vector<Bounds>frames;int primaryMax{},skinLayers{}; };
// width/height are the original image's (UV policy); image may hold a larger texture-pack replacement.
struct Texture { Image image;std::uint64_t serial{},submission{~0ull};bool dynamic{},alpha{},mipsValid{};std::uint32_t width{},height{};Image normal,orm;bool roughness{}; };
struct Warp { Image image;std::uint64_t serial{~0ull};int phase{-1};float range{};bool mipsValid{}; };
struct Sky { Image image;std::uint32_t frames{}; };
struct EffectBank { const k22::Image* identity{};std::uint32_t width{},height{};bool dynamic{},alpha{}; };
struct Effect { std::uint64_t generation{~0ull};EffectKind kind{EffectKind::None};std::array<EffectBank,2>banks{}; };
struct OverrideBounds{const Model*model{};std::uint64_t revision{~0ull};std::size_t count{};Bounds bounds;};
struct Allocation {Buffer*buffer{};VkDeviceSize offset{},size{};};
enum Program {Scene,Shadow,UI,Post,SkyProgram,WarpProgram,CloudProgram,TonemapProgram,EffectProgram};
// premultiplied: 1 premultiplied, 2 native model alpha, 3 multiply (AO), 4 additive keeping destination alpha (fog, bloom).
// tess: PN-triangle tessellation stages (tessellation=1, MDL draws; scene.tesc/scene.tese).
struct PipelineKey {int program{},samples{},depth{},write{},blend{},additive{},premultiplied{},cull{},clockwise{},stencil{},colorFormat{},depthFormat{},tess{};auto operator<=>(const PipelineKey&)const=default;};
}
struct Raster {
    VulkanBackend::State&s;
    VkDescriptorSetLayout layout{};VkPipelineLayout pipelineLayout{};
    std::array<VkShaderModule,16>shaders{};std::map<PipelineKey,VkPipeline>pipelines;
    struct Arena {std::deque<Buffer>buffers;std::vector<VkDeviceSize>used;std::vector<VkDescriptorPool>pools;std::vector<VkImageView>views;std::size_t pool{};};
    std::array<Arena,2>arenas;
    std::unordered_map<const Model*,Mesh>meshes;
    std::unordered_map<const k22::Image*,std::array<Texture,3>>textures;
    std::unordered_map<const k22::Image*,Warp>warps;
    std::unordered_set<const k22::Image*>turbulent; // Never pack-replaced: the native warp table addresses original texels.
    std::unordered_map<const k22::Image*,Sky>skies;
    std::unordered_map<std::uint32_t,Effect>effects;
    std::unordered_map<std::uint32_t,OverrideBounds>overrideBounds;
    std::map<std::tuple<bool,bool,double,float>,VkSampler>samplers;
    Image white,black,gamma,video,flatNormal,defaultOrm;std::uint64_t videoSerial{~0ull};ColorAdjustment adjustment{};bool gammaUploaded{};
    const RenderFrame*frame{};WindowMetrics metrics{};bool centroid{},portalPass{},maskPass{},active{};
    int selectedLight{-1};
    Target*target{};VkRect2D region{};Uniform view{};Allocation lightAllocation,zeroAllocation;
    VkExtent2D renderExtent{};
    Target*opaqueTarget{};VkExtent2D viewExtent{}; // Destination size of the current view: s.render for FSR background views, else s.extent.
    std::vector<UIVertex>uiVertices;std::vector<const DrawInstance*>transparent,shadowOrder;
    struct CasterDraw{VkBuffer vertices,indices;VkDescriptorSet set;bool cull,clockwise,tess;std::uint32_t first,count;};std::vector<CasterDraw>casterDraws;
    enum class Encounter {Live,Prepare,Replay};
    struct Prepared {Pose*pose{};std::array<NativeEntityLightState,3>lights{};std::array<int,2>queues{-1,-1};unsigned admitted{};unsigned latest{3};};
    std::unordered_map<const DrawInstance*,Prepared>prepared;
    std::unordered_map<std::uint32_t,NativeEntityLightState>actorLights;
    DrawInstance worldDraw{},doorDraw{}; // doorDraw: RenderFrame::door's world mesh (render.hpp DoorPortal).
    // Wave-2 effects (docs/GRAPHICS_ROADMAP.md, docs/RAYTRACING.md); nothing below is allocated unless enabled.
    VkFormat renderColor{VK_FORMAT_UNDEFINED},renderDepth{VK_FORMAT_UNDEFINED};VkSampleCountFlagBits renderSamples{VK_SAMPLE_COUNT_1_BIT};
    std::array<std::pair<Image*,VkSampler>,6>globals{}; // Bindings 9-14: sun/cube shadow maps, RT mask/GI/reflection, G-buffer depth.
    Image dummyDepth,dummyCube;VkSampler compareSampler{};bool dummiesReady{};
    struct Fx {
        VkExtent2D extent{};
        Image depth,depthSampled;VkImageView depthView{}; // Single-sample primary depth before view-space entities; depthSampled aliases a depth-only view.
        Image linear,ao,ldr,fog;std::array<Image,2>taa;std::array<Image,5>bloom;
        Image sharp; // sharpen with FXAA: FXAA output, read by the final CAS pass.
        Image fsrDepth,fsrMotion,upscaled; // FSR inputs (render size: R32F device depth, RG16F full motion) and its display-size output.
        Image opaque,reactive; // FSR: primary view colour before translucent/particle/view-space draws, and the derived reactive mask.
        // Frame generation: upscaled interpolated halfway from the previous frame's (display size, upscaled's format).
        // upscaledNow: FSR ran this frame; fsrReset/fsrMs: its history reset and frame time.
        Image interpolated;bool upscaledNow{},fsrReset{};float fsrMs{16.7f};std::uint64_t generationFrame{};
        Image sun,cube;std::array<VkImageView,6>faces{};
        std::array<Image,2>gdata,gdepth,length;std::array<Image,3>raw,tmp;std::array<std::array<Image,3>,2>history;
        Image motion,motionDepth; // Motion vectors of moving/animated entities (scene pass 3) for TAA and the denoiser.
        std::unordered_map<std::uint32_t,Matrix>previousModels,currentModels;std::uint64_t motionFrame{};
        Matrix sunMatrix{},previousVP{},unjittered{},inverseVP{};float sunTexel{},shadowRange{},nearPlane{1},farPlane{1};Vec3 shadowLight{},eye{},forward{};int shadowIndex{-1};
        unsigned taaIndex{},historyIndex{};bool taaValid{},historyValid{},linearValid{},shadowsValid{},motionValid{},fsrValid{},opaqueValid{};bool captureOpaque{};
        std::array<float,2> fsrJitter{};float fovY{1};std::uint64_t fsrFrame{};std::chrono::steady_clock::time_point fsrTime{};
    } fx;
    VkDescriptorSetLayout computeSetLayout{};VkPipelineLayout computeLayout{};VkPipeline signalsPipeline{},filterPipeline{};
    std::uint64_t frameIndex{};const Camera*primary{};
    // shadows=rt actor shadows: light records appended after the shading lights (rt_signals.comp actorShadow). They replace
    // the planar shadow, which is then never drawn.
    std::size_t actorShadowFirst{},actorShadowCount{};bool actorShadowCasters{};
    bool actorShadows()const{return s.rayEnabled&&s.options.features.shadows==ShadowMode::RayTraced;}
    bool rtSignals()const{const auto&f=s.options.features;return s.rayEnabled&&(f.shadows==ShadowMode::RayTraced||f.gi||f.roughReflections);}
    bool motionVectors()const{return s.options.features.taa||s.options.features.fsr||(rtSignals()&&s.options.features.denoise);}
    // Caster push constant stages: the tessellation evaluation stage projects tessellated casters too.
    VkShaderStageFlags casterStages{VK_SHADER_STAGE_VERTEX_BIT};
    bool smoothing()const{return s.options.features.smoothNormals||s.options.features.tessellation;}
    PipelineKey tessellated(const DrawInstance&d,PipelineKey key)const{key.tess=s.options.features.tessellation&&!d.billboard&&pn_model(*d.model);return key;}
    explicit Raster(VulkanBackend::State&state):s(state){
        try{
        // Bindings: 0 uniform, 1-4 skin/lightmap/portal/effect normal, 5-6 storage, 7-8 PBR normal/ORM, 9-14 wave-2 globals.
        std::array<VkDescriptorSetLayoutBinding,15>bindings{};
        for(unsigned i=0;i<bindings.size();++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==5||i==6?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;bindings[i].stageFlags=VK_SHADER_STAGE_ALL_GRAPHICS;}
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=bindings.size();li.pBindings=bindings.data();require(vkCreateDescriptorSetLayout(s.device,&li,nullptr,&layout),"raster descriptor layout");
        std::array<VkDescriptorSetLayout,2>layouts{layout,s.rays?rt_layout(*s.rays):VK_NULL_HANDLE};
        if(s.options.features.tessellation)casterStages|=VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pi.setLayoutCount=s.rayEnabled?2:1;pi.pSetLayouts=layouts.data();VkPushConstantRange casterPush{casterStages,0,sizeof(Matrix)};pi.pushConstantRangeCount=1;pi.pPushConstantRanges=&casterPush;require(vkCreatePipelineLayout(s.device,&pi,nullptr,&pipelineLayout),"raster pipeline layout");
        const std::array<std::span<const std::uint32_t>,14>code{spirv::scene_vert,s.rayEnabled?std::span<const std::uint32_t>(spirv::scene_rt_frag):std::span<const std::uint32_t>(spirv::scene_frag),spirv::shadow_vert,spirv::shadow_geom,spirv::shadow_frag,spirv::ui_vert,spirv::ui_frag,spirv::post_vert,spirv::post_frag,spirv::sky_frag,spirv::warp_frag,spirv::cloud_frag,spirv::tonemap_frag,spirv::effects_frag};
        for(std::size_t i=0;i<code.size();++i)shaders[i]=s.shader(code[i]);
        if(s.options.features.tessellation){shaders[14]=s.shader(spirv::scene_tesc);shaders[15]=s.shader(spirv::scene_tese);}
        if(rtSignals()){
            // Compute set: 0 uniform, 1 lights, 2-9 sampled images, 10-20 storage images; set 1 is the ray-query scene.
            std::array<VkDescriptorSetLayoutBinding,21>cb{};for(unsigned i=0;i<cb.size();++i){cb[i].binding=i;cb[i].descriptorCount=1;cb[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==1?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i<10?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;cb[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
            VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.bindingCount=cb.size();ci.pBindings=cb.data();require(vkCreateDescriptorSetLayout(s.device,&ci,nullptr,&computeSetLayout),"RT signal descriptor layout");
            std::array<VkDescriptorSetLayout,2>sets{computeSetLayout,rt_layout(*s.rays)};VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,8};
            VkPipelineLayoutCreateInfo cp{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};cp.setLayoutCount=2;cp.pSetLayouts=sets.data();cp.pushConstantRangeCount=1;cp.pPushConstantRanges=&push;require(vkCreatePipelineLayout(s.device,&cp,nullptr,&computeLayout),"RT signal pipeline layout");
            for(auto [pipeline,words]:{std::pair{&signalsPipeline,std::span<const std::uint32_t>(spirv::rt_signals_comp)},{&filterPipeline,std::span<const std::uint32_t>(spirv::rt_filter_comp)}}){
                auto module=s.shader(words);VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pc.layout=computeLayout;pc.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pc.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pc.stage.module=module;pc.stage.pName="main";
                auto status=vkCreateComputePipelines(s.device,VK_NULL_HANDLE,1,&pc,nullptr,pipeline);vkDestroyShaderModule(s.device,module,nullptr);require(status,"RT signal compute pipeline");
            }
        }
        const std::array<std::uint8_t,4>w{255,255,255,255},b{0,0,0,255};
        white=s.image(1,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);black=s.image(1,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};s.upload(white,std::as_bytes(std::span(w)),std::span(&copy,1));s.upload(black,std::as_bytes(std::span(b)),std::span(&copy,1));
        const std::array<std::uint8_t,4>n{128,128,255,255},o{255,255,0,255};
        flatNormal=s.image(1,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);defaultOrm=s.image(1,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        s.upload(flatNormal,std::as_bytes(std::span(n)),std::span(&copy,1));s.upload(defaultOrm,std::as_bytes(std::span(o)),std::span(&copy,1));
        gamma=s.image(256,3,VK_FORMAT_R16_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);copy.imageExtent={256,3,1};s.upload(gamma,std::as_bytes(std::span(adjustment.gammaRamp)),std::span(&copy,1));
        }catch(...){destroy_owned();throw;}
    }
    ~Raster(){destroy_owned();}
    void destroy_owned(){
        for(auto&[k,p]:pipelines)vkDestroyPipeline(s.device,p,nullptr);for(auto module:shaders)if(module)vkDestroyShaderModule(s.device,module,nullptr);vkDestroyPipelineLayout(s.device,pipelineLayout,nullptr);vkDestroyDescriptorSetLayout(s.device,layout,nullptr);
        vkDestroyPipeline(s.device,signalsPipeline,nullptr);vkDestroyPipeline(s.device,filterPipeline,nullptr);vkDestroyPipelineLayout(s.device,computeLayout,nullptr);vkDestroyDescriptorSetLayout(s.device,computeSetLayout,nullptr);
        for(auto&a:arenas){for(auto view:a.views)vkDestroyImageView(s.device,view,nullptr);for(auto&b:a.buffers)s.destroy(b);for(auto p:a.pools)vkDestroyDescriptorPool(s.device,p,nullptr);}for(auto&[k,p]:samplers)vkDestroySampler(s.device,p,nullptr);
        release();s.destroy(white);s.destroy(black);s.destroy(gamma);s.destroy(video);s.destroy(flatNormal);s.destroy(defaultOrm);
        free_effects(true);s.destroy(dummyDepth);s.destroy(dummyCube);if(compareSampler)vkDestroySampler(s.device,compareSampler,nullptr);
    }
    // Extent-dependent effect images are retired (fence-safe) on resize; shadow maps only on teardown.
    void free_effects(bool all){
        auto drop=[&](Image&i){if(all)s.destroy(i);else s.retire(i);};auto dropView=[&](VkImageView&v){if(!v)return;if(all)vkDestroyImageView(s.device,v,nullptr);else arenas[s.slotIndex].views.push_back(v);v=VK_NULL_HANDLE;};
        dropView(fx.depthView);fx.depthSampled={};drop(fx.depth);drop(fx.linear);drop(fx.ao);drop(fx.fog);drop(fx.ldr);drop(fx.motion);drop(fx.motionDepth);for(auto*set:{&fx.taa,&fx.gdata,&fx.gdepth,&fx.length})for(auto&i:*set)drop(i);for(auto&i:fx.bloom)drop(i);for(auto&i:fx.raw)drop(i);for(auto&i:fx.tmp)drop(i);for(auto&h:fx.history)for(auto&i:h)drop(i);
        drop(fx.sharp);drop(fx.fsrDepth);drop(fx.fsrMotion);drop(fx.upscaled);drop(fx.opaque);drop(fx.reactive);drop(fx.interpolated);
        fx.taaValid=fx.historyValid=fx.linearValid=fx.fsrValid=false;fx.extent={};
        if(all){for(auto&v:fx.faces)dropView(v);drop(fx.sun);drop(fx.cube);}
    }
    // keep (seamless door world, render.hpp DoorPortal): its mesh and the textures/lightmaps its materials name survive.
    void release(const Model*keep=nullptr){
        std::unordered_set<const k22::Image*>kept;if(keep)for(auto&mat:keep->materials)for(auto*image:{mat.texture.get(),mat.lightmap.get()})if(image)kept.insert(image);
        fx.fsrValid=false;std::erase_if(meshes,[&](auto&e){if(e.first==keep)return false;s.destroy(e.second.indices);return true;});
        std::erase_if(textures,[&](auto&e){if(kept.contains(e.first))return false;for(auto&t:e.second){s.destroy(t.image);s.destroy(t.normal);s.destroy(t.orm);}return true;});
        std::erase_if(warps,[&](auto&e){if(kept.contains(e.first))return false;s.destroy(e.second.image);return true;});std::erase_if(turbulent,[&](auto*image){return !kept.contains(image);});
        std::erase_if(skies,[&](auto&e){if(kept.contains(e.first))return false;s.destroy(e.second.image);return true;});
        effects.clear();overrideBounds.clear();prepared.clear();actorLights.clear();s.retainedColorValid=s.portalColorValid=false;
    }
    VkSampler sampler(bool linear,bool clamp,double mip=0,bool anisotropic=false){mip=mip<1?0:mip<=2?1:3;float a=anisotropic?effective_anisotropy(s.options.forceAnisotropy?1:frame?frame->anisotropy:0,std::clamp(s.options.anisotropy,1.f,s.properties.limits.maxSamplerAnisotropy)):1;auto key=std::tuple(linear,clamp,mip,a);auto[it,inserted]=samplers.try_emplace(key);if(inserted)try{it->second=s.sampler(linear,clamp,mip,a);}catch(...){samplers.erase(it);throw;}return it->second;}
    Allocation allocate(VkDeviceSize size,VkDeviceSize alignment=16,const void*data=nullptr){
        auto&a=arenas[s.slotIndex];for(std::size_t i=0;;++i){if(i==a.buffers.size()){a.used.push_back(0);try{a.buffers.emplace_back();}catch(...){a.used.pop_back();throw;}try{a.buffers.back()=s.buffer(std::max<VkDeviceSize>(1024*1024,size+alignment),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,true);}catch(...){a.buffers.pop_back();a.used.pop_back();throw;}}auto offset=(a.used[i]+alignment-1)/alignment*alignment;if(offset+size>a.buffers[i].size)continue;a.used[i]=offset+size;if(data)std::memcpy(static_cast<std::byte*>(a.buffers[i].mapped)+offset,data,size);return {&a.buffers[i],offset,size};}
    }
    void reset_arena(){auto&a=arenas[s.slotIndex];for(auto view:a.views)vkDestroyImageView(s.device,view,nullptr);a.views.clear();std::fill(a.used.begin(),a.used.end(),0);a.pool=0;for(auto p:a.pools)require(vkResetDescriptorPool(s.device,p,0),"raster descriptor pool reset");}
    VkDescriptorSet descriptor(const Uniform&u,std::array<Image*,6>images,std::array<VkSampler,6>sampling,Allocation offsets={}){
        auto&a=arenas[s.slotIndex];VkDescriptorSet set{};
        for(;;){if(a.pool==a.pools.size()){std::array<VkDescriptorPoolSize,4>sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2048},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4096},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,24576},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1024}}};VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};ci.maxSets=2048;ci.poolSizeCount=sizes.size();ci.pPoolSizes=sizes.data();a.pools.emplace_back();try{require(vkCreateDescriptorPool(s.device,&ci,nullptr,&a.pools.back()),"raster descriptor pool");}catch(...){a.pools.pop_back();throw;}}VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=a.pools[a.pool];ai.descriptorSetCount=1;ai.pSetLayouts=&layout;auto result=vkAllocateDescriptorSets(s.device,&ai,&set);if(result==VK_ERROR_OUT_OF_POOL_MEMORY||result==VK_ERROR_FRAGMENTED_POOL){++a.pool;continue;}require(result,"raster descriptor allocation");break;}
        auto uniform=allocate(sizeof(u),std::max<VkDeviceSize>(16,s.properties.limits.minUniformBufferOffsetAlignment),&u);auto lights=lightAllocation.buffer?lightAllocation:zeroAllocation;auto displacement=offsets.buffer?offsets:zeroAllocation;
        std::array<VkDescriptorBufferInfo,3>bi{{{uniform.buffer->handle,uniform.offset,uniform.size},{lights.buffer->handle,lights.offset,lights.size},{displacement.buffer->handle,displacement.offset,displacement.size}}};std::array<VkDescriptorImageInfo,12>ii{};std::array<VkWriteDescriptorSet,15>w{};
        for(unsigned i=0;i<w.size();++i){w[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w[i].dstSet=set;w[i].dstBinding=i;w[i].descriptorCount=1;const bool storage=i==5||i==6;w[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:storage?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;if(i==0)w[i].pBufferInfo=&bi[0];else if(storage)w[i].pBufferInfo=&bi[i-4];else if(i>=9){ii[i-3]={globals[i-9].second,globals[i-9].first->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};w[i].pImageInfo=&ii[i-3];}else{const unsigned slot=i<5?i-1:i-3;auto*image=images[slot]?images[slot]:&white;ii[slot]={sampling[slot]?sampling[slot]:sampler(true,true),image->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};w[i].pImageInfo=&ii[slot];}}
        vkUpdateDescriptorSets(s.device,w.size(),w.data(),0,nullptr);return set;
    }
    VkPipeline pipeline(PipelineKey key){
        auto found=pipelines.find(key);if(found!=pipelines.end())return found->second;
        std::array<VkPipelineShaderStageCreateInfo,4>stages{};unsigned count=2;unsigned vs=key.program==Scene?0:key.program==Shadow?2:key.program==UI?5:7,fs=key.program==Scene?1:key.program==Shadow?4:key.program==UI?6:key.program==Post?8:key.program==SkyProgram?9:key.program==WarpProgram?10:key.program==TonemapProgram?12:key.program==EffectProgram?13:11;
        stages[0]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=shaders[vs];stages[0].pName="main";stages[1]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=shaders[fs];stages[1].pName="main";if(key.program==Shadow){stages[2]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[2].stage=VK_SHADER_STAGE_GEOMETRY_BIT;stages[2].module=shaders[3];stages[2].pName="main";count=3;}
        if(key.tess)for(auto [stage,module]:{std::pair{VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,shaders[14]},{VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,shaders[15]}}){stages[count]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[count].stage=stage;stages[count].module=module;stages[count++].pName="main";}
        const VkBool32 on=VK_TRUE;const VkSpecializationMapEntry entry{0,0,sizeof(on)};const VkSpecializationInfo spec{1,&entry,sizeof(on),&on};if(key.program==Scene&&smoothing())stages[1].pSpecializationInfo=&spec; // scene.frag k22SmoothNormals.
        VkVertexInputBindingDescription binding{0,static_cast<std::uint32_t>(key.program==UI?sizeof(UIVertex):sizeof(Vertex)+sizeof(Vec3)),VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription,5>attributes{};unsigned attrs=0;
        if(key.program==Scene||key.program==Shadow){attributes={{{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,position)},{1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,normal)},{2,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,uv)},{3,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,lightmap_uv)},{4,0,VK_FORMAT_R32G32B32_SFLOAT,sizeof(Vertex)}}};attrs=key.program==Shadow?1:5;}
        else if(key.program==UI){attributes[0]={0,0,VK_FORMAT_R32G32_SFLOAT,0};attributes[1]={1,0,VK_FORMAT_R32G32_SFLOAT,8};attributes[2]={2,0,VK_FORMAT_R32G32B32A32_SFLOAT,16};attributes[3]={3,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(UIVertex,u0)};attrs=4;}
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vi.vertexBindingDescriptionCount=attrs?1:0;vi.pVertexBindingDescriptions=&binding;vi.vertexAttributeDescriptionCount=attrs;vi.pVertexAttributeDescriptions=attributes.data();VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        if(key.tess)ia.topology=VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;VkPipelineTessellationStateCreateInfo ts{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};ts.patchControlPoints=3;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=key.cull?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_NONE;rs.frontFace=key.clockwise?VK_FRONT_FACE_CLOCKWISE:VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=static_cast<VkSampleCountFlagBits>(key.samples);VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};ds.depthTestEnable=key.depth;ds.depthWriteEnable=key.write;ds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;ds.stencilTestEnable=key.stencil!=0;ds.front={VK_STENCIL_OP_KEEP,VK_STENCIL_OP_KEEP,VK_STENCIL_OP_KEEP,key.stencil==1?VK_COMPARE_OP_ALWAYS:VK_COMPARE_OP_EQUAL,255,key.stencil==1?255u:0u,1};if(key.stencil==1)ds.front.passOp=VK_STENCIL_OP_REPLACE;ds.back=ds.front;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=key.stencil==1?0:15;blend.blendEnable=key.blend;blend.srcColorBlendFactor=key.premultiplied==1?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_SRC_ALPHA;blend.dstColorBlendFactor=key.additive?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.colorBlendOp=VK_BLEND_OP_ADD;blend.srcAlphaBlendFactor=key.premultiplied==1?VK_BLEND_FACTOR_ONE:(key.program==UI||key.premultiplied==2)?VK_BLEND_FACTOR_SRC_ALPHA:VK_BLEND_FACTOR_ONE;blend.dstAlphaBlendFactor=(key.program==UI||key.premultiplied==2)&&key.additive?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.alphaBlendOp=VK_BLEND_OP_ADD;
        if(key.premultiplied>=3){blend.srcColorBlendFactor=key.premultiplied==3?VK_BLEND_FACTOR_ZERO:VK_BLEND_FACTOR_ONE;blend.dstColorBlendFactor=key.premultiplied==3?VK_BLEND_FACTOR_SRC_COLOR:VK_BLEND_FACTOR_ONE;blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE;}
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=key.colorFormat?1:0;cb.pAttachments=&blend;std::array<VkDynamicState,2>dyn{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dy.dynamicStateCount=dyn.size();dy.pDynamicStates=dyn.data();VkFormat format=VkFormat(key.colorFormat),depthFormat=VkFormat(key.depthFormat);VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};rendering.colorAttachmentCount=key.colorFormat?1:0;rendering.pColorAttachmentFormats=&format;rendering.depthAttachmentFormat=depthFormat;rendering.stencilAttachmentFormat=depthFormat==VK_FORMAT_D32_SFLOAT?VK_FORMAT_UNDEFINED:depthFormat;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.pNext=&rendering;ci.stageCount=count;ci.pStages=stages.data();ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&vp;ci.pRasterizationState=&rs;ci.pMultisampleState=&ms;ci.pDepthStencilState=&ds;ci.pColorBlendState=&cb;ci.pDynamicState=&dy;ci.layout=pipelineLayout;VkPipeline result{};
        if(key.tess)ci.pTessellationState=&ts;
        try{require(vkCreateGraphicsPipelines(s.device,VK_NULL_HANDLE,1,&ci,nullptr,&result),"Vulkan raster pipeline");pipelines.emplace(key,result);}catch(...){if(result)vkDestroyPipeline(s.device,result,nullptr);throw;}
        return result;
    }
    void end(){if(active){vkCmdEndRendering(s.command);active=false;}}
    VkImageView attachment_view(const Image&image,std::uint32_t mip=0,std::uint32_t layer=0){
        if(mip>=image.levels||layer>=image.layers)throw std::runtime_error("Vulkan attachment subresource out of range");
        if(image.levels==1&&image.layers==1)return image.view;
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=image.handle;ci.viewType=VK_IMAGE_VIEW_TYPE_2D;ci.format=image.format;ci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,mip,1,layer,1};
        auto&views=arenas[s.slotIndex].views;views.emplace_back();
        try{require(vkCreateImageView(s.device,&ci,nullptr,&views.back()),"raster attachment view");}catch(...){views.pop_back();throw;}
        return views.back();
    }
    void begin(Target&t){end();target=&t;renderExtent={t.color.width,t.color.height};s.transition(t.color,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);s.transition(t.depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=t.color.view;color.imageLayout=t.color.layout;color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};depth.imageView=t.depth.view;depth.imageLayout=t.depth.layout;depth.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea={{0,0},renderExtent};ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&color;ri.pDepthAttachment=&depth;ri.pStencilAttachment=&depth;vkCmdBeginRendering(s.command,&ri);active=true;}
    void begin_image(Image&image,VkImageView alternate=VK_NULL_HANDLE,std::uint32_t width=0,std::uint32_t height=0,bool load=false){end();target=nullptr;renderColor=image.format;renderDepth=VK_FORMAT_UNDEFINED;renderSamples=image.samples;renderExtent={width?width:image.width,height?height:image.height};s.transition(image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|(load?VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:0));VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=alternate?alternate:attachment_view(image);color.imageLayout=image.layout;color.loadOp=load?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_DONT_CARE;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea={{0,0},renderExtent};ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&color;vkCmdBeginRendering(s.command,&ri);active=true;}
    // Depth-only (shadow map face) or color+depth (G-buffer) rendering into single-sample effect images; both are cleared.
    void begin_offscreen(Image*color,Image&depth,VkImageView depthView,std::uint32_t width,std::uint32_t height){
        end();target=nullptr;renderColor=color?color->format:VK_FORMAT_UNDEFINED;renderDepth=depth.format;renderSamples=VK_SAMPLE_COUNT_1_BIT;renderExtent={width,height};
        if(color)s.transition(*color,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        s.transition(depth,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo c{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};if(color){c.imageView=color->view;c.imageLayout=color->layout;c.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;c.storeOp=VK_ATTACHMENT_STORE_OP_STORE;}
        VkRenderingAttachmentInfo d{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};d.imageView=depthView;d.imageLayout=depth.layout;d.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;d.storeOp=VK_ATTACHMENT_STORE_OP_STORE;d.clearValue.depthStencil={1,0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea={{0,0},renderExtent};ri.layerCount=1;ri.colorAttachmentCount=color?1:0;ri.pColorAttachments=color?&c:nullptr;ri.pDepthAttachment=&d;vkCmdBeginRendering(s.command,&ri);active=true;set_region({{0,0},{width,height}});
    }
    VkRect2D bounded(VkRect2D r)const{
        const auto x0=std::clamp<std::int64_t>(r.offset.x,0,renderExtent.width),y0=std::clamp<std::int64_t>(r.offset.y,0,renderExtent.height);
        const auto x1=std::clamp<std::int64_t>(std::int64_t(r.offset.x)+r.extent.width,0,renderExtent.width),y1=std::clamp<std::int64_t>(std::int64_t(r.offset.y)+r.extent.height,0,renderExtent.height);
        return {{std::int32_t(x0),std::int32_t(y0)},{std::uint32_t(x1-x0),std::uint32_t(y1-y0)}};
    }
    void set_region(VkRect2D r){region=r;VkViewport viewport{float(r.offset.x),float(r.offset.y),float(std::max(1u,r.extent.width)),float(std::max(1u,r.extent.height)),0,1};vkCmdSetViewport(s.command,0,1,&viewport);auto scissor=bounded(r);vkCmdSetScissor(s.command,0,1,&scissor);}
    VkRect2D full()const{return {{0,0},renderExtent};}
    VkRect2D rectangle(Rect r)const{if(r.width<=0||r.height<=0)r={0,0,float(metrics.width),float(metrics.height)};const float sx=metrics.scaleX*float(viewExtent.width)/float(s.extent.width),sy=metrics.scaleY*float(viewExtent.height)/float(s.extent.height);return {{std::int32_t(std::lround(r.x*sx)),std::int32_t(std::lround(r.y*sy))},{std::uint32_t(std::max(0l,std::lround(r.width*sx))),std::uint32_t(std::max(0l,std::lround(r.height*sy)))}};}
    void clear(Color c,bool color,bool depth,bool stencil,VkRect2D r){r=bounded(r);if(!r.extent.width||!r.extent.height)return;std::array<VkClearAttachment,2>a{};unsigned n=0;if(color){a[n].aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;a[n].colorAttachment=0;a[n++].clearValue.color={{c.r,c.g,c.b,c.a}};}if(depth||stencil){a[n].aspectMask=(depth?VK_IMAGE_ASPECT_DEPTH_BIT:0)|(stencil?VK_IMAGE_ASPECT_STENCIL_BIT:0);a[n++].clearValue.depthStencil={1,0};}if(n){VkClearRect rect{r,0,1};vkCmdClearAttachments(s.command,n,a.data(),1,&rect);}}
    void bind(PipelineKey key,const Uniform&u,std::array<Image*,6>images={},std::array<VkSampler,6>sampling={},Allocation offsets={}){key.samples=target?target->color.samples:renderSamples;key.colorFormat=target?target->color.format:renderColor;key.depthFormat=target?target->depth.format:renderDepth;vkCmdBindPipeline(s.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(key));auto set=descriptor(u,images,sampling,offsets);vkCmdBindDescriptorSets(s.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,0,nullptr);if(key.program==Scene&&s.rayEnabled){auto rays=rt_set(*s.rays);vkCmdBindDescriptorSets(s.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,1,1,&rays,0,nullptr);}}
    void mipmaps(Image&image){end();s.transition(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);for(std::uint32_t level=1,w=image.width,h=image.height;level<image.levels;++level){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image.handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,level-1,1,0,image.layers};vkCmdPipelineBarrier(s.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);auto nw=std::max(1u,w/2),nh=std::max(1u,h/2);VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level-1,0,image.layers};blit.srcOffsets[1]={int(w),int(h),1};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,image.layers};blit.dstOffsets[1]={int(nw),int(nh),1};vkCmdBlitImage(s.command,image.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);w=nw;h=nh;}if(image.levels>1){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image.handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,image.levels-1,0,image.layers};vkCmdPipelineBarrier(s.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);}image.layout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;s.transition(image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);}
    // upscale: texture_upscale candidate (original world/model skin) 1 edge-addressed, 2 tiling.
    Texture& texture(const k22::Image*source,bool dynamic=false,std::uint64_t serial=0,UICommand::TextureMode mode=UICommand::TextureMode::Default,int upscale=0){
        if(!source)throw std::runtime_error("NULL texture cache request");check_image(*source);if(source->width>s.properties.limits.maxImageDimension2D||source->height>s.properties.limits.maxImageDimension2D)throw std::runtime_error("Game image exceeds Vulkan texture size");
        auto&t=textures[source][std::size_t(mode)];
        if(!t.image.handle||(dynamic&&(!t.dynamic||(t.serial!=serial&&t.submission!=s.submissions)||t.image.width!=source->width||t.image.height!=source->height))){
            end();if(t.image.handle)s.retire(t.image);if(t.normal.handle)s.retire(t.normal);if(t.orm.handle)s.retire(t.orm);t.roughness=false;
            std::vector<std::uint8_t>keyed;std::vector<std::uint16_t>packed;
            auto pixels=std::as_bytes(std::span(source->rgba));VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
            if(mode==UICommand::TextureMode::CursorKey16){
                packed.resize(source->rgba.size()/4);
                for(std::size_t i=0;i<packed.size();++i){const auto*p=source->rgba.data()+i*4;const std::uint16_t raw=(p[0]>>3)<<11|(p[1]>>3)<<6|(p[1]&1)<<5|(p[2]>>3);packed[i]=raw?((raw&0xffc0)|((raw&31)<<1)|1):0;}
                pixels=std::as_bytes(std::span(packed));format=VK_FORMAT_R5G5B5A1_UNORM_PACK16;
            }else if(mode==UICommand::TextureMode::CursorKey24){
                keyed=source->rgba;for(std::size_t i=0;i<keyed.size();i+=4)keyed[i+3]=(keyed[i]|keyed[i+1]|keyed[i+2])?255:0;pixels=std::as_bytes(std::span(keyed));
            }
            // Packs replace immutable Default-mode images only; turbulent CPU warps keep their native source.
            const k22::Image*upload=source;auto pack=s.options.packs&&!dynamic&&mode==UICommand::TextureMode::Default&&!turbulent.contains(source)?s.options.packs->find(*source):std::nullopt;
            if(pack&&pack->albedo&&(pack->albedo->width>s.properties.limits.maxImageDimension2D||pack->albedo->height>s.properties.limits.maxImageDimension2D)){std::fprintf(stderr,"kabus22: texture pack: %s exceeds Vulkan texture size; using original\n",texture_key_hex(texture_key(*source)).c_str());pack.reset();}
            std::optional<k22::Image>upscaled;
            if(pack&&pack->albedo){upload=pack->albedo.get();pixels=std::as_bytes(std::span(upload->rgba));}
            else if(upscale&&s.options.features.upscale&&!dynamic&&mode==UICommand::TextureMode::Default&&!turbulent.contains(source)&&std::max(source->width,source->height)<=512){upscaled=upscale_texture_2x(*source,upscale==2);upload=&*upscaled;pixels=std::as_bytes(std::span(upload->rgba));}
            auto levels=!dynamic&&mode==UICommand::TextureMode::Default?std::bit_width(std::max(upload->width,upload->height)):1u;
            bool generate=levels>1&&frame&&frame->mipmapping>=1;
            t.image=s.image(upload->width,upload->height,format,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,levels);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={upload->width,upload->height,1};s.upload(t.image,pixels,std::span(&copy,1),generate);t.serial=serial;t.dynamic=dynamic;t.mipsValid=generate;t.alpha=dynamic||mode!=UICommand::TextureMode::Default;for(std::size_t i=3;!t.alpha&&i<source->rgba.size();i+=4)t.alpha=source->rgba[i]!=255;
            t.width=source->width;t.height=source->height;
            auto map=[&](const std::shared_ptr<const k22::Image>&image,Image&target){if(!image)return;target=s.image(image->width,image->height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,std::bit_width(std::max(image->width,image->height)));VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={image->width,image->height,1};s.upload(target,std::as_bytes(std::span(image->rgba)),std::span(&region,1),true);};
            if(pack){map(pack->normal,t.normal);map(pack->orm,t.orm);t.roughness=pack->roughness;}
            t.submission=s.submissions;
        }
        // CPU image contents are immutable within this synchronous packet; differing aliases share its owned upload.
        if(dynamic&&t.dynamic)t.serial=serial;
        if(!t.dynamic&&mode==UICommand::TextureMode::Default&&frame&&frame->mipmapping>=1&&!t.mipsValid){if(t.image.levels>1)mipmaps(t.image);t.mipsValid=true;}
        return t;
    }
    Image* sampled(const k22::Image*source,bool dynamic=false,std::uint64_t serial=0,UICommand::TextureMode mode=UICommand::TextureMode::Default){if(!source)return &white;auto&t=texture(source,dynamic,serial,mode);if(mode==UICommand::TextureMode::Default&&source->nativeStorageBytes==2){auto found=warps.find(source);if(found!=warps.end()){auto&w=found->second;if(w.serial==t.serial&&w.image.width==t.image.width&&w.image.height==t.image.height){if(!t.dynamic&&frame&&frame->mipmapping>=1&&!w.mipsValid){if(w.image.levels>1)mipmaps(w.image);w.mipsValid=true;}return &w.image;}}}return &t.image;}
    void turbulence(const k22::Image*source,bool dynamic,std::uint64_t serial){
        if(!source)throw std::runtime_error("Turbulent game surface has no decoded bitmap");if(source->nativeStorageBytes==255)throw std::runtime_error("Native turbulent bitmap CPU storage is unavailable");if(source->nativeStorageBytes!=2)return;turbulent.insert(source);
        auto&t=texture(source,dynamic,serial);if(source->height>source->width)throw std::runtime_error("Turbulent bitmap rows exceed native displacement table");if(source->width==1)return;
        auto&w=warps[source];const int phase=int(std::int64_t(double(frame->turbulenceSpeed)*source->width*frame->animationTimeSeconds)&(source->width-1));if(w.image.handle&&w.image.width==source->width&&w.image.height==source->height&&w.phase==phase&&w.range==frame->turbulenceRange&&w.serial==t.serial){if(!dynamic&&frame->mipmapping>=1&&!w.mipsValid){if(w.image.levels>1)mipmaps(w.image);w.mipsValid=true;}return;}
        const auto oldView=w.image.handle&&w.serial==t.serial&&w.image.width==t.image.width&&w.image.height==t.image.height?w.image.view:t.image.view;
        end();if(w.image.handle)s.retire(w.image);const auto levels=dynamic?1u:std::bit_width(std::max(source->width,source->height));w.image=s.image(source->width,source->height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,levels);
        std::vector<std::int32_t>offsets(source->width);float amplitude=float(double(frame->turbulenceRange)*source->width*.05),angleScale=float(6.28318/(source->width-1));for(unsigned i=0;i<source->width;++i)offsets[i]=turbulence_offset(phase+i,amplitude,angleScale);auto data=allocate(offsets.size()*sizeof(offsets[0]),std::max<VkDeviceSize>(16,s.properties.limits.minStorageBufferOffsetAlignment),offsets.data());
        Uniform u;u.viewport[2]=source->width;u.viewport[3]=source->height;u.parameters[0][2]=float(1u<<(std::bit_width(source->width)-1));begin_image(w.image);set_region({{0,0},{source->width,source->height}});bind({WarpProgram},u,{&t.image},{sampler(true,false)},data);vkCmdDraw(s.command,3,1,0,0);end();w.mipsValid=!dynamic&&frame->mipmapping>=1;if(levels>1&&w.mipsValid)mipmaps(w.image);else s.transition(w.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);w.phase=phase;w.range=frame->turbulenceRange;w.serial=t.serial;
        if(s.rayEnabled)rt_replace_texture(*s.rays,oldView,w.image.view);
    }
    Mesh& mesh(const Model&model){
        auto found=meshes.find(&model);if(found!=meshes.end())return found->second;
        if(model.vertices.empty()&&!model.triangles.empty())throw std::runtime_error("Game mesh has triangles without vertices");
        if(model.vertices.empty()&&!model.frames.empty())throw std::runtime_error("Animation frame has no model vertices");
        Mesh m;std::vector<Triangle>centroidTriangles;centroid_geometry(model,frame&&frame->world&&&frame->world->mesh==&model?frame->world:model.nativeWorld,m.expanded,centroidTriangles);for(auto&v:model.vertices)include_point(m.bounds,v.position);for(auto&f:model.frames){if(f.positions.size()!=model.vertices.size()||(!f.normals.empty()&&f.normals.size()!=model.vertices.size()))throw std::runtime_error("Animation frame must supply complete decoded vertex pose");m.frames.push_back(position_bounds(f.positions));}
        if(model.triangles.size()+centroidTriangles.size()>std::numeric_limits<std::uint32_t>::max()/3u)throw std::runtime_error("Game mesh exceeds Vulkan index capacity");
        m.batches.resize(std::max<std::size_t>(model.materials.size(),1));for(std::size_t i=0;i<m.batches.size();++i)m.batches[i].material=i;
        auto count=[&](const auto&triangles,bool centroid){for(const auto&t:triangles){auto index=display_material_index(model.formatVersion,t.materialIndex);if(index>=m.batches.size())throw std::runtime_error("Game triangle references invalid material");for(auto vertex:t.indices)if(vertex>=(m.expanded.empty()?model.vertices.size():m.expanded.size()))throw std::runtime_error("Game triangle references invalid vertex");(centroid?m.batches[index].centroidCount:m.batches[index].count)+=3;}};count(model.triangles,false);count(centroidTriangles,true);std::uint32_t total=0;for(auto&b:m.batches){b.first=total;total+=b.count;b.count=0;}for(auto&b:m.batches){b.centroidFirst=total;total+=b.centroidCount;b.centroidCount=0;}std::vector<std::uint32_t>indices(total);for(auto&t:model.triangles){auto&b=m.batches[display_material_index(model.formatVersion,t.materialIndex)];for(auto i:t.indices)indices[b.first+b.count++]=i;}for(auto&t:centroidTriangles){auto&b=m.batches[display_material_index(model.formatVersion,t.materialIndex)];for(auto i:t.indices)indices[b.centroidFirst+b.centroidCount++]=i;}if(total)m.indices=s.buffer(indices.size()*sizeof(indices[0]),VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|(s.rayEnabled?VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR:0),false,indices.data());m.batches.erase(std::remove_if(m.batches.begin(),m.batches.end(),[](const Batch&b){return !b.count;}),m.batches.end());
        if(model.formatVersion==7){m.primaryMax=m.batches.empty()?0:m.batches.back().material;m.skinLayers=native_skin_layers(model,m.primaryMax);}
        if(!m.expanded.empty()){m.tangents.resize(model.vertices.size());derive_tangents(model,{},{},m.tangents);m.tangents.resize(m.expanded.size());for(auto&t:centroidTriangles)if(t.indices[0]>=model.vertices.size())m.tangents[t.indices[0]]=add(m.tangents[t.indices[0]],m.tangents[t.indices[1]]);for(std::size_t i=model.vertices.size();i<m.tangents.size();++i)m.tangents[i]=unit(m.tangents[i]);}
        return meshes.emplace(&model,std::move(m)).first->second;
    }
    Matrix model_matrix(const DrawInstance&d,const Camera*camera)const{Vec3 angles=d.angles;if(d.billboard&&camera)angles=d.viewSpace?Vec3{180,0,d.angles.z}:Vec3{camera->angles.x+180,-camera->angles.y,d.angles.z-camera->angles.z};auto m=transform(d.position,angles,d.scale);if(d.viewSpace&&camera)m=product(transform(camera->position,camera->angles,{1,1,1}),m);return m;}
    Bounds bounds(const DrawInstance&d,Mesh&m){
        if(!m.frames.empty()&&(d.frame>=m.frames.size()||d.nextFrame>=m.frames.size()))throw std::runtime_error("Entity animation index outside model");
        auto endpoint=[&](std::uint32_t id,std::uint64_t revision,std::span<const Vec3>positions,std::uint32_t f){if(positions.empty())return m.frames.empty()?m.bounds:m.frames[f];if(!id||positions.size()!=d.model->vertices.size())throw std::runtime_error("Invalid mutable entity position override");auto&cache=overrideBounds[id];if(cache.model!=d.model||cache.revision!=revision||cache.count!=positions.size()){cache={d.model,revision,positions.size(),position_bounds(positions)};}return cache.bounds;};
        auto first=endpoint(d.meshId,d.meshRevision,d.positions,d.frame);auto next=m.frames.empty()&&d.nextPositions.empty()?first:endpoint(d.nextMeshId,d.nextMeshRevision,d.nextPositions,d.nextFrame);return interpolated_bounds(first,next,d.interpolation);
    }
    void prepare(const DrawInstance&d,bool shadow=false){
        if(!d.model||(!shadow&&!d.visible&&!d.shadowRequested)||d.scale.x==0||d.scale.y==0||d.scale.z==0)return;
        auto&m=mesh(*d.model);if(m.batches.empty())return;
        auto*live=script_material(frame,d.materialIndex);auto&pose=rt_pose(*s.rays,d,m.expanded,m.tangents,(live&&live->effect!=EffectKind::None)||s.options.pbr);prepared[&d]={&pose};bounds(d,m);
        // Only material skins are texture_upscale candidates: lightmaps, sky strips, UI, fonts and particles keep originals.
        auto upload=[&](const k22::Image*image,int upscale){auto binding=resolved_skin(image,frame->skinBindings);if(binding.image)texture(binding.image,binding.dynamic,binding.serial,UICommand::TextureMode::Default,upscale);};
        const int tiling=(frame->world&&d.model==&frame->world->mesh)||(frame->door.world&&d.model==&frame->door.world->mesh)?2:1;
        for(auto&mat:d.model->materials){if(d.model->formatVersion==6&&(mat.flags&15)==9&&mat.texture)if(auto binding=resolved_skin(mat.texture.get(),frame->skinBindings);binding.image)turbulent.insert(binding.image);upload(mat.texture.get(),(mat.flags&15)==5?0:tiling);if(mat.lightmap)texture(mat.lightmap.get());}
        if(live)for(auto image:live->skins)upload(image,1);
    }
    Sky& sky_texture(const k22::Image&identity){
        auto found=skies.find(&identity);if(found!=skies.end())return found->second;
        auto pack=s.options.packs?s.options.packs->find(identity):std::nullopt;const k22::Image&source=pack&&pack->albedo?*pack->albedo:identity;
        check_image(source);const std::uint32_t frames=source.width==source.height*6?6:source.width==source.height*2?2:0;if(!frames)throw std::runtime_error("Bitmap is not a native sky strip");if(frames==2&&!source.sourceDepth&&source.nativeStorageBytes==255)throw std::runtime_error("Native cloud bitmap storage depth is unavailable");end();
        auto width=source.width/frames,height=source.height;const auto levels=std::bit_width(std::max(width,height));Sky sky;sky.frames=frames;Image original=s.image(width,height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,levels,frames);std::vector<std::uint8_t>pixels(std::size_t(width)*height*frames*4);std::vector<VkBufferImageCopy>copies(frames);for(unsigned layer=0;layer<frames;++layer){for(unsigned row=0;row<height;++row)std::memcpy(pixels.data()+(std::size_t(layer)*width*height+std::size_t(row)*width)*4,source.rgba.data()+(std::size_t(row)*source.width+layer*width)*4,width*4);copies[layer].bufferOffset=std::size_t(layer)*width*height*4;copies[layer].imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,layer,1};copies[layer].imageExtent={width,height,1};}s.upload(original,std::as_bytes(std::span(pixels)),copies,true);
        const bool convert=frames==2&&((source.sourceDepth>0&&source.sourceDepth<=16)||source.nativeStorageBytes==2);
        if(convert){sky.image=s.image(width,height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,levels,frames);for(unsigned level=0,w=width,h=height;level<levels;++level,w=std::max(1u,w/2),h=std::max(1u,h/2))for(unsigned layer=0;layer<frames;++layer){auto viewHandle=attachment_view(sky.image,level,layer);Uniform u;u.parameters[2][1]=float(layer);u.parameters[2][2]=float(level);u.parameters[2][3]=layer>0?1:0;begin_image(sky.image,viewHandle,w,h);set_region({{0,0},{w,h}});bind({CloudProgram},u,{&original},{sampler(true,false)});vkCmdDraw(s.command,3,1,0,0);end();}s.transition(sky.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);s.retire(original);}else sky.image=original;
        return skies.emplace(&identity,std::move(sky)).first->second;
    }
    struct MaterialDraw {Uniform u;std::array<Image*,6>images{};std::array<VkSampler,6>sampling{};bool native{},blend{true},additive{},depthWrite{true};RayMaterial ray;};
    MaterialDraw material(const DrawInstance&d,const Batch&b,const Camera&camera,bool depthWrite,const NativeEntityLightState&light,std::unordered_map<std::uint32_t,Effect>&bindings,const Matrix&model,bool rasterDraw){
        auto&meshGPU=mesh(*d.model);const bool worldSurface=d.model->formatVersion==6,nativeSubsets=camera.perDrawOpacity&&d.model->formatVersion==7,nativeModelPass=camera.perDrawOpacity&&model_has_entity_skins(*d.model)&&!maskPass;
        const auto*mat=nativeSubsets&&b.material>0?&d.model->materials[b.material]:model_material(*d.model,b.material,d.skinIndex);const auto*live=script_material(frame,d.materialIndex);const bool effect=live&&live->effect!=EffectKind::None;const int type=mat?mat->flags&15:0;
        MaterialDraw result;auto&u=result.u;u=view;u.model=model;for(int col=0;col<3;++col){float squared=u.model[col*4]*u.model[col*4]+u.model[col*4+1]*u.model[col*4+1]+u.model[col*4+2]*u.model[col*4+2];for(int row=0;row<3;++row)u.normalMatrix[col*4+row]=squared>0?u.model[col*4+row]/squared:0;}
        u.flags[0][1]=d.spriteFrames;u.flags[0][2]=d.frame;u.flags[0][3]=d.nextFrame;u.parameters[0][1]=std::clamp(d.interpolation,0.f,1.f);u.flags[1][1]=d.unlit||!frame->hardwareLighting||(worldSurface&&type==3);u.flags[1][2]=d.flare;u.flags[1][3]=d.overlay;u.flags[2][0]=d.invertFlare;u.flags[2][2]=d.noFog;u.flags[2][3]=maskPass;u.flags[3][0]=live?int(live->effect):0;u.flags[3][1]=live&&frame->hardwareLighting;u.flags[3][2]=worldSurface&&!d.unlit&&type!=3&&!maskPass;u.normalUv[3]=d.model->clockwise?-1:1;
        if(worldSurface&&mat&&mat->raw.size()!=64)throw std::runtime_error("WMB material lacks native surface metadata");int nativeAmbient=worldSurface&&mat?std::int8_t(mat->raw[42]):0,nativeAlbedo=worldSurface&&mat?std::int8_t(mat->raw[43]):0;u.parameters[0][0]=worldSurface&&mat&&(mat->flags&0x2000)?surface_reflection(nativeAlbedo,d.reflectivity,d.objectId!=0):0;u.normalUv[2]=surface_ambient(nativeAmbient,int(camera.nativeAmbient),d.objectId!=0);u.fogParameters[3]=worldSurface?surface_light(nativeAmbient,int(camera.nativeAmbient),d.objectId!=0)/255.f:model_ambient(d.ambient,int(camera.nativeAmbient));
        if(u.door[0][3]==2){u.flags[0][0]=0;u.effects={};u.parameters[0][0]=0;} // Door world: lightmaps only, no current-level lights, shadow maps, ray queries or reflections.
        if(mat&&!mat->textureName.empty()&&!mat->texture)throw std::runtime_error("Unresolved game skin: "+mat->textureName);if(mat&&!mat->lightmapName.empty()&&!mat->lightmap)throw std::runtime_error("Unresolved game lightmap: "+mat->lightmapName);
        auto entitySkin=resolved_skin(mat&&mat->texture?mat->texture.get():nullptr,frame->skinBindings);auto skin=entitySkin.image?entitySkin:resolved_skin(live?live->skins[0]:nullptr,frame->skinBindings);const auto*image=skin.image;result.native=nativeModelPass;result.depthWrite=depthWrite;result.additive=d.additive;if(nativeModelPass){auto pass=model_pass_state(d,camera,entitySkin.nativeTextureAlpha,depthWrite);result.blend=pass.blend;result.additive=pass.additive;result.depthWrite=pass.depthWrite;}
        u.flags[1][3]=overlay_keyed(d,image);
        // Shared bitmap mutation follows admitted raster material encounters, never speculative RT/preload visits.
        if(rasterDraw&&worldSurface&&type==9)turbulence(image,skin.dynamic,skin.serial);
        auto baseScale=mat?mat->uvScale:Vec2{1,1};auto uvScale=model_uv_scale(d.model->formatVersion,baseScale,image);auto normalScale=baseScale;
        if(effect){
            const k22::Image*normalImage=nullptr;if(model_has_entity_skins(*d.model)){int following=nativeSubsets?meshGPU.primaryMax+1:int(d.skinIndex)+1;if(following>=0&&std::size_t(following)<d.model->materials.size()){if(nativeSubsets&&meshGPU.skinLayers>=2)following=int(b.material)+1;if(std::size_t(following)<d.model->materials.size())normalImage=d.model->materials[following].texture.get();}}if(!normalImage)normalImage=live->skins[1];auto normal=resolved_skin(normalImage,frame->skinBindings);auto&retained=bindings[live->sourceId];if(retained.generation!=live->effectGeneration||retained.kind!=live->effect)retained={live->effectGeneration,live->effect,{}};std::array<SkinBinding,2>newBanks{skin,normal};for(unsigned bank=0;bank<2;++bank){if(newBanks[bank].image){auto&t=texture(newBanks[bank].image,newBanks[bank].dynamic,newBanks[bank].serial);retained.banks[bank]={newBanks[bank].image,t.width,t.height,t.dynamic,t.alpha};}auto&saved=retained.banks[bank];result.images[bank==0?0:3]=saved.identity?sampled(saved.identity,saved.dynamic,textures.at(saved.identity)[0].serial):&black;result.sampling[bank==0?0:3]=sampler(true,true,saved.dynamic?0:frame->mipmapping,!saved.dynamic);}
            auto&color=retained.banks[0];auto&normalBank=retained.banks[1];if(color.identity)uvScale=model_uv_scale(d.model->formatVersion,baseScale,color.width,color.height);if(normalBank.identity)normalScale=model_uv_scale(d.model->formatVersion,baseScale,normalBank.width,normalBank.height);u.flags[2][1]=color.alpha;
        }else{result.images[0]=sampled(image,skin.dynamic,skin.serial);bool dynamic=image&&textures.at(image)[0].dynamic;result.sampling[0]=sampler(true,false,dynamic?0:frame->mipmapping,true);u.flags[2][1]=image&&textures.at(image)[0].alpha;}
        result.images[1]=sampled(mat?mat->lightmap.get():nullptr);result.sampling[1]=sampler(true,true,frame->mipmapping,true);result.images[2]=view.flags[3][3]?&s.portal.color:&white;result.sampling[2]=sampler(true,true);u.flags[1][0]=mat&&mat->lightmap;
        if(s.options.pbr&&!effect){auto*maps=image?&textures.at(image)[0]:nullptr;result.images[4]=maps&&maps->normal.handle?&maps->normal:&flatNormal;result.images[5]=maps&&maps->orm.handle?&maps->orm:&defaultOrm;result.sampling[4]=result.sampling[5]=sampler(true,false,frame->mipmapping,true);u.pbr[2]=maps&&maps->roughness;u.pbr[3]=legacy_roughness(mat?mat->power:0);}
        Color tint=live?Color{1,1,1,d.tint.a}:d.tint;if(mat&&!worldSurface){tint.r*=mat->albedo.r;tint.g*=mat->albedo.g;tint.b*=mat->albedo.b;tint.a*=mat->albedo.a;}u.tint=vec(tint);u.fogParameters[2]=draw_opacity(d.alpha*(mat?mat->alpha:1),&camera);u.uvTransform={uvScale.x,uvScale.y,mat?mat->uvOffset.x:0,mat?mat->uvOffset.y:0};u.normalUv[0]=normalScale.x;u.normalUv[1]=normalScale.y;
        if(live){constexpr float channel=1.f/255;u.preparedAmbient={live->ambient.r*channel,live->ambient.g*channel,live->ambient.b*channel,1};u.preparedDiffuse={live->diffuse.r*channel,live->diffuse.g*channel,live->diffuse.b*channel,float(selectedLight)};u.preparedSpecular={live->specular.r*channel,live->specular.g*channel,live->specular.b*channel,live->power};u.preparedEmissive={live->emissive.r*channel,live->emissive.g*channel,live->emissive.b*channel,0};if(effect){for(unsigned i=0;i<4;++i){u.skillMain[i]=std::bit_cast<float>(live->skillBits[i]);u.skillExtra[i]=std::bit_cast<float>(live->skillBits[i+4]);}}else{u.ambientLight=vec(material_ambient_light(d,&camera,&light));u.ambientLight[3]=material_light_strength(live->albedoRaw,light.scalar,frame->bonusProfile,frame->nativeFixedLightBank);}}
        auto&ray=result.ray;ray.texture=result.images[0]->view;ray.sampler=result.sampling[0];ray.normalTexture=(result.images[3]?result.images[3]:&white)->view;ray.normalSampler=result.sampling[3]?result.sampling[3]:sampler(true,true);ray.uvTransform=u.uvTransform;ray.normalUvTransform={normalScale.x,normalScale.y,u.uvTransform[2],u.uvTransform[3]};ray.tint=tint;ray.alpha=u.fogParameters[2];ray.spriteFrames=d.spriteFrames;ray.frame0=d.frame;ray.frame1=d.nextFrame;ray.interpolation=u.parameters[0][1];ray.flags=(u.flags[1][3]?1u:0)|(d.flare?2u:0)|(d.invertFlare?4u:0)|(u.flags[2][1]?8u:0)|((d.doubleSided||camera.noCull)?16u:0)|(d.model->clockwise?32u:0)|(effect?64u:0)|(live&&live->effect==EffectKind::NormalSunMix?128u:0)|(!effect?256u:0)|(result.additive?512u:0);ray.effectAmbient={u.preparedAmbient[0],u.preparedAmbient[1],u.preparedAmbient[2],u.preparedAmbient[3]};ray.skillMain=u.skillMain;ray.skillExtra=u.skillExtra;ray.effectSunDirection=frame->sunDirection;ray.effectCameraPosition=camera.position;bool qualified=centroid&&b.centroidCount;ray.first=qualified?b.centroidFirst:b.first;ray.count=qualified?b.centroidCount:b.count;
        if(result.blend)ray.flags|=1024u;
        if(live&&!effect&&frame->hardwareLighting)ray.flags|=2048u;
        if(d.unlit)ray.flags|=4096u;
        ray.preparedAmbient=u.preparedAmbient;ray.preparedDiffuse=u.preparedDiffuse;ray.preparedSpecular=u.preparedSpecular;ray.preparedEmissive=u.preparedEmissive;ray.ambientLight=u.ambientLight;
        ray.infiniteViewer={u.parameters[2][1],u.parameters[2][2],u.parameters[2][3]};ray.lightCount=u.flags[0][0];ray.selectedLight=static_cast<std::int32_t>(u.preparedDiffuse[3]);
        return result;
    }
    bool batch_admitted(const DrawInstance&d,const Batch&b,const Camera&camera,int queue)const{
        bool subsets=camera.perDrawOpacity&&d.model->formatVersion==7;const Material*mat=subsets&&b.material>0?&d.model->materials[b.material]:model_material(*d.model,b.material,d.skinIndex);if(subsets&&b.material>0&&(!mat->texture||!mat->nativePrimarySubset))return false;bool world=d.model->formatVersion==6;int type=mat?mat->flags&15:0;if(maskPass?(!world||!mat||type!=5):(world&&mat&&(type==5||type==11||(portalPass&&(mat->flags&0x2000)))))return false;if(!maskPass&&camera.perDrawOpacity&&model_has_entity_skins(*d.model)&&queue>=0){auto skin=resolved_skin(mat&&mat->texture?mat->texture.get():nullptr,frame->skinBindings);if(model_translucent(d,skin.nativeTextureAlpha)!=(queue==1))return false;}return true;
    }
    unsigned model_queue(const DrawInstance&d){unsigned result=0;for(auto&b:mesh(*d.model).batches){const Material*mat=d.model->formatVersion==7&&b.material>0?&d.model->materials[b.material]:model_material(*d.model,b.material,d.skinIndex);if(d.model->formatVersion==7&&b.material>0&&(!mat->texture||!mat->nativePrimarySubset))continue;auto skin=resolved_skin(mat&&mat->texture?mat->texture.get():nullptr,frame->skinBindings);result|=model_translucent(d,skin.nativeTextureAlpha)?2u:1u;}return result;}
    void draw_instance(const DrawInstance&d,const Camera&camera,bool depthWrite=true,int queue=-1,Encounter encounter=Encounter::Live){
        const unsigned slot=depthWrite?0:1;auto p=prepared.end();
        if(encounter==Encounter::Replay){p=prepared.find(&d);if(p==prepared.end()||!(p->second.admitted&(1u<<slot)))return;queue=p->second.queues[slot];}
        else if((!d.visible&&!d.shadowRequested)||!d.model||d.scale.x==0||d.scale.y==0||d.scale.z==0||(maskPass&&(!d.visible||d.model->formatVersion!=6)))return;
        auto&m=mesh(*d.model);if(m.batches.empty())return;auto model=model_matrix(d,&camera);
        if(encounter!=Encounter::Replay&&!geometry_eligible(bounds(d,m),model,view.viewProjection,view.clipPlane))return;
        NativeEntityLightState liveLight;const NativeEntityLightState*light;
        if(encounter==Encounter::Replay)light=&p->second.lights[slot];
        else{
            liveLight=!maskPass&&frame->prepareEntityLight?frame->prepareEntityLight(frame->lightContext,d,camera):NativeEntityLightState{d.nativeLightScalar,d.nativeLightDelta,d.nativeLightBias};light=&liveLight;
            if(s.rayEnabled&&!maskPass&&d.objectId)actorLights.insert_or_assign(d.objectId,liveLight);
            if(encounter==Encounter::Prepare){
                p=prepared.find(&d);if(p==prepared.end())throw std::runtime_error("Eligible Vulkan draw has no shared prepared GPU pose");
                p->second.lights[slot]=liveLight;p->second.queues[slot]=queue;p->second.admitted|=1u<<slot;p->second.latest=slot;return;
            }
        }
        if(!d.visible)return;auto*live=script_material(frame,d.materialIndex);if(d.alpha<=0&&(!live||live->effect==EffectKind::None))return;
        if(p==prepared.end())p=prepared.find(&d);if(p==prepared.end())throw std::runtime_error("Eligible Vulkan draw has no shared prepared GPU pose");auto&pose=*p->second.pose;VkDeviceSize offset=0;vkCmdBindVertexBuffers(s.command,0,1,&pose.vertices.handle,&offset);vkCmdBindIndexBuffer(s.command,m.indices.handle,0,VK_INDEX_TYPE_UINT32);
        for(auto&b:m.batches){
            if(!batch_admitted(d,b,camera,queue))continue;
            bool clockwise=(camera.horizontalFov<0)!=(camera.pixelAspect<0);clockwise^=d.model->clockwise;clockwise^=d.scale.x*d.scale.y*d.scale.z<0;
            // Vulkan's signed framebuffer area and shader Y inversion cancel; retain native GL winding.
            if(maskPass){Uniform u=view;u.model=model;u.flags[2][3]=1;bind(tessellated(d,{Scene,0,1,0,0,0,0,!(d.doubleSided||d.billboard||camera.noCull),clockwise,1}),u);vkCmdDrawIndexed(s.command,b.count,1,b.first,0,0);continue;}
            auto*destination=target;auto viewport=region;
            auto draw=material(d,b,camera,depthWrite,*light,effects,model,true);
            if(!active){begin(*destination);set_region(viewport);}
            if(d.spriteFrames>1&&(d.frame>=d.spriteFrames||d.nextFrame>=d.spriteFrames))throw std::runtime_error("Sprite frame index outside texture strip");
            bind(tessellated(d,{Scene,0,1,draw.depthWrite,draw.blend,draw.additive,draw.native?2:0,!(d.doubleSided||d.billboard||camera.noCull),clockwise,0}),draw.u,draw.images,draw.sampling);
            bool qualified=centroid&&b.centroidCount;vkCmdDrawIndexed(s.command,qualified?b.centroidCount:b.count,1,qualified?b.centroidFirst:b.first,0,0);
        }
    }
    void draw_planar(const DrawInstance&d,const Camera&camera,Encounter encounter=Encounter::Live){
        auto found=prepared.end();
        if(encounter==Encounter::Replay){found=prepared.find(&d);if(found==prepared.end()||!(found->second.admitted&4u))return;}
        else if(!d.model||(d.model->formatVersion!=4&&d.model->formatVersion!=5&&d.model->formatVersion!=7)||d.viewSpace||d.scale.x==0||d.scale.y==0||d.scale.z==0)return;
        auto&m=mesh(*d.model);if(m.batches.empty())return;auto bound=bounds(d,m);
        NativeEntityLightState liveLight;const NativeEntityLightState*light;
        if(encounter==Encounter::Replay)light=&found->second.lights[2];
        else{
            liveLight=frame->prepareEntityLight?frame->prepareEntityLight(frame->lightContext,d,camera):NativeEntityLightState{d.nativeLightScalar,d.nativeLightDelta,d.nativeLightBias,d.nativeShadow};light=&liveLight;
            if(s.rayEnabled&&d.objectId)actorLights.insert_or_assign(d.objectId,liveLight);
            if(encounter==Encounter::Prepare){
                found=prepared.find(&d);if(found==prepared.end())throw std::runtime_error("Planar Vulkan draw has no shared prepared pose");
                found->second.lights[2]=liveLight;found->second.admitted|=4u;found->second.latest=2;return;
            }
        }
        auto model=transform(d.position,d.angles,d.scale);
        auto&shadow=light->shadow;if(!shadow.caster)return;auto projection=planar_projection(d,shadow,*frame);if(!geometry_eligible(bound,product(projection,model),view.viewProjection,view.clipPlane))return;if(found==prepared.end())found=prepared.find(&d);if(found==prepared.end())throw std::runtime_error("Planar Vulkan draw has no shared prepared pose");Uniform u=view;u.model=model;u.projection=projection;u.viewport[0]=region.extent.width;u.viewport[1]=region.extent.height;u.parameters[0][2]=-2.f*frame->clipSize;auto alpha=std::uint8_t(std::int64_t(std::trunc(frame->shadowAlpha*.01f*camera.opacityByte)));u.fogParameters[2]=alpha/255.f;u.flags[2][2]=d.noFog;bool reversed=(camera.horizontalFov<0)!=(camera.pixelAspect<0);reversed^=d.model->clockwise;reversed^=d.scale.x*d.scale.y*d.scale.z<0;u.parameters[2][0]=reversed;bind({Shadow,0,1,1,1},u);VkDeviceSize offset=0;vkCmdBindVertexBuffers(s.command,0,1,&found->second.pose->vertices.handle,&offset);vkCmdBindIndexBuffer(s.command,m.indices.handle,0,VK_INDEX_TYPE_UINT32);for(auto&b:m.batches)vkCmdDrawIndexed(s.command,b.count,1,b.first,0,0);
    }
    void ray_scene(const Camera&camera){
        if(!s.rayEnabled)return;
        end();
        std::vector<RayGeometry>geometry;
        std::vector<std::vector<RayMaterial>>materials;
        materials.reserve(frame->instances.size()*2+1);geometry.reserve(frame->instances.size()*2+1);
        auto retained=effects;
        auto addGeometry=[&](const DrawInstance&d,bool depthWrite=true,int queue=-1){
            if(!d.visible||!d.model||d.viewSpace||d.billboard||d.scale.x==0||d.scale.y==0||d.scale.z==0)return;
            auto found=prepared.find(&d);if(found==prepared.end())return;
            auto&m=mesh(*d.model);
            auto model=model_matrix(d,&camera);
            if(m.batches.empty())return;
            auto*live=script_material(frame,d.materialIndex);
            if(d.alpha<=0&&(!live||live->effect==EffectKind::None))return;
            materials.emplace_back();auto&list=materials.back();list.reserve(m.batches.size());
            const NativeEntityLightState packetLight{d.nativeLightScalar,d.nativeLightDelta,d.nativeLightBias};
            const NativeEntityLightState*light=&packetLight;
            if(d.objectId){auto latest=actorLights.find(d.objectId);if(latest!=actorLights.end())light=&latest->second;}
            else if(found->second.latest<found->second.lights.size())light=&found->second.lights[found->second.latest];
            std::unordered_map<std::uint32_t,Effect>isolated;
            auto*simulated=&retained;
            if(live&&live->effect!=EffectKind::None&&!geometry_eligible(bounds(d,m),model,view.viewProjection,view.clipPlane)){
                auto previous=retained.find(live->sourceId);
                if(previous!=retained.end())isolated.emplace(previous->first,previous->second);
                simulated=&isolated;
            }
            for(auto&b:m.batches){if(!batch_admitted(d,b,camera,queue))continue;list.push_back(material(d,b,camera,depthWrite,*light,*simulated,model,false).ray);}
            if(!list.empty())geometry.push_back({&d,found->second.pose,&m.indices,list,model,view.clipPlane});
        };
        if(camera.drawWorld&&frame->world&&(camera.layers&1))addGeometry(worldDraw);
        std::vector<const DrawInstance*>translucent;
        for(auto&d:frame->instances){
            if(camera.noEntities||!frame->renderEntities||!(d.layers&camera.layers)||d.viewSpace||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
            if(camera.perDrawOpacity&&d.model&&model_has_entity_skins(*d.model)){
                if(d.scale.x==0||d.scale.y==0||d.scale.z==0||!d.visible)continue;
                auto queues=model_queue(d);if(queues&1)addGeometry(d,true,0);if(queues&2)translucent.push_back(&d);continue;
            }
            bool alpha=d.alpha<1||d.additive||d.translucent||d.flare;if(d.model)for(auto&mat:d.model->materials)alpha|=mat.alpha<1;
            if(alpha)translucent.push_back(&d);else addGeometry(d);
        }
        std::sort(translucent.begin(),translucent.end(),[&](auto*a,auto*b){auto da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));return dot(da,da)>dot(db,db);});
        for(auto*d:translucent)addGeometry(*d,false,camera.perDrawOpacity&&d->model&&model_has_entity_skins(*d->model)?1:-1);
        rt_scene(*s.rays,geometry);
    }
    void draw_sky(const Camera&camera){
        if(!camera.drawWorld||!frame->world||!(camera.layers&1))return;const k22::Image*cube=nullptr,*dome=nullptr;for(auto&mat:frame->world->mesh.materials)if((mat.flags&15)==5){if(!mat.texture)throw std::runtime_error("SKY surface has no decoded native bitmap");auto&image=*mat.texture;if(image.width==image.height*6)cube=&image;else if(image.width==image.height*2)dome=&image;}auto*image=cube?cube:dome;if(!image)return;auto&sky=skies.at(image);
        clear({},false,false,true,region);maskPass=true;draw_instance(worldDraw,camera);if(frame->renderEntities&&!camera.noEntities)for(auto&d:frame->instances)if(!d.viewSpace&&(d.layers&camera.layers)&&(!camera.excludeEntityId||d.objectId!=camera.excludeEntityId))draw_instance(d,camera);maskPass=false;
        Uniform u;u.normalMatrix=transform({},camera.angles,{1,1,1});float horizontal=camera.horizontalFov<0?-1:1,vertical=camera.pixelAspect<0?-1:1;if(cube){float tangent=std::tan(rad(std::abs(camera.horizontalFov))*.5f)/(camera.widescreen?camera.referenceAspect:float(region.extent.width)/region.extent.height)*std::abs(camera.pixelAspect);horizontal*=2*tangent*float(region.extent.width)/region.extent.height/std::abs(camera.pixelAspect);vertical*=2*tangent;}u.skyConeScroll[0]=horizontal;u.skyConeScroll[1]=vertical;u.skyParameters={cube?1.f:0.f,frame->skyBlend?1.f:0.f,frame->skyCurve*4,0};float argument=frame->skyClip*.01745329238474369f;u.skyParameters[3]=float(double(float(std::sin(double(argument))))-.1);double time=frame->animationTimeSeconds;float clock=float((time-std::int32_t(time/1024.-1024.))*65536.);for(unsigned layer=0;layer<(cube?1u:sky.frames);++layer){u.parameters[2][1]=layer;u.skyConeScroll[2]=frame->skySpeed.x*(layer+1)*clock/(double(image->width/sky.frames)*65536);u.skyConeScroll[3]=frame->skySpeed.y*(layer+1)*clock/(double(image->height)*65536);bind({SkyProgram,0,0,0,1,0,0,0,0,2},u,{&sky.image},{sampler(true,cube!=nullptr,frame->mipmapping,true)});vkCmdDraw(s.command,3,1,0,0);}
    }
    void quad(Rect r,Rect uv,Color color,float angle=0){
        float cx=r.x+r.width*.5f,cy=r.y+r.height*.5f,cs=std::cos(rad(angle)),sn=std::sin(rad(angle));const float u0=std::min(uv.x,uv.x+uv.width),v0=std::min(uv.y,uv.y+uv.height),u1=std::max(uv.x,uv.x+uv.width),v1=std::max(uv.y,uv.y+uv.height);UIVertex v[4];for(int i=0;i<4;++i){float x=(i==1||i==2?r.width:0)-r.width*.5f,y=(i>=2?r.height:0)-r.height*.5f;v[i]={cx+x*cs-y*sn,cy+x*sn+y*cs,uv.x+(i==1||i==2?uv.width:0),uv.y+(i>=2?uv.height:0),color.r,color.g,color.b,color.a,u0,v0,u1,v1};}for(auto i:{0,1,2,0,2,3})uiVertices.push_back(v[i]);
    }
    void flush_ui(Image&image,const Uniform&u,bool additive,bool linear,bool premultiplied=false){
        if(uiVertices.empty())return;auto vertices=allocate(uiVertices.size()*sizeof(UIVertex),16,uiVertices.data());bind({UI,0,0,0,1,additive,premultiplied?1:0},u,{&image},{sampler(linear,true)});vkCmdBindVertexBuffers(s.command,0,1,&vertices.buffer->handle,&vertices.offset);vkCmdDraw(s.command,uiVertices.size(),1,0,0);uiVertices.clear();
    }
    void draw_ui(const UICommand&c){
        Uniform u;u.viewport[0]=metrics.width;u.viewport[1]=metrics.height;u.parameters[1]={c.overlay?1.f:0.f,c.kind==UICommand::Kind::Text?1.f:0.f,(c.cursorOverlay||c.textureMode!=UICommand::TextureMode::Default)?1.f:0.f,0};u.parameters[0][3]=c.alphaRef;uiVertices.clear();Image*image=&white;if(c.kind==UICommand::Kind::Image){if(!c.image)throw std::runtime_error("UI image command has no decoded game image");image=sampled(c.image,c.dynamicImage,c.imageSerial,c.textureMode);quad(c.rect,c.uv,c.color,c.angle);}else if(c.kind==UICommand::Kind::Solid)quad(c.rect,{0,0,1,1},c.color,c.angle);else{if(!c.font)throw std::runtime_error("UI text command has no font atlas");image=sampled(&c.font->image);float x=c.rect.x,y=c.rect.y;for(unsigned char ch:c.text){if(ch=='\n'){x=c.rect.x;y+=c.font->lineHeight*c.scaleY;continue;}auto&g=c.font->glyphs[ch];if(g.pixels.width&&g.pixels.height)quad({x+g.bearing.x*c.scaleX,y+(c.font->ascent-g.bearing.y)*c.scaleY,g.pixels.width*c.scaleX,g.pixels.height*c.scaleY},{g.pixels.x/c.font->image.width,g.pixels.y/c.font->image.height,g.pixels.width/c.font->image.width,g.pixels.height/c.font->image.height},c.color);x+=(g.advance+c.advanceDelta)*c.scaleX;}}
        begin(s.scene);set_region(full());if(c.clip.width>0&&c.clip.height>0){auto clip=rectangle(c.clip);auto left=std::clamp(clip.offset.x,0,int(s.extent.width)),top=std::clamp(clip.offset.y,0,int(s.extent.height));auto right=std::clamp(std::int64_t(clip.offset.x)+clip.extent.width,std::int64_t(left),std::int64_t(s.extent.width)),bottom=std::clamp(std::int64_t(clip.offset.y)+clip.extent.height,std::int64_t(top),std::int64_t(s.extent.height));clip={{left,top},{std::uint32_t(right-left),std::uint32_t(bottom-top)}};vkCmdSetScissor(s.command,0,1,&clip);}
        flush_ui(*image,u,c.additive,c.filter);
    }
    static const Model&particle_quad(){static const Model quad=[](){Model q;q.vertices={{{0,-.5f,-.5f},{1,0,0},{0,1},{}},{{0,.5f,-.5f},{1,0,0},{1,1},{}},{{0,.5f,.5f},{1,0,0},{1,0},{}},{{0,-.5f,.5f},{1,0,0},{0,0},{}}};q.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};return q;}();return quad;}
    Pose*particlePose{};
    void particles(const Camera&camera){
        if(!camera.drawWorld||frame->particles.empty())return;Matrix basis=transform({},camera.angles,{1,1,1});Vec3 right{-basis[4],-basis[5],-basis[6]},up{basis[8],basis[9],basis[10]},forward{basis[0],basis[1],basis[2]};std::vector<const Particle*>order;order.reserve(frame->particles.size());for(auto&p:frame->particles)if(p.size>0&&(!p.translucent||p.alphaRaw>0)&&p.scaleZ!=0)order.push_back(&p);std::sort(order.begin(),order.end(),[&](auto*a,auto*b){if(a->additive!=b->additive)return !a->additive;if(a->additive&&a->image!=b->image)return std::less<const k22::Image*>{}(a->image,b->image);auto da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));return dot(da,da)>dot(db,db);});auto&m=mesh(particle_quad());VkDeviceSize offset=0;vkCmdBindVertexBuffers(s.command,0,1,&particlePose->vertices.handle,&offset);vkCmdBindIndexBuffer(s.command,m.indices.handle,0,VK_INDEX_TYPE_UINT32);
        auto*destination=target;auto viewport=region;
        // One instanced draw per run of equal image/blend (as GL): per-particle model, tint and flare/overlay/invertFlare go to a
        // storage buffer read by scene.vert (binding 6, particleInstanced); a descriptor set + uniform per particle cost ~0.5 us each.
        std::vector<float> instances;instances.reserve(order.size()*24);
        for(auto*particle:order){auto&p=*particle;float c=std::cos(rad(p.angle)),sn=std::sin(rad(p.angle));Vec3 r=add(mul(right,c),mul(up,sn)),u=add(mul(up,c),mul(right,-sn)),position=p.position;float length=p.size*std::abs(p.scaleZ);if((p.beam||p.streak)&&dot(p.velocity,p.velocity)>0){auto projected=add(p.velocity,mul(forward,-dot(p.velocity,forward)));if(dot(projected,projected)>.000001f){u=unit(projected);r=unit(cross(u,forward));}float speed=std::sqrt(dot(p.velocity,p.velocity));if(p.streak)length+=speed;if(p.beam){u=unit(p.velocity);auto side=cross(u,add(camera.position,mul(p.position,-1)));r=dot(side,side)>.000001f?unit(side):right;length+=speed;position=add(position,mul(p.velocity,-.5f));}}auto n=unit(cross(r,u));auto color=p.color;color.a=particle_alpha(p.alphaRaw,p.translucent,p.additive,p.image!=nullptr);
            const float data[24]{n.x,n.y,n.z,0,r.x*p.size,r.y*p.size,r.z*p.size,0,u.x*length,u.y*length,u.z*length,0,position.x,position.y,position.z,1,color.r,color.g,color.b,color.a,float(p.flare),float(p.overlay),float(p.invertFlare),0};instances.insert(instances.end(),std::begin(data),std::end(data));}
        const auto instanceData=allocate(instances.size()*sizeof(float),std::max<VkDeviceSize>(16,s.properties.limits.minStorageBufferOffsetAlignment),instances.data());
        for(std::size_t first=0;first<order.size();){auto&p=*order[first];std::size_t last=first+1;while(last<order.size()&&order[last]->image==p.image&&order[last]->additive==p.additive)++last;
            auto*image=sampled(p.image);if(!active){begin(*destination);set_region(viewport);}
            Uniform uniform=view;uniform.model=transform({},{},{1,1,1});uniform.normalMatrix=transform({}, {},{1,1,1});uniform.tint={1,1,1,1};uniform.uvTransform={1,1,0,0};uniform.fogParameters[2]=1;uniform.flags[0][1]=1;uniform.flags[1]={0,1,0,0};uniform.flags[2]={0,p.image&&textures.at(p.image)[0].alpha,0,0};uniform.flags[3]={0,0,0,0};uniform.parameters[0][0]=0;uniform.parameters[1][0]=1;
            bool dynamic=p.image&&textures.at(p.image)[0].dynamic;bind({Scene,0,1,0,1,p.additive},uniform,{image},{sampler(true,false,dynamic?0:frame->mipmapping,true)},instanceData);vkCmdDrawIndexed(s.command,6,std::uint32_t(last-first),0,0,std::uint32_t(first));first=last;}
    }
    // ---- Wave-2 effects. Every pass below runs only for an enabled feature; the default frame never reaches them. ----
    VkImageView depth_view(const Image&image,std::uint32_t layer=0){
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=image.handle;ci.viewType=VK_IMAGE_VIEW_TYPE_2D;ci.format=image.format;ci.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,layer,1};
        VkImageView result{};require(vkCreateImageView(s.device,&ci,nullptr,&result),"effect depth view");return result;
    }
    void ensure_dummies(){
        if(dummiesReady)return;
        for(auto [image,layers]:{std::pair{&dummyDepth,1u},{&dummyCube,6u}}){
            *image=s.image(1,1,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,1,layers,VK_SAMPLE_COUNT_1_BIT,layers==6);
            s.transition(*image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearDepthStencilValue one{1,0};VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,layers};
            vkCmdClearDepthStencilImage(s.command,image->handle,image->layout,&one,1,&range);s.transition(*image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(s.physical,VK_FORMAT_D32_SFLOAT,&properties);const bool filtered=properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};ci.magFilter=ci.minFilter=filtered?VK_FILTER_LINEAR:VK_FILTER_NEAREST;ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;ci.compareEnable=VK_TRUE;ci.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        require(vkCreateSampler(s.device,&ci,nullptr,&compareSampler),"shadow comparison sampler");dummiesReady=true;
    }
    void reset_globals(){
        const auto nearest=sampler(false,true);globals={{{&dummyDepth,compareSampler},{&dummyCube,compareSampler},{&white,nearest},{&white,nearest},{&white,nearest},{&dummyDepth,nearest}}};
        if(fx.shadowsValid){globals[0]={&fx.sun,compareSampler};if(fx.shadowIndex>=0)globals[1]={&fx.cube,compareSampler};}
    }
    void ensure_effects(){
        const auto&f=s.options.features;const bool depthEffects=f.ssao||f.fog||f.taa||f.fsr,rt=rtSignals();
        if((!depthEffects&&!f.bloom&&!f.fxaa&&!rt)||(fx.extent.width==s.render.width&&fx.extent.height==s.render.height&&(!f.fsr||(fx.upscaled.width==s.extent.width&&fx.upscaled.height==s.extent.height))&&(!s.generating||(fx.interpolated.width==s.extent.width&&fx.interpolated.height==s.extent.height))))return;
        free_effects(false);fx.extent=s.render;const auto w=s.render.width,h=s.render.height;
        constexpr VkImageUsageFlags color=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,storage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        auto readable=[&](Image&i){s.transition(i,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);};
        auto general=[&](Image&i){s.transition(i,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);};
        if(depthEffects){fx.depth=s.image(w,h,s.depthFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);fx.depthView=depth_view(fx.depth);fx.depthSampled=fx.depth;fx.depthSampled.view=fx.depthView;fx.linear=s.image(w,h,VK_FORMAT_R32_SFLOAT,color);}
        if(f.ssao)fx.ao=s.image(w,h,VK_FORMAT_R16_SFLOAT,color);
        if(f.fog)fx.fog=s.image(std::max(1u,w/2),std::max(1u,h/2),VK_FORMAT_R16G16B16A16_SFLOAT,color);
        if(f.taa||f.fxaa||f.sharpen||f.fsr)fx.ldr=s.image(w,h,VK_FORMAT_R16G16B16A16_SFLOAT,color);
        if(f.fsr){fx.fsrDepth=s.image(w,h,VK_FORMAT_R32_SFLOAT,color);fx.fsrMotion=s.image(w,h,VK_FORMAT_R16G16_SFLOAT,color);fx.upscaled=s.image(s.extent.width,s.extent.height,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|(s.generating?VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT}:0u));readable(fx.upscaled);
            // Frame interpolation copies its source (upscaled) for the next frame and may clear its output.
            if(s.generating){fx.interpolated=s.image(s.extent.width,s.extent.height,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);readable(fx.interpolated);}
            fx.opaque=s.image(w,h,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);fx.reactive=s.image(w,h,VK_FORMAT_R8_UNORM,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);readable(fx.reactive);}
        if(f.fxaa&&f.sharpen)fx.sharp=s.image(w,h,VK_FORMAT_R16G16B16A16_SFLOAT,color);
        if(f.taa&&!f.fsr)for(auto&i:fx.taa){i=s.image(w,h,VK_FORMAT_R16G16B16A16_SFLOAT,color);readable(i);} // FSR replaces TAA (composite).
        if(f.bloom)for(unsigned i=0;i<fx.bloom.size();++i)fx.bloom[i]=s.image(std::max(1u,w>>(i+1)),std::max(1u,h>>(i+1)),VK_FORMAT_R16G16B16A16_SFLOAT,color);
        if(motionVectors()){fx.motion=s.image(w,h,VK_FORMAT_R16G16B16A16_SFLOAT,color);fx.motionDepth=s.image(w,h,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);readable(fx.motion);}
        if(rt){
            for(unsigned k=0;k<2;++k){fx.gdata[k]=s.image(w,h,VK_FORMAT_R16G16B16A16_SFLOAT,color);fx.gdepth[k]=s.image(w,h,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);fx.length[k]=s.image(w,h,VK_FORMAT_R32_SFLOAT,storage);readable(fx.gdata[k]);readable(fx.gdepth[k]);general(fx.length[k]);}
            // Disabled signals keep 1x1 placeholders so every statically used storage binding stays valid.
            const std::array<bool,3>on{f.shadows==ShadowMode::RayTraced,f.gi,f.roughReflections};
            for(unsigned i=0;i<3;++i)for(auto*image:{&fx.raw[i],&fx.tmp[i],&fx.history[0][i],&fx.history[1][i]}){*image=s.image(on[i]?w:1,on[i]?h:1,VK_FORMAT_R16G16B16A16_SFLOAT,storage);general(*image);}
        }
    }
    // Depth-only casters (world and opaque models); alpha-tested texels are rejected as in the scene pass. Materials and
    // descriptor sets are prepared once per frame and replayed by every shadow map face with its view-projection pushed as
    // a constant (scene.vert pass 1), instead of re-preparing every batch for each of the seven faces.
    void prepare_casters(const Camera&camera){
        casterDraws.clear();auto saved=view;view={};view.cameraPosition=vec(camera.position,camera.farClip);view.sunDirection=vec(frame->sunDirection);view.flags[0][0]=std::min<std::size_t>(frame->lights.size(),32);view.pass[0]=1;portalPass=maskPass=false;
        auto retained=effects;
        auto cast=[&](const DrawInstance&d){
            if(!d.visible||!d.model||d.viewSpace||d.billboard||d.scale.x==0||d.scale.y==0||d.scale.z==0||d.alpha<1||d.additive||d.translucent||d.flare)return;
            auto found=prepared.find(&d);if(found==prepared.end())return;auto&m=mesh(*d.model);if(m.batches.empty())return;
            auto model=model_matrix(d,&camera);const NativeEntityLightState light{d.nativeLightScalar,d.nativeLightDelta,d.nativeLightBias};
            const bool clockwise=d.model->clockwise^(d.scale.x*d.scale.y*d.scale.z<0);
            for(auto&b:m.batches){if(!batch_admitted(d,b,camera,-1))continue;auto draw=material(d,b,camera,true,light,retained,model,false);bool q=centroid&&b.centroidCount;casterDraws.push_back({found->second.pose->vertices.handle,m.indices.handle,descriptor(draw.u,draw.images,draw.sampling),!d.doubleSided,clockwise,tessellated(d,{}).tess!=0,q?b.centroidFirst:b.first,q?b.centroidCount:b.count});}
        };
        if(frame->world)cast(worldDraw);if(frame->renderEntities)for(auto&d:frame->instances)cast(d);
        view=saved;
    }
    // Faces turned away from the light are culled (one-sided BSP faces around a light must not occlude it); `flipped`
    // marks the Y-inverted cube-face projection, which reverses the native winding.
    void draw_casters(const Matrix&vp,bool flipped){
        vkCmdPushConstants(s.command,pipelineLayout,casterStages,0,sizeof(Matrix),vp.data());
        if(s.rayEnabled){auto rays=rt_set(*s.rays);vkCmdBindDescriptorSets(s.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,1,1,&rays,0,nullptr);}
        VkPipeline bound{};VkBuffer vertices{},indices{};
        for(auto&c:casterDraws){
            PipelineKey key{Scene,0,1,1,0,0,0,c.cull,c.clockwise!=flipped};key.samples=renderSamples;key.colorFormat=renderColor;key.depthFormat=renderDepth;key.tess=c.tess;auto p=pipeline(key);
            if(p!=bound){vkCmdBindPipeline(s.command,VK_PIPELINE_BIND_POINT_GRAPHICS,p);bound=p;}
            if(c.vertices!=vertices){VkDeviceSize offset=0;vkCmdBindVertexBuffers(s.command,0,1,&c.vertices,&offset);vertices=c.vertices;}
            if(c.indices!=indices){vkCmdBindIndexBuffer(s.command,c.indices,0,VK_INDEX_TYPE_UINT32);indices=c.indices;}
            vkCmdBindDescriptorSets(s.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&c.set,0,nullptr);vkCmdDrawIndexed(s.command,c.count,1,c.first,0,0);
        }
    }
    void shadow_maps(const Camera&camera){
        if(!fx.sun.handle){fx.sun=s.image(sunShadowResolution,sunShadowResolution,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);fx.cube=s.image(cubeShadowResolution,cubeShadowResolution,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,1,6,VK_SAMPLE_COUNT_1_BIT,true);for(unsigned i=0;i<6;++i)fx.faces[i]=depth_view(fx.cube,i);}
        Rect v=camera.viewport;const float aspect=v.width>0&&v.height>0?v.width/v.height:float(metrics.width)/metrics.height;
        fx.sunMatrix=sun_shadow_matrix(camera,aspect,frame->sunDirection,mesh(frame->world->mesh).bounds,sunShadowDistance,sunShadowResolution);
        fx.sunTexel=2/(std::sqrt(fx.sunMatrix[0]*fx.sunMatrix[0]+fx.sunMatrix[4]*fx.sunMatrix[4]+fx.sunMatrix[8]*fx.sunMatrix[8])*sunShadowResolution);
        prepare_casters(camera);begin_offscreen(nullptr,fx.sun,fx.sun.view,sunShadowResolution,sunShadowResolution);draw_casters(fx.sunMatrix,false);end();
        s.transition(fx.sun,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        fx.shadowIndex=shadow_light_index(frame->lights,camera.position);
        if(fx.shadowIndex>=0){
            // Cube faces are stored in GL texel order: cancel the scene shader's Vulkan Y inversion.
            const auto&light=frame->lights[fx.shadowIndex];fx.shadowLight=light.position;fx.shadowRange=light.range;const Matrix flip{1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1};
            for(int face=0;face<6;++face){begin_offscreen(nullptr,fx.cube,fx.faces[face],cubeShadowResolution,cubeShadowResolution);draw_casters(product(flip,cube_face_matrix(light.position,face,cubeShadowNear,light.range)),true);}
            end();s.transition(fx.cube,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        fx.shadowsValid=true;
    }
    Uniform effect_uniform(int mode,float texelWidth,float texelHeight)const{Uniform u=view;u.inverseViewProjection=fx.inverseVP;u.camera=vec(fx.forward,fx.nearPlane);u.range={fx.farPlane,view.fogParameters[0],view.fogParameters[1],0};u.pass={float(mode),float(frameIndex%64)*7.f,texelWidth,texelHeight};return u;}
    // Primary view, before view-space entities clear depth: single-sample depth copy, linear depth, SSAO, volumetric fog.
    void depth_effects(Target&destination,VkRect2D r){
        const auto&f=s.options.features;if(!(f.ssao||f.fog||f.taa||f.fsr))return;end();
        constexpr auto depthStages=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;constexpr auto depthAccess=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        if(destination.depth.samples!=VK_SAMPLE_COUNT_1_BIT){
            // An empty rendering instance resolves sample 0 of the MSAA depth (Vulkan 1.2 depth/stencil resolve).
            s.transition(destination.depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,depthStages,depthAccess);s.transition(fx.depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,depthStages,depthAccess);
            VkRenderingAttachmentInfo d{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};d.imageView=destination.depth.view;d.imageLayout=destination.depth.layout;d.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;d.storeOp=VK_ATTACHMENT_STORE_OP_STORE;d.resolveMode=VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;d.resolveImageView=fx.depth.view;d.resolveImageLayout=fx.depth.layout;
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};ri.renderArea={{0,0},s.render};ri.layerCount=1;ri.pDepthAttachment=&d;vkCmdBeginRendering(s.command,&ri);vkCmdEndRendering(s.command);
        }else{
            s.transition(destination.depth,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);s.transition(fx.depth,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={s.render.width,s.render.height,1};vkCmdCopyImage(s.command,destination.depth.handle,destination.depth.layout,fx.depth.handle,fx.depth.layout,1,&copy);
        }
        s.transition(fx.depth,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        const float tw=1.f/s.render.width,th=1.f/s.render.height;const auto nearest=sampler(false,true),linear=sampler(true,true);
        auto readable=[&](Image&i){s.transition(i,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);};
        begin_image(fx.linear);set_region(full());bind({EffectProgram},effect_uniform(1,tw,th),{&fx.depthSampled},{nearest});vkCmdDraw(s.command,3,1,0,0);end();readable(fx.linear);fx.linearValid=true;
        if(f.ssao){
            begin_image(fx.ao);set_region(full());bind({EffectProgram},effect_uniform(2,tw,th),{&fx.linear},{nearest});vkCmdDraw(s.command,3,1,0,0);end();readable(fx.ao);
            begin(destination);set_region(r);bind({EffectProgram,0,0,0,1,0,3},effect_uniform(3,tw,th),{&fx.ao,&fx.linear},{linear,nearest});vkCmdDraw(s.command,3,1,0,0);
        }
        // Volumetric fog marches at half resolution (alpha keeps each sample's depth) and is added with a depth-aware upsample.
        if(f.fog){begin_image(fx.fog);set_region(full());bind({EffectProgram},effect_uniform(4,1.f/fx.fog.width,1.f/fx.fog.height),{&fx.linear},{nearest});vkCmdDraw(s.command,3,1,0,0);end();readable(fx.fog);
            begin(destination);set_region(r);bind({EffectProgram,0,0,0,1,0,4},effect_uniform(14,tw,th),{&fx.fog,&fx.linear},{nearest,nearest});vkCmdDraw(s.command,3,1,0,0);}
        begin(destination);set_region(r);
    }
    VkDescriptorSet allocate_set(VkDescriptorSetLayout setLayout){
        auto&a=arenas[s.slotIndex];VkDescriptorSet set{};
        for(;;){if(a.pool==a.pools.size()){std::array<VkDescriptorPoolSize,4>sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2048},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4096},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,24576},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1024}}};VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};ci.maxSets=2048;ci.poolSizeCount=sizes.size();ci.pPoolSizes=sizes.data();a.pools.emplace_back();try{require(vkCreateDescriptorPool(s.device,&ci,nullptr,&a.pools.back()),"raster descriptor pool");}catch(...){a.pools.pop_back();throw;}}VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=a.pools[a.pool];ai.descriptorSetCount=1;ai.pSetLayouts=&setLayout;auto result=vkAllocateDescriptorSets(s.device,&ai,&set);if(result==VK_ERROR_OUT_OF_POOL_MEMORY||result==VK_ERROR_FRAGMENTED_POOL){++a.pool;continue;}require(result,"raster descriptor allocation");return set;}
    }
    // Compute set: 0 uniform, 1 lights, 2.. sampled images, 10.. storage images (GENERAL); null entries stay unwritten.
    VkDescriptorSet compute_set(const ComputeUniform&cu,std::span<const std::pair<Image*,VkSampler>>sampledImages,std::span<Image* const>storage){
        auto set=allocate_set(computeSetLayout);auto uniform=allocate(sizeof(cu),std::max<VkDeviceSize>(16,s.properties.limits.minUniformBufferOffsetAlignment),&cu);auto lights=lightAllocation.buffer?lightAllocation:zeroAllocation;
        std::array<VkDescriptorBufferInfo,2>bi{{{uniform.buffer->handle,uniform.offset,uniform.size},{lights.buffer->handle,lights.offset,lights.size}}};std::array<VkDescriptorImageInfo,20>ii{};std::vector<VkWriteDescriptorSet>w;
        auto write=[&](std::uint32_t binding,VkDescriptorType type){VkWriteDescriptorSet x{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};x.dstSet=set;x.dstBinding=binding;x.descriptorCount=1;x.descriptorType=type;w.push_back(x);return &w.back();};
        w.reserve(24);write(0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)->pBufferInfo=&bi[0];write(1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)->pBufferInfo=&bi[1];
        for(std::size_t i=0;i<sampledImages.size();++i){ii[i]={sampledImages[i].second,sampledImages[i].first->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};write(2+i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)->pImageInfo=&ii[i];}
        for(std::size_t i=0;i<storage.size();++i)if(storage[i]){ii[8+i]={VK_NULL_HANDLE,storage[i]->view,VK_IMAGE_LAYOUT_GENERAL};write(10+i,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)->pImageInfo=&ii[8+i];}
        vkUpdateDescriptorSets(s.device,w.size(),w.data(),0,nullptr);return set;
    }
    void dispatch(VkPipeline pipeline,VkDescriptorSet set,int mode,int stride){
        vkCmdBindPipeline(s.command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);std::array<VkDescriptorSet,2>sets{set,rt_set(*s.rays)};vkCmdBindDescriptorSets(s.command,VK_PIPELINE_BIND_POINT_COMPUTE,computeLayout,0,2,sets.data(),0,nullptr);
        const std::array<std::int32_t,2>push{mode,stride};vkCmdPushConstants(s.command,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push.data());vkCmdDispatch(s.command,(s.render.width+7)/8,(s.render.height+7)/8,1);
    }
    void compute_barrier(VkPipelineStageFlags source=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VkAccessFlags written=VK_ACCESS_SHADER_WRITE_BIT){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=written;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;vkCmdPipelineBarrier(s.command,source,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);}
    // One depth-tested prepass draw (G-buffer pass 2, motion-vector pass 3); `previous` and `before` supply the motion pass's
    // previous transform and previous GPU-deformed pose.
    void prepass_draw(const DrawInstance&d,int queue,const Camera&camera,std::unordered_map<std::uint32_t,Effect>&retained,const Matrix&model,const Matrix*previous=nullptr,Pose*before=nullptr){
        if(!d.model||d.scale.x==0||d.scale.y==0||d.scale.z==0)return;auto found=prepared.find(&d);if(found==prepared.end())return;auto&m=mesh(*d.model);if(m.batches.empty())return;
        auto*live=script_material(frame,d.materialIndex);if(d.alpha<=0&&(!live||live->effect==EffectKind::None))return;
        VkDeviceSize offset=0;vkCmdBindVertexBuffers(s.command,0,1,&found->second.pose->vertices.handle,&offset);vkCmdBindIndexBuffer(s.command,m.indices.handle,0,VK_INDEX_TYPE_UINT32);
        const NativeEntityLightState light{d.nativeLightScalar,d.nativeLightDelta,d.nativeLightBias};bool clockwise=(camera.horizontalFov<0)!=(camera.pixelAspect<0);clockwise^=d.model->clockwise;clockwise^=d.scale.x*d.scale.y*d.scale.z<0;
        const Allocation pose=before?Allocation{&before->vertices,0,before->vertices.size}:Allocation{};
        for(auto&b:m.batches){if(!batch_admitted(d,b,camera,queue))continue;auto mat=material(d,b,camera,true,light,retained,model,false);if(previous){mat.u.previousModel=*previous;mat.u.pass[1]=before?1.f:0.f;}bind(tessellated(d,{Scene,0,1,1,0,0,0,!(d.doubleSided||d.billboard||camera.noCull),clockwise,0}),mat.u,mat.images,mat.sampling,pose);bool q=centroid&&b.centroidCount;vkCmdDrawIndexed(s.command,q?b.centroidCount:b.count,1,q?b.centroidFirst:b.first,0,0);}
    }
    // Opaque primary-view entities of the prepasses (the world is drawn by the caller).
    template<class F> void opaque_entities(const Camera&camera,F&&draw){
        for(auto&d:frame->instances){
            if(camera.noEntities||!frame->renderEntities||!(d.layers&camera.layers)||d.viewSpace||!d.visible||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;
            if(camera.perDrawOpacity&&d.model&&model_has_entity_skins(*d.model)){if(model_queue(d)&1)draw(d,0);continue;}
            bool alpha=d.alpha<1||d.additive||d.translucent||d.flare;if(d.model)for(auto&mat:d.model->materials)alpha|=mat.alpha<1;if(!alpha)draw(d,-1);
        }
    }
    // Motion vectors (pass 3) of entities that moved or animated since the previous frame, for TAA and the denoiser; the
    // rest of the scene reprojects through the camera. Transforms are keyed by entity id and trusted from the previous frame only.
    void motion_vectors(const Camera&camera){
        const bool consecutive=fx.motionFrame+1==frameIndex;fx.motionFrame=frameIndex;std::swap(fx.previousModels,fx.currentModels);fx.currentModels.clear();
        // The previous frame's deformed poses (its compute writes) are read as vertex-shader storage.
        end();VkMemoryBarrier poses{VK_STRUCTURE_TYPE_MEMORY_BARRIER};poses.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;poses.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;vkCmdPipelineBarrier(s.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0,1,&poses,0,nullptr,0,nullptr);
        auto saved=view;view.pass={3,0,1.f/s.render.width,1.f/s.render.height};view.previousViewProjection=fx.previousVP;begin_offscreen(&fx.motion,fx.motionDepth,fx.motionDepth.view,s.render.width,s.render.height);auto retained=effects;
        opaque_entities(camera,[&](const DrawInstance&d,int queue){
            auto model=model_matrix(d,&camera);if(d.objectId)fx.currentModels.insert_or_assign(d.objectId,model);
            auto previous=consecutive&&d.objectId?fx.previousModels.find(d.objectId):fx.previousModels.end();auto found=prepared.find(&d);
            Pose*before=consecutive&&found!=prepared.end()?rt_previous_pose(*s.rays,*found->second.pose):nullptr;
            if(!before&&(previous==fx.previousModels.end()||previous->second==model))return;
            prepass_draw(d,queue,camera,retained,model,previous==fx.previousModels.end()?&model:&previous->second,before);
        });
        end();view=saved;s.transition(fx.motion,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);fx.motionValid=true;
    }
    // Primary RT view: G-buffer prepass, stochastic ray-query signals, optional temporal + a-trous denoise (docs/RAYTRACING.md).
    void rt_signals(const Camera&camera){
        const auto&f=s.options.features;const int bits=(f.shadows==ShadowMode::RayTraced?1:0)|(f.gi?2:0)|(f.roughReflections?4:0);const unsigned cur=fx.historyIndex,prev=cur^1u;
        auto saved=view;view.pass[0]=2;begin_offscreen(&fx.gdata[cur],fx.gdepth[cur],fx.gdepth[cur].view,s.render.width,s.render.height);auto retained=effects;
        auto draw=[&](const DrawInstance&d,int queue){prepass_draw(d,queue,camera,retained,model_matrix(d,&camera));};
        if(camera.drawWorld&&frame->world&&(camera.layers&1))draw(worldDraw,-1);
        opaque_entities(camera,draw);
        end();view=saved;s.mark("rt_gbuffer");
        for(auto*image:{&fx.gdata[cur],&fx.gdepth[cur]})s.transition(*image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        for(auto*set:{&fx.raw,&fx.tmp})for(auto&image:*set)s.transition(image,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        // The TLAS and the GPU-deformed poses written this frame are read by compute ray queries.
        compute_barrier(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT);
        ComputeUniform cu;cu.inverseViewProjection=inverse(view.viewProjection);cu.previousViewProjection=fx.previousVP;cu.eye=vec(camera.position,camera.farClip);cu.sun=vec(unit(frame->sunDirection),0.02f);
        cu.params={float(frameIndex&0xffff),1500.f,40.f,camera.nearClip};cu.camera=vec(fx.forward,float(std::min<std::size_t>(frame->lights.size(),32)));cu.config={bits,int(s.render.width),int(s.render.height),fx.historyValid?1:0};cu.actor={float(actorShadowFirst),float(actorShadowCount),std::clamp(frame->shadowAlpha*.01f,0.f,1.f),0};cu.planar=vec(planar_light_direction(frame->shadowSun),actorShadowCasters?1.f:0.f);
        const auto nearest=sampler(false,true);const std::array<std::pair<Image*,VkSampler>,5>edges{{{&fx.gdepth[cur],nearest},{&fx.gdata[cur],nearest},{&fx.gdepth[prev],nearest},{&fx.gdata[prev],nearest},{&fx.motion,nearest}}};
        dispatch(signalsPipeline,compute_set(cu,std::span(edges).first(2),std::array<Image*,3>{&fx.raw[0],&fx.raw[1],&fx.raw[2]}),0,0);s.mark("rt_signals");
        std::array<Image,3>*result=&fx.raw;
        if(f.denoise){
            auto&history=fx.history[cur];auto&before=fx.history[prev];
            auto filter=[&](int mode,int stride,std::array<Image,3>&in,std::array<Image,3>&out){compute_barrier();dispatch(filterPipeline,compute_set(cu,edges,std::array<Image*,11>{&in[0],&in[1],&in[2],&before[0],&before[1],&before[2],&fx.length[prev],&out[0],&out[1],&out[2],&fx.length[cur]}),mode,stride);};
            filter(0,0,fx.raw,history);filter(1,1,history,fx.tmp);filter(1,2,fx.tmp,fx.raw);filter(1,4,fx.raw,fx.tmp);result=&fx.tmp;s.mark("rt_denoise");
        }
        for(auto&image:*result)s.transition(image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        globals[2]={&(*result)[0],nearest};globals[3]={&(*result)[1],nearest};globals[4]={&(*result)[2],nearest};globals[5]={&fx.gdepth[cur],nearest};view.effects[1]=float(bits|int(view.effects[1]));
        fx.historyValid=true;fx.historyIndex=prev;
    }
    // Foreground views depth-test against the background views' depth/stencil. At a different (FSR render) size it cannot
    // be copied: they start from cleared depth/stencil (VIEW entities draw in front of the world anyway).
    void carry_depth(){
        s.transition(s.scene.depth,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        if(s.render.width==s.extent.width&&s.render.height==s.extent.height){
            s.transition(s.hdr.depth,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={s.extent.width,s.extent.height,1};vkCmdCopyImage(s.command,s.hdr.depth.handle,s.hdr.depth.layout,s.scene.depth.handle,s.scene.depth.layout,1,&copy);
        }else{const VkClearDepthStencilValue clear{1,0};const VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};vkCmdClearDepthStencilImage(s.command,s.scene.depth.handle,s.scene.depth.layout,&clear,1,&range);}
    }
    // FSR 3.1 inputs at render size (device depth as R32F, unjittered full motion: entity vectors where the depth agrees,
    // camera reprojection elsewhere), then the upscale of the tone-mapped LDR view into fx.upscaled (display size).
    // FSR reactive input: the primary view's colour once its opaque geometry is drawn (resolved when multisampled).
    void snapshot_opaque(Target&t){
        end();fx.captureOpaque=false;auto&c=t.color;
        s.transition(c,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);s.transition(fx.opaque,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        if(c.samples!=VK_SAMPLE_COUNT_1_BIT){VkImageResolve r{};r.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};r.dstSubresource=r.srcSubresource;r.extent={c.width,c.height,1};vkCmdResolveImage(s.command,c.handle,c.layout,fx.opaque.handle,fx.opaque.layout,1,&r);}
        else{VkImageCopy r{};r.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};r.dstSubresource=r.srcSubresource;r.extent={c.width,c.height,1};vkCmdCopyImage(s.command,c.handle,c.layout,fx.opaque.handle,fx.opaque.layout,1,&r);}
        s.transition(fx.opaque,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);fx.opaqueValid=true;
    }
    void upscale(Uniform u,Image&source){
        const auto nearest=sampler(false,true);
        auto pass=[&](Image&out,int mode,std::array<Image*,6>in){begin_image(out);set_region({{0,0},{out.width,out.height}});u.pass={float(mode),0,1.f/out.width,1.f/out.height};bind({EffectProgram},u,in,{nearest,nearest});vkCmdDraw(s.command,3,1,0,0);end();};
        pass(fx.fsrDepth,11,{&fx.depthSampled});
        u.inverseViewProjection=fx.inverseVP;u.previousViewProjection=fx.previousVP;u.viewProjection=fx.unjittered;u.cameraPosition=vec(fx.eye,fx.farPlane);u.camera=vec(fx.forward,fx.nearPlane);
        pass(fx.fsrMotion,13,{&fx.linear,fx.motionValid?&fx.motion:&black});
        const auto now=std::chrono::steady_clock::now();
        const float ms=fx.fsrTime==std::chrono::steady_clock::time_point{}?16.7f:std::chrono::duration<float,std::milli>(now-fx.fsrTime).count();
        const bool reset=!fx.fsrValid||fx.fsrFrame+1!=frameIndex;fx.fsrTime=now;fx.fsrFrame=frameIndex;fx.fsrValid=true;
        for(auto*i:{&fx.ldr,&fx.fsrDepth,&fx.fsrMotion,&fx.upscaled})s.transition(*i,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        constexpr VkImageUsageFlags color=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,storage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        auto in=[](Image&i,VkImageUsageFlags usage){return Fsr3::Image{i.handle,i.format,{i.width,i.height},usage};};
        Fsr3::Image reactive{};
        if(fx.opaqueValid){s.transition(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);s.fsr->reactive(s.command,in(fx.opaque,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT),in(source,color|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT),in(fx.reactive,storage));
            reactive=in(fx.reactive,storage);s.transition(fx.reactive,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);}
        s.fsr->dispatch({s.command,in(fx.ldr,color),in(fx.fsrDepth,color),in(fx.fsrMotion,color),reactive,in(fx.upscaled,storage),
            fx.fsrJitter,std::clamp(ms,.1f,1000.f),s.options.features.sharpen?.4f:0.f,fx.nearPlane,fx.farPlane,fx.fovY,reset});
        s.transition(fx.upscaled,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);s.mark("fsr");
        fx.upscaledNow=true;fx.fsrReset=reset;fx.fsrMs=std::clamp(ms,.1f,1000.f);
    }
    // FSR 3.1.4 frame generation: optical flow + frame interpolation of this frame's HUD-less upscaled view halfway from
    // the previous one into fx.interpolated, reusing the upscaler's dilated depth/motion (docs/GRAPHICS_ROADMAP.md).
    void interpolate(){
        end();
        constexpr VkImageUsageFlags usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        s.transition(fx.interpolated,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        s.fsr->interpolate({s.command,{fx.upscaled.handle,fx.upscaled.format,{fx.upscaled.width,fx.upscaled.height},usage},
            {fx.interpolated.handle,fx.interpolated.format,{fx.interpolated.width,fx.interpolated.height},usage|VK_IMAGE_USAGE_TRANSFER_DST_BIT},
            fx.fsrMs,fx.nearPlane,fx.farPlane,fx.fovY,fx.fsrReset,++fx.generationFrame});
        s.transition(fx.interpolated,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);s.mark("interpolate");
    }
    // The display-size view image into the LDR scene target exactly like composite()'s last pass (mode 11), with the
    // carried depth restored, so foreground views and UI can be drawn over it once more.
    void scene_from(Image&view){
        end();carry_depth();begin(s.scene);set_region(full());Uniform u;u.pbr={s.options.pbr?1.f:0.f,0,0,0};u.pass={11.f,0,1.f/view.width,1.f/view.height};
        bind({EffectProgram},u,{&view},{sampler(true,true)});vkCmdDraw(s.command,3,1,0,0);
    }
    // Background-view composite: resolve, bloom, tone map (PBR) or identity, TAA or FSR, FXAA, written into the LDR scene target.
    void composite(){
        const auto&f=s.options.features;if(!composite_enabled(s.options.pbr,f))return;
        if(!f.bloom&&!f.fxaa&&!f.taa&&!f.sharpen&&!f.fsr){composite_hdr();return;}
        end();Image*source=&s.hdr.color;
        if(s.samples!=VK_SAMPLE_COUNT_1_BIT){s.transition(s.hdr.color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);s.transition(s.hdr.resolve,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);VkImageResolve resolve{};resolve.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};resolve.dstSubresource=resolve.srcSubresource;resolve.extent={s.render.width,s.render.height,1};vkCmdResolveImage(s.command,s.hdr.color.handle,s.hdr.color.layout,s.hdr.resolve.handle,s.hdr.resolve.layout,1,&resolve);source=&s.hdr.resolve;}
        carry_depth();
        s.transition(*source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        const auto linear=sampler(true,true),nearest=sampler(false,true);Uniform u;u.pbr={s.options.pbr?1.f:0.f,0,0,0};
        auto pass=[&](Image&out,int mode,std::array<Image*,6>in,std::array<VkSampler,6>sampling,int blend=0,bool load=false){
            begin_image(out,VK_NULL_HANDLE,0,0,load);set_region({{0,0},{out.width,out.height}});u.pass={float(mode),0,1.f/in[0]->width,1.f/in[0]->height};bind({EffectProgram,0,0,0,blend!=0,0,blend},u,in,sampling);vkCmdDraw(s.command,3,1,0,0);end();
            s.transition(out,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        };
        if(f.bloom){pass(fx.bloom[0],5,{source},{linear});for(unsigned i=1;i<fx.bloom.size();++i)pass(fx.bloom[i],6,{&fx.bloom[i-1]},{linear});for(int i=int(fx.bloom.size())-2;i>=0;--i)pass(fx.bloom[i],7,{&fx.bloom[i+1]},{linear},4,true);}
        u.range[3]=f.bloom?0.15f:0.f; // effects.glsl k22BloomStrength.
        const std::array<Image*,6>compositeInputs{source,f.bloom?&fx.bloom[0]:&white};const std::array<VkSampler,6>compositeSampling{nearest,linear};
        const bool chain=f.taa||f.fxaa||f.sharpen||f.fsr;Image*current=&fx.ldr;
        if(chain)pass(fx.ldr,8,compositeInputs,compositeSampling);else{begin(s.scene);set_region(full());u.pass={8,0,0,0};bind({EffectProgram},u,compositeInputs,compositeSampling);vkCmdDraw(s.command,3,1,0,0);}
        if(f.taa&&!f.fsr){ // FSR replaces TAA: its output was never read.
            auto&next=fx.taa[fx.taaIndex];auto&history=fx.taa[fx.taaIndex^1u];u.inverseViewProjection=fx.inverseVP;u.previousViewProjection=fx.previousVP;u.cameraPosition=vec(fx.eye,fx.farPlane);u.camera=vec(fx.forward,fx.nearPlane);u.range[3]=fx.taaValid&&fx.linearValid?1.f:0.f;
            pass(next,9,{&fx.ldr,&history,fx.linearValid?&fx.linear:&white,fx.motionValid?&fx.motion:&black},{nearest,linear,nearest,nearest});current=&next;fx.taaIndex^=1u;fx.taaValid=fx.linearValid;
        }
        // sharpen: contrast-adaptive sharpening (effects.frag mode 12) is the last LDR pass, after TAA/FXAA.
        // FSR replaces TAA (and FXAA/CAS: its RCAS sharpens); without a primary 3D view this frame the LDR view is stretched.
        if(f.fsr){if(fx.linearValid){upscale(u,*source);current=&fx.upscaled;}else fx.fsrValid=false;}
        else if(f.sharpen&&f.fxaa){pass(fx.sharp,10,{current},{linear});current=&fx.sharp;}
        if(chain){begin(s.scene);set_region(full());u.pass={f.fsr?11.f:f.sharpen?12.f:f.fxaa?10.f:11.f,0,1.f/current->width,1.f/current->height};bind({EffectProgram},u,{current},{linear});vkCmdDraw(s.command,3,1,0,0);}
    }
    void camera(const Camera&camera,bool targetOnly=false,const PortalSurface*targetPlane=nullptr){
        // Composited background views (PBR, bloom, TAA, FXAA, FSR) render to the HDR target; LDR targets tone map in shader.
        const bool hdrView=composite_enabled(s.options.pbr,s.options.features)&&!camera.foreground&&!targetOnly;
        struct ExtentGuard{VkExtent2D&e;VkExtent2D saved;~ExtentGuard(){e=saved;}}extentGuard{viewExtent,viewExtent};if(!targetOnly)viewExtent=hdrView?s.render:s.extent; // portal targets inherit their parent's size
        Rect viewport=camera.viewport;if(viewport.width<=0||viewport.height<=0)viewport={0,0,float(metrics.width),float(metrics.height)};auto r=rectangle(viewport);if(!r.extent.width||!r.extent.height||(!camera.perDrawOpacity&&camera.alpha<=0))return;
        const Camera*portalCamera=nullptr;std::optional<PortalSurface>surface;if(!targetOnly&&camera.portalId){for(auto&candidate:frame->cameras)if(candidate.sourceId==camera.portalId){portalCamera=&candidate;break;}if(!portalCamera)throw std::runtime_error("Attached portal view is absent from game frame");surface=Renderer::portal_surface(*frame,camera,metrics);if(surface){end();s.ensure_auxiliary(s.portal,viewExtent);this->camera(*portalCamera,true,&*surface);end();s.transition(s.portal.color,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);}}
        Target&main=hdrView?s.hdr:s.scene;auto seeded=[&](Color c){return hdrView&&s.options.pbr?inverse_tone_map(c):c;};
        bool translucent=!targetOnly&&!camera.perDrawOpacity&&camera.alpha<1;end();Target*destination=&main;if(targetOnly){s.ensure_auxiliary(s.portal,viewExtent);destination=&s.portal;}else if(translucent){s.ensure_auxiliary(s.alpha,viewExtent);destination=&s.alpha;}
        portalPass=targetOnly;maskPass=false;view={};view.viewProjection=camera_matrix(camera,float(r.extent.width)/r.extent.height);view.cameraPosition=vec(camera.position,camera.farClip);view.sunDirection=vec(frame->sunDirection);view.fogColor=vec(camera.environmentOverride?camera.fogColor:frame->fogColor);view.fogParameters={camera.environmentOverride?camera.fogStart:frame->fogStart,camera.environmentOverride?camera.fogEnd:frame->fogEnd,1,0};view.flags[0][0]=std::min<std::size_t>(frame->lights.size(),32);view.flags[3][3]=surface.has_value();if(surface){Rect pv=portalCamera->viewport;if(pv.width<=0||pv.height<=0)pv={0,0,float(metrics.width),float(metrics.height)};view.portalProjection=camera_matrix(*portalCamera,pv.width/pv.height);view.portalViewport={pv.x/metrics.width,pv.y/metrics.height,pv.width/metrics.width,pv.height/metrics.height};view.portalPlane=vec(surface->normal,-dot(surface->normal,surface->position));}
        view.pbr={s.options.pbr?1.f:0.f,s.options.pbr&&!hdrView?1.f:0.f,0,0};
        // Original D3D VIEW +Z maps to world forward even though native projection is GL-style RH.
        auto basis=transform({},camera.angles,{1,1,1});view.parameters[2][1]=basis[0];view.parameters[2][2]=basis[1];view.parameters[2][3]=basis[2];
        const bool primaryView=&camera==primary&&!targetOnly;
        fx.captureOpaque=primaryView&&s.fsr;opaqueTarget=destination;
        if(primaryView){fx.unjittered=view.viewProjection;
            // FSR: its Halton phase (render pixels, y down); clip y is up before scene.vert's Vulkan inversion.
            if(s.fsr){fx.fsrJitter=s.fsr->jitter(frameIndex);view.viewProjection=jittered(view.viewProjection,{fx.fsrJitter[0],-fx.fsrJitter[1]},float(r.extent.width),float(r.extent.height));const float aspect=float(r.extent.width)/float(r.extent.height);fx.fovY=2*std::atan(std::tan(rad(std::abs(camera.horizontalFov))*0.5f)/(camera.widescreen?camera.referenceAspect:aspect)*std::abs(camera.pixelAspect));}
            else if(s.options.features.taa)view.viewProjection=jittered(view.viewProjection,taa_jitter(frameIndex),float(r.extent.width),float(r.extent.height));fx.inverseVP=inverse(view.viewProjection);fx.eye=camera.position;fx.forward={basis[0],basis[1],basis[2]};fx.nearPlane=camera.nearClip;fx.farPlane=camera.farClip;}
        if(s.rayEnabled&&s.options.features.shadows==ShadowMode::RayTraced)view.effects[1]=8; // shadows=rt: inline visibility rays in every view.
        if(fx.shadowsValid){view.effects[0]=1;view.sunShadow=fx.sunMatrix;view.effects[2]=fx.sunTexel;view.effects[3]=float(fx.shadowIndex);view.shadowLight=vec(fx.shadowLight,fx.shadowIndex>=0?fx.shadowRange:0);}
        if(camera.portalClip){auto position=targetPlane?targetPlane->position:camera.portalPosition,normal=targetPlane?targetPlane->normal:camera.portalNormal;if(dot(normal,normal)==0)throw std::runtime_error("Portal clip view has no real surface plane");normal=unit(normal);view.clipPlane=vec(normal,-dot(normal,position));}
        // Seamless door (render.hpp DoorPortal): every pass of this view cuts the doorway (scene.frag mode 1); encounters
        // add the door world (mode 2). sC<=0 or a closed rect: neither.
        const auto&door=frame->door;const bool doorOpen=camera.doorPortal&&door.world&&door.halfWidth>0&&door.halfHeight>0&&dot(add(camera.position,mul(door.center,-1)),door.normal)>0;
        if(doorOpen)view.door={vec(door.center,1),vec(door.normal,door.halfWidth),vec(door.right,door.halfHeight),vec(door.up)};
        auto selectShadows=[&]{
            shadowOrder.clear();if(camera.drawWorld&&!camera.noShadow&&!camera.noEntities&&frame->renderEntities&&(!frame->bonusProfile||frame->planarCapable)&&!frame->shadows.empty()){if(frame->bonusProfile?frame->shadowStencilRaw>0:frame->shadowStencilRaw!=0)throw std::runtime_error("Nondefault source stencil-volume shadows are not implemented; native Vulkan supports the recovered planar route");for(auto&d:frame->shadows){if(!(d.layers&camera.layers)||(d.shadowViewId&&d.shadowViewId!=camera.sourceId)||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;auto cached=frame->cachedEntityShadow?frame->cachedEntityShadow(frame->lightContext,d):d.nativeShadow;if(frame->bonusProfile||cached.caster)shadowOrder.push_back(&d);}std::sort(shadowOrder.begin(),shadowOrder.end(),[&](auto*a,auto*b){auto da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));if(!frame->shadowSortVertical)da.z=db.z=0;return dot(da,da)>dot(db,db);});}
            if(actorShadows())shadowOrder.clear(); // shadows=rt: the actor shadows (rt_signals.comp actorShadow) replace the planar shadow.
        };
        auto encounters=[&](Encounter encounter){
            if(camera.drawWorld&&frame->world&&(camera.layers&1)){draw_instance(worldDraw,camera,true,-1,encounter);if(doorOpen){view.door[0][3]=2;draw_instance(doorDraw,camera,true,-1,encounter);view.door[0][3]=1;}}
            if(encounter==Encounter::Replay){
                // Only the prepared opaque slots replay; transparent and shadow queues retain their source order.
                for(auto&d:frame->instances)draw_instance(d,camera,true,-1,encounter);
            }else{
                transparent.clear();for(auto&d:frame->instances){if(camera.noEntities||!frame->renderEntities||!(d.layers&camera.layers)||d.viewSpace||(camera.excludeEntityId&&d.objectId==camera.excludeEntityId))continue;if(camera.perDrawOpacity&&d.model&&model_has_entity_skins(*d.model)){if(d.scale.x==0||d.scale.y==0||d.scale.z==0)continue;if(!d.visible){draw_instance(d,camera,true,-1,encounter);continue;}auto queues=model_queue(d);if(queues&1)draw_instance(d,camera,true,0,encounter);if(queues&2)transparent.push_back(&d);continue;}bool translucentDraw=d.alpha<1||d.additive||d.translucent||d.flare;if(d.model)for(auto&mat:d.model->materials)translucentDraw|=mat.alpha<1;if(translucentDraw)transparent.push_back(&d);else draw_instance(d,camera,true,-1,encounter);}
                std::sort(transparent.begin(),transparent.end(),[&](auto*a,auto*b){auto da=add(a->position,mul(camera.position,-1)),db=add(b->position,mul(camera.position,-1));return dot(da,da)>dot(db,db);});
            }
            if(fx.captureOpaque&&encounter!=Encounter::Prepare)snapshot_opaque(*opaqueTarget);
            for(auto*d:transparent)draw_instance(*d,camera,false,encounter==Encounter::Replay?-1:camera.perDrawOpacity&&d->model&&model_has_entity_skins(*d->model)?1:-1,encounter);
            for(auto*d:shadowOrder)draw_planar(*d,camera,encounter);
        };
        if(s.rayEnabled){
            // Child portal encounters have finished. Keep their actor results, but reset this camera's replay admission.
            for(auto&[d,p]:prepared)p.admitted=0;
            selectShadows(); // A6 cached-caster admission precedes every current-camera callback.
            encounters(Encounter::Prepare);
        }
        ray_scene(camera);s.mark("as_build");if(primaryView&&motionVectors()){motion_vectors(camera);s.mark("motion");}if(primaryView&&rtSignals())rt_signals(camera);begin(*destination);set_region(r);if(targetOnly&&(!camera.perDrawOpacity||!s.portalColorValid)){clear(camera.environmentOverride?camera.background:frame->clearColor,true,true,true,full());s.portalColorValid=true;}if(translucent)clear(seeded(camera.foreground?Color{0,0,0,0}:camera.environmentOverride?camera.background:frame->clearColor),true,true,true,full());if(camera.perDrawOpacity)clear(seeded(camera.environmentOverride?camera.background:frame->clearColor),camera.clearColor,camera.clearDepth,camera.clearStencil,r);else if(camera.clearDepth)clear({},false,true,false,r);
        if(!s.rayEnabled)selectShadows();
        draw_sky(camera);encounters(s.rayEnabled?Encounter::Replay:Encounter::Live);s.mark("scene");
        particles(camera);s.mark("particles");if(primaryView){depth_effects(*destination,r);s.mark("depth_fx");}bool hasView=false;if(frame->renderEntities&&!camera.noEntities)for(auto&d:frame->instances)if(!targetOnly&&d.viewSpace&&(d.layers&camera.layers)&&d.visible&&(!camera.excludeEntityId||d.objectId!=camera.excludeEntityId)){hasView=true;break;}if(hasView){clear({},false,true,false,r);view.fogParameters[0]=view.fogParameters[1]=0;view.door[0][3]=0;for(auto&d:frame->instances)if(d.viewSpace&&(d.layers&camera.layers)&&(!camera.excludeEntityId||d.objectId!=camera.excludeEntityId))draw_instance(d,camera);}
        if(translucent){end();s.transition(s.alpha.color,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);begin(main);set_region(full());Uniform u;u.viewport={float(metrics.width),float(metrics.height),0,0};uiVertices.clear();quad(viewport,{viewport.x/metrics.width,viewport.y/metrics.height,viewport.width/metrics.width,viewport.height/metrics.height},{camera.alpha,camera.alpha,camera.alpha,camera.alpha});flush_ui(s.alpha.color,u,false,true,true);}
        if(primaryView&&rtSignals()){const auto nearest=sampler(false,true);globals[2]=globals[3]=globals[4]={&white,nearest};globals[5]={&dummyDepth,nearest};}
    }
    // Resolve HDR background views, carry depth/stencil for foreground views, tone map into the LDR scene target.
    void composite_hdr(){
        if(!s.options.pbr)return;end();Image*source=&s.hdr.color;
        if(s.samples!=VK_SAMPLE_COUNT_1_BIT){s.transition(s.hdr.color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);s.transition(s.hdr.resolve,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);VkImageResolve resolve{};resolve.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};resolve.dstSubresource=resolve.srcSubresource;resolve.extent={s.render.width,s.render.height,1};vkCmdResolveImage(s.command,s.hdr.color.handle,s.hdr.color.layout,s.hdr.resolve.handle,s.hdr.resolve.layout,1,&resolve);source=&s.hdr.resolve;}
        carry_depth();
        s.transition(*source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        begin(s.scene);set_region(full());Uniform u;bind({TonemapProgram},u,{source},{sampler(false,true)});vkCmdDraw(s.command,3,1,0,0);
    }
    void draw(const RenderFrame&f,WindowMetrics m){
        struct BorrowGuard {Raster&r;~BorrowGuard(){r.frame=nullptr;r.prepared.clear();r.actorLights.clear();r.transparent.clear();r.shadowOrder.clear();r.particlePose=nullptr;r.worldDraw=r.doorDraw={};r.active=false;r.target=nullptr;}} guard{*this};
        if(m.drawableWidth<=0||m.drawableHeight<=0)return;
        if(f.world&&!f.hardwareLighting)throw std::runtime_error("Legacy software world-light topology is not implemented by the native Vulkan hardware path");
        if(!s.command)throw std::runtime_error("Vulkan raster requires an admitted platform frame");
        frame=&f;metrics=m;viewExtent=s.extent;centroid=centroid_lighting(f);reset_arena();prepared.clear();actorLights.clear();
        ++frameIndex;ensure_dummies();fx.shadowsValid=fx.linearValid=fx.motionValid=fx.opaqueValid=fx.captureOpaque=fx.upscaledNow=false;reset_globals();ensure_effects();
        // scene.vert reads the caster push constant statically (selected only by shadow passes): define it for every draw.
        {const Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};vkCmdPushConstants(s.command,pipelineLayout,casterStages,0,sizeof(Matrix),identity.data());}
        // The primary view (effects owner): first full-window background world view that is not alpha-blended.
        primary=nullptr;if(f.world)for(auto&c:f.cameras){Rect v=c.viewport;if(c.foreground||c.portalTarget||!c.drawWorld||(!c.perDrawOpacity&&c.alpha<1))continue;if(v.width<=0||v.height<=0||(v.x<=0&&v.y<=0&&v.x+v.width>=m.width&&v.y+v.height>=m.height)){primary=&c;break;}}
        selectedLight=material_selected_light(f);
        std::array<float,4>zero{};zeroAllocation=allocate(sizeof(zero),std::max<VkDeviceSize>(16,s.properties.limits.minStorageBufferOffsetAlignment),zero.data());
        std::array<F4,(32+actorShadowLightLimit)*5>lights{};std::size_t count=std::min<std::size_t>(32,f.lights.size());
        auto pack=[&](const RenderLight&l,std::size_t i){lights[i*5]=vec(l.position,l.range);lights[i*5+1]={l.color.r,l.color.g,l.color.b,l.baked?-l.intensity:l.intensity};lights[i*5+2]=vec(l.direction,float(l.type));lights[i*5+3]=vec(l.attenuation,l.falloff);lights[i*5+4]={l.type==2?std::cos(rad(l.theta)*.5f):0,l.type==2?std::cos(rad(l.phi)*.5f):0,l.synthetic?1.f:0.f,0};};
        for(std::size_t i=0;i<count;++i)pack(f.lights[i],i);
        actorShadowFirst=count;actorShadowCount=0;actorShadowCasters=false;
        if(actorShadows()){
            std::vector<Vec3>casters;for(auto&d:f.instances)if(d.shadowRequested&&d.visible&&!d.viewSpace)casters.push_back(d.position);
            actorShadowCasters=!casters.empty();
            if(primary)for(auto i:actor_shadow_lights(f.lights,casters))pack(f.lights[i],count+actorShadowCount++);
        }
        lightAllocation=count?allocate((count+actorShadowCount)*5*sizeof(F4),std::max<VkDeviceSize>(16,s.properties.limits.minStorageBufferOffsetAlignment),lights.data()):zeroAllocation;
        auto prepareWorld=[&](const World&world,DrawInstance&draw){draw.model=&world.mesh;prepare(draw);for(auto&mat:world.mesh.materials)if((mat.flags&15)==5&&mat.texture&&(mat.texture->width==mat.texture->height*6||mat.texture->width==mat.texture->height*2))sky_texture(*mat.texture);};
        worldDraw=doorDraw={};if(f.world)prepareWorld(*f.world,worldDraw);
        // The door world is prepared whenever present (also while its view is closed) so its first visible frame does not upload.
        if(f.door.world){doorDraw.position=f.door.position;doorDraw.angles=f.door.angles;prepareWorld(*f.door.world,doorDraw);}
        for(auto&d:f.instances)prepare(d);
        if(!actorShadows())for(auto&d:f.shadows)prepare(d,true); // shadows=rt never draws the planar shadow.
        if(!f.particles.empty()){DrawInstance quad;quad.model=&particle_quad();auto&meshGPU=mesh(*quad.model);particlePose=&rt_pose(*s.rays,quad,meshGPU.expanded,meshGPU.tangents,false);for(auto&p:f.particles)if(p.image)texture(p.image);}
        for(auto&c:f.ui){if(c.kind==UICommand::Kind::Image&&c.image)texture(c.image,c.dynamicImage,c.imageSerial,c.textureMode);if(c.kind==UICommand::Kind::Text&&c.font)texture(&c.font->image);}
        if(f.video){check_image(*f.video);if(!video.handle||video.width!=f.video->width||video.height!=f.video->height||videoSerial!=f.videoSerial){end();if(video.handle)s.retire(video);video=s.image(f.video->width,f.video->height,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={f.video->width,f.video->height,1};s.upload(video,std::as_bytes(std::span(f.video->rgba)),std::span(&copy,1));videoSerial=f.videoSerial;}}
        if(f.adjustment.enabled&&(!gammaUploaded||adjustment.gammaRamp!=f.adjustment.gammaRamp)){end();s.retire(gamma);gamma=s.image(256,3,VK_FORMAT_R16_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={256,3,1};s.upload(gamma,std::as_bytes(std::span(f.adjustment.gammaRamp)),std::span(&copy,1));adjustment=f.adjustment;gammaUploaded=true;}adjustment.enabled=f.adjustment.enabled;
        s.mark("prepare");if(primary&&shadow_maps_enabled(s.options.features)){shadow_maps(*primary);s.mark("shadow_maps");}reset_globals();
        const bool composited=composite_enabled(s.options.pbr,s.options.features);
        begin(composited?s.hdr:s.scene);set_region(full());auto seed=[&](Color c){return s.options.pbr?inverse_tone_map(c):c;};bool native=std::any_of(f.cameras.begin(),f.cameras.end(),[](auto&c){return c.perDrawOpacity;});if(!native||!s.retainedColorValid){clear(seed(f.clearColor),true,true,true,full());s.retainedColorValid=true;}if(native&&f.clearScreen){const Camera*first=nullptr;for(auto&c:f.cameras)if(!c.foreground&&!c.portalTarget){first=&c;break;}if(!first)for(auto&c:f.cameras)if(c.foreground&&!c.portalTarget){first=&c;break;}Rect v=first?first->viewport:Rect{};bool fullScreen=first&&(v.width<=0||v.height<=0||(v.x<=0&&v.y<=0&&v.x+v.width>=m.width&&v.y+v.height>=m.height));if(!fullScreen)clear(seed(f.screenColor),true,false,false,full());}
        for(auto&c:f.cameras)if(!c.foreground&&!c.portalTarget)camera(c);s.mark("views");composite();s.mark("composite");
        // Foreground views and UI interleaved by layer, video and cursor over the composited scene, then resolve and post into out.
        auto overlay=[&](Image&out){
        std::size_t ci=0,ui=0,cursor=f.ui.size();while(ci<f.cameras.size()||ui<f.ui.size()){while(ci<f.cameras.size()&&(!f.cameras[ci].foreground||f.cameras[ci].portalTarget))++ci;if(ci<f.cameras.size()&&(ui==f.ui.size()||f.cameras[ci].layer<=f.ui[ui].layer))camera(f.cameras[ci++]);else if(ui<f.ui.size()){auto&c=f.ui[ui++];if(c.cursorOverlay){if(cursor==f.ui.size())cursor=ui-1;}else draw_ui(c);}}
        if(f.video){begin(s.scene);set_region(full());Rect r=f.videoRect;if(r.width<=0||r.height<=0){float scale=std::min(float(m.width)/f.video->width,float(m.height)/f.video->height);r={(m.width-f.video->width*scale)/2,(m.height-f.video->height*scale)/2,f.video->width*scale,f.video->height*scale};}Uniform u;u.viewport={float(m.width),float(m.height),0,0};uiVertices.clear();quad(r,{0,0,1,1},{1,1,1,1});flush_ui(video,u,false,true);}for(;cursor<f.ui.size();++cursor)if(f.ui[cursor].cursorOverlay)draw_ui(f.ui[cursor]);end();s.mark("ui");
        Image*resolved=&s.scene.color;
        if(s.samples!=VK_SAMPLE_COUNT_1_BIT){s.transition(s.scene.color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);s.transition(s.scene.resolve,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);VkImageResolve resolve{};resolve.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};resolve.dstSubresource=resolve.srcSubresource;resolve.extent={s.extent.width,s.extent.height,1};vkCmdResolveImage(s.command,s.scene.color.handle,s.scene.color.layout,s.scene.resolve.handle,s.scene.resolve.layout,1,&resolve);resolved=&s.scene.resolve;}
        s.transition(*resolved,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        begin_image(out);set_region(full());Uniform post;post.parameters[1][3]=adjustment.enabled;bind({Post},post,{resolved,&gamma},{sampler(true,true),sampler(false,true)});vkCmdDraw(s.command,3,1,0,0);end();s.mark("post");
        };
        // FSR 3.1.4 frame generation (docs/GRAPHICS_ROADMAP.md): the HUD-less upscaled view is interpolated and this frame's
        // foreground views and UI are drawn over it exactly as over the rendered frame (the HUD is never interpolated) into
        // s.generated; the scene target then gets the rendered view back. After an FSR history reset only the rendered frame
        // is presented (the interpolation still runs to keep its previous-frame copy).
        s.generatedValid=false;
        if(s.generating&&fx.upscaledNow){interpolate();if(!fx.fsrReset){scene_from(fx.interpolated);overlay(s.generated[s.slotIndex]);scene_from(fx.upscaled);s.generatedValid=true;}}
        overlay(s.finalImage);
        transparent.clear();shadowOrder.clear();particlePose=nullptr;if(primary)fx.previousVP=fx.unjittered;
    }
};
void RasterDelete::operator()(Raster*r)const{raster_destroy(r);}
std::unique_ptr<Raster,RasterDelete> raster_create(VulkanBackend::State&s){return std::unique_ptr<Raster,RasterDelete>(new Raster(s));}
void raster_destroy(Raster*r){delete r;}
void raster_draw(Raster&r,const RenderFrame&f,WindowMetrics m){r.draw(f,m);}
void raster_release(Raster&r,const Model*keep){r.release(keep);}
void raster_purge_model(Raster&r,const Model*model){auto it=r.meshes.find(model);if(it!=r.meshes.end()){r.s.destroy(it->second.indices);r.meshes.erase(it);}for(auto it=r.overrideBounds.begin();it!=r.overrideBounds.end();)if(it->second.model==model)it=r.overrideBounds.erase(it);else ++it;}
void raster_purge_image(Raster&r,const k22::Image*image){for(auto&[id,effect]:r.effects)for(auto&bank:effect.banks)if(bank.identity==image)bank={};auto found=r.textures.find(image);if(found!=r.textures.end()){for(auto&t:found->second){r.s.destroy(t.image);r.s.destroy(t.normal);r.s.destroy(t.orm);}r.textures.erase(found);}auto warp=r.warps.find(image);if(warp!=r.warps.end()){r.s.destroy(warp->second.image);r.warps.erase(warp);}auto sky=r.skies.find(image);if(sky!=r.skies.end()){r.s.destroy(sky->second.image);r.skies.erase(sky);}r.turbulent.erase(image);}
int raster_shader_version(const Raster&r){return r.shaders[0]&&r.shaders[1]?2020:0;}
}
