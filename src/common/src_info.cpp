#include <stdexcept>

#include "common/src_info.h"

namespace {
using stc::SrcFile, stc::SrcLocation;

void print_locinfo(const SrcFile& file, SrcLocation location, std::ostream& out) {
    out << '[' << file.path << ':' << location.line << ':' << location.col << "] ";
}

} // namespace

namespace stc {

std::nullptr_t report(const SrcFile& file, SrcLocation location, std::string_view msg,
                      std::string_view prefix, std::ostream& out) {
    // FEATURE: print code snippet

    print_locinfo(file, location, out);
    return report(msg, prefix, out);
}

std::nullptr_t error(const SrcFile& file, SrcLocation location, std::string_view msg,
                     std::ostream& out) {
    print_locinfo(file, location, out);
    return error(msg, out);
}

std::nullptr_t warning(const SrcFile& file, SrcLocation location, std::string_view msg,
                       std::ostream& out) {
    print_locinfo(file, location, out);
    return warning(msg, out);
}

SrcInfoManager::SrcInfoManager(ArenaTy& arena, size_t initial_file_capacity)
    : arena_alloc{&arena},
      file_bounds{},
      invalid_location_id{arena_alloc.emplace(SrcLocation::invalid()).first},
      last_loc_id{invalid_location_id} {
    file_bounds.reserve(initial_file_capacity);

    // invalid location and file states
    std::ignore = make_file("SRC FILE INFO UNAVAILABLE");
}

SrcLocationId SrcInfoManager::make_location(uint32_t line, uint32_t col) {
    if (!arena_alloc.can_allocate()) {
        warning(
            "Invalid source location information returned from SrcInfoManager, line/col numbers "
            "printed after this might not be accurate. The processed file might be too large.");

        return invalid_location_id;
    }

    auto [id, _ptr] = arena_alloc.emplace(line, col);
    last_loc_id     = id;
    return id;
}

uint64_t SrcInfoManager::make_file(std::string path) {
    file_bounds.emplace_back(last_loc_id, SrcFile{std::move(path)});

    return file_bounds.size() - 1;
}

const SrcFile& SrcInfoManager::get_file_for_location(SrcLocationId loc_id) const {
    // the below loop relies on these invariants
    assert(!file_bounds.empty() &&
           "File pool was empty, its size should be at least 1 (the invalid src file at idx 0)");

    // CLEANUP: last_id_of_prev is monotonically increasing, binary search can be used for large N-s
    const SrcFile* last_file = &file_bounds[0].second;
    for (const auto& [last_id_of_prev, file] : file_bounds) {
        if (loc_id <= last_id_of_prev)
            return *last_file;

        last_file = &file;
    }

    return *last_file;
}

} // namespace stc