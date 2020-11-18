#include "AccessInfoTracker.hpp"
#include <math.h>

using namespace llvm;

char pdg::AccessInfoTracker::ID = 0;

bool pdg::AccessInfoTracker::runOnModule(Module &M)
{
  module = &M;
  PDG = &getAnalysis<ProgramDependencyGraph>();
  auto &pdgUtils = PDGUtils::getInstance();
  // get cross domain functions and setup kernel and driver side functions
  initializeNumStats();
  std::set<Function *> crossDomainFuncCalls = pdgUtils.computeCrossDomainFuncs(M);
  importedFuncs = pdgUtils.computeImportedFuncs(M);
  driverExportFuncPtrNames = pdgUtils.computeDriverExportFuncPtrName();
  driverDomainFuncs = pdgUtils.computeDriverDomainFuncs(M);
  kernelDomainFuncs = pdgUtils.computeKernelDomainFuncs(M);
  driverExportFuncPtrNameMap = pdgUtils.computeDriverExportFuncPtrNameMap();
  // asyncCalls = PDG->inferAsynchronousCalledFunction();
  // counter for how many field we eliminate using shared data
  setupStrOpsMap();
  setupAllocatorMap();
  globalOpsStr = "";
  privateDataSize = 0;
  savedSyncDataSize = 0;
  numProjectedFields = 0;
  numEliminatedPrivateFields = 0;
  
  // start generating IDL
  std::string file_name = "kernel";
  file_name += ".idl";
  idl_file.open(file_name);
  idl_file << "module kernel"
           << " {\n";
  computeSharedData();
  std::set<Function*> crossDomainTransFuncs;
  pdgUtils.computeCrossDomainTransFuncs(M, crossDomainTransFuncs);
  std::set<Function*> reachableFuncInKernel;
  for (auto func : crossDomainTransFuncs)
  {
    if (kernelDomainFuncs.find(func) != kernelDomainFuncs.end())
      reachableFuncInKernel.insert(func);
  }
  printCopiableFuncs(reachableFuncInKernel);
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
  printNumStats();
  return false;
}

void pdg::AccessInfoTracker::setupStrOpsMap()
{
  stringOperations.insert("strcpy");
  stringOperations.insert("strlen");
  stringOperations.insert("strlcpy");
  stringOperations.insert("strcmp");
  stringOperations.insert("strncmp");
}

void pdg::AccessInfoTracker::setupAllocatorMap()
{
  allocatorMap.insert("kmalloc");
  allocatorMap.insert("malloc");
  allocatorMap.insert("zalloc");
  allocatorMap.insert("kzalloc");
}

void pdg::AccessInfoTracker::initializeNumStats()
{
  concurrentFieldsNum = 0;
  totalNumOfFields = 0;
  unionNum = 0;
  unNamedUnionNum = 0;
  voidPointerNum = 0;
  unhandledVoidPointerNum = 0;
  pointerArithmeticNum = PDG->getUnsafeTypeCastNum();
  sentialArrayNum = 0;
  arrayNum = 0;
  unHandledArrayNum = 0;
  stringNum = 0;
}

void pdg::AccessInfoTracker::printNumStats()
{
  auto &pdgUtils = PDGUtils::getInstance();
  std::ofstream numStats;
  numStats.open("numStats.txt", std::ios::app);
  numStats << "union/unhandled type number: " << unionNum << "[" << unNamedUnionNum << "]"<< "\n";
  numStats << "void pointer/unhandled number: " << voidPointerNum << "[" << unhandledVoidPointerNum << "]" << "\n";
  numStats << "wild pointer: " << pointerArithmeticNum << "\n";
  numStats << "sential array number: " << sentialArrayNum << "\n";
  numStats << "array/unhandled number: " << (arrayNum + unHandledArrayNum) << "[" << unHandledArrayNum << "]" << "\n";
  numStats << "string number: " << stringNum << "\n";
  numStats << "global used in driver: " << computeUsedGlobalNumInDriver() << "\n";
  numStats << "Driver to Kernel Invocation: " << importedFuncs.size() << "\n";
  numStats << "Kernel to Driver Invocation: " << driverExportFuncPtrNames.size() << "\n";
  // eliminated fields
  std::string sharedDataStatsStr = "sharedDataStats.txt";
  std::ofstream sharedDataStatsFile;
  sharedDataStatsFile.open(sharedDataStatsStr);
  sharedDataStatsFile << "shared data stats: \n";
  sharedDataStatsFile << "number of shared data types: " << sharedDataTypeMap.size() << "\n";
  sharedDataStatsFile << "Concurrently Executed Fields: " << concurrentFieldsNum << "\n";
  unsigned numOfSharedFields = 0;
  for (auto pair : sharedDataTypeMap)
  {
    numOfSharedFields += pair.second.size();
  }
  sharedDataStatsFile << "total number of fields: " << totalNumOfFields << "\n";
  // sharedDataStatsFile << "number of shared fields: " << numOfSharedFields << "\n";
  // sharedDataStatsFile << "number of private fields: " << (totalNumOfFields - numOfSharedFields) << "\n";
  sharedDataStatsFile << "number of projected fields found by basic algorithm: " << numProjectedFields << "\n";
  sharedDataStatsFile << "number of projected fields with shared data optimziation: " << (numProjectedFields - numEliminatedPrivateFields ) << "\n";
  // sharedDataStatsFile << "save data in bytes: " << savedSyncDataSize << "\n";
  // sharedDataStatsFile << "save data using shared data in bytes: " << privateDataSize << "\n";
  // sharedDataStatsFile << "total save data in bytes: " << (savedSyncDataSize + privateDataSize) << "\n";
  sharedDataStatsFile.close();
}

