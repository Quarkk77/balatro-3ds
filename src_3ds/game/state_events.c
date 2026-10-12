/* state_events.c - port of functions/state_events.lua (win/end/new round,
 * drawing, discarding, playing and scoring hands, round evaluation and
 * the tutorial controller). */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define LOC(k) ((char *)localize_key(k))

static void dv_profile_hs_set(const char *score, double v) {
    DVal *hs = dv_get(dv_get(profile(), "high_scores"), score);
    if (hs) dv_set_num(hs, "amt", v);
}
static double profile_hs(const char *score) { return dv_get_num(dv_get(dv_get(profile(), "high_scores"), score), "amt", 0); }

StatusExtra *sextra_from_eval(EvalResult *r) {
    StatusExtra *x = NEW(StatusExtra);
    if (!r) return x;
    x->message = r->message;
    x->colour = r->colour;
    x->delay = r->delay; x->has_delay = r->has_delay;
    x->no_juice = r->no_juice;
    x->focus = r->focus ? &r->focus->n : NULL;
    x->edition = r->edition;
    x->mult_mod = r->has_mult_mod;
    x->Xmult_mod = r->has_Xmult_mod;
    x->chip_mod = r->has_chip_mod;
    x->swap = r->swap;
    x->playing_cards_created = r->playing_cards_created;
    return x;
}
static StatusExtra *sextra_from_extra(EvalExtra *e) {
    StatusExtra *x = NEW(StatusExtra);
    if (!e) return x;
    x->message = e->message;
    x->colour = e->colour;
    x->delay = e->delay; x->has_delay = e->has_delay;
    x->no_juice = e->no_juice;
    x->focus = e->focus;
    x->mult_mod = e->has_mult_mod;
    x->Xmult_mod = e->has_Xmult_mod;
    x->chip_mod = e->has_chip_mod;
    x->swap = e->has_swap;
    return x;
}

static bool card_eval_nonempty(CardEval *e) {
    return e->has_chips || e->has_mult || e->has_x_mult || e->has_p_dollars || e->has_h_mult || e->jokers || e->edition || e->seals;
}

/* a unified view of an entry in the Lua `effects` lists */
typedef struct {
    double chips, mult, x_mult, p_dollars, dollars, h_mult, h_dollars;
    bool has_chips, has_mult, has_x_mult, has_p_dollars, has_dollars, has_h_mult, has_h_dollars;
    EvalExtra *extra;
    EvalResult *edition;
    Card *card;
    EvalResult *src;      /* joker result (for message based effects) */
    bool empty;
} Effect;

static Effect effect_from_cardeval(CardEval *e) {
    Effect f = { 0 };
    f.chips = e->chips; f.has_chips = e->has_chips;
    f.mult = e->mult; f.has_mult = e->has_mult;
    f.x_mult = e->x_mult; f.has_x_mult = e->has_x_mult;
    f.p_dollars = e->p_dollars; f.has_p_dollars = e->has_p_dollars;
    f.h_mult = e->h_mult; f.has_h_mult = e->has_h_mult;
    f.edition = e->edition;
    f.empty = !card_eval_nonempty(e);
    return f;
}
static Effect effect_from_result(EvalResult *r) {
    Effect f = { 0 };
    if (!r) { f.empty = true; return f; }
    f.chips = r->chips; f.has_chips = r->has_chips;
    f.mult = r->mult; f.has_mult = r->has_mult;
    f.x_mult = r->x_mult; f.has_x_mult = r->has_x_mult;
    f.dollars = r->dollars; f.has_dollars = r->has_dollars;
    f.h_mult = r->h_mult; f.has_h_mult = r->has_h_mult;
    f.h_dollars = r->h_dollars; f.has_h_dollars = r->has_h_dollars;
    f.extra = r->extra;
    f.card = r->card;
    f.src = r;
    f.empty = r->is_empty;
    return f;
}
static EvalResult *effect_as_result(Effect *f) {
    if (f->src) return f->src;
    EvalResult *r = eval_new();
    r->is_empty = f->empty;
    return r;
}

typedef VEC(Effect) EffectList;
typedef VEC(EvalResult *) RepList;   /* NULL entry = the initial `1` */

static void effects_card_ctx(EffectList *effects, JokerContext *ctx) {
    ctx->n_card_effects = effects->count;
    ctx->card_effects = xcalloc((size_t)(effects->count > 0 ? effects->count : 1), sizeof(EvalResult *));
    for (int i = 0; i < effects->count; i++) ctx->card_effects[i] = effect_as_result(&effects->items[i]);
}

/* ------------------------------------------------------------------ */
/* win_game                                                            */
/* ------------------------------------------------------------------ */
typedef struct { CardCharacter *jimbo; } JimboCtx;
EVFN(ev_win_wishlist) { JimboCtx *c = p_; (void)e_; CardCharacter_add_button(c->jimbo, localize_key("b_wishlist"), NULL, G.C.DARK_EDITION, NULL, true, 1.6); return true; }
EVFN(ev_win_jimbo) {
    (void)e_; (void)p_;
    UIElement *spot = G.OVERLAY_MENU ? UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "jimbo_spot", NULL) : NULL;
    if (spot) {
        CardCharacter *j = card_character_new(0, 5, NAN, NAN, NULL);
        if (spot->config->object) node_remove(spot->config->object);
        spot->config->object = &j->n;
        j->n.ui_object_updated = true;
        CardCharacter_add_speech_bubble(j, tfmt("wq_%d", math_random_range(1, 7)), NULL, true, NULL);
        CardCharacter_say_stuff(j, 5, false);
        if (G.F_JAN_CTA) { JimboCtx *c = CTX(JimboCtx); c->jimbo = j; add_event(EVENT(.func = ev_win_wishlist, .ctx = c)); }
    }
    return true;
}
EVFN(ev_win_overlay) {
    (void)e_; (void)p_;
    for (int i = 0; i < G.I.CARD.count; i++) if (node_is(G.I.CARD.items[i], NT_CARD)) ((Card *)G.I.CARD.items[i])->sticker_run = NULL;
    sound_play("win", NAN, NAN);
    G.SETTINGS.paused = true;
    overlay_menu(create_UIBox_win(), (OverlayArgs){ .no_esc = true });
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 2.5, .blocking = 0, .func = ev_win_jimbo));
    return true;
}
EVFN(ev_endless_text) {
    (void)e_; (void)p_;
    if (!G.SETTINGS.paused) { str_set(&G.GAME.current_round.round_text, "Endless Round "); return true; }
    return false;
}

void win_game(void) {
    if (!G.GAME.seeded && !G.GAME.challenge) {
        set_joker_win();
        set_deck_win();
        check_and_set_high_score("win_streak", profile_hs("current_streak") + 1);
        check_and_set_high_score("current_streak", profile_hs("current_streak") + 1);
        UNLOCK(.type = "win_no_hand");
        UNLOCK(.type = "win_no");
        UNLOCK(.type = "win_custom");
        UNLOCK(.type = "win_deck");
        UNLOCK(.type = "win_stake");
        UNLOCK(.type = "win");
        inc_career_stat("c_wins", 1);
    }
    set_profile_progress();
    if (G.GAME.challenge) {
        DVal *cp = dv_get_or_create_table(profile(), "challenge_progress");
        dv_set_bool(dv_get_or_create_table(cp, "completed"), G.GAME.challenge, true);
        set_challenge_unlock();
        UNLOCK(.type = "win_challenge");
        game_save_settings();
    }
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_win_overlay));
    if (!G.GAME.seeded && !G.GAME.challenge)
        dv_set_num(profile(), "stake", MAX(dv_get_num(profile(), "stake", 1), (G.GAME.stake ? G.GAME.stake : 1) + 1));
    game_save_progress();
    G.FILE_HANDLER.force = true;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_endless_text));
}

/* ------------------------------------------------------------------ */
/* end_round                                                           */
/* ------------------------------------------------------------------ */
typedef struct { Card *card; } CardCtx;
EVFN(ev_juice07) { CardCtx *c = p_; (void)e_; node_juice_up(&c->card->n, 0.7, NAN); return true; }
static void juice_event(Card *card) {
    CardCtx *c = CTX(CardCtx); c->card = card;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_juice07, .ctx = c));
}

EVFN(ev_win_check) {
    (void)e_; (void)p_;
    if (G.STATE == STATE_ROUND_EVAL) { win_game(); G.GAME.won = true; return true; }
    return false;
}

EVFN(ev_end_round_eval_state) {
    (void)e_; (void)p_;
    G.STATE = STATE_ROUND_EVAL;
    G.STATE_COMPLETE = false;
    RoundResets *rr = &G.GAME.round_resets;
    if (rr->blind == P_BLIND("bl_small")) rr->blind_states[BT_SMALL] = "Defeated";
    else if (rr->blind == P_BLIND("bl_big")) rr->blind_states[BT_BIG] = "Defeated";
    else {
        G.GAME.current_round.voucher = get_next_voucher_key(false);
        rr->blind_states[BT_BOSS] = "Defeated";
        for (int i = 0; i < G.playing_cards.count; i++) ((Card *)G.playing_cards.items[i])->ability.played_this_ante = false;
    }
    if (rr->has_temp_handsize) { CardArea_change_size(G.hand, -rr->temp_handsize); rr->temp_handsize = 0; rr->has_temp_handsize = false; }
    if (rr->has_temp_reroll_cost) { rr->temp_reroll_cost = 0; rr->has_temp_reroll_cost = false; calculate_reroll_cost(true); }
    reset_idol_card();
    reset_mail_rank();
    reset_ancient_card();
    reset_castle_card();
    for (int i = 0; i < G.playing_cards.count; i++) {
        Card *v = G.playing_cards.items[i];
        v->ability.discarded = false;
        v->ability.forced_selection = false;
    }
    return true;
}

