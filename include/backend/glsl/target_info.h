#pragma once

#include "backend/glsl/types.h"
#include "common/target_info.h"

namespace stc::glsl {

class GLSLTargetInfo final : public TargetInfo {
private:
    GLSLTypes gl_types;

public:
    // globals and functions should already be stored in alphabetical order name-wise in the binary
    // so parsing them in sequential order should already yield a sorted result
    // (if not, an assert will catch it in the base class)
    explicit GLSLTargetInfo(GLSLTypes types)
        : GLSLTargetInfo{create_globals(types), create_fns(types), std::move(types)} {}

    explicit GLSLTargetInfo(TypePool& type_pool)
        : GLSLTargetInfo{GLSLTypes{type_pool}} {}

    GLSLTargetInfo(const GLSLTargetInfo&)            = delete;
    GLSLTargetInfo& operator=(const GLSLTargetInfo&) = delete;
    GLSLTargetInfo(GLSLTargetInfo&&)                 = default;
    GLSLTargetInfo& operator=(GLSLTargetInfo&&)      = default;

    bool valid_ctor_call(TypeId target, const TypeList& arg_types) const override;
    bool can_implicit_cast(TypeId src_ty, TypeId dest_ty) const override;

private:
    explicit GLSLTargetInfo(GlobalList builtin_globals, FnList builtin_fns, GLSLTypes types)
        : TargetInfo{std::move(builtin_globals), std::move(builtin_fns)},
          gl_types{std::move(types)} {}

    static GlobalList create_globals(GLSLTypes& types);
    static FnList create_fns(GLSLTypes& types);
};

} // namespace stc::glsl
