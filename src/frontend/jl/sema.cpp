#include "frontend/jl/sema.h"
#include "frontend/jl/ast_utils.h"
#include "frontend/jl/dumper.h"
#include "frontend/jl/rt/utils.h"
#include "julia_guard.h"
#include "types/type_to_string.h"

namespace stc::jl {

void JLSema::dump(const Expr& expr) const {
    NodeId root = NodeId::null_id();

    switch (ctx.config.err_dump_verbosity) {
        case DumpVerbosity::None:
            return;

        case DumpVerbosity::Partial:
            root = ctx.calculate_node_id(expr);
            break;

        case DumpVerbosity::Verbose:
            root = ctx.calculate_node_id(global_scope().body);
            break;

        default:
            throw std::logic_error{"Unaccounted DumpVerbosity case in sema dump"};
    }

    JLDumper dumper{ctx, std::cerr};
    dumper.visit(root);
}

TypeId JLSema::fail(std::string_view msg, const Expr& expr) {
    std::cerr << '\n';

    auto [loc, file] = ctx.src_info_pool.get_loc_and_file(expr.location);
    error(file, loc, msg);
    _success = false;

    if (ctx.config.err_dump_verbosity != DumpVerbosity::None) {
        std::cerr << "The above error was emitted while processing the following node:\n";
        dump(expr);
    }

    return TypeId::null_id();
}

TypeId JLSema::warn(std::string_view msg, const Expr& expr) const {
    std::cerr << '\n';

    auto [loc, file] = ctx.src_info_pool.get_loc_and_file(expr.location);
    warning(file, loc, msg);

    if (ctx.config.err_dump_verbosity != DumpVerbosity::None) {
        std::cerr << "The above warning was emitted while processing the following node:\n";
        dump(expr);
    }

    return TypeId::null_id();
}

TypeId JLSema::internal_error(std::string_view msg, const Expr& expr) {
    // stop user errors from propagating into internal assumption errors
    if (!_success)
        return TypeId::null_id();

    std::cerr << '\n';

    auto [loc, file] = ctx.src_info_pool.get_loc_and_file(expr.location);
    stc::internal_error(file, loc, msg);
    _success = false;

    if (ctx.config.err_dump_verbosity != DumpVerbosity::None) {
        std::cerr << "The above error occured while processing the following node:\n";
        dump(expr);
    }

    return TypeId::null_id();
}

std::string JLSema::type_str(TypeId id) const {
    return type_to_string(id, ctx.type_pool, ctx.sym_pool);
}

jl_datatype_t* JLSema::to_jl_type(TypeId type) {
    return type_to_jl.dispatch(type);
}

NodeId JLSema::try_unwrap_cmpd(NodeId cmpd_id) {
    const auto* cmpd = ctx.get_and_dyn_cast<CompoundExpr>(cmpd_id);

    if (cmpd == nullptr)
        return cmpd_id;

    if (cmpd->body.empty()) {
        fail("empty compound expression in position where a single expression is expected", *cmpd);
        return NodeId::null_id();
    }

    if (cmpd->body.size() > 1) {
        fail("multi-expression compound expression in position where a single expression is "
             "expected",
             *cmpd);
        return NodeId::null_id();
    }

    assert(cmpd->body.size() == 1);

    return try_unwrap_cmpd(cmpd->body[0]);
}

TypeId JLSema::visit_default_case() {
    _success = false;
    stc::internal_error("unexpected null id node found in the AST");
    return TypeId::null_id();
}

void JLSema::finalize() {
    if (!_success)
        return;

    if (scopes.size() > 1) {
        stc::internal_error(
            "active local scopes found at the time Julia sema's finalize was called");
        return;
    }

    if (scopes.empty()) {
        stc::internal_error("global scope not found at the time Julia sema's finalize was called");
        return;
    }

    if (ctx.config.dump_scopes) {
        std::cout << "scope dump before popping global scope:\n";
        scopes[0].dump(ctx);
    }

    assert(&current_scope() == &global_scope());

    auto& body = current_scope().body.body;
    body.insert(body.begin(), captured_uniforms.begin(), captured_uniforms.end());

    if (!main_fn_decl.is_null()) {
        const auto* main_fn = ctx.get_and_dyn_cast<FunctionDecl>(main_fn_decl);
        assert(main_fn != nullptr);

        if (main_fn->methods.size() == 1) {
            const auto* mdecl = ctx.get_and_dyn_cast<MethodDecl>(main_fn->methods[0]);
            assert(mdecl != nullptr);

            if (mdecl->ret_type != ctx.jl_Nothing_t() || !mdecl->param_decls.empty())
                fail("reserved function 'main' must return void, and take no parameters", *mdecl);
        } else {
            fail("reserved function 'main' cannot have more than one method defined", *main_fn);
        }
    }

    pop_scope(true);
}

bool JLSema::mangle_scope(JLScope& scope) {
    assert_scopes_notempty();

    for (auto [sym_id, decl_id] : scope.symbol_table) {
        if (&scope != &(global_scope())) {
            auto bt = scope.bt_find_sym(sym_id);
            if (bt == BindingType::Captured)
                continue;
        }

        auto* decl_expr = ctx.get_node(decl_id);
        if (decl_expr == nullptr) {
            stc::internal_error("invalid declaration node id found in symbol table");
            return false;
        }

        auto* decl = dyn_cast<Decl>(decl_expr);
        if (decl == nullptr) {
            internal_error("non-declaration node found in symbol table", *decl_expr);
            return false;
        }

        if (!decl->qualifiers.is_null()) {
            const auto& decl_quals = ctx.get_quals(decl->qualifiers);
            auto it =
                std::find_if(decl_quals.quals.begin(), decl_quals.quals.end(), [](QualKind kind) {
                    return kind == QualKind::tq_in || kind == QualKind::tq_out ||
                           kind == QualKind::tq_uniform;
                });

            if (it != decl_quals.quals.end())
                continue;
        }

        auto* fn_decl  = dyn_cast<FunctionDecl>(decl);
        auto* var_decl = dyn_cast<VarDecl>(decl);

        bool rewrite = (var_decl != nullptr && !var_decl->is_builtin()) || isa<ParamDecl>(decl) ||
                       (fn_decl != nullptr && ctx.get_sym(fn_decl->identifier) != "main");
        if (!rewrite)
            continue;

        SymbolId usym = get_usym_for(decl->identifier, ctx.sym_pool);
        if (usym.is_null()) {
            internal_error("couldn't generate unique symbol for declaration", *decl);
            return false;
        }

        decl->identifier = usym;

        if (fn_decl != nullptr) {
            if (fn_decl->identifier == sym_main)
                continue;

            for (NodeId mdecl_id : fn_decl->methods) {
                auto* mdecl = ctx.get_and_dyn_cast<MethodDecl>(mdecl_id);
                assert(mdecl != nullptr);

                mdecl->identifier = usym;
            }
        }
    }

    return true;
}

void JLSema::pop_scope(bool is_global, bool skip_mangle) {
    assert_scopes_notempty();

    // is_global is mostly just for catching accidental global scope pops (for now)
    if (is_global && scopes.size() != 1) {
        stc::internal_error("trying to pop non-global scope, but is_global argument is set to "
                            "true in pop_scope function");
        return;
    }

    if (!is_global && scopes.size() == 1) {
        stc::internal_error("trying to pop global scope, without setting is_global argument to "
                            "true in pop_scope function");
        return;
    }

    // uses index lookup for dmq, because f could force dmq to grow, potentially
    // creating dangling references/invalidated iterators

    JLScope& scope = scopes.back();

    for (NodeId m_id : scope.deferred_method_queue) {
        auto* mdecl = ctx.get_and_dyn_cast<MethodDecl>(m_id);

        if (mdecl == nullptr) {
            Expr* expr = ctx.get_node(m_id);

            if (expr == nullptr)
                stc::internal_error("Invalid node id in deferred method body visitor queue");
            else
                internal_error("non-method-declaration node found in deferred method body "
                               "visitor queue",
                               *expr);

            continue;
        }

        visit_method_body(*mdecl);
        assert(&scopes.back() == &scope);
    }

    assert(&scopes.back() == &scope);

    // mangle_scope already reports its own errors (and sets _success)
    if (!skip_mangle)
        mangle_scope(scope);

    assert(&scopes.back() == &scope);

    scopes.pop_back();
}

JLSema::TypeCheckResult JLSema::check_type_against(TypeId actual_type, TypeId checked_type,
                                                   const Expr& base_expr) const {
    if (actual_type.is_null()) {
        // TODO: custom error?
        return TypeCheckResult::Incompatible;
    }

    if (actual_type == checked_type)
        return TypeCheckResult::Match;

    if (is_jl_convertible(actual_type, checked_type, tpool)) {
        if (ctx.target_info == nullptr) {
            warn(fmt::format("conversion from {} to {} has been allowed based on Julia semantics, "
                             "since target info is unavailable",
                             type_str(actual_type), type_str(checked_type)),
                 base_expr);

            return TypeCheckResult::Match;
        }

        if (ctx.target_info->can_implicit_cast(actual_type, expected_type))
            return TypeCheckResult::ImplicitCast;

        if (ctx.target_info->valid_ctor_call(expected_type, std::vector{actual_type}))
            return TypeCheckResult::ExplicitCast;

        return TypeCheckResult::Incompatible;
    }

    const auto& expected_td = tpool.get_td(checked_type);
    LazyInit actual_td{[&]() -> const TypeDescriptor& { return tpool.get_td(actual_type); }};

    // if expected fn type has an identifier, actual has to match it
    // if it doesnt, only the function-ness of the actual type is checked
    if (expected_td.is_function() &&
        (actual_td.get().is_function() || actual_td.get().is_struct())) {
        auto expected_fn = expected_td.as<FunctionTD>();

        SymbolId actual_id = SymbolId::null_id();
        if (actual_td.get().is_function())
            actual_id = actual_td.get().as<FunctionTD>().identifier;
        else {
            assert(actual_td.get().as<StructTD>().data != nullptr);
            actual_id = actual_td.get().as<StructTD>().data->name;
        }

        if (expected_fn.identifier.is_null() || expected_fn.identifier == actual_id)
            return TypeCheckResult::Match;

        return TypeCheckResult::Incompatible;
    }

    if (expected_td.is_array() && actual_td.get().is_array()) {
        ArrayTD expected_arr_ty = expected_td.as<ArrayTD>();
        ArrayTD actual_arr_ty   = actual_td.get().as<ArrayTD>();

        if (check_type_against(actual_arr_ty.element_type_id, expected_arr_ty.element_type_id,
                               base_expr) != TypeCheckResult::Match)
            return TypeCheckResult::Incompatible;

        if (tpool.is_array_any_size(checked_type))
            return TypeCheckResult::Match;

        return actual_arr_ty.length == expected_arr_ty.length ? TypeCheckResult::Match
                                                              : TypeCheckResult::Incompatible;
    }

    if (expected_td.is_vector() && actual_td.get().is_vector()) {
        VectorTD expected_vec_ty = expected_td.as<VectorTD>();
        VectorTD actual_vec_ty   = actual_td.get().as<VectorTD>();

        if (check_type_against(actual_vec_ty.component_type_id, expected_vec_ty.component_type_id,
                               base_expr) != TypeCheckResult::Match)
            return TypeCheckResult::Incompatible;

        if (tpool.is_vec_any_size(checked_type))
            return TypeCheckResult::Match;

        return actual_vec_ty.component_count == expected_vec_ty.component_count
                   ? TypeCheckResult::Match
                   : TypeCheckResult::Incompatible;
    }

    if (expected_td.is_matrix() && actual_td.get().is_matrix()) {
        MatrixTD expected_mat_ty = expected_td.as<MatrixTD>();
        MatrixTD actual_mat_ty   = actual_td.get().as<MatrixTD>();

        assert(tpool.is_type_of<VectorTD>(actual_mat_ty.column_type_id));
        assert(tpool.is_type_of<VectorTD>(expected_mat_ty.column_type_id));

        VectorTD actual_col_ty   = tpool.get_td(actual_mat_ty.column_type_id).as<VectorTD>();
        VectorTD expected_col_ty = tpool.get_td(expected_mat_ty.column_type_id).as<VectorTD>();

        if (check_type_against(actual_col_ty.component_type_id, expected_col_ty.component_type_id,
                               base_expr) != TypeCheckResult::Match)
            return TypeCheckResult::Incompatible;

        if (tpool.is_mat_any_size(checked_type))
            return TypeCheckResult::Match;

        if (actual_mat_ty.column_count == expected_mat_ty.column_count &&
            actual_col_ty.component_count == expected_col_ty.component_count)
            return TypeCheckResult::Match;

        return TypeCheckResult::Incompatible;
    }

    return TypeCheckResult::Incompatible;
}

JLSema::TypeCheckResult JLSema::check(Expr& expr, TypeId checked_type, bool allow_pretyped,
                                      bool handles_casts) {
    if (!handles_casts) {
        internal_error("base type check function called without caller setting handles_casts. this "
                       "is an indicator that implicit/explicit casting semantics were not taken "
                       "into consideration at the call site.",
                       expr);

        return TypeCheckResult::Incompatible;
    }

    bool old_pretyped_value = allow_pretyped_nodes;
    if (allow_pretyped)
        allow_pretyped_nodes = true;

    // method decls will, by the nature of their resolution flow, get invoked multiple times
    if (!allow_pretyped_nodes && !expr.type.is_null() && !isa<MethodDecl>(&expr)) {
        internal_error("type check function called with an expression whose type has "
                       "already been determined, while the allow_pretyped argument is false",
                       expr);

        return TypeCheckResult::Incompatible;
    }
    auto prev_expected  = this->expected_type;
    this->expected_type = checked_type;

    const ScopeGuard exp_type_scope_guard{[&]() {
        this->expected_type  = prev_expected;
        allow_pretyped_nodes = old_pretyped_value;
    }};

    // delegate type checking for returns to their visitor only
    // this is because they're checked against the current fn return type instead of an expected
    // type, given that they always should resolve to the void type (or null on failure)
    if (isa<ReturnStmt>(expr)) {
        expr.type = impl_this()->visit(&expr);
        assert(expr.type.is_null() || expr.type == tpool.void_td());

        return !expr.type.is_null() ? TypeCheckResult::Match : TypeCheckResult::Incompatible;
    }

    TypeId actual_type = expr.type.is_null() ? impl_this()->visit(&expr) : expr.type;

    if (_success && !actual_type.is_null() && !visiting_indexer &&
        tpool.get_td(actual_type).is_builtin(BuiltinTypeKind::String))
        fail("nodes representing strings are not allowed", expr);

    TypeCheckResult result = check_type_against(actual_type, checked_type, expr);
    if (result == TypeCheckResult::Incompatible) {
        // if actual_type is null, that's mostly an already reported error
        if (_success || !actual_type.is_null()) {
            std::string reason{};

            if (ctx.config.print_convert_fail_reason) {
                bool jl_allows = is_jl_convertible(actual_type, checked_type, tpool);
                bool target_allows =
                    ctx.target_info != nullptr &&
                    (ctx.target_info->can_implicit_cast(actual_type, checked_type) ||
                     ctx.target_info->valid_ctor_call(checked_type, std::vector{actual_type}));

                if (!jl_allows || !target_allows) {
                    reason = " (conversion not allowed by ";
                    if (!jl_allows)
                        reason += "Julia";

                    if (!target_allows) {
                        if (!jl_allows)
                            reason += " and ";

                        reason += "backend";
                    }
                    reason += ")";
                }
            }

            report_type_mismatch(actual_type, checked_type, expr, reason);
        }

        return TypeCheckResult::Incompatible;
    }

    expr.type = actual_type;

    return result;
}

bool JLSema::check(NodeId& node_id, TypeId expected, bool allow_pretyped) {
    if (node_id.is_null()) {
        _success = false;
        stc::internal_error("trying to type check node with null id");
        return false;
    }

    Expr* expr = ctx.get_node(node_id);

    if (expr == nullptr) {
        _success = false;
        stc::internal_error("arena returned nullptr for node id during type checking");
        return false;
    }

    TypeCheckResult result = check(*expr, expected, allow_pretyped, true);

    // NOTE: cast AST nodes already set their type in their ctor, there's no need to visit them
    switch (result.value) {
        case TypeCheckResult::Match:
            return true;

        case TypeCheckResult::Incompatible:
            return false;

        case TypeCheckResult::ImplicitCast:
            wrap_in_cast(node_id, expected, false, *expr);
            return true;

        case TypeCheckResult::ExplicitCast:
            wrap_in_cast(node_id, expected, true, *expr);
            return true;

        default:
            throw std::logic_error{"unaccounted TypeCheckResult in check"};
    }
}

TypeId JLSema::infer(Expr& expr, bool allow_pretyped) {
    bool old_pretyped_value = allow_pretyped_nodes;
    if (allow_pretyped)
        allow_pretyped_nodes = true;

    if (!allow_pretyped_nodes && !expr.type.is_null() && !isa<MethodDecl>(&expr)) {
        allow_pretyped_nodes = old_pretyped_value;

        return internal_error("type infer function called with an expression whose type has "
                              "already been determined, while the allow_pretyped argument is false",
                              expr);
    }

    auto prev_expected = expected_type;
    expected_type      = TypeId::null_id();

    const ScopeGuard exp_type_scope_guard{[&]() {
        expected_type        = prev_expected;
        allow_pretyped_nodes = old_pretyped_value;
    }};

    TypeId inferred = visit(&expr);

    // only report infer failure, if the source of the error hasn't been reported yet
    if (inferred.is_null())
        return _success ? fail("couldn't infer type for node during type checking", expr)
                        : TypeId::null_id();

    if (_success && !visiting_indexer && tpool.get_td(inferred).is_builtin(BuiltinTypeKind::String))
        fail("nodes representing strings are not allowed", expr);

    expr.type = inferred;

    return inferred;
}

TypeId JLSema::visit_VarDecl(VarDecl& vdecl) {
    if (vdecl.identifier.is_null())
        return fail("variable declaration with null as identifier", vdecl);

    if (vdecl.scope() == MaybeScopeType::Unspec) {
        return fail("variable declarations must specify a scope type (i.e. local x::Int instead of "
                    "x::Int), as without them, the language construct may be a type assert, "
                    "not a declaration",
                    vdecl);
    }

    TypeId result_type = TypeId::null_id();

    bool has_type = !vdecl.annot_type.is_null();
    bool has_init = !vdecl.initializer.is_null();

    auto expected_bt = binding_of(vdecl.identifier);

    if (!expected_bt.has_value())
        return internal_error(fmt::format("symbol resolution pass failed to infer binding type for "
                                          "a variable declaration's symbol: '{}'",
                                          ctx.get_sym(vdecl.identifier)),
                              vdecl);

    if (*expected_bt == BindingType::Captured) {
        return internal_error(
            "symbol resolution wrongfully inferred captured binding type for a symbol that is "
            "explicitly declared in scope",
            vdecl);
    }

    ScopeType expected_st = bt_to_st(*expected_bt);
    ScopeType actual_st   = mst_to_st(vdecl.scope());

    // special msg to reduce confusion about global scope in transpiled code
    if (expected_st == ScopeType::Global && actual_st == ScopeType::Local) {
        return fail(fmt::format("cannot declare symbol '{}' local in the global scope",
                                ctx.get_sym(vdecl.identifier)),
                    vdecl);
    }

    if (actual_st != expected_st) {
        return internal_error(
            fmt::format(
                "scope mismatch for target scope of variable declaration. inferred {}, found {}",
                scope_str(expected_st), scope_str(actual_st)),
            vdecl);
    }

    if (!vdecl.is_builtin() && ctx.target_info != nullptr &&
        ctx.target_info->has_builtin_global(ctx.get_sym(vdecl.identifier))) {
        return fail(fmt::format("explicit declaration for builtin global '{}' is not allowed",
                                ctx.get_sym(vdecl.identifier)),
                    vdecl);
    }

    // explicitly allow 'global var_name'-like expressions to bind to global scope
    bool is_global_signal = !vdecl.is_builtin() && actual_st == ScopeType::Global &&
                            vdecl.initializer.is_null() && (&current_scope() != &global_scope());

    if (is_global_signal) {
        if (!global_scope().st_contains(vdecl.identifier)) {
            return fail(fmt::format("trying to bind symbol '{}' to an undeclared global binding",
                                    ctx.get_sym(vdecl.identifier)),
                        vdecl);
        }

        NodeId glob_decl_id = global_scope().st_find_sym(vdecl.identifier);
        Expr* glob_decl     = ctx.get_node(glob_decl_id);
        assert(glob_decl != nullptr);

        if (glob_decl->type.is_null())
            return internal_error("found unresolved declaration in symbol table", *glob_decl);

        if (!vdecl.annot_type.is_null()) {
            TypeCheckResult tcr = check_type_against(vdecl.annot_type, glob_decl->type, vdecl);

            if (tcr != TypeCheckResult::Match) {
                return fail(fmt::format("invalid type in local binding for global symbol '{}' "
                                        "(expected {}, got {})",
                                        ctx.get_sym(vdecl.identifier), type_str(glob_decl->type),
                                        type_str(vdecl.annot_type)),
                            vdecl);
            }
        }

        vdecl.set_is_silent_decl(true);

        return glob_decl->type;
    }

    // TODO: lazy infer declared var type
    if (!has_type && !has_init)
        return fail("variable declaration without neither a type annotation, or an initializer is "
                    "currently not allowed",
                    vdecl);

    if (has_type) {
        if (has_init) {
            bool valid_init = check(vdecl.initializer, vdecl.annot_type);

            // check already reports the type mismatch, this is just to stop further traversal of
            // AST subtree (to reduce number of errors propagated from the same source)
            if (!valid_init)
                return TypeId::null_id();

            if (tpool.is_array_any_size(vdecl.annot_type))
                vdecl.annot_type = ctx.get_node(vdecl.initializer)->type;
        }

        result_type = vdecl.annot_type;
    } else {
        result_type = infer(vdecl.initializer);
    }

    NodeId decl_id = ctx.calculate_node_id(vdecl);

    assert(expected_st == actual_st);
    JLScope& target_scope = expected_st == ScopeType::Global ? global_scope() : current_scope();

    bool added = target_scope.st_add_sym(vdecl.identifier, decl_id);
    if (!added) {
        return fail(fmt::format("redeclaration of symbol '{}' in the same scope (as a variable)",
                                ctx.get_sym(vdecl.identifier)),
                    vdecl);
    }

    return result_type;
}

TypeId JLSema::visit_ParamDecl(ParamDecl& pdecl) {
    // TODO: support for kwargs (here and in MethodDecl visitor)
    if (pdecl.is_kwarg())
        return fail("kwargs are currently not supported", pdecl);

    TypeId result_type = TypeId::null_id();

    bool has_type = !pdecl.annot_type.is_null();
    bool has_init = !pdecl.default_initializer.is_null();

    if (!has_type && !has_init)
        return fail("parameter declaration without either a type annotation or a default "
                    "initializer is not allowed",
                    pdecl);

    if (has_type) {
        if (has_init) {
            bool valid_init = check(pdecl.default_initializer, pdecl.annot_type);

            if (!valid_init)
                return TypeId::null_id();
        }

        result_type = pdecl.annot_type;
    } else {
        result_type = infer(pdecl.default_initializer);
    }

    NodeId decl_id = ctx.calculate_node_id(pdecl);
    bool added     = st_register(pdecl.identifier, decl_id);
    if (!added) {
        NodeId prev_decl_id = find_sym_in_current_scope(pdecl.identifier);

        // param-as-param redecl (duplicate param identifiers) is reported by method decl visitor
        if (!ctx.isa<ParamDecl>(prev_decl_id)) {
            return fail(
                fmt::format("redeclaration of symbol '{}' in the same scope (as a parameter)",
                            ctx.get_sym(pdecl.identifier)),
                pdecl);
        }

        _success = false;
    }

    return result_type;
}

TypeId JLSema::visit_OpaqueFunction(OpaqueFunction& opaq_fn) {
    return tpool.func_td(opaq_fn.fn_name());
}

TypeId JLSema::visit_BuiltinFunction(BuiltinFunction& builtin_fn) {
    return tpool.func_td(builtin_fn.fn_name());
}

TypeId JLSema::visit_FunctionDecl(FunctionDecl& fn_decl) {
    if (fn_decl.identifier.is_null())
        return internal_error("function declaration with null as the identifier symbol", fn_decl);

    if (fn_decl.identifier == sym_main) {
        if (&current_scope() != &global_scope())
            return fail("function name 'main' is reserved for global-level declarations only",
                        fn_decl);

        main_fn_decl = ctx.calculate_node_id(fn_decl);
    }

    return tpool.func_td(fn_decl.identifier);
}

bool JLSema::is_method_sig_redecl(const MethodDecl& method_decl, const FunctionDecl& fn_decl) {
    const auto& method_list = fn_decl.methods;

    assert(std::find(method_list.begin(), method_list.end(), ctx.calculate_node_id(method_decl)) !=
               method_list.end() &&
           "trying to check signature redeclaration for a method which is not part of the provided "
           "function declaration's method list");

    // a single method can have multiple signatures thanks to default initialized params
    // it was a deliberate choice to not generate wrapper methods for these, as that would require
    // mimicking the lowering logic of the Julia compiler, instead of the behavior it in turn has on
    // the language, and its capabilities
    // (i think doing the second is slightly cleaner, and hopefully requires less maintaining
    // between versions)
    std::unordered_set<std::vector<TypeId>, VectorHash<TypeId>> target_sigs{};
    target_sigs.reserve(method_decl.param_decls.size() + 1); // this is the lowest upper bound

    std::vector<TypeId> current_sig{};
    current_sig.reserve(method_decl.param_decls.size());

    for (NodeId pdecl_id : method_decl.param_decls) {
        auto* pdecl = ctx.get_and_dyn_cast<ParamDecl>(pdecl_id);
        if (pdecl == nullptr) {
            internal_error("unexpected non-parameter-declaration node in parameter list "
                           "of method declaration",
                           method_decl);
            return false;
        }

        if (pdecl->type.is_null()) {
            assert(!_success);
            continue;
        }

        assert(!pdecl->type.is_null());
        current_sig.emplace_back(pdecl->type);

        if (!pdecl->default_initializer.is_null())
            target_sigs.emplace(current_sig);
    }

    target_sigs.emplace(current_sig);

    auto is_redecl = [&](const std::vector<TypeId>& sig) -> bool {
        bool redecl = target_sigs.contains(sig);

        if (redecl) {
            fail(fmt::format("Trying to overwrite the method definition for an already "
                             "defined signature of function '{}'",
                             ctx.get_sym(method_decl.identifier)),
                 method_decl);
        }

        return redecl;
    };

    for (NodeId fn_method_id : method_list) {
        auto* fn_method = ctx.get_and_dyn_cast<MethodDecl>(fn_method_id);
        if (fn_method == nullptr) {
            internal_error(
                "unexpected non-method-declaration node in method list of function declaration",
                fn_decl);
            return false;
        }

        // do not check against itself
        if (fn_method == &method_decl)
            continue;

        // reuse container for checking of other methods
        current_sig.clear();
        current_sig.reserve(fn_method->param_decls.size());

        for (NodeId pdecl_id : fn_method->param_decls) {
            auto* pdecl = ctx.get_and_dyn_cast<ParamDecl>(pdecl_id);

            if (pdecl == nullptr) {
                internal_error("unexpected non-parameter-declaration node in parameter list "
                               "of method declaration",
                               *fn_method);
                return false;
            }

            assert(!pdecl->type.is_null());
            current_sig.emplace_back(pdecl->type);

            // assumes that initializable params are grouped at the end (verified earlier)
            if (!pdecl->default_initializer.is_null()) {
                if (is_redecl(current_sig))
                    return true;
            }
        }

        if (is_redecl(current_sig))
            return true;
    }

    return false;
}

// CLEANUP: break up this function into 3 parts (global, local, common)
TypeId JLSema::visit_MethodDecl(MethodDecl& method) {
    auto* main_body_cmpd = ctx.get_and_dyn_cast<CompoundExpr>(method.body);

    if (main_body_cmpd == nullptr)
        return fail("method declaration with non-compound expression as a body is not allowed",
                    method);

    // unwrap nested cmpds
    while (main_body_cmpd->body.size() == 1 && ctx.isa<CompoundExpr>(main_body_cmpd->body[0]))
        main_body_cmpd = ctx.get_and_dyn_cast<CompoundExpr>(main_body_cmpd->body[0]);

    assert(main_body_cmpd != nullptr);
    method.body = ctx.calculate_node_id(*main_body_cmpd);

    if (method.identifier == sym_main) {
        if (!method.ret_type.is_null() && method.ret_type != ctx.jl_Nothing_t())
            return fail("main function with return type other than Nothing is not allowed", method);

        method.ret_type = ctx.jl_Nothing_t();

        if (main_body_cmpd->body.empty() || !ctx.isa<ReturnStmt>(main_body_cmpd->body.back())) {
            SrcLocationId impl_ret_loc = !main_body_cmpd->body.empty()
                                             ? ctx.get_node(main_body_cmpd->body.back())->location
                                             : main_body_cmpd->location;

            NodeId empty_ret = ctx.emplace_node<ReturnStmt>(impl_ret_loc).first;
            main_body_cmpd->body.emplace_back(empty_ret);
        }

        if (!method.param_decls.empty())
            return fail("main function with non-zero argument count is not allowed", method);
    }

    NodeId method_id = ctx.calculate_node_id(method);

    auto expected_bt = binding_of(method.identifier);
    if (!expected_bt.has_value())
        return internal_error(fmt::format("symbol resolution pass failed to infer binding type for "
                                          "declaration of a method for '{}'",
                                          ctx.get_sym(method.identifier)),
                              method);

    if (*expected_bt == BindingType::Captured)
        return internal_error(fmt::format("symbol resolution pass inferred binding type of "
                                          "Captured for declaration of a method for '{}'",
                                          ctx.get_sym(method.identifier)),
                              method);

    bool is_global = *expected_bt == BindingType::Global;

    // global only vars
    bool check_method_redecl = false;
    // local only vars (needed again at the end, so no refs)
    bool is_fn_resolver      = false;
    LFTEntry* fn_entry       = nullptr;

    FunctionDecl* fn_decl = nullptr;
    if (is_global) {
        NodeId fn_decl_id = global_scope().st_find_sym(method.identifier);

        if (fn_decl_id.is_null()) {
            fn_decl_id = ctx.emplace_node<FunctionDecl>(method.location, method.identifier,
                                                        std::vector{{method_id}})
                             .first;

            infer(fn_decl_id);
            global_scope().st_add_sym(method.identifier, fn_decl_id);
        } else {
            fn_decl = ctx.get_and_dyn_cast<FunctionDecl>(fn_decl_id);
            if (fn_decl == nullptr)
                return fail(fmt::format("trying to declare '{}' as a function, but it has already "
                                        "been declared as a non-function symbol previously",
                                        ctx.get_sym(method.identifier)),
                            method);

            for (NodeId decled_m_id : fn_decl->methods) {
                auto* decled_m = ctx.get_and_dyn_cast<MethodDecl>(decled_m_id);

                if (decled_m == nullptr)
                    return internal_error(fmt::format("nullptr found in method list of a function "
                                                      "declaration for symbol '{}'",
                                                      ctx.get_sym(fn_decl->identifier)),
                                          *fn_decl);
            }

            fn_decl->methods.emplace_back(method_id);

            // signature may already exist, but we don't know before param types have been resolved
            check_method_redecl = true;
        }
    } else {
        auto& lft = current_scope().local_fn_table;

        auto fn_entry_it = lft.find(method.identifier);
        if (fn_entry_it == lft.end())
            return internal_error(
                fmt::format(
                    "symbol resolution pass missed adding the locally defined function '{}' to the "
                    "local function table",
                    ctx.get_sym(method.identifier)),
                method);

        fn_entry = &(fn_entry_it->second);

        if (fn_entry->state == LFTEntry::State::Resolved)
            return tpool.func_td(method.identifier);

        // info also needed near the end
        is_fn_resolver = fn_entry->state == LFTEntry::State::Unresolved;

        NodeId fn_decl_id = find_local_sym(method.identifier);
        // function decl registration has to happen here, because param initializers may perform
        // recursion on the methods themselves
        if (is_fn_resolver) {
            fn_entry->state = LFTEntry::State::InProgress;

            if (!fn_decl_id.is_null()) {
                if (ctx.isa<FunctionDecl>(fn_decl_id))
                    return internal_error(
                        "function declaration already present in symbol table at the "
                        "time of the first method declaration's resolution",
                        method);

                return fail(fmt::format("redeclaration of already declared symbol '{}' as a method",
                                        ctx.get_sym(method.identifier)),
                            method);
            }

            // methods could each insert their own id during their visitor run, but we already have
            // it grouped by the symbol resolution pass, so might as well use it and avoid having to
            // update the list later
            std::vector<NodeId> method_ids;
            method_ids.reserve(fn_entry->method_decls.size());
            for (const auto* m_decl : fn_entry->method_decls) {
                if (m_decl == nullptr)
                    return internal_error(
                        "nullptr found in the method list of a local function table", method);

                method_ids.emplace_back(ctx.calculate_node_id(*m_decl));
            }

            // TODO:
            // function decls only live in the symbol table rn, some interface will be necessary to
            // forward it to later passes

            std::tie(fn_decl_id, fn_decl) = ctx.emplace_node<FunctionDecl>(
                method.location, method.identifier, std::move(method_ids));

            infer(fn_decl_id);

            bool added = st_register(fn_decl->identifier, fn_decl_id);
            if (!added) {
                return fail(
                    fmt::format("redeclaration of symbol '{}' in the same scope (as a method)",
                                ctx.get_sym(method.identifier)),
                    method);
            }
        }
    }

    // NOTE:
    // reasoning for param initializer handling would be too complicated to describe here
    // see my thesis for an in-depth explanation with examples.
    // short version: because Julia generates a separate wrapper method for each possible arity, and
    // default initializers can be arbitrary expressions (with their own assignments/declarations),
    // how symbol visibility is handled across them can get tricky

    std::vector<std::reference_wrapper<ParamDecl>> param_decls{};
    param_decls.reserve(method.param_decls.size());

    for (NodeId param : method.param_decls) {
        auto* pdecl = ctx.get_and_dyn_cast<ParamDecl>(param);

        if (pdecl == nullptr)
            return internal_error("invalid node kind in param decl list of a method decl", method);

        param_decls.emplace_back(*pdecl);
    }

    assert(param_decls.size() == method.param_decls.size());

    std::vector<TypeId> param_types{};
    param_types.reserve(method.param_decls.size());

    size_t first_init_idx = 0;
    for (auto& pdecl : param_decls) {
        if (!pdecl.get().default_initializer.is_null())
            break;

        // type might be null here. that's okay, as long as we can infer it later
        param_types.emplace_back(pdecl.get().annot_type);
        first_init_idx++;
    }

    assert(first_init_idx <= param_decls.size());

    {
        // swallows symbol table registrations
        CompoundExpr empty_cmpd{method.location, std::vector<NodeId>{}};
        ScopeRAII temp_scope{*this, ScopeKind::Hard, empty_cmpd,
                             std::span{param_decls.data(), param_decls.size() - first_init_idx},
                             true};

        for (size_t i = 0; i < first_init_idx; i++)
            infer(param_decls[i]);
    }

    // this handles iterating over all possible arities of the method
    for (size_t i = first_init_idx; i < param_decls.size(); i++) {
        ParamDecl& pdecl = param_decls[i];

        if (pdecl.default_initializer.is_null()) {
            return fail("parameter without a default initializer following a default initialized "
                        "parameter in method signature",
                        pdecl);
        }

        std::vector<NodeId> dummy_method_body{};
        dummy_method_body.reserve(method.param_decls.size() - i + 1);

        for (size_t j = 0; j < method.param_decls.size(); j++) {
            // TODO: this is kinda a temporary solution
            if (j < i) {
                dummy_method_body.emplace_back(method.param_decls[j]);
                continue;
            }

            auto* pdecl_j = ctx.get_and_dyn_cast<ParamDecl>(method.param_decls[j]);
            if (pdecl_j == nullptr || pdecl_j->default_initializer.is_null())
                continue;

            // eval of initializer
            dummy_method_body.emplace_back(pdecl_j->default_initializer);
        }

        CompoundExpr dummy_wrapper{method.location, std::move(dummy_method_body)};
        {
            ScopeRAII dummy_scope{*this, ScopeKind::Hard, dummy_wrapper, param_decls};

            // FEATURE: allow method decl to resolve to different signatures for different arities

            for (size_t j = i; j < method.param_decls.size(); j++) {
                if (param_types.size() <= j)
                    param_types.emplace_back(infer(method.param_decls[j]));
                else if (param_types[j].is_null())
                    param_types[j] = infer(method.param_decls[j]);
                else
                    check(method.param_decls[j], param_types[j]);

                // TODO: allow param type infer from body
                if (param_types[j].is_null())
                    return fail(
                        fmt::format(
                            "couldn't infer type for method parameter '{}'. "
                            "Currently, a parameter must either have an explicit type annotation, "
                            "or its type must be inferrable from its default initializer.",
                            ctx.get_sym(param_decls[j].get().identifier)),
                        param_decls[j]);
            }
        }
    }

    // inferred return types don't support deferred body visitors
    if (method.ret_type.is_null())
        visit_method_body(method);
    else
        current_scope().defer_method_body_visit(method_id);

    if (is_global && check_method_redecl) {
        assert(fn_decl != nullptr);

        bool redecl = is_method_sig_redecl(method, *fn_decl);
        if (redecl)
            return TypeId::null_id();
    } else if (!is_global && is_fn_resolver) {
        assert(fn_entry != nullptr);

        for (auto* fn_method : fn_entry->method_decls) {
            if (fn_method != &method)
                infer(*fn_method);
        }

        for (auto* fn_method : fn_entry->method_decls) {
            bool redecl = is_method_sig_redecl(*fn_method, *fn_decl);
            if (redecl)
                return TypeId::null_id();
        }

        fn_entry->state = LFTEntry::State::Resolved;
    }

    return tpool.func_td(method.identifier);
}

void JLSema::visit_method_body(MethodDecl& method) {
    std::vector<std::reference_wrapper<ParamDecl>> param_decls;
    param_decls.reserve(method.param_decls.size());

    for (NodeId pdecl_id : method.param_decls) {
        auto* pdecl = ctx.get_and_dyn_cast<ParamDecl>(pdecl_id);
        assert(pdecl != nullptr);
        param_decls.emplace_back(*pdecl);
    }

    // fn scope
    ScopeRAII fn_scope{*this, ScopeKind::Hard, method.body, param_decls};
    if (!fn_scope.sym_res_successful()) {
        fail("symbol resolution pass failed in function scope of a method declaration", method);
        return;
    }

    // this registers into function scope (dummy_scope swallows symbols from param decl visitor)
    bool any_failed = false;
    std::unordered_set<SymbolId> used_param_ids{};
    used_param_ids.reserve(param_decls.size());
    for (auto& pdecl : param_decls) {
        NodeId pdecl_id = ctx.calculate_node_id(pdecl.get());

        bool used_before = !used_param_ids.emplace(pdecl.get().identifier).second;
        if (used_before) {
            fail(fmt::format("more than one parameter named '{}' in definition of function '{}'",
                             ctx.get_sym(pdecl.get().identifier), ctx.get_sym(method.identifier)),
                 pdecl);
            any_failed = true;
            continue;
        }

        bool added = st_register(pdecl.get().identifier, pdecl_id);
        if (!added) {
            assert(!_success);
            any_failed = true;
        }
    }

    if (any_failed)
        return;

    TypeId prev_ret = current_fn_ret;
    current_fn_ret  = method.ret_type;

    auto* prev_method = current_method;
    current_method    = &method;

    assert(!visiting_method_body);
    if (!current_fn_ret.is_null()) {
        visiting_method_body = true;
        check(method.body, current_fn_ret);
        visiting_method_body = false;
    } else {
        if (!ctx.isa<CompoundExpr>(method.body)) {
            internal_error(
                fmt::format("non-compound-expression as body of method definition for '{}'",
                            ctx.get_sym(method.identifier)),
                method);
            return;
        }

        visiting_method_body = true;
        TypeId body_inf      = infer(method.body);
        visiting_method_body = false;

        if (current_fn_ret.is_null()) {
            fail(fmt::format("couldn't infer return type from a method definition for '{}'. Try "
                             "adding an explicit return type to the function header.",
                             ctx.get_sym(method.identifier)),
                 method);
            return;
        }

        if (current_fn_ret != body_inf) {
            internal_error(
                fmt::format(
                    "inferred return type based on return statements ({}) got desynchronized "
                    "from body's inferred type ({}) for a method of '{}'",
                    type_str(current_fn_ret), type_str(body_inf), ctx.get_sym(method.identifier)),
                method);
            return;
        }

        method.ret_type = current_fn_ret;
    }

    for (auto [sym_id, bt] : current_scope().binding_table) {
        if (bt == BindingType::Captured)
            method.set_has_captured_syms(true);
    }

    current_method = prev_method;
    current_fn_ret = prev_ret;
}

TypeId JLSema::visit_FieldDecl(FieldDecl& fdecl) {
    if (fdecl.type.is_null())
        return fail("field declarations without an explicit type annotation are not supported",
                    fdecl);

    const auto& td = tpool.get_td(fdecl.type);
    bool valid_field_type =
        td.is_scalar() || td.is_vector() || td.is_matrix() || td.is_matrix() || td.is_array();

    if (!valid_field_type) {
        return fail(
            fmt::format("field declarations with type {} are not supported", type_str(fdecl.type)),
            fdecl);
    }

    return fdecl.type;
}

TypeId JLSema::visit_StructDecl(StructDecl& sdecl) {
    TypeId s_type = tpool.get_struct_td(sdecl.identifier);

    if (s_type.is_null())
        return internal_error("struct's type has not been registered by parser", sdecl);

    if (&current_scope() != &global_scope()) {
        return fail(fmt::format("definition of struct '{}' in non-global scope is not allowed",
                                ctx.get_sym(sdecl.identifier)),
                    sdecl);
    }

    bool added = st_register(sdecl.identifier, ctx.calculate_node_id(sdecl));
    if (!added) {
        return fail(fmt::format("redefinition of struct '{}' is not allowed",
                                ctx.get_sym(sdecl.identifier)),
                    sdecl);
    }

    return s_type;
}

TypeId JLSema::visit_InterfaceBlockDecl(InterfaceBlockDecl& iface_blk) {
    if (&current_scope() != &global_scope()) {
        return fail(
            fmt::format("definition of interface block '{}' in non-global scope is not allowed",
                        ctx.get_sym(iface_blk.block_name())),
            iface_blk);
    }

    if (!iface_blk.instance_name().is_null())
        return fail("interface block with instance name is currently not supported", iface_blk);

    for (NodeId field_id : iface_blk.field_decls) {
        const auto* field = ctx.get_and_dyn_cast<FieldDecl>(field_id);

        if (field == nullptr)
            return internal_error("non-field-declaration node in field list of interface block",
                                  iface_blk);

        bool added = st_register(field->identifier, ctx.calculate_node_id(*field));
        if (!added) {
            return fail(fmt::format("redefinition of name '{}' in the global scope",
                                    ctx.get_sym(field->identifier)),
                        *field);
        }
    }

    return tpool.void_td();
}

TypeId JLSema::visit_CompoundExpr(CompoundExpr& cmpd) {
    if (cmpd.body.empty()) {
        if (visiting_method_body)
            current_fn_ret = ctx.jl_Nothing_t();

        return ctx.jl_Nothing_t();
    }

    // stop visiting_method_body flag from propagating further (e.g. nested compound exprs)
    bool is_method_body = visiting_method_body;
    if (is_method_body)
        visiting_method_body = false;

    TypeId result_type = TypeId::null_id();

    // last expression of body is inferred/checked separately
    for (size_t i = 0; i < cmpd.body.size() - 1; i++)
        infer(cmpd.body[i]);

    if (is_checking()) {
        bool valid_last_expr = check(cmpd.body.back());
        if (!valid_last_expr)
            return TypeId::null_id();

        result_type = expected_type;
    } else {
        result_type = infer(cmpd.body.back());
    }

    if (result_type.is_null()) {
        if (is_method_body)
            visiting_method_body = true;

        return TypeId::null_id();
    }

    // handle last expression implicit/explicit returning
    if (is_method_body) {
        auto* last_expr = ctx.get_node(cmpd.body.back());
        assert(last_expr != nullptr);

        auto* ret = dyn_cast<ReturnStmt>(last_expr);

        if (current_fn_ret.is_null()) {
            assert(!last_expr->type.is_null());

            if (ret != nullptr)
                return internal_error("return type has not been inferred for method body, even "
                                      "though it's last expression is a return statement",
                                      cmpd);

            const auto& last_td = tpool.get_td(last_expr->type);
            if (!last_td.is_function() && !last_td.is_method() && !last_td.is_builtin())
                current_fn_ret = last_expr->type;
            else
                current_fn_ret = ctx.jl_Nothing_t();
        }

        result_type = current_fn_ret;

        if (current_fn_ret != ctx.jl_Nothing_t() && ret == nullptr) {
            // if the last expression isn't a return (and we're in a method body), insert an
            // explicit return for consistency

            NodeId inner =
                current_fn_ret != ctx.jl_Nothing_t() ? cmpd.body.back() : NodeId::null_id();
            bool in_place = true;

            if (inner != NodeId::null_id()) {
                NodeId target_inner = cmpd.body.back();

                if (auto* last_assign = ctx.get_and_dyn_cast<Assignment>(target_inner)) {
                    if (last_assign->is_implicit_decl()) {
                        in_place     = false;
                        target_inner = ctx.get_and_dyn_cast<DeclRefExpr>(last_assign->target)->decl;
                    }
                }

                if (auto* last_decl = ctx.get_and_dyn_cast<Decl>(target_inner)) {
                    in_place = false;

                    NodeId sym_lit =
                        ctx.emplace_node<SymbolLiteral>(last_decl->location, last_decl->identifier)
                            .first;

                    inner = ctx.emplace_node<DeclRefExpr>(last_decl->location, sym_lit).first;
                    infer(inner);
                }
            }

            NodeId gen_ret = ctx.emplace_node<ReturnStmt>(last_expr->location, inner).first;

            if (in_place)
                cmpd.body[cmpd.body.size() - 1] = gen_ret;
            else
                cmpd.body.emplace_back(gen_ret);

            infer(gen_ret, true);
        }

        visiting_method_body = true;
    }

    return result_type;
}

#define DEFINE_LIT(type)                                                                           \
    TypeId JLSema::visit_##type##Literal([[maybe_unused]] type##Literal& lit) {                    \
        return ctx.jl_##type##_t();                                                                \
    }

