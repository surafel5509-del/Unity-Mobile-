// PRISM ENGINE — ai/ai.cpp
#include "prism/ai/ai.h"
#include "prism/core/hash.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <queue>

namespace prism::ai {

// ============================================================== grid nav ====
GridNav::GridNav(i32 width, i32 height) { resize(width, height); }

void GridNav::resize(i32 width, i32 height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    blocked_.assign(static_cast<std::size_t>(width_) * height_, 0);
}
void GridNav::clear(bool blocked) { std::fill(blocked_.begin(), blocked_.end(), blocked ? 1 : 0); }
bool GridNav::inside(i32 x, i32 y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
bool GridNav::blocked(i32 x, i32 y) const {
    if (!inside(x, y)) return true;                     // out of bounds counts as blocked
    return blocked_[static_cast<std::size_t>(y) * width_ + x] != 0;
}
void GridNav::set_blocked(i32 x, i32 y, bool b) {
    if (inside(x, y)) blocked_[static_cast<std::size_t>(y) * width_ + x] = b ? 1 : 0;
}
void GridNav::fill_rect(i32 x0, i32 y0, i32 x1, i32 y1, bool b) {
    for (i32 y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
        for (i32 x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
            set_blocked(x, y, b);
}

bool GridNav::line_of_sight(i32 x0, i32 y0, i32 x1, i32 y1) const {
    // Supercover Bresenham: visits every cell the segment touches.
    i32 dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    i32 sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy;
    i32 x = x0, y = y0;
    for (i32 guard = 0; guard < width_ * height_ + 4; ++guard) {
        if (blocked(x, y)) return false;
        if (x == x1 && y == y1) return true;
        const i32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
    return false;
}

GridNav::Result GridNav::find_path(i32 sx, i32 sy, i32 gx, i32 gy, bool allow_diagonal) const {
    Result out;
    if (!inside(sx, sy) || !inside(gx, gy)) return out;
    if (blocked(sx, sy) || blocked(gx, gy)) return out;
    if (sx == gx && sy == gy) { out.found = true; out.path.push_back({sx, sy}); return out; }

    const std::size_t n = static_cast<std::size_t>(width_) * height_;
    std::vector<f32> g(n, std::numeric_limits<f32>::infinity());
    std::vector<i32> came(n, -1);
    std::vector<u8> closed(n, 0);

    auto idx = [&](i32 x, i32 y) { return static_cast<std::size_t>(y) * width_ + x; };
    auto h = [&](i32 x, i32 y) {
        // Octile distance: admissible for 8-way movement with diag cost sqrt(2).
        const i32 dx = std::abs(x - gx), dy = std::abs(y - gy);
        return static_cast<f32>(std::max(dx, dy)) + (1.41421356f - 1.0f) * static_cast<f32>(std::min(dx, dy));
    };

    using Node = std::pair<f32, i32>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    g[idx(sx, sy)] = 0.0f;
    open.push({h(sx, sy), static_cast<i32>(idx(sx, sy))});

    static const i32 dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const i32 dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!open.empty()) {
        const i32 cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        ++out.expanded;
        const i32 cx = static_cast<i32>(cur % width_), cy = static_cast<i32>(cur / width_);
        if (cx == gx && cy == gy) {
            out.found = true;
            out.cost = g[cur];
            i32 node = cur;
            while (node != -1) {
                out.path.push_back({static_cast<i32>(node % width_), static_cast<i32>(node / width_)});
                node = came[node];
            }
            std::reverse(out.path.begin(), out.path.end());
            return out;
        }
        const i32 dirs = allow_diagonal ? 8 : 4;
        for (i32 d = 0; d < dirs; ++d) {
            const i32 nx = cx + dx8[d], ny = cy + dy8[d];
            if (!inside(nx, ny) || blocked(nx, ny)) continue;
            if (d >= 4) {
                // no corner cutting through two blocked orthogonal cells
                if (blocked(cx + dx8[d], cy) && blocked(cx, cy + dy8[d])) continue;
            }
            const std::size_t ni = idx(nx, ny);
            if (closed[ni]) continue;
            const f32 step = (d >= 4) ? 1.41421356f : 1.0f;
            const f32 ng = g[cur] + step;
            if (ng < g[ni]) {
                g[ni] = ng;
                came[ni] = cur;
                open.push({ng + h(nx, ny), static_cast<i32>(ni)});
            }
        }
    }
    return out;
}

std::vector<std::pair<i32,i32>> GridNav::smooth(const std::vector<std::pair<i32,i32>>& path) const {
    std::vector<std::pair<i32,i32>> out;
    if (path.size() < 3) return path;
    std::size_t anchor = 0;
    out.push_back(path[0]);
    while (anchor + 1 < path.size()) {
        std::size_t farthest = anchor + 1;
        for (std::size_t i = path.size() - 1; i > anchor + 1; --i) {
            if (line_of_sight(path[anchor].first, path[anchor].second, path[i].first, path[i].second)) {
                farthest = i;
                break;
            }
        }
        out.push_back(path[farthest]);
        anchor = farthest;
    }
    return out;
}

std::vector<f32> GridNav::distance_field(i32 gx, i32 gy) const {
    const std::size_t n = static_cast<std::size_t>(width_) * height_;
    std::vector<f32> dist(n, kUnreachable);
    if (!inside(gx, gy)) return dist;
    std::vector<i32> frontier;
    dist[static_cast<std::size_t>(gy) * width_ + gx] = 0.0f;
    frontier.push_back(static_cast<i32>(static_cast<std::size_t>(gy) * width_ + gx));
    static const i32 dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const i32 dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    std::size_t head = 0;
    // Uniform-cost expansion (all edge weights are 1 or sqrt(2)); good enough
    // for a flow field, and O(n) instead of a full priority queue.
    while (head < frontier.size()) {
        const i32 cur = frontier[head++];
        const i32 cx = static_cast<i32>(cur % width_), cy = static_cast<i32>(cur / width_);
        for (i32 d = 0; d < 8; ++d) {
            const i32 nx = cx + dx8[d], ny = cy + dy8[d];
            if (!inside(nx, ny) || blocked(nx, ny)) continue;
            if (d >= 4 && blocked(cx + dx8[d], cy) && blocked(cx, cy + dy8[d])) continue;
            const std::size_t ni = static_cast<std::size_t>(ny) * width_ + nx;
            const f32 nd = dist[cur] + (d >= 4 ? 1.41421356f : 1.0f);
            if (nd < dist[ni]) { dist[ni] = nd; frontier.push_back(static_cast<i32>(ni)); }
        }
    }
    return dist;
}

// ============================================================== navmesh =====
Vec3 NavPoly::centroid() const {
    Vec3 c;
    for (const auto& v : verts) c = c + v;
    const f32 n = verts.empty() ? 1.0f : static_cast<f32>(verts.size());
    return c / n;
}

bool NavPoly::contains(Vec2 p) const {
    if (verts.size() < 3) return false;
    bool sign_set = false;
    bool positive = false;
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const Vec3& a = verts[i];
        const Vec3& b = verts[(i + 1) % verts.size()];
        const f32 cross = (b.x - a.x) * (p.y - a.z) - (b.z - a.z) * (p.x - a.x);
        if (std::fabs(cross) < kEpsilon) continue;
        if (!sign_set) { positive = cross > 0; sign_set = true; }
        else if ((cross > 0) != positive) return false;
    }
    return sign_set;
}

u32 NavMesh::add_polygon(std::vector<Vec3> verts, u32 area) {
    if (verts.size() < 3) return kInvalidId;
    NavPoly p;
    p.verts = std::move(verts);
    p.area = area;
    polys_.push_back(std::move(p));
    return static_cast<u32>(polys_.size() - 1);
}

namespace {
bool same_point(Vec3 a, Vec3 b, f32 eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
f32 cross_xz(Vec3 a, Vec3 b) { return a.x * b.z - a.z * b.x; }
} // namespace

void NavMesh::build_adjacency(f32 weld_epsilon) {
    for (auto& p : polys_) p.neighbours.clear();
    for (std::size_t i = 0; i < polys_.size(); ++i) {
        for (std::size_t j = i + 1; j < polys_.size(); ++j) {
            int shared = 0;
            for (const auto& a : polys_[i].verts)
                for (const auto& b : polys_[j].verts)
                    if (same_point(a, b, weld_epsilon)) ++shared;
            if (shared >= 2) {
                polys_[i].neighbours.push_back(static_cast<u32>(j));
                polys_[j].neighbours.push_back(static_cast<u32>(i));
            }
        }
    }
}

i32 NavMesh::polygon_at(Vec2 p) const {
    for (std::size_t i = 0; i < polys_.size(); ++i)
        if (polys_[i].contains(p)) return static_cast<i32>(i);
    return -1;
}

std::vector<Portal> NavMesh::corridor_portals(const std::vector<u32>& corridor) const {
    std::vector<Portal> out;
    for (std::size_t i = 0; i + 1 < corridor.size(); ++i) {
        const NavPoly& A = polys_[corridor[i]];
        const NavPoly& B = polys_[corridor[i + 1]];
        std::vector<Vec3> shared;
        for (const auto& a : A.verts)
            for (const auto& b : B.verts)
                if (same_point(a, b, 0.001f)) shared.push_back(a);
        if (shared.size() < 2) continue;
        const Vec3 dir = B.centroid() - A.centroid();
        const Vec3 base = A.centroid();
        const f32 c = cross_xz(dir, shared[0] - base);
        if (c >= 0.0f) out.push_back(Portal{shared[0], shared[1]});
        else           out.push_back(Portal{shared[1], shared[0]});
    }
    return out;
}

std::vector<Vec3> NavMesh::funnel(Vec3 start, Vec3 end, const std::vector<Portal>& portals) {
    std::vector<Vec3> pts;
    pts.push_back(start);
    if (portals.empty()) { pts.push_back(end); return pts; }

    auto triarea2 = [](Vec3 a, Vec3 b, Vec3 c) {
        const f32 ax = b.x - a.x, az = b.z - a.z;
        const f32 bx = c.x - a.x, bz = c.z - a.z;
        return bx * az - ax * bz;
    };
    auto vequal = [](Vec3 a, Vec3 b) {
        return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z) < 1e-6f;
    };

    Vec3 apex = start;
    Vec3 left = portals[0].left, right = portals[0].right;
    std::size_t apex_i = 0, left_i = 0, right_i = 0;
    std::size_t i = 1;

    while (i < portals.size()) {
        const Vec3 pleft = portals[i].left;
        const Vec3 pright = portals[i].right;

        if (triarea2(apex, right, pright) <= 0.0f) {
            if (vequal(apex, right) || triarea2(apex, left, pright) > 0.0f) {
                right = pright; right_i = i;
            } else {
                pts.push_back(left);
                apex = left; apex_i = left_i;
                left = apex; right = apex; left_i = apex_i; right_i = apex_i;
                i = apex_i + 1;
                continue;
            }
        }
        if (triarea2(apex, left, pleft) >= 0.0f) {
            if (vequal(apex, left) || triarea2(apex, right, pleft) < 0.0f) {
                left = pleft; left_i = i;
            } else {
                pts.push_back(right);
                apex = right; apex_i = right_i;
                left = apex; right = apex; left_i = apex_i; right_i = apex_i;
                i = apex_i + 1;
                continue;
            }
        }
        ++i;
    }
    if (pts.empty() || !vequal(pts.back(), end)) pts.push_back(end);
    return pts;
}

NavMesh::Result NavMesh::find_path(Vec3 from, Vec3 to) const {
    Result out;
    if (polys_.empty()) return out;
    const i32 start = polygon_at(Vec2(from.x, from.z));
    const i32 goal = polygon_at(Vec2(to.x, to.z));
    if (start < 0 || goal < 0) return out;
    if (start == goal) { out.found = true; out.path = {from, to}; out.polys = {static_cast<u32>(start)}; return out; }

    const std::size_t n = polys_.size();
    std::vector<f32> g(n, std::numeric_limits<f32>::infinity());
    std::vector<i32> came(n, -1);
    std::vector<u8> closed(n, 0);
    const Vec3 goal_c = polys_[goal].centroid();
    auto h = [&](u32 i) { return polys_[i].centroid().distance(goal_c); };

    using Node = std::pair<f32, u32>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    g[start] = 0.0f;
    open.push({h(static_cast<u32>(start)), static_cast<u32>(start)});

    while (!open.empty()) {
        const u32 cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        ++out.expanded;
        if (cur == static_cast<u32>(goal)) {
            out.found = true;
            std::vector<u32> corridor;
            i32 node = static_cast<i32>(cur);
            while (node != -1) { corridor.push_back(static_cast<u32>(node)); node = came[node]; }
            std::reverse(corridor.begin(), corridor.end());
            out.polys = corridor;
            out.path = funnel(from, to, corridor_portals(corridor));
            return out;
        }
        for (u32 nb : polys_[cur].neighbours) {
            if (closed[nb]) continue;
            const f32 step = polys_[cur].centroid().distance(polys_[nb].centroid());
            const f32 ng = g[cur] + step;
            if (ng < g[nb]) { g[nb] = ng; came[nb] = static_cast<i32>(cur); open.push({ng + h(nb), nb}); }
        }
    }
    return out;
}

namespace {
/// Ray/segment intersection in the XZ plane. Returns t in (0,1] or -1.
f32 ray_segment_xz(Vec3 from, Vec3 to, Vec3 a, Vec3 b) {
    const Vec3 d = to - from;
    const Vec3 e = b - a;
    const f32 det = e.x * d.z - d.x * e.z;
    if (std::fabs(det) < kEpsilon) return -1.0f;
    const f32 t = (-(a.x - from.x) * e.z + e.x * (a.z - from.z)) / det;
    const f32 u = (d.x * (a.z - from.z) - d.z * (a.x - from.x)) / det;
    if (t <= 1e-5f || t > 1.0f + 1e-5f) return -1.0f;
    if (u < -1e-5f || u > 1.0f + 1e-5f) return -1.0f;
    return math::clampf(t, 0.0f, 1.0f);
}
} // namespace

bool NavMesh::raycast(Vec3 from, Vec3 to, u32& hit_poly, Vec3& hit_point) const {
    const i32 start = polygon_at(Vec2(from.x, from.z));
    if (start < 0) { hit_poly = kInvalidId; return false; }
    i32 cur = start;
    i32 prev = -1;                     // never re-cross the portal we entered by
    for (int iter = 0; iter < 512; ++iter) {
        const NavPoly& P = polys_[static_cast<std::size_t>(cur)];
        f32 best_t = std::numeric_limits<f32>::max();
        int best_edge = -1;
        for (std::size_t i = 0; i < P.verts.size(); ++i) {
            const Vec3& a = P.verts[i];
            const Vec3& b = P.verts[(i + 1) % P.verts.size()];
            if (prev >= 0) {
                const NavPoly& Q = polys_[static_cast<std::size_t>(prev)];
                bool ha = false, hb = false;
                for (const auto& v : Q.verts) {
                    if (same_point(v, a, 0.001f)) ha = true;
                    if (same_point(v, b, 0.001f)) hb = true;
                }
                if (ha && hb) continue;          // entry edge: skip it
            }
            const f32 t = ray_segment_xz(from, to, a, b);
            if (t > 0.0f && t < best_t) { best_t = t; best_edge = static_cast<int>(i); }
        }
        if (best_edge < 0) {                       // stays inside this polygon
            hit_poly = static_cast<u32>(cur);
            hit_point = to;
            return false;
        }
        const Vec3& a = P.verts[static_cast<std::size_t>(best_edge)];
        const Vec3& b = P.verts[(static_cast<std::size_t>(best_edge) + 1) % P.verts.size()];
        // find the neighbour across this edge
        u32 next = kInvalidId;
        for (u32 nb : P.neighbours) {
            const NavPoly& Q = polys_[nb];
            bool ha = false, hb = false;
            for (const auto& v : Q.verts) {
                if (same_point(v, a, 0.001f)) ha = true;
                if (same_point(v, b, 0.001f)) hb = true;
            }
            if (ha && hb) { next = nb; break; }
        }
        if (next == kInvalidId) {                  // exited the mesh
            hit_poly = static_cast<u32>(cur);
            hit_point = from + (to - from) * best_t;
            return true;
        }
        prev = cur;
        cur = static_cast<i32>(next);
    }
    hit_poly = kInvalidId;
    return false;
}

// ============================================================ flow field ====
void FlowField::build(const GridNav& grid, i32 goal_x, i32 goal_y) {
    width_ = grid.width();
    height_ = grid.height();
    goal_x_ = goal_x;
    goal_y_ = goal_y;
    cost_ = grid.distance_field(goal_x, goal_y);
    dir_.assign(static_cast<std::size_t>(std::max(0, width_)) * std::max(0, height_), Vec2(0, 0));
    reachable_ = 0;
    for (f32 c : cost_) if (c < GridNav::kUnreachable * 0.5f) ++reachable_;

    // Direction = steepest descent over the 8 neighbours.
    for (i32 y = 0; y < height_; ++y) {
        for (i32 x = 0; x < width_; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width_ + x;
            const f32 base = cost_[i];
            if (base >= GridNav::kUnreachable * 0.5f) continue;
            Vec2 best(0, 0);
            f32 best_cost = base;
            for (i32 dy = -1; dy <= 1; ++dy) {
                for (i32 dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const i32 nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                    const f32 c = cost_[static_cast<std::size_t>(ny) * width_ + nx];
                    if (c < best_cost) { best_cost = c; best = Vec2(static_cast<f32>(dx), static_cast<f32>(dy)); }
                }
            }
            dir_[i] = best.normalized();
        }
    }
}

f32 FlowField::cost_at(i32 x, i32 y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return GridNav::kUnreachable;
    return cost_[static_cast<std::size_t>(y) * width_ + x];
}
bool FlowField::reachable(i32 x, i32 y) const { return cost_at(x, y) < GridNav::kUnreachable * 0.5f; }
Vec2 FlowField::direction_at(i32 x, i32 y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return Vec2(0, 0);
    return dir_[static_cast<std::size_t>(y) * width_ + x];
}

// ========================================================== blackboard ======
f64 Blackboard::number(const std::string& k, f64 fallback) const {
    auto it = numbers_.find(k);
    return it == numbers_.end() ? fallback : it->second;
}
bool Blackboard::boolean(const std::string& k, bool fallback) const {
    auto it = numbers_.find(k);
    return it == numbers_.end() ? fallback : it->second != 0.0;
}
std::string Blackboard::text(const std::string& k, const char* fallback) const {
    auto it = strings_.find(k);
    return it == strings_.end() ? std::string(fallback) : it->second;
}
void Blackboard::erase(const std::string& k) { numbers_.erase(k); strings_.erase(k); }

// ======================================================= behavior tree ======
const char* node_status_name(NodeStatus s) {
    switch (s) {
        case NodeStatus::Success: return "success";
        case NodeStatus::Failure: return "failure";
        case NodeStatus::Running: return "running";
    }
    return "failure";
}

NodeStatus BtSequence::tick(Blackboard& bb, f32 dt) {
    ++ticks_;
    while (cursor_ < children_.size()) {
        const NodeStatus s = children_[cursor_]->tick(bb, dt);
        if (s == NodeStatus::Running) return NodeStatus::Running;
        if (s == NodeStatus::Failure) { cursor_ = 0; return NodeStatus::Failure; }
        ++cursor_;
    }
    cursor_ = 0;
    return NodeStatus::Success;
}

NodeStatus BtSelector::tick(Blackboard& bb, f32 dt) {
    ++ticks_;
    while (cursor_ < children_.size()) {
        const NodeStatus s = children_[cursor_]->tick(bb, dt);
        if (s == NodeStatus::Running) return NodeStatus::Running;
        if (s == NodeStatus::Success) { cursor_ = 0; return NodeStatus::Success; }
        ++cursor_;
    }
    cursor_ = 0;
    return NodeStatus::Failure;
}

NodeStatus BtParallel::tick(Blackboard& bb, f32 dt) {
    ++ticks_;
    i32 success = 0, failure = 0, running = 0;
    for (auto& c : children_) {
        const NodeStatus s = c->tick(bb, dt);
        if (s == NodeStatus::Success) ++success;
        else if (s == NodeStatus::Failure) ++failure;
        else ++running;
    }
    if (require_all_) {
        if (failure > 0) return NodeStatus::Failure;
        if (running > 0) return NodeStatus::Running;
        return NodeStatus::Success;
    }
    if (success > 0) return NodeStatus::Success;
    if (running > 0) return NodeStatus::Running;
    return NodeStatus::Failure;
}

NodeStatus BtRepeater::tick(Blackboard& bb, f32 dt) {
    ++ticks_;
    const NodeStatus s = child_->tick(bb, dt);
    if (s == NodeStatus::Running) return NodeStatus::Running;
    ++count_;
    if (times_ >= 0 && count_ >= times_) { count_ = 0; return s; }
    return NodeStatus::Running;
}

NodeStatus BtCooldown::tick(Blackboard& bb, f32 dt) {
    ++ticks_;
    timer_ += dt;
    if (timer_ < cooldown_) return NodeStatus::Failure;
    const NodeStatus s = child_->tick(bb, dt);
    if (s != NodeStatus::Running) timer_ = 0.0f;
    return s;
}

NodeStatus BtTimeout::tick(Blackboard& bb, f32 dt) {
    ++ticks_;
    elapsed_ += dt;
    if (elapsed_ >= timeout_) { child_->reset(); elapsed_ = 0.0f; return NodeStatus::Failure; }
    const NodeStatus s = child_->tick(bb, dt);
    if (s != NodeStatus::Running) elapsed_ = 0.0f;
    return s;
}

// ======================================================= state machine ======
void StateMachine::add_state(State s) { states_.push_back(std::move(s)); }
void StateMachine::add_transition(Transition t) { trans_.push_back(std::move(t)); }

void StateMachine::start(const std::string& state) {
    for (auto& s : states_) {
        if (s.name == state) { current_ = state; time_in_state_ = 0.0f; if (s.on_enter) s.on_enter(0.0f); return; }
    }
}

bool StateMachine::force(const std::string& state) {
    for (auto& s : states_) {
        if (s.name != state) continue;
        for (auto& c : states_) if (c.name == current_ && c.on_exit) c.on_exit(0.0f);
        current_ = state;
        time_in_state_ = 0.0f;
        ++transitions_;
        if (s.on_enter) s.on_enter(0.0f);
        return true;
    }
    return false;
}

void StateMachine::update(f32 dt) {
    time_in_state_ += dt;
    State* cur = nullptr;
    for (auto& s : states_) if (s.name == current_) { cur = &s; break; }
    if (!cur) return;
    if (time_in_state_ >= cur->min_duration) {
        for (auto& t : trans_) {
            if (t.from != current_) continue;
            t.timer += dt;
            if (t.timer < t.cooldown) continue;
            if (t.guard && !t.guard()) continue;
            if (force(t.to)) {
                t.timer = 0.0f;
                // Tick the state we just entered on this same frame, otherwise
                // every transition costs the agent a full frame of AI.
                cur = nullptr;
                for (auto& s : states_) if (s.name == current_) { cur = &s; break; }
                break;
            }
        }
    }
    if (cur && cur->on_update) cur->on_update(dt);
}

// ============================================================== GOAP ========
namespace {
struct WorldHash {
    std::size_t operator()(const WorldState& s) const {
        u64 h = 1469598103934665603ull;
        for (i32 v : s) { h ^= static_cast<u64>(static_cast<u32>(v)); h *= 1099511628211ull; }
        return static_cast<std::size_t>(h);
    }
};
bool applies(const WorldState& s, const std::vector<Fact>& pre) {
    for (const auto& f : pre) if (s[f.first] != f.second) return false;
    return true;
}
void apply(WorldState& s, const std::vector<Fact>& post) {
    for (const auto& f : post) s[f.first] = f.second;
}
bool satisfies(const WorldState& s, const std::vector<Fact>& goal) { return applies(s, goal); }
} // namespace

GoapPlanner::Result GoapPlanner::plan(const WorldState& start, const std::vector<Fact>& goal,
                                      u32 max_expansions) const {
    Result out;
    if (goal.empty()) { out.found = true; return out; }
    if (satisfies(start, goal)) { out.found = true; return out; }

    f32 min_cost = std::numeric_limits<f32>::max();
    for (const auto& a : actions_) min_cost = std::min(min_cost, std::max(a.cost, 0.0f));
    if (min_cost == std::numeric_limits<f32>::max()) min_cost = 1.0f;

    auto heuristic = [&](const WorldState& s) {
        u32 unmet = 0;
        for (const auto& f : goal) if (s[f.first] != f.second) ++unmet;
        return static_cast<f32>(unmet) * min_cost;
    };

    struct Node { WorldState state; f32 g; std::vector<u32> actions; };
    using QItem = std::pair<f32, std::size_t>;
    std::vector<Node> pool;
    std::unordered_map<WorldState, f32, WorldHash> best;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> open;

    pool.push_back(Node{start, 0.0f, {}});
    best[start] = 0.0f;
    open.push({heuristic(start), 0});

    while (!open.empty()) {
        const std::size_t idx = open.top().second;
        open.pop();
        if (out.expanded >= max_expansions) break;
        ++out.expanded;
        const Node cur = pool[idx];       // copy: pool may reallocate
        if (satisfies(cur.state, goal)) {
            out.found = true;
            out.cost = cur.g;
            for (u32 ai : cur.actions) out.actions.push_back(actions_[ai].name);
            return out;
        }
        const f32 known = best.count(cur.state) ? best[cur.state] : std::numeric_limits<f32>::max();
        if (cur.g > known + kEpsilon) continue;

        for (std::size_t ai = 0; ai < actions_.size(); ++ai) {
            const GoapAction& a = actions_[ai];
            if (!applies(cur.state, a.preconditions)) continue;
            WorldState next = cur.state;
            apply(next, a.effects);
            const f32 cost = a.cost_fn ? a.cost_fn(cur.state) : a.cost;
            const f32 ng = cur.g + std::max(0.0f, cost);
            auto it = best.find(next);
            if (it != best.end() && it->second <= ng + kEpsilon) continue;
            best[next] = ng;
            Node n = cur;
            n.state = next;
            n.g = ng;
            n.actions.push_back(static_cast<u32>(ai));
            pool.push_back(std::move(n));
            open.push({ng + heuristic(next), pool.size() - 1});
        }
    }
    return out;
}

// ========================================================== utility AI ======
f32 Consideration::score(f32 x) const {
    const f32 xi = slope * (x - x_shift);
    f32 y = 0.0f;
    switch (curve) {
        case Curve::Linear:         y = xi + y_shift; break;
        case Curve::Square:         y = std::pow(math::clampf(xi, 0.0f, 1.0f), exponent) + y_shift; break;
        case Curve::InverseSquare:  y = 1.0f - std::pow(math::clampf(xi, 0.0f, 1.0f), exponent) + y_shift; break;
        case Curve::Sigmoid:        y = 1.0f / (1.0f + std::exp(-10.0f * (xi - 0.5f))) + y_shift; break;
        case Curve::Threshold:      y = (xi >= 0.5f) ? 1.0f + y_shift : y_shift; break;
        case Curve::Log: {
            const f32 d = std::max(xi, 0.0f);
            y = std::log1p(9.0f * d) / std::log(10.0f) + y_shift;
            break;
        }
    }
    return math::clampf(y, 0.0f, 1.0f);
}

std::vector<std::pair<std::string, f32>> UtilityAi::score_all(const Blackboard& bb) const {
    std::vector<std::pair<std::string, f32>> out;
    for (const auto& o : options_) {
        if (o.considerations.empty()) { out.emplace_back(o.name, 0.0f); continue; }
        f32 total_w = 0.0f, acc = 0.0f, hi = 0.0f, lo = 1.0f;
        for (const auto& c : o.considerations) {
            const f32 s = c.score(static_cast<f32>(bb.number(c.name, 0.0)));
            acc += s * c.weight;
            total_w += c.weight;
            hi = std::max(hi, s);
            lo = std::min(lo, s);
        }
        f32 value = 0.0f;
        switch (o.combine) {
            case Option::Combine::Max:     value = hi; break;
            case Option::Combine::Sum:     value = acc; break;
            case Option::Combine::Average: value = total_w > kEpsilon ? acc / total_w : 0.0f; break;
            case Option::Combine::Min:     value = lo; break;
        }
        out.emplace_back(o.name, math::clampf(value, 0.0f, 1.0f));
    }
    return out;
}

std::string UtilityAi::choose(const Blackboard& bb, f32 threshold) const {
    auto scored = score_all(bb);
    std::string best;
    f32 best_v = threshold;
    for (const auto& kv : scored) {
        if (kv.second > best_v) { best_v = kv.second; best = kv.first; }
    }
    return best;
}

// ============================================================ steering ======
namespace steering {

Vec2 seek(const SteeringAgent& a, Vec2 target) {
    const Vec2 desired = (target - a.position).normalized() * a.max_speed;
    Vec2 f = desired - a.velocity;
    const f32 len = f.length();
    return len > a.max_force ? f * (a.max_force / len) : f;
}

Vec2 flee(const SteeringAgent& a, Vec2 target) {
    const Vec2 desired = (a.position - target).normalized() * a.max_speed;
    Vec2 f = desired - a.velocity;
    const f32 len = f.length();
    return len > a.max_force ? f * (a.max_force / len) : f;
}

Vec2 arrive(const SteeringAgent& a, Vec2 target) {
    const Vec2 to = target - a.position;
    const f32 d = to.length();
    if (d < kEpsilon) return -a.velocity;                       // stop dead
    f32 speed = a.max_speed;
    if (d < a.slow_radius) {
        // Ramp linearly between the arrival and slow radii.
        const f32 t = a.slow_radius > a.arrival_radius
            ? (d - a.arrival_radius) / (a.slow_radius - a.arrival_radius)
            : 1.0f;
        speed = a.max_speed * math::clampf(t, 0.0f, 1.0f);
    }
    if (d < a.arrival_radius) speed = 0.0f;
    const Vec2 desired = (to / d) * speed;
    Vec2 f = desired - a.velocity;
    const f32 len = f.length();
    return len > a.max_force ? f * (a.max_force / len) : f;
}

Vec2 pursue(const SteeringAgent& a, Vec2 target_pos, Vec2 target_vel) {
    const f32 d = (target_pos - a.position).length();
    const f32 lookahead = std::min(2.0f, d / std::max(a.max_speed, kEpsilon));
    return seek(a, target_pos + target_vel * lookahead);
}

Vec2 evade(const SteeringAgent& a, Vec2 target_pos, Vec2 target_vel) {
    const f32 d = (a.position - target_pos).length();
    const f32 lookahead = std::min(2.0f, d / std::max(a.max_speed, kEpsilon));
    return flee(a, target_pos + target_vel * lookahead);
}

Vec2 wander(SteeringAgent& a, f32 dt, f32 jitter) {
    // LCG jitter keyed off the agent's own seed: wander stays reproducible,
    // which matters because LAN rollback replays must match bit for bit.
    a.wander_seed = a.wander_seed * 1664525u + 1013904223u;
    const f32 rnd = static_cast<f32>(a.wander_seed >> 8) * (1.0f / 16777216.0f);
    a.wander_angle += (rnd - 0.5f) * jitter * dt * 10.0f;
    const f32 circle_distance = 2.0f;
    const f32 circle_radius = 1.2f;
    Vec2 forward = a.velocity.normalized();
    if (forward.length_sq() < kEpsilon) forward = Vec2(1, 0);
    const Vec2 centre = a.position + forward * circle_distance;
    const Vec2 offset(std::cos(a.wander_angle) * circle_radius, std::sin(a.wander_angle) * circle_radius);
    return seek(a, centre + offset);
}

Vec2 separation(const SteeringAgent& a, const std::vector<SteeringAgent>& others, f32 radius) {
    Vec2 acc;
    int count = 0;
    for (const auto& o : others) {
        if (o.id == a.id) continue;
        const Vec2 d = a.position - o.position;
        const f32 dist = d.length();
        if (dist < kEpsilon || dist > radius) continue;
        acc += d / (dist * dist);          // closer neighbours push harder
        ++count;
    }
    if (count == 0) return Vec2(0, 0);
    acc = acc / static_cast<f32>(count);
    if (acc.length_sq() < kEpsilon) return Vec2(0, 0);
    Vec2 desired = acc.normalized() * a.max_speed;
    Vec2 f = desired - a.velocity;
    const f32 len = f.length();
    return len > a.max_force ? f * (a.max_force / len) : f;
}

Vec2 alignment(const SteeringAgent& a, const std::vector<SteeringAgent>& others, f32 radius) {
    Vec2 acc;
    int count = 0;
    for (const auto& o : others) {
        if (o.id == a.id) continue;
        if (a.position.distance(o.position) > radius) continue;
        acc += o.velocity;
        ++count;
    }
    if (count == 0) return Vec2(0, 0);
    acc = acc / static_cast<f32>(count);
    Vec2 desired = acc.normalized() * a.max_speed;
    Vec2 f = desired - a.velocity;
    const f32 len = f.length();
    return len > a.max_force ? f * (a.max_force / len) : f;
}

Vec2 cohesion(const SteeringAgent& a, const std::vector<SteeringAgent>& others, f32 radius) {
    Vec2 acc;
    int count = 0;
    for (const auto& o : others) {
        if (o.id == a.id) continue;
        if (a.position.distance(o.position) > radius) continue;
        acc += o.position;
        ++count;
    }
    if (count == 0) return Vec2(0, 0);
    return seek(a, acc / static_cast<f32>(count));
}

Vec2 flock(const SteeringAgent& a, const std::vector<SteeringAgent>& others,
           f32 sep_w, f32 ali_w, f32 coh_w, f32 radius) {
    return separation(a, others, radius * 0.5f) * sep_w +
           alignment(a, others, radius) * ali_w +
           cohesion(a, others, radius) * coh_w;
}

Vec2 avoid_obstacles(const SteeringAgent& a, const std::vector<std::pair<Vec2,f32>>& obstacles,
                     f32 look_ahead) {
    Vec2 forward = a.velocity.normalized();
    if (forward.length_sq() < kEpsilon) return Vec2(0, 0);
    const Vec2 side = forward.perpendicular();

    struct Hit { bool hit = false; f32 t = 1e9f; Vec2 point; f32 radius = 0; };
    auto probe = [&](Vec2 origin, Vec2 dir) {
        Hit best;
        for (const auto& o : obstacles) {
            // closest approach of the ray to the circle centre
            const Vec2 to = o.first - origin;
            const f32 along = to.dot(dir);
            if (along < 0.0f || along > look_ahead) continue;
            const Vec2 closest = origin + dir * along;
            const f32 d = closest.distance(o.first);
            if (d <= o.second && along < best.t) {
                best = Hit{true, along, closest, o.second};
            }
        }
        return best;
    };
    const Hit c = probe(a.position, forward);
    if (!c.hit) return Vec2(0, 0);
    const Hit l = probe(a.position + side * a.radius, forward);
    const Hit r = probe(a.position - side * a.radius, forward);
    // Steer away from the nearest threat, biased by which whisker hit.
    Vec2 away = (a.position - c.point).normalized();
    if (l.hit && l.t < c.t) away = away + side;
    if (r.hit && r.t < c.t) away = away - side;
    const f32 urgency = 1.0f - math::clampf(c.t / std::max(look_ahead, kEpsilon), 0.0f, 1.0f);
    Vec2 f = away.normalized() * (a.max_force * (0.4f + 0.6f * urgency));
    return f;
}

Vec2 follow_path(const SteeringAgent& a, const std::vector<Vec2>& path,
                 std::size_t& waypoint, f32 acceptance) {
    if (path.empty()) return Vec2(0, 0);
    if (waypoint >= path.size()) waypoint = path.size() - 1;
    while (waypoint + 1 < path.size() && a.position.distance(path[waypoint]) < acceptance) ++waypoint;
    const bool last = (waypoint + 1 >= path.size());
    return last ? arrive(a, path[waypoint]) : seek(a, path[waypoint]);
}

Vec2 wall_avoidance(const SteeringAgent& a, const std::vector<std::pair<Vec2,Vec2>>& walls,
                    f32 look_ahead) {
    Vec2 forward = a.velocity.normalized();
    if (forward.length_sq() < kEpsilon) return Vec2(0, 0);
    const Vec2 ahead = a.position + forward * look_ahead;
    Vec2 force;
    for (const auto& w : walls) {
        const Vec2 e = w.second - w.first;
        const f32 len2 = e.length_sq();
        if (len2 < kEpsilon) continue;
        const f32 t = math::clampf((ahead - w.first).dot(e) / len2, 0.0f, 1.0f);
        const Vec2 closest = w.first + e * t;
        const f32 d = ahead.distance(closest);
        if (d >= a.radius) continue;
        // Push directly off the wall surface. Using (ahead - closest) instead
        // would shove the agent *along* its own travel direction on a head-on
        // approach, which is the opposite of avoidance.
        Vec2 away = a.position - closest;
        const f32 adist = away.length();
        if (adist < kEpsilon) away = -forward;       // standing on the wall: back off
        else away = away / adist;
        force += away * (a.max_force * (1.0f - d / std::max(a.radius, kEpsilon)));
    }
    return force;
}

void integrate(SteeringAgent& a, Vec2 force, f32 dt) {
    const f32 inv_mass = a.mass > kEpsilon ? 1.0f / a.mass : 1.0f;
    a.acceleration = force * inv_mass;
    a.velocity += a.acceleration * dt;
    const f32 speed = a.velocity.length();
    if (speed > a.max_speed) a.velocity = a.velocity * (a.max_speed / speed);
    a.position += a.velocity * dt;
    if (speed < kEpsilon * 10.0f) a.velocity = Vec2(0, 0);
}
} // namespace steering

std::vector<Vec2> formation_slots(Formation f, std::size_t count, Vec2 leader, Vec2 facing, f32 spacing) {
    std::vector<Vec2> out;
    if (count == 0) return out;
    const Vec2 fwd = facing.length_sq() > kEpsilon ? facing.normalized() : Vec2(0, 1);
    const Vec2 right = fwd.perpendicular();
    const f32 sp = std::max(0.05f, spacing);
    out.reserve(count);

    switch (f) {
        case Formation::Line:
        case Formation::Column: {
            const Vec2 axis = (f == Formation::Line) ? right : -fwd;
            const f32 half = static_cast<f32>(count - 1) * 0.5f;
            for (std::size_t i = 0; i < count; ++i)
                out.push_back(leader + axis * (static_cast<f32>(i) - half) * sp);
            break;
        }
        case Formation::V:
        case Formation::Wedge: {
            out.push_back(leader);
            std::size_t placed = 1;
            for (std::size_t row = 1; placed < count; ++row) {
                for (int side = -1; side <= 1 && placed < count; side += 2) {
                    out.push_back(leader - fwd * (static_cast<f32>(row) * sp) +
                                  right * (static_cast<f32>(side) * static_cast<f32>(row) * sp));
                    ++placed;
                }
            }
            break;
        }
        case Formation::Circle: {
            const f32 radius = sp * static_cast<f32>(count) / math::TwoPi;
            for (std::size_t i = 0; i < count; ++i) {
                const f32 a = math::TwoPi * static_cast<f32>(i) / static_cast<f32>(count);
                out.push_back(leader + right * (std::cos(a) * radius) + fwd * (std::sin(a) * radius));
            }
            break;
        }
    }
    return out;
}

// =============================================================== crowd ======
u32 Crowd::add(Vec2 pos, f32 radius, f32 max_speed) {
    for (std::size_t i = 0; i < alive_.size(); ++i) {
        if (!alive_[i]) {
            alive_[i] = true;
            agents_[i] = SteeringAgent{};
            agents_[i].position = pos;
            agents_[i].radius = radius;
            agents_[i].max_speed = max_speed;
            agents_[i].id = static_cast<u32>(i);
            targets_[i] = pos;
            return static_cast<u32>(i);
        }
    }
    SteeringAgent a;
    a.position = pos;
    a.radius = radius;
    a.max_speed = max_speed;
    a.id = static_cast<u32>(agents_.size());
    agents_.push_back(a);
    targets_.push_back(pos);
    alive_.push_back(true);
    return a.id;
}

void Crowd::remove(u32 id) {
    if (id < alive_.size()) { alive_[id] = false; agents_[id].velocity = Vec2(0, 0); }
}

SteeringAgent* Crowd::agent(u32 id) { return id < agents_.size() && alive_[id] ? &agents_[id] : nullptr; }
const SteeringAgent* Crowd::agent(u32 id) const {
    return id < agents_.size() && alive_[id] ? &agents_[id] : nullptr;
}

void Crowd::set_target(u32 id, Vec2 target) { if (id < targets_.size()) targets_[id] = target; }

std::vector<u32> Crowd::neighbours(u32 id) const {
    std::vector<u32> out;
    if (id >= agents_.size() || !alive_[id]) return out;
    const SteeringAgent& a = agents_[id];
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        if (i == id || !alive_[i]) continue;
        if (a.position.distance(agents_[i].position) <= cfg_.neighbour_radius)
            out.push_back(static_cast<u32>(i));
    }
    if (cfg_.deterministic) std::sort(out.begin(), out.end());
    if (out.size() > static_cast<std::size_t>(cfg_.max_neighbours))
        out.resize(static_cast<std::size_t>(cfg_.max_neighbours));
    return out;
}

void Crowd::update(f32 dt, const FlowField* field, f32 cell_size) {
    // 1. desired velocity
    std::vector<Vec2> desired(agents_.size(), Vec2(0, 0));
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        if (!alive_[i]) continue;
        const SteeringAgent& a = agents_[i];
        Vec2 target = targets_[i];
        if (field && cell_size > kEpsilon) {
            const i32 cx = static_cast<i32>(std::floor(a.position.x / cell_size));
            const i32 cy = static_cast<i32>(std::floor(a.position.y / cell_size));
            const Vec2 dir = field->direction_at(cx, cy);
            if (dir.length_sq() > kEpsilon) {
                desired[i] = dir * a.max_speed;
                continue;
            }
        }
        const f32 d = a.position.distance(target);
        if (d < a.radius) continue;                 // arrived
        const f32 speed = a.max_speed * math::clampf(d / 2.0f, 0.2f, 1.0f);
        desired[i] = (target - a.position).normalized() * speed;
    }

    // 2. reciprocal velocity obstacle avoidance
    std::vector<Vec2> adjusted = desired;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        if (!alive_[i]) continue;
        const SteeringAgent& a = agents_[i];
        Vec2 push(0, 0);
        int count = 0;
        const auto nbrs = neighbours(static_cast<u32>(i));
        for (u32 j : nbrs) {
            const SteeringAgent& b = agents_[j];
            const Vec2 rel = a.position - b.position;
            const f32 dist = rel.length();
            const f32 min_dist = a.radius + b.radius;
            if (dist < kEpsilon) continue;
            // relative velocity along the collision course
            const Vec2 rel_vel = a.velocity - b.velocity;
            const f32 closing = rel_vel.dot(rel / dist);
            if (closing > 0.0f && dist < min_dist * 2.5f) {
                // take half the correction: that is the "reciprocal" part
                push += (rel / dist) * (closing * 0.5f);
                ++count;
            }
            if (dist < min_dist) {                 // hard separation
                push += (rel / dist) * (min_dist - dist) * 4.0f;
                ++count;
            }
        }
        if (count > 0) {
            adjusted[i] = desired[i] + push / static_cast<f32>(std::min(count, std::max(1, cfg_.max_neighbours)));
            const f32 len = adjusted[i].length();
            if (len > a.max_speed) adjusted[i] = adjusted[i] * (a.max_speed / len);
        }
    }

    // 3. integrate (deterministic order)
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        if (!alive_[i]) continue;
        SteeringAgent& a = agents_[i];
        const Vec2 force = (adjusted[i] - a.velocity) * (a.max_force / std::max(a.max_speed, kEpsilon));
        steering::integrate(a, force, dt);
    }
}

u64 Crowd::checksum() const {
    u64 acc = 2166136261ull;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        if (!alive_[i]) continue;
        u32 bx, by;
        std::memcpy(&bx, &agents_[i].position.x, sizeof(bx));
        std::memcpy(&by, &agents_[i].position.y, sizeof(by));
        acc = (acc ^ bx) * 16777619ull;
        acc = (acc ^ by) * 16777619ull;
    }
    return acc;
}

} // namespace prism::ai
