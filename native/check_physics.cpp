#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine.hpp"
#include "wdl.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {
using namespace k22;
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "k22-physics-XXXXXX").string();
        const auto* directory = ::mkdtemp(pattern.data());
        if (!directory) throw std::system_error(errno, std::generic_category(), "mkdtemp");
        path = directory;
    }
    ~TempDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
struct Environment {
    const char* name;
    std::optional<std::string> previous;
    Environment(const char* variable, const std::filesystem::path& path) : name(variable) {
        if (const auto* value = std::getenv(name)) previous = value;
        if (::setenv(name, path.c_str(), 1))
            throw std::system_error(errno, std::generic_category(), "setenv");
    }
    ~Environment() {
        if (previous) ::setenv(name, previous->c_str(), 1);
        else ::unsetenv(name);
    }
};
void close(double actual, double expected, double tolerance = .002) {
    assert(std::isfinite(actual) && std::abs(actual - expected) < tolerance);
}

void retain_world_xyz(World& world) {
    auto& bytes = world.sections[3];
    bytes.clear();
    bytes.reserve(world.collision.vertices.size() * 12);
    for (const auto point : world.collision.vertices) for (float coordinate : {point.x, point.y, point.z}) {
        const auto bits = std::bit_cast<std::uint32_t>(coordinate);
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(std::uint8_t(bits >> shift));
    }
}

// Observe real contact rows through ODE's allocator/feedback API, without exposing
// Physics internals. The fresh world's first solver allocation occurs after the
// body-batch flush and before row evaluation.
dBodyID contactProbeBody=nullptr;
std::vector<dJointFeedback> contactFeedback;
std::array<dReal,3> contactProbeCenter{};
void* contactProbeAllocate(dsizeint size) {
    if (contactProbeBody) {
        const int count=dBodyGetNumJoints(contactProbeBody);
        if (contactFeedback.empty()) contactFeedback.resize(std::size_t(count));
        assert(contactFeedback.size()==std::size_t(count));
        const auto* p=dBodyGetPosition(contactProbeBody);
        std::copy_n(p,3,contactProbeCenter.begin());
        for (int i=0;i<count;++i) {
            const auto joint=dBodyGetJoint(contactProbeBody,i);
            assert(dJointGetType(joint)==dJointTypeContact);
            assert(dJointGetBody(joint,0)==contactProbeBody && !dJointGetBody(joint,1));
            dJointSetFeedback(joint,&contactFeedback[std::size_t(i)]);
        }
    }
    return dAlloc(size);
}
}

