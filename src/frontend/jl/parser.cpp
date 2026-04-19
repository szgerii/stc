#include "frontend/jl/parser.h"
#include "frontend/jl/utils.h"
#include <algorithm>
#include <bit>
#include <utility>

// struct layouts were taken from:
// https://github.com/JuliaLang/julia/blob/master/base/boot.jl

namespace {

STC_FORCE_INLINE jl_expr_t* is_expr(jl_value_t* value, jl_sym_t* head) {
    if (value == nullptr || !jl_is_expr(value))
        return nullptr;

    auto* expr = reinterpret_cast<jl_expr_t*>(value);
    return expr->head == head ? expr : nullptr;
}

} // namespace

namespace stc::jl {

TypeId JLParser::resolve_type(jl_value_t* type) {
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

        static_assert((sizeof(void*) == 4U || sizeof(void*) == 8U) &&
                      "unsupported environment (sizeof(void*) is not 32 or 64 bits)");

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

        error(std::format("unsupported Julia type: {}", jl_symbol_name(tsym)));
        return TypeId::null_id();
    }

    if (is_expr(type, sym_cache.curly)) {
        jl_expr_t* type_expr = safe_cast<jl_expr_t>(type);
        size_t type_nargs    = jl_expr_nargs(type_expr);

        if (type_nargs == 0) {
            internal_error("unexpected parametric type layout (zero arg)");
            return TypeId::null_id();
        }

        jl_value_t* type_base = jl_exprarg(type_expr, 0);
        if (!jl_is_symbol(type_base)) {
            internal_error("unexpected parametric type layout (first arg is not a symbol)");
            return TypeId::null_id();
        }

        jl_sym_t* type_base_sym = safe_cast<jl_sym_t>(type_base);

        if (type_base_sym == sym_cache.Vector) {
            if (type_nargs != 2) {
                error("invalid Vector type, it must be in the form Vector{T}");
                return TypeId::null_id();
            }

            jl_value_t* el_type = jl_exprarg(type_expr, 1);
            TypeId res_el_type  = resolve_type(el_type);
            if (res_el_type.is_null())
                return TypeId::null_id();

            return ctx.type_pool.any_array_td(res_el_type);
        }

        error("general parametric types are currently not supported");
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

        SymbolId sym_id = ctx.sym_pool.get_id(jl_symbol_name(sym));
        NodeId sym_lit  = emplace_node<SymbolLiteral>(cur_loc, sym_id);

        return emplace_node<DeclRefExpr>(cur_loc, sym_lit);
    }

    // global refs are handled similarly to symbols, but with a module name symbol included
    if (jl_is_globalref(node)) {
        // ! mod::Module
        auto* module = safe_cast<jl_module_t>(safe_fieldref(node, 0, "mod"));
        // ! name::Symbol
        auto* name   = safe_cast<jl_sym_t>(safe_fieldref(node, 1, "name"));

        ModuleId mod_id  = ctx.module_pool.get_id(module);
        SymbolId name_id = ctx.sym_pool.get_id(jl_symbol_name(name));

        NodeId glob_ref = emplace_node<GlobalRef>(cur_loc, mod_id, name_id);

        return emplace_node<DeclRefExpr>(cur_loc, glob_ref);
    }

