use std::collections::HashSet;
use std::fmt::Display;

use llvm_ir::{Module, Instruction, Function, Operand, HasDebugLoc, Name, Terminator};
use llvm_ir::instruction::Call;

pub fn call_sites(module: &Module) -> Vec<(Function, Call)> {
    module
        .functions
        .iter()
        .flat_map(|f| 
            f
                .basic_blocks
                .iter()
                .flat_map(|b| &b.instrs)
                .filter_map(move |i| if let Instruction::Call(c) = i { Some((f.clone(), c.clone())) } else { None }))
        .collect() 
}

pub fn args_has_debug_info(f: &Function, c: &Call) -> bool {
    let instrs = f.basic_blocks
        .iter().flat_map(|b| &b.instrs).collect::<Vec<_>>();
    let debug_set = {
        let mut set = HashSet::<Name>::new();
        for p in &f.parameters {
            set.insert(p.name.to_owned());
        } 
        for i in instrs {
            if let Some(n) = i.try_get_result() {
                if i.get_debug_loc().is_some() {
                    set.insert(n.to_owned());
                }
            }
        }
        set
    };
            
    c.arguments.iter().all(|(op, _)| {
        match op {
            Operand::LocalOperand { name, ty: _ } => {
                debug_set.get(name).is_some()
            },
            _ => true,
        }
    })
}


#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum LLID {
    GlobalName { global_name: String }, 
    LocalName { global_name: String, local_name: Name }, 
    InstructionID { global_name: String, index: usize },
}

impl Display for LLID {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::GlobalName { global_name } => 
                f.write_fmt(format_args!("@{}", global_name)),
            Self::LocalName { global_name, local_name } =>
                f.write_fmt(format_args!("@{}::{}", global_name, local_name)),
            Self::InstructionID { global_name, index }  => 
                f.write_fmt(format_args!("@{}::{}", global_name, index)),
        }
    }
} 
#[derive(Debug, Clone)]
pub enum LLItem {
    Global(llvm_ir::module::GlobalVariable),
    Function(llvm_ir::function::Function),
    Parameter { function: llvm_ir::function::Function, index: usize, parameter: llvm_ir::function::Parameter },
    BasicBlock(llvm_ir::BasicBlock),
    Instruction(llvm_ir::Instruction),
    Terminator(llvm_ir::Terminator)
}

impl LLItem {
    pub fn to_string(&self) -> String {
        match self {
            LLItem::Global(g) => g.name.to_string(),
            LLItem::Function(f) => f.name.to_string(),
            LLItem::Parameter { function: _, index: _, parameter } => parameter.name.to_string(),
            LLItem::BasicBlock(b) => b.name.to_string(),
            LLItem::Instruction(i) => i.to_string(),
            LLItem::Terminator(t) => t.to_string(),
        }
    }
}

#[derive(Debug, Clone)]
pub struct LLValue {
    pub id: LLID,
    pub item: LLItem
}

impl LLValue {
    pub fn new(id: LLID, item: LLItem) -> LLValue {
        LLValue { id, item } 
    }
}

impl PartialEq for LLValue {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id 
    }
} 

impl Eq for LLValue {}
impl std::hash::Hash for LLValue {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.id.hash(state);
    }
}

impl LLID {

    pub fn global_name(&self) -> &str {
        match self {
            Self::GlobalName { global_name } => global_name,
            Self::LocalName { global_name, local_name: _ } => global_name,
            Self::InstructionID { global_name, index: _ } => global_name,
        }
    }
    pub fn local_name(&self) -> Option<&Name> {
        match self {
            Self::LocalName { global_name: _, local_name } => Some(local_name),
            _ => None,
        }
    }
    pub fn index(&self) -> Option<usize> {
        match self {
            Self::InstructionID { global_name: _, index } => Some(*index),
            _ => None,
        }
    }

}



