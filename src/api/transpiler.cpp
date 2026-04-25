#include "api/transpiler.h"

#include "backend/glsl/code_gen.h"
#include "frontend/jl/lowering.h"
#include "frontend/jl/parser.h"
#include "frontend/jl/sema.h"
#include <backend/glsl/target_info.h>
#include <frontend/jl/dumper.h>
#include <sir/dumper.h>

namespace stc::api {

template <bool RunBenchmark>
MaybeString transpile_parsed(jl::NodeId jl_ast, jl::JLCtx& jl_ctx,
                             detail::BenchmarkTracker<RunBenchmark>& benchmark_tracker) {
    using namespace stc::jl;

    auto* cmpd = jl_ctx.get_and_dyn_cast<CompoundExpr>(jl_ast);
    if (cmpd == nullptr) {
        std::cerr << "outermost AST node is not a compound expression\n";
        return std::nullopt;
    }

    benchmark_tracker.sema_start();

    JLSema sema{jl_ctx, *cmpd};
    sema.infer(jl_ast);
    sema.finalize();

    benchmark_tracker.sema_end();

    if (!sema.success()) {
        std::cerr << "\nJulia sema failed\n";
        return std::nullopt;
    }

    if (jl_ctx.config.dump_sema) {
        JLDumper dumper{jl_ctx, std::cout};
        dumper.visit(jl_ast);
    }

    benchmark_tracker.lowering_start();

    JLLoweringVisitor lowering{std::move(jl_ctx)};
    auto sir_ast = lowering.lower(jl_ast);

    if (!lowering.success()) {
        std::cerr << "\nJulia -> SIR lowering failed\n";
        return std::nullopt;
    }

    auto glsl_ctx = glsl::GLSLCtx(std::move(lowering.sir_ctx));

    benchmark_tracker.lowering_end();

    if (glsl_ctx.config.dump_lowered) {
        sir::SIRDumper dumper{glsl_ctx, std::cout};
        dumper.visit(sir_ast);
    }

    benchmark_tracker.code_gen_start();

    glsl::GLSLCodeGenVisitor code_gen_vis{glsl_ctx};
    code_gen_vis.visit(sir_ast);

    if (!code_gen_vis.success()) {
        std::cerr << "\nGLSL code gen failed\n";
        return std::nullopt;
    }

    benchmark_tracker.code_gen_end();
    benchmark_tracker.end();

    benchmark_tracker.print(std::cout);

    return code_gen_vis.move_result();
}

template MaybeString transpile_parsed<true>(jl::NodeId, jl::JLCtx&,
                                            detail::BenchmarkTracker<true>&);
template MaybeString transpile_parsed<false>(jl::NodeId, jl::JLCtx&,
                                             detail::BenchmarkTracker<false>&);

template <bool RunBenchmark>
MaybeString transpile(std::string_view code, std::optional<std::string_view> file_path,
                      stc::TranspilerConfig config) {
    using namespace stc::jl;

    detail::BenchmarkTracker<RunBenchmark> benchmark_tracker{};

    benchmark_tracker.start();
    benchmark_tracker.init_start();

    JLCtx jl_ctx{};
    jl_ctx.config = std::move(config);

    glsl::GLSLTargetInfo target_info{jl_ctx.type_pool};
    jl_ctx.target_info = &target_info;

    benchmark_tracker.init_end();
    benchmark_tracker.parser_start();

    JLParser parser{jl_ctx};

    if (file_path.has_value())
        parser.fallback_file = *file_path;

    NodeId jl_ast = parser.parse_code(code);

    if (!parser.success()) {
        std::cerr << "\nJulia parser failed\n";
        return std::nullopt;
    }

    benchmark_tracker.parser_end();

    if (jl_ctx.config.dump_parsed) {
        JLDumper dumper{jl_ctx, std::cout};
        dumper.visit(jl_ast);
    }

    return transpile_parsed<RunBenchmark>(jl_ast, jl_ctx, benchmark_tracker);
}

template MaybeString transpile<true>(std::string_view, std::optional<std::string_view>,
                                     stc::TranspilerConfig);
template MaybeString transpile<false>(std::string_view, std::optional<std::string_view>,
                                      stc::TranspilerConfig);

template <bool RunBenchmark>
MaybeString transpile(jl_value_t* expr_v, stc::TranspilerConfig config) {
    using namespace stc::jl;

    detail::BenchmarkTracker<RunBenchmark> benchmark_tracker{};

    benchmark_tracker.start();
    benchmark_tracker.init_start();

    JLCtx jl_ctx{};
    jl_ctx.config = std::move(config);

    glsl::GLSLTargetInfo target_info{jl_ctx.type_pool};
    jl_ctx.target_info = &target_info;

    benchmark_tracker.init_end();
    benchmark_tracker.parser_start();

    JLParser parser{jl_ctx};

    NodeId jl_ast = parser.parse(expr_v);

    if (!parser.success()) {
        std::cerr << "\nJulia parser failed\n";
        return std::nullopt;
    }

    benchmark_tracker.parser_end();

    if (jl_ctx.config.dump_parsed) {
        JLDumper dumper{jl_ctx, std::cout};
        dumper.visit(jl_ast);
    }

    return transpile_parsed<RunBenchmark>(jl_ast, jl_ctx, benchmark_tracker);
}

template MaybeString transpile<true>(jl_value_t*, stc::TranspilerConfig);
template MaybeString transpile<false>(jl_value_t*, stc::TranspilerConfig);

} // namespace stc::api
