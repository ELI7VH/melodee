/* SPDX-License-Identifier: GPL-3.0-only */
/* Real single-track UI, MIDI, master capture, NOR persistence and editor paths. */
#include <assert.h>
#define MELODEE_FLASH 1
#define MELODEE_USB_AUDIO 0
#include "synth_ui_harness.c"
#include "../firmware/src/usb_audio_stream.c"
#define SXQ 1024u
static uint32_t so_w, so_r;
static uint8_t wire[2048];
static uint32_t wire_n;
static void ota_wire_send(const uint8_t *p, uint32_t n)
{ assert(n <= sizeof wire); memcpy(wire, p, n); wire_n = n; }
#define ED_BK_FLASH_PTR(off) (nor + (off))
static int ota_frame_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void ota_frame_done(void) {}
#include "../firmware/src/editor.c"

static void request(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t f[1024] = {0x7D, 0x46, 0x4C};
    f[3] = (uint8_t)cmd;
    if (n) memcpy(f + 4, a, n);
    wire_n = 0; ed_handle(f, n + 4);
}
static void tick(void) { int32_t out[CTL * 2]; mix_block(out, CTL); }
static void pkt(uint32_t st, uint32_t n, uint32_t v)
{ midi_event(st & 0xF0, st & 15, n, v); }

static void playing(void)
{
    ui_power_on();
    assert(NTRK == 1 && NENG_SHOWN == 4);
    const uint32_t engines[] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, 3};
    for (uint32_t i = 0; i < 4; i++) {
        assert(eng_ok(engines[i]));
        host_preset(&trk[0], engines[i], 0);
        trk_note_on(&trk[0], 60, 100);
        uint64_t energy = 0;
        for (uint32_t b = 0; b < 128; b++) {
            int32_t out[CTL * 2]; mix_block(out, CTL);
            for (uint32_t j = 0; j < CTL * 2; j++) energy += labs(out[j]);
        }
        assert(energy); trk_all_off(&trk[0]);
    }
    assert(!eng_ok(ENGI_DRUM));
    ui_power_on();
    for (uint32_t ch = 0; ch < 16; ch++) {
        pkt(0x90 | ch, 60, 100); assert(busy_now());
        pkt(0x80 | ch, 60, 0); pkt(0xB0 | ch, 120, 0); tick();
    }
    press(B_PLAY); assert(trk[0].p[P_AHOLD] && !song.playing);
    press(B_PLAY); assert(!trk[0].p[P_AHOLD]);
    uint32_t page = ui.page, engine = trk[0].eng_req;
    press(B_REC); press(B_SEQ); turn(EN_ALGO, 3);
    assert(ui.home && ui.page == page && trk[0].eng_req == engine);
    trk[0].p[P_AMODE] = 1; press(B_PLAY);
    fm1_in.notes = 1u << 7; tick(); fm1_in.notes = 0; tick();
    assert(trk[0].nheld && trk[0].arp_note);
    press(B_PLAY); tick(); assert(!trk[0].nheld && !trk[0].arp_note);
    trk[0].p[P_AMODE] = 0;
    trk[0].p[P_CHRD] = 1; fm1_in.notes = 1u << 7; tick();
    assert(busy_now() >= 3); fm1_in.notes = 0; tick();
    puts("SYNTH: four engines, every MIDI channel, PLAY latch, chord/ARP release and retired buttons passed");
}

