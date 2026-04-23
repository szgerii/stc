export @gl_layout

"""
    @gl_layout(args..., expr)

Marks `expr` with layout qualifiers during transpilation.
On the Julia-side, this is a no-op;

All layout options are handled by this macro (valued or valueless).
"""
macro gl_layout(args...)
    expr = args[end]
    return esc(expr)
end

const NON_LAYOUT_QUALS = [
    # Storage / Param
    :const, :in, :out, :inout, :attribute, :uniform, :varying, :buffer, :shared,
    # AuxStorage
    :centroid, :sample, :patch,
    # Interpolation
    :smooth, :flat, :noperspective,
    # Precision
    :lowp, :mediump, :highp,
    # Invariance & Misc
    :invariant, :precise, :subroutine,
    # Memory
    :coherent, :volatile, :restrict, :readonly, :writeonly
]

for qual in NON_LAYOUT_QUALS
    macro_name = Symbol("gl_", qual)
    export_name = Symbol("@", macro_name)

    # Construct a highly generic, standardized docstring
    doc_str = """
        $export_name(expr)

    Applies the `$qual` type qualifier to the given declaration during transpilation.
    On the Julia-side, this is a no-op.
    """

    @eval begin
        export $export_name

        Core.@doc $doc_str
        macro $macro_name(expr)
            return esc(expr)
        end
    end
end
