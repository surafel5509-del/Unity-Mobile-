// =====================================================================
//  PRISM ENGINE — tests/test_ui.cpp
//  Layout, widgets, theming, localisation and touch-control binding.
// =====================================================================
#include "prism_test.h"
#include "prism/ui/ui.h"

using namespace prism;
using namespace prism::ui;

namespace {
WidgetPtr panel(Direction dir, f32 gap = 0, EdgeInsets pad = {}) {
    auto p = std::make_shared<Panel>();
    p->spec().direction = dir;
    p->spec().gap = gap;
    p->spec().padding = pad;
    return p;
}
WidgetPtr fixed(f32 w, f32 h) {
    auto p = std::make_shared<Panel>();
    p->spec().width = Sizing::Fixed;  p->spec().width_value = w;
    p->spec().height = Sizing::Fixed; p->spec().height_value = h;
    return p;
}
WidgetPtr fill_height(f32 grow) {
    auto p = std::make_shared<Panel>();
    p->spec().width = Sizing::Fill;
    p->spec().height = Sizing::Content;
    p->spec().grow = grow;
    return p;
}
std::size_t count_type(const std::vector<DrawCmd>& cmds, DrawCmd::Type t) {
    std::size_t n = 0;
    for (const auto& c : cmds) if (c.type == t) ++n;
    return n;
}
} // namespace

// ================================================================== theme ==
PRISM_TEST(ui_theme_carries_the_brand_palette) {
    Theme t;
    const math::Color violet = math::Color::from_hex(0x7C3AED);
    PRISM_CHECK_NEAR(t.violet.r, violet.r, 1e-6f);
    PRISM_CHECK_NEAR(t.violet.g, violet.g, 1e-6f);
    PRISM_CHECK_NEAR(t.violet.b, violet.b, 1e-6f);
    PRISM_CHECK_NEAR(t.cyan.g, 0xD9 / 255.0f, 1e-6f);
    PRISM_CHECK_NEAR(t.bg.r, 0x0A / 255.0f, 1e-6f);
    PRISM_CHECK_STR(t.font_face, "Inter");
    PRISM_CHECK_STR(t.mono_face, "JetBrains Mono");

    // A 0.5x scale must not shrink touch targets below the 48 dp minimum.
    const Theme small_t = t.scaled(0.5f);
    PRISM_CHECK_NEAR(small_t.touch_target, 48.0f, 1e-6f);
    PRISM_CHECK_NEAR(small_t.base_font_size, t.base_font_size * 0.5f, 1e-6f);
    const Theme big = t.scaled(2.0f);
    PRISM_CHECK_NEAR(big.touch_target, 96.0f, 1e-6f);
    PRISM_CHECK_NEAR(big.spacing, t.spacing * 2.0f, 1e-6f);
    // A zero or negative factor must not produce a degenerate theme.
    PRISM_CHECK(Theme().scaled(0.0f).base_font_size > 0.0f);

    const Theme light = Theme::light();
    PRISM_CHECK(light.bg.r > 0.9f);
    PRISM_CHECK(light.text.r < 0.2f);
    const Theme hc = Theme::high_contrast();
    PRISM_CHECK_NEAR(hc.text.r, 1.0f, 1e-6f);
    PRISM_CHECK_NEAR(hc.bg.r, 0.0f, 1e-6f);
}

// ================================================================= layout ==
PRISM_TEST(ui_layout_column_distributes_leftover_space_by_grow) {
    UiContext::Config cfg;
    cfg.width = 800; cfg.height = 600;
    UiContext ui(cfg);

    auto root = panel(Direction::Column, 10, EdgeInsets::all(10));
    auto header = fixed(780, 50);
    header->spec().width = Sizing::Fill;
    auto body = fill_height(1.0f);
    auto footer = fixed(780, 40);
    footer->spec().width = Sizing::Fill;
    root->add(header); root->add(body); root->add(footer);
    ui.set_root(root);

    ui.layout();
    PRISM_CHECK_EQ(ui.layout_passes(), 1u);
    PRISM_CHECK(!ui.layout_dirty());
    // A second pass with no changes must be a no-op.
    ui.layout();
    PRISM_CHECK_EQ(ui.layout_passes(), 1u);

    PRISM_CHECK_NEAR(header->box().x, 10.0f, 1e-4f);
    PRISM_CHECK_NEAR(header->box().y, 10.0f, 1e-4f);
    PRISM_CHECK_NEAR(header->box().w, 780.0f, 1e-4f);
    PRISM_CHECK_NEAR(header->box().h, 50.0f, 1e-4f);

    PRISM_CHECK_NEAR(body->box().y, 70.0f, 1e-4f);      // 10 + 50 + 10 gap
    PRISM_CHECK_NEAR(body->box().h, 470.0f, 1e-4f);     // 580 - 50 - 40 - 20 gaps
    PRISM_CHECK_NEAR(footer->box().y, 550.0f, 1e-4f);
    PRISM_CHECK_NEAR(footer->box().y + footer->box().h, 590.0f, 1e-4f);   // 600 - padding
}

