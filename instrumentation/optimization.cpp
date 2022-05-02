//
// Created by alogfans on 4/1/21.
//

#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include "context.h"

void FunctionContext::performAllOptimizations() {
    DT = &Parent.Pass->getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    AA = &Parent.Pass->getAnalysis<AAResultsWrapperPass>(F).getAAResults();
    bool Success;
    do {
        Success = false;
        Success |= performInsertionPtPromotion();
        Success |= performLoopHoisting();
        Success |= performLoopHoistingForBulk();
        Success |= performRedundantStoreElimination();
        Success |= performAnnotatedUninstrumentation();
        Success |= performTransientElimination();
        Success |= performManualInstrumentElimination();
        Success |= performStructStoreCombination();
        Success |= performLoopAggregation();
    } while (Success);
    DT->releaseMemory();
}

bool FunctionContext::performRedundantStoreElimination() {
    std::vector<MemoryStoreHook *> Buffer;
    bool Success = false;

    for (auto LHS = MemoryStore.begin(); LHS != MemoryStore.end(); LHS++) {
        auto RHS = LHS;
        RHS++;
        for (; RHS != MemoryStore.end(); RHS++) {
            auto LHS_Inst = (*LHS)->InsertionPt, RHS_Inst = (*RHS)->InsertionPt;
            auto LHS_Ptr = (*LHS)->Pointer, RHS_Ptr = (*RHS)->Pointer;
            if (DT->dominates(LHS_Inst, RHS_Inst) &&
                    AA->isMustAlias(LHS_Ptr, RHS_Ptr) &&
                    !maySplitByCheckpoint(LHS_Inst, RHS_Inst)) {
                Buffer.push_back(*RHS);
            }
        }
    }

    for (auto Entry : Buffer) {
        Success = true;
        MemoryStore.erase(Entry);
        Parent.updateStatistics("RedundantStoreElimination");
    }

    Buffer.clear();
    return Success;
}

bool FunctionContext::performAnnotatedUninstrumentation() {
    if (Parent.UninstrumentFunctions.count(&F)) {
        if (MemoryStore.empty() && MemoryBulkStore.empty()) {
            return false;
        }
        Parent.updateStatistics("AnnotatedUninstrumentation", MemoryStore.size());
        Parent.updateStatistics("AnnotatedUninstrumentation", MemoryBulkStore.size());
        MemoryStore.clear();
        MemoryBulkStore.clear();
        return true;
    }

    std::vector<MemoryStoreHook *> Buffer;
    bool Success = false;
    for (auto Entry : MemoryStore) {
        Value *Pointer = getUnaryPointer(Entry->Pointer, false);
        if (!Pointer || !Pointer->getType()->isPointerTy() || !isa<CallInst>(Pointer)) {
            continue;
        }
        auto Inst = dyn_cast<CallInst>(Pointer);
        auto CF = Inst->getCalledFunction();
        if (!CF || !CF->getName().startswith("llvm.ptr.annotation")) {
            continue;
        }
        auto GEPExpr = dyn_cast<ConstantExpr>(Inst->getOperand(1));
        if (!GEPExpr || GEPExpr->getOpcode() != Instruction::GetElementPtr) {
            continue;
        }
        auto Annotation = dyn_cast<GlobalVariable>(GEPExpr->getOperand(0));
        if (Annotation && Parent.UninstrumentAnnotations.count(Annotation)) {
            Buffer.push_back(Entry);
        }
    }

    for (auto Entry : Buffer) {
        Success = true;
        MemoryStore.erase(Entry);
        Parent.updateStatistics("AnnotatedUninstrumentation");
    }

    std::vector<MemoryBulkStoreHook *> BulkBuffer;
    for (auto Entry : MemoryBulkStore) {
        Value *Pointer = getUnaryPointer(Entry->Pointer, false);
        if (!Pointer || !Pointer->getType()->isPointerTy() || !isa<CallInst>(Pointer)) {
            continue;
        }
        auto Inst = dyn_cast<CallInst>(Pointer);
        auto CF = Inst->getCalledFunction();
        if (!CF || !CF->getName().startswith("llvm.ptr.annotation")) {
            continue;
        }
        auto GEPExpr = dyn_cast<ConstantExpr>(Inst->getOperand(1));
        if (!GEPExpr || GEPExpr->getOpcode() != Instruction::GetElementPtr) {
            continue;
        }
        auto Annotation = dyn_cast<GlobalVariable>(GEPExpr->getOperand(0));
        if (Annotation && Parent.UninstrumentAnnotations.count(Annotation)) {
            BulkBuffer.push_back(Entry);
        }
    }

    for (auto Entry : BulkBuffer) {
        Success = true;
        MemoryBulkStore.erase(Entry);
        Parent.updateStatistics("AnnotatedUninstrumentation");
    }

    return Success;
}

