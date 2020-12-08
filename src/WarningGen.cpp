#ifndef WARNING_GEN_
#define WARNING_GEN_
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <llvm/Support/raw_ostream.h>
#include "DebugInfoUtils.hpp"
#include "PDGUtils.hpp"
#include "ProgramDependencyGraph.hpp"

#include <set>
#include <queue>

namespace pdg
{
  using namespace llvm;

  class WarningGen : public ModulePass
  {
  public:
    static char ID;
    WarningGen() : ModulePass(ID)
    {
    }

    bool runOnModule(Module &M)
    {
      PDG = &getAnalysis<ProgramDependencyGraph>();
      di_type_object_tree_map_ = PDG->getGlobalTypeTrees();
      auto &pdgUtils = PDGUtils::getInstance();
      pdgUtils.constructFuncMap(M);
      warningNum = 0;
      atomicOpWarningNum = 0;
      // setups
      setupLockPairMap();
      pdgUtils.computeCrossDomainTransFuncs(M, crossDomainTransFuncs);
      // computeAsyncFuncs(M);
      computePtrToSharedData(pdgUtils.computeCrossDomainFuncs(M), M);
      CSWarningFile.open("CSWarning.txt");
      AtomicWarningFile.open("AtomicWarning.txt");
      // print warnings for critical sections
      computeCriticalSections(M);
      printWarningForCS();
      // print warnings for atomic operations
      printWarningsForAtomicOperation(M);
      CSWarningFile.close();
      AtomicWarningFile.close();
      auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
      ksplit_stats_collector.PrintAtomicRegionStats();
      return false;
    }

    void setupLockPairMap()
    {
      lockPairMap.insert(std::make_pair("mutex_lock", "mutex_unlock"));
      lockPairMap.insert(std::make_pair("_raw_spin_lock", "_raw_spin_unlock"));
      lockPairMap.insert(std::make_pair("_raw_spin_lock_irq", "_raw_spin_unlock_irq"));
      lockPairMap.insert(std::make_pair("global_lock", "global_unlock"));
    }

    void computeCriticalSections(Module& M)
    {
      // a list of locking functions we are looking for
      // std::set<Function *> reachableFuncs;
      // reachableFuncs.insert(crossDomainTransFuncs.begin(), crossDomainTransFuncs.end());
      // reachableFuncs.insert(asyncFuncs.begin(), asyncFuncs.end());

      for (auto &F : M)
      {
        if (F.isDeclaration())
          continue;
        auto csInFunc = collectCSInFunc(F); // find cs in each defined functions
        CS.insert(csInFunc.begin(), csInFunc.end());
      }
      auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
      ksplit_stats_collector.SetNumberOfCriticalSection(CS.size());
      errs() << "number of CS: " << CS.size() << "\n";
    }

    std::set<std::pair<Instruction *, Instruction *>> collectCSInFunc(Function &F)
    {
      std::set<std::pair<Instruction *, Instruction *>> csInFunc;
      for (inst_iterator I = inst_begin(F); I != inst_end(F); ++I)
      {
        // 1. find all lock instruction that call to acquire a lock
        if (CallInst *CI = dyn_cast<CallInst>(&*I))
        {
          auto calledFunc = CI->getCalledFunction();
          if (calledFunc == nullptr)
            continue;
          std::string lockCall = calledFunc->getName();
          if (lockPairMap.find(lockCall) == lockPairMap.end())
            continue;

          std::set<Instruction *> unlockInsts;
          for (inst_iterator II = I; II != inst_end(F); ++II)
          {
            Instruction *i = &*II;
            if (CallInst *unlockCI = dyn_cast<CallInst>(i))
            {
              if (Function *calledFunc = dyn_cast<Function>(unlockCI->getCalledValue()->stripPointerCasts()))
              {
                if (calledFunc->getName().str() == lockPairMap[lockCall])
                  unlockInsts.insert(&*II);
              }
            }
          }

          for (Instruction *unlockCall : unlockInsts)
          {
            if (CallInst *unlockCI = dyn_cast<CallInst>(unlockCall))
            {
              // if (useSameLock(CI, unlockCI))
              csInFunc.insert(std::make_pair(CI, unlockCall));
            }
          }
        }
      }
      return csInFunc;
    }

