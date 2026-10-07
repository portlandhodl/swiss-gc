/* -----------------------------------------------------------
      FrameBufferMagic.c - Framebuffer routines with GX
	      - by emu_kidid & sepp256

      Version 1.0 11/11/2009
        - Initial Code
   ----------------------------------------------------------- */

#include <fnmatch.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <math.h>
#include <ogc/exi.h>
#include <gctypes.h>
#include <ogc/lwp_watchdog.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "filemeta.h"
#include "swiss.h"
#include "main.h"
#include "util.h"
#include "ata.h"
#include "btns.h"
#include "dolparameters.h"
#include "cheats.h"
#include "scene3d.h"
#include "uisound.h"

#define GUI_MSGBOX_ALPHA 225

TPLFile imagesTPL;
TPLFile buttonsTPL;
TPLFile backdropTPL;
GXTexObj backdropTexObj;
GXTlutObj backdropTlutObj;
GXTexObj backdropIndTexObj;
GXTexObj bannerMaskTexObj;
GXTexObj swissTexObj;
GXTexObj gcdvdsmallTexObj;
GXTexObj sdsmallTexObj;
GXTlutObj sdsmallTlutObj;
GXTexObj hddTexObj;
GXTlutObj hddTlutObj;
GXTexObj qoobTexObj;
GXTlutObj qoobTlutObj;
GXTexObj qoobIndTexObj;
GXTexObj wodeimgTexObj;
GXTexObj usbgeckoTexObj;
GXTlutObj usbgeckoTlutObj;
GXTexObj memcardTexObj;
GXTlutObj memcardTlutObj;
GXTexObj memcardIndTexObj;
GXTexObj bbaTexObj;
GXTexObj wiikeyTexObj;
GXTexObj systemTexObj;
GXTexObj btnhilightTexObj;
GXTexObj btndeviceTexObj;
GXTexObj btnsettingsTexObj;
GXTexObj btninfoTexObj;
GXTexObj btnrefreshTexObj;
GXTexObj btnexitTexObj;
GXTexObj btngamesTexObj;
GXTexObj btnfilesTexObj;
GXTexObj boxinnerTexObj;
GXTexObj boxouterTexObj;
GXTexObj ntscjTexObj;
GXTexObj ntscuTexObj;
GXTexObj palTexObj;
GXTexObj checkedTexObj;
GXTexObj uncheckedTexObj;
GXTexObj loadingTexObj;
GXTexObj starTexObj;
GXTexObj dirimgTexObj;
GXTexObj dolimgTexObj;
GXTexObj dolcliimgTexObj;
GXTexObj elfimgTexObj;
GXTexObj fileimgTexObj;
GXTexObj fpkgimgTexObj;
GXTexObj gcmimgTexObj;
GXTexObj mp3imgTexObj;
GXTexObj tgcimgTexObj;
GXTexObj gcloaderTexObj;
GXTexObj m2loaderTexObj;
GXTexObj eth2gcTexObj;
GXTexObj flippyTexObj;
GXTexObj gcnetTexObj;
GXTexObj kunaigcTexObj;

static char fbTextBuffer[256];

// Video threading vars
#define VIDEO_STACK_SIZE (64*1024)
#define VIDEO_PRIORITY LWP_PRIO_HIGHEST
static char  video_thread_stack[VIDEO_STACK_SIZE] ATTRIBUTE_ALIGN (8);
static lwp_t video_thread = LWP_THREAD_NULL;
static mutex_t _videomutex = LWP_MUTEX_NULL;

enum VideoEventType
{
	EV_TEXOBJ = 0,
	EV_MSGBOX,
	EV_IMAGE,
	EV_PROGRESS,
	EV_SELECTABLEBUTTON,
	EV_EMPTYBOX,
	EV_TRANSPARENTBOX,
	EV_FILEBROWSERBUTTON,
	EV_VERTSCROLLBAR,
	EV_STYLEDLABEL,
	EV_CONTAINER,
	EV_MENUBUTTONS,
	EV_TOOLTIP,
	EV_TITLEBAR,
	EV_SCENE3D,
	EV_PAGEHEADER,
	EV_SELECTBAR,
	EV_COVERFLOW
};

char * typeStrings[] = {"TexObj", "MsgBox", "Image", "Progress", "SelectableButton", "EmptyBox", "TransparentBox",
						"FileBrowserButton", "VertScrollbar", "StyledLabel", "Container", "MenuButtons", "Tooltip", "TitleBar", "Scene3D", "PageHeader", "SelectBar", "Coverflow"};

typedef struct drawTexObjEvent {
	GXTexObj *texObj;
	int x;
	int y;
	int width;
	int height;
	int depth;
	float s1;
	float s2;
	float t1;
	float t2;
} drawTexObjEvent_t;

typedef struct drawVertScrollbarEvent {
	int x;
	int y;
	int width;
	int height;
	float scrollPercent;
	int scrollHeight;
} drawVertScrollbarEvent_t;

typedef struct drawImageEvent {
	int textureId;
	int x;
	int y;
	int width;
	int height;
	int depth;
	float s1;
	float s2;
	float t1;
	float t2;
} drawImageEvent_t;

typedef struct drawStyledLabelEvent {
	int x;
	int y;
	const char *(*getString)(void);
	char *string;
	float size;
	int align;
	GXColor color;
	int fadingDirection;
	bool showCaret;
	int caretPosition;
	GXColor caretColor;
} drawStyledLabelEvent_t;

typedef struct drawSelectableButtonEvent {
	int x1;
	int y1;
	int x2;
	int y2;
	int mode;
	char *msg;
} drawSelectableButtonEvent_t;

typedef struct drawBoxEvent {
	int x1;
	int y1;
	int x2;
	int y2;
	GXColor backfill;
} drawBoxEvent_t;

typedef struct drawFileBrowserButtonEvent {
	int x1;
	int y1;
	int x2;
	int y2;
	char *displayName;
	file_handle *file;
	int mode;
	int alpha;
	bool isAutoLoadEntry;
	bool isCarousel;	// Draw this as a full "card" style
	bool isGameCard;	// Draw this as a game card (cover flow)
	int distFromMiddle;	// 0 = middle card, otherwise how many cards to the left (-) or right (+)
} drawFileBrowserButtonEvent_t;

typedef struct drawMenuButtonsEvent {
	int selection;
} drawMenuButtonsEvent_t;

typedef struct drawTooltipEvent {
	char *tooltip;
} drawTooltipEvent_t;

typedef struct drawMsgBoxEvent {
	int type;
} drawMsgBoxEvent_t;

typedef struct drawProgressEvent {
	bool indeterminate;
	bool miniMode;
	int miniModePos;
	int miniModeAlpha;
	int percent;
	int speed;	// in bytes
	int timestart;
	int timeremain;
} drawProgressEvent_t;

typedef struct drawPageHeaderEvent {
	int x;
	int y;
	char *title;
	int page;
	int pageCount;
} drawPageHeaderEvent_t;

typedef struct drawCoverflowEvent {
	int selected;	// index into the full list
	int first;		// list index of cards[0]
	int count;
	struct uiDrawObj **cards;
} drawCoverflowEvent_t;

typedef struct uiDrawObjQueue {
	struct uiDrawObj *event;
	struct uiDrawObjQueue *next;
} uiDrawObjQueue_t;

static uiDrawObjQueue_t *videoEventQueue = NULL;
static uiDrawObj_t *buttonPanel = NULL;

// Home menu animation state, owned by the video thread
static bool customBackdrop = false;
static float homeBlend = 0.0f;	// 0 = file browser, 1 = home menu
static float ringPos = 0.0f;	// smoothed home menu selection
static float coverflowPos = -100.0f;	// smoothed cover flow position

// Home menu activation: the cube squashes, jumps with a spin, lands and the screen fades
#define ACTIVATE_JUMP_END  0.85f	// the next screen is opened from here
#define ACTIVATE_END       1.25f	// fade back in finished
static volatile u64 activateStart;
static volatile int activateItem = -1;

// Add root level uiDrawObj_t
static uiDrawObj_t* addVideoEvent(uiDrawObj_t *event) {
	// First entry, make it root
	if(videoEventQueue == NULL) {
		videoEventQueue = calloc(1, sizeof(uiDrawObjQueue_t));
		videoEventQueue->event = event;
		//print_debug("Added first event %08X (type %s)\n", (u32)videoEventQueue, typeStrings[event->type]);
		return event;
	}
	
	uiDrawObjQueue_t *current = videoEventQueue;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = calloc(1, sizeof(uiDrawObjQueue_t));
	current->next->event = event;
	event->disposed = false;
	//print_debug("Added a new event %08X (type %s)\n", (u32)event, typeStrings[event->type]);
	return event;
}

static void clearNestedEvent(uiDrawObj_t *event) {
	if(event && !event->disposed) {
		print_debug("Event was not disposed!!\n");
		print_debug("Event %08X (type %s)\n", (u32)event, typeStrings[event->type]);
	}
	
	if(event->child && event->child != event) {
		clearNestedEvent(event->child);
	}
	//print_debug("Dispose nested event %08X\n", (u32)event);
	if(event && event->data) {
		// Free any attached data
		if(event->type == EV_STYLEDLABEL) {
			if(((drawStyledLabelEvent_t*)event->data)->string) {
				//print_debug("Clear Nested EV_STYLEDLABEL\n");
				free(((drawStyledLabelEvent_t*)event->data)->string);
			}
		}
		else if(event->type == EV_FILEBROWSERBUTTON) {
			if(((drawFileBrowserButtonEvent_t*)event->data)->displayName) {
				//print_debug("Clear Nested EV_FILEBROWSERBUTTON\n");
				free(((drawFileBrowserButtonEvent_t*)event->data)->displayName);
			}
			if(((drawFileBrowserButtonEvent_t*)event->data)->file) {
				if(((drawFileBrowserButtonEvent_t*)event->data)->file->meta) {
					if(((drawFileBrowserButtonEvent_t*)event->data)->file->meta->banner) {
						free(((drawFileBrowserButtonEvent_t*)event->data)->file->meta->banner);
					}
					free(((drawFileBrowserButtonEvent_t*)event->data)->file->meta);
				}
				free(((drawFileBrowserButtonEvent_t*)event->data)->file);
			}
		}
		else if(event->type == EV_SELECTABLEBUTTON) {
			if(((drawSelectableButtonEvent_t*)event->data)->msg) {
				//print_debug("Clear Nested EV_SELECTABLEBUTTON\n");
				free(((drawSelectableButtonEvent_t*)event->data)->msg);
			}
		}
		else if(event->type == EV_TOOLTIP) {
			if(((drawTooltipEvent_t*)event->data)->tooltip) {
				//print_debug("Clear Nested EV_TOOLTIP\n");
				free(((drawTooltipEvent_t*)event->data)->tooltip);
			}
		}
		else if(event->type == EV_PAGEHEADER) {
			free(((drawPageHeaderEvent_t*)event->data)->title);
		}
		else if(event->type == EV_COVERFLOW) {
			drawCoverflowEvent_t *data = (drawCoverflowEvent_t*)event->data;
			for(int i = 0; i < data->count; i++) {
				data->cards[i]->disposed = true;
				clearNestedEvent(data->cards[i]);
			}
			free(data->cards);
		}
		//print_debug("Clear Nested event->data\n");
		free(event->data);
	}
	if(event) {
		//print_debug("Clear event\n");
		memset(event, 0, sizeof(uiDrawObj_t));
		free(event);
	}
}

static void disposeEvent(uiDrawObj_t *event) {
	if(videoEventQueue == NULL) {
		return;
	}

	// See if this is in our root event queue
	uiDrawObjQueue_t *current = videoEventQueue->next;
	uiDrawObjQueue_t *previous = videoEventQueue;
	// First node is what we're after.
	while (current != NULL) {
		if(current->event == event) {
			//print_debug("Disposing event %08X\n", (u32)current);
			clearNestedEvent(current->event);
			previous->next = current->next;
			free(current);
		}
		else {
			previous = current;
		}
		current = previous->next;
	}
}


static void init_textures() 
{
	TPL_OpenTPLFromMemory(&imagesTPL, (void *)images_tpl, images_tpl_size);
	TPL_OpenTPLFromMemory(&buttonsTPL, (void *)buttons_tpl, buttons_tpl_size);
	TPL_GetTextureCI(&imagesTPL, backdrop, &backdropTexObj, &backdropTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&backdropTexObj, &backdropTlutObj);
	TPL_GetTexture(&imagesTPL, backdrop_ind, &backdropIndTexObj);
	GX_InitTexObjUserData(&backdropIndTexObj, &backdropTexObj);
	TPL_GetTexture(&imagesTPL, banner_mask, &bannerMaskTexObj);
	TPL_GetTexture(&imagesTPL, swissimg, &swissTexObj);
	TPL_GetTexture(&imagesTPL, gcdvdsmall, &gcdvdsmallTexObj);
	TPL_GetTextureCI(&imagesTPL, sdsmall, &sdsmallTexObj, &sdsmallTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&sdsmallTexObj, &sdsmallTlutObj);
	TPL_GetTextureCI(&imagesTPL, hddimg, &hddTexObj, &hddTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&hddTexObj, &hddTlutObj);
	TPL_GetTextureCI(&imagesTPL, qoobimg, &qoobTexObj, &qoobTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&qoobTexObj, &qoobTlutObj);
	TPL_GetTexture(&imagesTPL, qoobimg_ind, &qoobIndTexObj);
	TPL_GetTexture(&imagesTPL, wodeimg, &wodeimgTexObj);
	TPL_GetTexture(&imagesTPL, wiikeyimg, &wiikeyTexObj);
	TPL_GetTexture(&imagesTPL, systemimg, &systemTexObj);
	TPL_GetTextureCI(&imagesTPL, memcardimg, &memcardTexObj, &memcardTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&memcardTexObj, &memcardTlutObj);
	TPL_GetTexture(&imagesTPL, memcardimg_ind, &memcardIndTexObj);
	TPL_GetTextureCI(&imagesTPL, usbgeckoimg, &usbgeckoTexObj, &usbgeckoTlutObj, GX_TLUT0);
	GX_InitTexObjUserData(&usbgeckoTexObj, &usbgeckoTlutObj);
	TPL_GetTexture(&imagesTPL, bbaimg, &bbaTexObj);
	TPL_GetTexture(&buttonsTPL, btnhilight, &btnhilightTexObj);
	TPL_GetTexture(&buttonsTPL, btndevice, &btndeviceTexObj);
	TPL_GetTexture(&buttonsTPL, btnsettings, &btnsettingsTexObj);
	TPL_GetTexture(&buttonsTPL, btninfo, &btninfoTexObj);
	TPL_GetTexture(&buttonsTPL, btnrefresh, &btnrefreshTexObj);
	TPL_GetTexture(&buttonsTPL, btnexit, &btnexitTexObj);
	TPL_GetTexture(&buttonsTPL, btngames, &btngamesTexObj);
	TPL_GetTexture(&buttonsTPL, btnfiles, &btnfilesTexObj);
	TPL_GetTexture(&buttonsTPL, boxinner, &boxinnerTexObj);
	TPL_GetTexture(&buttonsTPL, boxouter, &boxouterTexObj);
	TPL_GetTexture(&imagesTPL, ntscjimg, &ntscjTexObj);
	TPL_GetTexture(&imagesTPL, ntscuimg, &ntscuTexObj);
	TPL_GetTexture(&imagesTPL, palimg, &palTexObj);
	TPL_GetTexture(&buttonsTPL, checked_32, &checkedTexObj);
	TPL_GetTexture(&buttonsTPL, unchecked_32, &uncheckedTexObj);
	TPL_GetTexture(&buttonsTPL, loading_16, &loadingTexObj);
	TPL_GetTexture(&buttonsTPL, star_16, &starTexObj);
	TPL_GetTexture(&imagesTPL, dirimg, &dirimgTexObj);
	TPL_GetTexture(&imagesTPL, dolimg, &dolimgTexObj);
	TPL_GetTexture(&imagesTPL, dolcliimg, &dolcliimgTexObj);
	TPL_GetTexture(&imagesTPL, elfimg, &elfimgTexObj);
	TPL_GetTexture(&imagesTPL, fileimg, &fileimgTexObj);
	TPL_GetTexture(&imagesTPL, fpkgimg, &fpkgimgTexObj);
	TPL_GetTexture(&imagesTPL, gcmimg, &gcmimgTexObj);
	TPL_GetTexture(&imagesTPL, mp3img, &mp3imgTexObj);
	TPL_GetTexture(&imagesTPL, tgcimg, &tgcimgTexObj);
	TPL_GetTexture(&imagesTPL, gcloaderimg, &gcloaderTexObj);
	TPL_GetTexture(&imagesTPL, m2loaderimg, &m2loaderTexObj);
	TPL_GetTexture(&imagesTPL, eth2gcimg, &eth2gcTexObj);
	TPL_GetTexture(&imagesTPL, flippyimg, &flippyTexObj);
	TPL_GetTexture(&imagesTPL, gcnetimg, &gcnetTexObj);
	TPL_GetTexture(&imagesTPL, kunaigcimg, &kunaigcTexObj);
}

