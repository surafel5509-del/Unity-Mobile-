// =====================================================================
//  PRISM ENGINE — ai/ai.h
//  Integrated game AI. All of it runs offline on-device; the ONNX /
//  TensorFlow-Lite runtime (ai/ml_runtime.h) sits beside this and feeds
//  decisions into the same blackboard.
//
//  Contains:
//    * GridNav      — occupancy grid, 8-way A*, line-of-sight smoothing
//    * NavMesh      — convex polygon mesh, A* over the adjacency graph,
//                     funnel (string-pulling) path smoothing
//    * FlowField    — Dijkstra distance field + normalised direction field
//    * BehaviorTree — selector/sequence/parallel/decorators + blackboard
//    * StateMachine — timed states with guarded transitions
//    * GoapPlanner  — A* over world state, precondition/effect actions
//    * UtilityAi    — response-curve considerations, weighted selection
//    * Steering     — seek/flee/arrive/pursue/evade/wander/flocking/
//                     obstacle avoidance/path following/formations
//    * Crowd        — neighbour query + reciprocal velocity obstacle
// =====================================================================
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::ai {

using Vec2 = math::Vec2;
using Vec3 = math::Vec3;

// ============================================================ grid nav ======
/// Uniform occupancy grid. A* with an octile heuristic and 8-way movement;
/// corner cutting is blocked unless both adjacent cells are free.
class GridNav {
public:
    GridNav(i32 width = 0, i32 height = 0);
    void resize(i32 width, i32 height);
    void clear(bool blocked = false);
    void set_blocked(i32 x, i32 y, bool blocked = true);
    void fill_rect(i32 x0, i32 y0, i32 x1, i32 y1, bool blocked = true);
    [[nodiscard]] bool blocked(i32 x, i32 y) const;
    [[nodiscard]] bool inside(i32 x, i32 y) const;
    [[nodiscard]] i32 width() const { return width_; }
    [[nodiscard]] i32 height() const { return height_; }

    /// Bresenham line-of-sight: true if no blocked cell is crossed.
    [[nodiscard]] bool line_of_sight(i32 x0, i32 y0, i32 x1, i32 y1) const;

    struct Result {
        bool found = false;
        std::vector<std::pair<i32,i32>> path;
        u64 expanded = 0;
        f32 cost = 0;
    };
    [[nodiscard]] Result find_path(i32 sx, i32 sy, i32 gx, i32 gy, bool allow_diagonal = true) const;
    /// Removes waypoints when the agent can see past them.
    std::vector<std::pair<i32,i32>> smooth(const std::vector<std::pair<i32,i32>>& path) const;

    /// Dijkstra distance field from a goal cell (used by FlowField).
    [[nodiscard]] std::vector<f32> distance_field(i32 gx, i32 gy) const;
    static constexpr f32 kUnreachable = 1e30f;

private:
    i32 width_ = 0, height_ = 0;
    std::vector<u8> blocked_;
};

// ============================================================= navmesh ======
struct NavPoly {
    std::vector<Vec3> verts;      // convex, CCW
    std::vector<u32> neighbours;
    u32 area = 0;                 // cost modifier bucket
    [[nodiscard]] Vec3 centroid() const;
    [[nodiscard]] bool contains(Vec2 p) const;
};

/// Shared edge between two polygons, used as a funnel portal.
struct Portal { Vec3 left, right; };

class NavMesh {
public:
    u32 add_polygon(std::vector<Vec3> verts, u32 area = 0);
    /// Recomputes adjacency from shared edges. Call after building the mesh.
    void build_adjacency(f32 weld_epsilon = 0.001f);
    [[nodiscard]] const std::vector<NavPoly>& polygons() const { return polys_; }
    [[nodiscard]] std::size_t polygon_count() const { return polys_.size(); }
    [[nodiscard]] i32 polygon_at(Vec2 p) const;

    struct Result {
        bool found = false;
        std::vector<Vec3> path;
        std::vector<u32> polys;
        u64 expanded = 0;
    };
    /// A* across polygons, then string-pulled with the funnel algorithm.
    [[nodiscard]] Result find_path(Vec3 from, Vec3 to) const;

    /// Classic funnel / simple-staged string pulling. Portals are the shared
    /// edges between consecutive polygons, `start`/`end` cap the corridor.
    [[nodiscard]] static std::vector<Vec3> funnel(Vec3 start, Vec3 end,
                                                  const std::vector<Portal>& portals);
    /// Builds the portal list for a polygon corridor.
    [[nodiscard]] std::vector<Portal> corridor_portals(const std::vector<u32>& corridor) const;

    /// Raycast across the mesh; returns the polygon hit and the hit point.
    [[nodiscard]] bool raycast(Vec3 from, Vec3 to, u32& hit_poly, Vec3& hit_point) const;

private:
    std::vector<NavPoly> polys_;
};

// ============================================================ flow field ====
/// Integration-field pathfinding: one Dijkstra solve serves every agent that
/// shares a goal. This is what makes 500-unit crowds affordable on mobile.
class FlowField {
public:
    void build(const GridNav& grid, i32 goal_x, i32 goal_y);
    /// Normalised direction to descend the field at (x,y). Zero if unreachable.
    [[nodiscard]] Vec2 direction_at(i32 x, i32 y) const;
    [[nodiscard]] f32 cost_at(i32 x, i32 y) const;
    [[nodiscard]] bool reachable(i32 x, i32 y) const;
    [[nodiscard]] i32 goal_x() const { return goal_x_; }
    [[nodiscard]] i32 goal_y() const { return goal_y_; }
    [[nodiscard]] i32 width() const { return width_; }
    [[nodiscard]] i32 height() const { return height_; }
    [[nodiscard]] i32 reachable_cells() const { return reachable_; }

private:
    i32 width_ = 0, height_ = 0, goal_x_ = -1, goal_y_ = -1, reachable_ = 0;
    std::vector<f32> cost_;
    std::vector<Vec2> dir_;
};

// ========================================================= blackboard =======
/// Shared key/value store between AI subsystems and PrismScript.
class Blackboard {
public:
    void set(const std::string& k, f64 v) { numbers_[k] = v; }
    void set(const std::string& k, bool v) { numbers_[k] = v ? 1.0 : 0.0; }
    /// Without this overload a string literal binds to the bool overload
    /// (pointer->bool is a standard conversion and beats the user-defined
    /// conversion to std::string), silently storing 1.0 instead of the text.
    void set(const std::string& k, const char* v) { strings_[k] = v ? v : ""; }
    void set(const std::string& k, const std::string& v) { strings_[k] = v; }
    [[nodiscard]] f64 number(const std::string& k, f64 fallback = 0.0) const;
    [[nodiscard]] bool boolean(const std::string& k, bool fallback = false) const;
    [[nodiscard]] std::string text(const std::string& k, const char* fallback = "") const;
    [[nodiscard]] bool has(const std::string& k) const { return numbers_.count(k) || strings_.count(k); }
    void erase(const std::string& k);
    void clear() { numbers_.clear(); strings_.clear(); }
    [[nodiscard]] std::size_t size() const { return numbers_.size() + strings_.size(); }
private:
    std::unordered_map<std::string, f64> numbers_;
    std::unordered_map<std::string, std::string> strings_;
};

// ======================================================= behavior tree ======
enum class NodeStatus : u8 { Success, Failure, Running };
const char* node_status_name(NodeStatus s);

class BtNode;
using BtPtr = std::shared_ptr<BtNode>;

class BtNode {
public:
    virtual ~BtNode() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual NodeStatus tick(Blackboard& bb, f32 dt) = 0;
    virtual void reset() {}
    [[nodiscard]] u32 ticks() const { return ticks_; }
protected:
    u32 ticks_ = 0;
};

/// Leaf driven by a host callback — this is how PrismScript behaviour hooks in.
class BtAction : public BtNode {
public:
    using Fn = std::function<NodeStatus(Blackboard&, f32)>;
    BtAction(std::string n, Fn fn) : name_(std::move(n)), fn_(std::move(fn)) {}
    [[nodiscard]] std::string name() const override { return name_; }
    NodeStatus tick(Blackboard& bb, f32 dt) override { ++ticks_; return fn_ ? fn_(bb, dt) : NodeStatus::Success; }
private:
    std::string name_; Fn fn_;
};

class BtCondition : public BtNode {
public:
    BtCondition(std::string n, std::function<bool(Blackboard&)> fn)
        : name_(std::move(n)), fn_(std::move(fn)) {}
    [[nodiscard]] std::string name() const override { return name_; }
    NodeStatus tick(Blackboard& bb, f32) override {
        ++ticks_;
        return (fn_ && fn_(bb)) ? NodeStatus::Success : NodeStatus::Failure;
    }
private:
    std::string name_; std::function<bool(Blackboard&)> fn_;
};

class BtComposite : public BtNode {
public:
    explicit BtComposite(std::string n) : name_(std::move(n)) {}
    void add(BtPtr c) { children_.push_back(std::move(c)); }
    void reset() override { cursor_ = 0; for (auto& c : children_) c->reset(); }
    [[nodiscard]] const std::vector<BtPtr>& children() const { return children_; }
protected:
    std::string name_;
    std::vector<BtPtr> children_;
    std::size_t cursor_ = 0;
};

/// Runs children until one fails (or all succeed). Resumes at the running child.
class BtSequence : public BtComposite {
public:
    explicit BtSequence(std::string n = "sequence") : BtComposite(std::move(n)) {}
    [[nodiscard]] std::string name() const override { return name_; }
    NodeStatus tick(Blackboard& bb, f32 dt) override;
};

/// Runs children until one succeeds.
class BtSelector : public BtComposite {
public:
    explicit BtSelector(std::string n = "selector") : BtComposite(std::move(n)) {}
    [[nodiscard]] std::string name() const override { return name_; }
    NodeStatus tick(Blackboard& bb, f32 dt) override;
};

/// Ticks every child; succeeds if `require_all` and none failed.
class BtParallel : public BtComposite {
public:
    BtParallel(std::string n = "parallel", bool require_all = true)
        : BtComposite(std::move(n)), require_all_(require_all) {}
    [[nodiscard]] std::string name() const override { return name_; }
    NodeStatus tick(Blackboard& bb, f32 dt) override;
private:
    bool require_all_;
};

class BtInverter : public BtNode {
public:
    explicit BtInverter(BtPtr c) : child_(std::move(c)) {}
    [[nodiscard]] std::string name() const override { return "inverter"; }
    NodeStatus tick(Blackboard& bb, f32 dt) override {
        ++ticks_;
        NodeStatus s = child_->tick(bb, dt);
        if (s == NodeStatus::Success) return NodeStatus::Failure;
        if (s == NodeStatus::Failure) return NodeStatus::Success;
        return s;
    }
    void reset() override { child_->reset(); }
private:
    BtPtr child_;
};

class BtRepeater : public BtNode {
public:
    BtRepeater(BtPtr c, i32 times = -1) : child_(std::move(c)), times_(times) {}
    [[nodiscard]] std::string name() const override { return "repeater"; }
    NodeStatus tick(Blackboard& bb, f32 dt) override;
    void reset() override { count_ = 0; child_->reset(); }
private:
    BtPtr child_; i32 times_; i32 count_ = 0;
};

/// Succeeds without ticking the child until `seconds` have elapsed.
class BtCooldown : public BtNode {
public:
    BtCooldown(BtPtr c, f32 seconds) : child_(std::move(c)), cooldown_(seconds) {}
    [[nodiscard]] std::string name() const override { return "cooldown"; }
    NodeStatus tick(Blackboard& bb, f32 dt) override;
    void reset() override { timer_ = 0.0f; child_->reset(); }
private:
    BtPtr child_; f32 cooldown_; f32 timer_ = 1e9f;
};

/// Succeeds while the child runs, failing once `timeout` elapses.
class BtTimeout : public BtNode {
public:
    BtTimeout(BtPtr c, f32 timeout) : child_(std::move(c)), timeout_(timeout) {}
    [[nodiscard]] std::string name() const override { return "timeout"; }
    NodeStatus tick(Blackboard& bb, f32 dt) override;
    void reset() override { elapsed_ = 0.0f; child_->reset(); }
private:
    BtPtr child_; f32 timeout_; f32 elapsed_ = 0.0f;
};

// ======================================================= state machine ======
class StateMachine {
public:
    struct State {
        std::string name;
        std::function<void(f32 dt)> on_enter, on_update, on_exit;
        f32 min_duration = 0.0f;
    };
    struct Transition {
        std::string from, to;
        std::function<bool()> guard;
        f32 cooldown = 0.0f;
        f32 timer = 1e9f;
    };

    void add_state(State s);
    void add_transition(Transition t);
    void start(const std::string& state);
    void update(f32 dt);
    [[nodiscard]] const std::string& current() const { return current_; }
    [[nodiscard]] f32 time_in_state() const { return time_in_state_; }
    [[nodiscard]] u32 transition_count() const { return transitions_; }
    bool force(const std::string& state);

private:
    std::vector<State> states_;
    std::vector<Transition> trans_;
    std::string current_;
    f32 time_in_state_ = 0.0f;
    u32 transitions_ = 0;
};

// ============================================================== GOAP ========
inline constexpr std::size_t kGoapSlots = 16;
using WorldState = std::array<i32, kGoapSlots>;
using Fact = std::pair<u8, i32>;          // (slot, required value)

struct GoapAction {
    std::string name;
    f32 cost = 1.0f;
    std::vector<Fact> preconditions;
    std::vector<Fact> effects;
    /// Optional per-plan cost modifier (e.g. "attack is free if adjacent").
    std::function<f32(const WorldState&)> cost_fn;
};

class GoapPlanner {
public:
    void add_action(GoapAction a) { actions_.push_back(std::move(a)); }
    void clear() { actions_.clear(); }
    [[nodiscard]] const std::vector<GoapAction>& actions() const { return actions_; }

    struct Result {
        bool found = false;
        std::vector<std::string> actions;
        f32 cost = 0;
        u64 expanded = 0;
    };
    /// A* over world states. `goal` is a partial state: only listed slots matter.
    [[nodiscard]] Result plan(const WorldState& start, const std::vector<Fact>& goal,
                              u32 max_expansions = 4096) const;
private:
    std::vector<GoapAction> actions_;
};

// ========================================================== utility AI ======
enum class Curve : u8 { Linear, Square, InverseSquare, Sigmoid, Threshold, Log };

struct Consideration {
    std::string name;
    Curve curve = Curve::Linear;
    f32 slope = 1.0f, exponent = 1.0f, x_shift = 0.0f, y_shift = 0.0f;
    f32 weight = 1.0f;
    /// Maps blackboard input (0..1 typical) to a 0..1 score.
    [[nodiscard]] f32 score(f32 x) const;
};

class UtilityAi {
public:
    struct Option {
        std::string name;
        std::vector<Consideration> considerations;
        /// "max" takes the best single consideration, "sum" adds them,
        /// "average" divides by count, "min" demands all of them.
        enum class Combine : u8 { Max, Sum, Average, Min } combine = Combine::Average;
    };

    void add_option(Option o) { options_.push_back(std::move(o)); }
    void clear() { options_.clear(); }
    [[nodiscard]] const std::vector<Option>& options() const { return options_; }

    /// Scores every option. Inputs come from the blackboard (0..1 expected).
    [[nodiscard]] std::vector<std::pair<std::string, f32>> score_all(const Blackboard& bb) const;
    /// Highest-scoring option; empty string if nothing scored above `threshold`.
    [[nodiscard]] std::string choose(const Blackboard& bb, f32 threshold = 0.0f) const;
private:
    std::vector<Option> options_;
};

// ============================================================ steering ======
struct SteeringAgent {
    Vec2 position, velocity, acceleration;
    f32 max_speed = 4.0f;
    f32 max_force = 12.0f;
    f32 mass = 1.0f;
    f32 radius = 0.4f;
    f32 arrival_radius = 1.5f;
    f32 slow_radius = 4.0f;
    f32 wander_angle = 0.0f;
    u32 wander_seed = 0x9E3779B9u;   // deterministic wander jitter
    u32 id = 0;
};

struct SteeringOutput { Vec2 linear; f32 angular = 0.0f; };

namespace steering {
[[nodiscard]] Vec2 seek(const SteeringAgent& a, Vec2 target);
[[nodiscard]] Vec2 flee(const SteeringAgent& a, Vec2 target);
/// Decelerates smoothly inside `slow_radius`, stops inside `arrival_radius`.
[[nodiscard]] Vec2 arrive(const SteeringAgent& a, Vec2 target);
/// Predicts the target's future position from its velocity.
[[nodiscard]] Vec2 pursue(const SteeringAgent& a, Vec2 target_pos, Vec2 target_vel);
[[nodiscard]] Vec2 evade(const SteeringAgent& a, Vec2 target_pos, Vec2 target_vel);
/// Perlin-free wander: a jittering point projected on a circle ahead.
[[nodiscard]] Vec2 wander(SteeringAgent& a, f32 dt, f32 jitter = 1.2f);
[[nodiscard]] Vec2 separation(const SteeringAgent& a, const std::vector<SteeringAgent>& others, f32 radius);
[[nodiscard]] Vec2 alignment(const SteeringAgent& a, const std::vector<SteeringAgent>& others, f32 radius);
[[nodiscard]] Vec2 cohesion(const SteeringAgent& a, const std::vector<SteeringAgent>& others, f32 radius);
[[nodiscard]] Vec2 flock(const SteeringAgent& a, const std::vector<SteeringAgent>& others,
                         f32 sep_w, f32 ali_w, f32 coh_w, f32 radius);
/// Whisker avoidance: two feelers plus a centre ray against circles.
[[nodiscard]] Vec2 avoid_obstacles(const SteeringAgent& a, const std::vector<std::pair<Vec2,f32>>& obstacles,
                                   f32 look_ahead);
[[nodiscard]] Vec2 follow_path(const SteeringAgent& a, const std::vector<Vec2>& path,
                               std::size_t& waypoint, f32 acceptance);
[[nodiscard]] Vec2 wall_avoidance(const SteeringAgent& a, const std::vector<std::pair<Vec2,Vec2>>& walls,
                                  f32 look_ahead);
/// Integrates acceleration -> velocity -> position with the agent's limits.
void integrate(SteeringAgent& a, Vec2 force, f32 dt);
} // namespace steering

enum class Formation : u8 { V, Line, Circle, Column, Wedge };
/// Slot positions (world space) for `count` units around a leader.
[[nodiscard]] std::vector<Vec2> formation_slots(Formation f, std::size_t count, Vec2 leader,
                                                Vec2 facing, f32 spacing);

// =============================================================== crowd ======
/// Local avoidance using a simplified reciprocal velocity obstacle. Cheap
/// enough for ~300 agents on a mid-range phone; the deterministic mode
/// (fixed iteration count + stable ordering) keeps LAN replays in sync.
class Crowd {
public:
    struct Config {
        f32 neighbour_radius = 3.0f;
        f32 time_horizon = 1.2f;
        i32 max_neighbours = 8;
        bool deterministic = true;
    };
    Crowd() = default;
    explicit Crowd(const Config& c) : cfg_(c) {}

    u32 add(Vec2 pos, f32 radius = 0.4f, f32 max_speed = 4.0f);
    void remove(u32 id);
    void set_target(u32 id, Vec2 target);
    [[nodiscard]] const SteeringAgent* agent(u32 id) const;
    SteeringAgent* agent(u32 id);
    [[nodiscard]] std::size_t size() const { return agents_.size(); }
    [[nodiscard]] std::vector<u32> neighbours(u32 id) const;

    /// One simulation step: desired velocity from the flow field or target,
    /// then reciprocal avoidance, then integration.
    void update(f32 dt, const FlowField* field = nullptr, f32 cell_size = 1.0f);

    [[nodiscard]] const Config& config() const { return cfg_; }
    /// Deterministic checksum of every position — used by rollback netcode.
    [[nodiscard]] u64 checksum() const;

private:
    Config cfg_;
    std::vector<SteeringAgent> agents_;
    std::vector<Vec2> targets_;
    std::vector<bool> alive_;
};

} // namespace prism::ai
