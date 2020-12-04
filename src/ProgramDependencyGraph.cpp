#include "ProgramDependencyGraph.hpp"

using namespace llvm;

char pdg::ProgramDependencyGraph::ID = 0;

int pdg::EXPAND_LEVEL;
int pdg::USEDEBUGINFO;
int pdg::SHARED_DATA_FLAG;
llvm::cl::opt<int> expandLevel("l", llvm::cl::desc("Parameter tree expand level"), llvm::cl::value_desc("level"));
llvm::cl::opt<int> useDebugInfo("d", llvm::cl::desc("use debug information"), llvm::cl::value_desc("debugInfo"));
llvm::cl::opt<int> SharedDataFlag("sd", llvm::cl::desc("turn on shared data optimization"), llvm::cl::value_desc("shared_data"));

void pdg::ProgramDependencyGraph::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.addRequired<sea_dsa::DsaAnalysis>();
  // AU.addRequired<ControlDependencyGraph>();
  AU.addRequired<DataDependencyGraph>();
  AU.setPreservesAll();
}

bool pdg::ProgramDependencyGraph::runOnModule(Module &M)
{
  if (!expandLevel)
    expandLevel = 4;
  if (!useDebugInfo)
    useDebugInfo = 0;
  if (!SharedDataFlag)
    SharedDataFlag = 0;
  EXPAND_LEVEL = expandLevel;
  USEDEBUGINFO = useDebugInfo;
  SHARED_DATA_FLAG = SharedDataFlag;
  errs() << "Expand level " << EXPAND_LEVEL << "\n";
  errs() << "Using Debug Info " << USEDEBUGINFO << "\n";
  errs() << "Shared Data Optimization On: " << SharedDataFlag << "\n";

  module = &M;
  auto &pdgUtils = PDGUtils::getInstance();
  pdgUtils.constructFuncMap(M);
  pdgUtils.collectGlobalInsts(M);
  unsafeTypeCastNum = 0;
  // start constructing points to graph
  sea_dsa::DsaAnalysis *m_dsa = &getAnalysis<sea_dsa::DsaAnalysis>();
  pdgUtils.setDsaAnalysis(m_dsa);
  // compute shared struct types
  auto sharedTypes = DIUtils::collectSharedDITypes(M, pdgUtils.computeCrossDomainFuncs(M), EXPAND_LEVEL);
  errs() << "number of found shared struct type: " << sharedTypes.size() << "\n";
  // compute a set of functions that need PDG construction
  std::set<Function*> funcsNeedPDGConstruction;
  cross_domain_funcs_ = pdgUtils.computeCrossDomainFuncs(M);
  pdgUtils.computeCrossDomainTransFuncs(M, funcsNeedPDGConstruction);
  errs() << "Num of functions need PDG construction: " << funcsNeedPDGConstruction.size() << "\n";
  unsigned totalFuncInModule = 0;
  // start building pdg for each function
  for (Function *F : funcsNeedPDGConstruction)
  {
    if (F->isDeclaration() || F->empty())
      continue;
    totalFuncInModule++;
    buildPDGForFunc(F);
  }
  errs() << "total num of func in module: " << totalFuncInModule << "\n";
  errs()  << "Finish PDG Construction\n";
  auto driverDomainFuncs = pdgUtils.computeDriverDomainFuncs(M);
  auto kernelDomainFuncs = pdgUtils.computeKernelDomainFuncs(M);
  // collectSharedGlobalVars(driverDomainFuncs, kernelDomainFuncs);
  // errs() << "shared global var size: " << sharedGlobalVars.size() << "\n";
  // buildObjectTreeForGlobalVars();
  // connectGlobalObjectTreeWithAddressVars(funcsNeedPDGConstruction);
  if (SharedDataFlag)
  {
    errs() << "finish connecting global trees with users\n";
    buildGlobalTypeTrees(sharedTypes);
    errs() << "finish building global type trees\n";
    collectInstsWithDIType(funcsNeedPDGConstruction);
    connectGlobalTypeTreeWithAddressVars();
    errs() << "finish connecting global type trees with addr variables\n";
  }
  return false;
}

void pdg::ProgramDependencyGraph::collectInstsWithDIType(std::set<Function *> &search_domain)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  auto inst_di_type_map = pdgUtils.getInstDITypeMap();
  for (Function &F : *module)
  {
    if (F.isDeclaration() || F.empty())
      continue;
    if (search_domain.find(&F) == search_domain.end())
      continue;
    for (auto instI = inst_begin(F); instI != inst_end(F); ++instI)
    {
      Instruction* i = &*instI;
      if (inst_di_type_map.find(i) == inst_di_type_map.end())
        continue;
      // here, we don't use getRawName, as we want the instruction with struct type, not including the struct pointers.
      std::string inst_di_type_name = DIUtils::getDITypeName(inst_di_type_map[i]);
      auto iter = shared_data_name_and_instw_map_.find(inst_di_type_name);
      if (iter != shared_data_name_and_instw_map_.end())
      {
        iter->second.insert(instMap[i]);
      }
    }
  }
}

void pdg::ProgramDependencyGraph::buildPDGForFunc(Function *Func)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
  // cdg = &getAnalysis<ControlDependencyGraph>(*Func);
  auto ddg = &getAnalysis<DataDependencyGraph>(*Func);

  for (InstructionWrapper *instW : pdgUtils.getFuncInstWMap()[Func])
  {
    addNodeDependencies(instW, ddg);
    if (isUnsafeTypeCast(instW->getInstruction()))
    {
      ksplit_stats_collector.IncreaseNumberOfUnsafeCastedStructPointer();
    }
  }

  if (!pdgUtils.getFuncMap()[Func]->hasTrees())
  {
    buildFormalTreeForFunc(Func);
  }

  // errs() << "finish building PDG for: " << Func->getName() << "\n";
}

std::set<Function *> pdg::ProgramDependencyGraph::computeFunctionsNeedPDGConstruction(Module &M)
{
  std::set<Function *> funcSet;
  auto &pdgUtils = PDGUtils::getInstance();
  // 1. get the set of cross-domain functions.
  std::set<Function *> crossDomainFuncs = pdgUtils.computeCrossDomainFuncs(M);
  // 2. for each cross-domain function, compute its transitive closure
  errs() << "cross domain function size: " << crossDomainFuncs.size() << "\n";
  for (auto F : crossDomainFuncs)
  {
    auto transFuncs = pdgUtils.computeTransitiveClosure(*F);
    funcSet.insert(transFuncs.begin(), transFuncs.end());
  }
  return funcSet;
}

bool pdg::ProgramDependencyGraph::processIndirectCallInst(CallInst *CI, InstructionWrapper *instW)
{
  auto &pdgUtils = PDGUtils::getInstance();
  Type *t = CI->getCalledValue()->getType();
  FunctionType *funcTy = cast<FunctionType>(cast<PointerType>(t)->getElementType());
  // collect all possible function with same function signature
  std::vector<Function *> indirect_call_candidates = collectIndirectCallCandidates(funcTy, *(CI->getFunction()));
  if (indirect_call_candidates.size() == 0)
  {
    errs() << "cannot find possible indirect call candidates.. " << *CI << "\n";
    return false;
  }
  CallWrapper *callW = new CallWrapper(CI, indirect_call_candidates);
  pdgUtils.getCallMap()[CI] = callW;

  // build formal tree for all candidiates.
  for (Function *indirect_called_func : indirect_call_candidates)
  {
    if (indirect_called_func->isDeclaration())
      continue;
    if (indirect_called_func->arg_empty())
      continue;
    if (pdgUtils.getFuncMap()[indirect_called_func]->hasTrees())
      continue;
    buildPDGForFunc(indirect_called_func);
  }
  buildActualParameterTrees(CI);
  // connect actual tree with all possible candidaites.
  if (connectAllPossibleFunctions(CI, indirect_call_candidates))
  {
    instW->setVisited(true);
  }
  return true;
}

bool pdg::ProgramDependencyGraph::processCallInst(InstructionWrapper *instW)
{
  auto &pdgUtils = PDGUtils::getInstance();
  llvm::Instruction *inst = instW->getInstruction();
  if (inst != nullptr && isa<CallInst>(inst) && !instW->getVisited())
  {
    CallInst *CI = dyn_cast<CallInst>(inst);
    Function *callee = CI->getCalledFunction();

    // inline asm
    if (CI->isInlineAsm())
      return false;

    if (isIndirectCallOrInlineAsm(CI))
      return processIndirectCallInst(CI, instW); // indirect function call get func type for indirect call inst

    if (Function *f = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts())) // handle case for bitcast
      callee = f;

    // handle intrinsic functions
    if (callee->isIntrinsic())
      return false;

    // special cases done, common function
    CallWrapper *callW = new CallWrapper(CI);
    pdgUtils.getCallMap()[CI] = callW;
    if (callee->isDeclaration() || callee->empty())
      return false;
    if (!callee->arg_empty())
    {
      if (!pdgUtils.getFuncMap()[callee]->hasTrees())
        buildPDGForFunc(callee);
      buildActualParameterTrees(CI);
    } // end if !callee
    connectCallerAndCallee(instW, callee);
  }
  return true;
}

void pdg::ProgramDependencyGraph::addNodeDependencies(InstructionWrapper *instW, DataDependencyGraph* ddg)
{
  auto &pdgUtils = PDGUtils::getInstance();
  // processing Global instruction
  if (instW->getInstruction() != nullptr)
  {
    if (LoadInst *LDInst = dyn_cast<LoadInst>(instW->getInstruction()))
    {
      for (auto GlobalInstW : pdgUtils.getGlobalInstsSet())
      {
        // iterate users of the global value
        for (User *U : GlobalInstW->getValue()->users())
        {
          if (Instruction *userInst = dyn_cast<Instruction>(U))
          {
            InstructionWrapper *userInstW = pdgUtils.getInstMap()[userInst];
            PDG->addDependency(GlobalInstW, userInstW, DependencyType::GLOBAL_DEP);
          }
        }
      }
    }
  }

  // copy data dependency
  auto dataDList = ddg->getNodeDepList(instW->getInstruction());
  for (auto dependencyPair : dataDList)
  {
    InstructionWrapper *DNodeW2 = const_cast<InstructionWrapper *>(dependencyPair.first->getData());
    PDG->addDependency(instW, DNodeW2, dependencyPair.second);
  }

  // copy control dependency
  if (instW->getGraphNodeType() == GraphNodeType::ENTRY)
  {
    Function *parentFunc = instW->getFunction();
    for (InstructionWrapper *instW2 : pdgUtils.getFuncInstWMap()[parentFunc])
    {
      PDG->addDependency(instW, instW2, DependencyType::CONTROL);
    }
  }
}