PRISM_TEST(ui_layout_row_and_flex_grow_shares) {
    UiContext::Config cfg;
    cfg.width = 300; cfg.height = 100;
    UiContext ui(cfg);

    auto root = panel(Direction::Row);
    auto a = fixed(100, 100);
    auto b = std::make_shared<Panel>();
    b->spec().grow = 1.0f;
    b->spec().height = Sizing::Fill;
    auto c = fixed(100, 100);
    root->add(a); root->add(b); root->add(c);
    ui.set_root(root);
    ui.layout();

    PRISM_CHECK_NEAR(a->box().x, 0.0f, 1e-4f);
    PRISM_CHECK_NEAR(b->box().x, 100.0f, 1e-4f);
    PRISM_CHECK_NEAR(b->box().w, 100.0f, 1e-4f);        // 300 - 100 - 100
    PRISM_CHECK_NEAR(c->box().x, 200.0f, 1e-4f);
    PRISM_CHECK_NEAR(c->box().w, 100.0f, 1e-4f);
    PRISM_CHECK_NEAR(b->box().h, 100.0f, 1e-4f);

    // Two growers split the leftover evenly.
    UiContext ui2(cfg);
    auto root2 = panel(Direction::Row);
    auto g1 = std::make_shared<Panel>(); g1->spec().grow = 1; g1->spec().height = Sizing::Fill;
    auto g2 = std::make_shared<Panel>(); g2->spec().grow = 3; g2->spec().height = Sizing::Fill;
    root2->add(g1); root2->add(g2);
    ui2.set_root(root2);
    ui2.layout();
    PRISM_CHECK_NEAR(g1->box().w, 75.0f, 1e-4f);
    PRISM_CHECK_NEAR(g2->box().w, 225.0f, 1e-4f);
}

PRISM_TEST(ui_layout_alignment_and_padding) {
    UiContext::Config cfg;
    cfg.width = 400; cfg.height = 300;
    UiContext ui(cfg);

    auto root = panel(Direction::Column);
    root->spec().main_axis = Align::Center;
    root->spec().cross_axis = Align::Center;
    auto child = fixed(100, 50);
    root->add(child);
    ui.set_root(root);
    ui.layout();
    PRISM_CHECK_NEAR(child->box().x, 150.0f, 1e-4f);    // (400 - 100) / 2
    PRISM_CHECK_NEAR(child->box().y, 125.0f, 1e-4f);    // (300 - 50) / 2

    // End alignment pins to the far edge.
    UiContext ui2(cfg);
    auto root2 = panel(Direction::Column);
    root2->spec().main_axis = Align::End;
    root2->spec().cross_axis = Align::End;
    auto child2 = fixed(100, 50);
    root2->add(child2);
    ui2.set_root(root2);
    ui2.layout();
    PRISM_CHECK_NEAR(child2->box().x, 300.0f, 1e-4f);
    PRISM_CHECK_NEAR(child2->box().y, 250.0f, 1e-4f);

    // Stretch overrides the measured cross size.
    UiContext ui3(cfg);
    auto root3 = panel(Direction::Column, 0, EdgeInsets::all(20));
    root3->spec().cross_axis = Align::Stretch;
    auto child3 = fixed(100, 50);
    child3->spec().width = Sizing::Content;
    root3->add(child3);
    ui3.set_root(root3);
    ui3.layout();
    PRISM_CHECK_NEAR(child3->box().x, 20.0f, 1e-4f);
    PRISM_CHECK_NEAR(child3->box().w, 360.0f, 1e-4f);   // 400 - 2*20 padding
}

