# https://doc.rust-lang.org/cargo/getting-started/installation.html
echo Running test: $1
echo Producing SVF Points to Sets
../svf/build/bin/wpa -ander -print-pts $1/out.bc > $1/pts.txt
echo Transforming points to sets into alias sets
python3 coalesce_pts.py $1/pts.txt > $1/alias_sets.txt
echo Running validator
cargo run --release -- \
    --bc $1/out.bc \
    --pdg-data $1/pdg_data.csv \
    --pdg-counts-csv $1/pdg_counts.csv \
    --pdg-rollups-csv $1/pdg_subtotals.csv \
    --pdg-differences-md $1/pdg_diff.md \
    --ir-counts-csv $1/ir_counts.csv \
    --ir-rollups-csv $1/ir_subtotals.csv \
    --ir-differences-csv $1/ir_diff.csv \
    --validation-csv $1/reconcile_ir_pdg.csv \
    --alias-sets $1/alias_sets.txt \
    --validation-differences-md $1/validation_diff.md 