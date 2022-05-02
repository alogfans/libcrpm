//
// Created by alogfans on 4/1/21.
//

#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <regex>
#include "context.h"

const static string EntryPoint = "main";
const static string UninstrumentAnnotation = "__crpm_dont_instrument";
const static string EntryPointInstrumentFunc = "__crpm_hook_rt_init";
const static string ExitPointInstrumentFunc = "__crpm_hook_rt_fini";

ModuleContext::ModuleContext(Module &M_, ModulePass *Pass_) : M(M_), Pass(Pass_) {
    findUninstrumentAnnotations();
    findFunctions();
    checkCheckpointCalls();
    EnableMTUnsafeOptimization = true; // TODO according to parameters
}

ModuleContext::~ModuleContext() {
    printStatistics();
}

void ModuleContext::findUninstrumentAnnotations() {
    for (auto &GV : M.getGlobalList()) {
        auto Initializer = dyn_cast<ConstantDataSequential>(GV.getInitializer());
        if (Initializer && Initializer->isCString() &&
            Initializer->getAsCString().equals(UninstrumentAnnotation)) {
            UninstrumentAnnotations.insert(&GV);
            continue;
        }
    }

    if (GlobalVariable *GA = M.getGlobalVariable("llvm.global.annotations")) {
        for (Value *AOp : GA->operands()) {
            ConstantArray *CA = dyn_cast<ConstantArray>(AOp);
            if (!CA) {
                continue;
            }
            for (Value *CAOp : CA->operands()) {
                ConstantStruct *CS = dyn_cast<ConstantStruct>(CAOp);
                if (!CS || CS->getNumOperands() < 2) {
                    continue;
                }
                Function *AnnotatedFunction = cast<Function>(
                        CS->getOperand(0)->getOperand(0));
                GlobalVariable *GAnn = dyn_cast<GlobalVariable>(
                        CS->getOperand(1)->getOperand(0));
                if (!GAnn) {
                    continue;
                }
                ConstantDataArray *A =
                        dyn_cast<ConstantDataArray>(GAnn->getOperand(0));
                if (!A) {
                    continue;
                }
                StringRef AS = A->getAsString();
                if (AS.startswith(UninstrumentAnnotation)) {
                    UninstrumentFunctions.insert(AnnotatedFunction);
                }
            }
        }
    }
}

void ModuleContext::findFunctions() {
    string pattern = "std::vector<.+, std::allocator<.+> >::.+";
    regex re(pattern);
    for (auto &F: M.getFunctionList()) {
        if (F.isDeclaration() || F.getName().startswith("__crpm_hook_rt_")) {
            continue;
        }
        // std::vector<T, std::allocator<T> > is skipped
        string demangledName = demangle(F.getName());
        if (regex_match(demangledName, re)) {
            outs() << demangledName;
            continue;
        }
        Functions.push_back(new FunctionContext(F, *this));
        if (F.getLinkage() != GlobalValue::ExternalLinkage) {
            StaticFunctions.insert(&F);
        }
    }

    for (auto &FC : Functions) {
        FC->findIncomingArguments();
    }
}

void ModuleContext::checkCheckpointCalls() {
    while (true) {
        bool Success = false;
        for (auto FC : Functions) {
            if (FC->CheckpointCalls.empty()) {
                continue;
            }
            for (auto Call : FC->IncomingCalls) {
                auto ForeignFunc = getFunction(Call->getFunction());
                if (ForeignFunc && !ForeignFunc->CheckpointCalls.count(Call)) {
                    ForeignFunc->CheckpointCalls.insert(Call);
                    Success = true;
                }
            }
        }
        if (!Success) {
            break;
        }
    }
}

void ModuleContext::updateStatistics(const string &field, int count) {
    if (Statistics.count(field) == 0) {
        Statistics[field] = count;
    } else {
        Statistics[field] += count;
    }
}

void ModuleContext::printStatistics() {
    outs() << "Stat: ";
    for (auto KV : Statistics) {
        outs() << KV.first << "=" << KV.second << ", ";
    }
    outs() << "\n";
}

void ModuleContext::analysis() {
    for (auto FC : Functions) {
        FC->performAllOptimizations();
    }
}

void ModuleContext::transform() {
    for (auto FC : Functions) {
        FC->transform();
    }
    transformMainFunction();
}

void ModuleContext::transformMainFunction() {
    Function *F = M.getFunction(EntryPoint);
    if (!F) {
        return;
    }

    // First instruction of main()
    {
        IRBuilder<> Builder(&(*F->begin()->begin()));
        FunctionType *InitFuncType = FunctionType::get(Builder.getVoidTy(), false);
        auto InitFunc = M.getOrInsertFunction(EntryPointInstrumentFunc, InitFuncType);
        Builder.CreateCall(InitFunc);
    }

    // Last instruction of main()
    {
        for (auto &BB : *F) {
            for (auto &I : BB) {
                ReturnInst *RInst = dyn_cast<ReturnInst>(&I);
                if (RInst) {
                    IRBuilder<> Builder(RInst);
                    FunctionType *FiniFuncType =
                            FunctionType::get(Builder.getVoidTy(), false);
                    auto FiniFunc = M.getOrInsertFunction(ExitPointInstrumentFunc, FiniFuncType);
                    Builder.CreateCall(FiniFunc);
                }
            }
        }
    }
}

FunctionContext *ModuleContext::getFunction(Function *F) {
    for (auto &FC : Functions) {
        if (&FC->F == F) {
            return FC;
        }
    }
    return nullptr;
}