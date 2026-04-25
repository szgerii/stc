#include "frontend/jl/lowering.h"
#include "frontend/jl/ast_utils.h"
#include "sir/ast.h"
#include "types/type_to_string.h"

#include <array>

namespace {

using SIRNodeId = stc::sir::NodeId;
using namespace stc::jl;

[[nodiscard]] STC_FORCE_INLINE bool is_ret_type_allowed(const TypeDescriptor& ret_type) {
    return ret_type.is_scalar() || ret_type.is_vector() || ret_type.is_matrix() ||
           ret_type.is_array() || ret_type.is_struct() ||
           ret_type.is_builtin(BuiltinTypeKind::Nothing);
}

} // namespace

namespace stc::jl {

bool JLLoweringVisitor::pre_visit_ptr(Expr* expr) {
    swap_lower_type(expr->type);
    return true;
}

SIRNodeId JLLoweringVisitor::visit_default_case() {
    internal_error("nullptr found in the Julia AST during lowering to SIR");
    this->_success = false;

    return SIRNodeId::null_id();
}

SIRNodeId JLLoweringVisitor::fail(std::string_view msg) {
    stc::error(msg);
    _success = false;
    return SIRNodeId::null_id();
}

SIRNodeId JLLoweringVisitor::internal_error(std::string_view msg) {
    stc::internal_error(msg);
    _success = false;
    return SIRNodeId::null_id();
}

SIRNodeId JLLoweringVisitor::visit_and_check(NodeId id) {
    SIRNodeId result = visit(id);

    if (result.is_null()) {
        if (_success)
            internal_error("null_id returned by a node in the Julia -> SIR lowering visitor.");

        return SIRNodeId::null_id();
    }

    return result;
}

SIRNodeId JLLoweringVisitor::visit_ptr(Expr* node) {
    return this->dispatch_wrapper(node);
}

SIRNodeId JLLoweringVisitor::lower(NodeId global_cmpd_id) {
    auto* global_cmpd = ctx.get_and_dyn_cast<CompoundExpr>(global_cmpd_id);
    if (global_cmpd == nullptr)
        return internal_error(
            "Null id passed to Julia -> SIR lowering pass as the global scope body");

    auto& body = global_cmpd->body;

    // lift global var decls (including implicit ones) and method decls to the top
    std::vector<NodeId> prepended_exprs{};
    bool encountered_real_body = false;
    NodeId explicit_main       = NodeId::null_id(); // TODO: error on redecl
    size_t struct_count        = 0;
    for (size_t i = 0; i < body.size();) {
        NodeId expr    = body[i];
        auto ptrdiff_i = static_cast<ptrdiff_t>(i);

        if (ctx.isa<MethodDecl>(expr)) {
            body.erase(body.begin() + ptrdiff_i);

            if (sir_ctx.get_sym(ctx.get_and_dyn_cast<MethodDecl>(expr)->identifier) != "main")
                prepended_exprs.emplace_back(expr);
            else
                explicit_main = expr;

            continue;
        }

        if (ctx.isa<StructDecl>(expr)) {
            prepended_exprs.insert(prepended_exprs.begin() + static_cast<ptrdiff_t>(struct_count),
                                   expr);
            body.erase(body.begin() + ptrdiff_i);
            struct_count++;

            continue;
        }

        if (encountered_real_body) {
            i++;
            continue;
        }

        // CLEANUP: cleanup this loop structure a bit

        if (auto* vdecl = ctx.get_and_dyn_cast<VarDecl>(expr)) {
            if (mst_to_st(vdecl->scope()) == ScopeType::Global) {
                prepended_exprs.emplace_back(expr);

                if (vdecl->initializer.is_null() || !encountered_real_body) {
                    body.erase(body.begin() + ptrdiff_i);
                    continue;
                }

                NodeId dre = ctx.emplace_node<DeclRefExpr>(vdecl->location, expr).first;
                NodeId init_assign =
                    ctx.emplace_node<Assignment>(vdecl->location, dre, vdecl->initializer).first;
                body[i] = init_assign;

                vdecl->initializer = NodeId::null_id();
            }

            i++;
            continue;
        }

        auto* assign = ctx.get_and_dyn_cast<Assignment>(expr);
        if (assign != nullptr && assign->is_implicit_decl()) {
            const auto* dre = ctx.get_and_dyn_cast<DeclRefExpr>(assign->target);
            assert(dre != nullptr);

            auto* vdecl = ctx.get_and_dyn_cast<VarDecl>(dre->decl);
            if (mst_to_st(vdecl->scope()) == ScopeType::Global) {
                body[i] = dre->decl;
                continue; // parse current line again as a global var decl, with initializer
            }

            if (!ctx.isa<MethodDecl>(dre->decl))
                encountered_real_body = true;

            i++;
            continue;
        }

        encountered_real_body = true;
        i++;
    }

    body.insert(body.begin(), prepended_exprs.begin(), prepended_exprs.end());

    if (explicit_main.is_null()) {
        // wrap rest of body in a main function
        size_t body_first_idx = prepended_exprs.size();

        std::vector<NodeId> main_body{};

        if (body_first_idx < body.size()) {
            main_body.reserve(body.size() - body_first_idx);
            main_body.insert(main_body.end(), body.begin() + static_cast<ptrdiff_t>(body_first_idx),
                             body.end());
        }

        body.resize(body_first_idx); // leave one for the main method's decl

        SrcLocationId main_loc = !main_body.empty()
                                     ? ctx.get_node(main_body.back())->location
                                     : (!body.empty() ? ctx.get_node(body.back())->location
                                                      : ctx.src_info_pool.get_location(1, 1));

        NodeId main_cmpd = ctx.emplace_node<CompoundExpr>(main_loc, std::move(main_body)).first;

        explicit_main =
            ctx.emplace_node<MethodDecl>(main_loc, sir_ctx.sym_pool.get_id("main"),
                                         ctx.jl_Nothing_t(), std::vector<NodeId>{}, main_cmpd)
                .first;
    }

    body.emplace_back(explicit_main);

    return visit(global_cmpd);
}

SIRNodeId JLLoweringVisitor::visit_VarDecl(VarDecl& var) {
    swap_lower_type(var.annot_type);

    SIRNodeId init =
        !var.initializer.is_null() ? visit_and_check(var.initializer) : SIRNodeId::null_id();

    return emplace_decl<sir::VarDecl>(&var, var.location, var.identifier, var.type, var.qualifiers,
                                      init);
}

SIRNodeId JLLoweringVisitor::visit_MethodDecl(MethodDecl& method) {
    if (in_method)
        return fail(
            std::format("local functions are currently not supported (found local function: '{}')",
                        sir_ctx.get_sym(method.identifier)));

    bool prev_in_method = in_method;
    in_method           = true;

    if (method.ret_type.is_null() ||
        !is_ret_type_allowed(sir_ctx.type_pool.get_td(method.ret_type))) {

        return fail(std::format("cannot lower function with return type '{}'",
                                type_to_string(method.ret_type, sir_ctx, sir_ctx)));
    }

    std::vector<SIRNodeId> params;
    params.reserve(method.param_decls.size());

    for (NodeId param : method.param_decls)
        params.push_back(visit_and_check(param));

    SIRNodeId body = visit_and_check(method.body);

    SIRNodeId scoped_body = emplace_node<sir::ScopedStmt>(sir_ctx.get_node(body)->location, body);

    swap_lower_type(method.ret_type);

    in_method = prev_in_method;

    return emplace_decl<sir::FunctionDecl>(&method, method.location, method.identifier,
                                           method.ret_type, std::move(params), method.qualifiers,
                                           scoped_body);
}

SIRNodeId JLLoweringVisitor::visit_FunctionDecl(FunctionDecl& fn) {
    std::vector<SIRNodeId> methods;
    methods.reserve(fn.methods.size());

    for (NodeId method : fn.methods)
        methods.push_back(visit_and_check(method));

    return emplace_decl<sir::CompoundStmt>(&fn, fn.location, std::move(methods));
}

SIRNodeId JLLoweringVisitor::visit_ParamDecl(ParamDecl& param) {
    if (!param.default_initializer.is_null())
        return fail("default initialized parameters are currently not supported");

    swap_lower_type(param.annot_type);

    return emplace_decl<sir::ParamDecl>(&param, param.location, param.identifier, param.type,
                                        param.qualifiers);
}

SIRNodeId JLLoweringVisitor::visit_OpaqueFunction(OpaqueFunction& opaq_fn) {
    return fail(std::format("cannot transpile unknown Julia function '{}'",
                            sir_ctx.get_sym(opaq_fn.fn_name())));
}

SIRNodeId JLLoweringVisitor::visit_BuiltinFunction(BuiltinFunction& builtin_fn) {
    return internal_error(
        std::format("lowering reached a BuiltinFunction leaf node for '{}'. BuiltinFns should only "
                    "appear as targets to function calls, and should not be separately visited.",
                    sir_ctx.get_sym(builtin_fn.fn_name())));
}

SIRNodeId JLLoweringVisitor::visit_StructDecl(StructDecl& struct_) {
    if (struct_.field_decls.empty())
        return fail(std::format("empty structs are not allowed (in definition of struct '{}')",
                                sir_ctx.get_sym(struct_.identifier)));

    if (!struct_.qualifiers.is_null())
        return fail(std::format(
            "qualifiers on struct definitions are not allowed (in definition of struct '{}')",
            sir_ctx.get_sym(struct_.identifier)));

    std::vector<SIRNodeId> fields{};
    fields.reserve(struct_.field_decls.size());

    for (NodeId field : struct_.field_decls)
        fields.push_back(visit_and_check(field));

    return emplace_decl<sir::StructDecl>(&struct_, struct_.location, struct_.identifier,
                                         std::move(fields));
}

SIRNodeId JLLoweringVisitor::visit_FieldDecl(FieldDecl& field) {
    return emplace_decl<sir::FieldDecl>(&field, field.location, field.identifier, field.type,
                                        field.qualifiers);
}

SIRNodeId JLLoweringVisitor::visit_CompoundExpr(CompoundExpr& cmpd) {
    std::vector<SIRNodeId> sir_nodes;
    sir_nodes.reserve(cmpd.body.size());

    for (NodeId expr : cmpd.body)
        sir_nodes.push_back(visit_and_check(expr));

    return emplace_node<sir::CompoundStmt>(cmpd.location, std::move(sir_nodes));
}

SIRNodeId JLLoweringVisitor::visit_BoolLiteral(BoolLiteral& bool_lit) {
    SIRNodeId id = emplace_node<sir::BoolLiteral>(bool_lit.location, bool_lit.value());
    auto* bl     = sir_ctx.get_and_dyn_cast<sir::BoolLiteral>(id);
    bl->set_type(sir_ctx.type_pool.bool_td());

    return id;
}

#define GEN_INT_LITERAL_VISITOR(type, width, is_signed)                                            \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                               \
    SIRNodeId JLLoweringVisitor::visit_##type(type& lit) {                                         \
        return emplace_node<sir::IntLiteral>(lit.location,                                         \
                                             sir_ctx.type_pool.int_td((width), (is_signed)),       \
                                             std::to_string(lit.value));                           \
    }

