#include "frontend/jl/parser.h"
#include "frontend/jl/type_conversion.h"
#include "frontend/jl/utils.h"
#include "tracy_guard.h"
#include <algorithm>
#include <bit>
#include <utility>

// struct layouts were taken from:
// https://github.com/JuliaLang/julia/blob/master/base/boot.jl

namespace {

[[maybe_unused]]
STC_FORCE_INLINE jl_sym_t* is_sym(jl_value_t* value, jl_sym_t* checked_sym) {
    if (value == nullptr || !jl_is_symbol(value))
        return nullptr;

    auto* sym = stc::jl::safe_cast<jl_sym_t>(value);
    return sym == checked_sym ? sym : nullptr;
}

STC_FORCE_INLINE jl_expr_t* to_expr_if(jl_value_t* value, jl_sym_t* head) {
    if (value == nullptr || !jl_is_expr(value))
        return nullptr;

    auto* expr = stc::jl::safe_cast<jl_expr_t>(value);
    return expr->head == head ? expr : nullptr;
}

STC_FORCE_INLINE bool is_expr(jl_value_t* value, jl_sym_t* head) {
    return to_expr_if(value, head) != nullptr;
}

stc::SymbolId get_tmp_sym(stc::SymbolPool& sym_pool) {
    static uint32_t counter = 0;

    std::string temp_name{fmt::format("stc_tmp_sym_{}", counter++)};

    return sym_pool.get_id(temp_name);
}

} // namespace

