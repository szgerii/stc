#pragma once

#include "base.h"
#include "julia_guard.h"

#include <utility>

namespace stc::jl {

template <typename T>
struct jl_cast_trait;

template <>
struct jl_cast_trait<jl_sym_t> {
    static bool is_type_of(jl_value_t* value) { return jl_is_symbol(value); }
};

template <>
struct jl_cast_trait<jl_expr_t> {
    static bool is_type_of(jl_value_t* value) { return jl_is_expr(value); }
};

template <>
struct jl_cast_trait<jl_module_t> {
    static bool is_type_of(jl_value_t* value) { return jl_is_module(value); }
};

template <>
struct jl_cast_trait<jl_datatype_t> {
    static bool is_type_of(jl_value_t* value) { return jl_is_datatype(value); }
};

template <typename T>
concept CSafeCastable = requires (jl_value_t* value) {
    { jl_cast_trait<T>::is_type_of(value) } -> std::same_as<bool>;
};

// performs extra assumption checks in debug builds, same as jl_fieldref in release builds
// field_name is only used for debug assertions
[[nodiscard]]
STC_FORCE_INLINE jl_value_t* safe_fieldref(jl_value_t* node, size_t index,
                                           [[maybe_unused]] const char* field_name) {
#ifndef NDEBUG
    auto* dt         = reinterpret_cast<jl_datatype_t*>(jl_typeof(node));
    int actual_index = jl_field_index(dt, jl_symbol(field_name), 0);

    assert(actual_index >= 0 && std::cmp_equal(actual_index, index) &&
           "invalid libjulia C API assumption");
    assert(index < jl_datatype_nfields(dt) && "invalid julia C API fieldref index");
#endif

    return jl_fieldref(node, index);
}

template <typename T>
requires CSafeCastable<T>
[[nodiscard]]
STC_FORCE_INLINE T* safe_cast(jl_value_t* value) {
    if (value == nullptr)
        return nullptr;

    assert(jl_cast_trait<T>::is_type_of(value) &&
           "trying to cast jl_value_t pointer to invalid type");

    return reinterpret_cast<T*>(value);
}

template <typename T>
requires CSafeCastable<T>
[[nodiscard]]
STC_FORCE_INLINE T* try_cast(jl_value_t* value) {
    if (value == nullptr || !jl_cast_trait<T>::is_type_of(value))
        return nullptr;

    return reinterpret_cast<T*>(value);
}

} // namespace stc::jl
