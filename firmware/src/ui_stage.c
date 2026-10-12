/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee's Stage (HOME) in the synth build: the one sound, drawn as Stage's columns draw a track, across the screen
 * (docs/design: concept_stage_columns, stage_note_overlay). Under the header, the sound's card: the bar and the
 * category in the theme's colour, the name large (Rubik 30 px when it fits, else two lines of 15 px), the engine and
 * where the sound comes from, and the four pills of what plays the keys (the ARP's mode, HOLD, CHORD, the scale) in the
 * lights' grammar: filled happening now, outlined and dim there to turn on. Under the card the panel: the chord and
 * notes played in front of the smooth waveform, dimmed while held. The first turn of a knob drops the four knobs (the
 * engine's, stage_page) over the card's top (ST_DROP frames) and the panel shows what the knob shapes and its value;
 * ST_KNOB_MS after the last turn they go and the card is whole again. The card redraws when what it shows changes;
 * the panel follows the scope every other frame, drawn last (the SPI sends it while the next frame is worked out). */
#define ST_CARD_X 4                                     /* the sound's card: x 4 .. 236, y 22 .. 122 */
#define ST_CARD_Y Y_LABEL
#define ST_CARD_W 232
#define ST_CARD_H 100
#define ST_PANEL_Y (ST_CARD_Y + ST_CARD_H + 4)          /* the panel: y 126 .. 238 */
#define ST_PANEL_H (238 - ST_PANEL_Y)
#define ST_KNOB_MS 1500u                                /* the knobs stay this long after the last turn */
#define ST_DROP 4u                                      /* .. and drop in over this many frames */
_Static_assert(240u * ST_PANEL_H <= CV_MAX, "the panel fits a canvas");

static struct {
    uint32_t card;                                      /* the card's drawn state */
    uint32_t knob_ms;                                   /* the last knob turn on Stage (| 1), 0 none */
    uint8_t out, drop;                                  /* the knobs out; frames still dropping */
} stage;

static void stage_knob_touch(void) { stage.knob_ms = fm1_ms | 1u; }   /* (ui_input.c: a knob on Stage) */
static int stage_knob_active(void) { return stage.knob_ms && fm1_ms - (stage.knob_ms & ~1u) < ST_KNOB_MS; }

/* ------------------------------------------------------- the knobs --- */
/* a knob's label: the panel's, plain words for CZ-1's (its panel says R1, L1 ..) */
static const char *stage_label(uint32_t k, const param_desc_t *d)
{
    static const char *const CZ_L[4] = {"WAVE", "DCW", "DETUNE", "VIBRATO"};
    return stage_pg.scope == SC_CZ1 ? CZ_L[k & 3u] : d->label;
}
/* a knob's value as Stage shows it: the Prophet's 0..127 panel values in % (the mockup's CUTOFF 62 %) */
static void stage_value(const page_t *sp, const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    if (sp->scope == SC_P5 && d->fmt == F_INT && d->min == 0 && d->max == 127) {
        fmt_int(val, (v * 100 + 63) / 127);
        *unit = "%";
        return;
    }
    param_format(d, v, val, unit);
}
/* the four cards (while the knobs are out): a value without effect on the sound as it is drawn dim */
static void stage_columns(void)
{
    const page_t *sp = stage_page();
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const char *unit;
        char val[12];
        const param_desc_t *d = page_desc(sp, c, &vp);
        int dim = (sp->scope == SC_CZ1 && !cz_ed_active(cz_patch[song.sel % NTRK].raw, sp->id[c])) ||
                  (sp->scope == SC_P5 && !p5_ed_active(TSEL, sp->id[c]));
        if (!d || !vp || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
            continue;
        }
        stage_value(sp, d, *vp, val, &unit);
        draw_column(c, stage_label(c, d), val, unit, dim ? T_DIM : VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    ICON_NONE);
    }
}

/* ------------------------------------------------------- the panel --- */
/* the output's waveform over the panel's width, smooth: triggered on a rising zero crossing, auto-scaled, each sample
 * the mean of three, 2 px anti-aliased (gfx.c cv_vspan_aa); rows top .. top + h */
static void stage_wave(uint16_t c, int32_t top, int32_t h)
{
    int16_t *snap = scope_snap;
    uint32_t w = scope_w, i, trig = 1;
    int32_t peak = 1500, x, py = 0, cy = (top + h / 2) * 16 - 16;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak) peak = snap[i];
        else if (-snap[i] > peak) peak = -snap[i];
    }
    for (i = 2; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) { trig = i; break; }
    for (x = 0; x < 224; x++) {
        uint32_t k = trig + (uint32_t)x;
        int32_t v = ((int32_t)snap[k - 1u] + 2 * snap[k] + snap[k + 1u]) / 4;
        int32_t y = cy - v * (h / 2 - 4) * 16 / peak;
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

/* the notes the selected track played (live_last: its last voicing, held or let go), the chord they make, in front of
 * what is on the canvas (cv_over): the root 30 px, its quality 15 px, then the notes 11 px, on one baseline (38).
 * Held: the chord in text, the notes in the track's colour; let go: both faded into the panel. 0 none, 1 let go, 2 held */
static int stage_notes(int32_t x1)
{
    uint32_t bits[4], i, n = 0, pcs = 0, root = 0, bass, held = 0, k = song.sel % NTRK;
    uint8_t note[8];
    char b[48];
    const char *q;
    uint16_t c, cn;
    int32_t x = 10;
    fm1_irq_off();                                      /* one coherent ISR snapshot, nothing drawn meanwhile */
    for (i = 0; i < 4u; i++) {
        bits[i] = live_last[k][i];
        held |= live_held[k][i];
    }
    fm1_irq_on();
    for (i = 0; i < 128u; i++)
        if ((bits[i >> 5] >> (i & 31u)) & 1u) {
            pcs |= 1u << (i % 12u);
            if (n < sizeof note) note[n] = (uint8_t)i;
            n++;
        }
    if (!n) return 0;
    c = held ? T_TEXT : ux_mix(T_PANEL, T_THEME, 75);
    cn = held ? T_THEME : ux_mix(T_PANEL, T_THEME, 55);
    bass = note[0] % 12u;
    q = micro_active(TSEL) ? 0 : chord_of(pcs, bass, &root);
    if (q) {
        x = cv_text(x, 9, &AF_L, N_NOTE[root], c);
        x = cv_text(x + 1, 23, &AF_M, q, c);
        if (root != bass) {
            x = cv_text(x + 3, 9, &AF_L, "/", c);
            x = cv_text(x, 9, &AF_L, N_NOTE[bass], c);
        }
        x += 10;
    }
    notes_fit(b, note, n, &AF_S, x1 - x);
    cv_text(x, 27, &AF_S, b, cn);
    return (int)(1u + (held != 0u));
}
/* HOME's notes alone (tests/ui_test.c): 1 = there were notes */
static int graph_notes(void) { return stage_notes(220) != 0; }

/* After a knob turns, the panel shows what it shapes behind the notes: a filter's response (cutoff,
 * resonance; the Prophet's envelope amount too), an envelope (its attack .. release); 0 = nothing to show */
enum { SP_NONE, SP_FILTER, SP_ENV };
static uint32_t stage_picture(int32_t *v)
{
    const page_t *sp = stage_page();
    uint32_t hot = ui.hot_col & 3u, id = sp->id[hot], k;
    if (!stage_knob_active())
        return SP_NONE;
    if (sp->scope == SC_P5) {
        const uint8_t *r = p5_patch_of(TSEL)->raw;
        if (id == P5_CUTOFF || id == P5_RESONANCE || id == P5_ENV_FILTER) {
            v[0] = r[P5_CUTOFF];
            v[1] = r[P5_RESONANCE];
            return SP_FILTER;
        }
        if (id == P5_RELEASE_AMP) {
            v[0] = r[P5_ATTACK_AMP]; v[1] = r[P5_DECAY_AMP]; v[2] = r[P5_SUSTAIN_AMP]; v[3] = r[P5_RELEASE_AMP];
            return SP_ENV;
        }
        return SP_NONE;
    }
    if (sp->scope != SC_ENGINE)
        return SP_NONE;
    if (id >= P_ATK && id <= P_REL) {
        for (k = 0; k < 4u; k++)
            v[k] = TSEL->p[P_ATK + k];
        return SP_ENV;
    }
    {   /* an engine's filter: its F_CUTOFF knob and the RES beside it */
        int32_t cut = -1, res = 0;
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d = track_desc(TSEL, sp->id[k]);
            if (d->fmt == F_CUTOFF) cut = (int32_t)k;
            else if (str_eq(d->label, "RES")) res = TSEL->p[sp->id[k]];
        }
        if (cut < 0 || (hot != (uint32_t)cut && !str_eq(track_desc(TSEL, id)->label, "RES")))
            return SP_NONE;
        v[0] = TSEL->p[sp->id[cut]];
        v[1] = res;
        return SP_FILTER;
    }
}
/* a low-pass response: flat, the resonance's peak at the cutoff (a dotted line), then the slope; cut, res 0..127. In
 * the band from cv_oy + 3 down to the panel's foot */
