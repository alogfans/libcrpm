//
// Created by Feng Ren on 2021/1/20.
//

#include <stack>
#include <queue>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include "context.h"

static const set<string> StdAllocFuncs = {
        "malloc", "calloc", "realloc", "posix_memalign",
        "aligned_alloc", "pvalloc", "_Znam", "_Znvm", "_Znwm"
};

static const set<string> PersistentAllocFuncs = {
        "crpm_malloc", "_ZN5crpm10MemoryPool7pmallocEm"
};

static const set<string> CheckpointFuncs = {
        "_ZN5crpm10MemoryPool10checkpointEm", "crpm_checkpoint", "crpm_mpi_checkpoint"
};

const static string ManualInstrumentCall = "AnnotateCheckpointRegion";

static const map<string, int> ExternalFuncs = {
        {"read", 1}, {"fread", 0}, {"MPI_Recv", 0}, {"MPI_Irecv", 0},
};

static const set<string> WhiteListFuncs = {
        // ...
};

LoopContext::LoopContext(Loop &L, LoopContext *ParentLoop_) :
        ParentLoop(ParentLoop_), IndVar(nullptr) {
    PreHeader = L.getLoopPreheader();
    Header = L.getHeader();
    Latch = L.getLoopLatch();
    L.getExitingBlocks(ExitingBlocks);
    BasicBlocks = L.getBlocksVector();
}

FunctionContext::FunctionContext(Function &F_, ModuleContext &Parent_)
        : F(F_), Parent(Parent_) {
    DT = &Parent.Pass->getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    AA = &Parent.Pass->getAnalysis<AAResultsWrapperPass>(F).getAAResults();
    findReturnValues();
    findLoops();
    findManualInstrumentCalls();
    findCheckpointCalls();
    findMemoryStore();
    DT->releaseMemory();
}

void FunctionContext::findReturnValues() {
    for (auto &BB : F) {
        for (auto &Inst : BB) {
            if (isa<ReturnInst>(Inst)) {
                ReturnValues.push_back(cast<ReturnInst>(&Inst)->getReturnValue());
            }
        }
    }
}

void FunctionContext::findLoops() {
    auto &SE = Parent.Pass->getAnalysis<ScalarEvolutionWrapperPass>(F).getSE();
    auto &LI = Parent.Pass->getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    queue<Loop *> loopQueue;
    queue<LoopContext *> loopQueueParent;
    for (auto L : LI) {
        loopQueue.push(L);
        loopQueueParent.push(nullptr);
    }

    while (!loopQueue.empty()) {
        auto L = loopQueue.front();
        loopQueue.pop();
        auto LParent = loopQueueParent.front();
        loopQueueParent.pop();
        auto LC = new LoopContext(*L, LParent);
        auto LoopBound = L->getBounds(SE);
        if (LoopBound.hasValue()) {
            auto IndVar = L->getInductionVariable(SE); // PHI
            auto InitialVal = &LoopBound->getInitialIVValue();
            auto FinalVal = &LoopBound->getFinalIVValue();
            auto Direction = LoopBound->getDirection();
            if (Direction == Loop::LoopBounds::Direction::Increasing) {
                LC->IndVar = new IndVarContext(IndVar, InitialVal, FinalVal);
            } else if (Direction == Loop::LoopBounds::Direction::Decreasing) {
                LC->IndVar = new IndVarContext(IndVar, FinalVal, InitialVal);
            }
        } else {
            findInductionVariable(LC);
        }
        SortedLoops.push_back(LC);
        for (auto NestedLoop : *L) {
            loopQueue.push(NestedLoop);
            loopQueueParent.push(LC);
        }
    }
    LI.releaseMemory();
}

void FunctionContext::findIncomingArguments() {
    for (auto &BB : F) {
        for (auto &Inst : BB) {
            auto CallInst = dyn_cast<CallBase>(&Inst);
            if (!CallInst) {
                continue;
            }
            auto CallFunc = CallInst->getCalledFunction();
            if (!CallFunc) {
                continue;
            }
            auto FC = Parent.getFunction(CallFunc);
            if (FC) {
                FC->IncomingCalls.insert(CallInst);
                for (int i = 0; i < CallFunc->arg_size(); ++i) {
                    FC->IncomingArguments.insert(make_pair(
                            CallFunc->getArg(i),
                            CallInst->getArgOperand(i)));
                }
            }
        }
    }
}

