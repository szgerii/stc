#pragma once

#include "common/src_info.h"
#include "common/utils.h"
#include "types/types.h"

#define SAME_NODE_KIND_DEF(Kind)                                                                   \
    static bool same_node_kind(NodeKind kind) {                                                    \
        return kind == (Kind);                                                                     \
    }

namespace stc::jl {

struct NodeId : public SplitU32Id {
    using SplitU32Id::SplitU32Id;

    bool is_null() const { return *this == null_id(); }

    static constexpr NodeId null_id() { return NodeId{0U, 0U}; }
};

using namespace stc::types;

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
    using kind_type = NodeKind;

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

    static bool same_node_kind(NodeKind) { return true; }

    static Expr* safe_cast_to_base(void* node_ptr, NodeId node_id);
};

struct Stmt : public Expr {
    explicit Stmt(SrcLocationId location, NodeKind kind, uint8_t node_storage)
        : Expr{location, kind, TypeId::void_id(), node_storage} {}

    explicit Stmt(SrcLocationId location, NodeKind kind)
        : Expr{location, kind, TypeId::void_id()} {}

    static bool same_node_kind(NodeKind kind) {
        return NodeKind::FirstStmt <= kind && kind <= NodeKind::LastStmt;
    }
};

struct CompoundExpr : public Expr {
    std::vector<NodeId> body;

    explicit CompoundExpr(SrcLocationId location, std::vector<NodeId> body)
        : Expr{location, NodeKind::Compound}, body{std::move(body)} {}

    explicit CompoundExpr(SrcLocationId location, std::initializer_list<NodeId> nodes)
        : CompoundExpr{location, std::vector<NodeId>{nodes}} {}

    SAME_NODE_KIND_DEF(NodeKind::Compound)
};

struct BoolLiteral : public Expr {
    explicit BoolLiteral(SrcLocationId location, bool value)
        : Expr{location, NodeKind::BoolLit, static_cast<uint8_t>(value)} {}

    bool value() const { return static_cast<bool>(node_storage()); }

    SAME_NODE_KIND_DEF(NodeKind::BoolLit)
};

namespace detail {

template <NodeKind Kind, typename T>
requires std::integral<T>
struct IntLiteral : public Expr {
    T value;

    explicit IntLiteral(SrcLocationId location, T value)
        : Expr{location, Kind}, value{value} {}

    SAME_NODE_KIND_DEF(Kind)
};

} // namespace detail

using Int32Literal  = detail::IntLiteral<NodeKind::I32Lit, int32_t>;
using Int64Literal  = detail::IntLiteral<NodeKind::I64Lit, int64_t>;
using UInt8Literal  = detail::IntLiteral<NodeKind::U8Lit, uint8_t>;
using UInt16Literal = detail::IntLiteral<NodeKind::U16Lit, uint16_t>;
using UInt32Literal = detail::IntLiteral<NodeKind::U32Lit, uint32_t>;
using UInt64Literal = detail::IntLiteral<NodeKind::U64Lit, uint64_t>;

struct UInt128Literal : public Expr {
    uint64_t high, low;

    explicit UInt128Literal(SrcLocationId location, uint64_t high, uint64_t low)
        : Expr{location, NodeKind::U128Lit}, high{high}, low{low} {}

    SAME_NODE_KIND_DEF(NodeKind::U128Lit)
};

struct Float32Literal : public Expr {
    float value;

    explicit Float32Literal(SrcLocationId location, float value)
        : Expr{location, NodeKind::F32Lit}, value{value} {}

    SAME_NODE_KIND_DEF(NodeKind::F32Lit)
};

struct Float64Literal : public Expr {
    double value;

    explicit Float64Literal(SrcLocationId location, double value)
        : Expr{location, NodeKind::F64Lit}, value{value} {}

    SAME_NODE_KIND_DEF(NodeKind::F64Lit)
};

struct StringLiteral : public Expr {
    std::string value;

    explicit StringLiteral(SrcLocationId location, std::string value)
        : Expr{location, NodeKind::StrLit}, value{std::move(value)} {}

    SAME_NODE_KIND_DEF(NodeKind::StrLit)
};

struct SymbolLiteral : public Expr {
    SymbolId value;

    explicit SymbolLiteral(SrcLocationId location, SymbolId value)
        : Expr{location, NodeKind::SymLit}, value{value} {}

    SAME_NODE_KIND_DEF(NodeKind::SymLit)
};

struct FunctionCall : public Expr {
    NodeId target_fn;
    std::vector<NodeId> args;

    explicit FunctionCall(SrcLocationId location, NodeId target_fn, std::vector<NodeId> args)
        : Expr{location, NodeKind::FnCall}, target_fn{target_fn}, args{std::move(args)} {}

    explicit FunctionCall(SrcLocationId location, NodeId target_fn,
                          std::initializer_list<NodeId> args)
        : FunctionCall{location, target_fn, std::vector<NodeId>{args}} {}

    SAME_NODE_KIND_DEF(NodeKind::FnCall)
};

struct IfExpr : public Expr {
    NodeId condition, true_branch, false_branch;

    explicit IfExpr(SrcLocationId location, NodeId condition, NodeId true_branch,
                    NodeId false_branch = NodeId::null_id())
        : Expr{location, NodeKind::If},
          condition{condition},
          true_branch{true_branch},
          false_branch{false_branch} {}

    SAME_NODE_KIND_DEF(NodeKind::If)
};

struct ReturnStmt : public Stmt {
    NodeId inner;

    explicit ReturnStmt(SrcLocationId location, NodeId inner)
        : Stmt{location, NodeKind::Return}, inner{inner} {}

    SAME_NODE_KIND_DEF(NodeKind::Return)
};

struct ContinueStmt : public Stmt {
    explicit ContinueStmt(SrcLocationId location)
        : Stmt{location, NodeKind::Continue} {}

    SAME_NODE_KIND_DEF(NodeKind::Continue)
};

struct BreakStmt : public Stmt {
    explicit BreakStmt(SrcLocationId location)
        : Stmt{location, NodeKind::Break} {}

    SAME_NODE_KIND_DEF(NodeKind::Break)
};

}; // namespace stc::jl

#undef SAME_NODE_KIND_DEF
