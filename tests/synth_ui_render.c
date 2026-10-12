/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Renders of the firmware UI (firmware/src/ui*.c on tests/ui_test.c's stubs), the layout lint, the MONO check
 * and the host draw cost.
 *   ui_render OUTDIR [SLOTDIR]   (run by tests/run_tests.sh; tests/ui_render.py makes the PNGs;
 *                                 SLOTDIR: filmstrips of the rolling digits, MONO and GREEN)
 * Layout lint, over every screen below in every palette, every page of every engine and every value of every
 * column: the ink box of every text and icon drawn (gfx.c GFX_HOOK_TEXT) as it lands on the screen. A finding:
 * a box off the screen, cut by its canvas, partly covered by a later strip or fill, overlapping another box, or
 * ellipsised when it is not free text (labels and values must fit; names and messages may be ellipsised), or
 * spilling out of its cell: touching a rounded rectangle of its canvas (gfx.c cv_rrect: a cell, card, row, button,
 * GFX_HOOK_CELL) without lying inside it, though still on the screen (the FX map's REVERSE).
 * MONO: every pixel of every screen is RGB565 gray (R = B, G = 2 R).
 * Rolling digits: every frame of a header BPM roll and a card roll, up and down, in every palette (lint, MONO).
 * FM6 charts (ui_graph.c graph_fm6, its parts through FM6_CHART_HOOK), every algorithm and every chart drawn: no two
 * operator boxes overlapping or touching, no route, loop, bus or label inside a box or touching one it does not
 * connect, no two nets (one gap's routes that share an operator, the output, the loop, the label) sharing or
 * touching a pixel, everything inside the panel.
 * Output: OUTDIR/ppm/<PALETTE>_<screen>.ppm for MONO GREEN PAPER, OUTDIR/report.txt (findings, ellipsised free
 * text, the draw cost), OUTDIR/text_audit.tsv (MONO, per screen: texts, icons and keycaps with their ink-box px, the
 * ellipsised ones, the texts closer than 2 px to their cell's edge, the words).
 * Exit 1 on a finding or a MONO pixel off gray. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

typedef struct { int16_t x0, y0, x1, y1; uint8_t flags; char s[28]; } rbox_t;
static rbox_t pend[256], scr[1200];
static int16_t cells[64][4];                    /* GFX_HOOK_CELL: the cells and cards of the canvas being drawn */
static uint32_t ncells, nspill;
static FILE *aud;                               /* OUTDIR/text_audit.tsv: MONO, the text of each screen */
static char tight[400];                         /* .. the texts of this screen closer than 2 px to their cell's edge */
static uint32_t ntight;
static uint32_t npend, nscr, nfind, nfree;
static uint64_t px_visited, n_text, blit_px;
static FILE *rep;
static const char *cur_name = "";
static char free_seen[64][40];
static uint32_t nfree_seen;
static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    n_text++;
    if (npend >= 256u) return;
    pend[npend] = (rbox_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint8_t)flags, {0}};
    strncpy(pend[npend].s, s, 27);
    npend++;
}
static void hk_cell(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (ncells < 64u) {
        cells[ncells][0] = (int16_t)x0; cells[ncells][1] = (int16_t)y0;
        cells[ncells][2] = (int16_t)x1; cells[ncells][3] = (int16_t)y1;
        ncells++;
    }
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h);
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hk_text(x0, y0, x1, y1, s, flags)
#define GFX_HOOK_BLIT(x, y, r0) hk_blit(x, y, r0, cv_w, cv_h)
#define UI_REF_SCREEN 1
static uint16_t ref_screen[240 * 240];                  /* the screen as sending every canvas whole leaves it */
static void hk_whole(uint32_t x, uint32_t y, uint32_t r0, uint32_t fill);
#define GFX_HOOK_WHOLE(x, y, r0, fill) hk_whole(x, y, r0, fill)
#define GFX_HOOK_BEGIN() (npend = ncells = 0)
#define GFX_HOOK_CELL(x0, y0, x1, y1) hk_cell(x0, y0, x1, y1)
#define GFX_HOOK_PIXELS(n) (px_visited += (n))
#define UI_FILL_HOOK(x, y, w, h) hk_fill(x, y, w, h)
static void fm6_hook(uint32_t kind, uint32_t a, uint32_t b);
#define FM6_CHART_HOOK(kind, a, b) fm6_hook(kind, a, b)
#define UI_TEST_NO_MAIN 1
#include "synth_ui_harness.c"

/* the FM6 chart lint: each part of a chart (FMH_*) drawn alone onto a canvas of SENT, its pixels kept in fmp_mask
 * (bit: the part), then the canvas as drawn restored with the part on it; at FMH_END the parts are checked */
#define FMP_MAX 48u
#define FMP_SENT 0x0861u
static uint64_t fmp_mask[CV_MAX];
static uint16_t fmp_save[CV_MAX];
static uint8_t fmp_kind[FMP_MAX], fmp_a[FMP_MAX], fmp_b[FMP_MAX];
static uint32_t fmp_n, fmp_charts;
static void finding(const char *what, const rbox_t *b, const rbox_t *o);
static void fmp_find(uint32_t alg, const char *what, uint32_t i, int32_t x, int32_t y, uint32_t j)
{
    static const char *const KIND[] = {"box", "route", "carrier", "bus", "loop", "label"};
    rbox_t b = {(int16_t)x, (int16_t)y, (int16_t)x, (int16_t)y, 0, {0}}, o = b;
    snprintf(b.s, sizeof b.s, "ALG %u %s %u>%u", alg + 1u, KIND[fmp_kind[i]], fmp_a[i] + 1u, fmp_b[i] + 1u);
    snprintf(o.s, sizeof o.s, "%s %u>%u", j < FMP_MAX ? KIND[fmp_kind[j]] : "panel", j < FMP_MAX ? fmp_a[j] + 1u : 0u,
             j < FMP_MAX ? fmp_b[j] + 1u : 0u);
    finding(what, &b, &o);
}
static void fmp_check(uint32_t alg)
{
    int32_t bx0[FMP_MAX], by0[FMP_MAX], bx1[FMP_MAX], by1[FMP_MAX], x, y, dx, dy;
    uint32_t net[FMP_MAX], par[12], i, j, w = cv_w, h = cv_h;
    for (i = 0; i < 12u; i++) par[i] = i;               /* the routes' nets: an operator as source (0..5), as destination (6..11) */
    #define FMP_ROOT(v) ({ uint32_t r_ = (v); while (par[r_] != r_) r_ = par[r_]; r_; })
    for (i = 0; i < fmp_n; i++)
        if (fmp_kind[i] == FMH_ROUTE) par[FMP_ROOT(fmp_a[i])] = FMP_ROOT(6u + fmp_b[i]);
    for (i = 0; i < fmp_n; i++) {
        net[i] = fmp_kind[i] == FMH_ROUTE ? FMP_ROOT(6u + fmp_b[i]) : fmp_kind[i] == FMH_CAR || fmp_kind[i] == FMH_BUS ? 20u
                 : 21u + fmp_kind[i];
        bx0[i] = by0[i] = 9999; bx1[i] = by1[i] = -1;
    }
    #undef FMP_ROOT
    for (y = 0; y < (int32_t)h; y++)                     /* each part's extent */
        for (x = 0; x < (int32_t)w; x++)
            for (i = 0; i < fmp_n; i++)
                if (fmp_mask[y * w + x] >> i & 1u) {
                    if (x < bx0[i]) bx0[i] = x;
                    if (y < by0[i]) by0[i] = y;
                    if (x > bx1[i]) bx1[i] = x;
                    if (y > by1[i]) by1[i] = y;
                }
    for (i = 0; i < fmp_n; i++)                          /* boxes: apart, by a pixel at least */
        for (j = 0; j < i; j++)
            if (fmp_kind[i] == FMH_BOX && fmp_kind[j] == FMH_BOX && bx0[i] <= bx1[j] + 1 && bx0[j] <= bx1[i] + 1 &&
                by0[i] <= by1[j] + 1 && by0[j] <= by1[i] + 1)
                fmp_find(alg, "FM6 boxes overlap", i, bx0[i], by0[i], j);
    for (y = 0; y < (int32_t)h; y++)
        for (x = 0; x < (int32_t)w; x++) {
            uint64_t mk = fmp_mask[y * w + x];
            if (!mk) continue;
            for (i = 0; i < fmp_n; i++) {
                if (!(mk >> i & 1u) || fmp_kind[i] == FMH_BOX) continue;
                if (x < 6 || x > 233 || y < 2 || y > 119)
                    fmp_find(alg, "FM6 part off the panel", i, x, y, FMP_MAX);
                for (j = 0; j < fmp_n; j++) {
                    if (fmp_kind[j] != FMH_BOX) continue;
                    if (x >= bx0[j] && x <= bx1[j] && y >= by0[j] && y <= by1[j])
                        fmp_find(alg, "FM6 part through a box", i, x, y, j);
                    else if (x >= bx0[j] - 1 && x <= bx1[j] + 1 && y >= by0[j] - 1 && y <= by1[j] + 1 &&
                             !((fmp_kind[i] == FMH_ROUTE && (fmp_a[i] == fmp_a[j] || fmp_b[i] == fmp_a[j])) ||
                               (fmp_kind[i] == FMH_CAR && fmp_a[i] == fmp_a[j]) ||
                               (fmp_kind[i] == FMH_FB && (fmp_a[i] == fmp_a[j] || fmp_b[i] == fmp_a[j]))))
                        fmp_find(alg, "FM6 part touches a box", i, x, y, j);
                }
                for (dy = -1; dy <= 1; dy++)              /* another net on this pixel or next to it */
                    for (dx = -1; dx <= 1; dx++) {
                        int32_t nx = x + dx, ny = y + dy;
                        uint64_t mn;
                        if (nx < 0 || ny < 0 || nx >= (int32_t)w || ny >= (int32_t)h) continue;
                        mn = fmp_mask[ny * w + nx];
                        for (j = 0; j < fmp_n; j++)
                            if (mn >> j & 1u && fmp_kind[j] != FMH_BOX && net[j] != net[i])
                                fmp_find(alg, "FM6 nets cross or touch", i, x, y, j);
                    }
            }
        }
    fmp_charts++;
}
static void fm6_hook(uint32_t kind, uint32_t a, uint32_t b)
{
    uint32_t i, n = cv_w * cv_h;
    if (fmp_n) {                                         /* the part just drawn: its pixels */
        for (i = 0; i < n; i++)
            if (cv_px[i] != FMP_SENT) fmp_mask[i] |= (uint64_t)1u << (fmp_n - 1u);
            else cv_px[i] = fmp_save[i];
    } else {
        memset(fmp_mask, 0, n * sizeof fmp_mask[0]);
    }
    if (kind == FMH_END) {
        fmp_check(a);
        fmp_n = 0;
        return;
    }
    if (fmp_n >= FMP_MAX) { fprintf(stderr, "ui_render: an FM6 chart of more than %u parts\n", FMP_MAX); exit(1); }
    memcpy(fmp_save, cv_px, n * sizeof cv_px[0]);
    for (i = 0; i < n; i++) cv_px[i] = FMP_SENT;
    fmp_kind[fmp_n] = (uint8_t)kind; fmp_a[fmp_n] = (uint8_t)a; fmp_b[fmp_n] = (uint8_t)b;
    fmp_n++;
}

static uint8_t aud_on;                                  /* the LCD load audit runs: its frames are not linted */
static void finding(const char *what, const rbox_t *b, const rbox_t *o)
{
    static char seen[600][64];
    static uint32_t nseen;
    char key[64];
    uint32_t i;
    if (aud_on)
        return;
    nfind++;
    snprintf(key, sizeof key, "%.20s|%.14s|%.24s", cur_name, what, b->s);
    for (i = 0; i < nseen && strcmp(seen[i], key); i++) ;
    if (i < nseen) return;                       /* each finding once per screen */
    if (nseen < 600u) strcpy(seen[nseen++], key);
    else return;
    fprintf(rep, "LINT %-22s %-32s '%s' (%d,%d)-(%d,%d)", cur_name, what, b->s, b->x0, b->y0, b->x1, b->y1);
    if (o) fprintf(rep, " / '%s' (%d,%d)-(%d,%d)", o->s, o->x0, o->y0, o->x1, o->y1);
    fputc('\n', rep);
}
/* a rectangle of the screen is drawn again: boxes inside it are gone, boxes it covers in part are hidden */
static void cover(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t i, k = 0;
    for (i = 0; i < nscr; i++) {
        rbox_t *b = &scr[i];
        int in = b->x0 >= x0 && b->x1 <= x1 && b->y0 >= y0 && b->y1 <= y1;
        int touch = b->x0 < x1 && x0 < b->x1 && b->y0 < y1 && y0 < b->y1;
        if (in) continue;
        if (touch && !(b->flags & 48u)) finding("hidden in part by a later draw", b, 0);
        scr[k] = *b;
        if (touch && (b->flags & 48u) && x0 <= b->x0 && b->x1 <= x1) {   /* split across bands / scrolled / dimmed
                                                         * under a popup: drawn on purpose; what is left of it (a sheet
                                                         * drawn in slices covers it in turn) */
            if (y0 <= b->y0) scr[k].y0 = (int16_t)y1;
            else if (b->y1 <= y1) scr[k].y1 = (int16_t)y0;
        }
        k++;
    }
    nscr = k;
}
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { cover((int32_t)x, (int32_t)y, (int32_t)(x + w), (int32_t)(y + h)); }
/* a text across two bands (the menu and the document draw 124 + 85 rows): each band draws its part, the same
 * screen box twice, cut at the band edge; together they are whole (flag 16) */
static int split_pair(rbox_t *b)
{
    uint32_t j;
    if (!(b->flags & 2u)) return 0;
    for (j = 0; j < nscr; j++) {
        rbox_t *o = &scr[j];
        if ((o->flags & 2u) && o->x0 == b->x0 && o->x1 == b->x1 && o->y0 == b->y0 && o->y1 == b->y1 && !strcmp(o->s, b->s)) {
            o->flags = (uint8_t)((o->flags & ~2u) | 16u);
            return 1;
        }
    }
    return 0;
}
/* a text or icon box that touches a cell or card of its canvas (GFX_HOOK_CELL) but spills past its edge; for the
 * audit, a text inside its smallest cell closer than 2 px to an edge */
static void contain(int32_t x, int32_t y)
{
    uint32_t i, c;
    for (i = 0; i < npend; i++) {
        const rbox_t *b = &pend[i];
        int32_t best = 1 << 30, gap = 99;
        for (c = 0; c < ncells && !(b->flags & 4u); c++) {   /* (texts only: an icon's box is its whole cell) */
            const int16_t *r = cells[c];
            int32_t a = (r[2] - r[0]) * (r[3] - r[1]), g;
            if (b->x0 < r[0] || b->x1 > r[2] || b->y0 < r[1] || b->y1 > r[3] || a >= best) continue;
            best = a;
            g = b->x0 - r[0];
            if (r[2] - b->x1 < g) g = r[2] - b->x1;
            if (b->y0 - r[1] < g) g = b->y0 - r[1];
            if (r[3] - b->y1 < g) g = r[3] - b->y1;
            gap = g;
        }
        if (gap < 2 && aud) {
            ntight++;
            if (strlen(tight) + strlen(b->s) + 8u < sizeof tight)
                snprintf(tight + strlen(tight), sizeof tight - strlen(tight), "%s'%s' %dpx", tight[0] ? ", " : "", b->s, (int)gap);
        }
        for (c = 0; c < ncells; c++) {
            const int16_t *r = cells[c];
            int touch = b->x0 < r[2] && r[0] < b->x1 && b->y0 < r[3] && r[1] < b->y1;
            int in = b->x0 >= r[0] && b->x1 <= r[2] && b->y0 >= r[1] && b->y1 <= r[3];
            int around = r[0] >= b->x0 && r[2] <= b->x1 && r[1] >= b->y0 && r[3] <= b->y1;   /* (a mark inside the box) */
            if (touch && !in && !around) {
                rbox_t t = *b, o = {(int16_t)(r[0] + x), (int16_t)(r[1] + y), (int16_t)(r[2] + x), (int16_t)(r[3] + y), 0, "cell"};
                t.x0 += (int16_t)x; t.x1 += (int16_t)x; t.y0 += (int16_t)y; t.y1 += (int16_t)y;
                nspill++;
                finding("spills out of its cell", &t, &o);
                break;
            }
        }
    }
}
static void hk_whole(uint32_t x, uint32_t y, uint32_t r0, uint32_t fill)
{
    uint32_t i, j, w = cv_w, h = cv_h;
    if (fill) {                                         /* a fill: r0 its w | h << 16, fill its colour | 1 << 16 */
        uint16_t c = (uint16_t)fill;
        w = r0 & 0xFFFFu; h = r0 >> 16;
        for (j = 0; j < h && y + j < 240u; j++)
            for (i = 0; i < w && x + i < 240u; i++)
                ref_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
        return;
    }
    for (j = r0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            ref_screen[(y + j) * 240u + x + i] = cv_px[j * w + i];
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h)
{
    uint32_t i, k = 0;
    contain((int32_t)x, (int32_t)y);
    ncells = 0;
    blit_px += (uint64_t)w * (h > r0 ? h - r0 : 0u);
    for (i = 0; i < npend; i++) {
        rbox_t b = pend[i];
        if (b.y1 <= (int32_t)r0) continue;           /* rows not blitted */
        if (b.y0 < (int32_t)r0) b.flags |= 2u;
        b.x0 += (int16_t)x; b.x1 += (int16_t)x; b.y0 += (int16_t)y; b.y1 += (int16_t)y;
        if (!split_pair(&b)) pend[k++] = b;
    }
    cover((int32_t)x, (int32_t)(y + r0), (int32_t)(x + w), (int32_t)(y + h));
    for (i = 0; i < k && nscr < 1200u; i++)
        scr[nscr++] = pend[i];
    npend = 0;
}

static void lint(void)
{
    uint32_t i, j;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        if (b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) finding("off the screen", b, 0);
        if ((b->flags & 2u) && !(b->flags & 32u)) finding("cut by its canvas", b, 0);
        if ((b->flags & 1u) && !(b->flags & 8u)) finding("ellipsised (not free text)", b, 0);
        if ((b->flags & 9u) == 9u) {
            for (j = 0; j < nfree_seen && strcmp(free_seen[j], b->s); j++) ;
            if (j == nfree_seen && nfree_seen < 64u) {
                snprintf(free_seen[nfree_seen++], 40, "%s", b->s);
                fprintf(rep, "free   %-22s ellipsised '%s'\n", cur_name, b->s);
            }
            nfree++;
        }
        for (j = i + 1u; j < nscr; j++) {
            const rbox_t *o = &scr[j];
            if (b->x0 < o->x1 && o->x0 < b->x1 && b->y0 < o->y1 && o->y0 < b->y1) finding("overlaps", b, o);
        }
    }
}

/* the audit of one screen (MONO): its texts (count, ink-box px), icons, keycaps, the ellipsised and tight texts,
 * and the words themselves */
static FILE *audf;
static void audit_scene(const char *name)
{
    uint32_t i, nt = 0, ni = 0, nk = 0, ne = 0, at = 0, ai = 0, ak = 0;
    char words[1200] = "";
    if (!audf) return;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        uint32_t a = (uint32_t)((b->x1 - b->x0) * (b->y1 - b->y0));
        if ((b->flags & 4u) && !strcmp(b->s, "icon")) { ni++; ai += a; continue; }
        if (b->flags & 4u) { nk++; ak += a; continue; }
        nt++; at += a;
        if (b->flags & 1u) ne++;
        if (strlen(words) + strlen(b->s) + 4u < sizeof words)
            snprintf(words + strlen(words), sizeof words - strlen(words), "%s%s", words[0] ? " | " : "", b->s);
    }
    fprintf(audf, "%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\n", name, nt, at, ni, ai, nk, ak, ne, ntight, words, tight);
}
static uint32_t mono_bad;
static void mono_check(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        if ((c >> 11) != (c & 31u) || ((c >> 5) & 63u) != (c >> 11) * 2u) {
            if (!mono_bad) fprintf(rep, "MONO %s: pixel %u,%u = %04x off gray\n", cur_name, i % 240u, i / 240u, c);
            mono_bad++;
        }
    }
}
static void write_ppm(const char *dir, const char *pal, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/ppm/%s_%s.ppm", dir, pal, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}


static void screen_clear(void)
{
    memset(host_screen, 0, sizeof host_screen); memset(ref_screen, 0, sizeof ref_screen);
    memset(df_key, 0, sizeof df_key); nscr = npend = 0;
}
static void draw_scene(const char *dir, uint32_t palette, const char *name)
{
    cur_name = name; screen_clear(); ui.force = 1; ui_draw(); lint();
    if (palette == UI_GRAY_INDEX) { mono_check(); audit_scene(name); }
    write_ppm(dir, UI_PALETTES[palette].name, name);
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/ui_synth";
    char path[256], name[96];
    snprintf(path, sizeof path, "%s/report.txt", dir); rep = fopen(path, "w");
    snprintf(path, sizeof path, "%s/text_audit.tsv", dir); audf = fopen(path, "w");
    if (!rep || !audf) return 2;
    uint32_t scenes = 0;
    for (uint32_t palette = 0; palette < UI_PAL_ENTRIES; palette++) {
        for (uint32_t e = 0; e < NENGINES; e++) {
            if (!eng_ok(e)) continue;
            ui_power_on(); settings.palette = palette; palette_set(palette);
            engine_load_of(TSEL, e, 0); TSEL->engine = TSEL->eng_req;
            for (uint32_t i = 0; i < NPAGES; i++) {
                if (!page_visible(i)) continue;
                ui.home = 0; ui.page = i; page_entered();
                snprintf(name, sizeof name, "e%u_page%02u", e, i);
                draw_scene(dir, palette, name); scenes++;
            }
            ui.home = 1;
            snprintf(name, sizeof name, "e%u_stage", e); draw_scene(dir, palette, name); scenes++;
            ui.menu = 1; snprintf(name, sizeof name, "e%u_menu", e); draw_scene(dir, palette, name); scenes++;
            ui.menu = 0;
            if (e == ENGI_FM6) {
                ui.home = 0;
                for (uint32_t i = 0; i < NPAGES; i++) if (str_eq(PAGES[i].title, "ALGO")) ui.page = i;
                for (uint32_t a = 0; a < 32; a++) {
                    fm6_patch[0][FP_ALG] = a;
                    snprintf(name, sizeof name, "fm6_algorithm%02u", a+1); draw_scene(dir, palette, name); scenes++;
                }
            }
        }
    }
    ui_power_on();
    for (uint32_t p = 0; p < UI_PAL_ENTRIES; p++) {
        settings.palette = p; palette_set(p);
        for (uint32_t a = 0; a < 7; a++) for (uint32_t scl = 0; scl < SCALE_TOTAL; scl++) {
            trk[0].p[P_AMODE] = a; trk[0].p[P_SCALE] = scl; trk[0].p[P_ROOT] = 11;
            trk[0].p[P_CHRD] = trk[0].p[P_AHOLD] = trk[0].p[P_QUANT] = 1;
            snprintf(name, sizeof name, "stage_scale%02u_arp%u", scl, a); cur_name = name;
            screen_clear(); ui.force = 1; ui_draw(); lint(); scenes++;
        }
    }
    fprintf(rep, "%u scenes, %u findings, %u MONO pixels off gray\n", scenes, nfind, mono_bad);
    printf("SYNTH UI: %u scenes, %u findings, %u MONO pixels off gray\n", scenes, nfind, mono_bad);
    fclose(rep); fclose(audf); return !!(nfind || mono_bad);
}
