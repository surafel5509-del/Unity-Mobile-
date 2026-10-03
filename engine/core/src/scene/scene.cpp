// =====================================================================
//  PRISM ENGINE — scene/scene.cpp
// =====================================================================
#include "prism/scene/scene.h"

#include <algorithm>
#include <sstream>

namespace prism::scene {

// ------------------------------------------------------------------ node --
void Node::world_transform(math::Vec3& pos, math::Quat& rot, math::Vec3& scale) const {
    pos = position_;
    rot = rotation_;
    scale = scale_;
    for (const Node* p = parent_; p != nullptr; p = p->parent_) {
        pos = p->rotation_.rotate(math::Vec3(pos.x * p->scale_.x, pos.y * p->scale_.y, pos.z * p->scale_.z)) + p->position_;
        rot = p->rotation_ * rot;
        scale = math::Vec3(scale.x * p->scale_.x, scale.y * p->scale_.y, scale.z * p->scale_.z);
    }
}

math::Vec3 Node::world_position() const {
    math::Vec3 p; math::Quat r; math::Vec3 s;
    world_transform(p, r, s);
    return p;
}

math::Quat Node::world_rotation() const {
    math::Vec3 p; math::Quat r; math::Vec3 s;
    world_transform(p, r, s);
    return r;
}

void Node::add_child(std::shared_ptr<Node> child) {
    if (!child || child.get() == this) return;
    if (child->parent_) child->parent_->remove_child(child.get());
    child->parent_ = this;
    children_.push_back(std::move(child));
}

void Node::remove_child(Node* child) {
    children_.erase(std::remove_if(children_.begin(), children_.end(),
                                   [child](const std::shared_ptr<Node>& c) { return c.get() == child; }),
                    children_.end());
    if (child) child->parent_ = nullptr;
}

std::shared_ptr<Node> Node::find(std::string_view name, bool recursive) {
    for (auto& c : children_) {
        if (c->name_ == name) return c;
        if (recursive) {
            if (auto hit = c->find(name, true)) return hit;
        }
    }
    return nullptr;
}

std::size_t Node::subtree_size() const {
    std::size_t n = 1;
    for (const auto& c : children_) n += c->subtree_size();
    return n;
}

std::string Node::path() const {
    std::string p = name_;
    for (const Node* a = parent_; a != nullptr; a = a->parent_) p = a->name_ + "/" + p;
    return p;
}

void Node::set_component(const std::string& comp, json::Value props) {
    components_[comp] = std::move(props);
}

const json::Value* Node::component(std::string_view comp) const {
    auto it = components_.find(std::string(comp));
    return it == components_.end() ? nullptr : &it->second;
}

void Node::remove_component(const std::string& comp) { components_.erase(comp); }

std::vector<std::string> Node::component_names() const {
    std::vector<std::string> out;
    out.reserve(components_.size());
    for (const auto& kv : components_) out.push_back(kv.first);
    return out;
}

namespace {
json::Value vec3_json(const math::Vec3& v) {
    json::Value a = json::Value::make_array();
    a.push_back(json::Value(static_cast<f64>(v.x)));
    a.push_back(json::Value(static_cast<f64>(v.y)));
    a.push_back(json::Value(static_cast<f64>(v.z)));
    return a;
}
math::Vec3 json_vec3(const json::Value& a, math::Vec3 fallback = math::Vec3()) {
    if (!a.is_array() || a.size() < 3) return fallback;
    return math::Vec3(static_cast<f32>(a.at(0).as_number(fallback.x)),
                      static_cast<f32>(a.at(1).as_number(fallback.y)),
                      static_cast<f32>(a.at(2).as_number(fallback.z)));
}
} // namespace

json::Value Node::to_json() const {
    json::Value o = json::Value::make_object();
    o.set("name", json::Value(name_));
    o.set("position", vec3_json(position_));
    json::Value q = json::Value::make_array();
    q.push_back(json::Value(static_cast<f64>(rotation_.x)));
    q.push_back(json::Value(static_cast<f64>(rotation_.y)));
    q.push_back(json::Value(static_cast<f64>(rotation_.z)));
    q.push_back(json::Value(static_cast<f64>(rotation_.w)));
    o.set("rotation", q);
    o.set("scale", vec3_json(scale_));
    if (!components_.empty()) {
        json::Value comps = json::Value::make_object();
        for (const auto& kv : components_) comps.set(kv.first, kv.second);
        o.set("components", comps);
    }
    json::Value kids = json::Value::make_array();
    for (const auto& c : children_) kids.push_back(c->to_json());
    o.set("children", kids);
    return o;
}

// ----------------------------------------------------------------- scene --
Scene::Scene() { root_ = std::make_shared<Node>("root"); }

std::shared_ptr<Node> Scene::create(const std::string& name, Node* parent) {
    auto node = std::make_shared<Node>(name);
    (parent ? parent : root_.get())->add_child(node);
    return node;
}

bool Scene::destroy(Node* node) {
    if (!node || node == root_.get()) return false;
    Node* p = node->parent();
    if (!p) return false;
    p->remove_child(node);
    return true;
}

std::shared_ptr<Node> Scene::find_by_path(std::string_view path) const {
    std::shared_ptr<Node> cur = root_;
    std::string token;
    std::stringstream ss{std::string(path)};
    while (std::getline(ss, token, '/')) {
        if (token.empty()) continue;
        cur = cur->find(token, false);
        if (!cur) return nullptr;
    }
    return cur;
}

std::string Scene::to_json(i32 indent) const {
    return root_ ? root_->to_json().dump(indent) : "{}";
}

namespace {
void load_node(const json::Value& v, Node* into) {
    into->set_local_position(json_vec3(v["position"]));
    const json::Value& q = v["rotation"];
    if (q.is_array() && q.size() == 4) {
        into->set_local_rotation(math::Quat(
            static_cast<f32>(q.at(0).as_number(0)),
            static_cast<f32>(q.at(1).as_number(0)),
            static_cast<f32>(q.at(2).as_number(0)),
            static_cast<f32>(q.at(3).as_number(1))));
    }
    into->set_local_scale(json_vec3(v["scale"], math::Vec3(1, 1, 1)));
    const json::Value& comps = v["components"];
    if (comps.is_object()) {
        for (const auto& name : comps.keys()) into->set_component(name, comps[name]);
    }
    const json::Value& kids = v["children"];
    if (kids.is_array()) {
        for (std::size_t i = 0; i < kids.size(); ++i) {
            const json::Value& kv = kids.at(i);
            auto child = std::make_shared<Node>(kv["name"].as_string_or("node"));
            into->add_child(child);
            load_node(kv, child.get());
        }
    }
}
} // namespace

bool Scene::from_json(std::string_view text, std::string* error) {
    json::Value v = json::Value::parse(text, error);
    if (!v.is_object()) return false;
    root_ = std::make_shared<Node>(v["name"].as_string_or("root"));
    load_node(v, root_.get());
    return true;
}

} // namespace prism::scene
