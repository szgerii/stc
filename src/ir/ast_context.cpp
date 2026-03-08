#include "ir/ast_context.h"

namespace stc::ir {

NodeBase* ASTCtx::get_node(NodeId id) const {
    return static_cast<NodeBase*>(node_arena.get_ptr(id));
}

} // namespace stc::ir