GEN_INT_LITERAL_VISITOR(Int32Literal, 32, true)
GEN_INT_LITERAL_VISITOR(Int64Literal, 64, true)
GEN_INT_LITERAL_VISITOR(UInt8Literal, 8, false)
GEN_INT_LITERAL_VISITOR(UInt16Literal, 16, false)
GEN_INT_LITERAL_VISITOR(UInt32Literal, 32, false)
GEN_INT_LITERAL_VISITOR(UInt64Literal, 64, false)

SIRNodeId JLLoweringVisitor::visit_UInt128Literal([[maybe_unused]] UInt128Literal& lit) {
    return internal_error("unsupported UInt128 literal node not caught by sema");
}

SIRNodeId JLLoweringVisitor::visit_Float32Literal(Float32Literal& lit) {
    return emplace_node<sir::FloatLiteral>(
        lit.location, sir_ctx.type_pool.float_td(32),
        std::format("{:.{}f}", lit.value, std::numeric_limits<float>::max_digits10));
}

SIRNodeId JLLoweringVisitor::visit_Float64Literal(Float64Literal& lit) {
    return emplace_node<sir::FloatLiteral>(
        lit.location, sir_ctx.type_pool.float_td(64),
        std::format("{:.{}f}", lit.value, std::numeric_limits<double>::max_digits10));
}

