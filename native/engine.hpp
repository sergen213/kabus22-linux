#pragma once
#include "value.hpp"
#include "assets.hpp"
#include "render.hpp"
#include "media.hpp"
#include "physics.hpp"
#include "controller.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>
#include <optional>
#include <unordered_set>
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
struct EntityData {const Model* model=nullptr;std::unique_ptr<Model> ownedModel;std::string file;std::vector<ObjectId> skinObjects;std::size_t path=0,node=0;bool hasPath=false,scannable=true,physicsGeometry=true;double animation=0;std::uint32_t frame0=0,frame1=0,type=2;float frameBlend=0;ObjectId parent=0;Vec3 boundsOffset{};EntityLightCache light;std::uint8_t visibleLod=0;bool lightResident=false;std::uint64_t legacyPushEpoch=0;};
struct MeshOverride {std::vector<Vec3> mesh,meshNormals,sourceMesh,sourceMeshNormals;std::uint64_t revision=0;ObjectId meshId=0;Vec3 min,max;};
class Engine;
Vec3 worldModelVertex(const Engine&,ObjectId,const EntityData&,std::size_t renderIndex);
struct FileData {std::vector<std::uint8_t> bytes;std::size_t cursor=0;std::filesystem::path output;bool writable=false;};
struct ParticleData {ObjectId object=0;FunctionId callback=0;ObjectId owner=0;bool legacy=false,first=true;std::int32_t birthTick=0;};
struct CollisionNode {Vec3 min,max;std::uint32_t begin=0,count=0,left=0,right=0;};
// Seamless door geometry (native/seamless_doors.cpp). center: doorway centre on the door face plane; normal: into the room.
struct DoorSide {Vec3 center{},normal{},right{};float halfWidth{},halfHeight{},floor{};};
// b: the destination's return door; position/angles map destination coordinates into the source level (DrawInstance convention).
struct DoorLink {DoorSide a,b;Vec3 position{},angles{};};
std::optional<DoorSide> door_side(const World&,std::span<const Vec3> triggers);
std::vector<std::vector<Vec3>> door_clusters(const World&,int target); // LY_Kapi triggers with LevelID target, grouped per doorway
// arrival: the destination's original Giris placement for this transition (x,y,z,pan), if known.
std::optional<DoorLink> door_link(const World& from,std::span<const Vec3> triggers,const World& to,int fromId,const std::array<float,4>* arrival);
int door_table(int argc,char** argv); // kabus22 --door-table LEVELS.tsv --data GAME_ROOT
class Engine {
public:
 Engine(Assets&,Renderer*,Media&,State&);
 ~Engine();
 void initialize_plugins(const Runtime&);
 void prepare(Runtime&);
 EntityData& register_entity(ObjectId);
 void configure_language(Runtime&,std::string_view,bool explicitChoice);
 // Only an auto-selected original campaign graph is eligible; custom/patch/Bonus retain authored services.
 bool configure_display(Runtime&,bool originalCampaignEntry,bool native=true,int width=0,int height=0,WindowMode mode=WindowMode::Borderless);
 void resolve_builtin_font();
 Value builtin(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 std::optional<Value> service_builtin(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 std::optional<Value> world_builtin(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 Value key_state(int)const;
 void tick(Runtime&,const Input&);
 // Native convenience: skips the startup logos / New Game intro / full-screen movies on a confirm or cancel press; returns the input tick() should see.
 const Input& skip_intro(Runtime&,const Input&);
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
 std::deque<std::vector<ObjectId>> worldQueryScratch;std::size_t worldQueryDepth=0;std::vector<ObjectId> collideScratch_;
 std::vector<ParticleData> activeParticles;
 std::unordered_map<const Model*,std::unordered_map<std::size_t,MeshOverride>> meshOverrides;std::uint32_t nextMeshId=1;
 std::unordered_map<const Image*,ObjectId> skinBitmapObjects;
 std::string pendingLevel,currentLevel;bool pendingLevelLoad=false; // currentLevel: last LEVEL_LOAD file (diagnostics/scenarios).
 std::vector<CollisionNode> collisionNodes;std::vector<std::uint32_t> collisionOrder;ObjectId collisionYou=0,collisionActor=0;
 std::unordered_map<std::uint32_t,FileData> files;std::uint32_t nextFile=1;
 // fixedRandom (scenario mode): randomize() keeps the seeded stream instead of drawing from std::random_device.
 std::filesystem::path saveRoot;std::mt19937 random{0};bool fixedRandom=false;
 Input input;bool quit=false;std::uint32_t movie=0;double gamma=1;Vec3 colorAdjustment{1,1,1};
 Input skipInput;bool skipHold=false,movieLoop=false; // skip_intro: input replacement, "wait until everything is released"; movieLoop: full-screen movie is media_loop (menu background)
 ColorAdjustment nativeAdjustment;
 std::unordered_map<std::uint32_t,ObjectId> movieTargets;
 std::unordered_map<ObjectId,std::shared_ptr<const Image>> bitmapImages;
 WindowMode currentWindowMode=WindowMode::Windowed;
 int discordSocket=-1;std::uint32_t discordNonce=1;std::string discordApplication;
 std::map<std::string,std::string,std::less<>> iniValues;
 bool iniInitialized=false,pluginNative=false,pluginGamma=false;
 int windowStyle=0;std::vector<std::uint8_t> discordInput;bool discordReady=false;std::uint64_t discordRetryAt=0;
 std::string discordDetails,discordState;bool discordActivitySent=false;
 ControllerConfig controller;float rumble=0; // rumble: strength requested this tick (0 none).
 // Optional community-patch 1.8.5 first-person camera (cam.wdl fps_camera_control), engine-side for original scripts.
 enum class CameraMode {Original,FirstPerson};CameraMode cameraMode=CameraMode::Original;
 bool first_person_active()const{return firstPersonActive_;}
 // Native developer console, quicksave/quickload and cheats (docs/CONSOLE.md, native/console.cpp); not in the original.
 // console(): call before skip_intro/tick; handles the toggle, line editing and F5/F9, returns the input the game sees.
 const Input& console(Runtime&,const Input&);
 std::string console_command(Runtime&,std::string_view line); // one command line; returns its output
 std::string quicksave(Runtime&);
 std::string quickload(Runtime&,int slot=-1); // -1: the newest slot (Aktif_SaveNo-1)
 bool quickSaving=false; // file_close adds the player position to the slot sidecar while set
 struct PositionRestore {int level=0;Vec3 position{};float pan=0;bool started=false;int ticks=0;};
 std::optional<PositionRestore> positionRestore; // set by a quicksave slot read; applied once its level is in play
 // Native seamless doors (docs/SEAMLESS_DOORS.md, native/seamless_doors.cpp); false = the original LevelYukle fade exactly.
 bool seamlessDoors=false;
 // main loop: do not present (last frame stays up); an open switch prompt pauses door_tick, so it must stay visible.
 bool hold_presentation()const{return door_.phase==DoorPhase::Holding&&switchPrompt_<0;}
 int door_phase()const; // 0 idle, 1 opening, 2 crossing, 3 holding, 4 revealing
 std::uint32_t doorSwaps=0,fadeFrames=0; // completed seamless swaps; presented frames with a visible LYP_Fade (scenario $fadeframes)
 void door_scan(Runtime&,ObjectId trigger); // after a scan event: an LY_Kapi request may become a doorway preview
 // Character lock (native/character.cpp, docs/CHARACTER_LOCK.md); not in the original. -1: the body follows Aktif_Player.
 int characterLock=-1;
 // After Runtime::load: reroutes the body/menu/weapon reads of Aktif_Player. defaultLock (--character): the New Game selection
 // and the lock of loaded slots that carry none (original saves), -1 original.
 void configure_characters(Runtime&,int defaultLock=-1);int characterDefault=-1;
 void set_character_lock(int character);
 int character_prompt()const{return switchPrompt_;}
 int story_character()const;int body_character()const;int menu_character()const;int weapon_character()const;
private:
 enum class DoorPhase {Idle,Opening,Crossing,Holding,Revealing};
 struct DoorGroup {int target=0;std::vector<Vec3> triggers;Vec3 centroid{};std::optional<DoorLink> link;bool linked=false;};
 struct DoorState {DoorPhase phase=DoorPhase::Idle;std::vector<DoorGroup> groups;const World* groupsWorld=nullptr;std::size_t group=~std::size_t{},warm=~std::size_t{};
  const World* world=nullptr;std::string wmb;int from=0,target=0,ticks=0,held=0;float open=0,openTime=0;} door_;
 bool fadeHidden_=false,fadeVisible_=false;
 void door_tick(Runtime&);void door_frame();void door_reset();void door_level();const World* door_load(std::string_view level);
 const World* door_world(Runtime&,DoorGroup&,bool wait);std::optional<std::array<float,4>> door_arrival(Runtime&,int to,int from);
 bool girisParsed_=false;std::unordered_map<std::string,std::vector<std::pair<int,std::string>>> girisPlacements_;bool door_scripted(int from,int to)const;std::string level_wmb(int id)const;
 friend class Physics;
 friend std::optional<Value> worldModelBuiltin(Engine&,Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 void initialize_globals();void make_frame(bool mouseOutput=true);void update_input(Runtime&);void update_ui(Runtime&);void update_first_person();
 void console_tick(Runtime&);void console_overlay();void console_print(std::string_view);void notify(std::string);
 std::string gameplay_blocked(const Runtime&)const;ObjectId live_player()const;std::string saved_position()const;void read_saved_position(const std::filesystem::path& slot);
 bool consoleOpen_=false,consoleRelease_=false,god_=false,noclip_=false,fpsOverlay_=false;ObjectId noclipPlayer_=0;
 void apply_character_lock(Runtime&,std::vector<std::pair<ObjectId,FunctionId>>& placedActions);
 bool character_tick(Runtime&,const Input&); // true: a switch prompt is open and the scripts stay paused this frame
 void character_weapons(Runtime&,bool spawning);int ebruWeapon_=0;
 void character_overlay();float draw_menu_word(std::string_view,float x,float y,float scale,float alpha,bool measure);
 int storyAtLevel_=-1,switchPrompt_=-1,promptCursor_=0,newGameChoice_=0;bool adoptStory_=false,promptRelease_=false,promptArmed_=false,newGameMenu_=false;
 std::shared_ptr<const Image> menuAtlas_;std::array<Rect,256> menuGlyphs_{};std::array<Rect,2> menuArrows_{},promptRows_{};std::string promptTitle_,language_;
 std::string consoleLine_,consolePrompt_,toast_,fpsText_;std::deque<std::string> consoleLog_;std::vector<std::string> consoleHistory_;std::size_t historyCursor_=0;
 double toastUntil_=0,fpsTime_=0;int fpsFrames_=0;Input consoleInput_;
 bool display_admitted();void prepare_display();float display_aspect(float authored)const;
 bool firstPersonActive_=false;double firstPersonTilt_=0;std::array<double,7> firstPersonWritten_{},firstPersonScript_{};
 std::array<float,2> mouseForcePrior_{}; // A6 006bb5e4/006bb5e0: last frame's scaled mickeys (x, -y).
 static constexpr std::size_t DisplayTier=2; // GA_Cozunurluk slot owned by native display: 1024x768, the >1 art tier.
 bool nativeDisplay_=false;int displayWidth_=0,displayHeight_=0;
 std::uint8_t authoredResolution_=0; // User's GA_Cozunurluk (options.k22 byte 0); native tier stays in memory only.
 ObjectId displayResolution_=0,displayScale_=0,displayScale2_=0,displayFont_=0;
 enum class DisplayRole { Ordinary,MapPixel,Grid,Fill,IntroTitle,EyeRight,EyeLeft,Text,LoadList,LoadCursor,Credit,CreditFrame,InventoryLabel,Document,Bar,Boss,BossEnergy,Pause,Menu,Gameplay };
 struct DisplayObject {ObjectId id;DisplayRole role;int index=0;float menuY=0;bool menuPrepared=false;ObjectId bitmap=0;Vec2 bitmapSize{};float bossScaleX=0;};
 std::vector<DisplayObject> displayObjects_;
 std::array<char,32> displayLabel_{};
 std::array<ObjectId,3> displayScrolls_{};
 int displayLoadScroll_=0,displayLoadBudget_=0;
 std::unordered_set<ObjectId> displayFills_;
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
 Hit collide_legacy(Vec3,Vec3,std::uint32_t,ObjectId,const ProcessContext*,bool movement=false);
 void prepare_legacy_hit(Hit&,Vec3,Vec3,std::uint32_t,ObjectId);
 void publish_movement_hit(const Hit&,Vec3 displacement);
 Hit move_a6_legacy(Runtime&,ProcessContext&,ObjectId,Vec3&,Vec3,std::uint32_t);
 Hit move_a6_modern(Runtime&,ProcessContext&,ObjectId,Vec3&,Vec3,std::uint32_t);
 Hit move_a7_modern(Runtime&,ProcessContext&,ObjectId,Vec3&,Vec3,std::uint32_t);
 std::uint64_t legacyMoveEpoch_=0;
 bool loadingPlacedEntities_=false;
 // Per-source-module B/G/R, scalar, ambient, style and flags; not per-entity/public trace state.
 std::array<int,7> a6LegacyTextureTemps_{},a7LegacyTextureTemps_{};
 bool a7WorldQueryRoute_=false; // 1005af0a latches global-WMB info bit4; not a network/CONNECTION flag.
 struct Button {Rect rect;FunctionId callback=0,leaveCallback=0,overCallback=0;ObjectId owner=0;int declaration=0;float angle=0;};std::vector<Button> buttons_;
 struct BitmapFont {FontAtlas atlas;std::vector<std::uint8_t> indices;};
 std::deque<std::string> uiText_;std::size_t uiTextUsed_=0;std::unordered_map<ObjectId,BitmapFont> bitmapFonts_;std::vector<ObjectId> uiObjects_;
 std::array<std::uint8_t,768> fontPalette_{};std::uint64_t fontPaletteSerial_=0;
 void ensure_font_palette(std::string_view entry);
 void set_font_palette(std::span<const std::uint8_t>);
 std::unordered_map<ObjectId,const FontAtlas*> systemFonts_;
 std::array<Slot*,128> keyboard_{},keyboardCallbacks_{};
 std::array<std::array<Slot*,12>,2> joystick_{},joystickCallbacks_{};
 std::bitset<32> controllerHeld_;Vec3 modernForce_{};DamageRumble damageRumble_;std::string saveScript_;bool saveScriptKnown_=false;
 std::array<Slot*,3> mouse_{},mouseCallbacks_{};
 std::optional<std::size_t> pressedButton_,hoveredButton_;std::size_t focusedButton_=0;
 Vec2 mouseHit_{};
 std::vector<ObjectId> levelEntities_;double time_=0;int currentWidth_=0,currentHeight_=0;bool viewsPrepared_=false;
 Vec2 skySpeed_{1,1}; // SKY_SPEED is copied into the implicit dome when its level is created.
};
}
