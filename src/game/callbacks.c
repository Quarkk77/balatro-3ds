/* callbacks.c - G.FUNCS from button_callbacks.lua that are not widget or
 * menu-builder specific (shop, blind select, card use/sell/buy, overlay
 * menus, screen wipes, HUD updaters, profiles). */
#include "game.h"
#include <unistd.h>

#define EVFN(name) static bool name(Event *e_, void *p_)
#define LOC(k) ((char *)localize_key(k))
static void nofree(void *p) { (void)p; }

static void lock(const char *name, bool on) {
    if (on) strset_add(&G.CONTROLLER.locks.other, name);
    else strset_del(&G.CONTROLLER.locks.other, name);
}

static void set_button(UIElement *e, bool on, Col on_colour, UIFunc button) {
    if (on) { e->config->colour = on_colour; e->config->button = button; }
    else { e->config->colour = G.C.UI.BACKGROUND_INACTIVE; e->config->button = NULL; }
}

static bool is_card(void *p) { return p && node_is((Node *)p, NT_CARD); }

/* ------------------------------------------------------------------ */
/* overlay menu                                                        */
/* ------------------------------------------------------------------ */
void overlay_menu(UIDef *definition, OverlayArgs a) {
    if (!definition) return;
    if (G.OVERLAY_MENU) node_remove(&G.OVERLAY_MENU->n);
    G.CONTROLLER.locks.frame_set = true;
    G.CONTROLLER.locks.frame = true;
    G.CONTROLLER.cursor_down.target = NULL;
    Controller_mod_cursor_context_layer(&G.CONTROLLER, G.NO_MOD_CURSOR_STACK ? 0 : 1);
    UIBoxConfig *cfg = UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, a.has_offset_y ? a.offset_y : 10 }, .major = G.ROOM_ATTACH,
                              .bond = BOND_WEAK, .no_esc = a.no_esc);
    G.OVERLAY_MENU = uibox_new(definition, cfg);
    G.OVERLAY_MENU->no_esc = a.no_esc;
    G.OVERLAY_MENU->n.alignment.offset.y = 0;
    G.ROOM_jiggle += 1;
    Moveable_align_to_major(&G.OVERLAY_MENU->n);
}

/* G.SETTINGS.paused = true; G.FUNCS.overlay_menu{definition = ...}: the
 * definition must be built after pausing, as UIBoxes created unpaused stop
 * moving while paused (Moveable:move) and would stay where they spawned */
#define open_overlay(def) do { G.SETTINGS.paused = true; overlay_menu((def), (OverlayArgs){ 0 }); } while (0)

GF_IMPL(exit_overlay_menu) {
    if (!G.OVERLAY_MENU) return;
    G.CONTROLLER.locks.frame_set = true;
    G.CONTROLLER.locks.frame = true;
    Controller_mod_cursor_context_layer(&G.CONTROLLER, -1000);
    node_remove(&G.OVERLAY_MENU->n);
    G.OVERLAY_MENU = NULL;
    G.VIEWING_DECK = false;
    G.SETTINGS.paused = false;
    game_save_settings();
}

GF_IMPL(continue_unlock) {
    G_FUNCS_exit_overlay_menu(NULL);
    Controller_mod_cursor_context_layer(&G.CONTROLLER, -2000);
    event_manager_update(0, true);
}

GF_IMPL(options) { open_overlay(create_UIBox_options()); }
GF_IMPL(run_info) { open_overlay(UIDEF_run_info()); }
GF_IMPL(deck_info) {
    G.SETTINGS.paused = true;
    if (G.deck_preview) { node_remove(&G.deck_preview->n); G.deck_preview = NULL; }
    overlay_menu(UIDEF_deck_info(G.STATE == STATE_SELECTING_HAND || G.STATE == STATE_HAND_PLAYED || G.STATE == STATE_DRAW_TO_HAND), (OverlayArgs){ 0 });
}
void G_FUNCS_settings_instant(bool instant) {
    G.SETTINGS.paused = true;
    overlay_menu(create_UIBox_settings(), (OverlayArgs){ .has_offset_y = true, .offset_y = instant ? 0 : 10 });
}
GF_IMPL(settings) { G_FUNCS_settings_instant(false); }
GF_IMPL(show_credits) { open_overlay(UIDEF_credits()); }
GF_IMPL(language_selection) { open_overlay(UIDEF_language_selector()); }
GF_IMPL(your_collection) { open_overlay(create_UIBox_your_collection()); }
GF_IMPL(your_collection_blinds) { open_overlay(create_UIBox_your_collection_blinds(false)); }
GF_IMPL(your_collection_jokers) { open_overlay(create_UIBox_your_collection_jokers()); }
GF_IMPL(your_collection_tarots) { open_overlay(create_UIBox_your_collection_tarots()); }
GF_IMPL(your_collection_planets) { open_overlay(create_UIBox_your_collection_planets()); }
GF_IMPL(your_collection_spectrals) { open_overlay(create_UIBox_your_collection_spectrals()); }
GF_IMPL(your_collection_vouchers) { open_overlay(create_UIBox_your_collection_vouchers(false)); }
GF_IMPL(your_collection_enhancements) { open_overlay(create_UIBox_your_collection_enhancements(false)); }
GF_IMPL(your_collection_decks) { open_overlay(create_UIBox_your_collection_decks()); }
GF_IMPL(your_collection_editions) { open_overlay(create_UIBox_your_collection_editions()); }
GF_IMPL(your_collection_tags) { open_overlay(create_UIBox_your_collection_tags()); }
GF_IMPL(your_collection_seals) { open_overlay(create_UIBox_your_collection_seals(false)); }
GF_IMPL(your_collection_boosters) { open_overlay(create_UIBox_your_collection_boosters()); }
GF_IMPL(high_scores) { open_overlay(create_UIBox_high_scores()); }
GF_IMPL(customize_deck) { open_overlay(create_UIBox_customize_deck()); }
GF_IMPL(usage) { open_overlay(UIDEF_usage_tabs()); }

GF_IMPL(challenge_list) {
    bool fgo = e && str_eq(e->config->id, "from_game_over");
    G.SETTINGS.paused = true;
    overlay_menu(UIDEF_challenge_list(fgo), (OverlayArgs){ 0 });
    if (fgo && G.OVERLAY_MENU) G.OVERLAY_MENU->no_esc = true;
}

GF_IMPL(setup_run) {
    const char *id = e ? e->config->id : NULL;
    bool special = str_eq(id, "from_game_over") || str_eq(id, "from_game_won") || str_eq(id, "challenge_list");
    G.SETTINGS.paused = true;
    overlay_menu(UIDEF_run_setup(special ? id : NULL), (OverlayArgs){ 0 });
    if ((str_eq(id, "from_game_over") || str_eq(id, "from_game_won")) && G.OVERLAY_MENU) G.OVERLAY_MENU->no_esc = true;
}

typedef struct { const char *id; } IdCtx;
EVFN(ev_unlock_notify) { (void)e_; (void)p_; unlock_notify(); return true; }
EVFN(ev_setup_after_unlocks) {
    IdCtx *c = p_; (void)e_;
    if (event_queue_length(Q_UNLOCK) <= 0 && !G.OVERLAY_MENU) {
        G.SETTINGS.paused = true;
        bool special = str_eq(c->id, "from_game_over") || str_eq(c->id, "from_game_won");
        overlay_menu(UIDEF_run_setup(special ? c->id : NULL), (OverlayArgs){ 0 });
        if (special && G.OVERLAY_MENU) G.OVERLAY_MENU->no_esc = true;
        return true;
    }
    return false;
}
GF_IMPL(notify_then_setup_run) {
    if (G.OVERLAY_MENU) { node_remove(&G.OVERLAY_MENU->n); G.OVERLAY_MENU = NULL; }
    add_event(EVENT(.blockable = 0, .func = ev_unlock_notify));
    IdCtx *c = CTX(IdCtx); c->id = e ? intern(e->config->id ? e->config->id : "") : "";
    add_event(EVENT(.blockable = 0, .func = ev_setup_after_unlocks, .ctx = c));
}

GF_IMPL(deck_view_challenge) {
    Challenge *ch = e ? e->config->data : NULL;
    int id = ch ? get_challenge_int_from_id(ch->id ? ch->id : "") : 0;
    overlay_menu(GENERIC_OPTIONS(.back_func = G_FUNCS_deck_info, .contents = uidef_add(uinodes(), UIDEF_challenge_description(id, false, true))), (OverlayArgs){ 0 });
}

GF_IMPL(profile_select) {
    G.SETTINGS.paused = true;
    G.focused_profile = G.SETTINGS.profile;
    for (int i = 1; i <= 3; i++)
        if (i != G.focused_profile && file_exists(res_save_path(tfmt("%d/profile.jkr", i)))) game_load_profile(i);
    game_load_profile(G.focused_profile);
    overlay_menu(UIDEF_profile_select(), (OverlayArgs){ 0 });
}

GF_IMPL(quit) { G.quit_requested = true; }

/* ------------------------------------------------------------------ */
/* small HUD / state checks                                            */
/* ------------------------------------------------------------------ */
GF_IMPL(tut_next) {
    if (G.OVERLAY_TUTORIAL) {
        CardCharacter *j = G.OVERLAY_TUTORIAL->Jimbo;
        if (j) { CardCharacter_remove_button(j); CardCharacter_remove_speech_bubble(j); }
        G.OVERLAY_TUTORIAL->step_complete = false;
        G.OVERLAY_TUTORIAL->step++;
    }
}

GF_IMPL(blueprint_compat) {
    Card *c = e->config->ref_table;
    if (!str_eq(c->ability.blueprint_compat, c->ability.blueprint_compat_check)) {
        if (str_eq(c->ability.blueprint_compat, "compatible")) e->config->colour = mix_colours(G.C.GREEN, G.C.JOKER_GREY, 0.8);
        else if (str_eq(c->ability.blueprint_compat, "incompatible")) e->config->colour = mix_colours(G.C.RED, G.C.JOKER_GREY, 0.8);
        str_set(&c->ability.blueprint_compat_ui, tfmt(" %s ", localize_key(tfmt("k_%s", c->ability.blueprint_compat ? c->ability.blueprint_compat : ""))));
        c->ability.blueprint_compat_check = c->ability.blueprint_compat;
    }
}

GF_IMPL(sort_hand_suit) { CardArea_sort(G.hand, "suit desc"); sound_play("paper1", NAN, NAN); }
GF_IMPL(sort_hand_value) { CardArea_sort(G.hand, "desc"); sound_play("paper1", NAN, NAN); }

