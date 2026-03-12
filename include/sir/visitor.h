#pragma once

#include "ast/visitor.h"
#include "sir/ast.h"
#include "sir/context.h"
#include <type_traits>

namespace stc::sir {

// clang-format off
template <typename ImplTy, typename RetTy>
concept CSIRVisitorImpl = requires (ImplTy v) {
    #define X(type, kind) { v.visit_##type(std::declval<type&>()) } -> std::same_as<RetTy>;
        #include "sir/node_defs/all_nodes.def"
    #undef X
};
// clang-format on

template <typename ImplTy, typename CtxTy = SIRCtx, typename RetTy = void>
requires std::derived_from<CtxTy, SIRCtx>
class SIRVisitor : public ASTVisitor<ImplTy, CtxTy, RetTy> {
protected:
    using ASTVisitor<ImplTy, CtxTy, RetTy>::ASTVisitor;

public:
    RetTy dispatch(NodeBase* node) {
        // clang-format off
        switch (node->kind()) {

        #define X(type, kind)                                                                  \
            case (NodeKind::kind):                                                             \
                return this->impl_this()->visit_##type(this->template as<type>(node));

            #include "sir/node_defs/all_nodes.def"
        #undef X

            default:
                throw std::logic_error{"Missing NodeKind case in SIR ASTVisitor"};
        }
        // clang-format on
    }
};

} // namespace stc::sir
