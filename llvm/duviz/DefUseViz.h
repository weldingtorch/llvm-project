#include "llvm/IR/PassManager.h"

namespace llvm {

class DefUseViz : public PassInfoMixin<DefUseViz> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  
  static bool isRequired() { return true; }
};

} // namespace llvm
