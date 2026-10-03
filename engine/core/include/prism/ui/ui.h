// =====================================================================
//  PRISM ENGINE — ui/ui.h
//  The runtime UI layer. Layout and interaction live here; the actual
//  pixels are emitted as a flat list of draw commands so the same UI can
//  be drawn by the Vulkan path, the GLES fallback or the C# editor.
//
//  Layout is a flexbox subset (row / column / absolute, grow, shrink,
//  padding, margins, percentage sizes) because that is what a game HUD
//  and a 48 dpi tablet both need, and it is deterministic and testable.
//
//  Everything is offline: fonts come from the .prism pack, strings from
//  the bundled localisation tables, and there is no network access.
// =====================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::ui {

// ----------------------------------------------------------------- theme ---
/// The PRISM ENGINE brand palette, from brand/tokens/brand.json.
struct Theme {
    math::Color violet   = math::Color::prism_violet();      // #7C3AED
    math::Color cyan     = math::Color::spectrum_cyan();     // #00D9FF
    math::Color gold     = math::Color::prism_gold();        // #FFC93C
    math::Color magenta  = math::Color::sunset_magenta();    // #FF3D9A
    math::Color bg       = math::Color::dark_bg();           // #0A0A12

    math::Color panel          = math::Color{0.078f, 0.078f, 0.110f, 1.0f};
    math::Color panel_alt      = math::Color{0.110f, 0.110f, 0.155f, 1.0f};
    math::Color text           = math::Color{0.94f, 0.94f, 0.97f, 1.0f};
    math::Color text_dim       = math::Color{0.55f, 0.55f, 0.63f, 1.0f};
    math::Color outline        = math::Color{0.25f, 0.25f, 0.33f, 1.0f};
    math::Color danger         = math::Color{0.90f, 0.24f, 0.35f, 1.0f};
    math::Color success        = math::Color{0.20f, 0.78f, 0.50f, 1.0f};

    f32 base_font_size  = 16.0f;
    f32 corner_radius   = 6.0f;
    f32 control_height  = 36.0f;
    f32 spacing         = 8.0f;
    f32 touch_target    = 48.0f;    // Android accessibility minimum
    std::string font_face = "Inter";
    std::string mono_face = "JetBrains Mono";

    [[nodiscard]] static Theme light();
    [[nodiscard]] static Theme high_contrast();
    /// Scales spacing, fonts and touch targets for a phone/tablet/tv class.
    [[nodiscard]] Theme scaled(f32 factor) const;
};

// ------------------------------------------------------------------ rect ---
struct Box {
    f32 x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] bool contains(f32 px, f32 py) const { return px >= x && py >= y && px <= x + w && py <= y + h; }
    [[nodiscard]] Box inset(f32 d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    [[nodiscard]] Box translated(f32 dx, f32 dy) const { return {x + dx, y + dy, w, h}; }
    [[nodiscard]] static Box intersect(Box a, Box b);
    [[nodiscard]] bool valid() const { return w > 0 && h > 0; }
};

enum class Align : u8 { Start, Center, End, Stretch };
enum class Direction : u8 { Row, Column };
enum class Sizing : u8 { Fixed, Content, Fill, Percent };

struct EdgeInsets {
    f32 left = 0, top = 0, right = 0, bottom = 0;
    [[nodiscard]] f32 horizontal() const { return left + right; }
    [[nodiscard]] f32 vertical() const { return top + bottom; }
    static EdgeInsets all(f32 v) { return {v, v, v, v}; }
};

// ------------------------------------------------------------ layout node --
struct LayoutSpec {
    Direction direction = Direction::Column;
    Align main_axis = Align::Start;
    Align cross_axis = Align::Start;
    Sizing width = Sizing::Content;
    Sizing height = Sizing::Content;
    f32 width_value = 0;            // px for Fixed, 0..1 for Percent
    f32 height_value = 0;
    f32 grow = 0;                   // flex-grow share of leftover space
    f32 shrink = 1;
    f32 gap = 0;
    EdgeInsets padding;
    EdgeInsets margin;
    f32 min_width = 0, min_height = 0;
    f32 max_width = 1e9f, max_height = 1e9f;
    bool absolute = false;          // removed from the flex flow
    f32 absolute_x = 0, absolute_y = 0;
};

