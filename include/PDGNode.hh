#ifndef NODE_H_
#define NODE_H_
#include "LLVMEssentials.hh"
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Metadata.h>
#include "PDGEdge.hh"
#include "PDGEnums.hh"
#include <set>
#include <iterator>


namespace pdg
{
  template <typename NodeTy>
  class EdgeIterator;

  class Edge;
  class Node
  {
  public:
    using EdgeSet = std::set<Edge *>;
    using iterator = EdgeIterator<Node>;
    using const_iterator = EdgeIterator<Node>;

    Node(GraphNodeType node_type)
    {
      _val = nullptr;
      _node_type = node_type;
      _is_visited = false;
      _func = nullptr;
      _node_di_type = nullptr;
      node_count++;
      node_ID = node_count;
      _annotation = "None";
      _line_number = -1;
      _col_number = -1;
      _file_name = "Not Found";
      _paramIdx = -1;
      _inst_index = -1;
    }

    Node(llvm::Value &v, GraphNodeType node_type)
    {
      _val = &v;
      if (auto inst = llvm::dyn_cast<llvm::Instruction>(&v))
        _func = inst->getFunction();
      else if (node_type == pdg::GraphNodeType::FUNC_ENTRY )
        _func = llvm::dyn_cast<llvm::Function>(&v);
      else
        _func = nullptr;
      _node_type = node_type;
      _is_visited = false;
      _node_di_type = nullptr;
      node_count++;
      node_ID = node_count;
      _annotation = "None";
      _paramIdx = -1;
      _line_number = -1;
      _col_number = -1;
      _inst_index = -1;

      if (llvm::isa<llvm::Instruction>(_val) )
      {
        llvm::Instruction *instruction = llvm::dyn_cast<llvm::Instruction>(_val);
        const llvm::DebugLoc &debugInfo = instruction->getDebugLoc();
        // llvm:: errs() << "debug: " << debugInfo << "\n";
        if(debugInfo) {
          std::string directory = debugInfo->getDirectory().str();
          std::string filePath = debugInfo->getFilename().str();
          _file_name = directory + "/" + filePath;
          _line_number = debugInfo->getLine();
          _col_number = debugInfo->getColumn();
          // llvm:: errs() << "line number: " << _line_number << "\n";
        }
      }
      else if (node_type == pdg::GraphNodeType::FUNC_ENTRY)
      {
          // llvm::Function *function = llvm::dyn_cast<llvm::Function>(_val);
          unsigned dbgKind = _func->getContext().getMDKindID("dbg");
          if (auto *debugInfo = _func->getSubprogram() ) 
          {
            // llvm::DebugLoc Loc(Dbg);
            // auto *scope = llvm::cast<llvm::DIScope>(Loc->getScope());
            std::string directory = debugInfo->getDirectory().str();
            std::string filePath = debugInfo->getFilename().str();
            // filename = Loc.getDirectory().str() + "/" + Loc.getFilename().str();
            _line_number = debugInfo->getLine();
            _file_name = directory + "/" + filePath;
          }
      }
      else if( node_type == pdg::GraphNodeType::VAR_STATICALLOCGLOBALSCOPE || 
          node_type == pdg::GraphNodeType::VAR_STATICALLOCMODULESCOPE || 
          node_type == pdg::GraphNodeType::VAR_STATICALLOCFUNCTIONSCOPE
        )
        {
          llvm::SmallVector< llvm::DIGlobalVariableExpression *,4 >  GVs;
          llvm::GlobalVariable *globalVar = llvm::dyn_cast<llvm::GlobalVariable>(_val);
          globalVar->getDebugInfo(GVs);
          llvm::DIGlobalVariableExpression* dbgExpr =  GVs[0];
          llvm::DIGlobalVariable* dbgGV =  dbgExpr->getVariable();
          auto scope = dbgGV->getScope();
          auto fnName = scope->getName();
          auto M = globalVar->getParent();
          for(auto& F : *M)
          {
            if(F.getName() == fnName) 
            {
              _func = &F; 
              break;
            }
          }

          std::string directory = dbgGV->getDirectory().str();
          std::string filePath = dbgGV->getFilename().str();
          _line_number = dbgGV->getLine();
          _file_name = directory + "/" + filePath;

        }
      else
      {
        _line_number = -1;
        _file_name = "Not Found";
      }
    }

    
    int getParamIdx() { return _paramIdx;}
    void setParamIdx(int new_idx) { _paramIdx = new_idx;}
    void setAnno(std::string new_anno) {  _annotation = new_anno; }
    std::string getAnno() { return _annotation; }
    unsigned int getNodeID()  { return node_ID;}
    void addInEdge(Edge &e) { _in_edge_set.insert(&e); }
    void addOutEdge(Edge &e) { _out_edge_set.insert(&e); }
    EdgeSet &getInEdgeSet() { return _in_edge_set; }
    EdgeSet &getOutEdgeSet() { return _out_edge_set; }
    void setNodeType(GraphNodeType node_type) { _node_type = node_type; }
    GraphNodeType getNodeType() const { return _node_type; }
    bool isVisited() { return _is_visited; }
    llvm::Function *getFunc() const { return _func; }
    void setFunc(llvm::Function &f) { _func = &f; }
    llvm::Value *getValue() { return _val; }
    llvm::DIType *getDIType() const { return _node_di_type; }
    void setDIType(llvm::DIType &di_type) { _node_di_type = &di_type; }
    void addNeighbor(Node &neighbor, EdgeType edge_type);
    EdgeSet::iterator begin() { return _out_edge_set.begin(); }
    EdgeSet::iterator end() { return _out_edge_set.end(); }
    EdgeSet::const_iterator begin() const { return _out_edge_set.begin(); }
    EdgeSet::const_iterator end() const { return _out_edge_set.end(); }
    std::set<Node *> getInNeighbors();
    std::set<Node *> getInNeighborsWithDepType(EdgeType edge_type);
    std::set<Node *> getOutNeighbors();
    std::set<Node *> getOutNeighborsWithDepType(EdgeType edge_type);
    bool hasInNeighborWithEdgeType(Node &n, EdgeType edge_type);
    bool hasOutNeighborWithEdgeType(Node &n, EdgeType edge_type);
    int getLineNumber() {return _line_number;};
    int getColumnNumber() {return _col_number;};
    int getInstructionIndex() {return _inst_index;};
    void setInstructionIndex(int index) {_inst_index = index;};
    std::string getFileName() {return _file_name;};
    virtual ~Node() = default;

