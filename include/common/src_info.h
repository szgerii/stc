#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "common/bump_arena.h"
#include "common/bump_arena_allocator.h"
#include "common/config.h"
#include "common/utils.h"

namespace stc {

struct SrcFile {
    const std::string path;
};

// FEATURE: store byte offsets instead of line/col

struct SrcLocation {
    // up to ~1M lines and ~4K chars per line
    // these values should be enough for most shader transpilation use cases
    const uint32_t line : 20;
    const uint32_t col  : 12;

    constexpr explicit SrcLocation(uint32_t l, uint32_t c)
        : line(l), col(c) {
        assert(l > 0 && c > 0 && "Line and col numbers cannot be zero");
        assert(l < (1U << 20) && "Line number exceeds upper bound");
        assert(c < (1U << 12) && "Column number exceeds upper bound");
    }

    constexpr bool is_valid() const { return line != 0 && col != 0; }

    [[nodiscard]] static SrcLocation invalid() { return SrcLocation{}; }

private:
    // invalid state constructor
    constexpr explicit SrcLocation()
        : line(0), col(0) {}
};

// used by SrcInfoManager, to emplace invalid state into arena
static_assert(std::is_trivially_copy_constructible_v<SrcLocation>);

struct SrcLocationId : public StrongId<uint32_t> {
    using StrongId::StrongId;
};

std::nullptr_t report(const SrcFile& file, SrcLocation location, std::string_view msg,
                      std::string_view prefix = ""sv, std::ostream& out = std::cerr);
std::nullptr_t error(const SrcFile& file, SrcLocation location, std::string_view msg,
                     std::ostream& out = std::cerr);
std::nullptr_t warning(const SrcFile& file, SrcLocation location, std::string_view msg,
                       std::ostream& out = std::cerr);

class SrcInfoManager {
private:
    using ArenaTy      = BumpArena<SrcLocationId::id_type>;
    using ArenaAllocTy = BumpArenaAllocator<SrcLocationId::id_type, SrcLocation>;

public:
    // ! expects arena to not be modified by others, only inspected
    explicit SrcInfoManager(ArenaTy& arena, size_t initial_file_capacity = 4);

    SrcInfoManager(const SrcInfoManager&)            = delete;
    SrcInfoManager(SrcInfoManager&&)                 = default;
    SrcInfoManager& operator=(const SrcInfoManager&) = delete;
    SrcInfoManager& operator=(SrcInfoManager&&)      = default;

    [[nodiscard]] SrcLocationId make_location(uint32_t line, uint32_t col);
    [[nodiscard]] uint64_t make_file(std::string path);

    const SrcFile& get_file_for_location(SrcLocationId loc_id) const;

private:
    ArenaAllocTy arena_alloc;

    std::vector<std::pair<SrcLocationId, SrcFile>> file_bounds;

    SrcLocationId invalid_location_id;
    SrcLocationId last_loc_id;
};

} // namespace stc
