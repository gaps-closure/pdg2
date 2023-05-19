use std::{collections::{HashSet}};

use llvm_ir::{Constant};

use crate::{
    accounting::Account,
    id,
    id::ID,
    indexed_set::ISet,
    llvm::{LLValue, Names, LLID, LLEdge},
    pdg::{Edge, Pdg},
    report::Report,
    union,
};



fn edge_ids<'a>(iter: impl IntoIterator<Item = &'a Edge>, pdg: &'a Pdg) -> HashSet<LLEdge> {
    iter.into_iter()
        .map(|e| pdg.from_edge(&e))
        .map(|(src, dst)| LLEdge(pdg.llid(src).unwrap(), pdg.llid(dst).unwrap()))
        .collect()
}

fn account_anno_var(
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    ir_edge_iset: &ISet<ID, LLEdge>
) -> Account<LLEdge> {
    let ir_anno_var = ir_edge_iset.get(&id!(IRAnnoVar)); 
    let ir_anno_var_fn = ir_anno_var.iter().map(|LLEdge(_, dst)| {
        LLEdge(
            LLID::GlobalName {
                global_name: dst.global_name().to_owned(),
            },
            dst.clone(),
        )
    });
    let all_anno_var = union!(ir_anno_var.clone(), ir_anno_var_fn);
    let anno_var_edge = edge_ids(edge_iset.get(&id!(PDGEdge.Anno.Var)), pdg);
    Account {
        a: anno_var_edge,
        b: all_anno_var,
    }
}

fn account_anno_global(
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLEdge> {
    let global_annotation = ir_iset
        .get(&id!(IRGlobal.Internal.Annotation))
        .iter()
        .next();
    let init = global_annotation.map(|x| 
        x.item
        .global()
        .unwrap()
        .initializer
        .unwrap());
    let srcs = if let Some(Constant::Array {
        element_type: _,
        elements,
    }) = init.map(|x| x.as_ref().to_owned())
    {
        Some(elements.into_iter().flat_map(|c| {
            if let Constant::Struct {
                name: _,
                values,
                is_packed: _,
            } = c.as_ref()
            {
                values[0].names()
            } else {
                unreachable!()
            }
        }))
    } else {
        None 
    };
    let ir_derived_edges = srcs.into_iter().flat_map(move |iter| iter.map(move |name| {
        LLEdge(
            LLID::global_name_from_name(&name),
            global_annotation.unwrap().id.clone(),
        )
    }));
    let anno_global_edges = edge_ids(edge_iset.get(&id!(PDGEdge.Anno.Global)), pdg);
    Account {
        a: anno_global_edges,
        b: ir_derived_edges.collect(),
    }
}

fn account_for_anno_other(
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    _ir_iset: &ISet<ID, LLValue>,
) -> Account<LLEdge> {
    let pdg_varnode_global = edge_ids(edge_iset.get(&id!(PDGNode.Anno.Other)), pdg);
    Account {
        a: pdg_varnode_global,
        b: HashSet::new(),
    }
}

fn account_for_callinv(
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLEdge> {
    // let ir_funs = ir_iset.get(&id!(IRFunction)).iter().map(|v| (v.id.clone(), v)).collect::<HashMap<_,_>>();
    let ir_internal_calls = ir_iset
        .get(&id!(IRInstruction.Call.Internal))
        .iter()
        .filter_map(|v| {
            v.item
                .constant_call_name()
                .map(|x| LLEdge(v.id.clone(), LLID::global_name_from_name(&x)))
        })
        .collect::<HashSet<LLEdge>>();
    let callinv_edges = edge_ids(edge_iset.get(&id!(PDGEdge.ControlDep.CallInv)), pdg);
    Account {
        a: callinv_edges,
        b: ir_internal_calls,
    }
}

fn account_for_ret_edges(
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    _ir_iset: &ISet<ID, LLValue>,
    ir_edge_iset: &ISet<ID, LLEdge>
) -> Account<LLEdge> {
    let ir_rets = ir_edge_iset.get(&id!(IRRets)); 
    let pdg_ret_edges = edge_iset.get(&id!(PDGEdge.ControlDep.CallRet));
    Account {
        a: edge_ids(pdg_ret_edges, pdg),
        b: ir_rets.clone(),
    }
}

pub fn account_for_alias(pdg: &Pdg, edge_iset: &ISet<ID, Edge>, ir_edge_iset: &ISet<ID, LLEdge>) -> Account<LLEdge> {
    let ir_alias_edges = ir_edge_iset.get(&id!(IRAlias));
    let pdg_alias_edges = edge_iset.get(&id!(PDGEdge.DataDepEdge.Alias));
    Account {
        a: edge_ids(pdg_alias_edges, pdg),
        b: ir_alias_edges.clone()
    }
}


fn account_for_defuse(pdg: &Pdg, edge_iset: &ISet<ID, Edge>, ir_edge_iset: &ISet<ID, LLEdge>) -> Account<LLEdge> {
    let ir_defuse_edges = ir_edge_iset.get(&id!(IRDefUse));
    let pdg_defuse_edges = edge_iset.get(&id!(PDGEdge.DataDepEdge.DefUse));
    Account {
        a: edge_ids(pdg_defuse_edges, pdg),
        b: ir_defuse_edges.clone()
    }
}

// fn account_for_controldep_other(pdg: &Pdg, edge_iset: &ISet<ID, Edge>) -> Account<LLEdge> {
//     Account {
//         a: edge_ids(edge_iset.get(&id!(PDGEdge.ControlDep.Other))) 
//     }
// }


pub fn report_all_accounts(
    report: &mut Report,
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    ir_iset: &ISet<ID, LLValue>,
    ir_edge_iset: &ISet<ID, LLEdge>,
) {
    let acct_anno_var = account_anno_var(pdg, &edge_iset, ir_edge_iset);
    report.report_account("PDGEdge.Anno.Var", "IRAnnoVar", acct_anno_var);

    // TODO: Fix account_anno_global to use ir_edge_iset
    let acct_anno_global = account_anno_global(pdg, edge_iset, ir_iset);
    report.report_account("PDGEdge.Anno.Global", "IRAnnoGlobal", acct_anno_global);
    report.report_account(
        "PDGEdge.Anno.Other",
        "Empty",
        account_for_anno_other(pdg, &edge_iset, &ir_iset),
    );
    report.report_account(
        "PDGEdge.ControlDep.CallInv",
        "IRInstruction.Call.Internal",
        account_for_callinv(pdg, &edge_iset, &ir_iset),
    );
    let acct_ret = account_for_ret_edges(pdg, &edge_iset, &ir_iset, ir_edge_iset);
    report.report_account(
        "PDGEdge.ControlDep.CallRet",
        "IRRets",
        acct_ret
    );
    let alias_account = account_for_alias(pdg, edge_iset, ir_edge_iset);
    report.report_account("PDGEdge.DataDepEdge.Alias", "IRAlias", alias_account);

    let defuse_account = account_for_defuse(pdg, edge_iset, ir_edge_iset);
    report.report_account("PDGEdge.DataDepEdge.DefUse", "IRDefUse", defuse_account);
}

