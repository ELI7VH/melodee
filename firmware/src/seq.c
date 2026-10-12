/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, chords, arpeggiator, MIDI in and the external clock (the synth build: no sequencer).
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * The keys and every MIDI channel play the one track. Its CC1 (mod wheel), CC11 (expression) and channel
 * aftertouch go to the track's modulation matrix (mod.c). A key or MIDI note plays a chord when the track's CHRD
 * is on (chord.c): its notes go through the same input as so many keys. MIDI Start / Continue / Stop of the clock
 * input GLO > GLOBAL CLK picks run the transport the ARP, the SLICER and the FX layer follow (song.playing); with
 * the internal clock they run free at BPM. */
#define KB_SILENT 255u
static volatile uint8_t kb_asleep;
static uint32_t kb_prev;
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static uint8_t last_note = 60;
/* Stage's played notes, after scale/chord mapping. The ISR owns the held bits per
 * track; each track's last complete voicing survives release, including a tap between UI frames. */
static uint32_t live_held[NTRK][4], live_last[NTRK][4];
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i) { return i % NTRK; }   /* MIDI channel 0..15 of track i (keys -> MIDI out) */

static void live_note_on(const track_t *t, uint32_t note)
{
    uint32_t i, k = trk_index(t);
    live_held[k][note >> 5] |= 1u << (note & 31u);
    for (i = 0; i < 4u; i++)
        live_last[k][i] = live_held[k][i];
}

/* the 27 keys from F: black or white, and the key's place among the keys of its colour (white 0..15, black
 * 0..10) */
static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)(key_black(i) == key_black(k));
    return n;
}

static uint32_t scale_mask(const track_t *t)
{
    const micro_scale_t *s = micro_scale(t);
    if (s) { /* approximate pitch classes for the keyboard LEDs only */
        uint32_t mask = 0;
        for (uint32_t i = 0; i < s->count; i++)
            mask |= 1u << ((s->pitch[i] + 5000) / 10000 % 12);
        return mask;
    }
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, SCALE_LEGACY - 1)];
}

static uint32_t scale_count(const track_t *t)
{
    if (micro_scale(t)) return micro_scale(t)->count;
    uint32_t mask = scale_mask(t), count = 0;
    while (mask) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

static int32_t mpc_degree(const track_t *t) { return clamp(t->p[P_MPCDEG], 1, (int32_t)scale_count(t)); }

/* scale degree `degree` from C4 (0 = the ROOT above C4) in the track's scale, + offset semitones and TRN. strict: a
 * note off the MIDI range is silent (else clamped). Scales of 5, 6, 8 or 12 notes repeat no degree */
static uint32_t scale_degree_map(const track_t *t, int32_t degree, int32_t offset, int strict)
{
    if (micro_active(t)) return micro_degree_map(t, degree, offset, strict);
    uint32_t mask = scale_mask(t), i;
    int32_t count = (int32_t)scale_count(t), oct, n;
    oct = degree / count;
    degree %= count;
    if (degree < 0) {
        degree += count;
        oct--;
    }
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u) {
            if (!degree)
                break;
            degree--;
        }
    n = 60 + t->p[P_ROOT] + 12 * oct + (int32_t)i + offset + t->p[P_TRANS];
    if (strict && (n < 0 || n > 127))
        return KB_SILENT;
    return (uint32_t)clamp(n, 0, 127);
}

/* note n (a key's or a MIDI note) through the WHITE / ALL layouts. WHITE (and MPC on the keys): the white keys walk
 * the scale from C4 = ROOT, the black keys are silent. ALL: every key the next degree of the scale */
static uint32_t scale_map(const track_t *t, int32_t n, int32_t offset)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t degree;
    if (t->p[P_QUANT] == Q_ALL)
        return scale_degree_map(t, n - 60, offset, 1);
    if (t->p[P_QUANT] == Q_WHITE || t->p[P_QUANT] == Q_MPC) {
        if ((degree = DEGREE[n % 12]) < 0)
            return KB_SILENT;
        return scale_degree_map(t, degree + (n / 12 - 5) * 7, offset, 0);
    }
    return (uint32_t)clamp(n + offset + t->p[P_TRANS], 0, 127);
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    int32_t n;
    if (e->keys && (n = e->keys(t, k)) >= 0)           /* the engine's own key map (GM kit, slices) */
        return (uint32_t)n;
    n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == Q_SNAP) {                     /* SNAP: every key, rounded down to the scale */
        if (micro_active(t)) {
            uint32_t note = micro_snap(t, n + t->p[P_TRANS]);
            return note == KB_SILENT ? note : micro_degree_map(t, (int32_t)note - 60, 12 * song.octave, 1);
        }
        uint32_t mask = scale_mask(t), guard = 12;
        n += 12 * song.octave + t->p[P_TRANS];
        while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
            n--;
        return (uint32_t)clamp(n, 0, 127);
    }
    return scale_map(t, n, 12 * song.octave);
}

