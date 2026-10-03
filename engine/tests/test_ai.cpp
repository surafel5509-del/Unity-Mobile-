// PRISM ENGINE — game AI tests: A*, navmesh funnel, flow fields, behaviour
// trees, state machines, GOAP, utility AI, steering and crowds.
#include "prism_test.h"
#include "prism/ai/ai.h"
#include <cmath>

using namespace prism;
using namespace prism::ai;

namespace {
/// A 10x10 grid with a vertical wall at x=5 (gap at y=0..1).
GridNav make_maze() {
    GridNav g(10, 10);
    g.fill_rect(5, 2, 5, 9);
    return g;
}
bool step_ok(const std::vector<std::pair<i32,i32>>& p) {
    for (std::size_t i = 1; i < p.size(); ++i) {
        const i32 dx = std::abs(p[i].first - p[i-1].first);
        const i32 dy = std::abs(p[i].second - p[i-1].second);
        if (dx > 1 || dy > 1 || (dx == 0 && dy == 0)) return false;
    }
    return true;
}
} // namespace

PRISM_TEST(ai_grid_astar_around_obstacle) {
    GridNav g = make_maze();
    PRISM_CHECK(g.blocked(5, 5));
    PRISM_CHECK(!g.blocked(4, 5));
    PRISM_CHECK(g.blocked(-1, 0));           // out of bounds is blocked
    PRISM_CHECK(!g.inside(10, 10));

    auto r = g.find_path(0, 5, 9, 5);
    PRISM_CHECK(r.found);
    PRISM_CHECK(r.path.size() >= 2);
    PRISM_CHECK_EQ(r.path.front().first, 0);
    PRISM_CHECK_EQ(r.path.back().first, 9);
    PRISM_CHECK(step_ok(r.path));
    for (const auto& c : r.path) PRISM_CHECK(!g.blocked(c.first, c.second));
    PRISM_CHECK(r.expanded > 0);
    // the path must route through the gap at (5,0) or (5,1)
    bool via_gap = false;
    for (const auto& c : r.path) if (c.first == 5 && c.second <= 1) via_gap = true;
    PRISM_CHECK(via_gap);

    // unreachable: seal the wall completely
    g.fill_rect(5, 0, 5, 9);
    PRISM_CHECK(!g.find_path(0, 5, 9, 5).found);
    // start == goal
    auto trivial = g.find_path(2, 2, 2, 2);
    PRISM_CHECK(trivial.found);
    PRISM_CHECK_EQ(static_cast<int>(trivial.path.size()), 1);
    // blocked endpoints
    PRISM_CHECK(!g.find_path(5, 5, 2, 2).found);
}

PRISM_TEST(ai_grid_no_corner_cutting_and_los) {
    GridNav g(8, 8);
    g.set_blocked(3, 3);
    g.set_blocked(4, 4);
    // LOS through a blocked cell must fail
    PRISM_CHECK(!g.line_of_sight(2, 2, 4, 4));
    PRISM_CHECK(g.line_of_sight(0, 0, 0, 7));
    PRISM_CHECK(g.line_of_sight(5, 5, 5, 5));
    // diagonal between two blocked orthogonal cells is refused
    GridNav h(4, 4);
    h.set_blocked(1, 0);
    h.set_blocked(0, 1);
    auto r = h.find_path(0, 0, 1, 1, true);
    PRISM_CHECK(!r.found);
    // smoothing removes redundant waypoints on an open field
    GridNav open(20, 20);
    auto straight = open.find_path(0, 0, 19, 19);
    PRISM_CHECK(straight.found);
    auto smoothed = open.smooth(straight.path);
    PRISM_CHECK(smoothed.size() <= straight.path.size());
    PRISM_CHECK(smoothed.size() <= 2);       // a straight diagonal collapses to 2 points
    PRISM_CHECK_EQ(smoothed.front().first, 0);
    PRISM_CHECK_EQ(smoothed.back().first, 19);
}

PRISM_TEST(ai_grid_distance_field) {
    GridNav g = make_maze();
    auto field = g.distance_field(9, 9);
    PRISM_CHECK_EQ(static_cast<int>(field.size()), 100);
    PRISM_CHECK_NEAR(field[9 * 10 + 9], 0.0f, 1e-6);
    // cost grows with distance
    PRISM_CHECK(field[8 * 10 + 9] < field[5 * 10 + 9]);
    // blocked cells stay unreachable
    PRISM_CHECK(field[5 * 10 + 5] >= GridNav::kUnreachable * 0.5f);
    // out-of-range goal yields nothing reachable
    auto none = g.distance_field(-5, -5);
    bool any = false;
    for (f32 c : none) if (c < GridNav::kUnreachable * 0.5f) any = true;
    PRISM_CHECK(!any);
}

