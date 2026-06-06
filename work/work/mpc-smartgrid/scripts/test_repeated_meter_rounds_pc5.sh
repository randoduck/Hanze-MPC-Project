#!/usr/bin/env bash
set -euo pipefail

# Repeated smart-meter rounds (Plan P2.3): three 2-party rounds, each run N
# trials (default 10), to build reliability evidence for the meter path.
#   round 0: 312 + 500 = 812
#   round 1: 318 + 520 = 838
#   round 2: 305 + 510 = 815

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"
CFG="configs/cluster_test2.json"
TRIALS=10

while [[ $# -gt 0 ]]; do
  case "$1" in
    --trials) TRIALS="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

PI5_VALS=(312 318 305)
PI2_VALS=(500 520 510)

RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTDIR="$REPO_LOCAL/benchmark_results/repeated_${RUN_ID}"
mkdir -p "$OUTDIR"
CSV="$OUTDIR/repeated_rounds.csv"
echo "round,trial,expected,observed_pi5,observed_pi2,pi5_ms,pi2_ms,status" > "$CSV"

echo "[repeat] syncing + building"
for H in "$PI5_HOST" "$PI2_HOST"; do
  rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$H:~/mpc-smartgrid/" >/dev/null
  ssh "$H" "cd $REPO_REMOTE && make clean && make"
done

field() { grep "$2" "$1" 2>/dev/null | tail -n1 | awk '{print $NF}'; }

for r in 0 1 2; do
  v5="${PI5_VALS[$r]}"; v2="${PI2_VALS[$r]}"
  expected=$((v5 + v2))

  ssh "$PI5_HOST" "cd $REPO_REMOTE && mkdir -p inputs/repeated logs/repeated && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,0,${v5}\n' > inputs/repeated/r${r}_pi5.csv"
  ssh "$PI2_HOST" "cd $REPO_REMOTE && mkdir -p inputs/repeated logs/repeated && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,1,${v2}\n' > inputs/repeated/r${r}_pi2.csv"

  for ((t=1; t<=TRIALS; t++)); do
    ssh "$PI5_HOST" "fuser -k 5000/tcp 5001/tcp 2>/dev/null || true; tmux kill-session -t rep 2>/dev/null || true"
    ssh "$PI2_HOST" "pkill -x meter_node 2>/dev/null || true"

    ssh "$PI5_HOST" "cd $REPO_REMOTE && tmux new-session -d -s rep \
      'timeout 30s ./build/meter_node --id 0 --config $CFG --input inputs/repeated/r${r}_pi5.csv > logs/repeated/r${r}_t${t}_pi5.out 2>&1'"
    sleep 1
    ssh "$PI2_HOST" "cd $REPO_REMOTE && \
      timeout 30s ./build/meter_node --id 1 --config $CFG --input inputs/repeated/r${r}_pi2.csv > logs/repeated/r${r}_t${t}_pi2.out 2>&1" || true

    l5="$OUTDIR/r${r}_t${t}_pi5.out"; l2="$OUTDIR/r${r}_t${t}_pi2.out"
    rsync -av "$PI5_HOST:~/mpc-smartgrid/logs/repeated/r${r}_t${t}_pi5.out" "$l5" >/dev/null 2>&1 || true
    rsync -av "$PI2_HOST:~/mpc-smartgrid/logs/repeated/r${r}_t${t}_pi2.out" "$l2" >/dev/null 2>&1 || true

    o5="$(field "$l5" 'GLOBAL AGGREGATE')"; o2="$(field "$l2" 'GLOBAL AGGREGATE')"
    ms5="$(field "$l5" 'protocol_ms')"; ms2="$(field "$l2" 'protocol_ms')"

    status="PASS"
    [[ "$o5" == "$expected" && "$o2" == "$expected" ]] || status="FAIL"

    echo "${r},${t},${expected},${o5},${o2},${ms5},${ms2},${status}" >> "$CSV"
    echo "[repeat] round ${r} trial ${t}: ${status} (Pi5=$o5 Pi2=$o2 want $expected)"
  done
done

echo ""
echo "[repeat] matrix: $CSV"
cat "$CSV"