void pdg::AccessInfoTracker::printAsyncCalls()
{
  errs() << "async functions: -----------------------------------------------------\n";
  errs() << "size of ayns funcs: " << asyncCalls.size() << "\n";
  errs() << "async func access shared data: " << asyncCallAccessedSharedData.size() << "\n";
  for (auto asyncFunc : asyncCalls)
  {
    errs() << asyncFunc->getName() << "\n";
  }
  errs() << "----------------------------------------------------------------------\n";
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
        if (driverDomainFuncs.find(func) != driverDomainFuncs.end())
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
        if (dyn_cast<Instruction>(st->getPointerOperand()) == instW->getInstruction())
        {
          if (isa<Argument>(st->getValueOperand())) // ignore the store inst that store arg to stack mem
            break;
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
  errs() << "kernel func Num: " << kernelDomainFuncs.size() << "\n";
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

void pdg::AccessInfoTracker::computeSharedData()
{
  auto globalTypeTrees = PDG->getGlobalTypeTrees();
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  for (auto pair : globalTypeTrees)
  {
    DIType* sharedType = pair.first;
    auto typeTree = pair.second;
    if (!sharedType)
      continue;
    std::set<std::string> accessedFields;
    // iterate through each node, find their addr variables and then analyze the accesses to the addr variables
    for (auto treeI = typeTree.begin(); treeI != typeTree.end(); ++treeI)
    {
      DIType *fieldDIType = (*treeI)->getDIType();
      // hanlde static defined functions, assume all functions poineter that are linked with defined function in device driver module should be used by kernel.
      if (DIUtils::isFuncPointerTy((*treeI)->getDIType()))
      {
        std::string funcptrName = DIUtils::getDIFieldName((*treeI)->getDIType());
        if (driverExportFuncPtrNames.find(funcptrName) != driverExportFuncPtrNames.end())
        {
          DIType* parentDIType = nullptr;
          if (tree<InstructionWrapper *>::depth(treeI) > 0)
          {
            auto parentI = tree<InstructionWrapper*>::parent(treeI);
            parentDIType = (*parentI)->getDIType();
          }
          std::string fieldID = DIUtils::computeFieldID(parentDIType, fieldDIType);
          accessedFields.insert(fieldID);
        }
        continue;
      }

      // get valdep pair, and check for intraprocedural accesses
      auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
      bool accessInKernel = false;
      bool accessInDriver = false;
      AccessType nodeAccessTy = AccessType::NOACCESS;
      // errs() << "val dep for " << DIUtils::getDIFieldName((*treeI)->getDIType()) << "\n";
      for (auto valDepPair : valDepPairList)
      {
        auto dataW = valDepPair.first->getData();
        Instruction *inst = dataW->getInstruction();
        // errs() << "\t" << *inst << " - " << inst->getFunction()->getName() << "\n";
        AccessType accType = getAccessTypeForInstW(dataW);
        if (accType != AccessType::NOACCESS)
        {
          // check if a field is shared
          if (inst != nullptr)
          {
            Function *f = inst->getFunction();
            if (driverDomainFuncs.find(f) != driverDomainFuncs.end())
              accessInDriver = true;
            if (kernelDomainFuncs.find(f) != kernelDomainFuncs.end())
              accessInKernel = true;
          }
          if (accType == AccessType::WRITE)
            nodeAccessTy = AccessType::WRITE;
          if (accType == AccessType::READ && nodeAccessTy != AccessType::WRITE)
            nodeAccessTy = AccessType::READ;
        }
        // verified a field is accessed in both domains
        if (accessInDriver && accessInKernel)
          break;
      }
      // if a field is not shared, continue to next tree node
      if (!accessInDriver || !accessInKernel)
        continue;
      TreeTypeWrapper* treeW = static_cast<TreeTypeWrapper*>(const_cast<InstructionWrapper*>(*treeI));
      treeW->setShared(true);
      DIType* parentNodeDIType = nullptr;
      if (tree<InstructionWrapper *>::depth(treeI) > 0)
      {
        auto parentI = tree<InstructionWrapper*>::parent(treeI);
        parentNodeDIType = (*parentI)->getDIType();
      }
      // for (auto valDepPair : valDepPairList)
      // {
        // auto dataW = valDepPair.first->getData();
        // Instruction *inst = dataW->getInstruction();
      std::string fieldID = DIUtils::computeFieldID(parentNodeDIType, fieldDIType);
      // errs() << " shared field id: " << fieldID << "\n";
      accessedFields.insert(fieldID);
      // update the accessed type for a field
      if (globalFieldAccessInfo.find(fieldID) == globalFieldAccessInfo.end())
        globalFieldAccessInfo.insert(std::make_pair(fieldID, nodeAccessTy));
      // }
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

void pdg::AccessInfoTracker::computeArgAccessInfo(ArgumentWrapper* argW, TreeType treeTy)
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
          auto parentI = tree<InstructionWrapper*>::parent(treeI);
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
    }
  }
}

void pdg::AccessInfoTracker::computeInterprocArgAccessInfo(ArgumentWrapper* argW, Function &F)
{
  // errs() << "start computing inter proc info for: " << F.getName() << " - " << argW->getArg()->getArgNo() << "\n";
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  std::map<std::string, AccessType> interprocAccessFieldMap;
  for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
  {
    // need parent DI info for computing field ID in later steps
    DIType* parentNodeDIType = nullptr;
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
          if (Function *calledFunc = dyn_cast<Function>(ci->getCalledValue()->stripPointerCasts()))
          {
            if (calledFunc->isDeclaration() || calledFunc->empty())
              continue;
            // compute the field that are accessed in the callee. The return map's key is accessed field's id and the value is access type
            auto accessFieldMap = computeInterprocAccessedFieldMap(*calledFunc, callArgIdx, parentNodeDIType, DIUtils::getDIFieldName((*treeI)->getDIType()));
            interprocAccessFieldMap.insert(accessFieldMap.begin(), accessFieldMap.end());
          }
        }
      }
    }
  }
  // set accessed type according to the interproc access field map
  for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
  {
    DIType* parentDIType = nullptr;
    DIType* curNodeDIType = (*treeI)->getDIType();
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

std::vector<Function*> pdg::AccessInfoTracker::computeBottomUpCallChain(Function& F)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  // search domain is used to optimize the results by computing IDL made by first 
  auto searchDomain = kernelDomainFuncs;
  if (kernelDomainFuncs.find(&F) == kernelDomainFuncs.end())
    searchDomain = driverDomainFuncs;
  std::vector<Function *> ret;
  ret.push_back(&F);
  std::set<Function*> seenFuncs;
  std::queue<Function *> funcQ;
  funcQ.push(&F);
  seenFuncs.insert(&F);
  while (!funcQ.empty())
  {
    Function *func = funcQ.front();
    funcQ.pop();
    auto callInstList = funcMap[func]->getCallInstList();
    for (auto ci : callInstList)
    {
      if (Function *calledF = dyn_cast<Function>(ci->getCalledValue()->stripPointerCasts()))
      {
        if (calledF->isDeclaration() || calledF->empty())
          continue;
        if (seenFuncs.find(calledF) != seenFuncs.end()) // skip if already visited the called function
          continue;
        ret.push_back(calledF);
        seenFuncs.insert(calledF);
        funcQ.push(calledF);
      }
    }
  }
  return ret;
}