PRISM_TEST(ai_navmesh_path_and_funnel) {
    NavMesh mesh;
    // two quads in the XZ plane sharing the edge x=10
    mesh.add_polygon({Vec3(0,0,0), Vec3(10,0,0), Vec3(10,0,10), Vec3(0,0,10)});
    mesh.add_polygon({Vec3(10,0,0), Vec3(20,0,0), Vec3(20,0,10), Vec3(10,0,10)});
    mesh.build_adjacency();
    PRISM_CHECK_EQ(static_cast<int>(mesh.polygon_count()), 2);
    PRISM_CHECK_EQ(static_cast<int>(mesh.polygons()[0].neighbours.size()), 1);
    PRISM_CHECK_EQ(mesh.polygon_at(Vec2(5, 5)), 0);
    PRISM_CHECK_EQ(mesh.polygon_at(Vec2(15, 5)), 1);
    PRISM_CHECK_EQ(mesh.polygon_at(Vec2(50, 5)), -1);
    PRISM_CHECK(mesh.polygons()[0].contains(Vec2(5, 5)));
    PRISM_CHECK(!mesh.polygons()[0].contains(Vec2(15, 5)));

    auto r = mesh.find_path(Vec3(5, 0, 5), Vec3(15, 0, 5));
    PRISM_CHECK(r.found);
    PRISM_CHECK_EQ(static_cast<int>(r.polys.size()), 2);
    // unobstructed corridor: string pulling leaves just start and end
    PRISM_CHECK_EQ(static_cast<int>(r.path.size()), 2);
    PRISM_CHECK_NEAR(r.path.front().x, 5.0f, 1e-4);
    PRISM_CHECK_NEAR(r.path.back().x, 15.0f, 1e-4);

    // endpoints outside the mesh
    PRISM_CHECK(!mesh.find_path(Vec3(100, 0, 100), Vec3(15, 0, 5)).found);
    // same polygon: direct line
    auto same = mesh.find_path(Vec3(2, 0, 2), Vec3(8, 0, 8));
    PRISM_CHECK(same.found);
    PRISM_CHECK_EQ(static_cast<int>(same.path.size()), 2);

    // portals: one shared edge between two polygons
    auto portals = mesh.corridor_portals({0, 1});
    PRISM_CHECK_EQ(static_cast<int>(portals.size()), 1);
    auto funnel = NavMesh::funnel(Vec3(5,0,5), Vec3(15,0,5), portals);
    PRISM_CHECK_EQ(static_cast<int>(funnel.size()), 2);
    // no portals at all -> straight line
    PRISM_CHECK_EQ(static_cast<int>(NavMesh::funnel(Vec3(0,0,0), Vec3(1,0,1), {}).size()), 2);
}

PRISM_TEST(ai_navmesh_raycast_exits_the_mesh) {
    NavMesh mesh;
    mesh.add_polygon({Vec3(0,0,0), Vec3(10,0,0), Vec3(10,0,10), Vec3(0,0,10)});
    mesh.add_polygon({Vec3(10,0,0), Vec3(20,0,0), Vec3(20,0,10), Vec3(10,0,10)});
    mesh.build_adjacency();

    u32 hit = kInvalidId;
    Vec3 point;
    // stays inside both polygons
    PRISM_CHECK(!mesh.raycast(Vec3(2,0,5), Vec3(18,0,5), hit, point));
    PRISM_CHECK_EQ(static_cast<int>(hit), 1);
    // crosses the outer boundary at x=20
    PRISM_CHECK(mesh.raycast(Vec3(2,0,5), Vec3(30,0,5), hit, point));
    PRISM_CHECK_NEAR(point.x, 20.0f, 0.01f);
    // origin outside the mesh
    PRISM_CHECK(!mesh.raycast(Vec3(-50,0,-50), Vec3(5,0,5), hit, point));
}

