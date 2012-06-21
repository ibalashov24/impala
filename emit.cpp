#include "impala/ast.h"

#include "anydsl/cfg.h"
#include "anydsl/literal.h"
#include "anydsl/type.h"
#include "anydsl/var.h"
#include "anydsl/world.h"
#include "anydsl/util/for_all.h"

#include "impala/type.h"

using anydsl::BB;
using anydsl::Def;
using anydsl::Var;
using anydsl::make_name;
using anydsl::World;

namespace impala {

class CodeGen {
public:

    BB* curBB;
    anydsl::Fct* curFct;

    anydsl::World& world;
};

//------------------------------------------------------------------------------

void Prg::emit(CodeGen& cg) const {
    for_all (f, fcts())
        f->emit(cg);
}

void Fct::emit(CodeGen& cg) const {
}

void Decl::emit(CodeGen& cg) const {
}

/*
 * Expr
 */

Value EmptyExpr::emit(CodeGen& cg) const {
    return Value((const Def*) 0);
}

Value Literal::emit(CodeGen& cg) const {
    return Value(cg.world.literal((anydsl::PrimLitKind) kind(), value()));
}

Value Id::emit(CodeGen& cg) const {
    return Value(cg.curBB->getVar(symbol()));
}


Value PrefixExpr::emit(CodeGen& cg) const {
    Value val = right()->emit(cg);

    const Def* def = val.load();
    const anydsl::PrimType* p = def->type()->as<anydsl::PrimType>();

    if (kind() == ADD)
        return def; // this is a NOP

    switch (kind()) {
        case SUB: {
            // TODO incorrect for f32, f64
            const anydsl::PrimLit* zero = cg.world.literal(p->kind(), 0u);
            return cg.world.createArithOp(anydsl::ArithOp_sub, zero, def);
        }
        case INC:
        case DEC: {
            const anydsl::PrimLit* one = cg.world.literal(p->kind(), 1u);
            const Def* ndef = cg.world.createArithOp(Token::toArithOp((TokenKind) kind()), def, one);
            val.store(ndef);

            return val;
        }
        default: ANYDSL_UNREACHABLE; // TODO
    }
}

static Value emitInfix(anydsl::World& world, TokenKind op, Value aval, Value bval) {
    if (Token::isAsgn(op)) {
        if (op != Token::ASGN) {
            TokenKind sop = Token::seperateAssign(op);
            bval = emitInfix(world, sop, aval, bval);
        }

        aval.store(bval.load());

        return aval;

        if (Token::isArith(op))
            return world.createArithOp(Token::toArithOp(op), aval.load(), bval.load());
    }

    anydsl_assert(Token::isRel(op), "must be a relop");
    return world.createRelOp(Token::toRelOp(op), aval.load(), bval.load());
}

Value InfixExpr::emit(CodeGen& cg) const {
    Value aval = left()->emit(cg);
    Value bval = right()->emit(cg);

    return emitInfix(cg.world, (TokenKind) kind(), aval, bval);
}

Value PostfixExpr::emit(CodeGen& cg) const {
    return Value((const Def*) 0);
}

/*
 * Stmt
 */

void DeclStmt::emit(CodeGen& cg) const {
    decl()->emit(cg);
}

void ExprStmt::emit(CodeGen& cg) const {
    expr()->emit(cg);
}

void IfElseStmt::emit(CodeGen& cg) const {
    static int id = 0;

    // always create elseBB -- the edge from headBB to nextBB would be crtical anyway

    // create BBs
    BB* headBB = cg.curFct->createBB(make_name("if-head", id));
    BB* thenBB = cg.curFct->createBB(make_name("if-then", id));
    BB* elseBB = cg.curFct->createBB(make_name("if-else", id));
    BB* nextBB = cg.curFct->createBB(make_name("if-next", id));

    // head
    cg.curBB->fixto(headBB);
    cg.curBB = headBB;

    // condition
    const Def* cvar = cond()->emit(cg).load();
    headBB->branches(cvar, thenBB, elseBB);
    headBB->seal();
    thenBB->seal();
    elseBB->seal();

    // then
    cg.curBB = thenBB;
    thenStmt()->emit(cg);
    cg.curBB->fixto(nextBB);

    // else
    cg.curBB = elseBB;
    elseStmt()->emit(cg);
    cg.curBB->fixto(nextBB);

    // next
    cg.curBB = nextBB;
    nextBB->seal();

    ++id;
}

void WhileStmt::emit(CodeGen& cg) const {
    static int id = 0;

    // create BBs
    BB* headBB = cg.curFct->createBB(make_name("while-head", id));
    BB* bodyBB = cg.curFct->createBB(make_name("while-body", id));
    BB* nextBB = cg.curFct->createBB(make_name("while-next", id));

    // head
    cg.curBB->fixto(headBB);
    cg.curBB = headBB;

    // condition
    const Def* cvar = cond()->emit(cg).load();
    headBB->branches(cvar, bodyBB, nextBB);
    bodyBB->seal();

    // body
    cg.curBB = bodyBB;
    body()->emit(cg);
    cg.curBB->fixto(headBB);
    headBB->seal();

    // next
    cg.curBB = nextBB;
    nextBB->seal();

    ++id;
}

void DoWhileStmt::emit(CodeGen& cg) const {
    body()->emit(cg);
    cond()->emit(cg);
}

void ForStmt::emit(CodeGen& cg) const {
    init()->emit(cg);
    cond()->emit(cg);
    inc()->emit(cg);
    body()->emit(cg);
}

void BreakStmt::emit(CodeGen& cg) const {
}

void ContinueStmt::emit(CodeGen& cg) const {
}

void ReturnStmt::emit(CodeGen& cg) const {
    expr()->emit(cg);
}

void ScopeStmt::emit(CodeGen& cg) const {
    for_all (const &s, stmts())
        s->emit(cg);
}

//------------------------------------------------------------------------------

/*
 * Type
 */

const anydsl::Type* PrimType::emit(anydsl::World& world) const {
    switch (kind()) {
#define IMPALA_TYPE(itype, atype) \
        case TYPE_##itype: \
            return world.type_##atype();
#include "impala/tokenlist.h"
        default:
            ANYDSL_UNREACHABLE;
    }
}

const anydsl::Type* Void::emit(anydsl::World& world) const {
    return world.unit();
}

const anydsl::Type* TypeError::emit(anydsl::World& /*world*/) const {
    ANYDSL_UNREACHABLE;
}

} // namespace impala