bool pdg::ProgramDependencyGraph::hasRecursiveType(ArgumentWrapper *argW, tree<InstructionWrapper *>::iterator insert_loc)
{
  TreeType treeTy = TreeType::FORMAL_IN_TREE;
  int height = argW->getTree(treeTy).depth(insert_loc);
  if (height != 0)
  {
    bool recursion_flag = false;
    tree<InstructionWrapper *>::iterator backTreeIt = insert_loc;
    while (height > 0)
    {
      backTreeIt = argW->getTree(treeTy).parent(backTreeIt);
      if ((*insert_loc)->getLLVMType() == (*backTreeIt)->getLLVMType())
      {
        recursion_flag = true;
        break;
      }
      height -= 1;
    }
    // process next type, because this type brings in a recursion
    if (recursion_flag)
      return true;
  }
  return false;
}

bool pdg::ProgramDependencyGraph::isFilePtrOrFuncTy(Type *ty)
{
  //if field is a function Ptr
  if (ty->isFunctionTy())
  {
    std::string Str;
    raw_string_ostream OS(Str);
    OS << ty;
    return true;
  }

  if (ty->isPointerTy())
  {
    Type *childEleTy = dyn_cast<PointerType>(ty)->getElementType();
    if (childEleTy->isStructTy())
    {
      std::string Str;
      raw_string_ostream OS(Str);
      OS << ty;
      //FILE*, bypass, no need to buildTypeTree
      if ("%struct._IO_FILE*" == OS.str() || "%struct._IO_marker*" == OS.str())
        return true;
    }
  }
  return false;
}

std::vector<Instruction *> pdg::ProgramDependencyGraph::getArgStoreInsts(Argument &arg)
{
  auto &pdgUtils = PDGUtils::getInstance();
  std::vector<Instruction *> initialStoreInsts;
  if (arg.getArgNo() == RETVALARGNO)
  {
    auto &pdgUtils = PDGUtils::getInstance();
    Function *func = arg.getParent();
    for (auto st : pdgUtils.getFuncMap()[func]->getStoreInstList())
    {
      // use type matching approach for return value
      if (st->getValueOperand()->getType() == arg.getType())
        initialStoreInsts.push_back(st);
    }
    return initialStoreInsts;
  }

  for (auto UI = arg.user_begin(); UI != arg.user_end(); ++UI)
  {
    if (auto st = dyn_cast<StoreInst>(*UI))
    {
      if (st->getValueOperand() == &arg)
        initialStoreInsts.push_back(st);
    }

    if (isa<CastInst>(*UI))
    {
      for (auto CIU = (*UI)->user_begin(); CIU != (*UI)->user_end(); ++CIU)
      {
        if (auto cist = dyn_cast<StoreInst>(*CIU))
        {
          if (cist->getValueOperand() == *UI)
          {
            initialStoreInsts.push_back(cist);
          }
        }
      }
    }
  }

  return initialStoreInsts;
}

Instruction *pdg::ProgramDependencyGraph::getArgAllocaInst(Argument &arg)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto f = arg.getParent();
  if (funcMap.find(f) == funcMap.end())
    return nullptr;
  
  for (auto dbgInst : funcMap[f]->getDbgInstList())
  {
    DILocalVariable *DLV = nullptr;
    if (auto declareInst = dyn_cast<DbgDeclareInst>(dbgInst))
      DLV = declareInst->getVariable();
    if (auto valueInst = dyn_cast<DbgValueInst>(dbgInst))
      DLV = valueInst->getVariable();
    if (!DLV)
      continue;
    if (DLV->isParameter() && DLV->getScope()->getSubprogram() == arg.getParent()->getSubprogram() && DLV->getArg() == arg.getArgNo() + 1)
    {
      Instruction *allocaInst = dyn_cast<Instruction>(dbgInst->getVariableLocation());
      return allocaInst;
    }
  }
  // errs() << arg.getParent()->getName() << " " << arg.getArgNo() << "\n";
  return nullptr;
  assert(false && "no viable alloca inst for argument.");
}

bool pdg::ProgramDependencyGraph::nameMatch(std::string str1, std::string str2)
{
  std::string deli = ".";
  unsigned str1FirstDeliPos = str1.find(deli);
  unsigned str1SecondDeliPos = str1.find(deli);
  unsigned str2FirstDeliPos = str2.find(deli);
  unsigned str2SecondDeliPos = str2.find(deli);
  str1 = str1.substr(str1FirstDeliPos, str1SecondDeliPos);
  str2 = str2.substr(str2FirstDeliPos, str2SecondDeliPos);
  return str1 == str2;
}

bool pdg::ProgramDependencyGraph::isFuncTypeMatch(FunctionType *funcTy1, FunctionType *funcTy2)
{
  if (funcTy1->getNumParams() != funcTy2->getNumParams())
    return false;

  auto func1RetType = funcTy1->getReturnType();
  auto func2RetType = funcTy2->getReturnType();

  if (func1RetType != func2RetType)
    return false;

  for (unsigned i = 0; i < funcTy1->getNumParams(); ++i)
  {
    if (funcTy1->getParamType(i) != funcTy2->getParamType(i))
    {
      if (isStructPointer(funcTy1->getParamType(i)) && isStructPointer(funcTy2->getParamType(i)))
      {
        std::string func1ParamName = funcTy1->getParamType(i)->getPointerElementType()->getStructName();
        std::string func2ParamName = funcTy2->getParamType(i)->getPointerElementType()->getStructName();
        if (nameMatch(func1ParamName, func2ParamName))
          continue;
      }
      return false;
    }
  }

  return true;
}

bool pdg::ProgramDependencyGraph::isIndirectCallOrInlineAsm(CallInst *CI)
{
  const Value *V = CI->getCalledValue();
  if (isa<Function>(V) || isa<Constant>(V))
    return false;
  if (CI->isInlineAsm())
    return true;
  return true;
}

tree<InstructionWrapper *>::iterator pdg::ProgramDependencyGraph::getTreeNodeInsertLoc(tree<InstructionWrapper *> &objectTree, InstructionWrapper *treeW)
{
  tree<InstructionWrapper *>::iterator insertLoc = objectTree.begin();
  while ((*insertLoc) != treeW && insertLoc != objectTree.end())
  {
    insertLoc++;
  }
  return insertLoc;
}

tree<InstructionWrapper *>::iterator pdg::ProgramDependencyGraph::getInstInsertLoc(pdg::ArgumentWrapper *argW, InstructionWrapper *tyW, TreeType treeTy)
{
  tree<InstructionWrapper *>::iterator insert_loc = argW->getTree(treeTy).begin();
  while ((*insert_loc) != tyW && insert_loc != argW->getTree(treeTy).end())
  {
    insert_loc++;
  }
  return insert_loc;
}

typename pdg::DependencyNode<pdg::InstructionWrapper>::DependencyLinkList pdg::ProgramDependencyGraph::getNodeDepList(Instruction *inst)
{
  return PDG->getNodeDepList(PDGUtils::getInstance().getInstMap()[inst]);
}

typename DependencyNode<InstructionWrapper>::DependencyLinkList pdg::ProgramDependencyGraph::getNodesWithDepType(const InstructionWrapper *instW, DependencyType depType)
{
  assert(instW != nullptr);
  auto node = PDG->getNodeByData(instW);
  return node->getNodesWithDepType(depType);
}

std::set<InstructionWrapper *> pdg::ProgramDependencyGraph::getDepInstWrapperWithDepType(const InstructionWrapper *inst_w, DependencyType dep_type)
{
  std::set<InstructionWrapper *> dep_instws;
  auto dep_instw_pairs = getNodesWithDepType(inst_w, dep_type);
  for (auto dep_instw_pair : dep_instw_pairs)
  {
    dep_instws.insert(const_cast<InstructionWrapper *>(dep_instw_pair.first->getData()));
  }
  return dep_instws;
}
void pdg::ProgramDependencyGraph::getDepInstsWithDepType(Instruction *source_inst, DependencyType target_dep_type, std::set<Instruction *> &dep_insts)
{
  auto node_dep_list = getNodeDepList(source_inst);
  for (auto dep_pair : node_dep_list)
  {
    auto inst_w = const_cast<InstructionWrapper*>(dep_pair.first->getData());
    auto dep_inst = inst_w->getInstruction();
    if (!dep_inst)
      continue;
    auto dep_type = dep_pair.second;
    if (dep_type == target_dep_type)
      dep_insts.insert(dep_inst);
  }
}

// --------------------------------------
// --------------------------------------
// Build tree functions
// --------------------------------------
// --------------------------------------

void pdg::ProgramDependencyGraph::buildFormalTreeForFunc(Function *Func)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto FuncW = pdgUtils.getFuncMap()[Func];
  bool is_black_list_func = pdgUtils.IsBlackListFunc(Func->getName().str());
  if (is_black_list_func)
    return;
  for (auto argW : FuncW->getArgWList())
  {
    // build formal in tree first
    buildFormalTreeForArg(*argW->getArg(), TreeType::FORMAL_IN_TREE);
    // then, copy formal in tree content to formal out tree
    // argW->copyTree(argW->getTree(TreeType::FORMAL_IN_TREE), TreeType::FORMAL_OUT_TREE);
  }
  buildFormalTreeForArg(*FuncW->getRetW()->getArg(), TreeType::FORMAL_IN_TREE);
  drawFormalParameterTree(Func, TreeType::FORMAL_IN_TREE);
  connectFunctionAndFormalTrees(Func);
  pdgUtils.getFuncMap()[Func]->setTreeFlag(true);
  // drawFormalParameterTree(Func, TreeType::FORMAL_OUT_TREE);
  // connectCallerAndActualTrees(Func);
}

