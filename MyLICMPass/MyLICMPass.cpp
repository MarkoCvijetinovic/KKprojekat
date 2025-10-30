#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopUnrollAnalyzer.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopPeel.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/SizeOpts.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <algorithm>
#include <iterator>
#include <llvm/Analysis/BranchProbabilityInfo.h>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"

#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>

#define OPTLOADSTORE true
#define CONSERVATIVE false

using namespace llvm;

namespace {
struct MyLICMPass : public LoopPass {
  static char ID; // Pass identification, replacement for typeid

  MyLICMPass() : LoopPass(ID){}

  std::unordered_map<Value*, Value*> MappedVars; //map of vars for ssa
  BasicBlock* NewPreheader;  //newly created preheader
  BasicBlock* NewFirstBlock; //modified landing pad if
  std::unordered_map<BasicBlock*, std::set<BasicBlock*>> Dominators; //dominators map for da
  std::unordered_map<BasicBlock*, std::set<BasicBlock*>> calculateDominators(std::vector<BasicBlock*> Blocks) {

    std::unordered_map<BasicBlock*, std::set<BasicBlock*>> Dominators;

    // Make cfg predecessors map
    std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> Predecessors;
    for (auto *Block : Blocks) {
      for (BasicBlock *Successor : successors(Block)) {
        Predecessors[Successor].push_back(Block);
      }
    }

    //init for algorithm, all blocks dominate all blocks
    for (BasicBlock *BB : Blocks) {
      Dominators[BB] = std::set<BasicBlock*>();
      for (BasicBlock *BBB : Blocks) {
        Dominators[BB].insert(BBB);
      }
    }

    //may not be block[0]
    BasicBlock* StartBB=NewFirstBlock;
    Dominators[StartBB] = {StartBB}; //dominated by itself

    bool Done=false;
    while(!Done){ //iterative calculate dominators as intersection of dominators of predecessors
      Done=true;
      for (auto&BB:Blocks){
        auto OldDoms=Dominators[BB]; //for change check

        Dominators[BB]=std::set<BasicBlock*>();
        Dominators[BB].insert(BB); //block dominates itself

        std::set<BasicBlock*> Intersection{}; //intersect all predecessor dominators
        bool First=true;
        for (auto *PBB : Predecessors[BB]) {
          if (First) {
            Intersection.insert(Dominators[PBB].begin(), Dominators[PBB].end());
            First = false;
          }
          else {
            std::set<BasicBlock*> Tmp;
            std::set_intersection(Intersection.begin(),Intersection.end(),Dominators[PBB].begin(),Dominators[PBB].end(), std::inserter(Tmp, Tmp.begin()));
            Intersection.swap(Tmp);
          }
        }
        Dominators[BB].insert(Intersection.begin(), Intersection.end());

        if (Dominators[BB] != OldDoms) {
          Done = false;
        }
      }
    }
    return Dominators;
  }

