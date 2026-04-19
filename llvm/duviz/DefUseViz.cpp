#include "DefUseViz.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#include <cstddef>
#include <map>
#include <string>

#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>


namespace {
    constexpr char LOGGER_FUNC_NAME[] = "log_call";
    
    
    bool introduceLogger(Module &M) {
        Function *LoggerFunc = M.getFunction(LOGGER_FUNC_NAME);
        if (LoggerFunc == nullptr) {
            LLVMContext &Context = M.getContext();
            
            // build function prototype
            Type *RetType = Type::getVoidTy(Context);
            Type *FilenameType = PointerType::getUnqual(Context);
            Type *NodeIdType = Type::getInt64Ty(Context);
            Type *ValueType = Type::getInt32Ty(Context);

            Type* FuncArgTypes[] {FilenameType, NodeIdType, ValueType};
            FunctionType *Proto = FunctionType::get(RetType, FuncArgTypes, false);

            // add function to module
            M.getOrInsertFunction(LOGGER_FUNC_NAME, Proto);
            return true;
        }
        return false;
    }
    
    
    void instrumentInstruction(Module &Module, IRBuilder<> &Builder, Instruction &Instr, Agnode_t *Node, Value *OutFilename) {
        Function *LoggerFunc = Module.getFunction(LOGGER_FUNC_NAME);
        
        // create node id arg
        IDTYPE NodeId = AGID(Node);
        Value * NodeIdArg = ConstantInt::get(LoggerFunc->getArg(1)->getType(), NodeId);

        // create instruction value casted to int32 arg
        Type *SrcType = Instr.getType();
        Type *DestType = LoggerFunc->getArg(2)->getType();
        Value *CastedVal;

        if (SrcType->isPointerTy()) {
            CastedVal = Builder.CreatePtrToInt(&Instr, DestType);
        } else if (SrcType->isFloatingPointTy()) {
            CastedVal = Builder.CreateFPToSI(&Instr, DestType);
        } else if (SrcType->isIntegerTy() || SrcType->isByteTy()) {
            CastedVal = Builder.CreateSExtOrTrunc(&Instr, DestType);
        } else if (SrcType->getPrimitiveSizeInBits() == DestType->getPrimitiveSizeInBits()) {
            CastedVal = Builder.CreateBitCast(&Instr, DestType);
        } else {
            CastedVal = ConstantInt::get(DestType, 0);
        }

        // insert call to logger
        Value *Args[] = {OutFilename, NodeIdArg, CastedVal};
        Builder.CreateCall(LoggerFunc, Args);
    }
    
    
    bool processFunction(Module &M, Function &F, IRBuilder<> &Builder, Agraph_t *ModuleGraph, std::map<Value *, Agnode_t *> &Nodes, Value *OutFilenameVal) {
        bool Modified{};

        Agraph_t *FunctionSubgraph = agsubg(ModuleGraph, NULL, true);
        agsafeset(FunctionSubgraph, "label", F.getName().data(), "");


        ReversePostOrderTraversal<Function *> RPOT(&F);

        // build separately all basic blocks subgraphs
        for (auto *BB: RPOT) {
            Agraph_t *BasicBlockSubgraph = agsubg(FunctionSubgraph, NULL, true);
            agsafeset(BasicBlockSubgraph, "label", BB->getName().str().c_str(), "");
            Agnode_t *PrevNode{nullptr};
            
            
            // add all instructions in basic block to subgraph
            for (auto &Instr: *BB) {
                // add current instruction to BasicBlockSubgraph
                Agnode_t *CurNode = agnode(BasicBlockSubgraph, NULL, true);
                
                std::string StrBuf = "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\"><TR><TD>";
                llvm::raw_string_ostream Operation(StrBuf);
                Operation << Instr;
                Operation << "</TD><TD>VALUE=0</TD></TR></TABLE>";
                
                char* NodeLabel = agstrdup_html(BasicBlockSubgraph, StrBuf.c_str());
                agset(CurNode, "label", NodeLabel);
                agstrfree(BasicBlockSubgraph, NodeLabel, true);
                
                agsafeset(CurNode, "shape", "plaintext", "");
                
                // add parent edge
                if (PrevNode != nullptr) {
                    agedge(BasicBlockSubgraph, PrevNode, CurNode, NULL, true);
                }
                
                Nodes[&Instr] = CurNode;
                PrevNode = CurNode;
                
                // add use edges
                for (auto &Op: Instr.operands()) {
                    auto NodeIter = Nodes.find(Op);
                    if (NodeIter != Nodes.end()) {
                        Agedge_t *Edge = agedge(BasicBlockSubgraph, CurNode, NodeIter->second, NULL, 1);
                        agsafeset(Edge, "color", "cyan", "");
                    }
                }
            }
            
            
            // add value logging, except for branching instructions
            Builder.SetInsertPoint(BB->getFirstNonPHIIt());
            auto InstrIt = BB->begin();
            
            // "nice" basic blocks start with phis
            while (InstrIt->getOpcode() == Instruction::PHI) {
                Instruction &Instr = *InstrIt;
                ++InstrIt; // next instruction
                
                instrumentInstruction(M, Builder, Instr, Nodes[&Instr], OutFilenameVal);
                Modified = true;
            }
            
            InstrIt = Builder.GetInsertPoint(); // skip phi logging to first non-phi
            
            // "nice" basic blocks end with a single terminator 
            while (!InstrIt->isTerminator()) {
                Instruction &Instr = *InstrIt;
                Builder.SetInsertPoint(++Builder.GetInsertPoint()); // move builder to insert after current instruction
                
                instrumentInstruction(M, Builder, Instr, Nodes[&Instr], OutFilenameVal);
                Modified = true;
                
                InstrIt = Builder.GetInsertPoint(); // advance the invalidated it
            }
        }

        // connect basic block subgraphs by adding branch edges
        for (auto &BB: F) {
            Instruction *Term = BB.getTerminator();
            
            if (Term->getOpcode() == Instruction::CondBr) {
                auto It = Term->successors().begin();
                
                Agedge_t *EdgeIfTrue = agedge(FunctionSubgraph, Nodes[Term], Nodes[&It->front()], NULL, 1);
                agsafeset(EdgeIfTrue, "color", "green", "");
                It++;
                
                Agedge_t *EdgeIfFalse = agedge(FunctionSubgraph, Nodes[Term], Nodes[&It->front()], NULL, 1);
                agsafeset(EdgeIfFalse, "color", "red", "");
            } else {
                for (auto *Succ: Term->successors()) {
                    Agedge_t *Edge = agedge(FunctionSubgraph, Nodes[Term], Nodes[&Succ->front()], NULL, 1);
                    agsafeset(Edge, "color", "gray", "");
                }
            }
        }

        return Modified;
    }
} // namespace


