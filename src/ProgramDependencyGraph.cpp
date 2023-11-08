#include "ProgramDependencyGraph.hh"
#include <chrono> 

using namespace llvm;

char pdg::ProgramDependencyGraph::ID = 0;

bool pdg::DEBUG;

cl::opt<bool, true> DEBUG("pdg-debug", cl::desc("print debug messages"), cl::value_desc("print debug messages"), cl::location(pdg::DEBUG), cl::init(false));

void pdg::ProgramDependencyGraph::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.addRequired<DataDependencyGraph>();
  AU.addRequired<ControlDependencyGraph>();
  AU.setPreservesAll();
}

bool pdg::ProgramDependencyGraph::runOnModule(Module &M)
{
  auto start = std::chrono::high_resolution_clock::now();
  _module = &M;
  _PDG = &ProgramGraph::getInstance();

  // PDGCallGraph &call_g = PDGCallGraph::getInstance();
  // if (!call_g.isBuild())
  //   call_g.build(M);

  // for(auto edge : call_g.getEdgeSet())
  // {
  //   // edge->getSrcNode()
  // }

  // for(auto node : call_g)
  // {
  //   _PDG->addNode(*node);
  // }

  // for(auto edge : call_g.getEdgeSet())
  // {
  //   edge->getSrcNode()->addInEdge(*edge);
  //   edge->getDstNode()->addOutEdge(*edge);
  // }

  if (!_PDG->isBuild())
  {
    _PDG->build(M);
    _PDG->bindDITypeToNodes(M);
  }
  populateInitializerMap();
  unsigned func_size = 0;
  connectGlobalWithUses();
  for (auto &F : M)
  {
    if (F.isDeclaration())
      continue;
    connectIntraprocDependencies(F);
    connectInterprocDependencies(F);
    func_size++;
  }
  // errs() << "func size: " << func_size << "\n";
  // errs() << "Finsh adding dependencies" << "\n";
  auto stop = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
  // errs() << "building PDG takes: " <<  duration.count() << "\n";
  // errs() << "PDG Node size: " << _PDG->numNode() << "\n";

  if (DEBUG)
    _PDG->dumpGraph();

  return false;
}

void pdg::ProgramDependencyGraph::populateInitializerMap()
{
  for(auto &global_var : _module->getGlobalList())
  {
    if(global_var.hasInitializer())
    {
      initializer_map.insert(std::pair<llvm::Constant*, llvm::GlobalVariable*>(global_var.getInitializer(), &global_var));
    }
  }
}

void pdg::ProgramDependencyGraph::connectGlobalWithUses()
{

  for (auto &global_var : _module->getGlobalList())
  {
    Node* n = _PDG->getNode(global_var);
    if (n == nullptr)
      continue;


    for (auto user : global_var.users())
    {
      Node* user_node = _PDG->getNode(*user);
      if (user_node == nullptr)
      {
        if(initializer_map.find(user) != initializer_map.end())
        {
          auto other_global = initializer_map[user];
          if(auto other_node = _PDG->getNode(*other_global))
          {
            other_node->addNeighbor(*n, EdgeType::DATA_GLOBAL_DEF_USE);
          }
        }
      } 
      else 
      {
        n->addNeighbor(*user_node, EdgeType::DATA_DEF_USE);
      }
    }
  }
}


void pdg::ProgramDependencyGraph::connectInTrees(Tree* src_tree, Tree* dst_tree, EdgeType edge_type)
{
  if (src_tree->size() != dst_tree->size())
    return;
  auto src_tree_root_node = src_tree->getRootNode();
  auto dst_tree_root_node = dst_tree->getRootNode();
  std::queue<std::pair<TreeNode*, TreeNode*>> node_pairs_queue;
  node_pairs_queue.push(std::make_pair(src_tree_root_node, dst_tree_root_node));
  while (!node_pairs_queue.empty())
  {
    auto current_node_pair = node_pairs_queue.front();
    node_pairs_queue.pop();
    TreeNode* src = current_node_pair.first;
    TreeNode* dst = current_node_pair.second;
    assert(src->numOfChild() == dst->numOfChild());
    src->addNeighbor(*dst, edge_type);
    auto src_node_children = src->getChildNodes();
    auto dst_node_children = dst->getChildNodes();
    for (int i = 0; i < src->numOfChild(); i++)
    {
      node_pairs_queue.push(std::make_pair(src_node_children[i], dst_node_children[i]));
    }
  }
}