namespace stc::jl {

TypeId JLParser::resolve_type(jl_value_t* type) {
    ZoneScoped;

    if (jl_is_symbol(type)) {
        auto* tsym = reinterpret_cast<jl_sym_t*>(type);

        // clang-format off
        if (tsym == sym_cache.Bool)    return ctx.jl_Bool_t();
        if (tsym == sym_cache.Int8)    return ctx.jl_Int8_t();
        if (tsym == sym_cache.Int16)   return ctx.jl_Int16_t();
        if (tsym == sym_cache.Int32)   return ctx.jl_Int32_t();
        if (tsym == sym_cache.Int64)   return ctx.jl_Int64_t();
        if (tsym == sym_cache.Int128)  return ctx.jl_Int128_t();
        if (tsym == sym_cache.UInt8)   return ctx.jl_UInt8_t();
        if (tsym == sym_cache.UInt16)  return ctx.jl_UInt16_t();
        if (tsym == sym_cache.UInt32)  return ctx.jl_UInt32_t();
        if (tsym == sym_cache.UInt64)  return ctx.jl_UInt64_t();
        if (tsym == sym_cache.UInt128) return ctx.jl_UInt128_t();
        if (tsym == sym_cache.Float32) return ctx.jl_Float32_t();
        if (tsym == sym_cache.Float64) return ctx.jl_Float64_t();
        if (tsym == sym_cache.String)  return ctx.jl_String_t();
        if (tsym == sym_cache.Symbol)  return ctx.jl_Symbol_t();
        if (tsym == sym_cache.Nothing) return ctx.jl_Nothing_t();
        // clang-format on

        static_assert((sizeof(void*) == 4U || sizeof(void*) == 8U),
                      "unsupported environment (sizeof(void*) is not 32 or 64 bits)");

        if (tsym == sym_cache.Int && ctx.config.coerce_to_i32)
            return ctx.jl_Int32_t();

        if constexpr (sizeof(void*) == 4U) {
            if (tsym == sym_cache.Int)
                return ctx.jl_Int32_t();
            if (tsym == sym_cache.UInt)
                return ctx.jl_UInt32_t();
        } else if constexpr (sizeof(void*) == 8U) {
            if (tsym == sym_cache.Int)
                return ctx.jl_Int64_t();
            if (tsym == sym_cache.UInt)
                return ctx.jl_UInt64_t();
        }

        jl_value_t* type_val = jl_get_global(ctx.jl_env.module_cache.glm_mod, tsym);

        if (type_val == nullptr)
            type_val = jl_get_global(ctx.jl_env.module_cache.main_mod, tsym);

        if (type_val != nullptr && jl_is_datatype(type_val)) {
            TypeId parsed = parse_jl_type(safe_cast<jl_datatype_t>(type_val), ctx);

            if (!parsed.is_null())
                return parsed;
        }

        // TODO: check for struct/iface types
        fail(fmt::format("unsupported Julia type: {}", jl_symbol_name(tsym)));
        return TypeId::null_id();
    }

    if (auto* type_expr = to_expr_if(type, sym_cache.curly)) {
        size_t type_nargs = jl_expr_nargs(type_expr);

        if (type_nargs == 0) {
            internal_error("unexpected parametric type layout (zero arg)");
            return TypeId::null_id();
        }

        jl_value_t* type_base = jl_exprarg(type_expr, 0);
        if (!jl_is_symbol(type_base)) {
            internal_error("unexpected parametric type layout (first arg is not a symbol)");
            return TypeId::null_id();
        }

        jl_value_t* el_type = jl_exprarg(type_expr, 1);
        LazyInit res_el_type{[&]() -> TypeId { return resolve_type(el_type); }};

        auto* type_base_sym = safe_cast<jl_sym_t>(type_base);

        if (type_base_sym == sym_cache.Vector) {
            if (type_nargs != 2) {
                fail("invalid Vector type, it must be in the form Vector{T}");
                return TypeId::null_id();
            }

            if (res_el_type.get().is_null())
                return TypeId::null_id();

            return ctx.type_pool.any_array_td(res_el_type.get());
        }

        if (type_base_sym == sym_cache.VecTN || type_base_sym == sym_cache.VecNT) {
            std::string_view vec_base = type_base_sym == sym_cache.VecTN ? "VecTN" : "VecNT";

            auto parse_T = [&](jl_value_t* type_v) -> TypeId {
                TypeId el_ty      = resolve_type(type_v);
                const auto& el_td = ctx.type_pool.get_td(el_ty);
                if (!el_td.is_scalar()) {
                    fail(fmt::format("invalid {} component type (expected scalar type)", vec_base));
                    return TypeId::null_id();
                }

                return el_ty;
            };

            auto parse_N = [&](jl_value_t* n_v) -> uint32_t {
                if (!jl_is_int64(n_v)) {
                    fail(fmt::format("invalid {} component count type (expected Int64)", vec_base));
                    return 0;
                }

                int64_t n = jl_unbox_int64(n_v);

                if (n < 2 || n > 4) {
                    fail(fmt::format("invalid {} component count value (n not between 2 and 4)",
                                     vec_base));
                    return 0;
                }

                return static_cast<uint32_t>(n);
            };

            if (jl_expr_nargs(type_expr) != 3) {
                fail(fmt::format("invalid {} type layout (expected two args)", vec_base));
                return TypeId::null_id();
            }

            TypeId el_type_id = TypeId::null_id();
            uint32_t comp_cnt = 0;

            if (type_base_sym == sym_cache.VecTN) {
                el_type_id = parse_T(jl_exprarg(type_expr, 1));
                comp_cnt   = parse_N(jl_exprarg(type_expr, 2));
            } else {
                comp_cnt   = parse_N(jl_exprarg(type_expr, 1));
                el_type_id = parse_T(jl_exprarg(type_expr, 2));
            }

            if (el_type_id.is_null() || comp_cnt == 0) {
                assert(!_success);
                return TypeId::null_id();
            }

            return ctx.type_pool.vector_td(el_type_id, comp_cnt);
        }

        if (type_base_sym == sym_cache.Vec2T || type_base_sym == sym_cache.Vec3T ||
            type_base_sym == sym_cache.Vec4T) {

            if (res_el_type.get().is_null())
                return TypeId::null_id();

            const auto& el_td = ctx.type_pool.get_td(res_el_type.get());
            if (!el_td.is_scalar()) {
                fail("VecNT type with non-scalar T is not allowed");
                return TypeId::null_id();
            }

            uint32_t n = 2;
            if (type_base_sym == sym_cache.Vec3T)
                n = 3;
            else if (type_base_sym == sym_cache.Vec4T)
                n = 4;

            return ctx.type_pool.vector_td(res_el_type.get(), n);
        }

        fail("general parametric types are currently not supported");
        return TypeId::null_id();
    }

    internal_error("unexpected non-symbol node in type specifying position");
    return TypeId::null_id();
}

NodeId JLParser::parse(jl_value_t* node) {
    if (jl_is_linenumbernode(node)) {
        // ! line::Int
        intptr_t line = jl_unbox_long(safe_fieldref(node, 0, "line"));

        // ! file::Union{Symbol, Nothing}
        jl_value_t* file_v = safe_fieldref(node, 1, "file");

        if (jl_is_symbol(file_v)) {
            auto* file_sym = reinterpret_cast<jl_sym_t*>(file_v);

            std::string file_name = (file_sym != sym_cache.none || !fallback_file.has_value())
                                        ? jl_symbol_name(file_sym)
                                        : *fallback_file;

            std::ignore = ctx.src_info_pool.get_file(file_name);
        } else {
            std::ignore = ctx.src_info_pool.get_file(fallback_file.value_or("<unknown file>"));
        }

        cur_loc = ctx.src_info_pool.get_location(line, 1U);

        return NodeId::null_id();
    }

    // parsed symbols are treated as declaration references, with the symbol as the target
    // the actual decl it points to will be resolved by sema
    if (jl_is_symbol(node)) {
        auto* sym = reinterpret_cast<jl_sym_t*>(node);

        if (sym == sym_cache.nothing)
            return emplace_node<NothingLiteral>(cur_loc);

        return emplace_decl_ref(cur_loc, sym);
    }

    // global refs are handled similarly to symbols, but with a module name symbol included
    if (jl_is_globalref(node)) {
        // ! mod::Module
        auto* module = safe_cast<jl_module_t>(safe_fieldref(node, 0, "mod"));
        // ! name::Symbol
        auto* name   = safe_cast<jl_sym_t>(safe_fieldref(node, 1, "name"));

        SymbolId name_id = ctx.sym_pool.get_id(jl_symbol_name(name));

        NodeId glob_ref = emplace_node<GlobalRef>(cur_loc, module, name_id);

        return emplace_node<DeclRefExpr>(cur_loc, glob_ref);
    }

    // quotenode-s are inserted as raw SymbolLiteral nodes
    if (jl_is_quotenode(node)) {
        // ! value::Any
        jl_value_t* inner_v = safe_fieldref(node, 0, "value");

        if (!jl_is_symbol(inner_v))
            return fail("QuoteNode-s not wrapping Symbol-s are currently not supported");

        auto* sym = reinterpret_cast<jl_sym_t*>(inner_v);

        SymbolId sym_id = ctx.sym_pool.get_id(jl_symbol_name(sym));
        return emplace_node<SymbolLiteral>(cur_loc, sym_id);
    }

    if (jl_is_expr(node))
        return parse_expr(safe_cast<jl_expr_t>(node));

    if (jl_is_bool(node)) {
        // jl_unbox_bool returns an uint8_t
        bool value = static_cast<bool>(jl_unbox_bool(node));

        return emplace_node<BoolLiteral>(cur_loc, value);
    }

#define HANDLE_LITERAL(jl_type, value_type, node_type)                                             \
    static_assert(std::constructible_from<node_type, SrcLocationId, value_type>);                  \
    if (jl_is_##jl_type(node)) {                                                                   \
        value_type value = jl_unbox_##jl_type(node);                                               \
        return emplace_node<node_type>(cur_loc, value);                                            \
    }

    HANDLE_LITERAL(int32, int32_t, Int32Literal)
    HANDLE_LITERAL(uint8, uint8_t, UInt8Literal)
    HANDLE_LITERAL(uint16, uint16_t, UInt16Literal)
    HANDLE_LITERAL(uint32, uint32_t, UInt32Literal)
    HANDLE_LITERAL(uint64, uint64_t, UInt64Literal)

#undef HANDLE_LITERAL

    if (jl_is_int64(node)) {
        constexpr auto i32_min = static_cast<int64_t>(std::numeric_limits<int32_t>::min());
        constexpr auto i32_max = static_cast<int64_t>(std::numeric_limits<int32_t>::max());

        int64_t value = jl_unbox_int64(node);

        if (ctx.config.coerce_to_i32 && i32_min <= value && value <= i32_max)
            return emplace_node<Int32Literal>(cur_loc, static_cast<int32_t>(value));

        return emplace_node<Int64Literal>(cur_loc, value);
    }

    if (jl_typeis(node, ctx.jl_env.type_cache.uint128)) {
        const auto* data = reinterpret_cast<const uint64_t*>(node);
        uint64_t hi = 0, lo = 0;

        if constexpr (std::endian::native == std::endian::little) {
            lo = data[0];
            hi = data[1];
        } else if constexpr (std::endian::native == std::endian::big) {
            lo = data[1];
            hi = data[0];
        } else {
            return fail("using UInt128 literals is not supported on mixed-endian systems");
        }

        return emplace_node<UInt128Literal>(cur_loc, hi, lo);
    }

    if (jl_typeis(node, jl_float32_type)) {
        float value = jl_unbox_float32(node);

        return emplace_node<Float32Literal>(cur_loc, value);
    }

    if (jl_typeis(node, jl_float64_type)) {
        double value = jl_unbox_float64(node);

        if (ctx.config.coerce_to_f32)
            return emplace_node<Float32Literal>(cur_loc, static_cast<float>(value));

        return emplace_node<Float64Literal>(cur_loc, value);
    }

    if (jl_is_string(node)) {
        const char* str_data = jl_string_ptr(node);
        size_t str_len       = jl_string_len(node);

        std::string str{str_data, str_len};

        return emplace_node<StringLiteral>(cur_loc, std::move(str));
    }

    // rest of the nodes should be raw Julia objects directly injected into the AST
    // sema can decide what to do with these later
    // for known datatypes, they can be captured with their current value, or an error can be thrown

    auto* datatype     = safe_cast<jl_datatype_t>(jl_typeof(node));
    auto* type_name    = jl_symbol_name(datatype->name->name);
    SymbolId tname_sid = ctx.sym_pool.get_id(type_name);

    return emplace_node<OpaqueNode>(cur_loc, tname_sid, node);
}

// parses the code argument using Julia's Meta.parse and invokes the regular parsing pipeline on it
NodeId JLParser::parse_code(std::string_view code) {
    ZoneScoped;
    ZoneText(code.data(), code.size());

    jl_value_t* code_jl_str = nullptr;
    jl_value_t* parsed_expr = nullptr;
    JL_GC_PUSH2(&code_jl_str, &parsed_expr); // NOLINT

    const ScopeGuard jl_gc_pop_guard{[]() { JL_GC_POP(); }};

    jl_value_t* parse_fn = ctx.jl_env.module_cache.meta_mod.get_fn("parse", false);

    if (parse_fn == nullptr)
        return fail("failed to load parse function from the Meta module");

    // implemented as a simple memcpy in libjulia, avoids strlen (==> null termination agnostic)
    code_jl_str = jl_pchar_to_string(code.data(), code.size());

    parsed_expr = jl_call1(parse_fn, code_jl_str);

    if (check_exceptions())
        return fail("Julia exception while trying to parse code string using Meta.parse");

    return parse(parsed_expr);
}

std::pair<jl_value_t*, TypeId> JLParser::parse_type_annotation(jl_expr_t* annot) {
    assert(annot->head == sym_cache.dbl_col);

    if (jl_expr_nargs(annot) != 2) {
        internal_error("unexpected type annotation layout (expected two args)");
        return {nullptr, TypeId::null_id()};
    }

    jl_value_t* target_v = jl_exprarg(annot, 0);

    jl_value_t* type_v = jl_exprarg(annot, 1);
    TypeId type_id     = resolve_type(type_v);

    return {target_v, type_id};
}

jl_value_t* JLParser::unwrap_layout_qual(jl_expr_t* lq_expr, std::vector<QualKind>& quals,
                                         LQPayload& lq_payloads) {
    assert(lq_expr->head == sym_cache.macrocall);
    assert(is_sym(jl_exprarg(lq_expr, 0), sym_cache.gl_layout));

    size_t nargs = jl_expr_nargs(lq_expr);
    for (size_t i = 2; i < nargs - 1; i++) {
        jl_value_t* lq_opt_v = jl_exprarg(lq_expr, i);

        bool has_value         = false;
        int32_t value          = 0;
        jl_sym_t* opt_name_sym = nullptr;

        if (auto* lq_opt_expr = to_expr_if(lq_opt_v, sym_cache.eq)) {
            jl_value_t* opt_name_v = jl_exprarg(lq_opt_expr, 0);
            if (!jl_is_symbol(opt_name_v)) {
                fail("only symbols are allowed on the lhs of a layout option");
                return nullptr;
            }

            opt_name_sym = safe_cast<jl_sym_t>(opt_name_v);

            jl_value_t* rhs_v = jl_exprarg(lq_opt_expr, 1);
            if (jl_is_int32(rhs_v)) {
                value     = jl_unbox_int32(rhs_v);
                has_value = true;
            } else if (jl_is_int64(rhs_v)) {
                static constexpr auto i32_min =
                    static_cast<int64_t>(std::numeric_limits<int32_t>::min());
                static constexpr auto i32_max =
                    static_cast<int64_t>(std::numeric_limits<int32_t>::max());

                int64_t value_i64 = jl_unbox_int64(rhs_v);

                if (value_i64 < i32_min || value_i64 > i32_max) {
                    fail("layout option values have to be 32 bit int literals, or 64 bit int "
                         "literals convertible to 32 bits without truncation");
                    return nullptr;
                }

                value     = static_cast<int32_t>(value_i64);
                has_value = true;
            }
        } else if (jl_is_symbol(lq_opt_v)) {
            opt_name_sym = safe_cast<jl_sym_t>(lq_opt_v);
        } else {
            fail("only symbols and assignment expressions are allowed in a layout qualifier's "
                 "option");
            return nullptr;
        }

        std::string_view opt_name = jl_symbol_name(opt_name_sym);

        auto qual = try_parse_qual(opt_name);
        if (!qual.has_value() || !is_layout_qual(*qual)) {
            fail(fmt::format("invalid layout qualifier option: {}", opt_name));
            return nullptr;
        }

        if (has_value && is_valueless_layout_qual(*qual)) {
            fail(fmt::format("layout option '{}' does not expect a value", opt_name));
            return nullptr;
        }

        if (!has_value && is_value_layout_qual(*qual)) {
            fail(fmt::format("layout option '{}' expects a value", opt_name));
            return nullptr;
        }

        quals.emplace_back(*qual);

        if (has_value)
            lq_payloads.set(*qual, value);
    }

    return nargs > 0 ? jl_exprarg(lq_expr, nargs - 1) : nullptr;
}

NodeId JLParser::parse_qualified_decl(jl_value_t* qualified_expr, ParseCallback next_parser) {
    ZoneScoped;

    std::vector<QualKind> quals{};
    LQPayload lq_payloads{};

    std::optional<QualKind> iface_blk_qual{}; // in, out, uniform, buffer
    bool more_than_one_iface_qual = false;

    auto* expr_it_v = qualified_expr;
    while (auto* expr_it = to_expr_if(expr_it_v, sym_cache.macrocall)) {
        assert(expr_it->head == sym_cache.macrocall);

        size_t nargs = jl_expr_nargs(expr_it);
        if (nargs < 2)
            return internal_error("unexpected macrocall layout (expected at least two args)");

        jl_value_t* arg1_v = jl_exprarg(expr_it, 0);
        jl_value_t* arg2_v = jl_exprarg(expr_it, 1);

        if (!jl_is_linenumbernode(arg2_v))
            return internal_error(
                "unexpected macrocall layout (second arg is not a LineNumberNode)");

        // update cur_loc
        parse(arg2_v);

        if (!jl_is_symbol(arg1_v))
            return internal_error("unexpected macrocall layout (first arg is not a Symbol)");

        auto* arg1_sym = safe_cast<jl_sym_t>(arg1_v);

        // layout qualifiers are handled separately
        if (arg1_sym == sym_cache.gl_layout) {
            expr_it_v = unwrap_layout_qual(expr_it, quals, lq_payloads);
            continue;
        }

        std::string_view qual_macro_name = jl_symbol_name(arg1_sym);

        std::optional<QualKind> qual = std::nullopt;
        if (qual_macro_name.size() > 4 && qual_macro_name.starts_with("@gl_")) {
            // @gl_x -> x
            std::string_view qual_name{qual_macro_name.begin() + 4, qual_macro_name.end()};

            qual = try_parse_qual(qual_name);

            if (qual.has_value()) {
                QualKind qk = *qual;

                // no macro exists for these, simply discard
                if (is_layout_qual(qk))
                    qual = std::nullopt;

                if (qk == QualKind::tq_in || qk == QualKind::tq_out || qk == QualKind::tq_uniform ||
                    qk == QualKind::tq_buffer) {

                    if (iface_blk_qual.has_value())
                        more_than_one_iface_qual = true;

                    iface_blk_qual = qk;
                }
            }
        }

        if (!qual.has_value())
            return fail("non-qualifier macro calls are currently not supported");

        quals.emplace_back(*qual);

        if (nargs == 2)
            return fail("empty qualifier target is not allowed");

        expr_it_v = jl_exprarg(expr_it, 2);
    }

    // intercept pipeline for interface block parsing
    bool is_iface_blk_decl = iface_blk_qual && is_expr(expr_it_v, sym_cache.struct_);

    if (is_iface_blk_decl && more_than_one_iface_qual)
        return fail("cannot declare interface block with more than one storage qualifier applied");

    NodeId decl_id = is_iface_blk_decl
                         ? parse_interface_block(safe_cast<jl_expr_t>(expr_it_v), *iface_blk_qual)
                         : (this->*next_parser)(expr_it_v);

    if (!_success)
        return NodeId::null_id();

    Decl* decl = ctx.get_and_dyn_cast<Decl>(decl_id);
    if (decl == nullptr)
        return fail("qualifiers can only be applied to declarations");

    assert(decl->qualifiers.is_null());
    QualId qual_id   = !quals.empty() ? ctx.qual_pool.emplace(std::move(quals), lq_payloads).first
                                      : QualId::null_id();
    decl->qualifiers = qual_id;

    return decl_id;
}

NodeId JLParser::parse_expr(jl_expr_t* expr) {
    ZoneScoped;

    jl_sym_t* head = expr->head;
    size_t nargs   = jl_expr_nargs(expr);

#ifdef STC_PROFILING
    const char* head_str_prof = jl_symbol_name(head);
    ZoneText(head_str_prof, strlen(head_str_prof));
#endif

    if (head == sym_cache.block)
        return parse_block(expr, nargs);

    if (head == sym_cache.call)
        return parse_call(expr, nargs);

    if (head == sym_cache.if_)
        return parse_if(expr, nargs);

    if (head == sym_cache.while_)
        return parse_while(expr, nargs);

    if (head == sym_cache.return_)
        return parse_return(expr, nargs);

    if (head == sym_cache.eq)
        return parse_assignment(expr, nargs);

    if (head == sym_cache.global || head == sym_cache.local)
        return parse_var_decl(expr, nargs);

    if (head == sym_cache.function)
        return parse_method_decl(expr, nargs);

    if (head == sym_cache.dot)
        return parse_dot_chain(expr, nargs);

    if (head == sym_cache.vect)
        return parse_vect(expr, nargs);

    if (head == sym_cache.ref)
        return parse_ref(expr, nargs);

    if (head == sym_cache.dbl_amper || head == sym_cache.dbl_pipe)
        return parse_log_op(expr, nargs);

    if (head == sym_cache.struct_)
        return parse_struct(expr, nargs);

    if (head == sym_cache.macrocall)
        return parse_qualified_decl(reinterpret_cast<jl_value_t*>(expr));

    if (head == sym_cache.break_)
        return emplace_node<BreakStmt>(cur_loc);

    if (head == sym_cache.continue_)
        return emplace_node<ContinueStmt>(cur_loc);

    // leave at the end so that every other case is caught earlier naturally
    // e.g. <=, >=, ==
    std::string_view head_sv{jl_symbol_name(head)};
    if (head_sv.ends_with('='))
        return parse_update_assignment(expr, nargs);

    if (head == sym_cache.arrow)
        return fail("arrow functions are not supported currently");

    fail("unsupported Expr node in Julia source code:");

    jl_value_t* dump_fn = ctx.jl_env.module_cache.base_mod.get_fn("dump");
    jl_call1(dump_fn, reinterpret_cast<jl_value_t*>(expr));
    std::cerr << '\n';

    return NodeId::null_id();
}

NodeId JLParser::parse_var_decl(jl_expr_t* expr, size_t nargs) {
    ZoneScoped;
    assert(expr->head == sym_cache.global || expr->head == sym_cache.local ||
           expr->head == sym_cache.eq || expr->head == sym_cache.dbl_col);

    SrcLocationId decl_loc = cur_loc;

    auto* inner          = reinterpret_cast<jl_value_t*>(expr);
    jl_value_t* id       = nullptr;
    jl_value_t* type     = nullptr;
    jl_value_t* init     = nullptr;
    MaybeScopeType scope = MaybeScopeType::Unspec;

    if (nargs == 0 || nargs > 2) {
        return internal_error(
            "unexpected number of args in variable declaration (expected 1 or 2)");
    }

    if (is_expr(inner, sym_cache.global) || is_expr(inner, sym_cache.local)) {
        scope = is_expr(inner, sym_cache.global) ? MaybeScopeType::Global : MaybeScopeType::Local;
        inner = jl_exprarg(expr, 0);
    }

    if (auto* assignment_expr = to_expr_if(inner, sym_cache.eq)) {
        if (jl_expr_nargs(assignment_expr) != 2)
            return internal_error("unexpected assignment layout (expected two args)");

        inner = jl_exprarg(assignment_expr, 0);
        init  = jl_exprarg(assignment_expr, 1);
    }

    if (auto* typed_expr = to_expr_if(inner, sym_cache.dbl_col)) {
        if (jl_expr_nargs(typed_expr) != 2)
            return internal_error("unexpected type annotation layout (expected two args)");

        id   = jl_exprarg(typed_expr, 0);
        type = jl_exprarg(typed_expr, 1);
    }

    if (jl_is_symbol(inner)) {
        id = inner;
    }

    if (id == nullptr || !jl_is_symbol(id))
        return internal_error(
            "invalid variable declaration expression, couldn't unwrap identifier symbol");

    SymbolId id_sym = ctx.sym_pool.get_id(jl_symbol_name(safe_cast<jl_sym_t>(id)));

    return emplace_node<VarDecl>(decl_loc, id_sym,
                                 type != nullptr ? resolve_type(type) : TypeId::null_id(), scope,
                                 init != nullptr ? parse(init) : NodeId::null_id());
}

NodeId JLParser::parse_method_decl(jl_expr_t* expr, size_t nargs) {
    ZoneScoped;
    assert(expr->head == sym_cache.function || expr->head == sym_cache.eq);

    if (nargs != 2)
        return internal_error("unexpected function definition layout, expected two argument for "
                              "both longdef and shortdef formats");

    SrcLocationId method_loc = cur_loc;

    // assignment lhs in shortdef, or header part of fn def
    jl_value_t* header_v = jl_exprarg(expr, 0);
    if (!jl_is_expr(header_v))
        return internal_error("unexpected function definition layout, couldn't unwrap header");
    auto* header = reinterpret_cast<jl_expr_t*>(header_v);

    // function(x::T) where {T <: Int} ... end
    if (header->head == sym_cache.where)
        return fail("type parameters in method definitions are not supported currently (i.e. "
                    "using 'where' in the header)");

    // function() ... end
    if (header->head == sym_cache.tuple)
        return fail("anonymous functions are not currently supported");

    TypeId expl_ret_type = TypeId::null_id();
    // function f()::Int ... end
    if (header->head == sym_cache.dbl_col) {
        auto [node, type] = parse_type_annotation(header);

        // assumes error has already been reported, only propagates failure
        if (node == nullptr || type.is_null())
            return NodeId::null_id();

        expl_ret_type = type;

        if (!jl_is_expr(node))
            return internal_error("unexpected non-expr node in function header");

        header = reinterpret_cast<jl_expr_t*>(node);
    }

    if (header->head != sym_cache.call)
        return internal_error(
            fmt::format("unexpected function header Expr kind: {}", jl_symbol_name(header->head)));

    size_t header_nargs = jl_expr_nargs(header);

    if (header_nargs == 0)
        return internal_error("unexpected empty function header layout");

    jl_value_t* name_v = jl_exprarg(header, 0);
    if (!jl_is_symbol(name_v))
        return internal_error("unexpected non-symbol name in function header");
    auto* name = reinterpret_cast<jl_sym_t*>(name_v);

    SymbolId fn_name = ctx.sym_pool.get_id(jl_symbol_name(name));

    std::vector<NodeId> param_decls{};
    param_decls.reserve(header_nargs - 1);
    for (size_t i = 1; i < header_nargs; i++) {
        jl_value_t* arg_v = jl_exprarg(header, i);

        // manually intercept empty kwargs collection and skip it
        if (is_expr(arg_v, sym_cache.parameters) && jl_expr_nargs(arg_v) == 0)
            continue;

        NodeId arg_id = parse_qualified_decl(arg_v, &JLParser::parse_param_decl);
        if (arg_id.is_null())
            continue;

        assert(ctx.isa<ParamDecl>(arg_id));
        param_decls.emplace_back(arg_id);
    }

    // assignment rhs in shortdef, or body part of fn def
    jl_value_t* body_v = jl_exprarg(expr, 1);
    if (!jl_is_expr(body_v))
        return internal_error("unexpected function definition layout, couldn't unwrap body");

    NodeId body_node = parse(body_v);

    return emplace_node<MethodDecl>(method_loc, fn_name, expl_ret_type, std::move(param_decls),
                                    body_node);
}

NodeId JLParser::parse_param_decl(jl_value_t* param) {
    ZoneScoped;
    if (param == nullptr)
        return internal_error("null pointer in Julia AST");

    SrcLocationId param_loc = cur_loc;

    auto create_param = [this, param_loc](jl_sym_t* sym, TypeId type = TypeId::null_id(),
                                          NodeId init = NodeId::null_id()) -> NodeId {
        return this->emplace_node<ParamDecl>(param_loc, ctx.sym_pool.get_id(jl_symbol_name(sym)),
                                             type, false, init);
    };

    if (jl_is_symbol(param))
        return create_param(safe_cast<jl_sym_t>(param));

    if (jl_is_expr(param)) {
        auto* param_expr = safe_cast<jl_expr_t>(param);

        TypeId type = TypeId::null_id();
        NodeId init = NodeId::null_id();

        // TODO: kwargs
        if (param_expr->head == sym_cache.parameters)
            return fail("kwargs are currently not supported");

        if (param_expr->head == sym_cache.kw) {
            if (jl_expr_nargs(param_expr) != 2)
                return internal_error("unexpected layout in default initialized parameter node");

            init  = parse(jl_exprarg(param_expr, 1));
            param = jl_exprarg(param_expr, 0);
        }

        if (jl_is_symbol(param))
            return create_param(safe_cast<jl_sym_t>(param), type, init);

        param_expr = safe_cast<jl_expr_t>(param);

        if (param_expr->head == sym_cache.dots)
            return fail("variadic arguments are currently not supported");

        if (param_expr->head == sym_cache.dbl_col) {
            auto [inner, annot_type] = parse_type_annotation(param_expr);

            type  = annot_type;
            param = inner;
        }

        if (jl_is_symbol(param))
            return create_param(safe_cast<jl_sym_t>(param), type, init);

        return internal_error("unexpected parameter layout");
    }

    return internal_error("unexpected node kind for a parameter");
}

NodeId JLParser::parse_assignment(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.eq);