  // creates a preheader for hoisting instructions if one is not yet available and surrounds it with landing pad if for loops that are not executed even once
  BasicBlock* makeNewPreheader(Loop * Loop) {


    //insert my preheader before first loop block (header)
    BasicBlock *FirstBlock = Loop->getHeader();
    BasicBlock *NewBasicBlock = BasicBlock::Create(FirstBlock->getContext(), "hoist_point", FirstBlock->getParent(),FirstBlock);

    IRBuilder<> Builder(NewBasicBlock);
    Builder.CreateBr(FirstBlock); //connect to old loop blocks

    for (BasicBlock *Pred : predecessors(FirstBlock)) { //reroute predecessors to new block
      if (!Loop->contains(Pred) && Pred!=NewBasicBlock) {
        Instruction *Term = Pred->getTerminator();
        for (unsigned i = 0; i < Term->getNumSuccessors(); i++) {
          if (Term->getSuccessor(i) == FirstBlock)
            Term->setSuccessor(i, NewBasicBlock);
        }
      }
    }



    BasicBlock *Header = FirstBlock;
    BasicBlock *preheader = NewBasicBlock;

    BasicBlock *Last = Loop->getLoopLatch();

    std::vector<BasicBlock*> LoopBlocks=Loop->getBlocksVector();

    BasicBlock *HeaderCopy = BasicBlock::Create(Header->getContext(), "if_guard",Header->getParent(),preheader);

    std::unordered_map<Value *, Value *> Mapping;
    IRBuilder<> CopyBuilder(HeaderCopy->getContext());

    CopyBuilder.SetInsertPoint(HeaderCopy);

    //coping instructions making new header
    for (Instruction &I : *Header) {
      Instruction *Clone = I.clone();
      CopyBuilder.Insert(Clone);
      Mapping[&I] = Clone;
      for (size_t i = 0; i < Clone->getNumOperands(); i++) {
        if (Mapping.find(Clone->getOperand(i)) != Mapping.end()) { //ssa map
          Clone->setOperand(i, Mapping[Clone->getOperand(i)]);
        }
      }
    }

    //connect to old blocks, make entrance of loop, reroute predcs
    HeaderCopy->getTerminator()->setSuccessor(0, preheader);

    for (BasicBlock *Pred : predecessors(preheader)) {
      if (!Loop->contains(Pred)&&Pred!=NewBasicBlock&&Pred!=HeaderCopy) {
        Instruction *Term = Pred->getTerminator();
        for (unsigned i = 0; i < Term->getNumSuccessors(); i++) {
          if (Term->getSuccessor(i) == preheader)
            Term->setSuccessor(i, HeaderCopy);
        }
      }
    }
    NewFirstBlock=HeaderCopy;
    NewPreheader=NewBasicBlock;
    return HeaderCopy;
  }



  static std::unordered_map<Value*, Value*>  mapVariables(Loop *L)
  {
    std::unordered_map<Value*, Value*> VariablesMap;
    Function *F = L->getHeader()->getParent();
    for (BasicBlock &BB : *F) {
      for (Instruction &I : BB) {
        if (isa<LoadInst>(&I)) {
          VariablesMap[&I] = I.getOperand(0);
        }
      }
    }
    return VariablesMap;
  }

  //check if instruction dominates all loop exits, for safety of hoisting (when loop is not executed)
  bool dominatesExits(Instruction* I,Loop*L) {
    SmallVector<BasicBlock*, 8> ExitBlocks;
    std::set<BasicBlock*> Dom;
    L->getExitBlocks(ExitBlocks);
    for (BasicBlock *Exit : ExitBlocks) {
      Dom=Dominators[Exit];
      if (Dom.find(I->getParent())==Dom.end()){
        return false;
      }
    }
    return true;
  }

  //check if instruction dominates all its uses inside the loop, for safety of hoisting
  bool dominatesUses(Instruction* I, Loop *L) {
    std::set<BasicBlock*> Dom;
    for (Value *U : getUsers(I,L)) {
      if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
        if (L->contains(UserInst)) {
          Dom=Dominators[UserInst->getParent()];
          if (Dom.find(I->getParent())==Dom.end()){
            return false;
          }
        }
      }
    }
    return true;
  }

  //check if instruction is loop invariant and can be hoisted

  bool isInstructionInvariant(Instruction *I, Loop *L) {
    if (alwaysInvariant(I)) return true;   //some instructions are always invariant some are unsafe
    if (neverInvariant(I)) return false;
    if (!isDefinedInsideLoop(I,L)) return false;
    if (!maybeInvariant(I)) return false; //conservative

    if (specialInst(I)) { //load store and pointers require special care
      return specialCheck(I,L);
    }

    //else, check if operands are invariant, for chained hoisting algorithm has to be run multiple times to hoist operands first
    for (size_t i = 0; i < I->getNumOperands(); i++) {
      Value *Operand = I->getOperand(i);
      if (isa<Constant>(Operand)) continue;
      if (Instruction *OpInst = dyn_cast<Instruction>(Operand)) {
        if (L->contains(OpInst)) {
          return false;
        }
      }
    }
    return true;
  }

