#include <backend/glsl/code_gen.h>
#include <common/literals.h>
#include <frontend/jl/dumper.h>
#include <frontend/jl/lowering.h>
#include <iostream>
#include <sir/dumper.h>
#include <sir/sema.h>

// NOTE: main() can throw right now
int main() { // NOLINT
    using namespace stc;
    using namespace jl;

    JLCtx ctx{};
    SrcLocationId loc{0};

    // NOLINTBEGIN
    auto lit1 = ctx.emplace_node<Int32Literal>(loc, 10).first;
    auto lit2 = ctx.emplace_node<Int64Literal>(loc, 12).first;
    auto lit3 = ctx.emplace_node<UInt8Literal>(loc, 23_u8).first;
    auto lit4 = ctx.emplace_node<UInt64Literal>(loc, 23123U).first;
    // NOLINTEND

    auto true_lit  = ctx.emplace_node<BoolLiteral>(loc, true).first;
    auto false_lit = ctx.emplace_node<BoolLiteral>(loc, false).first;

    SymbolId sym_add = ctx.sym_pool.get_id("+");
    SymbolId sym_mul = ctx.sym_pool.get_id("*");
    SymbolId sym_sin = ctx.sym_pool.get_id("sin");

    auto lit_add  = ctx.emplace_node<SymbolLiteral>(loc, sym_add).first;
    auto lit_mul  = ctx.emplace_node<SymbolLiteral>(loc, sym_mul).first;
    auto lit_mul2 = ctx.emplace_node<SymbolLiteral>(loc, sym_mul).first;
    auto lit_sin  = ctx.emplace_node<SymbolLiteral>(loc, sym_sin).first;

    auto binop1 =
        ctx.emplace_node<FunctionCall>(loc, lit_add, std::vector<NodeId>{lit1, lit2}).first;
    auto sin_res = ctx.emplace_node<FunctionCall>(loc, lit_sin, std::vector<NodeId>{binop1}).first;
    auto binop2 =
        ctx.emplace_node<FunctionCall>(loc, lit_mul, std::vector<NodeId>{lit3, lit4}).first;
    auto binop3 =
        ctx.emplace_node<FunctionCall>(loc, lit_mul2, std::vector<NodeId>{sin_res, binop2}).first;

    auto ret = ctx.emplace_node<ReturnStmt>(loc, false_lit).first;

    auto ret_cmpd   = ctx.emplace_node<CompoundExpr>(loc, std::vector<NodeId>{ret}).first;
    auto empty_cmpd = ctx.emplace_node<CompoundExpr>(loc, std::vector<NodeId>{}).first;

    auto if_stmt = ctx.emplace_node<IfExpr>(loc, true_lit, ret_cmpd, empty_cmpd).first;

    auto cmpd = ctx.emplace_node<CompoundExpr>(loc, std::vector<NodeId>{if_stmt, binop3}).first;
    ctx.get_node(cmpd)->type = TypePool::void_td();

    std::cout << "<== Original Julia AST ==>\n\n";

    JLDumper jl_dumper{ctx, std::cout};
    jl_dumper.visit(cmpd);

    JLLoweringVisitor lowering_vis{std::move(ctx)};
    sir::NodeId base_id = lowering_vis.visit(cmpd);

    if (!lowering_vis.successful()) {
        std::cerr << "Julia -> SIR lowering failed\n";
        return 1;
    }

    auto glsl_ctx = glsl::GLSLCtx(std::move(lowering_vis.sir_ctx));

    std::cout << "\n<== Lowered Shader IR ==>\n\n";

    sir::SIRDumper sir_dumper{glsl_ctx, std::cout};
    sir_dumper.visit(base_id);

    std::cout << "\n<== SIR Semantic Verification ==>\n\n";
    sir::SIRSemaVisitor sema_vis{glsl_ctx};

    sema_vis.visit(base_id);

    if (!sema_vis.success()) {
        std::cerr << "SIR semantic verification failed\n";
    }

    glsl::GLSLCodeGenVisitor code_gen_vis{glsl_ctx};
    code_gen_vis.visit(base_id);

    if (!code_gen_vis.successful()) {
        std::cerr << "GLSL code gen failed\n";
        return 1;
    }

    std::cout << "\n<== Generated GLSL code ==>\n\n";
    std::cout << code_gen_vis.result();

    std::cout.flush();

    return 0;
}
