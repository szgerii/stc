#include "frontend/jl/sema.h"
#include "base.h"
#include "frontend/jl/ast_utils.h"
#include "frontend/jl/dumper.h"
#include "frontend/jl/rt/utils.h"
#include "frontend/jl/sema.h"
#include "julia_guard.h"
#include "types/type_to_string.h"

namespace {

using namespace stc::jl;

[[nodiscard]] STC_FORCE_INLINE std::string_view scope_str(ScopeType scope) {
    switch (scope) {
        case ScopeType::Global:
            return "global";

        case ScopeType::Local:
            return "local";
    }

    throw std::logic_error{"Unaccounted ScopeType value in scope_str"};
}

[[nodiscard]] STC_FORCE_INLINE ScopeType bt_to_st(BindingType bt) {
    if (bt == BindingType::Captured)
        throw std::logic_error{"Trying to convert BindingType with value Captured to a ScopeType"};

    assert((bt == BindingType::Global || bt == BindingType::Local) && "unaccounted binding type");
    return bt == BindingType::Global ? ScopeType::Global : ScopeType::Local;
}

[[nodiscard]] STC_FORCE_INLINE std::string_view bt_str(BindingType bt) {
    if (bt == BindingType::Captured)
        return "captured";

    return scope_str(bt_to_st(bt));
}

} // namespace

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

TypeId JLSema::warn(std::string_view msg, const Expr& expr) {
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
    auto cmpd = ctx.get_and_dyn_cast<CompoundExpr>(cmpd_id);

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

    return cmpd->body[0];
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

        bool rewrite = isa<VarDecl>(decl) || isa<ParamDecl>(decl) || isa<MethodDecl>(decl);
        if (!rewrite)
            continue;

        SymbolId usym = get_usym_for(decl->identifier, ctx.sym_pool);
        if (usym.is_null()) {
            internal_error("couldn't generate unique symbol for declaration", *decl);
            return false;
        }

        decl->identifier = usym;
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

    // uses index lookup for scopes and dmq, because visit_method_body could force scopes or
    // dmq to grow, potentially creating dangling references/invalidated iterators

    size_t scope_idx = scopes.size() - 1;
    for (size_t i = 0; i < scopes[scope_idx].deferred_method_queue.size(); i++) {
        NodeId m_id = scopes[scope_idx].deferred_method_queue[i];
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
    }

    // mangle_scope already reports its own errors (and sets _success)
    if (!skip_mangle)
        mangle_scope(scopes[scope_idx]);

    scopes.pop_back();
}

