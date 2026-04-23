#pragma once

#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <tuple>
#include <vector>

#include "base.h"
#include "common/src_info.h"
#include "common/utils.h"
#include "types/qualifier_pool.h"
#include "types/type_pool.h"

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
    SymbolId identifier;

    static_assert(std::same_as<QualId::id_type, uint16_t>);

    explicit Decl(SrcLocationId location, NodeKind kind, SymbolId identifier, QualId qualifiers,
                  uint8_t node_storage = 0U)
        : NodeBase{location, kind, (static_cast<uint32_t>(qualifiers) << 8) | node_storage},
          identifier{identifier} {}

    Decl(const Decl&)                = default;
    Decl& operator=(const Decl&)     = default;
    Decl(Decl&&) noexcept            = default;
    Decl& operator=(Decl&&) noexcept = default;

    QualId qualifiers() const { return QualId{static_cast<uint16_t>(node_storage() >> 8)}; }

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
                     QualId qualifiers = QualId::null_id(), NodeId initializer = NodeId::null_id())
        : Decl{location, NodeKind::VarDecl, var_name, qualifiers},
          type{type},
          initializer{initializer} {}

    SAME_NODE_KIND_DEF(NodeKind::VarDecl)
};

struct FunctionDecl : public Decl {
    TypeId return_type;
    std::vector<NodeId> param_decls;
    NodeId body;

    explicit FunctionDecl(SrcLocationId location, SymbolId fn_name, TypeId return_type,
                          std::vector<NodeId> param_decls, QualId qualifiers = QualId::null_id(),
                          NodeId body = NodeId::null_id())
        : Decl{location, NodeKind::FuncDecl, fn_name, qualifiers},
          return_type{return_type},
          param_decls{std::move(param_decls)},
          body{body} {}

    SAME_NODE_KIND_DEF(NodeKind::FuncDecl)
};

struct ParamDecl : public Decl {
    TypeId param_type;

    explicit ParamDecl(SrcLocationId location, SymbolId param_name, TypeId type,
                       QualId qualifiers = QualId::null_id())
        : Decl{location, NodeKind::ParamDecl, param_name, qualifiers}, param_type{type} {}

    SAME_NODE_KIND_DEF(NodeKind::ParamDecl)
};

struct StructDecl : public Decl {
    std::vector<NodeId> field_decls;

    explicit StructDecl(SrcLocationId location, SymbolId struct_name,
                        std::vector<NodeId> field_decls)
        : Decl{location, NodeKind::StructDecl, struct_name, QualId::null_id()},
          field_decls{std::move(field_decls)} {}

    SAME_NODE_KIND_DEF(NodeKind::StructDecl)
};

struct FieldDecl : public Decl {
    TypeId field_type;

    explicit FieldDecl(SrcLocationId location, SymbolId field_name, TypeId field_type,
                       QualId qualifiers = QualId::null_id())
        : Decl{location, NodeKind::FieldDecl, field_name, qualifiers}, field_type{field_type} {}

    SAME_NODE_KIND_DEF(NodeKind::FieldDecl)
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

    void set_type(TypeId id) { _node_storage = 0x00FFFFFF & pack_to_u32(id, node_storage()); }

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
    std::string value;

    explicit IntLiteral(SrcLocationId location, TypeId type, std::string data)
        : Expr{location, NodeKind::IntLit, type}, value{std::move(data)} {}

    SAME_NODE_KIND_DEF(NodeKind::IntLit)
};

struct FloatLiteral : public Expr {
    std::string value;

    explicit FloatLiteral(SrcLocationId location, TypeId type, std::string data)
        : Expr{location, NodeKind::FloatLit, type}, value{std::move(data)} {}

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

// unfortunately 4 + 4^2 + 4^3 + 4^4 = 340 > 256
// so mathematically, this cannot fit into the already reserved 8 bits (node_storage)
// for alignment reasons, theres not much use utilizing node_storage + a uint8_t vs. having a single
// local uint16_t
struct SwizzleLiteral : public Expr {
    uint16_t _count : 3; // allows invalid state with count == 0 (INVARIANT: count <= 4)

    // for all i in [count+1 : 4] : compi == 0
    uint16_t _comp1    : 2; // 0 - x, 1 - y, 2 - z, 3 - w
    uint16_t _comp2    : 2;
    uint16_t _comp3    : 2;
    uint16_t _comp4    : 2;
    uint16_t _reserved : 5;

