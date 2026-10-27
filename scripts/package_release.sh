#!/usr/bin/env bash
# Package a completed native build. Build tools and generated sources stay out.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD="${1:-build}"
PLATFORM="${2:?usage: package_release.sh BUILD PLATFORM}"
VERSION="$(tr -d '[:space:]' < VERSION)"
[[ "${RELEASE_VERSION:-$VERSION}" == "$VERSION" ]]
grep -q '^SNESRECOMP_SETUP_HOST:BOOL=OFF$' "$BUILD/CMakeCache.txt"
grep -q "^SNESRECOMP_BUILD_VERSION:STRING=$VERSION$" "$BUILD/CMakeCache.txt"
NAME="supermetroid-$VERSION-$PLATFORM"
STAGE="$ROOT/dist/$NAME"
[[ ! -e "$STAGE" ]] || { echo "Stage already exists: $STAGE" >&2; exit 1; }
EXE=SuperMetroidSNESRecomp
[[ "$PLATFORM" != windows-* ]] || EXE="$EXE.exe"
[[ -f "$BUILD/$EXE" && -d "$BUILD/assets" ]]
mkdir -p "$STAGE"
cp "$BUILD/$EXE" "$STAGE/"
cp -R "$BUILD/assets" "$STAGE/"
cp VERSION framework_pins.txt README.md LICENSE "$STAGE/"
cp tools/replay_diagnostic.py "$STAGE/replay_diagnostic.py"
cp profiles/release.json "$STAGE/release-profile.json"
# Repository defaults only. Never copy a developer's keybinds, ROM paths, saves,
# writable mods, video settings or mute configuration from a build directory.
cp config.ini "$STAGE/"
mkdir -p "$STAGE/docs"
cp docs/custom-renderer.md "$STAGE/docs/"
cp -R docs/images "$STAGE/docs/"
FRAMEWORK="$(sed -n 's/^SNESRECOMP_ROOT:PATH=//p' "$BUILD/CMakeCache.txt" | tr -d '\r')"
FRAMEWORK="${FRAMEWORK:-$ROOT/snesrecomp}"
[[ -f "$FRAMEWORK/tools/ci/bundle_runtime_libs.sh" ]]
bash "$FRAMEWORK/tools/ci/bundle_runtime_libs.sh" \
  --exe "$STAGE/$EXE" --stage "$STAGE" --build-dir "$BUILD" --strict
if [[ "$PLATFORM" == linux-* ]]; then
  cat > "$STAGE/Launch.sh" <<'LAUNCH'
#!/bin/sh
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export LD_LIBRARY_PATH="$HERE${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$HERE" || exit 1
exec "$HERE/SuperMetroidSNESRecomp" "$@"
LAUNCH
  chmod +x "$STAGE/Launch.sh"
elif [[ "$PLATFORM" == macos-* ]]; then
  # Re-sign after rewriting dylib paths; preserve the native executable bit.
  find "$STAGE" -name '*.dylib' -exec codesign --force --sign - {} \;
  codesign --force --sign - "$STAGE/$EXE"
fi
python3 - "$STAGE" <<'PY'
from pathlib import Path
import sys, zipfile
stage = Path(sys.argv[1])
with zipfile.ZipFile(str(stage) + '.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            archive.write(path, path.relative_to(stage.parent).as_posix())
PY
python3 tools/release_payload.py audit "$STAGE.zip"
