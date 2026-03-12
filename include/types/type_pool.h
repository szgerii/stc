#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "common/bump_arena.h"
#include "types/type_descriptors.h"

namespace stc::types {

class TypePool final {
private:
    using ArenaTy = BumpArena<TypeId::id_type>;

public:
    // ! expects arena to be empty, and unused by others during its lifetime
    explicit TypePool(ArenaTy& arena)
        : arena{arena} {

        // NOTE: if these macros ever change, don't forget to modify this
        static_assert(TypeId::void_id() == TypeId{1U} && TypeId::bool_id() == TypeId{2U});

        // make sure arena IDs 1 and 2 will not be given out
        std::ignore = arena.allocate(2, 1);
        assert(arena.get_current_offset() >= 3);
    }

    explicit TypePool(ArenaTy& arena, std::vector<BuiltinTD> builtins)
        : TypePool{arena} {

        for (BuiltinTD td : builtins)
            std::ignore = insert_or_get(td, true);
    }

    TypePool(const TypePool&)            = delete;
    TypePool(TypePool&&)                 = delete;
    TypePool& operator=(const TypePool&) = delete;
    TypePool& operator=(TypePool&&)      = delete;

    const TypeDescriptor& get_td(TypeId id) const;

    template <CTypeDescriptorTy T>
    bool is_type_of(TypeId id) const {
        auto* td = static_cast<TypeDescriptor*>(arena.get_ptr(id));
        assert(td != nullptr);

        return td->is<T>();
    }

    inline TypeId size() const { return static_cast<TypeId>(pool.size()); }

    [[nodiscard]] static TypeId void_td() { return TypeId::void_id(); }
    [[nodiscard]] static TypeId bool_td() { return TypeId::bool_id(); }
    [[nodiscard]] TypeId int_td(uint32_t width, bool is_signed);
    [[nodiscard]] TypeId float_td(uint32_t width,
                                  FloatTD::Encoding encoding = FloatTD::Encoding::ieee754);
    [[nodiscard]] TypeId vector_td(TypeId component_type_id, uint32_t component_count);
    [[nodiscard]] TypeId matrix_td(TypeId column_type_id, uint32_t column_count);
    [[nodiscard]] TypeId array_td(TypeId element_type_id, uint32_t length);

    [[nodiscard]] TypeId builtin_td(uint8_t kind);

    template <CEnumOf<uint8_t> T>
    TypeId builtin_td(T kind) {
        return builtin_td(static_cast<uint8_t>(kind));
    }

    [[nodiscard]] TypeId get_struct_td(std::string_view name);
    TypeId make_struct_td(std::string name, std::vector<StructData::FieldInfo> fields);

private:
    TypeId get(TDVariantType type) const;
    TypeId insert_or_get(TDVariantType type, bool fail_on_get = false);

    ArenaTy& arena;
    std::unordered_map<TDVariantType, TypeId> pool;
    std::unordered_map<std::string_view, TypeId> struct_map;
};

} // namespace stc::types
