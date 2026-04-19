#pragma once

#include <cstdint>

namespace stc {

// None -> nothing is dumped when an error is encountered
// Partial -> the affected subtree of the AST is dumped when an error is encountered
// Verbose -> the entire AST is dumped when an error is encountered
enum class DumpVerbosity : uint8_t { None = 0, Partial, Verbose };

struct TranspilerConfig {
    size_t code_gen_indent           = 4;
    size_t dump_indent               = 2;
    DumpVerbosity err_dump_verbosity = DumpVerbosity::None;
    bool use_tabs                    = false;
    bool dump_scopes                 = false;
    bool warn_on_jl_sema_query       = false;
};

} // namespace stc
