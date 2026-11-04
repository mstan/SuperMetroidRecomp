/* Export semantic map facts from an owned, unheadered stock cartridge. */
#include "sm_world.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct Image { unsigned char *bytes; size_t size; } Image;
static uint8_t Read(void *context, uint32_t a) {
  Image *r=context; size_t i=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && i<r->size ? r->bytes[i] : 0;
}
int main(int argc, char **argv) {
  if (argc!=3) { fprintf(stderr,"usage: sm_world_dump <owned-ROM.sfc> <output.json>\n"); return 2; }
  FILE *f=fopen(argv[1],"rb"); if (!f) return 2;
  if (fseek(f,0,SEEK_END)) { fclose(f); return 2; }
  long size=ftell(f); rewind(f);
  if (size!=0x300000) { fprintf(stderr,"expected unheadered 3 MiB stock ROM\n"); fclose(f); return 2; }
  Image image={malloc((size_t)size),(size_t)size};
  SmWorld *world=malloc(sizeof(*world));
  if (!image.bytes || !world) { fclose(f); free(world); free(image.bytes); return 2; }
  if (fread(image.bytes,1,image.size,f)!=image.size) { fclose(f); free(world); free(image.bytes); return 2; }
  fclose(f);
  if (!SmWorldLoad(world,Read,&image)) { fprintf(stderr,"%s\n",world->error); free(world); free(image.bytes); return 1; }
  f=fopen(argv[2],"w"); if (!f) { free(world); free(image.bytes); return 2; }
  fprintf(f,"{\"rooms\":[");
  for (unsigned i=0; i<world->room_count; ++i) {
    SmWorldRoom *r=&world->rooms[i];
    fprintf(f,"%s{\"header\":%u,\"area\":%u,\"x\":%u,\"y\":%u,\"width\":%u,\"height\":%u,\"default_state\":%u,\"first_door\":%u,\"door_count\":%u}",i?",":"",r->header,r->area,r->x,r->y,r->width,r->height,r->default_state,r->first_door,r->door_count);
  }
  fprintf(f,"],\"doors\":[");
  for (unsigned i=0; i<world->door_count; ++i) {
    SmWorldDoor *d=&world->doors[i];
    fprintf(f,"%s{\"definition\":%u,\"source\":%u,\"destination\":%u,\"index\":%u,\"block_x\":%d,\"block_y\":%d,\"direction\":%u,\"flags\":%u,\"arrival_x\":%u,\"arrival_y\":%u,\"reciprocal\":%s}",i?",":"",d->definition,d->source,d->destination,d->index,d->block_x,d->block_y,d->direction,d->flags,d->arrival_x,d->arrival_y,d->reciprocal?"true":"false");
  }
  fprintf(f,"],\"markers\":[");
  for (unsigned i=0; i<world->marker_count; ++i) {
    SmWorldMarker *m=&world->markers[i];
    fprintf(f,"%s{\"room\":%u,\"state\":%u,\"plm\":%u,\"argument\":%u,\"x\":%u,\"y\":%u,\"kind\":%u,\"detail\":%u,\"direction\":%u}",i?",":"",m->room,m->state,m->plm,m->argument,m->x,m->y,m->kind,m->detail,m->direction);
  }
  fprintf(f,"],\"states\":[");
  for (unsigned i=0; i<world->state_count; ++i)
    fprintf(f,"%s{\"room\":%u,\"state\":%u}",i?",":"",world->state_rooms[i],world->states[i]);
  fprintf(f,"]}\n"); int failed=fclose(f);
  printf("%u rooms, %u doors, %u states, %u markers\n",world->room_count,world->door_count,world->state_count,world->marker_count);
  free(world); free(image.bytes); return failed ? 1 : 0;
}