    // CLEANUP: separate swizzle type?
    explicit SwizzleLiteral(SrcLocationId location, uint8_t count, uint8_t comp1 = 0U,
                            uint8_t comp2 = 0U, uint8_t comp3 = 0U, uint8_t comp4 = 0U)
        : Expr{location, NodeKind::SwizzleLit, TypeId::void_id()},
          _count{static_cast<uint16_t>(count & 0x07)},
          _comp1{static_cast<uint16_t>(comp1 & 0x03)},
          _comp2{static_cast<uint16_t>(comp2 & 0x03)},
          _comp3{static_cast<uint16_t>(comp3 & 0x03)},
          _comp4{static_cast<uint16_t>(comp4 & 0x03)},
          _reserved{0U} {

        assert(count <= 4 && "Trying to create SwizzleLiteral with more than 4 components");
        assert((count == 4 || comp4 == 0U) &&
               "Trying to create SwizzleLiteral with less than 4 components, but non-zero comp4");
        assert((count >= 3 || comp3 == 0U) &&
               "Trying to create SwizzleLiteral with less than 3 components, but non-zero comp3");
        assert((count >= 2 || comp2 == 0U) &&
               "Trying to create SwizzleLiteral with less than 2 components, but non-zero comp2");
        assert((count >= 1 || comp1 == 0U) &&
               "Trying to create SwizzleLiteral with less than 1 components, but non-zero comp1");
    }

    uint8_t count() const { return _count; }
    uint8_t comp1() const { return _comp1; }
    uint8_t comp2() const { return _comp2; }
    uint8_t comp3() const { return _comp3; }
    uint8_t comp4() const { return _comp4; }

    SAME_NODE_KIND_DEF(NodeKind::SwizzleLit)
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

struct FieldAccess : public Expr {
    NodeId target;
    NodeId field_decl;

    explicit FieldAccess(SrcLocationId location, NodeId target, NodeId field_decl)
        : Expr{location, NodeKind::FieldAccess}, target{target}, field_decl{field_decl} {}

    SAME_NODE_KIND_DEF(NodeKind::FieldAccess)
};

struct ScopedExpr : public Expr {
    NodeId inner;

    explicit ScopedExpr(SrcLocationId location, NodeId inner)
        : Expr{location, NodeKind::ScopedExpr}, inner{inner} {}

    SAME_NODE_KIND_DEF(NodeKind::ScopedExpr)
};

struct Assignment : public Expr {
    NodeId target, value;

    explicit Assignment(SrcLocationId location, NodeId target, NodeId value)
        : Expr{location, NodeKind::Assignment}, target{target}, value{value} {}

    SAME_NODE_KIND_DEF(NodeKind::Assignment)
};

struct UnaryOp : public Expr {
    enum class OpKind : uint8_t { plus, minus, lneg, bneg };

    NodeId target;

    explicit UnaryOp(SrcLocationId location, OpKind op, NodeId target)
        : Expr{location, NodeKind::UnOp, static_cast<uint8_t>(op)}, target{target} {}

    OpKind op() const { return static_cast<OpKind>(node_storage()); }

    SAME_NODE_KIND_DEF(NodeKind::UnOp)
};

struct BinaryOp : public Expr {
    // clang-format off
    enum class OpKind : uint8_t {
        add,  sub, mul, div, pow, mod,
        eq,   neq, lt,  leq, gt,  geq,
        land, lor, lxor,
        band, bor, bxor
    };
    // clang-format on

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

struct IndexerExpr : public Expr {
    NodeId target_arr;
    NodeId indexer;

    explicit IndexerExpr(SrcLocationId location, NodeId target_arr, NodeId indexer)
        : Expr{location, NodeKind::ArrMem}, target_arr{target_arr}, indexer{indexer} {}

    SAME_NODE_KIND_DEF(NodeKind::ArrMem)
};

struct ConstructorCall : public Expr {
    std::vector<NodeId> args;

    explicit ConstructorCall(SrcLocationId location, TypeId type, std::vector<NodeId> args)
        : Expr{location, NodeKind::CtorCall, type}, args{std::move(args)} {}

    SAME_NODE_KIND_DEF(NodeKind::CtorCall)
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

    explicit DeclRefExpr(SrcLocationId location, NodeId decl, TypeId type)
        : Expr{location, NodeKind::DeclRef, type}, decl{decl} {}

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
    NodeId condition;
    NodeId true_block;
    NodeId false_block;

    explicit IfStmt(SrcLocationId location, NodeId condition, NodeId true_block, NodeId false_block)
        : Stmt{location, NodeKind::If},
          condition{condition},
          true_block{true_block},
          false_block{false_block} {}

    SAME_NODE_KIND_DEF(NodeKind::If)
};

struct WhileStmt : public Stmt {
    NodeId condition;
    NodeId body;

    explicit WhileStmt(SrcLocationId location, NodeId condition, NodeId body)
        : Stmt{location, NodeKind::While}, condition{condition}, body{body} {}

    SAME_NODE_KIND_DEF(NodeKind::While)
};

struct ReturnStmt : public Stmt {
    NodeId inner;

    explicit ReturnStmt(SrcLocationId location, NodeId inner)
        : Stmt{location, NodeKind::Return}, inner{inner} {}

    explicit ReturnStmt(SrcLocationId location)
        : ReturnStmt{location, NodeId::null_id()} {}

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
