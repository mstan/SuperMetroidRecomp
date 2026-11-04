#ifndef SM_SM_RTL_H_
#define SM_SM_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

/* Per-frame host orchestration for Super Metroid (see sm_rtl.c). */
void SmDrawPpuFrame(void);
void RunOneFrameOfGame(void);
void SmHardwareReset(void);

/* File snapshot extension for the default LLE scheduler (no host pointers). */
void SmStateSaveExtra(struct SaveLoadInfo *sli);
void SmStateLoadExtra(struct SaveLoadInfo *sli, uint32_t version);
void SmOnStateLoaded(uint32_t version);
const char *SmSnapshotIdentity(void);
bool SmSnapshotPreflight(const void *data, size_t size);

/* Build-specific scheduler/SPC position for rollback and run-ahead. */
size_t SmExecStateBound(void);
size_t SmExecStateSave(void *out, size_t capacity);
int    SmExecStateLoad(const void *in, size_t size);

#endif  /* SM_SM_RTL_H_ */
