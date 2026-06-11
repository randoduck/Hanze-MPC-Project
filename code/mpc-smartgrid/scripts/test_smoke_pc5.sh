#!/usr/bin/env bash
set -euo pipefail

# Smoke test: the first thing to run after any code change.
# Syncs source, rebuilds both Pis, runs make test, then a 2-party bench_mpc
# (expect 1000) and a 2-party meter_node (expect 812).

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"
CFG="configs/cluster_test2.json"

RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTDIR="$REPO_LOCAL/benchmark_results/smoke_${RUN_ID}"
mkdir -p "$OUTDIR"

fail() { echo "[smoke] FAIL: $*"; exit 1; }

echo "[smoke] syncing source"
rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
  "$REPO_LOCAL/" "$PI5_HOST:~/mpc-smartgrid/" >/dev/null
rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
  "$REPO_LOCAL/" "$PI2_HOST:~/mpc-smartgrid/" >/dev/null

echo "[smoke] building + unit tests on Pi 5"
ssh "$PI5_HOST" "cd $REPO_REMOTE && make clean && make && make test" || fail "Pi5 build/test"
echo "[smoke] building + unit tests on Pi 2"
ssh "$PI2_HOST" "cd $REPO_REMOTE && make clean && make && make test" || fail "Pi2 build/test"

extract() { grep "$2" "$1" 2>/dev/null | tail -n 1 | awk -F'=' '{gsub(/[ \t]/,"",$2); print $2}'; }

cleanup_ports() {
  ssh "$PI5_HOST" "fuser -k 5000/tcp 5001/tcp 2>/dev/null || true; tmux kill-session -t smoke 2>/dev/null || true"
  ssh "$PI2_HOST" "pkill -x bench_mpc 2>/dev/null || true; pkill -x meter_node 2>/dev/null || true"
}

# ---- 2-party bench_mpc: 300 + 700 = 1000 ----
echo "[smoke] 2-party bench_mpc (expect 1000)"
cleanup_ports
ssh "$PI5_HOST" "cd $REPO_REMOTE && mkdir -p logs/smoke && tmux new-session -d -s smoke \
  'timeout 30s ./build/bench_mpc --id 0 --config $CFG --secret 300 > logs/smoke/bench_pi5.out 2>&1'"
sleep 1
ssh "$PI2_HOST" "cd $REPO_REMOTE && mkdir -p logs/smoke && \
  timeout 30s ./build/bench_mpc --id 1 --config $CFG --secret 700 > logs/smoke/bench_pi2.out 2>&1" || true

rsync -av "$PI5_HOST:~/mpc-smartgrid/logs/smoke/bench_pi5.out" "$OUTDIR/" >/dev/null 2>&1 || true
rsync -av "$PI2_HOST:~/mpc-smartgrid/logs/smoke/bench_pi2.out" "$OUTDIR/" >/dev/null 2>&1 || true

b5="$(extract "$OUTDIR/bench_pi5.out" 'GLOBAL AGGREGATE')"
b2="$(extract "$OUTDIR/bench_pi2.out" 'GLOBAL AGGREGATE')"
[[ "$b5" == "1000" && "$b2" == "1000" ]] || fail "bench_mpc aggregate Pi5=$b5 Pi2=$b2 (want 1000)"
echo "[smoke] bench_mpc OK (Pi5=$b5 Pi2=$b2)"

# ---- 2-party meter_node: 312 + 500 = 812 ----
echo "[smoke] 2-party meter_node (expect 812)"
cleanup_ports
ssh "$PI5_HOST" "cd $REPO_REMOTE && mkdir -p inputs/smoke && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,0,312\n' > inputs/smoke/pi5.csv"
ssh "$PI2_HOST" "cd $REPO_REMOTE && mkdir -p inputs/smoke && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,1,500\n' > inputs/smoke/pi2.csv"

ssh "$PI5_HOST" "cd $REPO_REMOTE && tmux new-session -d -s smoke \
  'timeout 30s ./build/meter_node --id 0 --config $CFG --input inputs/smoke/pi5.csv > logs/smoke/meter_pi5.out 2>&1'"
sleep 1
ssh "$PI2_HOST" "cd $REPO_REMOTE && \
  timeout 30s ./build/meter_node --id 1 --config $CFG --input inputs/smoke/pi2.csv > logs/smoke/meter_pi2.out 2>&1" || true

rsync -av "$PI5_HOST:~/mpc-smartgrid/logs/smoke/meter_pi5.out" "$OUTDIR/" >/dev/null 2>&1 || true
rsync -av "$PI2_HOST:~/mpc-smartgrid/logs/smoke/meter_pi2.out" "$OUTDIR/" >/dev/null 2>&1 || true

m5="$(grep 'GLOBAL AGGREGATE' "$OUTDIR/meter_pi5.out" 2>/dev/null | tail -n1 | awk '{print $NF}')"
m2="$(grep 'GLOBAL AGGREGATE' "$OUTDIR/meter_pi2.out" 2>/dev/null | tail -n1 | awk '{print $NF}')"
[[ "$m5" == "812" && "$m2" == "812" ]] || fail "meter_node aggregate Pi5=$m5 Pi2=$m2 (want 812)"
echo "[smoke] meter_node OK (Pi5=$m5 Pi2=$m2)"

echo "[smoke] ALL SMOKE CHECKS PASSED"
echo "[smoke] logs: $OUTDIR"
