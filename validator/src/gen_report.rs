use crate::{
    bag::Bag,
    counts::{IndexedSets, EDGE_IDS, IR_IDS, NODE_IDS, ir_edge_iset, IR_EDGE_IDS},
    id::ID,
    indexed_set::ISet,
    llvm::{instr_name, term_name, FunctionUsedAsPointer, LLItem, LLValue, Users, LLID},
    pdg::{Edge, Node, Pdg},
    report::Report,
    union, validator::{self, util::RECONCILIATIONS}, alias::{svf::parse_svf_sets},
};
use csv::Writer;
use llvm_ir::{
    function::Parameter,
    module::{GlobalVariable, Linkage},
    types::Typed,
    Function, Instruction, Module, Operand, Type,
};
use petgraph::{algo::floyd_warshall, graph::NodeIndex, Graph};
use std::hash::Hash;
use std::{
    collections::{HashMap, HashSet},
    fs::File,
};

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

fn has_prefix<'a, V>(
    map: &'a Bag<SetID, V>,
    pref: &'a SetID,
) -> impl Iterator<Item = (&'a SetID, &'a Vec<V>)> {
    map.hashmap.iter().filter_map(move |(vec, c)| {
        if vec.len() != pref.len() + 1 {
            return None;
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
        let prefixes = bag
            .hashmap
            .iter()
            .filter_map(|(v, n)| {
                if v.len() != k {
                    None
                } else {
                    Some((v[..k - 1].to_owned(), n.to_owned()))
                }
            })
            .collect::<Vec<_>>();
        for (k, v) in prefixes {
            bag.insert_all(k, v);
        }
        k = k - 1;
    }
}

fn ir_iter_to_map<'a, I: IntoIterator<Item = &'a LLValue>>(iter: I) -> HashMap<LLID, &'a LLValue> {
    iter.into_iter().map(|v| (v.id.clone(), v)).collect()
}
fn node_iter_to_map<'a, I: IntoIterator<Item = &'a Node>>(
    iter: I,
    pdg: &Pdg,
) -> HashMap<LLID, &'a Node> {
    iter.into_iter()
        .map(|n| (pdg.llid(n).unwrap(), n))
        .collect()
}

fn map_difference<'a, K: Hash + Eq, A, B>(
    map1: &'a HashMap<K, A>,
    map2: &'a HashMap<K, B>,
) -> HashMap<&'a K, &'a A> {
    map1.iter()
        .filter(|(k, _)| !map2.contains_key(&k))
        .collect()
}
fn map_difference_owned<'a, K: Hash + Eq + ToOwned, A, B>(
    map1: &'a HashMap<K, A>,
    map2: &'a HashMap<K, B>,
) -> HashMap<<K as ToOwned>::Owned, &'a A>
where
    <K as ToOwned>::Owned: Hash + Eq,
{
    map1.iter()
        .filter(|(k, _)| !map2.contains_key(&k))
        .map(|(k, v)| (k.to_owned(), v))
        .collect()
}

fn compare_a_b<'a, K: Hash + Eq + ToOwned, A: ToOwned, B: ToOwned>(
    a_name: &str,
    b_name: &str,
    a: &'a HashMap<K, A>,
    b: &'a HashMap<K, B>,
    writer: &mut Writer<File>,
) {
    let a_minus_b = map_difference(a, b);
    let b_minus_a = map_difference(b, a);
    writer
        .write_record(&id![
            a_name,
            b_name,
            a.len(),
            b.len(),
            a_minus_b.len(),
            b_minus_a.len()
        ])
        .unwrap();
}

fn compare_node_ir<'a, I: IntoIterator<Item = &'a Node>, J: IntoIterator<Item = &'a LLValue>>(
    node_name: &str,
    ir_name: &str,
    node_iter: I,
    ir_iter: J,
    pdg: &Pdg,
    writer: &mut Writer<File>,
) {
    compare_a_b(
        node_name,
        ir_name,
        &node_iter_to_map(node_iter, pdg),
        &ir_iter_to_map(ir_iter),
        writer,
    );
}


fn proper_param_edges<'a>(pdg: &Pdg, edge_bag: &Bag<SetID, &'a Edge>) -> Vec<&'a Edge> {
    edge_bag
        .get(&id!(PDGEdge.Parameter.In))
        .iter()
        .filter(|e| {
            let (src, dst) = pdg.from_edge(e);
            src.r#type == "Param_ActualIn"
                && dst.r#type == "Param_FormalIn"
                && dst.param_idx.is_some()
        })
        .map(|e| (*e))
        .collect()
}

