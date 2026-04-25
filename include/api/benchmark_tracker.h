#pragma once

#include <chrono>
#include <ostream>

namespace stc::api::detail {

inline std::string format_bm_duration(std::chrono::nanoseconds dur) {
    using namespace std::chrono;

    duration<double, std::milli> exact_ms = dur;
    duration<double, std::micro> exact_us = dur;

    return duration_cast<milliseconds>(dur).count() > 0
               ? std::format("{:.3f} ms", exact_ms.count())
               : std::format("{:.0f} us", exact_us.count());
}

template <bool Enabled>
struct BenchmarkTracker {
    void start() {}
    void init_start() {}
    void init_end() {}
    void parser_start() {}
    void parser_end() {}
    void sema_start() {}
    void sema_end() {}
    void lowering_start() {}
    void lowering_end() {}
    void code_gen_start() {}
    void code_gen_end() {}
    void end() {}

    void print([[maybe_unused]] std::ostream& out) const {}
};

template <>
struct BenchmarkTracker<true> {
    using clock = std::chrono::steady_clock;

    std::chrono::time_point<clock> tp_start, tp_init_start, tp_init_end, tp_parser_start,
        tp_parser_end, tp_sema_start, tp_sema_end, tp_lowering_start, tp_lowering_end,
        tp_code_gen_start, tp_code_gen_end, tp_end;

    void start() { tp_start = clock::now(); }
    void init_start() { tp_init_start = clock::now(); }
    void init_end() { tp_init_end = clock::now(); }
    void parser_start() { tp_parser_start = clock::now(); }
    void parser_end() { tp_parser_end = clock::now(); }
    void sema_start() { tp_sema_start = clock::now(); }
    void sema_end() { tp_sema_end = clock::now(); }
    void lowering_start() { tp_lowering_start = clock::now(); }
    void lowering_end() { tp_lowering_end = clock::now(); }
    void code_gen_start() { tp_code_gen_start = clock::now(); }
    void code_gen_end() { tp_code_gen_end = clock::now(); }
    void end() { tp_end = clock::now(); }

    void print(std::ostream& out) const {
        out << "\nCtx init finished in " << format_bm_duration(tp_init_end - tp_init_start) << '\n';
        out << "Parser finished in " << format_bm_duration(tp_parser_end - tp_parser_start) << '\n';
        out << "Sema finished in " << format_bm_duration(tp_sema_end - tp_sema_start) << '\n';
        out << "Lowering finished in " << format_bm_duration(tp_lowering_end - tp_lowering_start)
            << '\n';
        out << "Codegen finished in " << format_bm_duration(tp_code_gen_end - tp_code_gen_start)
            << '\n';
        out << "\nEntire transpilation pipeline finished in "
            << format_bm_duration(tp_end - tp_start) << "\n\n";

        out.flush();
    }
};

static_assert(sizeof(BenchmarkTracker<false>) == 1U);

} // namespace stc::api::detail
