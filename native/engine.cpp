#include "engine.hpp"
#include "wdl.hpp"
#include "saves.hpp"
#include "modern_box.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
namespace k22 {

struct NativeDefault {const char* name;std::int32_t words[3];bool bonusOnly=false;};
static constexpr NativeDefault nativeDefaults[]={
{"VERSION",{6465,0,0}},
{"EDITION",{4096,0,0}},
{"COMPATIBILITY",{3072,0,0}},
{"PATCH",{0,0,0},true},
{"LOGO",{0,0,0}},
{"NEXUS",{0,0,0}},
{"EVENT_TYPE",{0,0,0}},
{"WARN_LEVEL",{1024,0,0}},
{"MAX_LOOPS",{5120000,0,0}},
{"FREEZE_MODE",{0,0,0}},
{"DEBUG_MODE",{0,0,0}},
{"DEBUG_EXTERNAL",{0,0,0}},
{"DEBUG_RESULT",{0,0,0}},
{"nullvector",{0,0,0}},
{"protocol",{0,0,0}},
{"r000",{0,0,0}},
{"r001",{0,0,0}},
{"r002",{0,0,0}},
{"r003",{0,0,0}},
{"r004",{0,0,0}},
{"r005",{0,0,0}},
{"TIME",{1024,0,0}},
{"TIME_FACTOR",{1024,0,0}},
{"TIME_SMOOTH",{512,0,0}},
{"TIME_FRAME",{0,0,0}},
{"TOTAL_TICKS",{0,0,0}},
{"TOTAL_SECS",{0,0,0}},
{"TOTAL_FRAMES",{0,0,0}},
{"FPS_MAX",{245760,0,0}},
{"FPS_MIN",{4096,0,0}},
{"FPS_LOCK",{0,0,0}},
{"TIME_MODELS",{0,0,0}},
{"TIME_SPRITES",{0,0,0}},
{"TIME_TERRAIN",{0,0,0}},
{"TIME_ACTIONS",{0,0,0}},
{"TIME_RENDER",{0,0,0}},
{"TIME_LEVEL",{0,0,0}},
{"TIME_ENTITIES",{0,0,0}},
{"TIME_EFFECTS",{0,0,0}},
{"TIME_PFUNCS",{0,0,0}},
{"TIME_PORTALS",{0,0,0}},
{"TIME_SKY",{0,0,0}},
{"TIME_CLIENT",{0,0,0}},
{"TIME_SERVER",{0,0,0}},
{"TIME_PANELS",{0,0,0}},
{"TIME_UPDATE",{0,0,0}},
{"TIME_PHYSICS",{0,0,0}},
{"TIME_TOTAL",{0,0,0}},
{"TIME_IDLE",{0,0,0}},
{"r021",{0,0,0}},
{"r022",{0,0,0}},
{"SYS_SECONDS",{0,0,0}},
{"SYS_MINUTES",{0,0,0}},
{"SYS_HOURS",{0,0,0}},
{"SYS_DAY",{0,0,0}},
{"SYS_MONTH",{0,0,0}},
{"SYS_YEAR",{0,0,0}},
{"SYS_DOW",{0,0,0}},
{"SYS_DOY",{0,0,0}},
{"r030",{0,0,0}},
{"r031",{0,0,0}},
{"r032",{0,0,0}},
{"r033",{0,0,0}},
{"r034",{0,0,0}},
{"r035",{0,0,0}},
{"ENABLE_JOYSTICK",{1024,0,0}},
{"NUM_JOYSTICKS",{0,0,0}},
{"JOY_SENSE",{1024,0,0}},
{"JOY_FORCE",{0,0,0}},
{"JOY_RAW",{0,0,0}},
{"JOY_ROT",{0,0,0}},
{"JOY_HAT",{0,0,0}},
{"JOY2_RAW",{0,0,0}},
{"JOY2_ROT",{0,0,0}},
{"JOY2_HAT",{0,0,0}},
{"r040",{0,0,0}},
{"r041",{0,0,0}},
{"r042",{0,0,0}},
{"ENABLE_MOUSE",{1024,0,0}},
{"MOUSE_MODE",{0,0,0}},
{"MOUSE_SENSE",{1024,0,0}},
{"MOUSE_FORCE",{0,0,0}},
{"MOUSE_POS",{0,0,0}},
{"MOUSE_SPOT",{0,0,0}},
{"MOUSE_POINTER",{1024,0,0}},
{"MOUSE_RANGE",{1024000,0,0}},
{"MOUSE_TIME",{4096,0,0}},
{"MOUSE_MOVING",{0,0,0}},
{"MOUSE_CALM",{1024,0,0}},
{"MOUSE_SYNC",{0,0,0}},
{"MOUSE_VALID",{1024,0,0}},
{"TOUCH_STATE",{0,0,0}},
{"MICKEY",{0,0,0}},
{"POINTER",{0,0,0}},
{"MOUSE_POS3D",{0,0,0}},
{"MOUSE_DIR3D",{0,0,0}},
{"r050",{0,0,0}},
{"r051",{0,0,0}},
{"r052",{0,0,0}},
{"ENABLE_KEY",{1024,0,0}},
{"KEY_SENSE",{1024,0,0}},
{"KEY_FORCE",{0,0,0}},
{"SHIFT_SENSE",{2048,0,0}},
{"KEY_SHIFT",{0,0,0}},
{"KEY_ANY",{0,0,0}},
{"KEY_LASTPRESSED",{0,0,0}},
{"INKEY_ACTIVE",{0,0,0}},
{"r060",{0,0,0}},
{"r061",{0,0,0}},
{"r062",{0,0,0}},
{"VIDEO_MODE",{6144,0,0}},
{"VIDEO_DEPTH",{32768,0,0}},
{"VIDEO_SCREEN",{2048,0,0}},
{"SCREEN_SIZE",{655360,0,0}},
{"SCREEN_COLOR",{0,0,0}},
{"WINDOW_FOCUS",{1024,0,0}},
{"r070",{0,0,0}},
{"r071",{0,0,0}},
{"r072",{0,0,0}},
{"r073",{0,0,0}},
{"r074",{0,0,0}},
{"r075",{0,0,0}},
{"D3D_TRANSFORM",{1024,0,0}},
{"D3D_MODE",{0,0,0}},
{"D3D_NOBSP",{0,0,0}},
{"D3D_NOENTS",{0,0,0}},
{"D3D_NOVIS",{0,0,0}},
{"D3D_NOTEX",{0,0,0}},
{"D3D_DITHER",{1024,0,0}},
{"D3D_LIGHTRES",{0,0,0}},
{"D3D_MONOCHROME",{0,0,0}},
{"D3D_LINES",{0,0,0}},
{"D3D_ANISOTROPY",{0,0,0}},
{"D3D_VBUFFERSIZE",{1048576,0,0}},
{"D3D_LOCKABLE",{0,0,0}},
{"D3D_SHADOWFILTER",{1024,0,0}},
{"D3D_AUTOTRANSPARENCY",{0,0,0}},
{"D3D_AUTOMATERIAL",{0,0,0}},
{"D3D_SHADERVERSION",{0,0,0}},
{"SHADOW_OFFSET",{2048,0,0}},
{"SHADOW_THRESHOLD",{51200,0,0}},
{"SHADOW_RANGE",{51200,0,0}},
{"SHADOW_STENCIL",{0,0,0}},
{"SHADOW_LOD",{1024,0,0},true},
{"D3D_SHADOWDEPTH",{24576,0,0}},
{"D3D_ALPHADEPTH",{32768,0,0}},
{"D3D_TEXSIZE",{0,0,0}},
{"D3D_TEXSMAPS",{0,0,0}},
{"D3D_TEXBMAPS",{0,0,0}},
{"D3D_TEXSKINS",{0,0,0}},
{"D3D_TEXSURFS",{0,0,0}},
{"D3D_TEXTOTAL",{0,0,0}},
{"D3D_TEXMEMORY",{0,0,0}},
{"D3D_TEXMEMMIN",{1048576,0,0}},
{"D3D_TEXFREE",{1048576,0,0}},
{"D3D_TEXRESERVED",{1048576,0,0}},
{"D3D_TEXMAX",{0,0,0}},
{"D3D_TEXLIMIT",{2097152,0,0}},
{"D3D_ENTSORT",{1024,0,0}},
{"D3D_NEAR",{256,1024,0}},
{"D3D_MIPMAPPING",{3072,0,0}},
{"D3D_VSYNC",{1024,0,0}},
{"D3D_TRIPLEBUFFER",{0,0,0}},
{"D3D_LODFACTOR",{12800,25600,51200}},
{"D3D_POINTLIGHTFALLOFF",{1536,0,0}},
{"D3D_SPOTLIGHTFALLOFF",{1536,0,0}},
{"D3D_SPOTLIGHTCONE",{30720,15360,1024}},
{"CLIP_RANGE",{51200000,0,0}},
{"CLIP_FACTOR",{2048,0,0}},
{"CLIP_PARTICLES",{512,0,0}},
{"CLIP_SIZE",{512,0,0}},
{"FLOOR_RANGE",{1024000,0,0}},
{"FLOOR_FACTOR",{1024,0,0}},
{"MIP_SHADED",{1024,0,0}},
{"MIP_FLAT",{1024,0,0}},
{"MIP_SPRITES",{2048,0,0}},
{"SHADOW_MODE",{61440,0,0}},
{"TRACE_FLOOR",{1024,0,0}},
{"MAX_PARTICLES",{10240000,0,0}},
{"MAX_ENTITIES",{409600,0,0}},
{"MAX_LIGHTS",{8192,0,0}},
{"SKY_COLOR",{131072,0,0}},
{"OLDSKY_COLOR",{131072,131072,131072}},
{"SKY_BLEND",{15360,0,0}},
{"SKY_SPEED",{1024,1024,0}},
{"CLOUD_SPEED",{3072,3072,0}},
{"SKY_SCALE",{1024,0,0}},
{"SKY_CURVE",{1024,0,0}},
{"SKY_CLIP",{0,0,0}},
{"SCENE_ANGLE",{0,-5120,0}},
{"SCENE_FIELD",{92160,0,0}},
{"SCENE_NOFILTER",{0,0,0}},
{"SCENE_COLOR",{131072,131072,131072}},
{"SUN_LIGHT",{51200,0,0}},
{"SUN_COLOR",{131072,131072,131072}},
{"SUN_ANGLE",{46080,30720,102400000}},
{"SUN_POS",{0,0,0}},
{"SUN_FIELD",{10240,0,0}},
{"FOG_COLOR",{0,0,0}},
{"D3D_FOGCOLOR1",{0,0,0}},
{"D3D_FOGCOLOR2",{0,0,0}},
{"D3D_FOGCOLOR3",{0,0,0}},
{"D3D_FOGCOLOR4",{0,0,0}},
{"TURB_SPEED",{1024,0,0}},
{"TURB_RANGE",{1024,0,0}},
{"TEX_CYCLES",{10240,0,0}},
{"DETAIL_SIZE",{4096,0,0}},
{"POS_RESOLUTION",{1024,0,0}},
{"TEX_SHARE",{0,0,0}},
{"MAP_SUBENTS",{1024,0,0}},
{"NUM_PARTICLES",{0,0,0}},
{"NUM_ENTITIES",{0,0,0}},
{"NUM_VISENTS",{0,0,0}},
{"NUM_VISMAPPOLYS",{0,0,0}},
{"NUM_VISENTPOLYS",{0,0,0}},
{"NUM_VISTRIANGLES",{0,0,0}},
{"NUM_ACTIONS",{0,0,0}},
{"CONNECTION",{0,0,0}},
{"DPLAY_MAXCLIENTS",{25600,0,0}},
{"DPLAY_SYNC",{0,0,0}},
{"DPLAY_UNRELIABLE",{0,0,0}},
{"DPLAY_SMOOTH",{2048,0,0}},
{"DPLAY_LOCALFUNCTION",{0,0,0}},
{"DPLAY_CLIENTTIMEOUT",{327680,0,0}},
{"DPLAY_ENTUPDATE",{3072,0,0}},
{"DPLAY_PINGRATE",{16384,0,0}},
{"DPLAY_ENTRATE",{1024,0,0}},
{"DPLAY_PACKETSIZE",{131072,0,0}},
{"DPLAY_TIMEOUT",{3072,0,0}},
{"DPLAY_LATENCY",{0,0,0}},
{"DPLAY_BPS",{0,0,0}},
{"DPLAY_BPSPEAK",{0,0,0}},
{"DPLAY_BPSRELIABLE",{0,0,0}},
{"DPLAY_BPSUNRELIABLE",{0,0,0}},
{"DPLAY_DROPPED",{0,0,0}},
{"RECBUF_SIZE",{0,0,0}},
{"DPLAY_DIAG",{0,0,0}},
{"r630",{0,0,0}},
{"r631",{0,0,0}},
{"r632",{0,0,0}},
{"r633",{0,0,0}},
{"r634",{0,0,0}},
{"r635",{0,0,0}},
{"SOUND_VOL",{102400,0,0}},
{"CDAUDIO_VOL",{51200,0,0}},
{"MIDI_VOL",{51200,0,0}},
{"MEDIA_HANDLE",{0,0,0}},
{"MEDIA_LAYER",{921600,0,0}},
{"NUM_MEDIASTREAMS",{0,0,0}},
{"DOPPLER_FACTOR",{1024,0,0}},
{"r640",{0,0,0}},
{"r641",{0,0,0}},
{"r642",{0,0,0}},
{"HULL_FATMIN",{-32768,-32768,-16384}},
{"HULL_FATMAX",{32768,32768,32768}},
{"HULL_NARROWMIN",{-16384,-16384,-16384}},
{"HULL_NARROWMAX",{16384,16384,16384}},
{"MAX_PH_ENTITIES",{1023998976,0,0}},
{"ENABLE_POLYCOLLISION",{1024,0,0}},
{"TRACE_MODE",{0,0,0}},
{"MOVE_MODE",{0,0,0}},
{"MOVE_FRICTION",{256,0,0}},
{"INPUT_DIST",{0,0,0}},
{"NORMAL",{0,0,0}},
{"BOUNCE",{0,0,0}},
{"TARGET",{0,0,0}},
{"VGLIDE",{0,0,0}},
{"ON_PASSABLE",{0,0,0}},
{"IN_PASSABLE",{0,0,0}},
{"IN_SOLID",{0,0,0}},
{"TEX_LIGHT",{0,0,0}},
{"TEX_COLOR",{0,0,0}},
{"TEX_FOG",{0,0,0}},
{"TEX_FLAG1",{0,0,0}},
{"TEX_FLAG2",{0,0,0}},
{"TEX_FLAG3",{0,0,0}},
{"TEX_FLAG4",{0,0,0}},
{"TEX_FLAG5",{0,0,0}},
{"TEX_FLAG6",{0,0,0}},
{"TEX_FLAG7",{0,0,0}},
{"TEX_FLAG8",{0,0,0}},
{"HITVERTEX",{-1024,0,0}},
{"TERRAIN_CELL_SIZE",{10240,0,0}},
{"TRACE_HIT",{0,0,0}},
{"PH_CANCEL_CONTACT",{0,0,0}},
{"PH_NUM_CONTACTS",{0,0,0}},
{"PH_ITERATIONS",{0,0,0}},
{"PH_FPS_MAX_LOCK",{71680,0,0}},
{"MOVE_MIN_Z",{-1024,0,0}},
{"DISABLE_Z_GLIDE",{0,0,0}},
{"MOVE_BUFFER",{0,0,0}},
{"PH_CHECK_DISTANCE",{10240,0,0}},
{"_arg1",{0,0,0}},
{"_arg2",{0,0,0}},
{"_arg3",{0,0,0}},
{"_arg4",{0,0,0}},
{"nullskill",{0,0,0}},
{"RESULT",{0,0,0}},
{"DRAW_ENTITIES",{1024,0,0}},
{"ERROR_CODE",{0,0,0}},
{"CLIENT",{0,0,0}},
{"DLL_HANDLE",{0,0,0}},
{"DLL_APPHANDLE",{0,0,0}},
{"OUT_VAR1",{0,0,0}},
{"OUT_VAR2",{0,0,0}},
{"PI",{3216,0,0}},
{"TWO_PI",{6433,0,0}},
{"HALF_PI",{1608,0,0}},
{"CD_TRACK",{0,0,0}},
{"TIME_FAC",{1024,0,0}},
{"STR_LEN",{0,0,0}},
{"GAMMA",{1126,0,0}},
{"D3D_ANTIALIAS",{0,0,0}},
{"D3D_SHADOWSONLY",{0,0,0}},
{"RENDER_INFLATE",{0,0,0}},
{"PIXEL_CENTER",{0,0,0}},
{"MY_AGE",{0,0,0}},
{"MY_POS",{0,0,0}},
{"MY_ANGLE",{0,0,0}},
{"MY_SPEED",{0,0,0}},
{"MY_COLOR",{0,0,0}},
{"MY_SIZE",{0,0,0}},
{"MY_TRANSPARENT",{0,0,0}},
{"MY_FLARE",{0,0,0}},
{"MY_BRIGHT",{0,0,0}},
{"MY_ALPHA",{0,0,0}},
{"DEBUG_ACTIVATE",{0,0,0}},
{"SIZE_Y",{0,0,0}},
{"LINES",{0,0,0}},
{"SCOPE_1",{0,0,0}},
{"SCOPE_2",{0,0,0}},
{"TIME_SCOPE",{0,0,0}},
{"D3D_TEXDEPTH",{16384,0,0},true},
{"RECORD_MODE",{0,0,0},true},
};
struct NativeConstant {const char* name;std::int32_t integer;bool bonusOnly=false;};
static constexpr NativeConstant nativeConstants[]={
{"OFF",0},
{"ON",1},
{"EVENT_BLOCK",1},
{"EVENT_ENTITY",2},
{"EVENT_STUCK",3},
{"EVENT_PUSH",4},
{"EVENT_IMPACT",5},
{"EVENT_FRICTION",6},
{"EVENT_DETECT",7},
{"EVENT_SCAN",8},
{"EVENT_SHOOT",9},
{"EVENT_TRIGGER",10},
{"EVENT_SONAR",11},
{"EVENT_FRAME",12},
{"EVENT_TOUCH",16},
{"EVENT_RELEASE",17},
{"EVENT_CLICK",18},
{"EVENT_RIGHTCLICK",19},
{"EVENT_BUTTONUP",15},
{"EVENT_RECEIVE",13},
{"EVENT_DISCONNECT",20},
{"EVENT_JOIN",32},
{"EVENT_LEAVE",33},
{"EVENT_STRING",34},
{"EVENT_VAR",37},
{"CONNECT_SERVER",1},
{"CONNECT_CLIENT",2},
{"CONTENT_EMPTY",1},
{"CONTENT_PASSABLE",2},
{"CONTENT_SOLID",3},
{"IGNORE_ME",1},
{"IGNORE_YOU",2},
{"IGNORE_PASSABLE",4},
{"IGNORE_PASSENTS",8},
{"IGNORE_MODELS",16},
{"IGNORE_SPRITES",32},
{"IGNORE_CONTENT",64,true},
{"IGNORE_TERRAIN",256},
{"IGNORE_MAPS",128},
{"IGNORE_PUSH",512},
{"ACTIVATE_SHOOT",1024},
{"ACTIVATE_SONAR",2048},
{"ACTIVATE_TRIGGER",4096},
{"SCAN_TEXTURE",65536},
{"USE_BOX",8192},
{"GET_HITVERTEX",16384},
{"GLIDE",262144},
{"SV_VARS",1},
{"SV_INFO",2},
{"SV_STRINGS",4},
{"SV_SYS",4096},
{"SV_POINTERS",8},
{"SV_BMAPS",16},
{"SV_KEYS",64},
{"SV_FUNCTIONS",32},
{"SV_PANELS",1024},
{"SV_VIEWS",512},
{"SV_SKY",256},
{"SV_LEVEL",128},
{"SV_MATERIAL",8192},
{"SV_PATH",16384},
{"SV_STRUCT",32768},
{"SV_PHYSICS",131072},
{"SV_ALL",1048575},
{"SEND_VEC",1},
{"SEND_ALL",2},
{"SEND_RATE",4},
{"SEND_UNRELIABLE",8},
{"ANM_CYCLE",1},
{"ANM_ADD",2},
{"ANM_SKIP",4,true},
{"PH_RIGID",1},
{"PH_WAVE",2},
{"PH_BOX",0},
{"PH_SPHERE",1},
{"PH_CYLINDER",2},
{"PH_POLY",3},
{"PH_BALL",1},
{"PH_HINGE",2},
{"PH_SLIDER",3},
{"PH_WHEEL",4},
};
struct NativeMask {Object::Kind kind;const char* name;const char* backing;std::uint32_t mask;};
static constexpr NativeMask nativeMasks[]={
{Object::Kind::String,"INVISIBLE","flags",0x00000002u},
{Object::Kind::Entity,"BEAM","flags",0x00000200u},
{Object::Kind::Entity,"BLEND","flags",0x00200000u},
{Object::Kind::Entity,"BRIGHT","flags",0x00200000u},
{Object::Kind::Entity,"CAST","flags",0x00800000u},
{Object::Kind::Entity,"CLIENT","flags2",0x00000010u},
{Object::Kind::Entity,"DECAL","flags",0x00400000u},
{Object::Kind::Entity,"DYNAMIC","event_flags",0x00000001u},
{Object::Kind::Entity,"ENABLE_BLOCK","event_flags",0x00000002u},
{Object::Kind::Entity,"ENABLE_CLICK","event_flags",0x00040000u},
{Object::Kind::Entity,"ENABLE_DETECT","event_flags",0x00000080u},
{Object::Kind::Entity,"ENABLE_DISCONNECT","event_flags",0x00100000u},
{Object::Kind::Entity,"ENABLE_ENTITY","event_flags",0x00000004u},
{Object::Kind::Entity,"ENABLE_FRAME","event_flags",0x00001000u},
{Object::Kind::Entity,"ENABLE_FRICTION","event_flags",0x00000040u},
{Object::Kind::Entity,"ENABLE_IMPACT","event_flags",0x00000020u},
{Object::Kind::Entity,"ENABLE_PUSH","event_flags",0x00000010u},
{Object::Kind::Entity,"ENABLE_RECEIVE","event_flags",0x00002000u},
{Object::Kind::Entity,"ENABLE_RELEASE","event_flags",0x00020000u},
{Object::Kind::Entity,"ENABLE_RIGHTCLICK","event_flags",0x00080000u},
{Object::Kind::Entity,"ENABLE_SCAN","event_flags",0x00000100u},
{Object::Kind::Entity,"ENABLE_SHOOT","event_flags",0x00000200u},
{Object::Kind::Entity,"ENABLE_SONAR","event_flags",0x00000800u},
{Object::Kind::Entity,"ENABLE_STUCK","event_flags",0x00000008u},
{Object::Kind::Entity,"ENABLE_TOUCH","event_flags",0x00010000u},
{Object::Kind::Entity,"ENABLE_TRIGGER","event_flags",0x00000400u},
{Object::Kind::Entity,"FACING","flags",0x00002000u},
{Object::Kind::Entity,"FAT","flags2",0x00000002u},
{Object::Kind::Entity,"FLAG1","flags",0x00000001u},
{Object::Kind::Entity,"FLAG2","flags",0x00000002u},
{Object::Kind::Entity,"FLAG3","flags",0x00000004u},
{Object::Kind::Entity,"FLAG4","flags",0x00000008u},
{Object::Kind::Entity,"FLAG5","flags",0x00000010u},
{Object::Kind::Entity,"FLAG6","flags",0x00000020u},
{Object::Kind::Entity,"FLAG7","flags",0x00000040u},
{Object::Kind::Entity,"FLAG8","flags",0x00000080u},
{Object::Kind::Entity,"FLARE","flags",0x00008000u},
{Object::Kind::Entity,"INVISIBLE","flags",0x00000100u},
{Object::Kind::Entity,"LIGHT","flags",0x00080000u},
{Object::Kind::Entity,"LOCAL","flags2",0x00000004u},
{Object::Kind::Entity,"METAL","flags",0x00400000u},
{Object::Kind::Entity,"MOVE","flags",0x00000800u},
{Object::Kind::Entity,"NARROW","flags2",0x00000001u},
{Object::Kind::Entity,"NATIVE","flags2",0x00000008u},
{Object::Kind::Entity,"NEAR","flags",0x00004000u},
{Object::Kind::Entity,"NOFILTER","flags",0x00010000u},
{Object::Kind::Entity,"NOFOG","flags",0x00100000u},
{Object::Kind::Entity,"NOSEND","nosend_flags",0x00008000u},
{Object::Kind::Entity,"NOSEND_ALPHA","nosend_flags",0x00000100u},
{Object::Kind::Entity,"NOSEND_AMBIENT","nosend_flags",0x00000080u},
{Object::Kind::Entity,"NOSEND_ANGLES","nosend_flags",0x00000008u},
{Object::Kind::Entity,"NOSEND_COLOR","nosend_flags",0x00000400u},
{Object::Kind::Entity,"NOSEND_FLAGS","nosend_flags",0x00000040u},
{Object::Kind::Entity,"NOSEND_FRAME","nosend_flags",0x00000010u},
{Object::Kind::Entity,"NOSEND_LIGHT","nosend_flags",0x00000200u},
{Object::Kind::Entity,"NOSEND_ORIGIN","nosend_flags",0x00000004u},
{Object::Kind::Entity,"NOSEND_SCALE","nosend_flags",0x00000002u},
{Object::Kind::Entity,"NOSEND_SKIN","nosend_flags",0x00000020u},
{Object::Kind::Entity,"NOSEND_SOUND","nosend_flags",0x00001000u},
{Object::Kind::Entity,"NOSEND_TYPE","nosend_flags",0x00000001u},
{Object::Kind::Entity,"NOSEND_UV","nosend_flags",0x00000800u},
{Object::Kind::Entity,"ORIENTED","flags",0x00001000u},
{Object::Kind::Entity,"OVERLAY","flags",0x00001000u},
{Object::Kind::Entity,"PASSABLE","flags",0x00000200u},
{Object::Kind::Entity,"POLYGON","flags",0x04000000u},
{Object::Kind::Entity,"SHADOW","flags",0x00040000u},
{Object::Kind::Entity,"SPOTLIGHT","flags3",0x00000002u},
{Object::Kind::Entity,"STREAK","flags",0x00000100u},
{Object::Kind::Entity,"TRANSLUCENT","flags",0x00000400u},
{Object::Kind::Entity,"TRANSPARENT","flags",0x00000400u},
{Object::Kind::Entity,"UNLIT","flags",0x00020000u},
{Object::Kind::Entity,"UNTOUCHABLE","flags3",0x00000001u},
{Object::Kind::Entity,"UVSPEED","flags",0x00000800u},
{Object::Kind::Entity,"VISIBLE","flags3",0x00010000u},
{Object::Kind::Panel,"D3D","flags",0x00000001u},
{Object::Kind::Panel,"OVERLAY","flags",0x00001000u},
{Object::Kind::Panel,"REFRESH","flags",0x00000001u},
{Object::Kind::Panel,"TRANSLUCENT","flags",0x00000400u},
{Object::Kind::Panel,"TRANSPARENT","flags",0x00000400u},
{Object::Kind::Panel,"VISIBLE","flags",0x00004000u},
{Object::Kind::Text,"CENTER","flags",0x00000300u},
{Object::Kind::Text,"CENTER_X","flags",0x00000100u},
{Object::Kind::Text,"CENTER_Y","flags",0x00000200u},
{Object::Kind::Text,"CONDENSED","flags",0x00000002u},
{Object::Kind::Text,"D3D","flags",0x00000001u},
{Object::Kind::Text,"NARROW","flags",0x00002000u},
{Object::Kind::Text,"OUTLINE","flags",0x00080000u},
{Object::Kind::Text,"SHADOW","flags",0x00040000u},
{Object::Kind::Text,"TRANSLUCENT","flags",0x00000400u},
{Object::Kind::Text,"TRANSPARENT","flags",0x00000400u},
{Object::Kind::Text,"VISIBLE","flags",0x00004000u},
{Object::Kind::View,"AUDIBLE","flags",0x00008000u},
{Object::Kind::View,"CUBE","flags",0x00000004u},
{Object::Kind::View,"LINES","flags",0x00080000u},
{Object::Kind::View,"NOCULL","flags",0x00040000u},
{Object::Kind::View,"NOENTITIES","flags",0x00080000u},
{Object::Kind::View,"NOPARTICLE","flags",0x00800000u},
{Object::Kind::View,"NOSHADOW","flags",0x00400000u},
{Object::Kind::View,"PORTALCLIP","flags",0x00200000u},
{Object::Kind::View,"PORTALVISIBLE","flags",0x02000000u},
{Object::Kind::View,"REMEMBER","flags",0x00000002u},
{Object::Kind::View,"SKYVISIBLE","flags",0x00100000u},
{Object::Kind::View,"TRANSLUCENT","flags",0x00000400u},
{Object::Kind::View,"TRANSPARENT","flags",0x00000400u},
{Object::Kind::View,"VISIBLE","flags",0x00004000u},
{Object::Kind::Material,"ENABLE_RENDER","flags",0x00020000u},
{Object::Kind::Material,"ENABLE_VIEW","flags",0x00040000u},
{Object::Kind::Material,"TANGENT","flags",0x00010000u},
};
struct NativeKey {const char* name;const char* callback;int code;};
static constexpr NativeKey nativeKeys[]={
{"KEY_BKSP","ON_BKSP",14},
{"KEY_ENTER","ON_ENTER",28},
{"KEY_ESC","ON_ESC",1},
{"KEY_ALT","ON_ALT",56},
{"KEY_CTRL","ON_CTRL",29},
{"KEY_SHIFTL","ON_SHIFTL",42},
{"KEY_SHIFTR","ON_SHIFTR",54},
{"KEY_SPACE","ON_SPACE",57},
{"KEY_TAB","ON_TAB",15},
{"KEY_F1","ON_F1",59},
{"KEY_F2","ON_F2",60},
{"KEY_F3","ON_F3",61},
{"KEY_F4","ON_F4",62},
{"KEY_F5","ON_F5",63},
{"KEY_F6","ON_F6",64},
{"KEY_F7","ON_F7",65},
{"KEY_F8","ON_F8",66},
{"KEY_F9","ON_F9",67},
{"KEY_F10","ON_F10",68},
{"KEY_F11","ON_F11",87},
{"KEY_F12","ON_F12",88},
{"KEY_A","ON_A",30},
{"KEY_B","ON_B",48},
{"KEY_C","ON_C",46},
{"KEY_D","ON_D",32},
{"KEY_E","ON_E",18},
{"KEY_F","ON_F",33},
{"KEY_G","ON_G",34},
{"KEY_H","ON_H",35},
{"KEY_I","ON_I",23},
{"KEY_J","ON_J",36},
{"KEY_K","ON_K",37},
{"KEY_L","ON_L",38},
{"KEY_M","ON_M",50},
{"KEY_N","ON_N",49},
{"KEY_O","ON_O",24},
{"KEY_P","ON_P",25},
{"KEY_Q","ON_Q",16},
{"KEY_R","ON_R",19},
{"KEY_S","ON_S",31},
{"KEY_T","ON_T",20},
{"KEY_U","ON_U",22},
{"KEY_V","ON_V",47},
{"KEY_W","ON_W",17},
{"KEY_X","ON_X",45},
{"KEY_Y","ON_Y",21},
{"KEY_Z","ON_Z",44},
{"KEY_CUU","ON_CUU",72},
{"KEY_CUD","ON_CUD",80},
{"KEY_CUR","ON_CUR",77},
{"KEY_CUL","ON_CUL",75},
{"KEY_PGUP","ON_PGUP",73},
{"KEY_PGDN","ON_PGDN",81},
{"KEY_HOME","ON_HOME",71},
{"KEY_END","ON_END",79},
{"KEY_INS","ON_INS",82},
{"KEY_DEL","ON_DEL",83},
{"KEY_CAR","ON_CAR",52},
{"KEY_CAL","ON_CAL",51},
{"KEY_SLASH","ON_SLASH",53},
{"KEY_EQUALS","ON_EQUALS",13},
{"KEY_PERIOD","ON_PERIOD",52},
{"KEY_COMMA","ON_COMMA",51},
{"KEY_BRACKL","ON_BRACKL",26},
{"KEY_BRACKR","ON_BRACKR",27},
{"KEY_SEMIC","ON_SEMIC",39},
{"KEY_BKSL","ON_BKSL",43},
{"KEY_APOS","ON_APOS",40},
{"KEY_MINUSC","ON_MINUSC",12},
{"KEY_GRAVE","ON_GRAVE",41},
{"KEY_APO","ON_APO",13},
{"KEY_MINUS","ON_MINUS",53},
{"KEY_PLUS","ON_PLUS",27},
{"KEY_SZ","ON_SZ",12},
{"KEY_AE","ON_AE",40},
{"KEY_UE","ON_UE",26},
{"KEY_OE","ON_OE",39},
{"KEY_1","ON_1",2},
{"KEY_2","ON_2",3},
{"KEY_3","ON_3",4},
{"KEY_4","ON_4",5},
{"KEY_5","ON_5",6},
{"KEY_6","ON_6",7},
{"KEY_7","ON_7",8},
{"KEY_8","ON_8",9},
{"KEY_9","ON_9",10},
{"KEY_0","ON_0",11},
{"KEY_SCRLK","ON_SCRLK",70},
{"KEY_PAUSE","ON_PAUSE",69},
};

void worldFrameEvents(Engine&,Runtime&);
namespace {
void svc_flush(FileData&);
std::filesystem::path svc_private(const std::filesystem::path&,const std::filesystem::path&);
std::filesystem::path svc_relative(std::string_view);
std::vector<std::uint8_t> svc_read_file(const std::filesystem::path&);
const Image& svc_bitmap(Engine&,Value);
}
// A6.31 640x480..1600x1200 video modes (video_switch 6..10).
static constexpr std::array<std::array<int,2>,5> a6VideoModes{{{640,480},{800,600},{1024,768},{1280,1024},{1600,1200}}};
// A6 00443950 sizes emit() particles (PARTICLE+0x34 bit 0x2000000 clear) in screen pixels, not quants:
// px=((trunc(32768/depth)*size_raw)>>10)>>shift with shift=14-trunc((W-1)/320+.5) (0043fc10), W the view width.
// effect() particles use size*focal/depth, focal=W/(2*tan(arc/2)) with arc clamped 1..175 (0044e260).
// So a legacy size in quants is size*2^(15-shift)/focal. Pixel truncation of tiny sizes is not reproduced.
static double legacy_particle_quants(double arc,int width){
 const int shift=14-int((width-1)/320.0+.5);
 return std::ldexp(1.0,15-shift)*2*std::tan(std::clamp(std::abs(arc),1.0,175.0)*std::numbers::pi/360)/width;
}
static bool core_equal(std::string_view a,std::string_view b){return case_equal(a,b);}
static const NativeMask* core_mask(Object::Kind kind,std::string_view name,SemanticProfile profile){if(kind==Object::Kind::Particle)kind=Object::Kind::Entity;
 if(profile==SemanticProfile::BonusA7&&kind==Object::Kind::View&&core_equal(name,"NOENTITIES")){static constexpr NativeMask noEntities{Object::Kind::View,"NOENTITIES","flags",0x10000u};return &noEntities;}
 if(profile==SemanticProfile::BonusA7&&(kind==Object::Kind::Panel||kind==Object::Kind::Text)&&core_equal(name,"FILTER")){
  // A7 100015e0 descriptors: both UI kinds alias flags+0x1c, physical mask0x100000.
  static constexpr NativeMask panel{Object::Kind::Panel,"FILTER","flags",0x100000u},text{Object::Kind::Text,"FILTER","flags",0x100000u};
  return kind==Object::Kind::Panel?&panel:&text;
 }
 if(profile==SemanticProfile::BonusA7&&kind==Object::Kind::Entity){static constexpr NativeMask sonar{Object::Kind::Entity,"ENABLE_SONAR","event_flags",0x400u},trigger{Object::Kind::Entity,"ENABLE_TRIGGER","event_flags",0x200u};if(core_equal(name,sonar.name))return &sonar;if(core_equal(name,trigger.name))return &trigger;}auto i=std::lower_bound(std::begin(nativeMasks),std::end(nativeMasks),std::pair{kind,name},[](const NativeMask& m,const auto& key){return m.kind!=key.first?int(m.kind)<int(key.first):CaseLess{}(m.name,key.second);});return i!=std::end(nativeMasks)&&i->kind==kind&&core_equal(name,i->name)?i:nullptr;}
static std::string_view core_alias(Object::Kind kind,std::string_view n){if(kind==Object::Kind::Vector){if(core_equal(n,"pan")||core_equal(n,"blue"))return "x";if(core_equal(n,"tilt")||core_equal(n,"green"))return "y";if(core_equal(n,"roll")||core_equal(n,"red"))return "z";}if(kind!=Object::Kind::Particle)return n;if(core_equal(n,"pan"))return "vel_x";if(core_equal(n,"tilt"))return "vel_y";if(core_equal(n,"roll")||core_equal(n,"tex_scale"))return "vel_z";if(core_equal(n,"scale_z"))return "function";if(core_equal(n,"ambient"))return "gravity";return n;}
Ref State::member(ObjectId id,std::string_view name){auto& o=require(id);if(o.kind==Object::Kind::Text&&core_equal(name,"string"))return {nullptr,id,"string"};if(auto* m=core_mask(o.kind,name,profile))return {&o.field(m->backing),id,m->name,0,m->mask};
 const char* canonical=nullptr;if(core_equal(name,"pos"))canonical="x";else if(core_equal(name,"angle")||core_equal(name,"angles"))canonical="pan";else if(core_equal(name,"scale"))canonical="scale_x";else if(core_equal(name,"color"))canonical="blue";
 name=core_alias(o.kind,canonical?std::string_view(canonical):name);if(o.kind==Object::Kind::Vector){std::size_t component=core_equal(name,"x")?0:core_equal(name,"y")?1:core_equal(name,"z")?2:3;if(component<3){static constexpr const char* names[]={"x","y","z"};if(o.scalar&&component==0)return {o.scalar,id,"x"};if(!o.array.empty()){if(component>=o.array.size())throw std::runtime_error("vector component outside array");return {&o.array[component],id,names[component],component};}}}auto& slot=o.field(name);auto it=o.fields.find(name);return {&slot,id,it->first};}
Engine::Engine(Assets& a,Renderer* r,Media& m,State& s):assets(a),renderer(r),media(m),state(s),physics(*this){saveRoot=saves::root();std::filesystem::create_directories(saveRoot);initialize_globals();
 for(auto& slot:vectorTemps_){auto& object=state.create(Object::Kind::Vector);object.array.resize(3);slot=&object;}
}
void Engine::resolve_builtin_font(){
 auto font=global("_a4font"),sprite=global("_a4spr");if(!font||bitmapImages.contains(font.bits))return;
 assets.set_native_alpha_depth(global("D3D_ALPHADEPTH").real());
 auto image=assets.image_ptr("engine/_a4font.pcx");
 if(image->width!=192||image->height!=72)throw std::runtime_error("genuine _a4font must be a192x72 bitmap");
 bitmapImages.emplace(font.bits,image);if(sprite)bitmapImages.emplace(sprite.bits,std::move(image));
}
void Engine::configure_language(Runtime& runtime,std::string_view language,bool explicitChoice){
 language_=std::string(language);
 // The widescreen catalog protocol extends the original options record by one byte.
 if(!runtime.function("load_subtitle"))return;
 // Patch DilMenu/dil_degistir DD_Dil: 0 TR, 1 EN, 2 DE, 3 ES, 4 RU, 5 PL (byte 32 of options.k22).
 const std::uint8_t index=language=="tr"?0:language=="de"?2:language=="es"?3:language=="ru"?4:language=="pl"?5:1;
 set_global("DD_Dil",Value::from_int(index));
 std::vector<std::uint8_t> bytes;const auto destination=svc_private(saveRoot,"options.k22");
 if(std::filesystem::exists(destination)){
  const auto size=std::filesystem::file_size(destination);if(size!=31&&size!=32)throw std::runtime_error("unsupported native options.k22 record size");
  bytes.resize(size);std::ifstream in(destination,std::ios::binary);if(!in||!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size)))throw std::runtime_error("cannot read private options.k22");
 }else if(assets.contains("options.k22"))bytes=assets.read("options.k22");
 if(bytes.empty())return;
 if(bytes.size()!=31&&bytes.size()!=32)throw std::runtime_error("unsupported source options.k22 record size");
 bool changed=bytes.size()==31;if(changed)bytes.push_back(index);
 if(explicitChoice){bytes[31]=index;changed=true;}
 if(bytes[31]>5)throw std::runtime_error("invalid saved patch language index");
 if(changed){FileData file{std::move(bytes),0,destination,true};svc_flush(file);}
}
bool Engine::display_admitted(){
 if(!nativeDisplay_)return false;
 for(auto [name,id]:{std::pair{"GA_Cozunurlukler",displayResolution_},std::pair{"GA_ScaleOranlari",displayScale_},std::pair{"GA_ScaleOranlari2",displayScale2_},std::pair{"GA_FontOranlari",displayFont_}}){const auto* object=state.get(id);if(global(name).bits!=id||!object||!object->alive){nativeDisplay_=false;displayObjects_.clear();displayFills_.clear();return false;}}
 return true;
}
bool Engine::configure_display(Runtime& runtime,bool originalCampaignEntry,bool native,int width,int height,WindowMode mode){
 const bool continuing=display_admitted();
 if(continuing&&!native){auto restore=[&](ObjectId id,std::size_t at,std::initializer_list<double> values){auto& a=state.require(id).array;for(double v:values)a[at++].value=Value::from_double(v);};restore(displayResolution_,2*DisplayTier,{1024,768});restore(displayScale_,2*DisplayTier,{1.6,1.6});restore(displayScale2_,2*DisplayTier,{1,1});restore(displayFont_,DisplayTier,{1.611});set_global("GA_Cozunurluk",Value::from_int(authoredResolution_));}
 nativeDisplay_=false;if(!continuing||!native){displayObjects_.clear();displayFills_.clear();}
 if(!originalCampaignEntry||!native||state.profile!=SemanticProfile::CampaignA6||runtime.function("dbg_screen_reset"))return false;
 for(auto name:{"GA_AyarlariYukle","Menu_AcilisEkrani","AnaMenuAc","EM_Ayarla","GrafikMenuKapat","SmartMenuAc"})if(!runtime.function(name))return false;
 auto array=[&](std::string_view name,std::size_t count)->ObjectId{auto value=global(name);auto* object=state.get(value.bits);return value.kind==Value::Kind::Object&&object&&object->alive&&object->array.size()==count?object->id:0;};
 displayResolution_=array("GA_Cozunurlukler",10);displayScale_=array("GA_ScaleOranlari",10);displayScale2_=array("GA_ScaleOranlari2",10);displayFont_=array("GA_FontOranlari",4);
 if(!displayResolution_||!displayScale_||!displayScale2_||!displayFont_||!array("EM_Kordinat",54)||!array("EML_Paneller",27))return false;
 const auto& resolution=state.require(displayResolution_).array;
 constexpr std::array<int,10> authored{640,480,800,600,1024,768,1280,1024,1600,1200};
 // A continuing graph owns only the native tier slot; every other authored entry must remain intact.
 for(std::size_t i=0;i<authored.size();++i)if(!(continuing&&i/2==DisplayTier)&&resolution[i].value.integer()!=authored[i])return false;
 auto matches=[&](ObjectId id,const auto& values){const auto& a=state.require(id).array;const std::size_t stride=values.size()==4?1:2;for(std::size_t i=0;i<values.size();++i)if(!(continuing&&i/stride==DisplayTier)&&a[i].value.raw()!=Value::from_double(values[i]).raw())return false;return true;};
 if(!matches(displayScale_,std::array{1.0,1.0,1.25,1.25,1.6,1.6,2.0,2.133,2.5,2.5})||
    !matches(displayScale2_,std::array{1.0,1.0,1.25,1.25,1.0,1.0,1.25,1.334,1.563,1.563})||
    !matches(displayFont_,std::array{1.0,1.166,1.611,2.0}))return false;
 if(width<0||height<0||width>16384||height>16384||(width&&width<1280)||(height&&height<720))throw std::runtime_error("native resolution must be1280x720 through16384x16384, or unset");
 if(!width||!height||mode==WindowMode::Borderless){
  if(renderer){SDL_DisplayMode desktop{};if(SDL_GetDesktopDisplayMode(0,&desktop))throw std::runtime_error(std::string("cannot query native desktop: ")+SDL_GetError());width=desktop.w;height=desktop.h;}
  else if(!width||!height){width=currentWidth_;height=currentHeight_;}
 }
 if(width<1280||height<720||width>16384||height>16384)throw std::runtime_error("native display dimensions outside1280x720 through16384x16384");
 if(renderer){renderer->set_mode(width,height,mode);const auto metrics=renderer->metrics();width=metrics.width;height=metrics.height;}
 std::snprintf(displayLabel_.data(),displayLabel_.size(),"%dx%d",width,height);
 currentWindowMode=mode;windowStyle=mode==WindowMode::Borderless?1:mode==WindowMode::Windowed?16:0;currentWidth_=width;currentHeight_=height;if(!continuing){displayWidth_=width;displayHeight_=height;}
 set_vector(global_vector("screen_size"),{float(width),float(height),0});set_global("video_screen",Value::from_int(mode==WindowMode::Fullscreen?1:2));
 displayScrolls_={global("CRMT_Credit").bits,global("Son_yazi").bits,global("CRMT_Oyunsonu").bits};
 static constexpr std::array textNames{"VST_AltText","LYT_AltYazi","LYT_UstYazi","ozel_Altdiyalog","Gecici_AltYazi","LYT_OyunSonuYazi","CRMT_Oyunsonu","Son_yazi","Par_AltYaziText","Par_AltGorunText","Par_AltGormeText","Envanter_AltYazi","Buyu_GosterYazi","NBM_AltYazi","envanterload_text","intaktifload1_text","intaktifload2_text","YUMT_Buffer","t_Silah","t_Olay","NMT_AltYazi"};
 if(!continuing)for(ObjectId id:state.uiCandidates){auto* object=state.get(id);if(!object||!object->alive||(object->kind!=Object::Kind::Panel&&object->kind!=Object::Kind::Text))continue;
  auto is=[&](std::string_view name){return core_equal(object->name,name);};auto starts=[&](std::string_view prefix){return object->name.size()>=prefix.size()&&core_equal(std::string_view(object->name).substr(0,prefix.size()),prefix);};
  DisplayRole role=DisplayRole::Ordinary;int index=0;
  if(is("Menu_Acilis")||is("DMP_Arka")||is("PMP_Arka")||is("STMP_Arka")||is("EMP_Arka")||is("EMP_ArkaO")||is("loading_pn")||starts("Giris_Acilispnl")||is("kararma_fon_pnl")||is("videokara_fon_pnl")||is("siyah_fon_pnl")||is("LYP_Fade")||is("KBP_Fade"))role=DisplayRole::Fill;
  else if(is("grid_fon_pnl"))role=DisplayRole::Grid;
  else if(starts("genel_panel")||starts("kapi_panel")||starts("yon_oklari")||is("player_noktasi"))role=DisplayRole::MapPixel;
  else if(starts("Intro_Acilpnl"))role=DisplayRole::IntroTitle;
  else if(is("DMP_GozSag"))role=DisplayRole::EyeRight;
  else if(is("DMP_Gozsol"))role=DisplayRole::EyeLeft;
  else if(is("YUMT_Liste"))role=DisplayRole::LoadList;
  else if(is("YUMP_Cursor"))role=DisplayRole::LoadCursor;
  else if(is("CRMT_Credit"))role=DisplayRole::Credit;
  else if(starts("CRMP_Cerceve_"))role=DisplayRole::CreditFrame;
  else if(is("DOTP_Dokuman_text"))role=DisplayRole::Document;
  else if(starts("EMT_Cisim")&&object->name.size()==10){role=DisplayRole::InventoryLabel;index=object->name.back()-'0';}
  else if(is("oy_syh_pan1")||is("oy_syh_pan2"))role=DisplayRole::Bar;
  else if(is("BEP_Enerji"))role=DisplayRole::BossEnergy;
  else if(starts("BEP_"))role=DisplayRole::Boss;
  else if(starts("STMP_"))role=DisplayRole::Pause;
  else if(object->kind==Object::Kind::Panel&&(starts("AMP_")||starts("AYMP_")||starts("CMP_")||starts("YMP_")||starts("GMP_")||starts("SMP_")||starts("KSMP_")||starts("KONMP_")||starts("JKONMP_")||starts("YUMP_")))role=DisplayRole::Menu;
  else if(object->kind==Object::Kind::Panel&&(starts("EMP_")||starts("PEMP_")||starts("KVMP_")||starts("KOMP_"))){role=DisplayRole::Gameplay;index=starts("PEMP_")||starts("KVMP_");}
  else for(auto name:textNames)if(is(name)){role=DisplayRole::Text;break;}
  displayObjects_.push_back({id,role,index});
  if(role==DisplayRole::Fill)displayFills_.insert(id);
 }
 if(!continuing)authoredResolution_=std::uint8_t(number("GA_Cozunurluk"));
 nativeDisplay_=true;prepare_display();return true;
}
void Engine::prepare_display(){
 if(!display_admitted())return;
 const float width=float(currentWidth_),height=float(currentHeight_);auto ratio=[](float size,float basis){return float(Value::from_double(size/basis).real());};const float mx=ratio(width,1280),my=ratio(height,720),gx=ratio(width,640),gy=ratio(height,480),ax=ratio(width,1024),ay=ratio(height,768);
 const bool gameplay=number("LY_AktifLevelID")!=0&&number("level_yuklendi_ok")!=0&&number("game_over_start")==0&&number("STM_Acik")==0;
 // Original art rule (Kabusdismenu/YeniMenu/K22/PlayerEnerji/KoruView/kotu_koruyucu): GA_Cozunurluk>1 selects the
 // 1024-tier art drawn with GA_ScaleOranlari2 = resolution/1024x768; every native size is >=1280x720, i.e. that tier.
 auto pair=[&](ObjectId id,float x,float y){auto& a=state.require(id).array;a[2*DisplayTier].value=Value::from_double(x);a[2*DisplayTier+1].value=Value::from_double(y);};
 const float sourceScale2X=float(state.require(displayScale2_).array[2*DisplayTier].value.real());
 pair(displayResolution_,width,height);pair(displayScale_,gameplay?gx:mx,gameplay?gy:my);pair(displayScale2_,gameplay?ax:ratio(width,2048),gameplay?ay:ratio(height,1152));
 state.require(displayFont_).array[DisplayTier].value=Value::from_double(mx);
 set_global("GA_Cozunurluk",Value::from_int(DisplayTier));set_global("GM_CursorCozunurluk",Value::from_int(DisplayTier));
 // Text keeps the tier-0 font set scaled by GA_FontOranlari (patch font model, D16); only art follows the tier.
 for(auto name:{"LV_Fontlar","LV_EskiFontlar","LV_DokFontlar","timesfont_dizi","font_yukseklik_oran"}){auto value=global(name);auto* fonts=value.kind==Value::Kind::Object?state.get(value.bits):nullptr;if(fonts&&fonts->alive&&fonts->array.size()>DisplayTier)fonts->array[DisplayTier].value=fonts->array[0].value;}
 set_global("video_screen",Value::from_int(currentWindowMode==WindowMode::Fullscreen?1:2));
 const bool resized=displayWidth_!=currentWidth_||displayHeight_!=currentHeight_;
 if(resized)std::snprintf(displayLabel_.data(),displayLabel_.size(),"%dx%d",currentWidth_,currentHeight_);
 const float rx=width/displayWidth_,ry=height/displayHeight_;
 const float oldmx=resized?ratio(float(displayWidth_),1280):mx,oldmy=resized?ratio(float(displayHeight_),720):my;
 auto scale=[&](ObjectId id,float x,float y){set_field(id,"scale_x",x);set_field(id,"scale_y",y);};
 auto place=[&](ObjectId id,float x,float y){set_field(id,"pos_x",x);set_field(id,"pos_y",y);};
 float loadTop=height-170*my,loadBottom=height-10*my;displayLoadScroll_=0;displayLoadBudget_=0;
 const auto loadList=global("YUMT_Liste");const bool loadVisible=loadList&&field(loadList.bits,"visible");
 if(loadVisible){float cursorHeight=18;for(auto& entry:displayObjects_)if(entry.role==DisplayRole::LoadCursor){auto* cursor=state.get(entry.id);if(cursor&&cursor->alive){auto bitmap=cursor->fields.find("bmap");if(bitmap!=cursor->fields.end()&&bitmap->second.value){if(entry.bitmap!=bitmap->second.value.bits){const auto& image=svc_bitmap(*this,bitmap->second.value);entry.bitmap=bitmap->second.value.bits;entry.bitmapSize={float(image.width),float(image.height)};}cursorHeight=entry.bitmapSize.y;}}break;}
  const float body=std::max({160*my,144*mx+12*my,(126+cursorHeight)*mx+12*my}),available=height-20*my;
  loadTop=loadBottom-std::min(body,available);
  if(body>available){const float selected=std::clamp(float(number("YUM_Cursor")),0.0f,7.0f);displayLoadScroll_=std::max(0,int(std::ceil((18*selected*mx+std::max(18.0f,cursorHeight)*mx+12*my-available)/mx)));displayLoadBudget_=std::max(19,int((available-10*my)/mx)+1);}
  set_global("YUM_Y",Value::from_double(loadTop));
 }
 for(auto& entry:displayObjects_){auto* object=state.get(entry.id);if(!object||!object->alive)continue;const ObjectId id=entry.id;
  const float sourceBossScaleX=entry.role==DisplayRole::BossEnergy?float(field(id,"scale_x")):0;
  // Retain authored center/bottom offsets in the quantized menu scale domain.
  // Scaling cached physical pixels first truncates scale_y and drifts the anchor.
  if(resized&&entry.role==DisplayRole::Menu&&entry.menuPrepared){place(id,width/2+(float(field(id,"pos_x"))-displayWidth_/2.0f)*mx/oldmx,height+(float(field(id,"pos_y"))-displayHeight_)*my/oldmy);scale(id,mx,my);entry.menuY=float(field(id,"pos_y"));}
  else if(resized&&entry.role!=DisplayRole::MapPixel){place(id,float(field(id,"pos_x"))*rx,float(field(id,"pos_y"))*ry);scale(id,float(field(id,"scale_x"))*rx,float(field(id,"scale_y"))*ry);entry.menuY=float(field(id,"pos_y"));}
  auto bitmap_size=[&](Value bitmap){if(entry.bitmap!=bitmap.bits){const auto& image=svc_bitmap(*this,bitmap);entry.bitmap=bitmap.bits;entry.bitmapSize={float(image.width),float(image.height)};if(entry.role==DisplayRole::BossEnergy){auto* cam=state.get(global("BEP_Cam").bits);if(cam){auto found=cam->fields.find("bmap");if(found!=cam->fields.end()&&found->second.value)entry.bitmapSize.x=float(svc_bitmap(*this,found->second.value).width);}}}return entry.bitmapSize;};
  switch(entry.role){
   case DisplayRole::Grid:scale(id,gx,gy);break;
   case DisplayRole::Fill:{const auto bitmap=object->fields.find("bmap");if(field(id,"visible")&&bitmap!=object->fields.end()&&bitmap->second.value){const auto size=bitmap_size(bitmap->second.value);scale(id,width/size.x,height/size.y);place(id,0,0);}break;}
   case DisplayRole::IntroTitle:{const auto bitmap=object->fields.find("bmap");if(field(id,"visible")&&bitmap!=object->fields.end()&&bitmap->second.value){const auto size=bitmap_size(bitmap->second.value);scale(id,ax,ay);place(id,(width-size.x*ax)/2,(height-size.y*ay)/2);}break;}
   // Kabusdismenu.wdl gozsag/gozsol_pos_x/y[2]: 1024-tier eye frames in 1024x768 space.
   case DisplayRole::EyeRight:case DisplayRole::EyeLeft:{const bool right=entry.role==DisplayRole::EyeRight;scale(id,ax,ay);place(id,(right?662:327)*ax,(right?601:603)*ay);break;}
   case DisplayRole::CreditFrame:scale(id,mx,my);place(id,(width-400*mx)/2,(height-300*my)/2);break;
   case DisplayRole::Text:case DisplayRole::Document:case DisplayRole::InventoryLabel:case DisplayRole::Credit:case DisplayRole::LoadList:{
    scale(id,mx,entry.role==DisplayRole::LoadList?mx:my);
    if(core_equal(object->name,"VST_AltText"))set_field(id,"pos_y",height*15/16);
    if(entry.role==DisplayRole::Document)place(id,10*gx,10*gy);
    if(entry.role==DisplayRole::Credit){place(id,width/2,(height-300*my)/2+5*my);set_field(id,"size_y",290);}
    if(entry.role==DisplayRole::LoadList){place(id,(width-384*mx)/2+5*mx,loadTop+5*my);set_field(id,"size_y",std::max(290*my,144.0f));}
    break;
   }
   case DisplayRole::LoadCursor:scale(id,mx,mx);place(id,(width-384*mx)/2,loadTop+(float(number("YUM_Cursor"))*18-displayLoadScroll_)*mx+6*my);break;
   case DisplayRole::Bar:scale(id,gx,gy);break; // D10: patch cinematic.wdl bars W/640,H/480 instead of W/600,H/440.
   case DisplayRole::Boss:case DisplayRole::BossEnergy:{auto bitmap=object->fields.find("bmap");if(field(id,"visible")&&bitmap!=object->fields.end()&&bitmap->second.value){const auto size=bitmap_size(bitmap->second.value);float sx=ax;if(entry.role==DisplayRole::BossEnergy){sx=float(field(id,"scale_x"));if(sourceBossScaleX!=entry.bossScaleX&&sourceScale2X>0)sx=sourceBossScaleX*ax/sourceScale2X;}scale(id,sx,ay);entry.bossScaleX=float(field(id,"scale_x"));place(id,(width-size.x*ax)/2,218*gy+(height-size.y*ay)/2);}break;}
   case DisplayRole::Pause:{
    scale(id,mx,my);const float bottom=height-32*my-10;
    if(core_equal(object->name,"STMP_Devam")||core_equal(object->name,"STMP_AnaMenu"))place(id,(width-172*mx)/2+22*mx,bottom-(core_equal(object->name,"STMP_Devam")?32*my:0));
    else if(core_equal(object->name,"STMP_MenuSoru"))place(id,(width-344*mx)/2,bottom-60*my);
    else if(core_equal(object->name,"STMP_SoruEvet")||core_equal(object->name,"STMP_SoruHayir"))place(id,(width-340*mx)/2+(core_equal(object->name,"STMP_SoruHayir")?170*mx:0),bottom);
    break;
   }
   case DisplayRole::Menu:{
    const float sx=float(field(id,"scale_x")),sy=float(field(id,"scale_y"));
    const float y=float(field(id,"pos_y"));
    if(field(id,"visible")&&sx>0&&sy>0&&(!entry.menuPrepared||y!=entry.menuY||sx!=mx||sy!=my)){const float margin=entry.menuPrepared&&y==entry.menuY?0:16;place(id,width/2+(float(field(id,"pos_x"))-width/2)*mx/sx,height+(y-height+margin)*my/sy);scale(id,mx,my);entry.menuY=float(field(id,"pos_y"));entry.menuPrepared=true;}
    break;
   }
   case DisplayRole::Gameplay:{
    // index1: PlayerEnerji/KoruView HUD art follows GA_ScaleOranlari2 (1024 tier); others the 640 basis.
    const float sx=float(field(id,"scale_x")),sy=float(field(id,"scale_y")),tx=entry.index?ax:gx,ty=entry.index?ay:gy;
    if(sx>0&&sy>0&&(sx!=tx||sy!=ty)){place(id,float(field(id,"pos_x"))*tx/sx,float(field(id,"pos_y"))*ty/sy);scale(id,tx,ty);}
    break;
   }
   case DisplayRole::Ordinary:case DisplayRole::MapPixel:break;
  }
 }
 for(const auto& entry:displayObjects_)if(entry.role==DisplayRole::InventoryLabel&&entry.index>=0&&entry.index<8){static constexpr std::array names{"EMP_Nesne0","EMP_Nesne1","EMP_Nesne2","EMP_Nesne3","EMP_Nesne4","EMP_Nesne5","EMP_Nesne6","EMP_Nesne7"};auto panel=global(names[entry.index]);if(panel)place(entry.id,float(field(panel.bits,"pos_x")),float(field(panel.bits,"pos_y"))+34*gy);}
 if(resized){
  const bool credit=field(global("CRMT_Credit").bits,"visible")!=0;
  const float oldAnchor=credit?displayHeight_/2.0f:float(displayHeight_),anchor=credit?height/2:height;
  const float margin=number("DM_Acik")!=0&&!credit&&!loadVisible?16.0f:0;
  for(auto name:{"Cer_aX1","Cer_aX2","Cer_hX1","Cer_hX2"})set_global(name,Value::from_double(width/2+(number(name)-displayWidth_/2.0)*mx/oldmx));
  for(auto name:{"Cer_aY1","Cer_aY2","Cer_hY1","Cer_hY2"})set_global(name,Value::from_double(anchor+(number(name)-oldAnchor+margin)*my/oldmy-margin));
  set_global("GM_ParlakKor",Value::from_double(number("GM_ParlakKor")*rx));
 }
 if(loadVisible){set_global("Cer_hX1",Value::from_double((width-384*mx)/2));set_global("Cer_hX2",Value::from_double((width+384*mx)/2));set_global("Cer_hY1",Value::from_double(loadTop));set_global("Cer_hY2",Value::from_double(loadBottom));}
 // CikisMenu.wdl:47 bottom 480*GA_ScaleOranlari[y]-DM_AltPay equals screen_size.y-16 in every authored table; native's
 // H/720 menu scale breaks that identity (frame inverted above the question), so restore the authored bottom.
 if(field(global("CMP_Soru").bits,"visible"))set_global("Cer_hY2",Value::from_double(height-16));
 auto camera=global("camera");if(camera){set_field(camera.bits,"size_x",width);set_field(camera.bits,"size_y",height);}
 displayWidth_=currentWidth_;displayHeight_=currentHeight_;
}
float Engine::display_aspect(float authored)const{
 if(!nativeDisplay_||authored==0)return authored;
 // Original0044e260/0044cd00: screen-mode factor, not a patch-added Hor+ projection.
 const float modeFactor=currentWindowMode==WindowMode::Fullscreen?float(currentHeight_)*320/(float(currentWidth_)*240):1;
 return 1/(authored*modeFactor);
}
void Engine::update_first_person(){
 // Community patch 1.8.5 cam.wdl fps_camera_control on the original scripts: the patch guards every level camera
 // writer off and its starter owns CAMERA; here the pose is applied after all scripts. Demir(0)/Ebru(1) only, in a
 // level, controllable, unfrozen, CAMERA shown (Video_Cam cutscenes and freeze_mode keep their own cameras).
 static constexpr std::array<std::string_view,7> names{"x","y","z","pan","tilt","roll","arc"};
 const auto cameraValue=global("camera"),playerValue=global("player"),cutscene=global("Video_Cam");
 const ObjectId camera=cameraValue.kind==Value::Kind::Object?cameraValue.bits:0,player=playerValue.kind==Value::Kind::Object?playerValue.bits:0;
 const auto* actor=state.get(player);const int who=int(number("Aktif_Player"));
 const bool active=cameraMode==CameraMode::FirstPerson&&camera&&actor&&actor->alive&&entities.contains(player)&&(who==0||who==1)&&number("LY_AktifLevelID")!=0&&(number("oyuncu_pasif")==0||number("koruyucu_pasif")==0)&&global("freeze_mode").raw()==0&&field(camera,"visible")!=0&&!(cutscene.kind==Value::Kind::Object&&field(cutscene.bits,"visible")!=0);
 std::array<double,7> now{};if(camera)for(std::size_t i=0;i<names.size();++i)now[i]=field(camera,names[i]);
 if(!active){
  // Yield: fields the scripts did not rewrite since the last override return to their last script-authored values.
  if(firstPersonActive_&&camera)for(std::size_t i=0;i<names.size();++i)if(now[i]==firstPersonWritten_[i])set_field(camera,names[i],firstPersonScript_[i]);
  firstPersonActive_=false;return;
 }
 for(std::size_t i=0;i<names.size();++i)if(!firstPersonActive_||now[i]!=firstPersonWritten_[i])firstPersonScript_[i]=now[i];
 if(!firstPersonActive_)firstPersonTilt_=0; // The patch's F5 toggle inherited a level-camera tilt; a persistent setting starts level.
 const bool forward=number("Key_Ileri")!=0,aim=number("Key_SilahKaldir")==1,run=number("Key_Kosma")==1;
 double distance=2,height=who==0?35:30,low=-45;
 if(forward){distance=who==0?1:0;height=who==0?30:25;low=-30;}
 firstPersonTilt_=std::clamp(firstPersonTilt_,low,40.0);
 if(aim){distance=3;height=25;firstPersonTilt_=-5;}
 if(run&&forward){distance=-8;height=25;firstPersonTilt_=std::clamp(firstPersonTilt_,-25.0,40.0);}
 // ponytail: patch validate_view() is skipped; with |distance|<=8 the camera stays inside the player's own hull, so its
 // wall traces (from player.z+50, threshold dist_total<=3) cannot trigger. Port it if the distances ever grow.
 constexpr double radians=3.14159265358979323846/180;const double tilt3rd=5*radians,planar=std::cos(tilt3rd)*distance;
 const auto origin=position(player);double pan=field(player,"pan");
 const Vec3 eye{float(origin.x-std::cos(pan*radians)*planar),float(origin.y-std::sin(pan*radians)*planar),float(origin.z+height+std::sin(tilt3rd)*planar)};
 // Patch cam.wdl: pan-=8*mouse_force.x*time; tilt+=8*mouse_force.y*time, on the original A6 mouse_force (update_input).
 const auto force=vector(global_vector("mouse_force"));const double turn=8*number("time");
 pan-=turn*force.x;set_field(player,"pan",pan);
 firstPersonTilt_+=turn*force.y;double cameraPan=field(player,"pan"),cameraTilt=firstPersonTilt_;
 if(number("bombacam_salla_s")!=0){const double shake=number("bombacam_salla_m");cameraPan+=shake*(std::generate_canonical<double,32>(random)*10-5);cameraTilt+=shake*(std::generate_canonical<double,32>(random)*10-5);}
 const std::array<double,7> pose{eye.x,eye.y,eye.z,cameraPan,cameraTilt,0,70};
 for(std::size_t i=0;i<names.size();++i){set_field(camera,names[i],pose[i]);firstPersonWritten_[i]=field(camera,names[i]);}
 firstPersonActive_=true;
}
Value Engine::global(std::string_view n)const{auto i=state.globals.find(n);return i==state.globals.end()?Value{}:i->second.value;}
Argument Engine::global_vector(std::string_view n)const{auto it=state.numericGlobals.find(n);if(it!=state.numericGlobals.end())return {global(n),{state.require(it->second).scalar,it->second,"x"}};auto v=global(n);if(v.kind!=Value::Kind::Object)throw std::runtime_error("unknown vector global "+std::string(n));return {v,{}};}
double Engine::number(std::string_view n)const{return global(n).real();}
void Engine::set_global(std::string_view n,Value v){state.global(n).value=v;}
static bool core_physical(std::string_view n){return core_equal(n,"flags")||core_equal(n,"event_flags")||core_equal(n,"flags2")||core_equal(n,"flags3")||core_equal(n,"nosend_flags");}
double Engine::field(ObjectId id,std::string_view n)const{auto* o=state.get(id);if(!o)return 0;if(auto* m=core_mask(o->kind,n,state.profile)){auto it=o->fields.find(m->backing);return it!=o->fields.end()&&(it->second.value.bits&m->mask)?1:0;}n=core_alias(o->kind,n);if(o->scalar&&core_equal(n,"x"))return o->scalar->value.real();if(o->kind==Object::Kind::Vector&&!o->array.empty()){auto index=core_equal(n,"x")?0:core_equal(n,"y")?1:core_equal(n,"z")?2:3;if(index<3&&index<o->array.size())return o->array[index].value.real();}auto it=o->fields.find(n);return it==o->fields.end()?0:core_physical(n)?double(it->second.value.bits):it->second.value.real();}
void Engine::set_field(ObjectId id,std::string_view n,double v){if(core_physical(n)){if(v<0||v>double(UINT32_MAX))throw std::runtime_error("physical flags out of range");state.require(id).field(n).value=Value::from_raw(std::bit_cast<std::int32_t>(std::uint32_t(v)));}else state.member(id,n).set(Value::from_double(v));}
std::uint32_t Engine::flags(ObjectId id)const{return std::uint32_t(field(id,"flags"));}
std::uint32_t Engine::constant(std::string_view n)const{auto it=state.globals.find(n);if(it==state.globals.end())throw std::runtime_error("unknown engine constant "+std::string(n));return std::uint32_t(it->second.value.integer());}
FunctionId Engine::callback(ObjectId id,std::string_view n)const{auto* o=state.get(id);if(!o)return 0;auto it=o->fields.find(n);return it!=o->fields.end()&&it->second.value.kind==Value::Kind::Function?it->second.value.bits:0;}
Vec3 Engine::position(ObjectId id)const{return {float(field(id,"x")),float(field(id,"y")),float(field(id,"z"))};}
Vec3 Engine::angles(ObjectId id)const{return {float(field(id,"pan")),float(field(id,"tilt")),float(field(id,"roll"))};}
void Engine::set_position(ObjectId id,Vec3 v){set_field(id,"x",v.x);set_field(id,"y",v.y);set_field(id,"z",v.z);}
// A6 entity skills are contiguous floats (K22.exe 0043e390: entSkill41..44 at ENTITY+0x134..0x140, vecSkill41 passes +0x134 as a vector),
// so a vector argument my.skillN addresses skillN..skillN+2 (Yrt_Temp=Skill33 / Yrt_HedefPan=Skill36 in klasik_yara.wdl).
static std::array<const char*,3> core_vector_names(std::string_view f){if(core_equal(f,"pan")||core_equal(f,"angle")||core_equal(f,"angles"))return {"pan","tilt","roll"};if(core_equal(f,"scale")||core_equal(f,"scale_x"))return {"scale_x","scale_y","scale_z"};if(core_equal(f,"blue")||core_equal(f,"color"))return {"blue","green","red"};if(core_equal(f,"min_x"))return {"min_x","min_y","min_z"};if(core_equal(f,"max_x"))return {"max_x","max_y","max_z"};if(core_equal(f,"vel_x"))return {"vel_x","vel_y","vel_z"};
 if(f.size()>5&&core_equal(f.substr(0,5),"skill")){unsigned n=0;auto [end,error]=std::from_chars(f.data()+5,f.data()+f.size(),n);if(error==std::errc{}&&end==f.data()+f.size()&&n>=1&&n<=100){static const auto skills=[]{std::array<std::string,103> names;for(unsigned i=1;i<names.size();++i)names[i]="skill"+std::to_string(i);return names;}();return {skills[n].c_str(),skills[n+1].c_str(),skills[n+2].c_str()};}}
 return {"x","y","z"};}
Vec3 Engine::vector(const Argument& a)const{const auto value=a.current();const auto ref=value.kind==Value::Kind::Object?Ref{}:a.ref;ObjectId id=value.kind==Value::Kind::Object?value.bits:ref.owner;auto* o=state.get(id);if(!o)throw std::runtime_error("vector argument is not an object/reference");if(!o->array.empty()&&(o->kind==Object::Kind::Vector||core_equal(ref.field,"skill"))){if(o->array.size()<ref.index+3)throw std::runtime_error("vector array range");return {float(o->array[ref.index].value.real()),float(o->array[ref.index+1].value.real()),float(o->array[ref.index+2].value.real())};}auto n=core_vector_names(ref.field);return {float(field(id,n[0])),float(field(id,n[1])),float(field(id,n[2]))};}
void Engine::set_vector(const Argument& a,Vec3 v){const auto value=a.current();const auto ref=value.kind==Value::Kind::Object?Ref{}:a.ref;ObjectId id=value.kind==Value::Kind::Object?value.bits:ref.owner;auto& o=state.require(id);if(!o.array.empty()&&(o.kind==Object::Kind::Vector||core_equal(ref.field,"skill"))){if(o.array.size()<ref.index+3)throw std::runtime_error("vector array range");o.array[ref.index].value=Value::from_double(v.x);o.array[ref.index+1].value=Value::from_double(v.y);o.array[ref.index+2].value=Value::from_double(v.z);return;}auto n=core_vector_names(ref.field);set_field(id,n[0],v.x);set_field(id,n[1],v.y);set_field(id,n[2],v.z);}
std::string_view Engine::string(Value v)const{if(!v.bits)return {};if(v.kind!=Value::Kind::Object&&v.kind!=Value::Kind::NativeText)throw std::runtime_error("expected string object/native bytes");auto& o=const_cast<State&>(state).require(v.bits);if(!o.alive)throw std::runtime_error("dead STRING/native bytes");if(o.nullStringBuffer)throw std::runtime_error("read from NULL STRING buffer");return o.text;}
std::string& Engine::mutable_string(const Argument& a){auto v=a.current();if(v.kind!=Value::Kind::Object)throw std::runtime_error("expected mutable STRING object");auto& o=state.require(v.bits);if(!o.alive||o.kind!=Object::Kind::String)throw std::runtime_error("expected live mutable STRING object");return o.text;}
Value Engine::new_string(std::string s){return state.make_string(std::move(s));}
void Engine::initialize_globals(){for(auto& d:nativeDefaults){if(d.bonusOnly&&state.profile!=SemanticProfile::BonusA7)continue;const char* name=state.profile==SemanticProfile::BonusA7&&core_equal(d.name,"POINTER")?"MOUSE_CURSOR":d.name;auto& slot=state.global(name);slot.value=Value::from_raw(d.words[0]);auto& o=state.create(Object::Kind::Vector,name);o.scalar=&slot;o.field("y").value=Value::from_raw(d.words[1]);o.field("z").value=Value::from_raw(d.words[2]);state.numericGlobals.emplace(name,o.id);}for(auto& c:nativeConstants){if(c.bonusOnly&&state.profile!=SemanticProfile::BonusA7)continue;set_global(c.name,Value::from_int(c.integer));state.compilerMacros.emplace(c.name,std::to_string(c.integer));}
 // Bonus row101abb80/public101e7390 defaults100; fixed-device capacity is a separate clamp.
 if(state.profile==SemanticProfile::BonusA7)set_global("MAX_LIGHTS",Value::from_int(100));
 auto& camera=state.create(Object::Kind::View,"camera");set_global("camera",Value::object(camera.id));for(auto n:{"x","y","z","pan","tilt","roll","pos_x","pos_y","layer","flags"})camera.field(n);set_field(camera.id,"visible",1);set_field(camera.id,"audible",1);set_field(camera.id,"arc",75);set_field(camera.id,"clip_near",1);set_field(camera.id,"clip_far",10000);set_field(camera.id,"size_x",1280);set_field(camera.id,"size_y",720);
 // The native embedded engine script declares persistent VAR temp[5], not a temporary vector ring.
 auto& temp=state.create(Object::Kind::Vector,"temp");temp.array.resize(5);set_global("temp",Value::object(temp.id));
 auto& font=state.create(Object::Kind::Font,"_a4font");font.text="engine/_a4font.pcx";set_field(font.id,"style",6);set_field(font.id,"size",9);set_global("_a4font",Value::object(font.id));
 auto& sprite=state.create(Object::Kind::Bmap,"_a4spr");sprite.text=font.text;set_global("_a4spr",Value::object(sprite.id));
 // Native 00415ad0/00415390 register the actual game's built-in MATERIALs before WDL (birth: map255, alpha100, cycle480).
 // The mat_model/terrain/sprite/unlit/metal defaults are bound by A6 0042fa20; Bonus registers only mat_particle here.
 struct BuiltinMaterial{const char* name;int ambient,diffuse,specular,emissive,albedo,power;};
 static constexpr BuiltinMaterial builtinMaterials[]{{"mat_particle",200,0,0,0,0,0},{"mat_model",200,255,0,0,50,0},{"mat_terrain",200,255,0,0,50,0},{"mat_sprite",200,128,0,0,0,0},{"mat_unlit",0,0,0,128,0,0},{"mat_metal",100,100,200,0,50,4}};
 for(const auto& builtin:builtinMaterials){
  if(state.profile!=SemanticProfile::CampaignA6&&builtin.name!=std::string_view("mat_particle"))continue;
  auto& material=state.create(Object::Kind::Material,builtin.name);set_global(builtin.name,Value::object(material.id));
  for(auto [prefix,value]:{std::pair{"ambient",builtin.ambient},{"diffuse",builtin.diffuse},{"specular",builtin.specular},{"emissive",builtin.emissive},{"map",255}})for(auto channel:{"blue","green","red"})material.field(std::string(prefix)+"_"+channel).value=Value::from_int(value);
  for(auto [name,value]:{std::pair{"albedo",builtin.albedo},{"power",builtin.power},{"alpha",100},{"cycle",480},{"scale1",0},{"scale2",0},{"flags",0}})material.field(name).value=Value::from_int(value);
  for(auto name:{"effect","skin1","skin2","skin3","skin4"})material.field(name).value=Value::object(0);
  material.field("event").value=Value::function(0);
 }
 // Both source initializers register this live object; authored declarations may replace its binding.
 if(const auto existing=global("mat_shadow");!existing||!state.get(existing.bits)||!state.get(existing.bits)->alive){
  auto& shadowMaterial=state.create(Object::Kind::Material,"mat_shadow");set_global("mat_shadow",Value::object(shadowMaterial.id));
  for(auto prefix:{"ambient","diffuse","specular","emissive"})for(auto channel:{"blue","green","red"})shadowMaterial.field(std::string(prefix)+"_"+channel).value={};
  for(auto name:{"power","albedo","flags"})shadowMaterial.field(name).value={};
  shadowMaterial.field("alpha").value=Value::from_int(50);
 }
 set_global("delimit_str",new_string(","));
 // A7 10025d72 constructs a live empty offline STRING borrowing static bytes (flags0x24).
 // Native globals instead retain an owned STRING through the existing Value lifetime.
 if(state.profile==SemanticProfile::BonusA7)set_global("PLAYER_NAME",new_string(""));
 auto callbackGlobal=[&](std::string_view name)->Slot&{auto& slot=state.global(name);slot.callback=true;return slot;};
 set_global("null",{});for(auto name:{"on_close","on_anykey","on_click","on_mouse_stop","on_load","on_join","on_leave","on_string","render_sky","render_world","render_entities","on_server","on_client","on_d3d_lost","on_d3d_reset","my_action"})callbackGlobal(name);for(auto name:{"key_any","_arg1","my_map"})state.global(name);if(state.profile==SemanticProfile::BonusA7)callbackGlobal("on_level");state.compilerMacros.emplace("BG_COLOR","SKY_COLOR");state.compilerMacros.emplace("MY_FUNCTION","MY_ACTION");
 if(state.profile==SemanticProfile::BonusA7){set_global("mouse_map",Value::object(0));state.compilerMacros.emplace("POINTER","MOUSE_CURSOR");set_global("passable_ent",Value::object(0));set_global("terrain_chunk",Value::from_int(32));set_global("terrain_lod",{});set_global("terrain_cell_size",Value::from_int(10));} // 1000e260 / source HMP and nullable cursor registry.
 for(auto& key:nativeKeys){auto code=std::size_t(key.code);if(!keyboard_[code]){keyboard_[code]=&state.global(key.name);keyboardCallbacks_[code]=&callbackGlobal(key.callback);}else{for(auto& prior:nativeKeys)if(prior.code==key.code){if(!core_equal(prior.name,key.name))state.compilerMacros.emplace(key.name,prior.name);if(!core_equal(prior.callback,key.callback))state.compilerMacros.emplace(key.callback,prior.callback);break;}}}
 for(std::size_t j=0;j<2;++j)for(std::size_t b=0;b<12;++b){auto n=std::to_string(b+1);joystick_[j][b]=&state.global((j?"JOY2_":"JOY_")+n);joystickCallbacks_[j][b]=&callbackGlobal((j?"ON_JOY2_":"ON_JOY")+n);}constexpr const char* mouseNames[]={"MOUSE_LEFT","MOUSE_RIGHT","MOUSE_MIDDLE"};constexpr const char* mouseOn[]={"ON_MOUSE_LEFT","ON_MOUSE_RIGHT","ON_MOUSE_MIDDLE"};for(std::size_t i=0;i<3;++i){mouse_[i]=&state.global(mouseNames[i]);mouseCallbacks_[i]=&callbackGlobal(mouseOn[i]);}
 for(auto& m:nativeMasks)if(m.kind==Object::Kind::Entity&&core_equal(m.backing,"flags"))state.global(m.name).value=Value::from_raw(std::bit_cast<std::int32_t>(m.mask));
 // Declaration-only flags have kind-specific meanings; general masks common to A6 UI.
 for(auto [n,v]:{std::pair{"VISIBLE",0x4000},std::pair{"REFRESH",1},std::pair{"D3D",1},std::pair{"CENTER_X",0x100},std::pair{"CENTER_Y",0x200},std::pair{"CENTER",0x300},std::pair{"LOOP",1},std::pair{"ANM_CYCLE",1},std::pair{"ANM_ADD",2}})state.global(n).value=Value::from_int(v);
 // Native compatibility lowering, not an original numeric DEFINE: preserve the keyword
 // and .filter member spelling while composite FLAGS expressions resolve raw0x100000.
 if(state.profile==SemanticProfile::BonusA7){set_global("FILTER",Value::from_int(1024));state.compilerMacros.emplace("FILTER","FILTER");}
 set_global("D3D_SHADERVERSION",Value::from_int(renderer?renderer->shader_version():0));
 if(renderer){auto m=renderer->metrics();currentWidth_=m.width;currentHeight_=m.height;}else{currentWidth_=1280;currentHeight_=720;}set_vector(global_vector("screen_size"),{float(currentWidth_),float(currentHeight_),0});set_global("video_mode",Value::from_int(7));}
EntityData load_entity_asset(Assets&,std::string_view,std::uint32_t& spriteFrames);
EntityData& Engine::register_entity(ObjectId id){
 auto* object=state.get(id);if(!object||!object->alive||object->kind!=Object::Kind::Entity)throw std::runtime_error("entity registration requires a live entity");
 auto found=entities.find(id);if(found!=entities.end())return found->second;
 std::string file=object->text;for(auto name:{"file","type"}){auto it=object->fields.find(name);if(it!=object->fields.end()&&it->second.value.kind==Value::Kind::Object)file=std::string(string(it->second.value));}
 if(!file.empty()){assets.initialize_native_lightmap_tables(global("GAMMA").raw(),global("D3D_MONOCHROME").bits!=0,renderer!=nullptr);assets.set_native_alpha_depth(global("D3D_ALPHADEPTH").real());assets.set_native_shadow_depth(global("D3D_SHADOWDEPTH").real(),renderer!=nullptr);}
 std::uint32_t spriteFrames;EntityData data=load_entity_asset(assets,file,spriteFrames);
 // A7 WDL ENTITY finalizer 1001e850 marks declaration-only/view-space geometry.
 data.physicsGeometry=false;
 for(auto name:{"scale_x","scale_y","scale_z"})if(!object->fields.contains(name))set_field(id,name,1);
 for(auto name:{"alpha","albedo"})if(!object->fields.contains(name))set_field(id,name,100);
 for(auto name:{"blue","green","red"})if(!object->fields.contains(name))set_field(id,name,128); // 004347b0 / 1003ac80 birth; explicit declarations override.
 if(!object->fields.contains("frame"))set_field(id,"frame",1);set_field(id,"sprite_frames",spriteFrames);
 if(data.model){auto lo=data.model->min,hi=data.model->max;for(auto [name,value]:{std::pair{"min_x",lo.x},{"min_y",lo.y},{"min_z",lo.z},{"max_x",hi.x},{"max_y",hi.y},{"max_z",hi.z}})if(!object->fields.contains(name))set_field(id,name,value);}
 if(state.profile==SemanticProfile::BonusA7&&data.type==5&&data.model)prepare_query_geometry(id,data);
 return entities.emplace(id,std::move(data)).first->second;
}
void Engine::prepare(Runtime& runtime){ensure_font_palette(runtime.compiler_settings().entry);assets.initialize_native_lightmap_tables(global("GAMMA").raw(),global("D3D_MONOCHROME").bits!=0,renderer!=nullptr);const auto count=state.objects.size();for(std::size_t i=0;i<count;++i){auto& object=state.objects[i];if(!object.alive||object.kind!=Object::Kind::Entity||entities.contains(object.id))continue;ObjectId id=object.id;register_entity(id);auto fn=callback(id,"action");if(fn){if(state.profile==SemanticProfile::BonusA7)runtime.enqueue(fn,{},id,0);else runtime.start(fn,{},id,0);}}if(state.profile==SemanticProfile::BonusA7)for(std::size_t i=0;i<count;++i){const auto& object=state.objects[i];if(object.alive&&object.kind==Object::Kind::Entity)initialize_query_actor(object.id);}make_frame();}
int legacy_scancode(int code){static constexpr int codes[]={SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_1,SDL_SCANCODE_2,SDL_SCANCODE_3,SDL_SCANCODE_4,SDL_SCANCODE_5,SDL_SCANCODE_6,SDL_SCANCODE_7,SDL_SCANCODE_8,SDL_SCANCODE_9,SDL_SCANCODE_0,SDL_SCANCODE_MINUS,SDL_SCANCODE_EQUALS,SDL_SCANCODE_BACKSPACE,SDL_SCANCODE_TAB,SDL_SCANCODE_Q,SDL_SCANCODE_W,SDL_SCANCODE_E,SDL_SCANCODE_R,SDL_SCANCODE_T,SDL_SCANCODE_Y,SDL_SCANCODE_U,SDL_SCANCODE_I,SDL_SCANCODE_O,SDL_SCANCODE_P,SDL_SCANCODE_LEFTBRACKET,SDL_SCANCODE_RIGHTBRACKET,SDL_SCANCODE_RETURN,SDL_SCANCODE_LCTRL,SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_D,SDL_SCANCODE_F,SDL_SCANCODE_G,SDL_SCANCODE_H,SDL_SCANCODE_J,SDL_SCANCODE_K,SDL_SCANCODE_L,SDL_SCANCODE_SEMICOLON,SDL_SCANCODE_APOSTROPHE,SDL_SCANCODE_GRAVE,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_BACKSLASH,SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_C,SDL_SCANCODE_V,SDL_SCANCODE_B,SDL_SCANCODE_N,SDL_SCANCODE_M,SDL_SCANCODE_COMMA,SDL_SCANCODE_PERIOD,SDL_SCANCODE_SLASH,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_KP_MULTIPLY,SDL_SCANCODE_LALT,SDL_SCANCODE_SPACE,SDL_SCANCODE_CAPSLOCK,SDL_SCANCODE_F1,SDL_SCANCODE_F2,SDL_SCANCODE_F3,SDL_SCANCODE_F4,SDL_SCANCODE_F5,SDL_SCANCODE_F6,SDL_SCANCODE_F7,SDL_SCANCODE_F8,SDL_SCANCODE_F9,SDL_SCANCODE_F10,SDL_SCANCODE_NUMLOCKCLEAR,SDL_SCANCODE_SCROLLLOCK,SDL_SCANCODE_HOME,SDL_SCANCODE_UP,SDL_SCANCODE_PAGEUP,SDL_SCANCODE_KP_MINUS,SDL_SCANCODE_LEFT,SDL_SCANCODE_KP_5,SDL_SCANCODE_RIGHT,SDL_SCANCODE_KP_PLUS,SDL_SCANCODE_END,SDL_SCANCODE_DOWN,SDL_SCANCODE_PAGEDOWN,SDL_SCANCODE_INSERT,SDL_SCANCODE_DELETE};if(code>=0&&code<int(std::size(codes)))return codes[code];switch(code){case 87:return SDL_SCANCODE_F11;case 88:return SDL_SCANCODE_F12;case 156:return SDL_SCANCODE_KP_ENTER;case 157:return SDL_SCANCODE_RCTRL;case 181:return SDL_SCANCODE_KP_DIVIDE;case 184:return SDL_SCANCODE_RALT;case 199:return SDL_SCANCODE_HOME;case 200:return SDL_SCANCODE_UP;case 201:return SDL_SCANCODE_PAGEUP;case 203:return SDL_SCANCODE_LEFT;case 205:return SDL_SCANCODE_RIGHT;case 207:return SDL_SCANCODE_END;case 208:return SDL_SCANCODE_DOWN;case 209:return SDL_SCANCODE_PAGEDOWN;case 210:return SDL_SCANCODE_INSERT;case 211:return SDL_SCANCODE_DELETE;default:return SDL_SCANCODE_UNKNOWN;}}
static bool core_key_bits(const std::bitset<SDL_NUM_SCANCODES>& bits,int code){auto scan=legacy_scancode(code);bool result=scan!=SDL_SCANCODE_UNKNOWN&&bits[scan];int alias=SDL_SCANCODE_UNKNOWN;switch(code){case 28:alias=SDL_SCANCODE_KP_ENTER;break;case 29:alias=SDL_SCANCODE_RCTRL;break;case 56:alias=SDL_SCANCODE_RALT;break;case 53:alias=SDL_SCANCODE_KP_DIVIDE;break;case 71:alias=SDL_SCANCODE_KP_7;break;case 72:alias=SDL_SCANCODE_KP_8;break;case 73:alias=SDL_SCANCODE_KP_9;break;case 75:alias=SDL_SCANCODE_KP_4;break;case 77:alias=SDL_SCANCODE_KP_6;break;case 79:alias=SDL_SCANCODE_KP_1;break;case 80:alias=SDL_SCANCODE_KP_2;break;case 81:alias=SDL_SCANCODE_KP_3;break;case 82:alias=SDL_SCANCODE_KP_0;break;case 83:alias=SDL_SCANCODE_KP_PERIOD;break;case 86:alias=SDL_SCANCODE_NONUSBACKSLASH;break;default:break;}return result||(alias!=SDL_SCANCODE_UNKNOWN&&bits[alias]);}
// The d-pad doubles as the arrow keys in both controller modes. A/B → Enter/Esc only in modern mode: in original mode they
// are raw JOY_1/JOY_2, the shipped aim/fire (GA_JTuslar), and Enter while aiming reloads instead (oyuncu2.wdl:2529).
static int core_gamepad_key(int code,bool modern){switch(code){case 28:return modern?SDL_CONTROLLER_BUTTON_A:-1;case 1:return modern?SDL_CONTROLLER_BUTTON_B:-1;case 72:return SDL_CONTROLLER_BUTTON_DPAD_UP;case 80:return SDL_CONTROLLER_BUTTON_DPAD_DOWN;case 75:return SDL_CONTROLLER_BUTTON_DPAD_LEFT;case 77:return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;default:return -1;}}
Value Engine::key_state(int code)const{if(code<0||code>=127)return {};bool down=core_key_bits(input.keys,code)||core_key_bits(input.pressed,code);auto pad=core_gamepad_key(code,controller.modern);if(pad>=0)down=down||input.controllerButtons[pad]||input.controllerPressed[pad];
 // Patch 1.8.5 YukleMenu.wdl: the load list also takes JOY_Button[GA_JTuslar[1]]/[2] as Enter/Esc and JOY_Ileri/Geri as
 // Up/Down. Offered only in modern controller mode; original mode keeps the ISO keyboard-only list.
 if(!down&&controller.modern&&(code==28||code==1||code==72||code==80)&&field(global("YUMT_Liste").bits,"visible")!=0){
  auto array=[&](std::string_view name)->const Object*{auto v=global(name);return v.kind==Value::Kind::Object?state.get(v.bits):nullptr;};
  if(code==72||code==80)down=number(code==72?"JOY_Ileri":"JOY_Geri")!=0;
  else if(auto* map=array("GA_JTuslar"),*buttons=array("JOY_Button");map&&buttons&&map->array.size()>2){const auto b=map->array[code==28?1:2].value.integer();down=b>=0&&std::size_t(b)<buttons->array.size()&&buttons->array[std::size_t(b)].value.bits!=0;}
 }
 return Value::from_int(down);}
void Engine::update_input(Runtime& runtime){auto edge=[&](Value fn,int code){if(fn.kind!=Value::Kind::Function||!fn.bits)return;auto prior=global("_arg1");set_global("_arg1",Value::from_int(code));std::array<Argument,1> args{{{Value::from_int(code),{}}}};runtime.start(fn.bits,args,0,0);set_global("_arg1",prior);};auto anykey=[&](int code){edge(global("on_anykey"),code);};
 if(state.profile==SemanticProfile::BonusA7){
  set_vector(global_vector("mouse_cursor"),{input.mouseX,input.mouseY,0});
  const bool valid=(input.mouseX>=0)&&(input.mouseY>=0)&&(input.mouseX<currentWidth_)&&(input.mouseY<currentHeight_);
  set_global("mouse_valid",Value::from_int(valid));
  if(global("mouse_mode").raw()>=3072)set_vector(global_vector("mouse_pos"),{input.mouseX,input.mouseY,0});
 }
 for(int code=0;code<127;++code)if(keyboard_[code])keyboard_[code]->value=key_state(code);
 set_global("key_any",Value::from_int(input.keys.any()||input.pressed.any()));for(int code=0;code<127;++code){auto pad=core_gamepad_key(code,controller.modern);if(core_key_bits(input.pressed,code)||(pad>=0&&input.controllerPressed[pad])){if(keyboardCallbacks_[code])edge(keyboardCallbacks_[code]->value,code);anykey(code);}}
 constexpr int mouseButtons[]={SDL_BUTTON_LEFT,SDL_BUTTON_RIGHT,SDL_BUTTON_MIDDLE};for(std::size_t i=0;i<3;++i){auto b=mouseButtons[i];mouse_[i]->value=Value::from_int(input.mouseButtons[b]||input.mousePressed[b]);if(input.mousePressed[b]){edge(mouseCallbacks_[i]->value,280+int(i));anykey(280+int(i));}}
 if(state.profile==SemanticProfile::CampaignA6)set_vector(global_vector("mouse_pos"),{input.mouseX,input.mouseY,0});
 // A6 K22.exe 00422000 (A7 acknex.dll 10027844 identical, constants 10.0/50.0): mickeys scaled by MOUSE_SENSE*10/max(raw
 // TIME_FRAME,50), plus the previous frame's scaled mickeys, y up, truncated, clamped to +-2*MOUSE_SENSE; .z never written.
 // Skipped (force kept) in MOUSE_MODE>=2. ponytail: WINDOW_FOCUS gate omitted; unfocused SDL windows deliver no motion.
 if(global("mouse_mode").raw()<0x800){
  // x87: scale stored as float; product and sum in extended precision; the prior product stored as float.
  const std::int32_t sense=global("mouse_sense").raw();const float frame=float(global("time_frame").raw()),k=float(sense)*10.0f/(frame<=50?50.0f:frame);
  const double x=double(input.mouseDX)*k,y=-(double(input.mouseDY)*k);
  auto limit=[&](double v){auto r=std::int32_t(v);return Value::from_raw(r< -2*sense?-2*sense:r>2*sense?2*sense:r).real();};
  const auto force=global_vector("mouse_force");
  set_vector(force,{float(limit(x+mouseForcePrior_[0])),float(limit(y+mouseForcePrior_[1])),vector(force).z});
  mouseForcePrior_={float(x),float(y)};
 }
 set_global("mouse_moving",Value::from_int(input.mouseDX!=0||input.mouseDY!=0));
 if(number("enable_mouse")!=0&&number("mouse_moving")==0){auto stop=global("on_mouse_stop");if(stop.kind==Value::Kind::Function&&stop.bits)runtime.start(stop.bits,{},0,0);}
 for(std::size_t j=0;j<2;++j)for(std::size_t b=0;b<12;++b){joystick_[j][b]->value=Value::from_int(input.joystickButtons[j][b]||input.joystickPressed[j][b]);if(input.joystickPressed[j][b]){edge(joystickCallbacks_[j][b]->value,256+int(b));anykey(256+int(j)*12+int(b));}}
 // A6 00422000: JOY_RAW/JOY_ROT = (pos*255)/range-255 (-255..255); JOY_FORCE.x/y = trunc(deadzone32(raw)*JOY_SENSE*0.02f), y negated,
 // clamped to +-2*JOY_SENSE (full stick 2.0, so Joy_Takip's JOY_Ileri=joy_force.y/2 reaches 1); JOY_FORCE.z is never written.
 const auto a6Axis=[](float a){return float(std::clamp(int(std::floor(a*255.0f)),-255,255));};
 const auto a6Force=[&](float a,bool negate){const int r=int(a6Axis(a)),dz=r>32?r-32:r<-32?r+32:0;const std::int64_t sense=global("JOY_SENSE").raw();
  const double v=double(float(dz))*double(float(float(sense)*0.02f));return float(std::clamp<std::int64_t>(std::int64_t(negate?-v:v),-2*sense,2*sense))/1024;};
 set_global("num_joysticks",Value::from_int(int(input.joystickConnected[0])+int(input.joystickConnected[1])));set_vector(global_vector("joy_raw"),{a6Axis(input.joystickAxes[0][0]),a6Axis(input.joystickAxes[0][1]),a6Axis(input.joystickAxes[0][2])});set_vector(global_vector("joy2_raw"),{a6Axis(input.joystickAxes[1][0]),a6Axis(input.joystickAxes[1][1]),a6Axis(input.joystickAxes[1][2])});
 set_vector(global_vector("joy_force"),controller.modern?modernForce_:Vec3{a6Force(input.controllerAxes[SDL_CONTROLLER_AXIS_LEFTX],false),a6Force(input.controllerAxes[SDL_CONTROLLER_AXIS_LEFTY],true),0});set_vector(global_vector("joy_rot"),{a6Axis(input.controllerAxes[SDL_CONTROLLER_AXIS_RIGHTX]),a6Axis(input.controllerAxes[SDL_CONTROLLER_AXIS_RIGHTY]),a6Axis(input.controllerAxes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT])});
 if(input.quit){auto close=global("on_close");if(close.kind==Value::Kind::Function)edge(close,0);else quit=true;}if(renderer&&state.profile==SemanticProfile::CampaignA6)renderer->set_mouse_mode(number("mouse_mode")!=0,number("mouse_mode")==0&&(number("enable_mouse")!=0||firstPersonActive_));}
void Engine::tick(Runtime& runtime,const Input& in){ensure_font_palette(runtime.compiler_settings().entry);input=in;if(!std::isfinite(in.dt))throw std::runtime_error("nonfinite native frame duration");auto seconds=std::clamp(in.dt,0.0,1.0);time_+=std::max(0.0,in.dt);auto frameRaw=std::max<std::int32_t>(1,Value::from_double(seconds*16).raw());set_global("time_frame",Value::from_raw(frameRaw));set_global("total_ticks",Value::from_raw(std::bit_cast<std::int32_t>(global("total_ticks").bits+std::uint32_t(frameRaw))));set_global("total_secs",Value::from_double(time_));set_global("total_frames",Value::from_int(global("total_frames").integer()+1));set_global("fps",Value::from_double(seconds?1/seconds:0));
 // A6 00414650 (004146e0-004147fb): every main-loop pass copies localtime into SYS_*; DOW is Monday=1..Sunday=7, DOY 1-based.
 {const auto now=std::time(nullptr);std::tm t{};localtime_r(&now,&t);for(auto [name,value]:{std::pair{"sys_seconds",t.tm_sec},{"sys_minutes",t.tm_min},{"sys_hours",t.tm_hour},{"sys_day",t.tm_mday},{"sys_month",t.tm_mon+1},{"sys_year",t.tm_year+1900},{"sys_dow",(t.tm_wday+6)%7+1},{"sys_doy",t.tm_yday+1}})set_global(name,Value::from_int(value));}
 coop_tick(runtime,in); // 2 PLAYER menu item and character select (native/coop_menu.cpp); hides the presses it consumes from `input`
 rumble=0;if(controller.modern){modernForce_=apply_modern_controller(controller,input,controllerHeld_);if(controller.vibration){auto p=global("player");ObjectId id=p.kind==Value::Kind::Object&&entities.contains(p.bits)&&state.get(p.bits)&&state.get(p.bits)->alive?p.bits:0;rumble=damageRumble_.update(id,id?field(id,"skill1"):0);if(rumble>0&&renderer)renderer->rumble(rumble,250);}}
 auto timeRaw=number("fps_lock")!=0?Value::from_double(16/std::max(1.0,number("fps_max"))).raw():frameRaw;auto minfps=std::max(1.0,number("fps_min"));timeRaw=std::clamp(timeRaw,51,std::max(51,Value::from_double(16/minfps).raw()));auto mul=[](std::int32_t a,std::int32_t b){auto n=std::int64_t(a)*b+512;auto q=n>=0?n/1024:-((-n+1023)/1024);return std::bit_cast<std::int32_t>(std::uint32_t(q));};timeRaw=mul(timeRaw,global("time_factor").raw());auto prior=global("time").raw();auto diff=std::bit_cast<std::int32_t>(std::uint32_t(prior)-std::uint32_t(timeRaw));timeRaw=std::bit_cast<std::int32_t>(std::uint32_t(timeRaw)+std::uint32_t(mul(diff,global("time_smooth").raw())));set_global("time",Value::from_raw(timeRaw));set_global("time_fac",Value::from_raw(0x100000/std::max(51,timeRaw)));
 if(renderer){auto m=renderer->metrics();currentWidth_=m.width;currentHeight_=m.height;set_vector(global_vector("screen_size"),{float(m.width),float(m.height),0});}
 if(nativeDisplay_&&(displayWidth_!=currentWidth_||displayHeight_!=currentHeight_))prepare_display();
 if(state.profile==SemanticProfile::CampaignA6&&character_tick(runtime,in)){media.update();make_frame();character_overlay();console_overlay();return;}
 update_input(runtime);make_frame(state.profile!=SemanticProfile::BonusA7);if(renderer)for(auto& c:frame_.cameras)if(c.portalId&&!c.portalTarget)if(auto surface=Renderer::portal_surface(frame_,c,renderer->metrics())){set_field(c.sourceId,"portal_x",surface->position.x);set_field(c.sourceId,"portal_y",surface->position.y);set_field(c.sourceId,"portal_z",surface->position.z);set_field(c.sourceId,"pnormal_x",surface->normal.x);set_field(c.sourceId,"pnormal_y",surface->normal.y);set_field(c.sourceId,"pnormal_z",surface->normal.z);}
 if(state.profile==SemanticProfile::CampaignA6)update_ui(runtime);runtime.tick(timeRaw,global("freeze_mode").raw());if(!viewsPrepared_){prepare(runtime);viewsPrepared_=true;}
 console_tick(runtime); // native console cheats/quickload position; no-op unless used (native/console.cpp)
 door_tick(runtime); // native seamless doors; inert when off (native/seamless_doors.cpp)
 if(global("freeze_mode").raw()<1&&global("debug_mode").raw()<1){const std::uint32_t moveMask=core_mask(Object::Kind::Particle,"move",state.profile)->mask;const std::size_t particleCount=activeParticles.size();for(std::size_t i=0;i<particleCount&&i<activeParticles.size();++i){auto pd=activeParticles[i];auto* o=state.get(pd.object);if(!o||!o->alive)continue;
 auto integrate=[&](std::string_view p,std::string_view velocity){auto& a=o->field(p).value;auto b=o->field(velocity).value.raw();a=Value::from_raw(std::bit_cast<std::int32_t>(a.bits+std::uint32_t(mul(b,timeRaw))));};
 if(pd.legacy){set_global("my_age",pd.first?Value{}:Value::from_raw(std::bit_cast<std::int32_t>(global("total_ticks").bits-std::uint32_t(pd.birthTick))));set_vector(global_vector("my_pos"),position(pd.object));set_vector(global_vector("my_speed"),{float(field(pd.object,"vel_x")),float(field(pd.object,"vel_y")),float(field(pd.object,"vel_z"))});set_vector(global_vector("my_color"),{float(field(pd.object,"blue")),float(field(pd.object,"green")),float(field(pd.object,"red"))});for(auto [g,f]:{std::pair{"my_size","size"},std::pair{"my_alpha","alpha"},std::pair{"my_flare","flare"},std::pair{"my_transparent","transparent"},std::pair{"my_bright","bright"}})set_global(g,Value::from_double(field(pd.object,f)));set_global("my_map",o->field("bmap").value);set_global("my_action",o->field("function").value);
 auto fn=global("my_action");if(fn.kind==Value::Kind::Function)runtime.start(fn.bits,{},0,0);o=state.get(pd.object);if(!o||!o->alive)continue;fn=global("my_action");if(!fn.bits){runtime.kill_for_entity(pd.object);state.retire(pd.object);continue;}o->field("function").value=fn;o->field("bmap").value=global("my_map");set_position(pd.object,vector(global_vector("my_pos")));auto speed=vector(global_vector("my_speed"));set_field(pd.object,"vel_x",speed.x);set_field(pd.object,"vel_y",speed.y);set_field(pd.object,"vel_z",speed.z);auto color=vector(global_vector("my_color"));set_field(pd.object,"blue",color.x);set_field(pd.object,"green",color.y);set_field(pd.object,"red",color.z);for(auto [g,f]:{std::pair{"my_size","size"},std::pair{"my_alpha","alpha"},std::pair{"my_flare","flare"},std::pair{"my_transparent","transparent"},std::pair{"my_bright","bright"}})set_field(pd.object,f,number(g));integrate("x","vel_x");integrate("y","vel_y");integrate("z","vel_z");if(i<activeParticles.size()&&activeParticles[i].object==pd.object)activeParticles[i].first=false;
 }else {auto fn=callback(pd.object,"function");if(fn)runtime.start(fn,{},pd.object,0);o=state.get(pd.object);if(!o||!o->alive)continue;auto& lifespan=o->field("lifespan").value;if(lifespan.raw()<1){runtime.kill_for_entity(pd.object);state.retire(pd.object);continue;}lifespan=Value::from_raw(std::bit_cast<std::int32_t>(lifespan.bits-std::uint32_t(timeRaw)));if(o->field("flags").value.bits&moveMask){integrate("x","vel_x");integrate("y","vel_y");integrate("z","vel_z");auto& vz=o->field("vel_z").value;vz=Value::from_raw(std::bit_cast<std::int32_t>(vz.bits-std::uint32_t(mul(o->field("gravity").value.raw(),timeRaw))));}}
 if(o->alive){auto& particleFlags=o->field("flags").value;particleFlags=Value::from_raw(std::bit_cast<std::int32_t>(particleFlags.bits&~0x01000000u));}
 }}
 std::erase_if(activeParticles,[&](const auto& p){auto* o=state.get(p.object);return !o||!o->alive;});set_global("num_particles",Value::from_int(std::int32_t(activeParticles.size())));
 if(state.profile==SemanticProfile::BonusA7){if(pendingLevelLoad&&!pendingExit_){auto next=std::move(pendingLevel);pendingLevelLoad=false;load_level(runtime,next);}update_query_actors(runtime);}
 else{worldFrameEvents(*this,runtime);if(pendingLevelLoad){auto next=std::move(pendingLevel);pendingLevelLoad=false;load_level(runtime,next);}}
 if(state.profile==SemanticProfile::CampaignA6){physics.step(runtime);update_first_person();}
 media.master_volume(number("sound_vol"),number("midi_vol"));media.update();auto camera=global("camera");if(camera.kind==Value::Kind::Object)media.listener(position(camera.bits),field(camera.bits,"pan"));make_frame();
 if(fadeVisible_&&!hold_presentation())++fadeFrames;
 if(state.profile==SemanticProfile::BonusA7)update_ui(runtime);
 if(state.profile==SemanticProfile::CampaignA6)character_overlay();
 console_overlay();
 if(state.profile==SemanticProfile::BonusA7&&pendingExit_){pendingExit_=false;quit=true;}
}
void Engine::finish_frame(Runtime& runtime){if(state.profile==SemanticProfile::BonusA7)physics.step(runtime);}
// Native convenience, not in the original. A6 cannot skip the startup logos (K22.wdl Menu_AcilisEkrani: sleeps and alpha fades,
// about 15 s), and the New Game intro (YeniMenu.wdl YeniOyunBaslat) and level movies (LevelYonetim.wdl) only on key_esc.
// Escape, Return, Space, a left click or pad A/B/Start fast-forwards the logos to the main menu (ordinary 1/60 s ticks without
// drawing, so every script effect still happens) and acts as Escape in the intro and in a full-screen media_play movie (not the
// media_loop main-menu background, Kabusdismenu.wdl kn_arg.k22). Afterwards the
// game sees no input until everything is released, so the press never reaches the menu or the game.
const Input& Engine::skip_intro(Runtime& runtime,const Input& in){
 auto neutral=[&]{skipInput=Input{};skipInput.dt=in.dt;skipInput.quit=in.quit;skipInput.mouseX=in.mouseX;skipInput.mouseY=in.mouseY;skipInput.joystickConnected=in.joystickConnected;return std::cref(skipInput);};
 if(skipHold){if(in.keys.any()||in.mouseButtons.any()||in.controllerButtons.any()||in.joystickButtons[0].any()||in.joystickButtons[1].any())return neutral();skipHold=false;}
 const bool press=in.pressed[SDL_SCANCODE_ESCAPE]||in.pressed[SDL_SCANCODE_RETURN]||in.pressed[SDL_SCANCODE_KP_ENTER]||in.pressed[SDL_SCANCODE_SPACE]||in.mousePressed[SDL_BUTTON_LEFT]
  ||in.controllerPressed[SDL_CONTROLLER_BUTTON_A]||in.controllerPressed[SDL_CONTROLLER_BUTTON_B]||in.controllerPressed[SDL_CONTROLLER_BUTTON_START];
 if(!press||state.profile==SemanticProfile::BonusA7)return in;
 if(auto logos=runtime.function("Menu_AcilisEkrani");runtime.running(logos)){
  neutral();skipInput.dt=1.0/60;
  for(int i=0;i<60*120&&runtime.running(logos)&&!quit;++i){tick(runtime,skipInput);finish_frame(runtime);}
  skipHold=true;return neutral();
 }
 if(runtime.running(runtime.function("YeniOyunBaslat"))||(movie&&!movieLoop&&media.movie_playing(movie))){skipInput=in;skipInput.keys.set(SDL_SCANCODE_ESCAPE);skipInput.pressed.set(SDL_SCANCODE_ESCAPE);skipHold=true;return skipInput;}
 return in;
}
Value Engine::builtin(Runtime& runtime,ProcessContext& ctx,std::string_view name,std::span<const Argument> args){
 ensure_font_palette(runtime.compiler_settings().entry);
 display_admitted();
 if(name=="k22_body_player")return Value::from_int(body_character());
 if(name=="k22_menu_player")return Value::from_int(menu_character());
 if(name=="k22_weapon_player")return Value::from_int(weapon_character());
 if(name=="k22_body_oyuncu_pasif"||name=="k22_body_koruyucu_pasif"){ // a locked body obeys the story character's control gate
  const bool koru=characterLock>=0?story_character()==2:name=="k22_body_koruyucu_pasif";
  return global(koru?"koruyucu_pasif":"oyuncu_pasif");
 }
 if(name=="k22_ebru_hit_kind"){const double t=number("dmr_yaralan_tip");return characterLock>=0&&(t==2||t==3)?Value::from_int(int(t)-2):global("dmr_yaralan_tip");} // character.cpp
 if(name=="k22_bullet_hit_kind"){ // silah.wdl:680 you.skill3=aktif_silah; Lain handles one firearm kind (character.cpp)
  const auto lain=runtime.function("kotukoru_ai");
  if(characterLock>=0&&lain&&ctx.you&&state.get(ctx.you)&&callback(ctx.you,"action1")==lain)return Value::from_int(2);
  return global("Aktif_Silah");
 }
 if(name=="k22_pose_weapon"){const double w=number("Aktif_Silah");return characterLock>=0&&(w<0||w>7)?Value::from_int(1):global("Aktif_Silah");} // oy_Oynat: Demir's t_Silah has weapons 0-7
 if(state.profile==SemanticProfile::BonusA7){
  auto count=[&](std::size_t n){if(args.size()!=n)throw std::runtime_error(std::string(name)+": invalid argument count");};
  auto numeric=[&](std::size_t i){auto v=args[i].current();if(v.kind!=Value::Kind::Number)throw std::runtime_error(std::string(name)+": numeric argument required");return v.raw();};
  auto live=[&](Value v)->Object&{auto* o=state.get(v.bits);if((v.kind!=Value::Kind::Object&&v.kind!=Value::Kind::NativeText)||!o||!o->alive)throw std::runtime_error(std::string(name)+": dead or invalid object");return *o;};
  if(core_equal(name,"fraction")){count(1);return Value::from_raw(numeric(0)%1024);}
  if(core_equal(name,"str_to_asc")){count(1);live(args[0].current());const auto text=string(args[0].current());return Value::from_int(text.empty()?0:static_cast<unsigned char>(text.front()));}
  if(core_equal(name,"str_trunc")){
   count(2);auto value=args[0].current();auto& o=live(value);if(o.kind!=Object::Kind::String)throw std::runtime_error("str_trunc requires STRING");if(o.nullStringBuffer)throw std::runtime_error("str_trunc reads NULL STRING buffer");
   auto n=numeric(1)>>10;if(n<0)throw std::runtime_error("str_trunc negative suffix writes outside STRING buffer");
   o.text.resize(o.text.size()-std::min(o.text.size(),std::size_t(n)));return value;
  }
  if(core_equal(name,"str_create")){
   count(1);auto source=args[0].current();std::string text;const bool nullBuffer=!source||live(source).nullStringBuffer;
   if(!nullBuffer){auto bytes=string(source);if(bytes.size()>1&&bytes[0]=='#'&&bytes[1]>='0'&&bytes[1]<='9'){
    std::size_t length=0;for(auto c:bytes.substr(1)){if(c<'0'||c>'9')break;auto digit=std::size_t(c-'0');if(length>(text.max_size()-digit)/10)throw std::runtime_error("str_create capacity overflow");length=length*10+digit;}text.assign(length,' ');
   }else text.assign(bytes);}
   auto result=new_string(std::move(text));state.require(result.bits).nullStringBuffer=nullBuffer;return result;
  }
  if(core_equal(name,"str_remove")){count(1);auto& o=live(args[0].current());if(o.kind!=Object::Kind::String)throw std::runtime_error("str_remove requires STRING");state.retire(o.id);return Value::from_int(1);}
  if(core_equal(name,"handle")){count(1);return Value::from_raw(std::bit_cast<std::int32_t>(live(args[0].current()).id));}
  if(core_equal(name,"ptr_for_handle")){count(1);auto word=args[0].current();if(word.kind!=Value::Kind::Number)throw std::runtime_error("ptr_for_handle requires opaque numeric handle");auto* o=state.get(word.bits);return o&&o->alive?state.object_value(o->id):Value::object(0);}
  if(core_equal(name,"vec_lerp")){
   count(4);
   auto components=[&](const Argument& arg){std::array<Ref,3> result;auto value=arg.current();auto ref=value.kind==Value::Kind::Object?Ref{}:arg.ref;auto& o=live(Value::object(value.kind==Value::Kind::Object?value.bits:ref.owner));
    if(!o.array.empty()&&(o.kind==Object::Kind::Vector||core_equal(ref.field,"skill"))){if(ref.index+3>o.array.size())throw std::runtime_error("vec_lerp vector array range");for(std::size_t i=0;i<3;++i)result[i]={&o.array[ref.index+i],o.id,ref.field,ref.index+i};}
    else {auto names=core_vector_names(ref.field);for(std::size_t i=0;i<3;++i)result[i]=state.member(o.id,names[i]);}return result;};
   auto out=components(args[0]),a=components(args[1]),b=components(args[2]);auto t=numeric(3);auto inverse=std::bit_cast<std::int32_t>(1024u-std::uint32_t(t));
   auto multiply=[](std::int32_t k,std::int32_t v){auto p=std::int64_t(k)*v;return std::uint32_t((p>>10)+((p>>9)&1));};
   for(std::size_t i=0;i<3;++i)out[i].set(Value::from_raw(std::bit_cast<std::int32_t>(multiply(inverse,a[i].get().raw())+multiply(t,b[i].get().raw()))));
   return args[0].current();
  }
  if(core_equal(name,"c_setminmax")){
   count(1);auto& o=live(args[0].current());if(o.kind!=Object::Kind::Entity)throw std::runtime_error("c_setminmax requires ENTITY");o.field("flags").value=Value::from_raw(std::bit_cast<std::int32_t>(o.field("flags").value.bits|3u));
   auto found=entities.find(o.id);if(found==entities.end()||!found->second.model)return Value{};
   const auto& data=found->second;auto lo=data.model->min,hi=data.model->max,offset=data.boundsOffset;
   const std::array<float,6> bounds{lo.x+offset.x,lo.y+offset.y,lo.z+offset.z,hi.x+offset.x,hi.y+offset.y,hi.z+offset.z};
   const std::array<std::string_view,6> names{"min_x","min_y","min_z","max_x","max_y","max_z"};for(std::size_t i=0;i<6;++i)state.member(o.id,names[i]).set(Value::from_double(bounds[i]));return Value::from_int(1);
  }
  if(core_equal(name,"sys_exit")){if(args.size()>1)throw std::runtime_error("sys_exit takes optional message");if(!args.empty()){auto message=args[0].current();if(message){auto& o=live(message);exitMessage_=Value::owned(o,message.kind);}}pendingExit_=true;return Value::from_raw(1);}
 }
 if(core_equal(name,"effect_load")){
  if(args.size()!=2)throw std::runtime_error("effect_load requires MATERIAL and filename");
  const auto value=args[0].current();auto* material=state.get(value.bits);if(value.kind!=Value::Kind::Object||!material||!material->alive||material->kind!=Object::Kind::Material)throw std::runtime_error("effect_load requires live MATERIAL");
  const auto resource=args[1].current();const auto* source=(resource.kind==Value::Kind::Object||resource.kind==Value::Kind::NativeText)?state.get(resource.bits):nullptr;
  auto filename=source&&source->alive&&source->nullStringBuffer?std::string_view{}:string(resource);if(auto end=filename.find('\0');end!=std::string_view::npos)filename=filename.substr(0,end);
  auto retained=materialEffects_.find(material->id);if(filename.empty()){if(retained==materialEffects_.end())return Value::from_int(1);filename=retained->second.resource;}
  const auto bytes=assets.read(filename);const auto kind=Renderer::identify_effect(filename,std::as_bytes(std::span(bytes)));
  const auto generation=++effectGeneration_;
  if(retained==materialEffects_.end())materialEffects_.emplace(material->id,MaterialEffect{std::string(filename),kind,generation});
  else {if(filename.data()!=retained->second.resource.data())retained->second.resource.assign(filename);retained->second.kind=kind;retained->second.generation=generation;}
  // A7 10007d30 returns raw0x400; NULL/empty releases compilation, retains its filename, and prepares again.
  return Value::from_int(1);
 }
 if(auto result=service_builtin(runtime,ctx,name,args))return *result;if(auto result=world_builtin(runtime,ctx,name,args))return *result;
 if(auto result=physics.builtin(runtime,ctx,name,args)){
  if(state.profile==SemanticProfile::BonusA7&&core_equal(name,"phent_settype")&&*result&&!args.empty()){const auto owner=args[0].current();if(owner.kind==Value::Kind::Object&&physics.query_body_geometry(owner.bits)){const auto actor=queryActors_.find(owner.bits);if(actor!=queryActors_.end())actor->second.shape=0;}}
  return *result;
 }
 if(core_equal(name,"exit")){if(!args.empty())throw std::runtime_error("exit takes no arguments");quit=true;return {};}
 if(core_equal(name,"txt_load")){
  if(args.size()!=2)throw std::runtime_error("txt_load needs text and filename");
  auto& o=state.require(args[0].current().bits);if(o.kind!=Object::Kind::Text)throw std::runtime_error("txt_load requires TEXT");
  std::vector<std::uint8_t> bytes;
  if(state.profile==SemanticProfile::BonusA7){auto relative=svc_relative(string(args[1].current()));auto path=svc_private(saveRoot,relative);bytes=std::filesystem::exists(path)?svc_read_file(path):assets.read(relative.generic_string());}
  else bytes=assets.read(string(args[1].current()));
  const std::size_t capacity=o.array.size();
  const auto delimit=string(global("delimit_str"));const int stop=delimit.empty()?0:static_cast<signed char>(delimit.front());
  std::size_t cursor=0,stored=0;while(cursor<bytes.size()&&stored<capacity){
   while(cursor<bytes.size()&&bytes[cursor]<=31)++cursor;
   if(cursor==bytes.size())break;
   std::string line;line.reserve(128);line.push_back(char(bytes[cursor++]));
   while(cursor<bytes.size()){auto c=bytes[cursor++];if(c<=31||c==stop)break;if(line.size()<4095)line.push_back(char(c));}
   o.array[stored++].value=new_string(std::move(line));
  }
  return Value::from_int(std::int32_t(stored));
 }
 if(core_equal(name,"remove")){if(args.size()!=1)throw std::runtime_error("remove needs object");auto id=args[0].current().bits;auto* o=state.get(id);if(!o)return {};if(o->kind==Object::Kind::Entity)remove_entity(runtime,id);else {runtime.kill_for_entity(id);state.retire(id);}return {};}
 if(core_equal(name,"error")){if(args.empty())throw std::runtime_error("WDL error()");throw std::runtime_error("WDL error: "+std::string(string(args[0].current())));}
 throw std::runtime_error("unsupported native engine builtin: "+std::string(name));}
}

#include <fcntl.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace k22 {
namespace {
constexpr double svc_pi=std::numbers::pi;
bool svc_equal(std::string_view a,std::string_view b){return case_equal(a,b);}
std::filesystem::path svc_relative(std::string_view name){
 std::string normalized(name);for(char& c:normalized){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c+=32;}
 std::filesystem::path path(normalized);if(path.empty()||path.is_absolute()||path.filename().empty()||path.filename()=="."||normalized.find(':')!=std::string::npos)throw std::runtime_error("invalid private file name");
 for(const auto& part:path)if(part=="..")throw std::runtime_error("private file traversal");return path.lexically_normal();
}
std::filesystem::path svc_private(const std::filesystem::path& root,const std::filesystem::path& relative){
 auto base=std::filesystem::weakly_canonical(root),target=std::filesystem::weakly_canonical(root/relative);auto match=std::mismatch(base.begin(),base.end(),target.begin(),target.end());if(match.first!=base.end())throw std::runtime_error("private file symlink escapes save directory");return target;
}
std::vector<std::uint8_t> svc_read_file(const std::filesystem::path& path){
 std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("cannot open private file: "+path.string());auto end=f.tellg();if(end<0||end>64*1024*1024)throw std::runtime_error("private file exceeds limit");std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));f.seekg(0);if(!bytes.empty()&&!f.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("cannot read private file: "+path.string());return bytes;
}
void svc_flush(FileData& f){
 if(f.writable)saves::write_atomic(f.output,f.bytes);
}
void svc_append(FileData& f,std::string_view s){if(!f.writable)throw std::runtime_error("file handle is read-only");if(s.size()>64*1024*1024-f.bytes.size())throw std::runtime_error("private file exceeds limit");f.bytes.insert(f.bytes.end(),s.begin(),s.end());f.cursor=f.bytes.size();}
std::string_view svc_trim(std::string_view s){while(!s.empty()&&(s.front()==' '||s.front()=='\t'||s.front()=='\r'))s.remove_prefix(1);while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\r'))s.remove_suffix(1);return s;}
const Image& svc_bitmap(Engine& e,Value value){
 // Native ADD keeps the address word; arithmetic's Number tag does not change its handle identity.
 if(value.kind!=Value::Kind::Object&&value.kind!=Value::Kind::Number)throw std::runtime_error("bitmap object required");auto& bitmap=e.state.require(value.bits);if(!bitmap.alive||bitmap.kind!=Object::Kind::Bmap)throw std::runtime_error("live bitmap object required");
 for(const auto& [handle,target]:e.movieTargets)if(target==value.bits)if(const auto* image=e.media.movie_frame(handle))return *image;
 auto cached=e.bitmapImages.find(value.bits);if(cached!=e.bitmapImages.end())return *cached->second;
 auto entity=bitmap.fields.find("entity"),skin=bitmap.fields.find("skin");if(entity!=bitmap.fields.end()&&skin!=bitmap.fields.end()){auto found=e.entities.find(entity->second.value.bits);if(found==e.entities.end()||!found->second.model)throw std::runtime_error("bitmap owning model removed");auto index=skin->second.value.integer();const auto& materials=found->second.model->materials;if(index<1||std::size_t(index)>materials.size()||!materials[index-1].texture)throw std::runtime_error("invalid model bitmap skin");auto image=materials[index-1].texture;e.bitmapImages.emplace(value.bits,image);return *image;}
 e.assets.set_native_alpha_depth(e.global("D3D_ALPHADEPTH").real());
 return e.assets.image(bitmap.text);
}
std::string svc_json(std::string_view s){
 bool utf8=true;for(std::size_t i=0;i<s.size()&&utf8;){unsigned char c=s[i++];if(c<128)continue;unsigned count=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;if(!count||s.size()-i<count){utf8=false;break;}unsigned cp=c&((1u<<(6-count))-1);for(unsigned j=0;j<count;++j){unsigned char next=s[i++];if((next&0xc0)!=0x80){utf8=false;break;}cp=(cp<<6)|(next&63);}if(cp<(count==1?128u:count==2?2048u:65536u)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))utf8=false;}
 std::string converted;if(!utf8){char* bytes=SDL_iconv_string("UTF-8","CP1254",s.data(),s.size()+1);if(!bytes)throw std::runtime_error("Discord activity text cannot be decoded");converted=bytes;SDL_free(bytes);s=converted;}
 std::string result="\"";for(unsigned char c:s){switch(c){case '"':result+="\\\"";break;case '\\':result+="\\\\";break;case '\n':result+="\\n";break;case '\r':result+="\\r";break;case '\t':result+="\\t";break;default:if(c<32){char b[7];std::snprintf(b,sizeof b,"\\u%04x",c);result+=b;}else result+=char(c);}}result+='"';return result;
}
void svc_disconnect(Engine& e){if(e.discordSocket>=0)::close(e.discordSocket);e.discordSocket=-1;e.discordReady=false;e.discordActivitySent=false;e.discordInput.clear();}
std::uint32_t svc_u32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
bool svc_send(Engine& e,std::uint32_t op,std::string_view json){
 if(e.discordSocket<0)return false;std::array<std::uint8_t,8> header{};for(int i=0;i<4;++i){header[i]=std::uint8_t(op>>(8*i));header[4+i]=std::uint8_t(json.size()>>(8*i));}
 auto send_part=[&](const void* data,std::size_t size){const char* p=static_cast<const char*>(data);while(size){auto n=::send(e.discordSocket,p,size,MSG_NOSIGNAL);if(n>0){p+=n;size-=std::size_t(n);continue;}if(n<0&&errno==EINTR)continue;if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){pollfd fd{e.discordSocket,POLLOUT,0};if(::poll(&fd,1,100)>0&&(fd.revents&POLLOUT))continue;}return false;}return true;};
 if(!send_part(header.data(),header.size())||!send_part(json.data(),json.size())){svc_disconnect(e);return false;}return true;
}
void svc_receive(Engine& e){
 if(e.discordSocket<0)return;std::array<std::uint8_t,4096> chunk;for(;;){auto n=::recv(e.discordSocket,chunk.data(),chunk.size(),MSG_DONTWAIT);if(n>0){if(e.discordInput.size()+std::size_t(n)>1024*1024){svc_disconnect(e);return;}e.discordInput.insert(e.discordInput.end(),chunk.begin(),chunk.begin()+n);}else if(n==0){svc_disconnect(e);return;}else if(errno==EINTR)continue;else if(errno==EAGAIN||errno==EWOULDBLOCK)break;else{svc_disconnect(e);return;}}
 std::size_t consumed=0;while(e.discordInput.size()-consumed>=8){auto* p=e.discordInput.data()+consumed;auto op=svc_u32(p),len=svc_u32(p+4);if(len>1024*1024){svc_disconnect(e);return;}if(e.discordInput.size()-consumed<8+len)break;std::string_view payload(reinterpret_cast<const char*>(p+8),len);if(op==2){svc_disconnect(e);return;}if(op==3){std::string pong(payload);if(!svc_send(e,4,pong))return;}else if(op==1){auto evt=payload.find("\"evt\"");if(evt!=std::string_view::npos){auto colon=payload.find(':',evt+5);if(colon!=std::string_view::npos){auto value=svc_trim(payload.substr(colon+1));if(value.starts_with("\"READY\""))e.discordReady=true;}}}else if(op!=4){svc_disconnect(e);return;}consumed+=8+len;}
 if(consumed)e.discordInput.erase(e.discordInput.begin(),e.discordInput.begin()+static_cast<std::ptrdiff_t>(consumed));
}
bool svc_connect(Engine& e,std::string_view app){
 svc_receive(e);if(e.discordSocket>=0)return e.discordReady;
 for(char c:app)if(c<'0'||c>'9')throw std::runtime_error("invalid Discord application id");if(app.empty())throw std::runtime_error("empty Discord application id");
 auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();if(std::uint64_t(now)<e.discordRetryAt)return false;e.discordRetryAt=std::uint64_t(now)+1000;
 std::array<const char*,5> roots{std::getenv("XDG_RUNTIME_DIR"),std::getenv("TMPDIR"),std::getenv("TMP"),std::getenv("TEMP"),"/tmp"};
 for(const char* root:roots){if(!root||!*root)continue;for(int i=0;i<10;++i){sockaddr_un addr{};addr.sun_family=AF_UNIX;std::string path=std::string(root)+"/discord-ipc-"+std::to_string(i);if(path.size()>=sizeof addr.sun_path)continue;std::memcpy(addr.sun_path,path.c_str(),path.size()+1);int fd=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0)continue;int result=::connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof addr);if(result<0&&errno==EINPROGRESS){pollfd p{fd,POLLOUT,0};int error=0;socklen_t sz=sizeof error;result=(::poll(&p,1,100)>0&&::getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&sz)==0&&error==0)?0:-1;}if(result<0){::close(fd);continue;}e.discordSocket=fd;e.discordApplication=app;if(!svc_send(e,0,"{\"v\":1,\"client_id\":"+svc_json(app)+"}"))continue;svc_receive(e);return e.discordReady;}}
 return false;
}
std::string svc_ini_normalize(std::string_view key,std::string_view value){
 int maximum=key=="Screen:Mode"?2:key=="Screen:X"?3840:key=="Screen:Y"?2160:-1;if(maximum<0)return std::string(value);
 int n=0;if(value.empty())return "0";for(char c:value){if(c<'0'||c>'9'||n>maximum/10)return "0";n=n*10+c-'0';if(n>maximum)return "0";}return std::to_string(n);
}
void svc_ini_save(Engine& e){
 FileData file;file.writable=true;file.output=svc_private(e.saveRoot,svc_relative("K22.ini"));std::string previous;bool first=true;
 auto row=[&](std::string_view key,std::string_view value){svc_append(file,key);svc_append(file," = ");svc_append(file,value);svc_append(file,"\n");};
 for(const auto& [name,value]:e.iniValues)if(name.find(':')==std::string::npos)row(name,value);
 for(const auto& [name,value]:e.iniValues){auto split=name.rfind(':');if(split==std::string::npos)continue;std::string section=name.substr(0,split);auto key=std::string_view(name).substr(split+1);if(first||section!=previous){svc_append(file,"\n["+section+"]\n");previous=section;first=false;}row(key,value);}svc_flush(file);
}
bool svc_ini_parse(Engine& e,const std::filesystem::path& name,int depth){
 if(depth>16)throw std::runtime_error("K22.ini include depth exceeded");auto relative=svc_relative(name.generic_string());std::vector<std::uint8_t> bytes;auto privatePath=svc_private(e.saveRoot,relative);if(std::filesystem::exists(privatePath))bytes=svc_read_file(privatePath);else if(e.assets.contains(relative.generic_string()))bytes=e.assets.read(relative.generic_string());else return false;
 std::string_view text(reinterpret_cast<const char*>(bytes.data()),bytes.size());std::string section,joined;
 while(!text.empty()){auto end=text.find('\n');auto line=svc_trim(text.substr(0,end));text=end==std::string_view::npos?std::string_view{}:text.substr(end+1);
  if(line.empty())continue;
  if(line.front()==';'||line.front()=='#'){auto include=svc_trim(line.substr(1));if(line.starts_with("#include "))include=line;if(include.starts_with("#include ")){auto filename=svc_trim(include.substr(9));if(filename.empty()||!svc_ini_parse(e,relative.parent_path()/std::string(filename),depth+1))throw std::runtime_error("missing K22.ini include");}continue;}
  if(line.back()=='\\'||line.back()=='/'){joined.append(line.substr(0,line.size()-1));if(text.empty())throw std::runtime_error("unterminated K22.ini continuation");continue;}if(!joined.empty()){joined.append(line);line=joined;}
  auto comment=line.find_first_of(";#");if(comment!=std::string_view::npos)line=svc_trim(line.substr(0,comment));if(line.empty()){joined.clear();continue;}
  if(line.front()=='['||line.front()=='{'){char close=line.front()=='['?']':'}';if(line.back()!=close)throw std::runtime_error("malformed K22.ini section");section=std::string(svc_trim(line.substr(1,line.size()-2)));joined.clear();continue;}
  auto separator=line.find_first_of("=:");if(separator==std::string_view::npos)throw std::runtime_error("malformed K22.ini assignment");auto key=svc_trim(line.substr(0,separator)),value=svc_trim(line.substr(separator+1));if(key.empty())throw std::runtime_error("empty K22.ini key");e.iniValues[section.empty()?std::string(key):section+":"+std::string(key)]=std::string(value);joined.clear();
 }
 return true;
}
void svc_ini_load(Engine& e){
 svc_ini_parse(e,"K22.ini",0);bool corrected=false;
 for(std::string_view key:{"Screen:Mode","Screen:X","Screen:Y"}){auto it=e.iniValues.find(key);std::string value=svc_ini_normalize(key,it==e.iniValues.end()?std::string_view{}:std::string_view(it->second));if(it==e.iniValues.end()||it->second!=value){e.iniValues[std::string(key)]=value;corrected=true;}}
 if(corrected)svc_ini_save(e);e.iniInitialized=true;
}
Value svc_plugin_open(Engine& e,std::string_view name){
 auto path=svc_relative(name);if(!path.parent_path().empty()&&path.parent_path()!=".")throw std::runtime_error("native DLL services require the current plugin directory");
 auto plugin=path.filename().string();
 if(plugin=="renkayar.dll"){e.pluginGamma=true;return Value::from_int(1);}
 // Kabus Index.hpp's module-global IniVar owns K22.ini once; dll_open is not a reload.
 if(plugin=="kabus_dll.dll"){if(!e.iniInitialized)svc_ini_load(e);e.pluginNative=true;return Value::from_int(2);}
 throw std::runtime_error("unsupported native DLL service: "+plugin);
}
} // namespace
void Engine::initialize_plugins(const Runtime& runtime){
 // A6 00413990 calls 004133c0 after compilation, before MAIN/STARTER scheduling.
 // Native installs omit PE DLLs: parsed external imports select compiled providers,
 // not the original *.dll enumeration. Only default/current PLUGINDIR is supported.
 auto directory=runtime.compiler_settings().plugin_directory;for(char& c:directory)if(c=='\\')c='/';
 if(!directory.empty()){std::filesystem::path path(directory);if(path.is_absolute())return;for(const auto& part:path)if(!part.empty()&&part!=".")return;}
 for(std::string_view name:{"ini_isinitialized","ini_getvariable","ini_setvariable","parse_escapechar","discordrpc_tryconnect","discordrpc_isconnected","discordrpc_trydisconnect","discordrpc_setstatedetails"})if(runtime.external_import(name)){svc_plugin_open(*this,"kabus_dll.dll");break;}
 if(runtime.external_import("dl_pakaga")||runtime.external_import("dl_reset"))svc_plugin_open(*this,"renkayar.dll");
}
Engine::~Engine(){physics.clear();if(exitMessage_){if(auto* o=state.get(exitMessage_.bits))std::cerr<<'\n'<<o->text;exitMessage_={};}for(auto& [id,file]:files){try{svc_flush(file);}catch(const std::exception& error){std::cerr<<"save flush failed: "<<error.what()<<'\n';}}svc_disconnect(*this);}
std::optional<Value> Engine::service_builtin(Runtime& runtime,ProcessContext&,std::string_view name,std::span<const Argument> a){
 auto count=[&](std::size_t n){if(a.size()!=n)throw std::runtime_error(std::string(name)+": invalid argument count");};
 auto num=[&](std::size_t i)->double{if(i>=a.size()||a[i].current().kind!=Value::Kind::Number)throw std::runtime_error(std::string(name)+": numeric argument required");return a[i].current().real();};
 auto integer=[&](std::size_t i){num(i);return a[i].current().integer();};
 auto text=[&](std::size_t i)->std::string_view{if(i>=a.size())throw std::runtime_error(std::string(name)+": missing string argument");return string(a[i].current());};
 auto null_text=[&](std::size_t i){auto value=a[i].current();if(!value)return true;const auto* o=(value.kind==Value::Kind::Object||value.kind==Value::Kind::NativeText)?state.get(value.bits):nullptr;return o&&o->alive&&o->nullStringBuffer;};
 auto raw=[&](std::size_t i){num(i);return a[i].current().raw();};
 auto file=[&]() -> FileData& {auto id=integer(0);auto it=files.find(std::uint32_t(id));if(id<=0||it==files.end())throw std::runtime_error("invalid file handle");return it->second;};
 auto ret=[](double n){return Value::from_double(n);};
 if(name=="abs"){count(1);auto n=raw(0);return Value::from_raw(n<0?std::bit_cast<std::int32_t>(0u-std::uint32_t(n)):n);}
 if(name=="int"||name=="integer"){count(1);return Value::from_int(integer(0));}
 if(name=="float"){count(1);const float decoded=float(raw(0))/1024.f;return Value::from_raw(std::bit_cast<std::int32_t>(decoded));}
 if(name=="frc"){count(1);return Value::from_raw(raw(0)%1024);}
 if(name=="sign"){count(1);return Value::from_int((raw(0)>0)-(raw(0)<0));}
 if(name=="min"||name=="max"){count(2);return Value::from_raw(name=="min"?std::min(raw(0),raw(1)):std::max(raw(0),raw(1)));}
 if(name=="clamp"){count(3);auto lo=raw(1),hi=raw(2);if(lo>hi)throw std::runtime_error("clamp bounds reversed");return Value::from_raw(std::clamp(raw(0),lo,hi));}
 if(name=="ang"){count(1);auto n=raw(0)%(360*1024);if(n>180*1024)n-=360*1024;if(n< -180*1024)n+=360*1024;return Value::from_raw(n);}
 if(name=="sin"||name=="cos"){count(1);double angle=num(0)*svc_pi/180;return ret(name=="sin"?std::sin(angle):std::cos(angle));}
 if(name=="sqrt"){count(1);if(num(0)<0)throw std::runtime_error("sqrt negative argument");return ret(std::sqrt(num(0)));}
 if(name=="pow"){count(2);return ret(std::pow(num(0),num(1)));}
 if(name=="random"){count(1);double range=num(0);if(range<0)throw std::runtime_error("negative random range");return ret(std::generate_canonical<double,32>(random)*range);}
 if(name=="randomize"){count(0);if(!fixedRandom)random.seed(std::random_device{}());return Value{};}
 if(core_equal(name,"vector")||core_equal(name,"_vec")){
  count(3);const bool nativeFloat=core_equal(name,"_vec");
  vectorTempCursor_=(vectorTempCursor_+1)%vectorTemps_.size();auto& v=*vectorTemps_[vectorTempCursor_];
  for(std::size_t i=0;i<3;++i){auto value=a[i].current();if(value.kind!=Value::Kind::Number)throw std::runtime_error("vector: numeric argument required");v.array[i].value=nativeFloat?Value::from_double(std::bit_cast<float>(value.bits)):value;}
  return Value::object(v.id);
 }
 if(name=="vec_set"||name=="vec_add"||name=="vec_sub"||name=="vec_dist"){count(2);auto q=vector(a[1]);if(name=="vec_set"){set_vector(a[0],q);return a[0].current();}auto p=vector(a[0]);double dx=double(p.x)-q.x,dy=double(p.y)-q.y,dz=double(p.z)-q.z;if(name=="vec_dist")return ret(std::sqrt(dx*dx+dy*dy+dz*dz));float s=name=="vec_add"?1:-1;set_vector(a[0],{p.x+s*q.x,p.y+s*q.y,p.z+s*q.z});return a[0].current();}
 if(name=="vec_scale"){count(2);auto p=vector(a[0]);double s=num(1);set_vector(a[0],{float(p.x*s),float(p.y*s),float(p.z*s)});return a[0].current();}
 if(name=="vec_to_angle"){count(2);auto p=vector(a[1]);double length=std::sqrt(double(p.x)*p.x+double(p.y)*p.y+double(p.z)*p.z);double pan=std::atan2(p.y,p.x)*180/svc_pi;if(pan<0)pan+=360;set_vector(a[0],{float(pan),float(std::atan2(p.z,std::hypot(p.x,p.y))*180/svc_pi),vector(a[0]).z});return ret(length);}
 if(name=="str_len"){count(1);return null_text(0)?Value{}:ret(text(0).size());}
 if(name=="str_cpy"||name=="str_cat"){count(2);if(state.profile==SemanticProfile::BonusA7&&null_text(1)){if(name=="str_cat")mutable_string(a[0]);return a[0].current();}auto source=text(1);auto& target=mutable_string(a[0]);auto& object=state.require(a[0].current().bits);if(name=="str_cat"&&object.nullStringBuffer&&source.empty())throw std::runtime_error("str_cat reads NULL STRING buffer for empty append");if(source.data()==target.data()){if(name=="str_cat"){std::string copy(source);target+=copy;}}else if(name=="str_cpy")target.assign(source);else target.append(source);object.nullStringBuffer=false;return a[0].current();}
 if(name=="str_clip"){count(2);auto n=integer(1);if(n<0)throw std::runtime_error("negative str_clip length");text(0);auto& target=mutable_string(a[0]);target.erase(0,std::min(target.size(),std::size_t(n)));return a[0].current();}
 if(name=="str_cmpi"){count(2);return Value::from_int(svc_equal(text(0),text(1)));}
 if(name=="str_to_num"){count(1);auto s=text(0);while(!s.empty()&&(s.front()==' '||(s.front()>='\t'&&s.front()<='\r')))s.remove_prefix(1);if(!s.empty()&&s.front()=='+')s.remove_prefix(1);if(s.empty())return Value{};double n=0;auto result=std::from_chars(s.data(),s.data()+s.size(),n);if(result.ec==std::errc::invalid_argument)return Value{};if(result.ec!=std::errc{}||!std::isfinite(n))throw std::runtime_error("string number out of range");return ret(n);}
 if(name=="str_for_num"){count(2);char buffer[64];auto n=raw(1);int size=n%1024==0?std::snprintf(buffer,sizeof buffer,"%d",n>>10):std::snprintf(buffer,sizeof buffer,"%.3f",double(float(n))/1024);if(size<0||size>=int(sizeof buffer))throw std::runtime_error("number formatting failed");mutable_string(a[0]).assign(buffer,std::size_t(size));state.require(a[0].current().bits).nullStringBuffer=false;return a[0].current();}
 if(name=="snd_play"||name=="snd_loop"){count(3);auto h=media.sound_play(text(0),num(1),num(2),100,name=="snd_loop");return ret(h);}
 if(name=="snd_playing"){count(1);return Value::from_int(media.sound_playing(std::uint32_t(integer(0))));}
 if(name=="snd_stop"){count(1);media.sound_stop(std::uint32_t(integer(0)));return Value{};}
 if(name=="snd_tune"){count(4);media.sound_tune(std::uint32_t(integer(0)),num(1),num(2),num(3));return Value{};}
 if(name=="snd_stopall"){count(1);auto mode=integer(0);if(mode!=4)throw std::runtime_error("unsupported snd_stopall mode");media.sound_stop_all();return Value{};}
 if(name=="media_play"||name=="media_loop"){count(3);auto target=a[1].current();if(target&&(target.kind!=Value::Kind::Object||state.require(target.bits).kind!=Object::Kind::Bmap))throw std::runtime_error("movie target must be bitmap or NULL");auto h=media.movie_play(text(0),num(2),name=="media_loop");if(target){for(auto it=movieTargets.begin();it!=movieTargets.end();){if(it->second==target.bits){media.movie_stop(it->first);it=movieTargets.erase(it);}else ++it;}movieTargets[h]=target.bits;}else{if(movie)media.movie_stop(movie);movie=h;movieLoop=name=="media_loop";}set_global("media_handle",ret(h));return ret(h);}
 if(name=="media_playing"){count(1);auto h=std::uint32_t(integer(0));return ret(media.movie_status(h));}
 if(name=="media_stop"){count(1);auto h=std::uint32_t(integer(0));media.movie_stop(h);if(!h||h==movie)movie=0;return Value{};}
 if(name=="dll_open"){count(1);return svc_plugin_open(*this,text(0));}
 if(name=="ini_isinitialized"){count(0);return Value::from_int(pluginNative&&iniInitialized);}
 if(name=="ini_getvariable"){count(1);if(!pluginNative)throw std::runtime_error("INI plugin service not opened");if(!iniInitialized)return Value{};auto it=iniValues.find(text(0));return state.make_string(it==iniValues.end()?"undefined":it->second,true);} // Original export 100c3600 returns an independent strdup char*.
 if(name=="ini_setvariable"){count(2);if(!pluginNative)throw std::runtime_error("INI plugin service not opened");if(!iniInitialized)throw std::runtime_error("INI service not initialized");auto key=std::string(text(0)),value=std::string(text(1));if(key.empty()||key.find_first_of("\r\n=[];#")!=std::string::npos||value.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("invalid INI variable");iniValues[key]=svc_ini_normalize(key,value);svc_ini_save(*this);return Value{};}
 if(name=="parse_escapechar"){count(1);if(!pluginNative)throw std::runtime_error("escape plugin service not opened");auto input=a[0].current();if(input.kind!=Value::Kind::Object||state.require(input.bits).kind!=Object::Kind::String)throw std::runtime_error("Parse_EscapeChar requires an engine STRING");std::string result(string(input));constexpr std::array<std::pair<std::string_view,std::string_view>,9> pairs{{{"\\'","'"},{"\\\"","\""},{"\\\\","\\"},{"\\n","\n"},{"\\r","\r"},{"\\t","\t"},{"\\b","\b"},{"\\f","\f"},{"\\v","\v"}}};for(auto [escaped,decoded]:pairs){std::size_t pos=0;while((pos=result.find(escaped,pos))!=std::string::npos){result.replace(pos,escaped.size(),decoded);pos+=decoded.size();}}return state.make_string(std::move(result),true);}
 if(name=="discordrpc_tryconnect"){count(1);if(!pluginNative)throw std::runtime_error("Discord plugin service not opened");return Value::from_int(svc_connect(*this,text(0)));}
 if(name=="discordrpc_isconnected"){count(0);if(!pluginNative)throw std::runtime_error("Discord plugin service not opened");svc_receive(*this);return Value::from_int(discordReady);}
 if(name=="discordrpc_trydisconnect"){count(0);svc_disconnect(*this);return Value{};}
 if(name=="discordrpc_setstatedetails"){count(2);if(!pluginNative)throw std::runtime_error("Discord plugin service not opened");auto details=text(0),status=text(1);svc_receive(*this);if(!discordReady)return Value{};if(discordActivitySent&&details==discordDetails&&status==discordState)return Value::from_int(1);auto command="{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":"+std::to_string(::getpid())+",\"activity\":{\"details\":"+svc_json(details)+",\"state\":"+svc_json(status)+"}},\"nonce\":\""+std::to_string(discordNonce++)+"\"}";if(!svc_send(*this,1,command))return Value{};discordDetails=details;discordState=status;discordActivitySent=true;return Value::from_int(1);}
 if(name=="key_pressed"){
  count(1);int code=raw(0)>>10;
  if(code>=0&&code<127){
   // Patch GenelAyarlar.wdl Tuslari_Esitle: with the FPS camera, mouse right/left also hold aim (GA_SilahKaldir 4) and fire (GA_Ates 5).
   if(firstPersonActive_)if(auto keys=global("GA_Tuslar");keys.kind==Value::Kind::Object)if(auto* bound=state.get(keys.bits);bound&&bound->array.size()>5){
    if((code==bound->array[4].value.integer()&&number("mouse_right")!=0)||(code==bound->array[5].value.integer()&&number("mouse_left")!=0))return Value::from_int(1);
   }
   return key_state(code);
  }
  constexpr std::array<std::string_view,24> joy{"joy_1","joy_2","joy_3","joy_4","joy_5","joy_6","joy_7","joy_8","joy_9","joy_10","joy_11","joy_12","joy2_1","joy2_2","joy2_3","joy2_4","joy2_5","joy2_6","joy2_7","joy2_8","joy2_9","joy2_10","joy2_11","joy2_12"};if(code>=256&&code<280)return global(joy[code-256]);if(code==280)return global("mouse_left");if(code==281)return global("mouse_right");if(code==282)return global("mouse_middle");return Value{};
 }
 if(name=="video_window"){count(4);Vec3 pos{};if(a[0].current())pos=vector(a[0]);if(a[1].current())throw std::runtime_error("non-NULL legacy video_window size not used by Kabus22");windowStyle=integer(2);if(windowStyle!=0&&windowStyle!=1&&windowStyle!=16)throw std::runtime_error("unsupported legacy window style");if(renderer){renderer->set_title(text(3));if(a[0].current())renderer->set_position(int(pos.x),int(pos.y));}else (void)text(3);return Value::from_int(1);}
 if(nativeDisplay_&&name=="video_set"){count(4);int w=integer(0),h=integer(1),depth=integer(2),mode=integer(3);if(w<=0||h<=0||w>16384||h>16384||(depth!=16&&depth!=24&&depth!=32)||mode<0||mode>2)throw std::runtime_error("invalid video mode");prepare_display();return Value::from_int(1);}
 if(name=="video_set"){count(4);int w=integer(0),h=integer(1),depth=integer(2),mode=integer(3);if(w<=0||h<=0||w>16384||h>16384||(depth!=16&&depth!=24&&depth!=32)||mode<0||mode>2)throw std::runtime_error("invalid video mode");auto windowMode=mode==0?currentWindowMode:(mode==1?WindowMode::Fullscreen:(windowStyle==1?WindowMode::Borderless:WindowMode::Windowed));if(renderer)renderer->set_mode(w,h,windowMode);currentWindowMode=windowMode;set_global("video_screen",ret(windowMode==WindowMode::Fullscreen?1:2));auto screenSize=global_vector("screen_size");set_vector(screenSize,{float(w),float(h),vector(screenSize).z});currentWidth_=w;currentHeight_=h;return Value::from_int(1);}
 if(nativeDisplay_&&name=="video_switch"){count(3);int mode=integer(0),depth=integer(1),screen=integer(2);if(mode<6||mode>10||(depth!=16&&depth!=24&&depth!=32)||screen<0||screen>2)throw std::runtime_error("unsupported video_switch mode");prepare_display();set_global("video_mode",ret(mode));return ret(mode);}
 if(name=="video_switch"){count(3);int mode=integer(0),depth=integer(1),screen=integer(2);if(mode<6||mode>10||(depth!=16&&depth!=24&&depth!=32)||screen<0||screen>2)throw std::runtime_error("unsupported video_switch mode");auto dimensions=a6VideoModes[mode-6];auto windowMode=screen==0?currentWindowMode:(screen==1?WindowMode::Fullscreen:WindowMode::Windowed);if(renderer)renderer->set_mode(dimensions[0],dimensions[1],windowMode);currentWindowMode=windowMode;currentWidth_=dimensions[0];currentHeight_=dimensions[1];auto screenSize=global_vector("screen_size");set_vector(screenSize,{float(currentWidth_),float(currentHeight_),vector(screenSize).z});set_global("video_mode",ret(mode));set_global("video_screen",ret(screen==0?(windowMode==WindowMode::Fullscreen?1:2):screen));return ret(mode);}
 if(name=="sys_metrics"){count(1);auto metric=integer(0);if(metric!=0&&metric!=1)throw std::runtime_error("unsupported system metric");SDL_DisplayMode display{};if(SDL_GetDesktopDisplayMode(0,&display)==0)return ret(metric?display.h:display.w);if(!renderer&&currentWidth_>0&&currentHeight_>0)return ret(metric?currentHeight_:currentWidth_);throw std::runtime_error(std::string("cannot query display metrics: ")+SDL_GetError());}
 if(name=="add_resource"){count(1);assets.mount(text(0));return Value::from_int(1);}
 if(name=="bmap_width"||name=="bmap_height"){count(1);const auto& image=svc_bitmap(*this,a[0].current());return ret(name=="bmap_width"?image.width:image.height);}
 if(name=="bmap_purge"){count(1);auto value=a[0].current();const auto& image=svc_bitmap(*this,value);if(renderer)renderer->purge_image(&image);bitmapImages.erase(value.bits);const auto& resource=state.require(value.bits).text;if(!resource.empty()&&assets.contains(resource))assets.purge_image(resource);return Value{};}
 if(name=="error"){count(1);throw std::runtime_error(std::string(text(0)));}
 if(name=="vec_to_screen"){
  count(2);auto p=vector(a[0]);auto value=a[1].current();if(value.kind!=Value::Kind::Object)throw std::runtime_error("view required for vec_to_screen");auto& view=state.require(value.bits);if(view.kind!=Object::Kind::View)throw std::runtime_error("vec_to_screen requires view");auto setting=[&](std::string_view key,double fallback){auto it=view.fields.find(key);return it==view.fields.end()?fallback:it->second.value.real();};
  if(nativeDisplay_&&view.id==global("camera").bits){set_field(view.id,"size_x",currentWidth_);set_field(view.id,"size_y",currentHeight_);}
  Camera camera;camera.position=position(view.id);camera.angles=angles(view.id);camera.horizontalFov=float(setting("arc",60));camera.pixelAspect=float(setting("aspect",1));camera.nearClip=float(std::max(.01,setting("clip_near",1)));camera.farClip=float(std::max(double(camera.nearClip)+.01,setting("clip_far",10000)));camera.viewport={float(setting("pos_x",0)),float(setting("pos_y",0)),float(setting("size_x",0)),float(setting("size_y",0))};camera.widescreen=false;
  camera.pixelAspect=display_aspect(camera.pixelAspect);
  // K22.exe 00404020 -> 0044ee20: the result depends only on depth and the view rectangle; the view's VISIBLE flag is not read.
  WindowMetrics metrics=renderer?renderer->metrics():WindowMetrics{currentWidth_,currentHeight_,currentWidth_,currentHeight_,1,1};auto result=Renderer::project(p,camera,metrics);set_vector(a[0],result);auto width=camera.viewport.width>0?camera.viewport.width:float(metrics.width),height=camera.viewport.height>0?camera.viewport.height:float(metrics.height);
  return Value::from_int(result.z>=camera.nearClip&&result.z<=camera.farClip&&result.x>=camera.viewport.x&&result.x<camera.viewport.x+width&&result.y>=camera.viewport.y&&result.y<camera.viewport.y+height);
 }
 if(name=="dl_reset"){count(0);if(!pluginGamma)throw std::runtime_error("gamma service not opened");for(auto& channel:nativeAdjustment.gammaRamp)for(std::size_t i=0;i<channel.size();++i)channel[i]=std::uint16_t(i*257);nativeAdjustment.enabled=true;return Value{};}
 if(name=="dl_pakaga"){count(4);if(!pluginGamma)throw std::runtime_error("gamma service not opened");auto b=raw(0)>>10,c=(raw(1)>>10)+100,g=raw(2)>>10,mode=raw(3)>>10;for(std::uint32_t i=0;i<256;++i){auto product=std::bit_cast<std::int32_t>((255u-i)*i*std::uint32_t(g));auto n=std::uint16_t(std::int64_t(std::trunc(i+double(product)/25600.0)));double contrast=std::clamp((int(n)-128)*(c*0.01)+128,0.0,255.0);auto v=std::clamp(int(std::uint16_t(std::trunc(contrast)))+b,0,255);auto w=std::uint16_t(v*257);for(int channel=0;channel<3;++channel)nativeAdjustment.gammaRamp[channel][i]=mode==1?std::uint16_t(65535-w):((mode>=2&&mode<=4&&channel!=mode-2)?0:w);}nativeAdjustment.enabled=true;return Value{};}
 if(name=="file_open_read"||name=="file_open_write"){
  count(1);auto relative=svc_relative(text(0));auto path=svc_private(saveRoot,relative);FileData data;
  // Save slots/index: rotate the previous file into backups/, then replace atomically at file_close (no truncating pre-flush).
  if(name=="file_open_write"){data.writable=true;data.output=path;if(saves::rotated(relative.filename().string()))saves::backup(saveRoot,relative);else svc_flush(data);}
  else if(std::filesystem::exists(path)){if(saves::slot_number(relative.filename().string())>=0){saves::migrate_meta(path,{saves::MetaFormat,K22_VERSION,"","","","",""},std::cerr);read_saved_position(path);}data.bytes=svc_read_file(path);}
  else if(assets.contains(relative.generic_string()))data.bytes=assets.read(relative.generic_string());
  else if(relative.parent_path()=="_data/saves"&&assets.contains(relative.filename().string()))data.bytes=assets.read(relative.filename().string());
  else return Value{};
  if(!data.writable&&!data.bytes.empty()&&core_equal(relative.filename().string(),"options.k22")&&display_admitted())authoredResolution_=data.bytes[0];
  if(nextFile>=2097152)throw std::runtime_error("file handle range exhausted");auto handle=nextFile++;files.emplace(handle,std::move(data));return ret(handle);
 }
 if(name=="file_close"){count(1);auto& data=file();
  // GA_AyarlariKaydet writes the in-memory native tier; persist the user's authored GA_Cozunurluk instead.
  if(data.writable&&!data.bytes.empty()&&core_equal(data.output.filename().string(),"options.k22")&&display_admitted())data.bytes[0]=authoredResolution_;
  svc_flush(data);
  if(data.writable&&saves::slot_number(data.output.filename().string())>=0){const auto& entry=runtime.compiler_settings().entry;if(!saveScriptKnown_){if(assets.contains(entry))saveScript_=saves::fnv1a64(assets.read(entry));saveScriptKnown_=true;}saves::write_meta(data.output,{saves::MetaFormat,K22_VERSION,saves::now_utc(),entry,saveScript_,"native",quickSaving?saved_position():"",characterLock});}
  files.erase(std::uint32_t(integer(0)));return Value{};}
 if(name=="file_asc_read"){count(1);auto& data=file();if(data.writable)throw std::runtime_error("file handle is write-only");if(data.cursor==data.bytes.size())return Value::from_int(-1);return Value::from_int(data.bytes[data.cursor++]);}
 if(name=="file_asc_write"){count(2);auto& data=file();auto byte=std::uint8_t(raw(1)>>10);svc_append(data,std::string_view(reinterpret_cast<const char*>(&byte),1));return Value::from_int(byte);}
 if(name=="file_str_write"){count(2);auto& data=file();auto source=text(1);auto nul=source.find('\0');if(nul!=std::string_view::npos)source=source.substr(0,nul);svc_append(data,source);return ret(source.size());}
 if(name=="file_var_write"){count(2);auto& data=file();auto n=raw(1);char buffer[64];int length=n%1024==0?std::snprintf(buffer,sizeof buffer,"%d ",n>>10):std::snprintf(buffer,sizeof buffer,"%.3f ",double(float(n)/1024.0f));if(length<0||length>=int(sizeof buffer))throw std::runtime_error("file number formatting failed");svc_append(data,std::string_view(buffer,std::size_t(length)));return ret(length);}
 if(name=="file_var_read"){count(1);auto& data=file();if(data.writable)throw std::runtime_error("file handle is write-only");auto whitespace=[](std::uint8_t c){return c==' '||(c>='\t'&&c<='\r');};while(data.cursor<data.bytes.size()&&whitespace(data.bytes[data.cursor]))++data.cursor;if(data.cursor==data.bytes.size())return Value{};auto begin=reinterpret_cast<const char*>(data.bytes.data()+data.cursor),end=reinterpret_cast<const char*>(data.bytes.data()+data.bytes.size());bool positive=*begin=='+';if(positive)++begin;float n=0;auto parsed=std::from_chars(begin,end,n);if(parsed.ec!=std::errc{}||parsed.ptr==begin||!std::isfinite(n))throw std::runtime_error("malformed file number");data.cursor=std::size_t(parsed.ptr-reinterpret_cast<const char*>(data.bytes.data()));while(data.cursor<data.bytes.size()&&whitespace(data.bytes[data.cursor]))++data.cursor;return ret(double(n));}
 if(name=="file_str_read"){count(2);auto& data=file();if(data.writable)throw std::runtime_error("file handle is write-only");while(data.cursor<data.bytes.size()&&data.bytes[data.cursor]<32)++data.cursor;if(data.cursor==data.bytes.size())return Value::from_int(-1);auto& target=mutable_string(a[1]);auto delimiter=global("delimit_str");unsigned char stop=',';if(delimiter.kind==Value::Kind::Object){auto s=string(delimiter);stop=s.empty()?0:static_cast<unsigned char>(s.front());}std::size_t start=data.cursor++,total=1;while(data.cursor<data.bytes.size()){auto c=data.bytes[data.cursor++];if(c<32||c==stop)break;++total;}target.assign(reinterpret_cast<const char*>(data.bytes.data()+start),std::min(total,std::size_t(4095)));state.require(a[1].current().bits).nullStringBuffer=false;return ret(total);}
 return std::nullopt;
}
} // namespace k22

#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
namespace k22 {
Vec3 worldModelVertex(const Engine&,ObjectId,const EntityData&,std::size_t);
namespace {
constexpr float worldPi=3.14159265358979323846f;
Vec3 wAdd(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 wSub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 wMul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float wDot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 wCross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float wLength(Vec3 a){return std::sqrt(wDot(a,a));}
Vec3 wUnit(Vec3 a){float n=wLength(a);return n>0?wMul(a,1/n):Vec3{};}
Vec3 wRotate(Vec3 v,Vec3 a){float p=a.x*worldPi/180,t=a.y*worldPi/180,r=a.z*worldPi/180;float cp=std::cos(p),sp=std::sin(p),ct=std::cos(t),st=std::sin(t),cr=std::cos(r),sr=std::sin(r);Vec3 q{v.x,cr*v.y-sr*v.z,sr*v.y+cr*v.z};q={ct*q.x-st*q.z,q.y,st*q.x+ct*q.z};return {cp*q.x-sp*q.y,sp*q.x+cp*q.y,q.z};}
Vec3 wInverseRotate(Vec3 v,Vec3 a){float p=-a.x*worldPi/180,t=-a.y*worldPi/180,r=-a.z*worldPi/180;Vec3 q{std::cos(p)*v.x-std::sin(p)*v.y,std::sin(p)*v.x+std::cos(p)*v.y,v.z};q={std::cos(t)*q.x-std::sin(t)*q.z,q.y,std::sin(t)*q.x+std::cos(t)*q.z};return {q.x,std::cos(r)*q.y-std::sin(r)*q.z,std::sin(r)*q.y+std::cos(r)*q.z};}
Vec3 wBasisRotate(Vec3 p,const std::array<Vec3,3>& r){return {(r[2].x*p.z+r[1].x*p.y)+r[0].x*p.x,(r[2].y*p.z+r[1].y*p.y)+r[0].y*p.x,(r[2].z*p.z+r[1].z*p.y)+r[0].z*p.x};}
Vec3 wMin(Vec3 a,Vec3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
Vec3 wMax(Vec3 a,Vec3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
Vec3 wScale(const Engine& e,ObjectId id){return {float(e.field(id,"scale_x")),float(e.field(id,"scale_y")),float(e.field(id,"scale_z"))};}
Vec3 wWorldVertex(const Engine& e,ObjectId id,const EntityData& d,std::size_t i){Vec3 p=worldModelVertex(e,id,d,i),s=wScale(e,id);return wAdd(e.position(id),wRotate({p.x*s.x,p.y*s.y,p.z*s.z},e.angles(id)));}
bool wSweepBox(Vec3 start,Vec3 delta,Vec3 extent,Vec3 lo,Vec3 hi,float& fraction,Vec3& normal){lo=wSub(lo,extent);hi=wAdd(hi,extent);float enter=0,leave=fraction;Vec3 n{};const float a[3]={start.x,start.y,start.z},v[3]={delta.x,delta.y,delta.z},l[3]={lo.x,lo.y,lo.z},h[3]={hi.x,hi.y,hi.z};bool inside=true;for(int i=0;i<3;++i){inside&=a[i]>l[i]+1e-5f&&a[i]<h[i]-1e-5f;if(std::abs(v[i])<1e-10f){if(a[i]<l[i]||a[i]>h[i])return false;continue;}float t0=(l[i]-a[i])/v[i],t1=(h[i]-a[i])/v[i],sign=v[i]>0?-1.f:1.f;if(t0>t1)std::swap(t0,t1);if(t0>=enter){enter=t0;n={};if(i==0)n.x=sign;else if(i==1)n.y=sign;else n.z=sign;}leave=std::min(leave,t1);if(enter>leave)return false;}if(leave<0||enter>fraction)return false;if(inside){normal=wUnit(wMul(delta,-1));fraction=0;return true;}if(wDot(n,delta)>=0)return false;fraction=std::max(0.f,enter);normal=n;return true;}
bool wPublicHullRay(Vec3 start,Vec3 direction,Vec3 center,const std::array<Vec3,3>& axes,Vec3 half,float& distance,Vec3& normal){
 // 10080150: an inside/boundary start selects the exit, with no local range clipping.
 float enter=-999999.f,leave=999999.f;Vec3 entryNormal{},exitNormal{};bool exitKnown=false;
 const std::array extents{half.x,half.y,half.z};const auto offset=wSub(center,start);const auto epsilon=std::bit_cast<float>(0x38d1b717u);
 for(std::size_t i=0;i<3;++i){const auto axis=axes[i];const float p=float((static_cast<long double>(axis.z)*offset.z+static_cast<long double>(axis.y)*offset.y)+static_cast<long double>(axis.x)*offset.x),v=float((static_cast<long double>(axis.x)*direction.x+static_cast<long double>(axis.z)*direction.z)+static_cast<long double>(axis.y)*direction.y),h=extents[i];
  if(std::abs(v)<=epsilon){if(p<-h||p>h)return false;continue;}
  const float reciprocal=1.f/v;float near=float((static_cast<long double>(p)-h)*reciprocal),far=float((static_cast<long double>(p)+h)*reciprocal);if(near>far)std::swap(near,far);
  if(near>enter){enter=near;entryNormal=wMul(axis,v>=0?-1.f:1.f);}if(far<leave){leave=far;exitNormal=wMul(axis,v<=0?-1.f:1.f);exitKnown=true;}
  if(enter>leave||leave<0)return false;
 }
 if(enter>0){distance=enter;normal=entryNormal;}else{if(!exitKnown)throw std::runtime_error("A7 hull ray has an indeterminate source exit normal (10080150)");distance=leave;normal=exitNormal;}
 return true;
}
// A7 10068440's stack packets and 10080ee0's source-ordered contact algorithm.
// A6 00467530 / A7 1006d370: float length, normalized components and strict snapping.
float wNormalizeMovement(Vec3& value){
 const float squared=float((static_cast<long double>(value.x)*value.x+static_cast<long double>(value.y)*value.y)+static_cast<long double>(value.z)*value.z);
 const float length=float(std::sqrt(double(squared))),epsilon=std::bit_cast<float>(0x38d1b717u);
 if(length<=epsilon){value={};return 0;}
 for(float* component:{&value.x,&value.y,&value.z}){*component=float(static_cast<long double>(*component)/length);if(std::abs(*component)<epsilon)*component=0;}
 return length;
}
struct WPublicHull {Vec3 center;std::array<Vec3,3> axes;Vec3 half;};
using WHullMatrix=std::array<std::array<float,3>,3>;
long double wHullDot(Vec3 a,Vec3 b){return (static_cast<long double>(a.z)*b.z+static_cast<long double>(a.y)*b.y)+static_cast<long double>(a.x)*b.x;}
std::int32_t wQueryFixedRaw(float x){const float scaled=x*1024;return std::isfinite(scaled)&&scaled>=-2147483648.f&&scaled<2147483648.f?std::int32_t(std::trunc(scaled)):INT32_MIN;}
float wQueryFixed(float x){return float(wQueryFixedRaw(x))/1024;}
Vec3 wHullFaceMeans(const WPublicHull& hull,const WPublicHull& clip,unsigned& faces){
 const std::array half{hull.half.x,hull.half.y,hull.half.z},otherHalf{clip.half.x,clip.half.y,clip.half.z};
 const std::array scaled{wMul(hull.axes[0],half[0]),wMul(hull.axes[1],half[1]),wMul(hull.axes[2],half[2])};
 std::array<Vec3,8> corners;constexpr std::array<unsigned,8> bits{0,1,3,2,4,5,7,6};
 for(unsigned i=0;i<8;++i){auto p=hull.center;for(unsigned j=0;j<3;++j)p=(bits[i]&(1u<<j))?wAdd(p,scaled[j]):wSub(p,scaled[j]);corners[i]=p;}
 constexpr std::array<std::array<unsigned,4>,6> indices{{{0,1,2,3},{7,6,5,4},{1,5,6,2},{7,3,0,4},{0,1,5,4},{2,3,7,6}}};
 // Six clips of four vertices emit at most 42 vertices, even with nonconvex rounded classifications.
 std::array<float,64*3> first,second;std::array<float,64> distances;std::array<unsigned char,64> classes;
 const auto epsilon=std::bit_cast<float>(0x38d1b717u);Vec3 means{};faces=0;
 for(const auto& face:indices){auto* in=first.data();auto* out=second.data();std::size_t count=4;
  for(unsigned i=0;i<4;++i){const auto p=corners[face[i]];in[i*3]=p.x;in[i*3+1]=p.y;in[i*3+2]=p.z;}
  for(unsigned plane=0;plane<6&&count;++plane){const auto j=plane%3;const auto axis=clip.axes[j];const bool negative=plane<3;const auto n=negative?wMul(axis,-1):axis;const std::array normal{n.x,n.y,n.z};
   const long double centerDot=(static_cast<long double>(clip.center.y)*axis.y+static_cast<long double>(clip.center.z)*axis.z)+static_cast<long double>(clip.center.x)*axis.x;
   const float constant=float((negative?-centerDot:centerDot)-otherHalf[j]);unsigned positives=0,negatives=0;
   for(std::size_t i=0;i<count;++i){const auto* p=in+i*3;const float d=float(((static_cast<long double>(p[2])*n.z+static_cast<long double>(p[1])*n.y)+static_cast<long double>(p[0])*n.x)-constant);distances[i]=d;classes[i]=d>epsilon?0:d<-epsilon?1:2;positives+=classes[i]==0;negatives+=classes[i]==1;}
   if(!negatives)continue;if(!positives){count=0;break;}
   std::size_t emitted=0;
   for(std::size_t i=0;i<count;++i){const auto next=(i+1)%count;const auto* p=in+i*3;const auto* q=in+next*3;
    if(classes[i]!=1){for(unsigned k=0;k<3;++k)out[emitted*3+k]=p[k];++emitted;}
    if(classes[i]==2||classes[next]==2||classes[i]==classes[next])continue;
    const float t=float(static_cast<long double>(distances[i])/(static_cast<long double>(distances[i])-distances[next]));
    for(unsigned k=0;k<3;++k){const auto raw=std::bit_cast<std::uint32_t>(normal[k]);out[emitted*3+k]=raw==0x3f800000u?constant:raw==0xbf800000u?-constant:float((static_cast<long double>(q[k])-p[k])*t+p[k]);}++emitted;
   }
   count=emitted;std::swap(in,out);
  }
  if(!count)continue;
  Vec3 sum{};for(std::size_t i=0;i<count;++i)sum=wAdd(sum,{in[i*3],in[i*3+1],in[i*3+2]});const float inverse=1.f/float(count);const auto centroid=wMul(sum,inverse);means={float(static_cast<long double>(means.x)+centroid.x),float(static_cast<long double>(means.y)+centroid.y),float(static_cast<long double>(means.z)+centroid.z)};++faces;
 }
 return means;
}
Vec3 wHullOverlapPoint(const WPublicHull& a,const WPublicHull& b){
 unsigned countA,countB;const auto meansA=wHullFaceMeans(a,b,countA),meansB=wHullFaceMeans(b,a,countB);
 const bool selectB=countB<countA;const auto count=selectB?countB:countA;if(!count)return selectB?a.center:b.center;const auto point=selectB?meansB:meansA;return count>1?wMul(point,1.f/float(count)):point;
}
Vec3 wHullSupportPoint(const WPublicHull& a,const WPublicHull& b,Vec3 d,const WHullMatrix& r,int id){
 const std::array ah{a.half.x,a.half.y,a.half.z},bh{b.half.x,b.half.y,b.half.z};std::array<float,3> u{},v{};
 auto sign=[](float x){return x<0?-1.f:1.f;};
 auto sum=[&](const std::array<float,3>& coefficients,const WPublicHull& hull){Vec3 p{};for(unsigned k=0;k<3;++k){const auto product=wMul(hull.axes[k],coefficients[k]);p={float(static_cast<long double>(p.x)+product.x),float(static_cast<long double>(p.y)+product.y),float(static_cast<long double>(p.z)+product.z)};}return wAdd(p,hull.center);};
 if(id<3){const auto axis=a.axes[id];const float s=((static_cast<long double>(axis.x)*d.x+static_cast<long double>(axis.y)*d.y)+static_cast<long double>(axis.z)*d.z)>0?1.f:-1.f;
  for(unsigned j=0;j<3;++j){if(r[id][j]!=0)v[j]=-sign(r[id][j])*bh[j]*s;else{float radius=0;for(unsigned k=0;k<3;++k)radius=float(static_cast<long double>(radius)+static_cast<long double>(std::abs(r[k][j]))*ah[k]);const auto axisB=b.axes[j];const long double q=-((static_cast<long double>(axisB.y)*d.y+static_cast<long double>(axisB.z)*d.z)+static_cast<long double>(axisB.x)*d.x);float lo=float(q-radius),hi=float(q+radius);if(hi>=bh[j])hi=bh[j];if(lo<=-bh[j])lo=-bh[j];v[j]=float((static_cast<long double>(hi)+lo)*.5L);}}
  return sum(v,b);
 }
 if(id<6){const unsigned j=id-3;const auto axis=b.axes[j];const float s=((static_cast<long double>(axis.x)*d.x+static_cast<long double>(axis.y)*d.y)+static_cast<long double>(axis.z)*d.z)>0?1.f:-1.f;
  for(unsigned i=0;i<3;++i){if(r[i][j]!=0)u[i]=sign(r[i][j])*ah[i]*s;else{float radius=0;for(unsigned k=0;k<3;++k)radius=float(static_cast<long double>(radius)+static_cast<long double>(std::abs(r[i][k]))*bh[k]);const auto axisA=a.axes[i];const long double q=(static_cast<long double>(axisA.y)*d.y+static_cast<long double>(axisA.x)*d.x)+static_cast<long double>(axisA.z)*d.z;float lo=float(q-radius),hi=float(q+radius);if(hi>=ah[i])hi=ah[i];if(lo<=-ah[i])lo=-ah[i];u[i]=float((static_cast<long double>(hi)+lo)*.5L);}}
  return sum(u,a);
 }
 if(id==6)return a.center;
 // 1007fbf0's original dispatch is shifted; IDs9/10 intentionally share A0/B2.
 constexpr std::array<std::array<unsigned,2>,8> pairs{{{0,0},{0,1},{0,2},{0,2},{1,0},{1,1},{1,2},{2,0}}};const auto i=pairs[id-7][0],j=pairs[id-7][1];const auto x=a.axes[i],y=b.axes[j];
 const Vec3 cross{float(static_cast<long double>(x.y)*y.z-static_cast<long double>(x.z)*y.y),float(static_cast<long double>(x.z)*y.x-static_cast<long double>(x.x)*y.z),float(static_cast<long double>(x.x)*y.y-static_cast<long double>(x.y)*y.x)};
 const float s=((static_cast<long double>(cross.y)*d.y+static_cast<long double>(cross.z)*d.z)+static_cast<long double>(cross.x)*d.x)>0?1.f:-1.f;unsigned q1=0,q2=0,c1=0,c2=0;float denominator=r[i][j];
 switch(id){
  case 7:v[1]=-sign(r[0][2])*bh[1]*s;v[2]=sign(r[0][1])*bh[2]*s;u[1]=-sign(r[2][0])*ah[1]*s;u[2]=sign(r[1][0])*ah[2]*s;q1=1;q2=2;c1=2;c2=1;break;
  case 8:v[0]=sign(r[0][2])*bh[0]*s;v[2]=-sign(r[0][0])*bh[2]*s;u[1]=-sign(r[2][1])*ah[1]*s;u[2]=sign(r[1][1])*ah[2]*s;q1=2;q2=1;c1=0;c2=2;break;
  case 9:case 10:v[0]=-sign(r[0][1])*bh[0]*s;v[1]=sign(r[0][0])*bh[1]*s;u[1]=-sign(r[2][2])*ah[1]*s;u[2]=sign(r[1][2])*ah[2]*s;q1=2;q2=1;c1=0;c2=1;denominator=r[0][1];break;
  case 11:v[1]=-sign(r[1][2])*bh[1]*s;v[2]=sign(r[1][1])*bh[2]*s;u[0]=sign(r[2][0])*ah[0]*s;u[2]=-sign(r[0][0])*ah[2]*s;q1=0;q2=2;c1=2;c2=1;break;
  case 12:v[0]=sign(r[1][2])*bh[0]*s;v[2]=-sign(r[1][0])*bh[2]*s;u[0]=sign(r[2][1])*ah[0]*s;u[2]=-sign(r[0][1])*ah[2]*s;q1=2;q2=0;c1=0;c2=2;break;
  case 13:v[0]=-sign(r[1][1])*bh[0]*s;v[1]=sign(r[1][0])*bh[1]*s;u[0]=sign(r[2][2])*ah[0]*s;u[2]=-sign(r[0][2])*ah[2]*s;q1=2;q2=0;c1=0;c2=1;break;
  case 14:v[1]=-sign(r[2][2])*bh[1]*s;v[2]=sign(r[2][1])*bh[2]*s;u[0]=-sign(r[1][0])*ah[0]*s;u[1]=-sign(r[0][0])*ah[1]*s;q1=1;q2=0;c1=2;c2=1;break;
 }
 const auto baseB=wHullDot(wMul(b.axes[j],-1),d);const float l=float(((baseB+static_cast<long double>(u[q1])*r[q1][j])+static_cast<long double>(u[q2])*r[q2][j])*r[i][j]);
 const float numerator=float(((wHullDot(a.axes[i],d)+static_cast<long double>(v[c1])*r[i][c1])+static_cast<long double>(v[c2])*r[i][c2])+l);
 u[i]=float(static_cast<long double>(numerator)/(1.L-static_cast<long double>(denominator)*denominator));return sum(u,a);
}
bool wPublicHullBox(const WPublicHull& a,const WPublicHull& b,Vec3 direction,float horizon,float& scalar,Vec3& point,Vec3& normal){
 WHullMatrix r,q;std::array<float,3> d,w;const std::array ah{a.half.x,a.half.y,a.half.z},bh{b.half.x,b.half.y,b.half.z};const auto offset=wSub(b.center,a.center),velocity=wMul(direction,-1);const auto parallel=std::bit_cast<float>(0x3f7ff972u),epsilon=std::bit_cast<float>(0x38d1b717u);
 const bool stationary=horizon==0;bool skipCross=false;int selected=-1;float exit=std::numeric_limits<float>::max();scalar=0;
 auto interval=[&](float amin,float amax,float bmin,float bmax,float v,int id){
  if(bmax<=amin){if(v<=0)return false;const float inverse=1.f/v,incoming=float((static_cast<long double>(amin)-bmax)*inverse);if(incoming>scalar){scalar=incoming;selected=id;}if(scalar>horizon)return false;const float outgoing=float((static_cast<long double>(amax)-bmin)*inverse);if(outgoing<exit)exit=outgoing;}
  else if(amax<=bmin){if(v>=0)return false;const float inverse=1.f/v,incoming=float((static_cast<long double>(amax)-bmin)*inverse);if(incoming>scalar){scalar=incoming;selected=id;}if(scalar>horizon)return false;const float outgoing=float((static_cast<long double>(amin)-bmax)*inverse);if(outgoing<exit)exit=outgoing;}
  else if(v>0){const float outgoing=float((static_cast<long double>(amax)-bmin)/v);if(outgoing<exit){exit=outgoing;scalar=-outgoing;}}
  else if(v<0){const float outgoing=float((static_cast<long double>(amin)-bmax)/v);if(outgoing<exit){exit=outgoing;scalar=-outgoing;}}
  return true;
 };
 for(unsigned i=0;i<3;++i){for(unsigned j=0;j<3;++j){r[i][j]=float(wHullDot(a.axes[i],b.axes[j]));q[i][j]=std::abs(r[i][j]);skipCross|=q[i][j]>parallel;}d[i]=float(wHullDot(offset,a.axes[i]));if(!stationary)w[i]=float(wHullDot(velocity,a.axes[i]));const float radius=float((static_cast<long double>(bh[2])*q[i][2]+static_cast<long double>(bh[1])*q[i][1])+static_cast<long double>(bh[0])*q[i][0]);
  if(stationary){if(static_cast<long double>(std::abs(d[i]))>static_cast<long double>(ah[i])+radius)return false;}else if(!interval(-ah[i],ah[i],float(static_cast<long double>(d[i])-radius),float(static_cast<long double>(d[i])+radius),w[i],i))return false;
 }
 for(unsigned j=0;j<3;++j){const float radius=float((static_cast<long double>(ah[1])*q[1][j]+static_cast<long double>(ah[2])*q[2][j])+static_cast<long double>(ah[0])*q[0][j]),projection=float(wHullDot(offset,b.axes[j]));
  if(stationary){if(static_cast<long double>(std::abs(projection))>static_cast<long double>(radius)+bh[j])return false;}else if(!interval(-radius,radius,float(static_cast<long double>(projection)-bh[j]),float(static_cast<long double>(projection)+bh[j]),float(wHullDot(velocity,b.axes[j])),j+3))return false;
 }
 if(!skipCross)for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){const auto k=(i+1)%3,l=(i+2)%3,m=(j+1)%3,n=(j+2)%3;const float center=float(static_cast<long double>(r[k][j])*d[l]-static_cast<long double>(r[l][j])*d[k]),v=stationary?0:float(static_cast<long double>(r[k][j])*w[l]-static_cast<long double>(r[l][j])*w[k]),radiusA=float(static_cast<long double>(ah[k])*q[l][j]+static_cast<long double>(ah[l])*q[k][j]),radiusB=float(static_cast<long double>(bh[m])*q[i][n]+static_cast<long double>(bh[n])*q[i][m]);
  if(stationary){if(static_cast<long double>(std::abs(center))>static_cast<long double>(radiusA)+radiusB)return false;}else if(!interval(-radiusA,radiusA,float(static_cast<long double>(center)-radiusB),float(static_cast<long double>(center)+radiusB),v,6+i*3+j))return false;
 }
 if(!stationary&&scalar>exit)return false;
 point=selected>=0?wHullSupportPoint(a,b,offset,r,selected):wHullOverlapPoint(a,b);
 const auto relative=wSub(point,b.center);float closest=std::numeric_limits<float>::max();bool normalKnown=false;
 for(unsigned face=0;face<6;++face){const auto j=face%3;const auto n=face<3?b.axes[j]:wMul(b.axes[j],-1);const float gap=float(wHullDot(relative,n)-bh[j]),magnitude=std::abs(gap);if(!(magnitude<closest))continue;const float incidence=std::abs(float(wHullDot(n,direction)));if(stationary?!(incidence<parallel):!(incidence>epsilon))continue;closest=magnitude;normal=n;normalKnown=true;if(gap<=0)scalar=gap;}
 if(!normalKnown)throw std::runtime_error("A7 public BOX has an indeterminate source normal (1007fc20)");return true;
}
bool wSweepCylinder(Vec3 start,Vec3 delta,Vec3 extent,Vec3 lo,Vec3 hi,float& fraction,Vec3& normal){Vec3 center=wMul(wAdd(lo,hi),.5f);float radius=std::max((hi.x-lo.x)*.5f,(hi.y-lo.y)*.5f)+std::max(extent.x,extent.y);Vec3 p=wSub(start,center);float zlo=lo.z-extent.z,zhi=hi.z+extent.z;float enter=0,leave=fraction;Vec3 n{};float a=delta.x*delta.x+delta.y*delta.y,b=2*(p.x*delta.x+p.y*delta.y),c=p.x*p.x+p.y*p.y-radius*radius;if(a<1e-12f){if(c>0)return false;}else{float disc=b*b-4*a*c;if(disc<0)return false;float root=std::sqrt(disc),t0=(-b-root)/(2*a),t1=(-b+root)/(2*a);if(t0>=enter){enter=t0;Vec3 q=wAdd(p,wMul(delta,t0));n=wUnit({q.x,q.y,0});}leave=std::min(leave,t1);}if(std::abs(delta.z)<1e-12f){if(start.z<zlo||start.z>zhi)return false;}else{float t0=(zlo-start.z)/delta.z,t1=(zhi-start.z)/delta.z,sign=delta.z>0?-1.f:1.f;if(t0>t1)std::swap(t0,t1);if(t0>=enter){enter=t0;n={0,0,sign};}leave=std::min(leave,t1);}if(enter>leave||leave<0||enter>fraction)return false;if(c<0&&start.z>zlo&&start.z<zhi&&enter==0)n=wUnit(wMul(delta,-1));if(wDot(n,delta)>=0)return false;fraction=std::max(0.f,enter);normal=n;return true;}
// Continuous separating-axis test: the moving box is checked against the actual triangle, not its AABB.
// Project relative to the sweep origin, as the ellipsoid path does: world-space dot subtraction can make redundant SAT axes disagree for short rays against large translated triangles.
bool wSweepTriangle(Vec3 start,Vec3 delta,Vec3 extent,Vec3 a,Vec3 b,Vec3 c,float& fraction,Vec3& normal){Vec3 e0=wSub(b,a),e1=wSub(c,b),e2=wSub(a,c),face=wCross(e0,wSub(c,a));if(wDot(face,face)<1e-16f)return false;std::array<Vec3,13> axes{{{1,0,0},{0,1,0},{0,0,1},face,wCross(e0,{1,0,0}),wCross(e0,{0,1,0}),wCross(e0,{0,0,1}),wCross(e1,{1,0,0}),wCross(e1,{0,1,0}),wCross(e1,{0,0,1}),wCross(e2,{1,0,0}),wCross(e2,{0,1,0}),wCross(e2,{0,0,1})}};a=wSub(a,start);b=wSub(b,start);c=wSub(c,start);float enter=-std::numeric_limits<float>::infinity(),leave=fraction;Vec3 n{};for(Vec3 axis:axes){if(wDot(axis,axis)<1e-16f)continue;float d=wDot(delta,axis),r=std::abs(axis.x)*extent.x+std::abs(axis.y)*extent.y+std::abs(axis.z)*extent.z;float lo=std::min({wDot(a,axis),wDot(b,axis),wDot(c,axis)})-r,hi=std::max({wDot(a,axis),wDot(b,axis),wDot(c,axis)})+r;if(std::abs(d)<1e-10f){if(0<lo-1e-5f||0>hi+1e-5f)return false;continue;}float t0=lo/d,t1=hi/d;Vec3 candidate=wUnit(wMul(axis,d>0?-1.f:1.f));if(t0>t1)std::swap(t0,t1);if(t0>enter){enter=t0;n=candidate;}leave=std::min(leave,t1);if(enter>leave+1e-6f)return false;}if(leave<0||enter>fraction||enter<-1e-4f)return false;fraction=std::max(0.f,enter);normal=n;return true;}
bool wSweepEllipsoid(Vec3 start,Vec3 delta,Vec3 extent,Vec3 a,Vec3 b,Vec3 c,float& fraction,Vec3& normal){
 if(extent.x<=0&&extent.y<=0&&extent.z<=0)return wSweepTriangle(start,delta,{},a,b,c,fraction,normal);
 Vec3 radii{std::max(extent.x,1e-5f),std::max(extent.y,1e-5f),std::max(extent.z,1e-5f)};auto scaled=[&](Vec3 p){return Vec3{p.x/radii.x,p.y/radii.y,p.z/radii.z};};a=scaled(wSub(a,start));b=scaled(wSub(b,start));c=scaled(wSub(c,start));Vec3 velocity=scaled(delta);float best=fraction;Vec3 selected{};bool found=false;
 auto accept=[&](float t,Vec3 n){if(t<-1e-5f||t>best||wDot(n,velocity)>=-1e-7f)return;t=std::max(0.f,t);best=t;selected=wUnit(n);found=true;};
 auto root=[&](Vec3 q,Vec3 v,float& t){float aa=wDot(v,v),bb=2*wDot(q,v),cc=wDot(q,q)-1;if(cc<=0){t=0;return true;}if(aa<1e-16f)return false;float disc=bb*bb-4*aa*cc;if(disc<0)return false;t=(-bb-std::sqrt(disc))/(2*aa);return t>=0&&t<=best;};
 Vec3 face=wUnit(wCross(wSub(b,a),wSub(c,a)));if(wDot(face,face)>0){float distance=-wDot(a,face),speed=wDot(velocity,face);auto inside=[&](Vec3 p){Vec3 ab=wSub(b,a),bc=wSub(c,b),ca=wSub(a,c);return wDot(wCross(ab,wSub(p,a)),face)>=-1e-5f&&wDot(wCross(bc,wSub(p,b)),face)>=-1e-5f&&wDot(wCross(ca,wSub(p,c)),face)>=-1e-5f;};if(std::abs(distance)<=1){Vec3 p=wMul(face,-distance);if(inside(p))accept(0,wMul(face,distance<0?-1.f:1.f));}if(std::abs(speed)>1e-12f)for(float side:{-1.f,1.f}){float t=(side-distance)/speed;if(t>=0&&t<=best){Vec3 p=wSub(wMul(velocity,t),wMul(face,side));if(inside(p))accept(t,wMul(face,side));}}}
 for(Vec3 point:{a,b,c}){float t;if(root(wMul(point,-1),velocity,t))accept(t,wSub(wMul(velocity,t),point));}
 const std::array<Vec3,3> points{a,b,c};for(int i=0;i<3;++i){Vec3 p=points[i],edge=wSub(points[(i+1)%3],p);float length=wDot(edge,edge);if(length<1e-16f)continue;Vec3 q=wMul(p,-1),qPerp=wSub(q,wMul(edge,wDot(q,edge)/length)),vPerp=wSub(velocity,wMul(edge,wDot(velocity,edge)/length));float t;if(!root(qPerp,vPerp,t))continue;Vec3 center=wMul(velocity,t);float s=wDot(wSub(center,p),edge)/length;if(s>=0&&s<=1)accept(t,wSub(center,wAdd(p,wMul(edge,s))));}
 if(!found)return false;fraction=best;normal=wUnit({selected.x/radii.x,selected.y/radii.y,selected.z/radii.z});return true;
}
// Birth/morph bounds. A6 00435730: MDL/sprite/HMP resource box times SCALE (WMB unscaled); with neither NARROW nor FAT, the XY size picks
// FAT (>6*HULL_NARROWMAX.y), NARROW (>=HULL_NARROWMAX.y) or none (ENABLE_POLYCOLLISION>=2: both); unless both, MDL/sprite XY become
// a square of the averaged min/max corners with any excess beyond +-16 halved, and WMB a cube. Stored truncated (ftol 005b72cc).
// ponytail: recomputed at birth and ent_morph only; A6 00432cb0 also recomputes after per-frame SCALE changes. Every SCALE write
// in the original campaign scripts after birth targets a PASSABLE entity (weapons, effects, decals) and every script trace/move
// uses IGNORE_PASSABLE, so the recompute has no observable effect there (docs/KNOWN_ISSUES.md); port it if a script needs it.
void wBounds(Engine& e,ObjectId id,const EntityData& d){
 const auto& model=*d.model;
 if(e.state.profile!=SemanticProfile::CampaignA6){e.set_field(id,"min_x",model.min.x);e.set_field(id,"min_y",model.min.y);e.set_field(id,"min_z",model.min.z);e.set_field(id,"max_x",model.max.x);e.set_field(id,"max_y",model.max.y);e.set_field(id,"max_z",model.max.z);return;}
 const Vec3 lo=d.type==2?model.resourceMin:model.min,hi=d.type==2?model.resourceMax:model.max; // MDL: first-frame resource box (+0x38).
 std::array<float,6> b{lo.x,lo.y,lo.z,hi.x,hi.y,hi.z};
 if(d.type!=4){const auto s=wScale(e,id);for(int i=0;i<6;++i)b[i]*=std::array{s.x,s.y,s.z}[i%3];}
 auto& eflags=e.state.require(id).field("flags2").value;std::uint32_t hull=eflags.bits&3;
 if(!hull){const float size=float((double(b[4])-b[1]+b[3])-b[0]);
  if(e.global("enable_polycollision").raw()>=0x800)hull=3;
  else{const float narrow=float(e.vector(e.global_vector("hull_narrowmax")).y);hull=narrow*6<size?2:size>=narrow?1:0;}
  eflags.bits|=hull;}
 if(hull!=3&&d.type==4){const float lo=std::min(std::min(b[0],b[1])-32.f,b[2]-16.f),hi=std::max(std::max(b[3],b[4])+32.f,b[5]+16.f),r=std::max(-lo,hi);b={-r,-r,-r,r,r,r};}
 else if(hull!=3&&(d.type==1||d.type==2)){
  const float cx=float((double(b[3])+b[0])*.5),cy=float((double(b[4])+b[1])*.5);float lo=float((double(b[1])+b[0])*.5),hi=float((double(b[4])+b[3])*.5);
  if(hi>16)hi=float(hi-(double(hi)-16)*.5);if(lo<-16)lo=float(lo-(double(lo)+16)*.5);
  b[0]=lo+cx;b[1]=lo+cy;b[3]=hi+cx;b[4]=hi+cy;}
 constexpr std::array names{"min_x","min_y","min_z","max_x","max_y","max_z"};
 for(int i=0;i<6;++i)e.state.member(id,names[i]).set(Value::from_raw(std::int32_t(std::trunc(double(b[i])*1024))));
}
Vec3 wExtent(const Engine& e,ObjectId id){return {float((e.field(id,"max_x")-e.field(id,"min_x"))*.5),float((e.field(id,"max_y")-e.field(id,"min_y"))*.5),float((e.field(id,"max_z")-e.field(id,"min_z"))*.5)};}
Vec3 wCenter(const Engine& e,ObjectId id){return {float((e.field(id,"max_x")+e.field(id,"min_x"))*.5),float((e.field(id,"max_y")+e.field(id,"min_y"))*.5),float((e.field(id,"max_z")+e.field(id,"min_z"))*.5)};}
int wWorldContent(const World* world,Vec3 point){if(!world||world->bspNodes.empty())return 1;std::int32_t index=0;for(std::size_t step=0;step<=world->bspNodes.size();++step){if(index<0){std::size_t leaf=std::size_t(-std::int64_t(index)-1);if(leaf>=world->bspLeaves.size())throw std::runtime_error("invalid collision BSP leaf");int content=world->bspLeaves[leaf].content;if(content==-128)return 3;if(content==-126)return 2;if(content==-256)return 1;throw std::runtime_error("unsupported WMB collision leaf content: "+std::to_string(content));}if(std::size_t(index)>=world->bspNodes.size())throw std::runtime_error("invalid collision BSP node");const auto& node=world->bspNodes[std::size_t(index)];if(node.plane>=world->planes.size())throw std::runtime_error("invalid collision BSP plane");const auto& plane=world->planes[node.plane];index=node.children[wDot(point,plane.normal)>=plane.distance?0:1];}throw std::runtime_error("cyclic collision BSP");}
struct WorldQuerySnapshot {Engine& engine;std::vector<ObjectId>* ids;explicit WorldQuerySnapshot(Engine& e,bool includeViews=false):engine(e){std::size_t depth=e.worldQueryDepth;while(e.worldQueryScratch.size()<=depth)e.worldQueryScratch.emplace_back();ids=&e.worldQueryScratch[depth];ids->clear();ids->reserve(e.entities.size());for(const auto& [id,data]:e.entities){const auto* o=e.state.get(id);if(data.scannable&&o&&o->alive&&(includeViews||e.field(id,"view")==0))ids->push_back(id);}std::sort(ids->begin(),ids->end());++e.worldQueryDepth;}~WorldQuerySnapshot(){--engine.worldQueryDepth;}WorldQuerySnapshot(const WorldQuerySnapshot&)=delete;};
// A6 004523f0 clears support MIN/MAX with its 80-byte query record. Generic
// planes consequently use zero support, never this actor's/public hull bounds.
struct WLegacyHullQuery {
 std::span<const WorldPlane> planes;std::span<const WorldHull> nodes;
 const WorldLegacyHull& hull;std::int32_t mask;Engine::Hit& hit;
 const WorldHull& node(std::int32_t index)const{
  if(index<0||index>hull.last||std::size_t(index)>=nodes.size())throw std::runtime_error("A6 legacy BSP node outside descriptor");
  return nodes[std::size_t(index)];
 }
 const WorldPlane& plane(const WorldHull& n)const{
  if(n.plane>=planes.size())throw std::runtime_error("A6 legacy BSP plane outside resource");
  return planes[n.plane];
 }
 float distance(const WorldPlane& p,Vec3 v,bool classifier)const{
  const int type=std::bit_cast<std::int8_t>(std::uint8_t(p.type));
  const std::array<float,3> components{v.x,v.y,v.z};
  if(type>=0&&type<3)return float(static_cast<long double>(components[type])-p.distance);
  if(hull.generic&&type>=-3&&type<0)return float(-static_cast<long double>(components[type+3])-p.distance);
  const long double x=static_cast<long double>(v.x)*p.normal.x,y=static_cast<long double>(v.y)*p.normal.y,z=static_cast<long double>(v.z)*p.normal.z;
  return float((classifier?((x+y)+z):((x+z)+y))-p.distance);
 }
 std::int32_t content(std::int32_t index,Vec3 v)const{
  if(hull.last<0)return -256;
  for(std::size_t depth=0;index>=0;++depth){
   if(depth>nodes.size())throw std::runtime_error("cyclic A6 legacy BSP");
   const auto& n=node(index);index=n.children[distance(plane(n),v,true)<0?1:0];
  }
  return index;
 }
 static Vec3 interpolate(Vec3 a,Vec3 b,float fraction){
  return {float((static_cast<long double>(b.x)-a.x)*fraction+a.x),float((static_cast<long double>(b.y)-a.y)*fraction+a.y),float((static_cast<long double>(b.z)-a.z)*fraction+a.z)};
 }
 bool trace(std::int32_t index,float first,float last,Vec3 start,Vec3 end,std::size_t depth=0){
  if(index<0){
   if(index==-128){hit.status|=6;return true;}
   const auto previous=hit.status;hit.status&=~5u;
   if(index==-126){if(!(previous&0x12))hit.status|=0x80;hit.status|=0x20;}
   else hit.status|=0x10;
   return true;
  }
  if(depth>nodes.size())throw std::runtime_error("cyclic A6 legacy BSP");
  const auto& n=node(index);const auto& p=plane(n);
  const float a=distance(p,start,false),b=distance(p,end,false);
  if(a>=0&&b>=0)return trace(n.children[0],first,last,start,end,depth+1);
  if(a<0&&b<0)return trace(n.children[1],first,last,start,end,depth+1);
  const unsigned side=a<0?1:0;
  float split=std::clamp(float((static_cast<long double>(a)+(side?0.03125L:-0.03125L))/(static_cast<long double>(a)-b)),0.f,1.f);
  const float span=float(static_cast<long double>(last)-first);
  float fraction=float(static_cast<long double>(span)*split+first);
  Vec3 middle=interpolate(start,end,split);
  if(!trace(n.children[side],first,fraction,start,middle,depth+1))return false;
  const auto opposite=content(n.children[side^1],middle);
  if((opposite&mask)!=-128)return trace(n.children[side^1],fraction,last,middle,end,depth+1);
  if(opposite==-126)hit.status|=0x40;else if(opposite==-128)hit.status|=4;
  if(!(hit.status&1)){
   hit.normal=side?wMul(p.normal,-1):p.normal;
   hit.planeDistance=side?-p.distance:p.distance;
   while(content(hull.root,middle)==-128){
    split=float(static_cast<long double>(split)-0.1L);if(split<0)break;
    fraction=float(static_cast<long double>(span)*split+first);middle=interpolate(start,end,split);
   }
   hit.bspNode=n.bspNode;hit.fraction=fraction;hit.position=middle;hit.localPosition=middle;
  }
  return false;
 }
 void run(Vec3 start,Vec3 end){
  hit.fraction=1;hit.position=end;hit.status=1;
  if(hull.last<0){hit.status=0x10;return;}
  trace(hull.root,0,1,start,end);
 }
};
std::int32_t wLegacyPointContent(const World* world,Vec3 point){
 if(!world)return -256;
 if(!world->legacyHullsValid)throw std::runtime_error("A6 legacy point query requires compiled BSP metadata");
 Engine::Hit scratch;const auto& hull=world->legacyHulls[0];
 return WLegacyHullQuery{world->planes,world->legacyPointNodes,hull,-3,scratch}.content(hull.root,point);
}
}
Engine::QueryGeometry& Engine::prepare_query_geometry(ObjectId id,const EntityData& data,bool captureBake){
 auto [entry,inserted]=queryGeometry_.try_emplace(data.model);auto& geometry=entry->second;
 if(inserted){
  if(data.model){const auto& model=*data.model;const auto& vertices=model.sourceVertices.empty()?model.vertices:model.sourceVertices;
   if(!vertices.empty()){geometry.hasVertices=true;geometry.sourceMin=geometry.sourceMax=vertices.front().position;for(const auto& vertex:vertices){geometry.sourceMin=wMin(geometry.sourceMin,vertex.position);geometry.sourceMax=wMax(geometry.sourceMax,vertex.position);}}
   if(state.profile==SemanticProfile::BonusA7&&data.type==5&&global("terrain_chunk").raw()>0){const auto name=std::string_view(data.file).substr(0,31);const auto dot=name.find_last_of('.');geometry.chunked=!(dot!=std::string_view::npos&&name.size()-dot-1<=3&&dot>=2&&name[dot-2]=='_');} // 1005da80 load policy; cached resources retain it.
   if(data.type==4){
    const World* source=world&&data.model==&world->mesh?world:!data.file.empty()?&assets.world(data.file):nullptr;
    if(source){
     auto readWord=[](const std::vector<std::uint8_t>& bytes,std::size_t offset){if(offset+4>bytes.size())throw std::runtime_error("WMB query topology outside retained section");std::uint32_t value;std::memcpy(&value,bytes.data()+offset,4);return value;};
     for(std::size_t leafIndex=1;leafIndex<source->bspLeaves.size();++leafIndex){const auto& leaf=source->bspLeaves[leafIndex];std::int32_t content=leaf.content;if(content<0){content=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(content)&0xfffffff2u);if(content&16)content=-256;}if(content!=-256)continue;
      for(std::uint32_t block=0;block<leaf.blockCount;++block){const auto faceIndex=source->bspBlocks.at(std::size_t(leaf.firstBlock)+block);const auto& face=source->faces.at(faceIndex);const auto& material=model.materials.at(face.materialIndex);auto& output=(material.flags&64)?geometry.auxiliary:geometry.primary;
       auto vertex=[&](std::uint32_t edge){const auto reference=std::bit_cast<std::int32_t>(readWord(source->sections[13],(std::size_t(face.firstEdge)+edge)*4));const auto magnitude=reference<0?-std::int64_t(reference):std::int64_t(reference);return readWord(source->sections[12],std::size_t(magnitude)*8+(reference<=0?4:0));};
       if(face.edgeCount<3)continue;const auto first=vertex(0);auto previous=vertex(1);
       for(std::uint32_t fan=2;fan<face.edgeCount;++fan){const auto current=vertex(fan);const auto a=source->collision.vertices.at(current),b=source->collision.vertices.at(previous),c=source->collision.vertices.at(first);Vec3 normal=wCross(wSub(b,a),wSub(c,a));const float length=wLength(normal),epsilon=std::bit_cast<float>(0x38d1b717u);if(length<=epsilon)normal={};else{normal=wMul(normal,1/length);for(float* component:{&normal.x,&normal.y,&normal.z})if(std::abs(*component)<epsilon)*component=0;}
        if(std::isfinite(normal.x)&&std::isfinite(normal.y)&&std::isfinite(normal.z)&&normal.x>=-1&&normal.x<=1&&normal.y>=-1&&normal.y<=1&&normal.z>=-1&&normal.z<=1&&(std::abs(normal.x)>epsilon||std::abs(normal.y)>epsilon||std::abs(normal.z)>epsilon)){
         std::uint32_t renderTriangle=~0u;for(std::uint32_t i=0;i<model.triangles.size();++i)if(model.triangles[i].sourceFace==faceIndex){renderTriangle=i+fan-2;break;}output.push_back({{current,previous,first},renderTriangle,face.materialIndex});
        }previous=current;
       }
      }
     }
    }
   }
  }
 }
 // 1003b300 captures a chunk resource only at genuine preparation; never lazily on a query.
 if(captureBake&&state.profile==SemanticProfile::BonusA7&&data.type==5&&geometry.hasVertices&&geometry.chunked&&!geometry.baked){
  geometry.baked=true;geometry.bakedScale=wScale(*this,id);geometry.bakedOrigin=position(id);geometry.bakedMin={INFINITY,INFINITY,INFINITY};geometry.bakedMax={-INFINITY,-INFINITY,-INFINITY};const auto& vertices=data.model->sourceVertices.empty()?data.model->vertices:data.model->sourceVertices;for(const auto& vertex:vertices){const auto p=vertex.position,s=geometry.bakedScale,e=geometry.bakedOrigin;Vec3 value{p.x*s.x+e.x,p.y*s.y+e.y,p.z*s.z+e.z};geometry.bakedMin=wMin(geometry.bakedMin,value);geometry.bakedMax=wMax(geometry.bakedMax,value);}
  if(geometry.baked){const auto center=wAdd(geometry.bakedMin,wMul(wSub(geometry.bakedMax,geometry.bakedMin),.5f));float greatest=0;const auto& vertices=data.model->sourceVertices.empty()?data.model->vertices:data.model->sourceVertices;for(const auto& vertex:vertices){const auto p=vertex.position,s=geometry.bakedScale,e=geometry.bakedOrigin;const auto delta=wSub({p.x*s.x+e.x,p.y*s.y+e.y,p.z*s.z+e.z},center);greatest=std::max(greatest,float((static_cast<long double>(delta.x)*delta.x+static_cast<long double>(delta.y)*delta.y)+static_cast<long double>(delta.z)*delta.z));}geometry.bakedRadius=float(std::sqrt(double(greatest)));}
 }
 return geometry;
}
void Engine::construct_query_actor(ObjectId id,QueryActor& actor){
 const auto& data=entities.at(id);
 if(!data.model||!data.type)throw std::runtime_error("A7 1006e1e0 rebuild to an empty resource has no concrete frontend (1006df5e source fault)");
 auto& geometry=prepare_query_geometry(id,data);
 actor.type=data.type;actor.baked=geometry.baked;actor.scale=wScale(*this,id); // Concrete initial source bank0.
 Vec3 lo=geometry.baked?geometry.bakedMin:geometry.sourceMin,hi=geometry.baked?geometry.bakedMax:geometry.sourceMax;
 if(!geometry.baked){const auto s=actor.scale;lo={lo.x*s.x,lo.y*s.y,lo.z*s.z};hi={hi.x*s.x,hi.y*s.y,hi.z*s.z};}
 const auto half=wMul(wSub(hi,lo),.5f);actor.center=wAdd(lo,half);actor.alternateCenter=actor.center;actor.min=wSub(lo,actor.center);actor.max=wSub(hi,actor.center);
 actor.radius=geometry.baked?geometry.bakedRadius:float(std::sqrt(double(float((static_cast<long double>(half.x)*half.x+static_cast<long double>(half.z)*half.z)+static_cast<long double>(half.y)*half.y))));
 actor.selected={};actor.axes={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};actor.vertices.clear();actor.triangles.clear();actor.primary=geometry.primary;actor.auxiliary=geometry.auxiliary;
 if(data.type!=1){ // Sprite +94 private factory is not a MODEL/render-quad geometry provider.
  const auto& model=*data.model;const auto& source=data.type==4&&!model.sourceVertices.empty()?model.sourceVertices:model.vertices;
  actor.vertices.reserve(source.size());for(const auto& vertex:source){auto p=vertex.position;if(data.type!=4&&!model.frames.empty())p=model.frames.front().positions.at(actor.vertices.size());const auto s=geometry.baked?geometry.bakedScale:actor.scale,e=geometry.baked?geometry.bakedOrigin:Vec3{};p={p.x*s.x+e.x,p.y*s.y+e.y,p.z*s.z+e.z};actor.vertices.push_back(geometry.baked?p:wSub(p,actor.center));}
  if(data.type!=4){actor.triangles.reserve(model.triangles.size());for(std::uint32_t i=0;i<model.triangles.size();++i){const auto& t=model.triangles[i];actor.triangles.push_back({t.indices,i,t.materialIndex});}}
 }
 refresh_query_pose(id,actor);
}
void Engine::refresh_query_pose(ObjectId id){
 if(state.profile!=SemanticProfile::BonusA7)return;
 const auto actor=queryActors_.find(id);if(actor==queryActors_.end())throw std::runtime_error("A7 body wake has no retained context (10066160 / 1006dff0 source fault)");refresh_query_pose(id,actor->second);
}
void Engine::refresh_query_pose(ObjectId id,QueryActor& actor){
 const auto found=entities.find(id);if(found==entities.end())return;
 // 10070c40 / 100732a0 test the LIVE resource bit8, not the retained frontend kind.
 const auto live=queryGeometry_.find(found->second.model);if(live!=queryGeometry_.end()&&live->second.baked)return;
 const auto a=angles(id);actor.axes={wRotate({1,0,0},a),wRotate({0,1,0},a),wRotate({0,0,1},a)};actor.selected=wAdd(position(id),wBasisRotate(actor.center,actor.axes));
 if(physics.query_body_geometry(id)){
  // MODEL20 bank0 constructs both cached C values from the same source/scales (1006dc70/10071650).
  // 1006dff0 is transient: the following fixed substep may restore the previous solver snapshot.
  const auto p=wAdd(position(id),wBasisRotate(actor.alternateCenter,actor.axes));const auto& r=actor.axes;dMatrix3 matrix{r[0].x,r[1].x,r[2].x,0,r[0].y,r[1].y,r[2].y,0,r[0].z,r[1].z,r[2].z,0};physics.set_query_body_transform(id,p,matrix);
 }
}
void Engine::initialize_query_actor(ObjectId id,bool prepareResource){
 const auto found=entities.find(id);const auto* object=state.get(id);
 if(queryActors_.contains(id)||found==entities.end()||!object||!object->alive)return;
 const auto& data=found->second;if(!data.model||!data.type||!data.physicsGeometry)return;
 if(prepareResource)prepare_query_geometry(id,data,true);
 if(!global("enable_polycollision").raw())return;
 auto& actor=queryActors_[id];actor.shape=data.type==5?4:0;construct_query_actor(id,actor);
 actor.snapshotResource=data.model;actor.snapshotPosition=position(id);actor.snapshotAngles=angles(id);actor.snapshotScale=wScale(*this,id);
}
void Engine::synchronize_query_body(ObjectId id,dGeomID body){
 if(state.profile!=SemanticProfile::BonusA7||!body)return;
 const auto found=queryActors_.find(id);if(found==queryActors_.end())return;auto& actor=found->second;if(actor.shape==3||actor.shape==4)return;
 // 1006e150 selector0 copies completed solver S/R into PRIMARY, before interpolation.
 const auto* p=dGeomGetPosition(body);const auto* r=dGeomGetRotation(body);actor.selected={float(p[0]),float(p[1]),float(p[2])};actor.axes={Vec3{float(r[0]),float(r[4]),float(r[8])},Vec3{float(r[1]),float(r[5]),float(r[9])},Vec3{float(r[2]),float(r[6]),float(r[10])}};
}
void Engine::update_query_actor(ObjectId id,int mode){
 auto found=queryActors_.find(id);const auto data=entities.find(id);if(found==queryActors_.end()||data==entities.end())return;auto& actor=found->second;
 const auto p=position(id),a=angles(id),s=wScale(*this,id);auto differs=[](Vec3 x,Vec3 y){return x.x!=y.x||x.y!=y.y||x.z!=y.z;};
 if(!physics.query_body_geometry(id)){
  if(differs(s,actor.snapshotScale)||(mode!=3&&data->second.model!=actor.snapshotResource)){if(data->second.type==5)actor.shape=4;construct_query_actor(id,actor);}
  else if(differs(p,actor.snapshotPosition)||differs(a,actor.snapshotAngles))refresh_query_pose(id,actor);
 }
 if(mode==0||mode==2){actor.snapshotResource=data->second.model;actor.snapshotPosition=p;actor.snapshotAngles=a;actor.snapshotScale=s;}
}
void Engine::update_query_actors(Runtime& runtime){
 if(!world)return;
 WorldQuerySnapshot snapshot(*this,true);snapshot.ids->clear();for(const auto& [id,data]:entities){const auto* object=state.get(id);if(object&&object->alive)snapshot.ids->push_back(id);}std::sort(snapshot.ids->begin(),snapshot.ids->end());
 for(ObjectId id:*snapshot.ids){const auto* object=state.get(id);if(!object||!object->alive||!entities.contains(id)||!field(id,"event_flags"))continue;if(std::uint32_t(field(id,"event_flags"))&0x1000)invoke_event(runtime,id,id,12);object=state.get(id);if(object&&object->alive&&entities.contains(id))update_query_actor(id,0);}
}
void Engine::sort_query_contacts(std::vector<QueryContact>& a){
 // 1006a140: retain source insertion/partition/heap permutations, including equal keys.
 using Index=std::ptrdiff_t;auto less=[](const QueryContact& x,const QueryContact& y){return x.hit.queryScalar_<y.hit.queryScalar_;};
 auto order3=[&](Index x,Index y,Index z){if(less(a[y],a[x]))std::swap(a[y],a[x]);if(less(a[z],a[y]))std::swap(a[z],a[y]);if(less(a[y],a[x]))std::swap(a[y],a[x]);};
 auto partition=[&](Index lo,Index hi){
  const auto count=hi-lo,mid=lo+count/2,last=hi-1;if(count<=41)order3(lo,mid,last);else{const auto step=count/8;order3(lo,lo+step,lo+2*step);order3(mid-step,mid,mid+step);order3(last-2*step,last-step,last);order3(lo+step,mid,last-step);}
  auto pf=mid,pl=mid+1;while(lo<pf&&!less(a[pf-1],a[pf])&&!less(a[pf],a[pf-1]))--pf;while(pl<hi&&!less(a[pl],a[pf])&&!less(a[pf],a[pl]))++pl;
  auto gf=pl,gl=pf;
  for(;;){while(gf<hi){if(less(a[pf],a[gf]))++gf;else if(less(a[gf],a[pf]))break;else{std::swap(a[pl],a[gf]);++pl;++gf;}}
   while(lo<gl){const auto candidate=gl-1;if(less(a[candidate],a[pf]))--gl;else if(less(a[pf],a[candidate]))break;else{--pf;std::swap(a[pf],a[candidate]);--gl;}}
   if(gl==lo&&gf==hi)return std::pair{pf,pl};
   if(gl==lo){if(pl!=gf)std::swap(a[pf],a[pl]);std::swap(a[pf],a[gf]);++pf;++pl;++gf;}
   else if(gf==hi){--gl;--pf;if(gl!=pf)std::swap(a[gl],a[pf]);--pl;std::swap(a[pf],a[pl]);}
   else{--gl;std::swap(a[gf],a[gl]);++gf;}
  }
 };
 auto adjust=[&](Index base,Index hole,Index count,const QueryContact& saved){const auto top=hole;auto right=2*hole+2;
  while(right<count){auto child=right;if(less(a[base+right],a[base+right-1]))--child;a[base+hole]=a[base+child];hole=child;right=2*hole+2;}
  if(right==count){a[base+hole]=a[base+count-1];hole=count-1;}
  while(top<hole){const auto parent=(hole-1)/2;if(!less(a[base+parent],saved))break;a[base+hole]=a[base+parent];hole=parent;}a[base+hole]=saved;
 };
 auto sort=[&](auto&& self,Index lo,Index hi,Index ideal)->void{
  while(hi-lo>32){if(ideal<=0){auto count=hi-lo;for(auto hole=count/2-1;hole>=0;--hole){const auto saved=a[lo+hole];adjust(lo,hole,count,saved);}while(count>1){const auto saved=a[lo+count-1];a[lo+count-1]=a[lo];adjust(lo,0,count-1,saved);--count;}return;}
   const auto [pf,pl]=partition(lo,hi);const auto half=ideal/2;ideal=half+half/2;if(pf-lo<hi-pl){self(self,lo,pf,ideal);lo=pl;}else{self(self,pl,hi,ideal);hi=pf;}
  }
  for(auto i=lo+1;i<hi;++i){auto destination=i;if(less(a[i],a[lo]))destination=lo;else if(less(a[i],a[i-1])){destination=i-1;while(less(a[i],a[destination-1]))--destination;}if(destination!=i){const auto saved=a[i];for(auto j=i;j>destination;--j)a[j]=a[j-1];a[destination]=saved;}}
 };
 const auto count=Index(a.size());if(count>1)sort(sort,0,count,count);
}
namespace {
void wLegacyBounds(const Engine& engine,ObjectId actor,Vec3& lo,Vec3& hi){
 lo={float(engine.field(actor,"min_x")),float(engine.field(actor,"min_y")),float(engine.field(actor,"min_z"))};
 hi={float(engine.field(actor,"max_x")),float(engine.field(actor,"max_y")),float(engine.field(actor,"max_z"))};
 const auto found=engine.entities.find(actor);if(found==engine.entities.end()||found->second.type!=4||!found->second.model)return;
 const auto& resource=*found->second.model;const std::array<float,3> margins{32,32,16},min{resource.min.x,resource.min.y,resource.min.z},max{resource.max.x,resource.max.y,resource.max.z};
 std::array<float*,3> lower{&lo.x,&lo.y,&lo.z},upper{&hi.x,&hi.y,&hi.z};
 for(unsigned axis=0;axis<3;++axis){if(*lower[axis]<=min[axis]-margins[axis])*lower[axis]+=margins[axis];if(*upper[axis]>=max[axis]+margins[axis])*upper[axis]-=margins[axis];}
}
}
void Engine::prepare_legacy_hit(Hit& hit,Vec3 start,Vec3 end,std::uint32_t mode,ObjectId actor){
 hit.legacyQuery=true;hit.textureScan=(mode&0x1010000)==0x10000;
 const bool passableWorld=hit.entity&&hit.queryWorld&&(flags(hit.entity)&512);
 hit.inSolid_=(hit.status&3)&&!passableWorld;
 hit.inPassable=((hit.status&3)&&passableWorld)||(!(hit.status&3)&&(hit.status&0xa0));
 hit.onPassable=((hit.status&0x40)&&(hit.status&0x10))||((hit.status&4)&&(hit.status&0x10)&&passableWorld);
 hit.publicTarget_=hit.inSolid_?start:hit.position;hit.endpoint_=!hit.inSolid_&&hit.fraction==1;
 hit.hit=(hit.status&3)||hit.fraction!=1;
 if(!hit.endpoint_){
  if(hit.queryWorld||(hit.entity&&entities.at(hit.entity).type==5))hit.publicNormal_=hit.normal;
  else if(hit.entity){const auto origin=position(hit.entity);hit.publicNormal_=wUnit({start.x-origin.x,start.y-origin.y,0});}
 }
 if(hit.inSolid_){hit.resultRaw_=-1024;return;}
 if(hit.endpoint_){hit.resultRaw_=0;return;}
 const Vec3 travelled=wSub(hit.position,start);
 const float distance=float(std::sqrt(double((static_cast<long double>(travelled.z)*travelled.z+static_cast<long double>(travelled.y)*travelled.y)+static_cast<long double>(travelled.x)*travelled.x)));
 hit.resultRaw_=wQueryFixedRaw(distance);
 if(!(mode&8192)||!actor)return;
 Vec3 queryMin{},queryMax{};wLegacyBounds(*this,actor,queryMin,queryMax);
 Vec3 selectedMin{},selectedMax{};
 if(hit.queryWorld||(hit.entity&&entities.at(hit.entity).type==5)){
  const auto shape=std::min(2u,unsigned(field(actor,"narrow")!=0)|(unsigned(field(actor,"fat")!=0)<<1));
  if(shape){selectedMin=vector(global_vector(shape==1?"hull_narrowmin":"hull_fatmin"));selectedMax=vector(global_vector(shape==1?"hull_narrowmax":"hull_fatmax"));if(!hit.queryWorld){selectedMin.x=selectedMin.y=selectedMax.x=selectedMax.y=0;}}
 }
 const std::array<float,3> reverse{start.x-end.x,start.y-end.y,start.z-end.z};
 unsigned axis=0;int sign=0;float dominant=0;
 for(unsigned i=0;i<3;++i){if(reverse[i]>dominant){dominant=reverse[i];axis=i;sign=1;}else if(-reverse[i]>dominant){dominant=-reverse[i];axis=i;sign=-1;}}
 const std::array<float,3> ownMin{queryMin.x,queryMin.y,queryMin.z},ownMax{queryMax.x,queryMax.y,queryMax.z},lower{selectedMin.x,selectedMin.y,selectedMin.z},upper{selectedMax.x,selectedMax.y,selectedMax.z};
 float extra=0;std::array<float*,3> target{&hit.publicTarget_.x,&hit.publicTarget_.y,&hit.publicTarget_.z};
 if(sign>0){extra=ownMin[axis]-lower[axis];*target[axis]+=lower[axis];}
 else if(sign<0){extra=upper[axis]-ownMax[axis];*target[axis]-=upper[axis];}
 const auto correction=wQueryFixedRaw(extra);
 hit.resultRaw_=std::bit_cast<std::int32_t>(std::uint32_t(hit.resultRaw_)+std::uint32_t(correction));
}
Engine::Hit Engine::collide_legacy(Vec3 start,Vec3 end,std::uint32_t mode,ObjectId ignore,const ProcessContext* context,bool movement){
 const ObjectId actor=context?context->my:collisionActor,you=context?context->you:collisionYou;
 Vec3 queryMin{},queryMax{};if((mode&8192)&&actor)wLegacyBounds(*this,actor,queryMin,queryMax);
 const auto shape=(mode&8192)&&actor?std::min(2u,unsigned(field(actor,"narrow")!=0)|(unsigned(field(actor,"fat")!=0)<<1)):0u;
 Hit best;best.position=end;
 auto worldQuery=[&](const World& source,ObjectId owner){
  if(!source.legacyHullsValid)throw std::runtime_error("A6 legacy WMB query requires compiled BSP metadata");
  Hit candidate;const auto& hull=source.legacyHulls[shape];
  const auto nodes=shape?std::span<const WorldHull>(source.hulls):std::span<const WorldHull>(source.legacyPointNodes);
  const Vec3 origin=owner?position(owner):Vec3{},rotation=owner?angles(owner):Vec3{};
  const bool rotated=rotation.x||rotation.y||rotation.z;
  Vec3 a=owner?wSub(start,origin):start,b=owner?wSub(end,origin):end;
  if(rotated){a=wInverseRotate(a,rotation);b=wInverseRotate(b,rotation);}
  WLegacyHullQuery{source.planes,nodes,hull,(mode&4)?-1:-3,candidate}.run(a,b);
  if((candidate.status&1)||((candidate.status&0x20)&&!(mode&4))||candidate.fraction!=1){candidate.entity=owner;candidate.queryWorld=&source;}
  if(candidate.fraction!=1){
   if(rotated){candidate.normal=wRotate(candidate.normal,rotation);candidate.position=WLegacyHullQuery::interpolate(start,end,candidate.fraction);}
   else if(owner)candidate.position=wAdd(candidate.position,origin);
  }else candidate.position=end;
  return candidate;
 };
 if(world&&!((mode&1)&&ignore&&field(ignore,"push")>0))best=worldQuery(*world,0);
 if(best.status&1)best.fraction=0;
 else{
  const auto sweptMin=wSub(wAdd(wMin(start,end),queryMin),{1,1,1}),sweptMax=wAdd(wAdd(wMax(start,end),queryMax),{1,1,1});
  // Candidates in birth (handle) order. Cheap EntityData filters run before any field lookup: scanning the whole
  // handle ledger (every object ever born) per trace made shell-casing traces cost O(objects) each.
  auto& candidates=collideScratch_;candidates.clear();
  for(const auto& [id,data]:entities){
   if(!data.scannable||!data.model||data.type==0||id==ignore||((mode&2)&&id==you)||(movement&&data.legacyPushEpoch==legacyMoveEpoch_))continue;
   if(((mode&128)&&data.type==4)||((mode&16)&&data.type==2)||((mode&32)&&data.type==1)||((mode&256)&&data.type==5))continue;
   const auto* object=state.get(id);if(object&&object->alive&&field(id,"view")==0)candidates.push_back(id);
  }
  std::sort(candidates.begin(),candidates.end());
  for(const ObjectId id:candidates){
   const auto& data=entities.find(id)->second;
   const auto f=flags(id);const bool pass=(f&512)!=0;
   if((!movement&&(mode&512)&&actor&&field(id,"push")<field(actor,"push"))||((mode&4)&&pass)||((mode&0x100000)&&(f&256))||((mode&8)&&pass&&data.type!=4))continue;
   const auto pos=position(id);
   Vec3 lo=wAdd(pos,{float(field(id,"min_x")),float(field(id,"min_y")),float(field(id,"min_z"))}),hi=wAdd(pos,{float(field(id,"max_x")),float(field(id,"max_y")),float(field(id,"max_z"))});
   if(sweptMin.x>hi.x||sweptMin.y>hi.y||sweptMin.z>hi.z||sweptMax.x<lo.x||sweptMax.y<lo.y||sweptMax.z<lo.z)continue;
   Hit candidate;candidate.position=end;
   if(data.type==4){if(!data.model->nativeWorld)throw std::runtime_error("A6 legacy entity WMB has no immutable BSP resource");candidate=worldQuery(*data.model->nativeWorld,id);}
   else if(data.type==5){
    const Vec3 extent=wMul(wSub(queryMax,queryMin),.5f);
    for(std::uint32_t i=0;i<data.model->triangles.size();++i){const auto& t=data.model->triangles[i];Vec3 normal;float fraction=candidate.fraction;if(wSweepEllipsoid(start,wSub(end,start),extent,wWorldVertex(*this,id,data,t.indices[0]),wWorldVertex(*this,id,data,t.indices[1]),wWorldVertex(*this,id,data,t.indices[2]),fraction,normal)){candidate.fraction=fraction;candidate.normal=normal;candidate.position=WLegacyHullQuery::interpolate(start,end,fraction);candidate.triangle=i;candidate.entity=id;candidate.status=0x14;}}
   }else{
    std::array<WorldPlane,6> planes{};std::array<WorldHull,6> nodes{};
    const Vec3 lower=wSub(wSub(lo,pos),queryMax),upper=wSub(wSub(hi,pos),queryMin);
    const std::array<float,3> min{lower.x,lower.y,lower.z},max{upper.x,upper.y,upper.z};
    for(unsigned i=0;i<6;++i){const unsigned axis=i/2;Vec3 n{};if(axis==0)n.x=1;else if(axis==1)n.y=1;else n.z=1;planes[i]={n,i&1?min[axis]:max[axis],axis};nodes[i].plane=i;nodes[i].children[i&1]=-256;nodes[i].children[(i&1)^1]=i==5?-128:std::int32_t(i+1);}
    const WorldLegacyHull hull{0,5};WLegacyHullQuery{planes,nodes,hull,(mode&4)?-1:-3,candidate}.run(wSub(start,pos),wSub(end,pos));
    candidate.position=wAdd(candidate.position,pos);if((candidate.status&1)||((candidate.status&0x20)&&!(mode&4))||candidate.fraction!=1)candidate.entity=id;
   }
   if((candidate.status&3)||candidate.fraction<best.fraction){
    if(candidate.fraction>=best.fraction)candidate.fraction=best.fraction;
    if(candidate.status&1)candidate.fraction=0;
    candidate.status|=best.status&0x82;best=candidate;
   }else if((candidate.status&0x80)&&!(best.status&2)&&!(mode&4)){candidate.entity=id;candidate.fraction=0;best=candidate;}
   if(best.status&1)break;
  }
 }
 best.legacyQuery=true;best.hit=(best.status&3)||best.fraction!=1;best.textureScan=(mode&0x1010000)==0x10000;return best;
}
Engine::Hit Engine::collide(Vec3 start,Vec3 end,Vec3 extent,std::uint32_t mode,ObjectId ignore,const ProcessContext* query,bool modern,Runtime* callbacks){
 if(!modern&&state.profile==SemanticProfile::CampaignA6)return collide_legacy(start,end,mode,ignore,query);
 Hit best;best.textureScan=(mode&0x1010000)==0x10000;const std::uint32_t passMask=0x200;
 const bool bonusModern=modern&&state.profile==SemanticProfile::BonusA7;
 if(bonusModern){start={wQueryFixed(start.x),wQueryFixed(start.y),wQueryFixed(start.z)};end={wQueryFixed(end.x),wQueryFixed(end.y),wQueryFixed(end.z)};}
 Vec3 delta=wSub(end,start);
 const auto rawDirection=delta;
 WorldQuerySnapshot snapshot(*this,true);
 if(bonusModern){snapshot.ids->clear();for(const auto& [id,actor]:queryActors_){const auto* object=state.get(id);if(object&&object->alive)snapshot.ids->push_back(id);}std::sort(snapshot.ids->begin(),snapshot.ids->end());}
 std::vector<QueryContact>* contacts=nullptr;
 if(bonusModern){const auto depth=worldQueryDepth-1;while(queryContacts_.size()<=depth)queryContacts_.emplace_back();contacts=&queryContacts_[depth];contacts->clear();}
 best.contentQuery=bonusModern&&!(mode&64);
 if(best.contentQuery)best.inPassable=wWorldContent(world,start)==2; // 100785c0 START6c; only class-126.
 const ObjectId queryYou=query?query->you:collisionYou,queryActor=query?query->my:collisionActor;
 const float rayLength=std::sqrt(double(float((static_cast<long double>(delta.x)*delta.x+static_cast<long double>(delta.y)*delta.y)+static_cast<long double>(delta.z)*delta.z)));
 Vec3 rayDirection{};const auto rayEpsilon=std::bit_cast<float>(0x38d1b717u);if(bonusModern&&rayLength>rayEpsilon){rayDirection={delta.x/rayLength,delta.y/rayLength,delta.z/rayLength};for(float* component:{&rayDirection.x,&rayDirection.y,&rayDirection.z})if(std::abs(*component)<rayEpsilon)*component=0;}
 bool bestSourcePoint=false;std::optional<WPublicHull> movingHull;
 if(bonusModern&&(mode&8192)&&queryActor){Vec3 lo{float(field(queryActor,"min_x")),float(field(queryActor,"min_y")),float(field(queryActor,"min_z"))},hi{float(field(queryActor,"max_x")),float(field(queryActor,"max_y")),float(field(queryActor,"max_z"))};const auto narrow=field(queryActor,"narrow")!=0,fat=field(queryActor,"fat")!=0;if(narrow!=fat){lo=vector(global_vector(narrow?"hull_narrowmin":"hull_fatmin"));hi=vector(global_vector(narrow?"hull_narrowmax":"hull_fatmax"));}auto span=wSub(hi,lo);for(float* component:{&span.x,&span.y,&span.z})if(!(*component>0))*component=1;extent=wMul(span,.5f);const auto a=angles(queryActor);const std::array axes{wRotate({1,0,0},a),wRotate({0,1,0},a),wRotate({0,0,1},a)};const auto offset=wBasisRotate(wAdd(lo,extent),axes);start=wAdd(start,offset);end=wAdd(end,offset);delta=wSub(end,start);movingHull.emplace(WPublicHull{start,axes,extent});if(best.contentQuery)best.inPassable=wWorldContent(world,start)==2;}
 const long double rawLength=static_cast<long double>(rayLength)*1024;const auto fixedLength=std::isfinite(rawLength)&&rawLength<2147483648.L?std::int32_t(std::trunc(rawLength)):INT32_MIN;
 if(modern&&state.profile==SemanticProfile::CampaignA6&&(mode&8192)&&queryActor){
  // 00470a50: live public hull, MOVE_BUFFER, and the fixed 0.05 extra only for a zero-length query.
  Vec3 lo{float(field(queryActor,"min_x")),float(field(queryActor,"min_y")),float(field(queryActor,"min_z"))},hi{float(field(queryActor,"max_x")),float(field(queryActor,"max_y")),float(field(queryActor,"max_z"))};
  const bool narrow=field(queryActor,"narrow")!=0,fat=field(queryActor,"fat")!=0;
  if(narrow!=fat){lo=vector(global_vector(narrow?"hull_narrowmin":"hull_fatmin"));hi=vector(global_vector(narrow?"hull_narrowmax":"hull_fatmax"));}
  const float buffer=float(number("move_buffer"))+(fixedLength==0?.0498046875f:0.f);const Vec3 padding{buffer,buffer,buffer};lo=wSub(lo,padding);hi=wAdd(hi,padding);
  auto span=wSub(hi,lo);for(float* component:{&span.x,&span.y,&span.z})if(!(*component>0))*component=1;
  extent=wMul(span,.5f);const auto a=angles(queryActor);
  const std::array axes{wRotate({1,0,0},a),wRotate({0,1,0},a),wRotate({0,0,1},a)};
  const auto offset=wBasisRotate(wAdd(lo,extent),axes);
  start=wAdd(start,offset);end=wAdd(end,offset);delta=wSub(end,start);movingHull=WPublicHull{start,axes,extent};
 }
 std::optional<modern_box::Query> meshBox;
 // Original stationary packet98 is uninitialized stack. Retain the existing
 // deterministic native stationary query; only evidenced moving primitives cut over.
 if(modern&&movingHull&&(delta.x!=0||delta.y!=0||delta.z!=0))meshBox.emplace(start,movingHull->axes,movingHull->half,delta,bonusModern?0.f:.0498046875f,bonusModern);
 const Vec3 broadExtent=meshBox?meshBox->broadExtent():extent;
 const float correctionFraction=meshBox&&!bonusModern&&rayLength>0?.0498046875f/rayLength:0.f;
 auto geometricHorizon=[&]{return std::min(1.f,best.fraction+correctionFraction);};
 const bool staticBox=movingHull&&std::abs(rawDirection.x)<=rayEpsilon&&std::abs(rawDirection.y)<=rayEpsilon&&std::abs(rawDirection.z)<=rayEpsilon;
 const float boxHorizon=staticBox?0.f:float(fixedLength)/1024;const auto boxDirection=staticBox?rawDirection:rayDirection;
 const float queryRadius=float(fixedLength)/1024.f+((mode&8192)&&queryActor?float(.5f*float(std::sqrt(double(float((static_cast<long double>(2*extent.x)*float(2*extent.x)+static_cast<long double>(2*extent.y)*float(2*extent.y))+static_cast<long double>(2*extent.z)*float(2*extent.z)))))):0.f);
 float markerFraction=std::numeric_limits<float>::infinity();ObjectId markerOwner=0;
 auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,ObjectId id,std::uint32_t index,std::uint32_t surface,const Material* material,bool auxiliary=false,bool typeRejected=false,const std::array<std::uint32_t,3>* sourceVertices=nullptr,const Vec3* retainedA=nullptr,const Vec3* retainedB=nullptr,const Vec3* retainedC=nullptr){
  const bool marker=bonusModern&&(auxiliary||(!id&&(surface&64)));float fraction=bonusModern?1.f:best.fraction;Vec3 normal;
  std::optional<modern_box::Contact> boxContact;
  if(meshBox){boxContact=meshBox->triangle(a,b,c,retainedA?*retainedA:a,retainedB?*retainedB:b,retainedC?*retainedC:c);if(!boxContact)return;fraction=boxContact->fraction;normal=boxContact->normal;}
  else if(!wSweepEllipsoid(start,delta,extent,a,b,c,fraction,normal))return;
  if(marker){best.onPassable=true;if(fraction<markerFraction){markerFraction=fraction;markerOwner=id;}}
  if(typeRejected||(marker?(mode&(id?8u:4u)):((mode&4)&&(surface&(id?512u:64u)))))return;
  Hit hit;hit.hit=true;hit.fraction=fraction;hit.queryScalar_=boxContact?boxContact->scalar:float(static_cast<long double>(fraction)*rayLength);if(boxContact)hit.position=boxContact->position;hit.normal=normal;hit.entity=id;hit.triangle=index;hit.surfaceFlags=surface;hit.material=material;
  const auto contact=boxContact?boxContact->position:wAdd(start,wMul(delta,fraction));const float da=wDot(wSub(a,contact),wSub(a,contact)),db=wDot(wSub(b,contact),wSub(b,contact)),dc=wDot(wSub(c,contact),wSub(c,contact));const auto corner=da<=db&&da<=dc?0u:db<=dc?1u:2u;
  const Model* model=id?entities.at(id).model:world?&world->mesh:nullptr;hit.vertex=corner;
  if(sourceVertices)hit.vertex=(*sourceVertices)[corner]+1;
  else if(!id&&world&&index<world->collision.triangles.size())hit.vertex=world->collision.triangles[index][corner]+1;
  else if(model&&index<model->triangles.size()){const auto render=model->triangles[index].indices[corner];hit.vertex=(model->vertexSources.empty()?render:model->vertexSources.at(render))+1;}
  if(contacts)contacts->push_back({hit,model,bool(boxContact)});
  if(!best.hit||(boxContact?hit.queryScalar_<best.queryScalar_:fraction<best.fraction)){const auto content=best;best=hit;bestSourcePoint=bool(boxContact);best.textureScan=content.textureScan;best.contentQuery=content.contentQuery;best.inPassable=content.inPassable;best.onPassable=content.onPassable;best.passableEntity=content.passableEntity;}
 };
 // A6 modern 004728d4 filters global WMB; A7 10067cc0 bypasses entity type masks for it.
 // Both legacy wrappers suppress it only for IGNORE_ME with a positive ME.PUSH.
 auto worldContacts=[&]{if(world&&!(query&&((modern&&state.profile==SemanticProfile::CampaignA6&&(mode&128))||(!modern&&(mode&1)&&queryActor&&field(queryActor,"push")>0)))){
  auto staticTriangle=[&](std::uint32_t index){const auto& mesh=world->collision;const auto& t=mesh.triangles[index];const Material* m=nullptr;std::uint32_t f=index<mesh.flags.size()?mesh.flags[index]:0;if(bonusModern&&(mode&4)&&(f&64))return; // 10078b00: CE gates global marker1 submission before contact observation.
   if(index<world->mesh.triangles.size()){auto mi=world->mesh.triangles[index].materialIndex;if(mi<world->mesh.materials.size())m=&world->mesh.materials[mi];}triangle(mesh.vertices[t[0]],mesh.vertices[t[1]],mesh.vertices[t[2]],0,index,f,m);};
  if(!collisionNodes.empty()){std::array<std::uint32_t,64> stack{};std::size_t count=1;while(count){const auto& node=collisionNodes[stack[--count]];float f=bonusModern?1.f:geometricHorizon();Vec3 n;if(!wSweepBox(start,delta,broadExtent,node.min,node.max,f,n))continue;if(node.count){for(std::uint32_t i=node.begin;i<node.begin+node.count;++i)staticTriangle(collisionOrder[i]);}else{if(count+2>stack.size())throw std::runtime_error("collision BVH exceeds balanced tree depth");stack[count++]=node.left;stack[count++]=node.right;}}}
  else for(std::uint32_t i=0;i<world->collision.triangles.size();++i)staticTriangle(i);
 }};
 if(!bonusModern)worldContacts();
 for(ObjectId id:*snapshot.ids){auto found=entities.find(id);const auto* object=state.get(id);if(found==entities.end()||!object||!object->alive)continue;
  // 10035e20 / 10039550 admit only resource-backed, non-SKY/non-view-space contexts.
  if(bonusModern&&!queryActors_.contains(id))continue;
  bool eventSent=false;
  if(bonusModern&&callbacks&&(mode&4096)&&(std::uint32_t(field(id,"event_flags"))&0x400)){if(!queryActor)throw std::runtime_error("A7 ACTIVATE_TRIGGER has no ME range provider (10078cf0 source fault)");const auto& geometry=queryActors_.at(id);const auto distance=wSub(start,geometry.selected);const float distanceSquared=float((static_cast<long double>(distance.z)*distance.z+static_cast<long double>(distance.y)*distance.y)+static_cast<long double>(distance.x)*distance.x),mine=float(field(queryActor,"trigger_range")),other=float(field(id,"trigger_range")),rangeSquared=float(static_cast<long double>(mine)*mine+static_cast<long double>(other)*other);if(distanceSquared<rangeSquared){invoke_event(*callbacks,id,queryActor,10);eventSent=true;}}
  found=entities.find(id);object=state.get(id);if(found==entities.end()||!object||!object->alive)continue;
  const auto& data=found->second;if((!bonusModern&&data.type==0)||field(id,"view")!=0||id==ignore||((mode&2)&&id==queryYou))continue;auto f=flags(id);bool pass=f&passMask;if((mode&0x100000)&&(f&0x100))continue;ObjectId pushSource=queryActor?queryActor:ignore;if((mode&512)&&pushSource&&field(id,"push")<field(pushSource,"push"))continue;
  const bool typeRejected=((mode&16)&&data.type==2)||((mode&32)&&data.type==1)||((mode&128)&&data.type==4)||((mode&256)&&data.type==5);
  if(bonusModern){
   if(!queryActors_.contains(id))continue;const auto& geometry=queryActors_.at(id);
   const auto liveResource=queryGeometry_.find(data.model);const bool liveBaked=liveResource!=queryGeometry_.end()&&liveResource->second.baked;
   if(typeRejected&&!(best.contentQuery&&pass&&(data.type==4||data.type==5)))continue;
   const auto solverPosition=geometry.selected,center=liveBaked?wAdd(solverPosition,geometry.center):solverPosition,distance=wSub(start,center);const float radius=queryRadius+geometry.radius,squared=float((static_cast<long double>(distance.z)*distance.z+static_cast<long double>(distance.y)*distance.y)+static_cast<long double>(distance.x)*distance.x);if(!(squared<=radius*radius))continue;
   if(best.contentQuery&&pass&&(data.type==4||data.type==5)&&(!best.inPassable||(!eventSent&&(mode&4096)&&(std::uint32_t(field(id,"event_flags"))&0x400)))){
    bool inside=false;
    if(data.type==4){const auto local=wInverseRotate(wSub(start,position(id)),angles(id));inside=local.x>=geometry.min.x&&local.x<=geometry.max.x&&local.y>=geometry.min.y&&local.y<=geometry.max.y&&local.z>=geometry.min.z&&local.z<=geometry.max.z;}
    else{const auto offset=liveBaked?geometry.center:solverPosition;const auto lo=wAdd(geometry.min,offset),hi=wAdd(geometry.max,offset);const auto cubeLo=wSub(start,{.5f,.5f,.5f}),cubeHi=wAdd(start,{.5f,.5f,.5f});inside=cubeLo.x<=hi.x&&cubeHi.x>=lo.x&&cubeLo.y<=hi.y&&cubeHi.y>=lo.y&&cubeLo.z<=hi.z&&cubeHi.z>=std::min(lo.z,cubeLo.z);}
    if(inside){best.inPassable=true;best.passableEntity=id;if(callbacks&&!eventSent&&(mode&4096)&&(std::uint32_t(field(id,"event_flags"))&0x400)){invoke_event(*callbacks,id,queryActor,10);eventSent=true;}}
   }
   found=entities.find(id);object=state.get(id);if(found==entities.end()||!object||!object->alive||!queryActors_.contains(id))continue;const auto& live=found->second;const auto& actor=queryActors_.at(id);
   auto vertex=[&](std::uint32_t index){const auto p=actor.vertices.at(index);return actor.baked?p:wAdd(actor.selected,wBasisRotate(p,actor.axes));};
   auto material=[&](std::uint32_t index)->const Material*{return live.model&&index<live.model->materials.size()?&live.model->materials[index]:nullptr;};
   if(actor.type==4){const auto& payload=actor.auxiliary.empty()?actor.primary:actor.auxiliary;auto submit=[&](bool auxiliary){for(const auto& t:payload)triangle(vertex(t.indices[0]),vertex(t.indices[1]),vertex(t.indices[2]),id,t.renderTriangle,f&~512u,material(t.material),auxiliary,typeRejected,&t.indices,&actor.vertices.at(t.indices[0]),&actor.vertices.at(t.indices[1]),&actor.vertices.at(t.indices[2]));};
    // 100704a0 admission uses route0 count; 1006fef8 binds AUX payload onto PRIMARY.
    if(!actor.primary.empty()&&!(pass&&(mode&4)))submit(false);
    if(live.type==4&&(!(mode&8)||(!best.inPassable&&best.contentQuery))&&pass&&!actor.primary.empty())submit(true);
   }else if(!typeRejected&&!(pass&&(mode&(actor.type==5?8u:12u)))){
    if(actor.type==5||(f&0x04000000u)||(mode&0x80000u)){if(actor.type==1)throw std::runtime_error("A7 Sprite POLYGON private frontend +94 factory is unsupported (10072af0 / 1016e258)");for(const auto& t:actor.triangles)triangle(vertex(t.indices[0]),vertex(t.indices[1]),vertex(t.indices[2]),id,t.renderTriangle,f&~512u,material(t.material),false,false,nullptr,&actor.vertices.at(t.indices[0]),&actor.vertices.at(t.indices[1]),&actor.vertices.at(t.indices[2]));}
    else{
     // 10068440 deliberately uses LIVE public candidate hull/pose, even with a body attached.
     Vec3 lo{float(field(id,"min_x")),float(field(id,"min_y")),float(field(id,"min_z"))},hi{float(field(id,"max_x")),float(field(id,"max_y")),float(field(id,"max_z"))};const bool narrow=field(id,"narrow")!=0,fat=field(id,"fat")!=0;if(narrow!=fat){lo=vector(global_vector(narrow?"hull_narrowmin":"hull_fatmin"));hi=vector(global_vector(narrow?"hull_narrowmax":"hull_fatmax"));}auto span=wSub(hi,lo);for(float* component:{&span.x,&span.y,&span.z})if(!(*component>0))*component=1;hi=wMul(span,.5f);const auto a=angles(id);const std::array axes{wRotate({1,0,0},a),wRotate({0,1,0},a),wRotate({0,0,1},a)};const auto hullPosition=wAdd(position(id),wBasisRotate(wAdd(lo,hi),axes));float scalar=0;Vec3 normal,sourcePoint{};bool hitHull=false;
     if(!movingHull){if(wPublicHullRay(start,rayDirection,hullPosition,axes,hi,scalar,normal)){const auto product=wMul(rayDirection,scalar);sourcePoint={float(static_cast<long double>(start.x)+product.x),float(static_cast<long double>(start.y)+product.y),float(static_cast<long double>(start.z)+product.z)};hitHull=true;}}
     else hitHull=wPublicHullBox(*movingHull,WPublicHull{hullPosition,axes,hi},boxDirection,boxHorizon,scalar,sourcePoint,normal);
     if(hitHull){Hit hit;hit.hit=true;hit.queryScalar_=scalar;hit.fraction=rayLength>0?scalar/rayLength:0;hit.normal=normal;hit.position=sourcePoint;hit.entity=id;hit.surfaceFlags=f;hit.material=material(0);contacts->push_back({hit,live.model,true});if(!best.hit||scalar<best.queryScalar_){const auto content=best;best=hit;bestSourcePoint=true;best.textureScan=content.textureScan;best.contentQuery=content.contentQuery;best.inPassable=content.inPassable;best.onPassable=content.onPassable;best.passableEntity=content.passableEntity;}}
    }
   }
   continue;
  }
  if((mode&4)&&pass)continue;if((mode&8)&&pass&&(data.type==2||data.type==1))continue;if(typeRejected)continue;
  bool polygon=data.model&&((f&0x4000000)||data.type==4||data.type==5||(mode&8192));Vec3 pos=position(id),lo=wAdd(pos,{float(field(id,"min_x")),float(field(id,"min_y")),float(field(id,"min_z"))}),hi=wAdd(pos,{float(field(id,"max_x")),float(field(id,"max_y")),float(field(id,"max_z"))});
  if(polygon){Vec3 s=wScale(*this,id),a=angles(id),mn=data.model->min,mx=data.model->max;auto changed=meshOverrides.find(data.model);if(changed!=meshOverrides.end()){std::size_t first=data.model->frames.empty()?0:std::size_t(std::max(1.,std::floor(field(id,"frame")))-1),next=field(id,"next_frame")>=1?std::size_t(field(id,"next_frame")-1):data.model->frames.empty()?0:std::min(first+1,data.model->frames.size()-1);for(std::size_t frame:{first,next}){auto found=changed->second.find(frame);if(found!=changed->second.end()){mn=wMin(mn,found->second.min);mx=wMax(mx,found->second.max);}}}lo={INFINITY,INFINITY,INFINITY};hi={-INFINITY,-INFINITY,-INFINITY};for(unsigned corner=0;corner<8;++corner){Vec3 v{(corner&1?mx.x:mn.x)*s.x,(corner&2?mx.y:mn.y)*s.y,(corner&4?mx.z:mn.z)*s.z};v=wAdd(pos,wRotate(v,a));lo=wMin(lo,v);hi=wMax(hi,v);}}
  Vec3 broadLo=lo,broadHi=hi;if(!polygon){Vec3 center=wMul(wAdd(lo,hi),.5f);float radius=std::max(hi.x-lo.x,hi.y-lo.y)*.5f;broadLo.x=center.x-radius;broadLo.y=center.y-radius;broadHi.x=center.x+radius;broadHi.y=center.y+radius;}float broad=contacts?1.f:geometricHorizon();Vec3 n;if(!wSweepBox(start,delta,broadExtent,broadLo,broadHi,broad,n))continue;
  if(polygon){for(std::uint32_t i=0;i<data.model->triangles.size();++i){const auto& t=data.model->triangles[i];const Material* mat=t.materialIndex<data.model->materials.size()?&data.model->materials[t.materialIndex]:nullptr;std::uint32_t surface=f;if(mat&&data.type==4&&(mat->flags&64))surface|=512;triangle(wWorldVertex(*this,id,data,t.indices[0]),wWorldVertex(*this,id,data,t.indices[1]),wWorldVertex(*this,id,data,t.indices[2]),id,i,surface,mat);}}
  else {float fraction=contacts?1.f:best.fraction;if(wSweepCylinder(start,delta,extent,lo,hi,fraction,n)){Hit hit;hit.hit=true;hit.fraction=fraction;hit.normal=n;hit.entity=id;hit.surfaceFlags=f;hit.material=data.model&&!data.model->materials.empty()?&data.model->materials.front():nullptr;if(contacts)contacts->push_back({hit,data.model});if(!best.hit||fraction<best.fraction){best.hit=true;best.fraction=fraction;best.normal=n;best.entity=id;best.triangle=~0u;best.vertex=0;best.surfaceFlags=f;best.material=hit.material;}}}
 }
 if(bonusModern)worldContacts(); // 100785c0 queries retained actors before the global world.
 if(contacts){sort_query_contacts(*contacts);const auto content=best;best={};bestSourcePoint=false;best.textureScan=content.textureScan;best.contentQuery=content.contentQuery;best.inPassable=content.inPassable;best.onPassable=content.onPassable;best.passableEntity=content.passableEntity;for(const auto& candidate:*contacts){const auto& hit=candidate.hit;if(hit.entity){const auto actor=entities.find(hit.entity);const auto* object=state.get(hit.entity);if(actor==entities.end()||!object||!object->alive||actor->second.model!=candidate.model)continue;}best.hit=true;best.fraction=hit.fraction;best.queryScalar_=hit.queryScalar_;best.position=hit.position;bestSourcePoint=candidate.sourcePoint;best.normal=hit.normal;best.entity=hit.entity;best.triangle=hit.triangle;best.vertex=hit.vertex;best.surfaceFlags=hit.surfaceFlags;best.material=hit.material;break;}}
 if(!best.passableEntity)best.passableEntity=markerOwner;
 if(bonusModern&&best.hit&&best.queryScalar_>0)best.queryScalar_=wQueryFixed(best.queryScalar_); // 100785c0 quantizes positive return distance, not its contact point.
 if(!bestSourcePoint){best.position=wAdd(start,wMul(delta,best.fraction));if(best.hit){float denominator=std::sqrt(extent.x*extent.x*best.normal.x*best.normal.x+extent.y*extent.y*best.normal.y*best.normal.y+extent.z*extent.z*best.normal.z*best.normal.z);if(denominator>0)best.position=wSub(best.position,{extent.x*extent.x*best.normal.x/denominator,extent.y*extent.y*best.normal.y/denominator,extent.z*extent.z*best.normal.z/denominator});}}
 if(modern&&callbacks&&best.hit&&best.entity&&(mode&(1024|2048))){
  // 00472800 / 100785c0: selected-query publication and SHOOT/SONAR
  // precede the mover's per-pass public-position write and contact callback.
  best.modernTexture=true;best.textureScan=(mode&65536)!=0;
  auto context=query?*query:ProcessContext{};publish_hit(context,best,start);
  auto recipientAlive=[&]{const auto* object=state.get(best.entity);return object&&object->alive&&object->kind==Object::Kind::Entity&&entities.contains(best.entity);};
  if((mode&1024)&&recipientAlive())invoke_event(*callbacks,best.entity,queryActor,9);
  const auto* actor=state.get(queryActor);if(queryActor&&(!actor||!actor->alive))return best;
  if((mode&2048)&&recipientAlive())invoke_event(*callbacks,best.entity,queryActor,11);
 }
 return best;
}
struct TextureSample {int light=128,fog=0;std::array<int,3> color{};bool mapped=false;std::uint32_t flags=0;bool named=false;const Material* material=nullptr;};
namespace {
// Shared CPU texel calculation. A7 WMB6 retains RGB24, not A6's mono/RGB565 policy.
std::optional<TextureSample> wTextureSample(const World& world,const Material& material,std::uint32_t triangle,Vec3 point,bool modern,bool bonus,const Assets& assets,std::uint32_t source=~0u){
 if((!modern&&!material.texture)||material.raw.size()<44)return {};
 if(source==~0u){if(triangle>=world.mesh.triangles.size())return {};source=world.mesh.triangles[triangle].sourceFace;}
 if(source>=world.faces.size())return {};
 const auto& face=world.faces[source];const int ambient=std::bit_cast<std::int8_t>(material.raw[42]),fog=std::bit_cast<std::int8_t>(material.raw[43]);
  auto word=[&](std::size_t offset){float value;std::memcpy(&value,material.raw.data()+offset,4);return value;};
  auto project=[&](std::size_t offset){long double x=static_cast<long double>(point.x)*word(offset),y=static_cast<long double>(point.y)*word(offset+4),z=static_cast<long double>(point.z)*word(offset+8);return (bonus&&modern?((z+x)+y):offset?((y+x)+z):((y+z)+x))+word(offset+12);};
  auto coordinate=[&](long double projected,std::int32_t origin){return modern?std::int64_t(std::trunc(static_cast<long double>(float(projected))-float(origin))):std::int64_t(std::trunc(projected))-origin;};
  auto s=coordinate(project(0),face.texelOriginS),v=coordinate(project(16),face.texelOriginT);if(s<0||v<0||s>face.texelExtentS||v>face.texelExtentT)return {};
 int blue=0,green=0,red=0,light=0;bool unlit=(face.nativeFlags&0x100)!=0,hasLightmap=!unlit&&face.lightmapOffset>=0&&(bonus?((world.infoFlags&1)&&!world.sections[16].empty()):!world.sections[8].empty());
 if(unlit)light=(modern?0:128)+ambient;
 else if(hasLightmap){
  s>>=face.nativeSampleShift;v>>=face.nativeSampleShift;const auto storage=bonus?3:world.nativeLightmapStorageBytes;
  if(storage==1){
   const auto& bytes=world.sections[8];auto index=std::size_t(face.lightmapOffset)+std::size_t(v)*((face.texelExtentS>>face.nativeSampleShift)+1)+std::size_t(s);
   if(world.infoFlags&1){if(index>=bytes.size())throw std::runtime_error("native monochrome lightmap sample outside source");blue=green=red=bytes[index];}
   else {index=std::size_t(face.lightmapOffset)+2*(index-std::size_t(face.lightmapOffset));if(index+1>=bytes.size())throw std::runtime_error("native packed monochrome lightmap sample outside source");blue=green=red=assets.native_lightmap_mono(std::uint16_t(bytes[index])|(std::uint16_t(bytes[index+1])<<8));}
   light=blue+ambient;
  }else if(storage==2||storage==3){
   const auto& bytes=world.sections[16];std::int32_t atlasS=std::int32_t(face.lightmapOrigin.x),atlasT=std::int32_t(face.lightmapOrigin.y);
   if(bonus&&atlasS==255&&atlasT==255){const auto packed=std::uint32_t(face.lightmapOffset);atlasS=std::bit_cast<std::int16_t>(std::uint16_t(packed));atlasT=std::bit_cast<std::int16_t>(std::uint16_t(packed>>16));}
   auto index=((std::size_t(face.lightmapIndex)*256+std::size_t(atlasT)+std::size_t(v))*256+std::size_t(atlasS)+std::size_t(s))*3;
   if(index+2>=bytes.size())throw std::runtime_error("native color lightmap sample outside source");blue=bytes[index];green=bytes[index+1];red=bytes[index+2];
   if(storage==2){auto packed=assets.native_lightmap_color(std::uint16_t((blue>>3)|((green&252)<<3)|((red&248)<<8)));blue=(packed&31)<<3;green=(packed>>3)&252;red=(packed>>8)&248;}
   light=bonus?(blue+green+red)/3+ambient+(modern?128:0):modern&&storage==2?(blue+green+red+3*ambient)/3:(blue+green+red)/3+(modern&&storage==3?0:ambient);
  }else throw std::runtime_error("native lightmap CPU storage was not captured at load");
 }
 int colorAmbient=modern&&!unlit&&!hasLightmap?0:ambient;
 return TextureSample{modern?std::min(255,light):light,modern?std::min(255,fog):fog,{blue+colorAmbient,green+colorAmbient,red+colorAmbient},true,material.flags,bool(material.texture),&material};
}
}
TextureSample Engine::sample_texture_hit(const Hit& hit){
 TextureSample sample;const bool bonus=state.profile==SemanticProfile::BonusA7;
 if(hit.legacyQuery){
  if(!hit.queryWorld||hit.bspNode==~0u)return sample;
  const auto& source=*hit.queryWorld;if(hit.bspNode>=source.bspNodes.size())throw std::runtime_error("A6 texture source BSP node outside resource");
  assets.initialize_native_lightmap_tables(global("gamma").raw(),global("d3d_monochrome").bits!=0,renderer!=nullptr);
  auto& temps=a6LegacyTextureTemps_;const Material* selected=nullptr;const auto& node=source.bspNodes[hit.bspNode];
  if(node.firstFace>source.faces.size()||node.faceCount>source.faces.size()-node.firstFace)throw std::runtime_error("A6 texture source face range outside resource");
  for(std::uint32_t index=node.firstFace;index<node.firstFace+node.faceCount;++index){
   const auto materialIndex=source.faces[index].materialIndex;if(materialIndex>=source.mesh.materials.size())throw std::runtime_error("A6 texture source material outside resource");
   const auto& material=source.mesh.materials[materialIndex];if(!material.texture)continue;
   if(material.raw.size()<44)throw std::runtime_error("A6 texture source material has no projection metadata");
   if(selected!=&material){selected=&material;temps[4]=std::bit_cast<std::int8_t>(material.raw[42]);temps[5]=std::bit_cast<std::int8_t>(material.raw[43]);temps[6]=std::bit_cast<std::int32_t>(material.flags);}
   if(const auto value=wTextureSample(source,material,~0u,hit.localPosition,false,false,assets,index)){
    for(unsigned channel=0;channel<3;++channel)temps[channel]=value->color[channel]-temps[4];temps[3]=value->light;break;
   }
  }
  sample.light=temps[3];sample.fog=temps[5];for(unsigned channel=0;channel<3;++channel)sample.color[channel]=std::min(255,temps[channel]+temps[4]);
  sample.flags=std::bit_cast<std::uint32_t>(temps[6]);sample.named=selected!=nullptr;sample.mapped=true;sample.material=selected;return sample;
 }
 const World* samplingWorld=world;Vec3 point=hit.position;
 if(hit.entity){const auto found=entities.find(hit.entity);if(found==entities.end())return sample;const auto& data=found->second;if(data.type!=4){if(bonus&&hit.modernTexture&&data.type==2&&data.model&&hit.triangle<data.model->triangles.size())return {128,50,{128,128,128},true};return sample;}samplingWorld=world&&data.model==&world->mesh?world:&assets.world(data.file);point=wInverseRotate(wSub(point,position(hit.entity)),angles(hit.entity));const auto scale=wScale(*this,hit.entity);point={point.x/scale.x,point.y/scale.y,point.z/scale.z};}
 if(!samplingWorld||!hit.material)return sample;
 if(!bonus)assets.initialize_native_lightmap_tables(global("gamma").raw(),global("d3d_monochrome").bits!=0,renderer!=nullptr);
 const auto value=wTextureSample(*samplingWorld,*hit.material,hit.triangle,point,hit.modernTexture,bonus,assets);
 auto& temps=bonus?a7LegacyTextureTemps_:a6LegacyTextureTemps_;
 const bool group=hit.material->raw.size()>=44&&hit.triangle<samplingWorld->mesh.triangles.size()&&samplingWorld->mesh.triangles[hit.triangle].sourceFace<samplingWorld->faces.size();
 if(!hit.modernTexture&&group&&hit.material->texture){temps[4]=std::bit_cast<std::int8_t>(hit.material->raw[42]);temps[5]=std::bit_cast<std::int8_t>(hit.material->raw[43]);temps[6]=std::bit_cast<std::int32_t>(hit.material->flags);}
 if(value){sample=*value;if(!hit.modernTexture){for(std::size_t i=0;i<3;++i)temps[i]=sample.color[i]-temps[4];temps[3]=sample.light;}}
 else if(!hit.modernTexture&&group){
  // 00452180 / 10055b00 retain texels on UV miss and all metadata on a NULL BMAP.
  sample.light=temps[3];sample.fog=temps[5];for(std::size_t i=0;i<3;++i)sample.color[i]=temps[i]+temps[4];sample.flags=std::bit_cast<std::uint32_t>(temps[6]);sample.named=bool(hit.material->texture);sample.mapped=true;
 }
 for(auto& channel:sample.color)channel=std::min(255,channel);return sample;
}
void Engine::observe_entity_light(ObjectId id,EntityData& data){
 auto& cache=data.light;const auto p=position(id);const auto entityFlags=flags(id);
 const bool bonus=state.profile==SemanticProfile::BonusA7;
 if(cache.observed){
  if(p.x!=cache.position.x||p.y!=cache.position.y||p.z!=cache.position.z||data.model!=cache.model)cache.dirty=true;
  const auto changed=(entityFlags^cache.flags)&0xffff00u;
  if(bonus?(changed&0x40000u)!=0:changed&&(entityFlags&0x40000u))cache.dirty=true;
 }
 cache.position=p;cache.model=data.model;cache.flags=entityFlags;cache.observed=true;
}
void Engine::update_entity_light(ObjectId id,EntityData& data,const Camera& camera){
 auto& cache=data.light;if(!cache.observed)observe_entity_light(id,data);
 const auto p=cache.position;const auto entityFlags=cache.flags;const bool bonus=state.profile==SemanticProfile::BonusA7;
 const float far=std::max(camera.farClip,std::max(1.f,camera.nearClip)+1.f);
 const float radius=float((static_cast<long double>(float(global("shadow_range").raw()))/1024)*far*static_cast<long double>(0.01f));
 Vec3 distance=wSub(camera.position,p);if(global("d3d_entsort").raw()>1024)distance.z=0;
 const bool inRange=wDot(distance,distance)<radius*radius;
 // Classification is live per admitted preparation, even when the cached floor query is clean.
 const bool queryFloor=cache.dirty&&global("trace_floor").raw()!=0&&(entityFlags&0x60000u)!=0x20000u&&
  (!bonus||(data.physicsGeometry&&(std::uint32_t(field(id,"flags3"))&0x420000u)==0));
 cache.dirty=false;
 if(queryFloor){
 // Resource bounds are symmetric for MDL/sprite/WMB; only HMP keeps its scaled midpoint.
 // A6 00435580/004348e0 and A7 1003b300/1003add0 do not rotate these bounds.
 Vec3 origin=p;if(data.type==5&&data.model){const auto scale=wScale(*this,id);const auto a=data.model->min,b=data.model->max;auto center=[](float value,float lo,float hi,float scale){const float min=lo*scale,max=hi*scale;return float(static_cast<long double>(value)+(static_cast<long double>(min)+max)*.5L);};origin={center(p.x,a.x,b.x,scale.x),center(p.y,a.y,b.y,scale.y),center(p.z,a.z,b.z,scale.z)};}
 auto fixed=[](float value){return float(Value::from_double(value).real());};origin={fixed(origin.x),fixed(origin.y),fixed(origin.z)};
 Vec3 end=origin;const auto endZ=std::uint32_t(Value::from_double(origin.z).raw())-global("floor_range").bits;end.z=float(Value::from_raw(std::bit_cast<std::int32_t>(endZ)).real());
 // Supported Assets worlds are WMB6: A7 1005e610 leaves extra-mesh count +88 zero for binary versions <7.
 const bool modern=global("trace_floor").raw()>1024&&(!bonus||global("enable_polycollision").raw()>0);
 const bool special=(entityFlags&0x40000u)&&inRange;
 ProcessContext query{0,id,0};const auto shadow=std::uint32_t(global("shadow_mode").raw());
 TextureSample sample;
 auto cast=[&](std::uint32_t raw){const auto mode=std::uint32_t(std::bit_cast<std::int32_t>(shadow|raw)>>10);auto hit=collide(origin,end,{},mode,(mode&1)?id:0,&query,modern);hit.modernTexture=modern;if(hit.legacyQuery){prepare_legacy_hit(hit,origin,end,mode,id);sample=!hit.endpoint_?sample_texture_hit(hit):TextureSample{};}else{hit.textureScan=true;sample=hit.hit&&(modern||hit.fraction<1)?sample_texture_hit(hit):TextureSample{};}return hit;};
 auto hit=cast(special?0x44000000u:0x44060000u);
 auto fixedDistance=[&](const Hit& contact){if(contact.legacyQuery)return contact.resultRaw_;return contact.hit&&(modern||contact.fraction<1)?(bonus&&modern?wQueryFixedRaw(contact.queryScalar_):Value::from_double(wLength(wSub(end,origin))*contact.fraction).raw()):0;};
 // Save the special receiver before an entity hit causes an ordinary light-only retry.
 if(special&&fixedDistance(hit)>0){const auto target=hit.legacyQuery?hit.publicTarget_:hit.position,normal=hit.legacyQuery?hit.publicNormal_:hit.normal;cache.shadow.planeZ=float(Value::from_double(target.z).real());cache.shadow.negativeNormal={-float(Value::from_double(normal.x).real()),-float(Value::from_double(normal.y).real()),-float(Value::from_double(normal.z).real())};}
 if(special&&hit.entity){query.you=hit.entity;hit=cast(0x44060000u);}
 // TEX_SCAN already ran even for a real contact whose fixed distance truncates to zero.
 const auto floorDistance=fixedDistance(hit);
 cache.floorValid=floorDistance>0;
 if(!cache.floorValid){cache.floorDistance=-1024;if(!bonus){cache.scalar=127;cache.style=50;cache.delta={};}}
 else {
 cache.scalar=std::bit_cast<std::int16_t>(std::uint16_t(sample.light));cache.style=std::uint8_t(sample.fog);
 const auto mean=(std::int64_t(sample.color[0])*1024+std::int64_t(sample.color[1])*1024+std::int64_t(sample.color[2])*1024)/3;
 for(std::size_t i=0;i<cache.delta.size();++i)cache.delta[i]=std::bit_cast<std::int16_t>(std::uint16_t((std::int64_t(sample.color[i])*1024-mean)>>10));
 cache.floorDistance=floorDistance;
 }
 state.member(id,"floor_dist").set(Value::from_raw(cache.floorDistance));
 }
 const bool caster=(entityFlags&0x40000u)&&cache.floorValid&&cache.scalar>(global("shadow_threshold").raw()>>10)&&inRange;
 // A6 preserves its previous bit for unsupported nonzero stencil; A7 clears failed admission.
 if(caster||bonus||global("shadow_stencil").raw()==0)cache.shadow.caster=caster;
}
NativeEntityLightState Engine::prepare_entity_light(const DrawInstance& draw,const Camera& camera){
 auto found=entities.find(draw.objectId);const auto* object=state.get(draw.objectId);
 if(found==entities.end()||!object||!object->alive)return {draw.nativeLightScalar,draw.nativeLightDelta,draw.nativeLightBias,draw.nativeShadow};
 // Native accepts ENTITY declarations, not SKY; 10052750 also requires explicit VISIBLE.
 if(state.profile==SemanticProfile::BonusA7&&object->kind==Object::Kind::Entity&&!found->second.physicsGeometry&&found->second.type==5&&found->second.model&&field(draw.objectId,"visible")&&!field(draw.objectId,"invisible"))prepare_query_geometry(draw.objectId,found->second,true);
 // A6 0044f970 special declaration draws never enter 00437b90, so dirty40 survives.
 if(state.profile!=SemanticProfile::CampaignA6||found->second.physicsGeometry)update_entity_light(draw.objectId,found->second,camera);
 const auto& cache=found->second.light;return {cache.scalar,cache.delta,draw.nativeLightBias,cache.shadow};
}
void Engine::publish_movement_hit(const Hit& hit,Vec3 displacement){
 if((hit.status&1)||hit.fraction==1)return;
 set_vector(global_vector("normal"),hit.normal);
 const float reflected=float(((static_cast<long double>(hit.normal.z)*displacement.z+static_cast<long double>(hit.normal.y)*displacement.y)+static_cast<long double>(hit.normal.x)*displacement.x)*-2.f);
 set_vector(global_vector("bounce"),wAdd(displacement,wMul(hit.normal,reflected)));
}
Engine::Hit Engine::move_a6_legacy(Runtime& runtime,ProcessContext& query,ObjectId id,Vec3& current,Vec3 velocity,std::uint32_t mode){
 // A6 00453940: six bumps, five retained planes, fractional time and source contact XYZ.
 // c_move has a different four-pass solver (0046f730); Bonus is not routed here.
 const Vec3 originalVelocity=velocity,origin=current;Vec3 planeVelocity=velocity;
 std::array<Vec3,5> planes{};std::size_t planeCount=0;float time=1;Hit contact;
 bool worldEvent=false,blocked=false;++legacyMoveEpoch_;
 auto alive=[&]{const auto* o=state.get(id);return o&&o->alive&&entities.contains(id);};
 auto quantize=[](Vec3 v){return Vec3{wQueryFixed(v.x),wQueryFixed(v.y),wQueryFixed(v.z)};};
 auto block=[&](ObjectId other){if(std::uint32_t(field(id,"event_flags"))&8u){invoke_event(runtime,id,other,3);blocked=true;}};
 auto clip=[&](Vec3 v,Vec3 n){
  // 004537a0: unclamped MOVE_FRICTION, 1.01 overclip, strict .2*(1-friction) deadband.
  // The deadband is per call: frames clearly shorter than the campaign's 72 FPS cap (TIME_FRAME raw below 216, ~76 FPS,
  // margin for pacing jitter; only reachable with --graphics fps_limit) shrink it with the frame, so wall slides keep about
  // their 72 FPS threshold instead of sticking at high rates. At the original cap or slower it is A6 exactly.
  const float keep=float(1.L-static_cast<long double>(float(global("move_friction").raw()))/1024.L);
  const auto frameRaw=global("time_frame").raw();
  const float dead=float(static_cast<long double>(keep)*.2*(frameRaw>0&&frameRaw<216?frameRaw/216.L:1.L));
  const float back=float(((static_cast<long double>(v.x)*n.x+static_cast<long double>(v.y)*n.y)+static_cast<long double>(v.z)*n.z)*1.01);
  auto component=[&](float value,float normal){float out=float((static_cast<long double>(value)-static_cast<long double>(back)*normal)*keep);return out>-dead&&out<dead?0.f:out;};
  return Vec3{component(v.x,n.x),component(v.y,n.y),component(v.z,n.z)};
 };
 for(int bump=0;bump<6&&alive();++bump){
  Vec3 end{float(static_cast<long double>(time)*velocity.x+current.x),float(static_cast<long double>(time)*velocity.y+current.y),float(static_cast<long double>(time)*velocity.z+current.z)};
  current=quantize(current);end=quantize(end);
  const Hit h=collide_legacy(current,end,mode,id,&query,true);
  if(h.entity&&(mode&512)&&field(id,"push")>field(h.entity,"push")){
   const auto* recipient=state.get(h.entity);
   if(recipient&&recipient->alive&&recipient->kind==Object::Kind::Entity&&entities.contains(h.entity)&&(std::uint32_t(field(h.entity,"event_flags"))&16u)){Hit opposite=h;opposite.normal=wMul(h.normal,-1);publish_movement_hit(opposite,originalVelocity);invoke_event(runtime,h.entity,id,4);}
   if(auto found=entities.find(h.entity);found!=entities.end())found->second.legacyPushEpoch=legacyMoveEpoch_;
   --bump;continue; // 00453b9b retries without consuming one of the six bumps.
  }
  set_global("in_solid",{});set_global("in_passable",Value::from_int((h.status&0x40)!=0));
  if(h.hit)contact=h;
  if(h.status&1){
   block(h.entity);
   velocity=wMul(velocity,h.entity?.5f:.1f);
   if(!h.entity){
    current=wAdd(origin,velocity);
    if(blocked)return contact;
    if(!worldEvent&&(std::uint32_t(field(id,"event_flags"))&2u)){Hit reject=h;reject.status=0;reject.fraction=0;publish_movement_hit(reject,originalVelocity);invoke_event(runtime,id,0,1);worldEvent=true;}
    return contact;
   }
   if(auto found=entities.find(h.entity);found!=entities.end())found->second.legacyPushEpoch=legacyMoveEpoch_;
   if(!blocked&&(std::uint32_t(field(id,"event_flags"))&4u)){Hit reject=h;reject.status=0;reject.fraction=0;publish_movement_hit(reject,originalVelocity);invoke_event(runtime,id,h.entity,2);}
   continue;
  }
  if(h.fraction>0){planeVelocity=velocity;current=h.position;planeCount=0;}
  if(h.fraction==1)break;
  publish_movement_hit(h,originalVelocity);
  const Vec3 savedNormal=vector(global_vector("normal")),savedBounce=vector(global_vector("bounce"));
  if(h.entity){
   invoke_event(runtime,id,h.entity,2);
   const auto* recipient=state.get(h.entity);
   if(recipient&&recipient->alive&&recipient->kind==Object::Kind::Entity&&entities.contains(h.entity)&&(std::uint32_t(field(h.entity,"event_flags"))&32u)){Hit opposite=h;opposite.normal=wMul(h.normal,-1);publish_movement_hit(opposite,originalVelocity);invoke_event(runtime,h.entity,id,5);}
   set_vector(global_vector("normal"),savedNormal);set_vector(global_vector("bounce"),savedBounce);
  }else if(!worldEvent){invoke_event(runtime,id,0,1);worldEvent=true;}
  if(!alive())return contact;
  if(state.require(id).kind==Object::Kind::View||(mode&262144)==0)break; // 00453eb9 VIEW tag0x63, not USE_BOX.
  const bool contactCallback=(std::uint32_t(field(id,"event_flags"))&(1u<<(h.entity?2:1)))!=0;
  if(contactCallback&&(mode&0x800000u)){velocity={};break;}
  time=float(static_cast<long double>(time)-static_cast<long double>(h.fraction)*time);
  if(planeCount==planes.size()){velocity={};break;}
  planes[planeCount++]=h.normal;const Vec3 before=velocity;bool accepted=false;
  for(std::size_t i=0;i<planeCount;++i){
   Vec3 candidate=clip(planeVelocity,planes[i]);bool clear=true;
   for(std::size_t j=0;j<planeCount;++j)if(j!=i&&wHullDot(candidate,planes[j])<0){clear=false;break;}
   if(clear){velocity=candidate;accepted=true;break;}
  }
  if(!accepted){
   if(planeCount!=2){velocity={};break;}
   const auto a=planes[0],b=planes[1];
   const Vec3 crease{float(static_cast<long double>(a.y)*b.z-static_cast<long double>(a.z)*b.y),float(static_cast<long double>(a.z)*b.x-static_cast<long double>(a.x)*b.z),float(static_cast<long double>(a.x)*b.y-static_cast<long double>(a.y)*b.x)};
   const float along=float(wHullDot(crease,velocity));velocity=wMul(crease,along); // Do not normalize the original crease.
  }
  if((static_cast<long double>(originalVelocity.z)*velocity.z+static_cast<long double>(originalVelocity.x)*velocity.x)+static_cast<long double>(originalVelocity.y)*velocity.y<0){velocity={};block(0);break;}
  const Vec3 change=wSub(velocity,before);const auto arg=global_vector("vglide");
  const auto vectorId=arg.ref.owner?arg.ref.owner:arg.current().bits;
  const std::array delta{change.x,change.y,change.z};const std::array axes{"x","y","z"};
  for(std::size_t i=0;i<delta.size();++i){auto ref=state.member(vectorId,axes[i]);auto old=ref.get();ref.set(Value::from_raw(std::bit_cast<std::int32_t>(old.bits+std::uint32_t(wQueryFixedRaw(delta[i])))));}
 }
 if(!blocked&&alive()){
  const float intended=(std::abs(originalVelocity.x)+std::abs(originalVelocity.z))+std::abs(originalVelocity.y);
  const auto travelled=wSub(current,origin);const float actual=(std::abs(travelled.x)+std::abs(travelled.z))+std::abs(travelled.y);
  if(intended>4&&static_cast<long double>(actual)*100<intended)block(0);
 }
 return contact;
}
Engine::Hit Engine::move_a6_modern(Runtime& runtime,ProcessContext& query,ObjectId id,Vec3& current,Vec3 displacement,std::uint32_t mode){
 // 0046f730/0046f3b0: four passes, UNIT direction plus quantized DISTANCE.
 // Neither legacy plane history/MOVE_FRICTION/VGLIDE nor A7 penetration encoding belongs here.
 Vec3 direction=displacement;float distance=wQueryFixed(wNormalizeMovement(direction));
 Vec3 originalDirection=displacement;wNormalizeMovement(originalDirection);
 const auto extent=wExtent(*this,id);Hit contact;float minimumZ=-99.f;
 auto fixed=[](Vec3 p){return Vec3{wQueryFixed(p.x),wQueryFixed(p.y),wQueryFixed(p.z)};};
 for(int pass=0;pass<4&&distance>std::bit_cast<float>(0x3c23d70au);++pass){
  Vec3 desiredEnd=wAdd(current,wMul(direction,distance));const Vec3 segmentStart=fixed(current);
  for(float* component:{&direction.x,&direction.y,&direction.z})if(std::abs(*component)<std::bit_cast<float>(0x3ca3d70au))*component=0;
  wNormalizeMovement(direction); // 0046f45a: snapping must not shorten distance-based travel.
  const Vec3 segmentEnd=fixed(wAdd(segmentStart,wMul(direction,distance)));
  auto h=collide(segmentStart,segmentEnd,extent,mode,id,&query,true,&runtime);
  const auto* actor=state.get(id);if(!actor||!actor->alive||!entities.contains(id))return contact;
  h.modernTexture=true;h.textureScan=(mode&65536)!=0;
  if(pass==0)publish_hit(query,h,segmentStart); // 004706d0 precedes this pass's position write.
  if(!h.hit){
   const auto segment=wSub(segmentEnd,segmentStart);const float length=wQueryFixed(wLength(segment));
   set_position(id,wAdd(current,wMul(direction,length)));current=position(id);break;
  }
  contact=h;const float travel=std::max(0.f,wQueryFixed(h.queryScalar_));
  if(travel!=0)set_position(id,wAdd(current,wMul(direction,travel)));
  Hit publication=h;publication.fraction=0;publish_movement_hit(publication,displacement);
  invoke_event(runtime,id,h.entity,h.entity?2:1);
  if(h.entity){const auto* recipient=state.get(h.entity);if(recipient&&recipient->alive&&recipient->kind==Object::Kind::Entity&&entities.contains(h.entity)&&(std::uint32_t(field(h.entity,"event_flags"))&32u)){publication.normal=wMul(h.normal,-1);publish_movement_hit(publication,displacement);invoke_event(runtime,h.entity,id,5);}if(entities.contains(h.entity))physics.wake_query_body(h.entity);}
  actor=state.get(id);if(!actor||!actor->alive||!entities.contains(id))return contact;
  current=position(id); // Callbacks may legitimately move their public actor.
  if(!(mode&262144))break;
  if(minimumZ==-99.f)minimumZ=float(number("move_min_z")); // Capture after first contact callback.
  const auto n=h.normal;
  const float originDot=float((static_cast<long double>(current.y)*n.y+static_cast<long double>(current.z)*n.z)+static_cast<long double>(current.x)*n.x);
  const float planeDelta=float(((static_cast<long double>(n.x)*desiredEnd.x+static_cast<long double>(n.z)*desiredEnd.z)+static_cast<long double>(n.y)*desiredEnd.y)-originDot);
  const Vec3 scaledNormal=wMul(n,std::bit_cast<float>(0x3f8147aeu)),offset=wMul(scaledNormal,planeDelta);
  Vec3 remainder=wSub(wSub(desiredEnd,offset),current);
  if(std::bit_cast<std::int32_t>(global("disable_z_glide").bits&0xfffffc00u)>0||n.z<minimumZ)remainder.z=0;
  distance=wNormalizeMovement(remainder);direction=remainder;
  if(wHullDot(originalDirection,direction)<0)distance=0;
 }
 return contact;
}
Engine::Hit Engine::move_a7_modern(Runtime& runtime,ProcessContext& query,ObjectId id,Vec3& current,Vec3 displacement,std::uint32_t mode){
 // 10075e80/10075bb0/100757e0, separately qualified from both A6 movers.
 Vec3 direction=displacement;float distance=wQueryFixed(wNormalizeMovement(direction));
 const float minimumZ=float(number("move_min_z"));const auto extent=wExtent(*this,id);Hit contact;
 auto fixed=[](Vec3 p){return Vec3{wQueryFixed(p.x),wQueryFixed(p.y),wQueryFixed(p.z)};};
 for(int pass=0;pass<4&&distance>=std::bit_cast<float>(0x3c75c28fu);++pass){
  const Vec3 desiredEnd=wAdd(current,wMul(direction,distance));auto h=collide(current,desiredEnd,extent,mode,id,&query,true,&runtime);
  const auto* actor=state.get(id);if(!actor||!actor->alive||!entities.contains(id))return contact;
  h.modernTexture=true;h.textureScan=(mode&65536)!=0;if(pass==0)publish_hit(query,h,current);
  float scalar=h.queryScalar_;
  if(scalar>0)scalar=std::max(0.f,float(-3-wQueryFixedRaw(-scalar))/1024.f); // 10075bb0's -1024 conversion, not a fraction.
  if(!h.hit){set_position(id,fixed(wAdd(current,wMul(direction,wLength(wSub(desiredEnd,current))))));current=position(id);break;}
  contact=h;if(scalar>0)set_position(id,fixed(wAdd(current,wMul(direction,scalar))));
  Hit publication=h;publication.fraction=0;publish_movement_hit(publication,displacement);
  invoke_event(runtime,id,h.entity,h.entity?2:1);
  if(h.entity){const auto* recipient=state.get(h.entity);if(recipient&&recipient->alive&&recipient->kind==Object::Kind::Entity&&entities.contains(h.entity)&&(std::uint32_t(field(h.entity,"event_flags"))&32u)){publication.normal=wMul(h.normal,-1);publish_movement_hit(publication,displacement);invoke_event(runtime,h.entity,id,5);}if(entities.contains(h.entity))physics.wake_query_body(h.entity);}
  actor=state.get(id);if(!actor||!actor->alive||!entities.contains(id))return contact;
  current=position(id);if(!(mode&262144))break;
  Vec3 remainder;const auto n=h.normal;
  if(scalar<0)remainder=wMul(n,std::min(distance,std::max(-scalar,std::bit_cast<float>(0x3c8b4395u))));
  else{
   const float approach=float(-wHullDot(direction,n));
   if(approach>0&&approach<std::bit_cast<double>(0x3feccccccccccccdull)){
    const float endpointDot=float((static_cast<long double>(n.z)*current.z+static_cast<long double>(n.x)*current.x)+static_cast<long double>(n.y)*current.y);
    const float projection=float(((static_cast<long double>(n.x)*desiredEnd.x+static_cast<long double>(n.z)*desiredEnd.z)+static_cast<long double>(n.y)*desiredEnd.y)-endpointDot);
    const auto offset=wMul(wMul(n,std::bit_cast<float>(0x3f8147aeu)),projection);
    remainder=wMul(wSub(wSub(desiredEnd,offset),current),float(1.L-static_cast<long double>(float(global("move_friction").raw()))/1024.L));
   }else{
    // Original 1007584e/10075863 skips all three local stores here. Keep
    // native's existing deterministic response at this undefined-stack boundary.
    remainder=wSub(desiredEnd,current);const float into=wDot(remainder,n);if(into<0)remainder=wSub(remainder,wMul(n,into));
    remainder=wMul(remainder,float(1-std::clamp(number("move_friction"),0.,1.)));
   }
  }
  if((global("disable_z_glide").bits&0xfffffc00u)==1024u||n.z<minimumZ)remainder.z=0;
  distance=wNormalizeMovement(remainder);direction=remainder;mode&=0xfffea3ffu;
 }
 return contact;
}
void Engine::publish_hit(ProcessContext& context,const Hit& hit,Vec3 start){
 auto vectorGlobal=[&](std::string_view name,Vec3 v){set_vector(global_vector(name),v);};
 auto textureName=[&](std::string_view name){auto value=global("tex_name");auto* object=value.kind==Value::Kind::Object?state.get(value.bits):nullptr;if(object&&object->kind==Object::Kind::String)object->text=name;else set_global("tex_name",new_string(std::string(name)));};
 if(hit.textureScan){
  set_global("tex_light",Value::from_int(128));set_global("tex_fog",Value{});
  if(!hit.modernTexture){for(int i=0;i<8;++i){char name[]="tex_flag1";name[8]=char('1'+i);set_global(name,Value{});}textureName({});}
  vectorGlobal("tex_color",hit.modernTexture?Vec3{}:vector(global_vector("nullvector")));
 }
 if(hit.legacyQuery){
  set_global("result",Value::from_raw(hit.resultRaw_));vectorGlobal("target",hit.publicTarget_);
  set_global("trace_hit",Value::from_int(hit.hit));set_global("hitvertex",Value{});
  if(hit.endpoint_)return; // 0045491c returns before NORMAL/status/owner publication.
  set_global("in_solid",Value::from_int(hit.inSolid_));set_global("in_passable",Value::from_int(hit.inPassable));set_global("on_passable",Value::from_int(hit.onPassable));
  vectorGlobal("normal",hit.publicNormal_);
 }else{
 // 10077890 publishes independent query60/61/64 even when no ordinary contact exists.
 if(hit.contentQuery){set_global("passable_ent",Value::object(hit.passableEntity));set_global("in_passable",Value::from_int(hit.inPassable));set_global("on_passable",Value::from_int(hit.onPassable&&!hit.inPassable));}
 set_global("trace_hit",Value::from_int(hit.hit));(void)context;
 set_global("hitvertex",Value::from_int(hit.vertex));
 vectorGlobal("target",hit.position);vectorGlobal("normal",hit.normal);Vec3 direction=wUnit(wSub(hit.position,start));vectorGlobal("bounce",wSub(direction,wMul(hit.normal,2*wDot(direction,hit.normal))));
 }
 if(hit.textureScan){
  if(hit.hit){
   if(hit.entity){auto it=entities.find(hit.entity);if(it!=entities.end()&&it->second.type!=4){textureName(it->second.file);if(state.profile==SemanticProfile::BonusA7&&hit.modernTexture){const auto sample=sample_texture_hit(hit);set_global("tex_light",Value::from_int(sample.light));set_global("tex_fog",Value::from_int(sample.fog));vectorGlobal("tex_color",{float(sample.color[0]),float(sample.color[1]),float(sample.color[2])});}return;}}
   const auto sample=sample_texture_hit(hit);if(!sample.mapped)return;
   set_global("tex_light",Value::from_int(sample.light));set_global("tex_fog",Value::from_int(sample.fog));
   vectorGlobal("tex_color",{float(sample.color[0]),float(sample.color[1]),float(sample.color[2])});
   for(int i=0;i<8;++i){bool flag=(sample.flags>>(16+i))&1;if(flag||!hit.modernTexture){char name[]="tex_flag1";name[8]=char('1'+i);set_global(name,Value::from_int(flag));}}
   if(sample.named)textureName(sample.material?sample.material->textureName:hit.material->textureName);
  }
 }
}
std::optional<Value> worldModelBuiltin(Engine&,Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
std::optional<Value> Engine::world_builtin(Runtime& runtime,ProcessContext& context,std::string_view name,std::span<const Argument> args){
 auto need=[&](std::size_t n){if(args.size()<n)throw std::runtime_error(std::string(name)+": missing arguments");};auto object=[&](std::size_t i){return args[i].current().bits;};auto numeric=[&](std::size_t i){return args[i].current().real();};
 const bool bonus=state.profile==SemanticProfile::BonusA7;
 // Named A6 c_* wrappers always use the modern backend; A7 wrappers share these two gates.
 auto cBackend=[&](bool cEntry,std::uint32_t& rawMode){if(!cEntry)return false;if(bonus&&a7WorldQueryRoute_)rawMode&=0xf7ffffffu;return !bonus||(global("enable_polycollision").raw()>=1024&&!(rawMode&0x08000000u));};
 if(name=="ent_create"||name=="ent_createlocal"||name=="create"){need(3);auto fn=args[2].current();if(fn&&fn.kind!=Value::Kind::Function)throw std::runtime_error("entity action is not a function");ObjectId id=create_entity(runtime,string(args[0].current()),vector(args[1]),fn.bits,context.my);if(name=="ent_createlocal")set_field(id,"local",1);context.you=id;return Value::object(id);}
 if(name=="level_load"){need(1);pendingLevel=string(args[0].current());pendingLevelLoad=true;return Value::from_int(1);}
 if(name=="ent_remove"||name=="remove"){
  need(1);ObjectId id=object(0);const Value result=name=="ent_remove"?Value::from_int(1):Value{};if(!id)return result;
  auto* o=state.get(id);if(!o)throw std::runtime_error("remove: invalid object");
  if(o->kind==Object::Kind::Particle){runtime.kill_for_entity(id);state.retire(id);activeParticles.erase(std::remove_if(activeParticles.begin(),activeParticles.end(),[&](const auto& p){return p.object==id;}),activeParticles.end());return result;}
  if(o->kind!=Object::Kind::Entity)return std::nullopt;remove_entity(runtime,id);return result;
 }
 const Model* previousModel=nullptr;bool previousOwned=false;
 if((name=="ent_morph"||name=="ent_purge")&&!args.empty()){const auto found=entities.find(object(0));if(found!=entities.end()){previousModel=found->second.model;previousOwned=bool(found->second.ownedModel);}}
 if(auto result=worldModelBuiltin(*this,runtime,context,name,args)){if(name=="ent_morph"||name=="ent_purge"){const auto id=object(0);const auto found=entities.find(id);if(previousModel&&found!=entities.end()&&found->second.model!=previousModel&&(previousOwned||name=="ent_purge"))queryGeometry_.erase(previousModel);if(bonus&&name=="ent_morph"&&found!=entities.end()&&found->second.model){if(!previousModel)initialize_query_actor(id,false);prepare_query_geometry(id,found->second,true);}}return result;}
 if(name=="trace"||name=="c_trace"){
  const bool explicitC=name=="c_trace",cEntry=explicitC||(bonus&&a7WorldQueryRoute_);need(explicitC?3:2);
  std::uint32_t rawMode=explicitC?args[2].current().bits:global("trace_mode").bits;const bool modern=cBackend(cEntry,rawMode);
  if(bonus&&cEntry)set_global("move_mode",Value::from_raw(std::bit_cast<std::int32_t>(rawMode)));
  const auto mode=std::uint32_t(std::bit_cast<std::int32_t>(rawMode)>>10);Vec3 start=vector(args[0]),end=vector(args[1]);collisionYou=context.you;collisionActor=context.my;Vec3 extent=!modern&&(mode&8192)&&context.my?wExtent(*this,context.my):Vec3{};if(!modern&&bonus&&(mode&8192)&&context.my){Vec3 center=wCenter(*this,context.my);start=wAdd(start,center);end=wAdd(end,center);}ObjectId ignored=(mode&1)?context.my:0;
  if(!modern&&!bonus){for(auto status:{"in_solid","in_passable","on_passable","result"})set_global(status,Value{});}
  Hit hit=collide(start,end,extent,mode,ignored,&context,modern,&runtime);
  if(hit.legacyQuery)prepare_legacy_hit(hit,start,end,mode,context.my);
  if(!modern&&bonus&&hit.hit&&hit.fraction>=1){hit={};hit.position=end;hit.textureScan=(mode&65536)!=0;}
  // A6 004546f0 always rewrites YOU storage 006441dc (hit entity, else NULL), IGNORE_YOU only filters candidates.
  hit.modernTexture=modern;if(modern)hit.textureScan=(mode&65536)!=0;publish_hit(context,hit,start);if(!(mode&2)||hit.legacyQuery)context.you=hit.endpoint_?0:hit.entity;const auto result=hit.legacyQuery?Value::from_raw(hit.resultRaw_):bonus&&modern?Value::from_raw(hit.hit?wQueryFixedRaw(hit.queryScalar_):0):Value::from_double(hit.hit?wLength(wSub(end,start))*hit.fraction:0);set_global("result",result);
  if(modern&&!(mode&2)&&context.you){const auto* recipient=state.get(context.you);if(!recipient||!recipient->alive||!entities.contains(context.you))context.you=0;}
  if(hit.legacyQuery&&hit.endpoint_)return result;
  const bool reverseNormal=hit.legacyQuery&&hit.entity&&(mode&(1024|2048));Vec3 savedNormal{};
  if(reverseNormal){savedNormal=vector(global_vector("normal"));set_vector(global_vector("normal"),wMul(savedNormal,-1));}
  if(!modern){if(hit.entity&&(mode&1024))invoke_event(runtime,hit.entity,context.my,9);if(hit.entity&&(mode&2048))invoke_event(runtime,hit.entity,context.my,11);}
  if(reverseNormal)set_vector(global_vector("normal"),savedNormal);return result;
 }
 if(name=="ent_move"||name=="c_move"){
  const bool explicitC=name=="c_move",cEntry=explicitC||(bonus&&a7WorldQueryRoute_);need(explicitC?4:2);ObjectId id=explicitC?object(0):context.my;if(!id||!entities.contains(id)||!state.require(id).alive)throw std::runtime_error("movement requires a live entity");
  std::uint32_t rawMode=explicitC?args[3].current().bits:global(bonus&&a7WorldQueryRoute_?"trace_mode":"move_mode").bits;const bool modern=cBackend(cEntry,rawMode);
  if(cEntry)set_global("move_mode",Value::from_raw(std::bit_cast<std::int32_t>(rawMode)));
  const auto mode=std::uint32_t(std::bit_cast<std::int32_t>(rawMode|0x00800400u)>>10);ObjectId ignoredYou=context.you;collisionYou=ignoredYou;collisionActor=id;auto query=context;query.my=id;const bool bonusModern=bonus&&modern,legacyA6=!bonus&&!modern;Vec3 start=position(id),delta=wAdd(wRotate(vector(args[explicitC?1:0]),angles(id)),vector(args[explicitC?2:1])),centerOffset=modern||legacyA6?Vec3{}:wCenter(*this,id),current=wAdd(start,centerOffset),extent=wExtent(*this,id),remaining=delta;Hit contact;bool ownPass=flags(id)&512;
  if(modern){set_global("in_passable",{});set_global("in_solid",{});}
  auto moving=[&]{return bonusModern?(std::abs(remaining.x)>std::bit_cast<float>(0x38d1b717u)||std::abs(remaining.y)>std::bit_cast<float>(0x38d1b717u)||std::abs(remaining.z)>std::bit_cast<float>(0x38d1b717u)):legacyA6?(remaining.x!=0||remaining.y!=0||remaining.z!=0):wLength(remaining)>1e-6f;};
  const bool requestedMovement=moving();
  if(legacyA6&&requestedMovement&&!ownPass)for(auto output:{"normal","bounce","vglide"})set_vector(global_vector(output),{});
  if((modern||legacyA6)&&ownPass)current=wAdd(current,remaining);
  else if(legacyA6){if(requestedMovement)contact=move_a6_legacy(runtime,query,id,current,delta,mode);}
  else if(modern)contact=bonusModern?move_a7_modern(runtime,query,id,current,delta,mode):move_a6_modern(runtime,query,id,current,delta,mode);
  else for(int step=0;step<6&&moving();++step){Hit h=collide(current,wAdd(current,remaining),extent,mode,id,&query,false,&runtime);if(!entities.contains(id)||!state.require(id).alive)return Value{};if(!h.hit||ownPass){current=wAdd(current,remaining);if(h.hit)contact=h;break;}contact=h;float length=wLength(remaining),fraction=std::max(0.f,h.fraction-(length>0?0.001f/length:0));current=wAdd(current,wMul(remaining,fraction));if(!(mode&262144))break;remaining=wMul(remaining,1-fraction);float into=wDot(remaining,h.normal);if(into<0)remaining=wSub(remaining,wMul(h.normal,into));remaining=wMul(remaining,float(1-std::clamp(number("move_friction"),0.,1.)));}
  if(!entities.contains(id)||!state.require(id).alive)return Value{};
  contact.modernTexture=modern;contact.textureScan=modern&&(mode&65536)!=0;contact.contentQuery=false; // Segment publication is source-ordered, not replayed from the final contact.
  Vec3 destination=wSub(current,centerOffset);const auto storedDestination=bonusModern?Vec3{wQueryFixed(destination.x),wQueryFixed(destination.y),wQueryFixed(destination.z)}:destination;set_position(id,storedDestination);Vec3 travelled=wSub(destination,start);set_vector(global_vector("my_speed"),travelled);float distance=wLength(travelled);if(!contact.hit)contact.position=current;if(!legacyA6&&!modern)publish_hit(context,contact,wAdd(start,centerOffset));set_global("result",Value::from_double(distance));
  if(!bonus&&(legacyA6||modern)){if(ownPass){const auto content=wLegacyPointContent(world,storedDestination);set_global("in_passable",Value::from_int(content==-126));set_global("in_solid",Value::from_int(content==-128));}}
  else if(!modern){int worldContent=wWorldContent(world,destination);bool inPassable=worldContent==2;if(worldContent!=3)for(const auto& [other,data]:entities){if(other==id||(data.type!=4&&data.type!=5)||!(flags(other)&512))continue;Vec3 v=wSub(destination,wAdd(position(other),wCenter(*this,other))),ext=wExtent(*this,other);if(std::abs(v.x)<=ext.x&&std::abs(v.y)<=ext.y&&std::abs(v.z)<=ext.z){inPassable=true;break;}}set_global("in_passable",Value::from_int(inPassable));set_global("in_solid",Value::from_int(worldContent==3));}
  if(!legacyA6&&!(mode&2)){const auto* recipient=state.get(contact.entity);context.you=recipient&&recipient->alive&&entities.contains(contact.entity)?contact.entity:0;}if(!legacyA6&&!modern&&contact.hit){if(mode&(1024|2048)){if(contact.entity&&(mode&1024))invoke_event(runtime,contact.entity,id,9);if(contact.entity&&(mode&2048))invoke_event(runtime,contact.entity,id,11);}else{invoke_event(runtime,id,contact.entity,contact.entity?2:1);if(distance<=1e-6f)invoke_event(runtime,id,contact.entity,3);if(contact.entity)invoke_event(runtime,contact.entity,id,5);}}
  if((!bonusModern||ownPass)&&(mode&4096)&&field(id,"trigger_range")>0&&requestedMovement)throw std::runtime_error("positive-range legacy trigger actor tree is unsupported (106bfe64 / cached ENTITY278/284)");
  if(!legacyA6&&!modern&&(mode&512)){WorldQuerySnapshot snapshot(*this);for(ObjectId other:*snapshot.ids){const auto* o=state.get(other);if(other==id||!o||!o->alive||!entities.contains(other))continue;if((mode&2)&&other==ignoredYou)continue;if(field(other,"push")<field(id,"push")){auto ext=wExtent(*this,other),ctr=wAdd(position(other),wCenter(*this,other));float f=1;Vec3 n;if(wSweepBox(wAdd(start,wCenter(*this,id)),delta,extent,wSub(ctr,ext),wAdd(ctr,ext),f,n))invoke_event(runtime,other,id,4);}}}
  if(bonusModern){if(!ownPass)set_position(id,storedDestination);auto actor=queryActors_.find(id);if(actor==queryActors_.end())throw std::runtime_error("A7 modern c_move owner has no context (1007631b source fault)");refresh_query_pose(id,actor->second);}
  return Value::from_double(distance);
 }
 if(name=="scan_entity"){
  need(2);Vec3 origin=vector(args[0]),sector=vector(args[1]),direction{};ObjectId owner=args[0].ref.owner;if(!owner&&args[0].current().kind==Value::Kind::Object)owner=args[0].current().bits;if(owner){const auto& o=state.require(owner);if(o.kind==Object::Kind::Entity||o.kind==Object::Kind::View)direction=angles(owner);else{std::size_t base=args[0].ref.index;if(o.array.size()>=base+5){direction.x=float(o.array[base+3].value.real());direction.y=float(o.array[base+4].value.real());}else{direction.x=float(field(owner,"pan"));direction.y=float(field(owner,"tilt"));}}}
  float nearest=std::numeric_limits<float>::infinity();ObjectId nearestId=0;Vec3 nearestPosition{};ObjectId scanner=context.my;WorldQuerySnapshot snapshot(*this);
  for(ObjectId id:*snapshot.ids){const auto* o=state.get(id);if(id==scanner||!o||!o->alive||!entities.contains(id))continue;Vec3 p=position(id),v=wSub(p,origin);float distance=wLength(v);if(distance>sector.z)continue;float pan=std::atan2(v.y,v.x)*180/worldPi,tilt=std::atan2(v.z,std::hypot(v.x,v.y))*180/worldPi;auto difference=[](float a,float b){return std::remainder(a-b,360.f);};if(sector.x<360&&std::abs(difference(pan,direction.x))>sector.x*.5f)continue;if(sector.y>0&&sector.y<360&&std::abs(difference(tilt,direction.y))>sector.y*.5f)continue;if(sector.y==0&&sector.x<360){Vec3 facing=wRotate({1,0,0},direction);if(distance>0&&wDot(wMul(v,1/distance),facing)<std::cos(sector.x*.5f*worldPi/180))continue;}
   if(distance<nearest){nearest=distance;nearestId=id;nearestPosition=p;}set_global("result",Value::from_double(distance));{const bool idle=seamlessDoors&&number("LY_HedefLevel")<=0;invoke_event(runtime,id,scanner,8);if(idle)door_scan(runtime,id);}if(scanner&&state.require(scanner).alive)invoke_event(runtime,scanner,id,7);
  }
  context.you=nearestId;set_vector(global_vector("target"),nearestId?nearestPosition:origin);float result=nearestId?nearest:0;set_global("result",Value::from_double(result));return Value::from_double(result);
 }
 if(name=="effect"||name=="emit"){
  bool legacy=name=="emit";need(legacy?3:4);std::size_t functionIndex=legacy?2:0,countIndex=legacy?0:1,positionIndex=2-(legacy?1:0);Value action=args[functionIndex].current();if(action.kind!=Value::Kind::Function||!action.bits)throw std::runtime_error("particle callback is not a function");double amount=numeric(countIndex);if(amount<=0)return Value{};if(amount>double(std::numeric_limits<int>::max()))throw std::runtime_error("particle count out of range");std::size_t count=std::size_t(amount),maximum=std::size_t(std::max(0.,number("max_particles")));if(count>maximum||activeParticles.size()>maximum-count)throw std::runtime_error("particle capacity exceeded");Vec3 pos=vector(args[positionIndex]),velocity{};
  // A7 10036e9a..10036eae replaces only a NULL direction pointer with three zero words.
  // A captured .x reference remains a supplied vector even when its first component is zero.
  if(!legacy&&(args[3].ref.owner||args[3].current()))velocity=vector(args[3]);
  for(std::size_t i=0;i<count;++i){auto& p=state.create(Object::Kind::Particle);ObjectId id=p.id;for(auto key:{"x","y","z","vel_x","vel_y","vel_z","gravity","flags","skill_a","skill_b","skill_c","skill_d","age"})p.field(key).value=Value{};for(auto key:{"red","green","blue","alpha"})p.field(key).value=Value::from_int(key==std::string_view("alpha")?100:255);p.field("size").value=Value::from_int(1);p.field("lifespan").value=Value::from_int(80);p.field("bmap").value=Value::object(0);p.field("material").value=Value::object(0);p.field("function").value=action;set_field(id,"flags",legacy?0x01000000:0x03000000);set_position(id,pos);set_field(id,"vel_x",velocity.x);set_field(id,"vel_y",velocity.y);set_field(id,"vel_z",velocity.z);ParticleData data;data.object=id;data.callback=action.bits;data.owner=context.my;data.legacy=legacy;data.first=legacy;data.birthTick=global("total_ticks").raw();activeParticles.push_back(data);if(!legacy)runtime.start(action.bits,{},id,0);}
  set_global("num_particles",Value::from_int(std::int32_t(activeParticles.size())));return Value::from_int(std::int32_t(count));
 }
 return std::nullopt;
}
void worldFrameEvents(Engine& engine,Runtime& runtime){WorldQuerySnapshot snapshot(engine,true);for(ObjectId id:*snapshot.ids){const auto* object=engine.state.get(id);if(object&&object->alive&&engine.entities.contains(id))engine.invoke_event(runtime,id,0,12);}}
}

namespace k22 {
EntityData load_entity_asset(Assets& assets,std::string_view file,std::uint32_t& spriteFrames) {
 EntityData data;data.file=file;spriteFrames=1;
 auto dot=file.find_last_of('.');auto ext=dot==std::string_view::npos?std::string_view{}:file.substr(dot);
 CaseLess less;auto extension=[&](std::string_view expected){return !less(ext,expected)&&!less(expected,ext);};
 if(file.empty())data.type=0;
 else if(extension(".mdl")){data.type=2;data.model=&assets.model(file);}
 else if(extension(".wmb")){const World& map=assets.world(file);if(std::any_of(map.entities.begin(),map.entities.end(),[](const WorldEntity& entity){return entity.type==3||entity.type==7;}))throw std::runtime_error("embedded WMB entity loading requires native child-transform 0042fd30 and map-loader 0045b810 recovery: "+std::string(file));data.type=4;data.model=&map.mesh;}
 else if(extension(".hmp")){data.type=5;data.model=&assets.model(file);}
 else if(extension(".pcx")||extension(".tga")||extension(".bmp")||extension(".png")||extension(".jpg")||extension(".jpeg")||extension(".dds")) {
  data.type=1;data.ownedModel=std::make_unique<Model>();auto& quad=*data.ownedModel;
  auto image=assets.image_ptr(file);auto plus=file.find_last_of('+');
  if(plus!=std::string_view::npos&&plus+1<dot){auto digits=file.substr(plus+1,dot-plus-1);if(std::all_of(digits.begin(),digits.end(),[](char c){return c>='0'&&c<='9';})){spriteFrames=0;for(char c:digits){auto digit=std::uint32_t(c-'0');if(spriteFrames>image->width/10||(spriteFrames==image->width/10&&digit>image->width%10))throw std::runtime_error("sprite frame count exceeds image width");spriteFrames=spriteFrames*10+digit;}if(!spriteFrames||image->width%spriteFrames)throw std::runtime_error("invalid horizontal sprite frame strip");}}
  float halfWidth=float(image->width/spriteFrames)*.5f,halfHeight=float(image->height)*.5f;
  quad.min={0,-halfWidth,-halfHeight};quad.max={0,halfWidth,halfHeight};
  quad.vertices={{{0,-halfWidth,-halfHeight},{1,0,0},{0,1},{}},{{0,halfWidth,-halfHeight},{1,0,0},{1,1},{}},{{0,halfWidth,halfHeight},{1,0,0},{1,0},{}},{{0,-halfWidth,halfHeight},{1,0,0},{0,0},{}}};
  quad.triangles={{{0,1,2},0,0},{{0,2,3},0,0}};
  // A6 00459a90 cuts a "+N" strip into N frame textures of width/N. Native keeps one texture; a strip wider than a GPU
  // texture (s_alev+61.tga in Seytan_k, 18300 px) holds the same frames as a grid of cells. The shaders address frame f at
  // cell (f mod cols, f div cols) with uvScale (1/cols, 1/rows); an ordinary strip is the one-row case.
  // ponytail: fixed 16384 limit (GL_MAX_TEXTURE_SIZE / maxImageDimension2D of current desktop GPUs); query the device if smaller GPUs matter.
  Vec2 cell{1.f/float(spriteFrames),1};
  if(constexpr std::uint32_t maxWidth=16384;image->width>maxWidth){
   const std::uint32_t frameWidth=image->width/spriteFrames,cols=maxWidth/frameWidth,rows=cols?(spriteFrames+cols-1)/cols:0;if(!cols||std::uint64_t(rows)*image->height>maxWidth)throw std::runtime_error("sprite frame strip exceeds texture limits: "+std::string(file));
   auto grid=std::make_shared<Image>(*image);grid->width=cols*frameWidth;grid->height=rows*image->height;grid->rgba.assign(std::size_t(grid->width)*grid->height*4,0);
   for(std::uint32_t f=0;f<spriteFrames;++f)for(std::uint32_t y=0;y<image->height;++y)std::copy_n(image->rgba.begin()+(std::size_t(y)*image->width+std::size_t(f)*frameWidth)*4,std::size_t(frameWidth)*4,grid->rgba.begin()+((std::size_t(f/cols)*image->height+y)*grid->width+std::size_t(f%cols)*frameWidth)*4);
   image=std::move(grid);cell={1.f/float(cols),1.f/float(rows)};}
  Material skin;skin.name=file;skin.textureName=file;skin.texture=std::move(image);skin.uvScale=cell;quad.materials.push_back(std::move(skin));data.model=&quad;
 } else throw std::runtime_error("unsupported entity asset extension: "+std::string(file));
 return data;
}

ObjectId Engine::create_entity(Runtime& runtime,std::string_view file,Vec3 pos,FunctionId action,ObjectId parent) {
 if(!file.empty()){assets.initialize_native_lightmap_tables(global("GAMMA").raw(),global("D3D_MONOCHROME").bits!=0,renderer!=nullptr);assets.set_native_alpha_depth(global("D3D_ALPHADEPTH").real());assets.set_native_shadow_depth(global("D3D_SHADOWDEPTH").real(),renderer!=nullptr);}
 std::uint32_t spriteFrames;EntityData data=load_entity_asset(assets,file,spriteFrames);data.parent=parent;
 auto& object=state.create(Object::Kind::Entity,std::string(file));ObjectId id=object.id;object.text=file;
 for(auto name:{"x","y","z","pan","tilt","roll","u","v","speed_u","speed_v","pose","flags","event_flags","flags2","flags3","nosend_flags","ambient","lightrange","trigger_range","floor_dist","push","min_x","min_y","min_z","max_x","max_y","max_z","sound_volume","sound_range"})object.field(name).value={};
 for(auto name:{"scale_x","scale_y","scale_z","frame","skin"})object.field(name).value=Value::from_int(1);
 object.field("next_frame").value={};
 for(auto name:{"alpha","albedo"})object.field(name).value=Value::from_int(100);
 for(auto name:{"red","green","blue"})object.field(name).value=Value::from_int(128);
 for(auto name:{"entity1","entity2","string1","string2","panel1","panel2","material"})object.field(name).value=Value::object(0);
 for(auto name:{"event","action1","action2"})object.field(name).value=Value::function(0);
 object.field("parent").value=Value::object(parent);object.field("action1").value=Value::function(action);
 object.field("type").value=Value::from_int(std::int32_t(data.type));
 object.field("sprite_frames").value=Value::from_int(std::int32_t(spriteFrames));
 for(unsigned i=1;i<=100;++i)object.field("skill"+std::to_string(i)).value={};
 if(state.profile==SemanticProfile::CampaignA6&&!loadingPlacedEntities_)object.field("event_flags").value.bits|=1u; // 00431546–00431550: every dynamic birth, before optional action.
 if(state.profile==SemanticProfile::BonusA7&&!loadingPlacedEntities_&&data.model)object.field("event_flags").value=Value::from_raw(1); // 10039598 EMASK60|1, before initial mode3/construction.
 set_position(id,pos);if(data.model)wBounds(*this,id,data);
 entities.emplace(id,std::move(data));levelEntities_.push_back(id);
 if(state.profile==SemanticProfile::BonusA7&&!loadingPlacedEntities_)initialize_query_actor(id);
 set_global("num_entities",Value::from_int(std::int32_t(std::count_if(entities.begin(),entities.end(),[](const auto& entry){return entry.second.scannable;}))));
 if(action){if(state.profile==SemanticProfile::BonusA7)runtime.enqueue(action,{},id,0);else runtime.start(action,{},id,parent);}
 return id;
}

void Engine::remove_entity(Runtime& runtime,ObjectId id) {
 auto* object=state.get(id);if(!object||!object->alive)return;
 if(object->kind!=Object::Kind::Entity&&object->kind!=Object::Kind::Particle)throw std::runtime_error("entity_remove requires an entity or particle");
 physics.remove_entity(id);
 runtime.kill_for_entity(id);state.retire(id);
 if(collisionYou==id)collisionYou=0;
 if(collisionActor==id)collisionActor=0;
 for(auto& particle:activeParticles)if(particle.object==id||particle.owner==id){runtime.kill_for_entity(particle.object);state.retire(particle.object);}
 std::erase_if(activeParticles,[&](const ParticleData& p){return p.object==id||p.owner==id;});
 draws_.clear();shadows_.clear();frame_.instances={};frame_.shadows={};particles_.clear();frame_.particles={};
 if(auto data=entities.find(id);data!=entities.end()&&data->second.ownedModel){if(renderer)renderer->purge_model(data->second.model);meshOverrides.erase(data->second.model);queryGeometry_.erase(data->second.model);}
 entities.erase(id);std::erase(levelEntities_,id);
 queryActors_.erase(id);
 for(auto& slot:entityLightSlots_)if(slot.object==id)slot={};
 set_global("num_entities",Value::from_int(std::int32_t(std::count_if(entities.begin(),entities.end(),[](const auto& entry){return entry.second.scannable;}))));
}

void Engine::invoke_event(Runtime& runtime,ObjectId my,ObjectId you,int event) {
 auto* object=state.get(my);if(!object||!object->alive||object->kind!=Object::Kind::Entity)return;
 if(event<=0||event>=32)return;
 if(!(std::uint32_t(field(my,"event_flags"))&(1u<<unsigned(event))))return;
 FunctionId function=callback(my,"event");if(!function)return;
 if(you){auto* target=state.get(you);if(!target||!target->alive)you=0;}
 const auto arg1=global("_arg1"),arg2=global("_arg2");
 set_global("event_type",Value::from_int(event));
 if(state.profile==SemanticProfile::BonusA7){set_global("_arg1",Value::object(my));set_global("_arg2",Value::object(you));}
 auto restore=[&]{if(state.profile==SemanticProfile::BonusA7){set_global("event_type",{});set_global("_arg1",arg1);set_global("_arg2",arg2);}};
 const std::array args{Argument{Value::object(my),{}},Argument{Value::object(you),{}}};
 try{runtime.start(function,state.profile==SemanticProfile::BonusA7?std::span<const Argument>(args):std::span<const Argument>{},my,you);}catch(...){restore();throw;}restore();
}

void Engine::level_progress(Runtime& runtime,std::int64_t stage){
 if(state.profile!=SemanticProfile::BonusA7)return;
 auto hook=global("on_level");if(hook.kind!=Value::Kind::Function||!hook.bits)return;
 if(stage>1){levelPercent_=0;levelQuantum_=100.f/float(stage);}else if(stage==1)levelPercent_=100.f;else if(stage==0)levelPercent_+=levelQuantum_;else levelPercent_-=float(stage)*levelQuantum_;
 const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 if(stage<=0&&now-levelCallbackTime_<10.0)return;
 levelCallbackTime_=now;const std::array argument{Argument{Value::from_double(double(levelPercent_)),{}}};
 runtime.start(hook.bits,argument,0,0);make_frame();if(renderer)renderer->draw(frame_);
}
void Engine::load_level(Runtime& runtime,std::string_view filename) {
 // The caller can pass an entity-owned string; preserve it before tombstoning the old level.
 std::string level(filename);currentLevel=level;auto oldEntities=std::move(levelEntities_);levelEntities_.clear();
 const World* keep=door_load(level); // seamless doors: a prefetched destination survives the release (native/seamless_doors.cpp)
 assets.set_native_alpha_depth(global("D3D_ALPHADEPTH").real());
 assets.set_native_shadow_depth(global("D3D_SHADOWDEPTH").real(),renderer!=nullptr);
 assets.initialize_native_lightmap_tables(global("GAMMA").raw(),global("D3D_MONOCHROME").bits!=0,renderer!=nullptr);
 physics.clear();
 for(ObjectId id:oldEntities)remove_entity(runtime,id);
 for(auto& particle:activeParticles){runtime.kill_for_entity(particle.object);state.retire(particle.object);}
 activeParticles.clear();collisionYou=0;collisionActor=0;collisionNodes.clear();collisionOrder.clear();
 draws_.clear();shadows_.clear();particles_.clear();lights_.clear();frame_={};world=nullptr;
 entityLightSlots_={};
 for(auto& [id,data]:entities)if(!data.ownedModel){data.model=nullptr;data.light.dirty=true;}
 if(renderer)renderer->release_scene(keep?&keep->mesh:nullptr);meshOverrides.clear();queryActors_.clear();queryGeometry_.clear();assets.release_level(keep?std::string_view(level):std::string_view{});
 for(auto& [id,data]:entities)if(!data.ownedModel&&data.type&&!data.file.empty())data.model=data.type==4?&assets.world(data.file).mesh:&assets.model(data.file);
 set_global("time_level",Value{});
 if(level.empty())return;
 world=&assets.world(level);
 if(!world->sections[0].empty()){
  const auto& palette=world->sections[0];
  if(palette.size()<16+fontPalette_.size())throw std::runtime_error("truncated world palette: "+level);
  set_font_palette(std::span<const std::uint8_t>(palette).subspan(16,fontPalette_.size()));
 }
 if(state.profile==SemanticProfile::CampaignA6&&world->legacyHullBoundsPublished){
  set_vector(global_vector("hull_narrowmin"),world->legacyHulls[1].lower);set_vector(global_vector("hull_narrowmax"),world->legacyHulls[1].upper);
  set_vector(global_vector("hull_fatmin"),world->legacyHulls[2].lower);set_vector(global_vector("hull_fatmax"),world->legacyHulls[2].upper);
 }
 if(state.profile==SemanticProfile::BonusA7&&world->mesh.formatVersion>=6&&(world->infoFlags&4))a7WorldQueryRoute_=true;
 // Coarse, measured Engine work: decoded resource/environment, collision build,
 // each entity birth and configured parent/attributes, then actual preparation.
 level_progress(runtime,std::int64_t(world->entities.size())*2+3);
 bool dome=false,cube=false;for(const auto& material:world->mesh.materials)if((material.flags&15u)==5u&&material.texture){const auto& image=*material.texture;if(image.width==2*image.height)dome=true;if(image.width==6*image.height)cube=true;}
 skySpeed_={1,1};if(dome&&!cube){const auto speed=vector(global_vector("SKY_SPEED"));skySpeed_={speed.x,speed.y};}
 auto setColor=[&](std::string_view name,Color color){set_vector(global_vector(name),{color.b*255,color.g*255,color.r*255});};
 setColor("sky_color",world->ambient);
 Argument sunAngle=global_vector("sun_angle");Vec3 angle=vector(sunAngle);angle.x=world->sunAzimuth;angle.y=world->sunElevation;set_vector(sunAngle,angle);
 for(unsigned i=0;i<4;++i)setColor("d3d_fogcolor"+std::to_string(i+1),world->fogColors[i]);
 set_global("fog_color",Value::from_int(world->fogEnd>world->fogStart?1:0));
 if(world->fogEnd>world->fogStart)if(auto* camera=state.get(global("camera").bits);camera&&camera->kind==Object::Kind::View){set_field(camera->id,"fog_start",world->fogStart);set_field(camera->id,"fog_end",world->fogEnd);}
 level_progress(runtime,0);
 const auto& collision=world->collision;collisionOrder.resize(collision.triangles.size());std::iota(collisionOrder.begin(),collisionOrder.end(),0u);
 if(!collisionOrder.empty()) {
  struct Bounds {Vec3 min,max,center;};std::vector<Bounds> bounds;bounds.reserve(collisionOrder.size());
  for(auto& triangle:collision.triangles){Vec3 a=collision.vertices.at(triangle[0]),b=collision.vertices.at(triangle[1]),c=collision.vertices.at(triangle[2]);Vec3 lo=wMin(a,wMin(b,c)),hi=wMax(a,wMax(b,c));bounds.push_back({lo,hi,wMul(wAdd(lo,hi),.5f)});}
  collisionNodes.reserve(collisionOrder.size()*2);
  auto build=[&](auto&& self,std::uint32_t begin,std::uint32_t end)->std::uint32_t {
   std::uint32_t index=std::uint32_t(collisionNodes.size());collisionNodes.emplace_back();Vec3 lo=bounds[collisionOrder[begin]].min,hi=bounds[collisionOrder[begin]].max;
   for(auto at=begin+1;at<end;++at){lo=wMin(lo,bounds[collisionOrder[at]].min);hi=wMax(hi,bounds[collisionOrder[at]].max);}
   collisionNodes[index].min=lo;collisionNodes[index].max=hi;
   if(end-begin<=8){collisionNodes[index].begin=begin;collisionNodes[index].count=end-begin;return index;}
   Vec3 span=wSub(hi,lo);unsigned axis=span.y>span.x?1:0;if(span.z>(axis==0?span.x:span.y))axis=2;
   auto coordinate=[&](std::uint32_t triangle){const Vec3& c=bounds[triangle].center;return axis==0?c.x:axis==1?c.y:c.z;};auto middle=begin+(end-begin)/2;
   std::nth_element(collisionOrder.begin()+begin,collisionOrder.begin()+middle,collisionOrder.begin()+end,[&](std::uint32_t a,std::uint32_t b){return coordinate(a)<coordinate(b);});
   auto left=self(self,begin,middle),right=self(self,middle,end);collisionNodes[index].left=left;collisionNodes[index].right=right;return index;
  };
  build(build,0,std::uint32_t(collisionOrder.size()));
 }
 level_progress(runtime,0);
 std::vector<ObjectId> created;created.reserve(world->entities.size());
 loadingPlacedEntities_=true;
 try{for(const auto& source:world->entities){created.push_back(create_entity(runtime,source.type==4?std::string_view{}:std::string_view(source.file),source.position,0));level_progress(runtime,0);}}catch(...){loadingPlacedEntities_=false;throw;}
 loadingPlacedEntities_=false;
 std::vector<ObjectId> entityRecords;entityRecords.reserve(created.size());
 for(std::size_t i=0;i<created.size();++i)if(world->entities[i].type==3||world->entities[i].type==7)entityRecords.push_back(created[i]);
 std::vector<std::pair<ObjectId,FunctionId>> actions;actions.reserve(created.size());
 CaseLess less;
 for(std::size_t i=0;i<created.size();++i) {
  const auto& source=world->entities[i];ObjectId id=created[i];auto& object=state.require(id);auto& data=entities.at(id);object.name=source.name;
  data.scannable=source.type==3||source.type==7;
  object.field("name").value=new_string(source.name);object.field("file").value=new_string(source.file);
  if(source.type==4){data.file=source.file;object.text=source.file;}
  object.field("object_index").value=Value::from_int(std::int32_t(source.objectIndex));
  object.field("record_type").value=Value::from_int(std::int32_t(source.type));
  object.field("path_index").value=Value::from_int(std::int32_t(source.pathIndex));
  object.field("attached_entity_index").value=Value::from_int(std::int32_t(source.attachedEntityIndex));
  if(source.attachedEntityIndex){if(source.attachedEntityIndex>=entityRecords.size())throw std::runtime_error("WMB entity attachment outside entity record list");data.parent=entityRecords[source.attachedEntityIndex];object.field("parent").value=Value::object(data.parent);}
  set_field(id,"flags",source.flags);set_field(id,"pan",source.angle.x);set_field(id,"tilt",source.angle.y);set_field(id,"roll",source.angle.z);
  set_field(id,"scale_x",source.scale.x);set_field(id,"scale_y",source.scale.y);set_field(id,"scale_z",source.scale.z);
  if(data.model&&state.profile==SemanticProfile::CampaignA6){
   // 004315d0 ASM 004316be-004316e8: WMB stores the resource bottom; placed Z -= SCALE_Z*resource min_z (raw, truncated).
   const float resourceMinZ=data.type==2?data.model->resourceMin.z:data.model->min.z; // MDL resource box is frame 0 (assets.hpp).
   auto& z=object.field("z").value;z=Value::from_raw(std::int32_t(std::trunc(double(float(z.raw()))-double(float(object.field("scale_z").value.raw()))*resourceMinZ)));
   object.field("flags2").value.bits&=~3u;wBounds(*this,id,data);} // 004315d0: one 00430920 pass at the placed SCALE.
  else if(data.model){Vec3 a=data.model->min,b=data.model->max,s=source.scale;a={a.x*s.x,a.y*s.y,a.z*s.z};b={b.x*s.x,b.y*s.y,b.z*s.z};Vec3 lo=wMin(a,b),hi=wMax(a,b);set_field(id,"min_x",lo.x);set_field(id,"min_y",lo.y);set_field(id,"min_z",lo.z);set_field(id,"max_x",hi.x);set_field(id,"max_y",hi.y);set_field(id,"max_z",hi.z);}
  set_field(id,"ambient",(source.ambient.r+source.ambient.g+source.ambient.b)*100/3);set_field(id,"albedo",source.albedo*100);
  set_field(id,"sound_volume",source.soundVolume);set_field(id,"sound_range",source.soundRange);
  for(unsigned skill=0;skill<100;++skill)set_field(id,"skill"+std::to_string(skill+1),source.skills[skill]);
  if(!data.model){set_field(id,"min_x",source.regionMin.x);set_field(id,"min_y",source.regionMin.y);set_field(id,"min_z",source.regionMin.z);set_field(id,"max_x",source.regionMax.x);set_field(id,"max_y",source.regionMax.y);set_field(id,"max_z",source.regionMax.z);}
  if(!source.string1.empty())object.field("string1").value=new_string(source.string1);
  if(!source.string2.empty())object.field("string2").value=new_string(source.string2);
  if(!source.material.empty()){Value material=global(source.material);auto* target=state.get(material.bits);if(material.kind!=Value::Kind::Object||!target||target->kind!=Object::Kind::Material)throw std::runtime_error("unknown WMB material: "+source.material);object.field("material").value=material;}
  if(source.pathIndex){if(source.pathIndex>world->paths.size())throw std::runtime_error("WMB entity path index outside path list");data.path=source.pathIndex-1;data.node=0;data.hasPath=true;object.field("path").value=new_string(world->paths[data.path].name);}
  else if(!source.path.empty()){auto found=std::find_if(world->paths.begin(),world->paths.end(),[&](const Path& path){return !less(path.name,source.path)&&!less(source.path,path.name);});if(found==world->paths.end())throw std::runtime_error("unknown WMB entity path: "+source.path);data.path=std::size_t(found-world->paths.begin());data.node=0;data.hasPath=true;object.field("path").value=new_string(source.path);}
  if(!source.action.empty()){if(state.profile==SemanticProfile::BonusA7)set_field(id,"event_flags",std::uint32_t(field(id,"event_flags"))|1u); // WDL startup sets101f59f0|1; 100399fa admits this serialized action.
   FunctionId action=runtime.function(source.action);if(!action)throw std::runtime_error("unknown WMB entity action: "+source.action);set_field(id,"dynamic",1);object.field("action1").value=Value::function(action);actions.emplace_back(id,action);}
  level_progress(runtime,0);
 }
 if(state.profile==SemanticProfile::BonusA7)for(ObjectId id:created)initialize_query_actor(id);
 set_global("num_entities",Value::from_int(std::int32_t(std::count_if(entities.begin(),entities.end(),[](const auto& entry){return entry.second.scannable;}))));
 physics.load_world();
 if(state.profile==SemanticProfile::BonusA7){prepare(runtime);viewsPrepared_=true;level_progress(runtime,0);level_progress(runtime,1);}
 if(state.profile==SemanticProfile::CampaignA6)apply_character_lock(runtime,actions);
 for(auto [id,action]:actions){auto* object=state.get(id);if(object&&object->alive)runtime.enqueue(action,{},id,0);}
}
}

#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
namespace k22 {
namespace {
bool wmEqual(std::string_view a,std::string_view b){CaseLess less;return !less(a,b)&&!less(b,a);}
EntityData& wmEntity(Engine& e,ObjectId id,bool hydrate=true){auto& d=e.register_entity(id);if(hydrate&&!d.model&&!d.file.empty()){e.assets.set_native_alpha_depth(e.global("D3D_ALPHADEPTH").real());e.assets.set_native_shadow_depth(e.global("D3D_SHADOWDEPTH").real(),e.renderer!=nullptr);std::uint32_t frames=1;auto loaded=load_entity_asset(e.assets,d.file,frames);d.ownedModel=std::move(loaded.ownedModel);d.model=loaded.model;d.type=loaded.type;d.light.dirty=true;e.set_field(id,"sprite_frames",frames);}return d;}
const Model& wmModel(const EntityData& d){if(!d.model)throw std::runtime_error("entity has no model geometry");return *d.model;}
std::size_t wmSourceCount(const Model& m){return m.sourceVertices.empty()?m.vertices.size():m.sourceVertices.size();}
struct WmPose {std::size_t first=0,next=0;float blend=0;};
WmPose wmPose(const Engine& e,ObjectId id,const Model& m){if(m.frames.empty())return {};double frame=e.field(id,"frame");if(!std::isfinite(frame)||frame<1||frame>=double(m.frames.size())+1)throw std::runtime_error("entity frame outside model animation");auto first=std::size_t(std::floor(frame)-1);double next=e.field(id,"next_frame");std::size_t second=next>=1?std::size_t(next-1):std::min(first+1,m.frames.size()-1);if(second>=m.frames.size())throw std::runtime_error("entity next_frame outside model animation");return {first,second,float(frame-std::floor(frame))};}
Vec3 wmFrameVertex(const Engine& e,const Model& m,std::size_t frame,std::size_t index,bool source,bool normal){auto model=e.meshOverrides.find(&m);if(model!=e.meshOverrides.end()){auto changed=model->second.find(frame);if(changed!=model->second.end())return (source?(normal?changed->second.sourceMeshNormals:changed->second.sourceMesh):(normal?changed->second.meshNormals:changed->second.mesh)).at(index);}if(m.frames.empty()){const auto& v=(source&&!m.sourceVertices.empty()?m.sourceVertices:m.vertices).at(index);return normal?v.normal:v.position;}const auto& pose=m.frames.at(frame);const auto& vertices=source?(normal?pose.sourceNormals:pose.sourcePositions):(normal?pose.normals:pose.positions);if(index>=vertices.size())throw std::runtime_error("unsupported unevaluated animation pose: complete vertex positions/normals required");return vertices[index];}
Vec3 wmSourceVertex(const Engine& e,ObjectId id,const EntityData& d,std::size_t index,bool normal=false,bool interpolate=true){const auto& m=wmModel(d);auto pose=wmPose(e,id,m);bool source=!m.sourceVertices.empty();Vec3 first=wmFrameVertex(e,m,pose.first,index,source,normal);if(!interpolate||!pose.blend)return first;Vec3 next=wmFrameVertex(e,m,pose.next,index,source,normal);Vec3 value=wAdd(wMul(first,1-pose.blend),wMul(next,pose.blend));return normal?wUnit(value):value;}
MeshOverride& wmMaterialize(Engine& e,ObjectId id,const EntityData& d){const auto& m=wmModel(d);auto frame=wmPose(e,id,m).first;auto model=e.meshOverrides.find(&m);if(model!=e.meshOverrides.end()){auto existing=model->second.find(frame);if(existing!=model->second.end())return existing->second;}if(!e.nextMeshId)throw std::runtime_error("mutable mesh handle space exhausted");MeshOverride geometry;geometry.meshId=e.nextMeshId++;geometry.min={INFINITY,INFINITY,INFINITY};geometry.max={-INFINITY,-INFINITY,-INFINITY};geometry.sourceMesh.resize(wmSourceCount(m));geometry.sourceMeshNormals.resize(geometry.sourceMesh.size());for(std::size_t i=0;i<geometry.sourceMesh.size();++i){geometry.sourceMesh[i]=wmSourceVertex(e,id,d,i,false,false);geometry.sourceMeshNormals[i]=wmSourceVertex(e,id,d,i,true,false);geometry.min=wMin(geometry.min,geometry.sourceMesh[i]);geometry.max=wMax(geometry.max,geometry.sourceMesh[i]);}geometry.mesh.resize(m.vertices.size());geometry.meshNormals.resize(m.vertices.size());for(std::size_t i=0;i<m.vertices.size();++i){auto source=m.vertexSources.empty()?i:std::size_t(m.vertexSources.at(i));geometry.mesh[i]=geometry.sourceMesh.at(source);geometry.meshNormals[i]=geometry.sourceMeshNormals.at(source);}return e.meshOverrides[&m].emplace(frame,std::move(geometry)).first->second;}
void wmPurgeMesh(Engine& e,ObjectId id,const EntityData& d){if(!d.model)return;for(const auto& [other,data]:e.entities)if(other!=id&&data.model==d.model){const auto* o=e.state.get(other);if(o&&o->alive)return;}e.meshOverrides.erase(d.model);}
std::size_t wmVertexIndex(Value value,const Model& m){if(value.kind!=Value::Kind::Number||value.integer()<1||value.real()!=value.integer()||std::size_t(value.integer())>wmSourceCount(m))throw std::runtime_error("model vertex index outside original source vertices");return std::size_t(value.integer()-1);}
std::shared_ptr<const Image> wmSkin(const EntityData& d,std::size_t index){if(!d.model||index>=d.model->materials.size())return {};return d.model->materials[index].texture;}
void wmBindBitmap(Engine& e,ObjectId entity,const EntityData& d,std::size_t index,ObjectId bitmap){auto& o=e.state.require(bitmap);o.field("entity").value=Value::object(entity);o.field("skin").value=Value::from_int(std::int32_t(index+1));o.text=d.model&&index<d.model->materials.size()?d.model->materials[index].textureName:std::string{};if(auto image=wmSkin(d,index)){e.skinBitmapObjects.insert_or_assign(image.get(),bitmap);if(!e.bitmapImages.contains(bitmap))e.bitmapImages.emplace(bitmap,std::move(image));const auto& current=*e.bitmapImages.at(bitmap);o.field("width").value=Value::from_int(std::int32_t(current.width));o.field("height").value=Value::from_int(std::int32_t(current.height));}else{e.bitmapImages.erase(bitmap);o.field("width").value={};o.field("height").value={};}}
std::pair<std::size_t,std::size_t> wmScene(const Model& m,std::string_view scene){scene=scene.substr(0,16);std::size_t first=m.frames.size(),count=0;for(std::size_t i=0;i<m.frames.size();++i){std::string_view frame=m.frames[i].name;bool match=frame.size()>=scene.size()&&wmEqual(frame.substr(0,scene.size()),scene);if(match){if(first==m.frames.size())first=i;++count;}else if(count)break;}return {first,count};}
const Path* wmPath(const Engine& e,const EntityData& d){if(!e.world||!d.hasPath||d.path>=e.world->paths.size())return nullptr;const auto& path=e.world->paths[d.path];return path.nodes.empty()?nullptr:&path;}
bool wmSamePoint(Vec3 a,Vec3 b){return Value::from_double(a.x).raw()==Value::from_double(b.x).raw()&&Value::from_double(a.y).raw()==Value::from_double(b.y).raw()&&Value::from_double(a.z).raw()==Value::from_double(b.z).raw();}
}
Vec3 worldModelVertex(const Engine& e,ObjectId id,const EntityData& d,std::size_t renderIndex){const auto& m=wmModel(d);auto pose=wmPose(e,id,m);Vec3 first=wmFrameVertex(e,m,pose.first,renderIndex,false,false);if(!pose.blend)return first;return wAdd(wMul(first,1-pose.blend),wMul(wmFrameVertex(e,m,pose.next,renderIndex,false,false),pose.blend));}
std::optional<Value> worldModelBuiltin(Engine& e,Runtime&,ProcessContext& context,std::string_view name,std::span<const Argument> arguments){
 const bool owned=name=="bmap_for_entity"||name=="ent_animate"||name=="ent_cycle"||name=="ent_frame"||name=="ent_morph"||name=="ent_nextpoint"||name=="ent_path"||name=="ent_prevpoint"||name=="ent_purge"||name=="ent_vertex"||name=="ent_vertices"||name=="ent_waypoint"||name=="vec_for_mesh"||name=="vec_for_vertex"||name=="vec_to_mesh";
 if(!owned)return std::nullopt;
 auto count=[&](std::size_t n){if(arguments.size()!=n)throw std::runtime_error(std::string(name)+" argument count");};
 auto object=[&](std::size_t n){Value v=arguments[n].current();if(v.kind!=Value::Kind::Object||!v.bits)throw std::runtime_error(std::string(name)+" requires entity object");return v.bits;};
 if(name=="bmap_for_entity"){count(2);Value entity=arguments[0].current();if(!entity.bits)return Value::object(0);if(entity.kind!=Value::Kind::Object)throw std::runtime_error("bmap_for_entity requires entity object");ObjectId id=entity.bits;auto& d=wmEntity(e,id);Value requested=arguments[1].current();if(requested.kind!=Value::Kind::Number)throw std::runtime_error("bmap_for_entity selector must be numeric");if(!d.model||(d.type!=1&&!model_has_entity_skins(*d.model)))return Value::object(0);auto selected=requested.raw()>>10;if(!selected)selected=e.state.member(id,d.type==1?"frame":"skin").get().raw()>>10;std::size_t index=d.type==1?0:model_skin_index(*d.model,selected);auto image=wmSkin(d,index);if(!image)return Value::object(0);if(d.skinObjects.size()<=index)d.skinObjects.resize(index+1);if(!d.skinObjects[index]){auto shared=e.skinBitmapObjects.find(image.get());d.skinObjects[index]=shared==e.skinBitmapObjects.end()?e.state.create(Object::Kind::Bmap).id:shared->second;}wmBindBitmap(e,id,d,index,d.skinObjects[index]);return Value::object(d.skinObjects[index]);}
 if(name=="ent_morph"){count(2);ObjectId id=object(0);auto& old=wmEntity(e,id,false);std::string file(e.string(arguments[1].current()));e.assets.set_native_alpha_depth(e.global("D3D_ALPHADEPTH").real());e.assets.set_native_shadow_depth(e.global("D3D_SHADOWDEPTH").real(),e.renderer!=nullptr);std::uint32_t spriteFrames=1;EntityData replacement=load_entity_asset(e.assets,file,spriteFrames);replacement.path=old.path;replacement.node=old.node;replacement.hasPath=old.hasPath;replacement.parent=old.parent;replacement.scannable=old.scannable;replacement.physicsGeometry=old.physicsGeometry;
  e.draws_.clear();e.shadows_.clear();e.frame_.instances={};e.frame_.shadows={};
  // A6 0042fc50 / A7 10036a00 change resources, not the entity's sampled cache.
  const bool sameResource=old.model==replacement.model||(old.type==1&&replacement.type==1&&old.model&&replacement.model&&wmEqual(old.file,replacement.file));
  replacement.light=old.light;replacement.light.model=replacement.model;if(!sameResource)replacement.light.dirty=true;
  replacement.visibleLod=old.visibleLod;replacement.lightResident=old.lightResident;
  if(sameResource)replacement.skinObjects=std::move(old.skinObjects);if(old.ownedModel){if(e.renderer)e.renderer->purge_model(old.model);e.meshOverrides.erase(old.model);}old=std::move(replacement);auto& o=e.state.require(id);o.text=file;o.field("file").value=e.new_string(file);e.set_field(id,"type",old.type);e.set_field(id,"sprite_frames",spriteFrames);e.set_field(id,"frame",1);e.set_field(id,"next_frame",0);e.set_field(id,"skin",1);if(old.model)wBounds(e,id,old);return Value{};}
 if(name=="ent_animate"||name=="ent_frame"||name=="ent_cycle"){
  bool explicitEntity=name=="ent_animate";count(explicitEntity?4:2);ObjectId id=explicitEntity?object(0):context.my;auto& d=wmEntity(e,id);const auto& m=wmModel(d);Value sceneValue=arguments[explicitEntity?1:0].current();Value percentValue=arguments[explicitEntity?2:1].current();if(percentValue.kind!=Value::Kind::Number)throw std::runtime_error("animation percent must be numeric");double percent=percentValue.real();std::uint32_t mode=name=="ent_cycle"?1u:0u;
  const bool bonus=e.state.profile==SemanticProfile::BonusA7;
  if(explicitEntity){Value value=arguments[3].current();if(value.kind==Value::Kind::Number){if(bonus)mode=std::uint32_t(value.raw()>>10);else{if(value.integer()<0||value.real()!=value.integer())throw std::runtime_error("animation mode must be nonnegative integer");mode=std::uint32_t(value.integer());}}else if(value.kind==Value::Kind::Object){Vec3 v=e.vector(arguments[3]);if(v.x||v.y||v.z)throw std::runtime_error("animation mode vector must be NULLVECTOR");}else throw std::runtime_error("animation mode is not a flag value");}
  if(!m.bones.empty())throw std::runtime_error("unsupported skeletal animation subtype: A6 bone composition is not defined by recovered assets");if(mode&~(bonus?5u:1u))throw std::runtime_error(bonus?"unsupported animation mode: only vertex/sprite ANM_CYCLE and ANM_SKIP are recovered":"unsupported animation mode: only vertex/sprite ANM_CYCLE is used by recovered game");
  if(!sceneValue.bits){e.set_field(id,"frame",1);e.set_field(id,"next_frame",0);d.frame0=d.frame1=0;d.frameBlend=0;d.animation=0;return Value::from_int(1);}
  std::string_view scene=e.string(sceneValue);std::size_t first=0,frames=0;if(d.type==1){frames=std::size_t(std::max(1.0,e.field(id,"sprite_frames")));}else{auto range=wmScene(m,scene);first=range.first;frames=range.second;}if(!frames){e.set_global("result",{});return Value{};}
  if(bonus&&d.type!=1){
   // A7 1003c481–52f stores the percent fraction and absolute position as float32.
   float position=float(first);bool wrap=false;
   if(percentValue.raw()>0&&frames>1){auto raw=(mode&1)?percentValue.raw()%102400:std::min(percentValue.raw(),102400);float fraction=float(double(raw)/1024.0*.01);position=float(double(float((mode&1)?frames:frames-1))*fraction+float(first));wrap=(mode&1)&&position>float(first+frames-1);}
   // 1003c533–566 truncates ANM_SKIP only after deciding the cycle's next_frame.
   if(mode&4)position=std::trunc(position);
   Value result=Value::from_double(double(position)+1);e.state.member(id,"frame").set(result);e.set_field(id,"next_frame",wrap?double(first+1):0);
   d.frame0=std::uint32_t(result.integer()-1);d.frame1=std::uint32_t(wrap?first:std::min(std::size_t(d.frame0)+1,m.frames.size()-1));d.frameBlend=float(result.raw()%1024)/1024;d.animation=percent;e.set_global("result",result);return result;
  }
  double position=0;if(percent>0){if(mode&1)position=std::fmod(percent,100.0)*.01*double(frames);else position=std::min(percent,100.0)*.01*double(frames-1);}
  std::size_t at=std::min(std::size_t(std::floor(position)),frames-1),next=(mode&1)?(at+1)%frames:std::min(at+1,frames-1);float blend=float(position-std::floor(position));e.set_field(id,"frame",double(first+at+1)+blend);e.set_field(id,"next_frame",(mode&1)&&position>double(frames-1)?double(first+1):0);d.frame0=std::uint32_t(first+at);d.frame1=std::uint32_t(first+next);d.frameBlend=blend;d.animation=percent;Value result=e.state.member(id,"frame").get();e.set_global("result",result);return result;
 }
 if(name=="ent_vertices"){count(1);const auto& d=wmEntity(e,object(0));return Value::from_int(std::int32_t(wmSourceCount(wmModel(d))));}
 if(name=="ent_vertex"||name=="vec_for_mesh"||name=="vec_for_vertex"||name=="vec_to_mesh"){
  bool legacy=name=="ent_vertex";count(legacy?2:3);ObjectId id=legacy?context.my:object(1);auto& d=wmEntity(e,id);const auto& m=wmModel(d);
  // A6 vec_for_vertex 004040d0->00449dc0 mode0: index=(raw>>10)-1 clamped to [0,vertices-1], never rejected (Ebru's ebru_silah.mdl has 6 vertices, oyuncu2_ebru.wdl:70 asks 169).
  auto a6Clamp=[&](Value v){auto n=std::int64_t(v.raw()>>10)-1;return std::size_t(std::clamp<std::int64_t>(n,0,std::int64_t(wmSourceCount(m))-1));};
  std::size_t index=name=="vec_for_vertex"&&e.state.profile==SemanticProfile::CampaignA6&&wmSourceCount(m)?a6Clamp(arguments[2].current()):wmVertexIndex(arguments[legacy?1:2].current(),m);
  if(name=="vec_to_mesh"){if(d.type!=2&&d.type!=5)throw std::runtime_error("vec_to_mesh supports only models and non-chunked terrain");Vec3 v=e.vector(arguments[0]);auto& geometry=wmMaterialize(e,id,d);if(!m.vertexSources.empty()&&(m.sourceRenderOffsets.size()!=wmSourceCount(m)+1||m.sourceRenderIndices.size()!=geometry.mesh.size()))throw std::runtime_error("model lacks original-to-render vertex reverse map");if(d.type==5){auto previous=geometry.sourceMesh.at(index);v.x=previous.x;v.y=previous.y;}geometry.sourceMesh.at(index)=v;if(m.vertexSources.empty())geometry.mesh.at(index)=v;else for(auto at=m.sourceRenderOffsets.at(index);at<m.sourceRenderOffsets.at(index+1);++at)geometry.mesh.at(m.sourceRenderIndices.at(at))=v;geometry.min=wMin(geometry.min,v);geometry.max=wMax(geometry.max,v);++geometry.revision;return Value{};}
  if(name=="vec_for_mesh"&&d.type!=2&&d.type!=5)throw std::runtime_error("vec_for_mesh supports only models and non-chunked terrain");Vec3 v=wmSourceVertex(e,id,d,index,false,name!="vec_for_mesh");if(name!="vec_for_mesh"){Vec3 scale=wScale(e,id);v=wAdd(e.position(id),wRotate({v.x*scale.x,v.y*scale.y,v.z*scale.z},e.angles(id)));}e.set_vector(arguments[0],v);return Value{};
 }
 // A6.31.4 wrappers 4055d0/407f50/4055f0/405620 call 434d90/434f20/434fa0.
 // Legacy navigation deliberately uses exact XYZ and stored node order, not the newer directed-edge API.
 if(name=="ent_path"){count(1);Value value=arguments[0].current();if(!value.bits||!e.world)return Value{};std::string_view pathName=e.string(value);for(std::size_t i=0;i<e.world->paths.size();++i)if(wmEqual(e.world->paths[i].name,pathName)){const auto& path=e.world->paths[i];if(path.nodes.empty())return Value{};e.set_vector(e.global_vector("target"),path.nodes.front().position);if(context.my){auto& d=wmEntity(e,context.my,false);d.path=i;d.node=0;d.hasPath=true;e.set_field(context.my,"path_index",double(i+1));}return Value::from_int(std::int32_t(path.nodes.size()));}return Value{};}
 if(name=="ent_waypoint"){count(2);if(!context.my)return Value{};auto& d=wmEntity(e,context.my,false);const auto* path=wmPath(e,d);if(!path)return Value{};Value number=arguments[1].current();if(number.kind!=Value::Kind::Number)throw std::runtime_error("ent_waypoint node must be numeric");auto integer=number.raw()>=0?number.raw()/1024:-std::int32_t((std::int64_t(-std::int64_t(number.raw()))+1023)/1024);std::size_t index=std::size_t(std::clamp<std::int64_t>(std::int64_t(integer)-1,0,std::int64_t(path->nodes.size()-1)));e.set_vector(arguments[0],path->nodes[index].position);d.node=index;return Value::from_int(std::int32_t(path->nodes.size()));}
 if(name=="ent_nextpoint"||name=="ent_prevpoint"){count(1);if(!context.my)return Value{};auto& d=wmEntity(e,context.my,false);const auto* path=wmPath(e,d);if(!path)return Value{};Vec3 position=e.vector(arguments[0]);std::size_t current=0;while(current<path->nodes.size()&&!wmSamePoint(position,path->nodes[current].position))++current;std::size_t next=name=="ent_nextpoint"?(current+1>=path->nodes.size()?0:current+1):(current==0?path->nodes.size()-1:current-1);e.set_vector(arguments[0],path->nodes[next].position);d.node=next;return Value::from_int(std::int32_t(next+1));}
 if(name=="ent_purge"){
  count(1);ObjectId id=object(0);auto& d=wmEntity(e,id,false);if(d.type==4)throw std::runtime_error("unsupported ent_purge subtype: WMB map collision/world cache cannot be evicted while referenced");const Model* model=d.model;bool shared=false;for(const auto& [other,data]:e.entities)if(other!=id&&data.model==model){const auto* o=e.state.get(other);shared|=o&&o->alive;}
  e.draws_.clear();e.shadows_.clear();e.frame_.instances={};e.frame_.shadows={};
  if(e.renderer&&model){e.renderer->purge_model(model);for(const auto& material:model->materials)if(material.texture)e.renderer->purge_image(material.texture.get());}
  for(ObjectId bitmap:d.skinObjects)if(bitmap)e.bitmapImages.erase(bitmap);wmPurgeMesh(e,id,d);
  if(model&&!shared){for(const auto& material:model->materials){const Image* image=material.texture.get();if(!image)continue;bool referenced=false;for(const auto& [other,data]:e.entities){if(other==id||!data.model)continue;const auto* o=e.state.get(other);if(!o||!o->alive)continue;for(const auto& skin:data.model->materials)referenced|=skin.texture.get()==image;}if(!referenced)e.skinBitmapObjects.erase(image);}d.model=nullptr;d.light.dirty=true;if(d.ownedModel){d.ownedModel.reset();e.assets.purge_image(d.file);}else e.assets.purge_model(d.file);}
  return Value{};
 }
 throw std::runtime_error("internal model builtin dispatcher mismatch");
}
}

#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
namespace k22 {
namespace {
Value ui_value(const Object& o,std::string_view key){auto i=o.fields.find(key);return i==o.fields.end()?Value{}:i->second.value;}
float ui_number(const Object& o,std::string_view key,float fallback=0){auto i=o.fields.find(key);return i==o.fields.end()?fallback:float(i->second.value.real());}
bool ui_name(std::string_view a,std::string_view b){CaseLess less;return !less(a,b)&&!less(b,a);}
struct UIBitmap {const Image* image{};Rect pixels{};bool dynamic{};std::uint64_t serial{};};
bool ui_contains(Rect rect,float angle,float x,float y,bool inclusive=false,Vec2 panelOrigin={}){if(inclusive){x=std::floor(x-panelOrigin.x)+panelOrigin.x;y=std::floor(y-panelOrigin.y)+panelOrigin.y;}float cx=rect.x+rect.width/2,cy=rect.y+rect.height/2,r=-angle*.01745329252f,dx=x-cx,dy=y-cy;x=cx+dx*std::cos(r)-dy*std::sin(r);y=cy+dx*std::sin(r)+dy*std::cos(r);return x>=rect.x&&y>=rect.y&&(inclusive?x<=rect.x+rect.width&&y<=rect.y+rect.height:x<rect.x+rect.width&&y<rect.y+rect.height);}
// ponytail: stable insertion sort avoids frame-time allocations; use a retained merge buffer if distinct UI layers number in the thousands.
template<class T,class Less>void ui_order(std::vector<T>& values,Less less){for(std::size_t i=1;i<values.size();++i){T value=std::move(values[i]);std::size_t j=i;while(j&&less(value,values[j-1])){values[j]=std::move(values[j-1]);--j;}values[j]=std::move(value);}}
// Format only conversions actually represented by a typed WDL value. Never pass an unchecked script format to printf.
void ui_digits(std::string& result,Value format,double value,std::string_view text,std::string_view literal){
 std::array<char,512> buf{};
 if(format.kind==Value::Kind::Number){double f=std::abs(format.real());int width=int(f);if(width>100)throw std::runtime_error("DIGITS numeric format out of range");double limit=std::pow(10.0,width)-1;value=std::clamp(value,-limit,limit);std::array<char,32> spec{};std::snprintf(spec.data(),spec.size(),format.real()<0?"%%0%.1ff":"%%%.1ff",f);std::snprintf(buf.data(),buf.size(),spec.data(),value);result.assign(buf.data());return;}
 result.clear();result.reserve(literal.size()+32);
 for(std::size_t i=0;i<literal.size();){if(literal[i]!='%'){result+=literal[i++];continue;}std::size_t begin=i++;if(i<literal.size()&&literal[i]=='%'){result+='%';++i;continue;}while(i<literal.size()&&(literal[i]=='-'||literal[i]=='+'||literal[i]==' '||literal[i]=='#'||literal[i]=='0'))++i;while(i<literal.size()&&literal[i]>='0'&&literal[i]<='9')++i;if(i<literal.size()&&literal[i]=='.'){++i;while(i<literal.size()&&literal[i]>='0'&&literal[i]<='9')++i;}if(i==literal.size())throw std::runtime_error("unterminated DIGITS conversion");char c=literal[i++];std::array<char,128> spec{};if(i-begin>=spec.size())throw std::runtime_error("DIGITS conversion too long");std::copy_n(literal.data()+begin,i-begin,spec.data());int n=0;if(c=='f'||c=='g'||c=='e'||c=='F'||c=='G'||c=='E')n=std::snprintf(buf.data(),buf.size(),spec.data(),value);else if(c=='d'||c=='i')n=std::snprintf(buf.data(),buf.size(),spec.data(),int(value));else if(c=='s')n=std::snprintf(buf.data(),buf.size(),spec.data(),text.data()?text.data():"");else throw std::runtime_error("unsupported DIGITS conversion");if(n<0||std::size_t(n)>=buf.size())throw std::runtime_error("DIGITS output exceeds supported display width");result.append(buf.data(),std::size_t(n));}
}
}
std::uint32_t Engine::material_index(Value value){
 if(!value)return ~0u;
 const auto* object=state.get(value.bits);if(value.kind!=Value::Kind::Object||!object||!object->alive||object->kind!=Object::Kind::Material)throw std::runtime_error("render material requires live MATERIAL");
 auto& entry=materialIndices_[object->id];if(entry.generation==materialGeneration_)return entry.index;
 ScriptMaterialState material;material.sourceId=object->id;material.flags=ui_value(*object,"flags").bits;
 auto color=[&](std::string_view red,std::string_view green,std::string_view blue){return Color{ui_number(*object,red),ui_number(*object,green),ui_number(*object,blue),1};};
 material.ambient=color("ambient_red","ambient_green","ambient_blue");material.diffuse=color("diffuse_red","diffuse_green","diffuse_blue");material.specular=color("specular_red","specular_green","specular_blue");material.emissive=color("emissive_red","emissive_green","emissive_blue");
 material.power=ui_number(*object,"power");material.alpha=ui_number(*object,"alpha");material.albedoRaw=ui_value(*object,"albedo").raw();
 static constexpr std::array<std::string_view,8> skills{"skill1","skill2","skill3","skill4","skill5","skill6","skill7","skill8"};
 for(std::size_t i=0;i<skills.size();++i)material.skillBits[i]=ui_value(*object,skills[i]).bits;
 if(auto effect=materialEffects_.find(object->id);effect!=materialEffects_.end()){material.effect=effect->second.kind;material.effectGeneration=effect->second.generation;}
 static constexpr std::array<std::string_view,2> skins{"skin1","skin2"};
 for(std::size_t i=0;i<skins.size();++i)if(auto skin=ui_value(*object,skins[i]))material.skins[i]=&svc_bitmap(*this,skin);
 entry={materialGeneration_,std::uint32_t(materials_.size())};materials_.push_back(material);return entry.index;
}
void Engine::ensure_font_palette(std::string_view entry){
 if(fontPaletteSerial_)return;
 // Original00455240: startup input basename, palette.raw, then RGB332.
 entry=entry.substr(entry.find_last_of("/\\")==std::string_view::npos?0:entry.find_last_of("/\\")+1);
 const auto extension=entry.find_last_of('.');std::string input(entry.substr(0,extension));input+=".raw";
 auto load=[&](std::string_view name){const auto source=assets.read(name);if(source.size()<fontPalette_.size())throw std::runtime_error("truncated startup palette: "+std::string(name));set_font_palette(std::span<const std::uint8_t>(source).first(fontPalette_.size()));};
 if(!entry.empty()&&assets.contains(input)){load(input);return;}
 if(assets.contains("palette.raw")){load("palette.raw");return;}
 std::array<std::uint8_t,768> palette{};
 for(unsigned i=0;i<256;++i){palette[i*3]=std::uint8_t(i&0xe0);palette[i*3+1]=std::uint8_t((i&0xfc)<<3);palette[i*3+2]=std::uint8_t(i<<6);}
 set_font_palette(palette);
}
void Engine::set_font_palette(std::span<const std::uint8_t> source){
 if(source.size()!=fontPalette_.size())throw std::runtime_error("native active palette requires768 RGB bytes");
 std::array<std::uint8_t,768> palette;std::copy(source.begin(),source.end(),palette.begin());
 // Original004550e0/00425f20: normalized RGB triples, not index-as-alpha.
 palette[0]=palette[1]=palette[2]=0;
 for(unsigned i=1;i<256;++i){auto* p=palette.data()+i*3;if(p[0]<8&&p[1]<8&&p[2]<8)p[0]=p[1]=p[2]=8;}
 palette[765]=palette[766]=palette[767]=255;
 if(fontPaletteSerial_&&palette==fontPalette_)return;
 fontPalette_=palette;++fontPaletteSerial_;
 for(auto& [id,font]:bitmapFonts_)for(std::size_t i=0;i<font.indices.size();++i){
  const auto* p=fontPalette_.data()+std::size_t(font.indices[i])*3;auto* rgba=font.atlas.image.rgba.data()+i*4;
  rgba[0]=p[0];rgba[1]=p[1];rgba[2]=p[2];rgba[3]=(p[0]||p[1]||p[2])?255:0;
 }
}
void Engine::make_frame(bool mouseOutput){
 prepare_display();
 assets.set_native_alpha_depth(global("D3D_ALPHADEPTH").real());
 assets.set_native_shadow_depth(global("D3D_SHADOWDEPTH").real(),renderer!=nullptr);
 const bool bonusMouse=state.profile==SemanticProfile::BonusA7;
 // A7 1003e540: supported WMB6 has no embedded meshes; absent original XYZ selects the linked pool.
 if(bonusMouse&&world&&world->sections[3].size()/12==0&&(global("patch").bits&0x2000u)==0)throw std::runtime_error("A7 linked-pool world lighting is not implemented for a world without original XYZ vertices");
 if(bonusMouse&&mouseOutput&&global("mouse_mode").bits){auto pos=global_vector("mouse_pos");auto& object=state.require(pos.ref.owner);auto x=state.member(object.id,"x"),y=state.member(object.id,"y");x.set(Value::from_raw(std::clamp(x.get().raw(),0,(currentWidth_-1)*1024)));y.set(Value::from_raw(std::clamp(y.get().raw(),0,(currentHeight_-1)*1024)));auto spot=global_vector("mouse_spot");auto& hot=state.require(spot.ref.owner);mouseHit_={float(std::bit_cast<std::int32_t>(x.get().bits+spot.current().bits))/1024,float(std::bit_cast<std::int32_t>(y.get().bits+state.member(hot.id,"y").get().bits))/1024};}
 std::optional<Button> previousPressed,previousHovered;if(pressedButton_&&*pressedButton_<buttons_.size())previousPressed=buttons_[*pressedButton_];if(hoveredButton_&&*hoveredButton_<buttons_.size())previousHovered=buttons_[*hoveredButton_];
 draws_.clear();shadows_.clear();cameras_.clear();ui_.clear();particles_.clear();lights_.clear();buttons_.clear();uiObjects_.clear();uiTextUsed_=0;frame_.world=world;frame_.adjustment=nativeAdjustment;frame_.video=nullptr;frame_.videoSerial=0;frame_.videoRect={};frame_.fogStart=frame_.fogEnd=0;
 frame_.nativeFixedLightBank=!bonusMouse||(global("patch").bits&0x2000u)||(world&&!world->sections[3].empty());
 frame_.lightContext=this;frame_.prepareEntityLight=[](void* context,const DrawInstance& draw,const Camera& camera){return static_cast<Engine*>(context)->prepare_entity_light(draw,camera);};
 frame_.cachedEntityShadow=[](void* context,const DrawInstance& draw){const auto& entities=static_cast<Engine*>(context)->entities;auto found=entities.find(draw.objectId);return found==entities.end()?draw.nativeShadow:found->second.light.shadow;};
 materials_.clear();if(++materialGeneration_==0){materialIndices_.clear();materialGeneration_=1;}
 frame_.animationTimeSeconds=time_;frame_.turbulenceSpeed=float(number("TURB_SPEED"));frame_.turbulenceRange=float(number("TURB_RANGE"));
 frame_.skyCurve=float(number("SKY_CURVE"));frame_.skyClip=float(number("SKY_CLIP"));frame_.skyBlend=global("SKY_BLEND").bits!=0;frame_.skySpeed=skySpeed_;
 frame_.mipmapping=global("D3D_MIPMAPPING").real();frame_.anisotropy=global("D3D_ANISOTROPY").real();
 frame_.bonusProfile=bonusMouse;frame_.hardwareLighting=global("D3D_TRANSFORM").bits!=0;
 // render_ent is the installed native dispatcher, not an unregistered WDL numeric setting.
 frame_.renderEntities=true;
 // Authenticated Bonus initializer10017070/edition5 retains capability2000 (and hardware400).
 frame_.planarCapable=true;
 frame_.shadowSortVertical=global("D3D_ENTSORT").raw()<=1024;
 frame_.lightResRaw=global("D3D_LIGHTRES").raw();frame_.tripleBufferRaw=global("D3D_TRIPLEBUFFER").raw();frame_.shadowStencilRaw=global("SHADOW_STENCIL").raw();
 frame_.shadowOffset=float(number("SHADOW_OFFSET"));frame_.clipSize=float(number("CLIP_SIZE"));
 const auto shadowMaterial=material_index(global("mat_shadow"));if(shadowMaterial==~0u)throw std::runtime_error("native mat_shadow is missing");
 frame_.shadowAlpha=materials_[shadowMaterial].alpha;
 skinBindings_.clear();
 for(const auto& [source,bitmapId]:skinBitmapObjects){const auto* bitmap=state.get(bitmapId);if(!bitmap||!bitmap->alive)continue;SkinBinding binding{source,source};if(auto cached=bitmapImages.find(bitmapId);cached!=bitmapImages.end())binding.image=cached->second.get();binding.nativeTextureAlpha=binding.image->nativeTextureAlpha;for(const auto& [handle,target]:movieTargets)if(target==bitmapId)if(const Image* image=media.movie_frame(handle)){binding.image=image;binding.dynamic=true;binding.serial=(std::uint64_t(handle)<<32)|std::uint32_t(media.movie_status(handle));}if(binding.image!=source||binding.dynamic)skinBindings_.push_back(binding);}
 frame_.skinBindings=skinBindings_;
 auto bitmap=[&](Value v)->UIBitmap{if(!v)return {};Object* o=state.get(v.bits);if(!o||!o->alive||(bonusMouse&&(v.kind!=Value::Kind::Object||o->kind!=Object::Kind::Bmap)))throw std::runtime_error("invalid UI bitmap handle");if(ui_name(o->name,"_a4spr"))resolve_builtin_font();for(const auto& [handle,target]:movieTargets)if(target==o->id){if(const Image* image=media.movie_frame(handle))return {image,{0,0,float(image->width),float(image->height)},true,(std::uint64_t(handle)<<32)|std::uint32_t(media.movie_status(handle))};}
  auto cached=bitmapImages.find(o->id);const Image& image=cached==bitmapImages.end()?assets.image(o->text):*cached->second;Rect source{ui_number(*o,"source_x"),ui_number(*o,"source_y"),ui_number(*o,"width",float(image.width)),ui_number(*o,"height",float(image.height))};if(source.x<0||source.y<0||source.width<0||source.height<0||source.x+source.width>image.width||source.y+source.height>image.height)throw std::runtime_error("BMAP crop outside source image: "+o->name);return {&image,source};};
 auto font=[&](Value value)->const FontAtlas*{
  if(!value){value=global("_a4font");if(!value){if(!renderer)return nullptr;throw std::runtime_error("native engine standard FONT _a4font has not been resolved");}}
  Object* o=state.get(value.bits);if(!o||!o->alive)throw std::runtime_error("invalid FONT handle");if(ui_name(o->name,"_a4font"))resolve_builtin_font();std::string_view name=o->text;if(name.empty())throw std::runtime_error("FONT has no resource: "+o->name);
  bool image=o&&ui_number(*o,"style")>=4;
  if(!image){if(!renderer)return nullptr;auto cached=systemFonts_.find(o->id);if(cached!=systemFonts_.end())return cached->second;int size=int(ui_number(*o,"size"));int style=int(ui_number(*o,"style"));const FontAtlas* atlas=&renderer->font_atlas(name,size,style&1,style&2);systemFonts_.emplace(o->id,atlas);return atlas;}
  if(!o)throw std::runtime_error("bitmap FONT requires an object handle");auto cached=bitmapFonts_.find(o->id);if(cached!=bitmapFonts_.end())return &cached->second.atlas;
  BitmapFont decoded;auto& atlas=decoded.atlas;auto builtinImage=bitmapImages.find(o->id);
  if(builtinImage==bitmapImages.end()){auto source=decode_bitmap_font(assets.read(name),name,fontPalette_);atlas.image=std::move(source.image);decoded.indices=std::move(source.indices);}else atlas.image=*builtinImage->second;
  int cw=int(ui_number(*o,"style")),ch=int(ui_number(*o,"size"));if(cw<=0||ch<=0)throw std::runtime_error("bitmap FONT requires explicit native cell dimensions: "+o->name);
  if(atlas.image.width%cw||atlas.image.height%ch)throw std::runtime_error("invalid bitmap FONT cells: "+o->name);unsigned columns=atlas.image.width/cw,rows=atlas.image.height/ch,count=columns*rows;if(!count||count>256)throw std::runtime_error("bitmap FONT glyph count exceeds native range: "+o->name);
  for(unsigned byte=0;byte<256;++byte){auto& g=atlas.glyphs[byte];g.advance=float(cw);g.bearing={0,float(ch)};int index=count<64?int(byte)-48:int(byte);if(index>=0&&unsigned(index)<count)g.pixels={float(unsigned(index)%columns*cw),float(unsigned(index)/columns*ch),float(cw),float(ch)};}
  for(std::size_t i=0;i<atlas.image.rgba.size();i+=4)if(!atlas.image.rgba[i]&&!atlas.image.rgba[i+1]&&!atlas.image.rgba[i+2])atlas.image.rgba[i+3]=0;
  atlas.lineHeight=float(ch);atlas.ascent=float(ch);return &bitmapFonts_.emplace(o->id,std::move(decoded)).first->second.atlas;
 };
 auto rgb=[&](const Object& o,float opacity){return Color{ui_number(o,"red",255)/255,ui_number(o,"green",255)/255,ui_number(o,"blue",255)/255,std::clamp(opacity,0.0f,1.0f)};};
 auto global_color=[&](std::string_view name,Color fallback){if(!state.numericGlobals.contains(name))return fallback;Vec3 c=vector(global_vector(name));return Color{c.z/255,c.y/255,c.x/255,ui_name(name,"sky_color")&&c.x==0&&c.y==0&&c.z==0?0.0f:1.0f};};
 frame_.clearColor=global_color("sky_color",world?world->fogColor:Color{0,0,0,1});frame_.ambient=global_color("scene_color",world?world->ambient:Color{.25f,.25f,.25f,1});frame_.sunColor=global_color("sun_color",world?world->sunColor:Color{});
 frame_.screenColor=global_color("screen_color",{0,0,0,1});frame_.clearScreen=true;
 if(state.profile==SemanticProfile::BonusA7){auto screen=global_vector("screen_color");const auto raw=screen.current();const auto& source=state.require(screen.ref.owner);const auto blue=std::uint8_t(raw.raw()>>10),green=std::uint8_t(ui_value(source,"y").raw()>>10),red=std::uint8_t(ui_value(source,"z").raw()>>10);frame_.screenColor={red/255.0f,green/255.0f,blue/255.0f,1};frame_.clearScreen=blue||green||red;}
 const auto sunlight=vector(global_vector("SUN_LIGHT"));const float sun=float(double(std::max({sunlight.x,sunlight.y,sunlight.z}))*0.02);
 const auto sunAngle=vector(global_vector("SUN_ANGLE"));const float p=sunAngle.x*0.01745329238474369f,t=sunAngle.y*0.01745329238474369f;
 const float cp=float(std::cos(double(p))),sp=float(std::sin(double(p))),ct=float(std::cos(double(t))),st=float(std::sin(double(t)));
 frame_.sunDirection={(cp*ct)*sun,(sp*ct)*sun,st*sun};
 const float shadowScale=float(0.75/double(std::max(0.01f,float(std::tan(double(t))))));
 frame_.shadowSun={(cp*ct)*shadowScale,(sp*ct)*shadowScale};
 Value cameraValue=global("camera");ObjectId mainCamera=cameraValue.bits;
 auto camera_for=[&](ObjectId id){
  const Object& o=state.require(id);Camera c;c.position=position(id);c.angles=angles(id);c.viewport={ui_number(o,"pos_x"),ui_number(o,"pos_y"),ui_number(o,"size_x"),ui_number(o,"size_y")};c.horizontalFov=ui_number(o,"arc",60);c.pixelAspect=ui_number(o,"aspect",1);c.widescreen=false;c.nearClip=std::max(.01f,ui_number(o,"clip_near",1));c.farClip=std::max(c.nearClip+.01f,ui_number(o,"clip_far",10000));c.layer=ui_number(o,"layer");c.layers=1;
  c.pixelAspect=display_aspect(c.pixelAspect);
  c.alpha=field(id,"transparent")?std::clamp(ui_number(o,"alpha",100)/100,0.0f,1.0f):1;c.environmentOverride=true;c.ambient=frame_.ambient;c.nativeAmbient=float(std::clamp(ui_value(o,"ambient").raw()>>10,0,127));c.fogStart=ui_number(o,"fog_start",world?world->fogStart:0);c.fogEnd=ui_number(o,"fog_end",world?world->fogEnd:0);c.fogColor=world?world->fogColor:Color{};c.background=frame_.clearColor;
  const auto viewFlags=ui_value(o,"flags").bits;c.noShadow=(viewFlags&0x400000u)!=0;c.noEntities=(viewFlags&(bonusMouse?0x10000u:0x80000u))!=0;
  // Prepared view alpha is a packed integer for planar emission in both profiles.
  if(viewFlags&0x410u)c.opacityByte=std::uint8_t(std::int64_t(std::trunc((double(float(ui_value(o,"alpha").raw())/1024.0f))*2.55)));
  if(state.profile==SemanticProfile::BonusA7){
   c.perDrawOpacity=true;c.clearDepth=true;c.clearStencil=true;
   // Genuine Bonus WDL startup sets mode101f59f0|1 (entry-contract.txt); no invented mutable graphics-mode global.
   const auto sky=global_vector("sky_color");const auto& skyObject=state.require(sky.ref.owner);const bool skyZero=sky.current().raw()==0&&ui_value(skyObject,"y").raw()==0&&ui_value(skyObject,"z").raw()==0;
   c.clearColor=(viewFlags&0x10410u)==0&&!skyZero;
   if(viewFlags&0x410u){const auto rawAlpha=ui_value(o,"alpha").raw();const float publicAlpha=float(rawAlpha)/1024.0f;const long double byteAlpha=std::trunc(static_cast<long double>(publicAlpha)*static_cast<long double>(0x1.4666666666666p+1));c.opacityByte=std::uint8_t(std::int64_t(byteAlpha));c.alpha=float(rawAlpha)*9.765624781721272e-6f;}
   else {c.opacityByte=255;c.alpha=1;}
  }
  c.sourceId=id;c.portalId=ui_value(o,"portal").bits;c.portalPosition={ui_number(o,"portal_x"),ui_number(o,"portal_y"),ui_number(o,"portal_z")};c.portalNormal={ui_number(o,"pnormal_x"),ui_number(o,"pnormal_y"),ui_number(o,"pnormal_z")};c.portalClip=field(id,"portalclip");c.noCull=field(id,"nocull");c.excludeEntityId=ui_value(o,"genius").bits;
  return c;
 };
 for(ObjectId id:state.uiCandidates){auto* candidate=state.get(id);if(!candidate||!candidate->alive)continue;auto& o=*candidate;if(o.kind==Object::Kind::View){bool visible=field(o.id,"visible"),linked=false;if(!visible)for(ObjectId sourceId:state.uiCandidates){const auto* source=state.get(sourceId);if(source&&source->alive&&source->kind==Object::Kind::View&&field(source->id,"visible")&&ui_value(*source,"portal").bits==o.id){linked=true;break;}}if(!visible&&!linked)continue;if(ui_value(o,"bmap"))throw std::runtime_error("VIEW render-to-bitmap is not implemented: "+o.name);auto c=camera_for(o.id);c.portalTarget=!visible&&linked;cameras_.push_back(c);if(visible&&field(o.id,"audible"))media.listener(c.position,c.angles.x);if(visible&&(o.id==mainCamera||cameras_.size()==1)){frame_.fogStart=c.fogStart;frame_.fogEnd=c.fogEnd;frame_.fogColor=c.fogColor;}}else if((o.kind==Object::Kind::Panel||o.kind==Object::Kind::Text)&&field(o.id,"visible"))uiObjects_.push_back(o.id);}
 for(auto& c:cameras_)c.doorPortal=c.sourceId==mainCamera&&!c.portalTarget&&!c.foreground;
 // Seamless doors hide LevelYukle's LYP_Fade panel; fadeVisible_ counts presented fade frames (scenario $fadeframes).
 fadeVisible_=false;if(auto fade=global("LYP_Fade");fade.kind==Value::Kind::Object&&std::find(uiObjects_.begin(),uiObjects_.end(),fade.bits)!=uiObjects_.end()){if(fadeHidden_)std::erase(uiObjects_,fade.bits);else fadeVisible_=field(fade.bits,"alpha")>0;}
 ui_order(cameras_,[](const Camera& a,const Camera& b){return a.layer<b.layer;});
 visibleLodRanges_.resize(cameras_.size());const auto lodFactors=vector(global_vector("D3D_LODFACTOR"));const std::array lodValues{lodFactors.x,lodFactors.y,lodFactors.z};
 for(std::size_t view=0;view<cameras_.size();++view){const auto far=cameras_[view].farClip;const float base=bonusMouse?float(static_cast<long double>(far)*far*1.25):float(static_cast<long double>(far)*far);auto& ranges=visibleLodRanges_[view];ranges[0]=bonusMouse?float(static_cast<long double>(base)*1.25):base;for(std::size_t i=0;i<3;++i){const float squared=float(static_cast<long double>(lodValues[i])*lodValues[i]);const float scaled=float(static_cast<long double>(squared)*std::bit_cast<float>(0x38d1b717u));ranges[i+1]=float(static_cast<long double>(scaled)*base);ranges[i+4]=float(static_cast<long double>(ranges[i+1])*1.15);}}
 // Native GL deliberately uses its 32-light shader/bank budget, not an invented D3D caps query.
 // WDL setters are observed at this native frame boundary, not reordered by unordered entity storage.
 const auto lightCount=std::size_t(std::clamp(global("MAX_LIGHTS").raw()>>10,0,32));
 for(auto& slot:entityLightSlots_)if(slot.object){const auto* object=state.get(slot.object);if(!object||!object->alive||ui_value(*object,"lightrange").raw()<1){if(auto data=entities.find(slot.object);data!=entities.end())data->second.lightResident=false;slot={};}}
 auto light_slot=[&](ObjectId owner,bool synthetic,bool resident,float radius)->EntityLightSlot*{
  if(!lightCount)return nullptr;
  for(std::size_t i=0;i<lightCount;++i){auto& slot=entityLightSlots_[i];if(slot.occupied()&&slot.object==owner&&slot.synthetic==synthetic)return &slot;}
  std::size_t selected=lightCount;for(std::size_t i=0;i<lightCount;++i)if(!entityLightSlots_[i].occupied()){selected=i;break;}
  if(selected==lightCount){
   if(!synthetic&&(bonusMouse?radius>0:resident))return nullptr;
   selected=7;float oldest=float(time_);
   for(std::size_t i=0;i<lightCount;++i){const auto& slot=entityLightSlots_[i];if(slot.light.type!=3&&slot.born<=oldest){selected=i;oldest=slot.born;}}
  }
  auto& slot=entityLightSlots_[selected];slot={};slot.object=owner;slot.synthetic=synthetic;slot.born=float(time_);slot.light.type=0;return &slot;
 };
 const auto pointFalloff=vector(global_vector("D3D_POINTLIGHTFALLOFF")),spotFalloff=vector(global_vector("D3D_SPOTLIGHTFALLOFF")),spotCone=vector(global_vector("D3D_SPOTLIGHTCONE"));
 // handles is State's retained birth ledger; reclaimed deque payload positions are not birth order.
 // Live entities in birth (handle) order, without scanning the whole handle ledger.
 WorldQuerySnapshot order(*this,true);order.ids->clear();for(const auto& [id,data]:entities){const auto* o=state.get(id);if(o&&o->alive)order.ids->push_back(id);}std::sort(order.ids->begin(),order.ids->end());
 for(const ObjectId id:*order.ids){const auto& object=state.require(id);auto& data=entities.at(id);const auto raw=ui_value(object,"lightrange").raw();if(raw<1){data.lightResident=false;continue;}const float radius=float(raw)/1024.f;auto* slot=light_slot(object.id,false,data.lightResident,radius);data.lightResident=true;if(!slot)continue;auto& light=slot->light;
  if(bonusMouse?radius>0.1f:frame_.hardwareLighting)light.type=(ui_value(object,"flags3").bits&2u)?2:1;
  light.position=position(object.id);light.range=radius+200;light.intensity=1;light.baked=false;light.synthetic=false;
  // The current native pose is shared with ordinary packets; do not invent a second parent/interpolation pose.
  if(light.type==2||light.type==3){const auto orientation=angles(object.id);const float pan=orientation.x*0.01745329238474369f,tilt=orientation.y*0.01745329238474369f;
   const float cosine=float(std::cos(double(tilt)));light.direction={float(std::cos(double(pan)))*cosine,float(std::sin(double(pan)))*cosine,float(std::sin(double(tilt)))};}
  const auto falloff=light.type==2?spotFalloff:pointFalloff;light.attenuation={falloff.z,falloff.y/radius,falloff.x/(radius*radius)};light.theta=spotCone.x;light.phi=spotCone.x+spotCone.y;light.falloff=1+spotCone.z;
  int red=ui_value(object,"red").raw()>>10,green=ui_value(object,"green").raw()>>10,blue=ui_value(object,"blue").raw()>>10;if(global("D3D_MONOCHROME").bits)red=green=blue=(red+green+blue)/3;light.color={red/255.f,green/255.f,blue/255.f,1};
 }
 const bool sunEnabled=frame_.hardwareLighting&&(bonusMouse?global("SUN_LIGHT").raw()>0:global("SUN_LIGHT").bits!=0);
 if(!sunEnabled){for(auto& slot:entityLightSlots_)if(slot.synthetic)slot={};}
 else if(auto* slot=light_slot(0,true,false,1)){
  // A6 00437800: D3DLIGHT9.Direction = zero vector 006c8e64 - unit SUN_ANGLE vector (light travels away from the sun);
  // 00438470 stores it into the directional slot. Bonus keeps the unverified A7 sign.
  auto& light=slot->light;light.type=3;light.synthetic=true;light.direction=bonusMouse?Vec3{cp*ct,sp*ct,st}:Vec3{-(cp*ct),-(sp*ct),-st};
  const float sunlight=float(global("SUN_LIGHT").raw())/1024.f;
  if(bonusMouse){const auto bgr=vector(global_vector("sun_color"));const float scale=float(double(sunlight)/25500.0);light.color={float(static_cast<long double>(bgr.z)*scale),float(static_cast<long double>(bgr.y)*scale),float(static_cast<long double>(bgr.x)*scale),1};}
  else{const float level=float(static_cast<long double>(sunlight)*std::bit_cast<float>(0x3ca3d70au));light.color={level,level,level,1};}
  light.range=1;light.intensity=1;
 }
 for(std::size_t i=0;i<lightCount;++i)if(entityLightSlots_[i].occupied())lights_.push_back(entityLightSlots_[i].light);
 // Serialized WMB lights remain the existing enhanced baked-light route, not fabricated native active slots.
 if(world)for(const auto& source:world->lights){RenderLight light{source.position,source.color,source.range,1,true};light.synthetic=true;lights_.push_back(light);}
 // Assets own immutable geometry; instances carry only live pose/mesh overrides.
 for(auto& [id,data]:entities){
  const Object* o=state.get(id);if(!o||!o->alive)continue;
  // Native position/flag producers run before visibility; offscreen moves still leave a pending query.
  observe_entity_light(id,data);
  bool viewEntity=o->fields.contains("view");const bool surfaceVisible=viewEntity?field(id,"visible")!=0:!field(id,"invisible");
  if(!surfaceVisible&&(viewEntity||(flags(id)&0x40000u)==0))continue;
  if(!data.model&&!data.file.empty()){data.model=data.type==4?&assets.world(data.file).mesh:&assets.model(data.file);data.light.model=data.model;data.light.dirty=true;if(state.profile==SemanticProfile::BonusA7&&data.type==5)prepare_query_geometry(id,data);}DrawInstance d;d.model=data.model;if(!d.model)continue;d.position=position(id);d.angles=angles(id);d.scale={ui_number(*o,"scale_x",1),ui_number(*o,"scale_y",1),ui_number(*o,"scale_z",1)};
  if(nativeDisplay_&&viewEntity&&core_equal(o->name,"SilahPanel")){d.position.y=-70;const bool crate=ui_number(*o,"z")==-32;d.position.z=crate?(currentWindowMode==WindowMode::Fullscreen?-40:-30):(currentWindowMode==WindowMode::Fullscreen?-52:-37.5f);}
  d.spriteFrames=unsigned(std::max(1.0f,ui_number(*o,"sprite_frames",1)));std::size_t frameCount=data.type==1?d.spriteFrames:std::max(std::size_t(1),d.model->frames.size());float frame=std::max(1.0f,ui_number(*o,"frame",1));d.frame=std::min(std::uint32_t(frame-1),std::uint32_t(frameCount-1));float next=ui_number(*o,"next_frame");d.nextFrame=next>0?std::min(std::uint32_t(std::max(0.0f,next-1)),std::uint32_t(frameCount-1)):std::min(d.frame+1,std::uint32_t(frameCount-1));d.interpolation=frame-std::floor(frame);
  if(auto modelMesh=meshOverrides.find(d.model);modelMesh!=meshOverrides.end()){if(auto mesh=modelMesh->second.find(d.frame);mesh!=modelMesh->second.end()){d.positions=mesh->second.mesh;d.normals=mesh->second.meshNormals;d.meshId=mesh->second.meshId;d.meshRevision=mesh->second.revision;}if(auto mesh=modelMesh->second.find(d.nextFrame);mesh!=modelMesh->second.end()){d.nextPositions=mesh->second.mesh;d.nextNormals=mesh->second.meshNormals;d.nextMeshId=mesh->second.meshId;d.nextMeshRevision=mesh->second.revision;}}if(model_has_entity_skins(*d.model)){auto skin=state.member(id,"skin");d.skinIndex=model_skin_index(*d.model,skin.get().raw()>>10);skin.set(Value::from_raw(std::int32_t(std::clamp<std::int64_t>(skin.get().raw(),1024,std::int64_t(d.model->materials.size())*1024))));}d.objectId=id;d.reflectivity=float(std::uint8_t(ui_value(*o,"albedo").raw()>>10))/50;
  d.translucent=field(id,"transparent");
  if(d.translucent&&state.profile==SemanticProfile::BonusA7){const auto alpha=o->fields.find("alpha");d.alpha=float(alpha==o->fields.end()?102400:alpha->second.value.raw())*std::bit_cast<float>(0x3723d70au);}
  else d.alpha=d.translucent?std::clamp(ui_number(*o,"alpha",100)/100,0.0f,1.0f):1;
  d.ambient=ui_number(*o,"ambient");d.tint=rgb(*o,1);d.unlit=field(id,"unlit");d.additive=field(id,"bright");d.overlay=field(id,"overlay");d.flare=field(id,"flare");d.invertFlare=d.flare&&d.translucent;d.billboard=data.type==1&&!field(id,"oriented");d.viewSpace=viewEntity;
  // A6 0043ee40 (models) and 00443350 (world sprites) blend only for FLARE, TRANSPARENT or view opacity < 255; BRIGHT alone draws opaque.
  if(state.profile==SemanticProfile::CampaignA6&&!d.translucent&&!d.flare)d.additive=false;
  // A sprite whose BMAP has the 32-bit TGA alpha flag 0x100 (0040e2b0, a6AlphaFormat) is alpha-blended without TRANSPARENT: A6 00440e90 does so for
  // VIEW sprites; world sprites match the original observed under Wine (Start4 telbasla.tga fence). The 00443350 state for such world sprites is unresolved.
  if(state.profile==SemanticProfile::CampaignA6&&data.type==1&&!d.translucent&&!d.model->materials.empty()&&d.model->materials[0].texture&&d.model->materials[0].texture->a6AlphaFormat)d.translucent=true;
  d.nativeLightScalar=data.light.scalar;d.nativeLightDelta=data.light.delta;d.bright=(flags(id)&0x200000u)!=0;
  d.nativeShadow=data.light.shadow;
  d.shadowRequested=(flags(id)&0x40000u)!=0;
  d.visible=surfaceVisible;
  d.nativeFloorLight=state.profile!=SemanticProfile::CampaignA6||data.physicsGeometry;
  if(!d.nativeFloorLight)d.nativeSpecialColor={ui_value(*o,"blue").raw()>>10,ui_value(*o,"green").raw()>>10,ui_value(*o,"red").raw()>>10};
  if(state.profile==SemanticProfile::BonusA7&&(flags(id)&0x80000u)&&ui_value(*o,"lightrange").raw()<1)d.nativeLightBias={ui_value(*o,"blue").raw()>>10,ui_value(*o,"green").raw()>>10,ui_value(*o,"red").raw()>>10};
  // A6 00430920/0044f970 -> 0042fa20: an entity without MATERIAL gets a persistent built-in one (METAL, UNLIT, then resource type).
  if(state.profile==SemanticProfile::CampaignA6&&!ui_value(*o,"material")){const auto f=flags(id);const char* name=(f&0x400000u)?"mat_metal":(f&0x20000u)?"mat_unlit":data.type==5?"mat_terrain":data.type==1?"mat_sprite":data.type==2?"mat_model":nullptr;if(name)state.member(id,"material").set(global(name));}
  if(viewEntity){ObjectId reference=ui_value(*o,"view").bits;if(!reference)reference=mainCamera;if(!reference||!field(reference,"visible"))continue;float layer=ui_number(*o,"layer");std::size_t group=cameras_.size();for(std::size_t i=0;i<cameras_.size();++i)if(!cameras_[i].drawWorld&&cameras_[i].layer==layer&&cameras_[i].viewport.x==ui_number(state.require(reference),"pos_x")&&cameras_[i].viewport.y==ui_number(state.require(reference),"pos_y")){group=i;break;}if(group==cameras_.size()){if(cameras_.size()>=31)throw std::runtime_error("too many independently layered VIEW entities");Camera c=camera_for(reference);c.position={};c.angles={};c.drawWorld=false;c.foreground=true;c.layer=layer;c.layers=1u<<cameras_.size();cameras_.push_back(c);}d.layers=cameras_[group].layers;}d.materialIndex=material_index(ui_value(*o,"material"));draws_.push_back(d);
  // Visible LOD is one source actor cache shared by successive views, not a default shadow slot.
  if(!viewEntity&&(data.type==2||data.type==5)&&(bonusMouse||(ui_value(*o,"flags3").bits&0x20000u)==0)){
   for(std::size_t view=0;view<cameras_.size();++view){const auto& camera=cameras_[view];if(!camera.drawWorld)continue;const auto& ranges=visibleLodRanges_[view];
    auto delta=wSub(camera.position,d.position);if(global("D3D_ENTSORT").raw()>1024)delta.z=0;
    float distance=wDot(delta,delta);
    if(data.type==5){const auto upper=data.model->max;const Vec3 scaled{upper.x*d.scale.x,upper.y*d.scale.y,upper.z*d.scale.z};distance=float(static_cast<long double>(distance)-wDot(scaled,scaled));}
    if(distance>ranges[0])continue;
    unsigned lod=distance>ranges[3]?3:distance>ranges[2]?2:distance>ranges[1]?1:0;
    if(lod&&data.visibleLod==lod-1&&distance<=ranges[lod+3])--lod;
    data.visibleLod=std::uint8_t(lod);
    if(!d.shadowRequested)continue;
    // The A6 ordinary selected pointer must survive before its independent shadow selector.
    if(!bonusMouse&&lod&&!data.model->shadowLods[lod-1])continue;
    const std::int64_t index=bonusMouse?std::int64_t(lod)+(global("SHADOW_LOD").raw()>>10):lod;
    const Model* selected=nullptr;
    if(bonusMouse){if(index<=0)selected=data.model;else if(index<4)selected=data.model->shadowLods[std::size_t(index-1)];}
    else if(index<3)selected=data.model->shadowLods[std::size_t(index)];
    if(!selected||(selected->formatVersion!=4&&selected->formatVersion!=5&&selected->formatVersion!=7))continue;
    DrawInstance shadow=d;shadow.model=selected;shadow.shadowViewId=camera.sourceId;shadow.visible=true;
    shadow.positions={};shadow.normals={};shadow.nextPositions={};shadow.nextNormals={};shadow.meshId=shadow.nextMeshId=0;shadow.meshRevision=shadow.nextMeshRevision=0;
    const auto count=std::uint32_t(std::max(std::size_t(1),selected->frames.size()));shadow.frame=std::min(std::uint32_t(frame-1),count-1);
    // A6 00447918/0044a869 and A7 10049da0 wrap next against the selected resource.
    const auto requestedNext=next>0?std::uint32_t(std::max(0.f,next-1)):shadow.frame+1;shadow.nextFrame=requestedNext<count?requestedNext:0;
    if(auto meshes=meshOverrides.find(selected);meshes!=meshOverrides.end()){if(auto mesh=meshes->second.find(shadow.frame);mesh!=meshes->second.end()){shadow.positions=mesh->second.mesh;shadow.normals=mesh->second.meshNormals;shadow.meshId=mesh->second.meshId;shadow.meshRevision=mesh->second.revision;}if(auto mesh=meshes->second.find(shadow.nextFrame);mesh!=meshes->second.end()){shadow.nextPositions=mesh->second.mesh;shadow.nextNormals=mesh->second.meshNormals;shadow.nextMeshId=mesh->second.meshId;shadow.nextMeshRevision=mesh->second.revision;}}
    shadows_.push_back(shadow);
   }
  }
 }
 // Campaign emit() particles: screen-pixel SIZE (legacy_particle_quants) at the selected original video mode's width (native display: 1024 tier).
 // ponytail: one conversion from the camera view's ARC for every view; per-view ARC only if a non-camera view shows emit particles.
 double legacyScale=1;if(state.profile==SemanticProfile::CampaignA6){const int mode=global("video_mode").raw()>>10;const auto* view=state.get(global("camera").bits);legacyScale=legacy_particle_quants(view?ui_number(*view,"arc",60):60,mode>=6&&mode<=10?a6VideoModes[std::size_t(mode-6)][0]:std::max(currentWidth_,1));}
 // One hash lookup per particle field (29k fire particles on a burning duma): no Engine::field mask/alias detour for plain
 // fields, FLAGS read once for the TRANSPARENT/BRIGHT/OVERLAY/FLARE/BEAM/STREAK masks, and each BMAP resolved once per frame.
 const auto particleMask=[&](std::string_view name){const auto* m=core_mask(Object::Kind::Particle,name,state.profile);if(!m||!core_equal(m->backing,"flags"))throw std::runtime_error("particle flag mask is not FLAGS-backed");return m->mask;};
 const std::uint32_t brightMask=particleMask("bright"),flareMask=particleMask("flare"),beamMask=particleMask("beam"),transparentMask=particleMask("transparent"),overlayMask=particleMask("overlay"),streakMask=particleMask("streak");
 std::unordered_map<std::uint64_t,UIBitmap> particleBitmaps;
 for(const auto& active:activeParticles){const Object* o=state.get(active.object);if(!o||!o->alive)continue;Particle p;
  auto slot=[&](std::string_view key)->const Value*{auto i=o->fields.find(key);return i==o->fields.end()?nullptr:&i->second.value;};
  auto number=[&](std::string_view key,float fallback=0){const Value* v=slot(key);return v?float(v->real()):fallback;};
  const Value* flagValue=slot("flags");const std::uint32_t flagBits=flagValue?flagValue->bits:0;
  p.position={number("x"),number("y"),number("z")};
  const Value* bmapSlot=slot("bmap");const Value bmap=bmapSlot?*bmapSlot:Value{};
  if(bmap){auto [cached,inserted]=particleBitmaps.try_emplace(std::uint64_t(bmap.kind)<<32|bmap.bits);if(inserted)cached->second=bitmap(bmap);p.image=cached->second.image;}
  const Value* alpha=slot("alpha");const float opacity=(alpha?float(alpha->real()):100)/100;p.color={number("red",255)/255,number("green",255)/255,number("blue",255)/255,1};p.color.a=state.profile==SemanticProfile::BonusA7?opacity:std::clamp(opacity,0.0f,1.0f);p.alphaRaw=alpha?alpha->raw():102400;
  p.translucent=(flagBits&transparentMask)!=0;p.size=float(number("size",1)*(active.legacy?legacyScale:1));p.angle=number("angle");p.additive=(flagBits&brightMask)!=0;p.velocity={number("vel_x"),number("vel_y"),number("vel_z")};p.scaleZ=number("scale_z",1);p.overlay=(flagBits&overlayMask)!=0;p.flare=(flagBits&flareMask)!=0;p.invertFlare=p.flare&&p.translucent;p.beam=(flagBits&beamMask)!=0;p.streak=(flagBits&streakMask)!=0;
  const Value* material=slot("material");p.materialIndex=material_index(material?*material:Value{});particles_.push_back(p);}
 ui_order(uiObjects_,[&](ObjectId a,ObjectId b){return ui_number(state.require(a),"layer")<ui_number(state.require(b),"layer");});
 auto text_command=[&](const Object& o,std::string_view text,Value fontValue,float x,float y,int* budget=nullptr,int scroll=0){
  if(auto zero=text.find('\0');zero!=std::string_view::npos)text=text.substr(0,zero);
  if(!fontValue)fontValue=global("_a4font");
  const FontAtlas* atlas=font(fontValue);if(!atlas||text.empty())return;float sx=ui_number(o,"scale_x",1),sy=ui_number(o,"scale_y",1);Color color=rgb(o,field(o.id,"transparent")?ui_number(o,"alpha",100)/100:1);float layer=ui_number(o,"layer");int lines=1+int(std::count(text.begin(),text.end(),'\n'));
  if(auto bitmapFont=bitmapFonts_.find(fontValue.bits);bitmapFont!=bitmapFonts_.end()){
   int height=int(atlas->lineHeight),advance=int(atlas->glyphs[0].advance)-(field(o.id,"condensed")?1:0)-(field(o.id,"narrow")?2:0);
   std::size_t start=0;for(std::size_t p=0;scroll>0&&p<text.size();++p)if(text[p]=='\n'){start=p+1;scroll-=height;}
   if(budget){*budget+=scroll-height;if(scroll>0||*budget<1)return;}
   text.remove_prefix(start);int dy=field(o.id,"center_y")?(int(std::count(text.begin(),text.end(),'\n'))-1)*(height/2):0;dy-=scroll;
   auto centered=[&](std::size_t p){auto end=text.find('\n',p);if(end==std::string_view::npos)end=text.size();return field(o.id,"center_x")?-int(end-p)*(advance/2):0;};int dx=centered(0);
   Vec3 screen=vector(global_vector("screen_size"));float screenWidth=screen.x,screenHeight=screen.y;
   for(std::size_t p=0;p<text.size();++p){unsigned char byte=text[p];if(byte=='\n'){if(budget){*budget-=height;if(*budget<1)return;}dy+=height;dx=centered(p+1);continue;}const auto& glyph=atlas->glyphs[byte];const float gx=x+std::trunc(dx*sx),gy=y+std::trunc(dy*sy);const bool fits=nativeDisplay_?gx>=0&&gy>=0&&gx+glyph.pixels.width*sx<screenWidth&&gy+glyph.pixels.height*sy<screenHeight:x+dx>=0&&y+dy>=0&&x+dx+advance<screenWidth&&y+dy+height<screenHeight;if(glyph.pixels.width&&fits){UICommand c;c.image=&atlas->image;c.dynamicImage=!bitmapFont->second.indices.empty();c.imageSerial=fontPaletteSerial_;c.rect={gx,gy,glyph.pixels.width*sx,glyph.pixels.height*sy};c.uv={glyph.pixels.x/atlas->image.width,glyph.pixels.y/atlas->image.height,glyph.pixels.width/atlas->image.width,glyph.pixels.height/atlas->image.height};c.color=color;c.layer=layer;c.overlay=true;c.filter=field(o.id,"filter");ui_.push_back(c);}dx+=advance;}
   return;
  }
  // Native TTF path bypasses bitmap NARROW/CONDENSED, CENTER_Y and scroll/clipping.
  for(std::size_t start=0;start<=text.size();){auto end=text.find('\n',start);if(end==std::string_view::npos)end=text.size();auto line=text.substr(start,end-start);float width=Renderer::text_extent(*atlas,line).x*sx;
   UICommand command;command.kind=UICommand::Kind::Text;command.rect={x-(field(o.id,"center_x")?width/2:0),y,width,atlas->lineHeight*sy};command.font=atlas;command.text=line;command.scaleX=sx;command.scaleY=sy;command.color=color;command.filter=field(o.id,"filter");command.layer=layer;
   auto shadow=command;shadow.color={0,0,0,color.a};if(field(o.id,"outline"))for(Vec2 offset:std::array<Vec2,3>{{{-1,-1},{1,-1},{-1,1}}}){shadow.rect=command.rect;shadow.rect.x+=offset.x;shadow.rect.y+=offset.y;ui_.push_back(shadow);}if(field(o.id,"shadow")||field(o.id,"outline")){shadow.rect=command.rect;shadow.rect.x+=1;shadow.rect.y+=1;ui_.push_back(shadow);}ui_.push_back(command);
   if(end==text.size())break;start=end+1;y+=atlas->lineHeight*sy;
  }
  if(budget){const Object* nativeFont=state.get(fontValue.bits);*budget-=int(nativeFont?ui_number(*nativeFont,"size",atlas->lineHeight):atlas->lineHeight)*lines;}
 };
 for(ObjectId id:uiObjects_){Object& o=state.require(id);float x=ui_number(o,"pos_x"),y=ui_number(o,"pos_y"),sx=ui_number(o,"scale_x",1),sy=ui_number(o,"scale_y",1),opacity=field(id,"transparent")?ui_number(o,"alpha",100)/100:1;
  if(nativeDisplay_&&core_equal(o.name,"CER_Cerceve")&&number("DM_Acik")!=0&&!field(global("CRMT_Credit").bits,"visible")&&!field(global("YUMP_Cursor").bits,"visible"))y+=16;
  if(nativeDisplay_&&core_equal(o.name,"GMP_CozunurlukA")){text_command(o,displayLabel_.data(),global("_a4font"),x+5*sx,y+9*sy);continue;}
  if(o.kind==Object::Kind::Text){
   Value fontValue=ui_value(o,"font");if(!fontValue)fontValue=global("_a4font");const FontAtlas* atlas=font(fontValue);if(!atlas)continue;const Object* nativeFont=state.get(fontValue.bits);
   float offsetValue=ui_number(o,"offset_y");
   if(nativeDisplay_&&std::find(displayScrolls_.begin(),displayScrolls_.end(),id)!=displayScrolls_.end())offsetValue/=float(state.require(displayFont_).array[DisplayTier].value.real());
   if(nativeDisplay_&&id==global("YUMT_Liste").bits){offsetValue+=displayLoadScroll_;}
   int height=int(nativeFont?ui_number(*nativeFont,"size",atlas->lineHeight):atlas->lineHeight),initial=std::max(0,int(ui_number(o,"size_y",1000))),offset=std::max(0,int(offsetValue)),covered=0,scroll=-1;
   if(nativeDisplay_&&id==global("YUMT_Liste").bits&&displayLoadBudget_)initial=std::min(initial,displayLoadBudget_);
   int remaining=initial;
   for(const auto& slot:o.array){if(!slot.value)continue;const Object* source=state.get(slot.value.bits);if(!source||!source->alive)continue;if(ui_value(*source,"flags").bits&2u)continue;std::string_view text=string(slot.value);if(auto zero=text.find('\0');zero!=std::string_view::npos)text=text.substr(0,zero);int end=covered+height*(1+int(std::count(text.begin(),text.end(),'\n')));if(scroll<0){if(offset>end){covered=end;continue;}scroll=offset-covered;}covered=end;float baseY=std::trunc(y+(initial-remaining)*sy);text_command(o,text,fontValue,std::trunc(x),baseY,&remaining,scroll);scroll=0;if(remaining<1)break;}
   continue;
  }
  // A6 004447e0: BMAP0x100 (32-bit TGA) panel bitmaps always blend with alpha byte min(2*a,255), a = 0040ce80(ALPHA), TRANSPARENT or not.
  auto a6_alpha_format_opacity=[&]{const std::int64_t raw=std::max<std::int64_t>(o.fields.contains("alpha")?ui_value(o,"alpha").raw():102400,0),p=raw*0xa33;const auto a=std::min<std::int64_t>(((p>>10)+((p>>9)&1))>>10,255);return float(std::min<std::int64_t>(2*a,255))/255.f;};
  auto image_command=[&](UIBitmap image,float px,float py,float width,float height){if(!image.image||width<=0||height<=0)return;UICommand c;c.rect={x+px*sx,y+py*sy,width*sx,height*sy};c.image=image.image;c.dynamicImage=image.dynamic;c.imageSerial=image.serial;c.uv={image.pixels.x/image.image->width,image.pixels.y/image.image->height,image.pixels.width/image.image->width,image.pixels.height/image.image->height};c.color=rgb(o,image.image->a6AlphaFormat?a6_alpha_format_opacity():opacity);c.overlay=field(id,"overlay");c.filter=field(id,"filter");c.angle=ui_number(o,"angle");c.layer=ui_number(o,"layer");ui_.push_back(c);};
  auto background=bitmap(ui_value(o,"bmap"));if(background.image){if(nativeDisplay_&&displayFills_.contains(id)){x=0;y=0;sx=float(currentWidth_)/background.pixels.width;sy=float(currentHeight_)/background.pixels.height;}image_command(background,0,0,background.pixels.width,background.pixels.height);}
  FunctionId panelClick=callback(id,"on_click");if(panelClick&&background.image)buttons_.push_back({{x,y,background.pixels.width*sx,background.pixels.height*sy},panelClick,0,0,id,0,ui_number(o,"angle")});int buttonNumber=0;
  for(const auto& property:o.properties){const auto& a=property.arguments;auto require=[&](std::size_t n){if(a.size()!=n)throw std::runtime_error("invalid PANEL "+property.name+" argument count: "+o.name);};auto n=[&](std::size_t i){return float(a.at(i).current().real());};
   if(ui_name(property.name,"digits")){require(6);Value f=a[2].current(),value=a[5].current();if(uiTextUsed_==uiText_.size())uiText_.emplace_back();auto& rendered=uiText_[uiTextUsed_++];ui_digits(rendered,f,n(4)*value.real(),value.kind==Value::Kind::Object?string(value):std::string_view{},f.kind==Value::Kind::Object?string(f):std::string_view{});text_command(o,rendered,a[3].current(),x+n(0)*sx,y+n(1)*sy);}
   else if(ui_name(property.name,"window")){require(7);auto image=bitmap(a[4].current());if(!image.image)throw std::runtime_error("PANEL WINDOW has no bitmap: "+o.name);Rect crop{image.pixels.x+n(5),image.pixels.y+n(6),n(2),n(3)};if(crop.x<0||crop.y<0||crop.x+crop.width>image.image->width||crop.y+crop.height>image.image->height)throw std::runtime_error("PANEL WINDOW crop outside bitmap: "+o.name);image.pixels=crop;image_command(image,n(0),n(1),n(2),n(3));}
   else if(ui_name(property.name,"button")){require(8);++buttonNumber;auto on=bitmap(a[2].current()),off=bitmap(a[3].current()),over=bitmap(a[4].current());if(!on.image)throw std::runtime_error("PANEL BUTTON requires on bitmap: "+o.name);Rect r{x+n(0)*sx,y+n(1)*sy,on.pixels.width*sx,on.pixels.height*sy};bool hover=(!bonusMouse||(global("mouse_mode").bits&&global("mouse_valid").bits))&&ui_contains(r,ui_number(o,"angle"),bonusMouse?mouseHit_.x:input.mouseX,bonusMouse?mouseHit_.y:input.mouseY,bonusMouse,{x,y});auto selected=hover&&input.mouseButtons[SDL_BUTTON_LEFT]?on:hover&&over.image?over:off.image?off:on;image_command(selected,n(0),n(1),on.pixels.width,on.pixels.height);buttons_.push_back({r,a[5].current().bits,a[6].current().bits,a[7].current().bits,id,buttonNumber,ui_number(o,"angle")});}
   else {bool scalar=false;for(auto key:{"flags","layer","bmap","pos_x","pos_y","scale_x","scale_y","red","green","blue","alpha","visible","transparent","translucent","refresh","d3d","overlay","on_click","angle","filter"})if(ui_name(property.name,key)){scalar=true;break;}if(!scalar)throw std::runtime_error("unsupported PANEL property "+property.name+": "+o.name);}
  }
 }
 if(movie&&media.movie_playing(movie)){auto target=movieTargets.find(movie);if(target==movieTargets.end()||!target->second){if(const Image* image=media.movie_frame(movie)){UICommand video;video.image=image;video.dynamicImage=true;video.imageSerial=(std::uint64_t(movie)<<32)|std::uint32_t(media.movie_status(movie));video.rect={0,0,float(currentWidth_?currentWidth_:1280),float(currentHeight_?currentHeight_:720)};video.layer=float(number("media_layer"));video.filter=true;ui_.push_back(video);}}}
 ui_order(cameras_,[](const Camera& a,const Camera& b){return a.layer<b.layer;});ui_order(ui_,[](const UICommand& a,const UICommand& b){return a.layer<b.layer;});
 if(bonusMouse&&mouseOutput){const bool active=global("mouse_mode").bits!=0;const auto selected=active?global("mouse_map"):Value::object(0);auto cursor=bitmap(selected);if(active&&cursor.image&&!global("mouse_sync").bits){const auto pos=global_vector("mouse_pos");const auto& object=state.require(pos.ref.owner);UICommand command;command.image=cursor.image;command.rect={float(pos.current().raw()>>10),float(ui_value(object,"y").raw()>>10),cursor.pixels.width,cursor.pixels.height};command.uv={cursor.pixels.x/cursor.image->width,cursor.pixels.y/cursor.image->height,cursor.pixels.width/cursor.image->width,cursor.pixels.height/cursor.image->height};command.overlay=true;command.cursorOverlay=true;command.dynamicImage=cursor.dynamic;command.imageSerial=cursor.serial;command.textureMode=cursor.image->nativeTextureAlpha?UICommand::TextureMode::Default:global("D3D_TEXDEPTH").raw()<=16384?UICommand::TextureMode::CursorKey16:UICommand::TextureMode::CursorKey24;ui_.push_back(command);}if(renderer)renderer->set_mouse_mode(currentWindowMode!=WindowMode::Fullscreen&&global("mouse_pointer").bits&&!cursor.image,number("mouse_mode")==0&&number("enable_mouse")!=0);}
 frame_.instances=draws_;frame_.shadows=shadows_;frame_.cameras=cameras_;frame_.ui=ui_;frame_.particles=particles_;frame_.lights=lights_;
 door_frame();
 frame_.materials=materials_;
 auto restore=[&](std::optional<std::size_t>& index,const std::optional<Button>& old){index.reset();if(old)for(std::size_t i=0;i<buttons_.size();++i)if(buttons_[i].owner==old->owner&&buttons_[i].declaration==old->declaration){index=i;break;}};restore(pressedButton_,previousPressed);restore(hoveredButton_,previousHovered);
}
void Engine::update_ui(Runtime& runtime){
 // Script polling still owns menu navigation; this dispatches declared controls, never hard-coded game menus.
 bool dispatched=false;
 const bool bonusMouse=state.profile==SemanticProfile::BonusA7;
 if(bonusMouse&&!global("mouse_mode").bits)return; // 10011dd0: selection survives; mode0 skips panel consumers.
 auto inside=[&](const Button& b,float x,float y){const auto* owner=state.get(b.owner);if(!owner||!owner->alive)return false;return ui_contains(b.rect,b.angle,x,y,bonusMouse,{ui_number(*owner,"pos_x"),ui_number(*owner,"pos_y")});};
 auto dispatch=[&](const Button& b,FunctionId function,std::string_view event){if(!function)return;const Object* owner=state.get(b.owner);if(!owner||!owner->alive||!field(b.owner,"visible"))return;set_global("event_type",Value::from_int(int(constant(event))));std::array<Argument,2> args{{{Value::from_int(b.declaration),{}},{Value::object(b.owner),{}}}};runtime.start(function,args,b.owner,0);dispatched=true;};
 std::optional<std::size_t> hover;if(!bonusMouse||(global("mouse_mode").bits&&global("mouse_valid").bits))for(std::size_t i=buttons_.size();i>0;--i)if(inside(buttons_[i-1],bonusMouse?mouseHit_.x:input.mouseX,bonusMouse?mouseHit_.y:input.mouseY)){hover=i-1;break;}
 if(hoveredButton_!=hover){if(hoveredButton_&&*hoveredButton_<buttons_.size()){Button b=buttons_[*hoveredButton_];dispatch(b,b.leaveCallback,"event_release");}hoveredButton_=hover;if(hover){Button b=buttons_[*hover];dispatch(b,b.overCallback,"event_touch");}}
 if(input.mousePressed[SDL_BUTTON_LEFT]&&hover){pressedButton_=hover;focusedButton_=*hover;Button b=buttons_[*hover];dispatch(b,b.callback,"event_click");}
 if(input.mouseReleased[SDL_BUTTON_LEFT]){if(pressedButton_&&*pressedButton_<buttons_.size()&&hover==pressedButton_){Button b=buttons_[*pressedButton_];dispatch(b,b.leaveCallback,"event_buttonup");}pressedButton_.reset();}
 if(buttons_.empty()){pressedButton_.reset();hoveredButton_.reset();focusedButton_=0;return;}
 if(!bonusMouse){bool next=input.pressed[SDL_SCANCODE_TAB]||input.pressed[SDL_SCANCODE_DOWN]||input.controllerPressed[SDL_CONTROLLER_BUTTON_DPAD_DOWN],previous=input.pressed[SDL_SCANCODE_UP]||input.controllerPressed[SDL_CONTROLLER_BUTTON_DPAD_UP];focusedButton_%=buttons_.size();if(next)focusedButton_=(focusedButton_+1)%buttons_.size();if(previous)focusedButton_=(focusedButton_+buttons_.size()-1)%buttons_.size();if(input.pressed[SDL_SCANCODE_RETURN]||input.pressed[SDL_SCANCODE_SPACE]||input.controllerPressed[SDL_CONTROLLER_BUTTON_A]){Button b=buttons_[focusedButton_];dispatch(b,b.callback,"event_click");}}
 if(dispatched)make_frame();
}
} // namespace k22
