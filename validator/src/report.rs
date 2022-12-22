use std::{collections::{HashMap}, fs::File};
use llvm_ir::{Module, Constant, Operand, Function, instruction::{Call, Store}, DebugLoc, Instruction, Terminator, module::{GlobalVariable, Linkage}, Name, function::Parameter, HasDebugLoc, types::Typed};
use crate::{pdg::{Pdg, Node, Edge}, llvm::{call_sites, LLID, LLValue, LLItem, instr_name, term_name, FunctionUsedAsPointer}, bag::Bag};
use csv::Writer;

type SetID = Vec<String>;
macro_rules! id {
    ($($x:ident).+) => {
        vec![$(stringify!{$x}.to_string()),+]
    };
    ($($e:expr),+) => {
        vec![$($e.to_string()),+]
    };
}

macro_rules! ids {
    ($($($x:ident).+),+) => {
        vec![$(id!($($x).+)),+]
    };
}

fn edge_ids() -> Vec<SetID> {
    ids! {
        PDGEdge,
        PDGEdge.ControlDep,
        PDGEdge.ControlDep.CallInv,
        PDGEdge.ControlDep.CallRet,
        PDGEdge.ControlDep.Entry,
        PDGEdge.ControlDep.Br,
        PDGEdge.ControlDep.Other,
        PDGEdge.DataDepEdge,
        PDGEdge.DataDepEdge.DefUse,
        PDGEdge.DataDepEdge.RAW,
        PDGEdge.DataDepEdge.Alias,
        PDGEdge.Parameter,
        PDGEdge.Parameter.In,
        PDGEdge.Parameter.Out,
        PDGEdge.Parameter.Field,
        PDGEdge.Anno,
        PDGEdge.Anno.Global,
        PDGEdge.Anno.Var,
        PDGEdge.Anno.Other
    }
}

fn node_ids() -> Vec<SetID> {
    ids! {
        PDGNode,
        PDGNode.Inst,
        PDGNode.Inst.FunCall,
        PDGNode.Inst.Ret,
        PDGNode.Inst.Br,
        PDGNode.Inst.Other,
        PDGNode.VarNode,
        PDGNode.VarNode.StaticGlobal,
        PDGNode.VarNode.StaticModule,
        PDGNode.VarNode.StaticFunction,
        PDGNode.VarNode.StaticOther,
        PDGNode.FunctionEntry,
        PDGNode.Param,
        PDGNode.Param.FormalIn,
        PDGNode.Param.FormalOut,
        PDGNode.Param.ActualIn,
        PDGNode.Param.ActualOut,
        PDGNode.Annotation,
        PDGNode.Annotation.Var,
        PDGNode.Annotation.Global,
        PDGNode.Annotation.Other
    }
}

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

fn instruction_tags(fn_map: &HashMap<String, Function>, instr: &Instruction) -> Vec<SetID> {
    let mut tags = Vec::new();
    tags.push(id!(IRInstruction));
    tags.push(id!("IRInstruction", instr_name(instr)));
    if let Instruction::Call(call) = instr {
        let fn_name = 
            call.function.clone().right().and_then(|op| {
                match &op {
                    Operand::LocalOperand { name, ty: _ } => Some(name.to_owned()),
                    Operand::ConstantOperand(_) => 
                        if let llvm_ir::Constant::GlobalReference { name, ty: _ } = op.as_constant().unwrap() { Some(name.to_owned()) } else { None },
                    Operand::MetadataOperand => None,
                }
            });
        
        let is_intrinsic = fn_name 
            .as_ref()
            .and_then(|n| if let llvm_ir::Name::Name(s) = n { Some(s.as_str()) } else { None })
            .map_or(false, |s| s.contains("llvm") && !s.contains("llvm.var.annotation"));
    
        if is_intrinsic {
            tags.push(id!(IRInstruction.Call.Intrinsic)); 
            return tags;
        }

        let is_annotation = 
            fn_name 
                .as_ref()
                .and_then(|n| if let llvm_ir::Name::Name(s) = n { Some(s.as_str()) } else { None })
                .map_or(false, |s| s.contains("llvm.var.annotation"));

        if is_annotation {
            tags.push(id!(IRInstruction.Call.Annotation));
            return tags;
        }

        let is_fn_pointer = call.function.clone().right().map_or(false, |o| 
            if let llvm_ir::Operand::LocalOperand { name: _, ty: _ } = o { true } else { false });
    
        if is_fn_pointer {
            tags.push(id!(IRInstruction.Call.Pointer));
            return tags;
        }
        let func_def = fn_name.and_then(|n| fn_map.get(&n.to_string()[1..]));
        match func_def {
            Some(f) => {
                tags.push(id!(IRInstruction.Call.Internal));
                if f.is_var_arg {
                    tags.push(id!(IRInstruction.Call.Internal.VarArg));
                } else {
                    tags.push(id!(IRInstruction.Call.Internal.NonVarArg));
                }
            },
            None => tags.push(id!(IRInstruction.Call.External.NonVarArg)),
        }
    } 
    tags
}

