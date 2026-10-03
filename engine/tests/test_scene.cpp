// =====================================================================
//  PRISM ENGINE — tests/test_scene.cpp
//  Hierarchy transforms, lookup/destroy, inspector component bags and the
//  JSON round-trip used by the .prism pack.
// =====================================================================
#include "prism_test.h"
#include "prism/scene/scene.h"

#include <cmath>

using namespace prism;
using namespace prism::scene;

namespace {
bool close(f32 a, f32 b, f32 eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace

PRISM_TEST(scene_hierarchy_composes_world_transforms) {
    Scene s;
    auto parent = s.create("parent");
    parent->set_local_position(math::Vec3(1, 2, 3));
    parent->set_local_rotation(math::Quat::from_axis_angle(math::Vec3(0, 1, 0), math::Pi * 0.5f));
    auto child = s.create("child", parent.get());
    child->set_local_position(math::Vec3(1, 0, 0));
    const math::Vec3 w = child->world_position();
    // 90° yaw about Y turns local +X into -Z, then the parent offset applies.
    PRISM_CHECK(close(w.x, 1.0f, 1e-3f));
    PRISM_CHECK(close(w.y, 2.0f, 1e-3f));
    PRISM_CHECK(close(w.z, 2.0f, 1e-3f));   // 3 + (-1)
    PRISM_CHECK_EQ(child->parent(), parent.get());
    PRISM_CHECK_EQ(s.node_count(), std::size_t(3));
}

PRISM_TEST(scene_scale_multiplies_down_the_chain) {
    Scene s;
    auto a = s.create("a");
    a->set_local_scale(math::Vec3(2, 2, 2));
    auto b = s.create("b", a.get());
    b->set_local_position(math::Vec3(1, 0, 0));
    b->set_local_scale(math::Vec3(3, 3, 3));
    math::Vec3 p, sc;
    math::Quat r;
    b->world_transform(p, r, sc);
    PRISM_CHECK(close(p.x, 2.0f));          // parent scale stretches child offset
    PRISM_CHECK(close(sc.x, 6.0f));         // 2 * 3
}

PRISM_TEST(scene_lookup_destroy_and_reparent) {
    Scene s;
    auto a = s.create("a");
    auto b = s.create("b", a.get());
    s.create("c", b.get());
    PRISM_CHECK(s.find_by_path("a/b/c") != nullptr);
    PRISM_CHECK(s.find_by_path("a/b/missing") == nullptr);
    PRISM_CHECK(s.find_by_path("a/b/c")->path() == "root/a/b/c");

    PRISM_CHECK_EQ(s.node_count(), std::size_t(4));
    PRISM_CHECK(s.destroy(b.get()));                 // takes "c" down with it
    PRISM_CHECK_EQ(s.node_count(), std::size_t(2));
    PRISM_CHECK(!s.destroy(s.root().get()));         // root is protected
    PRISM_CHECK(b->parent() == nullptr);             // destroyed node unlinked

    // Reparenting through add_child moves the subtree.
    Scene s2;
    auto x = s2.create("x");
    auto y = s2.create("y");
    x->add_child(y);
    PRISM_CHECK_EQ(s2.node_count(), std::size_t(3));
    auto z = s2.create("z");
    z->add_child(y);                                  // steal y from x
    PRISM_CHECK_EQ(x->children().size(), std::size_t(0));
    PRISM_CHECK_EQ(z->children().size(), std::size_t(1));
}

PRISM_TEST(scene_inspector_component_bags) {
    Scene s;
    auto n = s.create("player");
    auto mesh = json::Value::make_object();
    mesh.set("asset", json::Value("models/ray.prism"));
    mesh.set("visible", json::Value(true));
    n->set_component("MeshRenderer", mesh);
    auto light = json::Value::make_object();
    light.set("intensity", json::Value(2.5));
    n->set_component("Light", light);

    const json::Value* got = n->component("MeshRenderer");
    PRISM_CHECK(got != nullptr);
    PRISM_CHECK(got->get_string("asset") == "models/ray.prism");
    PRISM_CHECK(got->get_bool("visible", false));
    PRISM_CHECK(n->component("Light")->get_number("intensity") == 2.5);
    PRISM_CHECK(n->component("Missing") == nullptr);
    PRISM_CHECK_EQ(n->component_names().size(), std::size_t(2));
    n->remove_component("Light");
    PRISM_CHECK_EQ(n->component_names().size(), std::size_t(1));
}

PRISM_TEST(scene_json_roundtrip_preserves_everything) {
    Scene s;
    auto arm = s.create("arm");
    arm->set_local_position(math::Vec3(1, 2, 3));
    arm->set_local_rotation(math::Quat::from_axis_angle(math::Vec3(0, 1, 0), 0.5f));
    arm->set_local_scale(math::Vec3(2, 2, 2));
    auto hand = s.create("hand", arm.get());
    hand->set_local_position(math::Vec3(0.5f, 0, 0));
    auto props = json::Value::make_object();
    props.set("mesh", json::Value("hand.prm"));
    props.set("lod_bias", json::Value(1.5));
    hand->set_component("MeshRenderer", props);

    const std::string text = s.to_json();
    Scene s2;
    std::string err;
    PRISM_CHECK(s2.from_json(text, &err));
    PRISM_CHECK(err.empty());
    PRISM_CHECK_EQ(s2.node_count(), s.node_count());

    const auto arm2 = s2.find_by_path("arm");
    const auto hand2 = s2.find_by_path("arm/hand");
    PRISM_CHECK(arm2 != nullptr && hand2 != nullptr);
    PRISM_CHECK(close(arm2->local_position().y, 2.0f));
    PRISM_CHECK(close(arm2->local_scale().x, 2.0f));
    PRISM_CHECK(close(arm2->local_rotation().y, arm->local_rotation().y));
    PRISM_CHECK(close(arm2->local_rotation().w, arm->local_rotation().w));
    PRISM_CHECK(close(hand2->local_position().x, 0.5f));
    const json::Value* mr = hand2->component("MeshRenderer");
    PRISM_CHECK(mr != nullptr);
    PRISM_CHECK(mr->get_string("mesh") == "hand.prm");
    PRISM_CHECK(mr->get_number("lod_bias") == 1.5);

    // Malformed input is rejected without touching the scene.
    Scene s3;
    PRISM_CHECK(!s3.from_json("{ not json", &err));
    PRISM_CHECK(!err.empty());
}
