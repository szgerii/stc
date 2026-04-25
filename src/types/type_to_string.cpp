#include "types/type_to_string.h"

#include <format>

namespace {

using FPEnc = stc::types::FloatTD::Encoding;

std::string enc_to_str(FPEnc enc) {
    switch (enc) {
        case FPEnc::ieee754:
            return "IEEE-754";

        case FPEnc::bfloat16:
            return "bfloat16";

        case FPEnc::f8e4m3:
            return "F8 E4/M3";

        case FPEnc::f8e5m2:
            return "F8 E5/M2";
    }

    throw std::logic_error{"Unaccounted floating point encoding"};
}

} // namespace

namespace stc::types {

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::string TypeToStringVisitor::visit_null_id() {
    return "null type";
}

// this has to be caught early, as it needs to be checked at the TypeDescriptor-level due to
// interning
std::string TypeToStringVisitor::dispatch(const TypeDescriptor& td) {
    if (type_pool.is_any_func(&td))
        return "any fn";

    return TypeVisitor::dispatch(td);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::string TypeToStringVisitor::visit([[maybe_unused]] VoidTD void_td) {
    return "void";
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::string TypeToStringVisitor::visit([[maybe_unused]] BoolTD bool_td) {
    return "bool";
}

std::string TypeToStringVisitor::visit(IntTD int_td) {
    return std::format("{}i{}", int_td.is_signed ? "" : "u", int_td.width);
}

std::string TypeToStringVisitor::visit(FloatTD float_td) {
    return std::format("f{}{}{}", float_td.width, FloatTD::required_width(float_td.enc) ? "!" : "",
                       float_td.enc == FloatTD::Encoding::ieee754
                           ? ""
                           : std::format(" ({})", enc_to_str(float_td.enc)));
}

std::string TypeToStringVisitor::visit(VectorTD vec_td) {
    return std::format("vec{}<{}>", vec_td.component_count, dispatch(vec_td.component_type_id));
}

std::string TypeToStringVisitor::visit(MatrixTD mat_td) {
    return std::format("matrix ({}x {})", mat_td.column_count, dispatch(mat_td.column_type_id));
}

std::string TypeToStringVisitor::visit(ArrayTD arr_td) {
    return std::format("array[{}] of ({})", arr_td.length, dispatch(arr_td.element_type_id));
}

std::string TypeToStringVisitor::visit(StructTD struct_td) {
    return std::format("struct '{}'", sym_pool.get_symbol(struct_td.data->name));
}

std::string TypeToStringVisitor::visit(FunctionTD fn_td) {
    auto sym = sym_pool.get_symbol_maybe(fn_td.identifier);
    return std::format("fn '{}'", sym.value_or("null symbol id"));
}

std::string TypeToStringVisitor::visit(MethodTD method_td) {
    if (method_td.sig == nullptr)
        return "method ? -> ?";

    std::string param_list{};
    for (size_t i = 0; i < method_td.sig->param_types.size(); i++) {
        if (i > 0)
            param_list += ", ";

        param_list += dispatch(method_td.sig->param_types[i]);
    }

    return std::format("method ({}) -> {}", param_list, dispatch(method_td.sig->ret_type));
}

std::string TypeToStringVisitor::visit(BuiltinTD builtin_td) {
    return type_pool.builtin_kind_to_str(builtin_td.kind);
}

} // namespace stc::types
