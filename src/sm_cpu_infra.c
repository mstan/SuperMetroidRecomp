#include "common_cpu_infra.h"
#include "sm_rtl.h"
#include "sm_post_mortem.h"
#include "sm_replay.h"
#include "sm_video.h"
#include "sm_renderer.h"
#include "sm_activity.h"
#include "sm_rooms.h"
#include "sm_seamless.h"
#include "sm_room_collision.h"
#include "mods/sm_mod_overlay.h"

static bool SmFrameBegin(uint32_t *inputs) {
  if (!SmActivityFrameBegin()) return false;
  if (!SmReplayBegin(inputs)) return false;
  if (!SmRoomCollisionFrameBegin()) return false;
  if (g_sm_video.enhanced && g_sm_video.seamless && !SmSeamlessDoor()) return false;
  if (!SmRoomsFrame(g_ram,g_sm_video.enhanced,SmActivityExtra(),g_sm_video.neighboring_logic,g_sm_video.seamless) ||
      !SmRendererLatchRooms(SmRoomsScene())) return false;
  /* Playback may restore a checkpoint and change presentation at this exact
   * boundary, so latch objects after those changes. */
  if (g_sm_video.enhanced || g_sm_video.fps_enabled || SmActivityNetworkWide() || SmModOverlayActive())
    SmRendererLatchObjectState(g_ram);
  return true;
}

/* Called by the framework from SnesInit, once the machine exists. */
static void SmInitialize(void) {
  SmActivityInitialize();
  SmRoomsSetDoorEvent(SmActiveDoorEvent);
  SmRoomsSetEnemyReaction(SmActiveEnemyBlock);
  /* Add Super Metroid's object to the crash/exit report. Registered through
   * the framework hook rather than by forking post_mortem.c, which is how
   * this port previously carried its own 833-line copy of that file. */
  SmPostMortemRegister();
}

/* Game registration consumed by RtlRegisterGame() in main.c. The
 * `.title` / `.save_name_prefix` strings drive the save-file naming
 * (saves/save<N>.sav). run_frame / draw_ppu_frame are the per-frame
 * host hooks implemented in sm_rtl.c. */
const RtlGameInfo kSuperMetroidGameInfo = {
  .title = "sm",
  .initialize = &SmInitialize,
  .hardware_reset = &SmHardwareReset,
  .run_frame = &RunOneFrameOfGame,
  .frame_begin = &SmFrameBegin,
  .frame_end = &SmReplayEnd,
  .draw_ppu_frame = &SmDrawPpuFrame,
  .save_name_prefix = "save",
  .state_save_extra = &SmStateSaveExtra,
  .state_load_extra = &SmStateLoadExtra,
  .on_state_loaded = &SmOnStateLoaded,
  .snapshot_guard_identity = &SmSnapshotIdentity,
  .snapshot_preflight = &SmSnapshotPreflight,
  /* Rollback restores the guest scheduler continuation alongside RAM. */
  .exec_state_bound = &SmExecStateBound,
  .exec_state_save  = &SmExecStateSave,
  .exec_state_load  = &SmExecStateLoad,
};
