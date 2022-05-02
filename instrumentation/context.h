//
// Created by Feng Ren on 2021/1/20.
//

#ifndef LIBCRPM_CONTEXT_H
#define LIBCRPM_CONTEXT_H

#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/MemoryDependenceAnalysis.h>

using namespace llvm;
using namespace std;

enum {
    POINTER_VOLATILE, POINTER_PERSISTENT, POINTER_UNKNOWN
};

class FunctionContext;

class ModuleContext;

struct IndVarContext;

class LoopContext {
public:
    LoopContext(Loop &L, LoopContext *ParentLoop_);

    LoopContext *ParentLoop;
    vector<BasicBlock *> BasicBlocks;
    BasicBlock *PreHeader, *Header, *Latch;
    IndVarContext *IndVar;
    SmallVector<BasicBlock *, 8> ExitingBlocks;
};

struct IndVarContext {
    IndVarContext(Value *IndVar_, Value *InitialVal_, Value *FinalVal_) :
            IndVar(IndVar_), InitialVal(InitialVal_), FinalVal(FinalVal_) {}

    Value *IndVar;
    Value *InitialVal;
    Value *FinalVal;
};

struct MemoryStoreHook {
    MemoryStoreHook(Value *Pointer_, Instruction *InsertionPt_, bool CopyGEP_ = false) :
            Pointer(Pointer_), InsertionPt(InsertionPt_), CopyGEP(CopyGEP_) {}

    Instruction *InsertionPt;
    Value *Pointer;
    bool CopyGEP;
};

struct MemoryBulkStoreHook {
    MemoryBulkStoreHook(Value *Pointer_,
                        Instruction *InsertionPt_,
                        Value *LengthOrStartIndex_,
                        bool UseStartStopIndex_ = false,
                        Value *StopIndex_ = nullptr,
                        bool CopyLoad_ = false) :
            Pointer(Pointer_),
            InsertionPt(InsertionPt_),
            LengthOrStartIndex(LengthOrStartIndex_),
            StopIndex(StopIndex_),
            UseStartStopIndex(UseStartStopIndex_),
            CopyLoad(CopyLoad_) {}

    Instruction *InsertionPt;
    Value *Pointer;
    Value *LengthOrStartIndex;
    Value *StopIndex;
    bool UseStartStopIndex;
    bool CopyLoad;
};

class FunctionContext {
public:
    FunctionContext(Function &F_, ModuleContext &Parent_);

    void performAllOptimizations();

    void transform();

    // -------------------------------------
    void findLoops();

    void findInductionVariable(LoopContext *LC);

    void findManualInstrumentCalls();

    void findCheckpointCalls();

    void findReturnValues();

    void findMemoryStore();

    void findIncomingArguments();

    //  -------------------------------------

    bool performRedundantStoreElimination();

    bool performAnnotatedUninstrumentation();

    bool performManualInstrumentElimination();

    bool performStructStoreCombination();

    bool performTransientElimination();

    bool performLoopHoisting();

    bool performLoopHoistingForBulk();

    bool performLoopAggregation();

    bool performInsertionPtPromotion();

    //  -------------------------------------
    int getMemoryPointerState(Value *Source);

    Value *discoverDependency(std::set<Value *> &SeenValues, Value *Source);

    bool allowHoistingGetElemPtr(LoopContext *LC, GetElementPtrInst *GEPInst);

    bool allowAggregatingGetElemPtr(LoopContext *LC, GetElementPtrInst *GEPInst);

    bool isPointerOutOfLoop(LoopContext *LC, Value *Pointer);

    bool isBasicBlockOutOfLoop(LoopContext *LC, BasicBlock *BB);

    Value *getLoadPointer(Value *Pointer, bool null_if_failed);

    Value *getStorePointer(Instruction *Inst);

    Value *getUnaryPointer(Value *Pointer, bool null_if_failed);

    Value *getUncastPointer(Value *Pointer);

    bool hasPotentialStore(LoadInst *TargetInst, BasicBlock *BB);

    bool hasPotentialStore(LoadInst *TargetInst);

    bool maySplitByCheckpoint(Instruction *Start, Instruction *Stop);

    Function &F;
    ModuleContext &Parent;

    vector<LoopContext *> SortedLoops;
    vector<Value *> ReturnValues;                   // all possible return values
    vector<CallInst *> ManualInstrumentCalls;       // call crpm_annotate()
    set<CallBase *> CheckpointCalls;                // potentially call crpm_checkpoint()

    set<CallBase *> IncomingCalls;
    multimap<Argument *, Value *> IncomingArguments;

    set<MemoryStoreHook *> MemoryStore;
    set<MemoryBulkStoreHook *> MemoryBulkStore;
    map<Value *, CallBase *> ProtectedExternalCalls;

    DominatorTree *DT;
    AAResults *AA;
};

class ModuleContext {
public:
    ModuleContext(Module &M_, ModulePass *Pass_);

    ~ModuleContext();

    void analysis();

    void transform();

    void findUninstrumentAnnotations();

    void findFunctions();

    void checkCheckpointCalls();

    void transformMainFunction();

    void updateStatistics(const string &field, int count = 1);

    void printStatistics();

    FunctionContext *getFunction(Function *F);

    Module &M;
    ModulePass *Pass;
    vector<FunctionContext *> Functions;
    map<string, int> Statistics;

    set<GlobalVariable *> UninstrumentAnnotations;
    set<Function *> UninstrumentFunctions;
    set<Function *> StaticFunctions;

    bool EnableMTUnsafeOptimization;
};

#endif //LIBCRPM_CONTEXT_H