// TODO: value checks for some of these
DEFINE_LIT(Bool)
DEFINE_LIT(Int32)
DEFINE_LIT(Int64)
DEFINE_LIT(UInt8)
DEFINE_LIT(UInt16)
DEFINE_LIT(UInt32)
DEFINE_LIT(UInt64)
DEFINE_LIT(UInt128)
DEFINE_LIT(Float32)
DEFINE_LIT(Float64)

#undef DEFINE_LIT

TypeId JLSema::visit_StringLiteral(StringLiteral& str_lit) {
    if (!visiting_indexer)
        fail("string literals are not allowed", str_lit);

    return ctx.jl_String_t();
}

TypeId JLSema::visit_ArrayLiteral(ArrayLiteral& arr_lit) {
    if (arr_lit.members.size() >= std::numeric_limits<uint32_t>::max()) {
        return fail("array length exceeds upper limit (largest index needs to be storable in a "
                    "u32, excluding its maximum)",
                    arr_lit);
    }

    auto len = static_cast<uint32_t>(arr_lit.members.size());

    TypeId checked_el_type = TypeId::null_id();
    if (is_checking()) {
        assert(!expected_type.is_null());
        const auto& arr_td = tpool.get_td(expected_type);
        ArrayTD arr_ty     = arr_td.as<ArrayTD>();

        if (arr_td.is_array())
            checked_el_type = arr_ty.element_type_id;
    }

    if (!checked_el_type.is_null()) {
        for (NodeId& member : arr_lit.members)
            check(member, checked_el_type);

        return tpool.array_td(checked_el_type, len);
    }

    if (arr_lit.members.empty())
        return fail("cannot infer element type for empty array literal", arr_lit);

    TypeId inferred_el_type = infer(arr_lit.members[0]);

    if (inferred_el_type.is_null())
        return fail("failed to infer element type for first element of array literal", arr_lit);

    bool any_check_failed = false;
    for (size_t i = 1; i < arr_lit.members.size(); i++) {
        bool member_valid = check(arr_lit.members[i], inferred_el_type);
        any_check_failed  = any_check_failed || !member_valid;
    }

    if (any_check_failed)
        return fail("array literal with varying element type is not allowed", arr_lit);

    return tpool.array_td(inferred_el_type, len);
}