PRISM_TEST(ui_layout_absolute_children_leave_the_flow) {
    UiContext::Config cfg;
    cfg.width = 400; cfg.height = 300;
    UiContext ui(cfg);

    auto root = panel(Direction::Column, 5);
    auto flowing = fixed(100, 100);
    auto badge = fixed(50, 60);
    badge->spec().absolute = true;
    badge->spec().absolute_x = 20;
    badge->spec().absolute_y = 30;
    auto after = fixed(100, 100);
    root->add(flowing); root->add(badge); root->add(after);
    ui.set_root(root);
    ui.layout();

    PRISM_CHECK_NEAR(badge->box().x, 20.0f, 1e-4f);
    PRISM_CHECK_NEAR(badge->box().y, 30.0f, 1e-4f);
    PRISM_CHECK_NEAR(badge->box().w, 50.0f, 1e-4f);
    // The absolute child does not consume flow space, so `after` sits right
    // below `flowing` with only the 5 px gap between them.
    PRISM_CHECK_NEAR(after->box().y, 105.0f, 1e-4f);
}

PRISM_TEST(ui_layout_percent_and_min_max_clamps) {
    UiContext::Config cfg;
    cfg.width = 1000; cfg.height = 800;
    UiContext ui(cfg);
    auto root = panel(Direction::Column);
    auto half = std::make_shared<Panel>();
    half->spec().width = Sizing::Percent;  half->spec().width_value = 0.5f;
    half->spec().height = Sizing::Percent; half->spec().height_value = 0.25f;
    auto clamped = std::make_shared<Panel>();
    clamped->spec().width = Sizing::Fill;
    clamped->spec().max_width = 200;
    clamped->spec().height = Sizing::Fixed;
    clamped->spec().height_value = 10;
    clamped->spec().min_height = 40;
    root->add(half); root->add(clamped);
    ui.set_root(root);
    ui.layout();

    PRISM_CHECK_NEAR(half->box().w, 500.0f, 1e-4f);
    PRISM_CHECK_NEAR(half->box().h, 200.0f, 1e-4f);
    PRISM_CHECK_NEAR(clamped->box().w, 200.0f, 1e-4f);   // capped by max_width
    PRISM_CHECK_NEAR(clamped->box().h, 40.0f, 1e-4f);    // raised by min_height
}

// ================================================================ widgets ==
PRISM_TEST(ui_tree_navigation_and_hit_testing) {
    auto root = std::make_shared<Panel>();
    root->set_id("root");
    auto row = std::make_shared<Panel>();
    row->set_id("row");
    auto btn = std::make_shared<Button>("OK");
    btn->set_id("ok");
    btn->spec().width = Sizing::Fixed;  btn->spec().width_value = 200;
    btn->spec().height = Sizing::Fixed; btn->spec().height_value = 50;
    row->add(btn);
    root->add(row);

    PRISM_CHECK_EQ(root->node_count(), 3u);
    PRISM_CHECK(root->find("ok") == btn.get());
    PRISM_CHECK(root->find("nope") == nullptr);
    PRISM_CHECK(btn->parent() == row.get());
    PRISM_CHECK_STR(widget_kind_name(btn->kind()), "button");
    PRISM_CHECK_STR(widget_kind_name(WidgetKind::ProgressBar), "progress_bar");

    root->remove(row);
    PRISM_CHECK_EQ(root->node_count(), 1u);
    root->add(row);
    root->clear_children();
    PRISM_CHECK_EQ(root->child_count(), 0u);
    PRISM_CHECK(row->parent() == nullptr);
}

PRISM_TEST(ui_button_clicks_and_disabled_state) {
    UiContext::Config cfg;
    cfg.width = 400; cfg.height = 300;
    UiContext ui(cfg);

    auto root = std::make_shared<Panel>();
    auto btn = std::make_shared<Button>("Play");
    btn->set_id("play");
    btn->spec().width = Sizing::Fixed;  btn->spec().width_value = 200;
    btn->spec().height = Sizing::Fixed; btn->spec().height_value = 50;
    int callback = 0;
    btn->on_click = [&]() { ++callback; };
    root->add(btn);
    ui.set_root(root);
    ui.layout();

    PRISM_CHECK_NEAR(btn->box().w, 200.0f, 1e-4f);
    PRISM_CHECK(ui.tap(100, 25) == btn.get());
    PRISM_CHECK_EQ(btn->click_count(), 1u);
    PRISM_CHECK_EQ(callback, 1);
    ui.tap(100, 25);
    PRISM_CHECK_EQ(btn->click_count(), 2u);

    // A click outside the button does nothing.
    ui.tap(350, 250);
    PRISM_CHECK_EQ(btn->click_count(), 2u);

    btn->set_enabled(false);
    ui.tap(100, 25);
    PRISM_CHECK_EQ(btn->click_count(), 2u);
    PRISM_CHECK_EQ(callback, 2);

    // Hidden widgets are not hittable.
    btn->set_enabled(true);
    btn->set_visible(false);
    ui.invalidate();
    ui.layout();
    PRISM_CHECK(ui.tap(100, 25) != btn.get());
}

