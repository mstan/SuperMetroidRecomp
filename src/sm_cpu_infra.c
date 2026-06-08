#include "common_cpu_infra.h"
#include "sm_rtl.h"

/* Game registration consumed by RtlRegisterGame() in main.c. The
 * `.title` / `.save_name_prefix` strings drive the save-file naming
 * (saves/save<N>.sav). run_frame / draw_ppu_frame are the per-frame
 * host hooks implemented in sm_rtl.c. */
const RtlGameInfo kSuperMetroidGameInfo = {
  .title = "sm",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &SmDrawPpuFrame,
  .save_name_prefix = "save",
};
