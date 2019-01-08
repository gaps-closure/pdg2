#ifndef ACCESSINFO_TRACKER_H_
#define ACCESSINFO_TRACKER_H_
#include "llvm/IR/Module.h"
#include "llvm/PassAnalysisSupport.h"
#include "ProgramDependencyGraph.hpp"
#include "FieldNameExtractor.hpp"
#include <fstream>
#include <sstream>

namespace pdg
{
class AccessInfoTracker : public llvm::ModulePass
{
public:
  AccessInfoTracker() : llvm::ModulePass(ID) {}
  static char ID;
  bool runOnModule(llvm::Module &M);
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const;
  std::vector<llvm::Instruction *> getArgStoreInsts(llvm::Argument &arg);
  std::set<InstructionWrapper *> getAliasForArg(ArgumentWrapper *argW);
  void getIntraFuncReadWriteInfoForArg(ArgumentWrapper *argW);
  void getIntraFuncReadWriteInfoForFunc(llvm::Function &F);
  int getCallParamIdx(InstructionWrapper *instW, InstructionWrapper *callInstW);
  ArgumentMatchType getArgMatchType(llvm::Argument *arg1, llvm::Argument *arg2);
  void collectParamCallInstWForArg(ArgumentWrapper *argW, InstructionWrapper *aliasInstW);
  void mergeArgWAccessInfo(ArgumentWrapper *callerArgW, ArgumentWrapper *calleeArgW);
  void mergeTypeTreeAccessInfo(ArgumentWrapper *callerArgW, tree<InstructionWrapper *>::iterator mergeTo, tree<InstructionWrapper *>::iterator mergeFrom);
  void getInterFuncReadWriteInfo(llvm::Function &F);
  AccessType getAccessTypeForInstW(InstructionWrapper *instW);
  void propergateAccessInfoToParent(ArgumentWrapper *argW, tree<InstructionWrapper *>::iterator treeI);
  void printFuncArgAccessInfo(llvm::Function &F, std::map<unsigned, FieldNameExtractor::offsetNames> argsOffsetNames);
  void printArgAccessInfo(ArgumentWrapper *argW, FieldNameExtractor::offsetNames argOffsetNames);
  void generateIDLforFunc(llvm::Function &F, std::map<unsigned, FieldNameExtractor::offsetNames> argsOffsetNames);
  void generateIDLforArg(ArgumentWrapper *argW, FieldNameExtractor::offsetNames argOffsetNames);
  int generateIDLforStructField(int subtreeSize, tree<InstructionWrapper *>::iterator &treeI, std::stringstream &ss, int visit_order, FieldNameExtractor::offsetNames argOffsetNames);

private:
  ProgramDependencyGraph *PDG;
  std::ofstream idl_file;
};

bool isStructPointer(llvm::Type* ty);
std::string getTypeNameByTag(llvm::DIType *ty);

} // namespace pdg
#endif