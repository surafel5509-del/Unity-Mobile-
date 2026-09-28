// =====================================================================
//  PRISM ENGINE — core/json.h
//  Compact JSON reader/writer. Used for: scene files, project.prism.json,
//  touch-control templates, brand tokens, editor settings, save metadata.
//  Binary asset payloads use the PRISM pack container instead (see
//  assets/assets.h) — JSON is for human-editable config only.
//
//  Design notes:
//    * UTF-8 in / UTF-8 out, \u escapes decoded to UTF-8 (incl. surrogate pairs)
//    * No DOM diffing, no streaming — config files are small
//    * Deterministic key order on write (std::map) so APK diffs are stable
// =====================================================================
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "../core/types.h"

namespace prism::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    enum class Type : u8 { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b)                 : type_(Type::Bool), b_(b) {}
    Value(i32 n)                  : type_(Type::Number), n_(n) {}
    Value(i64 n)                  : type_(Type::Number), n_(static_cast<f64>(n)) {}
    Value(f32 n)                  : type_(Type::Number), n_(n) {}
    Value(f64 n)                  : type_(Type::Number), n_(n) {}
    Value(const char* s)          : type_(Type::String), s_(s) {}
    Value(std::string s)          : type_(Type::String), s_(std::move(s)) {}
    Value(Array a)                : type_(Type::Array), arr_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o)               : type_(Type::Object), obj_(std::make_shared<Object>(std::move(o))) {}

    static Value make_array()  { Value v; v.type_ = Type::Array;  v.arr_ = std::make_shared<Array>();  return v; }
    static Value make_object() { Value v; v.type_ = Type::Object; v.obj_ = std::make_shared<Object>(); return v; }

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool is_null()   const { return type_ == Type::Null; }
    [[nodiscard]] bool is_bool()   const { return type_ == Type::Bool; }
    [[nodiscard]] bool is_number() const { return type_ == Type::Number; }
    [[nodiscard]] bool is_string() const { return type_ == Type::String; }
    [[nodiscard]] bool is_array()  const { return type_ == Type::Array; }
    [[nodiscard]] bool is_object() const { return type_ == Type::Object; }

    [[nodiscard]] bool as_bool(f64 fallback = false) const { return type_ == Type::Bool ? b_ : (fallback != 0.0); }
    [[nodiscard]] f64  as_number(f64 fallback = 0) const { return type_ == Type::Number ? n_ : fallback; }
    [[nodiscard]] i64  as_int(i64 fallback = 0) const { return type_ == Type::Number ? static_cast<i64>(n_) : fallback; }
    [[nodiscard]] f32  as_float(f32 fallback = 0) const { return type_ == Type::Number ? static_cast<f32>(n_) : fallback; }
    [[nodiscard]] const std::string& as_string() const { return s_; }
    [[nodiscard]] std::string as_string_or(const char* fallback) const {
        return type_ == Type::String ? s_ : std::string(fallback);
    }

    // ---- object access ---------------------------------------------------
    [[nodiscard]] const Value& operator[](const std::string& key) const;
    Value& operator[](const std::string& key);
    [[nodiscard]] bool contains(const std::string& key) const {
        return type_ == Type::Object && obj_ && obj_->count(key) > 0;
    }
    [[nodiscard]] std::vector<std::string> keys() const;
    void set(const std::string& key, Value v);
    [[nodiscard]] const Value& get(const std::string& key, const Value& fallback) const;
    [[nodiscard]] std::string get_string(const std::string& key, const char* fallback = "") const;
    [[nodiscard]] f64  get_number(const std::string& key, f64 fallback = 0) const;
    [[nodiscard]] i64  get_int(const std::string& key, i64 fallback = 0) const;
    [[nodiscard]] bool get_bool(const std::string& key, bool fallback = false) const;
    [[nodiscard]] const Value& get_array(const std::string& key) const;
    [[nodiscard]] const Value& get_object(const std::string& key) const;

    // ---- array access ----------------------------------------------------
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool empty() const { return size() == 0; }
    [[nodiscard]] const Value& at(std::size_t i) const;
    void push_back(Value v);
    [[nodiscard]] const Array* array() const { return arr_.get(); }
    [[nodiscard]] const Object* object() const { return obj_.get(); }

    // ---- (de)serialisation ----------------------------------------------
    [[nodiscard]] std::string dump(i32 indent = 2) const;
    [[nodiscard]] static Value parse(std::string_view text, std::string* error = nullptr);

private:
    void dump_to(std::string& out, i32 indent, i32 depth) const;

    Type type_ = Type::Null;
    bool b_ = false;
    f64 n_ = 0;
    std::string s_;
    std::shared_ptr<Array> arr_;
    std::shared_ptr<Object> obj_;
};

/// Escapes a string for embedding in a JSON literal (no surrounding quotes).
[[nodiscard]] std::string escape(std::string_view s);
/// Reads a whole file into a string. Returns false if unreadable.
[[nodiscard]] bool read_file(const std::string& path, std::string& out);
[[nodiscard]] bool write_file(const std::string& path, std::string_view data);

} // namespace prism::json
