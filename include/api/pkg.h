#pragma once

#include "julia_guard.h"

#include "base.h"

namespace stc::jl {

// jl_* prefixing was used to avoid naming collision with possible future frontends
// these functions will appear under stc::jl on the C++ side, but when viewed from C
// they're collapsed into the single global "namespace"
extern "C" {
    // TRANSPILATION API

    // set cfg_hadle to NULL to use default configuration settings
    STC_API void* stc_jl_transpile(jl_value_t* expr_v, bool run_benchmark,
                                   void* cfg_handle) noexcept;
    STC_API const char* stc_jl_get_result(void* result_handle) noexcept;
    STC_API void stc_jl_free_result(void* result_handle) noexcept;

    // CONFIG API

    STC_API void* stc_jl_create_cfg() noexcept;
    STC_API void stc_jl_free_cfg(void* cfg_handle) noexcept;
    STC_API void stc_jl_set_code_gen_indent(void* cfg_handle, uint16_t value) noexcept;
    STC_API void stc_jl_set_dump_indent(void* cfg_handle, uint16_t value) noexcept;

    // 0 - None, 1 - Partial, 2 - Verbose
    STC_API void stc_jl_set_err_dump_verbosity(void* cfg_handle, uint8_t value) noexcept;

    STC_API void stc_jl_set_use_tabs(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_dump_scopes(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_forward_fns(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_warn_on_fn_forward(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_warn_on_jl_sema_query(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_print_convert_fail_reason(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_track_bindings(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_coerce_to_f32(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_coerce_to_i32(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_capture_uniforms(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_dump_parsed(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_dump_sema(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_dump_lowered(void* cfg_handle, bool value) noexcept;
    STC_API void stc_jl_set_target_version(void* cfg_handle, const char* value) noexcept;
}

} // namespace stc::jl
