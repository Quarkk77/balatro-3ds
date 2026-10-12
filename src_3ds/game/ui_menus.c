/* ui_menus.c - menu screens from UI_definitions.lua (options, settings,
 * stats, win / game over, run info, deck view, vouchers) and the settings
 * related callbacks from button_callbacks.lua. */
#include "game.h"

#define LOC(k) ((char *)localize_key(k))
#define EVFN(name) static bool name(Event *e_, void *p_)

static Sprite *ui_sprite(double w, double h, int x, int y) {
    Sprite *s = sprite_new(0, 0, w, h, atlas_get(G.SETTINGS.colourblind_option ? "ui_2" : "ui_1"), (SpritePos){ x, y });
    return s;
}
static DynaText *dt_simple(const char *s, Col col, double scale) {
    return DYNATEXT(DT_STR(s), DT_COL1(col), .shadow = true, .float_ = true, .scale = scale);
}

/* ------------------------------------------------------------------ */
/* options                                                             */
/* ------------------------------------------------------------------ */
EVFN(ev_refresh_alerts) { (void)e_; (void)p_; G.REFRESH_ALERTS = true; return true; }

UIDef *create_UIBox_options(void) {
    add_event(EVENT(.blockable = 0, .func = ev_refresh_alerts));
    UIDef *restart = NULL, *main_menu = NULL, *your_collection = NULL, *current_seed = NULL, *credits = NULL;
    if (G.STAGE == STAGE_RUN) {
        restart = UIBUTTON(.id = "restart_button", .label = { localize_key("b_start_new_run") }, .nlabel = 1, .button = G_FUNCS_setup_run, .minw = 5);
        main_menu = UIBUTTON(.label = { localize_key("b_main_menu") }, .nlabel = 1, .button = G_FUNCS_go_to_menu, .minw = 5);
        your_collection = UIBUTTON(.label = { localize_key("b_collection") }, .nlabel = 1, .button = G_FUNCS_your_collection, .minw = 5, .id = "your_collection");
        current_seed = UIR(CFG(.align = "cm", .padding = 0.05),
            UIC(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = (char *)tfmt("%s: ", localize_key("b_seed")), .scale = 0.4, .colour = G.C.WHITE))),
            UIC(CFG(.align = "cm", .padding = 0, .minh = 0.8),
                UIC(CFG(.align = "cm", .padding = 0, .minh = 0.8),
                    UIR(CFG(.align = "cm", .r = 0.1, .colour = G.GAME.seeded ? G.C.RED : G.C.BLACK, .minw = 1.8, .minh = 0.5, .padding = 0.1, .emboss = 0.05),
                        UIC(CFG(.align = "cm"),
                            UIT(CFG(.text = G.GAME.pseudorandom.seed ? G.GAME.pseudorandom.seed : "", .scale = 0.43, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)))))),
            UIBUTTON(.col = true, .button = G_FUNCS_copy_seed, .label = { localize_key("b_copy") }, .nlabel = 1, .colour = G.C.BLUE, .scale = 0.3, .minw = 1.3, .minh = 0.5));
    }
    if (G.STAGE == STAGE_MAIN_MENU)
        credits = UIBUTTON(.label = { localize_key("b_credits") }, .nlabel = 1, .button = G_FUNCS_show_credits, .minw = 5);
    UIDef *settings = UIBUTTON(.button = G_FUNCS_settings, .label = { localize_key("b_settings") }, .nlabel = 1, .minw = 5, .focus_args = FOCUS(.snap_to = true));
    UIDef *high_scores = UIBUTTON(.label = { localize_key("b_stats") }, .nlabel = 1, .button = G_FUNCS_high_scores, .minw = 5);
    UIDef *customize = UIBUTTON(.label = { localize_key("b_customize_deck") }, .nlabel = 1, .button = G_FUNCS_customize_deck, .minw = 5);
    UIDef *contents = uinodes();
    uidef_add(contents, settings);
    if (G.GAME.seeded && current_seed) uidef_add(contents, current_seed);
    uidef_add(contents, restart);
    uidef_add(contents, main_menu);
    uidef_add(contents, high_scores);
    uidef_add(contents, your_collection);
    uidef_add(contents, customize);
    uidef_add(contents, credits);
    return GENERIC_OPTIONS(.contents = contents);
}

/* ------------------------------------------------------------------ */
/* video settings helpers (GET_DISPLAYINFO equivalent)                 */
/* ------------------------------------------------------------------ */
#define MAX_DISPLAYS 8
#define MAX_RES 32
static struct {
    const char *display_names[MAX_DISPLAYS]; int ndisplays;
    struct { const char *strings[MAX_RES]; int w[MAX_RES], h[MAX_RES]; int n; } res[MAX_DISPLAYS];
} VID;
static struct {
    const char *screenmode; bool has_res; int rw, rh; int vsync; bool has_vsync; int display;
} QUEUED;

static int get_displayinfo(const char *screenmode, int display) {
    if (display < 1) display = G.SETTINGS.WINDOW.selected_display ? G.SETTINGS.WINDOW.selected_display : 1;
    if (!screenmode) screenmode = G.SETTINGS.WINDOW.screenmode ? G.SETTINGS.WINDOW.screenmode : "Windowed";
    int count = gfx_monitor_count();
    if (count < 1) count = 1;
    if (count > MAX_DISPLAYS) count = MAX_DISPLAYS;
    VID.ndisplays = count;
    int res_option = 1;
    int realw = gfx_width(), realh = gfx_height();
    static const int common[][2] = { { 3840, 2160 }, { 2560, 1440 }, { 2560, 1080 }, { 1920, 1200 }, { 1920, 1080 }, { 1680, 1050 }, { 1600, 900 },
                                     { 1440, 900 }, { 1366, 768 }, { 1280, 1024 }, { 1280, 800 }, { 1280, 720 }, { 1024, 768 }, { 800, 600 } };
    for (int i = 1; i <= count; i++) {
        VID.display_names[i - 1] = intern(tfmt("%d", i));
        int mw = 0, mh = 0;
        gfx_monitor_size(i - 1, &mw, &mh);
        if (mw <= 0) { mw = realw; mh = realh; }
        VID.res[i - 1].n = 0;
        if (!strcmp(screenmode, "Fullscreen")) {
            for (size_t k = 0; k < sizeof common / sizeof *common && VID.res[i - 1].n < MAX_RES; k++) {
                if (common[k][0] > mw || common[k][1] > mh) continue;
                int n = VID.res[i - 1].n++;
                VID.res[i - 1].strings[n] = intern(tfmt("%d X %d", common[k][0], common[k][1]));
                VID.res[i - 1].w[n] = common[k][0];
                VID.res[i - 1].h[n] = common[k][1];
                if (i == G.SETTINGS.WINDOW.selected_display && i == display && realw == common[k][0] && realh == common[k][1]) res_option = n + 1;
            }
        }
        if (VID.res[i - 1].n == 0) {
            const char *label = !strcmp(screenmode, "Windowed") ? "-" : intern(tfmt("%d X %d", mw, mh));
            VID.res[i - 1].strings[0] = label;
            VID.res[i - 1].w[0] = mw;
            VID.res[i - 1].h[0] = mh;
            VID.res[i - 1].n = 1;
        }
    }
    return res_option;
}

static void change_screen_resolution(CycleCallbackArgs *a) {
    int d = G.SETTINGS.WINDOW.selected_display ? G.SETTINGS.WINDOW.selected_display : 1;
    if (d < 1 || d > VID.ndisplays || a->to_key < 1 || a->to_key > VID.res[d - 1].n) return;
    QUEUED.has_res = true;
    QUEUED.rw = VID.res[d - 1].w[a->to_key - 1];
    QUEUED.rh = VID.res[d - 1].h[a->to_key - 1];
}

static void change_window_cycle_UI(void) {
    if (!G.OVERLAY_MENU) return;
    UIElement *swap = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "resolution_cycle", NULL);
    if (!swap) return;
    int fd = QUEUED.display ? QUEUED.display : G.SETTINGS.WINDOW.selected_display;
    const char *fm = QUEUED.screenmode ? QUEUED.screenmode : G.SETTINGS.WINDOW.screenmode;
    int res_option = get_displayinfo(fm, fd);
    UIElement *c1 = uie_child(swap, 1);
    if (c1) { child_remove_node(&swap->n, &c1->n); node_remove(&c1->n); }
    if (fd < 1 || fd > VID.ndisplays) fd = 1;
    UIBox_add_child(swap->UIBox, OPTION_CYCLE(.w = 4, .scale = 0.8, .options = VID.res[fd - 1].strings, .noptions = VID.res[fd - 1].n,
                                              .opt_callback = change_screen_resolution, .current_option = res_option ? res_option : 1), swap);
}

static void change_vsync(CycleCallbackArgs *a) {
    int v = (int)G.SETTINGS.WINDOW.vsync;
    QUEUED.has_vsync = false;
    if (v == 0 && a->to_key == 1) { QUEUED.has_vsync = true; QUEUED.vsync = 1; }
    else if (v == 1 && a->to_key == 2) { QUEUED.has_vsync = true; QUEUED.vsync = 0; }
}
static void change_screenmode(CycleCallbackArgs *a) {
    static const char *vals[3] = { "Windowed", "Fullscreen", "Borderless" };
    if (a->to_key >= 1 && a->to_key <= 3) QUEUED.screenmode = vals[a->to_key - 1];
    change_window_cycle_UI();
}
static void change_display(CycleCallbackArgs *a) {
    QUEUED.display = a->to_key;
    change_window_cycle_UI();
}
static void change_gamespeed(CycleCallbackArgs *a) { G.SETTINGS.GAMESPEED = atof(a->to_val); }
static void change_play_discard_position(CycleCallbackArgs *a) {
    G.SETTINGS.play_button_pos = a->to_key;
    if (G.buttons) {
        node_remove(&G.buttons->n);
        G.buttons = uibox_new(create_UIBox_buttons(), UIBCFG(.align = "bm", .has_offset = true, .offset = { 0, 0.3 }, .major = &G.hand->n, .bond = BOND_WEAK));
    }
}
static void change_shadows(CycleCallbackArgs *a) {
    str_set(&G.SETTINGS.GRAPHICS.shadows, a->to_key == 1 ? "On" : "Off");
    game_save_settings();
}
static void change_pixel_smoothing(CycleCallbackArgs *a) {
    G.SETTINGS.GRAPHICS.texture_scaling = a->to_key;
    game_set_render_settings();
    game_save_settings();
}
static void change_crt_bloom(CycleCallbackArgs *a) {
    G.SETTINGS.GRAPHICS.bloom = a->to_key;
    game_save_settings();
}