SIRNodeId JLLoweringVisitor::visit_ArrayLiteral(ArrayLiteral& arr_lit) {
    std::vector<SIRNodeId> lowered_members{};
    lowered_members.reserve(arr_lit.members.size());

    for (NodeId member : arr_lit.members) {
        SIRNodeId lowered_member = visit(member);
        if (lowered_member.is_null()) {
            if (_success)
                return fail("failed to lower member node in ArrayLiteral");

            return SIRNodeId::null_id();
        }

        lowered_members.emplace_back(lowered_member);
    }

    const auto& td = sir_ctx.type_pool.get_td(arr_lit.type);

    if (!td.is_array()) {
        return internal_error(std::format("non-array type inferred for array literal node by sema",
                                          type_to_string(arr_lit.type, sir_ctx, sir_ctx)));
    }

    ArrayTD arr_td    = td.as<ArrayTD>();
    const auto& el_td = sir_ctx.type_pool.get_td(arr_td.element_type_id);

    if (el_td.is_array())
        return fail("multidimensional arrays are currently not supported");

    if (!el_td.is_scalar())
        return fail("arrays with non-scalar element types are currently not supported");

    if (arr_td.length != lowered_members.size())
        return internal_error("inferred array length mismatch in type of array literal node");

    return emplace_node<sir::ArrayLiteral>(arr_lit.location, arr_lit.type,
                                           std::move(lowered_members));
}

