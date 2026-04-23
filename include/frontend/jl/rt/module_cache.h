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

    jl_module_t* mod_ptr() const { return _mod_ptr; }

    operator jl_module_t*() const { return mod_ptr(); }

    // returns fn ptr from cache, or retrieves from julia, adds to cache and returns
    jl_function_t* get_fn(std::string_view fn_name, bool throw_on_not_found = true);
};

class JuliaModuleCache {
    using ModuleTable =
        std::unordered_map<std::string, JuliaModule, TransparentStringHash, std::equal_to<>>;

    ModuleTable module_cache{};

    using MaybeModRef = std::optional<std::reference_wrapper<JuliaModule>>;

public:
    explicit JuliaModuleCache()
        : main_mod{register_mod("Main", jl_main_module)},
          base_mod{register_mod("Base", jl_base_module)},
          core_mod{register_mod("Core", jl_core_module)},
          meta_mod{get_mod_or_throw("Base.Meta")},
          comp_mod{get_mod_or_throw("Core.Compiler")},
          glm_mod{get_mod_or_throw("Main.JuliaGLM")} {}

    // returns JuliaModule from cache, or retrieves it from julia, adds to cache and returns
    // mod_path format: X.Y.Z
    [[nodiscard]] MaybeModRef get_mod(std::string_view mod_path,
                                      jl_module_t* root_mod = jl_main_module);

    [[nodiscard]] MaybeModRef get_mod(std::string_view mod_path, const JuliaModule& root_mod) {
        return get_mod(mod_path, root_mod.mod_ptr());
    }

    [[nodiscard]] JuliaModule& get_mod_or_throw(std::string_view mod_path,
                                                jl_module_t* root_mod = jl_main_module) {
        auto result = get_mod(mod_path, root_mod);

        if (!result.has_value())
            throw std::logic_error{std::format(
                "module path doesn't point to a Module object in Julia (in {})", mod_path)};

        return *result;
    }

    [[nodiscard]] JuliaModule& get_mod_or_throw(std::string_view mod_path,
                                                const JuliaModule& root_mod) {
        return get_mod_or_throw(mod_path, root_mod.mod_ptr());
    }

    // shorthands for common modules
    JuliaModule& main_mod; // Main
    JuliaModule& base_mod; // Base
    JuliaModule& core_mod; // Core
    JuliaModule& meta_mod; // Base.Meta
    JuliaModule& comp_mod; // Core.Compiler
    JuliaModule& glm_mod;

    // shorthands for common functions
    jl_function_t* meta_parse     = nullptr;
    jl_function_t* comp_ret_types = nullptr;

private:
    [[nodiscard]] JuliaModule& register_mod(std::string_view mod_path, jl_module_t* mod);
};

} // namespace stc::jl::rt