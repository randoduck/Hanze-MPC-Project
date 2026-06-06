#!/bin/bash
set -euo pipefail

# --- CLUSTER CONFIGURATION ---
# Define the connection strings as USER@IP
NODE_A="solomon@100.101.20.65"     # Your Pi 5
NODE_B="pi@100.85.83.7" # Your friend's Pi 2 (Change "username_of_pi2"!)

# Target directory path on the remote Raspberry Pis
REMOTE="~/mpc-smartgrid"

for node in "$NODE_A" "$NODE_B"; do
  if [ -z "$node" ] || [[ "$node" == *"username_of_pi2"* ]]; then
    echo "⚠️ Skipping unconfigured or placeholder node..."
    continue
  fi
  
  echo "--------------------------------------------------"
  echo "🚀 Beaming source files to Node: $node..."
  echo "--------------------------------------------------"
  
  ssh "$node" "mkdir -p $REMOTE"
  
  # Sync the code while stripping out unnecessary local binary noise
  rsync -av --delete \
        --exclude='build/' \
        --exclude='logs/' \
        --exclude='.git/' \
        ./ "$node":"$REMOTE"/
  
  echo "🛠️ Triggering native compiler on Node: $node..."
  # Instruct the remote node over the secure tunnel to clean and compile natively
  ssh "$node" "cd $REMOTE && make clean && make"
done

echo "=================================================="
echo "🎉 Deploy complete! All nodes synchronized and built."
echo "=================================================="
