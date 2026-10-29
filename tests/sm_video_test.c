#include "sm_mods.h"
#include "host_clock.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void geometry(void) {
  SmVideoSettings s;
  SmVideoDefaults(&s);
  assert(!s.enhanced && !s.fps_enabled && s.hud_anchored && s.aspect == SM_ASPECT_FIT);
  assert(SmCalculateViewport(&s, 5120, 1440).width == 256);
  s.enhanced = true;
  const int widths[] = {256,342,448,682};
  for (int i = 0; i < 4; ++i) {
    s.aspect = (SmAspect)i;
    SmViewport v = SmCalculateViewport(&s, 800, 600);
    assert(v.width == widths[i] && v.width == 256 + 2*v.extra);
    assert(SmHudAnchorX(v, 16, -1) == 16);
    assert(SmHudAnchorX(v, 240, 1) == v.width - 16);
    assert(SmHudAnchorX(v, 128, 0) == v.width / 2);
  }
  s.aspect = SM_ASPECT_FIT;
  assert(SmCalculateViewport(&s, 1920,1080).width == 342);
  assert(SmCalculateViewport(&s, 3440,1440).width == 458);
  assert(SmCalculateViewport(&s, 5120,1440).width == 682);
  assert(SmCalculateViewport(&s, INT_MAX,1).width == 682);
  assert(SmCalculateViewport(&s, 1,INT_MAX).width == 256);
  assert(SmCalculateViewport(&s, 0,0).width == 256);
  s.aspect = SM_ASPECT_16_9;
  SmRect r = SmDestination(SmCalculateViewport(&s, 800,600),800,600);
  assert(r.x == 0 && r.y == 75 && r.w == 800 && r.h == 450);
}

static void clock_invariance(void) {
  /* The framework clock, parameterized by this title's simulation rate. The
   * cases are the ones SmClock carried before it moved up. */
  const unsigned rates[] = {60,90,120,144,165,240,360};
  for (unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);++r) {
    SnesHostClock clock;
    snes_host_clock_reset(&clock,0,SM_SIMULATION_HZ,rates[r]);
    for (int ms=0;ms<=10000;++ms) {
      double now=ms/1000.0;
      while (snes_host_clock_simulation_due(&clock,now)) snes_host_clock_simulation_done(&clock,now,false,1);
      if (snes_host_clock_presentation_due(&clock,now)) snes_host_clock_presentation_done(&clock,now);
      double alpha=snes_host_clock_alpha(&clock,now);
      assert(alpha >= 0 && alpha <= 1);
    }
    assert(clock.simulation_frames == 601);
    assert(clock.presentations >= rates[r]*10-1 && clock.presentations <= rates[r]*10+1);
  }
  SnesHostClock stalled;
  snes_host_clock_reset(&stalled,0,SM_SIMULATION_HZ,144);
  snes_host_clock_presentation_done(&stalled,5);
  assert(stalled.simulation_frames == 0 && snes_host_clock_simulation_due(&stalled,5));
  while(snes_host_clock_simulation_due(&stalled,5)) snes_host_clock_simulation_done(&stalled,5,false,1);
  assert(stalled.simulation_frames == 1); /* Realtime play must discard stalled wall-time debt. */
  SnesHostClock loading;
  snes_host_clock_reset(&loading,0,SM_SIMULATION_HZ,144);
  while(snes_host_clock_simulation_due(&loading,5)) snes_host_clock_simulation_done(&loading,5,true,1);
  assert(loading.simulation_frames == 301); /* Door loading deliberately repays its audio debt. */
  /* A loader that executed 18 hardware periods already generated their
   * audio. Waiting those periods must not trigger 17 extra game iterations. */
  SnesHostClock extended;
  snes_host_clock_reset(&extended,0,SM_SIMULATION_HZ,165);
  snes_host_clock_simulation_done(&extended,0.23,true,18);
  assert(fabs(extended.next_simulation - 18 / SM_SIMULATION_HZ) < 1e-9);
  assert(!snes_host_clock_simulation_due(&extended,0.29));
  snes_host_clock_simulation_done(&extended,18 / SM_SIMULATION_HZ,false,1);
  assert(fabs(extended.next_simulation - 19 / SM_SIMULATION_HZ) < 1e-9);
  assert(SmPresentationHz(0,165) == 165);
  assert(SmPresentationHz(0,1000) == 360);
  assert(SmPresentationHz(0,NAN) == 60);
}

