use std::{collections::{HashMap}, fs::File};
use llvm_ir::{Module, Constant, Operand, Function, instruction::{Call, Store}, DebugLoc, Instruction, Terminator, module::{GlobalVariable, Linkage}, Name, function::Parameter, HasDebugLoc};
use crate::{pdg::{Pdg, Node, Edge}, llvm::{call_sites, LLID, LLValue, LLItem, instr_name, term_name}, bag::Bag};
use std::error::Error;
use std::hash::Hash;
use csv::Writer;

type SetID = Vec<String>;

fn has_prefix<'a, V>(map: &'a Bag<SetID, V>, pref: &'a SetID) -> impl Iterator<Item = (&'a SetID, &'a Vec<V>)> {
    map.hashmap.iter()
        .filter_map(move |(vec, c)| {
            if vec.len() != pref.len() + 1 {
                return None
            }
            let kp = &vec[0..pref.len()];
            if kp == pref.as_slice() {
                Some((vec, c))
            } else {
                None
            }
        })
}

fn rollup_prefixes<V: Clone>(bag: &mut Bag<SetID, V>) {
    let mut k = bag.hashmap.iter().map(|(k, _)| k.len()).max().unwrap();
    while k > 1 {
        let prefixes = 
            bag.hashmap
                .iter()
                .filter_map(|(v, n)| 
                    if v.len() != k {
                        None
                    } else { 
                        Some((v[..k - 1].to_owned(), n.to_owned())) 
                    }
                )
                .collect::<Vec<_>>();
        for (k, v) in prefixes {
            bag.insert_all(k, v);
        }
        k = k - 1;
    }
}



pub fn write_ir_bag(bag: &Bag<SetID, LLValue>, writer: &mut Writer<File>) {
    // let rows = length_sorted_rows(map);
    for (key, count) in bag.sizes() {
        writer.write_record(&vec![key.join("."), count.to_string()]).unwrap();
    }
}

fn instruction_tags(fn_map: &HashMap<String, Function>, instr: &Instruction) -> Vec<SetID> {
    let mut tags = Vec::new();
    tags.push(vec!["IRInstruction".to_string()]);
    tags.push(vec!["IRInstruction".to_string(), instr_name(instr).to_string()]);
    if let Instruction::Call(call) = instr {
        let fn_name = 
            call.function.clone().right().and_then(|op| {
                match &op {
                    Operand::LocalOperand { name, ty } => Some(name.to_owned()),
                    Operand::ConstantOperand(c) => 
                        if let llvm_ir::Constant::GlobalReference { name, ty: _ } = op.as_constant().unwrap() { Some(name.to_owned()) } else { None },
                    Operand::MetadataOperand => None,
                }
            });
        
        let is_intrinsic = fn_name 
            .as_ref()
            .and_then(|n| if let llvm_ir::Name::Name(s) = n { Some(s.as_str()) } else { None })
            .map_or(false, |s| s.contains("llvm") && !s.contains("llvm.var.annotation"));
    
        if is_intrinsic {
            tags.push(vec!["IRInstruction".to_string(), "Call".to_string(), "Intrinsic".to_string()]);
            return tags;
        }

        let is_annotation = 
            fn_name 
                .as_ref()
                .and_then(|n| if let llvm_ir::Name::Name(s) = n { Some(s.as_str()) } else { None })
                .map_or(false, |s| s.contains("llvm.var.annotation"));

        if is_annotation {
            tags.push(vec!["IRInstruction".to_string(), "Call".to_string(), "Annotation".to_string()]);
            return tags;
        }

        let is_fn_pointer = call.function.clone().right().map_or(false, |o| 
            if let llvm_ir::Operand::LocalOperand { name: _, ty: _ } = o { true } else { false });
    
        if is_fn_pointer {
            tags.push(vec!["IRInstruction".to_string(), "Call".to_string(), "Pointer".to_string()]);
            return tags;
        }
        let func_def = fn_name.and_then(|n| fn_map.get(&n.to_string()[1..]));
        match func_def {
            Some(f) => {
                tags.push(vec!["IRInstruction".to_string(), "Call".to_string(), "Internal".to_string()]);
                if f.is_var_arg {
                    tags.push(vec!["IRInstruction".to_string(), "Call".to_string(), "Internal".to_string(), "VarArg".to_string()]);
                } else {
                    tags.push(vec!["IRInstruction".to_string(), "Call".to_string(), "Internal".to_string(), "NonVarArg".to_string()]);
                }
            },
            None => tags.push(vec!["IRInstruction".to_string(), "Call".to_string(), "External".to_string()]),
        }
    } 
    
    tags
}

