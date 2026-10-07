/* -----------------------------------------------------------
      scene3d.h - 3D scene helpers for the IPL style UI

      Coordinates passed to the cube helpers are in UI screen
      space (x right, y down, 0..640 x 0..480) with z pointing
      towards the viewer. z = 0 lies exactly on the 2D UI plane.
   ----------------------------------------------------------- */

#ifndef SCENE3D_H
#define SCENE3D_H

#include <gccore.h>

// IPL inspired palette
#define THEME_BG_TOP        ((GXColor) { 28,  22,  74, 255})
#define THEME_BG_MID        ((GXColor) { 14,  11,  44, 255})
#define THEME_BG_BOTTOM     ((GXColor) {  5,   4,  18, 255})
#define THEME_PANEL         ((GXColor) { 46,  38, 120, 150})
#define THEME_PANEL_DARK    ((GXColor) { 22,  18,  64, 185})
#define THEME_BORDER        ((GXColor) {186, 178, 250, 215})
#define THEME_BORDER_DIM    ((GXColor) {120, 110, 210, 190})
#define THEME_SELECT        ((GXColor) {112,  88, 240, 200})
#define THEME_ACCENT        ((GXColor) {124, 104, 255, 255})
#define THEME_PROGRESS      ((GXColor) {136, 112, 255, 225})
#define THEME_PROGRESS_IND  ((GXColor) {170, 210, 255, 225})

typedef struct {
	float x, y, z;		// centre, UI screen space
	float size;			// edge length in pixels at z = 0
	float rx, ry, rz;	// rotation in radians
	GXColor color;		// base colour, alpha is opacity
	float edges;		// 0..1 brightness of the highlighted edges
} cube3d_t;

void Scene3D_Init(void);
float Scene3D_Time(void);
float Scene3D_FrameDelta(void);
void Scene3D_NewFrame(void);

// Shared transform used by every 2D UI primitive (see drawInit/drawFontInit)
void UI_ResetTransform(void);
void UI_SetTransform(Mtx m, bool perspective);
void UI_GetTransform(Mtx m, bool *perspective);
void UI_LoadProjection(void);
void UI_ApplyTransform(Mtx local, Mtx out);
void UI_TransformPoint(float *x, float *y, float *z);

// Build a transform that rotates a flat UI element about a pivot in 3D
void UI_MakeTransform(Mtx out, float pivotX, float pivotY, float tx, float ty, float tz, float rotY, float rotX, float scale);

void Scene3D_DrawBackground(void);
void Scene3D_DrawCubes(cube3d_t *cubes, int count);
void Scene3D_DrawLogoCube(float x, float y, float size, float alpha);
void Scene3D_DrawCubeIcon(const cube3d_t *cube, GXTexObj *texObj, float aspect, float alpha);
void Scene3D_DrawGlow(GXTexObj *texObj, float x, float y, float z, float w, float h, GXColor color);
void Scene3D_DrawReflections(const cube3d_t *cubes, int count, float floorY, float strength);
GXTexObj *Scene3D_GlowTexture(void);
void Scene3D_DrawGradientRect(float x, float y, float w, float h, GXColor top, GXColor bottom);

#endif