/* a MIDI note into track t: WHITE and ALL map it as the keys (without the octave buttons), MPC plays the 16 pads of
 * an MPC's Bank H (MIDI 20..35, H01..H16 of MPC Sample's default map; any other note is silent, kits too) as
 * successive degrees of the scale, pad H02 on degree DEG. Kits and slices keep their own note map. KB_SILENT: none */
static uint32_t midi_map(const track_t *t, uint32_t note)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    if (t->p[P_QUANT] == Q_MPC && (note < 20u || note > 35u))
        return KB_SILENT;
    if (e->keys && e->keys(t, 0) >= 0)
        return note;
    if (t->p[P_QUANT] == Q_MPC)
        return scale_degree_map(t, (int32_t)note - 21 + mpc_degree(t) - 1, 12 * song.octave, 1);
    if (t->p[P_QUANT] == Q_WHITE || t->p[P_QUANT] == Q_ALL)
        return scale_map(t, (int32_t)note, 0);
    if (micro_active(t) && t->p[P_QUANT] == Q_SNAP)
        return micro_snap(t, (int32_t)note + t->p[P_TRANS]);
    return note;
}

#include "chord.c"

/* ------------------------------------------------------------- arp --- */
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u)
        t->held[t->nheld++] = (uint8_t)note;
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note)
            t->held[k++] = t->held[i];
    t->nheld = (uint8_t)k;
}

static void arp_off(track_t *t)                  /* the sounding arp note ends */
{
    trk_note_off(t, t->arp_note);
    t->arp_note = 0;
}

/* n and gate_n: sample time (INT), musical units (MIDI); a stopped external
 * transport uses real time converted to musical units for a tapped note's gate. */
static volatile uint32_t beat_pos, beat_n;      /* clock units into the beat, beat of the bar (0..3) */
static void arp_step(track_t *t, uint32_t n, uint32_t gate_n)
{
    uint32_t period, cnt, list[64], len = 0, i, j, o;
    int32_t sw;
    if (t->arp_note) {
        if (t->arp_off <= gate_n)
            arp_off(t);
        else
            t->arp_off -= gate_n;
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld && t->arp_note)
            arp_off(t);
        return;
    }
    period = seq_div_samples((uint32_t)t->p[P_ARATE]);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    t->arp_pos += n;
    if (t->arp_pos < period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw) && t->arp_pos != 0xFFFFFFF + n)
        return;
    if (t->arp_pos >= 0xFFFFFFFu)
        t->arp_pos = 0;
    else if (midi_sync_active)
        t->arp_pos -= period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw);
    else
        t->arp_pos = 0;
    /* build the note list: held notes (sorted or as played) over OCT octaves */
    for (i = 0; i < t->nheld; i++)
        list[i] = t->held[i];
    cnt = t->nheld;
    if (!t->p[P_AORDER])
        for (i = 1; i < cnt; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                uint32_t x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < cnt && len < 64u; i++)
            if (micro_active(t)) {
                uint32_t note = micro_degree_map(t, (int32_t)list[i] - 60 + (int32_t)scale_note_period(t) * (int32_t)o, 0, 1);
                if (note < 128u) list[len++] = note;
            } else {
                list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
            }
    if (!len) return;
    if (t->p[P_AMODE] == 6) { list[0] = t->held[t->nheld - 1u]; len = 1; } /* REPEAT: last played note, no octave traversal */
    t->arp_idx++;
    switch (t->p[P_AMODE]) {
    case 2:
        j = len - 1u - t->arp_idx % len;
        break;
    case 3: {
        uint32_t cyc = len > 1u ? 2u * len - 2u : 1u, k = t->arp_idx % cyc;
        j = k < len ? k : cyc - k;
        break;
    }
    case 4:
        j = rng() % len;
        break;
    default:
        j = t->arp_idx % len;
        break;
    }
    if (t->arp_note)
        arp_off(t);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_note = (uint8_t)list[j];
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        trk_note_on(t, t->arp_note, 100);
    }
}