fn global_tags(global: &GlobalVariable) -> Vec<SetID> {
    let mut tags = Vec::new();
    tags.push(vec!["IRGlobal".to_string()]);
    if &global.name.to_string() == "@llvm.global.annotations" {
        tags.push(vec!["IRGlobal".to_string(), "Annotation".to_string()]);
        return tags;
    }

    if global.debugloc.is_some() {
        if global.name.to_string().contains(".") {
            tags.push(vec!["IRGlobal".to_string(), "Function".to_string()]);
        } else {
            // how to distinguish "Global" and "Module"  
            if global.linkage == Linkage::Internal {
                tags.push(vec!["IRGlobal".to_string(), "Module".to_string()]);
            } else {
                tags.push(vec!["IRGlobal".to_string(), "Global".to_string()]);
            } 
        }
        return tags;
    } 
    tags.push(vec!["IRGlobal".to_string(), "Other".to_string()]);
    tags
}

fn parameter_tags(function: &Function, parameter: &Parameter) -> Vec<SetID> {
    let mut tags = Vec::new();
    tags.push(vec!["IRParameter".to_string()]);

    tags.push(vec!["IRParameter".to_string(), "In".to_string()]);
    // let mut store_names = 
    //     function
    //         .basic_blocks
    //         .iter()
    //         .flat_map(|b| &b.instrs)
    //         .filter_map(|i| {
    //             if let Instruction::Store() = i {
    //                 Some(name)
    //             } else {
    //                 None
    //             } 
    //         });
    // if store_names.find(|n| *n == &parameter.name).is_some() {
    //     tags.push(vec!["IRParameter".to_string(), "Out".to_string()]);
    // } 
    tags
}

pub fn ir_bag(module: &Module) -> Bag<SetID, LLValue> {
    let fn_map = 
        module.functions.iter().map(|f| (f.name.clone(), f.to_owned())).collect::<HashMap<_,_>>();  
    let fns = 
        module
            .functions
            .iter()
            .map(|f| {
                let id = LLID::GlobalName { global_name: f.name.clone() };
                let item = LLItem::Function(f.clone());
                (vec!["IRFunction".to_string()], LLValue::new(id, item))
            });
    
    let globals = 
        module
            .global_vars
            .iter()
            .flat_map(|g| {
                let id = LLID::GlobalName { global_name: g.name.to_string() };
                let item = LLItem::Global(g.clone());
                global_tags(&g)
                    .iter()
                    .map(|t| (t.clone(), LLValue::new(id.clone(), item.clone())))
                    .collect::<Vec<_>>()
            });
    
    let params =
        module
            .functions
            .iter()
            .flat_map(|f| 
                f.parameters.iter().enumerate().flat_map(move |(idx, p)| {
                    let id = LLID::LocalName { global_name: f.name.clone(), local_name: p.name.clone() };
                    let item = LLItem::Parameter { function: f.clone(), index: idx, parameter: p.clone() };
                    parameter_tags(&f, &p)
                        .iter()
                        .map(|tag| (tag.clone(), LLValue::new(id.clone(), item.clone())))
                        .collect::<Vec<_>>()
                })
            );

    let instrs = 
        module
            .functions
            .iter()
            .flat_map(|f| 
                f.basic_blocks
                    .iter()
                    .flat_map(move |b| b.instrs.iter().map(|i| LLItem::Instruction(i.clone())).chain([LLItem::Terminator(b.term.clone())]))
                    .enumerate()
                    .map(move |(idx, item)| (f.clone(), item.clone(), idx)))
            .flat_map(|(f, item, idx)| {
                if let LLItem::Instruction(inst) = &item {
                    let id = LLID::InstructionID { global_name: f.name.clone(), index: idx };
                    return instruction_tags(&fn_map, &inst)
                        .iter()
                        .map(|tag| (tag.clone(), LLValue::new(id.clone(), item.clone())))
                        .collect::<Vec<_>>()
                } else if let LLItem::Terminator(term) = &item {
                    let id = LLID::InstructionID { global_name: f.name.clone(), index: idx };
                    return [vec!["IRInstruction".to_string()], vec!["IRInstruction".to_string(), term_name(term).to_string()]]
                        .iter()
                        .map(|tag| (tag.clone(), LLValue::new(id.clone(), item.clone())))
                        .collect::<Vec<_>>()
                }
                Vec::new()
            });
    globals.chain(fns).chain(instrs).chain(params).collect()
} 

