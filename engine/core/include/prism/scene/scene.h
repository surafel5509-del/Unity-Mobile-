// =====================================================================
//  PRISM ENGINE — scene/scene.h
//  The hierarchy/inspector data model: named nodes with local transforms,
//  parent/child composition into world space, and inspector-style typed
//  component bags stored as JSON values so a whole scene round-trips
//  through the .prism pack and the (future) editor without loss.
// =====================================================================
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../core/json.h"
#include "../core/types.h"
#include "../math/math.h"

namespace prism::scene {

class Scene;

class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(std::string name = "node") : name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] const math::Vec3& local_position() const { return position_; }
    void set_local_position(math::Vec3 v) { position_ = v; }
    [[nodiscard]] const math::Quat& local_rotation() const { return rotation_; }
    void set_local_rotation(math::Quat q) { rotation_ = q; }
    [[nodiscard]] const math::Vec3& local_scale() const { return scale_; }
    void set_local_scale(math::Vec3 v) { scale_ = v; }

    [[nodiscard]] math::Vec3 world_position() const;
    [[nodiscard]] math::Quat world_rotation() const;
    void world_transform(math::Vec3& pos, math::Quat& rot, math::Vec3& scale) const;

    void add_child(std::shared_ptr<Node> child);
    void remove_child(Node* child);
    [[nodiscard]] const std::vector<std::shared_ptr<Node>>& children() const { return children_; }
    [[nodiscard]] Node* parent() const { return parent_; }
    [[nodiscard]] std::shared_ptr<Node> find(std::string_view name, bool recursive = true);
    [[nodiscard]] std::size_t subtree_size() const;
    [[nodiscard]] std::string path() const;

    /// Inspector-style component bag: comp name -> JSON object of properties.
    void set_component(const std::string& comp, json::Value props);
    [[nodiscard]] const json::Value* component(std::string_view comp) const;
    void remove_component(const std::string& comp);
    [[nodiscard]] std::vector<std::string> component_names() const;

    [[nodiscard]] json::Value to_json() const;

private:
    friend class Scene;
    std::string name_;
    math::Vec3 position_;
    math::Quat rotation_;
    math::Vec3 scale_ = math::Vec3(1, 1, 1);
    Node* parent_ = nullptr;
    std::vector<std::shared_ptr<Node>> children_;
    std::map<std::string, json::Value> components_;
};

class Scene {
public:
    Scene();
    [[nodiscard]] std::shared_ptr<Node> root() const { return root_; }
    /// Creates a node under `parent` (or the root when null).
    std::shared_ptr<Node> create(const std::string& name, Node* parent = nullptr);
    bool destroy(Node* node);
    [[nodiscard]] std::shared_ptr<Node> find_by_path(std::string_view path) const;
    [[nodiscard]] std::size_t node_count() const { return root_ ? root_->subtree_size() : 0; }
    [[nodiscard]] std::string to_json(i32 indent = 2) const;
    bool from_json(std::string_view text, std::string* error = nullptr);

private:
    std::shared_ptr<Node> root_;
};

} // namespace prism::scene
