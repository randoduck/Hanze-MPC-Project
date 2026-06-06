#!/usr/bin/env bash
set -euo pipefail

# Thin wrapper preserving the original entry point. The 1000-meter run is now
# just the general suite with the validated configuration. Extra args pass
# through, e.g.:  ./scripts/run_final_1000_secure_agg_pc5.sh --trials 10
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/run_secure_agg_suite_pc5.sh" \
  --n 1000 \
  --trials 1 \
  --pi2-min 100 --pi2-max 300 \
  --meter-mem-kb 16384 \
  --pi5-group-cpu 300% --pi5-group-mem 1024M \
  --pi2-group-cpu 100% --pi2-group-mem 256M \
  --port 7000 --timeout-sec 420 \
  "$@"
