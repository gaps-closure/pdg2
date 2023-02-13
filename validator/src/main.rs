use std::env;

pub mod pdg;
pub mod llvm;
pub mod gen_report;
pub mod bag;
pub mod accounting;

fn main() {
    let args = env::args().collect::<Vec<_>>();
    gen_report::report(&args[1], &args[2],  &args[3], &args[4]);
}
