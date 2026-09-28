#include "prism_test.h"
#include "prism/core/engine.h"
#include "prism/ecs/ecs.h"
#include "prism/jobs/job_system.h"
#include "prism/input/input.h"
#include "prism/physics2d/physics2d.h"
#include <atomic>
using namespace prism;

PRISM_TEST(core_bus_typed_events) {
    EventBus b; int count=0;
    auto id=b.subscribe<TouchBegan>([&](const TouchBegan& e){count+=e.id+1;});
    PRISM_CHECK_EQ(b.publish(TouchBegan{2,0.5f,0.5f,1}),std::size_t(1));
    PRISM_CHECK_EQ(count,3);
    b.unsubscribe<TouchBegan>(id);
    PRISM_CHECK_EQ(b.publish(TouchBegan{2,0.5f,0.5f,1}),std::size_t(0));
    PRISM_CHECK_EQ(count,3);
}
PRISM_TEST(core_ecs_generation) {
    World w;auto e=w.create("Ray");
    w.transform(e).position.x=2;
    w.add<Health>(e,Health{42,100});
    int hits=0;
    w.each<Health>([&](EntityId id,Transform& t,Health& h){
        PRISM_CHECK(id==e); PRISM_CHECK_NEAR(t.position.x,2,1e-6);
        PRISM_CHECK_NEAR(h.current,42,1e-6); ++hits;
    });
    PRISM_CHECK_EQ(hits,1);
    w.destroy(e); PRISM_CHECK(!w.alive(e));
    auto recycled=w.create(); PRISM_CHECK_EQ(recycled.index,e.index);
    PRISM_CHECK(recycled.generation!=e.generation);
    PRISM_CHECK(w.transform(recycled).position==math::Vec3(0,0,0));
}
PRISM_TEST(core_jobs_parallel) {
    JobSystem jobs(2);std::atomic<int> sum{0};
    auto h=jobs.parallel_for(100,[&](int i){sum.fetch_add(i);},4);
    jobs.wait(h);PRISM_CHECK(h.done());
    PRISM_CHECK_EQ(sum.load(),4950);
    jobs.wait_all();PRISM_CHECK_EQ(jobs.pending(),u64(0));
}
PRISM_TEST(core_input_gestures) {
    input::GestureRecognizer gr;
    gr.touch_down(0,10,10,0);
    gr.touch_up(0,11,10,0.1f);
    PRISM_CHECK_EQ(static_cast<int>(gr.gestures().back().kind),static_cast<int>(input::GestureKind::Tap));
    gr.clear_gestures();
    gr.touch_down(0,11,10,0.2f);
    gr.touch_up(0,12,10,0.25f);
    PRISM_CHECK_EQ(static_cast<int>(gr.gestures().back().kind),static_cast<int>(input::GestureKind::DoubleTap));
}
PRISM_TEST(core_physics_fall_and_contact) {
    physics2d::World world;
    physics2d::Body g;g.type=physics2d::BodyType::Static;g.position={0,-2};
    physics2d::Shape box;box.half_extents={5,0.5f};g.shapes.push_back(box);
    auto ground=world.create_body(g);
    physics2d::Body p;p.position={0,0};box.half_extents={0.5f,0.5f};p.shapes.push_back(box);
    auto player=world.create_body(p);
    for(int i=0;i<180;++i)world.step(1.0f/60);
    std::printf("physics pos=%f vel=%f contacts=%zu\n",world.body(player)->position.y,world.body(player)->linear_velocity.y,world.contacts().size());
    PRISM_CHECK(world.body(player)->position.y<0);
    PRISM_CHECK(world.body(player)->position.y>-2.1f);
    PRISM_CHECK_EQ(world.stats().bodies,2);
    PRISM_CHECK(world.raycast({0,1},{0,-4})==player);
    PRISM_CHECK(world.checksum()!=0);
    (void)ground;
}
PRISM_TEST(core_rng_repeatable) {
    Rng a(42),b(42),c(43);
    PRISM_CHECK_EQ(a.next(),b.next());
    PRISM_CHECK(a.next()!=c.next());
}
