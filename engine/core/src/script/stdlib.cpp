// PRISM ENGINE — script/stdlib.cpp : PrismScript standard library (100% offline).
#include "prism/script/prismscript.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>

namespace prism::script {

namespace {
Value arg(const std::vector<Value>& a, std::size_t i) {
    return i < a.size() ? a[i] : Value::null();
}
f64 num(const std::vector<Value>& a, std::size_t i) { return arg(a, i).to_number(); }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
} // namespace

void Interpreter::register_stdlib() {
    // ---- literals ---------------------------------------------------------
    define_native("__array", [](Interpreter&, std::vector<Value>& a) {
        return Value::array(std::make_shared<ValueArray>(a));
    });
    define_native("__map", [](Interpreter&, std::vector<Value>& a) {
        auto m = std::make_shared<ValueMap>();
        for (std::size_t i = 0; i + 1 < a.size(); i += 2) (*m)[a[i].to_string()] = a[i + 1];
        return Value::make_map(m);
    });

    // ---- io ---------------------------------------------------------------
    define_native("print", [this](Interpreter&, std::vector<Value>& a) {
        std::string out;
        for (std::size_t i = 0; i < a.size(); ++i) { if (i) out += "\t"; out += a[i].to_string(); }
        print(out);
        return Value::null();
    });
    define_native("println", [this](Interpreter&, std::vector<Value>& a) {
        std::string out;
        for (std::size_t i = 0; i < a.size(); ++i) { if (i) out += "\t"; out += a[i].to_string(); }
        print(out);
        return Value::null();
    });

    // ---- types ------------------------------------------------------------
    define_native("type",  [](Interpreter&, std::vector<Value>& a) { return Value::string(arg(a, 0).type_name()); });
    define_native("str",   [](Interpreter&, std::vector<Value>& a) { return Value::string(arg(a, 0).to_string()); });
    define_native("num",   [](Interpreter&, std::vector<Value>& a) { return Value::number(arg(a, 0).to_number()); });
    define_native("int",   [](Interpreter&, std::vector<Value>& a) { return Value::number(static_cast<f64>(static_cast<i64>(arg(a,0).to_number()))); });
    define_native("bool",  [](Interpreter&, std::vector<Value>& a) { return Value::boolean(arg(a, 0).truthy()); });

    // ---- math -------------------------------------------------------------
    auto math1 = [](f64 (*f)(f64)) {
        return [f](Interpreter&, std::vector<Value>& a) { return Value::number(f(num(a, 0))); };
    };
    define_native("abs",   [](Interpreter&, std::vector<Value>& a) { return Value::number(std::fabs(num(a, 0))); });
    define_native("sqrt",  math1(std::sqrt));
    define_native("sin",   math1(std::sin));
    define_native("cos",   math1(std::cos));
    define_native("tan",   math1(std::tan));
    define_native("atan2", [](Interpreter&, std::vector<Value>& a) { return Value::number(std::atan2(num(a,0), num(a,1))); });
    define_native("floor", math1(std::floor));
    define_native("ceil",  math1(std::ceil));
    define_native("round", [](Interpreter&, std::vector<Value>& a) { return Value::number(std::round(num(a, 0))); });
    define_native("log",   math1(std::log));
    define_native("exp",   math1(std::exp));
    define_native("pow",   [](Interpreter&, std::vector<Value>& a) { return Value::number(std::pow(num(a,0), num(a,1))); });
    define_native("min",   [](Interpreter&, std::vector<Value>& a) {
        f64 r = num(a, 0); for (std::size_t i = 1; i < a.size(); ++i) r = std::min(r, num(a, i)); return Value::number(r); });
    define_native("max",   [](Interpreter&, std::vector<Value>& a) {
        f64 r = num(a, 0); for (std::size_t i = 1; i < a.size(); ++i) r = std::max(r, num(a, i)); return Value::number(r); });
    define_native("clamp", [](Interpreter&, std::vector<Value>& a) {
        return Value::number(std::max(num(a, 1), std::min(num(a, 0), num(a, 2)))); });
    define_native("lerp",  [](Interpreter&, std::vector<Value>& a) {
        return Value::number(num(a,0) + (num(a,1) - num(a,0)) * num(a,2)); });
    define_native("rand",  [](Interpreter&, std::vector<Value>&) {
        std::uniform_real_distribution<f64> d(0.0, 1.0); return Value::number(d(rng())); });
    define_native("randRange", [](Interpreter&, std::vector<Value>& a) {
        std::uniform_real_distribution<f64> d(num(a,0), num(a,1)); return Value::number(d(rng())); });
    define_native("randInt", [](Interpreter&, std::vector<Value>& a) {
        std::uniform_int_distribution<i64> d(static_cast<i64>(num(a,0)), static_cast<i64>(num(a,1)));
        return Value::number(static_cast<f64>(d(rng()))); });

    // ---- strings ----------------------------------------------------------
    define_native("len", [](Interpreter&, std::vector<Value>& a) {
        Value v = arg(a, 0);
        switch (v.type) {
            case Value::Type::String: return Value::number(static_cast<f64>(v.s.size()));
            case Value::Type::Array:  return Value::number(static_cast<f64>(v.arr ? v.arr->size() : 0));
            case Value::Type::Map:    return Value::number(static_cast<f64>(v.map ? v.map->size() : 0));
            default: return Value::number(0);
        }
    });
    define_native("substr", [](Interpreter&, std::vector<Value>& a) {
        const std::string& s = a[0].s;
        i64 start = static_cast<i64>(num(a, 1));
        i64 count = a.size() > 2 ? static_cast<i64>(num(a, 2)) : static_cast<i64>(s.size());
        if (start < 0) start = 0;
        if (start > static_cast<i64>(s.size())) start = static_cast<i64>(s.size());
        return Value::string(s.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(count)));
    });
    define_native("indexOf", [](Interpreter&, std::vector<Value>& a) {
        auto p = a[0].s.find(a[1].s);
        return Value::number(p == std::string::npos ? -1.0 : static_cast<f64>(p));
    });
    define_native("split", [](Interpreter&, std::vector<Value>& a) {
        auto out = std::make_shared<ValueArray>();
        const std::string& s = a[0].s;
        const std::string& d = a.size() > 1 ? a[1].s : std::string(",");
        std::size_t start = 0;
        for (;;) {
            auto p = s.find(d, start);
            if (p == std::string::npos) { out->push_back(Value::string(s.substr(start))); break; }
            out->push_back(Value::string(s.substr(start, p - start)));
            start = p + d.size();
        }
        return Value::array(out);
    });
    define_native("join", [](Interpreter&, std::vector<Value>& a) {
        if (a.empty() || a[0].type != Value::Type::Array || !a[0].arr) return Value::string("");
        std::string sep = a.size() > 1 ? a[1].s : std::string("");
        std::string out;
        for (std::size_t i = 0; i < a[0].arr->size(); ++i) { if (i) out += sep; out += (*a[0].arr)[i].to_string(); }
        return Value::string(out);
    });
    define_native("contains", [](Interpreter&, std::vector<Value>& a) {
        if (a[0].type == Value::Type::String) return Value::boolean(a[0].s.find(a[1].s) != std::string::npos);
        if (a[0].type == Value::Type::Array && a[0].arr) {
            for (auto& v : *a[0].arr) if (v.equals(a[1])) return Value::boolean(true);
        }
        if (a[0].type == Value::Type::Map && a[0].map)
            return Value::boolean(a[0].map->count(a[1].to_string()) > 0);
        return Value::boolean(false);
    });