fn write_inst_funcall_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, node_bag: &Bag<SetID, &Node>, writer: &mut Writer<File>) {
    let node_fun_calls = 
        node_bag.hashmap.get(&vec!["PDGNode".to_string(), "Inst".to_string(), "FunCall".to_string()]).unwrap();
    let ir_fun_calls =
        ir_bag.hashmap.get(&vec!["IRInstruction".to_string(), "Call".to_string()]).unwrap();
    let ir_fun_call_annotations = 
        ir_bag.hashmap.get(&vec!["IRInstruction".to_string(), "Call".to_string(), "Annotation".to_string()])
            .unwrap()
            .iter()
            .map(|v| (v.id.clone(), v.item.clone()))
            .collect::<HashMap<_,_>>();

    let ir_subtraction = 
        ir_fun_calls
            .iter()
            .filter(|l| !ir_fun_call_annotations.contains_key(&l.id))
            .collect::<Vec<_>>();
  
    let node_fun_call_map = 
        node_fun_calls
            .iter()
            .map(|n| (pdg.llid(n).unwrap(), n))
            .collect::<HashMap<_,_>>();
    let ir_subtraction_map =
        ir_subtraction
            .iter()
            .map(|v| {
                (v.id.clone(), v)
            })
            .collect::<HashMap<_,_>>();
    
    let a_minus_b = 
        node_fun_call_map
            .iter()
            .filter(|(key, _)| ir_subtraction_map.get(&key).is_none());
    let b_minus_a = 
        ir_subtraction_map
            .iter()
            .filter(|(key, _)| node_fun_call_map.get(&key).is_none());

    writer.write_record(&vec!["PDGNode.Inst.FunCall".to_string(), "IRInstruction.Call - IRInstruction.Call.Annotation".to_string(), 
        node_fun_call_map.len().to_string(), ir_subtraction_map.len().to_string(), a_minus_b.count().to_string(), b_minus_a.count().to_string()]).unwrap();
}

fn write_inst_other_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, node_bag: &Bag<SetID, &Node>, writer: &mut Writer<File>) {
    let ir_insts
        = ir_bag.hashmap.get(&vec!["IRInstruction".to_string()]).unwrap();
    let ir_fun_calls =
        ir_bag.hashmap.get(&vec!["IRInstruction".to_string(), "Call".to_string()]).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    let ir_rets =
        ir_bag.hashmap.get(&vec!["IRInstruction".to_string(), "Ret".to_string()]).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    let ir_brs =
        ir_bag.hashmap.get(&vec!["IRInstruction".to_string(), "Br".to_string()]).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();

    let ir_cond_brs =
        ir_bag.hashmap.get(&vec!["IRInstruction".to_string(), "CondBr".to_string()]).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();

    let ir_subtraction_map = 
        ir_insts
            .iter()
            .filter(|v| {
                let k = &v.id;
                !(ir_fun_calls.contains_key(k) || ir_rets.contains_key(k) || ir_brs.contains_key(k) || ir_cond_brs.contains_key(k))
            })
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    
    let node_inst_other_map = 
        node_bag.hashmap.get(&vec!["PDGNode".to_string(), "Inst".to_string(), "Other".to_string()]).unwrap()
            .iter()
            .map(|n| (pdg.llid(n).unwrap(), n))
            .collect::<HashMap<_,_>>();
    
    let a_minus_b = 
        node_inst_other_map
            .iter()
            .filter(|(key, _)| ir_subtraction_map.get(&key).is_none())
            .collect::<Vec<_>>();

    let b_minus_a = 
        ir_subtraction_map
            .iter()
            .filter(|(key, _)| node_inst_other_map.get(&key).is_none())
            .collect::<Vec<_>>();

    writer.write_record(&vec!["PDGNode.Inst.Other".to_string(), "IRInstruction - IRInstruction.Call - IRInstruction.Ret - IRInstruction.Br - IRInstruction.CondBr".to_string(), 
        node_inst_other_map.len().to_string(), ir_subtraction_map.len().to_string(), a_minus_b.len().to_string(), b_minus_a.len().to_string()]).unwrap();
   
}

