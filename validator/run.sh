echo Running test: $1
cargo run --release -- \
    --bc $1/out.bc \
    --pdg-data $1/pdg_data.csv \
    --pdg-counts-csv $1/pdg_counts.csv \
    --pdg-rollups-csv $1/pdg_subtotals.csv \
    --ir-counts-csv $1/ir_counts.csv \
    --ir-rollups-csv $1/ir_subtotals.csv \
    --validation-csv $1/reconcile_ir_pdg.csv