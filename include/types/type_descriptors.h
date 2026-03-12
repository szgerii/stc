#pragma once

#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

#include "common/utils.h"

namespace stc::types {

// basic types modelled after SPIR-V's unified specs
// some liberties have been taken for a more general sir type system
// this includes allowing non-float types as matrix component types,
// these can potentially be rewritten as arrays in the output (similarly to slang -> GLSL/SPIR-V)

class TypePool;

struct TypeId : public StrongId<uint16_t> {
    using StrongId::StrongId;

    bool constexpr is_null() const { return *this == null_id(); }

    static constexpr TypeId null_id() { return 0U; }
    static constexpr TypeId void_id() { return 1U; }
    static constexpr TypeId bool_id() { return 2U; }
};

struct VoidTD {
    constexpr bool operator==(const VoidTD&) const = default;
};

struct BoolTD {
    constexpr bool operator==(const BoolTD&) const = default;
};

struct IntTD {
    const uint32_t width;
    const bool is_signed;

    constexpr bool operator==(const IntTD&) const = default;
};

struct FloatTD {
    enum class Encoding : uint8_t { ieee754, bfloat16, f8e4m3, f8e5m2 };

    constexpr static std::optional<uint32_t> required_width(Encoding enc) {
        switch (enc) {
            case Encoding::ieee754:
                return std::nullopt;
            case Encoding::bfloat16:
                return 16;
            case Encoding::f8e4m3:
            case Encoding::f8e5m2:
                return 8;
        }

        throw std::logic_error{"Unaccounted float encoding in enumeration"};
    }

    const uint32_t width;
    const Encoding enc{Encoding::ieee754};

    constexpr bool operator==(const FloatTD&) const = default;
};

struct VectorTD {
    const TypeId component_type_id;
    const uint32_t component_count;

    constexpr bool operator==(const VectorTD&) const = default;
};

struct MatrixTD {
    struct MatrixInfo {
        const uint32_t rows;
        const uint32_t cols;
        const TypeId component_type;
    };

    const TypeId column_type_id;
    const uint32_t column_count;

    constexpr bool operator==(const MatrixTD&) const = default;

    static MatrixInfo get_info(TypeId mat_id, const TypePool& type_pool);
};

struct ArrayTD {
    const TypeId element_type_id;
    const uint32_t length;

    constexpr bool operator==(const ArrayTD&) const = default;

    static std::vector<uint32_t> get_dims(TypeId arr_id, const TypePool& type_pool);
};

struct StructData {
    struct FieldInfo {
        const TypeId type;
        const std::string name;

        constexpr bool operator==(const FieldInfo&) const = default;
    };

    const std::string name;
    const std::vector<FieldInfo> fields;

    constexpr bool operator==(const StructData&) const = default;
};

struct StructTD {
    const StructData* data;

    bool operator==(const StructTD& other) const;
};

struct BuiltinTD {
    const uint8_t kind;

    constexpr explicit BuiltinTD(uint8_t kind)
        : kind{kind} {}

    template <CEnumOf<uint8_t> T>
    constexpr explicit BuiltinTD(T kind)
        : kind{static_cast<uint8_t>(kind)} {}

    bool operator==(const BuiltinTD& other) const { return kind == other.kind; }
};

using TDVariantType =
    std::variant<VoidTD, BoolTD, IntTD, FloatTD, VectorTD, MatrixTD, ArrayTD, StructTD, BuiltinTD>;

// CLEANUP: use a variadic template instead
template <typename T>
concept CTypeDescriptorTy =
    std::is_same_v<T, VoidTD> || std::is_same_v<T, BoolTD> || std::is_same_v<T, IntTD> ||
    std::is_same_v<T, FloatTD> || std::is_same_v<T, VectorTD> || std::is_same_v<T, MatrixTD> ||
    std::is_same_v<T, ArrayTD> || std::is_same_v<T, StructTD> || std::is_same_v<T, BuiltinTD>;

struct TypeDescriptor {
    const TDVariantType type_data;