fn write_distinct_fn_sigs(module: &Module, writer: &mut Writer<File>) {
    let distinct_fns = module
        .functions
        .iter()
        .map(|f| (f.get_type(&module.types), f))
        .collect::<Bag<_, _>>();
    writer
        .write_record(id!(
            "IRDistinctFunctionSignatures",
            distinct_fns.hashmap.len()
        ))
        .unwrap();
}

fn write_funcs_used_as_ptr(module: &Module, writer: &mut Writer<File>) {
    let fns = module.funcs_used_as_pointer();
    writer
        .write_record(id! {"IRDistinctFunctionsUsedAsPointer", fns.len()})
        .unwrap();
}

fn predicate_cost<'a>(
    costs: &'a HashMap<(NodeIndex, NodeIndex), u32>,
    index: NodeIndex,
    pred: impl Fn(u32) -> bool + 'a,
) -> impl Iterator<Item = ((NodeIndex, NodeIndex), u32)> + 'a {
    costs
        .iter()
        .filter(move |((src, _), cost)| *src == index && pred(**cost))
        .map(|(pair, cost)| (*pair, *cost))
}

fn reachable_from(
    costs: &HashMap<(NodeIndex, NodeIndex), u32>,
    index: NodeIndex,
) -> impl Iterator<Item = ((NodeIndex, NodeIndex), u32)> + '_ {
    predicate_cost(costs, index, |cost| cost != u32::MAX)
}
fn not_reachable_from(
    costs: &HashMap<(NodeIndex, NodeIndex), u32>,
    index: NodeIndex,
) -> impl Iterator<Item = ((NodeIndex, NodeIndex), u32)> + '_ {
    predicate_cost(costs, index, |cost| cost == u32::MAX)
}

fn direct_callgraph<'a>(pdg: &'a Pdg) -> (HashMap<u64, NodeIndex>, Graph<&'a Node, ()>) {
    let edges = pdg
        .edges_of_type("ControlDep_CallInv")
        .map(|e| pdg.from_edge(e))
        .map(|(src, dest)| (pdg.has_function(src), dest));

    let mut node_index_map = HashMap::<u64, NodeIndex>::new();
    let mut graph = Graph::new();

    for node in pdg.function_entries() {
        let idx = graph.add_node(node);
        node_index_map.insert(node.id, idx);
    }

    for (src, dest) in edges {
        let idx_src = node_index_map.get(&src.id).map(|i| i.to_owned()).unwrap();
        let idx_dest = node_index_map.get(&dest.id).map(|i| i.to_owned()).unwrap();
        graph.update_edge(idx_src, idx_dest, ());
    }
    (node_index_map, graph)
}

fn full_callgraph<'a>(
    node_index_map: &HashMap<u64, NodeIndex>,
    mut direct_graph: Graph<&'a Node, ()>,
    pdg: &'a Pdg,
    module: &Module,
    ir_bag: &Bag<SetID, LLValue>,
    node_bag: &'a Bag<SetID, &Node>,
) -> (Graph<&'a Node, ()>, HashMap<LLID, Function>) {
    let fn_map = ir_bag
        .get(&id!(IRFunction))
        .iter()
        .map(|llval| (llval.id.clone(), llval.item.function().unwrap()))
        .collect::<HashMap<_, _>>();

    let type_bag = module
        .funcs_used_as_pointer()
        .iter()
        .map(|name| LLID::GlobalName {
            global_name: name.to_string()[1..].to_string(),
        })
        .filter_map(|name| {
            fn_map.get(&name).map(|f| {
                (
                    Type::PointerType {
                        pointee_type: f.get_type(&module.types),
                        addr_space: 0,
                    },
                    name,
                )
            })
        })
        .collect::<Bag<_, _>>();

    let ir_pointer_calls = ir_iter_to_map(ir_bag.get(&id!(IRInstruction.Call.Pointer)));

    let inst_fun_calls = node_bag.get(&id!(PDGNode.Inst.FunCall));
    let function_entry_map = node_iter_to_map(
        node_bag.get(&id!(PDGNode.FunctionEntry)).iter().map(|n| *n),
        pdg,
    );

    for node in inst_fun_calls {
        let id = pdg.llid(node).unwrap();
        let ir_call = match ir_pointer_calls.get(&id) {
            Some(v) => v,
            None => continue,
        };
        let call = ir_call.item.call().unwrap();
        let callee_type = call
            .function
            .clone()
            .right()
            .map(|o| match &o {
                Operand::LocalOperand { name: _, ty } => ty.clone(),
                _ => unreachable!(),
            })
            .unwrap();
        let candidates = type_bag.get(&callee_type);
        let idx_src = node_index_map
            .get(&node.has_fn)
            .map(|i| i.to_owned())
            .unwrap();
        for candidate in candidates {
            let dest = function_entry_map.get(candidate).unwrap();
            let idx_dest = node_index_map.get(&dest.id).map(|i| i.to_owned()).unwrap();
            // println!("{} -> {}", pdg.llid(pdg.has_function(node)).unwrap(), candidate);
            direct_graph.update_edge(idx_src, idx_dest, ());
        }
    }

    (direct_graph, fn_map)
}