GF_IMPL(can_apply_window_changes) {
    bool can = false;
    if (QUEUED.screenmode && !str_eq(QUEUED.screenmode, G.SETTINGS.WINDOW.screenmode)) can = true;
    else if (QUEUED.has_res) can = true;
    else if (QUEUED.has_vsync) can = true;
    else if (QUEUED.display && QUEUED.display != G.SETTINGS.WINDOW.selected_display) can = true;
    if (can) { e->config->button = G_FUNCS_apply_window_changes; e->config->colour = G.C.RED; }
    else { e->config->button = NULL; e->config->colour = G.C.UI.BACKGROUND_INACTIVE; }
}

void apply_window_changes(bool initial) {
    bool to_windowed = QUEUED.screenmode && !strcmp(QUEUED.screenmode, "Windowed");
    str_set(&G.SETTINGS.WINDOW.screenmode, QUEUED.screenmode ? QUEUED.screenmode : (G.SETTINGS.WINDOW.screenmode ? G.SETTINGS.WINDOW.screenmode : "Windowed"));
    G.SETTINGS.WINDOW.selected_display = QUEUED.display ? QUEUED.display : (G.SETTINGS.WINDOW.selected_display ? G.SETTINGS.WINDOW.selected_display : 1);
    G.SETTINGS.WINDOW.screen_res.w = QUEUED.has_res ? QUEUED.rw : gfx_width();
    G.SETTINGS.WINDOW.screen_res.h = QUEUED.has_res ? QUEUED.rh : gfx_height();
    G.SETTINGS.WINDOW.vsync = QUEUED.has_vsync ? QUEUED.vsync : G.SETTINGS.WINDOW.vsync;
    const char *sm = G.SETTINGS.WINDOW.screenmode;
    int w = to_windowed ? (int)(gfx_width() * 0.8) : G.SETTINGS.WINDOW.screen_res.w;
    int h = to_windowed ? (int)(gfx_height() * 0.8) : G.SETTINGS.WINDOW.screen_res.h;
    gfx_set_mode(w, h, strcmp(sm, "Windowed") != 0 && strcmp(sm, "Borderless") != 0, !strcmp(sm, "Borderless"),
                 G.SETTINGS.WINDOW.selected_display - 1, G.SETTINGS.WINDOW.vsync != 0);
    memset(&QUEUED, 0, sizeof QUEUED);
    if (!initial) {
        game_resize(gfx_width(), gfx_height());
        game_save_settings();
    }
    if (G.OVERLAY_MENU) {
        UIElement *tab_but = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "tab_but_Video", NULL);
        G_FUNCS_change_tab(tab_but);
    }
}
GF_IMPL(apply_window_changes) { apply_window_changes(false); }

/* ------------------------------------------------------------------ */
/* settings                                                            */
/* ------------------------------------------------------------------ */
static void refresh_contrast_mode_cb(bool v) { (void)v; refresh_contrast_mode(); }

void refresh_contrast_mode(void) {
    if (G.SETTINGS.colourblind_option) {
        G.C.SUITS.Hearts = G.C.SO_2.Hearts; G.C.SUITS.Diamonds = G.C.SO_2.Diamonds;
        G.C.SUITS.Spades = G.C.SO_2.Spades; G.C.SUITS.Clubs = G.C.SO_2.Clubs;
    } else {
        G.C.SUITS.Hearts = G.C.SO_1.Hearts; G.C.SUITS.Diamonds = G.C.SO_1.Diamonds;
        G.C.SUITS.Spades = G.C.SO_1.Spades; G.C.SUITS.Clubs = G.C.SO_1.Clubs;
    }
    for (int i = 0; i < G.I.CARD.count; i++) {
        Node *n = G.I.CARD.items[i];
        if (!node_is(n, NT_CARD)) continue;
        Card *c = (Card *)n;
        if (c->config.card && child_get(n, "front") && !str_eq(c->ability.effect, "Stone Card")) Card_set_sprites(c, NULL, c->config.card);
    }
}

static bool rumble_toggle;
static UIDef *settings_tab_fn(void *arg) { return UIDEF_settings_tab((const char *)arg); }

UIDef *create_UIBox_settings(void) {
    TabsArgs ta = { .tab_h = 7.05, .tab_alignment = "tm", .snap_to_nav = true };
    ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_set_game"), .chosen = true, .tab_definition_function = settings_tab_fn, .tab_definition_function_args = "Game" };
    if (G.F_VIDEO_SETTINGS)
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_set_video"), .tab_definition_function = settings_tab_fn, .tab_definition_function_args = "Video" };
    ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_set_graphics"), .tab_definition_function = settings_tab_fn, .tab_definition_function_args = "Graphics" };
    ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_set_audio"), .tab_definition_function = settings_tab_fn, .tab_definition_function_args = "Audio" };
    return GENERIC_OPTIONS(.back_func = G_FUNCS_options, .contents = uidef_add(uinodes(), create_tabs(ta)));
}

static const char **loc_list_heap(const char *key, int *n) {
    const char **out = xcalloc(16, sizeof(char *));
    *n = localize_list(key, NULL, out, 16);
    return out;
}

UIDef *UIDEF_settings_tab(const char *tab) {
    if (str_eq(tab, "Game")) {
        int nopt;
        const char **pd = loc_list_heap("ml_play_discard_pos_opt", &nopt);
        double gs = G.SETTINGS.GAMESPEED;
        int gs_opt = gs == 0.5 ? 1 : gs == 4 ? 4 : (int)gs + 1;
        if (G.SETTINGS.screenshake < 0) G.SETTINGS.screenshake = G.SETTINGS.reduced_motion ? 0 : 50;
        int ninfo; const char *info[4]; ninfo = localize_list("ml_crash_report_info", NULL, info, 4);
        ToggleArgs crash = { .label = localize_key("b_set_crash_reports"), .ref = &G.SETTINGS.crashreports, .ninfo = ninfo };
        for (int i = 0; i < ninfo; i++) crash.info[i] = info[i];
        return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            OPTION_CYCLE(.label = localize_key("b_set_gamespeed"), .scale = 0.8, .options = STRS("0.5", "1", "2", "4"), .noptions = 4,
                         .opt_callback = change_gamespeed, .current_option = gs_opt),
            OPTION_CYCLE(.w = 5, .label = localize_key("b_set_play_discard_pos"), .scale = 0.8, .options = pd, .noptions = nopt,
                         .opt_callback = change_play_discard_position, .current_option = (int)G.SETTINGS.play_button_pos),
            UIIF(G.F_RUMBLE, TOGGLE(.label = localize_key("b_set_rumble"), .ref = &rumble_toggle)),
            SLIDER(.label = localize_key("b_set_screenshake"), .w = 4, .h = 0.4, .ref = REFN(&G.SETTINGS.screenshake), .min = 0, .max = 100),
            TOGGLE(.label = localize_key("ph_display_stickers"), .ref = &G.SETTINGS.run_stake_stickers),
            TOGGLE(.label = localize_key("b_high_contrast_cards"), .ref = &G.SETTINGS.colourblind_option, .callback = refresh_contrast_mode_cb),
            TOGGLE(.label = localize_key("b_reduced_motion"), .ref = &G.SETTINGS.reduced_motion),
            UIIF(G.F_CRASH_REPORTS, create_toggle(crash)));
    } else if (str_eq(tab, "Video")) {
        memset(&QUEUED, 0, sizeof QUEUED);
        int res_option = get_displayinfo(G.SETTINGS.WINDOW.screenmode, G.SETTINGS.WINDOW.selected_display);
        int sd = G.SETTINGS.WINDOW.selected_display;
        if (sd < 1 || sd > VID.ndisplays) sd = 1;
        int nwm, nvs;
        const char **wm = loc_list_heap("ml_windowmode_opt", &nwm);
        const char **vs = loc_list_heap("ml_vsync_opt", &nvs);
        const char *sm = G.SETTINGS.WINDOW.screenmode;
        int wm_opt = str_eq(sm, "Fullscreen") ? 2 : str_eq(sm, "Borderless") ? 3 : 1;
        const char **dn = xcalloc((size_t)VID.ndisplays, sizeof(char *));
        memcpy(dn, VID.display_names, (size_t)VID.ndisplays * sizeof(char *));
        return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            OPTION_CYCLE(.w = 4, .scale = 0.8, .label = localize_key("b_set_monitor"), .options = dn, .noptions = VID.ndisplays, .opt_callback = change_display,
                         .current_option = sd),
            OPTION_CYCLE(.w = 4, .scale = 0.8, .label = localize_key("b_set_windowmode"), .options = wm, .noptions = nwm, .opt_callback = change_screenmode,
                         .current_option = wm_opt),
            UIR(CFG(.align = "cm", .id = "resolution_cycle"),
                OPTION_CYCLE(.w = 4, .scale = 0.8, .options = VID.res[sd - 1].strings, .noptions = VID.res[sd - 1].n, .opt_callback = change_screen_resolution,
                             .current_option = res_option ? res_option : 1)),
            UIR(CFG(.align = "cm"),
                OPTION_CYCLE(.w = 4, .scale = 0.8, .options = vs, .noptions = nvs, .opt_callback = change_vsync, .current_option = G.SETTINGS.WINDOW.vsync == 0 ? 2 : 1)),
            UIBUTTON(.button = G_FUNCS_apply_window_changes, .label = { localize_key("b_set_apply") }, .nlabel = 1, .minw = 3, .func = G_FUNCS_can_apply_window_changes));
    } else if (str_eq(tab, "Audio")) {
        return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            SLIDER(.label = localize_key("b_set_master_vol"), .w = 5, .h = 0.4, .ref = REFN(&G.SETTINGS.SOUND.volume), .min = 0, .max = 100),
            SLIDER(.label = localize_key("b_set_music_vol"), .w = 5, .h = 0.4, .ref = REFN(&G.SETTINGS.SOUND.music_volume), .min = 0, .max = 100),
            SLIDER(.label = localize_key("b_set_game_vol"), .w = 5, .h = 0.4, .ref = REFN(&G.SETTINGS.SOUND.game_sounds_volume), .min = 0, .max = 100));
    } else if (str_eq(tab, "Graphics")) {
        int nsh, nsm, nbl;
        const char **sh = loc_list_heap("ml_shadow_opt", &nsh);
        const char **smo = loc_list_heap("ml_smoothing_opt", &nsm);
        const char **bl = loc_list_heap("ml_bloom_opt", &nbl);
        return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            OPTION_CYCLE(.w = 4, .scale = 0.8, .label = localize_key("b_set_shadows"), .options = sh, .noptions = nsh, .opt_callback = change_shadows,
                         .current_option = str_eq(G.SETTINGS.GRAPHICS.shadows, "On") ? 1 : 2),
            OPTION_CYCLE(.w = 4, .scale = 0.8, .label = localize_key("b_set_pixel_smoothing"), .options = smo, .noptions = nsm, .opt_callback = change_pixel_smoothing,
                         .current_option = (int)G.SETTINGS.GRAPHICS.texture_scaling),
            SLIDER(.label = localize_key("b_set_CRT"), .w = 4, .h = 0.4, .ref = REFN(&G.SETTINGS.GRAPHICS.crt), .min = 0, .max = 100),
            OPTION_CYCLE(.w = 4, .scale = 0.8, .label = localize_key("b_set_CRT_bloom"), .options = bl, .noptions = nbl, .opt_callback = change_crt_bloom,
                         .current_option = (int)G.SETTINGS.GRAPHICS.bloom));
    }
    return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR, .minh = 5, .minw = 5));
}

