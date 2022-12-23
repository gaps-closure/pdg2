use std::{collections::{HashMap, HashSet}, fs::File};
use llvm_ir::{Module, Constant, Operand, Function, instruction::{Call, Store}, DebugLoc, Instruction, Terminator, module::{GlobalVariable, Linkage}, Name, function::Parameter, HasDebugLoc, types::Typed};
use crate::{pdg::{Pdg, Node, Edge}, llvm::{call_sites, LLID, LLValue, LLItem, instr_name, term_name, FunctionUsedAsPointer}, bag::Bag};
use csv::Writer;
use std::hash::Hash;

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

macro_rules! iter_union {
    ($e:expr) => {
        $e
    };
    ($e:expr, $($es:expr),+) => {
        $e
            .into_iter()
            .chain(iter_union!($($es),+))
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

fn ir_iter_to_map<'a, I: IntoIterator<Item = &'a LLValue>>(iter: I) -> HashMap<LLID, &'a LLValue> {
    iter
        .into_iter()
        .map(|v| (v.id.clone(), v))
        .collect()
}
fn node_iter_to_map<'a, I: IntoIterator<Item = &'a Node>>(iter: I, pdg: &Pdg) -> HashMap<LLID, &'a Node> {
    iter
        .into_iter()
        .map(|n| (pdg.llid(n).unwrap(), n))
        .collect()
}

fn map_difference<'a, K: Hash + Eq, A, B>(map1: &'a HashMap<K, A>, map2: &'a HashMap<K, B>)
    -> HashMap<&'a K, &'a A> {
    map1
        .iter()
        .filter(|(k, _)| !map2.contains_key(&k)) 
        .collect()
}
fn map_difference_owned<'a, K: Hash + Eq + ToOwned, A, B>(map1: &'a HashMap<K, A>, map2: &'a HashMap<K, B>)
    -> HashMap<<K as ToOwned>::Owned, &'a A> 
        where
        <K as ToOwned>::Owned: Hash + Eq 
        {
    map1
        .iter()
        .filter(|(k, _)| !map2.contains_key(&k)) 
        .map(|(k, v)| (k.to_owned(), v))
        .collect()
}

fn compare_a_b<'a, K: Hash + Eq + ToOwned, A: ToOwned, B: ToOwned>(
    a_name: &str, b_name: &str, a: &'a HashMap<K, A>, b: &'a HashMap<K, B>, writer: &mut Writer<File>) {

    let a_minus_b = map_difference(a, b);
    let b_minus_a = map_difference(b, a);
    writer.write_record(&id![a_name, b_name, a.len(), b.len(), a_minus_b.len(), b_minus_a.len()]).unwrap();
}

fn compare_node_ir<'a, I: IntoIterator<Item = &'a Node>, J: IntoIterator<Item = &'a LLValue>>(node_name: &str, ir_name: &str, node_iter: I, ir_iter: J, pdg: &Pdg, writer: &mut Writer<File>) {
    compare_a_b(node_name, ir_name, &node_iter_to_map(node_iter, pdg), &ir_iter_to_map(ir_iter), writer);
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

