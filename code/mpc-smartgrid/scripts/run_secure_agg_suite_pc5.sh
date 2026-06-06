#!/usr/bin/env bash
set -euo pipefail

# General, hardened secure-aggregation runner.
#
# Runs one or more trials of the central secure aggregation path across Pi 5
# and Pi 2 and writes a machine-readable CSV. Generalizes the old
# run_final_1000 script (Plan P2.6, Task 3): server readiness check, in-server
# timeout, batched client launch, missing-ID reporting, captured exit codes,
# and per-run failure reasons.
#
# Usage:
#   ./scripts/run_secure_agg_suite_pc5.sh \
#     --n 1000 --trials 10 \
#     --pi2-min 100 --pi2-max 300 \
#     [--pi2-meters K]            # fixed Pi 2 placement (overrides min/max)
#     [--meter-mem-kb 16384] \
#     [--pi5-group-cpu 300%] [--pi5-group-mem 1024M] \
#     [--pi2-group-cpu 100%] [--pi2-group-mem 256M] \
#     [--port 7000] [--timeout-sec 420] \
#     [--batch-size 50] [--batch-gap-ms 200] \
#     [--out DIR] [--stop-on-fail]

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"
PI5_IP="100.101.20.65"
REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"

# ---- Defaults (the validated configuration) -----------------------------
N=1000
TRIALS=1
PI2_MIN=100
PI2_MAX=300
PI2_FIXED=""
METER_MEM_KB=16384
METER_CPU_SECONDS=10
METER_FD_CAP=64
PI5_GROUP_CPU="300%"
PI5_GROUP_MEM="1024M"
PI2_GROUP_CPU="100%"
PI2_GROUP_MEM="256M"
PORT=7000
TIMEOUT_SEC=420
BATCH_SIZE=50
BATCH_GAP_MS=200
MASK_KEY="0x20260606ABCDEF01"
STOP_ON_FAIL=0
OUTDIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --n)             N="$2"; shift 2 ;;
    --trials)        TRIALS="$2"; shift 2 ;;
    --pi2-min)       PI2_MIN="$2"; shift 2 ;;
    --pi2-max)       PI2_MAX="$2"; shift 2 ;;
    --pi2-meters)    PI2_FIXED="$2"; shift 2 ;;
    --meter-mem-kb)  METER_MEM_KB="$2"; shift 2 ;;
    --meter-cpu-sec) METER_CPU_SECONDS="$2"; shift 2 ;;
    --pi5-group-cpu) PI5_GROUP_CPU="$2"; shift 2 ;;
    --pi5-group-mem) PI5_GROUP_MEM="$2"; shift 2 ;;
    --pi2-group-cpu) PI2_GROUP_CPU="$2"; shift 2 ;;
    --pi2-group-mem) PI2_GROUP_MEM="$2"; shift 2 ;;
    --port)          PORT="$2"; shift 2 ;;
    --timeout-sec)   TIMEOUT_SEC="$2"; shift 2 ;;
    --batch-size)    BATCH_SIZE="$2"; shift 2 ;;
    --batch-gap-ms)  BATCH_GAP_MS="$2"; shift 2 ;;
    --out)           OUTDIR="$2"; shift 2 ;;
    --stop-on-fail)  STOP_ON_FAIL=1; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN_ID="$(date +%Y%m%d_%H%M%S)"
[[ -z "$OUTDIR" ]] && OUTDIR="$REPO_LOCAL/benchmark_results/secure_agg_${RUN_ID}"
mkdir -p "$OUTDIR"
CSV="$OUTDIR/secure_agg_matrix.csv"

cat > "$CSV" <<EOF
run_id,timestamp,test_name,N,pi5_meters,pi2_meters,expected,observed,received,status,reason,wall_ms,aggregator_rss_kb,pi5_group_cpu,pi5_group_mem,pi2_group_cpu,pi2_group_mem,per_meter_mem_kb,per_meter_fd_cap,per_meter_cpu_seconds,missing_count,missing_ids_file,failed_meter_logs_count,server_exit_code,pi5_launcher_exit_code,pi2_launcher_exit_code
EOF

value_for_node() { echo $((100 + ((37*$1 + 11*$1*$1 + 73) % 900))); }

expected_sum() {
  local sum=0 i
  for ((i=0; i<N; i++)); do sum=$((sum + $(value_for_node "$i"))); done
  echo "$sum"
}

