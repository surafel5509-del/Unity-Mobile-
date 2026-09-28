#include "prism/physics2d/physics2d.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace prism::physics2d {
namespace {
math::Vec2 rotate(math::Vec2 v, f32 a) {
    f32 c = std::cos(a), s = std::sin(a);
    return {c*v.x-s*v.y, s*v.x+c*v.y};
}
f32 cross(math::Vec2 a, math::Vec2 b) { return a.x*b.y-a.y*b.x; }
math::Vec2 cross(f32 a, math::Vec2 v) { return {-a*v.y,a*v.x}; }
f32 project_radius(const Shape& s, f32 angle, math::Vec2 axis) {
    math::Vec2 x = rotate({1,0},angle), y = rotate({0,1},angle);
    return s.half_extents.x*std::fabs(x.dot(axis))+s.half_extents.y*std::fabs(y.dot(axis));
}
}
void Body::set_mass_from_shapes() {
    if (type != BodyType::Dynamic) { mass = inv_mass = inertia = inv_inertia = 0; return; }
    mass = inertia = 0;
    for (auto& s:shapes) {
        if (s.sensor) continue;
        f32 m = s.density*(s.type==ShapeType::Circle ? math::Pi*s.radius*s.radius : 4*s.half_extents.x*s.half_extents.y);
        mass += m;
        inertia += s.type==ShapeType::Circle ? m*s.radius*s.radius*0.5f : m*(s.half_extents.x*s.half_extents.x+s.half_extents.y*s.half_extents.y)/3;
    }
    if (mass <= kEpsilon) mass = 1;
    inv_mass = 1/mass;
    inv_inertia = (!fixed_rotation && inertia > kEpsilon) ? 1/inertia : 0;
}
AABB Body::aabb() const {
    AABB a{{position.x,position.y},{position.x,position.y}};
    for (auto& s:shapes) {
        math::Vec2 e;
        if (s.type==ShapeType::Circle) e={s.radius,s.radius};
        else {
            math::Vec2 x=rotate({1,0},angle),y=rotate({0,1},angle);
            e={std::fabs(x.x)*s.half_extents.x+std::fabs(y.x)*s.half_extents.y,
               std::fabs(x.y)*s.half_extents.x+std::fabs(y.y)*s.half_extents.y};
        }
        a.merge({position-e,position+e});
    }
    return a;
}
math::Vec2 Body::world_point(const math::Vec2& local) const { return position+rotate(local,angle); }
math::Vec2 Body::velocity_at(const math::Vec2& p) const { return linear_velocity+cross(angular_velocity,p-position); }
void Body::apply_impulse(const math::Vec2& impulse,const math::Vec2& p) {
    if(type!=BodyType::Dynamic) return;
    linear_velocity+=impulse*inv_mass;
    angular_velocity+=cross(p-position,impulse)*inv_inertia;
    wake();
}
BodyId World::create_body(const Body& desc) {
    BodyId id;
    if(!free_ids_.empty()){id=free_ids_.back();free_ids_.pop_back();}
    else {id=static_cast<BodyId>(bodies_.size());bodies_.push_back(nullptr);}
    bodies_[id]=std::make_unique<Body>(desc);
    bodies_[id]->id=id;
    bodies_[id]->set_mass_from_shapes();
    return id;
}
void World::destroy_body(BodyId id) { if(body(id)){bodies_[id].reset();free_ids_.push_back(id);} }
Body* World::body(BodyId id) { return id<bodies_.size()?bodies_[id].get():nullptr; }
const Body* World::body(BodyId id) const {return id<bodies_.size()?bodies_[id].get():nullptr;}
u32 World::add_joint(const Joint& j){joints_.push_back(j);return static_cast<u32>(joints_.size()-1);}
void World::remove_joint(u32 id){if(id<joints_.size())joints_[id].a=joints_[id].b=kNullBody;}
Joint* World::joint(u32 id){return id<joints_.size()?&joints_[id]:nullptr;}
void World::set_layer_collision(u32 a,u32 b,bool enabled){
    if(a>=32||b>=32)return;
    if(layer_matrix_.empty())layer_matrix_.assign(32*32,true);
    layer_matrix_[a*32+b]=layer_matrix_[b*32+a]=enabled;
}
bool World::layers_collide(u32 a,u32 b) const {
    return a<32&&b<32&&(layer_matrix_.empty()||layer_matrix_[a*32+b]);
}
void World::broadphase(std::vector<std::pair<BodyId,BodyId>>& pairs) const {
    for(u32 i=0;i<bodies_.size();++i){
        if(!body(i))continue;
        for(u32 j=i+1;j<bodies_.size();++j){
            if(!body(j) || (body(i)->type==BodyType::Static && body(j)->type==BodyType::Static))continue;
            if(body(i)->aabb().overlaps(body(j)->aabb()))pairs.emplace_back(i,j);
        }
    }
}
bool World::circle_circle(const Body& a,const Shape& sa,const Body& b,const Shape& sb,Contact& out) const {
    math::Vec2 d=b.position-a.position;
    f32 r=sa.radius+sb.radius,dist=d.length();
    if(dist>=r)return false;
    out.normal=dist>kEpsilon?d/dist:math::Vec2{0,1};
    out.penetration=r-dist;
    out.point=a.position+out.normal*(sa.radius-out.penetration*0.5f);
    return true;
}
bool World::box_box(const Body& a,const Shape& sa,const Body& b,const Shape& sb,Contact& out) const {
    math::Vec2 axes[]={rotate({1,0},a.angle),rotate({0,1},a.angle),rotate({1,0},b.angle),rotate({0,1},b.angle)};
    f32 best=1e30f; math::Vec2 normal;
    math::Vec2 d=b.position-a.position;
    for(auto axis:axes){
        f32 overlap=project_radius(sa,a.angle,axis)+project_radius(sb,b.angle,axis)-std::fabs(d.dot(axis));
        if(overlap<=0)return false;
        if(overlap<best){best=overlap;normal=d.dot(axis)<0?axis*-1:axis;}
    }
    out.normal=normal;
    out.penetration=best;
    out.point=(a.position+b.position)*0.5f;
    return true;
}
bool World::box_circle(const Body& a,const Shape& sa,const Body& b,const Shape& sb,Contact& out) const {
    math::Vec2 local=rotate(b.position-a.position,-a.angle);
    math::Vec2 closest{math::clampf(local.x,-sa.half_extents.x,sa.half_extents.x),
                       math::clampf(local.y,-sa.half_extents.y,sa.half_extents.y)};
    math::Vec2 delta=local-closest;
    f32 dist=delta.length();
    if(dist>=sb.radius)return false;
    if(dist<kEpsilon){
        // Center inside box: choose nearest face, push outward.
        f32 dx=sa.half_extents.x-std::fabs(local.x);
        f32 dy=sa.half_extents.y-std::fabs(local.y);
        if(dx<dy){delta={local.x<0?-1.0f:1.0f,0};out.penetration=sb.radius+dx;}
        else{delta={0,local.y<0?-1.0f:1.0f};out.penetration=sb.radius+dy;}
    }else{delta=delta/dist;out.penetration=sb.radius-dist;}
    out.normal=rotate(delta,a.angle);
    out.point=a.world_point(closest);
    return true;
}
bool World::narrowphase(Body& a,Body& b,Contact& out) const {
    for(auto& sa:a.shapes)for(auto& sb:b.shapes){
        if((sa.mask&sb.layer)==0||(sb.mask&sa.layer)==0)continue;
        bool hit=false;
        if(sa.type==ShapeType::Circle && sb.type==ShapeType::Circle)hit=circle_circle(a,sa,b,sb,out);
        else if(sa.type==ShapeType::Box && sb.type==ShapeType::Box)hit=box_box(a,sa,b,sb,out);
        else if(sa.type==ShapeType::Box && sb.type==ShapeType::Circle)hit=box_circle(a,sa,b,sb,out);
        else if(sa.type==ShapeType::Circle && sb.type==ShapeType::Box){hit=box_circle(b,sb,a,sa,out);out.normal=out.normal*-1;}
        // Polygon/capsule are reserved shape descriptors until their narrowphase is implemented.
        if(hit){out.a=a.id;out.b=b.id;out.sensor=sa.sensor||sb.sensor;return true;}
    }
    return false;
}
void World::integrate(f32 dt){
    for(auto& p:bodies_){
        if(!p||p->sleeping||p->type==BodyType::Static)continue;
        auto& b=*p;
        if(b.type==BodyType::Dynamic){
            b.linear_velocity+=(cfg_.gravity*b.gravity_scale+b.force*b.inv_mass)*dt;
            b.angular_velocity+=b.torque*b.inv_inertia*dt;
            b.linear_velocity*=1.0f/(1.0f+b.linear_damping*dt);
            b.angular_velocity*=1.0f/(1.0f+b.angular_damping*dt);
        }
        math::Vec2 translation=b.linear_velocity*dt;
        f32 len=translation.length();
        if(len>cfg_.max_translation_per_step)translation*=cfg_.max_translation_per_step/len;
        b.position+=translation;
        b.angle+=b.angular_velocity*dt;
        b.force={0,0}; b.torque=0;
    }
}
void World::solve_contacts(){
    for(i32 iter=0;iter<cfg_.velocity_iterations;++iter){
        for(auto& c:contacts_){
            if(c.sensor||c.ended)continue;
            Body& a=*body(c.a);Body& b=*body(c.b);
            math::Vec2 ra=c.point-a.position,rb=c.point-b.position;
            math::Vec2 rv=b.velocity_at(c.point)-a.velocity_at(c.point);
            f32 vel=rv.dot(c.normal);
            if(vel>0)continue;
            f32 ran=cross(ra,c.normal),rbn=cross(rb,c.normal);
            f32 denom=a.inv_mass+b.inv_mass+ran*ran*a.inv_inertia+rbn*rbn*b.inv_inertia;
            if(denom<kEpsilon)continue;
            f32 bounce=0.2f;
            f32 j=-(1+bounce)*vel/denom;
            math::Vec2 impulse=c.normal*j;
            a.apply_impulse(impulse*-1,c.point);b.apply_impulse(impulse,c.point);
            c.impulse+=j;
            rv=b.velocity_at(c.point)-a.velocity_at(c.point);
            math::Vec2 tangent=(rv-c.normal*rv.dot(c.normal)).normalized();
            if(tangent.length_sq()<kEpsilon)continue;
            f32 rat=cross(ra,tangent),rbt=cross(rb,tangent);
            f32 d=a.inv_mass+b.inv_mass+rat*rat*a.inv_inertia+rbt*rbt*b.inv_inertia;
            if(d<kEpsilon)continue;
            f32 jt=-rv.dot(tangent)/d;
            f32 friction=0.3f;
            jt=math::clampf(jt,-j*friction,j*friction);
            a.apply_impulse(tangent*-jt,c.point);b.apply_impulse(tangent*jt,c.point);
        }
    }
    for(i32 iter=0;iter<cfg_.position_iterations;++iter)
        for(auto& c:contacts_){
            if(c.sensor||c.ended)continue;
            Body& a=*body(c.a);Body& b=*body(c.b);
            f32 inv=a.inv_mass+b.inv_mass;
            if(inv<kEpsilon)continue;
            math::Vec2 correction=c.normal*(std::max(c.penetration-0.005f,0.0f)*0.2f/inv);
            if(a.type==BodyType::Dynamic)a.position-=correction*a.inv_mass;
            if(b.type==BodyType::Dynamic)b.position+=correction*b.inv_mass;
        }
}
void World::sleep_pass(f32 dt){
    for(auto& p:bodies_){
        if(!p||p->type!=BodyType::Dynamic||!p->allow_sleep||p->sleeping)continue;
        if(p->linear_velocity.length()<cfg_.linear_sleep_threshold&&std::fabs(p->angular_velocity)<cfg_.angular_sleep_threshold){
            p->sleep_timer+=dt;
            if(p->sleep_timer>cfg_.sleep_delay){p->sleeping=true;p->linear_velocity={0,0};p->angular_velocity=0;}
        }else p->sleep_timer=0;
    }
}
void World::step(f32 dt){
    auto start=std::chrono::steady_clock::now();
    time_left_+=std::min(dt,0.25f);
    int substeps=0;
    while(time_left_>=cfg_.fixed_dt&&substeps++<8){
        time_left_-=cfg_.fixed_dt;
        integrate(cfg_.fixed_dt);
        prev_contacts_=std::move(contacts_);contacts_.clear();
        pairs_.clear();broadphase(pairs_);
        stats_.pairs_tested+=pairs_.size();
        for(auto [ai,bi]:pairs_){
            Contact c;
            if(narrowphase(*body(ai),*body(bi),c)){
                ++stats_.pairs_collided;
                c.started=true;
                for(auto& old:prev_contacts_)if(old.a==c.a&&old.b==c.b&&!old.ended){c.started=false;break;}
                contacts_.push_back(c);
            }
        }
        for(auto& c:prev_contacts_){
            bool active=false;
            for(auto& n:contacts_)if(n.a==c.a&&n.b==c.b){active=true;break;}
            if(!active){c.ended=true;c.started=false;contacts_.push_back(c);}
        }
        solve_contacts();sleep_pass(cfg_.fixed_dt);
        ++stats_.steps;stats_.contacts=contacts_.size();
    }
    stats_.bodies=stats_.awake=0;
    for(auto& b:bodies_)if(b){++stats_.bodies;if(!b->sleeping)++stats_.awake;}
    stats_.joints=static_cast<i32>(joints_.size());
    stats_.step_ms=std::chrono::duration<f64,std::milli>(std::chrono::steady_clock::now()-start).count();
}
BodyId World::raycast(math::Vec2 from,math::Vec2 to,bool sensors) const {
    math::Vec2 d=to-from;
    BodyId found=kNullBody;f32 best=1e30f;
    for(auto& bp:bodies_){
        if(!bp)continue;
        if(!sensors){bool all_sensor=true;for(auto& s:bp->shapes)all_sensor&=s.sensor;if(all_sensor)continue;}
        AABB a=bp->aabb();f32 t0=0,t1=1;
        for(int axis=0;axis<2;++axis){
            if(std::fabs(d[axis])<kEpsilon){if(from[axis]<a.min[axis]||from[axis]>a.max[axis]){t0=2;break;}}
            else{f32 inv=1/d[axis],lo=(a.min[axis]-from[axis])*inv,hi=(a.max[axis]-from[axis])*inv;
                if(lo>hi)std::swap(lo,hi);t0=std::max(t0,lo);t1=std::min(t1,hi);}
        }
        if(t0<=t1&&t0<best){best=t0;found=bp->id;}
    }
    return found;
}
std::vector<BodyId> World::overlap_aabb(const AABB& a) const {
    std::vector<BodyId> out;for(auto& b:bodies_)if(b&&b->aabb().overlaps(a))out.push_back(b->id);return out;
}
u64 World::checksum() const {
    u64 h=1469598103934665603ull;
    for(auto& b:bodies_){if(!b)continue;
        f32 values[]={b->position.x,b->position.y,b->angle,b->linear_velocity.x,b->linear_velocity.y};
        for(f32 f:values){u32 bits;std::memcpy(&bits,&f,4);h=(h^bits)*1099511628211ull;}
    }
    return h;
}
} // namespace prism::physics2d
