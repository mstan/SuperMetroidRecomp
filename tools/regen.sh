#!/usr/bin/env bash
# Deterministic LLE-first regeneration for Super Metroid.
#
# Flags:
#   --no-tests             skip the framework test suite (default: run it).
#   --strict-idempotent    regenerate into a temporary directory and require
#                          byte-identical output.
#   -h | --help            this message.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ROM="Super Metroid (Japan, USA) (En,Ja).sfc"
PROFILE="profiles/attract_tier2.json"
TESTS="snesrecomp/tests/v2/run_tests.py"

RUN_TESTS=1
STRICT_IDEMPOTENT=0
for arg in "$@"; do
  case "$arg" in
    --no-tests) RUN_TESTS=0 ;;
    --strict-idempotent) STRICT_IDEMPOTENT=1 ;;
    -h|--help) sed -n '2,/^set -euo/p' "$0" | sed -n '/^# /p' | sed 's/^# //'; exit 0 ;;
    *) echo "regen.sh: unknown argument: $arg (try --help)" >&2; exit 2 ;;
  esac
done

cd "$ROOT"
PYTHON="${PYTHON:-$(command -v python3 || command -v python || true)}"
if [ -z "$PYTHON" ]; then
  echo "regen.sh: no python3/python interpreter found on PATH" >&2
  exit 1
fi
if [ ! -f "$ROM" ]; then
  echo "regen.sh: verified ROM not found: $ROM" >&2
  exit 1
fi
if [ ! -f "$PROFILE" ]; then
  echo "regen.sh: attract profile not found: $PROFILE" >&2
  exit 1
fi

step() { echo; echo "=== $* ==="; }

step "Regenerating banks"
# The runtime profile selects optional AOT work only. Missing or rejected exact
# variants continue to execute the real ROM through authoritative LLE.
"$PYTHON" snesrecomp/tools/v2_emit.py --rom "$ROM" \
    --cfg-dir recomp --out-dir src/gen --profile-manifest "$PROFILE"

step "Syncing funcs.h"
"$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp \
    --out recomp/funcs.h

if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
  step "Checking idempotency"
  TMP_GEN="$(mktemp -d)"
  "$PYTHON" snesrecomp/tools/v2_emit.py --rom "$ROM" \
      --cfg-dir recomp --out-dir "$TMP_GEN" --profile-manifest "$PROFILE"
  "$PYTHON" snesrecomp/tools/v2_compare_output.py \
      --expected src/gen --actual "$TMP_GEN"
  rm -rf "$TMP_GEN"
fi

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  "$PYTHON" "$TESTS"
fi

step "Done"
