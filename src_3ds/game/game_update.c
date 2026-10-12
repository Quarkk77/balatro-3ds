/* game_update.c - Game:update, Game:draw and the per-state update
 * functions from game.lua. */
#include "game.h"

/* ------------------------------------------------------------------ */
/* state updaters                                                      */
/* ------------------------------------------------------------------ */
static void remove_buttons(void) { if (G.buttons) { node_remove(&G.buttons->n); G.buttons = NULL; } }
static void remove_shop(void) { if (G.shop) { node_remove(&G.shop->n); G.shop = NULL; } }

static bool deck_hovered(void) {
    if (!G.deck || G.deck->cards.count == 0) return false;
    Card *c = G.deck->cards.items[0];
    return c->n.states.collide.is;
}

typedef struct { Card *card; double start; } RefocusCtx;
static bool ev_refocus(Event *e, void *p) {
    (void)e;
    RefocusCtx *c = p;
    if (c->card && !c->card->n.REMOVED && c->card->area && c->card->area == G.hand) {
        double x, y;
        node_put_focused_cursor(&c->card->n, &x, &y);
        Vec2 T = { x / (G.TILESCALE * G.TILESIZE), y / (G.TILESCALE * G.TILESIZE) };
        Controller_update_cursor(&G.CONTROLLER, &T);
    }
    return c->start + 0.4 < G.TIMERS.REAL;
}

static bool ev_deck_preview_watch(Event *e, void *p) {
    (void)e; (void)p;
    bool keep = (deck_hovered() && !G.CONTROLLER.HID.controller) || strset_has(&G.CONTROLLER.held_buttons, "triggerleft");
    if (G.deck_preview && !keep) {
        node_remove(&G.deck_preview->n);
        G.deck_preview = NULL;
        RefocusCtx *c = CTX(RefocusCtx);
        c->card = IS(G.CONTROLLER.focused.target, NT_CARD) ? (Card *)G.CONTROLLER.focused.target : NULL;
        c->start = G.TIMERS.REAL;
        add_event(EVENT(.func = ev_refocus, .ctx = c));
        return true;
    }
    return false;
}

static void update_selecting_hand(double dt) {
    (void)dt;
    if (!G.deck_preview && !G.OVERLAY_MENU) {
        bool hover = false;
        if (G.deck && G.deck->cards.count > 0) {
            Card *c = G.deck->cards.items[0];
            hover = c->n.states.collide.is && (!c->n.states.drag.is || G.CONTROLLER.HID.touch) && !G.CONTROLLER.HID.controller;
        }
        if (hover || strset_has(&G.CONTROLLER.held_buttons, "triggerleft")) {
            if (G.buttons) G.buttons->n.states.visible = false;
            G.deck_preview = uibox_new(UIDEF_deck_preview(false),
                                       UIBCFG(.align = "tm", .has_offset = true, .offset = { 0, -0.8 }, .major = &G.hand->n, .bond = BOND_WEAK));
            add_event(EVENT(.blocking = 0, .blockable = 0, .func = ev_deck_preview_watch));
        }
    }
    if (!G.buttons && !G.deck_preview)
        G.buttons = uibox_new(create_UIBox_buttons(), UIBCFG(.align = "bm", .has_offset = true, .offset = { 0, 0.3 }, .major = &G.hand->n, .bond = BOND_WEAK));
    if (G.buttons && !G.buttons->n.states.visible && !G.deck_preview) G.buttons->n.states.visible = true;

    if (G.hand->cards.count < 1 && G.deck->cards.count < 1 && G.play->cards.count < 1) end_round();

    remove_shop();
    if (!G.STATE_COMPLETE) {
        G.STATE_COMPLETE = true;
        if (G.hand->cards.count < 1 && G.deck->cards.count < 1) end_round();
        else {
            save_run();
            Controller_recall_cardarea_focus(&G.CONTROLLER, "hand");
        }
    }
}

static bool ev_save_run(Event *e, void *p) { (void)e; (void)p; save_run(); return true; }

