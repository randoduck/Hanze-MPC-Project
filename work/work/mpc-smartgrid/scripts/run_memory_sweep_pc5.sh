#!/usr/bin/env bash
set -euo pipefail

# Memory sweep (Plan P2.9 / Task 7): at fixed N=1000 / placement 900-100, lower
# the per-meter memory cap to find the minimum viable value.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"

N=1000
TRIALS=3
MEM_VALUES_KB=(32768 16384 8192 4096)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --n)        N="$2"; shift 2 ;;
    --trials)   TRIALS="$2"; shift 2 ;;
    --mem-kb)   IFS=',' read -r -a MEM_VALUES_KB <<< "$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN_ID="$(date +%Y%m%d_%H%M%S)"
PARENT="$REPO_LOCAL/benchmark_results/mem_sweep_${RUN_ID}"
SUMMARY="$PARENT/mem_sweep_summary.csv"
mkdir -p "$PARENT"
echo "per_meter_mem_kb,pass,trials,matrix" > "$SUMMARY"

for MEM in "${MEM_VALUES_KB[@]}"; do
  echo ""
  echo "############## MEMORY SWEEP per-meter=${MEM}KB ##############"
  out="$PARENT/mem_${MEM}"
  "$SCRIPT_DIR/run_secure_agg_suite_pc5.sh" --n "$N" --trials "$TRIALS" \
    --pi2-min 100 --pi2-max 100 --meter-mem-kb "$MEM" --out "$out" || true

  m="$out/secure_agg_matrix.csv"
  pass=0
  if [[ -f "$m" ]]; then
    pass="$(tail -n +2 "$m" | awk -F',' '$10=="PASS"{c++} END{print c+0}')"
  fi
  echo "${MEM},${pass},${TRIALS},${m}" >> "$SUMMARY"
done

echo ""
echo "[mem-sweep] summary:"
cat "$SUMMARY"
