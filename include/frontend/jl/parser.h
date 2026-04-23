#pragma once

#include "julia_guard.h"

#include "frontend/jl/ast.h"
#include "frontend/jl/context.h"
#include "sir/context.h"

#define PARSER_DECL(name) NodeId parse_##name(jl_expr_t* expr, size_t nargs)

namespace stc::jl {

class JLParser {
    JLCtx& ctx;
    rt::JuliaSymbolCache& sym_cache;

    SrcLocationId cur_loc;
    bool _success = true;

    using ParseCallback = NodeId (JLParser::*)(jl_value_t*);

public:
    std::optional<std::string> fallback_file = std::nullopt;

    explicit JLParser(JLCtx& ctx)
        : ctx{ctx}, sym_cache{ctx.jl_env.symbol_cache} {

        std::ignore = ctx.src_info_pool.get_file("dummy file");
        cur_loc     = ctx.src_info_pool.get_location(1, 1);
    }

    NodeId parse(jl_value_t* node);
    NodeId parse_code(std::string_view code);

    bool success() const { return _success; }

private:
    NodeId parse_expr(jl_expr_t* expr);

    PARSER_DECL(var_decl);
    PARSER_DECL(method_decl);
    PARSER_DECL(assignment);
    PARSER_DECL(block);
    PARSER_DECL(call);
    PARSER_DECL(if);
    PARSER_DECL(while);
    PARSER_DECL(return);
    PARSER_DECL(dot_chain);
    PARSER_DECL(vect);
    PARSER_DECL(ref);
    PARSER_DECL(log_op);
    PARSER_DECL(struct);

    // helper parser functions, not participating in the regular parse_expr flow
    std::pair<jl_value_t*, TypeId> parse_type_annotation(jl_expr_t* annot);
    jl_value_t* unwrap_layout_qual(jl_expr_t* lq_expr, std::vector<QualKind>& quals,
                                   LQPayload& lq_payloads);
    NodeId parse_qualified_decl(jl_value_t* qualified_expr,
                                ParseCallback next_parser = &JLParser::parse);
    NodeId parse_param_decl(jl_value_t* param_v);
    NodeId parse_field_decl(jl_value_t* field_decl_v);

    NodeId parse_method_decl(jl_value_t* val) {
        jl_expr_t* expr = jl_is_expr(val) ? safe_cast<jl_expr_t>(val) : nullptr;

        if (expr != nullptr && (expr->head == sym_cache.function || expr->head == sym_cache.eq))
            return parse_method_decl(expr, jl_expr_nargs(expr));

        return internal_error("unexpected parse_method_decl invocation");
    }

    NodeId parse_var_decl(jl_value_t* val) {
        jl_expr_t* expr = jl_is_expr(val) ? safe_cast<jl_expr_t>(val) : nullptr;

        if (expr != nullptr && (expr->head == sym_cache.global || expr->head == sym_cache.local ||
                                expr->head == sym_cache.eq || expr->head == sym_cache.dbl_col))
            return parse_var_decl(expr, jl_expr_nargs(expr));

        return internal_error("unexpected parse_var_decl invocation");
    }

    template <typename T, typename... Args>
    NodeId emplace_node(Args&&... args) {
        return ctx.emplace_node<T>(std::forward<Args>(args)...).first;
    }

    TypeId resolve_type(jl_value_t* type);
    NodeId error(std::string_view msg, SrcLocationId loc_id = SrcLocationId::null_id());
    NodeId internal_error(std::string_view msg, SrcLocationId loc_id = SrcLocationId::null_id());
};

#undef PARSER_DECL

} // namespace stc::jl