GF_IMPL(can_buy) {
    Card *c = e->config->ref_table;
    if (c->cost > G.GAME.dollars - G.GAME.bankrupt_at && c->cost > 0) set_button(e, false, NULL, NULL);
    else set_button(e, true, G.C.ORANGE, G_FUNCS_buy_from_shop);
    UIElement *rp = e->config->ref_parent;
    if (rp) {
        Node *bau = child_get(&rp->n, "buy_and_use");
        if (bau) e->UIBox->n.alignment.offset.y = bau->states.visible ? -0.6 : 0;
    }
}

GF_IMPL(can_buy_and_use) {
    Card *c = e->config->ref_table;
    if ((c->cost > G.GAME.dollars - G.GAME.bankrupt_at && c->cost > 0) || !Card_can_use_consumeable(c, false, false)) {
        e->UIBox->n.states.visible = false;
        set_button(e, false, NULL, NULL);
    } else {
        if (c->highlighted) e->UIBox->n.states.visible = true;
        set_button(e, true, G.C.SECONDARY_SET.Voucher, G_FUNCS_buy_from_shop);
    }
}

GF_IMPL(can_redeem) {
    Card *c = e->config->ref_table;
    set_button(e, !(c->cost > G.GAME.dollars - G.GAME.bankrupt_at), G.C.GREEN, G_FUNCS_use_card);
}

GF_IMPL(can_open) {
    Card *c = e->config->ref_table;
    set_button(e, !(c->cost > 0 && c->cost > G.GAME.dollars - G.GAME.bankrupt_at), G.C.GREEN, G_FUNCS_use_card);
}

GF_IMPL(redeem_from_shop) { G_FUNCS_use_card(e); }
GF_IMPL(open_booster) { G_FUNCS_use_card(e); }

GF_IMPL(HUD_blind_visible) {
    Blind *b = G.GAME.blind;
    if (!b) return;
    b->n.states.visible = b->name && b->name[0] && b->blind_set;
}

GF_IMPL(HUD_blind_debuff) {
    Blind *b = G.GAME.blind;
    UIElement *p = e->parent_uie;
    if (!p) return;
    if (b && b->loc_debuff_text && b->loc_debuff_text[0]) {
        if (p->config->minh == 0 || !str_eq(e->config->str, b->loc_debuff_text)) {
            p->config->minh = 0.35;
            e->config->scale = 0.36;
            const char *line = ref_tostring(e->config->ref);
            if (!line || !line[0] || !strcmp(line, "nil")) { e->config->scale = UI_SCALE_ZERO; p->config->minh = 0.001; }
            str_set(&e->config->str, b->loc_debuff_text);
            UIBox_recalculate(e->UIBox);
        }
    } else if (p->config->minh > 0) {
        p->config->minh = 0;
        e->config->scale = UI_SCALE_ZERO;
        UIBox_recalculate(e->UIBox);
    }
}

GF_IMPL(HUD_blind_debuff_prefix) {
    Blind *b = G.GAME.blind;
    char **slot = e->config->ref.p;
    if ((b && str_eq(b->name, "The Wheel") && !b->disabled) || str_eq(e->config->id, "bl_wheel")) {
        str_set(slot, num_str(G.GAME.probabilities.normal));
        e->config->scale = 0.32;
    } else {
        str_set(slot, "");
        e->config->scale = UI_SCALE_ZERO;
    }
}

GF_IMPL(HUD_blind_reward) {
    Blind *b = G.GAME.blind;
    int bt = b ? blind_type_index(Blind_get_type(b)) : -1;
    UIElement *c1 = uie_child(e, 1), *c2 = uie_child(e, 2);
    if (G.GAME.modifiers.has_no_blind_reward && bt >= 0 && G.GAME.modifiers.no_blind_reward[bt]) {
        if (e->config->minh > 0.44) {
            e->config->minh = 0.4;
            if (c1) { free(c1->config->text); c1->config->text = xstrdup(localize_key("k_no_reward")); }
            UIBox_recalculate(e->UIBox);
        }
    } else if (e->config->minh < 0.45) {
        e->config->minh = 0.45;
        if (c1) { free(c1->config->text); c1->config->text = xstrdup(tfmt("%s: ", localize_key("k_reward"))); }
        if (c2) c2->n.states.visible = true;
        UIBox_recalculate(e->UIBox);
    }
}

bool can_continue_check(UIElement *e) {
    if (!e->config->func) return false;
    bool ok = false;
    if (!file_exists(res_save_path(tfmt("%d/save.jkr", G.SETTINGS.profile)))) {
        e->config->colour = G.C.UI.BACKGROUND_INACTIVE;
        e->config->button = NULL;
    } else {
        if (G.SAVED_GAME.t == DV_NIL) {
            DVal sg;
            if (get_compressed(tfmt("%d/save.jkr", G.SETTINGS.profile), &sg)) G.SAVED_GAME = sg;
        }
        const char *ver = dv_get_str(&G.SAVED_GAME, "VERSION", NULL);
        if (!ver || strcmp(ver, "0.9.2") < 0) { e->config->colour = G.C.UI.BACKGROUND_INACTIVE; e->config->button = NULL; }
        else ok = true;
    }
    e->config->func = NULL;
    return ok;
}
GF_IMPL(can_continue) { can_continue_check(e); }

/* ------------------------------------------------------------------ */
/* profiles                                                            */
/* ------------------------------------------------------------------ */
GF_IMPL(can_load_profile) { set_button(e, G.SETTINGS.profile != G.focused_profile, G.C.BLUE, G_FUNCS_load_profile); }

typedef struct { bool del; } LoadProfCtx;
EVFN(ev_load_profile1) {
    LoadProfCtx *c = p_; (void)e_;
    game_delete_run();
    char *name = NULL;
    char **nref = profile_name_ref(G.focused_profile);
    if (*nref && (*nref)[0]) name = xstrdup(*nref);
    if (c->del) { dv_free(&G.PROFILES[G.focused_profile - 1]); G.PROFILES[G.focused_profile - 1] = dv_table(); }
    G.DISCOVER_TALLIES.present = false;
    G.PROGRESS.present = false;
    game_load_profile(G.focused_profile);
    str_set(nref, name ? name : "");
    if (name) dv_set_str(&G.PROFILES[G.focused_profile - 1], "name", name);
    free(name);
    game_init_item_prototypes();
    return true;
}
EVFN(ev_load_profile2) { (void)e_; (void)p_; game_main_menu(NULL); G.FILE_HANDLER.force = true; return true; }

void load_profile(bool delete_prof_data) {
    dv_free(&G.SAVED_GAME);
    event_clear_queue(-1, -1);
    G_FUNCS_wipe_on_text(NULL, false, 1, NULL);
    LoadProfCtx *c = CTX(LoadProfCtx); c->del = delete_prof_data;
    add_event(EVENT(.no_delete = true, .func = ev_load_profile1, .ctx = c));
    add_event(EVENT(.no_delete = true, .blockable = 1, .blocking = 0, .func = ev_load_profile2));
    G_FUNCS_wipe_off(NULL);
}
GF_IMPL(load_profile) { load_profile(false); }

static bool check_profile_data_set, check_profile_data;
GF_IMPL(can_delete_profile) {
    if (!check_profile_data_set) {
        check_profile_data = file_exists(res_save_path(tfmt("%d/profile.jkr", G.focused_profile)));
        check_profile_data_set = true;
    }
    if (!check_profile_data || e->disable_button) {
        check_profile_data = false;
        set_button(e, false, NULL, NULL);
    } else set_button(e, true, G.C.RED, G_FUNCS_delete_profile);
}
void reset_check_profile_data(void) { check_profile_data_set = false; }

typedef struct { UIElement *e; } UIECtx;
EVFN(ev_tarot2_076) { (void)e_; (void)p_; sound_play("tarot2", 0.76, 0.4); return true; }
EVFN(ev_enable_button) { UIECtx *c = p_; (void)e_; c->e->disable_button = false; return true; }

static void confirm_flash(UIElement *e) {
    e->disable_button = true;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.06, .blockable = 0, .blocking = 0, .func = ev_tarot2_076));
    UIECtx *c = CTX(UIECtx); c->e = e;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.35, .blockable = 0, .blocking = 0, .func = ev_enable_button, .ctx = c));
    sound_play("tarot2", 1, 0.4);
}

GF_IMPL(delete_profile) {
    UIElement *wt = UIBox_get_UIE_by_ID(e->UIBox, "warning_text", NULL);
    if (wt && wt->config->colour != G.C.WHITE) {
        node_juice_up(&wt->n, NAN, NAN);
        wt->config->colour = G.C.WHITE;
        wt->config->shadow = true;
        confirm_flash(e);
        return;
    }
    int p = G.focused_profile;
    static const char *files[4] = { "profile.jkr", "save.jkr", "meta.jkr", "unlock_notify.jkr" };
    for (int i = 0; i < 4; i++) remove(res_save_path(tfmt("%d/%s", p, files[i])));
    rmdir(res_save_path(tfmt("%d", p)));
    dv_free(&G.SAVED_GAME);
    G.DISCOVER_TALLIES.present = false;
    G.PROGRESS.present = false;
    dv_free(&G.PROFILES[p - 1]);
    G.PROFILES[p - 1] = dv_table();
    reset_check_profile_data();
    if (p == G.SETTINGS.profile) load_profile(true);
    else if (G.OVERLAY_MENU) G_FUNCS_change_tab(UIBox_get_UIE_by_ID(G.OVERLAY_MENU, tfmt("tab_but_%d", p), NULL));
}

GF_IMPL(can_unlock_all) {
    set_button(e, !(dv_get_bool(profile(), "all_unlocked", false) || e->disable_button), G.C.GREY, G_FUNCS_unlock_all);
}