static void drawInit()
{
	Mtx GXmodelView2D;

	// Reset various parameters from gfx plugin
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
	GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

	guMtxIdentity(GXmodelView2D);
	GX_LoadTexMtxImm(GXmodelView2D,GX_TEXMTX0,GX_MTX2x4);
	UI_ApplyTransform(GXmodelView2D, GXmodelView2D);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
	UI_LoadProjection();

	GX_SetZMode(GX_DISABLE,GX_ALWAYS,GX_FALSE);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_PNMTXIDX, GX_PNMTX0);
	GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	//set vertex attribute formats here
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	//enable textures
	GX_SetNumChans (1);
	GX_SetNumTexGens (1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTexCoordScaleManually(GX_TEXCOORD0, GX_DISABLE, 0, 0);

	GX_SetNumIndStages (0);
	GX_SetNumTevStages (2);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn (GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn (GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevAlphaOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect (GX_TEVSTAGE0);
	GX_SetTevOrder (GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn (GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASA, GX_CC_ZERO);
	GX_SetTevColorOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevAlphaIn (GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
	GX_SetTevDirect (GX_TEVSTAGE1);

	//set blend mode
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR); //Fix src alpha
	GX_SetColorUpdate(GX_ENABLE);
//	GX_SetAlphaUpdate(GX_ENABLE);
//	GX_SetDstAlpha(GX_DISABLE, 0xFF);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);
}

static void _drawRect(int x, int y, int width, int height, int depth, GXColor color, float s0, float s1, float t0, float t1)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t0);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t0);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t1);
	GX_End();
}

static void _drawRectGradient(int x, int y, int width, int height, int depth, GXColor top, GXColor bottom, float s0, float s1, float t0, float t1)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(top.r, top.g, top.b, top.a);
		GX_TexCoord2f32(s0,t0);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(top.r, top.g, top.b, top.a);
		GX_TexCoord2f32(s1,t0);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
		GX_TexCoord2f32(s0,t1);
	GX_End();
}

static GXColor mixColor(GXColor a, GXColor b, float t, float alpha)
{
	return (GXColor) {
		(u8)(a.r + (b.r - a.r) * t),
		(u8)(a.g + (b.g - a.g) * t),
		(u8)(a.b + (b.b - a.b) * t),
		(u8)(alpha < 0.0f ? 0 : alpha > 255.0f ? 255 : alpha)
	};
}

// One layer of a rounded box using the 4-way mirrored corner texture, shaded top -> middle -> bottom
static void _drawBoxLayer(GXTexObj *texObj, int x, int y, int width, int height, int depth, GXColor top, GXColor mid, GXColor bottom)
{
	GX_InvalidateTexAll();
	GX_LoadTexObj(texObj, GX_TEXMAP0);
	_drawRectGradient(x, y, width/2, height/2, depth, top, mid, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	_drawRectGradient(x+(width/2), y, width/2, height/2, depth, top, mid, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	_drawRectGradient(x, y+(height/2), width/2, height/2, depth, mid, bottom, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	_drawRectGradient(x+(width/2), y+(height/2), width/2, height/2, depth, mid, bottom, ((float)width/32), 0.0f, ((float)height/32), 0.0f);
}

// Diagonal band of light that periodically sweeps across a large panel
static void _drawGlassSheen(int x, int y, int width, int height)
{
	Mtx m;
	bool perspective;
	UI_GetTransform(m, &perspective);
	if(perspective) return;	// The scissor below only lines up with the panel in plain 2D

	float period = 7.0f;
	float phase = fmodf(Scene3D_Time() + (x + y) * 0.004f, period) / 1.4f;
	if(phase >= 1.0f) return;

	float skew = height * 0.45f;
	float band = 70.0f;
	float bx = x - band + phase * (width + band + skew);
	GXRModeObj *vmode = getVideoMode();
	float sy = (float)vmode->efbHeight / 480.0f;
	float os = UI_GetOverscan();
	float sx0 = 320.0f + (x - 320.0f) * os, sy0 = 240.0f + (y - 240.0f) * os;
	GX_SetScissor(MAX(0, (int)sx0), MAX(0, (int)(sy0 * sy)), (int)(width * os), (int)(height * os * sy));

	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
	for(int half = 0; half < 2; half++) {
		float x0 = bx + half * band / 2, x1 = x0 + band / 2;
		u8 a0 = half ? 46 : 0, a1 = half ? 0 : 46;
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position3f32(x0, y, 0.0f);               GX_Color4u8(255, 255, 255, a0); GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position3f32(x1, y, 0.0f);               GX_Color4u8(255, 255, 255, a1); GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position3f32(x1 - skew, y + height, 0.0f); GX_Color4u8(255, 255, 255, a1); GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position3f32(x0 - skew, y + height, 0.0f); GX_Color4u8(255, 255, 255, a0); GX_TexCoord2f32(0.0f, 0.0f);
		GX_End();
	}
	GX_SetScissor(0, 0, vmode->fbWidth, vmode->efbHeight);
	drawInit();
}

// Frosted glass panel: soft shadow, tinted body lit from above, specular highlight,
// bevelled edge and the occasional sheen. A transparent fill only draws the edge.
static void _DrawSimpleBox(int x, int y, int width, int height, int depth, GXColor fillColor, GXColor borderColor) 
{
	GXColor white = (GXColor) {255,255,255,255};
	GXColor black = (GXColor) {0,0,0,255};
	int bx = x, by = y, bw = width, bh = height;

	//Adjust for blank texture border
	x-=4; y-=4; width+=8; height+=8;

	if(fillColor.a) {
		float a = fillColor.a;
		if(height >= 30) {
			_drawBoxLayer(&boxinnerTexObj, x+3, y+5, width, height, depth,
				mixColor(black, black, 0, a*0.25f), mixColor(black, black, 0, a*0.30f), mixColor(black, black, 0, a*0.40f));
		}
		_drawBoxLayer(&boxinnerTexObj, x, y, width, height, depth,
			mixColor(fillColor, white, 0.22f, a), fillColor, mixColor(fillColor, black, 0.35f, a));
		_drawBoxLayer(&boxinnerTexObj, x, y, width, height, depth,
			mixColor(white, white, 0, a*0.28f), mixColor(white, white, 0, 0), mixColor(white, white, 0, a*0.05f));
	}

	_drawBoxLayer(&boxouterTexObj, x, y, width, height, depth,
		mixColor(borderColor, white, 0.35f, borderColor.a*0.85f), mixColor(borderColor, borderColor, 0, borderColor.a*0.8f), mixColor(borderColor, black, 0.25f, borderColor.a*0.6f));

	if(fillColor.a && bw >= 200 && bh >= 100) {
		_drawGlassSheen(bx, by, bw, bh);
	}
}

// Soft halo around a selected item
static void _DrawGlassGlow(int x, int y, int width, int height, GXColor color)
{
	float pulse = 0.7f + 0.3f*sinf(Scene3D_Time()*4.0f);
	x-=4; y-=4; width+=8; height+=8;
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
	for(int i = 1; i <= 3; i++) {
		int g = i * 3;
		GXColor c = color;
		c.a = (u8)(90.0f * pulse / i);
		_drawBoxLayer(&boxouterTexObj, x-g, y-g, width+g*2, height+g*2, 0, c, c, c);
	}
	drawInit();
}

// Untextured rectangle that follows the current UI transform
static void _DrawSolidRect(int x, int y, int width, int height, GXColor color) {
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	_drawRect(x, y, width, height, 0, color, 0.0f, 0.0f, 0.0f, 0.0f);
	drawInit();
}

// Internal
static void _DrawImageNow(int textureId, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered) {
	u16 ss = 0, ts = 0;
	GXTexObj *texObj = NULL;
	GXTexObj *indTexObj = NULL;
	GXColor color = (GXColor) {255,255,255,255};
	
	switch(textureId)
	{
		case TEX_BACKDROP:
			switch(GX_GetTexObjFmt(&backdropTexObj)) {
				case GX_TF_CI4:
				case GX_TF_CI8:
				case GX_TF_CI14:
					if(GX_GetTlutObjFmt(&backdropTlutObj) != GX_TL_IA8) {
						texObj = &backdropTexObj;
						break;
					}
				case GX_TF_IA4:
				case GX_TF_IA8:
					GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXA, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
					GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
					
					texObj = &backdropTexObj; color = (GXColor) {0,0,255,255};
					break;
				default:
					texObj = &backdropTexObj;
					break;
			}
			if(GX_GetTexObjUserData(&backdropIndTexObj) == texObj) {
				indTexObj = &backdropIndTexObj;
				ss = 640; ts = 480;
			}
			break;
		case TEX_SWISS:
			texObj = &swissTexObj;
			break;
		case TEX_GCDVDSMALL:
			texObj = &gcdvdsmallTexObj;
			break;
		case TEX_SDSMALL:
			texObj = &sdsmallTexObj;
			break;
		case TEX_HDD:
			texObj = &hddTexObj;
			break;
		case TEX_QOOB:
			texObj = &qoobTexObj;
			indTexObj = &qoobIndTexObj;
			ss = 96; ts = 102;
			break;
		case TEX_WODEIMG:
			texObj = &wodeimgTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_USBGECKO:
			texObj = &usbgeckoTexObj;
			break;
		case TEX_WIIKEY:
			texObj = &wiikeyTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_SYSTEM:
			texObj = &systemTexObj;
			break;
		case TEX_MEMCARD:
			texObj = &memcardTexObj;
			indTexObj = &memcardIndTexObj;
			ss = 80; ts = 92;
			break;
		case TEX_BBA:
			texObj = &bbaTexObj;
			break;
		case TEX_BTNHILIGHT:
			GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
			GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
			
			texObj = &btnhilightTexObj; color = (GXColor) {127,134,255,255};
			break;
		case TEX_BTNDEVICE:
			texObj = &btndeviceTexObj;
			break;
		case TEX_BTNSETTINGS:
			texObj = &btnsettingsTexObj;
			break;
		case TEX_BTNINFO:
			texObj = &btninfoTexObj;
			break;
		case TEX_BTNREFRESH:
			texObj = &btnrefreshTexObj;
			break;
		case TEX_BTNEXIT:
			texObj = &btnexitTexObj;
			break;
		case TEX_CHECKED:
			texObj = &checkedTexObj; color = (GXColor) {0,128,0,255};
			break;
		case TEX_UNCHECKED:
			texObj = &uncheckedTexObj; color = (GXColor) {87,87,87,255};
			ss = 32; ts = 32;
			break;
		case TEX_STAR:
			texObj = &starTexObj; color = (GXColor) {255,255,0,255};
			ss = 16;
			break;
		case TEX_GCLOADER:
			texObj = &gcloaderTexObj; color = (GXColor) {216,216,216,255};
			ts = 76;
			break;
		case TEX_M2LOADER:
			texObj = &m2loaderTexObj;
			break;
		case TEX_ETH2GC:
			texObj = &eth2gcTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_FLIPPY:
			texObj = &flippyTexObj; color = (GXColor) {216,216,216,255};
			t1 -= 18.0f/40.0f;
			break;
		case TEX_GCNET:
			texObj = &gcnetTexObj; color = (GXColor) {216,216,216,255};
			break;
		case TEX_GCODE:
			texObj = &gcloaderTexObj; color = (GXColor) {216,216,216,255};
			t1 -= 12.0f/88.0f;
			break;
		case TEX_KUNAIGC:
			texObj = &kunaigcTexObj; color = (GXColor) {216,216,216,255};
			break;
	}
	
	if(!ss) ss = GX_GetTexObjWidth(texObj);
	if(!ts) ts = GX_GetTexObjHeight(texObj);
	GX_SetTexCoordScaleManually(GX_TEXCOORD0, GX_ENABLE, ss, ts);
	
	GX_InvalidateTexAll();
	GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
	if(tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
	GX_LoadTexObj(texObj, GX_TEXMAP0);
	
	if(indTexObj) {
		GX_LoadTexObj(indTexObj, GX_TEXMAP1);
		
		GX_SetNumIndStages(1);
		GX_SetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD0, GX_TEXMAP1);
		GX_SetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_16, GX_ITS_16);
		
		switch(GX_GetTexObjFmt(indTexObj)) {
			case GX_TF_I8:
				GX_SetTevIndTile(GX_TEVSTAGE0, GX_INDTEXSTAGE0, 16, 16, 16, 0, GX_ITF_8, GX_ITM_0, GX_ITB_NONE, GX_ITBA_OFF);
				GX_SetTevIndRepeat(GX_TEVSTAGE1);
				break;
			case GX_TF_IA8:
				GX_SetTevIndTile(GX_TEVSTAGE0, GX_INDTEXSTAGE0, 16, 16, 16, 16, GX_ITF_8, GX_ITM_0, GX_ITB_NONE, GX_ITBA_OFF);
				GX_SetTevIndRepeat(GX_TEVSTAGE1);
				break;
		}
	}
	
	_drawRect(x, y, width, height, depth, color, s1, s2, t1, t2);
}

// Internal
static void _DrawImage(uiDrawObj_t *evt) {
	drawImageEvent_t *data = (drawImageEvent_t*)evt->data;
	_DrawImageNow(data->textureId, data->x, data->y, data->width, data->height, data->depth, data->s1, data->s2, data->t1, data->t2, 0);
}

// External
uiDrawObj_t* DrawImage(int textureId, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	drawImageEvent_t *eventData = calloc(1, sizeof(drawImageEvent_t));
	eventData->textureId = textureId;
	eventData->x = centered ? ((int) x - width/2) : x;
	eventData->y = y;
	eventData->width = width;
	eventData->height = height;
	eventData->depth = depth;
	eventData->s1 = s1;
	eventData->s2 = s2;
	eventData->t1 = t1;
	eventData->t2 = t2;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_IMAGE;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawTexObjNow(GXTexObj *texObj, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	if(GX_GetTexObjMagFilt(texObj) == GX_NEAR) {
		GX_SetNumTevStages(1);
		GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	}
	GX_InvalidateTexAll();
	GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
	if(tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
	GX_LoadTexObj(texObj, GX_TEXMAP0);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t1);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t2);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t2);
	GX_End();
}

// Internal
static void _DrawTexObj(uiDrawObj_t *evt)
{
	drawTexObjEvent_t *data = (drawTexObjEvent_t*)evt->data;
	_DrawTexObjNow(data->texObj, data->x, data->y, data->width, data->height, data->depth, data->s1, data->s2, data->t1, data->t2, 0);
}

// External
uiDrawObj_t* DrawTexObj(GXTexObj *texObj, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	drawTexObjEvent_t *eventData = calloc(1, sizeof(drawTexObjEvent_t));
	eventData->texObj = texObj;
	eventData->x = centered ? ((int) x - width/2) : x;
	eventData->y = y;
	eventData->width = width;
	eventData->height = height;
	eventData->depth = depth;
	eventData->s1 = s1;
	eventData->s2 = s2;
	eventData->t1 = t1;
	eventData->t2 = t2;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TEXOBJ;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawProgressBar(uiDrawObj_t *evt) {
	
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));

	GXColor fillColor = THEME_PANEL_DARK;
	GXColor noColor = (GXColor) {0,0,0,0}; //blank
	GXColor borderColor = THEME_BORDER;
	GXColor progressBarColor = THEME_PROGRESS;
	GXColor progressBarIndColor = THEME_PROGRESS_IND;
	
	if(data->miniMode) {	
		int x = 30, y = 420;
		if(data->miniModePos == PROGRESS_BOX_TOPRIGHT) {
			x = 535; y = 95;
		}
		GXColor loadingColor = (GXColor) {255,255,255,data->miniModeAlpha};
		int numSegments = (data->percent*8)/100;
		data->percent += (data->percent + 2 > 200 ? -200 : 2);
		if(data->speed != 0) {
			data->miniModeAlpha = MIN(255, data->miniModeAlpha + 3);
		}
		else {
			data->miniModeAlpha = MAX(0, data->miniModeAlpha - 3);
		}
		GX_InvalidateTexAll();
		GX_LoadTexObj(&loadingTexObj, GX_TEXMAP0);
		_drawRect(x-8, y-8, 16, 16, 0, loadingColor, (float) (numSegments)/8, (float) (numSegments+1)/8, 0.0f, 1.0f);
		drawString(x+8, y, "Loading\205", 0.55f, ALIGN_LEFT, loadingColor);
		return;
	}
	_DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor);		
	
	int middleY = (y2+y1)/2;
	if(data->indeterminate) {
		data->percent += (data->percent + 2 == 400 ? -398 : 2);
		int multiplier = (PROGRESS_BOX_WIDTH-20)/100;
		int progressBarWidth = multiplier*100;
		int progressStart = 0;
		int progressSize = 0;
		if(data->percent < 100) {
			progressStart = 0;
			progressSize = data->percent%100;
		}
		else if(data->percent >= 100 && data->percent < 200) {
			progressStart = (data->percent%100);
			progressSize = 100-progressStart;
		}
		else if(data->percent >= 200 && data->percent < 300) {
			progressStart = 100-(data->percent%100);
			progressSize = 100-progressStart;
		}
		else {
			progressStart = 0;
			progressSize = 100-(data->percent%100);
		}
		
		_DrawSimpleBox( (640/2 - progressBarWidth/2), y1+20,
				(multiplier*100), 20, 0, noColor, borderColor); 
		_DrawSimpleBox( (640/2 - progressBarWidth/2) + (progressStart*multiplier),
				y1+20,
				(multiplier*progressSize),
				20, 0, progressBarIndColor, noColor);
	}
	else {
		int multiplier = (PROGRESS_BOX_WIDTH-20)/100;
		int progressBarWidth = multiplier*100;
		_DrawSimpleBox( (640/2 - progressBarWidth/2), y1+20,
				(multiplier*100), 20, 0, noColor, borderColor); 
		_DrawSimpleBox( (640/2 - progressBarWidth/2), y1+20,
				(multiplier*data->percent), 20, 0, progressBarColor, noColor); 
		sprintf(fbTextBuffer,"%d%%", data->percent);
		bool displaySpeed = data->speed != 0;
		drawString(displaySpeed ? (x1 + 80) : (640/2), middleY+30, fbTextBuffer, 1.0f, ALIGN_CENTER, defaultColor);
		if(displaySpeed) {
			formatBytes(fbTextBuffer, data->speed, 0, true);
			strcat(fbTextBuffer, "/s");
			drawString(x1 + 280, middleY+30, fbTextBuffer, 1.0f, ALIGN_CENTER, defaultColor);
			sprintf(fbTextBuffer,"Elapsed: %02i:%02i:%02i", data->timestart / 3600, (data->timestart / 60)%60,  data->timestart % 60);
			drawString(x1 + 500, middleY+30, fbTextBuffer, 0.65f, ALIGN_CENTER, defaultColor);
			sprintf(fbTextBuffer,"Remain: %02i:%02i:%02i", data->timeremain / 3600, (data->timeremain / 60)%60,  data->timeremain % 60);
			drawString(x1 + 500, middleY+45, fbTextBuffer, 0.65f, ALIGN_CENTER, defaultColor);
		}
	}	
}

