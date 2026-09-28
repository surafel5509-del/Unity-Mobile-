// =====================================================================
//  PRISM ENGINE — script/prismscript.h
//  PrismScript: the engine's primary language. C#-inspired syntax,
//  lexed -> parsed -> AST -> tree-walking VM with closures & classes.
//  Host API (Touch, Entity, Audio, Save, Bus) is injected as natives.
// =====================================================================
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept>
#include <variant>
#include "../core/types.h"
#include "../math/math.h"

namespace prism::script {

// ------------------------------------------------------------------ lexer --
enum class Tok : u8 {
    End, Number, String, Ident,
    // keywords
    Let, Var, Func, Class, Extends, If, Else, While, For, In, Return,
    Break, Continue, True, False, Null, New, This, And, Or, Not, Using, Import,
    // punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semicolon, Dot, Colon, Arrow, Question,
    // operators
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Eq, Neq, Lt, Gt, Le, Ge,
    AndAnd, OrOr, Bang, Pipe
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    f64 number = 0;
    i32 line = 1, col = 1;
};

class Lexer {
public:
    explicit Lexer(std::string src) : src_(std::move(src)) {}
    std::vector<Token> tokenize();
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] bool had_error() const { return had_error_; }
private:
    std::string src_;
    std::size_t pos_ = 0;
    i32 line_ = 1, col_ = 1;
    std::string error_;
    bool had_error_ = false;
};

// -------------------------------------------------------------------- ast --
struct Expr;   using ExprPtr = std::shared_ptr<Expr>;
struct Stmt;   using StmtPtr = std::shared_ptr<Stmt>;
class Environment;
class Interpreter;

struct Expr {
    i32 line = 0;
    virtual ~Expr() = default;
};
struct Literal : Expr { 
    enum class Kind : u8 { Null, Bool, Number, String } kind = Kind::Null;
    bool b = false; f64 n = 0; std::string s;
};
struct Ident : Expr { std::string name; };
struct Unary : Expr { std::string op; ExprPtr operand; };
struct Binary : Expr { std::string op; ExprPtr left, right; };
struct Logical : Expr { std::string op; ExprPtr left, right; };
struct Assign : Expr { std::string op; ExprPtr target, value; };
struct Index : Expr { ExprPtr object, index; };
struct Member : Expr { ExprPtr object; std::string name; };
struct Call : Expr { ExprPtr callee; std::vector<ExprPtr> args; };
struct Lambda : Expr { std::vector<std::string> params; StmtPtr body; };
struct NewExpr : Expr { std::string class_name; std::vector<ExprPtr> args; };
struct ThisExpr : Expr {};
struct Ternary : Expr { ExprPtr cond, when_true, when_false; };

struct Stmt {
    i32 line = 0;
    virtual ~Stmt() = default;
};
struct BlockStmt : Stmt { std::vector<StmtPtr> statements; };
struct VarDecl : Stmt { std::string name; ExprPtr init; bool is_const = false; };
struct ExprStmt : Stmt { ExprPtr expr; };
struct IfStmt : Stmt { ExprPtr cond; StmtPtr then_branch, else_branch; };
struct WhileStmt : Stmt { ExprPtr cond; StmtPtr body; };
struct ForStmt : Stmt { StmtPtr init; ExprPtr cond, step; StmtPtr body; };
struct ForEachStmt : Stmt { std::string var; ExprPtr iterable; StmtPtr body; };
struct ReturnStmt : Stmt { ExprPtr value; };
struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};
struct FuncDecl : Stmt { std::string name; std::vector<std::string> params; StmtPtr body; };
struct ClassDecl : Stmt {
    std::string name, base;
    std::vector<std::shared_ptr<VarDecl>> fields;
    std::vector<std::shared_ptr<FuncDecl>> methods;
};
struct ImportStmt : Stmt { std::string module; };

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    std::vector<StmtPtr> parse_program();
    [[nodiscard]] bool had_error() const { return had_error_; }
    [[nodiscard]] const std::string& error() const { return error_; }
