#!/usr/bin/env bash
set -euo pipefail

# Launch a contiguous range of secure-aggregation meter clients on this host.
#
# Positional args (1-9 are required and backward compatible; 10-12 optional):
#   1  START_ID
#   2  END_ID
#   3  N            total meters in the run
#   4  AGG_IP       aggregator IP
#   5  PORT         aggregator port
#   6  MASK_KEY     shared mask key
#   7  OUTDIR       per-meter log dir
#   8  MEM_KB       per-meter address-space cap (ulimit -v)
#   9  CPU_SECONDS  per-meter CPU-time cap (ulimit -t)
#   10 BATCH_SIZE   meters launched before sleeping  (default 50)
#   11 BATCH_GAP_MS pause between batches in ms       (default 200)
#   12 TEST_ID      per-run id stamped into packets   (default 0)

START_ID="${1:?missing start_id}"
END_ID="${2:?missing end_id}"
N="${3:?missing N}"
AGG_IP="${4:?missing aggregator_ip}"
PORT="${5:?missing port}"
MASK_KEY="${6:?missing mask_key}"
OUTDIR="${7:?missing outdir}"
MEM_KB="${8:?missing mem_kb}"
CPU_SECONDS="${9:?missing cpu_seconds}"
BATCH_SIZE="${10:-50}"
BATCH_GAP_MS="${11:-200}"
TEST_ID="${12:-0}"

SCRIPT_PATH="$(readlink -f "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
cd "$REPO_DIR"
mkdir -p "$OUTDIR"

value_for_node() {
  local i="$1"
  echo $((100 + ((37*i + 11*i*i + 73) % 900)))
}

if (( START_ID > END_ID )); then
  exit 0
fi

# Launch in batches with a gap between them. Spreading the launch avoids a
# 1000-process thundering herd that bursts TCP connections all at once; the
# meter binary adds per-client jitter on top of this.
batch_gap_s="$(awk "BEGIN { printf \"%.3f\", ${BATCH_GAP_MS}/1000 }")"
launched=0

for i in $(seq "$START_ID" "$END_ID"); do
  v="$(value_for_node "$i")"

  (
    ulimit -v "$MEM_KB"
    ulimit -n 64
    ulimit -t "$CPU_SECONDS"

    nice -n 10 timeout 300s ./build/secure_agg_meter \
      --id "$i" \
      --n "$N" \
      --value "$v" \
      --aggregator "$AGG_IP" \
      --port "$PORT" \
      --mask-key "$MASK_KEY" \
      --test-id "$TEST_ID" \
      > "${OUTDIR}/meter_${i}.out" 2>&1
  ) &

  launched=$((launched + 1))
  if (( BATCH_SIZE > 0 && launched % BATCH_SIZE == 0 )); then
    sleep "$batch_gap_s"
  fi
done

wait
