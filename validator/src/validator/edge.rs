use std::collections::{HashMap, HashSet};

use llvm_ir::{constant::{BitCast, GetElementPtr}, Constant};
use serde::de::value::CowStrDeserializer;

use crate::{
    accounting::Account,
    id,
    id::ID,
    indexed_set::ISet,
    llvm::{LLValue, Names, Users, LLID},
    pdg::{Edge, Pdg},
    report::Report,
    union,
};

fn edge_ids<'a>(iter: impl IntoIterator<Item = &'a Edge>, pdg: &'a Pdg) -> HashSet<(LLID, LLID)> {
    iter.into_iter()
        .map(|e| pdg.from_edge(&e))
        .map(|(src, dst)| (pdg.llid(src).unwrap(), pdg.llid(dst).unwrap()))
        .collect()
}

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
) -> Account<(LLID, LLID)> {
    let anno_var_def_use = def_use_edges
        .anno_var
        .iter()
        .map(|(x, y)| (x.id.clone(), y.id.clone()));
    let anno_var_fn = def_use_edges.anno_var.iter().map(|(_, dst)| {
        (
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
) -> Account<(LLID, LLID)> {
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
        (
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

pub fn report_all_accounts(
    report: &mut Report,
    pdg: &Pdg,
    edge_iset: &ISet<ID, Edge>,
    ir_iset: &ISet<ID, LLValue>,
) {
    let def_use_edges = ir_defuse_edges(ir_iset);
    report.report_account(
        "PDGEdge.Anno.Var",
        "IRAnnoVar",
        account_anno_var(pdg, &edge_iset, &def_use_edges),
    );
    report.report_account(
        "PDGEdge.Anno.Global",
        "IRAnnoGlobal",
        account_anno_global(pdg, &edge_iset, &ir_iset),
    );
}
