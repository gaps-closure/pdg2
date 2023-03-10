use std::env;

use alias::alias_sets;

pub mod accounting;
pub mod alias;
pub mod bag;
pub mod counts;
pub mod gen_report;
pub mod id;
pub mod indexed_set;
pub mod llvm;
pub mod pdg;
pub mod report;
pub mod validator;

fn main() {
    let args = env::args().collect::<Vec<_>>();
    // let sets = alias_sets("../basicaa.out");
    // for (id, alias_set) in sets.id_to_sets {
    //     let mut s = String::new();
    //     for binder in &alias_set.set {
    //         let x = format!("\n\t{}", binder.to_string());
    //         s += &x;
    //     }
    //     println!(
    //         "{} {:?}, {:?}, (len {}): {}",
    //         id,
    //         alias_set.alias_status,
    //         alias_set.modref_status,
    //         alias_set.set.len(),
    //         s
    //     );
    // }
    gen_report::report2(
        &args[1], &args[2], &args[3], &args[4], &args[5], &args[6], &args[7],
    );
}
