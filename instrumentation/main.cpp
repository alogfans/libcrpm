//
// Created by Feng Ren on 2021/1/20.
//

#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Analysis/MemoryDependenceAnalysis.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

#include "context.h"

using namespace llvm;

namespace {
    class CrpmInstPass : public ModulePass {
    public:
        static char ID;

        CrpmInstPass() : ModulePass(ID) { }

        void getAnalysisUsage(AnalysisUsage &AU) const override {
            AU.setPreservesCFG();
            AU.addRequired<LoopInfoWrapperPass>();
            AU.addRequired<DominatorTreeWrapperPass>();
            AU.addRequired<MemoryDependenceWrapperPass>();
            AU.addRequired<AAResultsWrapperPass>();
            AU.addRequired<ScalarEvolutionWrapperPass>();
            AU.setPreservesAll();
        }

        bool runOnModule(Module &M) override {
            outs() << "Processing " << M.getModuleIdentifier() << "\n";
            MC = new ModuleContext(M, this);
            MC->analysis();
            MC->transform();
            delete MC;
            return true;
        }

    private:
        ModuleContext *MC;
    };
}

char CrpmInstPass::ID = 0;

static RegisterPass<CrpmInstPass> Z("crpm-opt", "CRPM Optimization Pass");
static RegisterStandardPasses W(PassManagerBuilder::EP_OptimizerLast,
                                [](const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
                                    PM.add(new CrpmInstPass());
                                    PM.add(createFunctionInliningPass());
                                    PM.add(createPromoteMemoryToRegisterPass());
                                    PM.add(createInstructionCombiningPass());
                                    PM.add(createLoopUnrollPass(3));
                                    PM.add(createLoopUnrollAndJamPass(3));
                                });