bool JLSema::check_type_against(TypeId actual_type, TypeId checked_type) const {
    if (actual_type.is_null()) {
        // TODO: custom error
        return false;
    }

    if (actual_type == checked_type)
        return true;

    const auto& expected_td = tpool.get_td(checked_type);
    auto actual_td = LazyInit{[&]() -> const TypeDescriptor& { return tpool.get_td(actual_type); }};

    // TODO: subsumption, promotion

    // if expected fn type has an identifier, actual has to match it
    // if it doesnt, only the function-ness of the actual type is checked
    if (expected_td.is_function() && actual_td.get().is_function()) {
        auto expected_fn = expected_td.as<FunctionTD>();

        return expected_fn.identifier.is_null() ||
               expected_fn.identifier == actual_td.get().as<FunctionTD>().identifier;
    }

    if (expected_td.is_array() && actual_td.get().is_array()) {
        ArrayTD expected_arr_ty = expected_td.as<ArrayTD>();
        ArrayTD actual_arr_ty   = actual_td.get().as<ArrayTD>();

        if (!check_type_against(actual_arr_ty.element_type_id, expected_arr_ty.element_type_id))
            return false;

        if (tpool.is_array_any_size(checked_type))
            return true;

        return actual_arr_ty.length == expected_arr_ty.length;
    }

    if (expected_td.is_vector() && actual_td.get().is_vector()) {
        VectorTD expected_vec_ty = expected_td.as<VectorTD>();
        VectorTD actual_vec_ty   = actual_td.get().as<VectorTD>();

        if (!check_type_against(actual_vec_ty.component_type_id, expected_vec_ty.component_type_id))
            return false;

        if (tpool.is_vec_any_size(checked_type))
            return true;

        return actual_vec_ty.component_count == expected_vec_ty.component_count;
    }

    if (expected_td.is_matrix() && actual_td.get().is_matrix()) {
        MatrixTD expected_mat_ty = expected_td.as<MatrixTD>();
        MatrixTD actual_mat_ty   = actual_td.get().as<MatrixTD>();

        assert(tpool.is_type_of<VectorTD>(actual_mat_ty.column_type_id));
        assert(tpool.is_type_of<VectorTD>(expected_mat_ty.column_type_id));

        VectorTD actual_col_ty   = tpool.get_td(actual_mat_ty.column_type_id).as<VectorTD>();
        VectorTD expected_col_ty = tpool.get_td(expected_mat_ty.column_type_id).as<VectorTD>();

        if (!check_type_against(actual_col_ty.component_type_id, expected_col_ty.component_type_id))
            return false;

        if (tpool.is_mat_any_size(checked_type))
            return true;

        return actual_mat_ty.column_count == expected_mat_ty.column_count &&
               actual_col_ty.component_count == expected_col_ty.component_count;
    }

    return false;
}

bool JLSema::check(Expr& expr, TypeId checked_type, bool allow_pretyped) {
    bool old_pretyped_value = allow_pretyped_nodes;
    if (allow_pretyped)
        allow_pretyped_nodes = true;

    // method decls will, by the nature of their resolution flow, get invoked multiple times
    if (!allow_pretyped_nodes && !expr.type.is_null() && !isa<MethodDecl>(&expr)) {
        internal_error("type check function called with an expression whose type has "
                       "already been determined, while the allow_pretyped argument is false",
                       expr);

        return false;
    }
    auto prev_expected  = this->expected_type;
    this->expected_type = checked_type;

    const ScopeGuard exp_type_scope_guard{[&]() {
        this->expected_type  = prev_expected;
        allow_pretyped_nodes = old_pretyped_value;
    }};

    // delegate type checking for returns to their visitor-only
    // this is because they're checked against the current fn return type instead of an expected
    // type, given that they always should resolve to the void type (or null on failure)
    if (isa<ReturnStmt>(expr)) {
        expr.type = impl_this()->visit(&expr);
        assert(expr.type.is_null() || expr.type == tpool.void_td());

        return !expr.type.is_null();
    }

    TypeId actual_type = expr.type.is_null() ? impl_this()->visit(&expr) : expr.type;

    if (_success && !actual_type.is_null() && !visiting_indexer &&
        tpool.get_td(actual_type).is_builtin(BuiltinTypeKind::String))
        fail("nodes representing strings are not allowed", expr);

    if (!check_type_against(actual_type, checked_type)) {
        // if actual_type is null, that's mostly an already reported error
        if (_success || !actual_type.is_null())
            fail(std::format("type mismatch during type checking: expected {}, got {}",
                             type_str(checked_type), type_str(actual_type)),
                 expr);

        return false;
    }

    // TODO
    // maybe it would be wiser to use expected_type here, and do manual case-by-case rewriting (e.g.
    // for any-sized-array or any func)
    expr.type = actual_type;

    return true;
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
                    "x::Int), as without them, the language construct is officially a type assert, "
                    "not a declaration",
                    vdecl);
    }

    TypeId result_type = TypeId::null_id();

    bool has_type = !vdecl.annot_type.is_null();
    bool has_init = !vdecl.initializer.is_null();

    auto expected_bt = binding_of(vdecl.identifier);

    if (!expected_bt.has_value())
        return internal_error(std::format("symbol resolution pass failed to infer binding type for "
                                          "a variable declaration's symbol: '{}'",
                                          ctx.get_sym(vdecl.identifier)),
                              vdecl);
    else if (*expected_bt == BindingType::Captured)
        return internal_error("wrongfully inferred binding type of Captured for a symbol that is "
                              "explicitly declared in scope",
                              vdecl);

    ScopeType expected_st = bt_to_st(*expected_bt);
    ScopeType actual_st   = mst_to_st(vdecl.scope());

    if (expected_st == ScopeType::Global && actual_st == ScopeType::Local) {
        return fail(std::format("cannot declare symbol '{}' local in the global scope",
                                ctx.get_sym(vdecl.identifier)),
                    vdecl);
    }

    if (actual_st != expected_st) {
        return internal_error(
            std::format(
                "scope mismatch for target scope of variable declaration. inferred {}, found {}",
                scope_str(expected_st), scope_str(actual_st)),
            vdecl);
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

    // CLEANUP: add info about new and old decl type (here and elsewhere)
    NodeId decl_id = ctx.calculate_node_id(vdecl);
    bool added     = st_register(vdecl.identifier, decl_id);
    if (!added) {
        return fail(std::format("redeclaration of symbol '{}' in the same scope (as a variable)",
                                ctx.get_sym(vdecl.identifier)),
                    vdecl);
    }

    return result_type;
}

