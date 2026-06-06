#!/usr/bin/env bash
set -euo pipefail

PI5_HOST="solomon@100.101.20.65"
PI2_HOST="pi@100.85.83.7"

PI5_IP="100.101.20.65"
PI2_IP="100.85.83.7"

REPO_LOCAL="$HOME/Desktop/Work/Hanze/work/mpc-smartgrid"
REPO_REMOTE="~/mpc-smartgrid"

N_VALUES=(20 24 32)
TRIALS=3
TIMEOUT_SEC=90
OUTDIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --n-values)   IFS=',' read -r -a N_VALUES <<< "$2"; shift 2 ;;
    --trials)     TRIALS="$2"; shift 2 ;;
    --timeout-sec) TIMEOUT_SEC="$2"; shift 2 ;;
    --out)        OUTDIR="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

RUN_ID="$(date +%Y%m%d_%H%M%S)"
[[ -z "$OUTDIR" ]] && OUTDIR="$REPO_LOCAL/benchmark_results/scale_${RUN_ID}"
CSV="$OUTDIR/scaling_matrix.csv"

mkdir -p "$OUTDIR/logs"

cat > "$CSV" <<EOF
case_id,N,trial,placement,pi5_nodes,pi2_nodes,expected_aggregate,completed_nodes,all_match,mean_protocol_ms,max_protocol_ms,status,failure_reason
EOF

generate_case_files() {
  local N="$1"
  local split=$(( (N + 1) / 2 ))

  mkdir -p "$REPO_LOCAL/configs/generated"
  mkdir -p "$REPO_LOCAL/inputs/generated/N${N}"

  local cfg="$REPO_LOCAL/configs/generated/cluster_N${N}.json"

  {
    echo "{"
    echo "  \"nodes\": ["
    for ((i=0; i<N; i++)); do
      local host
      if (( i < split )); then
        host="$PI5_IP"
      else
        host="$PI2_IP"
      fi

      local port=$((5000 + i))
      local comma=","
      if (( i == N - 1 )); then
        comma=""
      fi

      echo "    { \"id\": ${i}, \"host\": \"${host}\", \"port\": ${port} }${comma}"
    done
    echo "  ]"
    echo "}"
  } > "$cfg"

  for ((i=0; i<N; i++)); do
    local value=$((i + 1))
    cat > "$REPO_LOCAL/inputs/generated/N${N}/node${i}.csv" <<EOF
timestamp,meter_id,energy_wh
2026-06-06T00:00:00Z,${i},${value}
EOF
  done
}

sync_and_build() {
  echo "[scale] syncing source to Pi 5 and Pi 2"

  rsync -av --delete \
    --exclude build/ \
    --exclude logs/ \
    --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$PI5_HOST:~/mpc-smartgrid/" >/dev/null

  rsync -av --delete \
    --exclude build/ \
    --exclude logs/ \
    --exclude benchmark_results/ \
    "$REPO_LOCAL/" "$PI2_HOST:~/mpc-smartgrid/" >/dev/null

  echo "[scale] building on Pi 5"
  ssh "$PI5_HOST" "cd $REPO_REMOTE && make clean && make"

  echo "[scale] building on Pi 2"
  ssh "$PI2_HOST" "cd $REPO_REMOTE && make clean && make"

  echo "[scale] verifying binaries"
  ssh "$PI5_HOST" "cd $REPO_REMOTE && file build/meter_node"
  ssh "$PI2_HOST" "cd $REPO_REMOTE && file build/meter_node"
}

remote_cleanup() {
  local host="$1"
  local N="$2"

  ssh "$host" "cd $REPO_REMOTE || exit 0; \
    pkill -x meter_node 2>/dev/null || true; \
    for p in \$(seq 5000 $((5000 + N - 1))); do \
      fuser -k \${p}/tcp 2>/dev/null || true; \
    done; \
    for s in \$(tmux ls 2>/dev/null | awk -F: '/scale_/ {print \$1}'); do \
      tmux kill-session -t \"\$s\" 2>/dev/null || true; \
    done; \
    exit 0" || true
}

launch_node() {
  local host="$1"
  local case_id="$2"
  local N="$3"
  local node_id="$4"

  local session="scale_${case_id}_n${node_id}"
  local cfg="configs/generated/cluster_N${N}.json"
  local input="inputs/generated/N${N}/node${node_id}.csv"
  local logdir="logs/scale/${case_id}"
  local logfile="${logdir}/node${node_id}.out"

  ssh "$host" "cd $REPO_REMOTE && \
    mkdir -p ${logdir} && \
    tmux new-session -d -s ${session} \
    \"timeout ${TIMEOUT_SEC}s ./build/meter_node --id ${node_id} --config ${cfg} --input ${input} > ${logfile} 2>&1\""
}

wait_for_case() {
  local case_id="$1"
  local deadline=$((SECONDS + TIMEOUT_SEC + 15))

  while (( SECONDS < deadline )); do
    local r5
    local r2

    r5="$(ssh "$PI5_HOST" "tmux ls 2>/dev/null | grep -c scale_${case_id} || true")"
    r2="$(ssh "$PI2_HOST" "tmux ls 2>/dev/null | grep -c scale_${case_id} || true")"

    if [[ "$r5" == "0" && "$r2" == "0" ]]; then
      return 0
    fi

    sleep 2
  done

  return 1
}

