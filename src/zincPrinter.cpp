#include "zincPrinter.hh"
#include <chrono> 

using namespace llvm;

char pdg::MiniZincPrinter::ID = 0;

bool DEBUGZINC;

cl::opt<bool, true> DEBUG_ZINC("zinc-debug", cl::desc("print debug messages"), cl::value_desc("print debug messages"), cl::location(DEBUGZINC), cl::init(false));


void pdg::MiniZincPrinter::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.addRequired<ProgramDependencyGraph>();
  AU.setPreservesAll();
}


template<typename K1, typename K2, typename V>
static std::map<K2, V> map_key_optional(std::function<std::optional<K2>(K1)> fn, std::map<K1, V> m)
{
  std::map<K2, V> result; 
  for(auto pair : m) {
    auto key = fn(pair.first);
    if(key)
      result[*key] = pair.second;
  }
  return result;
}

std::optional<pdg::MznNodeType> pdg::MiniZincPrinter::nodeMznType(pdg::GraphNodeType nodeType)
{
  std::map<pdg::GraphNodeType, pdg::MznNodeType> map {
    { pdg::GraphNodeType::INST_FUNCALL, pdg::MznNodeType::Inst_FunCall },
    { pdg::GraphNodeType::INST_RET, pdg::MznNodeType::Inst_Ret },
    { pdg::GraphNodeType::INST_BR, pdg::MznNodeType::Inst_Br },
    { pdg::GraphNodeType::INST_OTHER, pdg::MznNodeType::Inst_Other },
    { pdg::GraphNodeType::VAR_STATICALLOCGLOBALSCOPE, pdg::MznNodeType::VarNode_StaticGlobal },
    { pdg::GraphNodeType::VAR_STATICALLOCMODULESCOPE, pdg::MznNodeType::VarNode_StaticModule }, 
    { pdg::GraphNodeType::VAR_STATICALLOCFUNCTIONSCOPE, pdg::MznNodeType::VarNode_StaticFunction }, 
    { pdg::GraphNodeType::VAR_OTHER, pdg::MznNodeType::VarNode_StaticOther }, 
    { pdg::GraphNodeType::FUNC_ENTRY, pdg::MznNodeType::FunctionEntry },
    { pdg::GraphNodeType::PARAM_FORMALIN, pdg::MznNodeType::Param_FormalIn },
    { pdg::GraphNodeType::PARAM_FORMALOUT, pdg::MznNodeType::Param_FormalOut },
    { pdg::GraphNodeType::PARAM_ACTUALIN, pdg::MznNodeType::Param_ActualIn },
    { pdg::GraphNodeType::PARAM_ACTUALOUT, pdg::MznNodeType::Param_ActualOut },
    { pdg::GraphNodeType::ANNO_VAR, pdg::MznNodeType::Annotation_Var },
    { pdg::GraphNodeType::ANNO_GLOBAL, pdg::MznNodeType::Annotation_Global },
    { pdg::GraphNodeType::ANNO_OTHER, pdg::MznNodeType::Annotation_Other },
  };
  if (map.find(nodeType) == map.end())
    return std::nullopt;
  return std::optional<pdg::MznNodeType>(map[nodeType]);
}

std::optional<pdg::MznEdgeType> pdg::MiniZincPrinter::edgeMznType(pdg::EdgeType edgeType)
{
  std::map<pdg::EdgeType, pdg::MznEdgeType> map {
    { pdg::EdgeType::CONTROLDEP_CALLINV, pdg::MznEdgeType::ControlDep_CallInv },
    { pdg::EdgeType::IND_CALL, pdg::MznEdgeType::ControlDep_Indirect },
    { pdg::EdgeType::CONTROLDEP_CALLRET, pdg::MznEdgeType::ControlDep_CallRet },
    { pdg::EdgeType::CONTROLDEP_ENTRY, pdg::MznEdgeType::ControlDep_Entry },
    { pdg::EdgeType::CONTROLDEP_BR, pdg::MznEdgeType::ControlDep_Br },
    { pdg::EdgeType::CONTROLDEP_OTHER, pdg::MznEdgeType::ControlDep_Other },
    { pdg::EdgeType::DATA_DEF_USE, pdg::MznEdgeType::DataDepEdge_DefUse },
    { pdg::EdgeType::DATA_RAW, pdg::MznEdgeType::DataDepEdge_RAW },
    { pdg::EdgeType::DATA_ALIAS, pdg::MznEdgeType::DataDepEdge_Alias },
    { pdg::EdgeType::DATA_RET, pdg::MznEdgeType::DataDepEdge_Ret },
    { pdg::EdgeType::PARAMETER_IN, pdg::MznEdgeType::Parameter_In },
    { pdg::EdgeType::PARAMETER_OUT, pdg::MznEdgeType::Parameter_Out },
    { pdg::EdgeType::PARAMETER_FIELD, pdg::MznEdgeType::Parameter_Field },
    { pdg::EdgeType::ANNO_GLOBAL, pdg::MznEdgeType::Anno_Global },
    { pdg::EdgeType::ANNO_VAR, pdg::MznEdgeType::Anno_Var },
    { pdg::EdgeType::ANNO_OTHER, pdg::MznEdgeType::Anno_Other },
  };
  if (map.find(edgeType) == map.end())
    return std::nullopt;
  return std::optional<pdg::MznEdgeType>(map[edgeType]);
}

