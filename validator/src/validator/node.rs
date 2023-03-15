use std::collections::HashSet;

use crate::{
    accounting::Account,
    id,
    id::ID,
    indexed_set::ISet,
    llvm::{LLValue, LLID},
    pdg::{Node, Pdg},
    report::Report,
    union,
};

use super::util::ir_ids;

fn node_ids<'a>(iter: impl IntoIterator<Item = &'a Node>, pdg: &Pdg) -> HashSet<LLID> {
    iter
        .into_iter()
        .map(|n| pdg.llid(n).unwrap())
        .collect()
}

fn root_param_nodes<'a>(node_iset: &'a ISet<ID, Node>) -> impl Iterator<Item = &'a Node> {
    node_iset
        .get(&id!(PDGNode.Param.FormalIn))
        .iter()
        .filter(|n| n.param_idx.is_some())
}

fn account_for_functions(
    pdg: &Pdg,
    node_iset: &ISet<ID, Node>,
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLID> {
    let node_set = node_ids(node_iset
        .get(&id!(PDGNode.FunctionEntry)), pdg);
    let ir_set = ir_ids(ir_iset.get(&id!(IRFunction)));
    Account {
        a: node_set,
        b: ir_set,
    }
}
fn account_for_funcalls(
    pdg: &Pdg,
    node_iset: &ISet<ID, Node>,
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLID> {
    let node_set = node_ids(node_iset
        .get(&id!(PDGNode.Inst.FunCall)), pdg);
    let ir_calls = ir_ids(ir_iset
        .get(&id!(IRInstruction.Call))
        .difference(ir_iset.get(&id!(IRInstruction.Call.Annotation))));
    Account {
        a: node_set,
        b: ir_calls,
    }
}

fn account_for_inst_other(
    pdg: &Pdg,
    node_iset: &ISet<ID, Node>,
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLID> {
    let ir_insts = ir_iset.get(&id!(IRInstruction));
    let ir_funcalls = ir_iset.get(&id!(IRInstruction.Call));
    let ir_ret = ir_iset.get(&id!(IRInstruction.Ret));
    let ir_br = ir_iset.get(&id!(IRInstruction.Br));
    let ir_condbr = ir_iset.get(&id!(IRInstruction.CondBr));
    let union = union!(
        ir_funcalls.clone(),
        ir_ret.clone(),
        ir_br.clone(),
        ir_condbr.clone()
    );
    let ir_subtraction = ir_insts.difference(&union);

    Account {
        a: node_ids(node_iset
            .get(&id!(PDGNode.Inst.Other)), pdg),
        b: ir_ids(ir_subtraction), 
    }
}

fn account_for_inst_ret(
    pdg: &Pdg,
    node_iset: &ISet<ID, Node>,
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLID> {
    let node_rets = node_ids(node_iset.get(&id!(PDGNode.Inst.Ret)), pdg);
    let ir_rets = ir_ids(ir_iset.get(&id!(IRInstruction.Ret)));
    Account {
        a: node_rets,
        b: ir_rets
    }
}

fn account_for_inst_br(pdg: &Pdg, node_iset: &ISet<ID, Node>, ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let ir_br = ir_iset.get(&id!(IRInstruction.Br));
    let ir_cond_br = ir_iset.get(&id!(IRInstruction.CondBr));
    let ir_br = ir_br.iter().chain(ir_cond_br);
    let pdg_br = node_ids(node_iset.get(&id!(PDGNode.Inst.Br)), pdg);
    Account {
        a: ir_ids(ir_br), 
        b: pdg_br
    }
}

fn account_for_root_param_nodes(pdg: &Pdg, node_iset: &ISet<ID, Node>, ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let root_param_nodes = root_param_nodes(node_iset);
    let ir_params = ir_iset.get(&id!(IRParameter)); 
    Account {
        a: node_ids(root_param_nodes, pdg),
        b: ir_ids(ir_params)
    }
}

fn account_for_annotation_var(pdg: &Pdg, node_iset: &ISet<ID, Node>, ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let ir_anno_var = ir_iset.get(&id!(IRInstruction.Call.Annotation));
    let pdg_anno_var = node_iset
        .get(&id!(PDGNode.Annotation.Var));
    Account {
        a: node_ids(pdg_anno_var, pdg), 
        b: ir_ids(ir_anno_var),
    }
}

fn account_for_annotation_global(pdg: &Pdg, node_iset: &ISet<ID, Node>, ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let ir_anno_glob = ir_iset.get(&id!(IRGlobal.Internal.Annotation));
    let pdg_anno_glob = node_iset
        .get(&id!(PDGNode.Annotation.Global));
    Account {
        a: node_ids(pdg_anno_glob, pdg), 
        b: ir_ids(ir_anno_glob), 
    }
}

fn account_for_static_module_var(pdg: &Pdg, node_iset: &ISet<ID, Node>, ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let ir_global_omni = ir_iset.get(&id!(IRGlobal.Internal.Omni));
    let pdg_varnode_global = node_iset
        .get(&id!(PDGNode.VarNode.StaticGlobal));
    Account {
        a: node_ids(pdg_varnode_global, pdg),
        b: ir_ids(ir_global_omni)
    }
}

fn account_for_static_function_var(pdg: &Pdg, node_iset: &ISet<ID, Node>, ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let ir_global_function = ir_iset.get(&id!(IRGlobal.Internal.Function));
    let pdg_varnode_function = node_iset
        .get(&id!(PDGNode.VarNode.StaticFunction));
    Account {
        a: node_ids(pdg_varnode_function, pdg),
        b: ir_ids(ir_global_function)
    }

}

fn account_for_static_global_var(pdg: &Pdg, node_iset: &ISet<ID, Node>, ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let ir_internal_module = ir_iset.get(&id!(IRGlobal.Internal.Module));
    let pdg_varnode_static = node_iset
        .get(&id!(PDGNode.VarNode.StaticModule));
    Account {
        a: node_ids(pdg_varnode_static, pdg),
        b: ir_ids(ir_internal_module)
    }
}

fn account_for_annotation_other(pdg: &Pdg, node_iset: &ISet<ID, Node>, _ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let annotation_other = node_iset
        .get(&id!(PDGNode.Annotation.Other));
    Account {
        a: node_ids(annotation_other, pdg),
        b: HashSet::new() 
    }
}
fn account_for_static_other(pdg: &Pdg, node_iset: &ISet<ID, Node>, _ir_iset: &ISet<ID, LLValue>) -> Account<LLID> {
    let annotation_other = node_iset
        .get(&id!(PDGNode.VarNode.StaticOther));
    Account {
        a: node_ids(annotation_other, pdg),
        b: HashSet::new() 
    }
}





pub fn report_all_accounts(
    report: &mut Report,
    pdg: &Pdg,
    node_iset: &ISet<ID, Node>,
    ir_iset: &ISet<ID, LLValue>,
) {
    report.report_account(
        "PDGNode.FunctionEntry",
        "IRFunction",
        account_for_functions(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Inst.FunCall",
        "IRInstruction.Call - IRInstruction.Call.Annotation",
        account_for_funcalls(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Inst.Other", 
        "IRInstruction - IRInstruction.Call - IRInstruction.Ret - IRInstruction.Br - IRInstruction.CondBr", 
        account_for_inst_other(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Inst.Ret", 
        "IRInstruction.Ret",
        account_for_inst_ret(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Inst.Br",
        "IRInstruction.Br + IRInstruction.CondBr",
        account_for_inst_br(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Param.FormalIn.Root",
        "IRParameter",
        account_for_root_param_nodes(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Annotation.Var",
        "IRInstruction.Call.Annotation",
        account_for_annotation_var(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Annotation.Global",
        "IRGlobal.Internal.Annotation",
        account_for_annotation_global(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.Annotation.Other",
        "Empty",
        account_for_annotation_other(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.VarNode.StaticGlobal",
        "IRGlobal.Internal.Omni",
        account_for_static_global_var(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.VarNode.StaticFunction",
        "IRGlobal.Internal.Function",
        account_for_static_function_var(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.VarNode.StaticModule",
        "IRGlobal.Internal.Module",
        account_for_static_module_var(pdg, node_iset, ir_iset),
    );
    report.report_account(
        "PDGNode.VarNode.StaticOther",
        "Empty",
        account_for_annotation_other(pdg, node_iset, ir_iset),
    );
}

