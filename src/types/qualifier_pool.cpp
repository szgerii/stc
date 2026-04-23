#include "types/qualifier_pool.h"

namespace stc::types {

std::pair<QualId, DeclQualifiers&> QualifierPool::emplace(std::vector<QualKind> quals,
                                                          LQPayload layout_payloads) {
    auto [qual_id, decl_qual] = arena.emplace<DeclQualifiers>(std::move(quals), layout_payloads);

    assert(decl_qual != nullptr);

    return {QualId{qual_id}, *decl_qual};
}

const DeclQualifiers& QualifierPool::get_quals(QualId id) const {
    static DeclQualifiers no_quals{{}, {}};

    if (id.is_null())
        return no_quals;

    auto* qual_ptr = static_cast<DeclQualifiers*>(arena.get_ptr(id));

    if (qual_ptr == nullptr)
        throw std::logic_error{"trying to retrieve qualifier object not in arena"};

    return *qual_ptr;
}

} // namespace stc::types