    // quotenode-s are inserted as raw SymbolLiteral nodes
    if (jl_is_quotenode(node)) {
        // ! value::Any
        jl_value_t* inner_v = safe_fieldref(node, 0, "value");

        if (!jl_is_symbol(inner_v))
            return error("QuoteNode-s not wrapping Symbol-s are currently not supported");

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
    HANDLE_LITERAL(int64, int64_t, Int64Literal)
    HANDLE_LITERAL(uint8, uint8_t, UInt8Literal)
    HANDLE_LITERAL(uint16, uint16_t, UInt16Literal)
    HANDLE_LITERAL(uint32, uint32_t, UInt32Literal)
    HANDLE_LITERAL(uint64, uint64_t, UInt64Literal)

#undef HANDLE_LITERAL

    // TODO: TEST ON SOME BE VM!!
    if (jl_typeis(node, ctx.jl_env.type_cache.uint128)) {
        const auto* data = reinterpret_cast<const uint64_t*>(node);
        uint64_t hi, lo; // NOLINT(cppcoreguidelines-init-variables)

        if constexpr (std::endian::native == std::endian::little) {
            lo = data[0];
            hi = data[1];
        } else if constexpr (std::endian::native == std::endian::big) {
            lo = data[1];
            hi = data[0];
        } else {
            throw std::runtime_error{
                "Using UInt128 literals is not supported on mixed-endian systems"};
        }

        return emplace_node<UInt128Literal>(cur_loc, hi, lo);
    }

    if (jl_typeis(node, jl_float32_type)) {
        float value = jl_unbox_float32(node);

        return emplace_node<Float32Literal>(cur_loc, value);
    }

    if (jl_typeis(node, jl_float64_type)) {
        double value = jl_unbox_float64(node);

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
    jl_value_t* code_jl_str = nullptr;
    jl_value_t* parsed_expr = nullptr;
    JL_GC_PUSH2(&code_jl_str, &parsed_expr);

    const ScopeGuard jl_gc_pop_guard{[]() { JL_GC_POP(); }};

    jl_value_t* meta_mod_v = jl_get_global(jl_base_module, jl_symbol("Meta"));
    if (meta_mod_v == nullptr || !jl_is_module(meta_mod_v))
        throw std::logic_error{"Failed to look up Meta module inside Base"};

    jl_module_t* meta_mod = reinterpret_cast<jl_module_t*>(meta_mod_v);

    jl_function_t* parse_fn = jl_get_global(meta_mod, jl_symbol("parse"));
    if (parse_fn == nullptr)
        throw std::logic_error{"Failed to look up parse function inside the Meta module"};

    // implemented as a simple memcpy in libjulia, avoids strlen (==> null termination agnostic)
    code_jl_str = jl_pchar_to_string(code.data(), code.size());

    parsed_expr = jl_call1(parse_fn, code_jl_str);

    if (check_exceptions())
        throw std::runtime_error{
            "Julia exception while trying to parse code string using Meta.parse"};

    return parse(parsed_expr);
}

std::pair<jl_value_t*, TypeId> JLParser::parse_type_annotation(jl_expr_t* annot) {
    assert(annot->head == sym_cache.double_col);

    if (jl_expr_nargs(annot) != 2) {
        internal_error("unexpected type annotation layout");
        return {nullptr, TypeId::null_id()};
    }

    jl_value_t* target_v = jl_exprarg(annot, 0);

    jl_value_t* type_v = jl_exprarg(annot, 1);
    TypeId type_id     = resolve_type(type_v);

    return {target_v, type_id};
}

NodeId JLParser::parse_expr(jl_expr_t* expr) {
    jl_sym_t* head = expr->head;
    size_t nargs   = jl_expr_nargs(expr);

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

    if (head == sym_cache.break_)
        return emplace_node<BreakStmt>(cur_loc);

    if (head == sym_cache.continue_)
        return emplace_node<ContinueStmt>(cur_loc);

    if (head == sym_cache.arrow)
        return error("arrow functions are not currently supported");

    // TODO: julia dump node
    jl_static_show(jl_stderr_stream(), reinterpret_cast<jl_value_t*>(expr));
    std::cerr << '\n';
    return error("unsupported Expr node in Julia source code");
}

NodeId JLParser::parse_var_decl(jl_expr_t* expr, size_t nargs) {
    assert(expr->head == sym_cache.global || expr->head == sym_cache.local ||
           expr->head == sym_cache.eq || expr->head == sym_cache.double_col);

    SrcLocationId decl_loc = cur_loc;

    jl_value_t* inner    = reinterpret_cast<jl_value_t*>(expr);
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

    if (auto* assignment_expr = is_expr(inner, sym_cache.eq)) {
        if (jl_expr_nargs(assignment_expr) != 2)
            return internal_error("assignment expression with more/less than two args");

        inner = jl_exprarg(assignment_expr, 0);
        init  = jl_exprarg(assignment_expr, 1);
    }

    if (auto* typed_expr = is_expr(inner, sym_cache.double_col)) {
        if (jl_expr_nargs(typed_expr) != 2)
            return internal_error("type annotation expression with more/less than two args");

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
    assert(expr->head == sym_cache.function || expr->head == sym_cache.eq);

    if (nargs != 2)
        return internal_error("unexpected function definition layout, expected two argument for "
                              "both longdef and shortdef formats");

    SrcLocationId method_loc = cur_loc;

    // assignment lhs in shortdef, or header part of fn def
    jl_value_t* header_v = jl_exprarg(expr, 0);
    if (!jl_is_expr(header_v))
        return internal_error("unexpected function definition layout, couldn't unwrap header");
    jl_expr_t* header = reinterpret_cast<jl_expr_t*>(header_v);

    // function(x::T) where {T <: Int} ... end
    if (header->head == sym_cache.where)
        return error("type parameters in method definitions are not supported currently (i.e. "
                     "using 'where' in the header)");

    // function() ... end
    if (header->head == sym_cache.tuple)
        return error("anonymous functions are not currently supported");

    TypeId expl_ret_type = TypeId::null_id();
    // function f()::Int ... end
    if (header->head == sym_cache.double_col) {
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
            std::format("unexpected function header Expr kind: {}", jl_symbol_name(header->head)));

    size_t header_nargs = jl_expr_nargs(header);

    if (header_nargs == 0)
        return internal_error("unexpected empty function header layout");

    jl_value_t* name_v = jl_exprarg(header, 0);
    if (!jl_is_symbol(name_v))
        return internal_error("unexpected non-symbol name in function header");
    jl_sym_t* name = reinterpret_cast<jl_sym_t*>(name_v);

    SymbolId fn_name = ctx.sym_pool.get_id(jl_symbol_name(name));

    std::vector<NodeId> param_decls{};
    param_decls.reserve(header_nargs - 1);
    for (size_t i = 1; i < header_nargs; i++) {
        jl_value_t* arg_v = jl_exprarg(header, i);

        NodeId arg_id = parse_param_decl(arg_v);
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
        jl_expr_t* param_expr = safe_cast<jl_expr_t>(param);

        TypeId type = TypeId::null_id();
        NodeId init = NodeId::null_id();

        // TODO: kwargs
        if (param_expr->head == sym_cache.parameters)
            return error("kwargs are currently not supported");

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
            return error("variadic arguments are currently not supported");

        if (param_expr->head == sym_cache.double_col) {
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
        return internal_error("Assignment expression with more/less than two args");

    jl_value_t* lhs = jl_exprarg(expr, 0);

    // f(x, y) = x + y
    if (is_expr(lhs, sym_cache.call))
        return parse_method_decl(expr, nargs);

    // x::Int = 0
    if (is_expr(lhs, sym_cache.double_col))
        return parse_var_decl(expr, nargs);

    SrcLocationId outer_loc = cur_loc;

    NodeId parsed_lhs = parse(lhs);
    NodeId parsed_rhs = parse(jl_exprarg(expr, 1));

    return emplace_node<Assignment>(outer_loc, parsed_lhs, parsed_rhs);
}

NodeId JLParser::parse_block(jl_expr_t* expr, size_t nargs) {
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
    assert(expr->head == sym_cache.call);

    SrcLocationId call_loc = cur_loc;

    if (nargs == 0)
        return internal_error("function call expression with zero arguments");

    NodeId parsed_target_fn = parse(jl_exprarg(expr, 0));

    std::vector<NodeId> params;
    for (size_t i = 1; i < nargs; i++) {
        jl_value_t* arg = jl_exprarg(expr, i);

        // handle kwargs
        auto* arg_expr = try_cast<jl_expr_t>(arg);
        if (arg_expr != nullptr && arg_expr->head == sym_cache.parameters) {
            // TODO
            // args traversal where arg is either Expr(:kw, k, v) or Symbol (<=> Expr(:kw, k, k))
            throw std::runtime_error{"Keyword arguments are not currently supported"};
        }

        NodeId parsed_arg = parse(arg);
        params.push_back(parsed_arg);
    }

    return emplace_node<FunctionCall>(call_loc, parsed_target_fn, std::move(params));
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
        return internal_error("unexpected return expr layout (more or less than one arg)");

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
    assert(expr->head == sym_cache.dot);

    if (nargs != 2)
        return internal_error("unexpected dot expr layout (more or less than two args)");

    jl_value_t* current_lhs = jl_exprarg(expr, 0);
    jl_value_t* current_rhs = jl_exprarg(expr, 1);

    auto sym_lit_from_dre = [&](NodeId parsed_node) {
        auto* dre = ctx.get_and_dyn_cast<DeclRefExpr>(parsed_node);
        if (dre == nullptr)
            return internal_error("parser returned non-decl-ref-expr node for symbol/quotenode");

        NodeId sym_lit = dre->decl;
        assert(ctx.isa<SymbolLiteral>(sym_lit) && "non-sym-lit node in parsed dre");

        return sym_lit;
    };

    std::vector<NodeId> lookup_chain{};
    bool is_done = false;
    while (!is_done) {
        if (!jl_is_symbol(current_rhs) && !jl_is_quotenode(current_rhs))
            return internal_error(
                "unexpected dot expr layout (rhs is neither a Symbol or a QuoteNode)");

        NodeId parsed_rhs = parse(current_rhs);
        lookup_chain.emplace_back(sym_lit_from_dre(parsed_rhs));

        if (jl_is_symbol(current_lhs) || jl_is_quotenode(current_lhs)) {
            NodeId parsed_lhs = parse(current_lhs);

            lookup_chain.emplace_back(sym_lit_from_dre(parsed_lhs));
            is_done = true;
            continue;
        }

        if (is_expr(current_lhs, sym_cache.dot)) {
            jl_expr_t* lhs_expr = safe_cast<jl_expr_t>(current_lhs);

            if (jl_expr_nargs(lhs_expr) != 2)
                return internal_error("unexpected dot expr layout (more or less than two args)");

            current_lhs = jl_exprarg(lhs_expr, 0);
            current_rhs = jl_exprarg(lhs_expr, 1);
            continue;
        }

        // e.g. fn_that_returns_a_mod().some_var
        return error(
            "module lookup chains (dot expressions) containg non-symbol nodes are not allowed");
    }

    // chains are a recursive structure, so the above flattening creates the reversed version of the
    // resulting chain
    std::reverse(lookup_chain.begin(), lookup_chain.end());

    NodeId mod_lookup = emplace_node<ModuleLookup>(cur_loc, std::move(lookup_chain));
    return emplace_node<DeclRefExpr>(cur_loc, mod_lookup);
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
        return internal_error("unexpected ref expression layout (less than two args)");

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

NodeId JLParser::error(std::string_view msg, SrcLocationId loc_id) {
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
