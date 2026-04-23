#include "sir/sema.h"

namespace stc::sir {

void SIRSemaVisitor::error(const NodeBase& node, std::string_view msg, std::ostream& out) {
    _success = false;

    out << "<SIR sema>\n";

    auto [loc, file] = ctx.src_info_pool.get_loc_and_file(node.location);
    stc::error(file, loc, msg, out);
}

void SIRSemaVisitor::warning(const NodeBase& node, std::string_view msg, std::ostream& out) const {
    out << "<SIR sema>\n";

    auto [loc, file] = ctx.src_info_pool.get_loc_and_file(node.location);
    stc::warning(file, loc, msg, out);
}

TypeId SIRSemaVisitor::expr_type(NodeId node_id) const {
    Expr* expr = ctx.get_and_dyn_cast<Expr>(node_id);

    if (expr == nullptr)
        return TypeId::null_id();

    return expr->type();
}

bool SIRSemaVisitor::has_type(NodeId expr_id, TypeId type_id) const {
    TypeId expr_type = this->expr_type(expr_id);

    return !expr_type.is_null() && expr_type == type_id;
}

bool SIRSemaVisitor::pre_visit_ptr(NodeBase* node) {
    if (const auto* expr = dyn_cast<Expr>(node)) {
        if (expr->type().is_null()) {
            error(*expr, "untyped SIR expression node");
            return false;
        }
    }

    return true;
}

bool SIRSemaVisitor::check_sym_decl(const NodeBase& node, SymbolId sym) {
    assert(!symbols.empty() && "no active scope in symbol table");

    if (symbols[symbols.size() - 1].contains(sym)) {
        error(node, std::format("redeclaration of symbol '{}' in current scope", ctx.get_sym(sym)));
        return false;
    }

    return true;
}

void SIRSemaVisitor::visit_VarDecl(VarDecl& var_decl) {
    if (!check_sym_decl(var_decl, var_decl.identifier))
        return;

    if (!var_decl.initializer.is_null()) {
        if (!has_type(var_decl.initializer, var_decl.type))
            return error(var_decl,
                         "invalid variable declaration, type mismatch between declaration's "
                         "type and initializer expression's type");

        visit(var_decl.initializer);
    }

    symbols[symbols.size() - 1].insert(var_decl.identifier);
}

void SIRSemaVisitor::visit_FunctionDecl(FunctionDecl& fn_decl) {
    if (!check_sym_decl(fn_decl, fn_decl.identifier))
        return;

    if (expected_ret_type.has_value())
        return error(fn_decl, "nested function declarations");

    for (auto param : fn_decl.param_decls) {
        if (ctx.get_and_dyn_cast<ParamDecl>(param) == nullptr)
            return error(
                fn_decl,
                "invalid function declaration, parameter list contains non-param-decl statement");

        visit(param);
    }

    if (fn_decl.return_type.is_null())
        return error(fn_decl, "invalid function declaration, return type is null");

    if (!fn_decl.body.is_null() && ctx.isa<FunctionDecl>(fn_decl.body))
        return error(fn_decl, "function definition's body must open a new scope");

    expected_ret_type = fn_decl.return_type;
    symbols.emplace_back();
    visit(fn_decl.body);
    symbols.pop_back();
    expected_ret_type = std::nullopt;

    symbols[symbols.size() - 1].insert(fn_decl.identifier);
}

void SIRSemaVisitor::visit_ParamDecl(ParamDecl& param_decl) {
    if (param_decl.param_type.is_null())
        return error(param_decl, "parameter declaration with null type");
}

void SIRSemaVisitor::visit_StructDecl(StructDecl& struct_decl) {
    if (!check_sym_decl(struct_decl, struct_decl.identifier))
        return;

    TypeId s_id = ctx.type_pool.get_struct_td(struct_decl.identifier);

    if (s_id.is_null())
        return error(struct_decl,
                     "struct decl without corresponding struct descriptor in type pool");

    StructTD s_td = ctx.type_pool.get_td(s_id).as<StructTD>();

    if (s_td.data == nullptr)
        return error(struct_decl, "incomplete struct type, struct data pointer is null");

    if (s_td.data->name != struct_decl.identifier)
        return error(struct_decl, "invalid struct decl, name mismatch between declared struct type "
                                  "and referred to struct type descriptor");

    if (s_td.data->fields.size() != struct_decl.field_decls.size())
        return error(struct_decl, "invalid struct decl, number of fields in declaration does not "
                                  "match number of fields in corresponding struct type");

    for (auto field_decl_id : struct_decl.field_decls) {
        auto* field_decl = ctx.get_and_dyn_cast<FieldDecl>(field_decl_id);

        if (field_decl == nullptr)
            return error(
                *ctx.get_node(field_decl_id),
                "invalid struct decl, a field declaration id points to a non-field-decl node");

        auto it = std::find_if(
            s_td.data->fields.begin(), s_td.data->fields.end(),
            [field_decl](StructData::FieldInfo fi) { return fi.name == field_decl->identifier; });

        if (it == s_td.data->fields.end())
            return error(*field_decl, "invalid field inside struct decl, field decl's identifier "
                                      "not found in corresponding struct type's field list");

        if (field_decl->field_type != it->type)
            return error(*field_decl, "invalid field inside struct decl, field decl's type does "
                                      "not match field type inside corresponding struct type");
    }

    symbols[symbols.size() - 1].insert(struct_decl.identifier);
}

void SIRSemaVisitor::visit_FieldDecl([[maybe_unused]] FieldDecl& field_decl) {}

void SIRSemaVisitor::visit_BoolLiteral(BoolLiteral& bool_lit) {
    if (!ctx.type_pool.is_type_of<BoolTD>(bool_lit.type()))
        return error(bool_lit, "bool literal node with non-bool type");
}

void SIRSemaVisitor::visit_IntLiteral(IntLiteral& int_lit) {
    if (!ctx.type_pool.is_type_of<IntTD>(int_lit.type()))
        return error(int_lit, "int literal node with non-integer type");
}

void SIRSemaVisitor::visit_FloatLiteral(FloatLiteral& float_lit) {
    if (!ctx.type_pool.is_type_of<FloatTD>(float_lit.type()))
        return error(float_lit, "float literal node with non-floating-point type");
}

void SIRSemaVisitor::visit_VectorLiteral(VectorLiteral& vec_lit) {
    const auto& vec_td = ctx.type_pool.get_td(vec_lit.type());

    if (!vec_td.is_vector())
        return error(vec_lit, "vector literal node with non-vector type");

    TypeId comp_t_id = vec_td.as<VectorTD>().component_type_id;

    if (comp_t_id.is_null())
        return error(vec_lit, "incomplete vector literal type, null id for component type");

    for (auto comp : vec_lit.components) {
        if (!has_type(comp, comp_t_id))
            return error(vec_lit, "invalid vector literal, type mismatch between component type "
                                  "and component initializer expression's type");
    }
}

void SIRSemaVisitor::visit_MatrixLiteral(MatrixLiteral& mat_lit) {
    const auto& mat_td = ctx.type_pool.get_td(mat_lit.type());

    if (!mat_td.is_matrix())
        return error(mat_lit, "matrix literal node with non-matrix type");

    auto col_t_id = mat_td.as<MatrixTD>().column_type_id;
    if (col_t_id.is_null())
        return error(mat_lit, "incomplete matrix literal node type, missing column type");

    const auto& col_td = ctx.type_pool.get_td(col_t_id);
    if (!col_td.is_vector())
        return error(mat_lit, "invalid matrix literal type, non-vector column type");

    TypeId comp_t_id = col_td.as<VectorTD>().component_type_id;
    for (auto comp : mat_lit.data) {
        if (!has_type(comp, comp_t_id))
            return error(mat_lit, "invalid matrix literal, type mismatch between component type "
                                  "and component initializer expression's type");
    }
}

void SIRSemaVisitor::visit_ArrayLiteral(ArrayLiteral& arr_lit) {
    const auto& arr_td = ctx.type_pool.get_td(arr_lit.type());

    if (!arr_td.is_array())
        return error(arr_lit, "array literal node with non-array type");

    TypeId elem_t_id = arr_td.as<ArrayTD>().element_type_id;

    for (auto el : arr_lit.elements) {
        if (!has_type(el, elem_t_id))
            return error(arr_lit, "invalid array literal, type mismatch between element type and "
                                  "element initializer expression's type");
    }
}

void SIRSemaVisitor::visit_SwizzleLiteral(SwizzleLiteral& swizzle_lit) {
    if (swizzle_lit.count() > 4)
        return error(swizzle_lit, "invalid swizzle literal, component count is greater than 4");
}

void SIRSemaVisitor::visit_StructInstantiation(StructInstantiation& s_inst) {
    const auto& struct_td = ctx.type_pool.get_td(s_inst.type());

    if (!struct_td.is_struct())
        return error(s_inst, "struct instantiation node with non-struct type");

    const StructData* s_data = struct_td.as<StructTD>().data;

    if (s_data == nullptr)
        return error(s_inst, "invalid struct type, StructData pointer was null");

    if (s_data->name != s_inst.struct_name)
        return error(s_inst, "invalid struct instantiation node, name mismatch between referred to "
                             "struct and name of node's type");

    if (s_inst.field_values.size() != s_data->fields.size())
        return error(s_inst,
                     "invalid struct instantiation node, number of field initializer expressions "
                     "does not match the number of fields in the referred to struct type");

    for (size_t i = 0; i < s_data->fields.size(); i++) {
        if (!has_type(s_inst.field_values[i], s_data->fields[i].type))
            return error(
                s_inst,
                std::format("invalid struct instantiation node, type mismatch in initializer "
                            "expression of field '{}'",
                            ctx.get_sym(s_data->fields[i].name)));
    }
}

void SIRSemaVisitor::visit_ScopedExpr(ScopedExpr& scoped_expr) {
    symbols.emplace_back();
    visit(scoped_expr.inner);
    symbols.pop_back();
}

void SIRSemaVisitor::visit_UnaryOp(UnaryOp& un_op) {
    visit(un_op.target);
}

void SIRSemaVisitor::visit_BinaryOp(BinaryOp& bin_op) {
    visit(bin_op.rhs);
    visit(bin_op.lhs);

    TypeId lhs_t = expr_type(bin_op.lhs);
    TypeId rhs_t = expr_type(bin_op.rhs);

    if (lhs_t.is_null() || rhs_t.is_null())
        return error(bin_op, "incomplete BinaryOp node");

    if (lhs_t != rhs_t)
        return error(bin_op, "invalid BinaryOp, lhs-rhs type mismatch");
}

void SIRSemaVisitor::visit_ExplicitCast(ExplicitCast& expl_cast) {
    visit(expl_cast.inner);
}

void SIRSemaVisitor::visit_IndexerExpr(IndexerExpr& idx_expr) {
    visit(idx_expr.target_arr);
    visit(idx_expr.indexer);
}

void SIRSemaVisitor::visit_FieldAccess(FieldAccess& acc) {
    visit(acc.target);
    visit(acc.field_decl);
}

void SIRSemaVisitor::visit_Assignment(Assignment& assignment) {
    // TODO: l-value, r-value
    visit(assignment.target);
    visit(assignment.value);
}

void SIRSemaVisitor::visit_FunctionCall(FunctionCall& fn_call) {
    for (auto arg : fn_call.args)
        visit(arg);
}

void SIRSemaVisitor::visit_ConstructorCall(ConstructorCall& ctor_call) {
    for (auto arg : ctor_call.args)
        visit(arg);
}

void SIRSemaVisitor::visit_DeclRefExpr(DeclRefExpr& decl_ref) {
    if (decl_ref.decl.is_null())
        return error(decl_ref, "incomplete DeclRefExpr node");

    visit(decl_ref.decl);

    Decl* decl = ctx.get_and_dyn_cast<Decl>(decl_ref.decl);

    if (decl == nullptr)
        return error(decl_ref, "DeclRefExpr node with storing non-decl reference");

    auto it = std::find_if(symbols.rbegin(), symbols.rend(),
                           [&decl](const auto& scope) { return scope.contains(decl->identifier); });

    if (it == symbols.rend())
        return error(decl_ref,
                     "invalid DeclRefExpr, declaration target not found in surrounding scopes");
}

void SIRSemaVisitor::visit_ScopedStmt([[maybe_unused]] ScopedStmt& scoped_stmt) {
    symbols.emplace_back();
    visit(scoped_stmt.inner_stmt);
    symbols.pop_back();
}

void SIRSemaVisitor::visit_CompoundStmt(CompoundStmt& cmpd_stmt) {
    for (auto stmt : cmpd_stmt.body)
        visit(stmt);
}

void SIRSemaVisitor::visit_IfStmt(IfStmt& if_stmt) {
    visit(if_stmt.condition);
    visit(if_stmt.true_block);

    if (!if_stmt.false_block.is_null())
        visit(if_stmt.false_block);

    if (!has_type(if_stmt.condition, ctx.type_pool.bool_td()))
        return error(if_stmt, "invalid IfStmt, condition's type must be bool");
}

void SIRSemaVisitor::visit_WhileStmt(WhileStmt& while_stmt) {
    visit(while_stmt.condition);
    visit(while_stmt.body);

    if (!has_type(while_stmt.condition, ctx.type_pool.bool_td()))
        return error(while_stmt, "invalid WhileStmt, condition's type must be bool");
}

void SIRSemaVisitor::visit_ReturnStmt(ReturnStmt& return_stmt) {
    if (!expected_ret_type.has_value())
        return error(return_stmt, "unexpected return stmt");

    if (!return_stmt.inner.is_null()) {
        Expr* inner_expr = ctx.get_and_dyn_cast<Expr>(return_stmt.inner);

        if (inner_expr == nullptr)
            return error(return_stmt, "invalid return stmt, inner node is not an expression");

        visit(return_stmt.inner);

        assert(expected_ret_type.has_value() &&
               "expected_ret_type invalidated by visiting inner expression");

        if (*expected_ret_type != inner_expr->type())
            return error(return_stmt,
                         "invalid return stmt, type mismatch between expected return type "
                         "and inner expression's type");
    } else if (expected_ret_type != ctx.type_pool.void_td()) {
        return error(return_stmt, "invalid empty return in non-void returning context");
    }
}

void SIRSemaVisitor::visit_ContinueStmt([[maybe_unused]] ContinueStmt& continue_stmt) {}

void SIRSemaVisitor::visit_BreakStmt([[maybe_unused]] BreakStmt& break_stmt) {}

} // namespace stc::sir