    if (nargs != 2)
        return internal_error("unexpected assignment layut (expected two args)");

    jl_value_t* lhs = jl_exprarg(expr, 0);

    // f(x, y) = x + y
    if (is_expr(lhs, sym_cache.call))
        return parse_qualified_decl(reinterpret_cast<jl_value_t*>(expr),
                                    &JLParser::parse_method_decl);

    // x::Int = 0
    if (is_expr(lhs, sym_cache.dbl_col))
        return parse_qualified_decl(reinterpret_cast<jl_value_t*>(expr), &JLParser::parse_var_decl);

    jl_value_t* rhs = jl_exprarg(expr, 1);

    if (is_expr(lhs, sym_cache.tuple) && is_expr(rhs, sym_cache.tuple))
        return parse_tuple_assignment(expr);

    SrcLocationId outer_loc = cur_loc;

    NodeId parsed_lhs = parse(lhs);
    NodeId parsed_rhs = parse(rhs);

    if (parsed_lhs.is_null() || parsed_rhs.is_null())
        return NodeId::null_id();

    return emplace_node<Assignment>(outer_loc, parsed_lhs, parsed_rhs);
}

NodeId JLParser::parse_tuple_assignment(jl_expr_t* expr) {
    ZoneScoped;
    // these should all be prevalidated by the caller
    assert(expr->head == sym_cache.eq);
    assert(jl_expr_nargs(expr) == 2);
    assert(is_expr(jl_exprarg(expr, 0), sym_cache.tuple));
    assert(is_expr(jl_exprarg(expr, 1), sym_cache.tuple));

    SrcLocationId assign_loc = cur_loc;

    auto* lhs_tuple = safe_cast<jl_expr_t>(jl_exprarg(expr, 0));
    auto* rhs_tuple = safe_cast<jl_expr_t>(jl_exprarg(expr, 1));

    size_t lhs_n = jl_expr_nargs(lhs_tuple), rhs_n = jl_expr_nargs(rhs_tuple);
    if (lhs_n != rhs_n)
        return fail("non-element-wise tuple assignment is not supported (lhs tuple length has to "
                    "match rhs)");

    std::vector<NodeId> assignments_block{};
    assignments_block.resize(lhs_n * 2); // rhs -> tmp and tmp -> lhs assignments

    for (size_t i = 0; i < lhs_n; i++) {
        NodeId lhs = parse(jl_exprarg(lhs_tuple, i));
        NodeId rhs = parse(jl_exprarg(rhs_tuple, i));

        if (lhs.is_null() || rhs.is_null()) {
            if (_success)
                fail("couldn't parse member in tuple assignment");

            // their place remains set to the default initialized null id
            continue;
        }

        SymbolId tmp_sym = get_tmp_sym(ctx.sym_pool);

        NodeId tmp_dre_rhs = emplace_decl_ref(assign_loc, tmp_sym);
        NodeId rhs_to_tmp  = emplace_node<Assignment>(assign_loc, tmp_dre_rhs, rhs);

        NodeId tmp_dre_lhs = emplace_decl_ref(assign_loc, tmp_sym);
        NodeId tmp_to_lhs  = emplace_node<Assignment>(assign_loc, lhs, tmp_dre_lhs);

        assignments_block[i]         = rhs_to_tmp;
        assignments_block[lhs_n + i] = tmp_to_lhs;
    }

    return emplace_node<CompoundExpr>(assign_loc, std::move(assignments_block));
}

