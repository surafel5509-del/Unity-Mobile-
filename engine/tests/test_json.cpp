// PRISM ENGINE — JSON reader/writer tests.
#include "prism_test.h"
#include "prism/core/json.h"
#include <limits>

using namespace prism;
using namespace prism::json;

PRISM_TEST(json_parse_primitives) {
    std::string err;
    auto v = Value::parse(R"({"n":42,"f":-1.5e3,"s":"hi","t":true,"f2":false,"z":null})", &err);
    PRISM_CHECK_STR(err, "");
    PRISM_CHECK(v.is_object());
    PRISM_CHECK_NEAR(v["n"].as_number(), 42, 1e-9);
    PRISM_CHECK_NEAR(v["f"].as_number(), -1500.0, 1e-9);
    PRISM_CHECK_STR(v["s"].as_string(), "hi");
    PRISM_CHECK(v["t"].as_bool());
    PRISM_CHECK(!v["f2"].as_bool());
    PRISM_CHECK(v["z"].is_null());
    PRISM_CHECK(v["missing"].is_null());
}

PRISM_TEST(json_parse_nested_arrays_objects) {
    auto v = Value::parse(R"({
        "entities": [
            {"name":"player","pos":[1.5,2.0,3.0],"tags":["hero","p1"]},
            {"name":"enemy","pos":[-1,0,4],"tags":[]}
        ],
        "meta": {"version":1,"offline":true}
    })");
    const auto& list = v["entities"];
    PRISM_CHECK(list.is_array());
    PRISM_CHECK_EQ(static_cast<int>(list.size()), 2);
    PRISM_CHECK_STR(list.at(0)["name"].as_string(), "player");
    PRISM_CHECK_NEAR(list.at(0)["pos"].at(2).as_number(), 3.0, 1e-9);
    PRISM_CHECK_EQ(static_cast<int>(list.at(1)["tags"].size()), 0);
    PRISM_CHECK(v["meta"]["offline"].as_bool());
    PRISM_CHECK(v.at(99).is_null());
}

PRISM_TEST(json_escapes_and_unicode) {
    auto v = Value::parse(R"({"s":"line\nbreak\t\"quoted\" \u00e9 \u1200 \ud83d\ude00"})", nullptr);
    const std::string& s = v["s"].as_string();
    PRISM_CHECK(s.find("line\nbreak") != std::string::npos);
    PRISM_CHECK(s.find("\"quoted\"") != std::string::npos);
    // U+00E9 -> C3 A9, U+1200 (Ethiopic HA) -> E1 88 80, U+1F600 -> F0 9F 98 80
    PRISM_CHECK(s.find("\xC3\xA9") != std::string::npos);
    PRISM_CHECK(s.find("\xE1\x88\x80") != std::string::npos);
    PRISM_CHECK(s.find("\xF0\x9F\x98\x80") != std::string::npos);
}

PRISM_TEST(json_error_reporting) {
    std::string err;
    PRISM_CHECK(Value::parse("{", &err).is_null());
    PRISM_CHECK(!err.empty());
    err.clear();
    Value::parse(R"({"a":1,})", &err);
    PRISM_CHECK(!err.empty());
    err.clear();
    Value::parse(R"({"a":1} trailing)", &err);
    PRISM_CHECK(err.find("trailing") != std::string::npos);
    err.clear();
    Value::parse(R"({"a":tru})", &err);
    PRISM_CHECK(!err.empty());
}

PRISM_TEST(json_roundtrip_deterministic) {
    Value v = Value::make_object();
    v.set("name", Value("PRISM ENGINE"));
    v.set("tagline", Value("Every Angle. Every World. One File."));
    v.set("offline", Value(true));
    v.set("fps", Value(120));
    v.set("scale", Value(0.85));
    Value abis = Value::make_array();
    abis.push_back(Value("arm64-v8a"));
    abis.push_back(Value("armeabi-v7a"));
    v.set("abi", std::move(abis));

    const std::string a = v.dump(2);
    const std::string b = v.dump(2);
    PRISM_CHECK_STR(a, b);                          // stable ordering
    auto back = Value::parse(a);
    PRISM_CHECK_STR(back["name"].as_string(), "PRISM ENGINE");
    PRISM_CHECK_EQ(static_cast<int>(back["abi"].size()), 2);
    PRISM_CHECK_NEAR(back["scale"].as_number(), 0.85, 1e-9);
    // std::map ordering puts "abi" before "fps" before "name"
    PRISM_CHECK(a.find("\"abi\"") < a.find("\"fps\""));
    PRISM_CHECK(a.find("\"fps\"") < a.find("\"name\""));
    // compact form has no newlines
    PRISM_CHECK(v.dump(0).find('\n') == std::string::npos);
}

PRISM_TEST(json_number_formatting) {
    PRISM_CHECK_STR(Value(42).dump(0), "42");
    PRISM_CHECK_STR(Value(42.0).dump(0), "42");
    PRISM_CHECK_STR(Value(0.5).dump(0), "0.5");
    PRISM_CHECK_STR(Value(true).dump(0), "true");
    PRISM_CHECK_STR(Value().dump(0), "null");
    PRISM_CHECK_STR(Value("a\"b").dump(0), "\"a\\\"b\"");
    // empty containers
    PRISM_CHECK_STR(Value::make_array().dump(0), "[]");
    PRISM_CHECK_STR(Value::make_object().dump(0), "{}");
    // NaN/inf cannot be represented in JSON
    PRISM_CHECK_STR(Value(std::numeric_limits<double>::quiet_NaN()).dump(0), "null");
}

PRISM_TEST(json_repo_files_are_valid) {
    // Every JSON asset that ships in the repo must parse. This is the offline
    // "airplane mode" guarantee: no config file may depend on a download.
    const char* paths[] = {
        "brand/tokens/brand.json",
        "templates/touch-controls/platformer.json",
        "templates/touch-controls/fps.json",
        "templates/touch-controls/rpg.json",
        "templates/touch-controls/one-tap.json",
        "samples/platformer2d/project.prism.json",
        "samples/platformer2d/scenes/main.scene.json",
        "samples/fps3d/project.prism.json",
        "samples/fps3d/scenes/main.scene.json",
        "samples/rpg/project.prism.json",
        "samples/rpg/scenes/main.scene.json",
        "samples/hypercasual/project.prism.json",
        "samples/hypercasual/scenes/main.scene.json",
    };
    for (const char* p : paths) {
        std::string text;
        if (!read_file(p, text)) {
            PRISM_CHECK_STR(std::string("missing ") + p, "present");
            continue;
        }
        std::string err;
        auto v = Value::parse(text, &err);
        if (!err.empty()) PRISM_CHECK_STR(std::string(p) + " -> " + err, "valid json");
        PRISM_CHECK(v.is_object());
    }
    // brand tokens must carry the documented Prism Violet
    std::string text;
    PRISM_CHECK(read_file("brand/tokens/brand.json", text));
    auto brand = Value::parse(text);
    PRISM_CHECK(brand.dump(0).find("7C3AED") != std::string::npos);
    // touch templates must declare at least one control
    PRISM_CHECK(read_file("templates/touch-controls/platformer.json", text));
    auto tpl = Value::parse(text);
    PRISM_CHECK(tpl.dump(0).size() > 20);
}
