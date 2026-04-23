#include "backend/glsl/code_gen.h"
#include "backend/glsl/type_utils.h"
#include "common/config.h"
#include "types/qualifier_pool.h"

#include <algorithm>

namespace {

using BinOpKind = stc::sir::BinaryOp::OpKind;
using UnOpKind  = stc::sir::UnaryOp::OpKind;

std::string_view un_op_kind_str(UnOpKind op) {
    // clang-format off
    switch (op) {
        case UnOpKind::plus:  return "+";
        case UnOpKind::minus: return "-";
        case UnOpKind::lneg:  return "!";
        case UnOpKind::bneg:  return "~";

        default:
            throw std::logic_error{"Unhandled unary op kind in glsl code gen"};
    }
    // clang-format on
}

std::string_view bin_op_kind_str(BinOpKind op) {
    // clang-format off
    switch (op) {
        case BinOpKind::add:  return "+";
        case BinOpKind::sub:  return "-";
        case BinOpKind::mul:  return "*";
        case BinOpKind::div:  return "/";
        case BinOpKind::mod:  return "%";
        case BinOpKind::eq:   return "==";
        case BinOpKind::neq:  return "!=";
        case BinOpKind::lt:   return "<";
        case BinOpKind::leq:  return "<=";
        case BinOpKind::gt:   return ">";
        case BinOpKind::geq:  return ">";
        case BinOpKind::land: return "&&";
        case BinOpKind::lor:  return "||";
        case BinOpKind::lxor: return "^^";
        case BinOpKind::band: return "&";
        case BinOpKind::bor:  return "|";
        case BinOpKind::bxor: return "^";

        default:
            throw std::logic_error{"Unhandled binary op kind in glsl code gen"};
    }
    // clang-format on
}

void print_quals(const stc::types::DeclQualifiers& decl_quals, std::ostream& out) {
    using namespace stc::types;

    if (decl_quals.quals.empty())
        return;

    std::vector<QualKind> sorted_quals = decl_quals.quals;

    std::ranges::sort(sorted_quals, std::less<>{}, stc::glsl::qual_rank);

    bool in_layout = false;
    for (QualKind qk : sorted_quals) {
        if (!is_layout_qual(qk)) {
            if (in_layout) {
                out << ") ";
                in_layout = false;
            }

            out << stc::glsl::qual_kind_to_str(qk) << ' ';
            continue;
        }

        if (!in_layout)
            out << "layout(";
        else
            out << ", ";

        out << stc::glsl::qual_kind_to_str(qk);

        if (!is_valueless_layout_qual(qk))
            out << " = " << decl_quals.layout_qual_payloads.get_qual_value(qk);

        in_layout = true;
    }

    if (in_layout)
        out << ") ";
}

} // namespace

