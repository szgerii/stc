#pragma once

#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <tuple>
#include <vector>

#include "common/base.h"
#include "common/concepts.h"
#include "common/src_info.h"
#include "common/utils.h"
#include "ir/types.h"

#define SAME_NODE_T_DEF(Kind)                                                                      \
    static bool same_node_t(const NodeBase* node) {                                                \
        return node->kind() == (Kind);                                                             \
    }

namespace stc::ir {

// CLEANUP: go through structs and make use of _node_storage properly
// FEATURE: trailing objects pattern where applicable

struct NodeId : public StrongId<uint32_t> {
    using StrongId::StrongId;
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
        #include "node_defs/decl.def"
    #undef X_FIRST

    LastDecl = FieldDecl,
    FirstExpr,

    #define X_FIRST(type, kind) kind = FirstExpr,
        #include "node_defs/expr.def"
    #undef X_FIRST

    LastExpr = DeclRef,
    FirstStmt,

    #define X_FIRST(type, kind) kind = FirstStmt,
        #include "node_defs/stmt.def"
    #undef X_FIRST

    LastStmt = Return,

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
    std::optional<NodeId> initializer;

    explicit VarDecl(SrcLocationId location, std::string var_name, TypeId type,
                     std::optional<NodeId> initializer = std::nullopt)
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

    explicit FunctionDecl(SrcLocationId location, std::string fn_name, TypeId return_type,
                          std::vector<NodeId> param_decls)
        : Decl{location, NodeKind::FuncDecl, std::move(fn_name)},
          return_type{return_type},
          param_decls{std::move(param_decls)} {}

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

    TypeId type() const { return static_cast<TypeId>((_node_storage >> 8) & 0xFFFF); }
    uint8_t node_storage() const { return static_cast<uint8_t>(_node_storage & 0xFF); }

    static bool same_node_t(const NodeBase* node) {
        return node->kind() >= NodeKind::FirstExpr && node->kind() <= NodeKind::LastExpr;
    }

private:
    inline static uint32_t pack_to_u32(TypeId type, uint8_t node_storage) {
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

    explicit IntLiteral(SrcLocationId location, TypeId int_type, std::string data)
        : Expr{location, NodeKind::IntLit, int_type}, data{std::move(data)} {}

    SAME_NODE_T_DEF(NodeKind::IntLit)
};

struct FloatLiteral : public Expr {
    std::string data;

    explicit FloatLiteral(SrcLocationId location, TypeId float_type, std::string data)
        : Expr{location, NodeKind::FloatLit, float_type}, data{std::move(data)} {}

    SAME_NODE_T_DEF(NodeKind::FloatLit)
};

struct VectorLiteral : public Expr {
    std::vector<NodeId> components;

    explicit VectorLiteral(SrcLocationId location, TypeId vec_type, std::vector<NodeId> components)
        : Expr{location, NodeKind::VecLit, vec_type}, components{std::move(components)} {}

    SAME_NODE_T_DEF(NodeKind::VecLit)
};

// column-major storage
struct MatrixLiteral : public Expr {
    std::vector<NodeId> data;

    explicit MatrixLiteral(SrcLocationId location, TypeId mat_type, std::vector<NodeId> data)
        : Expr{location, NodeKind::MatLit, mat_type}, data{std::move(data)} {}

    SAME_NODE_T_DEF(NodeKind::MatLit)
};

struct ArrayLiteral : public Expr {
    std::vector<NodeId> elements;

    explicit ArrayLiteral(SrcLocationId location, TypeId arr_type, std::vector<NodeId> elements)
        : Expr{location, NodeKind::ArrayLit, arr_type}, elements{std::move(elements)} {}

    SAME_NODE_T_DEF(NodeKind::ArrayLit)
};

struct StructInstantiationLiteral : public Expr {
    std::vector<std::pair<std::string, NodeId>> field_values;

    explicit StructInstantiationLiteral(SrcLocationId location, TypeId struct_type,
                                        std::vector<std::pair<std::string, NodeId>> field_values)
        : Expr{location, NodeKind::StructInstLit, struct_type},
          field_values{std::move(field_values)} {}

    SAME_NODE_T_DEF(NodeKind::StructInstLit)
};

struct BinaryOp : public Expr {
    enum class OpKind : uint8_t { add, sub, mul, div, pow, mod };

    NodeId lhs, rhs;

    // CLEANUP: remove need for explicit type
    explicit BinaryOp(SrcLocationId location, TypeId type, OpKind op, NodeId lhs, NodeId rhs)
        : Expr{location, NodeKind::BinOp, type, static_cast<uint8_t>(op)}, lhs{lhs}, rhs{rhs} {}

    OpKind op() const { return static_cast<OpKind>(node_storage()); }

    SAME_NODE_T_DEF(NodeKind::BinOp)
};

struct ExplicitCast : public Expr {
    NodeId base;

    explicit ExplicitCast(SrcLocationId location, NodeId base, TypeId target_type)
        : Expr{location, NodeKind::ExplCast, target_type}, base{base} {}

    SAME_NODE_T_DEF(NodeKind::ExplCast)
};

struct DeclRefExpr : public Expr {
    NodeId decl;

    // TODO: remove need for explicit type
    explicit DeclRefExpr(SrcLocationId location, NodeId decl, TypeId decl_type)
        : Expr{location, NodeKind::DeclRef, decl_type}, decl{decl} {}

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
    NodeId ret_value_expr;

    explicit ReturnStmt(SrcLocationId location, NodeId ret_value_expr)
        : Stmt{location, NodeKind::Return}, ret_value_expr{ret_value_expr} {}

    SAME_NODE_T_DEF(NodeKind::Return)
};

// ==================
//   dyn_cast utils
// ==================

template <typename T>
concept CNodeTy = std::derived_from<T, NodeBase>;

template <typename T>
concept CStmtTy = std::derived_from<T, Stmt>;

template <typename T>
concept CDeclTy = std::derived_from<T, Decl>;

// requires part is just a safe-guard, all nodes should have it implemented
template <typename To, typename From>
concept CIsValidDynCast = CNodeTy<To> && CNodeTy<From> && requires (From* ptr) {
    { To::same_node_t(ptr) } -> std::same_as<bool>;
};

template <typename To, typename From>
requires CIsValidDynCast<To, From>
To* dyn_cast(From* ptr) {
    if (ptr == nullptr)
        return nullptr;

    if (To::same_node_t(ptr)) {
        return static_cast<To*>(ptr);
    }

    return nullptr;
}

template <typename To, typename From>
requires CIsValidDynCast<To, From>
std::unique_ptr<To> dyn_unique_cast(std::unique_ptr<From>&& ptr) {
    if (ptr == nullptr)
        return nullptr;

    if (auto* cast_ptr = dyn_cast<To, From>(ptr.get())) {
        std::ignore = ptr.release();
        return std::unique_ptr<To>{cast_ptr};
    }

    return nullptr;
}

} // namespace stc::ir

#undef SAME_NODE_T_DEF