    // ---- arrays / maps ----------------------------------------------------
    define_native("push", [](Interpreter&, std::vector<Value>& a) {
        if (a.size() >= 2 && a[0].type == Value::Type::Array && a[0].arr) {
            a[0].arr->push_back(a[1]);
            return Value::number(static_cast<f64>(a[0].arr->size()));
        }
        return Value::null();
    });
    define_native("pop", [](Interpreter&, std::vector<Value>& a) {
        if (!a.empty() && a[0].type == Value::Type::Array && a[0].arr && !a[0].arr->empty()) {
            Value v = a[0].arr->back();
            a[0].arr->pop_back();
            return v;
        }
        return Value::null();
    });
    define_native("removeAt", [](Interpreter&, std::vector<Value>& a) {
        if (a.size() >= 2 && a[0].type == Value::Type::Array && a[0].arr) {
            i64 i = static_cast<i64>(num(a, 1));
            if (i >= 0 && i < static_cast<i64>(a[0].arr->size())) {
                Value v = (*a[0].arr)[static_cast<std::size_t>(i)];
                a[0].arr->erase(a[0].arr->begin() + static_cast<std::ptrdiff_t>(i));
                return v;
            }
        }
        return Value::null();
    });
    define_native("range", [](Interpreter&, std::vector<Value>& a) {
        auto out = std::make_shared<ValueArray>();
        f64 start = a.size() > 1 ? num(a, 0) : 0.0;
        f64 end   = a.size() > 1 ? num(a, 1) : num(a, 0);
        f64 step  = a.size() > 2 ? num(a, 2) : 1.0;
        if (std::fabs(step) < 1e-12) step = 1.0;
        if (step > 0) for (f64 v = start; v < end; v += step) out->push_back(Value::number(v));
        else          for (f64 v = start; v > end; v += step) out->push_back(Value::number(v));
        return Value::array(out);
    });
    define_native("keys", [](Interpreter&, std::vector<Value>& a) {
        auto out = std::make_shared<ValueArray>();
        if (!a.empty() && a[0].type == Value::Type::Map && a[0].map)
            for (auto& kv : *a[0].map) out->push_back(Value::string(kv.first));
        return Value::array(out);
    });
    define_native("map", [](Interpreter& vm, std::vector<Value>& a) {
        auto out = std::make_shared<ValueArray>();
        if (a.size() < 2 || a[0].type != Value::Type::Array || !a[0].arr) return Value::array(out);
        for (std::size_t i = 0; i < a[0].arr->size(); ++i) {
            std::vector<Value> args{(*a[0].arr)[i], Value::number(static_cast<f64>(i))};
            out->push_back(vm.call_value(a[1], args));
        }
        return Value::array(out);
    });
    define_native("filter", [](Interpreter& vm, std::vector<Value>& a) {
        auto out = std::make_shared<ValueArray>();
        if (a.size() < 2 || a[0].type != Value::Type::Array || !a[0].arr) return Value::array(out);
        for (auto& item : *a[0].arr) {
            std::vector<Value> args{item};
            if (vm.call_value(a[1], args).truthy()) out->push_back(item);
        }
        return Value::array(out);
    });
    define_native("forEach", [](Interpreter& vm, std::vector<Value>& a) {
        if (a.size() < 2 || a[0].type != Value::Type::Array || !a[0].arr) return Value::null();
        for (auto& item : *a[0].arr) { std::vector<Value> args{item}; vm.call_value(a[1], args); }
        return Value::null();
    });