PRISM_TEST(ai_flow_field_points_at_the_goal) {
    GridNav g = make_maze();
    FlowField f;
    f.build(g, 9, 9);
    PRISM_CHECK_EQ(f.goal_x(), 9);
    PRISM_CHECK(f.reachable(0, 0));
    PRISM_CHECK(!f.reachable(5, 5));            // sealed wall cell
    PRISM_CHECK(f.reachable_cells() > 50);
    // on the open right side the direction points up/right toward the goal
    Vec2 d = f.direction_at(9, 5);
    PRISM_CHECK(d.y > 0.0f);
    PRISM_CHECK_NEAR(d.length(), 1.0f, 1e-4);   // normalised
    // cost decreases along the descent direction
    PRISM_CHECK(f.cost_at(9, 6) < f.cost_at(9, 5));
    // out of bounds is safe
    PRISM_CHECK_NEAR(f.direction_at(-5, -5).length(), 0.0f, 1e-6);
    PRISM_CHECK(f.cost_at(100, 100) >= GridNav::kUnreachable * 0.5f);
    // one solve serves many agents: every reachable cell has a direction
    i32 with_dir = 0;
    for (i32 y = 0; y < f.height(); ++y)
        for (i32 x = 0; x < f.width(); ++x)
            if (f.reachable(x, y) && f.direction_at(x, y).length_sq() > 0.1f) ++with_dir;
    PRISM_CHECK(with_dir > 60);
}

PRISM_TEST(ai_blackboard_roundtrip) {
    Blackboard bb;
    bb.set("hp", 42.0);
    bb.set("alive", true);
    bb.set("name", "Ray");
    PRISM_CHECK_NEAR(bb.number("hp"), 42.0, 1e-9);
    PRISM_CHECK(bb.boolean("alive"));
    PRISM_CHECK_STR(bb.text("name"), "Ray");
    PRISM_CHECK(bb.has("hp"));
    PRISM_CHECK(!bb.has("missing"));
    PRISM_CHECK_NEAR(bb.number("missing", -1.0), -1.0, 1e-9);
    PRISM_CHECK_STR(bb.text("missing", "fallback"), "fallback");
    bb.erase("hp");
    PRISM_CHECK(!bb.has("hp"));
    bb.clear();
    PRISM_CHECK_EQ(static_cast<int>(bb.size()), 0);
}

