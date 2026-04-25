#include "frontend/jl/rt/module_cache.h"

namespace stc::jl::rt {

jl_function_t* JuliaModule::get_fn(std::string_view fn_name, bool throw_on_not_found) {
    auto it = fn_cache.find(fn_name);
    if (it != fn_cache.end())
        return it->second;

    std::string fn_str{fn_name};
    jl_function_t* fn = jl_get_function(_mod_ptr, fn_str.c_str());

    if (fn == nullptr) {
        if (throw_on_not_found)
            throw std::logic_error{
                std::format("couldn't find function with name {} in module", fn_name)};

        return nullptr;
    }

    auto [fn_cache_it, inserted] = fn_cache.emplace(std::move(fn_str), fn);
    assert(inserted);

    return fn_cache_it->second;
}

JuliaModuleCache::MaybeModRef JuliaModuleCache::get_mod(std::string_view mod_path,
                                                        jl_module_t* root_mod) {
    auto it = module_cache.find(mod_path);
    if (it != module_cache.end())
        return it->second;

    auto get_next_mod = [&](size_t first, size_t len, jl_module_t* current_mod) -> jl_module_t* {
        // libjulia needs a null-terminated string
        std::string mod_name{mod_path.substr(first, len)};

        jl_value_t* mod_v = jl_get_global(current_mod, jl_symbol(mod_name.c_str()));
        auto* next_mod    = try_cast<jl_module_t>(mod_v);

        return next_mod;
    };

    size_t first_pos    = 0U;
    size_t dot_pos      = mod_path.find('.', first_pos);
    jl_module_t* mod_it = root_mod;
    while (dot_pos != std::string_view::npos) {
        if (first_pos == dot_pos)
            return std::nullopt;

        assert(first_pos < dot_pos);

        mod_it = get_next_mod(first_pos, dot_pos - first_pos, mod_it);

        if (mod_it == nullptr)
            return std::nullopt;

        first_pos = dot_pos + 1;
        dot_pos   = mod_path.find('.', first_pos);
    }

    if (first_pos >= mod_path.length())
        return std::nullopt;

    mod_it = get_next_mod(first_pos, mod_path.length() - first_pos, mod_it);
    if (mod_it == nullptr)
        return std::nullopt;

    auto [mod_cache_it, inserted] = module_cache.emplace(std::string{mod_path}, mod_it);
    assert(inserted);

    return mod_cache_it->second;
}

JuliaModule& JuliaModuleCache::register_mod(std::string_view mod_path, jl_module_t* mod) {
    auto it = module_cache.find(mod_path);
    if (it != module_cache.end()) {
        if (it->second.mod_ptr() != mod)
            throw std::logic_error{std::format(
                "trying to overwrite already registered module at {} with a different target",
                mod_path)};

        return it->second;
    }

    auto [mod_cache_it, inserted] = module_cache.emplace(std::string{mod_path}, mod);
    assert(inserted);

    return mod_cache_it->second;
}

} // namespace stc::jl::rt