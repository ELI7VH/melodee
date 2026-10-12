/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee UI input: LEDs, knobs and buttons, panel setup. */
#include "ui_name.c"                                    /* NAME: naming user presets */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
static int led_get(const uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((nl[q >> 3] >> (q & 7u)) & 1u);
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_GLO};   /* (SEQ and TRK: no pages in the synth build) */

static uint32_t cur_fam(void)                       /* (an engine's own envelope / LFO page: ENV / LFO, ui.c native_page) */
{
    return ui.home ? FAM_HOME : native_page(ui.page, FAM_ENV) ? FAM_ENV : native_page(ui.page, FAM_LFO) ? FAM_LFO :
           cur_page()->fam;
}

static int layer_set_open(void);                       /* (ui_layer.c) */
/* OCT- / OCT+ shift the octave on Stage (HOME) only; everywhere else, and over Stage's menu, dialogs and popups,
 * they are Esc / Enter (docs/design/README.md, Navigation) */
static int oct_nav(void)
{
    return designer_on() || !ui.home || ui.menu || ui.confirm || name_on() || layer_set_open() || pop.on || smap.on;
}
static int oct_enter_ok(void)                           /* OCT+ would do something here */
{
    return list_on() || slot_kind() || (!ui.home && cur_page()->graph == GR_SCALE_PICKER) || page_sheet() || pop.on || smap.on || (!ui.home && cur_page()->graph == GR_BROWSE);
}

/* the OCT LEDs, bit 0 OCT-, bit 1 OCT+. In the dialogs, the menu and on action pages OCT- (back) is
 * lit and OCT+ blinks while it would do something; off Stage OCT- lit, OCT+ lit where it enters; on Stage
 * they show the octave shift */
static uint32_t oct_leds(void)
{
    if (designer_on() && !name_on() && !ui.confirm) return 3u;
    uint32_t blink = ((fm1_ms / 250u) & 1u) == 0u;
    if (name_on() && !ui.confirm && !ui.menu)          /* NAME: OCT- cancels, OCT+ (blinking) goes on */
        return 1u | (blink ? 2u : 0u);
    if (layer_set_open())                               /* a SET layer: OCT- puts back (UNDO), OCT+ nothing */
        return 1u;
    if (ui.confirm || ui.menu || act_cols())
        return 1u | (blink && (ui.confirm || (ui.menu ? ui.menu == 1u : act_ready())) ? 2u : 0u);
    if (oct_nav())
        return 1u | (oct_enter_ok() ? 2u : 0u);
    return (song.octave < 0 ? 1u : 0u) | (song.octave > 0 ? 2u : 0u);
}

/* an ARP playing on any part flashes the ARP button on the beat, the bar's first beat longer: 1 lit, 0 dark,
 * 2 no ARP playing */
static uint32_t arp_led(void)
{
    uint32_t k, on = 0, b = seq_beat_samples();
    for (k = 0; k < NPART; k++)
        on |= trk[k].p[P_AMODE] && trk[k].nheld;
    return !on ? 2u : beat_pos < (beat_n ? b / 6u : b / 2u);
}

/* key k of track t as the keys play now (LIGHTS): bright while it sounds (pressed, or its note held on the track by
 * a key or MIDI), dim when it plays something (a kit's or the slices' own map: every key; QNT OFF: the notes in the
 * scale; the layouts: the keys not silent), else dark */
enum { KL_OFF, KL_DIM, KL_ON };
static uint32_t play_key_led(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t n = kb_map(t, k), ti = trk_index(t);
    if (((fm1_in.notes >> k) & 1u) || (n < 128u && ((live_held[ti][n >> 5] >> (n & 31u)) & 1u)))
        return KL_ON;
    if (n >= 128u)
        return KL_OFF;
    if (e->keys && e->keys(t, k) >= 0)
        return KL_DIM;
    if (t->p[P_QUANT] == Q_OFF)
        return (scale_mask(t) >> ((n + 120u - (uint32_t)t->p[P_ROOT]) % 12u)) & 1u ? KL_DIM : KL_OFF;
    return KL_DIM;
}

static const uint8_t LIGHTS_MASK[LIGHTS_N] = {0, 7, 3, 1, 0};   /* the dim keys' duty: -, 1/8, 1/4, 1/2, all */
#define BTN_DIM_MASK 3u                                         /* idle buttons: 1/4 duty each scan */
/* The lights' grammar, keys and buttons alike: dark nothing there, dim something there, bright happening now, breathing
 * waiting. Breathing: the third dim plane,
 * its level swept full .. 1/8 .. dark .. 1/8 .. full over a second (BREATH, 125 ms a step; 0xFF: dark) */