void pdg::ProgramDependencyGraph::buildFormalTreeForArg(Argument &arg, TreeType treeTy)
{
  auto &pdgUtils = PDGUtils::getInstance();
  Function *func = arg.getParent();
  try
  {
    DIType *argDIType = DIUtils::getArgDIType(arg);
    if (!argDIType)
      return;
    auto &ksplit_stats_collector = KSplitStatsCollector::getInstance();
    if (DIUtils::isVoidPointer(argDIType))
    {
      if (cross_domain_funcs_.find(func) != cross_domain_funcs_.end())
        ksplit_stats_collector.IncreaseNumberOfVoidPointer();
      DIType *void_ptr_cast_type = nullptr;
      // if a void pointer is return, we need to find the type it is casted from
      if (pdgUtils.isReturnValue(arg))
        void_ptr_cast_type = FindCastFromDIType(arg);
      else
      {
        std::set<Function *> seen_funcs;
        void_ptr_cast_type = FindCastToDIType(arg, seen_funcs);
      }
      if (void_ptr_cast_type)
      {
        argDIType = void_ptr_cast_type;
      }
      else
      {
        errs() << "[Warning]: void pointer has zero or multiple casts << " << arg.getParent()->getName() << "\n";
        if (cross_domain_funcs_.find(func) != cross_domain_funcs_.end())
          ksplit_stats_collector.IncreaseNumberOfUnhandledVoidPointer();
      }
    }

    // assert(argDIType != nullptr && "cannot build formal tree due to lack of argument debugging info.");
    InstructionWrapper *treeTyW = new TreeTypeWrapper(arg.getParent(), GraphNodeType::FORMAL_IN, &arg, arg.getType(), nullptr, 0, argDIType);
    pdgUtils.getFuncInstWMap()[func].insert(treeTyW);
    //find the right arg, and set tree root
    ArgumentWrapper *argW = pdgUtils.getFuncMap()[func]->getArgWByArg(arg);
    auto treeRoot = argW->getTree(treeTy).set_head(treeTyW);
    assert(argW->getTree(treeTy).size() != 0 && "parameter tree has size 0 after root build!");

    std::string Str;
    raw_string_ostream OS(Str);
    //FILE*, bypass, no need to buildTypeTree
    if ("%struct._IO_FILE*" == OS.str() || "%struct._IO_marker*" == OS.str())
    {
      errs() << "OS.str() = " << OS.str() << " FILE* appears, stop buildTypeTree\n";
    }
    else if (treeTyW->getLLVMType()->isPointerTy() && treeTyW->getLLVMType()->getContainedType(0)->isFunctionTy())
    {
      errs() << *arg.getParent()->getFunctionType() << " DEBUG 312: in buildFormalTree: function pointer arg = " << *treeTyW->getLLVMType() << "\n";
    }
    else
    {
      if (USEDEBUGINFO)
        buildTypeTreeWithDI(arg, treeTyW, treeTy, argDIType);
      else
        buildTypeTree(arg, treeTyW, treeTy);
    }
  }
  catch (std::exception &e)
  {
    errs() << e.what() << "\n";
  }
}

DIType *pdg::ProgramDependencyGraph::FindCastFromDIType(Argument& arg)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto func_map = pdgUtils.getFuncMap();
  Function* func = arg.getParent();
  if (func_map.find(func) == func_map.end())
    return nullptr;
  FunctionWrapper* func_w = func_map[func];
  ArgumentWrapper* arg_w = func_w->getArgWByArg(arg);
  auto formal_tree_begin_iter = arg_w->tree_begin(TreeType::FORMAL_IN_TREE);
  auto val_dep_pairs = getNodesWithDepType(*formal_tree_begin_iter, DependencyType::VAL_DEP);
  for (auto val_dep_pair : val_dep_pairs)
  {
    auto dep_inst_w = val_dep_pair.first->getData();
    Instruction* dep_inst = dep_inst_w->getInstruction();
    if (dep_inst == nullptr)
      return nullptr;
    if (BitCastInst *bci = dyn_cast<BitCastInst>(dep_inst))
    {
      Value* casted_from_value = bci->getOperand(0);
      if (LoadInst *load_i = dyn_cast<LoadInst>(casted_from_value))
      {
        Value* load_addr = load_i->getPointerOperand();
        if (AllocaInst *alloc_i = dyn_cast<AllocaInst>(load_addr))
          return pdgUtils.getInstDIType(alloc_i);
      }
    }
  }
  return nullptr;
}

DIType *pdg::ProgramDependencyGraph::FindCastToDIType(Argument &arg, std::set<Function *> &seen_funcs)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto func_map = pdgUtils.getFuncMap();
  Function* called_func = arg.getParent();
  if (seen_funcs.find(called_func) != seen_funcs.end())
    return nullptr;
  seen_funcs.insert(called_func);
  auto caller_func_w = func_map[arg.getParent()];
  std::set<Value*> load_insts_on_arg_alloc;
  auto arg_alloc_inst = getArgAllocaInst(arg);
  if (!arg_alloc_inst)
    return nullptr;
  for (auto user : arg_alloc_inst->users())
  {
    if (LoadInst *li = dyn_cast<LoadInst>(user))
    {
      if (li->getPointerOperand() == arg_alloc_inst)
        load_insts_on_arg_alloc.insert(li);
    }
  }

  int num_of_cast_inst = 0;
  BitCastInst* cast_inst = nullptr;
  for (auto load_inst_on_arg_inst : load_insts_on_arg_alloc)
  {
    for (auto user : load_inst_on_arg_inst->users())
    {
      if (BitCastInst *cast_i = dyn_cast<BitCastInst>(user))
      {
        if (cast_i->getOperand(0) == load_inst_on_arg_inst)
        {
          num_of_cast_inst++;
          cast_inst = cast_i;
        }
      }
      if (CallInst *ci = dyn_cast<CallInst>(user))
      {
        CallSite CS(ci);
        if (!CS.isCall() || CS.isIndirectCall())
          continue;
        if (Function *called_func = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts()))
        {
          if (called_func->isDeclaration())
            continue;
          unsigned arg_idx = 0;
          auto callee_func_w = func_map[called_func];
          for (auto arg_iter = CS.arg_begin(); arg_iter != CS.arg_end(); ++arg_iter)
          {
            if (*arg_iter == load_inst_on_arg_inst)
              break;
            arg_idx++;
          }
          
          if (arg_idx >= CS.arg_size())
            continue;
          auto callee_arg_w = callee_func_w->getArgWByIdx(arg_idx);
          if (callee_arg_w == nullptr)
            continue;
          Argument& arg_in_callee = *(callee_arg_w->getArg());
          DIType* casted_type = FindCastToDIType(arg_in_callee, seen_funcs);
          return casted_type;
        }
      }
    }
  }
  
  if (num_of_cast_inst != 1)
    return nullptr;

  for (auto user : cast_inst->users())
  {
    if (StoreInst *si = dyn_cast<StoreInst>(user))
    {
      auto casted_val = si->getPointerOperand();
      if (AllocaInst *ci = dyn_cast<AllocaInst>(casted_val))
        return DIUtils::getInstDIType(ci, caller_func_w->getDbgInstList());
    }
  }

  return nullptr;
}

InstructionWrapper *pdg::ProgramDependencyGraph::buildPointerTypeNode(ArgumentWrapper *argW, InstructionWrapper *curTyNode, tree<InstructionWrapper *>::iterator insert_loc)
{
  auto &pdgUtils = PDGUtils::getInstance();
  TreeType treeTy = TreeType::FORMAL_IN_TREE;
  Argument &arg = *argW->getArg();
  PointerType *pt = dyn_cast<PointerType>(curTyNode->getLLVMType());
  Type *pointedNodeTy = pt->getElementType();
  InstructionWrapper *pointedTypeW = new TreeTypeWrapper(arg.getParent(),
                                                         GraphNodeType::PARAMETER_FIELD,
                                                         &arg,
                                                         pointedNodeTy,
                                                         curTyNode->getLLVMType(),
                                                         0);
  pdgUtils.getFuncInstWMap()[arg.getParent()].insert(pointedTypeW);
  argW->getTree(treeTy).append_child(insert_loc, pointedTypeW);
  return pointedTypeW;
}

InstructionWrapper *pdg::ProgramDependencyGraph::buildPointerTypeNodeWithDI(ArgumentWrapper *argW, InstructionWrapper *curTyNode, tree<InstructionWrapper *>::iterator insert_loc, DIType *dt)
{
  auto &pdgUtils = PDGUtils::getInstance();
  TreeType treeTy = TreeType::FORMAL_IN_TREE;
  try
  {
    Argument &arg = *argW->getArg();
    InstructionWrapper *pointedTypeW = new TreeTypeWrapper(arg.getParent(),
                                                           GraphNodeType::PARAMETER_FIELD,
                                                           &arg,
                                                           nullptr,
                                                           nullptr,
                                                           0,
                                                           DIUtils::getBaseDIType(dt));
    pdgUtils.getFuncInstWMap()[arg.getParent()].insert(pointedTypeW);
    argW->getTree(treeTy).append_child(insert_loc, pointedTypeW);
    return pointedTypeW;
  }
  catch (std::exception &e)
  {
    errs() << e.what() << "\n";
    exit(0);
  }
}

void pdg::ProgramDependencyGraph::buildTypeTree(Argument &arg, InstructionWrapper *treeTyW, TreeType treeTy)
{
  auto &pdgUtils = PDGUtils::getInstance();
  Function *Func = arg.getParent();
  // setup instWQ to avoid recusion processing
  std::queue<InstructionWrapper *> instWQ;
  instWQ.push(treeTyW);
  ArgumentWrapper *argW = pdgUtils.getFuncMap()[Func]->getArgWByArg(arg);

  if (argW == nullptr)
    throw new ArgWrapperIsNullPtr("Argument Wrapper is nullptr");

  tree<InstructionWrapper *>::iterator insert_loc; // insert location in the parameter tree for the type wrapper
  int depth = 0;
  while (!instWQ.empty())
  {
    if (depth >= EXPAND_LEVEL)
      return;
    depth += 1;
    int qSize = instWQ.size();
    while (qSize > 0)
    {
      qSize -= 1;
      InstructionWrapper *curTyNode = instWQ.front();
      instWQ.pop();
      insert_loc = getInstInsertLoc(argW, curTyNode, treeTy);
      // handle recursion type using 1-limit approach
      // track back from child to parent, if find same type, stop building.
      // The type used here is form llvm type system.
      // if (hasRecursiveType(argW, insert_loc))
      //   continue;
      // if is pointer type, create node for the pointed type
      Type *curNodeTy = curTyNode->getLLVMType();
      if (curNodeTy->isPointerTy())
      {
        InstructionWrapper *pointedTypeW = buildPointerTypeNode(argW, curTyNode, insert_loc);
        instWQ.push(pointedTypeW); // put the pointed node to queue
        continue;
      }
      // compose for struct
      if (!curNodeTy->isStructTy())
        continue;
      // for struct type, insert all children to the tree
      for (unsigned int child_offset = 0; child_offset < curNodeTy->getNumContainedTypes(); child_offset++)
      {
        Type *parentType = curTyNode->getLLVMType();
        // field sensitive processing. Get correspond gep and link tree node with gep.
        Type *childType = curNodeTy->getContainedType(child_offset);
        InstructionWrapper *typeFieldW = new TreeTypeWrapper(arg.getParent(), GraphNodeType::PARAMETER_FIELD, &arg, childType, parentType, child_offset);
        // link gep with tree node
        pdgUtils.getFuncInstWMap()[arg.getParent()].insert(typeFieldW);
        // start inserting formal tree instructions
        argW->getTree(treeTy).append_child(insert_loc, typeFieldW);
        //skip function ptr, FILE*
        if (isFilePtrOrFuncTy(childType))
          continue;
        instWQ.push(typeFieldW);
      }
    }
  }
}