GF_IMPL(unlock_all) {
    UIElement *io = G.OVERLAY_MENU ? UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "overlay_menu_infotip", NULL) : NULL;
    if (io && !io->config->filler && !G.F_NO_ACHIEVEMENTS) {
        if (io->config->object) node_remove(io->config->object);
        const char *rows[8]; int n = localize_list(G.F_TROPHIES ? "ml_unlock_all_trophies" : "ml_unlock_all_explanation", NULL, rows, 8);
        const char **heap = xcalloc(8, sizeof(char *)); memcpy(heap, rows, sizeof rows);
        UIBox *b = uibox_new(overlay_infotip(heap, n), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "bm", .parent = &io->n));
        io->config->object = &b->n;
        node_juice_up(&b->UIRoot->n, NAN, NAN);
        io->config->filler = true;   /* config.set */
        confirm_flash(e);
        return;
    }
    dv_set_bool(profile(), "all_unlocked", true);
    for (int i = 0; i < G.n_centers; i++) { Center *v = &G.P_CENTERS_LIST[i]; if (!v->demo && !v->wip) { v->alerted = v->discovered = v->unlocked = true; } }
    for (int i = 0; i < G.n_blinds; i++) { BlindProto *v = &G.P_BLINDS_LIST[i]; v->alerted = v->discovered = true; }
    for (int i = 0; i < G.n_tags; i++) { TagProto *v = &G.P_TAGS_LIST[i]; v->alerted = v->discovered = true; }
    set_profile_progress();
    set_discover_tallies();
    game_save_progress();
    G.FILE_HANDLER.force = true;
    if (G.OVERLAY_MENU) G_FUNCS_change_tab(UIBox_get_UIE_by_ID(G.OVERLAY_MENU, tfmt("tab_but_%d", G.focused_profile), NULL));
}

static ScoreEntry *round_score_by_name(const char *name) {
#define RS(n) if (str_eq(name, #n)) return &G.GAME.round_scores.n
    RS(furthest_ante); RS(furthest_round); RS(hand); RS(poker_hand); RS(new_collection); RS(cards_played); RS(cards_discarded);
    RS(times_rerolled); RS(cards_purchased);
#undef RS
    return NULL;
}

GF_IMPL(high_score_alert) {
    if (e->config->id && !child_get(&e->n, "alert")) {
        ScoreEntry *rs = round_score_by_name(e->config->id);
        if (rs && rs->high_score) {
            UIBox *b = uibox_new(CARD_ALERT(.no_bg = true, .text = localize_key("k_high_score_ex"), .scale = 0.3),
                                 UIBCFG(.instance_type = "ALERT", .align = "tri", .has_offset = true, .offset = { 0.3, -0.18 }, .major = &e->n, .parent = &e->n));
            b->n.states.collide.can = false;
            child_set(&e->n, "alert", &b->n);
        }
    }
}

GF_IMPL(beta_lang_alert) {
    if (!child_get(&e->n, "alert")) {
        Language *l = e->config->ref_table;
        if (l && l->beta) {
            UIBox *b = uibox_new(CARD_ALERT(.no_bg = true, .text = "BETA", .scale = 0.35),
                                 UIBCFG(.instance_type = "ALERT", .align = "tri", .has_offset = true, .offset = { 0.07, -0.07 }, .major = &e->n, .parent = &e->n));
            b->n.states.collide.can = false;
            child_set(&e->n, "alert", &b->n);
        }
    }
}

GF_IMPL(warn_lang) {
    Language *l = e->config->ref_table;
    UIElement *io = G.OVERLAY_MENU ? UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "overlay_menu_infotip", NULL) : NULL;
    if (io && !str_eq(io->config->str, l->label) && !G.F_NO_ACHIEVEMENTS) {
        if (io->config->object) node_remove(io->config->object);
        const char **rows = xcalloc(3, sizeof(char *));
        int n = 0;
        for (int i = 0; i < 3; i++) if (l->warning[i]) rows[n++] = l->warning[i];
        UIBox *b = uibox_new(overlay_infotip(rows, n), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "bm", .parent = &io->n));
        io->config->object = &b->n;
        node_juice_up(&b->UIRoot->n, NAN, NAN);
        str_set(&io->config->str, l->label);
        confirm_flash(e);
        e->config->button = G_FUNCS_change_lang;
    }
}

EVFN(ev_change_lang) {
    (void)e_; (void)p_;
    game_delete_run();
    game_init_item_prototypes();
    game_main_menu(NULL);
    return true;
}
GF_IMPL(change_lang) {
    Language *l = e->config->ref_table;
    if (!l || l == G.LANG) { G_FUNCS_exit_overlay_menu(NULL); return; }
    str_set(&G.SETTINGS.language, l->key);
    game_set_language();
    event_clear_queue(-1, -1);
    G_FUNCS_wipe_on(NULL);
    add_event(EVENT(.no_delete = true, .blockable = 1, .blocking = 0, .func = ev_change_lang));
    G_FUNCS_wipe_off(NULL);
}

GF_IMPL(copy_seed) { gfx_set_clipboard(G.GAME.pseudorandom.seed ? G.GAME.pseudorandom.seed : ""); }

GF_IMPL(start_setup_run) {
    if (G.OVERLAY_MENU) G_FUNCS_exit_overlay_menu(NULL);
    if (str_eq(G.SETTINGS.current_setup, "New Run")) {
        if (!G.GAME.won && !G.GAME.seeded) {
            if (G.SAVED_GAME.t == DV_TABLE) {
                if (!dv_truthy(dv_path(&G.SAVED_GAME, "GAME.won"))) {
                    DVal *cs = dv_get(dv_get(profile(), "high_scores"), "current_streak");
                    if (cs) dv_set_num(cs, "amt", 0);
                }
                game_save_settings();
            }
        }
        const char *seed = (G.run_setup_seed && G.setup_seed) ? G.setup_seed : G.forced_seed;
        double stake = G.forced_stake ? G.forced_stake : dv_get_num(dv_get(profile(), "MEMORY"), "stake", 1);
        start_run(e, (StartRunArgs){ .stake = stake, .seed = seed, .challenge = G.challenge_tab });
    } else if (str_eq(G.SETTINGS.current_setup, "Continue")) {
        if (G.SAVED_GAME.t == DV_TABLE) {
            DVal *copy = NEW(DVal);
            *copy = dv_copy(&G.SAVED_GAME);
            start_run(NULL, (StartRunArgs){ .savetext = copy });
        }
    }
}

GF_IMPL(start_challenge_run) {
    if (G.OVERLAY_MENU) G_FUNCS_exit_overlay_menu(NULL);
    int id = e && e->config->id ? atoi(e->config->id) : 0;
    start_run(e, (StartRunArgs){ .stake = 1, .challenge = (id >= 1 && id <= G_N_CHALLENGES) ? &G_CHALLENGES[id - 1] : NULL });
}

GF_IMPL(start_tutorial) {
    if (G.OVERLAY_MENU) G_FUNCS_exit_overlay_menu(NULL);
    dv_free(&G.SETTINGS.tutorial_progress);
    DVal tp;
    dv_parse("{forced_shop = {'j_joker', 'c_empress'}, forced_voucher = 'v_grabber', forced_tags = {'tag_handy', 'tag_garbage'}, hold_parts = {}, completed_parts = {}}", &tp);
    G.SETTINGS.tutorial_progress = tp;
    G.SETTINGS.tutorial_complete = false;
    start_run(e, (StartRunArgs){ 0 });
}

/* ------------------------------------------------------------------ */
/* HUD text updaters                                                   */
/* ------------------------------------------------------------------ */
double scale_number(double number, double scale, double max) {
    double sw = 100000000000.0;
    if (isnan(max)) max = 10000;
    if (number >= sw) scale = scale * floor(log10(max * 10)) / floor(log10(1000000.0 * 10));
    else if (number >= max) scale = scale * floor(log10(max * 10)) / floor(log10(number * 10));
    return scale;
}

GF_IMPL(chip_UI_set) {
    const char *nt = number_format(G.GAME.chips);
    if (!str_eq(G.GAME.chips_text, nt)) {
        e->config->scale = MIN(0.8, scale_number(G.GAME.chips, 1.1, NAN));
        str_set(&G.GAME.chips_text, nt);
    }
}

GF_IMPL(blind_chip_UI_scale) {
    if (G.GAME.blind) e->config->scale = scale_number(G.GAME.blind->chips, 0.7, 100000);
}

static void text_super_juice(UIElement *e, double amount) {
    DynaText *dt = (DynaText *)e->config->object;
    DynaText_set_quiver(dt, 0.03 * amount);
    DynaText_pulse(dt, 0.3 + 0.08 * amount);
    DynaText_update_text(dt, false);
    DynaText_align_letters(dt);
    UIElement_update_object(e);
}

GF_IMPL(hand_mult_UI_set) {
    CurrentRound *cr = &G.GAME.current_round;
    const char *nt = cr->current_hand.mult_text_is_str ? cr->current_hand.mult_text_override : number_format(cr->current_hand.mult);
    if (!str_eq(nt, cr->current_hand.mult_text)) {
        str_set(&cr->current_hand.mult_text, nt);
        DynaText *dt = (DynaText *)e->config->object;
        dt->scale = scale_number(cr->current_hand.mult, 0.9, 1000);
        DynaText_update_text(dt, false);
        if (!G.TAROT_INTERRUPT_PULSE) text_super_juice(e, MAX(0, floor(log10(cr->current_hand.mult > 0 ? cr->current_hand.mult : 1))));
    }
}

GF_IMPL(hand_chip_UI_set) {
    CurrentRound *cr = &G.GAME.current_round;
    const char *nt = cr->current_hand.chip_text_is_str ? cr->current_hand.chip_text_override : number_format(cr->current_hand.chips);
    if (!str_eq(nt, cr->current_hand.chip_text)) {
        str_set(&cr->current_hand.chip_text, nt);
        DynaText *dt = (DynaText *)e->config->object;
        dt->scale = scale_number(cr->current_hand.chips, 0.9, 1000);
        DynaText_update_text(dt, false);
        if (!G.TAROT_INTERRUPT_PULSE) text_super_juice(e, MAX(0, floor(log10(cr->current_hand.chips > 0 ? cr->current_hand.chips : 1))));
    }
}

GF_IMPL(hand_chip_total_UI_set) {
    CurrentRound *cr = &G.GAME.current_round;
    if (cr->current_hand.chip_total < 1) {
        str_set(&cr->current_hand.chip_total_text, "");
    } else {
        const char *nt = number_format(cr->current_hand.chip_total);
        if (!str_eq(nt, cr->current_hand.chip_total_text)) {
            DynaText *dt = (DynaText *)e->config->object;
            dt->scale = scale_number(cr->current_hand.chip_total, 0.95, 100000000);
            str_set(&cr->current_hand.chip_total_text, nt);
            if (!G.has_ARGS_hand_chip_total_UI_set || G.ARGS_hand_chip_total_UI_set < cr->current_hand.chip_total)
                text_super_juice(e, floor(log10(cr->current_hand.chip_total)));
            G.ARGS_hand_chip_total_UI_set = cr->current_hand.chip_total;
            G.has_ARGS_hand_chip_total_UI_set = true;
        }
    }
}

