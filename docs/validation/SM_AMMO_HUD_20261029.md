# Remove duplicate gameplay ammo counters

The native HUD already shows current missiles, supers and power bombs. The
playtest incorrectly enabled a second current/maximum row. Following the owner's
rejection, gameplay rendering and the `Always` setting are removed. The optional
pause readout now adds only new information: `MAX M 99 S 30 P 20`. Existing native
counters remain the source of current amounts, including on pause screens.

Future playtest selections use `ammo-totals` without a `where` option. Old isolated
profiles retain their old executable/catalog for evidence; they are not modified
under a running game. The replacement is `sm-hud-build/SuperMetroidSNESRecomp.exe`;
its identity is in `G:/Temp/Codex/sm-qol-20261027/hud-build-identity.json`.

Targeted checks:

- The existing native map test now covers enabled ammo capacity leaving every
  gameplay pixel untouched, read-only WRAM, paused display, maximum-capacity
  changes, fade exclusions and framebuffer guards. It passes Windows and
  Ubuntu/GCC builds.
- `tools/test_qol_ammo_pause.py` runs real gameplay and pause with the feature
  off/on. Gameplay BMPs and both complete WRAM checkpoints are identical. The
  pause BMP adds the maximum capacities; SRAM remains unchanged. Final raw
  evidence: `hud-pause-runtime-max/`, with `hud-*-max.log`.
- The current owner executable remains byte-identical. Output was redirected
  to a separate candidate directory while reusing build objects. Fresh limited
  owner states in `owner-hud-ready-max/` restore and resume successfully; prior
  notes and accepted checks are retained.

Only the affected presentation code and checks were rebuilt. No unrelated
gameplay matrix, native-save changes, code push, main merge or release.
