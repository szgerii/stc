#pragma once

#include "frontend/jl/scope.h"
#include "frontend/jl/sym_res.h"
#include "frontend/jl/type_conversion.h"
#include "frontend/jl/visitor.h"

#include <span>

namespace stc::jl {

class JLSema : public JLVisitor<JLSema, JLCtx, TypeId> {
    TypePool& tpool;
    std::vector<JLScope> scopes;
    TypeToJLVisitor type_to_jl;

    NodeId main_fn_decl        = NodeId::null_id();
    SymbolId sym_main          = SymbolId::null_id();
    TypeId expected_type       = TypeId::null_id();
    TypeId current_fn_ret      = TypeId::null_id();
    MethodDecl* current_method = nullptr;
    bool _success              = true;
    bool visiting_method_body  = false;
    bool visiting_indexer      = false;
    bool allow_pretyped_nodes  = false;
    bool in_interactive_ctx;

public:
    explicit JLSema(JLCtx& ctx, CompoundExpr& global_scope_body, bool in_interactive_ctx = false)
        : JLVisitor{ctx},
          tpool{ctx.type_pool},
          scopes{},
          type_to_jl{ctx},
          sym_main{ctx.sym_pool.get_id("main")},
          in_interactive_ctx{in_interactive_ctx} {

        push_scope(ScopeKind::Global, global_scope_body);
    }

    bool success() const { return _success; }
    bool is_interactive() const { return in_interactive_ctx; }

    void finalize();

    TypeId visit_default_case();

    // clang-format off
    #define X(type, kind) STC_AST_VISITOR_DECL(TypeId, type)
        #include "frontend/jl/node_defs/all_nodes.def"
    #undef X
    // clang-format on

    // deferred method body visitor
    void visit_method_body(MethodDecl& method);

    struct TypeCheckResult {
        enum TypeCheckResultOption : uint8_t { Ok, ImplicitCast, ExplicitCast, Failure };

        TypeCheckResultOption value;

        constexpr TypeCheckResult(TypeCheckResultOption value)
            : value{value} {}

        constexpr operator bool() const { return value != Failure; }

        constexpr bool operator==(const TypeCheckResultOption& opt) const { return value == opt; }
    };

    TypeCheckResult check_type_against(TypeId actual_type, TypeId checked_type,
                                       const Expr& base_expr) const;

    TypeCheckResult check(Expr& expr, TypeId checked_type, bool allow_pretyped = false,
                          bool handles_casts = false);

    bool check(NodeId& node_id, TypeId expected, bool allow_pretyped = false);
    bool check(NodeId& node_id, bool allow_pretyped = false) {
        return check(node_id, expected_type, allow_pretyped);
    }

    TypeId infer(Expr& expr, bool allow_pretyped = false);
    TypeId infer(NodeId node_id, bool allow_pretyped = false) {
        if (node_id.is_null()) {
            _success = false;
            stc::internal_error("trying to infer type for node with null id");
            return TypeId::null_id();
        }

        Expr* expr = ctx.get_node(node_id);

        if (expr == nullptr) {
            _success = false;
            stc::internal_error("arena returned nullptr for node id during type inference");
            return TypeId::null_id();
        }

        return infer(*expr, allow_pretyped);
    }

    bool is_checking() const { return !expected_type.is_null(); }
    bool is_inferring() const { return !is_checking(); }

private:
    jl_datatype_t* to_jl_type(TypeId type);

    NodeId try_unwrap_cmpd(NodeId cmpd_id);

    bool is_method_sig_redecl(const MethodDecl& method_decl, const FunctionDecl& fn_decl);

    TypeId ret_type_of_jl_call(jl_function_t* fn, const std::vector<TypeId>& arg_types,
                               const Expr& base_expr);

    std::optional<MethodDecl*> find_sig_match(const FunctionDecl& fn_decl,
                                              const std::vector<TypeId>& arg_types,
                                              const Expr& base_expr);

    void wrap_in_cast(NodeId& target, TypeId cast_type, bool explicit_cast, Expr& expr) {
        assert(!target.is_null() && ctx.get_node(target) != nullptr);
        assert(ctx.get_node(target) == &expr);

        if (explicit_cast)
            target = ctx.emplace_node<ExplicitCast>(expr.location, target, cast_type).first;
        else
            target = ctx.emplace_node<ImplicitCast>(expr.location, target, cast_type).first;
    }

    void wrap_in_cast(NodeId& target, TypeId cast_type, bool explicit_cast) {
        if (target.is_null())
            throw std::logic_error{"wrap_in_cast called with null id"};

        Expr* expr = ctx.get_node(target);
        assert(expr != nullptr);

        wrap_in_cast(target, cast_type, explicit_cast, *expr);
    }

    void assert_scopes_notempty() const {
        assert(!scopes.empty() && "Empty scopes list in Julia Sema class");
    }

    JLScope& push_scope(ScopeKind scope_kind, CompoundExpr& body) {
        return scopes.emplace_back(scope_kind, body, scopes.size() + 1, ctx);
    }

    static SymbolId get_usym_for(SymbolId sym_id, SymbolPool& sym_pool) {
        static size_t current_usym_idx = 0U;

        if (sym_id.is_null())
            throw std::logic_error{"Trying to create usym for null symbol"};

        auto sym_str = sym_pool.get_symbol_maybe(sym_id);
        if (!sym_str.has_value())
            throw std::logic_error{"Trying to create usym for symbol not in symbol pool"};

        std::string usym_str =
            std::string{*sym_str} + "_stc_usym_" + std::to_string(current_usym_idx);

        SymbolId id = sym_pool.get_id(usym_str);

        current_usym_idx++;
        return id;
    }