GF_IMPL(flame_handler) {
    if (!G.UI_CHIPLICK) G.UI_CHIPLICK = col_new(1, 1, 1, 1);
    if (!G.UI_MULTLICK) G.UI_MULTLICK = col_new(1, 1, 1, 1);
    for (int i = 0; i < 3; i++) {
        double a = (G.C.UI_CHIPS[i] * 0.5 + G.C.YELLOW[i] * 0.5) + 0.1;
        double b = (G.C.UI_MULT[i] * 0.5 + G.C.YELLOW[i] * 0.5) + 0.1;
        G.UI_CHIPLICK[i] = (float)MIN(MAX(a * a, 0.1), 1);
        G.UI_MULTLICK[i] = (float)MIN(MAX(b * b, 0.1), 1);
    }
    FlameArgs *F;
    Col colour, accent;
    if (str_eq(e->config->id, "flame_chips")) { F = &G.ARGS_chip_flames; colour = G.C.UI_CHIPS; accent = G.UI_CHIPLICK; }
    else if (str_eq(e->config->id, "flame_mult")) { F = &G.ARGS_mult_flames; colour = G.C.UI_MULT; accent = G.UI_MULTLICK; }
    else return;
    Node *obj = e->config->object;
    if (!obj || !node_is(obj, NT_SPRITE) || obj->ID != F->sprite_id) {
        if (obj) node_remove(obj);
        Sprite *s = sprite_new(0, 0, 2.5, 2.5, atlas_get("ui_1"), (SpritePos){ 2, 0 });
        e->config->object = &s->n;
        F->sprite_id = s->n.ID;
        F->intensity = 0; F->real_intensity = 0; F->intensity_vel = 0;
        F->colour_1 = colour; F->colour_2 = accent;
        F->timer = G.TIMERS.REAL;
        Moveable_set_alignment(&s->n, ALIGN_ARGS(.major = e->n.parent, .type = "bmi", .has_offset = true, .offset = { 0, 0 }, .bond = BOND_WEAK));
        ShaderSend *sends = xcalloc(7, sizeof(ShaderSend));
        sends[0] = (ShaderSend){ .name = "time", .ref = REFN(&F->timer) };
        sends[1] = (ShaderSend){ .name = "amount", .ref = REFN(&F->real_intensity) };
        sends[2] = (ShaderSend){ .name = "image_details", .kind = 4, .ptr = s->image_dims };
        sends[3] = (ShaderSend){ .name = "texture_details", .kind = 5, .ptr = s };
        sends[4] = (ShaderSend){ .name = "colour_1", .ref = REFCOL(&F->colour_1) };
        sends[5] = (ShaderSend){ .name = "colour_2", .ref = REFCOL(&F->colour_2) };
        sends[6] = (ShaderSend){ .name = "id", .kind = 0, .val = { (float)s->n.ID } };
        DrawStep step = { .shader = "flame", .send = sends, .nsend = 7 };
        Sprite_define_draw_steps(s, &step, 1);
        float tmp[4]; Sprite_get_pos_pixel(s, tmp);
        obj = &s->n;
    }
    double exptime = exp(-0.4 * G.real_dt);
    double earned = G.ARGS_score_intensity.earned_score, required = G.ARGS_score_intensity.required_score;
    if (earned >= required && required > 0)
        F->intensity = ((G.pack_cards && !G.pack_cards->n.REMOVED) || G.TAROT_INTERRUPT) ? 0 : MAX(0., log(earned) / log(5) - 2);
    else F->intensity = 0;
    F->timer = F->timer + G.real_dt * (1 + F->intensity * 0.2);
    if (F->intensity_vel < 0) F->intensity_vel = F->intensity_vel * (1 - 10 * G.real_dt);
    F->intensity_vel = (1 - exptime) * (F->intensity - F->real_intensity) * G.real_dt * 25 + exptime * F->intensity_vel;
    F->real_intensity = MAX(0, F->real_intensity + F->intensity_vel);
    F->change = F->change * (1 - 4. * G.real_dt) + (4. * G.real_dt) * (F->real_intensity < F->intensity - 0.0 ? 1 : 0) * F->real_intensity;
}

GF_IMPL(hand_text_UI_set) {
    CurrentRound *cr = &G.GAME.current_round;
    const char *hn = cr->current_hand.handname ? cr->current_hand.handname : "";
    if (!cr->current_hand.handname_text || strcmp(hn, cr->current_hand.handname_text) != 0) {   /* nil ~= "" in Lua */
        str_set(&cr->current_hand.handname_text, hn);
        DynaText *dt = (DynaText *)e->config->object;
        size_t len = strlen(hn);   /* Lua :len() counts bytes */
        if (len >= 13) dt->scale = 12 * 0.56 / len;
        else dt->scale = 2.4 / sqrt(len + 5);
        DynaText_update_text(dt, false);
    }
}

GF_IMPL(can_play) {
    int n = G.hand->highlighted.count;
    set_button(e, !(n <= 0 || G.GAME.blind->block_play || n > 5), G.C.BLUE, G_FUNCS_play_cards_from_highlighted);
}

GF_IMPL(visible_blind) { e->n.states.visible = G.GAME.blind && G.GAME.blind->config.blind; }

GF_IMPL(can_reroll) {
    double rc = G.GAME.current_round.reroll_cost;
    set_button(e, !((G.GAME.dollars - G.GAME.bankrupt_at) - rc < 0 && rc != 0), G.C.GREEN, G_FUNCS_reroll_shop);
}

GF_IMPL(discard_cards_from_highlighted_button) { G_FUNCS_discard_cards_from_highlighted(e, false); }
GF_IMPL(can_discard) {
    set_button(e, !(G.GAME.current_round.discards_left <= 0 || G.hand->highlighted.count <= 0), G.C.RED, G_FUNCS_discard_cards_from_highlighted_button);
}

GF_IMPL(can_use_consumeable) {
    Card *c = e->config->ref_table;
    set_button(e, Card_can_use_consumeable(c, false, false), G.C.RED, G_FUNCS_use_card);
}

GF_IMPL(can_select_card) {
    Card *c = e->config->ref_table;
    bool ok = !str_eq(c->ability.set, "Joker") || (c->edition && c->edition->negative) || G.jokers->cards.count < G.jokers->config.card_limit;
    set_button(e, ok, G.C.GREEN, G_FUNCS_use_card);
}

GF_IMPL(can_sell_card) {
    Card *c = e->config->ref_table;
    set_button(e, Card_can_sell_card(c), G.C.GREEN, G_FUNCS_sell_card);
}

GF_IMPL(can_skip_booster) {
    bool ok = G.pack_cards && G.pack_cards->cards.count > 0 &&
              (G.STATE == STATE_PLANET_PACK || G.STATE == STATE_STANDARD_PACK || G.STATE == STATE_BUFFOON_PACK ||
               (G.hand && (G.hand->cards.count > 0 || G.hand->config.card_limit <= 0)));
    if (ok) { e->config->colour = G.C.GREY; e->config->button = G_FUNCS_skip_booster; }
    else set_button(e, false, NULL, NULL);
}

GF_IMPL(show_infotip) {
    if (e->config->ref_table) {
        UIDef *nodes = e->config->ref_table;
        UIBox *b = uibox_new(uidef_add_all(UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR, .padding = 0.02)), nodes),
                             UIBCFG(.has_offset = true, .offset = { -0.03, 0 }, .align = "cl", .parent = &e->n));
        child_set(&e->n, "info", &b->n);
        Moveable_align_to_major(&b->n);
        e->config->ref_table = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* use / sell / buy                                                    */
/* ------------------------------------------------------------------ */
typedef struct { UIElement *e; Card *card; CardArea *area; int prev_state; bool dont_dissolve; double delay_fac; } UseCtx;

EVFN(ev_use_reenable) { UseCtx *c = p_; (void)e_; c->e->disable_button = false; c->e->config->button = G_FUNCS_use_card; return true; }

static void restore_offset(UIBox *b) {
    if (b && b->n.alignment.has_offset_py) {
        b->n.alignment.offset.y = b->n.alignment.offset_py;
        b->n.alignment.has_offset_py = false;
    }
}
static void stash_offset(UIBox *b, double to) {
    if (b && !b->n.alignment.has_offset_py) {
        b->n.alignment.offset_py = b->n.alignment.offset.y;
        b->n.alignment.has_offset_py = true;
        b->n.alignment.offset.y = to;
    }
}

EVFN(ev_use_focus2) {
    UseCtx *c = p_; (void)e_;
    G.CONTROLLER.interrupt.focus = false;
    if (str_eq(c->card->ability.set, "Voucher")) {
        if (G.shop) Controller_snap_to_node(&G.CONTROLLER, &UIBox_get_UIE_by_ID(G.shop, "next_round_button", NULL)->n);
    } else if (c->area) Controller_recall_cardarea_focus_area(&G.CONTROLLER, c->area);
    return true;
}
EVFN(ev_use_focus1) {
    UseCtx *c = p_; (void)e_;
    UseCtx *n = CTX(UseCtx); *n = *c;
    add_event(EVENT(.func = ev_use_focus2, .ctx = n));
    return true;
}

static bool is_pack_state(int s) {
    return s == STATE_TAROT_PACK || s == STATE_PLANET_PACK || s == STATE_SPECTRAL_PACK || s == STATE_STANDARD_PACK || s == STATE_BUFFOON_PACK;
}

EVFN(ev_use_finish2) {
    UseCtx *c = p_; (void)e_;
    int prev = c->prev_state;
    G.STATE = prev;
    G.TAROT_INTERRUPT = 0;
    lock("use", false);
    if (is_pack_state(prev) && G.booster_pack) {
        if (c->area == G.consumeables) {
            G.booster_pack->n.alignment.offset.y = G.booster_pack->n.alignment.offset_py;
            G.booster_pack->n.alignment.has_offset_py = false;
        } else if (G.GAME.pack_choices > 1) {
            restore_offset(G.booster_pack);
            G.GAME.pack_choices -= 1;
        } else {
            G.CONTROLLER.interrupt.focus = true;
            if (prev == STATE_TAROT_PACK) inc_career_stat("c_tarot_reading_used", 1);
            if (prev == STATE_PLANET_PACK) inc_career_stat("c_planetarium_used", 1);
            end_consumeable(NULL, c->delay_fac);
        }
    } else {
        if (G.shop) { G.shop->n.alignment.offset.y = G.shop->n.alignment.offset_py; G.shop->n.alignment.has_offset_py = false; }
        if (G.blind_select) { G.blind_select->n.alignment.offset.y = G.blind_select->n.alignment.offset_py; G.blind_select->n.alignment.has_offset_py = false; }
        if (G.round_eval) { G.round_eval->n.alignment.offset.y = G.round_eval->n.alignment.offset_py; G.round_eval->n.alignment.has_offset_py = false; }
        if (c->area && c->area->cards.count > 0) {
            UseCtx *n = CTX(UseCtx); *n = *c;
            add_event(EVENT(.func = ev_use_focus1, .ctx = n));
        }
    }
    return true;
}
EVFN(ev_use_finish1) {
    UseCtx *c = p_; (void)e_;
    if (!c->dont_dissolve) Card_start_dissolve(c->card, NULL, 0, false, NAN, false);
    UseCtx *n = CTX(UseCtx); *n = *c;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_use_finish2, .ctx = n));
    return true;
}

