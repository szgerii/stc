#include <iostream>

#include "common/utils.h"

namespace stc {

std::nullptr_t report(std::string_view msg, std::string_view prefix, std::ostream& out) {
    out << prefix << msg << '\n';

    return nullptr;
}

std::nullptr_t error(std::string_view msg, std::ostream& out) {
    return report(msg, "error: ", out);
}

std::nullptr_t warning(std::string_view msg, std::ostream& out) {
    return report(msg, "warning: ", out);
}

std::nullptr_t internal_error(std::string_view msg, std::ostream& out) {
    return report(msg, "internal transpiler error: ", out);
}

} // namespace stc