bool FunctionContext::performManualInstrumentElimination() {
    std::set<Value *> ExtraAnnotatePtr;
    if (Parent.EnableMTUnsafeOptimization) {
        for (auto Inst : ManualInstrumentCalls) {
            auto AnnotatePtr = Inst->getArgOperand(0);
            auto AnnotateLoadInst = dyn_cast<LoadInst>(getUncastPointer(AnnotatePtr));
            if (!AnnotateLoadInst || hasPotentialStore(AnnotateLoadInst)) {
                continue;
            }
            ExtraAnnotatePtr.insert(AnnotatePtr);
        }
    }

    std::vector<MemoryStoreHook *> RemovingContext;
    for (auto Entry : MemoryStore) {
        auto StorePtr = getUnaryPointer(Entry->Pointer, false);
        for (auto Inst : ManualInstrumentCalls) {
            auto AnnotatePtr = Inst->getArgOperand(0);
            if (!DT->dominates(Inst, Entry->InsertionPt) ||
                maySplitByCheckpoint(Inst, Entry->InsertionPt)) {
                continue;
            }
            if (AA->isMustAlias(AnnotatePtr, StorePtr)) {
                RemovingContext.push_back(Entry);
                break;
            }
            if (Parent.EnableMTUnsafeOptimization) {
                auto AnnotateLPtr = getLoadPointer(AnnotatePtr, true);
                auto StoreLPtr = getLoadPointer(StorePtr, true);
                if (!AnnotateLPtr || !StoreLPtr || !AA->isMustAlias(AnnotateLPtr, StoreLPtr)) {
                    continue;
                }
                if (ExtraAnnotatePtr.count(AnnotatePtr)) {
                    RemovingContext.push_back(Entry);
                    break;
                }
            }
        }
    }

    std::vector<MemoryBulkStoreHook *> RemovingBulkContext;
    for (auto Entry : MemoryBulkStore) {
        auto StorePtr = getUnaryPointer(Entry->Pointer, false);
        for (auto Inst : ManualInstrumentCalls) {
            auto AnnotatePtr = Inst->getArgOperand(0);
            if (!DT->dominates(Inst, Entry->InsertionPt) ||
                maySplitByCheckpoint(Inst, Entry->InsertionPt)) {
                continue;
            }
            if (AA->isMustAlias(AnnotatePtr, StorePtr)) {
                RemovingBulkContext.push_back(Entry);
                break;
            }
            if (Parent.EnableMTUnsafeOptimization) {
                auto AnnotateLPtr = getLoadPointer(AnnotatePtr, true);
                auto StoreLPtr = getLoadPointer(StorePtr, true);
                if (!AnnotateLPtr || !StoreLPtr || !AA->isMustAlias(AnnotateLPtr, StoreLPtr)) {
                    continue;
                }
                if (ExtraAnnotatePtr.count(AnnotatePtr)) {
                    RemovingBulkContext.push_back(Entry);
                    break;
                }
            }
        }
    }

    bool Success = false;
    for (auto Entry : RemovingContext) {
        Success = true;
        MemoryStore.erase(Entry);
        Parent.updateStatistics("ManualInstrumentElimination");
    }
    RemovingContext.clear();
    for (auto Entry : RemovingBulkContext) {
        Success = true;
        MemoryBulkStore.erase(Entry);
        Parent.updateStatistics("ManualInstrumentElimination");
    }
    RemovingBulkContext.clear();
    return Success;
}

