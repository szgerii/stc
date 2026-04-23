#include <julia_guard.h>
JULIA_DEFINE_FAST_TLS

#include <backend/glsl/code_gen.h>
#include <backend/glsl/target_info.h>
#include <frontend/jl/dumper.h>
#include <frontend/jl/lowering.h>
#include <frontend/jl/parser.h>
#include <frontend/jl/sema.h>
#include <sir/dumper.h>
#include <sir/sema.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

std::filesystem::path tail_from_cwd(const std::filesystem::path& p) {
    namespace fs = std::filesystem;

    fs::path abs = fs::absolute(p);
    fs::path cwd = fs::current_path();

    fs::path rel = abs.lexically_relative(cwd);

    if (!rel.empty())
        return rel;

    return abs;
}

std::string format_duration(std::chrono::nanoseconds dur) {
    using namespace std::chrono;

    duration<double, std::milli> exact_ms = dur;
    duration<double, std::micro> exact_us = dur;

    return duration_cast<milliseconds>(dur).count() > 0
               ? std::format("{:.3f} ms", exact_ms.count())
               : std::format("{:.0f} us", exact_us.count());
}

int transpile(std::string_view code, std::string file_path, stc::TranspilerConfig config,
              bool dump_parsed, bool dump_sema, bool dump_lowered, bool write_to_file) {
    using namespace stc;
    using namespace stc::jl;
    using clock = std::chrono::steady_clock;

    auto start = clock::now();

    JLCtx ctx{};
    ctx.config = std::move(config);

    const auto& target_info = glsl::GLSLTargetInfo::get(ctx.type_pool);
    ctx.target_info         = &target_info;

    auto init_done = clock::now();

    JLParser parser{ctx};
    parser.fallback_file = file_path;

    NodeId jl_ast = parser.parse_code(code);

    if (!parser.success()) {
        std::cerr << "\nJulia parser failed" << std::endl;
        return 1;
    }

    auto parser_done = clock::now();

    if (dump_parsed) {
        JLDumper dumper{ctx, std::cout};
        dumper.visit(jl_ast);
    }

    auto* cmpd = ctx.get_and_dyn_cast<CompoundExpr>(jl_ast);
    if (cmpd == nullptr)
        throw std::logic_error{"outermost node is not a compound expression"};

    auto sema_start = clock::now();

    JLSema sema{ctx, *cmpd};
    sema.infer(jl_ast);
    sema.finalize();

    auto sema_done = clock::now();

    if (!sema.success()) {
        std::cerr << "\nJulia sema failed" << std::endl;
        return 1;
    }

    if (dump_sema) {
        JLDumper dumper{ctx, std::cout};
        dumper.visit(jl_ast);
    }

    auto lowering_start = clock::now();

    JLLoweringVisitor lowering{std::move(ctx)};
    auto sir_ast = lowering.lower(jl_ast);

    if (!lowering.successful()) {
        std::cerr << "\nJulia -> SIR lowering failed" << std::endl;
        return 1;
    }

    auto glsl_ctx = glsl::GLSLCtx(std::move(lowering.sir_ctx));

    auto lowering_done = clock::now();

    if (dump_lowered) {
        sir::SIRDumper dumper{glsl_ctx, std::cout};
        dumper.visit(sir_ast);
    }

    auto codegen_start = clock::now();

    glsl::GLSLCodeGenVisitor code_gen_vis{glsl_ctx};
    code_gen_vis.visit(sir_ast);

    if (!code_gen_vis.successful()) {
        std::cerr << "\nGLSL code gen failed" << std::endl;
        return 1;
    }

    auto end = clock::now();

    if (write_to_file) {
        // gotta do C-style file writing, cause libjulia messes with std::locale in a way that
        // breaks std::ofstream in release builds

        FILE* out_file = fopen("out.comp", "w");
        if (out_file) {
            const std::string& res = code_gen_vis.result();
            fwrite(res.data(), 1, res.size(), out_file);
            fclose(out_file);
        } else {
            std::cerr << "\nFailed to open out.comp for writing" << std::endl;
            return 1;
        }
    }

    std::cout << "\nCtx init finished in " << format_duration(init_done - start) << '\n';
    std::cout << "Parser finished in " << format_duration(parser_done - init_done) << '\n';
    std::cout << "Sema finished in " << format_duration(sema_done - sema_start) << '\n';
    std::cout << "Lowering finished in " << format_duration(lowering_done - lowering_start) << '\n';
    std::cout << "Codegen finished in " << format_duration(end - codegen_start) << '\n';
    std::cout << "\nEntire transpilation pipeline finished in " << format_duration(end - start)
              << '\n';

    std::cout.flush();
    return 0;
}

std::optional<std::size_t> try_parse_size_t(const std::string& str) {
    size_t value;
    auto [ptr, err] = std::from_chars(str.data(), str.data() + str.size(), value);

    if (err == std::errc() && ptr == str.data() + str.size())
        return value;

    return std::nullopt;
}

