/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop fork: the living set list. The song you are in is never lost and comes back as you left it.
 *  - A song with a project slot (A-D) already saves itself there when the FM-1 has been quiet a while
 *    (ui_input.c autosave_poll -> project_autosave: every sound, tweak and pattern).
 *  - A new song (the power-on or template sounds, no slot yet) claims a free slot the first time it differs from
 *    how it started, and from then on saves itself like any other. With no free slot it waits for a SAVE.
 *  - The slot last used is kept in the settings record (the favourites row of retired engine 5, VOICE: its presets
 *    are never shown, so its bits are never set) and loads at power-on, unless a BOOT project is set (it wins).
 * Flash is written only while stopped and, as every settings write, not while USB audio streams (project.c). */
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
/* quiet a while (autosave_poll): a new song that changed takes a free slot; the slot in use is remembered */
static void resume_save(void)
{
    uint8_t *b = favorites.factory[RS_ROW];
    if (proj_cur >= 4u && settings_autosave && !transport_busy()) {
        uint32_t k = (uint32_t)song.g[G_SLOT] - 1u, h = rs_hash();
        if (k >= 4u || project_used(k) || project_old(k))
            k = project_free_slot();
        if (h && h != rs_start_hash && k < 4u && !project_used(k) && !project_old(k))
            project_save(k);                            /* (it is the song's slot from now on: proj_cur) */
    }
    if (proj_cur < 4u && (b[0] != RS_TAG || b[1] != RS_VER || b[2] != proj_cur + 1u)) {
        memset(b, 0, 32);
        b[0] = RS_TAG; b[1] = RS_VER; b[2] = (uint8_t)(proj_cur + 1u);
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
