#include "backend/glsl/types.h"

#include "backend/glsl/code_gen.h"

namespace {

using BinOpKind = stc::sir::BinaryOp::OpKind;

inline char bin_op_kind_str(BinOpKind op) {
    switch (op) {
        case BinOpKind::add:
            return '+';

        case BinOpKind::sub:
            return '-';

        case BinOpKind::mul:
            return '*';

        case BinOpKind::div:
            return '/';

        case BinOpKind::pow:
        case BinOpKind::mod:
            return '?';

        default:
            throw std::logic_error{"Unhandled binary op kind in glsl code gen"};
    }
}

} // namespace

namespace stc::glsl {

std::string GLSLCodeGenVisitor::indent() const {
    return stc::indent(indent_level, STC_CG_INDENT);
}

// ================
//   Declarations
// ================

void GLSLCodeGenVisitor::visit_VarDecl(VarDecl& var_decl) {
    out << indent() << type_str(var_decl.type, ctx.type_pool, ctx.sym_pool) << ' '
        << ctx.get_sym(var_decl.identifier);

    if (!var_decl.initializer.is_null()) {
        out << " = ";
        visit(var_decl.initializer);
    }

    out << ';';
}

void GLSLCodeGenVisitor::visit_FunctionDecl(FunctionDecl& fn_decl) {
    assert(ctx.isa<CompoundStmt>(fn_decl.body) &&
           "non-compound-stmt function body not caught by sema");

    out << indent() << type_str(fn_decl.return_type, ctx.type_pool, ctx.sym_pool) << " "
        << ctx.get_sym(fn_decl.identifier) << '(';

    for (size_t i = 0; i < fn_decl.param_decls.size(); i++) {
        NodeId param_decl = fn_decl.param_decls[i];
        assert(ctx.isa<ParamDecl>(param_decl) &&
               "non-param-decl function parameter not caught by sema");

        visit(param_decl);

        if (i != fn_decl.param_decls.size() - 1)
            out << ", ";
    }

    out << ")";

    if (fn_decl.body != NodeId::null_id()) {
        out << '\n';
        visit(fn_decl.body);
        out << '\n';
    } else {
        out << ";\n";
    }
}

void GLSLCodeGenVisitor::visit_ParamDecl(ParamDecl& param_decl) {
    out << type_str(param_decl.param_type, ctx.type_pool, ctx.sym_pool) << ' '
        << ctx.get_sym(param_decl.identifier);
}

void GLSLCodeGenVisitor::visit_StructDecl(StructDecl& struct_decl) {
    out << "struct " << ctx.get_sym(struct_decl.identifier) << "\n{\n";

    for (NodeId field_decl : struct_decl.field_decls) {
        assert(ctx.isa<FieldDecl>(field_decl) &&
               "non-field-decl struct decl field not caught by sema");

        visit(field_decl);
        out << ';';
    }

    out << "};\n";
}

void GLSLCodeGenVisitor::visit_FieldDecl(FieldDecl& field_decl) {
    out << type_str(field_decl.field_type, ctx.type_pool, ctx.sym_pool) << ' '
        << ctx.get_sym(field_decl.identifier);
}

// ===============
//   Expressions
// ===============

void GLSLCodeGenVisitor::visit_BoolLiteral(BoolLiteral& bool_lit) {
    out << (bool_lit.value() ? "true" : "false");
}

void GLSLCodeGenVisitor::visit_IntLiteral(IntLiteral& int_lit) {
    out << int_lit.data;
}

void GLSLCodeGenVisitor::visit_FloatLiteral(FloatLiteral& float_lit) {
    out << float_lit.data;
}

void GLSLCodeGenVisitor::visit_VectorLiteral(VectorLiteral& vec_lit) {
    const TypeDescriptor& td = ctx.type_pool.get_td(vec_lit.type());
    assert(td.is_vector());

    out << type_str(td, ctx.type_pool, ctx.sym_pool) << '(';

    size_t n = vec_lit.components.size();
    for (size_t i = 0; i < n; i++) {
        visit(vec_lit.components[i]);

        if (i != n - 1)
            out << ", ";
    }

    out << ')';
}

void GLSLCodeGenVisitor::visit_MatrixLiteral(MatrixLiteral& mat_lit) {
    assert(ctx.type_pool.is_type_of<MatrixTD>(mat_lit.type()) &&
           "MatrixLiteral with non-matrix type in AST");

    out << type_str(mat_lit.type(), ctx.type_pool, ctx.sym_pool) << '(';

    for (size_t i = 0; i < mat_lit.data.size(); i++) {
        visit(mat_lit.data[i]);

        if (i != mat_lit.data.size() - 1)
            out << ", ";
    }

    out << ')';
}

// NOTE
// array literals and struct instantiations use explicit constructors instead of initializer lists
// as an effort to reduce the chances of producing misbehaving code
// if there is some kind of type-mismatch bug, try to fail at glsl compile-time

void GLSLCodeGenVisitor::visit_ArrayLiteral(ArrayLiteral& arr_lit) {
    assert(ctx.type_pool.is_type_of<ArrayTD>(arr_lit.type()) &&
           "ArrayLiteral with non-array type in AST");

    out << type_str(arr_lit.type(), ctx.type_pool, ctx.sym_pool) << '(';

    assert(arr_lit.elements.size() == ctx.type_pool.get_td(arr_lit.type()).as<ArrayTD>().length &&
           "array literal with wrong # of elements not caught by sema");

    for (size_t i = 0; i < arr_lit.elements.size(); i++) {
        visit(arr_lit.elements[i]);

        if (i != arr_lit.elements.size() - 1)
            out << ", ";
    }

    out << ')';
}

void GLSLCodeGenVisitor::visit_StructInstantiation(StructInstantiation& s_inst) {
    assert(ctx.type_pool.is_type_of<StructTD>(s_inst.type()) &&
           "StructInstantiation with non-struct type in AST");

    StructTD s_td = ctx.type_pool.get_td(s_inst.type()).as<StructTD>();
    assert(s_td.data != nullptr && "StructTD without data storage");

    out << ctx.get_sym(s_td.data->name) << '(';

    for (size_t i = 0; i < s_inst.field_values.size(); i++) {
        visit(s_inst.field_values[i]);

        if (i != s_inst.field_values.size() - 1)
            out << ", ";
    }

    out << ')';
}

void GLSLCodeGenVisitor::visit_ScopedExpr([[maybe_unused]] ScopedExpr& scoped_expr) {
    error("The GLSL backend does not support scoped expressions.");
    successful_gen = false;
}

void GLSLCodeGenVisitor::visit_BinaryOp(BinaryOp& bin_op) {
    out << '(';
    visit(bin_op.lhs);
    out << ") " << bin_op_kind_str(bin_op.op()) << " (";
    visit(bin_op.rhs);
    out << ')';
}

void GLSLCodeGenVisitor::visit_ExplicitCast(ExplicitCast& cast) {
    out << type_str(cast.type(), ctx.type_pool, ctx.sym_pool) << '(';
    visit(cast.inner);
    out << ')';
}

void GLSLCodeGenVisitor::visit_FunctionCall(FunctionCall& fn_call) {
    out << ctx.get_sym(fn_call.fn_name) << '(';

    for (size_t i = 0; i < fn_call.args.size(); i++) {
        visit(fn_call.args[i]);

        if (i != fn_call.args.size() - 1)
            out << ", ";
    }

    out << ')';
}

void GLSLCodeGenVisitor::visit_DeclRefExpr(DeclRefExpr& decl_ref) {
    NodeBase* decl_node = ctx.get_node(decl_ref.decl);
    assert(decl_node != nullptr && "nullptr pointing DeclRefExpr");

    Decl* decl = dyn_cast<Decl>(decl_node);
    assert(decl != nullptr && "non-decl pointing DeclRefExpr not caught by sema");

    out << ctx.get_sym(decl->identifier);
}

// ================
//   Declarations
// ================

void GLSLCodeGenVisitor::visit_ScopedStmt(ScopedStmt& scoped_stmt) {
    out << "{\n";

    indent_level++;
    visit(scoped_stmt.inner_stmt);
    indent_level--;

    out << "}\n";
}

void GLSLCodeGenVisitor::visit_CompoundStmt(CompoundStmt& cmpd_stmt) {
    for (NodeId node : cmpd_stmt.body) {
        visit(node);

        if (ctx.isa<Expr>(node))
            out << ";\n";
    }
}

void GLSLCodeGenVisitor::visit_IfStmt(IfStmt& if_stmt) {
    out << indent() << "if (";
    visit(if_stmt.condition_expr);
    out << ")\n";

    assert(ctx.isa<ScopedStmt>(if_stmt.true_block) &&
           (if_stmt.false_block.is_null() || ctx.isa<ScopedStmt>(if_stmt.false_block)) &&
           "invalid if child not caught by sema");

    visit(if_stmt.true_block);

    if (!if_stmt.false_block.is_null()) {
        out << '\n' << indent() << "else\n";
        visit(if_stmt.false_block);
    }
}

void GLSLCodeGenVisitor::visit_ReturnStmt(ReturnStmt& return_stmt) {
    out << indent() << "return ";
    visit(return_stmt.inner);
    out << ";\n";
}

void GLSLCodeGenVisitor::visit_ContinueStmt([[maybe_unused]] ContinueStmt& cont_stmt) {
    out << indent() << "continue;\n";
}

void GLSLCodeGenVisitor::visit_BreakStmt([[maybe_unused]] BreakStmt& break_stmt) {
    out << indent() << "break;\n";
}

} // namespace stc::glsl
