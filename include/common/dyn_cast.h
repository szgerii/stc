#pragma once

#include <memory>

namespace stc {

template <typename To, typename From, typename KindTy>
concept CDynCastable = std::derived_from<To, From> && requires (From from, KindTy kind) {
    { from.kind() } -> std::same_as<KindTy>;
    { To::same_node_kind(kind) } -> std::same_as<bool>;
};

template <typename To, typename From, typename KindTy>
requires CDynCastable<To, From, KindTy>
To* dyn_cast(From* ptr) {
    if (ptr == nullptr)
        return nullptr;

    if (To::same_node_kind(ptr->kind())) {
        return static_cast<To*>(ptr);
    }

    return nullptr;
}

template <typename To, typename From>
requires requires { typename From::kind_type; } && CDynCastable<To, From, typename From::kind_type>
To* dyn_cast(From* ptr) {
    return dyn_cast<To, From, typename From::kind_type>(ptr);
}

} // namespace stc