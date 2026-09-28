#include "prism/script/prismscript.h"
#include "prism/core/log.h"
#include <chrono>
#include <cmath>
#include <cctype>

namespace prism::script {

// ----------------------------------------------------------- Environment ---
bool Environment::define(const std::string& name, Value v, bool is_const) {
    auto it = vars_.find(name);
    if (it != vars_.end() && consts_.count(name) && consts_[name]) return false;
    vars_[name] = std::move(v);
    consts_[name] = is_const;
    return true;
}

bool Environment::assign(const std::string& name, const Value& v) {
    Environment* env = this;
    while (env) {
        auto it = env->vars_.find(name);
        if (it != env->vars_.end()) {
            auto c = env->consts_.find(name);
            if (c != env->consts_.end() && c->second) return false;   // immutable
            it->second = v;
            return true;
        }
        env = env->parent_.get();
    }
    return false;
}

bool Environment::lookup(const std::string& name, Value& out) const {
    const Environment* env = this;
    while (env) {
        auto it = env->vars_.find(name);
        if (it != env->vars_.end()) { out = it->second; return true; }
        env = env->parent_.get();
    }
    return false;
}

bool Environment::is_const(const std::string& name) const {
    const Environment* env = this;
    while (env) {
        auto it = env->vars_.find(name);
        if (it != env->vars_.end()) {
            auto c = env->consts_.find(name);
            return c != env->consts_.end() && c->second;
        }
        env = env->parent_.get();
    }
    return false;
}

std::vector<std::string> Environment::names() const {
    std::vector<std::string> out;
    out.reserve(vars_.size());
    for (auto& kv : vars_) out.push_back(kv.first);
    return out;
}

// ----------------------------------------------------------- Interpreter ---
Interpreter::Interpreter() {
    globals_ = std::make_shared<Environment>();
    register_stdlib();
}

Interpreter::~Interpreter() = default;

void Interpreter::print(const std::string& s) {
    if (print_sink_) print_sink_(s);
    else PRISM_INFO("prismscript", s);
}

void Interpreter::define_native(const std::string& name, NativeFn fn) {
    Value v;
    v.type = Value::Type::Native;
    v.native = std::move(fn);
    v.native_name = name;
    globals_->define(name, v);
}

void Interpreter::define_global(const std::string& name, Value v) { globals_->define(name, std::move(v)); }

bool Interpreter::get_global(const std::string& name, Value& out) const { return globals_->lookup(name, out); }

std::vector<std::string> Interpreter::class_names() const {
    std::vector<std::string> out;
    out.reserve(classes_.size());
    for (auto& kv : classes_) out.push_back(kv.first);
    return out;
}

bool Interpreter::run_source(const std::string& source, const std::string& origin) {
    Lexer lex(source);
    auto tokens = lex.tokenize();
    if (lex.had_error()) {
        last_error_ = origin + std::string(": ") + lex.error();
        last_error_line_ = 0;
        PRISM_ERROR("prismscript", last_error_);
        return false;
    }
    Parser parser(std::move(tokens));
    auto program = parser.parse_program();
    if (parser.had_error()) {
        last_error_ = origin + std::string(": ") + parser.error();
        PRISM_ERROR("prismscript", last_error_);
        return false;
    }
    auto t0 = std::chrono::steady_clock::now();
    try {
        if (!run_statements(program, globals_)) return false;
    } catch (const ScriptError& e) {
        last_error_ = origin + std::string(":") + std::to_string(e.line) + std::string(": ") + e.what();
        last_error_line_ = e.line;
        PRISM_ERROR("prismscript", last_error_);
        return false;
    }
    auto t1 = std::chrono::steady_clock::now();
    stats_.ms += std::chrono::duration<f64, std::milli>(t1 - t0).count();
    return true;
}

bool Interpreter::run_statements(const std::vector<StmtPtr>& stmts, std::shared_ptr<Environment> env) {
    for (auto& s : stmts) {
        auto r = execute(s, *env);
        if (r.flow == Flow::Return) {
            Value ret = r.value;
            globals_->define("__return__", ret);
            return true;
        }
    }
    return true;
}

bool Interpreter::call(const std::string& name, std::vector<Value> args, Value& out) {
    Value fn;
    if (!globals_->lookup(name, fn)) {
        last_error_ = std::string("no global function '") + name + "'";
        return false;
    }
    try {
        out = call_value(fn, std::move(args));
        return true;
    } catch (const ScriptError& e) {
        last_error_ = std::string(e.what()) + std::string(" (line ") + std::to_string(e.line) + ")";
        last_error_line_ = e.line;
        PRISM_ERROR("prismscript", last_error_);
        return false;
    }
}

void Interpreter::bind_class_methods(const std::shared_ptr<ClassDef>& c) {
    (void)c;   // methods are already Function objects bound through ClassDef::methods
}

bool Interpreter::instantiate(const std::string& class_name, std::vector<Value> args,
                              std::shared_ptr<Instance>& out) {
    auto it = classes_.find(class_name);
    if (it == classes_.end()) { last_error_ = std::string("unknown class '") + class_name + "'"; return false; }
    auto inst = std::make_shared<Instance>();
    inst->klass = it->second;
    // field initialisers, base first
    std::vector<std::shared_ptr<ClassDef>> chain;
    for (auto c = inst->klass; c; c = c->base) chain.push_back(c);
    for (auto ci = chain.rbegin(); ci != chain.rend(); ++ci) {
        for (auto& f : (*ci)->fields) {
            Value v;
            if (f->init) {
                auto env = std::make_shared<Environment>(globals_);
                env->define("this", [&] { Value t; t.type = Value::Type::Object; t.obj = inst; return t; }());
                v = evaluate(f->init, *env);
            }
            inst->fields[f->name] = v;
        }
    }
    out = inst;
    if (!args.empty() || inst->klass->has_method("ctor")) {
        Value unused;
        if (inst->klass->has_method("ctor")) call_method(inst, "ctor", std::move(args), unused);
    }
    return true;
}

bool Interpreter::call_method(const std::shared_ptr<Instance>& inst, const std::string& method,
                              std::vector<Value> args, Value& out) {
    if (!inst || !inst->klass) { last_error_ = "null instance"; return false; }
    auto fn = inst->klass->find_method(method);
    if (!fn) { last_error_ = std::string("no method '") + method + std::string("' on ") + inst->klass->name; return false; }
    Value callee;
    callee.type = Value::Type::Function;
    callee.fn = fn;
    try {
        out = call_value(callee, std::move(args), inst);
        return true;
    } catch (const ScriptError& e) {
        last_error_ = inst->klass->name + std::string(".") + method + std::string(": ") + e.what();
        last_error_line_ = e.line;
        PRISM_ERROR("prismscript", last_error_);
        return false;
    }
}

Value Interpreter::call_value(const Value& callee, std::vector<Value> args,
                              const std::shared_ptr<Instance>& this_inst) {
    if (callee.type == Value::Type::Native) {
        ++stats_.native_calls;
        return callee.native(*this, args);
    }
    if (callee.type != Value::Type::Function || !callee.fn)
        throw ScriptError(std::string("value is not callable (") + callee.type_name() + ")");

    auto& f = *callee.fn;
    if (++depth_ > max_depth_) { --depth_; throw ScriptError("maximum call depth exceeded"); }
    ++stats_.calls;

    auto env = std::make_shared<Environment>(f.closure ? f.closure : globals_);
    for (std::size_t i = 0; i < f.params.size(); ++i)
        env->define(f.params[i], i < args.size() ? args[i] : Value::null());

    auto self = this_inst ? this_inst : f.bound_this;
    if (self) {
        Value t; t.type = Value::Type::Object; t.obj = self;
        env->define("this", t);
    }

    Value result;
    try {
        if (f.body) {
            auto r = execute(f.body, *env);
            if (r.flow == Flow::Return) result = r.value;
        }
    } catch (...) {
        --depth_;
        throw;
    }
    --depth_;
    return result;
}

// ------------------------------------------------------------- statements ---
Interpreter::FlowResult Interpreter::execute(const StmtPtr& s, Environment& env) {
    if (!s) return {};
    if (dynamic_cast<BlockStmt*>(s.get())) {
        auto* b = static_cast<BlockStmt*>(s.get());
        auto scope = std::make_shared<Environment>(env.shared_from_this());
        for (auto& st : b->statements) {
            auto r = execute(st, *scope);
            if (r.flow != Flow::None) return r;
        }
        return {};
    }
    if (dynamic_cast<VarDecl*>(s.get())) {
        auto* v = static_cast<VarDecl*>(s.get());
        Value init = v->init ? evaluate(v->init, env) : Value::null();
        if (!env.define(v->name, init, v->is_const))
            throw ScriptError(std::string("cannot reassign constant '") + v->name + "'", s->line);
        return {};
    }
    if (dynamic_cast<ExprStmt*>(s.get())) {
        evaluate(static_cast<ExprStmt*>(s.get())->expr, env);
        return {};
    }
    if (dynamic_cast<IfStmt*>(s.get())) {
        auto* i = static_cast<IfStmt*>(s.get());
        if (evaluate(i->cond, env).truthy()) return execute(i->then_branch, env);
        if (i->else_branch) return execute(i->else_branch, env);
        return {};
    }
    if (dynamic_cast<WhileStmt*>(s.get())) {
        auto* w = static_cast<WhileStmt*>(s.get());
        while (evaluate(w->cond, env).truthy()) {
            auto r = execute(w->body, env);
            if (r.flow == Flow::Break) break;
            if (r.flow == Flow::Return) return r;
        }
        return {};
    }
    if (dynamic_cast<ForStmt*>(s.get())) {
        auto* f = static_cast<ForStmt*>(s.get());
        auto scope = std::make_shared<Environment>(env.shared_from_this());
        if (f->init) execute(f->init, *scope);
        for (;;) {
            if (f->cond && !evaluate(f->cond, *scope).truthy()) break;
            auto r = execute(f->body, *scope);
            if (r.flow == Flow::Break) break;
            if (r.flow == Flow::Return) return r;
            if (f->step) evaluate(f->step, *scope);
        }
        return {};
    }
    if (dynamic_cast<ForEachStmt*>(s.get())) {
        auto* fe = static_cast<ForEachStmt*>(s.get());
        Value iter = evaluate(fe->iterable, env);
        auto scope = std::make_shared<Environment>(env.shared_from_this());
        auto step = [&](const Value& item) -> Flow {
            scope->define(fe->var, item);
            auto r = execute(fe->body, *scope);
            return r.flow;
        };
        if (iter.type == Value::Type::Array && iter.arr) {
            std::vector<Value> copy = *iter.arr;
            for (auto& item : copy) {
                Flow f = step(item);
                if (f == Flow::Break) break;
                if (f == Flow::Return) { FlowResult r; r.flow = f; return r; }
            }
        } else if (iter.type == Value::Type::Map && iter.map) {
            std::vector<std::string> keys;
            for (auto& kv : *iter.map) keys.push_back(kv.first);
            for (auto& k : keys) {
                Flow f = step(Value::string(k));
                if (f == Flow::Break) break;
                if (f == Flow::Return) { FlowResult r; r.flow = f; return r; }
            }
        } else if (iter.type == Value::Type::String) {
            for (char c : iter.s) {
                Flow f = step(Value::string(std::string(1, c)));
                if (f == Flow::Break) break;
                if (f == Flow::Return) { FlowResult r; r.flow = f; return r; }
            }
        } else {
            throw ScriptError(std::string("cannot iterate over ") + std::string(iter.type_name()), fe->line);
        }
        return {};
    }
    if (dynamic_cast<ReturnStmt*>(s.get())) {
        auto* r = static_cast<ReturnStmt*>(s.get());
        FlowResult out;
        out.flow = Flow::Return;
        out.value = r->value ? evaluate(r->value, env) : Value::null();
        return out;
    }
    if (dynamic_cast<BreakStmt*>(s.get()))    { FlowResult r; r.flow = Flow::Break;    return r; }
    if (dynamic_cast<ContinueStmt*>(s.get())) { FlowResult r; r.flow = Flow::Continue; return r; }
    if (dynamic_cast<FuncDecl*>(s.get())) {
        auto* d = static_cast<FuncDecl*>(s.get());
        auto f = std::make_shared<Function>();
        f->name = d->name;
        f->params = d->params;
        f->body = d->body;
        f->closure = env.shared_from_this();
        Value v; v.type = Value::Type::Function; v.fn = f;
        env.define(d->name, v);
        return {};
    }
    if (dynamic_cast<ClassDecl*>(s.get())) {
        auto* d = static_cast<ClassDecl*>(s.get());
        auto c = std::make_shared<ClassDef>();
        c->name = d->name;
        if (!d->base.empty()) {
            auto it = classes_.find(d->base);
            if (it == classes_.end()) throw ScriptError(std::string("unknown base class '") + d->base + "'", d->line);
            c->base = it->second;
        }
        c->fields = d->fields;
        for (auto& m : d->methods) {
            auto f = std::make_shared<Function>();
            f->name = m->name;
            f->params = m->params;
            f->body = m->body;
            f->closure = env.shared_from_this();
            c->methods[m->name] = f;
        }
        classes_[c->name] = c;
        bind_class_methods(c);
        // expose the class as a global so `new Foo()` and `Foo` both work
        Value v; v.type = Value::Type::String; v.s = c->name;
        globals_->define(c->name, v);
        return {};
    }
    if (dynamic_cast<ImportStmt*>(s.get())) {
        // Offline module resolution happens through the AssetDB host hook.
        return {};
    }
    return {};
}

// ----------------------------------------------------------- expressions ----
Value Interpreter::evaluate(const ExprPtr& e, Environment& env) {
    if (!e) return Value::null();

    if (dynamic_cast<Literal*>(e.get())) {
        auto* l = static_cast<Literal*>(e.get());
        switch (l->kind) {
            case Literal::Kind::Null:   return Value::null();
            case Literal::Kind::Bool:   return Value::boolean(l->b);
            case Literal::Kind::Number: return Value::number(l->n);
            case Literal::Kind::String: return Value::string(l->s);
        }
    }
    if (dynamic_cast<Ident*>(e.get())) {
        auto* id = static_cast<Ident*>(e.get());
        Value v;
        if (env.lookup(id->name, v)) return v;
        throw ScriptError(std::string("undefined identifier '") + id->name + "'", e->line);
    }
    if (dynamic_cast<ThisExpr*>(e.get())) {
        Value v;
        if (!env.lookup("this", v)) throw ScriptError("'this' used outside of a method", e->line);
        return v;
    }
    if (dynamic_cast<Unary*>(e.get())) {
        auto* u = static_cast<Unary*>(e.get());
        Value v = evaluate(u->operand, env);
        if (u->op == "-") return Value::number(-v.to_number());
        if (u->op == "+") return Value::number(v.to_number());
        return Value::boolean(!v.truthy());
    }
    if (dynamic_cast<Binary*>(e.get())) {
        auto* b = static_cast<Binary*>(e.get());
        return eval_binary(b->op, evaluate(b->left, env), evaluate(b->right, env), b->line);
    }
    if (dynamic_cast<Logical*>(e.get())) {
        auto* l = static_cast<Logical*>(e.get());
        Value left = evaluate(l->left, env);
        if (l->op == "&&") return left.truthy() ? evaluate(l->right, env) : left;
        return left.truthy() ? left : evaluate(l->right, env);
    }
    if (dynamic_cast<Ternary*>(e.get())) {
        auto* t = static_cast<Ternary*>(e.get());
        return evaluate(t->cond, env).truthy() ? evaluate(t->when_true, env) : evaluate(t->when_false, env);
    }
    if (dynamic_cast<Assign*>(e.get())) {
        auto* a = static_cast<Assign*>(e.get());
        Value rhs = evaluate(a->value, env);
        if (a->op != "=") {
            Value cur;
            if (dynamic_cast<Ident*>(a->target.get())) {
                if (!env.lookup(static_cast<Ident*>(a->target.get())->name, cur))
                    throw ScriptError("undefined identifier in assignment", a->line);
            } else if (dynamic_cast<Member*>(a->target.get())) {
                auto* m = static_cast<Member*>(a->target.get());
                cur = get_member(evaluate(m->object, env), m->name, a->line);
            } else if (dynamic_cast<Index*>(a->target.get())) {
                auto* ix = static_cast<Index*>(a->target.get());
                Value obj = evaluate(ix->object, env);
                Value key = evaluate(ix->index, env);
                cur = get_member(obj, key.to_string(), a->line);
            }
            std::string op = a->op.substr(0, a->op.size() - 1);
            rhs = eval_binary(op, cur, rhs, a->line);
        }
        assign_target(a->target, rhs, env, a->line);
        return rhs;
    }
    if (dynamic_cast<Index*>(e.get())) {
        auto* ix = static_cast<Index*>(e.get());
        Value obj = evaluate(ix->object, env);
        Value key = evaluate(ix->index, env);
        if (obj.type == Value::Type::Array && obj.arr) {
            i64 i = static_cast<i64>(key.to_number());
            if (i < 0) i += static_cast<i64>(obj.arr->size());
            if (i < 0 || i >= static_cast<i64>(obj.arr->size())) return Value::null();
            return (*obj.arr)[i];
        }
        if (obj.type == Value::Type::Map && obj.map) {
            auto it = obj.map->find(key.to_string());
            return it == obj.map->end() ? Value::null() : it->second;
        }
        if (obj.type == Value::Type::String) {
            i64 i = static_cast<i64>(key.to_number());
            if (i < 0 || i >= static_cast<i64>(obj.s.size())) return Value::null();
            return Value::string(std::string(1, obj.s[static_cast<std::size_t>(i)]));
        }
        if (obj.type == Value::Type::Object && obj.obj) {
            auto it = obj.obj->fields.find(key.to_string());
            return it == obj.obj->fields.end() ? Value::null() : it->second;
        }
        throw ScriptError(std::string("cannot index into ") + std::string(obj.type_name()), e->line);
    }
    if (dynamic_cast<Member*>(e.get())) {
        auto* m = static_cast<Member*>(e.get());
        Value obj = evaluate(m->object, env);
        return get_member(obj, m->name, m->line);
    }
    if (dynamic_cast<Call*>(e.get())) {
        auto* c = static_cast<Call*>(e.get());
        std::vector<Value> args;
        args.reserve(c->args.size());
        for (auto& a : c->args) args.push_back(evaluate(a, env));
        std::shared_ptr<Instance> this_inst;
        if (dynamic_cast<Member*>(c->callee.get())) {
            auto* m = static_cast<Member*>(c->callee.get());
            Value obj = evaluate(m->object, env);
            if (obj.type == Value::Type::Object && obj.obj) this_inst = obj.obj;
            Value callee = get_member(obj, m->name, c->line);
            return call_value(callee, std::move(args), this_inst);
        }
        Value callee = evaluate(c->callee, env);
        return call_value(callee, std::move(args));
    }
    if (dynamic_cast<Lambda*>(e.get())) {
        auto* l = static_cast<Lambda*>(e.get());
        auto f = std::make_shared<Function>();
        f->name = "lambda";
        f->params = l->params;
        f->body = l->body;
        f->closure = env.shared_from_this();
        Value v; v.type = Value::Type::Function; v.fn = f;
        return v;
    }
    if (dynamic_cast<NewExpr*>(e.get())) {
        auto* n = static_cast<NewExpr*>(e.get());
        std::vector<Value> args;
        for (auto& a : n->args) args.push_back(evaluate(a, env));
        std::shared_ptr<Instance> inst;
        if (!instantiate(n->class_name, std::move(args), inst))
            throw ScriptError(last_error_, n->line);
        Value v; v.type = Value::Type::Object; v.obj = inst;
        return v;
    }
    throw ScriptError("unsupported expression", e->line);
}

Value Interpreter::eval_binary(const std::string& op, const Value& l, const Value& r, i32 line) {
    if (op == "+") {
        if (l.type == Value::Type::String || r.type == Value::Type::String)
            return Value::string(l.to_string() + r.to_string());
        if (l.type == Value::Type::Array && r.type == Value::Type::Array) {
            auto out = std::make_shared<ValueArray>(*l.arr);
            out->insert(out->end(), r.arr->begin(), r.arr->end());
            return Value::array(out);
        }
        return Value::number(l.to_number() + r.to_number());
    }
    if (op == "-") return Value::number(l.to_number() - r.to_number());
    if (op == "*") {
        if (l.type == Value::Type::String && r.type == Value::Type::Number) {
            std::string out;
            i64 n = static_cast<i64>(r.n);
            for (i64 i = 0; i < n; ++i) out += l.s;
            return Value::string(out);
        }
        return Value::number(l.to_number() * r.to_number());
    }
    if (op == "/") {
        f64 d = r.to_number();
        if (std::fabs(d) < 1e-12) throw ScriptError("division by zero", line);
        return Value::number(l.to_number() / d);
    }
    if (op == "%") {
        f64 d = r.to_number();
        if (std::fabs(d) < 1e-12) throw ScriptError("modulo by zero", line);
        return Value::number(std::fmod(l.to_number(), d));
    }
    if (op == "==") return Value::boolean(l.equals(r));
    if (op == "!=") return Value::boolean(!l.equals(r));
    if (op == "<")  return Value::boolean(l.to_number() <  r.to_number());
    if (op == ">")  return Value::boolean(l.to_number() >  r.to_number());
    if (op == "<=") return Value::boolean(l.to_number() <= r.to_number());
    if (op == ">=") return Value::boolean(l.to_number() >= r.to_number());
    throw ScriptError(std::string("unknown operator '") + op + "'", line);
}

void Interpreter::assign_target(const ExprPtr& target, const Value& v, Environment& env, i32 line) {
    if (dynamic_cast<Ident*>(target.get())) {
        auto* id = static_cast<Ident*>(target.get());
        if (!env.assign(id->name, v)) {
            if (env.is_const(id->name)) throw ScriptError(std::string("cannot reassign constant '") + id->name + "'", line);
            env.define(id->name, v);   // implicit global
        }
        return;
    }
    if (dynamic_cast<Member*>(target.get())) {
        auto* m = static_cast<Member*>(target.get());
        Value obj = evaluate(m->object, env);
        set_member(obj, m->name, v, line);
        return;
    }
    if (dynamic_cast<Index*>(target.get())) {
        auto* ix = static_cast<Index*>(target.get());
        Value obj = evaluate(ix->object, env);
        Value key = evaluate(ix->index, env);
        if (obj.type == Value::Type::Array && obj.arr) {
            i64 i = static_cast<i64>(key.to_number());
            if (i < 0) i += static_cast<i64>(obj.arr->size());
            if (i < 0 || i >= static_cast<i64>(obj.arr->size()))
                throw ScriptError("array index out of range", line);
            (*obj.arr)[static_cast<std::size_t>(i)] = v;
            return;
        }
        if (obj.type == Value::Type::Map && obj.map) { (*obj.map)[key.to_string()] = v; return; }
        if (obj.type == Value::Type::Object && obj.obj) { obj.obj->fields[key.to_string()] = v; return; }
        throw ScriptError(std::string("cannot assign into ") + std::string(obj.type_name()), line);
    }
    throw ScriptError("invalid assignment target", line);
}

Value Interpreter::get_member(const Value& obj, const std::string& name, i32 line) {
    if (obj.type == Value::Type::Object && obj.obj) {
        auto it = obj.obj->fields.find(name);
        if (it != obj.obj->fields.end()) return it->second;
        if (obj.obj->klass) {
            auto m = obj.obj->klass->find_method(name);
            if (m) { Value v; v.type = Value::Type::Function; v.fn = m; v.fn->bound_this = obj.obj; return v; }
        }
        return Value::null();
    }
    if (obj.type == Value::Type::Map && obj.map) {
        auto it = obj.map->find(name);
        return it == obj.map->end() ? Value::null() : it->second;
    }
    if (obj.type == Value::Type::String) {
        if (name == "length") return Value::number(static_cast<f64>(obj.s.size()));
        if (name == "upper")  { Value v; v.type = Value::Type::Native; v.native_name = "str.upper";
            std::string s = obj.s;
            v.native = [s](Interpreter&, std::vector<Value>&) {
                std::string out = s;
                for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return Value::string(out);
            };
            return v; }
        if (name == "lower")  { std::string s = obj.s; Value v; v.type = Value::Type::Native; v.native_name = "str.lower";
            v.native = [s](Interpreter&, std::vector<Value>&) {
                std::string out = s;
                for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return Value::string(out);
            };
            return v; }
    }
    if (obj.type == Value::Type::Array && obj.arr) {
        if (name == "length" || name == "size" || name == "count")
            return Value::number(static_cast<f64>(obj.arr->size()));
    }
    if (obj.type == Value::Type::Number && name == "floor")
        return Value::number(std::floor(obj.n));
    throw ScriptError(std::string("no member '") + name + std::string("' on ") + std::string(obj.type_name()), line);
}

void Interpreter::set_member(const Value& obj, const std::string& name, const Value& v, i32 line) {
    if (obj.type == Value::Type::Object && obj.obj) { obj.obj->fields[name] = v; return; }
    if (obj.type == Value::Type::Map && obj.map)    { (*obj.map)[name] = v; return; }
    throw ScriptError(std::string("cannot set member '") + name + std::string("' on ") + std::string(obj.type_name()), line);
}

} // namespace prism::script