static void persistence(void)
{
    ui_power_on(); memset(nor, 255, sizeof nor);
    persist_boot(); settings_init();
    /* Store foreign project bytes and a template. Every synth/settings save must preserve them. */
    uint8_t foreign[100] = {1, 2, 3}, template[24] = {4, 5, 6};
    uint32_t len = sizeof template, magic = TMPL_MAGIC;
    memcpy(template + len - 8, &len, 4); memcpy(template + len - 4, &magic, 4);
    assert(!st_save(OBJ_BANK0, foreign, sizeof foreign));
    assert(tmpl_take(template, sizeof template));
    settings.palette = 2; settings_save();
    persist_boot(); assert(tmpl_len == sizeof template && !memcmp(set_rec.t, template, sizeof template));
    for (uint32_t i = 0; i < NENG_SHOWN; i++) {
        uint32_t e = ENGINE_ORDER[i];
        host_preset(&trk[0], e, 1); trk[0].p[P_REV] = 51;
        song.g[G_BPM] = 137; song.octave = 2;
        synth_state_t expected, actual; state_capture(&expected);
        assert(state_autosave()); uint32_t before = erases;
        assert(state_autosave() && erases == before);
        ui_power_on(); syn_saved = 0; state_boot(); state_capture(&actual);
        assert(!memcmp(&expected, &actual, sizeof actual));
    }
    trk[0].p[P_REV] = 63; erase_error = 1;
    assert(!state_autosave() && syn_retry);
    erase_error = 0; assert(state_autosave() && !syn_retry);
    uint32_t saved = syn_saved;
    trk[0].p[P_REV] = 71; fail_after = 1;
    assert(!state_autosave()); fail_after = -1;
    ui_power_on(); state_boot(); assert(trk[0].p[P_REV] == 63 && syn_saved == saved);
    bootguard.failed = 1; ui_power_on(); state_boot(); assert(trk[0].p[P_REV] != 63); bootguard.failed = 0;
    song.playing = 1; ui.confirm = CF_RESTART; oc_restart = 0; ui_confirm_accept();
    assert(!song.playing && oc_restart); oc_restart = 0;
    trk[0].p[P_REV]++; erase_error = 1;
    ui.confirm = CF_RESTART; ui_confirm_accept(); assert(!oc_restart); erase_error = 0;
    uint8_t got[100]; assert(st_load(OBJ_BANK0, got, sizeof got) == sizeof got && !memcmp(got, foreign, sizeof got));
    assert(st_sector(OBJ_SYNTH, 0) == 0x9D000 && st_sector(OBJ_SYNTH, 1) == 0x9E000);
    puts("SYNTH: native last-state round trips, no redundant erase, failed/torn saves, boot guard and foreign storage passed");
}

static void capture(void)
{
    ui_power_on(); host_preset(&trk[0], ENGI_CZ, 4);
    trk[0].p[P_PAN] = -40; trk[0].p[P_CHOR] = 70; trk[0].p[P_REV] = 80;
    trk_note_on(&trk[0], 60, 100); ua_reset(); ua.cap_alt = 1;
    uint64_t energy = 0, stereo = 0;
    for (uint32_t b = 0; b < 512; b++) {
        int32_t out[CTL * 2]; mix_block(out, CTL); ua.cw = ua.cr = 0;
        ua_audio(out, CTL, song.master_q12);
        for (uint32_t i = 0; i < CTL * 2; i++) { assert(ua.cap[i] == out[i]); energy += labs(out[i]); }
        for (uint32_t i = 0; i < CTL; i++) stereo += labs(out[2*i] - out[2*i+1]);
    }
    assert(energy && stereo);
    song.master_q12 = 0;
    for (uint32_t b = 0; b < 512; b++) tick();
    int32_t out[CTL * 2]; mix_block(out, CTL); ua.cw = ua.cr = 0; ua_audio(out, CTL, 0);
    for (uint32_t i = 0; i < CTL * 2; i++) assert(labs(out[i]) <= 1 && ua.cap[i] == out[i]);
    puts("SYNTH: stereo capture equals the real post-FX/post-master/post-limiter output passed");
}

static void libraries(void)
{
    ui_power_on(); memset(nor, 255, sizeof nor); up_boot();
    for (uint32_t i = 0; i < NENG_SHOWN; i++) {
        uint32_t e = ENGINE_ORDER[i]; host_preset(&trk[0], e, 1);
        if (e == ENGI_PROPHET || e == ENGI_FM6 || e == ENGI_CZ) {
            uint8_t raw[CZ_BYTES]; uint32_t n = native_size(e);
            if (e == ENGI_PROPHET) memcpy(raw, p5_patch_of(&trk[0]), n);
            else if (e == ENGI_FM6) fm6_pack(fm6_patch[0], raw);
            else memcpy(raw, cz_patch[0].raw, n);
            assert(!native_put(e, 3, raw)); native_cache_reset();
            assert(native_used(e, 3) && !memcmp(native_raw(e, 3), raw, n));
            assert(!native_load(e, 3, 0));
        } else {
            int16_t value = param_desc_of(e, P_E0)->max;
            trk[0].p[P_E0] = value;
            assert(!up_store(3, "SID TEST")); up_cache_reset();
            trk[0].p[P_E0] = 0; assert(!up_load(3)); assert(trk[0].p[P_E0] == value);
        }
    }
    puts("SYNTH: native Prophet/FM6/CZ and general SID user slots survive cache reset and load passed");
}

