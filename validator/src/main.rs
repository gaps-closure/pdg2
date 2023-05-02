// use alias::alias_sets;

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

use alias::svf::parse_svf_sets;
use clap::Parser;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    #[arg(short, long)]
    bc: String,

    #[arg(short, long)]
    pdg_data: String,

    #[arg(short, long)]
    pdg_counts_csv: String,

    #[arg(short, long)]
    pdg_rollups_csv: String,

    #[arg(short, long)]
    pdg_differences_md: String,

    #[arg(short, long)]
    ir_counts_csv: String,

    #[arg(short, long)]
    ir_rollups_csv: String,

    #[arg(short, long)]
    ir_differences_csv: String,

    #[arg(short, long)]
    validation_csv: String,

    #[arg(short, long)]
    validation_differences_md: String,

    #[arg(short, long)]
    alias_sets: String,
}

fn main() {
    let args = Args::parse();

    let contents = std::fs::read_to_string(&args.alias_sets).unwrap();
    let (_, sets) = parse_svf_sets(&contents).unwrap();
    println!("Parsed {} sets", sets.len());
    // let args = env::args().collect::<Vec<_>>();
    // let sets = alias_sets(&args[8]);
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
        &args.bc,
        &args.pdg_data,
        &args.pdg_counts_csv,
        &args.pdg_rollups_csv,
        &args.pdg_differences_md,
        &args.ir_counts_csv,
        &args.ir_rollups_csv,
        &args.ir_differences_csv,
        &args.validation_csv,
        &args.validation_differences_md,
        &args.alias_sets
    );
}