namespace stc::glsl {

std::string GLSLCodeGenVisitor::indent() const {
    return stc::indent(indent_level, ctx.config.code_gen_indent, ctx.config.use_tabs);
}

// ================
//   Declarations
// ================

void GLSLCodeGenVisitor::visit_VarDecl(VarDecl& var_decl) {
    print_quals(ctx.get_quals(var_decl.qualifiers()), out);

    out << type_str(var_decl.type, ctx.type_pool, ctx.sym_pool) << ' '
        << ctx.get_sym(var_decl.identifier);

    if (!var_decl.initializer.is_null()) {
        out << " = ";
        visit(var_decl.initializer);
    }

    out << ";\n";
}

void GLSLCodeGenVisitor::visit_FunctionDecl(FunctionDecl& fn_decl) {
    assert(ctx.isa<ScopedStmt>(fn_decl.body) && "non-scoped-stmt function body not caught by sema");

    print_quals(ctx.get_quals(fn_decl.qualifiers()), out);

    out << type_str(fn_decl.return_type, ctx.type_pool, ctx.sym_pool) << " "
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
    print_quals(ctx.get_quals(param_decl.qualifiers()), out);

    out << type_str(param_decl.param_type, ctx.type_pool, ctx.sym_pool) << ' '
        << ctx.get_sym(param_decl.identifier);
}

void GLSLCodeGenVisitor::visit_StructDecl(StructDecl& struct_decl) {
    out << "struct " << ctx.get_sym(struct_decl.identifier) << "\n{\n";

    indent_level++;
    for (NodeId field_decl : struct_decl.field_decls) {
        assert(ctx.isa<FieldDecl>(field_decl) &&
               "non-field-decl struct decl field not caught by sema");

        visit(field_decl);
        out << ';';
    }
    indent_level--;

    out << "\n};\n\n";
}

void GLSLCodeGenVisitor::visit_FieldDecl(FieldDecl& field_decl) {
    print_quals(ctx.get_quals(field_decl.qualifiers()), out);

    out << indent() << type_str(field_decl.field_type, ctx.type_pool, ctx.sym_pool) << ' '
        << ctx.get_sym(field_decl.identifier);
}

// ===============
//   Expressions
// ===============

void GLSLCodeGenVisitor::visit_BoolLiteral(BoolLiteral& bool_lit) {
    out << (bool_lit.value() ? "true" : "false");
}

void GLSLCodeGenVisitor::visit_IntLiteral(IntLiteral& int_lit) {
    assert(!int_lit.type().is_null());
    const auto& lit_td = ctx.type_pool.get_td(int_lit.type());
    assert(lit_td.is<IntTD>());

    out << int_lit.value;

    if (!lit_td.as<IntTD>().is_signed)
        out << 'u';
}

void GLSLCodeGenVisitor::visit_FloatLiteral(FloatLiteral& float_lit) {
    assert(!float_lit.type().is_null());
    const auto& lit_td = ctx.type_pool.get_td(float_lit.type());
    assert(lit_td.is<FloatTD>());

    FloatTD float_td = lit_td.as<FloatTD>();

    if (float_td.enc != FloatTD::Encoding::ieee754) {
        error("The GLSL backend does not support non-IEEE754 floating point types");
        return;
    }

    if (float_td.width != 32 && float_td.width != 64) {
        error(std::format("The GLSL backend does not support floating point types with width {} "
                          "(allowed: 32 or 64)",
                          float_td.width));
        return;
    }

    out << float_lit.value;

    if (float_td.width == 32)
        out << 'f';
    else
        out << "lf";
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

void GLSLCodeGenVisitor::visit_SwizzleLiteral(SwizzleLiteral& swizzle_lit) {
    static constexpr char comp_map[4] = {'x', 'y', 'z', 'w'};

    assert(0 < swizzle_lit.count() && swizzle_lit.count() <= 4 &&
           "invalid swizzle literal not caught before codegen");

    const uint8_t count = swizzle_lit.count();

    if (count >= 1)
        out << comp_map[swizzle_lit.comp1() & 0x03];
    if (count >= 2)
        out << comp_map[swizzle_lit.comp2() & 0x03];
    if (count >= 3)
        out << comp_map[swizzle_lit.comp3() & 0x03];
    if (count >= 4)
        out << comp_map[swizzle_lit.comp4() & 0x03];
}

void GLSLCodeGenVisitor::visit_FieldAccess(FieldAccess& acc) {
    out << '(';
    visit(acc.target);
    out << ").";

    const auto* fdecl = ctx.get_and_dyn_cast<FieldDecl>(acc.field_decl);
    if (fdecl == nullptr)
        return internal_error("invalid field accessor not caught by sema");

    out << ctx.get_sym(fdecl->identifier);
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

void GLSLCodeGenVisitor::visit_UnaryOp(UnaryOp& un_op) {
    out << un_op_kind_str(un_op.op()) << '(';
    visit(un_op.target);
    out << ')';
}

void GLSLCodeGenVisitor::visit_BinaryOp(BinaryOp& bin_op) {
    if (bin_op.op() == BinOpKind::pow) {
        out << "pow(";
        visit(bin_op.lhs);
        out << ", ";
        visit(bin_op.rhs);
        out << ")";

        return;
    }

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

void GLSLCodeGenVisitor::visit_IndexerExpr(IndexerExpr& arr_mem) {
    visit(arr_mem.target_arr);

    bool is_swizzle = isa<SwizzleLiteral>(ctx.get_node(arr_mem.indexer));

    if (is_swizzle)
        out << '.';
    else
        out << '[';

    visit(arr_mem.indexer);

    if (!is_swizzle)
        out << ']';
}

void GLSLCodeGenVisitor::visit_Assignment(Assignment& assign) {
    visit(assign.target);
    out << " = ";
    visit(assign.value);
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

void GLSLCodeGenVisitor::visit_ConstructorCall(ConstructorCall& ctor_call) {
    out << type_str(ctor_call.type(), ctx, ctx) << '(';

    for (size_t i = 0; i < ctor_call.args.size(); i++) {
        visit(ctor_call.args[i]);

        if (i != ctor_call.args.size() - 1)
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
    out << indent() << "{\n";

    indent_level++;
    visit(scoped_stmt.inner_stmt);
    indent_level--;

    out << indent() << "}\n";
}

void GLSLCodeGenVisitor::visit_CompoundStmt(CompoundStmt& cmpd_stmt) {
    for (NodeId node : cmpd_stmt.body) {
        out << indent();
        visit(node);

        if (ctx.isa<Expr>(node))
            out << ";\n";
    }
}

void GLSLCodeGenVisitor::visit_IfStmt(IfStmt& if_stmt) {
    out << "if (";
    visit(if_stmt.condition);
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

void GLSLCodeGenVisitor::visit_WhileStmt(WhileStmt& while_stmt) {
    out << "while (";
    visit(while_stmt.condition);
    out << ")\n";

    assert(ctx.isa<ScopedStmt>(while_stmt.body) && "non-scoped while body not caught by sema");

    visit(while_stmt.body);
}

void GLSLCodeGenVisitor::visit_ReturnStmt(ReturnStmt& return_stmt) {
    out << "return ";
    visit(return_stmt.inner);
    out << ";\n";
}

void GLSLCodeGenVisitor::visit_ContinueStmt([[maybe_unused]] ContinueStmt& cont_stmt) {
    out << "continue;\n";
}

void GLSLCodeGenVisitor::visit_BreakStmt([[maybe_unused]] BreakStmt& break_stmt) {
    out << "break;\n";
}

} // namespace stc::glsl
