#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ENGINE=${SNESRECOMP_ROOT:-$ROOT/snesrecomp}
exec sh "$ENGINE/tools/regen_project.sh" --project-root "$ROOT" "$@"
