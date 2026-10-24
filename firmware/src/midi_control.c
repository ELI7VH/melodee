/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Adapted from MIDI control contribution by ChanceTheMaker (2026).
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared USB/TRS channel controls. Included by seq.c after its input helpers.
 * USB and TRS intentionally share channel state, matching the existing routing.
 * A synth part has one live bend/wheel state; channels assigned to the same part
 * share it (last controller wins). Drum hits ignore bend and sustain. CC1 keeps the existing matrix routing. */
typedef struct {
    int16_t bend;                           /* signed 14-bit value, zero = centre */
    uint8_t wheel, pedal, targets;
    uint8_t owned[NTRK];                    /* held/pedal notes per track, at most 128 per channel */
    uint8_t ready, semis, cents;
    uint8_t rpn_msb, rpn_lsb;
} midi_channel_t;
static midi_channel_t midi_ch[16];
/* A note's owner, a nibble per note (two notes a byte): bits 0..2 track + 1, bit 3 key released, held by its
 * channel's pedal. mn_get / mn_set speak the byte form: track + 1, MIDI_PEDAL_NOTE */
static uint8_t midi_sel_on[16][64];
static uint16_t midi_owners[NTRK];           /* avoids rescanning all 2048 entries for CC123 */
#define MIDI_PEDAL_NOTE 0x80u
_Static_assert(NTRK < 8u, "a note's owner fits three bits");
static uint32_t mn_get(uint32_t ch, uint32_t note)
{
    uint32_t v = (uint32_t)(midi_sel_on[ch][note >> 1] >> ((note & 1u) * 4u)) & 15u;
    return (v & 7u) | (v & 8u ? MIDI_PEDAL_NOTE : 0u);
}
static void mn_set(uint32_t ch, uint32_t note, uint32_t v)
{
    uint8_t *b = &midi_sel_on[ch][note >> 1];
    uint32_t s = (note & 1u) * 4u, n = (v & 7u) | (v & MIDI_PEDAL_NOTE ? 8u : 0u);
    *b = (uint8_t)((*b & ~(15u << s)) | n << s);
}

static midi_channel_t *midi_channel(uint32_t ch)
{
    midi_channel_t *c = &midi_ch[ch];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}

static uint32_t midi_targets(uint32_t ch)
{
    track_t *t = midi_track(ch);
    return midi_channel(ch)->targets | (t ? 1u << trk_index(t) : 0u);
}

static void midi_expression(track_t *t, const midi_channel_t *c)
{
    int32_t range = ((int32_t)c->semis * 100 + c->cents) * 256 / 100;
    if(t->eng_req==ENGI_PROPHET)range=(clamp(p5_patch_of(t)->raw[P5_BEND],0,11)+1)*256;
    if (drum_track(t))
        return;
    midi_bend_target[trk_index(t)] = (int32_t)c->bend * range / (c->bend < 0 ? 8192 : 8191);
    t->bend_raw = c->bend;                        /* (FM6 bends by its own range: its DX7 function settings) */
}

static void midi_expression_channel(uint32_t ch)
{
    uint32_t i, mask = midi_targets(ch);
    for (i = 0; i < NPART; i++)
        if (mask & (1u << i))
            midi_expression(&trk[i], midi_channel(ch));
}

/* a MIDI note of track t sounds note: one played as itself, or a tone of one played as a chord (chord.c) */
static int midi_note_held(const track_t *t, uint32_t note)
{
    uint32_t ch, id = trk_index(t) + 1u;
    for (ch = 0; ch < 16u; ch++)
        if ((mn_get(ch, note) & 0x7Fu) == id && !mchord_of(ch, note, id))
            return 1;
    return mchord_held(id, note);
}

/* a key held on track t sounds note (the key's own note, or a tone of its chord) */
static int midi_local_held(const track_t *t, uint32_t note)
{
    uint32_t k, i;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] == trk_index(t))
            for (i = 0; i < kb_chn[k]; i++)
                if (kb_chord[k][i] == note)
                    return 1;
    return 0;
}