void use_card(UIElement *e, bool mute, bool nosave) {
    e->config->button = NULL;
    Card *card = e->config->ref_table;
    CardArea *area = card->area;
    int prev_state = G.STATE;
    bool dont_dissolve = false;
    double delay_fac = 1;
    if (Card_check_use(card)) {
        UseCtx *c = CTX(UseCtx); c->e = e;
        add_event(EVENT(.func = ev_use_reenable, .ctx = c));
        return;
    }
    if (str_eq(card->ability.set, "Booster") && !nosave && G.STATE == STATE_SHOP) save_with_action(card->sort_id);
    G.TAROT_INTERRUPT = G.STATE;
    if (str_eq(card->ability.set, "Booster")) G.GAME.PACK_INTERRUPT = G.STATE;
    G.STATE = is_pack_state(G.STATE) ? G.STATE : STATE_PLAY_TAROT;
    lock("use", true);
    if (G.booster_pack && !G.booster_pack->n.alignment.has_offset_py && (card->ability.consumeable || !(G.GAME.pack_choices > 1)))
        stash_offset(G.booster_pack, G.ROOM->T->y + 29);
    stash_offset(G.shop, G.ROOM->T->y + 29);
    stash_offset(G.blind_select, G.ROOM->T->y + 39);
    stash_offset(G.round_eval, G.ROOM->T->y + 29);
    static const char *kids[3] = { "use_button", "sell_button", "price" };
    for (int i = 0; i < 3; i++) {
        Node *k = child_get(&card->n, kids[i]);
        if (k) { child_set(&card->n, kids[i], NULL); node_remove(k); }
    }
    if (card->area) CardArea_remove_card(card->area, card, false);
    const char *set = card->ability.set;
    if (card->ability.consumeable) {
        if (G.STATE == STATE_TAROT_PACK || G.STATE == STATE_PLANET_PACK || G.STATE == STATE_SPECTRAL_PACK) {
            card->n.T->x = G.hand->n.T->x + G.hand->n.T->w / 2 - card->n.T->w / 2;
            card->n.T->y = G.hand->n.T->y + G.hand->n.T->h / 2 - card->n.T->h / 2 - 0.5;
            discover_card(card->config.center);
        } else draw_card(G.hand, G.play, 1, "up", true, card, NAN, mute, false, NAN, false);
        delay(0.2);
        Card_use_consumeable(card, area, NULL);
        for (int i = 0; i < G.jokers->cards.count; i++)
            Card_calculate_joker(G.jokers->cards.items[i], JCTX(.using_consumeable = true, .consumeable = card));
    } else if (str_eq(set, "Enhanced") || str_eq(set, "Default")) {
        G.playing_card = G.playing_card + 1;
        CardArea_emplace(G.deck, card, NULL, false);
        sound_play("card1", 0.8, 0.6);
        sound_play("generic1", NAN, NAN);
        card->playing_card = G.playing_card;
        CardList one = { 0 }; vec_push(&one, card);
        playing_card_joker_effects(&one, 1);
        vec_free(&one);
        Card_add_to_deck(card, false);
        vec_push(&G.playing_cards, card);
        dont_dissolve = true;
        delay_fac = 0.2;
    } else if (str_eq(set, "Joker")) {
        Card_add_to_deck(card, false);
        CardArea_emplace(G.jokers, card, NULL, false);
        sound_play("card1", 0.8, 0.6);
        sound_play("generic1", NAN, NAN);
        dont_dissolve = true;
        delay_fac = 0.2;
    } else if (str_eq(set, "Booster")) {
        delay(0.1);
        if (card->ability.booster_pos >= 1 && card->ability.booster_pos <= 2) str_set(&G.GAME.current_round.used_packs[card->ability.booster_pos - 1], "USED");
        draw_card(G.hand, G.play, 1, "up", true, card, NAN, true, false, NAN, false);
        if (!card->from_tag) G.GAME.round_scores.cards_purchased.amt += 1;
        Card_open(card);
    } else if (str_eq(set, "Voucher")) {
        delay(0.1);
        draw_card(G.hand, G.play, 1, "up", true, card, NAN, true, false, NAN, false);
        G.GAME.round_scores.cards_purchased.amt += 1;
        Card_redeem(card);
    }
    if (str_eq(set, "Booster")) {
        lock("use", false);
        G.TAROT_INTERRUPT = 0;
    } else {
        UseCtx *c = CTX(UseCtx);
        *c = (UseCtx){ e, card, area, prev_state, dont_dissolve, delay_fac };
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_use_finish1, .ctx = c));
    }
}
GF_IMPL(use_card) { use_card(e, false, false); }

GF_IMPL(sell_card) {
    Card *card = e->config->ref_table;
    Card_sell_card(card);
    for (int i = 0; i < G.jokers->cards.count; i++)
        if (G.jokers->cards.items[i] != card) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.selling_card = true, .card = card));
}

GF_IMPL(skip_tutorial_section) {
    if (!G.OVERLAY_TUTORIAL) return;
    G.OVERLAY_TUTORIAL->skip_steps = true;
    if (G.OVERLAY_TUTORIAL->Jimbo) node_remove(&G.OVERLAY_TUTORIAL->Jimbo->n);
    if (G.OVERLAY_TUTORIAL->content) node_remove(&G.OVERLAY_TUTORIAL->content->n);
    node_remove(&G.OVERLAY_TUTORIAL->n);
    G.OVERLAY_TUTORIAL = NULL;
    event_clear_queue(Q_TUTORIAL, -1);
    DVal *tp = &G.SETTINGS.tutorial_progress;
    const char *section = dv_get_str(tp, "section", "");
    DVal *cp = dv_get_or_create_table(tp, "completed_parts");
    if (str_eq(section, "small_blind")) dv_set_bool(cp, "small_blind", true);
    else if (str_eq(section, "big_blind")) { dv_set_bool(cp, "big_blind", true); dv_set(tp, "forced_tags", dv_nil()); }
    else if (str_eq(section, "second_hand")) { dv_set_bool(cp, "second_hand", true); dv_set_bool(dv_get_or_create_table(tp, "hold_parts"), "second_hand", true); }
    else if (str_eq(section, "first_hand")) {
        static const char *parts[5] = { "first_hand", "first_hand_2", "first_hand_3", "first_hand_4", "first_hand_section" };
        for (int i = 0; i < 5; i++) dv_set_bool(cp, parts[i], true);
    } else if (str_eq(section, "shop")) { dv_set_bool(cp, "shop_1", true); dv_set(tp, "forced_voucher", dv_nil()); }
}

GF_IMPL(shop_voucher_empty) {
    if (G.shop_vouchers && (G.shop_vouchers->cards.count > 0 || G.GAME.current_round.voucher)) e->n.states.visible = false;
    else if (!str_eq(G.SETTINGS.language, "en-us")) e->n.states.visible = false;
    else e->n.states.visible = true;
}

bool check_for_buy_space(Card *card) {
    const char *set = card->ability.set;
    bool neg = card->edition && card->edition->negative;
    if (!str_eq(set, "Voucher") && !str_eq(set, "Enhanced") && !str_eq(set, "Default") &&
        !(str_eq(set, "Joker") && G.jokers->cards.count < G.jokers->config.card_limit + (neg ? 1 : 0)) &&
        !(card->ability.consumeable && G.consumeables->cards.count < G.consumeables->config.card_limit + (neg ? 1 : 0))) {
        alert_no_space(card, card->ability.consumeable ? G.consumeables : G.jokers);
        return false;
    }
    return true;
}

typedef struct { UIElement *e; Card *c1; bool buy_and_use; } BuyCtx;
EVFN(ev_buy_calc) { BuyCtx *c = p_; (void)e_; Card_calculate_joker(c->c1, JCTX(.buying_card = true, .card = c->c1)); return true; }
EVFN(ev_inflation) {
    (void)e_; (void)p_;
    for (int i = 0; i < G.I.CARD.count; i++) if (node_is(G.I.CARD.items[i], NT_CARD)) Card_set_cost((Card *)G.I.CARD.items[i]);
    return true;
}
EVFN(ev_buy) {
    BuyCtx *c = p_; (void)e_;
    Card *c1 = c->c1;
    if (c1->area) CardArea_remove_card(c1->area, c1, false);
    Card_add_to_deck(c1, false);
    Node *price = child_get(&c1->n, "price");
    if (price) { child_set(&c1->n, "price", NULL); node_remove(price); }
    Node *bb = child_get(&c1->n, "buy_button");
    if (bb) { child_set(&c1->n, "buy_button", NULL); node_remove(bb); }
    const char *set = c1->ability.set;
    if (str_eq(set, "Default") || str_eq(set, "Enhanced")) {
        inc_career_stat("c_playing_cards_bought", 1);
        G.playing_card = G.playing_card + 1;
        CardArea_emplace(G.deck, c1, NULL, false);
        c1->playing_card = G.playing_card;
        CardList one = { 0 }; vec_push(&one, c1);
        playing_card_joker_effects(&one, 1);
        vec_free(&one);
        vec_push(&G.playing_cards, c1);
    } else if (!c->buy_and_use) {
        if (c1->ability.consumeable) CardArea_emplace(G.consumeables, c1, NULL, false);
        else CardArea_emplace(G.jokers, c1, NULL, false);
        BuyCtx *n = CTX(BuyCtx); *n = *c;
        add_event(EVENT(.func = ev_buy_calc, .ctx = n));
    }
    G.GAME.round_scores.cards_purchased.amt += 1;
    if (c1->ability.consumeable) {
        if (str_eq(c1->config.center->set, "Planet")) inc_career_stat("c_planets_bought", 1);
        else if (str_eq(c1->config.center->set, "Tarot")) inc_career_stat("c_tarots_bought", 1);
    } else if (str_eq(set, "Joker")) G.GAME.current_round.jokers_purchased += 1;
    for (int i = 0; i < G.jokers->cards.count; i++) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.buying_card = true, .card = c1));
    if (G.GAME.modifiers.inflation) {
        G.GAME.inflation += 1;
        add_event(EVENT(.func = ev_inflation));
    }
    sound_play("card1", NAN, NAN);
    inc_career_stat("c_shop_dollars_spent", c1->cost);
    if (c1->cost != 0) ease_dollars(-c1->cost, false);
    Controller_save_cardarea_focus(&G.CONTROLLER, "jokers");
    Controller_recall_cardarea_focus(&G.CONTROLLER, "jokers");
    if (c->buy_and_use) use_card(c->e, true, false);
    return true;
}

