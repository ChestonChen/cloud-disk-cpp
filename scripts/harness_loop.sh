#!/usr/bin/env bash
# Build + smoke harness with retry loop.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAX_ATTEMPTS="${MAX_ATTEMPTS:-5}"
attempt=1

while (( attempt <= MAX_ATTEMPTS )); do
  echo "==> harness attempt $attempt/$MAX_ATTEMPTS"
  if cmake -S "$ROOT_DIR/backend" -B "$ROOT_DIR/build" \
    && cmake --build "$ROOT_DIR/build" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" \
    && "$ROOT_DIR/scripts/prod_smoke_test.sh"; then
    echo "HARNESS OK"
    exit 0
  fi
  echo "attempt $attempt failed; retrying..." >&2
  attempt=$((attempt + 1))
  sleep 1
done

echo "HARNESS FAILED after $MAX_ATTEMPTS attempts" >&2
exit 1