TypeId JLSema::visit_IndexerExpr(IndexerExpr& idx_expr) {
    bool prev_vis_idx = visiting_indexer;
    visiting_indexer  = true;
    for (auto& idx : idx_expr.indexers)
        infer(idx);
    visiting_indexer = prev_vis_idx;

    infer(idx_expr.target);
    TypeId coll_type = ctx.get_node(idx_expr.target)->type;

    if (coll_type.is_null())
        return fail("failed to infer type for target of an indexer expression", idx_expr);

    if (is_checking() && tpool.get_td(expected_type).is_scalar()) {
        TypeCheckResult res = TypeCheckResult::Incompatible;

        res = check_type_against(coll_type, tpool.any_vec_td(expected_type), idx_expr);

        if (res == TypeCheckResult::Incompatible)
            res = check_type_against(coll_type, tpool.any_mat_td(expected_type), idx_expr);

        if (res == TypeCheckResult::Incompatible)
            res = check_type_against(coll_type, tpool.any_array_td(expected_type), idx_expr);

        // this explicitly disallows casting on the target's type
        if (res != TypeCheckResult::Match) {
            return fail(fmt::format("target of an indexer expression could not be checked to hold "
                                    "a collection of the expected '{}' type",
                                    type_str(expected_type)),
                        idx_expr);
        }
    }

    const auto& coll_td = tpool.get_td(coll_type);

    TypeId el_type = tpool.el_type_of(coll_td);
    if (el_type.is_null())
        return fail("cannot use an indexer expression on a non-collection-type", idx_expr);

    if (coll_td.is_vector()) {
        if (idx_expr.indexers.size() != 1)
            return fail("vector types only support one indexer value", idx_expr);

        const Expr* idx = ctx.get_node(idx_expr.indexers[0]);

        if (ctx.type_pool.is_type_of<IntTD>(idx->type))
            return el_type;

        if (idx->type != ctx.jl_String_t() && idx->type != ctx.jl_Symbol_t())
            return fail(fmt::format("invalid vector indexer type '{}'",
                                    type_to_string(idx->type, ctx, ctx)),
                        idx_expr);

        // SWIZZLE

        // centralize swizzles into symbol literals
        if (const auto* str_idx = dyn_cast<const StringLiteral>(idx)) {
            SymbolId sym_id = ctx.sym_pool.get_id(str_idx->value);

            std::tie(idx_expr.indexers[0], idx) =
                ctx.emplace_node<SymbolLiteral>(idx->location, sym_id);

            prev_vis_idx     = visiting_indexer;
            visiting_indexer = true;
            infer(idx_expr.indexers[0]);
            visiting_indexer = prev_vis_idx;
        }

        if (const auto* sym_idx = dyn_cast<const SymbolLiteral>(idx)) {
            std::string_view swizzle = ctx.get_sym(sym_idx->value);

            size_t swizzle_n = swizzle.length();
            if (swizzle_n == 0 || swizzle_n > 4)
                return fail(fmt::format("invalid swizzle expression length for '{}'", swizzle),
                            idx_expr);

            uint32_t vec_n = coll_td.as<VectorTD>().component_count;
            SwizzleSet set = SwizzleSet::Invalid;

            for (size_t i = 0; i < swizzle_n; i++) {
                auto [comp, cur_set] = parse_swizzle_component(swizzle[i]);

                if (comp == INVALID_SWIZZLE)
                    return fail(
                        fmt::format("invalid swizzle expression component '{}'", swizzle[i]),
                        idx_expr);

                if (cur_set == SwizzleSet::Invalid)
                    return internal_error(
                        "invalid swizzle set returned for valid swizzle component", idx_expr);

                if (set == SwizzleSet::Invalid)
                    set = cur_set;
                else if (set != cur_set)
                    return fail(
                        fmt::format(
                            "mixing components from different swizzle sets is not allowed ('{}')",
                            swizzle),
                        idx_expr);

                assert(vec_n != 0);
                if (comp > vec_n - 1)
                    return fail(fmt::format("swizzle component '{}' points outside "
                                            "vector bounds (vector component count is {})",
                                            swizzle[i], vec_n),
                                idx_expr);
            }

            return swizzle_n == 1
                       ? el_type
                       : ctx.type_pool.vector_td(el_type, static_cast<uint32_t>(swizzle_n));
        }

        return fail("swizzle expressions must be literals", idx_expr);
    }

    if (coll_td.is_matrix()) {
        if (idx_expr.indexers.size() == 1)
            return coll_td.as<MatrixTD>().column_type_id;

        if (idx_expr.indexers.size() != 2)
            return fail("matrix types only support one or two indexer values", idx_expr);
    }

    if (coll_td.is_array()) {
        if (idx_expr.indexers.size() != 1)
            return fail("vector types only support one indexer value", idx_expr);
    }

    return el_type;
}

