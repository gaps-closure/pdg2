use std::collections::HashSet;

use lazy_static::lazy_static;

use crate::{id::ID, id, llvm::{LLValue, LLID}};

#[macro_export]
macro_rules! id_pairs {
    ($($($x:tt).+ & $($y:tt).+),+) => {
        vec![$((id!($($x).+), id!($($y).+))),+]
    };
}

lazy_static! {
    pub static ref RECONCILIATIONS: Vec<(ID, ID)> = id_pairs! {
        PDGNode.Annotation.Global & IRGlobal.Annotation,
        PDGNode.Annotation.Other & "Empty",
        PDGNode.Annotation.Var & IRInstruction.Call.Annotation,
        PDGNode.Annotation & "N/A",
        PDGNode.FunctionEntry & IRFunction,
        PDGNode.Inst.Br & "IRInstruction.Br + IRInstruction.CondBr",
        PDGNode.Inst.FunCall & "IRInstruction.Call - IRInstruction.Call.Annotation",
        PDGNode.Inst.Other & "IRInstruction - IRInstruction.Call - IRInstruction.Ret - IRInstruction.Br - IRInstruction.CondBr",
        PDGNode.Inst.Ret & IRInstruction.Ret,
        PDGNode.Inst & "N/A",
        PDGNode.Param.ActualIn.Root & "N/A",
        PDGNode.Param.ActualIn.NonRoot & "N/A",
        PDGNode.Param.ActualIn & "N/A",
        PDGNode.Param.ActualOut.Root & "N/A",
        PDGNode.Param.ActualOut.NonRoot & "N/A",
        PDGNode.Param.ActualOut & "N/A",
        PDGNode.Param.FormalIn.Root & IRParameter,
        PDGNode.Param.FormalIn.NonRoot & "N/A",
        PDGNode.Param.FormalIn & "N/A",
        PDGNode.Param.FormalOut.Root & IRParameter,
        PDGNode.Param.FormalOut.NonRoot & "N/A",
        PDGNode.Param.FormalOut & "N/A",
        PDGNode.Param & "N/A",
        PDGNode.VarNode.StaticFunction & IRGlobal.Internal.Function,
        PDGNode.VarNode.StaticGlobal & IRGlobal.Internal.Omni,
        PDGNode.VarNode.StaticModule & IRGlobal.Internal.Module,
        PDGNode.VarNode.StaticOther & "Empty",
        PDGNode.VarNode & "N/A",
        PDGNode & "N/A",
        PDGEdge.Anno.Global & IRAnnoGlobal,
        PDGEdge.Anno.Other & "Empty",
        PDGEdge.Anno.Var & IRAnnoVar,
        PDGEdge.Anno & "N/A",
        PDGEdge.ControlDep.Br & "N/A",
        PDGEdge.ControlDep.CallInv & IRInstruction.Call.Internal,
        PDGEdge.ControlDep.CallRet & "N/A",
        PDGEdge.ControlDep.Entry & "N/A",
        PDGEdge.ControlDep.Other & "N/A",
        PDGEdge.ControlDep & "N/A",
        PDGEdge.DataDepEdge.Alias & "N/A",
        PDGEdge.DataDepEdge.DefUse & "N/A",
        PDGEdge.DataDepEdge.RAW & IRRAW,
        PDGEdge.DataDepEdge.Ret & "N/A",
        PDGEdge.DataDepEdge & "N/A",
        PDGEdge.Parameter.Field & "N/A",
        PDGEdge.Parameter.In & "N/A",
        PDGEdge.Parameter.Out & "N/A",
        PDGEdge.Parameter & "N/A",
        PDGEdge & "N/A",
        XPDGEdge.ControlDep.CallInv.ViaExternal & "N/A",
        XPDGEdge.ControlDep.CallInv.Indirect & "N/A",
        XPDGEdge.ControlDep.CallRet.Indirect & "N/A",
        XPDGEdge.DataDepEdge.ViaExternal & "N/A",
        XPDGEdge.DataDepEdge.Indirect.Ret & "N/A",
        XPDGEdge.DataDepEdge.Indirect.Raw & "N/A",
        XPDGEdge.DataDepEdge.Indirect.DefUse & "N/A",
        XPDGEdge.DataDepEdge.Indirect & "N/A",
        XPDGEdge.DataDepEdge.Parameter.Indirect.Actual.In & "N/A",
        XPDGEdge.DataDepEdge.Parameter.Indirect.Actual.Out & "N/A",
        XPDGEdge.DataDepEdge.Parameter.Indirect.Formal.In & "N/A",
        XPDGEdge.DataDepEdge.Parameter.Indirect.Formal.Out & "N/A",
        "Annotation Applications in MZN" & "N/A"
    };
}

pub fn ir_ids<'a>(iter: impl IntoIterator<Item = &'a LLValue>) -> HashSet<LLID> {
    iter
        .into_iter()
        .map(|x| &x.id)
        .cloned()
        .collect()
}