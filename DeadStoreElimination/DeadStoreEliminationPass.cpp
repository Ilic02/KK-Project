#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <unordered_set>
using namespace llvm;

namespace {
struct MyDeadStoreEliminationPass : public FunctionPass {
  static char ID;
  MyDeadStoreEliminationPass() : FunctionPass(ID) {}

  bool eliminateDeadStoresInBasicBlock(BasicBlock &BB) {
    bool Changed = false;
    std::vector<Instruction *> InstructionsToRemove;
    std::unordered_map<Value *, StoreInst *> LastStore;

    for (BasicBlock *Succ : successors(&BB)) {
      for (Instruction &I : *Succ) {
        if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
          Value *Ptr = SI->getPointerOperand();
          if (LastStore.find(Ptr) == LastStore.end()) {
            LastStore[Ptr] = SI;
          }
        }
      }
    }

    for (auto It = BB.rbegin(); It != BB.rend(); ++It) {
      Instruction &Instr = *It;

      if (StoreInst *SI = dyn_cast<StoreInst>(&Instr)) {
        Value *Ptr = SI->getPointerOperand();

        if (LastStore.find(Ptr) != LastStore.end()) {
          errs() << "Dead store eliminated:" << *SI << "\n";
          InstructionsToRemove.push_back(SI);
          Changed = true;
          continue;
        }

        LastStore[Ptr] = SI;
      } else if (LoadInst *LI = dyn_cast<LoadInst>(&Instr)) {
        Value *Ptr = LI->getPointerOperand();
        LastStore.erase(Ptr);
      }
    }

    for (Instruction *I : InstructionsToRemove) {
      I->eraseFromParent();
    }

    return Changed;
  }

  bool runOnFunction(Function &F) override {
    bool Changed = false;
    bool LocalChanged;

    do {
      LocalChanged = false;

      for (BasicBlock &BB : F) {
        if (eliminateDeadStoresInBasicBlock(BB)) {
          LocalChanged = true;
          Changed = true;
        }
      }
    } while (LocalChanged);

    return Changed;
  }
};
} // namespace

char MyDeadStoreEliminationPass::ID = 0;
static RegisterPass<MyDeadStoreEliminationPass>
    X("my-dse", "This is an example of DSE Pass");