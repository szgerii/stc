#pragma once

#include "ast/context.h"
#include <type_traits>

namespace stc {

#define STC_AST_VISITOR_DECL(RetTy, type) RetTy visit_##type(type&);

// CRTP base AST visitor class

// derived visitors need to implement a visit_T function for every Stmt and Decl in the AST
// sir/node_defs/all_nodes.def can be used with the X-macro system to automatically generate
// declarations for these, as seen below

// CtxTy MUST be a derived class of an ASTCtx instantiation
// since ASTCtx is a class template, std::derived_from cannot be used in requires, so this only
// checks for requirements directly used by the ASTVisitor base class

template <typename ImplTy, typename CtxTy, typename RetTy = void>
requires requires (CtxTy ctx, CtxTy::node_id_type id) {
    typename CtxTy::node_id_type;
    typename CtxTy::node_base_type;
    typename CtxTy::node_kind_type;
    { ctx.get_node(id) } -> std::convertible_to<typename CtxTy::node_base_type*>;
}
class ASTVisitor {
    using NodeIdTy   = CtxTy::node_id_type;
    using NodeBaseTy = CtxTy::node_base_type;

public:
    CtxTy& ctx;

    explicit ASTVisitor(CtxTy& ast_context)
        : ctx{ast_context} {}

    ImplTy* impl_this() { return static_cast<ImplTy*>(this); }
    const ImplTy* impl_this() const { return static_cast<const ImplTy*>(this); }

    template <typename T, typename U>
    static T& as(U* ptr) {
        return *static_cast<T*>(ptr);
    }

public:
    RetTy visit(NodeBaseTy* node, bool call_pre_visit = true) {
        if (call_pre_visit) {
            bool pre_result = impl_this()->pre_visit_ptr(node);

            if (!pre_result)
                return impl_this()->visit_default_case();
        }

        return impl_this()->dispatch_wrapper(node);
    }

    RetTy visit(NodeIdTy id, bool call_pre_visit = true) {
        if (call_pre_visit) {
            bool pre_result = impl_this()->pre_visit_id(id);

            if (!pre_result)
                return impl_this()->visit_default_case();
        }

        return impl_this()->visit(ctx.get_node(id), false);
    }

protected:
    RetTy dispatch_wrapper(NodeBaseTy* node) {
        if (node == nullptr)
            return impl_this()->visit_default_case();

        return impl_this()->dispatch(node);
    }

    // ImplTy MUST implement this
    RetTy dispatch([[maybe_unused]] NodeBaseTy* node) {
        // CLEANUP: static error

        throw std::logic_error{
            "Unimplemented dispatch(NodeBaseTy*) function member in derived class of ASTVisitor"};
    }

    // if RetTy isn't void, ImplTy MUST implement this
    RetTy visit_default_case() {
        if constexpr (!std::is_void_v<RetTy>) {
            static_assert(
                dependent_false_v<RetTy>,
                "Non-void returning ASTVisitors must define fallback logic in visit_default_case");
        }
    }

    // ImplTy CAN implement these
    bool pre_visit_id([[maybe_unused]] NodeIdTy id) { return true; }
    bool pre_visit_ptr([[maybe_unused]] NodeBaseTy* node) { return true; }
};

} // namespace stc