int main(int argc, char* argv[]) {
    using namespace stc;
    using namespace stc::jl;

    TranspilerConfig config{};

    bool dump_parsed  = false;
    bool dump_sema    = false;
    bool dump_lowered = false;
    auto err_dump     = config.err_dump_verbosity;
    size_t ite_count  = 1U;
    for (int i = 2; i < argc; i++) {
        std::string arg{argv[i]};
        if (arg == "--dump-parsed")
            dump_parsed = true;
        else if (arg == "--dump-sema")
            dump_sema = true;
        else if (arg == "--dump-lowered")
            dump_lowered = true;
        else if (arg == "--dump-scopes")
            config.dump_scopes = true;
        else if (arg == "--fwd-fns")
            config.forward_fns = true;
        else if (arg == "--conv-fail-reason")
            config.print_conv_fail_reason = true;
        else if (arg == "--no-coercion") {
            config.coerce_to_i32 = false;
            config.coerce_to_f32 = false;
        } else if (arg == "-Wfwd-fns")
            config.warn_on_fn_forward = true;
        else if (arg == "-Wjl-query")
            config.warn_on_jl_sema_query = true;
        else if (arg == "--errdump-none")
            err_dump = DumpVerbosity::None;
        else if (arg == "--errdump-partial")
            err_dump = DumpVerbosity::Partial;
        else if (arg == "--errdump-verbose")
            err_dump = DumpVerbosity::Verbose;
        else if (arg == "--it") {
            if (i + 1 >= argc) {
                std::cerr << "--it must be followed by the number of iterations";
                return 1;
            }

            auto maybe_ite_count = try_parse_size_t(std::string{argv[i + 1]});

            if (!maybe_ite_count.has_value()) {
                std::cerr << "--it followed by a non-numeric string";
                return 1;
            }

            ite_count = *maybe_ite_count;
            i++;
        } else {
            std::cerr << std::format("unknown argument: {}", arg);
            return 1;
        }
    }

    config.err_dump_verbosity = err_dump;

// ! TODO: remove
#ifdef _WIN32
    std::string path = argc > 1 ? argv[1] : "C:\\Users\\szucs\\szakdoga\\stc\\test.jl";
#else
    std::string path = argc > 1 ? argv[1] : "/mnt/c/Users/szucs/szakdoga/stc/test.jl";
#endif

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Couldn't open input file at '" << path << "'\n";
        return 1;
    }

    std::stringstream code_stream;
    code_stream << file.rdbuf();
    std::string code{code_stream.str()};

#define STC_CHECK_EXCEPTIONS                                                                       \
    if (check_exceptions()) {                                                                      \
        std::cerr << "\nAn error occured while initializing Julia\n";                              \
        return 1;                                                                                  \
    }

#define STC_EVAL_AND_CHECK(str)                                                                    \
    jl_eval_string(str);                                                                           \
    STC_CHECK_EXCEPTIONS

    std::cout << "Initializing Julia environment...\n";
    jl_init();
    STC_CHECK_EXCEPTIONS

    STC_EVAL_AND_CHECK("using Pkg");

#ifdef _WIN32
    STC_EVAL_AND_CHECK("Pkg.develop(path=\"C:\\\\Users\\\\szucs\\\\szakdoga\\\\juliagebra\")");
    STC_EVAL_AND_CHECK("Pkg.develop(path=\"C:\\\\Users\\\\szucs\\\\szakdoga\\\\stc\\\\STJL\")");
#else
    STC_EVAL_AND_CHECK("Pkg.develop(path=\"/mnt/c/Users/szucs/szakdoga/juliagebra\")");
    STC_EVAL_AND_CHECK("Pkg.develop(path=\"/mnt/c/Users/szucs/szakdoga/stc/STJL\")");
#endif

    STC_EVAL_AND_CHECK("import JuliaGLM");
    STC_EVAL_AND_CHECK("import STJL");

    // ! TODO: remove (current purpose: jl cache warmup + force compile return_type)
    STC_EVAL_AND_CHECK("sin(0.5f0) + 1");
    STC_EVAL_AND_CHECK("Core.Compiler.return_type(sin, Tuple{Float32})");
    STC_EVAL_AND_CHECK("Core.Compiler.return_type(JuliaGLM.sin, Tuple{JuliaGLM.Vec3})");

    std::cout << "Starting transpilation...\n";

    const ScopeGuard jl_guard{[&]() { jl_atexit_hook(0); }};

    for (size_t i = 0; i < ite_count; i++) {
        if (ite_count != 1) {
            std::cout << std::format("\n======================\n"
                                     "Transpilation #{}\n"
                                     "======================\n",
                                     i + 1);
        }

        int result = transpile(code, tail_from_cwd(path).string(), config, dump_parsed, dump_sema,
                               dump_lowered, i + 1 == ite_count);

        if (result != 0) {
            std::cout << "\nAn error occured during transpilation\n";
            std::cout.flush();
            return result;
        }
    }

    return 0;

#undef STC_EVAL_AND_CHECK
#undef STC_CHECK_EXCEPTIONS
}