pub fn instr_name(instr: &Instruction) -> &'static str {
    match instr {
        Instruction::Add(_) => "Add",
        Instruction::Sub(_) => "Sub",
        Instruction::Mul(_) => "Mul",
        Instruction::UDiv(_) => "UDiv",
        Instruction::SDiv(_) => "SDiv",
        Instruction::URem(_) => "URem",
        Instruction::SRem(_) => "SRem",
        Instruction::And(_) => "And",
        Instruction::Or(_) => "Or",
        Instruction::Xor(_) => "Xor",
        Instruction::Shl(_) => "Shl",
        Instruction::LShr(_) => "LShr",
        Instruction::AShr(_) => "AShr",
        Instruction::FAdd(_) => "FAdd",
        Instruction::FSub(_) => "FSub",
        Instruction::FMul(_) => "FMul",
        Instruction::FDiv(_) => "FDiv",
        Instruction::FRem(_) => "FRem",
        Instruction::FNeg(_) => "FNeg",
        Instruction::ExtractElement(_) => "ExtractElement",
        Instruction::InsertElement(_) => "InsertElement",
        Instruction::ShuffleVector(_) => "ShuffleVector",
        Instruction::ExtractValue(_) => "ExtractValue",
        Instruction::InsertValue(_) => "InsertValue",
        Instruction::Alloca(_) => "Alloca",
        Instruction::Load(_) => "Load",
        Instruction::Store(_) => "Store",
        Instruction::Fence(_) => "Fence",
        Instruction::CmpXchg(_) => "CmpXchg",
        Instruction::AtomicRMW(_) => "AtomicRMW",
        Instruction::GetElementPtr(_) => "GetElementPtr",
        Instruction::Trunc(_) => "Trunc",
        Instruction::ZExt(_) => "ZExt",
        Instruction::SExt(_) => "SExt",
        Instruction::FPTrunc(_) => "FPTrunc",
        Instruction::FPExt(_) => "FPExt",
        Instruction::FPToUI(_) => "FPToUI",
        Instruction::FPToSI(_) => "FPToSI",
        Instruction::UIToFP(_) => "UIToFP",
        Instruction::SIToFP(_) => "SIToFP",
        Instruction::PtrToInt(_) => "PtrToInt",
        Instruction::IntToPtr(_) => "IntToPtr",
        Instruction::BitCast(_) => "BitCast",
        Instruction::AddrSpaceCast(_) => "AddrSpaceCast",
        Instruction::ICmp(_) => "ICmp",
        Instruction::FCmp(_) => "FCmp",
        Instruction::Phi(_) => "Phi",
        Instruction::Select(_) => "Select",
        Instruction::Freeze(_) => "Freeze",
        Instruction::Call(_) => "Call",
        Instruction::VAArg(_) => "VAArg",
        Instruction::LandingPad(_) => "LandingPad",
        Instruction::CatchPad(_) => "CatchPad",
        Instruction::CleanupPad(_) => "CleanupPad",
    }
}

pub fn term_name(term: &Terminator) -> &'static str {
    match term {
        Terminator::Ret(_) => "Ret",
        Terminator::Br(_) => "Br",
        Terminator::CondBr(_) => "CondBr",
        Terminator::Switch(_) => "Switch",
        Terminator::IndirectBr(_) => "IndirectBr",
        Terminator::Invoke(_) => "Invoke",
        Terminator::Resume(_) => "Resume",
        Terminator::Unreachable(_) => "Unreachable",
        Terminator::CleanupRet(_) => "CleanupRet",
        Terminator::CatchRet(_) => "CatchRet",
        Terminator::CatchSwitch(_) => "CatchSwitch",
        Terminator::CallBr(_) => "CallBr",
    }
}

pub fn callee_name(call: &Call) -> Option<Name> {
    call.function.clone().right().as_ref().and_then(|op| {
        match op {
            Operand::LocalOperand { name, ty: _ } => Some(name.clone()),
            Operand::ConstantOperand(_) => 
                match op.as_constant().unwrap() {
                    llvm_ir::Constant::GlobalReference { name, ty: _ } => Some(name.clone()),
                    _ => None,
                },
            Operand::MetadataOperand => None,
        }
    }) 
}