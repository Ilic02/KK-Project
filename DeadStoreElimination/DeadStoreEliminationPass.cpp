#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace llvm;

namespace {
struct MyDeadStoreEliminationPass : public FunctionPass {
  static char ID;
  MyDeadStoreEliminationPass() : FunctionPass(ID) {}

  using PtrSet = std::unordered_set<Value *>;

  std::unordered_map<BasicBlock *, PtrSet> LiveIn;
  std::unordered_map<BasicBlock *, PtrSet> LiveOut;

  PtrSet computeLiveIn(BasicBlock &BB, const PtrSet &LiveOutSet) {
    PtrSet Live = LiveOutSet;

    for (auto It = BB.rbegin(); It != BB.rend(); ++It) {
      Instruction &Instr = *It;

      if (StoreInst *SI = dyn_cast<StoreInst>(&Instr)) {
        Live.erase(SI->getPointerOperand());
      } else if (LoadInst *LI = dyn_cast<LoadInst>(&Instr)) {
        Live.insert(LI->getPointerOperand());
      }
    }

    return Live;
  }

  void computeLiveness(Function &F) {
    for (BasicBlock &BB : F) {
      LiveIn[&BB] = PtrSet();
      LiveOut[&BB] = PtrSet();
    }

    bool Changed;
    do {
      Changed = false;
      for (BasicBlock &BB : F) {
        PtrSet NewLiveOut;
        for (BasicBlock *Succ : successors(&BB)) {
          const PtrSet &SuccLiveIn = LiveIn[Succ];
          NewLiveOut.insert(SuccLiveIn.begin(), SuccLiveIn.end());
        }

        PtrSet NewLiveIn = computeLiveIn(BB, NewLiveOut);

        if (NewLiveOut != LiveOut[&BB] || NewLiveIn != LiveIn[&BB]) {
          LiveOut[&BB] = std::move(NewLiveOut);
          LiveIn[&BB] = std::move(NewLiveIn);
          Changed = true;
        }
      }
    } while (Changed);
  }

  bool eliminateDeadStoresInBasicBlock(BasicBlock &BB) {
    bool Changed = false;
    std::vector<Instruction *> InstructionsToRemove;

    PtrSet Live = LiveOut[&BB];

    for (auto It = BB.rbegin(); It != BB.rend(); ++It) {
      Instruction &Instr = *It;

      if (StoreInst *SI = dyn_cast<StoreInst>(&Instr)) {
        Value *Ptr = SI->getPointerOperand();

        if (!Live.count(Ptr)) {
          errs() << "Dead store eliminated: " << *SI << "\n";
          InstructionsToRemove.push_back(SI);
          Changed = true;
          continue;
        }

        Live.erase(Ptr);
      } else if (LoadInst *LI = dyn_cast<LoadInst>(&Instr)) {
        Live.insert(LI->getPointerOperand());
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
      computeLiveness(F);

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
    X("my-dse", "My Dead Store Elimination Pass");