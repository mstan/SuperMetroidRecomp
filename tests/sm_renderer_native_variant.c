/* Qualify the optimized implementation even while product defaults are OFF. */
#define SM_NATIVE_RASTER 1
#define SM_NATIVE_ROW_PLAN 1
#define SM_NATIVE_CENTER_REUSE 1
#include "../src/sm_renderer.c"
