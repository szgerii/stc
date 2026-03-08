#include <format>

#include "ir/ast_dumper.h"
#include "ir/type_descriptors.h"

namespace {

using OpKind = stc::ir::BinaryOp::OpKind;

std::string op_str(OpKind op) {
    switch (op) {
        case OpKind::add:
            return "+";

        case OpKind::sub:
            return "-";

        case OpKind::mul:
            return "*";

        case OpKind::div:
            return "/";

        case OpKind::pow:
            return "^";

        case OpKind::mod:
            return "%";
    }

    throw std::logic_error{"Unaccounted binary operator kind"};
}

inline std::string label(const std::string& label_str) {
    return std::format("({}):\n", label_str);
}

} // namespace

namespace stc::ir {

std::string ASTDumper::type_str(TypeId id) const {
    return to_string(id, ctx.type_pool);
}

std::string ASTDumper::indent() const {
    return stc::indent(indent_level);
}

void ASTDumper::inc_indent(size_t level) {
    indent_level += level;
}

void ASTDumper::dec_indent(size_t level) {
    indent_level -= level;
}

void ASTDumper::pre_visit(NodeId node_id) {
    out << indent() << '[' << std::format("{:p}", static_cast<void*>(ctx.get_node(node_id))) << "|"
        << node_id << "]\n";
}

void ASTDumper::visit_VarDecl(VarDecl& var_decl) {
    out << indent() << "VarDecl: '" << var_decl.identifier << "' <: " << type_str(var_decl.type)
        << '\n';

    if (var_decl.initializer.has_value()) {
        out << indent() << label("initializer");

        inc_indent();
        visit(*var_decl.initializer);
        dec_indent();
    }
}

void ASTDumper::visit_FunctionDecl(FunctionDecl& fn_decl) {
    out << indent() << "FunctionDecl: '" << fn_decl.identifier << "' -> "
        << type_str(fn_decl.return_type) << '\n';

    inc_indent();
    for (NodeId param : fn_decl.param_decls) {
        visit(param);
    }
    dec_indent();
}

void ASTDumper::visit_ParamDecl(ParamDecl& param_decl) {
    out << indent() << "ParamDecl: " << param_decl.identifier
        << " <: " << type_str(param_decl.param_type) << '\n';
}

void ASTDumper::visit_StructDecl(StructDecl& struct_decl) {
    out << indent() << "StructDecl: " << struct_decl.identifier << '\n';

    inc_indent();
    for (NodeId decl : struct_decl.field_decls)
        visit(decl);
    dec_indent();
}

void ASTDumper::visit_FieldDecl(FieldDecl& field_decl) {
    out << indent() << "FieldDecl: " << field_decl.identifier
        << " <: " << type_str(field_decl.field_type) << '\n';
}

void ASTDumper::visit_BoolLiteral(BoolLiteral& bool_lit) {
    out << indent() << "BoolLiteral: " << (bool_lit.value() ? "true" : "false") << '\n';
}

void ASTDumper::visit_IntLiteral(IntLiteral& int_lit) {
    out << indent() << "IntLiteral (" << type_str(int_lit.type()) << "): " << int_lit.data << '\n';
}

void ASTDumper::visit_FloatLiteral(FloatLiteral& float_lit) {
    out << indent() << "FloatLiteral (" << type_str(float_lit.type()) << "): " << float_lit.data
        << '\n';
}

void ASTDumper::visit_VectorLiteral(VectorLiteral& vec_lit) {
    out << indent() << "VectorLiteral (" << type_str(vec_lit.type()) << "):\n";

    inc_indent();
    for (NodeId component_stmt : vec_lit.components)
        visit(component_stmt);
    dec_indent();
}

void ASTDumper::visit_MatrixLiteral(MatrixLiteral& mat_lit) {
    out << indent() << "MatrixLiteral (" << type_str(mat_lit.type()) << "):\n";

    auto [rows, cols] = MatrixTD::get_dims(mat_lit.type(), ctx.type_pool);
    assert((size_t)(rows * cols) == mat_lit.data.size() &&
           "invalid number of elements in matrix literal");

    for (size_t col_idx = 0; col_idx < cols; col_idx++) {
        out << indent() << label(std::format("column #{}", col_idx + 1));

        inc_indent();
        for (size_t row_idx = 0; row_idx < rows; row_idx++) {
            visit(mat_lit.data[(col_idx * (size_t)rows) + row_idx]);
        }
        dec_indent();
    }
}

void ASTDumper::visit_ArrayLiteral(ArrayLiteral& arr_lit) {
    out << indent() << "ArrayLiteral (" << type_str(arr_lit.type()) << "):\n";

    inc_indent();
    for (NodeId elem : arr_lit.elements)
        visit(elem);
    dec_indent();
}

void ASTDumper::visit_StructInstantiationLiteral(StructInstantiationLiteral& si_lit) {
    out << indent() << "StructInstantiationLiteral (" << type_str(si_lit.type()) << "):\n";

    size_t f_idx = 1;
    for (auto& [f_name, f_value] : si_lit.field_values) {
        out << indent() << label(std::format("field #{} => '{}'", f_idx, f_name));

        inc_indent();
        visit(f_value);
        dec_indent();

        f_idx++;
    }
}

void ASTDumper::visit_BinaryOp(BinaryOp& bin_op) {
    out << indent() << "BinaryOp (" << op_str(bin_op.op()) << "):\n";

    out << indent() << label("lhs");
    inc_indent();
    visit(bin_op.lhs);
    dec_indent();

    out << indent() << label("rhs");
    inc_indent();
    visit(bin_op.rhs);
    dec_indent();
}

void ASTDumper::visit_ExplicitCast(ExplicitCast& expl_cast) {
    out << indent() << "ExplicitCast to '" << type_str(expl_cast.type()) << "':\n";

    inc_indent();
    visit(expl_cast.base);
    dec_indent();
}

void ASTDumper::visit_DeclRefExpr(DeclRefExpr& decl_ref) {
    NodeBase* node = ctx.get_node(decl_ref.decl);

    Decl* decl = dyn_cast<Decl>(node);
    assert(decl != nullptr && "decl ref expr points to nullptr, or a non-decl node");

    out << indent() << "DeclRefExpr to '" << decl->identifier << "'\n";
}

void ASTDumper::visit_CompoundStmt(CompoundStmt& cmpd_stmt) {
    out << indent() << "CompoundStmt:";

    if (cmpd_stmt.body.empty()) {
        out << " -\n";
    } else {
        out << '\n';

        inc_indent();
        for (NodeId stmt : cmpd_stmt.body)
            visit(stmt);
        dec_indent();
    }
}

void ASTDumper::visit_IfStmt(IfStmt& if_stmt) {
    out << indent() << "IfStmt:\n";
    out << indent() << label("condition");
    inc_indent();
    visit(if_stmt.condition_expr);
    dec_indent();

    out << indent() << label("true branch");
    inc_indent();
    visit(if_stmt.true_block);
    dec_indent();

    out << indent() << label("false branch");
    inc_indent();
    visit(if_stmt.false_block);
    dec_indent();
}

void ASTDumper::visit_ReturnStmt(ReturnStmt& return_stmt) {
    out << indent() << "ReturnStmt:\n";

    inc_indent();
    visit(return_stmt.ret_value_expr);
    dec_indent();
}

} // namespace stc::ir