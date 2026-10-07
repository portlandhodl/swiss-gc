/* -----------------------------------------------------------
      uisound.c - Menu sound effects

      All sounds are generated at startup (soft bell tones in the
      spirit of the GameCube menu) and mixed by the audio DMA
      interrupt into a small double buffer at 32 kHz.
   ----------------------------------------------------------- */

#include <gccore.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include "uisound.h"
#include "swiss.h"
#include "input.h"

#define RATE        32000
#define DMA_FRAMES  512
#define DMA_BYTES   (DMA_FRAMES * 2 * sizeof(s16))
#define MAX_VOICES  6
#define VOLUME      200		// out of 256

static s16 *sounds[SND_COUNT];
static u32 soundLen[SND_COUNT];

static s16 dmaBuffer[2][DMA_FRAMES * 2] ATTRIBUTE_ALIGN(32);
static int dmaNext;

static struct {
	const s16 *data;
	u32 len;
	u32 pos;
} voices[MAX_VOICES];

static bool running;
static bool disabled;

/* ---------------------------------------------------------------------------
 * Synthesis
 * ------------------------------------------------------------------------- */

static float *synthBegin(float seconds, u32 *samples)
{
	*samples = (u32)(seconds * RATE);
	return calloc(*samples, sizeof(float));
}

// Bell-ish tone: a sine with quickly fading overtones
static void addBell(float *buf, u32 samples, float start, float freq, float decay, float amp)
{
	u32 first = (u32)(start * RATE);
	for (u32 i = first; i < samples; i++) {
		float t = (float)(i - first) / RATE;
		float attack = t < 0.003f ? t / 0.003f : 1.0f;
		float env = attack * expf(-decay * t);
		float w = 2.0f * M_PI * freq * t;
		float s = sinf(w)
		        + 0.30f * sinf(2.0f * w) * expf(-decay * t)
		        + 0.12f * sinf(3.01f * w) * expf(-2.0f * decay * t);
		buf[i] += amp * env * s;
	}
}

// Low buzzy tone made from odd harmonics
static void addBuzz(float *buf, u32 samples, float start, float freq, float length, float amp)
{
	u32 first = (u32)(start * RATE);
	u32 last = first + (u32)(length * RATE);
	if (last > samples) last = samples;
	for (u32 i = first; i < last; i++) {
		float t = (float)(i - first) / RATE;
		float env = (t < 0.004f ? t / 0.004f : 1.0f) * expf(-14.0f * t);
		float w = 2.0f * M_PI * freq * t;
		buf[i] += amp * env * (sinf(w) + sinf(3.0f * w) / 3.0f + sinf(5.0f * w) / 5.0f);
	}
}

// Filtered noise swell, the low pass opens up over the course of the sound
static void addWhoosh(float *buf, u32 samples, float length, float amp)
{
	u32 rng = 0x1234567;
	float lp = 0.0f;
	u32 last = (u32)(length * RATE);
	if (last > samples) last = samples;
	for (u32 i = 0; i < last; i++) {
		float t = (float)i / last;
		rng = rng * 1664525 + 1013904223;
		float noise = (float)(s32)rng / 2147483648.0f;
		float cutoff = 0.02f + 0.18f * t;
		lp += cutoff * (noise - lp);
		buf[i] += amp * sinf(M_PI * t) * lp * 3.0f;
	}
}

static void synthEnd(int sound, float *buf, u32 samples)
{
	// Round up to a whole number of 32 byte blocks
	u32 padded = (samples + 15) & ~15;
	s16 *out = memalign(32, padded * sizeof(s16));
	for (u32 i = 0; i < padded; i++) {
		float v = i < samples ? buf[i] : 0.0f;
		// Short fade out so nothing clicks at the end
		if (i + 64 > samples && i < samples) v *= (float)(samples - i) / 64.0f;
		if (v > 1.0f) v = 1.0f;
		if (v < -1.0f) v = -1.0f;
		out[i] = (s16)(v * 32767.0f);
	}
	free(buf);
	sounds[sound] = out;
	soundLen[sound] = padded;
}

