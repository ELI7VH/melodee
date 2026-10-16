/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef WL_MIX_H
#define WL_MIX_H
#include <stdint.h>
/* WaveLoop fork: the monitor crossfade (CC8 on any channel; Oxygen fader 9). 0 the computer alone, 64 both at full
 * (the start), 127 the FM-1 alone. Set by MIDI (midi_control.c), applied with USB connected (usb_audio_stream.c). */
#define WL_MIX_CC 8u
static volatile uint8_t wl_mix = 64u;
static int32_t wl_mix_q15(uint32_t x, int computer)   /* Q15 gain at fader x: the FM-1 full from 64 up, the computer to 64 */
{
    if (computer)
        return x <= 64u ? 32768 : (int32_t)((127u - x) * 32768u / 63u);
    return x >= 64u ? 32768 : (int32_t)(x * 32768u / 64u);
}
static int32_t wl_mix_pct(int32_t x, int computer) { return (wl_mix_q15((uint32_t)x, computer) * 100 + 16384) >> 15; }
#endif
