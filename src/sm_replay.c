/* Super Metroid diagnostic sessions, adapted from MMX's recorder (mstan).
 * The shared snesrecomp writer owns disk I/O and hashes immutable frame copies.
 * All checkpoint serialization happens on the simulation thread at a boundary.
 */
#include "sm_replay.h"
#include "sm_activity.h"
#include "sm_video.h"
#include "diagnostics/replay_writer.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "desktop/config.h"
#include "host_paths.h"
#include "host_main.h"
#include "execution_mode.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/apu.h"
#include "snes_runahead.h"
#if SNESRECOMP_ENABLE_MODS
#include "mod_runtime.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define replay_mkdir(p) _mkdir(p)
#define replay_pid() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define replay_mkdir(p) mkdir(p, 0755)
#define replay_pid() getpid()
#endif
#ifndef SNESRECOMP_BUILD_VERSION
#define SNESRECOMP_BUILD_VERSION "dev"
#endif
#ifndef SM_REPLAY_REVISION
#define SM_REPLAY_REVISION "unknown"
#endif
#ifndef SM_REPLAY_ENGINE
#define SM_REPLAY_ENGINE "unknown"
#endif
#ifndef SM_REPLAY_TARGET
#define SM_REPLAY_TARGET "unknown"
#endif

enum { SEGMENT_FRAMES=600, SEGMENTS=360, STATE_LIMIT=16*1024*1024,
       HISTORY_LIMIT=256*1024*1024, SNAPSHOT_LIMIT=2*1024*1024 };
static bool playing, finished, failed, pending, recording, loaded;
static bool awaiting_render, incomplete_render;
static SnesReplayWriter *writer;
static FILE *stream, *states;
static char stem[1200], path[1280], config_path[1200]="config.ini";
static unsigned segment, oldest=1, rows, next_frame, playback_rows;
static unsigned recorded_width=256, viewport_width=256;
static double recorded_aspect=4.0/3.0, viewport_aspect=4.0/3.0;
static uint64_t generation, load_sequence, recorded_load_sequence;
static size_t segment_bytes, history_bytes, history_sizes[SEGMENTS];
static void *snapshot;
static size_t snapshot_capacity;
static uint32_t rom_identity;
extern Snes *g_snes;
extern uint8_t g_ram[0x20000];
extern CpuState g_cpu;

