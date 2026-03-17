#pragma once

#include "sir/context.h"

namespace stc::glsl {

using namespace stc::types;

class GLSLCtx : public sir::SIRCtx {
public:
    // clang-format off
    #define X(type) TypeId gl_##type##_t() const { return _gl_##type##_t; }
        #include "backend/glsl/node_defs/scalars.def"
    #undef X
    // clang-format on

    using sir::SIRCtx::SIRCtx;

    explicit GLSLCtx(sir::NodeId::id_type node_arena_kb       = 128U,
                     SrcLocationId::id_type src_info_arena_kb = 128U,
                     TypeId::id_type type_arena_kb            = 32U)
        : sir::SIRCtx{{}, node_arena_kb, src_info_arena_kb, type_arena_kb} {
        init_gl_types();
    }

    explicit GLSLCtx(SIRCtx&& other)
        : SIRCtx{std::move(other)} {
        init_gl_types();
    }

protected:
    template <typename T, typename U>
    explicit GLSLCtx(ASTCtx<T, U>&& other, sir::NodeId::id_type node_arena_kb)
        : SIRCtx{std::move(other), node_arena_kb} {
        init_gl_types();
    }

public:
    template <typename T, typename U>
    [[nodiscard]] static GLSLCtx move_pools_from(ASTCtx<T, U>&& other,
                                                 sir::NodeId::id_type node_arena_kb = 128U) {
        return GLSLCtx{std::move(other), node_arena_kb};
    }

    [[nodiscard]] TypeId gl_TvecN_t(TypeId T, uint32_t N) { return type_pool.vector_td(T, N); }

    [[nodiscard]] TypeId gl_vec_t(uint32_t n) { return gl_TvecN_t(gl_float_t(), n); }
    [[nodiscard]] TypeId gl_dvec_t(uint32_t n) { return gl_TvecN_t(gl_double_t(), n); }

    [[nodiscard]] TypeId gl_TmatNxM_t(TypeId T, uint32_t N, uint32_t M) {
        return type_pool.matrix_td(gl_TvecN_t(T, N), M);
    }

    [[nodiscard]] TypeId gl_mat_t(uint32_t n, uint32_t m) {
        return gl_TmatNxM_t(gl_float_t(), n, m);
    }

    [[nodiscard]] TypeId gl_dmat_t(uint32_t n, uint32_t m) {
        return gl_TmatNxM_t(gl_double_t(), n, m);
    }

private:
    // ensures all glsl types are in the pool and sets up _gl_type members
    // for already initialized types, this is a no-op
    void init_gl_types();

    // clang-format off
    #define X(type) TypeId _gl_##type##_t = TypeId::null_id();
        #include "backend/glsl/node_defs/scalars.def"
    #undef X
    // clang-format on
};

} // namespace stc::glsl