PRISM_TEST(ui_slider_drag_sets_the_value) {
    UiContext::Config cfg;
    cfg.width = 400; cfg.height = 300;
    UiContext ui(cfg);

    auto root = std::make_shared<Panel>();
    auto slider = std::make_shared<Slider>();
    slider->set_id("volume");
    slider->set_range(0.0f, 10.0f);
    slider->spec().absolute = true;
    slider->spec().absolute_x = 100;
    slider->spec().absolute_y = 200;
    slider->spec().width = Sizing::Fixed;  slider->spec().width_value = 200;
    slider->spec().height = Sizing::Fixed; slider->spec().height_value = 40;
    f32 last = -1;
    slider->on_change = [&](f32 v) { last = v; };
    root->add(slider);
    ui.set_root(root);
    ui.layout();

    PRISM_CHECK_NEAR(slider->box().x, 100.0f, 1e-4f);
    PRISM_CHECK_NEAR(slider->value(), 0.0f, 1e-6f);
    PRISM_CHECK(ui.drag(100, 220, 200, 220) == slider.get());
    PRISM_CHECK_NEAR(slider->value(), 5.0f, 1e-4f);     // halfway across
    PRISM_CHECK_NEAR(last, 5.0f, 1e-4f);
    PRISM_CHECK_NEAR(slider->normalised(), 0.5f, 1e-4f);

    ui.drag(100, 220, 500, 220);                        // past the right edge
    PRISM_CHECK_NEAR(slider->value(), 10.0f, 1e-4f);
    ui.drag(100, 220, 0, 220);                          // before the left edge
    PRISM_CHECK_NEAR(slider->value(), 0.0f, 1e-4f);

    // Out-of-range setters clamp.
    slider->set_value(99.0f);
    PRISM_CHECK_NEAR(slider->value(), 10.0f, 1e-6f);
    slider->set_value(-99.0f);
    PRISM_CHECK_NEAR(slider->value(), 0.0f, 1e-6f);
}

PRISM_TEST(ui_toggle_flips_and_reports) {
    UiContext::Config cfg;
    cfg.width = 400; cfg.height = 300;
    UiContext ui(cfg);
    auto root = std::make_shared<Panel>();
    auto t = std::make_shared<Toggle>("Vsync");
    t->spec().width = Sizing::Fixed;  t->spec().width_value = 200;
    t->spec().height = Sizing::Fixed; t->spec().height_value = 48;
    int changes = 0;
    bool seen = false;
    t->on_change = [&](bool v) { ++changes; seen = v; };
    root->add(t);
    ui.set_root(root);
    ui.layout();

    PRISM_CHECK(!t->checked());
    ui.tap(50, 24);
    PRISM_CHECK(t->checked());
    PRISM_CHECK(seen);
    PRISM_CHECK_EQ(changes, 1);
    ui.tap(50, 24);
    PRISM_CHECK(!t->checked());
    PRISM_CHECK_EQ(changes, 2);
}

PRISM_TEST(ui_focus_follows_the_pointer) {
    UiContext::Config cfg;
    cfg.width = 400; cfg.height = 300;
    UiContext ui(cfg);
    auto root = std::make_shared<Panel>();
    root->spec().direction = Direction::Row;
    auto a = std::make_shared<Button>("A");
    auto b = std::make_shared<Button>("B");
    for (auto* w : {a.get(), b.get()}) {
        w->spec().width = Sizing::Fixed;  w->spec().width_value = 100;
        w->spec().height = Sizing::Fixed; w->spec().height_value = 50;
    }
    root->add(a); root->add(b);
    ui.set_root(root);
    ui.layout();

    ui.tap(50, 25);
    PRISM_CHECK(ui.focus() == a.get());
    PRISM_CHECK(a->focused());
    ui.tap(150, 25);
    PRISM_CHECK(ui.focus() == b.get());
    PRISM_CHECK(!a->focused());
    PRISM_CHECK(b->focused());
}