static const uint8_t BREATH[8] = {0, 1, 3, 7, 0xFF, 7, 3, 1};
static uint32_t breath_on(void) { return BREATH[(fm1_ms / 125u) & 7u] != 0xFFu; }

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0}, nw[FM1_NCOL] = {0};
    uint32_t k, c, play, lights = settings_lights % LIGHTS_N;
    uint32_t fam = cur_fam();
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    if (!ui.layer || FAM_BTN[fam] != layer_btn())
        led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if ((k = arp_led()) != 2u && (!ui.layer || layer_btn() != B_ARP))
        led_put(nl, panel.btn[B_ARP], FAM_BTN[fam] == B_ARP ? !k : (int)k);   /* (on ARP's page: dark flashes) */
    if (ui.layer)                                       /* the layer's button blinks while its map is up */
        led_put(nl, panel.btn[layer_btn()], ((fm1_ms / 250u) & 1u) == 0u);
    led_put(nl, panel.btn[B_PLAY], TSEL->p[P_AHOLD] != 0);   /* PLAY: the ARP's HOLD (latch) */
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)(k >> 1));
    play = !(name_on() && !ui.menu) && !ui.layer;
    c = name_on() && !ui.menu ? name_leds() : ui.layer ? layer_leds() :
        fm1_in.notes & ~kb_layer;                       /* NAME's keys, the map, the keys held */
#if MELODEE_SLICE
    if (!ui.layer && !ui.menu && !name_on() && slice_page_on())
        c |= slice_leds();                              /* SLICES: and the keys of the selected slice */
#endif
    for (k = 0; k < 27u; k++) {
        uint32_t lv = play ? play_key_led(TSEL, k) : KL_OFF;   /* playing: the layout, MIDI's notes too (LIGHTS) */
        led_put(nl, 14u + k, (int)(((c >> k) & 1u) || lv == KL_ON));
        led_put(nd, 14u + k, lv == KL_DIM && lights != LIGHTS_OFF);
    }
    for (k = 0; lights != LIGHTS_OFF && k < NB; k++)   /* the buttons glow when idle (one breathing: not under it) */
        led_put(nb, panel.btn[k], !led_get(nw, panel.btn[k]));
    fm1_led_dim_mask[0] = LIGHTS_MASK[lights];
    fm1_led_dim_mask[1] = BTN_DIM_MASK;
    fm1_led_dim_mask[2] = BREATH[(fm1_ms / 125u) & 7u];
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[0][c] = (uint8_t)(nd[c] | nl[c]);   /* first: a key going dim <-> bright never goes dark */
        fm1_led_dim[1][c] = nb[c];
        fm1_led_dim[2][c] = breath_on() ? nw[c] : 0u;
        fm1_led[c] = nl[c];
    }
}

/* ---------------------------------------------------------- input --- */
/* Knob acceleration, after Felucca 1.4 (#52), with fm1-x0x's curve (tuned on the hardware): a slow turn is one step a
 * detent, and a few clicks are one step each however quick. Only a spin speeds up: from the fourth read of a turn,
 * each under ACC_GAP ms a detent after the one before in the same direction, 3 steps a detent under 45 ms, 5 under
 * 25 ms and 8 under 12 ms, so a quick half turn sweeps 0..127. cap: the most for the value (accel, list_accel); a cap
 * over 8 (the tempo, the long lists) doubles them. MENU > KNOB ACCEL OFF (PREF_ACCEL_OFF) keeps every detent one step.
 * The main loop reads the knobs many times a frame (main.c), so a read holds one detent as a rule; a read of several is
 * timed per detent and counts once (a few detents read at once are not a spin). A pause or a reversal starts a new
 * turn, and the sign is always the detents'. *fast (if asked): this detent followed the previous one within ACC_GAP ms,
 * in the same direction (the browser waits for such a turn to rest).
 * ui.enc_t[role]: bits 0..23 the ms of its last read, bit 24 its direction (+1), 25..31 the reads of its turn */
#define ACC_GAP 60u
#define ACC_SPIN 4u
static int32_t accel_by(uint32_t role, int32_t s, uint32_t cap, uint32_t *fast)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role % NE], up = s > 0, run = st >> 25, a, dt, m = 1;
    if (fast)
        *fast = 0;
    if ((PREF_BITS & PREF_ACCEL_OFF) || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    dt = ((now - st) & 0xFFFFFFu) / a;                  /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || dt >= ACC_GAP) {
        run = 1;                                        /* a new turn, or reversed */
    } else {
        if (fast)
            *fast = 1;
        run++;
        if (run >= ACC_SPIN)
            m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
        if (cap > 8u && m > 1u)
            m *= 2u;
        m = m > cap ? cap : m;
    }
    ui.enc_t[role % NE] = now | up << 24 | (run > 127u ? 127u : run) << 25;
    return s * (int32_t)m;
}
/* a value of range steps: none up to 24 (and a list of names passes 0), x3 at most under 100, x8, x16 over 150 */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    return range <= 24 ? s : accel_by(role, s, range < 100 ? 3u : range > 150 ? 16u : 8u, 0);
}
static int32_t desc_range(const param_desc_t *d)     /* (a list of names: no acceleration) */
{
    return d->fmt == F_ENUM ? 0 : d->max - d->min;
}
/* a list of total entries (the sounds, the scales, the user slots): up to x16 over 256 entries */
static int32_t list_accel(uint32_t role, int32_t s, uint32_t total, uint32_t *fast)
{
    return accel_by(role, s, total > 256u ? 16u : total > 64u ? 8u : total > 16u ? 4u : 1u, fast);
}

