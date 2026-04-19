#include <iostream>

#include "common/term_utils.h"
#include "common/utils.h"

namespace stc {

void report(std::string_view msg, std::string_view prefix, std::ostream& out) {
    out << prefix << msg << '\n';
}

void error(std::string_view msg, std::ostream& out) {
    report(msg, colored("[Error] ", ansi_codes::red), out);
}

void warning(std::string_view msg, std::ostream& out) {
    report(msg, colored("[Warning] ", ansi_codes::yellow), out);
}

void internal_error(std::string_view msg, std::ostream& out) {
    report(msg, colored("[Internal Error] ", ansi_codes::red), out);
}

} // namespace stc