int pdg::AccessInfoTracker::getCallOperandIdx(Value* operand, CallInst* callInst)
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


std::map<std::string, AccessType> pdg::AccessInfoTracker::computeInterprocAccessedFieldMap(Function& callee, unsigned argNo, DIType* parentNodeDIType, std::string fieldNameInCaller)
{
  std::map<std::string, AccessType> accessFieldMap;
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto funcW = funcMap[&callee];
  ArgumentWrapper* argW = funcW->getArgWByIdx(argNo);
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

std::set<Value *> pdg::AccessInfoTracker::findAliasInDomain(Value &V, Function &F, std::set<Function*> domainTransitiveClosure)
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
      Instruction* curInst = &*I;
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
          if (F.getName() == "dummy_dev_init")
            errs() << "find alias in " << transFunc->getName() << "\n";
          aliasInDomain.insert(&*I);
          break;
        }
      }
    }
  }
  return aliasInDomain;
}

void pdg::AccessInfoTracker::computeFuncAccessInfoBottomUp(Function& F)
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
      if (tree<InstructionWrapper*>::depth(treeI) < 1)
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
    if (static_cast<int>((*callerTreeI)->getAccessType()) < static_cast<int>((*calleeTreeI)->getAccessType())) {
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
      errs() << "** Root type node **" << "\n";
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
  DIType *funcRetType = DIUtils::getFuncRetDIType(F);
  std::string retTypeName;
  if (funcRetType == nullptr)
    retTypeName = "void";
  else
    retTypeName = DIUtils::getDITypeName(funcRetType);

  // swap the function name with its registered function pointer. Just to align with the IDL syntax
  auto funcName = F.getName().str();
  funcName = getRegisteredFuncPtrName(funcName);
  // handle return type, concate with function name
  if (PDG->isStructPointer(F.getReturnType())) // generate alloc(caller) as return struct pointer is from the other side
  {
    // if (retTypeName.back() == '*')
    // {
    //   retTypeName.pop_back();
    // }
    retTypeName = "projection " + retTypeName;
    std::string retAttributeStr = getReturnAttributeStr(F);
    if (!retAttributeStr.empty())
      retTypeName = retTypeName + " " + retAttributeStr;
  }
  
  idl_file << "\trpc " << retTypeName << " " << funcName;
  if (funcName.find("ioremap") != std::string::npos)
    idl_file << " [ioremap(caller)] ";
  idl_file << "( ";
  // handle parameters
  for (auto argW : pdgUtils.getFuncMap()[&F]->getArgWList())
  {
    Argument &arg = *argW->getArg();
    Type *argType = arg.getType();
    DIType *argDIType = DIUtils::getArgDIType(arg);
    auto &dbgInstList = pdgUtils.getFuncMap()[&F]->getDbgInstList();
    std::string argName = DIUtils::getArgName(arg);
    // infer annotation
    if (argType->isPointerTy())
    {
      auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE);
      if (treeI != argW->tree_end(TreeType::FORMAL_IN_TREE))
      {
        treeI++;
        std::string annotation = inferFieldAnnotation(*treeI);
        if (!annotation.empty())
        {
          argName = argName + " " + annotation;
          if (annotation == "[string]")
            stringNum++;
        }
      }
    }

    if (PDG->isStructPointer(argType))
    {
      auto argTypeName = DIUtils::getArgTypeName(arg);
      auto argName = DIUtils::getArgName(arg);
      // if (argTypeName.back() == '*')
      // {
      //   argTypeName.pop_back();
      //   argFuncName.push_back('*');
      // }

      // if (argTypeName.find("ops") == std::string::npos) // handling projection for function structs
      //   argTypeName = argTypeName;
      // else
      //   argTypeName.push_back('*');
      
      // generate array annotation for struct pointers
      uint64_t arrSize = getArrayArgSize(arg, F);
      if (arrSize > 0)
      {
        argName = argName + "[" + std::to_string(arrSize) + "]";
        arrayNum++;
      }
      
      idl_file << "projection " << argTypeName << " " << argName;
    }
    else if (PDG->isFuncPointer(argType))
    {
      Function *indirectCalledFunc = nullptr;
      Function *indirectFunc = module->getFunction(switchIndirectCalledPtrName(argName));
      idl_file << "rpc " << DIUtils::getFuncSigName(DIUtils::getLowestDIType(DIUtils::getArgDIType(arg)), indirectCalledFunc, argName, "");
    }
    else if (argType->isArrayTy())
    {
      std::string argTypeName = DIUtils::getArgTypeName(arg);
      std::string argStr = argTypeName + " " + argName;
      uint64_t arrSize = getArrayArgSize(arg, F);
      if (arrSize > 0)
        argStr = argStr + "[" + std::to_string(arrSize) + "]";
      else 
        unHandledArrayNum++;
      idl_file << argStr;
      arrayNum++;
    }
    else
      idl_file << DIUtils::getArgTypeName(arg) << " " << argName;

    // collecting stats 
    if (DIUtils::isUnionPointerTy(argDIType) || DIUtils::isUnionTy(argDIType))
      unionNum++;
    if (DIUtils::isSentinelType(argDIType))
      sentialArrayNum++;
    if (DIUtils::isVoidPointer(argDIType))
    {
      voidPointerNum++;
      // if (voidPointerHasMultipleCasts(*argW->tree_begin(TreeType::FORMAL_IN_TREE)))
      //   unhandledVoidPointerNum++;
    }

    if (argW->getArg()->getArgNo() < F.arg_size() - 1 && !argName.empty())
      idl_file << ", ";
  }
  idl_file << " )";
}

uint64_t pdg::AccessInfoTracker::getArrayArgSize(Value &V, Function& F)
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
    Value* tmp = const_cast<Value*>(a1);
    if (AllocaInst *ai = dyn_cast<AllocaInst>(tmp))
    {
      PointerType *allocType = ai->getType();
      Type *pointedTy = allocType->getElementType();
      if (pointedTy->isArrayTy())
        return pointedTy->getArrayNumElements();
    }
    // TODO: need to handle dynamic allocated array
  }
  return 0;
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