NodeId JLParser::parse_update_assignment(jl_expr_t* expr, size_t nargs) {
    std::string_view head_sv{jl_symbol_name(expr->head)};
    assert(head_sv.ends_with('=') && head_sv != "=" && head_sv != "!=" && head_sv != ">=" &&
           head_sv != "<=");

    if (nargs != 2)
        return internal_error("unexpected update assignment layout (expected two args)");

    SrcLocationId assign_loc = cur_loc;

    bool is_broadcast = head_sv.starts_with('.');
    std::string_view fn_sym =
        head_sv.substr(is_broadcast ? 1 : 0, head_sv.size() - (is_broadcast ? 2 : 1));

    if (fn_sym.empty())
        return fail("broadcasting on regular assignment is currently not supported");

    NodeId fn_dre        = emplace_decl_ref(assign_loc, fn_sym);
    NodeId parsed_target = parse(jl_exprarg(expr, 0));
    NodeId parsed_value  = parse(jl_exprarg(expr, 1));

    if (fn_dre.is_null() || parsed_target.is_null() || parsed_value.is_null())
        return NodeId::null_id();

    return emplace_node<UpdateAssignment>(assign_loc, fn_dre, parsed_target, parsed_value,
                                          is_broadcast);
}

NodeId JLParser::parse_block(jl_expr_t* expr, size_t nargs) {
    ZoneScoped;
    assert(expr->head == sym_cache.block);

    SrcLocationId cmpd_loc = cur_loc;
    std::vector<NodeId> inner_exprs{};
    inner_exprs.reserve(nargs);

    for (size_t i = 0; i < nargs; i++) {
        NodeId parsed_arg = parse(jl_exprarg(expr, i));

        if (parsed_arg.is_null())
            continue;

        inner_exprs.push_back(parsed_arg);

        // if block is not empty, make its location point to the first inner line, rather than the
        // last outer one
        if (inner_exprs.size() == 1) {
            cmpd_loc = cur_loc;
        }
    }

    return emplace_node<CompoundExpr>(cmpd_loc, std::move(inner_exprs));
}

