#pragma once
#include <stdbool.h>
/* Called only at the guest's ordinary frame boundary. Native transition
 * mode and non-planar connections retain their cartridge scheduler. */
bool SmSeamlessDoor(void);
