#include "AccessInfoTracker.hpp"
#include <math.h>

using namespace llvm;

char pdg::AccessInfoTracker::ID = 0;

bool pdg::AccessInfoTracker::runOnModule(Module &M)
{
  module = &M;
  PDG = &getAnalysis<ProgramDependencyGraph>();
  auto &pdgUtils = PDGUtils::getInstance();
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  // get cross domain functions and setup kernel and driver side functions

  std::set<Function *> crossDomainFuncCalls = pdgUtils.computeCrossDomainFuncs(M);
  importedFuncs = pdgUtils.computeImportedFuncs(M);
  ksplit_stats_collector.SetNumberOfKernelToDriverCalls(importedFuncs.size());
  driverExportFuncPtrNames = pdgUtils.computeDriverExportFuncPtrName();
  ksplit_stats_collector.SetNumberOfDriverToKernelCalls(driverExportFuncPtrNames.size());

  driver_domain_funcs_ = pdgUtils.computeDriverDomainFuncs(M);
  kernel_domain_funcs_ = pdgUtils.computeKernelDomainFuncs(M);
  driverExportFuncPtrNameMap = pdgUtils.computeDriverExportFuncPtrNameMap(M);
  // counter for how many field we eliminate using shared data
  setupStrOpsMap();
  setupMemOpsMap();
  setupAllocatorWrappers();
  setupDeallocatorWrappers();
  globalOpsStr = "";
  log_file.open("analysis_log");
  shared_ptr_file.open("shared_ptr.txt");
  shared_ptr_debug_log.open("shared_ptr_debug_log.txt");
  // start generating IDL
  std::string file_name = "kernel";
  file_name += ".idl";
  idl_file.open(file_name);
  idl_file << "module kernel"
           << " {\n";
  computeSharedData();
  ksplit_stats_collector.SetNumberOfSharedStructType(sharedDataTypeMap.size());
  unsigned num_of_shared_fields = 0;
  // for (auto pair : sharedDataTypeMap)
  // {
  //   for (auto shared_field_id : pair.second)
  //   {
  //     errs() << "shared data id: " << shared_field_id << "\n";
  //   }
  // }
  // ksplit_stats_collector.SetNumberOfSharedStructFields(num_of_shared_fields);

  std::set<Function*> crossDomainTransFuncs;
  pdgUtils.computeCrossDomainTransFuncs(M, crossDomainTransFuncs);
  std::set<Function*> reachableFuncInKernel;
  for (auto func : crossDomainTransFuncs)
  {
    if (kernel_domain_funcs_.find(func) != kernel_domain_funcs_.end())
      reachableFuncInKernel.insert(func);
  }
  // printCopiableFuncs(reachableFuncInKernel);
  // computeGlobalVarsAccessInfo();
  for (Function *F : crossDomainFuncCalls)
  {
    if (F->isDeclaration() || F->empty())
      continue;
    // computeFuncAccessInfo(*F);
    computeFuncAccessInfoBottomUp(*F);
    generateIDLforFunc(*F);
    // generateSyncDataStubAtFuncEnd(*F);
  }
  idl_file << globalOpsStr << "\n";
  idl_file << "}";
  idl_file.close();
  log_file.close();
  shared_ptr_file.close();
  shared_ptr_debug_log.close();
  ksplit_stats_collector.PrintProjectionStats();
  ksplit_stats_collector.PrintKernelIdiomStats();
  ksplit_stats_collector.PrintKernelIdiomSharedStats();
  return false;
}

void pdg::AccessInfoTracker::setupStrOpsMap()
{
  stringOperations.insert("strcpy");
  stringOperations.insert("strncpy");
  stringOperations.insert("strlen");
  stringOperations.insert("strlcpy");
  stringOperations.insert("strcmp");
  stringOperations.insert("strncmp");
  stringOperations.insert("kobject_set_name");
}

void pdg::AccessInfoTracker::setupMemOpsMap()
{
  memOperations.insert("memcpy");
  memOperations.insert("memset");
  memOperations.insert("memcmp");
  memOperations.insert("memmove");
}

void pdg::AccessInfoTracker::setupAllocatorWrappers()
{
  allocatorWrappers.insert("kmalloc");
  allocatorWrappers.insert("malloc");
  allocatorWrappers.insert("zalloc");
  allocatorWrappers.insert("kzalloc");
}

void pdg::AccessInfoTracker::setupDeallocatorWrappers()
{
  deallocatorWrappers.insert("kfree");
}

void pdg::AccessInfoTracker::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.addRequired<pdg::ProgramDependencyGraph>();
  AU.addRequired<CallGraphWrapperPass>();
  AU.setPreservesAll();
}

std::string pdg::AccessInfoTracker::getRegisteredFuncPtrName(std::string funcName)
{
  if (driverExportFuncPtrNameMap.find(funcName) != driverExportFuncPtrNameMap.end())
    return driverExportFuncPtrNameMap[funcName];
  return funcName;
}

bool pdg::AccessInfoTracker::voidPointerHasMultipleCasts(InstructionWrapper* voidPtrW)
{
  // get dep list
  unsigned castTimes = 0;
  auto valDepList = PDG->getNodesWithDepType(voidPtrW, DependencyType::VAL_DEP);
  for (auto depPair : valDepList)
  {
    auto dataW = const_cast<InstructionWrapper *>(depPair.first->getData());
    if (dataW->getInstruction() != nullptr)
    {
      auto depInsts = PDG->getNodesWithDepType(dataW, DependencyType::VAL_DEP);
      for (auto depInstPair : depInsts)
      {
        auto instW = const_cast<InstructionWrapper *>(depInstPair.first->getData());
        if (instW->getInstruction() == nullptr)
          continue;
        if (CastInst *castI = dyn_cast<CastInst>(instW->getInstruction()))
        {
          if (castI->getOperand(0) == dataW->getInstruction())
            castTimes++;
          if (castTimes > 1)
            return true;
        }
      }
    }
  }
  return false;
}

unsigned pdg::AccessInfoTracker::computeUsedGlobalNumInDriver()
{
  auto &pdgUtils = PDGUtils::getInstance();
  unsigned globalUsedInDriver = 0;
  for (auto globalIt = module->global_begin(); globalIt != module->global_end(); ++globalIt)
  {
    for (auto user : globalIt->users())
    {
      if (Instruction *inst = dyn_cast<Instruction>(user))
      {
        auto func = inst->getFunction();
        if (driver_domain_funcs_.find(func) != driver_domain_funcs_.end())
        {
          globalUsedInDriver++;
          break;
        }
      }
    }
  }
  return globalUsedInDriver;
}

AccessType pdg::AccessInfoTracker::getAccessTypeForInstW(const InstructionWrapper *instW)
{
  auto dataDList = PDG->getNodeDepList(instW->getInstruction());
  AccessType accessType = AccessType::NOACCESS;

  for (auto depPair : dataDList)
  {
    InstructionWrapper *depInstW = const_cast<InstructionWrapper *>(depPair.first->getData());
    DependencyType depType = depPair.second;

    // check for read
    if (!depInstW->getInstruction())
      continue;

    if (depType == DependencyType::DATA_DEF_USE)
    {
      // if (isa<LoadInst>(depInstW->getInstruction()) || isa<GetElementPtrInst>(depInstW->getInstruction()))
      accessType = AccessType::READ;
    }

    // check for store instruction.
    if (depType == DependencyType::DATA_DEF_USE)
    {
      if (StoreInst *st = dyn_cast<StoreInst>(depInstW->getInstruction()))
      {
        // if a value is used in a store instruction and is the store destination
        if (st->getPointerOperand() == instW->getInstruction())
        {
          if (isa<Argument>(st->getValueOperand())) // ignore the store inst that store arg to stack mem
            continue;
          // if the modified field is a pointer field, then we assume that 
          if (IsStoreOfAlias(st))
            continue;
          accessType = AccessType::WRITE;
          break;
        }
      }
    }
  }
  return accessType;
}

void pdg::AccessInfoTracker::printRetValueAccessInfo(Function &Func)
{
  auto &pdgUtils = PDGUtils::getInstance();
  for (CallInst *CI : pdgUtils.getFuncMap()[&Func]->getCallInstList())
  {
    CallWrapper *CW = pdgUtils.getCallMap()[CI];
    errs() << "Ret Value Acc Info.." << "\n";
    printArgAccessInfo(CW->getRetW(), TreeType::ACTUAL_IN_TREE);
    errs() << "......... [ END " << Func.getName() << " ] .........\n\n";
  }
}

void pdg::AccessInfoTracker::printCopiableFuncs(std::set<Function*> &searchDomain)
{
  auto funcsPrivate = computeFuncsAccessPrivateData(searchDomain);
  auto funcsCS = computeFuncsContainCS(searchDomain);
  unsigned count = 0;
  errs() << "======================= copiable funcs  ===========================\n";
  for (auto func : searchDomain)
  {
    if (func->isDeclaration() || func->empty())
      continue;
    if (funcsPrivate.find(func) == funcsPrivate.end() && funcsCS.find(func) == funcsCS.end())
    {
      errs() << func->getName() << "\n";
      count++;
    }
  }
  errs() << "Copiable func Num: " << count << "\n";
  errs() << "kernel func Num: " << kernel_domain_funcs_.size() << "\n";
  errs() << "======================= copiable funcs  ===========================\n";
}

std::set<Function*> pdg::AccessInfoTracker::computeFuncsAccessPrivateData(std::set<Function*> &searchDomain)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  auto funcMap = pdgUtils.getFuncMap();
  std::set<Function*> ret;
  for (auto func : searchDomain)
  {
    if (func->isDeclaration() || func->empty())
      continue;
    if (!funcMap[func]->hasTrees())
      continue;
    bool accessPrivateData = false;
    for (auto instI = inst_begin(func); instI != inst_end(func); ++instI)
    {
      auto instW = instMap[&*instI];
      auto valDepPairs = PDG->getNodesWithDepType(instW, DependencyType::VAL_DEP);
      if (valDepPairs.size() == 0)
        continue;
      for (auto valDepPair : valDepPairs)
      {
        auto dataW = valDepPair.first->getData();
        if (!dataW->isSharedNode())
        {
          accessPrivateData = true;
          break;
        }
      }
      if (accessPrivateData)
      {
        ret.insert(func);
        break;
      }
    }
  }
  return ret;
}

std::set<Function *> pdg::AccessInfoTracker::computeFuncsContainCS(std::set<Function *> &searchDomain)
{
  std::map<std::string, std::string> lockPairMap;
  lockPairMap.insert(std::make_pair("mutex_lock", "mutex_unlock"));
  lockPairMap.insert(std::make_pair("_raw_spin_lock", "_raw_spin_unlock"));
  lockPairMap.insert(std::make_pair("_raw_spin_lock_irq", "_raw_spin_unlock_irq"));
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  auto funcMap = pdgUtils.getFuncMap();
  std::set<Function*> ret;
  for (auto func : searchDomain)
  {
    if (func->isDeclaration() || func->empty())
      continue;
    if (!funcMap[func]->hasTrees())
      continue;
    for (auto instI = inst_begin(func); instI != inst_end(func); ++instI)
    {
      if (CallInst *ci = dyn_cast<CallInst>(&*instI))
      {
        auto calledVal = ci->getCalledValue()->stripPointerCasts();
        if (calledVal == nullptr)
          continue;
        if (Function *f = dyn_cast<Function>(calledVal))
        {
          if (lockPairMap.find(f->getName().str()) != lockPairMap.end())
            ret.insert(func);
        }
      }
    }
  }
  return ret;
}

