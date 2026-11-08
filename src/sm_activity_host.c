#include "sm_activity.h"
#include "sm_video.h"
#include "sm_replay.h"
#include "common_rtl.h"
#include "host_api.h"
#include <stdio.h>
#if defined(SNES_HAS_LOBBY_CLIENT)
#include "snes_netplay.h"
#endif

static bool network_profile_valid, local_profile_valid, network_error;
static unsigned network_extra, local_extra;
static unsigned network_flags, local_flags;

static unsigned local_view(void) {
#if SM_VIEW_ACTIVITY
  int w = 0, h = 0;
  if (g_window) SDL_GetWindowSize(g_window, &w, &h);
  return (unsigned)SmCalculateViewport(&g_sm_video, w, h).extra;
#else
  return 0;
#endif
}
static bool network_active(void) {
#if defined(SNES_HAS_LOBBY_CLIENT)
  return snes_netplay_active() != 0;
#else
  return false;
#endif
}
#if defined(SNES_HAS_LOBBY_CLIENT)
static void capture_profile(uint8_t out[2]) {
  if (!local_profile_valid) {
    local_extra = local_view(); local_profile_valid = true;
    local_flags=(g_sm_video.enhanced ? 4u : 0u) | (g_sm_video.neighboring_logic ? 8u : 0u) |
        (g_sm_video.seamless ? 16u : 0u) | (g_sm_video.connected_rooms ? 64u : 0u);
  }
  out[0] = (uint8_t)local_extra; out[1] = (uint8_t)((SM_VIEW_ACTIVITY ? (0xa0 | SM_ACTIVITY_PROFILE) : 0xa0) | local_flags);
}
static void apply_profile(const uint8_t in[2]) {
  unsigned expected = SM_VIEW_ACTIVITY ? (0xa0 | SM_ACTIVITY_PROFILE) : 0xa0;
  if ((in[1]&0xa3) != expected || !SmActivityExtraValid(in[0]) ||
      (network_profile_valid && (network_extra != in[0] || network_flags!=(in[1]&92u)))) {
    if (!network_error) fprintf(stderr, "[sm-activity] incompatible or changing netplay viewport profile\n");
    network_error = true; snesrecomp_request_exit(); return;
  }
  if (!network_profile_valid)
    fprintf(stderr, "[sm-activity] netplay viewport profile=%u extra=%u fixed for this match\n", expected, in[0]);
  network_extra = in[0]; network_profile_valid = true;
  network_flags=in[1]&92u;
  g_sm_video.enhanced=(network_flags&4)!=0;
  g_sm_video.neighboring_logic=(network_flags&8)!=0;
  g_sm_video.seamless=(network_flags&16)!=0;
  g_sm_video.connected_rooms=(network_flags&64)!=0;
  SmActivityRestore(network_extra);
}
#endif
void SmActivityInitialize(void) {
  network_profile_valid = local_profile_valid = network_error = false;
  network_extra = local_extra = 0; SmActivityRestore(0);
  network_flags=local_flags=0;
#if defined(SNES_HAS_LOBBY_CLIENT)
  snes_netplay_set_sync_byte_hooks(capture_profile, apply_profile);
#endif
}
bool SmActivityFrameBegin(void) {
  if (network_active()) {
    if (network_error || !network_profile_valid) return false;
    SmActivityRestore(network_extra);
  } else if (!RtlSpeculativeFrame() && !SmReplayPlaying()) {
    network_profile_valid = local_profile_valid = false;
    SmActivityRestore(local_view());
  }
  return true;
}
bool SmActivityNetworkWide(void) {
  return network_active() && network_profile_valid && network_extra != 0;
}
void SmActivityViewport(SmViewport *view) {
  if (!network_active() || !network_profile_valid) return;
  /* A local resize must not reveal a larger, sleeping simulation margin. */
  view->extra = (int)network_extra; view->width = 256 + 2 * view->extra;
  view->aspect = (4.0 / 3.0) * view->width / 256.0;
  view->enhanced = view->extra != 0;
}