    bool useSameLock(CallInst* lockInst, CallInst* unlockInst)
    {
      auto &pdgUtils = PDGUtils::getInstance();
      auto instMap = pdgUtils.getInstMap();
      auto lockInstUsedLock = getUsedLock(lockInst);
      auto unlockInstUsedLock = getUsedLock(unlockInst);
      if (!lockInstUsedLock || !unlockInstUsedLock)
        return false;
      if (Instruction *i1 = dyn_cast<Instruction>(lockInstUsedLock))
      {
        std::set<InstructionWrapper *> lockAliasSet;
        PDG->getAllAlias(i1, lockAliasSet);
        errs() << "alias size: " << lockAliasSet.size() << "\n";
        if (Instruction *i2 = dyn_cast<Instruction>(unlockInstUsedLock))
        {
          errs() << *i1 << " - " << *i2 << "\n";
          if (lockAliasSet.find(instMap[i2]) != lockAliasSet.end())
            return true;
        }
      }
      return false;
    }

    Value *getUsedLock(CallInst *CI)
    {
      if (auto lockInstl = dyn_cast<BitCastInst>(CI->getOperand(0)))
      {
        auto usedLockInstance = lockInstl->getOperand(0);
        if (auto gep = dyn_cast<GetElementPtrInst>(usedLockInstance))
        {
          if (auto li = dyn_cast<LoadInst>(gep->getPointerOperand()))
            return li;
            // return li->getPointerOperand();
        }
      }
      return nullptr;
    }

    std::set<Instruction *> collectInstsInCS(std::pair<Instruction *, Instruction *> lockPair, Function &F)
    {
      std::set<Instruction *> instSet;
      auto instI = inst_begin(F);
      while (instI != inst_end(F))
      {
        if (&*instI == lockPair.first)
          break;
        instI++;
      }

      while (&*instI != lockPair.second && instI != inst_end(F))
      {
        instSet.insert(&*instI);
        instI++;
      }

      return instSet;
    }

    std::set<LoadInst *> collectLoadInstsInCS(std::pair<Instruction *, Instruction *> lockPair, Function &F, std::set<Instruction*>& instsInCS)
    {
      std::set<LoadInst *> loadInstsInCS;
      for (auto inst : instsInCS)
      {
        if (LoadInst *li = dyn_cast<LoadInst>(inst))
        {
          loadInstsInCS.insert(li);
        }
      }
      return loadInstsInCS;
    }

    std::set<StoreInst *> collectStoreInstsInCS(std::pair<Instruction *, Instruction *> lockPair, Function &F, std::set<Instruction *> &instsInCS)
    {
      std::set<StoreInst *> storeInstsInCS;
      for (auto inst : instsInCS)
      {
        if (StoreInst *si = dyn_cast<StoreInst>(inst))
        {
          storeInstsInCS.insert(si);
        }
      }
      return storeInstsInCS;
    }

