#!/usr/bin/env bash
# Single documented test command: plain build, then ASan+UBSan build; runs C DSA + C++ backend suites.
# Database tests need a role with CREATEDB (development credentials shown; override as needed).
set -euo pipefail
cd "$(dirname "$0")/.."
export CARBON_TEST_ADMIN_URL="${CARBON_TEST_ADMIN_URL:-postgresql://carbon_dev:carbon_dev_pw@localhost:5432/postgres}"
if ! pg_isready -q -d "$CARBON_TEST_ADMIN_URL" 2>/dev/null; then
  echo "WARNING: PostgreSQL not reachable at CARBON_TEST_ADMIN_URL; database tests will be SKIPPED" >&2
fi
for mode in plain asan; do
  dir=build; flags=()
  if [ "$mode" = asan ]; then dir=build-asan; flags=(-DCARBON_ENABLE_SANITIZERS=ON); fi
  echo "=== $mode ==="
  cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE=Debug "${flags[@]}" >/dev/null
  cmake --build "$dir" -j"$(nproc)"
  ctest --test-dir "$dir" --output-on-failure -j4
done
