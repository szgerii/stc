#include <format>

#include "sir/dumper.h"
#include "types/type_descriptors.h"
#include "types/type_to_string.h"

namespace {

using UnOpKind  = stc::sir::UnaryOp::OpKind;
using BinOpKind = stc::sir::BinaryOp::OpKind;

std::string op_str(UnOpKind op) {
    // clang-format off
    switch (op) {
        case UnOpKind::plus:  return "plus";
        case UnOpKind::minus: return "minus";
        case UnOpKind::lneg:  return "logical negation";
        case UnOpKind::bneg:  return "bitwise negation";
    }
    // clang-format on

    throw std::logic_error{"unaccounted unary operator kind"};
}

std::string op_str(BinOpKind op) {
#define STC_OP_STR_CASE(tok)                                                                       \
    case BinOpKind::tok:                                                                           \
        return #tok;

    switch (op) {
        STC_OP_STR_CASE(add)
        STC_OP_STR_CASE(sub)
        STC_OP_STR_CASE(mul)
        STC_OP_STR_CASE(div)
        STC_OP_STR_CASE(pow)
        STC_OP_STR_CASE(mod)
        STC_OP_STR_CASE(eq)
        STC_OP_STR_CASE(neq)
        STC_OP_STR_CASE(lt)
        STC_OP_STR_CASE(leq)
        STC_OP_STR_CASE(gt)
        STC_OP_STR_CASE(geq)
        STC_OP_STR_CASE(land)
        STC_OP_STR_CASE(lor)
        STC_OP_STR_CASE(lxor)
        STC_OP_STR_CASE(band)
        STC_OP_STR_CASE(bor)
        STC_OP_STR_CASE(bxor)
    }

    throw std::logic_error{"Unaccounted binary operator kind"};

#undef STC_OP_STR_CASE
}

} // namespace

