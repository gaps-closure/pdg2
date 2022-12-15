from pathlib import Path
from neo4j import GraphDatabase
from dataclasses import dataclass
import sys
import csv
import re
from typing import Iterable, Optional  

csv.field_size_limit(sys.maxsize)

@dataclass
class Node: 
    id: int
    type: str
    ir: str
    has_function: int
    source: str
    line: Optional[int] 
    col: Optional[int]
    param_idx: Optional[int]

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "type": self.type,
            "ir": self.ir,
            "has_function": self.has_function,
            "source": self.source,
            "line": self.line,
            "col": self.col,
            "param_idx": self.param_idx 
        }

@dataclass
class Edge: 
    id: int 
    type: str 
    source_node_id: int
    destination_node_id: int 

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "type": self.type,
            "source_node_id": self.source_node_id,
            "destination_node_id": self.destination_node_id,
        }

with open('pdg_data_unspaced.csv') as pdg_csv_f:
    pdg_csv = list(csv.reader(
        pdg_csv_f, quotechar='"', skipinitialspace=False))

def read_mzn():
    lines = Path('pdg_instance.mzn').read_text().split("\n")
    param_idxs = map(int, \
        next((lines[i + 1] for i, line in enumerate(lines) if line.startswith('hasParamIdx'))).strip().split(','))
    param_start = int(re.match("Param_start = (\d+);", next((line for line in lines if line.startswith('Param_start')))).group(1))
    param_end = int(re.match("Param_end = (\d+);", next((line for line in lines if line.startswith('Param_end')))).group(1))
    return param_start, param_end, list(param_idxs)


nodes = list((Node(int(id), type, "".join(ir), int(has_function), source, int(line) if line != -1 else None, int(col) if col != -1 else None, None) for enum, id, type, _, *ir, has_function, _, _, source, line, col in pdg_csv if enum == "Node"))
edges = (Edge(int(id), type, int(source_node_id), int(dest_node_id)) for enum, id, type, _, _, _, source_node_id, dest_node_id, *_ in pdg_csv if enum == "Edge")

param_start, param_end, param_idxs = read_mzn()
for node in nodes:
    if param_start <= node.id <= param_end:
        idx = param_idxs[node.id - param_start] 
        node.param_idx = idx if idx != -1 else None  

def create_nodes(tx, nodes: Iterable[Node]) -> None:
    tx.run("UNWIND $nodes as map CREATE (n:Node) SET n = map", nodes = list(map(lambda x: x.to_dict(), nodes)))

def create_edges(tx, edges: Iterable[Edge]) -> None:
    tx.run(
        "UNWIND $edges as map MATCH (n:Node), (m:Node) WHERE n.id = map.source_node_id AND m.id = map.destination_node_id CREATE (n)-[:EDGE { id: map.id, type: map.type }]->(m)",
        edges = list(map(lambda x: x.to_dict(), edges))
    )


uri = "neo4j://localhost:7687"
driver = GraphDatabase.driver(uri, auth=("neo4j", "password"))

with driver.session(database="neo4j") as session:
    print("Clearing database...")
    session.run("MATCH (n) DETACH DELETE n")
    print("Creating nodes...")
    session.execute_write(create_nodes, nodes)
    print("Creating node index...")
    session.run("CREATE INDEX node_index IF NOT EXISTS FOR (n:Node) ON (n.id)")
    print("Creating edges...")
    session.execute_write(create_edges, edges)
    print("Finished")
driver.close()
