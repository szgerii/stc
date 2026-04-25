#include <julia_guard.h>
JULIA_DEFINE_FAST_TLS

#include <backend/glsl/code_gen.h>
#include <backend/glsl/target_info.h>
#include <common/term_utils.h>
#include <frontend/jl/dumper.h>
#include <frontend/jl/lowering.h>
#include <frontend/jl/parser.h>
#include <frontend/jl/sema.h>
#include <meta/version.h>
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

int transpile(std::string_view code, std::string_view file_path, stc::TranspilerConfig config,
              bool dump_parsed, bool dump_sema, bool dump_lowered, bool write_to_file,
              const std::string& out_path) {
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
        std::cerr << "\nJulia parser failed\n";
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
        std::cerr << "\nJulia sema failed\n";
        return 1;
    }

    if (dump_sema) {
        JLDumper dumper{ctx, std::cout};
        dumper.visit(jl_ast);
    }

    auto lowering_start = clock::now();

    JLLoweringVisitor lowering{std::move(ctx)};
    auto sir_ast = lowering.lower(jl_ast);

    if (!lowering.success()) {
        std::cerr << "\nJulia -> SIR lowering failed\n";
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

    if (!code_gen_vis.success()) {
        std::cerr << "\nGLSL code gen failed\n";
        return 1;
    }

    auto end = clock::now();

    if (write_to_file) {
        // gotta do C-style file writing, cause libjulia messes with std::locale in a way that
        // breaks std::ofstream in release builds

        stc::FileRAII out_file{out_path, "w"};
        if (out_file.get() != nullptr) {
            const std::string& res = code_gen_vis.result();
            fwrite(res.data(), 1, res.size(), out_file.get());
        } else {
            std::cerr << "\nFailed to open '" << out_path << "' for writing (errno: " << errno
                      << '\n';
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
    size_t value    = 0;
    auto [ptr, err] = std::from_chars(str.data(), str.data() + str.size(), value);

    if (err == std::errc() && ptr == str.data() + str.size())
        return value;

    return std::nullopt;
}

void print_help() {
    static constexpr auto title_col = ansi_codes::cyan;
    static constexpr auto flag_col  = ansi_codes::yellow;

    // clang-format off
    std::cout << stc::colored("usage:", title_col) << " stc <input file> [OPTIONS]\n\n";

    std::cout << stc::colored("general:\n", title_col);
    std::cout << "  " << stc::colored("-h, --help",         flag_col)        << "           show this help message and exit\n";
    std::cout << "  " << stc::colored("-v, --version",      flag_col)        << "        show version information and exit\n";
    std::cout << "  " << stc::colored("-o <path>",          flag_col)        << "            set output file path (default: out.comp)\n";
    std::cout << "  " << stc::colored("--gl-version <ver>", flag_col)        << "   set OpenGL version for #version directive (default: \"460\")\n";
    std::cout << "  " << stc::colored("--it <n>",           flag_col)        << "             run transpilation N times (for benchmarking)\n";
    std::cout << '\n';
    
    std::cout << stc::colored("transpilation behavior:\n", title_col);
    std::cout << "  " << stc::colored("--conv-fail-reason", flag_col)   << "   print reason for conversion/casting failures\n";
    std::cout << "  " << stc::colored("--no-coerce-i32",    flag_col)   << "      disable automatic Int64 -> Int32 literal type coercion\n";
    std::cout << "  " << stc::colored("--no-coerce-f32",    flag_col)   << "      disable automatic Float64 -> Float32 literal type coercion\n";
    std::cout << "  " << stc::colored("--no-coercion",      flag_col)   << "        disable all literal type coercion\n";
    std::cout << "  " << stc::colored("--fwd-fns",          flag_col)   << "            enable blind forwarding for non-backend-resolvable function calls (may lead to invalid code)\n";
    std::cout << '\n';
    
    std::cout << stc::colored("debugging:\n", title_col);
    std::cout << "  " << stc::colored("--dump-parsed",  flag_col)       << "        print the parsed version of the Julia AST\n";
    std::cout << "  " << stc::colored("--dump-sema",    flag_col)       << "          print the AST after semantic analysis\n";
    std::cout << "  " << stc::colored("--dump-lowered", flag_col)       << "       print the lowered (SIR) representation\n";
    std::cout << "  " << stc::colored("--dump-scopes",  flag_col)       << "        dump scope tree info during semantic analysis\n";
    std::cout << '\n';
    
    std::cout << stc::colored("formatting:\n", title_col);
    std::cout << "  " << stc::colored("--tabs",          flag_col)      << "               use tabs for indentation\n";
    std::cout << "  " << stc::colored("--spaces",        flag_col)      << "             use spaces for indentation (default)\n";
    std::cout << "  " << stc::colored("--cg-indent <n>", flag_col)      << "      set indentation width (default: 4)\n";
    std::cout << '\n';

    std::cout << stc::colored("errors and warnings:\n", title_col);
    std::cout << "  " << stc::colored("--err-dump-none",    flag_col)   << "      disable AST dumping on errors (default)\n";
    std::cout << "  " << stc::colored("--err-dump-partial", flag_col)   << "   dump the AST subtree that caused the given error\n";
    std::cout << "  " << stc::colored("--err-dump-verbose", flag_col)   << "   dump the full AST at every error reported\n";
    std::cout << "  " << stc::colored("-Wfwd-fns",          flag_col)   << "            warn when a function call is forwarded (see --fwd-fns)\n";
    std::cout << "  " << stc::colored("-Wjl-query",         flag_col)   << "           warn when the Julia runtime was queried for function call resolution\n";
    // clang-format on
}

void print_version_info() {
    static constexpr auto val_col = ansi_codes::yellow;

    std::string header{"  Shader Transpiler Core (STC)"};
    std::string stc_ver{" v" STC_META_VERSION "  "};

    std::string sep(header.size() + stc_ver.size(), '-');

    std::cout << sep << '\n';
    std::cout << header;

    std::cout << stc::colored(stc_ver, val_col) << '\n';

    std::cout << sep << "\n\n";

    // clang-format off
    std::cout << "target:       " << stc::colored(STC_META_SYSTEM_ARCH "-" STC_META_SYSTEM_NAME, val_col) << '\n';
    std::cout << "compiler:     " << stc::colored(STC_META_COMPILER_ID " " STC_META_COMPILER_VERSION, val_col) << '\n';
    std::cout << "build type:   " << stc::colored(STC_META_BUILD_TYPE, val_col) << '\n';
    std::cout << "build commit: " << stc::colored(STC_META_BUILD_COMMIT, val_col) << '\n';
    std::cout << "julia compat: " << stc::colored(STC_META_JULIA_VERSION, val_col) << '\n';
    std::cout << "\n";
    std::cout << "build date:   ";
    // clang-format on

    if (stc::TerminalInfo::supports_color())
        std::cout << val_col;

    std::cout << __DATE__ << ", " << __TIME__ << '\n';

    if (stc::TerminalInfo::supports_color())
        std::cout << ansi_codes::reset;

    std::cout << "repo link:    " << stc::colored("https://github.com/szgerii/stc", val_col)
              << '\n';
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
int run(int argc, char* argv[]) {
    using namespace stc;
    using namespace stc::jl;

    if (argc < 2) {
        std::cerr << stc::colored("not enough arguments\n", ansi_codes::red);
        std::cerr << "usage: stc <input file> [OPTIONS]\n";
        std::cerr << "run with " << stc::colored("--help", ansi_codes::yellow) << " or "
                  << stc::colored("-h", ansi_codes::yellow) << " for a list of available options\n";
        return 1;
    }

    // only help or version info can be invoked on their own
    // for every other case, arguments must start with the input path

    std::string argv1{argv[1]};

    if (argv1 == "-h" || argv1 == "--help") {
        print_help();
        return 0;
    }

    if (argv1 == "-v" || argv1 == "--version") {
        print_version_info();
        return 0;
    }

    std::string path = argv[1];

    TranspilerConfig config{};
    bool dump_parsed     = false;
    bool dump_sema       = false;
    bool dump_lowered    = false;
    auto err_dump        = config.err_dump_verbosity;
    size_t ite_count     = 1U;
    std::string out_path = "out.comp";

    for (int i = 2; i < argc; i++) {
        std::string arg{argv[i]};

        if (arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        }

        if (arg == "-v" || arg == "--version") {
            print_version_info();
            return 0;
        }

        if (arg == "--dump-parsed")
            dump_parsed = true;
        else if (arg == "--dump-sema")
            dump_sema = true;
        else if (arg == "--dump-lowered")
            dump_lowered = true;
        else if (arg == "--dump-scopes")
            config.dump_scopes = true;
        else if (arg == "--spaces")
            config.use_tabs = false;
        else if (arg == "--tabs")
            config.use_tabs = true;
        else if (arg == "--fwd-fns")
            config.forward_fns = true;
        else if (arg == "--conv-fail-reason")
            config.print_conv_fail_reason = true;
        else if (arg == "--no-coerce-i32")
            config.coerce_to_i32 = false;
        else if (arg == "--no-coerce-f32")
            config.coerce_to_f32 = false;
        else if (arg == "--no-coercion") {
            config.coerce_to_i32 = false;
            config.coerce_to_f32 = false;
        } else if (arg == "-Wfwd-fns")
            config.warn_on_fn_forward = true;
        else if (arg == "-Wjl-query")
            config.warn_on_jl_sema_query = true;
        else if (arg == "--err-dump-none")
            err_dump = DumpVerbosity::None;
        else if (arg == "--err-dump-partial")
            err_dump = DumpVerbosity::Partial;
        else if (arg == "--err-dump-verbose")
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
        } else if (arg == "--cg-indent") {
            if (i + 1 >= argc) {
                std::cerr << "--cg-indent must be followed by the width of indentation (in spaces)";
                return 1;
            }

            auto maybe_cg_indent = try_parse_size_t(std::string{argv[i + 1]});

            if (!maybe_cg_indent.has_value()) {
                std::cerr << "--cg-indent followed by a non-numeric string";
                return 1;
            }

            config.code_gen_indent = *maybe_cg_indent;
            i++;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "-o must be followed by the output file's path";
                return 1;
            }

            out_path = argv[i + 1];
            i++;
        } else if (arg == "--gl-version") {
            if (i + 1 >= argc) {
                std::cerr << "--gl-version must be followed by the desired OpenGL version";
                return 1;
            }

            config.target_version = std::string{argv[i + 1]};
            i++;
        } else {
            std::cerr << std::format("unknown argument: {}\n", arg);
            return 1;
        }
    }

    config.err_dump_verbosity = err_dump;

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

    std::cout << "Initializing Julia environment for transpilation...\n";

    jl_init();
    STC_CHECK_EXCEPTIONS

    STC_EVAL_AND_CHECK("import JuliaGLM");
    STC_EVAL_AND_CHECK("import STJL");

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
                               dump_lowered, i + 1 == ite_count, out_path);

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

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "an unexpected error occured while running STC:\n";
        std::cerr << ex.what() << '\n';
    } catch (...) {
        std::cerr << "an unexpected error occured while running STC\n";
    }
}
