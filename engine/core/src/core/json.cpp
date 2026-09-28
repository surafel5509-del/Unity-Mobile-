// PRISM ENGINE — core/json.cpp : RFC 8259 reader/writer with UTF-8 escapes.
#include "prism/core/json.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace prism::json {

namespace {
const Value kNull;
const Value kEmptyArray = Value::make_array();
const Value kEmptyObject = Value::make_object();

inline bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

void encode_utf8(u32 cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Reader {
public:
    Reader(std::string_view t) : t_(t) {}

    bool parse(Value& out, std::string& err) {
        skip_ws();
        if (!value(out, err)) return false;
        skip_ws();
        if (pos_ != t_.size()) return fail(err, "trailing characters after JSON value");
        return true;
    }

private:
    bool fail(std::string& err, const std::string& what) {
        i32 line = 1;
        for (std::size_t i = 0; i < pos_ && i < t_.size(); ++i) if (t_[i] == '\n') ++line;
        err = what + " (line " + std::to_string(line) + ")";
        return false;
    }
    void skip_ws() { while (pos_ < t_.size() && is_ws(t_[pos_])) ++pos_; }
    bool eof() const { return pos_ >= t_.size(); }
    char peek() const { return pos_ < t_.size() ? t_[pos_] : '\0'; }

    bool literal(std::string_view lit) {
        if (t_.compare(pos_, lit.size(), lit) == 0) { pos_ += lit.size(); return true; }
        return false;
    }

    bool value(Value& out, std::string& err) {
        if (eof()) return fail(err, "unexpected end of input");
        switch (peek()) {
            case '{': return object(out, err);
            case '[': return array(out, err);
            case '"': { std::string s; if (!string(s, err)) return false; out = Value(std::move(s)); return true; }
            case 't': if (literal("true"))  { out = Value(true);  return true; } return fail(err, "invalid literal");
            case 'f': if (literal("false")) { out = Value(false); return true; } return fail(err, "invalid literal");
            case 'n': if (literal("null"))  { out = Value();      return true; } return fail(err, "invalid literal");
            default:  return number(out, err);
        }
    }

    bool number(Value& out, std::string& err) {
        std::size_t start = pos_;
        if (peek() == '-' || peek() == '+') ++pos_;
        bool digits = false;
        while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') { ++pos_; digits = true; }
        if (peek() == '.') {
            ++pos_;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') { ++pos_; digits = true; }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '-' || peek() == '+') ++pos_;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        }
        if (!digits) return fail(err, "invalid number");
        std::string tok(t_.substr(start, pos_ - start));
        char* end = nullptr;
        f64 v = std::strtod(tok.c_str(), &end);
        if (end == tok.c_str()) return fail(err, "unparseable number");
        out = Value(v);
        return true;
    }

    bool string(std::string& out, std::string& err) {
        if (peek() != '"') return fail(err, "expected string");
        ++pos_;
        out.clear();
        while (!eof()) {
            char c = t_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (eof()) return fail(err, "unterminated escape");
                char e = t_[pos_++];
                switch (e) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        u32 cp = 0;
                        if (!hex4(cp, err)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < t_.size() &&
                            t_[pos_] == '\\' && t_[pos_ + 1] == 'u') {
                            std::size_t save = pos_;
                            pos_ += 2;
                            u32 lo = 0;
                            if (hex4(lo, err) && lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                pos_ = save;   // lone high surrogate
                            }
                        }
                        encode_utf8(cp, out);
                        break;
                    }
                    default: return fail(err, "unknown escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail(err, "unterminated string");
    }

    bool hex4(u32& out, std::string& err) {
        if (pos_ + 4 > t_.size()) return fail(err, "truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[pos_++];
            u32 d;
            if (c >= '0' && c <= '9') d = static_cast<u32>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<u32>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<u32>(c - 'A' + 10);
            else return fail(err, "invalid hex digit in \\u escape");
            out = (out << 4) | d;
        }
        return true;
    }

    bool array(Value& out, std::string& err) {
        ++pos_;  // consume '['
        out = Value::make_array();
        skip_ws();
        if (peek() == ']') { ++pos_; return true; }
        for (;;) {
            skip_ws();
            Value v;
            if (!value(v, err)) return false;
            out.push_back(std::move(v));
            skip_ws();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; return true; }
            return fail(err, "expected ',' or ']' in array");
        }
    }

    bool object(Value& out, std::string& err) {
        ++pos_;  // consume '{'
        out = Value::make_object();
        skip_ws();
        if (peek() == '}') { ++pos_; return true; }
        for (;;) {
            skip_ws();
            std::string key;
            if (!string(key, err)) return false;
            skip_ws();
            if (peek() != ':') return fail(err, "expected ':' after object key");
            ++pos_;
            skip_ws();
            Value v;
            if (!value(v, err)) return false;
            out.set(key, std::move(v));
            skip_ws();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; return true; }
            return fail(err, "expected ',' or '}' in object");
        }
    }

    std::string_view t_;
    std::size_t pos_ = 0;
};

void append_number(std::string& out, f64 v) {
    if (std::isnan(v) || std::isinf(v)) { out += "null"; return; }
    // integral values print without a decimal point; others use shortest round-trip
    if (v == static_cast<f64>(static_cast<i64>(v)) && std::fabs(v) < 1e15) {
        out += std::to_string(static_cast<i64>(v));
        return;
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    out += buf;
}

void indent_to(std::string& out, i32 indent, i32 depth) {
    if (indent <= 0) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
}
} // namespace

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

void Value::dump_to(std::string& out, i32 indent, i32 depth) const {
    switch (type_) {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += b_ ? "true" : "false"; break;
        case Type::Number: append_number(out, n_); break;
        case Type::String: out.push_back('"'); out += escape(s_); out.push_back('"'); break;
        case Type::Array: {
            if (!arr_ || arr_->empty()) { out += "[]"; break; }
            out.push_back('[');
            bool first = true;
            for (const auto& v : *arr_) {
                if (!first) out.push_back(',');
                first = false;
                indent_to(out, indent, depth + 1);
                v.dump_to(out, indent, depth + 1);
            }
            indent_to(out, indent, depth);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            if (!obj_ || obj_->empty()) { out += "{}"; break; }
            out.push_back('{');
            bool first = true;
            for (const auto& kv : *obj_) {
                if (!first) out.push_back(',');
                first = false;
                indent_to(out, indent, depth + 1);
                out.push_back('"'); out += escape(kv.first); out += "\": ";
                kv.second.dump_to(out, indent, depth + 1);
            }
            indent_to(out, indent, depth);
            out.push_back('}');
            break;
        }
    }
}

std::string Value::dump(i32 indent) const {
    std::string out;
    dump_to(out, indent, 0);
    if (indent > 0) out.push_back('\n');
    return out;
}

Value Value::parse(std::string_view text, std::string* error) {
    Reader r(text);
    Value out;
    std::string err;
    if (!r.parse(out, err)) {
        if (error) *error = err;
        return Value();
    }
    if (error) error->clear();
    return out;
}

const Value& Value::operator[](const std::string& key) const {
    if (type_ != Type::Object || !obj_) return kNull;
    auto it = obj_->find(key);
    return it == obj_->end() ? kNull : it->second;
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::Object) { type_ = Type::Object; obj_ = std::make_shared<Object>(); }
    if (!obj_) obj_ = std::make_shared<Object>();
    return (*obj_)[key];
}

std::vector<std::string> Value::keys() const {
    std::vector<std::string> out;
    if (type_ == Type::Object && obj_) for (const auto& kv : *obj_) out.push_back(kv.first);
    return out;
}

void Value::set(const std::string& key, Value v) {
    if (type_ != Type::Object) { type_ = Type::Object; obj_ = std::make_shared<Object>(); }
    if (!obj_) obj_ = std::make_shared<Object>();
    (*obj_)[key] = std::move(v);
}

const Value& Value::get(const std::string& key, const Value& fallback) const {
    const Value& v = (*this)[key];
    return v.is_null() ? fallback : v;
}
std::string Value::get_string(const std::string& key, const char* fallback) const {
    return (*this)[key].as_string_or(fallback);
}
f64  Value::get_number(const std::string& key, f64 fallback) const { return (*this)[key].as_number(fallback); }
i64  Value::get_int(const std::string& key, i64 fallback) const { return (*this)[key].as_int(fallback); }
bool Value::get_bool(const std::string& key, bool fallback) const { return (*this)[key].as_bool(fallback ? 1 : 0); }
const Value& Value::get_array(const std::string& key) const {
    const Value& v = (*this)[key];
    return v.is_array() ? v : kEmptyArray;
}
const Value& Value::get_object(const std::string& key) const {
    const Value& v = (*this)[key];
    return v.is_object() ? v : kEmptyObject;
}

std::size_t Value::size() const {
    if (type_ == Type::Array && arr_) return arr_->size();
    if (type_ == Type::Object && obj_) return obj_->size();
    if (type_ == Type::String) return s_.size();
    return 0;
}

const Value& Value::at(std::size_t i) const {
    if (type_ != Type::Array || !arr_ || i >= arr_->size()) return kNull;
    return (*arr_)[i];
}

void Value::push_back(Value v) {
    if (type_ != Type::Array) { type_ = Type::Array; arr_ = std::make_shared<Array>(); }
    if (!arr_) arr_ = std::make_shared<Array>();
    arr_->push_back(std::move(v));
}

bool read_file(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool write_file(const std::string& path, std::string_view data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::size_t written = std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return written == data.size();
}

} // namespace prism::json
