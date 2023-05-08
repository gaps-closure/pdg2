use std::{collections::HashSet, iter::FromIterator};


use nom::{IResult, bytes::complete::tag, multi::{separated_list0, fold_many0, separated_list1}, sequence::tuple, character::{streaming::{alphanumeric1, space0, newline}}, branch::alt};

use crate::{indexed_set::ISet, id::ID, llvm::{LLValue, LLID, LLEdge}};

use super::util::{Binder, get_all_values, binder_to_llid_map, AliasSubType};

type _AliasSet = HashSet<Binder>;

pub fn parse_svf_sets(input: &str) -> IResult<&str, Vec<HashSet<Binder>>> {
    separated_list1(newline, parse_svf_set)(input)
}

fn parse_svf_set(input: &str) -> IResult<&str, HashSet<Binder>> {
    let (input, _) = tag("{")(input)?;
    let (input, binders) = separated_list0(tuple((tag(","), space0)), parse_binder)(input)?;
    let (input, _) = tag("}")(input)?;
    Ok((input, HashSet::from_iter(binders)))
}

fn parse_binder(input: &str) -> IResult<&str, Binder> {
    let (input, _) = tag("'")(input)?;
    let (input, binder) = alt((parse_global_binder, parse_local_binder))(input)?;
    let (input, _) = tag("'")(input)?;
    Ok((input, binder))
}

fn parse_local_binder(input: &str) -> IResult<&str, Binder> {
    let (input, fn_name) = llvm_name(input)?;
    let (input, _) = tag("::%")(input)?;
    let (input, local_name) = llvm_name(input)?;
    Ok((input, Binder::Local(fn_name, local_name)))
}
fn parse_global_binder(input: &str) -> IResult<&str, Binder> {
    let (input, _) = tag("@")(input)?;
    let (input, name) = llvm_name(input)?;
    Ok((input, Binder::Global(name)))
}


fn llvm_name(input: &str) -> IResult<&str, String> {
    fold_many0(alt((alphanumeric1, tag("."), tag("_"))), String::new, 
        |mut acc, next| {
            acc += next;
            acc
        })(input)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Aliaser {
    Function,
    Parameter
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Aliasee {
    Function,
    Global,
    Instruction
}

pub type AliasEdges = ISet<(Aliaser, Aliasee), LLEdge>;

pub fn alias_edges(ir_iset: &ISet<ID, LLValue>, alias_sets: &Vec<HashSet<Binder>>) -> AliasEdges {
    let ir_value_set = get_all_values(ir_iset);
    let binder_to_llid = binder_to_llid_map(ir_value_set.iter());
    let llid_alias_sets = 
        alias_sets
            .iter()
            .map(|set| 
                set.iter()
                    .filter_map(|x| binder_to_llid.get(x))
                    .map(|x| x.to_owned())
                    .collect::<HashSet<_>>()
            );
    let mut edges = ISet::new();
    for set in llid_alias_sets {
        for (x, x_type) in &set {
            let aliaser = match x_type {
                AliasSubType::Function => Aliaser::Function,
                AliasSubType::Global => continue, 
                AliasSubType::Instruction => Aliaser::Function,
                AliasSubType::Parameter => continue, 
            };
            let from = LLID::global_name_from_string(x.global_name().to_string());
            for (y, y_type) in &set {
                let aliasee = match y_type {
                    AliasSubType::Function => Aliasee::Function,
                    AliasSubType::Global => Aliasee::Global,
                    AliasSubType::Instruction => Aliasee::Instruction,
                    AliasSubType::Parameter => continue,
                };
                let to = y; 
                if &from != to {
                    edges.insert((aliaser, aliasee), LLEdge::from((from.clone(), to.clone())));
                }
            }
        }
    }
    edges
}