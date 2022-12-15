use std::{collections::HashMap, iter::FromIterator};

pub struct Bag<K, V> {
    pub hashmap: HashMap<K, Vec<V>> 
}

impl<K: std::hash::Hash + Eq, V> Bag<K, V> {
    pub fn new() -> Bag<K, V> {
        Bag { hashmap: HashMap::new() }
    }
    pub fn sizes(&self) -> HashMap<&K, usize> {
        self
            .hashmap
            .iter()
            .map(|(k, v)| (k.clone(), v.len()))
            .collect()
    }
    pub fn insert(&mut self, key: K, value: V) -> Option<Vec<V>> {
        match self.hashmap.remove(&key) {
            Some(mut bag) => {
                bag.push(value);
                self.hashmap.insert(key, bag)
            },
            None => {
                self.hashmap.insert(key, vec![value])
            }
        }
    }
    pub fn insert_all<I: IntoIterator<Item = V>>(&mut self, key: K, values: I) -> Option<Vec<V>> {
        match self.hashmap.remove(&key) {
            Some(mut bag) => {
                bag.extend(values.into_iter());
                self.hashmap.insert(key, bag)
            },
            None => {
                self.hashmap.insert(key, values.into_iter().collect()) 
            }
        }
    }
}

impl<K: std::hash::Hash + Eq, V> FromIterator<(K, V)> for Bag<K, V> {
    fn from_iter<T: IntoIterator<Item = (K, V)>>(iter: T) -> Self {
        let mut bag = Bag::new();
        for (k, v) in iter {
            bag.insert(k, v);
        }
        bag
    }
}