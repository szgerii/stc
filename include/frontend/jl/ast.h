#pragma once

#include "base.h"
#include "common/src_info.h"
#include "common/utils.h"
#include "frontend/jl/module_pool.h"
#include "types/qualifier_pool.h"
#include "types/type_pool.h"

#define SAME_NODE_KIND_DEF(Kind)                                                                   \
    static bool same_node_kind(NodeKind kind) {                                                    \
        return kind == (Kind);                                                                     \
    }

#define ASSERT_NOT_NULL(id)                                                                        \
    assert(!((id).is_null()) && "trying to init non-nullable id field of AST node with null id")

// moved into a conditional macro so that release builds don't contain empty fors
// (which would cause compiler warnings)
#ifndef NDEBUG
    #define ASSERT_CONTAINS_NO_NULL(coll)                                                          \
        for (const auto& it : (coll)) {                                                            \
            ASSERT_NOT_NULL(it);                                                                   \
        }
#else
    #define ASSERT_CONTAINS_NO_NULL(coll)
#endif

namespace stc::jl {

enum class ScopeKind : uint8_t { Global, Hard, Soft };
enum class ScopeType : uint8_t { Global, Local };

// it's good to have this separate for AST nodes, which may or may not specify a scope (e.g. var
// decls), but not force checking for unspec in sema helpers, where it shouldn't ever be unspec
enum class MaybeScopeType : uint8_t { Global, Local, Unspec };

static_assert(static_cast<uint8_t>(MaybeScopeType::Global) ==
              static_cast<uint8_t>(ScopeType::Global));
static_assert(static_cast<uint8_t>(MaybeScopeType::Local) ==
              static_cast<uint8_t>(ScopeType::Local));

[[nodiscard]] STC_FORCE_INLINE constexpr ScopeType mst_to_st(MaybeScopeType mst) {
    if (mst == MaybeScopeType::Unspec)
        throw std::logic_error{
            "Trying to convert MaybeBindingType with value Unspec to a ScopeType"};

    assert(mst == MaybeScopeType::Global || mst == MaybeScopeType::Local);

    return static_cast<ScopeType>(static_cast<uint8_t>(mst));
}

[[nodiscard]] STC_FORCE_INLINE constexpr MaybeScopeType st_to_mst(ScopeType st) {
    return static_cast<MaybeScopeType>(static_cast<uint8_t>(st));
}

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

    FirstDecl,
    #define X_FIRST(type, kind) kind = FirstDecl,
        #include "frontend/jl/node_defs/decl.def"
    #undef X_FIRST
    LastDecl = FieldDecl,

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
        : location{location}, type{type}, _kind{kind}, _node_storage{node_storage} {
        ASSERT_NOT_NULL(location);
    }

    explicit Expr(SrcLocationId location, NodeKind kind, uint8_t node_storage = 0U)
        : Expr{location, kind, TypeId::null_id(), node_storage} {}

    NodeKind kind() const { return _kind; }
    uint8_t node_storage() const { return _node_storage; }

    static bool same_node_kind(NodeKind) { return true; }

    static Expr* safe_cast_to_base(void* node_ptr, NodeId node_id);
};

struct Stmt : public Expr {
    explicit Stmt(SrcLocationId location, NodeKind kind, uint8_t node_storage)
        : Expr{location, kind, node_storage} {}

    explicit Stmt(SrcLocationId location, NodeKind kind)
        : Expr{location, kind} {}

    static bool same_node_kind(NodeKind kind) {
        return NodeKind::FirstStmt <= kind && kind <= NodeKind::LastStmt;
    }
};

struct Decl : public Expr {
    SymbolId identifier;
    QualId qualifiers;

    explicit Decl(SrcLocationId location, NodeKind kind, SymbolId identifier, uint8_t node_storage,
                  TypeId type = TypeId::null_id(), QualId qualifiers = QualId::null_id())
        : Expr{location, kind, type, node_storage}, identifier{identifier}, qualifiers{qualifiers} {
        ASSERT_NOT_NULL(identifier);
    }

    explicit Decl(SrcLocationId location, NodeKind kind, SymbolId identifier,
                  TypeId type = TypeId::null_id(), QualId qualifiers = QualId::null_id())
        : Expr{location, kind, type}, identifier{identifier}, qualifiers{qualifiers} {
        ASSERT_NOT_NULL(identifier);
    }

    static bool same_node_kind(NodeKind kind) {
        return NodeKind::FirstDecl <= kind && kind <= NodeKind::LastDecl;
    }
};

// ================
//   Declarations
// ================

struct VarDecl : public Decl {
    TypeId annot_type;
    NodeId initializer;