static bool ev_shop_arrive(Event *e, void *p) {
    (void)e;
    bool shop_exists = *(bool *)p;
    if (!G.shop) return true;
    if (fabs(G.shop->n.T->y - G.shop->n.VT.y) >= 3) return false;
    G.ROOM_jiggle += 3;
    sound_play("cardFan2", NAN, NAN);
    for (int i = 0; i < G.GAME.tags.count; i++) Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "shop_start" }, NULL, NULL);
    bool nosave_shop = false;
    if (!shop_exists) {
        if (G.load_shop_jokers_set) {
            nosave_shop = true;
            CardArea_load(G.shop_jokers, &G.load_shop_jokers);
            for (int k = 0; k < G.shop_jokers->cards.count; k++) {
                Card *v = G.shop_jokers->cards.items[k];
                create_shop_card_ui(v, NULL, NULL);
                if (v->ability.consumeable) Card_start_materialize(v, NULL, 0, false, NAN);
                for (int t = 0; t < G.GAME.tags.count; t++)
                    if (Tag_apply_to_run(G.GAME.tags.items[t], (TagContext){ .type = "store_joker_modify", .card = v }, NULL, NULL)) break;
            }
            dv_free(&G.load_shop_jokers);
            G.load_shop_jokers_set = false;
        } else {
            int n = (int)G.GAME.shop.joker_max - G.shop_jokers->cards.count;
            for (int i = 0; i < n; i++) CardArea_emplace(G.shop_jokers, create_card_for_shop(G.shop_jokers), NULL, false);
        }

        if (G.load_shop_vouchers_set) {
            nosave_shop = true;
            CardArea_load(G.shop_vouchers, &G.load_shop_vouchers);
            for (int k = 0; k < G.shop_vouchers->cards.count; k++) {
                Card *v = G.shop_vouchers->cards.items[k];
                create_shop_card_ui(v, NULL, NULL);
                Card_start_materialize(v, NULL, 0, false, NAN);
            }
            dv_free(&G.load_shop_vouchers);
            G.load_shop_vouchers_set = false;
        } else if (G.GAME.current_round.voucher && P_CENTER(G.GAME.current_round.voucher)) {
            CardParams prm = { .bypass_discovery_center = true, .bypass_discovery_ui = true };
            Card *card = card_new(G.shop_vouchers->n.T->x + G.shop_vouchers->n.T->w / 2, G.shop_vouchers->n.T->y, G.CARD_W, G.CARD_H, NULL,
                                  P_CENTER(G.GAME.current_round.voucher), &prm);
            card->shop_voucher = true;
            create_shop_card_ui(card, "Voucher", G.shop_vouchers);
            Card_start_materialize(card, NULL, 0, false, NAN);
            CardArea_emplace(G.shop_vouchers, card, NULL, false);
        }

        if (G.load_shop_booster_set) {
            nosave_shop = true;
            CardArea_load(G.shop_booster, &G.load_shop_booster);
            for (int k = 0; k < G.shop_booster->cards.count; k++) {
                Card *v = G.shop_booster->cards.items[k];
                create_shop_card_ui(v, NULL, NULL);
                Card_start_materialize(v, NULL, 0, false, NAN);
            }
            dv_free(&G.load_shop_booster);
            G.load_shop_booster_set = false;
        } else {
            CurrentRound *cr = &G.GAME.current_round;
            for (int i = 0; i < 2; i++) {
                if (!cr->used_packs[i]) {
                    Center *pk = get_pack("shop_pack", NULL);
                    cr->used_packs[i] = xstrdup(pk ? pk->key : "p_arcana_normal_1");
                    if (cr->n_used_packs < i + 1) cr->n_used_packs = i + 1;
                }
                if (strcmp(cr->used_packs[i], "USED") != 0) {
                    CardParams prm = { .bypass_discovery_center = true, .bypass_discovery_ui = true };
                    Card *card = card_new(G.shop_booster->n.T->x + G.shop_booster->n.T->w / 2, G.shop_booster->n.T->y, G.CARD_W * 1.27,
                                          G.CARD_H * 1.27, NULL, P_CENTER(cr->used_packs[i]), &prm);
                    create_shop_card_ui(card, "Booster", G.shop_booster);
                    card->ability.booster_pos = i + 1;
                    Card_start_materialize(card, NULL, 0, false, NAN);
                    CardArea_emplace(G.shop_booster, card, NULL, false);
                }
            }
            for (int i = 0; i < G.GAME.tags.count; i++) Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "voucher_add" }, NULL, NULL);
            for (int i = 0; i < G.GAME.tags.count; i++) Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "shop_final_pass" }, NULL, NULL);
        }
    }
    UIElement *nb = UIBox_get_UIE_by_ID(G.shop, "next_round_button", NULL);
    if (nb) Controller_snap_to(&G.CONTROLLER, &nb->n, NULL);
    if (!nosave_shop) add_event(EVENT(.func = ev_save_run));
    return true;
}

static bool ev_shop_open(Event *e, void *p) {
    (void)e;
    if (G.shop) {
        G.shop->n.alignment.offset.y = -5.3;
        G.shop->n.alignment.offset.x = 0;
    }
    bool *c = CTX(bool);
    *c = *(bool *)p;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .blockable = 0, .func = ev_shop_arrive, .ctx = c));
    return true;
}

