#pragma once

#include "common/bump_arena.h"
#include "ir/ast.h"
#include "ir/type_pool.h"

namespace stc::ir {

struct ASTCtx {
private:
    BumpArena<NodeId::id_type> node_arena;            // currently: BumpArena32
    BumpArena<SrcLocationId::id_type> src_info_arena; // currently: BumpArena32
    BumpArena<TypeId::id_type> type_arena;            // currently: BumpArena16

public:
    TypePool type_pool;
    SrcInfoManager src_info_manager;

    explicit ASTCtx()
        : node_arena{128 * 1024},
          src_info_arena{128 * 1024},
          type_arena{32 * 1024},
          type_pool{type_arena},
          src_info_manager{src_info_arena} {}

    template <CNodeTy T, typename... Args>
    std::pair<NodeId, T*> alloc_node(Args&&... args) {
        return node_arena.emplace<T>(std::forward<Args>(args)...);
    }

    NodeBase* get_node(NodeId id) const;
};

}; // namespace stc::ir
