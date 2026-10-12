/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee's Stage (HOME) in the synth build: the previous firmware's Stage with its one track. The track's column,
 * across the four columns' place, in their four colours (on the panel's fill, the rings' tracks showing): the four
 * columns' bars, the role (the sound's category) in the track's colour, the sound, the engine, its level as the ring
 * at the top right, the pills of what plays the keys as the patterns' were drawn, each in a column's colour (filled:
 * on, dashed: HOLD waiting for the ARP), its voices as the step dots, four a column slot in its colour (lit while
 * sounding, the newest larger in text, releasing dimmer), and its four sends as the level's ring under the slots, each
 * in its slot's colour. Under it the panel, unchanged: the notes played and their chord in front of the waveform,
 * smooth, dimmed while held. The first turn of a knob drops the track's four knobs (its engine's, stage_page), each in
 * its slot's colour, down over the card's top (ST_DROP frames); while they are out the panel shows what the knob
 * turning shapes and its value, in its colour;
 * ST_KNOB_MS after the last turn they go and the card is whole again.
 * Each part remembers what it drew: the card redraws when its sound, pills, level or sends change, the voices their
 * row alone; the panel follows the scope every other frame, drawn last (the SPI, 12 MHz, sends it while the next frame
 * is worked out: a blit right after it would wait for it, lcd_window). */
#define ST_COL_Y Y_LABEL                                /* the card: the four columns' place, x 4 .. 236, y 22 .. 174 */
#define ST_COL_X CARD_X(0)
#define ST_COL_W (CARD_X(3) + CARD_W - CARD_X(0))
#define ST_COL_H 152
#define ST_VOICE_Y 86                                   /* .. its voices: a row of dots, rows 86 .. 97 (a canvas of their
                                                         * own) */
#define ST_VOICE_H 12
#define ST_PANEL_Y (ST_COL_Y + ST_COL_H + 4)            /* the panel: y 178 .. 238 */
#define ST_PANEL_H 60
#define ST_KNOB_MS 1500u                                /* the knobs stay this long after the last turn */
#define ST_DROP 4u                                      /* .. and drop in over this many frames */

static struct {
    uint32_t card, voices;                              /* the card's drawn state, its voices' */
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
    uint16_t theme = ux.theme;
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const char *unit;
        char val[12];
        const param_desc_t *d = page_desc(sp, c, &vp);
        int dim = (sp->scope == SC_CZ1 && !cz_ed_active(cz_patch[song.sel % NTRK].raw, sp->id[c])) ||
                  (sp->scope == SC_P5 && !p5_ed_active(TSEL, sp->id[c]));
        ux.theme = T_TRK(c);                            /* each card in its column slot's colour */
        if (!d || !vp || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
            continue;
        }
        stage_value(sp, d, *vp, val, &unit);
        draw_column(c, stage_label(c, d), val, unit, dim ? T_DIM : VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    ICON_NONE);
    }
    ux.theme = theme;
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
    /* behind: the waveform (dimmed while notes are held), or what the knob turning shapes (in its card's colour) */
    if (pic == SP_FILTER)
        stage_filter(pv[0], pv[1], ux_mix(T_PANEL, T_TRK(ui.hot_col), 55));
    else if (pic == SP_ENV)
        graph_adsr_v(pv[0], pv[1], pv[2], pv[3], 4, ST_PANEL_H - 8, ux_mix(T_PANEL, T_TRK(ui.hot_col), 55));
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
            cv_text_r(228, 4, &AF_M, val, T_TRK(ui.hot_col), T_PANEL);
        }
    }
    cv_over = 0;
    cv_blit(0, ST_PANEL_Y);
}

