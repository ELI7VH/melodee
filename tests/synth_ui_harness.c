/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[3][FM1_NCOL], fm1_led_dim_mask[3];   /* (hal/fm1_input.h: the dim planes) */
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
static int32_t fm1_enc_take(uint32_t e)
{
    e %= 7u;
    int32_t s = host_enc[e]; host_enc[e] = 0;
#ifdef UI_ENC_TAKE_HOOK
    UI_ENC_TAKE_HOOK(e);                           /* a scan interrupt can publish another detent immediately */
#endif
    return s;
}
static void fm1_wdt_feed(void) {}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint16_t host_screen[240 * 240];
static int host_blit_trace;                       /* (tests/ui_render.c UI_AUDIT_BLITS: each transfer listed) */
static uint64_t host_sent;                        /* pixels sent to the LCD, blitted or filled (tests/ui_render.c) */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    if (host_blit_trace) fprintf(stderr, " [fill %u,%u %ux%u]", x, y, w, h);
    if (x < 240u && y < 240u)
        host_sent += (uint64_t)(w < 240u - x ? w : 240u - x) * (h < 240u - y ? h : 240u - y);
#ifdef UI_FILL_HOOK
    UI_FILL_HOOK(x, y, w, h);                     /* (tests/ui_render.c: what a fill covers is gone) */
#endif
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
}
static void lcd_sync(void) {}
#define SCOPE_N 512u                              /* audio.c: the HOME oscilloscope */
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_sent += (uint64_t)w * h;
    if (host_blit_trace) fprintf(stderr, " [blit %u,%u %ux%u]", x, y, w, h);
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_screen[(y + j) * 240u + x + i] = p[j * w + i];
}
static struct { uint32_t stage; } melodee_dbg;
#ifndef MELODEE_FLASH
#define MELODEE_FLASH 0
#endif
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "TEST"
#endif
#define UI_TEST_PALETTE 1                         /* + GRAY: every screen drawn in it must stay gray */
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#if MELODEE_FLASH
#include "synth_flash_harness.c"
#endif
#include "../firmware/src/upreset.c"
#if !MELODEE_FLASH
#define ST_PAYLOAD_MAX 3840u
#endif
#include "../firmware/src/persist.c"
#include "../firmware/src/cz_store.c"
#include "../firmware/src/fm6_store.c"

static void ui_power_on(void)
{
    memset(trk, 0, sizeof trk); memset(&song, 0, sizeof song); memset(&ui, 0, sizeof ui);
    memset(&nm, 0, sizeof nm); memset(&pop, 0, sizeof pop);
    memset(&sx_import, 0, sizeof sx_import); memset(&bulk, 0, sizeof bulk);
    memset(&brw, 0, sizeof brw); memset(&lys, 0, sizeof lys);
    memset(&midi_clock, 0, sizeof midi_clock); midi_forget_track(0);
    undo_clear(); up_cache_reset(); native_cache_reset();
    cz_init();
    for (uint32_t i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    undo_depth++;
    track_defaults(&trk[0]); set_engine_of(&trk[0], TRK_DEF[0][0]);
    apply_preset_to(&trk[0], TRK_DEF[0][1]); trk[0].engine = trk[0].eng_req;
    undo_depth--;
    song.master_q12 = 4096; ui.home = ui.force = 1;
    panel = PANEL_DEFAULT; settings_init(); palette_set(settings.palette);
    kb_prev = kb_layer = kb_lock = 0; kb_mask = layer_bits();
    memset(kb_chn, 0, sizeof kb_chn); memset(mchord, 0, sizeof mchord);
    memset(chord_last, 0, sizeof chord_last);
    fm1_in.buttons = fm1_in.notes = host_pressed = host_notes = 0;
    memset(host_enc, 0, sizeof host_enc);
    oct_eat = oct_deferred = 0; smap.on = sec.ok = 0;
    panic_req = 0; mi_r = mi_w = midi_in_overflow = 0;
}
static void frame(void)
{
    host_ticks += 16000u; fm1_ms += 16u; ui_input(); ui_draw();
}
static void press(uint32_t label)
{
    fm1_ms += 320u;
    fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label];
    frame(); fm1_in.buttons &= ~(1u << panel.btn[label]); frame();
}
static void turn(uint32_t role, int32_t s)
{
    host_enc[panel.enc[role]] += s * panel.dir[role]; frame();
    host_ticks += 200000u; fm1_ms += 200u;
}