  //simple recursive check for loop invariance
  static bool isLoopInvariantSimpleRecursive(Value *V, Loop *L) {
    if (isa<Constant>(V) || !isDefinedInsideLoop(V,L)) return true;
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      if (alwaysInvariant(I)) return true;
      if (neverInvariant(I) || specialInst(I)) return false;
      if (!L->contains(I)) return true;
      for (Value *Op : I->operands())
        if (!isLoopInvariantSimpleRecursive(Op, L))
          return false;
      return true;
    }
    return true;
  }


  static bool isDefinedInsideLoop(Value *V, Loop *L) {
    if (Instruction *I = dyn_cast<Instruction>(V))
      return L->contains(I);
    return false;
  }

  static bool isUsedInLoop(Instruction *I, Loop *L) { //check if instruction has users inside the loop as operand
    for (Value *U : getUsers(I,L)) {
      if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
        if (L->contains(UserInst)) {
          return true;
        }
      }
    }
    return false;
  }

  static std::vector<Value*> getUsers(Instruction* I, Loop* L) {
    std::vector<Value*> Users;
    for (BasicBlock* BB: L->blocks()){
      for (Instruction &Inst: *BB){
        for (Value *Op: Inst.operands()){
          if (Op==I){
            Users.push_back(&Inst);
          }
        }
      }
    }
    return Users;
  }

  //checks if pointer is changed in loop
  static bool isPointerInvariantSimple(Value *Ptr, Loop *L) {
    if (!isLoopInvariantSimpleRecursive(Ptr, L))
      return false;
    return true;
  }

  //checks if memory location pointed to by Ptr is changed in loop
  static bool isMemLocationInvariantSimple(Value *Ptr, Loop *L) {
    for (BasicBlock *BB : L->blocks()) {
      for (Instruction &I : *BB) {
        if (auto *AI = dyn_cast<AllocaInst>(&I)) { //multiple allocs cannot hoist
          if (AI == Ptr)
            return false;
        }
        if (auto *SI = dyn_cast<StoreInst>(&I)) {
          if (CONSERVATIVE) { //risk of aliasing conservative false for any store
            return false;
          }
          if (SI->getPointerOperand() == Ptr) //store to same location, aliasing ignored, not conservative!!
            return false;

        }
      }
    }
    return true;
  }
  static bool isMemLocationInvariantFull(Value *Ptr, Loop *L, AAResults &AA, Instruction*except=nullptr) { //with alias analysis, except instruction used for skipping load/store being checked
    for (BasicBlock *BB : L->blocks()) {
      for (Instruction &I : *BB) {
        if (except==&I) continue;
        if (auto *SI = dyn_cast<StoreInst>(&I)) {
          if (mayAlias(SI, Ptr, AA))
            return false;
        }
        if (auto *CI = dyn_cast<CallInst>(&I)) {  //calls may write to memory
          if (CI->mayWriteToMemory() && mayAlias(CI, Ptr, AA))
            return false;
        }
      }
    }
    return true;
  }

  //load is safe if ptr doesnt change, and memory location is invariant
  static bool isSafeLoadFull(LoadInst *LI, Loop *L, AAResults &AA) {
    Value *ptr = LI->getPointerOperand();
    if (isDefinedInsideLoop(ptr,L)) return false;
    if (!isPointerInvariantSimple(ptr,L)) return false;
    if (isMemLocationInvariantFull(ptr,L,AA)) return true;
    return false;
  }

  //store is safe if ptr doesnt change and no other store to same location
  bool isStoreSafe(StoreInst *S, Loop *L, AAResults &AA) {
    Value* ptr=S->getPointerOperand();
    if (!(isa<Constant>(S->getValueOperand()) || !isDefinedInsideLoop(S->getValueOperand(),L))) return false;
    if (!isPointerInvariantSimple(ptr,L)) return false;
    if (isDefinedInsideLoop(ptr,L)) return false;
    if (!isDefinedInsideLoop(ptr,L) && !isMemLocationInvariantFull(ptr,L,AA,S)) return false;
    return true;
  }

  //check for load/store/gep instructions that require special care
  bool specialCheck(Instruction*I,Loop*L) {
    AAResults& AA= getAnalysis<AAResultsWrapperPass>().getAAResults(); //for alisa check

    if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
      if (L->isLoopInvariant(GEP->getPointerOperand())) { //pointer with offset, if pointer and offset is invariant
        bool InvIndices = true;
        for (auto &Ind : GEP->indices()) {
          Value *Idx = Ind.get();
          if (!(isa<Constant>(Idx) || L->isLoopInvariant(Idx))) {
            InvIndices = false;
            break;
          }
        }
        if (InvIndices)
          return true;
      }
      return false;
    }
    if (auto *LI = dyn_cast<LoadInst>(I)) {
      return isSafeLoadFull(LI,L,AA);
    }
    if (auto *SI = dyn_cast<StoreInst>(I)) {
      return isStoreSafe(SI,L,AA);
    }
    if (auto *CI = dyn_cast<CallInst>(I)) {
      return isCallSafe(CI, L, AA);
    }
    return false;
  }


  static bool specialInst(Instruction* I) {
    if (isa<LoadInst>(I) || isa<StoreInst>(I)) return true;
    if (isa<GetElementPtrInst>(I)) return true;
    if (isa<CallInst>(I)) return true;
    return false;
  }

  //conservatively always invariant, never invariant, maybe invariant

  static bool alwaysInvariant(Value* I) {
    if (Argument *A = dyn_cast<Argument>(I)) return true;
    if (isa<GlobalVariable>(I)) return true;
    if (isa<Constant>(I)) return true;
    return false;
  }

  //maybe means candidates for invariance, but need further checks
  static bool maybeInvariant(Instruction* I) {
    if (isa<CastInst>(I)) return true;
    if (isa<BinaryOperator>(I)) return true;
    if (isa<UnaryInstruction>(I)) return true;
    if (isa<CmpInst>(I)) return true;
    if (OPTLOADSTORE) {
      if (isa<StoreInst>(I) || isa<LoadInst>(I)) return true;
      if (isa<GetElementPtrInst>(I)) return true;
      if (isa<CallInst>(I)) return true;
    }
    return false;
  }
  static bool neverInvariant(Instruction *I) {
    if (I->isTerminator() || isa<PHINode>(I)) return true; //call!!!
    if (isa<AllocaInst>(I)) return true;
    if (CallInst *CI = dyn_cast<CallInst>(I)) {
      Function *F = CI->getCalledFunction();
      if (F!=nullptr && (F->getName() == "malloc" ||  F->getName() == "free")) {
        return true;
      }
    }

    if (!OPTLOADSTORE) {
      if (isa<StoreInst>(I) || isa<LoadInst>(I)) return true;
      if (isa<GetElementPtrInst>(I)) return true;
      if (isa<CallInst>(I)) return true;
    }
    return false;
  }

  //check for calls, its safe if no side effects, args invariant, no memory access
  static bool isCallSafe(CallInst *CI, Loop *L, AAResults &AA) {
    // Reject indirect calls conservatively
    Function *F = CI->getCalledFunction();
    if (!F)
      return false;

    // Cant have side effects
    if (CI->mayHaveSideEffects())
      return false;

    // All args must be invariant
    for (const Use &U : CI->args()) {
      Value *Arg = U.get();
      if (!isLoopInvariantSimpleRecursive(Arg, L))
        return false;
    }

    // Doesn't access memory at all, safe
    if (CI->doesNotAccessMemory())
      return true;

    // Only reads memory, check that there are no writes in the loop
    if (CI->onlyReadsMemory()) {
      if (CONSERVATIVE) {
        for (BasicBlock *BB : L->blocks()) {
          for (Instruction &I : *BB) {
            if (auto *SI = dyn_cast<StoreInst>(&I))
              return false;
            if (auto *OtherCI = dyn_cast<CallInst>(&I)) {
              if (OtherCI != CI && OtherCI->mayWriteToMemory())
                return false;
            }
          }
        }
      }
      else {
        for (BasicBlock *BB : L->blocks()) {
          for (Instruction &I : *BB) {
            if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
              MemoryLocation LocS = MemoryLocation::get(SI);
              if (AA.getModRefInfo(CI, LocS) != ModRefInfo::NoModRef)
                return false;
            }
            if (CallInst *OtherCI = dyn_cast<CallInst>(&I)) {
              if (OtherCI == CI) continue;
              if (!OtherCI->mayWriteToMemory()) continue;
              if (AA.getModRefInfo(CI, OtherCI) != ModRefInfo::NoModRef)
                return false;
            }
          }
        }
      }
    }
    return false;
  }

   static bool mayAlias(Instruction *A, Value *Ptr, AAResults &AA) {
     MemoryLocation LocB(Ptr, MemoryLocation::UnknownSize);
     MemoryLocation LocA = MemoryLocation::get(A);
     AliasResult Result = AA.alias(LocA, LocB);
     return Result != AliasResult::NoAlias;
   }

  static void hoistInstruction(Instruction *I, BasicBlock *Preheader) {
    Instruction *Term = Preheader->getTerminator();
    I->moveBefore(Term);
  }

  //stores and instr not whose values are not used in loop should be sank
  static void sinkInstruction(Instruction *I, BasicBlock *ExitHeader) {
    I->moveBefore(ExitHeader->getFirstInsertionPt());
  }



  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    MappedVars = mapVariables(L);
    // if (L->getLoopPreheader() == nullptr) {
    //   Preheader = makePreheader(L);
    // } else {
    //   Preheader = L->getLoopPreheader();
    // }

    makeNewPreheader(L);

    // compute dominators over new loop blocks
    std::vector<BasicBlock*> Blocks = L->getBlocksVector();
    if (NewPreheader)
      Blocks.push_back(NewPreheader);
    if (NewFirstBlock)
      Blocks.push_back(NewFirstBlock);
    Dominators = calculateDominators(Blocks);

    std::vector<Instruction*> forHoist;
    std::vector<Instruction*> forSink;

    bool changed = true;
    SmallVector<BasicBlock*, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);
    BasicBlock *ExitHeader = ExitBlocks[0];

    while (changed) {
      changed = false;
      forHoist.clear();
      forSink.clear();
      for (BasicBlock *BB : L->blocks()) {
        for (Instruction &I : *BB) {
          if (isInstructionInvariant(&I,L)) {
            if (dominatesUses(&I,L)) {
              if (isa<StoreInst>(I) || !isUsedInLoop(&I,L)) {
                forSink.push_back(&I);
              }
              else
                forHoist.push_back(&I);
              changed = true;
            }
          }
        }
      }
      if (forHoist.size())
        errs() <<"Hoisting:" << "\n";
      for (Instruction *I : forHoist) {
        errs()<<*I<<"    "<<I->getParent()->getName()<<" → "<<NewPreheader->getName() << "\n";
        hoistInstruction(I, NewPreheader);
      }
      if (forSink.size())
        errs() <<"Sinking:" << "\n";
      for (Instruction *I : forSink) {
        errs()<<*I<<"    "<<I->getParent()->getName()<<" → "<<ExitHeader->getName() << "\n";
        sinkInstruction(I, ExitHeader);
      }
    }

    return true;
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
  }

}; // end of struct OurLoopInversionPass
}  // end of anonymous namespace

char MyLICMPass::ID = 0;
static RegisterPass<MyLICMPass> X("my-licm", "Hoisting invariant code out of loops",
                                              false /* Only looks at CFG */,
                                              false /* Analysis Pass */);