std::map<pdg::GraphNodeType, std::vector<pdg::Node *>> pdg::MiniZincPrinter::nodesByNodeType(pdg::ProgramGraph &PDG)
{
  std::map<pdg::GraphNodeType, std::vector<pdg::Node *>> map;
  for(auto node : PDG)
  {
    auto nodeType = node->getNodeType();

    if(map.find(nodeType) == map.end())
      map[nodeType] = std::vector<pdg::Node *>();

    map[nodeType].push_back(node);
  }
  return map;
}

std::map<pdg::EdgeType, std::vector<pdg::Edge *>> pdg::MiniZincPrinter::edgesByEdgeType(pdg::ProgramGraph &PDG)
{
  std::map<pdg::EdgeType, std::vector<pdg::Edge *>> map;
  for(auto node : PDG)
  {
    for(auto edge : *node)
    {
      // similarly found in original exporter: why was it needed? 
      if(edge->getSrcNode()->getNodeType() == GraphNodeType::ANNO_VAR 
      || edge->getDstNode()->getNodeType() == GraphNodeType::ANNO_VAR) 
      {
        edge->setEdgeType(EdgeType::ANNO_VAR);
      }

      auto edgeType = edge->getEdgeType(); 

      if(map.find(edgeType) == map.end()) 
        map[edgeType] = std::vector<pdg::Edge *>();

      map[edgeType].push_back(edge);
    }
  }
  return map;
}

template<typename A, typename B>
pdg::RangesAndIds<A, B> pdg::MiniZincPrinter::toRangesAndIds(std::map<A, std::vector<B>> groupedByA, std::function<unsigned int(B)> getId)
{
  std::map<A, std::pair<size_t, size_t>> ranges;
  std::map<unsigned int, size_t> ids;
  std::vector<B> ordered;
  size_t index = 0;
  for(auto pair : groupedByA) 
  {
    auto a = pair.first; 
    auto bs = pair.second; 
    int starting_index = index; 
    for(auto b : bs)
    {
      ids[getId(b)] = index;
      ordered.push_back(b);
      index++; 
    }
    ranges[a] = std::make_pair(starting_index, index); 
  }
  RangesAndIds<A, B> result{ ranges, ids, ordered };
  return result;
}


std::map<unsigned int, unsigned int> pdg::MiniZincPrinter::hasFn(pdg::ProgramGraph &PDG)
{
  std::map<unsigned int, unsigned int> result; 
  for(auto node : PDG)
  {
    auto fn = node->getFunc();
    if(fn) 
    {
      auto fnNode = PDG.getFuncWrapperMap()[fn]->getEntryNode();
      if(fnNode)
        result[node->getNodeID()] = fnNode->getNodeID();
    }
  }
  return result;
}

size_t pdg::MiniZincPrinter::maxFnParams(pdg::ProgramGraph &PDG)
{
  size_t maxFnParams = 0;
  for(auto node : PDG)
  {
    if(node->getNodeType() == GraphNodeType::FUNC_ENTRY)
    {
      auto numParams = node->getFunc()->arg_size();
      maxFnParams = std::max(maxFnParams, numParams);
    }
  }
  return maxFnParams;
}