void FunctionContext::findInductionVariable(LoopContext *LC) {
    if (LC->IndVar) {
        return;
    }

    for (auto &I : LC->Header->getInstList()) {
        if (!isa<PHINode>(I)) {
            break;
        }
        auto PHIValue = cast<PHINode>(&I);
        if (PHIValue->getNumOperands() != 2) {
            continue;
        }
        Value *StartValue, *LatchValue;
        bool BlockInLoop[2];
        BlockInLoop[0] = !isBasicBlockOutOfLoop(LC, PHIValue->getIncomingBlock(0));
        BlockInLoop[1] = !isBasicBlockOutOfLoop(LC, PHIValue->getIncomingBlock(1));
        if (BlockInLoop[0] && !BlockInLoop[1]) {
            StartValue = PHIValue->getIncomingValue(1);
            LatchValue = PHIValue->getIncomingValue(0);
        } else if (BlockInLoop[1] && !BlockInLoop[0]) {
            StartValue = PHIValue->getIncomingValue(0);
            LatchValue = PHIValue->getIncomingValue(1);
        } else {
            continue;
        }

        auto LatchInst = dyn_cast<Instruction>(LatchValue);
        if (!LatchInst ||
            LatchInst->getOpcode() != Instruction::Add ||
            LatchInst->getOperand(0) != PHIValue ||
            !isPointerOutOfLoop(LC, LatchInst->getOperand(1))) {
            continue;
        }

        if (LC->ExitingBlocks.size() != 1) {
            continue;
        }
        auto ExitingBB = *LC->ExitingBlocks.begin();
        auto BrInst = dyn_cast<BranchInst>(ExitingBB->getTerminator());
        if (!BrInst) {
            continue;
        }
        auto Condition = dyn_cast<CmpInst>(BrInst->getCondition());
        if (!Condition || Condition->getPredicate() != CmpInst::ICMP_EQ) {
            continue;
        }
        auto FinalValue = Condition->getOperand(1);
        if (Condition->getOperand(0) == LatchInst && isPointerOutOfLoop(LC, FinalValue)) {
            LC->IndVar = new IndVarContext(PHIValue, StartValue, FinalValue);
            break;
        }
    }
}

void FunctionContext::findManualInstrumentCalls() {
    for (auto &BB : F.getBasicBlockList()) {
        for (auto &Inst : BB) {
            auto FlushInst = dyn_cast<CallInst>(&Inst);
            if (!FlushInst || !FlushInst->getCalledFunction()) {
                continue;
            }
            auto FuncName = FlushInst->getCalledFunction()->getName();
            if (FuncName.equals(ManualInstrumentCall)) {
                ManualInstrumentCalls.push_back(FlushInst);
            }
        }
    }
}

void FunctionContext::findCheckpointCalls() {
    for (auto &BB : F) {
        for (auto &Inst : BB) {
            auto Call = dyn_cast<CallBase>(&Inst);
            if (!Call) {
                continue;
            }
            auto CalledFunc = Call->getCalledFunction();
            if (CalledFunc && CheckpointFuncs.count(CalledFunc->getName())) {
                CheckpointCalls.insert(Call);   // explicit checkpoint() call
            } else {
                /*
                // Conservative approach
                if (!CalledFunc) {
                    CheckpointCalls.insert(Call);
                    continue;
                }
                if (Parent.getFunction(CalledFunc)) {
                    continue;                   // postpone to module level
                }
                if (WhiteListFuncs.count(CalledFunc->getName())) {
                    continue;                   // external functions without checkpoint() call
                }
                CheckpointCalls.insert(Call);
                 */
            }
        }
    }
}

void FunctionContext::findMemoryStore() {
    Value *Pointer;
    for (auto &BB : F) {
        for (auto &Inst : BB) {
            Pointer = getStorePointer(&Inst);
            if (Pointer) {
                MemoryStore.insert(new MemoryStoreHook(Pointer, &Inst));
                continue;
            }

            auto MemOp = dyn_cast<AnyMemIntrinsic>(&Inst);
            if (MemOp) {
                Pointer = MemOp->getRawDest();
                Value *Length = MemOp->getLength();
                MemoryBulkStore.insert(new MemoryBulkStoreHook(Pointer, &Inst, Length));
                continue;
            }

            auto CallOp = dyn_cast<CallBase>(&Inst);
            if (CallOp) {
                auto CallFunc = CallOp->getCalledFunction();
                if (CallFunc && ExternalFuncs.count(CallFunc->getName())) {
                    Value *Arg = CallOp->getArgOperand(ExternalFuncs.at(CallFunc->getName()));
                    ProtectedExternalCalls[Arg] = CallOp;
                }
            }
        }
    }
}

