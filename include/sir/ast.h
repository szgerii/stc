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

#define SAME_NODE_T_DEF(Kind)                                                                      \
    static bool same_node_t(const NodeBase* node) {                                                \
        return node->kind() == (Kind);                                                             \
    }

namespace stc::sir {

using namespace stc::types;

// CLEANUP: go through structs and make use of _node_storage properly
// CLEANUP: remove need for explicit type in ctors if possible
// FEATURE: trailing objects pattern where applicable

struct NodeId : public StrongId<uint32_t> {
    using StrongId::StrongId;

    bool is_null() const { return *this == null_id(); }

    // TODO: enforce in arenas
    static constexpr NodeId null_id() { return 0U; }
};

// ============
//   NodeKind
// ============

// clang-format off
enum class NodeKind : uint8_t {
    #define X(type, kind) kind,

    InvalidKind,
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
    SrcLocationId location;
    uint32_t _kind         : 8;
    uint32_t _node_storage : 24;

    explicit NodeBase(SrcLocationId location, NodeKind kind = NodeKind::InvalidKind,
                      uint32_t node_storage = 0U)
        : location{location},
          _kind{static_cast<uint32_t>(kind)},
          _node_storage{static_cast<uint32_t>(node_storage)} {

        assert(node_storage <= 0xFFFFFF && "implicit truncation in node_storage of NodeBase");
    }

    // NOTE: node_storage really returns a "uint24_t"
    uint32_t node_storage() const { return _node_storage; }

    [[nodiscard]] NodeKind kind() const { return static_cast<NodeKind>(_kind); }

    static bool same_node_t(const NodeBase*) {
        assert(false && "same_node_t called on NodeBase");
        return false;
    }
};

struct Decl : public NodeBase {
    // CLEANUP: better packing for decl, string interning
    std::string identifier;

    explicit Decl(SrcLocationId location, NodeKind kind, std::string identifier)
        : NodeBase{location, kind}, identifier{std::move(identifier)} {}

    Decl(const Decl&)            = default;
    Decl(Decl&&)                 = default;
    Decl& operator=(const Decl&) = default;
    Decl& operator=(Decl&&)      = default;

    static bool same_node_t(const NodeBase* node) {
        return NodeKind::FirstDecl <= node->kind() && node->kind() <= NodeKind::LastDecl;
    }
};

struct Stmt : public NodeBase {
    explicit Stmt(SrcLocationId location, NodeKind kind, uint32_t node_storage = 0U)
        : NodeBase{location, kind, node_storage} {}

    Stmt(const Stmt&)            = default;
    Stmt(Stmt&&)                 = default;
    Stmt& operator=(const Stmt&) = default;
    Stmt& operator=(Stmt&&)      = default;

    static bool same_node_t(const NodeBase* node) {
        return NodeKind::FirstStmt <= node->kind() && node->kind() <= NodeKind::LastStmt;
    }
};

// ================
//   Declarations
// ================

struct VarDecl : public Decl {
    TypeId type;
    NodeId initializer;

    explicit VarDecl(SrcLocationId location, std::string var_name, TypeId type,
                     NodeId initializer = NodeId::null_id())
        : Decl{location, NodeKind::VarDecl, std::move(var_name)},
          type{type},
          initializer{initializer} {}

    SAME_NODE_T_DEF(NodeKind::VarDecl)
};

struct ParamDecl : public Decl {
    TypeId param_type;

    explicit ParamDecl(SrcLocationId location, std::string param_name, TypeId type)
        : Decl{location, NodeKind::ParamDecl, std::move(param_name)}, param_type{type} {}

    SAME_NODE_T_DEF(NodeKind::ParamDecl)
};

struct FunctionDecl : public Decl {
    TypeId return_type;
    std::vector<NodeId> param_decls;
    NodeId body;

    explicit FunctionDecl(SrcLocationId location, std::string fn_name, TypeId return_type,
                          std::vector<NodeId> param_decls, NodeId body = NodeId::null_id())
        : Decl{location, NodeKind::FuncDecl, std::move(fn_name)},
          return_type{return_type},
          param_decls{std::move(param_decls)},
          body{body} {}

    SAME_NODE_T_DEF(NodeKind::FuncDecl)
};

struct FieldDecl : public Decl {
    TypeId field_type;

    explicit FieldDecl(SrcLocationId location, std::string field_name, TypeId field_type)
        : Decl{location, NodeKind::FieldDecl, std::move(field_name)}, field_type{field_type} {}

    SAME_NODE_T_DEF(NodeKind::FieldDecl)
};

struct StructDecl : public Decl {
    std::vector<NodeId> field_decls;

    explicit StructDecl(SrcLocationId location, std::string struct_name,
                        std::vector<NodeId> field_decls)
        : Decl{location, NodeKind::StructDecl, std::move(struct_name)},
          field_decls{std::move(field_decls)} {}

    SAME_NODE_T_DEF(NodeKind::StructDecl)
};

// ===============
//   Expressions
// ===============

/*
Stmt has 24 free bits for storage
TypeId (16 bits) is packed into the upper 16 bits, lower 8 bits remains free for children
*/
struct Expr : public Stmt {
    explicit Expr(SrcLocationId location, NodeKind kind, TypeId type, uint8_t node_storage = 0U)
        : Stmt{location, kind, pack_to_u32(type, node_storage)} {}

    explicit Expr(SrcLocationId location, NodeKind kind, uint8_t node_storage = 0U)
        : Expr{location, kind, TypeId::null_id(), node_storage} {}

    TypeId type() const { return static_cast<TypeId>((_node_storage >> 8) & 0xFFFF); }
    uint8_t node_storage() const { return static_cast<uint8_t>(_node_storage & 0xFF); }