    TypeDescriptor(const TypeDescriptor&)                  = delete;
    TypeDescriptor(TypeDescriptor&&)                       = default;
    TypeDescriptor& operator=(const TypeDescriptor& other) = delete;
    TypeDescriptor& operator=(TypeDescriptor&&)            = delete;

    template <CTypeDescriptorTy T>
    constexpr bool is() const {
        return std::holds_alternative<T>(type_data);
    }

    constexpr bool is_void() const { return is<VoidTD>(); }
    constexpr bool is_scalar() const { return is<BoolTD>() || is<IntTD>() || is<FloatTD>(); }
    constexpr bool is_vector() const { return is<VectorTD>(); }
    constexpr bool is_matrix() const { return is<MatrixTD>(); }
    constexpr bool is_array() const { return is<ArrayTD>(); }
    constexpr bool is_struct() const { return is<StructTD>(); }
    constexpr bool is_builtin() const { return is<BuiltinTD>(); }

    template <CTypeDescriptorTy T>
    constexpr const T& as() const {
        assert(is<T>() && "invalid cast from TypeDescriptor to a CTypeDescriptorTy");
        return std::get<T>(type_data);
    }

    bool operator==(const TypeDescriptor&) const;

private:
    friend class TypePool;

    explicit TypeDescriptor(TDVariantType type_data)
        : type_data{std::move(type_data)} {}
};
static_assert(sizeof(TypeDescriptor) == sizeof(TDVariantType));

std::string to_string(const TypeDescriptor&, const TypePool&);
std::string to_string(TypeId, const TypePool&);

} // namespace stc::types

template <>
struct std::hash<stc::types::TypeId> {
    size_t operator()(const stc::types::TypeId& x) const noexcept {
        return std::hash<stc::types::TypeId::id_type>{}(x.value);
    }
};

template <>
struct std::hash<stc::types::VoidTD> {
    size_t operator()(const stc::types::VoidTD&) const noexcept { return 0; }
};

template <>
struct std::hash<stc::types::BoolTD> {
    size_t operator()(const stc::types::BoolTD&) const noexcept { return 0; }
};

template <>
struct std::hash<stc::types::IntTD> {
    size_t operator()(const stc::types::IntTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.width), x.is_signed);
    }
};

template <>
struct std::hash<stc::types::FloatTD> {
    size_t operator()(const stc::types::FloatTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.width), static_cast<uint8_t>(x.enc));
    }
};

template <>
struct std::hash<stc::types::VectorTD> {
    size_t operator()(const stc::types::VectorTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.component_count), x.component_type_id);
    }
};

template <>
struct std::hash<stc::types::MatrixTD> {
    size_t operator()(const stc::types::MatrixTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.column_count), x.column_type_id);
    }
};

template <>
struct std::hash<stc::types::ArrayTD> {
    size_t operator()(const stc::types::ArrayTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.length), x.element_type_id);
    }
};

template <>
struct std::hash<stc::types::StructData::FieldInfo> {
    size_t operator()(const stc::types::StructData::FieldInfo& x) const noexcept {
        return stc::hash_combine(std::hash<std::string>{}(x.name), x.type);
    }
};

template <>
struct std::hash<stc::types::StructData> {
    size_t operator()(const stc::types::StructData& x) const noexcept {
        size_t h = std::hash<std::string>{}(x.name);

        for (const auto& field : x.fields) {
            h = stc::hash_combine(h, field);
        }

        return h;
    }
};

template <>
struct std::hash<stc::types::StructTD> {
    size_t operator()(const stc::types::StructTD& x) const noexcept {
        const stc::types::StructData& data =
            x.data != nullptr ? *x.data : stc::types::StructData{"", {}};

        return std::hash<stc::types::StructData>{}(data);
    }
};

template <>
struct std::hash<stc::types::BuiltinTD> {
    size_t operator()(const stc::types::BuiltinTD& x) const noexcept {
        return std::hash<uint8_t>{}(x.kind);
    }
};

static_assert(stc::CHashable<stc::types::TDVariantType>);
static_assert(stc::CEqualityComparable<stc::types::TDVariantType>);
