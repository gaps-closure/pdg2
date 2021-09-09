#ifndef ZINCPRINTER_H_
#define ZINCPRINTER_H_
#include "Graph.hh"
#include "ProgramDependencyGraph.hh"
#include "FunctionWrapper.hh"

#include <fstream>

namespace pdg
{
  class MiniZincPrinter : public llvm::ModulePass
  {
    public:
      typedef std::unordered_map<llvm::Function *, FunctionWrapper *> FuncWrapperMap;
      static char ID;
      MiniZincPrinter() : llvm::ModulePass(ID) {};
      bool runOnModule(llvm::Module &M) override;
      void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
\
  };
}
#endif