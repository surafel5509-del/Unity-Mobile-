// =====================================================================
//  PRISM ENGINE — input/input.h
//  Multi-touch, gesture recognition, virtual controls, gamepad mapping,
//  motion sensors. Fed by the Android MotionEvent layer (JNI).
// =====================================================================
#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <deque>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::input {

inline constexpr i32 kMaxTouches = 10;

enum class TouchPhase : u8 { Began, Moved, Stationary, Ended, Cancelled };

struct Touch {
    i32 id = 0;
    math::Vec2 position;
    math::Vec2 start_position;
    math::Vec2 delta;
    f32 pressure = 1.0f;
    f32 start_time = 0;
    f32 age = 0;
    TouchPhase phase = TouchPhase::Began;
};

enum class GestureKind : u8 { None, Tap, DoubleTap, LongPress, Drag, Swipe, Pinch, Rotate, TwoFingerDrag };

struct Gesture {
    GestureKind kind = GestureKind::None;
    math::Vec2 position;
    math::Vec2 delta;
    f32 scale = 1.0f;        // pinch
    f32 rotation = 0.0f;     // radians, two-finger rotate
    f32 velocity = 0.0f;     // px/s for swipe
    f32 duration = 0.0f;
};

struct GestureConfig {
    f32 tap_max_duration    = 0.25f;
    f32 tap_max_distance    = 24.0f;
    f32 double_tap_window   = 0.30f;
    f32 long_press_duration = 0.50f;
    f32 swipe_min_distance  = 60.0f;
    f32 swipe_max_duration  = 0.40f;
    f32 min_pinch_distance  = 20.0f;
    i32 multi_touch_max     = kMaxTouches;
};

/// Deterministic, allocation-free gesture recogniser (no OS dependency: unit-testable).
class GestureRecognizer {
public:
    explicit GestureRecognizer(GestureConfig cfg = {}) : cfg_(cfg) {}

    void touch_down(i32 id, f32 x, f32 y, f32 time);
    void touch_move(i32 id, f32 x, f32 y, f32 time);
    void touch_up(i32 id, f32 x, f32 y, f32 time);

    /// Call every frame with the current time; appends recognised gestures.
    void update(f32 time);

    [[nodiscard]] const std::vector<Gesture>& gestures() const { return out_; }
    void clear_gestures() { out_.clear(); }
    [[nodiscard]] const std::vector<Touch>& touches() const { return active_; }
    [[nodiscard]] i32 touch_count() const { return static_cast<i32>(active_.size()); }
    [[nodiscard]] const Touch* touch(i32 index) const {
        return (index >= 0 && index < static_cast<i32>(active_.size())) ? &active_[index] : nullptr;
    }
    void set_config(GestureConfig c) { cfg_ = c; }

private:
    Touch* find(i32 id);
    void recognise_on_release(Touch& t, f32 time);
    void recognise_multi(f32 time);

    GestureConfig cfg_;
    std::vector<Touch> active_;
    std::vector<Gesture> out_;
    f32 last_tap_time_ = -10.0f;
    math::Vec2 last_tap_pos_;
    f32 prev_pinch_dist_ = 0.0f;
    f32 prev_pinch_angle_ = 0.0f;
    bool pinching_ = false;
};

/// Virtual controls shipped as templates (templates/touch-controls).
enum class ControlKind : u8 { Joystick, DPad, Button, Slider, SwipeArea, TapZone };

struct VirtualControl {
    std::string name;
    ControlKind kind = ControlKind::Button;
    math::Rect rect;                    // screen-space, 0..1 normalised
    math::Vec2 value;                   // joystick axis / slider value
    bool pressed = false;
    bool visible = true;
    f32 dead_zone = 0.12f;
    math::Color tint = math::Color::spectrum_cyan();
    i32 tracking_touch = -1;
};

/// Maps physical/pointer input to named axes & buttons ("horizontal", "jump", ...).
class InputMap {
public:
    struct State {
        std::unordered_map<std::string, f32> axes;
        std::unordered_map<std::string, bool> buttons;
        std::unordered_map<std::string, bool> buttons_down;   // edge: pressed this frame
        std::unordered_map<std::string, bool> buttons_up;
    };

    void set_axis(const std::string& name, f32 v) { state_.axes[name] = v; }
    f32  axis(const std::string& name) const { auto it = state_.axes.find(name); return it == state_.axes.end() ? 0.0f : it->second; }
    bool button(const std::string& name) const { auto it = state_.buttons.find(name); return it != state_.buttons.end() && it->second; }
    bool button_down(const std::string& name) const { auto it = state_.buttons_down.find(name); return it != state_.buttons_down.end() && it->second; }
    bool button_up(const std::string& name) const { auto it = state_.buttons_up.find(name); return it != state_.buttons_up.end() && it->second; }

    void press(const std::string& name);
    void release(const std::string& name);
    void end_frame();     // clears edge states

    VirtualControl& control(const std::string& name);
    [[nodiscard]] const std::vector<VirtualControl>& controls() const { return controls_; }
    /// Apply virtual control state onto axes/buttons (joystick -> "horizontal"/"vertical").
    void apply_controls();

    // ---- gamepad (Bluetooth HID: Xbox / PS / generic) -------------------
    void set_gamepad_axis(i32 pad, const std::string& axis, f32 v);
    void set_gamepad_button(i32 pad, const std::string& button, bool down);
    [[nodiscard]] f32 gamepad_axis(i32 pad, const std::string& axis) const;
    [[nodiscard]] bool gamepad_button(i32 pad, const std::string& button) const;
    [[nodiscard]] bool gamepad_connected(i32 pad) const;
    void set_gamepad_connected(i32 pad, bool connected);

    // ---- motion sensors -------------------------------------------------
    struct Motion { math::Vec3 accel; math::Vec3 gyro; math::Vec3 magnet; };
    void set_motion(const Motion& m) { motion_ = m; }
    [[nodiscard]] const Motion& motion() const { return motion_; }

    State& state() { return state_; }
    [[nodiscard]] const State& state() const { return state_; }

private:
    State state_;
    std::vector<VirtualControl> controls_;
    std::unordered_map<i32, std::unordered_map<std::string, f32>> pad_axes_;
    std::unordered_map<i32, std::unordered_map<std::string, bool>> pad_buttons_;
    std::unordered_map<i32, bool> pad_connected_;
    Motion motion_;
};

} // namespace prism::input
