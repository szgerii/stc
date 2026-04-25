#pragma once

#include "frontend/jl/rt/env.h"

namespace stc::jl::rt {

// lookup chain's expected format is: [ModuleA.ModuleB.(...).]fn_name
// the chain is expected to start from root_mod
[[nodiscard]]
inline jl_function_t* find_jl_function(std::string_view lookup_chain, rt::JuliaRTEnv& env,
                                       JuliaModule& root_mod, bool throw_on_not_found = true) {

    size_t last_dot_idx = lookup_chain.find_last_of('.');

    // TODO: check for dot end/begin

    JuliaModule& mod = root_mod;

    if (last_dot_idx != lookup_chain.npos) {
        if (throw_on_not_found) {
            mod = env.module_cache.get_mod_or_throw(lookup_chain.substr(0, last_dot_idx), root_mod);
        } else {
            auto maybe_mod =
                env.module_cache.get_mod(lookup_chain.substr(0, last_dot_idx), root_mod);

            if (!maybe_mod.has_value())
                return nullptr;

            mod = maybe_mod->get();
        }
    }

    return mod.get_fn(lookup_chain.substr(last_dot_idx + 1), throw_on_not_found);
}

[[nodiscard]]
inline jl_function_t* find_jl_function(std::string_view lookup_chain, rt::JuliaRTEnv& env,
                                       bool throw_on_not_found = true) {
    return find_jl_function(lookup_chain, env, env.module_cache.main_mod, throw_on_not_found);
}

// these were mainly added to satisfy clang-tidy's no implicit int -> bool cast rule
// without having to disable it globally, or mark it at every check to these

[[nodiscard]]
inline bool is_type(jl_value_t* value) {
    return jl_is_type(value) != 0;
}

[[nodiscard]]
inline bool is_concrete_type(jl_value_t* value) {
    return jl_is_concrete_type(value) != 0;
}

[[nodiscard]]
inline bool is_array_type(jl_datatype_t* value) {
    return jl_is_array_type(value) != 0;
}

} // namespace stc::jl::rt