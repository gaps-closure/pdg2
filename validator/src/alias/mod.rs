use nom::{
    branch::alt,
    bytes::{
        complete::take_until,
        complete::{tag, take_till},
    },
    character::{
        complete::{alphanumeric1, anychar, newline, not_line_ending},
        complete::{digit1, space0, space1},
    },
    combinator::{map, opt},
    multi::{many1, many_till, separated_list1},
    sequence::{delimited, preceded, terminated, tuple},
    IResult,
};
use std::{
    cmp::{max},
    collections::{HashMap, HashSet},
    fmt::Display,
    fs,
    iter::FromIterator,
};

pub type SetID = String;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum AliasStatus {
    NoAlias,
    MustAlias,
    MayAlias,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum ModRefStatus {
    Mod,
    Ref,
    ModRef,
}

#[derive(Debug, Clone)]
pub struct AliasSet {
    pub id: SetID,
    pub set: HashSet<Binder>,
    pub alias_status: AliasStatus,
    pub modref_status: ModRefStatus,
}

impl PartialEq for AliasSet {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id 
    }
} 

impl Eq for AliasSet {}

impl Extend<Self> for AliasSet {
    fn extend<T: IntoIterator<Item = Self>>(&mut self, iter: T) {
        for x in iter {
            self.set.extend(x.set);
            self.modref_status = max(self.modref_status, x.modref_status);
            self.alias_status = max(self.alias_status, x.alias_status);
        }
    }
} 

#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Binder {
    Global(String),
    Local(String, String),
}

impl Display for Binder {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Binder::Global(s) => f.write_fmt(format_args!("@{}", s)),
            Binder::Local(fn_name, b) => f.write_fmt(format_args!("{}::%{}", fn_name, b)),
        }
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Default)]
pub struct AliasSets { 
    pub id_to_sets: HashMap<SetID, AliasSet>,
    pub binder_to_set_id: HashMap<Binder, SetID>,
}

impl AliasSets {
    pub fn new() -> Self {
        AliasSets {
            id_to_sets: Default::default(),
            binder_to_set_id: Default::default()
        }
    }
}


impl FromIterator<AliasSets> for AliasSets {
    fn from_iter<T: IntoIterator<Item = Self>>(iter: T) -> Self {
        let mut this = AliasSets::new();
        for other in iter {
            for (other_id, mut other_set) in other.id_to_sets {
                let set_ids_to_union = 
                    other_set.set.iter()
                    .filter_map(|b| this.binder_to_set_id.get(b))
                    .map(|x| x.to_owned())
                    .collect::<Vec<_>>();

                let sets_to_union =
                    set_ids_to_union
                    .into_iter()
                    .filter_map(|x| this.id_to_sets.remove(&x))
                    .collect::<Vec<_>>();

 
                for alias_set in sets_to_union {
                    other_set.set.extend(alias_set.set);
                }

                this.binder_to_set_id.extend(other_set.set.iter()
                    .map(|b| (b.clone(), other_id.clone())));
               
                this.id_to_sets.insert(other_id.clone(), other_set);

            }
        }
        this
    }
}

pub fn alias_sets(file: &str) -> AliasSets {
    let contents = fs::read_to_string(file).unwrap();
    let (_, alias_sets) = parse_alias_sets(&contents).unwrap(); // (("", AliasSets::new()));
    alias_sets
}

pub fn parse_alias_sets(input: &str) -> IResult<&str, AliasSets> {
    let (input, alias_sets) = many1(parse_alias_set_for_function)(input)?;
    let alias_sets = AliasSets::from_iter(alias_sets);
    Ok((input, alias_sets))
}

fn parse_alias_set_for_function(input: &str) -> IResult<&str, AliasSets> {
    let (input, fn_name) = parse_function_line(input)?;
    let (input, _) = parse_alias_sets_header(input)?;
    // let double_newline = pair(newline, newline);
    // map(newline, |_| HashMap::new())
    let (input, (sets, _)) = many_till(
        alt((
            map(parse_alias_set(fn_name.clone()), |x| Some(x)),
            map(many_till(not_line_ending, newline), |_| None),
        )),
        newline,
    )(input)?;
    let sets = sets.into_iter().filter_map(|x| x).collect::<Vec<_>>();
    let id_to_sets = sets
        .into_iter()
        .map(|alias_set| {
            (alias_set.id.clone(), alias_set)
        })
        .collect::<HashMap<_, _>>();

    let binder_to_set_id = id_to_sets
        .iter()
        .flat_map(|(id, alias_set)| alias_set.set.iter().map(move |b| (b.clone(), id.clone())))
        .collect::<HashMap<_, _>>();

    Ok((input, AliasSets{ id_to_sets, binder_to_set_id }))
}

fn parse_alias_sets_header(input: &str) -> IResult<&str, ()> {
    let (input, _) = take_until("\n")(input)?;
    let (input, _) = newline(input)?;
    Ok((input, ()))
}

fn parse_alias_set(fn_name: String) -> impl Fn(&str) -> IResult<&str, AliasSet> {
    move |input: &str| {
        let (input, id) = delimited(
            tag("  AliasSet["),
            parse_hex,
            tuple((tag(", "), digit1, tag("] "))),
        )(input)?;
        let (input, alias_status) =
            terminated(parse_alias_status, tuple((tag(","), space0)))(input)?;
        let (input, modref_status) = terminated(parse_modref_status, space0)(input)?;
        let parse_pair = delimited(tag("("), parse_alias_set_element(fn_name.clone()), tag(")"));
        let parse_pointers = delimited(
            tuple((tag("Pointers:"), space0)),
            separated_list1(tuple((tag(","), space0)), parse_pair),
            newline,
        );
        let parse_forwarding = tuple((tag("forwarding to "), parse_hex, newline));
        let (input, names) = alt((
            parse_pointers,
            map(parse_forwarding, |_| Vec::new()),
            map(newline, |_| Vec::new()),
        ))(input)?;
        let (input, _) = opt(tuple((tag("    "), not_line_ending, newline)))(input)?;
        Ok((
            input,
            AliasSet {
                id: format!("{}::{}", fn_name.clone(), id.to_string()),
                set: HashSet::from_iter(names),
                alias_status,
                modref_status,
            },
        ))
    }
}

fn parse_alias_set_element(fn_name: String) -> impl Fn(&str) -> IResult<&str, Binder> {
    move |input: &str| {
        let (input, (_, (_, binder_type))) = many_till(anychar, tuple((space1, alt((tag("%"), tag("@"))))))(input)?;
        let (input, (binder_name, _)) = many_till(anychar, tuple((tag(","), space0)))(input)?;
        let binder_name = binder_name.iter().collect();
        let binder = match binder_type {
            "@" => Binder::Global(binder_name),
            _ => Binder::Local(fn_name.clone(), binder_name.to_string()),
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
