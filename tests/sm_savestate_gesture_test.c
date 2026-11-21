/* The lock-up: a held button re-satisfying the open gesture forever.
 *
 * snes_savestate_menu.h states the contract in words -- filter once per
 * frame, pass the RESULT to both the guest and poll_open -- and a host that
 * passes the unfiltered word instead gets a browser that reopens on the frame
 * after it closes, every frame, so the game never runs again. This drives the
 * module the way a host does and asserts the loop terminates. */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "snes_savestate_menu.h"
#include "snes_overlay_draw.h"

/* The module's only externals. Stubbed so this test links against the
 * overlay modules alone rather than the whole runtime -- the behaviour under
 * test is the input guard, which touches none of them. */
#include "common_rtl.h"
#include "netplay/snes_netplay.h"
#include "snes_osd.h"
void RtlSaveLoad(int t, int slot) { (void)t; (void)slot; }
/* The menu now saves/loads through snapshot paths directly (framework
 * 2026-09); the gesture under test never reaches them. */
void RtlEnsureSaveDir(void) { }
bool RtlSaveSnapshot(const char *filename) { (void)filename; return false; }
bool RtlSaveSnapshotRingBlanked(const char *filename) { (void)filename; return false; }
bool RtlLoadSnapshot(const char *filename) { (void)filename; return false; }
void RtlSaveSlotPath(int slot, char *buf, size_t buflen) {
    if (buf && buflen) snprintf(buf, buflen, "/nonexistent/slot%d.sav", slot);
}
void snes_osd_push_slot_saved(int s) { (void)s; }
void snes_osd_push_slot_loaded(int s) { (void)s; }
void snes_osd_push_slot_empty(int s) { (void)s; }
/* Exercise the offline gesture without linking a session or toast renderer. */
int snes_netplay_active(void) { return 0; }
int snes_netplay_is_host(void) { return 0; }
int snes_netplay_menu_open(void) { return 0; }
int snes_netplay_menu_paused(void) { return 0; }
int snes_netplay_menu_ready(void) { return 0; }
void snes_netplay_menu_cursor(int slot) { (void)slot; }
int snes_netplay_menu_save(int slot) { (void)slot; return 0; }
int snes_netplay_menu_load(int slot) { (void)slot; return 0; }
int snes_netplay_menu_close(void) { return 0; }
void snes_osd_push(const char *message, int duration_ms) {
    (void)message; (void)duration_ms;
}

static int fails, checks;
static void check(int c, const char *w) {
    checks++; if (c) printf("  ok    %s\n", w);
    else { fails++; printf("  FAIL  %s\n", w); }
}

/* A player holding Select+R -- as they must, to make the gesture at all. */
#define HELD (SNES_PAD_SELECT | SNES_PAD_R)

int main(void) {
    printf("save-state browser: a held gesture must not reopen it forever\n");

    /* Frame 1: the gesture opens the browser. */
    uint32_t w = snes_savestate_menu_filter_guest_input(HELD);
    check(snes_savestate_menu_poll_open(w) == 1, "Select+R opens it");
    check(snes_savestate_menu_is_open(), "and it reports open");

    /* The player closes it (B on the pad / Escape) while STILL holding the
     * shoulder button, which is the normal way this happens. */
    snes_savestate_menu_close();
    check(!snes_savestate_menu_is_open(), "closing works");

    /* Now the host keeps running frames with the button still held. Correct
     * behaviour: the guard masks it until released, so it stays closed. */
    int reopened = 0;
    for (int f = 0; f < 600; f++) {
        uint32_t fw = snes_savestate_menu_filter_guest_input(HELD);
        if (snes_savestate_menu_poll_open(fw)) { reopened = 1; break; }
    }
    check(!reopened,
          "600 frames with the gesture still held: it stays closed");

    /* Releasing and pressing again must still work -- the guard shrinks. */
    (void)snes_savestate_menu_filter_guest_input(0);
    uint32_t again = snes_savestate_menu_filter_guest_input(HELD);
    check(snes_savestate_menu_poll_open(again) == 1,
          "after releasing, the gesture opens it again");

    /* And the other direction, so this test has teeth: a host that skips the
     * filter and polls the RAW word reopens the browser immediately. If this
     * check ever starts failing, the module grew its own guard and the host
     * contract changed -- read snes_savestate_menu.h before "fixing" it. */
    snes_savestate_menu_close();
    (void)snes_savestate_menu_filter_guest_input(0);   /* release the guard */
    (void)snes_savestate_menu_poll_open(0);            /* settle the edge */
    {
        int reopened_unfiltered = 0;
        for (int f = 0; f < 4; f++) {
            if (snes_savestate_menu_poll_open(HELD)) { reopened_unfiltered = 1; break; }
        }
        check(reopened_unfiltered,
              "the UNFILTERED word does reopen it (this is the bug the filter prevents)");
    }

    snes_savestate_menu_close();
    snes_savestate_menu_filter_guest_input(0);
    check(snes_savestate_menu_open(SNES_PAD_B), "host shortcut opens with its actual held buttons");
    snes_savestate_menu_poll_nav(SNES_PAD_B, 1000);
    check(snes_savestate_menu_is_open(), "held opening B does not immediately close the menu");
    snes_savestate_menu_poll_nav(0, 1001);
    snes_savestate_menu_poll_nav(SNES_PAD_B, 1002);
    check(!snes_savestate_menu_is_open(), "release and press B closes normally");
    check(!snes_savestate_menu_filter_guest_input(SNES_PAD_B),
          "closing B stays out of guest input until release");
    snes_savestate_menu_filter_guest_input(0);
    check(snes_savestate_menu_filter_guest_input(SNES_PAD_B) == SNES_PAD_B,
          "released navigation button returns to gameplay");
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
