#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Host-only diagnostics. Never part of guest saves or rollback state. */
void SmReplayRomLoaded(const uint8_t *rom, unsigned size);
void SmReplaySetConfigPath(const char *path);
void SmReplayStateLoaded(void);
void SmReplayShutdown(void);
bool SmReplayBegin(uint32_t *inputs);
void SmReplayEnd(void);
void SmReplayRendered(void);
struct SmViewport;
void SmReplayViewport(struct SmViewport *view);
bool SmReplayPlayback(const char *path);
bool SmReplayPlaying(void);
bool SmReplayFinished(void);
bool SmReplayFailed(void);
/* Same visible recording switch, cached environment override and playback/error
 * gates used by the recorder. Does not change its execution/segment policy. */
bool SmReplayDiagnosticsEnabled(void);