fn callgraph_reachability(
    ir_bag: &Bag<SetID, LLValue>,
    node_bag: &Bag<SetID, &Node>,
    module: &Module,
    pdg: &Pdg,
    writer: &mut Writer<File>,
) {
    let (node_index_map, direct_graph) = direct_callgraph(pdg);

    let costs = floyd_warshall(&direct_graph, |_| 0 as u32).unwrap();

    let main = pdg
        .function_entries()
        .find(|n| n.ir_name() == Some("main".to_string()))
        .unwrap();

    let main_idx = *node_index_map.get(&main.id).unwrap();

    let get_dest_index = |((_, idx), _)| idx;
    let connected_to_main = reachable_from(&costs, main_idx).map(get_dest_index);
    let not_connected_to_main = not_reachable_from(&costs, main_idx).map(get_dest_index);

    writer
        .write_record(id! {"PDGDirectCallGraphEdges", direct_graph.edge_count()})
        .unwrap();
    writer
        .write_record(id! {"PDGMainComponentDirect", connected_to_main.count()})
        .unwrap();
    writer
        .write_record(id! {"PDGNonMainComponentsDirect", not_connected_to_main.count()})
        .unwrap();

    let (full_graph, fn_map) =
        full_callgraph(&node_index_map, direct_graph, pdg, module, ir_bag, node_bag);

    let costs = floyd_warshall(&full_graph, |_| 0 as u32).unwrap();
    let connected_to_main = reachable_from(&costs, main_idx)
        .map(get_dest_index)
        .collect::<Vec<_>>();

    let not_connected_to_main = not_reachable_from(&costs, main_idx).map(get_dest_index);

    writer
        .write_record(id! {"EPDGDirectIndirectCallGraphEdges", full_graph.edge_count()})
        .unwrap();
    writer
        .write_record(id! {"EPDGMainComponentDirectIndirect", connected_to_main.len()})
        .unwrap();
    writer
        .write_record(id! {"EPDGNonMainComponentsDirectIndirect", not_connected_to_main.count()})
        .unwrap();

    let fn_entry_map = node_bag
        .get(&id!(PDGNode.FunctionEntry))
        .iter()
        .map(|n| (pdg.llid(n).unwrap(), *n))
        .collect::<HashMap<_, _>>();

    // let dot_graph = full_graph.map(|_, x| pdg.llid(x).unwrap(), |_, _| "");
    // let dot = Dot::with_config(&dot_graph, &[Config::EdgeNoLabel]);
    // println!("{}", dot);

    let mut full_graph = full_graph.map(|_, x| Some(*x), |_, _| ());

    let fakeroot_idx = full_graph.add_node(None);
    let _fakeroot_edge_idx = full_graph.add_edge(fakeroot_idx, main_idx, ());

    let costs = floyd_warshall(&full_graph, |_| 0 as u32).unwrap();

    let connected_to_fakeroot = reachable_from(&costs, fakeroot_idx)
        .map(get_dest_index)
        .collect::<Vec<_>>();

    let function_names_in_main_component = connected_to_fakeroot
        .iter()
        .filter_map(|idx| full_graph[*idx])
        .map(|n| pdg.llid(n).unwrap())
        .map(|id| fn_map.get(&id).unwrap())
        .map(|f| LLID::global_name_from_string(f.name.clone()))
        .collect::<HashSet<_>>();

    let mut xc = function_names_in_main_component.clone();

    let referenced_in_globals = module
        .global_vars
        .iter()
        .filter(|g| &g.name.to_string()[1..] != "llvm.global.annotations")
        .flat_map(|g| g.funcs_used_as_pointer())
        .map(|name| LLID::global_name_from_name(&name))
        .collect::<HashSet<_>>();

    let referenced_in = |xc: &HashSet<LLID>| {
        xc.iter()
            .map(|n| fn_map.get(n).unwrap())
            .flat_map(|f| f.funcs_used_as_pointer())
            .map(|n| LLID::global_name_from_name(&n))
            .filter(|id| fn_entry_map.contains_key(id))
            .collect::<HashSet<LLID>>()
    };

    let mut difference = union!(referenced_in(&xc), referenced_in_globals)
        .difference(&xc)
        .map(|l| l.clone())
        .collect::<Vec<_>>();

    let mut xc_edges = Vec::new();

    let mut k = 0;
    while difference.len() > 0 {
        for id in difference {
            let node = *fn_entry_map.get(&id).unwrap();
            let src_idx = node_index_map.get(&node.id).unwrap();
            xc_edges.push(pdg.llid(node).unwrap());
            xc.extend(
                reachable_from(&costs, *src_idx)
                    .map(get_dest_index)
                    .map(|idx| full_graph[idx].unwrap())
                    .map(|n| pdg.llid(n).unwrap()),
            )
        }
        difference = referenced_in(&xc)
            .difference(&xc)
            .map(|l| l.clone())
            .collect::<Vec<_>>();
        k = k + 1;
    }
    // for id in &xc_edges {
    //     println!("{}", id);
    // }

    let fn_ids = fn_map.keys().map(|l| l.clone()).collect::<HashSet<LLID>>();
    let not_xc = fn_ids.difference(&xc);
    // let extension = xc.difference(&function_names_in_main_component);
    writer
        .write_record(id! {"EPDGExtendedComponent", xc.len()})
        .unwrap();
    writer
        .write_record(id! {"EPDGNotExtendedComponents", not_xc.count()})
        .unwrap();
    writer
        .write_record(id! {"EPDGExternalCallInvEdges", xc_edges.len()})
        .unwrap();
}

