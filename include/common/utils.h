#pragma once

#include <cassert>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "common/config.h"
#include "common/utils.h"

namespace stc {

// CLEANUP: better structure for utils, concepts, etc.

using namespace std::literals;

namespace detail {

template <typename F, typename T>
using unqual_return_t = std::remove_cvref_t<std::invoke_result_t<F, T>>;

} // namespace detail

template <typename T>
concept CHashable = requires (T a) {
    { std::hash<T>{}(a) } -> std::same_as<size_t>;
};

template <typename T>
concept CEqualityComparable = requires (T a, T b) {
    { std::equal_to<T>{}(a, b) } -> std::same_as<bool>;
};

inline std::string indent(size_t level, size_t unit_width) {
    return STC_USE_TABS ? std::string(level, '\t') : std::string(level * unit_width, ' ');
}

void report(std::string_view msg, std::string_view prefix = ""sv, std::ostream& out = std::cerr);

void error(std::string_view msg, std::ostream& out = std::cerr);
void warning(std::string_view msg, std::ostream& out = std::cerr);
void internal_error(std::string_view msg, std::ostream& out = std::cerr);

inline std::string dump_label(const std::string& label_str) {
    return std::format("({}):\n", label_str);
}

template <typename T, typename Projection = std::identity>
requires std::regular_invocable<Projection&, const T&> && requires {
    // unordered set is instantiable with the return type of Projection
    std::unordered_set<detail::unqual_return_t<Projection&, const T&>>{};
}
bool has_duplicates(const std::vector<T>& vec, Projection proj = {}) {
    std::unordered_set<detail::unqual_return_t<Projection&, const T&>> buffer;
    buffer.reserve(vec.size());

    for (const T& el : vec) {
        if (!buffer.insert(std::invoke(proj, el)).second)
            return true;
    }

    return false;
}

template <typename T>
requires std::is_integral_v<T>
bool is_power_of_two(T x) {
    return x != 0 && (x & (x - 1)) == 0;
}

// same as boost's current hash_combine implementation for 32/64-bit size_t:
// https://www.boost.org/doc/libs/latest/libs/container_hash/doc/html/hash.html#notes_hash_combine
template <typename T>
requires CHashable<T>
constexpr size_t hash_combine(size_t seed, const T& v) {
    size_t x = seed + 0x9e3779b9 + std::hash<T>{}(v);

    if constexpr (sizeof(size_t) == 8U) {
        x ^= x >> 32;
        x *= 0xe9846af9b1a615d;
        x ^= x >> 32;
        x *= 0xe9846af9b1a615d;
        x ^= x >> 28;
    } else if constexpr (sizeof(size_t) == 4U) {
        x ^= x >> 16;
        x *= 0x21f0aaad;
        x ^= x >> 15;
        x *= 0x735a2d97;
        x ^= x >> 15;
    } else {
        static_assert(false, "no hash_combine implementation for current size_t width");
    }

    return x;
}

// named after strong typedefs (pattern has been slightly tweaked/extended)
template <std::unsigned_integral IdTy>
requires CHashable<IdTy>
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

template <typename T>
concept CStrongId = requires {
    typename T::id_type;
} && std::derived_from<T, StrongId<typename T::id_type>> && CHashable<typename T::id_type>;

template <typename T>
concept CNullableStrongId = CStrongId<T> && requires {
    { T::null_id() } -> std::convertible_to<T>;
};

// base for AST node ids, where kind also has to be stored in the id
// most significant 8 bits are for kinds, rest are for the id value
struct SplitU32Id : public StrongId<uint32_t> {
    using kind_type = uint8_t;

    static constexpr uint32_t ID_MASK   = 0x00FFFFFF;
    static constexpr uint32_t KIND_MASK = ~ID_MASK;

    constexpr SplitU32Id() = default;
    constexpr SplitU32Id(uint32_t id, uint8_t kind)
        : StrongId{static_cast<uint32_t>(kind) << 24 | id} {

        if ((id & KIND_MASK) != 0)
            throw std::logic_error{"id value must fit into 24 bits"};
    };

    uint32_t id_value() const { return value & ID_MASK; }
    uint8_t kind_value() const { return static_cast<uint8_t>(value >> 24); }
};

template <typename T>
concept CNullableSplitId =
    CNullableStrongId<T> &&
    std::same_as<std::remove_cvref_t<decltype(T::ID_MASK)>, typename T::id_type> &&
    std::same_as<std::remove_cvref_t<decltype(T::KIND_MASK)>, typename T::id_type> &&
    requires (T t) {
        typename T::kind_type;
        { T{0U, 0U} } -> std::same_as<T>;
        { t.id_value() } -> std::same_as<typename T::id_type>;
        { t.kind_value() } -> std::same_as<typename T::kind_type>;
    } && std::constructible_from<T, typename T::id_type, typename T::kind_type>;

template <typename... Ts>
inline constexpr bool dependent_false_v = false;

template <typename T, typename V>
concept CEnumOf = std::is_enum_v<T> && std::same_as<std::underlying_type_t<T>, V>;

template <typename... Args>
constexpr bool no_nullptrs(Args*... args) {
    return ((args != nullptr) && ...);
}
} // namespace stc

// hash impl for all id types derived from StrongId that "forwards" to the underlying value type
template <stc::CStrongId T>
struct std::hash<T> {
    size_t operator()(const T& id) const noexcept {
        return std::hash<typename T::id_type>{}(id.value);
    }
};
