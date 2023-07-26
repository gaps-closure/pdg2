#ifndef ZINCPRINTER_H_
#define ZINCPRINTER_H_
#include "Graph.hh"
#include "ProgramDependencyGraph.hh"
#include "PDGEnums.hh"
#include "FunctionWrapper.hh"

#include <fstream>
#include <functional>
#include <optional>

namespace pdg
{

  /* ordering here matters, it is used to determine map ordering, and is relied on throughout export */
  enum MznNodeType {
    Inst_FunCall, 
    Inst_Ret,
    Inst_Br,
    Inst_Other,
    Inst,
    VarNode_StaticGlobal,
    VarNode_StaticModule,
    VarNode_StaticFunction,
    VarNode_StaticOther,
    VarNode,
    FunctionEntry, 
    Param_FormalIn,
    Param_FormalOut,
    Param_ActualIn,
    Param_ActualOut,
    Param,
    Annotation_Var,
    Annotation_Global,
    Annotation_Other,
    Annotation,
    PDGNode
  };

  enum MznEdgeType {
    ControlDep_CallInv,
    ControlDep_Indirect_CallInv,
    ControlDep_CallRet,
    ControlDep_Entry,
    ControlDep_Br,
    ControlDep_Other,
    ControlDep,
    DataDepEdge_DefUse,
    DataDepEdge_RAW,
    DataDepEdge_Ret,
    DataDepEdge_Alias,
    DataDepEdge,
    Parameter_In,
    Parameter_Out,
    Parameter_Field,
    Parameter,
    Anno_Global,
    Anno_Var,
    Anno_Other,
    Anno,
    PDGEdge
  };

  template<typename A, typename B>
  struct RangesAndIds {
    std::map<A, std::pair<size_t, size_t>> ranges;
    std::map<unsigned int, size_t> ids;
    std::vector<B> ordered;
  };

  typedef RangesAndIds<MznNodeType, Node *> NodeRangesAndIds;
  typedef RangesAndIds<MznEdgeType, Edge *> EdgeRangesAndIds;

  class MiniZincPrinter : public llvm::ModulePass
  { 
  private:
    template<typename A, typename B>
    static RangesAndIds<A, B> toRangesAndIds(std::map<A, std::vector<B>> groupedByA, std::function<unsigned int(B)> getId); 
    static std::map<unsigned int, unsigned int> hasFn(pdg::ProgramGraph &PDG);
    static std::optional<MznNodeType> nodeMznType(pdg::GraphNodeType nodeType);
    static std::optional<MznEdgeType> edgeMznType(pdg::EdgeType nodeType);
    static std::string mznNodeName(MznNodeType nodeType);
    static std::string mznEdgeName(MznEdgeType edgeType);
    static std::map<pdg::GraphNodeType, std::vector<Node *>> nodesByNodeType(pdg::ProgramGraph &PDG);
    static std::map<pdg::EdgeType, std::vector<Edge *>> edgesByEdgeType(pdg::ProgramGraph &PDG);
    static size_t maxFnParams(pdg::ProgramGraph &PDG);
    static std::map<unsigned int, bool> fnResultUsed(EdgeRangesAndIds ids);

    template<typename A>
    static std::optional<std::pair<int, int>> calculateCollatedRange(std::map<A, std::pair<size_t, size_t>> ranges, A start, A end);

    template<typename A>
    static void exportVector(std::ofstream &mzn, std::string name, std::vector<A> items, std::optional<std::string> asArray1dOf = std::nullopt);

    static void exportMznNodes(std::ofstream &mzn, NodeRangesAndIds nodes);
    static void exportMznEdges(std::ofstream &mzn, EdgeRangesAndIds edges);
    static void exportMznHasFn(std::ofstream &mzn, NodeRangesAndIds nodes, std::map<unsigned int, unsigned int> hasFn);
    static void exportMznSrcDst(std::ofstream &mzn, NodeRangesAndIds nodes, EdgeRangesAndIds edges);
    static void exportMznParamIdx(std::ofstream &mzn, NodeRangesAndIds nodes);
    static void exportMznUserAnnotated(std::ofstream &mzn, NodeRangesAndIds nodes);
    static void exportMznConstraints(std::ofstream &mzn, NodeRangesAndIds nodes);
    static void exportMzn(std::string filename, NodeRangesAndIds nodes, EdgeRangesAndIds edges, std::map<unsigned int, unsigned int> hasFn, size_t maxFnParams);
    static void exportDebug(std::string filename, NodeRangesAndIds nodes, EdgeRangesAndIds edges, std::map<unsigned int, unsigned int> hasFn);
    static void exportOneway(std::string filename, NodeRangesAndIds nodes, std::map<unsigned int, bool> fnResultUsed);
    static void exportFnArgs(std::string filename, NodeRangesAndIds nodes);
    static void exportLineNumbers(std::string filename, NodeRangesAndIds nodes);

  public:
    typedef std::unordered_map<llvm::Function *, FunctionWrapper *> FuncWrapperMap;
    static char ID;
    MiniZincPrinter() : llvm::ModulePass(ID) {};
    bool runOnModule(llvm::Module &M) override;
    void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
  };
}
#endif