static void arp_tick(track_t *t, uint32_t n) { arp_step(t, n, n); }

/* -------------------------------------------------------- note input --- */
static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    last_note = (uint8_t)note;
    live_note_on(t, note);
    if (t->p[P_AMODE])
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    live_held[trk_index(t)][note >> 5] &= ~(1u << (note & 31u));
    arp_remove(t, note);                            /* both: the note may have started in the */
    trk_note_off(t, note);                          /* other mode (ARP switched while held) */
}

/* the effect of key k in the FX layer (PF_N none): the first PF_M1 white keys (F3 .. A4) in order, black keys
 * 1..4 (F#3 G#3 A#3 C#4) the track mutes */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_KEYS ? perf_map[p] : PF_N) : p < NTRK ? PF_M1 + p : PF_N;
}

static uint8_t kb_fx[27];
static __attribute__((noinline)) void perf_key_press(uint32_t k, int down)
{
    uint32_t e, j;
    if (down) {
        e = perf_key(k);
        kb_fx[k] = (uint8_t)(e + 1u);
        perf_press(e, 1);
        return;
    }
    if (!kb_fx[k])
        return;
    e = kb_fx[k] - 1u;
    kb_fx[k] = 0;
    for (j = 0; j < 27u; j++)
        if (kb_fx[j] == e + 1u)
            return;
    perf_press(e, 0);
}

/* the map changed (ui_layer.c: PRESETS with a key held, EDIT, a settings load): a white key held takes its new
 * effect at once (the old one let go); FX LATCH: a latched effect of the key's becomes the new one */
static __attribute__((noinline)) void perf_key_remap(void)
{
    uint32_t k, e, o;
    perf_remap = 0;
    for (k = 0; k < 27u; k++) {
        if (!kb_fx[k] || key_black(k) || (o = kb_fx[k] - 1u) == (e = perf_key(k)))
            continue;
        if (!perf_latch_on) {
            perf_key_press(k, 0);
            perf_key_press(k, 1);
            continue;
        }
        kb_fx[k] = (uint8_t)(e + 1u);
        if (o < PF_N && ((perf_latched >> o) & 1u)) {
            perf_latched &= ~PF_BIT(o);
            if (e < PF_N) {
                perf_latched |= PF_BIT(e);
                perf_ord[e] = ++perf_seq;
            }
        }
    }
}


/* key k plays kb_note[k] on track t: its chord (chord.c; the note alone with CHRD OFF). A note another key
 * holds already sounds: it is not started again (nor sent to MIDI OUT); the key keeps its notes */
static void key_on(uint32_t k, track_t *t)
{
    uint32_t n = chord_build(t, kb_note[k], kb_chord[k]), i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_on(t, x, 100);
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | 100u << 24);
    }
    kb_chn[k] = (uint8_t)n;
    last_note = kb_note[k];                         /* (step entry, the SAMPLE zone: the key's note) */
}

/* key k is up: the notes it started end, but those another key still holds */
static void key_off(uint32_t k, track_t *t)
{
    uint32_t n = kb_chn[k], i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_off(t, x);
        midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
    }
}

#ifdef FM1_INPUT_LAT
/* the press latency stats (hal/fm1_input.h fm1_in_stat, console `inp`): from the first scan that saw
 * key k closed to its note-on here, and to the note's first sample leaving the DMA (kb_out_tick:
 * audio.c, the TIMER4 tick at which the block being rendered starts to play) */
static uint32_t kb_out_tick;
static __attribute__((noinline)) void kb_lat(uint32_t k)
{
    uint32_t t0 = fm1_in.note_t0[k], d = fm1_ticks() - t0, o = kb_out_tick - t0;
    fm1_in_stat.kb_n++;
    fm1_in_stat.kb_sum += d;
    fm1_in_stat.dac_sum += o;
    if (d > fm1_in_stat.kb_max)
        fm1_in_stat.kb_max = d;
    if (o > fm1_in_stat.dac_max)
        fm1_in_stat.dac_max = o;
}
#endif

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    if (perf_remap) perf_key_remap();
    ch = cur ^ kb_prev;                           /* keys also sound while entering steps */
    if (!ch)
        return;
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_trk[k] = song.sel;
            if (kb_asleep) { kb_note[k]=KB_SILENT; continue; }
            if ((fm1_in.buttons & kb_mask) || kb_lock) {       /* a layer's button held: the key is the layer's (ui_layer.c), */
                kb_note[k] = KB_SILENT;           /* with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if ((fm1_in.buttons & perf_mask) || (kb_lock & 2u))
                    perf_key_press(k, 1);
                continue;
            }
            if (song.grid)                        /* NAME (ui_name.c): every key types, none sounds */
                kb_note[k] = KB_SILENT;
            else
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            if (kb_note[k] == KB_SILENT)
                continue;
            key_on(k, &trk[kb_trk[k]]);
#ifdef FM1_INPUT_LAT
            kb_lat(k);
#endif
        } else {
            if ((kb_layer >> k) & 1u) {
                kb_layer &= ~(1u << k);
                perf_key_press(k, 0);
                continue;
            }
            if (kb_note[k] == KB_SILENT)
                continue;
            key_off(k, &trk[kb_trk[k] % NTRK]);
        }
    }
    kb_prev = cur;
}