EVFN(ev_end_round) {
    (void)e_; (void)p_;
    bool game_over = true, game_won = false;
    G.RESET_BLIND_STATES = true;
    G.RESET_JIGGLES = true;
    if (G.GAME.chips - G.GAME.blind->chips >= 0) game_over = false;
    for (int i = 0; i < G.jokers->cards.count; i++) {
        Card *j = G.jokers->cards.items[i];
        EvalResult *ev = Card_calculate_joker(j, JCTX(.end_of_round = true, .game_over = game_over));
        if (ev) {
            if (ev->saved) game_over = false;
            card_eval_status_text(&j->n, "jokers", 0, NAN, NULL, sextra_from_eval(ev));
        }
        Card_calculate_rental(j);
        Card_calculate_perishable(j);
    }
    if (G.GAME.round_resets.ante == G.GAME.win_ante && str_eq(Blind_get_type(G.GAME.blind), "Boss")) { game_won = true; G.GAME.won = true; }
    if (game_over) {
        G.STATE = STATE_GAME_OVER;
        if (!G.GAME.won && !G.GAME.seeded && !G.GAME.challenge) dv_profile_hs_set("current_streak", 0);
        game_save_settings();
        G.FILE_HANDLER.force = true;
        G.STATE_COMPLETE = false;
        return true;
    }
    G.GAME.unused_discards += G.GAME.current_round.discards_left;
    if (G.GAME.blind && G.GAME.blind->config.blind) discover_blind(G.GAME.blind->config.blind);
    bool boss = str_eq(Blind_get_type(G.GAME.blind), "Boss");
    if (boss) {
        const char *handname = "High Card";
        double played = -1, order = 100;
        for (int k = 0; k < HAND_COUNT; k++) {
            int hi = hand_index(DATA_HANDS_PAIRS_ORDER[k]);
            HandStat *v = &G.GAME.hands[hi];
            if (v->played > played || (v->played == played && order > v->order)) { played = v->played; handname = HAND_NAMES[hi]; }
        }
        G.GAME.current_round.most_played_poker_hand = handname;
    }
    if (boss && !G.GAME.seeded && !G.GAME.challenge) {
        G.GAME.current_boss_streak += 1;
        check_and_set_high_score("boss_streak", G.GAME.current_boss_streak);
    }
    if (G.GAME.current_round.hands_played == 1) inc_career_stat("c_single_hand_round_streak", 1);
    else if (!G.GAME.seeded && !G.GAME.challenge) {
        dv_set_num(dv_get_or_create_table(profile(), "career_stats"), "c_single_hand_round_streak", 0);
        game_save_settings();
    }
    UNLOCK(.type = "round_win");
    set_joker_usage();
    if (game_won && !G.GAME.win_notified) {
        G.GAME.win_notified = true;
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .blocking = 0, .blockable = 0, .func = ev_win_check));
    }
    for (int i = 0; i < G.hand->cards.count; i++) {
        Card *hc = G.hand->cards.items[i];
        RepList reps = { 0 };
        vec_push(&reps, NULL);
        for (int j = 0; j < reps.count; j++) {
            double percent = (i + 1 - 0.999) / (G.hand->cards.count - 0.998) + j * 0.1;
            if (reps.items[j]) card_eval_status_text(&reps.items[j]->card->n, "jokers", 0, NAN, NULL, sextra_from_eval(reps.items[j]));
            EffectList effects = { 0 };
            vec_push(&effects, effect_from_result(Card_get_end_of_round_effect(hc, NULL)));
            for (int k = 0; k < G.jokers->cards.count; k++) {
                EvalResult *ev = Card_calculate_joker(G.jokers->cards.items[k], JCTX(.cardarea = G.hand, .other_card = hc, .individual = true, .end_of_round = true));
                if (ev) vec_push(&effects, effect_from_result(ev));
            }
            if (j == 0) {
                CardEval sev = eval_card(hc, JCTX(.end_of_round = true, .cardarea = G.hand, .repetition = true, .repetition_only = true));
                if (card_eval_nonempty(&sev) && (!effects.items[0].empty || effects.count > 1))
                    for (int h = 0; sev.seals && h < (int)sev.seals->repetitions; h++) vec_push(&reps, sev.seals);
                for (int jj = 0; jj < G.jokers->cards.count; jj++) {
                    JokerContext ctx = { .cardarea = G.hand, .other_card = hc, .repetition = true, .end_of_round = true };
                    effects_card_ctx(&effects, &ctx);
                    CardEval jev = eval_card(G.jokers->cards.items[jj], &ctx);
                    if (card_eval_nonempty(&jev) && jev.jokers)
                        for (int h = 0; h < (int)jev.jokers->repetitions; h++) vec_push(&reps, jev.jokers);
                }
            }
            for (int ii = 0; ii < effects.count; ii++) {
                Effect *f = &effects.items[ii];
                if (f->card) juice_event(f->card);
                if (f->has_h_dollars) {
                    ease_dollars(f->h_dollars, false);
                    card_eval_status_text(&hc->n, "dollars", f->h_dollars, percent, NULL, NULL);
                }
                if (f->extra) card_eval_status_text(&hc->n, "extra", 0, percent, NULL, sextra_from_extra(f->extra));
            }
            vec_free(&effects);
        }
        vec_free(&reps);
    }
    delay(0.3);
    G_FUNCS_draw_from_hand_to_discard();
    if (boss) {
        if (G.GAME.modifiers.has_set_eternal_ante && G.GAME.round_resets.ante == G.GAME.modifiers.set_eternal_ante)
            for (int k = 0; k < G.jokers->cards.count; k++) Card_set_eternal(G.jokers->cards.items[k], true);
        if (G.GAME.modifiers.has_set_joker_slots_ante && G.GAME.round_resets.ante == G.GAME.modifiers.set_joker_slots_ante) G.jokers->config.card_limit = 0;
        delay(0.4);
        ease_ante(1);
        delay(0.4);
        UNLOCK(.type = "ante_up", .ante = G.GAME.round_resets.ante + 1);
    }
    G_FUNCS_draw_from_discard_to_deck();
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.3, .func = ev_end_round_eval_state));
    return true;
}

void end_round(void) {
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_end_round));
}

/* ------------------------------------------------------------------ */
/* new_round                                                           */
/* ------------------------------------------------------------------ */
EVFN(ev_new_round_draw) {
    (void)e_; (void)p_;
    G.STATE = STATE_DRAW_TO_HAND;
    CardArea_shuffle(G.deck, tfmt("nr%s", num_str(G.GAME.round_resets.ante)));
    CardArea_hard_set_cards(G.deck);
    G.STATE_COMPLETE = false;
    return true;
}
EVFN(ev_new_round) {
    (void)e_; (void)p_;
    CurrentRound *cr = &G.GAME.current_round;
    RoundResets *rr = &G.GAME.round_resets;
    cr->discards_left = MAX(0, rr->discards + G.GAME.round_bonus.discards);
    cr->hands_left = MAX(1, rr->hands + G.GAME.round_bonus.next_hands);
    cr->hands_played = 0;
    cr->discards_used = 0;
    cr->reroll_cost_increase = 0;
    for (int i = 0; i < cr->n_used_packs; i++) { free(cr->used_packs[i]); cr->used_packs[i] = NULL; }
    cr->n_used_packs = 0;
    for (int k = 0; k < HAND_COUNT; k++) G.GAME.hands[k].played_this_round = 0;
    for (int i = 0; i < G.playing_cards.count; i++) ((Card *)G.playing_cards.items[i])->ability.wheel_flipped = false;
    CardList chaos = { 0 };
    find_joker("Chaos the Clown", false, &chaos);
    cr->free_rerolls = chaos.count;
    vec_free(&chaos);
    calculate_reroll_cost(true);
    G.GAME.round_bonus.next_hands = 0;
    G.GAME.round_bonus.discards = 0;
    const char *blhash;
    if (rr->blind == P_BLIND("bl_small")) { rr->blind_states[BT_SMALL] = "Current"; G.GAME.current_boss_streak = 0; blhash = "S"; }
    else if (rr->blind == P_BLIND("bl_big")) { rr->blind_states[BT_BIG] = "Current"; G.GAME.current_boss_streak = 0; blhash = "B"; }
    else { rr->blind_states[BT_BOSS] = "Current"; blhash = "L"; }
    str_set(&G.GAME.subhash, tfmt("%s%s", num_str(rr->ante), blhash));
    Blind_set_blind(G.GAME.blind, rr->blind, false, false);
    for (int i = 0; i < G.jokers->cards.count; i++) Card_calculate_joker(G.jokers->cards.items[i], JCTX(.setting_blind = true, .blind = rr->blind));
    delay(0.4);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_new_round_draw));
    return true;
}
void new_round(void) {
    G.RESET_JIGGLES = false;
    delay(0.4);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_new_round));
}

