use std::collections::HashSet;
use std::fmt::Display;
use std::iter::FromIterator;

use llvm_ir::module::GlobalVariable;
use llvm_ir::{Module, Instruction, Function, Operand, HasDebugLoc, Name, Terminator, Type, TypeRef, Constant};
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

impl LLID {

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
    pub fn function(&self) -> Option<Function> {
        match self {
            LLItem::Function(f) => Some(f.clone()),
            _ => None
        }
    }
    pub fn global(&self) -> Option<GlobalVariable> {
        match self {
            LLItem::Global(g) => Some(g.clone()),
            _ => None
        }
    }
    pub fn call(&self) -> Option<Call> {
        match self {
            LLItem::Instruction(Instruction::Call(c)) => Some(c.clone()),
            _ => None
        }
    }
    pub fn instruction(&self) -> Option<Instruction> {
        match self {
            LLItem::Instruction(i) => Some(i.clone()),
            _ => None
        }
    }
    pub fn call_name_predicate(&self, f: impl Fn(&str) -> bool) -> bool {
        self.call().and_then(|c| 
            c.function.right())
            .map(|o| match o {
                Operand::ConstantOperand(g) => 
                    match g.as_ref() {
                        Constant::GlobalReference { name, ty: _ } => f(&name.to_string()),
                        _ => false
                    },
                    _ => false,
            }).unwrap_or(false)
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
    pub fn global(&self) -> Option<&String> {
        if let LLID::GlobalName { global_name } = self {
            Some(global_name)
        } else {
            None
        }
    }

    pub fn instruction(&self) -> Option<(&String, usize)> {
        if let LLID::InstructionID { global_name, index } = self {
            Some((global_name, *index))
        } else {
            None
        }
    }
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
    pub fn global_name_from_name(name: &Name) -> Self {
        Self::GlobalName { global_name: name.to_string()[1..].to_string() }
    }

    pub fn global_name_from_string(str: String) -> Self {
        Self::GlobalName { global_name: str }
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

pub trait FunctionUsedAsPointer {
    fn funcs_used_as_pointer(&self) -> HashSet<Name>;
} 

impl<T: FunctionUsedAsPointer> FunctionUsedAsPointer for Vec<T> {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        self.iter().flat_map(|e| e.funcs_used_as_pointer()).collect()
    }
}
// impl FunctionUsedAsPointer for llvm_ir::ConstantRef {
//     fn funcs_used_as_pointer(&self) -> HashSet<Name> {
//         self.to_owned().funcs_used_as_pointer()
//     }
// }
#[macro_export]
macro_rules! union {
    ($e:expr, $($f:expr),+) => {
        {
            let mut s = $e;
            $(
                s.extend($f);
            )+
            s 
        }
    };
}

impl FunctionUsedAsPointer for llvm_ir::Module {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        union! {
            self.functions.funcs_used_as_pointer(),
            self.global_vars.funcs_used_as_pointer(),
            self.global_aliases.funcs_used_as_pointer()
        }
    }
}

impl FunctionUsedAsPointer for llvm_ir::module::GlobalAlias {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        self.aliasee.funcs_used_as_pointer()
    }
}
impl FunctionUsedAsPointer for llvm_ir::module::GlobalVariable {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        self.initializer.as_ref().map(|i| i.funcs_used_as_pointer()).unwrap_or(HashSet::new())
    }
}

impl FunctionUsedAsPointer for llvm_ir::Function {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        self.basic_blocks.funcs_used_as_pointer()
    }
}
impl FunctionUsedAsPointer for llvm_ir::BasicBlock {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        union!(self.instrs.funcs_used_as_pointer(), self.term.funcs_used_as_pointer())
    }
}

impl FunctionUsedAsPointer for llvm_ir::Terminator {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        let empty = HashSet::new();
        match self {
            Terminator::Ret(r) => r.return_operand.as_ref().map(|r| r.funcs_used_as_pointer()).unwrap_or(empty),
            Terminator::Br(_) => empty,
            Terminator::CondBr(b) => b.condition.funcs_used_as_pointer(),
            Terminator::Switch(s) => union!(s.operand.funcs_used_as_pointer(), s.dests.iter().flat_map(|(c,_)| c.funcs_used_as_pointer())),
            Terminator::IndirectBr(b) => b.operand.funcs_used_as_pointer(),
            Terminator::Invoke(i) => i.arguments.iter().flat_map(|(o,_)| o.funcs_used_as_pointer()).collect(),
            Terminator::Resume(v) => v.operand.funcs_used_as_pointer(),
            Terminator::Unreachable(_) => empty,
            Terminator::CleanupRet(r) => r.cleanup_pad.funcs_used_as_pointer(),
            Terminator::CatchRet(r) => r.catch_pad.funcs_used_as_pointer(),
            Terminator::CatchSwitch(r) => r.parent_pad.funcs_used_as_pointer(),
            Terminator::CallBr(b) => b.arguments.iter().flat_map(|(o,_)| o.funcs_used_as_pointer()).collect(),
        } 
    }
}

