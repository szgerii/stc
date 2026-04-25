#include "frontend/jl/type_conversion.h"
#include "frontend/jl/rt/utils.h"

namespace stc::jl {

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
jl_datatype_t* TypeToJLVisitor::visit_null_id() {
    return nullptr;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
jl_datatype_t* TypeToJLVisitor::visit([[maybe_unused]] VoidTD void_td) {
    return nullptr;
}

jl_datatype_t* TypeToJLVisitor::visit([[maybe_unused]] BoolTD bool_td) {
    return type_cache.bool_;
}

jl_datatype_t* TypeToJLVisitor::visit(IntTD int_td) {
    switch (int_td.width) {
        case 8:
            return int_td.is_signed ? type_cache.int8 : type_cache.uint8;
        case 16:
            return int_td.is_signed ? type_cache.int16 : type_cache.uint16;
        case 32:
            return int_td.is_signed ? type_cache.int32 : type_cache.uint32;
        case 64:
            return int_td.is_signed ? type_cache.int64 : type_cache.uint64;
        case 128:
            return int_td.is_signed ? type_cache.int128 : type_cache.uint128;

        default:
            return nullptr;
    }
}

jl_datatype_t* TypeToJLVisitor::visit(FloatTD float_td) {
    if (float_td.enc != FloatTD::Encoding::ieee754)
        return nullptr;

    switch (float_td.width) {
        case 16:
            return type_cache.float16;
        case 32:
            return type_cache.float32;
        case 64:
            return type_cache.float64;

        default:
            return nullptr;
    }
}

jl_datatype_t* TypeToJLVisitor::visit(VectorTD vec_td) {
    const auto& comp_td = type_pool.get_td(vec_td.component_type_id);
    if (comp_td.is<FloatTD>()) {
        FloatTD comp_ty = comp_td.as<FloatTD>();

        if (comp_ty.width == 32 && comp_ty.enc == FloatTD::Encoding::ieee754) {
            if (vec_td.component_count == 2)
                return type_cache.vec2;
            if (vec_td.component_count == 3)
                return type_cache.vec3;
            if (vec_td.component_count == 4)
                return type_cache.vec4;
        }
    }

    jl_value_t* n_tp = jl_box_long(vec_td.component_count);
    auto* t_tp       = reinterpret_cast<jl_value_t*>(dispatch(vec_td.component_type_id));

    jl_value_t* dt_v =
        jl_apply_type2(reinterpret_cast<jl_value_t*>(type_cache.vec_nt_ua), n_tp, t_tp);

    return safe_cast<jl_datatype_t>(dt_v);
}

jl_datatype_t* TypeToJLVisitor::visit([[maybe_unused]] MatrixTD mat_td) {
    // TODO
    return nullptr;
}

jl_datatype_t* TypeToJLVisitor::visit(ArrayTD arr_td) {
    if (!type_pool.get_td(arr_td.element_type_id).is_scalar())
        return nullptr;

    jl_datatype_t* el_type = dispatch(arr_td.element_type_id);
    if (el_type == nullptr)
        return nullptr;

    return type_cache.vector_of(el_type);
}

jl_datatype_t* TypeToJLVisitor::visit([[maybe_unused]] StructTD struct_td) {
    return nullptr;
}

jl_datatype_t* TypeToJLVisitor::visit([[maybe_unused]] FunctionTD fn_td) {
    return nullptr;
}

jl_datatype_t* TypeToJLVisitor::visit([[maybe_unused]] MethodTD method_td) {
    return nullptr;
}

jl_datatype_t* TypeToJLVisitor::visit(BuiltinTD builtin_td) {
    auto kind_of = [this](TypeId type) -> uint8_t {
        const auto& td = type_pool.get_td(type);

#ifndef NDEBUG
        if (!td.is_builtin())
            throw std::logic_error{"kind_of invoked on non-builtin type descriptor"};
#endif

        return td.as<BuiltinTD>().kind;
    };

    if (builtin_td.kind == kind_of(ctx.jl_Nothing_t()))
        return jl_nothing_type;

    if (builtin_td.kind == kind_of(ctx.jl_Symbol_t()))
        return jl_symbol_type;

    if (builtin_td.kind == kind_of(ctx.jl_String_t()))
        return jl_string_type;

    return nullptr;
}

TypeId parse_jl_type(jl_datatype_t* dt, JLCtx& ctx) {
    if (dt == nullptr)
        return TypeId::null_id();

    assert(jl_is_concrete_type(reinterpret_cast<jl_value_t*>(dt)) &&
           "parse_jl_type called with non-concrete datatype");

    const auto& type_cache = ctx.jl_env.type_cache;

    // clang-format off
    if (dt == jl_nothing_type)    return ctx.jl_Nothing_t();

    if (dt == type_cache.bool_)   return ctx.jl_Bool_t();

    if (dt == type_cache.int8)    return ctx.jl_Int8_t();
    if (dt == type_cache.uint8)   return ctx.jl_UInt8_t();
    if (dt == type_cache.int16)   return ctx.jl_Int16_t();
    if (dt == type_cache.uint16)  return ctx.jl_UInt16_t();
    if (dt == type_cache.int32)   return ctx.jl_Int32_t();
    if (dt == type_cache.uint32)  return ctx.jl_UInt32_t();
    if (dt == type_cache.int64)   return ctx.jl_Int64_t();
    if (dt == type_cache.uint64)  return ctx.jl_UInt64_t();
    if (dt == type_cache.int128)  return ctx.jl_Int128_t();
    if (dt == type_cache.uint128) return ctx.jl_UInt128_t();

    if (dt == type_cache.float16) return ctx.jl_Float16_t();
    if (dt == type_cache.float32) return ctx.jl_Float32_t();
    if (dt == type_cache.float64) return ctx.jl_Float64_t();
    // clang-format on

    if (rt::is_array_type(dt)) {
        jl_value_t* eltype_tp = jl_tparam0(dt);
        jl_value_t* dim_tp    = jl_tparam1(dt);

        if (jl_is_long(dim_tp) && jl_is_datatype(eltype_tp) && jl_unbox_long(dim_tp) == 1) {
            TypeId el_type = parse_jl_type(safe_cast<jl_datatype_t>(eltype_tp), ctx);
            return ctx.type_pool.any_array_td(el_type);
        }
    }

    bool is_vec_nt = is_spec_of(dt, type_cache.vec_nt_ua),
         is_vec_2t = is_spec_of(dt, type_cache.vec_2t_ua),
         is_vec_3t = is_spec_of(dt, type_cache.vec_3t_ua),
         is_vec_4t = is_spec_of(dt, type_cache.vec_4t_ua),
         is_vec    = is_vec_nt || is_vec_2t || is_vec_3t || is_vec_4t;

    // TODO: 32/64 bit env handling
    if (is_vec) {
        size_t n = 0;

        jl_value_t* t_tp = nullptr;
        if (is_vec_nt) {
            jl_value_t* n_tp = jl_tparam0(dt);
            t_tp             = jl_tparam1(dt);

            if (!jl_is_int64(n_tp))
                throw std::logic_error{"unexpected non-int64 type param in VecNT type's N"};

            int64_t n_i64 = jl_unbox_int64(n_tp);

            if (n_i64 < 1)
                throw std::runtime_error{"non-positive N value for VecNT's N type param"};

            static constexpr auto u32_max =
                static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
            if (n_i64 > u32_max)
                throw std::runtime_error{"N value for VecNT's N type param is too large"};

            n = static_cast<uint32_t>(n_i64);
        } else {
            t_tp = jl_tparam0(dt);

            if (is_vec_2t)
                n = 2;
            else if (is_vec_3t)
                n = 3;
            else if (is_vec_4t)
                n = 4;
        }

        assert(t_tp != nullptr);
        assert(n != 0);

        if (!jl_is_datatype(t_tp))
            throw std::logic_error{"unexpected non-datatype type param in VecNT type's T"};

        TypeId el_type = parse_jl_type(safe_cast<jl_datatype_t>(t_tp), ctx);

        if (!ctx.type_pool.get_td(el_type).is_scalar())
            throw std::logic_error{"non-scalar type in VecNT's T type param"};

        return ctx.type_pool.vector_td(el_type, static_cast<uint32_t>(n));
    }

    return TypeId::null_id();
}

} // namespace stc::jl
