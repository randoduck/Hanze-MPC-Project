#!/usr/bin/env bash
set -euo pipefail

cd ~/mpc-smartgrid
mkdir -p logs/repeated

for r in 0 1 2; do
  echo "[pi5] starting round $r"
  ./build/meter_node \
    --id 0 \
    --config configs/cluster_test2.json \
    --input data/rounds/pi5_round${r}.csv \
    > logs/repeated/pi5_round${r}.out 2>&1
  sleep 1
done