TypeId JLSema::visit_ParamDecl(ParamDecl& pdecl) {
    // TODO: support for kwargs (here and in MethodDecl visitor)
    if (pdecl.is_kwarg())
        return fail("kwargs are currently not supported", pdecl);

    /*
    auto expected_bt = binding_of(pdecl.identifier);

    if (!expected_bt.has_value())
        return internal_error(
            std::format(
                "symbol resolution pass failed to infer binding type for parameter symbol '{}'",
                ctx.get_sym(pdecl.identifier)),
            pdecl);

    if (*expected_bt != BindingType::Local)
        return internal_error(
            std::format("wrongfully inferred non-local binding type for parameter symbol '{}'",
                        ctx.get_sym(pdecl.identifier)),
            pdecl);
    */

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
                std::format("redeclaration of symbol '{}' in the same scope (as a parameter)",
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

TypeId JLSema::visit_FunctionDecl(FunctionDecl& fn_decl) {
    if (fn_decl.identifier.is_null())
        return internal_error("function declaration with null as the identifier symbol", fn_decl);

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
            fail(std::format("Trying to overwrite the method definition for an already "
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
    if (!ctx.isa<CompoundExpr>(method.body))
        return fail("method declaration with non-compound expression as a body is not allowed",
                    method);

    NodeId method_id = ctx.calculate_node_id(method);

    auto expected_bt = binding_of(method.identifier);
    if (!expected_bt.has_value())
        return internal_error(std::format("symbol resolution pass failed to infer binding type for "
                                          "declaration of a method for '{}'",
                                          ctx.get_sym(method.identifier)),
                              method);

    if (*expected_bt == BindingType::Captured)
        return internal_error(std::format("symbol resolution pass inferred binding type of "
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
                return fail(std::format("trying to declare '{}' as a function, but it has already "
                                        "been declared as a non-function symbol previously",
                                        ctx.get_sym(method.identifier)),
                            method);

            for (NodeId decled_m_id : fn_decl->methods) {
                auto* decled_m = ctx.get_and_dyn_cast<MethodDecl>(decled_m_id);

                if (decled_m == nullptr)
                    return internal_error(std::format("nullptr found in method list of a function "
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
                std::format(
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
                else
                    return fail(
                        std::format("redeclaration of already declared symbol '{}' as a method",
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
                    std::format("redeclaration of symbol '{}' in the same scope (as a method)",
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
    for (ParamDecl& pdecl : param_decls) {
        if (!pdecl.default_initializer.is_null())
            break;

        // type might be null here. that's okay, as long as we can infer it later
        param_types.emplace_back(pdecl.annot_type);
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
                        std::format(
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
    for (ParamDecl& pdecl : param_decls) {
        NodeId pdecl_id = ctx.calculate_node_id(pdecl);

        bool used_before = !used_param_ids.emplace(pdecl.identifier).second;
        if (used_before) {
            fail(std::format("more than one parameter named '{}' in definition of function '{}'",
                             ctx.get_sym(pdecl.identifier), ctx.get_sym(method.identifier)),
                 pdecl);
            any_failed = true;
            continue;
        }

        bool added = st_register(pdecl.identifier, pdecl_id);
        if (!added) {
            assert(!_success);
            // this should have been reported by paramdecl visitor already
            // fail(std::format("redeclaration of symbol '{}' in the same scope (as a parameter)",
            //                  ctx.get_sym(pdecl.identifier)),
            //      pdecl);
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
                std::format("non-compound-expression as body of method definition for '{}'",
                            ctx.get_sym(method.identifier)),
                method);
            return;
        }

        visiting_method_body = true;
        TypeId body_inf      = infer(method.body);
        visiting_method_body = false;

        if (current_fn_ret.is_null()) {
            fail(std::format("couldn't infer return type from a method definition for '{}'. Try "
                             "adding an explicit return type to the function header.",
                             ctx.get_sym(method.identifier)),
                 method);
            return;
        }

        if (current_fn_ret != body_inf) {
            internal_error(
                std::format(
                    "inferred return type based on return statements ({}) got desynchronized "
                    "from body's inferred type ({}) for a method of '{}'",
                    type_str(current_fn_ret), type_str(body_inf), ctx.get_sym(method.identifier)),
                method);
            return;
        }

        method.ret_type = current_fn_ret;
    }

    current_method = prev_method;
    current_fn_ret = prev_ret;
}

// TODO: structs

TypeId JLSema::visit_FieldDecl(FieldDecl& fdecl) {
    return fail("Structs are not currently supported", fdecl);
}

TypeId JLSema::visit_StructDecl(StructDecl& sdecl) {
    return fail("Structs are not currently supported", sdecl);
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

    uint32_t len = static_cast<uint32_t>(arr_lit.members.size());

    TypeId checked_el_type = TypeId::null_id();
    if (is_checking()) {
        assert(!expected_type.is_null());
        const auto& arr_td = tpool.get_td(expected_type);
        ArrayTD arr_ty     = arr_td.as<ArrayTD>();

        if (arr_td.is_array())
            checked_el_type = arr_ty.element_type_id;
    }

    if (!checked_el_type.is_null()) {
        for (NodeId member : arr_lit.members)
            check(member, checked_el_type);

        return tpool.array_td(checked_el_type, len);
    }

    if (arr_lit.members.empty())
        return fail("cannot infer element type for empty array literal", arr_lit);

    TypeId inferred_el_type = infer(arr_lit.members[0]);

    if (inferred_el_type.is_null())
        return fail("failed to infer element type for first element of array literal", arr_lit);

    bool any_check_failed = false;
    for (size_t i = 1; i < arr_lit.members.size(); i++)
        any_check_failed = any_check_failed || !check(arr_lit.members[i], inferred_el_type);

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

    if (is_checking()) {
        bool any_coll_match = check_type_against(coll_type, tpool.any_vec_td(expected_type)) ||
                              (tpool.get_td(expected_type).is_scalar() &&
                               (check_type_against(coll_type, tpool.any_mat_td(expected_type)) ||
                                check_type_against(coll_type, tpool.any_array_td(expected_type))));

        if (!any_coll_match) {
            return fail(std::format("target of an indexer expression could not be checked to hold "
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
            return fail(std::format("invalid vector indexer type '{}'",
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
                return fail(std::format("invalid swizzle expression length for '{}'", swizzle),
                            idx_expr);

            uint32_t vec_n = coll_td.as<VectorTD>().component_count;
            SwizzleSet set = SwizzleSet::Invalid;

            for (size_t i = 0; i < swizzle_n; i++) {
                auto [comp, cur_set] = parse_swizzle_component(swizzle[i]);

                if (comp == 0xFF)
                    return fail(
                        std::format("invalid swizzle expression component '{}'", swizzle[i]),
                        idx_expr);

                if (cur_set == SwizzleSet::Invalid)
                    return internal_error(
                        "invalid swizzle set returned for valid swizzle component", idx_expr);

                if (set == SwizzleSet::Invalid)
                    set = cur_set;
                else if (set != cur_set)
                    return fail(
                        std::format(
                            "mixing components from different swizzle sets is not allowed ('{}')",
                            swizzle),
                        idx_expr);

                assert(vec_n != 0);
                if (comp > vec_n - 1)
                    return fail(std::format("swizzle component '{}' points outside "
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

TypeId JLSema::visit_ModuleLookup(ModuleLookup& ml) {
    return internal_error(
        "sema pass reached a module lookup \"leaf subtree\", which should never happen", ml);
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

TypeId JLSema::visit_DeclRefExpr(DeclRefExpr& dre) {
    auto* inner = ctx.get_node(dre.decl);
    if (inner == nullptr)
        return internal_error("declaration reference expression points to null", dre);

    // already resolved state
    if (auto* decl = dyn_cast<Decl>(inner))
        return decl->type;

    if (auto* gref = dyn_cast<GlobalRef>(inner))
        return fail("global refs are currently not supported", *gref);

    auto* sym = dyn_cast<SymbolLiteral>(inner);
    auto* ml  = dyn_cast<ModuleLookup>(inner);
    if (sym == nullptr && ml == nullptr)
        return internal_error("declaration reference expression points to invalid node kind", dre);

    if (ml != nullptr) {
        if (!tpool.is_any_func(expected_type))
            return fail("module lookups are currently only supported for function resolutions",
                        dre);

        if (ml->chain.empty())
            return internal_error(
                "empty module lookup as target of a declaration reference expression", *ml);

        std::string mod_path = mod_chain_to_path(ml->chain, ctx, ml->chain.size() - 1);

        auto mod = ctx.jl_env.module_cache.get_mod(mod_path);

        if (!mod.has_value())
            return fail(
                std::format("invalid module lookup chain, couldn't resolve target module for '{}'",
                            mod_path),
                *ml);

        auto* sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(ml->chain.back());
        if (sym_lit == nullptr)
            return internal_error("unexpected non-symbol-literal node in module lookup chain", *ml);

        jl_function_t* jl_fn = mod->get().get_fn(ctx.get_sym(sym_lit->value), false);
        if (jl_fn == nullptr)
            return fail(std::format("no function with name '{}' was found in module '{}'",
                                    ctx.get_sym(sym_lit->value), mod_path),
                        *ml);

        SymbolId fn_name_id = ctx.sym_pool.get_id(get_jl_fn_name(jl_fn));

        dre.decl = ctx.emplace_node<OpaqueFunction>(dre.location, fn_name_id, jl_fn).first;
        infer(dre.decl);

        return tpool.func_td(fn_name_id);
    }

    auto maybe_bt = binding_of(sym->value);

    if (!maybe_bt.has_value())
        return internal_error(std::format("symbol resolution pass failed to infer binding type for "
                                          "symbol '{}' in a declaration",
                                          ctx.get_sym(sym->value)),
                              *sym);

    bool is_captured   = *maybe_bt == BindingType::Captured;
    NodeId reffed_decl = find_sym(sym->value);

    if (!reffed_decl.is_null()) {
        auto* decl = ctx.get_and_dyn_cast<Decl>(reffed_decl);
        if (decl == nullptr)
            return internal_error("non-declaration node in symbol table", dre);

        dre.decl = reffed_decl;

        return decl->type;
    }

    if (is_captured) {
        return fail(
            std::format("forward capture of symbol '{}' is not allowed", ctx.get_sym(sym->value)),
            dre);
    }

    bool is_fn_ref = *maybe_bt == BindingType::Global && tpool.is_any_func(expected_type);
    if (is_fn_ref) {
        // try to resolve in JuliaGLM first
        jl_function_t* jl_fn =
            ctx.jl_env.module_cache.glm_mod.get_fn(ctx.get_sym(sym->value), false);

        if (jl_fn == nullptr)
            jl_fn = find_jl_function(ctx.get_sym(sym->value), ctx.jl_env, false);

        if (jl_fn == nullptr) {
            return fail(std::format("couldn't find function '{}' in the symbol table, or in the "
                                    "root julia module",
                                    ctx.get_sym(sym->value)),
                        dre);
        }

        if (jl_is_type(jl_fn)) {
            // TODO: ctors
        }

        SymbolId fn_name = ctx.sym_pool.get_id(get_jl_fn_name(jl_fn));

        dre.decl = ctx.emplace_node<OpaqueFunction>(dre.location, fn_name, jl_fn).first;
        infer(dre.decl);

        if (ctx.config.warn_on_jl_sema_query)
            warn(std::format("had to resolve function reference through julia for symbol '{}'",
                             ctx.get_sym(fn_name)),
                 dre);

        return tpool.func_td(fn_name);
    }

    return fail(std::format("use of undeclared or uninitialized symbol '{}' (binding type: {})",
                            ctx.get_sym(sym->value), bt_str(*maybe_bt)),
                dre);
}

TypeId JLSema::visit_Assignment(Assignment& assign) {
    Expr* lhs = ctx.get_node(assign.target);

    if (lhs == nullptr)
        return internal_error("invalid assignment lhs", assign);

    // ! TODO: type might be function type
    if (auto* dre = dyn_cast<DeclRefExpr>(lhs)) {
        Expr* lhs_target = ctx.get_node(dre->decl);

        // uninitialized symbol
        // infer rhs -> define decl from binding info of sym res pass
        if (auto* sym = dyn_cast<SymbolLiteral>(lhs_target)) {
            NodeId decl_id = find_sym(sym->value);

            if (decl_id.is_null()) {
                TypeId inf_type = infer(assign.value);
                if (inf_type.is_null())
                    return TypeId::null_id();

                auto maybe_bt = binding_of(sym->value);

                if (!maybe_bt.has_value())
                    return internal_error(
                        std::format("symbol resolution pass failed to infer binding "
                                    "type for symbol '{}' in assignment lhs",
                                    ctx.get_sym(sym->value)),
                        assign);

                BindingType bt = *maybe_bt;

                if (bt == BindingType::Captured) {
                    // TODO:
                    // build capture sema into MethodDecl, allow generating captures-as-args
                    // that could, in turn, allow this to be handled
                    return fail(
                        std::format(
                            "assignment to captured symbol before definition for symbol '{}'. "
                            "Currently, all variables must be assigned before they can be captured "
                            "by an inner function.",
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

            // already initialized symbol
            // infer lhs -> check rhs
            dre->decl = decl_id;
        }

        if (ctx.isa<ModuleLookup>(dre->decl))
            return fail("assignment to module resolved name is not allowed", assign);

        if (ctx.isa<OpaqueFunction>(dre->decl))
            return fail("assignment to Julia-side function is not allowed", assign);
    }

    TypeId target_type = infer(assign.target);
    check(assign.value, target_type);

    return target_type;
}

// TODO: add jl dumps for types
// TODO: print inferred sig
TypeId JLSema::ret_type_of_jl_call(jl_function_t* fn, const std::vector<TypeId>& arg_types,
                                   const Expr& base_expr) {
    assert(fn != nullptr);

    // only actually alloc and init string if needed for an error msg
    auto error_suffix = LazyInit{[&]() -> std::string {
        return std::format("(in call to function '{}')", get_jl_fn_name(fn));
    }};

    if (ctx.config.warn_on_jl_sema_query)
        warn(std::format("had to resolve function call's return type through julia {}",
                         error_suffix.get()),
             base_expr);

    jl_value_t* type_tuple  = nullptr;
    jl_value_t* res_jl_type = nullptr;
    JL_GC_PUSH2(&type_tuple, &res_jl_type);

    const ScopeGuard jl_gc_pop_guard{[&]() { JL_GC_POP(); }};

    jl_function_t* ret_type_fn = ctx.jl_env.module_cache.comp_mod.get_fn("return_type");

    std::vector<jl_value_t*> arg_jl_types{};
    arg_jl_types.reserve(arg_types.size());
    for (TypeId arg_type : arg_types) {
        jl_datatype_t* dt = to_jl_type(arg_type);

        if (dt == nullptr) {
            return fail(std::format("argument of type '{}' cannot participate in function call "
                                    "return type resolution {}",
                                    type_str(arg_type), error_suffix.get()),
                        base_expr);
        }

        arg_jl_types.emplace_back(reinterpret_cast<jl_value_t*>(dt));
    }

    type_tuple  = jl_apply_tuple_type_v(arg_jl_types.data(), arg_jl_types.size());
    res_jl_type = jl_call2(ret_type_fn, fn, type_tuple);

    if (check_exceptions()) {
        std::cerr << "the above julia exception occured while trying to resolve the return type of "
                     "a function call\n";

        return fail(std::format("couldn't infer return type for call {}", error_suffix.get()),
                    base_expr);
    }

    if (res_jl_type == reinterpret_cast<jl_value_t*>(jl_bottom_type)) { // Union{}
        // either function is not callable with given signature, or function body never returns
        // normally (e.g. throw, infinite loop, etc. on every branch)

        // it's not worth it to check hasmethod earlier, since for non-bottom returning cases, it's
        // implied to be true (and so the happy path performs one less julia call)

        jl_function_t* has_method_fn = ctx.jl_env.module_cache.base_mod.get_fn("hasmethod");
        jl_value_t* has_method_val   = jl_call2(has_method_fn, fn, type_tuple);

        if (check_exceptions()) {
            std::cerr << "the above Julia exception occured while trying to check if a function "
                         "has a specific signature\n";

            return internal_error(
                std::format("couldn't retrieve call signature validity for function after return "
                            "type has been inferred to be bottom {}",
                            error_suffix.get()),
                base_expr);
        }

        if (has_method_val == jl_false) {
            return fail(
                std::format("no method matching the signature inferred from the arguments {}",
                            error_suffix.get()),
                base_expr);
        }

        return fail(std::format("Julia inferred bottom as the return type, meaning the function "
                                "execution never exits normally {}",
                                error_suffix.get()),
                    base_expr);
    }

    if (!jl_is_datatype(res_jl_type) || !jl_is_concrete_type(res_jl_type)) {
        std::cerr << "Inferred signature: ";
        jl_static_show(jl_stderr_stream(), type_tuple);
        std::cerr << "\nInferred return type: ";
        jl_static_show(jl_stderr_stream(), res_jl_type);
        std::cerr << '\n';
        return fail(
            std::format("Julia could only infer a non-concrete return type {}", error_suffix.get()),
            base_expr);
    }

    TypeId res_type = parse_jl_type(safe_cast<jl_datatype_t>(res_jl_type), ctx);

    if (res_type.is_null())
        return fail(std::format("Julia inferred an unsupported return type {}", error_suffix.get()),
                    base_expr);

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
            internal_error(std::format("non-method-declaration node in method list of "
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
                internal_error(std::format("non-parameter-declaration node in parameter list of "
                                           "method declaration for symbol '{}'",
                                           ctx.get_sym(mdecl->identifier)),
                               *mdecl);
                return std::nullopt;
            }

            if (arg_it == arg_types.end()) {
                // assumes that if the current param is default initializable, all the rest are too
                // if not, that should've be caught as an error by the method decl visitor
                if (pdecl->default_initializer.is_null())
                    sig_match = false;

                break;
            }

            if (!check_type_against(*arg_it, pdecl->type)) {
                sig_match = false;
                break;
            }

            arg_it++;
        }

        if (sig_match) {
            if (mdecl->ret_type.is_null()) {
                if (mdecl == current_method) {
                    fail(std::format("recursion on method with implicit return type is not "
                                     "allowed (call to '{}')",
                                     ctx.get_sym(mdecl->identifier)),
                         base_expr);
                    return std::nullopt;
                } else {
                    fail(std::format(
                             "call to method '{}' with implicit return type, which has not been "
                             "inferred yet (this is most likely the result of mutually recursive "
                             "methods with implicit return types, which is not allowed)",
                             ctx.get_sym(mdecl->identifier)),
                         base_expr);
                    return std::nullopt;
                }
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

    bool is_valid_fn = check(fn_call.target_fn, tpool.any_func_td());
    if (!is_valid_fn)
        return fail("call expression's target couldn't be checked to have function type", fn_call);

    auto* fn_dre = ctx.get_and_dyn_cast<DeclRefExpr>(fn_call.target_fn);

    if (fn_dre == nullptr)
        return internal_error("unexpected node kind as function call's target", fn_call);

    FunctionDecl* fn_decl   = nullptr;
    OpaqueFunction* opaq_fn = nullptr;

    if (fn_dre->decl.is_null())
        return internal_error("empty declaration in function call's target", fn_call);

    Expr* decl_expr = ctx.get_node(fn_dre->decl);
    assert(decl_expr != nullptr);

    const auto* decl_base = dyn_cast<Decl>(decl_expr);
    assert(decl_base != nullptr);

    fn_decl = dyn_cast<FunctionDecl>(decl_expr);

    if (fn_decl == nullptr)
        opaq_fn = dyn_cast<OpaqueFunction>(decl_expr);

    if (fn_decl == nullptr && opaq_fn == nullptr) {
        return fail(
            std::format("call to non-function symbol '{}'", ctx.get_sym(decl_base->identifier)),
            fn_call);
    }

    std::vector<TypeId> arg_types{};
    arg_types.reserve(fn_call.args.size());

    for (NodeId arg : fn_call.args) {
        if (arg.is_null())
            return internal_error("null node as argument to function call", fn_call);

        TypeId arg_type = infer(arg);

        if (arg_type.is_null()) {
            Expr* arg_expr = ctx.get_node(arg);
            assert(arg_expr != nullptr);

            return fail("cannot infer static type for argument in function call", *arg_expr);
        }

        arg_types.emplace_back(arg_type);
    }

    assert(arg_types.size() == fn_call.args.size());

    if (fn_decl != nullptr) {
        auto target_method = find_sig_match(*fn_decl, arg_types, fn_call);

        if (!target_method.has_value())
            return TypeId::null_id();

        if (*target_method == nullptr) {
            return fail(
                std::format("no method matches inferred argument types for function call to '{}'",
                            ctx.get_sym(fn_decl->identifier)),
                fn_call);
        }

        return (*target_method)->ret_type;
    }

    assert(opaq_fn != nullptr);

    // ret_type_of_jl_call should already print any error necessary
    return ret_type_of_jl_call(opaq_fn->jl_function, arg_types, fn_call);
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
            return fail(std::format("empty return stmt in function expected to return {}",
                                    type_str(current_fn_ret)),
                        ret);
    }

    return TypePool::void_td();
}

TypeId JLSema::visit_ContinueStmt([[maybe_unused]] ContinueStmt& cont) {
    return TypePool::void_td();
}

TypeId JLSema::visit_BreakStmt([[maybe_unused]] BreakStmt& brk) {
    return TypePool::void_td();
}

} // namespace stc::jl
