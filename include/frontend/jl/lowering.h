#pragma once

#include "frontend/jl/visitor.h"
#include "sir/context.h"

namespace stc::jl {

class JLLoweringVisitor final : public JLVisitor<JLLoweringVisitor, const JLCtx, sir::NodeId> {
    using SIRNodeId = sir::NodeId;

public:
    explicit JLLoweringVisitor(const JLCtx& ctx)
        : JLVisitor{ctx}, sir_ctx{} {}

    SIRNodeId visit_default_case();

    // clang-format off
    #define X(type, kind) STC_AST_VISITOR_DECL(SIRNodeId, type)
        #include "frontend/jl/node_defs/all_nodes.def"
    #undef X
    // clang-format on

private:
    sir::SIRCtx sir_ctx;
    bool success = true;

    template <typename T, typename... Args>
    SIRNodeId emplace_node(Args&&... args) {
        return sir_ctx.emplace_node<T>(std::forward<Args>(args)...).first;
    }

    SIRNodeId fail(std::string_view msg);
    SIRNodeId visit_and_check(NodeId id);

    // skips id-lookup roundtrip for nodes that have already been looked up
    SIRNodeId visit_ptr(Expr* node);
};

} // namespace stc::jl