bool pdg::AccessInfoTracker::IsUsedInMemOps(InstructionWrapper *candidate_array_inst_w)
{
  std::set<Instruction*> dep_insts_on_string_inst;
  Instruction* candidate_array_inst = candidate_array_inst_w->getInstruction();
  if (candidate_array_inst == nullptr)
    return false;
  PDG->getDepInstsWithDepType(candidate_array_inst, DependencyType::DATA_READ, dep_insts_on_string_inst);
  for (Instruction* i : dep_insts_on_string_inst)
  {
    if (LoadInst *li = dyn_cast<LoadInst>(i))
    {
      std::set<Instruction*> intra_func_users;
      PDG->getDepInstsWithDepType(li, DependencyType::DATA_DEF_USE, intra_func_users);
      for (Instruction* intra_func_user : intra_func_users)
      {
        CallSite CS(intra_func_user);
        if (CS.isCall() && !CS.isIndirectCall())
        {
          Function* called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
          if (called_func != nullptr)
          {
            std::string called_func_name = called_func->getName().str();
            if (IsMemOps(called_func_name))
              return true;
          }
        }
      }
    }
  }
  return false;
}

bool pdg::AccessInfoTracker::IsUsedInStrOps(InstructionWrapper *candidate_string_inst_w)
{
  // step 1: collect all the load on this instW. A load inst on 
  std::set<Instruction*> dep_insts_on_string_inst;
  Instruction* candidate_string_inst = candidate_string_inst_w->getInstruction();
  if (candidate_string_inst == nullptr)
    return false;
  PDG->getDepInstsWithDepType(candidate_string_inst, DependencyType::DATA_READ, dep_insts_on_string_inst);
  for (Instruction* i : dep_insts_on_string_inst)
  {
    if (LoadInst *li = dyn_cast<LoadInst>(i))
    {
      std::set<Instruction*> intra_func_users;
      PDG->getDepInstsWithDepType(li, DependencyType::DATA_DEF_USE, intra_func_users);
      for (Instruction* intra_func_user : intra_func_users)
      {
        CallSite CS(intra_func_user);
        if (CS.isCall() && !CS.isIndirectCall())
        {
          Function* called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
          if (called_func != nullptr)
          {
            std::string called_func_name = called_func->getName().str();
            if (IsStringOps(called_func_name))
              return true;
          }
        }
      }
    }
  }
  return false;
}

void pdg::AccessInfoTracker::computeSharedData()
{
  auto globalTypeTrees = PDG->getGlobalTypeTrees();
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  for (auto pair : globalTypeTrees)
  {
    DIType *sharedType = pair.first;
    auto typeTree = pair.second;
    if (!sharedType)
      continue;
    std::set<std::string> accessedFields;
    // iterate through each node, find their addr variables and then analyze the accesses to the addr variables
    for (auto treeI = typeTree.begin(); treeI != typeTree.end(); ++treeI)
    {
      DIType *field_di_type = (*treeI)->getDIType();
      DIType *parent_node_di_type = nullptr;
      if (tree<InstructionWrapper *>::depth(treeI) > 0)
      {
        auto parentI = tree<InstructionWrapper *>::parent(treeI);
        parent_node_di_type = (*parentI)->getDIType();
      }
      std::string fieldID = DIUtils::computeFieldID(parent_node_di_type, field_di_type);
      // hanlde static defined functions, assume all functions poineter that are linked with defined function in device driver module should be used by kernel.
      if (DIUtils::isFuncPointerTy((*treeI)->getDIType()))
      {
        std::string funcptrName = DIUtils::getDIFieldName((*treeI)->getDIType());
        if (driverExportFuncPtrNames.find(funcptrName) != driverExportFuncPtrNames.end())
        {
          accessedFields.insert(fieldID);
        }
        continue;
      }

      std::set<std::string> kernel_access_func_names;
      std::set<std::string> driver_access_func_names;
      // get valdep pair, and check for intraprocedural accesses
      auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
      bool accessInKernel = false;
      bool accessInDriver = false;
      AccessType nodeAccessTy = AccessType::NOACCESS;
      for (auto valDepPair : valDepPairList)
      {
        auto dataW = const_cast<InstructionWrapper*>(valDepPair.first->getData());
        Instruction *inst = dataW->getInstruction();
        AccessType accType = getAccessTypeForInstW(dataW);
        if (accType != AccessType::NOACCESS)
        {
          // check if a field is shared
          if (inst != nullptr)
          {
            Function *f = inst->getFunction();
            if (driver_domain_funcs_.find(f) != driver_domain_funcs_.end())
            {
              driver_access_func_names.insert(f->getName().str());
              accessInDriver = true;
            }
            if (kernel_domain_funcs_.find(f) != kernel_domain_funcs_.end())
            {
              kernel_access_func_names.insert(f->getName().str());
              accessInKernel = true;
            }
          }
          if (accType == AccessType::WRITE)
            nodeAccessTy = AccessType::WRITE;
          if (accType == AccessType::READ && nodeAccessTy != AccessType::WRITE)
            nodeAccessTy = AccessType::READ;
        }
        
        // here, we also collect string type field on the fly
        if (IsUsedInStrOps(dataW))
        {
          if (DIUtils::isCharPointer(field_di_type))
            global_string_struct_fields_.insert(fieldID);
        }

        if (IsUsedInMemOps(dataW))
          global_array_fields_.insert(fieldID);

        // verified a field is accessed in both domains
        if (accessInDriver && accessInKernel)
          break;
      }
      // if a field is not shared, continue to next tree node
      if (!accessInDriver || !accessInKernel)
        continue;

      log_file << "field ID: " << fieldID << " - " << tree<InstructionWrapper*>::depth(treeI) << "\n";
      log_file << "\t driver funcs: ";
      for (auto func_name : driver_access_func_names)
      {
        log_file << func_name << ",  ";
      }
      log_file << "\n";
      log_file << "\t kernel funcs: ";
      for (auto func_name : kernel_access_func_names)
      {
        log_file << func_name << ",  ";
      }
      log_file << "\n";


      TreeTypeWrapper *treeW = static_cast<TreeTypeWrapper *>(const_cast<InstructionWrapper *>(*treeI));
      treeW->setShared(true);
      accessedFields.insert(fieldID);
      // update the accessed type for a field
      if (globalFieldAccessInfo.find(fieldID) == globalFieldAccessInfo.end())
        globalFieldAccessInfo.insert(std::make_pair(fieldID, nodeAccessTy));
    }

    std::string sharedTypeName = DIUtils::getDITypeName(sharedType);
    if (sharedDataTypeMap.find(sharedTypeName) == sharedDataTypeMap.end())
    {
      sharedDataTypeMap[sharedTypeName] = accessedFields;
      diTypeNameMap[sharedTypeName] = sharedType;
    }
    else
      sharedDataTypeMap[sharedTypeName].insert(accessedFields.begin(), accessedFields.end());
  }
}

void pdg::AccessInfoTracker::computeArgAccessInfo(ArgumentWrapper *argW, TreeType treeTy)
{
  auto argTree = argW->getTree(treeTy);
  if (argTree.size() == 0)
    return;
  // assert(argTree.size() != 0 && "find empty formal tree. Stop computing arg access info");
  auto func = argW->getArg()->getParent();
  auto treeI = argW->getTree(treeTy).begin();
  // check for debugging information availability
  if ((*treeI)->getDIType() == nullptr)
  {
    errs() << "Empty debugging info for " << func->getName() << " - " << argW->getArg()->getArgNo() << "\n";
    return;
  }
  // we compute access information only for pointers
  if ((*treeI)->getDIType()->getTag() != dwarf::DW_TAG_pointer_type)
  {
    errs() << func->getName() << " - " << argW->getArg()->getArgNo() << " Find non-pointer type parameter, do not track...\n";
    return;
  }

  computeIntraprocArgAccessInfo(argW, *func);
  computeInterprocArgAccessInfo(argW, *func);
}

// intraprocedural
void pdg::AccessInfoTracker::computeIntraprocArgAccessInfo(ArgumentWrapper *argW, Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  // iterate through each node, find their addr variables and then analyze the accesses to the addr variables
  for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
  {
    // hanlde static defined functions, assume all functions poineter that are linked with defined function in device driver module should be used by kernel.
    if (DIUtils::isFuncPointerTy((*treeI)->getDIType()))
    {
      std::string funcptrName = DIUtils::getDIFieldName((*treeI)->getDIType());
      std::string funcName = switchIndirectCalledPtrName(funcptrName);
      if (driverExportFuncPtrNameMap.find(funcName) != driverExportFuncPtrNameMap.end())
      {
        (*treeI)->setAccessType(AccessType::READ);
        usedCallBackFuncs.insert(funcName);
        if (tree<InstructionWrapper *>::depth(treeI) > 0)
        {
          auto parentI = tree<InstructionWrapper *>::parent(treeI);
          (*parentI)->setAccessType(AccessType::READ);
        }
        continue;
      }
    }

    // get valdep pair, and check for intraprocedural accesses
    auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
    for (auto valDepPair : valDepPairList)
    {
      auto dataW = valDepPair.first->getData();
      AccessType accType = getAccessTypeForInstW(dataW);
      if (static_cast<int>(accType) > static_cast<int>((*treeI)->getAccessType()))
        (*treeI)->setAccessType(accType);
      auto parentI = getParentIter(treeI);
      if ((*parentI)->getAccessType() == AccessType::NOACCESS)
        (*parentI)->setAccessType(accType);
    }
  }
}

void pdg::AccessInfoTracker::computeInterprocArgAccessInfo(ArgumentWrapper *argW, Function &F)
{
  // errs() << "start computing inter proc info for: " << F.getName() << " - " << argW->getArg()->getArgNo() << "\n";
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  std::map<std::string, AccessType> interprocAccessFieldMap;
  for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
  {
    // need parent DI info for computing field ID in later steps
    DIType *parentNodeDIType = nullptr;
    if (tree<InstructionWrapper *>::depth(treeI) != 0)
    {
      auto parentI = tree<InstructionWrapper *>::parent(treeI);
      parentNodeDIType = (*parentI)->getDIType();
    }
    auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
    for (auto valDepPair : valDepPairList)
    {
      auto dataW = valDepPair.first->getData();
      // compute interprocedural access info in the receiver domain
      auto depNodePairs = PDG->getNodesWithDepType(dataW, DependencyType::DATA_CALL_PARA);
      for (auto depNodePair : depNodePairs)
      {
        // check if the struct or some of its fields are passed through function calls
        auto depInstW = depNodePair.first->getData();
        if (CallInst *ci = dyn_cast<CallInst>(depInstW->getInstruction()))
        {
          int callArgIdx = getCallOperandIdx(dataW->getInstruction(), ci);
          if (callArgIdx < 0) // invalid idx
            continue;
          if (Function *called_func = dyn_cast<Function>(ci->getCalledValue()->stripPointerCasts()))
          {
            if (called_func->isDeclaration() || called_func->empty())
              continue;
            // compute the field that are accessed in the callee. The return map's key is accessed field's id and the value is access type
            auto accessFieldMap = computeInterprocAccessedFieldMap(*called_func, callArgIdx, parentNodeDIType, DIUtils::getDIFieldName((*treeI)->getDIType()));
            interprocAccessFieldMap.insert(accessFieldMap.begin(), accessFieldMap.end());
          }
        }
      }
    }
  }
  // set accessed type according to the interproc access field map
  for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
  {
    DIType *parentDIType = nullptr;
    DIType *curNodeDIType = (*treeI)->getDIType();
    if (tree<InstructionWrapper *>::depth(treeI) != 0)
    {
      auto ParentI = tree<InstructionWrapper *>::parent(treeI);
      parentDIType = (*ParentI)->getDIType();
    }
    std::string fieldId = "";
    if (DIUtils::isPointerType(parentDIType))
      fieldId = DIUtils::computeFieldID(parentDIType, parentDIType) + "*";
    else
      fieldId = DIUtils::computeFieldID(parentDIType, curNodeDIType);

    if (interprocAccessFieldMap.find(fieldId) != interprocAccessFieldMap.end())
      (*treeI)->setAccessType(interprocAccessFieldMap[fieldId]);
  }
}

