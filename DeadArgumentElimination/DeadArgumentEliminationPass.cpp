#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <ranges>

using namespace llvm;

// Dead Argument Elimination is a *module* pass: removing an argument means
// rewriting the function signature AND fixing up every call site, which is
// only possible with visibility over the whole module. That's why we take a
// Module& here, unlike the Function& in the HelloPass example.
struct DeadArgumentEliminationPass
    : PassInfoMixin<DeadArgumentEliminationPass> {
    PreservedAnalyses run(Module &Mod, ModuleAnalysisManager &) {

        // Flag all functions with dead arguments
        std::vector<Function*> deadArgFuncs;
        for (auto &Func: Mod.getFunctionList()) {
            bool dead = false;

            for (auto& Arg : Func.args()) {
                if (Arg.use_empty())    dead = true;
            }

            if (dead) deadArgFuncs.push_back(&Func);
        }

        std::vector<CallBase*> callsToErase;
        std::vector<Function*> funcsToErase;

        // Iterate through flagged functions
        for (auto Func: deadArgFuncs) {

            // Find dead args
            std::vector<size_t>deadArgs;
            for (size_t argIndex = 0; argIndex < Func->arg_size(); argIndex++) {
                auto Arg = Func->getArg(argIndex);
                if (Arg->use_empty()) {
                    deadArgs.push_back(argIndex);
                }
            }


            // Construct a new vector of parameters(their types) = originalParams - deadParams
            // Keep the index of each surviving parameter for future use
            std::map<int, Type*> survivingParams;
            for (size_t i = 0; i < Func->getFunctionType()->params().size(); i++) {
                if (std::ranges::find(deadArgs, i) == deadArgs.end()) {
                    survivingParams[i] = Func->getArg(i)->getType();
                }
            }

            // Create a new (or reuse an already created) function type(ret type, params, variadic flag) from the FunctionType Factory
            auto values = std::views::values(survivingParams);
            std::vector<Type*> paramTypes(values.begin(), values.end());
            auto newFuncType = FunctionType::get(Func->getReturnType(), paramTypes, Func->isVarArg());

            // Create a new function, copy over most attributes from original function except for the FunctionType
            auto newFunc = Function::Create(newFuncType, Func->getLinkage(), Func->getAddressSpace(), "", &Mod);

            // Rename the new function to the old function's name (minor detail)
            newFunc->takeName(Func);

            // MOVE the body of the old function to the new function
            // (saves us from having to remap all the variables in the body)
            newFunc->splice(newFunc->begin(), Func);

            // Body is copied over, but the body still references the old arguments.
            // Replace all uses of the old surviving arguments with the new ones
            int newArgIndex = 0;
            for (auto key : std::views::keys(survivingParams)) {
                Func->getArg(key)->replaceAllUsesWith(newFunc->getArg(newArgIndex++));
            }

            // New functions are created, now wire them up.
            // First, replace all calls of the old function with the new function
            for (auto Use : Func->users()) {
                if (auto CB = dyn_cast<CallBase>(Use)) {
                    std::vector<Value*> survivingArgs;
                    for (size_t i = 0; i < CB->arg_size(); i++) {
                        if (std::ranges::find(deadArgs, i) == deadArgs.end()) {
                            survivingArgs.push_back(CB->getArgOperand(i));
                        }
                    }

                    auto newCall = CallInst::Create(newFuncType, newFunc, survivingArgs, "", CB->getIterator());
                    CB->replaceAllUsesWith(newCall);
                    newCall->takeName(CB);
                    callsToErase.push_back(CB);
                }
            }

            funcsToErase.push_back(Func);
        }

        for (auto CB : callsToErase) {
            CB->eraseFromParent();
        }

        for (auto Func : funcsToErase) {
            Func->eraseFromParent();
        }


        return PreservedAnalyses::all();
    }
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "DeadArgumentEliminationPass", "0.1",
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, ModulePassManager &MPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "dae") {
                            MPM.addPass(DeadArgumentEliminationPass());
                            return true;
                        }
                        return false;
                    });
            }};
}