std::string pdg::MiniZincPrinter::mznNodeName(MznNodeType nodeType)
{
  std::map<pdg::MznNodeType, std::string> map {
    { pdg::MznNodeType::Inst_FunCall, "Inst_FunCall" },
    { pdg::MznNodeType::Inst_Ret, "Inst_Ret" },
    { pdg::MznNodeType::Inst_Br, "Inst_Br" },
    { pdg::MznNodeType::Inst_Other, "Inst_Other" },
    { pdg::MznNodeType::Inst, "Inst" },
    { pdg::MznNodeType::VarNode_StaticGlobal, "VarNode_StaticGlobal" },
    { pdg::MznNodeType::VarNode_StaticModule, "VarNode_StaticModule" },
    { pdg::MznNodeType::VarNode_StaticFunction, "VarNode_StaticFunction" },
    { pdg::MznNodeType::VarNode_StaticOther, "VarNode_StaticOther" },
    { pdg::MznNodeType::VarNode, "VarNode" },
    { pdg::MznNodeType::FunctionEntry , "FunctionEntry" },
    { pdg::MznNodeType::Param_FormalIn, "Param_FormalIn" },
    { pdg::MznNodeType::Param_FormalOut, "Param_FormalOut" },
    { pdg::MznNodeType::Param_ActualIn, "Param_ActualIn" },
    { pdg::MznNodeType::Param_ActualOut, "Param_ActualOut" },
    { pdg::MznNodeType::Param, "Param" },
    { pdg::MznNodeType::Annotation_Var, "Annotation_Var" },
    { pdg::MznNodeType::Annotation_Global, "Annotation_Global" },
    { pdg::MznNodeType::Annotation_Other, "Annotation_Other" },
    { pdg::MznNodeType::Annotation, "Annotation" },
    { pdg::MznNodeType::PDGNode, "PDGNode" }
  };
  return map[nodeType];
}

std::string pdg::MiniZincPrinter::mznEdgeName(MznEdgeType edgeType)
{
  std::map<pdg::MznEdgeType, std::string> map {
    { pdg::MznEdgeType::ControlDep_CallInv, "ControlDep_CallInv" },
    { pdg::MznEdgeType::ControlDep_Indirect, "ControlDep_Indirect" },
    { pdg::MznEdgeType::ControlDep_CallRet, "ControlDep_CallRet" },
    { pdg::MznEdgeType::ControlDep_Entry, "ControlDep_Entry" },
    { pdg::MznEdgeType::ControlDep_Br, "ControlDep_Br" },
    { pdg::MznEdgeType::ControlDep_Other, "ControlDep_Other" },
    { pdg::MznEdgeType::ControlDep, "ControlDep" },
    { pdg::MznEdgeType::DataDepEdge_DefUse, "DataDepEdge_DefUse" },
    { pdg::MznEdgeType::DataDepEdge_RAW, "DataDepEdge_RAW" },
    { pdg::MznEdgeType::DataDepEdge_Ret, "DataDepEdge_Ret" },
    { pdg::MznEdgeType::DataDepEdge_Alias, "DataDepEdge_Alias" },
    { pdg::MznEdgeType::DataDepEdge, "DataDepEdge" },
    { pdg::MznEdgeType::Parameter_In, "Parameter_In" },
    { pdg::MznEdgeType::Parameter_Out, "Parameter_Out" },
    { pdg::MznEdgeType::Parameter_Field, "Parameter_Field" },
    { pdg::MznEdgeType::Parameter, "Parameter" },
    { pdg::MznEdgeType::Anno_Global, "Anno_Global" },
    { pdg::MznEdgeType::Anno_Var, "Anno_Var" },
    { pdg::MznEdgeType::Anno_Other, "Anno_Other" },
    { pdg::MznEdgeType::Anno, "Anno" },
    { pdg::MznEdgeType::PDGEdge, "PDGEdge" },
  };
  return map[edgeType];
}


template<typename A>
std::optional<std::pair<int, int>> pdg::MiniZincPrinter::calculateCollatedRange(std::map<A, std::pair<size_t, size_t>> ranges, A start, A end)
{
  // find start and end of collation
  // while loops are needed because ranges may not be defined at endpoints 
  auto firstDefined = (size_t)start;
  while(firstDefined < (size_t)end)
  {
    if(ranges.find((A)firstDefined) == ranges.end())
      firstDefined++;
    else
      break;
  }
  auto lastDefined = (size_t)end;
  while(lastDefined > (size_t)start)
  {
    if(ranges.find((A)lastDefined) == ranges.end())
      lastDefined--;
    else
      break;
  }

  if(
    ranges.find((A)firstDefined) == ranges.end() || 
    ranges.find((A)lastDefined) == ranges.end())
  {
    return std::nullopt;
  }
  else 
  {
    auto start = ranges[(A)firstDefined].first;
    auto end = ranges[(A)lastDefined].second;
    return std::optional(std::make_pair(start, end));
  }
}