TypeId JLSema::visit_SymbolLiteral(SymbolLiteral& sym) {
    if (visiting_indexer)
        return ctx.jl_Symbol_t();

    return fail("symbol literal in unexpected position (only supported inside swizzle "
                "expressions)",
                sym);
}

TypeId JLSema::visit_FieldAccess(FieldAccess& acc) {
    if (!acc.type.is_null())
        return acc.type;

    TypeId target_ty      = infer(acc.target);
    const auto& target_td = tpool.get_td(target_ty);

    if (!target_td.is_struct())
        return fail(
            fmt::format("field access into invalid or unsupported type {}", type_str(target_ty)),
            acc);

    const auto& s_td = target_td.as<StructTD>();
    assert(s_td.data != nullptr);

    NodeId sdecl_id = global_scope().st_find_sym(s_td.data->name);

    if (sdecl_id.is_null() || !ctx.isa<StructDecl>(sdecl_id))
        return internal_error(
            fmt::format("couldn't find struct type declaration in global scope for {}",
                        type_str(target_ty)),
            acc);

    const auto* sdecl = ctx.get_and_dyn_cast<StructDecl>(sdecl_id);
    assert(sdecl != nullptr);

    if (sdecl->type != target_ty) {
        return internal_error("desynchronization between type pool's struct list and "
                              "sema's symbol table",
                              acc);
    }

    auto* target_sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(acc.field_decl);
    if (target_sym_lit == nullptr)
        return internal_error("unexpected node kind in rhs of a FieldAccess expression", acc);

    for (NodeId fdecl_id : sdecl->field_decls) {
        const auto* fdecl = ctx.get_and_dyn_cast<FieldDecl>(fdecl_id);
        assert(fdecl != nullptr);

        if (fdecl->identifier == target_sym_lit->value) {
            acc.field_decl       = fdecl_id;
            acc.target_type_decl = sdecl_id;

            return fdecl->type;
        }
    }

    return fail(fmt::format("{} has no field with identifier '{}'", type_str(sdecl->type),
                            ctx.get_sym(target_sym_lit->value)),
                acc);
}