/* --------------------------------------------------------- the card --- */
/* a name in two lines of w px: the first ends at the last space, '.' or '-' that fits (one long word: cut), the rest below */
static void stage_two_lines(const char *s, char *a, char *b, int32_t w)
{
    uint32_t i, cut = 0;
    for (i = 0; s[i] && i < 15u; i++)                   /* (cut: the first line's length; a space goes) */
        if (s[i] == ' ' || s[i] == '.' || s[i] == '-') {
            uint32_t n = i + (s[i] != ' ');
            str_cpy(a, s, n + 1u);
            if (n && text_w(&AF_S, a) <= w) cut = n;
        }
    if (!cut || text_w(&AF_S, s) <= w) {
        str_cpy(a, s, 16);
        b[0] = 0;
        return;
    }
    str_cpy(a, s, cut + 1u);
    str_cpy(b, s + cut + (s[cut] == ' '), 16);
}
/* a pill on row y at x, as the patterns' were drawn: filled in c (on), dashed (waiting); its right edge + 3 */
static int32_t stage_pill(int32_t x, int32_t y, const char *s, int dashed, uint16_t c, uint16_t bg)
{
    int32_t w = text_w(&AF_X, s) + 10;
    if (dashed) {
        cv_dashed(x, y, w, 14, 3, c);
        cv_text_c(x + w / 2, y + 1, &AF_X, s, c, bg);
    } else {
        cv_rrect(x, y, w, 14, 4, c, bg);
        cv_text_c(x + w / 2, y + 1, &AF_X, s, T_INK, c);
    }
    return x + w + 3;
}
/* the track's voices as the steps were drawn: a dot a voice its engine can sound, four a column slot in its colour, lit
 * while it sounds, dimmer while it releases, the newest larger in text; ox: the card's x 0 on the canvas */
static uint32_t stage_voice_state(const track_t *t, uint32_t i)   /* 0 free, 1 releasing, 2 sounding, 3 the newest */
{
    const voice_t *v = &t->v[i];
    if (!v->active || v->stage == 4u)
        return 0;
    if (v->stage == 3u)
        return 1;
    return i == t->m_vi ? 3u : 2u;
}
static void stage_voices_at(const track_t *t, uint16_t bg, int32_t ox)
{
    uint32_t i, n = trk_nvoice(t);
    for (i = 0; i < n && i < NVOICE; i++) {             /* four a column slot, as its steps' dots, in its colour */
        uint32_t s = stage_voice_state(t, i), k = i / 4u;
        uint16_t c = T_TRK(k);
        int32_t x = ox + CARD_X(k) - ST_COL_X + 8 + (int32_t)(i % 4u) * 113 / 10;
        cv_circle(x, 6, s == 3u ? 6 : 5, s == 3u ? T_TEXT : s == 2u ? c : s == 1u ? ux_mix(bg, c, 40) : T_LINE, bg);
    }
}
static uint32_t stage_voice_sig(const track_t *t)
{
    uint32_t i, h = trk_nvoice(t) * 977u;
    for (i = 0; i < NVOICE; i++)
        h = h * 4u + stage_voice_state(t, i);
    return h + 1u;
}
/* the card's parts, in the card's rows (the canvas holds a slice of them: cv_oy) */
typedef struct {
    char nm[24], scl[16];
    const char *en;
    uint32_t cat, lvl, amode, hold, chord, qnt, mute;
    int16_t send[4];
    int pending;
    uint16_t tc, bg;
} stage_card_t;
/* the name in the knob values' type (15 px), in the first three column slots: one line, else two (cut at the last
 * space, '.' or '-' that fits), else free text; rows 19 .. 53 */
static void stage_name(const char *s, uint16_t c, uint16_t bg)
{
    char a[24], b[24];
    uint32_t i, cut = 0;
    const int32_t w = CARD_X(3) - ST_COL_X - 8;         /* (the level's ring in the last slot) */
    if (text_w(&AF_M, s) <= w) {
        cv_text_on(6, 19, &AF_M, s, c, bg);
        return;
    }
    for (i = 0; s[i] && i < 22u; i++)
        if (s[i] == ' ' || s[i] == '.' || s[i] == '-') {
            uint32_t n = i + (s[i] != ' ');
            str_cpy(a, s, n + 1u);
            if (n && text_w(&AF_M, a) <= w) cut = n;
        }
    if (!cut) {
        cv_free_text(6, 19, &AF_M, s, c, bg, w);
        return;
    }
    str_cpy(a, s, cut + 1u);
    str_cpy(b, s + cut + (s[cut] == ' '), sizeof b);
    cv_text_on(6, 19, &AF_M, a, c, bg);
    cv_free_text(6, 36, &AF_M, b, c, bg, w);
}
/* a ring as the columns' level was drawn (KNOB_RING, 3 px, from 7:30, its value inside), its label over it in c, centred
 * on cx; v 0..127 */
