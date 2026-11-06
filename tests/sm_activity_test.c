#include "sm_activity.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char ram[0x20000], before[sizeof(ram)];
static void put(unsigned at, unsigned value) { ram[at]=value;ram[at+1]=value>>8; }
static unsigned get(unsigned at) { return ram[at] | ram[at+1]<<8; }
static void enemy(unsigned i, int x, unsigned properties) {
  unsigned at=0xf78+i*64;
  put(at,0xd4bf);put(at+2,1000+x);put(at+6,100);put(at+10,8);put(at+14,properties);
}
static void setup(void) {
  memset(ram,0,sizeof(ram));put(0x911,1000);put(0x915,0);
  enemy(7,128,0);put(0x17a4,2);put(0x17a6,2);
  put(0x17ac,7*64);put(0x17ae,0xffff);put(0x17ec,7*64);put(0x17ee,0xffff);
}
static void projectile_handoff(void) {
  memset(ram,0,sizeof(ram));
  /* Retain live instruction lists independently of type bits. Super missiles
   * keep linked-slot references; bombs share the pool's second half. */
  put(0xc40,0x8123);put(0xb64,2308);put(0xb78,400);put(0xb8c,0xc000);
  put(0xc1a,0x200);put(0xc42,0x8234);put(0xc7e,8);put(0xb66,2294);put(0xb7a,400);
  put(0xc22,0x500);put(0xc4a,0x8345);put(0xc72,0x91a0);put(0xb6e,2275);put(0xb82,410);
  put(0xcce,2);put(0xcd2,1);put(0xc54,3);put(0xb68,333);
  put(0xd658,2);put(0xd730,2296);put(0xd778,405);
  put(0xd69e,1);put(0xd776,2302);put(0xd7be,406);
  memcpy(before,ram,sizeof(ram));
  SmActivityRebaseProjectiles(ram,2304,256);
  assert(get(0xb64)==4 && get(0xb78)==144);
  assert((int16_t)get(0xb66)==-10 && get(0xb7a)==144 && get(0xc7e)==8);
  assert((int16_t)get(0xb6e)==-29 && get(0xb82)==154 && get(0xc72)==0x91a0);
  assert(get(0xb68)==333 && get(0xb8c)==0xc000 && get(0xc54)==3);
  assert(get(0xcce)==2 && get(0xcd2)==1);
  assert((int16_t)get(0xd730)==-8 && get(0xd778)==149);
  assert((int16_t)get(0xd776)==-2 && get(0xd7be)==150);
  SmActivityRebaseProjectiles(ram,-2304,-256);
  assert(!memcmp(before,ram,sizeof(ram)));
}
int main(void) {
  projectile_handoff();
  setup();enemy(1,360,0);
  assert(!SmActivityEnemyHorizontalVisible(ram,64,0,false) && SmActivityEnemyHorizontalVisible(ram,64,214,false));
  for (unsigned normal=0; normal<2; ++normal) for (unsigned extra=0; extra<=214; extra+=107) {
    int radius=normal ? 8 : 0;
    enemy(1,-(int)extra-radius,0);assert(SmActivityEnemyHorizontalVisible(ram,64,extra,normal!=0));
    enemy(1,-(int)extra-radius-1,0);assert(!SmActivityEnemyHorizontalVisible(ram,64,extra,normal!=0));
    enemy(1,256+(int)extra+radius,0);assert(SmActivityEnemyHorizontalVisible(ram,64,extra,normal!=0));
    enemy(1,257+(int)extra+radius,0);assert(!SmActivityEnemyHorizontalVisible(ram,64,extra,normal!=0));
  }
  put(0x911,0xff00);put(0xf7a+64,104);assert(SmActivityEnemyHorizontalVisible(ram,64,214,false));
  assert(!SmActivityEnemyHorizontalVisible(ram,1,214,false) && !SmActivityEnemyHorizontalVisible(ram,64,215,false));
  const unsigned widths[]={43,96,213,214};
  for(unsigned k=0;k<4;k++) {
    unsigned extra=widths[k];setup();
    enemy(1,-(int)extra,0);enemy(8,256+extra,0x400);
    enemy(10,-(int)extra-9,0);enemy(11,256+extra+9,0);
    enemy(12,275,0x200);enemy(13,275,0);put(0xf78+13*64,0xdaff);
    enemy(14,275,0);put(0xf78+14*64+6,249);
    memcpy(before,ram,sizeof(ram));
    assert(SmActivityAugmentQueues(ram,0));assert(!memcmp(before,ram,sizeof(ram)));
    assert(SmActivityAugmentQueues(ram,extra));
    assert(get(0x17a4)==6 && get(0x17a6)==4);
    assert(get(0x17ac)==64 && get(0x17ae)==448 && get(0x17b0)==512 && get(0x17b2)==0xffff);
    assert(get(0x17ec)==64 && get(0x17ee)==448 && get(0x17f0)==0xffff);
    assert(!memcmp(ram+0xf78,before+0xf78,2048)); /* properties never forged */
    memcpy(before,ram,sizeof(ram));assert(SmActivityAugmentQueues(ram,extra));
    assert(!memcmp(before,ram,sizeof(ram))); /* idempotent */
  }
  setup();put(0x183c,1);enemy(1,-20,0);memcpy(before,ram,sizeof(ram));
  assert(SmActivityAugmentQueues(ram,214));assert(!memcmp(before,ram,sizeof(ram)));
  setup();for(unsigned i=0;i<31;i++)enemy(i,275,0);
  assert(SmActivityAugmentQueues(ram,43));assert(get(0x17a4)==62 && get(0x17ea)==0xffff);
  setup();for(unsigned i=0;i<32;i++)enemy(i,275,0);memcpy(before,ram,sizeof(ram));
  assert(!SmActivityAugmentQueues(ram,43));assert(!memcmp(before,ram,sizeof(ram)));
  setup();for(unsigned i=0;i<32;i++)enemy(i,275,0x400);
  assert(SmActivityAugmentQueues(ram,43));assert(get(0x17a4)==64 && get(0x17a6)==0 && get(0x17ec)==0xffff);
  memcpy(before,ram,sizeof(ram));
  assert(!SmActivityAugmentQueues(ram,215));assert(!memcmp(before,ram,sizeof(ram)));
  puts("PASS: native viewport horizontal edges/radii/wrap, widened queues, slot order, exclusions, interaction and capacity");
}
