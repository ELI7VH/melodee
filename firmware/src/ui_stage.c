/* SPDX-License-Identifier: GPL-3.0-only */
/* HOME follows the approved Stage note-overlay design: the four knob cards,
 * a smooth waveform with the chord and notes over it, and the selected sound
 * lane. A single instrument leaves room for a taller scope and one quiet row
 * of playing status; FX retain their existing pages and held layer. */
#define ST_PANEL_Y Y_GRAPH
#define ST_PANEL_H 124
#define ST_SOUND_Y (ST_PANEL_Y + ST_PANEL_H + 4)
#define ST_STATUS_Y (ST_SOUND_Y + 21)
#define ST_KNOB_MS 1500u
_Static_assert(240u * ST_PANEL_H <= CV_MAX, "HOME scope must fit the drawing canvas");

static struct {
    uint32_t knob_ms;                                   /* last HOME knob turn, for the response picture */
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
/* The four cards stay visible: labels and values use the established Stage typography. */
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
 * what is on the canvas (cv_over): the root and quality on the first line, notes on the second.
 * Held: the chord in text, the notes in the track's colour; let go: both faded into the panel. 0 none, 1 let go, 2 held */
static int stage_notes(int32_t x1)
{
    uint32_t bits[4], i, n = 0, pcs = 0, root = 0, bass, held = 0, k = song.sel % NTRK;
    uint8_t note[8];
    char b[48];
    const char *q;
    uint16_t c, cn;
    int32_t x;
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
        x = cv_text(10, 1, &AF_L, N_NOTE[root], c);
        x = cv_text(x + 1, 15, &AF_M, q, c);
        if (root != bass) {
            x = cv_text(x + 3, 1, &AF_L, "/", c);
            cv_text(x, 1, &AF_L, N_NOTE[bass], c);
        }
        notes_fit(b, note, n, &AF_S, x1 - 10);
        cv_text(10, 39, &AF_S, b, cn);
    } else {
        const aafont_t *f = &AF_L;
        if (notes_fit(b, note, n, f, x1 - 10) != n) {
            f = &AF_S;
            notes_fit(b, note, n, f, x1 - 10);
        }
        cv_text(10, f == &AF_L ? 1 : 15, f, b, c);
    }
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

/* ------------------------------------------------------- sound / status --- */
static void stage_sound(void)
{
    char nm[16], tag[6];
    uint32_t src, kk;
    int pending = browse_pending();
    uint16_t tc = TSEL->p[P_MUTE] ? T_DIM : T_THEME;
    if (pending) {
        src = browse_shown(&kk); entry_label(src, kk, tag, nm);
        if (!nm[0]) str_cpy(nm, tag, sizeof nm);
    } else {
        trk_short_name(0, nm);
        if (TSEL->eng_req == ENGI_PROPHET) p5_short_name(TSEL, nm, sizeof nm);
        cur_entry(&src, &kk);
    }
    cv_begin(240, 19, T_BG);
    cv_rrect(4, 0, 232, 19, 5, tc, T_BG);
    cv_rrect(5, 1, 230, 17, 4, T_RAISE, tc);
    cv_rect(7, 4, 2, 11, tc);
    cv_free_text(14, 3, &AF_S, nm, pending ? T_ACCENT : T_TEXT, T_RAISE, 166);
    cv_text_r(227, 4, &AF_X, CAT_NAME[entry_cat(src, kk) % CAT_N], tc, T_RAISE);
    cv_blit(0, ST_SOUND_Y);
}
static void stage_status(void)
{
    static const char *const AM[7] = {"ARP OFF", "ARP UP", "ARP DOWN", "ARP UP/DN", "ARP RANDOM", "ARP ORDER", "REPEAT"};
    uint32_t mode = (uint32_t)clamp(TSEL->p[P_AMODE], 0, 6);
    int32_t x;
    char scale[32];
    cv_begin(240, 19, T_BG);
    x = cv_text(8, 3, &AF_X, AM[mode], mode ? T_THEME : T_DIM);
    if (TSEL->p[P_AHOLD]) x = cv_text(x + 8, 3, &AF_X, "HOLD", T_THEME);
    if (TSEL->p[P_CHRD]) x = cv_text(x + 8, 3, &AF_X, "CHORD", T_THEME);
    if (TSEL->p[P_QUANT] != Q_OFF) {
        str_cpy(scale, N_NOTE[(uint32_t)TSEL->p[P_ROOT] % 12u], 4);
        str_cpy(scale + str_len(scale), " ", 2);
        str_cpy(scale + str_len(scale), N_SCALE[clamp(TSEL->p[P_SCALE], 0, SCALE_TOTAL - 1u)], 24);
        char fitted[32];
        text_fit(fitted, sizeof fitted, scale, &AF_X, 232 - x - 8);
        cv_free_text(232 - text_w(&AF_X, fitted), 3, &AF_X, scale, T_MID, T_BG, 232 - x - 8);
    }
    cv_blit(0, ST_STATUS_Y);
}

static void stage_frame(void)
{
    lcd_fill(0, Y_LABEL, 4, CARD_H, T_BG);
    lcd_fill(236, Y_LABEL, 4, CARD_H, T_BG);
    lcd_fill(0, Y_LABEL + CARD_H, 240, ST_PANEL_Y - Y_LABEL - CARD_H, T_BG);
    lcd_fill(0, ST_PANEL_Y + ST_PANEL_H, 240, ST_SOUND_Y - ST_PANEL_Y - ST_PANEL_H, T_BG);
    lcd_fill(0, ST_SOUND_Y + 19, 240, 2, T_BG);
    lcd_fill(0, ST_STATUS_Y + 19, 240, 240 - ST_STATUS_Y - 19, T_BG);
}
/* The scope is drawn last, as in the approved UI, while the LCD sends it. */
static void stage_draw(void)
{
    stage_columns();
    if (!ui.force && (ui.frame & 1u)) return;
    stage_sound();
    stage_status();
    stage_panel();
}