fn global_tags(global: &GlobalVariable) -> Vec<SetID> {
    let mut tags = Vec::new();
    tags.push(id!(IRGlobal));
    if &global.name.to_string() == "@llvm.global.annotations" {
        tags.push(id!(IRGlobal.Annotation));
        return tags;
    }

    if global.debugloc.is_some() {
        if global.name.to_string().contains(".") {
            tags.push(id!(IRGlobal.Function));
        } else {
            // how to distinguish "Global" and "Module"  
            if global.linkage == Linkage::Internal {
                tags.push(id!(IRGlobal.Module));
            } else {
                tags.push(id!(IRGlobal.Global));
            } 
        }
        return tags;
    } 
    tags.push(id!(IRGlobal.Other));
    tags
}

fn parameter_tags(_function: &Function, _parameter: &Parameter) -> Vec<SetID> {
    // temporary, will need to discriminate between in / out parameters
    ids! {
        IRParameter,
        IRParameter.In
        // IRParameter.Out
    }
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
                (id!(IRFunction), LLValue::new(id, item))
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
                    return [id!(IRInstruction), id!("IRInstruction", term_name(term))]
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
        node_bag.hashmap.get(&id!(PDGNode.Inst.FunCall)).unwrap();
    let ir_fun_calls =
        ir_bag.hashmap.get(&id!(IRInstruction.Call)).unwrap();
    let ir_fun_call_annotations = 
        ir_bag.hashmap.get(&id!(IRInstruction.Call.Annotation))
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

    writer.write_record(&id!("PDGNode.Inst.FunCall", "IRInstruction.Call - IRInstruction.Call.Annotation", 
        node_fun_call_map.len(), ir_subtraction_map.len(), a_minus_b.count(), b_minus_a.count())).unwrap();
}

fn write_fns_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, node_bag: &Bag<SetID, &Node>, writer: &mut Writer<File>) {
    let ir_fns
        = ir_bag.hashmap.get(&id!(IRFunction)).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    let function_entries
        = node_bag.hashmap.get(&id!(PDGNode.FunctionEntry)).unwrap()
            .iter()
            .map(|n| (pdg.llid(n).unwrap(), n))
            .collect::<HashMap<_,_>>();
    let a_minus_b = 
        function_entries
            .iter()
            .filter(|(key, _)| ir_fns.get(&key).is_none())
            .collect::<Vec<_>>();

    let b_minus_a = 
        ir_fns
            .iter()
            .filter(|(key, _)| function_entries.get(&key).is_none())
            .collect::<Vec<_>>(); 
    writer.write_record(&id!("PDGNode.FunctionEntry", "IRFunction", 
        function_entries.len(), ir_fns.len(), a_minus_b.len(), b_minus_a.len())).unwrap();

}

fn write_inst_other_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, node_bag: &Bag<SetID, &Node>, writer: &mut Writer<File>) {
    let ir_insts
        = ir_bag.hashmap.get(&id!(IRInstruction)).unwrap();
    let ir_fun_calls =
        ir_bag.hashmap.get(&id!(IRInstruction.Call)).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    let ir_rets =
        ir_bag.hashmap.get(&id!(IRInstruction.Ret)).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    let ir_brs =
        ir_bag.hashmap.get(&id!(IRInstruction.Br)).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();

    let ir_cond_brs =
        ir_bag.hashmap.get(&id!(IRInstruction.CondBr)).unwrap()
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
        node_bag.hashmap.get(&id!(PDGNode.Inst.Other)).unwrap()
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

    writer.write_record(&id!("PDGNode.Inst.Other", "IRInstruction - IRInstruction.Call - IRInstruction.Ret - IRInstruction.Br - IRInstruction.CondBr", 
        node_inst_other_map.len(), ir_subtraction_map.len(), a_minus_b.len(), b_minus_a.len())).unwrap();
   
}