// External
uiDrawObj_t* DrawProgressBar(bool indeterminate, int percent, const char *message) {
	drawProgressEvent_t *eventData = calloc(1, sizeof(drawProgressEvent_t));
	eventData->percent = percent;
	eventData->indeterminate = indeterminate;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_PROGRESS;
	event->data = eventData;
	DrawSetAnimation(event, UI_ANIM_POP, 320, 240);
	if(message && strlen(message) > 0) {
		sprintf(txtbuffer, "%s", message);
		// Add child component(s) for label(s)
		char *tok = strtok(txtbuffer,"\n");
		int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
		int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
		int middleY = (y2+y1)/2;
		while(tok != NULL) {
			DrawAddChild(event, DrawStyledLabel(640/2, middleY, tok, 1.0f, ALIGN_CENTER, defaultColor));
			tok = strtok(NULL,"\n");
			middleY+=24;
		}
	}
	return event;
}

uiDrawObj_t* DrawProgressLoading(int miniModePos) {
	drawProgressEvent_t *eventData = calloc(1, sizeof(drawProgressEvent_t));
	eventData->miniMode = true;
	eventData->miniModePos = miniModePos;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_PROGRESS;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawMessageBox(uiDrawObj_t *evt) {
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	
	GXColor fillColor = THEME_PANEL_DARK;
	GXColor borderColor = THEME_BORDER;
	
	_DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor); 
	
	// Coloured accent along the top tells the kind of message at a glance
	drawMsgBoxEvent_t *data = (drawMsgBoxEvent_t*)evt->data;
	GXColor accent;
	switch(data->type) {
		case D_WARN: accent = (GXColor) {255,190, 70,255}; break;
		case D_FAIL: accent = (GXColor) {255, 86, 86,255}; break;
		case D_PASS: accent = (GXColor) { 90,220,120,255}; break;
		default:     accent = THEME_ACCENT; break;
	}
	_DrawSolidRect(x1+16, y1+4, (x2-x1)-32, 3, accent);
}	

// External
uiDrawObj_t* DrawMessageBox(int type, const char *msg)
{
	drawMsgBoxEvent_t *eventData = calloc(1, sizeof(drawMsgBoxEvent_t));
	eventData->type = type;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_MSGBOX;
	event->data = eventData;
	DrawSetAnimation(event, UI_ANIM_POP, 320, 240);
	if(type == D_FAIL || type == D_WARN) {
		UISound_Play(SND_ERROR);
	}
	else if(type == D_PASS) {
		UISound_Play(SND_INFO);
	}
	
	// Add child component(s) for label(s)
	sprintf(txtbuffer, "%s", msg);
	char *tok = strtok(txtbuffer,"\n");
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	int middleY = y2-y1 < 23 ? y1+3 : (y2+y1)/2-12;
	while(tok != NULL) {
		uiDrawObj_t *lineLabel = DrawStyledLabel(640/2, middleY, tok, 1.0f, ALIGN_CENTER, defaultColor);
		tok = strtok(NULL,"\n");
		middleY+=24;
		DrawAddChild(event, lineLabel);
	}
	
	return event;
}

// Internal
static void _DrawSelectableButton(uiDrawObj_t *evt) {
	drawSelectableButtonEvent_t *data = (drawSelectableButtonEvent_t*)evt->data;
	int x1 = data->x1;
	int x2 = data->x2;
	GXColor selectColor = THEME_SELECT;
	GXColor noColor = (GXColor) {0,0,0,0}; //black
	GXColor borderColor = THEME_BORDER_DIM;
	
	int borderSize = 4;
	//determine length of the text ourselves if x2 == -1
	x2 = (x2 == -1) ? GetTextSizeInPixels(data->msg)+x1+(borderSize*2)+6 : x2;
	//Draw Text and backfill (if selected)
	if(data->mode==B_SELECTED) {
		_DrawGlassGlow(x1, data->y1, x2-x1, data->y2-data->y1+2, THEME_ACCENT);
		_DrawSimpleBox( x1, data->y1, x2-x1, data->y2-data->y1+2, 0, selectColor, THEME_BORDER);
	}
	else {
		_DrawSimpleBox( x1, data->y1, x2-x1, data->y2-data->y1+2, 0, noColor, borderColor);
	}
	
	if(data->msg) {
		float scale = GetTextScaleToFitInWidth(data->msg, (x2-x1)-(borderSize*2)-6);
		// Adjust font when we can't fit vertically too
		int availHeight = data->y2 - data->y1 - 4;
		if(GetFontHeight(scale) > availHeight) {
			int fullHeight = GetFontHeight(1.0f);
			scale = (float)availHeight / (float)fullHeight;
		}
		drawString(data->x1+borderSize+3, data->y1+(data->y2-data->y1)/2, data->msg, scale, ALIGN_LEFT, defaultColor);
	}
}

