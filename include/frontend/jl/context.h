#pragma once

#include "ast/context.h"
#include "frontend/jl/ast.h"

namespace stc::jl {

using types::BuiltinTD;

struct NodeId : StrongId<uint32_t> {
    using StrongId::StrongId;

    bool is_null() const { return *this == null_id(); }

    static constexpr NodeId null_id() { return 0U; }
};

struct Expr;
enum class NodeKind : uint8_t;

enum class BuiltinTypeKind : uint8_t { Nothing, String };

class JLCtx : public ASTCtx<NodeId, Expr, NodeKind> {
public:
    const types::TypeId jl_Bool_t, jl_Int32_t, jl_Int64_t, jl_UInt8_t, jl_UInt16_t, jl_UInt32_t,
        jl_UInt64_t, jl_UInt128_t, jl_Float16_t, jl_Float32_t, jl_Float64_t, jl_Nothing_t,
        jl_String_t;

    explicit JLCtx()
        : ASTCtx<NodeId, Expr, NodeKind>{{BuiltinTD{BuiltinTypeKind::Nothing},
                                          BuiltinTD{BuiltinTypeKind::String}}},
          jl_Bool_t{type_pool.bool_td()},
          jl_Int32_t{type_pool.int_td(32, true)},
          jl_Int64_t{type_pool.int_td(64, true)},
          jl_UInt8_t{type_pool.int_td(8, false)},
          jl_UInt16_t{type_pool.int_td(16, false)},
          jl_UInt32_t{type_pool.int_td(32, false)},
          jl_UInt64_t{type_pool.int_td(64, false)},
          jl_UInt128_t{type_pool.int_td(128, false)},
          jl_Float16_t{type_pool.float_td(16)},
          jl_Float32_t{type_pool.float_td(32)},
          jl_Float64_t{type_pool.float_td(64)},
          jl_Nothing_t{type_pool.builtin_td(BuiltinTypeKind::Nothing)},
          jl_String_t{type_pool.builtin_td(BuiltinTypeKind::String)} {}
};

} // namespace stc::jl
