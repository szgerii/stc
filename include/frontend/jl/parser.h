#pragma once

#include "julia_guard.h"

#include "frontend/jl/ast.h"
#include "frontend/jl/context.h"
#include "sir/context.h"

#define PARSER_DECL(name) NodeId parse_##name(jl_expr_t* expr, size_t nargs)

namespace stc::jl {

class JLParser {
    JLCtx ctx;
    rt::JuliaSymbolCache& sym_cache;

    SrcLocationId cur_loc;
    bool _success = true;

public:
    explicit JLParser()
        : ctx{}, sym_cache{ctx.jl_env.symbol_cache} {

        std::ignore = ctx.src_info_pool.get_file("dummy file");
        cur_loc     = ctx.src_info_pool.get_location(1, 1);
    }

    NodeId parse(jl_value_t* node);
    NodeId parse_code(std::string_view code);

    [[nodiscard]] JLCtx&& steal_ctx() { return std::move(ctx); }
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

    // helper parser functions, not participating in the regular parse_expr flow
    std::pair<jl_value_t*, TypeId> parse_type_annotation(jl_expr_t* annot);
    NodeId parse_param_decl(jl_value_t* param);

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