bool FunctionContext::hasPotentialStore(LoadInst *TargetInst, BasicBlock *BB) {
    for (auto &Inst : *BB) {
        Value *Pointer = getStorePointer(&Inst);
        if (!Pointer || !DT->dominates(&Inst, TargetInst)) {
            continue;
        }
        auto TargetMemLoc = MemoryLocation::get(TargetInst);
        auto MemLoc = MemoryLocation(Pointer, TargetMemLoc.Size);
        if (!AA->isNoAlias(MemLoc, TargetMemLoc)) {
            return true;
        }
    }
    return false;
}

bool FunctionContext::hasPotentialStore(LoadInst *TargetInst) {
    for (auto &BB : F) {
        if (hasPotentialStore(TargetInst, &BB)) {
            return true;
        }
    }
    return false;
}

Value *FunctionContext::getLoadPointer(Value *Pointer, bool null_if_failed) {
    Value *Result = getUncastPointer(Pointer);
    if (!isa<LoadInst>(Result)) {
        return null_if_failed ? nullptr : Result;
    } else {
        return dyn_cast<LoadInst>(Result)->getPointerOperand();
    }
}

Value *FunctionContext::getStorePointer(Instruction *Inst) {
    if (isa<StoreInst>(Inst)) {
        return cast<StoreInst>(Inst)->getPointerOperand();
    } else if (isa<AtomicCmpXchgInst>(Inst)) {
        return cast<AtomicCmpXchgInst>(Inst)->getPointerOperand();
    } else if (isa<AtomicRMWInst>(Inst)) {
        return cast<AtomicRMWInst>(Inst)->getPointerOperand();
    }
    return nullptr;
}

Value *FunctionContext::getUnaryPointer(Value *Pointer, bool null_if_failed) {
    Value *Result = getUncastPointer(Pointer);
    if (!isa<GetElementPtrInst>(Result)) {
        return null_if_failed ? nullptr : Result;
    } else {
        return dyn_cast<GetElementPtrInst>(Result)->getPointerOperand();
    }
}

Value *FunctionContext::getUncastPointer(Value *Pointer) {
    Value *Result = Pointer;
    if (isa<CastInst>(Result)) {
        Result = cast<CastInst>(Result)->getOperand(0);
    }
    return Result;
}

bool FunctionContext::isPointerOutOfLoop(LoopContext *LC, Value *Pointer) {
    if (isa<Constant>(Pointer) || isa<GlobalVariable>(Pointer) || isa<Argument>(Pointer)) {
        return true;
    }

    if (!isa<Instruction>(Pointer)) {
        return false; // unsure
    }

    auto BB = cast<Instruction>(Pointer)->getParent();
    return find(LC->BasicBlocks, BB) == LC->BasicBlocks.end();
}

bool FunctionContext::isBasicBlockOutOfLoop(LoopContext *LC, BasicBlock *BB) {
    return find(LC->BasicBlocks, BB) == LC->BasicBlocks.end();
}

bool FunctionContext::allowHoistingGetElemPtr(LoopContext *LC, GetElementPtrInst *GEPInst) {
    for (int idx = 0; idx != GEPInst->getNumOperands(); idx++) {
        if (!isPointerOutOfLoop(LC, GEPInst->getOperand(idx))) {
            return false;
        }
    }
    return true;
}