    void printWarningForCS()
    {
      auto &pdgUtils = PDGUtils::getInstance();
      auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
      auto inst_di_type_map = pdgUtils.getInstDITypeMap();
      // collect store insts for each cs
      for (auto lockPair : CS)
      {
        bool is_cs_shared = false;
        Function *CSFunc = lockPair.first->getFunction();
        CSWarningFile << "Critical Section found in func: " << CSFunc->getName().str() << "\n";
        auto instsInCS = collectInstsInCS(lockPair, *CSFunc);
        for (auto inst : instsInCS)
        {
          if (!is_cs_shared && inst_di_type_map.find(inst) != inst_di_type_map.end())
          {
            DIType *inst_di_type = inst_di_type_map[inst];
            if (di_type_object_tree_map_.find(inst_di_type) != di_type_object_tree_map_.end())
              is_cs_shared = true;
          }
        }
        // find data read in the CS, synchronize these data at the beginning of critical sections
        auto loadInstsInCS = collectLoadInstsInCS(lockPair, *CSFunc, instsInCS);
        CSWarningFile << "read data: \n";
        for (auto loadInst : loadInstsInCS)
        {
          Value* readVal = loadInst->getPointerOperand();
          if (!isSharedData(readVal))
            continue;
          if (Instruction *i = dyn_cast<Instruction>(readVal))
          {
            CSWarningFile << "\t" << getAccessedDataName(*readVal) << "\n";
            is_cs_shared = true;
          }
        }
        CSWarningFile << " ----------------------------------------------- \n";

        // find data modified in the CS, synchronize them at the end of critical sections
        CSWarningFile << "modified data: \n";
        auto storeInstsInCS = collectStoreInstsInCS(lockPair, *CSFunc, instsInCS);
        for (auto storeInst : storeInstsInCS)
        {
          Value *modifiedVal = storeInst->getPointerOperand();
          if (!isSharedData(modifiedVal))
            continue;
          if (Instruction *i = dyn_cast<Instruction>(modifiedVal))
          {
            // CSWarningFile << "\tmodified data: " << DIUtils::getDIFieldName(instDIType) << "\n";
            CSWarningFile << "\t" << getAccessedDataName(*modifiedVal) << "\n";
            // std::string instStr;
            // llvm::raw_string_ostream rs(instStr);
            // rs << *i << "\n";
            // CSWarningFile << "\tmodified val: " << rs.str() << "\n";
            // printCallChain(CSFunc, CSWarningFile);
            is_cs_shared = true;
          }
        }
        CSWarningFile << " ----------------------------------------------- \n";
        if (is_cs_shared)
          ksplit_stats_collector.IncreaseNumberOfCriticalSectionSharedData();
      }
    }

    std::string getAccessedDataName(Value &accessedVal)
    {
      // if the modified data does not belonging to a struct, we directly print out the data name.
      auto &pdgUtils = PDGUtils::getInstance();
      auto instMap = pdgUtils.getInstMap();
      auto funcMap = pdgUtils.getFuncMap();
      if (Instruction *i = dyn_cast<Instruction>(&accessedVal))
      {
        // get corresponding tree node for a instruction
        InstructionWrapper *instW = instMap[i];
        errs() << "inst: " << *i << " - " << i->getFunction()->getName() << "\n";
        auto depTreeNodes = PDG->getNodesWithDepType(instW, DependencyType::VAL_DEP);
        errs() << "dep node size: " << depTreeNodes.size() << "\n";
        if (depTreeNodes.size() != 1)
        {
          // if the modified data is a strcut field, we need to print the format: struct->field
          auto instDITypeMap = pdgUtils.getInstDITypeMap();
          if (instDITypeMap.find(i) != instDITypeMap.end())
          {
            DIType *instDIType = instDITypeMap[i];
            // OS << "modified data name: " << DIUtils::getDIFieldName(instDIType) << "\n";
            return DIUtils::getDIFieldName(instDIType);
          }
        }
        else
        {
          // if accessed variable has dependency with tree node, then this is likely a field in struct. Need to print the struct hierachy here
          TreeTypeWrapper* treeW = (TreeTypeWrapper*)(depTreeNodes[0].first->getData());
          tree<InstructionWrapper*> objectTree;
          // global and local parameter both have tree associated with them. Need to retrive the tree depends on the type
          if (treeW->getValue() != nullptr && isa<GlobalVariable>(treeW->getValue()))
          {
            GlobalVariable *GV = dyn_cast<GlobalVariable>(treeW->getValue());
            objectTree = PDG->getGlobalObjectTrees()[GV];
          }
          else
          {
            auto arg = treeW->getArgument();
            if (arg == nullptr)
              return "none";
            FunctionWrapper *funcW = funcMap[arg->getParent()];
            ArgumentWrapper *argW = funcW->getArgWByArg(*arg);
            objectTree = argW->getTree(TreeType::FORMAL_IN_TREE);
          }
          if (objectTree.size() == 0)
            return "none";
          auto depTreeNode = depTreeNodes[0]; // the size should always be 1
          std::vector<std::string> nameList;
          auto treeIter = findTreeIter(treeW, objectTree);
          if (treeIter == objectTree.end())
            return "none";
          auto fieldName = DIUtils::getDIFieldName((*treeIter)->getDIType());
          nameList.push_back(fieldName);
          while (tree<InstructionWrapper *>::depth(treeIter) != 0)
          {
            // get parent first, because we need to get the read value, which is the parent's val
            treeIter = tree<InstructionWrapper *>::parent(treeIter);
            auto name = DIUtils::getDIFieldName((*treeIter)->getDIType());
            nameList.push_back(name);
          }

          auto dataStr = constructDataStr(nameList);
          if (treeW->getValue() != nullptr && isa<GlobalVariable>(treeW->getValue()))
            dataStr = "global var: " + dataStr;
          // OS << "Modified data name: " << 
          return dataStr;
        }
      }
      return "none";
    }