std::vector<Function *> pdg::AccessInfoTracker::computeBottomUpCallChain(Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  // search domain is used to optimize the results by computing IDL made by first
  auto searchDomain = kernel_domain_funcs_;
  if (kernel_domain_funcs_.find(&F) == kernel_domain_funcs_.end())
    searchDomain = driver_domain_funcs_;
  std::vector<Function *> ret;
  ret.push_back(&F);
  std::set<Function *> seen_funcs;
  std::queue<Function *> funcQ;
  funcQ.push(&F);
  seen_funcs.insert(&F);
  while (!funcQ.empty())
  {
    Function *func = funcQ.front();
    funcQ.pop();
    auto callInstList = funcMap[func]->getCallInstList();
    for (auto ci : callInstList)
    {
      CallSite CS(ci);
      if (CS.isCall() && !CS.isIndirectCall())
      {
        if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
        {
          std::string called_func_name = called_func->getName().str();
          if (pdgUtils.IsBlackListFunc(called_func_name))
            continue;
          if (called_func->isDeclaration() || called_func->empty())
            continue;
          if (seen_funcs.find(called_func) != seen_funcs.end())
            continue;

          seen_funcs.insert(called_func);
          ret.push_back(called_func);
          funcQ.push(called_func);
        }
      }
    }
  }
  return ret;
}

int pdg::AccessInfoTracker::getCallOperandIdx(Value *operand, CallInst *callInst)
{
  int argNo = 0;
  for (auto arg_iter = callInst->arg_begin(); arg_iter != callInst->arg_end(); ++arg_iter)
  {
    if (Instruction *tmpInst = dyn_cast<Instruction>(&*arg_iter))
    {
      if (tmpInst == operand)
        return argNo;
    }
    argNo++;
  }
  if (argNo == callInst->getNumArgOperands())
    return -1;
  return argNo;
}

std::map<std::string, AccessType> pdg::AccessInfoTracker::computeInterprocAccessedFieldMap(Function &callee, unsigned argNo, DIType *parentNodeDIType, std::string fieldNameInCaller)
{
  std::map<std::string, AccessType> accessFieldMap;
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto funcW = funcMap[&callee];
  ArgumentWrapper *argW = funcW->getArgWByIdx(argNo);
  if (argW == nullptr)
    return accessFieldMap;
  computeIntraprocArgAccessInfo(argW, callee); //TODO: has opportunity for optimization by avoid recomputing intra access info
  for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
  {
    if ((*treeI)->getAccessType() == AccessType::NOACCESS)
      continue;
    std::string parentFieldName = "";
    std::string fieldName = DIUtils::getDIFieldName((*treeI)->getDIType());
    if (tree<InstructionWrapper *>::depth(treeI) != 0)
    {
      auto ParentI = tree<InstructionWrapper *>::parent(treeI);
      std::string fieldId = "";
      // only pointer and field has name. If this is the value pointed by pointer, we add a * at the end of field Id.
      if (DIUtils::isPointerType((*ParentI)->getDIType()))
        fieldId = DIUtils::computeFieldID((*ParentI)->getDIType(), (*ParentI)->getDIType()) + "*";
      else
        fieldId = DIUtils::computeFieldID((*ParentI)->getDIType(), (*treeI)->getDIType());
      if (!fieldId.empty())
        accessFieldMap.insert(std::make_pair(fieldId, (*treeI)->getAccessType()));
    }
    else
    {
      std::string fieldId = DIUtils::getDITypeName(parentNodeDIType) + fieldNameInCaller;
      if (!fieldId.empty())
        accessFieldMap.insert(std::make_pair(fieldId, (*treeI)->getAccessType()));
    }
  }
  return accessFieldMap;
}

std::set<Value *> pdg::AccessInfoTracker::findAliasInDomainWithOffset(Value &V, Function &F, unsigned offset, std::set<Function *> receiverDomainTrans)
{
  auto &pdgUtils = PDGUtils::getInstance();
  sea_dsa::DsaAnalysis *m_dsa = pdgUtils.getDsaAnalysis();
  sea_dsa::Graph *sourceFuncGraph = &m_dsa->getDsaAnalysis().getGraph(F);
  std::set<Value *> interprocAlias;
  if (!sourceFuncGraph)
    return interprocAlias;

  if (!sourceFuncGraph->hasCell(V))
    return interprocAlias;

  auto const &c1 = sourceFuncGraph->getCell(V);
  auto const &s1 = c1.getNode()->getAllocSites();

  for (Function *transFunc : receiverDomainTrans)
  {
    if (transFunc == &F)
      continue;
    sea_dsa::Graph *transFuncGraph = &m_dsa->getDsaAnalysis().getGraph(*transFunc);
    if (!transFuncGraph)
      assert(transFuncGraph && "cannot construct points to graph for transitive function.");

    for (auto instI = inst_begin(transFunc); instI != inst_end(transFunc); ++instI)
    {
      Instruction *curInst = &*instI;
      if (!transFuncGraph->hasCell(*curInst))
        continue;
      auto const &c2 = transFuncGraph->getCell(*curInst);
      auto const &s2 = c2.getNode()->getAllocSites();
      for (auto const a1 : s1)
      {
        if (s2.find(a1) != s2.end())
        {
          if (c2.getOffset() == offset)
            interprocAlias.insert(curInst);
        }
      }
    }
  }
  return interprocAlias;
}

std::set<Value *> pdg::AccessInfoTracker::findAliasInDomain(Value &V, Function &F, std::set<Function *> domainTransitiveClosure)
{
  auto &pdgUtils = PDGUtils::getInstance();
  std::set<Value *> aliasInDomain;
  sea_dsa::DsaAnalysis *m_dsa = pdgUtils.getDsaAnalysis();
  sea_dsa::Graph *sourceFuncGraph = &m_dsa->getDsaAnalysis().getGraph(F); // get source points to graph
  assert(sourceFuncGraph && "cannot construct points to graph for source cross-domain function.");

  for (Function *transFunc : domainTransitiveClosure)
  {
    if (transFunc == &F)
      continue;
    sea_dsa::Graph *transFuncGraph = &m_dsa->getDsaAnalysis().getGraph(*transFunc);
    assert(transFuncGraph && "cannot construct points to graph for transitive function.");
    for (auto I = inst_begin(transFunc); I != inst_end(transFunc); ++I)
    {
      Instruction *curInst = &*I;
      if (!sourceFuncGraph->hasCell(V) || !transFuncGraph->hasCell(*curInst))
        continue;
      // assert(sourceFuncGraph->hasCell(V) && transFuncGraph->hasCell(*I) && "cannot find cell in points to graph.");
      auto const &c1 = sourceFuncGraph->getCell(V);
      auto const &c2 = transFuncGraph->getCell(*I);
      auto const &s1 = c1.getNode()->getAllocSites();
      auto const &s2 = c2.getNode()->getAllocSites();
      for (auto const a1 : s1)
      {
        if (s2.find(a1) != s2.end())
        {
          aliasInDomain.insert(&*I);
          break;
        }
      }
    }
  }
  return aliasInDomain;
}

void pdg::AccessInfoTracker::computeFuncAccessInfoBottomUp(Function &F)
{
  // get the call chian ordered in a bottom up manner
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  // assume the call chain is asyclic
  std::vector<Function *> bottomUpCallChain = computeBottomUpCallChain(F);
  for (auto iter = bottomUpCallChain.rbegin(); iter != bottomUpCallChain.rend(); ++iter)
  {
    if (funcMap[*iter]->isVisited())
      continue;
    // errs() << (*iter)->getName() << " has tree?" << funcMap[*iter]->hasTrees() << "\n";
    computeFuncAccessInfo(**iter);
    funcMap[*iter]->setVisited(true);
  }
}

void pdg::AccessInfoTracker::computeGlobalVarsAccessInfo()
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  auto globalObjectTreePairs = PDG->getGlobalObjectTrees();
  for (auto globalObjectTreePair : globalObjectTreePairs)
  {
    auto objectTree = globalObjectTreePair.second;
    for (auto treeI = objectTree.begin(); treeI != objectTree.end(); ++treeI)
    {
      if (tree<InstructionWrapper *>::depth(treeI) < 1)
        continue;
      // hanlde static defined functions, assume all functions poineter that are linked with defined function in device driver module should be used by kernel.
      if (DIUtils::isFuncPointerTy((*treeI)->getDIType()))
      {
        std::string funcptrName = DIUtils::getDIFieldName((*treeI)->getDIType());
        std::string funcName = switchIndirectCalledPtrName(funcptrName);
        if (driverExportFuncPtrNameMap.find(funcName) != driverExportFuncPtrNameMap.end())
        {
          (*treeI)->setAccessType(AccessType::READ);
          usedCallBackFuncs.insert(funcName);
          continue;
        }
      }

      // get valdep pair, and check for intraprocedural accesses
      auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
      for (auto valDepPair : valDepPairList)
      {
        auto dataW = valDepPair.first->getData();
        AccessType accType = getAccessTypeForInstW(dataW);
        if (static_cast<int>(accType) > static_cast<int>((*treeI)->getAccessType()))
          (*treeI)->setAccessType(accType);
      }
    }
  }
}

void pdg::AccessInfoTracker::computeFuncAccessInfo(Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  FunctionWrapper *funcW = pdgUtils.getFuncMap()[&F];
  // for arguments
  for (auto argW : funcW->getArgWList())
    computeArgAccessInfo(argW, TreeType::FORMAL_IN_TREE);
  // for return value
  computeArgAccessInfo(funcW->getRetW(), TreeType::FORMAL_IN_TREE);
}

pdg::ArgumentMatchType pdg::AccessInfoTracker::getArgMatchType(Argument *arg1, Argument *arg2)
{
  Type *arg1_type = arg1->getType();
  Type *arg2_type = arg2->getType();

  if (arg1_type == arg2_type)
    return pdg::ArgumentMatchType::EQUAL;

  if (arg1_type->isPointerTy())
    arg1_type = (dyn_cast<PointerType>(arg1_type))->getElementType();

  if (arg1_type->isStructTy())
  {
    StructType *arg1_st_type = dyn_cast<StructType>(arg1_type);
    for (unsigned i = 0; i < arg1_st_type->getNumElements(); ++i)
    {
      Type *arg1_element_type = arg1_st_type->getElementType(i);
      bool type_match = (arg1_element_type == arg2_type);

      if (arg2_type->isPointerTy())
      {
        bool pointed_type_match = ((dyn_cast<PointerType>(arg2_type))->getElementType() == arg1_element_type);
        type_match = type_match || pointed_type_match;
      }

      if (type_match)
        return pdg::ArgumentMatchType::CONTAINED;
    }
  }

  return pdg::ArgumentMatchType::NOTCONTAINED;
}

