#include "cpu_state.h"
#include "funcs.h"

/* Generator-only roots for the runtime-gated widescreen override injector.
 *
 * These functions may be absent from the attract coverage profile, but
 * tools/apply_widescreen_overrides.py needs their exact generated blocks.
 * tools/regen.sh scans this file; CMake does not compile it into the game. */
void SmWidescreenOverrideAotRoots(CpuState *cpu) {
  DetermineWhichEnemiesToProcess(cpu);
  CheckIfEnemyIsOnScreen(cpu);
  EnemyWithNormalSpritesIsOffScreen(cpu);
  WriteEnemyOams(cpu);
  EprojPreInstr_NorfairLavaquakeRocks_Inner2(cpu);
  EprojPreInstrHelper_SpikeShootingPlantSpikes_Func2(cpu);
  EprojPreInstrHelper_DBF2_Func2(cpu);
  sub_86DFA0(cpu);
  sub_86E0B0(cpu);
  CheckIfEprojIsOffScreen(cpu);
  sub_86EC18(cpu);
  ProjPreInstr_IceSba2(cpu);
  ProjPreInstr_SpeedEcho(cpu);
  ProjPreInstr_PlasmaSbaFunc_2(cpu);
  DeleteProjectileIfFarOffScreen(cpu);
  AtmosphericTypeFunc_1_FootstepSplash(cpu);
  SamusBottomDrawn_0_Standing(cpu);
  Samus_ArmCannon_Draw(cpu);
  GrappleBeamHandler(cpu);
  GrappleBeamFunc_FireGoToCancel(cpu);
  GrappleBeamFunc_Firing(cpu);
  GrappleBeamFunc_Cancel(cpu);
  HandleGrappleBeamGfx(cpu);
  DrawGrappleOams(cpu);
  DrawGrappleOams3(cpu);
  EnableHdmaObjects(cpu);
  CalculateXrayHdmaTable(cpu);
  SpawnPowerBombExplosion(cpu);
  CalculatePowerBombHdmaObjectTablePtrs(cpu);
  CalculatePowerBombHdmaTablePointers(cpu);
  CalculateCrystalFlashHdmaObjectTablePtrs(cpu);
  HdmaobjPreInstr_PowerBombExplode_SetWindowConf(cpu);
  HdmaobjPreInstr_PowerBombExplode_Stage5_Afterglow(cpu);
  HdmaobjPreInstr_PowerBombExplode_ExplosionYellow(cpu);
  HdmaobjPreInstr_PowerBombExplode_ExplosionWhite(cpu);
  HdmaobjPreInstr_PowerBombExplode_PreExplosionWhite(cpu);
  HdmaobjPreInstr_PowerBombExplode_PreExplosionYellow(cpu);
  HdmaobjPreInstr_RainBg3Scroll(cpu);
  HdmaobjPreInstr_SporesBG3Xscroll(cpu);
  HdmaobjPreInstr_FogBG3Scroll(cpu);
  HdmaobjPreInstr_DF94(cpu);
  sub_88E987(cpu);
}
