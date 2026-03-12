#include "frontend/jl/lowering.h"
#include "sir/ast.h"

namespace {

using SIRNodeId = stc::sir::NodeId;

} // namespace

namespace stc::jl {

SIRNodeId JLLoweringVisitor::visit_default_case() {
    internal_error("nullptr found in the Julia AST during lowering to SIR");
    this->success = false;

    return SIRNodeId::null_id();
}

SIRNodeId JLLoweringVisitor::fail(std::string_view msg) {
    internal_error(msg);
    success = false;
    return SIRNodeId::null_id();
}

SIRNodeId JLLoweringVisitor::visit_and_check(NodeId id) {
    SIRNodeId result = visit(id);

    if (result.is_null()) {
        return fail("null_id returned by a node in the Julia -> SIR lowering visitor.");
    }

    return result;
}

SIRNodeId JLLoweringVisitor::visit_ptr(Expr* node) {
    return this->dispatch_wrapper(node);
}

SIRNodeId JLLoweringVisitor::visit_BoolLiteral(BoolLiteral& bool_lit) {
    return emplace_node<sir::BoolLiteral>(bool_lit.location, bool_lit.value());
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
    return fail("unsupported UInt128 literal node not caught by sema");
}

SIRNodeId JLLoweringVisitor::visit_Float32Literal(Float32Literal& lit) {
    return emplace_node<sir::FloatLiteral>(lit.location, sir_ctx.type_pool.float_td(32),
                                           std::to_string(lit.value));
}

SIRNodeId JLLoweringVisitor::visit_Float64Literal(Float64Literal& lit) {
    return emplace_node<sir::FloatLiteral>(lit.location, sir_ctx.type_pool.float_td(64),
                                           std::to_string(lit.value));
}

SIRNodeId JLLoweringVisitor::visit_StringLiteral([[maybe_unused]] StringLiteral& lit) {
    return fail("unsupported String literal node not caught by sema");
}

SIRNodeId JLLoweringVisitor::visit_FunctionCall(FunctionCall& fn_call) {
    auto* str_lit = ctx.get_dyn<StringLiteral>(fn_call.target_fn);

    if (str_lit == nullptr)
        return fail("non-string literal node in FunctionCall's target_fn not caught by sema");

    std::vector<SIRNodeId> args{};
    args.reserve(fn_call.args.size());

    for (NodeId arg : fn_call.args)
        args.push_back(visit_and_check(arg));

    return emplace_node<sir::FunctionCall>(fn_call.location, std::move(str_lit->value),
                                           std::move(args));
}

SIRNodeId JLLoweringVisitor::visit_IfExpr(IfExpr& if_expr) {
    SIRNodeId lo_cond  = visit_and_check(if_expr.condition);
    SIRNodeId lo_true  = visit_and_check(if_expr.true_branch);
    SIRNodeId lo_false = visit_and_check(if_expr.false_branch);

    return emplace_node<sir::IfStmt>(if_expr.location, lo_cond, lo_true, lo_false);
}

SIRNodeId JLLoweringVisitor::visit_ReturnStmt(ReturnStmt& return_stmt) {
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
