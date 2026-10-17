#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine.hpp"
#include "wdl.hpp"
#include "saves.hpp"
#include "check_modern_box.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace {
using namespace k22;
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "k22-engine-XXXXXX").string();
        const auto* directory = ::mkdtemp(pattern.data());
        if (!directory) throw std::system_error(errno, std::generic_category(), "mkdtemp");
        path = directory;
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
struct SaveEnvironment {
    std::optional<std::string> previous;
    const char* name;
    explicit SaveEnvironment(const std::filesystem::path& path, const char* variable = "XDG_DATA_HOME")
        : name(variable) {
        if (const auto* value = std::getenv(name)) previous = value;
        if (::setenv(name, path.c_str(), 1))
            throw std::system_error(errno, std::generic_category(), "setenv");
    }
    ~SaveEnvironment() {
        if (previous) ::setenv(name, previous->c_str(), 1);
        else ::unsetenv(name);
    }
};
void close(double actual, double expected, double tolerance = 0.00003) {
    assert(std::isfinite(actual));
    if (std::abs(actual - expected) >= tolerance)
        std::cerr << "close: actual=" << actual << " expected=" << expected << " tolerance=" << tolerance << '\n';
    assert(std::abs(actual - expected) < tolerance);
}
void close_vector(Vec3 actual, Vec3 expected, double tolerance = 0.00003) {
    close(actual.x, expected.x, tolerance);
    close(actual.y, expected.y, tolerance);
    close(actual.z, expected.z, tolerance);
}
World triangle_world(Vec3 a, Vec3 b, Vec3 c) {
    World world;
    world.collision.vertices = {a, b, c};
    // Retain the same genuine XYZ source section used by the collision mesh.
    for (const auto point : {a, b, c}) for (float coordinate : {point.x, point.y, point.z}) {
        const auto bits = std::bit_cast<std::uint32_t>(coordinate);
        for (unsigned shift = 0; shift < 32; shift += 8)
            world.sections[3].push_back(std::uint8_t(bits >> shift));
    }
    world.collision.triangles = {{0, 1, 2}};
    world.collision.flags = {0};
    world.mesh.vertices = {{a, {0, 0, 1}, {}, {}},
                           {b, {0, 0, 1}, {}, {}},
                           {c, {0, 0, 1}, {}, {}}};
    world.mesh.triangles = {{{0, 1, 2}, 0, 0, 0}};
    world.infoFlags = 1;
    world.nativeLightmapStorageBytes = 3;
    world.sections[8] = {0};
    WorldFace face;
    face.edgeCount = 3; face.lightmapOffset = -1; face.nativeSampleShift = 4;
    face.lightmapOrigin = {1, 0};
    world.faces.push_back(face);
    // Actual owned-WMB constructor visits real leaves1..N-1 and retained signed edge topology.
    world.bspLeaves.resize(2);world.bspLeaves[0].content=-128;world.bspLeaves[1].content=-256;
    world.bspLeaves[1].blockCount=1;world.bspBlocks={0};
    const std::array<std::uint32_t,6> edges{0,0,1,0,2,0};
    const std::array<std::int32_t,3> references{0,1,2};
    world.sections[12].resize(sizeof(edges));std::memcpy(world.sections[12].data(),edges.data(),sizeof(edges));
    world.sections[13].resize(sizeof(references));std::memcpy(world.sections[13].data(),references.data(),sizeof(references));
    world.mesh.sourceVertices=world.mesh.vertices;
    Material material;
    // WMB user texture flags occupy bits 16+i, independently of entity flags.
    material.flags = 1u << (16 + 2);
    material.textureName = "regression-floor";
    material.raw.resize(64);
    material.raw[36] = 4;
    material.raw[42] = 50; // Original signed integer material ambient and fog fields.
    material.raw[43] = 25;
    material.ambient = {0.5f, 0.5f, 0.5f, 1};
    material.albedo.r = 0.25f;
    auto texture = std::make_shared<Image>();
    texture->width = texture->height = 1;
    texture->rgba = {255, 255, 255, 255};
    texture->nativeStorageBytes = 3;
    material.texture = std::move(texture);
    world.mesh.materials.push_back(std::move(material));
    return world;
}
void legacy_floor_fixture(World& world) {
    // Independently authored finite BSP floor at z=0; genuine epsilon contact is
    // z=1/32. Faces remain original source metadata, not render-triangle fallbacks.
    world.planes = {{{1, 0, 0}, 100, 0}, {{1, 0, 0}, -100, 0},
                    {{0, 1, 0}, 100, 1}, {{0, 1, 0}, -100, 1}, {{0, 0, 1}, 0, 2}};
    world.bspNodes = {{0, {-2, 1}}, {1, {2, -2}}, {2, {-2, 3}}, {3, {4, -2}},
                      {4, {-2, -1}, {-100, -100, -100}, {100, 100, 0}, 0, 1}};
    world.legacyPointNodes = {{0, {-256, 1}, 0}, {1, {2, -256}, 1},
                             {2, {-256, 3}, 2}, {3, {4, -256}, 3}, {4, {-256, -128}, 4}};
    world.hulls = {{0, {-256, 1}}, {1, {2, -256}}, {2, {-256, 3}},
                   {3, {4, -256}}, {4, {-256, -128}}};
    for (auto& hull : world.legacyHulls) hull = {0, 4, {}, {}, false};
    world.legacyHullsValid = true;
    world.mesh.flags = 0x10; world.mesh.nativeWorld = &world;
}
void write_animation_model(const std::filesystem::path& path, float bias = 0) {
    std::vector<uint8_t> bytes;
    auto word = [&](uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
    };
    auto shortWord = [&](uint16_t value) {
        bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8));
    };
    auto real = [&](float value) { word(std::bit_cast<uint32_t>(value)); };
    auto name = [&](std::string_view value) {
        const auto start = bytes.size(); bytes.resize(start + 16);
        std::copy(value.begin(), value.end(), bytes.begin() + start);
    };
    auto vertex = [&](Vec3 position, uint16_t index) {
        real(position.x); real(position.y); real(position.z);
        shortWord(index); bytes.push_back(0); bytes.push_back(0);
    };
    // Genuine MDL7 indexed replacement frames, including a noncontiguous second RUN block.
    bytes.insert(bytes.end(), {'M', 'D', 'L', '7'});
    for (uint32_t value : {7u, 0u, 1u, 0u, 0u, 0u}) word(value);
    for (uint16_t value : std::array<uint16_t, 10>{16, 28, 16, 68, 8, 12, 16, 16, 68, 24})
        shortWord(value);
    bytes.insert(bytes.end(), {1, 0, 0, 0}); word(0); name("animation-group");
    for (uint32_t value : {1u, 3u, 1u, 3u, 6u}) word(value);
    word(4); word(1); word(1); name("white");
    bytes.insert(bytes.end(), {255, 255, 255});
    for (const auto uv : std::array<Vec2, 3>{{{0, 0}, {1, 0}, {0, 1}}}) {
        real(uv.x); real(uv.y);
    }
    for (uint16_t value : std::array<uint16_t, 6>{0, 1, 2, 0, 1, 2}) shortWord(value);
    const std::array<Vec3, 3> triangle{{{bias, 0, 0}, {bias, 10, 0}, {bias, 0, 10}}};
    for (const auto position : triangle) vertex(position, 65535);
    const std::array<std::string_view, 6> scenes{"idle0", "run0", "run1", "run2", "stop0", "run3"};
    const std::array<float, 6> offsets{0, 10, 20, 40, 80, 160};
    for (std::size_t frame = 0; frame < scenes.size(); ++frame) {
        name(scenes[frame]); word(3); word(0);
        for (uint16_t index = 0; index < triangle.size(); ++index) {
            auto position = triangle[index]; position.x += offsets[frame];
            vertex(position, index);
        }
    }
    const auto size = uint32_t(bytes.size());
    for (unsigned shift = 0; shift < 32; shift += 8) bytes[16 + shift / 8] = uint8_t(size >> shift);
    std::ofstream fixture(path, std::ios::binary);
    assert(fixture.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())).good());
}
void a6_movement_consumers() {
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    Assets assets(temporary.path); Media media(assets, false); State state;
    state.profile = SemanticProfile::CampaignA6;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&](auto& vm, auto& ctx, auto name, auto args) {
        if(name=="nested_push_budget"){assert(engine.number("pushed_events")==1);return Value{};}
        return engine.builtin(vm, ctx, name, args);
    });
    runtime.load_text("a6-movement-consumer.wdl", R"(
        VAR contacts,contact_x,contact_z,blocked;
        FUNCTION touch() {
            IF(event_type==EVENT_STUCK) { blocked+=1; }
            ELSE { contacts+=1; contact_x=MY.x; contact_z=MY.z; }
        }
        VAR query_order,query_actor_x,impact_count,impact_normal,kill_query,pushed_events;
        VAR nested_up[3]=0,0,1; VAR nested_down[3]=0,0,-1;
        FUNCTION query_events(receiver,other) {
            IF(event_type==EVENT_SHOOT) { query_order=query_order*100+9;query_actor_x=YOU.x;
                IF(kill_query) { ent_remove(YOU);ent_remove(MY); }
            }
            IF(event_type==EVENT_SONAR) { query_order=query_order*100+11; }
            IF(event_type==EVENT_IMPACT) { impact_count+=1;impact_normal=normal.x; }
        }
        FUNCTION push_nested() {
            pushed_events+=1;nested_push_budget();
            ent_move(nullvector,nested_up);ent_move(nullvector,nested_down);
        }
    )");
    World floor; legacy_floor_fixture(floor); engine.world = &floor;
    Model resource; resource.flags = 0x20;
    auto mover = state.create(Object::Kind::Entity).id;
    auto& data = engine.entities[mover]; data.model = &resource; data.type = 2;
    state.member(mover, "event").set(Value::function(runtime.function("touch")));
    engine.set_field(mover, "event_flags", (1u << 1) | (1u << 2) | (1u << 3));
    auto v = [&](Vec3 value) {
        Argument arg{Value::object(state.create(Object::Kind::Vector).id), {}};
        engine.set_vector(arg, value); return arg;
    };
    auto move = [&](Vec3 start, Vec3 delta, double friction, std::uint32_t mode = 262144) {
        engine.set_position(mover, start);
        engine.set_global("move_mode", Value::from_int(mode));
        engine.set_global("move_friction", Value::from_double(friction));
        engine.set_global("contacts", {}); engine.set_global("blocked", {});
        ProcessContext ctx{0, mover, 0};
        const std::array args{v({}), v(delta)};
        return engine.builtin(runtime, ctx, "ent_move", args);
    };
    move({0, 0, 2}, {4, 0, -4}, .25);
    close_vector(engine.position(mover), {3.4921875f, 0, .03125f});
    close_vector(engine.vector(engine.global_vector("vglide")), {-1, 0, 4});
    close_vector(engine.vector(engine.global_vector("bounce")), {4, 0, 4});
    assert(engine.number("contacts") == 1 && engine.number("blocked") == 0);
    close(engine.number("contact_x"), 0); close(engine.number("contact_z"), 2);
    // Per-call slide deadband .2*(1-friction): a .1125 slide is dropped at the 72 FPS cap but kept when an fps_limit
    // frame (TIME_FRAME raw 68, 240 FPS) shrinks the deadband with the frame.
    move({0, 0, 2}, {.15f, 0, -4}, .25);
    assert(engine.position(mover).x < .08f);
    engine.set_global("time_frame", Value::from_raw(227));
    move({0, 0, 2}, {.15f, 0, -4}, .25);
    assert(engine.position(mover).x < .08f);
    engine.set_global("time_frame", Value::from_raw(68));
    move({0, 0, 2}, {.15f, 0, -4}, .25);
    assert(engine.position(mover).x > .1f);
    engine.set_global("time_frame", {});
    // The original helper does not clamp friction. Reversal stops at the contact,
    // before VGLIDE accumulation, and sends STUCK before final position publication.
    move({0, 0, 2}, {4, 0, -4}, 1.25);
    close_vector(engine.position(mover), {1.96875f, 0, .03125f});
    close_vector(engine.vector(engine.global_vector("vglide")), {});
    assert(engine.number("blocked") == 1);
    // World start-solid rejection scales velocity to .1 from the authored origin;
    // it is not a relocation, penetration solver or unqualified zero-motion stop.
    move({0, 0, -1}, {0, 0, -4}, .25);
    close_vector(engine.position(mover), {0, 0, -1.3994140625f});
    assert(engine.number("blocked") == 1 && engine.number("contacts") == 0);
    // Two coincident contact planes in an obtuse corner: neither single-plane
    // candidate clears the other, so 00439150/00439240 keeps the UNNORMALIZED crease.
    World corner;
    corner.planes = {{{1,0,0},-.03125f,0}, {{-.6f,.8f,0},-.03125f,3}};
    corner.legacyPointNodes = {{0,{1,-128},0}, {1,{-256,-128},1}};
    corner.hulls = corner.legacyPointNodes;
    corner.legacyHullsValid = true;
    for (auto& hull : corner.legacyHulls) hull = {0,1,{},{},false};
    engine.world = &corner;
    move({}, {-4,-4,4}, .25);
    close_vector(engine.position(mover), {0,0,1.919921875f}, .002);
    close_vector(engine.vector(engine.global_vector("vglide")), {4,4,-2.080078125f}, .002);
    assert(engine.number("contacts") == 1); // World contact is deduplicated per move.
    move({}, {-4,-4,0}, .25);
    close_vector(engine.position(mover), {});
    engine.set_field(mover, "flags", 512);
    move({}, {-4,-4,0}, .25);
    close_vector(engine.position(mover), {-4,-4,0});
    // Public modern consumers: retained yawed/asymmetric MY hull, direct
    // source surface TARGET and distance scalar (not normalized-time movement).
    engine.set_field(mover,"flags",0);engine.set_field(mover,"narrow",0);engine.set_field(mover,"fat",0);
    for(auto [axis,lo,hi]:std::array{std::tuple{"x",-4.,4.},std::tuple{"y",-1.,1.},std::tuple{"z",-1.,1.}}){
        engine.set_field(mover,std::string("min_")+axis,lo);engine.set_field(mover,std::string("max_")+axis,hi);
    }
    engine.set_field(mover,"pan",90);
    World wall=triangle_world({0,-100,-100},{0,100,-100},{0,0,100});engine.world=&wall;
    ProcessContext modernContext{0,mover,0};
    const std::array traceArgs{v({5,0,0}),v({-5,0,0}),Argument{Value::from_int(8192|1),{}}};
    close(engine.builtin(runtime,modernContext,"c_trace",traceArgs).real(),3.9501953125,.002);
    close(engine.vector(engine.global_vector("target")).x,.0498046875,.002);
    close_vector(engine.vector(engine.global_vector("normal")),{1,0,0},.002);
    engine.set_field(mover,"pan",0);engine.set_field(mover,"min_x",-1);engine.set_field(mover,"max_x",1);
    engine.set_field(mover,"min_z",-3);engine.set_field(mover,"max_z",-1);engine.set_field(mover,"scale_z",7);
    World modernFloor=triangle_world({-100,-100,0},{100,-100,0},{0,100,0});engine.world=&modernFloor;
    const std::array asymmetricArgs{v({0,0,5}),v({0,0,-5}),Argument{Value::from_int(8192|1),{}}};
    close(engine.builtin(runtime,modernContext,"c_trace",asymmetricArgs).real(),1.9501953125,.002);
    auto modernMove=[&](Vec3 start,Vec3 delta,std::uint32_t mode=262144){
        engine.set_position(mover,start);engine.set_global("contacts",{});engine.set_global("blocked",{});
        const std::array args{Argument{Value::object(mover),{}},v({}),v(delta),Argument{Value::from_int(mode),{}}};
        return engine.builtin(runtime,modernContext,"c_move",args);
    };
    modernMove({0,0,5},{0,0,-10},0);
    close(engine.position(mover).z,3.0498046875,.003);
    close(engine.number("contact_z"),engine.position(mover).z,.001); // Callback sees this pass's new public XYZ.
    engine.world=&wall;engine.set_field(mover,"min_z",-1);engine.set_field(mover,"max_z",1);
    engine.set_global("move_min_z",Value::from_int(-1));engine.set_global("disable_z_glide",{});
    engine.set_vector(engine.global_vector("vglide"),{7,8,9});engine.set_global("move_friction",Value::from_double(1.25));
    modernMove({2,0,0},{-4,0,4});
    close(engine.position(mover).z,4,.003);assert(engine.position(mover).x>1);
    close_vector(engine.vector(engine.global_vector("vglide")),{7,8,9}); // A6 modern does not publish legacy VGLIDE/friction.
    engine.set_global("disable_z_glide",Value::from_int(1));
    modernMove({2,0,0},{-4,0,4});
    assert(engine.position(mover).z>.9f&&engine.position(mover).z<1.f&&engine.position(mover).x>1.f);
    engine.set_global("disable_z_glide",{});engine.set_global("move_min_z",Value::from_int(1));
    modernMove({2,0,0},{-4,0,4});
    assert(engine.position(mover).z>.9f&&engine.position(mover).z<1.f); // Independent source MOVE_MIN_Z gate.
    engine.set_global("move_min_z",Value::from_int(-1));engine.world=nullptr;
    modernMove({}, {20,.39f,0},0);
    close_vector(engine.position(mover),{20.0029296875f,0,0},.001); // 0046f45a renormalizes snapped .0195 minor component.
    auto receiver=state.create(Object::Kind::Entity).id;
    auto& receiverData=engine.entities[receiver];receiverData.model=&wall.mesh;receiverData.type=2;
    for(auto axis:{"x","y","z"}){engine.set_field(receiver,std::string("scale_")+axis,1);engine.set_field(receiver,std::string("min_")+axis,-1);engine.set_field(receiver,std::string("max_")+axis,1);}
    state.member(receiver,"event").set(Value::function(runtime.function("query_events")));
    engine.set_field(receiver,"event_flags",0);
    modernMove({5,0,0},{-10,0,0},0);
    close_vector(engine.vector(engine.global_vector("normal")),{1,0,0},.002);
    engine.set_field(receiver,"event_flags",1u<<5);
    modernMove({5,0,0},{-10,0,0},0);
    assert(engine.number("impact_count")==1);close(engine.number("impact_normal"),-1,.002);
    close_vector(engine.vector(engine.global_vector("normal")),{-1,0,0},.002);
    engine.set_field(receiver,"event_flags",(1u<<9)|(1u<<11));
    modernMove({5,0,0},{-10,0,0},1024|2048);
    assert(engine.number("query_order")==911);close(engine.number("query_actor_x"),5);
    close_vector(engine.vector(engine.global_vector("normal")),{1,0,0},.002);
    // Source correction is not a broadphase horizon: a plane .025 nearer
    // must win against the global plane even though its geometric entry is later than the backed-off old best.
    engine.world=&modernFloor;receiverData.model=&modernFloor.mesh;engine.set_position(receiver,{0,0,.025f});
    engine.set_field(receiver,"event_flags",0);engine.set_field(mover,"scale_z",1);
    const std::array closestArgs{v({0,0,5}),v({0,0,-5}),Argument{Value::from_int(8192|1),{}}};
    close(engine.builtin(runtime,modernContext,"c_trace",closestArgs).real(),3.9251953125,.002);
    assert(modernContext.you==receiver);close(engine.vector(engine.global_vector("target")).z,.0748046875,.002);
    engine.remove_entity(runtime,receiver);
    World layered=triangle_world({-100,-100,0},{100,-100,0},{0,100,0});
    layered.collision.vertices.insert(layered.collision.vertices.end(),{{-100,-100,.025f},{100,-100,.025f},{0,100,.025f}});
    layered.collision.triangles.push_back({3,4,5});layered.collision.flags.push_back(0);
    engine.world=&layered;engine.collisionOrder={0,1};
    engine.collisionNodes={{{-100,-100,0},{100,100,.025f},0,0,1,2},{{-100,-100,.025f},{100,100,.025f},1,1,0,0},{{-100,-100,0},{100,100,0},0,1,0,0}};
    close(engine.builtin(runtime,modernContext,"c_trace",closestArgs).real(),3.9251953125,.002);
    assert(modernContext.you==0);close(engine.vector(engine.global_vector("target")).z,.0748046875,.002);
    engine.collisionNodes.clear();engine.collisionOrder.clear();
    World destination;legacy_floor_fixture(destination);engine.world=&destination;engine.set_field(mover,"flags",512);
    for(auto content:{-126,-128,-256}){
        destination.legacyPointNodes.back().children[1]=content;
        modernMove({0,0,2},{0,0,-4},0);
        close_vector(engine.position(mover),{0,0,-2});
        assert(engine.number("in_passable")==int(content==-126)&&engine.number("in_solid")==int(content==-128));
    }
    // EVENT_PUSH performs two real nested legacy moves, restoring its pose but
    // changing the current source epoch. A stale post-callback stamp would repeat indefinitely.
    engine.world=nullptr;engine.set_field(mover,"flags",0);engine.set_field(mover,"push",1);
    receiver=state.create(Object::Kind::Entity).id;auto& pushed=engine.entities[receiver];pushed.model=&resource;pushed.type=2;
    for(auto axis:{"x","y","z"}){engine.set_field(receiver,std::string("scale_")+axis,1);engine.set_field(receiver,std::string("min_")+axis,-1);engine.set_field(receiver,std::string("max_")+axis,1);}
    engine.set_position(receiver,{5,0,0});engine.set_field(receiver,"push",0);engine.set_field(receiver,"event_flags",1u<<4);
    state.member(receiver,"event").set(Value::function(runtime.function("push_nested")));
    move({}, {10,0,0},.25,512);
    assert(engine.number("pushed_events")==1);close_vector(engine.position(mover),{10,0,0});close_vector(engine.position(receiver),{5,0,0});
    engine.remove_entity(runtime,receiver);engine.set_field(mover,"push",0);
    // SHOOT removal invalidates both actor and recipient before SONAR/impact; no stale owner access or final actor write.
    receiver=state.create(Object::Kind::Entity).id;auto& removed=engine.entities[receiver];removed.model=&wall.mesh;removed.type=2;
    for(auto axis:{"x","y","z"}){engine.set_field(receiver,std::string("scale_")+axis,1);engine.set_field(receiver,std::string("min_")+axis,-1);engine.set_field(receiver,std::string("max_")+axis,1);}
    engine.set_field(receiver,"event_flags",(1u<<9)|(1u<<11)|(1u<<5));state.member(receiver,"event").set(Value::function(runtime.function("query_events")));
    engine.set_global("query_order",{});engine.set_global("kill_query",Value::from_int(1));
    close(modernMove({5,0,0},{-10,0,0},1024|2048).real(),0);
    assert(engine.number("query_order")==9&&!engine.entities.contains(mover)&&!engine.entities.contains(receiver));
}

void legacy_trace_consumers() {
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    auto write_world = [&](std::string_view name, bool isGeneric, bool hasCompiledNodes) {
        std::array<std::vector<std::uint8_t>, 17> sections;
        auto word = [](std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
            if (bytes.size() < offset + 4) bytes.resize(offset + 4);
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes[offset + shift / 8] = std::uint8_t(value >> shift);
        };
        sections[1].resize(20);
        word(sections[1], 8, std::bit_cast<std::uint32_t>(1.f));
        word(sections[1], 12, std::bit_cast<std::uint32_t>(-9.25f)); word(sections[1], 16, 2);
        sections[3].resize(36);
        for (std::size_t i = 0; i < 9; ++i)
            word(sections[3], i * 4, std::bit_cast<std::uint32_t>(i < 3 ? -100.f : 100.f));
        if (hasCompiledNodes) {
            sections[9].resize(16); word(sections[9], 4, 0xdeadbeef);
            word(sections[9], 8, std::uint32_t(-256)); word(sections[9], 12, std::uint32_t(-128));
        }
        sections[14].resize(160);
        for (std::size_t i = 0; i < 3; ++i) {
            word(sections[14], 0x40 + i * 4, isGeneric ? 0 : std::uint32_t(-int(i + 1)));
            word(sections[14], 0x70 + i * 4, std::uint32_t(i + 4));
            word(sections[14], 0x4c + i * 4, std::uint32_t(-int(i + 7)));
            word(sections[14], 0x7c + i * 4, std::uint32_t(i + 10));
        }
        std::vector<std::uint8_t> bytes(140); std::memcpy(bytes.data(), "WMB6", 4);
        for (std::size_t i = 0; i < sections.size(); ++i) if (!sections[i].empty()) {
            word(bytes, 4 + i * 8, std::uint32_t(bytes.size()));
            word(bytes, 8 + i * 8, std::uint32_t(sections[i].size()));
            bytes.insert(bytes.end(), sections[i].begin(), sections[i].end());
        }
        std::ofstream output(temporary.path / name, std::ios::binary);
        assert(output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())).good());
    };
    write_world("published.wmb", false, true);
    write_world("generic.wmb", true, true);
    write_world("empty.wmb", false, false);
    // Borrowed resources stay alive together for the complete Engine lifetime.
    World floor, generic, nearMap, insideMap, missing;
    Model model, zeroFlagResource;
    auto halfspace = [](World& world, std::array<float, 3> heights) {
        world.mesh.flags = 0x10;
        world.mesh.nativeWorld = &world;
        world.mesh.min = {-100, -100, -100}; world.mesh.max = {100, 100, 100};
        for (auto height : heights) world.planes.push_back({{0, 0, 1}, height, 2});
        world.bspNodes.push_back({0, {-2, -1}, {-100, -100, -100}, {100, 100, 100}, 0, 0});
        world.bspLeaves.resize(2);
        world.bspLeaves[0].content = -128; world.bspLeaves[1].content = -256;
        world.legacyPointNodes = {{0, {-256, -128}, 0}};
        world.hulls = {{1, {-256, -128}}, {2, {-256, -128}}};
        world.legacyHulls[0] = {0, 0, {}, {}, false};
        world.legacyHulls[1] = {0, 1, {-1, -1, -1}, {1, 1, 1}, false};
        world.legacyHulls[2] = {1, 1, {-2, -2, -2}, {2, 2, 2}, false};
        world.legacyHullsValid = true;
        world.legacyHullBoundsPublished = true;
    };
    halfspace(floor, {-10.25f, -9.5f, -8.125f});
    halfspace(generic, {-10.25f, -9.25f, -8.25f});
    generic.legacyHulls[1].generic = true;
    halfspace(nearMap, {-9.25f, -9.25f, -9.25f});
    halfspace(insideMap, {-7, -7, -7});
    model.flags = 0x20; model.min = {-1, -1, -1}; model.max = {1, 1, 1};
    zeroFlagResource.min = model.min; zeroFlagResource.max = model.max;
    Assets assets(temporary.path); Media media(assets, false); State state;
    state.profile = SemanticProfile::CampaignA6;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&](Runtime& vm, ProcessContext& context, std::string_view name,
                               std::span<const Argument> args) { return engine.builtin(vm, context, name, args); });
    runtime.load_text("a6-compiled-trace-consumer.wdl", R"wdl(
        VAR plain_owner;
        function cast(from,to,mode) {
            trace_mode=mode; var distance=trace(from,to); plain_owner=YOU; return distance;
        }
        function cast_you(from,to,mode,who) {
            you=who; trace_mode=mode; trace(from,to); plain_owner=YOU;
        }
    )wdl");
    auto vector = [&](Vec3 value) {
        Argument argument{Value::object(state.create(Object::Kind::Vector).id), {}};
        engine.set_vector(argument, value); return argument;
    };
    auto actor = [&](const Model& resource, std::uint32_t type, Vec3 position) {
        auto id = state.create(Object::Kind::Entity).id;
        auto& data = engine.entities.try_emplace(id).first->second;
        data.model = &resource; data.type = type; engine.set_position(id, position);
        for (auto axis : {"x", "y", "z"}) {
            engine.set_field(id, std::string("scale_") + axis, 1);
            engine.set_field(id, std::string("min_") + axis, -4);
            engine.set_field(id, std::string("max_") + axis, 4);
        }
        return id;
    };
    const auto me = actor(model, 2, {1000, 1000, 1000});
    auto cast = [&](Vec3 from, Vec3 to, std::uint32_t mode, ObjectId owner = 0) {
        const std::array args{vector(from), vector(to), Argument{Value::from_int(mode), {}}};
        return runtime.start("cast", args, owner, 0);
    };
    engine.world = &floor;
    engine.set_vector(engine.global_vector("hull_narrowmin"), {-1, -1, -1});
    engine.set_vector(engine.global_vector("hull_narrowmax"), {1, 1, 1});
    engine.set_vector(engine.global_vector("hull_fatmin"), {-2, -2, -2});
    engine.set_vector(engine.global_vector("hull_fatmax"), {2, 2, 2});
    const Vec3 start{0, 0, -8}, end{0, 0, -20};
    // 004514b0 epsilon=1/32, not an ellipsoid touching the visual triangles.
    assert(cast(start, end, 0).raw() == 2272);
    close_vector(engine.vector(engine.global_vector("target")), {0, 0, -10.21875f});
    close_vector(engine.vector(engine.global_vector("normal")), {0, 0, 1});
    // NARROW is outside the compiled floor even though its public full hull overlaps.
    // 004546f0 separately truncates distance and signed hull correction; a negative
    // corrected distance is NOT the embedded-solid sentinel.
    for (auto shape : {0u, 1u, 2u, 3u}) {
        engine.set_field(me, "narrow", shape & 1); engine.set_field(me, "fat", bool(shape & 2));
        const std::array<std::int32_t, 4> expected{-1824, -1568, -1952, -1952};
        const std::array<float, 4> target{-10.21875f, -10.46875f, -10.09375f, -10.09375f};
        assert(cast(start, end, 8192 | 1, me).raw() == expected[shape]);
        close_vector(engine.vector(engine.global_vector("target")), {0, 0, target[shape]});
        close(engine.number("in_solid"), 0);
        close_vector(engine.vector(engine.global_vector("normal")), {0, 0, 1});
    }
    engine.set_field(me, "narrow", 1); engine.set_field(me, "fat", 0);
    assert(cast({0, 0, -9.75f}, end, 8192 | 1, me).raw() == -1024);
    close_vector(engine.vector(engine.global_vector("target")), {0, 0, -9.75f});
    close_vector(engine.vector(engine.global_vector("normal")), {});
    close(engine.number("in_solid"), 1);
    // A public no-hit resets status/RESULT but leaves NORMAL untouched.
    engine.set_vector(engine.global_vector("normal"), {3, 4, 5});
    assert(cast({0, 0, -8}, {0, 0, -7}, 0).raw() == 0);
    close_vector(engine.vector(engine.global_vector("normal")), {3, 4, 5});
    close(engine.number("in_solid"), 0);
    // Generic WMB support globals are zero-cleared by 004523f0, not actor bounds.
    engine.world = &generic;
    assert(cast(start, end, 8192 | 1, me).raw() == -1824);
    generic.planes[1] = {{0, 0, -1}, 9.25f, std::uint32_t(std::int32_t(-1))};
    generic.hulls[0].children = {-128, -256};
    assert(cast(start, end, 8192 | 1, me).raw() == -1824);
    close_vector(engine.vector(engine.global_vector("normal")), {0, 0, 1});
    generic.planes[1] = {{0.6f, 0, 0.8f}, -7.4f, 3};
    generic.hulls[0].children = {-256, -128};
    assert(cast(start, end, 8192 | 1, me).raw() == -1832);
    close_vector(engine.vector(engine.global_vector("target")), {0, 0, -10.2109375f}, 0.0001);
    // NORMAL is published as original 22.10 VAR words, not geometric float components.
    close_vector(engine.vector(engine.global_vector("normal")), {614.f / 1024, 0, 819.f / 1024});
    // PUSH suppresses global WMB only when IGNORE_ME actually supplies ignored ME.
    engine.world = &floor; engine.set_field(me, "push", 1);
    assert(cast(start, end, 0, me).raw() == 2272);
    assert(cast(start, end, 1, me).raw() == 0);
    assert(cast(start, end, 128, me).raw() == 2272);
    const auto rejected = engine.create_entity(runtime, "", start, 0);
    assert(cast(start, end, 0).raw() == 2272); // No resource: no six-plane candidate.
    engine.remove_entity(runtime, rejected);
    engine.world = nullptr;
    // Serialized MDL flags zero is valid, not the original runtime resource-category word.
    const auto primitive = actor(zeroFlagResource, 2, {});
    assert(cast({10, 2, 3}, {-10, 2, 3}, 0).raw() == 6112);
    close_vector(engine.vector(engine.global_vector("target")), {4.03125f, 2, 3});
    // MDL public normal is the normalized start-to-owner XY vector (00454e8e),
    // not the geometric box face normal used by the movement solver.
    close_vector(engine.vector(engine.global_vector("normal")), {1004.f / 1024, 200.f / 1024, 0});
    assert(cast({10, 2, 3}, {-10, 2, 3}, 16).raw() == 0);
    // 004184b0 registers VIEW actors on the separate special list, not world collision.
    const auto presentationView = state.create(Object::Kind::View).id;
    state.member(primitive, "view").set(Value::object(presentationView));
    assert(cast({10, 2, 3}, {-10, 2, 3}, 0).raw() == 0);
    assert(engine.global("plain_owner").bits == 0);
    state.member(primitive, "view").set(Value::object(0));
    assert(cast({10, 2, 3}, {-10, 2, 3}, 0).raw() == 6112);
    assert(engine.global("plain_owner").bits == primitive);
    close_vector(engine.vector(engine.global_vector("target")), {4.03125f, 2, 3});
    // 004546f0 rewrites YOU storage 006441dc even with IGNORE_YOU: the hit entity, else NULL
    // (dinamik_modeller.wdl ucakkursun_gonder damages YOU after an IGNORE_YOU trace).
    for (auto [from, expected] : {std::pair{Vec3{10, 2, 3}, primitive}, {Vec3{10, 20, 3}, ObjectId{0}}}) {
        const std::array args{vector(from), vector({-10, from.y, 3}), Argument{Value::from_int(2), {}}, Argument{Value::object(me), {}}};
        runtime.start("cast_you", args, 0, 0);
        assert(engine.global("plain_owner").bits == expected);
    }
    engine.remove_entity(runtime, primitive); engine.world = &floor;
    engine.set_field(me, "push", 0);
    const auto nearer = actor(nearMap.mesh, 4, {});
    engine.set_field(nearer, "min_z", -100); engine.set_field(nearer, "max_z", 100);
    assert(cast(start, end, 0).raw() == 1248);
    assert(engine.global("plain_owner").bits == nearer);
    assert(cast(start, end, 128).raw() == 2272);
    engine.set_position(nearer, {0, 0, -1});
    assert(cast(start, end, 0).raw() == 2272); // Equal ordinary contacts retain global world.
    assert(engine.global("plain_owner").bits == 0);
    engine.set_position(nearer, {});
    engine.set_field(nearer, "flags", 512);
    assert(cast(start, end, 8).raw() == 1248); // IGNORE_PASSENTS does not reject WMB.
    close(engine.number("on_passable"), 1); close(engine.number("in_passable"), 0);
    assert(cast(start, end, 4).raw() == 2272);
    // A later all-solid WMB supersedes an earlier ordinary nearest contact.
    engine.set_field(nearer, "flags", 0);
    const auto embedded = actor(insideMap.mesh, 4, {});
    engine.set_field(embedded, "min_z", -100); engine.set_field(embedded, "max_z", 100);
    assert(cast(start, end, 0).raw() == -1024);
    assert(engine.global("plain_owner").bits == embedded);
    close_vector(engine.vector(engine.global_vector("target")), start);
    close_vector(engine.vector(engine.global_vector("normal")), {});
    close(engine.number("in_solid"), 1);
    engine.set_field(embedded, "flags", 512);
    // 004523f0 retains contact=end; 00452f90 forces only fraction to zero.
    // PASSABLE WMB bypasses -1 in 004546f0, which measures contact-start, not fraction.
    for (auto mode : {0u, 8u}) { // IGNORE_PASSENTS still admits PASSABLE WMB.
        const auto contact = engine.collide(start, end, {}, mode, 0);
        assert(contact.legacyQuery && contact.hit && contact.status == 7);
        assert(contact.entity == embedded && contact.queryWorld == &insideMap);
        close(contact.fraction, 0); close_vector(contact.position, end);
        close_vector(contact.normal, {});
        assert(cast(start, end, mode).raw() == 12288); // |(0,0,-20)-(0,0,-8)| * 1024.
        assert(engine.global("result").raw() == 12288);
        assert(engine.global("plain_owner").bits == embedded);
        close_vector(engine.vector(engine.global_vector("target")), end);
        close_vector(engine.vector(engine.global_vector("normal")), {});
        close(engine.number("trace_hit"), 1);
        close(engine.number("in_solid"), 0); close(engine.number("in_passable"), 1);
        close(engine.number("on_passable"), 0);
    }
    assert(cast(start, end, 4).raw() == 1248); // IGNORE_PASSABLE retains the nearer solid WMB.
    assert(engine.global("plain_owner").bits == nearer);
    close_vector(engine.vector(engine.global_vector("target")), {0, 0, -9.21875f});
    close_vector(engine.vector(engine.global_vector("normal")), {0, 0, 1});
    close(engine.number("in_solid"), 0); close(engine.number("in_passable"), 0);
    close(engine.number("on_passable"), 0);
    engine.world = &insideMap;
    assert(cast(start, end, 0).raw() == -1024); // Global all-solid skips all candidate traversal.
    assert(engine.global("plain_owner").bits == 0);
    engine.entities.clear();
    engine.world = &floor;
    floor.legacyPointNodes[0].children[1] = -126;
    floor.bspLeaves[0].content = -126;
    // 004514b0: OPEN->PASSABLE contact is status0x50, not start-in-passable0xa0.
    assert(cast(start, end, 0).raw() == 2272);
    close(engine.number("in_passable"), 0); close(engine.number("on_passable"), 1);
    close(engine.number("in_solid"), 0);
    assert(cast(start, end, 4).raw() == 0);
    close(engine.number("in_passable"), 0); close(engine.number("on_passable"), 0);
    const auto mover = actor(model, 2, start);
    // actor() leaves NARROW/FAT clear: USE_BOX8192 still selects POINT (00452a45..58).
    // Its PASSABLE contact sets MOVE.IN_PASSABLE from status0x40 (00453bbe), unlike TRACE.
    auto move = [&](Vec3 delta) {
        ProcessContext context{0, mover, 0};
        const std::array args{vector({}), vector(delta)};
        return engine.builtin(runtime, context, "ent_move", args);
    };
    engine.set_global("move_mode", Value::from_int(0));
    for (auto [name, value] : {std::pair{"in_solid", 11}, {"in_passable", 12}, {"on_passable", 13}})
        engine.set_global(name, Value::from_int(value));
    const Vec3 savedVector{17, 18, 19};
    for (auto name : {"normal", "bounce", "vglide", "target"})
        engine.set_vector(engine.global_vector(name), savedVector);
    move({});
    close(engine.number("in_solid"), 11); close(engine.number("in_passable"), 12);
    close(engine.number("on_passable"), 13);
    for (auto name : {"normal", "bounce", "vglide", "target"})
        close_vector(engine.vector(engine.global_vector(name)), savedVector);
    engine.set_field(mover, "flags", 512);
    move({0, 0, -3});
    close_vector(engine.position(mover), {0, 0, -11});
    close(engine.number("in_solid"), 0); close(engine.number("in_passable"), 1);
    close(engine.number("on_passable"), 13);
    for (auto name : {"normal", "bounce", "vglide", "target"})
        close_vector(engine.vector(engine.global_vector(name)), savedVector);
    engine.set_field(mover, "flags", 0); engine.set_position(mover, start);
    move({0, 0, -12});
    close(engine.number("in_solid"), 0); close(engine.number("in_passable"), 1);
    close(engine.number("on_passable"), 13);
    floor.legacyPointNodes[0].children[1] = -128; floor.bspLeaves[0].content = -128;
    engine.set_position(mover, start); move({0, 0, -12});
    close(engine.number("in_solid"), 0); close(engine.number("in_passable"), 0);
    close(engine.number("on_passable"), 13);
    close_vector(engine.vector(engine.global_vector("normal")), {0, 0, 1});
    close_vector(engine.vector(engine.global_vector("target")), savedVector);
    engine.set_position(mover, start); move({1, 0, 0});
    for (auto name : {"normal", "bounce", "vglide"})
        close_vector(engine.vector(engine.global_vector(name)), {});
    close_vector(engine.vector(engine.global_vector("target")), savedVector);
    engine.entities.clear();
    engine.world = &missing;
    bool rejectedMissingBsp = false;
    try { cast(start, end, 0); }
    catch (const std::runtime_error& error) {
        rejectedMissingBsp = std::string_view(error.what()).find("requires compiled BSP metadata") != std::string_view::npos;
        if (!rejectedMissingBsp) throw;
    }
    assert(rejectedMissingBsp); // Plain A6 never falls back to absent triangles/metadata.
    // 0045b470..49d: non-level resource loads do not publish HULL globals.
    const Vec3 sentinel{31, 32, 33};
    for (auto name : {"hull_narrowmin", "hull_narrowmax", "hull_fatmin", "hull_fatmax"})
        engine.set_vector(engine.global_vector(name), sentinel);
    assets.world("published.wmb");
    for (auto name : {"hull_narrowmin", "hull_narrowmax", "hull_fatmin", "hull_fatmax"})
        close_vector(engine.vector(engine.global_vector(name)), sentinel);
    engine.load_level(runtime, "published.wmb");
    close_vector(engine.vector(engine.global_vector("hull_narrowmin")), {-4, -5, -6});
    close_vector(engine.vector(engine.global_vector("hull_narrowmax")), {1, 2, 3});
    close_vector(engine.vector(engine.global_vector("hull_fatmin")), {-10, -11, -12});
    close_vector(engine.vector(engine.global_vector("hull_fatmax")), {7, 8, 9});
    for (auto file : {"generic.wmb", "empty.wmb"}) {
        for (auto name : {"hull_narrowmin", "hull_narrowmax", "hull_fatmin", "hull_fatmax"})
            engine.set_vector(engine.global_vector(name), sentinel);
        engine.load_level(runtime, file);
        for (auto name : {"hull_narrowmin", "hull_narrowmax", "hull_fatmin", "hull_fatmax"})
            close_vector(engine.vector(engine.global_vector(name)), sentinel);
    }
}
void graphics_consumers() {
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    for (int slot = 0; slot < 4; ++slot)
        write_animation_model(temporary.path / ("animation_" + std::to_string(slot) + ".mdl"), float(slot * 100));
    write_animation_model(temporary.path / "missing_0.mdl");
    write_animation_model(temporary.path / "solo.mdl");
    // Oracles: paired planar-shadow-source and graphics-settings-source contracts,
    // not producer field copies. Each profile owns one uninterrupted resource/Engine lifetime.
    for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
        const bool bonus = profile == SemanticProfile::BonusA7;
        auto floor = triangle_world({-100, -100, 0}, {100, -100, 0}, {0, 100, 0});
        legacy_floor_fixture(floor);
        // Map polygon admission uses the resource bounds before visiting its genuine vertices.
        floor.mesh.min = {-100, -100, 0};
        floor.mesh.max = {100, 100, 0};
        World unsupported;
        floor.faces[0].lightmapOffset = 0;
        floor.sections[16].assign(256 * 256 * 3, 0);
        floor.sections[16][3] = 100; floor.sections[16][4] = 40; floor.sections[16][5] = 250;
        Assets assets(temporary.path, bonus);
        Media media(assets, false);
        State state; state.profile = profile;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(vm, context, name, args);
        });
        runtime.load_text("graphics-consumers.wdl", R"wdl(
            MATERIAL authoredShadow { alpha=37; }
            function bind_shadow() { mat_shadow=authoredShadow; }
            function edit(p) { vec_to_mesh(p,my,1); }
            function morph() { ent_morph(my,"animation_0.mdl"); }
            function purge() { ent_purge(my); }
        )wdl");
        auto numeric = [&](std::string_view name, double value) { engine.set_global(name, Value::from_double(value)); };
        auto vector_arg = [&](Vec3 value) {
            const auto id = state.create(Object::Kind::Vector).id;
            Argument argument{Value::object(id), {}}; engine.set_vector(argument, value); return argument;
        };
        auto draw_for = [&](ObjectId id) {
            for (const auto& draw : engine.frame().instances) if (draw.objectId == id) return draw;
            throw std::runtime_error("graphics consumer actor missing from ordinary packet");
        };
        const auto cameraId = engine.global("camera").bits;
        auto shadow_for = [&](ObjectId id, ObjectId view) -> std::optional<DrawInstance> {
            for (const auto& draw : engine.frame().shadows)
                if (draw.objectId == id && draw.shadowViewId == view) return draw;
            return {};
        };
        auto sample = [&](ObjectId id, const Camera& view) {
            engine.prepare(runtime);
            const auto draw = draw_for(id);
            return engine.frame().prepareEntityLight(engine.frame().lightContext, draw, view);
        };
        engine.world = &floor;
        numeric("TRACE_FLOOR", 1); numeric("ENABLE_POLYCOLLISION", 1);
        numeric("SHADOW_RANGE", 50); numeric("SHADOW_THRESHOLD", 50);
        numeric("SUN_LIGHT", 0);
        engine.set_field(cameraId, "clip_far", 100); engine.set_position(cameraId, {0, 0, 5});
        Camera view; view.position = {0, 0, 5}; view.nearClip = 1; view.farClip = 100;
        const auto actor = engine.create_entity(runtime, "animation_0.mdl", {0, 0, 5}, 0);
        engine.set_field(actor, "shadow", 1);
        const auto* base = engine.entities.at(actor).model;
        assert(base->formatVersion == 7 && base->frames.size() == 6 &&
               base->vertices.size() == 3 && base->triangles.size() == 1);
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto* model = base->shadowLods[slot];
            assert(model && model != base && model->formatVersion == 7 && model->frames.size() == 6);
            close_vector(model->frames[0].positions[0], {float((slot + 1) * 100), 0, 0});
            close_vector(model->frames[1].positions[0], {float((slot + 1) * 100 + 10), 0, 0});
        }
        auto& cache = engine.entities.at(actor).light;
        constexpr std::array scratchNames{"trace_hit", "hitvertex", "result", "tex_light", "tex_fog", "trace_mode"};
        for (std::size_t i = 0; i < scratchNames.size(); ++i) numeric(scratchNames[i], 700 + i);
        engine.set_vector(engine.global_vector("target"), {11, 12, 13});
        engine.set_vector(engine.global_vector("normal"), {21, 22, 23});
        engine.prepare(runtime);
        const auto born = draw_for(actor);
        assert(cache.dirty && !cache.floorValid && !born.nativeShadow.caster);
        assert(!engine.frame().cachedEntityShadow(engine.frame().lightContext, born).caster && cache.dirty);
        auto light = engine.frame().prepareEntityLight(engine.frame().lightContext, born, view);
        assert(light.scalar == 180 && (light.delta == std::array<std::int16_t, 3>{-15, -75, 90}));
        assert(cache.floorValid && !cache.dirty && cache.style == 25 && light.shadow.caster);
        close(engine.field(actor, "floor_dist"), bonus ? 5 : 4.96875, .002);
        close(light.shadow.planeZ, bonus ? 0 : 0.03125); close_vector(light.shadow.negativeNormal, {0, 0, -1});
        assert(!born.nativeShadow.caster && engine.frame().cachedEntityShadow(engine.frame().lightContext, born).caster);
        for (std::size_t i = 0; i < scratchNames.size(); ++i) close(engine.number(scratchNames[i]), 700 + i);
        close_vector(engine.vector(engine.global_vector("target")), {11, 12, 13});
        close_vector(engine.vector(engine.global_vector("normal")), {21, 22, 23});
        floor.sections[16][3] = 60;
        assert(sample(actor, view).scalar == 180); close(engine.field(actor, "floor_dist"), bonus ? 5 : 4.96875, .002);
        floor.sections[16][3] = 100;
        numeric("SHADOW_THRESHOLD", 180); assert(!sample(actor, view).shadow.caster && !cache.dirty);
        numeric("SHADOW_THRESHOLD", 179.5); assert(sample(actor, view).shadow.caster && !cache.dirty);
        // Signed cache admission is independent of the positive real sampler control above.
        cache.scalar = -1;
        numeric("SHADOW_THRESHOLD", 0); assert(!sample(actor, view).shadow.caster);
        numeric("SHADOW_THRESHOLD", -2); assert(sample(actor, view).shadow.caster);
        numeric("SHADOW_THRESHOLD", -1); assert(!sample(actor, view).shadow.caster);
        cache.scalar = 180; numeric("SHADOW_THRESHOLD", 50);
        view.position = {50, 0, 5}; assert(!sample(actor, view).shadow.caster);
        view.position = {49.9f, 0, 5}; assert(sample(actor, view).shadow.caster);
        view.position = {0, 0, 105}; numeric("D3D_ENTSORT", 1); assert(!sample(actor, view).shadow.caster);
        numeric("D3D_ENTSORT", 2); assert(sample(actor, view).shadow.caster);
        view.position = {0, 0, 5};
        numeric("SHADOW_STENCIL", 1); numeric("SHADOW_THRESHOLD", 180);
        assert(sample(actor, view).shadow.caster == !bonus);
        numeric("SHADOW_STENCIL", 0); assert(!sample(actor, view).shadow.caster);
        numeric("SHADOW_THRESHOLD", 50);
        engine.set_position(actor, {0, 0, 0}); assert(!sample(actor, view).shadow.caster && !cache.floorValid);
        close(engine.field(actor, "floor_dist"), -1);
        engine.set_position(actor, {0, 0, 5}); assert(sample(actor, view).scalar == 180);
        engine.set_position(actor, {200, 200, 5}); light = sample(actor, view);
        assert(!cache.floorValid && !light.shadow.caster && light.scalar == (bonus ? 180 : 127));
        assert(cache.style == (bonus ? 25 : 50));
        assert((cache.delta == (bonus ? std::array<std::int16_t, 3>{-15, -75, 90} : std::array<std::int16_t, 3>{})));
        close(engine.field(actor, "floor_dist"), -1);
        numeric("TRACE_FLOOR", 0); engine.set_position(actor, {0, 0, 5});
        sample(actor, view); assert(!cache.dirty && !cache.floorValid);
        numeric("TRACE_FLOOR", 1); sample(actor, view); assert(!cache.floorValid);
        engine.set_field(actor, "shadow", 0); engine.set_field(actor, "unlit", 1);
        engine.set_position(actor, {1, 0, 5}); sample(actor, view); assert(!cache.floorValid && !cache.dirty);
        engine.set_field(actor, "shadow", 1); assert(sample(actor, view).shadow.caster && cache.floorValid);
        engine.set_field(actor, "unlit", 0);
        if (bonus) {
            engine.set_field(actor, "flags3", 0x420000); engine.set_position(actor, {200, 200, 5});
            assert(sample(actor, view).scalar == 180 && cache.floorValid && !cache.dirty);
            engine.set_field(actor, "flags3", 0);
        }
        engine.set_position(actor, {0, 0, 5}); assert(sample(actor, view).shadow.caster);
        // Instance the retained source World mesh: map sampling requires its actual owning World.
        const auto receiver = state.create(Object::Kind::Entity).id;
        engine.entities.try_emplace(receiver); engine.set_position(receiver, {0, 0, 2});
        engine.entities.at(receiver).model = &floor.mesh; engine.entities.at(receiver).type = 4;
        engine.set_field(receiver, "type", 4);
        for (auto axis : {"scale_x", "scale_y", "scale_z"}) engine.set_field(receiver, axis, 1);
        engine.set_position(actor, {1, 0, 5}); light = sample(actor, view);
        close(engine.field(actor, "floor_dist"), bonus ? 5 : 4.96875, .002); assert(light.scalar == 180 && cache.floorValid);
        close(light.shadow.planeZ, bonus ? 2 : 2.03125, .002); close_vector(light.shadow.negativeNormal, {0, 0, -1}, .002);
        engine.set_position(receiver, {0, 0, 5}); engine.set_position(actor, {0, 0, 5});
        light = sample(actor, view);
        close(engine.field(actor, "floor_dist"), bonus ? 5 : 4.96875, .002); assert(cache.floorValid);
        close(light.shadow.planeZ, bonus ? 2 : 2.03125, .002); // Zero special contact retains the saved positive plane.
        engine.remove_entity(runtime, receiver);
        engine.entities.at(actor).physicsGeometry = false; engine.set_position(actor, {0, 0, 6});
        sample(actor, view); assert(cache.dirty == !bonus); close(engine.field(actor, "floor_dist"), bonus ? 5 : 4.96875, .002);
        engine.entities.at(actor).physicsGeometry = true; engine.set_position(actor, {0, 0, 5});
        assert(sample(actor, view).shadow.caster);
        const auto material = engine.global("mat_shadow").bits;
        engine.set_field(material, "alpha", 29); engine.prepare(runtime); close(engine.frame().shadowAlpha, 29);
        runtime.start("bind_shadow"); engine.prepare(runtime);
        close(engine.frame().shadowAlpha, 37);
        assert(engine.global("mat_shadow").bits == engine.global("authoredShadow").bits);
        engine.set_vector(engine.global_vector("SUN_ANGLE"), {0, 60, 0});
        for (int strength : {0, 100}) {
            numeric("SUN_LIGHT", strength); engine.prepare(runtime);
            close(engine.frame().shadowSun.x, .21650635); close(engine.frame().shadowSun.y, 0);
        }
        engine.set_vector(engine.global_vector("SUN_ANGLE"), {});
        engine.prepare(runtime); close(engine.frame().shadowSun.x, 75);
        engine.set_vector(engine.global_vector("SUN_ANGLE"), {0, -45, 0});
        engine.prepare(runtime); close(engine.frame().shadowSun.x, 53.03301);
        numeric("SUN_LIGHT", 0);
        engine.set_field(cameraId, "flags", 0x400010u | (bonus ? 0x10000u : 0x80000u));
        engine.set_field(cameraId, "visible", 1); // Whole FLAGS assignment also cleared VISIBLE0x4000.
        engine.set_field(cameraId, "alpha", 50); engine.prepare(runtime);
        assert(engine.frame().cameras.front().opacityByte == 127);
        engine.set_field(cameraId, "alpha", 100); engine.prepare(runtime);
        assert(engine.frame().cameras.front().opacityByte == 254);
        engine.set_field(cameraId, "flags", 0);
        engine.set_field(cameraId, "visible", 1);
        // Selected animated spans must belong to the actual companion, including its mesh edits.
        for (int next : {0, 7}) {
            engine.set_field(actor, "frame", 6.5); engine.set_field(actor, "next_frame", next);
            engine.prepare(runtime); const auto wrapped = shadow_for(actor, cameraId);
            assert(wrapped && wrapped->frame == 5 && wrapped->nextFrame == 0);
            close(wrapped->interpolation, .5);
            close((wrapped->model->frames[wrapped->frame].positions[0].x +
                   wrapped->model->frames[wrapped->nextFrame].positions[0].x) * .5, 180);
        }
        engine.set_field(actor, "next_frame", 0); // Ordinary mesh edits retain their independent valid-index contract.
        const auto selectedOwner = engine.create_entity(runtime, "animation_1.mdl", {300, 0, 5}, 0);
        auto edit = [&](ObjectId id, int frame, float x) {
            engine.set_field(id, "frame", frame);
            const std::array args{vector_arg({x, 0, 0})}; runtime.start("edit", args, id, 0);
        };
        edit(actor, 1, 333); edit(selectedOwner, 1, 777); edit(selectedOwner, 2, 888);
        engine.set_field(actor, "frame", 1.5); engine.set_field(actor, "next_frame", 2);
        engine.prepare(runtime);
        auto selected = shadow_for(actor, cameraId); assert(selected && selected->model == base->shadowLods[0]);
        assert(selected->frame == 0 && selected->nextFrame == 1); close(selected->interpolation, .5);
        close(selected->positions[0].x, 777); close(selected->nextPositions[0].x, 888);
        close(selected->positions[0].x * .5 + selected->nextPositions[0].x * .5, 832.5);
        close(draw_for(actor).positions[0].x, 333);
        const auto& overrideFrames = engine.meshOverrides.at(selected->model);
        assert(selected->meshId == overrideFrames.at(0).meshId &&
               selected->nextMeshId == overrideFrames.at(1).meshId &&
               selected->meshRevision == overrideFrames.at(0).revision &&
               selected->nextMeshRevision == overrideFrames.at(1).revision);
        engine.remove_entity(runtime, selectedOwner);
        engine.set_field(actor, "invisible", 1); engine.prepare(runtime);
        assert(!draw_for(actor).visible && shadow_for(actor, cameraId));
        engine.set_field(actor, "shadow", 0); engine.prepare(runtime);
        assert(engine.frame().instances.empty() && engine.frame().shadows.empty());
        engine.set_field(actor, "invisible", 0);
        engine.set_field(cameraId, "clip_far", 1000);
        engine.set_vector(engine.global_vector("D3D_LODFACTOR"), {12.5f, 25, 50});
        auto lod = [&](float distance, int expected) {
            engine.set_position(cameraId, {distance, 0, 5}); engine.prepare(runtime);
            assert(engine.entities.at(actor).visibleLod == expected);
            if (engine.field(actor, "shadow")) {
                const auto chosen = shadow_for(actor, cameraId);
                if (expected < 3) assert(chosen && chosen->model == base->shadowLods[expected]);
                else assert(!chosen);
            }
        };
        lod(0, 0); lod(bonus ? 145.f : 126.f, 0);
        lod(bonus ? 151.f : 135.f, 1); // Ordinary visibility warmed LOD before SHADOW was enabled.
        engine.set_field(actor, "shadow", 1); lod(bonus ? 145.f : 126.f, 1);
        if (!bonus) { lod(125, 0); lod(126, 0); lod(135, 1); lod(269, 2); lod(537, 3); }
        else {
            lod(0, 0); engine.set_position(cameraId, {125, 62.5f, 5}); engine.prepare(runtime);
            assert(engine.entities.at(actor).visibleLod == 0); // Exact squared threshold19531.25.
            lod(151, 1); lod(301, 2); lod(601, 3);
        }
        assert(!shadow_for(actor, cameraId)); // Source selector3/offset1 must reject, not fall back.
        lod(0, 0);
        if (bonus) {
            for (int raw : {0, -1, 1023}) {
                engine.set_global("SHADOW_LOD", Value::from_raw(raw)); engine.prepare(runtime);
                const auto chosen = shadow_for(actor, cameraId);
                assert(chosen && chosen->model == base);
            }
            numeric("SHADOW_LOD", 4); engine.prepare(runtime); assert(!shadow_for(actor, cameraId));
            numeric("SHADOW_LOD", 1);
        }
        const auto missing = engine.create_entity(runtime, "missing_0.mdl", {0, 0, 5}, 0);
        engine.set_field(missing, "shadow", 1); engine.prepare(runtime);
        assert(!shadow_for(missing, cameraId)); // Real failed companion loads, no injected fake selector.
        engine.set_position(cameraId, {bonus ? 151.f : 135.f, 0, 5}); engine.prepare(runtime);
        assert(!shadow_for(missing, cameraId)); // Missing ordinary prerequisite must not revive a later source slot.
        engine.remove_entity(runtime, missing);
        lod(bonus ? 151.f : 135.f, 1);
        const auto secondView = state.create(Object::Kind::View).id;
        engine.set_field(secondView, "visible", 1); engine.set_field(secondView, "layer", 1);
        engine.set_field(secondView, "clip_far", 1000); engine.set_field(secondView, "size_x", 320);
        engine.set_field(secondView, "size_y", 240); engine.set_position(secondView, {bonus ? 145.f : 126.f, 0, 5});
        engine.prepare(runtime);
        assert(engine.entities.at(actor).visibleLod == 1);
        const auto firstShadow = shadow_for(actor, cameraId), secondShadow = shadow_for(actor, secondView);
        assert(firstShadow && secondShadow && firstShadow->model == base->shadowLods[1] &&
               secondShadow->model == base->shadowLods[1]);
        state.retire(secondView); lod(0, 0);
        runtime.start("morph", {}, actor, 0);
        assert(engine.frame().instances.empty() && engine.frame().shadows.empty());
        engine.prepare(runtime); assert(shadow_for(actor, cameraId));
        const auto sharer = engine.create_entity(runtime, "animation_0.mdl", {20, 0, 5}, 0);
        engine.remove_entity(runtime, actor);
        assert(engine.frame().instances.empty() && engine.frame().shadows.empty());
        engine.prepare(runtime); assert(draw_for(sharer).model == base);
        engine.remove_entity(runtime, sharer);
        const auto solo = engine.create_entity(runtime, "solo.mdl", {0, 0, 5}, 0);
        engine.set_field(solo, "shadow", 1); engine.prepare(runtime); assert(shadow_for(solo, cameraId));
        runtime.start("purge", {}, solo, 0);
        assert(engine.frame().instances.empty() && engine.frame().shadows.empty());
        assert(!engine.entities.at(solo).model);
        engine.remove_entity(runtime, solo);
        // Ordered persistent slots are consumed by renderer LIGHTRES, not sorted by actor ID.
        numeric("MAX_LIGHTS", 2); numeric("D3D_TRANSFORM", 1);
        engine.set_vector(engine.global_vector("D3D_POINTLIGHTFALLOFF"), {1.5f, 2, 3});
        engine.set_vector(engine.global_vector("D3D_SPOTLIGHTFALLOFF"), {1.5f, 2, 3});
        engine.set_vector(engine.global_vector("D3D_SPOTLIGHTCONE"), {20, 10, 2});
        auto lamp = [&](float x, double range, bool spot = false) {
            const auto id = engine.create_entity(runtime, "animation_0.mdl", {x, 0, 5}, 0);
            engine.set_field(id, "lightrange", range); engine.set_field(id, "flags3", spot ? 2 : 0);
            engine.set_field(id, "red", 128); engine.set_field(id, "green", 64);
            state.member(id, "blue").set(Value::from_raw(-512)); return id;
        };
        auto a = lamp(10, 100), b = lamp(20, 50, true);
        engine.set_field(b, "pan", 90); engine.prepare(runtime);
        assert(engine.frame().lights.size() == 2);
        auto point = engine.frame().lights[0], spot = engine.frame().lights[1];
        assert(point.type == 1 && spot.type == 2 && !point.synthetic && !point.baked);
        close_vector(point.position, {10, 0, 5}); close_vector(spot.position, {20, 0, 5});
        close(point.range, 300); close(spot.range, 250); close(point.intensity, 1);
        close_vector(spot.direction, {0, 1, 0}); close_vector(point.attenuation, {3, .02f, .00015f});
        close_vector(spot.attenuation, {3, .04f, .0006f});
        close(spot.theta, 20); close(spot.phi, 30); close(spot.falloff, 3);
        close(point.color.r, 128.0 / 255); close(point.color.g, 64.0 / 255); close(point.color.b, -1.0 / 255);
        numeric("D3D_MONOCHROME", 1); engine.prepare(runtime);
        const auto mono = engine.frame().lights[0].color;
        close(mono.r, 63.0 / 255); close(mono.g, mono.r); close(mono.b, mono.r);
        numeric("D3D_MONOCHROME", 0);
        engine.set_vector(engine.global_vector("D3D_POINTLIGHTFALLOFF"), {1.5f, 0, 0});
        engine.prepare(runtime); close_vector(engine.frame().lights[0].attenuation, {0, 0, .00015f});
        if (!bonus) {
            numeric("D3D_TRANSFORM", 0); engine.set_field(a, "flags3", 2); engine.prepare(runtime);
            assert(engine.frame().lights[0].type == 1);
        }
        engine.remove_entity(runtime, a);
        auto c = lamp(30, bonus ? 1.0 / 1024 : 100);
        engine.prepare(runtime);
        assert(engine.frame().lights.size() == 2);
        close_vector(engine.frame().lights[0].position, {30, 0, 5});
        close_vector(engine.frame().lights[1].position, {20, 0, 5});
        assert(engine.frame().lights[0].type == 0);
        numeric("D3D_TRANSFORM", 1); engine.set_field(c, "lightrange", 100); engine.prepare(runtime);
        assert(engine.frame().lights[0].type == 1);
        engine.remove_entity(runtime, b); engine.remove_entity(runtime, c);
        a = lamp(40, 100); b = lamp(50, 100); c = lamp(60, 100);
        engine.prepare(runtime);
        assert(engine.frame().lights.size() == 2 && engine.entities.at(c).lightResident);
        close_vector(engine.frame().lights[0].position, {40, 0, 5});
        close_vector(engine.frame().lights[1].position, {bonus ? 50.f : 60.f, 0, 5});
        engine.remove_entity(runtime, a); engine.remove_entity(runtime, b); engine.remove_entity(runtime, c);
        // Different native birth times select the oldest non-directional resident, not the last ID.
        Input idle; idle.dt = 1.0 / 60;
        a = lamp(80, 100); engine.prepare(runtime);
        engine.tick(runtime, idle); engine.finish_frame(runtime);
        b = lamp(90, 100); engine.prepare(runtime);
        engine.tick(runtime, idle); engine.finish_frame(runtime);
        c = lamp(100, 100); engine.prepare(runtime);
        assert(engine.frame().lights.size() == 2);
        close_vector(engine.frame().lights[0].position, {bonus ? 80.f : 100.f, 0, 5});
        close_vector(engine.frame().lights[1].position, {90, 0, 5});
        engine.remove_entity(runtime, a); engine.remove_entity(runtime, b); engine.remove_entity(runtime, c);
        engine.set_vector(engine.global_vector("SUN_ANGLE"), {0, 60, 0}); numeric("SUN_LIGHT", 100);
        engine.set_vector(engine.global_vector("sun_color"),{32,64,128});
        engine.prepare(runtime); assert(engine.frame().lights.size() == 1);
        const auto sun = engine.frame().lights[0];
        assert(sun.synthetic && !sun.baked && sun.type == 3);
        // A6 00437800 aims the D3D directional light away from the sun (toward-light vector = SUN_ANGLE, lit from above).
        close_vector(sun.direction, bonus ? Vec3{.5f, 0, .8660254f} : Vec3{-.5f, 0, -.8660254f}); close(sun.color.r,bonus?128.f/255.f:2.f);
        close(sun.color.g,bonus?64.f/255.f:2.f);close(sun.color.b,bonus?32.f/255.f:2.f);
        close(sun.range,1);close(sun.intensity,1);
        a = lamp(110, 100); engine.prepare(runtime);
        b = lamp(120, 100); engine.prepare(runtime);
        assert(engine.frame().lights.size() == 2 && engine.frame().lights[0].type == 3);
        close_vector(engine.frame().lights[1].position, {bonus ? 110.f : 120.f, 0, 5});
        engine.remove_entity(runtime, a); engine.remove_entity(runtime, b);
        numeric("SUN_LIGHT",-1);engine.prepare(runtime);
        if(bonus)assert(engine.frame().lights.empty());
        else{assert(engine.frame().lights.size()==1);close(engine.frame().lights[0].color.r,-.02);}
        numeric("SUN_LIGHT", 0); engine.prepare(runtime); assert(engine.frame().lights.empty());
        floor.lights.push_back({{70, 0, 5}, {1, 0, 0, 1}, 25});
        engine.prepare(runtime); assert(engine.frame().lights.size() == 1 && engine.frame().lights[0].baked &&
                                        engine.frame().lights[0].synthetic);
        floor.lights.clear();
        engine.world = &unsupported;
        if (bonus) {
            bool rejected = false;
            try { engine.prepare(runtime); }
            catch (const std::runtime_error& error) {
                rejected = std::string_view(error.what()).find("A7 linked-pool") != std::string_view::npos;
                if (!rejected) throw;
            }
            assert(rejected);
            engine.set_global("patch", Value::from_raw(0x2000)); engine.prepare(runtime);
            engine.set_global("patch", {}); engine.world = nullptr; engine.prepare(runtime);
            engine.world = &floor; engine.prepare(runtime);
        } else engine.prepare(runtime);
    }
}
// Optional, real-data SourceAPI integration, not a walked checkpoint or an asset-free CI fixture.
void campaign_persistence(const std::filesystem::path& data) {
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    const auto runtimeDirectory = temporary.path / "runtime";
    std::filesystem::create_directory(runtimeDirectory);
    std::filesystem::permissions(runtimeDirectory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
    SaveEnvironment privateRuntime(runtimeDirectory, "XDG_RUNTIME_DIR");
    std::string phase = "loading unchanged supplied campaign program";
    auto require = [&](bool condition, std::string_view detail) {
        if (!condition) throw std::runtime_error(std::string(detail));
    };
    struct Campaign {
        Assets assets;
        Media media;
        State state;
        Engine engine;
        Runtime runtime;
        explicit Campaign(const std::filesystem::path& root)
            : assets(root), media(assets, false), engine(assets, nullptr, media, state),
              runtime(state, [this](Runtime& vm, ProcessContext& context,
                                   std::string_view name, std::span<const Argument> args) {
                  return engine.builtin(vm, context, name, args);
              }) {
            for (auto archive : {"K22.wrs", "Kaynak.k22", "K22Widescreen.wrs"})
                if (assets.contains(archive)) assets.mount(archive);
            const bool original = !assets.contains("K22Widescreen.wdl");
            if (original && !assets.contains("K22.wdl"))
                throw std::runtime_error("--campaign-data requires the supplied campaign");
            for (auto define : {"defiae_d3d", "defses_en", "defdil_en"})
                state.compilerMacros.emplace(define, "1");
            runtime.load(original ? "K22.wdl" : "K22Widescreen.wdl",
                         [this](std::string_view name) { return assets.text(name); },
                         [this](std::string_view name) { assets.mount(name); });
            engine.initialize_plugins(runtime);
            engine.configure_language(runtime, "en", true);
            if (original && !engine.configure_display(runtime, true, true, 2560, 1440, WindowMode::Windowed))
                throw std::runtime_error("original campaign display graph was not admitted");
            runtime.bootstrap();
        }
        Value& element(std::string_view name, std::size_t index) {
            return state.require(engine.global(name).bits).array.at(index).value;
        }
        void tick(const Input& input) {
            engine.tick(runtime, input);
            engine.finish_frame(runtime);
        }
    };
    Input idle;
    idle.dt = 1.0 / 60;
    auto until = [&](Campaign& game, std::string_view label, int frames, auto ready,
                     const Input& input) {
        phase = label;
        for (int frame = 0; frame < frames && !ready(); ++frame) {
            require(!game.engine.quit, "original program exited before the phase settled");
            game.tick(input);
        }
        require(ready(), "bounded source phase did not settle: level=" +
                std::to_string(game.engine.number("LY_AktifLevelID")) + " target=" +
                std::to_string(game.engine.number("LY_HedefLevel")) + " load=" +
                std::to_string(game.engine.number("oyun_ilk_basladi")));
    };
    auto key = [&](Campaign& game, int scan, std::string_view label, auto ready) {
        phase = label;
        Input pressed = idle;
        pressed.keys.set(scan);
        pressed.pressed.set(scan);
        // A real press/release, not holding a key until an action completes: authored menu
        // entry and cursor branches explicitly wait for release. Emit the pressed edge once.
        for (int frame = 0; frame < 8; ++frame) {
            require(!game.engine.quit, "original program exited during input press");
            game.tick(pressed);
            pressed.pressed.reset(scan);
        }
        game.tick(idle); // Publish release of every key before observing source completion.
        until(game, label, 90, ready, idle);
        for (int frame = 0; frame < 4; ++frame) game.tick(idle);
    };
    auto menuReady = [&](Campaign& game) {
        until(game, "original startup / brightness admission", 1800, [&] {
            return game.engine.number("ParlaklikMenuAcik") != 0 ||
                   bool(game.engine.global("AM_KernelTimer"));
        }, idle);
        if (game.engine.number("ParlaklikMenuAcik") != 0)
            key(game, SDL_SCANCODE_RETURN, "original brightness Enter", [&] {
                return game.engine.number("ParlaklikMenuAcik") == 0;
            });
        until(game, "original main menu", 120, [&] {
            return game.engine.number("DM_Acik") == 1 &&
                   bool(game.engine.global("AM_KernelTimer"));
        }, idle);
        if (!game.runtime.function("dbg_screen_reset")) {
            close_vector(game.engine.vector(game.engine.global_vector("screen_size")), {2560,1440,0});
            close(game.engine.number("GA_Cozunurluk"),2); // Native size >=1280x720: original GA_Cozunurluk>1 (1024) art tier.
            close(game.element("GA_ScaleOranlari",4).real(),2);
            close(game.element("GA_ScaleOranlari",5).real(),2);
            close(game.engine.field(game.engine.global("camera").bits,"size_x"),2560);
            close(game.engine.field(game.engine.global("camera").bits,"size_y"),1440);
        }
    };
    auto bytes = [&](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        require(input.good(), "missing private source-written file " + path.string());
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                         std::istreambuf_iterator<char>());
    };
    const std::array<const char*, 12> headerNames{
        "save_versiyon", "LY_AktifLevelID", "Aktif_Player", "Aktif_Silah", "akt_music_idno",
        "koruyucu_mana", "oyn_yaralanma", "koru_yaralanma", "buyu_no", "lain_sona_erdi",
        "ebru_yaralanma", "OyunZorluk"};
    // Deliberate fixture values, including non-default wounds/spell; music12 is Start6's authored
    // gameplay selection. Music0 is a command consumed as 99 by the original menu music STARTER.
    const std::array<std::uint8_t, 12> header{100,18,0,1,12,211,77,167,3,0,91,1};
    // Source-derived offsets, independent of native output/read-back agreement.
    auto schema = [&](const std::vector<std::uint8_t>& payload, bool picked) {
        require(payload.size() == 2030, "original payload must contain exactly 2030 unsigned bytes");
        const auto mismatch = std::mismatch(header.begin(), header.end(), payload.begin());
        if (mismatch.first != header.end()) {
            const auto offset = std::size_t(mismatch.first - header.begin());
            throw std::runtime_error("payload header byte " + std::to_string(offset) + " (" +
                headerNames[offset] + "): expected " + std::to_string(*mismatch.first) +
                ", got " + std::to_string(*mismatch.second));
        }
        const std::array<std::uint8_t, 4> demir{1,2,3,4}, ebru{4,3,2,1};
        const std::array<std::uint8_t, 4> active = picked
            ? std::array<std::uint8_t, 4>{6,7,8,9} : std::array<std::uint8_t, 4>{2,3,4,5};
        require(std::equal(demir.begin(), demir.end(), payload.begin()+728) &&
                std::equal(ebru.begin(), ebru.end(), payload.begin()+940) &&
                std::equal(active.begin(), active.end(), payload.begin()+1152),
                "reserve ammunition must be four numeric digit bytes, high-to-low");
        require(payload[1569] == 1 && payload[1570] == 1 && payload[1571] == 4,
                "source minute encoding must include elapsed ticks (one day, one hour, four minutes)");
        require(payload[1217] == picked && payload[1465] == 1 &&
                payload[1755] == 1 && payload[2020] == 2 && payload[2029] == 8,
                "unique/autosave/gate/final-array tails missing or out of order");
    };
    try {
        const auto directory = temporary.path / "private/kabus22/_data/saves";
        std::vector<std::uint8_t> first;
        {
            Campaign writer(data);
            menuReady(writer);
            phase = "original reset and deterministic fixture inputs";
            writer.runtime.start("Yeni_Oyun_Kolay");
            // These are isolated fixture inputs, never mutations of a running authored campaign.
            for (std::size_t index = 0; index < header.size(); ++index)
                writer.engine.set_global(headerNames[index], Value::from_int(header[index]));
            for (auto [name, value] : {std::pair{"aktif_oyun_sure",1502},
                    {"aktif_kayit_sayisi",7}, {"aktif_oldurme_sayisi",23}})
                writer.engine.set_global(name, Value::from_int(value));
            writer.engine.set_global("akt_oldtick_sure",
                Value::from_raw(writer.engine.global("total_ticks").raw() - 1920 * 1024));
            for (auto [name, index, value] : {
                    std::tuple{"ari_ani_dizi",9u,2}, {"ari_adet_dizi",29u,3},
                    {"Demir_NS_Nesneler",99u,10}, {"Ebru_NS_Nesneler",99u,11},
                    {"NS_Nesneler",99u,12}, {"Demir_Do_Dokumanlar",49u,2},
                    {"Ebru_Do_Dokumanlar",49u,3}, {"DO_Dokumanlar",49u,4},
                    {"Demir_Kursun_Sayisi",7u,1234}, {"Ebru_Kursun_Sayisi",8u,4321},
                    {"Kursun_Sayisi",8u,2345}, {"S_Kursun_Sayisi",8u,17},
                    {"unic_obje_dizi",1u,0}, {"unic_obje_dizi",249u,1},
                    {"varil_dizi",99u,1}, {"latex_gel_dizi",99u,1},
                    {"OSN_OtoSave_dizi",31u,1}, {"kapi_acikmi",149u,2}})
                writer.element(name, index) = Value::from_int(value);
            for (std::size_t index = 0; index < 9; ++index)
                writer.element("taskoy_esle_dz", index) = Value::from_int(int(index));
            const std::array args{Argument{Value::from_int(1), {}}}; // Checkpoint display name, not world ID.
            writer.runtime.start("LV_LevelKaydeta", args);
            until(writer, "first original payload and delayed index publish", 8, [&] {
                return writer.engine.number("Aktif_SaveNo") == 1 && writer.engine.files.empty();
            }, idle);
            first = bytes(directory / "kayit0.sav");
            schema(first, false);
            const auto firstIndex = bytes(directory / "kysd.sav");
            const auto& labels = writer.state.require(writer.engine.global("YUMT_Buffer").bits).array;
            const auto firstLabel = std::string(writer.engine.string(labels.at(0).value));
            require(firstLabel.starts_with("01)"), "slot zero must display public slot one");
            require(firstLabel.find(writer.engine.string(writer.element("SDN_isim_dizi",
                    2 + std::size_t(writer.engine.number("GA_Dil"))))) != std::string::npos,
                    "index must contain the actual localized checkpoint display name");
            require(std::string(firstIndex.begin(), firstIndex.end()) == "1 1 " + firstLabel + "\n",
                    "index must advertise next slot, count, and original display label");
            writer.element("unic_obje_dizi", 1) = Value::from_int(1);
            writer.element("Kursun_Sayisi", 8) = Value::from_int(6789);
            writer.runtime.start("LV_LevelKaydeta", args);
            until(writer, "second original save slot placement", 8, [&] {
                return writer.engine.number("Aktif_SaveNo") == 2 && writer.engine.files.empty();
            }, idle);
            require(bytes(directory / "kayit0.sav") == first, "second save overwrote the prior slot");
            schema(bytes(directory / "kayit1.sav"), true);
            const auto secondLabel = std::string(writer.engine.string(labels.at(1).value));
            require(secondLabel.starts_with("02)"), "second slot public label must be two");
            const auto index = bytes(directory / "kysd.sav");
            require(std::string(index.begin(), index.end()) ==
                    "2 2 " + firstLabel + "\n" + secondLabel + "\n",
                    "second index placement/count/order changed");
        }
        Campaign reader(data); // Nothing from writer's State/Runtime/Engine survives.
        menuReady(reader);
        require(reader.engine.number("Aktif_SaveNo") == 2 &&
                reader.engine.number("SLG_KayitSayisi") == 2, "startup did not read the private index");
        auto loadMenu = [&] {
            key(reader, SDL_SCANCODE_DOWN, "original main-menu Load selection", [&] {
                return reader.engine.number("AM_Cursor") == 1;
            });
            key(reader, SDL_SCANCODE_RETURN, "original main-menu Enter / Load menu", [&] {
                return bool(reader.engine.global("YUM_KernelTimer"));
            });
            require(reader.engine.number("YUM_Cursor") == 1 &&
                    reader.engine.number("YUM_Kafa") == 0, "load menu did not select the latest index slot");
        };
        auto actor = [&](ObjectId firstBirth = 0) -> ObjectId {
            for (const auto& [id, entity] : reader.engine.entities)
                if (id >= firstBirth && reader.engine.field(id, "object_index") == 159 &&
                    entity.file == "s_key_d2.mdl" && reader.engine.field(id, "skill3") == 1)
                    return id;
            return 0;
        };
        auto load = [&](bool picked) {
            const auto firstBirth = ObjectId(reader.state.handles.size() + 1);
            const auto timer = reader.engine.global("YUM_KernelTimer").bits;
            key(reader, SDL_SCANCODE_RETURN, "original selected-slot Enter acceptance", [&] {
                auto* object = reader.state.get(timer);
                return !object || !object->alive;
            });
            until(reader, "original reset / byte reader / target publication", 120, [&] {
                return reader.engine.number("aktif_oyun_sure") == 1504 &&
                       reader.element("Kursun_Sayisi", 8).integer() == (picked ? 6789 : 2345);
            }, idle);
            require(reader.engine.number("Yukleme_Yapilsin") == 0, "source load-control flag did not clear");
            for (auto index : {2u,3u,5u,6u,7u,8u,9u,10u,11u})
                require(reader.engine.number(headerNames[index]) == header[index],
                        std::string(headerNames[index]) + " not restored by the original byte reader");
            require(reader.engine.number("load_music_idno") == header[4],
                    "original reader must restore stored music into load_music_idno");
            const auto loadClock = reader.engine.global("akt_oldtick_sure").raw();
            require(loadClock > 0 && loadClock <= reader.engine.global("total_ticks").raw() &&
                    reader.engine.global("total_ticks").raw() - loadClock < 120 * 1024,
                    "reader must rebase the elapsed-minute clock to its fresh total_ticks");
            until(reader, "public level_load / genuine Start6 WMB actor births", 300, [&] {
                return reader.engine.world && actor(firstBirth) != 0 &&
                       reader.engine.world == &reader.assets.world("Start6.WMB");
            }, idle);
            const auto keyActor = actor(firstBirth);
            require(reader.engine.callback(keyActor, "action1") == reader.runtime.function("NS_Nesne") &&
                    reader.engine.field(keyActor, "skill1") == 10,
                    "expected genuine Start6 authored inventory consumer");
            reader.tick(idle); // Original queued NS_Nesne consumes the restored unique-object byte.
            require(bool(actor(firstBirth)) == !picked, "restored unique-object state did not control the real authored actor");
            until(reader, "original level-entry / player morph / load completion", 1800, [&] {
                return reader.engine.number("LY_AktifLevelID") == 18 &&
                       reader.engine.number("level_yuklendi_ok") == 1 &&
                       reader.engine.number("player_morph_ok") != 0 &&
                       reader.engine.number("oyun_ilk_basladi") == 0;
            }, idle);
            phase = "restored numeric globals survive genuine world teardown";
            for (auto [name, index, value] : {
                    std::tuple{"ari_ani_dizi",9u,2}, {"ari_adet_dizi",29u,3},
                    {"Demir_NS_Nesneler",99u,10}, {"Ebru_NS_Nesneler",99u,11},
                    {"NS_Nesneler",99u,12}, {"Demir_Do_Dokumanlar",49u,2},
                    {"Ebru_Do_Dokumanlar",49u,3}, {"DO_Dokumanlar",49u,4},
                    {"Demir_Kursun_Sayisi",7u,1234}, {"Ebru_Kursun_Sayisi",8u,4321},
                    {"Kursun_Sayisi",8u,picked ? 6789 : 2345}, {"S_Kursun_Sayisi",8u,17},
                    {"unic_obje_dizi",1u,int(picked)}, {"unic_obje_dizi",249u,1},
                    {"varil_dizi",99u,1}, {"latex_gel_dizi",99u,1},
                    {"OSN_OtoSave_dizi",31u,1}, {"kapi_acikmi",149u,2},
                    {"taskoy_esle_dz",8u,8}})
                require(reader.element(name, index).integer() == value,
                        std::string(name) + "[" + std::to_string(index) + "] not restored/preserved");
            require(reader.engine.number("aktif_oyun_sure") == 1504 &&
                    reader.engine.number("aktif_kayit_sayisi") == 7 &&
                    reader.engine.number("aktif_oldurme_sayisi") == 23,
                    "restored minute/statistic semantics changed during level load");
            require(reader.engine.files.empty(), "original reader leaked its public file handle");
        };
        loadMenu();
        key(reader, SDL_SCANCODE_UP, "original Load cursor selects earlier slot", [&] {
            return reader.engine.number("YUM_Cursor") == 0;
        });
        load(false); // Negative control: same original actor stays present for the unpicked save.
        const auto oldPlayer = reader.engine.global("player").bits;
        require(oldPlayer && reader.state.require(oldPlayer).alive &&
                reader.engine.field(oldPlayer, "event_flags") != 0, "expected live authored player context");
        reader.state.retain_object(oldPlayer);
        // An otherwise-unused declared ENTITY* tests registered-cell cleanup, not a synthetic entity/action.
        reader.engine.set_global("seytan_ent", Value::object(oldPlayer));
        reader.runtime.start("DisMenuAc");
        menuReady(reader);
        loadMenu();
        load(true);
        phase = "world reload clears original actor contexts and typed entity references";
        require(!reader.engine.global("seytan_ent") &&
                !reader.state.require(oldPlayer).alive &&
                !reader.engine.entities.contains(oldPlayer) &&
                reader.state.require(oldPlayer).retained == 1,
                "old typed entity/context/query registration survived real world teardown");
        require(reader.engine.global("player").bits != oldPlayer, "old handle aliased the new authored player");
        reader.state.release_object(oldPlayer);
        require(!reader.state.get(oldPlayer), "released old actor payload was not reclaimed");
        std::cout << "campaign SourceAPI private save/menu/read/world/actor consumer passed\n";
    } catch (const std::exception& error) {
        throw std::runtime_error("campaign SourceAPI [" + phase + "]: " + error.what());
    }
}
// Optional actual-surface check. Menu transitions use original held/released input;
// subsequent presentation producers are deliberately not a campaign playthrough.
void campaign_display(const std::filesystem::path& root,std::string_view backend,const std::filesystem::path& captures,std::string_view phase="all"){
    std::cout<<std::unitbuf;
    if(backend!="opengl"&&backend!="vulkan"&&backend!="vulkan-rt")
        throw std::runtime_error("campaign display backend must be opengl, vulkan or vulkan-rt");
    if(phase!="all"&&phase!="menus"&&phase!="presentation"&&phase!="projection")
        throw std::runtime_error("campaign display phase must be all, menus, presentation or projection");
    const bool menus=phase=="all"||phase=="menus",presentation=phase=="all"||phase=="presentation",projection=phase=="all"||phase=="projection";
    TempDirectory temporary;SaveEnvironment saves(temporary.path/"data");
    const auto privateRuntime=temporary.path/"runtime";std::filesystem::create_directory(privateRuntime);
    std::filesystem::permissions(privateRuntime,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
    SaveEnvironment runtimeEnvironment(privateRuntime,"XDG_RUNTIME_DIR");
    std::filesystem::create_directories(captures);
    Assets assets(root);for(auto archive:{"K22.wrs","Kaynak.k22"})if(assets.contains(archive))assets.mount(archive);
    if(!assets.contains("K22.wdl")||assets.contains("K22Widescreen.wdl"))
        throw std::runtime_error("campaign display requires original ISO-only data");
    RenderOptions options;options.width=1600;options.height=900;options.samples=0;options.vsync=false;
    options.backend=backend=="opengl"?RendererBackend::OpenGL:RendererBackend::Vulkan;options.rayTracing=backend=="vulkan-rt";
    Renderer renderer(options);Media media(assets,false);State state;Engine engine(assets,&renderer,media,state);
    Runtime runtime(state,[&](auto& vm,auto& context,auto name,auto args){return engine.builtin(vm,context,name,args);});
    for(auto define:{"defiae_d3d","defses_en","defdil_en"})state.compilerMacros.emplace(define,"1");
    runtime.load("K22.wdl",[&](std::string_view name){return assets.text(name);},[&](std::string_view name){assets.mount(name);});
    engine.initialize_plugins(runtime);
    assert(engine.configure_display(runtime,true,true,2560,1440,WindowMode::Windowed));
    runtime.bootstrap();
    Input idle;idle.dt=1.0/60;
    auto tick=[&](const Input& input){engine.tick(runtime,input);engine.finish_frame(runtime);if(engine.quit)throw std::runtime_error("original campaign exited during display check");};
    auto until=[&](std::string_view label,int frames,auto ready){
        for(int i=0;i<frames&&!ready();++i)tick(idle);
        if(!ready())throw std::runtime_error("original menu did not settle: "+std::string(label));
    };
    auto key=[&](int scan){Input held=idle;held.keys.set(scan);held.pressed.set(scan);for(int i=0;i<8;++i){tick(held);held.pressed.reset(scan);}for(int i=0;i<8;++i)tick(idle);};
    auto visible=[&](std::string_view name){return engine.field(engine.global(name).bits,"visible")!=0;};
    // Independent22.10 expectations: truncation towardzero, never nearest rounding.
    auto var=[](double value){return double(std::int64_t(value*1024))/1024;};
    auto ratio=[](int extent,int basis){return double(std::int64_t(extent)*1024/basis)/1024;};
    auto capture=[&](std::string_view name){
        engine.prepare(runtime);renderer.draw(engine.frame());renderer.capture((captures/(std::string(name)+".ppm")).string());
        const auto metrics=renderer.metrics();const auto screen=engine.vector(engine.global_vector("screen_size"));
        close(screen.x,metrics.width);close(screen.y,metrics.height);
        close(engine.field(engine.global("camera").bits,"size_x"),screen.x);
        close(engine.field(engine.global("camera").bits,"size_y"),screen.y);
        assert(!engine.frame().ui.empty());
        std::cout<<"original-display "<<name<<" screen="<<screen.x<<'x'<<screen.y<<" ui="<<engine.frame().ui.size()
                 <<" scale="<<state.require(engine.global("GA_ScaleOranlari").bits).array[4].value.real()<<','
                 <<state.require(engine.global("GA_ScaleOranlari").bits).array[5].value.real()<<'\n';
    };
    auto full=[&](std::string_view name){
        const auto id=engine.global(name).bits;const auto bitmap=state.require(id).field("bmap").value;
        assert(bitmap&&visible(name));const auto& image=assets.image(state.require(bitmap.bits).text);
        const auto metrics=renderer.metrics();
        assert(std::any_of(engine.frame().ui.begin(),engine.frame().ui.end(),[&](const auto& command){
            return command.image==&image&&command.rect.x==0&&command.rect.y==0&&
                   std::abs(command.rect.width-metrics.width)<.01&&std::abs(command.rect.height-metrics.height)<.01;
        }));
    };
    auto scrollGlyph=[&](std::string_view name){
        const auto id=engine.global(name).bits;auto& text=state.require(id);const auto& font=state.require(text.field("font").value.bits);
        const auto& bitmap=assets.image(font.text);const int cw=int(engine.field(font.id,"style")),ch=int(engine.field(font.id,"size"));
        assert(cw>0&&ch>0);std::vector<std::string_view> rows;
        for(const auto& slot:text.array)if(slot.value){auto bytes=engine.string(slot.value);for(std::size_t start=0;;){auto end=bytes.find('\n',start);rows.push_back(bytes.substr(start,end==std::string_view::npos?bytes.size()-start:end-start));if(end==std::string_view::npos)break;start=end+1;}}
        const int offset=int(float(engine.field(id,"offset_y"))/float(state.require(engine.global("GA_FontOranlari").bits).array[2].value.real()));
        std::size_t row=std::size_t((offset+ch-1)/ch);while(row<rows.size()&&rows[row].empty())++row;
        const auto glyph=std::find_if(engine.frame().ui.begin(),engine.frame().ui.end(),[&](const auto& command){return command.layer==engine.field(id,"layer")&&command.image&&command.image->width==bitmap.width&&command.image->height==bitmap.height;});
        if(row>=rows.size()||int(row*ch)-offset+ch>=engine.field(id,"size_y")){assert(glyph==engine.frame().ui.end());return;}
        assert(glyph!=engine.frame().ui.end());const float sx=float(engine.field(id,"scale_x")),sy=float(engine.field(id,"scale_y"));
        const int advance=cw-int(engine.field(id,"condensed")!=0)-2*int(engine.field(id,"narrow")!=0);
        const int dx=engine.field(id,"center_x")?-int(rows[row].size())*(advance/2):0;
        close(glyph->rect.x,std::trunc(engine.field(id,"pos_x"))+std::trunc(dx*sx),2);
        close(glyph->rect.y,std::trunc(engine.field(id,"pos_y"))+std::trunc((int(row*ch)-offset)*sy),2);
        const auto byte=static_cast<unsigned char>(rows[row].front());const int columns=bitmap.width/cw;
        close(glyph->uv.x,float((byte%columns)*cw)/bitmap.width,.0001);
        close(glyph->uv.y,float((byte/columns)*ch)/bitmap.height,.0001);
    };
    auto mapProducer=[&](bool produce,std::string_view captureName){
        if(produce){tick(idle);for(int tile:{0,1}){const std::array args{Argument{Value::from_int(tile),{}},Argument{Value::from_int(0),{}}};runtime.start("haritanin_gosterilemsi",args);}
            const std::array door{Argument{Value::from_int(0),{}},Argument{Value::from_int(0),{}}};runtime.start("kapilarin_gosterilemsi",door);
            state.member(engine.global("player_noktasi").bits,"bmap").set(state.require(engine.global("harita_ok_yonleri").bits).array[0].value);
        }
        const std::array group{Argument{Value::from_int(0),{}}};runtime.start("panel_oklari",group);
        for(auto name:{"genel_panel0","genel_panel1","kapi_panel0","player_noktasi","yon_oklari0","yon_oklari1","yon_oklari2","yon_oklari3","grid_fon_pnl","siyah_fon_pnl"})engine.set_field(engine.global(name).bits,"visible",1);
        capture(captureName);full("siyah_fon_pnl");
        for(auto name:{"genel_panel0","genel_panel1","kapi_panel0","player_noktasi","yon_oklari0","yon_oklari1","yon_oklari2","yon_oklari3"}){
            const auto id=engine.global(name).bits;const auto bitmap=state.require(id).field("bmap").value;const auto& image=assets.image(state.require(bitmap.bits).text);
            close(engine.field(id,"scale_x"),1);close(engine.field(id,"scale_y"),1);
            const auto command=std::find_if(engine.frame().ui.begin(),engine.frame().ui.end(),[&](const auto& c){return c.image==&image&&c.layer==engine.field(id,"layer")&&c.rect.x==engine.field(id,"pos_x")&&c.rect.y==engine.field(id,"pos_y");});
            assert(command!=engine.frame().ui.end());close(command->rect.width,image.width);close(command->rect.height,image.height);
        }
        const auto a=engine.global("genel_panel0").bits,b=engine.global("genel_panel1").bits,door=engine.global("kapi_panel0").bits;
        close(engine.field(b,"pos_x")-engine.field(a,"pos_x"),123);close(engine.field(b,"pos_y")-engine.field(a,"pos_y"),176);
        close(engine.field(door,"pos_x")-engine.field(a,"pos_x"),140);close(engine.field(door,"pos_y")-engine.field(a,"pos_y"),176);
        const auto metrics=renderer.metrics();close(engine.field(engine.global("grid_fon_pnl").bits,"scale_x"),ratio(metrics.width,640));close(engine.field(engine.global("grid_fon_pnl").bits,"scale_y"),ratio(metrics.height,480));
        const auto grid=engine.global("grid_fon_pnl").bits;const auto gridBitmap=state.require(grid).field("bmap").value;const auto& gridImage=assets.image(state.require(gridBitmap.bits).text);
        const auto gridCommand=std::find_if(engine.frame().ui.begin(),engine.frame().ui.end(),[&](const auto& c){return c.image==&gridImage&&c.layer==engine.field(grid,"layer");});
        assert(gridCommand!=engine.frame().ui.end());close(gridCommand->rect.width,gridImage.width*ratio(metrics.width,640),.01);close(gridCommand->rect.height,gridImage.height*ratio(metrics.height,480),.01);
    };
    auto mapClose=[&]{runtime.start("genel_panelleri_kapat");runtime.start("altgrup_oklarini_kapat");for(auto name:{"player_noktasi","grid_fon_pnl","siyah_fon_pnl"})engine.set_field(engine.global(name).bits,"visible",0);};
    auto loadGeometry=[&](Engine& producer,Runtime& vm,State& values,Assets& resources,bool allRows){
        producer.prepare(vm);
        const auto screen=producer.vector(producer.global_vector("screen_size"));const float mx=ratio(int(screen.x),1280),my=ratio(int(screen.y),720);
        const auto list=producer.global("YUMT_Liste").bits,cursor=producer.global("YUMP_Cursor").bits;auto& object=values.require(list);
        const auto& font=values.require(object.field("font").value.bits);const auto& atlas=resources.image(font.text);const int cw=int(producer.field(font.id,"style")),ch=int(producer.field(font.id,"size"));
        close(producer.field(list,"scale_y"),mx);close(producer.field(cursor,"scale_y"),mx);assert(ch==18&&object.array.size()==8&&producer.number("YUM_Kafa")==0);
        const auto bitmap=values.require(cursor).field("bmap").value;const auto& image=resources.image(values.require(bitmap.bits).text);
        const auto highlight=std::find_if(producer.frame().ui.begin(),producer.frame().ui.end(),[&](const auto& c){return c.image==&image&&c.layer==producer.field(cursor,"layer");});
        assert(highlight!=producer.frame().ui.end());assert(highlight->rect.y>=0&&highlight->rect.y+highlight->rect.height<screen.y);
        const int selected=int(producer.number("YUM_Cursor"));assert(selected>=0&&selected<8);
        for(int row=allRows?0:selected;row<=(allRows?7:selected);++row){
            const auto label=producer.string(object.array[row].value);assert(label.size()>2&&label[0]=='0'&&label[1]=='1'+row&&label[2]==')');
            const float y=allRows?float(std::trunc(producer.field(list,"pos_y")+row*18*mx)):highlight->rect.y-my;
            const float x=float(std::trunc(producer.field(list,"pos_x"))+std::trunc((cw-2)*mx));
            const auto glyph=std::find_if(producer.frame().ui.begin(),producer.frame().ui.end(),[&](const auto& c){return c.image&&c.image->width==atlas.width&&c.image->height==atlas.height&&c.layer==producer.field(list,"layer")&&std::abs(c.rect.x-x)<.01&&std::abs(c.rect.y-y)<2;});
            assert(glyph!=producer.frame().ui.end());assert(glyph->rect.y>=0&&glyph->rect.y+glyph->rect.height<screen.y);
            const auto byte=static_cast<unsigned char>(label[1]);close(glyph->uv.x,float((byte%(atlas.width/cw))*cw)/atlas.width,.0001);close(glyph->uv.y,float((byte/(atlas.width/cw))*ch)/atlas.height,.0001);
        }
        close(producer.number("Cer_hY2"),screen.y-10*my,.01);
        assert(producer.number("Cer_hY1")>=0&&highlight->rect.y>=producer.number("Cer_hY1")&&highlight->rect.y+highlight->rect.height<=producer.number("Cer_hY2"));
    };
    auto createSaveSlots=[&]{
        for(int slot=0;slot<8;++slot){const std::array checkpoint{Argument{Value::from_int(1),{}}};runtime.start("LV_LevelKaydeta",checkpoint);
            until("source save-slot publication",20,[&]{return engine.number("Aktif_SaveNo")==slot+1&&engine.files.empty();});
            assert(std::filesystem::is_regular_file(engine.saveRoot/("kayit"+std::to_string(slot)+".sav")));
        }
        {std::ifstream index(engine.saveRoot/"kysd.sav",std::ios::binary);std::string bytes(std::istreambuf_iterator<char>(index),{});assert(bytes.starts_with("8 8 "));}
    };
    until("brightness/main",1800,[&]{return engine.number("ParlaklikMenuAcik")!=0||visible("AMP_YeniOyun");});
    if(engine.number("ParlaklikMenuAcik")!=0){if(menus){capture("brightness");full("PMP_Arka");}key(SDL_SCANCODE_RETURN);}
    until("main",120,[&]{return visible("AMP_YeniOyun")&&engine.number("DM_Acik")==1;});
    auto bitmapFile=[&](std::string_view name){const auto bitmap=state.require(engine.global(name).bits).field("bmap").value;std::string file=bitmap?state.require(bitmap.bits).text:std::string();for(auto& c:file)c=char(std::tolower(static_cast<unsigned char>(c)));return file;};
    if(menus){capture("main");full("DMP_Arka");
        // Kabusdismenu.wdl:157-158 GA_Cozunurluk>1 -> Diablo_1024 (kan_1024.tga); eyes at gozsag/gozsol_pos[2] in 1024 space.
        assert(bitmapFile("DMP_Arka")=="kan_1024.tga");const auto w=renderer.metrics().width,h=renderer.metrics().height;
        close(engine.field(engine.global("DMP_GozSag").bits,"pos_x"),662*ratio(w,1024),1);close(engine.field(engine.global("DMP_Gozsol").bits,"pos_y"),603*ratio(h,768),1);}
    const auto initialMetrics=renderer.metrics();
    if(projection&&!menus)createSaveSlots();
    if(menus){
    const auto initialFontX=ratio(initialMetrics.width,1280),initialFontY=ratio(initialMetrics.height,720),initialGameX=ratio(initialMetrics.width,640);
    close(engine.field(engine.global("AMP_YeniOyun").bits,"pos_y"),var(initialMetrics.height-5*32*initialFontY));
    // Genuine panel and original callback producer, proving the same half-open physical
    // rectangle drives draw and dispatch. No script source, assets or progression changes.
    {
        const auto panel=engine.global("AMP_YeniOyun").bits,item=engine.global("EMP_Nesne0").bits;
        assert(state.require(engine.global("EML_Paneller").bits).array[7].value.bits==item);
        runtime.start("EM_Ayarla");engine.prepare(runtime);close(engine.field(item,"pos_x"),var(270*initialGameX));
        auto callback=state.member(panel,"on_click");const auto prior=callback.get();
        callback.set(Value::function(runtime.function("EM_Ayarla")));
        engine.prepare(runtime);
        const auto packet=std::find_if(engine.frame().ui.begin(),engine.frame().ui.end(),[&](const auto& command){return command.image&&command.layer==engine.field(panel,"layer")&&command.rect.x==engine.field(panel,"pos_x")&&command.rect.y==engine.field(panel,"pos_y");});
        assert(packet!=engine.frame().ui.end());const Rect rectangle=packet->rect;
        Input click=idle;click.mouseY=rectangle.y+1;click.mouseX=rectangle.x+rectangle.width;click.mousePressed.set(SDL_BUTTON_LEFT);
        engine.set_field(item,"pos_x",137);tick(click);close(engine.field(item,"pos_x"),137);
        click.mouseX-=.25f;engine.set_field(item,"pos_x",137);tick(click);
        close(engine.field(item,"pos_x"),var(270*initialGameX));
        callback.set(prior);tick(idle);
    }
    key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_DOWN);assert(engine.number("AM_Cursor")==2);
    key(SDL_SCANCODE_RETURN);until("settings",90,[&]{return visible("AYMP_Grafik");});capture("settings");
    key(SDL_SCANCODE_RETURN);until("graphics",90,[&]{return visible("GMP_CozunurlukA");});
    key(SDL_SCANCODE_RIGHT);assert(engine.number("GM_CursorCozunurluk")==2);capture("graphics-native-resolution");
    {
        const auto row=engine.global("GMP_CozunurlukA").bits;const float x=float(engine.field(row,"pos_x")+5*initialFontX),y=float(engine.field(row,"pos_y")+9*initialFontY);
        const auto label=std::to_string(initialMetrics.width)+"x"+std::to_string(initialMetrics.height);
        const auto glyphs=std::count_if(engine.frame().ui.begin(),engine.frame().ui.end(),[&](const auto& command){
            return command.image&&command.image->width==192&&command.image->height==72&&
                   command.rect.y==y&&command.rect.x>=x&&command.rect.x<x+label.size()*6*initialFontX;
        });
        assert(glyphs==std::ptrdiff_t(label.size())); // Original typed font cells spell the actual drawable dimensions.
    }
    key(SDL_SCANCODE_ESCAPE);until("graphics apply",90,[&]{return visible("AYMP_Grafik");});
    close_vector(engine.vector(engine.global_vector("screen_size")),{float(initialMetrics.width),float(initialMetrics.height),0});capture("graphics-applied");
    key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RETURN);until("sound",90,[&]{return visible("SMP_Efekt");});capture("sound");
    key(SDL_SCANCODE_ESCAPE);until("settings after sound",90,[&]{return visible("AYMP_Grafik");});
    key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RETURN);until("control selection",90,[&]{return visible("KSMP_Klavye");});capture("control-selection");
    key(SDL_SCANCODE_RETURN);until("keyboard controls",90,[&]{return visible("KONMP_Ileri");});capture("keyboard-controls");
    key(SDL_SCANCODE_ESCAPE);until("control selection after keyboard",90,[&]{return visible("KSMP_Klavye");});
    key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RETURN);until("joystick controls",90,[&]{return visible("JKONMP_Ileri");});capture("joystick-controls");
    key(SDL_SCANCODE_ESCAPE);until("control selection after joystick",90,[&]{return visible("KSMP_Klavye");});
    key(SDL_SCANCODE_ESCAPE);until("settings after controls",90,[&]{return visible("AYMP_Grafik");});
    key(SDL_SCANCODE_ESCAPE);until("main after settings",90,[&]{return visible("AMP_YeniOyun");});
    createSaveSlots();
    key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RETURN);
    until("credits",90,[&]{return visible("CRMT_Credit");});capture("credits");close(engine.field(engine.global("CRMT_Credit").bits,"size_y"),290);
    {
        const auto credit=engine.global("CRMT_Credit").bits;const auto factor=state.require(engine.global("GA_FontOranlari").bits).array[2].value.real();
        const double threshold=(engine.number("YeniDil_No")==0?3980:2740)*factor;
        // GA_Kosma is original DEFINE6, not a runtime global. Read its live configured
        // binding rather than querying the nonexistent VAR or assuming a host key.
        const auto& bindings=state.require(engine.global("GA_Tuslar").bits).array;const int scan=legacy_scancode(bindings.at(6).value.integer());
        assert(scan>=0&&scan<SDL_NUM_SCANCODES);Input run=idle;run.keys.set(scan);run.pressed.set(scan);
        double previous=engine.field(credit,"offset_y"),largest=previous;bool reset=false;int samples=0;
        for(int i=0;i<5000&&!reset;++i){tick(run);run.pressed.reset(scan);const double now=engine.field(credit,"offset_y");largest=std::max(largest,now);
            if(now<previous){assert(now==0&&previous>threshold-8*factor);reset=true;capture("credits-full-cycle-reset");scrollGlyph("CRMT_Credit");}
            else if(i%400==0){engine.prepare(runtime);scrollGlyph("CRMT_Credit");++samples;}
            assert(now<=threshold);previous=now;
        }
        const auto runKey=engine.number("KEY_KOSMA");
        std::cout<<"original credits source cycle reset="<<reset<<" threshold="<<threshold<<" largest="<<largest<<" samples="<<samples<<" runKey="<<runKey<<" scancode="<<scan<<'\n';
        if(!reset||largest<=threshold*.9||runKey!=1)std::cerr<<"original credits failure: reset="<<reset<<" samples="<<samples<<" largest="<<largest<<" threshold="<<threshold<<" KEY_KOSMA="<<runKey<<'\n';
        assert(reset);assert(largest>threshold*.9);assert(runKey==1);
        tick(idle);
    }
    key(SDL_SCANCODE_ESCAPE);until("main after credits",90,[&]{return visible("AMP_YeniOyun");});close(engine.field(engine.global("CRMT_Credit").bits,"offset_y"),0);
    key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RETURN);until("load list",90,[&]{return visible("YUMT_Liste");});capture("load-list");
    close(engine.field(engine.global("YUMT_Liste").bits,"scale_y"),initialFontX);close(engine.field(engine.global("YUMP_Cursor").bits,"scale_y"),initialFontX);
    assert(engine.number("YUM_Cursor")==7);loadGeometry(engine,runtime,state,assets,true);
    for(int row=6;row>=0;--row){key(SDL_SCANCODE_UP);assert(engine.number("YUM_Cursor")==row);loadGeometry(engine,runtime,state,assets,true);}
    for(int row=1;row<8;++row){key(SDL_SCANCODE_DOWN);assert(engine.number("YUM_Cursor")==row);loadGeometry(engine,runtime,state,assets,true);}
    capture("load-list-eight-genuine-slots");
    {
        const auto list=engine.global("YUMT_Liste").bits;const auto& font=state.require(state.require(list).field("font").value.bits);const auto& atlas=assets.image(font.text);
        std::vector<UICommand> glyphs;for(const auto& command:engine.frame().ui)if(command.layer==engine.field(list,"layer")&&command.image&&command.image->width==atlas.width&&command.image->height==atlas.height)glyphs.push_back(command);
        assert(!glyphs.empty());const auto& first=glyphs.front();
        close(engine.field(list,"alpha"),50);close(first.color.a,.5);assert(first.dynamicImage);
        std::cout<<"load-font font="<<font.text<<" glyphs="<<glyphs.size()<<" sourceAlpha="<<engine.field(list,"alpha")<<" opacity="<<first.color.a<<'\n';
        struct CaptureRGB {int width{},height{};std::vector<unsigned char> pixels;};
        auto readCapture=[&](std::string_view name){CaptureRGB result;std::ifstream file(captures/(std::string(name)+".ppm"),std::ios::binary);std::string magic;int maximum=0;
            file>>magic>>result.width>>result.height>>maximum;assert(magic=="P6"&&maximum==255&&result.width>0&&result.height>0);file.get();
            result.pixels.resize(std::size_t(result.width)*result.height*3);file.read(reinterpret_cast<char*>(result.pixels.data()),std::streamsize(result.pixels.size()));assert(file.gcount()==std::streamsize(result.pixels.size()));return result;};
        auto shown=readCapture("load-list-eight-genuine-slots");engine.set_field(list,"visible",0);capture("load-list-font-hidden");auto hidden=readCapture("load-list-font-hidden");
        assert(shown.width==hidden.width&&shown.height==hidden.height);engine.set_field(list,"visible",1);capture("load-list-font-restored");
        const auto pixels=renderer.metrics();assert(shown.width==pixels.drawableWidth&&shown.height==pixels.drawableHeight);
        std::array<std::size_t,8> nonblack{},rawPass{},changed{},brighter{};
        const float top=first.rect.y,pitch=18*float(initialFontX);
        for(const auto& glyph:glyphs){const int row=int((glyph.rect.y-top)/pitch+.5f);assert(row>=0&&row<8);
            const auto& image=*glyph.image;const int x0=int(glyph.uv.x*image.width+.5f),y0=int(glyph.uv.y*image.height+.5f),cw=int(glyph.uv.width*image.width+.5f),ch=int(glyph.uv.height*image.height+.5f);
            for(int y=0;y<ch;++y)for(int x=0;x<cw;++x){const auto index=(std::size_t(y0+y)*image.width+x0+x)*4;assert(index+3<image.rgba.size());
                if(!(image.rgba[index]||image.rgba[index+1]||image.rgba[index+2]))continue;++nonblack[row];
                if(image.rgba[index+3]/255.0f>=glyph.alphaRef)++rawPass[row];
                const int px=int(std::floor((glyph.rect.x+(x+.5f)*glyph.rect.width/cw)*pixels.scaleX)),py=int(std::floor((glyph.rect.y+(y+.5f)*glyph.rect.height/ch)*pixels.scaleY));
                assert(px>=0&&px<shown.width&&py>=0&&py<shown.height);const auto pixel=(std::size_t(py)*shown.width+px)*3;
                const int before=hidden.pixels[pixel]+hidden.pixels[pixel+1]+hidden.pixels[pixel+2],after=shown.pixels[pixel]+shown.pixels[pixel+1]+shown.pixels[pixel+2];
                if(shown.pixels[pixel]!=hidden.pixels[pixel]||shown.pixels[pixel+1]!=hidden.pixels[pixel+1]||shown.pixels[pixel+2]!=hidden.pixels[pixel+2])++changed[row];if(after>before)++brighter[row];
            }
        }
        for(int row=0;row<8;++row){
            std::cout<<"load-font pixels row="<<row<<" nonblack="<<nonblack[row]<<" rawAlphaPass="<<rawPass[row]<<" changed="<<changed[row]<<" brighter="<<brighter[row]<<'\n';
            assert(nonblack[row]>0&&rawPass[row]>0&&changed[row]>0&&brighter[row]>0);
        }
    }
    key(SDL_SCANCODE_ESCAPE);until("main after load list",90,[&]{return visible("AMP_YeniOyun");});
    key(SDL_SCANCODE_ESCAPE);key(SDL_SCANCODE_RETURN);until("exit",90,[&]{return visible("CMP_Hayir");});for(int i=0;i<90;++i)tick(idle);capture("exit");
    {
        // CikisMenu.wdl:44-47 frame: question top to the authored bottom 480*sy-DM_AltPay == screen_size.y-16; menus draw +16.
        const auto question=engine.global("CMP_Soru").bits,no=engine.global("CMP_Hayir").bits,frame=engine.global("CER_Cerceve").bits;
        close(engine.number("Cer_hY2"),initialMetrics.height-16,.01);assert(engine.number("Cer_hY1")<engine.number("Cer_hY2"));
        close(engine.number("Cer_hY1")+16,engine.field(question,"pos_y"),1);close(engine.number("Cer_hY2")+16,engine.field(no,"pos_y")+32*engine.field(no,"scale_y"),1);
        close(engine.number("Cer_aY2"),engine.number("Cer_hY2"),1);assert(engine.field(frame,"scale_y")>0&&engine.field(frame,"pos_y")<engine.field(question,"pos_y"));
    }
    key(SDL_SCANCODE_ESCAPE);until("exit back",90,[&]{return visible("AMP_YeniOyun");});
    }
    // A real resize while the original menu is live, with no second original menu invocation.
    auto resizedMetrics=initialMetrics;
    if(presentation){mapProducer(true,"producer-map-before-resize");mapClose();}
    if(presentation||projection){renderer.set_mode(1920,1080,WindowMode::Windowed);tick(idle);resizedMetrics=renderer.metrics();}
    if(presentation){
    capture("main-resized");full("DMP_Arka");
    const auto fontX=ratio(resizedMetrics.width,1280),fontY=ratio(resizedMetrics.height,720),gameX=ratio(resizedMetrics.width,640),gameY=ratio(resizedMetrics.height,480);
    close(engine.field(engine.global("AMP_YeniOyun").bits,"pos_y"),var(resizedMetrics.height-5*32*fontY));
    close(engine.number("Cer_hY1")+16,engine.field(engine.global("AMP_YeniOyun").bits,"pos_y"));
    close(engine.number("Cer_hY2")+16,resizedMetrics.height);
    close(engine.number("Cer_hX1"),engine.field(engine.global("AMP_YeniOyun").bits,"pos_x"));
    // Draw original presentation producers without claiming world/actor progression.
    mapProducer(true,"producer-map-after-hidden-resize");mapClose();
    runtime.start("AnaMenuKapat");
    runtime.start("DisMenuKapat");
    assert(engine.number("DM_Acik")==0&&engine.number("goz_panel_ok")==0&&engine.number("CER_Aktif")==0);
    for(auto name:{"AMP_YeniOyun","DMP_Arka","DMP_GozSag","DMP_Gozsol","CER_Cerceve"})assert(!visible(name));
    // Source-owned bitmap/coordinate palettes through the public panel producer API.
    // No dummy player, story flags or world position are introduced to paint a HUD.
    // GA_Cozunurluk>1 branches: PEML1024/PEME1024 and BEP1_* art authored for GA_ScaleOranlari2 = resolution/1024x768.
    const auto artX=ratio(resizedMetrics.width,1024),artY=ratio(resizedMetrics.height,768),menuArtX=ratio(resizedMetrics.width,2048),menuArtY=ratio(resizedMetrics.height,1152);
    close(state.require(engine.global("GA_ScaleOranlari2").bits).array[4].value.real(),menuArtX);
    for(auto [panelName,paletteName,coordinateName,index]:{
            std::tuple{"PEMP_Enerji","PEML1024_Enerji","PEME1024_KoseKorXY",20u},
            std::tuple{"PEMP_Kalp","PEML1024_Kalp","PEMK1024_KoseKorXY",0u}}){
        const auto id=engine.global(panelName).bits;
        const auto& palette=state.require(engine.global(paletteName).bits).array;
        const auto& coordinates=state.require(engine.global(coordinateName).bits).array;
        state.member(id,"bmap").set(palette.at(index).value);
        engine.set_field(id,"pos_x",coordinates[0].value.real()*menuArtX);
        engine.set_field(id,"pos_y",coordinates[1].value.real()*menuArtY);
        engine.set_field(id,"scale_x",menuArtX);engine.set_field(id,"scale_y",menuArtY);engine.set_field(id,"visible",1);
    }
    capture("producer-player-hud");
    close(engine.field(engine.global("PEMP_Enerji").bits,"scale_x"),artX);
    close(engine.field(engine.global("PEMP_Enerji").bits,"scale_y"),artY);
    close(engine.field(engine.global("PEMP_Enerji").bits,"pos_x"),state.require(engine.global("PEME1024_KoseKorXY").bits).array[0].value.real()*artX,1);
    engine.set_field(engine.global("PEMP_Enerji").bits,"visible",0);
    engine.set_field(engine.global("PEMP_Kalp").bits,"visible",0);
    for(auto [panelName,bitmapName]:{std::pair{"BEP_Enerji","BEP1_Enerji"},std::pair{"BEP_Cam","BEP1_Cam"},std::pair{"BEP_Ust","BEP1_Ust"}}){
        const auto id=engine.global(panelName).bits;state.member(id,"bmap").set(engine.global(bitmapName));
        engine.set_field(id,"scale_x",menuArtX);engine.set_field(id,"scale_y",menuArtY);engine.set_field(id,"visible",1);
    }
    const auto bossEnergy=engine.global("BEP_Enerji").bits,bossCam=engine.global("BEP_Cam").bits;
    const auto half=var(menuArtX*.5),quarter=var(menuArtX*.25);
    const auto halfNative=var(float(half)*float(artX)/float(menuArtX)),quarterNative=var(float(quarter)*float(artX)/float(menuArtX));
    engine.set_field(bossEnergy,"scale_x",half);capture("producer-boss-hud");
    close(engine.field(bossEnergy,"scale_x"),halfNative);close(engine.field(bossCam,"scale_x"),artX);
    close(engine.field(bossEnergy,"pos_x"),engine.field(bossCam,"pos_x"));
    engine.prepare(runtime);close(engine.field(bossEnergy,"scale_x"),halfNative);
    engine.set_field(bossEnergy,"scale_x",quarter);engine.prepare(runtime);close(engine.field(bossEnergy,"scale_x"),quarterNative);
    for(auto name:{"BEP_Enerji","BEP_Cam","BEP_Ust"})engine.set_field(engine.global(name).bits,"visible",0);
    runtime.start("EM_Ayarla");runtime.start("EnvanterMenuAc");capture("producer-inventory");
    close(engine.field(engine.global("EMP_Nesne0").bits,"pos_x"),var(270*gameX));
    close(engine.field(engine.global("EMT_Cisim0").bits,"pos_y"),var((47+34)*gameY));
    close(engine.field(engine.global("EMT_Cisim0").bits,"scale_x"),fontX);
    runtime.start("EnvanterMenuKapat");runtime.start("SmartMenuAc");capture("producer-pause");full("STMP_Arka");
    close(engine.field(engine.global("STMP_Devam").bits,"scale_x"),fontX);
    runtime.start("SmartMenuKapat");
    for(auto name:{"Son_yazi","CRMT_Oyunsonu"}){
        const auto id=engine.global(name).bits;auto& object=state.require(id);const auto font=object.field("font").value;
        const std::array old{engine.field(id,"visible"),engine.field(id,"offset_y"),engine.field(id,"pos_x"),engine.field(id,"pos_y"),engine.field(id,"size_y")};
        engine.set_field(id,"visible",1);engine.set_field(id,"pos_x",name==std::string_view("Son_yazi")?25*fontX:resizedMetrics.width/2.0);engine.set_field(id,"pos_y",0);engine.set_field(id,"size_y",resizedMetrics.height);
        for(double offset:{72.0,360.0,1080.0}){engine.set_field(id,"offset_y",offset*fontX);engine.prepare(runtime);scrollGlyph(name);close(engine.field(id,"offset_y"),var(offset*fontX));}
        capture(std::string("producer-scroll-")+name);
        for(auto [field,value]:{std::pair{"visible",old[0]},std::pair{"offset_y",old[1]},std::pair{"pos_x",old[2]},std::pair{"pos_y",old[3]},std::pair{"size_y",old[4]}})engine.set_field(id,field,value);object.field("font").value=font;
    }
    for(auto name:{"kararma_fon_pnl","videokara_fon_pnl","siyah_fon_pnl","LYP_Fade","KBP_Fade"}){
        const auto id=engine.global(name).bits;const bool wasVisible=visible(name);
        engine.set_field(id,"visible",1);capture(std::string("producer-")+std::string(name));full(name);engine.set_field(id,"visible",wasVisible);
    }
    const std::array barArgs{Argument{Value::from_int(1),{}}};runtime.start("acil_syh_ctrl",barArgs);
    for(int i=0;i<20;++i)tick(idle);
    capture("producer-cinematic-bars");
    close(engine.field(engine.global("oy_syh_pan1").bits,"scale_x"),gameX);
    close(engine.field(engine.global("oy_syh_pan2").bits,"scale_y"),gameY);
    }
    // Recovered mode/ASPECT preparation remains shared with source projection; a
    // subviewport is NOT forced to the physical main-camera window.
    if(projection){
        auto& subview=state.create(Object::Kind::View,"display-test-subviewport");
        for(auto [name,value]:{std::pair{"pos_x",73.0},{"pos_y",41.0},{"size_x",700.0},{"size_y",300.0},{"arc",-60.0},{"aspect",-2.0},{"clip_near",1.0},{"clip_far",1000.0},{"visible",1.0}})engine.set_field(subview.id,name,value);
        auto& point=state.create(Object::Kind::Vector,"display-test-project");
        const auto originalMode=engine.currentWindowMode;
        for(auto mode:{WindowMode::Windowed,WindowMode::Borderless,WindowMode::Fullscreen})for(float aspect:{1.0f,2.0f,-2.0f})for(float arc:{60.0f,-60.0f}){
            engine.currentWindowMode=mode;engine.set_field(subview.id,"aspect",aspect);engine.set_field(subview.id,"arc",arc);engine.prepare(runtime);
            auto prepared=std::find_if(engine.frame().cameras.begin(),engine.frame().cameras.end(),[&](const auto& camera){return camera.sourceId==subview.id;});
            assert(prepared!=engine.frame().cameras.end());close(prepared->viewport.width,700);close(prepared->viewport.height,300);
            const float q=mode==WindowMode::Fullscreen?float(resizedMetrics.height)*320/(float(resizedMetrics.width)*240):1;
            close(prepared->pixelAspect,1/(aspect*q));close(prepared->horizontalFov,arc);assert(!prepared->widescreen);
            assert(engine.number("video_screen")==(mode==WindowMode::Fullscreen?1:2));
            engine.set_position(point.id,{100,10,5});const auto expected=Renderer::project({100,10,5},*prepared,renderer.metrics());
            ProcessContext context;const std::array args{Argument{Value::object(point.id),{}},Argument{Value::object(subview.id),{}}};
            engine.builtin(runtime,context,"vec_to_screen",args);close_vector(engine.position(point.id),expected,.001);
        }
        engine.currentWindowMode=originalMode;
        engine.set_field(subview.id,"aspect",0);engine.prepare(runtime);
        const auto zero=std::find_if(engine.frame().cameras.begin(),engine.frame().cameras.end(),[&](const auto& camera){return camera.sourceId==subview.id;});
        assert(zero!=engine.frame().cameras.end());bool invalid=false;try{Renderer::project({100,10,5},*zero,renderer.metrics());}catch(const std::exception&){invalid=true;}assert(invalid);
        engine.set_field(subview.id,"visible",0);
    }
    if(presentation){mapProducer(true,"producer-map-visible-before-resize");renderer.set_mode(initialMetrics.width,initialMetrics.height,WindowMode::Windowed);tick(idle);mapProducer(false,"producer-map-visible-after-resize");mapClose();}
    if(projection){
    assert(!engine.configure_display(runtime,true,false));
    ProcessContext context;const std::array preset{Argument{Value::from_int(6),{}},Argument{Value::from_int(32),{}},Argument{Value::from_int(2),{}}};
    assert(engine.builtin(runtime,context,"video_switch",preset).integer()==6);
    close_vector(engine.vector(engine.global_vector("screen_size")),{640,480,0});
    // Producer/API geometry at dimensions the host window manager need not admit.
    // These use another genuine original program and the eight source-created saves,
    // not a WSI bypass, replacement script, fabricated labels or a played campaign.
    {
        Assets geometryAssets(root);for(auto archive:{"K22.wrs","Kaynak.k22"})if(geometryAssets.contains(archive))geometryAssets.mount(archive);
        Media geometryMedia(geometryAssets,false);State geometryState;Engine geometryEngine(geometryAssets,nullptr,geometryMedia,geometryState);
        Runtime geometryRuntime(geometryState,[&](auto& vm,auto& context,auto name,auto args){return geometryEngine.builtin(vm,context,name,args);});
        for(auto define:{"defiae_d3d","defses_en","defdil_en"})geometryState.compilerMacros.emplace(define,"1");
        geometryRuntime.load("K22.wdl",[&](std::string_view name){return geometryAssets.text(name);},[&](std::string_view name){geometryAssets.mount(name);});
        geometryEngine.initialize_plugins(geometryRuntime);
        assert(geometryEngine.configure_display(geometryRuntime,true,true,1366,768,WindowMode::Windowed));
        close(geometryState.require(geometryEngine.global("GA_FontOranlari").bits).array[2].value.real(),ratio(1366,1280));
        assert(geometryEngine.configure_display(geometryRuntime,true,true,5120,1440,WindowMode::Windowed));geometryRuntime.bootstrap();
        auto geometryTick=[&](const Input& in){geometryEngine.tick(geometryRuntime,in);geometryEngine.finish_frame(geometryRuntime);assert(!geometryEngine.quit);};
        auto geometryVisible=[&](std::string_view name){return geometryEngine.field(geometryEngine.global(name).bits,"visible")!=0;};
        for(int i=0;i<1800&&!geometryVisible("AMP_YeniOyun");++i){geometryTick(idle);if(geometryEngine.number("ParlaklikMenuAcik")){Input enter=idle;enter.keys.set(SDL_SCANCODE_RETURN);enter.pressed.set(SDL_SCANCODE_RETURN);geometryTick(enter);geometryTick(idle);}}
        assert(geometryVisible("AMP_YeniOyun"));geometryRuntime.start("SLG_Yukle");geometryRuntime.start("AnaMenuKapat");geometryRuntime.start("YukleMenuAc");geometryTick(idle);
        assert(geometryVisible("YUMT_Liste")&&geometryEngine.number("YUM_Cursor")==7);
        auto geometryKey=[&](int scan){Input held=idle;held.keys.set(scan);held.pressed.set(scan);for(int i=0;i<8;++i){geometryTick(held);held.pressed.reset(scan);}for(int i=0;i<8;++i)geometryTick(idle);};
        for(int i=0;i<120;++i)geometryTick(idle);loadGeometry(geometryEngine,geometryRuntime,geometryState,geometryAssets,true);
        for(int row=6;row>=0;--row){geometryKey(SDL_SCANCODE_UP);assert(geometryEngine.number("YUM_Cursor")==row);loadGeometry(geometryEngine,geometryRuntime,geometryState,geometryAssets,true);}
        for(int row=1;row<8;++row){geometryKey(SDL_SCANCODE_DOWN);assert(geometryEngine.number("YUM_Cursor")==row);loadGeometry(geometryEngine,geometryRuntime,geometryState,geometryAssets,true);}
        close(geometryEngine.number("Cer_aY1"),geometryEngine.number("Cer_hY1"),.01);close(geometryEngine.number("Cer_aY2"),geometryEngine.number("Cer_hY2"),.01);
        assert(geometryEngine.configure_display(geometryRuntime,true,true,16384,720,WindowMode::Windowed));
        for(int i=0;i<120;++i)geometryTick(idle);loadGeometry(geometryEngine,geometryRuntime,geometryState,geometryAssets,false);
        for(int row=6;row>=0;--row){geometryKey(SDL_SCANCODE_UP);assert(geometryEngine.number("YUM_Cursor")==row);loadGeometry(geometryEngine,geometryRuntime,geometryState,geometryAssets,false);}
        for(int row=1;row<8;++row){geometryKey(SDL_SCANCODE_DOWN);assert(geometryEngine.number("YUM_Cursor")==row);loadGeometry(geometryEngine,geometryRuntime,geometryState,geometryAssets,false);}
        std::cout<<"original load producer/API:5120x1440 all8slots,16384x720 selected0–7 clipped viewport; no WSI/playthrough claim\n";
    }
    }
    std::cout<<"original campaign display phase="<<phase<<" passed; source-menu bootstrap and selected menu/producer/API assertions; no NewGame/playthrough claim\n";
}
std::vector<std::uint8_t> file_bytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);assert(in);return {std::istreambuf_iterator<char>(in),{}};}
void write_bytes(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
std::vector<std::uint8_t> slot_payload(std::uint8_t level){std::vector<std::uint8_t> b(2030,1);b[0]=100;b[1]=level;return b;}
std::string text_of(const std::filesystem::path& path){auto b=file_bytes(path);return {b.begin(),b.end()};}
// Contract F controller.ini + modern mapping, and save atomicity/backups/metadata/import (asset-free).
void input_saves_contracts(){
    std::ostringstream warnings;
    auto config=parse_controller_config("[Controller]\nmode = Modern ; inline comment\n# full comment\ndeadzone=0.25\nsensitivity=9\nvibration=0\nbogus=1\n[Buttons]\nA=12\nrighttrigger=0\ndpup=3\nnosuch=1\nb=13\n[Other]\nx=1\n",warnings);
    assert(config.modern&&config.deadzone==0.25f&&config.sensitivity==1&&!config.vibration);
    assert(config.buttons[SDL_CONTROLLER_BUTTON_A]==12&&config.buttons[ControllerConfig::RightTrigger]==0&&config.buttons[SDL_CONTROLLER_BUTTON_DPAD_UP]==3);
    assert(config.buttons[SDL_CONTROLLER_BUTTON_B]==8&&config.buttons[ControllerConfig::LeftTrigger]==1&&config.buttons[SDL_CONTROLLER_BUTTON_DPAD_DOWN]==0);
    {auto log=warnings.str();assert(std::count(log.begin(),log.end(),'\n')==6);for(auto needle:{"sensitivity","bogus","nosuch","button b","[Other]","outside"})assert(log.find(needle)!=std::string::npos);}
    assert(!parse_controller_config("",warnings).modern&&!parse_controller_config("[Controller]\nmode=modrn\n",warnings).modern);
    {
        TempDirectory home;SaveEnvironment xdg(home.path,"XDG_CONFIG_HOME");std::ostringstream quiet;
        assert(!load_controller_config(quiet).modern&&quiet.str().empty()); // Absent ⇒ original.
        {std::filesystem::create_directories(home.path/"kabus22");std::ofstream(home.path/"kabus22/controller.ini")<<"[Controller]\nmode=modern\nsensitivity=2.5\n";}
        auto loaded=load_controller_config(quiet);assert(loaded.modern&&loaded.sensitivity==2.5f&&loaded.deadzone==0.15f&&loaded.vibration);
    }
    auto zone=radial_deadzone(0.1f,0.1f,0.15f);assert(zone[0]==0&&zone[1]==0);
    zone=radial_deadzone(0.3f,0.4f,0.2f);close(zone[0],0.225);close(zone[1],0.3);
    zone=radial_deadzone(0.6f,0.8f,0.5f);close(zone[0],0.6);close(zone[1],0.8);
    zone=radial_deadzone(0,-1,0);close(zone[1],-1);
    ControllerConfig modern;modern.modern=true;modern.sensitivity=2;
    Input pad;std::bitset<32> held;pad.joystickButtons[0].set(0); // Raw device bits are replaced, not merged.
    auto& b0=pad.controllerButtons[0];auto& a0=pad.controllerAxes[0];
    b0.set(SDL_CONTROLLER_BUTTON_A);a0[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]=0.6f;a0[SDL_CONTROLLER_AXIS_TRIGGERLEFT]=0.4f;
    a0[SDL_CONTROLLER_AXIS_LEFTX]=0.3f;a0[SDL_CONTROLLER_AXIS_LEFTY]=-0.4f;
    pad.controllerButtons[1].set(SDL_CONTROLLER_BUTTON_X);pad.controllerAxes[1][SDL_CONTROLLER_AXIS_LEFTY]=-1; // Pad 1 never leaks into pad 0's mapping.
    auto force=apply_modern_controller(modern,pad,0,held);
    assert(pad.joystickButtons[0]==std::bitset<32>((1u<<6)|(1u<<1))&&pad.joystickPressed[0]==pad.joystickButtons[0]&&pad.joystickReleased[0].none()&&held==pad.joystickButtons[0]);
    assert(force.x==0&&force.y==0);close(force.z,40,0.001); // Standing turn 0.49 < half: in-place turning needs Key_Sag==1 (oyuncu2.wdl:1560), so nothing.
    {Input other=pad;std::bitset<32> h;auto f=apply_modern_controller(modern,other,1,h);assert(other.joystickButtons[0]==std::bitset<32>(1u<<8)&&f.y==2&&f.z==0); // Pad 1 as the legacy pad: X → button 9, forward.
     f=apply_modern_controller(modern,other,-1,h);assert(other.joystickButtons[0].none()&&other.joystickReleased[0]==std::bitset<32>(1u<<8)&&f.x==0&&f.y==0&&f.z==0);} // -1: a pad at rest.
    b0.reset();pad.controllerPressed[0].set(SDL_CONTROLLER_BUTTON_START); // Same-frame tap still produces one edge.
    a0[SDL_CONTROLLER_AXIS_LEFTX]=0;a0[SDL_CONTROLLER_AXIS_LEFTY]=-1;a0[SDL_CONTROLLER_AXIS_RIGHTX]=1;
    force=apply_modern_controller(modern,pad,0,held);
    assert(pad.joystickButtons[0]==std::bitset<32>((1u<<3)|(1u<<1))&&pad.joystickPressed[0]==std::bitset<32>(1u<<3)&&pad.joystickReleased[0]==std::bitset<32>(1u<<6));
    assert(force.y==2&&force.x==2); // Digital forward (Key_Ileri==1); right stick turns, saturated by sensitivity.
    a0[SDL_CONTROLLER_AXIS_LEFTY]=1;a0[SDL_CONTROLLER_AXIS_RIGHTX]=-0.1f;force=apply_modern_controller(modern,pad,0,held);assert(force.y==-2&&force.x==0);
    a0[SDL_CONTROLLER_AXIS_LEFTY]=-1;a0[SDL_CONTROLLER_AXIS_RIGHTX]=0;a0[SDL_CONTROLLER_AXIS_LEFTX]=0.3f;force=apply_modern_controller(modern,pad,0,held);
    close(force.x,2*2*0.3/std::hypot(0.3,1.0),0.0001);assert(force.y==2); // Walking: analog turn (oyuncu2.wdl:820), scaled by sensitivity.
    a0[SDL_CONTROLLER_AXIS_LEFTY]=0;a0[SDL_CONTROLLER_AXIS_LEFTX]=-0.4f;force=apply_modern_controller(modern,pad,0,held);assert(force.x==-2&&force.y==0); // Standing: 0.59 left → full in-place turn.
    assert(pad.pad_held(SDL_CONTROLLER_BUTTON_X)&&pad.pad_pressed(SDL_CONTROLLER_BUTTON_START)&&!pad.pad_pressed(SDL_CONTROLLER_BUTTON_X)&&pad.pad_any());
    DamageRumble damage;assert(damage.update(7,100)==0&&damage.update(7,100)==0);close(damage.update(7,80),0.55);assert(damage.update(7,0)==1&&damage.update(9,50)==0&&damage.update(9,60)==0&&damage.update(0,0)==0);
    TempDirectory temporary;SaveEnvironment data(temporary.path);const auto root=temporary.path/"kabus22";
    {
        Assets assets(temporary.path);Media media(assets,false);State state;Engine engine(assets,nullptr,media,state);
        Runtime runtime(state,[&engine](auto& vm,auto& ctx,auto name,auto args){return engine.builtin(vm,ctx,name,args);});
        runtime.load_text("input-saves.wdl","function main() {}");
        Input frame;frame.controllerAxes[0][SDL_CONTROLLER_AXIS_LEFTX]=0.5f;frame.controllerAxes[0][SDL_CONTROLLER_AXIS_LEFTY]=-0.05f;frame.controllerButtons[0].set(SDL_CONTROLLER_BUTTON_A);frame.joystickButtons[0].set(0);frame.dt=1.0/60;
        frame.controllerAxes[1][SDL_CONTROLLER_AXIS_LEFTX]=-1;frame.controllerAxes[1][SDL_CONTROLLER_AXIS_LEFTY]=-1;frame.controllerButtons[1].set(SDL_CONTROLLER_BUTTON_B); // Pad 1 never reaches the legacy globals.
        engine.tick(runtime,frame);engine.finish_frame(runtime); // Original mode: raw joystick bits and the A6 00422000 axis scaling.
        assert(engine.number("JOY_1")==1&&engine.number("JOY_7")==0);close_vector(engine.vector(engine.global_vector("joy_force")),{1945.0/1024,0,0},1e-6); // trunc((127-32)*1024*0.02f); |-12|<32 dead zone
        {Input stick=frame;stick.controllerAxes[0][SDL_CONTROLLER_AXIS_LEFTY]=-1;stick.controllerAxes[0][SDL_CONTROLLER_AXIS_LEFTX]=-0.4f;stick.joystickAxes[0]={-0.5f,1,0.1f};
         engine.tick(runtime,stick);engine.finish_frame(runtime); // Full forward saturates at 2*JOY_SENSE: Joy_Takip JOY_Ileri=joy_force.y/2=1 walks (Key_Ileri==1).
         close_vector(engine.vector(engine.global_vector("joy_force")),{-1433.0/1024,2,0},1e-6);close_vector(engine.vector(engine.global_vector("joy_raw")),{-128,255,25},1e-6);
         engine.set_global("JOY_SENSE",Value::from_double(0.5));engine.tick(runtime,stick);engine.finish_frame(runtime);
         close_vector(engine.vector(engine.global_vector("joy_force")),{-716.0/1024,1,0},1e-6);engine.set_global("JOY_SENSE",Value::from_int(1));
         stick.controllerConnected={false,true};engine.tick(runtime,stick);engine.finish_frame(runtime);assert(engine.legacyPad==1);close_vector(engine.vector(engine.global_vector("joy_force")),{-2,2,0},1e-6); // Only pad 1 connected: it is the legacy pad (failover).
         stick.controllerConnected={true,true};engine.tick(runtime,stick);engine.finish_frame(runtime);assert(engine.legacyPad==0); // Both: the lowest slot.
         stick.controllerConnected={false,true};engine.coop.active=true;engine.tick(runtime,stick);engine.finish_frame(runtime);assert(engine.legacyPad==-1&&engine.coop_pad(1)==1); // Coop, one pad: it is P2's, P1 has none.
         close_vector(engine.vector(engine.global_vector("joy_force")),{0,0,0},1e-6);engine.coop={};}
        engine.controller=modern;engine.tick(runtime,frame);engine.finish_frame(runtime);
        assert(engine.number("JOY_1")==0&&engine.number("JOY_7")==1);close_vector(engine.vector(engine.global_vector("joy_force")),{2,0,0},0.01); // Standing turn 0.82 ≥ half: full in-place turn.
        const auto player=engine.create_entity(runtime,"",{0,0,0},0);engine.set_global("player",Value::object(player));engine.set_field(player,"skill1",100);
        engine.tick(runtime,frame);engine.finish_frame(runtime);assert(engine.rumble[0]==0&&engine.rumble[1]==0);
        engine.set_field(player,"skill1",70);engine.tick(runtime,frame);engine.finish_frame(runtime);close(engine.rumble[0],0.65,0.001);assert(engine.rumble[1]==0);
        // Pad 1 rumbles for its own entity (rumbleEntity); the global player stays on the legacy pad.
        const auto second=engine.create_entity(runtime,"",{0,0,0},0);engine.set_field(second,"skill1",100);engine.rumbleEntity[1]=second;engine.tick(runtime,frame);engine.finish_frame(runtime);
        engine.set_field(second,"skill1",90);engine.tick(runtime,frame);engine.finish_frame(runtime);assert(engine.rumble[0]==0);close(engine.rumble[1],0.45,0.001);engine.rumbleEntity[1]=0;
        engine.controller.vibration=false;engine.set_field(player,"skill1",40);engine.tick(runtime,frame);engine.finish_frame(runtime);assert(engine.rumble[0]==0);
        // Source-written save path: backup rotation, atomic replacement and native metadata sidecar.
        ProcessContext context;
        auto save=[&](std::string_view name,std::uint8_t level){
            const std::array open{Argument{engine.new_string(std::string(name)),{}}};auto handle=engine.builtin(runtime,context,"file_open_write",open);assert(handle.integer()>0);
            for(auto byte:slot_payload(level)){const std::array write{Argument{handle,{}},Argument{Value::from_int(byte),{}}};engine.builtin(runtime,context,"file_asc_write",write);}
            assert(file_bytes(root/name)!=slot_payload(level)||level==1); // Nothing is truncated or replaced before file_close.
            const std::array closing{Argument{handle,{}}};engine.builtin(runtime,context,"file_close",closing);assert(file_bytes(root/name)==slot_payload(level));
        };
        write_bytes(root/"kayit3.sav",slot_payload(1));
        for(std::uint8_t level=2;level<=6;++level)save("kayit3.sav",level);
        for(int n=1;n<=3;++n)assert(file_bytes(root/("backups/kayit3.sav."+std::to_string(n)))==slot_payload(std::uint8_t(6-n)));
        assert(!std::filesystem::exists(root/"backups/kayit3.sav.4"));
        for(const auto& entry:std::filesystem::directory_iterator(root))assert(entry.path().filename().string().find(".tmp.")==std::string::npos);
        auto meta=saves::parse_meta(text_of(root/"kayit3.sav.meta"));assert(meta&&meta->format==saves::MetaFormat&&meta->engine==K22_VERSION&&meta->origin=="native"&&meta->entry=="input-saves.wdl"&&meta->written.size()==20);
        // Migration hook: an original save without sidecar (format 0) gains a legacy v1 sidecar when read.
        write_bytes(root/"kayit5.sav",slot_payload(9));
        const std::array read{Argument{engine.new_string("kayit5.sav"),{}}};auto handle=engine.builtin(runtime,context,"file_open_read",read);
        const std::array closing{Argument{handle,{}}};engine.builtin(runtime,context,"file_close",closing);
        meta=saves::parse_meta(text_of(root/"kayit5.sav.meta"));assert(meta&&meta->format==1&&meta->origin=="legacy"&&meta->entry.empty()&&meta->script.empty());
        assert(!std::filesystem::exists(root/"backups/kayit5.sav.1")&&file_bytes(root/"kayit5.sav")==slot_payload(9)); // Reading never rewrites the payload.
        // Native quicksave (docs/CONSOLE.md): only a slot written while quickSaving carries the player position; reading it arms the
        // restore, which applies once LevelYukle has cleared and set level_yuklendi_ok again for that level, until the player has control.
        assert(text_of(root/"kayit3.sav.meta").find("position=")==std::string::npos);
        engine.set_global("LY_AktifLevelID",Value::from_int(37));engine.set_position(player,{10.5f,-20,3});engine.set_field(player,"pan",90);
        write_bytes(root/"kayit4.sav",slot_payload(1));engine.quickSaving=true;save("kayit4.sav",2);engine.quickSaving=false;
        meta=saves::parse_meta(text_of(root/"kayit4.sav.meta"));assert(meta&&meta->position=="37 10.5 -20 3 90"&&meta->origin=="native");
        assert(saves::parse_meta(saves::format_meta(*meta))->position==meta->position);
        auto load=[&](std::string_view name){const std::array open{Argument{engine.new_string(std::string(name)),{}}};auto h=engine.builtin(runtime,context,"file_open_read",open);const std::array closing{Argument{h,{}}};engine.builtin(runtime,context,"file_close",closing);};
        load("kayit4.sav");assert(engine.positionRestore&&engine.positionRestore->level==37&&engine.positionRestore->position.x==10.5f&&engine.positionRestore->pan==90);
        load("kayit5.sav");assert(!engine.positionRestore); // a script save point keeps the original G00 spawn
        load("kayit4.sav");engine.set_position(player,{0,0,0});engine.set_global("oyuncu_pasif",Value::from_int(1));
        engine.tick(runtime,frame);engine.finish_frame(runtime);assert(engine.positionRestore&&engine.positionRestore->started); // level_yuklendi_ok 0: the load has begun
        engine.set_global("level_yuklendi_ok",Value::from_int(1));engine.tick(runtime,frame);engine.finish_frame(runtime);
        close_vector(engine.position(player),{10.5f,-20,3},1e-4);assert(engine.positionRestore); // re-applied while the player is passive
        engine.set_global("oyuncu_pasif",Value{});engine.tick(runtime,frame);engine.finish_frame(runtime);assert(!engine.positionRestore&&engine.field(player,"pan")==90);
    }
    std::ostringstream migration;const auto future="kabus22-save-meta 2\nengine=9.9.9\n";write_bytes(root/"kayit6.sav.meta",{future,future+std::strlen(future)});
    assert(!saves::migrate_meta(root/"kayit6.sav",{1,"x"},migration)&&text_of(root/"kayit6.sav.meta")==future&&migration.str().find("newer")!=std::string::npos);
    write_bytes(root/"kayit7.sav.meta",{'b','a','d'});assert(!saves::migrate_meta(root/"kayit7.sav",{1,"x"},migration)&&text_of(root/"kayit7.sav.meta")=="bad");
    assert(saves::slot_number("KAYIT98.SAV")==98&&saves::slot_number("kayit0.sav")==0&&saves::slot_number("kayit99.sav")<0&&saves::slot_number("kayit01.sav")<0&&saves::slot_number("kayit.sav")<0&&saves::rotated("KYSD.sav")&&!saves::rotated("options.k22"));
    // Atomicity: a failed write leaves the previous file intact and no partial target.
    {auto target=root/"atomic.bin";write_bytes(target,{1,2,3});auto blocker=target;blocker+=".tmp."+std::to_string(::getpid());std::filesystem::create_directory(blocker);
     bool threw=false;try{const std::vector<std::uint8_t> next{9};saves::write_atomic(target,next);}catch(const std::exception&){threw=true;}assert(threw&&file_bytes(target)==std::vector<std::uint8_t>({1,2,3}));std::filesystem::remove(blocker);}
    // Import: whole validated set or nothing; existing files are rotated into backups first.
    const auto windows=temporary.path/"windows";auto index=[](std::string_view t){return std::vector<std::uint8_t>(t.begin(),t.end());};
    write_bytes(windows/"KAYIT0.SAV",slot_payload(3));write_bytes(windows/"kayit1.sav",slot_payload(4));write_bytes(windows/"Kysd.sav",index("1 2 01)a\n02)b\n"));write_bytes(windows/"ftz.k22",{0,1,2});
    write_bytes(root/"kayit0.sav",slot_payload(77));const auto before=file_bytes(root/"kayit0.sav");
    auto refuse=[&](std::string_view why){bool threw=false;try{saves::import_windows(windows,root,{},{1,"t"});}catch(const std::exception& error){threw=std::string_view(error.what()).find(why)!=std::string_view::npos;if(!threw)std::cerr<<error.what()<<'\n';}assert(threw&&file_bytes(root/"kayit0.sav")==before&&!std::filesystem::exists(root/"kysd.sav"));};
    {auto bad=slot_payload(3);bad[0]=99;write_bytes(windows/"kayit1.sav",bad);refuse("save_versiyon");bad.resize(2029);bad[0]=100;write_bytes(windows/"kayit1.sav",bad);refuse("2030");write_bytes(windows/"kayit1.sav",slot_payload(4));}
    write_bytes(windows/"Kysd.sav",index("1 3 01)a\n02)b\n03)c\n"));refuse("kayit2.sav is missing");write_bytes(windows/"Kysd.sav",index("1 3 01)a\n"));refuse("label count");
    write_bytes(windows/"Kysd.sav",index("1 2 01)a\n02)b\n"));write_bytes(windows/"ftz.k22",{0,30,2});refuse("play-time");write_bytes(windows/"ftz.k22",{0,1,2});
    std::filesystem::rename(windows/"Kysd.sav",windows/"other");refuse("no kysd.sav");std::filesystem::rename(windows/"other",windows/"Kysd.sav");
    auto imported=saves::import_windows(windows,root,{},{1,K22_VERSION,"now","","","import:test"});
    assert(imported.imported.size()==4&&imported.imported[2]=="kysd.sav"&&imported.imported[3]=="ftz.k22"&&imported.backedUp==std::vector<std::string>{"kayit0.sav"});
    assert(file_bytes(root/"kayit0.sav")==slot_payload(3)&&file_bytes(root/"kayit1.sav")==slot_payload(4)&&text_of(root/"kysd.sav")=="1 2 01)a\n02)b\n"&&file_bytes(root/"backups/kayit0.sav.1")==before);
    auto meta=saves::parse_meta(text_of(root/"kayit1.sav.meta"));assert(meta&&meta->origin=="import:test"&&meta->entry=="K22.wdl");
    // A newer K22Widescreen-layout set under _data/saves wins and lands in the requested slot directory.
    write_bytes(windows/"_data/saves/kysd.sav",index("0 1 01)p\n"));write_bytes(windows/"_data/saves/kayit0.sav",slot_payload(5));
    std::filesystem::last_write_time(windows/"_data/saves/kysd.sav",std::filesystem::last_write_time(windows/"Kysd.sav")+std::chrono::seconds(5));
    imported=saves::import_windows(windows,root,"_data/saves",{1,K22_VERSION,"now","","","import:test"});
    assert(imported.source==windows/"_data/saves"&&file_bytes(root/"_data/saves/kayit0.sav")==slot_payload(5)&&saves::parse_meta(text_of(root/"_data/saves/kayit0.sav.meta"))->entry=="K22Widescreen.wdl");
    std::cout<<"controller/save contracts passed\n";
}
// A6 vec_for_vertex 00449dc0 clamps (raw>>10)-1 into the model's vertex range (Ebru weapon asks vertex 169 of 6).
void vertex_index_consumers(){
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    write_animation_model(temporary.path / "three.mdl");
    Assets assets(temporary.path); Media media(assets, false); State state;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&engine](auto& vm, auto& ctx, auto name, auto args) { return engine.builtin(vm, ctx, name, args); });
    runtime.load_text("vertex-index.wdl", "function main() {}");
    const auto actor = engine.create_entity(runtime, "three.mdl", {0, 0, 0}, 0);
    ProcessContext context;
    auto vertex = [&](Value index) {
        Argument out{Value::object(state.create(Object::Kind::Vector).id), {}};engine.set_vector(out, {});
        const std::array args{out, Argument{Value::object(actor), {}}, Argument{index, {}}};
        engine.builtin(runtime, context, "vec_for_vertex", args); return engine.vector(out);
    };
    close_vector(vertex(Value::from_int(169)), vertex(Value::from_int(3)));
    close_vector(vertex(Value::from_int(0)), vertex(Value::from_int(1)));
    close_vector(vertex(Value::from_int(-4)), vertex(Value::from_int(1)));
    close_vector(vertex(Value::from_double(2.9)), vertex(Value::from_int(2)));
}
// Entity skills are contiguous (K22.exe 0043e390 entSkill41..44 / vecSkill41): vec_* on my.skillN touch skillN..N+2, never XYZ.
// kotu_koruyucu.wdl Kotukoru_DurusAcim: vec_set(my.Yrt_Temp,player.x);vec_sub(my.Yrt_Temp,my.x);vec_to_angle(my.Yrt_HedefPan,my.Yrt_Temp).
void skill_vector_consumers(){
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    write_animation_model(temporary.path / "three.mdl");
    Assets assets(temporary.path); Media media(assets, false); State state;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&engine](auto& vm, auto& ctx, auto name, auto args) { return engine.builtin(vm, ctx, name, args); });
    runtime.load_text("skill-vector.wdl", R"(
        var target_pos[3] = 8, 10, 7;
        function main() {}
        function probe() { vec_set(my.Skill33, target_pos); vec_sub(my.Skill33, my.x); vec_to_angle(my.Skill36, my.Skill33); }
    )");
    const auto actor = engine.create_entity(runtime, "three.mdl", {5, 6, 7}, 0);
    runtime.start("probe", {}, actor, 0);
    close_vector(engine.position(actor), {5, 6, 7});
    close(engine.field(actor, "pan"), 0);
    close(engine.field(actor, "skill33"), 3); close(engine.field(actor, "skill34"), 4); close(engine.field(actor, "skill35"), 0);
    close(engine.field(actor, "skill36"), std::atan2(4.0, 3.0) * 180 / M_PI, 0.01); close(engine.field(actor, "skill37"), 0, 0.01);
}
// K22.exe 00404020 -> 0044ee20: vec_to_screen tests only depth and the view rectangle, never the view's VISIBLE flag.
// duma_yaratik waits in `if (vec_to_screen(loc_temp.x,camera)){break;}` (duma_yara.wdl) before it may attack.
void screen_projection_consumers(){
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    Assets assets(temporary.path); Media media(assets, false); State state;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&engine](auto& vm, auto& ctx, auto name, auto args) { return engine.builtin(vm, ctx, name, args); });
    runtime.load_text("screen.wdl", R"(
        var shown_visible = 0; var shown_hidden = 0; var shown_behind = 0;
        function main() {}
        function probe() {
            var loc_temp[3];
            camera.visible = on;
            vec_set(loc_temp.x, camera.x); loc_temp.x += 150; if (vec_to_screen(loc_temp.x, camera)) { shown_visible = 1; }
            camera.visible = off;
            vec_set(loc_temp.x, camera.x); loc_temp.x += 150; if (vec_to_screen(loc_temp.x, camera)) { shown_hidden = 1; }
            vec_set(loc_temp.x, camera.x); loc_temp.x -= 150; if (vec_to_screen(loc_temp.x, camera)) { shown_behind = 1; }
        }
    )");
    runtime.start("probe", {}, 0, 0);
    assert(engine.number("shown_visible") == 1 && engine.number("shown_hidden") == 1 && engine.number("shown_behind") == 0);
}
// A6 00435730 birth/morph bounds (K22.exe ASM, reverse/wave2/GameplayWorld/K22.exe.00435730.asm.txt): SCALEd model box, automatic
// NARROW/FAT from the XY size, and the squared XY box whose excess over +-16 is halved.
void entity_bounds_consumers(){
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    write_animation_model(temporary.path / "three.mdl");
    Assets assets(temporary.path); Media media(assets, false); State state;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&engine](auto& vm, auto& ctx, auto name, auto args) { return engine.builtin(vm, ctx, name, args); });
    runtime.load_text("bounds.wdl", "function main() {}");
    // A6 resource box is frame 0 only (00457d10/00457d70, 00458500 +0x38): 0,0,0 .. 0,10,10; later frames reach x 160 and are ignored.
    const auto actor = engine.create_entity(runtime, "three.mdl", {0, 0, 0}, 0);
    auto expect = [&](std::array<double, 6> bounds, int narrow, int fat) {
        const std::array names{"min_x", "min_y", "min_z", "max_x", "max_y", "max_z"};
        for (std::size_t i = 0; i < 6; ++i) close(engine.field(actor, names[i]), bounds[i]);
        assert(engine.field(actor, "narrow") == narrow && engine.field(actor, "fat") == fat);
    };
    ProcessContext context;
    const std::array morph{Argument{Value::object(actor), {}}, Argument{engine.new_string("three.mdl"), {}}};
    auto remorph = [&](double scale, bool clearHull) {
        for (auto axis : {"scale_x", "scale_y", "scale_z"}) engine.set_field(actor, axis, scale);
        if (clearHull) { engine.set_field(actor, "narrow", 0); engine.set_field(actor, "fat", 0); }
        engine.builtin(runtime, context, "ent_morph", morph);
    };
    expect({0, 5, 0, 5, 10, 10}, 0, 0);              // XY size 10 < 16: no hull; square of the averaged corners.
    remorph(10, false); expect({0, 50, 0, 33, 83, 100}, 0, 1); // SCALEd XY size 100 > 6*16: FAT; square half 50 -> 50-(50-16)/2.
    remorph(2, true); expect({0, 10, 0, 10, 20, 20}, 1, 0);    // XY size 20: NARROW.
    engine.set_global("enable_polycollision", Value::from_int(2));
    remorph(1, true); expect({0, 0, 0, 0, 10, 10}, 1, 1);      // both flags: unmodified frame-0 resource box.
    std::printf("entity bounds contracts passed: A6 00435730 SCALE, automatic NARROW/FAT, squared XY box\n");
}
// A6 00459a90 cuts a "+N" sprite strip into N frame textures. Native keeps one texture; a strip wider than 16384 (the original
// s_alev+61.tga is 18300 px) is laid out as a grid of frame cells with uvScale (1/cols, 1/rows); narrower strips keep one row.
void sprite_strip_consumers(){
    TempDirectory temporary;
    SaveEnvironment saves(temporary.path / "private");
    auto tga = [&](const char* name, std::uint16_t frameWidth, std::uint16_t frames) {
        const std::uint16_t width = std::uint16_t(frameWidth * frames);
        std::vector<std::uint8_t> bytes{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, std::uint8_t(width), std::uint8_t(width >> 8), 2, 0, 32, 0x28};
        for (int y = 0; y < 2; ++y) for (int x = 0; x < width; ++x) bytes.insert(bytes.end(), {0, std::uint8_t(y), std::uint8_t(x / frameWidth), 255});
        write_bytes(temporary.path / name, bytes);
    };
    tga("wide+3.tga", 6000, 3); tga("narrow+3.tga", 4, 3);
    Assets assets(temporary.path); Media media(assets, false); State state;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&engine](auto& vm, auto& ctx, auto name, auto args) { return engine.builtin(vm, ctx, name, args); });
    runtime.load_text("sprites.wdl", "function main() {}");
    auto skin = [&](ObjectId id) -> const Material& { return engine.entities.at(id).model->materials.at(0); };
    auto red = [](const Image& image, std::uint32_t x, std::uint32_t y) { return image.rgba[(std::size_t(y) * image.width + x) * 4]; };
    const auto wide = engine.create_entity(runtime, "wide+3.tga", {}, 0);
    const auto& grid = *skin(wide).texture;
    assert(engine.field(wide, "sprite_frames") == 3 && grid.width == 12000 && grid.height == 4);
    assert(skin(wide).uvScale.x == .5f && skin(wide).uvScale.y == .5f);
    assert(red(grid, 0, 0) == 0 && red(grid, 6000, 1) == 1 && red(grid, 5999, 2) == 2 && red(grid, 6000, 2) == 0 && grid.rgba[(std::size_t(3) * grid.width) * 4 + 1] == 1);
    close(engine.entities.at(wide).model->max.y, 3000); // frame width, not the cell grid
    const auto narrow = engine.create_entity(runtime, "narrow+3.tga", {}, 0);
    assert(skin(narrow).texture->width == 12 && skin(narrow).texture->height == 2 && skin(narrow).uvScale.y == 1.f);
    close(skin(narrow).uvScale.x, 1.0 / 3);
    std::printf("sprite strip contracts passed: A6 00459a90 frame cells, wide strip as a 2x2 grid\n");
}
// A6 00443950: emit() SIZE is screen pixels, (32768/depth*SIZE)>>shift with shift=14-trunc((W-1)/320+.5) (0043fc10);
// effect() SIZE is quants times focal=W/(2tan(ARC/2)) (0044e260). Legacy quants = SIZE*2^(15-shift)/focal.
void legacy_particle_size_consumers(){
    TempDirectory temporary;SaveEnvironment saves(temporary.path/"private");
    Assets assets(temporary.path);Media media(assets,false);State state;Engine engine(assets,nullptr,media,state);
    Runtime runtime(state,[&engine](auto& vm,auto& ctx,auto name,auto args){return engine.builtin(vm,ctx,name,args);});
    runtime.load_text("particles.wdl",R"(
        VAR origin[3];
        function blood(){ if(my_age==0){my_size=300;my_alpha=100;} }
        function smoke(){ my.size=10; }
        function spawn(){ emit(1,origin,blood); effect(smoke,1,origin,nullvector); }
        function main(){}
    )");
    engine.set_field(engine.global("camera").bits,"arc",80);engine.set_global("video_mode",Value::from_int(8));
    Input idle;idle.dt=1.0/60;runtime.start("spawn",{},0,0);
    auto frame=[&]{engine.tick(runtime,idle);engine.finish_frame(runtime);assert(engine.frame().particles.size()==2);return engine.frame().particles;};
    frame();auto particles=frame();
    const double focalTan=2*std::tan(40*std::numbers::pi/180);
    close(particles[0].size,300*16*focalTan/1024,.001); // 1024x768: shift 11 -> 16 px per (SIZE/depth).
    close(particles[1].size,10);                         // effect() SIZE stays in quants.
    engine.set_global("video_mode",Value::from_int(7));particles=frame();
    close(particles[0].size,300*8*focalTan/800,.001);   // 800x600: shift 12 -> 8.
    std::printf("legacy particle size contracts passed: A6 00443950 emit() screen-pixel SIZE, effect() quants\n");
}
// Community patch 1.8.5 behaviours ported as options: load-list joystick (modern controller only) and the
// engine-side first-person camera (--camera first-person); asset-free behavioural fixture, not campaign scripts.
void patch_option_contracts(){
    TempDirectory temporary;SaveEnvironment saves(temporary.path/"private");
    write_animation_model(temporary.path/"player.mdl");
    Assets assets(temporary.path);Media media(assets,false);State state;Engine engine(assets,nullptr,media,state);
    Runtime runtime(state,[&engine](auto& vm,auto& ctx,auto name,auto args){return engine.builtin(vm,ctx,name,args);});
    runtime.load_text("patch-options.wdl",R"(
        TEXT YUMT_Liste { strings=1; }
        VAR GA_JTuslar[6]=1,2,3,4,5,6; VAR JOY_Button[13]; VAR JOY_Ileri; VAR JOY_Geri;
        VAR GA_Tuslar[10]=72,80,77,75,29,57,42,50,35,48;
        VAR Aktif_Player=0; VAR LY_AktifLevelID=7; VAR oyuncu_pasif=0; VAR koruyucu_pasif=1;
        VAR Key_Ileri; VAR Key_SilahKaldir; VAR Key_Kosma; VAR bombacam_salla_s; VAR bombacam_salla_m;
        VIEW Video_Cam {}
        FUNCTION main(){}
    )");
    auto key=[&](int code){return engine.key_state(code).integer()!=0;};
    auto& buttons=state.require(engine.global("JOY_Button").bits).array;const auto list=engine.global("YUMT_Liste").bits;
    engine.set_global("JOY_Ileri",Value::from_int(1));buttons[2].value=Value::from_int(1);
    engine.set_field(list,"visible",1);assert(!key(72)&&!key(28)); // Original controller mode keeps the ISO keyboard-only list.
    engine.controller.modern=true;engine.set_field(list,"visible",0);assert(!key(72)&&!key(28)); // Only while the list is shown.
    engine.set_field(list,"visible",1);assert(key(72)&&!key(80)&&key(28)&&!key(1)); // JOY_Button[GA_JTuslar[1]=2] -> Enter.
    buttons[3].value=Value::from_int(1);assert(key(1)); // JOY_Button[GA_JTuslar[2]=3] -> Esc.
    engine.set_global("JOY_Ileri",{});engine.set_global("JOY_Geri",Value::from_int(1));assert(!key(72)&&key(80));
    engine.controller.modern=false;engine.set_field(list,"visible",0);engine.set_global("JOY_Geri",{});
    // Pad A/B are Enter/Esc only in modern mode; in original mode they are JOY_1/JOY_2 (aim/fire), and Enter while aiming
    // reloads instead of firing (oyuncu2.wdl:2529). The d-pad is the arrow keys in both modes.
    engine.input.controllerButtons[0].set(SDL_CONTROLLER_BUTTON_A);engine.input.controllerButtons[0].set(SDL_CONTROLLER_BUTTON_B);engine.input.controllerButtons[0].set(SDL_CONTROLLER_BUTTON_DPAD_UP);
    engine.input.controllerButtons[1].set(SDL_CONTROLLER_BUTTON_DPAD_DOWN);assert(!key(80)); // Only the legacy pad stands in for keys.
    engine.legacyPad=1;assert(key(80)&&!key(72));engine.legacyPad=-1;assert(!key(80)&&!key(72));engine.legacyPad=0;
    assert(!key(28)&&!key(1)&&key(72));engine.controller.modern=true;assert(key(28)&&key(1)&&key(72));
    engine.controller.modern=false;engine.input=Input{};

    const auto player=engine.create_entity(runtime,"player.mdl",{100,50,10},0);
    engine.set_global("player",Value::object(player));engine.set_field(player,"pan",90);
    const auto camera=engine.global("camera").bits;Input idle;idle.dt=1.0/60;
    auto pose=[&]{engine.set_position(camera,{0,0,500});engine.set_field(camera,"pan",10);engine.set_field(camera,"tilt",-60);engine.set_field(camera,"arc",60);};
    auto fired=[&]{ProcessContext context;const std::array args{Argument{Value::from_int(57),{}}};return engine.builtin(runtime,context,"key_pressed",args).integer()!=0;};
    pose();engine.tick(runtime,idle);assert(!engine.first_person_active());close_vector(engine.position(camera),{0,0,500}); // Default original.
    engine.cameraMode=Engine::CameraMode::FirstPerson;engine.tick(runtime,idle);assert(engine.first_person_active());
    // cam.wdl Demir idle: dist_total 2, yuksek 35, tilt_3rd 5 behind player.pan; ARC 70, roll 0.
    const double planar=std::cos(5*std::numbers::pi/180)*2;
    close_vector(engine.position(camera),{100,float(50-planar),float(45+std::sin(5*std::numbers::pi/180)*planar)},.002);
    close(engine.field(camera,"pan"),90);close(engine.field(camera,"tilt"),0);close(engine.field(camera,"arc"),70);
    engine.set_global("Key_SilahKaldir",Value::from_int(1));engine.tick(runtime,idle);close(engine.field(camera,"tilt"),-5);close(engine.position(camera).z,35+std::sin(5*std::numbers::pi/180)*std::cos(5*std::numbers::pi/180)*3,.002);
    engine.set_global("Key_SilahKaldir",{});
    // A6 00422000 mouse_force: raw=trunc(mickey*(1024*10/time_frame_raw)+prior), y up, clamp +-2*mouse_sense; dt 1/60 -> time_frame raw 273.
    auto forceRaw=[&](int axis){const auto v=engine.vector(engine.global_vector("mouse_force"));return Value::from_double(axis?v.y:v.x).raw();};
    Input look=idle;look.mouseDX=10;look.mouseDY=-5;look.mouseButtons.set(SDL_BUTTON_LEFT);engine.tick(runtime,look);
    assert(engine.global("time_frame").raw()==273&&forceRaw(0)==375&&forceRaw(1)==187);const double turn=8*engine.number("time");
    close(engine.field(player,"pan"),90-turn*375/1024,.002);close(engine.field(camera,"tilt"),-5+turn*187/1024,.002);assert(fired()); // Mouse turns, looks up from aim tilt -5, fires.
    {Input fast=idle;fast.mouseDX=-100;fast.mouseDY=0;Input tock=idle;tock.mouseDX=0;tock.mouseDY=0;
     engine.set_global("mouse_mode",Value::from_int(2));engine.tick(runtime,fast);assert(forceRaw(0)==375&&forceRaw(1)==187); // MOUSE_MODE>=2 keeps the last force.
     engine.set_global("mouse_mode",{});engine.tick(runtime,fast);assert(forceRaw(0)==-2048&&forceRaw(1)==187); // Clamp -2*sense; prior y term still summed.
     engine.tick(runtime,tock);assert(forceRaw(0)==-2048&&forceRaw(1)==0);engine.tick(runtime,tock);assert(forceRaw(0)==0&&forceRaw(1)==0);}
    engine.set_field(player,"pan",90);
    // Yield: freeze_mode / Inzar / Video_Cam restore untouched fields; a script write made meanwhile survives.
    engine.set_global("freeze_mode",Value::from_int(1));engine.tick(runtime,idle);assert(!engine.first_person_active()&&!fired());
    close_vector(engine.position(camera),{0,0,500},.002);close(engine.field(camera,"tilt"),-60);close(engine.field(camera,"arc"),60);
    engine.set_global("freeze_mode",{});engine.tick(runtime,idle);assert(engine.first_person_active());
    engine.set_field(camera,"x",-300);engine.set_global("Aktif_Player",Value::from_int(2));engine.tick(runtime,idle);assert(!engine.first_person_active());
    close(engine.field(camera,"x"),-300);close(engine.field(camera,"z"),500,.002);
    engine.set_global("Aktif_Player",Value::from_int(1));engine.set_field(engine.global("Video_Cam").bits,"visible",1);engine.tick(runtime,idle);assert(!engine.first_person_active());
    engine.set_field(engine.global("Video_Cam").bits,"visible",0);engine.set_global("Key_Ileri",Value::from_int(1));engine.tick(runtime,idle);assert(engine.first_person_active());
    close(engine.position(camera).z,35,.002); // Ebru walking: dist_total 0, yuksek 25.
    std::cout<<"patch option contracts passed: modern load-list joystick, first-person camera pose/yield/mouse\n";
}
}

int main(int argc, char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--input-saves"){input_saves_contracts();patch_option_contracts();entity_bounds_consumers();sprite_strip_consumers();legacy_particle_size_consumers();return 0;}
    if((argc==7||argc==8)&&std::string_view(argv[3])=="--campaign-display"){
        campaign_display(argv[4],argv[5],argv[6],argc==8?std::string_view(argv[7]):"all");return 0;
    }
    if (argc != 3 && !(argc == 5 && std::string_view(argv[3]) == "--campaign-data"))
        throw std::runtime_error("Usage: check_engine FX_DIR HMP [--campaign-data GAME_ROOT | --campaign-display ORIGINAL_GAME_ROOT opengl|vulkan|vulkan-rt CAPTURE_DIR [all|menus|presentation|projection]]");
    k22::Renderer::boundary_check();
    graphics_consumers();
    vertex_index_consumers();
    skill_vector_consumers();
    screen_projection_consumers();
    {
        TempDirectory temporary;
        SaveEnvironment saves(temporary.path);
        Assets assets(temporary.path); Media media(assets, false); State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](auto& vm, auto& ctx, auto name, auto args) {
            return engine.builtin(vm, ctx, name, args);
        });
        runtime.load_text("custom-display.wdl", "function main() {}");
        assert(!engine.configure_display(runtime, true, true, 2560, 1440, WindowMode::Windowed));
        ProcessContext context;
        const std::array preset{Argument{Value::from_int(6),{}},
            Argument{Value::from_int(32),{}},Argument{Value::from_int(2),{}}};
        assert(engine.builtin(runtime, context, "video_switch", preset).integer()==6);
        close_vector(engine.vector(engine.global_vector("screen_size")), {640,480,0});
        // A behavioral graph fixture, not campaign progression or a replacement script.
        const std::string graph=R"(
            VAR GA_Cozunurlukler[10]=640,480,800,600,1024,768,1280,1024,1600,1200;
            VAR GA_ScaleOranlari[10]=1,1,1.25,1.25,1.6,1.6,2,2.133,2.5,2.5;
            VAR GA_ScaleOranlari2[10]=1,1,1.25,1.25,1,1,1.25,1.334,1.563,1.563;
            VAR GA_FontOranlari[4]=1,1.166,1.611,2;
            VAR EM_Kordinat[54]; VAR EML_Paneller[27]; VAR GA_Cozunurluk=0;
            VAR LY_AktifLevelID=0; VAR level_yuklendi_ok=0; VAR game_over_start=0; VAR STM_Acik=0;
            FUNCTION GA_AyarlariYukle(){} FUNCTION Menu_AcilisEkrani(){} FUNCTION AnaMenuAc(){}
            FUNCTION EM_Ayarla(){} FUNCTION GrafikMenuKapat(){} FUNCTION SmartMenuAc(){} FUNCTION main(){}
        )";
        runtime.load_text("display-graph-fixture.wdl",graph);
        assert(!engine.configure_display(runtime,false,true,2560,1440,WindowMode::Windowed));
        assert(engine.configure_display(runtime,true,true,2560,1440,WindowMode::Windowed));
        // Menu GA_ScaleOranlari=(W/1280,H/720); 1024-tier GA_ScaleOranlari2 keeps the authored 640/1024 ratio (W/2048,H/1152)
        // in menus and (W/1024,H/768) in gameplay.
        auto scales=[&](double x,double y,double x2,double y2){
            engine.prepare(runtime);
            for(auto name:{"GA_ScaleOranlari","GA_ScaleOranlari2"}){
                const auto& table=state.require(engine.global(name).bits).array;const bool first=name==std::string_view("GA_ScaleOranlari");
                close(table[4].value.real(),first?x:x2);close(table[5].value.real(),first?y:y2);
                close(table[0].value.real(),1);close(table[1].value.real(),1); // Authored 640 slot stays intact.
            }
            const auto& fonts=state.require(engine.global("GA_FontOranlari").bits).array;
            close(fonts[2].value.real(),2);close(fonts[0].value.real(),1);
            close(engine.number("GA_Cozunurluk"),2);
        };
        scales(2,2,1.25,1.25);
        engine.set_global("LY_AktifLevelID",Value::from_int(18));scales(2,2,1.25,1.25);
        engine.set_global("level_yuklendi_ok",Value::from_int(1));scales(4,3,2.5,1.875);
        engine.set_global("STM_Acik",Value::from_int(1));scales(2,2,1.25,1.25);
        engine.set_global("STM_Acik",{});scales(4,3,2.5,1.875);
        engine.set_global("game_over_start",Value::from_int(1));scales(2,2,1.25,1.25);
        { // options.k22 byte 0 keeps the user's authored GA_Cozunurluk; the native tier stays in memory.
            write_bytes(engine.saveRoot/"options.k22",{1,7});
            auto call=[&](std::string_view fn,Value a,Value b={}){std::vector<Argument> args{Argument{a,{}}};if(fn=="file_asc_write")args.push_back(Argument{b,{}});return engine.builtin(runtime,context,fn,args);};
            auto handle=call("file_open_read",engine.new_string("options.k22"));assert(call("file_asc_read",handle).integer()==1);call("file_close",handle);
            engine.prepare(runtime);close(engine.number("GA_Cozunurluk"),2);
            handle=call("file_open_write",engine.new_string("options.k22"));call("file_asc_write",handle,Value::from_int(int(engine.number("GA_Cozunurluk"))));call("file_asc_write",handle,Value::from_int(9));call("file_close",handle);
            assert(file_bytes(engine.saveRoot/"options.k22")==std::vector<std::uint8_t>({1,9}));close(engine.number("GA_Cozunurluk"),2);
        }
        assert(engine.builtin(runtime,context,"video_switch",preset).integer()==6);
        close_vector(engine.vector(engine.global_vector("screen_size")),{2560,1440,0});
        for(bool prepareFirst:{false,true}){
            runtime.load_text("direct-custom-replacement.wdl","FUNCTION main(){}");
            auto& view=state.create(Object::Kind::View,"withdrawn-native-custom-view");
            for(auto [name,value]:{std::pair{"size_x",700.0},{"size_y",300.0},{"arc",60.0},{"aspect",2.0},{"clip_near",1.0},{"clip_far",1000.0},{"visible",1.0}})engine.set_field(view.id,name,value);
            if(prepareFirst)engine.prepare(runtime);
            assert(engine.builtin(runtime,context,"video_switch",preset).integer()==6);
            close_vector(engine.vector(engine.global_vector("screen_size")),{640,480,0});
            engine.prepare(runtime);
            const auto camera=std::find_if(engine.frame().cameras.begin(),engine.frame().cameras.end(),[&](const auto& c){return c.sourceId==view.id;});
            assert(camera!=engine.frame().cameras.end());close(camera->pixelAspect,2);
            runtime.load_text("display-graph-readmission.wdl",graph);
            assert(engine.configure_display(runtime,true,true,2560,1440,WindowMode::Windowed));
        }
        runtime.load_text("patched-graph-fixture.wdl",graph+" FUNCTION dbg_screen_reset(){}");
        assert(!engine.configure_display(runtime,true,true,2560,1440,WindowMode::Windowed));
        runtime.load_text("modified-display-graph-fixture.wdl",graph);
        state.require(engine.global("GA_FontOranlari").bits).array[3].value=Value::from_double(3);
        assert(!engine.configure_display(runtime,true,true,2560,1440,WindowMode::Windowed));
        runtime.load_text("custom-display-reload.wdl","FUNCTION main(){}");
        assert(engine.builtin(runtime,context,"video_switch",preset).integer()==6);
        close_vector(engine.vector(engine.global_vector("screen_size")),{640,480,0});
    }
    legacy_trace_consumers();
    a6_movement_consumers();
    k22::check_modern_box();
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path);
        k22::Assets assets(temporary.path);
        k22::Media media(assets, false);
        k22::State state;
        k22::Engine engine(assets, nullptr, media, state);
        k22::Runtime runtime(state, [&engine](auto& vm, auto& context, auto name, auto args) {
            return engine.builtin(vm, context, name, args);
        });
        // The original native numeric STR_LEN symbol and str_len(...) occupy distinct namespaces.
        runtime.load_text("native-text-call-namespace.wdl", R"(
            STRING first="slot zero"; STRING second="second distinct slot"; STRING empty="";
            TEXT slots { strings=3; string=first,second,empty; }
            VAR length0,length1,lengthEmpty,bare;
            FUNCTION measure(_word) {
                STR_LEN=_word;
                bare=STR_LEN;
                length0=str_len(slots.string[0]);
                length1=str_len(slots.string[1]);
                lengthEmpty=str_len(slots.string[2]);
            }
        )");
        for (int word : {0,73}) {
            const std::array args{k22::Argument{k22::Value::from_int(word), {}}};
            runtime.start("measure", args);
            assert(engine.global("bare").raw() == k22::Value::from_int(word).raw());
            assert(engine.global("STR_LEN").raw() == k22::Value::from_int(word).raw());
            assert(engine.number("length0") == std::string_view("slot zero").size());
            assert(engine.number("length1") == std::string_view("second distinct slot").size());
            assert(engine.number("lengthEmpty") == 0);
        }
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path);
        {std::ofstream fixture(temporary.path/"text-load.fixture",std::ios::binary);fixture<<"\r\n\tAlpha  \tBeta\n\nGamma$"<<std::string(5000,'x')<<"\nTail\n";}
        {std::ofstream settings(temporary.path/"mode_ayarlar.k22");settings<<"0\n0\n1\n0\n25\n";}
        Assets assets(temporary.path);
        // 004555e0 captures enhanced startup gamma 1.5 once, before any Engine sampler/prepare consumer.
        assets.initialize_native_lightmap_tables(1536, false, true);
        Media media(assets, false);
        State state;
        World world, floor;
        Model model;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& runtime, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(runtime, context, name, args);
        });
        {
            const auto objectCount = state.objects.size();
            ProcessContext context;
            const std::array rawArguments{
                Argument{Value::from_raw(2147483647), {}},
                Argument{Value::from_raw(-12345), {}},
                Argument{Value::from_raw(777), {}}};
            const auto borrowed = engine.builtin(runtime, context, "vector", rawArguments);
            assert(borrowed.bits == objectCount - 62); // Initial cursor is slot0; first write is slot1.
            auto& slot = state.require(borrowed.bits);
            assert(slot.kind == Object::Kind::Vector && slot.array.size() == 3);
            const auto blue = state.member(borrowed.bits, "blue");
            const auto pan = state.member(borrowed.bits, "pan");
            const auto green = state.member(borrowed.bits, "green");
            const auto red = state.member(borrowed.bits, "red");
            assert(blue.slot == pan.slot && blue.slot == &slot.array[0]);
            assert(green.slot == &slot.array[1] && red.slot == &slot.array[2]);
            const std::array nativeArguments{
                Argument{Value::from_raw(std::bit_cast<std::int32_t>(1.2345f)), {}},
                Argument{Value::from_raw(std::bit_cast<std::int32_t>(-2.2345f)), {}},
                Argument{Value::from_raw(std::bit_cast<std::int32_t>(0.0005f)), {}}};
            std::array<ObjectId, 64> ring{};
            ring[0] = borrowed.bits;
            for (std::size_t advance = 1; advance < ring.size(); ++advance) {
                const bool native = advance % 2 != 0;
                const auto value = engine.builtin(runtime, context, native ? "_vec" : "vector",
                                                   native ? nativeArguments : rawArguments);
                ring[advance] = value.bits;
                assert(value.bits != borrowed.bits);
                for (std::size_t prior = 0; prior < advance; ++prior)
                    assert(ring[prior] != value.bits);
                assert(blue.get().raw() == 2147483647 && green.get().raw() == -12345 &&
                       red.get().raw() == 777);
                if (native) {
                    const auto& converted = state.require(value.bits).array;
                    assert(converted[0].value.raw() == 1264);
                    assert(converted[1].value.raw() == -2288);
                    assert(converted[2].value.raw() == 0);
                }
            }
            assert(ring[63] == objectCount - 63); // The 63rd subsequent call wraps to slot0.
            const auto overwritten = engine.builtin(runtime, context, "_vec", nativeArguments);
            assert(overwritten.bits == borrowed.bits);
            assert(blue.slot == &slot.array[0] && blue.get().raw() == 1264);
            assert(pan.get().raw() == 1264 && green.get().raw() == -2288 && red.get().raw() == 0);
            for (std::size_t advance = 1; advance <= 4096; ++advance) {
                const auto value = engine.builtin(runtime, context, "vector", rawArguments);
                assert(value.bits == ring[advance % ring.size()]);
                assert(state.member(value.bits, "pan").get().raw() == 2147483647);
                assert(state.member(value.bits, "green").get().raw() == -12345);
                assert(state.member(value.bits, "red").get().raw() == 777);
            }
            const std::array wrappingArguments{
                Argument{Value::from_raw(std::bit_cast<std::int32_t>(4194304.5f)), {}},
                Argument{Value::from_raw(std::bit_cast<std::int32_t>(-4194304.5f)), {}},
                Argument{Value::from_raw(std::bit_cast<std::int32_t>(-0.0005f)), {}}};
            const auto wrapped = engine.builtin(runtime, context, "_vec", wrappingArguments);
            assert(wrapped.bits == ring[1]);
            assert(state.member(wrapped.bits, "blue").get().raw() == 512);
            assert(state.member(wrapped.bits, "green").get().raw() == -512);
            assert(state.member(wrapped.bits, "red").get().raw() == 0);
            assert(state.objects.size() == objectCount);
            auto& backing = state.create(Object::Kind::Vector);
            backing.array.resize(4);
            backing.array[1].value = wrapped;
            const Argument pointerParameter{wrapped, state.member(backing.id, "y")};
            close_vector(engine.vector(pointerParameter), {0.5f, -0.5f, 0});
            engine.set_vector(pointerParameter, {7, 8, 9});
            assert(backing.array[1].value.kind == Value::Kind::Object &&
                   backing.array[1].value.bits == wrapped.bits);
            assert(state.member(wrapped.bits, "blue").get().raw() == 7 * 1024);
            assert(state.member(wrapped.bits, "green").get().raw() == 8 * 1024);
            assert(state.member(wrapped.bits, "red").get().raw() == 9 * 1024);
            const Argument component{Value{}, state.member(wrapped.bits, "pan")};
            close_vector(engine.vector(component), {7, 8, 9});
            engine.set_vector(component, {4, 5, 6});
            close_vector(engine.vector(pointerParameter), {4, 5, 6});
        }
        {
            auto& text=state.create(Object::Kind::Text);
            text.array.resize(6);engine.set_field(text.id,"strings",6);
            text.array[5].value=engine.new_string("kept");
            Slot* first=&text.array[0];engine.set_global("delimit_str",engine.new_string("$"));
            ProcessContext context;std::array<Argument,2> args{{{Value::object(text.id),{}},{engine.new_string("text-load.fixture"),{}}}};
            assert(engine.builtin(runtime,context,"txt_load",args).integer()==5);
            assert(&text.array[0]==first&&text.array.size()==6);
            assert(engine.string(text.array[0].value)=="Alpha  ");
            assert(engine.string(text.array[1].value)=="Beta");
            assert(engine.string(text.array[2].value)=="Gamma");
            assert(engine.string(text.array[3].value)==std::string(4095,'x'));
            assert(engine.string(text.array[4].value)=="Tail");
            assert(engine.string(text.array[5].value)=="kept");
            engine.set_global("delimit_str",engine.new_string(","));
            state.profile=SemanticProfile::BonusA7;
            const auto privateSettings=engine.saveRoot/"mode_ayarlar.k22";
            std::filesystem::create_directories(privateSettings.parent_path());
            constexpr std::array expectedSettings{"2","2","0","1","50"};
            {std::ofstream out(privateSettings);out<<"2\n2\n0\n1\n50\n";}
            args[1].value=engine.new_string("mode_ayarlar.k22");
            assert(engine.builtin(runtime,context,"txt_load",args).integer()==5);
            for(std::size_t i=0;i<5;++i)assert(engine.string(text.array[i].value)==expectedSettings[i]);
            {std::ofstream out(privateSettings);out<<"malformed\n";}
            assert(engine.builtin(runtime,context,"txt_load",args).integer()==1);
            assert(engine.string(text.array[0].value)=="malformed");
            std::filesystem::remove(privateSettings);
            assert(engine.builtin(runtime,context,"txt_load",args).integer()==5);
            assert(engine.string(text.array[0].value)=="0"&&engine.string(text.array[4].value)=="25");
            state.profile=SemanticProfile::CampaignA6;
        }
        auto vector_arg = [&](Vec3 value) {
            auto id = state.create(Object::Kind::Vector).id;
            Argument argument{Value::object(id), {}};
            engine.set_vector(argument, value);
            return argument;
        };
        auto entity = [&](Vec3 position) {
            const auto id = state.create(Object::Kind::Entity).id;
            engine.entities.try_emplace(id);
            engine.set_position(id, position);
            for (auto field : {"min_x", "min_y", "min_z"}) engine.set_field(id, field, -1);
            for (auto field : {"max_x", "max_y", "max_z"}) engine.set_field(id, field, 1);
            for (auto field : {"scale_x", "scale_y", "scale_z"}) engine.set_field(id, field, 1);
            return id;
        };
        auto trace = [&](ProcessContext& context, Vec3 start, Vec3 end, std::uint32_t mode) {
            engine.set_global("trace_mode", Value::from_int(static_cast<std::int32_t>(mode)));
            const std::array arguments{vector_arg(start), vector_arg(end)};
            return engine.builtin(runtime, context, "trace", arguments);
        };
        auto modern_query = [&](ProcessContext& context, Vec3 start, Vec3 end, std::uint32_t mode) {
            const std::array arguments{vector_arg(start), vector_arg(end), Argument{Value::from_int(mode), {}}};
            return engine.builtin(runtime, context, "c_trace", arguments);
        };

        world = triangle_world({0, 0, 0}, {10, 0, 0}, {0, 10, 0});
        engine.world = &world;
        const auto edge = engine.collide({5, -3, 0.5f}, {5, 3, 0.5f}, {1, 1, 1}, 0, 0, nullptr, true);
        assert(edge.hit && edge.entity == 0 && edge.triangle == 0);
        close(edge.fraction, (3 - std::sqrt(0.75)) / 6);
        close_vector(edge.normal, {0, -float(std::sqrt(0.75)), 0.5f});
        close_vector(edge.position, {5, 0, 0});
        const auto vertex = engine.collide({-3, -3, 0.5f}, {3, 3, 0.5f}, {1, 1, 1}, 0, 0, nullptr, true);
        assert(vertex.hit && vertex.vertex == 1);
        close(vertex.fraction, (3 - std::sqrt(0.375)) / 6);
        close_vector(vertex.normal, {-float(std::sqrt(0.375)), -float(std::sqrt(0.375)), 0.5f});
        close_vector(vertex.position, {0, 0, 0});

        floor = triangle_world({-100, -100, 0}, {100, -100, 0}, {0, 100, 0});
        legacy_floor_fixture(floor);
        engine.world = &floor;
        const auto face = engine.collide({0, 0, 5}, {0, 0, -5}, {1, 1, 1}, 0, 0, nullptr, true);
        assert(face.hit && face.material == &floor.mesh.materials[0]);
        close(face.fraction, 0.4);
        close_vector(face.normal, {0, 0, 1});
        close_vector(face.position, {0, 0, 0});
        engine.collisionNodes.push_back({{-100, -100, 0}, {100, 100, 0}, 0, 1, 0, 0});
        engine.collisionOrder.push_back(0);
        const auto flatLeaf = engine.collide({0, 0, 5}, {0, 0, -5}, {}, 0, 0, nullptr, true);
        assert(flatLeaf.hit && flatLeaf.material == &floor.mesh.materials[0]);
        close(flatLeaf.fraction, 0.5);
        close_vector(flatLeaf.normal, {0, 0, 1});
        engine.collisionNodes.clear();
        engine.collisionOrder.clear();
        floor.collision.flags[0] = 64;
        assert(!engine.collide({0, 0, 5}, {0, 0, -5}, {1, 1, 1}, 4, 0, nullptr, true).hit);
        assert(engine.collide({0, 0, 5}, {0, 0, -5}, {1, 1, 1}, 8, 0, nullptr, true).hit);
        floor.collision.flags[0] = 0;
        for (auto mode : {0u, 16u}) {
            const auto hit = engine.collide({0, 0, 5}, {0, 0, -5}, {1, 1, 1}, mode, 0, nullptr, true);
            assert(hit.hit && hit.entity == 0);
            close(hit.fraction, 0.4);
        }
        const auto probe = entity({0, 0, 5});
        ProcessContext floorContext{0, probe, 0};
        // A6 00472072..0047209d: USE_BOX + MY enables the hull query.
        close(modern_query(floorContext, {0, 0, 5}, {0, 0, -5}, 8192 | 1 | 65536).real(), 3.9501953125);
        assert(floorContext.you == 0 && engine.number("trace_hit") == 1);
        assert(engine.number("tex_flag1") == 0 && engine.number("tex_flag3") == 1);
        close_vector(engine.vector(engine.global_vector("target")), {0, 0, .0498046875f}, 0.002);
        close_vector(engine.vector(engine.global_vector("normal")), {0, 0, 1}, 0.002);
        // A6 00470a50 rotates MIN + span/2; SCALE is not read by the hull builder.
        engine.set_field(probe, "min_z", -3);
        engine.set_field(probe, "max_z", -1);
        engine.set_field(probe, "scale_z", 7);
        close(modern_query(floorContext, {0, 0, 5}, {0, 0, -5}, 8192 | 1 | 65536).real(), 1.9501953125);
        close_vector(engine.vector(engine.global_vector("target")), {0, 0, .0498046875f}, 0.002);
        engine.set_field(probe, "min_z", -1);
        engine.set_field(probe, "max_z", 1);
        engine.set_field(probe, "scale_z", 1);
        for (auto mode : {16u, 128u, 16u | 128u}) {
            close(trace(floorContext, {0, 0, 5}, {0, 0, -5}, mode | 1).real(), 4.96875);
            assert(floorContext.you == 0 && engine.number("trace_hit") == 1);
        }
        engine.entities.clear();

        engine.world = nullptr;
        const auto my = entity({30, 0, 0});
        auto cylinder = engine.collide({26, 0.9f, 0}, {34, 0.9f, 0}, {}, 0, 0, nullptr, true);
        assert(cylinder.hit && cylinder.entity == my && cylinder.triangle == ~0u);
        close(cylinder.fraction, (4 - std::sqrt(0.19)) / 8);
        close_vector(cylinder.normal, {-float(std::sqrt(0.19)), 0.9f, 0});
        const auto you = entity({34, 0, 0});
        const auto other = entity({38, 0, 0});
        ProcessContext context{0, my, you};
        close(modern_query(context, {26, 0, 0}, {42, 0, 0}, 0).real(), 3);
        assert(context.you == my);
        context.you = you;
        close(modern_query(context, {26, 0, 0}, {42, 0, 0}, 1).real(), 7);
        assert(context.you == you);
        context.you = you;
        close(modern_query(context, {26, 0, 0}, {42, 0, 0}, 2).real(), 3);
        assert(context.you == you);
        context.you = you;
        close(modern_query(context, {26, 0, 0}, {42, 0, 0}, 1 | 2).real(), 11);
        assert(context.you == you);
        for (auto id : {my, you, other}) engine.set_field(id, "flags", 512);
        for (auto mode : {4u, 8u, 16u})
            assert(!engine.collide({26, 0, 0}, {42, 0, 0}, {}, mode, 0, nullptr, true).hit);
        for (auto id : {my, you, other}) engine.set_field(id, "flags", 0);

        constexpr std::uint32_t polygon = 0x04000000;
        const auto polygonRef = state.member(my, "polygon");
        assert(polygonRef.bitMask == polygon);
        polygonRef.set(Value::from_int(1));
        assert(state.require(my).field("flags").value.bits == polygon);
        assert(polygonRef.get().integer() == 1 && engine.flags(my) == polygon);
        engine.set_field(my, "flags", polygon | 512);
        assert(state.require(my).field("flags").value.bits == (polygon | 512));
        assert(engine.field(my, "flags") == double(polygon | 512));
        engine.set_field(my, "polygon", 0);
        assert(engine.flags(my) == 512);
        engine.set_field(my, "polygon", 1);
        assert(engine.flags(my) == (polygon | 512));

        runtime.load_text("engine-regression.wdl", R"wdl(
            function trace_probe(from,to) {
                c_trace(from,to,trace_mode);
                my.skill10 = you.skill9;
            }
            function legacy_texture_probe(from,to) {
                trace_mode = 65536;
                trace(from,to);
                my.skill61 = tex_light;
                my.skill62 = tex_fog;
            }
            function modern_texture_probe(from,to) {
                c_trace(from,to,65536);
                my.skill61 = tex_light;
                my.skill62 = tex_fog;
            }
            function vector_param_probe(p) {
                vec_scale(p,2);
            }
            action scan_callback() {
                my.skill1 += 1;
                my.skill2 = event_type;
                my.skill3 = you.skill9;
                wait(1);
                my.skill4 += 1;
                my.skill5 += you.skill9;
            }
            function physics_event_probe(receiver,other) {
                my.skill70=(receiver==my)&&(other==you);
                my.skill71=event_type;
                my.skill72=(_arg1==my)&&(_arg2==you);
                target.x=123; normal.z=-2;
                wait(1);
                my.skill73=(receiver==my)&&(other==you);
            }
            function physics_event_error(receiver,other) { error("scoped collision"); }
        )wdl");
        assert(runtime.diagnostics().empty());
        engine.set_field(you, "skill9", 77);
        engine.set_field(my, "flags", 0);
        engine.set_global("trace_mode", Value::from_int(1 | 2));
        const std::array traceArguments{vector_arg({26, 0, 0}), vector_arg({42, 0, 0})};
        runtime.start("trace_probe", traceArguments, my, you);
        close(engine.field(my, "skill10"), 77);
        close(engine.number("result"), 11);
        assert(engine.number("trace_hit") == 1);
        const std::array passedVector{vector_arg({2, 3, 4})};
        runtime.start("vector_param_probe", passedVector, 0, 0);
        close_vector(engine.vector(passedVector[0]), {4, 6, 8});
        const auto passedEntityId = entity({7, 8, 9});
        const std::array passedEntity{Argument{Value::object(passedEntityId), {}}};
        runtime.start("vector_param_probe", passedEntity, 0, 0);
        close_vector(engine.position(passedEntityId), {14, 16, 18});
        engine.entities.erase(passedEntityId);

        // Legacy model scans publish the model resource, not its skin/material.
        engine.entities.clear();
        model = floor.mesh;
        model.flags = 0x20; model.nativeWorld = nullptr;
        model.min = {-100, -100, 0};
        model.max = {100, 100, 0};
        const auto textured = entity({0, 0, 0});
        engine.entities.at(textured).model = &model;
        engine.entities.at(textured).type = 2;
        engine.entities.at(textured).file = "floor-probe.mdl";
        engine.set_field(textured, "flags", polygon | 1);
        const auto polygonFace = engine.collide({0, 0, 5}, {0, 0, -5}, {}, 65536, 0, nullptr, true);
        assert(polygonFace.hit && polygonFace.entity == textured);
        close(polygonFace.fraction, 0.5);
        close_vector(polygonFace.normal, {0, 0, 1});
        const auto polygonBack = engine.collide({0, 0, -5}, {0, 0, 5}, {}, 65536, 0, nullptr, true);
        assert(polygonBack.hit && polygonBack.entity == textured);
        close(polygonBack.fraction, 0.5);
        close_vector(polygonBack.normal, {0, 0, -1});
        engine.set_field(textured, "flags", 1);
        engine.set_field(textured, "min_z", 0);
        engine.set_field(textured, "max_z", 0);
        const auto flatCylinder = engine.collide({0, 0, 5}, {0, 0, -5}, {}, 65536, 0, nullptr, true);
        assert(flatCylinder.hit && flatCylinder.entity == textured);
        close(flatCylinder.fraction, 0.5);
        close_vector(flatCylinder.normal, {0, 0, 1});
        engine.set_field(textured, "flags", polygon | 1);
        engine.set_field(textured, "min_z", -1);
        engine.set_field(textured, "max_z", 1);
        context = {};
        close(trace(context, {0, 0, 5}, {0, 0, -5}, 65536).real(), 3.96875);
        assert(context.you == textured && engine.number("trace_hit") == 1);
        assert(engine.number("tex_flag1") == 0 && engine.number("tex_flag3") == 0);
        close(engine.number("tex_fog"), 0);
        close(engine.number("tex_light"), 128);
        close_vector(engine.vector(engine.global_vector("tex_color")), {});
        assert(engine.string(engine.global("tex_name")) == "floor-probe.mdl");
        close_vector(engine.vector(engine.global_vector("target")), {0, 0, 1.03125f}, 0.002);
        close_vector(engine.vector(engine.global_vector("normal")), {}, 0.002);
        const auto savedName = engine.global("tex_name");
        close(trace(context, {200, 200, 5}, {200, 200, -5}, 65536).real(), 0);
        assert(engine.number("trace_hit") == 0 && context.you == 0);
        assert(engine.number("tex_flag1") == 0 && engine.number("tex_flag3") == 0);
        assert(engine.global("tex_name").bits == savedName.bits);
        assert(engine.string(savedName).empty()); // Original preallocated TEX_NAME is mutated.
        close(engine.number("tex_fog"), 0);
        close(engine.number("tex_light"), 128);
        close_vector(engine.vector(engine.global_vector("tex_color")), {});

        auto modernTrace = [&](Vec3 start, Vec3 end, std::uint32_t mode) {
            const std::array arguments{vector_arg(start), vector_arg(end),
                                       Argument{Value::from_int(mode), {}}};
            return *engine.world_builtin(runtime, context, "c_trace", arguments);
        };
        // Modern query publication retains absent user flags and a null name pointer.
        engine.set_global("tex_flag2", Value::from_int(1));
        close(modernTrace({0, 0, 5}, {0, 0, -5}, 65536).real(), 5);
        assert(engine.string(savedName) == "floor-probe.mdl");
        assert(engine.number("tex_flag2") == 1);
        close(modernTrace({200, 200, 5}, {200, 200, -5}, 65536).real(), 0);
        assert(engine.global("tex_name").bits == savedName.bits);
        assert(engine.string(savedName) == "floor-probe.mdl");
        assert(engine.number("tex_flag2") == 1);
        close(engine.number("tex_light"), 128);
        close(engine.number("tex_fog"), 0);
        close_vector(engine.vector(engine.global_vector("tex_color")), {});
        engine.set_global("tex_light", Value::from_int(73));
        close(trace(context, {200, 200, 5}, {200, 200, -5}, 0).real(), 0);
        close(engine.number("tex_light"), 73);
        assert(engine.string(savedName) == "floor-probe.mdl");
        close(trace(context, {200, 200, 5}, {200, 200, -5}, 65536).real(), 0);
        assert(engine.string(savedName).empty());
        assert(engine.number("tex_flag2") == 0);

        // Native WMB source fields and sampler paths are independent of model skins.
        engine.entities.clear();
        engine.world = &floor;
        context = {};
        auto& sampledMaterial = floor.mesh.materials[0];
        auto sampleSurface = [&](bool modern) {
            close((modern ? modernTrace({0, 0, 5}, {0, 0, -5}, 65536)
                          : trace(context, {0, 0, 5}, {0, 0, -5}, 65536)).real(), modern ? 5 : 4.96875);
            assert(engine.string(savedName) == "regression-floor");
            assert(engine.number("tex_flag3") == 1);
            close(engine.number("tex_fog"), 25);
        };
        sampleSurface(false);
        close(engine.number("tex_light"), 0);
        close_vector(engine.vector(engine.global_vector("tex_color")), {50, 50, 50});
        sampleSurface(true);
        close(engine.number("tex_light"), 0);
        close_vector(engine.vector(engine.global_vector("tex_color")), {});

        auto& sampledFace = floor.faces[0];
        sampledFace.lightmapOffset = 0;
        floor.sections[16].assign(256 * 256 * 3, 0);
        floor.sections[16][3] = 100; floor.sections[16][4] = 40; floor.sections[16][5] = 250;
        sampleSurface(false);
        close(engine.number("tex_light"), 180); // floor((100+40+250)/3)+50.
        close_vector(engine.vector(engine.global_vector("tex_color")), {150, 90, 255});
        sampleSurface(true);
        close(engine.number("tex_light"), 130); // Modern RGB3 excludes material ambient.
        close_vector(engine.vector(engine.global_vector("tex_color")), {150, 90, 255});
        const auto textureConsumer = entity({200, 200, 0});
        const std::array textureEndpoints{vector_arg({0, 0, 5}), vector_arg({0, 0, -5})};
        runtime.start("legacy_texture_probe", textureEndpoints, textureConsumer, 0);
        close(engine.field(textureConsumer, "skill61"), 180);
        close(engine.field(textureConsumer, "skill62"), 25);
        runtime.start("modern_texture_probe", textureEndpoints, textureConsumer, 0);
        close(engine.field(textureConsumer, "skill61"), 130);
        close(engine.field(textureConsumer, "skill62"), 25);
        // Compiled hull contacts have no source BSP node: SCAN_TEXTURE cannot
        // manufacture a triangle sample, while the following point cast still can.
        engine.entities.at(textureConsumer).model = &model;
        engine.entities.at(textureConsumer).type = 2;
        engine.set_field(textureConsumer, "narrow", 1);
        engine.set_vector(engine.global_vector("hull_narrowmin"), {});
        engine.set_vector(engine.global_vector("hull_narrowmax"), {});
        ProcessContext compiledContext{0, textureConsumer, 0};
        close(trace(compiledContext, {0, 0, 5}, {0, 0, -5}, 8192 | 1 | 65536).real(), 3.96875);
        assert(engine.string(savedName).empty() && engine.number("tex_flag3") == 0);
        close(engine.number("tex_light"), 128);
        engine.set_field(textureConsumer, "narrow", 0);
        for(auto profile:{SemanticProfile::CampaignA6,SemanticProfile::BonusA7}){
            state.profile=profile;engine.set_global("enable_polycollision",Value::from_int(1));
            close(trace(context,{0,0,5},{0,0,-5},65536).real(),profile==SemanticProfile::BonusA7?5:4.96875);close(engine.number("tex_light"),180);
            const auto sourceFlags=sampledMaterial.flags;const float outside=9999,zero=0;
            floor.sections[16][3]=60;close(trace(context,{0,0,5},{0,0,0},65536).real(),0);close(engine.number("tex_light"),128);close(engine.number("trace_hit"),0);
            floor.sections[16][3]=100; // Exact legacy endpoint does not advance module texel scratch.
            std::memcpy(sampledMaterial.raw.data()+12,&outside,4);sampledMaterial.flags&=~(1u<<18);
            sampledMaterial.raw[42]=std::bit_cast<std::uint8_t>(std::int8_t(-50));sampledMaterial.raw[43]=std::bit_cast<std::uint8_t>(std::int8_t(-25));
            close(trace(context,{0,0,5},{0,0,-5},65536).real(),profile==SemanticProfile::BonusA7?5:4.96875);close(engine.number("tex_light"),180);close(engine.number("tex_fog"),-25);
            close_vector(engine.vector(engine.global_vector("tex_color")),{50,-10,200});close(engine.number("tex_flag3"),0);
            auto sourceBitmap=sampledMaterial.texture;sampledMaterial.texture.reset();sampledMaterial.flags=sourceFlags;sampledMaterial.raw[42]=50;sampledMaterial.raw[43]=25;
            close(trace(context,{0,0,5},{0,0,-5},65536).real(),profile==SemanticProfile::BonusA7?5:4.96875);close(engine.number("tex_light"),180);close(engine.number("tex_fog"),-25);
            close_vector(engine.vector(engine.global_vector("tex_color")),{50,-10,200});close(engine.number("tex_flag3"),0);assert(engine.string(savedName).empty());
            sampledMaterial.texture=std::move(sourceBitmap);std::memcpy(sampledMaterial.raw.data()+12,&zero,4);
            close(trace(context,{0,0,5},{0,0,-5},65536).real(),profile==SemanticProfile::BonusA7?5:4.96875);close(engine.number("tex_fog"),25);close(engine.number("tex_flag3"),1);
            sourceBitmap=sampledMaterial.texture;sampledMaterial.texture.reset();
            close(modernTrace({0,0,5},{0,0,-5},65536).real(),5);close(engine.number("tex_light"),profile==SemanticProfile::BonusA7?255:130);
            assert(engine.string(savedName)=="regression-floor"); // NULL modern diffuse BMAP neither blocks CPU sampling nor clears TEX_NAME.
            sampledMaterial.texture=std::move(sourceBitmap);
        }
        state.profile=SemanticProfile::CampaignA6;
        // The A7 WDL binder aliases formal receiver/other to the two raw argument slots.
        state.profile = SemanticProfile::BonusA7;
        const auto eventOther = entity({250, 250, 0});
        engine.set_field(textureConsumer, "event_flags", (1u << 2) | (1u << 5) | (1u << 6));
        state.require(textureConsumer).field("event").value = Value::function(runtime.function("physics_event_probe"));
        engine.set_global("_arg1", Value::from_int(71));engine.set_global("_arg2", Value::from_int(72));
        for (int event : {2, 5, 6}) {
            engine.set_global("event_type", Value::from_int(19));
            engine.invoke_event(runtime, textureConsumer, eventOther, event);
            close(engine.field(textureConsumer, "skill70"), 1);
            close(engine.field(textureConsumer, "skill71"), event);
            close(engine.field(textureConsumer, "skill72"), 1);
            close(engine.number("event_type"), 0);
            close(engine.number("_arg1"), 71);close(engine.number("_arg2"), 72);
            close(engine.vector(engine.global_vector("target")).x, 123);
            close(engine.vector(engine.global_vector("normal")).z, -2);
            runtime.tick(1024, 0);
            close(engine.field(textureConsumer, "skill73"), 1);
        }
        engine.set_field(textureConsumer, "skill71", -1);
        engine.invoke_event(runtime, textureConsumer, eventOther, 1);
        close(engine.field(textureConsumer, "skill71"), -1); // No fabricated EVENT_BLOCK for ODE.
        state.require(textureConsumer).field("event").value = Value::function(runtime.function("physics_event_error"));
        bool eventFailed = false;
        try { engine.invoke_event(runtime, textureConsumer, eventOther, 2); }
        catch (const std::runtime_error&) { eventFailed = true; }
        assert(eventFailed);
        close(engine.number("event_type"), 0);
        close(engine.number("_arg1"), 71);close(engine.number("_arg2"), 72);
        state.profile = SemanticProfile::CampaignA6;
        // Exercise named wrappers against the existing world, including A7's independent fallback gate.
        for(auto profile:{SemanticProfile::CampaignA6,SemanticProfile::BonusA7}){
            state.profile=profile;const bool bonus=profile==SemanticProfile::BonusA7;
            engine.set_global("enable_polycollision",Value::from_int(1));context={};
            close(modernTrace({0,0,5},{0,0,-5},128|65536).real(),bonus?5:0);
            close(trace(context,{0,0,5},{0,0,-5},128|65536).real(),bonus?5:4.96875);
            close(modernTrace({0,0,5},{0,0,-5},128|65536|0x20000).real(),bonus?5:0);
            if(bonus){
                close(engine.number("tex_light"),180);
                engine.set_global("enable_polycollision",Value::from_raw(1));
                close(modernTrace({0,0,5},{0,0,-5},65536).real(),5);close(engine.number("tex_light"),180);
                engine.set_global("enable_polycollision",Value::from_int(1));
                close(modernTrace({0,0,5},{0,0,-5},65536).real(),5);close(engine.number("tex_light"),255);
            }
            const auto mover=entity({0,0,5});engine.entities.at(mover).model=&floor.mesh;engine.prepare(runtime);context={0,mover,0}; // Retain the modeled owner at its source prepare epoch, before c_move.
            auto moveSurface=[&](bool c,std::uint32_t mode){
                const auto relative=vector_arg({}),absolute=vector_arg({0,0,-10});
                if(c){const std::array args{Argument{Value::object(mover),{}},relative,absolute,Argument{Value::from_int(std::int32_t(mode)),{}}};return engine.builtin(runtime,context,"c_move",args);}
                engine.set_global("move_mode",Value::from_int(std::int32_t(mode)));const std::array args{relative,absolute};return engine.builtin(runtime,context,"ent_move",args);
            };
            moveSurface(true,128|65536);
            if(bonus)assert(engine.position(mover).z>=1&&engine.position(mover).z<2);else close(engine.position(mover).z,-5);
            engine.set_position(mover,{0,0,5});engine.set_global("tex_light",Value::from_int(73));engine.set_global("tex_fog",Value::from_int(-13));engine.set_vector(engine.global_vector("tex_color"),{12,34,56});
            moveSurface(false,128|65536);if(bonus)assert(engine.position(mover).z>=1&&engine.position(mover).z<2);else close(engine.position(mover).z,0.03125,.002);
            close(engine.number("tex_light"),73);close(engine.number("tex_fog"),-13);close_vector(engine.vector(engine.global_vector("tex_color")),{12,34,56});
            if(bonus){
                engine.set_position(mover,{0,0,5});engine.set_global("enable_polycollision",{});moveSurface(true,128|65536);
                assert(engine.position(mover).z>=1&&engine.position(mover).z<2);close(engine.number("tex_light"),73);close_vector(engine.vector(engine.global_vector("tex_color")),{12,34,56});
                engine.set_global("enable_polycollision",Value::from_int(1));
            }
            engine.remove_entity(runtime,mover);context={};
        }
        state.profile=SemanticProfile::CampaignA6;
        // Use the same real WMB face/material/atlas in the automatic per-draw provider.
        Camera sampledView;sampledView.position={0,0,5};sampledView.farClip=100;
        auto encounter = [&](ObjectId id, const Camera& view) {
            engine.prepare(runtime);
            const auto& frame=engine.frame();
            assert(frame.lightContext==&engine&&frame.prepareEntityLight);
            for(const auto& draw:frame.instances)if(draw.objectId==id)
                return frame.prepareEntityLight(frame.lightContext,draw,view);
            throw std::runtime_error("sampled actor missing from render packet");
        };
        constexpr std::array queryGlobals{"trace_hit","hitvertex","result","tex_light","tex_fog","tex_flag1","tex_flag2","tex_flag3","tex_flag4","tex_flag5","tex_flag6","tex_flag7","tex_flag8","trace_mode","move_mode","_arg1","_arg2","event_type","in_passable","on_passable","in_solid"};
        for(std::size_t i=0;i<queryGlobals.size();++i)engine.set_global(queryGlobals[i],Value::from_int(700+int(i)));
        engine.set_global("passable_ent",Value::object(eventOther));
        engine.set_vector(engine.global_vector("target"),{11,12,13});
        engine.set_vector(engine.global_vector("normal"),{21,22,23});
        engine.set_vector(engine.global_vector("bounce"),{31,32,33});
        const std::string queryName(engine.string(engine.global("tex_name")));
        for(auto profile:{SemanticProfile::CampaignA6,SemanticProfile::BonusA7}){
            state.profile=profile;const bool bonus=profile==SemanticProfile::BonusA7;
            engine.set_global("trace_floor",Value::from_int(1));engine.set_global("enable_polycollision",Value::from_int(1));
            const auto actor=entity({0,0,5});engine.entities.at(actor).model=&floor.mesh;
            engine.set_field(actor,"ambient",-91);engine.set_field(actor,"min_z",90);engine.set_field(actor,"max_z",110);
            engine.collisionYou=actor;engine.collisionActor=eventOther;
            const auto& born=engine.entities.at(actor).light;
            assert((born.scalar==127&&born.style==50&&born.delta==std::array<std::int16_t,3>{}&&born.floorDistance==0));
            auto value=encounter(actor,sampledView);
            assert(value.scalar==180&&(value.delta==std::array<std::int16_t,3>{-15,-75,90}));
            assert(engine.entities.at(actor).light.style==25);
            close(engine.field(actor,"floor_dist"),bonus?5:4.96875,.002); // Private floor starts from source actor origin, not public center.
            engine.set_position(actor,{0,0,0});value=encounter(actor,sampledView);
            assert(value.scalar==(bonus?180:127));close(engine.field(actor,"floor_dist"),-1);
            engine.set_position(actor,{0,0,5});assert(encounter(actor,sampledView).scalar==180);
            auto sampledBitmap=sampledMaterial.texture;sampledMaterial.texture.reset();
            sampledMaterial.raw[42]=std::bit_cast<std::uint8_t>(std::int8_t(-50));sampledMaterial.raw[43]=std::bit_cast<std::uint8_t>(std::int8_t(-25));
            engine.set_position(actor,{1,0,5});value=encounter(actor,sampledView);
            assert((value.scalar==180&&value.delta==std::array<std::int16_t,3>{-15,-75,90}));assert(engine.entities.at(actor).light.style==25);
            engine.set_global("trace_floor",Value::from_int(2));engine.set_field(actor,"shadow",1);value=encounter(actor,sampledView);
            assert((value.scalar==(bonus?208:130)&&value.delta==std::array<std::int16_t,3>{-30,-90,120}));assert(engine.entities.at(actor).light.style==std::uint8_t(-25));
            sampledMaterial.texture=std::move(sampledBitmap);sampledMaterial.raw[42]=50;sampledMaterial.raw[43]=25;
            engine.set_global("trace_floor",Value::from_int(1));engine.set_field(actor,"shadow",0);engine.set_position(actor,{0,0,5});assert(encounter(actor,sampledView).scalar==180);
            floor.sections[16][3]=60;engine.set_field(actor,"invisible",1);engine.set_position(actor,{1,0,5});engine.prepare(runtime);
            engine.set_position(actor,{0,0,5});engine.prepare(runtime);engine.set_field(actor,"invisible",0);
            assert(encounter(actor,sampledView).scalar==166); // Source frame producers retain offscreen A->B->A dirty.
            floor.sections[16][3]=100;engine.set_position(actor,{2,0,5});assert(encounter(actor,sampledView).scalar==180);
            if(bonus){
                // A7 CPU sampling is RGB24 even when the A6/GPU storage choice is RGB565.
                floor.nativeLightmapStorageBytes=2;engine.set_position(actor,{1,0,5});
                assert(encounter(actor,sampledView).scalar==180);floor.nativeLightmapStorageBytes=3;
                sampledFace.lightmapOrigin={255,255};sampledFace.lightmapOffset=1;engine.set_position(actor,{2,0,5});
                assert(encounter(actor,sampledView).scalar==180);sampledFace.lightmapOrigin={1,0};sampledFace.lightmapOffset=0;
                const auto previousSampleShift=sampledFace.nativeSampleShift;
                sampledFace.lightmapOrigin={255,255};sampledFace.lightmapOffset=0x0001ffff;sampledFace.texelOriginS=-16;sampledFace.texelOriginT=16;sampledFace.texelExtentS=16;sampledFace.nativeSampleShift=4;
                const float negativeHalf=-.5f,oneCell=16;std::memcpy(sampledMaterial.raw.data()+12,&negativeHalf,4);std::memcpy(sampledMaterial.raw.data()+28,&oneCell,4);
                for(std::size_t c=0;c<3;++c){floor.sections[16][255*3+c]=std::uint8_t(10+c*10);floor.sections[16][256*3+c]=std::uint8_t(30+c*10);}
                engine.set_position(actor,{3,0,5});assert(encounter(actor,sampledView).scalar==90); // Legacy truncates before subtracting native minimum.
                engine.set_global("trace_floor",Value::from_int(2));engine.set_global("enable_polycollision",Value::from_raw(1));engine.set_position(actor,{4,0,5});
                assert(encounter(actor,sampledView).scalar==198); // Floor bridge allows positive fractional enable; modern subtracts before truncation.
                sampledFace.lightmapOrigin={1,0};sampledFace.lightmapOffset=0;sampledFace.texelOriginS=sampledFace.texelOriginT=sampledFace.texelExtentS=0;sampledFace.nativeSampleShift=previousSampleShift;
                const float restoredZero=0;std::memcpy(sampledMaterial.raw.data()+12,&restoredZero,4);std::memcpy(sampledMaterial.raw.data()+28,&restoredZero,4);
                engine.set_global("trace_floor",Value::from_int(1));engine.set_global("enable_polycollision",Value::from_int(1));engine.set_position(actor,{2,0,5});assert(encounter(actor,sampledView).scalar==180);
                floor.sections[16][3]=60;
                const auto scratchWriter=entity({3,0,0});engine.entities.at(scratchWriter).model=&floor.mesh;
                assert(encounter(scratchWriter,sampledView).scalar==127);close(engine.field(scratchWriter,"floor_dist"),-1);engine.remove_entity(runtime,scratchWriter);
                floor.sections[16][3]=100;
                const float outside=9999;std::memcpy(sampledMaterial.raw.data()+12,&outside,sizeof outside);
                sampledMaterial.raw[42]=std::bit_cast<std::uint8_t>(std::int8_t(-50));sampledMaterial.raw[43]=std::bit_cast<std::uint8_t>(std::int8_t(-25));
                engine.set_position(actor,{1,0,5});value=encounter(actor,sampledView);
                assert(value.scalar==166&&(value.delta==std::array<std::int16_t,3>{-57,-77,133}));
                assert(engine.entities.at(actor).light.style==std::uint8_t(-25)); // Shared legacy texel scratch, new candidate ambient/style.
                const float zero=0;std::memcpy(sampledMaterial.raw.data()+12,&zero,sizeof zero);
                sampledMaterial.raw[42]=50;sampledMaterial.raw[43]=25;engine.set_position(actor,{0,0,5});
                assert(encounter(actor,sampledView).scalar==180);
            }
            floor.sections[16][3]=60;
            assert(encounter(actor,sampledView).scalar==180); // No dirty bit: keep the previous actual sample.
            engine.set_position(actor,{1,0,5});value=encounter(actor,sampledView);
            assert(value.scalar==166&&(value.delta==std::array<std::int16_t,3>{-42,-62,103}));
            engine.set_position(actor,{200,200,5});value=encounter(actor,sampledView);
            close(engine.field(actor,"floor_dist"),-1);
            assert(value.scalar==(bonus?166:127));
            assert(engine.entities.at(actor).light.style==(bonus?25:50));
            assert((value.delta==(bonus?std::array<std::int16_t,3>{-42,-62,103}:std::array<std::int16_t,3>{})));
            floor.sections[16][3]=100;engine.set_position(actor,{0,0,5});
            assert(encounter(actor,sampledView).scalar==180);
            engine.set_global("trace_floor",{});engine.set_position(actor,{1,0,5});
            assert(encounter(actor,sampledView).scalar==180&&!engine.entities.at(actor).light.dirty);
            floor.sections[16][3]=60;engine.set_global("trace_floor",Value::from_int(1));
            assert(encounter(actor,sampledView).scalar==180); // Turning TRACE_FLOOR back on alone does not dirty.
            engine.set_field(actor,"unlit",1);engine.set_position(actor,{2,0,5});
            assert(encounter(actor,sampledView).scalar==180);
            engine.set_field(actor,"unlit",0);
            assert(encounter(actor,sampledView).scalar==180);
            floor.sections[16][3]=100;
            engine.set_field(actor,"flags",0x80000u|0x200000u);
            engine.set_field(actor,"blue",70000);engine.set_field(actor,"green",-70000);engine.set_field(actor,"red",3.5);
            state.member(actor,"lightrange").set(Value::from_raw(1));
            assert((encounter(actor,sampledView).bias==std::array<std::int32_t,3>{}));
            state.member(actor,"lightrange").set({});
            value=encounter(actor,sampledView);
            assert((value.bias==(bonus?std::array<std::int32_t,3>{70000,-70000,3}:std::array<std::int32_t,3>{})));
            assert((engine.entities.at(actor).light.delta==std::array<std::int16_t,3>{-15,-75,90})); // Public color never enters persistent cache.
            for(const auto& draw:engine.frame().instances)if(draw.objectId==actor)assert(draw.bright&&draw.nativeLightScalar==180);
            // A6 0042fa20: a drawn MDL without MATERIAL keeps mat_model (not UNLIT/METAL); Bonus binds none.
            assert(state.member(actor,"material").get().bits==(bonus?0u:engine.global("mat_model").bits)&&(bonus||engine.global("mat_model").bits));
            engine.set_global("trace_floor",Value::from_int(2));engine.set_field(actor,"shadow",1);
            value=encounter(actor,sampledView);assert(value.scalar==(bonus?255:130));
            assert((value.delta==std::array<std::int16_t,3>{-15,-75,90}));
            sampledFace.nativeFlags=0x100;sampledMaterial.raw[42]=std::bit_cast<std::uint8_t>(std::int8_t(-50));sampledMaterial.raw[43]=std::bit_cast<std::uint8_t>(std::int8_t(-25));
            engine.set_position(actor,{0,1,5});value=encounter(actor,sampledView);
            assert((value.scalar==-50&&value.delta==std::array<std::int16_t,3>{}));
            assert(engine.entities.at(actor).light.style==std::uint8_t(-25));
            sampledFace.nativeFlags=0;sampledMaterial.raw[42]=50;sampledMaterial.raw[43]=25;
            engine.set_position(actor,{0,0,5});assert(encounter(actor,sampledView).scalar==(bonus?255:130));
            Camera farView=sampledView;farView.position={0,0,1000};farView.farClip=1;
            if(bonus)sampledMaterial.raw[42]=std::bit_cast<std::uint8_t>(std::int8_t(-50));
            engine.set_position(actor,{0,1,5});value=encounter(actor,farView);
            assert(value.scalar==(bonus?208:127));close(engine.field(actor,"floor_dist"),bonus?5:-1,.002);
            sampledMaterial.raw[42]=50;
            if(bonus){engine.set_global("enable_polycollision",{});engine.set_position(actor,{0,2,5});assert(encounter(actor,farView).scalar==180);}
            engine.set_global("enable_polycollision",Value::from_int(1));engine.set_global("trace_floor",Value::from_int(1));
            // A special map-entity hit retries the ordinary query, which excludes the map but retains the global WMB in legacy.
            const auto map=entity({0,0,5});engine.entities.at(map).model=&floor.mesh;engine.entities.at(map).type=4;
            engine.set_position(actor,{0,0,5});assert(encounter(actor,sampledView).scalar==180);
            close(engine.field(actor,"floor_dist"),bonus?5:4.96875,.002);
            engine.remove_entity(runtime,map);
            // A6 declaration callers retain dirty; A7's ineligible setter consumes it without changing cache.
            engine.entities.at(actor).physicsGeometry=false;engine.set_position(actor,{0,0,6});assert(encounter(actor,sampledView).scalar==180);close(engine.field(actor,"floor_dist"),bonus?5:4.96875,.002);
            assert(engine.entities.at(actor).light.dirty==!bonus);
            for(const auto& draw:engine.frame().instances)if(draw.objectId==actor)assert(draw.nativeFloorLight==bonus);
            engine.remove_entity(runtime,actor);
            const auto terrain=entity({0,0,5});auto& terrainData=engine.entities.at(terrain);terrainData.model=&floor.mesh;terrainData.type=5;
            floor.mesh.min={0,0,10};floor.mesh.max={0,0,20};engine.set_field(terrain,"scale_z",2);engine.set_field(terrain,"pan",90);
            assert(encounter(terrain,sampledView).scalar==180);close(engine.field(terrain,"floor_dist"),bonus?35:34.96875,.002);
            engine.set_field(terrain,"scale_z",3);assert(encounter(terrain,sampledView).scalar==180);close(engine.field(terrain,"floor_dist"),bonus?35:34.96875,.002);
            engine.set_position(terrain,{0,0,6});assert(encounter(terrain,sampledView).scalar==180);close(engine.field(terrain,"floor_dist"),bonus?51:50.96875,.002);
            floor.mesh.min={};floor.mesh.max={};engine.remove_entity(runtime,terrain);
            assert(engine.collisionYou==0&&engine.collisionActor==eventOther); // Removal, not automatic queries, clears YOU.
        }
        state.profile=SemanticProfile::CampaignA6;engine.set_global("trace_floor",Value::from_int(1));
        for(std::size_t i=0;i<queryGlobals.size();++i)close(engine.number(queryGlobals[i]),700+int(i));
        assert(engine.global("passable_ent").kind==Value::Kind::Object&&engine.global("passable_ent").bits==eventOther);
        close_vector(engine.vector(engine.global_vector("target")),{11,12,13});
        close_vector(engine.vector(engine.global_vector("normal")),{21,22,23});
        close_vector(engine.vector(engine.global_vector("bounce")),{31,32,33});
        assert(engine.string(engine.global("tex_name"))==queryName);
        // Full A7 frames require the A7 constructor registry, not a switched A6 State.
        // Reuse the same real mesh while leaving the campaign fixture's state untouched.
        {
            State physicsState;physicsState.profile=SemanticProfile::BonusA7;
            Engine physicsEngine(assets,nullptr,media,physicsState);physicsEngine.world=&floor;
            Runtime physicsRuntime(physicsState,[&](Runtime& vm,ProcessContext& ctx,std::string_view name,std::span<const Argument> args){return physicsEngine.builtin(vm,ctx,name,args);});
            ProcessContext physicsContext;
            auto physical=[&](std::string_view name,std::initializer_list<Argument> args){
                return physicsEngine.builtin(physicsRuntime,physicsContext,name,std::span<const Argument>(args.begin(),args.size()));
            };
            auto bodyEntity=[&](){
                const auto id=physicsState.create(Object::Kind::Entity).id;
                auto& data=physicsEngine.entities[id];data.model=&floor.mesh;data.type=2;
                physicsEngine.set_position(id,{0,0,20});
                for(auto name:{"min_x","min_y","min_z"})physicsEngine.set_field(id,name,-1);
                for(auto name:{"max_x","max_y","max_z","scale_x","scale_y","scale_z"})physicsEngine.set_field(id,name,1);
                return id;
            };
            close(physicsEngine.number("ph_fps_max_lock"),70);close(physicsEngine.number("ph_iterations"),0);
            auto gravity=physicsEngine.global_vector("temp");physicsEngine.set_vector(gravity,{0,0,-10});
            assert(physical("ph_setgravity",{gravity}).raw()==1024);
            physicsEngine.set_global("ph_fps_max_lock",Value::from_int(64));
            const auto body=bodyEntity();physicsEngine.prepare(physicsRuntime);
            assert(physical("phent_settype",{{Value::object(body),{}},{Value::from_int(1),{}},{Value::from_int(0),{}}}).raw()==1024);
            assert(physical("phent_setmass",{{Value::object(body),{}},{Value::from_int(2),{}},{Value::from_int(0),{}}}).raw()==1024);
            const auto savedTimeSmooth=physicsEngine.global("time_smooth");physicsEngine.set_global("time_smooth",{}); // Fixed-step assertions use supplied duration.
            physicsEngine.set_global("freeze_mode",Value::from_raw(1));
            Input physicsInput;physicsInput.dt=3.0/128.0;physicsEngine.tick(physicsRuntime,physicsInput);physicsEngine.finish_frame(physicsRuntime);close(physicsEngine.position(body).z,20);
            physicsEngine.set_global("freeze_mode",{});physicsEngine.tick(physicsRuntime,physicsInput);
            assert(physicsEngine.global("time").raw()==384);
            close(physicsEngine.position(body).z,20); // A7 captures/draws before any physics integration.
            bool bodyPublished=false;
            for(const auto& draw:physicsEngine.frame().instances)if(draw.objectId==body){close(draw.position.z,20);bodyPublished=true;}
            assert(bodyPublished);
            physicsEngine.finish_frame(physicsRuntime);assert(physicsEngine.position(body).z<20); // Explicit native headless completion.
            for(const auto& draw:physicsEngine.frame().instances)if(draw.objectId==body)close(draw.position.z,20);
            // One completed 1/64 step and a half-step interpolation use different source geometries.
            const double completedZ=20.0-10.0/4096.0;
            close(physicsEngine.position(body).z,Value::from_double(20.0-5.0/4096.0).real());
            auto primary=physicsEngine.collide({0,0,20.1},{0,0,19.9},{},0x80000|32,0,nullptr,true,nullptr); // Source IGNORE_SPRITES, tested MODEL PRIMARY.
            assert(primary.hit&&primary.entity==body);close(primary.position.z,completedZ,.0001);
            physicsInput.dt=1.0/256.0;physicsEngine.tick(physicsRuntime,physicsInput);physicsEngine.finish_frame(physicsRuntime);
            primary=physicsEngine.collide({0,0,20.1},{0,0,19.9},{},0x80000|32,0,nullptr,true,nullptr);
            assert(primary.hit&&primary.entity==body);close(primary.position.z,completedZ,.0001);
            close(physicsEngine.position(body).z,Value::from_double(20.0-7.5/4096.0).real());
            physicsEngine.remove_entity(physicsRuntime,body);
            physicsEngine.set_global("time_smooth",savedTimeSmooth);
            assert(!physical("phent_enable",{{Value::object(body),{}},{Value::from_int(1),{}}}));
            physicsEngine.set_global("ph_fps_max_lock",Value::from_int(70));
            const auto clearedBody=bodyEntity();physicsEngine.prepare(physicsRuntime); // PH_BOX wakes source-prepared owner.
            assert(physical("phent_settype",{{Value::object(clearedBody),{}},{Value::from_int(1),{}},{Value::from_int(0),{}}}).raw()==1024);
            physicsEngine.load_level(physicsRuntime,"");
            assert(!physicsEngine.world&&!physical("phent_enable",{{Value::object(clearedBody),{}},{Value::from_int(1),{}}}));
            physicsEngine.remove_entity(physicsRuntime,clearedBody);
        }
        sampledMaterial.raw[42] = std::bit_cast<std::uint8_t>(std::int8_t(-50));
        sampledMaterial.raw[43] = std::bit_cast<std::uint8_t>(std::int8_t(-25));
        close(trace(context, {0, 0, 5}, {0, 0, -5}, 65536).real(), 4.96875);
        close(engine.number("tex_light"), 80);
        close(engine.number("tex_fog"), -25);
        close_vector(engine.vector(engine.global_vector("tex_color")), {50, -10, 200});
        close(modernTrace({0, 0, 5}, {0, 0, -5}, 65536).real(), 5);
        close(engine.number("tex_light"), 130);
        close(engine.number("tex_fog"), -25);
        close_vector(engine.vector(engine.global_vector("tex_color")), {50, -10, 200});
        sampledMaterial.raw[42] = 50; sampledMaterial.raw[43] = 25;
        floor.nativeLightmapStorageBytes = 2;
        floor.sections[16][3] = 247; floor.sections[16][4] = 127; floor.sections[16][5] = 255;
        for (bool modern : {false, true}) {
            sampleSurface(modern);
            close(engine.number("tex_light"), 254);
            close_vector(engine.vector(engine.global_vector("tex_color")), {255, 174, 255});
        }
        floor.nativeLightmapStorageBytes = 1;
        floor.sections[8][0] = 77;
        for (bool modern : {false, true}) {
            sampleSurface(modern);
            close(engine.number("tex_light"), 127);
            close_vector(engine.vector(engine.global_vector("tex_color")), {127, 127, 127});
        }
        floor.infoFlags = 0;
        floor.sections[8] = {255, 255};
        sampleSurface(false);
        close(engine.number("tex_light"), 305);
        close_vector(engine.vector(engine.global_vector("tex_color")), {255, 255, 255});
        sampleSurface(true);
        close(engine.number("tex_light"), 255);
        close_vector(engine.vector(engine.global_vector("tex_color")), {255, 255, 255});
        floor.infoFlags = 1; floor.nativeLightmapStorageBytes = 3;
        floor.sections[16][3] = 100; floor.sections[16][4] = 40; floor.sections[16][5] = 250;
        const float negativeFraction = -0.5f;
        std::memcpy(sampledMaterial.raw.data() + 12, &negativeFraction, sizeof negativeFraction);
        sampledFace.texelOriginS = -16; sampledFace.texelExtentS = 16;
        sampledFace.lightmapOrigin = {};
        sampleSurface(false);
        close(engine.number("tex_light"), 180); // trunc(-.5)-(-16) selects texel 1.
        sampleSurface(true);
        close(engine.number("tex_light"), 0); // trunc(float(-.5)-float(-16)) selects texel 0.
        close_vector(engine.vector(engine.global_vector("tex_color")), {50, 50, 50});
        sampledFace.nativeFlags = 0x100;
        sampleSurface(false);
        close(engine.number("tex_light"), 178);
        sampleSurface(true);
        close(engine.number("tex_light"), 50);
        close_vector(engine.vector(engine.global_vector("tex_color")), {50, 50, 50});
        engine.world = nullptr;

        // Every in-sector recipient is notified, not just the nearest one; waits retain MY/YOU.
        engine.entities.clear();
        const auto scanner = entity({0, 0, 0});
        const auto near = entity({3, 0, 0});
        const auto far = entity({5, 0, 0});
        const auto outside = entity({20, 0, 0});
        for (auto id : {scanner, near, far, outside}) {
            engine.set_field(id, "skill9", id == scanner ? 91 : id == near ? 92 : 93);
            engine.set_field(id, "event_flags", (1u << 7) | (1u << 8));
            state.require(id).field("event").value = Value::function(runtime.function("scan_callback"));
        }
        context = {0, scanner, 0};
        const std::array scanArguments{vector_arg({0, 0, 0}), vector_arg({360, 180, 10})};
        close(engine.builtin(runtime, context, "scan_entity", scanArguments).real(), 3);
        assert(context.you == near);
        assert(engine.field(near, "skill1") == 1 && engine.field(far, "skill1") == 1);
        assert(engine.field(scanner, "skill1") == 2 && engine.field(outside, "skill1") == 0);
        assert(engine.field(near, "skill2") == 8 && engine.field(far, "skill2") == 8);
        assert(engine.field(scanner, "skill2") == 7);
        assert(engine.field(near, "skill3") == 91 && engine.field(far, "skill3") == 91);
        assert(runtime.processes() == 4);
        runtime.tick(64, 0);
        assert(engine.field(near, "skill4") == 1 && engine.field(far, "skill4") == 1);
        assert(engine.field(scanner, "skill4") == 2 && engine.field(outside, "skill4") == 0);
        assert(engine.field(near, "skill5") == 91 && engine.field(far, "skill5") == 91);
        assert(engine.field(scanner, "skill5") == 92 + 93);
        assert(runtime.processes() == 0);

        // Legacy BSP contents normalize into movement's boolean status globals.
        World contents;
        // BSP-only content tests still retain genuine original XYZ; no collision faces are added.
        contents.collision.vertices = floor.collision.vertices;
        contents.sections[3] = floor.sections[3];
        contents.planes.push_back({{1, 0, 0}, 0, 0});
        WorldBspNode contentRoot;
        contentRoot.children = {-1, -1};
        contents.bspNodes.push_back(contentRoot);
        contents.bspLeaves.emplace_back();
        engine.world = &contents;
        engine.entities.clear();
        const auto walker = entity({1, 0, 0});
        ProcessContext movementContext{0, walker, 0};
        engine.set_global("move_mode", Value::from_int(0));
        const std::array unmoved{vector_arg({}), vector_arg({})};
        const std::array modernUnmoved{Argument{Value::object(walker), {}},
                                      unmoved[0], unmoved[1], Argument{Value::from_int(0), {}}};
        for (std::int32_t content : {-256, -128, -126}) {
            contents.bspLeaves[0].content = content;
            // 004544a1–004544aa: a non-PASSABLE zero move only dispatches triggers.
            engine.set_global("in_passable", Value::from_int(51));
            engine.set_global("in_solid", Value::from_int(52));
            close(engine.builtin(runtime, movementContext, "ent_move", unmoved).real(), 0);
            close(engine.number("in_passable"), 51);
            close(engine.number("in_solid"), 52);
            close(engine.builtin(runtime, movementContext, "c_move", modernUnmoved).real(), 0);
            // 0046fa80 clears both before non-PASSABLE zero c_move; no destination probe.
            close(engine.number("in_passable"),0);close(engine.number("in_solid"),0);
        }
        {
            State bonusState;bonusState.profile=SemanticProfile::BonusA7;
            Engine bonusEngine(assets,nullptr,media,bonusState);
            Runtime bonusRuntime(bonusState,[&](Runtime& vm,ProcessContext& ctx,std::string_view name,std::span<const Argument> args){return bonusEngine.builtin(vm,ctx,name,args);});
            bonusRuntime.load_text("content-consumer.wdl",R"wdl(
                DEFINE IGNORE_CONTENT = 0;
                var content_events=0;var content_bindings=0;entity* content_victim;
                function content_remove(receiver,other){content_events+=1;if(event_type==10&&receiver==my&&other==you&&_arg1==my&&_arg2==you)content_bindings+=1;ent_remove(my);}
                function remove_prior_contact(receiver,other){ent_remove(content_victim);ent_remove(my);}
                function masked_trace(from,to){return c_trace(from,to,IGNORE_CONTENT);}
            )wdl");
            auto v=[&](Vec3 value){Argument a{Value::object(bonusState.create(Object::Kind::Vector).id),{}};bonusEngine.set_vector(a,value);return a;};
            auto actor=[&](Vec3 value,std::uint32_t type=2,std::uint32_t flags=0){auto id=bonusState.create(Object::Kind::Entity).id;auto& data=bonusEngine.entities.try_emplace(id).first->second;data.model=&floor.mesh;data.type=type;bonusEngine.set_position(id,value);for(auto name:{"scale_x","scale_y","scale_z"})bonusEngine.set_field(id,name,1);for(auto name:{"min_x","min_y","min_z"})bonusEngine.set_field(id,name,-1);for(auto name:{"max_x","max_y","max_z"})bonusEngine.set_field(id,name,1);bonusEngine.set_field(id,"flags",flags);bonusEngine.prepare(bonusRuntime);return id;};
            const auto sentinel=actor({1000,1000,1000});ProcessContext ctx{0,sentinel,0};
            auto seed=[&]{bonusEngine.set_global("passable_ent",Value::object(sentinel));bonusEngine.set_global("in_passable",Value::from_int(41));bonusEngine.set_global("on_passable",Value::from_int(42));bonusEngine.set_global("in_solid",Value::from_int(43));};
            auto trace=[&](Vec3 from,Vec3 to,std::uint32_t mode){const std::array args{v(from),v(to),Argument{Value::from_int(mode),{}}};return bonusEngine.builtin(bonusRuntime,ctx,"c_trace",args);};
            bonusEngine.world=&contents;
            for(auto content:{-126,-128}){contents.bspLeaves[0].content=content;seed();close(trace({1,0,5},{-1,0,5},64).real(),0);close(bonusEngine.number("in_passable"),41);close(bonusEngine.number("on_passable"),42);assert(bonusEngine.global("passable_ent").bits==sentinel);close(trace({1,0,5},{-1,0,5},0).real(),0);close(bonusEngine.number("in_passable"),content==-126);close(bonusEngine.number("on_passable"),0);assert(bonusEngine.global("passable_ent").bits==0);close(bonusEngine.number("in_solid"),43);}
            bonusEngine.world=&floor;
            for(auto flags:{0u,64u}){floor.collision.flags[0]=flags;seed();close(trace({0,0,5},{0,0,-5},0).real(),5);close(bonusEngine.number("trace_hit"),1);close(bonusEngine.number("on_passable"),flags==64);close_vector(bonusEngine.vector(bonusEngine.global_vector("target")),{0,0,0},.002);const std::array args{v({0,0,5}),v({0,0,-5})};seed();close(bonusRuntime.start("masked_trace",args).real(),5);close(bonusEngine.number("in_passable"),41);close(bonusEngine.number("on_passable"),42);assert(bonusEngine.global("passable_ent").bits==sentinel);}
            for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
                bonusState.profile = profile;
                seed();
                if (profile == SemanticProfile::CampaignA6) {
                    close(trace({0,0,5}, {0,0,-5}, 64).real(), 5);
                    close(bonusEngine.number("in_passable"), 41);
                    close(bonusEngine.number("on_passable"), 42);
                    assert(bonusEngine.global("passable_ent").bits == sentinel);
                }
                seed();
                bonusEngine.set_global("trace_mode", Value::from_int(64));
                const std::array args{v({0,0,5}), v({0,0,-5})};
                const bool legacy = profile == SemanticProfile::CampaignA6;
                close(bonusEngine.builtin(bonusRuntime, ctx, "trace", args).real(),
                      legacy ? 4.96875 : 5);
                // 00454fc0 clears statuses; 004546f0 publishes zero for this solid BSP floor.
                close(bonusEngine.number("in_passable"), legacy ? 0 : 41);
                close(bonusEngine.number("on_passable"), legacy ? 0 : 42);
                assert(bonusEngine.global("passable_ent").bits == sentinel);
            }
            assert(!state.compilerMacros.contains("IGNORE_CONTENT"));
            floor.collision.flags[0]=64;
            const auto owner=actor({0,0,0},4,512);
            const auto blocker=actor({20,0,4});seed();close(trace({20,0,0},{20,0,10},12).real(),3);assert(ctx.you==blocker);assert(bonusEngine.global("passable_ent").bits==owner);close(bonusEngine.number("in_passable"),1);close(bonusEngine.number("in_solid"),43);
            seed();close(trace({20,0,0},{20,0,10},12|64).real(),3);assert(ctx.you==blocker);assert(bonusEngine.global("passable_ent").bits==sentinel);close(bonusEngine.number("in_passable"),41);close(bonusEngine.number("on_passable"),42);
            const auto priorEventFlags=static_cast<std::uint32_t>(bonusEngine.field(owner,"event_flags"));bonusEngine.set_field(owner,"ENABLE_SONAR",1);assert(static_cast<std::uint32_t>(bonusEngine.field(owner,"event_flags"))==(priorEventFlags|0x400));bonusState.require(owner).field("event").value=Value::function(bonusRuntime.function("content_remove"));bonusEngine.set_global("_arg1",Value::from_int(71));bonusEngine.set_global("_arg2",Value::from_int(72));
            close(trace({20,0,0},{20,0,10},4096|12|64).real(),3);assert(bonusEngine.entities.contains(owner));close(bonusEngine.number("content_events"),0); // Only content probing is masked; range trigger is out of range.
            close(trace({20,0,0},{20,0,10},4096|12).real(),3);assert(!bonusEngine.entities.contains(owner));close(bonusEngine.number("content_events"),1);close(bonusEngine.number("content_bindings"),1);close(bonusEngine.number("_arg1"),71);close(bonusEngine.number("_arg2"),72);close(bonusEngine.number("event_type"),0);
            const auto laterCallback=actor({20,0,8},4);bonusEngine.set_field(laterCallback,"trigger_range",100);bonusEngine.set_field(laterCallback,"ENABLE_SONAR",1);bonusState.require(laterCallback).field("event").value=Value::function(bonusRuntime.function("remove_prior_contact"));bonusEngine.set_global("content_victim",Value::object(blocker));const auto remainingBlocker=actor({20,0,7});close(trace({20,0,0},{20,0,10},4096|12|64).real(),6);assert(ctx.you==remainingBlocker);assert(!bonusEngine.entities.contains(blocker)&&!bonusEngine.entities.contains(laterCallback)); // IGNORE_CONTENT does not disable generic range events.
            const auto noResource=bonusEngine.create_entity(bonusRuntime,"",{20,0,0},0);bonusEngine.set_field(noResource,"ENABLE_SONAR",1);bonusEngine.set_field(noResource,"trigger_range",1000);bonusState.require(noResource).field("event").value=Value::function(bonusRuntime.function("content_remove"));close(trace({20,0,0},{20,0,10},4096|12).real(),6);assert(bonusEngine.entities.contains(noResource));close(bonusEngine.number("content_events"),1);
            close(trace({20,0,7},{20,0,7.5},12).real(),1);assert(ctx.you==remainingBlocker);close_vector(bonusEngine.vector(bonusEngine.global_vector("target")),{20,0,8},.002); // 10080150 returns the exit plane even beyond the supplied endpoint.
            close(trace({20,0,6},{20,0,6.5},12).real(),2);assert(ctx.you==remainingBlocker);close_vector(bonusEngine.vector(bonusEngine.global_vector("target")),{20,0,8},.002); // A boundary start uses the exit plane, not a fabricated zero-distance contact.
            close(trace({20,0,7},{20,0,7},8192|12).real(),-1);assert(ctx.you==remainingBlocker);close_vector(bonusEngine.vector(bonusEngine.global_vector("target")),{20,0,7},.002);close_vector(bonusEngine.vector(bonusEngine.global_vector("normal")),{1,0,0}); // 10080ee0 static overlap scalar and contact point are not distance*zero-length or ordinary SAT penetration.
            floor.collision.flags[0]=0;
            bonusEngine.world=&contents;const auto mover=actor({1,0,5});ProcessContext moveCtx{0,mover,0};
            for(auto mask:{0u,64u})for(bool pass:{false,true}){bonusEngine.set_field(mover,"flags",pass?512:0);seed();const std::array args{Argument{Value::object(mover),{}},v({}),v(pass?Vec3{0,1,0}:Vec3{}),Argument{Value::from_int(mask),{}}};close(bonusEngine.builtin(bonusRuntime,moveCtx,"c_move",args).real(),pass?1:0);close(bonusEngine.number("in_passable"),0);close(bonusEngine.number("in_solid"),0);close(bonusEngine.number("on_passable"),42);assert(bonusEngine.global("passable_ent").bits==sentinel);}
            // A7 100757e0's admitted .707 approach uses 1.01 overclip and
            // unclamped friction; it is not A6's six-plane crease solver.
            bonusEngine.world=&floor;bonusEngine.set_field(mover,"flags",0);
            bonusEngine.set_field(mover,"narrow",0);bonusEngine.set_field(mover,"fat",0);
            for(auto angle:{"pan","tilt","roll"})bonusEngine.set_field(mover,angle,0);
            bonusEngine.set_global("move_friction",Value::from_double(.25));
            bonusEngine.set_global("move_min_z",Value::from_int(-1));bonusEngine.set_global("disable_z_glide",{});
            auto diagonalMove=[&](std::uint32_t flags){
                bonusEngine.set_position(mover,{0,0,5});bonusEngine.prepare(bonusRuntime);
                const std::array args{Argument{Value::object(mover),{}},v({}),v({10,0,-10}),Argument{Value::from_int(flags|1|16),{}}};
                return bonusEngine.builtin(bonusRuntime,moveCtx,"c_move",args);
            };
            diagonalMove(0);const auto firstContact=bonusEngine.position(mover);
            assert(firstContact.x>3.99f&&firstContact.x<4.01f&&firstContact.z>1.f&&firstContact.z<1.01f);
            const auto diagonalDistance=diagonalMove(262144).real();
            const auto diagonal=bonusEngine.position(mover);
            assert(diagonal.x>8.4f&&diagonal.x<8.6f&&diagonal.z>1.04f&&diagonal.z<1.06f);
            close(diagonal.y,0);close(diagonalDistance,std::sqrt(diagonal.x*diagonal.x+(diagonal.z-5)*(diagonal.z-5)),.002);
            assert(!bonusState.compilerMacros.contains("IGNORE_CONTENTS"));
        }
        {
            // Extend the same retained WMB fixture: route0 at z=0, route1 at z=2.
            const auto materialFlags=floor.mesh.materials[0].flags;
            for(std::size_t i=0;i<3;++i){auto vertex=floor.mesh.vertices[i];vertex.position.z=2;floor.mesh.vertices.push_back(vertex);floor.mesh.sourceVertices.push_back(vertex);floor.collision.vertices.push_back(vertex.position);}
            for (std::size_t i = 3; i < floor.collision.vertices.size(); ++i) {
                const auto point = floor.collision.vertices[i];
                for (float coordinate : {point.x, point.y, point.z}) {
                    const auto bits = std::bit_cast<std::uint32_t>(coordinate);
                    for (unsigned shift = 0; shift < 32; shift += 8)
                        floor.sections[3].push_back(std::uint8_t(bits >> shift));
                }
            }
            floor.mesh.materials.push_back(floor.mesh.materials[0]);floor.mesh.materials[1].flags=64;
            floor.mesh.triangles.push_back({{3,4,5},1,1});auto secondFace=floor.faces[0];secondFace.firstEdge=3;secondFace.materialIndex=1;floor.faces.push_back(secondFace);
            const std::array<std::uint32_t,6> edges{3,0,4,0,5,0};const std::array<std::int32_t,3> references{3,4,5};floor.sections[12].resize(48);floor.sections[13].resize(24);std::memcpy(floor.sections[12].data()+24,edges.data(),24);std::memcpy(floor.sections[13].data()+12,references.data(),12);floor.bspBlocks.push_back(1);floor.bspLeaves[1].blockCount=2;floor.collision.flags[0]=64;
            for(bool onlyAuxiliary:{false,true}){
                floor.mesh.materials[0].flags=onlyAuxiliary?64:0;
                State state;state.profile=SemanticProfile::BonusA7;Engine engine(assets,nullptr,media,state);Runtime runtime(state,[&](Runtime& vm,ProcessContext& ctx,std::string_view name,std::span<const Argument> args){return engine.builtin(vm,ctx,name,args);});engine.world=&floor;
                const auto owner=state.create(Object::Kind::Entity).id;auto& data=engine.entities[owner];data.model=&floor.mesh;data.type=4;for(auto key:{"scale_x","scale_y","scale_z"})engine.set_field(owner,key,1);
                engine.prepare(runtime);
                ProcessContext query;auto hit=engine.collide({0,0,5},{0,0,-5},{},4,0,&query,true);
                assert(hit.hit==!onlyAuxiliary);if(hit.hit)close(hit.position.z,2,.002); // PRIMARY is actually bound to route1.
                engine.set_field(owner,"flags",512);hit=engine.collide({0,0,5},{0,0,-5},{},4,0,&query,true);
                assert(hit.hit==!onlyAuxiliary);assert(hit.onPassable==!onlyAuxiliary);if(hit.hit){close(hit.position.z,2,.002);assert(hit.passableEntity==owner);}
                hit=engine.collide({0,0,5},{0,0,-5},{},4|128,0,&query,true);assert(!hit.hit);assert(hit.onPassable==!onlyAuxiliary);assert(hit.passableEntity==(onlyAuxiliary?0:owner));
                hit=engine.collide({0,0,5},{0,0,-5},{},4|128|64,0,&query,true);assert(!hit.hit&&!hit.contentQuery&&!hit.onPassable);
            }
            // The decoded missing-texture producer clears flags even though raw material64 retains them.
            floor.mesh.materials[0].flags=0;floor.mesh.materials[1].flags=0;floor.mesh.materials[1].raw.resize(64);floor.mesh.materials[1].raw[56]=64;
            {
                State state;state.profile=SemanticProfile::BonusA7;Engine engine(assets,nullptr,media,state);engine.world=&floor;
                const auto owner=state.create(Object::Kind::Entity).id;auto& data=engine.entities[owner];data.model=&floor.mesh;data.type=4;for(auto key:{"scale_x","scale_y","scale_z"})engine.set_field(owner,key,1);
                Runtime runtime(state,[&](Runtime& vm,ProcessContext& ctx,std::string_view name,std::span<const Argument> args){return engine.builtin(vm,ctx,name,args);});engine.prepare(runtime);
                ProcessContext query;const auto hit=engine.collide({0,0,1},{0,0,-1},{},4,0,&query,true);assert(hit.hit&&hit.entity==owner);close(hit.position.z,0,.002);
            }
            floor.mesh.vertices.resize(3);floor.mesh.sourceVertices.resize(3);floor.collision.vertices.resize(3);floor.mesh.materials.resize(1);floor.mesh.materials[0].flags=materialFlags;floor.mesh.triangles.resize(1);floor.faces.resize(1);floor.sections[12].resize(24);floor.sections[13].resize(12);floor.bspBlocks.resize(1);floor.bspLeaves[1].blockCount=1;floor.collision.flags[0]=0;
            floor.sections[3].resize(3 * 12);
        }
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path);
        for(auto name:{"terrain.hmp","terrain_0.hmp","decl.hmp","cold.hmp"})
            assert(std::filesystem::copy_file(std::filesystem::path(argv[2]),temporary.path/name));
        Assets assets(temporary.path,true);Media media(assets,false);State state;state.profile=SemanticProfile::BonusA7;
        Engine engine(assets,nullptr,media,state);Runtime runtime(state,[&](Runtime& vm,ProcessContext& ctx,std::string_view name,std::span<const Argument> args){return engine.builtin(vm,ctx,name,args);});
        runtime.load_text("terrain-consumer.wdl",R"wdl(
            ENTITY declared_terrain { TYPE=<decl.hmp>; X=10000; Y=0; Z=0; VISIBLE=OFF; }
        )wdl");
        close(engine.number("terrain_chunk"),32);close(engine.number("terrain_lod"),0);
        const Vec3 birth{10,20,30};const auto first=engine.create_entity(runtime,"terrain.hmp",birth,0);
        const auto* model=engine.entities.at(first).model;assert(model&&model->formatVersion==105&&model->terrainWidth>1&&model->terrainHeight>1);
        const auto sourceFirst=model->sourceVertices.front().position;const auto& triangle=model->triangles.front();Vec3 local{};
        for(auto index:triangle.indices){const auto p=model->vertices.at(index).position;local.x+=p.x/3;local.y+=p.y/3;local.z+=p.z/3;}
        auto point=[&](Vec3 origin,float scale=1.f){return Vec3{origin.x+local.x*scale,origin.y+local.y*scale,origin.z+local.z*scale};};
        ProcessContext query;
        auto cast=[&](Vec3 target,ObjectId ignored=0){auto start=target,end=target;start.z+=100;end.z-=100;return engine.collide(start,end,{},0,ignored,&query,true);};
        auto hit=cast(point(birth));assert(hit.hit&&hit.entity==first);close_vector(hit.position,point(birth),.02);
        engine.set_position(first,{10000,10000,10000});engine.set_field(first,"pan",90);for(auto key:{"scale_x","scale_y","scale_z"})engine.set_field(first,key,3);
        hit=cast(point(birth));assert(hit.hit&&hit.entity==first);close_vector(hit.position,point(birth),.02);
        const auto shared=engine.create_entity(runtime,"terrain.hmp",{20000,20000,20000},0);assert(engine.entities.at(shared).model==model);
        hit=cast(point(birth),first);assert(hit.hit&&hit.entity==shared);close_vector(hit.position,point(birth),.02);
        close_vector(model->sourceVertices.front().position,sourceFirst);
        engine.remove_entity(runtime,first);engine.remove_entity(runtime,shared);
        const auto unsplit=engine.create_entity(runtime,"terrain_0.hmp",birth,0);hit=cast(point(birth));assert(hit.hit&&hit.entity==unsplit);
        const Vec3 moved{10000,20000,70};engine.set_position(unsplit,moved);for(auto key:{"scale_x","scale_y","scale_z"})engine.set_field(unsplit,key,2);
        hit=cast(point(birth));assert(hit.hit&&hit.entity==unsplit);assert(!cast(point(moved,2)).hit); // Public writes do not refresh query frontends.
        World activeWorld;
        // Real decoded HMP coordinates supply original XYZ without adding competing static faces.
        for (auto index : triangle.indices) {
            const auto point = model->vertices.at(index).position;
            activeWorld.collision.vertices.push_back(point);
            for (float coordinate : {point.x, point.y, point.z}) {
                const auto bits = std::bit_cast<std::uint32_t>(coordinate);
                for (unsigned shift = 0; shift < 32; shift += 8)
                    activeWorld.sections[3].push_back(std::uint8_t(bits >> shift));
            }
        }
        engine.world=&activeWorld;Input frameInput;frameInput.dt=1.0/60.0;engine.tick(runtime, frameInput);engine.finish_frame(runtime);
        assert(!cast(point(birth)).hit);hit=cast(point(moved,2));assert(hit.hit&&hit.entity==unsplit);close_vector(hit.position,point(moved,2),.02);
        engine.remove_entity(runtime,unsplit);
        engine.set_global("terrain_chunk",{});const auto cold=engine.create_entity(runtime,"cold.hmp",birth,0);
        engine.set_global("terrain_chunk",Value::from_int(32));const auto coldShared=engine.create_entity(runtime,"cold.hmp",{40000,40000,0},0);
        engine.set_position(cold,moved);hit=cast(point(birth),coldShared);assert(hit.hit&&hit.entity==cold);assert(!cast(point(moved),coldShared).hit);engine.tick(runtime, frameInput);engine.finish_frame(runtime);
        assert(!cast(point(birth),coldShared).hit);hit=cast(point(moved),coldShared);assert(hit.hit&&hit.entity==cold);
        engine.remove_entity(runtime,cold);engine.remove_entity(runtime,coldShared);
        engine.prepare(runtime);const auto declared=state.global("declared_terrain").value.bits;DrawInstance admitted;bool found=false;
        for(const auto& draw:engine.frame().instances)if(draw.objectId==declared){admitted=draw;found=true;}
        assert(found&&!engine.entities.at(declared).physicsGeometry&&!engine.field(declared,"visible"));
        const auto camera=engine.frame().cameras.front();
        engine.frame().prepareEntityLight(engine.frame().lightContext,admitted,camera); // Explicit VISIBLE is still off: no bake.
        engine.set_global("terrain_chunk",{});engine.set_field(declared,"visible",1);engine.set_field(declared,"invisible",1);engine.set_position(declared,{20000,0,0});
        engine.frame().prepareEntityLight(engine.frame().lightContext,admitted,camera); // INVISIBLE does not capture it either.
        const Vec3 firstVisible{30000,0,70};engine.set_position(declared,firstVisible);for(auto key:{"scale_x","scale_y","scale_z"})engine.set_field(declared,key,2);engine.set_field(declared,"invisible",0);engine.prepare(runtime);
        for(const auto& draw:engine.frame().instances)if(draw.objectId==declared)engine.frame().prepareEntityLight(engine.frame().lightContext,draw,camera);
        const auto declaredAlias=engine.create_entity(runtime,"decl.hmp",{50000,0,0},0);hit=cast(point(firstVisible,2));assert(hit.hit&&hit.entity==declaredAlias);close_vector(hit.position,point(firstVisible,2),.02);assert(!cast(point({10000,0,0})).hit&&!cast(point({20000,0,0})).hit);
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        // Two real decoded WMB7 entity records, one parent link, no resource mocks.
        constexpr std::size_t xyzOffset = 140 + 12 + 2 * 372;
        std::vector<std::uint8_t> bytes(xyzOffset + 36);
        auto word = [&](std::size_t offset, std::uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset + shift / 8] = std::uint8_t(value >> shift);
        };
        auto text = [&](std::size_t offset, std::string_view value) {
            std::copy(value.begin(), value.end(), bytes.begin() + offset);
        };
        text(0, "WMB6");word(124, 140);word(128, std::uint32_t(xyzOffset - 140));
        word(28, xyzOffset);word(32, 36);
        const std::array<float, 9> xyz{-100, -100, 0, 100, -100, 0, 0, 100, 0};
        for (std::size_t i = 0; i < xyz.size(); ++i)
            word(xyzOffset + i * 4, std::bit_cast<std::uint32_t>(xyz[i]));
        word(140, 2);word(144, 12);word(148, 384);
        for (std::size_t i = 0; i < 2; ++i) {
            const auto offset = 152 + i * 372;
            word(offset, 7);
            for (auto coordinate : {28u, 32u, 36u}) word(offset + coordinate, std::bit_cast<std::uint32_t>(1.f));
            text(offset + 40, i ? "parent" : "child");
            text(offset + 106, "bonus_action");
            if (!i) word(offset + 236, 1); // Attached entity-record index, not a native object ID.
        }
        { std::ofstream output(temporary.path / "bonus.wmb", std::ios::binary);
          output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); }
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;state.profile = SemanticProfile::BonusA7;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(vm, context, name, args);
        });
        ProcessContext context;
        auto call = [&](std::string_view name, std::initializer_list<Argument> args) {
            return engine.builtin(runtime, context, name, std::span(args.begin(), args.size()));
        };
        assert(call("fraction", {{Value::from_double(-1.25), {}}}).raw() == -256);
        assert(call("fraction", {{Value::from_raw(INT32_MIN), {}}}).raw() == 0);
        assert(call("integer", {{Value::from_double(-1.75), {}}}).raw() == -1024);
        assert(call("float", {{Value::from_raw(2147483647), {}}}).bits ==
               std::bit_cast<std::uint32_t>(float(2147483647) / 1024.f));
        auto source = engine.new_string(std::string("\xff" "ABC", 4));
        assert(call("str_to_asc", {{source, {}}}).raw() == 255 * 1024);
        assert(call("str_trunc", {{source, {}}, {Value::from_double(1.5), {}}}).bits == source.bits);
        assert(engine.string(source) == std::string("\xff" "AB", 3));
        call("str_trunc", {{source, {}}, {Value::from_int(30), {}}});
        assert(engine.string(source).empty() && !call("str_to_asc", {{source, {}}}));
        auto reservation = engine.new_string("#1000x");
        auto owned = call("str_create", {{reservation, {}}});
        assert(engine.string(owned) == std::string(1000, ' '));
        auto independent = call("str_create", {{owned, {}}});
        call("str_trunc", {{owned, {}}, {Value::from_int(2), {}}});
        assert(engine.string(independent).size() == 1000 && engine.string(owned).size() == 998);
        auto opaqueString = call("handle", {{owned, {}}});
        assert(opaqueString.kind == Value::Kind::Number && opaqueString.bits == owned.bits);
        assert(call("ptr_for_handle", {{opaqueString, {}}}).bits == owned.bits);
        assert(call("str_remove", {{owned, {}}}).raw() == 1024);
        assert(!state.require(owned.bits).alive && !call("ptr_for_handle", {{opaqueString, {}}}));
        bool deadStringRejected = false;
        try { call("str_len", {{owned, {}}}); } catch (const std::runtime_error&) { deadStringRejected = true; }
        assert(deadStringRejected);
        auto nullString = call("str_create", {{Value::object(0), {}}});
        assert(nullString.kind == Value::Kind::Object && nullString.bits);
        auto ordinaryEmpty = call("str_create", {{engine.new_string(""), {}}});
        auto zeroReservation = call("str_create", {{engine.new_string("#0"), {}}});
        assert(ordinaryEmpty.bits && zeroReservation.bits &&
               ordinaryEmpty.bits != zeroReservation.bits && ordinaryEmpty.bits != nullString.bits);
        assert(engine.string(ordinaryEmpty).empty() && engine.string(zeroReservation).empty());
        for (const auto& empty : {ordinaryEmpty, zeroReservation}) {
            assert(call("str_len", {{empty, {}}}).raw() == 0);
            assert(call("str_to_asc", {{empty, {}}}).raw() == 0);
            assert(call("str_trunc", {{empty, {}}, {Value::from_int(1), {}}}).bits == empty.bits);
        }
        auto rejectsNullBuffer = [&](auto operation) {
            bool rejected = false;
            try { operation(); } catch (const std::runtime_error&) { rejected = true; }
            assert(rejected);
        };
        rejectsNullBuffer([&] { engine.string(nullString); });
        assert(call("str_len", {{nullString, {}}}).raw() == 0);
        rejectsNullBuffer([&] { call("str_to_asc", {{nullString, {}}}); });
        rejectsNullBuffer([&] { call("str_trunc", {{nullString, {}}, {Value::from_int(1), {}}}); });
        auto copiedNull = call("str_create", {{nullString, {}}});
        assert(copiedNull.bits && copiedNull.bits != nullString.bits);
        rejectsNullBuffer([&] { call("str_to_asc", {{copiedNull, {}}}); });
        assert(call("str_remove", {{copiedNull, {}}}).raw() == 1024);
        assert(!state.require(copiedNull.bits).alive);
        copiedNull = {};
        auto ordinaryCopy = call("str_create", {{engine.new_string("alive"), {}}});
        assert(engine.string(ordinaryCopy) == "alive");
        assert(call("str_to_asc", {{ordinaryCopy, {}}}).raw() == 'a' * 1024);
        for (const auto& nullSource : {Value::object(0), nullString}) {
            assert(call("str_cpy", {{ordinaryCopy, {}}, {nullSource, {}}}).bits == ordinaryCopy.bits);
            assert(call("str_cat", {{ordinaryCopy, {}}, {nullSource, {}}}).bits == ordinaryCopy.bits);
            assert(engine.string(ordinaryCopy) == "alive");
            assert(call("str_cpy", {{nullString, {}}, {nullSource, {}}}).bits == nullString.bits);
            assert(call("str_cat", {{nullString, {}}, {nullSource, {}}}).bits == nullString.bits);
            assert(call("str_len", {{nullString, {}}}).raw() == 0);
            rejectsNullBuffer([&] { call("str_to_asc", {{nullString, {}}}); });
        }
        auto allocatedCopy = call("str_create", {{Value::object(0), {}}});
        assert(call("str_cpy", {{allocatedCopy, {}}, {ordinaryCopy, {}}}).bits == allocatedCopy.bits);
        assert(engine.string(allocatedCopy) == "alive");
        assert(call("str_to_asc", {{allocatedCopy, {}}}).raw() == 'a' * 1024);
        for (const auto& emptySource : {ordinaryEmpty, zeroReservation}) {
            auto allocatedEmpty = call("str_create", {{Value::object(0), {}}});
            assert(call("str_cpy", {{allocatedEmpty, {}}, {emptySource, {}}}).bits == allocatedEmpty.bits);
            assert(engine.string(allocatedEmpty).empty());
            assert(call("str_to_asc", {{allocatedEmpty, {}}}).raw() == 0);
            assert(call("str_remove", {{allocatedEmpty, {}}}).raw() == 1024);
            assert(!state.require(allocatedEmpty.bits).alive);
        }
        auto allocatedAppend = call("str_create", {{Value::object(0), {}}});
        rejectsNullBuffer([&] { call("str_cat", {{allocatedAppend, {}}, {ordinaryEmpty, {}}}); });
        assert(call("str_len", {{allocatedAppend, {}}}).raw() == 0);
        rejectsNullBuffer([&] { call("str_to_asc", {{allocatedAppend, {}}}); });
        assert(call("str_cat", {{allocatedAppend, {}}, {ordinaryCopy, {}}}).bits == allocatedAppend.bits);
        assert(engine.string(allocatedAppend) == "alive");
        assert(call("str_to_asc", {{allocatedAppend, {}}}).raw() == 'a' * 1024);
        auto allocatedNumber = call("str_create", {{Value::object(0), {}}});
        assert(call("str_for_num", {{allocatedNumber, {}}, {Value::from_int(12), {}}}).bits == allocatedNumber.bits);
        assert(engine.string(allocatedNumber) == "12");
        assert(call("str_to_asc", {{allocatedNumber, {}}}).raw() == '1' * 1024);
        auto readFilename = engine.new_string("null-buffer-read.fixture");
        auto writeHandle = call("file_open_write", {{readFilename, {}}});
        assert(writeHandle);
        call("file_str_write", {{writeHandle, {}}, {ordinaryCopy, {}}});
        call("file_close", {{writeHandle, {}}});
        auto readHandle = call("file_open_read", {{readFilename, {}}});
        assert(readHandle);
        auto allocatedRead = call("str_create", {{Value::object(0), {}}});
        call("file_str_read", {{readHandle, {}}, {allocatedRead, {}}});
        assert(engine.string(allocatedRead) == "alive");
        assert(call("str_to_asc", {{allocatedRead, {}}}).raw() == 'a' * 1024);
        call("file_close", {{readHandle, {}}});
        for (const auto& allocated : {allocatedCopy, allocatedAppend, allocatedNumber, allocatedRead}) {
            assert(call("str_remove", {{allocated, {}}}).raw() == 1024);
            assert(!state.require(allocated.bits).alive);
        }
        const auto nullStringHandle = call("handle", {{nullString, {}}});
        assert(call("ptr_for_handle", {{nullStringHandle, {}}}).bits == nullString.bits);
        assert(call("str_remove", {{nullString, {}}}).raw() == 1024);
        assert(!state.require(nullString.bits).alive);
        assert(!call("ptr_for_handle", {{nullStringHandle, {}}}));
        auto& overlap = state.create(Object::Kind::Vector);overlap.array.resize(5);
        for (std::size_t i = 0; i < 5; ++i) overlap.array[i].value = Value::from_raw(std::int32_t(1 + i * 2));
        auto slice = [&](std::size_t i) {return Argument{overlap.array[i].value, {&overlap.array[i], overlap.id, "x", i}};};
        call("vec_lerp", {slice(1), slice(0), slice(2), {Value::from_raw(512), {}}});
        assert(overlap.array[1].value.raw() == 4 && overlap.array[2].value.raw() == 6 && overlap.array[3].value.raw() == 8);
        for (auto& slot : overlap.array) slot.value = Value::from_raw(INT32_MAX);
        call("vec_lerp", {slice(0), slice(0), slice(0), {Value::from_raw(512), {}}});
        for (std::size_t i = 0; i < 3; ++i) assert(overlap.array[i].value.raw() == INT32_MIN);
        const auto actor = engine.create_entity(runtime, "", {300, 400, 500}, 0);
        auto identity = call("handle", {{Value::object(actor), {}}});
        state.member(actor, "skill70").set(identity);
        assert(call("ptr_for_handle", {{state.member(actor, "skill70").get(), {}}}).bits == actor);
        assert(!call("c_setminmax", {{Value::object(actor), {}}}) && (engine.flags(actor) & 3) == 3);
        Model bounds;bounds.min = {-1.001f, -2, -3};bounds.max = {4, 5, 6};
        auto& data = engine.entities.at(actor);data.model = &bounds;data.boundsOffset = {.0005f, 10, 20};
        assert(call("c_setminmax", {{Value::object(actor), {}}}).raw() == 1024);
        assert(state.member(actor, "min_x").get().raw() == Value::from_double(float(bounds.min.x + data.boundsOffset.x)).raw());
        close(engine.field(actor, "min_y"), 8);close(engine.field(actor, "max_z"), 26);
        engine.set_field(actor, "min_y", -77);close(engine.field(actor, "min_y"), -77);
        engine.remove_entity(runtime, actor);
        assert(!call("ptr_for_handle", {{identity, {}}}));
        runtime.load_text("bonus-load-consumer.wdl", R"wdl(
            var prefixes=0; var completions=0; var zero_calls=0;
            var callback_null=0; var completion_prefixes=-1;
            var declared_prefixes=0;
            ENTITY declared_bonus { action=declared_action; }
            VIEW bonus_aux { visible=ON; layer=1; }
            function bonus_black() { BG_COLOR=0; }
            function bonus_restore() { BG_COLOR=1; }
            action declared_action() { declared_prefixes+=1; }
            action bonus_action() { prefixes+=1; }
            function bonus_level(percent) {
                if(percent==0) { zero_calls+=1; return; }
                if(percent>=100) {
                    completions+=1; callback_null=(my==null)&&(you==null);
                    completion_prefixes=prefixes;
                    while(key_any==0) { wait(1); }
                    freeze_mode=0;
                }
            }
            function request_bonus() { on_level=bonus_level; freeze_mode=1; level_load("bonus.wmb"); }
        )wdl");
        engine.prepare(runtime);
        assert(engine.number("declared_prefixes") == 0);
        runtime.start("request_bonus", {}, 0, 0);
        assert(engine.pendingLevelLoad && !engine.world && engine.number("completions") == 0);
        Input input;input.dt = 1.0 / 60.0;engine.tick(runtime, input);engine.finish_frame(runtime);
        engine.create_entity(runtime, "", {}, runtime.function("bonus_action"));
        assert(engine.number("prefixes") == 0);
        assert(engine.world && engine.world->entities.size() == 2 && engine.frame().world == engine.world);
        assert(engine.number("completions") == 1 && engine.number("zero_calls") == 1);
        assert(engine.number("callback_null") == 1 && engine.number("completion_prefixes") == 0);
        assert(engine.number("prefixes") == 0 && engine.number("freeze_mode") == 1);
        ObjectId child = 0, parent = 0;
        for (const auto& [id, entity] : engine.entities) {
            if (state.require(id).name == "child") child = id;
            if (state.require(id).name == "parent") parent = id;
        }
        assert(child && parent && engine.entities.at(child).parent == parent);
        runtime.tick(64, 1024);
        assert(engine.number("prefixes") == 0 && engine.number("declared_prefixes") == 0);
        engine.set_global("key_any", Value::from_int(1));
        runtime.tick(64, 1024);runtime.tick(64, 0);
        assert(engine.number("freeze_mode") == 0 && engine.number("prefixes") == 3);
        const auto camera = engine.global("camera").bits;
        const auto auxiliary = engine.global("bonus_aux").bits;
        auto preparedCamera = [&](ObjectId id) {
            engine.prepare(runtime);
            for (const auto& view : engine.frame().cameras) {
                assert(view.perDrawOpacity); // Bonus opacity is per draw even when disabled.
                if (view.sourceId == id) return view;
            }
            assert(false && "live Bonus VIEW missing from frame");
            return Camera{};
        };
        engine.set_vector(engine.global_vector("SKY_COLOR"), {11, 22, 33});
        runtime.start("bonus_black", {}, 0, 0);
        assert(engine.global_vector("SKY_COLOR").ref.get().raw() == 0);
        close_vector(engine.vector(engine.global_vector("SKY_COLOR")), {0, 22, 33});
        runtime.start("bonus_restore", {}, 0, 0);
        close_vector(engine.vector(engine.global_vector("SKY_COLOR")), {1, 22, 33});
        engine.set_field(camera, "alpha", 40);
        for (std::uint32_t mask : {0u, 0x10u, 0x400u, 0x10000u, 0x10410u, 0x20u}) {
            state.member(camera, "flags").set(Value::from_raw(std::int32_t(0x4000 | mask)));
            const auto view = preparedCamera(camera);
            assert(view.clearColor == ((mask & 0x10410) == 0));
            assert(view.clearDepth && view.clearStencil);
            const bool opacity = (mask & 0x410) != 0;
            assert(view.opacityByte == (opacity ? 101 : 255));
            close(view.alpha, opacity ? 0.4 : 1);
        }
        state.member(camera, "flags").set(Value::from_raw(0x4400));
        engine.set_field(camera, "alpha", 100);
        assert(preparedCamera(camera).opacityByte == 254);
        state.member(camera, "flags").set(Value::from_raw(0x4000));
        assert(preparedCamera(camera).opacityByte == 255);
        assert(preparedCamera(auxiliary).opacityByte == 255);
        // Supported Bonus WDL entry proves mode&3 enabled; only all-three rawzero suppresses target.
        engine.set_vector(engine.global_vector("SKY_COLOR"), {});
        assert(!preparedCamera(camera).clearColor);
        const auto sky = engine.global_vector("SKY_COLOR");
        for (auto channel : {"x", "y", "z"}) {
            const auto slot = state.member(sky.ref.owner, channel);
            slot.set(Value::from_raw(1)); // Packed RGB remains black, but the native raw gate opens.
            assert(preparedCamera(camera).clearColor);
            slot.set(Value::from_raw(-1));
            assert(preparedCamera(camera).clearColor);
            slot.set({});
        }
        auto screen = engine.global_vector("SCREEN_COLOR");
        engine.set_vector(screen, {});
        engine.prepare(runtime);
        assert(!engine.frame().clearScreen);
        screen.ref.set(Value::from_raw(1));
        engine.prepare(runtime);
        assert(!engine.frame().clearScreen); // Unlike SKY_COLOR, outer clear uses packed RGB.
        engine.set_vector(screen, {1, 0, 0});
        engine.prepare(runtime);
        assert(engine.frame().clearScreen);
        close(engine.frame().screenColor.b, 1.0 / 255);
        close(engine.frame().screenColor.g, 0);close(engine.frame().screenColor.r, 0);
        screen.ref.set(Value::from_raw(-1));
        engine.prepare(runtime);
        assert(engine.frame().clearScreen);
        close(engine.frame().screenColor.b, 1); // Signed SAR10 then byte wrapping, not a rawzero gate.
        auto message = engine.new_string(" ");
        assert(call("sys_exit", {{message, {}}}).raw() == 1 && !engine.quit);
        call("sys_exit", {{Value::object(0), {}}});message = {};
        engine.tick(runtime, input);engine.finish_frame(runtime);assert(engine.quit);
        // Preserve the existing A6 serialized-world queue; no ON_LEVEL alias.
        State campaign;
        Engine campaignEngine(assets, nullptr, media, campaign);
        Runtime campaignRuntime(campaign, [&campaignEngine](Runtime& vm, ProcessContext& ctx,
                                                          std::string_view name, std::span<const Argument> args) {
            return campaignEngine.builtin(vm, ctx, name, args);
        });
        campaignRuntime.load_text("campaign-world-prefix.wdl", R"wdl(
            var prefixes=0; action bonus_action() { prefixes+=1; }
            function campaign_black() { BG_COLOR=0; }
            function campaign_restore() { BG_COLOR=1; }
        )wdl");
        campaignEngine.load_level(campaignRuntime, "bonus.wmb");
        assert(campaignEngine.number("prefixes") == 0 && campaign.profile == SemanticProfile::CampaignA6);
        campaignRuntime.tick(64, 0);
        assert(campaignEngine.number("prefixes") == 2);
        campaignEngine.set_vector(campaignEngine.global_vector("SKY_COLOR"), {11, 22, 33});
        campaignRuntime.start("campaign_black", {}, 0, 0);
        close_vector(campaignEngine.vector(campaignEngine.global_vector("SKY_COLOR")), {0, 22, 33});
        campaignRuntime.start("campaign_restore", {}, 0, 0);
        close_vector(campaignEngine.vector(campaignEngine.global_vector("SKY_COLOR")), {1, 22, 33});
        campaignEngine.prepare(campaignRuntime);
        assert(!campaignEngine.frame().cameras.empty());
        for (const auto& view : campaignEngine.frame().cameras) assert(!view.perDrawOpacity);
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        auto writeBitmap = [&](unsigned width, unsigned height, std::uint8_t red) {
            std::array<std::uint8_t, 36> bytes{};
            bytes[2] = 2; bytes[12] = width; bytes[14] = height; bytes[16] = 24; bytes[17] = 32;
            for (unsigned pixel = 0; pixel < 6; ++pixel) {
                bytes[18 + pixel * 3] = 30; bytes[19 + pixel * 3] = 20;
                bytes[20 + pixel * 3] = red;
            }
            std::ofstream file(temporary.path / "bitmap.tga", std::ios::binary);
            assert(file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()).good());
        };
        writeBitmap(2, 3, 10);
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&](Runtime& vm, ProcessContext& context, std::string_view name,
                                   std::span<const Argument> args) { return engine.builtin(vm, context, name, args); });
        runtime.load_text("bitmap-raw-address.wdl", R"wdl(
            BMAP bitmap = <bitmap.tga>;
            VAR maps[2]; VAR selected=1; VAR_NSAVE offset=0;
            VAR width=0; VAR height=0;
            MATERIAL skin_material { }
            function measure_bitmap() {
                maps[selected]=bitmap;
                width=bmap_width(maps[selected]+offset);
                height=bmap_height(maps[selected]-offset);
                skin_material.skin1=maps[selected]+offset;
                skin_material.skin2=offset+maps[selected];
            }
            function bitmap_actor() { my.material=skin_material; }
            function purge_bitmap() { bmap_purge(maps[selected]+offset); }
        )wdl");
        runtime.start("measure_bitmap");
        close(engine.number("width"), 2); close(engine.number("height"), 3);
        const auto actor = engine.create_entity(runtime, "bitmap.tga", {100, 0, 0},
                                                runtime.function("bitmap_actor"));
        auto checkPixels = [&](std::uint8_t red, unsigned width, unsigned height) {
            engine.prepare(runtime);
            for (const auto& draw : engine.frame().instances) if (draw.objectId == actor) {
                const auto& material = engine.frame().materials[draw.materialIndex];
                for (const auto* image : material.skins) {
                    assert(image && image->width == width && image->height == height);
                    assert(image->rgba.size() == 24);
                    for (unsigned pixel = 0; pixel < 6; ++pixel) {
                        assert(image->rgba[pixel * 4] == red && image->rgba[pixel * 4 + 1] == 20);
                        assert(image->rgba[pixel * 4 + 2] == 30 && image->rgba[pixel * 4 + 3] == 255);
                    }
                }
                return;
            }
            assert(false && "bitmap material actor missing");
        };
        checkPixels(10, 2, 3);
        writeBitmap(3, 2, 80);
        runtime.start("measure_bitmap");
        close(engine.number("width"), 2); close(engine.number("height"), 3); // Cached before purge.
        runtime.start("purge_bitmap");
        runtime.start("measure_bitmap");
        close(engine.number("width"), 3); close(engine.number("height"), 2);
        checkPixels(80, 3, 2);
        ProcessContext context;
        auto reject = [&](Value value) {
            const std::array args{Argument{value, {}}};
            for (auto name : {"bmap_width", "bmap_height", "bmap_purge"}) {
                bool rejected = false;
                try { engine.builtin(runtime, context, name, args); }
                catch (const std::runtime_error&) { rejected = true; }
                assert(rejected);
            }
        };
        reject(Value{}); reject(Value::from_raw(-1));
        reject(Value::from_raw(std::bit_cast<std::int32_t>(engine.global("camera").bits)));
        reject(engine.global("camera"));
        const auto bitmap = engine.global("bitmap").bits;
        reject(Value::function(bitmap)); reject(Value{Value::Kind::NativeText, bitmap});
        const auto dead = state.create(Object::Kind::Bmap).id;
        state.retain_object(dead); state.retire(dead);
        const auto deadWord = Value::from_raw(std::bit_cast<std::int32_t>(dead));
        reject(deadWord);
        state.release_object(dead);
        reject(deadWord); // Reclaimed addresses remain tombstones.
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        auto tga = [&](std::string_view name, unsigned depth) {
            std::array<unsigned char, 22> bytes{};
            bytes[2] = 2;bytes[12] = 1;bytes[14] = 1;
            bytes[16] = static_cast<unsigned char>(depth);bytes[17] = 32;
            bytes[18] = 30;bytes[19] = 20;bytes[20] = 10;bytes[21] = 255;
            std::ofstream output(temporary.path / name, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()), 18 + depth / 8);
        };
        tga("full.tga", 32);tga("reduced.tga", 24);tga("reduced32.tga", 32);tga("fractional.tga", 24);
        for (auto name : {"Normal_Mix.fx", "Normal_SunMix.fx"})
            assert(std::filesystem::copy_file(std::filesystem::path(argv[1]) / name,
                                              temporary.path / name));
        assert(std::filesystem::copy_file(temporary.path / "Normal_Mix.fx",
                                          temporary.path / "unsupported.fx"));
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(vm, context, name, args);
        });
        const auto camera = engine.global("camera").bits;
        const auto cameraAspect = state.member(camera, "aspect");
        assert(cameraAspect.get().raw() == 0x400);
        const auto directView = state.create(Object::Kind::View).id;
        assert(state.member(directView, "aspect").get().raw() == 0x400);
        state.member(directView, "aspect").set(Value{});
        assert(state.member(directView, "aspect").get().raw() == 0); // Explicit zero is not clamped.
        state.retire(directView);
        const auto reusedView = state.create(Object::Kind::View).id;
        assert(reusedView != directView && !state.get(directView));
        assert(state.member(reusedView, "aspect").get().raw() == 0x400);
        state.retire(reusedView);
        const auto scratch = engine.global("TEMP");
        assert(scratch.kind == Value::Kind::Object);
        const auto& scratchObject = state.require(scratch.bits);
        assert(scratchObject.kind == Object::Kind::Vector && scratchObject.array.size() == 5);
        for (const auto& slot : scratchObject.array)
            assert(slot.value.kind == Value::Kind::Number && slot.value.raw() == 0);
        assert(!state.numericGlobals.contains("temp"));
        runtime.load_text("native-temp.wdl", R"wdl(
            VIEW native_mirror { }
            function native_temp_scratch() {
                temp.x=11; temp.y=22; temp.z=33; temp[3]=44; temp[4]=55;
                native_mirror.aspect=camera.aspect;
            }
        )wdl");
        const auto scratchX = state.member(scratch.bits, "x");
        const auto mirror = engine.global("native_mirror").bits;
        assert(state.member(mirror, "aspect").get().raw() == 0x400);
        runtime.start("native_temp_scratch", {}, 0, 0);
        assert(state.member(scratch.bits, "x").slot == scratchX.slot);
        assert(state.member(camera, "aspect").slot == cameraAspect.slot);
        assert(cameraAspect.get().raw() == 0x400);
        assert(state.member(mirror, "aspect").get().raw() == 0x400);
        close_vector(engine.vector(engine.global_vector("TEMP")), {11, 22, 33});
        close(scratchObject.array[3].value.real(), 44);
        close(scratchObject.array[4].value.real(), 55);
        ProcessContext nullRemovalContext;
        const std::array nullRemoval{Argument{Value::object(0), {}}};
        close(engine.builtin(runtime, nullRemovalContext, "ent_remove", nullRemoval).real(), 1);
        const auto particleMaterial = engine.global("MAT_PARTICLE");
        assert(particleMaterial.kind == Value::Kind::Object);
        assert(state.require(particleMaterial.bits).kind == Object::Kind::Material);
        for (auto channel : {"blue", "green", "red"}) {
            close(engine.field(particleMaterial.bits, std::string("ambient_") + channel), 200);
            for (auto prefix : {"diffuse_", "specular_", "emissive_"})
                close(engine.field(particleMaterial.bits, std::string(prefix) + channel), 0);
            close(engine.field(particleMaterial.bits, std::string("map_") + channel), 255);
        }
        close(engine.field(particleMaterial.bits, "alpha"), 100);
        close(engine.field(particleMaterial.bits, "cycle"), 480);
        assert(engine.flags(particleMaterial.bits) == 0);
        assert(!state.require(particleMaterial.bits).field("effect").value);
        assert(!state.require(particleMaterial.bits).field("event").value);
        const auto full = engine.create_entity(runtime, "full.tga", {}, 0);
        auto original = engine.entities.at(full).model->materials[0].texture;
        assert(original->sourceDepth == 32 && original->nativeStorageBytes == 4);
        for(auto channel:{"blue","green","red"})close(engine.field(full,channel),128);
        runtime.load_text("alpha-depth.wdl", "function fractional_depth() { D3D_ALPHADEPTH = 16.25; } function reduce_depth() { D3D_ALPHADEPTH = 16; }");
        runtime.start("fractional_depth", {}, 0, 0);
        const auto fractional = engine.create_entity(runtime, "fractional.tga", {}, 0);
        assert(engine.entities.at(fractional).model->materials[0].texture->nativeStorageBytes == 3);
        runtime.start("reduce_depth", {}, 0, 0);
        const auto reduced = engine.create_entity(runtime, "reduced.tga", {}, 0);
        assert(engine.entities.at(reduced).model->materials[0].texture->nativeStorageBytes == 2);
        const auto reduced32 = engine.create_entity(runtime, "reduced32.tga", {}, 0);
        assert(engine.entities.at(reduced32).model->materials[0].texture->nativeStorageBytes == 2);
        const auto reused = engine.create_entity(runtime, "full.tga", {}, 0);
        assert(engine.entities.at(reused).model->materials[0].texture == original);
        assert(original->nativeStorageBytes == 4); // Creation state does not mutate cached images.
        engine.set_field(full, "ambient", 53);
        Input input;input.dt = 1.0 / 60.0;
        engine.tick(runtime, input);engine.finish_frame(runtime);
        bool foundCamera = false;
        for (const auto& view : engine.frame().cameras) if (view.sourceId == camera) {
            close(view.pixelAspect, 1);foundCamera = true;
        }
        assert(foundCamera);
        bool found = false;
        for (const auto& draw : engine.frame().instances) if (draw.objectId == full) {
            close(draw.ambient, 53);found = true;
            // A 32-bit TGA sprite (BMAP 0x100, A6 00440e90; Wine-observed for world sprites) is alpha-blended at opacity 255 without TRANSPARENT; 24-bit is not.
            assert(draw.translucent && draw.alpha == 1);
        } else if (draw.objectId == fractional) assert(!draw.translucent);
        assert(found);
        // cam1 gazel1.tga leaf decals: a material-less 32-bit TGA FLARE|OVERLAY floor sprite is lit through the default mat_sprite
        // (00430920 -> 0042fa20), blended, and keeps FLARE/OVERLAY; before that binding it took a fixed-ambient path and drew bright yellow.
        engine.set_field(full, "flare", 1);engine.set_field(full, "overlay", 1);engine.set_field(full, "oriented", 1);engine.set_field(full, "tilt", 90);
        engine.prepare(runtime);found = false;
        for (const auto& draw : engine.frame().instances) if (draw.objectId == full) {
            assert(draw.translucent && draw.flare && draw.overlay && !draw.billboard && !draw.additive && draw.materialIndex != ~0u);found = true;
        }
        assert(found && state.member(full, "material").get().bits == engine.global("mat_sprite").bits);
        engine.set_field(full, "flare", 0);engine.set_field(full, "overlay", 0);engine.set_field(full, "oriented", 0);engine.set_field(full, "tilt", 0);
        // A6 0043ee40/00443350: BRIGHT alone draws opaque; BRIGHT with TRANSPARENT stays additive.
        auto fractionalDraw = [&]() -> const DrawInstance& {
            engine.prepare(runtime);
            for (const auto& draw : engine.frame().instances) if (draw.objectId == fractional) return draw;
            throw std::runtime_error("fractional sprite missing from frame");
        };
        engine.set_field(fractional, "bright", 1);
        assert(!fractionalDraw().additive && !fractionalDraw().translucent);
        engine.set_field(fractional, "transparent", 1);
        assert(fractionalDraw().additive && fractionalDraw().translucent);
        engine.set_field(fractional, "bright", 0);engine.set_field(fractional, "transparent", 0);
        // Actual fileless declarations remain entities before and after a later morph.
        runtime.load_text("declared-view.wdl", R"wdl(
            ENTITY unbound_view { VIEW = camera; X = 150; Y = -70; Z = -37.5; }
            function morph_prepared() { ent_morph(unbound_view,"full.tga"); unbound_view.visible = ON; }
        )wdl");
        const auto declared = state.global("unbound_view").value.bits;
        const auto heldX = state.member(declared, "x");
        assert(state.retain_reference(heldX));
        engine.prepare(runtime);
        assert(engine.entities.contains(declared) && engine.entities.at(declared).type == 0);
        assert(!engine.entities.at(declared).model);
        close(heldX.get().real(), 150);
        auto& morphCache=engine.entities.at(declared).light;morphCache.scalar=-77;morphCache.style=231;morphCache.delta={-8,7,1};morphCache.floorDistance=12345;morphCache.dirty=false;
        state.member(declared,"floor_dist").set(Value::from_raw(12345));
        runtime.start("morph_prepared", {}, 0, 0);
        assert(engine.entities.at(declared).type == 1 && engine.entities.at(declared).file == "full.tga");
        assert(engine.entities.at(declared).model);
        const auto& retainedCache=engine.entities.at(declared).light;
        assert((retainedCache.scalar==-77&&retainedCache.style==231&&retainedCache.delta==std::array<std::int16_t,3>{-8,7,1}&&retainedCache.floorDistance==12345&&retainedCache.dirty));
        assert(!engine.entities.at(declared).physicsGeometry);close(engine.field(declared,"floor_dist"),Value::from_raw(12345).real());
        engine.entities.at(declared).light.dirty=false;runtime.start("morph_prepared",{},0,0);
        assert(engine.entities.at(declared).light.scalar==-77&&!engine.entities.at(declared).light.dirty); // Same actual sprite resource is not a cache rebirth/dirty producer.
        for(auto channel:{"blue","green","red"})close(engine.field(declared,channel),128);
        state.member(declared,"blue").set(Value::from_raw(71680511));state.member(declared,"green").set(Value::from_raw(-512));state.member(declared,"red").set(Value::from_raw(3584));
        engine.prepare(runtime);bool specialColorCaptured=false;
        for(const auto& draw:engine.frame().instances)if(draw.objectId==declared){
            assert(!draw.nativeFloorLight);assert((draw.nativeSpecialColor==std::array<std::int32_t,3>{70000,-1,3}));
            const auto actual=engine.frame().prepareEntityLight(engine.frame().lightContext,draw,engine.frame().cameras.front());
            assert(actual.scalar==-77&&!engine.entities.at(declared).light.dirty);specialColorCaptured=true;
        }
        assert(specialColorCaptured);
        assert(state.member(declared, "x").slot == heldX.slot);
        close(heldX.get().real(), 150);
        assert(engine.field(declared, "visible") == 1);
        state.release_reference(heldX);
        runtime.load_text("late-declared-view.wdl", R"wdl(
            ENTITY late_view { VIEW = camera; X = 90; }
            function morph_late() { ent_morph(late_view,"fractional.tga"); }
        )wdl");
        const auto late = state.global("late_view").value.bits;
        assert(!engine.entities.contains(late));
        runtime.start("morph_late", {}, 0, 0);
        assert(engine.entities.at(late).type == 1 && engine.entities.at(late).model);
        close(engine.position(late).x, 90);
        runtime.load_text("native-particle-material.wdl", R"wdl(
            BMAP native_smoke = <full.tga>;
            BMAP native_detail = <reduced.tga>;
            VAR smoke_material=0;
            MATERIAL shared_material {
                ambient_red=-12; ambient_green=34; ambient_blue=56;
                diffuse_red=78; diffuse_green=90; diffuse_blue=112;
                specular_red=134; specular_green=156; specular_blue=178;
                emissive_red=200; emissive_green=222; emissive_blue=300;
                power=17.5; alpha=43.25; albedo=-9;
                skin1=native_smoke; skin2=native_detail;
            }
            MATERIAL zero_material {
                ambient_red=0; ambient_green=0; ambient_blue=0;
                diffuse_red=0; diffuse_green=0; diffuse_blue=0;
                specular_red=0; specular_green=0; specular_blue=0;
                emissive_red=0; emissive_green=0; emissive_blue=0;
                power=0; alpha=0; albedo=0;
            }
            function share_live_material() { my.material=shared_material; }
            function use_zero_material() { my.material=zero_material; }
            function load_shared_effect(filename) { return effect_load(shared_material,filename); }
            function mutate_live_material() {
                shared_material.skin2=NULL;
            }
            function native_smoke_effect() {
                my.material = smoke_material;
                my.bmap = native_smoke;
                my.size = 10;
                my.alpha = 20;
                my.transparent = ON;
                my.flare = ON;
                my.bright = ON;
            }
            function native_smoke_spawn() { smoke_material=MAT_PARTICLE; effect(native_smoke_effect,1,NULLVECTOR,NULLVECTOR); }
        )wdl");
        const auto sharedMaterial = engine.global("shared_material").bits;
        const auto zeroMaterial = engine.global("zero_material").bits;
        const auto sharedFirst = engine.create_entity(runtime, "full.tga", {100, 0, 0},
                                                      runtime.function("share_live_material"));
        const auto sharedSecond = engine.create_entity(runtime, "full.tga", {150, 0, 0},
                                                       runtime.function("share_live_material"));
        const auto nullMaterialActor = engine.create_entity(runtime, "full.tga", {200, 0, 0}, 0);
        const auto zeroMaterialActor = engine.create_entity(runtime, "full.tga", {250, 0, 0},
                                                           runtime.function("use_zero_material"));
        runtime.start("native_smoke_spawn", {}, 0, 0);
        engine.tick(runtime, input);engine.finish_frame(runtime);
        assert(engine.activeParticles.size() == 1);
        const auto smoke = engine.activeParticles.front().object;
        assert(state.require(smoke).field("material").value.bits == particleMaterial.bits);
        assert(engine.frame().particles.size() == 1);
        const auto& renderedSmoke = engine.frame().particles.front();
        assert(renderedSmoke.image == original.get());
        close(renderedSmoke.size, 10);
        close(renderedSmoke.color.a, 0.2);
        assert(renderedSmoke.flare && renderedSmoke.additive);
        assert(renderedSmoke.translucent && renderedSmoke.alphaRaw == 20 * 1024);
        auto drawMaterial = [&](ObjectId actor) {
            for (const auto& draw : engine.frame().instances)
                if (draw.objectId == actor) return draw.materialIndex;
            assert(false && "live material actor missing from frame");
            return ~0u;
        };
        auto packetFor = [&](std::uint32_t index, ObjectId id) -> const ScriptMaterialState& {
            assert(index < engine.frame().materials.size());
            const auto& packet = engine.frame().materials[index];
            assert(packet.sourceId == id);
            return packet;
        };
        const auto sharedIndex = drawMaterial(sharedFirst);
        assert(packetFor(drawMaterial(sharedSecond), sharedMaterial).skins[0] == original.get());
        // A6 0042fa20 binds a persistent built-in default MATERIAL to an entity drawn without one.
        const auto defaultMaterial = state.member(nullMaterialActor, "material").get().bits;
        assert(defaultMaterial ? packetFor(drawMaterial(nullMaterialActor), defaultMaterial).sourceId == defaultMaterial : drawMaterial(nullMaterialActor) == ~0u);
        const auto& sharedPacket = packetFor(sharedIndex, sharedMaterial);
        const auto smokeBitmap = engine.global("native_smoke").bits;
        const auto detailBitmap = engine.global("native_detail").bits;
        assert(sharedPacket.skins[0] == &assets.image(state.require(smokeBitmap).text));
        assert(sharedPacket.skins[1] == &assets.image(state.require(detailBitmap).text));
        assert(sharedPacket.skins[0] == original.get() && sharedPacket.skins[1]);
        engine.set_global("smoke_material", Value::object(sharedMaterial));
        engine.tick(runtime, input);engine.finish_frame(runtime);
        assert(packetFor(drawMaterial(sharedSecond), sharedMaterial).skins[0] == original.get());
        assert(packetFor(engine.frame().particles.front().materialIndex, sharedMaterial).skins[0] == original.get());
        runtime.start("mutate_live_material", {}, 0, 0);
        engine.tick(runtime, input);engine.finish_frame(runtime);
        const auto& mutated = packetFor(drawMaterial(sharedFirst), sharedMaterial);
        assert(!packetFor(drawMaterial(sharedSecond), sharedMaterial).skins[1]);
        assert(!packetFor(engine.frame().particles.front().materialIndex, sharedMaterial).skins[1]);
        assert(mutated.skins[0] == original.get() && !mutated.skins[1]);
        // Explicit MATERIAL skin references resolve the live BMAP image, not the model's original skin.
        auto liveReplacement = std::make_shared<Image>(*original);
        liveReplacement->rgba[0] = 99;
        engine.bitmapImages[smokeBitmap] = liveReplacement;
        engine.tick(runtime, input);engine.finish_frame(runtime);
        assert(packetFor(drawMaterial(sharedFirst), sharedMaterial).skins[0] == liveReplacement.get());
        engine.set_global("smoke_material", Value::object(0));
        state.member(nullMaterialActor, "material").set(Value::from_int(0));
        engine.tick(runtime, input);engine.finish_frame(runtime);
        assert(engine.frame().particles.front().materialIndex == ~0u);
        // Clearing MATERIAL lets 00430920 rebind the same built-in default on the next frame.
        assert(defaultMaterial ? state.member(nullMaterialActor, "material").get().bits == defaultMaterial && packetFor(drawMaterial(nullMaterialActor), defaultMaterial).sourceId == defaultMaterial : drawMaterial(nullMaterialActor) == ~0u);
        for (std::int32_t raw : {-1, 102401, 33554433}) {
            state.member(smoke, "alpha").set(Value::from_raw(raw));
            engine.prepare(runtime);
            assert(engine.frame().particles.size() == 1);
            const auto& particle = engine.frame().particles.front();
            assert(particle.translucent && particle.alphaRaw == raw);
            close(particle.color.a, raw < 0 ? 0 : 1); // Campaign retains its existing public alpha clamp.
        }
        engine.set_field(smoke, "alpha", 20);
        ProcessContext effectContext;
        auto effectLoad = [&](ObjectId material, Value filename) {
            const std::array args{Argument{Value::object(material), {}}, Argument{filename, {}}};
            return engine.builtin(runtime, effectContext, "effect_load", args);
        };
        assert(effectLoad(zeroMaterial, Value::object(0)).raw() == 1024);
        assert(effectLoad(zeroMaterial, engine.new_string("")).raw() == 1024);
        engine.tick(runtime, input);engine.finish_frame(runtime);
        assert(packetFor(drawMaterial(zeroMaterialActor), zeroMaterial).effect == EffectKind::None);
        assert(packetFor(drawMaterial(zeroMaterialActor), zeroMaterial).effectGeneration == 0);
        std::uint64_t sharedEffectGeneration = 0;
        auto loadShared = [&](Value filename, EffectKind expected) {
            const std::array args{Argument{filename, {}}};
            assert(runtime.start("load_shared_effect", args, 0, 0).raw() == 1024);
            engine.tick(runtime, input);engine.finish_frame(runtime);
            assert(packetFor(drawMaterial(sharedFirst), sharedMaterial).effect == expected);
            const auto generation = packetFor(drawMaterial(sharedFirst), sharedMaterial).effectGeneration;
            assert(generation > sharedEffectGeneration);
            sharedEffectGeneration = generation;
            assert(packetFor(drawMaterial(sharedSecond), sharedMaterial).effectGeneration == generation);
            assert(drawMaterial(sharedFirst) == drawMaterial(sharedSecond));
            assert(effectLoad(zeroMaterial, Value::object(0)).raw() == 1024);
            assert(packetFor(drawMaterial(zeroMaterialActor), zeroMaterial).effect == EffectKind::None);
        };
        loadShared(engine.new_string("Normal_Mix.fx"), EffectKind::NormalMix);
        loadShared(engine.new_string("Normal_Mix.fx"), EffectKind::NormalMix); // Same identity still recompiles.
        loadShared(Value::object(0), EffectKind::NormalMix);
        loadShared(engine.new_string(""), EffectKind::NormalMix);
        loadShared(state.make_string("Normal_SunMix.fx", true), EffectKind::NormalSunMix);
        loadShared(Value::object(0), EffectKind::NormalSunMix);
        auto rejected = [&](Value filename) {
            bool failed = false;
            try { effectLoad(sharedMaterial, filename); }
            catch (const std::runtime_error&) { failed = true; }
            assert(failed);
            engine.tick(runtime, input);engine.finish_frame(runtime);
            assert(packetFor(drawMaterial(sharedFirst), sharedMaterial).effect == EffectKind::NormalSunMix);
            assert(packetFor(drawMaterial(sharedFirst), sharedMaterial).effectGeneration == sharedEffectGeneration);
        };
        rejected(engine.new_string("missing.fx"));
        rejected(engine.new_string("unsupported.fx"));
        const auto genuineSunMix = assets.read("Normal_SunMix.fx");
        auto modifiedSunMix = genuineSunMix;
        modifiedSunMix[modifiedSunMix.size() / 2] ^= 1;
        auto writeSunMix = [&](const auto& bytes) {
            std::ofstream output(temporary.path / "Normal_SunMix.fx", std::ios::binary);
            assert(output.write(reinterpret_cast<const char*>(bytes.data()),
                                std::streamsize(bytes.size())).good());
        };
        writeSunMix(modifiedSunMix);
        rejected(engine.new_string("Normal_SunMix.fx"));
        rejected(Value::object(0)); // NULL reparses retained identity; it is not a fake unload-to-None.
        rejected(engine.new_string(""));
        writeSunMix(genuineSunMix);
        loadShared(engine.new_string(""), EffectKind::NormalSunMix);
        {
            State bonusEffectState;bonusEffectState.profile = SemanticProfile::BonusA7;
            Engine bonusEffectEngine(assets, nullptr, media, bonusEffectState);
            Runtime bonusEffectRuntime(bonusEffectState, [&bonusEffectEngine](Runtime& vm, ProcessContext& ctx,
                                                                             std::string_view name,
                                                                             std::span<const Argument> args) {
                return bonusEffectEngine.builtin(vm, ctx, name, args);
            });
            bonusEffectRuntime.load_text("bonus-null-effect-resource.wdl", R"wdl(
                BMAP bonus_alpha_bitmap = <full.tga>;
                function bonus_alpha_particle() {
                    my.bmap=bonus_alpha_bitmap;
                    my.alpha=20;
                    my.transparent=ON;
                    my.bright=ON;
                }
                function bonus_alpha_spawn() { effect(bonus_alpha_particle,1,NULLVECTOR,NULLVECTOR); }
                function bonus_entity_alpha(value,enabled) { my.alpha=value; my.transparent=enabled; }
                function load_bonus_effect() { return effect_load(MAT_PARTICLE,"Normal_Mix.fx"); }
                function reload_bonus_null_effect() {
                    var effect_filename=str_create(NULL);
                    var result=effect_load(MAT_PARTICLE,effect_filename);
                    str_remove(effect_filename);
                    return result;
                }
            )wdl");
            const auto actor = bonusEffectEngine.create_entity(bonusEffectRuntime, "full.tga", {100, 0, 0}, 0);
            bonusEffectState.member(actor, "material").set(bonusEffectEngine.global("MAT_PARTICLE"));
            auto selectedBonusEffect = [&]() -> const ScriptMaterialState& {
                for (const auto& draw : bonusEffectEngine.frame().instances) if (draw.objectId == actor) {
                    assert(draw.materialIndex < bonusEffectEngine.frame().materials.size());
                    const auto& material = bonusEffectEngine.frame().materials[draw.materialIndex];
                    assert(material.sourceId == bonusEffectEngine.global("MAT_PARTICLE").bits);
                    return material;
                }
                throw std::runtime_error("Bonus effect actor missing from frame");
            };
            assert(bonusEffectRuntime.start("load_bonus_effect").raw() == 1024);
            bonusEffectEngine.tick(bonusEffectRuntime, input);
            assert(selectedBonusEffect().effect == EffectKind::NormalMix);
            const auto bonusEffectGeneration = selectedBonusEffect().effectGeneration;
            assert(bonusEffectGeneration != 0);
            assert(bonusEffectRuntime.start("reload_bonus_null_effect").raw() == 1024);
            bonusEffectEngine.tick(bonusEffectRuntime, input);
            assert(selectedBonusEffect().effect == EffectKind::NormalMix);
            assert(selectedBonusEffect().effectGeneration > bonusEffectGeneration);
            auto bonusEntityAlpha = [&](std::int32_t raw, bool enabled) {
                const std::array args{Argument{Value::from_raw(raw), {}},
                                      Argument{Value::from_int(enabled), {}}};
                bonusEffectRuntime.start("bonus_entity_alpha", args, actor, 0);
                bonusEffectEngine.prepare(bonusEffectRuntime);
                for (const auto& draw : bonusEffectEngine.frame().instances) if (draw.objectId == actor) {
                    assert(draw.translucent == enabled);
                    const float expected = enabled ? float(raw) * std::bit_cast<float>(0x3723d70au) : 1.0f;
                    assert(std::bit_cast<std::uint32_t>(draw.alpha) == std::bit_cast<std::uint32_t>(expected));
                    return;
                }
                assert(false && "live Bonus alpha actor missing from frame");
            };
            for (std::int32_t raw : {-1, 102401, 33554433}) bonusEntityAlpha(raw, true);
            bonusEntityAlpha(102401, false); // Stored out-of-range alpha is ignored without ENTITY.TRANSPARENT.
            bonusEffectRuntime.start("bonus_alpha_spawn");
            bonusEffectEngine.tick(bonusEffectRuntime, input);
            assert(bonusEffectEngine.activeParticles.size() == 1);
            const auto particleId = bonusEffectEngine.activeParticles.front().object;
            for (std::int32_t raw : {-1, 102401, 33554433}) {
                bonusEffectState.member(particleId, "alpha").set(Value::from_raw(raw));
                bonusEffectEngine.prepare(bonusEffectRuntime);
                assert(bonusEffectEngine.frame().particles.size() == 1);
                const auto& particle = bonusEffectEngine.frame().particles.front();
                assert(particle.translucent && particle.additive && particle.alphaRaw == raw);
                close(particle.color.a, double(raw) / 102400, 0.0001);
                assert(raw < 0 ? particle.color.a < 0 : particle.color.a > 1);
            }
            bonusEffectState.member(particleId, "transparent").set(Value::from_int(0));
            bonusEffectState.member(particleId, "bright").set(Value::from_int(0));
            bonusEffectEngine.prepare(bonusEffectRuntime);
            assert(!bonusEffectEngine.frame().particles.front().translucent);
            assert(!bonusEffectEngine.frame().particles.front().additive);
            assert(bonusEffectEngine.frame().particles.front().alphaRaw == 33554433);
        }
        // Original A7 10036e9a..10036eae substitutes three zero words only for a NULL direction;
        // 1004dd52..1004dd61 copies a supplied vector unchanged, including a zero first component.
        for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
            State particleState;particleState.profile = profile;
            Engine particleEngine(assets, nullptr, media, particleState);
            Runtime particleRuntime(particleState, [&particleEngine](Runtime& vm, ProcessContext& ctx,
                                                                     std::string_view name,
                                                                     std::span<const Argument> args) {
                return particleEngine.builtin(vm, ctx, name, args);
            });
            particleRuntime.load_text("optional-particle-direction.wdl", R"wdl(
                BMAP smoke_bitmap = <full.tga>;
                function smoke_property() {
                    my.skill_d+=1;
                    my.alpha-=5*time;
                }
                function smoke_initial() {
                    my.skill_a=my.vel_x; my.skill_b=my.vel_y; my.skill_c=my.vel_z;
                    my.bmap=smoke_bitmap; my.move=ON; my.transparent=ON;
                    my.alpha=100; my.size=10; my.lifespan=8;
                    my.function=smoke_property;
                }
                function null_smoke() {
                    smoke_initial();
                    my.vel_x=-2; my.vel_y=3; my.vel_z=5;
                }
                function spawn_smoke() {
                    var origin[3]; var direction[3];
                    origin.x=31; origin.y=-17; origin.z=9;
                    effect(null_smoke,1,origin.x,null);
                    origin.x+=40;
                    direction.x=0; direction.y=-7; direction.z=11;
                    effect(smoke_initial,1,origin.x,direction.x);
                }
            )wdl");
            particleEngine.set_global("time_smooth", Value{});
            particleEngine.set_global("time_factor", Value::from_int(1));
            particleEngine.set_global("fps_lock", Value{});
            particleRuntime.start("spawn_smoke");
            Input particleInput;particleInput.dt=1.0/16.0;
            for (int frame = 1; frame <= 2; ++frame) {
                particleEngine.tick(particleRuntime, particleInput);
                particleEngine.finish_frame(particleRuntime);
                for (std::size_t i = 0; i < 2; ++i) {
                    const auto id = particleEngine.activeParticles.at(i).object;
                    const Vec3 velocity = i == 0 ? Vec3{-2, 3, 5} : Vec3{0, -7, 11};
                    const Vec3 birth = i == 0 ? Vec3{31, -17, 9} : Vec3{71, -17, 9};
                    close_vector({float(particleEngine.field(id, "skill_a")),
                                  float(particleEngine.field(id, "skill_b")),
                                  float(particleEngine.field(id, "skill_c"))},
                                 i == 0 ? Vec3{} : velocity);
                    close(particleEngine.field(id, "skill_d"), frame);
                    close(particleEngine.field(id, "lifespan"), 8 - frame);
                    const auto& rendered = particleEngine.frame().particles[i];
                    close_vector(rendered.position, {birth.x + velocity.x * frame,
                                                     birth.y + velocity.y * frame,
                                                     birth.z + velocity.z * frame});
                    close_vector(rendered.velocity, velocity);
                    assert(rendered.image == original.get() && rendered.translucent);
                    close(rendered.color.a, (100 - 5 * frame) / 100.0);
                    close(rendered.size, 10);
                }
            }
        }
        // Actual legacy REMOVE ME sites end their actions; assert retirement, not unproved tail semantics.
        runtime.load_text("legacy-remove.wdl", R"wdl(
            function legacy_remove_me() { REMOVE ME; }
        )wdl");
        // Program replacement retired its declaration-owned actors; remove a fresh live dynamic actor.
        const auto removed = engine.create_entity(runtime, "full.tga", {90, 0, 0}, 0);
        const auto removedX = state.member(removed, "x");
        assert(state.retain_reference(removedX));
        runtime.start("legacy_remove_me", {}, removed, 0);
        assert(!engine.entities.contains(removed));
        const auto* retired = state.get(removed);
        assert(retired && !retired->alive);
        close(removedX.get().real(), 90);
        state.release_reference(removedX);
        assert(!state.get(removed));
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(vm, context, name, args);
        });
        runtime.load_text("payload-lifetime.wdl", R"wdl(
            var retained_result = 0;
            function held_coordinate(var &coordinate) {
                wait(1);
                coordinate += 7;
                retained_result = coordinate;
            }
            action short_particle() { my.lifespan = 0; }
            action old_particle() { my_action = null; }
            action old_particle_alias() { my_function = null; }
        )wdl");
        const auto actor = engine.create_entity(runtime, "", {35, 0, 0}, 0);
        const auto borrowed = state.member(actor, "x");
        const std::array coordinate{Argument{borrowed.get(), borrowed}};
        runtime.start("held_coordinate", coordinate, 0, 0);
        engine.remove_entity(runtime, actor);
        assert(state.get(actor) && !state.require(actor).alive && borrowed.get().integer() == 35);
        const auto payloadCapacity = state.objects.size();
        const auto uiCandidates = state.uiCandidates.size();
        ProcessContext context;
        const Argument zero = engine.global_vector("nullvector");
        const std::array effect{Argument{Value::function(runtime.function("short_particle")), {}},
                                Argument{Value::from_int(1), {}}, zero, zero};
        const std::array emit{Argument{Value::from_int(1), {}}, zero,
                              Argument{Value::function(runtime.function("old_particle")), {}}};
        const std::array emitAlias{Argument{Value::from_int(1), {}}, zero,
                                   Argument{Value::function(runtime.function("old_particle_alias")), {}}};
        Input input;
        input.dt = 1.0 / 60.0;
        engine.set_global("SKY_CURVE", Value::from_int(3));
        engine.set_global("SKY_CLIP", Value::from_int(123));
        engine.set_global("SKY_BLEND", {});
        engine.set_global("TURB_SPEED", Value::from_int(2));
        engine.set_global("TURB_RANGE", Value::from_int(4));
        engine.set_vector(engine.global_vector("SUN_LIGHT"), {12, 50, 20});
        engine.set_vector(engine.global_vector("SUN_ANGLE"), {});
        const auto camera = engine.global("camera").bits;
        engine.set_field(camera, "ambient", 180);
        for (int iteration = 0; iteration < 2000; ++iteration) {
            const auto id = engine.create_entity(runtime, "", {}, 0);
            assert(id != actor);
            engine.remove_entity(runtime, id);
            assert(!state.get(id));
            engine.builtin(runtime, context, "effect", effect);
            engine.builtin(runtime, context, "emit", iteration % 2 ? emitAlias : emit);
            engine.tick(runtime, input);engine.finish_frame(runtime);
            close(engine.frame().skyCurve, 3);
            close(engine.frame().skyClip, 123);
            assert(!engine.frame().skyBlend);
            close(engine.frame().turbulenceSpeed, 2);
            close(engine.frame().turbulenceRange, 4);
            close_vector(engine.frame().sunDirection, {1, 0, 0});
            assert(!engine.frame().cameras.empty());
            if (iteration == 0) {
                close(engine.frame().cameras[0].nativeAmbient, 127);
                engine.set_field(camera, "ambient", -1);
            } else if (iteration == 1) {
                close(engine.frame().cameras[0].nativeAmbient, 0);
                engine.set_field(camera, "ambient", 7.75);
            } else close(engine.frame().cameras[0].nativeAmbient, 7);
            assert(engine.activeParticles.empty() && engine.entities.empty());
            assert(state.objects.size() <= payloadCapacity + 16);
            assert(state.uiCandidates.size() == uiCandidates);
        }
        assert(engine.number("retained_result") == 42 && !state.get(actor));
        const auto unload = engine.create_entity(runtime, "", {}, 0);
        const auto held = state.member(unload, "skill1");
        held.set(Value::from_int(99));
        assert(state.retain_reference(held));
        engine.builtin(runtime, context, "effect", effect);
        engine.load_level(runtime, "");
        assert(engine.activeParticles.empty() && engine.entities.empty());
        assert(held.get().integer() == 99 && !state.require(unload).alive);
        state.release_reference(held);
        assert(!state.get(unload));
        const auto replacement = engine.create_entity(runtime, "", {}, 0);
        assert(replacement != unload && !state.get(unload));
        engine.remove_entity(runtime, replacement);
        assert(state.objects.size() <= payloadCapacity + 16);
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        { std::ofstream fixture(temporary.path / "K22.ini"); fixture << "[Screen]\nMode = 1\nX = 0\nY = 0\n"; }
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(vm, context, name, args);
        });
        runtime.load_text("startup-providers.wdl", R"wdl(
            DLLFUNCTION INI_IsInitialized();
            DLLFUNCTION INI_GetVariable(key);
            DLLFUNCTION INI_SetVariable(key,value);
            DLLFUNCTION dl_PaKaGa(brightness,contrast,gamma,mode);
            var startup_mode = 0;
            var startup_marker = -1;
            var silah_z = 0;
            var after_sleep = 0;
            var kabus_open_marker = 0;
            var cinematic_mode = 0;
            FUNCTION MAIN() {
                sleep(1);
                after_sleep = 1;
                wait(1);
                kabus_open_marker = 1;
                dll_open("kabus_dll.dll");
                dll_open("renkayar.dll");
                cinematic_mode = str_to_num(INI_GetVariable("Screen:Mode"));
            }
            STARTER SilahPanelDondur() {
                startup_marker = kabus_open_marker;
                startup_mode = str_to_num(INI_GetVariable("Screen:Mode"));
                if (startup_mode == 1) { silah_z = -52; }
                dl_PaKaGa(7,12,20,0);
            }
        )wdl");
        assert(runtime.external_import("INI_GetVariable") && runtime.external_import("dl_PaKaGa"));
        engine.initialize_plugins(runtime);
        ProcessContext context;
        assert(engine.builtin(runtime, context, "ini_isinitialized", {}).integer() == 1);
        runtime.bootstrap();
        assert(engine.number("kabus_open_marker") == 0 && engine.number("startup_marker") == -1);
        runtime.tick(64, 0);
        assert(engine.number("startup_marker") == 0 && engine.number("startup_mode") == 1);
        assert(engine.number("silah_z") == -52 && engine.number("after_sleep") == 0);
        assert(engine.number("kabus_open_marker") == 0 && runtime.processes() == 1);
        for (int tick = 0; tick < 512 && engine.number("after_sleep") == 0; ++tick)
            runtime.tick(64, 0);
        assert(engine.number("after_sleep") == 1 && engine.number("kabus_open_marker") == 0);
        runtime.tick(64, 0);
        assert(engine.number("kabus_open_marker") == 1 && engine.number("cinematic_mode") == 1);
        assert(runtime.processes() == 0);
        const std::array modeKey{Argument{engine.new_string("Screen:Mode"), {}}};
        const auto firstRead = engine.builtin(runtime, context, "ini_getvariable", modeKey);
        const auto secondRead = engine.builtin(runtime, context, "ini_getvariable", modeKey);
        assert(firstRead.kind == Value::Kind::NativeText && secondRead.kind == Value::Kind::NativeText);
        assert(firstRead.bits != secondRead.bits && engine.string(firstRead) == "1");
        const std::array setMode{modeKey[0], Argument{engine.new_string("2"), {}}};
        engine.builtin(runtime, context, "ini_setvariable", setMode);
        const auto changedRead = engine.builtin(runtime, context, "ini_getvariable", modeKey);
        assert(changedRead.kind == Value::Kind::NativeText && changedRead.bits != firstRead.bits &&
               changedRead.bits != secondRead.bits && engine.string(changedRead) == "2");
        assert(engine.string(firstRead) == "1" && engine.string(secondRead) == "1");
        // Reopening must preserve the live mutation, not reload the now-stale private file.
        { std::ofstream fixture(engine.saveRoot / "K22.ini"); fixture << "[Screen]\nMode = 1\nX = 0\nY = 0\n"; }
        const auto adjustment = engine.nativeAdjustment;
        assert(adjustment.enabled && adjustment.gammaRamp[0][0] != 0);
        const auto gamma = engine.gamma;
        assert(engine.discordSocket == -1 && !engine.discordReady);
        engine.discordNonce = 37;
        engine.discordRetryAt = 123456;
        const auto application = engine.discordApplication;
        const auto details = engine.discordDetails;
        const auto status = engine.discordState;
        const auto input = engine.discordInput;
        const auto activitySent = engine.discordActivitySent;
        const std::array kabus{Argument{engine.new_string("kabus_dll.dll"), {}}};
        const std::array renkayar{Argument{engine.new_string("renkayar.dll"), {}}};
        for (int reopen = 0; reopen < 2; ++reopen) {
            engine.initialize_plugins(runtime);
            assert(engine.builtin(runtime, context, "dll_open", kabus).integer() == 2);
            assert(engine.builtin(runtime, context, "dll_open", renkayar).integer() == 1);
            const auto current = engine.builtin(runtime, context, "ini_getvariable", modeKey);
            assert(current.kind == Value::Kind::NativeText && engine.string(current) == "2");
            assert(engine.string(firstRead) == "1" && engine.string(secondRead) == "1" &&
                   engine.string(changedRead) == "2");
            assert(engine.nativeAdjustment.enabled == adjustment.enabled &&
                   engine.nativeAdjustment.gammaRamp == adjustment.gammaRamp && engine.gamma == gamma);
            assert(engine.discordSocket == -1 && !engine.discordReady &&
                   engine.discordNonce == 37 && engine.discordRetryAt == 123456 &&
                   engine.discordApplication == application && engine.discordDetails == details &&
                   engine.discordState == status && engine.discordInput == input &&
                   engine.discordActivitySent == activitySent);
        }
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        struct ProviderCase { const char* source; bool external, active; };
        const std::array cases{
            ProviderCase{R"wdl(PLUGINDIR = "."; DLLFUNCTION INI_GetVariable(key); DLLFUNCTION dl_PaKaGa;)wdl", true, true},
            ProviderCase{R"wdl(PLUGINDIR = "./"; DLLFUNCTION INI_GetVariable(key); DLLFUNCTION dl_PaKaGa;)wdl", true, true},
            ProviderCase{R"wdl(PLUGINDIR = ".\\"; DLLFUNCTION INI_GetVariable(key); DLLFUNCTION dl_PaKaGa;)wdl", true, true},
            ProviderCase{R"wdl(PLUGINDIR = "././"; DLLFUNCTION INI_GetVariable(key); DLLFUNCTION dl_PaKaGa;)wdl", true, true},
            ProviderCase{R"wdl(PLUGINDIR = "plugins"; DLLFUNCTION INI_GetVariable(key); DLLFUNCTION dl_PaKaGa;)wdl", true, false},
            ProviderCase{R"wdl(DLLFUNCTION UnknownProvider;)wdl", false, false},
            ProviderCase{R"wdl(DLLFUNCTION INI_GetVariable(key); FUNCTION INI_GetVariable(key) { return 7; }
                              DLLFUNCTION dl_PaKaGa; FUNCTION dl_PaKaGa() { return 8; })wdl", false, false}
        };
        for (const auto& fixture : cases) {
            Assets assets(temporary.path);
            Media media(assets, false);
            State state;
            Engine engine(assets, nullptr, media, state);
            Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                            std::string_view name, std::span<const Argument> args) {
                return engine.builtin(vm, context, name, args);
            });
            runtime.load_text("provider-metadata.wdl", fixture.source);
            assert(runtime.external_import("INI_GetVariable") == fixture.external);
            assert(runtime.external_import("dl_PaKaGa") == fixture.external);
            if (runtime.function("UnknownProvider")) assert(runtime.external_import("UnknownProvider"));
            engine.initialize_plugins(runtime);
            assert(engine.pluginNative == fixture.active && engine.iniInitialized == fixture.active);
            assert(engine.pluginGamma == fixture.active);
        }
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        { std::ofstream fixture(temporary.path / "subtitle.fixture"); fixture << "Old\\nsubtitle\n"; }
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(vm, context, name, args);
        });
        ProcessContext context;
        runtime.load_text("native-text-provider.wdl", "DLLFUNCTION Parse_EscapeChar(value);");
        engine.initialize_plugins(runtime);
        auto& text = state.create(Object::Kind::Text);
        text.array.resize(1);
        const auto filename = engine.new_string("subtitle.fixture");
        const std::array load{Argument{Value::object(text.id), {}}, Argument{filename, {}}};
        engine.builtin(runtime, context, "txt_load", load);
        Value oldAlias = text.array[0].value;
        const auto oldId = oldAlias.bits;
        state.global("subtitle_alias").value = oldAlias;
        { std::ofstream fixture(temporary.path / "subtitle.fixture"); fixture << "New\\nsubtitle\n"; }
        engine.builtin(runtime, context, "txt_load", load);
        assert(text.array[0].value.bits != oldId && engine.string(oldAlias) == "Old\\nsubtitle");
        assert(engine.string(text.array[0].value) == "New\\nsubtitle");
        Value copied = oldAlias;
        copied = copied;
        copied = std::move(copied);
        Value moved = std::move(copied);
        assert(!copied && moved.bits == oldId && engine.string(moved) == "Old\\nsubtitle");
        oldAlias = {};
        moved = {};
        assert(state.get(oldId)); // The remaining global slot is a real alias.
        Ref{&state.global("subtitle_alias")}.set({});
        assert(!state.get(oldId));
        auto destination = engine.new_string("");
        const auto capacity = state.objects.size();
        for (int iteration = 0; iteration < 3000; ++iteration) {
            const auto displaced = text.array[0].value.bits;
            engine.builtin(runtime, context, "txt_load", load);
            assert(!state.get(displaced));
            const std::array source{Argument{text.array[0].value, {}}};
            ObjectId foreignId;
            {
                Value foreign = engine.builtin(runtime, context, "parse_escapechar", source);
                foreignId = foreign.bits;
                assert(foreign.kind == Value::Kind::NativeText);
                assert(state.require(foreignId).kind == Object::Kind::NativeText);
                if (iteration == 0) {
                    const std::array rawSource{Argument{foreign, {}}};
                    bool rejected = false;
                    try { engine.builtin(runtime, context, "parse_escapechar", rawSource); }
                    catch (const std::exception&) { rejected = true; }
                    assert(rejected); // DLL input is STRING+0xc, never an untagged char*.
                }
                const std::array copy{Argument{destination, {}}, Argument{foreign, {}}};
                engine.builtin(runtime, context, "str_cpy", copy);
                assert(engine.string(destination) == "New\nsubtitle");
                state.global("foreign_alias").value = foreign;
            }
            assert(engine.string(state.global("foreign_alias").value) == "New\nsubtitle");
            state.global("foreign_alias").value = {};
            assert(!state.get(foreignId));
            assert(state.objects.size() <= capacity + 2);
        }
        ObjectId pinnedId;
        Ref ref;
        {
            const auto pinned = engine.new_string("held");
            pinnedId = pinned.bits;
            ref = state.member(pinnedId, "flags");
            assert(state.retain_reference(ref));
        }
        assert(state.get(pinnedId));
        ref.set(Value::from_int(1));
        state.release_reference(ref);
        assert(!state.get(pinnedId));
        Value interior = engine.new_string("interior");
        const auto interiorId = interior.bits;
        auto& interiorSlot = state.require(interiorId).field("numeric");
        interiorSlot.value = Value::from_int(17);
        interior = interiorSlot.value; // Release can destroy the RHS slot; copy its words first.
        assert(interior.integer() == 17 && !state.get(interiorId));
        const std::array key{Argument{engine.new_string("owned"), {}}};
        const std::array setOwned{key[0], Argument{engine.new_string("original"), {}}};
        engine.builtin(runtime, context, "ini_setvariable", setOwned);
        ObjectId originalId;
        {
            const auto original = engine.builtin(runtime, context, "ini_getvariable", key);
            originalId = original.bits;
            assert(original.kind == Value::Kind::NativeText &&
                   state.require(originalId).kind == Object::Kind::NativeText);
            state.global("ini_alias").value = original;
            const auto independent = engine.builtin(runtime, context, "ini_getvariable", key);
            assert(independent.kind == Value::Kind::NativeText && independent.bits != originalId);
            const std::array update{key[0], Argument{engine.new_string("changed"), {}}};
            engine.builtin(runtime, context, "ini_setvariable", update);
            const auto changed = engine.builtin(runtime, context, "ini_getvariable", key);
            assert(changed.kind == Value::Kind::NativeText && changed.bits != originalId &&
                   changed.bits != independent.bits && engine.string(changed) == "changed");
            const std::array plugin{Argument{engine.new_string("kabus_dll.dll"), {}}};
            engine.initialize_plugins(runtime);
            engine.builtin(runtime, context, "dll_open", plugin);
            assert(engine.string(original) == "original" && engine.string(independent) == "original" &&
                   engine.string(changed) == "changed");
        }
        assert(state.get(originalId) && engine.string(state.global("ini_alias").value) == "original");
        Ref{&state.global("ini_alias")}.set({});
        assert(!state.get(originalId));
        runtime.load_text("managed-subtitle.wdl", R"wdl(
            DLLFUNCTION Parse_EscapeChar(value);
            string destination = "";
            string escaped = "script\\nsubtitle";
            text catalog { strings = 1; }
            function consume() { str_cpy(destination,Parse_EscapeChar(escaped)); }
        )wdl");
        engine.initialize_plugins(runtime);
        runtime.start("consume", {}, 0, 0);
        const auto scriptCapacity = state.objects.size();
        for (int iteration = 0; iteration < 3000; ++iteration) runtime.start("consume", {}, 0, 0);
        assert(engine.string(engine.global("destination")) == "script\nsubtitle");
        assert(state.objects.size() == scriptCapacity);
        // State teardown must release managed roots while every payload is still valid.
        state.global("teardown_first").value = engine.new_string("first");
        text.array[0].value = engine.new_string("last");
        state.global("teardown_last").value = text.array[0].value;
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        std::vector<uint8_t> bytes;
        auto word = [&](uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
        };
        auto shortWord = [&](uint16_t value) {
            bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8));
        };
        auto real = [&](float value) { word(std::bit_cast<uint32_t>(value)); };
        auto name = [&](std::string_view value) {
            const auto start = bytes.size(); bytes.resize(start + 16);
            std::copy(value.begin(), value.end(), bytes.begin() + start);
        };
        // Genuine MDL7: two global skins, one 12-byte triangle with no material index.
        bytes.insert(bytes.end(), {'M', 'D', 'L', '7'});
        for (uint32_t value : {7u, 0u, 1u, 0u, 0u, 0u}) word(value);
        for (uint16_t value : std::array<uint16_t, 10>{16, 28, 16, 68, 8, 12, 16, 16, 68, 24})
            shortWord(value);
        bytes.insert(bytes.end(), {1, 0, 0, 0}); word(0); name("skin-group");
        for (uint32_t value : {2u, 3u, 1u, 3u, 1u}) word(value);
        word(4); word(1); word(1); name("first-red");
        bytes.insert(bytes.end(), {0, 0, 255});
        word(4); word(1); word(1); name("second-blue");
        bytes.insert(bytes.end(), {255, 0, 0});
        for (const auto uv : std::array<Vec2, 3>{{{0, 0}, {1, 0}, {0, 1}}}) {
            real(uv.x); real(uv.y);
        }
        for (uint16_t value : std::array<uint16_t, 6>{0, 1, 2, 0, 1, 2}) shortWord(value);
        for (const auto position : std::array<Vec3, 3>{{{0, 0, 0}, {0, 10, 0}, {0, 0, 10}}}) {
            real(position.x); real(position.y); real(position.z);
            shortWord(65535); bytes.push_back(0); bytes.push_back(0);
        }
        name("skin-frame"); word(0); word(0);
        const auto size = uint32_t(bytes.size());
        for (unsigned shift = 0; shift < 32; shift += 8) bytes[16 + shift / 8] = uint8_t(size >> shift);
        {
            std::ofstream fixture(temporary.path / "two-skins.mdl", std::ios::binary);
            assert(fixture.write(reinterpret_cast<const char*>(bytes.data()),
                                 std::streamsize(bytes.size())).good());
        }
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(vm, context, name, args);
        });
        runtime.load_text("global-model-skins.wdl", R"wdl(
            var selected_bitmap = 0;
            action second_skin() { my.skin = 2; selected_bitmap = bmap_for_entity(my,0); }
            function select_skin(var selector) { my.skin = selector; }
        )wdl");
        // Keep every dynamic actor live by creating it after the sole program load.
        const auto actor = engine.create_entity(runtime, "two-skins.mdl", {100, 0, 0},
                                                runtime.function("second_skin"));
        const auto& model = *engine.entities.at(actor).model;
        assert(model.formatVersion == 7 && model.materials.size() == 2);
        assert(model.vertices.size() == 3 && model.triangles.size() == 1 &&
               model.frames.size() == 1 && model.triangles[0].materialIndex == 0);
        const auto firstImage = model.materials[0].texture;
        const auto secondImage = model.materials[1].texture;
        assert(firstImage && secondImage && firstImage != secondImage);
        assert(firstImage->rgba == (std::vector<uint8_t>{255, 0, 0, 255}));
        assert(secondImage->rgba == (std::vector<uint8_t>{0, 0, 255, 255}));
        ProcessContext context;
        auto bitmapFor = [&](ObjectId entity, int32_t rawSelector) {
            const std::array args{Argument{Value::object(entity), {}},
                                  Argument{Value::from_raw(rawSelector), {}}};
            return engine.builtin(runtime, context, "bmap_for_entity", args);
        };
        const auto firstBitmap = bitmapFor(actor, 1024);
        const auto secondBitmap = bitmapFor(actor, 2048);
        assert(firstBitmap.kind == Value::Kind::Object && secondBitmap.kind == Value::Kind::Object &&
               firstBitmap.bits && secondBitmap.bits && firstBitmap.bits != secondBitmap.bits);
        assert(engine.global("selected_bitmap").bits == secondBitmap.bits);
        assert(bitmapFor(actor, 0).bits == secondBitmap.bits);
        assert(engine.skinBitmapObjects.at(firstImage.get()) == firstBitmap.bits &&
               engine.skinBitmapObjects.at(secondImage.get()) == secondBitmap.bits);
        assert(engine.bitmapImages.at(firstBitmap.bits) == firstImage &&
               engine.bitmapImages.at(secondBitmap.bits) == secondImage);
        // Explicit requests truncate by SAR 10, clamp to the bank, and never change MY.SKIN.
        for (int32_t raw : {-102400, -1024, -512, 1024, 1536, 2047})
            assert(bitmapFor(actor, raw).bits == firstBitmap.bits);
        for (int32_t raw : {1, 512, 2048, 2560, 102400, INT32_MAX})
            assert(bitmapFor(actor, raw).bits == secondBitmap.bits);
        assert(state.member(actor, "skin").get().raw() == 2048);
        auto prepared = [&](ObjectId entity, uint32_t index, int32_t publicRaw) {
            engine.prepare(runtime);
            assert(state.member(entity, "skin").get().raw() == publicRaw);
            for (const auto& draw : engine.frame().instances) if (draw.objectId == entity) {
                assert(draw.model == &model && draw.skinIndex == index);
                const auto* selected = model_material(model, model.triangles[0].materialIndex, draw.skinIndex);
                assert(selected == &model.materials[index]);
                assert(selected->texture == (index ? secondImage : firstImage));
                return;
            }
            assert(false && "live model actor missing from prepared instances");
        };
        prepared(actor, 1, 2048); // Renderer resolves the second texture despite face material zero.
        auto select = [&](ObjectId entity, int32_t raw) {
            const std::array args{Argument{Value::from_raw(raw), {}}};
            runtime.start("select_skin", args, entity, 0);
        };
        select(actor, -512); prepared(actor, 0, 1024);
        assert(bitmapFor(actor, 0).bits == firstBitmap.bits);
        select(actor, 1536); prepared(actor, 0, 1536);
        assert(bitmapFor(actor, 0).bits == firstBitmap.bits);
        select(actor, 2047); prepared(actor, 0, 2047);
        select(actor, INT32_MAX); prepared(actor, 1, 2048);
        assert(bitmapFor(actor, 0).bits == secondBitmap.bits);
        auto replacement = std::make_shared<Image>();
        replacement->width = 2; replacement->height = 3;
        replacement->nativeTextureAlpha=true; // The selected runtime descriptor differs from the original model skin.
        replacement->rgba.resize(24);
        for (unsigned pixel = 0; pixel < 6; ++pixel)
            replacement->rgba[pixel * 4 + 1] = replacement->rgba[pixel * 4 + 3] = 255;
        engine.bitmapImages[firstBitmap.bits] = replacement;
        prepared(actor, 1, 2048);
        auto boundImage = [&](const Image* source) {
            for (const auto& binding : engine.frame().skinBindings) if (binding.source == source) {
                assert(!binding.dynamic && binding.serial == 0);
                assert(binding.nativeTextureAlpha==binding.image->nativeTextureAlpha);
                return binding.image;
            }
            return source;
        };
        assert(boundImage(firstImage.get()) == replacement.get());
        assert(boundImage(secondImage.get()) == secondImage.get());
        assert(model_material(model, 0, 1)->texture.get() == secondImage.get());
        assert(bitmapFor(actor, 1024).bits == firstBitmap.bits &&
               engine.bitmapImages.at(firstBitmap.bits) == replacement);
        close(engine.field(firstBitmap.bits, "width"), 2);
        close(engine.field(firstBitmap.bits, "height"), 3);
        select(actor, 1024); prepared(actor, 0, 1024);
        assert(boundImage(model_material(model, 0, 0)->texture.get()) == replacement.get());
        const auto sharedActor = engine.create_entity(runtime, "two-skins.mdl", {150, 0, 0}, 0);
        assert(engine.entities.at(sharedActor).model == &model);
        assert(bitmapFor(sharedActor, 1024).bits == firstBitmap.bits &&
               bitmapFor(sharedActor, 2048).bits == secondBitmap.bits);
        assert(engine.bitmapImages.at(firstBitmap.bits) == replacement);
        close(engine.field(firstBitmap.bits, "width"), 2);
        close(engine.field(firstBitmap.bits, "height"), 3);
        prepared(sharedActor, 0, 1024);
        assert(boundImage(model_material(model, 0, 0)->texture.get()) == replacement.get());
        runtime.start("second_skin", {}, sharedActor, 0);
        prepared(sharedActor, 1, 2048);
        assert(engine.global("selected_bitmap").bits == secondBitmap.bits &&
               boundImage(model_material(model, 0, 1)->texture.get()) == secondImage.get());
        // A WMB face still selects its own material, never the ENTITY.SKIN bank.
        Model worldModel;
        worldModel.formatVersion = 6;
        worldModel.materials = model.materials;
        assert(model_material(worldModel, 0, 1) == &worldModel.materials[0]);
        assert(model_material(worldModel, 1, 0) == &worldModel.materials[1]);
        assert(model_material(worldModel, 1, 0)->texture == secondImage);
        assert(model_material(model, 1, 0)->texture == firstImage);
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        write_animation_model(temporary.path / "animation.mdl");
        const std::array<float, 6> offsets{0, 10, 20, 40, 80, 160};
        Assets assets(temporary.path);
        Media media(assets, false);
        for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
            State state; state.profile = profile;
            const bool bonus = profile == SemanticProfile::BonusA7;
            Engine engine(assets, nullptr, media, state);
            Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                            std::string_view name, std::span<const Argument> args) {
                return engine.builtin(vm, context, name, args);
            });
            if (bonus) {
                close(engine.number("ANM_SKIP"), 4);
                assert(state.compilerMacros.at("ANM_SKIP") == "4");
            } else {
                assert(!state.globals.contains("ANM_SKIP") && !state.compilerMacros.contains("ANM_SKIP"));
            }
            std::string program = R"wdl(
                function animate(scene,progress,mode) { return ent_animate(my,scene,progress,mode); }
            )wdl";
            if (bonus) program += R"wdl(
                DEFINE ANM_SKIP,0;
                function snap(progress) { return ent_animate(my,"RuN",progress,nullvector+ANM_SKIP); }
                function wrap_snap(progress) { return ent_animate(my,"RuN",progress,ANM_CYCLE+ANM_SKIP); }
            )wdl";
            runtime.load_text("model-animation.wdl", program);
            if (bonus) {
                assert(std::any_of(runtime.diagnostics().begin(), runtime.diagnostics().end(),
                    [](const auto& diagnostic) {
                        return diagnostic.find("preserving protected compiler DEFINE anm_skip") != std::string::npos;
                    }));
                close(engine.number("ANM_SKIP"), 4);
            }
            // Birth follows the only load, so MY and the real model resource remain live.
            const auto actor = engine.create_entity(runtime, "animation.mdl", {100, 0, 0}, 0);
            const auto& data = engine.entities.at(actor);
            const auto& model = *data.model;
            assert(model.formatVersion == 7 && model.frames.size() == 6 &&
                   model.vertices.size() == 3 && model.triangles.size() == 1);
            for (std::size_t frame = 0; frame < offsets.size(); ++frame)
                close_vector(model.frames[frame].positions[0], {offsets[frame], 0, 0});
            auto animate = [&](std::string_view scene, Value percent, Value mode) {
                const std::array args{Argument{engine.new_string(std::string(scene)), {}},
                                      Argument{percent, {}}, Argument{mode, {}}};
                return runtime.start("animate", args, actor, 0);
            };
            auto pose = [&](int32_t frameRaw, int32_t nextRaw, uint32_t cacheNext, float x) {
                assert(state.member(actor, "frame").get().raw() == frameRaw);
                assert(state.member(actor, "next_frame").get().raw() == nextRaw);
                const auto first = uint32_t(frameRaw / 1024 - 1);
                const auto next = nextRaw ? uint32_t(nextRaw / 1024 - 1) : std::min(first + 1, 5u);
                const float blend = float(frameRaw % 1024) / 1024;
                assert(data.frame0 == first && data.frame1 == cacheNext);
                close(data.frameBlend, blend);
                close_vector(worldModelVertex(engine, actor, data, 0), {x, 0, 0});
                engine.prepare(runtime);
                bool published = false;
                for (const auto& draw : engine.frame().instances) if (draw.objectId == actor) {
                    assert(draw.model == &model && draw.frame == first && draw.nextFrame == next);
                    close(draw.interpolation, blend);
                    const auto a = draw.model->frames[draw.frame].positions[0];
                    const auto b = draw.model->frames[draw.nextFrame].positions[0];
                    close_vector({a.x * (1 - draw.interpolation) + b.x * draw.interpolation,
                                  a.y * (1 - draw.interpolation) + b.y * draw.interpolation,
                                  a.z * (1 - draw.interpolation) + b.z * draw.interpolation}, {x, 0, 0});
                    published = true;
                }
                assert(published && "animated model actor missing from prepared instances");
            };
            animate("RuN", Value::from_double(37.5), Value::from_int(0));
            pose(2816, 0, 2, 17.5f); close(data.animation, 37.5);
            animate("RuN", Value::from_double(87.5), Value::from_int(1));
            pose(4736, 2048, 1, 21.25f); close(data.animation, 87.5);
            for (int percent : {-25, 0}) {
                animate("RuN", Value::from_int(percent), Value::from_int(0));
                pose(2048, 0, 2, 10);
            }
            for (int percent : {100, 125}) {
                animate("RuN", Value::from_int(percent), Value::from_int(0));
                pose(4096, 0, bonus ? 4 : 3, 40);
            }
            animate("RuN", Value::from_double(37.5), Value::from_int(0));
            assert(!animate("absent", Value::from_int(80), Value::from_int(0)));
            pose(2816, 0, 2, 17.5f); close(data.animation, 37.5);
            if (bonus) {
                const std::array fraction{Argument{Value::from_double(37.5), {}}};
                runtime.start("snap", fraction, actor, 0);
                pose(2048, 0, 2, 10); // .75 snaps toward zero, not the nearer run1.
                const std::array wrap{Argument{Value::from_double(87.5), {}}};
                runtime.start("wrap_snap", wrap, actor, 0);
                pose(4096, 2048, 1, 40); // Preserve pre-snap last→first NEXT_FRAME.
                for (int percent : {-25, 0, 100, 125}) {
                    const std::array args{Argument{Value::from_int(percent), {}}};
                    runtime.start("snap", args, actor, 0);
                    pose(percent <= 0 ? 2048 : 4096, 0, percent <= 0 ? 2 : 4, percent <= 0 ? 10 : 40);
                }
                animate("RuN", Value::from_raw(10049), Value::from_int(0));
                pose(2248, 0, 2, 11.953125f); // Raw percent is not truncated to its integer part.
                animate("RuN", Value::from_raw(10049), Value::from_raw(5119));
                pose(2048, 0, 2, 10); // Numeric mode uses SAR 10: 4.999... is ANM_SKIP.
            } else {
                bool rejected = false;
                try { animate("RuN", Value::from_double(37.5), Value::from_int(4)); }
                catch (const std::runtime_error&) { rejected = true; }
                assert(rejected);
                pose(2816, 0, 2, 17.5f);
            }
        }
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        // Real serialized RGB24 pixels; shared ordinary UI and cursor must borrow the same image.
        std::vector<std::uint8_t> pixels(18 + 12 * 24 * 3, 255);
        std::fill_n(pixels.begin(), 18, 0);
        pixels[2] = 2; pixels[12] = 12; pixels[14] = 24; pixels[16] = 24; pixels[17] = 32;
        {std::ofstream file(temporary.path / "cursor.tga", std::ios::binary);
         assert(file.write(reinterpret_cast<const char*>(pixels.data()), std::streamsize(pixels.size())).good());}
        for (bool starter : {false, true}) {
            Assets assets(temporary.path, true); Media media(assets, false);
            State state; state.profile = SemanticProfile::BonusA7;
            Engine engine(assets, nullptr, media, state);
            Runtime runtime(state, [&](Runtime& vm, ProcessContext& context, std::string_view name,
                                       std::span<const Argument> args) {return engine.builtin(vm, context, name, args);});
            assert(!engine.global("mouse_map"));
            std::string script = R"wdl(
                BMAP cursor = <cursor.tga>;
                VAR clicks=0; VAR releases=0; VAR physical_x=0; VAR canonical_x=0;
                VAR manual=1;
                function clicked() { clicks+=1; }
                function released() { releases+=1; }
                PANEL menu { LAYER=999; VISIBLE=ON;
                    BUTTON=16,24,cursor,cursor,cursor,clicked,released,NULL; }
                function main() { mouse_map=cursor; mouse_pointer=0; mouse_mode=2; }
                function sample_alias() { physical_x=POINTER.X; canonical_x=MOUSE_CURSOR.X; }
            )wdl";
            // The actual Bonus starter's polling body, not a native automatic position copy.
            script += starter ? R"wdl(
                starter mouse_point_strtr {
                    while(1) {
                        if(mouse_mode!=0) { MOUSE_POS.X=POINTER.X; MOUSE_POS.Y=POINTER.Y; }
                        wait(1);
                    }
                }
            )wdl" : R"wdl(
                starter scripted_position {
                    while(1) {
                        if(manual) { MOUSE_POS.X=10.75; MOUSE_POS.Y=20.25; }
                        wait(1);
                    }
                }
            )wdl";
            runtime.load_text("bonus-cursor-consumer.wdl", script); runtime.bootstrap();
            engine.set_vector(engine.global_vector("mouse_spot"), starter ? Vec3{} : Vec3{5.5f,3.75f,0});
            Input input; input.dt=1.0/60.0; input.mouseX=starter?16:90; input.mouseY=starter?24:100;
            engine.tick(runtime,input);
            runtime.start("sample_alias");
            close(engine.number("physical_x"),input.mouseX); close(engine.number("canonical_x"),input.mouseX);
            const auto selected=engine.global("mouse_map");
            auto cursorCommand = [&]() -> const UICommand& {
                assert(!engine.frame().ui.empty());
                const auto& c=engine.frame().ui.back();assert(c.cursorOverlay && c.overlay && !c.filter);
                close(c.rect.width,12);close(c.rect.height,24);close(c.color.a,1);
                assert(c.image==engine.frame().ui.front().image && c.uv.x==0 && c.uv.y==0 && c.uv.width==1 && c.uv.height==1);
                return c;
            };
            close(cursorCommand().rect.x,starter?16:10);close(cursorCommand().rect.y,starter?24:20);
            assert(cursorCommand().textureMode==UICommand::TextureMode::CursorKey16);
            // Freeze1 and ENABLE_MOUSE=0 must not suppress panel clicks at the additive logical hotspot.
            engine.set_global("freeze_mode",Value::from_int(1));engine.set_global("enable_mouse",{});
            input.mousePressed[SDL_BUTTON_LEFT]=true;input.mouseButtons[SDL_BUTTON_LEFT]=true;
            engine.tick(runtime,input);close(engine.number("clicks"),1);close(engine.number("mouse_left"),1);
            input.mousePressed.reset();input.mouseButtons.reset();input.mouseReleased[SDL_BUTTON_LEFT]=true;
            engine.tick(runtime,input);close(engine.number("releases"),1);close(engine.number("mouse_left"),0);
            input.mouseReleased.reset();input.mousePressed[SDL_BUTTON_LEFT]=true;input.mouseX=1280;
            engine.tick(runtime,input);assert(!engine.global("mouse_valid"));close(engine.number("clicks"),1);
            runtime.start("sample_alias");close(engine.number("physical_x"),1280); // Unclamped physical half-open edge.
            input.mousePressed.reset();input.mouseX=90;input.mouseY=100;
            engine.set_global("mouse_mode",{});engine.set_vector(engine.global_vector("mouse_pos"),{77,88,0});
            engine.set_global("manual",{});engine.tick(runtime,input);
            assert(engine.global("mouse_map").bits==selected.bits && !engine.frame().ui.back().cursorOverlay);
            close_vector(engine.vector(engine.global_vector("mouse_pos")),{77,88,0});
            engine.set_global("mouse_mode",Value::from_int(2));engine.set_global("manual",Value::from_int(1));
            engine.set_global("D3D_TEXDEPTH",Value::from_raw(16385));engine.tick(runtime,input);
            assert(cursorCommand().textureMode==UICommand::TextureMode::CursorKey24);
            close(cursorCommand().rect.x,starter?90:10); // Reopening keeps selection and script position policy.
            ProcessContext context;const std::array args{Argument{selected,{}}};
            engine.builtin(runtime,context,"bmap_purge",args);engine.tick(runtime,input);
            assert(cursorCommand().image->width==12); // Existing resolver reloads after purge, no cursor ownership.
            engine.set_global("manual",{});engine.set_global("mouse_mode",Value::from_int(1));
            if (!starter) {
                engine.set_vector(engine.global_vector("mouse_pos"),{-1,1000,0});engine.tick(runtime,input);
                close_vector(engine.vector(engine.global_vector("mouse_pos")),{0,719,0});
                engine.set_global("mouse_mode",Value::from_int(3));engine.tick(runtime,input);
                close_vector(engine.vector(engine.global_vector("mouse_pos")),{90,100,0});
            }
            engine.set_global("mouse_mode",{});engine.set_field(engine.global("menu").bits,"visible",0);state.retire(selected.bits);engine.tick(runtime,input);
            assert(engine.global("mouse_map").bits==selected.bits);
            engine.set_global("mouse_mode",Value::from_int(2));bool rejected=false;
            try {engine.tick(runtime,input);}catch(const std::runtime_error&){rejected=true;}
            assert(rejected); // Mode0 hides a retained selection; reopening never invents a replacement arrow.
        }
        {
            // One real UI surface exercises the genuine A7 bare/comma/OR flag spellings.
            Assets assets(temporary.path, true); Media media(assets, false);
            State state; state.profile=SemanticProfile::BonusA7;
            Engine engine(assets,nullptr,media,state);
            Runtime runtime(state,[&](Runtime& vm,ProcessContext& context,std::string_view name,
                                     std::span<const Argument> args){return engine.builtin(vm,context,name,args);});
            const auto playerName=engine.global("PLAYER_NAME");
            assert(playerName.kind==Value::Kind::Object && playerName.bits);
            assert(state.require(playerName.bits).alive && state.require(playerName.bits).kind==Object::Kind::String);
            assert(!state.require(playerName.bits).nullStringBuffer && engine.string(playerName).empty());
            runtime.load_text("bonus-filter-consumer.wdl",R"wdl(
                DEFINE FILTER,0;
                BMAP surface=<cursor.tga>;
                FONT glyphs=<cursor.tga>,4,4;
                PANEL panel_bare { LAYER=11; BMAP=surface; FLAGS=FILTER; VISIBLE=ON; }
                PANEL panel_comma { LAYER=12; BMAP=surface; FLAGS=FILTER,TRANSPARENT; VISIBLE=ON; }
                PANEL panel_or { LAYER=13; BMAP=surface; FLAGS=FILTER|TRANSPARENT; VISIBLE=ON; }
                TEXT text_bare { LAYER=14; FONT=glyphs; STRING="A"; FLAGS=FILTER; VISIBLE=ON; }
                TEXT text_comma { LAYER=15; FONT=glyphs; STRING="A"; FLAGS=FILTER,TRANSPARENT; VISIBLE=ON; }
                TEXT text_or { LAYER=16; FONT=glyphs; STRING="A"; FLAGS=FILTER|TRANSPARENT; VISIBLE=ON; }
                function read_player_name() { return str_cmpi(PLAYER_NAME,""); }
                function set_filter(owner,enabled) { owner.filter=enabled; }
            )wdl");
            close(runtime.start("read_player_name").real(),1);
            assert(engine.global("PLAYER_NAME").bits==playerName.bits);
            assert(engine.global("FILTER").raw()==0x100000);
            engine.prepare(runtime);
            auto command=[&](ObjectId owner)->const UICommand&{
                const auto layer=engine.field(owner,"layer");
                const auto& ui=engine.frame().ui;
                auto found=std::find_if(ui.begin(),ui.end(),[&](const auto& draw){return draw.layer==layer;});
                assert(found!=ui.end() && found->image && !found->cursorOverlay);
                assert(std::count_if(ui.begin(),ui.end(),[&](const auto& draw){return draw.layer==layer;})==1);
                return *found;
            };
            for(auto name:{"panel_bare","panel_comma","panel_or","text_bare","text_comma","text_or"}){
                const auto owner=engine.global(name).bits;
                const auto initial=engine.flags(owner);
                assert(initial==(0x100000u|0x4000u|(std::string_view(name).ends_with("bare")?0u:0x400u)));
                assert(state.member(owner,"filter").bitMask==0x100000 && command(owner).filter);
                for(bool enabled:{false,true}){
                    const std::array args{Argument{Value::object(owner),{}},Argument{Value::from_int(enabled),{}}};
                    runtime.start("set_filter",args);engine.prepare(runtime);
                    assert(engine.flags(owner)==(enabled?initial:initial&~0x100000u));
                    assert(command(owner).filter==enabled);
                }
            }
        }
    }
    {
        // A6 004447e0/0040ce80: a 32-bit TGA (BMAP0x100) panel bitmap blends with alpha min(2*round(ALPHA*2611/1024),255)/255
        // even without TRANSPARENT; a 24-bit one keeps ALPHA/100 under TRANSPARENT only.
        TempDirectory temporary;
        for(unsigned depth:{24u,32u}){std::vector<std::uint8_t> tga(18,0);tga[2]=2;tga[12]=1;tga[14]=1;tga[16]=std::uint8_t(depth);tga[17]=depth==32?8:0;tga.resize(18+depth/8,255);
            std::ofstream file(temporary.path/(std::to_string(depth)+".tga"),std::ios::binary);assert(file.write(reinterpret_cast<const char*>(tga.data()),std::streamsize(tga.size())).good());}
        Assets assets(temporary.path); Media media(assets,false); State state;
        Engine engine(assets,nullptr,media,state);
        Runtime runtime(state,[&](Runtime& vm,ProcessContext& context,std::string_view name,std::span<const Argument> args){return engine.builtin(vm,context,name,args);});
        runtime.load_text("a6-alpha-format.wdl",R"wdl(
            BMAP rgb=<24.tga>; BMAP argb=<32.tga>;
            PANEL rgb40 { LAYER=1; BMAP=rgb; ALPHA=40; FLAGS=TRANSPARENT; VISIBLE=ON; }
            PANEL argb40 { LAYER=2; BMAP=argb; ALPHA=40; FLAGS=TRANSPARENT; VISIBLE=ON; }
            PANEL argb18 { LAYER=3; BMAP=argb; ALPHA=18; VISIBLE=ON; }
            PANEL argb70 { LAYER=4; BMAP=argb; ALPHA=70; FLAGS=TRANSPARENT; VISIBLE=ON; }
        )wdl");
        engine.prepare(runtime);
        auto alpha=[&](float layer){const auto& ui=engine.frame().ui;auto c=std::find_if(ui.begin(),ui.end(),[&](const auto& d){return d.layer==layer;});assert(c!=ui.end());return double(c->color.a);};
        close(alpha(1),.4,1e-6);close(alpha(2),202/255.,1e-6);close(alpha(3),90/255.,1e-6);close(alpha(4),1,1e-6);
    }
    {
        TempDirectory temporary;
        auto write=[&](std::string_view name,const auto& bytes){std::ofstream file(temporary.path/std::string(name),std::ios::binary);assert(file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size())).good());};
        // Indexed PCX deliberately has a black embedded BMAP palette. FONT uses
        // the independently selected/normalized active RGB palette, not alpha=index.
        std::vector<std::uint8_t> pcx(128,0);pcx[0]=10;pcx[1]=5;pcx[2]=1;pcx[3]=8;pcx[8]=7;pcx[10]=3;pcx[65]=1;pcx[66]=8;pcx[68]=1;
        for(int y=0;y<4;++y)for(int x=0;x<8;++x){const auto index=std::array<std::uint8_t,4>{0,1,2,255}[x%4];if(index>=192)pcx.push_back(193);pcx.push_back(index);}
        pcx.push_back(12);pcx.resize(pcx.size()+768);write("indexed.pcx",pcx);
        std::array<std::uint8_t,768> inputPalette{};inputPalette[3]=64;inputPalette[4]=128;inputPalette[5]=192;inputPalette[6]=1;inputPalette[7]=2;inputPalette[8]=3;
        write("custom.raw",inputPalette);auto fallback=inputPalette;fallback[3]=190;write("palette.raw",fallback);
        auto worldPalette=inputPalette;worldPalette[3]=200;worldPalette[4]=40;worldPalette[5]=60;
        std::vector<std::uint8_t> worldBytes(140+16+768,0);std::memcpy(worldBytes.data(),"WMB6",4);
        for(unsigned shift=0;shift<32;shift+=8){worldBytes[4+shift/8]=std::uint8_t(140u>>shift);worldBytes[8+shift/8]=std::uint8_t(784u>>shift);}
        std::copy(worldPalette.begin(),worldPalette.end(),worldBytes.begin()+156);write("palette.wmb",worldBytes);
        std::vector<std::uint8_t> noPalette(140,0);std::memcpy(noPalette.data(),"WMB6",4);write("retained.wmb",noPalette);
        Assets assets(temporary.path);Media media(assets,false);State state;Engine engine(assets,nullptr,media,state);
        Runtime runtime(state,[&](Runtime& vm,ProcessContext& context,std::string_view name,std::span<const Argument> args){return engine.builtin(vm,context,name,args);});
        runtime.load_text("dir/custom.wdl",R"wdl(
            BMAP ordinary=<indexed.pcx>;FONT font=<indexed.pcx>,4,4;
            PANEL bitmap {LAYER=10;BMAP=ordinary;VISIBLE=ON;}
            TEXT label {LAYER=11;FONT=font;STRING="01";ALPHA=50;FLAGS=TRANSPARENT;VISIBLE=ON;}
            function palette_world(){level_load("palette.wmb");}
            function retained_world(){level_load("retained.wmb");}
        )wdl");
        assert(runtime.compiler_settings().entry=="dir/custom.wdl");engine.prepare(runtime);
        auto commands=[&](float layer){std::vector<UICommand> result;for(const auto& command:engine.frame().ui)if(command.layer==layer)result.push_back(command);return result;};
        const auto bmap=commands(10);auto glyphs=commands(11);assert(bmap.size()==1&&glyphs.size()==2);
        for(std::size_t p=0;p<bmap[0].image->rgba.size();p+=4){const auto& bytes=bmap[0].image->rgba;assert(bytes[p]==0&&bytes[p+1]==0&&bytes[p+2]==0&&bytes[p+3]==255);}
        const Image* atlas=glyphs[0].image;const auto serial=glyphs[0].imageSerial;
        auto color=[&](std::size_t pixel,std::array<std::uint8_t,4> expected){
            const auto* rgba=atlas->rgba.data()+pixel*4;
            if(!std::equal(expected.begin(),expected.end(),rgba)){
                std::cerr<<"FONT color pixel="<<pixel<<" actual="<<int(rgba[0])<<','<<int(rgba[1])<<','<<int(rgba[2])<<','<<int(rgba[3])<<" expected="<<int(expected[0])<<','<<int(expected[1])<<','<<int(expected[2])<<','<<int(expected[3])<<'\n';
            }
            assert(std::equal(expected.begin(),expected.end(),rgba));
        };
        color(0,{0,0,0,0});color(1,{64,128,192,255});color(2,{8,8,8,255});color(3,{255,255,255,255});
        assert(atlas->sourceDepth==8&&atlas->nativeStorageBytes==1&&!atlas->nativeTextureAlpha);
        for(const auto& glyph:glyphs){assert(glyph.dynamicImage&&glyph.image==atlas);close(glyph.color.a,.5);close(glyph.rect.width,4);close(glyph.rect.height,4);}
        close(glyphs[0].uv.x,0);close(glyphs[1].uv.x,.5);close(glyphs[0].uv.width,.5);
        auto paletteDraw=[&](std::string_view stage,std::uint64_t previous,bool changed){
            glyphs=commands(11);
            if(glyphs.size()!=2||glyphs[0].image!=atlas||(changed?glyphs[0].imageSerial<=previous:glyphs[0].imageSerial!=previous)){
                std::cerr<<"FONT palette stage="<<stage<<" glyphs="<<glyphs.size()<<" atlas="<<atlas<<" emitted="<<(glyphs.empty()?nullptr:glyphs[0].image)<<" serial="<<(glyphs.empty()?0:glyphs[0].imageSerial)<<" previous="<<previous<<" world="<<engine.world<<" section0="<<(engine.world?engine.world->sections[0].size():0)<<'\n';
                if(engine.world&&engine.world->sections[0].size()>=784){const auto* p=engine.world->sections[0].data()+19;std::cerr<<"FONT world palette index1 RGB="<<int(p[0])<<','<<int(p[1])<<','<<int(p[2])<<'\n';}
                const auto* p=atlas->rgba.data()+4;std::cerr<<"FONT CPU ink index1 RGBA="<<int(p[0])<<','<<int(p[1])<<','<<int(p[2])<<','<<int(p[3])<<'\n';
            }
            assert(glyphs.size()==2);color(1,{200,40,60,255});
            assert(glyphs[0].image==atlas);
            assert(changed?glyphs[0].imageSerial>previous:glyphs[0].imageSerial==previous);
        };
        Input idle;
        runtime.start("palette_world");engine.tick(runtime,idle);paletteDraw("changed",serial,true);
        const auto worldSerial=glyphs[0].imageSerial;
        runtime.start("retained_world");engine.tick(runtime,idle);paletteDraw("retained",worldSerial,false);
        const auto ordinary=commands(10);assert(ordinary.size()==1&&ordinary[0].image==bmap[0].image);
        for(std::size_t p=0;p<ordinary[0].image->rgba.size();p+=4)assert(ordinary[0].image->rgba[p]==0&&ordinary[0].image->rgba[p+1]==0&&ordinary[0].image->rgba[p+2]==0);
    }
    {
        TempDirectory temporary;
        SaveEnvironment saveEnvironment(temporary.path / "private");
        std::vector<std::uint8_t> original(31);
        for (std::size_t i = 0; i < original.size(); ++i) original[i] = std::uint8_t(i * 7);
        auto writeOptions = [](const std::filesystem::path& path,
                               const std::vector<std::uint8_t>& bytes) {
            std::ofstream output(path, std::ios::binary);
            assert(output.write(reinterpret_cast<const char*>(bytes.data()),
                                std::streamsize(bytes.size())).good());
        };
        auto readOptions = [](const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            assert(input.good());
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                             std::istreambuf_iterator<char>());
        };
        const auto source = temporary.path / "options.k22";
        writeOptions(source, original);
        Assets assets(temporary.path);
        Media media(assets, false);
        State state;
        Engine engine(assets, nullptr, media, state);
        Runtime runtime(state, [&engine](Runtime& runtime, ProcessContext& context,
                                        std::string_view name, std::span<const Argument> args) {
            return engine.builtin(runtime, context, name, args);
        });
        runtime.load_text("language-regression.wdl", "function load_subtitle() {}");
        const auto destination = engine.saveRoot / "options.k22";
        auto expected = original;
        expected.push_back(2);
        engine.configure_language(runtime, "de", false);
        assert(readOptions(destination) == expected && readOptions(source) == original);
        expected[31] = 5;
        writeOptions(destination, expected);
        engine.configure_language(runtime, "en", false);
        assert(readOptions(destination) == expected);
        engine.configure_language(runtime, "tr", true);
        expected[31] = 0;
        assert(readOptions(destination) == expected && readOptions(source) == original);
        writeOptions(destination, original);
        engine.configure_language(runtime, "en", false);
        expected[31] = 1;
        assert(readOptions(destination) == expected);
        writeOptions(destination, original);
        auto blockedTemporary = destination;
        blockedTemporary += ".tmp." + std::to_string(::getpid());
        std::filesystem::create_symlink(source, blockedTemporary);
        bool failed = false;
        try { engine.configure_language(runtime, "de", false); }
        catch (const std::exception&) { failed = true; }
        assert(failed && readOptions(destination) == original && readOptions(source) == original);
    }
    if (argc == 5) campaign_persistence(argv[4]);
    std::cout << "engine collision/trace/vector/options/payload/string regression passed\n";
}
