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

bool pdg::MiniZincPrinter::runOnModule(Module &M)
{

  auto _PDG = &ProgramGraph::getInstance();
  std::map<std::string, std::vector<std::string> > outputEnumsPDGNode;
  std::map<std::string, Node* > nodeID2Node;
  std::map<std::string, std::vector<std::string> > outputEnumsPDGEdge;
  std::map<std::string, Edge* > edgeID2Edge;
  std::map<std::string, std::vector<std::string> > outputArrays;

 // Need to save store nodes and edges so they are sorted correctly for minizinc 
  std::vector<std::vector<Edge*>> PDG_edges(19,std::vector<Edge*>()) ;
  std::vector<std::vector<Node*>> PDG_nodes(18,std::vector<Node*>());

  std::map<std::string,int> nodeID2enum;
  std::map<std::string,int> edgeID2enum;

  std::map<std::string,int> nodeID2index;


  std::vector<std::string> nodeOrder{
    "Inst_FunCall", 
    "Inst_Ret",
    "Inst_Br",
    "Inst_Other",
    "Inst", // 4
    "VarNode_StaticGlobal",
    "VarNode_StaticModule",
    "VarNode_StaticFunction",
    "VarNode_StaticOther",
    "VarNode", // 9
    "FunctionEntry", // 10
    "Param_FormalIn",
    "Param_FormalOut",
    "Param_ActualIn",
    "Param_ActualOut",
    "Param", // 15
    "Annotation_Var",
    "Annotation_Global",
    "Annotation_Other",
    "Annotation" // 19
  };

  std::vector<std::string> edgeOrder{
    "ControlDep_CallInv",
    "ControlDep_CallRet",
    "ControlDep_Entry",
    "ControlDep_Br",
    "ControlDep_Other",
    "ControlDep",
    "DataDepEdge_DefUse",
    "DataDepEdge_RAW",
    "DataDepEdge_Ret",
    "DataDepEdge_Alias",
    "DataDepEdge",
    "Parameter_In",
    "Parameter_Out",
    "Parameter_Field",
    "Parameter",
    "Anno_Global",
    "Anno_Var",
    "Anno_Other",
    "Anno"
  };

  std::vector<std::string> arrayOrder{
    "hasFunction",
    "hasSource",
    "hasDest",  
    // "hasParamIdx",
    // "invForRet"
  };

  std::map<std::string,bool> oneWayCheck;

  std::vector<std::string> hasParamIdx;

  // initialize output
  outputEnumsPDGNode["Inst_FunCall"] = std::vector<std::string>();
  outputEnumsPDGNode["Inst_Ret"] = std::vector<std::string>();
  outputEnumsPDGNode["Inst_Br"] = std::vector<std::string>();
  outputEnumsPDGNode["Inst_Other"] = std::vector<std::string>();
  outputEnumsPDGNode["VarNode_StaticGlobal"] = std::vector<std::string>();
  outputEnumsPDGNode["VarNode_StaticModule"] = std::vector<std::string>();
  outputEnumsPDGNode["VarNode_StaticFunction"] = std::vector<std::string>();
  outputEnumsPDGNode["VarNode_StaticOther"] = std::vector<std::string>();
  outputEnumsPDGNode["FunctionEntry"] = std::vector<std::string>();
  outputEnumsPDGNode["Param_FormalIn"] = std::vector<std::string>();
  outputEnumsPDGNode["Param_FormalOut"] = std::vector<std::string>();
  outputEnumsPDGNode["Param_ActualIn"] = std::vector<std::string>();
  outputEnumsPDGNode["Param_ActualOut"] = std::vector<std::string>();
  outputEnumsPDGNode["Annotation_Var"] = std::vector<std::string>();
  outputEnumsPDGNode["Annotation_Global"] = std::vector<std::string>();
  outputEnumsPDGNode["Annotation_Other"] = std::vector<std::string>();

  outputEnumsPDGEdge["ControlDep_CallInv"] = std::vector<std::string>();
  outputEnumsPDGEdge["ControlDep_Entry"] = std::vector<std::string>();
  outputEnumsPDGEdge["ControlDep_Br"] = std::vector<std::string>();
  outputEnumsPDGEdge["ControlDep_CallRet"] = std::vector<std::string>();
  outputEnumsPDGEdge["DataDepEdge_DefUse"] = std::vector<std::string>();
  outputEnumsPDGEdge["DataDepEdge_RAW"] = std::vector<std::string>();
  outputEnumsPDGEdge["DataDepEdge_Alias"] = std::vector<std::string>();
  outputEnumsPDGEdge["DataDepEdge_Ret"] = std::vector<std::string>();
  outputEnumsPDGEdge["Parameter_In"] = std::vector<std::string>();
  outputEnumsPDGEdge["Parameter_Out"] = std::vector<std::string>();
  outputEnumsPDGEdge["Parameter_Field"] = std::vector<std::string>();
  outputEnumsPDGEdge["Anno_Global"] = std::vector<std::string>();
  outputEnumsPDGEdge["Anno_Var"] = std::vector<std::string>();
  outputEnumsPDGEdge["Anno_Other"] = std::vector<std::string>();
  outputEnumsPDGEdge["ControlDep_Other"] = std::vector<std::string>();



  // _PDG->build(M);

  std::ofstream outFile;
  std::ofstream dbgFile;
  std::ofstream node2line;
  std::ofstream funFile;
  std::ofstream onewayFile;

  const char *delim = ",";
  const char *term = "\n";
  const char *quote = "\'";

  outFile.open ("pdg_instance.mzn");
  dbgFile.open ("pdg_data.csv");
  node2line.open ("node2lineNumber.txt");
  funFile.open ("functionArgs.txt");
  onewayFile.open ("oneway.txt");
  auto begin = std::chrono::high_resolution_clock::now();
  

  for (auto node_iter = _PDG->begin(); node_iter != _PDG->end(); ++node_iter)
  {
    auto node = *node_iter;
    // we don't care about this type
    if (node->getNodeType() == pdg::GraphNodeType::FUNC)
    {
      continue;
    }

    if (node->getNodeType() == pdg::GraphNodeType::INST_FUNCALL)
    {
      llvm::CallInst* inst = dyn_cast<llvm::CallInst>(node->getValue());
      
      Function *fn = inst->getCalledFunction();
      // need to check why this could be null for an actuall callInst
      if (fn != nullptr)
      {
        StringRef fn_name = fn->getName();
        // skip LLVM intrinsics
        if (fn_name.contains("llvm."))
        {
          continue;
        }
      }
      else
      {
        if (DEBUGZINC)
        {
          errs() << "Null Called Function: " << *inst << "\n";
        }
      }
    }

    for (auto out_edge : node->getOutEdgeSet())
    {
      
      // don't care about these yet
      if (out_edge->getEdgeType() == pdg::EdgeType::IND_CALL || out_edge->getEdgeType() == pdg::EdgeType::GLOBAL_DEP || out_edge->getEdgeType() == pdg::EdgeType::TYPE_OTHEREDGE)
      {
          continue;
      }
      
      Edge* edge = out_edge;
      PDG_edges[static_cast<int>(out_edge->getEdgeType())].push_back(edge);
    }
    PDG_nodes[static_cast<int>(node->getNodeType())].push_back(node);
  }

  
  auto end = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "199@ Time: " << elapsed.count() << "\n";

  //print edges
  // outFile << "=============== Edge Set ===============\n";
  
  begin = std::chrono::high_resolution_clock::now();
  int edge_index = 1;
  for (auto& enums: PDG_edges)
  {
    for (auto& edges: enums)
    {

      Edge* out_edge = edges;
      std::string edge_id;
      std::string src_id;
      std::string dest_id;
      edge_id = "";
      src_id = "";
      dest_id = "";
      raw_string_ostream getEdgeID(edge_id);
      raw_string_ostream getSrcID(src_id);
      raw_string_ostream getDstID(dest_id);
      getSrcID << out_edge->getSrcNode()->getNodeID();
      getDstID << out_edge->getDstNode()->getNodeID();

      
      // if (std::find(PDG_nodes[static_cast<int>(out_edge->getSrcNode()->getNodeType())].begin(), PDG_nodes[static_cast<int>(out_edge->getSrcNode()->getNodeType())].end(), out_edge->getSrcNode()) == PDG_nodes[static_cast<int>(out_edge->getSrcNode()->getNodeType())].end())
      // {

      //     errs() << "Warning, edges source idx became zero! \n"; 
      //     errs() << "Source ID: " << getSrcID.str() << "Source Node Type: " << pdgutils::getNodeTypeStr(out_edge->getSrcNode()->getNodeType())  <<"\n";
      //     errs() << "Dest ID: " << getDstID.str() << "Dest Node Type: " << pdgutils::getNodeTypeStr(out_edge->getDstNode()->getNodeType()) << "\n";
        
      //   PDG_nodes[static_cast<int>(out_edge->getSrcNode()->getNodeType())].push_back(out_edge->getSrcNode());
       
      //     errs() << "Node: " <<  getSrcID.str() << " added. \n";
        
      // }

      // if (std::find(PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].begin(), PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].end(), out_edge->getDstNode()) == PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].end())
      // {

      //     errs() << "Warning, edge Dest idx became zero! \n"; 
      //     errs() << "Source ID: " << getSrcID.str() << " Source Node Type: " << pdgutils::getNodeTypeStr(out_edge->getSrcNode()->getNodeType())  <<"\n";
      //     errs() << "Dest ID: " << getDstID.str() << " Dest Node Type: " << pdgutils::getNodeTypeStr(out_edge->getDstNode()->getNodeType()) << "\n";
         
      //     errs() << "Node: " <<  getDstID.str() << " added. \n";
        
      //   PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].push_back(out_edge->getDstNode());
      // }

      if (out_edge->getDstNode()->getNodeType() == pdg::GraphNodeType::INST_FUNCALL ||
          out_edge->getDstNode()->getNodeType() == pdg::GraphNodeType::PARAM_ACTUALIN ||
          out_edge->getDstNode()->getNodeType() == pdg::GraphNodeType::PARAM_ACTUALOUT 
          )
          {
            if (std::find(PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].begin(), PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].end(), out_edge->getDstNode()) == PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].end())
            {

                // errs() << "Warning, edge Dest idx became zero! \n"; 
                // errs() << "Source ID: " << getSrcID.str() << " Source Node Type: " << pdgutils::getNodeTypeStr(out_edge->getSrcNode()->getNodeType())  <<"\n";
                // errs() << "Dest ID: " << getDstID.str() << " Dest Node Type: " << pdgutils::getNodeTypeStr(out_edge->getDstNode()->getNodeType()) << "\n";
              
                // errs() << "Node: " <<  getDstID.str() << " added. \n";
              
              PDG_nodes[static_cast<int>(out_edge->getDstNode()->getNodeType())].push_back(out_edge->getDstNode());
            }
          }



      if (out_edge->getSrcNode()->getNodeType() == pdg::GraphNodeType::INST_FUNCALL)
      {
        llvm::CallInst* inst = dyn_cast<llvm::CallInst>(out_edge->getSrcNode()->getValue());
        Function *fn = inst->getCalledFunction();
        if (fn != nullptr)
        {
          StringRef fn_name = fn->getName();
          // skip LLVM intrinsics
          if (fn_name.contains("llvm."))
          {
            continue;
          }
        }
        else
        {
          if (DEBUGZINC)
          {
            errs() << "Null Called Function: " << *inst << "\n";
          }
        }
        }
      

      if (out_edge->getDstNode()->getNodeType() == pdg::GraphNodeType::INST_FUNCALL)
      {
        llvm::CallInst* inst = dyn_cast<llvm::CallInst>(out_edge->getDstNode()->getValue());
        Function *fn = inst->getCalledFunction();
        if (fn != nullptr)
        {
          StringRef fn_name = fn->getName();
          // skip LLVM intrinsics
          if (fn_name.contains("llvm."))
          {
            continue;
          }
        }
        else
        {
          if (DEBUGZINC)
          {
              errs() << "Null Called Function: " << *inst << "\n";
          }
        }
      }

      if (out_edge->getSrcNode()->getNodeType() == pdg::GraphNodeType::ANNO_VAR || 
          out_edge->getDstNode()->getNodeType() == pdg::GraphNodeType::ANNO_VAR 
      )
      {
        out_edge->setEdgeType(pdg::EdgeType::ANNO_VAR);
      }



      if (DEBUGZINC)
      {
        errs() << "edge: " << out_edge->getEdgeID() << " / " << "src[" << out_edge->getSrcNode()->getNodeID() << "]" << "(" << nodeID2enum[getSrcID.str()] << ")" <<  "/ " << " dst[" << out_edge->getDstNode()->getNodeID() << "]" << "(" << nodeID2enum[getDstID.str()] << ")" << " / " << pdgutils::getEdgeTypeStr(out_edge->getEdgeType()) << "\n";
        errs() << "Label Src: " << out_edge->getSrcNode()->getAnno() << " Label Dest: " << out_edge->getDstNode()->getAnno() << "\n";
      }
      getEdgeID << out_edge->getEdgeID();
      outputEnumsPDGEdge[pdgutils::getEdgeTypeStr(out_edge->getEdgeType())].push_back(getEdgeID.str());
      edgeID2enum[getEdgeID.str()] = edge_index;
      edgeID2Edge[getEdgeID.str()] = out_edge;
      

      edge_index++;


      if(out_edge->getEdgeType() == EdgeType::CONTROLDEP_CALLRET)
      {
          for (auto edge : out_edge->getDstNode()->getOutEdgeSet())
          {
            if (edge->getEdgeType() == EdgeType::CONTROLDEP_CALLINV)
            {
              edge_id = "";
              getEdgeID << edge->getEdgeID();
              // outputArrays["invForRet"].push_back(getEdgeID.str());
              break;
            }
          }
        
      }
      
      
      
    }
  }

  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "361@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();

  int node_index = 1;
  auto outputArrayHasFunction = &outputArrays["hasFunction"];
  for (auto& enums: PDG_nodes)
  {
    for (auto& nodes: enums)
    {
      Node* node = nodes; 
      std::string str;
    raw_string_ostream OS(str);
    std::string node_id = "";
    raw_string_ostream getNodeID(node_id);

    if (DEBUGZINC)
    {
        if (node->getValue() != nullptr)
        errs() << "node: " << node->getNodeID() << " annotation:" << node->getAnno() <<  " - " << pdgutils::rtrim(OS.str()) << " - " << pdgutils::getNodeTypeStr(node->getNodeType()) << "\n" ;
        else
        errs() << "node: " << node->getNodeID() << " annotation:" << node->getAnno() << " - " <<  pdgutils::getNodeTypeStr(node->getNodeType()) << "\n" ;

        if( node->getNodeType() == pdg::GraphNodeType::FUNC_ENTRY)
          errs() << "Value: " << *(node->getFunc()) << "Line Number: " << node->getLineNumber() << "\n";


    }
   
    if( node->getNodeType() == pdg::GraphNodeType::FUNC_ENTRY)
    {
      int numArgs = 0;
      for (auto arg_iter = node->getFunc()->arg_begin(); arg_iter != node->getFunc()->arg_end(); arg_iter++)
      {
        numArgs++;
      }
      if (node->getAnno() != "None")
        funFile << node->getAnno() << " " << numArgs << "\n";
    }
    

    Value* val = node->getValue();

    
    Node* func_node = nullptr;
    if (node->getFunc())
    {
      // FuncWrapperMap Fwm = _PDG->getFuncWrapperMap();
      func_node = _PDG->getFuncWrapperMap()[node->getFunc()]->getEntryNode();
    }
   
    
    if (func_node != nullptr)
    {
      getNodeID << func_node->getNodeID();
      // if (DEBUGZINC)
      // {
      //   errs() << "Adding Function" << *val << "\n";
      // }
      outputArrayHasFunction->push_back(getNodeID.str()); 
    }
    else
    {
      // needs to be assigned to something or minizinc will complain
      outputArrayHasFunction->push_back("0"); 
    }
    

    // if (val != nullptr)
    // {
    //   if (Function* f = dyn_cast<Function>(val))
    //     OS << f->getName().str() << "\n";
    //   else
    //     OS << *val << "\n";
    // }


    node_id = "";
    getNodeID << node->getNodeID();
    
    outputEnumsPDGNode[pdgutils::getNodeTypeStr(node->getNodeType())].push_back(getNodeID.str());
    nodeID2Node[getNodeID.str()] = node;
    nodeID2enum[getNodeID.str()] = node_index;
    node_index++;
    }

  }

  int index = 1;

  // outFile << "PDGNodes" << " = [ ";
 
  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "456@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  bool first = true;
  for(auto &id : nodeOrder)
  {
    for(auto const& i : outputEnumsPDGNode[id]) {
      // if (first)
      //   outFile <<  i ;
      // else 
      //   outFile << ", " <<  i ;
      first = false;
      nodeID2index[i] = index;
      index++;
    } 
  }
  // outFile << "];   \n ";

  index = 1;
  int class_idx = 0;
  int super_class_start = 1;
  int super_class_end = -1;
  int max_node_idx = 1;
  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "481@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();

  for(auto &id : nodeOrder)
  {
    if (class_idx == 4 || class_idx == 9 || class_idx == 15 || class_idx == 19)
    {
      outFile  << id << "_start" << " = " << super_class_start << ";\n";
      outFile  << id << "_end" << " = " << super_class_end << ";\n";
      super_class_start = super_class_end + 1;
      super_class_end = -1;
      class_idx++;
      continue;
    }
    // Need to subtract 1 because minizinc is inclusive of right index
    int endIdx = index + outputEnumsPDGNode[id].size() -1;
    if (endIdx < index)
    {
      outFile << id << "_start" << " = 0;\n";
      outFile << id << "_end" << " = -1;\n";
    }
    else
    {
      outFile << id << "_start" << " = " << index << ";\n";
      outFile << id << "_end" << " = " << endIdx << ";\n";
      // Need to progress to next set
      super_class_end = endIdx;
      max_node_idx = endIdx;
      index = endIdx + 1;
      // fix for function entry since it does not have any subclasses
      if (class_idx == 10)
      {
        super_class_start = super_class_end+1;
      }
      
    }
    class_idx++;
  }

  outFile << "PDGNode_start = 1;\n";
  outFile << "PDGNode_end" << " = " << max_node_idx << ";\n";
  std::vector<bool> hasFuncAnno;

  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "527@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  index = 1;
  for(auto &id : nodeOrder)
  {
    for(auto const& i : outputEnumsPDGNode[id]) {
      std::string valueStr = "No Value";
      std::string nameStr = "None";

      if (nodeID2Node[i]->getValue() != nullptr)
      {
        nameStr  = nodeID2Node[i]->getValue()->getName().str();
      }

      if(nodeID2Node[i]->getNodeType() == pdg::GraphNodeType::FUNC_ENTRY)
      {
        if(nodeID2Node[i]->getAnno() != "None")
        {
          hasFuncAnno.push_back(true);
        }
        else
        {
          hasFuncAnno.push_back(false);
        }

        oneWayCheck[i] = false;
      }

      if(nodeID2Node[i]->getNodeType() == pdg::GraphNodeType::PARAM_FORMALIN || 
         nodeID2Node[i]->getNodeType() == pdg::GraphNodeType::PARAM_FORMALOUT ||
         nodeID2Node[i]->getNodeType() == pdg::GraphNodeType::PARAM_ACTUALIN ||
         nodeID2Node[i]->getNodeType() == pdg::GraphNodeType::PARAM_ACTUALOUT )
      {
        if(nodeID2Node[i]->getParamIdx() >= 0)
        {
          hasParamIdx.push_back(std::to_string(nodeID2Node[i]->getParamIdx()+1));
        }
        else
        {
          hasParamIdx.push_back(std::to_string(nodeID2Node[i]->getParamIdx()));
        }
      }

      
      std::string filename = nodeID2Node[i]->getFileName();
      int lineNumber = nodeID2Node[i]->getLineNumber();
      int colNumber = nodeID2Node[i]->getColumnNumber();
      int instIndex = nodeID2Node[i]->getInstructionIndex();
      if (DEBUGZINC)
      {
        errs() << "hasFunction ID" << outputArrays["hasFunction"][index-1] << "Value: " <<valueStr << "\n";
      }
      if (outputArrays["hasFunction"][index-1] != "0")
      {
        if (filename == "" || filename == "Not Found")
        {
          filename = nodeID2Node[outputArrays["hasFunction"][index-1]]->getFileName();
        }
        if (lineNumber == -1)
        {
          lineNumber = nodeID2Node[outputArrays["hasFunction"][index-1]]->getLineNumber();
        }
      }

      
    
      if (nodeID2Node[i]->getValue() != nullptr)
      {
        valueStr = "";
        llvm::raw_string_ostream(valueStr) << *(nodeID2Node[i]->getValue());
        // escape quotes in the string
        std::string::size_type sz = 0;
        if(nodeID2Node[i]->getNodeType() == pdg::GraphNodeType::FUNC_ENTRY)
        {
          while ( ( sz = valueStr.find("\"", sz) ) != std::string::npos )
          {
              valueStr.replace(sz, 1, "\"\"");
              sz += 2;
          }
        }
      }

      if (nodeID2Node[i]->getValue() != nullptr) 
      {
        auto val = nodeID2Node[i]->getValue();

      }
       
      dbgFile 
        << "Node" << delim 
        << index << delim 
        << id << delim 
        << i << delim 
        << quote << valueStr << quote << delim
        << nodeID2index[outputArrays["hasFunction"][index-1]] << delim 
        << delim 
        << delim
        << filename << delim 
        << lineNumber << delim 
        << colNumber << delim
        << instIndex << delim
        << nodeID2Node[i]->getParamIdx() 
        << term;
      node2line << index << ", " << nameStr << ", " << filename  << ", " << lineNumber << "\n";
      index++;
    } 
  }
  
  index = 1;
  class_idx = 0;
  super_class_start = 1;
  super_class_end = -1;
  int max_edge_idx = 1; 
  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "619@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  for(auto &id : edgeOrder)
  {
    if (class_idx == 5 || class_idx == 10 || class_idx == 14 || class_idx == 18)
    {
      outFile << id << "_start" << " = " << super_class_start << ";\n";
      outFile << id << "_end" << " = " << super_class_end << ";\n";
      super_class_start = super_class_end + 1;
      super_class_end = -1;
      class_idx++;
      continue;
    }
    int endIdx = index + outputEnumsPDGEdge[id].size() -1;
    
    if (endIdx < index)
    {
      outFile << id << "_start" << " = 0;\n";
      outFile << id << "_end" << " = -1;\n";
    }
    else
    {
      outFile << id << "_start" << " = " << index << ";\n";
      outFile << id << "_end" << " = " << endIdx << ";\n";
      max_edge_idx = endIdx;
      super_class_end = endIdx;
      index = endIdx + 1;
    }
    class_idx++;
  }
  outFile << "PDGEdge_start = 1;\n";
  outFile << "PDGEdge_end" << " = " << max_edge_idx << ";\n";

  index = 1;
  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "656@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  for(auto &id : edgeOrder)
  {
     for(auto const &i : outputEnumsPDGEdge[id] )
     {
        Edge* edg = edgeID2Edge[i];
        std::string src_id;
        std::string dest_id;
        src_id = "";
        dest_id = "";
        raw_string_ostream getSrcID(src_id);
        raw_string_ostream getDstID(dest_id);
        getSrcID << edg->getSrcNode()->getNodeID();
        getDstID << edg->getDstNode()->getNodeID();

        if (edg->getEdgeType() == EdgeType::CONTROLDEP_CALLINV)
        {
          bool hasUse = edg->getSrcNode()->getValue()->user_empty();
          if (oneWayCheck[getSrcID.str()] == false)
          {
            oneWayCheck[getSrcID.str()] = hasUse;
          } 
        
        }

        if (nodeID2index[getSrcID.str()] == 0)
        {
          if (DEBUGZINC)
          {
              errs() << "Error! Source Edge ID is 0! \n";

              errs() << "Source ID: " << getSrcID.str() << " Source Node Type: " << pdgutils::getNodeTypeStr(edg->getSrcNode()->getNodeType())  <<"\n";
              errs() << "Dest ID: " << getDstID.str() << " Dest Node Type: " << pdgutils::getNodeTypeStr(edg->getDstNode()->getNodeType()) << "\n";
          }
          return false;
        }

        if (nodeID2index[getDstID.str()] == 0)
        {
          if (DEBUGZINC)
          {
            errs() << "Error! Dest Edge ID is 0! \n";

            errs() << "Source ID: " << getSrcID.str() << " Source Node Type: " << pdgutils::getNodeTypeStr(edg->getSrcNode()->getNodeType())  <<"\n";
            errs() << "Dest ID: " << getDstID.str() << " Dest Node Type: " << pdgutils::getNodeTypeStr(edg->getDstNode()->getNodeType()) << "\n";
          }
          return false;
        } 

        
        
        outputArrays["hasSource"].push_back(std::to_string(nodeID2index[getSrcID.str()]));
        outputArrays["hasDest"].push_back(std::to_string(nodeID2index[getDstID.str()]));
     }
  }


  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "717@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  index = 1;
