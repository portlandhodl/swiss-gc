#ifndef __CUBEBOOT_H__
#define __CUBEBOOT_H__

#include <stdbool.h>

// Passed to Swiss via boot.cli when cubeboot chainloads it after the intro
#define CUBEBOOT_INTRO_ARG "CubebootIntro=Yes"

#define CUBEBOOT_COLOR_MAX 10

extern char *cubebootColorStr[CUBEBOOT_COLOR_MAX];

bool cubeboot_intro_launched(int argc, char *argv[]);
const char *cubeboot_intro_apply(bool enable, int color);

#endif