bool FunctionContext::performStructStoreCombination() {
    map<Value *, int> AppliedCounter;
    set<Value *> AppliedPointers;
    std::vector<MemoryStoreHook *> RemovingContext;
    if (!CheckpointCalls.empty()) {
        return false; // FIXME
    }
    for (auto Entry : MemoryStore) {
        // Access member of a struct object
        auto Pointer = getUnaryPointer(Entry->Pointer, true);
        if (!Pointer || !Pointer->getType()->isPointerTy()) {
            continue;
        }
        if (!isa<Instruction>(Pointer) && !isa<Argument>(Pointer)) {
            continue;
        }
        auto Ty = Pointer->getType()->getPointerElementType();
        if (!Ty->isStructTy()) {
            continue;
        }
        auto Layout = Parent.M.getDataLayout().getStructLayout(cast<StructType>(Ty));
        if (Layout->getSizeInBytes() > 1024) {
            continue; // skip if it is too large
        }
        if (AppliedCounter.count(Pointer)) {
            AppliedCounter[Pointer]++;
        } else {
            AppliedCounter[Pointer] = 1;
        }
    }

    for (auto Entry : MemoryStore) {
        auto Pointer = getUnaryPointer(Entry->Pointer, true);
        if (AppliedCounter.count(Pointer) && AppliedCounter[Pointer] > 1) {
            AppliedPointers.insert(Pointer);
            RemovingContext.push_back(Entry);
        }
    }

    for (auto Entry : AppliedPointers) {
        Instruction *Inst;
        if (isa<PHINode>(Entry)) {
            Inst = cast<Instruction>(Entry)->getNextNonDebugInstruction();
            while (isa<PHINode>(Inst)) {
                Inst = cast<Instruction>(Inst)->getNextNonDebugInstruction();
            }
        } else if (isa<Instruction>(Entry)) {
            Inst = cast<Instruction>(Entry)->getNextNonDebugInstruction();
        } else {
            Inst = F.begin()->getFirstNonPHIOrDbg();
        }
        if (!Inst) {
            return false;
        }
        auto Ty = cast<StructType>(Entry->getType()->getPointerElementType());
        auto Layout = Parent.M.getDataLayout().getStructLayout(cast<StructType>(Ty));
        Type *Int64Ty = Type::getInt64Ty(F.getContext());
        auto LengthValue = ConstantInt::getSigned(Int64Ty, Layout->getSizeInBytes());
        MemoryBulkStore.insert(new MemoryBulkStoreHook(Entry, Inst, LengthValue));
    }

    bool Success = false;
    for (auto Entry : RemovingContext) {
        MemoryStore.erase(Entry);
        Parent.updateStatistics("StructStoreCombination");
        Success = true;
    }
    RemovingContext.clear();
    return Success;
}

bool FunctionContext::performTransientElimination() {
    std::vector<MemoryStoreHook *> Buffer;
    bool Success = false;
    for (auto Entry : MemoryStore) {
        int Result = getMemoryPointerState(Entry->Pointer);
        if (Result == POINTER_VOLATILE) {
            Buffer.push_back(Entry);
        }
    }
    for (auto Entry : Buffer) {
        Success = true;
        MemoryStore.erase(Entry);
        Parent.updateStatistics("TransientElimination");
    }
    Buffer.clear();

    std::vector<MemoryBulkStoreHook *> BulkBuffer;
    for (auto Entry : MemoryBulkStore) {
        int Result = getMemoryPointerState(Entry->Pointer);
        if (Result == POINTER_VOLATILE) {
            BulkBuffer.push_back(Entry);
        }
    }
    for (auto Entry : BulkBuffer) {
        Success = true;
        MemoryBulkStore.erase(Entry);
        Parent.updateStatistics("TransientElimination");
    }
    BulkBuffer.clear();

    std::vector<Value *> RemoveExternalCalls;
    for (auto Entry : ProtectedExternalCalls) {
        int Result = getMemoryPointerState(Entry.first);
        if (Result == POINTER_VOLATILE) {
            RemoveExternalCalls.push_back(Entry.first);
        }
    }
    for (auto Entry : RemoveExternalCalls) {
        Success = true;
        ProtectedExternalCalls.erase(Entry);
        Parent.updateStatistics("TransientElimination");
    }
    RemoveExternalCalls.clear();

    return Success;
}