private:
    [[nodiscard]] const Token& peek(std::size_t ahead = 0) const;
    [[nodiscard]] const Token& previous() const;
    bool check(Tok k) const;
    bool match(Tok k);
    Token consume(Tok k, const std::string& what);
    void fail(const std::string& msg);

    StmtPtr declaration();
    StmtPtr statement();
    StmtPtr block();
    std::shared_ptr<VarDecl> var_decl(bool is_const);
    std::shared_ptr<FuncDecl> func_decl();
    std::shared_ptr<ClassDecl> class_decl();
    StmtPtr if_stmt();
    StmtPtr while_stmt();
    StmtPtr for_stmt();

    ExprPtr expression();
    ExprPtr ternary();
    ExprPtr or_expr();
    ExprPtr and_expr();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr additive();
    ExprPtr multiplicative();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
    std::vector<ExprPtr> arg_list();
    std::vector<std::string> param_list();

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    bool had_error_ = false;
    std::string error_;
};

// ------------------------------------------------------------------ values --
struct Value;
using ValueArray = std::vector<Value>;
using ValueMap   = std::map<std::string, Value>;
using NativeFn   = std::function<Value(Interpreter&, std::vector<Value>&)>;

struct Function;
struct ClassDef;
struct Instance;

struct Value {
    enum class Type : u8 { Null, Bool, Number, String, Array, Map, Function, Native, Object };
    Type type = Type::Null;
    bool b = false;
    f64  n = 0;
    std::string s;
    std::shared_ptr<ValueArray> arr;
    std::shared_ptr<ValueMap>   map;
    std::shared_ptr<Function>   fn;
    NativeFn                    native;
    std::string                 native_name;
    std::shared_ptr<Instance>   obj;

    static Value null()               { return Value{}; }
    static Value boolean(bool v)      { Value x; x.type = Type::Bool; x.b = v; return x; }
    static Value number(f64 v)        { Value x; x.type = Type::Number; x.n = v; return x; }
    static Value string(std::string v){ Value x; x.type = Type::String; x.s = std::move(v); return x; }
    static Value array(std::shared_ptr<ValueArray> v) { Value x; x.type = Type::Array; x.arr = std::move(v); return x; }
    static Value make_map(std::shared_ptr<ValueMap> v) { Value x; x.type = Type::Map; x.map = std::move(v); return x; }
    static Value array_of(std::vector<Value> v)       { return array(std::make_shared<ValueArray>(std::move(v))); }

    [[nodiscard]] bool truthy() const;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] f64 to_number() const;
    [[nodiscard]] const char* type_name() const;
    bool equals(const Value& o) const;
};

struct Function {
    std::string name;
    std::vector<std::string> params;
    StmtPtr body;
    std::shared_ptr<Environment> closure;
    std::shared_ptr<Instance> bound_this;   // set for methods
};

struct ClassDef {
    std::string name;
    std::shared_ptr<ClassDef> base;
    std::vector<std::shared_ptr<VarDecl>> fields;
    std::unordered_map<std::string, std::shared_ptr<Function>> methods;
    bool has_method(const std::string& m) const {
        return methods.count(m) > 0 || (base && base->has_method(m));
    }
    std::shared_ptr<Function> find_method(const std::string& m) const {
        auto it = methods.find(m);
        if (it != methods.end()) return it->second;
        return base ? base->find_method(m) : nullptr;
    }
};

struct Instance {
    std::shared_ptr<ClassDef> klass;
    std::unordered_map<std::string, Value> fields;
    u32 engine_handle = kInvalidId;    // link to ECS EntityId.index
};