static void stage_ring(int32_t cx, int32_t cy, const char *label, int32_t v, const char *val, uint16_t c, uint16_t bg)
{
    int32_t a1 = v ? -KA_END + v * 2 * KA_END / 127 : -KA_END - 1;
    if (label)
        cv_text_c(cx, cy - 30, &AF_X, label, c, bg);
    knob_arc(cx - KNOB_RING_R, cy - KNOB_RING_R, KNOB_RING_R, KNOB_RING_COV, KNOB_RING_ANG, -KA_END, a1, T_LINE, c, bg);
    cv_text_c(cx, cy - 5, &AF_X, val, v ? T_TEXT : T_DIM, bg);
}
static void stage_card_draw(const track_t *t, const stage_card_t *c)
{
    static const char *const AM[7] = {"ARP", "ARP UP", "ARP DN", "ARP UPDN", "ARP RND", "ARP ORD", "REPEAT"};
    static const char *const SEND[4] = {"DST", "CHO", "DLY", "REV"};
    uint32_t i;
    char v[8];
    cv_rrect(0, 0, ST_COL_W, ST_COL_H, 6, c->bg, T_BG);
    if (!stage.out) {                                   /* (under the knobs, while they are out: not drawn) */
        for (i = 0; i < 4u; i++)                        /* the four columns' bars, each in its colour */
            cv_rect(CARD_X(i) - ST_COL_X + 3, 0, CARD_W - 6, 3, c->mute ? T_DIM : T_TRK(i));
        cv_text_on(6, 6, &AF_X, c->cat == CAT_KEYS ? "CHORDS" : c->cat && c->cat != CAT_OTHER ? CAT_NAME[c->cat] : "T1",
                   c->tc, c->bg);
        stage_name(c->nm, c->pending ? T_ACCENT : c->mute ? T_DIM : T_TEXT, c->bg);
        if (c->lvl) {                                   /* the level, its ring at the top right (dB) */
            int32_t d = LEVEL_DB_X10[c->lvl];
            fmt_int(v, (d + (d < 0 ? -5 : 5)) / 10);
        } else {
            str_cpy(v, "OFF", sizeof v);
        }
        stage_ring(CARD_X(3) - ST_COL_X + 27, 30, 0, (int32_t)c->lvl, v, c->tc, c->bg);
    }
    cv_text_fit(6, 55, &AF_X, c->en, T_MID, c->bg, ST_COL_W - 8);
    {   /* what plays the keys, as the patterns' pills, each in a column's colour: on filled, HOLD with the ARP off
         * dashed (waiting) */
        int32_t x = 6;
        if (c->amode)
            x = stage_pill(x, 69, AM[c->amode], 0, T_TRK(0), c->bg);
        if (c->hold)
            x = stage_pill(x, 69, "HOLD", !c->amode, T_TRK(1), c->bg);
        if (c->chord)
            x = stage_pill(x, 69, "CHORD", 0, T_TRK(2), c->bg);
        if (c->qnt)
            (void)stage_pill(x, 69, c->scl, 0, T_TRK(3), c->bg);
    }
    cv_oy += ST_VOICE_Y;                                /* the voices, as the step dots */
    stage_voices_at(t, c->bg, 0);
    cv_oy -= ST_VOICE_Y;
    for (i = 0; i < 4u; i++) {                          /* the sends: a ring under each column slot, in its colour */
        int32_t s = clamp(c->send[i], 0, 127);
        fmt_int(v, s * 100 / 127);
        stage_ring(CARD_X(i) - ST_COL_X + 27, 134, SEND[i], s, v, c->mute ? T_DIM : T_TRK(i), c->bg);
    }
}
#define ST_SLICE 84                                     /* the card in two canvases: rows 0 .. 83 (the knobs' rows, the
                                                         * pills), 84 .. 151 (the voices, the ring) */
