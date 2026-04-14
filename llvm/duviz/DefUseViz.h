#include "llvm/IR/PassManager.h"

namespace llvm {

class DefUseViz : public PassInfoMixin<DefUseViz> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  
  static bool isRequired() { return true; }
};

} // namespace llvm