/* ------------------------------------------------------------------ */
/* drawing / discarding / playing                                      */
/* ------------------------------------------------------------------ */
bool G_FUNCS_draw_from_deck_to_hand(double e) {
    if (!(G.STATE == STATE_TAROT_PACK || G.STATE == STATE_SPECTRAL_PACK) && G.hand->config.card_limit <= 0 && G.hand->cards.count == 0) {
        G.STATE = STATE_GAME_OVER;
        G.STATE_COMPLETE = false;
        return true;
    }
    double hand_space = !isnan(e) ? e : MIN(G.deck->cards.count, G.hand->config.card_limit - G.hand->cards.count);
    Blind *b = G.GAME.blind;
    if (str_eq(b->name, "The Serpent") && !b->disabled && (G.GAME.current_round.hands_played > 0 || G.GAME.current_round.discards_used > 0))
        hand_space = MIN(G.deck->cards.count, 3);
    delay(0.3);
    for (int i = 1; i <= (int)hand_space; i++) draw_card(G.deck, G.hand, i * 100 / hand_space, "up", true, NULL, NAN, false, false, NAN, false);
    return false;
}

static int cmp_tx(const void *a, const void *b) {
    Card *x = *(Card *const *)a, *y = *(Card *const *)b;
    return x->n.T->x < y->n.T->x ? -1 : x->n.T->x > y->n.T->x ? 1 : 0;
}

EVFN(ev_state_incomplete) { (void)e_; (void)p_; G.STATE_COMPLETE = false; return true; }

void G_FUNCS_discard_cards_from_highlighted(UIElement *e, bool hook) {
    (void)e;
    stop_use();
    G.CONTROLLER.interrupt.focus = true;
    Controller_save_cardarea_focus(&G.CONTROLLER, "hand");
    for (int i = 0; i < G.playing_cards.count; i++) ((Card *)G.playing_cards.items[i])->ability.forced_selection = false;
    int highlighted_count = (int)MIN(G.hand->highlighted.count, G.discard->config.card_limit - G.play->cards.count);
    if (highlighted_count <= 0) return;
    update_hand_text(HTC(.immediate = true, .nopulse = true, .delay = 0, .has_delay = true),
                     HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .level = "", .has_level = true, .handname = "", .has_handname = true));
    qsort(G.hand->highlighted.items, (size_t)G.hand->highlighted.count, sizeof(Card *), cmp_tx);
    inc_career_stat("c_cards_discarded", highlighted_count);
    for (int j = 0; j < G.jokers->cards.count; j++)
        Card_calculate_joker(G.jokers->cards.items[j], JCTX(.pre_discard = true, .full_hand = &G.hand->highlighted, .hook = hook));
    CardList cards = { 0 }, destroyed = { 0 };
    for (int i = 0; i < highlighted_count; i++) {
        Card *c = G.hand->highlighted.items[i];
        Card_calculate_seal(c, JCTX(.discard = true));
        bool removed = false;
        for (int j = 0; j < G.jokers->cards.count; j++) {
            EvalResult *ev = Card_calculate_joker(G.jokers->cards.items[j], JCTX(.discard = true, .other_card = c, .full_hand = &G.hand->highlighted));
            if (ev) {
                if (ev->remove) removed = true;
                card_eval_status_text(&G.jokers->cards.items[j]->n, "jokers", 0, 1, NULL, sextra_from_eval(ev));
            }
        }
        vec_push(&cards, c);
        if (removed) {
            vec_push(&destroyed, c);
            if (str_eq(c->ability.name, "Glass Card")) Card_shatter(c);
            else Card_start_dissolve(c, NULL, 0, false, NAN, false);
        } else {
            c->ability.discarded = true;
            draw_card(G.hand, G.discard, (i + 1) * 100.0 / highlighted_count, "down", false, c, NAN, false, false, NAN, false);
        }
    }
    if (destroyed.count)
        for (int j = 0; j < G.jokers->cards.count; j++) eval_card(G.jokers->cards.items[j], JCTX(.cardarea = G.jokers, .remove_playing_cards = true, .removed = &destroyed));
    G.GAME.round_scores.cards_discarded.amt += cards.count;
    UNLOCK(.type = "discard_custom", .cards = &cards);
    if (!hook) {
        if (G.GAME.modifiers.discard_cost) ease_dollars(-G.GAME.modifiers.discard_cost, false);
        ease_discard(-1, false, false);
        G.GAME.current_round.discards_used += 1;
        G.STATE = STATE_DRAW_TO_HAND;
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_state_incomplete));
    }
}

EVFN(ev_hand_played_state) { (void)e_; (void)p_; G.STATE = STATE_HAND_PLAYED; G.STATE_COMPLETE = true; return true; }
EVFN(ev_tarot2_076) { (void)e_; (void)p_; sound_play("tarot2", 0.76, 0.4); return true; }
EVFN(ev_blind_juice) {
    (void)e_; (void)p_;
    UIElement *d1 = UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_debuff_1", NULL), *d2 = UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_debuff_2", NULL);
    if (d1) node_juice_up(&d1->n, 0.3, 0);
    if (d2) node_juice_up(&d2->n, 0.3, 0);
    node_juice_up(&G.GAME.blind->n, NAN, NAN);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.06 * G.SETTINGS.GAMESPEED, .blockable = 0, .blocking = 0, .func = ev_tarot2_076));
    sound_play("tarot2", 1, 0.4);
    return true;
}
EVFN(ev_evaluate_play) { (void)e_; (void)p_; G_FUNCS_evaluate_play(NULL); return true; }
EVFN(ev_after_play) {
    (void)e_; (void)p_;
    UNLOCK(.type = "play_all_hearts");
    G_FUNCS_draw_from_play_to_discard();
    G.GAME.hands_played += 1;
    G.GAME.current_round.hands_played += 1;
    return true;
}
EVFN(ev_play_main) {
    (void)e_; (void)p_;
    UNLOCK(.type = "hand_contents", .cards = &G.play->cards);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_evaluate_play));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_after_play));
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_state_incomplete));
    return true;
}

GF_IMPL(play_cards_from_highlighted) {
    (void)e;
    if (G.play && G.play->cards.count > 0) return;
    stop_use();
    G.GAME.blind->triggered = false;
    G.CONTROLLER.interrupt.focus = true;
    Controller_save_cardarea_focus(&G.CONTROLLER, "hand");
    for (int i = 0; i < G.playing_cards.count; i++) ((Card *)G.playing_cards.items[i])->ability.forced_selection = false;
    qsort(G.hand->highlighted.items, (size_t)G.hand->highlighted.count, sizeof(Card *), cmp_tx);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_hand_played_state));
    inc_career_stat("c_cards_played", G.hand->highlighted.count);
    inc_career_stat("c_hands_played", 1);
    ease_hands_played(-1, false);
    delay(0.4);
    int n = G.hand->highlighted.count;
    for (int i = 0; i < n; i++) {
        Card *c = G.hand->highlighted.items[i];
        if (Card_is_face(c, false)) inc_career_stat("c_face_cards_played", 1);
        c->base.times_played += 1;
        c->ability.played_this_ante = true;
        G.GAME.round_scores.cards_played.amt += 1;
        draw_card(G.hand, G.play, (i + 1) * 100.0 / n, "up", false, c, NAN, false, false, NAN, false);
    }
    UNLOCK(.type = "run_card_replays");
    if (Blind_press_play(G.GAME.blind)) {
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_blind_juice));
        delay(0.4);
    }
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_play_main));
}

const char *G_FUNCS_get_poker_hand_info(CardList *cards, const char **disp_text, PokerHands **poker_hands, CardList **scoring_hand, const char **non_loc_disp_text) {
    PokerHands *ph = evaluate_poker_hand(cards);
    static const char *order[12] = { "Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House", "Flush", "Straight",
                                     "Three of a Kind", "Two Pair", "Pair", "High Card" };
    const char *text = "NULL";
    CardList *sh = NEW(CardList);
    for (int i = 0; i < 12; i++) {
        int hi = hand_index(order[i]);
        if (ph->h[hi].count > 0) {
            text = order[i];
            CardList *first = ph->h[hi].items[0];
            for (int k = 0; k < first->count; k++) vec_push(sh, first->items[k]);
            break;
        }
    }
    const char *disp = text;
    if (!strcmp(text, "Straight Flush")) {
        int min = 10;
        for (int j = 0; j < sh->count; j++) if (Card_get_id(sh->items[j]) < min) min = Card_get_id(sh->items[j]);
        if (min >= 10) disp = "Royal Flush";
    }
    if (disp_text) *disp_text = localize_cat(disp, "poker_hands");
    if (poker_hands) *poker_hands = ph;
    if (scoring_hand) *scoring_hand = sh;
    if (non_loc_disp_text) *non_loc_disp_text = disp;
    return text;
}

