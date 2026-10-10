#include "render_vulkan.hpp"
#include "render_policy.hpp"
#include "render_vulkan_spirv.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace k22::vk22 {
namespace {
constexpr VkBufferUsageFlags endpointUsage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
constexpr std::uint32_t textureLimit=1024;
struct PackedVertex { Vertex vertex; Vec3 tangent; };
static_assert(sizeof(PackedVertex)==sizeof(Vertex)+sizeof(Vec3));
static_assert(sizeof(PackedVertex)==13*sizeof(float));
struct Endpoint { Vec3 position,normal,tangent; };
struct Acceleration { VkAccelerationStructureKHR handle{}; Buffer storage; VkDeviceAddress address{}; };
struct PoseSignature {
    std::uint32_t frame{},next{},mesh{},nextMesh{},blend{}; std::uint64_t revision{},nextRevision{};
    bool tangents{};
    std::array<std::size_t,4> counts{};
    bool operator==(const PoseSignature&)const=default;
};
struct PoseRecord {
    Pose pose; const Model* model{}; std::uint32_t actor{}; bool expanded{};
    std::uint64_t epoch{},version{}; PoseSignature signature;
};
struct PoseKey { const Model* model; std::uint32_t actor; bool expanded; bool operator==(const PoseKey&)const=default; };
struct PoseKeyHash { std::size_t operator()(const PoseKey& key)const{return std::hash<const Model*>{}(key.model)^(static_cast<std::size_t>(key.actor)<<1)^key.expanded;} };
struct ModelData { Buffer base,endpoints; std::uint32_t count{},frameCount{}; };
struct OverrideData {
    Buffer endpoint; const Model* model{}; std::uint64_t revision{};
    std::uint32_t frame{}; std::size_t positions{},normals{}; bool tangents{};
};
struct Blas {
    Acceleration acceleration; PoseRecord* pose{}; VkBuffer indices{};
    std::vector<std::array<std::uint32_t,2>> ranges; std::uint64_t poseVersion{};
};
struct DescriptorPools { std::vector<VkDescriptorPool> pools; std::size_t used{}; };
struct SceneResources { Buffer instances,records,materials; Acceleration tlas; std::uint32_t count{}; std::vector<VkDescriptorImageInfo> textures; };
struct SlotData {
    std::uint64_t epoch{}; DescriptorPools poseDescriptors,rayDescriptors;
    std::vector<std::unique_ptr<PoseRecord>> poses;
    std::unordered_map<PoseKey,std::vector<PoseRecord*>,PoseKeyHash> poseGroups;
    std::unordered_map<Pose*,PoseRecord*> poseLookup;
    std::unordered_map<PoseRecord*,std::vector<std::size_t>> blasLookup;
    std::vector<Blas> blas;
    std::vector<Acceleration> garbage;
    std::vector<SceneResources> scenes; std::size_t sceneUsed{}; Buffer scratch;
    VkDescriptorSet set{};
};
struct alignas(16) GpuMaterial {
    std::array<float,4> uv,tint,factors;
    std::array<std::uint32_t,4> metadata,frames;
    std::array<float,4> normalUv,ambient,skillMain,skillExtra,sun,camera;
    std::array<float,4> preparedAmbient,preparedDiffuse,preparedSpecular,preparedEmissive,ambientLight,viewer;
    std::array<std::int32_t,4> lightState;
};
static_assert(sizeof(GpuMaterial)==288);
struct alignas(16) GpuInstance { std::uint64_t vertices,indices; std::uint32_t materialBase,padding{}; std::array<std::uint32_t,2> alignmentPadding{}; std::array<float,4> clipPlane{}; };
static_assert(sizeof(GpuInstance)==48);
std::array<float,4> color(Color c){return {c.r,c.g,c.b,c.a};}
void barrier(VkCommandBuffer command,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkAccessFlags from,VkAccessFlags to){
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=from;b.dstAccessMask=to;
    vkCmdPipelineBarrier(command,source,destination,0,1,&b,0,nullptr,0,nullptr);
}
}
struct RayTracing {
    VulkanBackend::State& s;
    VkDescriptorSetLayout poseLayout{},layout{};VkPipelineLayout computeLayout{};VkPipeline compute{};
    Image white;VkSampler whiteSampler{};
    std::unordered_map<const Model*,ModelData> models,expandedModels;
    std::unordered_map<std::uint32_t,OverrideData> overrides;
    std::array<SlotData,2> slots;
    std::vector<VkAccelerationStructureInstanceKHR> instanceData;
    std::vector<GpuInstance> recordData; std::vector<GpuMaterial> materialData;
    std::vector<VkDescriptorImageInfo> textureData;
    std::vector<std::array<std::uint32_t,2>> rangeData;
    std::vector<VkAccelerationStructureGeometryKHR> geometryData;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeData;
    std::vector<std::uint32_t> countData;
    VkDeviceSize scratchAlignment{1};
    PFN_vkCreateAccelerationStructureKHR createAs{};PFN_vkDestroyAccelerationStructureKHR destroyAs{};
    PFN_vkGetAccelerationStructureBuildSizesKHR buildSizes{};PFN_vkGetAccelerationStructureDeviceAddressKHR asAddress{};
    PFN_vkCmdBuildAccelerationStructuresKHR buildAs{};
    explicit RayTracing(VulkanBackend::State& state):s(state){}
    ~RayTracing();
    void destroy(Acceleration& a){if(a.handle)destroyAs(s.device,a.handle,nullptr);s.destroy(a.storage);a={};}
    SlotData& slot();
    VkDescriptorSet descriptors(VkDescriptorSetLayout,bool ray);
    void scene_descriptors(SceneResources&);
    Acceleration acceleration(VkAccelerationStructureTypeKHR,VkDeviceSize);
    Buffer scratch(VkDeviceSize);
    Buffer& upload(Buffer&,VkDeviceSize,VkBufferUsageFlags,const void*);
    ModelData& model(const Model&,std::span<const Vertex>,std::span<const Vec3>);
    std::pair<Buffer,std::uint32_t> endpoint(const DrawInstance&,ModelData&,bool next,bool tangents);
};
RayTracing::~RayTracing(){
    for(auto& slot:slots){for(auto& scene:slot.scenes){destroy(scene.tlas);s.destroy(scene.instances);s.destroy(scene.records);s.destroy(scene.materials);}s.destroy(slot.scratch);for(auto& a:slot.garbage)destroy(a);for(auto& b:slot.blas)destroy(b.acceleration);for(auto& p:slot.poses)s.destroy(p->pose.vertices);for(auto* pools:{&slot.poseDescriptors,&slot.rayDescriptors})for(auto p:pools->pools)vkDestroyDescriptorPool(s.device,p,nullptr);}
    for(auto* map:{&models,&expandedModels})for(auto& [key,m]:*map){s.destroy(m.base);s.destroy(m.endpoints);}
    for(auto& [key,o]:overrides)s.destroy(o.endpoint);
    s.destroy(white);if(whiteSampler)vkDestroySampler(s.device,whiteSampler,nullptr);
    if(compute)vkDestroyPipeline(s.device,compute,nullptr);if(computeLayout)vkDestroyPipelineLayout(s.device,computeLayout,nullptr);
    if(poseLayout)vkDestroyDescriptorSetLayout(s.device,poseLayout,nullptr);if(layout)vkDestroyDescriptorSetLayout(s.device,layout,nullptr);
}
SlotData& RayTracing::slot(){
    auto& slot=slots[s.slotIndex];auto epoch=s.submissions+1;
    if(slot.epoch!=epoch){
        slot.sceneUsed=0;for(auto& a:slot.garbage)destroy(a);slot.garbage.clear();
        for(auto* pools:{&slot.poseDescriptors,&slot.rayDescriptors}){for(auto pool:pools->pools)require(vkResetDescriptorPool(s.device,pool,0),"reset Vulkan pose/ray descriptors");pools->used=0;}
        slot.set={};slot.epoch=epoch;
    }
    return slot;
}
VkDescriptorSet RayTracing::descriptors(VkDescriptorSetLayout setLayout,bool ray){
    auto& frame=slot();auto& pools=ray?frame.rayDescriptors:frame.poseDescriptors;std::size_t capacity=ray?1:64;
    if(pools.used/capacity==pools.pools.size()){
        std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,ray?2u:256u},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,textureLimit},{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1}}};
        VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};ci.maxSets=capacity;ci.poolSizeCount=ray?3u:1u;ci.pPoolSizes=sizes.data();
        VkDescriptorPool pool{};require(vkCreateDescriptorPool(s.device,&ci,nullptr,&pool),"create Vulkan pose/ray descriptor pool");pools.pools.push_back(pool);
    }
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pools.pools[pools.used++/capacity];ai.descriptorSetCount=1;ai.pSetLayouts=&setLayout;
    VkDescriptorSet set{};require(vkAllocateDescriptorSets(s.device,&ai,&set),ray?"allocate ray descriptors":"allocate deformation descriptors");return set;
}
void RayTracing::scene_descriptors(SceneResources& scene){
    auto set=descriptors(layout,true);
    VkWriteDescriptorSetAccelerationStructureKHR accelerationWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    accelerationWrite.accelerationStructureCount=1;accelerationWrite.pAccelerationStructures=&scene.tlas.handle;
    std::array<VkDescriptorBufferInfo,2> buffers{{{scene.records.handle,0,VK_WHOLE_SIZE},{scene.materials.handle,0,VK_WHOLE_SIZE}}};
    std::array<VkWriteDescriptorSet,4> writes{};
    for(unsigned i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;}
    writes[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;writes[0].pNext=&accelerationWrite;
    for(unsigned i=1;i<3;++i){writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i-1];}
    writes[3].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[3].descriptorCount=textureLimit;writes[3].pImageInfo=scene.textures.data();
    vkUpdateDescriptorSets(s.device,writes.size(),writes.data(),0,nullptr);slot().set=set;
}
Acceleration RayTracing::acceleration(VkAccelerationStructureTypeKHR type,VkDeviceSize size){
    Acceleration result;result.storage=s.buffer(std::max<VkDeviceSize>(size,256),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};ci.buffer=result.storage.handle;ci.size=result.storage.size;ci.type=type;
    auto status=createAs(s.device,&ci,nullptr,&result.handle);if(status!=VK_SUCCESS){s.destroy(result.storage);require(status,"create KHR acceleration structure");}
    VkAccelerationStructureDeviceAddressInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};info.accelerationStructure=result.handle;result.address=asAddress(s.device,&info);return result;
}
Buffer RayTracing::scratch(VkDeviceSize size){auto& buffer=slot().scratch;auto required=std::max<VkDeviceSize>(size,1)+scratchAlignment-1;if(buffer.size<required){s.retire(buffer);buffer=s.buffer(required,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);}return buffer;}
Buffer& RayTracing::upload(Buffer& buffer,VkDeviceSize size,VkBufferUsageFlags usage,const void* data){if(buffer.size<size){s.retire(buffer);buffer=s.buffer(size,usage,true);}if(data)std::memcpy(buffer.mapped,data,size);return buffer;}
ModelData& RayTracing::model(const Model& source,std::span<const Vertex> vertices,std::span<const Vec3> tangents){
    auto& map=vertices.empty()?models:expandedModels;auto [it,inserted]=map.try_emplace(&source);auto& result=it->second;if(!inserted)return result;
    if(vertices.empty())vertices=source.vertices;
    if(vertices.size()>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Vulkan display vertex count exceeds 32-bit topology");
    auto endpointLimit=std::min<std::uint64_t>(s.properties.limits.maxStorageBufferRange/sizeof(Endpoint),std::numeric_limits<std::uint32_t>::max()/9u);
    if(source.frames.size()>=endpointLimit||(vertices.size()&&source.frames.size()+1>endpointLimit/vertices.size())||vertices.size()>s.properties.limits.maxStorageBufferRange/sizeof(PackedVertex)||vertices.size()>static_cast<std::uint64_t>(s.properties.limits.maxComputeWorkGroupCount[0])*64)throw std::runtime_error("Vulkan immutable display pose exceeds device storage/compute endpoint limits");
    result.count=vertices.size();result.frameCount=source.frames.size();
    std::vector<Vec3> derived;if(tangents.empty()){derived.resize(source.vertices.size());render_policy::derive_tangents(source,{},{},derived);derived.resize(vertices.size());tangents=derived;}
    if(tangents.size()!=vertices.size())throw std::runtime_error("Expanded Vulkan display tangents do not match vertices");
    std::vector<PackedVertex> base(vertices.size());std::vector<Endpoint> endpoints(vertices.size()*(source.frames.size()+1));
    for(std::size_t i=0;i<vertices.size();++i){base[i]={vertices[i],tangents[i]};endpoints[i]={vertices[i].position,vertices[i].normal,tangents[i]};}
    for(std::size_t f=0;f<source.frames.size();++f){
        const auto& frame=source.frames[f];if(frame.positions.size()!=source.vertices.size()||(!frame.normals.empty()&&frame.normals.size()!=source.vertices.size()))throw std::runtime_error("Animation frame must supply complete Vulkan decoded vertex pose");
        derived.resize(source.vertices.size());render_policy::derive_tangents(source,frame.positions,frame.normals,derived);
        for(std::size_t i=0;i<vertices.size();++i)endpoints[(f+1)*vertices.size()+i]=i<source.vertices.size()?Endpoint{frame.positions[i],frame.normals.empty()?source.vertices[i].normal:frame.normals[i],derived[i]}:endpoints[i];
    }
    result.base=s.buffer(std::max<std::size_t>(base.size()*sizeof(PackedVertex),4),endpointUsage,true,base.empty()?nullptr:base.data());
    result.endpoints=s.buffer(std::max<std::size_t>(endpoints.size()*sizeof(Endpoint),4),endpointUsage,true,endpoints.empty()?nullptr:endpoints.data());return result;
}
std::pair<Buffer,std::uint32_t> RayTracing::endpoint(const DrawInstance& d,ModelData& m,bool next,bool tangents){
    auto frame=next?d.nextFrame:d.frame;auto positions=next?d.nextPositions:d.positions,normals=next?d.nextNormals:d.normals;
    if(m.frameCount&&frame>=m.frameCount)throw std::runtime_error("Vulkan animation frame index outside decoded endpoint array");
    std::uint32_t offset=m.frameCount?(frame+1)*m.count*9:0;
    if(positions.empty()&&normals.empty())return {m.endpoints,offset};
    auto id=next?d.nextMeshId:d.meshId;auto revision=next?d.nextMeshRevision:d.meshRevision;
    if(!id||(!positions.empty()&&positions.size()!=d.model->vertices.size())||(!normals.empty()&&normals.size()!=d.model->vertices.size()))throw std::runtime_error("Invalid mutable Vulkan entity frame override");
    auto& cache=overrides[id];bool changed=cache.model!=d.model||cache.revision!=revision||cache.frame!=frame||cache.positions!=positions.size()||cache.normals!=normals.size();
    if(changed||(tangents&&!cache.tangents)){
        if(positions.empty()&&m.frameCount)positions=d.model->frames[frame].positions;
        if(normals.empty()&&m.frameCount)normals=d.model->frames[frame].normals;
        std::vector<Vec3> derived;if(tangents){derived.resize(d.model->vertices.size());render_policy::derive_tangents(*d.model,positions,normals,derived);}
        auto* immutable=static_cast<const Endpoint*>(m.endpoints.mapped)+offset/9;
        std::vector<Endpoint> data(m.count);for(std::size_t i=0;i<data.size();++i){data[i]=immutable[i];if(i<d.model->vertices.size()){if(!positions.empty())data[i].position=positions[i];if(!normals.empty())data[i].normal=normals[i];if(tangents)data[i].tangent=derived[i];}}
        s.retire(cache.endpoint);cache.endpoint=s.buffer(data.size()*sizeof(Endpoint),endpointUsage,true,data.data());cache.model=d.model;cache.revision=revision;cache.frame=frame;cache.positions=(next?d.nextPositions:d.positions).size();cache.normals=(next?d.nextNormals:d.normals).size();cache.tangents=tangents;
    }
    return {cache.endpoint,0};
}
std::unique_ptr<RayTracing,RayDelete> rt_create(VulkanBackend::State& s){
    auto rt=std::unique_ptr<RayTracing,RayDelete>(new RayTracing(s));
    std::array<VkDescriptorSetLayoutBinding,4> bindings{};for(unsigned i=0;i<4;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=bindings.size();li.pBindings=bindings.data();require(vkCreateDescriptorSetLayout(s.device,&li,nullptr,&rt->poseLayout),"create GPU pose descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,20};VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pi.setLayoutCount=1;pi.pSetLayouts=&rt->poseLayout;pi.pushConstantRangeCount=1;pi.pPushConstantRanges=&push;require(vkCreatePipelineLayout(s.device,&pi,nullptr,&rt->computeLayout),"create GPU deformation pipeline layout");
    auto module=s.shader(spirv::deform_comp);VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=rt->computeLayout;ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="main";auto status=vkCreateComputePipelines(s.device,VK_NULL_HANDLE,1,&ci,nullptr,&rt->compute);vkDestroyShaderModule(s.device,module,nullptr);require(status,"create GPU deformation pipeline");
    if(!s.rayEnabled)return rt;
#define K22_LOAD_AS(field,name) rt->field=reinterpret_cast<decltype(rt->field)>(vkGetDeviceProcAddr(s.device,name));if(!rt->field)throw std::runtime_error("Vulkan RT missing device entrypoint " name)
    K22_LOAD_AS(createAs,"vkCreateAccelerationStructureKHR");K22_LOAD_AS(destroyAs,"vkDestroyAccelerationStructureKHR");K22_LOAD_AS(buildSizes,"vkGetAccelerationStructureBuildSizesKHR");K22_LOAD_AS(asAddress,"vkGetAccelerationStructureDeviceAddressKHR");K22_LOAD_AS(buildAs,"vkCmdBuildAccelerationStructuresKHR");
#undef K22_LOAD_AS
    VkPhysicalDeviceAccelerationStructurePropertiesKHR ap{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};VkPhysicalDeviceProperties2 pp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};pp.pNext=&ap;vkGetPhysicalDeviceProperties2(s.physical,&pp);rt->scratchAlignment=std::max<VkDeviceSize>(ap.minAccelerationStructureScratchOffsetAlignment,1);
    if(s.properties.limits.maxPerStageDescriptorSamplers<textureLimit||s.properties.limits.maxDescriptorSetSamplers<textureLimit)throw std::runtime_error("Requested Vulkan RT requires 1024 material sampler descriptors; select --ray-tracing off for this device");
    bindings={{{0,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,textureLimit,VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};li.pBindings=bindings.data();require(vkCreateDescriptorSetLayout(s.device,&li,nullptr,&rt->layout),"create hardware ray query descriptor layout");
    rt->white=s.image(1,1,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);std::array<std::byte,4> white{std::byte{255},std::byte{255},std::byte{255},std::byte{255}};VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};s.upload(rt->white,white,std::span(&copy,1));rt->whiteSampler=s.sampler(true,false);return rt;
}
void rt_destroy(RayTracing* rt){delete rt;}
void RayDelete::operator()(RayTracing* rt)const{rt_destroy(rt);}
Pose& rt_pose(RayTracing& rt,const DrawInstance& d,std::span<const Vertex> expandedVertices,std::span<const Vec3> expandedTangents,bool needsTangents){
    if(!d.model)throw std::runtime_error("Vulkan pose requires a display model");if(!rt.s.command)throw std::runtime_error("Vulkan pose deformation requires an admitted frame");
    auto& slot=rt.slot();auto& m=rt.model(*d.model,expandedVertices,expandedTangents);
    PoseSignature signature{d.frame,d.nextFrame,d.meshId,d.nextMeshId,std::bit_cast<std::uint32_t>(std::clamp(d.interpolation,0.f,1.f)),d.meshRevision,d.nextMeshRevision,needsTangents,{d.positions.size(),d.normals.size(),d.nextPositions.size(),d.nextNormals.size()}};
    bool rigid=m.frameCount==0&&d.positions.empty()&&d.normals.empty()&&d.nextPositions.empty()&&d.nextNormals.empty();
    if(rigid){signature.frame=signature.next=signature.mesh=signature.nextMesh=signature.blend=0;signature.revision=signature.nextRevision=0;signature.tangents=false;}
    PoseKey key{d.model,rigid?0u:d.objectId,!expandedVertices.empty()};auto& group=slot.poseGroups[key];
    PoseRecord* record{};for(auto* p:group){if(p->epoch==slot.epoch&&p->signature==signature)return p->pose;if(p->epoch!=slot.epoch&&!record)record=p;}
    if(!record){auto p=std::make_unique<PoseRecord>();p->model=d.model;p->actor=key.actor;p->expanded=key.expanded;p->pose.count=m.count;VkBufferUsageFlags usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;if(rt.s.rayEnabled)usage|=VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;p->pose.vertices=rt.s.buffer(std::max<VkDeviceSize>(m.count*sizeof(PackedVertex),4),usage);record=p.get();slot.poses.push_back(std::move(p));group.push_back(record);slot.poseLookup.emplace(&record->pose,record);}
    bool changed=record->version==0||!(record->signature==signature);record->epoch=slot.epoch;if(!changed)return record->pose;
    auto a=rt.endpoint(d,m,false,needsTangents),b=rt.endpoint(d,m,true,needsTangents);
    // Original no-animation endpoint fallback aliases the first override, not immutable base.
    if(!m.frameCount&&d.nextPositions.empty()&&d.nextNormals.empty())b=a;
    VkDescriptorSet set=rt.descriptors(rt.poseLayout,false);std::array<VkDescriptorBufferInfo,4> infos{{{m.base.handle,0,VK_WHOLE_SIZE},{a.first.handle,0,VK_WHOLE_SIZE},{b.first.handle,0,VK_WHOLE_SIZE},{record->pose.vertices.handle,0,VK_WHOLE_SIZE}}};std::array<VkWriteDescriptorSet,4> writes{};for(unsigned i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&infos[i];}vkUpdateDescriptorSets(rt.s.device,writes.size(),writes.data(),0,nullptr);
    // VERTEX_SHADER: the motion-vector pass of the next frame reads this slot's poses as storage (rt_previous_pose).
    barrier(rt.s.command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(rt.s.command,VK_PIPELINE_BIND_POINT_COMPUTE,rt.compute);vkCmdBindDescriptorSets(rt.s.command,VK_PIPELINE_BIND_POINT_COMPUTE,rt.computeLayout,0,1,&set,0,nullptr);struct Push{std::uint32_t count,first,next;float blend;std::uint32_t nextFallback;}push{m.count,a.second,b.second,std::clamp(d.interpolation,0.f,1.f),!m.frameCount?((d.nextPositions.empty()?1u:0u)|(d.nextNormals.empty()?2u:0u)):0u};vkCmdPushConstants(rt.s.command,rt.computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);if(m.count)vkCmdDispatch(rt.s.command,(m.count+63)/64,1,1);
    auto destination=VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;if(rt.s.rayEnabled)destination|=VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;auto access=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_SHADER_READ_BIT;if(rt.s.rayEnabled)access|=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;barrier(rt.s.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,destination,VK_ACCESS_SHADER_WRITE_BIT,access);
    record->signature=signature;++record->version;return record->pose;
}
// The same actor's pose from the immediately preceding frame (other slot), when it differs from `current`; shared rigid
// or ambiguous (several poses per actor) records have none.
Pose* rt_previous_pose(RayTracing& rt,Pose& current){
    auto& slot=rt.slot();auto found=slot.poseLookup.find(&current);if(found==slot.poseLookup.end())return nullptr;const auto* record=found->second;
    auto& previous=rt.slots[rt.s.slotIndex^1u];if(!record->actor||previous.epoch!=rt.s.submissions)return nullptr;
    auto group=previous.poseGroups.find({record->model,record->actor,record->expanded});if(group==previous.poseGroups.end())return nullptr;
    PoseRecord* match{};for(auto* p:group->second)if(p->epoch==previous.epoch){if(match)return nullptr;match=p;}
    return match&&!(match->signature==record->signature)?&match->pose:nullptr;
}
void rt_scene(RayTracing& rt,std::span<const RayGeometry> scene){
    if(!rt.s.rayEnabled)return;auto& s=rt.s;auto& slot=rt.slot();if(!s.command)throw std::runtime_error("Ray scene requires admitted Vulkan frame");
    auto& instances=rt.instanceData;auto& records=rt.recordData;auto& materials=rt.materialData;auto& textures=rt.textureData;
    instances.clear();records.clear();materials.clear();textures.clear();textures.push_back({rt.whiteSampler,rt.white.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    auto texture=[&](VkImageView view,VkSampler sampler){if(!view)return 0u;if(!sampler)throw std::runtime_error("Ray material texture has no sampler");for(std::uint32_t i=0;i<textures.size();++i)if(textures[i].imageView==view&&textures[i].sampler==sampler)return i;if(textures.size()==textureLimit)throw std::runtime_error("Ray scene exceeds 1024 distinct material sampler bindings");textures.push_back({sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});return static_cast<std::uint32_t>(textures.size()-1);};
    std::uint64_t triangleCount=0;
    for(const auto& geometry:scene){
        if(!geometry.draw||!geometry.pose||!geometry.indices)throw std::runtime_error("Ray display geometry is incomplete");const auto& d=*geometry.draw;if(d.viewSpace||d.billboard||!d.visible)continue;
        if(!geometry.pose->count||!geometry.indices->size)continue;
        auto poseFound=slot.poseLookup.find(geometry.pose);if(poseFound==slot.poseLookup.end())throw std::runtime_error("Ray geometry does not use shared GPU deformed pose");auto* pose=poseFound->second;
        auto& ranges=rt.rangeData;ranges.clear();for(const auto& material:geometry.materials)if(material.count){if(material.count%3||static_cast<VkDeviceSize>(material.first+static_cast<std::uint64_t>(material.count))*4>geometry.indices->size)throw std::runtime_error("Ray material references invalid display triangle range");ranges.push_back({material.first,material.count});triangleCount+=material.count/3;}
        if(ranges.empty())continue;if(!geometry.pose->vertices.address||!geometry.indices->address)throw std::runtime_error("Ray display buffers lack device addresses");
        if(triangleCount>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Ray scene exceeds 32-bit reflection traversal bound");
        auto& candidateBlas=slot.blasLookup[pose];Blas* blas{};for(auto index:candidateBlas)if(slot.blas[index].indices==geometry.indices->handle){blas=&slot.blas[index];break;}if(!blas){candidateBlas.push_back(slot.blas.size());slot.blas.emplace_back();blas=&slot.blas.back();blas->pose=pose;blas->indices=geometry.indices->handle;}
        bool topologyChanged=blas->ranges!=ranges;bool changed=topologyChanged||!blas->acceleration.handle||blas->poseVersion!=pose->version;
        if(changed){
            auto& geometries=rt.geometryData;auto& buildRanges=rt.buildRangeData;auto& counts=rt.countData;geometries.clear();buildRanges.clear();counts.clear();
            for(auto range:ranges){VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};g.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;g.geometry.triangles={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};auto& t=g.geometry.triangles;t.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;t.vertexData.deviceAddress=geometry.pose->vertices.address;t.vertexStride=sizeof(PackedVertex);t.maxVertex=geometry.pose->count-1;t.indexType=VK_INDEX_TYPE_UINT32;t.indexData.deviceAddress=geometry.indices->address+range[0]*sizeof(std::uint32_t);geometries.push_back(g);buildRanges.push_back({range[1]/3,0,0,0});counts.push_back(range[1]/3);}
            VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};info.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;info.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR|VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;info.geometryCount=geometries.size();info.pGeometries=geometries.data();
            VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};rt.buildSizes(s.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&info,counts.data(),&sizes);
            bool update=!topologyChanged&&blas->acceleration.handle; if(!update){if(blas->acceleration.handle)slot.garbage.push_back(blas->acceleration);blas->acceleration=rt.acceleration(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,sizes.accelerationStructureSize);}
            info.mode=update?VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR:VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;info.srcAccelerationStructure=update?blas->acceleration.handle:VK_NULL_HANDLE;info.dstAccelerationStructure=blas->acceleration.handle;auto scratch=rt.scratch(update?sizes.updateScratchSize:sizes.buildScratchSize);info.scratchData.deviceAddress=(scratch.address+rt.scratchAlignment-1)/rt.scratchAlignment*rt.scratchAlignment;
            barrier(s.command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
            auto* rangePointer=buildRanges.data();rt.buildAs(s.command,1,&info,&rangePointer);blas->ranges=ranges;blas->poseVersion=pose->version;
        }
        if(instances.size()>=0xffffffu)throw std::runtime_error("Ray TLAS instance index exceeds Vulkan 24-bit custom index");VkAccelerationStructureInstanceKHR instance{};for(unsigned row=0;row<3;++row)for(unsigned column=0;column<4;++column)instance.transform.matrix[row][column]=geometry.transform[column*4+row];instance.instanceCustomIndex=instances.size();instance.mask=d.model&&d.model->formatVersion==6?1:2; // 1 level/map geometry, 2 models and sprites (contact shadows).
        instance.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;instance.accelerationStructureReference=blas->acceleration.address;instances.push_back(instance);records.push_back({geometry.pose->vertices.address,geometry.indices->address,static_cast<std::uint32_t>(materials.size()),0,{},geometry.clipPlane});
        for(const auto& m:geometry.materials)if(m.count){GpuMaterial packed{};packed.uv=m.uvTransform;packed.tint=color(m.tint);packed.factors={m.alpha,std::clamp(m.interpolation,0.f,1.f),0,0};packed.metadata={m.flags,texture(m.texture,m.sampler),texture(m.normalTexture,m.normalSampler),std::max(m.spriteFrames,1u)};packed.frames={m.frame0,m.frame1,m.first,m.count};packed.normalUv=m.normalUvTransform;packed.ambient=color(m.effectAmbient);packed.skillMain=m.skillMain;packed.skillExtra=m.skillExtra;packed.sun={m.effectSunDirection.x,m.effectSunDirection.y,m.effectSunDirection.z,0};packed.camera={m.effectCameraPosition.x,m.effectCameraPosition.y,m.effectCameraPosition.z,0};packed.preparedAmbient=m.preparedAmbient;packed.preparedDiffuse=m.preparedDiffuse;packed.preparedSpecular=m.preparedSpecular;packed.preparedEmissive=m.preparedEmissive;packed.ambientLight=m.ambientLight;packed.viewer={m.infiniteViewer.x,m.infiniteViewer.y,m.infiniteViewer.z,0};packed.lightState={static_cast<std::int32_t>(m.lightCount),m.selectedLight,static_cast<std::int32_t>(texture(m.lightmap,m.lightmapSampler)),0};if(m.lightmap)packed.metadata[0]|=8192u;packed.factors[2]=m.surfaceAmbient;packed.factors[3]=m.materialAmbient;materials.push_back(packed);}
    }
    // The existing GPU padding word carries the finite, scene-topology reflection layer bound.
    if(!records.empty())records.front().padding=static_cast<std::uint32_t>(triangleCount);
    if(slot.sceneUsed==slot.scenes.size())slot.scenes.emplace_back();auto& packet=slot.scenes[slot.sceneUsed++];
    auto& input=rt.upload(packet.instances,std::max<std::size_t>(instances.size()*sizeof(VkAccelerationStructureInstanceKHR),sizeof(VkAccelerationStructureInstanceKHR)),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,instances.empty()?nullptr:instances.data());
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};geometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;geometry.geometry.instances={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};geometry.geometry.instances.data.deviceAddress=input.address;
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};info.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;info.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR|VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;info.geometryCount=1;info.pGeometries=&geometry;
    std::uint32_t count=instances.size();VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};rt.buildSizes(s.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&info,&count,&sizes);
    bool update=packet.tlas.handle&&packet.count==count;auto& tlas=packet.tlas;
    if(!update){if(tlas.handle)slot.garbage.push_back(tlas);tlas=rt.acceleration(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,sizes.accelerationStructureSize);}
    info.mode=update?VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR:VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;info.srcAccelerationStructure=update?tlas.handle:VK_NULL_HANDLE;info.dstAccelerationStructure=tlas.handle;
    auto scratch=rt.scratch(update?sizes.updateScratchSize:sizes.buildScratchSize);info.scratchData.deviceAddress=(scratch.address+rt.scratchAlignment-1)/rt.scratchAlignment*rt.scratchAlignment;
    barrier(s.command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
    VkAccelerationStructureBuildRangeInfoKHR range{count,0,0,0};auto* rangePointer=&range;rt.buildAs(s.command,1,&info,&rangePointer);packet.count=count;
    rt.upload(packet.records,std::max<std::size_t>(records.size()*sizeof(GpuInstance),sizeof(GpuInstance)),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,records.empty()?nullptr:records.data());rt.upload(packet.materials,std::max<std::size_t>(materials.size()*sizeof(GpuMaterial),sizeof(GpuMaterial)),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,materials.empty()?nullptr:materials.data());
    textures.resize(textureLimit,textures.front());packet.textures.swap(textures);rt.scene_descriptors(packet);
    barrier(s.command,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_SHADER_READ_BIT);
}
void rt_replace_texture(RayTracing& rt,VkImageView oldView,VkImageView newView){
    if(!rt.s.rayEnabled||oldView==newView)return;
    auto& slot=rt.slot();if(!slot.set||!slot.sceneUsed)return;
    auto& scene=slot.scenes[slot.sceneUsed-1];bool changed=false;
    for(auto& texture:scene.textures)if(texture.imageView==oldView){texture.imageView=newView;changed=true;}
    // Descriptor writes are copied by Vulkan: prior draw sets retain the old, fence-retired views.
    if(changed)rt.scene_descriptors(scene);
}
VkDescriptorSetLayout rt_layout(const RayTracing& rt){return rt.layout;}
VkDescriptorSet rt_set(const RayTracing& rt){return rt.slots[rt.s.slotIndex].set;}
void rt_release(RayTracing& rt){
    for(auto& slot:rt.slots){for(auto& b:slot.blas)rt.destroy(b.acceleration);slot.blas.clear();slot.blasLookup.clear();for(auto& scene:slot.scenes){rt.destroy(scene.tlas);rt.s.destroy(scene.instances);rt.s.destroy(scene.records);rt.s.destroy(scene.materials);}slot.scenes.clear();slot.sceneUsed=0;for(auto& a:slot.garbage)rt.destroy(a);slot.garbage.clear();for(auto& p:slot.poses)rt.s.destroy(p->pose.vertices);slot.poses.clear();slot.poseGroups.clear();slot.poseLookup.clear();slot.set={};}
    for(auto* map:{&rt.models,&rt.expandedModels}){for(auto& [key,m]:*map){rt.s.destroy(m.base);rt.s.destroy(m.endpoints);}map->clear();}for(auto& [key,o]:rt.overrides)rt.s.destroy(o.endpoint);rt.overrides.clear();
}
void rt_purge_model(RayTracing& rt,const Model* model){
    for(auto& slot:rt.slots){
        for(auto& scene:slot.scenes){rt.destroy(scene.tlas);scene.count=0;}slot.set={};
        for(auto it=slot.blas.begin();it!=slot.blas.end();)if(it->pose->model==model){rt.destroy(it->acceleration);it=slot.blas.erase(it);}else ++it;
        slot.blasLookup.clear();for(std::size_t i=0;i<slot.blas.size();++i)slot.blasLookup[slot.blas[i].pose].push_back(i);
        for(auto it=slot.poses.begin();it!=slot.poses.end();)if((*it)->model==model){slot.poseLookup.erase(&(*it)->pose);rt.s.destroy((*it)->pose.vertices);it=slot.poses.erase(it);}else ++it;
        for(auto it=slot.poseGroups.begin();it!=slot.poseGroups.end();)if(it->first.model==model)it=slot.poseGroups.erase(it);else ++it;
    }
    for(auto* map:{&rt.models,&rt.expandedModels})if(auto it=map->find(model);it!=map->end()){rt.s.destroy(it->second.base);rt.s.destroy(it->second.endpoints);map->erase(it);}for(auto it=rt.overrides.begin();it!=rt.overrides.end();)if(it->second.model==model){rt.s.destroy(it->second.endpoint);it=rt.overrides.erase(it);}else ++it;
}
void rt_purge_image(RayTracing& rt,const k22::Image*){
    // Material records and sampled-image sets are frame-slot-owned, never retained by image identity.
    // The public purge wrapper drains before raster destroys image views; invalidate those dead sets.
    for(auto& slot:rt.slots)slot.set={};
}
}