  protected:
    llvm::Value *_val;
    llvm::Function *_func;
    bool _is_visited;
    EdgeSet _in_edge_set;
    EdgeSet _out_edge_set;
    GraphNodeType _node_type;
    llvm::DIType *_node_di_type;
    static unsigned int node_count;
    unsigned int node_ID;
    std::string _annotation;
    int _line_number;
    int _col_number;
    int _inst_index;
    std::string _file_name;
    int _paramIdx;
  };

  // used to iterate through all neighbors (used in dot pdg printer)
  template <typename NodeTy>
  class EdgeIterator : public std::iterator<std::input_iterator_tag, NodeTy>
  {
    typename Node::EdgeSet::iterator _edge_iter;
    typedef EdgeIterator<NodeTy> this_type;

  public:
    EdgeIterator(NodeTy *N) : _edge_iter(N->begin()) {}
    EdgeIterator(NodeTy *N, bool) : _edge_iter(N->end()) {}

    this_type &operator++()
    {
      _edge_iter++;
      return *this;
    }

    this_type operator++(int)
    {
      this_type old = *this;
      _edge_iter++;
      return old;
    }

    Node *operator*()
    {
      return (*_edge_iter)->getDstNode();
    }

    Node *operator->()
    {
      return operator*();
    }

    bool operator!=(const this_type &r) const
    {
      return _edge_iter != r._edge_iter;
    }

    bool operator==(const this_type &r) const
    {
      return !(operator!=(r));
    }

    EdgeType getEdgeType()
    {
      return (*_edge_iter)->getEdgeType();
    }
  };

} // namespace pdg
#endif