PreservedAnalyses DefUseViz::run(Module &M,
                            ModuleAnalysisManager &MAM) {
    
    GVC_t *Gvc = gvContext();
    Agraph_t *Graph = agopen(NULL, Agdirected, NULL);
    
    Agraph_t *ModuleSubgraph = agsubg(Graph, NULL, true);
    agsafeset(ModuleSubgraph, "cluster", "true", "true");
    agsafeset(ModuleSubgraph, "label", M.getName().data(), "");
    agsafeset(ModuleSubgraph, "newrank", "", "true");
    
    std::string OutFilename = (M.getName() + ".dot").str();
    std::map<Value *, Agnode_t *> Nodes;
    
    bool Modified{introduceLogger(M)};
    IRBuilder<> Builder(M.getContext());
    Value *OutFilenameVal = Builder.CreateGlobalString(OutFilename, "" , 0, &M);
    
    for (auto &F: M) {
        if (F.empty())
            continue;
        
        Modified |= processFunction(M, F, Builder, ModuleSubgraph, Nodes, OutFilenameVal);
    }

    // render module def-use graph to png
    FILE *OutFile = fopen(OutFilename.c_str(), "w");
    if (OutFile) {
        gvLayout(Gvc, Graph, "dot");
        gvRender(Gvc, Graph, "dot", OutFile);
        gvFreeLayout(Gvc, Graph);
    }
    fclose(OutFile);
    agclose(Graph);
    gvFreeContext(Gvc);

    return Modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
}


extern "C" llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DefUseVizPass", "1.0",
          [](llvm::PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](llvm::StringRef Name, llvm::ModulePassManager &MPM,
                   llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "duviz") {
                    MPM.addPass(DefUseViz());
                    return true;
                  }
                  return false;
                });
          }};
}