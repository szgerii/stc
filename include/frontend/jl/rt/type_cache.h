#pragma once

#include "frontend/jl/rt/module_cache.h"

namespace stc::jl::rt {

struct JuliaTypeCache {
    jl_datatype_t* uint128;

    explicit JuliaTypeCache(JuliaModuleCache& mod_cache) {
        jl_module_t* jl_core = mod_cache.core_module.mod_ptr();
        uint128 = safe_cast<jl_datatype_t>(jl_get_global(jl_core, jl_symbol("UInt128")));

        assert(uint128 != nullptr &&
               "failed to load uint128 datatype from julia through the core module");
    }
};

} // namespace stc::jl::rt