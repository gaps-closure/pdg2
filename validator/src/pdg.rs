use std::{collections::HashMap, fmt::Display};

use llvm_ir::Name;
use regex::Regex;

use crate::llvm::LLID;

#[derive(Debug,Clone,Hash)]
pub struct Node {
    pub id: u64, 
    pub r#type: String, 
    pub ir: String,
    pub source: String, 
    pub has_fn: u64, 
    pub line: Option<u64>, 
    pub col: Option<u64>,
    pub inst_index: Option<u64>,
    pub param_idx: Option<u64>,
    llid: Option<Option<LLID>>,
}

impl PartialEq for Node {
    fn eq(&self, other: &Node) -> bool {
        self.id == other.id
    }
}
impl Eq for Node {} 

impl Display for Node {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
       f.write_fmt(format_args!("{}", self.id))
    }
}

#[derive(Debug,Clone,Hash)]
pub struct Edge {
    pub id: u64, 
    pub r#type: String, 
    pub source_node_id: u64, 
    pub destination_node_id: u64 
}
impl PartialEq for Edge {
    fn eq(&self, other: &Edge) -> bool {
        self.id == other.id
    }
}
impl Eq for Edge {} 

impl Display for Edge {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
       f.write_fmt(format_args!("{}", self.id))
    }
}


#[derive(Debug)]
pub struct Pdg {
    pub nodes: Vec<Node>,
    pub edges: Vec<Edge>,
}

impl Node {
    pub fn from_string_record(rec: &csv::StringRecord) -> Result<Self, String> {
        let id = 
            u64::from_str_radix(rec.get(1).ok_or("Could not get node id")?, 10)
                .map_err(|_e| "Could not parse node id")?;
        let r#type = rec.get(2).ok_or("Could not get node type")?.to_string();
        let ir = rec.get(4).ok_or("Could not get node ir")?.to_string();
        let has_fn = u64::from_str_radix(rec.get(5).ok_or("Could not get node source")?, 10).map_err(|_e| "Could not parse has fn")?;
        let source = rec.get(8).ok_or("Could not get node source")?.to_string();
        let line = u64::from_str_radix(rec.get(9).ok_or("Could not get node source")?, 10).ok();
        let col = u64::from_str_radix(rec.get(10).ok_or("Could not get node source")?, 10).ok();
        let inst_index = u64::from_str_radix(rec.get(11).ok_or("Could not get node source")?, 10).ok();
        let param_idx = u64::from_str_radix(rec.get(12).ok_or("Could not get node source")?, 10).ok();
        Ok(Node {
            id,
            r#type,
            ir,
            has_fn,
            source,
            line,
            col,
            inst_index,
            param_idx,
            llid: Default::default(),
        })
    }
    pub fn in_ir(&self, name: &str) -> bool {
        self.ir.contains(name)
    } 
    pub fn ir_name(&self) -> Option<String> {
        let reg = Regex::new(r"@((\w|_|\.)+)").unwrap();
        let op = reg.captures_iter(&self.ir).next();
        op.map(|cap| cap[1].to_string())
    }
}

impl Edge {
    pub fn from_string_record(rec: &csv::StringRecord) -> Result<Self, String> {
        let id = 
            u64::from_str_radix(rec.get(1).ok_or("Could not get edge id")?, 10)
                .map_err(|_e| "Could not parse edge id")?;
        let r#type = rec.get(2).ok_or("Could not get edge type")?.to_string();
        let source_node_id = 
            u64::from_str_radix(rec.get(6).ok_or("Could not get edge source id")?, 10)
                .map_err(|_e| "Could not parse edge source id")?;
        let destination_node_id  = 
            u64::from_str_radix(rec.get(7).ok_or("Could not get edge source id")?, 10)
                .map_err(|_e| "Could not parse edge source id")?;
        
        Ok(Edge {
            id,
            r#type,
            source_node_id,
            destination_node_id
        })
    }
}


