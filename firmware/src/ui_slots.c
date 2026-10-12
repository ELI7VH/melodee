/* SPDX-License-Identifier: GPL-3.0-only */
/* Slot pages (docs/design: mock/r6_popups, README "Popups"): SAVE > USER, FM6 STORE and P5 STORE as the list of
 * their slots alone, no action chips. KNOB 1 / 2 the slot, OCT+ its sheet (the page's own actions: act_do in
 * ui_input.c does them, as the chips did), OCT- Stage. The slot picked is a card: its name larger, a line about it.
 * Included by ui_draw.c */
enum { SK_NONE, SK_USER, SK_FM6, SK_P5 };
#define SL_Y0 22                                        /* the rows from y 22: 22 px, the card 46 */
#define SL_ROW 22
#define SL_CARD 46
static struct { uint32_t sig; uint8_t first, save_page; } slv;   /* save_page: USER reached by SAVE (+1): its sheet on
                                                                  * Save here */
static void name_rename(void);                          /* ui_name.c */
static void act_do(void);                               /* ui_input.c */

static uint32_t slot_kind(void)
{
    const page_t *pg;
    if (ui.home || ui.menu || ui.layer)
        return SK_NONE;
    pg = cur_page();
    return pg->graph == GR_USER ? SK_USER : pg->graph == GR_FMSTORE ? SK_FM6 : pg->scope == SC_P5STORE ? SK_P5 : SK_NONE;
}
static uint32_t slot_count(uint32_t k) { (void)k; return user_limit(); }
static uint32_t slot_cur(uint32_t k)                    /* the row picked */
{
    switch (k) {
    case SK_USER: return ui.uslot % user_limit();
    case SK_FM6: return fm6_bslot % user_limit();
    default: return ((uint32_t)p5_store_slot - 1u) % user_limit();
    }
}
/* row r: its tag ("U07", "F012"), its name, used */
static int slot_row(uint32_t k, uint32_t r, char *tag, char *nm)
{
    (void)k;
    user_label(tag, r);
    if (!user_used(r) && !user_old(r)) {
        str_cpy(nm, "--", 16);
        return 0;
    }
    user_name(r, nm);
    return 1;
}
static void slot_detail(uint32_t k, uint32_t r, int used, char *b)   /* the card's second line */
{
    (void)k; (void)r;
    if (!used)
        str_cpy(b, "Empty", 24);
    else if (library_native())
        str_cpy(b, ENGINES[eng_idx(library_engine())]->name, 24);
    else
        str_cpy(b, ENGINES[eng_idx(up_engine(r))]->name, 24);
}

/* ------------------------------------------------------- the sheets --- */
static void sl_do(uint32_t col)                         /* the page's action in column col (act_do), done now */
{
    ui.act = (uint8_t)(col + 1u);
    act_do();
    ui.act = 0;
}
/* the columns (act_do): USER SLOT LOAD ERASE SAVE; the STOREs SLOT STORE SEND INIT */
static void sl_load(void) { sl_do(1u); }
static void sl_save(void) { sl_do(slot_kind() == SK_USER ? 3u : 1u); }
static void sl_erase(void) { sl_do(2u); }                /* (USER: act_do asks ERASE U07?) */
static void sl_send(void) { sl_do(2u); }
static void sl_init(void) { sl_do(3u); }
static const sheet_row_t SHEET_IMPORT[] = {{"Import here",0,0,sysex_choose,0},{"Cancel import",0,0,sysex_cancel,0}};
static const sheet_row_t SHEET_BULK[] = {{"Send selected",0,0,bulk_send,0},{"Erase selected",0,0,bulk_erase_ask,CF_BULK_ERASE},{"Clear selection",0,0,bulk_clear,0}};
static const sheet_row_t SHEET_USER[] = {{"Load", 0, 0, sl_load, 0}, {"Save here", 0, 0, sl_save, 0},
    {"Rename", 0, 0, name_rename, 0}, {"Send",0,0,library_send_current,0}, {"Erase", 0, 0, sl_erase, CF_ERASE_USER}};
static const sheet_row_t SHEET_STORE[] = {{"Save here", 0, 0, sl_save, 0}, {"Send", 0, 0, sl_send, 0},
    {"Init sound", 0, 0, sl_init, 0}};

/* OCT+ on a slot: its sheet */
static void slot_enter(void)
{
    uint32_t k = slot_kind(), r;
    char tag[6], nm[16], t[24];
    int used;
    if (!k)
        return;
    r = slot_cur(k);
    if(k==SK_USER && sx_import.count){sysex_import_name(nm);sheet_open("Import SysEx",nm,SHEET_IMPORT,NELEM(SHEET_IMPORT));return;}
    if(bulk.on){sheet_open("Selected presets","USB SysEx / erase",SHEET_BULK,NELEM(SHEET_BULK));return;}
    used = slot_row(k, r, tag, nm);
    str_cpy(t, tag, sizeof t);
    str_cpy(t + str_len(t), " \xB7 ", 4);
    str_cpy(t + str_len(t), used ? nm : "Empty", sizeof t - str_len(t));
    if (k == SK_USER)
        sheet_open(t, used ? "used" : "empty", SHEET_USER, NELEM(SHEET_USER));
    else
        sheet_open(t, used ? "used" : "empty", SHEET_STORE, NELEM(SHEET_STORE));
    if (k == SK_USER && slv.save_page == ui.page + 1u)  /* (reached by SAVE: on Save here) */
        pop.sel = 1;
    slv.save_page = 0;
}
/* SAVE, the sound sheet's Save as user..: SAVE > USER shown, nothing popped up (Kerem, 2026-10-10); its slot's sheet,
 * when OCT+ opens it, on Save here */