bool FunctionContext::performLoopHoisting() {
    std::vector<MemoryStoreHook *> Hoisting;
    std::vector<MemoryStoreHook *> HoistingAndCopyingGEP;
    bool Success = false;

    for (auto &Loop : SortedLoops) {
        if (!Loop->PreHeader) {
            continue;
        }

        Instruction *InsertionPt = Loop->PreHeader->getFirstNonPHIOrDbg();
        while (InsertionPt && !InsertionPt->isTerminator()) {
            InsertionPt = InsertionPt->getNextNonDebugInstruction();
        }
        if (!InsertionPt) {
            continue;
        }

        for (auto Entry : MemoryStore) {
            if (isBasicBlockOutOfLoop(Loop, Entry->InsertionPt->getParent())) {
                continue;
            }
            if (maySplitByCheckpoint(InsertionPt, Entry->InsertionPt)) {
                continue;
            }
            if (isa<Argument>(Entry->Pointer)) {
                Hoisting.push_back(Entry);
                continue;
            }
            if (isa<Instruction>(Entry->Pointer)) {
                Value *Pointer = getUncastPointer(Entry->Pointer);
                if (isPointerOutOfLoop(Loop, Pointer)) {
                    Hoisting.push_back(Entry);
                } else if (isa<GetElementPtrInst>(Pointer)) {
                    auto GEPInst = cast<GetElementPtrInst>(Pointer);
                    if (allowHoistingGetElemPtr(Loop, GEPInst)) {
                        HoistingAndCopyingGEP.push_back(Entry);
                    }
                }
            }
        }

        for (auto Entry : Hoisting) {
            Entry->Pointer = getUncastPointer(Entry->Pointer);
            Entry->InsertionPt = InsertionPt;
            Parent.updateStatistics("LoopHoisting");
            Success = true;
        }

        for (auto Entry : HoistingAndCopyingGEP) {
            Entry->Pointer = getUncastPointer(Entry->Pointer);
            Entry->InsertionPt = InsertionPt;
            Entry->CopyGEP = true;
            Parent.updateStatistics("LoopHoisting");
            Success = true;
        }

        Hoisting.clear();
        HoistingAndCopyingGEP.clear();
    }
    return Success;
}

bool FunctionContext::performLoopHoistingForBulk() {
    std::vector<MemoryBulkStoreHook *> Hoisting;
    bool Success = false;

    for (auto &Loop : SortedLoops) {
        if (!Loop->PreHeader) {
            continue;
        }

        Instruction *InsertionPt = Loop->PreHeader->getFirstNonPHIOrDbg();
        while (InsertionPt && !InsertionPt->isTerminator()) {
            InsertionPt = InsertionPt->getNextNonDebugInstruction();
        }
        if (!InsertionPt) {
            continue;
        }
        for (auto Entry : MemoryBulkStore) {
            if (isBasicBlockOutOfLoop(Loop, Entry->InsertionPt->getParent())) {
                continue;
            }
            if (Entry->UseStartStopIndex || Entry->CopyLoad) {
                continue; // Unsupported
            }
            if (!isPointerOutOfLoop(Loop, Entry->LengthOrStartIndex)) {
                continue;
            }
            if (maySplitByCheckpoint(InsertionPt, Entry->InsertionPt)) {
                continue;
            }
            if (isa<Argument>(Entry->Pointer)) {
                Hoisting.push_back(Entry);
                continue;
            }
            if (isa<Instruction>(Entry->Pointer)) {
                Value *Pointer = getUncastPointer(Entry->Pointer);
                if (isPointerOutOfLoop(Loop, Pointer)) {
                    Hoisting.push_back(Entry);
                }
            }
        }

        for (auto Entry : Hoisting) {
            Entry->Pointer = getUncastPointer(Entry->Pointer);
            Entry->InsertionPt = InsertionPt;
            Parent.updateStatistics("LoopHoistingForBulk");
            Success = true;
        }

        Hoisting.clear();
    }
    return Success;
}