/* Autosave: untouched for AUTOSAVE_MS (no key, button, knob or MIDI) and nothing sounding, the last state into flash when
 * it changed (persist.c state_autosave); once per rest */
#define AUTOSAVE_MS 3000u
static int sounding(void)                               /* a voice of any part, an ARP note, a latched chord */
{
    uint32_t k, i;
    for (k = 0; k < NPART; k++) {
        if (trk[k].arp_note || trk[k].nheld)
            return 1;
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active)
                return 1;
    }
    return 0;
}
static void autosave_poll(uint32_t active)
{
    static uint32_t t0, rx;
    static uint8_t done;
#if MELODEE_UART
    uint32_t r = usb.rx_pkts + um.bytes;
#else
    uint32_t r = usb.rx_pkts;
#endif
    if (active || r != rx || transport_busy() || ui.menu || ui.confirm || name_on() || ui.layer || momentary.active ||
        browse_pending() || designer_on() || sounding()) {
        t0 = fm1_ms;
        rx = r;
        done = 0;
        return;
    }
    if (!done && fm1_ms - t0 >= AUTOSAVE_MS) {
        done = (uint8_t)state_autosave();
        if (!done) t0 = fm1_ms;                 /* retry a failed write after another idle interval */
    }
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int32_t v;
    if (pg->graph == GR_SCALE_PICKER) {
        scale_picker_edit(slot, slot == 1u ? list_accel(EN_K1 + slot, steps, scale_picker_count(), 0) : steps);
        return;
    }
    if (scale_settings_page(pg) && slot == 1u) {
        scale_picker_mark(steps > 0);
        return;
    }
    if (pg->graph == GR_BROWSE) {   /* the browser's two columns: KNOB 1 LIST (ALL FAV RECENT, the categories), 2 the
                                     * sounds in it (as PRESETS); 3 FAV, 4 the next / previous engine */
        if (slot == 1u) {
            browse_turn(EN_K2, steps);
            return;
        }
        browse_commit();                                  /* (the others act on the sound shown: load it first) */
        if (slot == 3u) {
            select_engine(eng_step(TSEL->eng_req, steps));
        } else if (slot == 2u) {
            preset_mark(steps > 0);
        } else if (slot == 0u) {                          /* ALL FAV RECENT, the categories (no wrap) */
            uint32_t m = (uint32_t)clamp((int32_t)list_mode() + (steps > 0 ? 1 : -1), 0, (int32_t)LM_N - 1);
            if (m != list_mode()) {
                list_set(m);
                ui.force = 1;
                settings_save();
            }
        }
        return;
    }
#if MELODEE_SLICE
    if (pg->graph == GR_SLICES && slot < 2u) {           /* SLICES: KNOB 1 the marker, 2 moves it (ui_slice.c) */
        if (slice_page_ok())                              /* (the engine changed before ui_draw left the page) */
            slice_knob(slot, steps);
        return;
    }
#endif
    if ((act_cols() >> slot) & 1u) {                      /* an action's knob picks it (right) or drops it (left); */
        ui.act = steps > 0 ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;   /* (OCT+ does it: act_do) */
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 the slot */
        if (slot == 0u)
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + list_accel(EN_K1, steps, user_limit(), 0), 0, user_limit() - 1);
        return;
    }
    if (pg->graph == GR_MOD && slot == 1u) {             /* MOD: KNOB 2 the route; 1 its SRC, 3 DST, 4 AMT */
        mod_ui_slot = (uint8_t)clamp((int32_t)mod_ui_slot + (steps > 0 ? 1 : -1), 0, 3);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = enum_step(d, *vp, clamp(*vp + accel(EN_K1 + slot, steps, desc_range(d)), d->min, d->max));
    *vp = (int16_t)v;
    if(pg->scope==SC_P5){p5_edit_value(TSEL,pg->id[slot],p5_panel_raw(TSEL,pg->id[slot],v));return;}
    if(pg->scope==SC_P5STORE){p5_store_slot=(int16_t)v;return;}
    if (pg->scope == SC_CZ1) {                           /* (a copy of the tone's value: written back there) */
        uint8_t raw[CZ_BYTES];
        uint32_t tr = song.sel % NTRK;
        if (cz_ed_put(tr, pg->id[slot], (uint32_t)v, raw)) {
            if (!momentary.active) cz_compare_take(tr);
            memcpy(cz_patch[tr].raw, raw, 128u);
        }
        return;
    }
    if (pg->scope == SC_FM6 || pg->scope == SC_FMOP) {   /* (a copy of FM6's value: written back there) */
        fm6_page_put(pg, slot, v);
        return;
    }
    if (pg->scope == SC_GLOBAL && pg->id[slot] == G_A4) {
        settings_save();
        return;
    }
    if (pg->scope == SC_TRACK && scale_shared((uint32_t)(vp - TSEL->p)))
        scale_share(TSEL);
}

