#include <cstring>
#include <iostream>

#include "frontend/jl/pkg_driver.h"

// NOLINTBEGIN

namespace stc::jl {

using namespace std::literals;

extern "C" STC_API void stc_jl_free(void* ptr) noexcept {
    std::cout << "stc_jl_free invoked\n";

    if (ptr != nullptr)
        std::free(ptr);
}

inline constexpr std::string_view return_str{"return value string from cpp"sv};

const char* print_expr(jl_value_t* expr_val) {
    if (!jl_is_expr(expr_val)) {
        // jl_error("");
        // return;
        throw std::runtime_error{"Non-Expr value received from Julia"};
    }

    jl_expr_t* expr      = (jl_expr_t*)expr_val;
    std::string head_str = jl_symbol_name(expr->head);

    std::cout << "Expr head is: " << head_str << '\n';

    jl_function_t* dump_fn = jl_get_function(jl_base_module, "dump");
    assert(dump_fn && "Failed to load function 'dump' from Julia");

    std::cout << "Expr dump through Julia:\n";

    jl_call1(dump_fn, expr_val);

    char* ret = (char*)std::malloc(return_str.size() + 1);

    if (ret == nullptr)
        throw std::runtime_error{"Error during malloc"};

    std::memcpy(ret, return_str.data(), return_str.size() + 1);

    return ret;
}

extern "C" STC_API const char* stc_jl_print_expr(jl_value_t* expr_val) noexcept {
    try {
        std::cout << "stc_jl_print_expr invoked\n";
        return print_expr(expr_val);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return nullptr;
    } catch (...) {
        std::cerr << "Unknown error occured during C++ execution\n";
        return nullptr;
    }
}

} // namespace stc::jl

// NOLINTEND