impl FunctionUsedAsPointer for llvm_ir::Instruction {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        let empty = HashSet::new();
        match self {
            // Instruction::Add(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::Sub(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::Mul(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::UDiv(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::SDiv(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::URem(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::SRem(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::And(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::Or(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::Xor(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::Shl(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::LShr(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::AShr(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::FAdd(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::FSub(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::FMul(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::FDiv(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::FRem(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::FNeg(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::ExtractElement(x) => 
            //     union!(x.vector.funcs_used_as_pointer(), x.index.funcs_used_as_pointer()),
            // Instruction::InsertElement(x) =>
            //     union!(x.element.funcs_used_as_pointer(), x.index.funcs_used_as_pointer()),
            // Instruction::ShuffleVector(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::ExtractValue(x) => 
            //     x.aggregate.funcs_used_as_pointer(),
            // Instruction::InsertValue(x) => 
            //     union!(x.element.funcs_used_as_pointer(), x.aggregate.funcs_used_as_pointer()),
            // Instruction::Alloca(_) => empty,
            Instruction::Load(x) => 
                x.address.funcs_used_as_pointer(),
            Instruction::Store(x) => 
                union!(x.address.funcs_used_as_pointer(), x.value.funcs_used_as_pointer()),
            // Instruction::Fence(_) => empty,
            // Instruction::CmpXchg(x) => union!(x.address.funcs_used_as_pointer(), x.expected.funcs_used_as_pointer()),
            // Instruction::AtomicRMW(x) => 
                // union!(x.value.funcs_used_as_pointer(), x.address.funcs_used_as_pointer()),
            Instruction::GetElementPtr(x) => 
                x.address.funcs_used_as_pointer(),
            // Instruction::Trunc(x) => 
                // x.operand.funcs_used_as_pointer(),
            // Instruction::ZExt(x) => 
                // x.operand.funcs_used_as_pointer(),
            // Instruction::SExt(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::FPTrunc(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::FPExt(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::FPToUI(x) =>
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::FPToSI(x) =>
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::UIToFP(x) =>
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::SIToFP(x) =>
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::PtrToInt(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::IntToPtr(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::BitCast(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::AddrSpaceCast(x) => 
            //     x.operand.funcs_used_as_pointer(),
            // Instruction::ICmp(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::FCmp(x) => 
            //     union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
            // Instruction::Phi(x) => 
            //     x.incoming_values.iter().flat_map(|(c,_)| c.funcs_used_as_pointer()).collect(),
            // Instruction::Select(s) => 
            //     union!(s.condition.funcs_used_as_pointer(), s.true_value.funcs_used_as_pointer(), s.false_value.funcs_used_as_pointer()),
            // Instruction::Freeze(f) => 
            //     f.operand.funcs_used_as_pointer(),
            Instruction::Call(c) => 
                c.arguments.iter().flat_map(|(c, _)| c.funcs_used_as_pointer()).collect(),
            // Instruction::VAArg(x) => 
            //     x.arg_list.funcs_used_as_pointer(),
            // Instruction::LandingPad(_) => empty,
            // Instruction::CatchPad(_) => empty,
            // Instruction::CleanupPad(_) => empty,
            _ => empty,
        }
    }
}

impl FunctionUsedAsPointer for llvm_ir::Operand {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        let empty = HashSet::new();
        match self {
            // don't include local names
            Operand::LocalOperand { name: _, ty: _ } => empty,
            Operand::ConstantOperand(c) => c.funcs_used_as_pointer(),
            Operand::MetadataOperand => empty,
        }
    }
}

fn is_fn_pointer(ty: &TypeRef) -> bool {
    match ty.as_ref() {
        Type::PointerType { pointee_type, addr_space: _ } => is_fn_pointer(pointee_type),
        Type::FuncType { result_type: _, param_types: _, is_var_arg: _ } => true,
        _ => false,
    }
}

impl FunctionUsedAsPointer for llvm_ir::Constant {
    fn funcs_used_as_pointer(&self) -> HashSet<Name> {
        let empty = HashSet::new();
        match self {
            llvm_ir::Constant::GlobalReference { name, ty } => {
                if is_fn_pointer(ty) {
                    HashSet::from_iter([name.to_owned()])
                } else {
                    empty
                }
            },
            llvm_ir::Constant::Struct { name: _, values, is_packed: _ } => 
                values.iter().flat_map(|c| c.funcs_used_as_pointer()).collect::<HashSet<_>>(),
            llvm_ir::Constant::Array { element_type: _, elements } => 
                elements.iter().flat_map(|c| c.funcs_used_as_pointer()).collect::<HashSet<_>>(),
            llvm_ir::Constant::Vector(elements) =>
                elements.iter().flat_map(|c| c.funcs_used_as_pointer()).collect::<HashSet<_>>(),
            llvm_ir::Constant::BitCast(x) => 
                x.operand.funcs_used_as_pointer(),
            _ => empty
        }
        // match self {
        //     llvm_ir::Constant::Int { bits, value } => empty,
        //     llvm_ir::Constant::Float(_) => empty,
        //     llvm_ir::Constant::Null(_) => empty,
        //     llvm_ir::Constant::AggregateZero(_) => empty,
            // llvm_ir::Constant::Struct { name: _, values, is_packed: _ } => 
                // values.funcs_used_as_pointer(),
        //     llvm_ir::Constant::Array { element_type: _, elements } => 
        //         elements.funcs_used_as_pointer(),
        //     llvm_ir::Constant::Vector(elements) =>
        //         elements.funcs_used_as_pointer(),
        //     llvm_ir::Constant::Undef(_) => empty,
        //     llvm_ir::Constant::BlockAddress => empty,
        //     llvm_ir::Constant::GlobalReference { name, ty } => HashSet::from_iter([name.to_owned()]),
        //     llvm_ir::Constant::TokenNone => empty,
        //     llvm_ir::Constant::Add(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::Sub(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::Mul(x) =>
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::UDiv(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::SDiv(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::URem(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::SRem(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::And(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::Or(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::Xor(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::Shl(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::LShr(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::AShr(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::FAdd(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::FSub(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::FMul(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::FDiv(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::FRem(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::ExtractElement(e) => 
        //         union!(e.index.funcs_used_as_pointer(), e.vector.funcs_used_as_pointer()),
        //     llvm_ir::Constant::InsertElement(e) => 
        //         union!(e.index.funcs_used_as_pointer(), e.vector.funcs_used_as_pointer(), e.element.funcs_used_as_pointer()),
        //     llvm_ir::Constant::ShuffleVector(s) => 
        //         union!(s.mask.funcs_used_as_pointer(), s.operand0.funcs_used_as_pointer(), s.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::ExtractValue(e) => 
        //         e.aggregate.funcs_used_as_pointer(),
        //     llvm_ir::Constant::InsertValue(i) => 
        //         union!(i.aggregate.funcs_used_as_pointer(), i.element.funcs_used_as_pointer()),
        //     llvm_ir::Constant::GetElementPtr(g) => 
        //         union!(g.address.funcs_used_as_pointer(), g.indices.funcs_used_as_pointer()),
        //     llvm_ir::Constant::Trunc(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::ZExt(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::SExt(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::FPTrunc(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::FPExt(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::FPToUI(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::FPToSI(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::UIToFP(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::SIToFP(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::PtrToInt(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::IntToPtr(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::BitCast(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::AddrSpaceCast(x) => 
        //         x.operand.funcs_used_as_pointer(),
        //     llvm_ir::Constant::ICmp(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::FCmp(x) => 
        //         union!(x.operand0.funcs_used_as_pointer(), x.operand1.funcs_used_as_pointer()),
        //     llvm_ir::Constant::Select(s) => 
        //         union!(s.condition.funcs_used_as_pointer(), s.true_value.funcs_used_as_pointer(), s.false_value.funcs_used_as_pointer())
        // }
    }
} 

pub trait Names {
    fn names(&self) -> HashSet<Name>;
} 

impl Names for Name {
    fn names(&self) -> HashSet<Name> {
        HashSet::from_iter([self.to_owned()])
    }
}

impl Names for Constant {
    fn names(&self) -> HashSet<Name> {
        let empty = HashSet::new();
        match self {
            Constant::Int { bits: _, value: _ } => empty,
            Constant::Float(_) => empty,
            Constant::Null(_) => empty,
            Constant::AggregateZero(_) => empty,
            Constant::Struct { name: _, values, is_packed: _ } => values.iter().flat_map(|x| x.names()).collect(),
            Constant::Array { element_type: _, elements } => elements.iter().flat_map(|x| x.names()).collect(),
            Constant::Vector(v) => v.iter().flat_map(|x| x.names()).collect(),
            Constant::Undef(_) => empty,
            Constant::BlockAddress => empty,
            Constant::GlobalReference { name, ty: _ } => HashSet::from_iter([name.to_owned()]),
            Constant::TokenNone => empty,
            Constant::Add(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::Sub(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::Mul(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::UDiv(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::SDiv(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::URem(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::SRem(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::And(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::Or(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::Xor(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::Shl(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::LShr(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::AShr(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::FAdd(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::FSub(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::FMul(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::FDiv(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::FRem(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::ExtractElement(x) => union!(x.vector.names(), x.index.names()),
            Constant::InsertElement(x) => union!(x.vector.names(), x.element.names(), x.index.names()),
            Constant::ShuffleVector(x) => union!(x.mask.names(), x.operand0.names(), x.operand1.names()),
            Constant::ExtractValue(x) => x.aggregate.names(),
            Constant::InsertValue(x) => union!(x.element.names(), x.aggregate.names()),
            Constant::GetElementPtr(x) => union!(x.address.names(), x.indices.iter().flat_map(|x| x.names())),
            Constant::Trunc(x) => x.operand.names(),
            Constant::ZExt(x) => x.operand.names(),
            Constant::SExt(x) => x.operand.names(),
            Constant::FPTrunc(x) => x.operand.names(),
            Constant::FPExt(x) => x.operand.names(),
            Constant::FPToUI(x) => x.operand.names(),
            Constant::FPToSI(x) => x.operand.names(),
            Constant::UIToFP(x) => x.operand.names(),
            Constant::SIToFP(x) => x.operand.names(),
            Constant::PtrToInt(x) => x.operand.names(),
            Constant::IntToPtr(x) => x.operand.names(),
            Constant::BitCast(x) => x.operand.names(),
            Constant::AddrSpaceCast(x) => x.operand.names(),
            Constant::ICmp(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::FCmp(x) => union!(x.operand0.names(), x.operand1.names()),
            Constant::Select(x) => union!(x.condition.names(), x.false_value.names(), x.true_value.names()),
        } 
    }
}

impl Names for Operand {
    fn names(&self) -> HashSet<Name> {
        match self {
            Operand::LocalOperand { name, ty: _ } => HashSet::from_iter([name.to_owned()]),
            Operand::ConstantOperand(c) => c.names(),
            Operand::MetadataOperand => HashSet::new(),
        }
    }
}

impl Names for Instruction {
    fn names(&self) -> HashSet<Name> {
        let empty: HashSet<Name> = HashSet::new();
        match self {
            Instruction::Add(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::Sub(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::Mul(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::UDiv(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::SDiv(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::URem(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::SRem(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::And(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::Or(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::Xor(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::Shl(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::LShr(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::AShr(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::FAdd(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::FSub(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::FMul(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::FDiv(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::FRem(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::FNeg(x) => x.operand.names(),
            Instruction::ExtractElement(x) => union!(x.vector.names(), x.index.names()),
            Instruction::InsertElement(x) => union!(x.vector.names(), x.element.names(), x.index.names()),
            Instruction::ShuffleVector(x) => union!(x.mask.names(), x.operand0.names(), x.operand1.names()),
            Instruction::ExtractValue(x) => x.aggregate.names(),
            Instruction::InsertValue(x) => union!(x.element.names(), x.aggregate.names()),
            Instruction::Alloca(x) => HashSet::from_iter([x.dest.to_owned()]),
            Instruction::Load(x) => union!(x.address.names(), HashSet::<Name>::from_iter([x.dest.to_owned()])),
            Instruction::Store(x) => union!(x.value.names(), x.address.names()),
            Instruction::Fence(_) => empty,
            Instruction::CmpXchg(x) => union!(x.address.names(), x.expected.names(), x.replacement.names(), x.dest.names()),
            Instruction::AtomicRMW(x) => union!(x.address.names(), x.value.names(), x.dest.names()), 
            Instruction::GetElementPtr(x) => union!(x.address.names(), x.indices.iter().flat_map(|x| x.names()), x.dest.names()),
            Instruction::Trunc(x) => union!(x.dest.names(), x.operand.names()), 
            Instruction::ZExt(x) => union!(x.dest.names(), x.operand.names()), 
            Instruction::SExt(x) => union!(x.dest.names(), x.operand.names()), 
            Instruction::FPTrunc(x) => union!(x.dest.names(), x.operand.names()), 
            Instruction::FPExt(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::FPToUI(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::FPToSI(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::UIToFP(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::SIToFP(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::PtrToInt(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::IntToPtr(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::BitCast(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::AddrSpaceCast(x) => x.operand.names(),
            Instruction::ICmp(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::FCmp(x) => union!(x.operand0.names(), x.operand1.names()),
            Instruction::Phi(x) => union!(x.dest.names(), x.incoming_values.iter().flat_map(|(op, n)| union!(op.names(), n.names()))),
            Instruction::Select(x) => union!(x.condition.names(), x.false_value.names(), x.true_value.names()),
            Instruction::Freeze(x) => union!(x.dest.names(), x.operand.names()),
            Instruction::Call(x) => union!(x.dest.as_ref().map(|x| x.names()).unwrap_or(empty), x.arguments.iter().flat_map(|(op, _)| op.names()), x.function.as_ref().right().map(|x| x.names()).unwrap_or(HashSet::new())),
            Instruction::VAArg(x) => union!(x.dest.names(), x.arg_list.names()),
            Instruction::LandingPad(x) => x.dest.names(),
            Instruction::CatchPad(x) => union!(x.dest.names(), x.args.iter().flat_map(|x| x.names()), x.catch_switch.names()),
            Instruction::CleanupPad(x) => union!(x.dest.names(), x.args.iter().flat_map(|x| x.names()), x.parent_pad.names()),
        }
    }
} 

impl Names for Terminator {
    fn names(&self) -> HashSet<Name> {
        match self {
            Terminator::Ret(x) => x.return_operand.as_ref().map(|x| x.names()).unwrap_or_default(),
            Terminator::Br(x) => x.dest.names(),
            Terminator::CondBr(x) => union!(x.condition.names(), x.false_dest.names(), x.true_dest.names()),
            Terminator::Switch(x) => union!(x.operand.names(), x.default_dest.names(), x.dests.iter().flat_map(|(c, n)| union!(c.names(), n.names()))),
            Terminator::IndirectBr(x) => union!(x.operand.names(), x.possible_dests.iter().map(|x| x.clone())),
            Terminator::Invoke(x) => x.function.as_ref().right().map(|x| x.names()).unwrap_or_default(),
            Terminator::Resume(x) => x.operand.names(),
            Terminator::Unreachable(_) => Default::default(),
            Terminator::CleanupRet(x) => union!(x.cleanup_pad.names(), x.unwind_dest.as_ref().map(|x| x.names()).unwrap_or_default()),
            Terminator::CatchRet(x) => union!(x.catch_pad.names(), x.successor.names()),
            Terminator::CatchSwitch(x) => union!(x.result.names(), x.parent_pad.names(), x.catch_handlers.clone(), x.default_unwind_dest.as_ref().map(|x| x.names()).unwrap_or_default()),
            Terminator::CallBr(x) => union!{
                x.function.as_ref().right().map(|x| x.names()).unwrap_or_default(),
                x.arguments.iter().flat_map(|(op, _)| op.names()),
                x.result.names(),
                x.return_label.names()
            },
        }
    }
} 

pub trait Users {
    fn users(&self, func: &Function, global: bool) -> Vec<LLValue>;
} 

impl Users for Name {
    fn users(&self, func: &Function, global: bool) -> Vec<LLValue> {
        let mut assigned = HashSet::<&Name>::from_iter(func.parameters.iter().map(|p| &p.name)).contains(self) || global;
        let mut users = Vec::new();
        enum Either<A, B> {
            Left(A),
            Right(B)
        }
        let all_instructions_terminators = 
            func.basic_blocks
                .iter()
                .flat_map(|b| 
                    b.instrs.iter().map(|x| Either::Left(x)).chain([Either::Right(&b.term)]));
        let mut count = 0;
        for inst in all_instructions_terminators {
            let id = LLID::InstructionID { global_name: func.name.clone(), index: count };
            match inst {
                Either::Left(inst) => {
                    if assigned {
                        let used = inst.names().contains(self);
                        if used {
                            let item = LLItem::Instruction(inst.clone());
                            users.push(LLValue { id, item });
                        }
                    }
                    assigned = assigned || match inst.try_get_result() {
                        Some(n) => self == n,
                        None => false
                    };
                },
                Either::Right(term) => {
                    if assigned {
                        let used = term.names().contains(self);
                        if used {
                            let item = LLItem::Terminator(term.clone());
                            users.push(LLValue { id, item });
                        }
                    }

                }
            }
            count += 1;
        }
        users
    }
} 

impl Users for Instruction {
    fn users(&self, func: &Function, global: bool) -> Vec<LLValue> {
        self.try_get_result().map(|x| x.users(func, global)).unwrap_or_default()
    }
}