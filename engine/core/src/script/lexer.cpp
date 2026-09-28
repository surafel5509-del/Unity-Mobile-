#include "prism/script/prismscript.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace prism::script {

namespace {
const std::unordered_map<std::string, Tok>& keywords() {
    static const std::unordered_map<std::string, Tok> k = {
        {"let", Tok::Let}, {"var", Tok::Var}, {"func", Tok::Func}, {"class", Tok::Class},
        {"extends", Tok::Extends}, {"if", Tok::If}, {"else", Tok::Else}, {"while", Tok::While},
        {"for", Tok::For}, {"in", Tok::In}, {"return", Tok::Return}, {"break", Tok::Break},
        {"continue", Tok::Continue}, {"true", Tok::True}, {"false", Tok::False},
        {"null", Tok::Null}, {"new", Tok::New}, {"this", Tok::This},
        {"and", Tok::And}, {"or", Tok::Or}, {"not", Tok::Not},
        {"using", Tok::Using}, {"import", Tok::Import},
    };
    return k;
}
} // namespace

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (pos_ < src_.size()) {
        char c = src_[pos_];
        if (c == '\n') { ++line_; col_ = 1; ++pos_; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++pos_; ++col_; continue; }
        // comments
        if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
            while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
            continue;
        }
        if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '*') {
            pos_ += 2; col_ += 2;
            while (pos_ + 1 < src_.size() && !(src_[pos_] == '*' && src_[pos_ + 1] == '/')) {
                if (src_[pos_] == '\n') { ++line_; col_ = 1; } else { ++col_; }
                ++pos_;
            }
            if (pos_ + 1 < src_.size()) { pos_ += 2; col_ += 2; }
            continue;
        }
        Token t; t.line = line_; t.col = col_;
        // numbers
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && pos_ + 1 < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_ + 1])))) {
            std::size_t start = pos_;
            bool seen_dot = false;
            while (pos_ < src_.size()) {
                char d = src_[pos_];
                if (std::isdigit(static_cast<unsigned char>(d))) { ++pos_; }
                else if (d == '.' && !seen_dot) { seen_dot = true; ++pos_; }
                else break;
            }
            t.kind = Tok::Number;
            t.text = src_.substr(start, pos_ - start);
            t.number = std::stod(t.text);
            t.col = col_; col_ += static_cast<i32>(t.text.size());
            out.push_back(t);
            continue;
        }
        // strings (double or single quoted, with \n \t \\ \" escapes)
        if (c == '"' || c == '\'') {
            char quote = c;
            ++pos_; ++col_;
            std::string s;
            while (pos_ < src_.size() && src_[pos_] != quote) {
                char d = src_[pos_];
                if (d == '\\' && pos_ + 1 < src_.size()) {
                    char e = src_[pos_ + 1];
                    switch (e) {
                        case 'n': s += '\n'; break;
                        case 't': s += '\t'; break;
                        case 'r': s += '\r'; break;
                        case '0': s += '\0'; break;
                        default:  s += e;    break;
                    }
                    pos_ += 2; col_ += 2;
                    continue;
                }
                if (d == '\n') { ++line_; col_ = 1; } else { ++col_; }
                s += d; ++pos_;
            }
            if (pos_ >= src_.size()) { error_ = "unterminated string literal"; had_error_ = true; return out; }
            ++pos_; ++col_;
            t.kind = Tok::String; t.text = s;
            out.push_back(t);
            continue;
        }
        // identifiers / keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t start = pos_;
            while (pos_ < src_.size() &&
                   (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) { ++pos_; }
            t.text = src_.substr(start, pos_ - start);
            auto it = keywords().find(t.text);
            t.kind = (it != keywords().end()) ? it->second : Tok::Ident;
            col_ += static_cast<i32>(t.text.size());
            out.push_back(t);
            continue;
        }
        // operators & punctuation
        std::size_t op_start = pos_;
        auto two = [&](const char* s) {
            return pos_ + 1 < src_.size() && src_[pos_] == s[0] && src_[pos_ + 1] == s[1];
        };
        if (two("==")) { t.kind = Tok::Eq; pos_ += 2; }
        else if (two("!=")) { t.kind = Tok::Neq; pos_ += 2; }
        else if (two("<=")) { t.kind = Tok::Le; pos_ += 2; }
        else if (two(">=")) { t.kind = Tok::Ge; pos_ += 2; }
        else if (two("&&")) { t.kind = Tok::AndAnd; pos_ += 2; }
        else if (two("||")) { t.kind = Tok::OrOr; pos_ += 2; }
        else if (two("+=")) { t.kind = Tok::PlusAssign; pos_ += 2; }
        else if (two("-=")) { t.kind = Tok::MinusAssign; pos_ += 2; }
        else if (two("*=")) { t.kind = Tok::StarAssign; pos_ += 2; }
        else if (two("/=")) { t.kind = Tok::SlashAssign; pos_ += 2; }
        else if (two("->")) { t.kind = Tok::Arrow; pos_ += 2; }
        else {
            ++pos_;
            switch (c) {
                case '(': t.kind = Tok::LParen; break;
                case ')': t.kind = Tok::RParen; break;
                case '{': t.kind = Tok::LBrace; break;
                case '}': t.kind = Tok::RBrace; break;
                case '[': t.kind = Tok::LBracket; break;
                case ']': t.kind = Tok::RBracket; break;
                case ',': t.kind = Tok::Comma; break;
                case ';': t.kind = Tok::Semicolon; break;
                case '.': t.kind = Tok::Dot; break;
                case ':': t.kind = Tok::Colon; break;
                case '?': t.kind = Tok::Question; break;
                case '+': t.kind = Tok::Plus; break;
                case '-': t.kind = Tok::Minus; break;
                case '*': t.kind = Tok::Star; break;
                case '/': t.kind = Tok::Slash; break;
                case '%': t.kind = Tok::Percent; break;
                case '=': t.kind = Tok::Assign; break;
                case '<': t.kind = Tok::Lt; break;
                case '>': t.kind = Tok::Gt; break;
                case '!': t.kind = Tok::Bang; break;
                case '|': t.kind = Tok::Pipe; break;
                default:
                    error_ = std::string("unexpected character '") + c + "'";
                    had_error_ = true;
                    return out;
            }
            t.text = std::string(1, c);
            ++col_;
        }
        if (t.text.empty()) { t.text = src_.substr(op_start, pos_ - op_start); col_ += static_cast<i32>(t.text.size()); }
        out.push_back(t);
    }
    Token end; end.kind = Tok::End; end.line = line_; end.col = col_;
    out.push_back(end);
    return out;
}

