/* ui_setup.c - run setup, challenges, profiles, main menu and language
 * selector from UI_definitions.lua, plus their callbacks. */
#include "game.h"

#define LOC(k) ((char *)localize_key(k))
#define EVFN(name) static bool name(Event *e_, void *p_)

static DVal *prof(int p) { return &G.PROFILES[(p >= 1 && p <= 3 ? p : 1) - 1]; }

/* profile names are edited through a text input bound to a char*; they are
 * mirrored into the profile DVal on change. */
static char *profile_names[3];
char **profile_name_ref(int p) {
    if (p < 1 || p > 3) p = 1;
    if (!profile_names[p - 1]) profile_names[p - 1] = xstrdup(dv_get_str(prof(p), "name", ""));
    return &profile_names[p - 1];
}
void profile_name_sync(int p) {
    char **r = profile_name_ref(p);
    dv_set_str(prof(p), "name", *r ? *r : "");
}

/* ------------------------------------------------------------------ */
/* notify alert                                                        */
/* ------------------------------------------------------------------ */
UIDef *create_UIBox_notify_alert(const char *key, const char *type) {
    Center *c = P_CENTER(key);
    Atlas *atlas = str_eq(type, "Joker") ? atlas_get("Joker") : str_eq(type, "Voucher") ? atlas_get("Voucher") :
                   str_eq(type, "Back") ? atlas_get("centers") : atlas_get("icons");
    double ratio = atlas && atlas->py ? (double)atlas->px / atlas->py : 1;
    Sprite *t_s = sprite_new(0, 0, 1.5 * ratio, 1.5, atlas, c ? c->pos : (SpritePos){ 3, 0 });
    t_s->n.states.drag.can = false;
    t_s->n.states.hover.can = false;
    t_s->n.states.collide.can = false;
    const char *subtext = str_eq(type, "achievement") ? localize_key(G.F_TROPHIES ? "k_trophy" : "k_achievement") :
                          str_eq(type, "Joker") ? localize_key("k_joker") : str_eq(type, "Voucher") ? localize_key("k_voucher") :
                          str_eq(type, "Back") ? localize_key("k_deck") : "ERROR";
    if (str_eq(key, "b_challenge")) subtext = localize_key("k_challenges");
    const char *name = str_eq(type, "achievement") ? localize_cat(key, "achievement_names") : "ERROR";
    UIDef *info = !str_eq(type, "achievement")
        ? UIR(CFG(.align = "cm", .padding = 0.04),
              UIR(CFG(.align = "cm", .maxw = 3.4), UIT(CFG(.text = (char *)subtext, .scale = 0.5, .colour = G.C.FILTER, .shadow = true))),
              UIR(CFG(.align = "cm", .maxw = 3.4), UIT(CFG(.text = LOC("k_unlocked_ex"), .scale = 0.35, .colour = G.C.FILTER, .shadow = true))))
        : UIR(CFG(.align = "cm", .padding = 0.04),
              UIR(CFG(.align = "cm", .maxw = 3.4, .padding = 0.1), UIT(CFG(.text = (char *)name, .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
              UIR(CFG(.align = "cm", .maxw = 3.4), UIT(CFG(.text = (char *)subtext, .scale = 0.3, .colour = G.C.FILTER, .shadow = true))),
              UIR(CFG(.align = "cm", .maxw = 3.4), UIT(CFG(.text = LOC("k_unlocked_ex"), .scale = 0.35, .colour = G.C.FILTER, .shadow = true))));
    return UIROOT(CFG(.align = "cl", .r = 0.1, .padding = 0.06, .colour = G.C.UI.TRANSPARENT_DARK),
        UIR(CFG(.align = "cl", .padding = 0.2, .minw = 20, .r = 0.1, .colour = G.C.BLACK, .outline = 1.5, .outline_colour = G.C.GREY),
            UIR(CFG(.align = "cm", .r = 0.1),
                UIR(CFG(.align = "cm", .r = 0.1), UIO(CFG(.object = &t_s->n))),
                info)));
}

/* ------------------------------------------------------------------ */
/* challenges tab                                                      */
/* ------------------------------------------------------------------ */
static int challenges_completed(void) {
    int n = 0;
    DVal *done = dv_get(dv_get(profile(), "challenge_progress"), "completed");
    for (int k = 0; k < G_N_CHALLENGES; k++) if (G_CHALLENGES[k].id && dv_truthy(dv_get(done, G_CHALLENGES[k].id))) n++;
    return n;
}

typedef struct { double comp, unlocked; } ChTab;

UIDef *UIDEF_challenges(bool from_game_over) {
    DVal *p = profile();
    if (dv_get_bool(p, "all_unlocked", false)) dv_set_num(p, "challenges_unlocked", G_N_CHALLENGES);
    if (!dv_get(p, "challenges_unlocked") || dv_get(p, "challenges_unlocked")->t == DV_NIL) {
        int deck_wins = 0;
        DVal *du = dv_get(p, "deck_usage");
        for (int i = 0; i < dv_count(du); i++) {
            DVal *w = dv_get(dv_val(du, i), "wins");
            if (w && dv_geti(w, 1) && dv_geti(w, 1)->t != DV_NIL) deck_wins++;
        }
        UIDef *loc_nodes = uinodes();
        localize_ui((LocArgs){ .type = "descriptions", .key = "challenge_locked", .set = "Other", .nodes = loc_nodes,
                               .vars = LV(num_str(G.CHALLENGE_WINS), tfmt("%d", deck_wins)), .default_col = G.C.WHITE });
        return UIROOT(CFG(.align = "cm", .padding = 0.1, .colour = G.C.CLEAR, .minh = 8.02, .minw = 7), transparent_multiline_text(loc_nodes));
    }
    G.run_setup_seed = false;
    if (G.OVERLAY_MENU) {
        UIElement *st = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "run_setup_seed", NULL);
        if (st) st->n.states.visible = false;
    }
    int comp = challenges_completed(), tot = G_N_CHALLENGES;
    ChTab *tab = NEW(ChTab);
    tab->comp = comp;
    tab->unlocked = dv_get_num(p, "challenges_unlocked", 0);
    ProgressBar *pb1 = NEW(ProgressBar);
    pb1->max = tot; pb1->ref = REFN(&tab->unlocked); pb1->empty_col = G.C.L_BLACK; pb1->filled_col = G.C.FILTER;
    ProgressBar *pb2 = NEW(ProgressBar);
    pb2->max = tot; pb2->ref = REFN(&tab->comp); pb2->empty_col = G.C.L_BLACK; pb2->filled_col = adjust_alpha(G.C.GREEN, 0.5);
    return UIROOT(CFG(.align = "cm", .padding = 0.1, .colour = G.C.CLEAR, .minh = 8, .minw = 7),
        UIR(CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.BLACK),
            UIR(CFG(.align = "cm", .padding = 0.1), UIT(CFG(.text = LOC("k_challenge_mode"), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIR(CFG(.align = "cm", .minw = 8.5, .minh = 1.5, .padding = 0.2),
                UIBUTTON(.id = from_game_over ? "from_game_over" : NULL, .label = { localize_key("b_new_challenge") }, .nlabel = 1, .button = G_FUNCS_challenge_list,
                         .minw = 4, .scale = 0.4, .minh = 0.6)),
            UIR(CFG(.align = "cm", .minh = 0.8, .r = 0.1, .minw = 4.5, .colour = G.C.L_BLACK, .emboss = 0.05, .progress_bar = pb1),
                UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .minw = 4.5),
                    UIT(CFG(.text = localize_variable("unlocked", LV(num_str(tab->unlocked), tfmt("%d", tot))), .scale = 0.3, .colour = G.C.WHITE, .shadow = true)))),
            UIR(CFG(.align = "cm", .minh = 0.8, .r = 0.1, .minw = 4.5, .colour = G.C.L_BLACK, .emboss = 0.05, .progress_bar = pb2),
                UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .minw = 4.5),
                    UIT(CFG(.text = localize_variable("completed", LV(tfmt("%d", comp), tfmt("%d", tot))), .scale = 0.3, .colour = G.C.WHITE, .shadow = true))))));
}

/* ------------------------------------------------------------------ */
/* run setup                                                           */
/* ------------------------------------------------------------------ */
static UIDef *run_setup_option_fn(void *arg) { return UIDEF_run_setup_option((const char *)arg); }
static UIDef *challenges_fn(void *arg) { return UIDEF_challenges(arg != NULL); }