void pdg::ProgramDependencyGraph::connectOutTrees(Tree* src_tree, Tree* dst_tree, EdgeType edge_type)
{
  if (src_tree->size() != dst_tree->size())
    return;
  auto src_tree_root_node = src_tree->getRootNode();
  auto dst_tree_root_node = dst_tree->getRootNode();
  std::queue<std::pair<TreeNode*, TreeNode*>> node_pairs_queue;
  node_pairs_queue.push(std::make_pair(src_tree_root_node, dst_tree_root_node));
  while (!node_pairs_queue.empty())
  {
    auto current_node_pair = node_pairs_queue.front();
    node_pairs_queue.pop();
    TreeNode* src = current_node_pair.first;
    TreeNode* dst = current_node_pair.second;
    assert(src->numOfChild() == dst->numOfChild());
    if (src->hasWriteAccess())
      src->addNeighbor(*dst, edge_type);
    auto src_node_children = src->getChildNodes();
    auto dst_node_children = dst->getChildNodes();
    for (int i = 0; i < src->numOfChild(); i++)
    {
      node_pairs_queue.push(std::make_pair(src_node_children[i], dst_node_children[i]));
    }
  }
}

bool pdg::ProgramDependencyGraph::isFuncSignatureMatch(llvm::CallInst &ci, llvm::Function &f)
{
  if (f.isVarArg())
    return false;
  auto actual_arg_list_size = ci.arg_size();
  auto formal_arg_list_size = f.arg_size();
  if (actual_arg_list_size != formal_arg_list_size)
    return false;
  // compare return type
  auto actual_ret_type = ci.getType();
  auto formal_ret_type = f.getReturnType();
  if (!isTypeEqual(*actual_ret_type, *formal_ret_type))
    return false;
  
  for (unsigned i = 0; i < actual_arg_list_size; i++)
  {
    auto actual_arg = ci.getOperand(i);
    auto formal_arg = f.getArg(i);
    if (!isTypeEqual(*actual_arg->getType(), *formal_arg->getType()))
      return false;
  }
  return true;
}

bool pdg::ProgramDependencyGraph::isTypeEqual(llvm::Type& t1, llvm::Type &t2)
{
  if (&t1 == &t2)
    return true;
  // need to compare name for sturct, due to llvm-link duplicate struct types
  if (!t1.isPointerTy() || !t2.isPointerTy())
    return false;

  auto t1_pointed_ty = t1.getPointerElementType();
  auto t2_pointed_ty = t2.getPointerElementType();

  if (!t1_pointed_ty->isStructTy() || !t2_pointed_ty->isStructTy())
    return false;
  
  auto t1_name = pdgutils::stripVersionTag(t1_pointed_ty->getStructName().str());
  auto t2_name = pdgutils::stripVersionTag(t2_pointed_ty->getStructName().str());

  return (t1_name == t2_name);
}

void pdg::ProgramDependencyGraph::argPassEdges(CallWrapper& cw, EdgeType in_edge, EdgeType out_edge)
{
  auto& ci = *cw.getCallInst();
  for(auto arg : cw.getArgList())
  {
    auto in_tree = cw.getArgActualInTree(*arg);
    auto out_tree = cw.getArgActualOutTree(*arg);

    TreeNode* root_in = nullptr;
    TreeNode* root_out = nullptr;

    if(in_tree)
      root_in = in_tree->getRootNode();
    if(out_tree)
      root_out = out_tree->getRootNode();

    auto arg_node = _PDG->getNode(*arg);

    if(arg_node && root_in && root_out)
    {
      arg_node->addNeighbor(*root_in, in_edge);
      root_out->addNeighbor(*arg_node, out_edge);
    } else
    {
      errs() << "WARNING: No node found for argument: " << *arg << "\n";
    }
    
  }
}

