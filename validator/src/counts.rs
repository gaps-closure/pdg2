use std::{
    collections::{HashMap, HashSet},
};

use llvm_ir::{
    function::Parameter,
    module::{GlobalVariable, Linkage},
    Function, Instruction, Module, Terminator,
};

use lazy_static::lazy_static;

use crate::{
    id,
    id::ID,
    ids,
    indexed_set::ISet,
    llvm::{call_to_function_pointer, instr_name, term_name, LLItem, LLValue, LLID},
    pdg::{Edge, Node, Pdg},
};

lazy_static! {
    pub static ref EDGE_IDS: Vec<ID> = ids! {
        PDGEdge.Anno.Global,
        PDGEdge.Anno.Other,
        PDGEdge.Anno.Var,
        "PDGEdge.Anno.Global + PDGEdge.Anno.Other + PDGEdge.Anno.Var",
        PDGEdge.Anno,
        PDGEdge.ControlDep.Br,
        PDGEdge.ControlDep.CallInv,
        PDGEdge.ControlDep.CallRet,
        PDGEdge.ControlDep.Entry,
        PDGEdge.ControlDep.Other,
        "PDGEdge.ControlDep.Br + PDGEdge.ControlDep.CallInv + PDGEdge.ControlDep.CallRet + PDGEdge.ControlDep.Entry + PDGEdge.ControlDep.Other",
        PDGEdge.ControlDep,
        PDGEdge.DataDepEdge.Alias,
        PDGEdge.DataDepEdge.DefUse,
        PDGEdge.DataDepEdge.RAW,
        PDGEdge.DataDepEdge.Ret,
        "PDGEdge.DataDepEdge.Alias + PDGEdge.DataDepEdge.DefUse + PDGEdge.DataDepEdge.RAW + PDGEdge.DataDepEdge.Ret",
        PDGEdge.DataDepEdge,
        PDGEdge.Parameter.Field,
        PDGEdge.Parameter.In,
        PDGEdge.Parameter.Out,
        "PDGEdge.Parameter.Field + PDGEdge.Parameter.In + PDGEdge.Parameter.Out",
        PDGEdge.Parameter,
        "PDGEdge.Anno + PDGEdge.ControlDep + PDGEdge.DataDepEdge + PDGEdge.Parameter",
        PDGEdge,
        XPDGEdge.ControlDep.CallInv.ViaExternal,
        XPDGEdge.ControlDep.CallInv.Indirect,
        XPDGEdge.ControlDep.CallRet.Indirect,
        XPDGEdge.DataDepEdge.ViaExternal,
        XPDGEdge.DataDepEdge.Indirect.Ret,
        XPDGEdge.DataDepEdge.Indirect.Raw,
        XPDGEdge.DataDepEdge.Indirect.DefUse,
        XPDGEdge.DataDepEdge.Indirect,
        XPDGEdge.DataDepEdge.Parameter.Indirect.Actual.In,
        XPDGEdge.DataDepEdge.Parameter.Indirect.Actual.Out,
        XPDGEdge.DataDepEdge.Parameter.Indirect.Formal.In,
        XPDGEdge.DataDepEdge.Parameter.Indirect.Formal.Out,
        "Annotation Applications in MZN" 
    };

    pub static ref NODE_IDS: Vec<ID> = ids! {
        PDGNode.Annotation.Global,
        PDGNode.Annotation.Other,
        PDGNode.Annotation.Var,
        "PDGNode.Annotation.Global + PDGNode.Annotation.Other + PDGNode.Annotation.Var",
        PDGNode.Annotation,
        PDGNode.FunctionEntry,
        PDGNode.Inst.Br,
        PDGNode.Inst.FunCall,
        PDGNode.Inst.Other,
        PDGNode.Inst.Ret,
        "PDGNode.Inst.Br + PDGNode.Inst.FunCall + PDGNode.Inst.Other + PDGNode.Inst.Ret",
        PDGNode.Inst,
        PDGNode.Param.ActualIn.Root,
        PDGNode.Param.ActualIn.NonRoot,
        "PDGNode.Param.ActualIn.NonRoot + PDGNode.Param.ActualIn.Root",
        PDGNode.Param.ActualIn,
        PDGNode.Param.ActualOut.Root,
        PDGNode.Param.ActualOut.NonRoot,
        "PDGNode.Param.ActualOut.NonRoot + PDGNode.Param.ActualOut.Root",
        PDGNode.Param.ActualOut,
        PDGNode.Param.FormalIn.Root,
        PDGNode.Param.FormalIn.NonRoot,
        "PDGNode.Param.FormalIn.NonRoot + PDGNode.Param.FormalIn.Root",
        PDGNode.Param.FormalIn,
        PDGNode.Param.FormalOut.Root,
        PDGNode.Param.FormalOut.NonRoot,
        "PDGNode.Param.FormalOut.NonRoot + PDGNode.Param.FormalOut.Root",
        PDGNode.Param.FormalOut,
        "PDGNode.Param.ActualIn + PDGNode.Param.ActualOut + PDGNode.Param.FormalIn + PDGNode.Param.FormalOut",
        PDGNode.Param,
        PDGNode.VarNode.StaticFunction,
        PDGNode.VarNode.StaticGlobal,
        PDGNode.VarNode.StaticModule,
        PDGNode.VarNode.StaticOther,
        "PDGNode.VarNode.StaticFunction + PDGNode.VarNode.StaticGlobal + PDGNode.VarNode.StaticModule + PDGNode.VarNode.StaticOther",
        PDGNode.VarNode,
        "PDGNode.Annotation + PDGNode.FunctionEntry + PDGNode.Inst + PDGNode.Param + PDGNode.VarNode",
        PDGNode
    };

    pub static ref IR_IDS: Vec<ID> = ids! {
        IRAnnoGlobal,
        IRAnnoVar,
        IRFunction,
        IRGlobal.Annotation,
        IRGlobal.External,
        IRGlobal.Internal.Annotation,
        IRGlobal.Internal.Function,
        IRGlobal.Internal.Module,
        IRGlobal.Internal.Omni,
        "IRGlobal.Internal.Annotation + IRGlobal.Internal.Function + IRGlobal.Internal.Module + IRGlobal.Internal.Omni",
        IRGlobal.Internal,
        "IRGlobal.Annotation + IRGlobal.External + IRGlobal.Internal",
        IRGlobal,
        IRInstruction.AShr,
        IRInstruction.Add,
        IRInstruction.Alloca,
        IRInstruction.And,
        IRInstruction.BitCast,
        IRInstruction.Br,
        IRInstruction.Call.Annotation,
        IRInstruction.Call.External.VarArg,
        IRInstruction.Call.External.NonVarArg,
        "IRInstruction.Call.External.NonVarArg + IRInstruction.Call.External.VarArg",
        IRInstruction.Call.External,
        IRInstruction.Call.Internal.NonVarArg,
        IRInstruction.Call.Internal.VarArg,
        "IRInstruction.Call.Internal.NonVarArg + IRInstruction.Call.Internal.VarArg",
        IRInstruction.Call.Internal,
        IRInstruction.Call.Intrinsic,
        IRInstruction.Call.Pointer,
        "IRInstruction.Call.Annotation + IRInstruction.Call.External + IRInstruction.Call.Internal + IRInstruction.Call.Intrinsic + IRInstruction.Call.Pointer",
        IRInstruction.Call,
        IRInstruction.CondBr,
        IRInstruction.ExtractValue,
        IRInstruction.FAdd,
        IRInstruction.FCmp,
        IRInstruction.FDiv,
        IRInstruction.FMul,
        IRInstruction.FNeg,
        IRInstruction.FPExt,
        IRInstruction.FPToSI,
        IRInstruction.FPToUI,
        IRInstruction.FPTrunc,
        IRInstruction.FSub,
        IRInstruction.GetElementPtr,
        IRInstruction.ICmp,
        IRInstruction.IntToPtr,
        IRInstruction.LShr,
        IRInstruction.Load,
        IRInstruction.Mul,
        IRInstruction.Or,
        IRInstruction.Phi,
        IRInstruction.PtrToInt,
        IRInstruction.Ret,
        IRInstruction.SDiv,
        IRInstruction.SExt,
        IRInstruction.SIToFP,
        IRInstruction.SRem,
        IRInstruction.Select,
        IRInstruction.Shl,
        IRInstruction.Store,
        IRInstruction.Sub,
        IRInstruction.Switch,
        IRInstruction.Trunc,
        IRInstruction.UDiv,
        IRInstruction.UIToFP,
        IRInstruction.URem,
        IRInstruction.Unreachable,
        IRInstruction.Xor,
        IRInstruction.ZExt,
        "IRInstruction.AShr + IRInstruction.Add + IRInstruction.Alloca + IRInstruction.And + IRInstruction.BitCast + IRInstruction.Br + IRInstruction.Call + IRInstruction.CondBr + IRInstruction.ExtractValue + IRInstruction.FAdd + IRInstruction.FCmp + IRInstruction.FDiv + IRInstruction.FMul + IRInstruction.FNeg + IRInstruction.FPExt + IRInstruction.FPToSI + IRInstruction.FPToUI + IRInstruction.FPTrunc + IRInstruction.FSub + IRInstruction.GetElementPtr + IRInstruction.ICmp + IRInstruction.IntToPtr + IRInstruction.LShr + IRInstruction.Load + IRInstruction.Mul + IRInstruction.Or + IRInstruction.Phi + IRInstruction.PtrToInt + IRInstruction.Ret + IRInstruction.SDiv + IRInstruction.SExt + IRInstruction.SIToFP + IRInstruction.SRem + IRInstruction.Select + IRInstruction.Shl + IRInstruction.Store + IRInstruction.Sub + IRInstruction.Switch + IRInstruction.Trunc + IRInstruction.UDiv + IRInstruction.UIToFP + IRInstruction.URem + IRInstruction.Unreachable + IRInstruction.Xor + IRInstruction.ZExt",
        IRInstruction,
        IRParameter
    };
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
    id!(IRParameter)
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

fn add_extra_node_subtypes(iset: &mut ISet<ID, Node>) {
    let mut split_by_param_idx = |name: ID| {
        let set = iset.get(&name);
        let set_with_param_idx = 
            set 
                .iter()
                .filter(|n| n.param_idx.is_some())
                .cloned()
                .collect::<HashSet<_>>();
        let set_without_param_idx = 
            set 
                .iter()
                .filter(|n| n.param_idx.is_none())
                .cloned()
                .collect::<HashSet<_>>();
        iset.insert_all(name.right_extend("Root".to_string()), set_with_param_idx);
        iset.insert_all(name.right_extend("NonRoot".to_string()), set_without_param_idx);
    };
    split_by_param_idx(id!(PDGNode.Param.FormalIn));
    split_by_param_idx(id!(PDGNode.Param.FormalOut));
    split_by_param_idx(id!(PDGNode.Param.ActualIn));
    split_by_param_idx(id!(PDGNode.Param.ActualOut));
}

pub trait IndexedSets<A> {
    fn indexed_sets(&self) -> ISet<ID, A>;
}

impl IndexedSets<LLValue> for Module {
    fn indexed_sets(&self) -> ISet<ID, LLValue> {
        let mut iset = ISet::new();
        for id in IR_IDS.clone().into_iter() {
            iset.insert_empty(id);
        }
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
        for id in NODE_IDS.clone().into_iter() {
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
        add_extra_node_subtypes(&mut iset);
        iset
    }
}


impl IndexedSets<Edge> for Pdg {
    fn indexed_sets(&self) -> ISet<ID, Edge> {
        let mut iset = ISet::new();
        for id in EDGE_IDS.clone().into_iter() {
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