bool FunctionContext::allowAggregatingGetElemPtr(LoopContext *LC, GetElementPtrInst *GEPInst) {
    if (!GEPInst->isInBounds() || GEPInst->getNumIndices() != 1) {
        return false;
    }
    Value *Pointer = GEPInst->getPointerOperand();
    Value *Index = GEPInst->getOperand(1);
    if (!AA->isMustAlias(Index, LC->IndVar->IndVar)) {
        return false;
    }
    if (isPointerOutOfLoop(LC, Pointer)) {
        return true;
    }
    if (Parent.EnableMTUnsafeOptimization) {
        auto Inst = dyn_cast<LoadInst>(Pointer);
        if (Inst && isPointerOutOfLoop(LC, Inst->getPointerOperand())) {
            for (auto BB : LC->BasicBlocks) {
                if (hasPotentialStore(Inst, BB)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

Value *FunctionContext::discoverDependency(std::set<Value *> &SeenValues, Value *Source) {
    while (true) {
        if (SeenValues.find(Source) != SeenValues.end()) {
            // Find a loop
            return nullptr;
        }
        if (isa<CastInst>(Source)) {
            Value *Previous = cast<CastInst>(Source)->getOperand(0);
            assert(Source != Previous);
            Source = Previous;
        } else if (isa<GetElementPtrInst>(Source)) {
            Value *Previous = cast<GetElementPtrInst>(Source)->getPointerOperand();
            assert(Source != Previous);
            Source = Previous;
        } else if (isa<PHINode>(Source)) {
            auto PHIInst = cast<PHINode>(Source);
            set<Value *> Result;
            for (int i = 0; i < PHIInst->getNumOperands(); ++i) {
                Value *NewSource = PHIInst->getIncomingValue(i);
                SeenValues.insert(Source);
                Value *Sink = discoverDependency(SeenValues, NewSource);
                SeenValues.erase(Source);
                if (Sink != nullptr) {
                    Result.insert(Sink);
                }
            }
            if (Result.size() != 1) {
                return PHIInst;
            } else {
                return *Result.begin();
            }
        } else {
            return Source;
        }
    }
}

int FunctionContext::getMemoryPointerState(Value *Source) {
    std::set<Value *> SeenValues;
    Source = discoverDependency(SeenValues, Source);

    if (isa<AllocaInst>(Source) || isa<GlobalVariable>(Source)) {
        return POINTER_VOLATILE;
    } else if (isa<Argument>(Source)) {
        auto Arg = cast<Argument>(Source);
        if (!Parent.StaticFunctions.count(&F)) {
            return POINTER_UNKNOWN;
        }
        set<int> state;
        for (auto KV : IncomingArguments) {
            if (KV.first != Arg) {
                continue;
            }
            if (isa<AllocaInst>(KV.second) || isa<GlobalVariable>(KV.second)) {
                state.insert(POINTER_VOLATILE);
                continue;
            }
            if (!isa<Instruction>(KV.second)) {
                return POINTER_UNKNOWN;
            }
            auto FF = cast<Instruction>(KV.second)->getFunction();
            auto FC = Parent.getFunction(FF);
            if (!FC || FC == this) {
                return POINTER_UNKNOWN;
            }
            state.insert(FC->getMemoryPointerState(KV.second));
        }
        if (!state.count(POINTER_PERSISTENT) && !state.count(POINTER_UNKNOWN)) {
            return POINTER_VOLATILE;
        } else if (!state.count(POINTER_VOLATILE) && !state.count(POINTER_UNKNOWN)) {
            return POINTER_PERSISTENT;
        } else {
            return POINTER_UNKNOWN;
        }
    } else if (isa<CallBase>(Source)) {
        auto CallFunc = cast<CallBase>(Source)->getCalledFunction();
        if (!CallFunc) {
            return POINTER_UNKNOWN;
        }
        auto FName = CallFunc->getName();
        if (StdAllocFuncs.count(FName)) {
            return POINTER_VOLATILE;
        }
        if (PersistentAllocFuncs.count(FName)) {
            return POINTER_PERSISTENT;
        }
        auto CallFC = Parent.getFunction(CallFunc);
        if (!CallFC) {
            return POINTER_UNKNOWN;
        }
        set<int> state;
        for (auto V : CallFC->ReturnValues) {
            state.insert(CallFC->getMemoryPointerState(V));
        }
        if (!state.count(POINTER_PERSISTENT) && !state.count(POINTER_UNKNOWN)) {
            return POINTER_VOLATILE;
        } else if (!state.count(POINTER_VOLATILE) && !state.count(POINTER_UNKNOWN)) {
            return POINTER_PERSISTENT;
        } else {
            return POINTER_UNKNOWN;
        }
    }
    return POINTER_UNKNOWN;
}

bool FunctionContext::maySplitByCheckpoint(Instruction *Start, Instruction *Stop) {
    if (CheckpointCalls.empty()) {
        return false;
    }
    for (auto Call : CheckpointCalls) {
        if (Call == Start || Call == Stop) {
            return true;
        }
    }

    if (Start->getParent() == Stop->getParent()) {
        for (auto Call : CheckpointCalls) {
            if (Call->getParent() != Start->getParent()) {
                continue;
            }
            if (DT->dominates(Start, Call) && !DT->dominates(Stop, Call)) {
                return true;
            }
        }
        return false;
    } else {
        set<BasicBlock *> VisitedBB;
        queue<BasicBlock *> Queue;
        Queue.push(Stop->getParent());
        while (!Queue.empty()) {
            BasicBlock *CurBB = Queue.front();
            Queue.pop();
            if (VisitedBB.count(CurBB)) {
                continue;
            }
            VisitedBB.insert(CurBB);
            if (CurBB == Start->getParent()) {
                continue;
            }
            for (auto PrevBB : predecessors(CurBB)) {
                for (auto Call : CheckpointCalls) {
                    if (Call->getParent() == PrevBB) {
                        return true;
                    }
                }
                Queue.push(PrevBB);
            }
        }
        return false;
    }
}