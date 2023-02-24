use std::collections::HashSet;
use std::collections::hash_map::RandomState;
use std::collections::hash_set::{Intersection, Difference};
use std::hash::Hash;

pub struct Account<A> {
   pub a: HashSet<A>,
   pub b: HashSet<A>,
} 

impl<A> Account<A> 
    where A: Eq + Hash + Sized {
    pub fn account(set_a: HashSet<A>, set_b: HashSet<A>) -> Account<A> {
        Account {
            a: set_a,
            b: set_b
        }
    }
    pub fn intersection(&self) -> impl Iterator<Item = &A> {
        self.a.intersection(&self.b)
    } 
    pub fn a_minus_b(&self) -> impl Iterator<Item = &A> {
        self.a.difference(&self.b)
    }
    pub fn b_minus_a(&self) -> impl Iterator<Item = &A> {
        self.b.difference(&self.a)
    }
}