static void update_shop(double dt) {
    (void)dt;
    if (!G.STATE_COMPLETE) {
        stop_use();
        ease_background_colour_blind(STATE_SHOP, NULL);
        bool shop_exists = G.shop != NULL;
        if (!G.shop)
            G.shop = uibox_new(UIDEF_shop(), UIBCFG(.align = "tmi", .has_offset = true, .offset = { 0, G.ROOM->T->y + 11 },
                                                     .major = &G.hand->n, .bond = BOND_WEAK));
        bool *c = CTX(bool);
        *c = shop_exists;
        add_event(EVENT(.func = ev_shop_open, .ctx = c));
        G.STATE_COMPLETE = true;
    }
    remove_buttons();
}

static void update_play_tarot(double dt) { (void)dt; remove_buttons(); }

static bool ev_hand_played_next(Event *e, void *p) {
    (void)e; (void)p;
    if (G.GAME.chips - G.GAME.blind->chips >= 0 || G.GAME.current_round.hands_left < 1) G.STATE = STATE_NEW_ROUND;
    else G.STATE = STATE_DRAW_TO_HAND;
    G.STATE_COMPLETE = false;
    return true;
}

static void update_hand_played(double dt) {
    (void)dt;
    remove_buttons();
    remove_shop();
    if (!G.STATE_COMPLETE) {
        G.STATE_COMPLETE = true;
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_hand_played_next));
    }
}

static bool ev_to_selecting(Event *e, void *p) {
    (void)e; (void)p;
    G.STATE = STATE_SELECTING_HAND;
    G.STATE_COMPLETE = false;
    Blind_drawn_to_hand(G.GAME.blind);
    return true;
}

static bool ev_draw_to_hand(Event *e, void *p) {
    (void)e; (void)p;
    if (G_FUNCS_draw_from_deck_to_hand(NAN)) return true;
    if (G.GAME.current_round.hands_played == 0 && G.GAME.current_round.discards_used == 0 && G.GAME.facing_blind)
        for (int i = 0; i < G.jokers->cards.count; i++) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.first_hand_drawn = true));
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_to_selecting));
    return true;
}

static void update_draw_to_hand(double dt) {
    (void)dt;
    remove_buttons();
    remove_shop();
    if (!G.STATE_COMPLETE) {
        G.STATE_COMPLETE = true;
        for (int i = 0; i < G.GAME.tags.count; i++) Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "round_start_bonus" }, NULL, NULL);
        ease_background_colour_blind(STATE_DRAW_TO_HAND, NULL);
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_draw_to_hand));
    }
}

static void update_new_round(double dt) {
    (void)dt;
    remove_buttons();
    remove_shop();
    if (!G.STATE_COMPLETE) {
        G.STATE_COMPLETE = true;
        end_round();
    }
}

static bool ev_blind_select_show(Event *e, void *p) {
    (void)e; (void)p;
    sound_play("cancel", NAN, NAN);
    G.blind_select = uibox_new(create_UIBox_blind_select(),
                               UIBCFG(.align = "bmi", .has_offset = true, .offset = { 0, G.ROOM->T->y + 29 }, .major = &G.hand->n, .bond = BOND_WEAK));
    G.blind_select->n.alignment.offset.y = 0.8 - (G.hand->n.T->y - G.jokers->n.T->y) + G.blind_select->n.T->h;
    G.ROOM_jiggle += 3;
    G.blind_select->n.alignment.offset.x = 0;
    G.CONTROLLER.lock_input = false;
    for (int i = 0; i < G.GAME.tags.count; i++) Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "immediate" }, NULL, NULL);
    for (int i = 0; i < G.GAME.tags.count; i++)
        if (Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "new_blind_choice" }, NULL, NULL)) break;
    return true;
}
static bool ev_blind_select_outer(Event *e, void *p) {
    (void)e; (void)p;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_blind_select_show));
    return true;
}

static void update_blind_select(double dt) {
    (void)dt;
    remove_buttons();
    remove_shop();
    if (!G.STATE_COMPLETE) {
        stop_use();
        ease_background_colour_blind(STATE_BLIND_SELECT, NULL);
        add_event(EVENT(.func = ev_save_run));
        G.STATE_COMPLETE = true;
        G.CONTROLLER.interrupt.focus = true;
        add_event(EVENT(.func = ev_blind_select_outer));
    }
}

static bool ev_round_eval_slide(Event *e, void *p) {
    (void)e; (void)p;
    if (!G.round_eval) return true;
    if (G.round_eval->n.alignment.offset.y != -7.8) {
        G.round_eval->n.alignment.offset.y = -7.8;
        return false;
    }
    if (fabs(G.round_eval->n.T->y - G.round_eval->n.VT.y) < 3) {
        G.ROOM_jiggle += 3;
        sound_play("cardFan2", NAN, NAN);
        delay(0.1);
        G_FUNCS_evaluate_round();
        return true;
    }
    return false;
}

