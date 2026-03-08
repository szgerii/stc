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

#include "common/concepts.h"
#include "common/utils.h"

namespace stc::ir {

// basic types modelled after SPIR-V's unified specs
// some liberties have been taken for a more general IR type system
// this includes allowing non-float types as matrix component types,
// these can potentially be rewritten as arrays in the output (similarly to slang -> GLSL/SPIR-V)

class TypePool;

struct TypeId : public StrongId<uint16_t> {
    using StrongId::StrongId;

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
    uint32_t width;
    bool signedness;

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

    uint32_t width;
    Encoding enc{Encoding::ieee754};

    constexpr bool operator==(const FloatTD&) const = default;
};

struct VectorTD {
    TypeId component_type_id;
    uint32_t component_count;

    constexpr bool operator==(const VectorTD&) const = default;
};

struct MatrixTD {
    TypeId column_type_id;
    uint32_t column_count;

    constexpr bool operator==(const MatrixTD&) const = default;

    static std::pair<uint32_t, uint32_t> get_dims(TypeId mat_id, const TypePool& type_pool);
};

struct ArrayTD {
    TypeId element_type_id;
    uint32_t length;

    bool operator==(const ArrayTD&) const = default;
};

struct StructData {
    struct FieldInfo {
        TypeId type;
        std::string name;

        bool operator==(const FieldInfo&) const = default;
    };

    std::string name;
    std::vector<FieldInfo> fields;

    bool operator==(const StructData&) const = default;
};

struct StructTD {
    const StructData* data;

    explicit StructTD(const StructData* data)
        : data{data} {}

    bool operator==(const StructTD& other) const;
};

using TDVariantType =
    std::variant<VoidTD, BoolTD, IntTD, FloatTD, VectorTD, MatrixTD, ArrayTD, StructTD>;

template <typename T>
concept CTypeDescriptorTy =
    std::is_same_v<T, VoidTD> || std::is_same_v<T, BoolTD> || std::is_same_v<T, IntTD> ||
    std::is_same_v<T, FloatTD> || std::is_same_v<T, VectorTD> || std::is_same_v<T, MatrixTD> ||
    std::is_same_v<T, ArrayTD> || std::is_same_v<T, StructTD>;

struct TypeDescriptor {
    TDVariantType type_data;

    TypeDescriptor(const TypeDescriptor&)            = delete;
    TypeDescriptor(TypeDescriptor&&)                 = default;
    TypeDescriptor& operator=(const TypeDescriptor&) = delete;
    TypeDescriptor& operator=(TypeDescriptor&&)      = default;

    template <CTypeDescriptorTy T>
    constexpr bool is() const {
        return std::holds_alternative<T>(type_data);
    }

    constexpr bool is_void() const { return is<VoidTD>(); }
    constexpr bool is_scalar() const { return is<BoolTD>() || is<IntTD>() || is<FloatTD>(); }
    constexpr bool is_vector() const { return is<VectorTD>(); }
    constexpr bool is_matrix() const { return is<MatrixTD>(); }
    constexpr bool is_array() const { return is<ArrayTD>(); }
    constexpr bool is_custom() const { return is<StructTD>(); }

    template <CTypeDescriptorTy T>
    constexpr const T& as() const {
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

} // namespace stc::ir

template <>
struct std::hash<stc::ir::TypeId> {
    size_t operator()(const stc::ir::TypeId& x) const noexcept {
        return std::hash<stc::ir::TypeId::id_type>{}(x.value);
    }
};

template <>
struct std::hash<stc::ir::VoidTD> {
    size_t operator()(const stc::ir::VoidTD&) const noexcept { return 0; }
};

template <>
struct std::hash<stc::ir::BoolTD> {
    size_t operator()(const stc::ir::BoolTD&) const noexcept { return 0; }
};

template <>
struct std::hash<stc::ir::IntTD> {
    size_t operator()(const stc::ir::IntTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.width), x.signedness);
    }
};

template <>
struct std::hash<stc::ir::FloatTD> {
    size_t operator()(const stc::ir::FloatTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.width), static_cast<uint8_t>(x.enc));
    }
};

template <>
struct std::hash<stc::ir::VectorTD> {
    size_t operator()(const stc::ir::VectorTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.component_count), x.component_type_id);
    }
};

template <>
struct std::hash<stc::ir::MatrixTD> {
    size_t operator()(const stc::ir::MatrixTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.column_count), x.column_type_id);
    }
};

template <>
struct std::hash<stc::ir::ArrayTD> {
    size_t operator()(const stc::ir::ArrayTD& x) const noexcept {
        return stc::hash_combine(std::hash<uint32_t>{}(x.length), x.element_type_id);
    }
};

template <>
struct std::hash<stc::ir::StructData::FieldInfo> {
    size_t operator()(const stc::ir::StructData::FieldInfo& x) const noexcept {
        return stc::hash_combine(std::hash<std::string>{}(x.name), x.type);
    }
};

template <>
struct std::hash<stc::ir::StructData> {
    size_t operator()(const stc::ir::StructData& x) const noexcept {
        size_t h = std::hash<std::string>{}(x.name);

        for (const auto& field : x.fields) {
            h = stc::hash_combine(h, field);
        }

        return h;
    }
};

template <>
struct std::hash<stc::ir::StructTD> {
    size_t operator()(const stc::ir::StructTD& x) const noexcept {
        const stc::ir::StructData& data = x.data != nullptr ? *x.data : stc::ir::StructData{"", {}};

        return std::hash<stc::ir::StructData>{}(data);
    }
};

static_assert(stc::CIsUnorderedMapKey<stc::ir::TDVariantType>);

#undef STD_HASH_SPEC