void pdg::ProgramDependencyGraph::connectCallerIndirect(llvm::CallInst &ci)
{
  auto callSiteNode = _PDG->getNode(ci);
  for (auto &func : *_module)
  {
    auto potentialCallee = _PDG->getNode(func);
    if(potentialCallee)
    {
      auto hasUse = false; 
      for(auto user : func.users())
      {
        if(_PDG->hasNode(*user))
          hasUse = true;
      }
      if(isFuncSignatureMatch(ci, func) && hasUse)
      {
        callSiteNode->addNeighbor(*potentialCallee, EdgeType::IND_CALL);
        if(auto fw = getFuncWrapper(func))
        {
          CallWrapper cw(ci);
          cw.buildActualTreeForArgs(*fw);
          cw.buildActualTreesForRetVal(*fw);
          argPassEdges(cw, EdgeType::DATA_ARGPASS_INDIRECT_IN, EdgeType::DATA_ARGPASS_INDIRECT_OUT);

          for(auto arg : cw.getArgList())
          {
            auto in_tree = cw.getArgActualInTree(*arg);
            auto out_tree = cw.getArgActualOutTree(*arg);
            TreeNode* root_in = nullptr;
            TreeNode* root_out = nullptr;
            if(in_tree)
              root_in = in_tree->getRootNode();
            if(out_tree)
              root_out = out_tree->getRootNode();
            if(root_in)
              _PDG->addNode(*root_in);
            if(root_out)
              _PDG->addNode(*root_out);

            if(!(root_in && root_out))
              errs() << "WARNING: Could not connect indirect argpass edges" << "\n";
          }

          for(auto ret : fw->getReturnInsts())
          {
            Node *src = _PDG->getNode(*ret);
            if (src == nullptr)
              continue;
            src->addNeighbor(*potentialCallee, EdgeType::DATA_INDIRECT_RET);
          }
        }
      }
    }
  }
}

void pdg::ProgramDependencyGraph::connectCallerAndCallee(CallWrapper &cw, FunctionWrapper &fw)
{

  argPassEdges(cw, EdgeType::DATA_ARGPASS_IN, EdgeType::DATA_ARGPASS_OUT);

  // step 1: connect call site node with the entry node of function
  auto call_site_node = _PDG->getNode(*cw.getCallInst());
  auto func_entry_node = fw.getEntryNode();
  if (call_site_node == nullptr || func_entry_node == nullptr )
    return;
  call_site_node->addNeighbor(*func_entry_node, EdgeType::CONTROLDEP_CALLINV);

  // step4: connect both control/data return edges of callee to the call site
  auto ret_insts = fw.getReturnInsts();
  auto call_inst = cw.getCallInst();
  Node *dst = _PDG->getNode(*call_inst);
  assert(dst != nullptr && "cannot add control edge to call node on nullptr!\n");
  // add control return edge
  for (auto ret_inst : ret_insts)
  {
    Node *src = _PDG->getNode(*ret_inst);
    if (src == nullptr)
      continue;
    src->addNeighbor(*dst, EdgeType::CONTROLDEP_CALLRET);
  }
  // add data return edge
  for (auto ret_inst : ret_insts)
  {
    Node* src = _PDG->getNode(*ret_inst);
    if (src == nullptr)
      continue;
    src->addNeighbor(*dst, EdgeType::DATA_RET);
  }


  // step 2: connect actual in -> formal in, formal out -> actual out
  auto actual_arg_list = cw.getArgList();
  auto formal_arg_list = fw.getArgList();
  if(!(actual_arg_list.size() == formal_arg_list.size())) 
  {
    errs() << "cannot connect tree edges due to inequal arg num! " << *call_site_node->getValue() << "\n";
    return;
  }
  int num_arg = cw.getArgList().size();
  for (int i = 0; i < num_arg; i++)
  {
    Value *actual_arg = actual_arg_list[i];
    Argument *formal_arg =  formal_arg_list[i];
    // step 2: connect actual in -> formal in
    auto actual_in_tree = cw.getArgActualInTree(*actual_arg);
    auto formal_in_tree = fw.getArgFormalInTree(*formal_arg);
    if (actual_in_tree == nullptr || formal_in_tree == nullptr)
      continue;
    _PDG->addTreeNodesToGraph(*actual_in_tree);
    connectInTrees(actual_in_tree, formal_in_tree, EdgeType::PARAMETER_IN);
    // step 3: connect actual out -> formal out
    auto actual_out_tree = cw.getArgActualOutTree(*actual_arg);
    auto formal_out_tree = fw.getArgFormalOutTree(*formal_arg);
    _PDG->addTreeNodesToGraph(*actual_out_tree);
    connectOutTrees(formal_out_tree, actual_out_tree, EdgeType::PARAMETER_OUT);
  }

  // step3: connect return value actual in -> formal in, formal out -> actual out
  if (!fw.hasNullRetVal() && !cw.hasNullRetVal())
  {
    Tree *ret_formal_in_tree = fw.getRetFormalInTree();
    Tree *ret_formal_out_tree = fw.getRetFormalOutTree();
    Tree *ret_actual_in_tree = cw.getRetActualInTree();
    Tree *ret_actual_out_tree = cw.getRetActualOutTree();
    connectInTrees(ret_actual_in_tree, ret_formal_in_tree, EdgeType::PARAMETER_IN);
    connectInTrees(ret_actual_out_tree, ret_formal_out_tree, EdgeType::PARAMETER_OUT);
  }

}

