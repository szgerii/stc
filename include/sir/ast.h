#pragma once

#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <tuple>
#include <vector>

#include "common/base.h"
#include "common/src_info.h"
#include "common/utils.h"
#include "types/types.h"

#define SAME_NODE_KIND_DEF(Kind)                                                                   \
    static bool same_node_kind(NodeKind kind) {                                                    \
        return kind == (Kind);                                                                     \
    }

namespace stc::sir {

using namespace stc::types;

// CLEANUP: go through structs and make use of _node_storage properly
// CLEANUP: remove need for explicit type in ctors if possible
// FEATURE: trailing objects pattern where applicable

struct NodeId : public SplitU32Id {
    using SplitU32Id::SplitU32Id;

    bool is_null() const { return *this == null_id(); }

    // TODO: enforce in arenas
    static constexpr NodeId null_id() { return NodeId{0U, 0U}; }
};

// ============
//   NodeKind
// ============

// clang-format off
enum class NodeKind : uint8_t {
    #define X(type, kind) kind,

    NullKind,
    FirstDecl,

    #define X_FIRST(type, kind) kind = FirstDecl,
        #include "sir/node_defs/decl.def"
    #undef X_FIRST

    LastDecl = FieldDecl,
    FirstExpr,

    #define X_FIRST(type, kind) kind = FirstExpr,
        #include "sir/node_defs/expr.def"
    #undef X_FIRST

    LastExpr = DeclRef,
    FirstStmt,

    #define X_FIRST(type, kind) kind = FirstStmt,
        #include "sir/node_defs/stmt.def"
    #undef X_FIRST

    LastStmt = Break,

    #undef X
};
// clang-format on

// ===================
//   Base Node Types
// ===================

struct NodeBase {
    using kind_type = NodeKind;

    SrcLocationId location;
    uint32_t _kind         : 8;
    uint32_t _node_storage : 24;

    explicit NodeBase(SrcLocationId location, NodeKind kind = NodeKind::NullKind,
                      uint32_t node_storage = 0U)
        : location{location},
          _kind{static_cast<uint32_t>(kind)},
          _node_storage{0x00FFFFFF & static_cast<uint32_t>(node_storage)} {

        assert(node_storage <= 0xFFFFFF && "implicit truncation in node_storage of NodeBase");
    }

    // NOTE: node_storage actually returns a "uint24_t"
    uint32_t node_storage() const { return _node_storage; }

    [[nodiscard]] NodeKind kind() const { return static_cast<NodeKind>(_kind); }

    static bool same_node_kind(NodeKind) { return true; }

    static NodeBase* safe_cast_to_base(void* node_ptr, NodeId node_id);
};

struct Decl : public NodeBase {
    // CLEANUP: better packing for decl, string interning
    SymbolId identifier;

    explicit Decl(SrcLocationId location, NodeKind kind, SymbolId identifier)
        : NodeBase{location, kind}, identifier{identifier} {}

    Decl(const Decl&)                = default;
    Decl& operator=(const Decl&)     = default;
    Decl(Decl&&) noexcept            = default;
    Decl& operator=(Decl&&) noexcept = default;

    static bool same_node_kind(NodeKind kind) {
        return NodeKind::FirstDecl <= kind && kind <= NodeKind::LastDecl;
    }
};

struct Stmt : public NodeBase {
    explicit Stmt(SrcLocationId location, NodeKind kind, uint32_t node_storage = 0U)
        : NodeBase{location, kind, node_storage} {}

    Stmt(const Stmt&)                = default;
    Stmt& operator=(const Stmt&)     = default;
    Stmt(Stmt&&) noexcept            = default;
    Stmt& operator=(Stmt&&) noexcept = default;

    static bool same_node_kind(NodeKind kind) {
        return NodeKind::FirstStmt <= kind && kind <= NodeKind::LastStmt;
    }
};

// ================
//   Declarations
// ================

struct VarDecl : public Decl {
    TypeId type;
    NodeId initializer;

    explicit VarDecl(SrcLocationId location, SymbolId var_name, TypeId type,
                     NodeId initializer = NodeId::null_id())
        : Decl{location, NodeKind::VarDecl, var_name}, type{type}, initializer{initializer} {}

    SAME_NODE_KIND_DEF(NodeKind::VarDecl)
};

struct ParamDecl : public Decl {
    TypeId param_type;

