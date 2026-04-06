#pragma once

#include "julia_guard.h"

#include "common/utils.h"
#include "frontend/jl/utils.h"

#include <unordered_map>

namespace stc::jl::rt {

struct JuliaModule {
private:
    using FunctionCache =
        std::unordered_map<std::string, jl_function_t*, TransparentStringHash, std::equal_to<>>;

    jl_module_t* _mod_ptr;
    FunctionCache fn_cache;

public:
    explicit JuliaModule(jl_module_t* mod_ptr)
        : _mod_ptr{mod_ptr}, fn_cache{} {

        assert(mod_ptr != nullptr && "trying to initialize julia module with nullptr");

        fn_cache.reserve(16);
    }

    explicit JuliaModule(jl_module_t* mod_ptr, FunctionCache initial_fn_cache)
        : _mod_ptr{mod_ptr}, fn_cache{std::move(initial_fn_cache)} {

        assert(mod_ptr != nullptr && "trying to initialize julia module with nullptr");
    }

    jl_module_t* mod_ptr() const { return _mod_ptr; }

    // returns fn ptr from cache, or retrieves from julia, adds to cache and returns
    jl_function_t* get_fn(std::string_view fn_name);
};

class JuliaModuleCache {
    using ModuleTable =
        std::unordered_map<std::string, JuliaModule, TransparentStringHash, std::equal_to<>>;

    ModuleTable module_cache{};

public:
    explicit JuliaModuleCache()
        : main_module{get_mod("Main", jl_main_module)},
          base_module{get_mod("Base", jl_base_module)},
          core_module{get_mod("Core", jl_core_module)},
          meta_module{get_mod("Base.Meta")} {}

    // returns JuliaModule from cache, or retrieves it from julia, adds to cache and returns
    // mod_path format: X.Y.Z
    [[nodiscard]] JuliaModule& get_mod(std::string_view mod_path);

    // shorthands for common modules
    JuliaModule& main_module;
    JuliaModule& base_module;
    JuliaModule& core_module;
    JuliaModule& meta_module;

    // shorthands for common functions
    jl_function_t* meta_parse     = nullptr;
    jl_function_t* comp_ret_types = nullptr;

private:
    [[nodiscard]] JuliaModule& get_mod(std::string_view mod_path, jl_module_t* mod);
};

} // namespace stc::jl::rt