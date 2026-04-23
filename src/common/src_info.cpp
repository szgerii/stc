#include <stdexcept>

#include "common/src_info.h"
#include "common/term_utils.h"

namespace {
using stc::SrcFile, stc::SrcLocation, stc::TerminalInfo;

void print_locinfo(const SrcFile& file, SrcLocation location, std::ostream& out,
                   std::string_view color = ansi_codes::reset) {
    if (TerminalInfo::supports_color())
        out << color;

    out << '[' << file.path << ':' << location.line << ':' << location.col << "] ";

    if (TerminalInfo::supports_color())
        out << ansi_codes::reset;
}

} // namespace

namespace stc {

void report(const SrcFile& file, SrcLocation location, std::string_view msg,
            std::string_view prefix, std::ostream& out) {
    // FEATURE: print code snippet

    print_locinfo(file, location, out);
    return report(msg, prefix, out);
}

void report(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg,
            std::string_view prefix, std::ostream& out) {

    auto [loc, file] = pool.get_loc_and_file(loc_id);
    return report(file, loc, msg, prefix, out);
}

void error(const SrcFile& file, SrcLocation location, std::string_view msg, std::ostream& out) {
    print_locinfo(file, location, out, ansi_codes::red);
    return error(msg, out);
}

void error(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg, std::ostream& out) {
    auto [loc, file] = pool.get_loc_and_file(loc_id);
    return error(file, loc, msg, out);
}

void warning(const SrcFile& file, SrcLocation location, std::string_view msg, std::ostream& out) {
    print_locinfo(file, location, out, ansi_codes::yellow);
    return warning(msg, out);
}

void warning(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg,
             std::ostream& out) {
    auto [loc, file] = pool.get_loc_and_file(loc_id);
    return warning(file, loc, msg, out);
}

void internal_error(const SrcFile& file, SrcLocation location, std::string_view msg,
                    std::ostream& out) {
    print_locinfo(file, location, out, ansi_codes::red);
    return internal_error(msg, out);
}

void internal_error(const SrcInfoPool& pool, SrcLocationId loc_id, std::string_view msg,
                    std::ostream& out) {
    auto [loc, file] = pool.get_loc_and_file(loc_id);
    return internal_error(file, loc, msg, out);
}

SrcInfoPool::SrcInfoPool(SizeTy initial_location_capacity_kb, size_t initial_file_capacity)
    : arena{initial_location_capacity_kb * 1024U},
      arena_alloc{&arena},
      file_bounds{},
      last_loc_id{SrcLocationId::null_id()} {
    file_bounds.reserve(initial_file_capacity);

    // invalid location and file states
    std::ignore = get_file("SRC FILE INFO UNAVAILABLE");
}

SrcLocationId SrcInfoPool::get_location(intptr_t line, uint32_t col) {
    if (!arena_alloc.can_allocate()) {
        warning("Invalid source location information returned from SrcInfoPool, line/col numbers "
                "printed after this might not be accurate. The processed file might be too large.");

        return SrcLocationId::null_id();
    }

    // TODO: return interred loc, for same file only

    SrcLocationId id = SrcLocationId::null_id();

    std::tie(id, std::ignore) = arena_alloc.emplace(line, col);

    last_loc_id = id;
    return id;
}

size_t SrcInfoPool::get_file(std::string path) {
    if (!file_bounds.empty() && file_bounds[file_bounds.size() - 1].second.path == path)
        return file_bounds.size() - 1;

    file_bounds.emplace_back(last_loc_id, SrcFile{std::move(path)});

    return file_bounds.size() - 1;
}

SrcLocation SrcInfoPool::get_location(SrcLocationId loc_id) const {
    if (loc_id.is_null())
        return SrcLocation::null();

    auto* loc = arena.get_ptr<SrcLocation>(loc_id);

    if (loc == nullptr)
        return SrcLocation::null();

    return *loc;
}

const SrcFile& SrcInfoPool::get_file_for_location(SrcLocationId loc_id) const {
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

std::pair<SrcLocation, const SrcFile&> SrcInfoPool::get_loc_and_file(SrcLocationId id) const {
    return {get_location(id), get_file_for_location(id)};
}

} // namespace stc