#include "backend/glsl/target_info.h"
#include "backend/glsl/builtin_data.h"
#include "types/type_to_string.h"

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

TargetInfo::GlobalList GLSLTargetInfo::create_globals(GLSLTypes& types) {
    TargetInfo::GlobalList globals = {
        // clang-format off
        // VERTEX SHADER (7.1.1)
        {"gl_VertexID",             types.gl_int},
        {"gl_InstanceID",           types.gl_int},
        {"gl_DrawID",               types.gl_int},
        {"gl_BaseVertex",           types.gl_int},
        {"gl_BaseInstance",         types.gl_int},
        {"gl_Position",             types.gl_vec4},
        {"gl_PointSize",            types.gl_float},
        {"gl_ClipDistance",         types.type_pool.any_array_td(types.gl_float)},
        {"gl_CullDistance",         types.type_pool.any_array_td(types.gl_float)},

        // TESSELLATION CONTROL/EVALUATION SHADER (7.1.2-7.1.3)
        {"gl_PatchVerticesIn",      types.gl_int},
        {"gl_PrimitiveID",          types.gl_int},
        {"gl_InvocationID",         types.gl_int},
        {"gl_TessLevelOuter",       types.gl_array(types.gl_float, 4)},
        {"gl_TessLevelInner",       types.gl_array(types.gl_float, 2)},
        {"gl_TessCoord",            types.gl_vec3},

        // GEOMETRY SHADER (7.1.4)
        {"gl_PrimitiveIDIn",        types.gl_int},
        {"gl_Layer",                types.gl_int},
        {"gl_ViewportIndex",        types.gl_int},

        // FRAGMENT SHADER (7.1.5)
        {"gl_FragCoord",            types.gl_vec4},
        {"gl_FrontFacing",          types.gl_bool},
        {"gl_PointCoord",           types.gl_vec2},
        {"gl_HelperInvocation",     types.gl_bool},
        {"gl_FragDepth",            types.gl_float},
        {"gl_SampleID",             types.gl_int},
        {"gl_SamplePosition",       types.gl_vec2},
        {"gl_SampleMaskIn",         types.type_pool.any_array_td(types.gl_int)},
        {"gl_SampleMask",           types.type_pool.any_array_td(types.gl_int)},

        // COMPUTE SHADERS (7.1.6)
        {"gl_NumWorkGroups",        types.gl_uvec3},
        {"gl_WorkGroupSize",        types.gl_uvec3},
        {"gl_WorkGroupID",          types.gl_uvec3},
        {"gl_LocalInvocationID",    types.gl_uvec3},
        {"gl_GlobalInvocationID",   types.gl_uvec3},
        {"gl_LocalInvocationIndex", types.gl_uint},

        // Compatibility Profile Built-In Language Variables (7.1.7)
        {"gl_ClipVertex;",                               types.gl_vec4},
        {"vec4 gl_FrontColor",                           types.gl_vec4},
        {"vec4 gl_BackColor",                            types.gl_vec4},
        {"vec4 gl_FrontSecondaryColor",                  types.gl_vec4},
        {"vec4 gl_BackSecondaryColor",                   types.gl_vec4},
        {"vec4 gl_TexCoord",                             types.gl_vec4},
        {"float gl_FogFragCoord",                        types.gl_float},

        // Built-In Constans (7.3)
        {"gl_MaxVertexAttribs",                          types.gl_int},
        {"gl_MaxVertexUniformVectors",                   types.gl_int},
        {"gl_MaxVertexUniformComponents",                types.gl_int},
        {"gl_MaxVertexOutputComponents",                 types.gl_int},
        {"gl_MaxVaryingComponents",                      types.gl_int},
        {"gl_MaxVaryingVectors",                         types.gl_int},
        {"gl_MaxVertexTextureImageUnits",                types.gl_int},
        {"gl_MaxVertexImageUniforms",                    types.gl_int},
        {"gl_MaxVertexAtomicCounters",                   types.gl_int},
        {"gl_MaxVertexAtomicCounterBuffers",             types.gl_int},
        {"gl_MaxTessPatchComponents",                    types.gl_int},
        {"gl_MaxPatchVertices",                          types.gl_int},
        {"gl_MaxTessGenLevel",                           types.gl_int},
        {"gl_MaxTessControlInputComponents",             types.gl_int},
        {"gl_MaxTessControlOutputComponents",            types.gl_int},
        {"gl_MaxTessControlTextureImageUnits",           types.gl_int},
        {"gl_MaxTessControlUniformComponents",           types.gl_int},
        {"gl_MaxTessControlTotalOutputComponents",       types.gl_int},
        {"gl_MaxTessControlImageUniforms",               types.gl_int},
        {"gl_MaxTessControlAtomicCounters",              types.gl_int},
        {"gl_MaxTessControlAtomicCounterBuffers",        types.gl_int},
        {"gl_MaxTessEvaluationInputComponents",          types.gl_int},
        {"gl_MaxTessEvaluationOutputComponents",         types.gl_int},
        {"gl_MaxTessEvaluationTextureImageUnits",        types.gl_int},
        {"gl_MaxTessEvaluationUniformComponents",        types.gl_int},
        {"gl_MaxTessEvaluationImageUniforms",            types.gl_int},
        {"gl_MaxTessEvaluationAtomicCounters",           types.gl_int},
        {"gl_MaxTessEvaluationAtomicCounterBuffers",     types.gl_int},
        {"gl_MaxGeometryInputComponents",                types.gl_int},
        {"gl_MaxGeometryOutputComponents",               types.gl_int},
        {"gl_MaxGeometryImageUniforms",                  types.gl_int},
        {"gl_MaxGeometryTextureImageUnits",              types.gl_int},
        {"gl_MaxGeometryOutputVertices",                 types.gl_int},
        {"gl_MaxGeometryTotalOutputComponents",          types.gl_int},
        {"gl_MaxGeometryUniformComponents",              types.gl_int},
        {"gl_MaxGeometryVaryingComponents",              types.gl_int},
        {"gl_MaxGeometryAtomicCounters",                 types.gl_int},
        {"gl_MaxGeometryAtomicCounterBuffers",           types.gl_int},
        {"gl_MaxFragmentImageUniforms",                  types.gl_int},
        {"gl_MaxFragmentInputComponents",                types.gl_int},
        {"gl_MaxFragmentUniformVectors",                 types.gl_int},
        {"gl_MaxFragmentUniformComponents",              types.gl_int},
        {"gl_MaxFragmentAtomicCounters",                 types.gl_int},
        {"gl_MaxFragmentAtomicCounterBuffers",           types.gl_int},
        {"gl_MaxDrawBuffers",                            types.gl_int},
        {"gl_MaxTextureImageUnits",                      types.gl_int},
        {"gl_MinProgramTexelOffset",                     types.gl_int},
        {"gl_MaxProgramTexelOffset",                     types.gl_int},
        {"gl_MaxImageUnits",                             types.gl_int},
        {"gl_MaxSamples",                                types.gl_int},
        {"gl_MaxImageSamples",                           types.gl_int},
        {"gl_MaxClipDistances",                          types.gl_int},
        {"gl_MaxCullDistances",                          types.gl_int},
        {"gl_MaxViewports",                              types.gl_int},
        {"gl_MaxComputeImageUniforms",                   types.gl_int},
        {"gl_MaxComputeWorkGroupCount",                  types.gl_ivec3},
        {"gl_MaxComputeWorkGroupSize",                   types.gl_ivec3},
        {"gl_MaxComputeUniformComponents",               types.gl_int},
        {"gl_MaxComputeTextureImageUnits",               types.gl_int},
        {"gl_MaxComputeAtomicCounters",                  types.gl_int},
        {"gl_MaxComputeAtomicCounterBuffers",            types.gl_int},
        {"gl_MaxCombinedTextureImageUnits",              types.gl_int},
        {"gl_MaxCombinedImageUniforms",                  types.gl_int},
        {"gl_MaxCombinedImageUnitsAndFragmentOutputs",   types.gl_int},
        {"gl_MaxCombinedShaderOutputResources",          types.gl_int},
        {"gl_MaxCombinedAtomicCounters",                 types.gl_int},
        {"gl_MaxCombinedAtomicCounterBuffers",           types.gl_int},
        {"gl_MaxCombinedClipAndCullDistances",           types.gl_int},
        {"gl_MaxAtomicCounterBindings",                  types.gl_int},
        {"gl_MaxAtomicCounterBufferSize",                types.gl_int},
        {"gl_MaxTransformFeedbackBuffers",               types.gl_int},
        {"gl_MaxTransformFeedbackInterleavedComponents", types.gl_int},
        // clang-format on
    };

    // sort alphabetically
    std::ranges::sort(
        globals, [](const BuiltinGlobal& a, const BuiltinGlobal& b) { return a.name < b.name; });

    return globals;
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
