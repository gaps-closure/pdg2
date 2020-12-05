#ifndef PROGRAMDEPENDENCYGRAPH_H_
#define PROGRAMDEPENDENCYGRAPH_H_
#include "llvm/IR/Module.h"
#include "DependencyGraph.hpp"
#include "llvm/PassAnalysisSupport.h"
#include "ControlDependencyGraph.hpp"
#include "DataDependencyGraph.hpp"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/Debug.h"
#include "DebugInfoUtils.hpp"
#include "PDGCommandLineOptions.hpp"
#include "KSplitStatsCollector.hpp"


namespace pdg
{
class ProgramDependencyGraph : public llvm::ModulePass
{
public:
  static char ID;
  ProgramDependencyGraph() : llvm::ModulePass(ID) { PDG = new DependencyGraph<InstructionWrapper>(); }
  ~ProgramDependencyGraph() { delete PDG; }
  bool runOnModule(llvm::Module &M);
  void buildPDGForFunc(llvm::Function *Func);
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const;
  llvm::StringRef getPassName() { return "Program Dependency Graph"; }
  // PDG processing
  bool processCallInst(InstructionWrapper *instW);
  bool processIndirectCallInst(llvm::CallInst *CI, InstructionWrapper *instW);
  void addNodeDependencies(InstructionWrapper *instW, DataDependencyGraph* ddg);
  std::set<llvm::Function *> computeFunctionsNeedPDGConstruction(llvm::Module &M);
  // parameter tree building
  std::vector<llvm::Function *> collectIndirectCallCandidates(llvm::FunctionType *functionType, llvm::Function &oriFunc, const std::set<std::string> &filterFuncs = std::set<std::string>());
  void copyFormalTreeToActualTree(llvm::CallInst *CI, llvm::Function* func);
  void buildActualParameterTrees(llvm::CallInst *CI);
  void drawActualParameterTree(llvm::CallInst *CI, TreeType treeTy);
  void buildFormalTreeForFunc(llvm::Function *Func);
  void buildFormalTreeForArg(llvm::Argument &arg, TreeType treeTy);
  llvm::DIType *FindCastFromDIType(llvm::Argument &arg);
  llvm::DIType *FindCastToDIType(llvm::Argument &arg, std::set<llvm::Function *> &seen_funcs);
  InstructionWrapper *buildPointerTreeNodeWithDI(llvm::Value &val, InstructionWrapper &parentTreeNodeW, tree<InstructionWrapper *> &objectTree, llvm::DIType &curDIType);
  void collectSharedGlobalVars(std::set<llvm::Function *> &driverDomainFuncs, std::set<llvm::Function *> &kernelDomainFuncs);
  void buildObjectTreeForGlobalVars();
  void buildObjectTreeForGlobalVar(llvm::GlobalVariable &GV, llvm::DIType &DI);
  bool hasRecursiveType(ArgumentWrapper *argW, tree<InstructionWrapper *>::iterator insert_loc);
  bool isFilePtrOrFuncTy(llvm::Type* ty);
  InstructionWrapper *buildPointerTypeNode(ArgumentWrapper *argW, InstructionWrapper *curTyNode, tree<InstructionWrapper *>::iterator insertLoc);
  InstructionWrapper *buildPointerTypeNodeWithDI(ArgumentWrapper *argW, InstructionWrapper *curTyNode, tree<InstructionWrapper *>::iterator, llvm::DIType *dt);
  void buildTypeTree(llvm::Argument &arg, InstructionWrapper *treeTyW, TreeType TreeType);
  void buildTypeTreeWithDI(llvm::Argument &arg, InstructionWrapper *treeTyW, TreeType TreeType, llvm::DIType *argDIType);
  void buildGlobalTypeTrees(std::set<llvm::DIType *> sharedTypes);
  void buildGlobalTypeTreeForDIType(llvm::DIType &DI);
  void collectInstsWithDIType(std::set<llvm::Function *> &search_domain);
  void connectGlobalTypeTreeWithAddressVars();
  void drawFormalParameterTree(llvm::Function *Func, TreeType treeTy);
  void connectFunctionAndFormalTrees(llvm::Function *callee);
  void connectTreeNodeWithAddrVars(ArgumentWrapper* argW);
  void connectCallerAndActualTrees(llvm::Function* caller);
  bool connectAllPossibleFunctions(llvm::CallInst *CI, std::vector<llvm::Function *> indirect_call_candidates);
  void connectActualTreeToFormalTree(llvm::CallInst *CI, llvm::Function *called_func);
  bool connectCallerAndCallee(InstructionWrapper *instW, llvm::Function *callee);
  void connectGlobalObjectTreeWithAddressVars(std::set<llvm::Function *> &searchDomain);
  std::set<llvm::Function *> inferAsynchronousCalledFunction();
  std::set<InstructionWrapper *> collectInstWsOnDIType(llvm::DIType *dt, std::set<llvm::Function *> &searchDomain);
  // field sensitive related functions
  std::vector<llvm::Instruction *> getArgStoreInsts(llvm::Argument &arg);
  llvm::Instruction *getArgAllocaInst(llvm::Argument &arg);
  llvm::Value *getCallSiteParamVal(llvm::CallInst* CI, unsigned idx);
  // tree building helper functions
  void getReadInstsOnInst(llvm::Instruction* inst, std::set<InstructionWrapper*> &readInsts);
  void getAllAlias(llvm::Instruction *inst, std::set<InstructionWrapper*> &ret);
  bool isFuncTypeMatch(llvm::FunctionType *funcType1, llvm::FunctionType *funcType2);
  llvm::StructType *getStructTypeFromGEP(llvm::Instruction *inst);
  llvm::Value *getLShrOnGep(llvm::GetElementPtrInst *gep);
  bool isGEPforBitField(llvm::GetElementPtrInst *gep);
  uint64_t getGEPOffsetInBits(llvm::StructType *structTy, llvm::GetElementPtrInst *gep);
  int getGEPAccessFieldOffset(llvm::GetElementPtrInst *gep);
  bool isTreeNodeGEPMatch(llvm::StructType* structTy, InstructionWrapper *treeNode, llvm::Instruction *GEP);
  bool isIndirectCallOrInlineAsm(llvm::CallInst *CI);
  bool nameMatch(std::string str1, std::string str2);
  tree<InstructionWrapper *>::iterator getInstInsertLoc(ArgumentWrapper *argW, InstructionWrapper *tyW, TreeType treeTy);
  tree<InstructionWrapper *>::iterator getTreeNodeInsertLoc(tree<InstructionWrapper *> &objectTree, InstructionWrapper *treeW);
  std::set<llvm::GlobalVariable *> getSharedGlobalVars() { return sharedGlobalVars; }
  //  dep printer related functions
  std::vector<DependencyNode<InstructionWrapper> *> getNodeSet() { return PDG->getNodeSet(); }
  DependencyGraph<InstructionWrapper> *_getPDG() { return PDG; }
  typename DependencyNode<InstructionWrapper>::DependencyLinkList getNodeDepList(llvm::Instruction *inst);
  typename DependencyNode<InstructionWrapper>::DependencyLinkList getNodesWithDepType(const InstructionWrapper* instW, DependencyType depType);
  std::set<InstructionWrapper *> getDepInstWrapperWithDepType(const InstructionWrapper *instW, DependencyType depType);
  void getDepInstsWithDepType(llvm::Instruction *source_inst, DependencyType target_dep_type, std::set<llvm::Instruction *> &dep_insts);
  llvm::Function *getCalledFunction(llvm::CallInst *CI);
  bool isFuncPointer(llvm::Type *ty);
  bool isStructPointer(llvm::Type *ty);
  std::map<llvm::GlobalVariable*, tree<InstructionWrapper *>> getGlobalObjectTrees() { return globalObjectTrees; }
  std::map<llvm::DIType*, tree<InstructionWrapper *>> getGlobalTypeTrees() { return globalTypeTrees; }
  bool isUnsafeTypeCast(llvm::Instruction* inst);
  bool isContainerOfGEP(llvm::Instruction* inst);
  unsigned getUnsafeTypeCastNum() { return unsafeTypeCastNum; }

private:
  llvm::Module *module;
  DependencyGraph<InstructionWrapper> *PDG;
  // ControlDependencyGraph *cdg;
  // DataDependencyGraph *ddg;
  std::ofstream shared_data_log_file;
  std::map<llvm::GlobalVariable*, tree<InstructionWrapper*>> globalObjectTrees;
  std::map<llvm::DIType*, tree<InstructionWrapper*>> globalTypeTrees;
  std::map<std::string, std::set<InstructionWrapper*>> shared_data_name_and_instw_map_;
  std::set<llvm::GlobalVariable *> sharedGlobalVars;
  std::set<llvm::Function*> cross_domain_funcs_;
  unsigned unsafeTypeCastNum;
};
} // namespace pdg

#endif