static void slot_save_sheet(void)
{
    slv.save_page = (uint8_t)(ui.page + 1u);
}

/* ----------------------------------------------------------- drawing --- */
static void slot_draw(void)
{
    uint32_t k = slot_kind(), n = slot_count(k), cur = slot_cur(k), boot = 0u;
    uint32_t vis = 1u + (uint32_t)((PV_SOUND_Y - 2 - SL_Y0 - SL_CARD) / SL_ROW), r, sig;
    int32_t y, nx = 16 + text_w(&AF_S, "F000") + 10;   /* the names after the widest tag */
    char tag[6], nm[16];
    if (ui.force) {
        lcd_fill(0, H_HEAD, 240, PV_SOUND_Y - H_HEAD, T_BG);
        ui.graph_sig = 0;
    }
    draw_head();
    pv_sound();
    if (vis > n)
        vis = n;
    slv.first = (uint8_t)clamp((int32_t)cur - (int32_t)vis / 2, 0, (int32_t)(n - vis));
    sig = cur * 7u + slv.first * 131u + boot * 1031u + k * 40503u + ux.gen * 977u + ux.pal * 31u + song.sel * 13u +
          up_gen * 7919u + library_engine() * 104729u + bulk.on * 19u + bulk.anchor * 23u + bulk.end * 29u + sx_import.count * 37u;
    for (r = slv.first; r < slv.first + vis; r++)
        sig = str_hash(sig + (uint32_t)slot_row(k, r, tag, nm), nm);
    if (!ui.force && sig == slv.sig)
        return;
    slv.sig = sig;
    for (r = slv.first, y = SL_Y0; r < slv.first + vis; r++) {
        int used = slot_row(k, r, tag, nm), on = r == cur;
        if (on) {                                       /* the card: the name larger, a line about it */
            char d[24];
            int32_t nw = 196 - (boot == r + 1u ? 44 : 0);
            cv_begin(240, SL_CARD, T_BG);
            cv_rrect(6, 1, 228, SL_CARD - 2, 8, T_THEME, T_BG);
            cv_rrect(7, 2, 226, SL_CARD - 4, 7, T_LIFT, T_THEME);
            cv_text_on(16, 7, &AF_M, tag, T_THEME, T_LIFT);
            cv_free_text(16 + text_w(&AF_M, tag) + 8, 7, &AF_M, used ? nm : "Empty", used ? T_TEXT : T_MID, T_LIFT, nw);
            slot_detail(k, r, used, d);
            if(sx_import.count){fmt_int(d,sx_import.count);str_cpy(d+str_len(d)," incoming patches",20);}
            else if(bulk_has(r))str_cpy(d,"Selected",24);
            cv_text_on(16 + text_w(&AF_M, tag) + 8, 28, &AF_X, d, T_MID, T_LIFT);
            if (boot == r + 1u) {
                cv_rrect(186, 10, 40, 18, 5, T_SURF, T_LIFT);
                cv_text_c(206, 12, &AF_X, "Boot", T_SEC, T_SURF);
            }
            cv_blit(0, (uint32_t)y);
            y += SL_CARD;
        } else {
            cv_begin(240, SL_ROW, T_BG);
            if(bulk_has(r))cv_rect(6,3,3,SL_ROW-6,T_THEME);
            cv_text_on(16, 5, &AF_S, tag, used ? T_MID : T_DIM, T_BG);
            cv_free_text(nx, 5, &AF_S, nm, used ? T_SEC : T_DIM, T_BG, boot == r + 1u ? 180 - nx : 220 - nx);
            if (boot == r + 1u)
                cv_text_r(226, 6, &AF_X, "Boot", T_MID, T_BG);
            if (n > vis) {                              /* where the rows are in the list */
                int32_t span = (int32_t)(PV_SOUND_Y - 2 - SL_Y0), th = span * (int32_t)vis / (int32_t)n;
                int32_t ty = (span - th) * slv.first / (int32_t)(n - vis) - (y - SL_Y0);
                cv_rect(237, 0, 2, SL_ROW, T_LINE);
                if (ty < SL_ROW && ty + th > 0)
                    cv_rect(237, ty, 2, th, T_THEME);
            }
            cv_blit(0, (uint32_t)y);
            y += SL_ROW;
        }
    }
}
