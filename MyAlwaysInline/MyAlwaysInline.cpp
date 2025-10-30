#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Scalar.h"

using namespace llvm;

namespace {
struct MyAlwaysInline : public ModulePass {
  static char ID;
  MyAlwaysInline() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {
    bool Changed = false;
    SmallVector<Function*, 16> ToErase;

      for (Function &F : M) {
        // Skip declarations (external functions) – they can’t contain call sites we can inline.
        if (F.isDeclaration()) continue;

        // We mutate while iterating, so use a manual iterator pattern over instructions.
        for (BasicBlock &BB : F) {
          for (auto It = BB.begin(); It != BB.end(); ) {
            Instruction &I = *It++;
            auto *CB = dyn_cast<CallBase>(&I);
            if (!CB) continue;

            Function *Callee = CB->getCalledFunction();
            if (!Callee) continue;
            if (Callee->isDeclaration()) continue;

            // Only inline if the callee explicitly demands it.
            if (!Callee->hasFnAttribute(Attribute::AlwaysInline)) continue;

            // Use NoRecurse attribute from function-attr pass
            // to avoid recursive functions which create infinite code
            if (!Callee->hasFnAttribute(llvm::Attribute::NoRecurse)) continue;

            // Call built-in inlining function
            InlineFunctionInfo IFI;
            auto Ret = InlineFunction(*CB, IFI);
            if (Ret.isSuccess()) Changed = true;

            // Add functions that are no longer used to vector
            if (Callee->use_empty() && !Callee->isDeclaration() &&
                (Callee->hasLocalLinkage() || Callee->hasLinkOnceODRLinkage()))
                    ToErase.push_back(Callee);
          }
        }
      }

      // Erase functions that are no longer used
      for (Function *Fn : ToErase)
        if (Fn && Fn->use_empty()){
          Fn->eraseFromParent();
        }
      // Erase attributes to make a similar output to the original always-inline
      // Inlining might invalidate them anyway
      if (Changed)
      	for (Function &F : M)
          F.setAttributes(AttributeList());
      return Changed;
  }
};
};

char MyAlwaysInline::ID = 0;
static RegisterPass<MyAlwaysInline> X("my-always-inline",
                                      "A pass that (almost) always inlines labeled functions", false, false);