/* CZ TOOLS 1 > 2 / 2 > 1: line src's wave, window, key follow, level, velocity and envelopes over the other
 * line (Casio's 57-byte line block); MOD stays line 1's. Undoable as a sound load */
static void cz_line_copy(track_t *t, uint32_t src)
{
    uint8_t *b = cz_patch[trk_index(t)].raw;
    uint32_t s = src ? 71u : 14u, d = src ? 14u : 71u, mod = b[d + 1u] & 0x38u;
    cz_compare_take(trk_index(t));
    load_begin(t, UNDO_SOUND);
    fm1_irq_off();
    memcpy(b + d, b + s, 57u);
    b[d + 1u] = (uint8_t)((b[d + 1u] & ~0x38u) | mod);
    fm1_irq_on();
    load_end(t);
}

/* CZ TOOLS COMP: the tone before the first edit and the edited one change places; 0 = nothing edited */
static int cz_compare_swap(uint32_t tr)
{
    cz_patch_t x = cz_patch[tr % NTRK];
    if (cz_compare_tr != tr % NTRK + 1u)
        return 0;
    fm1_irq_off();
    cz_patch[tr % NTRK] = cz_compare;
    fm1_irq_on();
    cz_compare = x;
    return 1;
}

/* OCT+ on an action page: the picked action. A load stays picked (browse and load again); the others
 * are dropped once done. Flash writes only while stopped; over the user's data: the dialog */