namespace stc::sir {

std::string SIRDumper::type_str(TypeId id) const {
    return type_to_string(id, ctx.type_pool, ctx.sym_pool);
}

std::string SIRDumper::indent() const {
    return stc::indent(indent_level, ctx.config.dump_indent, ctx.config.use_tabs);
}

void SIRDumper::inc_indent() {
    indent_level += ctx.config.dump_indent;
}

void SIRDumper::dec_indent() {
    indent_level -= ctx.config.dump_indent;
}

bool SIRDumper::pre_visit_id(NodeId node_id) {
    NodeBase* node = ctx.get_node(node_id);

    out << indent() << '[' << std::format("{:p}", static_cast<void*>(ctx.get_node(node_id))) << "|"
        << node_id.id_value() << '|'
        << (node != nullptr ? std::to_string(static_cast<uint8_t>(node->kind())) : "?") << "]\n";

    Expr* expr = ctx.get_and_dyn_cast<Expr>(node_id);
    if (expr != nullptr)
        out << indent() << "<" << type_str(expr->type()) << '|' << std::to_string(expr->type())
            << ">\n";

    return true;
}

void SIRDumper::visit_VarDecl(VarDecl& var_decl) {
    out << indent() << "VarDecl: '" << ctx.get_sym(var_decl.identifier) << "' ("
        << type_str(var_decl.type) << ")\n";

    if (!var_decl.initializer.is_null()) {
        out << indent() << dump_label("initializer");

        inc_indent();
        visit(var_decl.initializer);
        dec_indent();
    }
}

void SIRDumper::visit_FunctionDecl(FunctionDecl& fn_decl) {
    out << indent() << "FunctionDecl: '" << ctx.get_sym(fn_decl.identifier) << "' -> "
        << type_str(fn_decl.return_type) << '\n';

    out << indent() << dump_label("args");
    inc_indent();
    for (NodeId param : fn_decl.param_decls) {
        visit(param);
    }
    dec_indent();

    out << indent() << dump_label("body");
    inc_indent();
    visit(fn_decl.body);
    dec_indent();
}

void SIRDumper::visit_ParamDecl(ParamDecl& param_decl) {
    out << indent() << "ParamDecl: " << ctx.get_sym(param_decl.identifier) << " ("
        << type_str(param_decl.param_type) << ")\n";
}

void SIRDumper::visit_StructDecl(StructDecl& struct_decl) {
    out << indent() << "StructDecl: " << ctx.get_sym(struct_decl.identifier) << '\n';

    inc_indent();
    for (NodeId decl : struct_decl.field_decls)
        visit(decl);
    dec_indent();
}

void SIRDumper::visit_FieldDecl(FieldDecl& field_decl) {
    out << indent() << "FieldDecl: " << ctx.get_sym(field_decl.identifier) << " ("
        << type_str(field_decl.field_type) << ")\n";
}

void SIRDumper::visit_BoolLiteral(BoolLiteral& bool_lit) {
    out << indent() << "BoolLiteral: " << (bool_lit.value() ? "true" : "false") << '\n';
}

void SIRDumper::visit_IntLiteral(IntLiteral& int_lit) {
    out << indent() << "IntLiteral (" << type_str(int_lit.type()) << "): " << int_lit.value << '\n';
}

void SIRDumper::visit_FloatLiteral(FloatLiteral& float_lit) {
    out << indent() << "FloatLiteral (" << type_str(float_lit.type()) << "): " << float_lit.value
        << '\n';
}

void SIRDumper::visit_VectorLiteral(VectorLiteral& vec_lit) {
    out << indent() << "VectorLiteral (" << type_str(vec_lit.type()) << "):\n";

    inc_indent();
    for (NodeId component_stmt : vec_lit.components)
        visit(component_stmt);
    dec_indent();
}

void SIRDumper::visit_MatrixLiteral(MatrixLiteral& mat_lit) {
    out << indent() << "MatrixLiteral (" << type_str(mat_lit.type()) << "):\n";

    auto [rows, cols, comp_type] = MatrixTD::get_info(mat_lit.type(), ctx.type_pool);
    assert((size_t)(rows * cols) == mat_lit.data.size() &&
           "invalid number of elements in matrix literal");

    for (size_t col_idx = 0; col_idx < cols; col_idx++) {
        out << indent() << dump_label(std::format("column #{}", col_idx + 1));

        inc_indent();
        for (size_t row_idx = 0; row_idx < rows; row_idx++) {
            visit(mat_lit.data[(col_idx * (size_t)rows) + row_idx]);
        }
        dec_indent();
    }
}

void SIRDumper::visit_ArrayLiteral(ArrayLiteral& arr_lit) {
    out << indent() << "ArrayLiteral (" << type_str(arr_lit.type()) << "):\n";

    inc_indent();
    for (NodeId elem : arr_lit.elements)
        visit(elem);
    dec_indent();
}

void SIRDumper::visit_SwizzleLiteral(SwizzleLiteral& swizzle_lit) {
    out << indent()
        << std::format("SwizzleLiteral: [{}, {}, {}, {}] (count: {})\n", swizzle_lit.comp1(),
                       swizzle_lit.comp2(), swizzle_lit.comp3(), swizzle_lit.comp4(),
                       swizzle_lit.count());
}

void SIRDumper::visit_StructInstantiation(StructInstantiation& s_inst) {
    out << indent() << "StructInstantiation (" << type_str(s_inst.type()) << "):\n";

    assert(ctx.type_pool.is_type_of<StructTD>(s_inst.type()));
    const StructData* s_data = ctx.type_pool.get_td(s_inst.type()).as<StructTD>().data;
    assert(s_data != nullptr);

    size_t f_idx = 0;
    for (NodeId f_value : s_inst.field_values) {
        out << indent()
            << dump_label(std::format("field #{} <=> '{}'", f_idx + 1,
                                      ctx.get_sym(s_data->fields[f_idx].name)));

        inc_indent();
        visit(f_value);
        dec_indent();

        f_idx++;
    }
}

void SIRDumper::visit_FieldAccess(FieldAccess& acc) {
    out << indent() << "FieldAccess:\n";

    inc_indent();
    out << indent() << dump_label("target");
    visit(acc.target);
    dec_indent();

    inc_indent();
    out << indent() << dump_label("field decl");
    visit(acc.field_decl);
    dec_indent();
}

void SIRDumper::visit_ScopedExpr(ScopedExpr& scoped_expr) {
    out << indent() << "ScopedExpr:\n";

    inc_indent();
    visit(scoped_expr.inner);
    dec_indent();
}

void SIRDumper::visit_UnaryOp(UnaryOp& un_op) {
    out << indent() << "UnaryOp (" << op_str(un_op.op()) << "):\n";

    out << indent() << dump_label("inner");
    inc_indent();
    visit(un_op.target);
    dec_indent();
}

void SIRDumper::visit_BinaryOp(BinaryOp& bin_op) {
    out << indent() << "BinaryOp (" << op_str(bin_op.op()) << "):\n";

    out << indent() << dump_label("lhs");
    inc_indent();
    visit(bin_op.lhs);
    dec_indent();

    out << indent() << dump_label("rhs");
    inc_indent();
    visit(bin_op.rhs);
    dec_indent();
}

void SIRDumper::visit_ExplicitCast(ExplicitCast& expl_cast) {
    out << indent() << "ExplicitCast to '" << type_str(expl_cast.type()) << "':\n";

    inc_indent();
    visit(expl_cast.inner);
    dec_indent();
}

void SIRDumper::visit_IndexerExpr(IndexerExpr& arr_mem) {
    out << indent() << "IndexerExpr: \n";

    out << indent() << dump_label("target array");
    inc_indent();
    visit(arr_mem.target_arr);
    dec_indent();

    out << indent() << dump_label("indexer");
    inc_indent();
    visit(arr_mem.indexer);
    dec_indent();
}

void SIRDumper::visit_Assignment(Assignment& assign) {
    out << indent() << "Assignment: \n";

    out << indent() << dump_label("target");
    inc_indent();
    visit(assign.target);
    dec_indent();

    out << indent() << dump_label("value");
    inc_indent();
    visit(assign.value);
    dec_indent();
}

void SIRDumper::visit_FunctionCall(FunctionCall& fn_call) {
    out << indent() << "FunctionCall: '" << ctx.get_sym(fn_call.fn_name) << "'\n";

    inc_indent();
    for (NodeId arg : fn_call.args)
        visit(arg);
    dec_indent();
}

void SIRDumper::visit_ConstructorCall(ConstructorCall& ctor_call) {
    out << indent() << "ConstructorCall: '" << type_str(ctor_call.type()) << "'\n";

    inc_indent();
    for (NodeId arg : ctor_call.args)
        visit(arg);
    dec_indent();
}

void SIRDumper::visit_DeclRefExpr(DeclRefExpr& decl_ref) {
    NodeBase* node = ctx.get_node(decl_ref.decl);

    Decl* decl = dyn_cast<Decl>(node);
    assert(decl != nullptr && "decl ref expr points to nullptr, or a non-decl node");

    out << indent() << "DeclRefExpr: '" << ctx.get_sym(decl->identifier) << "'\n";
}

void SIRDumper::visit_ScopedStmt(ScopedStmt& scoped_stmt) {
    out << indent() << "ScopedStmt:\n";
    inc_indent();
    visit(scoped_stmt.inner_stmt);
    dec_indent();
}

void SIRDumper::visit_CompoundStmt(CompoundStmt& cmpd_stmt) {
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

void SIRDumper::visit_IfStmt(IfStmt& if_stmt) {
    out << indent() << "IfStmt:\n";
    out << indent() << dump_label("condition");
    inc_indent();
    visit(if_stmt.condition);
    dec_indent();

    out << indent() << dump_label("true branch");
    inc_indent();
    visit(if_stmt.true_block);
    dec_indent();

    out << indent() << dump_label("false branch");
    inc_indent();
    visit(if_stmt.false_block);
    dec_indent();
}

void SIRDumper::visit_WhileStmt(WhileStmt& while_stmt) {
    out << indent() << "WhileStmt:\n";

    out << indent() << dump_label("condition");
    inc_indent();
    visit(while_stmt.condition);
    dec_indent();

    out << indent() << dump_label("body");
    inc_indent();
    visit(while_stmt.body);
    dec_indent();
}

void SIRDumper::visit_ReturnStmt(ReturnStmt& return_stmt) {
    out << indent() << "ReturnStmt:\n";

    inc_indent();
    visit(return_stmt.inner);
    dec_indent();
}

void SIRDumper::visit_ContinueStmt([[maybe_unused]] ContinueStmt& continue_stmt) {
    out << indent() << "ContinueStmt\n";
}

void SIRDumper::visit_BreakStmt([[maybe_unused]] BreakStmt& break_stmt) {
    out << indent() << "BreakStmt\n";
}

} // namespace stc::sir