fn write_validation_csv(ir_bag: Bag<SetID, LLValue>, mut validation_writer: Writer<File>, mut counts_writer: Writer<File>, pdg: &Pdg) {
    for (k, values) in &ir_bag.hashmap {
        let rollup = has_prefix(&ir_bag, &k).collect::<Vec<_>>();
        if rollup.len() == 0 {
            continue;
        }
        let rollup_names = 
            rollup
                .iter()
                .map(|(k, _)| k.join("."))
                .collect::<Vec<_>>()
                .join(" + ");

        let name = k.join(".");

        let rollup_count: usize =
            rollup
                .iter()
                .map(|(_, v)| v.len())
                .sum();
        
        // TODO: actually calculate values for last two entries
        validation_writer.write_record(&vec![name,rollup_names,values.len().to_string(),rollup_count.to_string(), "0".to_string(), "0".to_string()]).unwrap();
    } 
    // counts.write_record(&vec!["PDGNode".to_string(), "".to_string(), pdg.nodes.len().to_string()]).unwrap();

    let mut node_bag = Bag::new();
    for node in &pdg.nodes {
        let mut name = node.r#type.split("_").map(|x| x.to_owned()).collect::<Vec<_>>();
        name.insert(0, "PDGNode".to_string());
        node_bag.insert(name, node);
    }
    rollup_prefixes(&mut node_bag);
    let proper_param_nodes = proper_param_nodes(&node_bag);
    node_bag.hashmap.insert(vec!["PDGNode".to_string(), "Param".to_string(), "FormalIn".to_string(), "Proper".to_string()], proper_param_nodes.iter().collect::<Vec<_>>());
    write_inst_funcall_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_inst_other_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_param_in_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);


    for (k, v) in &node_bag.hashmap {
        counts_writer.write_record(&vec![k.join("."), String::new(), v.len().to_string()]).unwrap();
    }

    let mut edge_bag = Bag::new();
    for edge in &pdg.edges {
        let mut name = edge.r#type.split("_").map(|x| x.to_owned()).collect::<Vec<_>>();
        name.insert(0, "PDGEdge".to_string());
        edge_bag.insert(name, edge);
    }

    rollup_prefixes(&mut edge_bag);
    let proper_param_edges = proper_param_edges(pdg, &mut edge_bag);
    edge_bag.hashmap.insert(vec!["PDGEdge".to_string(), "Parameter".to_string(), "In".to_string(), "Proper".to_string()], proper_param_edges.iter().collect::<Vec<_>>());

    write_controldep_callinv_csv(&ir_bag, pdg, &edge_bag, &mut validation_writer);

    for (k, v) in &edge_bag.hashmap {
        counts_writer.write_record(&vec![k.join("."), String::new(), v.len().to_string()]).unwrap();
    }

}

