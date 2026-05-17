#include "api/transpiler.h"
#include "frontend/jl/utils.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace stc;

namespace {

std::optional<std::string> read_file(std::filesystem::path path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "couldn't open input file at '" << path << "'\n";
        return std::nullopt;
    }

    std::stringstream code_stream;
    code_stream << "begin ";
    code_stream << file.rdbuf();
    code_stream << " end";

    return code_stream.str();
}

std::optional<std::string> handle_transpile(std::filesystem::path path) {
    auto code = read_file(path);
    if (!code.has_value())
        return std::nullopt;

    TranspilerConfig config{};

    return api::transpile<false>(*code, path.string(), config);
}

std::filesystem::path shader_path(std::string_view shader_name) {
    static std::filesystem::path root = PROJECT_ROOT;

    return root / "examples" / shader_name;
}

} // namespace

TEST_CASE("Transpile Example Julia Shaders", "[Transpiler]") {
    jl_init();
    REQUIRE_FALSE(jl::check_exceptions());

    jl_eval_string("using JuliaGLM");
    REQUIRE_FALSE(jl::check_exceptions());

    SECTION("sdf_disk.jl") {
        REQUIRE(handle_transpile(shader_path("sdf_disk.jl")).has_value());
    }

    SECTION("sdf_rounded_box.jl") {
        REQUIRE(handle_transpile(shader_path("sdf_rounded_box.jl")).has_value());
    }

    SECTION("julia_quat.jl") {
        REQUIRE(handle_transpile(shader_path("julia_quat.jl")).has_value());
    }

    SECTION("bryant_kusner.jl") {
        REQUIRE(handle_transpile(shader_path("bryant_kusner.jl")).has_value());
    }
}