// ================================================================= render ==
PRISM_TEST(ui_render_emits_ordered_draw_commands) {
    UiContext::Config cfg;
    cfg.width = 400; cfg.height = 300;
    UiContext ui(cfg);

    auto root = std::make_shared<Panel>();
    auto label = std::make_shared<Label>("PRISM ENGINE");
    label->set_id("title");
    label->set_font_size(24.0f);
    auto bar = std::make_shared<ProgressBar>();
    bar->set_progress(0.4f);
    bar->spec().width = Sizing::Fill;
    bar->spec().height = Sizing::Fixed;
    bar->spec().height_value = 12;
    root->add(label);
    root->add(bar);
    ui.set_root(root);
    ui.layout();

    const auto cmds = ui.render();
    PRISM_CHECK(cmds.size() >= 4u);
    PRISM_CHECK(cmds[0].type == DrawCmd::Type::Rect);      // background clear
    PRISM_CHECK_NEAR(cmds[0].box.w, 400.0f, 1e-4f);
    PRISM_CHECK_EQ(count_type(cmds, DrawCmd::Type::Text), 1u);
    PRISM_CHECK_EQ(count_type(cmds, DrawCmd::Type::RoundedRect), 3u);  // panel + 2 bars

    // Depth must be strictly increasing so the painter's order is stable.
    for (std::size_t i = 1; i < cmds.size(); ++i) PRISM_CHECK(cmds[i].depth > cmds[i - 1].depth);

    bool found_title = false;
    for (const auto& c : cmds) {
        if (c.type == DrawCmd::Type::Text && c.text == "PRISM ENGINE") {
            found_title = true;
            PRISM_CHECK_NEAR(c.font_size, 24.0f, 1e-4f);
        }
    }
    PRISM_CHECK(found_title);

    // An empty context still clears the screen rather than emitting nothing.
    UiContext empty(cfg);
    PRISM_CHECK_EQ(empty.render().size(), 1u);
}

PRISM_TEST(ui_label_measure_scales_with_font_size) {
    Label small_t("abcd");
    Label big_t("abcd");
    big_t.set_font_size(32.0f);
    const math::Vec2 a = small_t.measure(math::Vec2(1000, 1000), 1.0f);
    const math::Vec2 b = big_t.measure(math::Vec2(1000, 1000), 1.0f);
    PRISM_CHECK_NEAR(a.x, 4 * 16.0f * 0.5f, 1e-4f);
    PRISM_CHECK_NEAR(b.x, a.x * 2.0f, 1e-4f);
    // A 2x dpi scale doubles the measured box.
    const math::Vec2 scaled = small_t.measure(math::Vec2(1000, 1000), 2.0f);
    PRISM_CHECK_NEAR(scaled.x, a.x * 2.0f, 1e-4f);
    Label empty;
    PRISM_CHECK_NEAR(empty.measure(math::Vec2(100, 100), 1.0f).x, 0.0f, 1e-6f);
}

// =========================================================== localisation ==
PRISM_TEST(ui_localisation_covers_seven_languages_offline) {
    PRISM_CHECK_EQ(Localisation::supported_languages().size(), 7u);
    PRISM_CHECK(Localisation::is_supported("en"));
    PRISM_CHECK(Localisation::is_supported("am"));
    PRISM_CHECK(Localisation::is_supported("hi"));
    PRISM_CHECK(!Localisation::is_supported("de"));
    PRISM_CHECK(Localisation::is_rtl("ar"));
    PRISM_CHECK(!Localisation::is_rtl("en"));

    Localisation l;
    install_builtin_strings(l);
    PRISM_CHECK_EQ(l.key_count("en"), 20u);
    for (const auto& lang : Localisation::supported_languages()) {
        PRISM_CHECK_EQ(l.key_count(lang), 20u);
        PRISM_CHECK_EQ(l.missing(lang).size(), 0u);
    }

    PRISM_CHECK_STR(l.get("play"), "Play");
    l.set_language("am");
    PRISM_CHECK_STR(l.language(), "am");
    PRISM_CHECK_STR(l.get("play"), "አጫውት");
    l.set_language("es");
    PRISM_CHECK_STR(l.get("high_score"), "Récord");
    l.set_language("zh");
    PRISM_CHECK_STR(l.get("settings"), "设置");
    l.set_language("fr");
    PRISM_CHECK_STR(l.get("loading"), "Chargement");
    l.set_language("ar");
    PRISM_CHECK_STR(l.get("quit"), "خروج");
    l.set_language("hi");
    PRISM_CHECK_STR(l.get("score"), "स्कोर");

    // An unsupported language falls back to English rather than failing.
    l.set_language("de");
    PRISM_CHECK_STR(l.language(), "en");
    PRISM_CHECK_STR(l.get("play"), "Play");
}