GF_IMPL(buy_from_shop) {
    Card *c1 = e->config->ref_table;
    if (!is_card(c1)) return;
    bool bau = str_eq(e->config->id, "buy_and_use");
    if (!bau && !check_for_buy_space(c1)) { e->disable_button = false; return; }
    BuyCtx *c = CTX(BuyCtx); c->e = e; c->c1 = c1; c->buy_and_use = bau;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_buy, .ctx = c));
}

/* ------------------------------------------------------------------ */
/* shop / blind select / boosters                                      */
/* ------------------------------------------------------------------ */
EVFN(ev_shop_slide) {
    (void)e_; (void)p_;
    if (G.shop) G.shop->n.alignment.offset.y = G.ROOM->T->y + 29;
    if (G.SHOP_SIGN) G.SHOP_SIGN->n.alignment.offset.y = -15;
    return true;
}
EVFN(ev_shop_close) {
    (void)e_; (void)p_;
    if (G.shop) { node_remove(&G.shop->n); G.shop = NULL; }
    if (G.SHOP_SIGN) { node_remove(&G.SHOP_SIGN->n); G.SHOP_SIGN = NULL; }
    G.STATE_COMPLETE = false;
    G.STATE = STATE_BLIND_SELECT;
    lock("toggle_shop", false);
    return true;
}
GF_IMPL(toggle_shop) {
    stop_use();
    lock("toggle_shop", true);
    if (G.shop) {
        for (int i = 0; i < G.jokers->cards.count; i++) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.ending_shop = true));
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_shop_slide));
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.5, .func = ev_shop_close));
    }
}

typedef struct { BlindProto *blind; } SelBlindCtx;
EVFN(ev_sel_blind1) {
    (void)e_; (void)p_;
    if (G.blind_prompt_box) G.blind_prompt_box->n.alignment.offset.y = -10;
    if (G.blind_select) { G.blind_select->n.alignment.offset.y = 40; G.blind_select->n.alignment.offset.x = 0; }
    return true;
}
EVFN(ev_sel_blind2) {
    SelBlindCtx *c = p_; (void)e_;
    ease_round(1);
    inc_career_stat("c_rounds", 1);
    G.GAME.round_resets.blind = c->blind;
    G.GAME.round_resets.blind_states[blind_type_index(G.GAME.blind_on_deck)] = "Current";
    if (G.blind_select) node_remove(&G.blind_select->n);
    if (G.blind_prompt_box) node_remove(&G.blind_prompt_box->n);
    G.blind_select = NULL;
    G.blind_prompt_box = NULL;
    delay(0.2);
    return true;
}
EVFN(ev_new_round) { (void)e_; (void)p_; new_round(); return true; }

GF_IMPL(select_blind) {
    stop_use();
    if (!G.blind_select) return;
    G.GAME.facing_blind = true;
    static const char *ids[2] = { "prompt_dynatext1", "prompt_dynatext2" };
    for (int i = 0; i < 2; i++) {
        UIElement *pe = G.blind_prompt_box ? UIBox_get_UIE_by_ID(G.blind_prompt_box, ids[i], NULL) : NULL;
        if (pe && pe->config->object) {
            DynaText *dt = (DynaText *)pe->config->object;
            dt->pop_delay = 0;
            DynaText_pop_out(dt, 5);
        }
    }
    add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.2, .func = ev_sel_blind1));
    SelBlindCtx *c = CTX(SelBlindCtx); c->blind = e->config->ref_table;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_sel_blind2, .ctx = c));
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_new_round));
}

GF_IMPL(skip_booster) {
    for (int i = 0; i < G.jokers->cards.count; i++) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.skipping_booster = true));
    end_consumeable(e, 1);
}

EVFN(ev_remove_booster) { (void)e_; (void)p_; if (G.booster_pack) { node_remove(&G.booster_pack->n); G.booster_pack = NULL; } return true; }
EVFN(ev_remove_booster_fx) {
    (void)e_; (void)p_;
    if (G.booster_pack_sparkles) { node_remove(&G.booster_pack_sparkles->n); G.booster_pack_sparkles = NULL; }
    if (G.booster_pack_stars) { node_remove(&G.booster_pack_stars->n); G.booster_pack_stars = NULL; }
    if (G.booster_pack_meteors) { node_remove(&G.booster_pack_meteors->n); G.booster_pack_meteors = NULL; }
    return true;
}
EVFN(ev_snap_next_round) {
    (void)e_; (void)p_;
    if (G.shop) { UIElement *b = UIBox_get_UIE_by_ID(G.shop, "next_round_button", NULL); if (b) Controller_snap_to_node(&G.CONTROLLER, &b->n); }
    return true;
}
EVFN(ev_end_cons3) {
    (void)e_; (void)p_;
    restore_offset(G.shop);
    restore_offset(G.blind_select);
    restore_offset(G.round_eval);
    G.CONTROLLER.interrupt.focus = true;
    add_event(EVENT(.func = ev_snap_next_round));
    G.STATE = G.GAME.PACK_INTERRUPT;
    ease_background_colour_blind(G.GAME.PACK_INTERRUPT, NULL);
    G.GAME.PACK_INTERRUPT = 0;
    return true;
}
EVFN(ev_save_run) { (void)e_; (void)p_; save_run(); return true; }
typedef struct { double delayfac; } DelayCtx;
EVFN(ev_end_cons2) {
    DelayCtx *c = p_; (void)e_;
    G_FUNCS_draw_from_hand_to_deck();
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2 * c->delayfac, .func = ev_end_cons3));
    for (int i = 0; i < G.GAME.tags.count; i++)
        if (Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "new_blind_choice" }, NULL, NULL)) break;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2 * c->delayfac, .func = ev_save_run));
    return true;
}

void end_consumeable(UIElement *e, double delayfac) {
    (void)e;
    if (isnan(delayfac) || delayfac == 0) delayfac = 1;
    stop_use();
    if (G.booster_pack) {
        if (G.booster_pack_sparkles) Particles_fade(G.booster_pack_sparkles, 1 * delayfac, 0);
        if (G.booster_pack_stars) Particles_fade(G.booster_pack_stars, 1 * delayfac, 0);
        if (G.booster_pack_meteors) Particles_fade(G.booster_pack_meteors, 1 * delayfac, 0);
        G.booster_pack->n.alignment.offset.y = G.ROOM->T->y + 9;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2 * delayfac, .blocking = 0, .blockable = 0, .func = ev_remove_booster));
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1 * delayfac, .blocking = 0, .blockable = 0, .func = ev_remove_booster_fx));
    }
    delay(0.2 * delayfac);
    DelayCtx *c = CTX(DelayCtx); c->delayfac = delayfac;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2 * delayfac, .func = ev_end_cons2, .ctx = c));
}

typedef struct { Node *node; } NodeCtx;
EVFN(ev_snap_node) { NodeCtx *c = p_; (void)e_; Controller_snap_to_node(&G.CONTROLLER, c->node); return true; }

