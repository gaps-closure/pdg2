use std::env;

use alias::alias_sets;

pub mod pdg;
pub mod llvm;
pub mod gen_report;
pub mod bag;
pub mod accounting;
pub mod report;
pub mod id;
pub mod counts; 
pub mod indexed_set; 
pub mod validator;
pub mod alias;

fn main() {
    let args = env::args().collect::<Vec<_>>();
    // let sets = alias_sets("../basicaa.out");
    // for ((fn_name, binder), si) in sets.0 {
        // println!("{} -> {} ({:?})", fn_name, binder, si);
    // }
    gen_report::report2(&args[1], &args[2],  &args[3], &args[4]);
}
