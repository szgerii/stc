#include <cstring>
#include <iostream>

#include "frontend/jl/dumper.h"
#include "frontend/jl/lowering.h"
#include "frontend/jl/parser.h"
#include "frontend/jl/pkg_api.h"
#include "frontend/jl/sema.h"

#include "backend/glsl/code_gen.h"

#include "backend/glsl/target_info.h"

namespace {

const char* do_transpile(jl_value_t* expr_v) {
    using namespace stc;
    using namespace stc::jl;

    if (!jl_is_expr(expr_v)) {
        std::cerr << "received non-Expr value\n";
        return nullptr;
    }

    JLCtx jl_ctx{};

    const auto& target_info = glsl::GLSLTargetInfo::get(jl_ctx.type_pool);
    jl_ctx.target_info      = &target_info;

    JLParser parser{jl_ctx};
    NodeId jl_ast = parser.parse(expr_v);

    if (!parser.success()) {
        std::cerr << "parser failed\n";
        return nullptr;
    }

    auto* global_cmpd = jl_ctx.get_and_dyn_cast<CompoundExpr>(jl_ast);
    if (global_cmpd == nullptr) {
        std::cerr << "top node is not a compound expression\n";
        return nullptr;
    }

    JLSema sema{jl_ctx, *global_cmpd};
    sema.infer(jl_ast);
    sema.finalize();

    if (!sema.success()) {
        std::cerr << "sema failed\n";
        return nullptr;
    }

    JLLoweringVisitor lowering{std::move(jl_ctx)};
    auto sir_ast = lowering.lower(jl_ast);

    if (!lowering.success()) {
        std::cerr << "lowering failed\n";
        return nullptr;
    }

    auto glsl_ctx = glsl::GLSLCtx(std::move(lowering.sir_ctx));

    glsl::GLSLCodeGenVisitor cg{glsl_ctx};
    cg.visit(sir_ast);

    if (!cg.success()) {
        std::cerr << "codegen failed\n";
        return nullptr;
    }

    // TODO: we can be more efficient than this
    std::string result     = cg.result();
    size_t result_mem_size = (result.size() + 1U) * sizeof(char);

    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    char* str_mem = reinterpret_cast<char*>(std::malloc(result_mem_size));
    if (str_mem == nullptr) {
        std::cerr << "couldn't alloc memory for returned copy\n";
        return nullptr;
    }

    std::memcpy(str_mem, result.data(), result_mem_size);

    return str_mem;
}

} // namespace

namespace stc::jl {

extern "C" STC_API void stc_jl_free(void* ptr) noexcept {
    if (ptr != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
        std::free(ptr);
    }
}

extern "C" STC_API void stc_jl_test() noexcept {
    std::cout << "writing to stdout\n";
    std::cerr << "writing to stderr\n";
}

// TODO: config API

extern "C" STC_API const char* stc_jl_transpile(jl_value_t* expr_v) noexcept {
    try {
        return do_transpile(expr_v);
    } catch (const std::exception& ex) {
        std::cerr << "the following std::exception was thrown during transpilation:\n";
        std::cerr << ex.what() << '\n';
    } catch (...) {
        std::cerr << "an unexpected error was thrown during transpilation\n";
    }

    return nullptr;
}

} // namespace stc::jl