NodeId JLParser::parse_call(jl_expr_t* expr, size_t nargs) {
    ZoneScoped;
    assert(expr->head == sym_cache.call || expr->head == sym_cache.dot);

    SrcLocationId call_loc = cur_loc;

    if (nargs == 0)
        return internal_error("function call expression with zero arguments");

    // regular/infix broadcast calls need to be dissected differently
    // but their processing logic is identical otherwise
    // f.(x, y, z)  ->  (.    f  tuple (x y z))
    // f(x, y, z)   ->  (call f  x y z)
    // x .+ y       ->  (call .+ x y)
    // x + y        ->  (call +  x y)

    // catches regular broadcast calls immediately
    bool is_broadcast       = expr->head == sym_cache.dot;
    NodeId parsed_target_fn = NodeId::null_id();
    bool unfold_binop       = false;

    jl_value_t* target_fn_v = jl_exprarg(expr, 0);
    if (!is_broadcast && jl_is_symbol(target_fn_v)) {
        assert(expr->head == sym_cache.call);
        jl_sym_t* fn_sym = safe_cast<jl_sym_t>(target_fn_v);
        std::string_view fn_name{jl_symbol_name(fn_sym)};

        // prefix broadcast calls, strip first symbol
        if (fn_name.starts_with('.')) {
            std::string_view target_fn{fn_name.substr(1)};

            is_broadcast     = true;
            parsed_target_fn = emplace_decl_ref(cur_loc, target_fn);
            unfold_binop     = fn_sym == sym_cache.dot_plus || fn_sym == sym_cache.dot_asterisk;
        } else {
            parsed_target_fn = parse(target_fn_v);
            unfold_binop     = fn_sym == sym_cache.plus || fn_sym == sym_cache.asterisk;
        }
    } else {
        parsed_target_fn = parse(target_fn_v);
    }

    jl_expr_t* params_expr = expr;
    size_t first_param_idx = 1;

    if (expr->head == sym_cache.dot) {
        jl_value_t* params_tuple_v = jl_exprarg(expr, 1);

        if (!is_expr(params_tuple_v, sym_cache.tuple))
            return fail("unexpected broadcast call layout (second arg is not a :tuple Expr)");

        params_expr     = safe_cast<jl_expr_t>(params_tuple_v);
        first_param_idx = 0;
    }

    size_t params_n = jl_expr_nargs(params_expr);

    std::vector<NodeId> params;
    for (size_t i = first_param_idx; i < params_n; i++) {
        jl_value_t* arg = jl_exprarg(params_expr, i);

        // FEATURE: handle kwargs
        if (is_expr(arg, sym_cache.parameters) || is_expr(arg, sym_cache.kw))
            return fail("Keyword arguments are currently not supported");

        NodeId parsed_arg = parse(arg);
        params.push_back(parsed_arg);
    }

    // flatten "n-ary parsed" binary operators
    // +(1, 2, 3, 4) -> +(+(+(1, 2), 3), 4)
    // NOTE:
    // this currently only affects + and *, so left-associativity is assumed here.
    // if this is extended in a later Julia version, this will need to be updated as well
    if (unfold_binop && params.size() > 2) {
        NodeId current_lhs = emplace_node<FunctionCall>(
            call_loc, parsed_target_fn, std::vector{params[0], params[1]}, is_broadcast);

        for (size_t rhs_idx = 2; rhs_idx < params.size(); rhs_idx++) {
            current_lhs =
                emplace_node<FunctionCall>(call_loc, parsed_target_fn,
                                           std::vector{current_lhs, params[rhs_idx]}, is_broadcast);
        }

        return current_lhs;
    }

    return emplace_node<FunctionCall>(call_loc, parsed_target_fn, std::move(params), is_broadcast);
}

