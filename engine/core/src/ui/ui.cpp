// =====================================================================
//  PRISM ENGINE — ui/ui.cpp
//  Flexbox-subset layout, widget drawing and offline localisation.
// =====================================================================
#include "prism/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace prism::ui {

namespace {
inline f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

// ================================================================== theme ==
Theme Theme::light() {
    Theme t;
    t.bg = math::Color{0.96f, 0.96f, 0.98f, 1.0f};
    t.panel = math::Color{1.0f, 1.0f, 1.0f, 1.0f};
    t.panel_alt = math::Color{0.93f, 0.93f, 0.96f, 1.0f};
    t.text = math::Color{0.08f, 0.08f, 0.12f, 1.0f};
    t.text_dim = math::Color{0.40f, 0.40f, 0.48f, 1.0f};
    t.outline = math::Color{0.80f, 0.80f, 0.86f, 1.0f};
    return t;
}

Theme Theme::high_contrast() {
    Theme t;
    t.bg = math::Color{0, 0, 0, 1};
    t.panel = math::Color{0, 0, 0, 1};
    t.panel_alt = math::Color{0.1f, 0.1f, 0.1f, 1};
    t.text = math::Color{1, 1, 1, 1};
    t.text_dim = math::Color{1, 1, 0, 1};
    t.outline = math::Color{1, 1, 1, 1};
    t.violet = math::Color{0.75f, 0.5f, 1.0f, 1.0f};
    t.cyan = math::Color{0.4f, 1.0f, 1.0f, 1.0f};
    return t;
}

Theme Theme::scaled(f32 factor) const {
    Theme t = *this;
    const f32 f = factor > 0.0f ? factor : 1.0f;
    t.base_font_size *= f;
    t.corner_radius *= f;
    t.control_height *= f;
    t.spacing *= f;
    // Never shrink a touch target below the 48 dp accessibility minimum.
    t.touch_target = std::max(48.0f, t.touch_target * f);
    return t;
}

// =================================================================== box ===
Box Box::intersect(Box a, Box b) {
    const f32 x0 = std::max(a.x, b.x);
    const f32 y0 = std::max(a.y, b.y);
    const f32 x1 = std::min(a.x + a.w, b.x + b.w);
    const f32 y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return {0, 0, 0, 0};
    return {x0, y0, x1 - x0, y1 - y0};
}

const char* widget_kind_name(WidgetKind k) {
    switch (k) {
        case WidgetKind::Panel: return "panel";
        case WidgetKind::Label: return "label";
        case WidgetKind::Button: return "button";
        case WidgetKind::Image: return "image";
        case WidgetKind::Slider: return "slider";
        case WidgetKind::Toggle: return "toggle";
        case WidgetKind::ScrollView: return "scroll_view";
        case WidgetKind::Spacer: return "spacer";
        case WidgetKind::TextField: return "text_field";
        case WidgetKind::ProgressBar: return "progress_bar";
    }
    return "unknown";
}

// ================================================================ widget ===
WidgetPtr Widget::add(WidgetPtr child) {
    if (!child) return nullptr;
    child->parent_ = this;
    children_.push_back(child);
    return child;
}

void Widget::remove(const WidgetPtr& child) {
    children_.erase(std::remove(children_.begin(), children_.end(), child), children_.end());
}

void Widget::clear_children() {
    for (auto& c : children_) c->parent_ = nullptr;
    children_.clear();
}

math::Vec2 Widget::measure(const math::Vec2& available, f32 scale) const {
    (void)available; (void)scale;
    return math::Vec2(0, 0);
}

void Widget::draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const {
    (void)out; (void)theme; (void)scale;
}

bool Widget::on_event(const UiEvent& e) {
    if (on_event_cb) { on_event_cb(e); return true; }
    return false;
}

Widget* Widget::find(std::string_view id) {
    if (id_ == id) return this;
    for (auto& c : children_) if (Widget* hit = c->find(id)) return hit;
    return nullptr;
}

std::size_t Widget::node_count() const {
    std::size_t n = 1;
    for (const auto& c : children_) n += c->node_count();
    return n;
}

Widget* Widget::hit_test(f32 x, f32 y) {
    if (!visible_ || !box_.contains(x, y)) return nullptr;
    // Deepest first: walk children back to front.
    for (auto it = children_.rbegin(); it != children_.rend(); ++it)
        if (Widget* hit = (*it)->hit_test(x, y)) return hit;
    return this;
}

// ================================================================ widgets ==
math::Vec2 Label::measure(const math::Vec2& available, f32 scale) const {
    (void)available;
    // Offline approximation: Inter's average advance is ~0.5 em. A real run
    // measures with the packed font's advance table; this keeps layout
    // deterministic and dependency-free for the host test suite.
    const f32 size = (font_size_ > 0 ? font_size_ : 16.0f) * scale;
    return math::Vec2(static_cast<f32>(text_.size()) * size * 0.5f, size * 1.25f);
}

void Label::draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const {
    DrawCmd c;
    c.type = DrawCmd::Type::Text;
    c.box = box_;
    c.text = text_;
    c.color = has_color_ ? color_ : theme.text;
    c.font_size = (font_size_ > 0 ? font_size_ : theme.base_font_size) * scale;
    out.push_back(std::move(c));
}

void Panel::draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const {
    DrawCmd c;
    c.type = DrawCmd::Type::RoundedRect;
    c.box = box_;
    c.color = has_background_ ? background_ : theme.panel;
    c.radius = theme.corner_radius * scale;
    out.push_back(std::move(c));
}

void Panel::set_background(math::Color c) { background_ = c; has_background_ = true; }

math::Vec2 Button::measure(const math::Vec2& available, f32 scale) const {
    (void)available;
    const f32 pad = 16.0f * scale;
    const f32 text_w = static_cast<f32>(label_.size()) * 16.0f * scale * 0.5f;
    return math::Vec2(text_w + pad * 2, 36.0f * scale);
}

void Button::draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const {
    DrawCmd bg;
    bg.type = DrawCmd::Type::RoundedRect;
    bg.box = box_;
    bg.color = !enabled_ ? theme.panel_alt : (pressed_ ? theme.magenta : (hovered_ ? theme.cyan : theme.violet));
    bg.radius = theme.corner_radius * scale;
    out.push_back(std::move(bg));

    DrawCmd label;
    label.type = DrawCmd::Type::Text;
    label.box = box_;
    label.text = label_;
    label.color = enabled_ ? theme.text : theme.text_dim;
    label.font_size = theme.base_font_size * scale;
    out.push_back(std::move(label));
}

bool Button::on_event(const UiEvent& e) {
    if (!enabled_) return false;
    switch (e.type) {
        case UiEvent::Type::PointerDown:
            pressed_ = true;
            return true;
        case UiEvent::Type::PointerUp: {
            const bool was = pressed_;
            pressed_ = false;
            if (was) {
                ++clicks_;
                if (on_click) on_click();
            }
            return was;
        }
        case UiEvent::Type::Click:
            ++clicks_;
            if (on_click) on_click();
            return true;
        default:
            return Widget::on_event(e);
    }
}

void Slider::set_value(f32 v) { value_ = clampf(v, min_, max_); }

void Slider::draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const {
    const f32 track_h = std::max(4.0f * scale, box_.h * 0.2f);
    const f32 cy = box_.y + box_.h * 0.5f;
    DrawCmd track;
    track.type = DrawCmd::Type::RoundedRect;
    track.box = {box_.x, cy - track_h * 0.5f, box_.w, track_h};
    track.color = theme.panel_alt;
    track.radius = track_h * 0.5f;
    out.push_back(std::move(track));

    DrawCmd fill;
    fill.type = DrawCmd::Type::RoundedRect;
    fill.box = {box_.x, cy - track_h * 0.5f, box_.w * normalised(), track_h};
    fill.color = theme.cyan;
    fill.radius = track_h * 0.5f;
    out.push_back(std::move(fill));

    DrawCmd knob;
    knob.type = DrawCmd::Type::RoundedRect;
    const f32 r = theme.touch_target * scale * 0.35f;
    knob.box = {box_.x + box_.w * normalised() - r, cy - r, r * 2, r * 2};
    knob.color = theme.violet;
    knob.radius = r;
    out.push_back(std::move(knob));
}

bool Slider::on_event(const UiEvent& e) {
    if (!enabled_) return false;
    if (e.type == UiEvent::Type::PointerDown) { dragging_ = true; }
    else if (e.type == UiEvent::Type::PointerUp) { dragging_ = false; return true; }
    else if (e.type != UiEvent::Type::PointerMove) return Widget::on_event(e);
    if (!dragging_ && e.type == UiEvent::Type::PointerMove) return false;
    if (box_.w <= 0.0f) return true;
    const f32 t = clampf((e.x - box_.x) / box_.w, 0.0f, 1.0f);
    const f32 next = min_ + t * (max_ - min_);
    if (std::fabs(next - value_) > 1e-6f) {
        value_ = next;
        if (on_change) on_change(value_);
    }
    return true;
}

void Toggle::draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const {
    const f32 size = theme.touch_target * scale * 0.5f;
    const Box swatch{box_.x, box_.y + (box_.h - size) * 0.5f, size, size};
    const f32 swatch_radius = theme.corner_radius * scale * 0.5f;

    DrawCmd box_cmd;
    box_cmd.type = DrawCmd::Type::RoundedRect;
    box_cmd.box = swatch;
    box_cmd.color = checked_ ? theme.success : theme.panel_alt;
    box_cmd.radius = swatch_radius;
    out.push_back(std::move(box_cmd));

    DrawCmd outline;
    outline.type = DrawCmd::Type::RoundedRect;
    outline.box = swatch;
    outline.color = theme.outline;
    outline.radius = swatch_radius;
    out.push_back(std::move(outline));

    DrawCmd label;
    label.type = DrawCmd::Type::Text;
    label.box = {box_.x + size + theme.spacing * scale, box_.y, box_.w - size, box_.h};
    label.text = label_;
    label.color = theme.text;
    label.font_size = theme.base_font_size * scale;
    out.push_back(std::move(label));
}

bool Toggle::on_event(const UiEvent& e) {
    if (!enabled_) return false;
    if (e.type != UiEvent::Type::PointerUp && e.type != UiEvent::Type::Click) return Widget::on_event(e);
    checked_ = !checked_;
    if (on_change) on_change(checked_);
    return true;
}

void ProgressBar::draw(std::vector<DrawCmd>& out, const Theme& theme, f32 scale) const {
    DrawCmd bg;
    bg.type = DrawCmd::Type::RoundedRect;
    bg.box = box_;
    bg.color = theme.panel_alt;
    bg.radius = theme.corner_radius * scale;
    out.push_back(std::move(bg));

    DrawCmd fill;
    fill.type = DrawCmd::Type::RoundedRect;
    fill.box = {box_.x, box_.y, box_.w * progress_, box_.h};
    fill.color = progress_ >= 1.0f ? theme.success : theme.gold;
    fill.radius = bg.radius;
    out.push_back(std::move(fill));
}

math::Vec2 Spacer::measure(const math::Vec2& available, f32 scale) const {
    (void)available; (void)scale;
    return math::Vec2(0, 0);            // all of its size comes from flex-grow
}

// ============================================================== context ====
UiContext::UiContext() = default;
UiContext::UiContext(const Config& c) : cfg_(c) {}

math::Vec2 UiContext::measure_node(Widget* w, const math::Vec2& available) const {
    const LayoutSpec& s = w->spec();
    const f32 scale = cfg_.scale;
    math::Vec2 content = w->measure(available, scale);

    // Children in the flex flow contribute to the content size.
    f32 main = 0, cross = 0;
    std::size_t flowing = 0;
    for (auto& child : w->children_) {
        if (child->spec().absolute || !child->visible()) continue;
        const math::Vec2 inner_avail(
            available.x - s.padding.horizontal() - child->spec().margin.horizontal(),
            available.y - s.padding.vertical() - child->spec().margin.vertical());
        const math::Vec2 cm = measure_node(child.get(), inner_avail);
        if (s.direction == Direction::Row) { main += cm.x; cross = std::max(cross, cm.y); }
        else { main += cm.y; cross = std::max(cross, cm.x); }
        ++flowing;
    }
    if (flowing > 1) main += s.gap * static_cast<f32>(flowing - 1);
    content.x = std::max(content.x, s.direction == Direction::Row ? main : cross);
    content.y = std::max(content.y, s.direction == Direction::Row ? cross : main);
    content.x += s.padding.horizontal();
    content.y += s.padding.vertical();

    // Resolve the explicit sizing modes.
    const f32 avail_w = std::max(0.0f, available.x);
    const f32 avail_h = std::max(0.0f, available.y);
    f32 w_out = content.x, h_out = content.y;
    switch (s.width) {
        case Sizing::Fixed: w_out = s.width_value; break;
        case Sizing::Percent: w_out = avail_w * s.width_value; break;
        case Sizing::Fill: w_out = avail_w; break;
        case Sizing::Content: break;
    }
    switch (s.height) {
        case Sizing::Fixed: h_out = s.height_value; break;
        case Sizing::Percent: h_out = avail_h * s.height_value; break;
        case Sizing::Fill: h_out = avail_h; break;
        case Sizing::Content: break;
    }
    w_out = clampf(w_out, s.min_width, s.max_width);
    h_out = clampf(h_out, s.min_height, s.max_height);
    return math::Vec2(w_out, h_out);
}

void UiContext::arrange_node(Widget* w, const Box& box) {
    w->set_box(box);
    const LayoutSpec& s = w->spec();
    const Box inner = Box{box.x + s.padding.left, box.y + s.padding.top,
                          std::max(0.0f, box.w - s.padding.horizontal()),
                          std::max(0.0f, box.h - s.padding.vertical())};

    struct Child { Widget* w; math::Vec2 size; };
    std::vector<Child> flowing;
    f32 total_main = 0, total_grow = 0;
    for (auto& child : w->children_) {
        if (child->spec().absolute || !child->visible()) continue;
        const math::Vec2 inner_avail(inner.w - child->spec().margin.horizontal(),
                                     inner.h - child->spec().margin.vertical());
        math::Vec2 cm = measure_node(child.get(), inner_avail);
        const f32 main_size = s.direction == Direction::Row ? cm.x : cm.y;
        total_main += main_size + (s.direction == Direction::Row ? child->spec().margin.horizontal()
                                                                 : child->spec().margin.vertical());
        total_grow += child->spec().grow;
        flowing.push_back({child.get(), cm});
    }
    const std::size_t n = flowing.size();
    if (n > 1) total_main += s.gap * static_cast<f32>(n - 1);

    const f32 inner_main = s.direction == Direction::Row ? inner.w : inner.h;
    const f32 inner_cross = s.direction == Direction::Row ? inner.h : inner.w;
    const f32 leftover = inner_main - total_main;

    // Distribute leftover space by flex-grow, or compress by flex-shrink.
    f32 cursor = 0;
    if (leftover > 0.0f && total_grow <= 0.0f) {
        if (s.main_axis == Align::Center) cursor = leftover * 0.5f;
        else if (s.main_axis == Align::End) cursor = leftover;
    }

    for (std::size_t i = 0; i < n; ++i) {
        auto& c = flowing[i];
        const LayoutSpec& cs = c.w->spec();
        f32 main_size = s.direction == Direction::Row ? c.size.x : c.size.y;
        f32 cross_size = s.direction == Direction::Row ? c.size.y : c.size.x;
        if (leftover > 0.0f && total_grow > 0.0f) main_size += leftover * (cs.grow / total_grow);
        else if (leftover < 0.0f && total_main > 0.0f && cs.shrink > 0.0f) {
            // Compress proportionally, but never below the child's minimum.
            const f32 floor_size = s.direction == Direction::Row ? cs.min_width : cs.min_height;
            main_size = std::max(floor_size, main_size * (inner_main / total_main));
        }
        if (s.cross_axis == Align::Stretch) {
            cross_size = inner_cross - (s.direction == Direction::Row ? cs.margin.vertical() : cs.margin.horizontal());
        }

        f32 cross_offset = 0;
        if (s.cross_axis == Align::Center) cross_offset = (inner_cross - cross_size) * 0.5f;
        else if (s.cross_axis == Align::End) cross_offset = inner_cross - cross_size;

        Box child_box;
        if (s.direction == Direction::Row) {
            child_box = Box{inner.x + cursor + cs.margin.left,
                            inner.y + cross_offset + cs.margin.top, main_size, cross_size};
            cursor += main_size + cs.margin.horizontal();
        } else {
            child_box = Box{inner.x + cross_offset + cs.margin.left,
                            inner.y + cursor + cs.margin.top, cross_size, main_size};
            cursor += main_size + cs.margin.vertical();
        }
        if (i + 1 < n) cursor += s.gap;
        arrange_node(c.w, child_box);
    }

    for (auto& child : w->children_) {
        if (!child->spec().absolute || !child->visible()) continue;
        const LayoutSpec& cs = child->spec();
        const math::Vec2 cm = measure_node(child.get(), math::Vec2(inner.w, inner.h));
        arrange_node(child.get(), Box{inner.x + cs.absolute_x, inner.y + cs.absolute_y, cm.x, cm.y});
    }
}

void UiContext::layout() {
    if (!dirty_ || !root_) return;
    const math::Vec2 root_size = measure_node(root_.get(), math::Vec2(cfg_.width, cfg_.height));
    (void)root_size;
    arrange_node(root_.get(), Box{0, 0, cfg_.width, cfg_.height});
    ++passes_;
    dirty_ = false;
}

void UiContext::collect_draws(const Widget* w, std::vector<DrawCmd>& out, f32& depth) const {
    if (!w->visible()) return;
    std::vector<DrawCmd> mine;
    w->draw(mine, theme_, cfg_.scale);
    for (auto& c : mine) { c.depth = depth++; out.push_back(std::move(c)); }
    for (const auto& child : w->children_) collect_draws(child.get(), out, depth);
}

std::vector<DrawCmd> UiContext::render() const {
    std::vector<DrawCmd> out;
    // The clear is unconditional: an empty scene still has to blank the
    // previous frame's pixels.
    DrawCmd bg;
    bg.type = DrawCmd::Type::Rect;
    bg.box = {0, 0, cfg_.width, cfg_.height};
    bg.color = theme_.bg;
    bg.depth = 0;
    out.push_back(std::move(bg));
    if (!root_) return out;
    f32 depth = 1;
    collect_draws(root_.get(), out, depth);
    return out;
}

Widget* UiContext::pointer(f32 x, f32 y, UiEvent::Type type) {
    if (!root_) return nullptr;
    layout();
    Widget* target = root_->hit_test(x, y);
    if (!target) return nullptr;
    UiEvent e;
    e.type = type;
    e.x = x;
    e.y = y;
    Widget* handled = target->on_event(e) ? target : nullptr;
    if (type == UiEvent::Type::PointerDown) focus(target);
    return handled;
}

Widget* UiContext::tap(f32 x, f32 y) {
    // Exactly two events. Widgets commit on PointerUp, so a synthetic Click
    // here would double-fire every callback. Click stays available for
    // programmatic activation (keyboard, gamepad, accessibility actions).
    pointer(x, y, UiEvent::Type::PointerDown);
    return pointer(x, y, UiEvent::Type::PointerUp);
}

Widget* UiContext::drag(f32 from_x, f32 from_y, f32 to_x, f32 to_y) {
    Widget* w = pointer(from_x, from_y, UiEvent::Type::PointerDown);
    if (!w) return nullptr;
    UiEvent move;
    move.type = UiEvent::Type::PointerMove;
    move.x = to_x;
    move.y = to_y;
    w->on_event(move);
    pointer(to_x, to_y, UiEvent::Type::PointerUp);
    return w;
}

void UiContext::focus(Widget* w) {
    if (focus_ == w) return;
    if (focus_) {
        focus_->set_focused(false);
        UiEvent e;
        e.type = UiEvent::Type::FocusLost;
        focus_->on_event(e);
    }
    focus_ = w;
    if (focus_) {
        focus_->set_focused(true);
        UiEvent e;
        e.type = UiEvent::Type::FocusGained;
        focus_->on_event(e);
    }
}

// ========================================================== localisation ===
const std::vector<std::string>& Localisation::supported_languages() {
    static const std::vector<std::string> langs = {"en", "am", "es", "zh", "fr", "ar", "hi"};
    return langs;
}

bool Localisation::is_supported(std::string_view code) {
    const auto& l = supported_languages();
    return std::find(l.begin(), l.end(), std::string(code)) != l.end();
}

bool Localisation::is_rtl(std::string_view code) { return code == "ar" || code == "he" || code == "fa"; }

void Localisation::set(std::string_view lang, std::string_view key, std::string value) {
    tables_[std::string(lang)][std::string(key)] = std::move(value);
}

void Localisation::set_language(std::string_view code) {
    language_ = is_supported(code) ? std::string(code) : "en";
}

std::string Localisation::get(std::string_view key) const {
    auto lang_it = tables_.find(language_);
    if (lang_it != tables_.end()) {
        auto it = lang_it->second.find(std::string(key));
        if (it != lang_it->second.end()) return it->second;
    }
    auto en_it = tables_.find("en");
    if (en_it != tables_.end()) {
        auto it = en_it->second.find(std::string(key));
        if (it != en_it->second.end()) return it->second;
    }
    return std::string(key);          // a missing string must be visible, not blank
}

std::string Localisation::format(std::string_view key, const std::vector<std::string>& args) const {
    std::string s = get(key);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string token = "{" + std::to_string(i) + "}";
        std::size_t pos = 0;
        while ((pos = s.find(token, pos)) != std::string::npos) {
            s.replace(pos, token.size(), args[i]);
            pos += args[i].size();
        }
    }
    return s;
}

