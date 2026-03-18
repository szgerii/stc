#include "frontend/jl/ast.h"

namespace stc::jl {

Expr* Expr::safe_cast_to_base(void* node_ptr, NodeId node_id) {
    uint8_t kind = node_id.kind_value();

    // clang-format off
    switch (static_cast<NodeKind>(kind)) {
        case NodeKind::InvalidKind:
            throw std::logic_error{"Trying to resolve jl AST node with unknown kind value"};

        #define X(type, kind)                                                                              \
            case (NodeKind::kind):                                                                         \
                return static_cast<Expr*>(static_cast<type*>(node_ptr)); // NOLINT(bugprone-macro-parentheses)

            #include "frontend/jl/node_defs/all_nodes.def"
        #undef X
    }
    // clang-format on

    throw std::logic_error{"Invalid node kind value in node id"};
}

} // namespace stc::jl