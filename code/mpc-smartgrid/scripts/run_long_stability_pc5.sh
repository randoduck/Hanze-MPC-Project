#!/usr/bin/env bash
set -euo pipefail

# Long-running stability test: repeats a workload on an interval
# for a fixed duration to surface slow leaks / flakiness needed for reliability
# claims.
#
#   --mode meter   2-party meter_node every --interval s (default 5)
#   --mode agg     secure aggregation N=1000 every --interval s (default 120)
#   --duration     total seconds to run (default 3600 = 1 hour)

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CFG="configs/cluster_test2.json"

MODE="meter"
DURATION=3600
INTERVAL=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)     MODE="$2"; shift 2 ;;
    --duration) DURATION="$2"; shift 2 ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

[[ -z "$INTERVAL" ]] && { [[ "$MODE" == "meter" ]] && INTERVAL=5 || INTERVAL=120; }

RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTDIR="$REPO_LOCAL/benchmark_results/stability_${MODE}_${RUN_ID}"
mkdir -p "$OUTDIR"
CSV="$OUTDIR/stability.csv"
echo "trial,timestamp,status,wall_ms,received,observed,reason" > "$CSV"

echo "[stability] mode=$MODE duration=${DURATION}s interval=${INTERVAL}s"
echo "[stability] syncing + building"
for H in "$PI5_HOST" "$PI2_HOST"; do
  rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$H:~/mpc-smartgrid/" >/dev/null
  ssh "$H" "cd $REPO_REMOTE && make clean && make"
done

field() { grep "$2" "$1" 2>/dev/null | tail -n1 | awk '{print $NF}'; }

run_meter_once() {
  local trial="$1"
  ssh "$PI5_HOST" "fuser -k 5000/tcp 5001/tcp 2>/dev/null || true; tmux kill-session -t stab 2>/dev/null || true"
  ssh "$PI2_HOST" "pkill -x meter_node 2>/dev/null || true"
  ssh "$PI5_HOST" "cd $REPO_REMOTE && mkdir -p inputs/stab logs/stab && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,0,312\n' > inputs/stab/pi5.csv && tmux new-session -d -s stab 'timeout 30s ./build/meter_node --id 0 --config $CFG --input inputs/stab/pi5.csv > logs/stab/t${trial}.out 2>&1'"
  sleep 1
  ssh "$PI2_HOST" "cd $REPO_REMOTE && mkdir -p inputs/stab && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,1,500\n' > inputs/stab/pi2.csv && timeout 30s ./build/meter_node --id 1 --config $CFG --input inputs/stab/pi2.csv > /dev/null 2>&1" || true
  local l="$OUTDIR/meter_t${trial}.out"
  rsync -av "$PI5_HOST:~/mpc-smartgrid/logs/stab/t${trial}.out" "$l" >/dev/null 2>&1 || true
  local obs ms status
  obs="$(field "$l" 'GLOBAL AGGREGATE')"; ms="$(field "$l" 'protocol_ms')"
  status="PASS"; [[ "$obs" == "812" ]] || status="FAIL"
  echo "${trial},$(date -Is),${status},${ms},,${obs}," >> "$CSV"
  echo "[stability] meter trial ${trial}: ${status} (obs=$obs)"
}

run_agg_once() {
  local trial="$1"
  local out="$OUTDIR/agg_t${trial}"
  "$SCRIPT_DIR/run_secure_agg_suite_pc5.sh" --n 1000 --trials 1 --out "$out" >/dev/null 2>&1 || true
  local m="$out/secure_agg_matrix.csv"
  local status="FAIL" wall="" recv="" obs="" reason=""
  if [[ -f "$m" ]]; then
    status="$(tail -n1 "$m" | awk -F',' '{print $10}')"
    wall="$(tail -n1 "$m" | awk -F',' '{print $12}')"
    recv="$(tail -n1 "$m" | awk -F',' '{print $9}')"
    obs="$(tail -n1 "$m" | awk -F',' '{print $8}')"
    reason="$(tail -n1 "$m" | awk -F',' '{print $11}')"
  fi
  echo "${trial},$(date -Is),${status},${wall},${recv},${obs},${reason}" >> "$CSV"
  echo "[stability] agg trial ${trial}: ${status} (recv=$recv wall=$wall)"
}

start=$SECONDS
trial=0
while (( SECONDS - start < DURATION )); do
  trial=$((trial + 1))
  if [[ "$MODE" == "meter" ]]; then
    run_meter_once "$trial"
  else
    run_agg_once "$trial"
  fi
  sleep "$INTERVAL"
done

echo ""
echo "[stability] complete: $trial trials over ${DURATION}s"
echo "[stability] matrix: $CSV"
