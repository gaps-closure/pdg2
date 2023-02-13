use std::collections::HashSet;
use std::collections::hash_map::RandomState;
use std::collections::hash_set::{Intersection, Difference};
use std::hash::Hash;

pub struct Account<'a, A> {
   pub a: &'a HashSet<A>,
   pub b: &'a HashSet<A>,
   pub intersection_a_b: Intersection<'a, A, RandomState>,
   pub a_minus_b:  Difference<'a, A, RandomState>,
   pub b_minus_a:  Difference<'a, A, RandomState>
} 

impl<'a, A> Account<'a, A> 
    where A: Eq + Hash + Sized {
    pub fn account(set_a: &'a HashSet<A>, set_b: &'a HashSet<A>) -> Account<'a, A> {
        let intersection_a_b =  
            set_a.intersection(&set_b);
        let a_minus_b = set_a.difference(set_b);
        let b_minus_a = set_b.difference(set_a);
        Account {
            a: set_a,
            b: set_b,
            intersection_a_b,
            a_minus_b,
            b_minus_a,
        }
    }
}
