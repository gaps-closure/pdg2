use std::{collections::{HashMap, HashSet}, fmt::Display};

use llvm_ir::{Constant, Instruction};

use crate::{
    accounting::Account,
    id,
    id::ID,
    indexed_set::ISet,
    llvm::{LLValue, Names, Users, LLID},
    pdg::{Edge, Pdg},
    report::Report,
    union, alias::{svf::alias_edges, util::Binder},
};

#[derive(Debug,Clone,PartialEq,Eq,Hash,PartialOrd,Ord)]
pub struct LLEdge(LLID, LLID);

impl Display for LLEdge {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!("{} --> {}", self.0, self.1))
    }
}

fn edge_ids<'a>(iter: impl IntoIterator<Item = &'a Edge>, pdg: &'a Pdg) -> HashSet<LLEdge> {
    iter.into_iter()
        .map(|e| pdg.from_edge(&e))
        .map(|(src, dst)| LLEdge(pdg.llid(src).unwrap(), pdg.llid(dst).unwrap()))
        .collect()
}

#[allow(dead_code)]
struct DefUseEdges {
    anno_var: HashSet<(LLValue, LLValue)>,
    anno_global: HashSet<(LLValue, LLValue)>,
    intra_intrinsic: HashSet<(LLValue, LLValue)>,
    intra_nonintrinsic: HashSet<(LLValue, LLValue)>,
    inter_intrinsic: HashSet<(LLValue, LLValue)>,
    inter_nonintrinsic: HashSet<(LLValue, LLValue)>,
}

fn ir_defuse_edges(ir_iset: &ISet<ID, LLValue>) -> DefUseEdges {
    let functions = ir_iset
        .get(&id!(IRFunction))
        .iter()
        .map(|v| (v.id.global_name().to_string(), v.item.function().unwrap()))
        .collect::<HashMap<_, _>>();

    let insts = ir_iset
        .get(&id!(IRInstruction))
        .iter()
        .filter_map(|v| v.item.instruction().map(|i| (v, v.id.clone(), i)))
        .collect::<Vec<_>>();

    let intra_uses = insts
        .iter()
        .flat_map(|(v, id, inst)| {
            inst.users(functions.get(id.global_name()).unwrap(), false)
                .iter()
                .map(|v2| (v.clone(), v2.clone()))
                .collect::<Vec<_>>()
        })
        .collect::<HashSet<_>>();

    let is_anno_var = |x: &str| x.contains("llvm.var.annotation");
    let is_anno_global = |x: &str| x.contains("llvm.global.annotation");
    let is_annotation = |x: &str| is_anno_global(x) || is_anno_var(x);
    let is_intrinsic = |x: &str| x.contains("llvm.");

    let intra_non_anno = intra_uses
        .iter()
        .filter(|(v1, v2)| {
            !(v1.item.call_name_predicate(is_annotation)
                || v2.item.call_name_predicate(is_annotation))
        })
        .collect::<HashSet<_>>();
    let intra_intrinsic = intra_non_anno
        .iter()
        .filter(|(v1, v2)| {
            v1.item.call_name_predicate(is_intrinsic) || v2.item.call_name_predicate(is_intrinsic)
        })
        .map(|(v1, v2)| ((*v1).to_owned(), v2.to_owned()))
        .collect::<HashSet<_>>();
    let intra_nonintrinsic = intra_non_anno
        .iter()
        .filter(|(v1, v2)| {
            !(v1.item.call_name_predicate(is_intrinsic)
                || v2.item.call_name_predicate(is_intrinsic))
        })
        .map(|(v1, v2)| ((*v1).to_owned(), v2.to_owned()))
        .collect::<HashSet<_>>();

    let globals = ir_iset.get(&id!(IRGlobal.Internal));

    let globals = globals
        .iter()
        .map(|v| (v.clone(), v.item.global().unwrap()));

    let global_uses = globals
        .flat_map(|(gv, g)| {
            functions
                .iter()
                .flat_map(|(_, f)| {
                    g.name
                        .users(f, true)
                        .iter()
                        .map(|f| (gv.clone(), f.clone()))
                        .collect::<Vec<_>>()
                })
                .collect::<Vec<_>>()
        })
        .collect::<HashSet<_>>();
    let all_uses = union!(intra_uses, global_uses.iter().map(|(x, y)| (x, y.clone())));

    let anno_var = all_uses
        .iter()
        .filter(|(v1, v2)| {
            v1.item.call_name_predicate(is_anno_var) || v2.item.call_name_predicate(is_anno_var)
        })
        .map(|(v1, v2)| ((*v1).to_owned(), v2.to_owned()))
        .collect::<HashSet<_>>();
    let anno_global = all_uses
        .iter()
        .filter(|(v1, v2)| {
            v1.item.call_name_predicate(is_anno_global)
                || v2.item.call_name_predicate(is_anno_global)
        })
        .map(|(v1, v2)| ((*v1).to_owned(), v2.to_owned()))
        .collect::<HashSet<_>>();

    let inter_intrinsic = global_uses
        .iter()
        .filter(|(v1, v2)| {
            v1.item.call_name_predicate(is_intrinsic) || v2.item.call_name_predicate(is_intrinsic)
        })
        .map(|x| x.to_owned())
        .collect::<HashSet<_>>();
    let inter_nonintrinsic = global_uses
        .iter()
        .filter(|(v1, v2)| {
            !(v1.item.call_name_predicate(is_intrinsic)
                || v2.item.call_name_predicate(is_intrinsic))
        })
        .map(|x| x.to_owned())
        .collect::<HashSet<_>>();

    DefUseEdges {
        anno_var,
        anno_global,
        intra_intrinsic,
        intra_nonintrinsic,
        inter_intrinsic,
        inter_nonintrinsic,
    }
}

