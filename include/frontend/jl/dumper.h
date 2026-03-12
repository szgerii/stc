#pragma once

#include <iostream>

#include "frontend/jl/visitor.h"

namespace stc::jl {

class JLDumper final : public JLVisitor<JLDumper, const JLCtx, void> {
public:
    explicit JLDumper(const JLCtx& ctx, std::ostream& out)
        : JLVisitor{ctx}, out{out} {}

    void pre_visit_id(NodeId node);

    // clang-format off
    #define X(type, kind) STC_AST_VISITOR_DECL(void, type)
        #include "frontend/jl/node_defs/all_nodes.def"
    #undef X
    // clang-format on

private:
    std::ostream& out;
    size_t indent_level = 0U;

    std::string type_str(TypeId type_id) const;
    std::string indent() const;
    void inc_indent(size_t level = STC_DUMP_INDENT);
    void dec_indent(size_t level = STC_DUMP_INDENT);
};

static_assert(CJLVisitorImpl<JLDumper, void>);

} // namespace stc::jl