/* ------------------------------------------------------------------ */
/* usage stats                                                         */
/* ------------------------------------------------------------------ */
typedef struct { const char *type, *set; } UsageArgs;
static UIDef *usage_fn(void *arg) { UsageArgs *a = arg; return create_UIBox_usage(a->type, a->set); }

UIDef *UIDEF_usage_tabs(void) {
    static UsageArgs args[6] = { { "joker_usage", NULL }, { "consumeable_usage", NULL }, { "consumeable_usage", "Tarot" },
                                 { "consumeable_usage", "Planet" }, { "consumeable_usage", "Spectral" }, { "voucher_usage", "Voucher" } };
    static const char *labels[6] = { "b_stat_jokers", "b_stat_consumables", "b_stat_tarots", "b_stat_planets", "b_stat_spectrals", "b_stat_vouchers" };
    TabsArgs ta = { .tab_h = 8, .snap_to_nav = true };
    for (int i = 0; i < 6; i++)
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key(labels[i]), .chosen = i == 0, .tab_definition_function = usage_fn, .tab_definition_function_args = &args[i] };
    return GENERIC_OPTIONS(.back_func = G_FUNCS_high_scores, .contents = uidef_add(uinodes(), create_tabs(ta)));
}

typedef struct { double count; const char *key; } UsedCard;
static int cmp_used(const void *a, const void *b) {
    const UsedCard *x = a, *y = b;
    return x->count > y->count ? -1 : x->count < y->count ? 1 : 0;
}

UIDef *create_UIBox_usage(const char *type, const char *set) {
    DVal *usage = dv_get(profile(), type);
    UsedCard *used = xcalloc((size_t)(dv_count(usage) + 1), sizeof(UsedCard));
    int nused = 0;
    double max_amt = 0;
    for (int i = 0; i < dv_count(usage); i++) {
        const DKey *k = dv_key(usage, i);
        if (!k->is_str) continue;
        Center *c = P_CENTER(k->s);
        if (c && (!set || str_eq(c->set, set)) && c->discovered) {
            double cnt = dv_get_num(dv_val(usage, i), "count", 0);
            used[nused].count = cnt; used[nused].key = c->key; nused++;
            if (cnt > max_amt) max_amt = cnt;
        }
    }
    qsort(used, (size_t)nused, sizeof(UsedCard), cmp_used);
    Col *sc = set ? set_colour_slot(set, true) : NULL;
    (void)sc;
    UIDef *hist = uinodes();
    for (int i = 0; i < 10; i++) {
        if (i < nused) {
            UsedCard *v = &used[i];
            Card *card = card_new(0, 0, 0.7 * G.CARD_W, 0.7 * G.CARD_H, NULL, P_CENTER(v->key), NULL);
            card->n.ambient_tilt = 0.8;
            CardArea *ca = cardarea_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W * 0.7, G.CARD_H * 0.7,
                                        CA_ARGS(.card_limit = 2, .has_card_limit = true, .type = "title", .highlight_limit = 0, .has_highlight_limit = true));
            CardArea_emplace(ca, card, NULL, false);
            Col *bc = set_colour_slot(P_CENTER(v->key)->set, true);
            uidef_add(hist, UIC(CFG(.align = "bm", .minh = 6.2, .colour = G.C.UI.TRANSPARENT_DARK, .r = 0.1),
                UIR(CFG(.align = "bm"),
                    UIR(CFG(.align = "cm", .minh = 0.7 * G.CARD_H + 0.1), UIO(CFG(.object = &ca->n))),
                    UIR(CFG(.align = "cm", .padding = 0.1),
                        UIT(CFG(.text = (char *)num_str(v->count), .scale = 0.35, .colour = mix_colours(G.C.FILTER, G.C.WHITE, 0.8), .shadow = true))),
                    UIR(CFG(.align = "cm"),
                        UIR(CFG(.align = "cm", .minh = v->count / max_amt * 3.6, .minw = 0.8, .colour = (bc && *bc) ? *bc : G.C.RED, .res = 0.15, .r = 0.001))))));
        } else {
            uidef_add(hist, UIC(CFG(.align = "bm", .minh = 6.2, .colour = G.C.UI.TRANSPARENT_DARK, .r = 0.1),
                UIR(CFG(.align = "bm"),
                    UIR(CFG(.align = "cm", .minh = 0.7 * G.CARD_H + 0.1, .minw = 0.7 * G.CARD_W)),
                    UIR(CFG(.align = "cm", .padding = 0.1), UIT(CFG(.text = "-", .scale = 0.35, .colour = mix_colours(G.C.FILTER, G.C.WHITE, 0.8), .shadow = true))),
                    UIR(CFG(.align = "cm"), UIR(CFG(.align = "cm", .minh = 0.2, .minw = 0.8, .colour = G.C.UI.TRANSPARENT_LIGHT, .res = 0.15, .r = 0.001))))));
        }
    }
    free(used);
    const char *title = str_eq(type, "joker_usage") ? localize_key("ph_stat_joker") : str_eq(type, "consumeable_usage") ? localize_key("ph_stat_consumable")
                      : localize_key("ph_stat_voucher");
    return UIROOT(CFG(.align = "cm", .minw = 3, .padding = 0.1, .r = 0.1, .colour = G.C.UI.TRANSPARENT_DARK),
        UIR(CFG(.align = "cm", .padding = 0.1),
            UIB(CFG(.w = 0.2, .h = 0.2, .r = 0.1, .colour = G.C.FILTER)),
            UIT(CFG(.text = (char *)title, .scale = 0.35, .colour = G.C.WHITE))),
        uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.1)), hist));
}

/* ------------------------------------------------------------------ */
/* customize deck                                                      */
/* ------------------------------------------------------------------ */
static const char *COLLAB_OPTIONS[4][7] = {
    { "default", "collab_TW", "collab_CYP", "collab_SK", "collab_DS", "collab_AC", "collab_STP" },
    { "default", "collab_AU", "collab_TBoI", "collab_CL", "collab_D2", "collab_CR", "collab_BUG" },
    { "default", "collab_VS", "collab_STS", "collab_PC", "collab_WF", "collab_FO", "collab_DBD" },
    { "default", "collab_DTD", "collab_SV", "collab_EG", "collab_XR", "collab_C7", "collab_R" },
};
static int suit_idx(const char *s) {
    return str_eq(s, "Spades") ? 0 : str_eq(s, "Hearts") ? 1 : str_eq(s, "Clubs") ? 2 : str_eq(s, "Diamonds") ? 3 : -1;
}
const char *collab_option(const char *suit, int idx) {
    int si = suit_idx(suit);
    if (si < 0 || idx < 1 || idx > 7) return "default";
    return COLLAB_OPTIONS[si][idx - 1];
}
char **collab_setting(const char *suit) {
    switch (suit_idx(suit)) {
    case 0: return &G.SETTINGS.CUSTOM_DECK_Collabs.Spades;
    case 1: return &G.SETTINGS.CUSTOM_DECK_Collabs.Hearts;
    case 2: return &G.SETTINGS.CUSTOM_DECK_Collabs.Clubs;
    case 3: return &G.SETTINGS.CUSTOM_DECK_Collabs.Diamonds;
    }
    return NULL;
}

static void change_collab(CycleCallbackArgs *a) {
    const char *suit = a->cycle_config->ref_value;   /* curr_suit */
    char **slot = collab_setting(suit);
    if (slot) str_set(slot, collab_option(suit, a->to_key));
    for (int i = 0; i < G.I.CARD.count; i++) {
        Node *n = G.I.CARD.items[i];
        if (!node_is(n, NT_CARD)) continue;
        Card *c = (Card *)n;
        if (c->config.card && child_get(n, "front") && !str_eq(c->ability.effect, "Stone Card")) Card_set_sprites(c, NULL, c->config.card);
    }
    game_save_settings();
}

static UIDef *custom_deck_tab_fn(void *arg) { return UIDEF_custom_deck_tab((const char *)arg); }

UIDef *create_UIBox_customize_deck(void) {
    TabsArgs ta = { .snap_to_nav = true, .no_shoulders = true };
    static const char *suits[4] = { "Spades", "Hearts", "Clubs", "Diamonds" };
    for (int i = 0; i < 4; i++)
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_cat(suits[i], "suits_plural"), .chosen = i == 0, .tab_definition_function = custom_deck_tab_fn,
                                         .tab_definition_function_args = (void *)suits[i] };
    return GENERIC_OPTIONS(.back_func = G_FUNCS_options, .contents = uidef_add(uinodes(), UIR(CFG(.align = "cm", .padding = 0), create_tabs(ta))));
}

