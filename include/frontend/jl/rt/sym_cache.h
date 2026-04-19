#pragma once

#include "julia_guard.h"

namespace stc::jl::rt {

struct JuliaSymbolCache {
    // keywords
    jl_sym_t* block      = jl_symbol("block");
    jl_sym_t* global     = jl_symbol("global");
    jl_sym_t* local      = jl_symbol("local");
    jl_sym_t* eq         = jl_symbol("=");
    jl_sym_t* call       = jl_symbol("call");
    jl_sym_t* if_        = jl_symbol("if");
    jl_sym_t* elseif     = jl_symbol("elseif");
    jl_sym_t* return_    = jl_symbol("return");
    jl_sym_t* break_     = jl_symbol("break");
    jl_sym_t* continue_  = jl_symbol("continue");
    jl_sym_t* kw         = jl_symbol("kw");         // default initialized parameter
    jl_sym_t* parameters = jl_symbol("parameters"); // kwarg collection
    jl_sym_t* double_col = jl_symbol("::");
    jl_sym_t* nothing    = jl_symbol("nothing");
    jl_sym_t* while_     = jl_symbol("while");
    jl_sym_t* function   = jl_symbol("function");
    jl_sym_t* curly      = jl_symbol("curly"); // T{U}
    jl_sym_t* arrow      = jl_symbol("->");    // () -> ()
    jl_sym_t* tuple      = jl_symbol("tuple");
    jl_sym_t* where      = jl_symbol("where");
    jl_sym_t* dot        = jl_symbol(".");
    jl_sym_t* dots       = jl_symbol("...");
    jl_sym_t* vect       = jl_symbol("vect"); // [1, 2, 3]
    jl_sym_t* ref        = jl_symbol("ref");  // arr[i]

    // types
    jl_sym_t* Bool    = jl_symbol("Bool");
    jl_sym_t* Int     = jl_symbol("Int");
    jl_sym_t* UInt    = jl_symbol("UInt");
    jl_sym_t* Int8    = jl_symbol("Int8");
    jl_sym_t* Int16   = jl_symbol("Int16");
    jl_sym_t* Int32   = jl_symbol("Int32");
    jl_sym_t* Int64   = jl_symbol("Int64");
    jl_sym_t* Int128  = jl_symbol("Int128");
    jl_sym_t* UInt8   = jl_symbol("UInt8");
    jl_sym_t* UInt16  = jl_symbol("UInt16");
    jl_sym_t* UInt32  = jl_symbol("UInt32");
    jl_sym_t* UInt64  = jl_symbol("UInt64");
    jl_sym_t* UInt128 = jl_symbol("UInt128");
    jl_sym_t* Float16 = jl_symbol("Float16");
    jl_sym_t* Float32 = jl_symbol("Float32");
    jl_sym_t* Float64 = jl_symbol("Float64");
    jl_sym_t* String  = jl_symbol("String");
    jl_sym_t* Symbol  = jl_symbol("Symbol");
    jl_sym_t* Nothing = jl_symbol("Nothing");
    jl_sym_t* Vector  = jl_symbol("Vector");

    // misc
    jl_sym_t* none = jl_symbol("none"); // for LineNumberNode-s without associated files
};

} // namespace stc::jl::rt