PRISM_TEST(ai_behavior_tree_composites) {
    Blackboard bb;
    // --- sequence: stops at the first failure ---
    int b_ran = 0, c_ran = 0;
    auto seq = std::make_shared<BtSequence>();
    seq->add(std::make_shared<BtAction>("a", [&](Blackboard&, f32) { return NodeStatus::Success; }));
    seq->add(std::make_shared<BtAction>("b", [&](Blackboard&, f32) { ++b_ran; return NodeStatus::Failure; }));
    seq->add(std::make_shared<BtAction>("c", [&](Blackboard&, f32) { ++c_ran; return NodeStatus::Success; }));
    PRISM_CHECK(seq->tick(bb, 0.016f) == NodeStatus::Failure);
    PRISM_CHECK_EQ(b_ran, 1);
    PRISM_CHECK_EQ(c_ran, 0);

    // --- selector: stops at the first success ---
    int y_ran = 0;
    auto sel = std::make_shared<BtSelector>();
    sel->add(std::make_shared<BtAction>("x", [&](Blackboard&, f32) { return NodeStatus::Failure; }));
    sel->add(std::make_shared<BtAction>("y", [&](Blackboard&, f32) { ++y_ran; return NodeStatus::Success; }));
    sel->add(std::make_shared<BtAction>("z", [&](Blackboard&, f32) { return NodeStatus::Failure; }));
    PRISM_CHECK(sel->tick(bb, 0.016f) == NodeStatus::Success);
    PRISM_CHECK_EQ(y_ran, 1);

    // --- a Running child is resumed, not restarted ---
    int ticks = 0;
    auto resume = std::make_shared<BtSequence>();
    resume->add(std::make_shared<BtAction>("slow", [&](Blackboard&, f32) {
        ++ticks;
        return ticks < 3 ? NodeStatus::Running : NodeStatus::Success;
    }));
    PRISM_CHECK(resume->tick(bb, 0.016f) == NodeStatus::Running);
    PRISM_CHECK(resume->tick(bb, 0.016f) == NodeStatus::Running);
    PRISM_CHECK(resume->tick(bb, 0.016f) == NodeStatus::Success);
    PRISM_CHECK_EQ(ticks, 3);

    // --- inverter / condition ---
    auto inv = std::make_shared<BtInverter>(
        std::make_shared<BtCondition>("has_ammo", [&](Blackboard& b) { return b.boolean("ammo"); }));
    PRISM_CHECK(inv->tick(bb, 0.016f) == NodeStatus::Success);   // no ammo -> condition fails -> inverter succeeds
    bb.set("ammo", true);
    PRISM_CHECK(inv->tick(bb, 0.016f) == NodeStatus::Failure);

    // --- repeater runs a fixed number of times ---
    int reps = 0;
    auto rep = std::make_shared<BtRepeater>(
        std::make_shared<BtAction>("count", [&](Blackboard&, f32) { ++reps; return NodeStatus::Success; }), 4);
    for (int i = 0; i < 3; ++i) PRISM_CHECK(rep->tick(bb, 0.016f) == NodeStatus::Running);
    PRISM_CHECK(rep->tick(bb, 0.016f) == NodeStatus::Success);
    PRISM_CHECK_EQ(reps, 4);

    // --- cooldown blocks re-entry ---
    int fired = 0;
    auto cd = std::make_shared<BtCooldown>(
        std::make_shared<BtAction>("fire", [&](Blackboard&, f32) { ++fired; return NodeStatus::Success; }), 1.0f);
    PRISM_CHECK(cd->tick(bb, 0.5f) == NodeStatus::Success);      // first tick always allowed
    PRISM_CHECK_EQ(fired, 1);
    PRISM_CHECK(cd->tick(bb, 0.2f) == NodeStatus::Failure);      // cooling down
    PRISM_CHECK(cd->tick(bb, 0.9f) == NodeStatus::Success);      // elapsed
    PRISM_CHECK_EQ(fired, 2);

    // --- timeout fails a stuck child ---
    auto to = std::make_shared<BtTimeout>(
        std::make_shared<BtAction>("stuck", [&](Blackboard&, f32) { return NodeStatus::Running; }), 0.5f);
    PRISM_CHECK(to->tick(bb, 0.2f) == NodeStatus::Running);
    PRISM_CHECK(to->tick(bb, 0.4f) == NodeStatus::Failure);

    // --- parallel ---
    auto par = std::make_shared<BtParallel>("par", true);
    par->add(std::make_shared<BtAction>("p1", [&](Blackboard&, f32) { return NodeStatus::Success; }));
    par->add(std::make_shared<BtAction>("p2", [&](Blackboard&, f32) { return NodeStatus::Success; }));
    PRISM_CHECK(par->tick(bb, 0.016f) == NodeStatus::Success);
    auto par2 = std::make_shared<BtParallel>("par2", true);
    par2->add(std::make_shared<BtAction>("p1", [&](Blackboard&, f32) { return NodeStatus::Success; }));
    par2->add(std::make_shared<BtAction>("p2", [&](Blackboard&, f32) { return NodeStatus::Failure; }));
    PRISM_CHECK(par2->tick(bb, 0.016f) == NodeStatus::Failure);

    PRISM_CHECK_STR(node_status_name(NodeStatus::Running), "running");
}

PRISM_TEST(ai_state_machine_transitions) {
    StateMachine sm;
    int idle_updates = 0, chase_updates = 0, enters = 0;
    Blackboard bb;
    sm.add_state(StateMachine::State{"idle",
        [&](f32) { ++enters; }, [&](f32) { ++idle_updates; }, nullptr, 0.5f});
    sm.add_state(StateMachine::State{"chase",
        nullptr, [&](f32) { ++chase_updates; }, nullptr, 0.0f});
    sm.add_state(StateMachine::State{"attack", nullptr, nullptr, nullptr, 0.0f});
    sm.add_transition(StateMachine::Transition{"idle", "chase",
        [&]() { return bb.boolean("enemy_visible"); }, 0.0f, 1e9f});
    sm.add_transition(StateMachine::Transition{"chase", "attack",
        [&]() { return bb.number("distance") < 2.0; }, 0.0f, 1e9f});
    sm.add_transition(StateMachine::Transition{"attack", "idle",
        [&]() { return bb.number("distance") >= 2.0; }, 0.0f, 1e9f});

    sm.start("idle");
    PRISM_CHECK_STR(sm.current(), "idle");
    PRISM_CHECK_EQ(enters, 1);

    bb.set("enemy_visible", true);
    sm.update(0.1f);                          // min_duration 0.5 not met yet
    PRISM_CHECK_STR(sm.current(), "idle");
    PRISM_CHECK(idle_updates >= 1);
    sm.update(0.5f);
    PRISM_CHECK_STR(sm.current(), "chase");
    PRISM_CHECK(sm.transition_count() == 1);

    bb.set("distance", 1.0);
    sm.update(0.1f);
    PRISM_CHECK_STR(sm.current(), "attack");
    bb.set("distance", 5.0);
    sm.update(0.1f);
    PRISM_CHECK_STR(sm.current(), "idle");
    PRISM_CHECK(sm.transition_count() == 3);
    PRISM_CHECK(chase_updates >= 1);
    // forcing an unknown state is refused
    PRISM_CHECK(!sm.force("nonexistent"));
    PRISM_CHECK(sm.force("chase"));
    PRISM_CHECK_STR(sm.current(), "chase");
    PRISM_CHECK(sm.time_in_state() < 0.001f);
}

