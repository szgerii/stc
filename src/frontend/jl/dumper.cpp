#include "frontend/jl/dumper.h"

namespace stc::jl {

std::string JLDumper::type_str(TypeId type_id) const {
    return to_string(type_id, ctx);
}

std::string JLDumper::indent() const {
    return stc::indent(indent_level, STC_DUMP_INDENT);
}

void JLDumper::inc_indent(size_t level) {
    indent_level += level;
}

void JLDumper::dec_indent(size_t level) {
    indent_level -= level;
}

void JLDumper::pre_visit_id(NodeId node_id) {
    out << indent() << '[' << std::format("{:p}", static_cast<void*>(ctx.get_node(node_id))) << "|"
        << node_id << "]\n";
}

void JLDumper::visit_BoolLiteral(BoolLiteral& bool_lit) {
    out << indent() << "BoolLiteral: " << (bool_lit.value() ? "true" : "false") << '\n';
}

#define GEN_LITERAL_VISITOR(type, suffix)                                                          \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                               \
    void JLDumper::visit_##type(type& lit) {                                                       \
        out << indent() << #type << ": " << lit.value << #suffix << '\n';                          \
    } // namespace stc::jl

#define GEN_UINT_LITERAL_VISITOR(type, width)                                                      \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                               \
    void JLDumper::visit_##type(type& lit) {                                                       \
        out << indent() << #type << ": 0x" << std::format("{:#0" #width "x}", lit.value) << '\n';  \
    }

GEN_LITERAL_VISITOR(Int32Literal, )
GEN_LITERAL_VISITOR(Int64Literal, )
GEN_LITERAL_VISITOR(Float32Literal, f0)
GEN_LITERAL_VISITOR(Float64Literal, )
GEN_UINT_LITERAL_VISITOR(UInt8Literal, 4)
GEN_UINT_LITERAL_VISITOR(UInt16Literal, 6)
GEN_UINT_LITERAL_VISITOR(UInt32Literal, 10)
GEN_UINT_LITERAL_VISITOR(UInt64Literal, 18)

void JLDumper::visit_UInt128Literal(UInt128Literal& lit) {
    out << indent() << "UInt128Literal: " << std::format("{:#034x}", lit.high)
        << std::format("{:034x}", lit.low) << '\n';
}

// CLEANUP: prettier printing for long/multiline literals with wrapping
void JLDumper::visit_StringLiteral(StringLiteral& lit) {
    out << indent() << "StringLiteral:\n";

    inc_indent();
    out << indent() << '"' << lit.value << "\"\n";
    dec_indent();
}

// CLEANUP: pretty printing target_fn isa Symbol case
void JLDumper::visit_FunctionCall(FunctionCall& fn_call) {
    out << indent() << "FunctionCall";

    out << indent() << dump_label("target_fn");
    inc_indent();
    visit(fn_call.target_fn);
    dec_indent();

    out << indent() << dump_label("args");
    inc_indent();
    for (NodeId arg : fn_call.args)
        visit(arg);
    dec_indent();
}

void JLDumper::visit_IfExpr(IfExpr& if_expr) {
    out << indent() << "IfExpr:\n";

    out << indent() << dump_label("condition");
    inc_indent();
    visit(if_expr.condition);
    dec_indent();

    out << indent() << dump_label("true branch");
    inc_indent();
    visit(if_expr.true_branch);
    dec_indent();

    out << indent() << dump_label("false branch");
    inc_indent();
    visit(if_expr.false_branch);
    dec_indent();
}

void JLDumper::visit_ReturnStmt(ReturnStmt& return_stmt) {
    out << indent() << "ReturnStmt:\n";

    inc_indent();
    visit(return_stmt.inner);
    dec_indent();
}

void JLDumper::visit_ContinueStmt([[maybe_unused]] ContinueStmt& cont_stmt) {
    out << indent() << "ContinueStmt\n";
}

void JLDumper::visit_BreakStmt([[maybe_unused]] BreakStmt& break_stmt) {
    out << indent() << "BreakStmt\n";
}
}; // namespace stc::jl