GF_IMPL(blind_choice_handler) {
    UIConfig *cfg = e->config;
    bool run_info = cfg->num != 0;
    int bt = cfg->id ? blind_type_index(cfg->id) : -1;
    if (run_info || !G.blind_select || G.blind_select->n.VT.y >= 10 || bt < 0 || !G.blind_select_opts[bt]) return;
    if (e->UIBox->n.role.xy_bond != BOND_WEAK) Moveable_set_role(&e->UIBox->n, ROLE_ARGS(.xy_bond = BOND_WEAK));
    int deck = (int)cfg->num2;   /* 0 nil, 1 on, 2 off */
    bool on_deck = str_eq(cfg->id, G.GAME.blind_on_deck);
    if (!((deck != 1 && on_deck) || (deck != 2 && !on_deck))) return;
    UIBox *choice = G.blind_select_opts[bt];
    UIElement *top = UIBox_get_UIE_by_ID(e->UIBox, "select_blind_button", NULL);
    UIElement *root_c1 = uie_child(e->UIBox->UIRoot, 1);
    UIElement *border = root_c1 ? uie_child(root_c1, 1) : NULL;
    UIElement *tag = UIBox_get_UIE_by_ID(e->UIBox, tfmt("tag_%s", cfg->id), NULL);
    UIElement *tagc = UIBox_get_UIE_by_ID(e->UIBox, "tag_container", NULL);
    bool shop1 = dv_truthy(dv_get(dv_get(&G.SETTINGS.tutorial_progress, "completed_parts"), "shop_1"));
    if (tagc && !G.SETTINGS.tutorial_complete && !shop1) tagc->n.states.visible = false;
    else if (tagc) tagc->n.states.visible = true;
    UIElement *tag2 = tag ? uie_child(tag, 2) : NULL;
    if (on_deck) {
        cfg->num2 = 1;
        cfg->draw_after = false;
        cfg->colour = G.C.CLEAR;
        if (border && border->parent_uie) {
            border->parent_uie->config->outline = 2;
            border->parent_uie->config->outline_colour = G.C.UI.TRANSPARENT_DARK;
            border->config->outline_colour = !isnan(border->config->outline) ? border->config->outline_colour : get_blind_main_colour(cfg->id);
            border->config->outline = 1.5;
        }
        choice->n.alignment.offset.y = -0.9;
        if (tag && tagc) {
            UIElement *tc2 = uie_child(tagc, 2);
            if (tc2) { tc2->config->draw_after = false; tc2->config->colour = G.C.BLACK; }
            if (tag2) {
                tag2->config->button = G_FUNCS_skip_blind;
                tag2->config->hover = true;
                tag2->config->colour = G.C.RED;
                UIElement *t21 = uie_child(tag2, 1);
                if (t21) t21->config->colour = G.C.UI.TEXT_LIGHT;
            }
            tag->config->outline_colour = adjust_alpha(G.C.BLUE, 0.5);
            Sprite *sp = tag->config->ref_table;
            if (sp) sp->n.cfg_force_focus = false;
        }
        if (top) {
            NodeCtx *c = CTX(NodeCtx); c->node = &top->n;
            add_event(EVENT(.func = ev_snap_node, .ctx = c));
            top->config->button = G_FUNCS_select_blind;
            top->config->colour = G.C.FILTER;
            top->config->hover = true;
            UIElement *t1 = uie_child(top, 1);
            if (t1) t1->config->colour = G.C.WHITE;
        }
    } else {
        cfg->num2 = 2;
        cfg->draw_after = true;
        bool skipped = str_eq(G.GAME.round_resets.blind_states[bt], "Skipped");
        cfg->colour = adjust_alpha(skipped ? mix_colours(G.C.BLUE, G.C.L_BLACK, 0.1) : G.C.L_BLACK, 0.5);
        if (border && border->parent_uie) {
            border->parent_uie->config->outline = NAN;
            border->parent_uie->config->outline_colour = NULL;
            border->config->outline_colour = NULL;
            border->config->outline = NAN;
        }
        choice->n.alignment.offset.y = -0.2;
        if (tag && tagc) {
            const char *st = G.GAME.round_resets.blind_states[bt];
            if (str_eq(st, "Skipped") || str_eq(st, "Defeated")) {
                for (int k = 2; k >= 1; k--) {
                    UIElement *tk = uie_child(tagc, k);
                    if (tk) { Moveable_set_role(&tk->n, ROLE_ARGS(.xy_bond = BOND_WEAK)); UIElement_align(&tk->n, 0, 10); }
                }
            }
            if (skipped) {
                UIBox *b = uibox_new(CARD_ALERT(.text_rot = -0.35, .has_text_rot = true, .no_bg = true, .text = localize_key("k_skipped_cap"), .bump_amount = 1,
                                                .scale = 0.9, .maxw = 3.4),
                                     UIBCFG(.align = "tmi", .has_offset = true, .offset = { 0, 2.2 }, .major = &choice->n, .parent = &choice->n));
                child_set(&choice->n, "alert", &b->n);
            }
            if (tag2) {
                tag2->config->button = NULL;
                tag2->config->hover = false;
                tag2->config->colour = G.C.UI.BACKGROUND_INACTIVE;
                UIElement *t21 = uie_child(tag2, 1);
                if (t21) t21->config->colour = G.C.UI.TEXT_INACTIVE;
            }
            tag->config->outline_colour = G.C.UI.BACKGROUND_INACTIVE;
            Sprite *sp = tag->config->ref_table;
            if (sp) sp->n.cfg_force_focus = true;
        }
        if (top) {
            top->config->colour = G.C.UI.BACKGROUND_INACTIVE;
            top->config->button = NULL;
            top->config->hover = false;
            UIElement *t1 = uie_child(top, 1);
            if (t1) t1->config->colour = G.C.UI.TEXT_INACTIVE;
        }
    }
}

GF_IMPL(hover_tag_proxy) {
    UIElement *p = e->parent_uie;
    if (!p) return;
    Node *alert = child_get(&p->n, "alert");
    if ((e->n.states.hover.is || p->n.states.hover.is) && e->n.created_on_pause == G.SETTINGS.paused && !alert) {
        Tag *t = e->config->ref_table;
        Sprite *sp = Tag_get_uibox_table(t, NULL);
        UIBox *b = uibox_new(UIDEF_card_h_popup(&sp->n, sp->n.ability_UIBox_table),
                             UIBCFG(.align = "tm", .has_offset = true, .offset = { 0, -0.1 }, .major = &p->n, .instance_type = "POPUP"));
        child_set(&p->n, "alert", &b->n);
        node_juice_up(&sp->n, 0.05, 0.02);
        sound_play("paper1", math_random() * 0.1 + 0.55, 0.42);
        sound_play("tarot2", math_random() * 0.1 + 0.55, 0.09);
        b->n.states.collide.can = false;
    } else if (alert && ((!e->n.states.collide.is && !p->n.states.collide.is) || e->n.created_on_pause != G.SETTINGS.paused)) {
        child_set(&p->n, "alert", NULL);
        node_remove(alert);
    }
}

EVFN(ev_unlock_skip) { (void)e_; (void)p_; lock("skip_blind", false); return true; }
EVFN(ev_skip_effects) {
    (void)e_; (void)p_;
    delay(0.3);
    for (int i = 0; i < G.jokers->cards.count; i++) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.skip_blind = true));
    save_run();
    for (int i = 0; i < G.GAME.tags.count; i++) Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "immediate" }, NULL, NULL);
    for (int i = 0; i < G.GAME.tags.count; i++)
        if (Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "new_blind_choice" }, NULL, NULL)) break;
    return true;
}
GF_IMPL(skip_blind) {
    stop_use();
    lock("skip_blind", true);
    add_event(EVENT(.no_delete = true, .trigger = TRIG_AFTER, .blocking = 0, .blockable = 0, .delay = 2.5, .timer = TIMER_TOTAL, .func = ev_unlock_skip));
    UIElement *tag = UIBox_get_UIE_by_ID(e->UIBox, "tag_container", NULL);
    G.GAME.skips += 1;
    if (tag) {
        add_tag(tag->config->ref_table);
        const char *skipped = G.GAME.blind_on_deck ? G.GAME.blind_on_deck : "Small";
        const char *skip_to = str_eq(skipped, "Small") ? "Big" : "Boss";
        G.GAME.round_resets.blind_states[blind_type_index(skipped)] = "Skipped";
        G.GAME.round_resets.blind_states[blind_type_index(skip_to)] = "Select";
        G.GAME.blind_on_deck = skip_to;
        sound_play("generic1", NAN, NAN);
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_skip_effects));
    }
}

static void set_child_shadow(UIElement *e, int i, bool v) {
    UIElement *c = uie_child(e, i);
    UIElement *cc = c ? uie_child(c, 1) : NULL;
    if (cc) cc->config->shadow = v;
}
GF_IMPL(reroll_boss_button) {
    bool ok = (G.GAME.dollars - G.GAME.bankrupt_at) - 10 >= 0 &&
              (strset_has(&G.GAME.used_vouchers, "v_retcon") || (strset_has(&G.GAME.used_vouchers, "v_directors_cut") && !G.GAME.round_resets.boss_rerolled));
    set_button(e, ok, G.C.RED, G_FUNCS_reroll_boss);
    set_child_shadow(e, 1, ok);
    set_child_shadow(e, 2, ok);
}

EVFN(ev_boss_slide) {
    (void)e_; (void)p_;
    sound_play("other1", NAN, NAN);
    UIBox *b = G.blind_select_opts[BT_BOSS];
    if (b) { Moveable_set_role(&b->n, ROLE_ARGS(.xy_bond = BOND_WEAK)); b->n.alignment.offset.y = 20; }
    return true;
}
EVFN(ev_unlock_boss_reroll) { (void)e_; (void)p_; lock("boss_reroll", false); return true; }
EVFN(ev_boss_replace) {
    (void)e_; (void)p_;
    UIBox *old = G.blind_select_opts[BT_BOSS];
    UIElement *par = old ? (UIElement *)old->n.parent : NULL;
    G.GAME.round_resets.blind_choices[BT_BOSS] = get_new_boss();
    if (old) node_remove(&old->n);
    UIDef *inner = uidef_add(uinodes(), create_UIBox_blind_choice("Boss", false));
    Col main = get_blind_main_colour("Boss");
    UIBox *nb = uibox_new_T(UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR), UIBox_dyn_container(inner, false, main, mix_colours(G.C.BLACK, main, 0.8), false, 0)),
                            UIBCFG(.align = "bmi", .has_offset = true, .offset = { 0, G.ROOM->T->y + 9 }, .major = par ? &par->n : NULL, .xy_bond = BOND_WEAK),
                            par ? par->n.T->x : 0, 0, 0, 0);
    G.blind_select_opts[BT_BOSS] = nb;
    if (par) {
        par->config->object = &nb->n;
        UIBox_recalculate(nb);
        nb->n.parent = &par->n;
    }
    nb->n.alignment.offset.y = 0;
    add_event(EVENT(.blocking = 0, .trigger = TRIG_AFTER, .delay = 0.5, .func = ev_unlock_boss_reroll));
    save_run();
    for (int i = 0; i < G.GAME.tags.count; i++)
        if (Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "new_blind_choice" }, NULL, NULL)) break;
    return true;
}
GF_IMPL(reroll_boss) {
    stop_use();
    G.GAME.round_resets.boss_rerolled = true;
    if (!G.from_boss_tag) ease_dollars(-10, false);
    G.from_boss_tag = false;
    lock("boss_reroll", true);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_boss_slide));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.3, .func = ev_boss_replace));
}

EVFN(ev_reroll_shop1) {
    (void)e_; (void)p_;
    bool final_free = G.GAME.current_round.free_rerolls > 0;
    G.GAME.current_round.free_rerolls = MAX(G.GAME.current_round.free_rerolls - 1, 0);
    G.GAME.round_scores.times_rerolled.amt += 1;
    calculate_reroll_cost(final_free);
    for (int i = G.shop_jokers->cards.count - 1; i >= 0; i--) {
        Card *c = CardArea_remove_card(G.shop_jokers, G.shop_jokers->cards.items[i], false);
        if (c) node_remove(&c->n);
    }
    sound_play("coin2", NAN, NAN);
    sound_play("other1", NAN, NAN);
    int n = (int)G.GAME.shop.joker_max - G.shop_jokers->cards.count;
    for (int i = 1; i <= n; i++) {
        Card *nc = create_card_for_shop(G.shop_jokers);
        if (!nc) continue;
        CardArea_emplace(G.shop_jokers, nc, NULL, false);
        node_juice_up(&nc->n, NAN, NAN);
    }
    return true;
}
EVFN(ev_reroll_shop3) {
    (void)e_; (void)p_;
    G.CONTROLLER.interrupt.focus = false;
    lock("shop_reroll", false);
    Controller_recall_cardarea_focus(&G.CONTROLLER, "shop_jokers");
    for (int i = 0; i < G.jokers->cards.count; i++) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.reroll_shop = true));
    return true;
}
EVFN(ev_reroll_shop2) { (void)e_; (void)p_; add_event(EVENT(.func = ev_reroll_shop3)); return true; }