PRISM_TEST(ai_goap_plans_a_weapon_kill) {
    GoapPlanner planner;
    planner.add_action(GoapAction{"gather_stone", 2.0f, {}, {{0, 1}}, nullptr});
    planner.add_action(GoapAction{"approach_enemy", 1.0f, {}, {{2, 1}}, nullptr});
    planner.add_action(GoapAction{"attack_enemy", 3.0f, {{0, 1}, {2, 1}}, {{1, 0}}, nullptr});
    planner.add_action(GoapAction{"dance", 1.0f, {}, {{3, 1}}, nullptr});   // irrelevant
    PRISM_CHECK_EQ(static_cast<int>(planner.actions().size()), 4);

    WorldState start{};
    start[1] = 1;      // enemy alive
    auto plan = planner.plan(start, {{1, 0}});
    PRISM_CHECK(plan.found);
    PRISM_CHECK_EQ(static_cast<int>(plan.actions.size()), 3);
    PRISM_CHECK_STR(plan.actions.back(), "attack_enemy");   // must be last
    PRISM_CHECK_NEAR(plan.cost, 6.0f, 1e-4);
    bool gathered = false, approached = false;
    for (const auto& a : plan.actions) {
        if (a == "gather_stone") gathered = true;
        if (a == "approach_enemy") approached = true;
    }
    PRISM_CHECK(gathered && approached);
    PRISM_CHECK(plan.expanded > 0);

    // already satisfied -> empty plan
    WorldState done = start; done[1] = 0;
    auto none = planner.plan(done, {{1, 0}});
    PRISM_CHECK(none.found);
    PRISM_CHECK_EQ(static_cast<int>(none.actions.size()), 0);

    // unreachable goal
    auto impossible = planner.plan(start, {{5, 99}});
    PRISM_CHECK(!impossible.found);
    // empty goal is trivially satisfied
    PRISM_CHECK(planner.plan(start, {}).found);
}

PRISM_TEST(ai_utility_curves_and_selection) {
    Consideration lin;  lin.curve = Curve::Linear;
    Consideration sq;   sq.curve = Curve::Square; sq.exponent = 2.0f;
    Consideration inv;  inv.curve = Curve::InverseSquare; inv.exponent = 2.0f;
    Consideration sig;  sig.curve = Curve::Sigmoid;
    Consideration thr;  thr.curve = Curve::Threshold;
    Consideration lg;   lg.curve = Curve::Log;

    PRISM_CHECK_NEAR(lin.score(0.0f), 0.0f, 1e-5);
    PRISM_CHECK_NEAR(lin.score(1.0f), 1.0f, 1e-5);
    PRISM_CHECK_NEAR(sq.score(0.5f), 0.25f, 1e-5);      // x^2
    PRISM_CHECK_NEAR(inv.score(0.5f), 0.75f, 1e-5);     // 1 - x^2
    PRISM_CHECK(sq.score(0.8f) > sq.score(0.4f));       // monotonic
    PRISM_CHECK(inv.score(0.2f) > inv.score(0.8f));     // inverse monotonic
    PRISM_CHECK(sig.score(0.9f) > sig.score(0.1f));
    PRISM_CHECK_NEAR(thr.score(0.2f), 0.0f, 1e-5);
    PRISM_CHECK_NEAR(thr.score(0.8f), 1.0f, 1e-5);
    PRISM_CHECK(lg.score(1.0f) > lg.score(0.2f));
    // every curve is clamped to [0,1]
    for (Curve c : {Curve::Linear, Curve::Square, Curve::InverseSquare, Curve::Sigmoid,
                    Curve::Threshold, Curve::Log}) {
        Consideration k; k.curve = c;
        for (f32 x = -2.0f; x <= 3.0f; x += 0.25f) {
            f32 s = k.score(x);
            PRISM_CHECK(s >= 0.0f && s <= 1.0f);
        }
    }

    UtilityAi ai;
    UtilityAi::Option flee{"flee", {}, UtilityAi::Option::Combine::Average};
    flee.considerations.push_back(Consideration{"low_health", Curve::InverseSquare, 1.0f, 2.0f, 0, 0, 1.0f});
    UtilityAi::Option attack{"attack", {}, UtilityAi::Option::Combine::Average};
    attack.considerations.push_back(Consideration{"low_health", Curve::Square, 1.0f, 2.0f, 0, 0, 1.0f});
    ai.add_option(flee);
    ai.add_option(attack);

    Blackboard bb;
    bb.set("low_health", 0.9);            // healthy -> attack wins
    PRISM_CHECK_STR(ai.choose(bb), "attack");
    bb.set("low_health", 0.1);            // hurt -> flee wins
    PRISM_CHECK_STR(ai.choose(bb), "flee");
    auto scored = ai.score_all(bb);
    PRISM_CHECK_EQ(static_cast<int>(scored.size()), 2);
    // a threshold above every score yields no choice
    PRISM_CHECK_STR(ai.choose(bb, 0.99f), "");
}

