#pragma once

#include "types/qualifier_pool.h"
#include "types/type_descriptors.h"
#include "types/type_pool.h"

namespace stc::glsl {

using namespace stc::types;

inline std::string type_prefix(const TypeDescriptor& td) {
    assert(td.is_scalar() && "trying to get prefix of non-scalar type");

    if (td.is<BoolTD>())
        return "b";

    if (td.is<IntTD>())
        return td.as<IntTD>().is_signed ? "i" : "u";

    if (td.is<FloatTD>())
        return td.as<FloatTD>().width == 32 ? "" : "d";

    return "?";
}

std::string type_str(const TypeDescriptor& td, const TypePool& pool, const SymbolPool& sym_pool);

inline std::string type_str(TypeId type_id, const TypePool& type_pool, const SymbolPool& sym_pool) {
    return type_str(type_pool.get_td(type_id), type_pool, sym_pool);
}

inline std::string_view qual_kind_to_str(QualKind kind) {
    switch (kind) {

#define X(name, ...)                                                                               \
    case (QualKind::tq_##name):                                                                    \
        return #name;

#include "types/qualifier_defs/non_layout.def"

#undef X
#define X(name, ...)                                                                               \
    case (QualKind::lq_##name):                                                                    \
        return #name;

#include "types/qualifier_defs/layout_value.def"
#include "types/qualifier_defs/layout_valueless.def"

        default:
            throw std::logic_error{"unaccounted QualifierKind in qual_kind_to_str"};
    }

#undef X
}

inline uint8_t qual_rank(QualKind kind) {
#define X(name, cat, rank)                                                                         \
    case QualKind::tq_##name:                                                                      \
        return rank;

    switch (kind) {
#include "types/qualifier_defs/non_layout.def"

        default:
            assert(is_layout_qual(kind));
            return 2;
    }

#undef X
}

} // namespace stc::glsl