SIRNodeId JLLoweringVisitor::visit_StringLiteral([[maybe_unused]] StringLiteral& lit) {
    return internal_error("unsupported String literal node not caught by sema");
}

SIRNodeId JLLoweringVisitor::visit_SymbolLiteral([[maybe_unused]] SymbolLiteral& lit) {
    return internal_error("symbol literal in AST should have been resolved by sema");
}

SIRNodeId JLLoweringVisitor::visit_NothingLiteral([[maybe_unused]] NothingLiteral& lit) {
    throw std::logic_error{"using nothing literal outside of a return stmt"};
}

SIRNodeId JLLoweringVisitor::visit_OpaqueNode([[maybe_unused]] OpaqueNode& opaq) {
    return internal_error("OpaqueNode node not caught by sema");
}

SIRNodeId JLLoweringVisitor::visit_GlobalRef([[maybe_unused]] GlobalRef& gref) {
    throw std::logic_error{"using unimplemented feature: global refs"};
}

SIRNodeId JLLoweringVisitor::visit_ImplicitCast(ImplicitCast& impl_cast) {
    return visit_and_check(impl_cast.target);
}

SIRNodeId JLLoweringVisitor::visit_ExplicitCast(ExplicitCast& expl_cast) {
    SIRNodeId inner = visit_and_check(expl_cast.target);
    return emplace_node<sir::ExplicitCast>(expl_cast.location, expl_cast.type, inner);
}

SIRNodeId JLLoweringVisitor::visit_DeclRefExpr(DeclRefExpr& dre) {
    assert(ctx.isa<Decl>(dre.decl));

    Decl* decl        = ctx.get_and_dyn_cast<Decl>(dre.decl);
    SIRNodeId decl_id = SIRNodeId::null_id();

    auto it = decl_map.find(decl);
    if (it != decl_map.end())
        decl_id = it->second;
    else
        decl_id = visit_and_check(dre.decl);

    assert(!decl_id.is_null());

    return emplace_node<sir::DeclRefExpr>(dre.location, decl_id, dre.type);
}