// ===== connect dependencies =====
void pdg::ProgramDependencyGraph::connectIntraprocDependencies(Function &F)
{
  // add control dependency edges
  getAnalysis<ControlDependencyGraph>(F); // add control dependencies for nodes in F
  // connect formal tree with address variables
  FunctionWrapper* func_w = getFuncWrapper(F);
  Node* entry_node = func_w->getEntryNode();
  for (auto arg : func_w->getArgList())
  {
    Tree* formal_in_tree = func_w->getArgFormalInTree(*arg);
    if (!formal_in_tree)
      return;

    Tree* formal_out_tree = func_w->getArgFormalOutTree(*arg);
    entry_node->addNeighbor(*formal_in_tree->getRootNode(), EdgeType::PARAMETER_IN);
    entry_node->addNeighbor(*formal_out_tree->getRootNode(), EdgeType::PARAMETER_OUT);
    connectFormalInTreeWithAddrVars(*formal_in_tree);
    connectFormalOutTreeWithAddrVars(*formal_out_tree);
  }

  if (!func_w->hasNullRetVal())
  {
    connectFormalInTreeWithAddrVars(*func_w->getRetFormalInTree());
    connectFormalOutTreeWithAddrVars(*func_w->getRetFormalOutTree());
  }
}

void pdg::ProgramDependencyGraph::connectInterprocDependencies(Function &F)
{
  auto func_w = getFuncWrapper(F);
  auto call_insts = func_w->getCallInsts();
  for (auto call_inst : call_insts)
  {
    if (_PDG->hasCallWrapper(*call_inst))
    {
      auto call_w = getCallWrapper(*call_inst);
      if (!call_w)
        continue;
      auto call_site_node = _PDG->getNode(*call_inst);
      if (!call_site_node)
        continue;
     
      for (auto arg : call_w->getArgList())
      {
        Tree* actual_in_tree = call_w->getArgActualInTree(*arg);
        if (!actual_in_tree)
        {
          errs() << "[WARNING]: empty actual tree for callsite " << *call_inst << "\n";
          continue;
        }
        Tree* actual_out_tree = call_w->getArgActualOutTree(*arg);
        call_site_node->addNeighbor(*actual_in_tree->getRootNode(), EdgeType::PARAMETER_IN);
        call_site_node->addNeighbor(*actual_out_tree->getRootNode(), EdgeType::PARAMETER_OUT);
        connectActualTreeWithAddrVars(*actual_in_tree, *call_inst, EdgeType::PARAMETER_IN, true);
        connectActualTreeWithAddrVars(*actual_out_tree, *call_inst, EdgeType::PARAMETER_OUT, false);
      }
      // connect return trees
      // TODO: should change the name here. for return value, we should only connect
      // the tree node with vars after the call instruction
      if (!call_w->hasNullRetVal())
      {
        connectActualTreeWithAddrVars(*call_w->getRetActualInTree(), *call_inst, EdgeType::PARAMETER_IN, true);
        connectActualTreeWithAddrVars(*call_w->getRetActualOutTree(), *call_inst, EdgeType::PARAMETER_OUT, false);
      }
      if(call_w->getCalledFunc() != nullptr) 
      {
        auto called_func_w = getFuncWrapper(*call_w->getCalledFunc());
        connectCallerAndCallee(*call_w, *called_func_w);
      } 
    }
    else if(call_inst->isIndirectCall())
    {
      connectCallerIndirect(*call_inst);
    }
  }
}

