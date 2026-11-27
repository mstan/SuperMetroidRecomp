#include "sm_door.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t ram[0x20000],before[sizeof(ram)];
static void put(unsigned at,unsigned v) { ram[at]=v;ram[at+1]=v>>8; }
static uint8_t read_rom(void *unused,uint32_t at) {
  (void)unused;
  /* Distinct opening/closing ranges, as in native door PLM definitions. */
  return (at&1) ? 0xb0 : 0;
}
static bool clear(int x,int y) {
  memcpy(before,ram,sizeof(ram));bool result=SmDoorOpeningBodyClear(read_rom,NULL,ram,x,y);
  assert(!memcmp(before,ram,sizeof(ram)));return result;
}
int main(void) {
  put(0x7a5,16);put(0x7a7,16);
  unsigned anchor=4*16+6;
  put(0x1c87,2*anchor);put(0x1d27,0xc000);
  for(unsigned direction=0;direction<4;++direction) {
    put(0x10002+2*anchor,0xc052);ram[0x16402+anchor]=0x40+direction;
    put(0x1c37,0xc872+6*direction);put(0x1cd7,0xbd88);assert(!clear(6,4)); /* locked Super cap */
    put(0x1cd7,0x8aa6);assert(clear(6,4)); /* native condition has succeeded */
    put(0x10002+2*(anchor+1),0x5052);ram[0x16402+anchor+1]=0xff;
    put(0x10002+2*(anchor+16),0xd052);ram[0x16402+anchor+16]=0xff;
    assert(clear(7,4) && clear(6,5));
    /* Actual blue/colored draw words erase extension types while retaining
     * the negative anchor offsets on each solid animation cell. */
    for(unsigned n=1;n<4;++n) {
      unsigned cell=anchor+n*(direction<2 ? 16 : 1);
      put(0x10002+2*cell,0x8052);ram[0x16402+cell]=(uint8_t)-(int)n;
      assert(clear(6+(direction>=2 ? (int)n : 0),4+(direction<2 ? (int)n : 0)));
    }
    put(0x10002+2*(anchor+4*(direction<2 ? 16 : 1)),0x8052);
    assert(!clear(6+(direction>=2 ? 4 : 0),4+(direction<2 ? 4 : 0)));
    put(0x1c37,0xc8a2);assert(clear(6,4)); /* actual blue opening PLM */
    put(0x1c37,0xc8ba);assert(!clear(6,4)); /* arrival/closing cap */
    put(0x1c37,0xc87e);put(0x1cd7,0xbe01);assert(!clear(6,4)); /* enemy quota */
  }
  put(0x1c37,0xc8a2);ram[0x16402+anchor]=8;assert(!clear(6,4)); /* special shot block */
  ram[0x16402+anchor]=0x40;put(0x10002+2*anchor,0x8052);assert(clear(6,4)); /* blue setup makes a solid cap */
  put(0x1c37,0);assert(!clear(6,4)); /* ordinary solid terrain */
  put(0x1c37,0xc87e);put(0x1cd7,0x8aa6);ram[0x16402+anchor]=0x44;
  assert(clear(6,4)); /* colored setup uses its shared cap BTS */
  put(0x1cd7,0xbd88);assert(!clear(6,4));
  assert(!clear(-1,4) && !clear(16,4));
  puts("PASS: immediate body passage only during native door opening, extensions and all orientations");
}
