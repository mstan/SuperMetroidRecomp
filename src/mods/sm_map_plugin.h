#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Other pause readouts yield their space while the whole-world view is open. */
bool SmMapFullScreen(const uint8_t *wram);