UIDef *UIDEF_run_setup(const char *from) {
    G.run_setup_seed = false;
    bool challenge_chosen = str_eq(from, "challenge_list");
    bool from_game_over = from && !challenge_chosen;
    bool can_continue = false;
    if (G.MAIN_MENU_UI) {
        UIConfig cfg = { .func = G_FUNCS_can_continue };
        UIElement fake = { .config = &cfg };
        can_continue = can_continue_check(&fake);
    }
    TabsArgs ta = { .snap_to_nav = true };
    ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_new_run"), .chosen = !challenge_chosen && !can_continue,
                                     .tab_definition_function = run_setup_option_fn, .tab_definition_function_args = "New Run" };
    if (G.STAGE == STAGE_MAIN_MENU) {
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_continue"), .chosen = !challenge_chosen && can_continue,
                                         .tab_definition_function = run_setup_option_fn, .tab_definition_function_args = "Continue", .func = G_FUNCS_can_continue };
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_challenges"), .tab_definition_function = challenges_fn,
                                         .tab_definition_function_args = from_game_over ? (void *)1 : NULL, .chosen = challenge_chosen };
    } else {
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key("b_challenges"), .tab_definition_function = challenges_fn,
                                         .tab_definition_function_args = from_game_over ? (void *)1 : NULL, .chosen = challenge_chosen };
    }
    return GENERIC_OPTIONS(.no_back = from_game_over, .contents = uidef_add(uinodes(), UIR(CFG(.align = "cm", .padding = 0, .draw_layer = 1), create_tabs(ta))));
}

static void change_stake_to(int to_key) {
    G.viewed_stake = to_key;
    dv_set_num(dv_get_or_create_table(profile(), "MEMORY"), "stake", to_key);
}
static void change_stake(CycleCallbackArgs *a) { change_stake_to(a->to_key); }
extern CycleCallback cb_change_viewed_back;

UIDef *UIDEF_run_setup_option(const char *type) {
    if (G.SAVED_GAME.t == DV_NIL) {
        DVal sg;
        if (get_compressed(tfmt("%d/save.jkr", G.SETTINGS.profile), &sg)) G.SAVED_GAME = sg;
    }
    str_set(&G.SETTINGS.current_setup, type);
    DVal *mem = dv_get_or_create_table(profile(), "MEMORY");
    G.GAME.viewed_back = back_new(get_deck_from_name(dv_get_str(mem, "deck", NULL)));
    if (!dv_get(mem, "stake")) dv_set_num(mem, "stake", 1);
    bool cont = str_eq(type, "Continue");
    DVal *sg = G.SAVED_GAME.t == DV_TABLE ? &G.SAVED_GAME : NULL;
    if (cont) {
        G.viewed_stake = 1;
        if (sg) {
            const char *bn = dv_str_or(dv_path(sg, "BACK.name"), "");
            const char *viewed = "b_red";
            for (int i = 0; i < G.n_centers; i++) if (str_eq(G.P_CENTERS_LIST[i].name, bn)) viewed = G.P_CENTERS_LIST[i].key;
            Back_change_to(G.GAME.viewed_back, P_CENTER(viewed));
            G.viewed_stake = dv_num_or(dv_path(sg, "GAME.stake"), 1);
        }
    }
    if (str_eq(type, "New Run")) {
        if (G.OVERLAY_MENU) {
            UIElement *st = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "run_setup_seed", NULL);
            if (st) st->n.states.visible = true;
        }
        G.viewed_stake = dv_get_num(mem, "stake", 1);
        change_stake_to((int)G.viewed_stake);
    } else {
        G.run_setup_seed = false;
        if (G.OVERLAY_MENU) {
            UIElement *st = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "run_setup_seed", NULL);
            if (st) st->n.states.visible = false;
        }
    }
    CardArea *area = cardarea_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W, G.CARD_H,
                                  CA_ARGS(.card_limit = 5, .has_card_limit = true, .type = "deck", .highlight_limit = 0, .has_highlight_limit = true,
                                          .deck_height = 0.75, .thin_draw = 1));
    for (int i = 1; i <= 10; i++) {
        Card *card = card_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W, G.CARD_H, random_p_card(), P_CENTER("c_base"),
                              &(CardParams){ .playing_card = i, .viewed_back = true });
        card->sprite_facing = FACE_BACK;
        card->facing = FACE_BACK;
        CardArea_emplace(area, card, NULL, false);
        if (i == 10) { G.sticker_card = card; card->sticker = get_deck_win_sticker(G.GAME.viewed_back->effect.center); }
    }
    const char **names = xcalloc((size_t)G.P_CENTER_POOLS.Back.count + 1, sizeof(char *));
    int viewed_deck = 1;
    for (int k = 0; k < G.P_CENTER_POOLS.Back.count; k++) {
        names[k] = G.P_CENTER_POOLS.Back.items[k]->name;
        if (str_eq(names[k], G.GAME.viewed_back->name)) viewed_deck = k + 1;
    }
    double lwidth = 1.4, rwidth = 1.8, scale = 0.39;
    str_set(&G.setup_seed, "");

    UIBox *stake_col = uibox_new(UIDEF_deck_stake_column(G.GAME.viewed_back->effect.center->key), UIBCFG(.has_offset = true, .offset = { 0, 0 }));
    UIDef *stake_col_o = UIO(CFG(.id = G.GAME.viewed_back->name, .func = G_FUNCS_RUN_SETUP_check_back_stake_column, .object = &stake_col->n));
    UIDef *top;
    if (cont && sg) {
#define STATROW(lbl, val, col) UIR(CFG(.align = "cm"), \
            UIC(CFG(.align = "cm", .minw = lwidth, .maxw = lwidth), UIT(CFG(.text = LOC(lbl), .colour = G.C.UI.TEXT_DARK, .scale = scale * 0.8))), \
            UIC(CFG(.align = "cm"), UIT(CFG(.text = ": ", .colour = G.C.UI.TEXT_DARK, .scale = scale * 0.8))), \
            UIC(CFG(.align = "cl", .minw = rwidth, .maxw = lwidth), UIT(CFG(.text = (char *)(val), .colour = (col), .scale = 0.8 * scale))))
        double best = dv_num_or(dv_path(sg, "GAME.round_scores.hand.amt"), 0);
        bool seeded = dv_truthy(dv_path(sg, "GAME.seeded"));
        UIDef *stats = UIR(CFG(.align = "cm", .colour = G.C.WHITE, .padding = 0.03, .minh = 1.75, .r = 0.1),
            STATROW("k_round", num_str(dv_num_or(dv_path(sg, "GAME.round"), 0)), G.C.RED),
            STATROW("k_ante", num_str(dv_num_or(dv_path(sg, "GAME.round_resets.ante"), 0)), G.C.BLUE),
            STATROW("k_money", tfmt("%s%s", localize_key("$"), num_str(dv_num_or(dv_path(sg, "GAME.dollars"), 0))), G.C.ORANGE),
            UIR(CFG(.align = "cm"),
                UIC(CFG(.align = "cm", .minw = lwidth, .maxw = lwidth), UIT(CFG(.text = LOC("k_best_hand"), .colour = G.C.UI.TEXT_DARK, .scale = scale * 0.8))),
                UIC(CFG(.align = "cm"), UIT(CFG(.text = ": ", .colour = G.C.UI.TEXT_DARK, .scale = scale * 0.8))),
                UIC(CFG(.align = "cl", .minw = rwidth, .maxw = lwidth),
                    UIT(CFG(.text = (char *)number_format(best), .colour = G.C.RED, .scale = scale_number(best, 0.8 * scale, NAN))))),
            UIIF(seeded, STATROW("k_seed", dv_str_or(dv_path(sg, "GAME.pseudorandom.seed"), ""), G.C.RED)));
