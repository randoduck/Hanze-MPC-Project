#!/usr/bin/env bash
set -euo pipefail

# Load sweep (Plan P2.7 / Task 5): find the secure-aggregation capacity curve.
# Runs the suite at increasing N with proportional placement, 3 trials each.
# Stops at the first N where every trial fails (--stop-cliff) or runs all.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"

N_VALUES=(250 500 750 1000 1250 1500)
TRIALS=3
STOP_CLIFF=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --n-values)  IFS=',' read -r -a N_VALUES <<< "$2"; shift 2 ;;
    --trials)    TRIALS="$2"; shift 2 ;;
    --no-stop)   STOP_CLIFF=0; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN_ID="$(date +%Y%m%d_%H%M%S)"
PARENT="$REPO_LOCAL/benchmark_results/load_sweep_${RUN_ID}"
SUMMARY="$PARENT/load_sweep_summary.csv"
mkdir -p "$PARENT"
echo "n,pass,trials,matrix" > "$SUMMARY"

for N in "${N_VALUES[@]}"; do
  echo ""
  echo "############## LOAD SWEEP N=${N} ##############"
  out="$PARENT/N${N}"
  "$SCRIPT_DIR/run_secure_agg_suite_pc5.sh" --n "$N" --trials "$TRIALS" --out "$out" || true

  m="$out/secure_agg_matrix.csv"
  pass=0
  if [[ -f "$m" ]]; then
    pass="$(tail -n +2 "$m" | awk -F',' '$10=="PASS"{c++} END{print c+0}')"
  fi
  echo "${N},${pass},${TRIALS},${m}" >> "$SUMMARY"

  if (( STOP_CLIFF == 1 )) && (( pass == 0 )); then
    echo "[load-sweep] N=${N} failed every trial; stopping (capacity cliff)"
    break
  fi
done

echo ""
echo "[load-sweep] summary:"
cat "$SUMMARY"
