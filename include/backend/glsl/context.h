#pragma once

#include "backend/glsl/types.h"
#include "sir/context.h"

namespace stc::glsl {

class GLSLCtx : public sir::SIRCtx {
public:
    using sir::SIRCtx::SIRCtx;

    GLSLTypes types;

    explicit GLSLCtx(const TargetInfo* target_info            = nullptr,
                     sir::NodeId::id_type node_arena_kb       = 128U,
                     SrcLocationId::id_type src_info_arena_kb = 128U,
                     TypeId::id_type type_arena_kb = 32U, SymbolId::id_type sym_arena_kb = 64U,
                     QualId::id_type qual_arena_kb = 16U)
        : sir::SIRCtx{{},           target_info,  node_arena_kb, src_info_arena_kb, type_arena_kb,
                      sym_arena_kb, qual_arena_kb},
          types{type_pool} {}

    explicit GLSLCtx(SIRCtx&& other)
        : SIRCtx{std::move(other)}, types{type_pool} {}

protected:
    template <typename T, typename U>
    explicit GLSLCtx(ASTCtx<T, U>&& other, sir::NodeId::id_type node_arena_kb)
        : SIRCtx{std::move(other), node_arena_kb}, types{type_pool} {}

public:
    template <typename T, typename U>
    [[nodiscard]] static GLSLCtx move_pools_from(ASTCtx<T, U>&& other,
                                                 sir::NodeId::id_type node_arena_kb = 128U) {
        return GLSLCtx{std::move(other), node_arena_kb};
    }
};

} // namespace stc::glsl
