#pragma once

#include <string>
#include <string_view>

#include "common/bump_arena_allocator.h"
#include "common/utils.h"

namespace stc {

struct SymbolId : public StrongId<uint32_t> {
    static SymbolId null_id() { return SymbolId{0U}; }

    bool is_null() const { return value == null_id().value; }
};

class SymbolPool {
    using ArenaTy = BumpArena<SymbolId::id_type>;

public:
    explicit SymbolPool(SymbolId::id_type initial_capacity_kb = 128U)
        : arena{initial_capacity_kb * 1024U}, pool{} {}

    SymbolPool(const SymbolPool&)            = delete;
    SymbolPool& operator=(const SymbolPool&) = delete;
    SymbolPool(SymbolPool&&)                 = default;
    SymbolPool& operator=(SymbolPool&&)      = default;

    SymbolId get_id(std::string_view symbol_value);
    std::string_view get_symbol(SymbolId id) const;
    bool has_id(SymbolId id) const;
    std::optional<std::string_view> get_symbol_maybe(SymbolId id) const;

private:
    ArenaTy arena;

    std::unordered_map<std::string_view, SymbolId> pool;
};

} // namespace stc