// External
uiDrawObj_t* DrawSelectableButton(int x1, int y1, int x2, int y2, const char *message, int mode)
{	
	drawSelectableButtonEvent_t *eventData = calloc(1, sizeof(drawSelectableButtonEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->mode = mode;
	if(message) {
		eventData->msg = strdup(message);
	}
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_SELECTABLEBUTTON;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawTooltip(uiDrawObj_t *evt) {

	drawTooltipEvent_t *data = (drawTooltipEvent_t*)evt->data;

	if(data->tooltip/* && data->tooltiptime >= 50*/) {
		//int alpha = data->tooltiptime*4 > 255 ? 255 : data->tooltiptime;
		int alpha = 255;
		int borderSize = 4;
		GXColor borderColorTT = (GXColor) {255,255,255,alpha};
		GXColor backColorTT = THEME_PANEL_DARK;
		backColorTT.a = alpha;
		int numLines = 1;
		char *strPtr = data->tooltip;
		for (numLines=1; strPtr[numLines]; strPtr[numLines]=='\n' ? numLines++ : *strPtr++);
		int height = numLines*26;
		int tooltipY1 = (getVideoMode()->efbHeight / 2) - (height/2);
		int tooltipX1 = 25, tooltipX2 = getVideoMode()->fbWidth-25, tooltipY2 = tooltipY1+height;
		_DrawSimpleBox( tooltipX1, tooltipY1-6, tooltipX2-tooltipX1, (tooltipY2-tooltipY1)+6, 0, backColorTT, THEME_BORDER);
		
		// Write each line
		strPtr = data->tooltip;
		int curLine = 0;
		while(numLines) {
			float scale = GetTextScaleToFitInWidthWithMax(strPtr, (tooltipX2-tooltipX1)-(borderSize*2)-6, 0.75f);
			drawString(tooltipX1+borderSize+3, tooltipY1+11+(curLine*25), strPtr, scale, ALIGN_LEFT, borderColorTT);
			numLines--;
			curLine++;
			// Increment to the next line if we have one.
			if(numLines > 0) {
				while(*strPtr != '\n') strPtr++;
				strPtr++;
			}
		}
	}
}

// External
uiDrawObj_t* DrawTooltip(const char *tooltip) {
	drawTooltipEvent_t *eventData = calloc(1, sizeof(drawTooltipEvent_t));
	if(tooltip && strlen(tooltip) > 0) {
		eventData->tooltip = strdup(tooltip);
	}
	else {
		eventData->tooltip = NULL;
	}
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TOOLTIP;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawStyledLabel(uiDrawObj_t *evt) {
	drawStyledLabelEvent_t *data = (drawStyledLabelEvent_t*)evt->data;
	const char *string = data->getString ? data->getString() : data->string;
	
	if(data->showCaret) {
		// blink the caret
		if(data->fadingDirection) {
			data->caretColor.a += (data->fadingDirection * 20);
			if(data->caretColor.a >= 255) { data->fadingDirection = -1; data->caretColor.a = 255; }
			else if(data->caretColor.a <= 15) { data->fadingDirection = 1; data->caretColor.a = 0; }
		}		
		drawStringWithCaret(data->x, data->y, string, data->size, data->align, data->color, data->caretPosition, data->caretColor);
	}
	else {
		if(data->fadingDirection) {
			data->color.a += data->fadingDirection;
			if(data->color.a >= 255) data->fadingDirection = -1;
			else if(data->color.a <= 15) data->fadingDirection = 1;
		}
		drawString(data->x, data->y, string, data->size, data->align, data->color);
	}
}

// External
uiDrawObj_t* DrawStyledLabel(int x, int y, const char *string, float size, int align, GXColor color)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	if(string && strlen(string) > 0) {
		eventData->string = strdup(string);
	}
	else {
		eventData->string = NULL;
	}
	eventData->size = size;
	eventData->align = align;
	eventData->color = color;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External
uiDrawObj_t* DrawStyledLabelWithCaret(int x, int y, const char *string, float size, int align, GXColor color, int caretPosition)
{	
	uiDrawObj_t *event = DrawStyledLabel(x, y, string, size, align, color);
	drawStyledLabelEvent_t *eventData = (drawStyledLabelEvent_t*)event->data;
	eventData->caretPosition = caretPosition;
	eventData->showCaret = true;
	eventData->fadingDirection = 1;
	eventData->caretColor = eventData->color;
	return event;
}

// External
uiDrawObj_t* DrawLabel(int x, int y, const char *string)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	if(string && strlen(string) > 0) {
		eventData->string = strdup(string);
	}
	else {
		eventData->string = NULL;
	}
	eventData->size = 1.0f;
	eventData->align = ALIGN_LEFT;
	eventData->color = defaultColor;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External
uiDrawObj_t* DrawFadingLabel(int x, int y, const char *string, float size)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	if(string && strlen(string) > 0) {
		eventData->string = strdup(string);
	}
	else {
		eventData->string = NULL;
	}
	eventData->size = size;
	eventData->align = ALIGN_LEFT;
	eventData->color = (GXColor) {255, 255, 255, 0};
	eventData->fadingDirection = 1;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External
uiDrawObj_t* DrawDynamicLabel(int x, int y, const char *(*getString)(void), float size, int align, GXColor color)
{	
	drawStyledLabelEvent_t *eventData = calloc(1, sizeof(drawStyledLabelEvent_t));
	eventData->x = x;
	eventData->y = y;
	eventData->getString = getString;
	eventData->size = size;
	eventData->align = align;
	eventData->color = color;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_STYLEDLABEL;
	event->data = eventData;
	return event;
}

// External (this is used to tie objects together, think of it as an invisible panel)
uiDrawObj_t* DrawContainer()
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_CONTAINER;
	return event;
}

// Internal
// Game card for the cover flow: banner, title, publisher, size and region
static void _DrawGameCard(drawFileBrowserButtonEvent_t *data) {
	file_handle *file = data->file;
	bool selected = data->mode == B_SELECTED;
	int w = data->x2 - data->x1;
	int x_mid = data->x1 + w/2;

	if(selected) {
		_DrawGlassGlow(data->x1, data->y1, w, data->y2-data->y1, THEME_ACCENT);
	}
	_DrawSimpleBox(data->x1, data->y1, w, data->y2-data->y1, 0, selected ? THEME_SELECT : THEME_PANEL, selected ? THEME_BORDER : THEME_BORDER_DIM);

	// Banner (96x32) at a whole multiple of its size so it stays sharp, or the file type icon until it has loaded
	int bnr_scale = MAX(1, (w - 20) / 96);
	int bnr_w = 96 * bnr_scale;
	int bnr_h = 32 * bnr_scale;
	int bnr_x = x_mid - bnr_w/2;
	int bnr_y = data->y1 + 12;
	if(file->meta && (file->meta->banner || file->meta->fileTypeTexObj)) {
		GXTexObj *texObj = file->meta->banner ? &file->meta->bannerTexObj : file->meta->fileTypeTexObj;
		int x = bnr_x, y = bnr_y, bw = bnr_w, bh = bnr_h;
		if(!file->meta->banner) {
			bh = bnr_h - 8;
			bw = bh * GX_GetTexObjWidth(texObj) / GX_GetTexObjHeight(texObj);
			x = x_mid - bw/2;
			y = bnr_y + 4;
		}
		else {
			GX_SetNumTevStages(1);
			GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
			// Pixel exact when the card faces the viewer, smooth while it is turned
			bool flat = !UI_IsPerspective() && UI_GetOverscan() == 1.0f;
			GX_InitTexObjFilterMode(texObj, flat ? GX_NEAR : GX_LINEAR, flat ? GX_NEAR : GX_LINEAR);
		}
		GX_InvalidateTexAll();
		GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
		if(tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
		GX_LoadTexObj(texObj, GX_TEXMAP0);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position3f32((float) x, (float) y, 0.0f);           GX_Color4u8(255, 255, 255, data->alpha); GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position3f32((float) (x+bw), (float) y, 0.0f);      GX_Color4u8(255, 255, 255, data->alpha); GX_TexCoord2f32(1.0f, 0.0f);
			GX_Position3f32((float) (x+bw), (float) (y+bh), 0.0f); GX_Color4u8(255, 255, 255, data->alpha); GX_TexCoord2f32(1.0f, 1.0f);
			GX_Position3f32((float) x, (float) (y+bh), 0.0f);      GX_Color4u8(255, 255, 255, data->alpha); GX_TexCoord2f32(0.0f, 1.0f);
		GX_End();
		drawInit();
	}

	// Title and publisher
	int text_y = bnr_y + bnr_h + 20;
	float scale = GetTextScaleToFitInWidthWithMax(data->displayName, w - 16, 0.75f);
	drawString(x_mid, text_y, data->displayName, scale, ALIGN_CENTER, defaultColor);
	if(file->meta && file->meta->banner) {
		sprintf(fbTextBuffer, "%.*s", BNR_FULL_TEXT_LEN, file->meta->bannerDesc.fullCompany);
		scale = GetTextScaleToFitInWidthWithMax(fbTextBuffer, w - 16, 0.5f);
		drawString(x_mid, text_y + 22, fbTextBuffer, scale, ALIGN_CENTER, accentColor);
	}

	// Size and region along the bottom
	formatBytes(fbTextBuffer, file->size, 0, !(file->device->location & LOC_SYSTEM));
	drawString(data->x1 + 12, data->y2 - 14, fbTextBuffer, 0.5f, ALIGN_LEFT, deSelectedColor);
	if(file->meta && file->meta->regionTexObj) {
		drawInit();
		_DrawTexObjNow(file->meta->regionTexObj, data->x2 - 44, data->y2 - 24, 32, 20, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
	}
}

// Internal
static void _DrawFileBrowserButton(uiDrawObj_t *evt) {
	if(((drawFileBrowserButtonEvent_t*)evt->data)->isGameCard) {
		_DrawGameCard((drawFileBrowserButtonEvent_t*)evt->data);
		return;
	}
	
	drawFileBrowserButtonEvent_t *data = (drawFileBrowserButtonEvent_t*)evt->data;
	int borderSize = 4;	
	if(data->isCarousel) {	
		// Every entry is a full card, the ones beside the middle are turned away in 3D
		GXColor noColor 	= data->distFromMiddle == 0 ? THEME_PANEL : THEME_PANEL_DARK;
		GXColor borderColor = data->distFromMiddle == 0 ? THEME_BORDER : THEME_BORDER_DIM;
		{
			if(data->distFromMiddle == 0) {
				_DrawGlassGlow(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, THEME_ACCENT);
			}
			_DrawSimpleBox(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, 0, noColor, borderColor);
			
			int x_mid = data->x2-((data->x2-data->x1)/2);
			int bnr_width = 96;
			int bnr_height = 32;
			file_handle *file = data->file;
			// Draw banner if there is one
			if(file->meta && (file->meta->banner || file->meta->fileTypeTexObj)) {
				GXTexObj *texObj = (file->meta->banner ? &file->meta->bannerTexObj : file->meta->fileTypeTexObj);
				bnr_width *= (file->meta->banner ? 2 : 1);
				bnr_height *= (file->meta->banner ? 2 : 1);
				if(file->meta->banner) {
					GX_SetNumTevStages(1);
					GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
				}
				GX_InvalidateTexAll();
				GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
				if(tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
				GX_LoadTexObj(texObj, GX_TEXMAP0);
				int bnr_x = x_mid - (bnr_width/2);
				GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
					GX_Position3f32((float) bnr_x,(float) data->y1+borderSize+40, 0.0f );
					GX_Color4u8(255, 255, 255, data->alpha);
					GX_TexCoord2f32(0.0f,0.0f);
					GX_Position3f32((float) (bnr_x+bnr_width),(float) data->y1+borderSize+40,0.0f );
					GX_Color4u8(255, 255, 255, data->alpha);
					GX_TexCoord2f32(1.0f,0.0f);
					GX_Position3f32((float) (bnr_x+bnr_width),(float) (data->y1+borderSize+40+bnr_height),0.0f );
					GX_Color4u8(255, 255, 255, data->alpha);
					GX_TexCoord2f32(1.0f,1.0f);
					GX_Position3f32((float) bnr_x,(float) (data->y1+borderSize+40+bnr_height),0.0f );
					GX_Color4u8(255, 255, 255, data->alpha);
					GX_TexCoord2f32(0.0f,1.0f);
				GX_End();
				
				if(data->isAutoLoadEntry) {
					drawInit();
					_DrawImageNow(TEX_STAR, bnr_x+bnr_width-16, data->y1+borderSize+40, 16, 16, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
				}
				
				// Company
				sprintf(fbTextBuffer, "%.*s", BNR_FULL_TEXT_LEN, file->meta->bannerDesc.fullCompany);
				float scale = GetTextScaleToFitInWidth(fbTextBuffer,(data->x2-data->x1)-(borderSize*2));
				drawString(x_mid, data->y1+(borderSize*2)+40+bnr_height+20, fbTextBuffer, scale, ALIGN_CENTER, defaultColor);
				
				// Description
				sprintf(fbTextBuffer, "%.*s", BNR_DESC_LEN, file->meta->bannerDesc.description);
				char* rest = &fbTextBuffer[0];
				char* tok;
				int line = 0;
				while ((tok = strtok_r (rest,"\r\n", &rest))) {
					scale = GetTextScaleToFitInWidthWithMax(tok,(data->x2-data->x1)-(borderSize*2), !line ? 1.0f : scale);
					drawString(x_mid, data->y1+(borderSize*2)+40+bnr_height+60+(line*scale*24), tok, scale, ALIGN_CENTER, defaultColor);
					line++;
				}
			}
			// Region
			if(file->meta && file->meta->regionTexObj) {
				drawString(data->x2 - 44, data->y2-(borderSize+41), "Region: ", 0.45f, ALIGN_RIGHT, defaultColor);
				drawInit();
				_DrawTexObjNow(file->meta->regionTexObj, data->x2 - 44, data->y2-(borderSize+50), 32, 20, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
			}
			
			// fullGameName displays some titles with incorrect encoding, use displayName instead
			float scale = GetTextScaleToFitInWidth(data->displayName, (data->x2-data->x1)-(borderSize*2));
			drawString(x_mid, data->y1+(borderSize*2)+10, data->displayName, scale, ALIGN_CENTER, defaultColor);
			
			// Print specific stats
			if(file->fileType==IS_FILE) {
				if(file->device == &__device_wode) {
					ISOInfo_t* isoInfo = (ISOInfo_t*)&file->other;
					sprintf(fbTextBuffer,"Partition: %i, ISO: %i", isoInfo->iso_partition,isoInfo->iso_number);
				}
				else if(file->device == &__device_card_a || file->device == &__device_card_b) {
					formatBytes(stpcpy(fbTextBuffer, "Size: "), file->size, 8192, false);
				}
				else if(file->device == &__device_qoob) {
					formatBytes(stpcpy(fbTextBuffer, "Size: "), file->size, 65536, false);
				}
				else {
					formatBytes(stpcpy(fbTextBuffer, "Size: "), file->size, 0, !(file->device->location & LOC_SYSTEM));
				}
				drawString(data->x2-(borderSize+8), data->y2-(borderSize+19), fbTextBuffer, 0.45f, ALIGN_RIGHT, defaultColor);
			}
		}
	}
	else {
		
		// Not selected
		GXColor noColor 	= (GXColor) {0,0,0,0};
		GXColor selectColor = THEME_SELECT;
		GXColor borderColor = data->mode == B_SELECTED ? THEME_BORDER : THEME_BORDER_DIM;

		if(data->mode == B_SELECTED) {
			_DrawGlassGlow(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, THEME_ACCENT);
		}
		_DrawSimpleBox(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, 
					0, data->mode == B_SELECTED ? selectColor : noColor, borderColor);
		
		// Draw banner if there is one
		file_handle *file = data->file;
		if(file->meta && (file->meta->banner || file->meta->fileTypeTexObj)) {
			GXTexObj *texObj = (file->meta->banner ? &file->meta->bannerTexObj : file->meta->fileTypeTexObj);
			if(file->meta->banner) {
				GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR0A0);
				GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
				GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_TEXA, GX_CA_ZERO);
				GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
			}
			GX_InvalidateTexAll();
			GXTlutObj *tlutObj = GX_GetTexObjUserData(texObj);
			if(tlutObj) GX_LoadTlut(tlutObj, GX_GetTexObjTlut(texObj));
			GX_LoadTexObj(texObj, GX_TEXMAP0);
			GX_LoadTexObj(&bannerMaskTexObj, GX_TEXMAP1);
			GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
				GX_Position3f32((float) data->x1+7,(float) data->y1+4, 0.0f );
				GX_Color4u8(255, 255, 255, data->alpha);
				GX_TexCoord2f32(0.0f,0.0f);
				GX_Position3f32((float) (data->x1+7+96),(float) data->y1+4,0.0f );
				GX_Color4u8(255, 255, 255, data->alpha);
				GX_TexCoord2f32(1.0f,0.0f);
				GX_Position3f32((float) (data->x1+7+96),(float) (data->y1+4+32),0.0f );
				GX_Color4u8(255, 255, 255, data->alpha);
				GX_TexCoord2f32(1.0f,1.0f);
				GX_Position3f32((float) data->x1+7,(float) (data->y1+4+32),0.0f );
				GX_Color4u8(255, 255, 255, data->alpha);
				GX_TexCoord2f32(0.0f,1.0f);
			GX_End();
			
			if(data->isAutoLoadEntry) {
				drawInit();
				_DrawImageNow(TEX_STAR, data->x1+7+96-16, data->y1+4, 16, 16, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
			}
		}
		if(file->meta && file->meta->regionTexObj) {
			drawInit();
			_DrawTexObjNow(file->meta->regionTexObj, data->x2 - 39, data->y1+borderSize+1, 32, 20, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
		}

		// fullGameName displays some titles with incorrect encoding, use displayName instead
		if(data->mode == B_SELECTED) {
			float scale = GetTextScaleToFitInWidthWithMax(data->displayName, (data->x2-data->x1-8-96-39)-(borderSize*2), 0.6f);
			drawString(data->x1+borderSize+8+96, data->y1+(data->y2-data->y1)/2, data->displayName, scale, ALIGN_LEFT, defaultColor);
		} else {
			drawStringEllipsis(data->x1+borderSize+8+96, data->y1+(data->y2-data->y1)/2, data->displayName, 0.6f, ALIGN_LEFT, defaultColor, false, (data->x2-data->x1-8-96-39)-(borderSize*2));
		}
		
		// Print specific stats
		if(file->fileType==IS_FILE) {
			if(file->device == &__device_wode) {
				ISOInfo_t* isoInfo = (ISOInfo_t*)&file->other;
				sprintf(fbTextBuffer,"Partition: %i, ISO: %i", isoInfo->iso_partition,isoInfo->iso_number);
			}
			else if(file->device == &__device_card_a || file->device == &__device_card_b) {
				formatBytes(fbTextBuffer, file->size, 8192, false);
			}
			else if(file->device == &__device_qoob) {
				formatBytes(fbTextBuffer, file->size, 65536, false);
			}
			else {
				formatBytes(fbTextBuffer, file->size, 0, !(file->device->location & LOC_SYSTEM));
			}
			drawString(data->x2-(borderSize+3), data->y1+borderSize+26, fbTextBuffer, 0.45f, ALIGN_RIGHT, defaultColor);
		}
	}
}

// External
uiDrawObj_t* DrawFileBrowserButton(int x1, int y1, int x2, int y2, const char *message, file_handle *file, int mode)
{
	drawFileBrowserButtonEvent_t *eventData = calloc(1, sizeof(drawFileBrowserButtonEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->displayName = strdup(message);
	eventData->mode = mode;
	eventData->file = calloc(1, sizeof(file_handle));
	memcpy(eventData->file, file, sizeof(file_handle));
	if(eventData->file->meta) {
		eventData->file->meta = calloc(1, sizeof(file_meta));
		memcpy(eventData->file->meta, file->meta, sizeof(file_meta));
		if(eventData->file->meta->banner && eventData->file->meta->bannerSum != 0xFFFF) {
			// Make a copy cause we want this one to be killed off when the display event is disposed
			eventData->file->meta->banner = memalign(32, eventData->file->meta->bannerSize);
			memcpy(eventData->file->meta->banner, file->meta->banner, eventData->file->meta->bannerSize);
			DCFlushRange(eventData->file->meta->banner, eventData->file->meta->bannerSize);
			GX_InitTexObjData(&eventData->file->meta->bannerTexObj, eventData->file->meta->banner);
			if(GX_GetTexObjUserData(&eventData->file->meta->bannerTexObj) == &file->meta->bannerTlutObj) {
				void *img_ptr;
				u16 wd, ht;
				u8 fmt, wrap_s, wrap_t, mipmap;
				GX_GetTexObjAll(&eventData->file->meta->bannerTexObj, &img_ptr, &wd, &ht, &fmt, &wrap_s, &wrap_t, &mipmap);
				GX_InitTlutObjData(&eventData->file->meta->bannerTlutObj, img_ptr + GX_GetTexBufferSize(wd, ht, fmt, mipmap, 0));
				GX_InitTexObjUserData(&eventData->file->meta->bannerTexObj, &eventData->file->meta->bannerTlutObj);
			}
		}
		else {
			eventData->file->meta->banner = NULL;
			eventData->file->meta->bannerSize = 0;
		}
		if(eventData->file->meta->displayName == file->meta->bannerDesc.gameName) {
			eventData->file->meta->displayName = eventData->file->meta->bannerDesc.gameName;
		}
		else if(eventData->file->meta->displayName == file->meta->bannerDesc.fullGameName) {
			eventData->file->meta->displayName = eventData->file->meta->bannerDesc.fullGameName;
		}
	}
	// Hide extension when rendering certain files
	if(eventData->file->fileType == IS_FILE) {
		char *fileName = endsWith(eventData->file->name, eventData->displayName);
		char *start = fileName ? eventData->displayName : getRelativeName(eventData->file->name);
		char *end;
		if((end = endsWith(start,".dol"))
			|| (end = endsWith(start,".dol+cli"))
			|| (end = endsWith(start,".elf"))
			|| (end = endsWith(start,".fdi"))
			|| (end = endsWith(start,".gci"))
			|| (end = endsWith(start,".gcm.gcm"))
			|| (end = endsWith(start,".gcm"))
			|| (end = endsWith(start,".gcs"))
			|| (end = endsWith(start,".nkit.iso.iso"))
			|| (end = endsWith(start,".nkit.iso"))
			|| (end = endsWith(start,".iso.iso"))
			|| (end = endsWith(start,".iso"))
			|| (end = endsWith(start,".mp3"))
			|| (end = endsWith(start,".sav"))
			|| (end = endsWith(start,".tgc"))) {
			if(fileName) {
				*end = '\0';
			}
			else if(memmem(eventData->displayName, strlen(eventData->displayName), start, end - start)) {
				end = mempcpy(eventData->displayName, start, end - start);
				*end = '\0';
			}
		}
	}
	eventData->alpha = (eventData->file->fileAttrib & ATTRIB_HIDDEN) || *getRelativeName(eventData->file->name) == '.' ? 128 : 255;
	eventData->isAutoLoadEntry = !strcmp(swissSettings.autoload, file->name) || !fnmatch(swissSettings.autoload, file->name, FNM_PATHNAME | FNM_PREFIX_DIRS);
	
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_FILEBROWSERBUTTON;
	event->data = eventData;
	return event;
}

uiDrawObj_t* DrawFileBrowserButtonMeta(int x1, int y1, int x2, int y2, const char *message, file_handle *file, int mode) {
	if(file->meta && file->meta->displayName) {
		message = file->meta->displayName;
	}
	return DrawFileBrowserButton(x1, y1, x2, y2, message, file, mode);
}

// Pose of a cover flow card that is rel places away from the (animated) middle
static void coverflowTransform(Mtx m, float rel, drawFileBrowserButtonEvent_t *card) {
	float side = rel < 0.0f ? -1.0f : 1.0f;
	float dist = fabsf(rel);
	float k = MIN(dist, 1.0f);
	float e = k * k * (3.0f - 2.0f * k);	// smoothstep into the turned pose
	float beyond = MAX(dist - 1.0f, 0.0f);
	float cx = (card->x1 + card->x2) / 2.0f, cy = (card->y1 + card->y2) / 2.0f;
	UI_MakeTransform(m, cx, cy, side * (e * 178.0f + beyond * 50.0f), 0.0f, -e * 170.0f - beyond * 26.0f, side * e * 1.0f, 0.0f, 1.0f);
}

// Internal
static void _DrawCoverflow(uiDrawObj_t *evt) {
	drawCoverflowEvent_t *data = (drawCoverflowEvent_t*)evt->data;
	if(!data->count) return;

	// Glide towards the selection, jump if it's far away (e.g. entering the view)
	if(fabsf(coverflowPos - data->selected) > 8.0f) coverflowPos = data->selected;
	coverflowPos += (data->selected - coverflowPos) * MIN(1.0f, Scene3D_FrameDelta() * 10.0f);

	Mtx base, m, full;
	bool basePerspective;
	UI_GetTransform(base, &basePerspective);

	int order[data->count];
	float rel[data->count];
	for(int i = 0; i < data->count; i++) {
		rel[i] = data->first + i - coverflowPos;
		order[i] = i;
	}
	for(int i = 1; i < data->count; i++) {
		for(int j = i; j > 0 && fabsf(rel[order[j]]) > fabsf(rel[order[j-1]]); j--) {
			int tmp = order[j]; order[j] = order[j-1]; order[j-1] = tmp;
		}
	}

	// Every card shares the same rectangle, only its pose differs
	drawFileBrowserButtonEvent_t *layout = (drawFileBrowserButtonEvent_t*)data->cards[0]->data;
	for(int pass = 0; pass < 2; pass++) {
		for(int k = 0; k < data->count; k++) {
			int i = order[k];
			drawFileBrowserButtonEvent_t *card = (drawFileBrowserButtonEvent_t*)data->cards[i]->data;
			coverflowTransform(m, rel[i], card);
			if(pass == 0) {
				// Mirror below the card for the reflection on the floor
				Mtx mirror, tmp;
				guMtxIdentity(mirror);
				mirror[1][1] = -1.0f;
				mirror[1][3] = 2.0f * card->y2 + 6.0f;
				guMtxConcat(m, mirror, tmp);
				guMtxCopy(tmp, m);
			}
			if(pass == 1 && fabsf(rel[i]) < 0.002f) {
				UI_SetTransform(base, basePerspective);	// settled in the middle, keep it pixel exact
			}
			else {
				guMtxConcat(base, m, full);
				UI_SetTransform(full, true);
			}
			drawInit();
			_DrawGameCard(card);
		}
		UI_SetTransform(base, basePerspective);
		if(pass == 0) {
			// Fade the reflections into the floor
			float fx = 320.0f, fy = layout->y2 + 3.0f, fz = 0.0f;
			UI_TransformPoint(&fx, &fy, &fz);
			Scene3D_DrawGradientRect(0, fy, 640, 480 - fy, (GXColor) {8,6,28,70}, (GXColor) {8,6,28,245});
			drawInit();
		}
	}
}

// External
// iTunes style cover flow of game cards around the selected one
uiDrawObj_t* DrawCoverflow(file_handle **files, int numFiles, int selected)
{
	int first = MAX(0, selected - 6);
	int last = MIN(numFiles - 1, selected + 6);
	drawCoverflowEvent_t *eventData = calloc(1, sizeof(drawCoverflowEvent_t));
	eventData->selected = selected;
	eventData->first = first;
	eventData->count = numFiles > 0 ? last - first + 1 : 0;
	eventData->cards = calloc(eventData->count ? eventData->count : 1, sizeof(uiDrawObj_t*));
	for(int i = 0; i < eventData->count; i++) {
		file_handle *file = files[first + i];
		uiDrawObj_t *card = DrawFileBrowserButtonMeta(200, 120, 440, 318, getRelativeName(file->name), file, first + i == selected ? B_SELECTED : B_NOSELECT);
		((drawFileBrowserButtonEvent_t*)card->data)->isGameCard = true;
		eventData->cards[i] = card;
	}
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_COVERFLOW;
	event->data = eventData;
	return event;
}

uiDrawObj_t* DrawFileCarouselEntry(int x1, int y1, int x2, int y2, const char *message, file_handle *file, int distFromMiddle) {
	uiDrawObj_t* event = DrawFileBrowserButtonMeta(x1, y1, x2, y2, message, file, B_SELECTED);
	drawFileBrowserButtonEvent_t *data = (drawFileBrowserButtonEvent_t*)event->data;
	data->isCarousel = true;
	data->distFromMiddle = distFromMiddle;
	if(distFromMiddle != 0) {
		// Cover-flow: turn the card towards the middle and push it back
		int side = distFromMiddle < 0 ? -1 : 1;
		int dist = abs(distFromMiddle);
		float cx = (x1 + x2) / 2.0f, cy = (y1 + y2) / 2.0f;
		UI_MakeTransform(event->xform, cx, cy, side * (240.0f + (dist-1) * 44.0f), 0.0f, -150.0f - (dist-1) * 40.0f, side * 0.95f, 0.0f, 0.92f);
		event->hasXform = true;
	}
	//print_debug("message %s dist = %i x: (%i -> %i) y: (%i -> %i)\n", message, distFromMiddle, x1, x2, y1, y2);
	return event;
}

// Internal
static void _DrawEmptyBox(uiDrawObj_t *evt) {
	drawBoxEvent_t *data = (drawBoxEvent_t*)evt->data;
	
	GXColor borderColor = THEME_BORDER;
	
	_DrawSimpleBox(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, 0, data->backfill, borderColor);
}

// External
uiDrawObj_t* DrawEmptyBox(int x1, int y1, int x2, int y2) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;
	
	drawBoxEvent_t *eventData = calloc(1, sizeof(drawBoxEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->backfill = THEME_PANEL_DARK;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_EMPTYBOX;
	event->data = eventData;
	return event;
}

// External
uiDrawObj_t* DrawEmptyColouredBox(int x1, int y1, int x2, int y2, GXColor colour) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;
	
	drawBoxEvent_t *eventData = calloc(1, sizeof(drawBoxEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->backfill = colour;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_EMPTYBOX;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawTransparentBox(uiDrawObj_t *evt) {
	drawBoxEvent_t *data = (drawBoxEvent_t*)evt->data;
	
	GXColor borderColor = THEME_BORDER_DIM;
	
	_DrawSimpleBox( data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, 0, data->backfill, borderColor);
}

// External
uiDrawObj_t* DrawTransparentBox(int x1, int y1, int x2, int y2) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;

	drawBoxEvent_t *eventData = calloc(1, sizeof(drawBoxEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	eventData->backfill = (GXColor) {0,0,0,0};
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TRANSPARENTBOX;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawScene3D(uiDrawObj_t *evt) {
	if(customBackdrop) {
		_DrawImageNow(TEX_BACKDROP, 0, 0, 640, 480, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
		return;
	}
	Scene3D_DrawBackground();
}

// External
uiDrawObj_t* DrawScene3D()
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_SCENE3D;
	return event;
}

// Internal
static void _DrawPageHeader(uiDrawObj_t *evt) {
	drawPageHeaderEvent_t *data = (drawPageHeaderEvent_t*)evt->data;
	drawString(data->x, data->y, data->title, 1.0f, ALIGN_LEFT, defaultColor);
	if(data->pageCount > 1) {
		// One small cube per page, the current one is lit up and spins
		float t = Scene3D_Time();
		cube3d_t cubes[data->pageCount];
		memset(cubes, 0, sizeof(cubes));
		for(int i = 0; i < data->pageCount; i++) {
			bool current = i == data->page;
			cubes[i].x = 600.0f - (data->pageCount-1-i)*20.0f;
			cubes[i].y = data->y + 1.0f;
			cubes[i].z = 0.0f;
			UI_TransformPoint(&cubes[i].x, &cubes[i].y, &cubes[i].z);
			cubes[i].size = (current ? 12.0f : 8.0f) * UI_GetOverscan();
			cubes[i].rx = 0.5f;
			cubes[i].ry = current ? t*1.5f : 0.6f;
			cubes[i].rz = 0.0f;
			cubes[i].color = current ? (GXColor) {150,128,255,240} : (GXColor) {90,84,170,150};
			cubes[i].edges = 0.7f;
		}
		Scene3D_DrawCubes(cubes, data->pageCount);
	}
}

// External
// Page title with a page indicator on the right, page is 0 based
uiDrawObj_t* DrawPageHeader(int x, int y, const char *title, int page, int pageCount)
{
	drawPageHeaderEvent_t *eventData = calloc(1, sizeof(drawPageHeaderEvent_t));
	eventData->x = x;
	eventData->y = y;
	eventData->title = strdup(title);
	eventData->page = page;
	eventData->pageCount = pageCount;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_PAGEHEADER;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawSelectionBar(uiDrawObj_t *evt) {
	drawBoxEvent_t *data = (drawBoxEvent_t*)evt->data;
	float pulse = 0.75f + 0.25f*sinf(Scene3D_Time()*4.0f);
	GXColor fillColor = THEME_SELECT;
	fillColor.a = (u8)(fillColor.a * pulse);
	_DrawGlassGlow(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, THEME_ACCENT);
	_DrawSimpleBox(data->x1, data->y1, data->x2-data->x1, data->y2-data->y1, 0, fillColor, THEME_BORDER);
}

// External
// Highlight behind the selected row of a list
uiDrawObj_t* DrawSelectionBar(int x1, int y1, int x2, int y2)
{
	drawBoxEvent_t *eventData = calloc(1, sizeof(drawBoxEvent_t));
	eventData->x1 = x1;
	eventData->y1 = y1;
	eventData->x2 = x2;
	eventData->y2 = y2;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_SELECTBAR;
	event->data = eventData;
	return event;
}

// Internal
static void _DrawTitleBar(uiDrawObj_t *evt) {
	_DrawSimpleBox(18, 16, 604, 62, 0, (GXColor) {40,32,112,150}, THEME_BORDER);
	float logoX = 52.0f, logoY = 47.0f, logoZ = 0.0f;
	UI_TransformPoint(&logoX, &logoY, &logoZ);
	Scene3D_DrawLogoCube(logoX, logoY, 30.0f * UI_GetOverscan(), 1.0f);

	drawInit();
	_DrawImageNow(TEX_SWISS, 80, 32, 96, 32, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
	drawString(186, 38, "for GameCube", 0.625f, ALIGN_LEFT, defaultColor);
	drawString(186, 57, "version 0.6", 0.5f, ALIGN_LEFT, deSelectedColor);
	
	sprintf(fbTextBuffer, "commit: %s \267 revision: %s", GIT_COMMIT, GIT_REVISION);
	drawString(getVideoMode()->fbWidth-36, 57, fbTextBuffer, 0.5f, ALIGN_RIGHT, deSelectedColor);
	
	s8 cputemp = SYS_GetCoreTemperature();
	if(cputemp >= 0) {
		sprintf(fbTextBuffer, "%i\260C", cputemp);
		drawString(getVideoMode()->fbWidth-246, 38, fbTextBuffer, 0.625f, ALIGN_CENTER, defaultColor);
	}
	time_t curtime;
	if(time(&curtime) != (time_t)-1) {
		strftime(fbTextBuffer, sizeof(fbTextBuffer), swissSettings.sramLanguage == SYS_LANG_ENGLISH_US ? "%D \267 %r" : "%F \267 %T", localtime(&curtime));
		drawString(getVideoMode()->fbWidth-36, 38, fbTextBuffer, 0.625f, ALIGN_RIGHT, defaultColor);
	}
}

// External
uiDrawObj_t* DrawTitleBar()
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_TITLEBAR;
	return event;
}

static const struct {
	const char *label;
	const char *desc;
	GXColor color;
} homeItems[MENU_MAX] = {
	[MENU_GAMES]    = {"Games",       "Your GameCube games on this device",     {255, 176,  72, 0}},
	[MENU_FILES]    = {"Files",       "Browse the folders and files on this device", {176, 112, 255, 0}},
	[MENU_DEVICE]   = {"Devices",     "Choose where to browse games and files", {120,  92, 255, 0}},
	[MENU_SETTINGS] = {"Settings",    "Configure Swiss and per-game options",   { 70, 130, 255, 0}},
	[MENU_INFO]     = {"System Info", "Console, device and version details",    { 40, 190, 210, 0}},
	[MENU_REFRESH]  = {"Refresh",     "Rescan the current device",              { 90, 200, 120, 0}},
	[MENU_EXIT]     = {"Exit",        "Leave Swiss and reboot the console",     {240, 100,  96, 0}},
};

static GXTexObj *homeIcon(int item, float *aspect) {
	switch(item) {
		case MENU_GAMES:    *aspect = 1.0f; return &btngamesTexObj;
		case MENU_FILES:    *aspect = 1.0f; return &btnfilesTexObj;
		case MENU_DEVICE:   *aspect = (float)BTNDEVICE_WIDTH / BTNDEVICE_HEIGHT;     return &btndeviceTexObj;
		case MENU_SETTINGS: *aspect = (float)BTNSETTINGS_WIDTH / BTNSETTINGS_HEIGHT; return &btnsettingsTexObj;
		case MENU_INFO:     *aspect = (float)BTNINFO_WIDTH / BTNINFO_HEIGHT;         return &btninfoTexObj;
		case MENU_REFRESH:  *aspect = (float)BTNREFRESH_WIDTH / BTNREFRESH_HEIGHT;   return &btnrefreshTexObj;
		default:            *aspect = (float)BTNEXIT_WIDTH / BTNEXIT_HEIGHT;         return &btnexitTexObj;
	}
}

// Controller button glyphs for the hint bars, drawn in the GameCube pad's colours and shapes.
// Each glyph character is one button: A B X Y Z L R, S = Start, D = D-Pad.
typedef struct {
	const char *glyphs;
	const char *label;
} ButtonHint;

typedef struct {
	float w, h, r;
	GXColor fill;
	GXColor text;
	const char *letter;
	float letterScale;
} ButtonGlyph;

static ButtonGlyph buttonGlyph(char button) {
	GXColor white = (GXColor) {255,255,255,255};
	GXColor grey = (GXColor) {196,196,204,255};
	GXColor dark = (GXColor) {56,56,68,255};
	switch(button) {
		case 'A': return (ButtonGlyph) {20, 20, 10, (GXColor) {24,176,112,255}, white, "A", 0.6f};
		case 'B': return (ButtonGlyph) {16, 16,  8, (GXColor) {222,40,48,255}, white, "B", 0.5f};
		case 'X': return (ButtonGlyph) {14, 20,  7, grey, dark, "X", 0.5f};
		case 'Y': return (ButtonGlyph) {20, 14,  7, grey, dark, "Y", 0.5f};
		case 'Z': return (ButtonGlyph) {26, 13, 6.5f, (GXColor) {104,80,208,255}, white, "Z", 0.45f};
		case 'L': return (ButtonGlyph) {22, 15,  5, grey, dark, "L", 0.5f};
		case 'R': return (ButtonGlyph) {22, 15,  5, grey, dark, "R", 0.5f};
		case 'S': return (ButtonGlyph) {36, 13, 6.5f, grey, dark, "START", 0.36f};
		default:  return (ButtonGlyph) {20, 20,  0, grey, dark, NULL, 0.0f};	// D-Pad
	}
}

// Filled rounded rectangle centred on (cx,cy) as a triangle fan, shaded top -> bottom
static void _drawRoundedFill(float cx, float cy, float w, float h, float r, GXColor top, GXColor bottom) {
	const int seg = 6;
	float x0 = cx - w/2 + r, x1 = cx + w/2 - r;
	float y0 = cy - h/2 + r, y1 = cy + h/2 - r;
	float corners[4][3] = {
		{x1, y1, 0.0f}, {x0, y1, M_PI/2}, {x0, y0, M_PI}, {x1, y0, M_PI*1.5f}
	};
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 2 + 4*(seg+1));
	GXColor mid = mixColor(top, bottom, 0.5f, (top.a + bottom.a) / 2);
	GX_Position3f32(cx, cy, 0.0f); GX_Color4u8(mid.r, mid.g, mid.b, mid.a); GX_TexCoord2f32(0.0f, 0.0f);
	for(int c = 0; c < 4; c++) {
		for(int i = 0; i <= seg; i++) {
			float a = corners[c][2] + (M_PI/2) * i / seg;
			float px = corners[c][0] + cosf(a) * r;
			float py = corners[c][1] + sinf(a) * r;
			float t = (py - (cy - h/2)) / h;
			GXColor col = mixColor(top, bottom, t, top.a + (bottom.a - top.a) * t);
			GX_Position3f32(px, py, 0.0f); GX_Color4u8(col.r, col.g, col.b, col.a); GX_TexCoord2f32(0.0f, 0.0f);
		}
	}
	// Close the fan back on the first rim vertex
	float t = (y1 - (cy - h/2)) / h;
	GXColor col = mixColor(top, bottom, t, top.a + (bottom.a - top.a) * t);
	GX_Position3f32(x1 + r, y1, 0.0f); GX_Color4u8(col.r, col.g, col.b, col.a); GX_TexCoord2f32(0.0f, 0.0f);
	GX_End();
}

static void _drawGlyphShape(float cx, float cy, float w, float h, float r, GXColor fill, bool dpad, float alpha) {
	GXColor white = (GXColor) {255,255,255,255};
	GXColor black = (GXColor) {0,0,0,255};
	GXColor rim = (GXColor) {16,12,36,(u8)(200*alpha)};
	GXColor top = mixColor(fill, white, 0.3f, 255*alpha);
	GXColor bottom = mixColor(fill, black, 0.25f, 255*alpha);
	if(dpad) {
		float arm = w * 0.36f;
		_drawRoundedFill(cx, cy, w + 2, arm + 2, 2.5f, rim, rim);
		_drawRoundedFill(cx, cy, arm + 2, h + 2, 2.5f, rim, rim);
		_drawRoundedFill(cx, cy, w, arm, 1.5f, top, bottom);
		_drawRoundedFill(cx, cy, arm, h, 1.5f, top, bottom);
		// Arrow nubs on each arm
		GXColor nub = (GXColor) {56,56,68,(u8)(200*alpha)};
		float d = w/2 - arm*0.45f;
		_drawRoundedFill(cx - d, cy, 2.5f, 2.5f, 1.25f, nub, nub);
		_drawRoundedFill(cx + d, cy, 2.5f, 2.5f, 1.25f, nub, nub);
		_drawRoundedFill(cx, cy - d, 2.5f, 2.5f, 1.25f, nub, nub);
		_drawRoundedFill(cx, cy + d, 2.5f, 2.5f, 1.25f, nub, nub);
		return;
	}
	_drawRoundedFill(cx, cy + 1, w + 2, h + 2, r + 1, rim, rim);
	_drawRoundedFill(cx, cy, w, h, r, top, bottom);
	// Specular highlight across the upper half
	GXColor shine = (GXColor) {255,255,255,(u8)(70*alpha)};
	GXColor clear = (GXColor) {255,255,255,0};
	float hr = MIN(r, h*0.25f);
	_drawRoundedFill(cx, cy - h*0.22f, w*0.7f, h*0.4f, hr, shine, clear);
}

static float buttonHintWidth(const ButtonHint *hint, float scale) {
	float w = 0;
	for(const char *g = hint->glyphs; *g; g++) {
		w += buttonGlyph(*g).w * scale + (g[1] ? 3 : 0);
	}
	return w + 6 * scale + GetTextSizeInPixels(hint->label) * 0.625f * scale;
}

// Row of [glyph] label pairs centred on (cx,cy), shrunk to fit maxWidth
static void drawButtonHints(int cx, int cy, int maxWidth, const ButtonHint *hints, int count, GXColor labelColor, float alpha) {
	const float spacing = 26;
	float total = 0;
	for(int i = 0; i < count; i++) {
		total += buttonHintWidth(&hints[i], 1.0f) + (i ? spacing : 0);
	}
	float scale = total > maxWidth ? maxWidth / total : 1.0f;
	float x = cx - total * scale / 2;
	for(int i = 0; i < count; i++) {
		for(const char *g = hints[i].glyphs; *g; g++) {
			ButtonGlyph bg = buttonGlyph(*g);
			float w = bg.w * scale, h = bg.h * scale;
			drawInit();
			GX_SetNumTevStages(1);
			GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
			GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
			GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
			_drawGlyphShape(x + w/2, cy, w, h, bg.r * scale, bg.fill, bg.letter == NULL, alpha);
			drawInit();
			if(bg.letter) {
				GXColor text = bg.text;
				text.a = (u8)(255*alpha);
				drawString(x + w/2, cy, bg.letter, bg.letterScale * scale, ALIGN_CENTER, text);
			}
			x += w + (g[1] ? 3 : 0);
		}
		x += 6 * scale;
		drawString(x, cy, hints[i].label, 0.625f * scale, ALIGN_LEFT, labelColor);
		x += GetTextSizeInPixels(hints[i].label) * 0.625f * scale + spacing * scale;
	}
	drawInit();
}

// IPL style ring of cubes, one per menu entry
static void drawHomeMenu(int selection) {
	float t = Scene3D_Time();
	float hb = homeBlend;
	float lift = (1.0f-hb) * 80.0f;
	cube3d_t cubes[MENU_MAX] = {0};
	int order[MENU_MAX];

	for(int i = 0; i < MENU_MAX; i++) {
		float rel = i - ringPos;
		while(rel >  MENU_MAX/2.0f) rel -= MENU_MAX;
		while(rel < -MENU_MAX/2.0f) rel += MENU_MAX;
		float theta = rel * (2.0f*M_PI/MENU_MAX);
		float front = (cosf(theta)+1.0f) * 0.5f;
		float emph = powf(front, 6.0f);
		cube3d_t *c = &cubes[i];
		c->x = 320.0f + sinf(theta)*235.0f;
		c->y = 236.0f - (1.0f-cosf(theta))*22.0f + lift;
		c->z = (cosf(theta)-1.0f)*190.0f - (1.0f-hb)*300.0f;
		// The selected cube comes forward a little
		c->size = 74.0f + 34.0f*emph;
		c->z += 45.0f*emph;
		if(i == selection) {
			c->size *= 1.0f + 0.03f*sinf(t*4.0f);
		}
		c->rx = 0.42f;
		c->ry = -theta*0.7f + emph*sinf(t*1.4f)*0.2f;
		c->rz = 0.0f;
		if(i == activateItem) {
			float at = (float)ticks_to_millisecs(gettime() - activateStart) / 1000.0f;
			float jump = 0.0f, spin = 0.0f;
			if(at < 0.12f) {
				c->squash = 0.28f * sinf(at / 0.12f * M_PI / 2.0f);	// crouch
			}
			else if(at < 0.50f) {
				float q = (at - 0.12f) / 0.38f;
				jump = 4.0f * q * (1.0f - q) * 85.0f;				// up and down again
				spin = (q * q * (3.0f - 2.0f * q)) * 2.0f * M_PI;	// one full turn
				c->squash = 0.28f * (1.0f - q) * (1.0f - q) - 0.14f * sinf(q * M_PI);
			}
			else if(at < 0.70f) {
				c->squash = 0.22f * sinf((at - 0.50f) / 0.20f * M_PI);	// land
			}
			c->y -= jump;
			c->y += c->size * c->squash * 0.5f;	// keep the bottom on the floor while squashing
			c->ry += spin;
		}
		c->color = homeItems[i].color;
		c->color.a = (u8)((110.0f + 120.0f*front) * hb);
		c->edges = 0.5f + 0.5f*emph;
		order[i] = i;
	}
	for(int i = 1; i < MENU_MAX; i++) {
		for(int j = i; j > 0 && cubes[order[j]].z < cubes[order[j-1]].z; j--) {
			int tmp = order[j]; order[j] = order[j-1]; order[j-1] = tmp;
		}
	}

	int sel = selection >= 0 ? selection : 0;
	float pulse = 0.75f + 0.25f*sinf(t*3.0f);
	GXColor glow = homeItems[sel].color;
	glow.r = (u8)(glow.r * pulse * hb);
	glow.g = (u8)(glow.g * pulse * hb);
	glow.b = (u8)(glow.b * pulse * hb);
	glow.a = 255;
	// Glossy floor: faded reflections of the ring with a line of light along it
	float floorY = 300.0f + lift;
	Scene3D_DrawReflections(cubes, MENU_MAX, floorY, 0.32f);
	Scene3D_DrawGradientRect(0, floorY, 640, 480 - floorY, (GXColor) {8,6,28,(u8)(40*hb)}, (GXColor) {8,6,28,(u8)(235*hb)});
	Scene3D_DrawGlow(Scene3D_GlowTexture(), 320.0f, floorY, 0.0f, 600.0f, 26.0f, (GXColor) {(u8)(70*hb), (u8)(60*hb), (u8)(130*hb), 255});
	Scene3D_DrawGlow(Scene3D_GlowTexture(), 320.0f, 236.0f + lift, -140.0f, 380.0f, 300.0f, glow);

	for(int i = 0; i < MENU_MAX; i++) {
		int item = order[i];
		float aspect;
		GXTexObj *icon = homeIcon(item, &aspect);
		Scene3D_DrawCubes(&cubes[item], 1);
		Scene3D_DrawCubeIcon(&cubes[item], icon, aspect, hb);
	}

	GXColor title = (GXColor) {255,255,255,(u8)(255*hb)};
	GXColor sub = (GXColor) {190,180,255,(u8)(230*hb)};
	GXColor border = THEME_BORDER;
	border.a = (u8)(border.a*hb);
	drawInit();
	_DrawSimpleBox(40, 436, 560, 32, 0, (GXColor) {40,32,112,(u8)(150*hb)}, border);
	drawString(320, 356 + lift, homeItems[sel].label, 1.0f, ALIGN_CENTER, title);
	drawString(320, 384 + lift, homeItems[sel].desc, 0.625f, ALIGN_CENTER, sub);
	if(devices[DEVICE_CUR] != NULL) {
		sprintf(fbTextBuffer, "Current device: %s", devices[DEVICE_CUR]->deviceName);
		drawString(320, 108, fbTextBuffer, 0.625f, ALIGN_CENTER, sub);
	}
	ButtonHint hints[4] = {
		{"D", "Select"},
		{"A", "Open"},
		{"B", "Back"},
		{"S", "Recent"},
	};
	drawButtonHints(320, 452, 540, hints, swissSettings.recentListLevel > 0 ? 4 : 3, sub, hb);
}

// Hint bar shown along the bottom while browsing files
static void drawBrowserDock(float alpha) {
	GXColor sub = (GXColor) {200,192,255,(u8)(240*alpha)};
	GXColor border = THEME_BORDER;
	border.a = (u8)(border.a*alpha);
	_DrawSimpleBox(40, 436, 560, 32, 0, (GXColor) {40,32,112,(u8)(150*alpha)}, border);
	ButtonHint hints[5];
	int count = 0;
	if(gamesMode) {
		hints[count++] = (ButtonHint) {"A", "Play"};
		hints[count++] = (ButtonHint) {"B", "Home Menu"};
		hints[count++] = (ButtonHint) {"LR", "Jump"};
	}
	else {
		hints[count++] = (ButtonHint) {"A", "Open"};
		hints[count++] = (ButtonHint) {"B", "Home Menu"};
		hints[count++] = (ButtonHint) {"X", "Parent Folder"};
		if(swissSettings.enableFileManagement) {
			hints[count++] = (ButtonHint) {"Z", "Manage"};
		}
	}
	if(swissSettings.recentListLevel > 0) {
		hints[count++] = (ButtonHint) {"S", "Recent"};
	}
	drawButtonHints(320, 452, 540, hints, count, sub, alpha);
}

// Internal
static void _DrawMenuButtons(uiDrawObj_t *evt) {
	drawMenuButtonsEvent_t *data = (drawMenuButtonsEvent_t*)evt->data;
	float dt = Scene3D_FrameDelta();

	static bool homeShown = false;
	if((data->selection >= 0) != homeShown) {
		homeShown = data->selection >= 0;
		if(homeShown) UISound_Play(SND_OPEN);
	}

	// Ease towards the home menu / file browser and the selected entry
	float target = data->selection >= 0 ? 1.0f : 0.0f;
	homeBlend += (target - homeBlend) * MIN(1.0f, dt*9.0f);
	if(fabsf(target - homeBlend) < 0.002f) homeBlend = target;
	if(data->selection >= 0) {
		float diff = data->selection - ringPos;
		while(diff >  MENU_MAX/2.0f) diff -= MENU_MAX;
		while(diff < -MENU_MAX/2.0f) diff += MENU_MAX;
		ringPos += diff * MIN(1.0f, dt*10.0f);
		if(ringPos < 0.0f) ringPos += MENU_MAX;
		if(ringPos >= MENU_MAX) ringPos -= MENU_MAX;
	}

	if(homeBlend < 0.99f) {
		drawBrowserDock(1.0f - homeBlend);
	}
	if(homeBlend > 0.01f) {
		drawHomeMenu(data->selection);
	}
}

// External
// Buttons
uiDrawObj_t* DrawMenuButtons(int selection) 
{
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	drawMenuButtonsEvent_t *eventData = calloc(1, sizeof(drawMenuButtonsEvent_t));
	eventData->selection = selection;
	event->type = EV_MENUBUTTONS;
	event->data = eventData;
	return event;
}

// External
// Play the jump for the selected home menu cube, wait with DrawHomeActivating() before switching screens
void DrawHomeActivate(int selection)
{
	activateStart = gettime();
	activateItem = selection;
}

bool DrawHomeActivating()
{
	return activateItem >= 0 && (float)ticks_to_millisecs(gettime() - activateStart) / 1000.0f < ACTIVATE_JUMP_END;
}

// Fade over everything while the home menu hands over to the next screen
static void drawActivateFade(void)
{
	if(activateItem < 0) return;
	float at = (float)ticks_to_millisecs(gettime() - activateStart) / 1000.0f;
	float alpha = 0.0f;
	if(at >= ACTIVATE_END) {
		activateItem = -1;
		return;
	}
	if(at > 0.55f && at < ACTIVATE_JUMP_END) alpha = (at - 0.55f) / (ACTIVATE_JUMP_END - 0.55f);
	else if(at >= ACTIVATE_JUMP_END) alpha = 1.0f - MAX(0.0f, at - 0.95f) / (ACTIVATE_END - 0.95f);
	if(alpha <= 0.0f) return;
	GXColor c = (GXColor) {5, 4, 18, (u8)(255.0f * MIN(alpha, 1.0f))};
	Scene3D_DrawGradientRect(0, 0, 640, 480, c, c);
}

// External
// Container for the file browser, it recedes into the scene while the home menu is open
uiDrawObj_t* DrawFilePanel()
{
	uiDrawObj_t *event = DrawContainer();
	DrawSetAnimation(event, UI_ANIM_FILEPANEL, 320, 260);
	return event;
}

// External
void DrawSetAnimation(uiDrawObj_t *evt, int anim, float cx, float cy)
{
	evt->anim = anim;
	evt->cx = cx;
	evt->cy = cy;
	evt->born = gettime();
}

void DrawUpdateProgressBar(uiDrawObj_t *evt, int percent) {
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	data->percent = percent;
}

void DrawUpdateProgressBarDetail(uiDrawObj_t *evt, int percent, int speed, int timestart, int timeremain) {
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	data->percent = percent;
	data->speed = speed;
	data->timestart = timestart;
	data->timeremain = timeremain;
}

void DrawUpdateProgressLoading(uiDrawObj_t *evt, int increment) {
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
	data->speed += increment;
}

void DrawUpdateMenuButtons(int selection) {
	drawMenuButtonsEvent_t *data = (drawMenuButtonsEvent_t*)buttonPanel->data;
	data->selection = selection;
}

void DrawUpdateFileBrowserButton(uiDrawObj_t *evt, int mode) {
	drawFileBrowserButtonEvent_t *data = (drawFileBrowserButtonEvent_t*)evt->data;
	data->mode = mode;
}

// Internal
static void _DrawVertScrollBar(uiDrawObj_t *evt) {
	drawVertScrollbarEvent_t *data = (drawVertScrollbarEvent_t*)evt->data;
	int x1 = data->x;
	int x2 = data->x+data->width;
	int y1 = data->y;
	int y2 = data->y+data->height;
	int scrollStartY = y1+3 + (int)((data->height-6-data->scrollHeight)*data->scrollPercent);

	if(scrollStartY > y2-3-data->scrollHeight)
		scrollStartY = y2-3-data->scrollHeight;
	
	GXColor fillColor = THEME_SELECT;
	GXColor noColor = (GXColor) {0,0,0,0}; //blank
	GXColor borderColor = THEME_BORDER_DIM;
	
	_DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, noColor, borderColor);
	
	_DrawSimpleBox( x1, scrollStartY,
			data->width, data->scrollHeight, 0, fillColor, borderColor); 
}

// External
uiDrawObj_t* DrawVertScrollBar(int x, int y, int width, int height, float scrollPercent, int scrollHeight) {
	scrollHeight = scrollHeight < 10 ? 10:scrollHeight;
	drawVertScrollbarEvent_t *eventData = calloc(1, sizeof(drawVertScrollbarEvent_t));
	eventData->x = x;
	eventData->y = y;
	eventData->width = width;
	eventData->height = height;
	eventData->scrollPercent = scrollPercent;
	eventData->scrollHeight = scrollHeight;
	uiDrawObj_t *event = calloc(1, sizeof(uiDrawObj_t));
	event->type = EV_VERTSCROLLBAR;
	event->data = eventData;
	return event;
}

static uiDrawObj_t* drawParameterForArgsSelector(Parameter *param, int x, int y, int selected) {

	uiDrawObj_t* container = DrawContainer();
	char *name = &param->arg.name[0];
	char *selValue = &param->values[param->currentValueIdx].name[0];
	
	int chkWidth = 32, nameWidth = 300, gapWidth = 13, paramWidth = 120;
	// [32px 10px 250px 10px 5px 80px 5px]
	// If not selected and not enabled, use greyed out font for everything
	GXColor fontColor = (param->enable || selected) ? defaultColor : deSelectedColor;

	// If selected draw that it's selected
	if(selected) DrawAddChild(container, DrawTransparentBox( x+chkWidth+gapWidth, y, getVideoMode()->fbWidth-52, y+30));
	DrawAddChild(container, DrawImage(param->enable ? TEX_CHECKED:TEX_UNCHECKED, x, y, 32, 32, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0));
	// Draw the parameter Name
	DrawAddChild(container, DrawStyledLabel(x+chkWidth+gapWidth+5, y+15, name, GetTextScaleToFitInWidth(name, nameWidth-10), ALIGN_LEFT, fontColor));
	// If enabled, draw arrows indicating where in the param list we are
	if(selected && param->enable && param->num_values > 1) {
		if(param->currentValueIdx != 0) {
			DrawAddChild(container, DrawStyledLabel(x+(chkWidth+nameWidth+(gapWidth*4)), y+15, "\213", .8f, ALIGN_LEFT, defaultColor));
		}
		if(param->currentValueIdx != param->num_values-1) {
			DrawAddChild(container, DrawStyledLabel(x+(chkWidth+nameWidth+paramWidth+(gapWidth*7)), y+15, "\233", .8f, ALIGN_LEFT, defaultColor));
		}
	}
	// Draw the current value
	DrawAddChild(container, DrawStyledLabel(x+chkWidth+nameWidth+(gapWidth*6), y+15, selValue, GetTextScaleToFitInWidth(selValue, paramWidth), ALIGN_LEFT, fontColor));
	return container;
}

// External
void DrawArgsSelector(const char *fileName) {
	Parameters* params = getParameters();
	int param_selection = 0;
	int params_per_page = 6;
	
	uiDrawObj_t *container = NULL;
	while (padsButtonsHeld() & BUTTON_A){ VIDEO_WaitVSync (); }
	while(1) {
		uiDrawObj_t *newPanel = DrawEmptyBox(20,60, getVideoMode()->fbWidth-20, 460);
		sprintf(txtbuffer, "%s Parameters:", fileName);
		DrawAddChild(newPanel, DrawStyledLabel(25, 74, txtbuffer, GetTextScaleToFitInWidth(txtbuffer, getVideoMode()->fbWidth-50), ALIGN_LEFT, defaultColor));

		int i = 0, j = 0;
		int current_view_start = MIN(MAX(0,param_selection-params_per_page/2),MAX(0,params->num_params-params_per_page));
		int current_view_end = MIN(params->num_params, MAX(param_selection+params_per_page/2,params_per_page));
	
		int scrollBarHeight = 90+(params_per_page*20);
		int scrollBarTabHeight = (int)((float)scrollBarHeight/(float)params->num_params);
		DrawAddChild(newPanel, DrawVertScrollBar(getVideoMode()->fbWidth-45, 120, 25, scrollBarHeight, (float)((float)param_selection/(float)(params->num_params-1)),scrollBarTabHeight));
		for(i = current_view_start,j = 0; i<current_view_end; ++i,++j) {
			DrawAddChild(newPanel, drawParameterForArgsSelector(&params->parameters[i], 25, 120+j*35, i==param_selection));
		}
		// Write about the default if there is any
		DrawAddChild(newPanel, DrawTransparentBox( 35, 350, getVideoMode()->fbWidth-35, 400));
		DrawAddChild(newPanel, DrawStyledLabel(33, 354, "Default values will be used by the DOL being loaded if a", 0.8f, ALIGN_LEFT, defaultColor));
		DrawAddChild(newPanel, DrawStyledLabel(33, 374, "parameter is not enabled. Please check the documentation", 0.8f, ALIGN_LEFT, defaultColor));
		DrawAddChild(newPanel, DrawStyledLabel(33, 394, "for this DOL if you are unsure of the default values.", 0.8f, ALIGN_LEFT, defaultColor));
		DrawAddChild(newPanel, DrawStyledLabel(640/2, 440, "(A) Toggle Param \267 (Start) Load the DOL", 1.0f, ALIGN_CENTER, defaultColor));
		
		container = DrawRepublish(container, newPanel);
		
		while (!(padsButtonsHeld() & (BUTTON_RIGHT|BUTTON_LEFT|BUTTON_UP|BUTTON_DOWN|BUTTON_START|BUTTON_A)))
			{ VIDEO_WaitVSync (); }
		u32 btns = padsButtonsHeld();
		if((btns & (BUTTON_RIGHT|BUTTON_LEFT)) && params->parameters[param_selection].enable) {
			int curValIdx = params->parameters[param_selection].currentValueIdx;
			int maxValIdx = params->parameters[param_selection].num_values;
			curValIdx = btns & BUTTON_LEFT ? 
				((--curValIdx < 0) ? maxValIdx-1 : curValIdx):((curValIdx + 1) % maxValIdx);
			params->parameters[param_selection].currentValueIdx = curValIdx;
		}
		if(btns & (BUTTON_UP|BUTTON_DOWN)) {
			param_selection = btns & BUTTON_UP ? 
				((--param_selection < 0) ? params->num_params-1 : param_selection)
				:((param_selection + 1) % params->num_params);
		}
		if(btns & BUTTON_A) {
			params->parameters[param_selection].enable ^= 1;
		}
		if(btns & BUTTON_START) {
			break;
		}
		while (padsButtonsHeld() & (BUTTON_RIGHT|BUTTON_LEFT|BUTTON_UP|BUTTON_DOWN|BUTTON_START|BUTTON_A))
			{ VIDEO_WaitVSync (); }
	}
	DrawDispose(container);
}

static uiDrawObj_t* drawCheatForCheatsSelector(CheatEntry *cheat, int x, int y, int selected) {

	char *name = &cheat->name[0];
	uiDrawObj_t* container = DrawContainer();
	
	int chkWidth = 32, nameWidth = 525, gapWidth = 13;
	// If not selected and not enabled, use greyed out font for everything
	GXColor fontColor = (cheat->enabled || selected) ? defaultColor : deSelectedColor;

	// If selected draw that it's selected
	DrawAddChild(container, DrawImage(cheat->enabled ? TEX_CHECKED:TEX_UNCHECKED, x, y, 32, 32, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0));
	if(selected) DrawAddChild(container, DrawTransparentBox( x+chkWidth+gapWidth, y, getVideoMode()->fbWidth-52, y+30));
	// Draw the cheat Name
	DrawAddChild(container, DrawStyledLabel(x+chkWidth+gapWidth+5, y+15, name, GetTextScaleToFitInWidth(name, nameWidth-10), ALIGN_LEFT, fontColor));
	return container;
}

// External
void DrawCheatsSelector(const char *fileName) {
	CheatEntries* cheats = getCheats();
	int cheat_selection = 0;
	int cheats_per_page = 6;

	uiDrawObj_t *container = NULL;
	while (padsButtonsHeld() & BUTTON_A){ VIDEO_WaitVSync (); }
	while(1) {
		uiDrawObj_t *newPanel = DrawEmptyBox(20,60, getVideoMode()->fbWidth-20, 460);
		sprintf(txtbuffer, "%s Cheats:", fileName);
		DrawAddChild(newPanel, DrawStyledLabel(25, 74, txtbuffer, GetTextScaleToFitInWidth(txtbuffer, getVideoMode()->fbWidth-50), ALIGN_LEFT, defaultColor));

		int i = 0, j = 0;
		int current_view_start = MIN(MAX(0,cheat_selection-cheats_per_page/2),MAX(0,cheats->num_cheats-cheats_per_page));
		int current_view_end = MIN(cheats->num_cheats, MAX(cheat_selection+cheats_per_page/2,cheats_per_page));
	
		int scrollBarHeight = 90+(cheats_per_page*20);
		int scrollBarTabHeight = (int)((float)scrollBarHeight/(float)cheats->num_cheats);
		DrawAddChild(newPanel, DrawVertScrollBar(getVideoMode()->fbWidth-45, 120, 25, scrollBarHeight, (float)((float)cheat_selection/(float)(cheats->num_cheats-1)),scrollBarTabHeight));
		for(i = current_view_start,j = 0; i<current_view_end; ++i,++j) {
			DrawAddChild(newPanel, drawCheatForCheatsSelector(&cheats->cheat[i], 25, 120+j*35, i==cheat_selection));
		}
		// Write about how many cheats are enabled
		DrawAddChild(newPanel, DrawTransparentBox( 35, 350, getVideoMode()->fbWidth-35, 410));
		
		float percent = (((float)getEnabledCheatsSize() / (float)kenobi_get_maxsize()) * 100.0f);
		sprintf(txtbuffer, "Space taken by cheats: %i/%i bytes (%.1f%% free)"
			, getEnabledCheatsSize(), kenobi_get_maxsize(), 100.0f-percent);
		DrawAddChild(newPanel, DrawStyledLabel(33, 354, txtbuffer, 0.8f, ALIGN_LEFT, defaultColor));
		
		sprintf(txtbuffer, "Enabled: %i Total: %i", getEnabledCheatsCount(), cheats->num_cheats);
		DrawAddChild(newPanel, DrawStyledLabel(33, 379, txtbuffer, 0.8f, ALIGN_LEFT, defaultColor));
		
		sprintf(txtbuffer, "WiiRD Debug %s", swissSettings.wiirdDebug ? "Enabled":"Disabled");
		DrawAddChild(newPanel, DrawStyledLabel(33, 404, txtbuffer, 0.8f, ALIGN_LEFT, defaultColor));
		DrawAddChild(newPanel, DrawStyledLabel(640/2, 440, "(A) Toggle Cheat \267 (X) WiiRD Debug \267 (B) Return", 0.9f, ALIGN_CENTER, defaultColor));

		container = DrawRepublish(container, newPanel);
		
		while (!(padsButtonsHeld() & (BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT|BUTTON_B|BUTTON_A|BUTTON_X|BUTTON_L|BUTTON_R)))
			{ VIDEO_WaitVSync (); }
		u32 btns = padsButtonsHeld();
		if(btns & (BUTTON_UP|BUTTON_DOWN)) {
			cheat_selection = btns & BUTTON_UP ? 
				((--cheat_selection < 0) ? cheats->num_cheats-1 : cheat_selection)
				:((cheat_selection + 1) % cheats->num_cheats);
		}
		if(btns & (BUTTON_LEFT|BUTTON_L)) {
			cheat_selection = (cheat_selection ? ((cheat_selection - cheats_per_page < 0) ? 0 : cheat_selection - cheats_per_page):(cheats->num_cheats-1));
		}
		if(btns & (BUTTON_RIGHT|BUTTON_R)) {
			cheat_selection = cheat_selection == cheats->num_cheats-1 ? 0 : ((cheat_selection + cheats_per_page > cheats->num_cheats-1) ? cheats->num_cheats-1 : (cheat_selection + cheats_per_page) % cheats->num_cheats);
		}
		if(btns & BUTTON_A) {
			cheats->cheat[cheat_selection].enabled ^= 1;
			if(getEnabledCheatsSize() > kenobi_get_maxsize())	// No room
				cheats->cheat[cheat_selection].enabled = 0;
		}
		if(btns & BUTTON_X) {
			swissSettings.wiirdDebug ^=1;
		}
		if(btns & BUTTON_B) {
			break;
		}
		while (padsButtonsHeld() & (BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT|BUTTON_B|BUTTON_A|BUTTON_X|BUTTON_L|BUTTON_R))
			{ VIDEO_WaitVSync (); }
	}
	DrawDispose(container);
}


void DrawGetTextEntry(int mode, const char *label, void *src, int size) {
	
	print_debug("DrawGetTextEntry Modes: Alpha [%s] Numeric [%s] IP [%s] Masked [%s] File [%s]\n", mode & ENTRYMODE_ALPHA ? "Y":"N", mode & ENTRYMODE_NUMERIC ? "Y":"N",
																	mode & ENTRYMODE_IP ? "Y":"N", mode & ENTRYMODE_MASKED ? "Y":"N", mode & ENTRYMODE_FILE ? "Y":"N");
	char *text = calloc(1, size + 1);
	char *masked = calloc(1, size + 1);
	if(mode & (ENTRYMODE_ALPHA|ENTRYMODE_IP)) {
		strncpy(text, src, size);
	}
	else {
		u16 *src_int = (u16*)src;
		itoa(*src_int, text, 10);
	}
	print_debug("Text is [%s] size %i\n", text, size);
	
	int caret = strlen(text);
	int cur_row = 0;
	int cur_col = 0;
	int num_rows = 0;
	int num_per_row[5] = {0,0,0,0,0};	// number of keys per row
	int pos_for_row[5] = {0,0,0,0,0};	// X pos to start drawing keys from
	int grid_gap = 45;
	int num_txt_modes = 0;	// Number of modes the text entry chars will have, e.g. upper case, lowercase etc.
	char *txt_modes_str[] = {"lowercase", "UPPERCASE"};
	// char arrays to grab from, exact order is important
	char *ip_mode_chars = "123456789.0\b";
	char *num_mode_chars = "1234567890\b";
	char *txt_mode_chars_lower = "1234567890-=\bqwertyuiop[]\\asdfghjkl;'zxcvbnm,./`!@\a#$%";
	char *txt_mode_chars_upper = "1234567890_+\bQWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?^&*\a()~";
	char *txt_mode_file_chars_lower = "1234567890-=\bqwertyuiop[]asdfghjkl;'zxcvbnm.`!\a#$%";
	char *txt_mode_file_chars_upper = "1234567890_+\bQWERTYUIOP{}ASDFGHJKL^&ZXCVBNM,@~\a()%";
	
	int cur_txt_mode = 0;
	char *gridText = NULL;
	
	// IP mode
	if(mode & ENTRYMODE_IP) {
		// 1  2  3
		// 4  5  6
		// 7  8  9
		//[.] 0  [backspace]
		num_rows = 4;
		grid_gap = 15;
		num_per_row[0] = 3;
		pos_for_row[0] = 240;
		num_per_row[1] = 3;
		pos_for_row[1] = 240;
		num_per_row[2] = 3;
		pos_for_row[2] = 240;
		num_per_row[3] = 3;
		pos_for_row[3] = 240;
		gridText = ip_mode_chars;
	}
	
	// Number only
	if((mode & ENTRYMODE_NUMERIC) && !(mode & ENTRYMODE_ALPHA)) {
		// 1  2  3
		// 4  5  6
		// 7  8  9
		// 0  [backspace]
		num_rows = 4;
		grid_gap = 15;
		num_per_row[0] = 3;
		pos_for_row[0] = 240;
		num_per_row[1] = 3;
		pos_for_row[1] = 240;
		num_per_row[2] = 3;
		pos_for_row[2] = 240;
		num_per_row[3] = 2;
		pos_for_row[3] = 240;
		gridText = num_mode_chars;
	}
	
	// Alpha only
	else if(!(mode & ENTRYMODE_NUMERIC) && (mode & ENTRYMODE_ALPHA)) {
		// TODO if we ever have to.
	}
	
	// Alphanumeric (not file)
	else if((mode & (ENTRYMODE_NUMERIC | ENTRYMODE_ALPHA)) && !(mode & ENTRYMODE_IP) && !(mode & ENTRYMODE_FILE)) {
		/* Mode 0:
		 1234567890-=<\b aka backspace>
		 qwertyuiop[]\
		 asdfghjkl;'
		 zxcvbnm,./
		 `!@<\a aka space>#$%
		
		 Mode 1:
		 1234567890_+<\b aka backspace>
		 QWERTYUIOP{}|
		 ASDFGHJKL:"
		 ZXCVBNM<>?
		 ^&*<\a aka space>()~
		*/
		
		num_txt_modes = 2;
		num_rows = 5;
		grid_gap = 10;
		num_per_row[0] = 13;
		pos_for_row[0] = 40;
		num_per_row[1] = 13;
		pos_for_row[1] = 60;
		num_per_row[2] = 11;
		pos_for_row[2] = 100;
		num_per_row[3] = 10;
		pos_for_row[3] = 120;
		num_per_row[4] = 7;
		pos_for_row[4] = 160;
	}
	
	// Alphanumeric (file)
	else if(mode & ENTRYMODE_FILE) {
		/* Mode 0:
		 1234567890-=<\b aka backspace>
		 qwertyuiop[]\
		 asdfghjkl;'
		 zxcvbnm
		 .`!<\a aka space>#$%
		
		 Mode 1:
		 1234567890_+<\b aka backspace>
		 QWERTYUIOP{}
		 ASDFGHJKL^&
		 ZXCVBNM
		 ,@~<\a aka space>()%
		*/
		
		num_txt_modes = 2;
		num_rows = 5;
		grid_gap = 10;
		num_per_row[0] = 13;
		pos_for_row[0] = 40;
		num_per_row[1] = 12;
		pos_for_row[1] = 60;
		num_per_row[2] = 11;
		pos_for_row[2] = 100;
		num_per_row[3] = 7;
		pos_for_row[3] = 120;
		num_per_row[4] = 7;
		pos_for_row[4] = 160;
	}
	
	// Wait for any A or Left/Right presses to finish
	while ((padsButtonsHeld() & (BUTTON_A|BUTTON_LEFT|BUTTON_RIGHT))){ VIDEO_WaitVSync (); }
	uiDrawObj_t *container = NULL;
	while(1) {
		// Double box for extra darkness
		uiDrawObj_t *newPanel = DrawEmptyBox(20,60, getVideoMode()->fbWidth-20, 440);
		DrawAddChild(newPanel, DrawEmptyBox(20,60, getVideoMode()->fbWidth-20, 440));
		sprintf(txtbuffer, "%s - Please enter a value", label);
		DrawAddChild(newPanel, DrawStyledLabel(25, 74, txtbuffer, GetTextScaleToFitInWidth(txtbuffer, getVideoMode()->fbWidth-50), ALIGN_LEFT, defaultColor));

		// Draw the text entry box
		char *displayText = text;
		if(mode & ENTRYMODE_MASKED) {
			displayText = masked;
			memset(masked, '*', strlen(text));
			masked[strlen(text)] = '\0';
		}
		DrawAddChild(newPanel, DrawEmptyBox(40, 100, getVideoMode()->fbWidth-40, 140));
		DrawAddChild(newPanel, DrawStyledLabelWithCaret(320, 120, displayText, GetTextScaleToFitInWidth(displayText, getVideoMode()->fbWidth-90), ALIGN_CENTER, defaultColor, caret));
		DrawAddChild(newPanel, DrawStyledLabel(320, 160, "(L/R) Cursor \267 (Start) Accept \267 (B) Discard", 0.75f, ALIGN_CENTER, defaultColor));

		// Alphanumeric has a little "mode" hint at the bottom (upper/lower case set switching)
		if(mode & ENTRYMODE_ALPHA) {
			gridText = cur_txt_mode == 0 ? 
				(mode & ENTRYMODE_FILE ? txt_mode_file_chars_lower : txt_mode_chars_lower)
				: 
				(mode & ENTRYMODE_FILE ? txt_mode_file_chars_upper : txt_mode_chars_upper);
			sprintf(txtbuffer, "Press X to change to [%s], current mode is [%s]",
													txt_modes_str[(cur_txt_mode + 1 >= num_txt_modes ? 0 : cur_txt_mode + 1)], txt_modes_str[cur_txt_mode]);
			DrawAddChild(newPanel, DrawStyledLabel(25, 427, txtbuffer, 0.65f, ALIGN_LEFT, defaultColor));
		}
		
		// Draw the grid
		int y = 200, dx = 0, dy = 0, i = 0;
		int button_height = 30;
		for(dy = 0; dy < num_rows; dy++) {
			int x = pos_for_row[dy];
			for(dx = 0; dx < num_per_row[dy]; dx++) {
				// Space and Backspace are special, draw them in double the width with smaller fonts
				bool isSpecial = (gridText[i] == '\a' || gridText[i] == '\b');
				int button_width = isSpecial ? (60 + grid_gap) : 30;
				DrawAddChild(newPanel, DrawEmptyColouredBox(x, y, x+button_width, y+button_height, cur_col == dx && cur_row == dy ? (GXColor) {96,107,164,GUI_MSGBOX_ALPHA} : (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}));
				float fontSize = isSpecial ? 0.65f : 1.0f;
				if(isSpecial)
					sprintf(txtbuffer, "%s", gridText[i] == '\a' ? "Space" : "(Y) Back");
				else
					sprintf(txtbuffer, "%c", gridText[i]);
				DrawAddChild(newPanel, DrawStyledLabel(x+(button_width/2), y+(button_height/2), txtbuffer, fontSize, ALIGN_CENTER, cur_col == dx && cur_row == dy ? defaultColor : deSelectedColor));
				x += (grid_gap + button_width);
				i++;
			}
			y += (grid_gap + button_height);
		}
		
		container = DrawRepublish(container, newPanel);
		
		while (!(padsButtonsHeld() & (BUTTON_L|BUTTON_R|BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT|BUTTON_B|BUTTON_A|BUTTON_X|BUTTON_Y|BUTTON_START)))
			{ VIDEO_WaitVSync (); }
		u32 btns = padsButtonsHeld();
		// Key nav
		if(btns & BUTTON_DOWN) {
			cur_row = (cur_row + 1 >= num_rows ? 0 : cur_row + 1);
		}
		if(btns & BUTTON_UP) {
			cur_row = (cur_row == 0 ? num_rows - 1 : cur_row - 1);
		}
		if(btns & BUTTON_LEFT) {
			cur_col = (cur_col == 0 ? num_per_row[cur_row] - 1 : cur_col - 1);
		}
		if(btns & BUTTON_RIGHT) {
			cur_col = (cur_col + 1 >= num_per_row[cur_row] ? 0 : cur_col + 1);
		}
		// If we went off the end due to a row that has less than another
		if(cur_col >= num_per_row[cur_row]) cur_col = num_per_row[cur_row] - 1;
		// Key press handling
		if((btns & BUTTON_A) || (btns & BUTTON_Y)) {
			int char_pos = cur_col;
			for(i = 0; i < cur_row; i++)
				char_pos += num_per_row[i];
			char pressed = gridText[char_pos];
			// Handle normal character presses
			if(pressed != '\b' && !(btns & BUTTON_Y)) {
				if(caret < size && strlen(text) < size) {
					//print_debug("Pressed [%c]\n", pressed);
					if(pressed == '\a')
						pressed = ' ';
					// Shuffle everything forward (don't want overwrite functionality)
					for(i = size-1; i > caret; i--) {
						text[i] = text[i-1];
					}
					text[caret] = pressed;
					caret++;
				}
			}
			// Handle deletes via Y button or "backspace" button
			else if((btns & BUTTON_Y) || (pressed == '\b')) {
				// Delete a character from the caret
				if(caret-1 >= 0) {
					for(i = caret-1; i < size; i++) {
						text[i] = text[i+1];
						text[i+1] = '\0';
					}
					text[size] = '\0';
					if(caret > 0)
						caret --;
				}
			}
		}
		// Mode switching if the set allows it (only alpha does for upper/lower)
		if(btns & BUTTON_X) {
			if(num_txt_modes > 0) {
				cur_txt_mode = (cur_txt_mode + 1 >= num_txt_modes ? 0 : cur_txt_mode + 1);
			}
		}
		if(btns & BUTTON_L) {
			if(caret > 0) caret--;
		}
		if(btns & BUTTON_R) {
			if(caret < strlen(text)) caret++;
		}
		if(btns & BUTTON_B) {
			break;
		}
		if(btns & BUTTON_START) {
			if(mode & (ENTRYMODE_ALPHA|ENTRYMODE_IP)) {
				strcpy(src, text);
			}
			else {
				u16 *src_int = (u16*)src;
				unsigned long val = strtoul(text, NULL, 10);
				*src_int = val > 0xFFFF ? 0xFFFF : val;
			}
			break;
		}
		while (padsButtonsHeld() & (BUTTON_L|BUTTON_R|BUTTON_UP|BUTTON_DOWN|BUTTON_LEFT|BUTTON_RIGHT|BUTTON_B|BUTTON_A|BUTTON_X|BUTTON_Y|BUTTON_START))
			{ VIDEO_WaitVSync (); }
	}
	if(text) free(text);
	if(masked) free(masked);
	DrawDispose(container);
}


static float easeOutBack(float t) {
	const float c1 = 1.70158f, c3 = c1 + 1.0f;
	t -= 1.0f;
	return 1.0f + c3*t*t*t + c1*t*t;
}

// Multiply a transform onto the current UI transform
static void composeTransform(Mtx local, bool perspective) {
	Mtx cur, out;
	bool curPerspective;
	UI_GetTransform(cur, &curPerspective);
	guMtxConcat(cur, local, out);
	UI_SetTransform(out, curPerspective || perspective);
}

static void videoDrawEvent(uiDrawObj_t *videoEvent) {
	//print_debug("Draw event: %08X (type %s)\n", (u32)videoEvent, typeStrings[videoEvent->type]);
	Mtx base, chain, local;
	bool basePerspective, chainPerspective;
	UI_GetTransform(base, &basePerspective);

	// Animations that also apply to the rest of the chain (labels of a message box etc.)
	switch(videoEvent->anim) {
		case UI_ANIM_POP: {
			float t = videoEvent->born ? (float)ticks_to_millisecs(gettime() - videoEvent->born) / 220.0f : 1.0f;
			if(t < 1.0f) {
				float e = easeOutBack(t);
				UI_MakeTransform(local, videoEvent->cx, videoEvent->cy, 0.0f, 0.0f, -220.0f*(1.0f-e), 0.0f, 0.3f*(1.0f-e), 0.9f+0.1f*e);
				composeTransform(local, true);
			}
			break;
		}
		case UI_ANIM_FILEPANEL:
			if(homeBlend > 0.5f) {
				return;	// Hidden behind the home menu
			}
			if(homeBlend > 0.001f) {
				UI_MakeTransform(local, 320.0f, 260.0f, 0.0f, 0.0f, -700.0f*homeBlend, 0.0f, 0.0f, 1.0f);
				composeTransform(local, true);
			}
			break;
	}
	UI_GetTransform(chain, &chainPerspective);

	// Transforms that only apply to this object
	if(videoEvent->hasXform) {
		composeTransform(videoEvent->xform, true);
	}
	else if(videoEvent->anim == UI_ANIM_SWAY) {
		float t = (float)ticks_to_millisecs(gettime() - videoEvent->born) / 300.0f;
		float flip = t < 1.0f ? (1.0f - easeOutBack(t)) * 1.6f : 0.0f;
		UI_MakeTransform(local, videoEvent->cx, videoEvent->cy, 0.0f, 0.0f, 0.0f, flip + sinf(Scene3D_Time()*1.3f)*0.35f, 0.0f, 1.0f);
		composeTransform(local, true);
	}

	drawInit();
	switch(videoEvent->type) {
		case EV_TEXOBJ:
			_DrawTexObj(videoEvent);
			break;
		case EV_IMAGE:
			_DrawImage(videoEvent);
			break;
		case EV_MSGBOX:
			_DrawMessageBox(videoEvent);
			break;
		case EV_PROGRESS:
			_DrawProgressBar(videoEvent);
			break;
		case EV_SELECTABLEBUTTON:
			_DrawSelectableButton(videoEvent);
			break;
		case EV_EMPTYBOX:
			_DrawEmptyBox(videoEvent);
			break;
		case EV_TRANSPARENTBOX:
			_DrawTransparentBox(videoEvent);
			break;
		case EV_FILEBROWSERBUTTON:
			_DrawFileBrowserButton(videoEvent);
			break;
		case EV_VERTSCROLLBAR:
			_DrawVertScrollBar(videoEvent);
			break;
		case EV_STYLEDLABEL:
			_DrawStyledLabel(videoEvent);
			break;
		case EV_MENUBUTTONS:
			_DrawMenuButtons(videoEvent);
			break;
		case EV_TOOLTIP:
			_DrawTooltip(videoEvent);
			break;
		case EV_TITLEBAR:
			_DrawTitleBar(videoEvent);
			break;
		case EV_SCENE3D:
			_DrawScene3D(videoEvent);
			break;
		case EV_PAGEHEADER:
			_DrawPageHeader(videoEvent);
			break;
		case EV_SELECTBAR:
			_DrawSelectionBar(videoEvent);
			break;
		case EV_COVERFLOW:
			_DrawCoverflow(videoEvent);
			break;
		default:
			break;
	}
	UI_SetTransform(chain, chainPerspective);
	if(videoEvent->child != NULL) {
		videoDrawEvent(videoEvent->child);
	}
	UI_SetTransform(base, basePerspective);
}

static void markDisposed(uiDrawObj_t *evt)
{
	if(evt && evt->child && !evt->child->disposed) {
		markDisposed(evt->child);
	}
	if(evt) {
		evt->disposed = true;
	}
}

static void *videoUpdate(void *videoEventQueue) {
	GX_SetCurrentGXThread();
	
	//int frames = 0;
	//int framerate = 0;
	//u32 lasttime = gettick();
	while(video_thread == LWP_GetSelf()) {
		whichfb ^= 1;
		//frames++;
		LWP_MutexLock(_videomutex);
		// Mark events recursively as disposed
		uiDrawObjQueue_t *videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
		while(videoEventQueueEntry != NULL) {
			uiDrawObj_t *videoEvent = videoEventQueueEntry->event;
			if(videoEvent->disposed) {
				markDisposed(videoEvent);
			}
			videoEventQueueEntry = videoEventQueueEntry->next;
		}
		// Free events
		videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
		while(videoEventQueueEntry != NULL) {
			uiDrawObj_t *videoEvent = videoEventQueueEntry->event;
			// Remove any video events marked for disposal
			if(videoEvent->disposed) {
				disposeEvent(videoEvent);
				videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
				continue;
			}
			videoEventQueueEntry = videoEventQueueEntry->next;
		}
		
		Scene3D_NewFrame();
		UI_SetOverscan(1.0f - swissSettings.uiOverscan / 100.0f);
		UISound_Poll();
		GXRModeObj *vmode = getVideoMode();
		if(vmode->field_rendering) {
			GX_SetViewportJitter(0.0f, 0.0f, vmode->fbWidth, vmode->efbHeight, 0.0f, 1.0f, VIDEO_GetNextField());
		}
		// Draw out every event
		videoEventQueueEntry = (uiDrawObjQueue_t*)videoEventQueue;
		while(videoEventQueueEntry != NULL) {
			uiDrawObj_t *videoEvent = videoEventQueueEntry->event;
			UI_ResetTransform();
			videoDrawEvent(videoEvent);
			videoEventQueueEntry = videoEventQueueEntry->next;
		}
		UI_ResetTransform();
		drawActivateFade();
		
		//Copy EFB->XFB
		if(vmode->copy_interlaced == GX_COPY_INTERLACED) {
			GX_SetDispCopyFrame2Field(GX_COPY_INTERLACED ^ VIDEO_GetNextField());
		}
		u16 width = vmode->fbWidth;
		u16 height = GX_SetDispCopyYScale(getYScaleFactor(vmode->efbHeight, vmode->xfbHeight));
		GX_CopyDisp(xfb[whichfb], GX_TRUE);
		GX_DrawDone();

		LWP_MutexUnlock(_videomutex);
		VIDEO_SetNextFramebuffer(xfb[whichfb]);
		VIDEO_ConfigurePan(0, 0, width, height);
		VIDEO_Flush();
		VIDEO_WaitForFlush();
	}
	return NULL;
}

void DrawAddChild(uiDrawObj_t *parent, uiDrawObj_t *child)
{
	LWP_MutexLock(_videomutex);
	//print_debug("Added a new child event %08X (type %s)\n", (u32)child, typeStrings[child->type]);
	uiDrawObj_t *current = parent;
    while (current->child != NULL) {
        current = current->child;
    }
	current->child = child;
	child->disposed = false;
	//print_debug("Add child %08X (type %s) to parent %08X (type %s)\n",
	//	(u32)child, typeStrings[child->type], (u32)parent, typeStrings[parent->type]);
	LWP_MutexUnlock(_videomutex);
}

uiDrawObj_t* DrawPublish(uiDrawObj_t *evt)
{
	LWP_MutexLock(_videomutex);
	uiDrawObj_t* event = addVideoEvent(evt);
	LWP_MutexUnlock(_videomutex);
	return event;
}

uiDrawObj_t* DrawRepublish(uiDrawObj_t *old, uiDrawObj_t *new)
{
	LWP_MutexLock(_videomutex);
	if (old) {
		// Replacing a dialog with an updated one shouldn't replay its entrance
		if (old->anim == UI_ANIM_POP && new->anim == UI_ANIM_POP) {
			new->born = old->born;
		}
		old->disposed = true;
	}
	uiDrawObj_t* event = addVideoEvent(new);
	LWP_MutexUnlock(_videomutex);
	return event;
}

void DrawDispose(uiDrawObj_t *evt)
{
	LWP_MutexLock(_videomutex);
	evt->disposed = true;
	LWP_MutexUnlock(_videomutex);
}

void DrawInit(GXRModeObj *videoMode, bool black) {
	setVideoMode(videoMode);
	padsInit();
	init_font();
	init_textures();
	Scene3D_Init();
	UISound_Init();
	uiDrawObj_t *container = DrawContainer();
	if(!black) {
		DrawAddChild(container, DrawScene3D());
		DrawAddChild(container, DrawTitleBar());
		buttonPanel = DrawMenuButtons(MENU_NOSELECT);
		DrawAddChild(container, buttonPanel);
	}
	DrawPublish(container);
	LWP_MutexInit(&_videomutex, false);
	LWP_CreateThread(&video_thread, videoUpdate, videoEventQueue, video_thread_stack, VIDEO_STACK_SIZE, VIDEO_PRIORITY);
}

void DrawLoadBackdrop(DEVICEHANDLER_INTERFACE *device) {
	file_handle *backdropFile = calloc(1, sizeof(file_handle));
	concat_path(backdropFile->name, device->initial->name, "swiss/backdrop.tpl");
	backdropFile->device = device;
	
	s32 id = 0;
	u32 fmt;
	u16 width, height;
	if(TPL_OpenTPLFromHandle(&backdropTPL, openFileStream(backdropFile)) >= 0) {
		time_t curtime;
		if(time(&curtime) != (time_t)-1) {
			struct tm *tm = localtime(&curtime);
			switch(backdropTPL.ntextures) {
				case 2:
					id = (tm->tm_mon + 2) % 12 / 6;
					break;
				case 3:
					id = (tm->tm_mon + 2) % 12 / 4;
					break;
				case 4:
					id = (tm->tm_mon + 1) % 12 / 3;
					break;
				case 6:
					id = (tm->tm_mon + 1) % 12 / 2;
					break;
				case 7:
					id = tm->tm_wday;
					break;
				case 12:
					id = tm->tm_mon;
					break;
				case 24:
					id = tm->tm_hour;
					break;
				case 30 ... 31:
					id = tm->tm_mday - 1;
					break;
				case 365 ... 366:
					id = tm->tm_yday;
					break;
				default:
					srand(curtime);
					id = rand();
					break;
			}
			id %= backdropTPL.ntextures;
		}
		if(TPL_GetTextureInfo(&backdropTPL, id, &fmt, &width, &height) >= 0) {
			switch(fmt) {
				case GX_TF_CI4:
				case GX_TF_CI8:
				case GX_TF_CI14:
					TPL_GetTextureCI(&backdropTPL, id, &backdropTexObj, &backdropTlutObj, fmt == GX_TF_CI14 ? GX_BIGTLUT0 : GX_TLUT0);
					GX_InitTexObjUserData(&backdropTexObj, &backdropTlutObj);
					break;
				default:
					TPL_GetTexture(&backdropTPL, id, &backdropTexObj);
					break;
			}
			GX_InitTexObjUserData(&backdropIndTexObj, NULL);
			customBackdrop = true;
		}
		else {
			TPL_CloseTPLFile(&backdropTPL);
			free(backdropFile);
		}
	}
	else {
		TPL_CloseTPLFile(&backdropTPL);
		free(backdropFile);
	}
}

void DrawShutdown() {
	UISound_Shutdown();
	mutex_t mutex = _videomutex;
	_videomutex = LWP_MUTEX_NULL;
	LWP_MutexDestroy(mutex);
	lwp_t thread = video_thread;
	video_thread = LWP_THREAD_NULL;
	LWP_JoinThread(thread, NULL);
	GX_SetCurrentGXThread();
	unsetVideoMode();
}

void DrawVideoMode(GXRModeObj *videoMode)
{
	LWP_MutexLock(_videomutex);
	if(getVideoMode() != videoMode) {
		setVideoMode(videoMode);
	}
	else {
		updateVideoMode(videoMode);
	}
	LWP_MutexUnlock(_videomutex);
}