static bool ev_round_eval_show(Event *e, void *p) {
    (void)e; (void)p;
    G.GAME.facing_blind = false;
    save_run();
    ease_background_colour_blind(STATE_ROUND_EVAL, NULL);
    G.round_eval = uibox_new(create_UIBox_round_evaluation(),
                             UIBCFG(.align = "bm", .has_offset = true, .offset = { 0, G.ROOM->T->y + 19 }, .major = &G.hand->n, .bond = BOND_WEAK));
    G.round_eval->n.alignment.offset.x = 0;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_round_eval_slide));
    return true;
}

static void update_round_eval(double dt) {
    (void)dt;
    remove_buttons();
    remove_shop();
    if (!G.STATE_COMPLETE) {
        stop_use();
        G.STATE_COMPLETE = true;
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_round_eval_show));
    }
}

/* ---- booster packs ---- */
typedef struct { int kind; } PackCtx;   /* 0 arcana, 1 spectral, 2 standard, 3 buffoon, 4 celestial */

static bool ev_recall_pack(Event *e, void *p) { (void)e; (void)p; Controller_recall_cardarea_focus(&G.CONTROLLER, "pack_cards"); return true; }
static bool ev_pack_draw(Event *e, void *p) {
    (void)e;
    PackCtx *c = p;
    if (c->kind <= 1) G_FUNCS_draw_from_deck_to_hand(NAN);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.5, .func = ev_recall_pack));
    return true;
}

static void make_sparkles(double scale, double lifespan, double speed, Col *cols, int ncols) {
    G.booster_pack_sparkles = PARTICLES(1, 1, 0, 0, .timer = 0.015, .scale = scale, .initialize = true, .lifespan = lifespan, .speed = speed,
                                        .has_speed = true, .padding = -1, .attach = G.ROOM_ATTACH, .colours = cols, .ncolours = ncols, .fill = true);
    G.booster_pack_sparkles->fade_alpha = 1;
    Particles_fade(G.booster_pack_sparkles, 1, 0);
}

static bool ev_pack_open(Event *e, void *p) {
    (void)e;
    PackCtx *c = p;
    UIDef *def = NULL;
    int state = G.STATE;
    switch (c->kind) {
    case 0:
        make_sparkles(0.2, 1, 1.1, dt_cols(4, G.C.WHITE, lighten(G.C.PURPLE, 0.4), lighten(G.C.PURPLE, 0.2), lighten(G.C.GOLD, 0.2)), 4);
        def = create_UIBox_arcana_pack(); state = STATE_TAROT_PACK;
        break;
    case 1:
        make_sparkles(0.1, 3, 0.2, dt_cols(2, G.C.WHITE, lighten(G.C.GOLD, 0.2)), 2);
        def = create_UIBox_spectral_pack(); state = STATE_SPECTRAL_PACK;
        break;
    case 2:
        make_sparkles(0.3, 3, 0.2, dt_cols(2, G.C.BLACK, G.C.RED), 2);
        def = create_UIBox_standard_pack(); state = STATE_STANDARD_PACK;
        break;
    case 3:
        def = create_UIBox_buffoon_pack(); state = STATE_BUFFOON_PACK;
        break;
    case 4:
        ease_background_colour_blind(STATE_PLANET_PACK, NULL);
        G.booster_pack_stars = PARTICLES(1, 1, 0, 0, .timer = 0.07, .scale = 0.1, .initialize = true, .lifespan = 15, .speed = 0.1, .has_speed = true,
                                         .padding = -4, .attach = G.ROOM_ATTACH,
                                         .colours = dt_cols(3, G.C.WHITE, HEX("a7d6e0"), HEX("fddca0")), .ncolours = 3, .fill = true);
        G.booster_pack_meteors = PARTICLES(1, 1, 0, 0, .timer = 2, .scale = 0.05, .lifespan = 1.5, .speed = 4, .has_speed = true,
                                           .attach = G.ROOM_ATTACH, .colours = dt_cols(1, G.C.WHITE), .ncolours = 1, .fill = true);
        def = create_UIBox_celestial_pack(); state = STATE_PLANET_PACK;
        break;
    }
    G.booster_pack = uibox_new(def, UIBCFG(.align = "tmi", .has_offset = true, .offset = { 0, G.ROOM->T->y + 9 }, .major = &G.hand->n, .bond = BOND_WEAK));
    G.booster_pack->n.alignment.offset.y = -2.2;
    G.ROOM_jiggle += 3;
    if (c->kind == 4) {
        add_event(EVENT(.func = ev_recall_pack));
    } else {
        ease_background_colour_blind(state, NULL);
        PackCtx *n = CTX(PackCtx); n->kind = c->kind;
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_pack_draw, .ctx = n));
    }
    return true;
}