static void stage_card(void)
{
    const track_t *t = TSEL;
    uint32_t k = song.sel % NTRK, sig, src, kk, vs, top;
    stage_card_t c;
    c.mute = t->p[P_MUTE] != 0u;
    c.lvl = (uint32_t)t->p[P_LEVEL] & 127u;
    c.amode = (uint32_t)clamp(t->p[P_AMODE], 0, 6);
    c.hold = t->p[P_AHOLD] != 0;
    c.chord = t->p[P_CHRD] != 0;
    c.qnt = t->p[P_QUANT] != Q_OFF;
    c.pending = browse_pending();
    c.tc = c.mute ? T_DIM : T_TRK(k);
    c.bg = T_PANEL;                                     /* (the panel's: the rings' tracks show, as on the pages) */
    if (c.pending) {                                    /* browsing: the sound the list shows, not yet loaded */
        char tag[6];
        src = browse_shown(&kk);
        entry_label(src, kk, tag, c.nm);
        if (!c.nm[0]) str_cpy(c.nm, tag, sizeof c.nm);
    } else {
        sound_name(t, c.nm);
        if (!c.nm[0]) str_cpy(c.nm, ENGINES[eng_idx(t->eng_req)]->name, sizeof c.nm);
        cur_entry(&src, &kk);
    }
    c.en = ENGINES[eng_idx(c.pending ? src_engine(src, kk) : t->eng_req)]->name;
    c.cat = entry_cat(src, kk) % CAT_N;
    for (top = 0; top < 4u; top++)
        c.send[top] = t->p[P_DIST + top];
    str_cpy(c.scl, N_NOTE[(uint32_t)t->p[P_ROOT] % 12u], 4);
    str_cpy(c.scl + str_len(c.scl), " ", 2);
    str_cpy(c.scl + str_len(c.scl), N_SCALE[clamp(t->p[P_SCALE], 0, (int32_t)NELEM(N_SCALE) - 1)], 10);
    vs = stage_voice_sig(t);
    sig = str_hash(str_hash(str_hash(1u + c.mute * 4u + (uint32_t)c.pending * 16u + stage.out * 32u, c.nm), c.en), c.scl) +
          c.lvl * 40503u + c.amode * 7919u + c.hold * 131u + c.chord * 613u + c.qnt * 1291u + ux.gen * 977u + ux.pal * 31u +
          c.cat * 61u;
    for (top = 0; top < 4u; top++)
        sig = (sig ^ (uint32_t)c.send[top]) * 16777619u;
    if (!ui.force && sig == stage.card) {
        if (vs != stage.voices) {                       /* the voices alone: their row */
            stage.voices = vs;
            cv_begin(ST_COL_W - 12, ST_VOICE_H, c.bg);
            stage_voices_at(t, c.bg, -6);
            cv_blit((uint32_t)(ST_COL_X + 6), (uint32_t)(ST_COL_Y + ST_VOICE_Y));
        }
        return;
    }
    stage.card = sig;
    stage.voices = vs;
    for (top = 0; top < ST_COL_H; top += ST_SLICE) {
        uint32_t h = ST_COL_H - top < ST_SLICE ? ST_COL_H - top : ST_SLICE;
        cv_begin(ST_COL_W, h, T_BG);
        cv_oy = -(int32_t)top;
        stage_card_draw(t, &c);
        cv_oy = 0;
        if (stage.out && !top)                          /* the knobs over the top: the rows under them only */
            cv_blit_from(ST_COL_X, ST_COL_Y, CARD_H);
        else
            cv_blit(ST_COL_X, ST_COL_Y + top);
    }
}

/* the knobs: out while one turns (and ST_KNOB_MS after), dropping in over ST_DROP frames (each its cards' bottom rows,
 * more each frame: the card under them does not redraw); gone at once, the card whole again */
static void stage_knobs(void)
{
    int out = stage_knobs_out();
    if (out && !stage.out) {
        stage.out = 1;
        stage.drop = ST_DROP;
    } else if (!out && stage.out) {
        stage.out = 0;
        stage.card = stage.voices = 0;                  /* (the card's top back) */
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
    lcd_fill(0, ST_COL_Y, ST_COL_X, ST_COL_H, T_BG);   /* left and right of the card */
    lcd_fill(ST_COL_X + ST_COL_W, ST_COL_Y, 240u - ST_COL_X - ST_COL_W, ST_COL_H, T_BG);
    lcd_fill(0, ST_COL_Y + ST_COL_H, 240, ST_PANEL_Y - ST_COL_Y - ST_COL_H, T_BG);
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