TypeId JLSema::visit_DotChain(DotChain& dc) {
    using namespace rt;

    if (dc.is_resolved())
        return ctx.get_node(dc.resolved_expr)->type;

    if (dc.chain.empty())
        return fail("malformed dot chain expression", dc);

    if (is_checking() && tpool.is_any_func(expected_type)) {
        std::string mod_path = mod_chain_to_path(dc.chain, ctx, dc.chain.size() - 1);

        auto mod = ctx.jl_env.module_cache.get_mod(mod_path);

        if (!mod.has_value())
            return fail(fmt::format("invalid dot chain, couldn't resolve module at '{}'", mod_path),
                        dc);

        const auto* sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(dc.chain.back());
        if (sym_lit == nullptr) {
            const auto* dre = ctx.get_and_dyn_cast<DeclRefExpr>(dc.chain.back());

            if (dre != nullptr)
                sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(dre->decl);
        }

        if (sym_lit == nullptr)
            return internal_error("unexpected node kind in dot chain", dc);

        jl_value_t* jl_fn = mod->get().get_fn(ctx.get_sym(sym_lit->value), false);
        if (jl_fn == nullptr)
            return fail(fmt::format("no function with name '{}' was found in module '{}'",
                                    ctx.get_sym(sym_lit->value), mod_path),
                        dc);

        SymbolId fn_name_id = ctx.sym_pool.get_id(get_jl_fn_name(jl_fn));

        dc.resolved_expr = ctx.emplace_node<OpaqueFunction>(dc.location, fn_name_id, jl_fn).first;
        infer(dc.resolved_expr);

        return tpool.func_td(fn_name_id);
    }

    if (dc.chain.size() == 2) {
        auto* field_expr = ctx.get_node(dc.chain[1]);
        auto* sym_lit    = dyn_cast<SymbolLiteral>(field_expr);

        if (sym_lit == nullptr) {
            auto* dre = dyn_cast<DeclRefExpr>(field_expr);

            if (dre != nullptr)
                sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(dre->decl);
        }

        if (sym_lit == nullptr)
            return fail(
                "non-symbol-literal node is not allowed as the rhs of a field access expression",
                dc);

        auto [acc_id, acc] = ctx.emplace_node<FieldAccess>(dc.location, dc.chain[0],
                                                           ctx.calculate_node_id(*sym_lit));

        if (is_checking())
            check(acc_id);
        else
            infer(acc_id);

        dc.resolved_expr = acc_id;
        return acc->type;
    }

    return fail("dot chain used in an unsupported context (currently only field access and "
                "function targeting module lookup are supported)",
                dc);
}

