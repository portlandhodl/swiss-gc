/* -----------------------------------------------------------
      scene3d.c - 3D scene helpers for the IPL style UI

      Everything here is drawn with GX directly. Callers are
      expected to call drawInit() afterwards to get back to the
      regular 2D UI state.
   ----------------------------------------------------------- */

#include <math.h>
#include <string.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include "scene3d.h"

#define CAMERA_DIST 600.0f
#define VTXFMT_LIT  GX_VTXFMT3
#define VTXFMT_TEX  GX_VTXFMT4

static Mtx uiXform;
static bool uiPerspective;
static bool initialised;

static u64 startTick;
static u64 lastTick;
static float frameDelta = 1.0f / 60.0f;

#define BG_CUBES 18
static struct {
	float x, y, z, size;
	float rx, ry, rz;
	float vrx, vry, vrz;
	float drift;
	GXColor color;
} bgCubes[BG_CUBES];

static const GXColor bgPalette[] = {
	{ 96,  72, 230, 0},
	{ 64,  92, 240, 0},
	{130,  96, 255, 0},
	{ 72,  60, 180, 0},
	{ 46, 120, 220, 0},
};

static u32 rngState = 0x5715EC0B;
static float frand(void)
{
	rngState = rngState * 1664525 + 1013904223;
	return (float)(rngState >> 8) / 16777216.0f;
}

void Scene3D_Init(void)
{
	if (initialised) return;
	initialised = true;
	guMtxIdentity(uiXform);
	uiPerspective = false;
	startTick = lastTick = gettime();

	for (int i = 0; i < BG_CUBES; i++) {
		bgCubes[i].z    = -450.0f - frand() * 1300.0f;
		float depthScale = (CAMERA_DIST - bgCubes[i].z) / CAMERA_DIST;
		bgCubes[i].x    = 320.0f + (frand() - 0.5f) * 760.0f * depthScale;
		bgCubes[i].y    = 240.0f + (frand() - 0.5f) * 560.0f * depthScale;
		bgCubes[i].size = 50.0f + frand() * 110.0f;
		bgCubes[i].rx   = frand() * 6.28f;
		bgCubes[i].ry   = frand() * 6.28f;
		bgCubes[i].rz   = frand() * 6.28f;
		bgCubes[i].vrx  = (frand() - 0.5f) * 0.5f;
		bgCubes[i].vry  = (frand() - 0.5f) * 0.6f;
		bgCubes[i].vrz  = (frand() - 0.5f) * 0.3f;
		bgCubes[i].drift = 4.0f + frand() * 10.0f;
		bgCubes[i].color = bgPalette[i % (sizeof(bgPalette) / sizeof(bgPalette[0]))];
		bgCubes[i].color.a = 40 + (u8)(frand() * 60.0f);
	}
}

float Scene3D_Time(void)
{
	return (float)ticks_to_microsecs(gettime() - startTick) / 1000000.0f;
}

float Scene3D_FrameDelta(void)
{
	return frameDelta;
}

void Scene3D_NewFrame(void)
{
	u64 now = gettime();
	frameDelta = (float)ticks_to_microsecs(now - lastTick) / 1000000.0f;
	if (frameDelta > 0.1f) frameDelta = 0.1f;
	lastTick = now;
}

/* ---------------------------------------------------------------------------
 * UI transform shared by every 2D primitive
 * ------------------------------------------------------------------------- */

// Maps UI screen space (y down, z towards viewer) to camera space (y up)
static void screenView(Mtx v)
{
	guMtxIdentity(v);
	v[0][3] = -320.0f;
	v[1][1] = -1.0f;
	v[1][3] = 240.0f;
	v[2][3] = -CAMERA_DIST;
}

static void loadPerspective(void)
{
	Mtx44 proj;
	// Field of view chosen so that z = 0 maps 1:1 onto the 640x480 UI plane
	float fovy = 2.0f * atanf(240.0f / CAMERA_DIST) * (180.0f / M_PI);
	guPerspective(proj, fovy, 640.0f / 480.0f, 10.0f, 5000.0f);
	GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
}

static void loadOrtho(void)
{
	Mtx44 proj;
	guOrtho(proj, 0, 480, 0, 640, 0, 1);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
}