PRISM_TEST(ui_localisation_falls_back_and_formats) {
    Localisation l;
    l.set("en", "greeting", "Hello, {0}!");
    l.set("en", "count", "{0} of {1}");
    l.set("am", "greeting", "ሰላም፣ {0}!");
    // "count" is deliberately untranslated.

    l.set_language("am");
    // get() returns the raw template; format() substitutes the arguments.
    PRISM_CHECK_STR(l.get("greeting"), "ሰላም፣ {0}!");
    PRISM_CHECK_STR(l.format("greeting", {"Ray"}), "ሰላም፣ Ray!");
    PRISM_CHECK_STR(l.format("count", {"3", "10"}), "3 of 10");   // English fallback
    PRISM_CHECK_EQ(l.missing("am").size(), 1u);
    PRISM_CHECK_STR(l.missing("am")[0], "count");

    // A key with no translation anywhere surfaces as the key itself, so a
    // missing string is visible in the running game instead of blank.
    PRISM_CHECK_STR(l.get("no.such.key"), "no.such.key");
    // Repeated placeholders are all replaced.
    l.set("en", "twice", "{0} and {0}");
    PRISM_CHECK_STR(l.format("twice", {"x"}), "x and x");

    l.clear();
    PRISM_CHECK_EQ(l.key_count("en"), 0u);
    PRISM_CHECK_STR(l.language(), "en");
}

// ========================================================= touch controls ==
PRISM_TEST(ui_touch_bindings_apply_dead_zone_and_invert) {
    TouchControls tc;
    TouchBinding jump;
    jump.widget_id = "btn_jump";
    jump.action = "jump";
    jump.kind = TouchBinding::Kind::Button;
    tc.bind(jump);

    TouchBinding steer;
    steer.widget_id = "stick_left";
    steer.action = "steer";
    steer.kind = TouchBinding::Kind::Axis;
    steer.dead_zone = 0.2f;
    tc.bind(steer);

    PRISM_CHECK_EQ(tc.bindings().size(), 2u);
    PRISM_CHECK(tc.find("jump") != nullptr);
    PRISM_CHECK(tc.find("steer") != nullptr);
    PRISM_CHECK(tc.find("crouch") == nullptr);

    // Buttons are binary.
    PRISM_CHECK_NEAR(tc.apply(jump, 0.0f, 0.0f), 0.0f, 1e-6f);
    PRISM_CHECK_NEAR(tc.apply(jump, 0.01f, 0.0f), 1.0f, 1e-6f);

    // Inside the dead zone the axis is exactly zero, and the remainder is
    // rescaled so full deflection still reaches 1.
    PRISM_CHECK_NEAR(tc.apply(steer, 0.1f, 0.0f), 0.0f, 1e-6f);
    PRISM_CHECK_NEAR(tc.apply(steer, 0.2f, 0.0f), 0.0f, 1e-6f);
    PRISM_CHECK_NEAR(tc.apply(steer, 0.6f, 0.0f), 0.5f, 1e-4f);
    PRISM_CHECK_NEAR(tc.apply(steer, 1.0f, 0.0f), 1.0f, 1e-4f);
    PRISM_CHECK_NEAR(tc.apply(steer, -1.0f, 0.0f), -1.0f, 1e-4f);
    PRISM_CHECK_NEAR(tc.apply(steer, 5.0f, 0.0f), 1.0f, 1e-4f);   // clamped

    TouchBinding inverted = steer;
    inverted.invert = true;
    PRISM_CHECK_NEAR(tc.apply(inverted, 1.0f, 0.0f), -1.0f, 1e-4f);

    TouchBinding sensitive = steer;
    sensitive.sensitivity = 2.0f;
    PRISM_CHECK_NEAR(tc.apply(sensitive, 0.3f, 0.0f), 0.5f, 1e-4f);   // 0.6 rescaled

    // A stick reads the vertical axis.
    TouchBinding look;
    look.kind = TouchBinding::Kind::Stick;
    look.dead_zone = 0.0f;
    PRISM_CHECK_NEAR(tc.apply(look, 0.0f, 0.75f), 0.75f, 1e-4f);

    tc.clear();
    PRISM_CHECK_EQ(tc.bindings().size(), 0u);
}

