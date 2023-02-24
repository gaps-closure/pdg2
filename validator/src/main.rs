use std::env;

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

fn main() {
    let args = env::args().collect::<Vec<_>>();
    gen_report::report2(&args[1], &args[2],  &args[3], &args[4]);
}