GF_IMPL(reroll_shop) {
    stop_use();
    lock("shop_reroll", true);
    if (Controller_save_cardarea_focus(&G.CONTROLLER, "shop_jokers")) G.CONTROLLER.interrupt.focus = true;
    if (G.GAME.current_round.reroll_cost > 0) {
        inc_career_stat("c_shop_dollars_spent", G.GAME.current_round.reroll_cost);
        inc_career_stat("c_shop_rerolls", 1);
        ease_dollars(-G.GAME.current_round.reroll_cost, false);
    }
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_reroll_shop1));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.3, .func = ev_reroll_shop2));
    add_event(EVENT(.func = ev_save_run));
}

EVFN(ev_cash_out1) {
    (void)e_; (void)p_;
    if (G.round_eval) { node_remove(&G.round_eval->n); G.round_eval = NULL; }
    G.GAME.current_round.jokers_purchased = 0;
    G.GAME.current_round.discards_left = MAX(0, G.GAME.round_resets.discards + G.GAME.round_bonus.discards);
    G.GAME.current_round.hands_left = MAX(1, G.GAME.round_resets.hands + G.GAME.round_bonus.next_hands);
    G.STATE = STATE_SHOP;
    G.GAME.shop_free = false;
    G.GAME.shop_d6ed = false;
    G.STATE_COMPLETE = false;
    return true;
}
EVFN(ev_cash_out2) { (void)e_; (void)p_; G.GAME.previous_round.dollars = G.GAME.dollars; return true; }

GF_IMPL(cash_out) {
    stop_use();
    if (G.round_eval) {
        e->config->button = NULL;
        G.round_eval->n.alignment.offset.y = G.ROOM->T->y + 15;
        G.round_eval->n.alignment.offset.x = 0;
        CardArea_shuffle(G.deck, tfmt("cashout%s", num_str(G.GAME.round_resets.ante)));
        CardArea_hard_set_cards(G.deck);
        delay(0.3);
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_cash_out1));
        ease_dollars(G.GAME.current_round.dollars, false);
        add_event(EVENT(.func = ev_cash_out2));
        sound_play("coin7", NAN, NAN);
        G.VIBRATION += 1;
    }
    ease_chips(0);
    if (str_eq(G.GAME.round_resets.blind_states[BT_BOSS], "Defeated")) {
        G.GAME.round_resets.blind_ante = G.GAME.round_resets.ante;
        G.GAME.round_resets.blind_tags[BT_SMALL] = get_next_tag_key(NULL);
        G.GAME.round_resets.blind_tags[BT_BIG] = get_next_tag_key(NULL);
    }
    reset_blinds();
    delay(0.6);
}

/* ------------------------------------------------------------------ */
/* run start / menu                                                    */
/* ------------------------------------------------------------------ */
EVFN(ev_delete_run) { (void)e_; (void)p_; game_delete_run(); return true; }
EVFN(ev_start_run) { StartRunArgs *a = p_; (void)e_; game_start_run(*a); return true; }

void start_run(UIElement *e, StartRunArgs args) {
    G.SETTINGS.paused = true;
    if (e && str_eq(e->config->id, "restart_button")) G.GAME.viewed_back = NULL;
    if (args.seed) args.seed = intern(args.seed);
    event_clear_queue(-1, -1);
    G_FUNCS_wipe_on(NULL);
    add_event(EVENT(.no_delete = true, .func = ev_delete_run));
    StartRunArgs *a = CTX(StartRunArgs); *a = args;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .no_delete = true, .func = ev_start_run, .ctx = a));
    G_FUNCS_wipe_off(NULL);
}
GF_IMPL(start_run) { start_run(e, (StartRunArgs){ 0 }); }

EVFN(ev_main_menu_game) { (void)e_; (void)p_; game_main_menu("game"); return true; }
GF_IMPL(go_to_menu) {
    G.SETTINGS.paused = true;
    event_clear_queue(-1, -1);
    G_FUNCS_wipe_on(NULL);
    add_event(EVENT(.no_delete = true, .func = ev_delete_run));
    add_event(EVENT(.no_delete = true, .blockable = 1, .blocking = 0, .func = ev_main_menu_game));
    G_FUNCS_wipe_off(NULL);
}

/* ------------------------------------------------------------------ */
/* screen wipe                                                         */
/* ------------------------------------------------------------------ */
EVFN(ev_wipe_flip) {
    (void)e_; (void)p_;
    if (G.screenwipecard) { Card_flip(G.screenwipecard); sound_play("cardFan2", NAN, NAN); }
    return true;
}

void G_FUNCS_wipe_on_text(const char **message, bool no_card, double timefac, Col alt_colour) {
    if (isnan(timefac) || timefac == 0) timefac = 1;
    if (G.screenwipe) return;
    G.CONTROLLER.locks.wipe = true;
    G.STAGE_OBJECT_INTERRUPT = true;
    G.screenwipe_black = HEX("4f6367FF");
    G.screenwipe_white = col_new(1, 1, 1, 1);
    if (!no_card) {
        G.screenwipecard = card_new(1, 1, G.CARD_W, G.CARD_H, random_p_card(), P_CENTER("c_base"), NULL);
        G.screenwipecard->sprite_facing = FACE_BACK;
        G.screenwipecard->facing = FACE_BACK;
        G.screenwipecard->n.states.hover.can = false;
        node_juice_up(&G.screenwipecard->n, 0.5, 1);
    }
    UIDef *msg = NULL;
    if (message) {
        msg = UIR(CFG(.id = "text", .align = "cm", .padding = 0.7));
        Col tc = MIN(G.C.BACKGROUND.C[0], G.C.BACKGROUND.C[1]) > 0.5 ? G.C.BLACK : G.C.WHITE;
        for (int k = 0; message[k]; k++)
            uidef_add(msg, UIR(CFG(.align = "cm"), UIO(CFG(.object = &DYNATEXT(DT_STR(message[k]), DT_COL1(tc), .shadow = true, .silent = k != 0, .float_ = true,
                                                                              .scale = 1.3, .pop_in = 0, .has_pop_in = true, .pop_in_rate = 2,
                                                                              .rotate = true)->n))));
    }
    RoleArgs *major = NEW(RoleArgs); *major = ROLE_ARGS(.role_type = ROLE_MAJOR);
    G.screenwipe = uibox_new(
        UIROOT(CFG(.align = "cm", .minw = 0, .minh = 0, .padding = 0.15, .r = 0.1, .colour = G.C.CLEAR),
            UIR(CFG(.align = "cm"),
                msg ? msg : UISKIP,
                UIIF(!no_card, UIO(CFG(.object = G.screenwipecard ? &G.screenwipecard->n : NULL, .role = major))))),
        UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, 0 }, .major = G.ROOM_ATTACH));
    Particles *p = particles_new(0, 0, 0, 0, (ParticlesConfig){ .timer = 0, .max = 1, .scale = 40, .speed = 0, .has_speed = true, .lifespan = 1.7 * timefac,
                                                                .attach = &G.screenwipe->n, .colours = dt_cols(1, alt_colour ? alt_colour : G.C.BACKGROUND.C),
                                                                .ncolours = 1 });
    child_set(&G.screenwipe->n, "particles", &p->n);
    G.STAGE_OBJECT_INTERRUPT = false;
    G.screenwipe->n.alignment.offset.y = 0;
    if (message) {
        UIElement *t = UIBox_get_UIE_by_ID(G.screenwipe, "text", NULL);
        for (int i = 1; t && i <= uie_child_count(t); i++) {
            UIElement *row = uie_child(t, i);
            UIElement *o = row ? uie_child(row, 1) : NULL;
            if (o && o->config->object) DynaText_pulse((DynaText *)o->config->object, NAN);
        }
    }
    add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.7, .no_delete = true, .blockable = 0, .func = ev_wipe_flip));
}
GF_IMPL(wipe_on) { G_FUNCS_wipe_on_text(NULL, false, 1, NULL); }

static double ease_linear(double t, void *ctx) { (void)ctx; return t; }
EVFN(ev_wipe_off1) {
    (void)e_; (void)p_;
    delay(0.3);
    if (!G.screenwipe) return true;
    Node *pn = child_get(&G.screenwipe->n, "particles");
    if (pn) ((Particles *)pn)->max = 0;
    add_event(EVENT(.trigger = TRIG_EASE, .no_delete = true, .blockable = 0, .blocking = 0, .timer = TIMER_REAL, .ref = REFF(&G.screenwipe_black[3]),
                    .ease_to = 0, .delay = 0.3, .ease_func = ease_linear));
    add_event(EVENT(.trigger = TRIG_EASE, .no_delete = true, .blockable = 0, .blocking = 0, .timer = TIMER_REAL, .ref = REFF(&G.screenwipe_white[3]),
                    .ease_to = 0, .delay = 0.3, .ease_func = ease_linear));
    return true;
}
EVFN(ev_wipe_off2) {
    (void)e_; (void)p_;
    if (G.screenwipecard) Card_start_dissolve(G.screenwipecard, (Col[]){ G.C.BLACK, G.C.ORANGE, G.C.GOLD, G.C.RED }, 4, false, NAN, false);
    UIElement *t = G.screenwipe ? UIBox_get_UIE_by_ID(G.screenwipe, "text", NULL) : NULL;
    for (int i = 1; t && i <= uie_child_count(t); i++) {
        UIElement *row = uie_child(t, i);
        UIElement *o = row ? uie_child(row, 1) : NULL;
        if (o && o->config->object) DynaText_pop_out((DynaText *)o->config->object, 4);
    }
    return true;
}
EVFN(ev_wipe_off3) {
    (void)e_; (void)p_;
    if (G.screenwipe) {
        Node *pn = child_get(&G.screenwipe->n, "particles");
        if (pn) { child_set(&G.screenwipe->n, "particles", NULL); node_remove(pn); }
        node_remove(&G.screenwipe->n);
    }
    G.screenwipe = NULL;
    G.screenwipecard = NULL;
    return true;
}
EVFN(ev_true) { (void)e_; (void)p_; return true; }

GF_IMPL(wipe_off) {
    add_event(EVENT(.no_delete = true, .func = ev_wipe_off1));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.55, .no_delete = true, .blocking = 0, .timer = TIMER_REAL, .func = ev_wipe_off2));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1.1, .no_delete = true, .blocking = 0, .timer = TIMER_REAL, .func = ev_wipe_off3));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1.2, .no_delete = true, .blocking = 1, .timer = TIMER_REAL, .func = ev_true));
    (void)nofree;
}