UIDef *UIDEF_custom_deck_tab(const char *suit) {
    UIDef *t = uinodes();
    CardArea *face_cards = cardarea_new(0, 0, 4 * G.CARD_W, 1.4 * G.CARD_H,
                                        CA_ARGS(.card_limit = 3, .has_card_limit = true, .type = "title", .highlight_limit = 0, .has_highlight_limit = true));
    uidef_add(t, UIR(CFG(.align = "cm", .colour = G.C.BLACK, .r = 0.1, .padding = 0.07, .no_fill = true), UIO(CFG(.object = &face_cards->n))));
    DVal *lo = dv_get(loc_misc("collabs"), suit);
    const char **opts = xcalloc(8, sizeof(char *));
    int nopts = 0;
    for (int i = 1; i <= 7; i++) {
        DVal *v = dv_get(lo, tfmt("%d", i));
        if (!v) v = dv_geti(lo, i);
        if (!v) break;
        opts[nopts++] = dv_str_or(v, "");
    }
    int current = 1;
    char **slot = collab_setting(suit);
    for (int k = 1; k <= 7; k++) if (slot && str_eq(*slot, collab_option(suit, k))) current = k;
    uidef_add(t, UIR(CFG(.align = "cm"),
        OPTION_CYCLE(.options = opts, .noptions = nopts, .w = 5.5, .cycle_shoulders = true, .ref_value = suit, .opt_callback = change_collab,
                     .current_option = current, .colour = G.C.RED, .focus_args = FOCUS(.snap_to = true, .nav = "wide"))));
    uidef_add(t, TOGGLE(.label = localize_key("b_high_contrast_cards"), .ref = &G.SETTINGS.colourblind_option, .callback = refresh_contrast_mode_cb));
    static const char *faces[3] = { "K", "Q", "J" };
    for (int i = 0; i < 3; i++) {
        const char *code = tfmt("%c_%s", suit[0], faces[i]);
        Card *card = card_new(0, 0, G.CARD_W * 1.2, G.CARD_H * 1.2, P_CARD(code), P_CENTER("c_base"), NULL);
        card->no_ui = true;
        CardArea_emplace(face_cards, card, NULL, false);
    }
    return uidef_add_all(UIROOT(CFG(.align = "cm", .padding = 0, .colour = G.C.CLEAR, .r = 0.1, .minw = 7, .minh = 4.2)), t);
}

/* ------------------------------------------------------------------ */
/* high scores / progress                                              */
/* ------------------------------------------------------------------ */
UIDef *create_UIBox_high_scores(void) {
    fetch_achievements();
    set_profile_progress();
    set_discover_tallies();
    UIDef *scores = uinodes();
    static const char *rows[] = { "hand", "furthest_round", "furthest_ante", "poker_hand", "most_money", "win_streak" };
    for (size_t i = 0; i < sizeof rows / sizeof *rows; i++) uidef_add(scores, create_UIBox_high_scores_row(rows[i]));
    G.focused_profile = G.SETTINGS.profile;
    UIDef *contents = uinodes();
    uidef_add(contents, UIC(CFG(.align = "cm", .minw = 3, .padding = 0.2, .r = 0.1, .colour = G.C.CLEAR),
        uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.1)), scores)));
    uidef_add(contents, UIC(CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.CLEAR),
        create_progress_box(NULL, false),
        UIBUTTON(.button = G_FUNCS_usage, .label = { localize_key("k_card_stats") }, .nlabel = 1, .minw = 7.5, .minh = 1, .focus_args = FOCUS(.nav = "wide"))));
    if (!G.F_NO_ACHIEVEMENTS) uidef_add(contents, UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.CLEAR)));
    return GENERIC_OPTIONS(.back_func = G_FUNCS_options, .snap_back = true, .contents = contents);
}

typedef struct { double tally, of; } TallyOf;
typedef struct { TallyOf discovered, deck_stakes, joker_stickers, challenges; double overall_tally, overall_of; } ProgressCopy;

static TallyOf tally_of(DVal *p, const char *k) {
    DVal *t = dv_get(p, k);
    TallyOf r = { dv_get_num(t, "tally", 0), dv_get_num(t, "of", 1) };
    if (r.of == 0) r.of = 1;
    return r;
}

UIDef *create_progress_box(DVal *profile_progress, bool smaller) {
    if (!profile_progress) profile_progress = dv_get_or_create_table(profile(), "progress");
    ProgressCopy *p = NEW(ProgressCopy);
    p->discovered = tally_of(profile_progress, "discovered");
    p->deck_stakes = tally_of(profile_progress, "deck_stakes");
    p->joker_stickers = tally_of(profile_progress, "joker_stickers");
    p->challenges = tally_of(profile_progress, "challenges");
    p->overall_tally = p->discovered.tally / p->discovered.of + p->deck_stakes.tally / p->deck_stakes.of + p->joker_stickers.tally / p->joker_stickers.of +
                       p->challenges.tally / p->challenges.of;
    p->overall_of = 4;
    dv_set_num(profile_progress, "overall_tally", p->overall_tally);
    dv_set_num(profile_progress, "overall_of", p->overall_of);
    double text_scale = smaller ? 0.7 : 1;
    DVal *fp = &G.PROFILES[G.focused_profile >= 1 && G.focused_profile <= 3 ? G.focused_profile - 1 : 0];
    Col bar_colour = dv_get_bool(fp, "all_unlocked", false) ? G.C.RED : G.C.BLUE;
    static const char *protos[4] = { "collection", "challenges", "joker_stickers", "deck_stake_wins" };
    UIDef *rows = uinodes();
    for (int i = 0; i < 4; i++) {
        const char *v = protos[i];
        TallyOf *tab = !strcmp(v, "collection") ? &p->discovered : !strcmp(v, "deck_stake_wins") ? &p->deck_stakes :
                       !strcmp(v, "joker_stickers") ? &p->joker_stickers : &p->challenges;
        ProgressBar *pb = NEW(ProgressBar);
        pb->max = tab->of; pb->ref = REFN(&tab->tally); pb->empty_col = G.C.BLACK; pb->filled_col = adjust_alpha(bar_colour, 0.5);
        uidef_add(rows, UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = darken(G.C.JOKER_GREY, 0.1), .emboss = 0.05),
            UIC(CFG(.align = "cm", .padding = 0.05, .minw = 3.5 * text_scale, .maxw = 3.5 * text_scale),
                UIT(CFG(.text = LOC(tfmt("k_%s", v)), .scale = 0.5 * text_scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIC(CFG(.align = "cl", .minh = smaller ? 0.5 : 0.8, .r = 0.1, .minw = 3.5 * text_scale, .colour = G.C.BLACK, .emboss = 0.05, .progress_bar = pb),
                UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .minw = 3.5 * text_scale),
                    UIO(CFG(.object = &dt_simple(tfmt("%d%%", (int)floor(0.01 + 100 * tab->tally / tab->of)), G.C.WHITE, 0.55 * text_scale)->n)),
                    UIT(CFG(.text = (char *)tfmt(" (%s/%s)", num_str(tab->tally), num_str(tab->of)), .scale = 0.35, .colour = G.C.JOKER_GREY))))));
    }
    ProgressBar *pb = NEW(ProgressBar);
    pb->max = p->overall_of; pb->ref = REFN(&p->overall_tally); pb->empty_col = G.C.BLACK; pb->filled_col = adjust_alpha(bar_colour, 0.5);
    return UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05),
        UIR(CFG(.align = "cm", .padding = 0.05),
            UIC(CFG(.align = "cm", .padding = 0.05, .minw = 3.5 * text_scale, .maxw = 3.5 * text_scale),
                UIT(CFG(.text = LOC("k_progress"), .scale = 0.5, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIC(CFG(.align = "cl", .minh = 0.6, .r = 0.1, .minw = 3.5 * text_scale, .colour = G.C.BLACK, .emboss = 0.05, .progress_bar = pb),
                UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .minw = 3.5 * text_scale),
                    UIO(CFG(.object = &dt_simple(tfmt("%d%%", (int)floor(0.01 + 100 * p->overall_tally / p->overall_of)), G.C.WHITE, 0.55)->n))))),
        uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.05)), rows));
}

UIDef *create_UIBox_high_scores_row(const char *score) {
    DVal *hs = dv_get(dv_get(profile(), "high_scores"), score);
    if (!hs) return NULL;
    const char *label = dv_get_str(hs, "label", "");
    double label_scale = 0.65 - 0.025 * MAX((double)strlen(label) - 8, 0);
    double label_w = 3.5, score_w = 4, h = 0.8;
    double amt = dv_get_num(hs, "amt", 0);
    UIDef *score_tab = uinodes();
    if (!strcmp(score, "poker_hand")) {
        const char *handname = localize_key("k_none"); double amount = 0;
        DVal *hu = dv_get(profile(), "hand_usage");
        for (int i = 0; i < dv_count(hu); i++) {
            DVal *v = dv_val(hu, i);
            double c = dv_get_num(v, "count", 0);
            if (c > amount) { handname = dv_get_str(v, "order", handname); amount = c; }
        }
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(amount < 1 ? handname : localize_cat(handname, "poker_hands"), G.C.WHITE, 0.55)->n)));
        uidef_add(score_tab, UIT(CFG(.text = (char *)tfmt(" (%s)", num_str(amount)), .scale = 0.45, .colour = G.C.JOKER_GREY)));
    } else if (!strcmp(score, "most_money")) {
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(tfmt("%s%s", localize_key("$"), number_format(amt)), G.C.MONEY, score_number_scale(0.85, amt))->n)));
    } else if (!strcmp(score, "win_streak")) {
        double cs = dv_get_num(dv_get(dv_get(profile(), "high_scores"), "current_streak"), "amt", 0);
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(number_format(amt), G.C.WHITE, score_number_scale(0.85, amt))->n)));
        uidef_add(score_tab, UIT(CFG(.text = (char *)tfmt(" (%s)", num_str(cs)), .scale = 0.45, .colour = G.C.JOKER_GREY)));
    } else if (!strcmp(score, "hand")) {
        Sprite *chip = ui_sprite(0.4, 0.4, 0, 0);
        chip->n.states.drag.can = false;
        uidef_add(score_tab, UIC(CFG(.align = "cm"), UIO(CFG(.w = 0.4, .h = 0.4, .object = &chip->n))));
        uidef_add(score_tab, UIC(CFG(.align = "cm"), UIO(CFG(.object = &dt_simple(number_format(amt), G.C.RED, MIN(0.75, score_number_scale(1.5, amt)))->n))));
    } else if (!strcmp(score, "collection")) {
        double tot = dv_get_num(hs, "tot", 1);
        uidef_add(score_tab, UIC(CFG(.align = "cm"),
            UIO(CFG(.object = &dt_simple(tfmt("%%%d", (int)floor(0.01 + 100 * amt / tot)), G.C.WHITE, MIN(0.75, score_number_scale(1.5, amt)))->n)),
            UIT(CFG(.text = (char *)tfmt(" (%s/%s)", num_str(amt), num_str(tot)), .scale = 0.45, .colour = G.C.JOKER_GREY))));
    } else {
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(number_format(amt), G.C.FILTER, score_number_scale(0.85, amt))->n)));
    }
    return UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = darken(G.C.JOKER_GREY, 0.1), .emboss = 0.05),
        UIC(CFG(.align = "cm", .padding = 0.05, .minw = label_w, .maxw = label_w),
            UIT(CFG(.text = (char *)localize_cat(score, "high_scores"), .scale = label_scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
        UIC(CFG(.align = "cl", .minh = h, .r = 0.1, .minw = score_w, .colour = G.C.BLACK, .emboss = 0.05),
            uidef_add_all(UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .minw = score_w, .maxw = score_w)), score_tab)));
}

