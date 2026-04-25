#pragma once

#include <cassert>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "common/config.h"

namespace stc {

// CLEANUP: better structure for utils, concepts, etc.

using namespace std::literals;

namespace detail {

template <typename F, typename T>
using unqual_return_t = std::remove_cvref_t<std::invoke_result_t<F, T>>;

} // namespace detail

template <typename T, typename Hasher = std::hash<T>>
concept CHashable = requires (const T& a) {
    { Hasher{}(a) } -> std::same_as<size_t>;
};

template <typename T>
concept CEqualityComparable = requires (T a, T b) {
    { std::equal_to<T>{}(a, b) } -> std::same_as<bool>;
};

inline std::string indent(size_t level, size_t unit_width, bool use_tabs) {
    return use_tabs ? std::string(level, '\t') : std::string(level * unit_width, ' ');
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

// same as boost container_hash's current hash_combine implementation for 32/64-bit size_t:
// https://www.boost.org/doc/libs/latest/libs/container_hash/doc/html/hash.html#notes_hash_combine
template <typename T, typename Hasher = std::hash<T>>
requires CHashable<T, Hasher>
constexpr size_t hash_combine(size_t seed, const T& v) {
    size_t x = seed + 0x9e3779b9 + Hasher{}(v);

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
    requires (!std::same_as<T, IdTy>) && (!std::same_as<T, bool>)
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
concept CNullableStrongId = CStrongId<T> && requires (T t) {
    { T::null_id() } -> std::convertible_to<T>;
    { t.is_null() } -> std::same_as<bool>;
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

template <typename ImplTy, typename RetTy, typename T>
concept CHasVisitorFor = requires (ImplTy impl, T t) {
    { impl.visit(t) } -> std::convertible_to<RetTy>;
};

template <typename T, typename V>
concept CEnumOf = std::is_enum_v<T> && std::same_as<std::underlying_type_t<T>, V>;

template <typename... Args>
constexpr bool no_nullptrs(Args*... args) {
    return ((args != nullptr) && ...);
}

// core idea from:
// https://tamir.dev/posts/that-overloaded-trick-overloading-lambdas-in-cpp17/
template <typename... Ts>
requires (requires { &Ts::operator(); } && ...)
struct Overloaded : Ts... {
    using Ts::operator()...;
};

// util for hashing std::vector<T>-s where T is hashable through an element-wise hash_combine
// (not included as a std::hash specialization because i dont wanna enable blind hashing capability
// for vector, only when explicitly requested)
template <typename T, typename Hasher = std::hash<T>>
requires CHashable<T, Hasher>
struct VectorHash {
    size_t operator()(const std::vector<T>& vec) const noexcept {
        size_t h = 0;

        for (const T& el : vec)
            h = hash_combine<T, Hasher>(h, el);

        return h;
    }
};

template <typename U, typename V, typename UHasher = std::hash<U>, typename VHasher = std::hash<V>>
requires CHashable<U, UHasher> && CHashable<V, VHasher>
struct PairwiseHash {
    size_t operator()(const std::pair<U, V>& pair) const noexcept {
        size_t u_hash = UHasher{}(pair.first);
        return hash_combine<V, VHasher>(u_hash, pair.second);
    }
};

// for heterogenous lookup for string/string_view (avoids implicit string alloc for string_views)
// https://www.cppstories.com/2021/heterogeneous-access-cpp20/
struct TransparentStringHash {
    using is_transparent = void;

    size_t operator()(const std::string& str) const { return std::hash<std::string>{}(str); }
    size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
    size_t operator()(const char* c_str) const { return std::hash<std::string_view>{}(c_str); }
};

// mainly just wraps an optional together with an initializer, a more generic version of:
// https://softwarepatternslexicon.com/cpp/creational-patterns-in-c/lazy-initialization/
// reference and pointer types are supported too
template <typename InitFn>
requires std::invocable<InitFn> && (!std::is_void_v<std::invoke_result_t<InitFn>>)
struct LazyInit {
private:
    using InitRetTy = std::invoke_result_t<InitFn>;
    using StoredTy =
        std::conditional_t<std::is_reference_v<InitRetTy>,
                           std::reference_wrapper<std::remove_reference_t<InitRetTy>>, InitRetTy>;

    std::optional<StoredTy> value = std::nullopt;
    InitFn initializer;

public:
    explicit LazyInit(InitFn initializer)
        : initializer{initializer} {}

    bool has_value() const { return value.has_value(); }

    decltype(auto) get() {
        if (!value.has_value())
            value.emplace(std::invoke(initializer));

        if constexpr (std::is_reference_v<InitRetTy>)
            return value->get(); // unwrap reference_wrapper
        else if constexpr (std::is_pointer_v<InitRetTy>)
            return *value + 0; // force copy so that T* is returned instead of T*&
        else
            return *value;
    }
};

// defers the execution of a Callable until the object's destruction
template <typename DeferredFn>
requires std::invocable<DeferredFn>
class ScopeGuard {
    DeferredFn deferred_fn;

public:
    explicit ScopeGuard(DeferredFn fn)
        : deferred_fn{fn} {}

    ~ScopeGuard() { deferred_fn(); }

    ScopeGuard(const ScopeGuard&)            = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&)                 = delete;
    ScopeGuard& operator=(ScopeGuard&&)      = delete;
};

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4996)
#endif
class FileRAII {
    FILE* file = nullptr;

public:
    explicit FileRAII(const std::string& path, const char* mode) {
        file = fopen(path.c_str(), mode);
    }

    ~FileRAII() {
        if (file != nullptr)
            fclose(file);
    }

    FileRAII(const FileRAII&)            = delete;
    FileRAII& operator=(const FileRAII&) = delete;

    FileRAII(FileRAII&& other) noexcept
        : file{other.file} {
        other.file = nullptr;
    }
    FileRAII& operator=(FileRAII&& other) {
        if (this == &other)
            return *this;

        if (file != nullptr)
            fclose(file);

        this->file = other.file;
        other.file = nullptr;

        return *this;
    }

    FILE* get() const { return file; }
};
#if _MSC_VER
    #pragma warning(pop)
#endif

} // namespace stc

// hash impl for all id types derived from StrongId that "forwards" to the underlying value type
template <stc::CStrongId T>
struct std::hash<T> {
    size_t operator()(const T& id) const noexcept {
        return std::hash<typename T::id_type>{}(id.value);
    }
};