    explicit ParamDecl(SrcLocationId location, SymbolId param_name, TypeId type)
        : Decl{location, NodeKind::ParamDecl, param_name}, param_type{type} {}

    SAME_NODE_KIND_DEF(NodeKind::ParamDecl)
};

struct FunctionDecl : public Decl {
    TypeId return_type;
    std::vector<NodeId> param_decls;
    NodeId body;

    explicit FunctionDecl(SrcLocationId location, SymbolId fn_name, TypeId return_type,
                          std::vector<NodeId> param_decls, NodeId body = NodeId::null_id())
        : Decl{location, NodeKind::FuncDecl, fn_name},
          return_type{return_type},
          param_decls{std::move(param_decls)},
          body{body} {}

    SAME_NODE_KIND_DEF(NodeKind::FuncDecl)
};

struct FieldDecl : public Decl {
    TypeId field_type;

    explicit FieldDecl(SrcLocationId location, SymbolId field_name, TypeId field_type)
        : Decl{location, NodeKind::FieldDecl, field_name}, field_type{field_type} {}

    SAME_NODE_KIND_DEF(NodeKind::FieldDecl)
};

struct StructDecl : public Decl {
    std::vector<NodeId> field_decls;

    explicit StructDecl(SrcLocationId location, SymbolId struct_name,
                        std::vector<NodeId> field_decls)
        : Decl{location, NodeKind::StructDecl, struct_name}, field_decls{std::move(field_decls)} {}

    SAME_NODE_KIND_DEF(NodeKind::StructDecl)
};

// ===============
//   Expressions
// ===============

/*
Stmt has 24 free bits for storage
TypeId (16 bits) is packed into the upper 16 bits, lower 8 bits remain free for children
*/
struct Expr : public Stmt {
    explicit Expr(SrcLocationId location, NodeKind kind, TypeId type, uint8_t node_storage = 0U)
        : Stmt{location, kind, pack_to_u32(type, node_storage)} {}

    explicit Expr(SrcLocationId location, NodeKind kind, uint8_t node_storage = 0U)
        : Expr{location, kind, TypeId::null_id(), node_storage} {}

    TypeId type() const { return static_cast<TypeId::id_type>((_node_storage >> 8) & 0x0000FFFF); }
    uint8_t node_storage() const { return static_cast<uint8_t>(_node_storage & 0xFF); }

    static bool same_node_kind(NodeKind kind) {
        return NodeKind::FirstExpr <= kind && kind <= NodeKind::LastExpr;
    }

private:
    static uint32_t pack_to_u32(TypeId type, uint8_t node_storage) {
        return (static_cast<uint32_t>(type) << 8) | node_storage;
    }
};

struct BoolLiteral : public Expr {
    explicit BoolLiteral(SrcLocationId location, bool value)
        : Expr{location, NodeKind::BoolLit, static_cast<uint8_t>(value)} {}

    bool value() const { return static_cast<bool>(node_storage()); }

    SAME_NODE_KIND_DEF(NodeKind::BoolLit)
};

struct IntLiteral : public Expr {
    std::string data;

    explicit IntLiteral(SrcLocationId location, TypeId type, std::string data)
        : Expr{location, NodeKind::IntLit, type}, data{std::move(data)} {}

    SAME_NODE_KIND_DEF(NodeKind::IntLit)
};

struct FloatLiteral : public Expr {
    std::string data;

    explicit FloatLiteral(SrcLocationId location, TypeId type, std::string data)
        : Expr{location, NodeKind::FloatLit, type}, data{std::move(data)} {}

    SAME_NODE_KIND_DEF(NodeKind::FloatLit)
};

struct VectorLiteral : public Expr {
    std::vector<NodeId> components;

    explicit VectorLiteral(SrcLocationId location, TypeId type, std::vector<NodeId> components)
        : Expr{location, NodeKind::VecLit, type}, components{std::move(components)} {}

    SAME_NODE_KIND_DEF(NodeKind::VecLit)
};

// column-major storage
struct MatrixLiteral : public Expr {
    std::vector<NodeId> data;

    explicit MatrixLiteral(SrcLocationId location, TypeId type, std::vector<NodeId> data)
        : Expr{location, NodeKind::MatLit, type}, data{std::move(data)} {}