enum class WidgetKind : u8 {
    Panel, Label, Button, Image, Slider, Toggle, ScrollView, Spacer, TextField, ProgressBar,
};
const char* widget_kind_name(WidgetKind k);

struct DrawCmd {
    enum class Type : u8 { Rect, RoundedRect, Text, Image, ClipPush, ClipPop };
    Type type = Type::Rect;
    Box box;
    math::Color color = math::Color{1, 1, 1, 1};
    std::string text;
    std::string image;
    f32 font_size = 16.0f;
    f32 radius = 0;
    f32 depth = 0;                  // painter order
};

class Widget;
using WidgetPtr = std::shared_ptr<Widget>;

struct UiEvent {
    enum class Type : u8 { PointerDown, PointerUp, PointerMove, Click, ValueChanged, FocusGained, FocusLost };
    Type type = Type::PointerDown;
    f32 x = 0, y = 0;
    f32 value = 0;
    std::string text;
};

/// Base widget. Owns its layout spec, its computed box and its children.
class Widget : public std::enable_shared_from_this<Widget> {
public:
    explicit Widget(WidgetKind kind) : kind_(kind) {}
    virtual ~Widget() = default;

    WidgetPtr add(WidgetPtr child);
    void remove(const WidgetPtr& child);
    void clear_children();
    [[nodiscard]] const std::vector<WidgetPtr>& children() const { return children_; }
    [[nodiscard]] std::size_t child_count() const { return children_.size(); }
    [[nodiscard]] Widget* parent() const { return parent_; }

    /// Natural size this widget would like, before flex resolution.
    [[nodiscard]] virtual math::Vec2 measure(const math::Vec2& available, f32 scale) const;
    /// Emits draw commands into `out`.
    virtual void draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const;
    /// Returns true if the widget consumed the event.
    virtual bool on_event(const UiEvent& e);

    [[nodiscard]] WidgetKind kind() const { return kind_; }
    [[nodiscard]] const Box& box() const { return box_; }
    [[nodiscard]] const std::string& id() const { return id_; }
    void set_id(std::string s) { id_ = std::move(s); }
    [[nodiscard]] LayoutSpec& spec() { return spec_; }
    [[nodiscard]] const LayoutSpec& spec() const { return spec_; }
    [[nodiscard]] bool visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }
    [[nodiscard]] bool enabled() const { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }
    [[nodiscard]] bool focused() const { return focused_; }
    void set_focused(bool v) { focused_ = v; }

    /// Depth-first search for an id; used by tests and by PrismScript.
    [[nodiscard]] Widget* find(std::string_view id);
    [[nodiscard]] std::size_t node_count() const;
    /// Hit test in screen space; returns the deepest visible widget.
    [[nodiscard]] Widget* hit_test(f32 x, f32 y);

    std::function<void(const UiEvent&)> on_event_cb;

protected:
    friend class UiContext;
    void set_box(const Box& b) { box_ = b; }

    WidgetKind kind_;
    std::string id_;
    LayoutSpec spec_;
    Box box_;
    std::vector<WidgetPtr> children_;
    Widget* parent_ = nullptr;
    bool visible_ = true, enabled_ = true, focused_ = false;
};

// ------------------------------------------------------------- widgets ----
class Label : public Widget {
public:
    explicit Label(std::string text = "") : Widget(WidgetKind::Label), text_(std::move(text)) {}
    [[nodiscard]] const std::string& text() const { return text_; }
    void set_text(std::string t) { text_ = std::move(t); }
    void set_color(math::Color c) { color_ = c; has_color_ = true; }
    void set_font_size(f32 s) { font_size_ = s; }
    [[nodiscard]] math::Vec2 measure(const math::Vec2& available, f32 scale) const override;
    void draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const override;
private:
    std::string text_;
    math::Color color_ = math::Color{1, 1, 1, 1};
    bool has_color_ = false;
    f32 font_size_ = 0;            // 0 = theme default
};

