#pragma once

#include "ast/visitor.h"
#include "frontend/jl/ast.h"
#include "frontend/jl/context.h"
#include <type_traits>

namespace stc::jl {

// clang-format off
template <typename ImplTy, typename RetTy>
concept CJLVisitorImpl = requires (ImplTy v) {
    #define X(type, kind) { v.visit_##type(std::declval<type&>()) } -> std::same_as<RetTy>;
        #include "frontend/jl/node_defs/all_nodes.def"
    #undef X
};
// clang-format on

template <typename ImplTy, typename CtxTy = JLCtx, typename RetTy = void>
requires std::derived_from<CtxTy, JLCtx>
class JLVisitor : public ASTVisitor<ImplTy, CtxTy, RetTy> {
protected:
    using ASTVisitor<ImplTy, CtxTy, RetTy>::ASTVisitor;

public:
    RetTy dispatch(Expr* node) {
        // clang-format off
        switch (node->kind()) {

        #define X(type, kind)                                                                  \
            case (NodeKind::kind):                                                             \
                return this->impl_this()->visit_##type(this->template as<type>(node));

            #include "frontend/jl/node_defs/all_nodes.def"
        #undef X

            default:
                throw std::logic_error{"Missing NodeKind case in Julia ASTVisitor"};
        }
        // clang-format on
    }
};

} // namespace stc::jl
