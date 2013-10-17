#include <fstream>
#include <vector>
#include <cctype>

#include "anydsl2/analyses/looptree.h"
#include "anydsl2/analyses/scope.h"
#include "anydsl2/analyses/verify.h"
#include "anydsl2/transform/vectorize.h"
#include "anydsl2/transform/partial_evaluation.h"
#include "anydsl2/be/air.h"
#include "anydsl2/be/il.h"
#include "anydsl2/be/llvm.h"

#include "impala/ast.h"
#include "impala/parser.h"
#include "impala/sema.h"
#include "impala/dump.h"
#include "impala/emit.h"
#include "impala/init.h"

#include "args.h"

//------------------------------------------------------------------------------

using namespace anydsl2;
using namespace std;

typedef vector<string> Names;

//------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        if (argc < 1)
            throw logic_error("bad number of arguments");

        string prgname = argv[0];
        Names infiles;
#ifndef NDEBUG
        Names breakpoints;
#endif
        string outfile;
        bool help, emit_all, emit_air, emit_il, emit_ast, emit_llvm, emit_looptree, fancy, opt, verify, nocleanup, nossa = false;
        int vectorlength = 0;
        auto cmd_parser = ArgParser("Usage: " + prgname + " [options] file...")
            .implicit_option("infiles", "input files", infiles)
            // specify options
            .add_option<bool>("help", "produce this help message", help, false)
            .add_option<string>("o", "specifies the output file", outfile, "-")
#ifndef NDEBUG
            .add_option<vector<string>>("break", "breakpoint at definition generation of number arg", breakpoints)
#endif
            .add_option<bool>("nocleanup", "no clean-up phase", nocleanup, false)
            .add_option<bool>("nossa", "use slots + load/store instead of SSA construction", nossa, false)
            .add_option<bool>("verify", "run verifier", verify, false)
            .add_option<int>("vectorize", "run vectorizer on main with given vector length (experimantal!!!), arg=<vector length>", vectorlength, false)
            .add_option<bool>("emit-air", "emit textual AIR representation of impala program", emit_air, false)
            .add_option<bool>("emit-il", "emit textual IL representation of impala program", emit_il, false)
            .add_option<bool>("emit-all", "emit AST, AIR, LLVM and loop tree", emit_all, false)
            .add_option<bool>("emit-ast", "emit AST of impala program", emit_ast, false)
            .add_option<bool>("emit-looptree", "emit loop tree", emit_looptree, false)
            .add_option<bool>("emit-llvm", "emit llvm from AIR representation (implies -O)", emit_llvm, false)
            .add_option<bool>("f", "use fancy output", fancy, false)
            .add_option<bool>("O", "optimize", opt, false);

        // do cmdline parsing
        cmd_parser.parse(argc, argv);

        if (emit_all)
            emit_air = emit_looptree = emit_ast = emit_llvm = true;
        opt |= emit_llvm;

        if (infiles.empty() && !help)
            throw exception("no input files");

        if (help) {
            cmd_parser.print_help();
            return EXIT_SUCCESS;
        }

        ofstream ofs;
        if (outfile != "-") {
            ofs.open(outfile.c_str());
            ofs.exceptions(istream::badbit);
        }
        ostream& out = ofs.is_open() ? ofs : cout;

        impala::Init init;

//#ifndef NDEBUG
//        for (auto b : breakpoints) {
//            assert(b.size() > 0);
//            size_t num = 0;
//            for (size_t i = 0, e = b.size(); i != e; ++i) {
//                char c = b[i];
//                if (!std::isdigit(c))
//                    throw exception("invalid breakpoint '" + b + "'");
//                num = num*10 + c - '0';
//            }
//
//            init.world.breakpoint(num);
//        }
//#endif

        anydsl2::AutoPtr<impala::Scope> prg = new impala::Scope();
        prg->set_loc(anydsl2::Location(infiles[0], 1, 1, 1, 1));

        bool result = true;
        for (auto infile : infiles) {
            const char* filename = infile.c_str();
            ifstream file(filename);
            result &= impala::parse(init.typetable, file, filename, prg);
        }

        if (emit_ast)
            dump_prg(prg, fancy);

        result &= check(init.typetable, prg, nossa);
        result &= result ? emit(init.world, prg) : false;

        if (result) {
            if (!nocleanup)
                init.world.cleanup();
            if (verify)
                anydsl2::verify(init.world);
            if (opt)
                init.world.opt();
            if (vectorlength != 0) {
                Lambda* impala_main = top_level_lambdas(init.world)[0];
                Scope scope(impala_main);
                anydsl2::vectorize(scope, vectorlength);
            }
            if (emit_air)
                anydsl2::emit_air(init.world, fancy);
            if (emit_il)
                anydsl2::emit_il(init.world, fancy);
            if (emit_looptree)
                std::cout << Scope(init.world).looptree().root() << std::endl; // TODO

            if (emit_llvm)
                anydsl2::emit_llvm(init.world);
        }

        return EXIT_SUCCESS;
    } catch (exception const& e) {
        cerr << e.what() << endl;
        return EXIT_FAILURE;
    } catch (...) {
        cerr << "unknown exception" << endl;
        return EXIT_FAILURE;
    }
}