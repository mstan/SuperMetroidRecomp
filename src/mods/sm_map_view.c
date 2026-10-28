#include "sm_map_view.h"
#include "snes_overlay_draw.h"

static uint16_t W(const uint8_t *p, unsigned a) { return (uint16_t)(p[a] | p[a+1]<<8); }
static int Min(int a,int b) { return a<b?a:b; }
static int Max(int a,int b) { return a>b?a:b; }
static uint32_t Dim(uint32_t c, unsigned scale) {
  return 0xff000000u | (((c>>16)&255)*scale/255)<<16 | (((c>>8)&255)*scale/255)<<8 | ((c&255)*scale/255);
}
uint32_t SmMapAreaColor(unsigned area) {
  static const uint32_t colors[]={0xff996138,0xff499725,0xffa43e3e,0xff687c88,0xff2f65b9,0xff9b4386};
  return area<SM_WORLD_AREAS ? colors[area] : 0xff64717d;
}
const char *SmMapAreaName(unsigned area) {
  static const char *names[]={"CRATERIA","BRINSTAR","NORFAIR","WRECKED SHIP","MARIDIA","TOURIAN"};
  return area<SM_WORLD_AREAS ? names[area] : "ZEBES";
}
typedef struct Canvas { uint32_t *p; int stride,height; SmMapView v; } Canvas;
static void Rect(Canvas c,int x,int y,int w,int h,uint32_t color) {
  int right=Min(x+w,c.v.x+c.v.width),bottom=Min(y+h,c.v.y+c.v.height);
  x=Max(x,c.v.x); y=Max(y,c.v.y);
  if (right>x && bottom>y) snes_ovl_fill_rect(c.p,c.stride,c.height,x,y,right-x,bottom-y,color);
}
static int X(Canvas c,int x16) { return c.v.x+c.v.width/2+(x16-c.v.center_x16)*c.v.cell/16; }
static int Y(Canvas c,int y16) { return c.v.y+c.v.height/2+(y16-c.v.center_y16)*c.v.cell/16; }
static void Arrow(Canvas c,int x,int y,int dx,int dy,uint32_t color) {
  int s=c.v.cell>=8 ? 3 : 2;
  for (int i=0; i<=s; ++i) {
    Rect(c,x-dx*i,y-dy*i,1,1,color);
    if (i) {
      Rect(c,x-dx*i+dy*i,y-dy*i-dx*i,1,1,color);
      Rect(c,x-dx*i-dy*i,y-dy*i+dx*i,1,1,color);
    }
  }
}
static void Ring(Canvas c,int x,int y,int radius,uint32_t color) {
  for (int i=-radius; i<=radius; ++i) {
    int a=i<0?-i:i;
    Rect(c,x+i,y-radius+a,1,1,color); Rect(c,x+i,y+radius-a,1,1,color);
  }
}
static bool Known(Canvas c,const SmWorld *world,const uint8_t *ram,unsigned area,int x,int y) {
  return SmWorldKnown(world,ram,area,x,y,c.v.reveal,c.v.secrets);
}
static void Cell(Canvas c,const SmWorld *world,const SmWorldRoom *room,const uint8_t *ram,
                 int mx,int my,int gx,int gy) {
  if (!Known(c,world,ram,room->area,mx,my)) return;
  int x=X(c,gx*16),y=Y(c,gy*16),s=c.v.cell;
  if (x+s<c.v.x || x>=c.v.x+c.v.width || y+s<c.v.y || y>=c.v.y+c.v.height) return;
  uint32_t color=SmMapAreaColor(room->area);
  if (!SmWorldExplored(ram,room->area,mx,my)) color=Dim(color,150);
  Rect(c,x,y,s,s,color);
  /* The fine, low-contrast screen grid stays distinct from room walls. */
  if (s>=6) { Rect(c,x,y,1,1,Dim(color,170)); Rect(c,x+s/2,y+s/2,1,1,Dim(color,205)); }
  const int dx[]={-1,1,0,0},dy[]={0,0,-1,1};
  for (unsigned side=0; side<4; ++side) {
    const SmWorldRoom *neighbor=SmWorldCellRoom(world,room->area,mx+dx[side],my+dy[side]);
    if (neighbor==room && Known(c,world,ram,room->area,mx+dx[side],my+dy[side])) continue;
    if (side<2) Rect(c,x+(side?s-1:0),y,1,s,0xffbcd1e4);
    else Rect(c,x,y+(side==3?s-1:0),s,1,0xffbcd1e4);
  }
}
static void Base(const SmWorld *world,const int16_t layout[SM_WORLD_ROOMS][2],const SmWorldRoom *room,
                  bool whole,int *x,int *y) {
  unsigned n=(unsigned)(room-world->rooms);
  *x=whole ? layout[n][0] : room->x; *y=whole ? layout[n][1] : room->y;
}
void SmMapPlayerPoint(const SmWorld *world,const int16_t layout[SM_WORLD_ROOMS][2],
                       const uint8_t *ram,bool whole,int *x16,int *y16) {
  const SmWorldRoom *room=SmWorldFindRoom(world,W(ram,0x79b));
  int x=0,y=0; if (room) Base(world,layout,room,whole,&x,&y);
  *x16=x*16+W(ram,0xaf6)/16; *y16=y*16+W(ram,0xafa)/16;
}
static uint32_t CapColor(unsigned cap) {
  static const uint32_t colors[]={0xff61d5ff,0xffff65b8,0xff7dff50,0xffffe64c,0xffd5d8de,0xffffa14f};
  return cap<sizeof(colors)/sizeof(colors[0]) ? colors[cap] : colors[0];
}
static void Connectors(Canvas c,const SmWorld *world,const int16_t layout[SM_WORLD_ROOMS][2],const uint8_t *ram) {
  for (unsigned i=0; i<world->door_count; ++i) {
    const SmWorldDoor *d=&world->doors[i];
    const SmWorldRoom *from=SmWorldFindRoom(world,d->source),*to=SmWorldFindRoom(world,d->destination);
    if (!from || !to || d->block_x<0 || (d->reciprocal && d->source>d->destination)) continue;
    int ax=(d->flags&0x80) ? d->arrival_screen_x : d->arrival_x/16;
    int ay=(d->flags&0x80) ? d->arrival_screen_y : d->arrival_y/16;
    if (!Known(c,world,ram,from->area,from->x+d->block_x/16,from->y+d->block_y/16) ||
        !Known(c,world,ram,to->area,to->x+ax,to->y+ay)) continue;
    unsigned f=(unsigned)(from-world->rooms),t=(unsigned)(to-world->rooms);
    int x=X(c,layout[f][0]*16+d->block_x), y=Y(c,layout[f][1]*16+d->block_y);
    int tx=X(c,(layout[t][0]+ax)*16+8), ty=Y(c,(layout[t][1]+ay)*16+8);
    int dx=tx>x?tx-x:x-tx,dy=ty>y?ty-y:y-ty,sx=x<tx?1:-1,sy=y<ty?1:-1,error=dx-dy;
    for (unsigned step=0; step<2048; ++step) {
      if (!(step&1)) Rect(c,x,y,1,1,0xff76879c);
      if (x==tx && y==ty) break;
      int twice=2*error;
      if (twice>-dy) { error-=dy; x+=sx; }
      if (twice<dx) { error+=dx; y+=sy; }
    }
  }
}
static void Doors(Canvas c,const SmWorld *world,const int16_t layout[SM_WORLD_ROOMS][2],
                    const SmWorldRoom *room,uint16_t state,const uint8_t *ram) {
  int bx,by; Base(world,layout,room,c.v.area<0,&bx,&by);
  for (unsigned i=0; i<room->door_count; ++i) {
    const SmWorldDoor *door=&world->doors[room->first_door+i];
    if (door->block_x<0 || !door->destination) continue;
    int mx=room->x+door->block_x/16,my=room->y+door->block_y/16;
    if (!Known(c,world,ram,room->area,mx,my)) continue;
    unsigned direction=door->direction&3,cap=SM_CAP_BLUE;
    bool elevator=(door->flags&0x80)!=0;
    for (unsigned j=0; j<world->marker_count; ++j) {
      const SmWorldMarker *m=&world->markers[j];
      if (m->kind!=SM_MAP_CAP || m->room!=room->header || m->state!=state) continue;
      int dx=(int)m->x-door->block_x,dy=(int)m->y-door->block_y;
      if (dx>=-3 && dx<=3 && dy>=-3 && dy<=3) { cap=m->detail; break; }
    }
    int x=X(c,(bx+door->block_x/16)*16+8),y=Y(c,(by+door->block_y/16)*16+8);
    int half=Max(1,c.v.cell/4),s=c.v.cell;
    if (elevator) {
      Arrow(c,x,y,direction==0?1:direction==1?-1:0,direction==2?1:direction==3?-1:0,0xffffe64c);
    } else if (direction<2) {
      x+=direction==0 ? s/2 : -s/2;
      Rect(c,x-1,y-half,2,2*half,0xff080d18); Rect(c,x,y-half,1,2*half,CapColor(cap));
    } else {
      y+=direction==2 ? s/2 : -s/2;
      Rect(c,x-half,y-1,2*half,2,0xff080d18); Rect(c,x-half,y,2*half,1,CapColor(cap));
    }
    if (!elevator && s>=8) {
      if (cap==SM_CAP_SUPER) { Rect(c,x-1,y-1,1,1,0xffffffff); Rect(c,x+1,y+1,1,1,0xffffffff); }
      else if (cap==SM_CAP_POWER_BOMB) Ring(c,x,y,2,CapColor(cap));
      else if (cap==SM_CAP_MISSILE) Rect(c,x,y,1,1,0xffffffff);
      else if (cap==SM_CAP_EYE) { Ring(c,x,y,1,0xffffffff); Rect(c,x,y,1,1,CapColor(cap)); }
    }
    if (!door->reciprocal && !elevator && s>=6)
      Arrow(c,x,y,direction==0?1:direction==1?-1:0,direction==2?1:direction==3?-1:0,0xffffffff);
  }
}
static void Markers(Canvas c,const SmWorld *world,const int16_t layout[SM_WORLD_ROOMS][2],
                      const SmWorldRoom *room,uint16_t state,const uint8_t *ram) {
  int bx,by; Base(world,layout,room,c.v.area<0,&bx,&by);
  for (unsigned i=0; i<world->marker_count; ++i) {
    const SmWorldMarker *m=&world->markers[i];
    if (m->room!=room->header || m->state!=state || m->kind==SM_MAP_CAP) continue;
    int mx=room->x+m->x/16,my=room->y+m->y/16;
    /* Station knowledge shows corridors, not undiscovered item contents. */
    if (!Known(c,world,ram,room->area,mx,my) || (!c.v.reveal && !SmWorldExplored(ram,room->area,mx,my))) continue;
    int x=X(c,bx*16+m->x+1),y=Y(c,by*16+m->y+1),s=c.v.cell;
    if (m->kind==SM_MAP_ITEM) {
      if (SmWorldItemCollected(ram,m)) Rect(c,x,y,1,1,0xffbdc5d2);
      else { Ring(c,x,y,s>=8?2:1,0xffffe64c); Rect(c,x,y,1,1,0xffffe64c); }
    } else if (m->kind==SM_MAP_GATE) {
      Rect(c,x,y,1,Max(2,s/2),CapColor(m->detail));
      Arrow(c,x+(m->direction?-2:2),y+Max(1,s/4),m->direction?-1:1,0,0xffffffff);
    } else if (m->kind==SM_MAP_DROP) {
      Arrow(c,x,y,0,1,0xffffffff);
    } else if (m->kind<=SM_MAP_AMMO) {
      const char symbols[]={'?','S','M','E','R'};
      if (s>=8 && x-3>=c.v.x && y-3>=c.v.y && x+5<c.v.x+c.v.width && y+5<c.v.y+c.v.height)
        snes_ovl_draw_char(c.p,c.stride,c.height,x-3,y-3,symbols[m->kind],0xffffee80,1);
      else Rect(c,x,y,2,2,0xffffee80);
    }
  }
}
void SmMapViewDraw(uint32_t *pixels,int stride,int height,const SmWorld *world,
                    const int16_t layout[SM_WORLD_ROOMS][2],SmWorldRead read,
                    void *context,const uint8_t *ram,SmMapView view) {
  if (!pixels || !world || !ram || !read || view.cell<2 || view.width<1 || view.height<1) return;
  Canvas c={pixels,stride,height,view};
  for (int y=Max(0,view.y); y<Min(height,view.y+view.height); ++y)
    for (int x=Max(0,view.x); x<Min(stride,view.x+view.width); ++x) {
      uint32_t color=0xff080f1e;
      if (view.transparent) {
        uint32_t old=pixels[y*stride+x];
        color=0xff000000u | (((old>>16)&255)*2/5)<<16 | (((old>>8)&255)*2/5)<<8 | ((old&255)*2/5);
      }
      pixels[y*stride+x]=color;
    }
  for (int y=view.y; y<view.y+view.height; y+=2)
    for (int x=view.x; x<view.x+view.width; x+=2) Rect(c,x,y,1,1,0xff142439);
  if (view.area<0) Connectors(c,world,layout,ram);
  for (unsigned i=0; i<world->room_count; ++i) {
    const SmWorldRoom *room=&world->rooms[i];
    if (view.area>=0 && room->area!=(unsigned)view.area) continue;
    int bx,by; Base(world,layout,room,view.area<0,&bx,&by);
    for (int y=0; y<room->height; ++y) for (int x=0; x<room->width; ++x) {
      if (SmWorldCellRoom(world,room->area,room->x+x,room->y+y)!=room) continue;
      Cell(c,world,room,ram,room->x+x,room->y+y,bx+x,by+y);
    }
  }
  for (unsigned i=0; i<world->room_count; ++i) {
    const SmWorldRoom *room=&world->rooms[i];
    if (view.area>=0 && room->area!=(unsigned)view.area) continue;
    uint16_t state=SmWorldRoomState(room,read,context,ram);
    Doors(c,world,layout,room,state,ram); Markers(c,world,layout,room,state,ram);
  }
  int px,py; SmMapPlayerPoint(world,layout,ram,view.area<0,&px,&py);
  if (view.area<0 || W(ram,0x79f)==(unsigned)view.area) {
    int x=X(c,px),y=Y(c,py); Ring(c,x,y,view.cell>=8?3:2,0xffffffff); Rect(c,x,y,1,1,0xffff5050);
  }
}