void pdg::ProgramDependencyGraph::buildTypeTreeWithDI(Argument &arg, InstructionWrapper *treeTyW, TreeType treeTy, DIType *argDIType)
{
  auto &pdgUtils = PDGUtils::getInstance();
  Function *Func = arg.getParent();
  std::queue<InstructionWrapper *> instWQ;
  std::queue<DIType *> DITypeQ;
  instWQ.push(treeTyW);
  DITypeQ.push(argDIType);

  ArgumentWrapper *argW = pdgUtils.getFuncMap()[Func]->getArgWByArg(arg);
  if (argW == nullptr)
    throw new ArgWrapperIsNullPtr("Argument Wrapper is nullptr");
  auto &formalInTree = argW->getTree(TreeType::FORMAL_IN_TREE);
  int depth = 0;
  while (!instWQ.empty())
  {
    if (depth >= EXPAND_LEVEL)
      return;
    depth += 1;

    int qSize = instWQ.size();
    while (qSize > 0)
    {
      qSize -= 1;
      InstructionWrapper *curTyNode = instWQ.front();
      DIType *nodeDIType = DITypeQ.front();
      instWQ.pop();
      DITypeQ.pop();

      if (!nodeDIType)
        continue;

      // process pointer type node.
      if (DIUtils::isPointerType(nodeDIType))
      {
        // extract the pointed value, push it to inst queue for further process.
        InstructionWrapper *pointedTypeW = buildPointerTreeNodeWithDI(arg, *curTyNode, formalInTree, *nodeDIType);
        instWQ.push(pointedTypeW);
        try
        {
          DITypeQ.push(DIUtils::getBaseDIType(nodeDIType));
        }
        catch (std::exception &e)
        {
          errs() << e.what() << "\n";
          exit(0);
        }
        continue;
      }

      // stop bulding if not a struct type
      if (!DIUtils::isStructTy(nodeDIType) && !DIUtils::isUnionTy(nodeDIType))
        continue;
      // get structure fields based on debugging information
      nodeDIType = DIUtils::getLowestDIType(nodeDIType);
      auto DINodeArr = dyn_cast<DICompositeType>(nodeDIType)->getElements();
      for (unsigned i = 0; i < DINodeArr.size(); ++i)
      {
        DIType *fieldDIType = dyn_cast<DIType>(DINodeArr[i]);
        InstructionWrapper *fieldNodeW = new TreeTypeWrapper(Func, GraphNodeType::PARAMETER_FIELD, &arg, nullptr, nullptr, i, fieldDIType);
        pdgUtils.getFuncInstWMap()[Func].insert(fieldNodeW);
        tree<InstructionWrapper *>::iterator insertLoc = getTreeNodeInsertLoc(formalInTree, curTyNode);
        argW->getTree(treeTy).append_child(insertLoc, fieldNodeW);
        instWQ.push(fieldNodeW);
        DITypeQ.push(DIUtils::getBaseDIType(fieldDIType));
      }
    }
  }
}

InstructionWrapper *pdg::ProgramDependencyGraph::buildPointerTreeNodeWithDI(Value &val, InstructionWrapper &parentTreeNodeW, tree<InstructionWrapper *> &objectTree, DIType &curDIType)
{
  tree<InstructionWrapper*>::iterator insertLoc = getTreeNodeInsertLoc(objectTree, &parentTreeNodeW);
  auto &pdgUtils = PDGUtils::getInstance();
  InstructionWrapper *pointedTreeW = nullptr;
  try
  {
    if (Argument *arg = dyn_cast<Argument>(&val))
    {
      pointedTreeW = new TreeTypeWrapper(arg->getParent(),
                                         GraphNodeType::PARAMETER_FIELD,
                                         arg,
                                         nullptr,
                                         nullptr,
                                         0,
                                         DIUtils::getBaseDIType(&curDIType));
      auto funcInstWMap = pdgUtils.getFuncInstWMap();
      funcInstWMap[arg->getParent()].insert(pointedTreeW);
    }
    else
    {
      // build for global variables
      pointedTreeW = new TreeTypeWrapper(&val,
                                         GraphNodeType::PARAMETER_FIELD,
                                         0,
                                         DIUtils::getBaseDIType(&curDIType));
    }

    objectTree.append_child(insertLoc, pointedTreeW);
    return pointedTreeW;
  }
  catch (std::exception &e)
  {
    errs() << e.what() << "\n";
    exit(0);
  }
}

// TODO: we should also consider instructions that are not of alloca types.
std::set<InstructionWrapper *> pdg::ProgramDependencyGraph::collectInstWsOnDIType(DIType *dt, std::set<Function*> &searchDomain)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto instMap = pdgUtils.getInstMap();
  auto instDITypeMap = pdgUtils.getInstDITypeMap();
  std::set<InstructionWrapper *> ret;
  // iterate through functions, find all function has an argument of this ditype parameter.
  for (Function &F : *module)
  {
    if (F.isDeclaration() || F.empty())
      continue;
    if (searchDomain.find(&F) == searchDomain.end())
      continue;

    std::string DITypeName = DIUtils::getDITypeName(dt);
    for (auto instI = inst_begin(F); instI != inst_end(F); ++instI)
    {
      Instruction* i = &*instI;
      if (instDITypeMap.find(i) == instDITypeMap.end())
        continue;
      std::string instDITypeName = DIUtils::getDITypeName(instDITypeMap[i]);
      if (instDITypeName == DITypeName)
      {
        errs() << "find di type name: " << instDITypeName << "\n";
        ret.insert(instMap[i]);
      }
      // here, gep always produce pointer variable. But the debugging info is misaligning here
      if (DITypeName.back() == '*')
      {
        DITypeName.pop_back();
        if (instDITypeName == DITypeName && isa<GetElementPtrInst>(i))
        {
          errs() << "insert gep di type: " << instDITypeName << "\n";
          ret.insert(instMap[i]);
        }
      }
    }
    // auto dbgInstList = funcMap[&F]->getDbgInstList(); 
    // for (auto instI = inst_begin(&F); instI != inst_end(&F); ++instI)
    // {
    //   DIType *instDIType = DIUtils::getInstDIType(&*instI, dbgInstList);
    //   if (!instDIType)
    //     continue;
    //   if (DIUtils::getDITypeName(instDIType) == DIUtils::getDITypeName(dt)) // getting the alloca and intra alias
    //   {
    //     std::set<InstructionWrapper*> aliasSet;
    //     getAllAlias(&*instI, aliasSet);
    //     for (auto aliasW : aliasSet)
    //     {
    //       if (aliasW->getInstruction() != nullptr)
    //         ret.insert(instMap[aliasW->getInstruction()]);
    //     }
    //   } 
    // }
  }

  return ret;
}


void pdg::ProgramDependencyGraph::connectGlobalObjectTreeWithAddressVars(std::set<Function*> &searchDomain)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  auto funcMap = pdgUtils.getFuncMap();
  for (auto pair : globalObjectTrees)
  {
    auto globalVar = pair.first;
    auto objectTree = pair.second;
    // link with all local alloca of the struct type that is not handeled by global var or cross-domain parameter
    DIType* globalVarDIType = DIUtils::getGlobalVarDIType(*globalVar);
    auto treeBegin = objectTree.begin();
    treeBegin++;
    if(treeBegin == objectTree.end())
      return;
    for (auto user : globalVar->users())
    {
      if (Instruction *inst = dyn_cast<Instruction>(user))
      {
        auto instW = instMap[inst];
        PDG->addDependency(*treeBegin, instW, DependencyType::VAL_DEP);
        PDG->addDependency(instW, *treeBegin, DependencyType::VAL_DEP);
        Function *userFunc = instW->getInstruction()->getFunction();
        if (!funcMap[userFunc]->hasTrees())
        {
          buildFormalTreeForFunc(userFunc);
        }
      }
    }

    // std::set<Instruction*> seenInsts;
    for (tree<InstructionWrapper *>::iterator treeI = objectTree.begin(); treeI != objectTree.end(); ++treeI)
    {
      if (tree<InstructionWrapper *>::depth(treeI) <= 1)
        continue;
      // for tree nodes that are not root, get parent node's dependent instructions and then find loadInst or GEP Inst from parent's address
      auto ParentI = tree<InstructionWrapper *>::parent(treeI);
      auto parentValDepNodes = getNodesWithDepType(*ParentI, DependencyType::VAL_DEP);
      for (auto pair : parentValDepNodes)
      {
        auto parentDepInstW = pair.first->getData();
        // collect all alias instructions for each parent' dependent instruction
        std::set<InstructionWrapper*> parentDepInstAliasList;
        getAllAlias(parentDepInstW->getInstruction(), parentDepInstAliasList);
        parentDepInstAliasList.insert(const_cast<InstructionWrapper *>(parentDepInstW));
        for (auto depInstAlias : parentDepInstAliasList)
        {
          if (depInstAlias->getInstruction() == nullptr)
            continue;

          std::set<InstructionWrapper*> readInstWs;
          getReadInstsOnInst(depInstAlias->getInstruction(), readInstWs);
          for (auto readInstW : readInstWs)
          {
            auto readInst = readInstW->getInstruction();
           // if (seenInsts.find(readInst) != seenInsts.end())
            //   continue;
            // seenInsts.insert(readInst);
            if (isa<LoadInst>(readInst))
            {
              PDG->addDependency(*treeI, readInstW, DependencyType::VAL_DEP);
              PDG->addDependency(readInstW, *treeI, DependencyType::VAL_DEP);
            }
            // for GEP, checks the offset acutally match
            else if (isa<GetElementPtrInst>(readInst))
            {
              StructType *structTy = getStructTypeFromGEP(readInst);
              if (structTy != nullptr)
              {
                if (isTreeNodeGEPMatch(structTy, *treeI, readInst))
                {
                  PDG->addDependency(*treeI, readInstW, DependencyType::VAL_DEP);
                  PDG->addDependency(readInstW, *treeI, DependencyType::VAL_DEP);
                }
              }
            }
          }
        }
      }
    }
  }
}