fn write_param_in_csv(ir_bag: &Bag<Vec<String>, LLValue>, pdg: &Pdg, node_bag: &Bag<Vec<String>, &Node>, writer: &mut Writer<File>) {
    let ir_params = 
        ir_bag.hashmap.get(&vec!["IRParameter".to_string()]).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();

    let proper_param_in =  
        node_bag.hashmap.get(&vec!["PDGNode".to_string(), "Param".to_string(), "FormalIn".to_string(), "Proper".to_string()]).unwrap()
            .iter()
            .map(|n| (pdg.llid(n).unwrap(), n))
            .collect::<HashMap<_,_>>();

    let a_minus_b = 
        proper_param_in
            .iter()
            .filter(|(key, _)| ir_params.get(&key).is_none())
            .collect::<Vec<_>>();

    let b_minus_a = 
        ir_params
            .iter()
            .filter(|(key, _)| proper_param_in.get(&key).is_none())
            .collect::<Vec<_>>();

    writer.write_record(&vec!["PDGNode.Param.FormalIn.Proper".to_string(), "IRParameter".to_string(), 
        proper_param_in.len().to_string(), ir_params.len().to_string(), a_minus_b.len().to_string(), b_minus_a.len().to_string()]).unwrap();

}
fn write_controldep_callinv_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, edge_bag: &Bag<SetID, &Edge>, writer: &mut Writer<File>) {
    let ir_internal_calls = 
        ir_bag.hashmap.get(&vec!["IRInstruction".to_string(), "Call".to_string(), "Internal".to_string()]).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    let callinv_edges = 
        edge_bag.hashmap.get(&vec!["PDGEdge".to_string(), "ControlDep".to_string(), "CallInv".to_string()]).unwrap()
            .iter()
            .map(|e| {
                let (src, _) = pdg.from_edge(e);
                (pdg.llid(src).unwrap(), e)
            })
            .collect::<HashMap<_,_>>();
    
    let a_minus_b = 
        callinv_edges
            .iter()
            .filter(|(key, _)| ir_internal_calls.get(key).is_none())
            .collect::<Vec<_>>();

    let b_minus_a = 
        ir_internal_calls
            .iter()
            .filter(|(key, _)| callinv_edges.get(key).is_none())
            .collect::<Vec<_>>();
    
    writer.write_record(&vec!["PDGEdge.ControlDep.CallInv".to_string(), "IRInstruction.Call.Internal".to_string(), 
        callinv_edges.len().to_string(), ir_internal_calls.len().to_string(), a_minus_b.len().to_string(), b_minus_a.len().to_string()]).unwrap();
    
}
fn proper_param_nodes(node_bag: &Bag<SetID, &Node>) -> Vec<Node> {
    node_bag.hashmap.get(&vec!["PDGNode".to_string(), "Param".to_string(), "FormalIn".to_string()]).unwrap()
        .iter()
        .filter(|n| n.param_idx.is_some())
        .map(|n| (*n).clone())
        .collect()
}

fn proper_param_edges(pdg: &Pdg, edge_bag: &Bag<SetID, &Edge>) -> Vec<Edge> {
    edge_bag.hashmap.get(&vec!["PDGEdge".to_string(), "Parameter".to_string(), "In".to_string()]).unwrap()
        .iter()
        .filter(|e| {
            let (src, dst) = pdg.from_edge(e);
            src.r#type == "Param_ActualIn" && dst.r#type == "Param_FormalIn" && dst.param_idx.is_some()
        })
        .map(|e| (*e).clone())
        .collect()
}


pub fn report(bc_file: &str, pdg_data_file: &str, counts_csv: &str, validation_csv: &str) {
    let module = Module::from_bc_path(bc_file).unwrap();
    let pdg = {
        let file = File::open(pdg_data_file).unwrap();
        let rdr = 
            csv::ReaderBuilder::new()
                .has_headers(false)
                .quote(b'\'')
                .trim(csv::Trim::All)
                .from_reader(std::io::BufReader::new(file));

        Pdg::from_csv_reader(rdr).unwrap()
    };
    let mut counts_writer = 
        csv::WriterBuilder::new()
            .flexible(true)
            .from_path(counts_csv)
            .unwrap();

    let validation_writer = 
        csv::WriterBuilder::new()
            .flexible(true)
            .from_path(validation_csv)
            .unwrap();
    let bag = ir_bag(&module);
    write_ir_bag(&bag, &mut counts_writer);
    write_validation_csv(bag, validation_writer, counts_writer, &pdg);
}