static void stage_filter(int32_t cut, int32_t res, uint16_t c)
{
    int32_t x, fc = 10 + clamp(cut, 0, 127) * 200 / 127, peak = clamp(res, 0, 127) * 14 / 127;
    int32_t top = 3, bot = ST_PANEL_H - 6 - cv_oy, y0 = 22 * 16, py = 0;
    for (x = top; x < bot; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_PANEL, c, 60));
    for (x = 0; x < 220; x++) {
        int32_t d = 8 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 10 : 0);
        y = clamp(y, top * 16, bot * 16);
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

/* the panel: the chord and notes played on a line at its top (none played yet: nothing there), the waveform in the
 * band under them (none played: the whole panel), dimmed while they are held; a knob turning: what it shapes in that
 * band and its value at the top right */
#define ST_TEXT_H 44                                    /* the chord's line: the panel's rows 0 .. 43 */
static void stage_panel(void)
{
    uint32_t sel = song.sel % NTRK, pic, i, any = 0;
    int32_t pv[4], top;
    int held = 0;
    cv_begin(240, ST_PANEL_H, T_BG);
    cv_rrect(4, 0, 232, ST_PANEL_H, 6, T_PANEL, T_BG);
    cv_bg = T_PANEL;
    fm1_irq_off();
    for (i = 0; i < 4u; i++) {
        held |= live_held[sel][i] != 0u;
        any |= live_last[sel][i];
    }
    fm1_irq_on();
    pic = stage_picture(pv);
    top = any || pic ? ST_TEXT_H : 6;                   /* the band behind: under the chord's line */
    if (pic == SP_FILTER) {
        cv_oy = top - 3;
        stage_filter(pv[0], pv[1], ux_mix(T_PANEL, T_THEME, 55));
        cv_oy = 0;
    } else if (pic == SP_ENV) {
        graph_adsr_v(pv[0], pv[1], pv[2], pv[3], top, ST_PANEL_H - 8, ux_mix(T_PANEL, T_THEME, 55));
    } else {
        stage_wave(held ? ux_mix(T_PANEL, T_THEME, 40) : T_THEME, top, ST_PANEL_H - 6 - top);
    }
    cv_over = 1;                                        /* in front: blended over it */
    stage_notes(pic ? 168 : 228);
    if (pic) {                                          /* the value turning, at the top right */
        int16_t *vp;
        const char *unit;
        char val[12];
        const page_t *sp = stage_page();
        const param_desc_t *d = page_desc(sp, ui.hot_col & 3u, &vp);
        if (d && vp) {
            stage_value(sp, d, *vp, val, &unit);
            str_cpy(val + str_len(val), unit, 4);
            cv_text_r(226, 12, &AF_M, val, T_THEME, T_PANEL);
        }
    }
    cv_over = 0;
    cv_blit(0, ST_PANEL_Y);
}