static void midi_release(uint32_t ch, uint32_t note)
{
    uint32_t id = mn_get(ch, note) & 0x7Fu;
    mn_set(ch, note, 0);
    if (id) {
        midi_owners[id - 1u]--;
        if (!--midi_ch[ch].owned[id - 1u])
            midi_ch[ch].targets &= (uint8_t)~(1u << (id - 1u));
    }
    if (id) {
        step_midi_edge(id - 1u, 0, 0, ch, note);
        mchord_t *m = mchord_of(ch, note, id);
        uint8_t nn[CHORD_MAX];
        uint32_t n = 1, i;
        nn[0] = (uint8_t)note;
        if (m) {                                  /* a chord: exactly the notes it started */
            n = m->n;
            for (i = 0; i < n; i++)
                nn[i] = m->note[i];
            m->id = 0;
        }
        for (i = 0; i < n; i++) {
            if (!midi_local_held(&trk[id - 1u], nn[i]))
                input_off(&trk[id - 1u], nn[i]);  /* input_off also checks other MIDI owners */
        }
    }
}

/* a MIDI note-on (ch, note) of track t: through the scale layouts (seq.c midi_map), then its chord (chord.c) or the
 * note alone. A note another key or MIDI note holds already sounds: not started again. What it plays is kept (an
 * mchord) whenever that is not the note itself, so the note-off ends exactly that. 0: it plays nothing */
static int midi_play(track_t *t, uint32_t ch, uint32_t note, uint32_t vel)
{
    uint8_t nn[CHORD_MAX];
    uint32_t m = midi_map(t, note), n, i, f = 0;
    if (m == KB_SILENT)
        return 0;
    n = chord_build(t, m, nn);
    if (n > 1u || nn[0] != note)
        for (f = 0; f < MCHORD_N && mchord[f].id; f++)
            ;
    if (f == MCHORD_N) {                          /* no room to keep a chord: the note alone, if it is itself */
        if (m != note)
            return 0;
        n = 1;
        nn[0] = (uint8_t)note;
    }
    for (i = 0; i < n; i++) {
        step_midi_edge(trk_index(t), nn[i], 1, ch, note);   /* (SEQ > STEP: the cursor step) */
        if (!midi_note_held(t, nn[i]) && !midi_local_held(t, nn[i]))
            input_on(t, nn[i], vel);
    }
    if (n > 1u || nn[0] != note) {
        mchord_t *m = &mchord[f];
        m->ch = (uint8_t)ch;
        m->src = (uint8_t)note;
        m->n = (uint8_t)n;
        for (i = 0; i < n; i++)
            m->note[i] = nn[i];
        m->id = (uint8_t)(trk_index(t) + 1u);
    }
    return 1;
}

static void midi_note_event(uint32_t ch, uint32_t note, uint32_t vel)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t id = mn_get(ch, note) & 0x7Fu;
    if (vel) {
        track_t *t = midi_track(ch);
        /* Repeated notes replace the previous press, including a pedal-held one. */
        if (id)
            midi_release(ch, note);
        if (!t)
            return;
        midi_expression(t, c);
        if (!midi_play(t, ch, note, vel))           /* (a note the layout silences: no owner) */
            return;
        drum_focus_note(t, note);                   /* (a DRUM track: the drum hit is the one picked) */
        if (t != TSEL)
            midi_hint = (uint8_t)(trk_index(t) + 1u);
        c->targets |= (uint8_t)(1u << trk_index(t));
        mn_set(ch, note, trk_index(t) + 1u);
        midi_owners[trk_index(t)]++;
        c->owned[trk_index(t)]++;
    } else if (id) {
        if (c->pedal && !drum_track(&trk[id - 1u])) {
            step_midi_edge(id - 1u, 0, 0, ch, note);   /* physical release ends step entry, even under sustain */
            mn_set(ch, note, mn_get(ch, note) | MIDI_PEDAL_NOTE);
        } else
            midi_release(ch, note);
    }
}

static void midi_pedal_up(uint32_t ch)
{
    uint32_t note;
    midi_channel(ch)->pedal = 0;
    for (note = 0; note < 128u; note++)
        if (mn_get(ch, note) & MIDI_PEDAL_NOTE)
            midi_release(ch, note);
}