    SAME_NODE_KIND_DEF(NodeKind::MatLit)
};

struct ArrayLiteral : public Expr {
    std::vector<NodeId> elements;

    explicit ArrayLiteral(SrcLocationId location, TypeId type, std::vector<NodeId> elements)
        : Expr{location, NodeKind::ArrayLit, type}, elements{std::move(elements)} {}

    SAME_NODE_KIND_DEF(NodeKind::ArrayLit)
};

struct StructInstantiation : public Expr {
    SymbolId struct_name;
    std::vector<NodeId> field_values;

    explicit StructInstantiation(SrcLocationId location, SymbolId struct_name,
                                 std::vector<NodeId> field_values)
        : Expr{location, NodeKind::StructInst},
          struct_name{struct_name},
          field_values{std::move(field_values)} {}

    SAME_NODE_KIND_DEF(NodeKind::StructInst)
};

struct ScopedExpr : public Expr {
    NodeId inner_expr;

    explicit ScopedExpr(SrcLocationId location, NodeId inner_expr)
        : Expr{location, NodeKind::ScopedExpr}, inner_expr{inner_expr} {}

    SAME_NODE_KIND_DEF(NodeKind::ScopedExpr)
};

struct BinaryOp : public Expr {
    enum class OpKind : uint8_t { add, sub, mul, div, pow, mod };

    NodeId lhs, rhs;

    explicit BinaryOp(SrcLocationId location, OpKind op, NodeId lhs, NodeId rhs)
        : Expr{location, NodeKind::BinOp, static_cast<uint8_t>(op)}, lhs{lhs}, rhs{rhs} {}

    OpKind op() const { return static_cast<OpKind>(node_storage()); }

    SAME_NODE_KIND_DEF(NodeKind::BinOp)
};

struct ExplicitCast : public Expr {
    NodeId inner;

    explicit ExplicitCast(SrcLocationId location, TypeId target_type, NodeId inner)
        : Expr{location, NodeKind::ExplCast, target_type}, inner{inner} {}

    SAME_NODE_KIND_DEF(NodeKind::ExplCast)
};

struct FunctionCall : public Expr {
    SymbolId fn_name;
    std::vector<NodeId> args;

    explicit FunctionCall(SrcLocationId location, SymbolId fn_name, std::vector<NodeId> args)
        : Expr{location, NodeKind::FnCall}, fn_name{fn_name}, args{std::move(args)} {}

    SAME_NODE_KIND_DEF(NodeKind::FnCall)
};

struct DeclRefExpr : public Expr {
    NodeId decl;

    explicit DeclRefExpr(SrcLocationId location, NodeId decl)
        : Expr{location, NodeKind::DeclRef}, decl{decl} {}

    SAME_NODE_KIND_DEF(NodeKind::DeclRef)
};

// ===============
//   Statements
// ===============

struct CompoundStmt : public Stmt {
    std::vector<NodeId> body;

    explicit CompoundStmt(SrcLocationId location, std::vector<NodeId> body)
        : Stmt{location, NodeKind::Compound}, body{std::move(body)} {}

    SAME_NODE_KIND_DEF(NodeKind::Compound)
};

struct ScopedStmt : public Stmt {
    NodeId inner_stmt;

    explicit ScopedStmt(SrcLocationId location, NodeId inner_stmt)
        : Stmt{location, NodeKind::ScopedStmt}, inner_stmt{inner_stmt} {}

    SAME_NODE_KIND_DEF(NodeKind::ScopedStmt)
};

struct IfStmt : public Stmt {
    NodeId condition_expr;
    NodeId true_block;
    NodeId false_block;

    explicit IfStmt(SrcLocationId location, NodeId condition_expr, NodeId true_block,
                    NodeId false_block)
        : Stmt{location, NodeKind::If},
          condition_expr{condition_expr},
          true_block{true_block},
          false_block{false_block} {}

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

template <typename T>
concept CNodeTy = std::derived_from<T, NodeBase>;

template <typename T>
concept CStmtTy = std::derived_from<T, Stmt>;

template <typename T>
concept CExprTy = std::derived_from<T, Expr>;

template <typename T>
concept CDeclTy = std::derived_from<T, Decl>;

} // namespace stc::sir

#undef SAME_NODE_KIND_DEF