//   if (DEBUGZINC)
//  {
  for(auto &id : edgeOrder)
  {
    for(auto const& i : outputEnumsPDGEdge[id]) {
      dbgFile 
        << "Edge" << delim 
        << index << delim
        << id << delim 
        << i << delim 
        << delim 
        << delim
        << outputArrays["hasSource"][index-1] << delim 
        << outputArrays["hasDest"][index-1] << delim 
        << delim 
        << delim 
        << delim 
        << delim
        << term;
      index++;
    } 
  }
//  }

 end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "734@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();

 std::vector<std::string> hasFunctionIndx;
 for(auto &i : outputArrays["hasFunction"])
 {
   
    hasFunctionIndx.push_back(std::to_string(nodeID2index[i]));
   
 }
 outputArrays["hasFunction"] = hasFunctionIndx;

end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "749@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  std::string out_string = "";
  for(auto &id : arrayOrder)
  {
    bool first = true;
    int row_count = 0;
    outFile << id << " = [";
    for(auto const& i : outputArrays[id]) {
      if(row_count % 20 == 0)
      {
        out_string += "\n";
      }
      out_string += i + ",";
      
      row_count++;
    }
    out_string.pop_back();
    outFile << out_string << "\n];\n";
    out_string = "";
  }

  first = true;
  int last_id = -1;
  
  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "777@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  out_string = "hasParamIdx = array1d(Param, [\n";
  for(auto const& i : hasParamIdx) {
    out_string += i + ",";

    first = false;
    last_id = std::stoi(i);
  }
    out_string.pop_back();
    out_string += "\n]);\n";
    outFile << out_string;

  //  std::ofstream outFileCle;
  // outFileCle.open ("init_cle.mzn");

  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "793@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  out_string = " ";
  outFile << "userAnnotatedFunction = array1d(FunctionEntry, [\n"; 
  for(auto b: hasFuncAnno)
  {
    if(b)
    {
      out_string+="true,";
    }
    else
    {
      out_string+="false,";
    }
  }
  out_string.pop_back();
  outFile << out_string << "\n]);\n";

end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "817@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  int maxParam = 3;
  for (auto node_iter = _PDG->begin(); node_iter != _PDG->end(); ++node_iter)
  {
    auto node = *node_iter;
    if (node->getNodeType() != pdg::GraphNodeType::FUNC_ENTRY)
    {
      continue;
    }
    llvm::Function* f = node->getFunc();
    int numArgs = 0;
    for (auto arg_iter = f->arg_begin(); arg_iter != f->arg_end(); arg_iter++)
    {
      numArgs++;
    }
    if(numArgs > maxParam)
    {
      if (DEBUGZINC)
      {
        errs() << *f << "\n";
      }
      maxParam = numArgs;
    }
  }

  outFile << "MaxFuncParms = " <<  maxParam << ";\n";
  
end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "848@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  // onewayFile << "OneWayCheck \n";
  for(auto &i : oneWayCheck)
  {
    if(nodeID2Node[i.first]->getNodeType() == pdg::GraphNodeType::FUNC_ENTRY && nodeID2Node[i.first]->getAnno() != "None")
      onewayFile << nodeID2Node[i.first]->getAnno() << " " << oneWayCheck[i.first] << "\n";
  }
  
  end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "860@ Time: " << elapsed.count() << "\n";
  begin = std::chrono::high_resolution_clock::now();
  for(auto &id : nodeOrder)
  {
    for(auto const& i : outputEnumsPDGNode[id]) {
      Node* node = nodeID2Node[i];
      if(node->getAnno() != "None")
      {
        outFile <<  "constraint ::  \"TaintOnNodeIdx" << nodeID2index[i]  << "\" taint[" << nodeID2index[i] << "]=" << node->getAnno() << ";\n"; 
      }
    } 
  }
end = std::chrono::high_resolution_clock::now();
  elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - begin);

  errs() << "875@ Time: " << elapsed.count() << "\n";



  
  // _PDG->dumpNodeLineNumbers();

  // outFileCle.close();
  outFile.close();
  dbgFile.close();
  node2line.close();
  funFile.close();
  onewayFile.close();
}


static RegisterPass<pdg::MiniZincPrinter>
    ZINCPRINTER("minizinc",
               "Dump PDG data in minizinc format",
               false, false);