# Title metadata and payload requirements for the engine release driver.
APP_NAME="SuperMetroid"
RELEASE_SLUG="SuperMetroidSNESRecomp"        # matches the windows zip prefix
CMAKE_TARGET="SuperMetroidSNESRecomp"
ROM_EXTS="sfc smc"
EXTRA_ARGS=""
REGEN_CMD="bash tools/regen.sh"
PREBUILD_CMD=""
POSTBUILD_CMD=""
BOXART="recomp/launcher/boxart.tga"          # AppImage icon source (optional)
EXTRA_PAYLOAD=()                             # repo-relative files -> usr/bin/
# Widescreen and presentation FPS use the compiled-in Mods provider; gameplay
# and QoL features are packages for the engine's mod runtime.
REQUIRED_MOD_MANIFESTS=(
  "preloaded/packages/super-metroid.pacing/1.0.0/manifest.toml"
  "preloaded/packages/super-metroid.combat/1.0.0/manifest.toml"
  "preloaded/packages/super-metroid.balance-fixes/1.0.0/manifest.toml"
  "preloaded/packages/super-metroid.controls/1.0.0/manifest.toml"
  "preloaded/packages/super-metroid.map-hud/1.0.0/manifest.toml"
  "preloaded/packages/super-metroid.movement/1.0.0/manifest.toml"
)
PROD_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=OFF )
DEBUG_CMAKE_FLAGS=( -DSNESRECOMP_ENABLE_TRACE=ON )

MOD_CATALOG_DIRS=(.)