/* ------------------------------------------------------- the card --- */
/* the sound's name as large as it fits: 30 px on one line (rows 16 .. 52), else 30 px on two (cut at the last space,
 * '.' or '-' that fits; rows 12 .. 78, over the engine's row: the header chip names it), else 15 px, free text.
 * 1: two lines */
static int stage_name(const char *s, uint16_t c, uint16_t bg)
{
    char a[24], b[24];
    uint32_t i, cut = 0;
    const int32_t w = ST_CARD_W - 20;
    if (text_w(&AF_L, s) <= w) {
        cv_text_on(10, 16, &AF_L, s, c, bg);
        return 0;
    }
    for (i = 0; s[i] && i < 22u; i++)                   /* (cut: the first line's length; a space goes) */
        if (s[i] == ' ' || s[i] == '.' || s[i] == '-') {
            uint32_t n = i + (s[i] != ' ');
            str_cpy(a, s, n + 1u);
            if (n && text_w(&AF_L, a) <= w) cut = n;
        }
    if (cut) {
        str_cpy(a, s, cut + 1u);
        str_cpy(b, s + cut + (s[cut] == ' '), sizeof b);
        if (text_w(&AF_L, b) <= w) {
            cv_text_on(10, 12, &AF_L, a, c, bg);
            cv_text_on(10, 42, &AF_L, b, c, bg);
            return 1;
        }
    }
    cv_free_text(10, 28, &AF_M, s, c, bg, w);
    return 0;
}
/* a pill at x on row y, at most maxw wide: on, filled in c; off, outlined and dim (the lights' grammar). Its right edge
 * + 4. A name that does not fit is free text (ellipsised: a scale's) */