NodeId JLParser::parse_if(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.if_ || expr->head == sym_cache.elseif);

    SrcLocationId if_loc = cur_loc;

    if (nargs < 2)
        return internal_error("if expr without at least two args");

    NodeId parsed_cond  = parse(jl_exprarg(expr, 0));
    NodeId parsed_true  = parse(jl_exprarg(expr, 1));
    NodeId parsed_false = NodeId::null_id();

    if (nargs == 3) {
        jl_value_t* false_branch = jl_exprarg(expr, 2);
        auto* false_branch_expr  = try_cast<jl_expr_t>(false_branch);

        if (false_branch_expr != nullptr && false_branch_expr->head == sym_cache.elseif) {
            parsed_false = parse_if(false_branch_expr, jl_expr_nargs(false_branch_expr));
        } else {
            parsed_false = parse(false_branch);
        }
    }

    return emplace_node<IfExpr>(if_loc, parsed_cond, parsed_true, parsed_false);
}

NodeId JLParser::parse_while(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.while_);

    if (nargs != 2)
        return internal_error("unexpected while expr arg count");

    SrcLocationId while_loc = cur_loc;

    NodeId parsed_cond = parse(jl_exprarg(expr, 0));
    NodeId parsed_body = parse(jl_exprarg(expr, 1));

    return emplace_node<WhileExpr>(while_loc, parsed_cond, parsed_body);
}