void pdg::AccessInfoTracker::mergeArgAccessInfo(ArgumentWrapper *callerArgW, ArgumentWrapper *calleeArgW, tree<InstructionWrapper *>::iterator callerTreeI)
{
  if (callerArgW == nullptr || calleeArgW == nullptr)
    return;
  auto callerFunc = callerArgW->getArg()->getParent();
  auto calleeFunc = calleeArgW->getArg()->getParent();
  if (callerFunc == nullptr || calleeFunc == nullptr)
    return;

  unsigned callerParamTreeSize = callerArgW->getTree(TreeType::FORMAL_IN_TREE).size(callerTreeI);
  unsigned calleeParamTreeSize = calleeArgW->getTree(TreeType::FORMAL_IN_TREE).size();

  if (callerParamTreeSize != calleeParamTreeSize)
    return;

  auto calleeTreeI = calleeArgW->tree_begin(TreeType::FORMAL_IN_TREE);
  for (; callerTreeI != callerArgW->tree_end(TreeType::FORMAL_IN_TREE) && calleeTreeI != calleeArgW->tree_end(TreeType::FORMAL_IN_TREE); ++callerTreeI, ++calleeTreeI)
  {
    if (callerTreeI == 0 || calleeTreeI == 0)
      return;
    if (static_cast<int>((*callerTreeI)->getAccessType()) < static_cast<int>((*calleeTreeI)->getAccessType()))
    {
      (*callerTreeI)->setAccessType((*calleeTreeI)->getAccessType());
    }
  }
}

int pdg::AccessInfoTracker::getCallParamIdx(const InstructionWrapper *instW, const InstructionWrapper *callInstW)
{
  Instruction *inst = instW->getInstruction();
  Instruction *callInst = callInstW->getInstruction();
  if (inst == nullptr || callInst == nullptr)
    return -1;

  if (CallInst *CI = dyn_cast<CallInst>(callInst))
  {
    int paraIdx = 0;
    for (auto arg_iter = CI->arg_begin(); arg_iter != CI->arg_end(); ++arg_iter)
    {
      if (Instruction *tmpInst = dyn_cast<Instruction>(&*arg_iter))
      {
        if (tmpInst == inst)
          return paraIdx;
      }
      paraIdx++;
    }
  }
  return -1;
}

void pdg::AccessInfoTracker::printFuncArgAccessInfo(Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  errs() << "For function: " << F.getName() << "\n";
  FunctionWrapper *funcW = pdgUtils.getFuncMap()[&F];
  for (auto argW : funcW->getArgWList())
  {
    printArgAccessInfo(argW, TreeType::FORMAL_IN_TREE);
  }
  printArgAccessInfo(funcW->getRetW(), TreeType::FORMAL_IN_TREE);
  errs() << "......... [ END " << F.getName() << " ] .........\n\n";
}

void pdg::AccessInfoTracker::printArgAccessInfo(ArgumentWrapper *argW, TreeType ty)
{
  std::vector<std::string> access_name = {
      "No Access",
      "Read",
      "Write"};

  errs() << argW->getArg()->getParent()->getName() << " Arg use information for arg no: " << argW->getArg()->getArgNo() << "\n";
  errs() << "Size of argW: " << argW->getTree(ty).size() << "\n";

  for (auto treeI = argW->tree_begin(ty);
       treeI != argW->tree_end(ty);
       ++treeI)
  {
    if ((argW->getTree(ty).depth(treeI) > EXPAND_LEVEL))
      return;
    InstructionWrapper *curTyNode = *treeI;
    if (curTyNode->getDIType() == nullptr)
      return;

    Type *parentTy = curTyNode->getParentLLVMType();
    Type *curType = curTyNode->getLLVMType();

    errs() << "Num of child: " << tree<InstructionWrapper *>::number_of_children(treeI) << "\n";

    if (parentTy == nullptr)
    {
      errs() << "** Root type node **"
             << "\n";
      errs() << "Field name: " << DIUtils::getDIFieldName(curTyNode->getDIType()) << "\n";
      errs() << "Access Type: " << access_name[static_cast<int>(curTyNode->getAccessType())] << "\n";
      errs() << dwarf::TagString(curTyNode->getDIType()->getTag()) << "\n";
      errs() << ".............................................\n";
      continue;
    }

    errs() << "sub field name: " << DIUtils::getDIFieldName(curTyNode->getDIType()) << "\n";
    errs() << "Access Type: " << access_name[static_cast<int>(curTyNode->getAccessType())] << "\n";
    errs() << dwarf::TagString(curTyNode->getDIType()->getTag()) << "\n";
    errs() << "..............................................\n";
  }
}

void pdg::AccessInfoTracker::generateRpcForFunc(Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto funcW = funcMap[&F];
  DIType *func_ret_di_type = DIUtils::getFuncRetDIType(F);
  unsigned total_pointer_field_num = DIUtils::computeTotalPointerFieldNumberInStructType(func_ret_di_type);
  ksplit_stats_collector.IncreaseNumberOfPointer(total_pointer_field_num);
  errs() << "total ptr num: " << F.getName() << " ret - " << total_pointer_field_num << "\n";
  std::string ret_type_name = DIUtils::getDITypeName(func_ret_di_type);
  // when referencing a projection, discard the struct keyword.
  auto ret_argw = funcW->getRetW();
  if (DIUtils::isStructPointerTy(func_ret_di_type))
  {
    pdgUtils.stripStr(ret_type_name, "struct ");
    ret_type_name = "projection ret_" + ret_type_name;
    std::string ret_annotation = getReturnValAnnotationStr(F);
    ret_type_name += ret_annotation;
    collectKSplitStats(nullptr, func_ret_di_type, ret_annotation);
    collectKSplitSharedStats(nullptr, func_ret_di_type, ret_annotation);
  }
  // swap the function name with its registered function pointer to align with the IDL syntax
  std::string func_name = F.getName().str();
  std::string func_ptr_name = getRegisteredFuncPtrName(func_name);
  if (DIUtils::isPointerType(func_ret_di_type))
    printSharedPointerDebugLog(func_name, ret_type_name, "ret value", ret_argw->tree_begin(TreeType::FORMAL_IN_TREE));

  std::string rpc_prefix = "\trpc ";
  if (func_ptr_name != func_name)
    rpc_prefix = "\trpc_ptr ";

  idl_file << rpc_prefix << ret_type_name << " " << func_ptr_name;
  if (func_name.find("ioremap") != std::string::npos)
    idl_file << " [ioremap(caller)] ";
  idl_file << "( ";

  // collect stats for return value
  // handle parameters
  for (auto argW : pdgUtils.getFuncMap()[&F]->getArgWList())
  {
    Argument &arg = *argW->getArg();
    // infer in/out attributes, default assume in and no attribute is needed
    auto arg_tree_begin = argW->tree_begin(TreeType::FORMAL_IN_TREE);
    if (arg_tree_begin == argW->tree_end(TreeType::FORMAL_IN_TREE))
      continue;
    if (isSeqPointer(arg_tree_begin))
    {
      ksplit_stats_collector.IncreaseNumberOfSeqPointer();
      ksplit_stats_collector.IncreaseNumberOfSeqPointerOp();
      printSharedPointers(arg_tree_begin);
    }
    DIType* arg_di_type = (*arg_tree_begin)->getDIType();
    assert(arg_di_type != nullptr && "cannot generate rpc due to missing arg debugging type info");
    DIType *arg_lowest_di_type = DIUtils::getLowestDIType(arg_di_type);
    std::string arg_name = DIUtils::getArgName(arg);
    std::string arg_type_name = DIUtils::getRawDITypeName(arg_di_type);
    pdgUtils.stripStr(arg_type_name, "struct ");
    std::string annotation_str = computeNodeAnnotationStr(arg_tree_begin);
    bool is_ptr_has_unknown_annotation = (annotation_str.find("string") == std::string::npos && !DIUtils::isArrayType(arg_di_type));
    // infer annotation, such as alloc/dealloc if possible.
    if (DIUtils::isFuncPointerTy(arg_di_type))
    {
      Function *indirect_called_func = module->getFunction(switchIndirectCalledPtrName(arg_name));
      // assert((indirect_called_func != nullptr) && "cannot generate arg sig for empty indirect called func");
      if (indirect_called_func == nullptr)
      {
        errs() << "cannot generate arg sig for empty indirect called func " << func_name << "\n" ;
        continue;
      }
      // assumption 1: only driver domain exports function pointer to kernel.
      // assumption 2: the pointed function by this function pointer parameter is known.
      idl_file << "rpc_ptr " << arg_name << " " << arg_name;
      // DIUtils::getFuncSigName(DIUtils::getLowestDIType(arg_di_type), indirect_called_func, arg_name, "");
    }
    else if (DIUtils::isPointerType(arg_di_type))
    {
      unsigned total_arg_pointer_field = DIUtils::computeTotalPointerFieldNumberInStructType(arg_di_type);
      errs() << "total ptr num: " << F.getName() << " arg - " << total_arg_pointer_field << "\n";
      ksplit_stats_collector.IncreaseNumberOfPointer(total_arg_pointer_field);
      printSharedPointerDebugLog(func_name, arg_name, arg_name, arg_tree_begin);
      // for a pointer type parameter, we don't know if the pointer could point
      // to an array of elements. So, we need to infer it.
      // if current arg is a struct, need to generate projection keyword and strip struct keyword
      // if the pointed element is yet another pointer, need to put * before argName
      // all pointer could point to array, need to check if the pointed buffer could be an array
      if (arg_type_name.find("_ops") != std::string::npos)
        arg_name = "_global_" + arg_type_name;
      if (DIUtils::isStructPointerTy(arg_di_type))
        arg_type_name = "projection " + arg_name;
      uint64_t arrSize = getArrayArgSize(arg, F);
      std::string pointerLevelStr = DIUtils::computePointerLevelStr(arg_di_type);
      std::string arg_str = "";
      if (arrSize > 0)
      {
        if (is_ptr_has_unknown_annotation)
          ksplit_stats_collector.IncreaseNumberOfArray();
        // find a char array. We should treat this as a string
        if (arg_type_name.compare("char") == 0)
        {
          // char string is handled differently
          arg_str = arg_type_name + " " + annotation_str + " " + pointerLevelStr + arg_name;
          if (is_ptr_has_unknown_annotation)
            ksplit_stats_collector.IncreaseNumberOfCharArray();
        }
        else
        {
          arg_str = "array<" + arg_type_name + ", " + std::to_string(arrSize) + ">" + pointerLevelStr + " " + arg_name;
          ksplit_stats_collector.IncreaseNumberOfHandledArray();
        }
      }
      else
      {
        if (arg_type_name.find("var_len") != std::string::npos)
          ksplit_stats_collector.IncreaseNumberOfUnhandledArray();
        arg_str = arg_type_name + " " + annotation_str + " " + pointerLevelStr + arg_name;
      }
      // unHandledArrayNum++;
      idl_file << arg_str;
    }
    else
      idl_file << DIUtils::getArgTypeName(arg) << " " << arg_name;

    // collecting stats
    // if (DIUtils::isSentinelType(arg_lowest_di_type))
    //   log_file << "sentinel: " << arg_name << " - " << func_name << "\n";
    collectKSplitStats(nullptr, arg_di_type, annotation_str);
    collectKSplitSharedStats(nullptr, arg_di_type, annotation_str);
    if (argW->getArg()->getArgNo() < F.arg_size() - 1 && !arg_name.empty())
      idl_file << ", ";
  }
  idl_file << " )";
}