void pdg::MiniZincPrinter::exportMznNodes(std::ofstream &mzn, pdg::NodeRangesAndIds nodes)
{
  std::map<MznNodeType, std::pair<MznNodeType, MznNodeType>> collations {
    { MznNodeType::Inst, { MznNodeType::Inst_FunCall, MznNodeType::Inst_Other } },
    { MznNodeType::VarNode, { MznNodeType::VarNode_StaticGlobal, MznNodeType::VarNode_StaticOther } },
    { MznNodeType::Param, { MznNodeType::Param_FormalIn, MznNodeType::Param_ActualOut } },
    { MznNodeType::Annotation, { MznNodeType::Annotation_Var, MznNodeType::Annotation_Other } },
    { MznNodeType::PDGNode, { MznNodeType::Inst_FunCall, MznNodeType::Annotation } },
  };
  auto outputStartEnd = [&](std::string name, int start, int end) {
    mzn << name << "_start = " << start << ";\n";
    mzn << name << "_end = " << end << ";\n";
  };

  for(size_t i = MznNodeType::Inst_FunCall; i <= MznNodeType::PDGNode; i++)
  {
    MznNodeType type = (MznNodeType)i;
    auto ranges = nodes.ranges;
    if(collations.find(type) != collations.end())
    {
      auto collation = collations[type];
      auto collatedRange = calculateCollatedRange(ranges, collation.first, collation.second);
      if(collatedRange)
        outputStartEnd(mznNodeName(type), collatedRange->first + 1, collatedRange->second);
      else
        outputStartEnd(mznNodeName(type), 0, -1);
    }
    else if(ranges.find(type) != ranges.end())
    {
      auto range = ranges[type];
      outputStartEnd(mznNodeName(type), range.first + 1, range.second);
    } else 
    {
      outputStartEnd(mznNodeName(type), 0, -1);
    }
  }
}


void pdg::MiniZincPrinter::exportMznEdges(std::ofstream &mzn, pdg::EdgeRangesAndIds edges)
{
  std::map<MznEdgeType, std::pair<MznEdgeType, MznEdgeType>> collations {
    { MznEdgeType::ControlDep, { MznEdgeType::ControlDep_CallInv, MznEdgeType::ControlDep_Other } },
    { MznEdgeType::DataDepEdge, { MznEdgeType::DataDepEdge_DefUse, MznEdgeType::DataDepEdge_Alias } },
    { MznEdgeType::Parameter, { MznEdgeType::Parameter_In, MznEdgeType::Parameter_Field } },
    { MznEdgeType::Anno, { MznEdgeType::Anno_Global, MznEdgeType::Anno_Other } },
    { MznEdgeType::PDGEdge, { MznEdgeType::ControlDep_CallInv, MznEdgeType::Anno } },
  };
  auto outputStartEnd = [&](std::string name, int start, int end) {
    mzn << name << "_start = " << start << ";\n";
    mzn << name << "_end = " << end << ";\n";
  };

  for(size_t i = MznEdgeType::ControlDep_CallInv; i <= MznEdgeType::PDGEdge; i++)
  {
    MznEdgeType type = (MznEdgeType)i;
    auto ranges = edges.ranges;
    if(collations.find(type) != collations.end())
    {
      auto collation = collations[type];
      auto collatedRange = calculateCollatedRange(ranges, collation.first, collation.second);
      if(collatedRange)
        outputStartEnd(mznEdgeName(type), collatedRange->first + 1, collatedRange->second);
      else
        outputStartEnd(mznEdgeName(type), 0, -1);
    }
    else if(ranges.find(type) != ranges.end())
    {
      auto range = ranges[type];
      outputStartEnd(mznEdgeName(type), range.first + 1, range.second);
    } else 
    {
      outputStartEnd(mznEdgeName(type), 0, -1);
    }
  }
}
template<typename A>
void pdg::MiniZincPrinter::exportVector(std::ofstream &mzn, std::string name, std::vector<A> items, std::optional<std::string> asArray1dOf)
{
  if(asArray1dOf)
    mzn << name << " = array1d(" << *asArray1dOf << ", [\n";
  else
    mzn << name << " = [\n";

  for(size_t i = 0; i < items.size(); i++)
  {
    mzn << items[i];
    if(i != items.size() - 1)
      mzn << ",";
  }
  if(asArray1dOf)
    mzn << "\n]);\n";
  else
    mzn << "\n];\n";
}

void pdg::MiniZincPrinter::exportMznSrcDst(std::ofstream &mzn, pdg::NodeRangesAndIds nodes, pdg::EdgeRangesAndIds edges)
{
  std::vector<size_t> hasSrcVec;  
  std::vector<size_t> hasDstVec;  
  for(auto edge : edges.ordered)
  {
    hasSrcVec.push_back(nodes.ids[edge->getSrcNode()->getNodeID()] + 1);
    hasDstVec.push_back(nodes.ids[edge->getDstNode()->getNodeID()] + 1);
  }
  exportVector(mzn, "hasSource", hasSrcVec);
  exportVector(mzn, "hasDest", hasDstVec);
}

