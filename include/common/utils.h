#pragma once

#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "common/concepts.h"
#include "common/config.h"

namespace stc {

inline std::string indent(size_t level) {
    return STC_USE_TABS ? std::string(level, '\t') : std::string(level * STC_DUMP_INDENT, ' ');
}

std::nullptr_t report(std::string_view msg, bool is_warning, std::ostream& out = std::cerr);
std::nullptr_t error(std::string_view msg, std::ostream& out = std::cerr);
std::nullptr_t warning(std::string_view msg, std::ostream& out = std::cerr);

template <typename T, typename Projection = std::identity>
requires std::regular_invocable<Projection&, const T&> && requires {
    // unordered set is instantiable with the return type of Projection
    std::unordered_set<std::remove_cvref_t<std::invoke_result_t<Projection&, const T&>>>{};
}
bool has_duplicates(const std::vector<T>& vec, Projection proj = {}) {
    using ProjType = std::remove_cvref_t<std::invoke_result_t<Projection&, const T&>>;

    std::unordered_set<ProjType> buffer;
    buffer.reserve(vec.size());

    for (const T& el : vec) {
        if (!buffer.insert(std::invoke(proj, el)).second)
            return true;
    }

    return false;
}

template <typename T>
requires std::is_integral_v<T>
inline bool is_power_of_two(T x) {
    return x != 0 && (x & (x - 1)) == 0;
}

template <typename To, typename From>
requires requires (From* f) { dynamic_cast<To*>(f); }
std::unique_ptr<To> dynamic_unique_cast(std::unique_ptr<From>&& ptr) {
    if (auto* cast_ptr = dynamic_cast<To*>(ptr.get())) {
        std::ignore = ptr.release();
        return std::unique_ptr<To>{cast_ptr};
    }

    return nullptr;
}

// only use when it's already verified that From can be successfully cast to To
// otherwise, memory owned by ptr WILL be leaked
template <typename To, typename From>
requires requires (From* f) { dynamic_cast<To*>(f); }
std::unique_ptr<To> static_unique_cast(std::unique_ptr<From>&& ptr) {
    assert(dynamic_cast<To*>(ptr.get()) != nullptr &&
           "Pointer passed to static_unique_cast could not be cast to target pointer type! This "
           "will produce UB without an explicit warning/error in non-debug builds!");

    return std::unique_ptr<To>(static_cast<To*>(ptr.release()));
}

// same as boost's current hash_combine implementation for 64-bit size_t:
// https://www.boost.org/doc/libs/latest/libs/container_hash/doc/html/hash.html#notes_hash_combine
template <typename T>
// requires CHashable<T>
inline size_t hash_combine(size_t seed, const T& v) {
    size_t x = seed + 0x9e3779b9 + std::hash<T>{}(v);
    x ^= x >> 32;
    x *= 0xe9846af9b1a615d;
    x ^= x >> 32;
    x *= 0xe9846af9b1a615d;
    x ^= x >> 28;
    return x;
}

// named after Boost's strong typedefs (pattern has been slightly tweaked/extended)
template <std::unsigned_integral IdTy>
struct StrongId {
    using id_type = IdTy;

    IdTy value;

    constexpr StrongId() = default;

    constexpr StrongId(IdTy id)
        : value{id} {}

    // to enable truncated static_cast-s without warnings
    template <std::unsigned_integral T>
    requires (!std::same_as<T, IdTy>)
    constexpr explicit StrongId(T value)
        : value{static_cast<IdTy>(value)} {}

    constexpr operator IdTy() const { return value; }

    constexpr bool operator==(const StrongId&) const  = default;
    constexpr auto operator<=>(const StrongId&) const = default;
};

template <typename... Ts>
inline constexpr bool dependent_false_v = false;

} // namespace stc
