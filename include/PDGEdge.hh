#ifndef PDGEDGE_H_
#define PDGEDGE_H_
#include "PDGNode.hh"
#include "PDGEnums.hh"

namespace pdg
{
  class Node;
  class Edge
  {
  private:
    EdgeType _edge_type;
    Node *_source;
    Node *_dst;
    static unsigned int edge_count;
    unsigned int edge_ID;

  public:
    Edge() = delete;
    Edge(Node *source, Node *dst, EdgeType edge_type)
    {
      _source = source;
      _dst = dst;
      _edge_type = edge_type;
      edge_count--;
      edge_ID = edge_count;
    }
    Edge(const Edge &e) // copy constructor
    {
      _source = e.getSrcNode();
      _dst = e.getDstNode();
      _edge_type = e.getEdgeType();
    }

    unsigned int getEdgeID()  { return edge_ID;}
    EdgeType getEdgeType() const { return _edge_type; }
    void setEdgeType(EdgeType newEdgeType)  { _edge_type = newEdgeType; }
    Node *getSrcNode() const { return _source; }
    Node *getDstNode() const { return _dst; }
    bool operator<(const Edge &e) const
    {
      return (_source == e.getSrcNode() && _dst == e.getDstNode() && _edge_type == e.getEdgeType());
    }
  };
} // namespace Edge

#endif