PRISM_TEST(ui_touch_controls_poll_maps_widgets_to_actions) {
    UiContext::Config cfg;
    cfg.width = 800; cfg.height = 480;
    UiContext ui(cfg);

    auto root = std::make_shared<Panel>();
    root->spec().direction = Direction::Row;
    auto left = std::make_shared<Panel>();
    left->set_id("stick_left");
    left->spec().grow = 1; left->spec().height = Sizing::Fill;
    auto right = std::make_shared<Panel>();
    right->set_id("btn_jump");
    right->spec().grow = 1; right->spec().height = Sizing::Fill;
    root->add(left); root->add(right);
    ui.set_root(root);
    ui.layout();
    PRISM_CHECK(ui.find("btn_jump") == right.get());

    TouchControls tc;
    TouchBinding steer;
    steer.widget_id = "stick_left";
    steer.action = "steer";
    steer.kind = TouchBinding::Kind::Axis;
    steer.dead_zone = 0.0f;
    tc.bind(steer);
    TouchBinding jump;
    jump.widget_id = "btn_jump";
    jump.action = "jump";
    jump.kind = TouchBinding::Kind::Button;
    tc.bind(jump);
    TouchBinding missing;
    missing.widget_id = "not_on_screen";
    missing.action = "crouch";
    tc.bind(missing);

    const auto state = tc.poll(ui, {{"stick_left", {0.5f, 0.0f}}, {"btn_jump", {0.0f, 0.0f}}});
    PRISM_CHECK_EQ(state.size(), 3u);
    f32 steer_v = -99, jump_v = -99, crouch_v = -99;
    for (const auto& [action, v] : state) {
        if (action == "steer") steer_v = v;
        else if (action == "jump") jump_v = v;
        else crouch_v = v;
    }
    PRISM_CHECK_NEAR(steer_v, 0.5f, 1e-4f);
    // The button is touched but reports no displacement, so it is not pressed.
    PRISM_CHECK_NEAR(jump_v, 0.0f, 1e-6f);
    // A binding whose widget is absent reads as released, never as garbage.
    PRISM_CHECK_NEAR(crouch_v, 0.0f, 1e-6f);
}

// ============================================================== box maths ==
PRISM_TEST(ui_box_helpers) {
    const Box a{0, 0, 100, 100};
    PRISM_CHECK(a.contains(0, 0));
    PRISM_CHECK(a.contains(100, 100));
    PRISM_CHECK(!a.contains(101, 50));
    PRISM_CHECK(!a.contains(-1, 50));

    const Box inset = a.inset(10);
    PRISM_CHECK_NEAR(inset.x, 10.0f, 1e-6f);
    PRISM_CHECK_NEAR(inset.w, 80.0f, 1e-6f);

    const Box inter = Box::intersect(a, Box{50, 50, 100, 100});
    PRISM_CHECK_NEAR(inter.x, 50.0f, 1e-6f);
    PRISM_CHECK_NEAR(inter.w, 50.0f, 1e-6f);
    PRISM_CHECK(!Box::intersect(a, Box{200, 200, 10, 10}).valid());
    PRISM_CHECK(!Box{0, 0, 0, 5}.valid());
    PRISM_CHECK(a.translated(5, -5).y == -5.0f);

    const EdgeInsets e{1, 2, 3, 4};
    PRISM_CHECK_NEAR(e.horizontal(), 4.0f, 1e-6f);
    PRISM_CHECK_NEAR(e.vertical(), 6.0f, 1e-6f);
    PRISM_CHECK_NEAR(EdgeInsets::all(7).left, 7.0f, 1e-6f);
}
