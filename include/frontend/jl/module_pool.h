#pragma once

#include <limits>
#include <optional>

#include "common/bump_arena.h"
#include "julia_guard.h"

namespace stc::jl {

struct ModuleId : public StrongId<uint16_t> {
    using StrongId<uint16_t>::StrongId;

    constexpr bool is_null() const { return *this == null_id(); }

    static constexpr ModuleId null_id() { return ModuleId{0U}; }
};

class ModulePool {
    using SizeTy = ModuleId::id_type;

public:
    explicit ModulePool(SizeTy initial_capacity)
        : storage{}, pool{} {

        storage.reserve(initial_capacity);
        pool.reserve(initial_capacity);

        storage.push_back(nullptr); // reserve first (null) id's spot
    }

    [[nodiscard]] ModuleId get_id(jl_module_t* module) {
        auto it = pool.find(module);
        if (it != pool.end())
            return it->second;

        if (storage.size() > std::numeric_limits<SizeTy>::max())
            throw std::overflow_error{
                "Module insertion would overflow ModuleId's internal value type"};

        ModuleId id{storage.size()};

        storage.push_back(module);
        pool.emplace(module, id);

        return id;
    }

    bool has_id(ModuleId id) const { return storage.size() > id.value; }

    [[nodiscard]] jl_module_t* get_module_ptr(ModuleId id) const {
        assert(!id.is_null() && "trying retrieve from module pool with a null id");
        assert(has_id(id) && "retrieval id points outside the module pool's arena");

        return storage[id];
    }

    [[nodiscard]] std::optional<jl_module_t*> get_module_ptr_maybe(ModuleId id) const {
        if (id.is_null() || !has_id(id))
            return std::nullopt;

        return storage[id];
    }

private:
    std::vector<jl_module_t*> storage;
    std::unordered_map<jl_module_t*, ModuleId> pool;
};

} // namespace stc::jl