static int32_t stage_pill(int32_t x, int32_t y, const char *s, int on, uint16_t c, uint16_t bg, int32_t maxw)
{
    int32_t w = text_w(&AF_X, s) + 12, fill = on ? c : bg, ink = on ? T_INK : T_DIM;
    if (w > maxw)
        w = maxw;
    if (on) {
        cv_rrect(x, y, w, 16, 5, c, bg);
    } else {
        cv_rrect(x, y, w, 16, 5, T_LINE, bg);
        cv_rrect(x + 1, y + 1, w - 2, 14, 4, bg, T_LINE);
    }
    if (text_w(&AF_X, s) + 12 > maxw)
        cv_free_text(x + 6, y + 2, &AF_X, s, (uint16_t)ink, (uint16_t)fill, w - 12);
    else
        cv_text_c(x + w / 2, y + 2, &AF_X, s, (uint16_t)ink, (uint16_t)fill);
    return x + w + 4;
}
static void stage_card(void)
{
    static const char *const AM[7] = {"ARP", "ARP UP", "ARP DN", "ARP UPDN", "ARP RND", "ARP ORD", "REPEAT"};
    const track_t *t = TSEL;
    uint32_t src, kk, cat, sig, amode = (uint32_t)clamp(t->p[P_AMODE], 0, 6);
    uint32_t hold = t->p[P_AHOLD] != 0, chord = t->p[P_CHRD] != 0, qnt = t->p[P_QUANT] != Q_OFF;
    int pending = browse_pending();
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_THEME, bg = T_PANEL;
    char nm[24], tag[8], where[16], scl[16];
    const char *en;
    int two = 0;
    if (pending) {                                      /* browsing: the sound the list shows, not yet loaded */
        src = browse_shown(&kk);
        entry_label(src, kk, tag, nm);
        if (!nm[0]) str_cpy(nm, tag, sizeof nm);
    } else {
        sound_name(t, nm);
        if (!nm[0]) str_cpy(nm, ENGINES[eng_idx(t->eng_req)]->name, sizeof nm);
        cur_entry(&src, &kk);
    }
    en = ENGINES[eng_idx(src_engine(src, kk))]->name;
    if (src_slot(src)) {                                /* a user or native slot: its tag ("U07", "P012") */
        char n2[24];
        entry_label(src, kk, where, n2);
    } else {                                            /* a factory sound: its place in the engine's ("12 / 200") */
        uint32_t np = ENGINES[eng_idx(src)]->npresets;
        fmt_int(where, (int32_t)(kk % (np ? np : 1u)) + 1);
        str_cpy(where + str_len(where), " / ", 4);
        fmt_int(where + str_len(where), (int32_t)np);
    }
    cat = entry_cat(src, kk) % CAT_N;
    if (qnt) {                                          /* the keys in a scale: its root and name ("C MIN") */
        str_cpy(scl, N_NOTE[(uint32_t)t->p[P_ROOT] % 12u], 4);
        str_cpy(scl + str_len(scl), " ", 2);
        str_cpy(scl + str_len(scl), N_SCALE[clamp(t->p[P_SCALE], 0, (int32_t)NELEM(N_SCALE) - 1)], 10);
    } else {
        str_cpy(scl, "SCALE", sizeof scl);
    }
    sig = str_hash(str_hash(str_hash(str_hash(1u + (uint32_t)pending * 16u + stage.out * 32u + t->p[P_MUTE] * 64u, nm),
                   en), where), scl) + cat * 61u + amode * 7919u + hold * 131u + chord * 613u + qnt * 1291u +
          ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == stage.card)
        return;
    stage.card = sig;
    cv_begin(ST_CARD_W, ST_CARD_H, T_BG);
    cv_rrect(0, 0, ST_CARD_W, ST_CARD_H, 6, bg, T_BG);
    if (!stage.out) {                                   /* (under the knobs, while they are out: not drawn) */
        cv_rect(3, 0, ST_CARD_W - 6, 3, tc);            /* the bar */
        cv_text_on(10, 5, &AF_X, cat && cat != CAT_OTHER ? CAT_NAME[cat] : "SOUND", tc, bg);
        cv_text_r(ST_CARD_W - 10, 5, &AF_X, where, T_MID, bg);
        two = stage_name(nm, pending ? T_ACCENT : t->p[P_MUTE] ? T_DIM : T_TEXT, bg);
    }
    if (!two)
        cv_text_on(10, 60, &AF_X, en, T_MID, bg);       /* the engine (two lines of name: the header chip's) */
    {   /* what plays the keys */
        int32_t x = 10, r = ST_CARD_W - 10;
        x = stage_pill(x, 80, AM[amode], amode != 0u, tc, bg, r - x);
        x = stage_pill(x, 80, "HOLD", (int)hold, tc, bg, r - x);
        x = stage_pill(x, 80, "CHORD", (int)chord, tc, bg, r - x);
        (void)stage_pill(x, 80, scl, (int)qnt, tc, bg, r - x);
    }
    if (stage.out)                                      /* the knobs over the top: the rows under them only */
        cv_blit_from(ST_CARD_X, ST_CARD_Y, CARD_H);     /* (two lines of name: their second shows under them) */
    else
        cv_blit(ST_CARD_X, ST_CARD_Y);
}