PRISM_TEST(ai_steering_seek_arrive_flee) {
    SteeringAgent a;
    a.position = Vec2(0, 0);
    a.max_speed = 5.0f;
    a.max_force = 20.0f;
    a.arrival_radius = 0.5f;
    a.slow_radius = 3.0f;

    // seek produces a force toward the target
    Vec2 f = steering::seek(a, Vec2(10, 0));
    PRISM_CHECK(f.x > 0.0f);
    // flee produces the opposite
    Vec2 fl = steering::flee(a, Vec2(10, 0));
    PRISM_CHECK(fl.x < 0.0f);
    // the force never exceeds max_force
    PRISM_CHECK(f.length() <= a.max_force + 1e-4f);

    // integrate drives the agent toward the target and caps its speed.
    // 1.5 s only: seek never brakes, so a longer run overshoots and orbits.
    // The ramp is force-limited (max_force/max_speed = 4 s to full speed).
    for (int i = 0; i < 90; ++i) steering::integrate(a, steering::seek(a, Vec2(10, 0)), 1.0f / 60.0f);
    PRISM_CHECK(a.position.x > 3.0f);
    PRISM_CHECK(a.position.x < 10.0f);
    PRISM_CHECK(a.velocity.length() <= a.max_speed + 1e-3f);

    // arrive stops inside the arrival radius
    SteeringAgent b;
    b.position = Vec2(0, 0);
    b.max_speed = 6.0f;
    b.max_force = 40.0f;
    b.arrival_radius = 0.25f;
    b.slow_radius = 4.0f;
    for (int i = 0; i < 600; ++i) steering::integrate(b, steering::arrive(b, Vec2(8, 0)), 1.0f / 60.0f);
    PRISM_CHECK(b.position.distance(Vec2(8, 0)) < 0.5f);
    PRISM_CHECK(b.velocity.length() < 0.5f);
    // arrive at the target is stable, not NaN
    Vec2 zero = steering::arrive(b, b.position);
    PRISM_CHECK(std::isfinite(zero.x) && std::isfinite(zero.y));
}

PRISM_TEST(ai_steering_pursue_leads_the_target) {
    SteeringAgent a;
    a.position = Vec2(0, 0); a.max_speed = 5.0f; a.max_force = 25.0f;
    // target moving away along +y: pursuit must aim above the current position
    Vec2 direct = steering::seek(a, Vec2(10, 0));
    Vec2 lead = steering::pursue(a, Vec2(10, 0), Vec2(0, 4));
    PRISM_CHECK(lead.y > direct.y);
    Vec2 run = steering::evade(a, Vec2(10, 0), Vec2(0, 4));
    PRISM_CHECK(run.x < 0.0f);

    // wander is deterministic for a fixed seed
    SteeringAgent w1; w1.wander_seed = 42;
    SteeringAgent w2; w2.wander_seed = 42;
    Vec2 v1 = steering::wander(w1, 0.016f);
    Vec2 v2 = steering::wander(w2, 0.016f);
    PRISM_CHECK_NEAR(v1.x, v2.x, 1e-6);
    PRISM_CHECK_NEAR(v1.y, v2.y, 1e-6);
    PRISM_CHECK(w1.wander_seed == w2.wander_seed);
}