SIRNodeId JLLoweringVisitor::visit_IndexerExpr(IndexerExpr& idx_expr) {
    SIRNodeId lowered_target = visit(idx_expr.target);
    if (lowered_target.is_null())
        return SIRNodeId::null_id();

    auto visit_and_wrap_minus_one = [&](NodeId idx, Expr* idx_node = nullptr) {
        if (idx_node == nullptr)
            idx_node = ctx.get_node(idx);

        assert(idx_node != nullptr);
        assert(sir_ctx.type_pool.is_type_of<IntTD>(idx_node->type));

        SIRNodeId lowered_base = visit(idx);
        if (lowered_base.is_null())
            return SIRNodeId::null_id();

        auto lit_one = emplace_node<sir::IntLiteral>(idx_expr.location, idx_node->type, "1");

        auto wrapped_idx = emplace_node<sir::BinaryOp>(
            idx_expr.location, sir::BinaryOp::OpKind::sub, lowered_base, lit_one);
        return emplace_node<sir::IndexerExpr>(idx_expr.location, lowered_target, wrapped_idx);
    };

    const auto& target_td = sir_ctx.type_pool.get_td(ctx.get_node(idx_expr.target)->type);

    size_t idx_n = idx_expr.indexers.size();
    bool is_arr  = target_td.is_array();
    bool is_vec  = target_td.is_vector();

    if (is_arr || is_vec) {
        if (idx_n != 1)
            return fail("arrays and vectors only support single component indexers");

        NodeId idx     = idx_expr.indexers[0];
        Expr* idx_node = ctx.get_node(idx);
        TypeId idx_t   = idx_node->type;

        if (is_arr) {
            if (!sir_ctx.type_pool.is_type_of<IntTD>(idx_t))
                return fail("non-integer indexer on array is not allowed");

            return visit_and_wrap_minus_one(idx, idx_node);
        }

        if (is_vec) {
            if (sir_ctx.type_pool.is_type_of<IntTD>(idx_t))
                return visit_and_wrap_minus_one(idx, idx_node);

            // swizzle
            if (auto* idx_sym = dyn_cast<SymbolLiteral>(idx_node)) {
                std::string_view swizzle = sir_ctx.get_sym(idx_sym->value);
                size_t swizzle_n         = swizzle.length();

                if (swizzle_n == 0 || swizzle_n > 4)
                    return internal_error("invalid swizzle component count not caught by sema");

                std::array<uint8_t, 4> comps = {0, 0, 0, 0};
                for (size_t i = 0; i < swizzle_n; i++) {
                    comps[i] = parse_swizzle_component(swizzle[i]).first;

                    if (comps[i] == INVALID_SWIZZLE)
                        return internal_error("invalid swizzle component not caught by sema");
                }

                SIRNodeId lowered_swizzle = emplace_node<sir::SwizzleLiteral>(
                    idx_node->location, static_cast<uint8_t>(swizzle_n), comps[0], comps[1],
                    comps[2], comps[3]);

                return emplace_node<sir::IndexerExpr>(idx_expr.location, lowered_target,
                                                      lowered_swizzle);
            }

            return fail("non-integer, non-swizzle indexer on vector is not allowed");
        }
    }

    return fail(std::format("indexers on type '{}' are not allowed",
                            type_to_string(target_td, sir_ctx, sir_ctx)));
}

SIRNodeId JLLoweringVisitor::visit_FieldAccess(FieldAccess& acc) {
    return emplace_node<sir::FieldAccess>(acc.location, visit_and_check(acc.target),
                                          visit_and_check(acc.field_decl));
}

SIRNodeId JLLoweringVisitor::visit_DotChain(DotChain& dc) {
    if (!dc.is_resolved())
        return internal_error("dot chain expression not resolved by sema");

    return visit_and_check(dc.resolved_expr);
}

SIRNodeId JLLoweringVisitor::visit_Assignment(Assignment& assign) {
    if (assign.is_implicit_decl()) {
        auto* dre = ctx.get_and_dyn_cast<DeclRefExpr>(assign.target);

        assert(dre != nullptr && "assignment marked as an implicit declaration, but lhs is not a "
                                 "declaration reference expression");

        if (!ctx.isa<VarDecl>(dre->decl))
            throw std::logic_error{
                "implicitly declaring assignment initializes a non-variable-declaration node"};

        // should only ever return a VarDecl (or maybe a MethodDecl later on)
        SIRNodeId result = visit_and_check(dre->decl);
        assert(sir_ctx.isa<sir::VarDecl>(result));

        return result;
    }

    return emplace_node<sir::Assignment>(assign.location, visit_and_check(assign.target),
                                         visit_and_check(assign.value));
}

