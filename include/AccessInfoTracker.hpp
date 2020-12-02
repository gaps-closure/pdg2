#ifndef ACCESSINFO_TRACKER_H_

#define ACCESSINFO_TRACKER_H_
#include "llvm/IR/Module.h"
#include "llvm/PassAnalysisSupport.h"
#include "ProgramDependencyGraph.hpp"
#include "llvm/Analysis/CallGraph.h" 
#include "DebugInfoUtils.hpp"
#include <map>

namespace pdg
{
class AccessInfoTracker : public llvm::ModulePass
{
public:
  AccessInfoTracker() : llvm::ModulePass(ID) {}
  static char ID;
  bool runOnModule(llvm::Module &M);
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const;
  void getIntraFuncReadWriteInfoForCallInsts(llvm::Function &Func);
  void printRetValueAccessInfo(llvm::Function &Func);
  void getIntraFuncReadWriteInfoForRetVal(CallWrapper *callW);
  void computeFuncAccessInfo(llvm::Function &F);
  void computeGlobalVarsAccessInfo();
  void computeFuncAccessInfoBottomUp(llvm::Function &F);
  std::vector<llvm::Function *> computeBottomUpCallChain(llvm::Function &F);
  void computeArgAccessInfo(ArgumentWrapper *argW, TreeType treeTy);
  void computeIntraprocArgAccessInfo(ArgumentWrapper *argW, llvm::Function &F);
  void computeInterprocArgAccessInfo(ArgumentWrapper *argW, llvm::Function &F);
  std::map<std::string, AccessType> computeInterprocAccessedFieldMap(llvm::Function &callee, unsigned argNo, llvm::DIType* parentType, std::string fieldNameInCaller);
  std::string getRegisteredFuncPtrName(std::string funcName);
  std::set<llvm::Value *> findAliasInDomainWithOffset(llvm::Value &V, llvm::Function &F, unsigned offset, std::set<llvm::Function *> receiverDomainTrans);
  std::set<llvm::Value *> findAliasInDomain(llvm::Value &V, llvm::Function &F, std::set<llvm::Function *> domainTransitiveClosure);
  std::set<llvm::Function *> getTransitiveClosureInDomain(llvm::Function &F, std::set<llvm::Function *> searchDomain);
  void getIntraFuncReadWriteInfoForCallee(llvm::Function &F);
  int getCallParamIdx(const InstructionWrapper *instW, const InstructionWrapper *callInstW);
  ArgumentMatchType getArgMatchType(llvm::Argument *arg1, llvm::Argument *arg2);
  void mergeArgAccessInfo(ArgumentWrapper *callerArgW, ArgumentWrapper *calleeArgW, tree<InstructionWrapper*>::iterator callerTreeI);
  AccessType getAccessTypeForInstW(const InstructionWrapper *instW);
  void printFuncArgAccessInfo(llvm::Function &F);
  void printArgAccessInfo(ArgumentWrapper *argW, TreeType ty);
  void generateIDLForCallInsts(llvm::Function &F);
  void generateIDLforFunc(llvm::Function &F);
  void generateSyncDataStubAtFuncEnd(llvm::Function &F);
  void generateIDLforFuncPtr(llvm::Type* ty, std::string funcName, llvm::Function& F);
  void generateIDLforFuncPtrWithDI(llvm::DIType *funcDIType, llvm::Module *module, std::string funcPtrName);
  void generateRpcForFunc(llvm::Function &F);
  void generateIDLForCallInstW(CallWrapper *CW);
  // void generateIDLforArg(ArgumentWrapper *argW, TreeType ty, std::string funcName = "", bool handleFuncPtr = false);
  void generateIDLforArg(ArgumentWrapper *argW);
  void generateProjectionForTreeNode(tree<InstructionWrapper *>::iterator treeI, llvm::raw_string_ostream &OS, std::string argName, std::queue<tree<InstructionWrapper *>::iterator> &nested_pointers,
                                     bool is_func_ptr_export_from_driver = false, std::string parent_struct_indent_level = "\t\t");
  void generateProjectionForGlobalVarInFunc(tree<InstructionWrapper *>::iterator treeI, llvm::raw_string_ostream &OS, llvm::DIType *argDIType, llvm::Function& func);
  tree<InstructionWrapper *>::iterator generateIDLforStructField(ArgumentWrapper *argW, int subtreeSize, tree<InstructionWrapper *>::iterator treeI, std::stringstream &ss, TreeType ty);
  std::string getArgAccessInfo(llvm::Argument &arg);
  std::string getAllocAttribute(std::string projStr, bool isPassedToCallee);
  void computeAccessedFieldsInStructType(std::string structTypeName);
  // compute Shared Data Based On Type
  void computeSharedData();
  void computeSharedDataInFunc(llvm::Function &F);
  std::set<std::string> computeAccessedFieldsForDIType(tree<InstructionWrapper *> objectTree, llvm::DIType *rootDIType);
  std::set<std::string> computeSharedDataForType(llvm::DIType* dt);
  std::set<std::string> computeAccessedDataInDomain(llvm::DIType* dt, std::set<llvm::Function*> domain);
  void inferAsynchronousCalledFunction(std::set<llvm::Function *> crossDomainFuncs);
  bool isChildFieldShared(llvm::DIType* parentNodeDIType, llvm::DIType* fieldDIType);
  ProgramDependencyGraph *_getPDG() { return PDG; }
  std::unordered_map<std::string, std::set<std::string>> getSharedDataTypeMap() { return sharedDataTypeMap; }
  std::string getReturnValAnnotationStr(llvm::Function &F);
  bool mayAlias(llvm::Value &V1, llvm::Value &V2, llvm::Function &F);
  std::set<llvm::Instruction *> getIntraFuncAlias(llvm::Instruction *inst);
  uint64_t getArrayArgSize(llvm::Value &V, llvm::Function &F);
  int getCallOperandIdx(llvm::Value *operand, llvm::CallInst *callInst);
  std::string switchIndirectCalledPtrName(std::string funcptr);
  void InferTreeNodeAnnotation(tree<InstructionWrapper *>::iterator tree_node_iter, std::set<std::string> &annotations, std::set<llvm::Function *> &visited_funcs);
  std::string ComputeNodeAnnotationStr(tree<InstructionWrapper *>::iterator tree_node_iter);
  std::string inferFieldAnnotation(InstructionWrapper* instW, std::string fieldID);
  bool voidPointerHasMultipleCasts(InstructionWrapper *voidPtrW);
  bool IsUsedInStrOps(InstructionWrapper* candidate_string_inst_w);
  bool IsUsedInMemOps(InstructionWrapper* candidate_string_inst_w);
  bool IsCastedToArrayType(llvm::Value& val);
  void setupStrOpsMap();
  void setupMemOpsMap();
  void setupAllocatorWrappers();
  void setupDeallocatorWrappers();
  void initializeNumStats();
  unsigned computeUsedGlobalNumInDriver();
  void printAsyncCalls();
  void printCopiableFuncs(std::set<llvm::Function *> &searchDomain);
  std::set<llvm::Function *> computeFuncsAccessPrivateData(std::set<llvm::Function *> &searchDomain);
  std::set<llvm::Function *> computeFuncsContainCS(std::set<llvm::Function *> &searchDomain);
  tree<InstructionWrapper *>::iterator getParentIter(tree<InstructionWrapper *>::iterator treeI);
  bool IsFuncPtrExportFromDriver(std::string);
  bool IsAllocator(std::string funcName);
  bool IsStringOps(std::string funcName);
  bool IsMemOps(std::string funcName);
  bool IsStoreOfAlias(llvm::StoreInst* store_inst);
  FunctionDomain computeFuncDomain(llvm::Function &F);

private:
  ProgramDependencyGraph *PDG;
  llvm::Module *module;
  llvm::CallGraph *CG;
  std::ofstream idl_file;
  std::ofstream log_file;
  std::set<llvm::Function *> kernel_domain_funcs_;
  std::set<llvm::Function *> driver_domain_funcs_;
  std::set<llvm::Function*> importedFuncs;
  std::set<std::string> driverExportFuncPtrNames;
  std::map<std::string, std::string> driverExportFuncPtrNameMap;
  std::set<std::string> usedCallBackFuncs;
  std::unordered_map<std::string, std::set<std::string>> sharedDataTypeMap;
  std::unordered_map<std::string, llvm::DIType*> diTypeNameMap;
  std::unordered_map<std::string, AccessType> globalFieldAccessInfo;
  std::set<std::string> seenFuncOps;
  std::set<std::string> stringOperations;
  std::set<std::string> memOperations;
  std::set<std::string> allocatorWrappers;
  std::set<std::string> deallocatorWrappers;
  std::set<std::string> global_string_struct_fields_;
  std::set<std::string> global_array_fields_;
  std::set<llvm::Function*> asyncCallAccessedSharedData;
  std::string globalOpsStr;
  bool crossBoundary; // indicate whether transitive closure cross two domains
};

std::string getAccessAttributeName(tree<InstructionWrapper *>::iterator treeI);

} // namespace pdg
#endif