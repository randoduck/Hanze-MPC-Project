#!/usr/bin/env bash
set -euo pipefail

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"

REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"

TRIALS=1
OUTDIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --trials) TRIALS="$2"; shift 2 ;;
    --out)    OUTDIR="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN_ID="$(date +%Y%m%d_%H%M%S)"
[[ -z "$OUTDIR" ]] && OUTDIR="$REPO_LOCAL/benchmark_results/run_${RUN_ID}"

mkdir -p "$OUTDIR/pi5" "$OUTDIR/pi2"

CSV="$OUTDIR/final_matrix.csv"

cat > "$CSV" <<EOF
test_id,trial,pi5_value,pi2_value,expected_aggregate,mode,observed_pi5,observed_pi2,pi5_protocol_ms,pi2_protocol_ms,pi5_max_rss_kb,pi2_max_rss_kb,status,notes
EOF

extract_field() {
  local file="$1"
  local key="$2"

  grep "$key" "$file" | tail -n 1 | awk -F'=' '{gsub(/[ \t]/, "", $2); print $2}'
}

run_case() {
  local test_id="$1"
  local pi5_value="$2"
  local pi2_value="$3"
  local mode="$4"
  local trial="${5:-1}"

  local expected=$((pi5_value + pi2_value))

  echo ""
  echo "============================================================"
  echo "Running $test_id: Pi5=$pi5_value Pi2=$pi2_value mode=$mode expected=$expected"
  echo "============================================================"

  ssh "$PI5_HOST" "cd $REPO_REMOTE && mkdir -p logs/auto inputs/auto && rm -f logs/auto/${test_id}_*.out"
  ssh "$PI2_HOST" "cd $REPO_REMOTE && mkdir -p logs/auto inputs/auto && rm -f logs/auto/${test_id}_*.out"

  ssh "$PI5_HOST" "cd $REPO_REMOTE && fuser -k 5000/tcp 5001/tcp 2>/dev/null || true"
  ssh "$PI2_HOST" "cd $REPO_REMOTE && fuser -k 5000/tcp 5001/tcp 2>/dev/null || true"

  ssh "$PI5_HOST" "cd $REPO_REMOTE && ./scripts/bench_worker.sh pi5 '$test_id' '$pi5_value' '$mode'" &
  local pid5=$!

  sleep 1

  ssh "$PI2_HOST" "cd $REPO_REMOTE && ./scripts/bench_worker.sh pi2 '$test_id' '$pi2_value' baseline" &
  local pid2=$!

  local status="PASS"
  local notes=""

  if ! wait "$pid5"; then
    status="FAIL"
    notes="${notes}Pi5 command failed; "
  fi

  if ! wait "$pid2"; then
    status="FAIL"
    notes="${notes}Pi2 command failed; "
  fi

  rsync -av "$PI5_HOST:~/mpc-smartgrid/logs/auto/${test_id}_pi5.out" "$OUTDIR/pi5/" >/dev/null || true
  rsync -av "$PI2_HOST:~/mpc-smartgrid/logs/auto/${test_id}_pi2.out" "$OUTDIR/pi2/" >/dev/null || true

  local pi5_log="$OUTDIR/pi5/${test_id}_pi5.out"
  local pi2_log="$OUTDIR/pi2/${test_id}_pi2.out"

  local obs5=""
  local obs2=""
  local ms5=""
  local ms2=""
  local rss5=""
  local rss2=""

  if [[ -f "$pi5_log" ]]; then
    obs5="$(extract_field "$pi5_log" "GLOBAL AGGREGATE" || true)"
    ms5="$(extract_field "$pi5_log" "protocol_ms" || true)"
    rss5="$(extract_field "$pi5_log" "max_rss_kb" || true)"
  else
    status="FAIL"
    notes="${notes}missing Pi5 log; "
  fi

  if [[ -f "$pi2_log" ]]; then
    obs2="$(extract_field "$pi2_log" "GLOBAL AGGREGATE" || true)"
    ms2="$(extract_field "$pi2_log" "protocol_ms" || true)"
    rss2="$(extract_field "$pi2_log" "max_rss_kb" || true)"
  else
    status="FAIL"
    notes="${notes}missing Pi2 log; "
  fi

  if [[ "$obs5" != "$expected" || "$obs2" != "$expected" ]]; then
    status="FAIL"
    notes="${notes}aggregate mismatch; "
  fi

  if [[ -z "$notes" ]]; then
    notes="ok"
  fi

  echo "${test_id},${trial},${pi5_value},${pi2_value},${expected},${mode},${obs5},${obs2},${ms5},${ms2},${rss5},${rss2},${status},${notes}" >> "$CSV"

  echo "Result: $status | trial=$trial | Pi5=$obs5 Pi2=$obs2 | Pi5_ms=$ms5 Pi2_ms=$ms2"
}

echo "[suite] output directory: $OUTDIR"

echo "[suite] syncing source to Pis, excluding build/"
rsync -av --delete --exclude build/ "$REPO_LOCAL/" "$PI5_HOST:~/mpc-smartgrid/" >/dev/null
rsync -av --delete --exclude build/ "$REPO_LOCAL/" "$PI2_HOST:~/mpc-smartgrid/" >/dev/null

echo "[suite] rebuilding on Pi 5"
ssh "$PI5_HOST" "cd $REPO_REMOTE && make clean && make"

echo "[suite] rebuilding on Pi 2"
ssh "$PI2_HOST" "cd $REPO_REMOTE && make clean && make"

echo "[suite] verifying binaries"
ssh "$PI5_HOST" "cd $REPO_REMOTE && file build/meter_node"
ssh "$PI2_HOST" "cd $REPO_REMOTE && file build/meter_node"

for ((trial=1; trial<=TRIALS; trial++)); do
  echo ""
  echo "################## TRIAL ${trial}/${TRIALS} ##################"

  # Baseline values
  run_case "T01_baseline_812" 312 500 baseline "$trial"
  run_case "T02_baseline_838" 318 520 baseline "$trial"
  run_case "T03_baseline_815" 305 510 baseline "$trial"

  # CPU constraints, high to low
  run_case "T04_cpu75_812" 312 500 cpu75 "$trial"
  run_case "T05_cpu50_812" 312 500 cpu50 "$trial"
  run_case "T06_cpu25_812" 312 500 cpu25 "$trial"

  # Memory constraints, high to low
  run_case "T07_mem64_812" 312 500 mem64 "$trial"
  run_case "T08_mem32_812" 312 500 mem32 "$trial"
  run_case "T09_mem16_812" 312 500 mem16 "$trial"

  # Combined constraints
  run_case "T10_cpu50_mem32_812" 312 500 cpu50_mem32 "$trial"
  run_case "T11_cpu25_mem32_812" 312 500 cpu25_mem32 "$trial"
  run_case "T12_cpu25_mem16_812" 312 500 cpu25_mem16 "$trial"
done

echo ""
echo "[suite] complete"
echo "[suite] final matrix: $CSV"
echo ""
cat "$CSV"