    explicit VarDecl(SrcLocationId location, SymbolId identifier, TypeId annot_type,
                     MaybeScopeType scope, NodeId initializer = NodeId::null_id(),
                     bool is_builtin = false)
        : Decl{location, NodeKind::VarDecl, identifier,
               static_cast<uint8_t>(scope) | static_cast<uint8_t>(is_builtin << 7)},
          annot_type{annot_type},
          initializer{initializer} {}

    explicit VarDecl(SrcLocationId location, SymbolId identifier, TypeId annot_type,
                     ScopeType scope, NodeId initializer = NodeId::null_id(),
                     bool is_builtin = false)
        : VarDecl{location, identifier, annot_type, st_to_mst(scope), initializer, is_builtin} {}

    MaybeScopeType scope() const {
        return static_cast<MaybeScopeType>(0b01111111 & node_storage());
    }

    void set_scope(MaybeScopeType value) {
        _node_storage = (_node_storage & (1U << 7)) | static_cast<uint8_t>(value);
    }

    bool is_builtin() const { return static_cast<bool>(node_storage() >> 7); }
    void set_is_builtin(bool value) {
        if (value)
            _node_storage |= (1U << 7);
        else
            _node_storage &= ~(1U << 7);
    }

    SAME_NODE_KIND_DEF(NodeKind::VarDecl)
};

struct MethodDecl : public Decl {
    TypeId ret_type;
    NodeId body;
    std::vector<NodeId> param_decls;

    explicit MethodDecl(SrcLocationId location, SymbolId identifier, TypeId ret_type,
                        std::vector<NodeId> param_decls, NodeId body,
                        bool has_captured_syms = false)
        : Decl{location, NodeKind::MethodDecl, identifier, static_cast<uint8_t>(has_captured_syms)},
          ret_type{ret_type},
          body{body},
          param_decls{std::move(param_decls)} {

        ASSERT_NOT_NULL(body);
        ASSERT_CONTAINS_NO_NULL(param_decls);
    }

    SAME_NODE_KIND_DEF(NodeKind::MethodDecl)

    void set_has_captured_syms(bool value) { _node_storage = static_cast<uint8_t>(value); }
    bool has_captured_syms() const { return static_cast<bool>(node_storage()); }
};

struct FunctionDecl : public Decl {
    std::vector<NodeId> methods;

    explicit FunctionDecl(SrcLocationId location, SymbolId identifier, std::vector<NodeId> methods)
        : Decl{location, NodeKind::FnDecl, identifier}, methods{std::move(methods)} {

        ASSERT_CONTAINS_NO_NULL(methods);
    }

    SAME_NODE_KIND_DEF(NodeKind::FnDecl)
};

struct ParamDecl : public Decl {
    TypeId annot_type;
    NodeId default_initializer;

    explicit ParamDecl(SrcLocationId location, SymbolId identifier, TypeId annot_type,
                       bool is_kwarg = false, NodeId default_initializer = NodeId::null_id())
        : Decl{location, NodeKind::ParamDecl, identifier, static_cast<uint8_t>(is_kwarg)},
          annot_type{annot_type},
          default_initializer{default_initializer} {}

    bool is_kwarg() const { return static_cast<bool>(_node_storage); }

    SAME_NODE_KIND_DEF(NodeKind::ParamDecl)
};

// stores a reference to a Julia-side function
// should only be used with functions expected to be rooted for the lifetime of the transpiler
// (e.g. global functions of modules accessible from Main)
struct OpaqueFunction : public Decl {
    jl_function_t* jl_function;

    explicit OpaqueFunction(SrcLocationId location, SymbolId fn_name, jl_function_t* jl_function,
                            bool is_ctor = false)
        : Decl{location, NodeKind::OpaqFn, fn_name, static_cast<uint8_t>(is_ctor)},
          jl_function{jl_function} {

        assert(jl_function != nullptr &&
               "trying to store nullptr in OpaqueFunction's raw julia function pointer");
    }

    SymbolId fn_name() const { return identifier; }

    bool is_ctor() const { return static_cast<bool>(node_storage()); }
    void set_is_ctor(bool new_value) { _node_storage = static_cast<uint8_t>(new_value); }

    SAME_NODE_KIND_DEF(NodeKind::OpaqFn)
};

struct BuiltinFunction : public Decl {
    explicit BuiltinFunction(SrcLocationId location, SymbolId fn_name)
        : Decl{location, NodeKind::BuiltinFn, fn_name} {}

    SymbolId fn_name() const { return identifier; }

    SAME_NODE_KIND_DEF(NodeKind::BuiltinFn)
};

struct StructDecl : public Decl {
    std::vector<NodeId> field_decls;

