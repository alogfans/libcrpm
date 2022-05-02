//
// Created by alogfans on 10/28/20.
//

#include <set>
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/IPO.h>

using namespace llvm;
using namespace std;

static const set<string> CheckpointFuncs = {
        "_ZN5crpm10MemoryPool10checkpointEm", "crpm_checkpoint", "crpm_mpi_checkpoint"
};

static const set<string> StdAllocFuncs = {
        "malloc", "calloc", "realloc", "posix_memalign",
        "aligned_alloc", "pvalloc", "_Znam", "_Znvm", "_Znwm"
};

namespace {
    struct PreprocessorPass : public ModulePass {
        static char ID;

        PreprocessorPass() : ModulePass(ID) {}

        bool runOnModule(Module &M) override {
            findTargetFunctions(M);
            for (auto &F : TargetFunctions) {
                for (auto &BB : F->getBasicBlockList()) {
                    for (auto &I : BB) {
                        auto Inst = dyn_cast<StoreInst>(&I);
                        if (Inst && isPotentialPersistent(Inst->getPointerOperand())) {
                            Inst->setVolatile(true);
                        }
                    }
                }
            }
            return true;
        }

        Value *discoverDependency(Value *Source) {
            while (true) {
                if (isa<CastInst>(Source)) {
                    auto CastI = cast<CastInst>(Source);
                    Value *Previous = CastI->getOperand(0);
                    assert(Source != Previous);
                    Source = Previous;
                } else if (isa<GetElementPtrInst>(Source)) {
                    auto GEPInst = cast<GetElementPtrInst>(Source);
                    Value *Previous = GEPInst->getPointerOperand();
                    assert(Source != Previous);
                    Source = Previous;
                } else {
                    break;
                }
            }
            return Source;
        }

        bool isPotentialPersistent(Value *Source) {
            Source = discoverDependency(Source);
            if (isa<AllocaInst>(Source) || isa<GlobalVariable>(Source)) {
                return false;
            } else if (isa<CallBase>(Source)) {
                auto CF = cast<CallBase>(Source)->getCalledFunction();
                if (CF && StdAllocFuncs.count(CF->getName())) {
                    return false;
                }
            }
            return true;
        }

        void findTargetFunctions(Module &M) {
            // 1. Find checkpoint call
            for (auto &F : M) {
                for (auto &BB : F) {
                    for (auto &Inst : BB) {
                        auto Call = dyn_cast<CallBase>(&Inst);
                        if (Call && CheckpointFuncs.count(Call->getName())) {
                            TargetFunctions.insert(&F);
                            break;
                        }
                    }
                }
            }

            // 2. Find closure
            while (true) {
                bool Success = false;
                for (auto &F : M) {
                    if (TargetFunctions.count(&F)) {
                        continue;
                    }
                    for (auto &BB : F) {
                        for (auto &Inst : BB) {
                            auto Call = dyn_cast<CallBase>(&Inst);
                            if (Call && TargetFunctions.count(Call->getCalledFunction())) {
                                TargetFunctions.insert(&F);
                                Success = true;
                            }
                        }
                    }
                }
                if (!Success) {
                    break;
                }
            }
        }

        set<Function *> TargetFunctions;
    };
}

char PreprocessorPass::ID = 0;

static RegisterPass<PreprocessorPass> X("crpm-pre", "CRPM Pre-processor Pass");
static RegisterStandardPasses Y(PassManagerBuilder::EP_ModuleOptimizerEarly,
                                [](const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
                                    // PM.add(createFunctionInliningPass());
                                    // PM.add(new PreprocessorPass());
                                });
