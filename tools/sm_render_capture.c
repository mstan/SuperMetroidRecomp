#include "sm_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Offline replay of real raster captures; no guest execution. */
int main(int argc, char **argv) {
  if (argc != 5 && argc != 7) {
    fprintf(stderr, "usage: sm_render_capture capture rom aspect output.bmp [previous-capture alpha]\n");
    return 2;
  }
  FILE *rom = fopen(argv[2], "rb");
  if (!rom) return 2;
  if (fseek(rom, 0, SEEK_END)) return 2;
  long length = ftell(rom);
  if (length <= 0 || length > 0x1000000) return 2;
  rewind(rom);
  uint8_t *bytes = malloc((size_t)length);
  if (!bytes || fread(bytes, (size_t)length, 1, rom) != 1) return 2;
  fclose(rom);
  SmRendererSetRom(bytes, (size_t)length);
  if (argc == 7 && !SmRendererLoadCapture(argv[5])) return 2;
  if (!SmRendererLoadCapture(argv[1])) return 2;
  SmVideoSettings settings;
  SmVideoDefaults(&settings);
  settings.enhanced = true;
  if (!SmParseAspect(argv[3], &settings.aspect)) return 2;
  SmViewport view = SmCalculateViewport(&settings, 16, 9);
  uint32_t *output = calloc((size_t)view.width * 224, sizeof(*output));
  double alpha = argc == 7 ? strtod(argv[6], NULL) : 1;
  if (!output || !SmRendererDraw(output, view, true, alpha)) return 2;
  const char *benchmark = getenv("SM_RENDER_BENCHMARK_COUNT");
  if (benchmark) {
    char *end;
    unsigned long count = strtoul(benchmark, &end, 10);
    if (*end || !count || count > 10000) return 2;
    clock_t start = clock();
    for (unsigned long i = 0; i < count; ++i)
      if (!SmRendererDraw(output, view, true, alpha)) return 2;
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    fprintf(stderr, "renderer benchmark: width=%d alpha=%.3f count=%lu clock_seconds=%.6f ms_per_draw=%.3f\n",
            view.width, alpha, count, seconds, seconds * 1000 / count);
  }
  SmRendererStats stats = SmRendererGetStats();
  fprintf(stderr, "interpolation: matched OBJ samples=%u (Samus=%u), scroll lines=%u\n",
          stats.matched_object_samples, stats.matched_samus_samples, stats.interpolated_scroll_lines);
  fprintf(stderr, "interpolation: matched B4 effect samples=%u\n", stats.matched_effect_samples);
  fprintf(stderr, "scanout: custom=%u stock-fallback=%u HUD=%u lines\n",
          stats.custom_lines, stats.stock_lines, stats.hud_lines);
  fprintf(stderr, "Mode-7 custom lines: %u\n", stats.mode7_lines);
  fprintf(stderr, "Power-bomb extended window lines: %u\n", stats.power_bomb_lines);
  fprintf(stderr, "X-ray extended window lines: %u\n", stats.xray_lines);
  fprintf(stderr, "Eye-beam extended window lines: %u\n", stats.eye_beam_lines);
  fprintf(stderr, "Mother Brain extended window lines: %u\n", stats.mother_brain_lines);
  fprintf(stderr, "X-ray reveal tiles: %u / %u differ\n", stats.xray_tiles_differ, stats.xray_tiles_compared);
  fprintf(stderr, "Grapple: native pieces matched=%u margin pixels=%u\n", stats.grapple_native_matches, stats.grapple_margin_pixels);
  fprintf(stderr, "Grapple interpolated pieces: %u\n", stats.grapple_interpolated_pieces);
  fprintf(stderr, "Grapple flare native matches: %u\n", stats.grapple_flare_native_matches);
  fprintf(stderr, "recovered native-edge OBJ pixels=%u\n", stats.recovered_edge_pixels);
  unsigned mismatches = 0;
  if (alpha == 1) {
    const uint32_t *stock = SmRendererStockFrame();
    for (unsigned y = 32; y < 224; ++y)
      for (unsigned x = 0; x < 256; ++x)
        if ((output[y * view.width + view.extra + x] & 0xffffff) !=
            (stock[y * 256 + x] & 0xffffff)) ++mismatches;
    fprintf(stderr, "native center below HUD: %u / 49152 pixels differ\n", mismatches);
  }
  uint8_t header[54] = {'B', 'M'};
  uint32_t image_size = view.width * 224 * 4, size = 54 + image_size;
  uint32_t offset = 54, dib = 40;
  int32_t width = view.width, height = -224;
  uint16_t planes = 1, bpp = 32;
  memcpy(header + 2, &size, 4); memcpy(header + 10, &offset, 4);
  memcpy(header + 14, &dib, 4); memcpy(header + 18, &width, 4);
  memcpy(header + 22, &height, 4); memcpy(header + 26, &planes, 2);
  memcpy(header + 28, &bpp, 2); memcpy(header + 34, &image_size, 4);
  FILE *image = fopen(argv[4], "wb");
  if (!image) return 2;
  bool ok = fwrite(header, sizeof(header), 1, image) == 1 &&
            fwrite(output, image_size, 1, image) == 1;
  ok = fclose(image) == 0 && ok;
  free(bytes); free(output);
  if (getenv("SM_RENDER_REQUIRE_NATIVE_MATCH") &&
      (alpha != 1 || mismatches || !stats.custom_lines || stats.stock_lines)) {
    fprintf(stderr, "native comparison gate failed: requires alpha=1, custom gameplay, no stock fallback and zero mismatches\n");
    ok = false;
  }
  if (getenv("SM_RENDER_REQUIRE_XRAY_MATCH") &&
      (!stats.xray_lines || !stats.xray_tiles_compared || stats.xray_tiles_differ)) {
    fprintf(stderr, "X-ray comparison gate failed: requires active extended beam and exact reveal-map tiles\n");
    ok = false;
  }
  return ok ? 0 : 2;
}