PRISM_TEST(ai_steering_flocking_separates_and_aligns) {
    std::vector<SteeringAgent> flock_v;
    for (int i = 0; i < 6; ++i) {
        SteeringAgent s;
        s.id = static_cast<u32>(i);
        s.position = Vec2(static_cast<f32>(i) * 0.2f, 0.0f);   // deliberately bunched up
        s.max_speed = 3.0f;
        s.max_force = 15.0f;
        flock_v.push_back(s);
    }
    // separation pushes the first agent away from its crowded neighbours
    Vec2 sep = steering::separation(flock_v[0], flock_v, 2.0f);
    PRISM_CHECK(sep.length() > 0.0f);
    PRISM_CHECK(sep.x < 0.0f);                       // pushed away from the +x cluster

    // alignment pulls toward the average heading
    for (auto& s : flock_v) s.velocity = Vec2(2.0f, 0.0f);
    flock_v[0].velocity = Vec2(-2.0f, 0.0f);
    Vec2 ali = steering::alignment(flock_v[0], flock_v, 5.0f);
    PRISM_CHECK(ali.x > 0.0f);

    // cohesion pulls toward the group centroid
    SteeringAgent lone;
    lone.id = 99; lone.position = Vec2(50, 0); lone.max_speed = 3.0f; lone.max_force = 15.0f;
    Vec2 coh = steering::cohesion(lone, flock_v, 1000.0f);
    PRISM_CHECK(coh.x < 0.0f);                       // centroid is to the left

    // no neighbours -> no force
    std::vector<SteeringAgent> empty;
    PRISM_CHECK_NEAR(steering::separation(lone, empty, 5.0f).length(), 0.0f, 1e-6);
    PRISM_CHECK_NEAR(steering::alignment(lone, empty, 5.0f).length(), 0.0f, 1e-6);
    PRISM_CHECK_NEAR(steering::cohesion(lone, empty, 5.0f).length(), 0.0f, 1e-6);

    // flocking a real group converges: average pairwise distance shrinks
    auto spread = [&](const std::vector<SteeringAgent>& v) {
        f64 total = 0; int n = 0;
        for (std::size_t i = 0; i < v.size(); ++i)
            for (std::size_t j = i + 1; j < v.size(); ++j) { total += v[i].position.distance(v[j].position); ++n; }
        return n ? total / n : 0.0;
    };
    std::vector<SteeringAgent> sim;
    for (int i = 0; i < 8; ++i) {
        SteeringAgent s;
        s.id = static_cast<u32>(i);
        s.position = Vec2(static_cast<f32>(i) * 3.0f, 0.0f);   // far apart
        s.max_speed = 3.0f; s.max_force = 15.0f;
        sim.push_back(s);
    }
    const f64 before = spread(sim);
    for (int step = 0; step < 400; ++step) {
        std::vector<Vec2> forces(sim.size());
        for (std::size_t i = 0; i < sim.size(); ++i)
            forces[i] = steering::flock(sim[i], sim, 1.5f, 1.0f, 1.0f, 4.0f);
        for (std::size_t i = 0; i < sim.size(); ++i) steering::integrate(sim[i], forces[i], 1.0f / 60.0f);
    }
    PRISM_CHECK(spread(sim) < before);
}

PRISM_TEST(ai_steering_obstacle_and_path_following) {
    SteeringAgent a;
    a.position = Vec2(0, 0);
    a.velocity = Vec2(3, 0);
    a.max_speed = 3.0f; a.max_force = 20.0f; a.radius = 0.3f;
    // obstacle dead ahead
    std::vector<std::pair<Vec2,f32>> obs = { {Vec2(4, 0), 1.0f} };
    Vec2 avoid = steering::avoid_obstacles(a, obs, 6.0f);
    PRISM_CHECK(avoid.length() > 0.0f);
    PRISM_CHECK(avoid.x < 0.0f || std::fabs(avoid.y) > 0.0f);   // it does not push straight through
    // no obstacle in range -> no force
    std::vector<std::pair<Vec2,f32>> far = { {Vec2(100, 100), 1.0f} };
    PRISM_CHECK_NEAR(steering::avoid_obstacles(a, far, 6.0f).length(), 0.0f, 1e-6);

    // wall avoidance pushes away from a wall the agent is about to hit
    a.radius = 1.5f;
    std::vector<std::pair<Vec2,Vec2>> walls = { {Vec2(3, -2), Vec2(3, 2)} };
    Vec2 wall = steering::wall_avoidance(a, walls, 3.5f);
    PRISM_CHECK(wall.x < 0.0f);                 // pushed back off the wall
    // no wall within the whisker length -> no force
    std::vector<std::pair<Vec2,Vec2>> none_walls = { {Vec2(100, -2), Vec2(100, 2)} };
    PRISM_CHECK_NEAR(steering::wall_avoidance(a, none_walls, 3.5f).length(), 0.0f, 1e-6);

    // path following advances the waypoint index
    std::vector<Vec2> path = { Vec2(2, 0), Vec2(5, 0), Vec2(8, 0) };
    std::size_t wp = 0;
    SteeringAgent p;
    p.position = Vec2(0, 0); p.max_speed = 4.0f; p.max_force = 20.0f;
    for (int i = 0; i < 200; ++i) {
        Vec2 f = steering::follow_path(p, path, wp, 0.3f);
        steering::integrate(p, f, 1.0f / 60.0f);
    }
    PRISM_CHECK(wp == path.size() - 1);
    PRISM_CHECK(p.position.x > 6.0f);
    // empty path is safe
    std::vector<Vec2> none;
    std::size_t z = 0;
    PRISM_CHECK_NEAR(steering::follow_path(p, none, z, 0.3f).length(), 0.0f, 1e-6);
}