SIRNodeId JLLoweringVisitor::visit_FunctionCall(FunctionCall& fn_call) {
    using BinOpKind = sir::BinaryOp::OpKind;
    using UnOpKind  = sir::UnaryOp::OpKind;

    auto* target_fn_expr = ctx.get_node(fn_call.target_fn);

    auto* dre = dyn_cast<DeclRefExpr>(target_fn_expr);
    auto* dc  = dyn_cast<DotChain>(target_fn_expr);

    if (dre == nullptr && dc == nullptr)
        return internal_error(
            "non-decl-ref, non-dot-chain node in FunctionCall's target_fn not caught by sema");

    if (dre != nullptr) {
        if (ctx.isa<SymbolLiteral>(dre->decl))
            return internal_error("unresolved declaration reference expression found post-sema");
    }

    if (dc != nullptr) {
        std::string mod_path =
            mod_chain_to_path(dc->chain, ctx, sir_ctx.sym_pool, dc->chain.size() - 1);

        auto& mod_cache = ctx.jl_env.module_cache;

        auto maybe_mod = mod_cache.get_mod(mod_path);
        if (!maybe_mod.has_value())
            return internal_error("dot chain with invalid method path not caught by sema");

        const auto& mod = maybe_mod->get();

        bool valid_mod = mod.mod_ptr() == mod_cache.base_mod.mod_ptr() ||
                         mod.mod_ptr() == mod_cache.glm_mod.mod_ptr() ||
                         mod.mod_ptr() == mod_cache.main_mod.mod_ptr();
        if (!valid_mod)
            return fail("cannot lower call to module-resolved function not available from Main, "
                        "Base or JuliaGLM");
    }

    auto* decl_expr = ctx.get_node(dre != nullptr ? dre->decl : dc->resolved_expr);

    auto fn_identifier = SymbolId::null_id();
    bool is_ctor       = false;

    if (auto* decl = dyn_cast<FunctionDecl>(decl_expr)) {
        fn_identifier = decl->identifier;
    } else if (auto* builtin = dyn_cast<BuiltinFunction>(decl_expr)) {
        fn_identifier = builtin->fn_name();
    } else if (auto* s_decl = dyn_cast<StructDecl>(decl_expr)) {
        fn_identifier = s_decl->identifier;
        is_ctor       = true;
    } else if (auto* opaq = dyn_cast<OpaqueFunction>(decl_expr)) {
        fn_identifier = opaq->fn_name();
        is_ctor       = opaq->is_ctor();
    } else {
        return internal_error(
            "unexpected declaration kind referenced in target of a function call");
    }

    if (fn_identifier.is_null())
        return internal_error("couldn't determine identifier for the target of a function call");

    auto make_unop = [this, &fn_call](UnOpKind kind) -> SIRNodeId {
        return emplace_node<sir::UnaryOp>(fn_call.location, kind, visit_and_check(fn_call.args[0]));
    };

    auto make_binop = [this, &fn_call](BinOpKind kind) -> SIRNodeId {
        return emplace_node<sir::BinaryOp>(fn_call.location, kind, visit_and_check(fn_call.args[0]),
                                           visit_and_check(fn_call.args[1]));
    };

    if (!is_ctor && fn_call.args.size() == 1) {
        if (fn_identifier == sym_plus)
            return make_unop(UnOpKind::plus);
        if (fn_identifier == sym_minus)
            return make_unop(UnOpKind::minus);
        if (fn_identifier == sym_bang)
            return make_unop(UnOpKind::lneg);
        if (fn_identifier == sym_tilde)
            return make_unop(UnOpKind::bneg);
    } else if (!is_ctor && fn_call.args.size() == 2) {
        if (fn_identifier == sym_plus)
            return make_binop(BinOpKind::add);
        if (fn_identifier == sym_minus)
            return make_binop(BinOpKind::sub);
        if (fn_identifier == sym_asterisk)
            return make_binop(BinOpKind::mul);
        if (fn_identifier == sym_div)
            return make_binop(BinOpKind::div);
        if (fn_identifier == sym_caret)
            return make_binop(BinOpKind::pow);
        if (fn_identifier == sym_perc || fn_identifier == sym_rem)
            return make_binop(BinOpKind::mod);

        if (fn_identifier == sym_dbl_eq)
            return make_binop(BinOpKind::eq);
        if (fn_identifier == sym_neq)
            return make_binop(BinOpKind::neq);
        if (fn_identifier == sym_lt)
            return make_binop(BinOpKind::lt);
        if (fn_identifier == sym_leq)
            return make_binop(BinOpKind::leq);
        if (fn_identifier == sym_gt)
            return make_binop(BinOpKind::gt);
        if (fn_identifier == sym_geq)
            return make_binop(BinOpKind::geq);

        if (fn_identifier == sym_amper)
            return make_binop(BinOpKind::band);
        if (fn_identifier == sym_pipe)
            return make_binop(BinOpKind::bor);

        if (fn_identifier == sym_xor) {
            if (fn_call.type == sir_ctx.type_pool.bool_td())
                return make_binop(BinOpKind::lxor);

            assert(sir_ctx.type_pool.get_td(fn_call.type).is<IntTD>());
            return make_binop(BinOpKind::bxor);
        }
    }

    std::vector<SIRNodeId> args{};
    args.reserve(fn_call.args.size());

    for (NodeId arg : fn_call.args)
        args.push_back(visit_and_check(arg));

    if (is_ctor)
        return emplace_node<sir::ConstructorCall>(fn_call.location, fn_call.type, std::move(args));

    return emplace_node<sir::FunctionCall>(fn_call.location, fn_identifier, std::move(args));
}