/* ------------------------------------------------------------------ */
/* win / game over                                                     */
/* ------------------------------------------------------------------ */
static ScoreEntry *round_score(const char *name) {
#define RS(n) if (!strcmp(name, #n)) return &G.GAME.round_scores.n
    RS(furthest_ante); RS(furthest_round); RS(hand); RS(poker_hand); RS(new_collection); RS(cards_played); RS(cards_discarded);
    RS(times_rerolled); RS(cards_purchased);
#undef RS
    return NULL;
}

static UIDef *wrap_jimbo(UIDef *t, bool row) {
    /* t.nodes[1] = {R, {C padding 2 {jimbo_spot}}, {C {old}}} */
    if (t->count < 1) return t;
    UIDef *old = t->nodes[0];
    UIDef *spot = UIO(CFG(.padding = 0, .id = "jimbo_spot", .object = moveable_new(0, 0, G.CARD_W * 1.1, G.CARD_H * 1.1)));
    UIDef *left = row ? UIC(CFG(.align = "cm", .padding = 2), UIR(CFG(.align = "cm"), spot)) : UIC(CFG(.align = "cm", .padding = 2), spot);
    t->nodes[0] = UIR(CFG(.align = "cm", .padding = 0.1), left, UIC(CFG(.align = "cm", .padding = 0.1), old));
    return t;
}

UIDef *create_UIBox_win(void) {
    bool show_win_cta = false;
    Col eased_green = col_copy(G.C.GREEN);
    eased_green[3] = 0;
    ease_value(REFF(&eased_green[3]), 0.5, false, -1, true, NAN, EASE_LERP);
    UIDef *contents = uinodes();
    uidef_add(contents, UIR(CFG(.align = "cm"),
        UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("ph_you_win")), DT_COL1(G.C.EDITION), .shadow = true, .float_ = true, .spacing = 10, .rotate = true,
                                    .scale = 1.5, .pop_in = 0.4, .has_pop_in = true, .maxw = 6.5)->n))));
    uidef_add(contents, UIR(CFG(.align = "cm", .padding = 0.15),
        UIC(CFG(.align = "cm"),
            UIR(CFG(.align = "cm", .padding = 0.08), create_UIBox_round_scores_row("hand", NULL), create_UIBox_round_scores_row("poker_hand", NULL)),
            UIR(CFG(.align = "cm"),
                UIC(CFG(.align = "cm", .padding = 0.08),
                    create_UIBox_round_scores_row("cards_played", G.C.BLUE),
                    create_UIBox_round_scores_row("cards_discarded", G.C.RED),
                    create_UIBox_round_scores_row("cards_purchased", G.C.MONEY),
                    create_UIBox_round_scores_row("times_rerolled", G.C.GREEN),
                    create_UIBox_round_scores_row("new_collection", G.C.WHITE),
                    create_UIBox_round_scores_row("seed", G.C.WHITE),
                    UIBUTTON(.button = G_FUNCS_copy_seed, .label = { localize_key("b_copy") }, .nlabel = 1, .colour = G.C.BLUE, .scale = 0.3, .minw = 2.3, .minh = 0.4)),
                UIC(CFG(.align = "tr", .padding = 0.08),
                    create_UIBox_round_scores_row("furthest_ante", G.C.FILTER),
                    create_UIBox_round_scores_row("furthest_round", G.C.FILTER),
                    UIR(CFG(.align = "cm", .minh = 0.4, .minw = 0.1)),
                    UIIF(!show_win_cta, UIBUTTON(.id = "from_game_won", .button = G_FUNCS_notify_then_setup_run, .label = { localize_key("b_start_new_run") }, .nlabel = 1,
                                                 .minw = 2.5, .maxw = 2.5, .minh = 1, .focus_args = FOCUS(.nav = "wide", .snap_to = true))),
                    UIIF(!show_win_cta, UIR(CFG(.align = "cm", .minh = 0.2, .minw = 0.1))),
                    UIIF(!show_win_cta, UIBUTTON(.button = G_FUNCS_go_to_menu, .label = { localize_key("b_main_menu") }, .nlabel = 1, .minw = 2.5, .maxw = 2.5,
                                                 .minh = 1, .focus_args = FOCUS(.nav = "wide"))))),
            UIR(CFG(.align = "cm", .padding = 0.08),
                UIBUTTON(.button = G_FUNCS_exit_overlay_menu, .label = { localize_key("b_endless") }, .nlabel = 1, .minw = 6.5, .maxw = 5, .minh = 1.2, .scale = 0.7,
                         .shadow = true, .colour = G.C.BLUE, .focus_args = FOCUS(.nav = "wide", .button = "x", .set_button_pip = true))))));
    UIDef *t = GENERIC_OPTIONS(.padding = 0, .has_padding = true, .bg_colour = eased_green, .colour = G.C.BLACK, .outline_colour = G.C.EDITION, .no_back = true,
                               .contents = contents);
    t->config->id = "you_win_UI";
    return wrap_jimbo(t, false);
}

UIDef *create_UIBox_game_over(void) {
    Col eased_red = col_copy(G.GAME.round_resets.ante <= G.GAME.win_ante ? G.C.RED : G.C.BLUE);
    eased_red[3] = 0;
    ease_value(REFF(&eased_red[3]), 0.8, false, -1, true, NAN, EASE_LERP);
    UIDef *contents = uinodes();
    uidef_add(contents, UIR(CFG(.align = "cm"),
        UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("ph_game_over")), DT_COL1(G.C.RED), .shadow = true, .float_ = true, .scale = 1.5, .pop_in = 0.4,
                                    .has_pop_in = true, .maxw = 6.5)->n))));
    uidef_add(contents, UIR(CFG(.align = "cm", .padding = 0.15),
        UIC(CFG(.align = "cm"),
            UIR(CFG(.align = "cm", .padding = 0.05, .colour = G.C.BLACK, .emboss = 0.05, .r = 0.1),
                UIR(CFG(.align = "cm", .padding = 0.08), create_UIBox_round_scores_row("hand", NULL), create_UIBox_round_scores_row("poker_hand", NULL)),
                UIR(CFG(.align = "cm"),
                    UIC(CFG(.align = "cm", .padding = 0.08),
                        create_UIBox_round_scores_row("cards_played", G.C.BLUE),
                        create_UIBox_round_scores_row("cards_discarded", G.C.RED),
                        create_UIBox_round_scores_row("cards_purchased", G.C.MONEY),
                        create_UIBox_round_scores_row("times_rerolled", G.C.GREEN),
                        create_UIBox_round_scores_row("new_collection", G.C.WHITE),
                        create_UIBox_round_scores_row("seed", G.C.WHITE),
                        UIBUTTON(.button = G_FUNCS_copy_seed, .label = { localize_key("b_copy") }, .nlabel = 1, .colour = G.C.BLUE, .scale = 0.3, .minw = 2.3,
                                 .minh = 0.4, .focus_args = FOCUS(.nav = "wide"))),
                    UIC(CFG(.align = "tr", .padding = 0.08),
                        create_UIBox_round_scores_row("furthest_ante", G.C.FILTER),
                        create_UIBox_round_scores_row("furthest_round", G.C.FILTER),
                        create_UIBox_round_scores_row("defeated_by", NULL)))),
            UIR(CFG(.align = "cm", .padding = 0.1),
                UIR(CFG(.id = "from_game_over", .align = "cm", .minw = 5, .padding = 0.1, .r = 0.1, .hover = true, .colour = G.C.RED,
                        .button = G_FUNCS_notify_then_setup_run, .shadow = true, .focus_args = FOCUS(.nav = "wide", .snap_to = true)),
                    UIR(CFG(.align = "cm", .padding = 0, .no_fill = true, .maxw = 4.8),
                        UIT(CFG(.text = LOC("b_start_new_run"), .scale = 0.5, .colour = G.C.UI.TEXT_LIGHT)))),
                UIR(CFG(.align = "cm", .minw = 5, .padding = 0.1, .r = 0.1, .hover = true, .colour = G.C.RED, .button = G_FUNCS_go_to_menu, .shadow = true,
                        .focus_args = FOCUS(.nav = "wide")),
                    UIR(CFG(.align = "cm", .padding = 0, .no_fill = true, .maxw = 4.8),
                        UIT(CFG(.text = LOC("b_main_menu"), .scale = 0.5, .colour = G.C.UI.TEXT_LIGHT))))))));
    UIDef *t = GENERIC_OPTIONS(.bg_colour = eased_red, .no_back = true, .padding = 0, .has_padding = true, .contents = contents);
    return wrap_jimbo(t, true);
}