class Panel : public Widget {
public:
    Panel() : Widget(WidgetKind::Panel) {}
    void set_background(math::Color c);
    void draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const override;
private:
    math::Color background_ = math::Color{0, 0, 0, 0};
    bool has_background_ = false;
};

class Button : public Widget {
public:
    explicit Button(std::string text = "") : Widget(WidgetKind::Button), label_(std::move(text)) {}
    [[nodiscard]] const std::string& text() const { return label_; }
    void set_text(std::string t) { label_ = std::move(t); }
    [[nodiscard]] u32 click_count() const { return clicks_; }
    [[nodiscard]] bool hovered() const { return hovered_; }
    [[nodiscard]] bool pressed() const { return pressed_; }
    [[nodiscard]] math::Vec2 measure(const math::Vec2& available, f32 scale) const override;
    void draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const override;
    bool on_event(const UiEvent& e) override;
    std::function<void()> on_click;
private:
    std::string label_;
    u32 clicks_ = 0;
    bool hovered_ = false, pressed_ = false;
};

class Slider : public Widget {
public:
    Slider() : Widget(WidgetKind::Slider) {}
    void set_range(f32 lo, f32 hi) { min_ = lo; max_ = hi; set_value(value_); }
    void set_value(f32 v);
    [[nodiscard]] f32 value() const { return value_; }
    [[nodiscard]] f32 min() const { return min_; }
    [[nodiscard]] f32 max() const { return max_; }
    [[nodiscard]] f32 normalised() const { return max_ <= min_ ? 0.0f : (value_ - min_) / (max_ - min_); }
    void draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const override;
    bool on_event(const UiEvent& e) override;
    std::function<void(f32)> on_change;
private:
    f32 min_ = 0, max_ = 1, value_ = 0;
    bool dragging_ = false;
};

class Toggle : public Widget {
public:
    explicit Toggle(std::string text = "") : Widget(WidgetKind::Toggle), label_(std::move(text)) {}
    void set_checked(bool c) { checked_ = c; }
    [[nodiscard]] bool checked() const { return checked_; }
    [[nodiscard]] const std::string& text() const { return label_; }
    void draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const override;
    bool on_event(const UiEvent& e) override;
    std::function<void(bool)> on_change;
private:
    std::string label_;
    bool checked_ = false;
};

class ProgressBar : public Widget {
public:
    ProgressBar() : Widget(WidgetKind::ProgressBar) {}
    void set_progress(f32 p) { progress_ = math::clampf(p, 0.0f, 1.0f); }
    [[nodiscard]] f32 progress() const { return progress_; }
    void draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const override;
private:
    f32 progress_ = 0;
};

class Spacer : public Widget {
public:
    Spacer() : Widget(WidgetKind::Spacer) { spec_.grow = 1.0f; }
    [[nodiscard]] math::Vec2 measure(const math::Vec2& available, f32 scale) const override;
};

// -------------------------------------------------------------- context ----
/// Owns the widget tree, runs layout, dispatches input and collects draws.
class UiContext {
public:
    struct Config {
        f32 width = 1920, height = 1080;
        f32 scale = 1.0f;                       // dpi scale factor
        f32 tap_slop = 12.0f;                   // px a touch may drift and still be a tap
        f32 long_press_ms = 500.0f;
    };

    UiContext();
    explicit UiContext(const Config& c);

    void set_root(WidgetPtr root) { root_ = std::move(root); dirty_ = true; }
    /// Call after mutating the tree so the next layout() re-flows it.
    void invalidate() { dirty_ = true; }
    [[nodiscard]] WidgetPtr root() const { return root_; }
    void set_theme(Theme t) { theme_ = std::move(t); }
    [[nodiscard]] const Theme& theme() const { return theme_; }
    [[nodiscard]] const Config& config() const { return cfg_; }
    void resize(f32 w, f32 h) { cfg_.width = w; cfg_.height = h; dirty_ = true; }

