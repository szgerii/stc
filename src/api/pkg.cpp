#include <cstring>
#include <iostream>

#include "api/pkg.h"
#include "api/transpiler.h"

namespace {

std::string* do_transpile(jl_value_t* expr_v, bool run_benchmark, stc::TranspilerConfig& config) {
    using namespace stc;
    using namespace stc::jl;

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto get_failure_result = []() { return new std::string{}; };

    if (!jl_is_expr(expr_v)) {
        std::cerr << "received non-Expr value from Julia\n";
        return get_failure_result();
    }

    std::optional<std::string> result = run_benchmark ? api::transpile<true>(expr_v, config)
                                                      : api::transpile<false>(expr_v, config);

    if (!result.has_value())
        return get_failure_result();

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return new std::string(std::move(*result));
}

} // namespace

namespace stc::jl {

extern "C" {

    // =============
    // TRANSPILE API
    // =============

    // ! this returns a handle ptr, which must be read through stc_jl_cstr_from_handle and destroyed
    // ! with free_handle
    STC_API void* stc_jl_transpile(jl_value_t* expr_v, bool run_benchmark,
                                   void* cfg_handle) noexcept {
        if (!jl_is_expr(expr_v)) {
            std::cerr << "received non-Expr julia object in stc_jl_transpile\n";
            return nullptr;
        }

        try {

            TranspilerConfig config = cfg_handle != nullptr
                                          ? *(static_cast<TranspilerConfig*>(cfg_handle))
                                          : TranspilerConfig{};

            std::string* result_mem = do_transpile(expr_v, run_benchmark, config);
            return static_cast<void*>(result_mem);

        } catch (const std::exception& ex) {
            std::cerr << "the following std::exception was thrown during transpilation:\n";
            std::cerr << ex.what() << '\n';
        } catch (...) {
            std::cerr << "an unexpected error was thrown during transpilation\n";
        }

        return nullptr;
    }

    // gets the underlying C string data of a result handle
    STC_API const char* stc_jl_get_result(void* result_handle) noexcept {
        if (result_handle == nullptr) {
            std::cerr << "nullptr passed to stc_jl_get_result\n";
            return nullptr;
        }

        return static_cast<std::string*>(result_handle)->c_str();
    }

    // frees the underlying string data belonging to a result handle
    STC_API void stc_jl_free_result(void* result_handle) noexcept {
        if (result_handle == nullptr) {
            std::cerr << "nullptr passed to stc_jl_free_result\n";
            return;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete static_cast<std::string*>(result_handle);
    }

    // ==========
    // CONFIG API
    // ==========

#define STC_SET_CFG_VALUE(handle, field, value)                                                    \
    if ((handle) != nullptr) {                                                                     \
        static_cast<TranspilerConfig*>(handle)->field = (value);                                   \
    }

    STC_API void* stc_jl_create_cfg() noexcept {
        try {
            return new TranspilerConfig{}; // NOLINT(cppcoreguidelines-owning-memory)
        } catch (...) {
            return nullptr;
        }
    }

    STC_API void stc_jl_free_cfg(void* cfg_handle) noexcept {
        if (cfg_handle != nullptr) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            delete static_cast<TranspilerConfig*>(cfg_handle);
        }
    }

    STC_API void stc_jl_set_code_gen_indent(void* cfg_handle, uint16_t value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, code_gen_indent, value);
    }

    STC_API void stc_jl_set_dump_indent(void* cfg_handle, uint16_t value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, dump_indent, value);
    }

    STC_API void stc_jl_set_err_dump_verbosity(void* cfg_handle, uint8_t value) noexcept {
        if (value < static_cast<uint8_t>(DumpVerbosity::First) ||
            value > static_cast<uint8_t>(DumpVerbosity::Last))
            return;

        STC_SET_CFG_VALUE(cfg_handle, err_dump_verbosity, static_cast<DumpVerbosity>(value));
    }

    STC_API void stc_jl_set_use_tabs(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, use_tabs, value);
    }

    STC_API void stc_jl_set_dump_scopes(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, dump_scopes, value);
    }

    STC_API void stc_jl_set_forward_fns(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, forward_fns, value);
    }

    STC_API void stc_jl_set_warn_on_fn_forward(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, warn_on_fn_forward, value);
    }

    STC_API void stc_jl_set_warn_on_jl_sema_query(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, warn_on_jl_sema_query, value);
    }

    STC_API void stc_jl_set_print_convert_fail_reason(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, print_convert_fail_reason, value);
    }

    STC_API void stc_jl_set_track_bindings(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, track_bindings, value);
    }

    STC_API void stc_jl_set_coerce_to_f32(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, coerce_to_f32, value);
    }

    STC_API void stc_jl_set_coerce_to_i32(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, coerce_to_i32, value);
    }

    STC_API void stc_jl_set_capture_uniforms(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, capture_uniforms, value);
    }

    STC_API void stc_jl_set_dump_parsed(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, dump_parsed, value);
    }

    STC_API void stc_jl_set_dump_sema(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, dump_sema, value);
    }

    STC_API void stc_jl_set_dump_lowered(void* cfg_handle, bool value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, dump_lowered, value);
    }

    STC_API void stc_jl_set_target_version(void* cfg_handle, const char* value) noexcept {
        STC_SET_CFG_VALUE(cfg_handle, target_version, value != nullptr ? std::string{value} : "");
    }

#undef STC_SET_CFG_VALUE
}

} // namespace stc::jl
