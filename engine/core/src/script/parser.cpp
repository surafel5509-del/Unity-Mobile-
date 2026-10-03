#include "prism/script/prismscript.h"

namespace prism::script {

const Token& Parser::peek(std::size_t ahead) const {
    std::size_t i = pos_ + ahead;
    return i < tokens_.size() ? tokens_[i] : tokens_.back();
}
const Token& Parser::previous() const { return pos_ > 0 ? tokens_[pos_ - 1] : tokens_.front(); }
bool Parser::check(Tok k) const { return peek().kind == k; }

bool Parser::match(Tok k) {
    if (check(k)) { ++pos_; return true; }
    return false;
}

Token Parser::consume(Tok k, const std::string& what) {
    if (check(k)) { Token t = peek(); ++pos_; return t; }
    fail(std::string("expected ") + what + std::string(" but found '") + peek().text + "'");
    return peek();
}

void Parser::fail(const std::string& msg) {
    had_error_ = true;
    error_ = std::string("line ") + std::to_string(peek().line) + std::string(": ") + msg;
    // skip to end to stop cascading errors
    pos_ = tokens_.empty() ? 0 : tokens_.size() - 1;
}

std::vector<StmtPtr> Parser::parse_program() {
    std::vector<StmtPtr> out;
    while (!check(Tok::End)) {
        std::size_t before = pos_;
        auto d = declaration();
        if (had_error_) break;
        if (d) out.push_back(d);
        if (pos_ == before) { ++pos_; }   // guarantee progress
    }
    return out;
}

StmtPtr Parser::declaration() {
    if (check(Tok::Let))   return var_decl(true);
    if (check(Tok::Var))   return var_decl(false);
    if (check(Tok::Func))  return func_decl();
    if (check(Tok::Class)) return class_decl();
    if (check(Tok::Using) || check(Tok::Import)) {
        auto s = std::make_shared<ImportStmt>();
        s->line = peek().line;
        ++pos_;
        // module path may be dotted: Prism.Engine
        std::string path;
        while (check(Tok::Ident)) {
            path += peek().text; ++pos_;
            if (check(Tok::Dot)) { path += "."; ++pos_; } else break;
        }
        s->module = path;
        match(Tok::Semicolon);
        return s;
    }
    return statement();
}

std::shared_ptr<VarDecl> Parser::var_decl(bool is_const) {
    auto s = std::make_shared<VarDecl>();
    s->line = peek().line;
    s->is_const = is_const;
    ++pos_;                                     // let | var
    s->name = consume(Tok::Ident, "variable name").text;
    if (match(Tok::Assign)) s->init = expression();
    match(Tok::Semicolon);
    return s;
}

std::shared_ptr<FuncDecl> Parser::func_decl() {
    auto f = std::make_shared<FuncDecl>();
    f->line = peek().line;
    ++pos_;                                     // func
    f->name = consume(Tok::Ident, "function name").text;
    f->params = param_list();
    f->body = block();
    return f;
}

std::shared_ptr<ClassDecl> Parser::class_decl() {
    auto c = std::make_shared<ClassDecl>();
    c->line = peek().line;
    ++pos_;                                     // class
    c->name = consume(Tok::Ident, "class name").text;
    if (match(Tok::Extends) || match(Tok::Colon)) c->base = consume(Tok::Ident, "base class").text;
    consume(Tok::LBrace, "'{' after class name");
    while (!check(Tok::RBrace) && !check(Tok::End)) {
        if (had_error_) break;
        if (check(Tok::Let) || check(Tok::Var)) {
            c->fields.push_back(var_decl(check(Tok::Let)));
        } else if (check(Tok::Func)) {
            c->methods.push_back(func_decl());
        } else if (check(Tok::Ident)) {
            // sugar: `speed = 6.0;` inside a class body is a field
            auto f = std::make_shared<VarDecl>();
            f->line = peek().line;
            f->name = peek().text; ++pos_;
            if (match(Tok::Assign)) f->init = expression();
            match(Tok::Semicolon);
            c->fields.push_back(f);
        } else {
            fail(std::string("unexpected token in class body: '") + peek().text + "'");
            break;
        }
    }
    consume(Tok::RBrace, "'}' to close class");
    return c;
}

StmtPtr Parser::statement() {
    if (match(Tok::Semicolon)) { auto b = std::make_shared<BlockStmt>(); b->line = previous().line; return b; }
    if (check(Tok::LBrace))  return block();
    if (check(Tok::If))      return if_stmt();
    if (check(Tok::While))   return while_stmt();
    if (check(Tok::For))     return for_stmt();
    if (check(Tok::Return)) {
        auto r = std::make_shared<ReturnStmt>();
        r->line = peek().line; ++pos_;
        if (!check(Tok::Semicolon) && !check(Tok::RBrace) && !check(Tok::End)) r->value = expression();
        match(Tok::Semicolon);
        return r;
    }
    if (check(Tok::Break))    { auto s = std::make_shared<BreakStmt>();    s->line = peek().line; ++pos_; match(Tok::Semicolon); return s; }
    if (check(Tok::Continue)) { auto s = std::make_shared<ContinueStmt>(); s->line = peek().line; ++pos_; match(Tok::Semicolon); return s; }

    auto s = std::make_shared<ExprStmt>();
    s->line = peek().line;
    s->expr = expression();
    match(Tok::Semicolon);
    return s;
}

StmtPtr Parser::block() {
    auto b = std::make_shared<BlockStmt>();
    b->line = peek().line;
    consume(Tok::LBrace, "'{'");
    while (!check(Tok::RBrace) && !check(Tok::End)) {
        std::size_t before = pos_;
        auto d = declaration();
        if (had_error_) break;
        if (d) b->statements.push_back(d);
        if (pos_ == before) ++pos_;
    }
    consume(Tok::RBrace, "'}'");
    return b;
}

StmtPtr Parser::if_stmt() {
    auto s = std::make_shared<IfStmt>();
    s->line = peek().line;
    ++pos_;
    bool paren = match(Tok::LParen);
    s->cond = expression();
    if (paren) consume(Tok::RParen, "')'");
    s->then_branch = block();
    if (match(Tok::Else)) {
        if (check(Tok::If)) s->else_branch = if_stmt();
        else s->else_branch = block();
    }
    return s;
}

StmtPtr Parser::while_stmt() {
    auto s = std::make_shared<WhileStmt>();
    s->line = peek().line;
    ++pos_;
    bool paren = match(Tok::LParen);
    s->cond = expression();
    if (paren) consume(Tok::RParen, "')'");
    s->body = block();
    return s;
}

StmtPtr Parser::for_stmt() {
    auto s = std::make_shared<ForStmt>();
    s->line = peek().line;
    ++pos_;
    bool paren = match(Tok::LParen);

    // for (x in coll) { }  /  for x in coll { }
    if ((check(Tok::Ident) && peek(1).kind == Tok::In) ||
        (check(Tok::Var) && peek(2).kind == Tok::In) ||
        (check(Tok::Let) && peek(2).kind == Tok::In)) {
        auto fe = std::make_shared<ForEachStmt>();
        fe->line = s->line;
        if (check(Tok::Var) || check(Tok::Let)) ++pos_;
        fe->var = consume(Tok::Ident, "loop variable").text;
        consume(Tok::In, "'in'");
        fe->iterable = expression();
        if (paren) consume(Tok::RParen, "')'");
        fe->body = block();
        return fe;
    }

    // classic C-style for
    if (check(Tok::Semicolon)) { ++pos_; }
    else { s->init = declaration(); }
    if (!check(Tok::Semicolon)) s->cond = expression();
    consume(Tok::Semicolon, "';' in for loop");
    if (paren && !check(Tok::RParen)) s->step = expression();
    if (!paren && !check(Tok::LBrace)) s->step = expression();
    if (paren) consume(Tok::RParen, "')'");
    s->body = block();
    return s;
}

std::vector<std::string> Parser::param_list() {
    std::vector<std::string> params;
    consume(Tok::LParen, "'('");
    if (!check(Tok::RParen)) {
        do { params.push_back(consume(Tok::Ident, "parameter name").text); } while (match(Tok::Comma));
    }
    consume(Tok::RParen, "')'");
    return params;
}

std::vector<ExprPtr> Parser::arg_list() {
    std::vector<ExprPtr> args;
    consume(Tok::LParen, "'('");
    if (!check(Tok::RParen)) {
        do { args.push_back(expression()); } while (match(Tok::Comma));
    }
    consume(Tok::RParen, "')'");
    return args;
}

// ------------------------------------------------------------- expressions --
ExprPtr Parser::expression() { return ternary(); }

ExprPtr Parser::ternary() {
    ExprPtr cond = or_expr();
    if (match(Tok::Question)) {
        auto t = std::make_shared<Ternary>();
        t->line = cond->line;
        t->cond = cond;
        t->when_true = expression();
        consume(Tok::Colon, "':' in ternary expression");
        t->when_false = expression();
        return t;
    }
    // assignment (right associative)
    if (check(Tok::Assign) || check(Tok::PlusAssign) || check(Tok::MinusAssign) ||
        check(Tok::StarAssign) || check(Tok::SlashAssign)) {
        auto a = std::make_shared<Assign>();
        a->line = cond->line;
        a->op = peek().text;
        ++pos_;
        a->target = cond;
        a->value = ternary();
        return a;
    }
    return cond;
}

ExprPtr Parser::or_expr() {
    ExprPtr left = and_expr();
    while (check(Tok::OrOr) || check(Tok::Or)) {
        auto e = std::make_shared<Logical>();
        e->line = peek().line; e->op = "||"; ++pos_;
        e->left = left; e->right = and_expr();
        left = e;
    }
    return left;
}

ExprPtr Parser::and_expr() {
    ExprPtr left = equality();
    while (check(Tok::AndAnd) || check(Tok::And)) {
        auto e = std::make_shared<Logical>();
        e->line = peek().line; e->op = "&&"; ++pos_;
        e->left = left; e->right = equality();
        left = e;
    }
    return left;
}

ExprPtr Parser::equality() {
    ExprPtr left = comparison();
    while (check(Tok::Eq) || check(Tok::Neq)) {
        auto e = std::make_shared<Binary>();
        e->line = peek().line; e->op = peek().text; ++pos_;
        e->left = left; e->right = comparison();
        left = e;
    }
    return left;
}

ExprPtr Parser::comparison() {
    ExprPtr left = additive();
    while (check(Tok::Lt) || check(Tok::Gt) || check(Tok::Le) || check(Tok::Ge)) {
        auto e = std::make_shared<Binary>();
        e->line = peek().line; e->op = peek().text; ++pos_;
        e->left = left; e->right = additive();
        left = e;
    }
    return left;
}

ExprPtr Parser::additive() {
    ExprPtr left = multiplicative();
    while (check(Tok::Plus) || check(Tok::Minus)) {
        auto e = std::make_shared<Binary>();
        e->line = peek().line; e->op = peek().text; ++pos_;
        e->left = left; e->right = multiplicative();
        left = e;
    }
    return left;
}

ExprPtr Parser::multiplicative() {
    ExprPtr left = unary();
    while (check(Tok::Star) || check(Tok::Slash) || check(Tok::Percent)) {
        auto e = std::make_shared<Binary>();
        e->line = peek().line; e->op = peek().text; ++pos_;
        e->left = left; e->right = unary();
        left = e;
    }
    return left;
}

ExprPtr Parser::unary() {
    if (check(Tok::Minus) || check(Tok::Bang) || check(Tok::Not)) {
        auto e = std::make_shared<Unary>();
        e->line = peek().line;
        e->op = (peek().kind == Tok::Not) ? "!" : peek().text;
        ++pos_;
        e->operand = unary();
        return e;
    }
    return postfix();
}

ExprPtr Parser::postfix() {
    ExprPtr e = primary();
    for (;;) {
        if (match(Tok::Dot)) {
            auto m = std::make_shared<Member>();
            m->line = previous().line;
            m->object = e;
            m->name = consume(Tok::Ident, "member name").text;
            e = m;
        } else if (match(Tok::LBracket)) {
            auto idx = std::make_shared<Index>();
            idx->line = previous().line;
            idx->object = e;
            idx->index = expression();
            consume(Tok::RBracket, "']'");
            e = idx;
        } else if (check(Tok::LParen)) {
            auto c = std::make_shared<Call>();
            c->line = peek().line;
            c->callee = e;
            c->args = arg_list();
            e = c;
        } else break;
    }
    return e;
}

ExprPtr Parser::primary() {
    Token t = peek();
    switch (t.kind) {
        case Tok::Number: { ++pos_; auto l = std::make_shared<Literal>(); l->line = t.line; l->kind = Literal::Kind::Number; l->n = t.number; return l; }
        case Tok::String: { ++pos_; auto l = std::make_shared<Literal>(); l->line = t.line; l->kind = Literal::Kind::String; l->s = t.text; return l; }
        case Tok::True:   { ++pos_; auto l = std::make_shared<Literal>(); l->line = t.line; l->kind = Literal::Kind::Bool; l->b = true; return l; }
        case Tok::False:  { ++pos_; auto l = std::make_shared<Literal>(); l->line = t.line; l->kind = Literal::Kind::Bool; l->b = false; return l; }
        case Tok::Null:   { ++pos_; auto l = std::make_shared<Literal>(); l->line = t.line; l->kind = Literal::Kind::Null; return l; }
        case Tok::This:   { ++pos_; auto e = std::make_shared<ThisExpr>(); e->line = t.line; return e; }
        case Tok::Ident:  { ++pos_; auto i = std::make_shared<Ident>(); i->line = t.line; i->name = t.text; return i; }
        case Tok::New: {
            ++pos_;
            auto n = std::make_shared<NewExpr>();
            n->line = t.line;
            n->class_name = consume(Tok::Ident, "class name after 'new'").text;
            n->args = arg_list();
            return n;
        }
        case Tok::Pipe: {          // lambda: |a, b| expr  or  |a| { stmts }
            ++pos_;
            auto l = std::make_shared<Lambda>();
            l->line = t.line;
            if (!check(Tok::Pipe)) {
                do { l->params.push_back(consume(Tok::Ident, "lambda parameter").text); } while (match(Tok::Comma));
            }
            consume(Tok::Pipe, "'|' to close lambda parameters");
            if (check(Tok::LBrace)) l->body = block();
            else {
                auto ret = std::make_shared<ReturnStmt>();
                ret->line = peek().line;
                ret->value = expression();
                auto b = std::make_shared<BlockStmt>();
                b->line = ret->line;
                b->statements.push_back(ret);
                l->body = b;
            }
            return l;
        }
        case Tok::Func: {          // anonymous function: func (a, b) { ... }
            ++pos_;
            auto l = std::make_shared<Lambda>();
            l->line = t.line;
            l->params = param_list();
            l->body = block();
            return l;
        }
        case Tok::LBracket: {      // array literal
            ++pos_;
            std::vector<Value> items;
            auto call = std::make_shared<Call>();
            call->line = t.line;
            auto id = std::make_shared<Ident>(); id->name = "__array"; id->line = t.line;
            call->callee = id;
            if (!check(Tok::RBracket)) {
                do { call->args.push_back(expression()); } while (match(Tok::Comma));
            }
            consume(Tok::RBracket, "']'");
            (void)items;
            return call;
        }
        case Tok::LBrace: {        // map literal { "k": v }
            ++pos_;
            auto call = std::make_shared<Call>();
            call->line = t.line;
            auto id = std::make_shared<Ident>(); id->name = "__map"; id->line = t.line;
            call->callee = id;
            while (!check(Tok::RBrace) && !check(Tok::End)) {
                ExprPtr key;
                if (check(Tok::String)) { auto l = std::make_shared<Literal>(); l->kind = Literal::Kind::String; l->s = peek().text; l->line = peek().line; ++pos_; key = l; }
                else if (check(Tok::Ident)) { auto l = std::make_shared<Literal>(); l->kind = Literal::Kind::String; l->s = peek().text; l->line = peek().line; ++pos_; key = l; }
                else key = expression();
                consume(Tok::Colon, "':' in map literal");
                call->args.push_back(key);
                call->args.push_back(expression());
                if (!match(Tok::Comma)) break;
            }
            consume(Tok::RBrace, "'}'");
            return call;
        }
        case Tok::Minus: return unary();
        case Tok::LParen: {
            ++pos_;
            ExprPtr e = expression();
            consume(Tok::RParen, "')'");
            return e;
        }
        default:
            fail(std::string("unexpected token '") + t.text + "'");
            return std::make_shared<Literal>();
    }
}

} // namespace prism::script