compute_placement() {
  # Proportional to (MemAvailable * cores), then clamped to [PI2_MIN, PI2_MAX],
  # unless --pi2-meters fixes it explicitly.
  if [[ -n "$PI2_FIXED" ]]; then
    PI2_METERS="$PI2_FIXED"
    PI5_METERS=$((N - PI2_METERS))
    return
  fi

  local pi5_mem pi2_mem pi5_cores pi2_cores
  pi5_mem="$(ssh "$PI5_HOST" "awk '/MemAvailable/ {print int(\$2/1024)}' /proc/meminfo")"
  pi2_mem="$(ssh "$PI2_HOST" "awk '/MemAvailable/ {print int(\$2/1024)}' /proc/meminfo")"
  pi5_cores="$(ssh "$PI5_HOST" "nproc")"
  pi2_cores="$(ssh "$PI2_HOST" "nproc")"

  local pi5_score=$((pi5_mem * pi5_cores))
  local pi2_score=$((pi2_mem * pi2_cores))
  local total=$((pi5_score + pi2_score))

  PI5_METERS=$((N * pi5_score / total))
  PI2_METERS=$((N - PI5_METERS))

  if (( PI2_METERS < PI2_MIN )); then PI2_METERS=$PI2_MIN; PI5_METERS=$((N - PI2_METERS)); fi
  if (( PI2_METERS > PI2_MAX )); then PI2_METERS=$PI2_MAX; PI5_METERS=$((N - PI2_METERS)); fi
}

extract() { grep "$2" "$1" 2>/dev/null | tail -n 1 | awk -F'=' '{gsub(/[ \t]/,"",$2); print $2}'; }

sync_and_build() {
  echo "[suite] syncing source"
  rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$PI5_HOST:~/mpc-smartgrid/" >/dev/null
  rsync -av --delete --exclude build/ --exclude logs/ --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$PI2_HOST:~/mpc-smartgrid/" >/dev/null

  echo "[suite] building"
  ssh "$PI5_HOST" "cd $REPO_REMOTE && make clean && make"
  ssh "$PI2_HOST" "cd $REPO_REMOTE && make clean && make"
}

