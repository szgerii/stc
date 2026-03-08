#include <iostream>
#include <ir/ast_dumper.h>

// NOTE: main() can  throw right now
int main() { // NOLINT
    using namespace stc;
    using namespace stc::ir;

    ASTCtx ctx{};
    ASTDumper dumper{ctx, std::cout};
    SrcLocationId loc{0};

    auto i32 = ctx.type_pool.int_td(32, true);
    auto u32 = ctx.type_pool.int_td(32, false);
    auto i96 = ctx.type_pool.int_td(96, true); // NOLINT

    auto lit1 = ctx.alloc_node<IntLiteral>(loc, i32, "-10").first;
    auto lit2 = ctx.alloc_node<IntLiteral>(loc, u32, "12U").first;
    auto lit3 = ctx.alloc_node<IntLiteral>(loc, i96, "3427452").first;

    auto true_lit  = ctx.alloc_node<BoolLiteral>(loc, true).first;
    auto false_lit = ctx.alloc_node<BoolLiteral>(loc, false).first;

    auto binop1 = ctx.alloc_node<BinaryOp>(loc, i32, BinaryOp::OpKind::mod, lit1, lit2).first;
    auto binop2 = ctx.alloc_node<BinaryOp>(loc, i96, BinaryOp::OpKind::mul, binop1, lit3).first;

    auto ret = ctx.alloc_node<ReturnStmt>(loc, false_lit).first;

    auto empty_cmpd = ctx.alloc_node<CompoundStmt>(loc, std::vector<NodeId>{}).first;

    auto if_stmt = ctx.alloc_node<IfStmt>(loc, true_lit, ret, empty_cmpd).first;

    auto cmpd = ctx.alloc_node<CompoundStmt>(loc, std::vector<NodeId>{if_stmt, binop2}).first;

    dumper.visit(NodeId{cmpd});

    auto vdecl_no_init   = ctx.alloc_node<VarDecl>(loc, "my_var1", i32).first;
    auto vdecl_with_init = ctx.alloc_node<VarDecl>(loc, "my_var2", u32, binop1).first;

    dumper.visit(vdecl_no_init);
    dumper.visit(vdecl_with_init);

    return 0;
}