static void resources_stress(void)
{
    /* Each child starts from boot state. Exercise every engine with all buses and the longest FX buffer. */
    for (uint32_t i = 0; i < NENG_SHOWN; i++) {
        pid_t pid = fork(); assert(pid >= 0);
        if (!pid) {
            memset(nor, 255, sizeof nor); ui_power_on();
            host_preset(&trk[0], ENGINE_ORDER[i], 0);
            trk[0].p[P_DIST] = 100;
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 127;
            song.g[G_RTYPE] = 2; song.playing = 1;
            for (uint32_t k = 0; k < 16; k++) trk_note_on(&trk[0], 48 + k, 100);
            for (uint32_t b = 0; b < 2048; b++) {
                if (b == 64) perf_press(PF_R16, 1);
                if (b == 512) { perf_press(PF_R16, 0); perf_press(PF_OUP, 1); }
                int32_t out[CTL * 2]; mix_block(out, CTL);
                for (uint32_t k = 0; k < CTL * 2; k++) assert(out[k] >= -32768 && out[k] <= 32767);
            }
            assert(!resource_failures && resource[RES_PERFORM].size == 32768);
            assert(resource_used() <= RESOURCE_CAPACITY);
            _exit(0);
        }
        int status; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    puts("SYNTH: all engines with full FX/HALL/REPEAT/harmonizer fit the resource budget and stay bounded passed");
}

int main(void)
{
    resources_stress(); playing(); persistence(); capture(); libraries();
    request(ED_INFO, 0, 0); assert(wire_n > 100);
    FILE *f = fopen("build/host/synth-info.bin", "wb"); assert(f); fwrite(wire + 5, 1, wire_n - 6, f); fclose(f);
    const uint8_t badtrack[] = {1}; request(ED_TRACK_DUMP, badtrack, 1); assert(!wire_n);
    request(ED_STEP_GET, (uint8_t[]){0}, 1); assert(!wire_n);
    request(ED_PROJECT, (uint8_t[]){2, 0}, 2); assert(!wire_n);
    request(ED_DUMP, 0, 0); assert(wire_n);
    assert(!ed_bk_capture());
    uint32_t len; const uint8_t *obj = ed_bk_object(28, &len);
    assert(len == sizeof(synth_state_t) && state_valid((const synth_state_t *)obj));
    synth_state_t original = *(const synth_state_t *)obj;
    f = fopen("build/host/synth-state.bin", "wb"); assert(f); fwrite(&original, 1, sizeof original, f); fclose(f);
    f = fopen("build/host/synth-settings.bin", "wb"); assert(f); fwrite(&ed_bk_set, 1, ed_bk_set_len, f); fclose(f);
    memcpy(ED_BK_RAW, &original, sizeof original);
    ed_bk_id = 28; ed_bk_pos = ed_bk_len = sizeof original; ed_bk_crc = st_crc32(&original, sizeof original);
    trk[0].p[P_REV] = 7;
    assert(!ed_bk_commit_inner()); assert(trk[0].p[P_REV] == original.p[P_REV]);
    ED_BK_RAW[0] ^= 1; ed_bk_crc = st_crc32(ED_BK_RAW, ed_bk_len); uint32_t before = erases;
    assert(ed_bk_commit_inner() == 2 && erases == before);
    puts("SYNTH: last-state archive round trip and malformed record refusal passed");
    puts("SYNTH: live editor info/dump and rejected sequencer/project/track commands passed");
    return 0;
}
