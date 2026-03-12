#include "common/utils.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

#include "types/type_pool.h"

namespace stc::types {

const TypeDescriptor& TypePool::get_td(TypeId id) const {
    return *arena.get_ptr<TypeDescriptor>(id);
}

TypeId TypePool::get(TDVariantType type) const {
    auto it = pool.find(type);

    return it != pool.end() ? it->second : TypeId::null_id();
}

TypeId TypePool::insert_or_get(TDVariantType type, bool fail_on_get) {
    TypeId pooled_id = get(type);

    if (pooled_id != TypeId::null_id()) {
        if (fail_on_get)
            throw std::logic_error{"Type already in pool for insert_or_get call that should have "
                                   "resulted in an insertion"};

        return pooled_id;
    }

    auto [id, mem] = arena.allocate_for<TypeDescriptor>();
    new (mem) TypeDescriptor{type};

    pool[type] = id;
    return static_cast<TypeId>(id);
}

TypeId TypePool::int_td(uint32_t width, bool is_signed) {
    return insert_or_get(IntTD{.width = width, .is_signed = is_signed});
}

TypeId TypePool::float_td(uint32_t width, FloatTD::Encoding encoding) {
    std::optional<uint32_t> req_width = FloatTD::required_width(encoding);

    if (req_width && *req_width != width)
        throw std::invalid_argument{
            "Invalid floating point type width provided for the given encoding"};

    return insert_or_get(FloatTD{.width = width, .enc = encoding});
}

TypeId TypePool::vector_td(TypeId component_type_id, uint32_t component_count) {
    const TypeDescriptor& component_type = get_td(component_type_id);

    if (!component_type.is_scalar())
        throw std::logic_error{"Cannot create vector type with non-scalar component type"};

    if (component_count < 2)
        throw std::logic_error{"Cannot create vector type with component count less than 2"};

    return insert_or_get(
        VectorTD{.component_type_id = component_type_id, .component_count = component_count});
}

TypeId TypePool::matrix_td(TypeId column_type_id, uint32_t column_count) {
    const TypeDescriptor& column_type = get_td(column_type_id);

    if (!column_type.is<VectorTD>())
        throw std::logic_error{"Cannot create matrix type with non-vector column type"};

    if (column_count < 2)
        throw std::logic_error{"Cannot create matrix type with column count less than 2"};

    return insert_or_get(MatrixTD{.column_type_id = column_type_id, .column_count = column_count});
}

TypeId TypePool::array_td(TypeId element_type_id, uint32_t length) {
    const TypeDescriptor& element_type = get_td(element_type_id);

    if (element_type.is_void())
        throw std::logic_error{"Cannot create array type with void as element type"};

    return insert_or_get(ArrayTD{.element_type_id = element_type_id, .length = length});
}

TypeId TypePool::builtin_td(uint8_t kind) {
    TypeId id = get(BuiltinTD{kind});
    assert(!id.is_null() && "built-in type not found in pool");
    return id;
}

TypeId TypePool::get_struct_td(std::string_view name) {
    if (auto it = struct_map.find(name); it != struct_map.end())
        return it->second;

    return TypeId::null_id();
}

TypeId TypePool::make_struct_td(std::string name, std::vector<StructData::FieldInfo> fields) {
    if (name.empty())
        throw std::logic_error{"Cannot create struct type with an empty type name"};

    if (std::ranges::any_of(fields, [](const StructData::FieldInfo& f) { return f.name.empty(); }))
        throw std::logic_error{"Cannot create struct type with an empty field name"};

    if (has_duplicates(fields,
                       [](const StructData::FieldInfo& fi) -> std::string_view { return fi.name; }))
        throw std::logic_error{"Field names have to be unique inside structs"};

    if (struct_map.contains(name))
        throw std::logic_error{"Cannot create two structs with identical names"};

    auto [_, data_ptr] = arena.emplace<StructData>(std::move(name), std::move(fields));
    TypeId t_id        = insert_or_get(StructTD{data_ptr}, true);

    struct_map[data_ptr->name] = t_id;

    return t_id;
}

} // namespace stc::types