pub fn report2(
    bc_file: &str,
    pdg_data_file: &str,
    pdg_counts_csv: &str,
    pdg_rollups_csv: &str,
    pdg_differences_csv: &str,
    ir_counts_csv: &str,
    ir_rollups_csv: &str,
    ir_differences_csv: &str,
    validation_csv: &str,
    validation_differences_csv: &str,
    alias_sets: &str
) {
    let module = Module::from_bc_path(bc_file).unwrap();
    let pdg = {
        let file = File::open(pdg_data_file).unwrap();
        let rdr = csv::ReaderBuilder::new()
            .has_headers(false)
            .quote(b'\'')
            .trim(csv::Trim::All)
            .from_reader(std::io::BufReader::new(file));

        Pdg::from_csv_reader(rdr).unwrap()
    };
    let mut pdg_report = Report::new(Some(pdg_counts_csv), pdg_rollups_csv, pdg_differences_csv).unwrap();
    let mut ir_report = Report::new(Some(ir_counts_csv), ir_rollups_csv, ir_differences_csv).unwrap();
    let mut reconciliations_report = Report::new(None, validation_csv, validation_differences_csv).unwrap();
    let node_iset: ISet<ID, Node> = pdg.indexed_sets();
    let edge_iset: ISet<ID, Edge> = pdg.indexed_sets();
    let ir_iset = module.indexed_sets();

    let contents = std::fs::read_to_string(&alias_sets).unwrap();
    let (_, alias_sets) = parse_svf_sets(&contents).unwrap();

    let ir_edge_iset = ir_edge_iset(&ir_iset, &alias_sets);

    node_iset.report_counts(&mut pdg_report);
    edge_iset.report_counts(&mut pdg_report);
    pdg_report.set_counts_ordering(
        [NODE_IDS.clone(), EDGE_IDS.clone()]
            .concat()
            .into_iter()
            .enumerate()
            .map(|(i, x)| (x.to_string(), i))
            .collect(),
    );
    ir_iset.report_counts(&mut ir_report);
    ir_edge_iset.report_counts(&mut ir_report);

    ir_report.set_counts_ordering(
        [IR_IDS.clone(), IR_EDGE_IDS.clone()]
            .concat()
            .into_iter()
            .enumerate()
            .map(|(i, x)| (x.to_string(), i))
            .collect(),
    );

    node_iset.report_rollups(&mut pdg_report);
    edge_iset.report_rollups(&mut pdg_report);
    ir_iset.report_rollups(&mut ir_report);
    ir_edge_iset.report_counts(&mut ir_report);

    pdg_report.write().unwrap();

    reconciliations_report.set_validations_ordering(
        RECONCILIATIONS
            .clone()
            .into_iter()
            .enumerate()
            .map(|(i, (a, b))| ((a.to_string(), b.to_string()), i))
            .collect()
    );
    validator::edge::report_all_accounts(&mut reconciliations_report, &pdg, &edge_iset, &ir_iset, &ir_edge_iset);
    validator::node::report_all_accounts(&mut reconciliations_report, &pdg, &node_iset, &ir_iset);

    ir_report.write().unwrap();

    reconciliations_report.write().unwrap();

}