    std::string constructDataStr(std::vector<std::string> nameList)
    {
      std::string ret;
      for (auto iter = nameList.rbegin(); iter != nameList.rend(); ++iter)
      {
        ret += *iter;
        if (std::next(iter) != nameList.rend())
          ret += "->";
      }
      return ret;
    }

    tree<InstructionWrapper *>::iterator findTreeIter(InstructionWrapper *instW, tree<InstructionWrapper *> &tree)
    {
      for (auto iter = tree.begin(); iter != tree.end(); ++iter)
      {
        if (*iter == instW)
          return iter;
      }
      return tree.end();
    }

    void printCallChain(Function *CSFunc, std::ofstream &OS)
    {
      Module &M = *CSFunc->getParent();
      // 1. obtian all the boundary functions
      auto &pdgUtils = PDGUtils::getInstance();
      auto boundaryFuncs = pdgUtils.computeCrossDomainFuncs(M);
      // 2. test if the CSFunc can be reached from the boundary funcs.
      for (auto boundaryFunc : boundaryFuncs)
      {
        auto pathes = computePathes(*boundaryFunc, *CSFunc);
        if (pathes.empty())
          continue;
        for (auto path : pathes)
          printPath(path, OS);
      }
    }

    void printPath(std::vector<Function *> path, std::ofstream &OS)
    {
      if (path.size() <= 1)
        return;
      for (auto func : path)
      {
        OS << func->getName().str();
        if (func != path.back())
          OS << " --> ";
      }
      OS << "\n";
    }

    std::vector<std::vector<Function *>> computePathes(Function &F1, Function &F2)
    {
      std::vector<std::vector<Function *>> res;
      std::set<Function *> seenFuncs;
      computePath(res, F1, F2, {}, seenFuncs);
      return res;
    }

    void computePath(std::vector<std::vector<Function *>> &res, Function &curFunc, Function &targetFunc, std::vector<Function *> path, std::set<Function *> &seenFuncs)
    {
      if (curFunc.isDeclaration() || curFunc.empty())
        return;
      auto &pdgUtils = PDGUtils::getInstance();
      auto funcMap = pdgUtils.getFuncMap();
      if (seenFuncs.find(&curFunc) != seenFuncs.end())
        return;
      seenFuncs.insert(&curFunc);
      path.push_back(&curFunc);
      if (&curFunc == &targetFunc)
        res.push_back(path);
      else
      {
        auto funcW = funcMap[&curFunc];
        auto callInstList = funcW->getCallInstList();
        for (auto callInst : callInstList)
        {
          if (callInst->getCalledValue() == nullptr)
            continue;
          if (Function *calledFunc = dyn_cast<Function>(callInst->getCalledValue()->stripPointerCasts()))
          {
            if (calledFunc->isDeclaration() || calledFunc->empty())
              continue;
            computePath(res, *calledFunc, targetFunc, path, seenFuncs);
          }
        }
      }
    }