/* Preset/project panic and CC120 must discard ownership so a later pedal-up
 * or note-off cannot release notes subsequently started on another patch. */
static void __attribute__((noinline)) midi_forget_track(uint32_t track)
{
    uint32_t ch, note;
    memset(live_held[track], 0, sizeof live_held[track]);
    step_midi_edge(track, 0, 2, 0, 0);          /* one UI release for the whole track */
    for (ch = 0; ch < 16u; ch++) {
        if (!(midi_ch[ch].targets & (1u << track)))
            continue;
        for (note = 0; note < 128u; note++)
            if ((mn_get(ch, note) & 0x7Fu) == track + 1u)
                mn_set(ch, note, 0);
        midi_ch[ch].targets &= (uint8_t)~(1u << track);
        midi_ch[ch].owned[track] = 0;
    }
    midi_owners[track] = 0;
    mchord_forget(track);
    midi_bend_q8[track] = midi_bend_target[track] = 0;
    trk[track].bend_raw = 0;
}

static void midi_silence_track(uint32_t track)
{
    track_t *t = &trk[track];
    uint32_t i;
    trk_all_off(t);
    t->nheld = t->arp_phys = t->arp_note = t->rh_n = 0;
    t->seq_n = t->seq_hold = t->slide_glide = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            voice_kill(&t->v[i]);             /* one-block fade, regardless of RELEASE */
    midi_forget_track(track);
}

static int midi_track_held(uint32_t track)
{
    uint32_t k;
    if (midi_owners[track])
        return 1;
    for (k = 0; k < 27u; k++)
        if ((fm1_in.notes & (1u << k)) && kb_trk[k] == track)
            return 1;
    return 0;
}

/* what a controller just changed, for the knob card (ui_draw.c draw_cc_card): track, parameter (P_*, or
 * MIDI_POP_NATIVE | CC for an engine's own envelope), value; seq moves on each change */
#define MIDI_POP_NATIVE 0x8000u
#define MIDI_POP_MIX 0x4000u                     /* WaveLoop fork: the monitor crossfade (CC8), value 0..127 */
#define MIDI_POP_DRUM 0x2000u                    /* WaveLoop fork: | sound << 4 | DCF_*, a drum of the kit's field */
static volatile uint16_t midi_pop_id;
static volatile int16_t midi_pop_v;
static volatile uint8_t midi_pop_trk, midi_pop_seq;
static void midi_pop_note(const track_t *t, uint32_t id, int32_t v)
{
    midi_pop_trk = (uint8_t)trk_index(t);
    midi_pop_id = (uint16_t)id;
    midi_pop_v = (int16_t)v;
    midi_pop_seq++;
}
/* what CC cc sets on track t: a parameter (P_*), MIDI_POP_NATIVE | cc for the engine's own envelope (72 73 75 on
 * Prophet, FM6, CZ-1), MIDI_POP_DRUM | DCF_* for the picked drum's field on a DRUM track (WaveLoop fork: knobs 1-4
 * PITCH TONE DECAY PAN, 5 SEND, 6 LEVEL; eng_drum.c drum_control.c), or P_COUNT: nothing (midi_parameter; the
 * controls page, ui_knobmap.c, names it) */
