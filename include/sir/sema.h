#pragma once

#include "sir/visitor.h"

namespace stc::sir {

class SIRSemaVisitor : public SIRVisitor<SIRSemaVisitor> {
public:
    explicit SIRSemaVisitor(SIRCtx& ctx)
        : SIRVisitor{ctx} {

        symbols.emplace_back();
    }

    bool success() const { return _success; }

    bool pre_visit_ptr(NodeBase* node);

    // clang-format off
    #define X(type, kind) STC_AST_VISITOR_DECL(void, type)
        #include "sir/node_defs/all_nodes.def"
    #undef X
    // clang-format on

private:
    bool _success = true;

    void error(const NodeBase& node, std::string_view msg, std::ostream& out = std::cerr);
    void warning(const NodeBase& node, std::string_view msg, std::ostream& out = std::cerr) const;

    TypeId expr_type(NodeId node_id) const;
    bool has_type(NodeId expr_id, TypeId type_id) const;

    bool check_sym_decl(const NodeBase& node, SymbolId sym_id);

    std::vector<std::unordered_set<SymbolId>> symbols;
    std::optional<TypeId> expected_ret_type;
};

static_assert(CSIRVisitorImpl<SIRSemaVisitor, void>);

} // namespace stc::sir