    bool isSharedData(Value *val)
    {
      return (ptrToSharedData.find(val) != ptrToSharedData.end());
    }

    void computePtrToSharedData(std::set<Function *> crossDomainFuncs, Module &M)
    {
      // auto sharedGlobalVars = PDG->getSharedGlobalVars();
      // for (auto sharedGlobalVar : sharedGlobalVars)
      // {
      //   auto valAccessSharedGlobalVar = computeValsAccessGlobalVar(sharedGlobalVar);
      //   ptrToSharedData.insert(valAccessSharedGlobalVar.begin(), valAccessSharedGlobalVar.end());
      // }

      auto transFuncFromInit = computeReachableFuncsFromInit(M);
      crossDomainFuncs.insert(transFuncFromInit.begin(), transFuncFromInit.end());
      for (Function &func : M)
      {
        for (auto argI = func.arg_begin(); argI != func.arg_end(); ++argI)
        {
          // start finding all pointers that are derived from the arg
          // get alloca instruction first. Then start deriving
          auto ptrDerivedFromArg = computePtrDerivedFromArg(*argI);
          ptrToSharedData.insert(ptrDerivedFromArg.begin(), ptrDerivedFromArg.end());
        }
      }
      errs() << "number of ptr to shared data: " << ptrToSharedData.size() << "\n";
    }

    std::set<Function *> computeReachableFuncsFromInit(Module &M)
    {
      std::set<Function *> initTransFuncs;
      auto &pdgUtils = PDGUtils::getInstance();
      Function *initFunc = M.getFunction("dummy_init_module");
      if (initFunc != nullptr)
      {
        auto initFuncTrans = pdgUtils.computeTransitiveClosure(*initFunc);
        for (auto transFunc : initFuncTrans)
        {
          if (transFunc->isDeclaration() || transFunc->empty())
            continue;
          initTransFuncs.insert(transFunc);
        }
      }
      return initTransFuncs;
    }

    std::set<Value *> computeValsAccessGlobalVar(GlobalVariable *sharedGlobalVar)
    {
      std::set<Value*> ptrsToGlobalVar;
      auto &pdgUtils = PDGUtils::getInstance();
      auto instMap = pdgUtils.getInstMap();
      for (auto user : sharedGlobalVar->users())
      {
        auto derivedPtrs = computeDerivedPtrsFromVal(user);
        ptrsToGlobalVar.insert(derivedPtrs.begin(), derivedPtrs.end());
      }
      return ptrsToGlobalVar;
    }

    std::set<Value *> computePtrDerivedFromArg(Argument &arg)
    {
      Function &F = *arg.getParent();
      auto argAlloc = PDG->getArgAllocaInst(arg);
      return computeDerivedPtrsFromVal(argAlloc);
    }