static void act_do(void)
{
    uint32_t c = act_col(), id;
    if (!c--)
        return;
    if(cur_page()->scope==SC_P5STORE){
        uint32_t slot=(uint32_t)p5_store_slot-1u;
        if(c==1u){if(transport_busy())ui_message("STOP TO SAVE");else if(native_used(ENGI_PROPHET,slot))confirm_open(CF_OVR_USER,slot);else name_open(NK_USER_SAVE,slot);}
        else if(c==2u)p5_send(song.sel);
        else {load_begin(TSEL,UNDO_SOUND);panic_req|=1u<<song.sel;fm1_irq_off();p5_patch_init(p5_patch_of(TSEL));p5_track_accept(TSEL);fm1_irq_on();load_end(TSEL);ui_message("PROPHET INIT");}
        ui.act=0;return;
    }
    if (cur_page()->graph == GR_CZTOOLS) {               /* CZ-1: NAME, copy a line over the other, COMPARE */
        uint32_t tr = song.sel % NTRK;
        if (c == 0u) {
            name_open(NK_CZ_NAME, tr);
        } else if (c < 3u) {
            cz_line_copy(TSEL, c == 1u ? 0u : 1u);
            ui_message(c == 1u ? "LINE 1 > 2" : "LINE 2 > 1");
        } else {
            ui_message(cz_compare_swap(tr) ? "COMPARE / EDIT" : "NO EDITS");
        }
        ui.act = 0;
        ui.force = 1;
        return;
    }
#if MELODEE_SLICE
    if (cur_page()->graph == GR_SLICES) {                 /* SPLIT / JOIN: stays picked (split again, join again) */
        if (slice_page_ok())
            slice_act(c);
        return;
    }
#endif
    if (cur_page()->graph == GR_FMSTORE) {                /* FM6: 1 STORE (into fm6_bslot), 2 SEND, 3 INIT */
        if (c == 1u) {
            if (transport_busy()) ui_message("STOP TO SAVE");
            else if (native_used(ENGI_FM6,fm6_bslot) || native_old(ENGI_FM6,fm6_bslot)) confirm_open(CF_OVR_USER, fm6_bslot);
            else fm6_store(fm6_bslot);
        }
        else if (c == 2u)
            fm6_send();
        else if (c == 3u)
            fm6_init_voice();
        ui.act = 0;
        return;
    }
    if (cur_page()->graph == GR_USER) {                   /* 1 LOAD, 2 ERASE, 3 SAVE (the NAME screen first) */
        if (c > 1u)
            ui.act = 0;
        ui.uslot %= user_limit();
        if (c == 2u && (user_used(ui.uslot) || user_old(ui.uslot)) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* ERASE: the dialog first */
        else if (c != 3u)
            user_ui_named(c - 1u, ui.uslot, 0);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (user_used(ui.uslot) || user_old(ui.uslot))
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    id = cur_page()->id[c & 3u];
    ui.act = 0;
    if (id == G_INITSND) {
        sound_init_of(TSEL, TSEL->eng_req);               /* the engine's INIT */
        ui_message("SOUND INIT");
        ui.force = 1;
    }
}

/* OCT- / OCT+ as Esc / Enter (oct_nav): on release, and only a press that began there; both down together
 * (UPDATE MODE, main.c) is no tap. Bit 0 OCT-, bit 1 OCT+ */
static uint8_t oct_eat;                                 /* OCT taps not to come (held for a sheet, used in a combo) */
static uint8_t oct_deferred;                            /* OCT+ pressed on Stage (its sheet when held): the octave on
                                                         * release */
static uint32_t oct_taps(uint32_t pressed, int here)
{
    static uint8_t down, chord;
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP];
    uint32_t now = ((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1, tap;
    if (here)
        down |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    if (now == 3u)
        chord = 1;
    tap = down & ~now;
    down &= (uint8_t)now;
    if (chord) {
        tap = 0;
        chord = now != 0u;
    }
    tap &= ~(uint32_t)oct_eat;
    oct_eat &= (uint8_t)now;
    return tap;
}

/* HOME / SAVE: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* the quick layers: ui_layer.c (included after this file) */
static void layer_masks(void);
static void layer_lock_input(uint32_t pressed);
static void fx_assign(int32_t s);
static void fx_default(void);
static uint32_t fx_key_held(void);
static void layer_arm(uint32_t pressed, uint32_t now);
static uint32_t layer_held(void);
static uint32_t layer_gesture(uint32_t now, uint32_t combo);
static void layer_show(void);
static void layer_tap(uint32_t l);
static void layer_keys(uint32_t keys);
static void layer_knob(uint32_t k, int32_t s);
static int layer_set_open(void);
static uint32_t layer_oct(uint32_t pressed, uint32_t oct);
static int layer_allowed(void);
static uint32_t ly_bit(uint32_t l);

/* a page button let go (they act on release; a layer's own button: layer_gesture) */
static void page_tap(uint32_t b)
{
    uint32_t f;
    if (b == B_EDIT && smap.on) {                       /* the map open: EDIT closes it */
        smap_close();
        return;
    }
    if (b == B_EDIT && sec_on()) {                      /* an EDIT page of an engine in sections: its map */
        smap_open();
        return;
    }
    if (b == B_GLO) {
        open_global();                                  /* GLOBAL -> SYSTEM -> GLOBAL */
        return;
    }
    if (b == B_SEQ)
        return;                                         /* (no sequencer in the synth build) */
    if (b == B_EDIT && !ui.home && cur_page()->graph == GR_USER) {
        name_rename();                                  /* SAVE > USER: EDIT renames the slot */
        return;
    }
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
        if (FAM_BTN[f] == b) {
            open_family(f);
            return;
        }
}


/* KNOB k turned s on the page shown (the sound designer's pages too): what it edits there, its card hot */
static void page_knob(uint32_t k, int32_t s)
{
    const page_t *pg = cur_page();
    int16_t *hv;
    if (ui.home || page_desc(pg, k, &hv) ||
        (pg->graph == GR_USER && k == 0u) || (pg->graph == GR_MOD && k == 1u)
        || pg->graph == GR_SCALE_PICKER || (scale_settings_page(pg) && k == 1u)
        || (pg->graph == GR_SLICES && k < 2u)) {       /* (not an empty column) */
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
    }
    if (list_on()) {                                    /* a list page: KNOB 2 the row, KNOB 1 its value (ui_list.c) */
        list_knob(k, s);
        return;
    }
    if (slot_kind()) {                                  /* a slot page: KNOB 1 / 2 the slot (ui_slots.c) */
        if (k < 2u)
            edit_param(0, s);
        return;
    }
    momentary_take(k);
    if (ui.home) {                                      /* Stage: the selected track's engine's four, as its pages; they
                                                         * drop in */
        stage_knob_touch();
        page_over = stage_page();
        edit_param(k, s);
        page_over = 0;
    } else {
        edit_param(k, s);
        pick_touch(k);                                  /* a name from a long list: its picker (ui_popup.c) */
    }
}

static int bulk_context(void)
{
    return !ui.home&&!ui.menu&&!ui.confirm&&!ui.layer&&!pop.on&&!name_on()&&!sx_import.count&&slot_kind();
}
static int bulk_jog(int32_t delta)
{
    if(!bulk_context()||!(fm1_in.buttons&(1u<<panel.btn[B_OCTUP])))return 0;
    uint32_t slots=slot_kind(),cur=slot_cur(slots);
    if(!bulk.on||bulk.on==2u){bulk.anchor=(uint16_t)cur;bulk.engine=(uint8_t)library_engine();bulk.track=song.sel;}
    bulk.on=1;uint32_t limit=user_limit();
    uint32_t to=(uint32_t)clamp((int32_t)cur+delta,0,(int32_t)limit-1);
    if(slots==SK_FM6)fm6_bslot=(uint8_t)to;else if(slots==SK_P5)p5_store_slot=(uint16_t)(to+1u);else ui.uslot=(uint8_t)to;
    bulk.end=(uint16_t)to;ui.bulk_hold=0;oct_eat|=2u;ui.oct_t0|=2u;ui.force=1;ui_message("PRESETS SELECTED");return 1;
}

/* Confirmed writes are infrequent; keep their dispatch out of every input frame. */
static void ui_confirm_accept(void)
{
    uint32_t kind = ui.confirm;
    ui.confirm = 0;
    ui.force = 1;
    if(kind==CF_RESTART){
        fm1_irq_off(); transport_stop(); fm1_irq_on(); /* end an external ARP clock before saving for restart */
        if(!state_autosave()){ui_message("SOUND SAVE FAILED");return;}
        oc_restart=1;ui_message("RESTARTING");
    } else if(kind==CF_OLD_SETTINGS){settings_old_accept();settings_save();
    } else if(kind==CF_IMPORT){preset_old_allow(1);sysex_commit();preset_old_allow(0);
    } else if(kind==CF_BULK_ERASE){bulk_erase();
    } else if (kind == CF_OVR_USER) {
        name_open(NK_USER_SAVE, ui.confirm_trk);
    } else if (kind == CF_ERASE_USER) {
        user_ui_named(1u, ui.confirm_trk, 0);
    } else if (kind == CF_DISCARD_DESIGN) {    /* the sound designer's original back */
        if (designer_on() && designer_quit) { designer_quit(1); ui_message("SOUND RESTORED"); }
    } else if (kind == CF_INIT_SOUND) {
        sound_init_of(TSEL, TSEL->eng_req); ui_message("SOUND INIT");
    }
}

#include "ui_designer_input.c"
static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    if(ui.import_refresh){pop.on=0;brw.on=0;ui.import_refresh=0;ui.force=1;}
    if (scr_input(pressed, notes)) return;
    if (designer_on() && !name_on() && !ui.confirm && designer_input(pressed, notes)) return;
    if (notes)
        browse_commit();                              /* a key played while browsing: hear the sound shown */
    browse_poll();
    layer_lock_input(pressed);
    if(momentary.active && (pressed&(1u<<panel.btn[B_OCTUP]))){
        momentary.active=momentary.count=momentary.native=0;pressed&=~(1u<<panel.btn[B_OCTUP]);ui_message("KEPT");
    }
    uint32_t bank_notes = notes;
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* held: UNDO (ui.c undo_swap) */
    uint32_t octup = btn_hold(&ui.oct_t0, B_OCTUP, now, page_sheet() != 0u);   /* held: the page's sheet */
    uint32_t oct;
    if (octup == BT_HOLD) {
        oct_eat |= 2u;                                  /* (its release is no OCT+) */
        oct_deferred = 0;
        if(bulk_context())ui.bulk_hold=1;else page_sheet_open();
    } else if (octup == BT_TAP && oct_deferred) {
        song.octave += song.octave < 3 ? 1 : 0;         /* (OCT+ pressed on a page with a sheet: on release) */
    }
    if (octup != BT_NONE || !((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u))
        oct_deferred = 0;
    if(ui.bulk_hold && !(fm1_in.buttons&(1u<<panel.btn[B_OCTUP]))){
        if(bulk_context() && !bulk.on)page_sheet_open();ui.bulk_hold=0;
    }
    oct = oct_taps(pressed, oct_nav());
    uint32_t lay, knob_layer, combo = 0, lytap, lkeys;
    int32_t s, ks[4] = {0, 0, 0, 0};
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: put back) */
    layer_arm(pressed, now);
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    /* The armed layer owns the release frame too. Read each knob once: the scan ISR can publish another
     * detent after a take, which must wait for the next frame rather than edit the underlying page. */
    knob_layer = ui.ly && layer_allowed();
    if (knob_layer)
        for (k = 0; k < 4u; k++)
            ks[k] = panel_enc(EN_K1 + k);
    if (!layer_allowed())
        perf_kill = 1;                                  /* (effects off until their keys are let go) */
    else if (!kb_layer)
        perf_kill = 0;
    {   /* a key pressed with the button: a combo (its edge, or the ISR already took it); then not the grid's or a step's */
        static uint32_t kb_seen;
        lkeys = kb_layer & ~kb_seen;
        combo = lay && (notes || lkeys);
        kb_seen = kb_layer;
    }
    if (lay)
        lkeys |= notes & ~fm1_in.notes;                 /* (tapped and let go already) */
    notes &= ~kb_layer;
    if (knob_layer)
        for (k = 0; k < 4u; k++)
            if (ks[k]) combo = 1;
    if (lay) {                                          /* a layer's button held: keys, knobs and buttons are combos */
        combo |= (pressed & ~ly_bit(ui.ly)) != 0u;
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no UNDO, no page */
            ui.save_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_HOME]))       /* no menu, no HOME */
            ui.home_t0 |= 2u;
        s=panel_enc(EN_PRESET);
        if (ui.ly==LAYER_FX && s) fx_assign(s);
        if (ui.ly==LAYER_FX && fx_key_held() && (pressed & (1u<<panel.btn[B_EDIT]))) fx_default();
        panel_enc(EN_ALGO);                             /* (another track: OCT- puts back the layer's track only) */
    }
    lytap = layer_gesture(now, combo);
    layer_masks();
    layer_show();
    layer_keys(lkeys);
    for (k = 0; k < 4u; k++)
        if (lay && ks[k]) {
            layer_knob(k, ks[k]);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
    song.grid = (uint8_t)keys_mode();                 /* (the menu, a dialog: the keys play again; NAME: silent) */
    if (bulk.on && (ui.home || !slot_kind() || bulk.track != song.sel || bulk.engine != library_engine()))
        bulk_clear();
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
        momentary_restore();
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;                             /* (a clear dialog is cancelled, NAME too) */
            name_close();
            ui.force = 1;
        }
    }
    if (ui.menu) {
        momentary_restore();                                      /* HOME / SAVE taps do nothing here */
        if (ui.save_t0)
            ui.save_t0 |= 2u;
        ui.pg_down = 0;
        if (!ui.home_t0)
            menu_input(oct);
        return;
    }
    if (name_on() && !ui.confirm) {                     /* NAME: the keys type, KNOB 1 / 2, OCT+ / OCT- (ui_name.c); */
        if (ui.save_t0)                                 /* SAVE does nothing */
            ui.save_t0 |= 2u;
        ui.pg_down = 0;
        name_input(notes, oct);
        return;
    }
    if (save == BT_HOLD)
        undo_swap();                                    /* the last sound load */
    else if (save == BT_TAP && !ui.confirm)             /* SAVE acts on release (a hold is the undo) */
        open_family(FAM_SAVE);
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ does it; nothing else reacts */
        if (oct & 2u) {
            ui_confirm_accept();
        } else if (oct & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (smap.on) {                                      /* the map: KNOB 1 / 2, OCT+ / OCT- (ui_sections.c); EDIT */
        int32_t k1 = panel_enc(EN_K1), k2 = panel_enc(EN_K2);   /* closes it too (page_tap) */
        smap_input(k1, k2, oct);
        if (lytap)
            layer_tap(lytap);
        b = ui.pg_down & ~fm1_in.buttons;
        ui.pg_down &= (uint16_t)~b;
        for (id = 0; b; id++, b >>= 1)
            if (b & 1u)
                page_tap(panel_btn_of(id));
        enc_drop();
        return;
    }
    if (pop.on == POP_SHEET || pop.on == POP_LIST) {   /* an action sheet, a value's list: KNOB 1 / 2, OCT+ / OCT-
                                                         * (ui_popup.c) */
        int32_t k1 = panel_enc(EN_K1), k2 = panel_enc(EN_K2);
        if (pop.on == POP_LIST) vlist_input(k2, oct);
        else sheet_input(k1, k2, oct);
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (lytap)                                          /* a layer's button acts on release (held: the layer) */
        layer_tap(lytap);
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        go_home();
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY: {                                  /* PLAY: the ARP's HOLD (latch) on / off */
            track_t *t = TSEL;
            t->p[P_AHOLD] = (int16_t)!t->p[P_AHOLD];
            ui_message(t->p[P_AHOLD] ? (t->p[P_AMODE] ? "ARP HOLD ON" : "HOLD ON: ARP OFF") : "ARP HOLD OFF");
            sync_reload = 1;
            ui.force = 1;
            break;
        }
        case B_SEQ:                                     /* SEQ, REC: nothing in the synth build */
        case B_REC:
        case B_SAVE:
        case B_FX:                                      /* the layers' buttons: ui_layer.c */
        case B_GLO:
        case B_SCL:
        case B_EDIT:
        case B_HOME:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]), bit = b == B_OCTUP ? 2u : 1u;
            if (pop.on == POP_PICK) {                   /* a picker: either closes it (ui_popup.c) */
                pop_close();
                oct_eat |= (uint8_t)bit;
                break;
            }
            if (act_cols() || layer_set_open() || list_on())   /* action, list pages: enter / back (below); SET layers:
                                                                 * OCT- */
                break;
            if ((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u) {   /* SAVE held: OCT- undoes a load further, */
                ui.save_t0 |= 2u;                       /* OCT+ redoes (no page, no undo when SAVE is let go) */
                ui.oct_t0 |= 2u;
                oct_eat |= (uint8_t)bit;
                undo_step(b == B_OCTUP);
                break;
            }
            if (!ui.home && cur_page()->graph == GR_BROWSE) {   /* the browser: OCT- back, OCT+ keep (ui_browser.c) */
                browser_key(b == B_OCTUP);
                oct_eat |= (uint8_t)bit;
                break;
            }
            if (oct_nav())                              /* off Stage: Esc / Enter, on release (below) */
                break;
            if ((fm1_in.buttons & both) == both) {
                song.octave = 0;
                ui.oct_t0 |= 2u;                        /* (OCT+ then neither a tap nor a sheet) */
                oct_deferred = 0;
            } else if (b == B_OCTUP && page_sheet() == 1u) {
                oct_deferred = 1;                       /* Stage: OCT+ on release, held: the sound's sheet */
            } else {
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            }
            break;
        }
        default:                                        /* page buttons (GLO SCL ENV LFO EDIT ARP): when let go */
            if (!lay)                                   /* (with FX held: swallowed) */
                ui.pg_down |= (uint16_t)(1u << id);
            break;
        }
    }
    if(momentary.active)ui.pg_down&=(uint16_t)~(1u<<panel.btn[B_LFO]);
    b = ui.pg_down & ~fm1_in.buttons;
    ui.pg_down &= (uint16_t)~b;
    for (id = 0; b; id++, b >>= 1)
        if (b & 1u)
            page_tap(panel_btn_of(id));
    if (bulk.on && (oct & 1u)) { bulk_clear(); oct = 0; }
    if (list_on() && (oct & 2u)) {                      /* a list page: OCT+ the row's list, OCT- Stage */
        list_enter();
    } else if (list_on() && (oct & 1u)) {
        go_home();
    } else if (act_cols() && (oct & 2u)) {              /* action pages: OCT+ does the picked action, */
        act_do();
    } else if (act_cols() && (oct & 1u)) {              /* OCT- drops it, or (none picked) goes HOME */
        if (ui.act)
            ui.act = 0;
        else
            go_home();
    } else if (oct_nav() && !ui.layer && (oct & 2u)) {  /* any other page: OCT+ Enter (a slot's sheet, the page's), */
        if (slot_kind())
            slot_enter();
        else if (cur_page()->graph == GR_SCALE_PICKER)  /* SCALES: done, SCL's list */
            scales_back();
        else if (page_sheet())
            page_sheet_open();
    } else if (oct_nav() && !ui.layer && (oct & 1u)) {  /* OCT- Esc: SCALES back to SCL's list, else Stage */
        uint32_t p = page_titled("SCL");
        if (str_eq(cur_page()->title, "SCALES") && p < NPAGES) {
            ui.page = (uint8_t)p;
            page_entered();
        } else {
            go_home();
        }
    }
    song.grid = (uint8_t)keys_mode();                 /* (seq.c: NAME's keys are silent) */
