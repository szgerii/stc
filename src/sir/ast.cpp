#include "sir/ast.h"

namespace stc::sir {

NodeBase* NodeBase::safe_cast_to_base(void* node_ptr, NodeId node_id) {
    uint8_t kind = node_id.kind_value();

    // clang-format off
    switch (static_cast<NodeKind>(kind)) {
        case NodeKind::NullKind:
                throw std::logic_error{"Trying to resolve SIR node with unknown kind value"};

        #define X(type, kind)                                                                              \
            case (NodeKind::kind):                                                                         \
                return static_cast<NodeBase*>(static_cast<type*>(node_ptr));

            #include "sir/node_defs/all_nodes.def"
        #undef X
    }
    // clang-format on

    throw std::logic_error{"Invalid node kind value in node id"};
}

} // namespace stc::sir