    // should be called just before a scope is popped
    bool mangle_scope(JLScope& scope);

    void pop_scope(bool is_global = false, bool skip_mangle = false);

    const JLScope& current_scope() const {
        assert_scopes_notempty();
        return scopes.back();
    }

    JLScope& current_scope() {
        assert_scopes_notempty();
        return scopes.back();
    }

    const JLScope& global_scope() const {
        assert_scopes_notempty();
        return scopes[0];
    }

    JLScope& global_scope() {
        assert_scopes_notempty();
        return scopes[0];
    }

    // false result means redeclaration attempt
    [[nodiscard]] bool st_register(SymbolId sym, NodeId decl) {
        assert_scopes_notempty();
        assert(!decl.is_null() && "Trying to declare a symbol table entry with null id as decl");

        return current_scope().st_add_sym(sym, decl);
    }

    std::optional<BindingType> binding_of(SymbolId sym) const {
        const JLScope& cur = current_scope();

        if (cur.is_global()) {
            assert(scopes.size() == 1);
            return BindingType::Global;
        }

        if (!cur.bt_contains(sym)) {
            return std::nullopt;
        }

        return cur.bt_find_sym(sym);
    }

    // attempts to find symbol in any visible scope
    NodeId find_sym(SymbolId sym) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); it++) {
            auto result = it->st_find_sym(sym);

            if (!result.is_null())
                return result;
        }

        return NodeId::null_id();
    }

    // attempts to find symbol in visible non-global scopes
    NodeId find_local_sym(SymbolId sym) const {
        for (auto it = scopes.rbegin(); (it + 1) != scopes.rend(); it++) {
            auto result = it->st_find_sym(sym);

            if (!result.is_null())
                return result;
        }

        return NodeId::null_id();
    }

    NodeId find_sym_in_current_scope(SymbolId sym) const {
        assert_scopes_notempty();

        return current_scope().st_find_sym(sym);
    }

    void dump_scopes() const {
        for (const JLScope& scope : scopes)
            scope.dump(ctx);
    }

    void dump(const Expr& expr) const;
    std::string type_str(TypeId id) const;

    TypeId fail(std::string_view msg, const Expr& expr);
    TypeId warn(std::string_view msg, const Expr& expr) const;
    TypeId internal_error(std::string_view msg, const Expr& expr);

    // TODO: make this not the case:
    // ! all local scope pushing should be handled through ScopeRAII, since push_scope doesn't run
    // ! the symbol resolution pass by default
    class ScopeRAII {
        using ParamDecls = std::span<std::reference_wrapper<ParamDecl>>;

        JLSema& sema;
        JLScope& scope;
        bool skip_mangle;
        bool _success = true;

    public:
        explicit ScopeRAII(JLSema& sema, ScopeKind scope_kind, CompoundExpr& scope_body,
                           ParamDecls param_decls = {}, bool skip_mangle = false)
            : sema{sema}, scope{sema.push_scope(scope_kind, scope_body)}, skip_mangle{skip_mangle} {

            SymbolRes res{sema.ctx, sema.scopes, sema.is_interactive()};

            for (ParamDecl& pdecl : param_decls)
                res.visit(&pdecl);

            res.visit(&scope_body);
            _success = res.finalize();

            if (sema.ctx.config.dump_scopes) {
                std::cout << "scope dump after symbol resolution:\n";
                scope.dump(sema.ctx);
            }
        }

        explicit ScopeRAII(JLSema& sema, ScopeKind scope_kind, NodeId scope_body_id,
                           ParamDecls param_decls = {})
            : ScopeRAII{sema, scope_kind, unwrap_or_throw(sema, scope_body_id), param_decls} {}

        ~ScopeRAII() {
            try {
                if (sema.ctx.config.dump_scopes) {
                    std::cout << "scope dump before popping:\n";
                    scope.dump(sema.ctx);
                }

                sema.pop_scope(scope.type() == ScopeType::Global, skip_mangle);
            } catch (std::exception& e) {
                stc::internal_error(std::format(
                    "exception thrown while popping scope at depth #{}, see message below:",
                    scope.depth()));
                std::cerr << e.what() << '\n';
            } catch (...) {
                stc::internal_error(std::format("exception thrown while popping scope at depth #{}",
                                                scope.depth()));
            }
        }

        ScopeRAII(const ScopeRAII&)            = delete;
        ScopeRAII(ScopeRAII&&)                 = delete;
        ScopeRAII& operator=(const ScopeRAII&) = delete;
        ScopeRAII& operator=(ScopeRAII&&)      = delete;

        bool sym_res_successful() const { return _success; }

    private:
        [[nodiscard]] CompoundExpr& unwrap_or_throw(JLSema& jl_sema, NodeId id) {
            CompoundExpr* cmpd = jl_sema.ctx.get_and_dyn_cast<CompoundExpr>(id);

            if (cmpd == nullptr)
                throw std::invalid_argument{
                    "Invalid node kind passed to function expecting a compound expression"};

            return *cmpd;
        }
    };
};
static_assert(CJLVisitorImpl<JLSema, TypeId>);

} // namespace stc::jl