#undef STATROW
        top = UIR(CFG(.align = "tm", .minh = 3.8, .padding = 0.15),
            UIR(CFG(.align = "cm", .minh = 3.3, .minw = 6.8),
                UIC(CFG(.align = "cm", .colour = G.C.BLACK, .padding = 0.15, .r = 0.1, .emboss = 0.05),
                    UIC(CFG(.align = "cm"), UIR(CFG(.align = "cm", .shadow = false), UIO(CFG(.object = &area->n)))),
                    UIC(CFG(.align = "cm", .minw = 4, .maxw = 4, .minh = 1.7, .r = 0.1, .colour = G.C.L_BLACK, .padding = 0.1),
                        UIR(CFG(.align = "cm", .r = 0.1, .minw = 4, .maxw = 4, .minh = 0.6),
                            UIO(CFG(.func = G_FUNCS_RUN_SETUP_check_back_name, .object = moveable_new(0, 0, 0, 0)))),
                        stats),
                    UIC(CFG(.align = "cm"), stake_col_o))));
    } else {
        UIBox *backui = uibox_new(Back_generate_UI(G.GAME.viewed_back, NULL, NAN, NAN, NULL), UIBCFG(.has_offset = true, .offset = { 0, 0 }));
        UIDef *mid = UIR(CFG(.align = "cm", .minh = 3.3, .minw = 5),
            UIC(CFG(.align = "cm", .colour = G.C.BLACK, .padding = 0.15, .r = 0.1, .emboss = 0.05),
                UIC(CFG(.align = "cm"), UIR(CFG(.align = "cm", .shadow = false), UIO(CFG(.object = &area->n)))),
                UIC(CFG(.align = "cm", .minh = 1.7, .r = 0.1, .colour = G.C.L_BLACK, .padding = 0.1),
                    UIR(CFG(.align = "cm", .r = 0.1, .minw = 4, .maxw = 4, .minh = 0.6),
                        UIO(CFG(.func = G_FUNCS_RUN_SETUP_check_back_name, .object = moveable_new(0, 0, 0, 0)))),
                    UIR(CFG(.align = "cm", .colour = G.C.WHITE, .minh = 1.7, .r = 0.1),
                        UIO(CFG(.id = G.GAME.viewed_back->name, .func = G_FUNCS_RUN_SETUP_check_back, .object = &backui->n)))),
                UIC(CFG(.align = "cm"), stake_col_o)));
        top = UIR(CFG(.align = "cm", .minh = 3.8),
            OPTION_CYCLE(.options = names, .noptions = G.P_CENTER_POOLS.Back.count, .opt_callback = cb_change_viewed_back, .current_option = viewed_deck,
                         .colour = G.C.RED, .w = 3.5, .mid = mid));
    }
    UIDef *stake_row = cont
        ? UIR(CFG(.align = "cm", .minh = 2.2, .minw = 5),
              UIR(CFG(.align = "cm", .minh = 0.17)),
              UIR(CFG(.align = "cm"), UIO(CFG(.func = G_FUNCS_RUN_SETUP_check_stake, .insta_func = true, .object = moveable_new(0, 0, 0, 0)))))
        : UIR(CFG(.align = "cm", .minh = 2.2, .minw = 6.8), UIO(CFG(.func = G_FUNCS_RUN_SETUP_check_stake, .insta_func = true, .object = moveable_new(0, 0, 0, 0))));
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR, .minh = 6.6, .minw = 6),
        top,
        UIR(CFG(.align = "cm"), stake_row),
        UIR(CFG(.align = "cm", .padding = 0.05, .minh = 0.9), UIO(CFG(.align = "cm", .func = G_FUNCS_toggle_seeded_run, .object = moveable_new(0, 0, 0, 0)))),
        UIR(CFG(.align = "cm", .padding = 0),
            UIC(CFG(.align = "cm", .minw = 2.4, .id = "run_setup_seed"),
                UIIF(str_eq(type, "New Run"), TOGGLE(.col = true, .label = localize_key("k_seeded_run"), .label_scale = 0.25, .w = 0.0001, .scale = 0.7,
                                                     .ref = &G.run_setup_seed))),
            UIC(CFG(.align = "cm", .minw = 5, .minh = 0.8, .padding = 0.2, .r = 0.1, .hover = true, .colour = G.C.BLUE, .button = G_FUNCS_start_setup_run,
                    .shadow = true, .func = G_FUNCS_can_start_run),
                UIR(CFG(.align = "cm", .padding = 0),
                    UIT(CFG(.text = LOC("b_play_cap"), .scale = 0.8, .colour = G.C.UI.TEXT_LIGHT, .func = G_FUNCS_set_button_pip,
                            .focus_args = FOCUS(.button = "x", .set_button_pip = true))))),
            UIC(CFG(.align = "cm", .minw = 2.5))));
}

GF_IMPL(can_start_run) {
    if (!G.GAME.viewed_back->effect.center->unlocked) { e->config->colour = G.C.UI.BACKGROUND_INACTIVE; e->config->button = NULL; }
    else { e->config->colour = G.C.BLUE; e->config->button = G_FUNCS_start_setup_run; }
}

GF_IMPL(toggle_seeded_run) {
    if (e->config->object && !G.run_setup_seed) {
        node_remove(e->config->object);
        e->config->object = NULL;
    } else if (!e->config->object && G.run_setup_seed) {
        if (!G.setup_seed) G.setup_seed = xstrdup("");
        int nps; const char *ps[2]; nps = localize_list("ml_paste_seed", NULL, ps, 2);
        UIButtonArgs pb = { .minw = 1, .minh = 0.6, .button = G_FUNCS_paste_seed, .colour = G.C.BLUE, .scale = 0.3, .col = true };
        for (int i = 0; i < nps; i++) pb.label[pb.nlabel++] = ps[i];
        UIBox *b = uibox_new(UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
                                 UIC(CFG(.align = "cm", .minw = 2.5, .padding = 0.05),
                                     simple_text_container("ml_disabled_seed", (SimpleTextArgs){ .colour = G.C.UI.TEXT_LIGHT, .scale = 0.26, .shadow = true })),
                                 UIC(CFG(.align = "cm", .minw = 0.1),
                                     TEXT_INPUT(.max_length = 8, .all_caps = true, .ref = &G.setup_seed, .prompt_text = localize_key("k_enter_seed")),
                                     UIC(CFG(.align = "cm", .minw = 0.1)),
                                     UIBox_button(pb)),
                                 UIC(CFG(.align = "cm", .minw = 2.5))),
                             UIBCFG(.has_offset = true, .offset = { 0, 0 }, .parent = &e->n, .align = "cm"));
        e->config->object = &b->n;
        UIBox_recalculate(b);
    }
}

GF_IMPL(RUN_SETUP_check_stake) {
    if (!str_eq(G.GAME.viewed_back->name, e->config->id)) {
        if (e->config->object) node_remove(e->config->object);
        UIBox *b = uibox_new(UIDEF_stake_option(G.SETTINGS.current_setup), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "tmi", .parent = &e->n));
        e->config->object = &b->n;
        e->config->id = G.GAME.viewed_back->name;
    }
}

GF_IMPL(RUN_SETUP_check_stake2) {
    const char *vs = intern(tfmt("%g", G.viewed_stake));
    if (!str_eq(vs, e->config->id)) {
        if (e->config->object) node_remove(e->config->object);
        UIBox *b = uibox_new(UIDEF_viewed_stake_option(), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "cm", .parent = &e->n));
        e->config->object = &b->n;
        e->config->id = vs;
    }
}

UIDef *UIDEF_stake_description(int stake) {
    if (stake < 1 || stake > G.P_CENTER_POOLS.Stake.count) return UIC(CFG(.align = "cm"));
    StakeProto *sc = G.P_CENTER_POOLS.Stake.items[stake - 1];
    UIDef *ret = uinodes();
    localize_ui((LocArgs){ .type = "descriptions", .key = sc->key, .set = sc->set, .nodes = ret });
    UIDef *desc = uinodes();
    for (int k = 0; k < ret->count; k++) uidef_add(desc, uidef_add_all(UIR(CFG(.align = "cm", .maxw = 5.3)), ret->nodes[k]));
    return UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = G.C.L_BLACK),
        UIR(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = (char *)localize_name_text(sc->set, sc->key), .scale = 0.35, .colour = G.C.WHITE))),
        uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.03, .colour = G.C.WHITE, .r = 0.1, .minh = 1, .minw = 5.5)), desc));
}

