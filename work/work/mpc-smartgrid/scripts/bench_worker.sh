#!/usr/bin/env bash
set -euo pipefail

ROLE="${1:?usage: bench_worker.sh <pi5|pi2> <test_id> <value> <mode>}"
TEST_ID="${2:?missing test_id}"
VALUE="${3:?missing value}"
MODE="${4:?missing mode}"

REPO="$HOME/mpc-smartgrid"
cd "$REPO"

mkdir -p logs/auto inputs/auto

if [[ "$ROLE" == "pi5" ]]; then
  NODE_ID=0
elif [[ "$ROLE" == "pi2" ]]; then
  NODE_ID=1
else
  echo "bad role: $ROLE" >&2
  exit 2
fi

INPUT="inputs/auto/${TEST_ID}_${ROLE}.csv"
OUT="logs/auto/${TEST_ID}_${ROLE}.out"

cat > "$INPUT" <<CSV
timestamp,meter_id,energy_wh
2026-05-31T18:30:00Z,${NODE_ID},${VALUE}
CSV

BASE_CMD="./build/meter_node --id ${NODE_ID} --config configs/cluster_test2.json --input ${INPUT}"

run_normal() {
  bash -lc "$BASE_CMD"
}

run_systemd() {
  local props=("$@")
  sudo systemd-run --scope --quiet "${props[@]}" bash -lc "$BASE_CMD"
}

{
  echo "TEST_ID=${TEST_ID}"
  echo "ROLE=${ROLE}"
  echo "NODE_ID=${NODE_ID}"
  echo "VALUE=${VALUE}"
  echo "MODE=${MODE}"
  echo "START_TS=$(date -Is)"
  echo "HOSTNAME=$(hostname)"
  echo "TAILSCALE_IP=$(tailscale ip -4 2>/dev/null || true)"
  echo "----------------------------------------"

  if [[ "$ROLE" == "pi2" ]]; then
    run_normal
  else
    case "$MODE" in
      baseline)
        run_normal
        ;;
      cpu75)
        run_systemd -p CPUQuota=75%
        ;;
      cpu50)
        run_systemd -p CPUQuota=50%
        ;;
      cpu25)
        run_systemd -p CPUQuota=25%
        ;;
      mem64)
        run_systemd -p MemoryMax=64M
        ;;
      mem32)
        run_systemd -p MemoryMax=32M
        ;;
      mem16)
        run_systemd -p MemoryMax=16M
        ;;
      cpu50_mem32)
        run_systemd -p CPUQuota=50% -p MemoryMax=32M
        ;;
      cpu25_mem32)
        run_systemd -p CPUQuota=25% -p MemoryMax=32M
        ;;
      cpu25_mem16)
        run_systemd -p CPUQuota=25% -p MemoryMax=16M
        ;;
      *)
        echo "unknown mode: $MODE" >&2
        exit 3
        ;;
    esac
  fi

  echo "----------------------------------------"
  echo "END_TS=$(date -Is)"
} > "$OUT" 2>&1
