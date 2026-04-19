#pragma once

#include <iostream>

#ifdef _WIN32
    #include <io.h>
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace ansi_codes {

inline constexpr std::string black  = "\033[30m";
inline constexpr std::string red    = "\033[31m";
inline constexpr std::string green  = "\033[32m";
inline constexpr std::string yellow = "\033[33m";
inline constexpr std::string blue   = "\033[34m";
inline constexpr std::string purple = "\033[35m";
inline constexpr std::string cyan   = "\033[36m";
inline constexpr std::string white  = "\033[37m";
inline constexpr std::string reset  = "\033[0m";

}; // namespace ansi_codes

namespace stc {

class TerminalInfo {
public:
    static bool supports_color();
};

inline std::string colored(std::string text, std::string_view color) {
    if (!TerminalInfo::supports_color())
        return text;

    return std::string{color} + text + ansi_codes::reset;
}

} // namespace stc