fn global_tags(global: &GlobalVariable, fn_names: &HashSet<String>) -> Vec<SetID> {
    let mut tags = Vec::new();
    tags.push(id!(IRGlobal));
    if &global.name.to_string() == "@llvm.global.annotations" {
        tags.push(id!(IRGlobal.Annotation));
        return tags;
    }

    let name = global.name.to_string()[1..].to_string();
    let mut subname_iter = name.split(".");

    let first_part = 
        subname_iter.next();
        
    let second_part = subname_iter.next();
    let fn_name_candidate = first_part.and_then(|s|
        if s == "__const" { second_part } else { Some(s) });
    let is_fn_local = fn_name_candidate
        .map(|n| fn_names.contains(n))
        .unwrap_or(false);
    if global.debugloc.is_some() {
        if is_fn_local {
            tags.push(id!(IRGlobal.Function));
        } else if global.linkage == Linkage::Internal {
            tags.push(id!(IRGlobal.Module));
        } else if global.is_constant {
            tags.push(id!(IRGlobal.Constant));
        } else {
            tags.push(id!(IRGlobal.Global));
        } 
    } else {
        tags.push(id!(IRGlobal.Other));
    }
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
    let fn_names =
        module
            .functions
            .iter()
            .map(|f| f.name.to_string())
            .collect::<HashSet<_>>();
        
    let globals = 
        module
            .global_vars
            .iter()
            .flat_map(|g| {
                let id = LLID::GlobalName { global_name: g.name.to_string() };
                let item = LLItem::Global(g.clone());
                global_tags(&g, &fn_names)
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
        node_iter_to_map(node_bag.get(&id!(PDGNode.Inst.FunCall)).unwrap().iter().map(|f| *f), pdg);
    let ir_fun_calls =
        ir_iter_to_map(ir_bag.get(&id!(IRInstruction.Call)).unwrap());
    let ir_fun_call_annotations =
        ir_iter_to_map(ir_bag.get(&id!(IRInstruction.Call.Annotation)).unwrap());
    let ir_subtraction = map_difference_owned(&ir_fun_calls, &ir_fun_call_annotations);
    compare_a_b("PDGNode.Inst.FunCall", "IRInstruction.Call - IRInstruction.Call.Annotation", &node_fun_calls, &ir_subtraction, writer);
}

fn write_fns_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, node_bag: &Bag<SetID, &Node>, writer: &mut Writer<File>) {
    let function_entries = node_bag.get(&id!(PDGNode.FunctionEntry)).unwrap()
        .iter().map(|f| *f);
    let ir_fns = ir_bag.get(&id!(IRFunction)).unwrap();
    compare_node_ir("PDGNode.FunctionEntry", "IRFunction", function_entries, ir_fns, pdg, writer);
}

fn write_inst_other_csv(ir_bag: &Bag<SetID, LLValue>, pdg: &Pdg, node_bag: &Bag<SetID, &Node>, writer: &mut Writer<File>) {
    let node_inst_other
        = node_iter_to_map(node_bag.get(&id!(PDGNode.Inst.Other)).unwrap().iter().map(|n| *n), pdg);
    let ir_insts
        = ir_iter_to_map(ir_bag.get(&id!(IRInstruction)).unwrap());
    let ir_fun_calls
        = ir_iter_to_map(ir_bag.get(&id!(IRInstruction.Call)).unwrap());
    let ir_rets
        = ir_iter_to_map(ir_bag.get(&id!(IRInstruction.Ret)).unwrap());
    let ir_brs
        = ir_iter_to_map(ir_bag.get(&id!(IRInstruction.Br)).unwrap());
    let ir_cond_brs
        = ir_iter_to_map(ir_bag.get(&id!(IRInstruction.CondBr)).unwrap());
    let union = 
        iter_union!(ir_fun_calls, ir_rets, ir_brs, ir_cond_brs).collect();
    let ir_subtraction
        = map_difference_owned(&ir_insts, &union);
    
    compare_a_b("PDGNode.Inst.Other", "IRInstruction - IRInstruction.Call - IRInstruction.Ret - IRInstruction.Br - IRInstruction.CondBr", 
        &node_inst_other, &ir_subtraction, writer);
}

fn write_inst_ret_csv(ir_bag: &Bag<Vec<String>, LLValue>, pdg: &Pdg, node_bag: &Bag<Vec<String>, &Node>, writer: &mut Writer<File>) {
    let pdg_ret
        = node_bag.get(&id!(PDGNode.Inst.Ret)).unwrap().iter().map(|n| *n);
    let ir_ret
        = ir_bag.get(&id!(IRInstruction.Ret)).unwrap();
    compare_node_ir("PDGNode.Inst.Ret", "IRInstruction.Ret", pdg_ret, ir_ret, pdg, writer);
}

fn write_inst_br_csv(ir_bag: &Bag<Vec<String>, LLValue>, pdg: &Pdg, node_bag: &Bag<Vec<String>, &Node>, writer: &mut Writer<File>) {
    let ir_br =
        ir_bag.hashmap.get(&id!(IRInstruction.Br)).unwrap();
    let ir_cond_br =
        ir_bag.hashmap.get(&id!(IRInstruction.CondBr)).unwrap();
    let ir_br = iter_union!(ir_br, ir_cond_br);
    let pdg_br =
        node_bag.hashmap.get(&id!(PDGNode.Inst.Br)).unwrap().iter().map(|n| *n);
    compare_node_ir("PDGNode.Inst.Br", "IRInstruction.Br + IRInstruction.CondBr", pdg_br, ir_br, pdg, writer);    
}

fn write_param_in_csv(ir_bag: &Bag<Vec<String>, LLValue>, pdg: &Pdg, node_bag: &Bag<Vec<String>, &Node>, writer: &mut Writer<File>) {
    let ir_params = 
        ir_bag.hashmap.get(&id!(IRParameter)).unwrap();

    let proper_param_in =  
        node_bag.hashmap.get(&id!(PDGNode.Param.FormalIn.Proper)).unwrap().iter().map(|n| *n);

    compare_node_ir("PDGNode.Param.FormalIn.Proper", "IRParameter", proper_param_in, ir_params, pdg, writer);
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
    let (node_bag, edge_bag) = pdg_bags;
    write_inst_funcall_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_fns_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_inst_other_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_inst_ret_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_inst_br_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_param_in_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    // write_varnode_csv(&ir_bag, pdg, &node_bag, &mut validation_writer);
    write_controldep_callinv_csv(&ir_bag, pdg, &edge_bag, &mut validation_writer);

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