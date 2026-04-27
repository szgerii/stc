#pragma once

#include "types/type_pool.h"

namespace stc::glsl {

using namespace stc::types;

#define STC_GL_DECL_VECS(prefix)                                                                   \
    const TypeId gl_##prefix##vec2, gl_##prefix##vec3, gl_##prefix##vec4;

#define STC_GL_DECL_MATS(prefix)                                                                   \
    const TypeId gl_##prefix##mat2x2, gl_##prefix##mat2x3, gl_##prefix##mat2x4, gl_##prefix##mat2; \
    const TypeId gl_##prefix##mat3x2, gl_##prefix##mat3x3, gl_##prefix##mat3x4, gl_##prefix##mat3; \
    const TypeId gl_##prefix##mat4x2, gl_##prefix##mat4x3, gl_##prefix##mat4x4, gl_##prefix##mat4;

// clang-format off
#define STC_GL_INIT_VECS(el_type, prefix)                                                          \
    gl_##prefix##vec2{type_pool.vector_td((el_type), 2)},                                          \
    gl_##prefix##vec3{type_pool.vector_td((el_type), 3)},                                          \
    gl_##prefix##vec4{type_pool.vector_td((el_type), 4)}

#define STC_GL_INIT_MAT_N(prefix, n)                                                               \
    gl_##prefix##mat##n##x2{type_pool.matrix_td(gl_##prefix##vec2, (n))},                          \
    gl_##prefix##mat##n##x3{type_pool.matrix_td(gl_##prefix##vec3, (n))},                          \
    gl_##prefix##mat##n##x4{type_pool.matrix_td(gl_##prefix##vec4, (n))},                          \
    gl_##prefix##mat##n{gl_##prefix##mat##n##x##n}
// clang-format on

#define STC_GL_INIT_MATS(prefix)                                                                   \
    STC_GL_INIT_MAT_N(prefix, 2), STC_GL_INIT_MAT_N(prefix, 3), STC_GL_INIT_MAT_N(prefix, 4)

class GLSLTypes {
public:
    TypePool& type_pool;

    explicit GLSLTypes(TypePool& type_pool)
        : type_pool{type_pool},
          gl_void{type_pool.void_td()},
          gl_bool{type_pool.bool_td()},
          gl_int{type_pool.int_td(32, true)},
          gl_uint{type_pool.int_td(32, false)},
          gl_float{type_pool.float_td(32)},
          gl_double{type_pool.float_td(64)},
          STC_GL_INIT_VECS(gl_float, ),
          STC_GL_INIT_VECS(gl_double, d),
          STC_GL_INIT_VECS(gl_bool, b),
          STC_GL_INIT_VECS(gl_int, i),
          STC_GL_INIT_VECS(gl_uint, u),
          STC_GL_INIT_MATS(),
          STC_GL_INIT_MATS(d) {}

    const TypeId gl_void, gl_bool, gl_int, gl_uint, gl_float, gl_double;

    STC_GL_DECL_VECS()
    STC_GL_DECL_VECS(d)
    STC_GL_DECL_VECS(b)
    STC_GL_DECL_VECS(i)
    STC_GL_DECL_VECS(u)

    STC_GL_DECL_MATS()
    STC_GL_DECL_MATS(d)

    [[nodiscard]] TypeId gl_TvecN(TypeId T, uint32_t N) { return type_pool.vector_td(T, N); }
    [[nodiscard]] TypeId gl_vecN(uint32_t n) { return gl_TvecN(gl_float, n); }
    [[nodiscard]] TypeId gl_dvecN(uint32_t n) { return gl_TvecN(gl_double, n); }

    [[nodiscard]] TypeId gl_TmatNxM(TypeId T, uint32_t N, uint32_t M) {
        return type_pool.matrix_td(gl_TvecN(T, M), N);
    }
    [[nodiscard]] TypeId gl_matNxM(uint32_t n, uint32_t m) { return gl_TmatNxM(gl_float, n, m); }
    [[nodiscard]] TypeId gl_dmatNxM(uint32_t n, uint32_t m) { return gl_TmatNxM(gl_double, n, m); }

    [[nodiscard]] TypeId gl_array(TypeId el_type, uint32_t length) {
        return type_pool.array_td(el_type, length);
    }

    [[nodiscard]] bool is_gl_scalar_type(TypeId T) const {
        return T == gl_float || T == gl_double || T == gl_bool || T == gl_int || T == gl_uint;
    }

    [[nodiscard]] bool is_gl_vec_type(TypeId T) const {
        if (T.is_null())
            return false;

        const auto& td = type_pool.get_td(T);

        if (!td.is_vector())
            return false;

        VectorTD vec_td = td.as<VectorTD>();
        TypeId el_type  = vec_td.component_type_id;

        return 2 <= vec_td.component_count && vec_td.component_count <= 4 &&
               is_gl_scalar_type(el_type);
    }

    [[nodiscard]] bool is_gl_mat_type(TypeId T) const {
        if (T.is_null())
            return false;

        const auto& td = type_pool.get_td(T);

        if (!td.is_matrix())
            return false;

        MatrixTD mat_td = td.as<MatrixTD>();
        TypeId col_type = mat_td.column_type_id;

        return 2 <= mat_td.column_count && mat_td.column_count <= 4 && is_gl_vec_type(col_type);
    }

    [[nodiscard]] bool is_gl_type(TypeId T) const {
        return T == gl_void || is_gl_scalar_type(T) || is_gl_vec_type(T) || is_gl_mat_type(T);
    }
};

#undef STC_GL_INIT_MATS
#undef STC_GL_INIT_MAT_N
#undef STC_GL_INIT_VECS

#undef STC_GL_DECL_MATS
#undef STC_GL_DECL_VECS

} // namespace stc::glsl