static void update_pack(int kind) {
    remove_buttons();
    if (G.shop) G.shop->n.alignment.offset.y = G.ROOM->T->y + 11;
    if (!G.STATE_COMPLETE) {
        G.STATE_COMPLETE = true;
        G.CONTROLLER.interrupt.focus = true;
        PackCtx *c = CTX(PackCtx); c->kind = kind;
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_pack_open, .ctx = c));
    }
}

/* ---- game over ---- */
static bool ev_game_over_jimbo(Event *e, void *p) {
    (void)e; (void)p;
    UIElement *spot = G.OVERLAY_MENU ? UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "jimbo_spot", NULL) : NULL;
    if (spot) {
        CardCharacter *jimbo = card_character_new(0, 5, NAN, NAN, NULL);
        if (spot->config->object) node_remove(spot->config->object);
        spot->config->object = &jimbo->n;
        jimbo->n.ui_object_updated = true;
        CardCharacter_add_speech_bubble(jimbo, tfmt("lq_%d", math_random_range(1, 10)), NULL, true, NULL);
        CardCharacter_say_stuff(jimbo, 5, false);
    }
    return true;
}

static void update_game_over(double dt) {
    (void)dt;
    if (!G.STATE_COMPLETE) {
        remove_save();
        if (G.GAME.round_resets.ante <= G.GAME.win_ante && !G.GAME.seeded && !G.GAME.challenge) {
            inc_career_stat("c_losses", 1);
            set_deck_loss();
            set_joker_loss();
        }
        sound_play("negative", 0.5, 0.7);
        sound_play("whoosh2", 0.9, 0.7);
        G.SETTINGS.paused = true;
        overlay_menu(create_UIBox_game_over(), (OverlayArgs){ .no_esc = true });
        G.ROOM_jiggle += 3;
        if (G.GAME.round_resets.ante <= G.GAME.win_ante)
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 2.5, .blocking = 0, .func = ev_game_over_jimbo));
        G.STATE_COMPLETE = true;
    }
}

/* ------------------------------------------------------------------ */
/* Game:update                                                         */
/* ------------------------------------------------------------------ */
static void boss_warning(void) {
    if (G.GAME.blind && G.boss_throw_hand && G.STATE == STATE_SELECTING_HAND) {
        if (!G.boss_warning_text) {
            DynaText *t1 = DYNATEXT(.scale = 0.7, DT_STR(localize_key("ph_unscored_hand")), .maxw = 9, DT_COL1(G.C.WHITE), .float_ = true,
                                    .shadow = true, .silent = true, .pop_in = 0, .has_pop_in = true, .pop_in_rate = 6);
            DynaText *t2 = DYNATEXT(.scale = 0.6, DT_STR(Blind_get_loc_debuff_text(G.GAME.blind)), .maxw = 9, DT_COL1(G.C.WHITE),
                                    .float_ = true, .shadow = true, .silent = true, .pop_in = 0, .has_pop_in = true, .pop_in_rate = 6);
            G.boss_warning_text = uibox_new(
                UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR, .padding = 0.2),
                    UIR(CFG(.align = "cm", .maxw = 1), UIO(CFG(.object = &t1->n))),
                    UIR(CFG(.align = "cm", .maxw = 1), UIO(CFG(.object = &t2->n)))),
                UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, -3.1 }, .major = &G.play->n));
            G.boss_warning_text->attention_text = true;
            G.boss_warning_text->n.states.collide.can = false;
            if (G.GAME.blind->animatedSprite) node_juice_up(&G.GAME.blind->animatedSprite->n, 0.05, 0.02);
            sound_play("chips1", math_random() * 0.1 + 0.55, 0.12);
        }
    } else {
        G.boss_throw_hand = false;
        if (G.boss_warning_text) {
            node_remove(&G.boss_warning_text->n);
            G.boss_warning_text = NULL;
        }
    }
}

