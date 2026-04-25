#include "common/target_info.h"

#include <algorithm>

namespace stc {

const TargetInfo::BuiltinFunction* TargetInfo::get_builtin_fn(std::string_view fn_name) const {
    auto fn = std::lower_bound(
        functions.begin(), functions.end(), fn_name,
        [](const BuiltinFunction& fn, std::string_view name) { return fn.name < name; });

    return fn != functions.end() && fn->name == fn_name ? &(*fn) : nullptr;
}

std::optional<TargetInfo::BuiltinGlobal>
TargetInfo::get_builtin_global(std::string_view global_name) const {
    auto glob = std::lower_bound(
        globals.begin(), globals.end(), global_name,
        [](const BuiltinGlobal& glob, std::string_view name) { return glob.name < name; });

    if (glob != globals.end() && glob->name == global_name)
        return *glob;

    return std::nullopt;
}

bool TargetInfo::has_builtin_fn(std::string_view fn_name) const {
    return get_builtin_fn(fn_name) != nullptr;
}

TypeId TargetInfo::builtin_fn_ret_ty(std::string_view fn_name, const TypeList& arg_types) const {
    const auto* fn = get_builtin_fn(fn_name);

    if (fn == nullptr)
        return TypeId::null_id();

    for (const auto& overload : fn->overloads) {
        if (overload.arg_types == arg_types)
            return overload.ret_type;
    }

    return TypeId::null_id();
}

std::pair<TypeId, const TargetInfo::TypeList&>
TargetInfo::builtin_fn_ret_ty_with_impl_cast(std::string_view fn_name,
                                             const TypeList& arg_types) const {
    static TypeList empty_list{};

    const auto* fn = get_builtin_fn(fn_name);

    if (fn == nullptr)
        return {TypeId::null_id(), empty_list};

    for (const auto& overload : fn->overloads) {
        if (arg_types.size() != overload.arg_types.size())
            continue;

        bool match = true;
        for (size_t i = 0; i < arg_types.size(); i++) {
            if (!can_implicit_cast(arg_types[i], overload.arg_types[i])) {
                match = false;
                break;
            }
        }

        if (match)
            return {overload.ret_type, overload.arg_types};
    }

    return {TypeId::null_id(), empty_list};
}

bool TargetInfo::has_builtin_fn(std::string_view fn_name, const TypeList& arg_types,
                                bool allow_impl_cast) const {
    return allow_impl_cast ? !builtin_fn_ret_ty_with_impl_cast(fn_name, arg_types).first.is_null()
                           : !builtin_fn_ret_ty(fn_name, arg_types).is_null();
}

bool TargetInfo::has_builtin_global(std::string_view global_name) const {
    return get_builtin_global(global_name).has_value();
}

TypeId TargetInfo::builtin_global_ty(std::string_view global_name) const {
    auto glob = get_builtin_global(global_name);

    if (!glob.has_value())
        return TypeId::null_id();

    return glob->type;
}

} // namespace stc