/* the knobs: out while one turns (and ST_KNOB_MS after), dropping in over ST_DROP frames (each its cards' bottom rows,
 * more each frame: the card under them does not redraw); gone at once, the card whole again */
static void stage_knobs(void)
{
    int out = stage_knob_active();
    if (out && !stage.out) {
        stage.out = 1;
        stage.drop = ST_DROP;
    } else if (!out && stage.out) {
        stage.out = 0;
        stage.card = 0;                                 /* (the card's top back) */
        memset(ui.col, 0, sizeof ui.col);
        return;
    }
    if (!stage.out)
        return;
    if (stage.drop) {                                   /* each card whole (its value snaps, no roll), its bottom rows */
        uint32_t c;
        for (c = 0; c < 4u; c++) {
            ui.col[c][0] = 0;
            ui.roll[c].sig = (uint8_t)~ui.roll[c].sig;
        }
        stage_drop_rows = (uint8_t)((ST_DROP + 1u - stage.drop) * CARD_H / ST_DROP);
        stage_columns();
        stage_drop_rows = 0;
        stage.drop--;
        return;
    }
    stage_columns();
}

/* the BG around the card and the panel (a full redraw: the canvases cover the rest) */
static void stage_frame(void)
{
    lcd_fill(0, ST_CARD_Y, ST_CARD_X, ST_CARD_H, T_BG);   /* left and right of the card */
    lcd_fill(ST_CARD_X + ST_CARD_W, ST_CARD_Y, 240u - ST_CARD_X - ST_CARD_W, ST_CARD_H, T_BG);
    lcd_fill(0, ST_CARD_Y + ST_CARD_H, 240, ST_PANEL_Y - ST_CARD_Y - ST_CARD_H, T_BG);
    lcd_fill(0, ST_PANEL_Y + ST_PANEL_H, 240, 240u - ST_PANEL_Y - ST_PANEL_H, T_BG);
}

/* every frame the knobs; every other frame (the scope's) the card, then the panel last */
static void stage_draw(void)
{
    stage_knobs();
    if (!ui.force && (ui.frame & 1u))
        return;
    stage_card();
    stage_panel();
}
