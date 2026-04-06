#pragma once

#include "ast/context.h"
#include "common/literals.h"
#include "frontend/jl/ast.h"
#include "frontend/jl/module_pool.h"
#include "frontend/jl/rt/env.h"

namespace stc::jl {

using types::BuiltinTD;

struct Expr;
enum class NodeKind : uint8_t;

enum class BuiltinTypeKind : uint8_t { Nothing, String, Symbol };

class JLCtx : public ASTCtx<NodeId, Expr> {
    using Base = ASTCtx<NodeId, Expr>;

public:
    rt::JuliaRTEnv jl_env{};

    ModulePool module_pool;

    // clang-format off
    #define X(type) types::TypeId type() const { return _##type; }
        #include "frontend/jl/node_defs/types.def"
    #undef X
    // clang-format on

    explicit JLCtx(NodeId::id_type node_arena_kb            = 128U,
                   SrcLocationId::id_type src_info_arena_kb = 128U,
                   TypeId::id_type type_arena_kb = 32U, SymbolId::id_type sym_arena_kb = 64U,
                   ModuleId::id_type module_arena_b = 128_u8)
        : ASTCtx{{{BuiltinTypeKind::Nothing}, {BuiltinTypeKind::String}, {BuiltinTypeKind::Symbol}},
                 node_arena_kb,
                 src_info_arena_kb,
                 type_arena_kb,
                 sym_arena_kb},
          module_pool{module_arena_b} {

        init_jl_types();
    }

    JLCtx(const JLCtx&)            = delete;
    JLCtx& operator=(const JLCtx&) = delete;
    JLCtx(JLCtx&&)                 = default;
    JLCtx& operator=(JLCtx&&)      = default;

protected:
    template <typename T, typename U>
    explicit JLCtx(ASTCtx<T, U>&& other, NodeId::id_type node_arena_kb = 128U)
        : Base{std::move(other), node_arena_kb} {
        init_jl_types();
    }

public:
    template <typename T, typename U>
    [[nodiscard]] static JLCtx move_pools_from(ASTCtx<T, U>&& other,
                                               NodeId::id_type node_arena_kb) {
        return JLCtx{std::move(other), node_arena_kb};
    }

private:
    // adds Julia type not already present and assigns them to the _<type name> members
    // if types are already inside the pool and the members, this is a no-op
    void init_jl_types();

    // clang-format off
    #define X(type) types::TypeId _##type = types::TypeId::null_id();
        #include "frontend/jl/node_defs/types.def"
    #undef X
    // clang-format on
};

} // namespace stc::jl
