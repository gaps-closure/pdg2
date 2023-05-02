use std::{collections::HashSet, iter::FromIterator};


use nom::{IResult, bytes::complete::tag, multi::{separated_list0, fold_many0, separated_list1}, sequence::tuple, character::{streaming::{alphanumeric1, space0, newline}}, branch::alt};

use super::util::Binder;

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
