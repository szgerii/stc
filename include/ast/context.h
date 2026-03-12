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
// NodeKindTy is the kind (enum) type used to uniquely identify different node types

template <CStrongId NodeIdTy, typename NodeBaseTy, typename NodeKindTy>
requires requires {
    { NodeIdTy::null_id() } -> std::convertible_to<NodeIdTy>;
}
struct ASTCtx {
    using node_id_type   = NodeIdTy;
    using node_base_type = NodeBaseTy;
    using node_kind_type = NodeKindTy;

protected:
    BumpArena<typename NodeIdTy::id_type> node_arena;
    BumpArena<SrcLocationId::id_type> src_info_arena;
    BumpArena<types::TypeId::id_type> type_arena;

public:
    types::TypePool type_pool;
    SrcInfoManager src_info_manager;

    explicit ASTCtx(std::vector<types::BuiltinTD> type_builtins = {},
                    NodeIdTy::id_type node_arena_kb             = 128,
                    SrcLocationId::id_type src_info_arena_kb    = 128,
                    types::TypeId::id_type type_arena_kb        = 32)
        : node_arena{node_arena_kb},
          src_info_arena{src_info_arena_kb},
          type_arena{type_arena_kb},
          type_pool{type_arena, std::move(type_builtins)},
          src_info_manager{src_info_arena} {}

    // CLEANUP: enable move semantics (requires BumpArena support), share ASTCtx between passes
    ASTCtx(const ASTCtx&)                  = delete;
    ASTCtx(ASTCtx&&)                       = delete;
    ASTCtx& operator=(const ASTCtx&) const = delete;
    ASTCtx& operator=(ASTCtx&&) const      = delete;

    template <typename T, typename... Args>
    requires std::derived_from<T, NodeBaseTy>
    std::pair<NodeIdTy, T*> emplace_node(Args&&... args) {
        return node_arena.template emplace<T>(std::forward<Args>(args)...);
    }

    inline NodeBaseTy* get_node(NodeIdTy id) const {
        return static_cast<NodeBaseTy*>(node_arena.get_ptr(id));
    }

    template <typename T>
    requires CDynCastable<T, NodeBaseTy>
    inline T* get_dyn(NodeIdTy id) const {
        return dyn_cast<T>(get_node(id));
    }

    // helper, mainly meant for debug assertions
    template <typename T>
    requires CDynCastable<T, NodeBaseTy>
    bool isa(NodeIdTy id) const {
        if (id == NodeIdTy::null_id())
            return false;

        NodeBaseTy* node = get_dyn<T>(id);

        return dyn_cast<T>(node) != nullptr;
    }

    operator const types::TypePool&() const { return type_pool; }
    operator const SrcInfoManager&() const { return src_info_manager; }
};

}; // namespace stc
