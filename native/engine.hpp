#pragma once
#include "value.hpp"
#include "assets.hpp"
#include "render.hpp"
#include "media.hpp"
#include "physics.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>
#include <optional>
namespace k22 {
class Runtime;
struct TextureSample;
int legacy_scancode(int code);
// A6 00434730 / A7 1003ac80 birth; these are private sampled values, not public AMBIENT/RGB.
struct EntityLightCache {
 std::int16_t scalar=127;std::array<std::int16_t,3> delta{};std::uint8_t style=50;
 std::int32_t floorDistance=0;bool dirty=true,observed=false;Vec3 position{};
 bool floorValid=false;NativeShadowState shadow{};
 const Model* model=nullptr;std::uint32_t flags=0;
};
struct EntityData {const Model* model=nullptr;std::unique_ptr<Model> ownedModel;std::string file;std::vector<ObjectId> skinObjects;std::size_t path=0,node=0;bool hasPath=false,scannable=true,physicsGeometry=true;double animation=0;std::uint32_t frame0=0,frame1=0,type=2;float frameBlend=0;ObjectId parent=0;Vec3 boundsOffset{};EntityLightCache light;std::uint8_t visibleLod=0;bool lightResident=false;};
struct MeshOverride {std::vector<Vec3> mesh,meshNormals,sourceMesh,sourceMeshNormals;std::uint64_t revision=0;ObjectId meshId=0;Vec3 min,max;};
class Engine;
Vec3 worldModelVertex(const Engine&,ObjectId,const EntityData&,std::size_t renderIndex);
struct FileData {std::vector<std::uint8_t> bytes;std::size_t cursor=0;std::filesystem::path output;bool writable=false;};
struct ParticleData {ObjectId object=0;FunctionId callback=0;ObjectId owner=0;bool legacy=false,first=true;std::int32_t birthTick=0;};
struct CollisionNode {Vec3 min,max;std::uint32_t begin=0,count=0,left=0,right=0;};
class Engine {
public:
 Engine(Assets&,Renderer*,Media&,State&);
 ~Engine();
 void initialize_plugins(const Runtime&);
 void prepare(Runtime&);
 EntityData& register_entity(ObjectId);
 void configure_language(Runtime&,std::string_view,bool explicitChoice);
 void resolve_builtin_font();
 Value builtin(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 std::optional<Value> service_builtin(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 std::optional<Value> world_builtin(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 Value key_state(int)const;
 void tick(Runtime&,const Input&);
 // A7: after successful draw; headless callers use their explicit logical CPU-frame boundary.
 void finish_frame(Runtime&);
 const RenderFrame& frame()const{return frame_;}
 void load_level(Runtime&,std::string_view);
 ObjectId create_entity(Runtime&,std::string_view,Vec3,FunctionId,ObjectId parent=0);
 void remove_entity(Runtime&,ObjectId);
 Vec3 vector(const Argument&)const;
 void set_vector(const Argument&,Vec3);
 std::string_view string(Value)const;
 std::string& mutable_string(const Argument&);
 double number(std::string_view)const;
 Value global(std::string_view)const;
 Argument global_vector(std::string_view)const;
 void set_global(std::string_view,Value);
 double field(ObjectId,std::string_view)const;
 void set_field(ObjectId,std::string_view,double);
 Vec3 position(ObjectId)const;
 Vec3 angles(ObjectId)const;
 void set_position(ObjectId,Vec3);
 std::uint32_t flags(ObjectId)const;
 std::uint32_t constant(std::string_view)const;
 FunctionId callback(ObjectId,std::string_view)const;
 Value new_string(std::string);
 void invoke_event(Runtime&,ObjectId,ObjectId,int);
 NativeEntityLightState prepare_entity_light(const DrawInstance&,const Camera&);
 struct Hit {bool hit=false;float fraction=1;Vec3 position,normal;ObjectId entity=0;std::uint32_t triangle=~0u,vertex=0,surfaceFlags=0;const Material* material=nullptr;bool textureScan=false,modernTexture=false;bool contentQuery=false,inPassable=false,onPassable=false;ObjectId passableEntity=0;
 bool legacyQuery=false;std::uint32_t status=0,bspNode=~0u;Vec3 localPosition{};float planeDistance=0;const World* queryWorld=nullptr;
 private:friend class Engine;float queryScalar_=0; // A7 BOX's signed scalar is not a normalized travel fraction.
 Vec3 publicTarget_{},publicNormal_{};std::int32_t resultRaw_=0;bool inSolid_=false,endpoint_=false;
 };
 Hit collide(Vec3 start,Vec3 end,Vec3 extent,std::uint32_t mode,ObjectId ignore,const ProcessContext* query=nullptr,bool modern=false,Runtime* callbacks=nullptr);
 void publish_hit(ProcessContext&,const Hit&,Vec3 start);
 // Slice-owned implementations share the actual VM object/state store, never shadow gameplay state.
 Assets& assets;Renderer* renderer;Media& media;State& state;Physics physics;
 const World* world=nullptr;std::unordered_map<ObjectId,EntityData> entities;
 std::deque<std::vector<ObjectId>> worldQueryScratch;std::size_t worldQueryDepth=0;
 std::vector<ParticleData> activeParticles;
 std::unordered_map<const Model*,std::unordered_map<std::size_t,MeshOverride>> meshOverrides;std::uint32_t nextMeshId=1;
 std::unordered_map<const Image*,ObjectId> skinBitmapObjects;
 std::string pendingLevel;bool pendingLevelLoad=false;
 std::vector<CollisionNode> collisionNodes;std::vector<std::uint32_t> collisionOrder;ObjectId collisionYou=0,collisionActor=0;
 std::unordered_map<std::uint32_t,FileData> files;std::uint32_t nextFile=1;
 std::filesystem::path saveRoot;std::mt19937 random{0};
 Input input;bool quit=false;std::uint32_t movie=0;double gamma=1;Vec3 colorAdjustment{1,1,1};
 ColorAdjustment nativeAdjustment;
 std::unordered_map<std::uint32_t,ObjectId> movieTargets;
 std::unordered_map<ObjectId,std::shared_ptr<const Image>> bitmapImages;
 WindowMode currentWindowMode=WindowMode::Windowed;
 int discordSocket=-1;std::uint32_t discordNonce=1;std::string discordApplication;
 std::map<std::string,std::string,std::less<>> iniValues;
 bool iniInitialized=false,pluginNative=false,pluginGamma=false;
 int windowStyle=0;std::vector<std::uint8_t> discordInput;bool discordReady=false;std::uint64_t discordRetryAt=0;
 std::string discordDetails,discordState;bool discordActivitySent=false;
private:
 friend class Physics;
 friend std::optional<Value> worldModelBuiltin(Engine&,Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 void initialize_globals();void make_frame(bool mouseOutput=true);void update_input(Runtime&);void update_ui(Runtime&);
 void level_progress(Runtime&,std::int64_t stage);
 float levelPercent_=0,levelQuantum_=0;double levelCallbackTime_=0;
 bool pendingExit_=false;Value exitMessage_;
 // A6 VECTOR/_vec return borrowed aliases into the shared 64-slot ring.
 std::array<Object*,64> vectorTemps_{};std::size_t vectorTempCursor_=0;
 RenderFrame frame_;std::vector<DrawInstance> draws_,shadows_;std::vector<Camera> cameras_;std::vector<UICommand> ui_;std::vector<Particle> particles_;std::vector<RenderLight> lights_;
 struct EntityLightSlot {ObjectId object{};bool synthetic=false;float born{};RenderLight light;bool occupied()const{return object||synthetic;}};
 std::array<EntityLightSlot,32> entityLightSlots_{};
 std::vector<std::array<float,7>> visibleLodRanges_;
 std::vector<SkinBinding> skinBindings_;
 std::vector<ScriptMaterialState> materials_;
 struct MaterialFrameIndex {std::uint64_t generation{};std::uint32_t index{};};
 std::unordered_map<ObjectId,MaterialFrameIndex> materialIndices_;
 std::uint64_t materialGeneration_=0;
 struct MaterialEffect {std::string resource;EffectKind kind=EffectKind::None;std::uint64_t generation{};};
 std::unordered_map<ObjectId,MaterialEffect> materialEffects_;
 std::uint64_t effectGeneration_=0;
 std::uint32_t material_index(Value);
 void observe_entity_light(ObjectId,EntityData&);
 void update_entity_light(ObjectId,EntityData&,const Camera&);
 TextureSample sample_texture_hit(const Hit&);
 struct QueryTriangle {std::array<std::uint32_t,3> indices{};std::uint32_t renderTriangle=~0u,material=0;};
 struct QueryGeometry {
  Vec3 sourceMin{},sourceMax{},bakedMin{},bakedMax{},bakedScale{1,1,1},bakedOrigin{};float bakedRadius=0;
  bool hasVertices=false,chunked=false,baked=false;std::vector<QueryTriangle> primary,auxiliary;
 };
 std::unordered_map<const Model*,QueryGeometry> queryGeometry_;
 // A7 1006e7c0 owns concrete payload/solver pose independently of the live resource.
 struct QueryActor {
  std::uint32_t type=0,shape=0;bool baked=false;
  Vec3 min{},max{},center{},alternateCenter{},selected{},scale{1,1,1};float radius=0;
  std::array<Vec3,3> axes{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
  const Model* snapshotResource=nullptr;Vec3 snapshotPosition{},snapshotAngles{},snapshotScale{1,1,1};
  std::vector<Vec3> vertices;std::vector<QueryTriangle> triangles,primary,auxiliary;
 };
 std::unordered_map<ObjectId,QueryActor> queryActors_;
 void initialize_query_actor(ObjectId,bool prepareResource=true);
 void construct_query_actor(ObjectId,QueryActor&);
 void refresh_query_pose(ObjectId);
 void refresh_query_pose(ObjectId,QueryActor&);
 void synchronize_query_body(ObjectId,dGeomID);
 void update_query_actor(ObjectId,int mode);
 void update_query_actors(Runtime&);
 struct QueryContact {Hit hit;const Model* model=nullptr;bool sourcePoint=false;};
 void sort_query_contacts(std::vector<QueryContact>&);
 std::deque<std::vector<QueryContact>> queryContacts_;
 QueryGeometry& prepare_query_geometry(ObjectId,const EntityData&,bool captureBake=false);
 Hit collide_legacy(Vec3,Vec3,std::uint32_t,ObjectId,const ProcessContext*);
 void prepare_legacy_hit(Hit&,Vec3,Vec3,std::uint32_t,ObjectId);
 void publish_movement_hit(const Hit&,Vec3 displacement);
 bool loadingPlacedEntities_=false;
 // Per-source-module B/G/R, scalar, ambient, style and flags; not per-entity/public trace state.
 std::array<int,7> a6LegacyTextureTemps_{},a7LegacyTextureTemps_{};
 bool a7WorldQueryRoute_=false; // 1005af0a latches global-WMB info bit4; not a network/CONNECTION flag.
 struct Button {Rect rect;FunctionId callback=0,leaveCallback=0,overCallback=0;ObjectId owner=0;int declaration=0;float angle=0;};std::vector<Button> buttons_;
 std::deque<std::string> uiText_;std::size_t uiTextUsed_=0;std::unordered_map<ObjectId,FontAtlas> bitmapFonts_;std::vector<ObjectId> uiObjects_;
 std::unordered_map<ObjectId,const FontAtlas*> systemFonts_;
 std::array<Slot*,128> keyboard_{},keyboardCallbacks_{};
 std::array<std::array<Slot*,12>,2> joystick_{},joystickCallbacks_{};
 std::array<Slot*,3> mouse_{},mouseCallbacks_{};
 std::optional<std::size_t> pressedButton_,hoveredButton_;std::size_t focusedButton_=0;
 Vec2 mouseHit_{};
 std::vector<ObjectId> levelEntities_;double time_=0;int currentWidth_=0,currentHeight_=0;bool viewsPrepared_=false;
 Vec2 skySpeed_{1,1}; // SKY_SPEED is copied into the implicit dome when its level is created.
};
}
