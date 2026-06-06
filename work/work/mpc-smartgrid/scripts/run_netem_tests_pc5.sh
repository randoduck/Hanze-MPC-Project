#!/usr/bin/env bash
set -euo pipefail

# Network impairment tests (Plan P2.10 / Task 8). Applies tc netem on the
# tailscale interface and runs both the 2-party meter path and the 1000-meter
# secure aggregation path under each condition. netem is always removed on
# exit, even if a run fails (cleanup trap).

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CFG="configs/cluster_test2.json"
IFACE="tailscale0"
APPLY_BOTH=0          # by default impair Pi 5 only; --both impairs Pi 2 too

while [[ $# -gt 0 ]]; do
  case "$1" in
    --iface) IFACE="$2"; shift 2 ;;
    --both)  APPLY_BOTH=1; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTDIR="$REPO_LOCAL/benchmark_results/netem_${RUN_ID}"
mkdir -p "$OUTDIR"
CSV="$OUTDIR/netem_summary.csv"
echo "condition,meter_status,meter_observed,secure_agg_status,secure_agg_received" > "$CSV"

hosts() { if (( APPLY_BOTH == 1 )); then echo "$PI5_HOST $PI2_HOST"; else echo "$PI5_HOST"; fi; }

netem_clear() {
  for H in $(hosts); do
    ssh "$H" "sudo tc qdisc del dev ${IFACE} root 2>/dev/null || true"
  done
}

netem_apply() {
  local spec="$1"
  for H in $(hosts); do
    ssh "$H" "sudo tc qdisc del dev ${IFACE} root 2>/dev/null || true"
    if [[ -n "$spec" ]]; then
      ssh "$H" "sudo tc qdisc add dev ${IFACE} root netem ${spec}"
    fi
  done
}

# Always remove impairment on exit.
trap netem_clear EXIT

echo "[netem] syncing + building"
for H in "$PI5_HOST" "$PI2_HOST"; do
  rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$H:~/mpc-smartgrid/" >/dev/null
  ssh "$H" "cd $REPO_REMOTE && make clean && make"
done

run_2party_meter() {
  ssh "$PI5_HOST" "fuser -k 5000/tcp 5001/tcp 2>/dev/null || true; tmux kill-session -t netem 2>/dev/null || true"
  ssh "$PI2_HOST" "pkill -x meter_node 2>/dev/null || true"
  ssh "$PI5_HOST" "cd $REPO_REMOTE && mkdir -p inputs/netem logs/netem && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,0,312\n' > inputs/netem/pi5.csv && tmux new-session -d -s netem 'timeout 60s ./build/meter_node --id 0 --config $CFG --input inputs/netem/pi5.csv > logs/netem/meter_pi5.out 2>&1'"
  sleep 1
  ssh "$PI2_HOST" "cd $REPO_REMOTE && mkdir -p inputs/netem logs/netem && printf 'timestamp,meter_id,energy_wh\n2026-05-31T18:30:00Z,1,500\n' > inputs/netem/pi2.csv && timeout 60s ./build/meter_node --id 1 --config $CFG --input inputs/netem/pi2.csv > logs/netem/meter_pi2.out 2>&1" || true
  ssh "$PI5_HOST" "grep 'GLOBAL AGGREGATE' ~/mpc-smartgrid/logs/netem/meter_pi5.out 2>/dev/null | tail -n1 | awk '{print \$NF}'"
}

declare -a NAMES=("baseline" "delay100" "delay100_jitter20" "delay100_loss1" "delay100_loss3")
declare -a SPECS=("" "delay 100ms" "delay 100ms 20ms" "delay 100ms loss 1%" "delay 100ms loss 3%")

for idx in "${!NAMES[@]}"; do
  cond="${NAMES[$idx]}"; spec="${SPECS[$idx]}"
  echo ""
  echo "############## NETEM ${cond} (${spec:-none}) ##############"
  netem_apply "$spec"

  obs="$(run_2party_meter || true)"
  mstatus="PASS"; [[ "$obs" == "812" ]] || mstatus="FAIL"

  out="$OUTDIR/${cond}_secure_agg"
  "$SCRIPT_DIR/run_secure_agg_suite_pc5.sh" --n 1000 --trials 1 --out "$out" || true
  m="$out/secure_agg_matrix.csv"
  sstatus="FAIL"; srecv=""
  if [[ -f "$m" ]]; then
    sstatus="$(tail -n1 "$m" | awk -F',' '{print $10}')"
    srecv="$(tail -n1 "$m" | awk -F',' '{print $9}')"
  fi

  echo "${cond},${mstatus},${obs},${sstatus},${srecv}" >> "$CSV"
done

netem_clear
echo ""
echo "[netem] summary:"
cat "$CSV"