    // ---- vectors (math helpers mirrored in engine/core) -------------------
    define_native("Vec2", [](Interpreter&, std::vector<Value>& a) {
        auto m = std::make_shared<ValueMap>();
        (*m)["x"] = Value::number(num(a, 0));
        (*m)["y"] = Value::number(a.size() > 1 ? num(a, 1) : 0.0);
        (*m)["__type"] = Value::string("Vec2");
        return Value::make_map(m);
    });
    define_native("Vec3", [](Interpreter&, std::vector<Value>& a) {
        auto m = std::make_shared<ValueMap>();
        (*m)["x"] = Value::number(num(a, 0));
        (*m)["y"] = Value::number(a.size() > 1 ? num(a, 1) : 0.0);
        (*m)["z"] = Value::number(a.size() > 2 ? num(a, 2) : 0.0);
        (*m)["__type"] = Value::string("Vec3");
        return Value::make_map(m);
    });
    define_native("vecAdd", [](Interpreter&, std::vector<Value>& a) {
        auto out = std::make_shared<ValueMap>();
        if (a.size() < 2 || !a[0].map || !a[1].map) return Value::make_map(out);
        for (auto& kv : *a[0].map) {
            if (kv.first == "__type") { (*out)[kv.first] = kv.second; continue; }
            auto it = a[1].map->find(kv.first);
            (*out)[kv.first] = Value::number(kv.second.to_number() + (it != a[1].map->end() ? it->second.to_number() : 0.0));
        }
        return Value::make_map(out);
    });
    define_native("vecScale", [](Interpreter&, std::vector<Value>& a) {
        auto out = std::make_shared<ValueMap>();
        if (a.empty() || !a[0].map) return Value::make_map(out);
        f64 s = a.size() > 1 ? num(a, 1) : 1.0;
        for (auto& kv : *a[0].map)
            (*out)[kv.first] = kv.first == "__type" ? kv.second : Value::number(kv.second.to_number() * s);
        return Value::make_map(out);
    });
    define_native("vecLen", [](Interpreter&, std::vector<Value>& a) {
        if (a.empty() || !a[0].map) return Value::number(0);
        f64 sum = 0;
        for (auto& kv : *a[0].map) if (kv.first != "__type") sum += kv.second.to_number() * kv.second.to_number();
        return Value::number(std::sqrt(sum));
    });
    define_native("distance", [](Interpreter&, std::vector<Value>& a) {
        if (a.size() < 2 || !a[0].map || !a[1].map) return Value::number(0);
        f64 sum = 0;
        for (auto& kv : *a[0].map) {
            if (kv.first == "__type") continue;
            auto it = a[1].map->find(kv.first);
            f64 d = kv.second.to_number() - (it != a[1].map->end() ? it->second.to_number() : 0.0);
            sum += d * d;
        }
        return Value::number(std::sqrt(sum));
    });

    // ---- time -------------------------------------------------------------
    define_native("time", [](Interpreter&, std::vector<Value>&) {
        return Value::number(std::chrono::duration<f64>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    });

    // ---- constants --------------------------------------------------------
    define_global("PI",  Value::number(3.14159265358979323846));
    define_global("TAU", Value::number(6.28318530717958647692));
    define_global("E",   Value::number(2.71828182845904523536));
    define_global("true",  Value::boolean(true));
    define_global("false", Value::boolean(false));
    define_global("null",  Value::null());
    define_global("PRISM_VERSION", Value::string(kEngineVersion.str()));
    define_global("PLATFORM", Value::string(
#ifdef PRISM_PLATFORM_ANDROID
        "android"
#else
        "host"
#endif
    ));
}

} // namespace prism::script
