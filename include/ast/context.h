#pragma once

#include "common/bump_arena.h"
#include "common/dyn_cast.h"
#include "common/src_info.h"
#include "types/type_pool.h"

namespace stc {

// base AST context class

// encapsulates data needed for any sort of handling of an AST, regardless of its type or
// implementation

// NodeIdTy is the size type used to store arena indices/offsets when referring to nodes
// NodeBaseTy is the base class from which all AST types are derived
// Derived should be provided by classes deriving from this, so that partial movability can be
// ensured automatically at compile-time through concepts

template <CNullableSplitId NodeIdTy, typename NodeBaseTy>
requires requires (NodeBaseTy node, NodeIdTy id, void* void_ptr) {
    typename NodeBaseTy::kind_type;
    { node.kind() } -> std::same_as<typename NodeBaseTy::kind_type>;
    { NodeBaseTy::safe_cast_to_base(void_ptr, id) } -> std::same_as<NodeBaseTy*>;
} && CEnumOf<typename NodeBaseTy::kind_type, typename NodeIdTy::kind_type>
class ASTCtx {
public:
    using node_id_type   = NodeIdTy;
    using node_base_type = NodeBaseTy;
    using node_kind_type = NodeBaseTy::kind_type;

protected:
    BumpArena<typename NodeIdTy::id_type> node_arena;

public:
    types::TypePool type_pool;
    SrcInfoPool src_info_pool;
    SymbolPool sym_pool;

    explicit ASTCtx(std::vector<types::BuiltinTD> type_builtins = {},
                    NodeIdTy::id_type node_arena_kb             = 128U,
                    SrcLocationId::id_type src_info_arena_kb    = 128U,
                    types::TypeId::id_type type_arena_kb        = 32U,
                    SymbolId::id_type sym_arena_kb              = 64U)
        : node_arena{node_arena_kb * 1024U},
          type_pool{static_cast<types::TypeId::id_type>(type_arena_kb * 1024U),
                    std::move(type_builtins)},
          src_info_pool{src_info_arena_kb * 1024U},
          sym_pool{sym_arena_kb * 1024U} {}

    ASTCtx(const ASTCtx&)                  = delete;
    ASTCtx& operator=(const ASTCtx&) const = delete;
    ASTCtx(ASTCtx&&) noexcept              = default;
    ASTCtx& operator=(ASTCtx&&) noexcept   = default;

protected:
    // "partial" move ctor, moving the type and src info pools, and empty initializing node arena
    // protected to allow derived classes to use as a starting point, but publicly only accessible
    // from the factory function move_pools_from
    template <typename T, typename U>
    explicit ASTCtx(ASTCtx<T, U>&& other, NodeIdTy::id_type node_arena_kb)
        : node_arena{node_arena_kb * 1024U},
          type_pool{std::move(other.type_pool)},
          src_info_pool{std::move(other.src_info_pool)},
          sym_pool{std::move(other.sym_pool)} {}

public:
    template <typename T, typename U>
    [[nodiscard]] static ASTCtx move_pools_from(ASTCtx<T, U>&& other,
                                                NodeIdTy::id_type node_arena_kb = 128U) {
        return ASTCtx{std::move(other), node_arena_kb};
    }

    template <typename T, typename... Args>
    requires std::derived_from<T, NodeBaseTy> && requires (T t) {
        { t.kind() } -> std::same_as<node_kind_type>;
    }
    [[nodiscard]] std::pair<NodeIdTy, T*> emplace_node(Args&&... args) {
        auto [offset, ptr] = node_arena.template emplace<T>(std::forward<Args>(args)...);

        if ((offset & NodeIdTy::KIND_MASK) != 0)
            throw std::overflow_error{"AST node id type exhausted"};

        return {NodeIdTy{offset, static_cast<NodeIdTy::kind_type>(ptr->kind())}, ptr};
    }

    [[nodiscard]] NodeBaseTy* get_node(NodeIdTy id) const {
        if (id == NodeIdTy::null_id())
            return nullptr;

        return NodeBaseTy::safe_cast_to_base(node_arena.get_ptr(id.id_value()), id);
    }

    template <typename T>
    requires CDynCastable<T, NodeBaseTy, node_kind_type>
    [[nodiscard]] T* get_and_dyn_cast(NodeIdTy id) const {
        // below is essentially the same as: return dyn_cast<T*>(get_node(id));
        // but skips the round-trip to NodeBaseTy* for non-castable cases

        if (!isa<T>(id))
            return nullptr;

        return static_cast<T*>(get_node(id));
    }

    template <typename T>
    requires CDynCastable<T, NodeBaseTy, node_kind_type>
    bool isa(NodeIdTy id) const {
        if (id == NodeIdTy::null_id())
            return false;

        return T::same_node_kind(static_cast<node_kind_type>(id.kind_value()));
    }

    [[nodiscard]] std::string_view get_sym(SymbolId sym_id) const {
        auto result = sym_pool.get_symbol_maybe(sym_id);

        if (!result.has_value())
            throw std::logic_error{"symbol id not found in current context state"};

        return *result;
    }

    operator const types::TypePool&() const { return type_pool; }
    operator const SrcInfoPool&() const { return src_info_pool; }
    operator const SymbolPool&() const { return sym_pool; }
};

}; // namespace stc