void pdg::ProgramDependencyGraph::collectSharedGlobalVars(std::set<Function *> &driverDomainFuncs, std::set<Function *> &kernelDomainFuncs)
{
  for (auto globalIter = module->global_begin(); globalIter != module->global_end(); ++globalIter)
  {
    if (auto globalVar = dyn_cast<GlobalVariable>(&*globalIter))
    {
      auto globalVarDIType = DIUtils::getGlobalVarDIType(*globalIter);
      if (DIUtils::isStructPointerTy(globalVarDIType) || DIUtils::isStructTy(globalVarDIType))
      {
        sharedGlobalVars.insert(&*globalIter);
      }
      // bool accessedInDriver = false;
      // bool accessedInKernel = false;
      // for (auto user : globalVar->users())
      // {
      //   if (Instruction *inst = dyn_cast<Instruction>(user))
      //   {
      //     Function *f = inst->getFunction();
      //     if (driverDomainFuncs.find(f) != driverDomainFuncs.end())
      //       accessedInDriver = true;
      //     if (kernelDomainFuncs.find(f) != kernelDomainFuncs.end())
      //       accessedInKernel = true;
      //     if (accessedInDriver && accessedInKernel)
      //     {
      //       sharedGlobalVars.insert(&*globalIter);
      //       break;
      //     }
      //   }
      // }
    }
  }
}

void pdg::ProgramDependencyGraph::buildObjectTreeForGlobalVars()
{
  // 1. iterate through global vars
  for (auto sharedGlobalVar : sharedGlobalVars)
  {
    // 2. check if a global var is of struct pointer type
    DIType *globalVarDIType = DIUtils::getGlobalVarDIType(*sharedGlobalVar);
    if (!globalVarDIType)
      continue;
    // build object tree for struct pointer global
    if (DIUtils::isStructPointerTy(globalVarDIType))
      buildObjectTreeForGlobalVar(*sharedGlobalVar, *globalVarDIType);
  }
}

void pdg::ProgramDependencyGraph::buildGlobalTypeTrees(std::set<DIType*> sharedTypes)
{
  for (DIType *dt : sharedTypes)
  {
    buildGlobalTypeTreeForDIType(*dt);
    std::set<InstructionWrapper*> s;
    shared_data_name_and_instw_map_.insert(std::make_pair(DIUtils::getRawDITypeName(dt), s));
  }
}

void pdg::ProgramDependencyGraph::buildGlobalTypeTreeForDIType(DIType &DI)
{
  tree<InstructionWrapper *> typeTree;
  InstructionWrapper *treeHead = new TreeTypeWrapper(GraphNodeType::GLOBAL_VALUE, 0, &DI);
  typeTree.set_head(treeHead);
  auto &pdgUtils = PDGUtils::getInstance();
  std::queue<InstructionWrapper *> instWQ;
  std::queue<DIType *> DITypeQ;
  instWQ.push(treeHead);
  DITypeQ.push(&DI);

  int depth = 0;
  tree<InstructionWrapper *>::iterator insertLoc;
  while (!instWQ.empty())
  {
    if (depth > EXPAND_LEVEL)
      break;
    depth++;
    int qSize = instWQ.size();
    while (qSize > 0)
    {
      qSize -= 1;
      InstructionWrapper *curTyNode = instWQ.front();
      DIType *nodeDIType = DITypeQ.front();
      instWQ.pop();
      DITypeQ.pop();

      if (!nodeDIType)
        continue;
      insertLoc = getTreeNodeInsertLoc(typeTree, curTyNode);
      // process pointer type
      if (DIUtils::isPointerType(nodeDIType))
      {
        // extract the pointed value, push it to inst queue for further process.
        InstructionWrapper *pointedTypeW = new TreeTypeWrapper(GraphNodeType::PARAMETER_FIELD, 0, DIUtils::getBaseDIType(nodeDIType));
        typeTree.insert(insertLoc, pointedTypeW);
        instWQ.push(pointedTypeW);
        try
        {
          DITypeQ.push(DIUtils::getBaseDIType(nodeDIType));
        }
        catch (std::exception &e)
        {
          errs() << e.what() << "\n";
          exit(0);
        }
        continue;
      }
      // stop bulding if not a struct field
      if (!DIUtils::isStructTy(nodeDIType))
        continue;
      // get structure fields based on debugging information
      nodeDIType = DIUtils::getLowestDIType(nodeDIType);
      auto DINodeArr = dyn_cast<DICompositeType>(nodeDIType)->getElements();
      for (unsigned i = 0; i < DINodeArr.size(); ++i)
      {
        DIType *fieldDIType = dyn_cast<DIType>(DINodeArr[i]);
        InstructionWrapper *fieldNodeW = new TreeTypeWrapper(GraphNodeType::PARAMETER_FIELD, i, fieldDIType);
        typeTree.append_child(insertLoc, fieldNodeW);
        instWQ.push(fieldNodeW);
        DITypeQ.push(DIUtils::getBaseDIType(fieldDIType));
      }
    }
  }
  globalTypeTrees[&DI] = typeTree;
}

void pdg::ProgramDependencyGraph::buildObjectTreeForGlobalVar(GlobalVariable &GV, DIType &DI)
{
  tree<InstructionWrapper *> objectTree;
  InstructionWrapper *globalW = new TreeTypeWrapper(&GV, GraphNodeType::GLOBAL_VALUE, 0, &DI);
  objectTree.set_head(globalW);
  // find all the uses of the global variable, set the user instructions as the address variable for the global var
  auto &pdgUtils = PDGUtils::getInstance();
  std::queue<InstructionWrapper *> instWQ;
  std::queue<DIType *> DITypeQ;
  instWQ.push(globalW);
  DITypeQ.push(&DI);

  int depth = 0;
  tree<InstructionWrapper*>::iterator insertLoc;
  while (!instWQ.empty())
  {
    if (depth > EXPAND_LEVEL)
      break;
    depth++;
    int qSize = instWQ.size();
    while (qSize > 0)
    {
      qSize -= 1;
      InstructionWrapper *curTyNode = instWQ.front();
      DIType *nodeDIType = DITypeQ.front();
      instWQ.pop();
      DITypeQ.pop();

      if (!nodeDIType)
        continue;
      insertLoc = getTreeNodeInsertLoc(objectTree, curTyNode);
      // process pointer type
      if (DIUtils::isPointerType(nodeDIType))
      {
        // extract the pointed value, push it to inst queue for further process.
        InstructionWrapper *pointedTypeW = buildPointerTreeNodeWithDI(GV, *curTyNode, objectTree, *nodeDIType);
        instWQ.push(pointedTypeW);
        try
        {
          DITypeQ.push(DIUtils::getBaseDIType(nodeDIType));
        }
        catch (std::exception &e)
        {
          errs() << e.what() << "\n";
          exit(0);
        }
        continue;
      }
      // stop bulding if not a struct field
      if (!DIUtils::isStructTy(nodeDIType))
        continue;
      // get structure fields based on debugging information
      nodeDIType = DIUtils::getLowestDIType(nodeDIType);
      auto DINodeArr = dyn_cast<DICompositeType>(nodeDIType)->getElements();
      for (unsigned i = 0; i < DINodeArr.size(); ++i)
      {
        DIType *fieldDIType = dyn_cast<DIType>(DINodeArr[i]);
        InstructionWrapper *fieldNodeW = new TreeTypeWrapper(&GV, GraphNodeType::PARAMETER_FIELD, i, fieldDIType);
        objectTree.append_child(insertLoc, fieldNodeW);
        instWQ.push(fieldNodeW);
        DITypeQ.push(DIUtils::getBaseDIType(fieldDIType));
      }
    }
  }
  globalObjectTrees.insert(std::make_pair(&GV, objectTree));
}

void pdg::ProgramDependencyGraph::connectGlobalTypeTreeWithAddressVars()
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto instMap = pdgUtils.getInstMap();
  auto funcMap = pdgUtils.getFuncMap();

  for (auto pair : globalTypeTrees)
  {
    DIType* shared_di_type = pair.first;
    auto typeTree = pair.second;
    // link with all local variable of the struct type that is not handeled by global var or cross-domain parameter
    std::string inst_di_type_name = DIUtils::getRawDITypeName(shared_di_type);
    auto insts_w_with_shared_data_type = shared_data_name_and_instw_map_[inst_di_type_name];
    auto treeBegin = typeTree.begin();
    for (auto inst_w : insts_w_with_shared_data_type)
    {
      Function* allocFunc = inst_w->getInstruction()->getFunction();
      std::set<InstructionWrapper*> alias_set = getDepInstWrapperWithDepType(inst_w, DependencyType::DATA_ALIAS);
      alias_set.insert(inst_w);
      for (auto alias_w : alias_set)
      {
        PDG->addDependency(*treeBegin, alias_w, DependencyType::VAL_DEP);
      }
      if (!funcMap[allocFunc]->hasTrees())
        buildFormalTreeForFunc(allocFunc);
    }
    
    for (tree<InstructionWrapper *>::iterator treeI = typeTree.begin(); treeI != typeTree.end(); ++treeI)
    {
      if (tree<InstructionWrapper *>::depth(treeI) == 0)
        continue;
      // for tree nodes that are not root, get parent node's dependent instructions and then find loadInst or GEP Inst from parent's address
      auto parent_iter = tree<InstructionWrapper *>::parent(treeI);
      auto parent_val_dep_nodes = getNodesWithDepType(*parent_iter, DependencyType::VAL_DEP);
      for (auto pair : parent_val_dep_nodes)
      {
        auto parent_dep_inst_w = const_cast<InstructionWrapper *>(pair.first->getData());
        std::set<InstructionWrapper *> read_insts_w;
        getReadInstsOnInst(parent_dep_inst_w->getInstruction(), read_insts_w);
        // collect all alias instructions for each parent' dependent instruction
        for (auto read_inst_w : read_insts_w)
        {
          std::set<InstructionWrapper *> alias_set = getDepInstWrapperWithDepType(read_inst_w, DependencyType::DATA_ALIAS);
          alias_set.insert(read_inst_w);
          Instruction *read_inst = read_inst_w->getInstruction();
          if (isa<LoadInst>(read_inst))
          {
            for (auto alias_inst_w : alias_set)
            {
              PDG->addDependency(*treeI, alias_inst_w, DependencyType::VAL_DEP);
              PDG->addDependency(alias_inst_w, *treeI, DependencyType::VAL_DEP);
            }
          }
          // for GEP, checks the offset acutally match
          else if (isa<GetElementPtrInst>(read_inst))
          {
            StructType *structTy = getStructTypeFromGEP(read_inst);
            if (structTy != nullptr)
            {
              if (isTreeNodeGEPMatch(structTy, *treeI, read_inst))
              {
                for (auto alias_inst_w : alias_set)
                {
                  PDG->addDependency(*treeI, alias_inst_w, DependencyType::VAL_DEP);
                  PDG->addDependency(alias_inst_w, *treeI, DependencyType::VAL_DEP);
                }
              }
            }
          }
        }
      }
    }
  }
}