UIDef *create_UIBox_round_scores_row(const char *score, Col text_colour) {
    ScoreEntry *rs = round_score(score);
    const char *label = rs ? localize_key(tfmt("ph_score_%s", score)) : "";
    bool check_high_score = false;
    bool big = !strcmp(score, "hand") || !strcmp(score, "poker_hand");
    double label_w = big ? 3.5 : 2.9, score_w = big ? 3.5 : 1, h = 0.5;
    UIDef *score_tab = uinodes();
    if (!strcmp(score, "furthest_ante")) {
        label_w = 1.9; check_high_score = true; label = localize_key("k_ante");
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(number_format(G.GAME.round_resets.ante), text_colour ? text_colour : G.C.FILTER, 0.45)->n)));
    }
    if (!strcmp(score, "furthest_round")) {
        label_w = 1.9; check_high_score = true; label = localize_key("k_round");
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(number_format(G.GAME.round), text_colour ? text_colour : G.C.FILTER, 0.45)->n)));
    }
    if (!strcmp(score, "seed")) {
        label_w = 1.9; score_w = 1.9; label = localize_key("k_seed");
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(G.GAME.pseudorandom.seed ? G.GAME.pseudorandom.seed : "", text_colour ? text_colour : G.C.WHITE, 0.45)->n)));
    }
    if (!strcmp(score, "defeated_by")) {
        label = localize_key("k_defeated_by");
        BlindProto *bp = (G.GAME.blind && G.GAME.blind->config.blind) ? G.GAME.blind->config.blind : P_BLIND("bl_small");
        Sprite *anim = animated_sprite_new(0, 0, 1.4, 1.4, strmap_get(&G.ANIMATION_ATLAS, "blind_chips"), bp->pos);
        DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
        Sprite_define_draw_steps(anim, steps, 2);
        uidef_add(score_tab, UIR(CFG(.align = "cm", .minh = 0.6),
            UIO(CFG(.object = &DYNATEXT(DT_STR(localize_name_text("Blind", bp->key)), DT_COL1(G.C.WHITE), .shadow = true, .float_ = true, .maxw = 2.2,
                                        .scale = 0.45)->n))));
        uidef_add(score_tab, UIR(CFG(.align = "cm", .padding = 0.1), UIO(CFG(.object = &anim->n))));
    }
    double label_scale = 0.5;
    if (!strcmp(score, "poker_hand")) {
        const char *handname = localize_key("k_none"); double amount = 0;
        for (int i = 0; i < G.GAME.hand_usage.cap; i++) {
            if (!STRMAP_LIVE(&G.GAME.hand_usage, i)) continue;
            UsageEntry *u = G.GAME.hand_usage.vals[i];
            if (u->count > amount) { handname = u->order_str; amount = u->count; }
        }
        uidef_add(score_tab, UIO(CFG(.object = &DYNATEXT(DT_STR(amount < 1 ? handname : localize_cat(handname, "poker_hands")), DT_COL1(text_colour ? text_colour : G.C.WHITE),
                                                         .shadow = true, .float_ = true, .scale = 0.45, .maxw = 2.5)->n)));
        uidef_add(score_tab, UIT(CFG(.text = (char *)tfmt(" (%s)", num_str(amount)), .scale = 0.35, .colour = G.C.JOKER_GREY)));
    } else if (!strcmp(score, "hand")) {
        check_high_score = true;
        Sprite *chip = ui_sprite(0.3, 0.3, 0, 0);
        chip->n.states.drag.can = false;
        double amt = rs ? rs->amt : 0;
        uidef_add(score_tab, UIC(CFG(.align = "cm"), UIO(CFG(.w = 0.3, .h = 0.3, .object = &chip->n))));
        uidef_add(score_tab, UIC(CFG(.align = "cm"),
            UIO(CFG(.object = &dt_simple(number_format(amt), text_colour ? text_colour : G.C.RED, MIN(0.6, score_number_scale(1.2, amt)))->n))));
    } else if (rs && score_tab->count == 0) {
        uidef_add(score_tab, UIO(CFG(.object = &dt_simple(number_format(rs->amt), text_colour ? text_colour : G.C.FILTER, score_number_scale(0.6, rs->amt))->n)));
    }
    bool db = !strcmp(score, "defeated_by");
    return UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = darken(G.C.JOKER_GREY, 0.1), .emboss = 0.05,
                   .func = check_high_score ? G_FUNCS_high_score_alert : NULL, .id = intern(score)),
        UIN(db ? UIT_R : UIT_C, CFG(.align = "cm", .padding = 0.02, .minw = label_w, .maxw = label_w),
            UIT(CFG(.text = (char *)label, .scale = label_scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
        UIN(db ? UIT_R : UIT_C, CFG(.align = "cr"),
            UIC(CFG(.align = "cm", .minh = h, .r = 0.1, .minw = db ? label_w : score_w, .colour = (!strcmp(score, "seed") && G.GAME.seeded) ? G.C.RED : G.C.BLACK,
                    .emboss = 0.05),
                uidef_add_all(UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .minw = score_w)), score_tab))));
}

/* ------------------------------------------------------------------ */
/* current hands                                                       */
/* ------------------------------------------------------------------ */
static UIDef *hand_tip_filler(void *arg) { return create_UIBox_hand_tip((const char *)arg); }

UIDef *create_UIBox_hand_tip(const char *handname) {
    int hi = hand_index(handname);
    if (hi < 0) return UIR(CFG(.align = "cm"));
    HandStat *h = &G.GAME.hands[hi];
    CardArea *ca = cardarea_new(2, 2, 3.5 * G.CARD_W, 0.75 * G.CARD_H,
                                CA_ARGS(.card_limit = 5, .has_card_limit = true, .type = "title", .highlight_limit = 0, .has_highlight_limit = true));
    for (int k = 0; k < 5 && h->example[k].card; k++) {
        Card *card = card_new(0, 0, 0.5 * G.CARD_W, 0.5 * G.CARD_H, P_CARD(h->example[k].card), P_CENTER("c_base"), NULL);
        if (h->example[k].scoring) node_juice_up(&card->n, 0.3, 0.2);
        if (k == 0) sound_play("paper1", 0.95 + math_random() * 0.1, 0.3);
        ease_value(REFN(&card->n.T->scale), h->example[k].scoring ? 0.25 : -0.15, false, TIMER_REAL, true, 0.2, EASE_LERP);
        CardArea_emplace(ca, card, NULL, false);
    }
    return UIR(CFG(.align = "cm", .colour = G.C.WHITE, .r = 0.1), UIC(CFG(.align = "cm"), UIO(CFG(.object = &ca->n))));
}

static Tooltip *hand_tooltip(const char *handname) {
    Tooltip *tt = NEW(Tooltip);
    tt->ntext = localize_list(handname, "poker_hand_descriptions", tt->text, 6);
    tt->filler = true;
    tt->filler_fn = hand_tip_filler;
    tt->filler_args = (void *)intern(handname);
    return tt;
}

UIDef *create_UIBox_current_hand_row(const char *handname, bool simple) {
    int hi = hand_index(handname);
    if (hi < 0 || !G.GAME.hands[hi].visible) return NULL;
    HandStat *h = &G.GAME.hands[hi];
    if (!simple) {
        int lvl = (int)MIN(7, h->level);
        if (lvl < 1) lvl = 1;
        return UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = darken(G.C.JOKER_GREY, 0.1), .emboss = 0.05, .hover = true, .force_focus = true,
                       .on_demand_tooltip = hand_tooltip(handname)),
            UIC(CFG(.align = "cl", .padding = 0, .minw = 5),
                UIC(CFG(.align = "cm", .padding = 0.01, .r = 0.1, .colour = G.C.HAND_LEVELS[lvl], .minw = 1.5, .outline = 0.8, .outline_colour = G.C.WHITE),
                    UIT(CFG(.text = (char *)tfmt("%s%s", localize_key("k_level_prefix"), num_str(h->level)), .scale = 0.5, .colour = G.C.UI.TEXT_DARK))),
                UIC(CFG(.align = "cm", .minw = 4.5, .maxw = 4.5),
                    UIT(CFG(.text = (char *)tfmt(" %s", localize_cat(handname, "poker_hands")), .scale = 0.45, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)))),
            UIC(CFG(.align = "cm", .padding = 0.05, .colour = G.C.BLACK, .r = 0.1),
                UIC(CFG(.align = "cr", .padding = 0.01, .r = 0.1, .colour = G.C.CHIPS, .minw = 1.1),
                    UIT(CFG(.text = (char *)num_str(h->chips), .scale = 0.45, .colour = G.C.UI.TEXT_LIGHT)),
                    UIB(CFG(.w = 0.08, .h = 0.01))),
                UIT(CFG(.text = "X", .scale = 0.45, .colour = G.C.MULT)),
                UIC(CFG(.align = "cl", .padding = 0.01, .r = 0.1, .colour = G.C.MULT, .minw = 1.1),
                    UIB(CFG(.w = 0.08, .h = 0.01)),
                    UIT(CFG(.text = (char *)num_str(h->mult), .scale = 0.45, .colour = G.C.UI.TEXT_LIGHT)))),
            UIC(CFG(.align = "cm"), UIT(CFG(.text = "  #", .scale = 0.45, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIC(CFG(.align = "cm", .padding = 0.05, .colour = G.C.L_BLACK, .r = 0.1, .minw = 0.9),
                UIT(CFG(.text = (char *)num_str(h->played), .scale = 0.45, .colour = G.C.FILTER, .shadow = true))));
    }
    return UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = darken(G.C.JOKER_GREY, 0.1), .force_focus = true, .emboss = 0.05, .hover = true,
                   .on_demand_tooltip = hand_tooltip(handname), .focus_args = FOCUS(.snap_to = simple && !strcmp(handname, "Straight Flush"))),
        UIC(CFG(.align = "cm", .padding = 0, .minw = 5),
            UIT(CFG(.text = (char *)localize_cat(handname, "poker_hands"), .scale = 0.5, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))));
}

UIDef *create_UIBox_current_hands(bool simple) {
    static const char *order[12] = { "Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House",
                                     "Flush", "Straight", "Three of a Kind", "Two Pair", "Pair", "High Card" };
    UIDef *hands = UIR(CFG(.align = "cm", .padding = 0.04));
    for (int i = 0; i < 12; i++) uidef_add(hands, create_UIBox_current_hand_row(order[i], simple));
    return UIROOT(CFG(.align = "cm", .minw = 3, .padding = 0.1, .r = 0.1, .colour = G.C.CLEAR), hands);
}

/* ------------------------------------------------------------------ */
/* deck info / run info                                                */
/* ------------------------------------------------------------------ */
static UIDef *view_deck_fn(void *arg) { return UIDEF_view_deck(arg != NULL); }
static UIDef *current_hands_fn(void *arg) { (void)arg; return create_UIBox_current_hands(false); }
static UIDef *current_blinds_fn(void *arg) { (void)arg; return UIDEF_current_blinds(); }
static UIDef *used_vouchers_fn(void *arg) { (void)arg; return UIDEF_used_vouchers(); }
static UIDef *current_stake_fn(void *arg) { (void)arg; return UIDEF_current_stake(); }