// ====== connect tree with variables ======
void pdg::ProgramDependencyGraph::connectFormalInTreeWithAddrVars(Tree &formal_in_tree)
{
  TreeNode* root_node = formal_in_tree.getRootNode();
  std::queue<TreeNode*> node_queue;
  node_queue.push(root_node);
  while (!node_queue.empty())
  {
    TreeNode* current_node = node_queue.front();
    node_queue.pop();
    TreeNode* parent_node = current_node->getParentNode();
    std::unordered_set<Value*> parent_node_addr_vars;
    if (parent_node != nullptr)
      parent_node_addr_vars = parent_node->getAddrVars();
    for (auto addr_var : current_node->getAddrVars())
    {
      if (!_PDG->hasNode(*addr_var))
        continue;
      auto addr_var_node = _PDG->getNode(*addr_var);
      current_node->addNeighbor(*addr_var_node, EdgeType::PARAMETER_IN);
      auto alias_nodes = addr_var_node->getOutNeighborsWithDepType(EdgeType::DATA_ALIAS);
      for (auto alias_node : alias_nodes)
      {
        Value* alias_node_val = alias_node->getValue();
        if (alias_node_val == nullptr)
          continue;
        if (parent_node_addr_vars.find(alias_node_val) != parent_node_addr_vars.end())
          continue;
        current_node->addNeighbor(*alias_node, EdgeType::PARAMETER_IN);
      }
    }

    for (auto child_node : current_node->getChildNodes())
    {
      node_queue.push(child_node);
    }
  }
}

void pdg::ProgramDependencyGraph::connectFormalOutTreeWithAddrVars(Tree &formal_out_tree)
{
  TreeNode *root_node = formal_out_tree.getRootNode();
  std::queue<TreeNode *> node_queue;
  node_queue.push(root_node);
  while (!node_queue.empty())
  {
    TreeNode *current_node = node_queue.front();
    node_queue.pop();
    for (auto addr_var : current_node->getAddrVars())
    {
      if (!_PDG->hasNode(*addr_var))
        continue;
      auto addr_var_node = _PDG->getNode(*addr_var);
      // TODO: add addr variables for formal out tree
      if (pdgutils::hasWriteAccess(*addr_var))
      {
        addr_var_node->addNeighbor(*current_node, EdgeType::PARAMETER_OUT);
        current_node->addAccessTag(AccessTag::DATA_WRITE);
      }
    }

    for (auto child_node : current_node->getChildNodes())
    {
      node_queue.push(child_node);
    }
  }
}

void pdg::ProgramDependencyGraph::connectActualTreeWithAddrVars(Tree &actual_in_tree, CallInst &ci, EdgeType type, bool use_before_only)
{
  TreeNode *root_node = actual_in_tree.getRootNode();
  std::set<Instruction*> insts_before_after_ci = pdgutils::getInstructionBeforeInst(ci);
  std::queue<TreeNode *> node_queue;
  node_queue.push(root_node);
  while (!node_queue.empty())
  {
    TreeNode *current_node = node_queue.front();
    node_queue.pop();
    for (auto addr_var : current_node->getAddrVars())
    {
      // only connect addr_var that are pred to the call site
      if(use_before_only)
      {
        if (Instruction *i = dyn_cast<Instruction>(addr_var))
        {
          if (insts_before_after_ci.find(i) == insts_before_after_ci.end())
            continue;
        }
      }
      if (!_PDG->hasNode(*addr_var))
        continue;
      auto addr_var_node = _PDG->getNode(*addr_var);
      addr_var_node->addNeighbor(*current_node, type);
    }

    for (auto child_node : current_node->getChildNodes())
    {
      node_queue.push(child_node);
    }
  }
}

static RegisterPass<pdg::ProgramDependencyGraph>
    PDG("pdg", "Program Dependency Graph Construction", false, true);