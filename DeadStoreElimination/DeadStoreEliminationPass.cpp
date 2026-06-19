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

    for (auto It = BB.rbegin(); It != BB.rend(); ++It) {
      Instruction &Instr = *It;

      if (StoreInst *SI = dyn_cast<StoreInst>(&Instr)) {
        Value *Operand = SI->getOperand(1);

        if (LastStore.find(Operand) != LastStore.end()) {
          errs() << "Dead store eliminated: " << *SI << "\n";
          InstructionsToRemove.push_back(SI);
          Changed = true;
          continue;
        }

        LastStore[Operand] = SI;
      } else if (LoadInst *LI = dyn_cast<LoadInst>(&Instr)) {
        Value *Operand = LI->getOperand(0);
        LastStore.erase(Operand);
      } else if (Instr.mayReadOrWriteMemory()) {
        LastStore.clear();
      }
    }

    for (Instruction *Instr : InstructionsToRemove) {
      Instr->eraseFromParent();
    }

    return Changed;
  }

  bool runOnFunction(Function &F) override {
    bool Changed = false;

    for (BasicBlock &BB : F) {
      if (eliminateDeadStoresInBasicBlock(BB)) {
        Changed = true;
      }
    }

    return Changed;
  }
};
} // namespace

char MyDeadStoreEliminationPass::ID = 0;
static RegisterPass<MyDeadStoreEliminationPass>
    X("my-dse", "This is an example of DSE Pass");