impl Pdg {
    pub fn from_csv_reader<R: std::io::Read>(mut rdr: csv::Reader<R>) -> Result<Pdg, String>  {
        let mut nodes = Vec::new();
        let mut edges = Vec::new();
        for result in rdr.records() {
            let rec = result.map_err(|e| format!("Could not read csv: {}", e))?; 
            let s = rec.get(0).ok_or("Could not get field")?;
            match s {
                "Node" => nodes.push(Node::from_string_record(&rec)?), 
                "Edge" => edges.push(Edge::from_string_record(&rec)?),
                _ => return Err("Neither Node or Edge".to_string())
            }
        }
        Ok(Pdg { nodes, edges })
    }
    pub fn from_edge(&self, e: &Edge) -> (&Node, &Node) {
        (&self.nodes[(e.source_node_id - 1) as usize], &self.nodes[(e.destination_node_id - 1) as usize]) 
    }
    pub fn nodes_of_type(&self, r#type: &'static str) -> impl Iterator<Item = &Node> {
        self
            .nodes
            .iter()
            .filter(move |n| n.r#type == r#type)
    }
    pub fn edges_of_type(&self, r#type: &'static str) -> impl Iterator<Item = &Edge> {
        self
            .edges
            .iter()
            .filter(move |n| n.r#type == r#type)
    }
    pub fn function_entries(&self) -> impl Iterator<Item = &Node> {
        self.nodes_of_type("FunctionEntry")
    }
    pub fn has_function(&self, node: &Node) -> &Node {
        &self.nodes[(node.has_fn - 1) as usize]
    }
    pub fn function_entry_map(&self) -> HashMap<String, &Node> {
        self
            .function_entries()
            .filter_map(|n| n.ir_name().map(|name| (name, n)))
            .collect()
    }
    pub fn call_invocations(&self) -> impl Iterator<Item = &Edge> {
        self
            .edges
            .iter()
            .filter(|e| e.r#type == "ControlDep_CallInv")
    }
    pub fn controldep_entries(&self) -> impl Iterator<Item = &Edge> {
        self
            .edges
            .iter()
            .filter(|e| e.r#type == "ControlDep_Entry")
    }
    // control dependency from destination node id
    pub fn controldep_entry_map(&self) -> HashMap<u64, &Edge> {
        self
            .controldep_entries()
            .map(|e| (e.destination_node_id, e))
            .collect()
    }
    pub fn parameter_in(&self) -> impl Iterator<Item = &Edge> {
        self
            .edges
            .iter()
            .filter(|e| e.r#type == "Parameter_In")
    }
    // map from (caller name, callee name) to edge
    pub fn call_invocation_map(&self) -> HashMap<(String, String), &Edge> {
        let controldep_map = self.controldep_entry_map(); 
        self
            .call_invocations()
            .filter_map(|e| 
                match (controldep_map.get(&e.source_node_id).and_then(|e| self.nodes[(e.source_node_id - 1) as usize].ir_name()), 
                    self.nodes[(e.destination_node_id - 1) as usize].ir_name()) {
                    (Some(caller), Some(callee)) => Some(((caller, callee), e)),
                    _ => None
                }
            )
            .collect()
    }
    pub fn datadep_ret_map(&self) -> HashMap<(String, String), &Edge> {
        self
            .edges_of_type("DataDepEdge_Ret")
            .filter_map(|e| {
                let (src, dst) = self.from_edge(e);
                match (self.nodes[(src.has_fn - 1) as usize].ir_name(), self.nodes[(dst.has_fn - 1) as usize].ir_name()) {
                    (Some(callee), Some(caller)) => Some(((caller, callee), e)),
                    _ => None
                }
            })
            .collect()
    }
    pub fn controldep_ret_map(&self) -> HashMap<(String, String), &Edge> {
        self
            .edges_of_type("ControlDep_CallRet")
            .filter_map(|e| {
                let (src, dst) = self.from_edge(e);
                match (self.nodes[(src.has_fn - 1) as usize].ir_name(), self.nodes[(dst.has_fn - 1) as usize].ir_name()) {
                    (Some(callee), Some(caller)) => Some(((caller, callee), e)),
                    _ => None
                }
            })
            .collect()
    }
    pub fn llid(&self, node: &Node) -> Option<LLID> {
        let get_id = || {
            if let Some(idx) = node.inst_index {
                let func = &self.nodes[(node.has_fn - 1) as usize];
                return func.ir_name().map(|func_name| LLID::InstructionID { global_name: func_name.clone(), index: idx as usize })
            }
            if let Some(idx) = node.param_idx {
                let func = &self.nodes[(node.has_fn - 1) as usize];
                return func.ir_name().map(|func_name| LLID::LocalName { global_name: func_name.clone(), local_name: Name::Number(idx as usize) })
            }
            if node.has_fn != 0 {
                let func = &self.nodes[(node.has_fn - 1) as usize];
                return func.ir_name().map(|func_name| LLID::GlobalName { global_name: func_name.clone() })
            }
            node.ir_name().map(|name| LLID::GlobalName { global_name: name })
        };
        match &node.llid {
            Some(id) => id.clone(),
            None => get_id(),
        }
     
    }

    
}  