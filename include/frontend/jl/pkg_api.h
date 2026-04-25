#pragma once

#include "julia_guard.h"

#include "base.h"

namespace stc::jl {

// jl_* prefixing was used to avoid naming collision with possible future frontends
// these functions will appear under stc::jl on the C++ side, but when viewed from C
// they're collapsed into the single global "namespace"
extern "C" {
    STC_API void stc_jl_free(void* ptr) noexcept;
    STC_API const char* stc_jl_transpile(jl_value_t* expr_v) noexcept;
}

} // namespace stc::jl