NodeId JLParser::parse_return(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.return_);

    if (nargs != 1)
        return internal_error("unexpected return expr layout (expected one arg)");

    SrcLocationId ret_loc = cur_loc;
    jl_value_t* inner     = jl_exprarg(expr, 0);
    NodeId parsed_inner   = NodeId::null_id();

    bool is_nothing_literal =
        jl_is_symbol(inner) && safe_cast<jl_sym_t>(inner) == sym_cache.nothing;
    bool is_implicit_nothing = jl_is_nothing(inner);

    if (!is_nothing_literal && !is_implicit_nothing)
        parsed_inner = parse(inner);

    return emplace_node<ReturnStmt>(ret_loc, parsed_inner);
}

NodeId JLParser::parse_dot_chain(jl_expr_t* expr, size_t nargs) {
    ZoneScoped;
    assert(expr->head == sym_cache.dot);

    if (nargs != 2)
        return internal_error("unexpected dot expr layout (expected two args)");

    // check if expr is a broadcast call first (and redirect if necessary)
    if (is_expr(jl_exprarg(expr, 1), sym_cache.tuple))
        return parse_call(expr, nargs);

    jl_value_t* current_lhs = jl_exprarg(expr, 0);
    jl_value_t* current_rhs = jl_exprarg(expr, 1);

    std::vector<NodeId> lookup_chain{};
    bool is_done = false;
    while (!is_done) {
        NodeId parsed_rhs = parse(current_rhs);
        lookup_chain.emplace_back(parsed_rhs);

        if (!is_expr(current_lhs, sym_cache.dot)) {
            NodeId parsed_lhs = parse(current_lhs);
            lookup_chain.emplace_back(parsed_lhs);

            is_done = true;
            continue;
        }

        auto* lhs_expr = safe_cast<jl_expr_t>(current_lhs);
        assert(lhs_expr->head == sym_cache.dot);

        if (jl_expr_nargs(lhs_expr) != 2)
            return internal_error("unexpected dot expr layout (expected two args)");

        current_lhs = jl_exprarg(lhs_expr, 0);
        current_rhs = jl_exprarg(lhs_expr, 1);
    }

    // chains are a recursive structure, so the above flattening creates the reversed version of the
    // resulting chain
    std::reverse(lookup_chain.begin(), lookup_chain.end());

    return emplace_node<DotChain>(cur_loc, std::move(lookup_chain));
}

NodeId JLParser::parse_vect(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.vect);

    SrcLocationId base_loc = cur_loc;

    std::vector<NodeId> members{};
    members.reserve(nargs);

    for (size_t i = 0; i < nargs; i++)
        members.emplace_back(parse(jl_exprarg(expr, i)));

    return emplace_node<ArrayLiteral>(base_loc, std::move(members));
}

NodeId JLParser::parse_ref(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.ref);

    SrcLocationId base_loc = cur_loc;

    if (nargs < 2)
        return internal_error("unexpected ref expression layout (expected at least two args)");

    NodeId target = parse(jl_exprarg(expr, 0));
    if (target.is_null())
        return NodeId::null_id();

    std::vector<NodeId> indexers{};
    indexers.reserve(nargs - 1);

    for (size_t i = 1; i < nargs; i++) {
        NodeId parsed_indexer = parse(jl_exprarg(expr, i));

        if (parsed_indexer.is_null())
            return NodeId::null_id();

        indexers.emplace_back(parsed_indexer);
    }

    return emplace_node<IndexerExpr>(base_loc, target, std::move(indexers));
}

NodeId JLParser::parse_field_decl(jl_value_t* field_decl_v) {
    if (jl_is_symbol(field_decl_v))
        return fail("field declaration without explicit type annotation is not allowed");

    jl_expr_t* field_decl_expr =
        jl_is_expr(field_decl_v) ? safe_cast<jl_expr_t>(field_decl_v) : nullptr;

    if (field_decl_expr == nullptr || field_decl_expr->head != sym_cache.dbl_col)
        return fail("only typed field declarations are supported inside struct definitions, "
                    "inner constructors are not allowed");

    auto [field_id_v, field_type] = parse_type_annotation(field_decl_expr);

    if (field_id_v == nullptr || field_type.is_null())
        return NodeId::null_id();

    if (!jl_is_symbol(field_id_v))
        return internal_error("unexpected field declaration layout (lhs is not a symbol)");

    std::string_view field_id{jl_symbol_name(safe_cast<jl_sym_t>(field_id_v))};
    SymbolId field_id_sym = ctx.sym_pool.get_id(field_id);

    NodeId parsed_decl = emplace_node<FieldDecl>(cur_loc, field_id_sym, field_type);
    assert(!parsed_decl.is_null());

    return parsed_decl;
}