void game_update(double dt) {
    G.FRAMES.MOVE++;
    if (!G.SETTINGS.tutorial_complete) G_FUNCS_tutorial_controller();
    sound_modulate(dt);
    update_canvas_juice(dt);
    G.TIMERS.REAL += dt;
    G.TIMERS.REAL_SHADER = G.SETTINGS.reduced_motion ? 300 : G.TIMERS.REAL;
    G.TIMERS.UPTIME += dt;
    G.SETTINGS.DEMO.total_uptime += dt;
    G.TIMERS.BACKGROUND += dt * G.ARGS_spin.amount;
    G.real_dt = dt;

    set_alerts();

    if (G.SETTINGS.paused) dt = 0;

    if (G.STATE != G.ACC_state) G.ACC = 0;
    G.ACC_state = G.STATE;
    if (G.STATE == STATE_HAND_PLAYED || G.STATE == STATE_NEW_ROUND) G.ACC = MIN(G.ACC + dt * 0.2 * G.SETTINGS.GAMESPEED, 16);
    else G.ACC = 0;

    G.SPEEDFACTOR = (G.STAGE == STAGE_RUN && !G.SETTINGS.paused && !G.screenwipe) ? G.SETTINGS.GAMESPEED : 1;
    G.SPEEDFACTOR += MAX(0, fabs(G.ACC) - 2);
    G.TIMERS.TOTAL += dt * G.SPEEDFACTOR;

    G.C.DARK_EDITION[0] = (float)(0.6 + 0.2 * sin(G.TIMERS.REAL * 1.3));
    G.C.DARK_EDITION[2] = (float)(0.6 + 0.2 * (1 - sin(G.TIMERS.REAL * 1.3)));
    G.C.DARK_EDITION[1] = MIN(G.C.DARK_EDITION[2], G.C.DARK_EDITION[0]);
    G.C.EDITION[0] = (float)(0.7 + 0.2 * (1 + sin(G.TIMERS.REAL * 1.5 + 0)));
    G.C.EDITION[2] = (float)(0.7 + 0.2 * (1 + sin(G.TIMERS.REAL * 1.5 + 3)));
    G.C.EDITION[1] = (float)(0.7 + 0.2 * (1 + sin(G.TIMERS.REAL * 1.5 + 6)));

    event_manager_update(G.real_dt, false);

    boss_warning();

    if (G.STATE == STATE_SELECTING_HAND) {
        if (G.hand->cards.count == 0 && G.deck->cards.count > 0) {
            G.STATE = STATE_DRAW_TO_HAND;
            G.STATE_COMPLETE = false;
        } else update_selecting_hand(dt);
    }
    if (G.STATE == STATE_SHOP) update_shop(dt);
    if (G.STATE == STATE_PLAY_TAROT) update_play_tarot(dt);
    if (G.STATE == STATE_HAND_PLAYED) update_hand_played(dt);
    if (G.STATE == STATE_DRAW_TO_HAND) update_draw_to_hand(dt);
    if (G.STATE == STATE_NEW_ROUND) update_new_round(dt);
    if (G.STATE == STATE_BLIND_SELECT) update_blind_select(dt);
    if (G.STATE == STATE_ROUND_EVAL) update_round_eval(dt);
    if (G.STATE == STATE_TAROT_PACK) update_pack(0);
    if (G.STATE == STATE_SPECTRAL_PACK) update_pack(1);
    if (G.STATE == STATE_STANDARD_PACK) update_pack(2);
    if (G.STATE == STATE_BUFFOON_PACK) update_pack(3);
    if (G.STATE == STATE_PLANET_PACK) update_pack(4);
    if (G.STATE == STATE_GAME_OVER) update_game_over(dt);

    for (int i = 0; i < G.ANIMATIONS.count; i++) {
        Node *v = G.ANIMATIONS.items[i];
        if (v) node_animate(v);
    }

    G.exp_times.xy = exp(-50 * G.real_dt);
    G.exp_times.scale = exp(-60 * G.real_dt);
    G.exp_times.r = exp(-190 * G.real_dt);
    double move_dt = MIN(1.0 / 20, G.real_dt);
    G.exp_times.max_vel = 70 * move_dt;

    for (int i = 0; i < G.MOVEABLES.count; i++) {
        Node *v = G.MOVEABLES.items[i];
        if (v->FRAME.MOVE < G.FRAMES.MOVE) node_move(v, move_dt);
    }
    for (int i = 0; i < G.MOVEABLES.count; i++) {
        Node *v = G.MOVEABLES.items[i];
        node_update(v, dt * G.SPEEDFACTOR);
        v->states.collide.is = false;
    }

    Controller_update(&G.CONTROLLER, G.real_dt);

    RoundResets *rr = &G.GAME.round_resets;
    if (G.prev_small_state != rr->blind_states[BT_SMALL] || G.prev_large_state != rr->blind_states[BT_BIG] ||
        G.prev_boss_state != rr->blind_states[BT_BOSS] || G.RESET_BLIND_STATES) {
        G.RESET_BLIND_STATES = false;
        G.prev_small_state = rr->blind_states[BT_SMALL];
        G.prev_large_state = rr->blind_states[BT_BIG];
        G.prev_boss_state = rr->blind_states[BT_BOSS];
        for (int i = 0; i < 3; i++)
            str_set(&rr->loc_blind_states[i], rr->blind_states[i] ? localize_cat(rr->blind_states[i], "blind_states") : "");
    }

    save_manager_update();
}