bool FunctionContext::performLoopAggregation() {
    std::vector<MemoryStoreHook *> Hoisting;
    bool Success = false;

    for (auto &Loop : SortedLoops) {
        if (!Loop->PreHeader || !Loop->IndVar ||
            !isPointerOutOfLoop(Loop, Loop->IndVar->InitialVal) ||
            !isPointerOutOfLoop(Loop, Loop->IndVar->FinalVal)) {
            continue;
        }

        Instruction *InsertionPt = Loop->PreHeader->getFirstNonPHIOrDbg();
        while (InsertionPt && !InsertionPt->isTerminator()) {
            InsertionPt = InsertionPt->getNextNonDebugInstruction();
        }
        if (!InsertionPt) {
            continue;
        }
        for (auto BB : Loop->BasicBlocks) {
            for (auto MS : MemoryStore) {
                if (MS->InsertionPt->getParent() != BB) {
                    continue;
                }
                if (maySplitByCheckpoint(InsertionPt, MS->InsertionPt)) {
                    continue;
                }
                Value *Pointer = getUncastPointer(MS->Pointer);
                auto GEPInst = dyn_cast<GetElementPtrInst>(Pointer);
                if (GEPInst && allowAggregatingGetElemPtr(Loop, GEPInst)) {
                    Hoisting.push_back(MS);
                }
            }
        }

        for (auto Entry : Hoisting) {
            Value *Pointer = getUncastPointer(Entry->Pointer);
            assert(isa<GetElementPtrInst>(Pointer));
            bool LoadCopy = false;
            if (!isPointerOutOfLoop(Loop, cast<GetElementPtrInst>(Pointer)->getPointerOperand())) {
                LoadCopy = true;
            }
            auto NewEntry = new MemoryBulkStoreHook(Pointer, InsertionPt,
                                                    Loop->IndVar->InitialVal,
                                                    true,
                                                    Loop->IndVar->FinalVal,
                                                    LoadCopy);
            MemoryStore.erase(Entry);
            MemoryBulkStore.insert(NewEntry);
            Success = true;
            Parent.updateStatistics("LoopAggregation");
        }
        Hoisting.clear();
    }
    return Success;
}

bool FunctionContext::performInsertionPtPromotion() {
    bool Success = false;

    for (auto Entry : MemoryStore) {
        auto newInsertionPt = dyn_cast<Instruction>(Entry->Pointer);
        if (!newInsertionPt || Entry->CopyGEP) {
            continue;
        }
        if (isa<PHINode>(newInsertionPt)) {
            newInsertionPt = newInsertionPt->getParent()->getFirstNonPHIOrDbg();
        } else {
            newInsertionPt = newInsertionPt->getNextNonDebugInstruction();
        }
        if (!newInsertionPt) {
            continue;
        }
        if (maySplitByCheckpoint(newInsertionPt, Entry->InsertionPt)) {
            continue;
        }
        if (Entry->InsertionPt != newInsertionPt) {
            Success = true;
            Entry->InsertionPt = newInsertionPt;
        }
    }
    return Success;
}

