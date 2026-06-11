#!/usr/bin/env bash
set -euo pipefail

# Pi 2 placement sweep: at fixed N=1000, vary how many
# meters land on Pi 2 to find where repeated-run reliability collapses.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"

N=1000
TRIALS=3
PI2_VALUES=(50 100 150 200 300)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --n)          N="$2"; shift 2 ;;
    --trials)     TRIALS="$2"; shift 2 ;;
    --pi2-values) IFS=',' read -r -a PI2_VALUES <<< "$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN_ID="$(date +%Y%m%d_%H%M%S)"
PARENT="$REPO_LOCAL/benchmark_results/pi2_sweep_${RUN_ID}"
SUMMARY="$PARENT/pi2_sweep_summary.csv"
mkdir -p "$PARENT"
echo "pi2_meters,pass,trials,matrix" > "$SUMMARY"

for P2 in "${PI2_VALUES[@]}"; do
  echo ""
  echo "############## PI2 PLACEMENT Pi2=${P2} ##############"
  out="$PARENT/pi2_${P2}"
  "$SCRIPT_DIR/run_secure_agg_suite_pc5.sh" --n "$N" --trials "$TRIALS" --pi2-meters "$P2" --out "$out" || true

  m="$out/secure_agg_matrix.csv"
  pass=0
  if [[ -f "$m" ]]; then
    pass="$(tail -n +2 "$m" | awk -F',' '$10=="PASS"{c++} END{print c+0}')"
  fi
  echo "${P2},${pass},${TRIALS},${m}" >> "$SUMMARY"
done

echo ""
echo "[pi2-sweep] summary:"
cat "$SUMMARY"
