#pragma once

#include "ast/context.h"
#include "sir/ast.h"

namespace stc::sir {

struct SIRCtx : public ASTCtx<NodeId, NodeBase, NodeKind> {
public:
    using ASTCtx::ASTCtx;

    inline Decl* get_decl(NodeId id) const { return get_dyn<Decl>(id); }
    inline Stmt* get_stmt(NodeId id) const { return get_dyn<Stmt>(id); }
    inline Expr* get_expr(NodeId id) const { return get_dyn<Expr>(id); }
};

}; // namespace stc::sir