UIDef *UIDEF_deck_info(bool show_remaining) {
    TabsArgs ta = { .tab_h = 8, .snap_to_nav = true };
    if (show_remaining) {
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_remaining"), .chosen = true, .tab_definition_function = view_deck_fn, .tab_definition_function_args = (void *)1 };
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_full_deck"), .tab_definition_function = view_deck_fn };
    } else {
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_full_deck"), .chosen = true, .tab_definition_function = view_deck_fn };
    }
    return GENERIC_OPTIONS(.contents = uidef_add(uinodes(), create_tabs(ta)));
}

UIDef *UIDEF_run_info(void) {
    TabsArgs ta = { .tab_h = 8, .snap_to_nav = true };
    ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_poker_hands"), .chosen = true, .tab_definition_function = current_hands_fn };
    ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_blinds"), .tab_definition_function = current_blinds_fn };
    ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_vouchers"), .tab_definition_function = used_vouchers_fn };
    if (G.GAME.stake > 1) ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_stake"), .tab_definition_function = current_stake_fn };
    return GENERIC_OPTIONS(.contents = uidef_add(uinodes(), create_tabs(ta)));
}

UIDef *UIDEF_current_blinds(void) {
    UIDef *t = UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR, .padding = 0.2));
    for (int i = 0; i < 3; i++) {
        if (str_eq(G.GAME.round_resets.blind_states[i], "Hide")) continue;
        uidef_add(t, UIC(CFG(.align = "tm", .padding = 0.1, .outline = 2, .r = 0.1, .line_emboss = 0.2, .outline_colour = G.C.BLACK),
            create_UIBox_blind_choice(BLIND_TYPE_NAMES[i], true)));
    }
    return t;
}

UIDef *UIDEF_deck_stake_column(const char *deck_key) {
    DVal *du = dv_get(dv_get(profile(), "deck_usage"), deck_key);
    DVal *wins = dv_get(du, "wins");
    bool all_unlocked = dv_get_bool(profile(), "all_unlocked", false);
    UIDef *col = UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR));
    bool valid = false;
    for (int i = G.P_CENTER_POOLS.Stake.count; i >= 1; i--) {
        double w = dv_num_or(dv_geti(wins, i), 0);
        if ((du && dv_geti(wins, i - 1) && dv_geti(wins, i - 1)->t != DV_NIL) || i == 1 || all_unlocked) valid = true;
        UIConfig *c = CFG(.id = intern(tfmt("%d", i)), .align = "cm", .colour = w > 0 ? G.C.GREY : G.C.CLEAR, .outline = 0, .outline_colour = G.C.WHITE, .r = 0.1,
                          .minh = 0.25, .minw = valid ? 0.45 : 0.25, .func = G_FUNCS_RUN_SETUP_check_back_stake_highlight);
        uidef_add(col, UIR(c, UIR(CFG(.align = "cm", .minh = valid ? 0.17 : 0.13, .minw = valid ? 0.37 : 0.13,
                                      .colour = w > 0 ? get_stake_col(i) : G.C.UI.TRANSPARENT_LIGHT, .r = 0.1))));
        if (i > 1) uidef_add(col, UIR(CFG(.align = "cm", .minh = 0.1, .minw = 0.04)));
    }
    return col;
}

static UIDef *stake_desc_rows(StakeProto *sc) {
    UIDef *sd = uinodes();
    localize_ui((LocArgs){ .type = "descriptions", .key = sc->key, .set = sc->set, .nodes = sd });
    UIDef *full = uinodes();
    for (int k = 0; k < sd->count - 1; k++) uidef_add(full, uidef_add_all(UIR(CFG(.align = "cm")), sd->nodes[k]));
    return full;
}

UIDef *UIDEF_current_stake(void) {
    UIDef *other_col = NULL;
    int stake = (int)G.GAME.stake;
    if (stake > 2) {
        UIDef *rows = uinodes();
        uidef_add(rows, UIR(CFG(.align = "cm", .padding = 0.05), UIT(CFG(.text = LOC("k_also_applied"), .scale = 0.4, .colour = G.C.WHITE))));
        for (int i = stake - 1; i >= 2; i--) {
            StakeProto *sc = G.P_CENTER_POOLS.Stake.items[i - 1];
            uidef_add(rows, UIR(CFG(.align = "cm"),
                UIC(CFG(.align = "cm"), UIC(CFG(.align = "cm", .colour = get_stake_col(i), .r = 0.1, .minh = 0.35, .minw = 0.35, .emboss = 0.05)), UIB(CFG(.w = 0.1, .h = 0.1))),
                uidef_add_all(UIC(CFG(.align = "cm", .padding = 0.03, .colour = G.C.WHITE, .r = 0.1, .minh = 0.7, .minw = 4.8)), stake_desc_rows(sc))));
        }
        other_col = uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = G.C.L_BLACK)), rows);
    }
    Sprite *stake_sprite = get_stake_sprite(stake, 0.8);
    StakeProto *sc = G.P_CENTER_POOLS.Stake.items[stake - 1];
    UIDef *current = UIR(CFG(.align = "cm"),
        UIR(CFG(.align = "cm", .padding = 0.1, .minw = 4),
            UIO(CFG(.colour = G.C.BLUE, .object = &stake_sprite->n, .hover = true, .can_collide = -1)),
            UIT(CFG(.text = (char *)localize_name_text("Stake", sc->key), .scale = 0.45, .colour = G.C.WHITE))),
        UIR(CFG(.align = "cm", .padding = 0.05, .colour = get_stake_col(stake), .r = 0.05),
            UIR(CFG(.align = "cm", .padding = 0.05, .colour = G.C.WHITE, .r = 0.05),
                uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.03, .minh = 0.7, .minw = 3.8)), stake_desc_rows(sc)))));
    return UIROOT(CFG(.align = "cm", .colour = G.C.BLACK, .r = 0.1, .padding = 0.1), current, other_col ? other_col : UISKIP);
}

/* ------------------------------------------------------------------ */
/* view deck                                                           */
/* ------------------------------------------------------------------ */
static int cmp_suit_nominal_desc(const void *a, const void *b) {
    Card *x = *(Card *const *)a, *y = *(Card *const *)b;
    double nx = Card_get_nominal(x, "suit"), ny = Card_get_nominal(y, "suit");
    return nx > ny ? -1 : nx < ny ? 1 : 0;
}

static DynaTextPart *two_parts(const char *a, Col ca, const char *b, Col cb) {
    DynaTextPart *p = xcalloc(2, sizeof(DynaTextPart));
    p[0].string = xstrdup(a); p[0].colour = ca;
    p[1].string = xstrdup(b); p[1].colour = cb;
    return p;
}

static UIDef *tally_sprite2(SpritePos pos, const char *a, const char *b, Col flip_col, const char *tip) {
    Col text_colour = G.C.BLACK;
    bool same = !strcmp(a, b);
    if (same) text_colour = flip_col ? flip_col : G.C.WHITE;
    Sprite *t_s = ui_sprite(0.5, 0.5, pos.x, pos.y);
    t_s->n.states.drag.can = false;
    t_s->n.states.hover.can = false;
    t_s->n.states.collide.can = false;
    Tooltip *tt = NEW(Tooltip);
    tt->text[0] = tip; tt->ntext = 1;
    return UIC(CFG(.align = "cm", .padding = 0.07, .force_focus = true, .focus_args = FOCUS(.type = "tally_sprite"), .tooltip = tt),
        UIR(CFG(.align = "cm", .r = 0.1, .padding = 0.04, .emboss = 0.05, .colour = G.C.JOKER_GREY),
            UIO(CFG(.w = 0.5, .h = 0.5, .can_collide = -1, .object = &t_s->n, .tooltip = tt))),
        UIR(CFG(.align = "cm"),
            !same ? UIO(CFG(.object = &DYNATEXT(.parts = two_parts(a, flip_col, b, G.C.BLUE), .nparts = 2, DT_COL1(G.C.RED), .scale = 0.4, .silent = true,
                                                .shadow = true, .pop_in_rate = 10, .pop_delay = 4, .has_pop_delay = true)->n))
                  : UIT(CFG(.text = (char *)a, .colour = text_colour, .scale = 0.4, .shadow = true))));
}

