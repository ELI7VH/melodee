/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop fork: the living set list. The song you are in is never lost and comes back as you left it.
 *  - A song with a project slot (A-D) already saves itself there when the FM-1 has been quiet a while
 *    (ui_input.c autosave_poll -> project_autosave: every sound, tweak and pattern).
 *  - A new song (the power-on or template sounds, no slot yet) claims a free slot the first time it differs from
 *    how it started, and from then on saves itself like any other. With no free slot it waits for a SAVE.
 *  - The slot last used is kept in the settings record (the favourites row of retired engine 5, VOICE: its presets
 *    are never shown, so its bits are never set) and loads at power-on, unless a BOOT project is set (it wins).
 * Flash is written only while stopped and quiet; the slot marker goes through while USB audio streams (persist_quiet),
 * as the song's own save does, so a FM-1 on a computer resumes too. */
#define RS_ROW 5u
#define RS_TAG 0x57u                                     /* 'W' */
#define RS_VER 2u
static uint32_t rs_start_hash;                           /* the new song as it started (power-on) */
static uint32_t rs_hash(void)                            /* the music now, packed as a project slot holds it */
{
    project_t *p = &proj_scratch;
    project_capture(p);
    proj_wire_gen++;                                    /* (the staging RAM: a backup's copy there is gone) */
    if (!bank_pack(proj_wire_u.raw, p, 1))
        return 0;
    return proj_hash(proj_wire_u.raw, BANK_STORE_SIZE);
}
/* a new song that changed takes a free slot (it is the song's slot from now on: proj_cur) */
static void rs_claim(void)
{
    uint32_t k = (uint32_t)song.g[G_SLOT] - 1u, h = rs_hash();
    if (k >= 4u || project_used(k) || project_old(k))
        k = project_free_slot();
    if (h && h != rs_start_hash && k < 4u && !project_used(k) && !project_old(k))
        project_save(k);
}
static void rs_mark(void);
/* quiet a while (autosave_poll): a new song that changed takes a free slot; the slot in use is remembered */
static void resume_save(void)
{
    if (proj_cur >= 4u && settings_autosave && !transport_busy())
        rs_claim();
    rs_mark();
}
static void rs_mark(void)
{
    uint8_t *b = favorites.factory[RS_ROW];
    if (proj_cur < 4u && (b[0] != RS_TAG || b[1] != RS_VER || b[2] != proj_cur + 1u)) {
        memset(b, 0, 32);
        b[0] = RS_TAG; b[1] = RS_VER; b[2] = (uint8_t)(proj_cur + 1u);
#if MELODEE_FLASH
        persist_quiet = 1;
#endif
        settings_save();
    }
}
/* power-on, after project_boot: the song last used, unless a BOOT project loaded */
static void resume_load(void)
{
    const uint8_t *b = favorites.factory[RS_ROW];
    uint32_t slot = b[2] - 1u;
    if (b[0] == RS_TAG && b[1] == RS_VER && slot < 4u && !(settings_boot && project_used(settings_boot - 1u)) &&
        project_used(slot) && !bootguard.failed) {
        project_load(slot);
        song.g[G_SLOT] = (int16_t)(slot + 1u);
    }
    rs_start_hash = proj_cur < 4u ? 0u : rs_hash();
}

/* The songs (the WaveLoop set list): the saved projects, slots A-D in order. pos: the song playing, 1-based (0: a new
 * song, not saved yet); returns how many there are. Stage's band shows it (ui_stage.c) */
static uint32_t song_count(uint32_t *pos)
{
    uint32_t k, n = 0;
    *pos = 0;
    for (k = 0; k < 4u; k++)
        if (project_used(k)) {
            n++;
            if (k == proj_cur)
                *pos = n;
        }
    return n;
}
/* the song playing's name -> b (PROJ_NAME_LEN + 1 bytes): "NEW" before it is saved, "" unnamed */
static void song_name(char *b)
{
    if (proj_cur >= 4u || !project_name(proj_cur, b))
        str_cpy(b, "NEW", PROJ_NAME_LEN + 1u);
}
/* OCT- / OCT+ on HOME (ui_input.c): the previous / next song, round the list. The song playing is kept first, tweaks
 * and all (a new one that changed takes a free slot), so nothing is lost on the way. Stopped only: a load is a cut */
static void song_step(int dir)
{
    uint32_t i, k = 4u, n, pos, from;
    char b[40], name[PROJ_NAME_LEN + 1u];
    if (transport_busy()) {
        ui_message("STOP TO CHANGE SONG");
        return;
    }
    if (proj_cur < 4u)
        project_autosave();
    else
        rs_claim();
    n = song_count(&pos);
    if (!n) {
        ui_message("NO SONGS SAVED");
        return;
    }
    if (n == 1u && pos) {
        ui_message("ONE SONG");
        return;
    }
    from = proj_cur < 4u ? proj_cur : dir > 0 ? 3u : 0u;   /* (a new song: next is the first, previous the last) */
    for (i = 1; i <= 4u; i++) {
        k = (from + (uint32_t)(dir > 0 ? (int)i : 4 - (int)i)) & 3u;
        if (project_used(k))
            break;
    }
    project_load(k);
    if (proj_cur != k)
        return;                                         /* (it said why: project_load) */
    song.g[G_SLOT] = (int16_t)(k + 1u);
    rs_start_hash = 0;
    rs_mark();
    song_count(&pos);
    str_cpy(b, "SONG 1/1 ", sizeof b);
    b[5] = (char)('0' + pos);
    b[7] = (char)('0' + n);
    project_name(k, name);
    str_cpy(b + 9, name[0] ? name : "", sizeof b - 9u);
    ui_message(b);
    ui.force = 1;
}
/* hold OCT+ on HOME (ui_input.c): a new song, a copy of this one (tweaks, patterns, all of it) in the next free slot,
 * the one playing kept first. No name: it is told apart by its number */
static void song_dupe(void)
{
    uint32_t k, pos, n;
    char b[16];
    if (transport_busy()) {
        ui_message("STOP TO CHANGE SONG");
        return;
    }
    if (proj_cur < 4u)
        project_autosave();
    else
        rs_claim();
    k = project_free_slot();
    if (k >= 4u || project_used(k) || project_old(k)) {
        ui_message("SONGS FULL");
        return;
    }
    proj_name[0] = 0;
    if (project_save(k) != 0 || proj_cur != k)
        return;                                         /* (it said why: project_save) */
    song.g[G_SLOT] = (int16_t)(k + 1u);
    rs_start_hash = 0;
    rs_mark();
    n = song_count(&pos);
    str_cpy(b, "NEW SONG 1/1", sizeof b);
    b[9] = (char)('0' + pos);
    b[11] = (char)('0' + n);
    ui_message(b);
    ui.force = 1;
}