TypeId JLSema::visit_NothingLiteral([[maybe_unused]] NothingLiteral& lit) {
    return ctx.jl_Nothing_t();
}

TypeId JLSema::visit_OpaqueNode(OpaqueNode& opaq) {
    return fail("opaque value found in source AST.", opaq);
}

TypeId JLSema::visit_GlobalRef(GlobalRef& gref) {
    return fail("global ref found in source AST.", gref);
}

TypeId JLSema::visit_ImplicitCast(ImplicitCast& impl_cast) {
    if (impl_cast.type.is_null())
        return internal_error("implicit cast node with null type", impl_cast);

    return impl_cast.type;
}

TypeId JLSema::visit_ExplicitCast(ExplicitCast& expl_cast) {
    if (expl_cast.type.is_null())
        return internal_error("explicit cast node with null type", expl_cast);

    return expl_cast.type;
}

NodeId JLSema::resolve_sym_with_binding(SymbolId sym, BindingType bt, const Expr& base_expr) {
    bool is_global   = bt == BindingType::Global;
    bool is_captured = bt == BindingType::Captured;
    bool is_local    = bt == BindingType::Local;

    NodeId reffed_decl = NodeId::null_id();
    if (is_global)
        reffed_decl = global_scope().st_find_sym(sym);
    else if (is_captured)
        reffed_decl = find_local_sym(sym);
    else if (is_local)
        reffed_decl = find_sym_in_current_scope(sym);

    if (!reffed_decl.is_null()) {
        auto* decl = ctx.get_and_dyn_cast<Decl>(reffed_decl);
        if (decl == nullptr) {
            internal_error("non-declaration node in symbol table", base_expr);
            return NodeId::null_id();
        }

        return reffed_decl;
    }

    if (is_global && ctx.target_info != nullptr) {
        TypeId glob_ty = ctx.target_info->builtin_global_ty(ctx.get_sym(sym));

        if (!glob_ty.is_null()) {
            NodeId builtin_decl =
                ctx.emplace_node<VarDecl>(base_expr.location, sym, glob_ty, ScopeType::Global,
                                          NodeId::null_id(), true, false)
                    .first;

            infer(builtin_decl);
            global_scope().st_add_sym(sym, builtin_decl);

            return builtin_decl;
        }
    }

    std::string_view sym_str = ctx.get_sym(sym);

    if (is_global && tpool.is_any_func(expected_type)) {
        // TODO
        // builtins hide calls to jl fns, if they'd have a valid match, while the builtin doesnt

        // try to resolve through builtins first
        if (ctx.target_info != nullptr && ctx.target_info->has_builtin_fn(sym_str)) {
            auto builtin_fn = ctx.emplace_node<BuiltinFunction>(base_expr.location, sym).first;
            infer(builtin_fn);

            return builtin_fn;
        }

        // perform ctor alias to type rewriting (if applicable, e.g. dvec3 -> DVec3)
        sym     = ctor_resolver.try_apply_rewrite(sym);
        sym_str = ctx.get_sym(sym);

        // try to resolve in JuliaGLM before any other module
        jl_value_t* jl_fn = ctx.jl_env.module_cache.glm_mod.get_fn(sym_str, false);

        if (jl_fn == nullptr)
            jl_fn = find_jl_function(sym_str, ctx.jl_env, false);

        if (jl_fn == nullptr) {
            fail(fmt::format("couldn't find function '{}' in the symbol table, or in the "
                             "root julia module",
                             sym_str),
                 base_expr);

            return NodeId::null_id();
        }

        SymbolId fn_name = ctx.sym_pool.get_id(get_jl_fn_name(jl_fn));

        auto opaq_fn = ctx.emplace_node<OpaqueFunction>(base_expr.location, fn_name, jl_fn).first;
        infer(opaq_fn);

        if (ctx.config.warn_on_jl_sema_query)
            warn(fmt::format("had to resolve function reference through julia for {}type "
                             "referring symbol '{}'",
                             !rt::is_type(jl_fn) ? "non-" : "", sym_str),
                 base_expr);

        return opaq_fn;
    }

    return NodeId::null_id();
}

TypeId JLSema::visit_DeclRefExpr(DeclRefExpr& dre) {
    auto* inner = ctx.get_node(dre.decl);
    if (inner == nullptr)
        return internal_error("declaration reference expression wrapping a nullptr", dre);

    // already resolved state
    if (auto* decl = dyn_cast<Decl>(inner))
        return decl->type;

    if (auto* gref = dyn_cast<GlobalRef>(inner))
        return fail("global refs are currently not supported", *gref);

    auto* sym = dyn_cast<SymbolLiteral>(inner);
    if (sym == nullptr)
        return internal_error("declaration reference expression points to invalid node kind", dre);

    auto maybe_bt = binding_of(sym->value);

    if (!maybe_bt.has_value()) {
        return internal_error(fmt::format("symbol resolution pass failed to infer binding type for "
                                          "symbol '{}' in a declaration",
                                          ctx.get_sym(sym->value)),
                              *sym);
    }

    BindingType bt = *maybe_bt;

    NodeId decl = resolve_sym_with_binding(sym->value, bt, dre);
    if (!decl.is_null()) {
        assert(ctx.get_node(decl) != nullptr);

        dre.decl = decl;
        return ctx.get_node(decl)->type;
    }

    if (bt == BindingType::Captured) {
        return fail(
            fmt::format("forward capture of symbol '{}' is not allowed", ctx.get_sym(sym->value)),
            dre);
    }

    auto report_undef = [&]() {
        return fail(fmt::format("use of undeclared or uninitialized symbol '{}' (binding type: {})",
                                ctx.get_sym(sym->value), bt_str(*maybe_bt)),
                    dre);
    };

    // uniform capture
    if (ctx.config.capture_uniforms && bt == BindingType::Global) {
        auto sym_str = ctx.get_sym(sym->value);

        jl_value_t* glob = jl_get_global(ctx.jl_env.module_cache.main_mod,
                                         jl_symbol_n(sym_str.data(), sym_str.size()));
        if (glob == nullptr)
            return report_undef();

        jl_value_t* glob_ty_v = jl_typeof(glob);
        if (!jl_is_datatype(glob_ty_v))
            return report_undef();

        TypeId glob_ty = parse_jl_type(safe_cast<jl_datatype_t>(glob_ty_v), ctx);
        if (glob_ty.is_null())
            return report_undef();

        const auto& glob_td = tpool.get_td(glob_ty);

        bool capture_as_uniform = glob_td.is_scalar() || glob_td.is_vector() || glob_td.is_matrix();

        if (capture_as_uniform) {
            QualId qid             = ctx.qual_pool.emplace({QualKind::tq_uniform}, {}).first;
            auto [vdecl_id, vdecl] = ctx.emplace_node<VarDecl>(dre.location, sym->value, glob_ty,
                                                               MaybeScopeType::Global);
            vdecl->qualifiers      = qid;
            infer(vdecl_id);

            captured_uniforms.emplace_back(vdecl_id);

            dre.decl = vdecl_id;
            return vdecl->type;
        }

        return fail(
            fmt::format(
                "found binding for symbol '{}' through Julia in the global scope, but uniform "
                "capturing does not support its type: {}",
                sym_str, type_str(glob_ty)),
            dre);
    }

    return report_undef();
}

TypeId JLSema::visit_Assignment(Assignment& assign) {
    Expr* lhs = ctx.get_node(assign.target);

    if (lhs == nullptr)
        return internal_error("invalid assignment lhs", assign);

    // ! TODO: type might be function or struct type
    if (auto* dre = dyn_cast<DeclRefExpr>(lhs)) {
        Expr* lhs_target = ctx.get_node(dre->decl);

        // uninitialized symbol
        // infer rhs -> define decl from binding info of sym res pass
        if (auto* sym = dyn_cast<SymbolLiteral>(lhs_target)) {
            auto maybe_bt = binding_of(sym->value);

            if (!maybe_bt.has_value()) {
                return internal_error(fmt::format("symbol resolution pass failed to infer binding "
                                                  "type for symbol '{}' in assignment lhs",
                                                  ctx.get_sym(sym->value)),
                                      assign);
            }

            BindingType bt = *maybe_bt;
            NodeId decl_id = resolve_sym_with_binding(sym->value, bt, assign);

            if (decl_id.is_null()) {
                TypeId inf_type = infer(assign.value);
                if (inf_type.is_null())
                    return TypeId::null_id();

                // TODO
                if (bt == BindingType::Captured) {
                    return fail(fmt::format("forward capture of symbol '{}' is not allowed",
                                            ctx.get_sym(sym->value)),
                                assign);
                }

                auto [new_decl_id, new_decl_ptr] =
                    ctx.emplace_node<VarDecl>(sym->location, sym->value, inf_type, bt_to_st(bt));
                new_decl_ptr->initializer = assign.value;

                infer(new_decl_id, true);
                dre->decl = new_decl_id;

                // tie back traversal into the general visitor logic
                infer(assign.target);

                assign.set_is_implicit_decl(true);

                return inf_type;
            }

            assert(ctx.get_node(decl_id) != nullptr);
            dre->decl = decl_id; // this is resolved after the if
        }

        if (ctx.isa<OpaqueFunction>(dre->decl))
            return fail("assignment to Julia-side function is not allowed", assign);

        if (ctx.isa<BuiltinFunction>(dre->decl))
            return fail("assignment to target language's builtin function is not allowed", assign);
    }

    TypeId target_type = infer(assign.target);

    if (const auto* dc = ctx.get_and_dyn_cast<DotChain>(assign.target)) {
        if (!dc->is_resolved()) {
            if (_success)
                fail("couldn't resolve dot chain on assignment lhs", assign);

            return TypeId::null_id();
        }

        auto* dc_res_node = ctx.get_node(dc->resolved_expr);

        if (const auto* acc = dyn_cast<FieldAccess>(dc_res_node)) {
            auto* target_decl = ctx.get_and_dyn_cast<Decl>(acc->target_type_decl);

            if (target_decl == nullptr)
                return internal_error("unresolved target type declaration in FieldAccess node",
                                      *acc);

            if (const auto* sdecl = dyn_cast<StructDecl>(target_decl)) {
                if (!sdecl->is_mutable()) {
                    return fail(
                        fmt::format(
                            "assignment to field of immutable struct type '{}' is not allowed",
                            ctx.get_sym(sdecl->identifier)),
                        assign);
                }
            }
        } else {
            return fail("assignment to non-field-accessing dot chain is not allowed", assign);
        }
    }

    if (target_type.is_null()) {
        if (_success)
            return fail("couldn't infer type for assignment, whose lhs is not a new binding",
                        assign);

        return TypeId::null_id();
    }

    if (assign.value.is_null()) {
        if (!_success)
            return internal_error("rhs of assignment points to null", assign);
        return TypeId::null_id();
    }

    auto* val_expr = ctx.get_node(assign.value);
    assert(val_expr != nullptr);

    TypeCheckResult tcr = check(*val_expr, target_type, false, true);

    if (tcr == TypeCheckResult::Match || tcr == TypeCheckResult::ImplicitCast)
        return target_type;

    if (tcr == TypeCheckResult::Incompatible)
        return TypeId::null_id();

    return fail(
        fmt::format("reassignment of {} to {}", type_str(target_type), type_str(val_expr->type)),
        assign);
}