void pdg::ProgramDependencyGraph::drawFormalParameterTree(Function *Func, TreeType treeTy)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto argWList = funcMap[Func]->getArgWList();
  argWList.push_back(funcMap[Func]->getRetW());
  for (ArgumentWrapper *argW : argWList)
  {
    for (tree<InstructionWrapper *>::iterator
             TI = argW->getTree(treeTy).begin(),
             TE = argW->getTree(treeTy).end();
         TI != TE; ++TI)
    {
      for (unsigned i = 0; i < TI.number_of_children(); i++)
      {
        InstructionWrapper *childW = *argW->getTree(treeTy).child(TI, i);
        PDG->addDependency(*TI, childW, DependencyType::PARAMETER);
      }
    }
  }
}

void pdg::ProgramDependencyGraph::getReadInstsOnInst(Instruction *inst, std::set<InstructionWrapper*> &readInstWs)
{
  auto depList = getNodeDepList(inst);
  for (auto pair : depList)
  {
    if (pair.second == DependencyType::DATA_READ)
      readInstWs.insert(const_cast<InstructionWrapper *>(pair.first->getData()));
  }
}

void pdg::ProgramDependencyGraph::getAllAlias(Instruction *inst, std::set<InstructionWrapper*> &ret)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto instW = pdgUtils.getInstMap()[inst];
  std::set<InstructionWrapper *> seen_nodes;
  std::queue<InstructionWrapper *> workQ;
  workQ.push(const_cast<InstructionWrapper *>(instW));
  seen_nodes.insert(const_cast<InstructionWrapper *>(instW));
  ret.insert(instW);
  while (!workQ.empty())
  {
    auto I = workQ.front();
    workQ.pop();
    if (I->getInstruction() != nullptr)
    {
      auto depList = getNodeDepList(I->getInstruction());
      for (auto pair : depList)
      {
        if (pair.second != DependencyType::DATA_ALIAS)
          continue;

        auto tmpInstW = const_cast<InstructionWrapper *>(pair.first->getData());

        if (!tmpInstW || seen_nodes.find(tmpInstW) != seen_nodes.end())
          continue;

        seen_nodes.insert(tmpInstW);
        workQ.push(tmpInstW);
        ret.insert(tmpInstW);
      }
    }
  }
}

void pdg::ProgramDependencyGraph::connectFunctionAndFormalTrees(Function *callee)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto funcMap = pdgUtils.getFuncMap();
  auto instMap = pdgUtils.getInstMap();
  for (std::vector<ArgumentWrapper *>::iterator argI = funcMap[callee]->getArgWList().begin(),
                                                argE = funcMap[callee]->getArgWList().end();
       argI != argE; ++argI)
  {
    auto formalInTreeBegin = (*argI)->tree_begin(TreeType::FORMAL_IN_TREE);
    auto formalOutTreeBegin = (*argI)->tree_begin(TreeType::FORMAL_OUT_TREE);
    // connect Function's EntryNode with formal in/out tree roots
    PDG->addDependency(pdgUtils.getFuncMap()[callee]->getEntryW(), *formalInTreeBegin, DependencyType::PARAMETER);
    // PDG->addDependency(pdgUtils.getFuncMap()[callee]->getEntryW(), *formalOutTreeBegin, DependencyType::PARAMETER);
    // find store instructions represent values for root. The value operand of
    // sotre instruction is the argument we interested.
    auto argAlloc = getArgAllocaInst(*(*argI)->getArg());

    if (argAlloc == nullptr)
    {
      errs() << "Cannot get arg alloc " << (*argI)->getArg()->getArgNo() << " - " << callee->getName() << "\n";
      return;
    }
    pdgUtils.getInstMap()[argAlloc]->setGraphNodeType(GraphNodeType::ARG_ALLOC);
    // add dependency between parameter tree root node and arg alloc instruction
    std::set<InstructionWrapper*> argAliasSet; 
    getAllAlias(argAlloc, argAliasSet);
    for (auto argAliasW : argAliasSet)
    {
      auto argAlias = argAliasW->getInstruction();
      PDG->addDependency(*formalInTreeBegin, pdgUtils.getInstMap()[argAlias], DependencyType::VAL_DEP);
      PDG->addDependency(pdgUtils.getInstMap()[argAlias], *formalInTreeBegin, DependencyType::VAL_DEP);
    }
    connectTreeNodeWithAddrVars(*argI);
  }

  ArgumentWrapper* retW = funcMap[callee]->getRetW();
  // link the root node with return instructions
  auto retInstList = funcMap[callee]->getReturnInstList();
  auto retTreeBegin = retW->tree_begin(TreeType::FORMAL_IN_TREE);
  for (auto retInst : retInstList)
  {
    if (retTreeBegin != retW->tree_end(TreeType::FORMAL_IN_TREE)) // due to the miss of alloc for return value 
    {
      retTreeBegin++;
      Value* ret_val = retInst->getReturnValue();
      if (!ret_val)
      {
        errs() << "find return null: " << *retInst << " - " << retInst->getFunction()->getName() << "\n";
        continue;
      }
      if (Instruction *ret_val_i = dyn_cast<Instruction>(ret_val))
      {
        std::set<InstructionWrapper *> ret_val_alias_set;
        getAllAlias(ret_val_i, ret_val_alias_set);
        for (auto alias_w : ret_val_alias_set)
        {
          PDG->addDependency(*retTreeBegin, alias_w, DependencyType::VAL_DEP);
        }
      }
    }
  }
  connectTreeNodeWithAddrVars(retW);
}

void pdg::ProgramDependencyGraph::connectTreeNodeWithAddrVars(ArgumentWrapper* argW)
{
  for (tree<InstructionWrapper *>::iterator treeI = argW->tree_begin(TreeType::FORMAL_IN_TREE); treeI != argW->tree_end(TreeType::FORMAL_IN_TREE); ++treeI)
  {
    if (tree<InstructionWrapper *>::depth(treeI) == 0)
      continue;
    // for tree nodes that are not root, get parent node's dependent instructions and then find loadInst or GEP Inst from parent's loads instructions
    auto parent_iter = tree<InstructionWrapper *>::parent(treeI);
    auto parentValDepNodes = getNodesWithDepType(*parent_iter, DependencyType::VAL_DEP);
    // for union, copy parent's addr var to child node. As they have the same address.
    if (DIUtils::isUnionTy((*parent_iter)->getDIType()))
    {
      for (auto pair : parentValDepNodes)
      {
        auto parentDepInstW = pair.first->getData();
        PDG->addDependency(*treeI, parentDepInstW, DependencyType::VAL_DEP);
      }
      continue;
    }

    for (auto pair : parentValDepNodes)
    {
      auto parentDepInstW = const_cast<InstructionWrapper *>(pair.first->getData());
      std::set<InstructionWrapper *> read_insts_w;
      getReadInstsOnInst(parentDepInstW->getInstruction(), read_insts_w);
      for (auto read_inst_w : read_insts_w)
      {
        std::set<InstructionWrapper *> alias_set = getDepInstWrapperWithDepType(read_inst_w, DependencyType::DATA_ALIAS);
        Instruction* read_inst = read_inst_w->getInstruction();
        alias_set.insert(read_inst_w);
        if (isa<LoadInst>(read_inst) && (*treeI)->getNodeOffset() == 0)
        {
          for (auto alias_inst_w : alias_set)
          {
            PDG->addDependency(*treeI, alias_inst_w, DependencyType::VAL_DEP);
            PDG->addDependency(alias_inst_w, *treeI, DependencyType::VAL_DEP);
          }
        }
        // for GEP, checks if the offsets match
        else if (isa<GetElementPtrInst>(read_inst))
        {
          Instruction *GEP = read_inst;
          StructType *structTy = getStructTypeFromGEP(GEP);
          if (structTy != nullptr)
          {
            if (isTreeNodeGEPMatch(structTy, *treeI, GEP))
            {
              for (auto alias_inst_w : alias_set)
              {
                PDG->addDependency(*treeI, alias_inst_w, DependencyType::VAL_DEP);
                PDG->addDependency(alias_inst_w, *treeI, DependencyType::VAL_DEP);
              }
            }
          }
        }
      }
    }
  }
}

bool pdg::ProgramDependencyGraph::connectAllPossibleFunctions(CallInst *CI, std::vector<Function *> indirect_call_candidates)
{
  auto &pdgUtils = PDGUtils::getInstance();
  InstructionWrapper *CInstW = pdgUtils.getInstMap()[CI];
  for (Function *func : indirect_call_candidates)
  {
    if (!connectCallerAndCallee(CInstW, func))
    {
      return false;
    }
  }
  return true;
}

