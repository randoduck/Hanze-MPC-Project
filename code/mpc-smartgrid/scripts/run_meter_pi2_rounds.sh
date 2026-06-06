#!/usr/bin/env bash
set -euo pipefail

cd ~/mpc-smartgrid
mkdir -p logs/repeated

for r in 0 1 2; do
  echo "[pi2] starting round $r"
  ./build/meter_node \
    --id 1 \
    --config configs/cluster_test2.json \
    --input data/rounds/pi2_round${r}.csv \
    > logs/repeated/pi2_round${r}.out 2>&1
  sleep 1
done
