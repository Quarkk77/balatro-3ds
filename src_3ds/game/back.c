/* back.c - port of back.lua (deck backs) */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define EVCTX(T) T *c = (T *)p_; (void)e_

Back *back_new(Center *selected) {
    if (!selected) selected = P_CENTER("b_red");
    Back *b = NEW(Back);
    b->name = selected->name ? selected->name : "Red Deck";
    b->effect.center = selected;
    b->effect.text_UI = "";
    b->effect.config = selected->config;
    b->loc_name = xstrdup(localize_name_text("Back", selected->key));
    b->pos = selected->unlocked ? selected->pos : SPOS(4, 0);
    return b;
}

const char *Back_get_name(Back *b) {
    if (b->effect.center->unlocked) return b->loc_name;
    return localize_key("k_locked");
}

UIDef *Back_generate_UI(Back *b, Center *other, double ui_scale, double min_dims, const char *challenge) {
    if (isnan(min_dims)) min_dims = 0.7;
    if (isnan(ui_scale)) ui_scale = 0.9;
    Center *bc = other ? other : b->effect.center;
    const char *name = other ? other->name : b->name;
    CenterConfig *ec = other ? &other->config : &b->effect.config;
    int ci = get_challenge_int_from_id(challenge ? challenge : "");
    Challenge *ch = ci ? &G_CHALLENGES[ci - 1] : NULL;
    UIDef *loc_nodes = uinodes();
    LocVars *lv = NULL;
    if (!bc->unlocked) {
        if (!bc->unlock_condition.present) {
            localize_ui((LocArgs){ .type = "descriptions", .key = "demo_locked", .set = "Other", .nodes = loc_nodes });
        } else if (str_eq(bc->unlock_condition.type, "win_deck")) {
            const char *other_name = localize_key("k_unknown");
            Center *d = P_CENTER(bc->unlock_condition.deck);
            if (d && d->unlocked) other_name = localize_name_text("Back", bc->unlock_condition.deck);
            lv = LV(other_name);
            localize_ui((LocArgs){ .type = "descriptions", .key = "deck_locked_win", .set = "Other", .nodes = loc_nodes, .vars = lv });
        } else if (str_eq(bc->unlock_condition.type, "discover_amount")) {
            lv = LV(num_str(bc->unlock_condition.amount));
            localize_ui((LocArgs){ .type = "descriptions", .key = "deck_locked_discover", .set = "Other", .nodes = loc_nodes, .vars = lv });
        } else if (str_eq(bc->unlock_condition.type, "win_stake")) {
            int st = (int)bc->unlock_condition.stake;
            const char *other_name = localize_name_text("Stake", G.P_CENTER_POOLS.Stake.items[st - 1]->key);
            lv = LV(other_name);
            lv->colours[lv->ncolours++] = get_stake_col(st);
            localize_ui((LocArgs){ .type = "descriptions", .key = "deck_locked_stake", .set = "Other", .nodes = loc_nodes, .vars = lv });
        }
    } else {
#define N(s) str_eq(name, (s))
        if (N("Blue Deck")) lv = LV(num_str(ec->hands));
        else if (N("Red Deck")) lv = LV(num_str(ec->discards));
        else if (N("Yellow Deck")) lv = LV(num_str(ec->dollars));
        else if (N("Green Deck")) lv = LV(num_str(ec->extra_hand_bonus), num_str(ec->extra_discard_bonus));
        else if (N("Black Deck")) lv = LV(num_str(ec->joker_slot), num_str(-ec->hands));
        else if (N("Magic Deck")) lv = LV(localize_name_text("Voucher", "v_crystal_ball"), localize_name_text("Tarot", "c_fool"));
        else if (N("Nebula Deck")) lv = LV(localize_name_text("Voucher", "v_telescope"), "-1");
        else if (N("Zodiac Deck")) lv = LV(localize_name_text("Voucher", "v_tarot_merchant"), localize_name_text("Voucher", "v_planet_merchant"),
                                           localize_name_text("Voucher", "v_overstock_norm"));
        else if (N("Painted Deck")) lv = LV(num_str(ec->hand_size), num_str(ec->joker_slot));
        else if (N("Anaglyph Deck")) lv = LV(localize_name_text("Tag", "tag_double"));
        else if (N("Plasma Deck")) lv = LV(num_str(ec->ante_scaling));
#undef N
        localize_ui((LocArgs){ .type = "descriptions", .key = bc->key, .set = "Back", .nodes = loc_nodes, .vars = lv });
    }
    UIDef *content;
    if (str_eq(name, "Challenge Deck")) {
        content = UIBox_button((UIButtonArgs){ .button = G_FUNCS_deck_view_challenge,
                                               .label = { localize_cat(ch ? ch->id : "", "challenge_names") }, .nlabel = 1,
                                               .minw = 2.2, .minh = 1, .scale = 0.6, .id_ptr = ch });
    } else content = desc_from_rows(loc_nodes, true, min_dims * 5);
    return UIROOT(CFG(.align = "cm", .minw = min_dims * 5, .minh = min_dims * 2.5, .id = b->name, .colour = G.C.CLEAR), content);
}