void pdg::ProgramDependencyGraph::connectActualTreeToFormalTree(CallInst *CI, Function *called_func)
{
  auto &pdgUtils = PDGUtils::getInstance();
  // old way, process four trees at the same time, remove soon
  std::vector<ArgumentWrapper *>::iterator formal_argI = pdgUtils.getFuncMap()[called_func]->getArgWList().begin();
  std::vector<ArgumentWrapper *>::iterator formal_argE = pdgUtils.getFuncMap()[called_func]->getArgWList().end();
  std::vector<ArgumentWrapper *>::iterator actual_argI = pdgUtils.getCallMap()[CI]->getArgWList().begin();

  // increase formal/actual tree iterator simutaneously
  for (; formal_argI != formal_argE; ++formal_argI, ++actual_argI)
  {
    // intra-connection between ACTUAL/FORMAL IN/OUT trees
    for (tree<InstructionWrapper *>::iterator
             actual_in_TI = (*actual_argI)->getTree(TreeType::ACTUAL_IN_TREE).begin(),
             actual_in_TE = (*actual_argI)->getTree(TreeType::ACTUAL_IN_TREE).end(),
             formal_in_TI = (*formal_argI)->getTree(TreeType::FORMAL_IN_TREE).begin(), // TI2
         formal_out_TI = (*formal_argI)->getTree(TreeType::FORMAL_OUT_TREE).begin(),   // TI3
         actual_out_TI = (*actual_argI)->getTree(TreeType::ACTUAL_OUT_TREE).begin();   // TI4
         actual_in_TI != actual_in_TE;
         ++actual_in_TI, ++formal_in_TI, ++formal_out_TI, ++actual_out_TI)
    {
      // connect trees: antual-in --> formal-in, formal-out --> actual-out
      PDG->addDependency(*actual_in_TI,
                         *formal_in_TI, DependencyType::PARAMETER);
      PDG->addDependency(*formal_out_TI,
                         *actual_out_TI, DependencyType::PARAMETER);

    } // end for(tree...) intra-connection between actual/formal
  }
}

bool pdg::ProgramDependencyGraph::connectCallerAndCallee(InstructionWrapper *instW, Function *callee)
{
  auto &pdgUtils = PDGUtils::getInstance();
  if (instW == nullptr || callee == nullptr)
    return false;

  // callInst in caller --> Entry Node in callee
  PDG->addDependency(instW, pdgUtils.getFuncMap()[callee]->getEntryW(), DependencyType::CONTROL);
  Function *caller = instW->getInstruction()->getFunction();
  // ReturnInst in callee --> CallInst in caller
  for (Instruction *retInst : pdgUtils.getFuncMap()[callee]->getReturnInstList())
  {
    for (InstructionWrapper *tmpInstW : pdgUtils.getFuncInstWMap()[caller])
    {
      if (retInst == tmpInstW->getInstruction())
      {
        if (dyn_cast<ReturnInst>(tmpInstW->getInstruction())->getReturnValue() != nullptr)
          PDG->addDependency(tmpInstW, instW, DependencyType::DATA_GENERAL);
      }
    }
  }

  // connect caller InstW with ACTUAL IN/OUT parameter trees
  CallInst *CI = dyn_cast<CallInst>(instW->getInstruction());
  for (ArgumentWrapper *argW : pdgUtils.getCallMap()[CI]->getArgWList())
  {
    InstructionWrapper *actual_inW = *(argW->getTree(TreeType::ACTUAL_IN_TREE).begin());
    InstructionWrapper *actual_outW = *(argW->getTree(TreeType::ACTUAL_OUT_TREE).begin());

    if (instW == actual_inW || instW == actual_outW)
      continue;

    PDG->addDependency(instW, actual_inW, DependencyType::PARAMETER);
    PDG->addDependency(instW, actual_outW, DependencyType::PARAMETER);
  }

  connectActualTreeToFormalTree(CI, callee);
  // // 3. ACTUAL_OUT --> LoadInsts in #Caller# function
  // for (tree<InstructionWrapper *>::iterator
  //          actual_out_TI = (*actual_argI)->getTree(TreeType::ACTUAL_OUT_TREE).begin(),
  //          actual_out_TE = (*actual_argI)->getTree(TreeType::ACTUAL_OUT_TREE).end();
  //      actual_out_TI != actual_out_TE; ++actual_out_TI)
  // {
  //   for (LoadInst *loadInst : pdgUtils.getFuncMap()[instW->getFunction()]->getLoadInstList())
  //   {
  //     if ((*actual_out_TI)->getLLVMType() != loadInst->getType())
  //       continue;

  //     for (InstructionWrapper *tmpInstW : pdgUtils.getFuncInstWMap()[callee])
  //     {
  //       if (tmpInstW->getInstruction() == dyn_cast<Instruction>(loadInst))
  //         PDG->addDependency(*actual_out_TI, tmpInstW, DependencyType::DATA_GENERAL);
  //     }
  //   }
  // }
  // }
  return true;
}

void pdg::ProgramDependencyGraph::copyFormalTreeToActualTree(CallInst *CI, Function *func)
{
  auto &pdgUtils = PDGUtils::getInstance();
  CallWrapper *CW = pdgUtils.getCallMap()[CI];
  FunctionWrapper *funcW = pdgUtils.getFuncMap()[func];
  auto argI = CW->getArgWList().begin();
  auto argE = CW->getArgWList().end();
  auto argFI = funcW->getArgWList().begin();
  auto argFE = funcW->getArgWList().end();
  //copy Formal Tree to Actual Tree. Actual trees are used by call site.
  for (; argI != argE && argFI != argFE; ++argI, ++argFI)
  {
    (*argI)->copyTree((*argFI)->getTree(TreeType::FORMAL_IN_TREE), TreeType::ACTUAL_IN_TREE);
    (*argI)->copyTree((*argFI)->getTree(TreeType::FORMAL_IN_TREE), TreeType::ACTUAL_OUT_TREE);
  }

  // copy return value wrapper
  ArgumentWrapper *CIRetW = pdgUtils.getCallMap()[CI]->getRetW();
  ArgumentWrapper *FuncRetW = pdgUtils.getFuncMap()[func]->getRetW();

  if (CIRetW == nullptr)
    return;
  CIRetW->copyTree(FuncRetW->getTree(TreeType::FORMAL_IN_TREE), TreeType::ACTUAL_IN_TREE);
  CIRetW->copyTree(FuncRetW->getTree(TreeType::FORMAL_IN_TREE), TreeType::ACTUAL_OUT_TREE);
  // linkGEPsWithTree(CI);
}

void pdg::ProgramDependencyGraph::buildActualParameterTrees(CallInst *CI)
{
  auto &pdgUtils = PDGUtils::getInstance();
  // need to obtained called function and then copy the formal tree
  Function *called_func;
  // processing indirect call. Pick the first candidate function
  if (CI->getCalledFunction() == nullptr)
  {
    if (Function *f = dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts())) // Call to bitcast case
    {
      called_func = f;
    }
    else
    {
      std::vector<Function *> indirect_call_candidate = collectIndirectCallCandidates(CI->getFunctionType(), *(CI->getFunction()));
      if (indirect_call_candidate.size() == 0)
      {
        errs() << "No possible matching candidate, no need to build actual parameter tree"
               << "\n";
        return;
      }
      // get the first possible candidate
      called_func = indirect_call_candidate[0];
    }
  }
  else
  {
    called_func = CI->getCalledFunction();
  }

  copyFormalTreeToActualTree(CI, called_func);
  drawActualParameterTree(CI, TreeType::ACTUAL_IN_TREE);
  drawActualParameterTree(CI, TreeType::ACTUAL_OUT_TREE);
}

void pdg::ProgramDependencyGraph::drawActualParameterTree(CallInst *CI, pdg::TreeType treeTy)
{
  auto &pdgUtils = PDGUtils::getInstance();
  int ARG_POS = 0;
  for (std::vector<ArgumentWrapper *>::iterator argWI = pdgUtils.getCallMap()[CI]->getArgWList().begin();
       argWI != pdgUtils.getCallMap()[CI]->getArgWList().end();
       ++argWI)
  {
    Value *tmp_val = CI->getOperand(ARG_POS); // get the corresponding argument
    if (Instruction *tmpInst = dyn_cast<Instruction>(tmp_val))
    {
      Function *func = (*argWI)->getArg()->getParent();
      auto treeBegin = (*argWI)->getTree(TreeType::ACTUAL_IN_TREE).begin();
      // link each argument's instruction with actual tree head
      PDG->addDependency(pdgUtils.getInstMap()[tmpInst], *treeBegin, DependencyType::PARAMETER);
    }
    for (tree<InstructionWrapper *>::iterator TI = (*argWI)->getTree(treeTy).begin();
         TI != (*argWI)->getTree(treeTy).end();
         ++TI)
    {
      for (unsigned i = 0; i < TI.number_of_children(); i++)
      {
        InstructionWrapper *childW = *(*argWI)->getTree(treeTy).child(TI, i);
        PDG->addDependency(*TI, childW, DependencyType::PARAMETER);
      }
    }
    ARG_POS++;
  }
}

std::vector<Function *> pdg::ProgramDependencyGraph::collectIndirectCallCandidates(FunctionType *funcType, Function &oriFunc, const std::set<std::string> &filterFuncs)
{
  std::vector<Function *> indirectCallList;
  for (auto &F : *module)
  {
    std::string funcName = F.getName().str();
    // get Function type
    if (funcName == "main" || &F == &oriFunc)
      continue;
    // compare the indirect call function type with each function, filter out certian functions that should not be considered as call targets
    if (isFuncTypeMatch(funcType, F.getFunctionType()) && filterFuncs.find(funcName) != filterFuncs.end())
      indirectCallList.push_back(&F);
  }

  return indirectCallList;
}

Function *pdg::ProgramDependencyGraph::getCalledFunction(CallInst *CI)
{
  if (isIndirectCallOrInlineAsm(CI))
    return nullptr;
  Function *calledFunc = CI->getCalledFunction();
  if (calledFunc == nullptr)
    return dyn_cast<Function>(CI->getCalledValue()->stripPointerCasts()); // handle case for bitcast
  return calledFunc;
  // assert(calledFunc != nullptr && "Cannot find called function");
  // return calledFunc;
}

// -------------------------------
//
// Field sensitive functions
//
// -------------------------------

Value *pdg::ProgramDependencyGraph::getLShrOnGep(GetElementPtrInst *gep)
{
  for (auto u : gep->users())
  {
    if (LoadInst *li = dyn_cast<LoadInst>(u))
    {
      for (auto user : li->users())
      {
        if (isa<LShrOperator>(user))
          return user;
      }
    }
  }
  return nullptr;
}

bool pdg::ProgramDependencyGraph::isGEPforBitField(GetElementPtrInst *gep)
{
  for (auto u : gep->users())
  {
    if (LoadInst *li = dyn_cast<LoadInst>(u))
    {
      for (auto user : li->users())
      {
        if (isa<LShrOperator>(user))
          return true;
      }
    }
  }
  return false;
}

