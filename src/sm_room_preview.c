#include "sm_room_preview.h"
#include "mods/sm_world.h"
#include <stdlib.h>
#include <string.h>

enum { kResidents = 16, kLevelBytes = 65536 };
typedef struct Preview {
  uint16_t room, state, incoming;
  int x, y, width, height;
  int left, top, right, bottom;
  unsigned level_bytes;
  uint8_t scroll_x, scroll_y;
  bool level_background;
  uint8_t padding[256];
  uint8_t level[kLevelBytes], definitions[8192], tiles[65536], palette[512];
} Preview;
static const uint8_t *image;
static size_t image_size;
static SmWorld world;
static bool attempted, ready;
static Preview *residents[kResidents];
static unsigned count;
static int camera_x, camera_y;
static uint16_t word(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint8_t read_byte(void *unused, uint32_t a) {
  (void)unused;
  size_t offset = ((a >> 16) & 127) * 32768u + (a & 32767);
  return (a & 0x8000) && offset < image_size ? image[offset] : 0xff;
}
static unsigned read_word(uint32_t a) { return read_byte(NULL,a) | read_byte(NULL,a+1) << 8; }
static uint32_t read_long(uint32_t a) { return read_word(a) | (uint32_t)read_byte(NULL,a+2) << 16; }
static void clear(void) {
  for (unsigned i = 0; i < count; ++i) { free(residents[i]); residents[i] = NULL; }
  count = 0;
}
void SmRoomPreviewSetRom(const uint8_t *rom, size_t size) {
  clear(); image = rom; image_size = size; attempted = ready = false;
}
static bool background(Preview *p, uint16_t state, uint16_t incoming) {
  uint8_t scratch[65536] = {0};
  for (unsigned i=0x9000; i<0xa000; i+=2) { p->tiles[i]=0x38; p->tiles[i+1]=3; }
  unsigned cursor = read_word(0x8f0000u+state+22);
  for (unsigned commands=0; (cursor&0x8000) && commands<256; ++commands) {
    uint32_t a=0x8f0000u+cursor; unsigned command=read_word(a); cursor+=2; a+=2;
    if (!command) return true;
    if (command==14) {
      if (read_word(a)!=incoming) { cursor+=9; continue; }
      cursor+=2; a+=2; command=2;
    }
    if (command==4) {
      unsigned destination=read_word(a+3);
      if (!SmWorldUnpack(read_byte,NULL,read_long(a),scratch+destination,sizeof(scratch)-destination)) return false;
      cursor+=5;
    } else if (command==2 || command==8) {
      uint32_t source=read_long(a);
      unsigned destination=(read_word(a+3)&0x7fff)*2, bytes=read_word(a+5);
      if (bytes>sizeof(p->tiles)-destination) return false;
      for (unsigned i=0; i<bytes; ++i) {
        uint32_t address=source+i;
        p->tiles[destination+i]=(address>>16)==0x7e ? scratch[address&65535] : read_byte(NULL,address);
      }
      cursor+=7;
    } else if (command==10 || command==12) {
      unsigned first=command==12 ? 0x8000 : 0x9000;
      for (unsigned i=first; i<0xa000; i+=2) { p->tiles[i]=0x38; p->tiles[i+1]=3; }
    } else if (command!=6) return false;
  }
  return !(cursor&0x8000);
}
static bool decode(Preview *p, const SmWorldRoom *room, uint16_t state, uint16_t incoming) {
  uint32_t s = 0x8f0000u | state;
  unsigned graphics = read_byte(NULL,s+3);
  if (graphics >= 29) return false;
  uint32_t tileset = 0x8f0000u | read_word(0x8fe7a7 + 2*graphics);
  size_t bytes = SmWorldUnpack(read_byte,NULL,read_long(s),p->level,sizeof(p->level));
  unsigned blocks = (unsigned)room->width * room->height * 256;
  if (bytes < 2) return false;
  p->level_bytes = word(p->level);
  if ((p->level_bytes & 1) || p->level_bytes < blocks*2 ||
      2 + p->level_bytes + p->level_bytes/2 > bytes) return false;
  p->scroll_x=read_byte(NULL,s+12); p->scroll_y=read_byte(NULL,s+13);
  p->level_background=!(p->scroll_x&1) && 2u+2*p->level_bytes+p->level_bytes/2<=bytes;
  /* The common room elements occupy tile definitions 0..255 and VRAM
   * bytes $5000..$7FFF; each tileset supplies its own remaining definitions. */
  if (!SmWorldUnpack(read_byte,NULL,0xb9a09d,p->definitions,2048) ||
      !SmWorldUnpack(read_byte,NULL,read_long(tileset),p->definitions+2048,6144) ||
      !SmWorldUnpack(read_byte,NULL,0xb98000,p->tiles+0x5000,0x3000) ||
      !SmWorldUnpack(read_byte,NULL,read_long(tileset+3),p->tiles,32768) ||
      !SmWorldUnpack(read_byte,NULL,read_long(tileset+6),p->palette,sizeof(p->palette))) return false;
  if (!background(p,state,incoming)) return false;
  p->room = room->header; p->state = state; p->incoming = incoming;
  p->width = room->width * 256; p->height = room->height * 256;
  for (unsigned sy=0; sy<room->height; ++sy) for (unsigned sx=0; sx<room->width; ++sx) {
    bool empty=true;
    for (unsigned by=0; empty && by<16; ++by) for (unsigned bx=0; bx<16; ++bx)
      if (word(p->level+2+2*((sy*16+by)*room->width*16+sx*16+bx))!=0x83ff) { empty=false; break; }
    p->padding[sy*16+sx]=empty;
  }
  return true;
}
static bool overlaps(int x, int y, int w, int h, int left, int top, int right, int bottom) {
  return x < right && x+w > left && y < bottom && y+h > top;
}
void SmRoomPreviewPrepare(const uint8_t ram[0x20000], int left, int top, int right, int bottom) {
  camera_x=(int16_t)word(ram+0x911); camera_y=(int16_t)word(ram+0x915);
  if (!attempted) {
    attempted = true;
    ready = image && image_size == 0x300000 && SmWorldLoad(&world,read_byte,NULL);
  }
  if (!ready) return;
  const SmWorldRoom *active = SmWorldFindRoom(&world,word(ram+0x79b));
  if (!active) { clear(); return; }
  if ((unsigned)active->width*active->height>0x3200/256) { clear(); return; }
  if (word(ram+0x7a5)!=active->width*16u || word(ram+0x7a7)!=active->height*16u) { clear(); return; }
  /* Breadth first, rooted at the actual active room. Door positions rather
   * than the automap locate rooms. Elevators/warps have no planar preview. */
  struct Placement { const SmWorldRoom *room; int x,y,left,top,right,bottom; } queue[kResidents+1];
  unsigned head = 0, total = 1;
  bool seen[SM_WORLD_ROOMS] = {false};
  seen[active-world.rooms] = true;
  /* Portal ownership is independent of the current viewport. Keep complete
   * loaded rooms available to interpolation and a live aspect change. */
  queue[0] = (struct Placement){active,0,0,-65536,-65536,65536,65536};
  Preview *old[kResidents]; unsigned old_count = count;
  memcpy(old,residents,sizeof(old)); count = 0;
  while (head < total) {
    struct Placement from = queue[head++];
    for (unsigned i = 0; i < from.room->door_count && total < kResidents+1; ++i) {
      const SmWorldDoor *d = &world.doors[from.room->first_door+i];
      const SmWorldRoom *to = SmWorldFindRoom(&world,d->destination);
      if (!to || seen[to-world.rooms] || d->block_x < 0 || (d->flags & 0x80)) continue;
      unsigned dir = d->direction & 3;
      static const int dx[] = {1,-1,0,0}, dy[] = {0,0,1,-1};
      int x = from.x + (d->block_x/16 + dx[dir] - d->arrival_x/16)*256;
      int y = from.y + (d->block_y/16 + dy[dir] - d->arrival_y/16)*256;
      /* Door alignment can overlap authored allocations (Kraid's exit is a
       * stock example). Clip previews to the far side of the connection,
       * rather than discarding the entire destination as non-planar. */
      int cl=from.left, ct=from.top, cr=from.right, cb=from.bottom;
      int plane_x=from.x+(d->block_x/16+(dir==0))*256;
      int plane_y=from.y+(d->block_y/16+(dir==2))*256;
      if (dir==0 && cl<plane_x) cl=plane_x;
      if (dir==1 && cr>plane_x) cr=plane_x;
      if (dir==2 && ct<plane_y) ct=plane_y;
      if (dir==3 && cb>plane_y) cb=plane_y;
      int vl=cl>left ? cl : left, vt=ct>top ? ct : top;
      int vr=cr<right ? cr : right, vb=cb<bottom ? cb : bottom;
      if (vl>=vr || vt>=vb || !overlaps(x,y,to->width*256,to->height*256,vl,vt,vr,vb)) continue;
      uint16_t state = SmWorldRoomStateForDoor(to,read_byte,NULL,ram,d->definition);
      Preview *p = NULL;
      for (unsigned j = 0; j < old_count; ++j)
        if (old[j] && old[j]->room == to->header && old[j]->state == state && old[j]->incoming == d->definition) {
          p = old[j]; old[j] = NULL; break;
        }
      if (!p) {
        p = calloc(1,sizeof(*p));
        if (!p) continue;
        if (!decode(p,to,state,d->definition)) { free(p); continue; }
      }
      p->left=cl; p->top=ct; p->right=cr; p->bottom=cb;
      p->x=x; p->y=y; residents[count++]=p;
      seen[to-world.rooms]=true;
      queue[total++] = (struct Placement){to,x,y,cl,ct,cr,cb};
    }
  }
  for (unsigned j = 0; j < old_count; ++j) free(old[j]);
}
unsigned SmRoomPreviewCount(void) { return count; }
bool SmRoomPreviewPlacement(unsigned index, SmRoomPlacement *out) {
  if (index>=count || !out) return false;
  const Preview *p=residents[index];
  *out=(SmRoomPlacement){.room=p->room,.state=p->state,.incoming=p->incoming,.reserved=0,
      .x=p->x,.y=p->y,.width=p->width,.height=p->height,
      .left=p->left,.top=p->top,.right=p->right,.bottom=p->bottom};
  return true;
}
const SmWorldRoom *SmRoomPreviewRoom(uint16_t header) { return ready ? SmWorldFindRoom(&world,header) : NULL; }
const uint8_t *SmRoomPreviewVram(unsigned index) { return index<count ? residents[index]->tiles : NULL; }
static unsigned tile_pixel(const Preview *p, unsigned tile, int x, int y) {
  unsigned tx=(x&7)^((tile&0x4000) ? 7 : 0), ty=(y&7)^((tile&0x8000) ? 7 : 0);
  unsigned at=(tile&1023)*32+ty*2, shift=7-tx;
  return ((p->tiles[at]>>shift)&1) | (((p->tiles[at+1]>>shift)&1)<<1) |
      (((p->tiles[at+16]>>shift)&1)<<2) | (((p->tiles[at+17]>>shift)&1)<<3);
}
static unsigned block_tile(const Preview *p, unsigned block, int x, int y) {
  unsigned qx=((x/8)&1)^((block>>10)&1), qy=((y/8)&1)^((block>>11)&1);
  return word(p->definitions+(block&1023)*8+(qy*2+qx)*2)^((block&0xc00)<<4);
}
static int parallax(int camera, unsigned rate) { return rate ? rate==1 ? 0 : camera*(int)(rate&254)/256 : camera; }
static unsigned image_tile(const SmRoomImage *p,unsigned block,int x,int y) {
  unsigned qx=((x/8)&1)^((block>>10)&1), qy=((y/8)&1)^((block>>11)&1);
  return word(p->ram+0xa000+(block&1023)*8+(qy*2+qx)*2)^((block&0xc00)<<4);
}
static unsigned image_pixel(const SmRoomImage *p,unsigned tile,int x,int y,unsigned base,unsigned bpp) {
  unsigned tx=(x&7)^((tile&0x4000) ? 7 : 0), ty=(y&7)^((tile&0x8000) ? 7 : 0);
  unsigned a=(base+(tile&1023)*bpp*8+ty*2)&65535, shift=7-tx;
  unsigned pixel=((p->vram[a]>>shift)&1) | (((p->vram[(a+1)&65535]>>shift)&1)<<1);
  if (bpp==4) pixel|=(((p->vram[(a+16)&65535]>>shift)&1)<<2) | (((p->vram[(a+17)&65535]>>shift)&1)<<3);
  return pixel;
}
bool SmRoomSceneSample(const SmRoomImage *p,int x,int y,int view_x,int view_y,uint16_t layers[3]) {
  if (!p || !layers) return false;
  const SmRoomPlacement *v=&p->placement;
  int lx=x-v->x, ly=y-v->y;
  unsigned w=word(p->ram+0x7a5),h=word(p->ram+0x7a7);
  if (!w || !h || (uint64_t)w*h>0x3200 || lx<0 || ly<0 || lx>=(int)w*16 || ly>=(int)h*16 ||
      x<v->left || x>=v->right || y<v->top || y>=v->bottom) return false;
  unsigned block=word(p->ram+0x10002+2*((ly/16)*w+lx/16));
  if (block==0x83ff) return false;
  memset(layers,0,3*sizeof(*layers));
  unsigned tile=image_tile(p,block,lx,ly), pixel=image_pixel(p,tile,lx,ly,0,4);
  if (pixel) layers[0]=(uint16_t)(((tile&0x2000 ? 12 : 8)<<12) | (((tile>>10)&7)*16+pixel));
  int cx=view_x-v->x,cy=view_y-v->y;
  int bx=lx-cx+parallax(cx,p->ram[0x91b]),by=ly-cy+parallax(cy,p->ram[0x91c]);
  bool valid=true;
  if (!(p->ram[0x91b]&1)) {
    valid=bx>=0 && by>=0 && bx<(int)w*16 && by<(int)h*16;
    if (valid) tile=image_tile(p,word(p->ram+0x19602+2*((by/16)*w+bx/16)),bx,by);
  } else {
    unsigned tx=((unsigned)bx/8)&63,ty=((unsigned)by/8)&31;
    tile=word(p->vram+0x9000+2*((tx&31)+ty*32+(tx>=32 ? 1024 : 0)));
  }
  pixel=valid ? image_pixel(p,tile,bx,by,0,4) : 0;
  if (pixel) layers[1]=(uint16_t)(((tile&0x2000 ? 11 : 7)<<12) | 0x100 | (((tile>>10)&7)*16+pixel));
  unsigned type=word(p->ram+0x196e);
  if (type==2 || type==4 || type==6) {
    int surface=(int16_t)word(p->ram+(type==6 ? 0x195e : 0x1962));
    if (surface>=0 && ly>=surface-8) {
      unsigned fy=ly<surface ? 248u+(unsigned)(ly-surface+8) : 256u+((unsigned)(ly-surface)&255);
      unsigned tx=((unsigned)lx/8)&31,ty=fy/8;
      tile=word(p->vram+0xb000+2*(tx+(ty&31)*32+(ty>=32 ? 1024 : 0)));
      pixel=image_pixel(p,tile,lx,(int)fy,(p->ram[0x5e]&15)*8192,2);
      if (pixel) layers[2]=(uint16_t)(((tile&0x2000 ? 3 : 1)<<12) | 0x200 | (((tile>>10)&7)*4+pixel));
    }
  }
  return true;
}
bool SmRoomPreviewPixel(int x, int y, uint16_t *color) {
  return SmRoomPreviewPixelAt(x,y,camera_x,camera_y,color);
}
bool SmRoomPreviewPixelAt(int x, int y, int view_x, int view_y, uint16_t *color) {
  if (!color) return false;
  for (unsigned i = 0; i < count; ++i) {
    const Preview *p = residents[i]; int lx=x-p->x, ly=y-p->y;
    if (x<p->left || x>=p->right || y<p->top || y>=p->bottom) continue;
    if (lx<0 || ly<0 || lx>=p->width || ly>=p->height) continue;
    unsigned block = word(p->level+2+2*((ly/16)*(p->width/16)+lx/16));
    /* $83FF is unallocated room padding, not visible authored scenery. */
    if (p->padding[(ly/256)*16+lx/256]) continue;
    unsigned tile=block_tile(p,block,lx,ly), pixel=tile_pixel(p,tile,lx,ly);
    int cx=view_x-p->x, cy=view_y-p->y;
    int bx=lx-cx+parallax(cx,p->scroll_x), by=ly-cy+parallax(cy,p->scroll_y);
    unsigned bt=0; bool background_valid=false;
    if (p->level_background) {
      if (bx>=0 && by>=0 && bx<p->width && by<p->height) {
        unsigned at=2+p->level_bytes+p->level_bytes/2+2*((by/16)*(p->width/16)+bx/16);
        bt=block_tile(p,word(p->level+at),bx,by);
        background_valid=true;
      }
    } else {
      /* Library BG2 uses the standard 64x32 map at VRAM word $4800. */
      unsigned tx=((unsigned)bx/8)&63, ty=((unsigned)by/8)&31;
      bt=word(p->tiles+0x9000+2*((tx&31)+ty*32+(tx>=32 ? 1024 : 0)));
      background_valid=true;
    }
    unsigned bp=background_valid ? tile_pixel(p,bt,bx,by) : 0;
    if (bp && (!pixel || (!(tile&0x2000) && (bt&0x2000)))) { tile=bt; pixel=bp; }
    *color = word(p->palette+2*(pixel ? (((tile>>10)&7)*16+pixel) : 0));
    return true;
  }
  return false;
}