static void synthesise(void)
{
	u32 n;
	float *b;

	b = synthBegin(0.08f, &n);
	addBell(b, n, 0.0f, 1760.0f, 60.0f, 0.22f);
	addBell(b, n, 0.0f, 2637.0f, 80.0f, 0.06f);
	synthEnd(SND_MOVE, b, n);

	b = synthBegin(0.40f, &n);
	addBell(b, n, 0.00f, 1046.5f, 14.0f, 0.24f);
	addBell(b, n, 0.06f, 1568.0f, 12.0f, 0.24f);
	synthEnd(SND_SELECT, b, n);

	b = synthBegin(0.34f, &n);
	addBell(b, n, 0.00f, 1318.5f, 16.0f, 0.22f);
	addBell(b, n, 0.06f, 880.0f, 14.0f, 0.22f);
	synthEnd(SND_BACK, b, n);

	b = synthBegin(0.55f, &n);
	addWhoosh(b, n, 0.32f, 0.16f);
	addBell(b, n, 0.05f, 523.25f, 7.0f, 0.12f);
	addBell(b, n, 0.11f, 783.99f, 7.0f, 0.10f);
	synthEnd(SND_OPEN, b, n);

	b = synthBegin(0.30f, &n);
	addBuzz(b, n, 0.00f, 196.0f, 0.10f, 0.20f);
	addBuzz(b, n, 0.13f, 185.0f, 0.12f, 0.20f);
	synthEnd(SND_ERROR, b, n);

	b = synthBegin(0.40f, &n);
	addBell(b, n, 0.00f, 1568.0f, 12.0f, 0.20f);
	addBell(b, n, 0.07f, 2093.0f, 11.0f, 0.20f);
	synthEnd(SND_INFO, b, n);
}

/* ---------------------------------------------------------------------------
 * Mixer
 * ------------------------------------------------------------------------- */

static void mix(s16 *out)
{
	for (int f = 0; f < DMA_FRAMES; f++) {
		s32 acc = 0;
		for (int v = 0; v < MAX_VOICES; v++) {
			if (voices[v].data) {
				acc += voices[v].data[voices[v].pos++];
				if (voices[v].pos >= voices[v].len) voices[v].data = NULL;
			}
		}
		acc = (acc * VOLUME) >> 8;
		if (acc > 32767) acc = 32767;
		if (acc < -32768) acc = -32768;
		out[f * 2] = out[f * 2 + 1] = (s16)acc;
	}
	DCFlushRange(out, DMA_BYTES);
}

// Called when the previously queued buffer starts playing
static void dmaCallback(void)
{
	mix(dmaBuffer[dmaNext]);
	AUDIO_InitDMA((u32)dmaBuffer[dmaNext], DMA_BYTES);
	dmaNext ^= 1;
}

void UISound_Init(void)
{
	if (running || disabled) return;
	if (!sounds[0]) synthesise();

	memset(voices, 0, sizeof(voices));
	AUDIO_Init(NULL);
	AUDIO_SetDSPSampleRate(AI_SAMPLERATE_32KHZ);
	AUDIO_RegisterDMACallback(dmaCallback);
	mix(dmaBuffer[0]);
	AUDIO_InitDMA((u32)dmaBuffer[0], DMA_BYTES);
	dmaNext = 1;
	AUDIO_StartDMA();
	running = true;
}

void UISound_Shutdown(void)
{
	if (!running) return;
	AUDIO_StopDMA();
	AUDIO_RegisterDMACallback(NULL);
	running = false;
}

// Stop for the rest of the session, another component takes over the audio interface
void UISound_Disable(void)
{
	UISound_Shutdown();
	disabled = true;
}

void UISound_Play(int sound)
{
	if (!running || !swissSettings.uiSounds || sound < 0 || sound >= SND_COUNT) return;

	u32 level = IRQ_Disable();
	int slot = 0;
	u32 mostPlayed = 0;
	for (int v = 0; v < MAX_VOICES; v++) {
		if (!voices[v].data) { slot = v; break; }
		// Steal the voice that is closest to finishing
		if (voices[v].pos > mostPlayed) { mostPlayed = voices[v].pos; slot = v; }
	}
	voices[slot].data = sounds[sound];
	voices[slot].len = soundLen[sound];
	voices[slot].pos = 0;
	IRQ_Restore(level);
}

// Per frame: turn button presses into feedback sounds
void UISound_Poll(void)
{
	static u32 prevButtons;
	static int prevStick;

	u32 held = padsButtonsHeld();
	u32 pressed = held & ~prevButtons;
	prevButtons = held;

	int stick = 0;
	if (padsStickX() > 40) stick |= 1;
	if (padsStickX() < -40) stick |= 2;
	if (padsStickY() > 40) stick |= 4;
	if (padsStickY() < -40) stick |= 8;
	int stickPressed = stick & ~prevStick;
	prevStick = stick;

	if (pressed & BUTTON_A) UISound_Play(SND_SELECT);
	else if (pressed & BUTTON_B) UISound_Play(SND_BACK);
	else if ((pressed & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT | BUTTON_L | BUTTON_R)) || stickPressed)
		UISound_Play(SND_MOVE);
}