SIRNodeId JLLoweringVisitor::visit_LogicalBinOp(LogicalBinOp& lbo) {
    using BinOpKind = sir::BinaryOp::OpKind;

    SIRNodeId lhs = visit_and_check(lbo.lhs);
    SIRNodeId rhs = visit_and_check(lbo.rhs);

    return lbo.is_land() ? emplace_node<sir::BinaryOp>(lbo.location, BinOpKind::land, lhs, rhs)
                         : emplace_node<sir::BinaryOp>(lbo.location, BinOpKind::lor, lhs, rhs);
}

SIRNodeId JLLoweringVisitor::visit_IfExpr(IfExpr& if_) {
    SIRNodeId lo_cond = visit_and_check(if_.condition);

    SIRNodeId lo_true =
        emplace_node<sir::ScopedStmt>(if_.location, visit_and_check(if_.true_branch));

    SIRNodeId lo_false = SIRNodeId::null_id();
    if (!if_.false_branch.is_null())
        lo_false = emplace_node<sir::ScopedStmt>(if_.location, visit_and_check(if_.false_branch));

    return emplace_node<sir::IfStmt>(if_.location, lo_cond, lo_true, lo_false);
}

SIRNodeId JLLoweringVisitor::visit_WhileExpr(WhileExpr& while_) {
    SIRNodeId lo_cond = visit_and_check(while_.condition);

    SIRNodeId lo_body =
        emplace_node<sir::ScopedStmt>(while_.location, visit_and_check(while_.body));

    return emplace_node<sir::WhileStmt>(while_.location, lo_cond, lo_body);
}

SIRNodeId JLLoweringVisitor::visit_ReturnStmt(ReturnStmt& return_stmt) {
    if (return_stmt.inner.is_null() || ctx.isa<NothingLiteral>(return_stmt.inner))
        return emplace_node<sir::ReturnStmt>(return_stmt.location);

    SIRNodeId lo_inner = visit_and_check(return_stmt.inner);

    return emplace_node<sir::ReturnStmt>(return_stmt.location, lo_inner);
}

SIRNodeId JLLoweringVisitor::visit_ContinueStmt(ContinueStmt& cont_stmt) {
    return emplace_node<sir::ContinueStmt>(cont_stmt.location);
}

SIRNodeId JLLoweringVisitor::visit_BreakStmt(BreakStmt& break_stmt) {
    return emplace_node<sir::BreakStmt>(break_stmt.location);
}

} // namespace stc::jl