// ------------------------------------------------------------- Value ------
bool Value::truthy() const {
    switch (type) {
        case Type::Null:   return false;
        case Type::Bool:   return b;
        case Type::Number: return n != 0.0;
        case Type::String: return !s.empty();
        case Type::Array:  return arr && !arr->empty();
        case Type::Map:    return map && !map->empty();
        case Type::Function:
        case Type::Native:
        case Type::Object: return true;
    }
    return false;
}

std::string Value::to_string() const {
    switch (type) {
        case Type::Null:   return "null";
        case Type::Bool:   return b ? "true" : "false";
        case Type::Number: {
            if (n == static_cast<i64>(n) && std::fabs(n) < 1e15) return std::to_string(static_cast<i64>(n));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.6g", n);
            return buf;
        }
        case Type::String: return s;
        case Type::Array: {
            if (!arr) return "[]";
            std::string out = "[";
            for (std::size_t i = 0; i < arr->size(); ++i) {
                if (i) out += ", ";
                out += (*arr)[i].type == Type::String ? ("\"" + (*arr)[i].s + "\"") : (*arr)[i].to_string();
            }
            return out + "]";
        }
        case Type::Map: {
            if (!map) return "{}";
            std::string out = "{";
            bool first = true;
            for (auto& kv : *map) {
                if (!first) out += ", ";
                first = false;
                out += kv.first + std::string(": ") + kv.second.to_string();
            }
            return out + "}";
        }
        case Type::Function: return "<func " + (fn ? fn->name : std::string("anonymous")) + ">";
        case Type::Native:   return std::string("<native ") + native_name + ">";
        case Type::Object:   return obj && obj->klass ? (obj->klass->name + " instance") : "<object>";
    }
    return "?";
}

f64 Value::to_number() const {
    switch (type) {
        case Type::Number: return n;
        case Type::Bool:   return b ? 1.0 : 0.0;
        case Type::String: try { return std::stod(s); } catch (...) { return 0.0; }
        case Type::Null:   return 0.0;
        default:           return 0.0;
    }
}

const char* Value::type_name() const {
    switch (type) {
        case Type::Null: return "null";
        case Type::Bool: return "bool";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Map: return "map";
        case Type::Function: return "func";
        case Type::Native: return "native";
        case Type::Object: return obj && obj->klass ? obj->klass->name.c_str() : "object";
    }
    return "?";
}

bool Value::equals(const Value& o) const {
    if (type != o.type) {
        // numeric/bool coercion
        if ((type == Type::Number && o.type == Type::Bool) ||
            (type == Type::Bool && o.type == Type::Number))
            return to_number() == o.to_number();
        return false;
    }
    switch (type) {
        case Type::Null:   return true;
        case Type::Bool:   return b == o.b;
        case Type::Number: return std::fabs(n - o.n) < 1e-12;
        case Type::String: return s == o.s;
        case Type::Array:  return arr == o.arr;
        case Type::Map:    return map == o.map;
        case Type::Function: return fn == o.fn;
        case Type::Native:   return native_name == o.native_name;
        case Type::Object:   return obj == o.obj;
    }
    return false;
}

} // namespace prism::script