uint64_t pdg::ProgramDependencyGraph::getGEPOffsetInBits(StructType *structTy, GetElementPtrInst *gep)
{
  // get the accessed struct member offset from the gep instruction
  unsigned gepFieldOffset = getGEPAccessFieldOffset(gep);
  if (gepFieldOffset < 0 || gepFieldOffset >= structTy->getNumElements())
    return -1;
  // use the struct layout to figure out the offset in bits
  auto const &dataLayout = module->getDataLayout();
  auto const structLayout = dataLayout.getStructLayout(structTy);
  uint64_t fieldOffsetInBits = structLayout->getElementOffsetInBits(gepFieldOffset);
  // check if the gep may be used for accessing bit fields
  if (isGEPforBitField(gep))
  {
    // compute the accessed bit offset here
    if (auto LShrInst = dyn_cast<LShrOperator>(getLShrOnGep(gep)))
    {
      auto LShrOffsetOp = LShrInst->getOperand(1);
      if (ConstantInt *constInst = dyn_cast<ConstantInt>(LShrOffsetOp))
      {
        fieldOffsetInBits += constInst->getSExtValue();
      }
    }
  }
  // other situations?
  return fieldOffsetInBits;
}

unsigned pdg::ProgramDependencyGraph::getGEPAccessFieldOffset(GetElementPtrInst *gep)
{
  int operand_num = gep->getNumOperands();
  llvm::Value *last_idx = gep->getOperand(operand_num - 1);
  // cast the last_idx to int type
  if (llvm::ConstantInt *constInt = dyn_cast<ConstantInt>(last_idx))
    return constInt->getSExtValue();
  return -1;
}

bool pdg::ProgramDependencyGraph::isTreeNodeGEPMatch(StructType *structTy, InstructionWrapper *treeNode, Instruction *GEP)
{
  if (structTy == nullptr)
    return false;
  if (auto gepInst = dyn_cast<GetElementPtrInst>(GEP))
  {
    uint64_t gepAccessMemOffset = getGEPOffsetInBits(structTy, gepInst);
    if (treeNode->getDIType() == nullptr || gepAccessMemOffset < 0)
      return false;
    uint64_t debuggingOffset = treeNode->getDIType()->getOffsetInBits();
    // if (GEP->getFunction()->getName() == "main")
    //   errs() << "\t" << *GEP << ": " << gepAccessMemOffset << " - " << debuggingOffset << "\n";
    if (gepAccessMemOffset == debuggingOffset)
      return true;
  }
  return false;
}

bool pdg::ProgramDependencyGraph::isFuncPointer(Type *ty)
{
  if (ty->isPointerTy())
    return dyn_cast<PointerType>(ty)->getElementType()->isFunctionTy();
  return false;
}

bool pdg::ProgramDependencyGraph::isStructPointer(Type *ty)
{
  if (ty->isPointerTy())
    return ty->getPointerElementType()->isStructTy();
  return false;
}

StructType *pdg::ProgramDependencyGraph::getStructTypeFromGEP(Instruction *inst)
{
  if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(inst))
  {
    Value *baseAddr = gep->getPointerOperand();
    if (baseAddr->getType()->isPointerTy())
    {
      if (StructType *structTy = dyn_cast<StructType>(baseAddr->getType()->getPointerElementType()))
        return structTy;
    }
    if (StructType *structTy = dyn_cast<StructType>(baseAddr->getType()))
      return structTy;
  }
  return nullptr;
}

void pdg::ProgramDependencyGraph::connectCallerAndActualTrees(Function *caller)
{
  auto &pdgUtils = PDGUtils::getInstance();
  auto callinstList = pdgUtils.getFuncMap()[caller]->getCallInstList();
  for (auto callinst : callinstList)
  {
    auto callW = pdgUtils.getCallMap()[callinst];
    if (callW == nullptr) // for special call insts such as intrinsic etc, we do not build call wrapper for it
      continue;

    // for each argument in a call instruction, we connect it with the variable in the caller.
    for (auto argW : callW->getArgWList())
    {
      unsigned argIdx = argW->getArg()->getArgNo();
      // get the arg value at callre side
      Value *argActualVal = getCallSiteParamVal(callinst, argIdx);
      Instruction *argActualInst = dyn_cast<Instruction>(argActualVal);
      InstructionWrapper *argActualInstW = pdgUtils.getInstMap()[argActualInst];
      // add dependency between the arg value with actual tree root.
      auto actualInTreeBegin = argW->tree_begin(TreeType::ACTUAL_IN_TREE);
      auto actualInTreeEnd = argW->tree_end(TreeType::ACTUAL_IN_TREE);
      PDG->addDependency(*actualInTreeBegin, argActualInstW, DependencyType::VAL_DEP);

      // recursively add child nodes with derived values
      for (auto actualTreeI = actualInTreeBegin; actualTreeI != actualInTreeEnd; ++actualInTreeBegin)
      {
        if (tree<InstructionWrapper *>::depth(actualTreeI) == 0)
          continue;

        auto parentI = tree<InstructionWrapper *>::parent(actualTreeI);
        auto parentValDepNodes = getNodesWithDepType(*parentI, DependencyType::VAL_DEP);
        for (auto pair : parentValDepNodes)
        {
          auto parentDepInstW = pair.first->getData();
          std::set<InstructionWrapper *> parentDepInstAliasList;
          getAllAlias(parentDepInstW->getInstruction(), parentDepInstAliasList);
          parentDepInstAliasList.insert(const_cast<InstructionWrapper *>(parentDepInstW));

          for (auto depInstAlias : parentDepInstAliasList)
          {
            if (depInstAlias->getInstruction() == nullptr)
              continue;
            std::set<InstructionWrapper *> readInstWs;
            getReadInstsOnInst(depInstAlias->getInstruction(), readInstWs);

            for (auto readInstW : readInstWs)
            {
              if (isa<LoadInst>(readInstW->getInstruction()))
                PDG->addDependency(*actualTreeI, readInstW, DependencyType::VAL_DEP);
              // for GEP, checks the offset acutally match
              else if (isa<GetElementPtrInst>(readInstW->getInstruction()))
              {
                StructType *structTy = getStructTypeFromGEP(readInstW->getInstruction());
                if (structTy != nullptr)
                {
                  if (isTreeNodeGEPMatch(structTy, *actualTreeI, readInstW->getInstruction()))
                    PDG->addDependency(*actualTreeI, readInstW, DependencyType::VAL_DEP);
                }
              }
            }
          }
        }
      }
    }
  }
}

Value *pdg::ProgramDependencyGraph::getCallSiteParamVal(CallInst *CI, unsigned idx)
{
  unsigned arg_size = CI->getNumArgOperands();
  if (idx >= arg_size)
  {
    assert(false && "Index out of bound for accesssing call instruction arg!");
  }
  return CI->getArgOperand(idx);
}

// std::set<Function *> pdg::ProgramDependencyGraph::inferAsynchronousCalledFunction()
// {
//   auto &pdgUtils = PDGUtils::getInstance();
//   auto funcMap = pdgUtils.getFuncMap();
//   auto crossDomainFuncs = pdgUtils.computeCrossDomainFuncs(*module);
//   auto kernelDomainFuncs = pdgUtils.computeKernelDomainFuncs(*module);
//   auto driverDomainFuncs = pdgUtils.computeDriverDomainFuncs(*module);
//   auto driverExportFuncPtrNameMap = pdgUtils.computeDriverExportFuncPtrNameMap();
//   std::set<Function *> asynCalls;
//   // interate through all call instructions and determine all the possible call targets.
//   std::set<Function *> calledFuncs;
//   for (Function &F : *module)
//   {
//     if (F.isDeclaration() || F.empty())
//       continue;
//     auto funcW = funcMap[&F];
//     auto callInstList = funcW->getCallInstList();
//     for (auto callInst : callInstList)
//     {
//       Function *calledFunc = dyn_cast<Function>(callInst->getCalledValue()->stripPointerCasts());
//       // direct call
//       if (calledFunc != nullptr)
//         calledFuncs.insert(calledFunc);
//     }
//   }

//   // driver export functions, assume to be called from kernel to driver
//   for (auto pair : driverExportFuncPtrNameMap)
//   {
//     Function *f = module->getFunction(pair.first);
//     if (f != nullptr)
//       calledFuncs.insert(f);
//   }

//   // determien if transitive closure of uncalled functions contains cross-domain functions
//   std::set<Function *> searchDomain;
//   searchDomain.insert(kernelDomainFuncs.begin(), kernelDomainFuncs.end());
//   searchDomain.insert(driverDomainFuncs.begin(), driverDomainFuncs.end());
//   for (auto &F : *module)
//   {
//     if (F.isDeclaration() || F.empty())
//       continue;
//     if (calledFuncs.find(&F) != calledFuncs.end())
//       continue;
//     if (driverDomainFuncs.find(&F) == driverDomainFuncs.end())
//       continue;
//     if (F.getName().find("init_module") != std::string::npos || F.getName().find("cleanup_module") != std::string::npos)
//       continue;
//     std::set<Function *> transitiveFuncs = pdgUtils.getTransitiveClosureInDomain(F, searchDomain);
//     for (auto transAsyncFunc : transitiveFuncs)
//     {
//       asynCalls.insert(transAsyncFunc);
//     }
//   }
//   return asynCalls;
// }

bool pdg::ProgramDependencyGraph::isUnsafeTypeCast(Instruction *inst)
{
  if (inst == nullptr)
    return false;

  if (CastInst *ci = dyn_cast<CastInst>(inst))
  {
    // an adhoc way for checking if this is a union type cast
    std::string inst_str;
    llvm::raw_string_ostream ss(inst_str);
    ss << *ci;
    if (ss.str().find("union") != std::string::npos)
      return false;
    if (ss.str().find("struct.anon") != std::string::npos)
      return false;

    Type* casted_type = ci->getType();
    Type* original_type = ci->getOperand(0)->getType();
    if (isStructPointer(casted_type) && isStructPointer(original_type))
    {
      if (casted_type != original_type)
      {
        errs() << "Unsafe type cast instruction: " << *ci << " - " << inst->getFunction()->getName() << "\n";
        return true;
      }
    }
  }
  return false;
}

static RegisterPass<pdg::ProgramDependencyGraph>
    PDG("pdg", "Program Dependency Graph Construction", false, true);