uint64_t pdg::AccessInfoTracker::getArrayArgSize(Value &V, Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto m_dsa = pdgUtils.getDsaAnalysis();
  sea_dsa::Graph *G = &m_dsa->getDsaAnalysis().getGraph(F);
  if (!G || !G->hasCell(V))
    return 0;
  auto const &c = G->getCell(V);
  auto const &s = c.getNode()->getAllocSites();
  for (auto const a1 : s)
  {
    Value *tmp = const_cast<Value *>(a1);
    if (AllocaInst *ai = dyn_cast<AllocaInst>(tmp))
    {
      PointerType *allocType = ai->getType();
      Type *pointedTy = allocType->getElementType();
      if (pointedTy->isArrayTy())
        return pointedTy->getArrayNumElements();
    }
    // TODO: need to handle dynamic allocated array
    if (CallInst *ci = dyn_cast<CallInst>(tmp))
    {
      CallSite CS(ci);
      if (!CS.isCall() || CS.isIndirectCall())
        continue;
      if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
      {
        std::string called_func_name = called_func->getName().str();
        if (IsAllocator(called_func_name))
        {
          if (IsCastedToArrayType(*ci))
            errs() << "[Warning]: find potential malloc array in function:" << called_func_name << "\n";
        }
      }
    }
  }
  return 0;
}

bool pdg::AccessInfoTracker::IsCastedToArrayType(Value& val)
{
  for (auto user : val.users())
  {
    if (BitCastInst *bci = dyn_cast<BitCastInst>(user))
    {
      if (bci->getOperand(0) == &val)
      {
        Type* casted_type = bci->getType();
        if (casted_type->isArrayTy())
          return true;
        while (casted_type->isPointerTy())
        {
          Type* element_type = casted_type->getPointerElementType();
          if (element_type->isArrayTy())
            return true;
          casted_type = element_type;
        }
      }
    }
  }
  return false;
}

bool pdg::AccessInfoTracker::mayAlias(Value &V1, Value &V2, Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto m_dsa = pdgUtils.getDsaAnalysis();
  sea_dsa::Graph *G = &m_dsa->getDsaAnalysis().getGraph(F);
  if (!G)
    return false;
  if (!G->hasCell(V1) || !G->hasCell(V2))
    return false;
  auto const &c1 = G->getCell(V1);
  auto const &c2 = G->getCell(V2);
  auto const &s1 = c1.getNode()->getAllocSites();
  auto const &s2 = c2.getNode()->getAllocSites();
  for (auto const a1 : s1)
  {
    if (s2.find(a1) != s2.end())
      return true;
  }
  return false;
}

std::set<Instruction *> pdg::AccessInfoTracker::getIntraFuncAlias(Instruction *inst)
{
  Function *F = inst->getFunction();
  std::set<Instruction *> aliasSet;
  aliasSet.insert(inst);
  for (auto instI = inst_begin(F); instI != inst_end(F); instI++)
  {
    if (&*instI == inst)
      continue;
    if (mayAlias(*inst, *instI, *F))
      aliasSet.insert(&*instI);
  }
  return aliasSet;
}

std::string pdg::AccessInfoTracker::getReturnValAnnotationStr(Function &F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto m_dsa = pdgUtils.getDsaAnalysis();
  sea_dsa::Graph *G = &m_dsa->getDsaAnalysis().getGraph(F);
  auto funcMap = pdgUtils.getFuncMap();
  auto funcW = funcMap[&F];
  auto retInstList = funcW->getReturnInstList();
  for (auto retInst : retInstList)
  {
    if (!isa<Instruction>(retInst->getReturnValue()))
      continue;
    Instruction *retVal = cast<Instruction>(retInst->getReturnValue());
    std::set<InstructionWrapper*> retValAliasSet;
    PDG->getAllAlias(retVal, retValAliasSet);
    for (auto aliasW : retValAliasSet)
    {
      auto aliasInst = aliasW->getInstruction();
      CallSite CS(aliasInst);
      if (CS.isCall() && !CS.isIndirectCall())
      {
        if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
        {
          if (called_func->isDeclaration() || called_func->empty())
            continue;
          std::string calleeRetStr = getReturnValAnnotationStr(*called_func);
          if (!calleeRetStr.empty())
            return calleeRetStr;
        }
      }

      if (!G->hasCell(*aliasInst))
        continue;
      auto const &c = G->getCell(*aliasInst);
      auto const &s = c.getNode()->getAllocSites();
      for (auto const a : s)
      {
        Value *tempV = const_cast<Value *>(a);
        CallSite CS(tempV);
        if (!CS.isCall() || CS.isIndirectCall())
          continue;

        if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
        {
          std::string called_func_name = called_func->getName().str();
          if (IsAllocator(called_func_name))
            return "[alloc(caller)]";
        }
      }
    }
  }
  return "";
}

void pdg::AccessInfoTracker::generateIDLforFunc(Function &F)
{
  // if a function is defined on the same side, no need to generate IDL rpc for this function.
  std::string funcName = F.getName().str();
  errs() << "Start generating IDL for " << funcName << "\n";
  // here, we don't generate any projection and rpc for indirect functions that are
  // never used in interface functions
  if (driverExportFuncPtrNameMap.find(funcName) != driverExportFuncPtrNameMap.end())
  {
    if (usedCallBackFuncs.find(funcName) == usedCallBackFuncs.end())
      return;
  }

  // if (driverExportFuncPtrNameMap.find(F.getName().str()) == driverExportFuncPtrNameMap.end())
  generateRpcForFunc(F);
  auto &pdgUtils = PDGUtils::getInstance();
  FunctionWrapper *funcW = pdgUtils.getFuncMap()[&F];
  idl_file << " {\n";
  for (auto argW : funcW->getArgWList())
  {
    generateIDLforArg(argW);
  }
  generateIDLforArg(funcW->getRetW());
  idl_file << "\t}\n\n";
  // don't generate rpc for driver export function pointers
}

// the purpose of this function is synchronizing data that won't sync correctly due to lack of cross-domain function call
// void pdg::AccessInfoTracker::generateSyncDataStubAtFuncEnd(Function &F)
// {
//   // handle global variable accesses
//   // 1. check if function access global variable
//   std::string funcName = F.getName().str();
//   auto globalObjectTrees = PDG->getGlobalObjectTrees();
//   for (auto globalObjectTreePair : globalObjectTrees)
//   {
//     auto globalVar = globalObjectTreePair.first;
//     bool accessedInFunc = false;
//     for (auto user : globalVar->users())
//     {
//       if (Instruction *i = dyn_cast<Instruction>(user))
//       {
//         if (i->getFunction() == &F)
//         {
//           accessedInFunc = true;
//           break;
//         }
//       }
//     }
//     if (!accessedInFunc)
//       continue;
//     auto globalObjectTree = globalObjectTreePair.second;
//     // if global variable is accessed in the function, start generating idl for the global which summarize the accessed data
//     auto treeBegin = globalObjectTree.begin();
//     std::queue<tree<InstructionWrapper *>::iterator> treeNodeQ;
//     treeNodeQ.push(treeBegin);
//     while (!treeNodeQ.empty())
//     {
//       auto treeI = treeNodeQ.front();
//       treeNodeQ.pop();
//       // generate projection for the current node.
//       DIType *curDIType = (*treeI)->getDIType();
//       if (!curDIType)
//         continue;
//       DIType *lowestDIType = DIUtils::getLowestDIType(curDIType);
//       if (!DIUtils::isProjectableTy(lowestDIType))
//         continue;
//       for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
//       {
//         auto childI = tree<InstructionWrapper *>::child(treeI, i);
//         bool isAccessed = ((*childI)->getAccessType() != AccessType::NOACCESS);
//         if (!isAccessed)
//           continue;
//         auto childDITy = (*childI)->getDIType();
//         childDITy = DIUtils::getLowestDIType(childDITy);
//         if (DIUtils::isProjectableTy(childDITy))
//           treeNodeQ.push(childI);
//       }
//       std::string str;
//       raw_string_ostream OS(str);
//       // idl_file << "Insert sync stab at end of function: " << funcName << "\n";
//       std::string globalVarName = DIUtils::getDIFieldName(DIUtils::getGlobalVarDIType(*globalVar));
//       generateProjectionForTreeNode(treeI, OS, globalVarName);
//       std::string structName = DIUtils::getDIFieldName(curDIType);
//       if (structName.find("ops") == std::string::npos)
//         structName = structName + "_" + funcName;
//       idl_file << "=== Data Sync at end of function " << funcName << " ===\n\tprojection < struct " << structName << "> " << structName << " {\n " << OS.str() << "\t}\n\n";
//     }
//   }
// }

void pdg::AccessInfoTracker::generateProjectionForGlobalVarInFunc(tree<InstructionWrapper *>::iterator treeI, raw_string_ostream &OS, DIType *parentNodeDIType, Function &func)
{
  auto curDIType = (*treeI)->getDIType();
  if (curDIType == nullptr)
    return;
  for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
  {
    auto childI = tree<InstructionWrapper *>::child(treeI, i);
    auto childDITy = (*childI)->getDIType();
    // check if field is private
    bool isPrivateField = (!isChildFieldShared(parentNodeDIType, childDITy));
    bool isFieldAccess = ((*childI)->getAccessType() != AccessType::NOACCESS);
    if (!isFieldAccess || isPrivateField)
      continue;
    // also check if this field is accessed in the target function
    auto treeNodeDepPairs = PDG->getNodesWithDepType(*childI, DependencyType::VAL_DEP);
    bool accessedInTargetFunc = false;
    for (auto depPair : treeNodeDepPairs)
    {
      InstructionWrapper *depInstW = const_cast<InstructionWrapper *>(depPair.first->getData());
      DependencyType depType = depPair.second;
      Instruction *depInst = depInstW->getInstruction();
      if (!depInst)
        continue;
      Function *depFunc = depInst->getFunction();
      if (depFunc == &func)
      {
        accessedInTargetFunc = true;
        break;
      }
    }
    if (!accessedInTargetFunc)
      continue;

    auto childLowestTy = DIUtils::getLowestDIType(childDITy);
    if (DIUtils::isFuncPointerTy(childLowestTy))
    {
      // generate rpc for the indirect function call
      std::string funcName = DIUtils::getDIFieldName(childDITy);
      // generate rpc for defined function in isolated driver. Also, if kernel hook a function to a function pointer, we need to caputre this write
      if (driverExportFuncPtrNames.find(funcName) == driverExportFuncPtrNames.end())
        continue;
      // for each function pointer, swap it with the function name registered to the pointer.
      funcName = switchIndirectCalledPtrName(funcName);
      Function *indirectFunc = module->getFunction(funcName);
      if (indirectFunc == nullptr)
        continue;
      OS << "\t\trpc " << DIUtils::getFuncSigName(DIUtils::getLowestDIType(childDITy), indirectFunc, DIUtils::getDIFieldName(childDITy)) << ";\n";
    }
    else if (DIUtils::isStructTy(childLowestTy))
    {
      std::string funcName = (*treeI)->getFunction()->getName().str();
      auto fieldTypeName = DIUtils::getDITypeName(childDITy);
      // formatting functions
      while (fieldTypeName.back() == '*')
      {
        fieldTypeName.pop_back();
      }

      std::string constStr = "const struct";
      auto pos = fieldTypeName.find(constStr);
      std::string projectStr = "projection ";
      if (pos != std::string::npos)
      {
        fieldTypeName = fieldTypeName.substr(pos + constStr.length() + 1);
        projectStr = "const " + projectStr;
      }

      OS << "\t\t"
         << projectStr << fieldTypeName << " "
         << " *" << DIUtils::getDIFieldName(childDITy) << "_" << funcName << ";\n";
    }
    else if (DIUtils::isUnionTy(childLowestTy))
    {
      OS << "\t\t"
         << "// union type \n";
    }
    else
    {
      std::string fieldName = DIUtils::getDIFieldName(childDITy);
      if (!fieldName.empty())
      {
        OS << "\t\t" + DIUtils::getDITypeName(childDITy) << " " << getAccessAttributeName(childI) << " " << DIUtils::getDIFieldName(childDITy) << ";\n";
      }
    }
  }
}