wait_server_ready() {
  # Poll until the aggregator is actually listening before launching clients.
  local k
  for ((k=0; k<150; k++)); do
    if ssh "$PI5_HOST" "ss -ltn | grep -q ':${PORT} '"; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

run_trial() {
  local trial="$1"
  local rel="logs/secure_agg/${RUN_ID}/trial${trial}"
  local test_id=$(( ($(date +%s) + trial) % 2000000000 ))

  echo ""
  echo "============================================================"
  echo "[suite] trial ${trial}/${TRIALS}: N=${N} Pi5=${PI5_METERS} Pi2=${PI2_METERS} test_id=${test_id}"
  echo "============================================================"

  local pi5_start=0
  local pi5_end=$((PI5_METERS - 1))
  local pi2_start=$PI5_METERS
  local pi2_end=$((N - 1))

  # ---- cleanup stale state on both hosts ----
  ssh "$PI5_HOST" "pkill -x secure_agg_server 2>/dev/null || true; pkill -x secure_agg_meter 2>/dev/null || true; fuser -k ${PORT}/tcp 2>/dev/null || true; tmux kill-session -t sa_agg 2>/dev/null || true"
  ssh "$PI2_HOST" "pkill -x secure_agg_meter 2>/dev/null || true"

  ssh "$PI5_HOST" "cd $REPO_REMOTE && mkdir -p ${rel} ${rel}/pi5"
  ssh "$PI2_HOST" "cd $REPO_REMOTE && mkdir -p ${rel}/pi2"

  # ---- start aggregator with in-server timeout + missing-id file ----
  local server_tmo_ms=$((TIMEOUT_SEC * 1000))
  ssh "$PI5_HOST" "cd $REPO_REMOTE && tmux new-session -d -s sa_agg \
    'ulimit -n 4096; timeout $((TIMEOUT_SEC + 30))s ./build/secure_agg_server \
       --n ${N} --port ${PORT} --expected ${EXPECTED} \
       --timeout-ms ${server_tmo_ms} --test-id ${test_id} \
       --missing-out ${rel}/missing_ids.txt \
       > ${rel}/aggregator.out 2>&1; echo \$? > ${rel}/server.exit'"

  if ! wait_server_ready; then
    echo "[suite] ERROR: aggregator never started listening on ${PORT}"
  fi

  # ---- launch meter groups under per-Pi cgroup caps ----
  ssh "$PI5_HOST" "cd $REPO_REMOTE && sudo systemd-run --scope --quiet \
    -p CPUQuota=${PI5_GROUP_CPU} -p MemoryMax=${PI5_GROUP_MEM} \
    ./scripts/secure_agg_launch_range.sh \
      ${pi5_start} ${pi5_end} ${N} ${PI5_IP} ${PORT} ${MASK_KEY} ${rel}/pi5 \
      ${METER_MEM_KB} ${METER_CPU_SECONDS} ${BATCH_SIZE} ${BATCH_GAP_MS} ${test_id}" &
  local pid5=$!

  ssh "$PI2_HOST" "cd $REPO_REMOTE && sudo systemd-run --scope --quiet \
    -p CPUQuota=${PI2_GROUP_CPU} -p MemoryMax=${PI2_GROUP_MEM} \
    ./scripts/secure_agg_launch_range.sh \
      ${pi2_start} ${pi2_end} ${N} ${PI5_IP} ${PORT} ${MASK_KEY} ${rel}/pi2 \
      ${METER_MEM_KB} ${METER_CPU_SECONDS} ${BATCH_SIZE} ${BATCH_GAP_MS} ${test_id}" &
  local pid2=$!

  local exit5=0 exit2=0
  wait "$pid5" || exit5=$?
  wait "$pid2" || exit2=$?

  # ---- wait for the aggregator to record its own exit ----
  local deadline=$((SECONDS + TIMEOUT_SEC + 60))
  while (( SECONDS < deadline )); do
    if ssh "$PI5_HOST" "test -f ${REPO_REMOTE}/${rel}/server.exit" 2>/dev/null; then break; fi
    sleep 2
  done

  # ---- collect logs ----
  local lt="$OUTDIR/trial${trial}"
  mkdir -p "$lt"
  rsync -av "$PI5_HOST:~/mpc-smartgrid/${rel}/" "$lt/" >/dev/null 2>&1 || true
  rsync -av "$PI2_HOST:~/mpc-smartgrid/${rel}/pi2/" "$lt/pi2/" >/dev/null 2>&1 || true

  local agg="$lt/aggregator.out"
  local observed received missing wall rss srv_status timed_out server_exit
  observed="$(extract "$agg" OBSERVED_AGGREGATE)"
  received="$(extract "$agg" RECEIVED_COUNT)"
  missing="$(extract "$agg" MISSING_COUNT)"
  wall="$(extract "$agg" WALL_MS)"
  rss="$(extract "$agg" MAX_RSS_KB)"
  srv_status="$(extract "$agg" STATUS)"
  timed_out="$(extract "$agg" TIMED_OUT)"
  server_exit="$(cat "$lt/server.exit" 2>/dev/null | tr -d '[:space:]' || true)"

  local failed_meters
  failed_meters="$(grep -rL 'STATUS=PASS' "$lt"/pi5 "$lt"/pi2 2>/dev/null | grep -c 'meter_' || true)"

  local missing_file=""
  [[ -f "$lt/missing_ids.txt" ]] && missing_file="trial${trial}/missing_ids.txt"

  local status="PASS" reason="ok"
  if [[ "$srv_status" != "PASS" ]]; then status="FAIL"; reason="server_status_${srv_status:-missing}"; fi
  if [[ "$observed" != "$EXPECTED" ]]; then status="FAIL"; reason="${reason};aggregate_mismatch"; fi
  if [[ "$received" != "$N" ]]; then status="FAIL"; reason="${reason};received_${received:-0}_of_${N}"; fi
  if [[ "${timed_out:-0}" == "1" ]]; then reason="${reason};server_timed_out"; fi
  if (( exit5 != 0 )); then reason="${reason};pi5_launcher_exit_${exit5}"; fi
  if (( exit2 != 0 )); then reason="${reason};pi2_launcher_exit_${exit2}"; fi

  echo "${RUN_ID},$(date -Is),secure_agg,${N},${PI5_METERS},${PI2_METERS},${EXPECTED},${observed},${received},${status},${reason},${wall},${rss},${PI5_GROUP_CPU},${PI5_GROUP_MEM},${PI2_GROUP_CPU},${PI2_GROUP_MEM},${METER_MEM_KB},${METER_FD_CAP},${METER_CPU_SECONDS},${missing:-},${missing_file},${failed_meters},${server_exit},${exit5},${exit2}" >> "$CSV"

  echo "[suite] trial ${trial}: status=${status} observed=${observed} received=${received}/${N} wall_ms=${wall} reason=${reason}"
  LAST_STATUS="$status"
}

# ---- main ----
EXPECTED="$(expected_sum)"
echo "[suite] expected aggregate = $EXPECTED"
echo "[suite] output dir = $OUTDIR"

sync_and_build
compute_placement
echo "[suite] placement: Pi5=${PI5_METERS} Pi2=${PI2_METERS}"

PASS=0
for ((t=1; t<=TRIALS; t++)); do
  run_trial "$t"
  [[ "$LAST_STATUS" == "PASS" ]] && PASS=$((PASS + 1))
  if (( STOP_ON_FAIL == 1 )) && [[ "$LAST_STATUS" == "FAIL" ]]; then
    echo "[suite] stopping after first failure (--stop-on-fail)"
    break
  fi
done

echo ""
echo "[suite] PASS ${PASS}/${TRIALS}"
echo "[suite] matrix: $CSV"
cat "$CSV"
