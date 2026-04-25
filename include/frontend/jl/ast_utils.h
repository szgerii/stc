#pragma once

#include "frontend/jl/ast.h"
#include "frontend/jl/context.h"
#include "frontend/jl/rt/env.h"

namespace stc::jl {

inline std::string mod_chain_to_path(const std::vector<NodeId>& chain, const JLCtx& ctx,
                                     const SymbolPool& sym_pool, size_t first_n = 0) {
    static constexpr size_t MOD_NAME_LENGTH_GUESS = 5U;

    if (first_n == 0 || first_n > chain.size())
        first_n = chain.size();

    std::string result{};
    result.reserve(chain.size() * (MOD_NAME_LENGTH_GUESS + 1)); // + 1 for dots

    for (size_t i = 0; i < first_n; i++) {
        const auto* sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(chain[i]);

        if (sym_lit == nullptr) {
            if (const auto* dre = ctx.get_and_dyn_cast<DeclRefExpr>(chain[i]))
                sym_lit = ctx.get_and_dyn_cast<SymbolLiteral>(dre->decl);

            if (sym_lit == nullptr)
                throw std::logic_error{"invalid node kind in module lookup chain"};
        }

        result += sym_pool.get_symbol(sym_lit->value);

        if (i + 1 != first_n)
            result += '.';
    }

    return result;
}

inline std::string mod_chain_to_path(const std::vector<NodeId>& chain, const JLCtx& ctx,
                                     size_t first_n = 0) {

    return mod_chain_to_path(chain, ctx, ctx.sym_pool, first_n);
}

enum class SwizzleSet : uint8_t { Invalid, XYZW, RGBA, STPQ };

inline constexpr uint8_t INVALID_SWIZZLE = 0xFF;

[[nodiscard]] STC_FORCE_INLINE std::pair<uint8_t, SwizzleSet> parse_swizzle_component(char c) {
    // clang-format off
    switch (c) {
        case 'x': return {0b00, SwizzleSet::XYZW};
        case 'y': return {0b01, SwizzleSet::XYZW};
        case 'z': return {0b10, SwizzleSet::XYZW};
        case 'w': return {0b11, SwizzleSet::XYZW};
        
        case 'r': return {0b00, SwizzleSet::RGBA};
        case 'g': return {0b01, SwizzleSet::RGBA};
        case 'b': return {0b10, SwizzleSet::RGBA};
        case 'a': return {0b11, SwizzleSet::RGBA};
        
        case 's': return {0b00, SwizzleSet::STPQ};
        case 't': return {0b01, SwizzleSet::STPQ};
        case 'p': return {0b10, SwizzleSet::STPQ};
        case 'q': return {0b11, SwizzleSet::STPQ};

        [[unlikely]]
        default:
            return {INVALID_SWIZZLE, SwizzleSet::Invalid};
    }
    // clang-format on
}

} // namespace stc::jl
