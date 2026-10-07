/* -----------------------------------------------------------
      uisound.h - Menu sound effects

      Sounds are synthesised at startup and mixed in software
      straight into the audio interface DMA, so the DSP and ARAM
      are never touched.
   ----------------------------------------------------------- */

#ifndef UISOUND_H
#define UISOUND_H

enum UISound {
	SND_MOVE = 0,	// cursor moved
	SND_SELECT,		// confirm (A)
	SND_BACK,		// cancel (B)
	SND_OPEN,		// home menu opened
	SND_ERROR,		// warning / failure message
	SND_INFO,		// success message
	SND_COUNT
};

void UISound_Init(void);
void UISound_Shutdown(void);
void UISound_Disable(void);
void UISound_Play(int sound);
void UISound_Poll(void);

#endif
