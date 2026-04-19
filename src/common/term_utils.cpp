#include "common/term_utils.h"

namespace stc {

bool TerminalInfo::supports_color() {
    static bool inited    = false;
    static bool supported = false;

    if (inited)
        return supported;
    inited = true;

    if (std::getenv("NO_COLOR"))
        return false;

#ifdef _WIN32
    if (!_isatty(_fileno(stdout)) || !_isatty(_fileno(stderr)))
        return false;

    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE),
           stderr_handle = GetStdHandle(STD_ERROR_HANDLE);

    if (stdout_handle == INVALID_HANDLE_VALUE || stderr_handle == INVALID_HANDLE_VALUE)
        return false;

    DWORD stdout_mode = 0, stderr_mode = 0;
    if (!GetConsoleMode(stdout_handle, &stdout_mode) ||
        !GetConsoleMode(stderr_handle, &stderr_mode))
        return false;

    stdout_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
    stderr_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;

    if (!SetConsoleMode(stdout_handle, stdout_mode) || !SetConsoleMode(stderr_handle, stderr_mode))
        return false;

    supported = true;
#else
    supported = isatty(STDOUT_FILENO) != 0;
#endif

    return supported;
};

} // namespace stc