void pdg::MiniZincPrinter::exportMznHasFn(std::ofstream &mzn, pdg::NodeRangesAndIds nodes, std::map<unsigned int, unsigned int> hasFn)
{

  std::vector<size_t> hasFnVec; 
  for(auto node : nodes.ordered)
  {
    if(hasFn.find(node->getNodeID()) != hasFn.end())
      hasFnVec.push_back(nodes.ids[hasFn[node->getNodeID()]] + 1); 
    else
      hasFnVec.push_back(0);
  }
  exportVector(mzn, "hasFunction", hasFnVec);
}


void pdg::MiniZincPrinter::exportMznParamIdx(std::ofstream &mzn, pdg::NodeRangesAndIds nodes)
{
  std::vector<int> indices;
  for(auto node : nodes.ordered)
  {
    int idx = node->getParamIdx();
    auto type = node->getNodeType();
    if(type == GraphNodeType::PARAM_FORMALIN
    || type == GraphNodeType::PARAM_FORMALOUT
    || type == GraphNodeType::PARAM_ACTUALIN
    || type == GraphNodeType::PARAM_ACTUALOUT)
    {
      if(idx >= 0)
        indices.push_back(idx + 1);
      else
        indices.push_back(idx);
    }
  }
  exportVector(mzn, "hasParamIdx", indices, "Param");
}

void pdg::MiniZincPrinter::exportMznUserAnnotated(std::ofstream &mzn, pdg::NodeRangesAndIds nodes)
{
  std::vector<std::string> userAnnotated;

  for(auto node : nodes.ordered)
  {
    if(node->getNodeType() == GraphNodeType::FUNC_ENTRY)
    {
      if(node->getAnno() != "None") 
        userAnnotated.push_back("true");
      else
        userAnnotated.push_back("false");
    }
  }
  exportVector(mzn, "userAnnotatedFunction", userAnnotated, "FunctionEntry");
}


void pdg::MiniZincPrinter::exportMznConstraints(std::ofstream &mzn, pdg::NodeRangesAndIds nodes)
{
  for(size_t i = 0; i < nodes.ordered.size(); i++)
  {
    auto node = nodes.ordered[i];
    if(node->getAnno() != "None")
    {
      mzn << "constraint :: \"TaintOnNodeIdx";
      mzn << i + 1;
      mzn << "\" taint[" << i + 1 << "]="; 
      mzn << node->getAnno() << ";\n";
    }
  }
}

void pdg::MiniZincPrinter::exportMzn(std::string filename, pdg::NodeRangesAndIds nodes, pdg::EdgeRangesAndIds edges, std::map<unsigned int, unsigned int> hasFn, size_t maxFuncParams)
{
  std::ofstream mzn;
  mzn.open(filename);

  exportMznNodes(mzn, nodes);
  exportMznEdges(mzn, edges);
  exportMznHasFn(mzn, nodes, hasFn);
  exportMznSrcDst(mzn, nodes, edges);
  exportMznParamIdx(mzn, nodes);
  exportMznUserAnnotated(mzn, nodes);
  mzn << "MaxFuncParams = " << maxFuncParams << ";\n";
  exportMznConstraints(mzn, nodes);
  mzn.close();
}

bool pdg::MiniZincPrinter::runOnModule(Module &M)
{
  auto PDG = &ProgramGraph::getInstance();

  auto nodesByType = nodesByNodeType(*PDG);
  auto edgesByType = edgesByEdgeType(*PDG);

  auto nodesByMzn = map_key_optional(std::function<std::optional<pdg::MznNodeType>(pdg::GraphNodeType)>(pdg::MiniZincPrinter::nodeMznType), nodesByType);   
  auto edgesByMzn = map_key_optional(std::function<std::optional<pdg::MznEdgeType>(pdg::EdgeType)>(pdg::MiniZincPrinter::edgeMznType), edgesByType);

  auto nodesById = toRangesAndIds(nodesByMzn, std::function<unsigned int(Node *)>([](Node *n) { return n->getNodeID(); }));
  auto edgesById = toRangesAndIds(edgesByMzn, std::function<unsigned int(Edge *)>([](Edge *n) { return n->getEdgeID(); }));

  auto functions = hasFn(*PDG); 
  auto maxParams = maxFnParams(*PDG);

  exportMzn("pdg_instance.mzn", nodesById, edgesById, functions, maxParams);

  return false;
}


static RegisterPass<pdg::MiniZincPrinter>
    ZINCPRINTER("minizinc",
               "Dump PDG data in minizinc format",
               false, false);