void Back_change_to(Back *b, Center *nb) {
    if (!nb) nb = P_CENTER("b_red");
    b->name = nb->name ? nb->name : "Red Deck";
    b->effect.center = nb;
    b->effect.text_UI = "";
    b->effect.config = nb->config;
    str_set(&b->loc_name, localize_name_text("Back", nb->key));
    b->pos = nb->unlocked ? nb->pos : SPOS(4, 0);
}

DVal Back_save(Back *b) {
    DVal t = dv_table();
    dv_set_str(&t, "name", b->name);
    DVal pos = dv_table();
    dv_set_num(&pos, "x", b->pos.x);
    dv_set_num(&pos, "y", b->pos.y);
    dv_set(&t, "pos", pos);
    DVal eff = dv_table();
    dv_set_str(&eff, "text_UI", b->effect.text_UI);
    dv_set(&eff, "center", dv_str("MANUAL_REPLACE"));
    dv_set(&t, "effect", eff);
    dv_set_str(&t, "key", b->effect.center->key ? b->effect.center->key : "b_red");
    return t;
}

void Back_load(Back *b, DVal *t) {
    const char *nm = dv_get_str(t, "name", "Red Deck");
    b->name = intern(nm);
    DVal *pos = dv_get(t, "pos");
    b->pos = SPOS(dv_get_num(pos, "x", 0), dv_get_num(pos, "y", 0));
    Center *c = P_CENTER(dv_get_str(t, "key", "b_red"));
    if (!c) c = P_CENTER("b_red");
    b->effect.center = c;
    b->effect.config = c->config;
    str_set(&b->loc_name, localize_name_text("Back", c->key));
}

EVFN(ev_anaglyph) {
    (void)e_; (void)p_;
    add_tag(tag_new("tag_double", false, NULL));
    sound_play("generic1", 0.9 + math_random() * 0.1, 0.8);
    sound_play("holo1", 1.2 + math_random() * 0.1, 0.4);
    return true;
}
EVFN(ev_plasma_restore2) {
    (void)e_; (void)p_;
    col_set(G.C.UI_CHIPS, G.C.BLUE);
    col_set(G.C.UI_MULT, G.C.RED);
    return true;
}
EVFN(ev_plasma_restore) {
    (void)e_; (void)p_;
    ease_colour(G.C.UI_CHIPS, G.C.BLUE, 2);
    ease_colour(G.C.UI_MULT, G.C.RED, 2);
    return true;
}
EVFN(ev_plasma) {
    (void)e_; (void)p_;
    const char *text = localize_key("k_balanced");
    sound_play("gong", 0.94, 0.3);
    sound_play("gong", 0.94 * 1.5, 0.2);
    sound_play("tarot1", 1.5, NAN);
    static float purple[4] = { 0.8f, 0.45f, 0.85f, 1 };
    ease_colour(G.C.UI_CHIPS, purple, NAN);
    ease_colour(G.C.UI_MULT, purple, NAN);
    ATT(.scale = 1.4, .text = text, .hold = 2, .align = "cm", .has_offset = true, .offset = { 0, -2.7 }, .major = &G.play->n);
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .blocking = 0, .delay = 4.3, .func = ev_plasma_restore));
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .blocking = 0, .no_delete = true, .delay = 6.3, .func = ev_plasma_restore2));
    return true;
}

void Back_trigger_effect(Back *b, const char *context, double *chips, double *mult, bool *changed) {
    if (changed) *changed = false;
    if (!context) return;
    if (str_eq(b->name, "Anaglyph Deck") && !strcmp(context, "eval") && G.GAME.last_blind.present && G.GAME.last_blind.boss)
        add_event(EVENT(.func = ev_anaglyph));
    if (str_eq(b->name, "Plasma Deck") && !strcmp(context, "blind_amount")) return;
    if (str_eq(b->name, "Plasma Deck") && !strcmp(context, "final_scoring_step")) {
        double tot = *chips + *mult;
        *chips = floor(tot / 2);
        *mult = floor(tot / 2);
        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = *mult, .has_mult = true, .chips = *chips, .has_chips = true));
        add_event(EVENT(.func = ev_plasma));
        delay(0.6);
        if (changed) *changed = true;
    }
}

