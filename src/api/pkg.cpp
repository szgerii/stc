#include <cstring>
#include <iostream>

#include "api/pkg.h"
#include "api/transpiler.h"

namespace {

std::string* do_transpile(jl_value_t* expr_v, bool run_benchmark) {
    using namespace stc;
    using namespace stc::jl;

    if (!jl_is_expr(expr_v)) {
        std::cerr << "received non-Expr value\n";
        return nullptr;
    }

    std::optional<std::string> result =
        run_benchmark ? api::transpile<true>(expr_v, stc::TranspilerConfig{})
                      : api::transpile<false>(expr_v, stc::TranspilerConfig{});

    if (!result.has_value())
        return nullptr;

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return new std::string(std::move(*result));
}

} // namespace

namespace stc::jl {

// TODO: config API

// ! this returns a handle ptr, which must be used through get_result_data and free_result
extern "C" STC_API void* stc_jl_transpile(jl_value_t* expr_v, bool run_benchmark) noexcept {
    if (!jl_is_expr(expr_v)) {
        std::cerr << "received non-Expr julia object in stc_jl_transpile\n";
        return nullptr;
    }

    try {
        std::string* result_mem = do_transpile(expr_v, run_benchmark);
        return static_cast<void*>(result_mem);
    } catch (const std::exception& ex) {
        std::cerr << "the following std::exception was thrown during transpilation:\n";
        std::cerr << ex.what() << '\n';
    } catch (...) {
        std::cerr << "an unexpected error was thrown during transpilation\n";
    }

    return nullptr;
}

// gets the string data of a handle
extern "C" STC_API const char* stc_jl_cstr_from_handle(void* handle) noexcept {
    if (handle == nullptr) {
        std::cerr << "nullptr passed to stc_jl_get_result_data\n";
        return nullptr;
    }

    return static_cast<std::string*>(handle)->c_str();
}

// frees the underlying string data belonging to a handle
extern "C" STC_API void stc_jl_free_handle(void* handle) noexcept {
    if (handle == nullptr) {
        std::cerr << "nullptr passed to stc_jl_free_result\n";
        return;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete static_cast<std::string*>(handle);
}

} // namespace stc::jl
