use std::{fmt::Display, cmp::max, collections::{HashMap, HashSet}, iter::FromIterator};

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