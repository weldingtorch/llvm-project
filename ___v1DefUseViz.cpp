#include "DefUseViz.h"

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>

using namespace llvm;

#include <map>

#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>


PreservedAnalyses DefUseViz::run(Function &F,
                              FunctionAnalysisManager &AM) {
    

    GVC_t *Gvc = gvContext();
    Agraph_t *Graph = agopen(NULL, Agdirected, NULL);
    Agraph_t *FunctionSubgraph = agsubg(Graph, NULL, true);
    agsafeset(FunctionSubgraph, "cluster", "true", "true");
    agsafeset(FunctionSubgraph, "label", F.getName().data(), "");
    agsafeset(FunctionSubgraph, "newrank", "", "true");

    std::map<Value *, Agnode_t *> Nodes;
    
    ReversePostOrderTraversal<Function *> RPOT(&F);
    
    // build separately all basic blocks subgraphs
    for (auto *BB: RPOT) {
        Agnode_t *PrevNode{nullptr};
        
        Agraph_t *BasicBlockSubgraph = agsubg(FunctionSubgraph, NULL, true);
        agsafeset(BasicBlockSubgraph, "label", BB->getName().str().c_str(), "");
        
        // traverse instructions in basic block
        for (auto &Instr: *BB) {
            
            // add current instruction to BasicBlockSubgraph
            Agnode_t *CurNode = agnode(BasicBlockSubgraph, NULL, true);
            
            std::string StrBuf;
            llvm::raw_string_ostream Operation(StrBuf);
            Operation << Instr;
            agset(CurNode, "label", StrBuf.c_str());
            StrBuf.clear();
            
            agsafeset(CurNode, "shape", "box", "");


            if (PrevNode != nullptr) {
                Agedge_t *Edge = agedge(BasicBlockSubgraph, PrevNode, CurNode, NULL, true);
            }
            Nodes[&Instr] = CurNode;
            PrevNode = CurNode;


            for (auto &Op: Instr.operands()) {
                auto NodeIter = Nodes.find(Op);
                if (NodeIter != Nodes.end()) {
                    Agedge_t *Edge = agedge(BasicBlockSubgraph, CurNode, NodeIter->second, NULL, 1);
                    agsafeset(Edge, "color", "cyan", "");
                }
            }
        }
    }

    for (auto &BB: F) {
        Instruction *Term = BB.getTerminator();
        
        if (Term->getOpcode() == Instruction::CondBr) {
            auto It = Term->successors().begin();
            
            Agedge_t *EdgeIfTrue = agedge(FunctionSubgraph, Nodes[Term], Nodes[&It->front()], NULL, 1);
            agsafeset(EdgeIfTrue, "color", "green", "");
            It;
            
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
    std::string OutFilename = (F.getName()  ".png").str();
    FILE *OutFile = fopen(OutFilename.c_str(), "w");
    if (OutFile) {
        gvLayout(Gvc, Graph, "dot");
        gvRender(Gvc, Graph, "png", OutFile);
        gvFreeLayout(Gvc, Graph);
    }
    fclose(OutFile);
    agclose(Graph);
    gvFreeContext(Gvc);
    
    return PreservedAnalyses::all();
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
`}