    explicit StructDecl(SrcLocationId location, SymbolId identifier,
                        std::vector<NodeId> field_decls, bool is_mutable)
        : Decl{location, NodeKind::StructDecl, identifier, static_cast<uint8_t>(is_mutable)},
          field_decls{std::move(field_decls)} {

        ASSERT_CONTAINS_NO_NULL(field_decls);
    }

    bool is_mutable() const { return static_cast<bool>(node_storage()); }

    SAME_NODE_KIND_DEF(NodeKind::StructDecl)
};

struct FieldDecl : public Decl {
    explicit FieldDecl(SrcLocationId location, SymbolId identifier, TypeId type)
        : Decl{location, NodeKind::FieldDecl, identifier, type} {

        ASSERT_NOT_NULL(type);
    }

    SAME_NODE_KIND_DEF(NodeKind::FieldDecl)
};

// ===============
//   Expressions
// ===============

struct CompoundExpr : public Expr {
    std::vector<NodeId> body;

    explicit CompoundExpr(SrcLocationId location, std::vector<NodeId> body)
        : Expr{location, NodeKind::Compound}, body{std::move(body)} {

        ASSERT_CONTAINS_NO_NULL(body);
    }

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

// TODO: char literal, nothing literal?, missing type + literal?

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
    uint64_t hi, lo;

    explicit UInt128Literal(SrcLocationId location, uint64_t hi, uint64_t lo)
        : Expr{location, NodeKind::U128Lit}, hi{hi}, lo{lo} {}

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

struct ArrayLiteral : public Expr {
    std::vector<NodeId> members;

    explicit ArrayLiteral(SrcLocationId location, std::vector<NodeId> literals)
        : Expr{location, NodeKind::ArrLit}, members{std::move(literals)} {}

    SAME_NODE_KIND_DEF(NodeKind::ArrLit)
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
        : Expr{location, NodeKind::SymLit}, value{value} {

        ASSERT_NOT_NULL(value);
    }

    SAME_NODE_KIND_DEF(NodeKind::SymLit)
};

struct FieldAccess : public Expr {
    NodeId target;
    NodeId field_decl;
    NodeId target_type_decl = NodeId::null_id();

    explicit FieldAccess(SrcLocationId location, NodeId target, NodeId field_decl)
        : Expr{location, NodeKind::FieldAccess}, target{target}, field_decl{field_decl} {

        ASSERT_NOT_NULL(target);
        ASSERT_NOT_NULL(field_decl);
    }

    SAME_NODE_KIND_DEF(NodeKind::FieldAccess)
};

struct DotChain : public Expr {
    std::vector<NodeId> chain;
    NodeId resolved_expr = NodeId::null_id();

    explicit DotChain(SrcLocationId location, std::vector<NodeId> chain)
        : Expr{location, NodeKind::DotChain}, chain{std::move(chain)} {

        ASSERT_CONTAINS_NO_NULL(chain);
    }

    bool is_resolved() const { return !resolved_expr.is_null(); }

    SAME_NODE_KIND_DEF(NodeKind::DotChain)
};

struct NothingLiteral : public Expr {
    explicit NothingLiteral(SrcLocationId location)
        : Expr{location, NodeKind::NothingLit} {}

    SAME_NODE_KIND_DEF(NodeKind::NothingLit)
};

// FEATURE: parse OpaqueNode into actual literals when possible during sema

// stores raw memory values found in the Julia AST
// uses void* instead of jl_value_t* to avoid propagating julia.h inclusion beyond the parser
struct OpaqueNode : public Expr {
    SymbolId jl_type_name;
    void* jl_value;

    explicit OpaqueNode(SrcLocationId location, SymbolId jl_type_name, void* jl_value)
        : Expr{location, NodeKind::OpaqNode}, jl_type_name{jl_type_name}, jl_value{jl_value} {

        ASSERT_NOT_NULL(jl_type_name);
        assert(jl_value != nullptr &&
               "trying to store nullptr in OpaqueNode's raw julia memory pointer");
    }

    SAME_NODE_KIND_DEF(NodeKind::OpaqNode)
};

struct GlobalRef : public Expr {
    ModuleId module;
    SymbolId sym_name;

    explicit GlobalRef(SrcLocationId location, ModuleId module, SymbolId sym_name)
        : Expr{location, NodeKind::GlobalRef}, module{module}, sym_name{sym_name} {

        ASSERT_NOT_NULL(module);
        ASSERT_NOT_NULL(sym_name);
    }

    SAME_NODE_KIND_DEF(NodeKind::GlobalRef)
};

struct ImplicitCast : public Expr {
    NodeId target;

    explicit ImplicitCast(SrcLocationId location, NodeId target, TypeId type)
        : Expr{location, NodeKind::ImplCast, type}, target{target} {
        ASSERT_NOT_NULL(target);
        ASSERT_NOT_NULL(type);
    }

