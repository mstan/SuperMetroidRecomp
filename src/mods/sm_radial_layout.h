/* Eight stick directions shared by weapon selection and its displayed nodes. */
#pragma once
#include <stdint.h>
#include <string.h>
static const char *const kSmRadialDirections[8] = {
  "up","up-right","right","down-right","down","down-left","left","up-left"
};
static const int kSmRadialDefaultPositions[6] = {1,0,2,4,6,7};
static const char *const kSmRadialPositionOptions[6] = {
  "beam-position","missile-position","super-position","power-bomb-position","grapple-position","xray-position"
};
static const int kSmRadialNodeX[8] = {0,32,44,32,0,-32,-44,-32};
static const int kSmRadialNodeY[8] = {-40,-30,0,30,40,30,0,-30};
static inline int SmRadialPositionIndex(const char *name) {
  for (int i=0;i<8;++i) if (!strcmp(name,kSmRadialDirections[i])) return i;
  return -1;
}
static inline int SmRadialStickDirection(int x,int y,int deadzone) {
  if ((int64_t)x*x+(int64_t)y*y < (int64_t)deadzone*deadzone) return -1;
  int up=y < -deadzone/2, down=y > deadzone/2;
  int left=x < -deadzone/2, right=x > deadzone/2;
  if (up && left) return 7;
  if (up && right) return 1;
  if (down && right) return 3;
  if (down && left) return 5;
  if (up) return 0;
  if (right) return 2;
  if (down) return 4;
  if (left) return 6;
  return -1;
}
static inline int SmRadialItemAt(const int positions[6],int x,int y,int deadzone) {
  int direction=SmRadialStickDirection(x,y,deadzone);
  if (direction<0) return -1;
  for (int item=0;item<6;++item) if (positions[item]==direction) return item;
  return -1;
}
