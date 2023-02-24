use std::{
    collections::{HashMap, HashSet},
    iter::{empty, Cloned, Empty},
};

use llvm_ir::{
    function::Parameter,
    module::{GlobalVariable, Linkage},
    Function, Instruction, Module, Operand, Terminator,
};

use crate::{
    bag::Bag,
    id,
    id::ID,
    ids,
    indexed_set::ISet,
    llvm::{call_to_function_pointer, instr_name, term_name, LLItem, LLValue, LLID},
    pdg::{Edge, Node, Pdg},
    report::Report,
};

fn edge_ids() -> Vec<ID> {
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

fn node_ids() -> Vec<ID> {
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

fn global_tags(global: &GlobalVariable, fn_names: &HashSet<String>) -> Option<ID> {
    if global.linkage == Linkage::Private {
        return None;
    }

    let name = global.name.to_string()[1..].to_string();
    if &name == "llvm.global.annotations" {
        return Some(id!(IRGlobal.Internal.Annotation));
    }
    if global.initializer.is_none() {
        return Some(id!(IRGlobal.External));
    }
    let mut subname_iter = name.split(".");
    let fn_name_candidate = subname_iter.next();
    let is_fn_local = fn_name_candidate
        .map(|n| fn_names.contains(n))
        .unwrap_or(false);

    if global.linkage == Linkage::Internal {
        if is_fn_local {
            Some(id!(IRGlobal.Internal.Function))
        } else {
            Some(id!(IRGlobal.Internal.Module))
        }
    } else {
        Some(id!(IRGlobal.Internal.Omni))
    }
}

fn parameter_tag(_function: &Function, _parameter: &Parameter) -> ID {
    // temporary, will need to discriminate between in / out parameters
    id!(IRParameter.In)
}

fn functions(module: &Module) -> HashSet<LLValue> {
    module
        .functions
        .iter()
        .map(|f| {
            let id = LLID::GlobalName {
                global_name: f.name.clone(),
            };
            let item = LLItem::Function(f.clone());
            LLValue::new(id, item)
        })
        .collect()
}

fn function_names(module: &Module) -> HashMap<String, &Function> {
    module
        .functions
        .iter()
        .map(|f| (f.name.clone(), f))
        .collect()
}

fn globals(module: &Module, fn_names: &HashSet<String>) -> ISet<ID, LLValue> {
    module
        .global_vars
        .iter()
        .flat_map(|g| {
            let id = LLID::global_name_from_name(&g.name);
            let item = LLItem::Global(g.clone());
            global_tags(&g, &fn_names)
                .iter()
                .map(|t| (t.clone(), LLValue::new(id.clone(), item.clone())))
                .collect::<Vec<_>>()
        })
        .collect()
}
fn terminator_tag(term: &Terminator) -> ID {
    id!("IRInstruction", term_name(term))
}

fn instruction_tag(fn_map: &HashMap<String, &Function>, instr: &Instruction) -> ID {
    if let Instruction::Call(call) = instr {
        let fn_name = LLItem::Instruction(instr.to_owned()).constant_call_name();
        let is_intrinsic = fn_name.as_ref().map_or(false, |n| {
            let s = n.to_string();
            s.contains("llvm") && !s.contains("llvm.var.annotation")
        });
        if is_intrinsic {
            return id!(IRInstruction.Call.Intrinsic);
        }

        let is_annotation = fn_name.as_ref().map_or(false, |n| {
            let s = n.to_string();
            s.contains("llvm.var.annotation")
        });

        if is_annotation {
            return id!(IRInstruction.Call.Annotation);
        }

        let is_fn_pointer = call_to_function_pointer(call);
        if is_fn_pointer {
            return id!(IRInstruction.Call.Pointer);
        }
        let func_def = fn_name.and_then(|n| fn_map.get(&n.to_string()[1..]));
        match func_def {
            Some(f) => {
                if f.is_var_arg {
                    return id!(IRInstruction.Call.Internal.VarArg);
                } else {
                    return id!(IRInstruction.Call.Internal.NonVarArg);
                }
            }
            None => {
                return id!(IRInstruction.Call.External.NonVarArg);
            }
        }
    }
    id!("IRInstruction", instr_name(instr))
}

fn instructions(module: &Module, fn_map: &HashMap<String, &Function>) -> ISet<ID, LLValue> {
    enum Either<A, B> {
        Left(A),
        Right(B),
    }

    module
        .functions
        .iter()
        .flat_map(|f| {
            f.basic_blocks
                .iter()
                .flat_map(move |b| {
                    b.instrs
                        .iter()
                        .map(|i| Either::Left(i))
                        .chain([Either::Right(&b.term)])
                })
                .enumerate()
                .map(move |(idx, e)| {
                    let id = LLID::InstructionID {
                        global_name: f.name.clone(),
                        index: idx,
                    };
                    match e {
                        Either::Left(inst) => {
                            let tag = instruction_tag(fn_map, inst);
                            let value = LLValue::new(id, LLItem::Instruction(inst.to_owned()));
                            (tag, value)
                        }
                        Either::Right(term) => {
                            let tag = terminator_tag(term);
                            let value = LLValue::new(id, LLItem::Terminator(term.to_owned()));
                            (tag, value)
                        }
                    }
                })
        })
        .collect()
}

fn parameters(module: &Module) -> ISet<ID, LLValue> {
    module
        .functions
        .iter()
        .flat_map(|f| {
            f.parameters.iter().enumerate().map(move |(idx, p)| {
                let id = LLID::LocalName {
                    global_name: f.name.clone(),
                    local_name: p.name.clone(),
                };
                let item = LLItem::Parameter {
                    function: f.clone(),
                    index: idx,
                    parameter: p.clone(),
                };
                let tag = parameter_tag(&f, &p);
                (tag, LLValue::new(id, item))
            })
        })
        .collect()
}

pub trait IndexedSets<A> {
    fn indexed_sets(&self) -> ISet<ID, A>;
}

impl IndexedSets<LLValue> for Module {
    fn indexed_sets(&self) -> ISet<ID, LLValue> {
        let mut iset = ISet::new();
        let fn_map = function_names(self);
        iset.insert_all(id!(IRFunction), functions(self));
        iset.extend(globals(self, &fn_map.keys().cloned().collect()));
        iset.extend(instructions(self, &fn_map));
        iset.extend(parameters(self));
        iset.rollup_prefixes();
        iset
    }
}

impl IndexedSets<Node> for Pdg {
    fn indexed_sets(&self) -> ISet<ID, Node> {
        let mut iset = ISet::new();
        for id in node_ids() {
            iset.insert_empty(id);
        }
        for node in &self.nodes {
            let mut name = node
                .r#type
                .split("_")
                .map(|x| x.to_owned())
                .collect::<Vec<_>>();
            name.insert(0, "PDGNode".to_string());
            iset.insert(ID(name), node.clone());
        }
        iset.rollup_prefixes();
        iset
    }
}

impl IndexedSets<Edge> for Pdg {
    fn indexed_sets(&self) -> ISet<ID, Edge> {
        let mut iset = ISet::new();
        for id in edge_ids() {
            iset.insert_empty(id);
        }
        for edge in &self.edges {
            let mut name = edge
                .r#type
                .split("_")
                .map(|x| x.to_owned())
                .collect::<Vec<_>>();
            name.insert(0, "PDGEdge".to_string());
            iset.insert(ID(name), edge.clone());
        }
        iset.rollup_prefixes();
        iset
    }
}
