/* super-metroid.pacing / return-to-title: leave the final screen with Start.
 *
 * The stock ending stops on its final screen (cinematic_function nullsub_127,
 * $8B:E7BA, once CinematicFunction_Intro_Func148 has scrolled cinematic_var10
 * $1997 below -79) until the console is reset. The hook sits after the
 * cinematic call in GameState_39_EndingAndCredits_ ($8B:D44F):
 *
 *   1. Start on the final screen selects CinematicFunctionNone ($8B:9A47, the
 *      title's empty step, never used in game state $27) as the marker of a
 *      quit in progress and primes a fade (screen_fade_delay/counter 1).
 *   2. While the marker is set, the hook steps the fade exactly as
 *      HandleFadeOut ($80:8924) does, on the INIDISP mirror $51.
 *   3. At black it enters the game-over menu's last step (game_state $1A,
 *      menu_index 7 = GameOverMenu_7_FadeOutToSoftReset). Next frame that
 *      step finds the screen black and takes the game's own soft reset,
 *      JMP $80:8462, the same path as Game Over -> End: WRAM is cleared and
 *      the title sequence starts; the SRAM saves are untouched.
 *
 * GameOverMenu_7 also runs DrawMenuSelectionMissile and
 * HandleGameOverBabyMetroid for that one (black) frame, so their state is
 * reset first: the missile animation ($198D, $1997) and the baby-metroid
 * timer ($0F94), which makes the latter start its stock animation instead of
 * following a stale enemy pointer.
 *
 * Disproved: reusing the intro's fade-out CinematicFunction_Intro_Func34
 * ($8B:B72F) as the fade hung the ending (NMI counter frozen once it reached
 * black, game state stuck at $27).
 */

#include <stdio.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "sm_return_to_title_plugin.h"
#include "../sm_pause_options.h"
#include "snes_overlay_draw.h"

#define RAM16(cpu, a) ((uint16_t)((cpu)->ram[(a)] | (cpu)->ram[(a) + 1] << 8))
static void Put16(CpuState *cpu, uint32_t a, uint16_t v) {
  cpu->ram[a] = (uint8_t)v;
  cpu->ram[a + 1] = (uint8_t)(v >> 8);
}

enum {
  kInidisp = 0x51, kJoypad1New = 0x8F, kButtonStart = 0x1000,
  kScreenFadeDelay = 0x723, kScreenFadeCounter = 0x725, kMenuIndex = 0x727,
  kGameState = 0x998, kBabyMetroidTimer = 0xF94,
  kMenuMissileTimer = 0x198D, kMenuMissileFrame = 0x1997,
  kCinematicVar10 = 0x1997, kCinematicFunction = 0x1F51,
  kNullsub127 = 0xE7BA, kQuitMarker = 0x9A47, /* CinematicFunctionNone */
};

enum { kQuitIdle, kQuitRequested, kQuitFading, kQuitNavigating };
static bool g_quit_enabled;
static uint32_t g_quit_state;

bool SmQuitFileSelectEnabled(void) { return g_quit_enabled; }
bool SmQuitFileSelectRequest(void) {
  if (!g_quit_enabled) return false;
  g_quit_state=kQuitRequested; return true;
}
uint32_t SmQuitFileSelectState(void) { return g_quit_state; }
bool SmQuitFileSelectStateValid(uint32_t state) { return state<=kQuitNavigating; }
void SmQuitFileSelectRestore(uint32_t state) { if (SmQuitFileSelectStateValid(state)) g_quit_state=state; }
static bool FadeOut(CpuState *cpu) {
  uint8_t *inidisp=&cpu->ram[kInidisp];
  uint16_t counter=RAM16(cpu,kScreenFadeCounter);
  if (!counter) {
    Put16(cpu,kScreenFadeCounter,RAM16(cpu,kScreenFadeDelay));
    if (*inidisp&15) *inidisp=(*inidisp&15)==1 ? 0x80 : (uint8_t)((*inidisp&15)-1);
  } else Put16(cpu,kScreenFadeCounter,(uint16_t)(counter-1));
  return !(*inidisp&15);
}

/* The existing Options wrapper runs this before entering the original pause
 * routine. A true result delegates to its bare RTS: no bank prefix was run
 * and no old pause dispatch may run with the new game-over menu index. */