static uint32_t midi_cc_id(const track_t *t, uint32_t cc)
{
    uint32_t id=P_COUNT;
    if(t->eng_req==ENGI_DRUM){
        uint32_t f=cc==74?DCF_PITCH:cc==71?DCF_TONE:cc==5?DCF_DECAY:cc==10?DCF_PAN:cc==76?DCF_SEND:cc==77?DCF_LEVEL:DCF_COUNT;
        if(f<DCF_PAN || (f<DCF_COUNT && drum_mixes(t)))return MIDI_POP_DRUM|f;   /* (SID: the track's PAN, its LFO) */
    }
    switch(cc){case 5:id=P_GLIDE;break;case 7:id=P_LEVEL;break;case 10:id=P_PAN;break;
    case 72:id=P_REL;break;case 73:id=P_ATK;break;case 75:id=P_DEC;break;
    case 76:id=P_LRATE;break;case 77:id=P_LD_FLT;break;case 78:id=P_LD_AMP;break;case 70:id=P_LWAVE;break;
                                                         /* (WaveLoop fork: knobs 5-8, the track's LFO: rate, filter
                                                          * wobble, tremolo, shape; ADSR is on faders 5-8) */
    case 79:return motion_native_tag(t)?P_COUNT:P_SUS;   /* WaveLoop fork: sustain (the engines with their own envelope:
                                                         * none) */
    case 74:case 71:{
        static const uint8_t map[NENGINES]={0,0,0x30,0x56,0,0x07,0,0,0,0,0x30,0x34,0x30,0,0,0,0,0,0,0x12};
        uint32_t macro=(map[t->eng_req%NENGINES]>>(cc==74?4:0))&15u;
        if(macro)id=P_E0+macro-1u;else if(cc==74 && t->eng_req==ENGI_CZ)id=P_ED_FLT;
        break; }default:return P_COUNT;}
    if((cc==72 || cc==73 || cc==75) && motion_native_tag(t))
        return MIDI_POP_NATIVE|cc;
    return id;
}
/* Native envelope loops run only for their CC, outside the audio IRQ's hot loops. */
static __attribute__((noinline)) void midi_parameter(track_t *t,uint32_t cc,uint32_t value)
{
    uint32_t id=midi_cc_id(t,cc);
    if(id==P_COUNT)return;
    if(id&MIDI_POP_DRUM){     /* the picked drum's field (not recorded as motion: it is the kit's own) */
        uint32_t f=id&15u,sound=drum_picked(t);const param_desc_t *d=drum_field_desc(t,sound,f);
        if(d->max<=d->min)return;
        drum_field_set(t,sound,f,f==DCF_PAN?(int32_t)value-64:d->min+(int32_t)((value*(uint32_t)(d->max-d->min)+63u)/127u));
        midi_pop_note(t,MIDI_POP_DRUM|sound<<4|f,drum_field_get(t,sound,f));return;
    }
    if(id&MIDI_POP_NATIVE){   /* (the engine's own envelope: its motion too) */
        uint8_t old[MO_NATIVE_N];uint32_t f=motion_guard();
        motion_native_peek(t,old);
        if(t->eng_req==ENGI_PROPHET){
            p5_edit_value(t,cc==72?P5_RELEASE_AMP:cc==73?P5_ATTACK_AMP:P5_DECAY_AMP,value);
        }else if(t->eng_req==ENGI_FM6){
            uint8_t raw[FP_SIZE+1u];memcpy(raw,fm6_patch[trk_index(t)],sizeof raw);
            for(uint32_t op=0;op<6;op++)raw[op*FP_OP+(cc==72?3:cc==73?0:1)]=(uint8_t)(99u-value*99u/127u);
            fm6_put_patch(trk_index(t),raw,0);
        }else{
            uint32_t tr=trk_index(t);uint8_t raw[CZ_BYTES];
            for(uint32_t line=0;line<2;line++) {
                uint32_t stage=cc==72?(cz_patch[tr].raw[CZ_ENV_END[line][2]]&7u):cc==73?0:1;
                if(cz_ed_put(tr,LCZ_EBASE(line,2)+stage,99u-value*99u/127u,raw))memcpy(cz_patch[tr].raw,raw,128u);
            }
        }
        motion_native_edited(t,old);motion_unguard(f);midi_pop_note(t,MIDI_POP_NATIVE|cc,(int32_t)value);return;
    }
    if(id>=P_COUNT)return;
    const param_desc_t *d=id<P_E0?&TP[id]:&ENGINES[t->eng_req]->edit[id-P_E0];
    if(d->max==d->min)return;
    int32_t v=d->min+(int32_t)(value*(uint32_t)(d->max-d->min)/127u);
    if(id==P_PAN)v=(int32_t)value-64;
    t->p[id]=(int16_t)v;motion_capture(t,id,v);midi_pop_note(t,id,v);
}
/* MiniLab main encoder (controllers/MiniLab-3.md), any channel: CC114 turns (relative, 64 = still) browse the sounds of
 * the track selected on the FM-1, CC112 (Shift + turn) jumps engine to engine, CC115 (press) selects the next track.
 * Counted here (the audio ISR), done by the main loop (ui_input.c midi_encoder_apply) as the panel's own knobs are */
