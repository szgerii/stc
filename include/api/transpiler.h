#pragma once

#include "api/benchmark_tracker.h"
#include "common/config.h"
#include "frontend/jl/ast.h"
#include "frontend/jl/context.h"

#include <optional>
#include <string>
#include <string_view>

namespace stc::api {

using MaybeString = std::optional<std::string>;

template <bool RunBenchmark>
MaybeString transpile_parsed(jl::NodeId jl_ast, jl::JLCtx& jl_ctx,
                             detail::BenchmarkTracker<RunBenchmark>& benchmark_tracker);

template <bool RunBenchmark>
MaybeString transpile(std::string_view code, std::optional<std::string_view> file_path,
                      stc::TranspilerConfig config);

template <bool RunBenchmark>
MaybeString transpile(jl_value_t* expr_v, stc::TranspilerConfig config);

}; // namespace stc::api
