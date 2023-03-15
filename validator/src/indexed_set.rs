use std::fmt::Display;
use std::hash::Hash;
use std::{
    collections::{HashMap, HashSet},
    iter::{FromIterator},
};

use crate::accounting::{Account};
use crate::id::ID;
use crate::report::Report;

pub struct ISet<K, V> {
    pub hashmap: HashMap<K, HashSet<V>>,
    empty: HashSet<V>,
}

impl<K: Hash + Eq, V: Hash + Eq> ISet<K, V> {
    pub fn new() -> ISet<K, V> {
        ISet {
            hashmap: HashMap::new(),
            empty: HashSet::new(),
        }
    }
    pub fn sizes(&self) -> HashMap<&K, usize> {
        self.hashmap.iter().map(|(k, s)| (k, s.len())).collect()
    }
    pub fn insert(&mut self, key: K, value: V) -> Option<HashSet<V>> {
        match self.hashmap.remove(&key) {
            Some(mut set) => {
                set.insert(value);
                self.hashmap.insert(key, set)
            }
            None => self.hashmap.insert(key, HashSet::from_iter([value])),
        }
    }
    pub fn insert_empty(&mut self, key: K) {
        if !self.hashmap.contains_key(&key) {
            self.hashmap.insert(key, HashSet::new());
        }
    }
    pub fn insert_all<I: IntoIterator<Item = V>>(
        &mut self,
        key: K,
        values: I,
    ) -> Option<HashSet<V>> {
        match self.hashmap.remove(&key) {
            Some(mut set) => {
                set.extend(values.into_iter());
                self.hashmap.insert(key, set)
            }
            None => self.hashmap.insert(key, values.into_iter().collect()),
        }
    }
    pub fn get(&self, key: &K) -> &HashSet<V> {
        self.hashmap.get(key).unwrap_or(&self.empty)
    }
    pub fn iter(&self) -> impl Iterator<Item = (&K, &V)> {
        self.hashmap
            .iter()
            .flat_map(|(k, s)| s.iter().map(move |v| (k, v)))
    }
}

impl<K: Hash + Eq, V: Hash + Eq> FromIterator<(K, V)> for ISet<K, V> {
    fn from_iter<T: IntoIterator<Item = (K, V)>>(iter: T) -> Self {
        let mut iset = ISet::new();
        for (k, v) in iter {
            iset.insert(k, v);
        }
        iset
    }
}

impl<K: Hash + Eq, V: Hash + Eq> Extend<(K, V)> for ISet<K, V> {
    fn extend<T: IntoIterator<Item = (K, V)>>(&mut self, iter: T) {
        for (k, v) in iter {
            self.insert(k, v);
        }
    }
}

impl<K: Hash + Eq + Clone + 'static, V: Hash + Eq + 'static> IntoIterator for ISet<K, V> {
    type Item = (K, V);

    type IntoIter = Box<dyn Iterator<Item = Self::Item>>;

    fn into_iter(self) -> Self::IntoIter {
        Box::new(
            self.hashmap
                .into_iter()
                .flat_map(|(k, s)| s.into_iter().map(move |v| (k.clone(), v))),
        )
    }
}

impl<V: Hash + Eq + Clone + Display + 'static> ISet<ID, V> {
    pub fn rollup_prefixes(&mut self) {
        let mut k = self.hashmap.iter().map(|(k, _)| k.len()).max().unwrap();
        while k > 1 {
            let prefixes = self
                .hashmap
                .iter()
                .filter_map(|(v, n)| {
                    if v.len() != k {
                        None
                    } else {
                        Some((v.prefix(k - 1), n.to_owned()))
                    }
                })
                .collect::<Vec<_>>();
            for (k, v) in prefixes {
                self.insert_all(k, v);
            }
            k = k - 1;
        }
    }
    pub fn report_counts(&self, report: &mut Report) {
        for (k, v) in &self.hashmap {
            report.report_count(k.clone(), v.len());
        }
    }

    pub fn filter_prefix<'a>(
        &'a self,
        pref: &'a ID,
    ) -> impl Iterator<Item = (&'a ID, &'a HashSet<V>)> {
        self.hashmap.iter().filter_map(move |(id, c)| {
            if id.len() != pref.len() + 1 {
                return None;
            }
            if &id.prefix(pref.len()) == pref {
                Some((id, c))
            } else {
                None
            }
        })
    }
    pub fn report_rollups(&self, report: &mut Report) {
        for (rollup_given_name, rollup_given) in &self.hashmap {
            let rollup = self.filter_prefix(&rollup_given_name).collect::<Vec<_>>();
            if rollup.len() == 0 {
                continue;
            }
            let mut rollup_calculated_name = rollup
                .iter()
                .map(|(k, _)| k.to_string())
                .collect::<Vec<_>>();
            rollup_calculated_name.sort(); 
            let rollup_calculated_name = rollup_calculated_name.join(" + ");
            let rollup_calculated = rollup
                .into_iter()
                .map(|(_, v)| v.clone())
                .reduce(|mut acc, x| {
                    acc.extend(x);
                    acc
                })
                .unwrap();

            let accounting = Account::account(rollup_given.clone(), rollup_calculated);

            report.report_account(rollup_given_name.to_string(), rollup_calculated_name, accounting);
        }
    }
}