/* ------------------------------------------------------------------ */
/* evaluate_play                                                       */
/* ------------------------------------------------------------------ */
typedef struct { double chip_total; } TotalCtx;
EVFN(ev_final_hand_text) {
    TotalCtx *c = p_; (void)e_;
    update_hand_text(HTC(.delay = 0, .has_delay = true, .immediate = true),
                     HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .chip_total = c->chip_total, .has_chip_total = true, .level = "",
                         .has_level = true, .handname = "", .has_handname = true));
    sound_play("button", 0.9, 0.6);
    return true;
}
EVFN(ev_chips2) { (void)e_; (void)p_; sound_play("chips2", NAN, NAN); return true; }
static double ease_floor(double t, void *ctx) { (void)ctx; return floor(t); }
EVFN(ev_clear_handname) { (void)e_; (void)p_; str_set(&G.GAME.current_round.current_hand.handname, ""); return true; }
typedef struct { CardList *scoring; } ScoringCtx;
EVFN(ev_perma_debuff) {
    ScoringCtx *c = p_; (void)e_;
    if (G.GAME.modifiers.debuff_played_cards)
        for (int i = 0; i < c->scoring->count; i++) c->scoring->items[i]->ability.perma_debuff = true;
    return true;
}
EVFN(ev_destroy_card) {
    CardCtx *c = p_; (void)e_;
    if (str_eq(c->card->ability.name, "Glass Card")) Card_shatter(c->card);
    else Card_start_dissolve(c->card, NULL, 0, false, NAN, false);
    return true;
}

static void status_jokers(Card *card, double percent, EvalResult *r) {
    card_eval_status_text(&card->n, "jokers", 0, percent, NULL, sextra_from_eval(r));
}