fn pdg_bag(pdg: &Pdg) -> (Bag<SetID, &Node>, Bag<SetID, &Edge>) {
    let mut node_bag = Bag::new();
    for id in node_ids() {
        node_bag.insert_all(id, vec![]);
    } 
    for node in &pdg.nodes {
        let mut name = node.r#type.split("_").map(|x| x.to_owned()).collect::<Vec<_>>();
        name.insert(0, "PDGNode".to_string());
        node_bag.insert(name, node);
    }
    
    rollup_prefixes(&mut node_bag);
    let proper_param_nodes = proper_param_nodes(&node_bag);
    node_bag.hashmap.insert(id!(PDGNode.Param.FormalIn.Proper), proper_param_nodes);

    let mut edge_bag = Bag::new();
    for id in edge_ids() {
        edge_bag.insert_all(id, vec![]);
    } 
    for edge in &pdg.edges {
        let mut name = edge.r#type.split("_").map(|x| x.to_owned()).collect::<Vec<_>>();
        name.insert(0, "PDGEdge".to_string());
        edge_bag.insert(name, edge);
    }

    rollup_prefixes(&mut edge_bag);
    let proper_param_edges = proper_param_edges(pdg, &mut edge_bag);
    edge_bag.hashmap.insert(id!(PDGEdge.Parameter.In.Proper), proper_param_edges);

    (node_bag, edge_bag)
}

fn write_counts_csv(ir_bag: &Bag<SetID, LLValue>, pdg_bags: (&Bag<SetID, &Node>, &Bag<SetID, &Edge>), writer: &mut Writer<File>) {
    let (node_bag, edge_bag) = pdg_bags;
    for (k, count) in node_bag.sizes() {
        writer.write_record(&vec![k.join("."), count.to_string()]).unwrap();
    }
    for (k, count) in edge_bag.sizes() {
        writer.write_record(&vec![k.join("."), count.to_string()]).unwrap();
    }
    for (k, count) in ir_bag.sizes() {
        writer.write_record(&vec![k.join("."), count.to_string()]).unwrap();
    }

}

fn write_validation_csv(ir_bag: Bag<SetID, LLValue>, pdg_bags: (Bag<SetID, &Node>, Bag<SetID, &Edge>), mut validation_writer: Writer<File>, pdg: &Pdg) {
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
    let (node_bag, edge_bag) = pdg_bags;
    write_inst_funcall_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_fns_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_inst_other_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_inst_ret_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_inst_br_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_param_in_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_controldep_callinv_csv(&ir_bag, pdg, &edge_bag, &mut validation_writer);

}

fn write_inst_ret_csv(ir_bag: &Bag<Vec<String>, LLValue>, pdg: &Pdg, node_bag: &Bag<Vec<String>, &Node>, writer: &mut Writer<File>) {
    let ir_ret =
        ir_bag.hashmap.get(&id!(IRInstruction.Ret)).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    
    let pdg_ret =
        node_bag.hashmap.get(&id!(PDGNode.Inst.Ret)).unwrap()
            .iter()
            .map(|n| (pdg.llid(n).unwrap(), n))
            .collect::<HashMap<_,_>>();
    
    let a_minus_b =
        pdg_ret
            .iter()
            .filter(|(k, _)| !ir_ret.contains_key(k)) 
            .collect::<Vec<_>>();
    
    let b_minus_a =
        ir_ret
            .iter()
            .filter(|(k, _)| !pdg_ret.contains_key(k))
            .collect::<Vec<_>>();
    
    writer.write_record(&id!("PDGNode.Inst.Ret", "IRInstruction.Ret", 
        ir_ret.len(), pdg_ret.len(), a_minus_b.len(), b_minus_a.len())).unwrap();
}

