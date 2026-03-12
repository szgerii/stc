#pragma once

#include <iostream>

#include "sir/visitor.h"

namespace stc::sir {

class SIRDumper final : public SIRVisitor<SIRDumper, const SIRCtx, void> {
public:
    explicit SIRDumper(const SIRCtx& ctx, std::ostream& out)
        : SIRVisitor{ctx}, out{out} {}

    void pre_visit_id(NodeId node);

    // clang-format off
    #define X(type, kind) STC_AST_VISITOR_DECL(void, type)
        #include "sir/node_defs/all_nodes.def"
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

static_assert(CSIRVisitorImpl<SIRDumper, void>);

} // namespace stc::sir