void G_FUNCS_evaluate_play(UIElement *e) {
    (void)e;
    const char *disp_text, *non_loc;
    PokerHands *poker_hands;
    CardList *scoring_hand;
    const char *text = G_FUNCS_get_poker_hand_info(&G.play->cards, &disp_text, &poker_hands, &scoring_hand, &non_loc);
    int hi = hand_index(text);
    HandStat *hs = &G.GAME.hands[hi];
    hs->played += 1;
    hs->played_this_round += 1;
    G.GAME.last_hand_played = HAND_NAMES[hi];
    set_hand_usage(text);
    hs->visible = true;

    CardList pures = { 0 };
    bool splash = has_joker("Splash");
    for (int i = 0; i < G.play->cards.count; i++) {
        Card *pc = G.play->cards.items[i];
        if (splash) {
            if (i < scoring_hand->count) scoring_hand->items[i] = pc;
            else vec_push(scoring_hand, pc);
        } else if (str_eq(pc->ability.effect, "Stone Card")) {
            bool inside = false;
            for (int j = 0; j < scoring_hand->count; j++) if (scoring_hand->items[j] == pc) inside = true;
            if (!inside) vec_push(&pures, pc);
        }
    }
    for (int i = 0; i < pures.count; i++) vec_push(scoring_hand, pures.items[i]);
    vec_free(&pures);
    qsort(scoring_hand->items, (size_t)scoring_hand->count, sizeof(Card *), cmp_tx);
    delay(0.2);
    for (int i = 0; i < scoring_hand->count; i++) highlight_card(scoring_hand->items[i], (i + 1 - 0.999) / 5, "up");

    double percent = 0.3, percent_delta = 0.08;
    bool name_changed = !str_eq(G.GAME.current_round.current_hand.handname, disp_text);
    if (name_changed) delay(0.3);
    update_hand_text(HTC(.sound = name_changed ? "button" : NULL, .volume = 0.4, .has_volume = true, .immediate = true, .delay = name_changed ? 0.4 : 0, .has_delay = true),
                     HTV(.handname = disp_text, .has_handname = true, .level_n = hs->level, .level_is_num = true, .has_level = true, .mult = hs->mult, .has_mult = true,
                         .chips = hs->chips, .has_chips = true));
    double mult, hand_chips;
    JokerContext base = { .full_hand = &G.play->cards, .scoring_hand = scoring_hand, .scoring_name = text, .poker_hands = poker_hands };
    if (!Blind_debuff_hand(G.GAME.blind, &G.play->cards, poker_hands, text, false)) {
        mult = mod_mult(hs->mult);
        hand_chips = mod_chips(hs->chips);
        UNLOCK(.type = "hand", .handname = text, .disp_text = non_loc, .scoring_hand = scoring_hand);
        delay(0.4);
        for (int i = 0; i < G.jokers->cards.count; i++) {
            JokerContext ctx = base; ctx.cardarea = G.jokers; ctx.before = true;
            CardEval ev = eval_card(G.jokers->cards.items[i], &ctx);
            if (ev.jokers) {
                status_jokers(G.jokers->cards.items[i], percent, ev.jokers);
                percent += percent_delta;
                if (ev.jokers->level_up) level_up_hand(&G.jokers->cards.items[i]->n, text, false, NAN);
            }
        }
        mult = mod_mult(hs->mult);
        hand_chips = mod_chips(hs->chips);
        bool modded = false;
        Blind_modify_hand(G.GAME.blind, &G.play->cards, poker_hands, text, &mult, &hand_chips, &modded);
        mult = mod_mult(mult);
        hand_chips = mod_chips(hand_chips);
        if (modded) update_hand_text(HTC(.sound = "chips2", .modded = true), HTV(.chips = hand_chips, .has_chips = true, .mult = mult, .has_mult = true));

        for (int i = 0; i < scoring_hand->count; i++) {
            Card *sc = scoring_hand->items[i];
            if (!str_eq(sc->ability.effect, "Stone Card")) {
                int ri = rank_order_index(sc->base.value);
                if (ri >= 0) {
                    G.GAME.cards_played[ri].total += 1;
                    nummap_set(&G.GAME.cards_played[ri].suits, sc->base.suit, 1);
                }
            }
            if (sc->debuff) {
                G.GAME.blind->triggered = true;
                add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_blind_juice));
                card_eval_status_text(&sc->n, "debuff", 0, NAN, NULL, NULL);
                continue;
            }
            RepList reps = { 0 };
            vec_push(&reps, NULL);
            {
                JokerContext ctx = base; ctx.repetition_only = true; ctx.cardarea = G.play; ctx.repetition = true;
                CardEval sev = eval_card(sc, &ctx);
                if (card_eval_nonempty(&sev) && sev.seals) for (int h = 0; h < (int)sev.seals->repetitions; h++) vec_push(&reps, sev.seals);
            }
            for (int j = 0; j < G.jokers->cards.count; j++) {
                JokerContext ctx = base; ctx.cardarea = G.play; ctx.other_card = sc; ctx.repetition = true;
                CardEval jev = eval_card(G.jokers->cards.items[j], &ctx);
                if (card_eval_nonempty(&jev) && jev.jokers) for (int h = 0; h < (int)jev.jokers->repetitions; h++) vec_push(&reps, jev.jokers);
            }
            for (int j = 0; j < reps.count; j++) {
                percent += percent_delta;
                if (reps.items[j]) card_eval_status_text(&reps.items[j]->card->n, "jokers", 0, NAN, NULL, sextra_from_eval(reps.items[j]));
                EffectList effects = { 0 };
                {
                    JokerContext ctx = { .cardarea = G.play, .full_hand = &G.play->cards, .scoring_hand = scoring_hand, .scoring_name = text };
                    CardEval cev = eval_card(sc, &ctx);
                    vec_push(&effects, effect_from_cardeval(&cev));
                }
                for (int k = 0; k < G.jokers->cards.count; k++) {
                    JokerContext ctx = base; ctx.cardarea = G.play; ctx.other_card = sc; ctx.individual = true;
                    EvalResult *ev = Card_calculate_joker(G.jokers->cards.items[k], &ctx);
                    if (ev) vec_push(&effects, effect_from_result(ev));
                }
                sc->lucky_trigger = false;
                for (int ii = 0; ii < effects.count; ii++) {
                    Effect *f = &effects.items[ii];
                    if (f->has_chips) {
                        if (f->card) juice_card(f->card);
                        hand_chips = mod_chips(hand_chips + f->chips);
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.chips = hand_chips, .has_chips = true));
                        card_eval_status_text(&sc->n, "chips", f->chips, percent, NULL, NULL);
                    }
                    if (f->has_mult) {
                        if (f->card) juice_card(f->card);
                        mult = mod_mult(mult + f->mult);
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = mult, .has_mult = true));
                        card_eval_status_text(&sc->n, "mult", f->mult, percent, NULL, NULL);
                    }
                    if (f->has_p_dollars) {
                        if (f->card) juice_card(f->card);
                        ease_dollars(f->p_dollars, false);
                        card_eval_status_text(&sc->n, "dollars", f->p_dollars, percent, NULL, NULL);
                    }
                    if (f->has_dollars) {
                        if (f->card) juice_card(f->card);
                        ease_dollars(f->dollars, false);
                        card_eval_status_text(&sc->n, "dollars", f->dollars, percent, NULL, NULL);
                    }
                    if (f->extra) {
                        if (f->card) juice_card(f->card);
                        bool ex_mult = false, ex_chips = false;
                        if (f->extra->has_mult_mod) { mult = mod_mult(mult + f->extra->mult_mod); ex_mult = true; }
                        if (f->extra->has_chip_mod) { hand_chips = mod_chips(hand_chips + f->extra->chip_mod); ex_chips = true; }
                        if (f->extra->has_swap) {
                            double old = mult;
                            mult = mod_mult(hand_chips);
                            hand_chips = mod_chips(old);
                            ex_chips = ex_mult = true;
                        }
                        if (f->extra->func) f->extra->func(f->extra->func_ctx);
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.chips = hand_chips, .has_chips = ex_chips, .mult = mult, .has_mult = ex_mult));
                        card_eval_status_text(&sc->n, "extra", 0, percent, NULL, sextra_from_extra(f->extra));
                    }
                    if (f->has_x_mult) {
                        if (f->card) juice_card(f->card);
                        mult = mod_mult(mult * f->x_mult);
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = mult, .has_mult = true));
                        card_eval_status_text(&sc->n, "x_mult", f->x_mult, percent, NULL, NULL);
                    }
                    if (f->edition) {
                        EvalResult *ed = f->edition;
                        hand_chips = mod_chips(hand_chips + (ed->has_chip_mod ? ed->chip_mod : 0));
                        mult = mult + (ed->has_mult_mod ? ed->mult_mod : 0);
                        mult = mod_mult(mult * (ed->has_Xmult_mod ? ed->Xmult_mod : 1));
                        update_hand_text(HTC(.delay = 0, .has_delay = true),
                                         HTV(.chips = hand_chips, .has_chips = ed->has_chip_mod, .mult = mult, .has_mult = ed->has_mult_mod || ed->has_Xmult_mod));
                        const char *msg = ed->has_chip_mod ? localize_variable("a_chips", LV(num_str(ed->chip_mod)))
                                        : ed->has_mult_mod ? localize_variable("a_mult", LV(num_str(ed->mult_mod)))
                                        : ed->has_Xmult_mod ? localize_variable("a_xmult", LV(num_str(ed->Xmult_mod))) : NULL;
                        card_eval_status_text(&sc->n, "extra", 0, percent, NULL,
                                              SEXTRA(.message = msg, .chip_mod = ed->has_chip_mod, .mult_mod = ed->has_mult_mod, .colour = G.C.DARK_EDITION, .edition = true));
                    }
                }
                vec_free(&effects);
            }
            vec_free(&reps);
        }

        delay(0.3);
        bool mod_percent = false;
        for (int i = 0; i < G.hand->cards.count; i++) {
            Card *hc = G.hand->cards.items[i];
            if (mod_percent) percent += percent_delta;
            mod_percent = false;
            RepList reps = { 0 };
            vec_push(&reps, NULL);
            for (int j = 0; j < reps.count; j++) {
                if (reps.items[j]) {
                    card_eval_status_text(&reps.items[j]->card->n, "jokers", 0, NAN, NULL, sextra_from_eval(reps.items[j]));
                    percent += percent_delta;
                }
                EffectList effects = { 0 };
                {
                    JokerContext ctx = base; ctx.cardarea = G.hand;
                    CardEval cev = eval_card(hc, &ctx);
                    vec_push(&effects, effect_from_cardeval(&cev));
                }
                for (int k = 0; k < G.jokers->cards.count; k++) {
                    JokerContext ctx = base; ctx.cardarea = G.hand; ctx.other_card = hc; ctx.individual = true;
                    EvalResult *ev = Card_calculate_joker(G.jokers->cards.items[k], &ctx);
                    if (ev) { mod_percent = true; vec_push(&effects, effect_from_result(ev)); }
                }
                if (j == 0) {
                    JokerContext sctx = base; sctx.repetition_only = true; sctx.cardarea = G.hand; sctx.repetition = true;
                    effects_card_ctx(&effects, &sctx);
                    CardEval sev = eval_card(hc, &sctx);
                    if (card_eval_nonempty(&sev) && (!effects.items[0].empty || effects.count > 1) && sev.seals)
                        for (int h = 0; h < (int)sev.seals->repetitions; h++) vec_push(&reps, sev.seals);
                    for (int jj = 0; jj < G.jokers->cards.count; jj++) {
                        JokerContext ctx = base; ctx.cardarea = G.hand; ctx.other_card = hc; ctx.repetition = true;
                        effects_card_ctx(&effects, &ctx);
                        CardEval jev = eval_card(G.jokers->cards.items[jj], &ctx);
                        if (card_eval_nonempty(&jev) && jev.jokers) for (int h = 0; h < (int)jev.jokers->repetitions; h++) vec_push(&reps, jev.jokers);
                    }
                }
                for (int ii = 0; ii < effects.count; ii++) {
                    Effect *f = &effects.items[ii];
                    if (f->card) { mod_percent = true; juice_event(f->card); }
                    if (f->has_dollars) {
                        ease_dollars(f->dollars, false);
                        card_eval_status_text(&hc->n, "dollars", f->dollars, percent, NULL, NULL);
                    }
                    if (f->has_h_mult) {
                        mod_percent = true;
                        mult = mod_mult(mult + f->h_mult);
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = mult, .has_mult = true));
                        card_eval_status_text(&hc->n, "h_mult", f->h_mult, percent, NULL, NULL);
                    }
                    if (f->has_x_mult) {
                        mod_percent = true;
                        mult = mod_mult(mult * f->x_mult);
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = mult, .has_mult = true));
                        card_eval_status_text(&hc->n, "x_mult", f->x_mult, percent, NULL, NULL);
                    }
                    if (f->src && f->src->message) {
                        mod_percent = true;
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = mult, .has_mult = true));
                        card_eval_status_text(&hc->n, "extra", 0, percent, NULL, sextra_from_eval(f->src));
                    }
                }
                vec_free(&effects);
            }
            vec_free(&reps);
        }

        /* joker effects */
        percent += percent_delta;
        int total = G.jokers->cards.count + G.consumeables->cards.count;
        for (int i = 0; i < total; i++) {
            Card *_card = i < G.jokers->cards.count ? G.jokers->cards.items[i] : G.consumeables->cards.items[i - G.jokers->cards.count];
            JokerContext ectx = base; ectx.cardarea = G.jokers; ectx.edition = true;
            CardEval ee = eval_card(_card, &ectx);
            if (ee.jokers) {
                ee.jokers->edition = true;
                if (ee.jokers->has_chip_mod) {
                    hand_chips = mod_chips(hand_chips + ee.jokers->chip_mod);
                    update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.chips = hand_chips, .has_chips = true));
                    card_eval_status_text(&_card->n, "jokers", 0, percent, NULL,
                                          SEXTRA(.message = localize_variable("a_chips", LV(num_str(ee.jokers->chip_mod))), .chip_mod = true, .colour = G.C.EDITION, .edition = true));
                }
                if (ee.jokers->has_mult_mod) {
                    mult = mod_mult(mult + ee.jokers->mult_mod);
                    update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = mult, .has_mult = true));
                    card_eval_status_text(&_card->n, "jokers", 0, percent, NULL,
                                          SEXTRA(.message = localize_variable("a_mult", LV(num_str(ee.jokers->mult_mod))), .mult_mod = true, .colour = G.C.DARK_EDITION, .edition = true));
                }
                percent += percent_delta;
            }
            JokerContext jctx = base; jctx.cardarea = G.jokers; jctx.joker_main = true;
            CardEval je = eval_card(_card, &jctx);
            if (je.jokers) {
                bool ex_mult = false, ex_chips = false;
                if (je.jokers->has_mult_mod) { mult = mod_mult(mult + je.jokers->mult_mod); ex_mult = true; }
                if (je.jokers->has_chip_mod) { hand_chips = mod_chips(hand_chips + je.jokers->chip_mod); ex_chips = true; }
                if (je.jokers->has_Xmult_mod) { mult = mod_mult(mult * je.jokers->Xmult_mod); ex_mult = true; }
                update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.chips = hand_chips, .has_chips = ex_chips, .mult = mult, .has_mult = ex_mult));
                status_jokers(_card, percent, je.jokers);
                percent += percent_delta;
            }
            for (int k = 0; k < G.jokers->cards.count; k++) {
                Card *v = G.jokers->cards.items[k];
                JokerContext octx = base; octx.other_joker = _card; octx.other_joker_ctx = true;
                EvalResult *eff = Card_calculate_joker(v, &octx);
                if (eff) {
                    bool ex_mult = false, ex_chips = false;
                    if (eff->has_mult_mod) { mult = mod_mult(mult + eff->mult_mod); ex_mult = true; }
                    if (eff->has_chip_mod) { hand_chips = mod_chips(hand_chips + eff->chip_mod); ex_chips = true; }
                    if (eff->has_Xmult_mod) { mult = mod_mult(mult * eff->Xmult_mod); ex_mult = true; }
                    if (ex_mult || ex_chips) {
                        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.chips = hand_chips, .has_chips = ex_chips, .mult = mult, .has_mult = ex_mult));
                        status_jokers(v, percent, eff);
                    }
                    percent += percent_delta;
                }
            }
            if (ee.jokers) {
                if (ee.jokers->has_Xmult_mod) {
                    mult = mod_mult(mult * ee.jokers->Xmult_mod);
                    update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = mult, .has_mult = true));
                    card_eval_status_text(&_card->n, "jokers", 0, percent, NULL,
                                          SEXTRA(.message = localize_variable("a_xmult", LV(num_str(ee.jokers->Xmult_mod))), .colour = G.C.EDITION, .edition = true));
                }
                percent += percent_delta;
            }
        }

        double nu_chip = hand_chips, nu_mult = mult;
        bool changed = false;
        Back_trigger_effect(G.GAME.selected_back, "final_scoring_step", &nu_chip, &nu_mult, &changed);
        if (changed) { mult = mod_mult(nu_mult); hand_chips = mod_chips(nu_chip); }
        else { mult = mod_mult(mult); hand_chips = mod_chips(hand_chips); }

        CardList *destroyed_list = NEW(CardList);
        for (int i = 0; i < scoring_hand->count; i++) {
            Card *sc = scoring_hand->items[i];
            bool destroyed = false;
            highlight_card(sc, (i + 1 - 0.999) / (scoring_hand->count - 0.998), "down");
            for (int j = 0; j < G.jokers->cards.count; j++) {
                EvalResult *d = Card_calculate_joker(G.jokers->cards.items[j], JCTX(.destroying_card = true, .card = sc, .full_hand = &G.play->cards));
                if (d) { destroyed = true; break; }
            }
            if (str_eq(sc->ability.name, "Glass Card") && !sc->debuff &&
                pseudorandom_seed(pseudoseed("glass")) < G.GAME.probabilities.normal / sc->ability.extra.num)
                destroyed = true;
            if (destroyed) {
                if (str_eq(sc->ability.name, "Glass Card")) sc->shattered = true;
                else sc->destroyed = true;
                vec_push(destroyed_list, sc);
            }
        }
        for (int j = 0; j < G.jokers->cards.count; j++)
            eval_card(G.jokers->cards.items[j], JCTX(.cardarea = G.jokers, .remove_playing_cards = true, .removed = destroyed_list));
        CardList glass = { 0 };
        for (int k = 0; k < destroyed_list->count; k++) if (destroyed_list->items[k]->shattered) vec_push(&glass, destroyed_list->items[k]);
        UNLOCK(.type = "shatter", .shattered = &glass);
        vec_free(&glass);
        for (int i = 0; i < destroyed_list->count; i++) {
            CardCtx *c = CTX(CardCtx); c->card = destroyed_list->items[i];
            add_event(EVENT(.func = ev_destroy_card, .ctx = c));
        }
    } else {
        mult = mod_mult(0);
        hand_chips = mod_chips(0);
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_blind_juice));
        play_area_status_text("Not Allowed!", false, NAN);
        for (int i = 0; i < G.jokers->cards.count; i++) {
            JokerContext ctx = base; ctx.cardarea = G.jokers; ctx.debuffed_hand = true;
            CardEval ev = eval_card(G.jokers->cards.items[i], &ctx);
            if (ev.jokers) {
                status_jokers(G.jokers->cards.items[i], percent, ev.jokers);
                percent += percent_delta;
            }
        }
    }
    TotalCtx *tc = CTX(TotalCtx); tc->chip_total = floor(hand_chips * mult);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_final_hand_text, .ctx = tc));
    check_and_set_high_score("hand", hand_chips * mult);
    UNLOCK(.type = "chip_score", .chips = floor(hand_chips * mult));
    if (hand_chips * mult > 0) {
        delay(0.8);
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_chips2));
    }
    add_event(EVENT(.trigger = TRIG_EASE, .blocking = 0, .ref = REFN(&G.GAME.chips), .ease_to = G.GAME.chips + floor(hand_chips * mult), .delay = 0.5,
                    .ease_func = ease_floor));
    add_event(EVENT(.trigger = TRIG_EASE, .blocking = 1, .ref = REFN(&G.GAME.current_round.current_hand.chip_total), .ease_to = 0, .delay = 0.5,
                    .ease_func = ease_floor));
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_clear_handname));
    delay(0.3);
    for (int i = 0; i < G.jokers->cards.count; i++) {
        JokerContext ctx = base; ctx.cardarea = G.jokers; ctx.after = true;
        CardEval ev = eval_card(G.jokers->cards.items[i], &ctx);
        if (ev.jokers) {
            status_jokers(G.jokers->cards.items[i], percent, ev.jokers);
            percent += percent_delta;
        }
    }
    ScoringCtx *s = CTX(ScoringCtx); s->scoring = scoring_hand;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_perma_debuff, .ctx = s));
}