/* ------------------------------------------------------------------ */
/* Game:draw                                                           */
/* ------------------------------------------------------------------ */
/* 3DS screen layout:
 *  - the title (logo and its card) and the version label: top screen only
 *  - the opening Jimbo: bottom screen only
 *  - main menu (after the splash): buttons, menus opened from them and their
 *    cards and tooltips on the bottom screen only; the background and screen
 *    wipes on both
 *  - in a run: everything on both screens */
static int screen_for(Node *n) {
    if ((G.SPLASH_BACK && n == &G.SPLASH_BACK->n) || (G.SPLASH_FRONT && n == &G.SPLASH_FRONT->n) ||
        (G.screenwipe && n == &G.screenwipe->n))
        return GFX_SCREEN_BOTH;
    if (G.version_label && n == &G.version_label->n) return GFX_SCREEN_TOP;
    if (G.SPLASH_LOGO && n == &G.SPLASH_LOGO->n) return GFX_SCREEN_TOP;
    if (G.title_top && n == &G.title_top->n) return GFX_SCREEN_TOP;
    if (IS(n, NT_CARD)) {
        Card *c = (Card *)n;
        if (G.title_top && c->area == G.title_top) return GFX_SCREEN_TOP;
        if (G.splash_jimbo && c == G.splash_jimbo) return GFX_SCREEN_BOTTOM;
    }
    if (G.STAGE == STAGE_MAIN_MENU && G.STATE != STATE_SPLASH) return GFX_SCREEN_BOTTOM;
    return GFX_SCREEN_BOTH;
}

static void draw_node(Node *n) {
    if (!n || n->REMOVED) return;
    int prev = gfx_get_screen_mask();
    gfx_set_screen_mask(screen_for(n));
    gfx_push();
    Node_translate_container(n);
    node_draw(n);
    gfx_pop();
    gfx_set_screen_mask(prev);
}