std::set<Instruction*> pdg::AccessInfoTracker::getIntraFuncAlias(Instruction *inst)
{
  Function *F = inst->getFunction();
  std::set<Instruction*> aliasSet;
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

std::string pdg::AccessInfoTracker::getReturnAttributeStr(Function &F)
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
    Instruction* retVal = cast<Instruction>(retInst->getReturnValue());
    auto aliasSet = getIntraFuncAlias(retVal);
    for (auto alias : aliasSet)
    {
      if (CallInst *ci = dyn_cast<CallInst>(alias))
      {
        if (Function *calledFunc = dyn_cast<Function>(ci->getCalledValue()->stripPointerCasts()))
        {
          if (calledFunc == &F) continue;
          if (calledFunc->isDeclaration() || calledFunc->empty()) continue;
          
          std::string calleeRetStr = getReturnAttributeStr(*calledFunc);
          if (!calleeRetStr.empty())
            return calleeRetStr;
        }
      }

      if (!G->hasCell(*alias))
        continue;
      auto const &c = G->getCell(*alias);
      auto const &s = c.getNode()->getAllocSites();
      for (auto const a : s)
      {
        Value* tempV = const_cast<Value*>(a);
        if (CallInst *ci = dyn_cast<CallInst>(tempV))
        {
          if (Function *calledFunc = dyn_cast<Function>(ci->getCalledValue()->stripPointerCasts()))
          {
            std::string funcName = calledFunc->getName().str();
            if (funcName.find("zalloc") != std::string::npos || funcName.find("malloc") != std::string::npos)
              return "[alloc(caller)]";
          }
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
  idl_file << "\t{\n";
  for (auto argW : funcW->getArgWList())
  {
    generateIDLforArg(argW);
  }
  generateIDLforArg(funcW->getRetW());
  idl_file << "\t}\n\n";
  // don't generate rpc for driver export function pointers
}

// the purpose of this function is synchronizing data that won't sync correctly due to lack of cross-domain function call
void pdg::AccessInfoTracker::generateSyncDataStubAtFuncEnd(Function &F)
{
  // handle global variable accesses
  // 1. check if function access global variable
  std::string funcName = F.getName().str();
  auto globalObjectTrees = PDG->getGlobalObjectTrees();
  for (auto globalObjectTreePair : globalObjectTrees)
  {
    auto globalVar = globalObjectTreePair.first;
    bool accessedInFunc = false;
    for (auto user : globalVar->users())
    {
      if (Instruction *i = dyn_cast<Instruction>(user))
      {
        if (i->getFunction() == &F)
        {
          accessedInFunc = true;
          break;
        }
      }
    }
    if (!accessedInFunc) 
      continue;
    auto globalObjectTree = globalObjectTreePair.second;
    // if global variable is accessed in the function, start generating idl for the global which summarize the accessed data
    auto treeBegin = globalObjectTree.begin();
    std::queue<tree<InstructionWrapper*>::iterator> treeNodeQ;
    treeNodeQ.push(treeBegin);
    while (!treeNodeQ.empty())
    {
      auto treeI = treeNodeQ.front();
      treeNodeQ.pop();
      // generate projection for the current node.
      DIType *curDIType = (*treeI)->getDIType();
      if (!curDIType)
        continue;
      DIType *lowestDIType = DIUtils::getLowestDIType(curDIType);
      if (!DIUtils::isProjectableTy(lowestDIType)) 
        continue;
      for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
      {
        auto childI = tree<InstructionWrapper *>::child(treeI, i);
        bool isAccessed = ((*childI)->getAccessType() != AccessType::NOACCESS);
        if (!isAccessed)
          continue;
        auto childDITy = (*childI)->getDIType();
        childDITy = DIUtils::getLowestDIType(childDITy);
        if (DIUtils::isProjectableTy(childDITy))
          treeNodeQ.push(childI);
      }
      std::string str;
      raw_string_ostream OS(str);
      // idl_file << "Insert sync stab at end of function: " << funcName << "\n";
      generateProjectionForTreeNode(treeI, OS, (*treeBegin)->getDIType());
      std::string structName = DIUtils::getDIFieldName(curDIType);
      if (structName.find("ops") == std::string::npos)
        structName = structName + "_" + funcName;
      idl_file << "=== Data Sync at end of function " << funcName << " ===\n\tprojection < struct " << structName << "> " << structName << " {\n " << OS.str() << "\t}\n\n";
    }
  }
}

void pdg::AccessInfoTracker::generateProjectionForGlobalVarInFunc(tree<InstructionWrapper *>::iterator treeI, raw_string_ostream &OS, DIType *parentNodeDIType, Function& func)
{
  auto curDIType = (*treeI)->getDIType();
  if (curDIType == nullptr)
    return;
  for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
  {
    auto childI = tree<InstructionWrapper*>::child(treeI, i);
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
      Function* depFunc = depInst->getFunction();
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
      OS << "\t\t" << "// union type \n";
    }
    else
    {
      std::string fieldName = DIUtils::getDIFieldName(childDITy);
      if (!fieldName.empty())
      {
        if (fieldName.find("[") != std::string::npos)
          arrayNum++;
        OS << "\t\t" + DIUtils::getDITypeName(childDITy) << " " << getAccessAttributeName(childI) << " " << DIUtils::getDIFieldName(childDITy) << ";\n";
      }
    }
  }
}

// receive a tree iterator and start 
void pdg::AccessInfoTracker::generateProjectionForTreeNode(tree<InstructionWrapper*>::iterator treeI, raw_string_ostream &OS, DIType* argDIType)
{
  auto curDIType = (*treeI)->getDIType();
  if (curDIType == nullptr)
    return;
  std::string indentLevel = "\t\t\t";
  for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
  {
    auto childI = tree<InstructionWrapper*>::child(treeI, i);
    auto childDITy = (*childI)->getDIType();
    // check if field is private
    bool isSharedField = isChildFieldShared(curDIType, childDITy);
    bool isFieldAccess = ((*childI)->getAccessType() != AccessType::NOACCESS);
    if (!isFieldAccess || !isSharedField)
      continue;

    std::string fieldAnnotation = inferFieldAnnotation(*childI);
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
      OS << indentLevel << "rpc " << DIUtils::getFuncSigName(DIUtils::getLowestDIType(childDITy), indirectFunc, DIUtils::getDIFieldName(childDITy)) << ";\n";
    }
    else if (DIUtils::isStructTy(childLowestTy))
    {
      std::string funcName = "";
      if ((*treeI)->getFunction())
        funcName = (*treeI)->getFunction()->getName().str();
      auto fieldTypeName = DIUtils::getDITypeName(childDITy);
      // formatting functions
      while (fieldTypeName.back() == '*')
      {
        fieldTypeName.pop_back();
      }

      std::string projectStr = "projection ";
      std::string constStr = "const struct";
      auto pos = fieldTypeName.find(constStr);
      if (pos != std::string::npos)
      {
        fieldTypeName = fieldTypeName.substr(pos + constStr.length() + 1);
        projectStr = "const " + projectStr;
      }

      OS << indentLevel
         << projectStr << fieldTypeName << " "
         << " *" << DIUtils::getDIFieldName(childDITy)
         << ";\n";
    }
    else if (DIUtils::isUnionTy(childLowestTy))
    {
      OS << indentLevel << "// union type \n";
    }
    else
    {
      std::string fieldName = DIUtils::getDIFieldName(childDITy);
      if (!fieldName.empty())
      {
        if (fieldName.find("[") != std::string::npos)
          arrayNum++;
        OS << indentLevel << DIUtils::getDITypeName(childDITy) << " " << getAccessAttributeName(childI) << " " << DIUtils::getDIFieldName(childDITy) << ";\n";
      }
    }
  }
}

void pdg::AccessInfoTracker::generateIDLforArg(ArgumentWrapper* argW)
{
  auto &pdgUtils = PDGUtils::getInstance();
  if (argW->getTree(TreeType::FORMAL_IN_TREE).size() == 0)
    return;
  Function &F = *argW->getFunc();
  // std::string funcName = F.getName().str();
  // funcName = getRegisteredFuncPtrName(funcName);
  std::string argName = DIUtils::getArgName(*(argW->getArg()));
  auto treeBegin = argW->tree_begin(TreeType::FORMAL_IN_TREE);
  DIType* argDIType = (*treeBegin)->getDIType();
  std::queue<tree<InstructionWrapper*>::iterator> treeNodeQ;
  treeNodeQ.push(treeBegin);
  std::queue<tree<InstructionWrapper*>::iterator> funcPtrQ;

  while (!treeNodeQ.empty())
  {
    auto treeI = treeNodeQ.front();
    treeNodeQ.pop();
    // generate projection for the current node. 
    DIType* curDIType = (*treeI)->getDIType();
    DIType* lowestDIType = DIUtils::getLowestDIType(curDIType);
    if (!DIUtils::isProjectableTy(lowestDIType))
      continue;
    // append child node needs projection to queue
    for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
    {
      auto childI = tree<InstructionWrapper*>::child(treeI, i);
      bool isAccessed = ((*childI)->getAccessType() != AccessType::NOACCESS);
      if (!isAccessed)
        continue;
      auto childDITy = (*childI)->getDIType();
      childDITy = DIUtils::getLowestDIType(childDITy);
      if (DIUtils::isProjectableTy(childDITy))
        treeNodeQ.push(childI);
    }

    // for a pointed value, we don't genereate projection.
    // DIType* parentDIType = nullptr;
    // if (tree<InstructionWrapper *>::depth(treeI) > 0)
    // {
    //   auto parentI = tree<InstructionWrapper *>::parent(treeI);
    //   parentDIType = (*parentI)->getDIType();
    // }
    
    // if (parentDIType && DIUtils::isPointerType(parentDIType) && DIUtils::getLowestDIType(parentDIType) == curDIType)
    //   continue;

    std::string str;
    raw_string_ostream OS(str);
    std::string structFieldName = DIUtils::getDIFieldName(curDIType);
    std::string structTypeName = DIUtils::getDITypeName(lowestDIType);
    generateProjectionForTreeNode(treeI, OS, argDIType);
    if (OS.str().empty())
      continue;
    // special handling for global op structs
    if (structTypeName.find("ops") != std::string::npos)
    {
      // find the first ops projection with fields inside
      if (seenFuncOps.find(structTypeName) != seenFuncOps.end())
        continue;
      seenFuncOps.insert(structTypeName);
      std::string projStr = "\tprojection < " + structTypeName + "> " + structFieldName + " {\n " + OS.str() + "\t}\n\n";
      globalOpsStr = globalOpsStr + "\n" + projStr;
    }
    else
      idl_file << "\t\tprojection < " << structTypeName << "> " << structFieldName << " {\n " << OS.str() << "\t\t}\n\n";
  }
}


std::string pdg::AccessInfoTracker::inferFieldAnnotation(InstructionWrapper *instW)
{
  auto valDepPairList = PDG->getNodesWithDepType(instW, DependencyType::VAL_DEP);
  for (auto valDepPair : valDepPairList)
  {
    auto dataW = valDepPair.first->getData();
    if (!dataW->getInstruction())
      continue;
    auto dataDList = PDG->getNodeDepList(dataW->getInstruction());
    for (auto depPair : dataDList)
    {
      InstructionWrapper *depInstW = const_cast<InstructionWrapper *>(depPair.first->getData());
      DependencyType depType = depPair.second;
      Instruction *depInst = depInstW->getInstruction();
      if (!depInst)
        continue;
      if (depType == DependencyType::DATA_DEF_USE)
      {
        if (CallInst *ci = dyn_cast<CallInst>(depInst))
        {
          if (Function *calledFunc = dyn_cast<Function>(ci->getCalledValue()->stripPointerCasts()))
          {
            if (calledFunc != nullptr)
            {
              std::string calledFuncName = calledFunc->getName().str();
              if (stringOperations.find(calledFuncName) != stringOperations.end())
              {
                // TODO: assume const char* is equal to string
                std::string retStr = "[string]";
                return retStr;
              }
            }
          }
        }

        // alloc callee annotation
        if (StoreInst *si = dyn_cast<StoreInst>(depInst))
        {
          if (CallInst *ci = dyn_cast<CallInst>(si->getValueOperand()))
          {
            auto calledFunc = ci->getCalledFunction();
            if (calledFunc != nullptr)
            {
              if (allocatorMap.find(ci->getCalledFunction()->getName().str()) != allocatorMap.end())
                return "[alloc(callee)] [out]";
            }
          }
        }

        // alloc caller annotation
        if (StoreInst *si = dyn_cast<StoreInst>(depInst))
        {
          if (si->getPointerOperand() == dataW->getInstruction())
          {
            if (isa<GlobalVariable>(si->getValueOperand()->stripPointerCasts()))
              return "[alloc(caller)] [out]";
          }
        }
        
        // dealloc annotation
        if (CallInst *ci = dyn_cast<CallInst>(depInst))
        {
          if (Function *calledFunc = dyn_cast<Function>(ci->getCalledValue()->stripPointerCasts()))
          {
            if (calledFunc != nullptr)
            {
              if (calledFunc->getName().str().find("free") != std::string::npos)
                return "[dealloc(caller)]";
            }
          }
        }
      }
    }
  }
  return "";
}

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
    DIType* argDIType = DIUtils::getArgDIType(arg);
    if (!DIUtils::isStructPointerTy(argDIType))
      continue;
    // check if shared fields for this struct type is already done
    std::string argTypeName = DIUtils::getArgTypeName(arg);
    if (sharedDataTypeMap.find(argTypeName) == sharedDataTypeMap.end()) {
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
  std::set<std::string> accessedFieldsInDriver = computeAccessedDataInDomain(dt, driverDomainFuncs);
  std::set<std::string> accessedFieldsInKernel = computeAccessedDataInDomain(dt, kernelDomainFuncs);
  // then, take the union of the accessed fields in the two domains.
  std::set<std::string> sharedFields;
  for (auto str : accessedFieldsInDriver)
  {
    if (accessedFieldsInKernel.find(str) != accessedFieldsInKernel.end())
      sharedFields.insert(str);
  }
  return sharedFields;
}

std::set<std::string> pdg::AccessInfoTracker::computeAccessedDataInDomain(DIType* dt, std::set<Function*> domain)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  std::set<std::string> accessedFieldsInDomain;
  for (auto func : domain)
  {
    auto funcW = funcMap[func];
    for (auto &arg : func->args())
    {
      DIType* argDIType = DIUtils::getArgDIType(arg);
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
    DIType* fieldDIType = (*treeI)->getDIType();
    std::string fieldName = DIUtils::getDIFieldName(fieldDIType);
    if (fieldName.find("ops") != std::string::npos)
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

bool pdg::AccessInfoTracker::isChildFieldShared(DIType* parentNodeDIType, DIType* fieldDIType)
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
  std::string parentNodeDITypeName = DIUtils::getDITypeName(parentNodeDIType);
  if (sharedDataTypeMap.find(parentNodeDITypeName) == sharedDataTypeMap.end())
  {
    errs() << "[WARNING] " << "cannot find struct type " << parentNodeDITypeName << "\n";
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

static RegisterPass<pdg::AccessInfoTracker>
    AccessInfoTracker("idl-gen", "Argument access information tracking Pass", false, true);


// void pdg::AccessInfoTracker::generateIDLforArg(ArgumentWrapper *argW, TreeType treeTy, std::string funcName, bool handleFuncPtr)
// {
//   auto &pdgUtils = PDGUtils::getInstance();
//   if (argW->getTree(TreeType::FORMAL_IN_TREE).size() == 0)
//     return;

//   Function &F = *argW->getArg()->getParent();

//   if (funcName.empty())
//     funcName = F.getName().str();

//   funcName = getRegisteredFuncPtrName(funcName);

//   auto &dbgInstList = pdgUtils.getFuncMap()[&F]->getDbgInstList();
//   std::string argName = DIUtils::getArgName(*(argW->getArg()));
//   auto treeBegin = argW->tree_begin(TreeType::FORMAL_IN_TREE);
//   DIType* argDIType = (*treeBegin)->getDIType();
//   unsigned fieldNumsInTypes = DIUtils::computeTotalFieldNumberInStructType(argDIType);
//   totalNumOfFields += fieldNumsInTypes;

//   std::queue<tree<InstructionWrapper*>::iterator> treeNodeQ;
//   treeNodeQ.push(argW->tree_begin(treeTy));
//   std::queue<tree<InstructionWrapper*>::iterator> funcPtrQ;

//   while (!treeNodeQ.empty())
//   {
//     auto treeI = treeNodeQ.front();
//     treeNodeQ.pop();
//     auto curDIType = (*treeI)->getDIType();
//     if (curDIType == nullptr) continue;
//     std::stringstream projection_str;
//     // only process sturct pointer and function pointer, these are the only types that we should generate projection for
//     if (!DIUtils::isStructPointerTy(curDIType) &&
//         !DIUtils::isFuncPointerTy(curDIType) &&
//         !DIUtils::isUnionPointerTy(curDIType) &&
//         !DIUtils::isStructTy(curDIType))
//       continue;

//     DIType* baseType = DIUtils::getLowestDIType(curDIType);

//     if (!DIUtils::isStructTy(baseType) && !DIUtils::isUnionType(baseType))
//       continue;

//     if (DIUtils::isStructPointerTy(curDIType) || DIUtils::isUnionPointerTy(curDIType))
//       treeI++;

//     for (int i = 0; i < tree<InstructionWrapper *>::number_of_children(treeI); ++i)
//     {
//       auto childT = tree<InstructionWrapper *>::child(treeI, i);
//       auto childDIType = (*childT)->getDIType();
//       if (childDIType == nullptr)
//         continue;
//       // if this is a call back func from kernel to driver, then we can assume the accessed fields are already shared.
//       // because the passed in object must have already has a copy in the kernel side.
//       bool isCallBackFunc = (driverExportFuncPtrNameMap.find(F.getName().str()) != driverExportFuncPtrNameMap.end());
//       bool isPrivateField = (!isChildFieldShared(argDIType, childDIType));
//       // parameters passed in call back functions can be considered shared,
//       // as they are sent by kernel, which allocates obj.
//       if (!isCallBackFunc)
//       {
//         if (isPrivateField)
//         {
//           numProjectedFields++;
//           numEliminatedPrivateFields++;
//           privateDataSize += (childDIType->getSizeInBits() / 8);
//           continue;
//         }
//       }

//       // determien if a field is accessed in concurrent context. If so, add it to projection.
//       std::string fieldID = DIUtils::computeFieldID(argDIType, childDIType);
//       if (accessedFieldsInAsyncCalls.find(fieldID) != accessedFieldsInAsyncCalls.end())
//       {
//         // retrieve the access type for a field accessed in asynchornous context
//         if (globalFieldAccessInfo.find(fieldID) != globalFieldAccessInfo.end())
//         {
//           auto accType = globalFieldAccessInfo[fieldID];
//           (*childT)->setAccessType(accType);
//           if (accType != AccessType::NOACCESS)
//             concurrentFieldsNum++;
//         }
//       }

//       // optimization conditions
//       bool childNodeNoAccess = ((*childT)->getAccessType() == AccessType::NOACCESS);

//       if (childNodeNoAccess)
//       {
//         savedSyncDataSize += (childDIType->getSizeInBits() / 8);
//         continue;
//       }
//       // check if an accessed field is in the set of shared data, also, assume if the function call from kernel to driver 
//       // will pass shared objects
//       // only check access status under cross boundary case. If not cross, we do not check and simply perform
//       // normal field finding analysis.
//       // alloc attribute
//       numProjectedFields++;
//       std::string fieldAnnotation = inferFieldAnnotation(*childT);
//       if (fieldAnnotation == "[string]")
//         stringNum++;
//       if (DIUtils::isFuncPointerTy(childDIType))
//       {
//         // generate rpc for the indirect function call
//         std::string funcName = DIUtils::getDIFieldName(childDIType);
//         // generate rpc for defined function in isolated driver. Also, if kernel hook a function to a function pointer, we need to caputre this write
//         if (driverExportFuncPtrNames.find(funcName) == driverExportFuncPtrNames.end())
//           continue;
//         // for each function pointer, swap it with the function name registered to the pointer.
//         funcName = switchIndirectCalledPtrName(funcName);
//         Function *indirectFunc = module->getFunction(funcName);
//         if (indirectFunc == nullptr)
//           continue;
//         projection_str << "\t\trpc " << DIUtils::getFuncSigName(DIUtils::getLowestDIType(childDIType), indirectFunc, DIUtils::getDIFieldName(childDIType)) << ";\n";
//         // put all function pointer in a queue, and process them all together later
//         funcPtrQ.push(childT);
//       }
//       else if (DIUtils::isStructPointerTy(childDIType))
//       {
//         // for struct pointer, generate a projection
//         auto tmpName = DIUtils::getDITypeName(childDIType);
//         auto tmpFuncName = funcName;
//         // formatting functions
//         while (tmpName.back() == '*')
//         {
//           tmpName.pop_back();
//           tmpFuncName.push_back('*');
//         }

//         std::string fieldTypeName = tmpName;
//         if (fieldTypeName.find("ops") == std::string::npos) // specific handling for struct ops
//           fieldTypeName = tmpName + "_" + tmpFuncName;
//         else 
//           fieldTypeName = tmpName + "*";

//         auto pos = fieldTypeName.find("const struct");
//         std::string projectStr = "projection ";
//         if (pos != std::string::npos)
//         {
//           fieldTypeName = fieldTypeName.substr(pos + 13);
//           projectStr = "const " + projectStr;
//         }

//         projection_str << "\t\t"
//                        << projectStr << fieldTypeName << fieldAnnotation << " "
//                        << " " << DIUtils::getDIFieldName(childDIType) << ";\n";
//         treeNodeQ.push(childT);
//       }
//       else if (DIUtils::isStructTy(childDIType))
//       {
//         auto fieldTypeName = DIUtils::getDITypeName(childDIType);
//         fieldTypeName.push_back('*');
//         auto pos = fieldTypeName.find("const struct");
//         std::string projectStr = "projection ";
//         if (pos != std::string::npos)
//         {
//           fieldTypeName = fieldTypeName.substr(pos + 13);
//           projectStr = "const " + projectStr;
//         }
//         projection_str << "\t\t"
//                        << projectStr << fieldTypeName << fieldAnnotation << " "
//                        << " " << DIUtils::getDIFieldName(childDIType) << "_" << funcName << ";\n";
//         treeNodeQ.push(childT);
//         continue;
//       }
//       else if (DIUtils::isUnionType(childDIType))
//       {
//         // currently, anonymous union type is not handled.
//         projection_str << "\t\t" << "// union type \n";
//         unionNum++;
//         unNamedUnionNum++;
//         continue;
//       }
//       else
//       {
//         std::string fieldName = DIUtils::getDIFieldName(childDIType);
//         if (!fieldName.empty())
//         {
//           if (fieldName.find("[") != std::string::npos)
//             arrayNum++;
//           projection_str << "\t\t" + DIUtils::getDITypeName(childDIType) << " " << getAccessAttributeName(childT) << " " << DIUtils::getDIFieldName(childDIType) << ";\n";
//         }
//       }

//       if (DIUtils::isVoidPointer(childDIType))
//       {
//         voidPointerNum++;
//         if (voidPointerHasMultipleCasts(*childT))
//           unhandledVoidPointerNum++;
//       }
//       if (DIUtils::isArrayType(childDIType))
//         arrayNum++;
//       if (DIUtils::isSentinelType(childDIType))
//         sentialArrayNum++;
//       // if (DIUtils::isUnionPointerTy(childDIType) || DIUtils::isUnionType(childDIType))
//       //   unionNum++;
//     }

//     // if any child field is accessed, we need to print out the projection for the current struct.
//     if (DIUtils::isStructTy(baseType))
//     {
//       std::stringstream temp;
//       std::string structName = DIUtils::getDITypeName(curDIType); // the name is stored in tag member.
//       // some formatting on struct type
//       auto pos = structName.find("const");
//       if (pos != std::string::npos)
//         structName = structName.substr(pos + 6);
//       pos = structName.find("struct");
//       if (pos != std::string::npos)
//         structName = structName.substr(pos + 7);
//       // remove * in projection definition
//       while (structName.back() == '*')
//         structName.pop_back();

//       // a very specific handling for generating IDL for function struct ops
//       if (structName.find("ops") == std::string::npos)
//       {
//         temp << "\tprojection "
//              << "< " << "struct " << structName << " > " << structName << "_" << funcName << " "
//              << " {\n"
//              << projection_str.str() << " \t}\n\n";
//       }
//       else
//       {
//         if (seenFuncOps.find(structName) == seenFuncOps.end()) // only generate projection for struct ops at the first time we see it.
//         {
//           temp << "\tprojection "
//                << "< " << "struct " << structName << " > " << structName << " "
//                << " {\n"
//                << projection_str.str() << " \t}\n\n";
//           seenFuncOps.insert(structName);
//         }
//       }
//       projection_str = std::move(temp);
//     }
//     else if (DIUtils::isUnionType(baseType))
//     {
//       std::stringstream temp;
//       std::string unionName = DIUtils::getDIFieldName(baseType);
//       // a very specific handling for generating IDL for function ops
//       temp << "\tprojection "
//            << "< union " << unionName << " > " << unionName << "_" << funcName << " "
//            << " {\n"
//            << projection_str.str() << " \t}\n\n";
//       projection_str = std::move(temp);
//     }
//     else
//       projection_str.str(""); 

//     idl_file << projection_str.str();
//   }
// }

// std::set<std::string> pdg::AccessInfoTracker::computeAccessFieldsForArg(ArgumentWrapper *argW, DIType* rootDIType)
// {
//   std::set<std::string> accessedFields;
//   // iterate through each node, find their addr variables and then analyze the accesses to the addr variables
//   for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
//   {
//     // hanlde function pointer exported by driver domain
//     DIType* fieldDIType = (*treeI)->getDIType();
//     std::string fieldName = DIUtils::getDIFieldName(fieldDIType);
//     if (fieldName.find("ops") != std::string::npos)
//     {
//         std::string fieldID = computeFieldID(rootDIType, fieldDIType);
//         accessedFields.insert(fieldID);
//         continue;
//     }

//     if (DIUtils::isFuncPointerTy(fieldDIType))
//     {
//       if (driverExportFuncPtrNames.find(fieldName) != driverExportFuncPtrNames.end())
//       {
//         std::string fieldID = computeFieldID(rootDIType, fieldDIType);
//         accessedFields.insert(fieldID);
//       }
//      continue; 
//     }

//     // get valdep pair, and check for intraprocedural accesses
//     auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
//     for (auto valDepPair : valDepPairList)
//     {
//       auto dataW = valDepPair.first->getData();
//       AccessType accType = getAccessTypeForInstW(dataW);
//       if (accType != AccessType::NOACCESS)
//       {
//         std::string fieldID = computeFieldID(rootDIType, fieldDIType);
//         accessedFields.insert(fieldID);
//       }
//     }
//   }
//   return accessedFields;
// }

// void pdg::AccessInfoTracker::computeInterprocArgAccessInfo(ArgumentWrapper* argW, Function &F, std::set<Function*> receiverDomainTrans)
// {
//   errs() << "start computing inter proc info for: " << F.getName() << " - " << argW->getArg()->getArgNo() << "\n";
//   auto &pdgUtils = PDGUtils::getInstance();
//   auto instMap = pdgUtils.getInstMap();
//   for (auto treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
//   {
//     // if has an address variable, then simply check if any alias exists in the transitive closure.
//     auto valDepPairList = PDG->getNodesWithDepType(*treeI, DependencyType::VAL_DEP);
//     for (auto valDepPair : valDepPairList)
//     {
//       auto dataW = valDepPair.first->getData();
//       // compute interprocedural access info in the receiver domain
//       std::set<Value *> aliasSet = findAliasInDomain(*(dataW->getInstruction()), F, receiverDomainTrans);
//       for (auto alias : aliasSet)
//       {
//         if (Instruction *i = dyn_cast<Instruction>(alias))
//         {
//           // analyze alias read/write info
//           auto aliasW = instMap[i];
//           AccessType aliasAccType = getAccessTypeForInstW(aliasW);
//           if (static_cast<int>(aliasAccType) > static_cast<int>((*treeI)->getAccessType()))
//             (*treeI)->setAccessType(aliasAccType);
//         }
//       }
//     }

//     // if the list is empty, then this node doesn't has any address variable inside a function
//     if (valDepPairList.size() != 0)
//       continue;
//     // no need for checking the root node 
//     if (tree<InstructionWrapper*>::depth(treeI) == 0)
//       continue;
//     //  get parent node
//     auto parentI = tree<InstructionWrapper*>::parent(treeI);
//     // check if parent is a struct 
//     DIType* parentDIType = (*parentI)->getDIType();
//     if (!DIUtils::isStructTy(parentDIType))
//       continue;
//     auto parentValDepList = PDG->getNodesWithDepType(*parentI, DependencyType::VAL_DEP);
//     if (parentValDepList.size() == 0)
//       continue;
    
//     unsigned childIdx = (*treeI)->getNodeOffset();
//     auto dataW = parentValDepList.front().first->getData();
//     assert(dataW->getInstruction() && "cannot find parent instruction while computing interproce access info.");
//     // get struct layout
//     DIType* childDIType = (*treeI)->getDIType();
//     auto dataLayout = module->getDataLayout();
//     std::string parentStructName = DIUtils::getDIFieldName(parentDIType);
//     parentStructName = "struct." + parentStructName;
//     auto parentLLVMStructTy = module->getTypeByName(StringRef(parentStructName));
//     if (!parentLLVMStructTy)
//       continue;
//     if (childIdx >= parentLLVMStructTy->getNumElements())
//       continue;
//     // compute element offset in byte
//     auto parentStructLayout = dataLayout.getStructLayout(parentLLVMStructTy);
//     unsigned childOffsetInByte = parentStructLayout->getElementOffset(childIdx);
//     auto interProcAlias = findAliasInDomainWithOffset(*dataW->getInstruction(), F, childOffsetInByte, receiverDomainTrans);
//     for (auto alias : interProcAlias)
//     {
//       if (Instruction *i = dyn_cast<Instruction>(alias))
//       {
//         auto aliasW = instMap[i];
//         AccessType aliasAccType = getAccessTypeForInstW(aliasW);
//         if (static_cast<int>(aliasAccType) > static_cast<int>((*treeI)->getAccessType()))
//           (*treeI)->setAccessType(aliasAccType);
//       }
//     }
//   }
// }