/* ------------------------------------------------------------------ */
/* draw helpers                                                        */
/* ------------------------------------------------------------------ */
void G_FUNCS_draw_from_play_to_discard(void) {
    int play_count = G.play->cards.count, it = 1;
    CardList copy = { 0 };
    for (int k = 0; k < G.play->cards.count; k++) vec_push(&copy, G.play->cards.items[k]);
    for (int k = 0; k < copy.count; k++) {
        Card *v = copy.items[k];
        if (!v->shattered && !v->destroyed) {
            draw_card(G.play, G.discard, it * 100.0 / play_count, "down", false, v, NAN, false, false, NAN, false);
            it++;
        }
    }
    vec_free(&copy);
}

void G_FUNCS_draw_from_play_to_hand(CardList *cards) {
    int n = cards->count;
    for (int i = 0; i < n; i++)
        if (!cards->items[i]->shattered && !cards->items[i]->destroyed)
            draw_card(G.play, G.hand, (i + 1) * 100.0 / n, "up", true, cards->items[i], NAN, false, false, NAN, false);
}

EVFN(ev_discard_to_deck) {
    (void)e_; (void)p_;
    int n = G.discard->cards.count;
    for (int i = 1; i <= n; i++) draw_card(G.discard, G.deck, i * 100.0 / n, "up", false, NULL, 0.005, i % 2 == 0, false, MAX((21 - i) / 20.0, 0.7), false);
    return true;
}
void G_FUNCS_draw_from_discard_to_deck(void) { add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_discard_to_deck)); }

void G_FUNCS_draw_from_hand_to_deck(void) {
    int n = G.hand->cards.count;
    for (int i = 1; i <= n; i++) draw_card(G.hand, G.deck, i * 100.0 / n, "down", false, NULL, 0.08, false, false, NAN, false);
}

void G_FUNCS_draw_from_hand_to_discard(void) {
    int n = G.hand->cards.count;
    for (int i = 1; i <= n; i++) draw_card(G.hand, G.discard, i * 100.0 / n, "down", false, NULL, 0.07, false, false, NAN, false);
}

/* ------------------------------------------------------------------ */
/* evaluate_round                                                      */
/* ------------------------------------------------------------------ */
EVFN(ev_defeat) { (void)e_; (void)p_; Blind_defeat(G.GAME.blind, false); return true; }
EVFN(ev_eval_bg) { (void)e_; (void)p_; ease_background_colour_blind(STATE_ROUND_EVAL, ""); return true; }

void G_FUNCS_evaluate_round(void) {
    double pitch = 0.95, dollars = 0;
    Blind *b = G.GAME.blind;
    if (G.GAME.chips - b->chips >= 0) {
        add_round_eval_row((RoundEvalRow){ .dollars = b->dollars, .has_dollars = true, .name = "blind1", .pitch = pitch, .has_pitch = true });
        pitch += 0.06;
        dollars += b->dollars;
    } else {
        add_round_eval_row((RoundEvalRow){ .dollars = 0, .has_dollars = true, .name = "blind1", .pitch = pitch, .has_pitch = true, .saved = true });
        pitch += 0.06;
    }
    add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 1.3 * MIN(b->dollars + 2, 7) / 2 * 0.15 + 0.5, .func = ev_defeat));
    delay(0.2);
    add_event(EVENT(.func = ev_eval_bg));
    Back_trigger_effect(G.GAME.selected_back, "eval", NULL, NULL, NULL);
    CurrentRound *cr = &G.GAME.current_round;
    Modifiers *m = &G.GAME.modifiers;
    if (cr->hands_left > 0 && !m->no_extra_hand_money) {
        double mph = m->money_per_hand ? m->money_per_hand : 1;
        add_round_eval_row((RoundEvalRow){ .dollars = cr->hands_left * mph, .has_dollars = true, .disp = cr->hands_left, .has_disp = true, .bonus = true, .name = "hands",
                                           .pitch = pitch, .has_pitch = true });
        pitch += 0.06;
        dollars += cr->hands_left * mph;
    }
    if (cr->discards_left > 0 && m->money_per_discard) {
        add_round_eval_row((RoundEvalRow){ .dollars = cr->discards_left * m->money_per_discard, .has_dollars = true, .disp = cr->discards_left, .has_disp = true,
                                           .bonus = true, .name = "discards", .pitch = pitch, .has_pitch = true });
        pitch += 0.06;
        dollars += cr->discards_left * m->money_per_discard;
    }
    for (int i = 0; i < G.jokers->cards.count; i++) {
        double ret = Card_calculate_dollar_bonus(G.jokers->cards.items[i]);
        if (!isnan(ret)) {
            add_round_eval_row((RoundEvalRow){ .dollars = ret, .has_dollars = true, .bonus = true, .name = intern(tfmt("joker%d", i + 1)), .pitch = pitch, .has_pitch = true,
                                               .card = G.jokers->cards.items[i] });
            pitch += 0.06;
            dollars += ret;
        }
    }
    for (int i = 0; i < G.GAME.tags.count; i++) {
        TagEvalResult te;
        Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "eval" }, NULL, &te);
        if (te.valid) {
            add_round_eval_row((RoundEvalRow){ .dollars = te.dollars, .has_dollars = true, .bonus = true, .name = intern(tfmt("tag%d", i + 1)), .pitch = pitch,
                                               .has_pitch = true, .condition = te.condition, .pos = te.pos, .tag = te.tag });
            pitch += 0.06;
            dollars += te.dollars;
        }
    }
    if (G.GAME.dollars >= 5 && !m->no_interest) {
        double interest = G.GAME.interest_amount * MIN(floor(G.GAME.dollars / 5), G.GAME.interest_cap / 5);
        add_round_eval_row((RoundEvalRow){ .bonus = true, .name = "interest", .pitch = pitch, .has_pitch = true, .dollars = interest, .has_dollars = true });
        pitch += 0.06;
        if (!G.GAME.seeded && !G.GAME.challenge) {
            DVal *cs = dv_get_or_create_table(profile(), "career_stats");
            if (interest == G.GAME.interest_amount * G.GAME.interest_cap / 5)
                dv_set_num(cs, "c_round_interest_cap_streak", dv_get_num(cs, "c_round_interest_cap_streak", 0) + 1);
            else dv_set_num(cs, "c_round_interest_cap_streak", 0);
        }
        UNLOCK(.type = "interest_streak");
        dollars += interest;
    }
    add_round_eval_row((RoundEvalRow){ .name = "bottom", .dollars = dollars, .has_dollars = true });
}