#if MELODEE_SLICE
    if (notes && slice_page_on())                       /* SLICES: a key picks the slice it plays */
        slice_keys_pick(notes);
#endif

    s = panel_enc(EN_PRESET);
    if (s && !ui.home && cur_page()->graph == GR_SCALE_PICKER) {
        scale_picker_step(list_accel(EN_PRESET, s, scale_picker_count(), 0));
    } else if (s && (ui.home || cur_page()->graph == GR_BROWSE)) {
        /* PRESETS browses the selected part's sounds (all engines, then user presets) on HOME and the
         * PRESETS page only (never the steps); elsewhere (TRACKS too, where one records) a stray turn
         * would throw away the sound being edited */
        browse_turn(EN_PRESET, s);                       /* past the factory ones: user presets */
    } else if (s && sec_on()) {                         /* an engine in sections: the next / previous one (FM6's
                                                         * operators are sections: ui_sections.c) */
        sec_turn(s);
    } else if (s && !ui.home && cur_page()->scope == SC_FMOP) {   /* FM6's operator pages: the operator */
        fm6_opsel = (uint8_t)clamp((int32_t)fm6_opsel + (s > 0 ? 1 : -1), 0, 5);
        ui.force = 1;
    }
    panel_enc(EN_ALGO);                                 /* ALGORITHM: nothing (one track) */
    if ((s = panel_enc(EN_SELECT)) != 0) {          /* SELECT: pages */
        if (bulk_jog(s)) {
        } else if (!ui.home) {
            page_scroll(s);
        }
    }
    for (k = 0; !knob_layer && k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (k < 2u && bulk_jog(s)) continue;
        page_knob(k, s);
    }
    if(momentary.active && !(fm1_in.buttons&(1u<<panel.btn[B_LFO])))momentary_restore();
    autosave_poll(pressed || bank_notes || fm1_in.buttons || fm1_in.notes || ui.hot_t);
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        cv_icon_on(x, 4, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, 3, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "TEACH EACH BUTTON AND KNOB", T_MID, 1);
    lcd_fill(16, 56, 208, 1, T_LINE);
}
static void setup_show(const char *what, const char *name)     /* "PRESS" / "TURN RIGHT", the control */
{
    draw_text_box(0, 80, 240, &AF_S, what, T_MID, 1);
    draw_text_box(0, 100, 240, &AF_L, name, T_THEME, 1);
}
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    setup_title();
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        setup_show("PRESS", B_NAME[i]);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        setup_show("TURN RIGHT", E_NAME[i]);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