TypeId JLSema::visit_UpdateAssignment(UpdateAssignment& up_assign) {
    if (up_assign.value.is_null())
        return TypeId::null_id();

    bool is_fn = check(up_assign.update_fn, tpool.any_func_td());
    if (!is_fn)
        return fail("update assignment's operator couldn't be checked to have function type",
                    up_assign);

    auto* fn_dre = ctx.get_and_dyn_cast<DeclRefExpr>(up_assign.update_fn);
    auto* opaq_fn =
        fn_dre != nullptr ? ctx.get_and_dyn_cast<OpaqueFunction>(fn_dre->decl) : nullptr;
    if (opaq_fn == nullptr)
        return internal_error("update assignment's operator resolved to a non-opaque function",
                              up_assign);

    // lhs should be inferrable for valid update assignments
    TypeId lhs_ty   = infer(up_assign.target);
    TypeId value_ty = infer(up_assign.value); // CLEANUP: check call instead of this

    std::vector<TypeId> arg_types = {lhs_ty, value_ty};

    TypeId rhs_ty = ret_type_of_jl_call(opaq_fn->jl_function, opaq_fn->fn_name(), arg_types,
                                        up_assign.is_broadcast(), std::nullopt, up_assign);

    if (rhs_ty.is_null())
        return TypeId::null_id();

    TypeCheckResult tcr = check_type_against(rhs_ty, lhs_ty, up_assign);

    if (tcr != TypeCheckResult::Match) {
        return fail(
            fmt::format("invalid update assignment operator, applying '{}' to lhs and rhs yields a "
                        "type that {}",
                        get_jl_fn_name(opaq_fn->jl_function),
                        tcr != TypeCheckResult::Incompatible
                            ? "can only be stored in the target through an implicit, or explicit "
                              "cast, which is not allowed"
                            : "cannot be converted to the target's expected type"),
            up_assign);
    }

    return rhs_ty;
}

TypeId JLSema::ret_type_of_jl_call(jl_value_t* fn, SymbolId fn_name, std::vector<TypeId>& arg_types,
                                   bool is_broadcast, MaybeArgListRef args, const Expr& base_expr) {
    assert(fn != nullptr);
    assert(!args.has_value() || args->get().size() == arg_types.size());

    // only actually alloc and init string if needed for an error msg
    LazyInit error_suffix{[&]() -> std::string {
        return fmt::format("(in call to function '{}')", get_jl_fn_name(fn));
    }};

    // try to resolve as a builtin operator first
    if (ctx.target_info != nullptr) {
        TargetOpKind t_op = OpMapping::INVALID_OP;

        // builtin unary/binary operators
        if (!arg_types.empty() && arg_types.size() <= 2)
            t_op = op_resolver.get_target_op(fn_name, arg_types.size() == 2);

        TypeId ret_ty = TypeId::null_id();
        bool is_op    = false;

        // CLEANUP: generalize GLSL-specific logic for binops
        if (t_op != OpMapping::INVALID_OP) {
            bool failed_op_res = false;

            if (arg_types.size() == 2 && arg_types[0] == ctx.type_pool.bool_td() &&
                arg_types[1] == ctx.type_pool.bool_td()) {
                // a op b where op in {&, |, xor} can be either bitwise or a non-short-circuiting
                // logical op based on arg types

                // TODO
                // currently, this naively turns non-short-circuiting boolean logic in Julia into
                // short-circuiting logic in GLSL

                if (t_op == TargetOpKind::BinopBitAnd)
                    t_op = TargetOpKind::BinopLogAnd;
                if (t_op == TargetOpKind::BinopBitOr)
                    t_op = TargetOpKind::BinopLogOr;
                if (t_op == TargetOpKind::BinopBitXor)
                    t_op = TargetOpKind::BinopLogXor;
            }

            std::string_view fn_name_str = ctx.get_sym(fn_name);

            // 'int / int' produces float in Julia, we wanna preserve that semantic in GLSL
            // and similarly for int collections
            if (arg_types.size() == 2 && fn_name_str == "/") {
                TypeId lhs_el_type = ctx.type_pool.el_type_of(arg_types[0]);
                TypeId rhs_el_type = ctx.type_pool.el_type_of(arg_types[1]);

                // handles all int/uint sizes
                bool is_lhs_int = ctx.type_pool.is_type_of<IntTD>(lhs_el_type);
                bool is_rhs_int = ctx.type_pool.is_type_of<IntTD>(rhs_el_type);

                if (is_lhs_int && is_rhs_int) {
                    if (args.has_value()) {
                        auto& args_ref = args->get();

                        // in case of failed arg resolution, should have already returned
                        assert(ctx.get_node(args_ref[0]) != nullptr);
                        assert(ctx.get_node(args_ref[1]) != nullptr);

                        SrcLocationId arg0_loc = ctx.get_node(args_ref[0])->location;
                        SrcLocationId arg1_loc = ctx.get_node(args_ref[1])->location;

                        TypeId float_el_type =
                            ctx.config.coerce_to_f32 ? ctx.jl_Float32_t() : ctx.jl_Float64_t();

                        TypeId arg0_cast_ty =
                            ctx.type_pool.with_el_type(arg_types[0], float_el_type);
                        TypeId arg1_cast_ty =
                            ctx.type_pool.with_el_type(arg_types[1], float_el_type);

                        args_ref[0] =
                            ctx.emplace_node<ExplicitCast>(arg0_loc, args_ref[0], arg0_cast_ty)
                                .first;
                        args_ref[1] =
                            ctx.emplace_node<ExplicitCast>(arg1_loc, args_ref[1], arg1_cast_ty)
                                .first;

                        // this also ensures that the broadcasting part doesn't need extra logic to
                        // handle int div correctly, since it uses arg_types as a starting point
                        arg_types[0] = arg0_cast_ty;
                        arg_types[1] = arg1_cast_ty;
                    } else {
                        failed_op_res = true;
                    }
                }
            }

            if (!failed_op_res) {
                ret_ty = ctx.target_info->builtin_op_ret_ty(t_op, arg_types);
                is_op  = ret_ty != TypeId::null_id();
            }
        }

        if (is_op && !ret_ty.is_null())
            return ret_ty;
    }

    if (!ctx.config.forward_fns) {
        return fail(
            fmt::format("couldn't resolve call to '{}'. To use Julia queried information "
                        "and blindly pass it down the pipeline, enable function forwarding.",
                        ctx.get_sym(fn_name)),
            base_expr);
    }

    if (ctx.config.warn_on_jl_sema_query)
        warn(fmt::format("had to resolve function call's return type through julia {}",
                         error_suffix.get()),
             base_expr);

    jl_value_t* type_tuple         = nullptr;
    jl_value_t* type_tuple_with_fn = nullptr;
    jl_value_t* res_jl_type        = nullptr;
    JL_GC_PUSH3(&type_tuple, &type_tuple_with_fn, &res_jl_type); // NOLINT

    const ScopeGuard jl_gc_pop_guard{[&]() { JL_GC_POP(); }};

    jl_value_t* ret_type_fn = ctx.jl_env.module_cache.comp_mod.get_fn("return_type");

    std::vector<jl_value_t*> arg_jl_types{};
    arg_jl_types.reserve(arg_types.size());
    for (TypeId arg_type : arg_types) {
        jl_datatype_t* dt = to_jl_type(arg_type);

        if (dt == nullptr) {
            return fail(fmt::format("argument of type '{}' cannot participate in function call "
                                    "return type resolution {}",
                                    type_str(arg_type), error_suffix.get()),
                        base_expr);
        }

        arg_jl_types.emplace_back(reinterpret_cast<jl_value_t*>(dt));
    }

    type_tuple = jl_apply_tuple_type_v(arg_jl_types.data(), arg_jl_types.size());

    if (is_broadcast) {
        jl_value_t* broadcast_fn = ctx.jl_env.module_cache.base_mod.get_fn("broadcast");
        jl_value_t* fn_type      = jl_typeof(fn);

        std::vector<jl_value_t*> args_with_fn{};
        args_with_fn.resize(arg_jl_types.size() + 1);
        args_with_fn[0] = fn_type;
        std::copy(arg_jl_types.begin(), arg_jl_types.end(), args_with_fn.begin() + 1);

        type_tuple_with_fn = jl_apply_tuple_type_v(args_with_fn.data(), args_with_fn.size());
        res_jl_type        = jl_call2(ret_type_fn, broadcast_fn, type_tuple_with_fn);
    } else {
        type_tuple  = jl_apply_tuple_type_v(arg_jl_types.data(), arg_jl_types.size());
        res_jl_type = jl_call2(ret_type_fn, fn, type_tuple);
    }

    if (check_exceptions()) {
        std::cerr << "the above julia exception occured while trying to resolve the return type of "
                     "a function call\n";

        return fail(fmt::format("couldn't infer return type for call {}", error_suffix.get()),
                    base_expr);
    }

    auto print_infer_info = [&]() {
        if (ctx.config.err_dump_verbosity != DumpVerbosity::None) {
            std::cerr << "\nFunction symbol:      " << get_jl_fn_name(fn)
                      << (is_broadcast ? " (broadcast)" : "");
            std::cerr << "\nInferred signature:   ";
            jl_static_show(jl_stderr_stream(), type_tuple);
            std::cerr << "\nInferred return type: ";
            jl_static_show(jl_stderr_stream(), res_jl_type);
            std::cerr << '\n';
        }
    };

    if (res_jl_type == jl_bottom_type) { // Union{}
        // either function is not callable with given signature, or function body never returns
        // normally (e.g. throw, infinite loop, etc. on every branch)

        // it's not worth it to check hasmethod earlier, since for non-bottom returning cases,
        // it's implied to be true (and so the happy path performs one less julia call)

        print_infer_info();

        jl_value_t* has_method_fn  = ctx.jl_env.module_cache.base_mod.get_fn("hasmethod");
        jl_value_t* has_method_val = jl_call2(has_method_fn, fn, type_tuple);

        if (check_exceptions()) {
            std::cerr << "the above Julia exception occured while trying to check if a function "
                         "has a specific signature\n";

            return internal_error(
                fmt::format("couldn't retrieve call signature validity for function after return "
                            "type has been inferred to be bottom {}",
                            error_suffix.get()),
                base_expr);
        }

        if (has_method_val == jl_false) {
            return fail(
                fmt::format("no method matching the signature inferred from the arguments {}",
                            error_suffix.get()),
                base_expr);
        }

        if (is_broadcast) {
            std::cerr << "Inferred signature: ";
            jl_static_show(jl_stderr_stream(), type_tuple_with_fn);
            std::cerr << "\nInferred return type: ";
            jl_static_show(jl_stderr_stream(), res_jl_type);
            std::cerr << '\n';

            return fail(
                fmt::format(
                    "Julia inferred bottom as the return type for a broadcast call. this probably "
                    "indicates a dimension mismatch in the arguments, but could be caused "
                    "by the target function having no valid return paths too.",
                    error_suffix.get()),
                base_expr);
        }

        return fail(fmt::format("Julia inferred bottom as the return type, meaning the function "
                                "execution never exits normally {}",
                                error_suffix.get()),
                    base_expr);
    }

    if (!jl_is_datatype(res_jl_type) || !rt::is_concrete_type(res_jl_type)) {
        print_infer_info();

        std::cerr << '\n';
        return fail(
            fmt::format("Julia could only infer a non-concrete return type {}", error_suffix.get()),
            base_expr);
    }

    TypeId res_type = parse_jl_type(safe_cast<jl_datatype_t>(res_jl_type), ctx);

    if (res_type.is_null()) {
        print_infer_info();

        return fail(fmt::format("Julia inferred an unsupported return type {}", error_suffix.get()),
                    base_expr);
    }

    return res_type;
}