/* ------------------------------------------------------------------ */
/* tutorial                                                            */
/* ------------------------------------------------------------------ */
static DVal *tp(void) { return &G.SETTINGS.tutorial_progress; }
static bool tp_part(const char *list, const char *key) { return dv_truthy(dv_get(dv_get(tp(), list), key)); }
static void tp_set(const char *list, const char *key) { dv_set_bool(dv_get_or_create_table(tp(), list), key, true); }

void G_FUNCS_tutorial_controller(void) {
    if (G.F_SKIP_TUTORIAL) {
        G.SETTINGS.tutorial_complete = true;
        dv_free(&G.SETTINGS.tutorial_progress);
        return;
    }
    if (tp()->t != DV_TABLE) {
        dv_parse("{forced_shop = {'j_joker', 'c_empress'}, forced_voucher = 'v_grabber', forced_tags = {'tag_handy', 'tag_garbage'}, hold_parts = {}, completed_parts = {}}",
                 tp());
    }
    if (G.SETTINGS.paused || G.SETTINGS.tutorial_complete) return;
    if (G.STATE == STATE_BLIND_SELECT && G.blind_select && !tp_part("completed_parts", "small_blind")) {
        dv_set_str(tp(), "section", "small_blind");
        G_FUNCS_tutorial_part("small_blind");
        tp_set("completed_parts", "small_blind");
        game_save_progress();
    }
    if (G.STATE == STATE_BLIND_SELECT && G.blind_select && !tp_part("completed_parts", "big_blind") && G.GAME.round > 0) {
        dv_set_str(tp(), "section", "big_blind");
        G_FUNCS_tutorial_part("big_blind");
        tp_set("completed_parts", "big_blind");
        dv_set(tp(), "forced_tags", dv_nil());
        game_save_progress();
    }
    if (G.STATE == STATE_SELECTING_HAND && !tp_part("completed_parts", "second_hand") && tp_part("hold_parts", "big_blind")) {
        dv_set_str(tp(), "section", "second_hand");
        G_FUNCS_tutorial_part("second_hand");
        tp_set("completed_parts", "second_hand");
        game_save_progress();
    }
    if (tp_part("hold_parts", "second_hand")) G.SETTINGS.tutorial_complete = true;
    if (!tp_part("completed_parts", "first_hand_section")) {
        if (G.STATE == STATE_SELECTING_HAND && !tp_part("completed_parts", "first_hand")) {
            dv_set_str(tp(), "section", "first_hand");
            G_FUNCS_tutorial_part("first_hand");
            tp_set("completed_parts", "first_hand");
            game_save_progress();
        }
        static const char *seq[3][2] = { { "first_hand_2", "first_hand" }, { "first_hand_3", "first_hand_2" }, { "first_hand_4", "first_hand_3" } };
        for (int i = 0; i < 3; i++) {
            if (G.STATE == STATE_SELECTING_HAND && !tp_part("completed_parts", seq[i][0]) && tp_part("hold_parts", seq[i][1])) {
                G_FUNCS_tutorial_part(seq[i][0]);
                tp_set("completed_parts", seq[i][0]);
                if (i == 2) tp_set("completed_parts", "first_hand_section");
                game_save_progress();
            }
        }
    }
    if (G.STATE == STATE_SHOP && G.shop && G.shop->n.VT.y < 5 && !tp_part("completed_parts", "shop_1")) {
        dv_set_str(tp(), "section", "shop");
        G_FUNCS_tutorial_part("shop_1");
        tp_set("completed_parts", "shop_1");
        dv_set(tp(), "forced_voucher", dv_nil());
        game_save_progress();
    }
}

/* blind_select.UIRoot.children[1].children[k].config.object */
static UIBox *blind_opt(int k) {
    if (!G.blind_select || !G.blind_select->UIRoot) return NULL;
    UIElement *r1 = uie_child(G.blind_select->UIRoot, 1);
    UIElement *ck = r1 ? uie_child(r1, k) : NULL;
    return ck && ck->config->object ? (UIBox *)ck->config->object : NULL;
}
static Node *blind_opt_node(int k) {
    UIElement *r1 = G.blind_select && G.blind_select->UIRoot ? uie_child(G.blind_select->UIRoot, 1) : NULL;
    UIElement *ck = r1 ? uie_child(r1, k) : NULL;
    return ck ? &ck->n : NULL;
}
static Node *blind_opt_uie(int k, const char *id) {
    UIBox *b = blind_opt(k);
    UIElement *e = b ? UIBox_get_UIE_by_ID(b, id, NULL) : NULL;
    return e ? &e->n : NULL;
}
static Node *hud_uie(const char *id) { UIElement *e = G.HUD ? UIBox_get_UIE_by_ID(G.HUD, id, NULL) : NULL; return e ? &e->n : NULL; }
static Node *dollar_parent3(void) {
    Node *n = hud_uie("dollar_text_UI");
    for (int i = 0; i < 3 && n; i++) n = n->parent;
    return n;
}
static Node *snap_blind1(void) { return blind_opt_uie(1, "select_blind_button"); }
static Node *snap_blind2(void) { return blind_opt_uie(2, "select_blind_button"); }
static Node *snap_run_info(void) { return hud_uie("run_info_button"); }
static Node *snap_hand1(void) { return G.hand && G.hand->cards.count ? &G.hand->cards.items[0]->n : NULL; }
static Node *snap_shop_joker2(void) { return G.shop_jokers && G.shop_jokers->cards.count >= 2 ? &G.shop_jokers->cards.items[1]->n : NULL; }
static Node *snap_shop_joker1(void) { return G.shop_jokers && G.shop_jokers->cards.count >= 1 ? &G.shop_jokers->cards.items[0]->n : NULL; }
static Node *snap_voucher1(void) { return G.shop_vouchers && G.shop_vouchers->cards.count ? &G.shop_vouchers->cards.items[0]->n : NULL; }
static Node *snap_booster1(void) { return G.shop_booster && G.shop_booster->cards.count ? &G.shop_booster->cards.items[0]->n : NULL; }
static Node *snap_next_round(void) { UIElement *e = G.shop ? UIBox_get_UIE_by_ID(G.shop, "next_round_button", NULL) : NULL; return e ? &e->n : NULL; }

static void hl_shop_common(PtrVec *o) { if (G.SHOP_SIGN) vec_push(o, &G.SHOP_SIGN->n); Node *d = dollar_parent3(); if (d) vec_push(o, d); }
static void hl_s3(PtrVec *o) { hl_shop_common(o); if (G.jokers->cards.count) vec_push(o, &G.jokers->cards.items[0]->n); }
static void hl_s5(PtrVec *o) { hl_shop_common(o); vec_push(o, &G.jokers->n); }
static void hl_s6(PtrVec *o) { hl_shop_common(o); if (G.shop_jokers && G.shop_jokers->cards.count) vec_push(o, &G.shop_jokers->cards.items[0]->n); }
static void hl_s7(PtrVec *o) { hl_shop_common(o); if (G.consumeables->cards.count) vec_push(o, &vec_last(&G.consumeables->cards)->n); }
static void hl_s8(PtrVec *o) { hl_shop_common(o); vec_push(o, &G.consumeables->n); }
static void hl_s9(PtrVec *o) { hl_shop_common(o); if (G.shop_vouchers) vec_push(o, &G.shop_vouchers->n); }
static void hl_s11(PtrVec *o) { hl_shop_common(o); if (G.shop_booster) vec_push(o, &G.shop_booster->n); }
static void hl_s12(PtrVec *o) { Node *n = snap_next_round(); if (n) vec_push(o, n); }

typedef struct { int step; const char *part; } TutEndCtx;
EVFN(ev_tutorial_end) {
    TutEndCtx *c = p_; (void)e_;
    UIBox *ot = G.OVERLAY_TUTORIAL;
    if (!ot) return true;
    if ((ot->step == c->step && !ot->step_complete) || ot->skip_steps) {
        if (ot->Jimbo) node_remove(&ot->Jimbo->n);
        if (ot->content) node_remove(&ot->content->n);
        node_remove(&ot->n);
        G.OVERLAY_TUTORIAL = NULL;
        tp_set("hold_parts", c->part);
        return true;
    }
    return ot->step > c->step || ot->skip_steps;
}

