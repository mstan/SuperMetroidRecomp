/* super-metroid.pacing / intro-skip, ceres-skip, credits-skip.
 *
 * intro-skip (behaviour of Super Metroid Redux IntroSkip.asm by samsamcmoi):
 *   GameState_37_CeresGoesBoomWithSamus_ ($8B:A35B) also runs the new-game
 *   intro (game state $1E). While the story is on screen (cinematic_function
 *   $1F51 is one of the states Redux allows: CinematicFunc_Nothing,
 *   nullsub_121 and the five "wait for input" pages) a new Start press jumps
 *   the cinematic to CinematicFunction_Intro_Func34 ($8B:B72F), the stock
 *   fade-out that ends the story and starts the flight to Ceres. The fade is
 *   primed like CinematicBgInstr_Func17, the stock path into Func34. As in
 *   Redux it is not offered with Japanese text; Redux's on-screen prompt is
 *   not reproduced. The hook runs before the cinematic call, so it reads
 *   the pad state ReadJoypadInputs left this frame, not the demo input the
 *   intro's gameplay segments substitute later in the frame.
 *
 * ceres-skip (Redux "Skip Ceres - Start In Zebes.ips", 11 bytes at $82:EEB7):
 *   GameOptionsMenu_4_StartGame for a file that has not reached Zebes
 *   (loading_game_state $7E:D914 is 0 = new game or $1F = starting at Ceres)
 *   clears area_index and load_station_index and continues at $82:EEF6, the
 *   stock debug path that saves the file as loading state 5 and opens the
 *   file-select map, so the game starts at the Landing Site ship.
 *
 * credits-skip:
 *   During the staff roll of GameState_39_EndingAndCredits_ ($8B:D443) the
 *   credits object ($19F7 list pointer into bank $8C) is moved to its last
 *   instruction at $8C:DC97, CinematicFunction_Intro_Func219, which ends the
 *   roll exactly as the stock list does; the ending pose and the final
 *   screen follow unchanged. The next list step is made due immediately.
 */

#include <stdio.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

#define RAM16(cpu, a) ((uint16_t)((cpu)->ram[(a)] | (cpu)->ram[(a) + 1] << 8))
static void Put16(CpuState *cpu, uint32_t a, uint16_t v) {
  cpu->ram[a] = (uint8_t)v;
  cpu->ram[a + 1] = (uint8_t)(v >> 8);
}

enum {
  kJoypad1New = 0x8F, kButtonStart = 0x1000,
  kScreenFadeDelay = 0x723, kScreenFadeCounter = 0x725,
  kGameState = 0x998, kAreaIndex = 0x79F, kLoadStationIndex = 0x78B,
  kJapaneseText = 0x9E2, kCinematicFunction = 0x1F51,
  kLoadingGameState = 0xD914,
  kCinematicVar10 = 0x1997, kCreditsListPtr = 0x19F7, kTextGlow = 0x19FF,
  kCreditsLastStep = 0x1A03,
};

/* ---- intro-skip ---- */
static const uint16_t kIntroSkippable[] = {
  0xA391, /* CinematicFunc_Nothing */
  0xA390, /* nullsub_121 */
  0xAEB8, /* CinematicFunction_Intro_WaitInputSetupMotherBrainFight */
  0xAF6C, /* CinematicFunction_Intro_WaitInputSetupBabyMetroid */
  0xB0F2, /* CinematicFunction_Intro_Func11 */
  0xB123, /* CinematicFunction_Intro_Func12 */
  0xB1DA, /* CinematicFunction_Intro_Func15 */
};

uint32 SmIntroSkipHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (RAM16(cpu, kGameState) != 0x1E || RAM16(cpu, kJapaneseText) ||
      !(RAM16(cpu, kJoypad1New) & kButtonStart))
    return 0;
  uint16_t fn = RAM16(cpu, kCinematicFunction);
  for (size_t i = 0; i < sizeof(kIntroSkippable) / sizeof(kIntroSkippable[0]); ++i) {
    if (fn == kIntroSkippable[i]) {
      Put16(cpu, kCinematicFunction, 0xB72F); /* CinematicFunction_Intro_Func34 */
      Put16(cpu, kScreenFadeDelay, 1);
      Put16(cpu, kScreenFadeCounter, 1);
      break;
    }
  }
  return 0;
}

static void SmIntroSkipActivate(void) {
  static const uint32_t kHooks[] = {0x8BA35B};
  size_t n = SmModArmHooks("intro-skip", kHooks, 1);
  fprintf(stderr, "[mods] intro skip: %zu/1 hooks\n", n);
}

/* ---- ceres-skip ---- */
uint32 SmCeresSkipHook(CpuState *cpu, uint32 pc24) {
  uint16_t loading = RAM16(cpu, kLoadingGameState);
  if (loading != 0x00 && loading != 0x1F) return 0;
  Put16(cpu, kAreaIndex, 0);
  Put16(cpu, kLoadStationIndex, 0);
  return (pc24 & 0xFF0000u) | 0xEEF6u;
}

static void SmCeresSkipActivate(void) {
  static const uint32_t kHooks[] = {0x82EEB7};
  size_t n = SmModArmHooks("ceres-skip", kHooks, 1);
  fprintf(stderr, "[mods] ceres skip: %zu/1 hooks\n", n);
}

/* ---- credits-skip ---- */
enum { kCreditsFirst = 0xD91B, kCreditsEnd = 0xDC97 };

uint32 SmCreditsSkipHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  uint16_t list = RAM16(cpu, kCreditsListPtr);
  if (RAM16(cpu, kGameState) != 0x27 || !(RAM16(cpu, kTextGlow) & 0x8000) ||
      list < kCreditsFirst || list >= kCreditsEnd ||
      !(RAM16(cpu, kJoypad1New) & kButtonStart))
    return 0;
  Put16(cpu, kCreditsListPtr, kCreditsEnd);
  /* CreditsObject_ProcessOne steps the list once cinematic_var10 has moved
   * 8 px past the last step. */
  Put16(cpu, kCreditsLastStep, (uint16_t)(RAM16(cpu, kCinematicVar10) - 8));
  return 0;
}

static void SmCreditsSkipActivate(void) {
  static const uint32_t kHooks[] = {0x8BD443};
  size_t n = SmModArmHooks("credits-skip", kHooks, 1);
  fprintf(stderr, "[mods] credits skip: %zu/1 hooks\n", n);
}

SNES_MOD_CONSTRUCTOR(sm_register_cinematic_skip_plugins) {
  snes_mod_register_activation_plugin("super-metroid.pacing.intro-skip",
                                      SmIntroSkipActivate);
  snes_mod_register_activation_plugin("super-metroid.pacing.ceres-skip",
                                      SmCeresSkipActivate);
  snes_mod_register_activation_plugin("super-metroid.pacing.credits-skip",
                                      SmCreditsSkipActivate);
}