void UI_ResetTransform(void)
{
	guMtxIdentity(uiXform);
	uiPerspective = false;
}

void UI_SetTransform(Mtx m, bool perspective)
{
	guMtxCopy(m, uiXform);
	uiPerspective = perspective;
}

void UI_GetTransform(Mtx m, bool *perspective)
{
	if (!initialised) Scene3D_Init();
	guMtxCopy(uiXform, m);
	*perspective = uiPerspective;
}

void UI_LoadProjection(void)
{
	if (!initialised) Scene3D_Init();
	if (uiPerspective) loadPerspective();
	else loadOrtho();
}

void UI_ApplyTransform(Mtx local, Mtx out)
{
	Mtx t;
	if (!initialised) Scene3D_Init();
	guMtxConcat(uiXform, local, t);
	if (uiPerspective) {
		Mtx v;
		screenView(v);
		guMtxConcat(v, t, out);
	}
	else {
		guMtxCopy(t, out);
	}
}

// Apply the current UI transform to a point in UI screen space
void UI_TransformPoint(float *x, float *y, float *z)
{
	if (!initialised) Scene3D_Init();
	guVector v = {*x, *y, *z};
	guVecMultiply(uiXform, &v, &v);
	*x = v.x;
	*y = v.y;
	*z = v.z;
}

void UI_MakeTransform(Mtx out, float pivotX, float pivotY, float tx, float ty, float tz, float rotY, float rotX, float scale)
{
	Mtx a, b, c;
	guMtxTrans(a, -pivotX, -pivotY, 0.0f);
	guMtxScale(b, scale, scale, scale);
	guMtxConcat(b, a, c);
	guMtxRotRad(a, 'x', rotX);
	guMtxConcat(a, c, b);
	guMtxRotRad(a, 'y', rotY);
	guMtxConcat(a, b, c);
	guMtxTrans(a, pivotX + tx, pivotY + ty, tz);
	guMtxConcat(a, c, out);
}

/* ---------------------------------------------------------------------------
 * Pipeline setup
 * ------------------------------------------------------------------------- */