#define ATTACH(maj, t, ox, oy) .attach = ALIGN_ARGS(.major = (maj), .type = (t), .has_offset = true, .offset = { (ox), (oy) }), .has_attach = true
#define HL(...) .highlight = { __VA_ARGS__ }, .nhighlight = (int)(sizeof((Node *[]){ __VA_ARGS__ }) / sizeof(Node *))

void G_FUNCS_tutorial_part(const char *part) {
    int step = 1;
    G.SETTINGS.paused = true;
    if (str_eq(part, "small_blind")) {
        step = TUTORIAL(.text_key = "sb_1", ATTACH(G.ROOM_ATTACH, "cm", 0, 0), .step = step);
        step = TUTORIAL(.text_key = "sb_2", ATTACH(G.ROOM_ATTACH, "cm", 0, 0), .step = step);
        step = TUTORIAL(.text_key = "sb_3", HL(blind_opt_uie(1, "blind_name"), blind_opt_uie(1, "blind_desc")), ATTACH(blind_opt_node(1), "tr", 2, 4), .step = step);
        step = TUTORIAL(.text_key = "sb_4", HL(blind_opt_node(1)), .snap_to = snap_blind1, ATTACH(blind_opt_node(1), "tr", 2, 4), .step = step, .no_button = true,
                        .button_listen = G_FUNCS_select_blind);
    } else if (str_eq(part, "big_blind")) {
        step = TUTORIAL(.text_key = "bb_1", HL(blind_opt_uie(2, "blind_name"), blind_opt_uie(2, "blind_desc")), .hard_set = true, ATTACH(&G.HUD->n, "cm", 0, -2), .step = step);
        step = TUTORIAL(.text_key = "bb_2", HL(blind_opt_uie(2, "blind_name"), blind_opt_uie(2, "tag_desc")), ATTACH(&G.HUD->n, "cm", 0, -2), .step = step);
        step = TUTORIAL(.text_key = "bb_3", HL(blind_opt_uie(3, "blind_name"), blind_opt_uie(3, "blind_desc")), ATTACH(&G.HUD->n, "cm", 0, -2), .step = step);
        step = TUTORIAL(.text_key = "bb_4", HL(blind_opt_uie(3, "blind_name"), blind_opt_uie(3, "blind_desc"), blind_opt_uie(3, "blind_extras"), hud_uie("hud_ante")),
                        ATTACH(&G.HUD->n, "cm", 0, -2), .step = step);
        step = TUTORIAL(.text_key = "bb_5", .loc_vars = LV(num_str(G.GAME.win_ante)), HL(&G.blind_select->n, hud_uie("hud_ante")), ATTACH(&G.HUD->n, "cm", 0, -2),
                        .step = step, .no_button = true, .snap_to = snap_blind2, .button_listen = G_FUNCS_select_blind);
    } else if (str_eq(part, "first_hand")) {
        step = TUTORIAL(.text_key = "fh_1", ATTACH(G.ROOM_ATTACH, "cm", 0, 0), .step = step);
        step = TUTORIAL(.text_key = "fh_2", HL(hud_uie("hand_text_area")), ATTACH(G.ROOM_ATTACH, "cm", 0, 0), .step = step);
        step = TUTORIAL(.text_key = "fh_3", ATTACH(G.ROOM_ATTACH, "cm", 0, 0), HL(hud_uie("run_info_button")), .no_button = true, .button_listen = G_FUNCS_run_info,
                        .snap_to = snap_run_info, .step = step);
    } else if (str_eq(part, "first_hand_2")) {
        UIElement *pb = G.buttons ? UIBox_get_UIE_by_ID(G.buttons, "play_button", NULL) : NULL;
        step = TUTORIAL(.hard_set = true, .text_key = "fh_4", HL(&G.hand->n, hud_uie("run_info_button")), ATTACH(&G.hand->n, "cl", -1.5, 0), .snap_to = snap_hand1, .step = step);
        step = TUTORIAL(.text_key = "fh_5", HL(&G.hand->n, pb ? &pb->n : NULL, hud_uie("run_info_button")), ATTACH(&G.hand->n, "cl", -1.5, 0), .no_button = true,
                        .button_listen = G_FUNCS_play_cards_from_highlighted, .step = step);
    } else if (str_eq(part, "first_hand_3")) {
        UIElement *db = G.buttons ? UIBox_get_UIE_by_ID(G.buttons, "discard_button", NULL) : NULL;
        step = TUTORIAL(.hard_set = true, .text_key = "fh_6", HL(&G.hand->n, db ? &db->n : NULL, hud_uie("run_info_button")), ATTACH(&G.hand->n, "cl", -1.5, 0),
                        .no_button = true, .button_listen = G_FUNCS_discard_cards_from_highlighted_button, .step = step);
    } else if (str_eq(part, "first_hand_4")) {
        Node *hh = hud_uie("hud_hands");
        step = TUTORIAL(.hard_set = true, .text_key = "fh_7", HL(hh ? hh->parent : NULL), ATTACH(G.ROOM_ATTACH, "cm", 0, 0), .step = step);
        step = TUTORIAL(.text_key = "fh_8", HL(hh ? hh->parent : NULL, hud_uie("row_dollars_chips"), G.HUD_blind ? &G.HUD_blind->n : NULL), ATTACH(G.ROOM_ATTACH, "cm", 0, 0),
                        .step = step);
    } else if (str_eq(part, "second_hand")) {
        step = TUTORIAL(.text_key = "sh_1", .hard_set = true, HL(&G.jokers->n), ATTACH(&G.HUD->n, "cm", 0, -2), .step = step);
        CardList emp = { 0 };
        find_joker("The Empress", false, &emp);
        if (emp.count) {
            Node *en = &emp.items[0]->n;
            step = TUTORIAL(.text_key = "sh_2", HL(en), ATTACH(&G.HUD->n, "cm", 0, -2), .step = step);
            step = TUTORIAL(.text_key = "sh_3", ATTACH(&G.HUD->n, "cm", 0, -2), HL(en, &G.hand->n), .no_button = true, .button_listen = G_FUNCS_use_card,
                            .snap_to = snap_hand1, .step = step);
        }
        vec_free(&emp);
    } else if (str_eq(part, "shop_1")) {
        Node *shop = &G.shop->n;
        step = TUTORIAL(.hard_set = true, .text_key = "s_1", HL(G.SHOP_SIGN ? &G.SHOP_SIGN->n : NULL, dollar_parent3()), ATTACH(shop, "tm", 0, 4), .step = step);
        step = TUTORIAL(.text_key = "s_2", HL(G.SHOP_SIGN ? &G.SHOP_SIGN->n : NULL, dollar_parent3(), snap_shop_joker2()), .snap_to = snap_shop_joker2,
                        ATTACH(shop, "tr", -0.5, 6), .no_button = true, .button_listen = G_FUNCS_buy_from_shop, .step = step);
        step = TUTORIAL(.text_key = "s_3", .loc_vars = LV(tfmt("%d", G.P_CENTER_POOLS.Joker.count)), .highlight_fn = hl_s3, ATTACH(shop, "tm", 0, 6), .step = step);
        step = TUTORIAL(.text_key = "s_4", .highlight_fn = hl_s3, ATTACH(shop, "tm", 0, 6), .step = step);
        step = TUTORIAL(.text_key = "s_5", .highlight_fn = hl_s5, ATTACH(shop, "tm", 0, 6), .step = step);
        step = TUTORIAL(.text_key = "s_6", .highlight_fn = hl_s6, .snap_to = snap_shop_joker1, .no_button = true, .button_listen = G_FUNCS_buy_from_shop,
                        ATTACH(shop, "tr", -0.5, 6), .step = step);
        step = TUTORIAL(.text_key = "s_7", .highlight_fn = hl_s7, ATTACH(shop, "tm", 0, 6), .step = step);
        step = TUTORIAL(.text_key = "s_8", .highlight_fn = hl_s8, ATTACH(shop, "tm", 0, 6), .step = step);
        step = TUTORIAL(.text_key = "s_9", .highlight_fn = hl_s9, .snap_to = snap_voucher1, ATTACH(shop, "tr", -4, 6), .step = step);
        step = TUTORIAL(.text_key = "s_10", .highlight_fn = hl_s9, ATTACH(shop, "tr", -4, 6), .step = step);
        step = TUTORIAL(.text_key = "s_11", .highlight_fn = hl_s11, .snap_to = snap_booster1, ATTACH(shop, "tl", 3, 6), .step = step);
        step = TUTORIAL(.text_key = "s_12", .highlight_fn = hl_s12, .snap_to = snap_next_round, .no_button = true, .button_listen = G_FUNCS_toggle_shop,
                        ATTACH(shop, "tm", 0, 6), .step = step);
    }
    TutEndCtx *c = CTX(TutEndCtx); c->step = step; c->part = intern(part);
    add_event_q(EVENT(.blockable = 0, .timer = TIMER_REAL, .func = ev_tutorial_end, .ctx = c), Q_TUTORIAL, false);
    G.SETTINGS.paused = false;
}
