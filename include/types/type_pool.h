#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "common/bump_arena.h"
#include "types/type_descriptors.h"

namespace stc::types {

class TypePool final {
    using SizeTy  = TypeId::id_type;
    using ArenaTy = BumpArena<SizeTy>;

    TypeId any_func = TypeId::null_id();
    std::unordered_map<uint8_t, std::string> builtin_str_map{};

public:
    explicit TypePool(SizeTy initial_capacity_kb)
        : arena{static_cast<SizeTy>(initial_capacity_kb * 1024U)} {

        // handling void id as constant 1U should be fine as long as this holds,
        // because arena starts counting offsets from 1 (to allow a null state),
        // which means 1U points exactly to the beginning of the first Slab's buffer.
        // Slabs are allocated using make_unique, which uses new, so they have the below alignment.
        // so as long as the buffer's alignment is not less than TD's, we're fine. hopefully.
        static_assert(TypeId::void_id() == TypeId{1U} &&
                      alignof(TypeDescriptor) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);

        // doesn't use arena.emplace to keep TypeDescriptor ctors private to TDs and TypePool
        auto [offset, mem] = arena.allocate_for<TypeDescriptor>();
        new (mem) TypeDescriptor{VoidTD{}};

        // this is a run-time check for the same thing as above, as I don't trust my math
        // (or C++. or the OS.)
        assert(TypeId{offset} == TypeId::void_id() && "invalid void type id");
    }

    explicit TypePool(SizeTy initial_capacity, std::vector<BuiltinTD> builtins)
        : TypePool{initial_capacity} {

        for (BuiltinTD td : builtins)
            std::ignore = insert_or_get(td, true);
    }

    TypePool(const TypePool&)            = delete;
    TypePool& operator=(const TypePool&) = delete;
    TypePool(TypePool&&)                 = default;
    TypePool& operator=(TypePool&&)      = default;

    const TypeDescriptor& get_td(TypeId id) const;

    template <CTypeDescriptorTy T>
    bool is_type_of(TypeId id) const {
        if (id.is_null())
            return false;

        const auto* td = arena.get_ptr<TypeDescriptor>(id);

        if (td == nullptr)
            return false;

        return td->is<T>();
    }

    [[nodiscard]] static TypeId void_td() { return TypeId::void_id(); }
    [[nodiscard]] TypeId bool_td();
    [[nodiscard]] TypeId int_td(uint32_t width, bool is_signed);
    [[nodiscard]] TypeId float_td(uint32_t width,
                                  FloatTD::Encoding encoding = FloatTD::Encoding::ieee754);
    [[nodiscard]] TypeId vector_td(TypeId component_type_id, uint32_t component_count);
    [[nodiscard]] TypeId matrix_td(TypeId column_type_id, uint32_t column_count);
    [[nodiscard]] TypeId array_td(TypeId element_type_id, uint32_t length);
    [[nodiscard]] TypeId method_td(TypeId ret_type, const std::vector<TypeId>& param_types);
    [[nodiscard]] TypeId func_td(SymbolId fn_name);
    [[nodiscard]] TypeId any_func_td();

    [[nodiscard]] bool is_any_func(TypeId type) const;
    // compares based on pointer equality
    [[nodiscard]] bool is_any_func(const TypeDescriptor* fn_td) const;

    [[nodiscard]] TypeId any_array_td(TypeId el_type);
    [[nodiscard]] TypeId any_vec_td(TypeId el_type);
    [[nodiscard]] TypeId any_mat_td(TypeId el_type);

    [[nodiscard]] bool is_array_any_size(TypeId type) const {
        const auto& td = get_td(type);
        return td.is_array() && td.as<ArrayTD>().length == std::numeric_limits<uint32_t>::max();
    }

    [[nodiscard]] bool is_vec_any_size(TypeId type) const {
        const auto& td = get_td(type);
        return td.is_vector() &&
               td.as<VectorTD>().component_count == std::numeric_limits<uint32_t>::max();
    }

    [[nodiscard]] bool is_mat_any_size(TypeId type) const {
        const auto& td = get_td(type);
        return td.is_matrix() &&
               td.as<MatrixTD>().column_count == std::numeric_limits<uint32_t>::max();
    }

    [[nodiscard]] TypeId builtin_td(BuiltinKind kind);

    template <CEnumOf<BuiltinKind> T>
    TypeId builtin_td(T kind) {
        return builtin_td(static_cast<BuiltinKind>(kind));
    }

    [[nodiscard]] TypeId get_struct_td(SymbolId name);
    TypeId make_struct_td(SymbolId name, std::vector<StructData::FieldInfo> fields,
                          const SymbolPool& sym_pool);

    [[nodiscard]] TypeId el_type_of(const TypeDescriptor& td) const;

    [[nodiscard]] TypeId el_type_of(TypeId id) const { return el_type_of(get_td(id)); }

    void register_builtin_str(BuiltinKind kind, std::string str);
    template <CEnumOf<BuiltinKind> T>
    void register_builtin_str(T kind, std::string str) {
        return register_builtin_str(static_cast<BuiltinKind>(kind), str);
    }

    void clear_builtin_str_map();

    std::string builtin_kind_to_str(BuiltinKind kind) const;
    template <CEnumOf<BuiltinKind> T>
    std::string builtin_kind_to_str(T kind) {
        return builtin_kind_to_str(static_cast<BuiltinKind>(kind));
    }

private:
    TypeId get(TDVariantType type) const;
    TypeId insert_or_get(TDVariantType type, bool fail_on_get = false);

    ArenaTy arena;
    std::unordered_map<TDVariantType, TypeId> pool;
    std::unordered_map<SymbolId, TypeId> struct_map;
};

} // namespace stc::types