    std::set<Value *> computeDerivedPtrsFromVal(Value* val)
    {
      std::set<Value *> ptrSet;
      if (!val)
        return ptrSet;
      if (!isa<Instruction>(val))
        return ptrSet;
      auto sourceInst = cast<Instruction>(val);
      auto &pdgUtils = PDGUtils::getInstance();
      auto instMap = pdgUtils.getInstMap();
      std::queue<InstructionWrapper *> instQ;
      std::set<InstructionWrapper *> seenInstW;
      auto startInstW = instMap[sourceInst];
      instQ.push(startInstW);
      seenInstW.insert(startInstW);
      while (!instQ.empty())
      {
        InstructionWrapper *instW = instQ.front();
        instQ.pop();
        // add alias to ptrSet
        auto aliasDepPairList = PDG->getNodesWithDepType(instW, DependencyType::DATA_ALIAS);
        for (auto depPair : aliasDepPairList)
        {
          auto dataW = const_cast<InstructionWrapper *>(depPair.first->getData());
          auto dataInst = dataW->getInstruction();
          if (!dataInst)
            continue;

          ptrSet.insert(dataInst);
          if (seenInstW.find(dataW) != seenInstW.end())
            continue;
          seenInstW.insert(dataW);
          instQ.push(dataW);
        }

        // add derived pointers through def-use relation
        auto defUseDepPairList = PDG->getNodesWithDepType(instW, DependencyType::DATA_DEF_USE);
        for (auto depPair : defUseDepPairList)
        {
          auto dataW = const_cast<InstructionWrapper *>(depPair.first->getData());
          Instruction *depInst = dataW->getInstruction();
          if (depInst == nullptr)
            continue;

          if (seenInstW.find(dataW) != seenInstW.end())
            continue;
          seenInstW.insert(dataW);
          instQ.push(dataW);

          if (depInst->getType()->isPointerTy())
            ptrSet.insert(depInst);

          // handle interprocedural derived ptrs
          if (CallInst *CI = dyn_cast<CallInst>(depInst))
          {
            int callArgIdx = getCallOperandIdx(dataW->getInstruction(), CI);
            if (callArgIdx < 0) // invalid idx
              continue;
            if (Function *callee = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts()))
            {
              if (callee->isDeclaration() || callee->empty())
                continue;

              int argIdx = 0;
              for (auto calleeArgI = callee->arg_begin(); calleeArgI != callee->arg_end(); ++calleeArgI)
              {
                if (argIdx == callArgIdx)
                {
                  auto ptrToSharedDataInCallee = computePtrDerivedFromArg(*calleeArgI);
                  ptrSet.insert(ptrToSharedDataInCallee.begin(), ptrToSharedDataInCallee.end());
                  break;
                }
                argIdx++;
              }
            }
          }
        }
      }
      return ptrSet;
    }

    int getCallOperandIdx(Value *operand, CallInst *callInst)
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

    // atomic Operation
    std::string getModifiedDataName(Instruction *inst)
    {
      std::string modifiedNames = "";
      auto &pdgUtils = PDGUtils::getInstance();
      auto instMap = pdgUtils.getInstMap();
      auto instDITypeMap = pdgUtils.getInstDITypeMap();
      // find alais for the inst
      auto aliasDep = PDG->getNodesWithDepType(instMap[inst], DependencyType::DATA_ALIAS);
      for (auto depPair : aliasDep)
      {
        auto depInstW = const_cast<InstructionWrapper *>(depPair.first->getData());
        Instruction *aliasInst = depInstW->getInstruction();
        if (aliasInst == nullptr)
          continue;
        if (instDITypeMap.find(aliasInst) != instDITypeMap.end())
        {
          auto instDIType = instDITypeMap[aliasInst];
          if (instDIType != nullptr)
          {
            std::string s = DIUtils::getDIFieldName(instDIType);
            if (s == "no name")
              continue;
            modifiedNames = modifiedNames + " | " + s;
          }
        }
      }
      return modifiedNames;
    }

    bool isAtomicAsmString(std::string str)
    {
      return (str.find("lock") != std::string::npos);
    }

    bool isAtomicOperation(Instruction *inst)
    {
      if (CallInst *ci = dyn_cast<CallInst>(inst))
      {
        if (!ci->isInlineAsm())
          return false;
        if (InlineAsm *ia = dyn_cast<InlineAsm>(ci->getCalledValue()))
        {
          auto asmString = ia->getAsmString();
          if (isAtomicAsmString(asmString))
            return true;
        }
      }
      return false;
    }