/* --------------------------------------------------------- transport --- */
/* MIDI Start: the ARP's beat, the SLICER's step 0 and the FX layer's 1/16 from the top; LFOs that retrigger with a
 * synced cycle restart */
static void transport_start(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (t->p[P_LTRIG] && t->p[P_LSYNC]) t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    }
    song.tick = 0;
    beat_pos = 0; beat_n = 0;                      /* the ARP LED's beat from the top too */
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the transport */
    perf_start();                                  /* perform.c: its 1/16 grid too */
}

static void transport_stop(void)
{
    uint32_t i;
    song.playing = 0;
    if (midi_sync_active)                           /* external clock: the arp stops with the transport, */
        for (i = 0; i < NPART; i++)                /* its sounding note too (HOLD keeps the latched chord) */
            if (trk[i].arp_note)
                arp_off(&trk[i]);
}

/* MIDI in: every channel plays the track */
static track_t *midi_track(uint32_t ch) { (void)ch; return TSEL; }

#include "midi_control.c"
#include "midi_clock.c"

static void clock_advance(uint32_t n)
{
    uint32_t i, beat = seq_beat_samples();
    for (i = 0; i < NPART; i++)
        arp_step(&trk[i], song.playing || !song.g[G_CLOCK] ? n : 0u, n);
    beat_pos += n;
    while (beat_pos >= beat) {
        beat_pos -= beat;
        beat_n = (beat_n + 1u) & 3u;
    }
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr;
    uint32_t selected_clock = (uint32_t)song.g[G_CLOCK];
    if (midi_clock.selected != selected_clock) {
        transport_stop();
        midi_clock_mode(0);
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.selected = (uint8_t)selected_clock;
        midi_beat_samples = 0;
    }
    uint32_t clock_mode = midi_clock.mode;
    pr = panic_req;
    panic_req = 0;
    if (midi_in_overflow) {                            /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        memset(midi_sel_on, 0, sizeof midi_sel_on);
        memset(midi_ch, 0, sizeof midi_ch);
        memset(midi_owners, 0, sizeof midi_owners);
        memset(mchord, 0, sizeof mchord);
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            t->arp_note = 0;
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            if (t->arp_note) {
                trk_note_off(t, t->arp_note);
                t->arp_note = 0;
            }
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        uint32_t time = midi_in_time[at];
        if (midi_render_timed && (int32_t)(time - midi_render_time) > 0)
            break;
        mi_r++;
        if (status >= 0xF8u) {
            if (selected_clock == source) {
                if (status == 0xFAu || status == 0xFBu || status == 0xFCu) {
                    if (!clock_mode) midi_clock_mode(source);
                    clock_mode = source;
                }
                if (!clock_mode) continue; /* clocks alone cannot claim the transport */
                if (status == 0xF8u)
                    midi_clock_pulse(ms, time);
                else {
                    if (song.playing)
                        clock_advance(midi_clock_advance(time));
                    midi_clock_transport(status, ms);
                }
                if (song.playing)
                    clock_advance(midi_clock_advance(time));
            }
        } else {
            if (clock_mode && song.playing)
                clock_advance(midi_clock_advance(time));
            midi_event(st, ch, d1, d2);
        }
    }
    if (clock_mode) {
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                transport_stop();                   /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                clock_advance(midi_clock_advance(midi_render_timed ? midi_render_time : MIDI_TIME_NOW()));
        }
        if (!song.playing)
            for (i = 0; i < NPART; i++)
                arp_step(&trk[i], 0, (uint32_t)((uint64_t)n * MIDI_BEAT_UNITS / beat_samples()));
    } else
        clock_advance(n);
    if (song.playing)
        song.tick++;
}