bool SmQuitPauseStep(CpuState *cpu) {
  if (!g_quit_enabled || (g_quit_state!=kQuitRequested && g_quit_state!=kQuitFading) || RAM16(cpu,kGameState)!=15) return 0;
  if (g_quit_state==kQuitRequested) {
    Put16(cpu,kScreenFadeDelay,1); Put16(cpu,kScreenFadeCounter,1); g_quit_state=kQuitFading;
  }
  SmPauseOptions_SetActive(false);
  /* Fade the existing pause picture first. Entering Game Over before black
   * would draw its baby/missile objects with the pause screen's graphics. */
  if (FadeOut(cpu)) {
    Put16(cpu,kMenuMissileTimer,0); Put16(cpu,kMenuMissileFrame,0); Put16(cpu,kBabyMetroidTimer,0);
    Put16(cpu,kScreenFadeDelay,0); Put16(cpu,kScreenFadeCounter,0);
    Put16(cpu,kMenuIndex,7); Put16(cpu,kGameState,0x1a); g_quit_state=kQuitNavigating;
  }
  return true;
}
/* Finish navigation in guest execution, including speculative/rollback frames. */
uint32 SmQuitFileSelectReadyHook(CpuState *cpu,uint32 pc24) {
  (void)pc24;
  if (g_quit_enabled && g_quit_state==kQuitNavigating && RAM16(cpu,kGameState)==4) g_quit_state=kQuitIdle;
  return 0;
}
static uint16_t SmQuitNavigationInput(uint16_t buttons,const SnesModInputContext *context) {
  if (context->player || !context->wram || !g_quit_enabled || g_quit_state!=kQuitNavigating) return buttons;
  const uint8_t *p=context->wram;
  uint16_t state=(uint16_t)(p[kGameState]|p[kGameState+1]<<8);
  uint16_t frame=(uint16_t)(p[0x5b6]|p[0x5b7]<<8);
  /* Fresh one-frame Start edges navigate the normal title/options screens.
   * Use guest time, with no host counter or writes into controller WRAM. */
  return (state==1 || state==2) && !(frame&7) ? (uint16_t)(buttons|SNES_PAD_START) : buttons;
}

uint32 SmReturnToTitleHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (RAM16(cpu, kGameState) != 0x27) return 0;
  uint16_t fn = RAM16(cpu, kCinematicFunction);
  if (fn == kQuitMarker) {
    if (!FadeOut(cpu)) return 0;
    cpu->ram[kInidisp] = 0x80;
    Put16(cpu, kCinematicFunction, kNullsub127);
    Put16(cpu, kScreenFadeDelay, 0);
    Put16(cpu, kScreenFadeCounter, 0);
    Put16(cpu, kMenuMissileTimer, 0);
    Put16(cpu, kMenuMissileFrame, 0);
    Put16(cpu, kBabyMetroidTimer, 0);
    Put16(cpu, kMenuIndex, 7);
    Put16(cpu, kGameState, 0x1A);
    return 0;
  }
  if (fn == kNullsub127 && (int16_t)RAM16(cpu, kCinematicVar10) < -79 &&
      (RAM16(cpu, kJoypad1New) & kButtonStart)) {
    Put16(cpu, kCinematicFunction, kQuitMarker);
    Put16(cpu, kScreenFadeDelay, 1);
    Put16(cpu, kScreenFadeCounter, 1);
  }
  return 0;
}

static void SmReturnToTitleActivate(void) {
  static const uint32_t kHooks[] = {0x8BD44F};
  size_t n = SmModArmHooks("return-to-title", kHooks, 1);
  fprintf(stderr, "[mods] return to title: %zu/1 hooks\n", n);
}
static void SmQuitFileSelectActivate(void) {
  static const uint32_t pcs[]={0x8289e5};
  g_quit_enabled=true; SmModArmHooks("quit-to-file-select",pcs,1);
  snes_mod_register_input_transform(SmQuitNavigationInput);
}
static void SmReturnReset(void) { g_quit_enabled=false; g_quit_state=kQuitIdle; }

SNES_MOD_CONSTRUCTOR(sm_register_return_to_title_plugin) {
  snes_mod_register_reset_callback(SmReturnReset);
  snes_mod_register_activation_plugin("super-metroid.pacing.return-to-title",
                                      SmReturnToTitleActivate);
  snes_mod_register_activation_plugin("super-metroid.pacing.quit-to-file-select",
                                      SmQuitFileSelectActivate);
}
