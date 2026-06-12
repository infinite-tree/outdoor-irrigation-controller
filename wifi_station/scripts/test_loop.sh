#!/usr/bin/env bash
# Build, upload, and run schedule tests until they pass.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST="${STATION_HOST:-192.168.2.216}"
export PATH="${HOME}/.local/bin:${HOME}/.platformio/penv/bin:${PATH}"

cd "$ROOT"
chmod +x scripts/test_schedules.sh

attempt=1
max_attempts=12

while (( attempt <= max_attempts )); do
  echo "=== attempt ${attempt}/${max_attempts} ==="
  pio run -t upload
  echo "waiting for boot..."
  sleep 12
  if scripts/test_schedules.sh "$HOST"; then
    echo "=== SUCCESS on attempt ${attempt} ==="
    exit 0
  fi
  echo "tests failed; retrying in 10s..."
  sleep 10
  attempt=$((attempt + 1))
done

echo "=== FAILED after ${max_attempts} attempts ==="
exit 1
