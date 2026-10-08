#pragma once
#include "value.hpp"
#include "assets.hpp"
#include <ode/ode.h>
#include <array>
#include <map>
#include <optional>

namespace k22 {
class Engine;
class Runtime;
class Physics {
public:
 explicit Physics(Engine&);
 ~Physics();
 Physics(const Physics&)=delete;
 Physics& operator=(const Physics&)=delete;
 std::optional<Value> builtin(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>);
 void step(Runtime&);
 void load_world();
 void remove_entity(ObjectId);
 void clear();
 // Borrowed only while this Physics owns the body; relookup after callbacks/removal.
 dGeomID query_body_geometry(ObjectId) const noexcept;
private:
 friend class Engine;
 bool set_query_body_transform(ObjectId,const Vec3& geometryPosition,const dMatrix3 geometryRotation);
 bool wake_query_body(ObjectId);
 struct Shape {
  ObjectId entity=0;dGeomID geom=nullptr;dTriMeshDataID mesh=nullptr;
  std::vector<dTriIndex> indices;std::vector<Vec3> vertices;Vec3 center{},extent{};float radius=0;
 };
 struct ContactCandidate {
  dContact payload;Vec3 point;
  ContactCandidate(const dContact& contact,Vec3 position):payload(contact),point(position){}
 };
 struct ContactGroup {
  Vec3 normal{};float plane=0,depth=-1;
  dGeomID first=nullptr,second=nullptr;
  std::vector<std::size_t> active,rejected;
 };
 struct ContactBatch {
  std::vector<ContactCandidate> candidates;
  std::vector<ContactGroup> groups;
  std::size_t usedGroups=0;
 };
 struct Body : Shape {
  dBodyID body=nullptr;float mass=1,friction=0,bounce=.5f,threshold=0;
  std::array<dReal,4> previousPosition{},previousQuaternion{},currentPosition{},currentQuaternion{},selectedQuaternion{},force{},torque{};
  ContactBatch contactBatch;
 };
 void wake_query_body(Body&);
 Engine& engine_;
 dWorldID world_=nullptr;dSpaceID space_=nullptr;dJointGroupID contacts_=nullptr;
 std::map<ObjectId,Body> bodies_;
 std::map<ObjectId,Shape> statics_;
 Shape worldShape_;
 float accumulator_=0;
 Vec3 gravity_{};bool requested_=false;
 Runtime* runtime_=nullptr;
 std::array<dContact,100> contactScratch_{};
 std::vector<std::size_t> contactHull_;
 void initialize();
 void refresh_statics();
 bool box(Shape&,ObjectId);
 void pose(Shape&);
 void copy_pose(Body&,float);
 void collide(dGeomID,dGeomID);
 void collect_contact(ContactBatch&,const dContact&);
 void flush_contacts(ContactBatch&);
 void reduce_contacts(ContactBatch&,ContactGroup&);
 static void near_callback(void*,dGeomID,dGeomID);
 static void destroy_shape(Shape&);
};
}
