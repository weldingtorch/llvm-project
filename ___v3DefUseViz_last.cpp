#include "DefUseViz.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>

using namespace llvm;

#include <map>
#include <vector>

#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>


constexpr char LOGGER_FUNC_NAME[] = "log_call";


static bool introduceLogger(Module *Module) {
    Function *LoggerFunc = Module->getFunction(LOGGER_FUNC_NAME);
    if (LoggerFunc == nullptr) {
        LLVMContext &Context = Module->getContext();
        
        // build function prototype
        Type *RetType = Type::getVoidTy(Context);
        Type *NodeType = PointerType::getUnqual(Context);
        Type *ValueType = Type::getInt32Ty(Context);

        std::vector<Type*> FuncArgTypes{NodeType, ValueType};
        FunctionType *Proto = FunctionType::get(RetType, FuncArgTypes, false);

        // add function to module
        Module->getOrInsertFunction(LOGGER_FUNC_NAME, Proto);
        
        return true;
    }
    return false;
}

void instrumentalize(Module *Module, Instruction &Instr, IRBuilder<> &Builder, std::map<Value *, Agnode_t *> &Nodes) {
    Function *LoggerFunc = Module->getFunction(LOGGER_FUNC_NAME);
    std::vector<Value *>Args;
    
    // add instruction name to args
    char *str = agget(Nodes[&Instr], "label"); 
    Value *StrPointer = Builder.CreateGlobalString(str);
    Args.push_back(StrPointer);

    // add instruction value casted to int32 to args
    Type *SrcType = Instr.getType();
    Type *DestType = LoggerFunc->getArg(1)->getType();
    Value *CastedVal;

    if (SrcType->isPointerTy()) {
        CastedVal = Builder.CreatePtrToInt(&Instr, DestType);
    } else if (SrcType->isFloatingPointTy()) {
        CastedVal = Builder.CreateFPToSI(&Instr, DestType);
    } else if (SrcType->isIntegerTy() || SrcType->isByteTy()) {
        CastedVal = Builder.CreateSExtOrTrunc(&Instr, DestType);
    } else if (SrcType->getPrimitiveSizeInBits() == DestType->getPrimitiveSizeInBits()) {
        CastedVal = Builder.CreateBitCast(&Instr, DestType);
    } else if (SrcType->isEmptyTy()) {
        CastedVal = ConstantInt::get(DestType, 0);
    }

    Args.push_back(CastedVal);


    // insert call to logger
    Builder.CreateCall(LoggerFunc, Args);                
    
}


PreservedAnalyses DefUseViz::run(Function &F,
                              FunctionAnalysisManager &AM) {
    
    if (F.getName() == LOGGER_FUNC_NAME) return PreservedAnalyses::all();

    GVC_t *Gvc = gvContext();
    Agraph_t *Graph = agopen(NULL, Agdirected, NULL);
    Agraph_t *FunctionSubgraph = agsubg(Graph, NULL, true);
    agsafeset(FunctionSubgraph, "cluster", "true", "true");
    agsafeset(FunctionSubgraph, "label", F.getName().data(), "");
    agsafeset(FunctionSubgraph, "newrank", "", "true");

    std::map<Value *, Agnode_t *> Nodes;
    
    Module *Module = F.getParent();
    bool Modified{introduceLogger(Module)};

    ReversePostOrderTraversal<Function *> RPOT(&F);

    // build separately all basic blocks subgraphs
    for (auto *BB: RPOT) {
        // outs() << BB->getName() << '\n';

        Agnode_t *PrevNode{nullptr};
        
        Agraph_t *BasicBlockSubgraph = agsubg(FunctionSubgraph, NULL, true);
        agsafeset(BasicBlockSubgraph, "label", BB->getName().str().c_str(), "");
        

        // traverse instructions in basic block and add to subgraph
        for (auto it = BB->begin(); it != BB->end(); it++) {
            auto &Instr = *it;
            outs() << Instr << '\n';
            sleep(1);
           
            
            // add current instruction to BasicBlockSubgraph
            Agnode_t *CurNode = agnode(BasicBlockSubgraph, NULL, true);
            
            std::string StrBuf;
            llvm::raw_string_ostream Operation(StrBuf);
            Operation << Instr;
            agset(CurNode, "label", StrBuf.c_str());
            
            agsafeset(CurNode, "shape", "box", "");

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
        

        outs() << "after bbs\n";

        Instruction *LastPhi; // terrible
        // add logging after phi-nodes
        IRBuilder<> Builder(BB);
        Builder.SetInsertPoint(BB->getFirstNonPHIIt());
        for (auto &Phi: BB->phis()) {
            instrumentalize(Module, Phi, Builder, Nodes);
            Modified = true;
            LastPhi = &Phi;
        }
        
        outs() << "after phis\n";

        auto InstrIter = LastPhi->getIterator();
        while (InstrIter != Builder.GetInsertPoint()) {
            outs() << InstrIter->getName() << '\n';
            InstrIter++;
        }
        // shift builder to insert after current instruction
        InstrIter++;

        outs() << "after skip\n";
        
        
        // add value logging, except for branching instructions
        while (!(InstrIter->getPrevNode()->isTerminator())) {
            Instruction *Instr = InstrIter->getPrevNode();
            Builder.SetInsertPoint(InstrIter);
            
            instrumentalize(Module, *Instr, Builder, Nodes);
            Modified = true;
        }

        outs() << "after last\n";

        
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
 
    // render function def-use graph to png
    std::string OutFilename = (F.getName() + ".png").str();
    FILE *OutFile = fopen(OutFilename.c_str(), "w");
    if (OutFile) {
        gvLayout(Gvc, Graph, "dot");
        gvRender(Gvc, Graph, "png", OutFile);
        gvFreeLayout(Gvc, Graph);
    }
    fclose(OutFile);
    agclose(Graph);
    gvFreeContext(Gvc);
    
    return Modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
}


extern "C" llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "BinShiftPass", "1.0",
          [](llvm::PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](llvm::StringRef Name, llvm::FunctionPassManager &FPM,
                   llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "duviz") {
                    FPM.addPass(DefUseViz());
                    return true;
                  }
                  return false;
                });
          }};
}