std::size_t Localisation::key_count(std::string_view lang) const {
    auto it = tables_.find(std::string(lang));
    return it == tables_.end() ? 0 : it->second.size();
}

std::vector<std::string> Localisation::missing(std::string_view lang) const {
    std::vector<std::string> out;
    auto en = tables_.find("en");
    if (en == tables_.end()) return out;
    auto target = tables_.find(std::string(lang));
    for (const auto& kv : en->second) {
        if (target == tables_.end() || target->second.find(kv.first) == target->second.end())
            out.push_back(kv.first);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Localisation::clear() { tables_.clear(); language_ = "en"; }

void install_builtin_strings(Localisation& l) {
    struct Row { const char* key; const char* en; const char* am; const char* es;
                 const char* zh; const char* fr; const char* ar; const char* hi; };
    static const Row rows[] = {
        {"play",         "Play",         "አጫውት",        "Jugar",        "开始游戏",  "Jouer",        "اللعب",         "खेलें"},
        {"pause",        "Pause",        "ለአፍታ አቁም",   "Pausa",        "暂停",      "Pause",        "إيقاف مؤقت",    "रोकें"},
        {"resume",       "Resume",       "ቀጥል",         "Reanudar",     "继续",      "Reprendre",    "استئناف",       "जारी रखें"},
        {"restart",      "Restart",      "እንደገና ጀምር",  "Reiniciar",    "重新开始",  "Recommencer",  "إعادة التشغيل", "फिर से शुरू करें"},
        {"settings",     "Settings",     "ቅንብሮች",       "Ajustes",      "设置",      "Paramètres",   "الإعدادات",     "सेटिंग्स"},
        {"quit",         "Quit",         "ውጣ",          "Salir",        "退出",      "Quitter",      "خروج",          "बाहर निकलें"},
        {"back",         "Back",         "ተመለስ",        "Atrás",        "返回",      "Retour",       "رجوع",          "वापस"},
        {"next",         "Next",         "ቀጣይ",         "Siguiente",    "下一个",    "Suivant",      "التالي",        "आगे"},
        {"score",        "Score",        "ነጥብ",         "Puntuación",   "得分",      "Score",        "النتيجة",       "स्कोर"},
        {"high_score",   "High Score",   "ከፍተኛ ነጥብ",    "Récord",       "最高分",    "Meilleur score", "أعلى نتيجة",  "उच्चतम स्कोर"},
        {"level",        "Level",        "ደረጃ",         "Nivel",        "关卡",      "Niveau",       "المستوى",       "स्तर"},
        {"player",       "Player",       "ተጫዋች",        "Jugador",      "玩家",      "Joueur",       "اللاعب",        "खिलाड़ी"},
        {"loading",      "Loading",      "በመጫን ላይ",     "Cargando",     "加载中",    "Chargement",   "جار التحميل",   "लोड हो रहा है"},
        {"on",           "On",           "በርቷል",        "Activado",     "开",        "Activé",       "مفعّل",         "चालू"},
        {"off",          "Off",          "ጠፍቷል",        "Desactivado",  "关",        "Désactivé",    "معطّل",         "बंद"},
        {"tap_to_start", "Tap to start", "ለመጀመር ንካ",    "Toca para empezar", "点击开始", "Appuyez pour commencer", "انقر للبدء", "शुरू करने के लिए टैप करें"},
        {"offline_ready", "Works offline", "ከመስመር ውጪ ይሰራል", "Funciona sin conexión", "离线可用", "Fonctionne hors ligne", "يعمل دون اتصال", "ऑफ़लाइन काम करता है"},
        {"welcome",      "Welcome, {0}!", "እንኳን ደህና መጡ፣ {0}!", "¡Bienvenido, {0}!", "欢迎，{0}！", "Bienvenue, {0} !", "مرحبًا، {0}!", "स्वागत है, {0}!"},
        {"lives_left",   "{0} lives left", "{0} ሕይወቶች ቀርተዋል", "Quedan {0} vidas", "剩余 {0} 条命", "{0} vies restantes", "بقي {0} أرواح", "{0} जीवन शेष"},
        {"language",     "Language",     "ቋንቋ",         "Idioma",       "语言",      "Langue",       "اللغة",         "भाषा"},
    };
    for (const auto& r : rows) {
        l.set("en", r.key, r.en);
        l.set("am", r.key, r.am);
        l.set("es", r.key, r.es);
        l.set("zh", r.key, r.zh);
        l.set("fr", r.key, r.fr);
        l.set("ar", r.key, r.ar);
        l.set("hi", r.key, r.hi);
    }
}

// ========================================================== touch binding ==
const TouchBinding* TouchControls::find(std::string_view action) const {
    for (const auto& b : bindings_) if (b.action == action) return &b;
    return nullptr;
}

f32 TouchControls::apply(const TouchBinding& b, f32 dx, f32 dy) const {
    f32 v = 0;
    if (b.kind == TouchBinding::Kind::Button) {
        v = (std::fabs(dx) > 0.0f || std::fabs(dy) > 0.0f) ? 1.0f : 0.0f;
    } else {
        v = b.kind == TouchBinding::Kind::Axis ? dx : dy;
        v *= b.sensitivity;
        if (b.invert) v = -v;
        if (std::fabs(v) < b.dead_zone) return 0.0f;
        // Rescale so the value still reaches exactly +-1 at full deflection.
        const f32 sign = v < 0 ? -1.0f : 1.0f;
        v = sign * (std::fabs(v) - b.dead_zone) / std::max(1e-6f, 1.0f - b.dead_zone);
        v = clampf(v, -1.0f, 1.0f);
    }
    return v;
}

std::vector<std::pair<std::string, f32>>
TouchControls::poll(UiContext& ui, const std::map<std::string, std::pair<f32, f32>>& pointers) const {
    std::vector<std::pair<std::string, f32>> out;
    for (const auto& b : bindings_) {
        Widget* w = ui.find(b.widget_id);
        const auto it = pointers.find(b.widget_id);
        f32 value = 0;
        if (w && it != pointers.end()) value = apply(b, it->second.first, it->second.second);
        out.emplace_back(b.action, value);
    }
    return out;
}

} // namespace prism::ui