static volatile int8_t midi_enc_sound, midi_enc_bank;
static volatile uint8_t midi_enc_press;
static void midi_encoder_cc(uint32_t cc, uint32_t value)
{
    int32_t d = (int32_t)value - 64;
    if (cc == 114u && d)
        midi_enc_sound = (int8_t)clamp(midi_enc_sound + d, -64, 63);
    else if (cc == 112u && d)
        midi_enc_bank = (int8_t)clamp(midi_enc_bank + d, -64, 63);
    else if (cc == 115u && value)
        midi_enc_press++;
}
static void midi_control(uint32_t ch, uint32_t cc, uint32_t value)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t i, mask;
    track_t *t = midi_track(ch);
    if (t) midi_parameter(t,cc,value);
    midi_encoder_cc(cc, value);
    if (cc == WL_MIX_CC) {                         /* the monitor crossfade, the card says where it is */
        wl_mix = (uint8_t)value;
        midi_pop_trk = 0; midi_pop_id = MIDI_POP_MIX; midi_pop_v = (int16_t)value; midi_pop_seq++;
    }
    switch (cc) {
    case 1:
        c->wheel = (uint8_t)value;
        break;
    case 120:                                      /* All Sound Off: ignores the pedal */
        mask = midi_targets(ch);
        for (i = 0; i < NTRK; i++)
            if (mask & (1u << i))
                midi_silence_track(i);
        break;
    case 123:                                      /* All Notes Off: normal releases, honours pedal */
        mask = midi_targets(ch);
        for (i = 0; i < 128u; i++)
            if (mn_get(ch, i))
                midi_note_event(ch, i, 0);
        for (i = 0; i < NTRK; i++)
            if ((mask & (1u << i)) && !midi_track_held(i)) {
                trk_all_off(&trk[i]);
                trk[i].nheld = trk[i].arp_phys = trk[i].arp_note = trk[i].rh_n = 0;
            }
        break;
    case 121:                                      /* Reset All Controllers, keep bend sensitivity */
        c->bend = 0;
        c->wheel = 0;
        c->rpn_msb = c->rpn_lsb = 127;
        midi_expression_channel(ch);
        midi_pedal_up(ch);
        break;
    case 64:
        if (value >= 64u)
            c->pedal = 1;
        else
            midi_pedal_up(ch);
        break;
    case 101: c->rpn_msb = (uint8_t)value; break;
    case 100: c->rpn_lsb = (uint8_t)value; break;
    case 99: case 98:                              /* NRPN selection cancels RPN data entry */
        c->rpn_msb = c->rpn_lsb = 127;
        break;
    case 6: case 38:
        if (!c->rpn_msb && !c->rpn_lsb) {           /* RPN 0: +/-0..24 semitones, 0..99 cents */
            if (cc == 6u)
                c->semis = (uint8_t)(value > 24u ? 24u : value);
            else
                c->cents = (uint8_t)(value > 99u ? 99u : value);
            midi_expression_channel(ch);
        }
        break;
    default: break;
    }
}

/* Keep the occasional controller/panic dispatch outside the hot rendering loop. */
static void __attribute__((noinline)) midi_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    track_t *t = midi_track(ch);
    /* Controllers retain channel state even outside ROUT, but only mapped/owned tracks receive them.
     * In particular, pedal-up must clear sustain even if the channel currently owns no notes. */
    if (st == 0x90u || st == 0x80u)
        midi_note_event(ch, d1, st == 0x90u ? d2 : 0);
    else if (st == 0xE0u) {
        midi_channel(ch)->bend = (int16_t)((int32_t)(d1 | (d2 << 7)) - 8192);
        midi_expression_channel(ch);
    } else if (st == 0xB0u) {
        if (t) mod_midi(t, st, d1, d2);
        midi_control(ch, d1, d2);
    } else if (st == 0xD0u && t)
        mod_midi(t, st, d1, d2);
}