    void printWarningsForAtomicOperation(Module &M)
    {
      auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
      for (auto &func : M)
      {
        if (func.isDeclaration() || func.empty())
          continue;
        for (auto instI = inst_begin(func); instI != inst_end(func); instI++)
        {
          if (isAtomicOperation(&*instI))
          {
            ksplit_stats_collector.IncreaseNumberOfAtomicOperation();
            auto modifiedAddrVar = instI->getOperand(0);
            if (ptrToSharedData.find(modifiedAddrVar) != ptrToSharedData.end())
            {
              printWarningForSharedVarInAtomicOperation(*modifiedAddrVar, *instI, func);
              ksplit_stats_collector.IncreaseNumberOfAtomicOperationSharedData();
            }
          }
        }
      }
    }

    void printWarningForSharedVarInAtomicOperation(Value &modifiedAddrVar, Instruction &atomicOp, Function &F)
    {
      auto disp = F.getSubprogram();
      if (!isa<Instruction>(&modifiedAddrVar))
        return;
      Instruction *i = cast<Instruction>(&modifiedAddrVar);
      // std::string modifiedDataName = getModifiedDataName(i);
      // if (modifiedDataName.empty())
      //   return;
      atomicOpWarningNum++;
      AtomicWarningFile << " ------------------------------------------------------- \n";
      AtomicWarningFile << "[WARNING " << atomicOpWarningNum << " | ATOMIC OPERATION ON SHARED DATA]: \n";
      AtomicWarningFile << "Accessed in " << disp->getFilename().str() << " in function " << F.getName().str() << "\n";
      // substitue the modified var with its data dependent component
      Value* modifiedVar = findSourceDependentVar(i);
      std::string varStr;
      llvm::raw_string_ostream rs1(varStr);
      rs1 << *modifiedVar;
      AtomicWarningFile << "substituded var: " << rs1.str() << "\n";
      AtomicWarningFile << getAccessedDataName(*modifiedVar);
      AtomicWarningFile << "Line Number: " << atomicOp.getDebugLoc()->getLine() << "\n";
      // errs() << "Modify instruction: " << atomicOp << "\n";
      std::string instStr;
      llvm::raw_string_ostream rs(instStr);
      rs << *i;
      AtomicWarningFile << "Accessed IR Variable: " << rs.str() << "\n";
      printCallChain(&F, AtomicWarningFile);
    }

    Value *findSourceDependentVar(Instruction* inst)
    {
      auto &pdgUtils = PDGUtils::getInstance();
      auto instMap = pdgUtils.getInstMap();
      auto instW = instMap[inst];
      auto dataRawReverseDepPairs = PDG->getNodesWithDepType(instW, DependencyType::DATA_RAW_REVERSE);
      if (dataRawReverseDepPairs.size() == 0)
        return inst;
      auto depPair = dataRawReverseDepPairs[0]; // a load can only has one RAW reverse edge to a store instruction
      auto depStoreInst = depPair.first->getData()->getInstruction();
      if (StoreInst *st = dyn_cast<StoreInst>(depStoreInst))
        return st->getValueOperand();
      return inst;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const
    {
      AU.addRequired<ProgramDependencyGraph>();
      AU.setPreservesAll();
    }

  private:
    unsigned warningNum;
    unsigned atomicOpWarningNum;
    std::map<std::string, std::string> lockPairMap;
    std::set<Function *> crossDomainTransFuncs;
    // std::set<Function *> asyncFuncs;
    std::set<Function *> executedFuncs;
    std::set<std::pair<Instruction *, Instruction *>> CS;
    std::set<Value *> ptrToSharedData;
    std::ofstream CSWarningFile;
    std::ofstream AtomicWarningFile;
    ProgramDependencyGraph *PDG;
    std::map<DIType*, tree<InstructionWrapper*>> di_type_object_tree_map_;
  }; // namespace pdg

  char WarningGen::ID = 0;
  static RegisterPass<WarningGen> WarningGen("warn-gen", "Warning Generation", false, true);
} // namespace pdg

#endif