NodeId JLParser::parse_struct(jl_expr_t* expr, size_t nargs, bool register_td) {
    ZoneScoped;
    assert(expr->head == sym_cache.struct_);

    if (nargs != 3)
        return internal_error("unexpected struct definition layout (expected three args)");

    SrcLocationId struct_loc = cur_loc;

    jl_value_t* is_mutable_arg  = jl_exprarg(expr, 0);
    jl_value_t* struct_id_arg   = jl_exprarg(expr, 1);
    jl_value_t* field_decls_arg = jl_exprarg(expr, 2);

    if (!jl_is_bool(is_mutable_arg))
        return internal_error("unexpected struct definition layout (first arg is non-bool)");

    bool is_mutable = static_cast<bool>(jl_unbox_bool(is_mutable_arg));

    if (!jl_is_symbol(struct_id_arg)) {
        if (!jl_is_expr(struct_id_arg))
            return internal_error(
                "unexpected struct definition layout (second arg is non-symbol, non-expr)");

        auto* struct_id_expr = safe_cast<jl_expr_t>(struct_id_arg);
        assert(struct_id_expr != nullptr);

        if (struct_id_expr->head == sym_cache.curly)
            return fail("parametric composite types are not supported");

        if (struct_id_expr->head == sym_cache.subtype_op)
            return fail("composite subtypes are not supported");

        return internal_error(
            "unexpected struct definition layout (second arg is an expr, but not {T} or <:)");
    }

    std::string_view struct_id{jl_symbol_name(safe_cast<jl_sym_t>(struct_id_arg))};
    SymbolId struct_id_sym = ctx.sym_pool.get_id(struct_id);

    if (!ctx.type_pool.get_struct_td(struct_id_sym).is_null())
        return fail(fmt::format("multiple definitions found for type '{}'", struct_id));

    if (!jl_is_expr(field_decls_arg))
        return internal_error("unexpected struct definition layout (third arg is non-expr)");

    auto* field_decls_block = safe_cast<jl_expr_t>(field_decls_arg);
    if (field_decls_block == nullptr || field_decls_block->head != sym_cache.block)
        return internal_error(
            "unexpected struct definition layout (third arg is a non-block expr)");

    size_t fields_block_nargs = jl_expr_nargs(field_decls_block);

    std::vector<StructData::FieldInfo> field_infos{};
    if (register_td)
        field_infos.reserve(fields_block_nargs);

    std::unordered_set<SymbolId> field_names{};
    field_names.reserve(fields_block_nargs);

    auto [sdecl_id, sdecl] =
        ctx.emplace_node<StructDecl>(struct_loc, struct_id_sym, std::vector<NodeId>{}, is_mutable);
    sdecl->field_decls.reserve(fields_block_nargs);

    for (size_t i = 0; i < fields_block_nargs; i++) {
        jl_value_t* field_decl_v = jl_exprarg(field_decls_block, i);

        if (jl_is_linenumbernode(field_decl_v)) {
            // make sure cur_loc is updated
            [[maybe_unused]] NodeId ln_node = parse(field_decl_v);
            assert(ln_node.is_null());
            continue;
        }

        NodeId parsed_decl = parse_qualified_decl(field_decl_v, &JLParser::parse_field_decl);

        auto* fdecl = ctx.get_and_dyn_cast<FieldDecl>(parsed_decl);
        if (parsed_decl.is_null())
            continue;

        bool inserted = field_names.emplace(fdecl->identifier).second;
        if (!inserted)
            return fail("struct with repeated field names is not allowed");

        sdecl->field_decls.emplace_back(parsed_decl);

        if (register_td)
            field_infos.emplace_back(fdecl->identifier, fdecl->type);
    }

    sdecl->field_decls.shrink_to_fit();

    if (register_td) {
        field_infos.shrink_to_fit();

        ctx.type_pool.make_struct_td(struct_id_sym, std::move(field_infos), ctx);
    }

    return sdecl_id;
}

NodeId JLParser::parse_interface_block(jl_expr_t* expr, QualKind storage) {
    ZoneScoped;
    assert(expr->head == sym_cache.struct_);

    InterfaceStorage st{};
    if (storage == QualKind::tq_in)
        st = InterfaceStorage::In;
    else if (storage == QualKind::tq_out)
        st = InterfaceStorage::Out;
    else if (storage == QualKind::tq_uniform)
        st = InterfaceStorage::Uniform;
    else if (storage == QualKind::tq_buffer)
        st = InterfaceStorage::Buffer;
    else
        return internal_error("invalid QualKind passed to parse_interface_block for storage");

    // CLEANUP: this could be done less wastefully
    NodeId parsed_struct = parse_struct(expr, jl_expr_nargs(expr), false);

    if (parsed_struct.is_null())
        return NodeId::null_id();

    auto* sdecl = ctx.get_and_dyn_cast<StructDecl>(parsed_struct);
    if (sdecl == nullptr)
        return internal_error("parse_struct returned non-struct-declaration node");

    return emplace_node<InterfaceBlockDecl>(sdecl->location, st, sdecl->identifier,
                                            std::move(sdecl->field_decls));
}

NodeId JLParser::parse_log_op(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.dbl_amper || expr->head == sym_cache.dbl_pipe);

    if (nargs != 2)
        return internal_error("unexpected logical operator layout (expected two args)");

    SrcLocationId base_loc = cur_loc;

    NodeId lhs = parse(jl_exprarg(expr, 0));
    NodeId rhs = parse(jl_exprarg(expr, 1));

    return emplace_node<LogicalBinOp>(base_loc, lhs, rhs, expr->head == sym_cache.dbl_amper);
}

NodeId JLParser::fail(std::string_view msg, SrcLocationId loc_id) {
    if (loc_id.is_null())
        loc_id = cur_loc;

    _success = false;
    stc::error(ctx.src_info_pool, loc_id, msg);

    return NodeId::null_id();
}

NodeId JLParser::internal_error(std::string_view msg, SrcLocationId loc_id) {
    if (loc_id.is_null())
        loc_id = cur_loc;

    _success = false;
    stc::internal_error(ctx.src_info_pool, loc_id, msg);

    return NodeId::null_id();
}

} // namespace stc::jl
