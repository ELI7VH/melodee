/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop fork: HOME's controls page (ui.home_pg 2; HOME tapped on Stage). What every mapped control of the MiniLab /
 * Oxygen presets (controllers/) does on the selected track, condensed in two columns of rows: the eight knobs, each
 * its target's name on this engine, its value and a bar (a dash: this engine has nothing there); then the four faders
 * (each track's level, in its colour), the encoder (the sound), the engine buttons, the press and the pads. One table (KM_ROWS): a control mapped later is a row more. A cell redraws when what it shows changes. */
enum { KM_CC, KM_LEVEL, KM_SOUND, KM_ENGINE, KM_PRESS, KM_PADS };
typedef struct {
    uint8_t kind, arg;                                   /* KM_CC: the CC; KM_LEVEL: the track */
    const char *src;                                     /* the control, as printed on the hardware ("K1") */
} km_cell_t;
static const km_cell_t KM_CELLS[16] = {
    {KM_CC, 74, "K1"}, {KM_CC, 71, "K2"}, {KM_CC, 5, "K3"}, {KM_CC, 10, "K4"},
    {KM_CC, 73, "K5"}, {KM_CC, 75, "K6"}, {KM_CC, 79, "K7"}, {KM_CC, 72, "K8"},
    {KM_LEVEL, 0, "F1"}, {KM_LEVEL, 1, "F2"}, {KM_LEVEL, 2, "F3"}, {KM_LEVEL, 3, "F4"},
    {KM_SOUND, 0, "ENC"}, {KM_ENGINE, 0, "BNK"}, {KM_PRESS, 0, "NEXT"}, {KM_PADS, 3, "PAD"},
};
#define KM_Y 34                                          /* two columns of eight rows: the knobs, then the rest */
#define KM_W 116
#define KM_H 23
#define KM_GAP 2
static uint32_t km_sig[16];

/* cell k: its name (what the control does here), its value, the bar (fill 0..1000, -1 none; bipolar: from the middle) */
static void km_cell(uint32_t k, char *name, char *val, int32_t *fill, int *bip, uint16_t *bar)
{
    const km_cell_t *c = &KM_CELLS[k];
    const track_t *t = TSEL;
    const char *unit = 0;
    *fill = -1; *bip = 0; *bar = T_THEME;
    name[0] = val[0] = 0;
    if (c->kind == KM_CC && c->arg == 1u) {             /* CC1: the mod wheel's state on this track's channel */
        str_cpy(name, "MOD", 16);
        fmt_int(val, midi_ch[song.sel % 16u].wheel);
        *fill = midi_ch[song.sel % 16u].wheel * 1000 / 127;
    } else if (c->kind == KM_CC) {
        uint32_t id = midi_cc_id(t, c->arg);
        if (id == P_COUNT) {
            str_cpy(name, "-", 16);
        } else if (id & MIDI_POP_NATIVE) {              /* the engine's own envelope */
            str_cpy(name, c->arg == 73u ? "ATK" : c->arg == 75u ? "DEC" : "REL", 16);
            str_cpy(val, "ENG", 16);
        } else {
            const param_desc_t *d = id < P_E0 ? &TP[id] : &ENGINES[eng_idx(t->eng_req)]->edit[id - P_E0];
            int32_t v = t->p[id];
            str_cpy(name, d->label, 16);
            param_format(d, v, val, &unit);
            if (unit && unit[0] == '%')                 /* (units only where short: the row is narrow) */
                str_cpy(val + str_len(val), unit, 8);
            if (d->max > d->min)
                *fill = (v - d->min) * 1000 / (d->max - d->min);
            *bip = id == P_PAN;
        }
    } else if (c->kind == KM_LEVEL) {
        const track_t *f = &trk[c->arg];
        str_cpy(name, "VOL", 16);
        if (c->arg >= trk_n()) {
            str_cpy(val, "-", 8);
        } else {
            param_format(&TP[P_LEVEL], f->p[P_LEVEL], val, &unit);
            *fill = (f->p[P_LEVEL] - TP[P_LEVEL].min) * 1000 / (TP[P_LEVEL].max - TP[P_LEVEL].min);
            *bar = f->p[P_MUTE] ? T_DIM : T_TRK(c->arg);
        }
    } else if (c->kind == KM_SOUND) {
        sound_name(t, val);                             /* (no name: the value across the row) */
    } else if (c->kind == KM_ENGINE) {
        str_cpy(val, ENGINES[eng_idx(t->eng_req)]->name, 16);
    } else if (c->kind == KM_PRESS) {
        str_cpy(val, "T1", 16);
        val[1] = (char)('1' + (trk_n() ? (song.sel + 1u) % trk_n() : 0u));
    } else {                                            /* the pads: track 4 (channel 4) */
        str_cpy(val, "T4 ", 16);
        str_cpy(val + 3, c->arg < trk_n() ? ENGINES[eng_idx(trk[c->arg].eng_req)]->name : "NONE", 16);
    }
}
static void km_draw_cell(uint32_t k)
{
    char name[24], val[32];
    int32_t fill, x = BOX_X + (int32_t)(k / 8u) * (KM_W + 4), y = KM_Y + (int32_t)(k % 8u) * (KM_H + KM_GAP);
    int bip;
    uint16_t bar, src = KM_CELLS[k].kind == KM_LEVEL && KM_CELLS[k].arg < trk_n() ? T_TRK(KM_CELLS[k].arg) : T_MID;
    uint32_t sig;
    km_cell(k, name, val, &fill, &bip, &bar);
    sig = str_hash(str_hash(1u + (uint32_t)(fill + 2) * 31u + (uint32_t)bip + bar * 7u, name), val) + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == km_sig[k])
        return;
    km_sig[k] = sig;
    cv_begin(KM_W, KM_H, T_BG);
    cv_rrect(0, 0, KM_W, KM_H, 5, T_SURF, T_BG);
    int32_t tx = cv_text_on(4, ty_y(10, TY_LB), TY_LB, KM_CELLS[k].src, src, T_SURF) + 6;   /* (after the control) */
    if (tx < 30)
        tx = 30;
    if (name[0]) {                                      /* the target, its value at the right */
        int32_t vw = text_w(TY_LB, val);
        cv_free_text(tx, ty_y(10, TY_LB), TY_LB, name, name[0] == '-' ? T_DIM : T_TEXT, T_SURF, KM_W - tx - vw - 8);
        cv_text_r(KM_W - 4, ty_y(10, TY_LB), TY_LB, val, T_MID, T_SURF);
    } else {                                            /* a name alone (the sound, the engine, ...) across the row */
        cv_free_text(tx, ty_y(10, TY_LB), TY_LB, val, T_TEXT, T_SURF, KM_W - tx - 4);
    }
    if (fill >= 0) {
        int32_t w = KM_W - 8, f = clamp(fill, 0, 1000) * w / 1000;
        cv_rect(4, KM_H - 3, w, 2, T_LINE);
        if (bip) {
            int32_t m = w / 2;
            cv_rect(4 + (f < m ? f : m), KM_H - 3, f < m ? m - f : f - m + 1, 2, bar);
        } else if (f > 0) {
            cv_rect(4, KM_H - 3, f, 2, bar);
        }
    }
    cv_blit((uint32_t)x, (uint32_t)y);
}
static void knobmap_draw(void)
{
    uint32_t k;
    if (!ui.force && (ui.frame & 1u))
        return;
    if (ui.force)
        lcd_fill(0, H_HEAD, 240, 240u - H_HEAD, T_BG);
    for (k = 0; k < 16u; k++)
        km_draw_cell(k);
}
