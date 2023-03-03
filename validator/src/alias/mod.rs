use crate::llvm::LLID;
use llvm_ir::Type;
use nom::{
    branch::alt,
    bytes::{
        complete::take_until,
        streaming::{is_not, tag, take_till},
    },
    character::{
        complete::{digit1, space0, space1},
        streaming::{alphanumeric1, anychar, newline, not_line_ending},
    },
    combinator::{not, opt, map},
    multi::{many_till, separated_list1, many0},
    sequence::{delimited, pair, preceded, terminated, tuple},
    IResult,
};
use std::{
    collections::{HashMap, HashSet},
    fs,
    iter::FromIterator, fmt::Display,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum AliasStatus {
    NoAlias,
    MayAlias,
    MustAlias,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum ModRefStatus {
    Mod,
    Ref,
    ModRef,
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct SetInfo {
    id: String,
    alias_status: AliasStatus,
    modref_status: ModRefStatus,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Binder {
    Global(String),
    Local(String),
}

impl Display for Binder {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Binder::Global(s) => f.write_fmt(format_args!("@{}", s)),
            Binder::Local(s) => f.write_fmt(format_args!("%{}", s))
        }
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Default)]
pub struct AliasSets(pub HashMap<(String, Binder), SetInfo>);
impl AliasSets {
    pub fn new() -> Self {
        AliasSets(Default::default())
    }
}
impl FromIterator<AliasSets> for AliasSets {
    fn from_iter<T: IntoIterator<Item = Self>>(iter: T) -> Self {
        let mut s = AliasSets::new();
        for x in iter {
            s.0.extend(x.0);
        }
        s
    }
}

pub fn alias_sets(file: &str) -> AliasSets {
    let contents = fs::read_to_string(file).unwrap();
    let (_, alias_sets) = parse_alias_sets(&contents).unwrap_or(("", AliasSets::new()));
    alias_sets
}

pub fn parse_alias_sets(input: &str) -> IResult<&str, AliasSets> {
    let (input, alias_sets) =
        many0(parse_alias_set_for_function)(input)?;
    let alias_sets = AliasSets::from_iter(alias_sets);
    Ok((input, alias_sets))
}

fn parse_alias_set_for_function(input: &str) -> IResult<&str, AliasSets> {
    let (input, fn_name) = parse_function_line(input)?;
    let (input, _) = parse_alias_sets_header(input)?;
    // let double_newline = pair(newline, newline);
    let (input, (sets, _)) = many_till(parse_alias_set, newline)(input)?;

    let map = sets
        .into_iter()
        .flat_map(|(binders, info)| {
            let fn_name = &fn_name;
            binders
                .into_iter()
                .map(move |b| ((fn_name.to_owned(), b), info.clone()))
        })
        .collect();
    Ok((input, AliasSets(map)))
}

fn parse_alias_sets_header(input: &str) -> IResult<&str, ()> {
    let (input, _) = take_until("\n")(input)?;
    let (input, _) = newline(input)?;
    Ok((input, ()))
}

fn parse_alias_set(input: &str) -> IResult<&str, (HashSet<Binder>, SetInfo)> {
    let (input, id) = delimited(
        tag("  AliasSet["),
        parse_hex,
        tuple((tag(", "), digit1, tag("] "))),
    )(input)?;
    let (input, alias_status) = terminated(parse_alias_status, tuple((tag(","), space0)))(input)?;
    let (input, modref_status) = terminated(parse_modref_status, space0)(input)?;
    let parse_pair = delimited(tag("("), parse_alias_set_element, tag(")"));
    let parse_pointers = delimited(
        tuple((tag("Pointers:"), space0)),
        separated_list1(tuple((tag(","), space0)), parse_pair),
        newline,
    );
    let parse_forwarding = tuple((
        tag("forwarding to "),
        parse_hex,
        newline
    ));
    let (input, names) = alt((parse_pointers, map(parse_forwarding, |_| Vec::new())))(input)?; 
    // let (input, names) = delimited(
    //     tuple((tag("Pointers:"), space0)),
    //     separated_list1(tuple((tag(","), space0)), parse_pair),
    //     newline,
    // )(input)?;
    let (input, _) = opt(tuple((tag("    "), not_line_ending, newline)))(input)?;

    Ok((
        input,
        (
            HashSet::from_iter(names),
            SetInfo {
                id: id.to_string(),
                alias_status,
                modref_status,
            },
        ),
    ))
}

fn parse_alias_set_element(input: &str) -> IResult<&str, Binder> {
    let (input, _) = many_till(anychar, space1)(input)?;
    let binder_prefix = alt((tag("%"), tag("@")));
    let (input, (binder_type, binder_name)) =
        tuple((binder_prefix, many_till(anychar, tuple((tag(","), space0)))))(input)?;
    let binder_name = binder_name.0.iter().collect();
    let binder = match binder_type {
        "@" => Binder::Global(binder_name),
        _ => Binder::Local(binder_name.to_string()),
    };
    let (input, _) = tuple((
        tag("LocationSize::"),
        alphanumeric1,
        tag("("),
        digit1,
        tag(")"),
    ))(input)?;
    Ok((input, binder))
}

fn parse_alias_status(input: &str) -> IResult<&str, AliasStatus> {
    let (input, status_str) = take_until(",")(input)?;
    let status = match status_str {
        "must alias" => AliasStatus::MustAlias,
        "no alias" => AliasStatus::NoAlias,
        _ => AliasStatus::MayAlias,
    };
    Ok((input, status))
}
fn parse_modref_status(input: &str) -> IResult<&str, ModRefStatus> {
    let (input, status_str) = take_till(|c| c == ' ' || c == '\t')(input)?;
    let status = match status_str {
        "Mod" => ModRefStatus::Mod,
        "Ref" => ModRefStatus::Ref,
        _ => ModRefStatus::ModRef,
    };
    Ok((input, status))
}

fn parse_hex(input: &str) -> IResult<&str, &str> {
    preceded(tag("0x"), alphanumeric1)(input)
}

fn parse_function_line(input: &str) -> IResult<&str, String> {
    let (input, _) = tag("Alias sets for function ")(input)?;
    let (input, fn_name) =
        delimited(tag("'"), take_until("'"), tuple((tag("':"), newline)))(input)?;
    Ok((input, fn_name.to_string()))
}
