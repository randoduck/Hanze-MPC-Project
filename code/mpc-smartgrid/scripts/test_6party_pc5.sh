#!/usr/bin/env bash
set -euo pipefail

# Six-party regression: nodes 0,1,2 on Pi 5 and 3,4,5 on Pi 2,
# secrets 10..60, expected aggregate 210. Verifies all six report 210.

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"
CFG="configs/cluster.json"
EXPECTED=210
TIMEOUT_SEC=60

SECRETS=(10 20 30 40 50 60)

RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTDIR="$REPO_LOCAL/benchmark_results/sixparty_${RUN_ID}"
mkdir -p "$OUTDIR"

echo "[6party] syncing + building"
for H in "$PI5_HOST" "$PI2_HOST"; do
  rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$H:~/mpc-smartgrid/" >/dev/null
  ssh "$H" "cd $REPO_REMOTE && make clean && make"
done

host_for() { local id="$1"; (( id < 3 )) && echo "$PI5_HOST" || echo "$PI2_HOST"; }

echo "[6party] cleaning ports + stale sessions"
for H in "$PI5_HOST" "$PI2_HOST"; do
  ssh "$H" "for p in 5000 5001 5002 5003 5004 5005; do fuser -k \${p}/tcp 2>/dev/null || true; done; \
            for s in \$(tmux ls 2>/dev/null | awk -F: '/six_/{print \$1}'); do tmux kill-session -t \"\$s\" 2>/dev/null || true; done; \
            mkdir -p ~/mpc-smartgrid/logs/sixparty"
done

echo "[6party] launching 6 nodes"
for id in 0 1 2 3 4 5; do
  H="$(host_for "$id")"
  s="${SECRETS[$id]}"
  ssh "$H" "cd $REPO_REMOTE && tmux new-session -d -s six_${id} \
    'timeout ${TIMEOUT_SEC}s ./build/bench_mpc --id ${id} --config $CFG --secret ${s} > logs/sixparty/node${id}.out 2>&1'"
done

echo "[6party] waiting for completion"
deadline=$((SECONDS + TIMEOUT_SEC + 20))
while (( SECONDS < deadline )); do
  r5="$(ssh "$PI5_HOST" "tmux ls 2>/dev/null | grep -c six_ || true")"
  r2="$(ssh "$PI2_HOST" "tmux ls 2>/dev/null | grep -c six_ || true")"
  [[ "$r5" == "0" && "$r2" == "0" ]] && break
  sleep 2
done

echo "[6party] collecting logs"
rsync -av "$PI5_HOST:~/mpc-smartgrid/logs/sixparty/" "$OUTDIR/" >/dev/null 2>&1 || true
rsync -av "$PI2_HOST:~/mpc-smartgrid/logs/sixparty/" "$OUTDIR/" >/dev/null 2>&1 || true

ok=1
for id in 0 1 2 3 4 5; do
  obs="$(grep 'GLOBAL AGGREGATE' "$OUTDIR/node${id}.out" 2>/dev/null | tail -n1 | awk '{print $NF}')"
  if [[ "$obs" == "$EXPECTED" ]]; then
    echo "[6party] node${id}: $obs OK"
  else
    echo "[6party] node${id}: $obs (want $EXPECTED) FAIL"
    ok=0
  fi
done

if (( ok == 1 )); then
  echo "[6party] PASS: all six nodes reported $EXPECTED"
else
  echo "[6party] FAIL"
  exit 1
fi
