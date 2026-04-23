#include "backend/glsl/target_info.h"
#include "backend/glsl/builtin_data.h"

#include <algorithm>

namespace stc::glsl {

namespace detail {
using namespace stc::glsl::builtins;

TypeId builtin_ty_to_id(BuiltinType ty, GLSLTypes& types) {
    using enum BuiltinType;

    assert(!is_generic_type(ty) && "generic type not unrolled during builtin parsing");

    if (ty == Void)
        return types.gl_void;

    if (ty == Float)
        return types.gl_float;

    if (ty == Double)
        return types.gl_double;

    if (ty == Int)
        return types.gl_int;

    if (ty == UInt)
        return types.gl_uint;

    if (ty == Bool)
        return types.gl_bool;

    if (is_vec(ty)) {
        BuiltinType el_type_ = el_type(ty);
        TypeId el_type_id    = builtin_ty_to_id(el_type_, types);
        return types.gl_TvecN(el_type_id, el_count(ty));
    }

    if (is_mat(ty)) {
        BuiltinType el_type_ = el_type(ty);
        TypeId el_type_id    = builtin_ty_to_id(el_type_, types);

        uint8_t col_count_ = col_count(ty);
        uint8_t row_count_ = row_count(ty);

        return types.gl_TmatNxM(el_type_id, col_count_, row_count_);
    }

    throw std::logic_error{"Invalid signature in GLSL's builtin function list"};
}

} // namespace detail

GLSLTargetInfo GLSLTargetInfo::create_instance(GLSLTypes types) {
    GlobalList globals = create_globals(types);
    FnList functions   = create_fns(types);

    // globals and functions should already be stored in alphabetical order name-wise in the binary
    // so parsing them in sequential order should already yield a sorted result
    // (if not, an assert will catch it in the base class)

    return GLSLTargetInfo{std::move(globals), std::move(functions), std::move(types)};
}

TargetInfo::GlobalList GLSLTargetInfo::create_globals(GLSLTypes& types) {
    return {
        // clang-format off
        {"gl_FragCoord",   types.gl_vec4},
        {"gl_FragDepth",   types.gl_float},
        {"gl_FrontFacing", types.gl_bool},
        {"gl_InstanceID",  types.gl_int},
        {"gl_Position",    types.gl_vec4},
        // clang-format on
    };
}

TargetInfo::FnList GLSLTargetInfo::create_fns(GLSLTypes& types) {
    FnList fns{};
    fns.reserve(builtins::builtin_fns.size());

    for (const auto& fn : builtins::builtin_fns) {
        OverloadList overloads{};
        overloads.reserve(fn.overloads.size());

        for (const auto& overload : fn.overloads) {
            TypeList arg_types{};
            arg_types.reserve(overload.arg_count);

            for (size_t i = 0; i < overload.arg_count; i++)
                arg_types.emplace_back(detail::builtin_ty_to_id(overload.args[i], types));

            overloads.emplace_back(std::move(arg_types),
                                   detail::builtin_ty_to_id(overload.ret_ty, types));
        }

        fns.emplace_back(fn.name, std::move(overloads));
    }

    return fns;
}

bool GLSLTargetInfo::valid_ctor_call(TypeId target, const TypeList& arg_types) const {
    if (target.is_null())
        return false;

    if (!gl_types.is_gl_type(target) || target == gl_types.gl_void || arg_types.empty())
        return false;

    if (arg_types.size() == 1 && gl_types.is_gl_scalar_type(arg_types[0]))
        return true;

    // matrix from matrix is always allowed
    bool is_target_mat = gl_types.is_gl_mat_type(target);
    if (is_target_mat && gl_types.is_gl_mat_type(arg_types[0]))
        return arg_types.size() == 1;

    auto get_comps = [this](TypeId ty) -> uint8_t {
        assert(!ty.is_null());
        assert(gl_types.is_gl_type(ty));
        assert(ty != gl_types.gl_void);

        const auto& td = gl_types.type_pool.get_td(ty);

        if (td.is_scalar())
            return 1;

        if (td.is_vector()) {
            return static_cast<uint8_t>(td.as<VectorTD>().component_count);
        }

        if (td.is_matrix()) {
            MatrixTD mat_td = td.as<MatrixTD>();
            assert(!mat_td.column_type_id.is_null());

            const auto& col_td = gl_types.type_pool.get_td(mat_td.column_type_id);
            assert(col_td.is_vector());

            return static_cast<uint8_t>(mat_td.column_count *
                                        col_td.as<VectorTD>().component_count);
        }

        return 0;
    };

    uint8_t req_comps = get_comps(target);
    assert(req_comps > 0);

    uint8_t actual_comps = 0;
    for (TypeId arg : arg_types) {
        // overspecifying
        if (actual_comps >= req_comps)
            return false;

        if (!gl_types.is_gl_type(arg) || arg == gl_types.gl_void)
            return false;

        // "If a matrix argument is given to a matrix constructor, it is a compile-time error to
        // have any other arguments."
        if (is_target_mat && gl_types.is_gl_mat_type(arg))
            return false;

        uint8_t arg_comps = get_comps(arg);
        if (arg_comps == 0)
            return false;

        actual_comps += arg_comps;
    }

    return actual_comps >= req_comps;
}

// implemented according to the table in the 4.1.10 (implicit conversions) section of the specs
bool GLSLTargetInfo::can_implicit_cast(TypeId src_ty, TypeId dest_ty) const {
    const auto& glt = gl_types;

    const auto can_cast_scalar = [&glt](TypeId src, TypeId dest) {
        assert(glt.is_gl_scalar_type(dest));

        if (dest == glt.gl_uint)
            return src == glt.gl_int;

        if (dest == glt.gl_float)
            return src == glt.gl_int || src == glt.gl_uint;

        if (dest == glt.gl_double)
            return src == glt.gl_int || src == glt.gl_uint || src == glt.gl_float;

        return false;
    };

    if (!glt.is_gl_type(src_ty) || !glt.is_gl_type(dest_ty))
        return false;

    if (src_ty == dest_ty)
        return true;

    // int -> uint
    // int, uint -> float
    // int, uint, float -> double
    if (glt.is_gl_scalar_type(dest_ty))
        return can_cast_scalar(src_ty, dest_ty);

    const auto& src_td  = glt.type_pool.get_td(src_ty);
    const auto& dest_td = glt.type_pool.get_td(dest_ty);

    // ivecn -> uvecn
    // ivecn, uvecn -> vecn
    // ivecn, uvecn, vecn -> dvecn
    if (src_td.is_vector() && dest_td.is_vector()) {
        VectorTD src_vec  = src_td.as<VectorTD>();
        VectorTD dest_vec = dest_td.as<VectorTD>();

        if (src_vec.component_count != dest_vec.component_count)
            return false;

        return can_cast_scalar(src_vec.component_type_id, dest_vec.component_type_id);
    }

    // matnxm -> dmatnxm
    if (src_td.is_matrix() && dest_td.is_matrix()) {
        MatrixTD src_mat  = src_td.as<MatrixTD>();
        MatrixTD dest_mat = dest_td.as<MatrixTD>();

        if (src_mat.column_count != dest_mat.column_count)
            return false;

        VectorTD src_col  = glt.type_pool.get_td(src_mat.column_type_id).as<VectorTD>();
        VectorTD dest_col = glt.type_pool.get_td(dest_mat.column_type_id).as<VectorTD>();

        if (src_col.component_count != dest_col.component_count)
            return false;

        return src_col.component_type_id == glt.gl_float &&
               dest_col.component_type_id == glt.gl_double;
    }

    return false;
}

} // namespace stc::glsl
