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

struct SrcLocation {
    const intptr_t line;
    const uint32_t col;

    constexpr explicit SrcLocation(intptr_t l, uint32_t c)
        : line(l), col(c) {
        assert(l > 0 && c > 0 && "Line and col numbers cannot be zero");
    }

    constexpr bool is_null() const { return line == 0 && col == 0; }

    [[nodiscard]] static SrcLocation null() { return SrcLocation{}; }

private:
    // null state constructor
    constexpr explicit SrcLocation()
        : line(0), col(0) {}
};

// used by SrcInfoPool, to emplace invalid state into arena
static_assert(std::is_trivially_copy_constructible_v<SrcLocation>);

struct SrcLocationId : public StrongId<uint32_t> {
    using StrongId::StrongId;

    constexpr bool is_null() const { return *this == null_id(); }

    static constexpr SrcLocationId null_id() { return SrcLocationId{0U}; }
};

void report(const SrcFile& file, SrcLocation location, std::string_view msg,
            std::string_view prefix = ""sv, std::ostream& out = std::cerr);
void error(const SrcFile& file, SrcLocation location, std::string_view msg,
           std::ostream& out = std::cerr);
void warning(const SrcFile& file, SrcLocation location, std::string_view msg,
             std::ostream& out = std::cerr);
void internal_error(const SrcFile& file, SrcLocation location, std::string_view msg,
                    std::ostream& out = std::cerr);

class SrcInfoPool {
private:
    using SizeTy       = SrcLocationId::id_type;
    using ArenaTy      = BumpArena<SizeTy>;
    using ArenaAllocTy = BumpArenaAllocator<SizeTy, SrcLocation>;

public:
    // ! expects arena to not be modified by others, only inspected
    explicit SrcInfoPool(SizeTy initial_location_capacity_kb, size_t initial_file_capacity = 4);

    SrcInfoPool(const SrcInfoPool&)                = delete;
    SrcInfoPool& operator=(const SrcInfoPool&)     = delete;
    SrcInfoPool(SrcInfoPool&&) noexcept            = default;
    SrcInfoPool& operator=(SrcInfoPool&&) noexcept = default;

    [[nodiscard]] SrcLocationId get_location(intptr_t line, uint32_t col);
    [[nodiscard]] size_t get_file(std::string path);

    [[nodiscard]] SrcLocation get_location(SrcLocationId loc_id) const;
    [[nodiscard]] const SrcFile& get_file_for_location(SrcLocationId loc_id) const;

    [[nodiscard]]
    std::pair<SrcLocation, const SrcFile&> get_loc_and_file(SrcLocationId loc_id) const;

    [[nodiscard]] SrcLocationId null_loc() const;

private:
    ArenaTy arena;
    ArenaAllocTy arena_alloc;

    std::vector<std::pair<SrcLocationId, SrcFile>> file_bounds;

    SrcLocationId last_loc_id;
};

void report(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg,
            std::string_view prefix = ""sv, std::ostream& out = std::cerr);
void error(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg,
           std::ostream& out = std::cerr);
void warning(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg,
             std::ostream& out = std::cerr);
void internal_error(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg,
                    std::ostream& out = std::cerr);

} // namespace stc