UIDef *UIDEF_stake_option(const char *type) {
    UIDef *middle = UIR(CFG(.align = "cm", .minh = 1.7, .minw = 7.3), UIO(CFG(.func = G_FUNCS_RUN_SETUP_check_stake2, .object = moveable_new(0, 0, 0, 0))));
    double max_stake = get_deck_win_stake(G.GAME.viewed_back->effect.center->key);
    if (dv_get_bool(profile(), "all_unlocked", false)) max_stake = 8;
    int n = (int)MIN(max_stake + 1, 8);
    const char **opts = xcalloc((size_t)n + 1, sizeof(char *));
    for (int i = 1; i <= n; i++) opts[i - 1] = intern(tfmt("%d", i));
    return UIROOT(CFG(.align = "tm", .colour = G.C.CLEAR, .minh = 2.03, .minw = 8.3),
        str_eq(type, "Continue") ? middle
                                 : OPTION_CYCLE(.options = opts, .noptions = n, .opt_callback = change_stake, .current_option = (int)G.viewed_stake,
                                                .colour = G.C.RED, .w = 6, .mid = middle));
}

UIDef *UIDEF_viewed_stake_option(void) {
    if (G.viewed_stake == 0) G.viewed_stake = 1;
    double max_stake = get_deck_win_stake(G.GAME.viewed_back->effect.center->key);
    if (dv_get_bool(profile(), "all_unlocked", false)) max_stake = 8;
    G.viewed_stake = MIN(max_stake + 1, G.viewed_stake);
    dv_set_num(dv_get_or_create_table(profile(), "MEMORY"), "stake", G.viewed_stake);
    Sprite *ss = get_stake_sprite((int)G.viewed_stake, NAN);
    return UIROOT(CFG(.align = "cm", .colour = G.C.BLACK, .r = 0.1),
        UIC(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = LOC("k_stake"), .scale = 0.4, .colour = G.C.L_BLACK, .vert = true))),
        UIC(CFG(.align = "cm", .padding = 0.1),
            UIC(CFG(.align = "cm", .padding = 0), UIO(CFG(.colour = G.C.BLUE, .object = &ss->n, .hover = true, .can_collide = -1))),
            UIDEF_stake_description((int)G.viewed_stake)));
}

/* ------------------------------------------------------------------ */
/* challenge list / description                                        */
/* ------------------------------------------------------------------ */
#define CHALLENGE_PAGE_SIZE 10

static void change_challenge_list_page(CycleCallbackArgs *a);
EVFN(ev_first_challenge_page) {
    (void)e_; (void)p_;
    CycleState cs = { .current_option = 1 };
    CycleCallbackArgs a = { .cycle_config = &cs };
    change_challenge_list_page(&a);
    return true;
}

UIDef *UIDEF_challenge_list(bool from_game_over) {
    int npages = (int)ceil((double)G_N_CHALLENGES / CHALLENGE_PAGE_SIZE);
    const char **pages = xcalloc((size_t)npages + 1, sizeof(char *));
    for (int i = 1; i <= npages; i++) pages[i - 1] = intern(tfmt("%s %d/%d", localize_key("k_page"), i, npages));
    add_event(EVENT(.func = ev_first_challenge_page));
    int comp = challenges_completed();
    (void)from_game_over;
    UIDef *contents = uinodes();
    uidef_add(contents, UIC(CFG(.align = "cm", .padding = 0.0),
        UIR(CFG(.align = "cm", .padding = 0.1, .minh = 7, .minw = 4.2), UIO(CFG(.id = "challenge_list", .object = moveable_new(0, 0, 0, 0)))),
        UIR(CFG(.align = "cm", .padding = 0.1),
            OPTION_CYCLE(.id = "challenge_page", .scale = 0.9, .h = 0.3, .w = 3.5, .options = pages, .noptions = npages, .cycle_shoulders = true,
                         .opt_callback = change_challenge_list_page, .current_option = 1, .colour = G.C.RED, .no_pips = true, .focus_args = FOCUS(.snap_to = true))),
        UIR(CFG(.align = "cm", .padding = 0.1),
            UIT(CFG(.text = localize_variable("challenges_completed", LV(tfmt("%d", comp), tfmt("%d", G_N_CHALLENGES))), .scale = 0.4, .colour = G.C.WHITE)))));
    uidef_add(contents, UIC(CFG(.align = "cm", .minh = 9, .minw = 11.5), UIO(CFG(.id = "challenge_area", .object = moveable_new(0, 0, 0, 0)))));
    return GENERIC_OPTIONS(.back_func = G_FUNCS_setup_run, .back_id = "challenge_list", .contents = contents);
}

UIDef *UIDEF_challenge_list_page(int page) {
    bool snapped = false;
    UIDef *list = UIROOT(CFG(.align = "cm", .padding = 0.1, .colour = G.C.CLEAR));
    DVal *done = dv_get(dv_get(profile(), "challenge_progress"), "completed");
    double unlocked = dv_get_num(profile(), "challenges_unlocked", -1);
    for (int k = 1; k <= G_N_CHALLENGES; k++) {
        if (!(k > CHALLENGE_PAGE_SIZE * page && k <= CHALLENGE_PAGE_SIZE * (page + 1))) continue;
        Challenge *v = &G_CHALLENGES[k - 1];
        Node *ft = G.CONTROLLER.focused.target;
        if (ft && node_is(ft, NT_UIELEMENT) && str_eq(((UIElement *)ft)->config->id, "challenge_page")) snapped = true;
        bool completed = dv_truthy(dv_get(done, v->id ? v->id : ""));
        bool ch_unlocked = unlocked >= 0 && unlocked >= k;
        Sprite *check = completed ? sprite_new(0, 0, 0.4, 0.4, atlas_get("icons"), (SpritePos){ 1, 0 }) : NULL;
        uidef_add(list, UIR(CFG(.align = "cm"),
            UIC(CFG(.align = "cl", .minw = 0.8), UIT(CFG(.text = (char *)tfmt("%d", k), .scale = 0.4, .colour = G.C.WHITE))),
            UIBUTTON(.id = tfmt("%d", k), .col = true, .label = { ch_unlocked ? localize_cat(v->id, "challenge_names") : localize_key("k_locked") }, .nlabel = 1,
                     .button = ch_unlocked ? G_FUNCS_change_challenge_description : NULL, .no_button = !ch_unlocked,
                     .colour = ch_unlocked ? G.C.RED : G.C.GREY, .minw = 4, .scale = 0.4, .minh = 0.6, .focus_args = FOCUS(.snap_to = !snapped)),
            UIC(CFG(.align = "cm", .padding = 0.05, .minw = 0.6),
                UIC(CFG(.minh = 0.4, .minw = 0.4, .emboss = 0.05, .r = 0.1, .colour = completed ? G.C.GREEN : G.C.BLACK),
                    UIIF(check, UIO(CFG(.object = check ? &check->n : NULL)))))));
        snapped = true;
    }
    return list;
}

GF_IMPL(change_challenge_description) {
    if (!G.OVERLAY_MENU) return;
    UIElement *desc = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "challenge_area", NULL);
    if (!desc) return;
    const char *id = e->config->id ? e->config->id : "nil";
    if (str_eq(desc->config->str, id)) return;
    UIElement *old = desc->config->data;
    if (old) old->config->chosen = 0;
    e->config->chosen = 2;   /* 'vert' */
    if (desc->config->object) node_remove(desc->config->object);
    UIBox *b = uibox_new(UIDEF_challenge_description(atoi(id), false, false), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "cm", .parent = &desc->n));
    desc->config->object = &b->n;
    str_set(&desc->config->str, id);
    desc->config->data = e->UIBox ? e : NULL;
}

static void change_challenge_list_page(CycleCallbackArgs *a) {
    if (!a || !a->cycle_config || !G.OVERLAY_MENU) return;
    UIElement *ch = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "challenge_list", NULL);
    if (!ch) return;
    if (ch->config->object) node_remove(ch->config->object);
    UIBox *b = uibox_new(UIDEF_challenge_list_page(a->cycle_config->current_option - 1), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "cm", .parent = &ch->n));
    ch->config->object = &b->n;
    UIConfig cfg = { .id = "nil" };
    UIElement fake = { .config = &cfg };
    G_FUNCS_change_challenge_description(&fake);
}

typedef struct { int id; const char *tab; } ChTabArgs;
static UIDef *challenge_tab_fn(void *arg);

