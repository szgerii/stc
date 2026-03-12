#pragma once

#include "common/src_info.h"
#include "common/utils.h"
#include "frontend/jl/context.h"
#include "types/types.h"

#define SAME_NODE_T_DEF(Kind)                                                                      \
    static bool same_node_t(const Expr* node) {                                                    \
        return node->kind() == (Kind);                                                             \
    }

namespace stc::jl {

using namespace stc::types;

using CtxRef = JLCtx&;

// clang-format off
enum class NodeKind : uint8_t {
    #define X(type, kind) kind,

    InvalidKind,

    FirstExpr,    
    #define X_FIRST(type, kind) kind = FirstExpr,
        #include "frontend/jl/node_defs/expr.def"
    #undef X_FIRST
    LastExpr = If,

    FirstStmt,
    #define X_FIRST(type, kind) kind = FirstStmt,
        #include "frontend/jl/node_defs/stmt.def"
    #undef X_FIRST
    LastStmt = Break

    #undef X
};
// clang-format on

struct Expr {
    SrcLocationId location;
    TypeId type;
    NodeKind _kind;
    uint8_t _node_storage;

    explicit Expr(SrcLocationId location, NodeKind kind, TypeId type, uint8_t node_storage = 0U)
        : location{location}, type{type}, _kind{kind}, _node_storage{node_storage} {}

    explicit Expr(SrcLocationId location, NodeKind kind, uint8_t node_storage = 0U)
        : Expr{location, kind, TypeId::null_id(), node_storage} {}

    NodeKind kind() const { return _kind; }
    uint8_t node_storage() const { return _node_storage; }

    static bool same_node_t(const Expr* node) { return node != nullptr; }
};

struct Stmt : public Expr {
    explicit Stmt(SrcLocationId location, NodeKind kind, uint8_t node_storage)
        : Expr{location, kind, TypeId::void_id(), node_storage} {}

    explicit Stmt(SrcLocationId location, NodeKind kind)
        : Expr{location, kind, TypeId::void_id()} {}

    static bool same_node_t(const Expr* node) {
        return node != nullptr && NodeKind::FirstStmt <= node->kind() &&
               node->kind() <= NodeKind::LastStmt;
    }
};

struct BoolLiteral : public Expr {
    explicit BoolLiteral(SrcLocationId location, bool value)
        : Expr{location, NodeKind::BoolLit, TypeId::bool_id(), static_cast<uint8_t>(value)} {}

    bool value() const { return static_cast<bool>(node_storage()); }

    SAME_NODE_T_DEF(NodeKind::BoolLit)
};

namespace detail {

template <NodeKind Kind, typename T, uint32_t Width, bool IsSigned>
requires std::integral<T>
struct IntLiteral : public Expr {
    T value;

    explicit IntLiteral(SrcLocationId location, T value, CtxRef ctx)
        : Expr{location, Kind, ctx.type_pool.int_td(Width, IsSigned)}, value{value} {}

    SAME_NODE_T_DEF(Kind)
};

} // namespace detail

using Int32Literal  = detail::IntLiteral<NodeKind::I32Lit, int32_t, 32, true>;
using Int64Literal  = detail::IntLiteral<NodeKind::I64Lit, int64_t, 64, true>;
using UInt8Literal  = detail::IntLiteral<NodeKind::U8Lit, uint8_t, 8, false>;
using UInt16Literal = detail::IntLiteral<NodeKind::U16Lit, uint16_t, 16, false>;
using UInt32Literal = detail::IntLiteral<NodeKind::U32Lit, uint32_t, 32, false>;
using UInt64Literal = detail::IntLiteral<NodeKind::U64Lit, uint64_t, 64, false>;

struct UInt128Literal : public Expr {
    uint64_t high, low;

    explicit UInt128Literal(SrcLocationId location, uint64_t high, uint64_t low, CtxRef ctx)
        : Expr{location, NodeKind::U128Lit, ctx.jl_UInt128_t}, high{high}, low{low} {}

    SAME_NODE_T_DEF(NodeKind::U128Lit)
};

struct Float32Literal : public Expr {
    float value;

    explicit Float32Literal(SrcLocationId location, float value, CtxRef ctx)
        : Expr{location, NodeKind::F32Lit, ctx.jl_Float32_t}, value{value} {}

    SAME_NODE_T_DEF(NodeKind::F32Lit)
};

struct Float64Literal : public Expr {
    double value;

    explicit Float64Literal(SrcLocationId location, double value, CtxRef ctx)
        : Expr{location, NodeKind::F64Lit, ctx.jl_Float64_t}, value{value} {}

    SAME_NODE_T_DEF(NodeKind::F64Lit)
};

struct StringLiteral : public Expr {
    std::string value;

    explicit StringLiteral(SrcLocationId location, std::string value, CtxRef ctx)
        : Expr{location, NodeKind::StrLit, ctx.jl_String_t}, value{std::move(value)} {}

    SAME_NODE_T_DEF(NodeKind::StrLit)
};

struct FunctionCall : public Expr {
    NodeId target_fn;
    std::vector<NodeId> args;

    explicit FunctionCall(SrcLocationId location, NodeId target_fn, std::vector<NodeId> args)
        : Expr{location, NodeKind::FnCall}, target_fn{target_fn}, args{std::move(args)} {}

    SAME_NODE_T_DEF(NodeKind::FnCall)
};

struct IfExpr : public Expr {
    NodeId condition, true_branch, false_branch;

    explicit IfExpr(SrcLocationId location, NodeId condition, NodeId true_branch,
                    NodeId false_branch = NodeId::null_id())
        : Expr{location, NodeKind::If},
          condition{condition},
          true_branch{true_branch},
          false_branch{false_branch} {}

    SAME_NODE_T_DEF(NodeKind::If)
};

struct ReturnStmt : public Stmt {
    NodeId inner;

    explicit ReturnStmt(SrcLocationId location, NodeId inner)
        : Stmt{location, NodeKind::Return}, inner{inner} {}

    SAME_NODE_T_DEF(NodeKind::Return)
};

struct ContinueStmt : public Stmt {
    explicit ContinueStmt(SrcLocationId location)
        : Stmt{location, NodeKind::Continue} {}

    SAME_NODE_T_DEF(NodeKind::Continue)
};

struct BreakStmt : public Stmt {
    explicit BreakStmt(SrcLocationId location)
        : Stmt{location, NodeKind::Break} {}

    SAME_NODE_T_DEF(NodeKind::Break)
};

}; // namespace stc::jl

#undef SAME_NODE_T_DEF