// receive a tree iterator and start
void pdg::AccessInfoTracker::generateProjectionForTreeNode(tree<InstructionWrapper *>::iterator treeI, raw_string_ostream &OS, std::string arg_name, std::queue<tree<InstructionWrapper *>::iterator> &pointer_queue, bool is_func_ptr_export_from_driver, std::string parent_struct_indent_level)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  auto struct_di_type = (*treeI)->getDIType();
  if (struct_di_type == nullptr)
    return;
  std::string field_indent_level = parent_struct_indent_level + "\t";
  std::string func_name = (*treeI)->getFunction()->getName().str();
  for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
  {
    ksplit_stats_collector.IncreaseTotalNumberOfField();
    auto childI = tree<InstructionWrapper *>::child(treeI, i);
    auto struct_field_di_type = (*childI)->getDIType();
    auto struct_field_lowest_di_type = DIUtils::getLowestDIType(struct_field_di_type);
    std::string field_annotation = computeNodeAnnotationStr(childI);

    if (isSeqPointer(childI))
      ksplit_stats_collector.IncreaseNumberOfSeqPointer();
    collectKSplitStats(struct_di_type, struct_field_di_type, field_annotation);
    // check if current field is accessed
    bool is_field_accessed = ((*childI)->getAccessType() != AccessType::NOACCESS);
    if (!is_field_accessed)
    {
      ksplit_stats_collector.IncreaseNumberOfNoAccessedFields();
      if (struct_field_lowest_di_type != nullptr)
        ksplit_stats_collector.IncreaseSavedDataSizeUseProjection(struct_field_lowest_di_type->getSizeInBits() / 8); // count in byte
      continue;
    }
    
    // check if field is private
    bool is_shared_field = false;
    if (SHARED_DATA_FLAG)
      is_shared_field = isChildFieldShared(struct_di_type, struct_field_di_type);

    if (!is_shared_field)
    {
      ksplit_stats_collector.IncreaseNumberOfEliminatedPrivateField();
      if (struct_field_lowest_di_type != nullptr)
        ksplit_stats_collector.IncreaseSavedDataSizeUseSharedData(struct_field_lowest_di_type->getSizeInBits() / 8); // count in byte
      continue;
    }
    ksplit_stats_collector.IncreaseNumberOfProjectedField();

    std::string fieldID = DIUtils::computeFieldID(struct_di_type, struct_field_di_type);

    if (DIUtils::isPointerType(struct_field_di_type))
    {
      printSharedPointers(childI);
      printSharedPointerDebugLog(func_name, arg_name, fieldID, childI);
    }
    // start generaeting IDL for each field
    if (DIUtils::isFuncPointerTy(struct_field_lowest_di_type))
    {
      // generate rpc for the indirect function call
      std::string func_ptr_name = DIUtils::getDIFieldName(struct_field_di_type);
      // generate rpc for defined function in isolated driver. Also, if kernel hook a function to a function pointer, we need to caputre this write
      if (driverExportFuncPtrNames.find(func_ptr_name) == driverExportFuncPtrNames.end())
        continue;
      // for each function pointer, swap it with the function name registered to the pointer.
      std::string indirect_called_func_name = switchIndirectCalledPtrName(func_ptr_name);
      Function *indirect_called_func = module->getFunction(indirect_called_func_name);
      if (indirect_called_func == nullptr)
        continue;
      OS << field_indent_level << "rpc " << DIUtils::getFuncSigName(DIUtils::getLowestDIType(struct_field_di_type), indirect_called_func, DIUtils::getDIFieldName(struct_field_di_type)) << ";\n";
    }
    else if (DIUtils::isStructPointerTy(struct_field_di_type))
    {
      // for sturct pointer, we genereate a seperate projection, and insert a reference to that projection. Here, we insert the reference.
      std::string func_name = "";
      if ((*treeI)->getFunction())
        func_name = (*treeI)->getFunction()->getName().str();
      std::string struct_field_raw_type_name = DIUtils::getRawDITypeName(struct_field_di_type);
      std::string struct_field_name = DIUtils::getDIFieldName(struct_field_di_type);
      pdgUtils.stripStr(struct_field_raw_type_name, "struct ");
      
      std::string field_name_prefix = arg_name;
      if (struct_field_name.find("_ops") != std::string::npos)
      {
        field_name_prefix = "_global";
      }

      OS << field_indent_level
         << "projection "
         << field_name_prefix << "_" << struct_field_name
         << field_annotation
         << " *"
         << struct_field_name
         << ";\n";

      std::string struct_raw_type_name = DIUtils::getRawDITypeName(struct_di_type);
      // if a struct field is a linked list, we check if the parent is the same type. If not, we increase
      // the number of found sentinel array. Otherwise, avoid repeat counting.
      // if (DIUtils::isSentinelType(struct_field_lowest_di_type))
      //   log_file << "sentinel: " << " - " << arg_name << " - " << struct_field_name << " - " << func_name << "\n";
      pointer_queue.push(childI);
    }
    else if (DIUtils::isProjectableTy(struct_field_di_type))
    {
      std::string func_name = "";
      if ((*treeI)->getFunction())
        func_name = (*treeI)->getFunction()->getName().str();
      // assumption: if a child field is an struct or union, we consider them as anonymous. So, we directly put these fields in the projection.
      std::string sub_fields_str;
      raw_string_ostream nested_fields_str(sub_fields_str);
      std::string struct_field_name = DIUtils::getDIFieldName(struct_field_di_type);
      // need to collect pointers nested in struct type field
      generateProjectionForTreeNode(childI, nested_fields_str, arg_name, pointer_queue, is_func_ptr_export_from_driver, field_indent_level);
      if (nested_fields_str.str().empty())
        continue;
      // for struct and union, if the field doesn't has a name, then this is an anonymous struct or union. 
      // in this case, directly generated nested projection
      if (struct_field_name.empty())
      {
        OS << nested_fields_str.str();
      }
      else
      {
        OS << field_indent_level
           << "projection " 
           << " {\n"
           << nested_fields_str.str()
           << field_indent_level
           << "} " 
           << struct_field_name 
           <<";\n";
      }
      // if (DIUtils::isSentinelType(struct_field_lowest_di_type))
      //   log_file << "sentinel: " << " - " << arg_name << " - " << struct_field_name << " - " << func_name << "\n";
    }
    else
    {
      std::string type_name = DIUtils::getDITypeName(struct_field_di_type);
      std::string field_name = DIUtils::getDIFieldName(struct_field_di_type);
      // handle case in which the current field is accessed in buffer manipulation function, such as memcpy
      if (global_array_fields_.find(fieldID) != global_array_fields_.end())
      {
        ksplit_stats_collector.IncreaseNumberOfArray();
        // find a char array
        if (DIUtils::isBasicTypePointer(struct_field_di_type))
        {
          std::string pointer_str = DIUtils::computePointerLevelStr(struct_field_di_type);
          std::string raw_field_name = DIUtils::getRawDITypeName(struct_field_di_type);
          OS << field_indent_level << "array<" << raw_field_name << ", " << "var_len>" << pointer_str << " " << field_name << ";\n";
          ksplit_stats_collector.IncreaseNumberOfUnhandledArray();
          if (DIUtils::isCharPointer(struct_field_di_type))
            ksplit_stats_collector.IncreaseNumberOfCharArray();
        }
      }
      else
      {
        if (!field_name.empty())
          OS << field_indent_level << type_name << " " << field_annotation << " " << field_name << ";\n";
      }
    }
    // collect union number stats
    collectKSplitSharedStats(struct_di_type, struct_field_di_type, field_annotation);
  }
}

void pdg::AccessInfoTracker::generateIDLforArg(ArgumentWrapper *argW)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  if (argW->getTree(TreeType::FORMAL_IN_TREE).size() == 0)
    return;
  Function &F = *argW->getFunc();
  std::string funcName = F.getName().str();
  bool is_func_ptr_export_from_driver = IsFuncPtrExportFromDriver(funcName);
  // funcName = getRegisteredFuncPtrName(funcName);
  std::string arg_name = DIUtils::getArgName(*(argW->getArg()));
  auto treeBegin = argW->tree_begin(TreeType::FORMAL_IN_TREE);
  DIType *argDIType = (*treeBegin)->getDIType();
  // for return value, we use "ret_" + type name as the return value's name 
  if (pdgUtils.isReturnValue(*argW->getArg()))
  {
    auto argDITypeStr = DIUtils::getRawDITypeName(argDIType);
    pdgUtils.stripStr(argDITypeStr, "struct ");
    arg_name = "ret_" + argDITypeStr;
  }

  std::queue<tree<InstructionWrapper *>::iterator> treeNodeQ;
  treeNodeQ.push(treeBegin);
  std::queue<tree<InstructionWrapper *>::iterator> funcPtrQ;
  // used for counting sentienl type
  while (!treeNodeQ.empty())
  {
    auto treeI = treeNodeQ.front();
    treeNodeQ.pop();
    // generate projection for the current node.
    DIType *current_struct_di_type = (*treeI)->getDIType();
    DIType *current_struct_lowest_di_type = DIUtils::getLowestDIType(current_struct_di_type);

    if (!DIUtils::isPointerToProjectableTy(current_struct_di_type))
      continue;
    treeI++;
    if (treeI == argW->tree_end(TreeType::FORMAL_IN_TREE))
      continue;
    
    // // append child node needs projection to queue
    // for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
    // {
    //   auto childI = tree<InstructionWrapper *>::child(treeI, i);
    //   bool isAccessed = ((*childI)->getAccessType() != AccessType::NOACCESS);
    //   if (!isAccessed)
    //     continue;
    //   // if child field is struct or pointer to struct, we need to generate projection for it
    //   auto struct_field_di_type = (*childI)->getDIType();
    //   auto struct_field_lowest_di_type = DIUtils::getLowestDIType(struct_field_di_type);
    //   // The current struct should always be struct or union pointer, since we don't compute shared data for union using type, we only check if a 
    //   if (!DIUtils::isPointerToProjectableTy(current_struct_di_type))
    //   {
    //     if (SHARED_DATA_FLAG)
    //     {
    //       if (!isChildFieldShared(current_struct_di_type, struct_field_di_type) && !is_func_ptr_export_from_driver)
    //         continue;
    //     }
    //   }
    // }

    // No projection is needed for pointer. Only for the underlying object
    // if (DIUtils::isStructPointerTy(current_struct_di_type))
    //   continue;
    std::string projectionReferenceName = DIUtils::getDIFieldName(current_struct_di_type);
    std::string projectionTypeName = DIUtils::getRawDITypeName(current_struct_lowest_di_type);
    std::string projectionRawTypeName = projectionTypeName;
    pdgUtils.stripStr(projectionRawTypeName, "struct ");
    // struct node's di type don't store naming info. Need to go to the parent node for fetching the naming information
    if (projectionRawTypeName == projectionReferenceName)
    {
      auto parentI = getParentIter(treeI);
      projectionReferenceName = DIUtils::getDIFieldName((*parentI)->getDIType());
    }
    // naming for struct field
    if (!pdgUtils.isRootNode(treeI))
    {
      if (DIUtils::isStructPointerTy(current_struct_di_type) || DIUtils::isStructTy(current_struct_di_type))
      {
        if (projectionTypeName.find("ops") == std::string::npos)
          projectionReferenceName = arg_name + "_" + projectionReferenceName;
      }
    }
    else
    {
      // naming for root pointer / object
      projectionReferenceName = arg_name;
    }

    std::string str;
    raw_string_ostream arg_projection(str);
    // nested pointers are the pointers inside the generated struct fields.
    generateProjectionForTreeNode(treeI, arg_projection, arg_name, treeNodeQ, is_func_ptr_export_from_driver);
    // special handling for global op structs
    if (projectionTypeName.find("_ops") != std::string::npos)
    {
      // find the first ops projection with fields inside
      if (seenFuncOps.find(projectionTypeName) != seenFuncOps.end())
        continue;
      seenFuncOps.insert(projectionTypeName);
      std::string projStr = "\t\tprojection < " + projectionTypeName + " > " + "_global_" + projectionReferenceName + " {\n " + arg_projection.str() + "\t\t};\n";
      globalOpsStr = globalOpsStr + "\n" + projStr;
    }
    else
    {
      idl_file << "\t\tprojection < " << projectionTypeName << " > " << projectionReferenceName << " {\n " << arg_projection.str() << "\t\t};\n";
    }
  }
}