static CardArea *challenge_area(double w, double limit, bool has_limit, bool spread) {
    return cardarea_new(0, 0, w, 0.6 * G.CARD_H,
                        CA_ARGS(.card_limit = limit, .has_card_limit = has_limit, .card_w = 0.6 * G.CARD_W, .type = "title_2", .spread = spread,
                                .highlight_limit = 0, .has_highlight_limit = true));
}

static void fill_challenge_cards(CardArea *a, ChallengeCard *cards, int n, bool pins) {
    for (int k = 0; k < n; k++) {
        ChallengeCard *v = &cards[k];
        Card *card = card_new(0, 0, G.CARD_W * 0.6, G.CARD_H * 0.6, NULL, P_CENTER(v->id),
                              &(CardParams){ .bypass_discovery_center = true, .bypass_discovery_ui = true, .bypass_lock = true });
        if (v->edition) Card_set_edition(card, edition_req_from_key(v->edition), true, true);
        if (v->eternal) Card_set_eternal(card, true);
        if (pins && v->pinned) card->pinned = true;
        CardArea_emplace(a, card, NULL, false);
    }
}

UIDef *UIDEF_challenge_description(int id, bool daily, bool is_row) {
    (void)daily;
    if (id < 1 || id > G_N_CHALLENGES)
        return UIROOT(CFG(.align = "cm", .colour = G.C.BLACK, .minh = 8.82, .minw = 11.5, .r = 0.1),
                      UIT(CFG(.text = LOC("ph_select_challenge"), .scale = 0.3, .colour = G.C.UI.TEXT_LIGHT)));
    Challenge *ch = &G_CHALLENGES[id - 1];
    ChallengeRule *jl = get_challenge_rule(ch, "modifiers", "joker_limit");
    ChallengeRule *cl = get_challenge_rule(ch, "modifiers", "consumable_limit");
    CardArea *jokers = challenge_area(10 * 0.6, jl ? jl->value_num : 5, true, false);
    fill_challenge_cards(jokers, ch->jokers, ch->njokers, true);
    CardArea *cons = challenge_area(3 * 0.6, cl ? cl->value_num : 2, true, true);
    fill_challenge_cards(cons, ch->consumeables, ch->nconsumeables, false);
    CardArea *vouch = challenge_area(3 * 0.6, 0, false, true);
    fill_challenge_cards(vouch, ch->vouchers, ch->nvouchers, false);
#define COL(lbl, sc, area, mw) UIC(CFG(.align = "cm", .padding = 0.05, .colour = G.C.L_BLACK, .r = 0.1, .maxh = 1.8), \
        UIT(CFG(.text = LOC(lbl), .scale = sc, .colour = G.C.UI.TEXT_LIGHT, .vert = true, .shadow = true)), \
        UIC(CFG(.align = "cm", .minh = 0.6 * G.CARD_H, .minw = mw, .r = 0.1, .colour = G.C.UI.TRANSPARENT_DARK), UIO(CFG(.object = &(area)->n))))
    TabsArgs ta = { .tab_h = 5, .padding = 0, .has_padding = true, .text_scale = 0.36, .scale = 0.85, .no_shoulders = true, .no_loop = true };
    static const char *tabs[3] = { "Rules", "Restrictions", "Deck" };
    static const char *labels[3] = { "b_rules", "b_restrictions", "b_deck" };
    for (int i = 0; i < 3; i++) {
        ChTabArgs *a = NEW(ChTabArgs); a->id = id; a->tab = tabs[i];
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = localize_key(labels[i]), .chosen = i == 0, .tab_definition_function = challenge_tab_fn, .tab_definition_function_args = a };
    }
    return UIN(is_row ? UIT_R : UIT_ROOT, CFG(.align = "cm", .r = 0.1, .colour = G.C.BLACK),
        UIR(CFG(.align = "cm", .padding = 0.1),
            COL("k_jokers_cap", 0.5, jokers, 5), COL("k_cap_consumables", 0.3, cons, 0), COL("k_vouchers_cap", 0.33, vouch, 0)),
        UIR(CFG(.align = "cm", .padding = 0), create_tabs(ta)),
        UIIF(!is_row, UIR(CFG(.align = "cm", .minh = 0.9),
            UIR(CFG(.align = "cm", .padding = 0.1, .minh = 0.7, .minw = 9, .r = 0.1, .hover = true, .colour = G.C.BLUE, .button = G_FUNCS_start_challenge_run,
                    .shadow = true, .id = intern(tfmt("%d", id))),
                UIT(CFG(.text = LOC("b_play_cap"), .scale = 0.5, .colour = G.C.UI.TEXT_LIGHT, .func = G_FUNCS_set_button_pip,
                        .focus_args = FOCUS(.button = "x", .set_button_pip = true)))))));
#undef COL
}

typedef struct { const char *key; double value; int order; bool custom; bool has_old; } ModRow;
static int cmp_modrow(const void *a, const void *b) { return ((const ModRow *)a)->order - ((const ModRow *)b)->order; }

static UIDef *text_row(const char *key, LocVars *vars, Col def) {
    UIDef *row = localize_ui((LocArgs){ .type = "text", .key = key, .vars = vars, .default_col = def });
    return uidef_add_all(UIR(CFG(.align = "cl", .maxw = 3.5)), row);
}

static void blind_hover(Node *n);
static void blind_stop_hover(Node *n);

static int cmp_tag_order(const void *a, const void *b) {
    const TagProto *x = *(TagProto *const *)a, *y = *(TagProto *const *)b;
    return x->order < y->order ? -1 : x->order > y->order ? 1 : 0;
}
static int cmp_blind_order(const void *a, const void *b) {
    const BlindProto *x = *(BlindProto *const *)a, *y = *(BlindProto *const *)b;
    return x->order < y->order ? -1 : x->order > y->order ? 1 : 0;
}
static int cmp_nominal_desc(const void *a, const void *b) {
    Card *x = *(Card *const *)a, *y = *(Card *const *)b;
    double nx = Card_get_nominal(x, NULL), ny = Card_get_nominal(y, NULL);
    return nx > ny ? -1 : nx < ny ? 1 : 0;
}

