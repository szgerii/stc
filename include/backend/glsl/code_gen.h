#pragma once

#include <sstream>

#include "backend/glsl/glsl_context.h"
#include "sir/visitor.h"

namespace stc::glsl {

using namespace stc::sir;

class GLSLCodeGenVisitor final : public SIRVisitor<GLSLCodeGenVisitor, const GLSLCtx, void> {
public:
    GLSLCodeGenVisitor(const GLSLCtx& ctx)
        : SIRVisitor{ctx} {}

    std::string result() const { return out.str(); }
    bool success() const { return successful_gen; }

    // clang-format off
    #define X(type, kind) STC_AST_VISITOR_DECL(void, type)
        #include "sir/node_defs/all_nodes.def"
    #undef X
    // clang-format on

private:
    std::stringstream out{};
    size_t indent_level = 0U;
    bool successful_gen = true;

    std::string indent() const;
};

} // namespace stc::glsl