typedef struct { Back *b; } BackCtx;
EVFN(ev_back_consumables) {
    EVCTX(BackCtx);
    for (int i = 0; i < c->b->effect.config.n_consumables; i++) {
        Card *card = create_card("Tarot", G.consumeables, false, RARITY_NIL, false, false, c->b->effect.config.consumables[i], "deck");
        Card_add_to_deck(card, false);
        CardArea_emplace(G.consumeables, card, NULL, false);
    }
    return true;
}
EVFN(ev_back_edition) {
    EVCTX(BackCtx);
    int i = 0;
    while (i < c->b->effect.config.edition_count) {
        CardList sorted = { 0 };
        for (int k = 0; k < G.playing_cards.count; k++) vec_push(&sorted, G.playing_cards.items[k]);
        for (int a = 1; a < sorted.count; a++) {
            Card *kk = sorted.items[a]; int j = a - 1;
            while (j >= 0 && sorted.items[j]->sort_id > kk->sort_id) { sorted.items[j + 1] = sorted.items[j]; j--; }
            sorted.items[j + 1] = kk;
        }
        Card *card = sorted.items[pseudorandom_index(sorted.count, pseudoseed("edition_deck"), true) - 1];
        vec_free(&sorted);
        if (!card->edition) {
            i++;
            Card_set_edition(card, edition_req_from_key(c->b->effect.config.edition), false, true);
        }
    }
    return true;
}
EVFN(ev_checkered) {
    (void)e_; (void)p_;
    for (int i = 0; i < G.playing_cards.count; i++) {
        Card *v = G.playing_cards.items[i];
        if (str_eq(v->base.suit, "Clubs")) Card_change_suit(v, "Spades");
        if (str_eq(v->base.suit, "Diamonds")) Card_change_suit(v, "Hearts");
    }
    return true;
}

void Back_apply_to_run(Back *b) {
    CenterConfig *ec = &b->effect.config;
    if (ec->voucher) {
        strset_add(&G.GAME.used_vouchers, ec->voucher);
        G.GAME.starting_voucher_count += 1;
        Card_apply_to_run(NULL, P_CENTER(ec->voucher));
    }
    if (ec->hands != 0) G.GAME.starting_params.hands += ec->hands;
    if (ec->n_consumables > 0) {
        delay(0.4);
        BackCtx *c = CTX(BackCtx); c->b = b;
        add_event(EVENT(.func = ev_back_consumables, .ctx = c));
    }
    if (ec->dollars != 0) G.GAME.starting_params.dollars += ec->dollars;
    if (ec->remove_faces) G.GAME.starting_params.no_faces = true;
    if (ec->spectral_rate != 0) G.GAME.spectral_rate = ec->spectral_rate;
    if (ec->discards != 0) G.GAME.starting_params.discards += ec->discards;
    if (ec->reroll_discount != 0) G.GAME.starting_params.reroll_cost -= ec->reroll_discount;
    if (ec->edition) {
        BackCtx *c = CTX(BackCtx); c->b = b;
        add_event(EVENT(.func = ev_back_edition, .ctx = c));
    }
    for (int i = 0; i < ec->n_vouchers; i++) {
        strset_add(&G.GAME.used_vouchers, ec->vouchers[i]);
        G.GAME.starting_voucher_count += 1;
        Card_apply_to_run(NULL, P_CENTER(ec->vouchers[i]));
    }
    if (str_eq(b->name, "Checkered Deck")) add_event(EVENT(.func = ev_checkered));
    if (ec->randomize_rank_suit) G.GAME.starting_params.erratic_suits_and_ranks = true;
    if (ec->joker_slot != 0) G.GAME.starting_params.joker_slots += ec->joker_slot;
    if (ec->hand_size != 0) G.GAME.starting_params.hand_size += ec->hand_size;
    if (ec->ante_scaling != 0) G.GAME.starting_params.ante_scaling = ec->ante_scaling;
    if (ec->consumable_slot != 0) G.GAME.starting_params.consumable_slots += ec->consumable_slot;
    if (ec->no_interest) G.GAME.modifiers.no_interest = true;
    if (ec->extra_hand_bonus != 0) G.GAME.modifiers.money_per_hand = ec->extra_hand_bonus;
    if (ec->extra_discard_bonus != 0) G.GAME.modifiers.money_per_discard = ec->extra_discard_bonus;
}
