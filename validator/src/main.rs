use std::env;

pub mod pdg;
pub mod llvm;
pub mod report;
pub mod bag;

fn main() {
    let args = env::args().collect::<Vec<_>>();
    report::report(&args[1], &args[2],  &args[3], &args[4]);
}