static uint32_t hash(const void *data,size_t size) {
  uint32_t h=2166136261u;
  const uint8_t *p=data;
  while(size--) h=(h^*p++)*16777619u;
  return h;
}
/* Explicit integers exclude process pointers and struct padding. */
static void cpu_words(uint64_t words[16]) {
  words[0]=g_cpu.A; words[1]=g_cpu.X; words[2]=g_cpu.Y;
  words[3]=g_cpu.S; words[4]=g_cpu.D; words[5]=g_cpu.DB; words[6]=g_cpu.PB;
  words[7]=g_cpu.P; words[8]=g_cpu.m_flag; words[9]=g_cpu.x_flag;
  words[10]=g_cpu.emulation; words[11]=g_cpu.cycles;
  words[12]=g_cpu.master_cycles; words[13]=g_cpu.coprocessor_master_cycles;
  words[14]=g_cpu.open_bus; words[15]=g_cpu.host_return_valid;
}
static void close_segment(void) {
  if(recording) {
    if(!SnesReplayWriterEnd(writer)) failed=true;
    recording=false;
  }
  if(stream) { if(fclose(stream)) failed=true; stream=NULL; }
  if(states) { if(fclose(states)) failed=true; states=NULL; }
  pending=false; rows=0;
  awaiting_render=false;
}
static void error(const char *why) {
  fprintf(stderr,"[sm-replay] %s: %s\n",path,why ? why : "recording stopped");
  failed=true;
  close_segment();
  fflush(stderr);
}
void SmReplayShutdown(void) {
  close_segment();
  const char *why=SnesReplayWriterDestroy(writer); writer=NULL;
  if(why) { fprintf(stderr,"[sm-replay] %s; capture stopped\n",why); failed=true; }
  free(snapshot); snapshot=NULL; snapshot_capacity=0;
}
void SmReplayRomLoaded(const uint8_t *rom,unsigned size) { rom_identity=hash(rom,size); }
void SmReplaySetConfigPath(const char *name) {
  if(!snesrecomp_abspath(name,config_path,sizeof(config_path)))
    error("cannot resolve configuration path");
}
void SmReplayStateLoaded(void) { ++load_sequence; }
bool SmReplayPlaying(void) { return playing; }
bool SmReplayFinished(void) { return finished; }
bool SmReplayFailed(void) { return failed; }
void SmReplayViewport(SmViewport *view) {
  if(playing && loaded) {
    view->width=(int)viewport_width; view->extra=((int)viewport_width-256)/2;
    view->aspect=viewport_aspect; view->enhanced=viewport_width>256;
  }
  recorded_width=(unsigned)view->width; recorded_aspect=view->aspect;
}
static bool open_directory(const char *folder) {
  replay_mkdir(folder);
  char probe[1200];
  if(snprintf(probe,sizeof(probe),"%s/.sm-replay-%ld",folder,(long)replay_pid())>=(int)sizeof(probe))return false;
  FILE *f=fopen(probe,"wb"); if(!f)return false;
  bool ok=fclose(f)==0; remove(probe); return ok;
}
static bool init_stem(void) {
  if(stem[0])return true;
  char folder[1024], stamp[32], absolute[1024];
  const char *override=getenv("SM_REPLAY_DIR");
  if(override && *override) {
    if(snprintf(folder,sizeof(folder),"%s",override)>=(int)sizeof(folder) || !open_directory(folder))return false;
  } else if(!(snesrecomp_exe_dir_path("logs",folder,sizeof(folder)) && open_directory(folder))) {
    const char *tmp=getenv("TEMP"); if(!tmp || !*tmp)tmp=getenv("TMPDIR"); if(!tmp || !*tmp)tmp=".";
    if(snprintf(folder,sizeof(folder),"%s/SuperMetroidSNESRecomp",tmp)>=(int)sizeof(folder))return false;
    replay_mkdir(folder);
    size_t n=strlen(folder);
    if(snprintf(folder+n,sizeof(folder)-n,"/logs")>=(int)(sizeof(folder)-n) || !open_directory(folder))return false;
  }
  if(!snesrecomp_abspath(folder,absolute,sizeof(absolute)))return false;
  time_t now=time(NULL); struct tm *t=localtime(&now);
  if(!t || !strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",t))strcpy(stamp,"session");
  if(snprintf(stem,sizeof(stem),"%s/sm-replay-%s-%ld",absolute,stamp,(long)replay_pid())>=(int)sizeof(stem))return false;
  atexit(SmReplayShutdown);
  return true;
}
static size_t history_limit(void) {
  static size_t limit;
  if(!limit) {
    limit=HISTORY_LIMIT;
    const char *value=getenv("SM_REPLAY_HISTORY_MIB"); char *end;
    if(value && *value) {
      unsigned long mib=strtoul(value,&end,10);
      if(!*end && mib>=2 && mib<=256)limit=(size_t)mib*1024*1024;
    }
  }
  return limit;
}
static bool evict_segment(void) {
  static const char *suffix[]={"",".sav",".config.ini",".mods.toml",".mods.txt"};
  char old[1400];
  for(unsigned i=0;i<5;++i) {
    snprintf(old,sizeof(old),"%s-%06u.replay%s",stem,oldest,suffix[i]);
    if(!SnesReplayWriterRemove(writer,old))return false;
  }
  history_bytes-=history_sizes[oldest%SEGMENTS];
  history_sizes[oldest%SEGMENTS]=0; ++oldest;
  return true;
}
static unsigned mods_identity(void);
static bool start_segment(void) {
  close_segment();
  if(failed)return false;
  if(!init_stem()) {error("cannot create replay directory");return false;}
  if(!writer)writer=SnesReplayWriterCreate();
  if(!writer) {error("cannot start replay worker");return false;}
  ++segment;
  while(segment-oldest>=SEGMENTS)
    if(!evict_segment()) {error(SnesReplayWriterError(writer));return false;}
  segment_bytes=0;
  snprintf(path,sizeof(path),"%s-%06u.replay",stem,segment);
  char header[512];
  snprintf(header,sizeof(header),"SMREPLAY 3\nBUILD %s %s %s %s\nROM %08x\nMODS %08x\nMODE lle\n",
      SNESRECOMP_BUILD_VERSION,SM_REPLAY_REVISION,SM_REPLAY_ENGINE,SM_REPLAY_TARGET,rom_identity,
      mods_identity());
  if(!SnesReplayWriterBegin(writer,path,header,config_path,"mods/preloaded/state.toml",
      "Super Metroid presentation mods and effective renderer settings are recorded in every input record.\n")) {
    error(SnesReplayWriterError(writer));return false;
  }
  recording=true;
  if(segment==1)fprintf(stderr,"[sm-replay] recording %s (background writer; up to one hour / 256 MiB checkpoints; attach all session segments and sidecars)\n",path);
  return true;
}
static bool allocate_snapshot(size_t size) {
  if(!size || size>SNAPSHOT_LIMIT)return false;
  if(size>snapshot_capacity) {
    void *data=realloc(snapshot,size); if(!data)return false;
    snapshot=data; snapshot_capacity=size;
  }
  return true;
}
static bool write_snapshot(void) {
  size_t size=RtlSaveSnapshotToMemory(NULL,0);
  if(!allocate_snapshot(size) || size>STATE_LIMIT-segment_bytes || RtlSaveSnapshotToMemory(snapshot,size)!=size) {
    error("cannot serialize machine checkpoint");return false;
  }
  while(oldest<segment && history_bytes+size>history_limit())
    if(!evict_segment()) {error(SnesReplayWriterError(writer));return false;}
  if(!SnesReplayWriterSnapshot(writer,snapshot,size)) {error(SnesReplayWriterError(writer));return false;}
  segment_bytes+=size; history_bytes+=size; history_sizes[segment%SEGMENTS]+=size;
  return true;
}
static unsigned policy(void) {
  return g_sm_video.enhanced | g_sm_video.fps_enabled<<1 | g_sm_video.hud_anchored<<2 |
      snesrecomp_desktop_new_renderer()<<3 | g_config.no_sprite_limits<<4 | rtl_apu_extended_frame_timing()<<5;
}
static bool replay_floor_active(void) {
  /* Policy labels select registered replacements, not the SM scheduler.
   * With none registered, on/force/auto (and legacy hle) still execute the
   * same faithful guest and can use its recording format. */
  return snesrecomp_execution_publishes_floor(snesrecomp_execution_policy()) ||
      snesrecomp_execution_optimization_count() == 0;
}
/* Gameplay mods change the simulation, so a recording only replays under the
 * same effective set (presentation-only features are excluded from it). The
 * state.toml sidecar names the set; this identity enforces it. */
static unsigned mods_identity(void) {
#if SNESRECOMP_ENABLE_MODS
  char set[8192];
  int size=snes_mod_runtime_effective_set_c(set,sizeof(set));
  if(size<=0)return 0;
  if(size>=(int)sizeof(set))return 0xffffffffu; /* too large to name: matches nothing */
  return hash(set,(size_t)size);
#else
  return 0;
#endif
}
static bool load_playback(void) {
  char line[512], version[80], revision[80], engine[80], target[80], tail;
  unsigned rom, mods=0;
  bool with_mods=false;
  if(!fgets(line,sizeof(line),stream) ||
      (strcmp(line,"SMREPLAY 2\n") && !(with_mods=!strcmp(line,"SMREPLAY 3\n"))) ||
      !fgets(line,sizeof(line),stream) || sscanf(line,"BUILD %79s %79s %79s %79s %c",version,revision,engine,target,&tail)!=4 ||
      !fgets(line,sizeof(line),stream) || sscanf(line,"ROM %x %c",&rom,&tail)!=1 ||
      (with_mods && (!fgets(line,sizeof(line),stream) || sscanf(line,"MODS %x %c",&mods,&tail)!=1)) ||
      !fgets(line,sizeof(line),stream) || strcmp(line,"MODE lle\n")) {
    error("invalid or truncated replay header");return false;
  }
  if(strcmp(version,SNESRECOMP_BUILD_VERSION) || strcmp(revision,SM_REPLAY_REVISION) ||
      strcmp(engine,SM_REPLAY_ENGINE) || strcmp(target,SM_REPLAY_TARGET)) {
    error("different runtime build or target; use the recording's exact release and platform");return false;
  }
  if(!g_snes || !g_snes->cart || rom!=rom_identity) {error("different source ROM");return false;}
  if(mods!=mods_identity()) {
    error("different gameplay mods; enable the recording's set (its state.toml sidecar)");return false;
  }
  if(!replay_floor_active() || snes_runahead_active()) {
    error("replay requires faithful guest execution and run-ahead disabled");return false;
  }
  char sidecar[1400]; snprintf(sidecar,sizeof(sidecar),"%s.sav",path);
  states=fopen(sidecar,"rb");
  if(!states) {error("missing machine checkpoint sidecar");return false;}
  loaded=true; return true;
}
bool SmReplayPlayback(const char *name) {
  SmReplayShutdown(); playing=true; finished=failed=loaded=false;
  if(!name || !snesrecomp_abspath(name,path,sizeof(path))) {error("invalid replay path");return false;}
  stream=fopen(path,"r");
  if(!stream) {error("cannot open replay");return false;}
  return true;
}
static bool playback_begin(uint32_t *inputs) {
  if(!loaded && !load_playback())return false;
  char line[256],tail;
  if(!fgets(line,sizeof(line),stream)) {
    if(ferror(stream))error("input read failed");
    else if(!playback_rows)error("replay has no completed frames");
    else {finished=true;close_segment();fprintf(stderr,"[sm-replay] all recorded checkpoints matched (%u executions)\n",playback_rows);}
    return false;
  }
  if(line[0]=='S') {
    unsigned offset,size,digest;
    if(sscanf(line,"S %u %u %x %c",&offset,&size,&digest,&tail)!=3 || offset>STATE_LIMIT ||
        size>STATE_LIMIT-offset || !allocate_snapshot(size) || fseek(states,(long)offset,SEEK_SET) ||
        fread(snapshot,1,size,states)!=size || hash(snapshot,size)!=digest ||
        !RtlLoadSnapshotFromMemory(snapshot,size) || !fgets(line,sizeof(line),stream)) {
      error("missing, damaged or incompatible machine checkpoint");return false;
    }
    next_frame=(unsigned)snes_frame_counter;
  } else if(!playback_rows) {error("first input has no machine checkpoint");return false;}
  unsigned frame, word, flags, aspect, fps, extra;
  if(sscanf(line,"I %u %x %x %u %u %u %c",&frame,&word,&flags,&aspect,&fps,&extra,&tail)!=6 ||
      flags>63 || aspect>=SM_ASPECT_COUNT || !SmValidFps(fps) || !SmActivityExtraValid(extra) || frame!=next_frame) {
    error("invalid input/settings record or discontinuity without checkpoint");return false;
  }
  g_sm_video=(SmVideoSettings){(flags&1)!=0,(SmAspect)aspect,fps,(flags&2)!=0,(flags&4)!=0};
  SmActivityRestore(extra);
  snesrecomp_desktop_set_new_renderer((flags&8)!=0); g_config.no_sprite_limits=(flags&16)!=0;
  if(flags&32)RtlEnableExtendedFrameTiming();
  if(!!(flags&32)!=rtl_apu_extended_frame_timing()) {error("different frame timing mode");return false;}
  *inputs=word; next_frame=frame+1; pending=true;
  return true;
}
bool SmReplayBegin(uint32_t *inputs) {
  if(playing)return !finished && !failed && playback_begin(inputs);
  static int capture=-1;
  if(capture<0) {const char *v=getenv("SM_REPLAY_RECORD");capture=!v || *v!='0';}
  if(!capture || failed)return true;
  const char *why=SnesReplayWriterError(writer);
  if(why) {error(why);return true;}
  if(snes_runahead_active() || RtlSpeculativeFrame() ||
      !replay_floor_active()) {
    static bool warned;
    if(!warned)fprintf(stderr,"[sm-replay] capture suspended: requires faithful guest execution and run-ahead disabled\n");
    warned=true; close_segment();return true;
  }
  unsigned frame=(unsigned)snes_frame_counter;
  bool discontinuity=frame!=next_frame || generation!=RtlStateGeneration() || recorded_load_sequence!=load_sequence;
  size_t limit=history_limit()<STATE_LIMIT ? history_limit():STATE_LIMIT;
  if(!recording || rows>=SEGMENT_FRAMES || (discontinuity && segment_bytes>limit-SNAPSHOT_LIMIT))
    if(!start_segment())return true;
  if((!rows || discontinuity) && !write_snapshot())return true;
  generation=RtlStateGeneration(); recorded_load_sequence=load_sequence;
  char input[160];
  snprintf(input,sizeof(input),"I %u %08x %02x %u %u %u\n",frame,*inputs,policy(),
      (unsigned)g_sm_video.aspect,g_sm_video.fps,SmActivityExtra());
  if(!SnesReplayWriterLine(writer,input)) {error(SnesReplayWriterError(writer));return true;}
  next_frame=frame+1; pending=true;
  return true; /* Optional diagnostics never stop gameplay. */
}
void SmReplayEnd(void) {
  if(!pending || failed || finished)return;
  pending=false;
  uint64_t cpu[16]; cpu_words(cpu);
  if(playing) {
    uint32_t actual[4]={hash(g_ram,0x20000),hash(cpu,sizeof(cpu)),hash(g_snes->apu->ram,sizeof(g_snes->apu->ram)),hash(g_ppu->vram,sizeof(g_ppu->vram))};
    unsigned frame, expected[4]; char line[256],tail;
    if(!fgets(line,sizeof(line),stream) || sscanf(line,"H %u %x %x %x %x %c",&frame,&expected[0],&expected[1],&expected[2],&expected[3],&tail)!=5 || frame!=next_frame) {
      error("incomplete checkpoint after input (the original frame may have hung/crashed)");return;
    }
    if((unsigned)snes_frame_counter!=next_frame || memcmp(actual,expected,sizeof(actual))) {
      fprintf(stderr,"[sm-replay] FIRST DIVERGENCE execution=%u frame=%u WRAM=%08x/%08x CPU=%08x/%08x APU=%08x/%08x VRAM=%08x/%08x\n",
          playback_rows,next_frame-1,actual[0],expected[0],actual[1],expected[1],actual[2],expected[2],actual[3],expected[3]);
      if(RtlSaveSnapshot("replay-divergence.sav"))fprintf(stderr,"[sm-replay] saved replay-divergence.sav\n");
      error("checkpoint mismatch");return;
    }
    /* View geometry is only known after the recording host queries its window
     * for this raster. Read it ahead so playback uses the exact same Fit width
     * even on the first frame or immediately after a resize. */
    unsigned width; double ratio;
    incomplete_render=false;
    if(!fgets(line,sizeof(line),stream)) {
      incomplete_render=true;
    } else if(sscanf(line,"V %u %u %lf %c",&frame,&width,&ratio,&tail)!=3 || frame!=next_frame ||
        width<256 || width>SM_MAX_WIDTH || (width&1) || !isfinite(ratio) || ratio<1 || ratio>4) {
      error("invalid raster geometry record");return;
    } else { viewport_width=width; viewport_aspect=ratio; }
    awaiting_render=true;
  } else {
    if(!SnesReplayWriterFrame(writer,(unsigned)snes_frame_counter,g_ram,0x20000,cpu,sizeof(cpu),
        g_snes->apu->ram,sizeof(g_snes->apu->ram),g_ppu->vram,sizeof(g_ppu->vram)))
      error(SnesReplayWriterError(writer));
    ++rows;
    awaiting_render=!failed;
  }
}
void SmReplayRendered(void) {
  if(!awaiting_render || failed || finished)return;
  awaiting_render=false;
  if(playing) {
    if(snesrecomp_desktop_new_renderer()!=g_config.new_renderer) {
      error("active renderer differs from the recording");return;
    }
    if(incomplete_render)error("incomplete raster record (the original raster may have hung/crashed)");
    else ++playback_rows;
  } else {
    char view[96];
    snprintf(view,sizeof(view),"V %u %u %.17g\n",(unsigned)snes_frame_counter,recorded_width,recorded_aspect);
    if(!SnesReplayWriterLine(writer,view))error(SnesReplayWriterError(writer));
  }
}