static void setupColorPipeline(bool lit)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(VTXFMT_LIT, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(VTXFMT_LIT, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(VTXFMT_LIT, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetCurrentMtx(GX_PNMTX0);

	GX_SetNumChans(1);
	GX_SetNumTexGens(0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetTevDirect(GX_TEVSTAGE0);

	if (lit) {
		GXLightObj key, rim;
		memset(&key, 0, sizeof(key));
		memset(&rim, 0, sizeof(rim));
		GX_InitLightPos(&key, -30000.0f, 40000.0f, 50000.0f);
		GX_InitLightColor(&key, (GXColor) {210, 205, 235, 255});
		GX_InitLightAttn(&key, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
		GX_LoadLightObj(&key, GX_LIGHT0);
		GX_InitLightPos(&rim, 40000.0f, -25000.0f, 15000.0f);
		GX_InitLightColor(&rim, (GXColor) {90, 70, 170, 255});
		GX_InitLightAttn(&rim, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
		GX_LoadLightObj(&rim, GX_LIGHT1);
		GX_SetChanAmbColor(GX_COLOR0, (GXColor) {70, 64, 96, 255});
		GX_SetChanCtrl(GX_COLOR0, GX_ENABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT0 | GX_LIGHT1, GX_DF_CLAMP, GX_AF_NONE);
	}
	else {
		GX_SetChanCtrl(GX_COLOR0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	}
	GX_SetChanCtrl(GX_ALPHA0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);

	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetColorUpdate(GX_ENABLE);
}

static void setupTexturePipeline(GXTexObj *texObj, bool additive)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(VTXFMT_TEX, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(VTXFMT_TEX, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(VTXFMT_TEX, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetCurrentMtx(GX_PNMTX0);

	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordScaleManually(GX_TEXCOORD0, GX_DISABLE, 0, 0);
	GX_SetNumIndStages(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevDirect(GX_TEVSTAGE0);
	if (additive) {
		// Glow textures carry their shape in the colour channels
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
		GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
		GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
	}
	else {
		GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
		GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	}

	GX_InvalidateTexAll();
	GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
	if (tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
	GX_LoadTexObj(texObj, GX_TEXMAP0);

	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
}

static void finishPipeline(void)
{
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
}

/* ---------------------------------------------------------------------------
 * Cubes
 * ------------------------------------------------------------------------- */

static const float faceNormal[6][3] = {
	{ 0,  0,  1}, { 0,  0, -1}, { 1,  0,  0}, {-1,  0,  0}, { 0,  1,  0}, { 0, -1,  0}
};

static const float faceVerts[6][4][3] = {
	{{-.5f, -.5f,  .5f}, { .5f, -.5f,  .5f}, { .5f,  .5f,  .5f}, {-.5f,  .5f,  .5f}},
	{{ .5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}, {-.5f,  .5f, -.5f}, { .5f,  .5f, -.5f}},
	{{ .5f, -.5f,  .5f}, { .5f, -.5f, -.5f}, { .5f,  .5f, -.5f}, { .5f,  .5f,  .5f}},
	{{-.5f, -.5f, -.5f}, {-.5f, -.5f,  .5f}, {-.5f,  .5f,  .5f}, {-.5f,  .5f, -.5f}},
	{{-.5f,  .5f,  .5f}, { .5f,  .5f,  .5f}, { .5f,  .5f, -.5f}, {-.5f,  .5f, -.5f}},
	{{-.5f, -.5f, -.5f}, { .5f, -.5f, -.5f}, { .5f, -.5f,  .5f}, {-.5f, -.5f,  .5f}},
};

static const u8 edgeList[12][2] = {
	{0, 1}, {1, 3}, {3, 2}, {2, 0},
	{4, 5}, {5, 7}, {7, 6}, {6, 4},
	{0, 4}, {1, 5}, {2, 6}, {3, 7}
};

static void cornerOf(int i, float *v)
{
	v[0] = (i & 1) ? .5f : -.5f;
	v[1] = (i & 2) ? .5f : -.5f;
	v[2] = (i & 4) ? .5f : -.5f;
}

static float mtxZ(Mtx m, const float *v)
{
	return m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[2][3];
}

// Rotation (for normals) and model-view matrix of a cube in camera space
static void cubeMatrices(const cube3d_t *c, Mtx mv, Mtx rot)
{
	Mtx rx, ry, rz, tmp, s, t;
	guMtxRotRad(rx, 'x', c->rx);
	guMtxRotRad(ry, 'y', c->ry);
	guMtxRotRad(rz, 'z', c->rz);
	guMtxConcat(ry, rx, tmp);
	guMtxConcat(rz, tmp, rot);
	guMtxScale(s, c->size, c->size, c->size);
	guMtxConcat(rot, s, tmp);
	guMtxTrans(t, c->x - 320.0f, 240.0f - c->y, c->z - CAMERA_DIST);
	guMtxConcat(t, tmp, mv);
}

static void drawCube(const cube3d_t *c)
{
	Mtx mv, rot;
	cubeMatrices(c, mv, rot);

	// Sort faces back to front so the translucent cube reads like glass
	int order[6];
	float depth[6];
	for (int f = 0; f < 6; f++) {
		float centre[3] = {faceNormal[f][0] * .5f, faceNormal[f][1] * .5f, faceNormal[f][2] * .5f};
		depth[f] = mtxZ(mv, centre);
		order[f] = f;
	}
	for (int i = 1; i < 6; i++) {
		for (int j = i; j > 0 && depth[order[j]] < depth[order[j - 1]]; j--) {
			int tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp;
		}
	}

	setupColorPipeline(true);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_LoadNrmMtxImm(rot, GX_PNMTX0);
	for (int i = 0; i < 6; i++) {
		int f = order[i];
		bool back = i < 3;
		u8 a = back ? c->color.a / 2 : c->color.a;
		GX_Begin(GX_QUADS, VTXFMT_LIT, 4);
		for (int v = 0; v < 4; v++) {
			GX_Position3f32(faceVerts[f][v][0], faceVerts[f][v][1], faceVerts[f][v][2]);
			GX_Normal3f32(faceNormal[f][0], faceNormal[f][1], faceNormal[f][2]);
			GX_Color4u8(c->color.r, c->color.g, c->color.b, a);
		}
		GX_End();
	}

	if (c->edges > 0.0f) {
		setupColorPipeline(false);
		GX_LoadPosMtxImm(mv, GX_PNMTX0);
		GX_SetLineWidth(12, GX_TO_ZERO);
		float centreZ = mv[2][3];
		GX_Begin(GX_LINES, VTXFMT_LIT, 24);
		for (int e = 0; e < 12; e++) {
			float a[3], b[3];
			cornerOf(edgeList[e][0], a);
			cornerOf(edgeList[e][1], b);
			float mid[3] = {(a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f, (a[2] + b[2]) * .5f};
			float front = mtxZ(mv, mid) >= centreZ ? 1.0f : 0.35f;
			u8 alpha = (u8)(c->edges * front * c->color.a);
			u8 r = (u8)((255 + c->color.r) / 2), g = (u8)((255 + c->color.g) / 2), bl = (u8)((255 + c->color.b) / 2);
			GX_Position3f32(a[0], a[1], a[2]);
			GX_Normal3f32(0, 0, 1);
			GX_Color4u8(r, g, bl, alpha);
			GX_Position3f32(b[0], b[1], b[2]);
			GX_Normal3f32(0, 0, 1);
			GX_Color4u8(r, g, bl, alpha);
		}
		GX_End();
	}
}

void Scene3D_DrawCubes(cube3d_t *cubes, int count)
{
	int order[count];
	for (int i = 0; i < count; i++) order[i] = i;
	for (int i = 1; i < count; i++) {
		for (int j = i; j > 0 && cubes[order[j]].z < cubes[order[j - 1]].z; j--) {
			int tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp;
		}
	}
	loadPerspective();
	for (int i = 0; i < count; i++) {
		drawCube(&cubes[order[i]]);
	}
	finishPipeline();
}

/* ---------------------------------------------------------------------------
 * Flat helpers
 * ------------------------------------------------------------------------- */

void Scene3D_DrawGradientRect(float x, float y, float w, float h, GXColor top, GXColor bottom)
{
	Mtx id;
	loadOrtho();
	guMtxIdentity(id);
	setupColorPipeline(false);
	GX_LoadPosMtxImm(id, GX_PNMTX0);
	GX_Begin(GX_QUADS, VTXFMT_LIT, 4);
		GX_Position3f32(x, y, 0.0f);         GX_Normal3f32(0, 0, 1); GX_Color4u8(top.r, top.g, top.b, top.a);
		GX_Position3f32(x + w, y, 0.0f);     GX_Normal3f32(0, 0, 1); GX_Color4u8(top.r, top.g, top.b, top.a);
		GX_Position3f32(x + w, y + h, 0.0f); GX_Normal3f32(0, 0, 1); GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
		GX_Position3f32(x, y + h, 0.0f);     GX_Normal3f32(0, 0, 1); GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
	GX_End();
	finishPipeline();
}

void Scene3D_DrawBackground(void)
{
	float t = Scene3D_Time();
	float dt = Scene3D_FrameDelta();

	Scene3D_DrawGradientRect(0, 0, 640, 260, THEME_BG_TOP, THEME_BG_MID);
	Scene3D_DrawGradientRect(0, 260, 640, 220, THEME_BG_MID, THEME_BG_BOTTOM);

	cube3d_t cubes[BG_CUBES];
	for (int i = 0; i < BG_CUBES; i++) {
		bgCubes[i].rx += bgCubes[i].vrx * dt;
		bgCubes[i].ry += bgCubes[i].vry * dt;
		bgCubes[i].rz += bgCubes[i].vrz * dt;
		bgCubes[i].x  += bgCubes[i].drift * dt;
		float depthScale = (CAMERA_DIST - bgCubes[i].z) / CAMERA_DIST;
		float halfSpan = 400.0f * depthScale + bgCubes[i].size;
		if (bgCubes[i].x > 320.0f + halfSpan) bgCubes[i].x -= 2.0f * halfSpan;

		cubes[i].x = bgCubes[i].x;
		cubes[i].y = bgCubes[i].y + sinf(t * 0.4f + i) * 12.0f;
		cubes[i].z = bgCubes[i].z;
		cubes[i].size = bgCubes[i].size;
		cubes[i].rx = bgCubes[i].rx;
		cubes[i].ry = bgCubes[i].ry;
		cubes[i].rz = bgCubes[i].rz;
		cubes[i].color = bgCubes[i].color;
		cubes[i].edges = 0.6f;
	}
	Scene3D_DrawCubes(cubes, BG_CUBES);
}

void Scene3D_DrawLogoCube(float x, float y, float size, float alpha)
{
	float t = Scene3D_Time();
	Mtx rx, ry, rot;
	// Same rotation order as cubeMatrices() so the cluster turns as one block
	guMtxRotRad(rx, 'x', 0.5f);
	guMtxRotRad(ry, 'y', t * 0.8f);
	guMtxConcat(ry, rx, rot);

	cube3d_t cubes[8];
	float sub = size * 0.46f;
	float gap = size * 0.25f;
	for (int i = 0; i < 8; i++) {
		float o[3] = {(i & 1) ? gap : -gap, (i & 2) ? gap : -gap, (i & 4) ? gap : -gap};
		// Rotate the offset in camera space (y up) and convert back to screen space
		float cx = rot[0][0] * o[0] + rot[0][1] * o[1] + rot[0][2] * o[2];
		float cy = rot[1][0] * o[0] + rot[1][1] * o[1] + rot[1][2] * o[2];
		float cz = rot[2][0] * o[0] + rot[2][1] * o[1] + rot[2][2] * o[2];
		cubes[i].x = x + cx;
		cubes[i].y = y - cy;
		cubes[i].z = cz;
		cubes[i].size = sub;
		cubes[i].rx = 0.5f;
		cubes[i].ry = t * 0.8f;
		cubes[i].rz = 0.0f;
		cubes[i].color = (GXColor) {110, 86, 255, (u8)(220 * alpha)};
		cubes[i].edges = 0.8f;
	}
	Scene3D_DrawCubes(cubes, 8);
}

void Scene3D_DrawCubeIcon(const cube3d_t *cube, GXTexObj *texObj, float aspect, float alpha)
{
	Mtx mv, rot;
	cubeMatrices(cube, mv, rot);
	// Only draw when the front face points towards the camera
	float nz = rot[2][2];
	float facing = nz;
	if (facing <= 0.05f) return;

	loadPerspective();
	setupTexturePipeline(texObj, false);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);

	float hw = 0.38f, hh = 0.38f;
	if (aspect > 1.0f) hh /= aspect;
	else hw *= aspect;
	u8 a = (u8)(255.0f * alpha * (facing > 1.0f ? 1.0f : facing));
	GX_Begin(GX_QUADS, VTXFMT_TEX, 4);
		GX_Position3f32(-hw,  hh, 0.502f); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(0.0f, 0.0f);
		GX_Position3f32( hw,  hh, 0.502f); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(1.0f, 0.0f);
		GX_Position3f32( hw, -hh, 0.502f); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(1.0f, 1.0f);
		GX_Position3f32(-hw, -hh, 0.502f); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(0.0f, 1.0f);
	GX_End();
	finishPipeline();
}

void Scene3D_DrawGlow(GXTexObj *texObj, float x, float y, float z, float w, float h, GXColor color)
{
	Mtx v;
	loadPerspective();
	setupTexturePipeline(texObj, true);
	screenView(v);
	GX_LoadPosMtxImm(v, GX_PNMTX0);
	float x1 = x - w / 2, x2 = x + w / 2, y1 = y - h / 2, y2 = y + h / 2;
	GX_Begin(GX_QUADS, VTXFMT_TEX, 4);
		GX_Position3f32(x1, y1, z); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(0.0f, 0.0f);
		GX_Position3f32(x2, y1, z); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(1.0f, 0.0f);
		GX_Position3f32(x2, y2, z); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(1.0f, 1.0f);
		GX_Position3f32(x1, y2, z); GX_Color4u8(color.r, color.g, color.b, color.a); GX_TexCoord2f32(0.0f, 1.0f);
	GX_End();
	finishPipeline();
}