    static bool same_node_t(const NodeBase* node) {
        return node->kind() >= NodeKind::FirstExpr && node->kind() <= NodeKind::LastExpr;
    }

private:
    static uint32_t pack_to_u32(TypeId type, uint8_t node_storage) {
        return (static_cast<uint32_t>(type) << 8) | node_storage;
    }
};

struct BoolLiteral : public Expr {
    explicit BoolLiteral(SrcLocationId location, bool value)
        : Expr{location, NodeKind::BoolLit, TypeId::bool_id(), static_cast<uint8_t>(value)} {}

    bool value() const { return static_cast<bool>(node_storage()); }

    SAME_NODE_T_DEF(NodeKind::BoolLit)
};

struct IntLiteral : public Expr {
    std::string data;

    explicit IntLiteral(SrcLocationId location, TypeId type, std::string data)
        : Expr{location, NodeKind::IntLit, type}, data{std::move(data)} {}

    SAME_NODE_T_DEF(NodeKind::IntLit)
};

struct FloatLiteral : public Expr {
    std::string data;

    explicit FloatLiteral(SrcLocationId location, TypeId type, std::string data)
        : Expr{location, NodeKind::FloatLit, type}, data{std::move(data)} {}

    SAME_NODE_T_DEF(NodeKind::FloatLit)
};

struct VectorLiteral : public Expr {
    std::vector<NodeId> components;

    explicit VectorLiteral(SrcLocationId location, TypeId type, std::vector<NodeId> components)
        : Expr{location, NodeKind::VecLit, type}, components{std::move(components)} {}

    SAME_NODE_T_DEF(NodeKind::VecLit)
};

// column-major storage
struct MatrixLiteral : public Expr {
    std::vector<NodeId> data;

    explicit MatrixLiteral(SrcLocationId location, TypeId type, std::vector<NodeId> data)
        : Expr{location, NodeKind::MatLit, type}, data{std::move(data)} {}

    SAME_NODE_T_DEF(NodeKind::MatLit)
};

struct ArrayLiteral : public Expr {
    std::vector<NodeId> elements;

    explicit ArrayLiteral(SrcLocationId location, TypeId type, std::vector<NodeId> elements)
        : Expr{location, NodeKind::ArrayLit, type}, elements{std::move(elements)} {}

    SAME_NODE_T_DEF(NodeKind::ArrayLit)
};

struct StructInstantiationLiteral : public Expr {
    std::string struct_name;
    std::vector<NodeId> field_values;

    explicit StructInstantiationLiteral(SrcLocationId location, std::string struct_name,
                                        std::vector<NodeId> field_values)
        : Expr{location, NodeKind::StructInstLit},
          struct_name{std::move(struct_name)},
          field_values{std::move(field_values)} {}

    SAME_NODE_T_DEF(NodeKind::StructInstLit)
};

struct ScopedExpr : public Expr {
    NodeId inner_expr;

    explicit ScopedExpr(SrcLocationId location, NodeId inner_expr)
        : Expr{location, NodeKind::ScopedExpr}, inner_expr{inner_expr} {}
};

struct BinaryOp : public Expr {
    enum class OpKind : uint8_t { add, sub, mul, div, pow, mod };

    NodeId lhs, rhs;

    explicit BinaryOp(SrcLocationId location, OpKind op, NodeId lhs, NodeId rhs)
        : Expr{location, NodeKind::BinOp, static_cast<uint8_t>(op)}, lhs{lhs}, rhs{rhs} {}

    OpKind op() const { return static_cast<OpKind>(node_storage()); }

    SAME_NODE_T_DEF(NodeKind::BinOp)
};

struct ExplicitCast : public Expr {
    NodeId inner;

    explicit ExplicitCast(SrcLocationId location, TypeId target_type, NodeId inner)
        : Expr{location, NodeKind::ExplCast, target_type}, inner{inner} {}

    SAME_NODE_T_DEF(NodeKind::ExplCast)
};

struct FunctionCall : public Expr {
    std::string fn_name;
    std::vector<NodeId> args;

    explicit FunctionCall(SrcLocationId location, std::string fn_name, std::vector<NodeId> args)
        : Expr{location, NodeKind::FnCall}, fn_name{std::move(fn_name)}, args{std::move(args)} {}

    SAME_NODE_T_DEF(NodeKind::FnCall)
};

struct DeclRefExpr : public Expr {
    NodeId decl;

    explicit DeclRefExpr(SrcLocationId location, NodeId decl)
        : Expr{location, NodeKind::DeclRef}, decl{decl} {}

    SAME_NODE_T_DEF(NodeKind::DeclRef)
};

// ===============
//   Statements
// ===============

struct CompoundStmt : public Stmt {
    std::vector<NodeId> body;

    explicit CompoundStmt(SrcLocationId location, std::vector<NodeId> body)
        : Stmt{location, NodeKind::Compound}, body{std::move(body)} {}

    SAME_NODE_T_DEF(NodeKind::Compound)
};

struct ScopedStmt : public Stmt {
    NodeId inner_stmt;

    explicit ScopedStmt(SrcLocationId location, NodeId inner_stmt)
        : Stmt{location, NodeKind::ScopedStmt}, inner_stmt{inner_stmt} {}
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

template <typename T>
concept CNodeTy = std::derived_from<T, NodeBase>;

template <typename T>
concept CStmtTy = std::derived_from<T, Stmt>;

template <typename T>
concept CExprTy = std::derived_from<T, Expr>;

template <typename T>
concept CDeclTy = std::derived_from<T, Decl>;

} // namespace stc::sir

#undef SAME_NODE_T_DEF
