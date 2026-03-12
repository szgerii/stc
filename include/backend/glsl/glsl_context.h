#pragma once

#include "sir/context.h"

namespace stc::glsl {

using namespace stc::types;

class GLSLCtx : public sir::SIRCtx {
public:
    const TypeId gl_void, gl_bool, gl_int, gl_uint, gl_float, gl_double;

    explicit GLSLCtx()
        : sir::SIRCtx{},
          gl_void{type_pool.void_td()},
          gl_bool{type_pool.bool_td()},
          gl_int{type_pool.int_td(32, true)},
          gl_uint{type_pool.int_td(32, false)},
          gl_float{type_pool.float_td(32)},
          gl_double{type_pool.float_td(64)} {}

    [[nodiscard]] TypeId gl_vec_t(TypeId elem_t, uint32_t n) {
        return type_pool.vector_td(elem_t, n);
    }

    [[nodiscard]] TypeId gl_vec_t(uint32_t n) { return gl_vec_t(gl_float, n); }

    [[nodiscard]] TypeId gl_mat_t(TypeId col_t, uint32_t col_count) {
        return type_pool.matrix_td(col_t, col_count);
    }

    [[nodiscard]] TypeId gl_mat_t(TypeId elem_t, uint32_t n, uint32_t m) {
        return type_pool.matrix_td(gl_vec_t(elem_t, n), m);
    }
};

} // namespace stc::glsl
