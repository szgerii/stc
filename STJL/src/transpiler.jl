# ! TODO: REMOVE
const LIB_PATH = joinpath(@__DIR__, "..", "..", "build", "gcc-rel", "lib", "libstc.dll")

# Low-level API
export unsafe_transpile, unsafe_transpile_file, unsafe_get_transpiler_result, unsafe_free_transpiler_handle
# High-level API
export transpile, transpile_file

# Low-level API for interacting with the STC lib

"""
    unsafe_transpile(ast::Expr, run_benchmarks::Bool=false)::Ptr{Cvoid}

Low-level endpoint for invoking the transpilation pipeline through the underlying STC library.

Returns a transpiler handle on success that can be used to retrieve the string output with `unsafe_get_transpiler_result`, or `C_NULL` on transpilation failure.
The caller is responsible for calling `unsafe_free_transpiler_handle` on the returned handle, if it's non-null.
"""
function unsafe_transpile(ast::Expr, run_benchmarks::Bool=false)::Ptr{Cvoid}
    GC.@preserve ast begin
        handle = ccall((:stc_jl_transpile, LIB_PATH), Ptr{Cvoid}, (Any, Bool), ast, run_benchmarks)
    end

    return handle
end

"""
    unsafe_transpile_file(path::String, run_benchmarks::Bool=false)

Low-level endpoint for calling `unsafe_transpile` with the contents of a file.

Returns `C_NULL` if the file at the given `path` could not be read, or if it contained syntactically malformed code.
Forwards the output of `unsafe_transpile` otherwise, see its docs for details and caller responsibilities.
"""
function unsafe_transpile_file(path::String, run_benchmark::Bool=false)::Ptr{Cvoid}
    if !isfile(path)
        println(stderr, "file not found at '$path'")
        return C_NULL
    end

    file_content = try
        read(path, String)
    catch ex
        println(stderr, "failed to read file at '$path':\n$ex")
        return C_NULL
    end

    file_expr = try
        expr = Meta.parse(file_content)

        if Meta.isexpr(expr, :error) || Meta.isexpr(expr, :incomplete)
            println(stderr, "Julia syntax error in '$path':")
            dump(stderr, expr)
            return C_NULL
        end

        expr
    catch ex
        println(stderr, "unexpected error while parsing '$path'\n$ex")
        return C_NULL
    end

    return unsafe_transpile(file_expr, run_benchmark)
end

"""
    unsafe_get_transpile_result(handle::Ptr{Cvoid})::Ptr{Cchar}

Low-level endpoint for obtaining a pointer to the C friendly representation of the generated code.

For valid invocations, this is guaranteed to be a null-terminated, contigious Cchar array. If `handle` is null, `C_NULL` is returned.
Invocations with invalid handles (i.e. non-transpiler-returned ones) are considered UB.
"""
function unsafe_get_transpiler_result(handle::Ptr{Cvoid})::Ptr{Cchar}
    handle == C_NULL && return C_NULL
    return ccall((:stc_jl_cstr_from_handle, LIB_PATH), Ptr{Cchar}, (Ptr{Cvoid},), handle)
end

"""
    unsafe_free_transpiler_handle(handle::Ptr{Cvoid})

Low-level endpoint for freeing the memory storing the generated code of a transpilation.

If `handle` is null, this is a no-op.
"""
function unsafe_free_transpiler_handle(handle::Ptr{Cvoid})
    handle != C_NULL && ccall((:stc_jl_free_handle, LIB_PATH), Cvoid, (Ptr{Cvoid},), handle)
end

# High-level API for transpilation

"""
Private binding for safely unwrapping the string result of a transpilation.
    
Frees the handle's underlying data on every non-crashing path.
"""
function safe_unwrap_transpiler_result(handle::Ptr{Cvoid}, throw_on_err::Bool)::String
    function handle_error(msg::String)
        handle != C_NULL && unsafe_free_transpiler_handle(handle)

        throw_on_err && error("an error occured during transpilation, and throw_on_err has been enabled:\n$msg")

        println(stderr, msg)
        return ""
    end

    handle == C_NULL && return handle_error("transpiler ran into an error while processing the given input")

    result_cchar_ptr = unsafe_get_transpiler_result(handle)
    result_cchar_ptr == C_NULL && return handle_error("couldn't read result data after a successful transpilation")

    try
        return unsafe_string(result_cchar_ptr)
    finally
        unsafe_free_transpiler_handle(handle)
    end
end

"""
    transpile(ast::Expr, run_benchmarks::Bool=false, throw_on_err::Bool=false)::String

Memory-safe endpoint for invoking the transpilation pipeline through the underlying STC library, and unwrapping its result.

Returns the generated code copied into a Julia `String` on success, or the empty string on failure, if `throw_on_err` is disabled.
If `throw_on_err` is set to `true`, any errors during transpilation (user and memory-related) will throw a runtime error, after freeing any potentially allocated memory.

If `run_benchmarks` is enabled, STC will print a breakdown for how long each transpilation pass took to execute.
"""
function transpile(ast::Expr, run_benchmarks::Bool=false, throw_on_err::Bool=false)::String
    handle = unsafe_transpile(ast, run_benchmarks)
    return safe_unwrap_transpiler_result(handle, throw_on_err)
end

"""
    transpile_file(path::String, run_benchmarks::Bool=false, throw_on_err::Bool=false)::String

Memory-safe endpoint for invoking the transpilation pipeline on the contents of a file through the underlying STC library, and unwrapping its result.

For the arguments and return type descriptions see the docs of `transpile` and `unsafe_transpile_file`.
"""
function transpile_file(path::String, run_benchmark::Bool=false, throw_on_err::Bool=false)::String
    handle = unsafe_transpile_file(path, run_benchmark)
    return safe_unwrap_transpiler_result(handle, throw_on_err)
end
