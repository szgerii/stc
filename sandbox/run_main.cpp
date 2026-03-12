#include <backend/glsl/code_gen.h>
#include <iostream>
#include <sir/dumper.h>

// NOTE: main() can  throw right now
int main() { // NOLINT
    using namespace stc;
    using namespace stc::sir;
    using namespace stc::glsl;

    GLSLCtx ctx{};
    SIRDumper dumper{ctx, std::cout};
    SrcLocationId loc{0};

    auto i32 = ctx.type_pool.int_td(32, true);
    auto u32 = ctx.type_pool.int_td(32, false);
    auto i96 = ctx.type_pool.int_td(96, true); // NOLINT

    auto lit1 = ctx.emplace_node<IntLiteral>(loc, i32, "-10").first;
    auto lit2 = ctx.emplace_node<IntLiteral>(loc, u32, "12U").first;
    auto lit3 = ctx.emplace_node<IntLiteral>(loc, i96, "3427452").first;

    auto true_lit  = ctx.emplace_node<BoolLiteral>(loc, true).first;
    auto false_lit = ctx.emplace_node<BoolLiteral>(loc, false).first;

    auto binop1 = ctx.emplace_node<BinaryOp>(loc, BinaryOp::OpKind::mod, lit1, lit2).first;
    auto binop2 = ctx.emplace_node<BinaryOp>(loc, BinaryOp::OpKind::mul, binop1, lit3).first;

    auto ret = ctx.emplace_node<ReturnStmt>(loc, false_lit).first;

    auto ret_cmpd   = ctx.emplace_node<CompoundStmt>(loc, std::vector<NodeId>{ret}).first;
    auto empty_cmpd = ctx.emplace_node<CompoundStmt>(loc, std::vector<NodeId>{}).first;

    auto if_stmt = ctx.emplace_node<IfStmt>(loc, true_lit, ret_cmpd, empty_cmpd).first;

    auto cmpd = ctx.emplace_node<CompoundStmt>(loc, std::vector<NodeId>{if_stmt, binop2}).first;

    dumper.visit(NodeId{cmpd});

    auto vdecl_no_init   = ctx.emplace_node<VarDecl>(loc, "my_var1", i32).first;
    auto vdecl_with_init = ctx.emplace_node<VarDecl>(loc, "my_var2", u32, binop1).first;

    dumper.visit(vdecl_no_init);
    dumper.visit(vdecl_with_init);

    GLSLCodeGenVisitor codegen{ctx};

    codegen.visit(NodeId{cmpd});

    std::cout << codegen.result();

    std::cout << "code gen " << (codegen.success() ? "was successful" : "failed") << '\n';

    return 0;
}
