/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee's Stage (HOME), the synth build's: the header (its chip: the engine); the sound's card across the screen:
 * a bar and its category in the theme's colour, the sound in two lines, the engine, what plays the keys (the ARP's
 * mode, HOLD, CHORD, the scale) as pills, and five rings: the level and the four sends (DST CHO DLY REV). Under it
 * the panel: the notes played and their chord in front of the waveform, smooth, dimmed while held. The first turn of a
 * knob drops the four knobs (the engine's, stage_page) down over the card's top (ST_DROP frames); while they are out
 * the panel shows what the knob turning shapes and its value; ST_KNOB_MS after the last turn they go and the card is
 * whole again. The card redraws when what it shows changes; the panel follows the scope every other frame, drawn last
 * (the SPI, 12 MHz, sends it while the next frame is worked out: a blit right after it would wait for it,
 * lcd_window). */
#define ST_COL_Y Y_LABEL                                /* the sound's card: x 4 .. 236, y 22 .. 146 */
#define ST_COL_H 124
#define ST_CARD_X 4
#define ST_CARD_W 232
#define ST_PANEL_Y (ST_COL_Y + ST_COL_H + 4)            /* the panel: y 150 .. 238 */
#define ST_PANEL_H 88
#define ST_KNOB_MS 1500u                                /* the knobs stay this long after the last turn */
#define ST_DROP 4u                                      /* .. and drop in over this many frames */

static struct {
    uint32_t col[NTRK];                                 /* the card's drawn state */
    uint32_t knob_ms;                                   /* the last knob turn on Stage (| 1), 0 none */
    uint8_t out, drop;                                  /* the knobs out; frames still dropping */
} stage;

static void stage_knob_touch(void) { stage.knob_ms = fm1_ms | 1u; }   /* (ui_input.c: a knob on Stage) */
static int stage_knobs_out(void) { return stage.knob_ms && fm1_ms - (stage.knob_ms & ~1u) < ST_KNOB_MS; }

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

/* While the knobs are out, the panel shows what the one turning shapes behind the notes: a filter's response (cutoff,
 * resonance; the Prophet's envelope amount too), an envelope (its attack .. release); 0 = nothing to show */
enum { SP_NONE, SP_FILTER, SP_ENV };
static uint32_t stage_picture(int32_t *v)
{
    const page_t *sp = stage_page();
    uint32_t hot = ui.hot_col & 3u, id = sp->id[hot], k;
    if (!stage_knobs_out())
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
/* a low-pass response: flat, the resonance's peak at the cutoff (a dotted line), then the slope; cut, res 0..127 */
static void stage_filter(int32_t cut, int32_t res, uint16_t c)
{
    int32_t x, fc = 10 + clamp(cut, 0, 127) * 200 / 127, peak = clamp(res, 0, 127) * 14 / 127;
    int32_t top = 3, bot = ST_PANEL_H - 4, y0 = 22 * 16, py = 0;
    for (x = top; x < bot; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_PANEL, c, 60));
    for (x = 0; x < 220; x++) {
        int32_t d = 8 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 10 : 0);
        y = clamp(y, top * 16, bot * 16);
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

static void stage_panel(void)
{
    uint32_t sel = song.sel % NTRK, pic, i;
    int32_t pv[4];
    int held = 0;
    cv_begin(240, ST_PANEL_H, T_BG);
    cv_rrect(4, 0, 232, ST_PANEL_H, 6, T_PANEL, T_BG);
    cv_bg = T_PANEL;
    fm1_irq_off();
    for (i = 0; i < 4u; i++) held |= live_held[sel][i] != 0u;
    fm1_irq_on();
    pic = stage_picture(pv);
    /* behind: the waveform (dimmed while notes are held), or what the knob turning shapes */
    if (pic == SP_FILTER)
        stage_filter(pv[0], pv[1], ux_mix(T_PANEL, T_THEME, 55));
    else if (pic == SP_ENV)
        graph_adsr_v(pv[0], pv[1], pv[2], pv[3], 4, ST_PANEL_H - 8, ux_mix(T_PANEL, T_THEME, 55));
    else
        stage_wave(held ? ux_mix(T_PANEL, T_THEME, 40) : T_THEME, 4, ST_PANEL_H - 8);
    cv_over = 1;                                        /* in front: blended over it */
    stage_notes(pic ? 176 : 228);
    if (pic) {                                          /* the value turning, at the top right */
        int16_t *vp;
        const char *unit;
        char val[12];
        const page_t *sp = stage_page();
        const param_desc_t *d = page_desc(sp, ui.hot_col & 3u, &vp);
        if (d && vp) {
            stage_value(sp, d, *vp, val, &unit);
            str_cpy(val + str_len(val), unit, 4);
            cv_text_r(228, 4, &AF_M, val, T_THEME, T_PANEL);
        }
    }
    cv_over = 0;
    cv_blit(0, ST_PANEL_Y);
}

/* ------------------------------------------------------- the card --- */
/* the sound as a list entry (browse.c cur_entry) */
static void stage_entry(uint32_t k, uint32_t *src, uint32_t *kk)
{
    uint8_t s = song.sel;
    song.sel = (uint8_t)k;
    cur_entry(src, kk);
    song.sel = s;
}
/* a name in two lines of w px: the first ends at the last space, '.' or '-' that fits (one long word: cut), the rest below */
static void stage_two_lines(const char *s, char *a, char *b, int32_t w)
{
    uint32_t i, cut = 0;
    for (i = 0; s[i] && i < 15u; i++)                   /* (cut: the first line's length; a space goes) */
        if (s[i] == ' ' || s[i] == '.' || s[i] == '-') {
            uint32_t n = i + (s[i] != ' ');
            str_cpy(a, s, n + 1u);
            if (n && text_w(&AF_M, a) <= w) cut = n;
        }
    if (!cut || text_w(&AF_M, s) <= w) {
        str_cpy(a, s, 16);
        b[0] = 0;
        return;
    }
    str_cpy(a, s, cut + 1u);
    str_cpy(b, s + cut + (s[cut] == ' '), 16);
}
/* a pill at x on row y: on in the colour, off outlined; its right edge + 4 */
static int32_t stage_pill(int32_t x, int32_t y, const char *s, int on, uint16_t tc, uint16_t bg)
{
    int32_t w = text_w(&AF_X, s) + 10;
    if (on) {
        cv_rrect(x, y, w, 14, 4, tc, bg);
        cv_text_c(x + w / 2, y + 1, &AF_X, s, T_INK, tc);
    } else {
        cv_rrect(x, y, w, 14, 4, T_LINE, bg);
        cv_rrect(x + 1, y + 1, w - 2, 12, 3, bg, T_LINE);
        cv_text_c(x + w / 2, y + 1, &AF_X, s, T_DIM, bg);
    }
    return x + w + 4;
}
/* a ring at centre (cx, cy): value v of 0..127 in the colour, its value inside and its label under it */
static void stage_ring(int32_t cx, int32_t cy, int32_t v, const char *val, const char *label, uint16_t tc, uint16_t bg)
{
    int32_t a1 = v ? -KA_END + v * 2 * KA_END / 127 : -KA_END - 1;
    knob_arc(cx - KNOB_RING_R, cy - KNOB_RING_R, KNOB_RING_R, KNOB_RING_COV, KNOB_RING_ANG, -KA_END, a1, T_LINE, tc, bg);
    cv_text_c(cx, cy - 5, &AF_X, val, v ? T_TEXT : T_DIM, bg);
    cv_text_c(cx, cy + KNOB_RING_R + 3, &AF_X, label, T_MID, bg);
}
static void stage_col(uint32_t k)
{
    static const char *const SEND[4] = {"DST", "CHO", "DLY", "REV"};
    const track_t *t = &trk[k];
    uint32_t mute = t->p[P_MUTE] != 0u, lvl = (uint32_t)t->p[P_LEVEL] & 127u, sig, src, kk, i;
    uint32_t amode = (uint32_t)clamp(t->p[P_AMODE], 0, 6), hold = t->p[P_AHOLD] != 0, chord = t->p[P_CHRD] != 0;
    int pending = browse_pending();
    uint16_t tc = mute ? T_DIM : T_THEME, bg = T_PANEL;
    char nm[16], l1[16], l2[16], scl[16], v[8];
    const char *en;
    if (pending) {                                      /* browsing: the sound the list shows, not yet loaded */
        char tag[6];
        src = browse_shown(&kk);
        entry_label(src, kk, tag, nm);
        if (!nm[0]) str_cpy(nm, tag, sizeof nm);
    } else {
        trk_short_name(k, nm);
        if (t->eng_req == ENGI_PROPHET)
            p5_short_name(t, nm, sizeof nm);
        stage_entry(k, &src, &kk);
    }
    en = ENGINES[eng_idx(pending ? src_engine(src, kk) : t->eng_req)]->name;
    str_cpy(scl, N_NOTE[(uint32_t)t->p[P_ROOT] % 12u], 4);
    str_cpy(scl + str_len(scl), " ", 2);
    str_cpy(scl + str_len(scl), N_SCALE[clamp(t->p[P_SCALE], 0, SCALE_TOTAL - 1u)], 12);
    sig = str_hash(str_hash(str_hash(1u + mute * 4u + (uint32_t)pending * 16u + stage.out * 32u, nm), en), scl) +
          lvl * 40503u + amode * 7919u + hold * 131u + chord * 613u + (uint32_t)t->p[P_QUANT] * 1291u + ux.gen * 977u +
          ux.pal * 31u + entry_cat(src, kk) * 61u;
    for (i = 0; i < 4u; i++)
        sig = (sig ^ (uint32_t)t->p[P_DIST + i]) * 16777619u;
    if (!ui.force && sig == stage.col[k])
        return;
    stage.col[k] = sig;
    cv_begin(ST_CARD_W, ST_COL_H, T_BG);
    cv_rrect(0, 0, ST_CARD_W, ST_COL_H, 6, bg, T_BG);
    if (!stage.out) {                                   /* (under the knobs, while they are out: not drawn) */
        uint32_t cat = entry_cat(src, kk) % CAT_N;
        cv_rect(3, 0, ST_CARD_W - 6, 3, tc);            /* the bar */
        cv_text_on(8, 6, &AF_X, cat && cat != CAT_OTHER ? CAT_NAME[cat] : "SOUND", tc, bg);   /* the category */
        cv_text_r(ST_CARD_W - 8, 6, &AF_X, en, T_MID, bg);                                    /* the engine */
        stage_two_lines(nm, l1, l2, ST_CARD_W - 16);
        cv_free_text(8, 20, &AF_M, l1, pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, ST_CARD_W - 16);
        if (l2[0])
            cv_free_text(8, 38, &AF_M, l2, pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, ST_CARD_W - 16);
    }
    {   /* what plays the keys: the ARP (its mode), HOLD, CHORD, the scale */
        static const char *const AM[7] = {"ARP", "ARP UP", "ARP DN", "ARP UPDN", "ARP RND", "ARP ORD", "REPEAT"};
        int32_t x = 8;
        x = stage_pill(x, 62, AM[amode], amode != 0u, tc, bg);
        x = stage_pill(x, 62, "HOLD", (int)hold, tc, bg);
        x = stage_pill(x, 62, "CHORD", (int)chord, tc, bg);
        (void)stage_pill(x, 62, scl, t->p[P_QUANT] != Q_OFF, tc, bg);
    }
    {   /* the level and the sends: five rings */
        int32_t cx = 26;
        if (lvl) {
            int32_t d = LEVEL_DB_X10[lvl];
            fmt_int(v, (d + (d < 0 ? -5 : 5)) / 10);
        } else {
            str_cpy(v, "OFF", sizeof v);
        }
        stage_ring(cx, 94, (int32_t)lvl, v, "LEVEL", tc, bg);
        for (i = 0; i < 4u; i++) {
            int32_t s = clamp(t->p[P_DIST + i], 0, 127);
            cx += 45;
            fmt_int(v, s * 100 / 127);
            stage_ring(cx, 94, s, v, SEND[i], tc, bg);
        }
    }
    if (stage.out)                                      /* the knobs over the top: the rows under them only */
        cv_blit_from(ST_CARD_X, ST_COL_Y, CARD_H);
    else
        cv_blit(ST_CARD_X, ST_COL_Y);
}

/* the knobs: out while one turns (and ST_KNOB_MS after), dropping in over ST_DROP frames (each its cards' bottom rows,
 * more each frame: no column under them redraws); gone at once, the columns whole again */
static void stage_knobs(void)
{
    int out = stage_knobs_out();
    if (out && !stage.out) {
        stage.out = 1;
        stage.drop = ST_DROP;
    } else if (!out && stage.out) {
        stage.out = 0;
        memset(stage.col, 0, sizeof stage.col);         /* (the card's top back) */
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

/* the BG around the columns and the panel (a full redraw: the canvases cover the rest) */
static void stage_frame(void)
{
    lcd_fill(0, ST_COL_Y, ST_CARD_X, ST_COL_H, T_BG);   /* left and right of the card */
    lcd_fill(ST_CARD_X + ST_CARD_W, ST_COL_Y, 240u - ST_CARD_X - ST_CARD_W, ST_COL_H, T_BG);
    lcd_fill(0, ST_COL_Y + ST_COL_H, 240, ST_PANEL_Y - ST_COL_Y - ST_COL_H, T_BG);
    lcd_fill(0, ST_PANEL_Y + ST_PANEL_H, 240, 240u - ST_PANEL_Y - ST_PANEL_H, T_BG);
}

/* every frame the knobs; every other frame (the scope's) the card, then the panel last */
static void stage_draw(void)
{
    uint32_t k;
    stage_knobs();
    if (!ui.force && (ui.frame & 1u))
        return;
    for (k = 0; k < NTRK; k++)
        stage_col(k);
    stage_panel();
}