static UIDef *challenge_tab_fn(void *arg) {
    ChTabArgs *args = arg;
    Challenge *ch = &G_CHALLENGES[args->id - 1];
    if (!strcmp(args->tab, "Rules")) {
        StartingParams sp = get_starting_params();
        ModRow mods[32]; int nm = 0;
        mods[nm++] = (ModRow){ "dollars", sp.dollars, 6 };
        mods[nm++] = (ModRow){ "discards", sp.discards, 2 };
        mods[nm++] = (ModRow){ "hands", sp.hands, 1 };
        mods[nm++] = (ModRow){ "reroll_cost", sp.reroll_cost, 7 };
        mods[nm++] = (ModRow){ "joker_slots", sp.joker_slots, 4 };
        mods[nm++] = (ModRow){ "consumable_slots", sp.consumable_slots, 5 };
        mods[nm++] = (ModRow){ "hand_size", sp.hand_size, 3 };
        int bonus = 100;
        for (int k = 0; k < ch->nmodifiers; k++) {
            ChallengeRule *v = &ch->modifiers[k];
            int found = -1;
            for (int i = 0; i < nm; i++) if (str_eq(mods[i].key, v->id)) found = i;
            if (found >= 0) { mods[found].value = v->value_num; mods[found].custom = true; mods[found].has_old = true; }
            else if (nm < 32) mods[nm++] = (ModRow){ v->id, v->value_num, bonus, true, false };
            bonus++;
        }
        qsort(mods, (size_t)nm, sizeof(ModRow), cmp_modrow);
        UIDef *start_rules = uinodes(), *modded = NULL;
        for (int i = 0; i < nm; i++) {
            LocVars *lv = LV(num_str(mods[i].value));
            if (mods[i].has_old) {
                if (!modded) modded = uinodes();
                uidef_add(modded, text_row(tfmt("ch_m_%s", mods[i].key), lv, G.C.L_BLACK));
            } else {
                uidef_add(start_rules, text_row(tfmt("ch_m_%s", mods[i].key), lv, !mods[i].custom ? G.C.UI.TEXT_INACTIVE : NULL));
            }
        }
        if (modded) {
            UIDef *sr = uinodes();
            uidef_add(sr, uidef_add_all(UIR(CFG(.align = "cl", .padding = 0.05)), modded));
            uidef_add(sr, UIR(CFG(.align = "cl", .padding = 0.05, .colour = G.C.GREY)));
            uidef_add(sr, uidef_add_all(UIR(CFG(.align = "cl", .padding = 0.05)), start_rules));
            start_rules = sr;
        }
        UIDef *game_rules = uinodes();
        for (int k = 0; k < ch->ncustom; k++) {
            ChallengeRule *v = &ch->custom[k];
            LocVars *lv = v->has_value ? LV(v->value_is_str ? v->value_str : num_str(v->value_num)) : loc_vars_new();
            UIDef *row = localize_ui((LocArgs){ .type = "text", .key = tfmt("ch_c_%s", v->id), .vars = lv });
            uidef_add(game_rules, uidef_add_all(UIR(CFG(.align = "cl")), row));
        }
        if (start_rules->count == 0 && !modded)
            uidef_add(start_rules, uidef_add_all(UIR(CFG(.align = "cl")), localize_ui((LocArgs){ .type = "text", .key = "ch_m_none", .vars = loc_vars_new() })));
        if (game_rules->count == 0)
            uidef_add(game_rules, uidef_add_all(UIR(CFG(.align = "cl")), localize_ui((LocArgs){ .type = "text", .key = "ch_c_none", .vars = loc_vars_new() })));
        UIDef *starting = UIC(CFG(.align = "cm", .minw = 3, .r = 0.1, .colour = G.C.BLUE),
            UIR(CFG(.align = "cm", .padding = 0.08, .minh = 0.6), UIT(CFG(.text = LOC("k_game_modifiers"), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            uidef_add_all(UIR(CFG(.align = "cm", .minh = 4.1, .minw = 4.2, .padding = 0.05, .r = 0.1, .colour = G.C.WHITE)), start_rules));
        UIDef *override = UIC(CFG(.align = "cm", .minw = 3, .r = 0.1, .colour = G.C.BLUE),
            UIR(CFG(.align = "cm", .padding = 0.08, .minh = 0.6), UIT(CFG(.text = LOC("k_custom_rules"), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            uidef_add_all(UIR(CFG(.align = "cm", .minh = 4.1, .minw = 6.8, .maxw = 6.7, .padding = 0.05, .r = 0.1, .colour = G.C.WHITE)), game_rules));
        return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            UIC(CFG(.align = "cm", .padding = 0.1, .colour = G.C.L_BLACK, .r = 0.1, .minw = 3), override, starting));
    }
    if (!strcmp(args->tab, "Restrictions")) {
        UIDef *banned_cards = uinodes(), *banned_tags = uinodes(), *banned_other = uinodes();
        int nb = ch->restrictions.n_banned_cards;
        if (nb > 0) {
            int n_rows = (int)MAX(1, floor(nb / 10.0) + 2 - floor(log2(6) * (1.0 / log2(nb))));
            int counts[16] = { 0 }; int max_width = 1;
            for (int k = 1; k <= nb; k++) {
                int r = (int)floor((k - 1) * (double)n_rows / nb + 1);
                if (r >= 1 && r <= 16) { counts[r - 1]++; if (counts[r - 1] > max_width) max_width = counts[r - 1]; }
            }
            double card_size = MAX(0.3, 0.75 - 0.01 * (max_width * n_rows));
            CardArea *areas[16] = { 0 };
            for (int r = 1; r <= n_rows && r <= 16; r++) {
                if (!counts[r - 1]) continue;
                areas[r - 1] = cardarea_new(0, 0, 6.7, 3.3 / n_rows, CA_ARGS(.type = "title_2", .view_deck = true, .highlight_limit = 0, .has_highlight_limit = true,
                                                                            .card_w = G.CARD_W * card_size));
                uidef_add(banned_cards, UIR(CFG(.align = "cm", .padding = 0), UIO(CFG(.object = &areas[r - 1]->n))));
            }
            for (int k = 1; k <= nb; k++) {
                int r = (int)floor((k - 1) * (double)n_rows / nb + 1);
                if (r < 1 || r > 16 || !areas[r - 1]) continue;
                Card *card = card_new(0, 0, G.CARD_W * card_size, G.CARD_H * card_size, NULL, P_CENTER(ch->restrictions.banned_cards[k - 1].id),
                                      &(CardParams){ .bypass_discovery_center = true, .bypass_discovery_ui = true });
                CardArea_emplace(areas[r - 1], card, NULL, false);
            }
        }
        int nt = ch->restrictions.n_banned_tags;
        if (nt > 0) {
            TagProto **tab = xcalloc((size_t)nt, sizeof(TagProto *));
            int n = 0;
            for (int k = 0; k < nt; k++) { TagProto *t = P_TAG(ch->restrictions.banned_tags[k].id); if (t) tab[n++] = t; }
            qsort(tab, (size_t)n, sizeof(TagProto *), cmp_tag_order);
            for (int k = 0; k < n; k++) {
                Tag *tt = tag_new(tab[k]->key, false, NULL);
                uidef_add(banned_tags, UIR(CFG(.align = "cm", .padding = 0), Tag_generate_UI(tt, 1.1 - 0.25 * sqrt(nt), NULL)));
            }
            free(tab);
        }
        int no = ch->restrictions.n_banned_other;
        if (no > 0) {
            BlindProto **tab = xcalloc((size_t)no, sizeof(BlindProto *));
            int n = 0;
            for (int k = 0; k < no; k++) {
                ChallengeBan *b = &ch->restrictions.banned_other[k];
                if (str_eq(b->type, "blind")) { BlindProto *bp = P_BLIND(b->id); if (bp) tab[n++] = bp; }
            }
            qsort(tab, (size_t)n, sizeof(BlindProto *), cmp_blind_order);
            for (int k = 0; k < n; k++) {
                BlindProto *v = tab[k];
                Sprite *tb = animated_sprite_new(0, 0, 1, 1, strmap_get(&G.ANIMATION_ATLAS, "blind_chips"), v->pos);
                DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
                Sprite_define_draw_steps(tb, steps, 2);
                tb->float_anim = true;
                tb->n.states.hover.can = true;
                tb->n.states.drag.can = false;
                tb->n.states.collide.can = true;
                tb->cfg_blind = v;
                tb->n.cfg_force_focus = true;
                tb->n.o_hover = blind_hover;
                tb->n.o_stop_hover = blind_stop_hover;
                uidef_add(banned_other, UIR(CFG(.align = "cm", .padding = 0), UIO(CFG(.object = &tb->n))));
            }
            free(tab);
        }
        if (!banned_cards->count) uidef_add(banned_cards, uidef_add_all(UIR(CFG(.align = "cl")), localize_ui((LocArgs){ .type = "text", .key = "ch_m_none", .vars = loc_vars_new() })));
        if (!banned_tags->count) uidef_add(banned_tags, uidef_add_all(UIR(CFG(.align = "cl")), localize_ui((LocArgs){ .type = "text", .key = "ch_c_none", .vars = loc_vars_new() })));
        if (!banned_other->count) uidef_add(banned_other, uidef_add_all(UIR(CFG(.align = "cl")), localize_ui((LocArgs){ .type = "text", .key = "ch_c_none", .vars = loc_vars_new() })));
        return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            UIC(CFG(.align = "cm", .padding = 0.1, .colour = G.C.L_BLACK, .r = 0.1),
                UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.RED),
                    UIR(CFG(.align = "cm", .padding = 0.08, .minh = 0.6), UIT(CFG(.text = LOC("k_banned_cards"), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
                    uidef_add_all(UIR(CFG(.align = "cm", .minh = 4.1, .minw = 7.33, .padding = 0.05, .r = 0.1, .colour = G.C.WHITE)), banned_cards)),
                UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.RED),
                    UIR(CFG(.align = "cm", .padding = 0.08, .minh = 0.6, .maxw = 1.48), UIT(CFG(.text = LOC("k_banned_tags"), .scale = 0.3, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
                    uidef_add_all(UIR(CFG(.align = "cm", .minh = 4.1, .minw = 1.48, .padding = 0.05, .r = 0.1, .colour = G.C.WHITE)), banned_tags)),
                UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.RED),
                    UIR(CFG(.align = "cm", .padding = 0.08, .minh = 0.6, .maxw = 1.84), UIT(CFG(.text = LOC("k_other"), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
                    uidef_add_all(UIR(CFG(.align = "cm", .minh = 4.1, .minw = 2, .padding = 0.05, .r = 0.1, .colour = G.C.WHITE)), banned_other))));
    }
    /* Deck */
    static const char *suit_map[4] = { "S", "H", "C", "D" };
    PtrVec suits[4]; memset(suits, 0, sizeof suits);
    CardProto *protos = ch->deck.present && ch->deck.cards ? ch->deck.cards : NULL;
    int nprotos = protos ? ch->deck.ncards : 0;
    CardProto *gen = NULL;
    if (!protos) {
        gen = xcalloc((size_t)G.n_cards, sizeof(CardProto));
        for (int i = 0; i < G.n_cards; i++) {
            const char *k = G.P_CARDS_LIST[i].key;
            char r[2] = { k[2], 0 }, s[2] = { k[0], 0 };
            bool keep = true;
            if (ch->deck.present) {
                if (ch->deck.n_yes_ranks) { bool f = false; for (int j = 0; j < ch->deck.n_yes_ranks; j++) if (str_eq(ch->deck.yes_ranks[j], r)) f = true; if (!f) keep = false; }
                for (int j = 0; j < ch->deck.n_no_ranks; j++) if (str_eq(ch->deck.no_ranks[j], r)) keep = false;
                if (ch->deck.n_yes_suits) { bool f = false; for (int j = 0; j < ch->deck.n_yes_suits; j++) if (str_eq(ch->deck.yes_suits[j], s)) f = true; if (!f) keep = false; }
                for (int j = 0; j < ch->deck.n_no_suits; j++) if (str_eq(ch->deck.no_suits[j], s)) keep = false;
            }
            if (keep) gen[nprotos++] = (CardProto){ intern(s), intern(r), ch->deck.present ? ch->deck.enhancement : NULL, ch->deck.present ? ch->deck.edition : NULL,
                                                    ch->deck.present ? ch->deck.gold_seal : NULL };
        }
        protos = gen;
    }
    for (int k = 0; k < nprotos; k++) {
        CardProto *v = &protos[k];
        Card *c = card_new(0, 0, G.CARD_W * 0.45, G.CARD_H * 0.45, P_CARD(tfmt("%s_%s", v->s, v->r)), P_CENTER(v->e ? v->e : "c_base"), NULL);
        if (v->d) Card_set_edition(c, edition_req_from_key(v->d), true, true);
        if (v->g) Card_set_seal(c, v->g, true, true);
        for (int s = 0; s < 4; s++) if (str_eq(v->s, suit_map[s])) vec_push(&suits[s], c);
    }
    UIDef *deck_tables = UIROOT(CFG(.align = "cm", .padding = 0, .colour = G.C.BLACK, .r = 0.1, .minw = 11.4, .minh = 4.2));
    for (int j = 0; j < 4; j++) {
        if (!suits[j].count) continue;
        qsort(suits[j].items, (size_t)suits[j].count, sizeof(void *), cmp_nominal_desc);
        CardArea *vd = cardarea_new(0, 0, 5.5 * G.CARD_W, 0.42 * G.CARD_H,
                                    CA_ARGS(.card_limit = suits[j].count, .has_card_limit = true, .type = "title_2", .view_deck = true, .highlight_limit = 0,
                                            .has_highlight_limit = true, .card_w = G.CARD_W * 0.5, .draw_layer1 = "card"));
        uidef_add(deck_tables, UIR(CFG(.align = "cm", .padding = 0), UIO(CFG(.object = &vd->n))));
        for (int i = 0; i < suits[j].count; i++) CardArea_emplace(vd, suits[j].items[i], NULL, false);
        vec_free(&suits[j]);
    }
    free(gen);
    return deck_tables;
}

static void blind_hover(Node *n) {
    Sprite *s = (Sprite *)n;
    BlindProto *v = s->cfg_blind;
    if (!G.CONTROLLER.dragging.target && !s->hovering && n->states.visible) {
        s->hovering = true;
        n->hover_tilt = 3;
        node_juice_up(n, 0.05, 0.02);
        sound_play("chips1", math_random() * 0.1 + 0.55, 0.12);
        n->h_popup = create_UIBox_blind_popup(v, true, NULL);
        n->h_popup_config = UIBCFG(.align = "cl", .has_offset = true, .offset = { -0.1, 0 }, .parent = n);
        Node_hover(n);
    }
}
static void blind_stop_hover(Node *n) { ((Sprite *)n)->hovering = false; Node_stop_hover(n); n->hover_tilt = 0; }

/* ------------------------------------------------------------------ */
/* profiles                                                            */
/* ------------------------------------------------------------------ */
static UIDef *profile_option_fn(void *arg) { return UIDEF_profile_option((int)(intptr_t)arg); }

UIDef *UIDEF_profile_select(void) {
    if (!G.focused_profile) G.focused_profile = G.SETTINGS.profile ? G.SETTINGS.profile : 1;
    TabsArgs ta = { .snap_to_nav = true };
    for (int i = 1; i <= 3; i++)
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = intern(tfmt("%d", i)), .chosen = G.focused_profile == i, .tab_definition_function = profile_option_fn,
                                         .tab_definition_function_args = (void *)(intptr_t)i };
    return GENERIC_OPTIONS(.padding = 0, .has_padding = true, .contents = uidef_add(uinodes(), UIR(CFG(.align = "cm", .padding = 0, .draw_layer = 1, .minw = 4), create_tabs(ta))));
}

static void profile_name_callback(void) {
    profile_name_sync(G.focused_profile);
    game_save_settings();
    G.FILE_HANDLER.force = true;
}

UIDef *UIDEF_profile_option(int p) {
    set_discover_tallies();
    G.focused_profile = p;
    DVal pd; bool has = get_compressed(tfmt("%d/profile.jkr", p), &pd);
    const char *name = has ? dv_get_str(&pd, "name", NULL) : NULL;
    if (has && !name) name = tfmt("P%d", p);
    char **nref = profile_name_ref(p);
    str_set(nref, has ? name : "");
    dv_set_str(prof(p), "name", *nref);
    double lwidth = 1, rwidth = 1, scale = 1;
    bool current = p == G.SETTINGS.profile;
    DVal *pp = prof(p);
    DVal *progress = dv_get(pp, "progress");
    bool all_unlocked = dv_get_bool(pp, "all_unlocked", false);
    UIDef *name_row = (current || !has)
        ? UIR(CFG(.align = "cm"), TEXT_INPUT(.w = 4, .max_length = 16, .prompt_text = localize_key("k_enter_name"), .ref = nref, .extended_corpus = true,
                                             .keyboard_offset = 1, .has_keyboard_offset = true, .callback = profile_name_callback))
        : UIR(CFG(.align = "cm", .padding = 0.1, .minw = 4, .r = 0.1, .colour = G.C.BLACK, .minh = 0.6), UIT(CFG(.text = *nref, .scale = 0.45, .colour = G.C.WHITE)));
    UIDef *prog = (progress && dv_get(progress, "discovered")) ? create_progress_box(progress, true)
                : UIC(CFG(.align = "cm", .minh = 4, .minw = 5.2, .colour = G.C.BLACK, .r = 0.1),
                      UIT(CFG(.text = LOC("k_empty_caps"), .scale = 0.5, .colour = G.C.UI.TRANSPARENT_LIGHT)));
    const char *load_txt = current ? localize_key("b_current_profile") : has ? localize_key("b_load_profile") : localize_key("b_create_profile");
    UIDef *t = UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIR(CFG(.align = "cm", .padding = 0.1, .minh = 0.8), name_row),
        UIR(CFG(.align = "cm", .padding = 0.1),
            UIC(CFG(.align = "cm", .minw = 6), prog),
            UIC(CFG(.align = "cm", .minh = 4),
                UIR(CFG(.align = "cm", .minh = 1),
                    UIIF(has, UIR(CFG(.align = "cm"),
                        UIC(CFG(.align = "cm", .minw = lwidth), UIT(CFG(.text = LOC("k_wins"), .colour = G.C.UI.TEXT_LIGHT, .scale = scale * 0.7))),
                        UIC(CFG(.align = "cm"), UIT(CFG(.text = ": ", .colour = G.C.UI.TEXT_LIGHT, .scale = scale * 0.7))),
                        UIC(CFG(.align = "cl", .minw = rwidth),
                            UIT(CFG(.text = (char *)num_str(has ? dv_num_or(dv_path(&pd, "career_stats.c_wins"), 0) : 0), .colour = G.C.RED, .shadow = true,
                                    .scale = 1 * scale)))))),
                UIR(CFG(.align = "cm", .padding = 0.2),
                    UIR(CFG(.align = "cm", .padding = 0),
                        UIR(CFG(.align = "cm", .minw = 4, .maxw = 4, .minh = 0.8, .padding = 0.2, .r = 0.1, .hover = true, .colour = G.C.BLUE,
                                .func = G_FUNCS_can_load_profile, .button = G_FUNCS_load_profile, .shadow = true, .focus_args = FOCUS(.nav = "wide")),
                            UIT(CFG(.text = (char *)load_txt, .scale = 0.5, .colour = G.C.UI.TEXT_LIGHT)))),
                    UIR(CFG(.align = "cm", .padding = 0, .minh = 0.7),
                        UIR(CFG(.align = "cm", .minw = 3, .maxw = 4, .minh = 0.6, .padding = 0.2, .r = 0.1, .hover = true, .colour = G.C.RED,
                                .func = G_FUNCS_can_delete_profile, .button = G_FUNCS_delete_profile, .shadow = true, .focus_args = FOCUS(.nav = "wide")),
                            UIT(CFG(.text = current ? LOC("b_reset_profile") : LOC("b_delete_profile"), .scale = 0.3, .colour = G.C.UI.TEXT_LIGHT)))),
                    (current && !dv_get_bool(profile(), "all_unlocked", false))
                        ? UIR(CFG(.align = "cm", .padding = 0, .minh = 0.7),
                              UIR(CFG(.align = "cm", .minw = 3, .maxw = 4, .minh = 0.6, .padding = 0.2, .r = 0.1, .hover = true, .colour = G.C.ORANGE,
                                      .func = G_FUNCS_can_unlock_all, .button = G_FUNCS_unlock_all, .shadow = true, .focus_args = FOCUS(.nav = "wide")),
                                  UIT(CFG(.text = LOC("b_unlock_all"), .scale = 0.3, .colour = G.C.UI.TEXT_LIGHT))))
                        : UIR(CFG(.align = "cm", .minw = 3, .maxw = 4, .minh = 0.7),
                              UIIF(all_unlocked && !G.F_NO_ACHIEVEMENTS,
                                   UIT(CFG(.text = LOC(G.F_TROPHIES ? "k_trophies_disabled" : "k_achievements_disabled"), .scale = 0.3, .colour = G.C.UI.TEXT_LIGHT))))))),
        UIR(CFG(.align = "cm", .padding = 0), UIT(CFG(.id = "warning_text", .text = LOC("ph_click_confirm"), .scale = 0.4, .colour = G.C.CLEAR))));
    if (has) dv_free(&pd);
    return t;
}

UIDef *create_UIBox_profile_button(void) {
    char **nref = profile_name_ref(G.SETTINGS.profile);
    if (!*nref || !(*nref)[0]) { str_set(nref, tfmt("P%d", G.SETTINGS.profile)); profile_name_sync(G.SETTINGS.profile); }
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIR(CFG(.align = "cm", .padding = 0.2, .r = 0.1, .emboss = 0.1, .colour = G.C.L_BLACK),
            UIR(CFG(.align = "cm"), UIT(CFG(.text = LOC("k_profile"), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIR(CFG(.align = "cm"),
                UIC(CFG(.align = "cm", .padding = 0.15, .minw = 2, .minh = 0.8, .maxw = 2, .r = 0.1, .hover = true, .colour = mix_colours(G.C.WHITE, G.C.GREY, 0.2),
                        .button = G_FUNCS_profile_select, .shadow = true),
                    UIT(CFG(.ref = REFS(nref), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))))));
}

/* ------------------------------------------------------------------ */
/* main menu                                                           */
/* ------------------------------------------------------------------ */
UIDef *create_UIBox_main_menu_buttons(void) {
    double text_scale = 0.45;
    Sprite *language = NULL;
    if (!G.F_ENGLISH_ONLY) {
        language = sprite_new(0, 0, 0.6, 0.6, atlas_get("icons"), (SpritePos){ 2, 0 });
        language->n.states.drag.can = false;
    }
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIC(CFG(.align = "bm"),
            UIR(CFG(.align = "cm", .padding = 0.2, .r = 0.1, .emboss = 0.1, .colour = G.C.L_BLACK, .mid = true),
                UIBUTTON(.id = "main_menu_play", .button = !G.SETTINGS.tutorial_complete ? G_FUNCS_start_run : G_FUNCS_setup_run, .colour = G.C.BLUE, .minw = 3.65,
                         .minh = 1.55, .label = { localize_key("b_play_cap") }, .nlabel = 1, .scale = text_scale * 2, .col = true),
                UIC(CFG(.align = "cm"),
                    UIBUTTON(.button = G_FUNCS_options, .colour = G.C.ORANGE, .minw = 2.65, .minh = 1.35, .label = { localize_key("b_options_cap") }, .nlabel = 1,
                             .scale = text_scale * 1.2, .col = true),
                    UIIF(G.F_QUIT_BUTTON, UIC(CFG(.align = "cm", .minw = 0.2))),
                    UIIF(G.F_QUIT_BUTTON, UIBUTTON(.button = G_FUNCS_quit, .colour = G.C.RED, .minw = 2.65, .minh = 1.35, .label = { localize_key("b_quit_cap") },
                                                   .nlabel = 1, .scale = text_scale * 1.2, .col = true))),
                UIBUTTON(.id = "collection_button", .button = G_FUNCS_your_collection, .colour = G.C.PALE_GREEN, .minw = 3.65, .minh = 1.55,
                         .label = { localize_key("b_collection_cap") }, .nlabel = 1, .scale = text_scale * 1.5, .col = true))),
        UIC(CFG(.align = "br", .minw = 3.2, .padding = 0.1),
            UIIF(!G.F_ENGLISH_ONLY, UIR(CFG(.align = "cm", .padding = 0.2, .r = 0.1, .emboss = 0.1, .colour = G.C.L_BLACK),
                UIR(CFG(.align = "cm", .padding = 0.15, .minw = 1, .r = 0.1, .hover = true, .colour = mix_colours(G.C.WHITE, G.C.GREY, 0.2),
                        .button = G_FUNCS_language_selection, .shadow = true),
                    UIO(CFG(.object = language ? &language->n : NULL)),
                    UIT(CFG(.text = (char *)(G.LANG ? G.LANG->label : ""), .scale = 0.4, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)))))));
}

static int cmp_lang_label(const void *a, const void *b) {
    const Language *x = *(Language *const *)a, *y = *(Language *const *)b;
    return strcmp(x->label, y->label);
}

UIDef *UIDEF_language_selector(void) {
    Language **langs = xcalloc((size_t)G.n_languages + 1, sizeof(Language *));
    int n = 0;
    for (int i = 0; i < G.n_languages; i++) if (!G.LANGUAGES[i].omit) langs[n++] = &G.LANGUAGES[i];
    qsort(langs, (size_t)n, sizeof(Language *), cmp_lang_label);
    UIDef *rows = UIR(CFG(.align = "cm", .padding = 0.05));
    UIDef *row = uinodes();
    for (int k = 1; k <= n; k++) {
        Language *v = langs[k - 1];
        if (!G.F_HIDE_BETA_LANGS || !v->beta) {
            uidef_add(row, UIC(CFG(.align = "cm", .func = G_FUNCS_beta_lang_alert, .padding = 0.05, .r = 0.1, .minh = 0.7, .minw = 4.5,
                                   .button = v->beta ? G_FUNCS_warn_lang : G_FUNCS_change_lang, .ref_table = v, .colour = v->beta ? G.C.RED : G.C.BLUE,
                                   .hover = true, .shadow = true, .focus_args = FOCUS(.snap_to = k == 1)),
                UIR(CFG(.align = "cm"), UIT(CFG(.text = (char *)v->label, .lang = v, .scale = 0.45, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)))));
        }
        if (row->count >= 3 || k == n) {
            uidef_add(rows, uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.1)), row));
            row = uinodes();
        }
    }
    free(langs);
    return GENERIC_OPTIONS(.contents = uidef_add(uinodes(), rows));
}