    /// Runs the layout pass. Idempotent: repeated calls with no changes are free.
    void layout();
    [[nodiscard]] bool layout_dirty() const { return dirty_; }
    [[nodiscard]] u32 layout_passes() const { return passes_; }

    /// Collects draw commands for the current frame.
    std::vector<DrawCmd> render() const;
    /// Feed a pointer event; returns the widget that consumed it.
    Widget* pointer(f32 x, f32 y, UiEvent::Type type);
    /// Convenience wrappers used by the touch-control layer.
    Widget* tap(f32 x, f32 y);
    Widget* drag(f32 from_x, f32 from_y, f32 to_x, f32 to_y);

    [[nodiscard]] Widget* focus() const { return focus_; }
    void focus(Widget* w);
    [[nodiscard]] Widget* find(std::string_view id) { return root_ ? root_->find(id) : nullptr; }

private:
    math::Vec2 measure_node(Widget* w, const math::Vec2& available) const;
    void arrange_node(Widget* w, const Box& box);
    void collect_draws(const Widget* w, std::vector<DrawCmd>& out, f32& depth) const;

    Config cfg_;
    Theme theme_;
    WidgetPtr root_;
    Widget* focus_ = nullptr;
    bool dirty_ = true;
    u32 passes_ = 0;
};

// --------------------------------------------------------- localisation ----
/// Offline string tables. Seven languages ship in the APK; adding one is a
/// data change, not a code change. Arabic and Hebrew are flagged RTL so the
/// layout pass can mirror.
class Localisation {
public:
    static const std::vector<std::string>& supported_languages();
    static bool is_supported(std::string_view code);
    static bool is_rtl(std::string_view code);

    void set(std::string_view lang, std::string_view key, std::string value);
    void set_language(std::string_view code);
    [[nodiscard]] const std::string& language() const { return language_; }
    /// Falls back to English, then to the key itself, so a missing string is
    /// visible in the UI rather than silently blank.
    [[nodiscard]] std::string get(std::string_view key) const;
    [[nodiscard]] std::string format(std::string_view key, const std::vector<std::string>& args) const;
    [[nodiscard]] std::size_t key_count(std::string_view lang) const;
    [[nodiscard]] std::vector<std::string> missing(std::string_view lang) const;
    void clear();

private:
    std::map<std::string, std::map<std::string, std::string>> tables_;
    std::string language_ = "en";
};

/// Installs the engine's own UI strings for all seven shipped languages.
void install_builtin_strings(Localisation& l);

// --------------------------------------------------- touch control binding --
/// Maps an on-screen widget to a game action, matching the JSON templates in
/// templates/touch-controls/. Dead-zone and invert follow the same semantics
/// as input::VirtualControl so a template behaves identically on screen and
/// on a gamepad.
struct TouchBinding {
    std::string widget_id;
    std::string action;
    enum class Kind : u8 { Button, Axis, Stick } kind = Kind::Button;
    f32 dead_zone = 0.15f;
    bool invert = false;
    f32 sensitivity = 1.0f;
    bool haptic = true;
};

class TouchControls {
public:
    void bind(TouchBinding b) { bindings_.push_back(std::move(b)); }
    void clear() { bindings_.clear(); }
    [[nodiscard]] const std::vector<TouchBinding>& bindings() const { return bindings_; }
    [[nodiscard]] const TouchBinding* find(std::string_view action) const;

    /// Applies a pointer event to the matching binding and returns the
    /// resulting axis value (-1..1) or button state (0/1).
    f32 apply(const TouchBinding& b, f32 dx, f32 dy) const;
    /// Emits the (action, value) pairs for one frame of pointer state.
    std::vector<std::pair<std::string, f32>> poll(UiContext& ui,
                                                  const std::map<std::string, std::pair<f32, f32>>& pointers) const;
private:
    std::vector<TouchBinding> bindings_;
};

} // namespace prism::ui