bool pdg::AccessInfoTracker::IsAllocator(std::string func_name)
{
  auto &pdgUtils = PDGUtils::getInstance();
  std::string func_name_without_version_num = pdgUtils.StripFuncnameVersionNumber(func_name);
  for (auto allocatorWrapper : allocatorWrappers)
  {
    if (func_name.find(allocatorWrapper) != std::string::npos)
      return true;
  }
  return false;
}

bool pdg::AccessInfoTracker::IsStringOps(std::string func_name)
{
  auto &pdgUtils = PDGUtils::getInstance();
  func_name = pdgUtils.StripFuncnameVersionNumber(func_name);
  for (auto str_op_name : stringOperations)
  {
    if (func_name.compare(str_op_name) == 0)
      return true;
  }
  return false;
}

bool pdg::AccessInfoTracker::IsMemOps(std::string func_name)
{
  auto &pdgUtils = PDGUtils::getInstance();
  func_name = pdgUtils.StripFuncnameVersionNumber(func_name);
  for (auto mem_op_name : memOperations)
  {
    if (func_name.find(mem_op_name) != std::string::npos)
      return true;
  }
  return false;
}

bool pdg::AccessInfoTracker::IsStoreOfAlias(StoreInst* store_inst)
{
  if (store_inst == nullptr)
    return false;
  Value* stored_val = store_inst->getValueOperand();
  Value* stored_addr = store_inst->getPointerOperand();
  if (LoadInst *li = dyn_cast<LoadInst>(stored_val))
  {
    // check if the load is loaded from another pointer type
    Value* load_addr = li->getPointerOperand();
    if (load_addr->getType() == stored_addr->getType())
      return true;
  }
  return false;
}

std::string pdg::AccessInfoTracker::computeNodeAnnotationStr(tree<InstructionWrapper *>::iterator tree_node_iter)
{
  std::set<std::string> annotations;
  std::set<Function*> visited_funcs;
  InferTreeNodeAnnotation(tree_node_iter, annotations, visited_funcs);
  std::string annotation_str = "";
  for (auto annotation : annotations)
  {
    annotation_str += annotation;
  }
  return annotation_str;
}

std::string pdg::AccessInfoTracker::inferTreeNodeStringAnnotation(tree<InstructionWrapper *>::iterator tree_node_iter, std::set<Function*> &visited_funcs)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto instMap = pdgUtils.getInstMap();
  auto parent_node_iter = getParentIter(tree_node_iter);
  DIType* tree_node_di_type = (*tree_node_iter)->getDIType();
  DIType* parent_node_di_type = (*parent_node_iter)->getDIType();
  std::string fieldID = DIUtils::computeFieldID(parent_node_di_type, tree_node_di_type);
  auto addr_var_ws= PDG->getDepInstWrapperWithDepType(*tree_node_iter, DependencyType::VAL_DEP);
  for (auto addr_var_w : addr_var_ws)
  {
    if (!DIUtils::isCharPointer(tree_node_di_type))
      continue;
    if (global_string_struct_fields_.find(fieldID) != global_string_struct_fields_.end())
      return "[string]";
    Instruction* addr_var_inst = addr_var_w->getInstruction();
    for (auto addr_var_inst_user : addr_var_inst->users())
    {
      if (LoadInst *li = dyn_cast<LoadInst>(addr_var_inst_user))
      {
        for (auto li_user : li->users())
        {
          CallSite CS(li_user);
          if (!CS.isCall() || CS.isIndirectCall())
            continue;
          if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
          {
            std::string called_func_name = called_func->getName().str();
            called_func_name = pdgUtils.StripFuncnameVersionNumber(called_func_name);
            errs() << "called function after strip version num: " << called_func_name << "\n";
            if (IsStringOps(called_func_name))
              return "[string]";
          }
        }
      }
    }

    std::set<Instruction *> call_insts_w_on_addr_var;
    PDG->getDepInstsWithDepType(addr_var_w->getInstruction(), DependencyType::DATA_CALL_PARA, call_insts_w_on_addr_var);
    for (auto call_inst : call_insts_w_on_addr_var)
    {
      if (call_inst == nullptr)
        continue;
      CallSite CS(call_inst);
      if (!CS.isCall() || CS.isIndirectCall())
        continue;
      if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
      {
        if (called_func->isDeclaration())
          continue;
        auto func_w = funcMap[called_func];
        if (!func_w->hasTrees())
          continue;
        // avoid recursive calls
        if (visited_funcs.find(called_func) != visited_funcs.end())
          continue;
        visited_funcs.insert(called_func);
        unsigned argIdx = getCallParamIdx(instMap[addr_var_inst], instMap[call_inst]);
        auto callee_arg_w = func_w->getArgWByIdx(argIdx);
        if (callee_arg_w == nullptr) // this could happen for varidic function
          continue;
        auto callee_arg_tree_begin = callee_arg_w->tree_begin(TreeType::FORMAL_IN_TREE);
        if (callee_arg_tree_begin == callee_arg_w->tree_end(TreeType::FORMAL_IN_TREE))
          continue;
        return inferTreeNodeStringAnnotation(callee_arg_tree_begin, visited_funcs);
      }
    }
  }
  return "";
}

void pdg::AccessInfoTracker::InferTreeNodeAnnotation(tree<InstructionWrapper *>::iterator tree_node_iter, std::set<std::string> &annotations, std::set<Function *> &visited_funcs)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto parent_node_iter = getParentIter(tree_node_iter);
  // compute field id
  DIType* tree_node_di_type = (*tree_node_iter)->getDIType();
  std::string string_annotation = inferTreeNodeStringAnnotation(tree_node_iter, visited_funcs);
  if (!string_annotation.empty())
    annotations.insert(string_annotation);
  // obtain address variables for a tree node
  auto addr_var_wrappers = PDG->getDepInstWrapperWithDepType(*tree_node_iter, DependencyType::VAL_DEP);
  // analyze the accesses to the address variable
  for (auto addr_var_w : addr_var_wrappers)
  {
    // first infer the access type for the tree node
    auto accType = getAccessTypeForInstW(addr_var_w);
    if (accType == AccessType::WRITE)
      annotations.insert("[out]");
    auto addr_var_inst = addr_var_w->getInstruction();
    assert(addr_var_inst != nullptr && "cannot analyze nullptr address var");
    // start infering string / alloc(caller) annotation
    std::set<Instruction *> user_insts_on_addr_var;
    PDG->getDepInstsWithDepType(addr_var_inst, DependencyType::DATA_DEF_USE, user_insts_on_addr_var);
    for (auto user_inst : user_insts_on_addr_var)
    {
      if (StoreInst *si = dyn_cast<StoreInst>(user_inst))
      {
        if (si->getPointerOperand() != addr_var_inst)
          continue;
        auto stored_val = si->getValueOperand();
        if (auto stored_val_inst = dyn_cast<Instruction>(stored_val))
        {
          std::set<InstructionWrapper *> alias_set;
          PDG->getAllAlias(stored_val_inst, alias_set);
          for (auto alias_w : alias_set)
          {
            auto alias_inst = alias_w->getInstruction();
            CallSite CS(alias_inst);
            if (!CS.isCall() || CS.isIndirectCall())
              continue;
            if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
            {
              if (called_func->isDeclaration())
                continue;
              std::string called_func_name = called_func->getName().str();
              called_func_name = pdgUtils.StripFuncnameVersionNumber(called_func_name);
              if (IsAllocator(called_func_name))
                annotations.insert("[alloc(caller)]");
              if (deallocatorWrappers.find(called_func_name) != deallocatorWrappers.end())
                annotations.insert("[dealloc(caller)]");
            }
          }
        }
      }
    }
  }
}

// receive a tree wrapper
std::string pdg::getAccessAttributeName(tree<InstructionWrapper *>::iterator treeI)
{
  std::vector<std::string> access_attribute = {
      "",
      "",
      "[out]"};
  int accessIdx = static_cast<int>((*treeI)->getAccessType());
  return access_attribute[accessIdx];
}

std::string pdg::AccessInfoTracker::getArgAccessInfo(Argument &arg)
{
  auto &pdgUtils = PDGUtils::getInstance();
  std::vector<std::string> mod_info = {"U", "R", "W", "T"};
  ArgumentWrapper *argW = pdgUtils.getFuncMap()[arg.getParent()]->getArgWByArg(arg);
  return mod_info[static_cast<int>((*argW->getTree(TreeType::FORMAL_IN_TREE).begin())->getAccessType())];
}

// compute shared data
void pdg::AccessInfoTracker::computeSharedDataInFunc(Function &F)
{
  // for each argument, computes it debugging information type
  for (auto &arg : F.args())
  {
    // do not process non-struct ptr type, struct type is coersed
    DIType *argDIType = DIUtils::getArgDIType(arg);
    if (!DIUtils::isStructPointerTy(argDIType))
      continue;
    // check if shared fields for this struct type is already done
    std::string argTypeName = DIUtils::getArgTypeName(arg);
    if (sharedDataTypeMap.find(argTypeName) == sharedDataTypeMap.end())
    {
      sharedDataTypeMap.insert(std::pair<std::string, std::set<std::string>>(argTypeName, std::set<std::string>()));
      diTypeNameMap[argTypeName] = argDIType;
    }
    std::set<std::string> accessedFieldsInArg = computeSharedDataForType(argDIType);
    sharedDataTypeMap[argTypeName].insert(accessedFieldsInArg.begin(), accessedFieldsInArg.end());
  }
}

std::set<std::string> pdg::AccessInfoTracker::computeSharedDataForType(DIType *dt)
{
  // compute accessed fields in driver/kernel domain separately.
  std::set<std::string> accessedFieldsInDriver = computeAccessedDataInDomain(dt, driver_domain_funcs_);
  std::set<std::string> accessedFieldsInKernel = computeAccessedDataInDomain(dt, kernel_domain_funcs_);
  // then, take the union of the accessed fields in the two domains.
  std::set<std::string> sharedFields;
  for (auto str : accessedFieldsInDriver)
  {
    if (accessedFieldsInKernel.find(str) != accessedFieldsInKernel.end())
      sharedFields.insert(str);
  }
  return sharedFields;
}

