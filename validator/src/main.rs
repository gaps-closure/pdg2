pub mod pdg;
pub mod llvm;
pub mod report;
pub mod bag;

fn main() {
    report::report("out.bc", "pdg_data.csv", "ir.csv", "validation.csv");
}