// nullopt -> error occured
// nullptr -> method not found
std::optional<MethodDecl*> JLSema::find_sig_match(const FunctionDecl& fn_decl,
                                                  const std::vector<TypeId>& arg_types,
                                                  const Expr& base_expr) {
    MethodDecl* target_method = nullptr;
    for (NodeId method_id : fn_decl.methods) {
        auto* mdecl = ctx.get_and_dyn_cast<MethodDecl>(method_id);

        if (mdecl == nullptr) {
            internal_error(fmt::format("non-method-declaration node in method list of "
                                       "function declaration for symbol '{}'",
                                       ctx.get_sym(fn_decl.identifier)),
                           fn_decl);
            return std::nullopt;
        }

        if (arg_types.size() > mdecl->param_decls.size())
            continue;

        auto arg_it    = arg_types.begin();
        bool sig_match = true;
        for (NodeId param_id : mdecl->param_decls) {
            auto* pdecl = ctx.get_and_dyn_cast<ParamDecl>(param_id);

            if (pdecl == nullptr) {
                internal_error(fmt::format("non-parameter-declaration node in parameter list of "
                                           "method declaration for symbol '{}'",
                                           ctx.get_sym(mdecl->identifier)),
                               *mdecl);
                return std::nullopt;
            }

            if (arg_it == arg_types.end()) {
                // assumes that if the current param is default initializable, all the rest are too
                // if not, that should've been caught as an error by the method decl visitor
                if (pdecl->default_initializer.is_null())
                    sig_match = false;

                break;
            }

            if (check_type_against(*arg_it, pdecl->type, base_expr) != TypeCheckResult::Match) {
                sig_match = false;
                break;
            }

            arg_it++;
        }

        if (sig_match) {
            if (mdecl->ret_type.is_null()) {
                if (mdecl == current_method) {
                    fail(fmt::format("recursion on method with implicit return type is not "
                                     "allowed (call to '{}')",
                                     ctx.get_sym(mdecl->identifier)),
                         base_expr);

                    return std::nullopt;
                }

                fail(fmt::format(
                         "call to method '{}' with implicit return type, which has not been "
                         "inferred yet (this is most likely the result of mutually recursive "
                         "methods with implicit return types, which is not allowed)",
                         ctx.get_sym(mdecl->identifier)),
                     base_expr);

                return std::nullopt;
            }

            target_method = mdecl;
            break;
        }
    }

    return target_method;
}

TypeId JLSema::visit_FunctionCall(FunctionCall& fn_call) {
    fn_call.target_fn = try_unwrap_cmpd(fn_call.target_fn);
    if (fn_call.target_fn.is_null())
        return TypeId::null_id();

    // allows pretyped because unfolded binop call chains reuse same parsed target
    bool is_valid_fn = check(fn_call.target_fn, tpool.any_func_td(), true);
    if (!is_valid_fn) {
        if (_success)
            fail("call expression's target couldn't be checked to have function type", fn_call);

        return TypeId::null_id();
    }

    auto* target_node = ctx.get_node(fn_call.target_fn);

    NodeId target_decl = NodeId::null_id();
    if (auto* dre = dyn_cast<DeclRefExpr>(target_node))
        target_decl = dre->decl;
    else if (auto* dc = dyn_cast<DotChain>(target_node))
        target_decl = dc->resolved_expr;
    else
        return internal_error("unexpected node kind as function call's target", fn_call);

    if (target_decl.is_null())
        return internal_error("empty declaration in function call's target", fn_call);

    std::vector<TypeId> arg_types{};
    arg_types.reserve(fn_call.args.size());

    for (NodeId& arg : fn_call.args) {
        if (arg.is_null())
            return internal_error("null node as argument to function call", fn_call);

        TypeId arg_type = infer(arg);

        if (arg_type.is_null()) {
            Expr* arg_expr = ctx.get_node(arg);
            assert(arg_expr != nullptr);

            if (_success)
                fail("cannot infer static type for argument in function call", *arg_expr);

            return TypeId::null_id();
        }

        arg_types.emplace_back(arg_type);
    }

    assert(arg_types.size() == fn_call.args.size());

    // exactly one of these should be non-nullptr by the end of resolution
    FunctionDecl* fn_decl       = nullptr;
    BuiltinFunction* builtin_fn = nullptr;
    OpaqueFunction* opaq_fn     = nullptr;
    StructDecl* struct_decl     = nullptr;
    bool resolved_fn            = false;

    Expr* decl_node = ctx.get_node(target_decl);
    assert(decl_node != nullptr);

    const auto* decl_base = dyn_cast<Decl>(decl_node);
    assert(decl_base != nullptr);

    fn_decl     = dyn_cast<FunctionDecl>(decl_node);
    resolved_fn = fn_decl != nullptr;

    if (!resolved_fn) {
        builtin_fn  = dyn_cast<BuiltinFunction>(decl_node);
        resolved_fn = builtin_fn != nullptr;
    }

    if (!resolved_fn) {
        opaq_fn     = dyn_cast<OpaqueFunction>(decl_node);
        resolved_fn = opaq_fn != nullptr;
    }

    if (!resolved_fn) {
        struct_decl = dyn_cast<StructDecl>(decl_node);
        resolved_fn = struct_decl != nullptr;
    }

    if (!resolved_fn) {
        return fail(
            fmt::format("call to non-function symbol '{}'", ctx.get_sym(decl_base->identifier)),
            fn_call);
    }

    assert(fn_decl != nullptr || opaq_fn != nullptr || builtin_fn != nullptr ||
           struct_decl != nullptr);

    if (builtin_fn == nullptr && opaq_fn == nullptr && fn_call.is_broadcast()) {
        // FEATURE: auto-generate element-wise application fn
        return fail("broadcast call to functions other than target language builtins is "
                    "currently not supported",
                    fn_call);
    }

    if (fn_decl != nullptr) {
        auto target_method = find_sig_match(*fn_decl, arg_types, fn_call);

        if (!target_method.has_value())
            return TypeId::null_id();

        if (*target_method == nullptr) {
            return fail(
                fmt::format("no method matches inferred argument types for function call to '{}'",
                            ctx.get_sym(fn_decl->identifier)),
                fn_call);
        }

        return (*target_method)->ret_type;
    }

    if (builtin_fn != nullptr) {
        assert(ctx.target_info != nullptr);

        std::string_view fn_name_str = ctx.get_sym(builtin_fn->fn_name());

        TypeId ret_ty =
            ctx.target_info->builtin_fn_ret_ty_with_impl_cast(fn_name_str, arg_types).first;

        // these have to be checked to avoid letting calls like cross.(vec3) through
        if (!ret_ty.is_null() && fn_call.is_broadcast()) {
            std::vector<TypeId> arg_el_types{};
            arg_el_types.reserve(arg_types.size());

            for (TypeId arg_type : arg_types)
                arg_el_types.emplace_back(tpool.el_type_of(arg_type, true));

            TypeId el_ret_ty =
                ctx.target_info->builtin_fn_ret_ty_with_impl_cast(fn_name_str, arg_el_types).first;

            if (!ret_ty.is_null() && el_ret_ty.is_null()) {
                return fail(fmt::format("broadcast call is invalid according to the target "
                                        "language, i.e. it has a "
                                        "collection-level overload, but not an "
                                        "element-level one (in call to '{}')",
                                        fn_name_str),
                            fn_call);
            }
        }

        if (ret_ty.is_null()) {
            return fail(fmt::format("builtin function does not have an overload for the inferred "
                                    "argument types (in call to '{}')",
                                    fn_name_str),
                        fn_call);
        }

        return ret_ty;
    }

    if (struct_decl != nullptr) {
        if (struct_decl->field_decls.size() > arg_types.size())
            return fail(fmt::format("not enough arguments in constructor call for struct '{}'",
                                    ctx.get_sym(struct_decl->identifier)),
                        fn_call);

        if (struct_decl->field_decls.size() < arg_types.size())
            return fail(fmt::format("too many arguments in constructor call for struct '{}'",
                                    ctx.get_sym(struct_decl->identifier)),
                        fn_call);

        bool valid_call = true;
        for (size_t i = 0; i < arg_types.size(); i++) {
            const auto* field_decl = ctx.get_and_dyn_cast<FieldDecl>(struct_decl->field_decls[i]);
            assert(field_decl != nullptr);

            bool arg_valid = check(fn_call.args[i], field_decl->type, true);
            valid_call     = valid_call && arg_valid;
        }

        if (!valid_call) {
            return fail(fmt::format("invalid constructor argument types for {}",
                                    type_str(struct_decl->type)),
                        fn_call);
        }

        return struct_decl->type;
    }

    assert(opaq_fn != nullptr);

    // try to handle ctors separately first
    if (jl_is_datatype(opaq_fn->jl_function)) {
        auto* dt = safe_cast<jl_datatype_t>(opaq_fn->jl_function);

        TypeId target_type = parse_jl_type(dt, ctx);

        if (ctx.target_info != nullptr &&
            ctx.target_info->valid_ctor_call(target_type, arg_types)) {
            if (fn_call.is_broadcast())
                return fail("broadcast call to constructor is not allowed", fn_call);

            opaq_fn->set_is_ctor(true);

            return target_type;
        }
    }

    // ret_type_of_jl_call should already print any error necessary
    TypeId ret_type = ret_type_of_jl_call(opaq_fn->jl_function, opaq_fn->fn_name(), arg_types,
                                          fn_call.is_broadcast(), fn_call.args, fn_call);

    return ret_type;
}

TypeId JLSema::visit_LogicalBinOp(LogicalBinOp& lbo) {
    TypeId bool_id = tpool.bool_td();

    check(lbo.lhs, bool_id);
    check(lbo.rhs, bool_id);

    return bool_id;
}

TypeId JLSema::visit_IfExpr(IfExpr& if_) {
    if_.condition = try_unwrap_cmpd(if_.condition);
    if (if_.condition.is_null())
        return TypeId::null_id();

    check(if_.condition, ctx.jl_Bool_t());

    if (if_.false_branch.is_null()) {
        infer(if_.true_branch);

        return ctx.jl_Nothing_t();
    }

    if (is_checking()) {
        check(if_.true_branch);
        check(if_.false_branch);

        return expected_type;
    }

    TypeId inf_tb = infer(if_.true_branch);
    TypeId inf_fb = infer(if_.false_branch);

    if (inf_tb == inf_fb)
        return inf_tb;

    return ctx.jl_Nothing_t();
}

TypeId JLSema::visit_WhileExpr(WhileExpr& while_) {
    if (while_.condition.is_null())
        return fail("empty condition in while expression", while_);

    while_.condition = try_unwrap_cmpd(while_.condition);
    if (while_.condition.is_null())
        return TypeId::null_id();

    auto* cmpd = ctx.get_and_dyn_cast<CompoundExpr>(while_.body);
    if (cmpd == nullptr)
        return fail("non-compound-expression node used as a while expression's body", while_);

    CompoundExpr wrapper_cmpd{while_.location, {while_.condition, while_.body}};
    {
        ScopeRAII scope{*this, ScopeKind::Soft, wrapper_cmpd};
        if (!scope.sym_res_successful())
            return fail("symbol resolution pass failed for body of while expression", while_);

        check(while_.condition, ctx.jl_Bool_t());

        infer(while_.body);
    }

    return ctx.jl_Nothing_t();
}

TypeId JLSema::visit_ReturnStmt(ReturnStmt& ret) {
    if (current_method == nullptr)
        return fail("return statement outside of method body", ret);

    bool has_inner = !ret.inner.is_null();

    if (has_inner) {
        ret.inner = try_unwrap_cmpd(ret.inner);

        if (ret.inner.is_null())
            return TypeId::null_id();
    }

    // first return in body
    if (current_fn_ret.is_null()) {
        if (has_inner)
            current_fn_ret = infer(ret.inner);
        else
            current_fn_ret = ctx.jl_Nothing_t();
    } else {
        if (has_inner)
            check(ret.inner, current_fn_ret);
        else if (current_fn_ret != ctx.jl_Nothing_t())
            return fail(fmt::format("empty return stmt in function expected to return {}",
                                    type_str(current_fn_ret)),
                        ret);
    }

    return tpool.void_td();
}

TypeId JLSema::visit_ContinueStmt([[maybe_unused]] ContinueStmt& cont) {
    return tpool.void_td();
}

TypeId JLSema::visit_BreakStmt([[maybe_unused]] BreakStmt& brk) {
    return tpool.void_td();
}

} // namespace stc::jl