void game_draw(void) {
    G.FRAMES.DRAW++;
    reset_drawhash();
    if (G.OVERLAY_TUTORIAL && !G.OVERLAY_MENU) G.under_overlay = true;

    gfx_set_canvas(G.CANVAS);
    gfx_push();
    gfx_scale(G.CANV_SCALE, G.CANV_SCALE);
    gfx_set_shader(NULL);
    gfx_clear(0, 0, 0, 1);

    if (G.SPLASH_BACK) draw_node(&G.SPLASH_BACK->n);

    for (int i = 0; i < G.I.NODE.count; i++) { Node *v = G.I.NODE.items[i]; if (!v->parent) draw_node(v); }
    for (int i = 0; i < G.I.MOVEABLE.count; i++) { Node *v = G.I.MOVEABLE.items[i]; if (!v->parent) draw_node(v); }
    if (G.SPLASH_LOGO) draw_node(&G.SPLASH_LOGO->n);

    Controller *C = &G.CONTROLLER;
    if (!G.OVERLAY_MENU || !G.F_HIDE_BG) {
        for (int i = 0; i < G.I.UIBOX.count; i++) {
            UIBox *v = G.I.UIBOX.items[i];
            if (!v->attention_text && !v->n.parent && v != G.OVERLAY_MENU && v != G.screenwipe && v != G.OVERLAY_TUTORIAL &&
                v != G.debug_tools && v != G.online_leaderboard && v != G.achievement_notification)
                draw_node(&v->n);
        }
        for (int i = 0; i < G.I.CARDAREA.count; i++) { Node *v = G.I.CARDAREA.items[i]; if (!v->parent) draw_node(v); }
        for (int i = 0; i < G.I.CARD.count; i++) {
            Node *v = G.I.CARD.items[i];
            if (!v->parent && v != C->dragging.target && v != C->focused.target) draw_node(v);
        }
        for (int i = 0; i < G.I.UIBOX.count; i++) {
            UIBox *v = G.I.UIBOX.items[i];
            if (v->attention_text && v != G.debug_tools && v != G.online_leaderboard && v != G.achievement_notification) draw_node(&v->n);
        }
        if (G.SPLASH_FRONT) draw_node(&G.SPLASH_FRONT->n);

        G.under_overlay = false;
        if (G.OVERLAY_TUTORIAL) {
            draw_node(&G.OVERLAY_TUTORIAL->n);
            for (int i = 0; i < G.OVERLAY_TUTORIAL->highlights.count; i++) {
                Node *v = G.OVERLAY_TUTORIAL->highlights.items[i];
                if (!v || v->REMOVED) continue;
                gfx_push();
                Node_translate_container(v);
                node_draw(v);
                if (v->vt->draw_children) {
                    if (v->vt->draw_self) v->vt->draw_self(v);
                    v->vt->draw_children(v);
                }
                gfx_pop();
            }
        }
    }
    if (G.OVERLAY_MENU && &G.OVERLAY_MENU->n != C->dragging.target) draw_node(&G.OVERLAY_MENU->n);
    if (G.debug_tools && &G.debug_tools->n != C->dragging.target) draw_node(&G.debug_tools->n);

    G.ALERT_ON_SCREEN = false;
    for (int i = 0; i < G.I.ALERT.count; i++) {
        draw_node(G.I.ALERT.items[i]);
        G.ALERT_ON_SCREEN = true;
    }

    if (C->dragging.target && C->dragging.target != C->focused.target) draw_node(C->dragging.target);
    if (C->focused.target && IS(C->focused.target, NT_CARD) &&
        (((Card *)C->focused.target)->area != G.hand || C->focused.target == C->dragging.target))
        draw_node(C->focused.target);

    for (int i = 0; i < G.I.POPUP.count; i++) draw_node(G.I.POPUP.items[i]);
    if (G.achievement_notification) draw_node(&G.achievement_notification->n);
    if (G.screenwipe) draw_node(&G.screenwipe->n);

    gfx_push();
    Node_translate_container(&G.CURSOR->n);
    gfx_translate(-G.CURSOR->n.T->w * G.TILESCALE * G.TILESIZE * 0.5, -G.CURSOR->n.T->h * G.TILESCALE * G.TILESIZE * 0.5);
    node_draw(&G.CURSOR->n);
    gfx_pop();

    gfx_pop();

    /* composite the game canvas to the screen through the CRT shader */
    gfx_set_canvas(G.AA_CANVAS);
    gfx_push();
    gfx_set_color(G.C.WHITE);
    GfxShader *crt = strmap_get(&G.SHADERS, "CRT");
    if (crt) {
        if (!G.ARGS_eased_cursor_set) {
            G.ARGS_eased_cursor_pos.x = G.CURSOR->n.T->x;
            G.ARGS_eased_cursor_pos.y = G.CURSOR->n.T->y;
            G.ARGS_eased_cursor_sx = G.CONTROLLER.cursor_position.x;
            G.ARGS_eased_cursor_sy = G.CONTROLLER.cursor_position.y;
            G.ARGS_eased_cursor_set = true;
        }
        G.screenwipe_amt = 0.95 * G.screenwipe_amt + 0.05 * ((G.screenwipe || G.screenglitch) ? 0.4 : 0);
        double crtv = G.SETTINGS.GRAPHICS.crt * 0.3;
        gfx_send_vec2(crt, "distortion_fac", (float)(1.0 + 0.07 * crtv / 100), (float)(1.0 + 0.1 * crtv / 100));
        gfx_send_vec2(crt, "scale_fac", (float)(1.0 - 0.008 * crtv / 100), (float)(1.0 - 0.008 * crtv / 100));
        gfx_send_float(crt, "feather_fac", 0.01f);
        gfx_send_float(crt, "bloom_fac", (float)(G.SETTINGS.GRAPHICS.bloom - 1));
        gfx_send_float(crt, "time", (float)(400 + G.TIMERS.REAL));
        gfx_send_float(crt, "noise_fac", (float)(0.001 * crtv / 100));
        gfx_send_float(crt, "crt_intensity", (float)(0.16 * crtv / 100));
        gfx_send_float(crt, "glitch_intensity", 0);
        gfx_send_float(crt, "scanlines", (float)(gfx_canvas_height(G.CANVAS) * 0.75 / G.CANV_SCALE));
        if (G.video_control) gfx_send_vec2(crt, "mouse_screen_pos", gfx_width() / 2.0f, gfx_height() / 2.0f);
        else gfx_send_vec2(crt, "mouse_screen_pos", (float)G.ARGS_eased_cursor_sx, (float)G.ARGS_eased_cursor_sy);
        gfx_send_float(crt, "screen_scale", (float)(G.TILESCALE * G.TILESIZE));
        gfx_send_float(crt, "hovering", 1);
        gfx_set_shader(crt);
    }
    gfx_draw_canvas(G.CANVAS, 0, 0);
    gfx_pop();

    gfx_set_canvas(NULL);
    gfx_set_shader(NULL);
    if (G.AA_CANVAS) {
        gfx_push();
        gfx_scale(1 / G.CANV_SCALE, 1 / G.CANV_SCALE);
        gfx_draw_canvas(G.AA_CANVAS, 0, 0);
        gfx_pop();
    }
}