// ------------------------------------------------------------ environment --
class Environment : public std::enable_shared_from_this<Environment> {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr) : parent_(std::move(parent)) {}
    bool define(const std::string& name, Value v, bool is_const = false);
    bool assign(const std::string& name, const Value& v);
    bool lookup(const std::string& name, Value& out) const;
    bool is_const(const std::string& name) const;
    [[nodiscard]] std::shared_ptr<Environment> parent() const { return parent_; }
    [[nodiscard]] std::size_t size() const { return vars_.size(); }
    std::vector<std::string> names() const;
private:
    std::shared_ptr<Environment> parent_;
    std::unordered_map<std::string, Value> vars_;
    std::unordered_map<std::string, bool> consts_;
};

// --------------------------------------------------------------- errors ----
struct ScriptError : public std::runtime_error {
    explicit ScriptError(const std::string& m, i32 line = 0)
        : std::runtime_error(m), line(line) {}
    i32 line = 0;
};

// ----------------------------------------------------------- interpreter ---
struct CallStats { u64 calls = 0, native_calls = 0, allocations = 0; f64 ms = 0; };

class Interpreter {
public:
    Interpreter();
    ~Interpreter();

    /// Register a native function callable from script.
    void define_native(const std::string& name, NativeFn fn);
    void define_global(const std::string& name, Value v);
    [[nodiscard]] bool get_global(const std::string& name, Value& out) const;

    /// Compile + run a whole source file. Returns false on error (message in last_error()).
    bool run_source(const std::string& source, const std::string& origin = "<script>");
    bool run_statements(const std::vector<StmtPtr>& stmts, std::shared_ptr<Environment> env);

    /// Call a global function by name.
    bool call(const std::string& name, std::vector<Value> args, Value& out);
    /// Instantiate a class and optionally call `start()`.
    bool instantiate(const std::string& class_name, std::vector<Value> args, std::shared_ptr<Instance>& out);
    /// Call a method on an instance (used for MonoBehaviour-style lifecycle).
    bool call_method(const std::shared_ptr<Instance>& inst, const std::string& method,
                     std::vector<Value> args, Value& out);

    Value call_value(const Value& callee, std::vector<Value> args,
                     const std::shared_ptr<Instance>& this_inst = nullptr);
    Value evaluate(const ExprPtr& e, Environment& env);
    enum class Flow { None, Return, Break, Continue };
    struct FlowResult { Flow flow = Flow::None; Value value; };
    FlowResult execute(const StmtPtr& s, Environment& env);

    [[nodiscard]] std::shared_ptr<Environment> globals() const { return globals_; }
    [[nodiscard]] const std::string& last_error() const { return last_error_; }
    [[nodiscard]] i32 last_error_line() const { return last_error_line_; }
    [[nodiscard]] const CallStats& stats() const { return stats_; }
    [[nodiscard]] std::size_t class_count() const { return classes_.size(); }
    [[nodiscard]] bool has_class(const std::string& n) const { return classes_.count(n) > 0; }
    std::vector<std::string> class_names() const;

    void set_max_call_depth(i32 d) { max_depth_ = d; }
    void set_output_sink(std::function<void(const std::string&)> sink) { print_sink_ = std::move(sink); }
    void print(const std::string& s);

    /// Built-in library registration (see stdlib.cpp).
    void register_stdlib();

private:
    Value eval_binary(const std::string& op, const Value& l, const Value& r, i32 line);
    void assign_target(const ExprPtr& target, const Value& v, Environment& env, i32 line);
    Value get_member(const Value& obj, const std::string& name, i32 line);
    void  set_member(const Value& obj, const std::string& name, const Value& v, i32 line);
    void  bind_class_methods(const std::shared_ptr<ClassDef>& c);

    std::shared_ptr<Environment> globals_;
    std::unordered_map<std::string, std::shared_ptr<ClassDef>> classes_;
    std::string last_error_;
    i32 last_error_line_ = 0;
    i32 depth_ = 0, max_depth_ = 512;
    CallStats stats_;
    std::function<void(const std::string&)> print_sink_;
};

} // namespace prism::script
