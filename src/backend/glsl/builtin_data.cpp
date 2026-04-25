#include "backend/glsl/builtin_data.h"

#include <algorithm>
#include <array>

namespace {
using namespace stc::glsl::builtins;

consteval BuiltinType try_resolve_gen_el_type(BuiltinType t) {
    using enum BuiltinType;

    if (FirstGenericFloat <= t && t <= LastGenericFloat)
        return Float;

    if (FirstGenericDouble <= t && t <= LastGenericDouble)
        return Double;

    if (FirstGenericBool <= t && t <= LastGenericBool)
        return Bool;

    if (FirstGenericInt <= t && t <= LastGenericInt)
        return Int;

    if (FirstGenericUInt <= t && t <= LastGenericUInt)
        return UInt;

    return t;
}

consteval BuiltinType try_resolve_generic(BuiltinType t, uint8_t n) {
    using enum BuiltinType;

    if (n == 0 || n > 4)
        throw "try_resolve_generic called with invalid n (constraint: 0 < n <= 4)";

    if (is_genvec(t) && n == 1)
        throw "trying to resolve vector generic with n = 1 (only allowed for GenTypes)";

    BuiltinType el_type = try_resolve_gen_el_type(t);

    if (el_type == t)
        return t;

    if (el_type == Float)
        return std::array{Float, Vec2F, Vec3F, Vec4F}[n - 1];

    if (el_type == Double)
        return std::array{Double, Vec2D, Vec3D, Vec4D}[n - 1];

    if (el_type == Bool)
        return std::array{Bool, Vec2B, Vec3B, Vec4B}[n - 1];

    if (el_type == Int)
        return std::array{Int, Vec2I, Vec3I, Vec4I}[n - 1];

    if (el_type == UInt)
        return std::array{UInt, Vec2U, Vec3U, Vec4U}[n - 1];

    throw "invalid el_type inferred in try_resolve_generic";
}

consteval uint8_t expansion_count(const BuiltinFnSig& sig) {
    bool has_gentype = is_gentype(sig.ret_ty);
    bool has_genvec  = is_genvec(sig.ret_ty);

    for (size_t i = 0; i < sig.arg_count; i++) {
        has_gentype = has_gentype || is_gentype(sig.args[i]);
        has_genvec  = has_genvec || is_genvec(sig.args[i]);
    }

    if (has_gentype && has_genvec)
        throw "invalid mixing of GenTypes and GenVec types in function signature";

    return has_gentype ? 4 : (has_genvec ? 3 : 1);
}

consteval size_t count_unrolled_sigs(std::span<const BuiltinFn> compressed_fns) {
    size_t total = 0;
    for (const auto& fn : compressed_fns) {
        for (const auto& sig : fn.overloads)
            total += expansion_count(sig);
    }

    return total;
}

template <size_t FnCount, size_t SigCount>
struct UnrolledData {
    struct FnDesc {
        std::string_view name;
        size_t sig_offset = 0;
        size_t sig_count  = 0;
    };

    std::array<BuiltinFnSig, SigCount> sigs;
    std::array<FnDesc, FnCount> fn_descs;
};

template <size_t FnCount, size_t SigCount>
consteval UnrolledData<FnCount, SigCount>
gen_unrolled_data(std::span<const BuiltinFn> compressed_fns) {
    UnrolledData<FnCount, SigCount> data{};

    size_t data_sig_idx = 0;
    size_t data_fn_idx  = 0;

    for (const auto& comp_fn : compressed_fns) {
        size_t first_sig_idx = data_sig_idx;

        for (const auto& comp_sig : comp_fn.overloads) {
            uint8_t exp_count   = expansion_count(comp_sig);
            uint8_t first_width = (exp_count == 3) ? 2 : 1; // skip scalar-expansion for genvec-s
            uint8_t max_width   = (exp_count != 1) ? 4 : 1;

            for (uint8_t width = first_width; width <= max_width; width++) {
                BuiltinFnSig unrolled_sig = comp_sig;

                unrolled_sig.ret_ty = try_resolve_generic(comp_sig.ret_ty, width);

                for (size_t i = 0; i < unrolled_sig.arg_count; i++)
                    unrolled_sig.args[i] = try_resolve_generic(unrolled_sig.args[i], width);

                data.sigs[data_sig_idx] = unrolled_sig;
                data_sig_idx++;
            }
        }

        data.fn_descs[data_fn_idx] = {comp_fn.name, first_sig_idx, data_sig_idx - first_sig_idx};
        data_fn_idx++;
    }

    return data;
}

template <size_t FnCount, size_t SigCount>
consteval std::array<BuiltinFn, FnCount>
gen_unrolled_fns(const UnrolledData<FnCount, SigCount>& data) {
    std::array<BuiltinFn, FnCount> fns{};

    for (size_t i = 0; i < FnCount; i++) {
        auto [fn_name, sig_offset, sig_count] = data.fn_descs[i];

        fns[i] =
            BuiltinFn{fn_name, std::span<const BuiltinFnSig>{&data.sigs[sig_offset], sig_count}};
    }

    return fns;
}

} // namespace

namespace stc::glsl::builtins {

namespace detail {

using enum BuiltinType;

#define X(name, ...) static constexpr BuiltinFnSig sigs_##name[] = {__VA_ARGS__};
#include "backend/glsl/builtin_defs/fn_sigs.def"
#undef X

// array of "compressed" fn signatures (e.g. GenF sin(GenF))
constexpr auto compressed_fn_data = std::to_array<BuiltinFn>({
#define X(name, ...) BuiltinFn{#name, detail::sigs_##name},
#include "backend/glsl/builtin_defs/fn_sigs.def"
#undef X
});

constexpr size_t builtin_fn_count   = std::size(compressed_fn_data);
constexpr size_t unrolled_sig_count = count_unrolled_sigs(compressed_fn_data);

constexpr auto unrolled_data =
    gen_unrolled_data<builtin_fn_count, unrolled_sig_count>(compressed_fn_data);

constexpr auto sorted_unrolled_fns = []() {
    auto unrolled_fns = gen_unrolled_fns(unrolled_data);

    std::sort(unrolled_fns.begin(), unrolled_fns.end(),
              [](const BuiltinFn& a, const BuiltinFn& b) { return a.name < b.name; });

    return unrolled_fns;
}();

} // namespace detail

const std::span<const BuiltinFn> builtin_fns{detail::sorted_unrolled_fns};

} // namespace stc::glsl::builtins