int main(int argc, char** argv) {
    static_assert(sizeof(dReal) == sizeof(float));
    if (argc != 2) { std::cerr << "usage: check_physics <yr.hmp>\n"; return 2; }
    const std::filesystem::path terrainPath(argv[1]);
    TempDirectory temporary;
    Environment data("XDG_DATA_HOME", temporary.path);
    Environment ipc("XDG_RUNTIME_DIR", temporary.path); // mkdtemp creates a private 0700 directory.
    Assets assets(terrainPath.parent_path(), true);
    Media media(assets, false);
    State state;
    state.profile = SemanticProfile::BonusA7;
    Engine engine(assets, nullptr, media, state);
    engine.set_global("enable_polycollision", Value::from_int(1));
    Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                    std::string_view name, std::span<const Argument> args) {
        return engine.builtin(vm, context, name, args);
    });
    runtime.load_text("physics-consumer.wdl", R"wdl(
        function push(body,force) { phent_addcentralforce(body,force.x); }
        function spin(body,point) { phent_addvellocal(body,vector(0,1,0),point.x); }
        function contact(receiver,other) {
            my.skill1 += 1;
            my.skill2 = receiver == my;
            my.skill3 = (other == you) && (you == 0);
            my.skill4 = (_arg1 == my) && (_arg2 == you);
            my.skill5 = event_type;
            my.skill6 = target.z;
            my.skill7 = target.x;
        }
    )wdl");
    assert(runtime.diagnostics().empty());
    ProcessContext context;
    auto number = [](double n) { return Argument{Value::from_double(n), {}}; };
    auto object = [](ObjectId id) { return Argument{Value::object(id), {}}; };
    auto call = [&](std::string_view name, std::initializer_list<Argument> args) {
        return engine.builtin(runtime, context, name, {args.begin(), args.size()}).integer();
    };
    auto vector = [&](Vec3 value) {
        const auto id = state.create(Object::Kind::Vector).id;
        state.require(id).array.resize(3);
        engine.set_vector(object(id), value);
        return id;
    };
    auto borrowed = [&](ObjectId id) { return Argument{Value::from_int(-999), state.member(id, "x")}; };
    auto gravity = [&](Vec3 value) {
        const auto id = vector(value);
        assert(call("ph_setgravity", {borrowed(id)}) == 1);
    };
    // Borrowed MODEL resources key persistent query contexts. Keep them alive
    // together: reusing one expired stack address aliases a different resource.
    Model model, slab, tall;
    model.min = {2,-1,-3}; model.max = {6,3,1};
    for (float x : {2.f,6.f}) for (float y : {-1.f,3.f}) for (float z : {-3.f,1.f})
        model.vertices.push_back({{x,y,z}, {}, {}, {}});
    auto body = [&](Vec3 position, Vec3 angles = Vec3{}) {
        const auto id = state.create(Object::Kind::Entity).id;
        auto& data = engine.entities.try_emplace(id).first->second;
        data.model = &model;
        data.type = 2;
        data.physicsGeometry = true;
        engine.set_position(id, position);
        engine.set_field(id, "pan", angles.x);
        engine.set_field(id, "tilt", angles.y);
        engine.set_field(id, "roll", angles.z);
        engine.set_field(id, "scale_x", 2);
        engine.set_field(id, "scale_y", 1.5);
        engine.set_field(id, "scale_z", .5);
        // Deliberately unrelated script bounds: PH_BOX must use scaled model vertices.
        for (auto field : {"min_x","min_y","min_z"}) engine.set_field(id, field, -99);
        for (auto field : {"max_x","max_y","max_z"}) engine.set_field(id, field, 99);
        engine.prepare(runtime); // Genuine persistent query context, before the PH_BOX exists.
        assert(!engine.physics.query_body_geometry(id));
        assert(call("phent_settype", {object(id), number(1), number(0)}) == 1);
        assert(call("phent_setmass", {object(id), number(2), number(0)}) == 1);
        assert(call("phent_setfriction", {object(id), number(100)}) == 1);
        assert(call("phent_setelasticity", {object(id), number(0), number(0)}) == 1);
        return id;
    };
    auto ticks = [&](int count) { for (int i = 0; i < count; ++i) engine.physics.step(runtime); };
    engine.set_global("ph_fps_max_lock", Value::from_int(64));
    engine.set_global("ph_iterations", {});
    engine.set_global("time", Value::from_double(.25)); // Exact 1/64-second substep.
    engine.set_global("freeze_mode", {});

    // Sky-marked solid floor stays collidable; the raw 0x40 partition above it is passable.
    // Scaled off-center model min_z is -1.5, independently of the script bounds.
    World floor;
    floor.collision.vertices = {{-1000,-1000,0},{1000,-1000,0},{0,1000,0},
                                {-1000,-1000,10},{1000,-1000,10},{0,1000,10}};
    floor.collision.triangles = {{0,1,2},{3,4,5}};
    floor.collision.flags = {0x5,0x40};
    retain_world_xyz(floor);
    engine.world = &floor;
    gravity({0,0,-32});
    {
        // Authentic basla.wmb face596/triangle1773 occurs at native25221 and25603.
        // Rotation is DERIVED from retained normals, not a captured failing935/53 pose.
        const std::array vertices{Vec3{-558,403,-177},Vec3{-563,223,-177},Vec3{-585,223,-177}};
        const std::array<dTriIndex,6> indices{0,1,2,0,1,2};
        const dMatrix3 rotation{
            .12827713787555695f,-.8532620072364807f,-.505459189414978f,0,
            -.8142618536949158f,-.38156720995903015f,.43747463822364807f,0,
            -.5661470890045166f,.35545814037323f,-.743725061416626f,0};
        const auto box=dCreateBox(nullptr,23.911157608032227f,23.911415100097656f,23.616233825683594f);
        dGeomSetPosition(box,-567.3906860351562f,208.7727508544922f,-171.95106506347656f);
        dGeomSetRotation(box,rotation);
        assert(!dGeomGetBody(box));
        std::array<dTriMeshDataID,2> meshData{};
        std::array<dGeomID,2> meshes{};
        std::array<std::array<dContactGeom,100>,2> contacts{};
        std::array<int,2> counts{};
        std::array<int,2> firstTriangles{};
        static int firstTriangle=-1;
        const auto observeTriangle=+[](dGeomID,dGeomID,int triangle) {
            if(firstTriangle<0)firstTriangle=triangle;
            return 1;
        };
        for(std::size_t query=0;query<meshes.size();++query) {
            meshData[query]=dGeomTriMeshDataCreate();
            dGeomTriMeshDataBuildSingle(meshData[query],vertices.data(),sizeof(Vec3),int(vertices.size()),
                                       indices.data(),int(3*(query+1)),3*sizeof(dTriIndex));
            meshes[query]=dCreateTriMesh(nullptr,meshData[query],observeTriangle,nullptr,nullptr);
            firstTriangle=-1;
            counts[query]=dCollide(meshes[query],box,int(contacts[query].size()),
                                  contacts[query].data(),sizeof(dContactGeom));
            firstTriangles[query]=firstTriangle;
            assert(firstTriangle>=0&&firstTriangle<int(query+1));
        }
        assert(counts[0]>0&&counts[0]<int(contacts[0].size()));
        constexpr std::array<std::uint32_t,3> expectedNormal{0x3e035b15,0xbf507377,0xbf10ef04};
        bool suppliedNormal=false;
        for(int i=0;i<counts[0];++i) {
            const auto& c=contacts[0][std::size_t(i)];
            const auto sign=std::bit_cast<std::uint32_t>(c.normal[0])^expectedNormal[0];
            bool matches=sign==0||sign==0x80000000;
            for(std::size_t axis=0;axis<expectedNormal.size();++axis)
                matches&=std::bit_cast<std::uint32_t>(c.normal[axis])==(expectedNormal[axis]^sign);
            suppliedNormal|=matches;
        }
        // This generated normal's wide self-dot exceeds the original strict threshold;
        // float self-dot rounds to equality, so modern GenerateContact retains duplicates.
        assert(suppliedNormal);
        assert(counts[1]==counts[0]);
        for(int i=0;i<counts[0];++i) {
            const auto& first=contacts[0][std::size_t(i)];
            const auto& repeated=contacts[1][std::size_t(i)];
            assert(first.g1==meshes[0]&&repeated.g1==meshes[1]);
            assert(first.g2==box&&repeated.g2==box);
            for(int axis=0;axis<3;++axis) {
                assert(std::bit_cast<std::uint32_t>(first.pos[axis])==
                       std::bit_cast<std::uint32_t>(repeated.pos[axis]));
                assert(std::bit_cast<std::uint32_t>(first.normal[axis])==
                       std::bit_cast<std::uint32_t>(repeated.normal[axis]));
            }
            assert(std::bit_cast<std::uint32_t>(first.depth)==std::bit_cast<std::uint32_t>(repeated.depth));
            // Native-only side1 keeps the first triangle observed by the real ODE callback.
            assert(first.side1==firstTriangles[0]&&repeated.side1==firstTriangles[1]);
            assert(first.side2==-1&&repeated.side2==-1);
        }
        // Original capacity-first admission keeps the first payload, not a later deeper one.
        dContactGeom limited{};
        firstTriangle=-1;
        const int limitedCount=dCollide(meshes[0],box,1,&limited,sizeof(dContactGeom));
        assert(limitedCount==1);
        const auto& first=contacts[0][0];
        assert(limited.g1==first.g1&&limited.g2==first.g2);
        for(int axis=0;axis<3;++axis) {
            assert(std::bit_cast<std::uint32_t>(limited.pos[axis])==
                   std::bit_cast<std::uint32_t>(first.pos[axis]));
            assert(std::bit_cast<std::uint32_t>(limited.normal[axis])==
                   std::bit_cast<std::uint32_t>(first.normal[axis]));
        }
        assert(std::bit_cast<std::uint32_t>(limited.depth)==std::bit_cast<std::uint32_t>(first.depth));
        assert(limited.side1==first.side1&&limited.side2==first.side2);
        for(std::size_t query=0;query<meshes.size();++query) {
            dGeomDestroy(meshes[query]);
            dGeomTriMeshDataDestroy(meshData[query]);
        }
        dGeomDestroy(box);
    }
    const auto falling = body({0,0,20});
    state.require(falling).field("event").value = Value::function(runtime.function("contact"));
    engine.set_field(falling, "event_flags", 1 << 1); // Main-world EVENT_BLOCK, not actor contact.
    engine.set_global("_arg1", Value::from_int(71));
    engine.set_global("_arg2", Value::from_int(72));
    ticks(16);
    assert(engine.position(falling).z < 20);
    ticks(240);
    close(engine.position(falling).z, 1.5, .08);
    close(engine.field(falling, "pan"), 0, .2);
    assert(engine.field(falling, "skill1") > 0);
    for (auto field : {"skill2","skill3","skill4"}) close(engine.field(falling, field), 1);
    close(engine.field(falling, "skill5"), 1);
    close(engine.field(falling, "skill6"), 0, .08);
    assert(engine.field(falling, "skill7") > 3.8 && engine.field(falling, "skill7") < 12.2);
    close(engine.number("event_type"), 0);
    close(engine.number("_arg1"), 71); close(engine.number("_arg2"), 72);
    engine.remove_entity(runtime, falling);
    engine.physics.clear();
    engine.world = nullptr;

    // Original 1007bc40 rejects flag512 only for a no-body ENTITY partner.
    // Overlapping statics stay put; a rigid passes through them until the flag is cleared.
    {
        slab.min = {-20,-20,-1}; slab.max = {20,20,0};
        for (float x : {-20.f,20.f}) for (float y : {-20.f,20.f}) for (float z : {-1.f,0.f})
            slab.vertices.push_back({{x,y,z}, {}, {}, {}});
        std::array<ObjectId,2> surfaces;
        const std::array positions{Vec3{}, Vec3{0,0,-.5f}};
        for (std::size_t i = 0; i < surfaces.size(); ++i) {
            const auto id = surfaces[i] = state.create(Object::Kind::Entity).id;
            auto& entity = engine.entities.try_emplace(id).first->second;
            entity.model = &slab;
            entity.type = 2;
            entity.physicsGeometry = true;
            engine.set_position(id, positions[i]);
            for (auto field : {"scale_x","scale_y","scale_z"}) engine.set_field(id, field, 1);
            engine.set_field(id, "flags", 512);
        }
        engine.prepare(runtime);
        gravity({0,0,-32}); // Builds the real overlapping ODE statics, with no rigid yet.
        auto unchangedStatics = [&] {
            for (std::size_t i = 0; i < surfaces.size(); ++i) {
                const auto p = engine.position(surfaces[i]);
                close(p.x, positions[i].x); close(p.y, positions[i].y); close(p.z, positions[i].z);
                for (auto field : {"pan","tilt","roll"}) close(engine.field(surfaces[i], field), 0);
                assert(!engine.physics.query_body_geometry(surfaces[i]));
            }
        };
        ticks(16);
        unchangedStatics();
        for (bool flagged : {true, false}) {
            for (const auto id : surfaces) engine.set_field(id, "flags", flagged ? 512 : 0);
            const auto rigid = body({-8,-1.5f,12}); // Existing helper prepares this MODEL's query context.
            const auto rigidGeom = engine.physics.query_body_geometry(rigid);
            assert(rigidGeom && dGeomGetBody(rigidGeom));
            const auto rigidBody = dGeomGetBody(rigidGeom);
            dMass mass; dBodyGetMass(rigidBody, &mass);
            assert(dMassCheck(&mass));
            close(mass.mass, 2); close(mass.I[0], 20.0/3);
            close(mass.I[5], 34.0/3); close(mass.I[10], 50.0/3);
            ticks(256);
            const auto p = engine.position(rigid);
            assert(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
            for (int i = 0; i < 3; ++i) {
                assert(std::isfinite(dBodyGetPosition(rigidBody)[i]));
                assert(std::isfinite(dBodyGetLinearVel(rigidBody)[i]));
                for (int j = 0; j < 3; ++j)
                    assert(std::isfinite(dBodyGetRotation(rigidBody)[i*4+j]));
            }
            if (flagged) {
                assert(p.z < -10 && dBodyGetPosition(rigidBody)[2] < -10
                       && dBodyGetLinearVel(rigidBody)[2] < -10);
            } else {
                close(p.z, 1.5, .08); // Scaled rigid min_z=-1.5 rests on the upper slab at z=0.
                // Source depth>.0001 can alternate contact and gravity; support is sustained height.
                for (int i = 0; i < 64; ++i) {
                    ticks(1);
                    close(engine.position(rigid).z, 1.5, .08);
                    close(dBodyGetPosition(rigidBody)[2], 1, .08); // Actual supported COM, not only copyback.
                    for (int axis = 0; axis < 3; ++axis) {
                        assert(std::isfinite(dBodyGetPosition(rigidBody)[axis]));
                        assert(std::isfinite(dBodyGetLinearVel(rigidBody)[axis]));
                    }
                }
                close(p.x, -8, .08); close(p.y, -1.5, .08);
            }
            unchangedStatics();
            engine.remove_entity(runtime, rigid); // Fresh body isolates the two contact histories.
        }
        for (const auto id : surfaces) engine.remove_entity(runtime, id);
        engine.physics.clear();
    }
    // Actual triangle queries of an asymmetric octagon include interior and
    // near-duplicate candidates. Recover survivors from real normal-row torques,
    // then load every boundary across reused body batches.
    {
        const std::array boundary{Vec3{-3.8f,-1.2f,0},Vec3{-1.8f,-2.8f,0},
                                 Vec3{1.5f,-2.8f,0},Vec3{3.8f,-1,0},
                                 Vec3{3.8f,1.2f,0},Vec3{1.8f,2.8f,0},
                                 Vec3{-1.5f,2.8f,0},Vec3{-3.8f,1,0}};
        World manifoldFloor;
        manifoldFloor.collision.vertices.push_back({0,0,0});
        manifoldFloor.collision.vertices.insert(manifoldFloor.collision.vertices.end(),boundary.begin(),boundary.end());
        for(std::uint32_t i=0;i<boundary.size();++i)
            manifoldFloor.collision.triangles.push_back({0,i+1,(i+1)%std::uint32_t(boundary.size())+1});
        // Exact shared vertices are merged by modern ODE already. This nearby
        // interior point survives that merge but enters the original 1.5 cube.
        manifoldFloor.collision.vertices.push_back({.25f,.25f,0});
        manifoldFloor.collision.triangles.push_back({0,5,9});
        // Original world traversal retains repeated BSP face entries.
        manifoldFloor.collision.triangles.push_back(manifoldFloor.collision.triangles[0]);
        manifoldFloor.collision.flags.resize(manifoldFloor.collision.triangles.size());
        retain_world_xyz(manifoldFloor);
        engine.world=&manifoldFloor;
        gravity({0,0,-32});
        const auto rigid=body({-8,-1.5f,1.48f}); // COM=(0,0,.98), bottom=-.02.
        assert(call("phent_setfriction",{object(rigid),number(0)})==1);
        const auto geom=engine.physics.query_body_geometry(rigid);
        const auto odeBody=dGeomGetBody(geom);
        dBodySetAutoDisableFlag(odeBody,0);
        std::vector<dTriIndex> indices;
        for(const auto triangle:manifoldFloor.collision.triangles)
            indices.insert(indices.end(),triangle.begin(),triangle.end());
        const auto mesh=dGeomTriMeshDataCreate();
        dGeomTriMeshDataBuildSingle(mesh,manifoldFloor.collision.vertices.data(),sizeof(Vec3),
                                   int(manifoldFloor.collision.vertices.size()),indices.data(),int(indices.size()),3*sizeof(dTriIndex));
        const auto queryFloor=dCreateTriMesh(nullptr,mesh,nullptr,nullptr,nullptr);
        std::array<dContactGeom,100> candidates{};
        const int candidateCount=dCollide(geom,queryFloor,int(candidates.size()),candidates.data(),sizeof(dContactGeom));
        std::array<bool,8> supplied{};
        bool interior=false,duplicate=false;
        for(int i=0;i<candidateCount;++i) {
            const auto& c=candidates[std::size_t(i)];
            assert(c.g1==geom&&c.g2==queryFloor&&c.depth>.0001);
            // Modern SAT may return either triangle-plane or incident-box-face
            // positions. The reducer must keep that actual payload unchanged.
            assert(std::abs(c.pos[2])<.0001||std::abs(c.pos[2]+c.depth)<.0001);
            close(c.normal[0],0,.00001);
            close(c.normal[1],0,.00001);close(c.normal[2],1,.00001);
            bool outer=false;
            for(std::size_t j=0;j<boundary.size();++j)
                if(std::abs(c.pos[0]-boundary[j].x)<.0001&&std::abs(c.pos[1]-boundary[j].y)<.0001)
                    supplied[j]=outer=true;
            interior|=!outer;
            for(int j=0;j<i;++j) {
                const auto& previous=candidates[std::size_t(j)];
                const double dx=c.pos[0]-previous.pos[0],dy=c.pos[1]-previous.pos[1];
                duplicate|=dx*dx+dy*dy>.000001&&dx*dx+dy*dy<1;
            }
        }
        assert(std::ranges::all_of(supplied,[](bool present){return present;}));
        assert(interior&&duplicate);
        dGeomDestroy(queryFloor);dGeomTriMeshDataDestroy(mesh);

        contactFeedback.clear();contactProbeBody=odeBody;
        const dWorldStepMemoryFunctionsInfo memory{sizeof(dWorldStepMemoryFunctionsInfo),contactProbeAllocate,dRealloc,dFree};
        const auto odeWorld=dBodyGetWorld(odeBody);
        assert(dWorldSetStepMemoryManager(odeWorld,&memory));
        ticks(1);
        contactProbeBody=nullptr;
        assert(contactFeedback.size()==boundary.size()); // Source uncapped eight-vertex hull invariant.
        std::array<bool,8> consumed{};
        double support=0;
        std::size_t loaded=0;
        for(const auto& feedback:contactFeedback) {
            assert(feedback.f1[2]>=0);
            close(feedback.f1[0],0,.0001);close(feedback.f1[1],0,.0001);
            if(feedback.f1[2]<.001)continue; // Zero-pressure rows have no torque/force quotient.
            const double x=contactProbeCenter[0]-feedback.t1[1]/feedback.f1[2];
            const double y=contactProbeCenter[1]+feedback.t1[0]/feedback.f1[2];
            bool found=false;
            for(std::size_t i=0;i<boundary.size();++i)
                if(std::abs(x-boundary[i].x)<.001&&std::abs(y-boundary[i].y)<.001) {
                    assert(!consumed[i]);consumed[i]=found=true;
                }
            assert(found);support+=feedback.f1[2];++loaded;
        }
        assert(loaded>=3&&support>64); // Actual boundary support exceeds mass2's gravitational load.
        assert(dBodyGetLinearVel(odeBody)[2]>0);
        dWorldCleanupWorkingMemory(odeWorld);
        assert(dWorldSetStepMemoryManager(odeWorld,nullptr));
        ticks(128);
        for(const auto point:boundary) {
            dBodyAddForceAtPos(odeBody,0,0,-16,point.x,point.y,0);
            ticks(64);
            close(dBodyGetPosition(odeBody)[2],1,.08);
            close(engine.position(rigid).z,1.5,.08);
            close(dBodyGetRotation(odeBody)[8],0,.02);
            close(dBodyGetRotation(odeBody)[9],0,.02);
        }
        assert(dBodyGetNumJoints(odeBody)==0); // No contacts linger beyond the substep.
        engine.remove_entity(runtime,rigid);
        engine.physics.clear();
        // A slight plane slope makes the strictly shallower box-Z SAT axis win,
        // so real ODE contact positions lie on the triangles, not one box face.
        for(auto& p:manifoldFloor.collision.vertices)p.z=p.x*.0625f;

        // Two planes in ONE world geometry have identical ordered endpoints
        // and normals, but d differs by 2>1.5. They must form distinct groups.
        const auto lowerVertices=manifoldFloor.collision.vertices.size();
        const auto lowerTriangles=manifoldFloor.collision.triangles.size();
        for(std::size_t i=0;i<lowerVertices;++i) {
            auto p=manifoldFloor.collision.vertices[i];p.z+=2;
            manifoldFloor.collision.vertices.push_back(p);
        }
        for(std::size_t i=0;i<lowerTriangles;++i) {
            auto triangle=manifoldFloor.collision.triangles[i];
            for(auto& index:triangle)index+=std::uint32_t(lowerVertices);
            manifoldFloor.collision.triangles.push_back(triangle);
        }
        manifoldFloor.collision.flags.resize(manifoldFloor.collision.triangles.size());
        retain_world_xyz(manifoldFloor);
        tall.min={-4,-3,-3};tall.max={4,3,3};
        for(float x:{-4.f,4.f})for(float y:{-3.f,3.f})for(float z:{-3.f,3.f})
            tall.vertices.push_back({{x,y,z},{},{},{}});
        const auto layered=state.create(Object::Kind::Entity).id;
        auto& entity=engine.entities.try_emplace(layered).first->second;
        entity.model=&tall;entity.type=2;entity.physicsGeometry=true;
        engine.set_position(layered,{0,0,2.5f}); // bottom=-.5, both sloped decks BELOW the actual COM.
        for(auto field:{"scale_x","scale_y","scale_z"})engine.set_field(layered,field,1);
        engine.prepare(runtime);
        gravity({0,0,-32});
        assert(call("phent_settype",{object(layered),number(1),number(0)})==1);
        assert(call("phent_setmass",{object(layered),number(2),number(0)})==1);
        assert(call("phent_setfriction",{object(layered),number(0)})==1);
        assert(call("phent_setelasticity",{object(layered),number(0),number(0)})==1);
        const auto layeredGeom=engine.physics.query_body_geometry(layered);
        const auto layeredBody=dGeomGetBody(layeredGeom);
        close(dGeomGetPosition(layeredGeom)[0],0,.000001);
        close(dGeomGetPosition(layeredGeom)[1],0,.000001);
        close(dGeomGetPosition(layeredGeom)[2],2.5,.000001);
        dVector3 layeredLengths;dGeomBoxGetLengths(layeredGeom,layeredLengths);
        close(layeredLengths[0],8,.000001);close(layeredLengths[1],6,.000001);close(layeredLengths[2],6,.000001);
        // Establish actual primitive output premises before interpreting the
        // source reducer's two groups; requested entity pose alone is not proof.
        indices.clear();
        for(const auto triangle:manifoldFloor.collision.triangles)
            indices.insert(indices.end(),triangle.begin(),triangle.end());
        const auto layeredMesh=dGeomTriMeshDataCreate();
        dGeomTriMeshDataBuildSingle(layeredMesh,manifoldFloor.collision.vertices.data(),sizeof(Vec3),
                                   int(manifoldFloor.collision.vertices.size()),indices.data(),int(indices.size()),3*sizeof(dTriIndex));
        const auto layeredQuery=dCreateTriMesh(nullptr,layeredMesh,nullptr,nullptr,nullptr);
        const int layeredCount=dCollide(layeredGeom,layeredQuery,int(candidates.size()),candidates.data(),sizeof(dContactGeom));
        std::array<std::array<bool,8>,2> suppliedLayers{};
        for(int i=0;i<layeredCount;++i) {
            const auto& c=candidates[std::size_t(i)];
            assert(c.g1==layeredGeom&&c.g2==layeredQuery);
            close(c.normal[0],0,.00001);close(c.normal[1],0,.00001);close(c.normal[2],1,.00001);
            const std::size_t layer=c.pos[2]>1?1:0;
            close(c.pos[2],2*double(layer)+c.pos[0]*.0625,.00001);
            close(c.depth,c.pos[2]+.5,.00001);
            for(std::size_t j=0;j<boundary.size();++j)
                if(std::abs(c.pos[0]-boundary[j].x)<.0001&&std::abs(c.pos[1]-boundary[j].y)<.0001)
                    suppliedLayers[layer][j]=true;
        }
        for(const auto& suppliedLayer:suppliedLayers)
            assert(std::ranges::all_of(suppliedLayer,[](bool present){return present;}));
        dGeomDestroy(layeredQuery);dGeomTriMeshDataDestroy(layeredMesh);
        contactFeedback.clear();contactProbeBody=layeredBody;
        const auto layeredWorld=dBodyGetWorld(layeredBody);
        assert(dWorldSetStepMemoryManager(layeredWorld,&memory));
        ticks(1);
        contactProbeBody=nullptr;
        assert(contactFeedback.size()==2*boundary.size());
        // Shallow lower rows cannot produce the upper plane's depth>=2.26 ERP
        // response. Original payload depths, not group's maximum, drive rows.
        assert(dBodyGetLinearVel(layeredBody)[2]>20);
        for(const auto& feedback:contactFeedback) {
            if(feedback.f1[2]<.001)continue;
            const double x=contactProbeCenter[0]-feedback.t1[1]/feedback.f1[2];
            const double y=contactProbeCenter[1]+feedback.t1[0]/feedback.f1[2];
            assert(std::ranges::any_of(boundary,[&](Vec3 p){return std::abs(x-p.x)<.001&&std::abs(y-p.y)<.001;}));
        }
        dWorldCleanupWorkingMemory(layeredWorld);
        assert(dWorldSetStepMemoryManager(layeredWorld,nullptr));
        engine.remove_entity(runtime,layered);
        engine.physics.clear();
        engine.world=nullptr;
    }
    gravity({});

    // A queued force survives frames with no ODE substep, but not the next completed step.
    const auto moving = body({0,0,20});
    const auto force = vector({128,0,0});
    const std::array pushArgs{object(moving), object(force)};
    runtime.start("push", pushArgs);
    engine.set_global("time", Value::from_double(.0625)); // Four frames per substep.
    engine.set_global("freeze_mode", Value::from_int(1));
    ticks(8);
    close(engine.position(moving).x, 0);
    engine.set_global("freeze_mode", {});
    ticks(3);
    close(engine.position(moving).x, 0);
    ticks(2);
    close(engine.position(moving).x, 1.0 / 256, .001);
    ticks(3);
    const auto firstStep = engine.position(moving).x;
    ticks(1);
    close(engine.position(moving).x - firstStep, 1.0 / 256, .001);
    const auto frozen = engine.position(moving).x;
    engine.set_global("freeze_mode", Value::from_int(1));
    ticks(8); close(engine.position(moving).x, frozen);
    engine.set_global("freeze_mode", {});
    ticks(4); assert(engine.position(moving).x > frozen);

    // Ordinary MODEL wake refreshes public pose before capturing the retained COM.
    assert(call("phent_enable", {object(moving), number(1)}) == 1);
    assert(call("phent_enable", {object(moving), number(0)}) == 1);
    const auto disabled = engine.position(moving).x;
    ticks(16); close(engine.position(moving).x, disabled);
    const auto movingGeom = engine.physics.query_body_geometry(moving);
    assert(movingGeom && dGeomGetBody(movingGeom));
    engine.set_position(moving, {50,0,20});
    assert(call("phent_enable", {object(moving), number(1)}) == 1);
    close(engine.position(moving).x, 50);
    const std::array refreshedCOM{58.f,1.5f,19.5f};
    for (int i = 0; i < 3; ++i)
        close(dGeomGetPosition(movingGeom)[i], refreshedCOM[i], .0000001);
    ticks(8); // Two substeps, fraction=.25: publish 1.25 steps beyond the refreshed COM.
    close(dGeomGetPosition(movingGeom)[0], refreshedCOM[0] + 1.25/64, .00001);
    close(engine.position(moving).x, 50 + 1.25/64, .001);
    engine.remove_entity(runtime, moving);
    assert(!state.get(moving) && !engine.entities.contains(moving));
    assert(!engine.physics.query_body_geometry(moving));
    assert(call("phent_enable", {object(moving), number(1)}) == 0);
    assert(call("phent_addcentralforce", {object(moving), borrowed(force)}) == 0);
    ticks(8);
    engine.physics.clear();

    // Read the borrowed ODE geom after a real step and half-step publication, not a pose echo.
    const auto posed = body({0,0,20}, {37,23,11}); // Scaled off-center C=(8,1.5,-.5).
    const auto poseForce = vector({129,0,0});
    const std::array poseArgs{object(posed), object(poseForce)};
    runtime.start("push", poseArgs);
    engine.set_global("time", Value::from_double(.375)); // 1/64 step plus 1/128 remainder.
    ticks(1);
    const auto geom = engine.physics.query_body_geometry(posed);
    assert(geom && dGeomGetBody(geom));
    const auto* p = dGeomGetPosition(geom);
    const std::array position{p[0],p[1],p[2]};
    const auto* r = dGeomGetRotation(geom);
    constexpr double radians = .01745329238474369;
    const auto cp = std::cos(37*radians), sp = std::sin(37*radians);
    const auto ct = std::cos(23*radians), st = std::sin(23*radians);
    const auto cr = std::cos(11*radians), sr = std::sin(11*radians);
    const std::array expectedRotation{cp*ct, -cp*st*sr-sp*cr, -cp*st*cr+sp*sr,
                                     sp*ct, -sp*st*sr+cp*cr, -sp*st*cr-cp*sr,
                                     st, ct*sr, ct*cr};
    std::array<dReal,9> rotation;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        rotation[i*3+j] = r[i*4+j];
        close(rotation[i*3+j], expectedRotation[i*3+j], .000001);
    }
    // B=E+R*C; F/m * dt^2 / 2 is the half-step displacement, not the full-step snapshot.
    for (int i = 0; i < 3; ++i)
        close(position[i], 8*expectedRotation[i*3] + 1.5*expectedRotation[i*3+1]
              - .5*expectedRotation[i*3+2] + (i == 0 ? 129.0/16384 : i == 2 ? 20 : 0), .000005);
    const auto originX = position[0] - (8.f*r[0] + 1.5f*r[1] - .5f*r[2]);
    close(originX, 129.0/16384, .000005);
    close(engine.position(posed).x, Value::from_double(originX).real(), .0000001);
    assert(std::abs(originX - engine.position(posed).x) > .00001);
    engine.set_position(posed, {70,80,90});
    engine.set_field(posed, "pan", -41);
    engine.set_field(posed, "tilt", -17);
    engine.set_field(posed, "roll", -9);
    const auto retained = engine.physics.query_body_geometry(posed);
    assert(retained == geom);
    const auto* retainedPosition = dGeomGetPosition(retained);
    const auto* retainedRotation = dGeomGetRotation(retained);
    for (int i = 0; i < 3; ++i) {
        close(retainedPosition[i], position[i], .0000001);
        for (int j = 0; j < 3; ++j)
            close(retainedRotation[i*4+j], rotation[i*3+j], .0000001);
    }
    engine.remove_entity(runtime, posed);
    assert(!engine.physics.query_body_geometry(posed)); // Re-lookup, never read the removed borrowed geom.
    engine.physics.clear();

    engine.set_global("time", Value::from_double(.25));
    const auto rotating = body({0,0,20});
    assert(rotating != moving);
    const auto point = vector({2,0,0});
    const std::array spinArgs{object(rotating), object(point)};
    runtime.start("spin", spinArgs);
    const auto rotatingGeom = engine.physics.query_body_geometry(rotating);
    assert(rotatingGeom && dGeomGetBody(rotatingGeom));
    const auto rotatingBody = dGeomGetBody(rotatingGeom);
    close(dBodyGetForce(rotatingBody)[1], 128, .000001);
    close(dBodyGetTorque(rotatingBody)[2], 256, .000001);
    // At TIME=.25, mass2 gives F_y=128, torque_z=256, I_z=2*(8^2+6^2)/12.
    // Thus omega_z=.24 and ODE's normalized quaternion Euler step rotates by this angle.
    constexpr double angularVelocity = .24;
    const double angleStep = 2*std::atan(angularVelocity/128);
    auto retainedPan = [&](double steps) {
        const double angle = steps*angleStep;
        const std::array expected{std::cos(angle),-std::sin(angle),0.,
                                 std::sin(angle),std::cos(angle),0.,0.,0.,1.};
        const auto* matrix = dGeomGetRotation(rotatingGeom);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            close(matrix[i*4+j], expected[i*3+j], .000001);
        close(engine.field(rotating, "pan"), Value::from_double(angle*57.29577791868205).real(), .0000001);
        close(engine.field(rotating, "tilt"), 0); close(engine.field(rotating, "roll"), 0);
        // Public E uses the retained R, not the privately interpolated quaternion.
        const auto* center = dGeomGetPosition(rotatingGeom);
        const auto origin = engine.position(rotating);
        const std::array published{origin.x,origin.y,origin.z};
        for (int i = 0; i < 3; ++i) {
            const float offset = 8.f*matrix[i*4] + 1.5f*matrix[i*4+1] - .5f*matrix[i*4+2];
            close(published[i], Value::from_double(center[i]-offset).real(), .0000001);
        }
    };
    ticks(8);
    close(dBodyGetAngularVel(rotatingBody)[2], angularVelocity, .000001);
    close(dBodyGetLinearVel(rotatingBody)[1], 1, .0002); // Only source linear damping applies.
    retainedPan(8); // previousQ=q7, currentQ=q8, selectedQ=q7 at fraction0; actual R=q8.
    const auto origin = engine.position(rotating);
    const auto pan = engine.field(rotating, "pan") * .01745329238474369;
    // Scaled C=(8,1.5,-.5): E=B-R*C can move -Y while the mass center travels +Y.
    close(origin.x + 8 * std::cos(pan) - 1.5 * std::sin(pan), 8);
    // Eight actual substeps still publish the seventh COM at remainder0, but retain eighth R.
    close(origin.y + 8 * std::sin(pan) + 1.5 * std::cos(pan), 1.5 + 7.0 / 64);
    close(origin.z - .5, 19.5);
    assert(origin.y < 0);
    const auto* seventhCOM = dGeomGetPosition(rotatingGeom);
    close(seventhCOM[0], 8, .000001); close(seventhCOM[1], 1.5 + 7.0/64, .00001);
    close(seventhCOM[2], 19.5, .000001);

    engine.set_global("time", Value::from_double(.0625)); // Exact quarter-substep, no solver work.
    ticks(1); // selectedQ=q7.25, actual R remains q8.
    retainedPan(8);
    close(dGeomGetPosition(rotatingGeom)[1], 1.5 + 7.25/64, .00001);
    ticks(1); // selectedQ=q7.5, actual R remains q8; COM is now B7.5.
    retainedPan(8);
    close(dGeomGetPosition(rotatingGeom)[1], 1.5 + 7.5/64, .00001);
    engine.remove_entity(runtime, rotating);
    engine.physics.clear();

    // Genuine HMP preparation sets the live resource header8/baked skip; no fake model flag.
    engine.set_global("terrain_chunk", Value::from_int(32));
    engine.set_global("time", Value::from_double(.25));
    const Vec3 terrainBirth{};
    const auto baked = engine.create_entity(runtime, terrainPath.filename().string(), terrainBirth, 0);
    const auto& terrainData = engine.entities.at(baked);
    assert(terrainData.type == 5 && terrainData.physicsGeometry && terrainData.model);
    const auto& terrainModel = *terrainData.model;
    assert(!terrainModel.vertices.empty());
    Vec3 lo = terrainModel.vertices.front().position, hi = lo;
    for (const auto& vertex : terrainModel.vertices) {
        const auto p = vertex.position;
        lo = {std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi = {std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    const Vec3 terrainCenter{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};
    engine.prepare(runtime);
    assert(call("phent_settype", {object(baked), number(1), number(0)}) == 1);
    assert(call("phent_setmass", {object(baked), number(2), number(0)}) == 1);
    const auto bakedGeom = engine.physics.query_body_geometry(baked);
    assert(bakedGeom && dGeomGetBody(bakedGeom));
    const auto bakedBody = dGeomGetBody(bakedGeom);
    // Fresh BODY starts at S=0/R=identity; baked force0 skips E+C repinning.
    for (int i = 0; i < 3; ++i) {
        close(dGeomGetPosition(bakedGeom)[i], 0, .0000001);
        for (int j = 0; j < 3; ++j)
            close(dGeomGetRotation(bakedGeom)[i*4+j], i == j ? 1 : 0, .0000001);
    }
    dVector3 lengths; dGeomBoxGetLengths(bakedGeom, lengths);
    dMass terrainMass; dBodyGetMass(bakedBody, &terrainMass);
    const double inertiaZ = 2*(double(lengths[0])*lengths[0]+double(lengths[1])*lengths[1])/12;
    close(terrainMass.I[10], inertiaZ, inertiaZ*.000001);
    // Use the actual box inertia and a quantized off-center point to target omega_z=.25.
    constexpr double terrainVelocity = 64;
    const float lever = float(Value::from_double(.25*inertiaZ/(2*terrainVelocity)).real());
    assert(lever > 0);
    const auto terrainPoint = vector({lever,0,0});
    const auto terrainImpulse = vector({0,float(terrainVelocity),0});
    assert(call("phent_addvellocal", {object(baked), borrowed(terrainImpulse), borrowed(terrainPoint)}) == 1);
    close(dBodyGetForce(bakedBody)[1], 8192, .000001);
    close(dBodyGetTorque(bakedBody)[2], double(lever)*8192, .01);
    const double omega1 = 2*terrainVelocity*lever/inertiaZ;
    const double theta1 = 2*std::atan(omega1/128);
    const double retention = float(std::pow(double(float(1/(1-.001f))), -1.0/64));
    const double dampingRate = (retention-1)*64;
    const double omega2 = omega1*(std::abs(inertiaZ*omega1*dampingRate/1000) > .0001 ? retention : 1);
    const double theta2 = theta1*.5 + 2*std::atan(omega2/128);
    auto bakedRotation = [&](double angle) {
        const std::array expected{std::cos(angle),-std::sin(angle),0.,
                                 std::sin(angle),std::cos(angle),0.,0.,0.,1.};
        const auto* matrix = dGeomGetRotation(bakedGeom);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            close(matrix[i*4+j], expected[i*3+j], .000001);
        close(engine.field(baked, "pan"), angle*57.29577791868205, .002);
        close(engine.field(baked, "tilt"), 0); close(engine.field(baked, "roll"), 0);
    };
    auto bakedOrigin = [&] {
        const auto* matrix = dGeomGetRotation(bakedGeom);
        const auto* center = dGeomGetPosition(bakedGeom);
        const auto origin = engine.position(baked);
        const std::array published{origin.x,origin.y,origin.z};
        for (int i = 0; i < 3; ++i) {
            const float offset = matrix[i*4]*terrainCenter.x + matrix[i*4+1]*terrainCenter.y
                               + matrix[i*4+2]*terrainCenter.z;
            close(published[i], Value::from_double(center[i]-offset).real(), .0000001);
        }
    };
    constexpr double initialY = 0; // Source fresh-body COM, not the baked public/entity center.
    ticks(1); // previousQ=q0, currentQ=q1, selectedQ=q0; retained R=q1, published COM=B0.
    close(dBodyGetAngularVel(bakedBody)[2], omega1, .000001);
    bakedRotation(theta1); bakedOrigin();
    close(dGeomGetPosition(bakedGeom)[1], initialY, .002);
    engine.set_global("time", Value::from_double(.0625));
    ticks(1); // No substep: selectedQ=q.25, retained R=q1, COM=B.25.
    bakedRotation(theta1); bakedOrigin();
    close(dGeomGetPosition(bakedGeom)[1], initialY + .25, .002);
    ticks(1); // No substep: selectedQ=q.5, retained R=q1, COM=B.5.
    bakedRotation(theta1); bakedOrigin();
    close(dGeomGetPosition(bakedGeom)[1], initialY + .5, .002);
    const auto* halfCOM = dGeomGetPosition(bakedGeom);
    const std::array selectedCOM{halfCOM[0],halfCOM[1],halfCOM[2]};
    assert(call("phent_enable", {object(baked), number(0)}) == 1);
    assert(!dBodyIsEnabled(bakedBody));
    engine.set_position(baked, {70,80,90});
    assert(call("phent_enable", {object(baked), number(1)}) == 1);
    assert(dBodyIsEnabled(bakedBody));
    // Baked force0 skips public repinning: capture B.5/privateQ.5, leaving actual R=q1.
    close(engine.position(baked).x, 70); close(engine.position(baked).y, 80);
    close(engine.position(baked).z, 90); // Public position stays stale until publication.
    bakedRotation(theta1);
    for (int i = 0; i < 3; ++i)
        close(dGeomGetPosition(bakedGeom)[i], selectedCOM[i], .0000001);
    engine.set_global("time", Value::from_double(.125));
    ticks(1); // Complete next substep: restore q.5, advance by damped omega2, publish captured B.5.
    bakedRotation(theta2); bakedOrigin(); // Recapturing retained/public q1 would add another theta1/2.
    close(dBodyGetAngularVel(bakedBody)[2], omega2, .000001);
    for (int i = 0; i < 3; ++i)
        close(dGeomGetPosition(bakedGeom)[i], selectedCOM[i], .0000001);
    ticks(1); // Zero-substep half publication selects shadow Q only; actual R stays at theta2.
    bakedRotation(theta2); bakedOrigin();
    close(dGeomGetPosition(bakedGeom)[1], selectedCOM[1] + terrainVelocity*retention/128, .002);
    engine.remove_entity(runtime, baked);
    engine.physics.clear();

    // Engine level teardown clears ODE handles; a fresh PH_BOX and world rebuild still work.
    engine.world = &floor;
    gravity({0,0,-32});
    const auto reloaded = body({0,0,20});
    engine.load_level(runtime, "");
    assert(!engine.physics.query_body_geometry(reloaded));
    ticks(8); close(engine.position(reloaded).z, 20);
    assert(call("phent_enable", {object(reloaded), number(1)}) == 0);
    engine.entities.at(reloaded).model = &model;
    engine.world = &floor;
    engine.physics.load_world();
    // Teardown discarded PRIMARY/resource contexts; rebinding the model needs a new prepare epoch.
    engine.prepare(runtime);
    assert(!engine.physics.query_body_geometry(reloaded));
    assert(call("phent_settype", {object(reloaded), number(1), number(0)}) == 1);
    assert(call("phent_setelasticity", {object(reloaded), number(0), number(0)}) == 1);
    ticks(256);
    close(engine.position(reloaded).z, 1.5, .08);
    engine.remove_entity(runtime, reloaded);
    engine.physics.clear();
    engine.physics.clear();
    std::cout << "physics consumer boundaries passed\n";
}
