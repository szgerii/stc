@gl_uniform global r::Float32
@gl_uniform global theta::Float32

function main()
    function comp_mul(x::Vec2, y::Vec2)
        re_ = x[:x] * y[:x] - x[:y] * y[:y]
        im_ = x[:y] * y[:x] + x[:x] * y[:y]

        return vec2(re_, im_)
    end

    function comp_div(x::Vec2, y::Vec2)
        denom = y[:x] * y[:x] + y[:y] * y[:y]

        re_ = (x[:x] * y[:x] + x[:y] * y[:y]) / denom
        im_ = (x[:y] * y[:x] - x[:x] * y[:y]) / denom

        return vec2(re_, im_)
    end

    w = vec2(r * cos(theta), r * sin(theta))
    w2 = comp_mul(w, w)
    w3 = comp_mul(w2, w)
    w4 = comp_mul(w2, w2)
    w6 = comp_mul(w3, w3)

    g_denom = w6 + comp_mul(vec2(sqrt(5), 0), w3) - vec2(1, 0)

    g1_base = comp_div(comp_mul(w, vec2(1, 0) - w4), g_denom)
    g1 = -1.5 * g1_base[:y]

    g2_base = comp_div(comp_mul(w, vec2(1, 0) + w4), g_denom)
    g2 = -1.5 * g2_base[:x]

    g3_base = comp_div(vec2(1, 0) + w6, g_denom)
    g3 = g3_base[:y] - 0.5

    pos_denom = g1 * g1 + g2 * g2 + g3 * g3
    pos = vec3(g1, g2, g3) / pos_denom
end
