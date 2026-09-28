#include "prism/input/input.h"
#include <algorithm>
#include <cmath>

namespace prism::input {

Touch* GestureRecognizer::find(i32 id) {
    for (auto& t : active_) if (t.id == id) return &t;
    return nullptr;
}

void GestureRecognizer::touch_down(i32 id, f32 x, f32 y, f32 time) {
    if (touch_count() >= cfg_.multi_touch_max) return;
    Touch t;
    t.id = id;
    t.position = t.start_position = math::Vec2{x, y};
    t.phase = TouchPhase::Began;
    t.start_time = time;
    t.age = 0;
    t.pressure = 1.0f;
    active_.push_back(t);
}

void GestureRecognizer::touch_move(i32 id, f32 x, f32 y, f32 time) {
    Touch* t = find(id);
    if (!t) return;
    math::Vec2 p{x, y};
    t->delta = p - t->position;
    t->position = p;
    t->phase = (p - t->start_position).length() > 1.0f ? TouchPhase::Moved : TouchPhase::Stationary;
    t->age = time - t->start_time;
}

void GestureRecognizer::touch_up(i32 id, f32 x, f32 y, f32 time) {
    for (auto it = active_.begin(); it != active_.end(); ++it) {
        if (it->id != id) continue;
        it->position = math::Vec2{x, y};
        it->phase = TouchPhase::Ended;
        it->age = time - it->start_time;
        recognise_on_release(*it, time);
        active_.erase(it);
        pinching_ = false;
        return;
    }
}

void GestureRecognizer::recognise_on_release(Touch& t, f32 time) {
    f32 dist = (t.position - t.start_position).length();
    f32 dur  = time - t.start_time;

    if (dist <= cfg_.tap_max_distance && dur <= cfg_.tap_max_duration) {
        Gesture g;
        g.kind = GestureKind::Tap;
        g.position = t.position;
        g.duration = dur;
        if (time - last_tap_time_ <= cfg_.double_tap_window &&
            (t.position - last_tap_pos_).length() <= cfg_.tap_max_distance) {
            g.kind = GestureKind::DoubleTap;
            last_tap_time_ = -10.0f;
        } else {
            last_tap_time_ = time;
            last_tap_pos_ = t.position;
        }
        out_.push_back(g);
        return;
    }
    if (dist <= cfg_.tap_max_distance && dur >= cfg_.long_press_duration) {
        Gesture g; g.kind = GestureKind::LongPress; g.position = t.position; g.duration = dur;
        out_.push_back(g);
        return;
    }
    if (dist >= cfg_.swipe_min_distance && dur <= cfg_.swipe_max_duration) {
        Gesture g;
        g.kind = GestureKind::Swipe;
        g.position = t.position;
        g.delta = t.position - t.start_position;
        g.velocity = dur > kEpsilon ? dist / dur : 0.0f;
        g.duration = dur;
        out_.push_back(g);
        return;
    }
    Gesture g;
    g.kind = GestureKind::Drag;
    g.position = t.position;
    g.delta = t.position - t.start_position;
    g.duration = dur;
    out_.push_back(g);
}

void GestureRecognizer::recognise_multi(f32 time) {
    if (active_.size() < 2) { pinching_ = false; return; }
    const Touch& a = active_[0];
    const Touch& b = active_[1];
    f32 dist  = (b.position - a.position).length();
    f32 angle = std::atan2(b.position.y - a.position.y, b.position.x - a.position.x);
    if (!pinching_) {
        pinching_ = true;
        prev_pinch_dist_ = dist;
        prev_pinch_angle_ = angle;
        return;
    }
    if (dist > cfg_.min_pinch_distance || prev_pinch_dist_ > cfg_.min_pinch_distance) {
        Gesture g;
        g.kind = GestureKind::Pinch;
        g.position = (a.position + b.position) * 0.5f;
        g.scale = prev_pinch_dist_ > kEpsilon ? dist / prev_pinch_dist_ : 1.0f;
        g.duration = time - std::min(a.start_time, b.start_time);
        out_.push_back(g);
    }
    f32 d_angle = angle - prev_pinch_angle_;
    if (std::fabs(d_angle) > 0.01f) {
        Gesture g;
        g.kind = GestureKind::Rotate;
        g.position = (a.position + b.position) * 0.5f;
        g.rotation = d_angle;
        out_.push_back(g);
    }
    prev_pinch_dist_ = dist;
    prev_pinch_angle_ = angle;
}

void GestureRecognizer::update(f32 time) {
    for (auto& t : active_) {
        t.age = time - t.start_time;
        if (t.phase == TouchPhase::Began) t.phase = TouchPhase::Stationary;
        // long press fires while the finger stays down
        if (t.phase == TouchPhase::Stationary &&
            (t.position - t.start_position).length() <= cfg_.tap_max_distance &&
            t.age >= cfg_.long_press_duration) {
            Gesture g;
            g.kind = GestureKind::LongPress;
            g.position = t.position;
            g.duration = t.age;
            out_.push_back(g);
            t.phase = TouchPhase::Moved;      // fire once
        }
    }
    recognise_multi(time);
}

// ---------------------------------------------------------------- InputMap --
void InputMap::press(const std::string& name) {
    bool was = button(name);
    state_.buttons[name] = true;
    state_.buttons_down[name] = !was;
    state_.buttons_up[name] = false;
}

void InputMap::release(const std::string& name) {
    bool was = button(name);
    state_.buttons[name] = false;
    state_.buttons_up[name] = was;
    state_.buttons_down[name] = false;
}

void InputMap::end_frame() {
    for (auto& kv : state_.buttons_down) kv.second = false;
    for (auto& kv : state_.buttons_up) kv.second = false;
}

VirtualControl& InputMap::control(const std::string& name) {
    for (auto& c : controls_) if (c.name == name) return c;
    VirtualControl c;
    c.name = name;
    controls_.push_back(std::move(c));
    return controls_.back();
}

void InputMap::apply_controls() {
    for (auto& c : controls_) {
        if (!c.visible) continue;
        switch (c.kind) {
            case ControlKind::Joystick: {
                math::Vec2 v = c.value;
                if (v.length() < c.dead_zone) v = math::Vec2{0, 0};
                else v = v.normalized() * ((v.length() - c.dead_zone) / (1.0f - c.dead_zone));
                set_axis(c.name + "_x", v.x);
                set_axis(c.name + "_y", v.y);
                if (c.name == "move") { set_axis("horizontal", v.x); set_axis("vertical", v.y); }
                break;
            }
            case ControlKind::DPad: {
                f32 x = 0, y = 0;
                if (std::fabs(c.value.x) > 0.5f) x = c.value.x > 0 ? 1.0f : -1.0f;
                if (std::fabs(c.value.y) > 0.5f) y = c.value.y > 0 ? 1.0f : -1.0f;
                set_axis("horizontal", x);
                set_axis("vertical", y);
                break;
            }
            case ControlKind::Button:
            case ControlKind::TapZone:
                if (c.pressed) press(c.name);
                break;
            case ControlKind::Slider:
                set_axis(c.name, c.value.x);
                break;
            case ControlKind::SwipeArea:
                set_axis(c.name + "_x", c.value.x);
                set_axis(c.name + "_y", c.value.y);
                break;
        }
    }
}

void InputMap::set_gamepad_axis(i32 pad, const std::string& axis, f32 v) { pad_axes_[pad][axis] = v; }
void InputMap::set_gamepad_button(i32 pad, const std::string& button, bool down) {
    bool was = gamepad_button(pad, button);
    pad_buttons_[pad][button] = down;
    if (down && !was) press(button);
    if (!down && was) release(button);
}
f32 InputMap::gamepad_axis(i32 pad, const std::string& axis) const {
    auto p = pad_axes_.find(pad);
    if (p == pad_axes_.end()) return 0.0f;
    auto a = p->second.find(axis);
    return a == p->second.end() ? 0.0f : a->second;
}
bool InputMap::gamepad_button(i32 pad, const std::string& button) const {
    auto p = pad_buttons_.find(pad);
    if (p == pad_buttons_.end()) return false;
    auto b = p->second.find(button);
    return b != p->second.end() && b->second;
}
bool InputMap::gamepad_connected(i32 pad) const {
    auto it = pad_connected_.find(pad);
    return it != pad_connected_.end() && it->second;
}
void InputMap::set_gamepad_connected(i32 pad, bool connected) { pad_connected_[pad] = connected; }

} // namespace prism::input
