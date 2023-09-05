#ifndef PROGRAMDEPENDENCYGRAPH_H_
#define PROGRAMDEPENDENCYGRAPH_H_
#include "LLVMEssentials.hh"
#include "Graph.hh"
#include "PDGCallGraph.hh"
#include "DataDependencyGraph.hh"
#include "ControlDependencyGraph.hh"

namespace pdg
{
  class ProgramDependencyGraph : public llvm::ModulePass
  {
    public:
      static char ID;
      ProgramDependencyGraph() : llvm::ModulePass(ID) {};
      bool runOnModule(llvm::Module &M) override;
      void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
      ProgramGraph *getPDG() { return _PDG; }
      llvm::StringRef getPassName() const override { return "Program Dependency Graph"; }
      FunctionWrapper *getFuncWrapper(llvm::Function &F) { return _PDG->getFuncWrapperMap()[&F]; }
      CallWrapper *getCallWrapper(llvm::CallInst &call_inst) { return _PDG->getCallWrapperMap()[&call_inst]; }

      bool isTypeEqual(llvm::Type& t1, llvm::Type &t2);
      bool isFuncSignatureMatch(llvm::CallInst &ci, llvm::Function &f);

      void connectGlobalWithUses();
      void connectInTrees(Tree *src_tree, Tree *dst_tree, EdgeType edge_type);
      void connectOutTrees(Tree *src_tree, Tree *dst_tree, EdgeType edge_type);
      void connectCallerIndirect(llvm::CallInst &ci);
      void connectCallerAndCallee(CallWrapper &cw, FunctionWrapper &fw);
      void connectIntraprocDependencies(llvm::Function &F);
      void connectInterprocDependencies(llvm::Function &F);
      void connectFormalInTreeWithAddrVars(Tree &formal_in_tree);
      void connectFormalOutTreeWithAddrVars(Tree &formal_out_tree);
      void connectActualTreeWithAddrVars(Tree &actual_in_tree, llvm::CallInst &ci, EdgeType type);
      bool canReach(Node &src, Node &dst);
      bool canReach(Node &src, Node &dst, std::set<EdgeType> exclude_edge_types);

    private:
      llvm::Module *_module;
      ProgramGraph *_PDG;
  };
}
#endif