    SAME_NODE_KIND_DEF(NodeKind::ImplCast)
};

struct ExplicitCast : public Expr {
    NodeId target;

    explicit ExplicitCast(SrcLocationId location, NodeId target, TypeId type)
        : Expr{location, NodeKind::ExplCast, type}, target{target} {
        ASSERT_NOT_NULL(target);
        ASSERT_NOT_NULL(type);
    }

    SAME_NODE_KIND_DEF(NodeKind::ExplCast)
};

// DeclRefExpr-s have 2 states:
// unresolved (after parser): decl points to a SymbolLiteral
// resolved (after sema): decl points to a Decl or an OpaqueFunction
struct DeclRefExpr : public Expr {
    NodeId decl;

    explicit DeclRefExpr(SrcLocationId location, NodeId decl)
        : Expr{location, NodeKind::DeclRef}, decl{decl} {

        ASSERT_NOT_NULL(decl);
    }

    SAME_NODE_KIND_DEF(NodeKind::DeclRef)
};

struct Assignment : public Expr {
    NodeId target, value;

    explicit Assignment(SrcLocationId location, NodeId target, NodeId value,
                        bool is_implicit_decl = false)
        : Expr{location, NodeKind::Assignment, static_cast<uint8_t>(is_implicit_decl)},
          target{target},
          value{value} {

        ASSERT_NOT_NULL(target);
        ASSERT_NOT_NULL(value);
    }

    bool is_implicit_decl() const { return static_cast<bool>(node_storage()); }
    void set_is_implicit_decl(bool new_value) { _node_storage = static_cast<uint8_t>(new_value); }

    SAME_NODE_KIND_DEF(NodeKind::Assignment)
};

struct IndexerExpr : public Expr {
    NodeId target;
    std::vector<NodeId> indexers;

    explicit IndexerExpr(SrcLocationId location, NodeId target, std::vector<NodeId> indexers)
        : Expr{location, NodeKind::IdxExpr}, target{target}, indexers{std::move(indexers)} {}

    SAME_NODE_KIND_DEF(NodeKind::IdxExpr)
};

struct FunctionCall : public Expr {
    NodeId target_fn;
    std::vector<NodeId> args;

    explicit FunctionCall(SrcLocationId location, NodeId target_fn, std::vector<NodeId> args)
        : Expr{location, NodeKind::FnCall}, target_fn{target_fn}, args{std::move(args)} {

        ASSERT_NOT_NULL(target_fn);
        ASSERT_CONTAINS_NO_NULL(args);
    }

    explicit FunctionCall(SrcLocationId location, NodeId target_fn,
                          std::initializer_list<NodeId> args)
        : FunctionCall{location, target_fn, std::vector<NodeId>{args}} {}

    SAME_NODE_KIND_DEF(NodeKind::FnCall)
};

struct LogicalBinOp : public Expr {
    NodeId lhs, rhs;

    explicit LogicalBinOp(SrcLocationId location, NodeId lhs, NodeId rhs, bool is_land)
        : Expr{location, NodeKind::LogBinOp, static_cast<uint8_t>(is_land)}, lhs{lhs}, rhs{rhs} {
        ASSERT_NOT_NULL(lhs);
        ASSERT_NOT_NULL(rhs);
    }

    bool is_land() const { return static_cast<bool>(node_storage()); }
    bool is_lor() const { return !is_land(); }

    SAME_NODE_KIND_DEF(NodeKind::LogBinOp)
};

struct WhileExpr : public Expr {
    NodeId condition, body;

    explicit WhileExpr(SrcLocationId location, NodeId condition, NodeId body)
        : Expr{location, NodeKind::While}, condition{condition}, body{body} {

        ASSERT_NOT_NULL(condition);
        ASSERT_NOT_NULL(body);
    }

    SAME_NODE_KIND_DEF(NodeKind::While)
};

struct IfExpr : public Expr {
    NodeId condition, true_branch, false_branch;

    explicit IfExpr(SrcLocationId location, NodeId condition, NodeId true_branch,
                    NodeId false_branch = NodeId::null_id())
        : Expr{location, NodeKind::If},
          condition{condition},
          true_branch{true_branch},
          false_branch{false_branch} {

        ASSERT_NOT_NULL(condition);
        ASSERT_NOT_NULL(true_branch);
    }

    SAME_NODE_KIND_DEF(NodeKind::If)
};

// ==============
//   Statements
// ==============

struct ReturnStmt : public Stmt {
    NodeId inner;

    explicit ReturnStmt(SrcLocationId location, NodeId inner = NodeId::null_id())
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

#undef ASSERT_NOT_NULL
#undef SAME_NODE_KIND_DEF
