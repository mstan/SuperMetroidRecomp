#include "sm_map_art.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool SmMapArtLoad(SmMapArt *art, const char *path) {
  static const uint8_t expected[32] = {
    0xfc,0x94,0xf6,0x53,0x80,0xb6,0x5b,0x87,0x0a,0x30,0xb9,0xb0,0x4b,0x39,0xcc,0xa1,
    0xdc,0x63,0xd6,0xe4,0x6a,0x4a,0x37,0x3d,0x39,0x04,0xad,0xc0,0x91,0x2e,0xbc,0x37};
  memset(art,0,sizeof(*art));
  if (!path || !*path) return false;
  FILE *f=fopen(path,"rb");
  if (!f) return false;
  uint8_t *rom=NULL, digest[32];
  bool valid=fseek(f,0,SEEK_END)==0 && ftell(f)==0x800000 && fseek(f,0,SEEK_SET)==0;
  if (valid) { rom=malloc(0x800000); valid=rom && fread(rom,1,0x800000,f)==0x800000; }
  fclose(f);
  if (valid) {
    sha256_compute(rom,0x800000,digest);
    valid=!memcmp(digest,expected,sizeof(expected));
  }
  if (valid) {
    /* metroidret/mzm 7708d105: minimap_tiles.gfx + 40f4c4.gfx,
     * minimap_tiles.pal. Original Nintendo graphics; never bundled. */
    memcpy(art->tiles,rom+0x40e0c4,sizeof(art->tiles));
    for (unsigned i=0;i<80;++i)
      art->palette[i]=(uint16_t)(rom[0x411360+2*i]|rom[0x411361+2*i]<<8);
    art->ready=true;
  }
  free(rom); return valid;
}
unsigned SmMapArtRoomTile(unsigned walls) {
  static const unsigned tiles[16]={
    0x67,0x20,0x420,0x83,0x01,0x00,0x400,0x63,
    0x801,0x800,0xc00,0x863,0x41,0x40,0x440,0x87};
  return tiles[walls&15];
}
uint32_t SmMapArtPixel(const SmMapArt *art,unsigned tile,unsigned x,unsigned y,unsigned palette) {
  if (!art || !art->ready || (tile&0x3ff)>=384 || x>=8 || y>=8 || palette>=5) return 0;
  if (tile&0x400) x=7-x;
  if (tile&0x800) y=7-y;
  unsigned texel=(art->tiles[(tile&0x3ff)*32+y*4+x/2]>>(4*(x&1)))&15;
  unsigned c=art->palette[palette*16+texel];
  unsigned red=c&31,green=(c>>5)&31,blue=(c>>10)&31;
  return 0xff000000u|((red<<3|red>>2)<<16)|((green<<3|green>>2)<<8)|(blue<<3|blue>>2);
}
