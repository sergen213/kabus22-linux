#include "physics.hpp"
#include "engine.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

namespace k22 {
namespace {
constexpr float radians=0.01745329238474369f;
constexpr double degrees=57.29577791868205;
static_assert(sizeof(dReal)==sizeof(float),"Bonus requires float32 ODE");
static_assert(sizeof(Vec3)==3*sizeof(float));
void rotation(dMatrix3 matrix,Vec3 angles){
 dQuaternion x,y,z,temporary,result;
 dQFromAxisAndAngle(x,1,0,0,angles.z*radians);
 dQFromAxisAndAngle(y,0,1,0,-angles.y*radians);
 dQFromAxisAndAngle(z,0,0,1,angles.x*radians);
 dQMultiply0(temporary,z,y);dQMultiply0(result,temporary,x);dQtoR(result,matrix);
}
Vec3 transform(const dReal* r,Vec3 v){return {r[0]*v.x+r[1]*v.y+r[2]*v.z,r[4]*v.x+r[5]*v.y+r[6]*v.z,r[8]*v.x+r[9]*v.y+r[10]*v.z};}
bool vector_argument(const Argument& a){return a.ref.owner!=0||a.current().kind==Value::Kind::Object;}
void snapshot(std::array<dReal,4>& position,std::array<dReal,4>& quaternion,dBodyID body){
 const auto* p=dBodyGetPosition(body);std::copy_n(p,3,position.begin());std::copy_n(dBodyGetQuaternion(body),4,quaternion.begin());
}
float speed_squared(dBodyID body){if(!body)return 0;const auto* v=dBodyGetLinearVel(body);return v[0]*v[0]+v[1]*v[1]+v[2]*v[2];}
}
Physics::Physics(Engine& engine):engine_(engine){}
Physics::~Physics(){clear();}
dGeomID Physics::query_body_geometry(ObjectId id) const noexcept{
 const auto body=bodies_.find(id);return body==bodies_.end()?nullptr:body->second.geom;
}
bool Physics::set_query_body_transform(ObjectId id,const Vec3& p,const dMatrix3 r){
 const auto found=bodies_.find(id);if(found==bodies_.end())return false;auto& body=found->second;
 dGeomSetPosition(body.geom,p.x,p.y,p.z);dGeomSetRotation(body.geom,r);
 std::copy_n(dBodyGetQuaternion(body.body),4,body.selectedQuaternion.begin());return true;
}
bool Physics::wake_query_body(ObjectId id){
 const auto found=bodies_.find(id);if(found==bodies_.end())return false;wake_query_body(found->second);return true;
}
void Physics::wake_query_body(Body& body){
 // 10065e30 resets enabled/sleep state, then dispatches context force0 pose; no rigid snapshots.
 dBodyEnable(body.body);engine_.refresh_query_pose(body.entity);
}
void Physics::initialize(){
 if(world_)return;
 if(!dInitODE2(0))throw std::runtime_error("ODE initialization failed");
 if(!dAllocateODEDataForThread(dAllocateMaskAll)){dCloseODE();throw std::runtime_error("ODE thread allocation failed");}
 world_=dWorldCreate();space_=dHashSpaceCreate(nullptr);contacts_=dJointGroupCreate(0);
 dSpaceSetCleanup(space_,0);
 dWorldSetGravity(world_,gravity_.x,gravity_.y,gravity_.z);
 dWorldSetERP(world_,.2f);dWorldSetCFM(world_,1.e-6f);
 dWorldSetAutoDisableLinearThreshold(world_,.01f);dWorldSetAutoDisableAngularThreshold(world_,.008f);
 dWorldSetAutoDisableSteps(world_,10);dWorldSetAutoDisableTime(world_,.1f);dWorldSetAutoDisableFlag(world_,1);
 // The original uses instantaneous velocity, not modern ODE's averaging window.
 dWorldSetAutoDisableAverageSamplesCount(world_,1);
}
void Physics::destroy_shape(Shape& shape){
 if(shape.geom)dGeomDestroy(shape.geom);
 if(shape.mesh)dGeomTriMeshDataDestroy(shape.mesh);
 shape.geom=nullptr;shape.mesh=nullptr;
}
void Physics::clear(){
 if(!world_)return;
 dJointGroupEmpty(contacts_);
 for(auto& [id,body]:bodies_){destroy_shape(body);dBodyDestroy(body.body);}
 bodies_.clear();for(auto& [id,shape]:statics_)destroy_shape(shape);statics_.clear();destroy_shape(worldShape_);worldShape_=Shape{};
 dJointGroupDestroy(contacts_);dSpaceDestroy(space_);dWorldDestroy(world_);
 contacts_=nullptr;space_=nullptr;world_=nullptr;runtime_=nullptr;accumulator_=0;dCloseODE();
}
void Physics::remove_entity(ObjectId id){
 if(!world_)return;
 // A script callback may remove a geometry while its body batch is pending.
 dGeomID removed=nullptr;
 if(auto it=bodies_.find(id);it!=bodies_.end())removed=it->second.geom;
 else if(auto it=statics_.find(id);it!=statics_.end())removed=it->second.geom;
 if(removed)for(auto& [entity,body]:bodies_)for(std::size_t i=0;i<body.contactBatch.usedGroups;++i){
  auto& group=body.contactBatch.groups[i];
  if(group.first==removed||group.second==removed){group.active.clear();group.rejected.clear();group.first=nullptr;group.second=nullptr;}
 }
 if(auto it=bodies_.find(id);it!=bodies_.end()){destroy_shape(it->second);dBodyDestroy(it->second.body);bodies_.erase(it);}
 if(auto it=statics_.find(id);it!=statics_.end()){destroy_shape(it->second);statics_.erase(it);}
}
bool Physics::box(Shape& shape,ObjectId id){
 auto it=engine_.entities.find(id);if(it==engine_.entities.end()||!it->second.physicsGeometry||!it->second.model||it->second.model->vertices.empty())return false;
 const auto& entity=it->second;Vec3 lo=worldModelVertex(engine_,id,entity,0),hi=lo;
 for(std::size_t i=1;i<entity.model->vertices.size();++i){const auto v=worldModelVertex(engine_,id,entity,i);lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
 const Vec3 scale{float(engine_.field(id,"scale_x")),float(engine_.field(id,"scale_y")),float(engine_.field(id,"scale_z"))};
 lo={lo.x*scale.x,lo.y*scale.y,lo.z*scale.z};hi={hi.x*scale.x,hi.y*scale.y,hi.z*scale.z};
 shape.entity=id;shape.center={(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};
 shape.extent={hi.x-lo.x,hi.y-lo.y,hi.z-lo.z};
 shape.radius=.5f*std::sqrt(shape.extent.x*shape.extent.x+shape.extent.y*shape.extent.y+shape.extent.z*shape.extent.z);
 if(shape.extent.x<=0)shape.extent.x=1;if(shape.extent.y<=0)shape.extent.y=1;if(shape.extent.z<=0)shape.extent.z=1;
 shape.geom=dCreateBox(space_,shape.extent.x,shape.extent.y,shape.extent.z);dGeomSetData(shape.geom,&shape);return true;
}
void Physics::pose(Shape& shape){
 dMatrix3 r;rotation(r,engine_.angles(shape.entity));const auto p=engine_.position(shape.entity),c=transform(r,shape.center);
 dGeomSetRotation(shape.geom,r);dGeomSetPosition(shape.geom,p.x+c.x,p.y+c.y,p.z+c.z);
}
void Physics::copy_pose(Body& body,float fraction){
 // Original 100b0d40 changes the private quaternion, not the geometry's retained matrix.
 // Keep that quaternion for enable's snapshot capture without making modern ODE Q/R inconsistent.
 auto& q=body.selectedQuaternion;float dot=0;for(int i=0;i<4;++i)dot+=body.previousQuaternion[i]*body.currentQuaternion[i];
 const float angle=std::acos(std::clamp(dot,-1.f,1.f)),sine=std::sin(angle);
 if(sine==0)q=body.currentQuaternion;
 else {const float a=std::sin((1-fraction)*angle)/sine,b=std::sin(fraction*angle)/sine;for(int i=0;i<4;++i)q[i]=a*body.previousQuaternion[i]+b*body.currentQuaternion[i];}
 dBodySetPosition(body.body,(1-fraction)*body.previousPosition[0]+fraction*body.currentPosition[0],(1-fraction)*body.previousPosition[1]+fraction*body.currentPosition[1],(1-fraction)*body.previousPosition[2]+fraction*body.currentPosition[2]);
 const auto* r=dBodyGetRotation(body.body);const auto* p=dBodyGetPosition(body.body);const auto c=transform(r,body.center);
 engine_.set_position(body.entity,{p[0]-c.x,p[1]-c.y,p[2]-c.z});
 engine_.set_field(body.entity,"pan",std::atan2(r[4],r[0])*degrees);
 engine_.set_field(body.entity,"tilt",std::atan2(r[8],std::sqrt(r[4]*r[4]+r[0]*r[0]))*degrees);
 engine_.set_field(body.entity,"roll",std::atan2(r[9],r[10])*degrees);
}
std::optional<Value> Physics::builtin(Runtime&,ProcessContext&,std::string_view name,std::span<const Argument> args){
 const bool owned=name=="ph_setgravity"||name=="phent_settype"||name=="phent_setmass"||name=="phent_setfriction"||name=="phent_setelasticity"||name=="phent_enable"||name=="phent_addcentralforce"||name=="phent_addvelglobal"||name=="phent_addvellocal";
 if(!owned)return std::nullopt;
 auto need=[&](std::size_t n){if(args.size()<n)throw std::runtime_error(std::string(name)+": missing arguments");};
 auto value=[&](std::size_t i){return args[i].current();};
 if(name=="ph_setgravity"){
  need(1);if(!vector_argument(args[0]))return Value{};gravity_=engine_.vector(args[0]);requested_=true;
  if(!world_){initialize();load_world();}dWorldSetGravity(world_,gravity_.x,gravity_.y,gravity_.z);
  for(auto& [id,body]:bodies_)wake_query_body(body);return Value::from_int(1);
 }
 need(2);const auto v=value(0);const ObjectId id=v.kind==Value::Kind::Object?v.bits:0;
 const auto* object=engine_.state.get(id);auto entity=engine_.entities.find(id);if(!object||!object->alive||entity==engine_.entities.end()||!entity->second.physicsGeometry||!entity->second.model)return Value{};
 auto it=bodies_.find(id);
 if(name=="phent_settype"){
  need(3);const auto type=value(1).raw()>>10,shape=value(2).raw()>>10;
  if(it!=bodies_.end()){if(type)return Value{};remove_entity(id);return Value::from_int(1);}
  if(type!=1||shape!=0)return Value{};
  requested_=true;if(!world_){initialize();load_world();}
  if(auto existing=statics_.find(id);existing!=statics_.end()){destroy_shape(existing->second);statics_.erase(existing);}
  auto [entry,inserted]=bodies_.try_emplace(id);auto& body=entry->second;
  if(!box(body,id)){bodies_.erase(entry);return Value::from_int(1);}
  body.body=dBodyCreate(world_);dBodySetData(body.body,&body);dGeomSetBody(body.geom,body.body);engine_.refresh_query_pose(id);
  dMass mass;dMassSetBoxTotal(&mass,1,body.extent.x,body.extent.y,body.extent.z);dBodySetMass(body.body,&mass);
  snapshot(body.previousPosition,body.previousQuaternion,body.body);body.currentPosition=body.previousPosition;body.currentQuaternion=body.previousQuaternion;body.selectedQuaternion=body.currentQuaternion;
  return Value::from_int(1);
 }
 if(it==bodies_.end())return Value{};auto& body=it->second;
 if(name=="phent_setmass"){
  need(3);if(value(2).raw()!=0)return Value{};const float total=float(value(1).real());
  // Only positive PH_BOX mass is reachable in the actual Bonus census; mass0 is not a kinematic policy.
  if(total<=0)throw std::runtime_error("Unreachable Bonus zero/negative rigid mass has no proved modern ODE mapping");
  body.mass=total;dMass mass;dMassSetBoxTotal(&mass,total,body.extent.x,body.extent.y,body.extent.z);dBodySetMass(body.body,&mass);
 }else if(name=="phent_setfriction"){
  body.friction=std::max(0.f,float(value(1).real()))/100;if(body.friction>1)body.friction=FLT_MAX;
 }else if(name=="phent_setelasticity"){
  need(3);body.bounce=std::clamp(float(value(1).real()),0.f,100.f)/100;body.threshold=std::max(0.f,float(value(2).real()));
 }else if(name=="phent_enable"){
  if(!value(1)){dBodyDisable(body.body);return Value::from_int(1);}
  wake_query_body(body);std::copy_n(dBodyGetPosition(body.body),4,body.currentPosition.begin());body.currentQuaternion=body.selectedQuaternion;
  body.previousPosition=body.currentPosition;body.previousQuaternion=body.currentQuaternion;return Value::from_int(1);
 }else {
  if(!vector_argument(args[1]))return Value{};const auto force=engine_.vector(args[1]);
  if(name=="phent_addcentralforce"){
   if(std::abs(force.x)>.0001f||std::abs(force.y)>.0001f||std::abs(force.z)>.0001f)dBodyAddForce(body.body,force.x,force.y,force.z);
  }else {
   need(3);if(!vector_argument(args[2]))return Value{};const auto point=engine_.vector(args[2]);const float scale=body.mass/float(engine_.number("time")/16);
   if(name=="phent_addvelglobal")dBodyAddForceAtPos(body.body,force.x*scale,force.y*scale,force.z*scale,point.x,point.y,point.z);
   else dBodyAddRelForceAtRelPos(body.body,force.x*scale,force.y*scale,force.z*scale,point.x,point.y,point.z);
  }
 }
 wake_query_body(body);return Value::from_int(1);
}
void Physics::load_world(){
 if(!requested_)return;initialize();destroy_shape(worldShape_);worldShape_=Shape{};
 if(engine_.world){
  const auto& world=*engine_.world;const auto& mesh=world.collision;
  // 1005c360 populates the world face list from section 11, including repetitions.
  // 10087e70 then selects only 100870d0's bit-0x40-clear partition.
  auto append=[&](std::size_t i){if(i<mesh.flags.size()&&(mesh.flags[i]&0x40))return;for(auto index:mesh.triangles[i])worldShape_.indices.push_back(index);};
  if(world.faces.empty()){
   // Direct collision-only Worlds (the consumer boundary) already supply the face feed.
   worldShape_.indices.reserve(mesh.triangles.size()*3);for(std::size_t i=0;i<mesh.triangles.size();++i)append(i);
  }else{
   std::vector<std::size_t> first(world.faces.size()+1);
   for(std::size_t i=0;i<world.faces.size();++i)first[i+1]=first[i]+world.faces[i].edgeCount-2;
   std::size_t count=0;for(auto face:world.bspBlocks)count+=first[face+1]-first[face];
   worldShape_.indices.reserve(count*3);
   for(auto face:world.bspBlocks)for(std::size_t i=first[face];i<first[face+1];++i)append(i);
  }
  if(worldShape_.indices.size()==3){const std::array<dTriIndex,3> triangle{worldShape_.indices[0],worldShape_.indices[1],worldShape_.indices[2]};worldShape_.indices.insert(worldShape_.indices.end(),triangle.begin(),triangle.end());}
  if(!worldShape_.indices.empty()){
   worldShape_.mesh=dGeomTriMeshDataCreate();dGeomTriMeshDataBuildSingle(worldShape_.mesh,mesh.vertices.data(),sizeof(Vec3),int(mesh.vertices.size()),worldShape_.indices.data(),int(worldShape_.indices.size()),3*sizeof(dTriIndex));
   // Original main-world contacts precede actor pairs, not space iteration order.
   worldShape_.geom=dCreateTriMesh(nullptr,worldShape_.mesh,nullptr,nullptr,nullptr);dGeomSetData(worldShape_.geom,&worldShape_);
  }
 }
 refresh_statics();
}
void Physics::refresh_statics(){
 for(auto it=statics_.begin();it!=statics_.end();){const auto* object=engine_.state.get(it->first);if(!object||!object->alive||!engine_.entities.contains(it->first)){destroy_shape(it->second);it=statics_.erase(it);}else ++it;}
 for(const auto& [id,entity]:engine_.entities){
  const auto* object=engine_.state.get(id);if(!object||!object->alive||bodies_.contains(id)||!entity.physicsGeometry||!entity.model||entity.type==0)continue;
  const bool solid=(engine_.flags(id)&engine_.constant("passable"))==0;
  auto it=statics_.find(id);
  if(!solid){if(it!=statics_.end())dGeomDisable(it->second.geom);continue;}
  const bool polygon=(engine_.flags(id)&0x04000000u)!=0||entity.type==5;
  if(it!=statics_.end()&&bool(it->second.mesh)!=polygon){destroy_shape(it->second);statics_.erase(it);it=statics_.end();}
  if(it==statics_.end()){
   auto [entry,inserted]=statics_.try_emplace(id);auto& shape=entry->second;if(!box(shape,id)){statics_.erase(entry);continue;}
   if(polygon){
    dGeomDestroy(shape.geom);shape.geom=nullptr;const auto& model=*entity.model;
    const Vec3 scale{float(engine_.field(id,"scale_x")),float(engine_.field(id,"scale_y")),float(engine_.field(id,"scale_z"))};
    shape.vertices.reserve(model.vertices.size());shape.indices.reserve(model.triangles.size()*3);
    for(std::size_t i=0;i<model.vertices.size();++i){const auto v=worldModelVertex(engine_,id,entity,i);shape.vertices.push_back({v.x*scale.x-shape.center.x,v.y*scale.y-shape.center.y,v.z*scale.z-shape.center.z});}
    for(const auto& triangle:model.triangles){const auto& indices=triangle.indices;if(model.clockwise)shape.indices.insert(shape.indices.end(),{indices[2],indices[1],indices[0]});else shape.indices.insert(shape.indices.end(),indices.begin(),indices.end());}
    if(shape.indices.empty()){statics_.erase(entry);continue;}
    shape.mesh=dGeomTriMeshDataCreate();dGeomTriMeshDataBuildSingle(shape.mesh,shape.vertices.data(),sizeof(Vec3),int(shape.vertices.size()),shape.indices.data(),int(shape.indices.size()),3*sizeof(dTriIndex));
    shape.geom=dCreateTriMesh(space_,shape.mesh,nullptr,nullptr,nullptr);dGeomSetData(shape.geom,&shape);
   }
   it=entry;
  }
  dGeomEnable(it->second.geom);pose(it->second);
 }
}
void Physics::collect_contact(ContactBatch& batch,const dContact& contact){
 // 1007ab80: ordered depth<=double .0001 rejects; unordered is admitted.
 if(contact.geom.depth<=.0001)return;
 const auto& c=contact.geom;
 const Vec3 n{c.normal[0],c.normal[1],c.normal[2]},p{c.pos[0],c.pos[1],c.pos[2]};
 const float plane=float((static_cast<long double>(p.z)*n.z+static_cast<long double>(p.y)*n.y)+static_cast<long double>(p.x)*n.x);
 std::size_t index=batch.usedGroups;
 for(std::size_t i=batch.usedGroups;i>0;--i){
  const auto& group=batch.groups[i-1];const auto& g=group.normal;
  if(group.first==c.g1&&group.second==c.g2&&
     static_cast<long double>(group.plane)-1.5f<=plane&&plane<=static_cast<long double>(group.plane)+1.5f&&
     (static_cast<long double>(n.z)*g.z+static_cast<long double>(n.y)*g.y)+static_cast<long double>(n.x)*g.x>=.9f){index=i-1;break;}
 }
 if(index==batch.usedGroups){
  if(index==batch.groups.size())batch.groups.emplace_back();
  auto& group=batch.groups[index];group.normal=n;group.plane=plane;group.depth=-1;group.first=c.g1;group.second=c.g2;
  ++batch.usedGroups;
 }
 auto& group=batch.groups[index];group.active.push_back(batch.candidates.size());
 batch.candidates.emplace_back(contact,p);
}
void Physics::reduce_contacts(ContactBatch& batch,ContactGroup& group){
 // Original 1006d2a0/1006c9b0/1006cc80/1006cf60, not a max-four manifold.
 // Explicit float stores retain the recovered spill points; long double keeps
 // unspilled arithmetic extended. Original incoming x87/CRT mode is unknown.
 constexpr float epsilon=.0001f;
 auto& active=group.active;
 if(active.size()<4)return;
 auto nearZero=[&](Vec3 v){return !(float(std::fabs(double(v.x)))>epsilon||float(std::fabs(double(v.y)))>epsilon||float(std::fabs(double(v.z)))>epsilon);};
 if(nearZero(group.normal))return;
 auto difference=[](Vec3 b,Vec3 a){return Vec3{float(static_cast<long double>(b.x)-a.x),float(static_cast<long double>(b.y)-a.y),float(static_cast<long double>(b.z)-a.z)};};
 auto radius=[](Vec3 v){return float((static_cast<long double>(v.x)*v.x+static_cast<long double>(v.y)*v.y)+static_cast<long double>(v.z)*v.z);};
 Vec3 t{};
 for(std::size_t i=1;i<active.size()&&nearZero(t);++i)t=difference(batch.candidates[active[i]].point,batch.candidates[active[0]].point);
 const float length=float(std::sqrt(double(radius(t))));
 if(length<=epsilon)t={};
 else{
  t={float(static_cast<long double>(t.x)/length),float(static_cast<long double>(t.y)/length),float(static_cast<long double>(t.z)/length)};
  if(float(std::fabs(double(t.x)))<epsilon)t.x=0;
  if(float(std::fabs(double(t.y)))<epsilon)t.y=0;
  if(float(std::fabs(double(t.z)))<epsilon)t.z=0;
 }
 const auto n=group.normal;const Vec3 negative{-t.x,-t.y,-t.z};
 const Vec3 b{float(static_cast<long double>(negative.y)*n.z-static_cast<long double>(negative.z)*n.y),
              float(static_cast<long double>(negative.z)*n.x-static_cast<long double>(negative.x)*n.z),
              float(static_cast<long double>(negative.x)*n.y-static_cast<long double>(negative.y)*n.x)};
 auto dot=[](Vec3 p,Vec3 axis){return float((static_cast<long double>(p.y)*axis.y+static_cast<long double>(p.z)*axis.z)+static_cast<long double>(p.x)*axis.x);};
 group.depth=-1;
 for(auto index:active){
  auto& candidate=batch.candidates[index];const auto p=candidate.point;
  if(candidate.payload.geom.depth>group.depth)group.depth=candidate.payload.geom.depth;
  const float z=dot(p,n),y=dot(p,b),x=dot(p,t);candidate.point={x,y,z};
 }
 std::size_t pivot=0;
 for(std::size_t i=1;i<active.size();++i){
  const auto p=batch.candidates[active[i]].point,q=batch.candidates[active[pivot]].point;
  if(p.y<q.y||(!(p.y>q.y)&&!(p.y<q.y)&&p.x<q.x))pivot=i;
 }
 std::swap(active[0],active[pivot]);
 auto discard=[&](std::size_t index){std::swap(active[index],active.back());group.rejected.push_back(active.back());active.pop_back();};
 for(std::size_t i=0;i+1<active.size();++i){
  for(std::size_t j=i+1;j<active.size();){
   const auto delta=difference(batch.candidates[active[j]].point,batch.candidates[active[i]].point);
   if(!(float(std::fabs(double(delta.x)))>1.5f||float(std::fabs(double(delta.y)))>1.5f||float(std::fabs(double(delta.z)))>1.5f))discard(j);
   else ++j;
  }
 }
 auto area=[](Vec3 a,Vec3 b,Vec3 c){return float((static_cast<long double>(c.y)-a.y)*(static_cast<long double>(b.x)-a.x)-(static_cast<long double>(b.y)-a.y)*(static_cast<long double>(c.x)-a.x));};
 for(std::size_t i=1;i+1<active.size();++i){
  for(std::size_t j=i+1;j<active.size();){
   const auto a=batch.candidates[active[0]].point,b=batch.candidates[active[i]].point,c=batch.candidates[active[j]].point;
   const float raw=area(a,b,c),orientation=float(std::fabs(double(raw)))>epsilon?raw:0;
   bool keep=orientation>0,remove=orientation==0;
   if(remove){
    const float rb=radius(difference(b,a)),rc=radius(difference(c,a));
    keep=!(static_cast<long double>(rc)-epsilon>=rb); // Less OR unordered; no premature float spill.
   }
   if(!keep)std::swap(active[i],active[j]);
   if(remove)discard(j);else ++j;
  }
 }
 if(active.size()<4)return;
 contactHull_.clear();contactHull_.reserve(active.size());
 contactHull_.push_back(active[0]);contactHull_.push_back(active[1]);
 for(std::size_t i=2;i<active.size();++i){
  const auto c=batch.candidates[active[i]].point;
  while(contactHull_.size()>=2){
   const auto a=batch.candidates[contactHull_[contactHull_.size()-2]].point,b=batch.candidates[contactHull_.back()].point;
   const float turn=area(a,b,c);
   if(float(std::fabs(double(turn)))>epsilon&&turn>=0)break;
   group.rejected.push_back(contactHull_.back());contactHull_.pop_back();
  }
  contactHull_.push_back(active[i]);
 }
 active.swap(contactHull_);contactHull_.clear();
}
void Physics::flush_contacts(ContactBatch& batch){
 for(std::size_t i=0;i<batch.usedGroups;++i){
  auto& group=batch.groups[i];reduce_contacts(batch,group);
  for(auto index:group.active){
   const auto& contact=batch.candidates[index].payload;
   auto joint=dJointCreateContact(world_,contacts_,&contact);
   dJointAttach(joint,dGeomGetBody(contact.geom.g1),dGeomGetBody(contact.geom.g2));
  }
  group.active.clear();group.rejected.clear();group.normal={};group.plane=0;group.depth=-1;group.first=nullptr;group.second=nullptr;
 }
 batch.usedGroups=0;batch.candidates.clear();
}
void Physics::near_callback(void* physics,dGeomID first,dGeomID second){static_cast<Physics*>(physics)->collide(first,second);}
void Physics::collide(dGeomID first,dGeomID second){
 auto a=dGeomGetBody(first),b=dGeomGetBody(second);if(!a&&!b)return;
 if(!a){std::swap(first,second);std::swap(a,b);}
 const auto idA=static_cast<Body*>(dBodyGetData(a))->entity;
 const bool eligibleA=dBodyIsEnabled(a)&&!(engine_.flags(idA)&512u);
 if(b){
  const auto idB=static_cast<Body*>(dBodyGetData(b))->entity;const bool eligibleB=dBodyIsEnabled(b)&&!(engine_.flags(idB)&512u);
  if(!eligibleA&&!eligibleB)return;
  if(!eligibleA||(eligibleB&&idB<idA)){std::swap(first,second);std::swap(a,b);}
 }else if(!eligibleA)return;
 // All five source-reachable boxes retain constructor group 1, which permits same-group pairs.
 auto& body=*static_cast<Body*>(dBodyGetData(a));auto* other=b?static_cast<Body*>(dBodyGetData(b)):nullptr;
 const auto* staticShape=static_cast<Shape*>(dGeomGetData(second));const ObjectId otherId=other?other->entity:staticShape->entity;
 const bool mainWorld=staticShape==&worldShape_;
 // 1007bcc8/1007bce5 tests geometry flag9 only for the no-body ENTITY partner.
 if(!other&&!mainWorld&&(engine_.flags(otherId)&512u))return;
 // Original reusable 100-entry scratch limits ONE query, not the body batch.
 auto& contact=contactScratch_;const int count=dCollide(first,second,int(contact.size()),&contact[0].geom,sizeof(dContact));if(!count)return;
 const float friction=other?(body.friction==FLT_MAX||other->friction==FLT_MAX?FLT_MAX:std::sqrt(body.friction*other->friction)):body.friction;
 const float bounce=other?body.bounce*other->bounce:body.bounce,threshold=other?std::max(body.threshold,other->threshold):body.threshold;
 auto target=[&](const dContactGeom& c,bool normal){engine_.set_vector(engine_.global_vector("target"),{c.pos[0],c.pos[1],c.pos[2]});if(normal)engine_.set_vector(engine_.global_vector("normal"),{c.normal[0],c.normal[1],c.normal[2]});};
 auto event=[&](ObjectId receiver,ObjectId otherEntity,int type,const dContactGeom& c,bool normal){if(!(std::uint32_t(engine_.field(receiver,"event_flags"))&(1u<<unsigned(type))))return;target(c,normal);engine_.invoke_event(*runtime_,receiver,otherEntity,type);};
 const bool fast=speed_squared(a)+speed_squared(b)>threshold*threshold;
 for(int i=0;i<count;++i){auto& c=contact[std::size_t(i)];c.surface.mode=(dContactApprox1_1|dContactApprox1_2)|(mainWorld||other||i>0?dContactSoftERP|dContactSoftCFM:0)|(other||bounce>0?dContactBounce:0);c.surface.mu=friction;c.surface.bounce=bounce;c.surface.bounce_vel=threshold;c.surface.soft_erp=.2f;c.surface.soft_cfm=1.e-6f;
  if(fast){event(body.entity,otherId,6,c.geom,true);if(other)event(otherId,body.entity,6,c.geom,true);}
  collect_contact(body.contactBatch,c);
 }
 if(mainWorld){
  const auto mask=std::uint32_t(engine_.field(body.entity,"event_flags"));
  if((mask&2u)&&!(mask&0x40000000u)){
   const auto& c=contact[0].geom;const auto* velocity=dBodyGetLinearVel(a);
   const Vec3 v{bounce*velocity[0],bounce*velocity[1],bounce*velocity[2]};
   const float dot=c.normal[0]*v.x+c.normal[1]*v.y+c.normal[2]*v.z;
   engine_.set_vector(engine_.global_vector("bounce"),{v.x-2*dot*c.normal[0],v.y-2*dot*c.normal[1],v.z-2*dot*c.normal[2]});
   event(body.entity,0,1,c,true);
  }
  engine_.set_field(body.entity,"event_flags",std::uint32_t(engine_.field(body.entity,"event_flags"))|0x40000000u);
 }else{
  event(body.entity,otherId,2,contact[0].geom,false);
  if(otherId)event(otherId,body.entity,5,contact[0].geom,false);
 }
}
void Physics::step(Runtime& runtime){
 if(!world_||engine_.global("freeze_mode").raw()>=1)return;
 // Engine/Runtime publish TIME; genuine Bonus defines TIME as TIME_STEP.
 const float rate=float(engine_.number("ph_fps_max_lock"));if(rate<=0)return;const float delta=float(engine_.number("time")/16),interval=1/rate;
 accumulator_=std::min(.25f,accumulator_+delta);
 if(bodies_.empty()){while(accumulator_>=interval)accumulator_-=interval;return;}
 runtime_=&runtime;bool completed=false;
 // Source leaves both damping coefficients at .001; no damping work when no substep is due.
 const float dampingRate=accumulator_>=interval?(float(std::pow(double(float(1/(1-.001f))),-double(interval)))-1)/interval:0;
 if(accumulator_>=interval)refresh_statics();
 while(accumulator_>=interval){
  for(auto& [id,body]:bodies_){dBodySetPosition(body.body,body.currentPosition[0],body.currentPosition[1],body.currentPosition[2]);dBodySetQuaternion(body.body,body.currentQuaternion.data());body.selectedQuaternion=body.currentQuaternion;body.previousPosition=body.currentPosition;body.previousQuaternion=body.currentQuaternion;std::copy_n(dBodyGetForce(body.body),3,body.force.begin());std::copy_n(dBodyGetTorque(body.body),3,body.torque.begin());}
  for(auto& [id,body]:bodies_)if(dBodyIsEnabled(body.body)&&!(engine_.flags(id)&512u)){
   // 10087e70 resets the contact mark before even testing for a world mesh.
   engine_.set_field(id,"event_flags",std::uint32_t(engine_.field(id,"event_flags"))&~0x40000000u);
   if(worldShape_.geom)collide(body.geom,worldShape_.geom);
  }
  dSpaceCollide(space_,this,&Physics::near_callback);
  // Isolate each initiating body's world + eligible partner query subsequence.
  // Keep modern dSpaceCollide's pair admission and encounter order: original
  // kernel geometry/multiplicity/order and per-body event timing are not parity
  // claims. No solver step occurs between these equivalent body-batch flushes.
  for(auto& [id,body]:bodies_)flush_contacts(body.contactBatch);
  for(auto& [id,body]:bodies_){if(!dBodyIsEnabled(body.body))continue;
   const auto* v=dBodyGetLinearVel(body.body);
   const float linear=dampingRate*body.mass;
   if(std::abs(v[0]*linear)>.0001f||std::abs(v[1]*linear)>.0001f||std::abs(v[2]*linear)>.0001f)dBodyAddForce(body.body,v[0]*linear,v[1]*linear,v[2]*linear);
   const auto* w=dBodyGetAngularVel(body.body);dVector3 local,torque;dBodyVectorFromWorld(body.body,w[0],w[1],w[2],local);dMass mass;dBodyGetMass(body.body,&mass);
   const float angular=dampingRate;
   dBodyVectorToWorld(body.body,(mass.I[0]*local[0]+mass.I[1]*local[1]+mass.I[2]*local[2])*angular,(mass.I[4]*local[0]+mass.I[5]*local[1]+mass.I[6]*local[2])*angular,(mass.I[8]*local[0]+mass.I[9]*local[1]+mass.I[10]*local[2])*angular,torque);
   // 100665c0 divides by 1000 before 10067000's near-zero test, then multiplies back.
   if(std::abs(torque[0]/1000)>.0001f||std::abs(torque[1]/1000)>.0001f||std::abs(torque[2]/1000)>.0001f)dBodyAddTorque(body.body,torque[0],torque[1],torque[2]);
  }
  const int iterations=int(engine_.number("ph_iterations"));int stepped;
  if(iterations>0){dWorldSetQuickStepNumIterations(world_,iterations);stepped=dWorldQuickStep(world_,interval);}else stepped=dWorldStep(world_,interval);
  dJointGroupEmpty(contacts_);
  if(!stepped){
   // ODE guarantees unchanged objects on allocation failure; discard only our damping corrections.
   for(auto& [id,body]:bodies_){dBodySetForce(body.body,body.force[0],body.force[1],body.force[2]);dBodySetTorque(body.body,body.torque[0],body.torque[1],body.torque[2]);}
   runtime_=nullptr;throw std::runtime_error("ODE world step could not allocate working memory");
  }
  Vec3 lo{-100000,-100000,-100000},hi{100000,100000,100000};
  if(engine_.world){const auto& mesh=engine_.world->mesh;if(mesh.max.x>mesh.min.x||mesh.max.y>mesh.min.y||mesh.max.z>mesh.min.z){lo=mesh.min;hi=mesh.max;}}
  for(auto& [id,body]:bodies_){
   const auto* p=dBodyGetPosition(body.body);const float radius=body.radius;
   // 1007a6a0 restores only position and zeros velocities; it does not reset orientation or forces.
   if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2])||p[0]<lo.x-radius||p[0]>hi.x+radius||p[1]<lo.y-radius||p[1]>hi.y+radius||p[2]<lo.z-radius||p[2]>hi.z+radius){
    dBodySetLinearVel(body.body,0,0,0);dBodySetAngularVel(body.body,0,0,0);dBodySetPosition(body.body,body.previousPosition[0],body.previousPosition[1],body.previousPosition[2]);
   }
   engine_.synchronize_query_body(id,body.geom);
   snapshot(body.currentPosition,body.currentQuaternion,body.body);body.selectedQuaternion=body.currentQuaternion;dBodySetForce(body.body,body.force[0],body.force[1],body.force[2]);dBodySetTorque(body.body,body.torque[0],body.torque[1],body.torque[2]);
  }
  accumulator_-=interval;completed=true;
 }
 for(auto& [id,body]:bodies_){copy_pose(body,accumulator_/interval);if(completed){dBodySetForce(body.body,0,0,0);dBodySetTorque(body.body,0,0,0);}}
 runtime_=nullptr;
}
}
