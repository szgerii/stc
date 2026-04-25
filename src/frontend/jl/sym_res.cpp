#include "frontend/jl/sym_res.h"
#include "frontend/jl/ast_utils.h"

#include <algorithm>
#include <ranges>

// TODO: allow global MethodDecl in soft local scopes (still shouldn't be allowed in hard scopes)

namespace stc::jl {

#define EMPTY_VISITOR_DEF(type)                                                                    \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                               \
    void SymbolRes::visit_##type([[maybe_unused]] type& node) {}

bool SymbolRes::finalize() {
    if (!_success)
        return false;

    assert(scopes.size() >= 2);

    JLScope& scope = scopes.back();

    bool in_hard_ctx = false;
    for (const auto& it : std::views::reverse(scopes)) {
        if (it.is_hard()) {
            in_hard_ctx = true;
            break;
        }
    }

    for (auto [sym, inferred] : scope_infer_table) {
        assert((!scope.bt_contains(sym) || isa<ParamDecl>(&inferred.first.get())) &&
               "non-empty scope provided to symbol resolution pass");

        auto [expr_ref, infer_src] = inferred;
        Expr* expr                 = &expr_ref.get();

        // CLEANUP: flatten out these if-s a bit

        BindingType inferred_binding = BindingType::Global;
        if (infer_src == ScopeInferSrc::Decl) {
            assert((isa<VarDecl, MethodDecl, ParamDecl>(expr)));

            if (auto* vdecl = dyn_cast<VarDecl>(expr)) {
                assert(vdecl->scope() != MaybeScopeType::Unspec);

                inferred_binding = mst_to_st(vdecl->scope()) == ScopeType::Global
                                       ? BindingType::Global
                                       : BindingType::Local;
            } else {
                assert((isa<MethodDecl, ParamDecl>(expr)));
                inferred_binding = BindingType::Local;
            }
        } else {
            assert(!(isa<Decl>(expr)));

            // reference:
            // https://docs.julialang.org/en/v1/manual/variables-and-scoping/

            auto it = std::find_if(scopes.rbegin(), scopes.rend(), [sym](JLScope& scope) -> bool {
                return scope.bt_contains(sym);
            });

            bool found    = it != scopes.rend();
            bool in_outer = found && (&(*it) != &scope);

            if (infer_src == ScopeInferSrc::Assign) {
                if (found && it->is_local()) {
                    inferred_binding = in_outer ? BindingType::Captured : BindingType::Local;
                } else if (in_hard_ctx) {
                    inferred_binding = BindingType::Local;
                } else {
                    // in soft scope chain and local is not yet defined
                    inferred_binding =
                        (found && in_interactive_ctx) ? BindingType::Global : BindingType::Local;
                }
            } else {
                assert(infer_src == ScopeInferSrc::Access);

                // ideally, we find the symbol in scopes, but if not, we might be able to capture it
                // directly from the Julia module itself during sema, so mark it as global
                // (locals should always be detected in valid code, so if a local is missed, that's
                // a sema/sym res bug)
                inferred_binding =
                    found ? (in_outer ? (it->type() == ScopeType::Global ? BindingType::Global
                                                                         : BindingType::Captured)
                                      : BindingType::Local)
                          : BindingType::Global;
            }
        }

        scope.bt_add_sym(sym, inferred_binding);
    }

    return true;
}

Decl* SymbolRes::get_prev_decl(SymbolId sym) const {
    auto it = scope_infer_table.find(sym);

    if (it != scope_infer_table.end() && it->second.second == ScopeInferSrc::Decl) {
        Decl* decl = dyn_cast<Decl>(&it->second.first.get());

        assert(decl != nullptr);
        return decl;
    }

    return nullptr;
}

void SymbolRes::visit_VarDecl(VarDecl& vdecl) {
    Decl* prev_decl = get_prev_decl(vdecl.identifier);

    // initial declaration of variable
    if (prev_decl == nullptr) {
        try_register(vdecl.identifier, vdecl, ScopeInferSrc::Decl);
        return;
    }

    // redeclaration of variable
    if (auto* prev_vdecl = dyn_cast<VarDecl>(prev_decl)) {
        if (prev_vdecl->scope() != vdecl.scope()) {
            error(ctx.src_info_pool, vdecl.location,
                  std::format("symbol '{}' declared both global and local",
                              ctx.get_sym(vdecl.identifier)));
            _success = false;
        }

        return;
    }

    // redeclaration as a variable (illegal)
    std::string_view prev_str = isa<MethodDecl>(prev_decl) ? "method" : "parameter";

    error(ctx.src_info_pool, vdecl.location,
          std::format("symbol '{}' redeclared as a variable (previously declared as a {})",
                      ctx.get_sym(vdecl.identifier), prev_str));
    _success = false;
}

void SymbolRes::visit_MethodDecl(MethodDecl& mdecl) {
    Decl* prev_decl = get_prev_decl(mdecl.identifier);

    if (prev_decl != nullptr && !isa<MethodDecl>(prev_decl)) {
        assert((isa<VarDecl, ParamDecl>(prev_decl)));

        std::string_view prev_str = isa<VarDecl>(prev_decl) ? "variable" : "parameter";
        error(ctx.src_info_pool, mdecl.location,
              std::format("symbol '{}' redeclared as a method (previously declared as a {})",
                          ctx.get_sym(mdecl.identifier), prev_str));

        _success = false;
        return;
    }

    try_register(mdecl.identifier, mdecl, ScopeInferSrc::Decl);

    assert(!scopes.empty());
    JLScope& scope = scopes.back();

    auto it = scope.local_fn_table.find(mdecl.identifier);
    if (it == scope.local_fn_table.end())
        scope.local_fn_table.try_emplace(mdecl.identifier, std::vector({&mdecl}),
                                         LFTEntry::State::Unresolved);
    else
        it->second.method_decls.emplace_back(&mdecl);
}

EMPTY_VISITOR_DEF(FunctionDecl)

void SymbolRes::visit_ParamDecl(ParamDecl& pdecl) {
    Decl* prev_decl = get_prev_decl(pdecl.identifier);

    if (prev_decl != nullptr) {
        std::string_view prev_str = ""sv;

        if (isa<VarDecl>(prev_decl))
            prev_str = "variable";
        else if (isa<MethodDecl>(prev_decl))
            prev_str = "method";
        else // repeated parameters are ignored, sema will handle them
            assert(isa<ParamDecl>(prev_decl));

        if (!prev_str.empty()) {
            error(ctx.src_info_pool, pdecl.location,
                  std::format("symbol '{}' redeclared as a param (previously declared as a {})",
                              ctx.get_sym(pdecl.identifier), prev_str));

            _success = false;
            return;
        }
    }

    // initializers aren't visited here, because they need to be handled in a more complicated way,
    // which has to be done by sema (see visit_MethodDecl of Sema)

    // if (!pdecl.default_initializer.is_null())
    //      visit(pdecl.default_initializer);

    try_register(pdecl.identifier, pdecl, ScopeInferSrc::Decl);
}

// opaque/builtin functions should not be visible during symbol resolution, they only get inserted
// during the actual sema pass
EMPTY_VISITOR_DEF(OpaqueFunction)
EMPTY_VISITOR_DEF(BuiltinFunction)

// struct declaration are only allowed at the top level (global scope)
// SymbolRes is designed to only be ran on local scopes
EMPTY_VISITOR_DEF(StructDecl)
EMPTY_VISITOR_DEF(FieldDecl)

void SymbolRes::visit_CompoundExpr(CompoundExpr& cmpd) {
    for (NodeId node : cmpd.body)
        visit(node);
}

EMPTY_VISITOR_DEF(BoolLiteral)
EMPTY_VISITOR_DEF(Int32Literal)
EMPTY_VISITOR_DEF(Int64Literal)
EMPTY_VISITOR_DEF(UInt8Literal)
EMPTY_VISITOR_DEF(UInt16Literal)
EMPTY_VISITOR_DEF(UInt32Literal)
EMPTY_VISITOR_DEF(UInt64Literal)
EMPTY_VISITOR_DEF(UInt128Literal)
EMPTY_VISITOR_DEF(Float32Literal)
EMPTY_VISITOR_DEF(Float64Literal)

void SymbolRes::visit_ArrayLiteral(ArrayLiteral& arr_lit) {
    for (NodeId member : arr_lit.members)
        visit(member);
}

void SymbolRes::visit_IndexerExpr(IndexerExpr& idx_expr) {
    visit(idx_expr.target);

    for (NodeId idx : idx_expr.indexers)
        visit(idx);
}

EMPTY_VISITOR_DEF(StringLiteral)

void SymbolRes::visit_SymbolLiteral(SymbolLiteral& sym_lit) {
    // symbols in defining positions aren't visited,
    // so their symbol literal visitor isn't invoked
    try_register(sym_lit.value, sym_lit, ScopeInferSrc::Access);
}

void SymbolRes::visit_FieldAccess(FieldAccess& field_access) {
    const auto* target = ctx.get_and_dyn_cast<Decl>(field_access.target);
    try_register(target->identifier, field_access, ScopeInferSrc::Access);
}

void SymbolRes::visit_DotChain(DotChain& dc) {
    if (dc.chain.size() != 2 || in_fn_call_target)
        return;

    const auto* sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(dc.chain[0]);

    if (sym_lit == nullptr) {
        const auto* dre = ctx.get_and_dyn_cast<DeclRefExpr>(dc.chain[1]);

        if (dre != nullptr)
            sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(dre->decl);
    }

    assert(sym_lit != nullptr);

    try_register(sym_lit->value, dc, ScopeInferSrc::Access);
}

EMPTY_VISITOR_DEF(NothingLiteral)
EMPTY_VISITOR_DEF(OpaqueNode)
EMPTY_VISITOR_DEF(GlobalRef)

void SymbolRes::visit_ImplicitCast(ImplicitCast& impl_cast) {
    visit(impl_cast.target);
}

void SymbolRes::visit_ExplicitCast(ExplicitCast& expl_cast) {
    visit(expl_cast.target);
}

void SymbolRes::visit_DeclRefExpr(DeclRefExpr& dre) {
    visit(dre.decl);
}

void SymbolRes::visit_Assignment(Assignment& assign) {
    visit(assign.value);

    Expr* lhs = ctx.get_node(assign.target);

    if (auto* dre = dyn_cast<DeclRefExpr>(lhs)) {
        assert(ctx.isa<SymbolLiteral>(dre->decl));

        if (auto* sym = ctx.get_and_dyn_cast<SymbolLiteral>(dre->decl)) {
            try_register(sym->value, assign, ScopeInferSrc::Assign);
            return;
        }
    }

    // fallback for refs, field accesses, etc.
    visit(lhs);
}

void SymbolRes::visit_FunctionCall(FunctionCall& fn_call) {
    bool prev_value   = in_fn_call_target;
    in_fn_call_target = true;

    visit(fn_call.target_fn);

    in_fn_call_target = prev_value;

    for (NodeId arg : fn_call.args)
        visit(arg);
}

void SymbolRes::visit_LogicalBinOp(LogicalBinOp& lbo) {
    visit(lbo.lhs);
    visit(lbo.rhs);
}

void SymbolRes::visit_IfExpr(IfExpr& if_) {
    visit(if_.condition);
    visit(if_.true_branch);

    if (!if_.false_branch.is_null())
        visit(if_.false_branch);
}

EMPTY_VISITOR_DEF(WhileExpr)

void SymbolRes::visit_ReturnStmt(ReturnStmt& ret) {
    if (!ret.inner.is_null())
        visit(ret.inner);
}

EMPTY_VISITOR_DEF(ContinueStmt)
EMPTY_VISITOR_DEF(BreakStmt)

#undef EMPTY_VISITOR_DEF

} // namespace stc::jl