fn write_inst_br_csv(ir_bag: &Bag<Vec<String>, LLValue>, pdg: &Pdg, node_bag: &Bag<Vec<String>, &Node>, writer: &mut Writer<File>) {
    let ir_br =
        ir_bag.hashmap.get(&id!(IRInstruction.Br)).unwrap();
    let ir_cond_br =
        ir_bag.hashmap.get(&id!(IRInstruction.CondBr)).unwrap();
    let ir_br = 
        ir_br.iter().chain(ir_cond_br)
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    
    let pdg_br =
        node_bag.hashmap.get(&id!(PDGNode.Inst.Br)).unwrap()
            .iter()
            .map(|n| (pdg.llid(n).unwrap(), n))
            .collect::<HashMap<_,_>>();
    
    let a_minus_b =
        pdg_br
            .iter()
            .filter(|(k, _)| !ir_br.contains_key(k)) 
            .collect::<Vec<_>>();
    
    let b_minus_a =
        ir_br
            .iter()
            .filter(|(k, _)| !pdg_br.contains_key(k))
            .collect::<Vec<_>>();
   
    writer.write_record(&id!("PDGNode.Inst.Br", "IRInstruction.Br + IRInstruction.CondBr", 
        pdg_br.len(), ir_br.len(), a_minus_b.len(), b_minus_a.len())).unwrap();
}


fn write_param_in_csv(ir_bag: &Bag<Vec<String>, LLValue>, pdg: &Pdg, node_bag: &Bag<Vec<String>, &Node>, writer: &mut Writer<File>) {
    let ir_params = 
        ir_bag.hashmap.get(&id!(IRParameter)).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();

    let proper_param_in =  
        node_bag.hashmap.get(&id!(PDGNode.Param.FormalIn.Proper)).unwrap()
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

    writer.write_record(&id!("PDGNode.Param.FormalIn.Proper", "IRParameter", 
        proper_param_in.len(), ir_params.len(), a_minus_b.len(), b_minus_a.len())).unwrap();

}


fn write_controldep_callinv_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, edge_bag: &Bag<SetID, &Edge>, writer: &mut Writer<File>) {
    let ir_internal_calls = 
        ir_bag.hashmap.get(&id!(IRInstruction.Call.Internal)).unwrap()
            .iter()
            .map(|v| (v.id.clone(), v))
            .collect::<HashMap<_,_>>();
    let callinv_edges = 
        edge_bag.hashmap.get(&id!(PDGEdge.ControlDep.CallInv)).unwrap()
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
    
    writer.write_record(&id!("PDGEdge.ControlDep.CallInv", "IRInstruction.Call.Internal", 
        callinv_edges.len(), ir_internal_calls.len(), a_minus_b.len(), b_minus_a.len())).unwrap();
    
}
fn proper_param_nodes<'a>(node_bag: &Bag<SetID, &'a Node>) -> Vec<&'a Node> {
    node_bag.hashmap.get(&id!(PDGNode.Param.FormalIn)).unwrap()
        .iter()
        .filter(|n| n.param_idx.is_some())
        .map(|n| (*n))
        .collect()
}

fn proper_param_edges<'a>(pdg: &Pdg, edge_bag: &Bag<SetID, &'a Edge>) -> Vec<&'a Edge> {
    edge_bag.hashmap.get(&id!(PDGEdge.Parameter.In)).unwrap()
        .iter()
        .filter(|e| {
            let (src, dst) = pdg.from_edge(e);
            src.r#type == "Param_ActualIn" && dst.r#type == "Param_FormalIn" && dst.param_idx.is_some()
        })
        .map(|e| (*e))
        .collect()
}


fn write_distinct_fn_sigs(module: &Module, writer: &mut Writer<File>) {
    let distinct_fns =
        module
            .functions
            .iter()
            .map(|f| (f.get_type(&module.types), f))
            .collect::<Bag<_,_>>();
    writer.write_record(id!("IRDistinctFunctionSignatures", distinct_fns.hashmap.len())).unwrap();
}

fn write_funcs_used_as_ptr(module: &Module, writer: &mut Writer<File>) {
    let fns = module.funcs_used_as_pointer();
    writer.write_record(id!{"IRDistinctFunctionsUsedAsPointer", fns.len()}).unwrap();
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

    write_distinct_fn_sigs(&module, &mut counts_writer);
    write_funcs_used_as_ptr(&module, &mut counts_writer);
    let ir_bag = ir_bag(&module);
    let (node_bag, edge_bag) = pdg_bag(&pdg);
    write_counts_csv(&ir_bag, (&node_bag, &edge_bag), &mut counts_writer);
    write_validation_csv(ir_bag, (node_bag, edge_bag), validation_writer, &pdg);
}