static void mods_and_persistence(const char *path) {
  SmVideoSettings s, loaded;
  SmVideoDefaults(&s);
  const RecompLauncherCModProvider *p=SmModsProvider(&s,path);
  assert(p->feature_count(p->ctx) == 2);
  assert(p->package_count(p->ctx) == 2);
  assert(!strcmp(p->archive_extension, ".snesmod"));
  for (int index = 0; index < 2; ++index) {
    RecompLauncherCModPackage package;
    RecompLauncherCModFeature feature;
    assert(p->package_get(p->ctx, index, &package));
    assert(p->feature_get(p->ctx, index, &feature));
    assert(!strcmp(package.id, feature.package_id));
    assert(feature.author[0] && feature.source_name[0]);
    assert(!strcmp(feature.source_url, "https://github.com/mstan/SuperMetroidRecomp"));
    assert(!strcmp(package.source_url, feature.source_url));
    assert(!package.enabled && !feature.enabled);
    assert(feature.option_count == (index ? 1 : 2));
    for (int option_index = 0; option_index < feature.option_count; ++option_index) {
      RecompLauncherCModOption option;
      assert(p->feature_option_get(p->ctx, package.id, feature.id, option_index, &option));
      assert(option.type == RECOMP_MOD_OPTION_CHOICE);
      bool found_default = false;
      for (int choice_index = 0; choice_index < option.choice_count; ++choice_index) {
        RecompLauncherCModChoice choice;
        assert(p->feature_choice_get(p->ctx, package.id, feature.id, option.id, choice_index, &choice));
        if (!strcmp(choice.value, option.default_value)) found_default = true;
        assert(p->feature_set_option(p->ctx, package.id, feature.id, option.id, choice.value));
        RecompLauncherCModOption selected;
        assert(p->feature_option_get(p->ctx, package.id, feature.id, option_index, &selected));
        assert(!strcmp(selected.value, choice.value));
        assert(!s.enhanced && !s.fps_enabled);
      }
      assert(found_default);
      RecompLauncherCModChoice invalid;
      assert(!p->feature_choice_get(p->ctx, package.id, feature.id, option.id, option.choice_count, &invalid));
      assert(p->feature_set_option(p->ctx, package.id, feature.id, option.id, option.default_value));
    }
    RecompLauncherCModOption invalid;
    assert(!p->feature_option_get(p->ctx, package.id, feature.id, feature.option_count, &invalid));
  }
  assert(p->feature_set_option(p->ctx,"sm-widescreen","widescreen","aspect","32:9"));
  assert(!s.enhanced); /* Editing a disabled mod must not activate it. */
  assert(!p->feature_set_option(p->ctx,"sm-widescreen","widescreen","aspect","garbage"));
  assert(!p->feature_enable(p->ctx,"wrong","widescreen",1));
  assert(p->feature_enable(p->ctx,"sm-widescreen","widescreen",1));
  assert(p->feature_set_option(p->ctx,"sm-widescreen","widescreen","hud","Center"));
  assert(p->feature_enable(p->ctx,"sm-presentation-fps","presentation-fps",1));
  assert(p->feature_set_option(p->ctx,"sm-presentation-fps","presentation-fps","fps","165"));
  assert(!p->feature_set_option(p->ctx,"sm-presentation-fps","presentation-fps","fps","165junk"));
  assert(!p->feature_set_option(p->ctx,"sm-presentation-fps","presentation-fps","fps","100"));
  assert(p->commit(p->ctx,NULL));
  assert(SmVideoLoad(&loaded,path));
  assert(loaded.enhanced && loaded.fps_enabled && loaded.fps==165);
  assert(loaded.aspect==SM_ASPECT_32_9 && !loaded.hud_anchored);
  assert(p->feature_enable(p->ctx,"sm-widescreen","widescreen",0));
  assert(s.fps_enabled && s.aspect==SM_ASPECT_32_9);
  assert(p->feature_enable(p->ctx,"sm-presentation-fps","presentation-fps",0));
  assert(!s.enhanced && !s.fps_enabled);
  /* A failed commit reports an error without damaging the saved settings.
   * The valid config is a file, so it cannot also be a parent directory. */
  char bad_path[1024];
  assert(snprintf(bad_path,sizeof(bad_path),"%s/invalid-child.ini",path) < (int)sizeof(bad_path));
  p=SmModsProvider(&s,bad_path);
  assert(!p->commit(p->ctx,NULL));
  assert(p->last_error(p->ctx) && p->last_error(p->ctx)[0]);
  assert(SmVideoLoad(&loaded,path));
  assert(loaded.enhanced && loaded.fps_enabled && loaded.fps==165);
  p=SmModsProvider(&s,path);
  assert(p->commit(p->ctx,NULL));
  assert(!p->last_error(p->ctx)[0]);
  assert(SmVideoLoad(&loaded,path));
  assert(!loaded.enhanced && !loaded.fps_enabled);
  assert(remove(path)==0);
}
int main(int argc,char **argv) {
  assert(argc==2);
  geometry(); clock_invariance(); mods_and_persistence(argv[1]);
  puts("Super Metroid video geometry, cadence and Mods checks passed.");
  return 0;
}