void FunctionContext::transform() {
    Type *VoidTy = Type::getVoidTy(F.getContext());
    Type *Int32Ty = Type::getInt32Ty(F.getContext());
    Type *Int32PtrTy = Type::getInt32PtrTy(F.getContext());
    Type *Int64Ty = Type::getInt64Ty(F.getContext());
    AllocaInst *TmpAlloca;
    {
        IRBuilder<> Builder(&(*F.begin()->begin()));
        TmpAlloca = Builder.CreateAlloca(Int32Ty);
    }

    {
        FunctionType *StoreFuncTy = FunctionType::get(VoidTy, {Int64Ty}, false);
        FunctionCallee StoreFunc = Parent.M.getOrInsertFunction("__crpm_hook_rt_store",
                                                                StoreFuncTy);

        for (auto MS : MemoryStore) {
            if (MS->CopyGEP) {
                IRBuilder<> builder(MS->InsertionPt);
                auto GEPInst = cast<GetElementPtrInst>(MS->Pointer);
                Value *Result = builder.Insert(GEPInst->clone());
                Value *PointerCasted = builder.CreatePtrToInt(Result, Int64Ty);
                builder.CreateCall(StoreFunc, {PointerCasted});
            } else {
                IRBuilder<> builder(MS->InsertionPt);
                Value *PointerCasted = builder.CreatePtrToInt(MS->Pointer, Int64Ty);
                builder.CreateCall(StoreFunc, {PointerCasted});
            }
            Parent.updateStatistics("Transform");
        }
    }

    {
        FunctionType *StoreFuncTy = FunctionType::get(VoidTy, {Int64Ty, Int64Ty}, false);
        FunctionCallee StoreFunc = Parent.M.getOrInsertFunction("__crpm_hook_rt_range_store",
                                                                StoreFuncTy);

        for (auto MRS : MemoryBulkStore) {
            if (MRS->UseStartStopIndex) {
                IRBuilder<> builder(MRS->InsertionPt);
                auto GEPInst = cast<GetElementPtrInst>(MRS->Pointer);
                Value *Pointer = GEPInst->getPointerOperand();
                if (MRS->CopyLoad) {
                    auto Inst = cast<LoadInst>(Pointer);
                    Pointer = builder.CreateLoad(Inst->getPointerOperand());
                }
                Value *StartIdx = builder.CreateIntCast(MRS->LengthOrStartIndex, Int64Ty, true);
                Value *StartAddr = builder.CreateInBoundsGEP(nullptr, Pointer, StartIdx);
                Value *StopIdx = builder.CreateIntCast(MRS->StopIndex, Int64Ty, true);
                Value *EndAddr = builder.CreateInBoundsGEP(nullptr, Pointer, StopIdx);
                Value *StartPointerCasted = builder.CreatePtrToInt(StartAddr, Int64Ty);
                Value *EndPointerCasted = builder.CreatePtrToInt(EndAddr, Int64Ty);
                auto Range = builder.CreateSub(EndPointerCasted, StartPointerCasted);
                builder.CreateCall(StoreFunc, {StartPointerCasted, Range});
            } else {
                IRBuilder<> builder(MRS->InsertionPt);
                Value *PointerCasted = builder.CreatePtrToInt(MRS->Pointer, Int64Ty);
                Value *Range = builder.CreateIntCast(MRS->LengthOrStartIndex, Int64Ty, true);
                builder.CreateCall(StoreFunc, {PointerCasted, Range});
            }
            Parent.updateStatistics("Transform");
        }

        for (auto Entry : ProtectedExternalCalls) {
            auto Call = Entry.second;
            IRBuilder<> builder(Call);
            Value *PointerCasted = builder.CreatePtrToInt(Entry.first, Int64Ty);
            Value *Range = nullptr;
            auto funcName = Call->getCalledFunction()->getName();
            if (funcName.equals("read")) {
                Range = builder.CreateIntCast(Call->getArgOperand(2), Int64Ty, true);
            } else if (funcName.equals("fread")) {
                auto LHS = builder.CreateIntCast(Call->getArgOperand(1), Int64Ty, true);
                auto RHS = builder.CreateIntCast(Call->getArgOperand(2), Int64Ty, true);
                Range = builder.CreateMul(LHS, RHS);
            } else if (funcName.equals("MPI_Irecv") || funcName.equals("MPI_Recv")) {
                Type *DataTypeTy = Call->getArgOperand(2)->getType();
                FunctionType *GetTypeSizeTy =
                        FunctionType::get(Int32Ty, {DataTypeTy, Int32PtrTy}, false);
                FunctionCallee GetTypeFunc =
                        Parent.M.getOrInsertFunction("MPI_Type_size", GetTypeSizeTy);
                builder.CreateCall(GetTypeFunc, {Call->getArgOperand(2), TmpAlloca});
                auto LHS = builder.CreateIntCast(Call->getArgOperand(1), Int64Ty, true);
                auto RHS = builder.CreateIntCast(builder.CreateLoad(TmpAlloca), Int64Ty, true);
                Range = builder.CreateMul(LHS, RHS);
            } else {
                assert(0 && "unreachable");
            }
            builder.CreateCall(StoreFunc, {PointerCasted, Range});
            Parent.updateStatistics("Transform");
        }
    }
}