collect_logs() {
  local case_id="$1"

  mkdir -p "$OUTDIR/logs/${case_id}/pi5"
  mkdir -p "$OUTDIR/logs/${case_id}/pi2"

  rsync -av "$PI5_HOST:~/mpc-smartgrid/logs/scale/${case_id}/" "$OUTDIR/logs/${case_id}/pi5/" >/dev/null 2>&1 || true
  rsync -av "$PI2_HOST:~/mpc-smartgrid/logs/scale/${case_id}/" "$OUTDIR/logs/${case_id}/pi2/" >/dev/null 2>&1 || true
}

extract_field() {
  local file="$1"
  local key="$2"

  grep "$key" "$file" | tail -n 1 | awk -F'=' '{gsub(/[ \t]/, "", $2); print $2}'
}

analyze_case() {
  local case_id="$1"
  local N="$2"
  local trial="$3"
  local split=$(( (N + 1) / 2 ))
  local expected=$(( N * (N + 1) / 2 ))

  local completed=0
  local all_match="true"
  local status="PASS"
  local reason=""
  local ms_values=()

  for ((i=0; i<N; i++)); do
    local base
    if (( i < split )); then
      base="$OUTDIR/logs/${case_id}/pi5"
    else
      base="$OUTDIR/logs/${case_id}/pi2"
    fi

    local log="$base/node${i}.out"

    if [[ ! -f "$log" ]]; then
      all_match="false"
      status="FAIL"
      reason="${reason}missing_node_${i}_log;"
      continue
    fi

    local obs
    obs="$(extract_field "$log" "GLOBAL AGGREGATE" || true)"

    if [[ -z "$obs" ]]; then
      all_match="false"
      status="FAIL"
      reason="${reason}missing_node_${i}_aggregate;"
    elif [[ "$obs" != "$expected" ]]; then
      all_match="false"
      status="FAIL"
      reason="${reason}node_${i}_aggregate_${obs}_expected_${expected};"
    else
      completed=$((completed + 1))
    fi

    local ms
    ms="$(extract_field "$log" "protocol_ms" || true)"
    if [[ -n "$ms" ]]; then
      ms_values+=("$ms")
    fi
  done

  local mean_ms=""
  local max_ms=""

  if (( ${#ms_values[@]} > 0 )); then
    mean_ms="$(printf "%s\n" "${ms_values[@]}" | awk '{s+=$1;n++} END {if(n) printf "%.3f", s/n}')"
    max_ms="$(printf "%s\n" "${ms_values[@]}" | awk 'BEGIN{m=0} {if($1>m)m=$1} END{printf "%.3f", m}')"
  fi

  if [[ -z "$reason" ]]; then
    reason="ok"
  fi

  echo "${case_id},${N},${trial},balanced,${split},$((N - split)),${expected},${completed},${all_match},${mean_ms},${max_ms},${status},${reason}" >> "$CSV"

  echo "[scale] ${case_id}: status=${status}, completed=${completed}/${N}, expected=${expected}, mean_ms=${mean_ms}, max_ms=${max_ms}, reason=${reason}"
}

run_case() {
  local N="$1"
  local trial="$2"
  local case_id="N${N}_trial${trial}"

  local split=$(( (N + 1) / 2 ))
  local expected=$(( N * (N + 1) / 2 ))

  echo ""
  echo "============================================================"
  echo "Running ${case_id}: N=${N}, Pi5 nodes=${split}, Pi2 nodes=$((N - split)), expected=${expected}"
  echo "============================================================"

  remote_cleanup "$PI5_HOST" "$N"
  remote_cleanup "$PI2_HOST" "$N"

  ssh "$PI5_HOST" "mkdir -p ~/mpc-smartgrid/logs/scale/${case_id}"
  ssh "$PI2_HOST" "mkdir -p ~/mpc-smartgrid/logs/scale/${case_id}"

  for ((i=0; i<N; i++)); do
    if (( i < split )); then
      launch_node "$PI5_HOST" "$case_id" "$N" "$i"
    else
      launch_node "$PI2_HOST" "$case_id" "$N" "$i"
    fi
  done

  if ! wait_for_case "$case_id"; then
    echo "[scale] warning: ${case_id} exceeded timeout window"
    remote_cleanup "$PI5_HOST" "$N"
    remote_cleanup "$PI2_HOST" "$N"
  fi

  collect_logs "$case_id"
  analyze_case "$case_id" "$N" "$trial"
}

main() {
  cd "$REPO_LOCAL"

  echo "[scale] output directory: $OUTDIR"

  for N in "${N_VALUES[@]}"; do
    generate_case_files "$N"
  done

  sync_and_build

  for N in "${N_VALUES[@]}"; do
    for ((trial=1; trial<=TRIALS; trial++)); do
      run_case "$N" "$trial"

      latest_status="$(tail -n 1 "$CSV" | awk -F',' '{print $12}')"
      if [[ "$latest_status" == "FAIL" ]]; then
        echo "[scale] stopping after first failure at N=${N}"
        echo "[scale] final matrix: $CSV"
        cat "$CSV"
        exit 0
      fi
    done
  done

  echo ""
  echo "[scale] complete"
  echo "[scale] final matrix: $CSV"
  cat "$CSV"
}

main "$@"
