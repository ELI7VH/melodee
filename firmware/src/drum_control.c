/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop fork: every drum of a kit shaped on its own (the WaveLoop Kit's drum_control.c, on Melodee's drum_patch).
 * Hit a drum - a key, a pad or a MIDI note on its track, its lane on the grid - and it is the track's picked drum
 * (drum_focus). The DRUM pages, Stage's knobs on a DRUM track and the controller's knobs (midi_control.c midi_cc_id)
 * then shape that drum: PITCH DECAY TONE LEVEL (drum_patch: offsets every kit plays, the factory sound at zero) and
 * PAN SEND (drum_mix: its place in the mix against the track's PAN and SEND). A kit with no PAN or SEND set mixes as
 * one bus, exactly as before; with one set, each voice renders on its own bus (voice.c) and mix.c mix_drum_samples
 * places it. KIT SID is one chip on one voice: its drums have no PAN or SEND of their own. Included by eng_drum.c */
enum { DCF_PITCH, DCF_DECAY, DCF_TONE, DCF_LEVEL, DCF_PAN, DCF_SEND, DCF_COUNT };
/* the fields as values: what the screen, the knob card and a controller show and set */
static const param_desc_t DRUM_FIELD[DCF_COUNT] = {
    {"PITCH", F_SEMI, -24, 24, 0, 0, 0},
    {"DECAY", F_PCT, 0, 127, 64, 0, 0},
    {"TONE", F_PCT, 0, 127, 64, 0, 0},
    {"LEVEL", F_PCT, 0, 127, 127, 0, 0},
    {"PAN", F_BIPCT, -64, 63, 0, 0, 0},
    {"SEND", F_PCT, 0, 127, 0, 0, 0},
};
static const param_desc_t DRUM_TONE_AS[3] = {           /* TONE as the drum calls it */
    {"SNAPPY", F_PCT, 0, 127, 64, 0, 0}, {"ATTACK", F_PCT, 0, 127, 64, 0, 0}, {"SNAP", F_PCT, 0, 127, 64, 0, 0},
};
static const param_desc_t DRUM_FIELD_NONE = {"-", F_INT, 0, 0, 0, 0, 0};
static int drum_mixes(const track_t *t) { return !drum_issid(t); }   /* PAN and SEND per drum */
static const param_desc_t *drum_field_desc(const track_t *t, uint32_t sound, uint32_t f)
{
    if (f >= DCF_COUNT || (f >= DCF_PAN && !drum_mixes(t)))
        return &DRUM_FIELD_NONE;
    if (f == DCF_TONE) {
        if (drum_is909(t))
            return sound == D9_SD ? &DRUM_TONE_AS[0] : sound <= D9_HT ? &DRUM_TONE_AS[1] : &DRUM_FIELD[DCF_TONE];
        if (drum_issid(t))
            return &DRUM_TONE_AS[2];
        return sound == D9_SD ? &DRUM_TONE_AS[0] : &DRUM_FIELD[DCF_TONE];
    }
    return &DRUM_FIELD[f];
}
static int32_t drum_field_get(const track_t *t, uint32_t sound, uint32_t f)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK;
    const int8_t *c = drum_patch[k].c[sound & 15u];
    switch (f) {
    case DCF_PITCH: return c[0];
    case DCF_DECAY: return c[1] + 64;
    case DCF_TONE: return c[2] + 64;
    case DCF_LEVEL: return 127 - c[3];
    case DCF_PAN: return drum_mix[k].pan[sound & 15u];
    case DCF_SEND: return clamp(t->p[P_FXSEND] + drum_mix[k].send[sound & 15u], 0, 127);   /* (what it sends) */
    default: return 0;
    }
}
/* SEND is kept against the track's SEND: the drum follows when the track's moves */
static void drum_field_set(const track_t *t, uint32_t sound, uint32_t f, int32_t v)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK;
    int8_t *c = drum_patch[k].c[sound & 15u];
    switch (f) {
    case DCF_PITCH: c[0] = (int8_t)clamp(v, -24, 24); break;
    case DCF_DECAY: c[1] = (int8_t)clamp(v - 64, -64, 63); break;
    case DCF_TONE: c[2] = (int8_t)clamp(v - 64, -64, 63); break;
    case DCF_LEVEL: c[3] = (int8_t)(127 - clamp(v, 0, 127)); break;
    case DCF_PAN: if (drum_mixes(t)) drum_mix[k].pan[sound & 15u] = (int8_t)clamp(v, -64, 63); break;
    case DCF_SEND:
        if (drum_mixes(t))
            drum_mix[k].send[sound & 15u] = (int8_t)clamp(clamp(v, 0, 127) - t->p[P_FXSEND], -127, 127);
        break;
    default: break;
    }
}
/* the drum picked on track t (one this kit has) */
static uint32_t drum_picked(const track_t *t) { return drum_sound_norm(t, drum_focus[(uint32_t)(t - trk) % NTRK]); }

/* A live hit (a key, a pad, a MIDI note; never the sequencer's) picks its drum on its track. The ISR's; the UI
 * follows on the selected track (ui_input.c drum_follow) */
static volatile uint8_t drum_focus_hit;                  /* a bit per track: picked by a hit since the UI looked */
static void drum_focus_note(const track_t *t, uint32_t note)
{
    uint32_t k = (uint32_t)(t - trk);
    if (k >= NTRK || t->eng_req != ENGI_DRUM)
        return;
    drum_focus[k] = (uint8_t)drum_sound_of(t, note);
    drum_focus_hit |= (uint8_t)(1u << k);
}

/* ------------------------------------------------------------- the mix: a bus per voice --- */
/* this block's buses of track k (0: the kit mixes as one), and the ones its voices wrote (voice.c) */
static int32_t (*drum_bus_now[NTRK])[CTL];
static uint8_t drum_bus_used[NTRK];
static int32_t (*drum_buses(const track_t *t))[CTL]
{
    uint32_t k = (uint32_t)(t - trk);
    if (drum_is909(t)) { drum909_part_t *p = drum909_part(k); return p ? p->bus : 0; }
    if (drum_iskit(t)) { dvk_part_t *p = dvk_part(k); return p ? p->bus : 0; }
    if (drum_issid(t)) return 0;
    dr8_part_t *p = dr8_part(k);
    return p ? p->bus : 0;
}
/* before the track renders (mix.c): its drums on their own buses when any has a PAN, or a SEND that sends now.
 * Never across an engine switch's fade or a KIT the state is not sized for yet (eng_state_prepare) */
static __attribute__((noinline)) int drum_mix_begin(track_t *t, uint32_t send_on)
{
    uint32_t k = (uint32_t)(t - trk), i, any = 0;
    drum_bus_now[k] = 0;
    drum_bus_used[k] = 0;
    if (k >= NTRK || t->engine != ENGI_DRUM || t->xf_on || !drum_mixes(t))
        return 0;
    for (i = 0; i < 16u; i++)
        any |= (uint32_t)(drum_mix[k].pan[i] | (send_on ? drum_mix[k].send[i] : 0));
    if (!any || !drum_state_ready(t))
        return 0;
    drum_bus_now[k] = drum_buses(t);
    if (!drum_bus_now[k])
        return 0;
    memset(drum_bus_now[k], 0, sizeof(int32_t) * DRUM_BUSES * CTL);
    return 1;
}