fn account_anno_var(
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    def_use_edges: &DefUseEdges,
) -> Account<LLEdge> {
    let anno_var_def_use = def_use_edges
        .anno_var
        .iter()
        .map(|(x, y)| LLEdge(x.id.clone(), y.id.clone()));
    let anno_var_fn = def_use_edges.anno_var.iter().map(|(_, dst)| {
        LLEdge(
            LLID::GlobalName {
                global_name: dst.id.global_name().to_owned(),
            },
            dst.id.clone(),
        )
    });
    let all_anno_var = union!(anno_var_def_use.collect::<HashSet<_>>(), anno_var_fn);
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
        .next()
        .unwrap();
    let init = global_annotation
        .item
        .global()
        .unwrap()
        .initializer
        .unwrap();
    let srcs = if let Constant::Array {
        element_type: _,
        elements,
    } = init.as_ref()
    {
        elements.iter().flat_map(|c| {
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
        })
    } else {
        unreachable!()
    };
    let ir_derived_edges = srcs.map(|name| {
        LLEdge(
            LLID::global_name_from_name(&name),
            global_annotation.id.clone(),
        )
    });
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
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLEdge> {
    let ir_rets = ir_iset
        .get(&id!(IRInstruction.Ret))
        .iter()
        .map(|x| {
            (
                LLID::global_name_from_string(x.id.global_name().to_string()),
                x.id.clone(),
            )
        })
        .collect::<ISet<LLID, LLID>>();
    let ir_calls = ir_iset.get(&id!(IRInstruction.Call.Internal));
    let ir_ret_edges = ir_calls
        .iter()
        .filter_map(|v| v.item.constant_call_name().map(|x| (v, x)))
        .flat_map(|(call, name)| 
            ir_rets.get(&LLID::global_name_from_name(&name)).iter().map(move |x| LLEdge(x.clone(), call.id.clone())))
        .collect::<HashSet<_>>();
    let pdg_ret_edges = edge_iset.get(&id!(PDGEdge.ControlDep.CallRet));
    Account {
        a: edge_ids(pdg_ret_edges, pdg),
        b: ir_ret_edges,
    }
}

fn account_for_raw(
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    ir_iset: &ISet<ID, LLValue>,
) -> Account<LLEdge> {
    let pdg_raw = edge_ids(edge_iset.get(&id!(PDGEdge.DataDepEdge.RAW)), pdg);
    enum Either<A, B> {
        Left(A),
        Right(B),
    }
    let ir_raw = ir_iset
        .get(&id!(IRFunction))
        .iter()
        .flat_map(|v| {
            let f = v.item.function().unwrap();
            let instrs = f
                .basic_blocks
                .iter()
                .flat_map(|b| {
                    b.instrs
                        .iter()
                        .map(|x| Either::Left(x))
                        .chain([Either::Right(&b.term)])
                })
                .enumerate()
                .collect::<Vec<_>>();
            let stores = instrs
                .iter()
                .filter_map(|(idx, inst)| match inst {
                    Either::Left(Instruction::Store(s)) => {
                        let addr = s.address.names().into_iter().next().unwrap();
                        Some((
                            addr,
                            LLID::InstructionID {
                                global_name: v.id.global_name().to_string(),
                                index: *idx,
                            },
                        ))
                    }
                    _ => None,
                })
                .collect::<ISet<_, _>>();
            instrs
                .iter()
                .flat_map(|(idx, inst)| match inst {
                    Either::Left(Instruction::Load(l)) => {
                        let addr = l.address.names().into_iter().next().unwrap();
                        stores
                            .get(&addr)
                            .iter()
                            .filter_map(|store| {
                                if store.index().unwrap() < *idx {
                                    Some(LLEdge(
                                        store.clone(),
                                        LLID::InstructionID {
                                            global_name: v.id.global_name().to_string(),
                                            index: *idx,
                                        },
                                    ))
                                } else {
                                    None
                                }
                            })
                            .collect::<Vec<_>>()
                    }
                    _ => Vec::new(),
                })
                .collect::<Vec<_>>()
        })
        .collect::<HashSet<_>>();
    Account {
        a: pdg_raw,
        b: ir_raw,
    }
}

pub fn account_for_alias(pdg: &Pdg, edge_iset: &ISet<ID, Edge>, alias_edges: HashSet<(LLValue, LLValue)>) -> Account<LLEdge> {
    let ir_alias_edges = alias_edges
        .into_iter()
        .map(|(v1, v2)| LLEdge(v1.id, v2.id));
    let pdg_alias_edges = edge_iset.get(&id!(PDGEdge.DataDepEdge.Alias));
    Account {
        a: edge_ids(pdg_alias_edges, pdg),
        b: ir_alias_edges.collect()
    }
}

pub fn report_all_accounts(
    report: &mut Report,
    ir_report: &mut Report,
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    ir_iset: &ISet<ID, LLValue>,
    alias_sets: Vec<HashSet<Binder>>
) {
    let def_use_edges = ir_defuse_edges(ir_iset);
    let acct_anno_var = account_anno_var(pdg, &edge_iset, &def_use_edges);
    ir_report.report_count("IRAnnoVar", acct_anno_var.b.len());
    report.report_account("PDGEdge.Anno.Var", "IRAnnoVar", acct_anno_var);
    let acct_anno_global = account_anno_global(pdg, &edge_iset, &ir_iset);
    ir_report.report_count("IRAnnoGlobal", acct_anno_global.b.len());
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
    let acct_ret = account_for_ret_edges(pdg, &edge_iset, &ir_iset);
    ir_report.report_count("IRRets", acct_ret.b.len());
    report.report_account(
        "PDGEdge.ControlDep.CallRet",
        "IRRets",
        acct_ret
    );
    let alias_edges = alias_edges(&ir_iset, &alias_sets);
    let alias_account = account_for_alias(pdg, edge_iset, alias_edges);
    ir_report.report_count("IRAlias", alias_account.b.len());
    report.report_account("PDGEdge.DataDepEdge.Alias", "IRAlias", alias_account);

    // let acct = account_for_raw(pdg, edge_iset, ir_iset);
    // // for (src, dst) in acct.b_minus_a().take(10) {
    // //     println!("{} --> {}", src, dst);
    // // }
    // ir_report.report_count("IRRAW", acct.b.len());
    // report.report_account("PDGEdge.DataDepEdge.RAW", "IRRAW", acct);
}
