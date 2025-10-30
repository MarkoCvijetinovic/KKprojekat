#include "llvm/Pass.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"

#include <algorithm>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {

struct MyInstCombine : public FunctionPass {

  static char ID;
  MyInstCombine() : FunctionPass(ID) {}

  static bool replaceInst(Instruction &I, Value *V) 
  {
    if (V == &I) return false; 

    I.replaceAllUsesWith(V);   
    I.eraseFromParent();       

    return true;               
  }

  static bool moveConstToRHS(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO) return false;
    
    unsigned Op = BO->getOpcode();
    if (!Instruction::isCommutative(Op)) return false;

    Value *L = BO->getOperand(0);
    Value *R = BO->getOperand(1);

    if (isa<Constant>(L) && !isa<Constant>(R)) {
      BO->setOperand(0, R);
      BO->setOperand(1, L);
      return true;
    }

    return false;
  }

  static bool orderBitwiseWithConst(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO) return false;

    unsigned Op = BO->getOpcode();
    if (Op != Instruction::And && Op != Instruction::Or && Op != Instruction::Xor)
      return false;

    bool Changed = moveConstToRHS(I);

    Value *L = BO->getOperand(0);
    Value *R = BO->getOperand(1);

    auto IsBitOrShift = [](Value *V) -> bool 
    {
      if (auto *B = dyn_cast<BinaryOperator>(V)) {
        unsigned O = B->getOpcode();
        return O == Instruction::And || O == Instruction::Or || O == Instruction::Xor ||
               O == Instruction::Shl || O == Instruction::LShr || O == Instruction::AShr;
      }
      return false;
    };

    auto BitRank = [](unsigned Opcode) -> int 
    {
      if (Opcode == Instruction::Shl || Opcode == Instruction::LShr || Opcode == Instruction::AShr) return 0;
      if (Opcode == Instruction::Or)  return 1;
      if (Opcode == Instruction::And) return 2;
      if (Opcode == Instruction::Xor) return 3;
      return 4; 
    };

    if (Instruction::isCommutative(Op)) 
    {
      int Lrank = IsBitOrShift(L) ? BitRank(cast<BinaryOperator>(L)->getOpcode()) : -1;
      int Rrank = IsBitOrShift(R) ? BitRank(cast<BinaryOperator>(R)->getOpcode()) : -1;

      auto PreferLeft = [&](int Lr, int Rr) 
      {
        if (Rr == -1) return false;
        if (Lr == -1) return true;
        return Rr < Lr;
      };

      if (PreferLeft(Lrank, Rrank)) 
      {
        BO->setOperand(0, R);
        BO->setOperand(1, L);
        Changed = true;
      }
    }

    return Changed;
  }

  static bool foldSimpleArith(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO) return false;
    Value *X = nullptr;

    if (match(BO, m_Add(m_Value(X), m_Zero())) || match(BO, m_Add(m_Zero(), m_Value(X))))
      return replaceInst(I, X);

    if (match(BO, m_Sub(m_Value(X), m_Zero())))
      return replaceInst(I, X);

    if (match(BO, m_Mul(m_Value(X), m_One())) || match(BO, m_Mul(m_One(), m_Value(X))))
      return replaceInst(I, X);

    if (match(BO, m_Mul(m_Value(X), m_Zero())) || match(BO, m_Mul(m_Zero(), m_Value(X))))
      return replaceInst(I, Constant::getNullValue(I.getType()));

    if (match(BO, m_And(m_Value(X), m_AllOnes())) || match(BO, m_And(m_AllOnes(), m_Value(X))))
      return replaceInst(I, X);

    if (match(BO, m_Or(m_Value(X), m_Zero())) || match(BO, m_Or(m_Zero(), m_Value(X))))
      return replaceInst(I, X);

    if (match(BO, m_Xor(m_Value(X), m_Zero())) || match(BO, m_Xor(m_Zero(), m_Value(X))))
      return replaceInst(I, X);

    if (match(BO, m_Xor(m_Value(X), m_Specific(X))) || match(BO, m_Sub(m_Value(X), m_Specific(X))))
      return replaceInst(I, Constant::getNullValue(I.getType()));

    return false;
  }

  static bool foldLogicBasics(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO) return false;
    Value *X = nullptr;

    if (match(BO, m_And(m_Value(X), m_Specific(X))) || match(BO, m_Or(m_Value(X), m_Specific(X))))
      return replaceInst(I, X);

    if (match(BO, m_And(m_Value(X), m_Zero())) || match(BO, m_And(m_Zero(), m_Value(X))))
      return replaceInst(I, Constant::getNullValue(I.getType()));

    if (match(BO, m_Or(m_Value(X), m_AllOnes())) || match(BO, m_Or(m_AllOnes(), m_Value(X))))
      return replaceInst(I, Constant::getAllOnesValue(I.getType()));

    return false;
  }

  static bool foldNegations(Instruction &I) 
  {
    if (I.getOpcode() == Instruction::Sub) 
    {
      Value *X = nullptr;
      if (match(&I, m_Sub(m_Zero(), m_Value(X)))) 
      {
        IRBuilder<> B(&I);
        return replaceInst(I, B.CreateNeg(X, "neg"));
      }
    }

    if (I.getOpcode() == Instruction::Add) 
    {
      Value *X = nullptr, *Y = nullptr;
      if (match(&I, m_Add(m_Value(X), m_Neg(m_Value(Y))))) 
      {
        IRBuilder<> B(&I);
        return replaceInst(I, B.CreateSub(X, Y, "add.sub"));
      }
    }

    return false;
  }

  static bool foldFAddZero(Instruction &I) 
  {
    if (I.getOpcode() != Instruction::FAdd) return false;

    Value *X = nullptr;

    if (match(&I, m_FAdd(m_Value(X), m_AnyZeroFP())) || match(&I, m_FAdd(m_AnyZeroFP(), m_Value(X))))
      return replaceInst(I, X);

    return false;
  }

  static bool foldAddXX(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO || BO->getOpcode() != Instruction::Add) return false;

    Value *L = BO->getOperand(0);
    Value *R = BO->getOperand(1);
    
    if (L != R) return false;

    IRBuilder<> B(&I);
    Value *One = ConstantInt::get(L->getType(), 1);
    auto *Shl = B.CreateShl(L, One, "twox");

    if (BO->hasNoSignedWrap()) cast<BinaryOperator>(Shl)->setHasNoSignedWrap();
    if (BO->hasNoUnsignedWrap()) cast<BinaryOperator>(Shl)->setHasNoUnsignedWrap();
    return replaceInst(I, Shl);
  }

  static bool foldMulPow2ToShl(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO || BO->getOpcode() != Instruction::Mul) return false;

    Value *X = nullptr; ConstantInt *C = nullptr;
    if (!(match(BO, m_Mul(m_Value(X), m_ConstantInt(C))) ||
          match(BO, m_Mul(m_ConstantInt(C), m_Value(X)))))
      return false;

    if (!C->getValue().isPowerOf2()) return false;
    
    if (C->isOne()) return false;

    unsigned K = C->getValue().exactLogBase2();
    IRBuilder<> B(&I);
    Value *ShiftAmt = ConstantInt::get(X->getType(), K);
    auto *Shl = B.CreateShl(X, ShiftAmt, "mul2k");

    if (BO->hasNoSignedWrap()) cast<BinaryOperator>(Shl)->setHasNoSignedWrap();
    if (BO->hasNoUnsignedWrap()) cast<BinaryOperator>(Shl)->setHasNoUnsignedWrap();
    return replaceInst(I, Shl);
  }

  static bool foldDivPow2ToShr(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO) return false;
    
    unsigned Opcode = BO->getOpcode();
    if (Opcode != Instruction::UDiv && Opcode != Instruction::SDiv) return false;

    Value *X = nullptr; ConstantInt *C = nullptr;
    if (!match(BO, m_BinOp(m_Value(X), m_ConstantInt(C)))) return false;
    
    if (!C->getValue().isPowerOf2()) return false;

    unsigned K = C->getValue().exactLogBase2();
    IRBuilder<> B(&I);
    Value *ShiftAmt = ConstantInt::get(X->getType(), K);
    
    Value *Result = nullptr;
    if (Opcode == Instruction::UDiv) 
    {
      Result = B.CreateLShr(X, ShiftAmt, "udiv2k");
    } 
    else 
    {
      if (C->isNegative()) return false;
      Result = B.CreateAShr(X, ShiftAmt, "sdiv2k");
    }
    
    return replaceInst(I, Result);
  }

  static bool foldConstOp(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO) return false;
    auto *C1 = dyn_cast<Constant>(BO->getOperand(0));
    auto *C2 = dyn_cast<Constant>(BO->getOperand(1));

    if (!C1 || !C2) return false;

    if (Constant *Folded = ConstantExpr::get(BO->getOpcode(), C1, C2))
      return replaceInst(I, Folded);

    return false;
  }

  static bool reassocAddConst(Instruction &I) 
  {
    auto *BO = dyn_cast<BinaryOperator>(&I);
    if (!BO || BO->getOpcode() != Instruction::Add) return false;

    Value *X = nullptr; ConstantInt *C1 = nullptr, *C2 = nullptr;

    if (match(BO, m_Add(m_Add(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2))) ||
        match(BO, m_Add(m_Add(m_ConstantInt(C1), m_Value(X)), m_ConstantInt(C2)))) {
      IRBuilder<> B(&I);
      auto *Sum = ConstantInt::get(C1->getType(), C1->getValue() + C2->getValue());
      return replaceInst(I, B.CreateAdd(X, Sum, "add.fold"));
    }

    Instruction *Inner = nullptr;
    if (match(BO, m_Add(m_Instruction(Inner), m_ConstantInt(C2)))) 
    {
      if (Inner->getOpcode() == Instruction::Add) 
      {
        Value *X0 = Inner->getOperand(0);
        if (auto *C1i = dyn_cast<ConstantInt>(Inner->getOperand(1))) 
        {
          IRBuilder<> B(&I);
          auto *Sum = ConstantInt::get(C1i->getType(), C1i->getValue() + C2->getValue());
          return replaceInst(I, B.CreateAdd(X0, Sum, "add.fold2"));
        }
      }
    }

    return false;
  }

  static bool foldRelICmpToEqNe(Instruction &I) 
  {
    auto *Cmp = dyn_cast<ICmpInst>(&I);
    if (!Cmp) return false;
    Value *X = Cmp->getOperand(0), *Y = Cmp->getOperand(1);
    auto Pred = Cmp->getPredicate();

    ConstantInt *C = dyn_cast<ConstantInt>(Y);
    if (!C) return false;

    IRBuilder<> B(&I);
    Type *Ty = X->getType();
    if (!Ty->isIntegerTy()) return false;

    // Unsigned comparisons with 0 and 1
    if (Pred == ICmpInst::ICMP_ULT && C->isOne())
      return replaceInst(I, B.CreateICmpEQ(X, ConstantInt::get(Ty, 0)));

    if (Pred == ICmpInst::ICMP_UGE && C->isOne())
      return replaceInst(I, B.CreateICmpNE(X, ConstantInt::get(Ty, 0)));

    if (Pred == ICmpInst::ICMP_ULE && C->isZero())
      return replaceInst(I, B.CreateICmpEQ(X, ConstantInt::get(Ty, 0)));

    if (Pred == ICmpInst::ICMP_UGT && C->isZero())
      return replaceInst(I, B.CreateICmpNE(X, ConstantInt::get(Ty, 0)));

    return false;
  }

  static bool foldICmpOnBool(Instruction &I) 
  {
    auto *Cmp = dyn_cast<ICmpInst>(&I);
    if (!Cmp) return false;
    Value *A = Cmp->getOperand(0), *B = Cmp->getOperand(1);

    if (!A->getType()->isIntegerTy(1) || !B->getType()->isIntegerTy(1))
      return false;

    IRBuilder<> Builder(&I);
    if (Cmp->getPredicate() == ICmpInst::ICMP_EQ) 
    {
      Value *X = Builder.CreateXor(A, B, "xeq");
      Value *NotX = Builder.CreateXor(X, ConstantInt::getTrue(I.getContext()), "not");
      return replaceInst(I, NotX);
    }

    if (Cmp->getPredicate() == ICmpInst::ICMP_NE) 
    {
      Value *X = Builder.CreateXor(A, B, "xne");
      return replaceInst(I, X);
    }

    return false;
  }

  static bool applyOptimizations(Instruction &I) 
  {

    auto Opts = {
        moveConstToRHS,
        orderBitwiseWithConst,
        foldRelICmpToEqNe,
        foldICmpOnBool,
        foldAddXX,
        foldMulPow2ToShl,
        foldDivPow2ToShr,
        foldSimpleArith,
        foldLogicBasics,
        foldConstOp,
        reassocAddConst,
        foldNegations,
        foldFAddZero              
    };

    for(auto &Opt : Opts)
    {
      if (Opt(I))
        return true;
    }

    return false;
  }

  bool runOnFunction(Function &F) override
  {
    bool Changed = false;
    bool LocalChanged = true;
    unsigned IterationCount = 0;
    const unsigned MaxIterations = 100; 

    while (LocalChanged && IterationCount < MaxIterations) 
    {
      LocalChanged = false;
      IterationCount++;
      
      SmallVector<Instruction*> Worklist;
      for (BasicBlock &BB : F) 
      {
        for (Instruction &I : BB) 
        {
          Worklist.push_back(&I);
        }
      }

      for (unsigned J = 0; J < Worklist.size(); ++J) 
      {
        Instruction *I = Worklist[J];
        
        if (!I || !I->getParent()) continue;

        // Store users before optimization (they might be needed for worklist)
        SmallVector<Instruction*> Users;
        for (User *U : I->users()) 
        {
          if (auto *UserInst = dyn_cast<Instruction>(U)) 
          {
            Users.push_back(UserInst);
          }
        }

        bool InstChanged = applyOptimizations(*I);
        LocalChanged |= InstChanged;
        
        // If we modified this instruction, add its users back to the worklist
        // This ensures we catch new optimization opportunities that emerge
        if (InstChanged) 
        {
          for (Instruction *UserInst : Users) 
          {
            if (UserInst->getParent()) 
            { // Check user still exists
              // Add to worklist if not already there
              if (std::find(Worklist.begin() + J + 1, Worklist.end(), UserInst) == Worklist.end()) 
              {
                Worklist.push_back(UserInst);
              }
            }
          }
        }
      }
      
      Changed |= LocalChanged;
    }
    
    return Changed;
  }
};

} // namespace


char MyInstCombine::ID = 0;
static RegisterPass<MyInstCombine> X("my-inst-combine", "A simplified version of InstCombine pass");