PRISM_TEST(ai_formations_have_the_right_slot_count) {
    for (Formation f : {Formation::V, Formation::Line, Formation::Circle,
                        Formation::Column, Formation::Wedge}) {
        auto slots = formation_slots(f, 7, Vec2(0, 0), Vec2(0, 1), 1.5f);
        PRISM_CHECK_EQ(static_cast<int>(slots.size()), 7);
        for (const auto& s : slots) PRISM_CHECK(std::isfinite(s.x) && std::isfinite(s.y));
    }
    // the leader always takes the apex of a V
    auto v = formation_slots(Formation::V, 5, Vec2(3, 4), Vec2(0, 1), 2.0f);
    PRISM_CHECK_NEAR(v[0].x, 3.0f, 1e-5);
    PRISM_CHECK_NEAR(v[0].y, 4.0f, 1e-5);
    // circle slots are evenly spaced on a ring
    auto ring = formation_slots(Formation::Circle, 8, Vec2(0, 0), Vec2(0, 1), 2.0f);
    f32 r0 = ring[0].length();
    for (const auto& s : ring) PRISM_CHECK_NEAR(s.length(), r0, 1e-4);
    PRISM_CHECK(formation_slots(Formation::Line, 0, Vec2(0,0), Vec2(1,0), 1.0f).empty());
}

PRISM_TEST(ai_crowd_reaches_targets_without_overlapping) {
    Crowd crowd;
    const u32 a = crowd.add(Vec2(0, 0), 0.4f, 3.0f);
    const u32 b = crowd.add(Vec2(0.3f, 0), 0.4f, 3.0f);
    const u32 c = crowd.add(Vec2(20, 0), 0.4f, 3.0f);
    PRISM_CHECK_EQ(static_cast<int>(crowd.size()), 3);
    crowd.set_target(a, Vec2(10, 0));
    crowd.set_target(b, Vec2(10, 0.2f));
    crowd.set_target(c, Vec2(10, -0.2f));

    auto nbrs = crowd.neighbours(a);
    PRISM_CHECK_EQ(static_cast<int>(nbrs.size()), 1);      // b is within 3 units, c is not

    const u64 before = crowd.checksum();
    for (int i = 0; i < 600; ++i) crowd.update(1.0f / 60.0f);
    PRISM_CHECK(crowd.checksum() != before);

    const SteeringAgent* pa = crowd.agent(a);
    const SteeringAgent* pb = crowd.agent(b);
    PRISM_CHECK(pa && pb);
    PRISM_CHECK(pa->position.distance(Vec2(10, 0)) < 1.5f);
    // they must not have collapsed onto the same point
    PRISM_CHECK(pa->position.distance(pb->position) > 0.3f);

    // deterministic mode gives a stable neighbour ordering
    auto n1 = crowd.neighbours(a);
    auto n2 = crowd.neighbours(a);
    PRISM_CHECK(n1 == n2);

    // removal frees the slot for reuse
    crowd.remove(c);
    PRISM_CHECK(crowd.agent(c) == nullptr);
    const u32 reused = crowd.add(Vec2(5, 5), 0.4f, 3.0f);
    PRISM_CHECK_EQ(static_cast<int>(reused), static_cast<int>(c));

    // a flow field can drive the crowd instead of explicit targets
    GridNav g(32, 32);
    FlowField field;
    field.build(g, 30, 16);
    Crowd driven;
    const u32 d = driven.add(Vec2(1.0f, 1.0f), 0.3f, 4.0f);
    for (int i = 0; i < 400; ++i) driven.update(1.0f / 60.0f, &field, 1.0f);
    PRISM_CHECK(driven.agent(d)->position.x > 1.0f);       // the field pushed it forward
}