std::set<std::string> pdg::AccessInfoTracker::computeAccessedDataInDomain(DIType *dt, std::set<Function *> domain)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  std::set<std::string> accessedFieldsInDomain;
  for (auto func : domain)
  {
    auto funcW = funcMap[func];
    for (auto &arg : func->args())
    {
      DIType *argDIType = DIUtils::getArgDIType(arg);
      // check if debugging type matches
      auto n1 = DIUtils::getDITypeName(argDIType);
      auto n2 = DIUtils::getDITypeName(dt);
      if (n1 != n2)
        continue;
      // if type names match, compute the accessed fields id and store it.
      ArgumentWrapper *argW = funcW->getArgWByArg(arg);
      auto argFormalTree = argW->getTree(TreeType::FORMAL_IN_TREE);
      std::set<std::string> accessedFields = computeAccessedFieldsForDIType(argFormalTree, argDIType);
      accessedFieldsInDomain.insert(accessedFields.begin(), accessedFields.end());
      // special handling: compute accessed fields in asynchronous call. Need to put all such fields into the transitive closure of cross-domain call function
      // if (asyncCalls.find(func) != asyncCalls.end())
      //   accessedFieldsInAsyncCalls.insert(accessedFields.begin(), accessedFields.end());
    }
  }
  return accessedFieldsInDomain;
}

std::set<std::string> pdg::AccessInfoTracker::computeAccessedFieldsForDIType(tree<InstructionWrapper *> objectTree, DIType *rootDIType)
{
  std::set<std::string> accessedFields;
  for (auto treeI = objectTree.begin(); treeI != objectTree.end(); ++treeI)
  {
    // hanlde function pointer exported by driver domain
    DIType *fieldDIType = (*treeI)->getDIType();
    std::string fieldName = DIUtils::getDIFieldName(fieldDIType);
    if (fieldName.find("_ops") != std::string::npos)
    {
      std::string fieldID = DIUtils::computeFieldID(rootDIType, fieldDIType);
      accessedFields.insert(fieldID);
      globalFieldAccessInfo.insert(std::make_pair(fieldID, AccessType::READ));
      continue;
    }

    if (DIUtils::isFuncPointerTy(fieldDIType))
    {
      if (driverExportFuncPtrNames.find(fieldName) != driverExportFuncPtrNames.end())
      {
        std::string fieldID = DIUtils::computeFieldID(rootDIType, fieldDIType);
        accessedFields.insert(fieldID);
        globalFieldAccessInfo.insert(std::make_pair(fieldID, AccessType::READ));
      }
      continue;
    }

    // get valdep pair, and check for intraprocedural accesses
    auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
    for (auto valDepPair : valDepPairList)
    {
      auto dataW = valDepPair.first->getData();
      AccessType accType = getAccessTypeForInstW(dataW);
      if (accType != AccessType::NOACCESS)
      {
        std::string fieldID = DIUtils::computeFieldID(rootDIType, fieldDIType);
        accessedFields.insert(fieldID);
        globalFieldAccessInfo.insert(std::make_pair(fieldID, accType));
      }
    }
  }
  return accessedFields;
}

bool pdg::AccessInfoTracker::isChildFieldShared(DIType *parentNodeDIType, DIType *fieldDIType)
{
  // if field is a function pointer, and its name is in function pointer list exported by driver, then we retrun it.
  // errs() << "checking shared field: " << DIUtils::getDITypeName(parentNodeDIType) << " - " << DIUtils::getDIFieldName(fieldDIType) << "\n";
  if (DIUtils::isFuncPointerTy(fieldDIType))
  {
    std::string fieldName = DIUtils::getDIFieldName(fieldDIType);
    if (driverExportFuncPtrNames.find(fieldName) != driverExportFuncPtrNames.end())
      return true;
  }
  // compute field id and check if it is presents in the set of shared data computed for the arg Type
  // std::string rootDITypeName = DIUtils::getLowestDIType(DIUtils::getDITypeName(rootDIType));
  std::string parentNodeDITypeName = DIUtils::getRawDITypeName(parentNodeDIType);
  if (sharedDataTypeMap.find(parentNodeDITypeName) == sharedDataTypeMap.end())
  {
    errs() << "[WARNING] "
           << "cannot find struct type " << parentNodeDITypeName << "\n";
    return false;
  }
  std::string fieldID = DIUtils::computeFieldID(parentNodeDIType, fieldDIType);
  auto sharedFields = sharedDataTypeMap[parentNodeDITypeName];
  // errs() << "shared field size for " << parentNodeDITypeName << " : " << sharedFields.size() << "\n";
  // errs() << fieldID << " - " << (sharedFields.find(fieldID) == sharedFields.end()) << "\n";
  if (sharedFields.find(fieldID) == sharedFields.end())
    return false;
  return true;
}

std::string pdg::AccessInfoTracker::switchIndirectCalledPtrName(std::string funcptrName)
{
  for (auto it = driverExportFuncPtrNameMap.begin(); it != driverExportFuncPtrNameMap.end(); ++it)
  {
    if (it->second == funcptrName)
    {
      return it->first;
    }
  }
  return funcptrName;
}

tree<InstructionWrapper *>::iterator pdg::AccessInfoTracker::getParentIter(tree<InstructionWrapper *>::iterator treeI)
{
  if (tree<InstructionWrapper *>::depth(treeI) < 1)
    return treeI;
  return tree<InstructionWrapper *>::parent(treeI);
}

FunctionDomain pdg::AccessInfoTracker::computeFuncDomain(Function& F)
{
  if (kernel_domain_funcs_.find(&F) == kernel_domain_funcs_.end())
    return FunctionDomain::DRIVER_DOMAIN;
  return FunctionDomain::KERNEL_DOMAIN;
}

bool pdg::AccessInfoTracker::IsFuncPtrExportFromDriver(std::string func_name)
{
  return driverExportFuncPtrNameMap.find(func_name) != driverExportFuncPtrNameMap.end();
}

void pdg::AccessInfoTracker::collectKSplitStats(DIType* struct_di_type, DIType* struct_field_di_type, std::string annotation_str)
{
  if (struct_field_di_type == nullptr)
    return;
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  DIType* struct_lowest_di_type = DIUtils::getLowestDIType(struct_di_type);
  DIType* struct_field_lowest_di_type = DIUtils::getLowestDIType(struct_field_di_type);
  if (DIUtils::isCharPointer(struct_field_di_type))
    ksplit_stats_collector.IncreaseNumberOfCharPointer();
  if (DIUtils::isVoidPointer(struct_field_di_type))
    ksplit_stats_collector.IncreaseNumberOfVoidPointer();
  if (DIUtils::isArrayType(struct_field_di_type))
  {
    ksplit_stats_collector.IncreaseNumberOfArray();
    if (DIUtils::isCharArray(struct_field_di_type))
      ksplit_stats_collector.IncreaseNumberOfCharArray();
  }
  if (annotation_str.find("string") != std::string::npos)
    ksplit_stats_collector.IncreaseNumberOfString();
  if (DIUtils::isUnionTy(struct_field_lowest_di_type))
  {
    ksplit_stats_collector.IncreaseNumberOfUnion();
  }
  if (DIUtils::isSentinelType(struct_field_lowest_di_type))
  {
    if (struct_lowest_di_type != struct_field_lowest_di_type)
      ksplit_stats_collector.IncreaseNumberOfSentinelArray();
  }
}

void pdg::AccessInfoTracker::collectKSplitSharedStats(DIType* struct_di_type, DIType* struct_field_di_type, std::string annotation_str)
{
  if (struct_field_di_type == nullptr)
    return;
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  DIType* struct_lowest_di_type = DIUtils::getLowestDIType(struct_di_type);
  DIType* struct_field_lowest_di_type = DIUtils::getLowestDIType(struct_field_di_type);
  if (DIUtils::isPointerType(struct_field_di_type))
    ksplit_stats_collector.IncreaseNumberOfPointerOp();
  if (DIUtils::isVoidPointer(struct_field_di_type))
  {
    ksplit_stats_collector.IncreaseNumberOfVoidPointerOp();
    if (struct_di_type != nullptr)
      ksplit_stats_collector.IncreaseNumberOfUnhandledVoidPointerOp();
  }
  if (DIUtils::isArrayType(struct_field_di_type))
    ksplit_stats_collector.IncreaseNumberOfHandledArray();
  if (annotation_str.find("string") != std::string::npos)
    ksplit_stats_collector.IncreaseNumberOfStringOp();
  if (DIUtils::isUnionTy(struct_field_lowest_di_type))
    ksplit_stats_collector.IncreaseNumberOfUnionOp();
  if (DIUtils::isSentinelType(struct_field_lowest_di_type))
  {
    if (struct_lowest_di_type != struct_field_lowest_di_type)
      ksplit_stats_collector.IncreaseNumberOfSentinelArrayOp();
  }
}

void pdg::AccessInfoTracker::printSharedPointerDebugLog(std::string func_name, std::string arg_name, std::string field_id, tree<InstructionWrapper *>::iterator treeI)
{
  shared_ptr_debug_log << func_name << " - " << arg_name << " - " << field_id << "\n";
  auto dep_inst_pairs = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
  for (auto dep_inst_pair : dep_inst_pairs)
  {
    auto dataW = const_cast<InstructionWrapper *>(dep_inst_pair.first->getData());
    auto data_inst = dataW->getInstruction();
    if (data_inst == nullptr)
      continue;
    std::string str;
    raw_string_ostream ss(str);
    ss << *data_inst;
    shared_ptr_debug_log << "\t" << ss.str() << "\n";
  }
}

void pdg::AccessInfoTracker::printSharedPointers(tree<InstructionWrapper*>::iterator treeI)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto dep_inst_pairs = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
  for (auto dep_inst_pair : dep_inst_pairs)
  {
    auto dataW = const_cast<InstructionWrapper *>(dep_inst_pair.first->getData());
    auto data_inst = dataW->getInstruction();
    if (data_inst == nullptr)
      continue;
    std::string inst_id = pdgUtils.computeInstID(*data_inst);
    if (seen_inst_ids_.find(inst_id) == seen_inst_ids_.end())
    {
      seen_inst_ids_.insert(inst_id);
      shared_ptr_file << inst_id << "\n";
    }
  }
}

bool pdg::AccessInfoTracker::isSeqPointer(tree<InstructionWrapper*>::iterator iter)
{
  auto dep_inst_pairs = PDG->getNodesWithDepType(*iter, DependencyType::VAL_DEP);
  for (auto dep_inst_pair : dep_inst_pairs)
  {
    auto dataW = const_cast<InstructionWrapper *>(dep_inst_pair.first->getData());
    Instruction* data_inst = dataW->getInstruction();
    if (data_inst == nullptr)
      continue;
    for (auto user : data_inst->users())
    {
      if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(user))
      {
        if (!GEP->hasAllZeroIndices())
        {
          // Type* ptr_operand_ty = GEP->getPointerOperandType();
          // if (!PDG->isStructPointer(ptr_operand_ty))
          return true;
        }
      }
    }
  }
  return false;
}

static RegisterPass<pdg::AccessInfoTracker>
    AccessInfoTracker("idl-gen", "Argument access information tracking Pass", false, true);