UIDef *UIDEF_view_deck(bool unplayed_only) {
    UIDef *deck_tables = uinodes();
    G.VIEWING_DECK = true;
    qsort(G.playing_cards.items, (size_t)G.playing_cards.count, sizeof(void *), cmp_suit_nominal_desc);
    static const char *suit_map[4] = { "Spades", "Hearts", "Clubs", "Diamonds" };
    for (int j = 0; j < 4; j++) {
        int n = 0;
        for (int i = 0; i < G.playing_cards.count; i++) if (str_eq(((Card *)G.playing_cards.items[i])->base.suit, suit_map[j])) n++;
        if (!n) continue;
        CardArea *vd = cardarea_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, 6.5 * G.CARD_W, 0.6 * G.CARD_H,
                                    CA_ARGS(.card_limit = n, .has_card_limit = true, .type = "title", .view_deck = true, .highlight_limit = 0,
                                            .has_highlight_limit = true, .card_w = G.CARD_W * 0.7, .draw_layer1 = "card"));
        uidef_add(deck_tables, UIR(CFG(.align = "cm", .padding = 0), UIO(CFG(.object = &vd->n))));
        for (int i = 0; i < G.playing_cards.count; i++) {
            Card *v = G.playing_cards.items[i];
            if (!str_eq(v->base.suit, suit_map[j])) continue;
            bool greyed = unplayed_only && !((v->area && v->area == G.deck) || v->ability.wheel_flipped);
            Card *copy = copy_card(v, NULL, 0.7, -1, false);
            copy->greyed = greyed;
            copy->n.T->x = vd->n.T->x + vd->n.T->w / 2;
            copy->n.T->y = vd->n.T->y;
            node_hard_set_T(&copy->n, NAN, NAN, NAN, NAN);
            CardArea_emplace(vd, copy, NULL, false);
        }
    }
    Col flip_col = G.C.WHITE;
    int suit_t[4] = { 0 }, mod_suit_t[4] = { 0 }, rank_t[13] = { 0 }, mod_rank_t[13] = { 0 };
    static const char *rank_names[13] = { "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
    int face = 0, mod_face = 0, num = 0, mod_num = 0, ace = 0, mod_ace = 0, wheel_flipped = 0;
    for (int i = 0; i < G.playing_cards.count; i++) {
        Card *v = G.playing_cards.items[i];
        if (str_eq(v->ability.name, "Stone Card") || (unplayed_only && !((v->area && v->area == G.deck) || v->ability.wheel_flipped))) continue;
        if (v->ability.wheel_flipped && unplayed_only) wheel_flipped++;
        for (int s = 0; s < 4; s++) {
            if (str_eq(v->base.suit, suit_map[s])) suit_t[s]++;
            if (Card_is_suit(v, suit_map[s], false, false)) mod_suit_t[s]++;
        }
        int id = Card_get_id(v);
        if (id == 11 || id == 12 || id == 13) face++;
        if (Card_is_face(v, false)) mod_face++;
        if (id > 1 && id < 11) { num++; if (!v->debuff) mod_num++; }
        if (id == 14) { ace++; if (!v->debuff) mod_ace++; }
        if (id >= 2 && id <= 14) { rank_t[id - 2]++; if (!v->debuff) mod_rank_t[id - 2]++; }
    }
    bool modded = face != mod_face;
    for (int s = 0; s < 4; s++) if (mod_suit_t[s] != suit_t[s]) modded = true;
    if (wheel_flipped > 0) flip_col = mix_colours(G.C.FILTER, G.C.WHITE, 0.7);
    UIDef *rank_cols = uinodes();
    for (int i = 12; i >= 0; i--) {
        bool delta = mod_rank_t[i] != rank_t[i];
        uidef_add(rank_cols, UIR(CFG(.align = "cm", .padding = 0.07),
            UIC(CFG(.align = "cm", .r = 0.1, .padding = 0.04, .emboss = 0.04, .minw = 0.5, .colour = G.C.L_BLACK),
                UIT(CFG(.text = (char *)rank_names[i], .colour = G.C.JOKER_GREY, .scale = 0.35, .shadow = true))),
            UIC(CFG(.align = "cr", .minw = 0.4),
                delta ? UIO(CFG(.object = &DYNATEXT(.parts = two_parts(tfmt("%d", rank_t[i]), flip_col, tfmt("%d", mod_rank_t[i]), G.C.BLUE), .nparts = 2,
                                                    DT_COL1(G.C.RED), .scale = 0.4, .y_offset = -2, .silent = true, .shadow = true, .pop_in_rate = 10,
                                                    .pop_delay = 4, .has_pop_delay = true)->n))
                      : UIT(CFG(.text = (char *)tfmt("%d", rank_t[i]), .colour = flip_col, .scale = 0.45, .shadow = true)))));
    }
    DynaTextPart *bc = xcalloc(2, sizeof(DynaTextPart));
    bc[0].string = xstrdup(localize_key("k_base_cards")); bc[0].colour = G.C.RED;
    int nbc = 1;
    if (modded) { bc[1].string = xstrdup(localize_key("k_effective")); bc[1].colour = G.C.BLUE; nbc = 2; }
    Back *sb = G.GAME.selected_back;
    UIBox *back_ui = uibox_new(Back_generate_UI(sb, NULL, 0.7, 0.5, G.GAME.challenge), UIBCFG(.has_offset = true, .offset = { 0, 0 }));
    char *wheel_txt = NULL;
    if (wheel_flipped > 0) wheel_txt = localize_variable(wheel_flipped > 1 ? "deck_preview_wheel_plural" : "deck_preview_wheel_singular", LV(tfmt("%d", wheel_flipped)));
#define TS(px, py, a, b, tip) tally_sprite2((SpritePos){ px, py }, tfmt("%d", a), tfmt("%d", b), flip_col, tip)
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIR(CFG(.align = "cm", .padding = 0.05)),
        UIR(CFG(.align = "cm"),
            UIC(CFG(.align = "cm", .minw = 1.5, .minh = 2, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05),
                UIC(CFG(.align = "cm", .padding = 0.1),
                    UIR(CFG(.align = "cm", .r = 0.1, .colour = G.C.L_BLACK, .emboss = 0.05, .padding = 0.15),
                        UIR(CFG(.align = "cm"),
                            UIO(CFG(.object = &DYNATEXT(DT_STR(sb->loc_name), DT_COL1(G.C.WHITE), .bump = true, .rotate = true, .shadow = true,
                                                        .scale = 0.6 - strlen(sb->loc_name ? sb->loc_name : "") * 0.01)->n))),
                        UIR(CFG(.align = "cm", .r = 0.1, .padding = 0.1, .minw = 2.5, .minh = 1.3, .colour = G.C.WHITE, .emboss = 0.05),
                            UIO(CFG(.object = &back_ui->n)))),
                    UIR(CFG(.align = "cm", .r = 0.1, .outline_colour = G.C.L_BLACK, .line_emboss = 0.05, .outline = 1.5),
                        UIR(CFG(.align = "cm", .minh = 0.05, .padding = 0.07),
                            UIO(CFG(.object = &DYNATEXT(.parts = bc, .nparts = nbc, DT_COL1(G.C.RED), .silent = true, .scale = 0.4, .pop_in_rate = 10,
                                                        .pop_delay = 4, .has_pop_delay = true)->n))),
                        UIR(CFG(.align = "cm", .minh = 0.05, .padding = 0.1),
                            TS(1, 0, ace, mod_ace, localize_key("k_aces")),
                            TS(2, 0, face, mod_face, localize_key("k_face_cards")),
                            TS(3, 0, num, mod_num, localize_key("k_numbered_cards"))),
                        UIR(CFG(.align = "cm", .minh = 0.05, .padding = 0.1),
                            TS(3, 1, suit_t[0], mod_suit_t[0], localize_cat("Spades", "suits_plural")),
                            TS(0, 1, suit_t[1], mod_suit_t[1], localize_cat("Hearts", "suits_plural"))),
                        UIR(CFG(.align = "cm", .minh = 0.05, .padding = 0.1),
                            TS(2, 1, suit_t[2], mod_suit_t[2], localize_cat("Clubs", "suits_plural")),
                            TS(1, 1, suit_t[3], mod_suit_t[3], localize_cat("Diamonds", "suits_plural"))))),
                uidef_add_all(UIC(CFG(.align = "cm")), rank_cols),
                UIB(CFG(.w = 0.1, .h = 0.1))),
            UIB(CFG(.w = 0.2, .h = 0.1)),
            uidef_add_all(UIC(CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05)), deck_tables)),
        UIR(CFG(.align = "cm", .minh = 0.8, .padding = 0.05),
            UIIF(modded, UIR(CFG(.align = "cm"),
                UIC(CFG(.padding = 0.3, .r = 0.1, .colour = mix_colours(G.C.BLUE, G.C.WHITE, 0.7))),
                UIT(CFG(.text = (char *)tfmt(" %s", localize_key("ph_deck_preview_effective")), .colour = G.C.WHITE, .scale = 0.3)))),
            UIIF(wheel_flipped > 0, UIR(CFG(.align = "cm"),
                UIC(CFG(.padding = 0.3, .r = 0.1, .colour = flip_col)),
                UIT(CFG(.text = (char *)tfmt(" %s", wheel_txt ? wheel_txt : ""), .colour = G.C.WHITE, .scale = 0.3))))));
#undef TS
}

/* ------------------------------------------------------------------ */
/* used vouchers                                                       */
/* ------------------------------------------------------------------ */
UIDef *UIDEF_used_vouchers(void) {
    bool silent = false;
    int nv = G.P_CENTER_POOLS.Voucher.count;
    int ngroups = (nv + 1) / 2;
    Center ***keys_used = xcalloc((size_t)ngroups + 1, sizeof(Center **));
    int *counts = xcalloc((size_t)ngroups + 1, sizeof(int));
    for (int g = 0; g < ngroups; g++) keys_used[g] = xcalloc(2, sizeof(Center *));
    for (int k = 1; k <= nv; k++) {
        Center *v = G.P_CENTER_POOLS.Voucher.items[k - 1];
        int key = (int)floor((k - 0.1) / 2);
        if (strset_has(&G.GAME.used_vouchers, v->key) && counts[key] < 2) keys_used[key][counts[key]++] = v;
    }
    int area_count = 0;
    for (int g = 0; g < ngroups; g++) if (counts[g]) area_count++;
    int nareas = 0;
    UIDef *voucher_tables = uinodes();
    UIDef *voucher_table_rows = uinodes();
    for (int g = 0; g < ngroups; g++) {
        if (!counts[g]) continue;
        if (nareas == 5 || nareas == 10) {
            uidef_add(voucher_table_rows, uidef_add_all(UIR(CFG(.align = "cm", .padding = 0, .no_fill = true)), voucher_tables));
            voucher_tables = uinodes();
        }
        CardArea *va = cardarea_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, (counts[g] == 1 ? 1 : 1.33) * G.CARD_W,
                                    (area_count >= 10 ? 0.75 : 1.07) * G.CARD_H,
                                    CA_ARGS(.card_limit = 2, .has_card_limit = true, .type = "voucher", .highlight_limit = 0, .has_highlight_limit = true));
        nareas++;
        for (int k = 0; k < counts[g]; k++) {
            Center *vv = keys_used[g][k];
            Card *card = card_new(va->n.T->x + va->n.T->w / 2, va->n.T->y, G.CARD_W, G.CARD_H, NULL, vv,
                                  &(CardParams){ .bypass_discovery_center = true, .bypass_discovery_ui = true, .bypass_lock = true });
            card->ability.order = vv->order;
            card->ability.has_order = true;
            Card_start_materialize(card, NULL, 0, silent, NAN);
            silent = true;
            CardArea_emplace(va, card, NULL, false);
        }
        uidef_add(voucher_tables, UIC(CFG(.align = "cm", .padding = 0, .no_fill = true), UIO(CFG(.object = &va->n))));
    }
    uidef_add(voucher_table_rows, uidef_add_all(UIR(CFG(.align = "cm", .padding = 0, .no_fill = true)), voucher_tables));
    for (int g = 0; g < ngroups; g++) free(keys_used[g]);
    free(keys_used); free(counts);
    if (silent) {
        return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
            UIR(CFG(.align = "cm"), UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("ph_vouchers_redeemed")), DT_COL1(G.C.UI.TEXT_LIGHT), .bump = true, .scale = 0.6)->n))),
            UIR(CFG(.align = "cm", .minh = 0.5)),
            UIR(CFG(.align = "cm", .colour = G.C.BLACK, .r = 1, .padding = 0.15, .emboss = 0.05),
                uidef_add_all(UIR(CFG(.align = "cm")), voucher_table_rows)));
    }
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("ph_no_vouchers")), DT_COL1(G.C.UI.TEXT_LIGHT), .bump = true, .scale = 0.6)->n)));
}
