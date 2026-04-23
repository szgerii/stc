#pragma once

#include "frontend/jl/scope.h"
#include "frontend/jl/visitor.h"
#include <functional>

namespace stc::jl {

class SymbolRes : public JLVisitor<SymbolRes, JLCtx, void> {
    enum class ScopeInferSrc : uint8_t { Access = 0, Assign, Decl };

    using ScopeInferTableEntry = std::pair<std::reference_wrapper<Expr>, ScopeInferSrc>;
    using ScopeInferTable      = std::unordered_map<SymbolId, ScopeInferTableEntry>;

    std::vector<JLScope>& scopes;
    ScopeInferTable scope_infer_table{};
    bool in_interactive_ctx;
    bool in_fn_call_target = false;
    bool _success          = true;

public:
    explicit SymbolRes(JLCtx& ctx, std::vector<JLScope>& scopes, bool in_interactive_ctx = false)
        : JLVisitor{ctx}, scopes{scopes}, in_interactive_ctx{in_interactive_ctx} {

        if (scopes.empty())
            throw std::logic_error{"Symbol resolution pass invoked on empty scope list"};

        if (!scopes.back().is_local())
            throw std::logic_error{"Symbol resolution pass invoked on non-local scope"};
    }

    bool try_register(SymbolId sym, Expr& node, ScopeInferSrc infer_src) {
        auto it = scope_infer_table.find(sym);
        bool has_stronger_src =
            it != scope_infer_table.end() &&
            static_cast<uint8_t>(infer_src) <= static_cast<uint8_t>(it->second.second);

        if (has_stronger_src)
            return false;

        scope_infer_table.insert_or_assign(sym, ScopeInferTableEntry{node, infer_src});
        return true;
    }

    [[nodiscard]] bool finalize();

    Decl* get_prev_decl(SymbolId sym) const;

    // clang-format off
    #define X(type, kind) STC_AST_VISITOR_DECL(void, type)
        #include "frontend/jl/node_defs/all_nodes.def"
    #undef X
    // clang-format on
};

} // namespace stc::jl
