/* card_joker.c - Card:calculate_seal and Card:calculate_joker from card.lua */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define EVCTX(T) T *c = (T *)p_; (void)e_
typedef struct { Card *card; Card *other; double a; int i; } JCtx;

#define NEWR() eval_new()
static EvalResult *rmsg(char *msg) { EvalResult *r = NEWR(); r->message = msg; r->has_message = msg != NULL; return r; }
static char *dup(const char *s) { return xstrdup(s); }
static char *var1(const char *key, double v) { return localize_variable(key, LV(num_str(v))); }
static char *money_msg(double v) { return xstrdup(tfmt("%s%s", localize_key("$"), num_str(v))); }
static Card *bpc(JokerContext *ctx, Card *self) { return ctx->blueprint_card ? ctx->blueprint_card : self; }
static bool ph(JokerContext *ctx, const char *name) { return poker_hand_has(ctx->poker_hands, name); }
static int cons_space(void) { return (int)(G.consumeables->config.card_limit - (G.consumeables->cards.count + G.GAME.consumeable_buffer)); }

/* ------------------------------------------------------------------ */
/* seals                                                               */
/* ------------------------------------------------------------------ */
EVFN(ev_purple_seal) {
    (void)e_; (void)p_;
    Card *card = create_card("Tarot", G.consumeables, false, RARITY_NIL, false, false, NULL, "8ba");
    Card_add_to_deck(card, false);
    CardArea_emplace(G.consumeables, card, NULL, false);
    G.GAME.consumeable_buffer = 0;
    return true;
}

EvalResult *Card_calculate_seal(Card *c, JokerContext *ctx) {
    if (c->debuff) return NULL;
    if (ctx->repetition) {
        if (str_eq(c->seal, "Red")) {
            EvalResult *r = rmsg(dup(localize_key("k_again_ex")));
            r->repetitions = 1; r->has_repetitions = true;
            r->card = c;
            return r;
        }
    }
    if (ctx->discard) {
        if (str_eq(c->seal, "Purple") && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
            G.GAME.consumeable_buffer += 1;
            add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.0, .func = ev_purple_seal));
            card_eval_status_text(&c->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_plus_tarot"), .colour = G.C.PURPLE));
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* shared event bodies                                                 */
/* ------------------------------------------------------------------ */
typedef struct { const char *type; const char *append; } CreateCtx;
EVFN(ev_create_consumable) {
    EVCTX(CreateCtx);
    Card *card = create_card(c->type, G.consumeables, false, RARITY_NIL, false, false, NULL, c->append);
    Card_add_to_deck(card, false);
    CardArea_emplace(G.consumeables, card, NULL, false);
    G.GAME.consumeable_buffer = 0;
    return true;
}
static void queue_consumable(const char *type, const char *append, EventTrigger trig) {
    CreateCtx *cc = CTX(CreateCtx);
    cc->type = type; cc->append = append;
    add_event(EVENT(.trigger = trig, .delay = 0.0, .func = ev_create_consumable, .ctx = cc));
}

/* the shared "eaten / extinct / melted" removal animation */
EVFN(ev_food_remove) {
    EVCTX(JCtx);
    CardArea_remove_card(G.jokers, c->card, false);
    node_remove(&c->card->n);
    return true;
}
EVFN(ev_food_start) {
    EVCTX(JCtx);
    Card *self = c->card;
    sound_play("tarot1", NAN, NAN);
    self->n.T->r = -0.2;
    Card_juice_up(&self->n, 0.3, 0.4);
    self->n.states.drag.is = true;
    Sprite *ce = (Sprite *)child_get(&self->n, "center");
    if (ce) ce->n.pinch->x = true;
    JCtx *n = CTX(JCtx); n->card = self;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.3, .blockable = 0, .func = ev_food_remove, .ctx = n));
    return true;
}
static void food_remove(Card *self) {
    JCtx *c = CTX(JCtx); c->card = self;
    add_event(EVENT(.func = ev_food_start, .ctx = c));
}

typedef struct { Card *card; char *msg; Col colour; double delay; bool has_delay; } StatusCtx;
EVFN(ev_status_text) {
    EVCTX(StatusCtx);
    card_eval_status_text(&c->card->n, "extra", 0, 0, NULL, SEXTRA(.message = c->msg, .colour = c->colour, .delay = c->delay, .has_delay = c->has_delay));
    return true;
}
static void queue_status(Card *card, char *msg, Col colour) {
    StatusCtx *s = CTX(StatusCtx);
    s->card = card; s->msg = msg; s->colour = colour;
    add_event(EVENT(.func = ev_status_text, .ctx = s));
}
EVFN(ev_reset_dollar_buffer) { (void)e_; (void)p_; G.GAME.dollar_buffer = 0; return true; }
static void dollar_buffer_add(double v) {
    G.GAME.dollar_buffer += v;
    add_event(EVENT(.func = ev_reset_dollar_buffer));
}

static bool eval_loyalty(Card *card, void *ctx) { (void)ctx; return card->ability.loyalty_remaining == 0 && !G.RESET_JIGGLES; }
static bool eval_loyalty2(Card *card, void *ctx) { (void)ctx; return card->ability.loyalty_remaining == 0; }
static bool eval_dna(Card *card, void *ctx) { (void)card; (void)ctx; return G.GAME.current_round.hands_played == 0; }
static bool eval_trading(Card *card, void *ctx) { (void)card; (void)ctx; return G.GAME.current_round.discards_used == 0 && !G.RESET_JIGGLES; }
static bool eval_not_removed(Card *card, void *ctx) { (void)ctx; return !card->n.REMOVED; }

EVFN(ev_add_double_tag) {
    (void)e_; (void)p_;
    add_tag(tag_new("tag_double", false, NULL));
    sound_play("generic1", 0.9 + math_random() * 0.1, 0.8);
    sound_play("holo1", 1.2 + math_random() * 0.1, 0.4);
    return true;
}
EVFN(ev_perkeo) {
    (void)e_; (void)p_;
    CardList sorted = { 0 };
    for (int i = 0; i < G.consumeables->cards.count; i++) vec_push(&sorted, G.consumeables->cards.items[i]);
    for (int i = 1; i < sorted.count; i++) {
        Card *k = sorted.items[i]; int j = i - 1;
        while (j >= 0 && sorted.items[j]->sort_id > k->sort_id) { sorted.items[j + 1] = sorted.items[j]; j--; }
        sorted.items[j + 1] = k;
    }
    int idx = pseudorandom_index(sorted.count, pseudoseed("perkeo"), true);
    if (idx < 1) { vec_free(&sorted); return true; }
    Card *card = copy_card(sorted.items[idx - 1], NULL, NAN, 0, false);
    vec_free(&sorted);
    Card_set_edition(card, ED_NEG, true, false);
    Card_add_to_deck(card, false);
    CardArea_emplace(G.consumeables, card, NULL, false);
    return true;
}
EVFN(ev_certificate) {
    EVCTX(JCtx);
    /* pseudorandom_element(G.P_CARDS): keys sorted alphabetically */
    const char *keys[64]; int n = 0;
    for (int i = 0; i < G.n_cards; i++) keys[n++] = G.P_CARDS_LIST[i].key;
    for (int i = 1; i < n; i++) { const char *k = keys[i]; int j = i - 1; while (j >= 0 && strcmp(keys[j], k) > 0) { keys[j + 1] = keys[j]; j--; } keys[j + 1] = k; }
    int idx = pseudorandom_index(n, pseudoseed("cert_fr"), true);
    Col cols[1] = { G.C.SECONDARY_SET.Enhanced };
    Card *_card = create_playing_card((PlayingCardInit){ P_CARD(keys[idx - 1]), P_CENTER("c_base") }, G.hand, false, false, cols, 1);
    double st = pseudorandom_seed(pseudoseed("certsl"));
    if (st > 0.75) Card_set_seal(_card, "Red", true, false);
    else if (st > 0.5) Card_set_seal(_card, "Blue", true, false);
    else if (st > 0.25) Card_set_seal(_card, "Gold", true, false);
    else Card_set_seal(_card, "Purple", true, false);
    Blind_debuff_card(G.GAME.blind, _card, false);
    CardArea_sort(G.hand, NULL);
    if (c->other) Card_juice_up(&c->other->n, NAN, NAN);
    else Card_juice_up(&c->card->n, NAN, NAN);
    return true;
}
EVFN(ev_chicot_inner) {
    (void)e_; (void)p_;
    Blind_disable(G.GAME.blind);
    sound_play("timpani", NAN, NAN);
    delay(0.4);
    return true;
}
EVFN(ev_chicot) {
    EVCTX(JCtx);
    add_event(EVENT(.func = ev_chicot_inner));
    card_eval_status_text(&c->card->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("ph_boss_disabled")));
    return true;
}
typedef struct { Card *who; Card *victim; } SliceCtx;
EVFN(ev_madness_slice) {
    EVCTX(SliceCtx);
    Card_juice_up(&c->who->n, 0.8, 0.8);
    Col r[1] = { G.C.RED };
    Card_start_dissolve(c->victim, r, 1, false, 1.6, false);
    return true;
}
typedef struct { Card *self; Card *bp; double extra; } BurglarCtx;
EVFN(ev_burglar) {
    EVCTX(BurglarCtx);
    ease_discard(-G.GAME.current_round.discards_left, false, true);
    ease_hands_played(c->extra, false);
    card_eval_status_text(&c->bp->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_variable("a_hands", LV(num_str(c->extra)))));
    return true;
}
typedef struct { int n; } RiffCtx;
EVFN(ev_riff_raff) {
    EVCTX(RiffCtx);
    for (int i = 1; i <= c->n; i++) {
        Card *card = create_card("Joker", G.jokers, false, 0, false, false, NULL, "rif");
        Card_add_to_deck(card, false);
        CardArea_emplace(G.jokers, card, NULL, false);
        Card_start_materialize(card, NULL, 0, false, NAN);
        G.GAME.joker_buffer = 0;
    }
    return true;
}
EVFN(ev_cartomancer_inner) {
    (void)e_; (void)p_;
    Card *card = create_card("Tarot", G.consumeables, false, RARITY_NIL, false, false, NULL, "car");
    Card_add_to_deck(card, false);
    CardArea_emplace(G.consumeables, card, NULL, false);
    G.GAME.consumeable_buffer = 0;
    return true;
}
EVFN(ev_cartomancer) {
    EVCTX(JCtx);
    add_event(EVENT(.func = ev_cartomancer_inner));
    card_eval_status_text(&c->card->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_plus_tarot"), .colour = G.C.PURPLE));
    return true;
}
EVFN(ev_dagger) {
    EVCTX(SliceCtx);
    G.GAME.joker_buffer = 0;
    c->who->ability.mult += c->victim->sell_cost * 2;
    Card_juice_up(&c->who->n, 0.8, 0.8);
    Col g[1] = { HEX("57ecab") };
    Card_start_dissolve(c->victim, g, 1, false, 1.6, false);
    sound_play("slice1", 0.96 + math_random() * 0.08, NAN);
    return true;
}
EVFN(ev_marble) {
    (void)e_; (void)p_;
    const char *keys[64]; int n = 0;
    for (int i = 0; i < G.n_cards; i++) keys[n++] = G.P_CARDS_LIST[i].key;
    for (int i = 1; i < n; i++) { const char *k = keys[i]; int j = i - 1; while (j >= 0 && strcmp(keys[j], k) > 0) { keys[j + 1] = keys[j]; j--; } keys[j + 1] = k; }
    int idx = pseudorandom_index(n, pseudoseed("marb_fr"), true);
    G.playing_card = G.playing_card ? G.playing_card + 1 : 1;
    CardParams params = { .playing_card = G.playing_card };
    Card *card = card_new(G.play->n.T->x + G.play->n.T->w / 2, G.play->n.T->y, G.CARD_W, G.CARD_H, P_CARD(keys[idx - 1]), P_CENTER("m_stone"), &params);
    Col cols[1] = { G.C.SECONDARY_SET.Enhanced };
    Card_start_materialize(card, cols, 1, false, NAN);
    CardArea_emplace(G.play, card, NULL, false);
    vec_push(&G.playing_cards, card);
    return true;
}
EVFN(ev_deck_limit_inc) { (void)e_; (void)p_; G.deck->config.card_limit += 1; return true; }
typedef struct { Card *self; double faces; char *msg; } CainoCtx;
EVFN(ev_caino_inner) {
    EVCTX(CainoCtx);
    c->self->ability.caino_xmult += c->faces * c->self->ability.extra.num;
    return true;
}
EVFN(ev_caino) {
    EVCTX(CainoCtx);
    CainoCtx *n = CTX(CainoCtx); *n = *c;
    add_event(EVENT(.func = ev_caino_inner, .ctx = n));
    card_eval_status_text(&c->self->n, "extra", 0, 0, NULL, SEXTRA(.message = c->msg));
    return true;
}
EVFN(ev_glass_inner) {
    EVCTX(CainoCtx);
    c->self->ability.x_mult += c->self->ability.extra.num * c->faces;
    return true;
}
EVFN(ev_glass) {
    EVCTX(CainoCtx);
    CainoCtx *n = CTX(CainoCtx); *n = *c;
    add_event(EVENT(.func = ev_glass_inner, .ctx = n));
    card_eval_status_text(&c->self->n, "extra", 0, 0, NULL, SEXTRA(.message = c->msg));
    return true;
}
typedef struct { Card *self; Card *bp; double dollars; } FacelessCtx;
EVFN(ev_faceless) {
    EVCTX(FacelessCtx);
    ease_dollars(c->dollars, false);
    card_eval_status_text(&c->bp->n, "extra", 0, 0, NULL, SEXTRA(.message = money_msg(c->dollars), .colour = G.C.MONEY, .delay = 0.45, .has_delay = true));
    return true;
}
EVFN(ev_mr_bones) {
    EVCTX(JCtx);
    if (G.hand_text_area.blind_chips) node_juice_up(&G.hand_text_area.blind_chips->n, NAN, NAN);
    if (G.hand_text_area.game_chips) node_juice_up(&G.hand_text_area.game_chips->n, NAN, NAN);
    sound_play("tarot1", NAN, NAN);
    Card_start_dissolve(c->card, NULL, 0, false, NAN, false);
    return true;
}
typedef struct { Card *card; } JuiceCtx;
EVFN(ev_juice_v) {
    EVCTX(JuiceCtx);
    Card_juice_up(&c->card->n, NAN, NAN);
    return true;
}
EVFN(ev_vampire_v) {
    EVCTX(JuiceCtx);
    Card_juice_up(&c->card->n, NAN, NAN);
    c->card->vampired = false;
    return true;
}
EVFN(ev_dna_materialize) {
    EVCTX(JuiceCtx);
    Card_start_materialize(c->card, NULL, 0, false, NAN);
    return true;
}
EVFN(ev_baseball_juice) {
    EVCTX(JuiceCtx);
    Card_juice_up(&c->card->n, 0.5, 0.5);
    return true;
}
static void eight_ball_func(void *p) {
    (void)p;
    queue_consumable("Tarot", "8ba", TRIG_BEFORE);
}

/* ------------------------------------------------------------------ */
/* calculate_joker                                                     */
/* ------------------------------------------------------------------ */
static EvalResult *R_xmult_msg(double x) { EvalResult *r = rmsg(var1("a_xmult", x)); r->Xmult_mod = x; r->has_Xmult_mod = true; return r; }
static EvalResult *R_mult_msg(double x) { EvalResult *r = rmsg(var1("a_mult", x)); r->mult_mod = x; r->has_mult_mod = true; return r; }
static EvalResult *R_chips_msg(double x) { EvalResult *r = rmsg(var1("a_chips", x)); r->chip_mod = x; r->has_chip_mod = true; return r; }

EvalResult *Card_calculate_joker(Card *self, JokerContext *ctx) {
    if (self->debuff) return NULL;
    Ability *A = &self->ability;
    const char *name = A->name ? A->name : "";
#define IS_NAME(s) (!strcmp(name, (s)))
    if (str_eq(A->set, "Planet") && !self->debuff) {
        if (ctx->joker_main) {
            if (strset_has(&G.GAME.used_vouchers, "v_observatory") && A->consumeable && str_eq(A->consumeable->hand_type, ctx->scoring_name)) {
                double ex = P_CENTER("v_observatory")->config.extra.num;
                return R_xmult_msg(ex);
            }
        }
    }
    if (!str_eq(A->set, "Joker") || self->debuff) return NULL;

    if (IS_NAME("Blueprint")) {
        Card *other = NULL;
        for (int i = 0; i < G.jokers->cards.count; i++)
            if (G.jokers->cards.items[i] == self) other = i + 1 < G.jokers->cards.count ? G.jokers->cards.items[i + 1] : NULL;
        if (other && other != self) {
            ctx->blueprint = ctx->blueprint ? ctx->blueprint + 1 : 1;
            if (!ctx->blueprint_card) ctx->blueprint_card = self;
            if (ctx->blueprint > G.jokers->cards.count + 1) return NULL;
            EvalResult *r = Card_calculate_joker(other, ctx);
            if (r) {
                r->card = ctx->blueprint_card ? ctx->blueprint_card : self;
                r->colour = G.C.BLUE;
                return r;
            }
        }
    }
    if (IS_NAME("Brainstorm")) {
        Card *other = G.jokers->cards.count > 0 ? G.jokers->cards.items[0] : NULL;
        if (other && other != self) {
            ctx->blueprint = ctx->blueprint ? ctx->blueprint + 1 : 1;
            if (!ctx->blueprint_card) ctx->blueprint_card = self;
            if (ctx->blueprint > G.jokers->cards.count + 1) return NULL;
            EvalResult *r = Card_calculate_joker(other, ctx);
            if (r) {
                r->card = ctx->blueprint_card ? ctx->blueprint_card : self;
                r->colour = G.C.RED;
                return r;
            }
        }
    }

    if (ctx->open_booster) {
        if (IS_NAME("Hallucination") && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
            if (pseudorandom(tfmt("halu%s", num_str(G.GAME.round_resets.ante))) < G.GAME.probabilities.normal / A->extra.num) {
                G.GAME.consumeable_buffer += 1;
                queue_consumable("Tarot", "hal", TRIG_BEFORE);
                card_eval_status_text(&self->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_plus_tarot"), .colour = G.C.PURPLE));
            }
        }
    } else if (ctx->buying_card) {
    } else if (ctx->selling_self) {
        if (IS_NAME("Luchador")) {
            if (G.GAME.blind && !G.GAME.blind->disabled && str_eq(Blind_get_type(G.GAME.blind), "Boss")) {
                card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("ph_boss_disabled")));
                Blind_disable(G.GAME.blind);
            }
        }
        if (IS_NAME("Diet Cola")) add_event(EVENT(.func = ev_add_double_tag));
        if (IS_NAME("Invisible Joker") && A->invis_rounds >= A->extra.num && !ctx->blueprint) {
            juice_card_until(self, eval_loyalty, NULL, true, NAN);
            CardList jokers = { 0 };
            for (int i = 0; i < G.jokers->cards.count; i++) if (G.jokers->cards.items[i] != self) vec_push(&jokers, G.jokers->cards.items[i]);
            if (jokers.count > 0) {
                if (G.jokers->cards.count <= G.jokers->config.card_limit) {
                    card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_duplicated_ex")));
                    for (int i = 1; i < jokers.count; i++) {
                        Card *k = jokers.items[i]; int j = i - 1;
                        while (j >= 0 && jokers.items[j]->sort_id > k->sort_id) { jokers.items[j + 1] = jokers.items[j]; j--; }
                        jokers.items[j + 1] = k;
                    }
                    Card *chosen = jokers.items[pseudorandom_index(jokers.count, pseudoseed("invisible"), true) - 1];
                    Card *card = copy_card(chosen, NULL, NAN, 0, chosen->edition && chosen->edition->negative);
                    if (card->ability.has_invis_rounds) card->ability.invis_rounds = 0;
                    Card_add_to_deck(card, false);
                    CardArea_emplace(G.jokers, card, NULL, false);
                } else card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_no_room_ex")));
            } else card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_no_other_jokers")));
            vec_free(&jokers);
        }
    } else if (ctx->selling_card) {
        if (IS_NAME("Campfire") && !ctx->blueprint) {
            A->x_mult += A->extra.num;
            queue_status(self, dup(localize_key("k_upgrade_ex")), NULL);
        }
        return NULL;
    } else if (ctx->reroll_shop) {
        if (IS_NAME("Flash Card") && !ctx->blueprint) {
            A->mult += A->extra.num;
            queue_status(self, var1("a_mult", A->mult), G.C.MULT);
        }
    } else if (ctx->ending_shop) {
        if (IS_NAME("Perkeo")) {
            if (G.consumeables->cards.count > 0) {
                add_event(EVENT(.func = ev_perkeo));
                card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_duplicated_ex")));
            }
            return NULL;
        }
        return NULL;
    } else if (ctx->skip_blind) {
        if (IS_NAME("Throwback") && !ctx->blueprint) queue_status(self, var1("a_xmult", A->x_mult), G.C.RED);
        return NULL;
    } else if (ctx->skipping_booster) {
        if (IS_NAME("Red Card") && !ctx->blueprint) {
            A->mult += A->extra.num;
            StatusCtx *s = CTX(StatusCtx);
            s->card = self; s->msg = var1("a_mult", A->extra.num); s->colour = G.C.RED; s->delay = 0.45; s->has_delay = true;
            add_event(EVENT(.func = ev_status_text, .ctx = s));
        }
        return NULL;
    } else if (ctx->playing_card_added && !self->getting_sliced) {
        if (IS_NAME("Hologram") && !ctx->blueprint && ctx->ncards_added > 0) {
            A->x_mult += ctx->ncards_added * A->extra.num;
            card_eval_status_text(&self->n, "extra", 0, 0, NULL, SEXTRA(.message = var1("a_xmult", A->x_mult)));
        }
    } else if (ctx->first_hand_drawn) {
        if (IS_NAME("Certificate")) {
            JCtx *jc = CTX(JCtx); jc->card = self; jc->other = ctx->blueprint_card;
            add_event(EVENT(.func = ev_certificate, .ctx = jc));
            playing_card_joker_effects(NULL, 1);
        }
        if (IS_NAME("DNA") && !ctx->blueprint) juice_card_until(self, eval_dna, NULL, true, NAN);
        if (IS_NAME("Trading Card") && !ctx->blueprint) juice_card_until(self, eval_trading, NULL, true, NAN);
    } else if (ctx->setting_blind && !self->getting_sliced) {
        bool boss = ctx->blind && ctx->blind->has_boss;
        if (IS_NAME("Chicot") && !ctx->blueprint && boss && !self->getting_sliced) {
            JCtx *jc = CTX(JCtx); jc->card = self;
            add_event(EVENT(.func = ev_chicot, .ctx = jc));
        }
        if (IS_NAME("Madness") && !ctx->blueprint && !boss) {
            A->x_mult += A->extra.num;
            CardList d = { 0 };
            for (int i = 0; i < G.jokers->cards.count; i++) {
                Card *v = G.jokers->cards.items[i];
                if (v != self && !v->ability.eternal && !v->getting_sliced) vec_push(&d, v);
            }
            Card *victim = NULL;
            if (d.count > 0) {
                for (int i = 1; i < d.count; i++) {
                    Card *k = d.items[i]; int j = i - 1;
                    while (j >= 0 && d.items[j]->sort_id > k->sort_id) { d.items[j + 1] = d.items[j]; j--; }
                    d.items[j + 1] = k;
                }
                victim = d.items[pseudorandom_index(d.count, pseudoseed("madness"), true) - 1];
            }
            vec_free(&d);
            if (victim && !bpc(ctx, self)->getting_sliced) {
                victim->getting_sliced = true;
                SliceCtx *sc = CTX(SliceCtx); sc->who = bpc(ctx, self); sc->victim = victim;
                add_event(EVENT(.func = ev_madness_slice, .ctx = sc));
            }
            if (!bpc(ctx, self)->getting_sliced)
                card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = var1("a_xmult", A->x_mult)));
        }
        if (IS_NAME("Burglar") && !bpc(ctx, self)->getting_sliced) {
            BurglarCtx *bc = CTX(BurglarCtx); bc->self = self; bc->bp = bpc(ctx, self); bc->extra = A->extra.num;
            add_event(EVENT(.func = ev_burglar, .ctx = bc));
        }
        if (IS_NAME("Riff-raff") && !bpc(ctx, self)->getting_sliced && G.jokers->cards.count + G.GAME.joker_buffer < G.jokers->config.card_limit) {
            int n = (int)MIN(2, G.jokers->config.card_limit - (G.jokers->cards.count + G.GAME.joker_buffer));
            G.GAME.joker_buffer += n;
            RiffCtx *rc = CTX(RiffCtx); rc->n = n;
            add_event(EVENT(.func = ev_riff_raff, .ctx = rc));
            card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_plus_joker"), .colour = G.C.BLUE));
        }
        if (IS_NAME("Cartomancer") && !bpc(ctx, self)->getting_sliced && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
            G.GAME.consumeable_buffer += 1;
            JCtx *jc = CTX(JCtx); jc->card = bpc(ctx, self);
            add_event(EVENT(.func = ev_cartomancer, .ctx = jc));
        }
        if (IS_NAME("Ceremonial Dagger") && !ctx->blueprint) {
            int my_pos = -1;
            for (int i = 0; i < G.jokers->cards.count; i++) if (G.jokers->cards.items[i] == self) { my_pos = i; break; }
            Card *next = (my_pos >= 0 && my_pos + 1 < G.jokers->cards.count) ? G.jokers->cards.items[my_pos + 1] : NULL;
            if (next && !self->getting_sliced && !next->ability.eternal && !next->getting_sliced) {
                next->getting_sliced = true;
                G.GAME.joker_buffer -= 1;
                SliceCtx *sc = CTX(SliceCtx); sc->who = self; sc->victim = next;
                add_event(EVENT(.func = ev_dagger, .ctx = sc));
                card_eval_status_text(&self->n, "extra", 0, 0, NULL, SEXTRA(.message = var1("a_mult", A->mult + 2 * next->sell_cost), .colour = G.C.RED, .no_juice = true));
            }
        }
        if (IS_NAME("Marble Joker") && !bpc(ctx, self)->getting_sliced) {
            add_event(EVENT(.func = ev_marble));
            card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_plus_stone"), .colour = G.C.SECONDARY_SET.Enhanced));
            add_event(EVENT(.func = ev_deck_limit_inc));
            draw_card(G.play, G.deck, 90, "up", false, NULL, NAN, false, false, NAN, false);
            playing_card_joker_effects(NULL, 1);
        }
        return NULL;
    } else if (ctx->destroying_card && !ctx->blueprint) {
        if (IS_NAME("Sixth Sense") && ctx->full_hand->count == 1 && Card_get_id(ctx->full_hand->items[0]) == 6 && G.GAME.current_round.hands_played == 0) {
            if (G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
                G.GAME.consumeable_buffer += 1;
                queue_consumable("Spectral", "sixth", TRIG_BEFORE);
                card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_plus_spectral"), .colour = G.C.SECONDARY_SET.Spectral));
            }
            EvalResult *r = NEWR();
            r->is_empty = false;
            r->remove = true;     /* `return true` */
            return r;
        }
        return NULL;
    } else if (ctx->cards_destroyed) {
        if (IS_NAME("Caino") && !ctx->blueprint) {
            int faces = 0;
            for (int i = 0; ctx->glass_shattered && i < ctx->glass_shattered->count; i++) if (Card_is_face(ctx->glass_shattered->items[i], false)) faces++;
            if (faces > 0) {
                CainoCtx *cc = CTX(CainoCtx); cc->self = self; cc->faces = faces;
                cc->msg = var1("a_xmult", A->caino_xmult + faces * A->extra.num);
                add_event(EVENT(.func = ev_caino, .ctx = cc));
            }
            return NULL;
        }
        if (IS_NAME("Glass Joker") && !ctx->blueprint) {
            int glasses = 0;
            for (int i = 0; ctx->glass_shattered && i < ctx->glass_shattered->count; i++) if (ctx->glass_shattered->items[i]->shattered) glasses++;
            if (glasses > 0) {
                CainoCtx *cc = CTX(CainoCtx); cc->self = self; cc->faces = glasses;
                cc->msg = var1("a_xmult", A->x_mult + A->extra.num * glasses);
                add_event(EVENT(.func = ev_glass, .ctx = cc));
            }
            return NULL;
        }
    } else if (ctx->remove_playing_cards) {
        if (IS_NAME("Caino") && !ctx->blueprint) {
            int faces = 0;
            for (int i = 0; ctx->removed && i < ctx->removed->count; i++) if (Card_is_face(ctx->removed->items[i], false)) faces++;
            if (faces > 0) {
                A->caino_xmult += faces * A->extra.num;
                queue_status(self, var1("a_xmult", A->caino_xmult), NULL);
            }
            return NULL;
        }
        if (IS_NAME("Glass Joker") && !ctx->blueprint) {
            int glass = 0;
            for (int i = 0; ctx->removed && i < ctx->removed->count; i++) if (ctx->removed->items[i]->shattered) glass++;
            if (glass > 0) {
                CainoCtx *cc = CTX(CainoCtx); cc->self = self; cc->faces = glass;
                cc->msg = var1("a_xmult", A->x_mult + A->extra.num * glass);
                add_event(EVENT(.func = ev_glass, .ctx = cc));
            }
            return NULL;
        }
    } else if (ctx->using_consumeable) {
        if (IS_NAME("Glass Joker") && !ctx->blueprint && ctx->consumeable && card_name_is(ctx->consumeable, "The Hanged Man")) {
            int sg = 0;
            for (int i = 0; i < G.hand->highlighted.count; i++) if (card_name_is(G.hand->highlighted.items[i], "Glass Card")) sg++;
            if (sg > 0) {
                A->x_mult += A->extra.num * sg;
                queue_status(self, var1("a_xmult", A->x_mult), NULL);
            }
            return NULL;
        }
        if (IS_NAME("Fortune Teller") && !ctx->blueprint && ctx->consumeable && str_eq(ctx->consumeable->ability.set, "Tarot"))
            queue_status(self, var1("a_mult", G.GAME.consumeable_usage_total.tarot), NULL);
        if (IS_NAME("Constellation") && !ctx->blueprint && ctx->consumeable && str_eq(ctx->consumeable->ability.set, "Planet")) {
            A->x_mult += A->extra.num;
            queue_status(self, var1("a_xmult", A->x_mult), NULL);
            return NULL;
        }
        return NULL;
    } else if (ctx->debuffed_hand) {
        if (IS_NAME("Matador")) {
            if (G.GAME.blind->triggered > 0) {
                ease_dollars(A->extra.num, false);
                dollar_buffer_add(A->extra.num);
                EvalResult *r = rmsg(money_msg(A->extra.num));
                r->dollars = A->extra.num; r->has_dollars = true;
                r->colour = G.C.MONEY;
                return r;
            }
        }
    } else if (ctx->pre_discard) {
        if (IS_NAME("Burnt Joker") && G.GAME.current_round.discards_used <= 0 && !ctx->hook) {
            const char *disp = NULL;
            const char *text = G_FUNCS_get_poker_hand_info(&G.hand->highlighted, &disp, NULL, NULL, NULL);
            card_eval_status_text(&bpc(ctx, self)->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_upgrade_ex")));
            HandStat *h = &G.GAME.hands[hand_index(text)];
            update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 0.8, .has_pitch = true, .delay = 0.3, .has_delay = true),
                             HTV(.handname = localize_cat(text, "poker_hands"), .has_handname = true, .chips = h->chips, .has_chips = true,
                                 .mult = h->mult, .has_mult = true, .level_n = h->level, .level_is_num = true, .has_level = true));
            level_up_hand(&bpc(ctx, self)->n, text, false, 1);
            update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 1.1, .has_pitch = true, .delay = 0, .has_delay = true),
                             HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .handname = "", .has_handname = true, .level = "", .has_level = true));
        }
    } else if (ctx->discard) {
        if (IS_NAME("Ramen") && !ctx->blueprint) {
            if (A->x_mult - A->extra.num <= 1) {
                food_remove(self);
                EvalResult *r = rmsg(dup(localize_key("k_eaten_ex")));
                r->colour = G.C.FILTER;
                return r;
            }
            A->x_mult -= A->extra.num;
            EvalResult *r = rmsg(var1("a_xmult_minus", A->extra.num));
            r->delay = 0.2; r->has_delay = true;
            r->colour = G.C.RED;
            return r;
        }
        if (IS_NAME("Yorick") && !ctx->blueprint) {
            if (A->yorick_discards <= 1) {
                A->yorick_discards = A->extra.discards;
                A->x_mult += A->extra.xmult;
                EvalResult *r = rmsg(var1("a_xmult", A->x_mult));
                r->delay = 0.2; r->has_delay = true;
                r->colour = G.C.RED;
                return r;
            }
            A->yorick_discards -= 1;
            return NULL;
        }
        if (IS_NAME("Trading Card") && !ctx->blueprint && G.GAME.current_round.discards_used <= 0 && ctx->full_hand->count == 1) {
            ease_dollars(A->extra.num, false);
            EvalResult *r = rmsg(money_msg(A->extra.num));
            r->colour = G.C.MONEY;
            r->delay = 0.45; r->has_delay = true;
            r->remove = true;
            r->card = self;
            return r;
        }
        Card *oc = ctx->other_card;
        if (IS_NAME("Castle") && oc && !oc->debuff && Card_is_suit(oc, G.GAME.current_round.castle_card.suit, false, false) && !ctx->blueprint) {
            A->extra.chips += A->extra.chip_mod;
            EvalResult *r = rmsg(dup(localize_key("k_upgrade_ex")));
            r->card = self;
            r->colour = G.C.CHIPS;
            return r;
        }
        if (IS_NAME("Mail-In Rebate") && oc && !oc->debuff && Card_get_id(oc) == G.GAME.current_round.mail_card.id) {
            ease_dollars(A->extra.num, false);
            EvalResult *r = rmsg(money_msg(A->extra.num));
            r->colour = G.C.MONEY;
            r->card = self;
            return r;
        }
        if (IS_NAME("Hit the Road") && oc && !oc->debuff && Card_get_id(oc) == 11 && !ctx->blueprint) {
            A->x_mult += A->extra.num;
            EvalResult *r = rmsg(var1("a_xmult", A->x_mult));
            r->colour = G.C.RED;
            r->delay = 0.45; r->has_delay = true;
            r->card = self;
            return r;
        }
        Card *last = ctx->full_hand && ctx->full_hand->count > 0 ? ctx->full_hand->items[ctx->full_hand->count - 1] : NULL;
        if (IS_NAME("Green Joker") && !ctx->blueprint && oc == last) {
            double prev = A->mult;
            A->mult = MAX(0, A->mult - A->extra.discard_sub);
            if (A->mult != prev) {
                EvalResult *r = rmsg(var1("a_mult_minus", A->extra.discard_sub));
                r->colour = G.C.RED;
                r->card = self;
                return r;
            }
        }
        if (IS_NAME("Faceless Joker") && oc == last) {
            int faces = 0;
            for (int i = 0; i < ctx->full_hand->count; i++) if (Card_is_face(ctx->full_hand->items[i], false)) faces++;
            if (faces >= A->extra.faces) {
                FacelessCtx *fc = CTX(FacelessCtx); fc->self = self; fc->bp = bpc(ctx, self); fc->dollars = A->extra.dollars;
                add_event(EVENT(.func = ev_faceless, .ctx = fc));
                return NULL;
            }
        }
        return NULL;
    } else if (ctx->end_of_round) {
        if (ctx->individual) {
        } else if (ctx->repetition) {
            if (ctx->cardarea == G.hand) {
                if (IS_NAME("Mime") && ((ctx->n_card_effects > 0 && ctx->card_effects[0] && !ctx->card_effects[0]->is_empty) || ctx->n_card_effects > 1)) {
                    EvalResult *r = rmsg(dup(localize_key("k_again_ex")));
                    r->repetitions = A->extra.num; r->has_repetitions = true;
                    r->card = self;
                    return r;
                }
            }
        } else if (!ctx->blueprint) {
            if (IS_NAME("Campfire") && G.GAME.blind->boss && A->x_mult > 1) {
                A->x_mult = 1;
                EvalResult *r = rmsg(dup(localize_key("k_reset")));
                r->colour = G.C.RED;
                return r;
            }
            if (IS_NAME("Rocket") && G.GAME.blind->boss) {
                A->extra.dollars += A->extra.increase;
                EvalResult *r = rmsg(dup(localize_key("k_upgrade_ex")));
                r->colour = G.C.MONEY;
                return r;
            }
            if (IS_NAME("Turtle Bean") && !ctx->blueprint) {
                if (A->extra.h_size - A->extra.h_mod <= 0) {
                    food_remove(self);
                    EvalResult *r = rmsg(dup(localize_key("k_eaten_ex")));
                    r->colour = G.C.FILTER;
                    return r;
                }
                A->extra.h_size -= A->extra.h_mod;
                CardArea_change_size(G.hand, -A->extra.h_mod);
                EvalResult *r = rmsg(var1("a_handsize_minus", A->extra.h_mod));
                r->colour = G.C.FILTER;
                return r;
            }
            if (IS_NAME("Invisible Joker") && !ctx->blueprint) {
                A->invis_rounds += 1;
                if (A->invis_rounds == A->extra.num) juice_card_until(self, eval_not_removed, NULL, true, NAN);
                char *msg = A->invis_rounds < A->extra.num ? xstrdup(tfmt("%s/%s", num_str(A->invis_rounds), num_str(A->extra.num))) : dup(localize_key("k_active_ex"));
                EvalResult *r = rmsg(msg);
                r->colour = G.C.FILTER;
                return r;
            }
            if (IS_NAME("Popcorn") && !ctx->blueprint) {
                if (A->mult - A->extra.num <= 0) {
                    food_remove(self);
                    EvalResult *r = rmsg(dup(localize_key("k_eaten_ex")));
                    r->colour = G.C.RED;
                    return r;
                }
                A->mult -= A->extra.num;
                EvalResult *r = rmsg(var1("a_mult_minus", A->extra.num));
                r->colour = G.C.MULT;
                return r;
            }
            if (IS_NAME("To Do List") && !ctx->blueprint) {
                const char *hands[HAND_COUNT]; int n = 0;
                for (int i = 0; i < HAND_COUNT; i++) {
                    int hi = hand_index(DATA_HANDS_PAIRS_ORDER[i]);
                    if (G.GAME.hands[hi].visible && !str_eq(HAND_NAMES[hi], A->to_do_poker_hand)) hands[n++] = HAND_NAMES[hi];
                }
                A->to_do_poker_hand = hands[pseudorandom_index(n, pseudoseed("to_do"), true) - 1];
                return rmsg(dup(localize_key("k_reset")));
            }
            if (IS_NAME("Egg")) {
                A->extra_value += A->extra.num;
                Card_set_cost(self);
                EvalResult *r = rmsg(dup(localize_key("k_val_up")));
                r->colour = G.C.MONEY;
                return r;
            }
            if (IS_NAME("Gift Card")) {
                for (int i = 0; i < G.jokers->cards.count; i++) {
                    Card *v = G.jokers->cards.items[i];
                    v->ability.extra_value += A->extra.num;
                    Card_set_cost(v);
                }
                for (int i = 0; i < G.consumeables->cards.count; i++) {
                    Card *v = G.consumeables->cards.items[i];
                    v->ability.extra_value += A->extra.num;
                    Card_set_cost(v);
                }
                EvalResult *r = rmsg(dup(localize_key("k_val_up")));
                r->colour = G.C.MONEY;
                return r;
            }
            if (IS_NAME("Hit the Road") && A->x_mult > 1) {
                A->x_mult = 1;
                EvalResult *r = rmsg(dup(localize_key("k_reset")));
                r->colour = G.C.RED;
                return r;
            }
            if (IS_NAME("Gros Michel") || IS_NAME("Cavendish")) {
                if (pseudorandom(IS_NAME("Cavendish") ? "cavendish" : "gros_michel") < G.GAME.probabilities.normal / A->extra.odds) {
                    food_remove(self);
                    if (IS_NAME("Gros Michel")) strset_add(&G.GAME.pool_flags, "gros_michel_extinct");
                    return rmsg(dup(localize_key("k_extinct_ex")));
                }
                return rmsg(dup(localize_key("k_safe_ex")));
            }
            if (IS_NAME("Mr. Bones") && ctx->game_over && G.GAME.chips / G.GAME.blind->chips >= 0.25) {
                JCtx *jc = CTX(JCtx); jc->card = self;
                add_event(EVENT(.func = ev_mr_bones, .ctx = jc));
                EvalResult *r = rmsg(dup(localize_key("k_saved_ex")));
                r->saved = true;
                r->colour = G.C.RED;
                return r;
            }
        }
    } else if (ctx->individual) {
        Card *oc = ctx->other_card;
        if (ctx->cardarea == G.play) {
            if (IS_NAME("Hiker")) {
                oc->ability.perma_bonus += A->extra.num;
                EvalResult *r = NEWR();
                r->extra = NEW(EvalExtra);
                r->extra->message = dup(localize_key("k_upgrade_ex"));
                r->extra->colour = G.C.CHIPS;
                r->colour = G.C.CHIPS;
                r->card = self;
                return r;
            }
            if (IS_NAME("Lucky Cat") && oc->lucky_trigger && !ctx->blueprint) {
                A->x_mult += A->extra.num;
                EvalResult *r = NEWR();
                r->extra = NEW(EvalExtra);
                r->extra->focus = &self->n;
                r->extra->message = dup(localize_key("k_upgrade_ex"));
                r->extra->colour = G.C.MULT;
                r->card = self;
                return r;
            }
            if (IS_NAME("Wee Joker") && Card_get_id(oc) == 2 && !ctx->blueprint) {
                A->extra.chips += A->extra.chip_mod;
                EvalResult *r = NEWR();
                r->extra = NEW(EvalExtra);
                r->extra->focus = &self->n;
                r->extra->message = dup(localize_key("k_upgrade_ex"));
                r->card = self;
                r->colour = G.C.CHIPS;
                return r;
            }
            if (IS_NAME("Photograph")) {
                Card *first_face = NULL;
                for (int i = 0; i < ctx->scoring_hand->count; i++) if (Card_is_face(ctx->scoring_hand->items[i], false)) { first_face = ctx->scoring_hand->items[i]; break; }
                if (oc == first_face) {
                    EvalResult *r = NEWR();
                    r->x_mult = A->extra.num; r->has_x_mult = true;
                    r->colour = G.C.RED;
                    r->card = self;
                    return r;
                }
            }
            if (IS_NAME("8 Ball") && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
                if (Card_get_id(oc) == 8 && pseudorandom("8ball") < G.GAME.probabilities.normal / A->extra.num) {
                    G.GAME.consumeable_buffer += 1;
                    EvalResult *r = NEWR();
                    r->extra = NEW(EvalExtra);
                    r->extra->focus = &self->n;
                    r->extra->message = dup(localize_key("k_plus_tarot"));
                    r->extra->func = eight_ball_func;
                    r->colour = G.C.SECONDARY_SET.Tarot;
                    r->card = self;
                    return r;
                }
            }
            if (IS_NAME("The Idol") && Card_get_id(oc) == G.GAME.current_round.idol_card.id && Card_is_suit(oc, G.GAME.current_round.idol_card.suit, false, false)) {
                EvalResult *r = NEWR();
                r->x_mult = A->extra.num; r->has_x_mult = true;
                r->colour = G.C.RED;
                r->card = self;
                return r;
            }
            if (IS_NAME("Scary Face") && Card_is_face(oc, false)) {
                EvalResult *r = NEWR(); r->chips = A->extra.num; r->has_chips = true; r->card = self; return r;
            }
            if (IS_NAME("Smiley Face") && Card_is_face(oc, false)) {
                EvalResult *r = NEWR(); r->mult = A->extra.num; r->has_mult = true; r->card = self; return r;
            }
            if (IS_NAME("Golden Ticket") && card_name_is(oc, "Gold Card")) {
                dollar_buffer_add(A->extra.num);
                EvalResult *r = NEWR(); r->dollars = A->extra.num; r->has_dollars = true; r->card = self; return r;
            }
            if (IS_NAME("Scholar") && Card_get_id(oc) == 14) {
                EvalResult *r = NEWR();
                r->chips = A->extra.chips; r->has_chips = true;
                r->mult = A->extra.mult; r->has_mult = true;
                r->card = self;
                return r;
            }
            if (IS_NAME("Walkie Talkie") && (Card_get_id(oc) == 10 || Card_get_id(oc) == 4)) {
                EvalResult *r = NEWR();
                r->chips = A->extra.chips; r->has_chips = true;
                r->mult = A->extra.mult; r->has_mult = true;
                r->card = self;
                return r;
            }
            if (IS_NAME("Business Card") && Card_is_face(oc, false) && pseudorandom("business") < G.GAME.probabilities.normal / A->extra.num) {
                dollar_buffer_add(2);
                EvalResult *r = NEWR(); r->dollars = 2; r->has_dollars = true; r->card = self; return r;
            }
            if (IS_NAME("Fibonacci")) {
                int id = Card_get_id(oc);
                if (id == 2 || id == 3 || id == 5 || id == 8 || id == 14) {
                    EvalResult *r = NEWR(); r->mult = A->extra.num; r->has_mult = true; r->card = self; return r;
                }
            }
            if (IS_NAME("Even Steven")) {
                int id = Card_get_id(oc);
                if (id <= 10 && id >= 0 && id % 2 == 0) {
                    EvalResult *r = NEWR(); r->mult = A->extra.num; r->has_mult = true; r->card = self; return r;
                }
            }
            if (IS_NAME("Odd Todd")) {
                int id = Card_get_id(oc);
                if ((id <= 10 && id >= 0 && id % 2 == 1) || id == 14) {
                    EvalResult *r = NEWR(); r->chips = A->extra.num; r->has_chips = true; r->card = self; return r;
                }
            }
            if (str_eq(A->effect, "Suit Mult") && Card_is_suit(oc, A->extra.suit, false, false)) {
                EvalResult *r = NEWR(); r->mult = A->extra.s_mult; r->has_mult = true; r->card = self; return r;
            }
            if (IS_NAME("Rough Gem") && Card_is_suit(oc, "Diamonds", false, false)) {
                dollar_buffer_add(A->extra.num);
                EvalResult *r = NEWR(); r->dollars = A->extra.num; r->has_dollars = true; r->card = self; return r;
            }
            if (IS_NAME("Onyx Agate") && Card_is_suit(oc, "Clubs", false, false)) {
                EvalResult *r = NEWR(); r->mult = A->extra.num; r->has_mult = true; r->card = self; return r;
            }
            if (IS_NAME("Arrowhead") && Card_is_suit(oc, "Spades", false, false)) {
                EvalResult *r = NEWR(); r->chips = A->extra.num; r->has_chips = true; r->card = self; return r;
            }
            if (IS_NAME("Bloodstone") && Card_is_suit(oc, "Hearts", false, false) && pseudorandom("bloodstone") < G.GAME.probabilities.normal / A->extra.odds) {
                EvalResult *r = NEWR(); r->x_mult = A->extra.Xmult; r->has_x_mult = true; r->card = self; return r;
            }
            if (IS_NAME("Ancient Joker") && Card_is_suit(oc, G.GAME.current_round.ancient_card.suit, false, false)) {
                EvalResult *r = NEWR(); r->x_mult = A->extra.num; r->has_x_mult = true; r->card = self; return r;
            }
            if (IS_NAME("Triboulet") && (Card_get_id(oc) == 12 || Card_get_id(oc) == 13)) {
                EvalResult *r = NEWR(); r->x_mult = A->extra.num; r->has_x_mult = true; r->colour = G.C.RED; r->card = self; return r;
            }
        }
        if (ctx->cardarea == G.hand) {
            if (IS_NAME("Shoot the Moon") && Card_get_id(oc) == 12) {
                if (oc->debuff) { EvalResult *r = rmsg(dup(localize_key("k_debuffed"))); r->colour = G.C.RED; r->card = self; return r; }
                EvalResult *r = NEWR(); r->h_mult = 13; r->has_h_mult = true; r->card = self; return r;
            }
            if (IS_NAME("Baron") && Card_get_id(oc) == 13) {
                if (oc->debuff) { EvalResult *r = rmsg(dup(localize_key("k_debuffed"))); r->colour = G.C.RED; r->card = self; return r; }
                EvalResult *r = NEWR(); r->x_mult = A->extra.num; r->has_x_mult = true; r->card = self; return r;
            }
            if (IS_NAME("Reserved Parking") && Card_is_face(oc, false) && pseudorandom("parking") < G.GAME.probabilities.normal / A->extra.odds) {
                if (oc->debuff) { EvalResult *r = rmsg(dup(localize_key("k_debuffed"))); r->colour = G.C.RED; r->card = self; return r; }
                dollar_buffer_add(A->extra.dollars);
                EvalResult *r = NEWR(); r->dollars = A->extra.dollars; r->has_dollars = true; r->card = self; return r;
            }
            if (IS_NAME("Raised Fist")) {
                double temp_mult = 15; int temp_id = 15;
                Card *raised = NULL;
                for (int i = 0; i < G.hand->cards.count; i++) {
                    Card *v = G.hand->cards.items[i];
                    if (temp_id >= v->base.id && !str_eq(v->ability.effect, "Stone Card")) { temp_mult = v->base.nominal; temp_id = v->base.id; raised = v; }
                }
                if (raised == oc) {
                    if (oc->debuff) { EvalResult *r = rmsg(dup(localize_key("k_debuffed"))); r->colour = G.C.RED; r->card = self; return r; }
                    EvalResult *r = NEWR(); r->h_mult = 2 * temp_mult; r->has_h_mult = true; r->card = self; return r;
                }
            }
        }
    } else if (ctx->repetition) {
        Card *oc = ctx->other_card;
        if (ctx->cardarea == G.play) {
            if (IS_NAME("Sock and Buskin") && Card_is_face(oc, false)) {
                EvalResult *r = rmsg(dup(localize_key("k_again_ex"))); r->repetitions = A->extra.num; r->has_repetitions = true; r->card = self; return r;
            }
            if (IS_NAME("Hanging Chad") && ctx->scoring_hand->count > 0 && oc == ctx->scoring_hand->items[0]) {
                EvalResult *r = rmsg(dup(localize_key("k_again_ex"))); r->repetitions = A->extra.num; r->has_repetitions = true; r->card = self; return r;
            }
            if (IS_NAME("Dusk") && G.GAME.current_round.hands_left == 0) {
                EvalResult *r = rmsg(dup(localize_key("k_again_ex"))); r->repetitions = A->extra.num; r->has_repetitions = true; r->card = self; return r;
            }
            if (IS_NAME("Seltzer")) {
                EvalResult *r = rmsg(dup(localize_key("k_again_ex"))); r->repetitions = 1; r->has_repetitions = true; r->card = self; return r;
            }
            if (IS_NAME("Hack")) {
                int id = Card_get_id(oc);
                if (id == 2 || id == 3 || id == 4 || id == 5) {
                    EvalResult *r = rmsg(dup(localize_key("k_again_ex"))); r->repetitions = A->extra.num; r->has_repetitions = true; r->card = self; return r;
                }
            }
        }
        if (ctx->cardarea == G.hand) {
            if (IS_NAME("Mime") && ((ctx->n_card_effects > 0 && ctx->card_effects[0] && !ctx->card_effects[0]->is_empty) || ctx->n_card_effects > 1)) {
                EvalResult *r = rmsg(dup(localize_key("k_again_ex"))); r->repetitions = A->extra.num; r->has_repetitions = true; r->card = self; return r;
            }
        }
    } else if (ctx->other_joker) {
        if (IS_NAME("Baseball Card") && ctx->other_joker->config.center->rarity == 2 && self != ctx->other_joker) {
            JuiceCtx *jc = CTX(JuiceCtx); jc->card = ctx->other_joker;
            add_event(EVENT(.func = ev_baseball_juice, .ctx = jc));
            return R_xmult_msg(A->extra.num);
        }
    } else {
        if (ctx->cardarea == G.jokers) {
            if (ctx->before) {
                if (IS_NAME("Spare Trousers") && (ph(ctx, "Two Pair") || ph(ctx, "Full House")) && !ctx->blueprint) {
                    A->mult += A->extra.num;
                    EvalResult *r = rmsg(dup(localize_key("k_upgrade_ex"))); r->colour = G.C.RED; r->card = self; return r;
                }
                if (IS_NAME("Space Joker") && pseudorandom("space") < G.GAME.probabilities.normal / A->extra.num) {
                    EvalResult *r = rmsg(dup(localize_key("k_level_up_ex"))); r->card = self; r->level_up = true; return r;
                }
                if (IS_NAME("Square Joker") && ctx->full_hand->count == 4 && !ctx->blueprint) {
                    A->extra.chips += A->extra.chip_mod;
                    EvalResult *r = rmsg(dup(localize_key("k_upgrade_ex"))); r->colour = G.C.CHIPS; r->card = self; return r;
                }
                if (IS_NAME("Runner") && ph(ctx, "Straight") && !ctx->blueprint) {
                    A->extra.chips += A->extra.chip_mod;
                    EvalResult *r = rmsg(dup(localize_key("k_upgrade_ex"))); r->colour = G.C.CHIPS; r->card = self; return r;
                }
                if (IS_NAME("Midas Mask") && !ctx->blueprint) {
                    int faces = 0;
                    for (int i = 0; i < ctx->scoring_hand->count; i++) {
                        Card *v = ctx->scoring_hand->items[i];
                        if (Card_is_face(v, false)) {
                            faces++;
                            Card_set_ability(v, P_CENTER("m_gold"), false, true);
                            JuiceCtx *jc = CTX(JuiceCtx); jc->card = v;
                            add_event(EVENT(.func = ev_juice_v, .ctx = jc));
                        }
                    }
                    if (faces > 0) { EvalResult *r = rmsg(dup(localize_key("k_gold"))); r->colour = G.C.MONEY; r->card = self; return r; }
                }
                if (IS_NAME("Vampire") && !ctx->blueprint) {
                    int enhanced = 0;
                    Center *base = P_CENTER("c_base");
                    for (int i = 0; i < ctx->scoring_hand->count; i++) {
                        Card *v = ctx->scoring_hand->items[i];
                        if (v->config.center != base && !v->debuff && !v->vampired) {
                            enhanced++;
                            v->vampired = true;
                            Card_set_ability(v, base, false, true);
                            JuiceCtx *jc = CTX(JuiceCtx); jc->card = v;
                            add_event(EVENT(.func = ev_vampire_v, .ctx = jc));
                        }
                    }
                    if (enhanced > 0) {
                        A->x_mult += A->extra.num * enhanced;
                        EvalResult *r = rmsg(var1("a_xmult", A->x_mult)); r->colour = G.C.MULT; r->card = self; return r;
                    }
                }
                if (IS_NAME("To Do List") && str_eq(ctx->scoring_name, A->to_do_poker_hand)) {
                    ease_dollars(A->extra.dollars, false);
                    dollar_buffer_add(A->extra.dollars);
                    EvalResult *r = rmsg(money_msg(A->extra.dollars)); r->dollars = A->extra.dollars; r->has_dollars = true; r->colour = G.C.MONEY; return r;
                }
                if (IS_NAME("DNA") && G.GAME.current_round.hands_played == 0) {
                    if (ctx->full_hand->count == 1) {
                        G.playing_card = G.playing_card ? G.playing_card + 1 : 1;
                        Card *_card = copy_card(ctx->full_hand->items[0], NULL, NAN, G.playing_card, false);
                        Card_add_to_deck(_card, false);
                        G.deck->config.card_limit += 1;
                        vec_push(&G.playing_cards, _card);
                        CardArea_emplace(G.hand, _card, NULL, false);
                        _card->n.states.visible = false;
                        JuiceCtx *jc = CTX(JuiceCtx); jc->card = _card;
                        add_event(EVENT(.func = ev_dna_materialize, .ctx = jc));
                        EvalResult *r = rmsg(dup(localize_key("k_copied_ex"))); r->colour = G.C.CHIPS; r->card = self; r->playing_cards_created = NEW(CardList); vec_push(r->playing_cards_created, _card);
                        return r;
                    }
                }
                if (IS_NAME("Ride the Bus") && !ctx->blueprint) {
                    bool faces = false;
                    for (int i = 0; i < ctx->scoring_hand->count; i++) if (Card_is_face(ctx->scoring_hand->items[i], false)) faces = true;
                    if (faces) {
                        double last = A->mult;
                        A->mult = 0;
                        if (last > 0) { EvalResult *r = rmsg(dup(localize_key("k_reset"))); r->card = self; return r; }
                    } else A->mult += A->extra.num;
                }
                if (IS_NAME("Obelisk") && !ctx->blueprint) {
                    bool reset = true;
                    int si = hand_index(ctx->scoring_name);
                    double play_more_than = si >= 0 ? G.GAME.hands[si].played : 0;
                    for (int i = 0; i < HAND_COUNT; i++) {
                        if (i != si && G.GAME.hands[i].played >= play_more_than && G.GAME.hands[i].visible) reset = false;
                    }
                    if (reset) {
                        if (A->x_mult > 1) {
                            A->x_mult = 1;
                            EvalResult *r = rmsg(dup(localize_key("k_reset"))); r->card = self; return r;
                        }
                    } else A->x_mult += A->extra.num;
                }
                if (IS_NAME("Green Joker") && !ctx->blueprint) {
                    A->mult += A->extra.hand_add;
                    EvalResult *r = rmsg(var1("a_mult", A->extra.hand_add)); r->card = self; return r;
                }
            } else if (ctx->after) {
                if (IS_NAME("Ice Cream") && !ctx->blueprint) {
                    if (A->extra.chips - A->extra.chip_mod <= 0) {
                        food_remove(self);
                        EvalResult *r = rmsg(dup(localize_key("k_melted_ex"))); r->colour = G.C.CHIPS; return r;
                    }
                    A->extra.chips -= A->extra.chip_mod;
                    EvalResult *r = rmsg(var1("a_chips_minus", A->extra.chip_mod)); r->colour = G.C.CHIPS; return r;
                }
                if (IS_NAME("Seltzer") && !ctx->blueprint) {
                    if (A->extra.num - 1 <= 0) {
                        food_remove(self);
                        EvalResult *r = rmsg(dup(localize_key("k_drank_ex"))); r->colour = G.C.FILTER; return r;
                    }
                    A->extra.num -= 1;
                    EvalResult *r = rmsg(xstrdup(num_str(A->extra.num))); r->colour = G.C.FILTER; return r;
                }
            } else {
                if (IS_NAME("Loyalty Card")) {
                    A->loyalty_remaining = lua_mod(A->extra.every - 1 - (G.GAME.hands_played - A->hands_played_at_create), A->extra.every + 1);
                    if (ctx->blueprint) {
                        if (A->loyalty_remaining == A->extra.every) return R_xmult_msg(A->extra.Xmult);
                    } else {
                        if (A->loyalty_remaining == 0) juice_card_until(self, eval_loyalty2, NULL, true, NAN);
                        else if (A->loyalty_remaining == A->extra.every) return R_xmult_msg(A->extra.Xmult);
                    }
                }
                if (!IS_NAME("Seeing Double") && A->x_mult > 1 && (!A->type[0] || ph(ctx, A->type))) {
                    EvalResult *r = R_xmult_msg(A->x_mult); r->colour = G.C.RED; return r;
                }
                if (A->t_mult > 0 && ph(ctx, A->type)) return R_mult_msg(A->t_mult);
                if (A->t_chips > 0 && ph(ctx, A->type)) return R_chips_msg(A->t_chips);
                if (IS_NAME("Half Joker") && ctx->full_hand->count <= A->extra.size) return R_mult_msg(A->extra.mult);
                if (IS_NAME("Abstract Joker")) {
                    int x = 0;
                    for (int i = 0; i < G.jokers->cards.count; i++) if (str_eq(G.jokers->cards.items[i]->ability.set, "Joker")) x++;
                    return R_mult_msg(x * A->extra.num);
                }
                if (IS_NAME("Acrobat") && G.GAME.current_round.hands_left == 0) return R_xmult_msg(A->extra.num);
                if (IS_NAME("Mystic Summit") && G.GAME.current_round.discards_left == A->extra.d_remaining) return R_mult_msg(A->extra.mult);
                if (IS_NAME("Misprint")) {
                    double m = pseudorandom_range("misprint", (int)A->extra.min, (int)A->extra.max);
                    return R_mult_msg(m);
                }
                if (IS_NAME("Banner") && G.GAME.current_round.discards_left > 0) return R_chips_msg(G.GAME.current_round.discards_left * A->extra.num);
                if (IS_NAME("Stuntman")) return R_chips_msg(A->extra.chip_mod);
                if (IS_NAME("Matador")) {
                    if (G.GAME.blind->triggered > 0) {
                        ease_dollars(A->extra.num, false);
                        dollar_buffer_add(A->extra.num);
                        EvalResult *r = rmsg(money_msg(A->extra.num)); r->dollars = A->extra.num; r->has_dollars = true; r->colour = G.C.MONEY; return r;
                    }
                }
                if (IS_NAME("Supernova")) {
                    int si = hand_index(ctx->scoring_name);
                    return R_mult_msg(si >= 0 ? G.GAME.hands[si].played : 0);
                }
                if (IS_NAME("Ceremonial Dagger") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Vagabond") && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
                    if (G.GAME.dollars <= A->extra.num) {
                        G.GAME.consumeable_buffer += 1;
                        queue_consumable("Tarot", "vag", TRIG_BEFORE);
                        EvalResult *r = rmsg(dup(localize_key("k_plus_tarot"))); r->card = self; return r;
                    }
                }
                if (IS_NAME("Superposition") && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
                    int aces = 0;
                    for (int i = 0; i < ctx->scoring_hand->count; i++) if (Card_get_id(ctx->scoring_hand->items[i]) == 14) aces++;
                    if (aces >= 1 && ph(ctx, "Straight")) {
                        G.GAME.consumeable_buffer += 1;
                        queue_consumable("Tarot", "sup", TRIG_BEFORE);
                        EvalResult *r = rmsg(dup(localize_key("k_plus_tarot"))); r->colour = G.C.SECONDARY_SET.Tarot; r->card = self; return r;
                    }
                }
                if (IS_NAME("Seance") && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
                    if (ph(ctx, A->extra.poker_hand)) {
                        G.GAME.consumeable_buffer += 1;
                        queue_consumable("Spectral", "sea", TRIG_BEFORE);
                        EvalResult *r = rmsg(dup(localize_key("k_plus_spectral"))); r->colour = G.C.SECONDARY_SET.Spectral; r->card = self; return r;
                    }
                }
                if (IS_NAME("Flower Pot")) {
                    int H = 0, D = 0, S = 0, C = 0;
                    for (int i = 0; i < ctx->scoring_hand->count; i++) {
                        Card *v = ctx->scoring_hand->items[i];
                        if (!card_name_is(v, "Wild Card")) {
                            if (Card_is_suit(v, "Hearts", true, false) && H == 0) H++;
                            else if (Card_is_suit(v, "Diamonds", true, false) && D == 0) D++;
                            else if (Card_is_suit(v, "Spades", true, false) && S == 0) S++;
                            else if (Card_is_suit(v, "Clubs", true, false) && C == 0) C++;
                        }
                    }
                    for (int i = 0; i < ctx->scoring_hand->count; i++) {
                        Card *v = ctx->scoring_hand->items[i];
                        if (card_name_is(v, "Wild Card")) {
                            if (Card_is_suit(v, "Hearts", false, false) && H == 0) H++;
                            else if (Card_is_suit(v, "Diamonds", false, false) && D == 0) D++;
                            else if (Card_is_suit(v, "Spades", false, false) && S == 0) S++;
                            else if (Card_is_suit(v, "Clubs", false, false) && C == 0) C++;
                        }
                    }
                    if (H > 0 && D > 0 && S > 0 && C > 0) return R_xmult_msg(A->extra.num);
                }
                if (IS_NAME("Seeing Double")) {
                    int H = 0, D = 0, S = 0, C = 0;
                    for (int i = 0; i < ctx->scoring_hand->count; i++) {
                        Card *v = ctx->scoring_hand->items[i];
                        if (!card_name_is(v, "Wild Card")) {
                            if (Card_is_suit(v, "Hearts", false, false)) H++;
                            if (Card_is_suit(v, "Diamonds", false, false)) D++;
                            if (Card_is_suit(v, "Spades", false, false)) S++;
                            if (Card_is_suit(v, "Clubs", false, false)) C++;
                        }
                    }
                    for (int i = 0; i < ctx->scoring_hand->count; i++) {
                        Card *v = ctx->scoring_hand->items[i];
                        if (card_name_is(v, "Wild Card")) {
                            if (Card_is_suit(v, "Clubs", false, false) && C == 0) C++;
                            else if (Card_is_suit(v, "Diamonds", false, false) && D == 0) D++;
                            else if (Card_is_suit(v, "Spades", false, false) && S == 0) S++;
                            else if (Card_is_suit(v, "Hearts", false, false) && H == 0) H++;
                        }
                    }
                    if ((H > 0 || D > 0 || S > 0) && C > 0) return R_xmult_msg(A->extra.num);
                }
                if (IS_NAME("Wee Joker")) { EvalResult *r = R_chips_msg(A->extra.chips); r->colour = G.C.CHIPS; return r; }
                if (IS_NAME("Castle") && A->extra.chips > 0) { EvalResult *r = R_chips_msg(A->extra.chips); r->colour = G.C.CHIPS; return r; }
                if (IS_NAME("Blue Joker") && G.deck->cards.count > 0) { EvalResult *r = R_chips_msg(A->extra.num * G.deck->cards.count); r->colour = G.C.CHIPS; return r; }
                if (IS_NAME("Erosion") && (G.GAME.starting_deck_size - G.playing_cards.count) > 0) {
                    EvalResult *r = R_mult_msg(A->extra.num * (G.GAME.starting_deck_size - G.playing_cards.count)); r->colour = G.C.MULT; return r;
                }
                if (IS_NAME("Square Joker")) { EvalResult *r = R_chips_msg(A->extra.chips); r->colour = G.C.CHIPS; return r; }
                if (IS_NAME("Runner")) { EvalResult *r = R_chips_msg(A->extra.chips); r->colour = G.C.CHIPS; return r; }
                if (IS_NAME("Ice Cream")) { EvalResult *r = R_chips_msg(A->extra.chips); r->colour = G.C.CHIPS; return r; }
                if (IS_NAME("Stone Joker") && A->stone_tally > 0) { EvalResult *r = R_chips_msg(A->extra.num * A->stone_tally); r->colour = G.C.CHIPS; return r; }
                if (IS_NAME("Steel Joker") && A->steel_tally > 0) { EvalResult *r = R_xmult_msg(1 + A->extra.num * A->steel_tally); r->colour = G.C.MULT; return r; }
                if (IS_NAME("Bull") && (G.GAME.dollars + G.GAME.dollar_buffer) > 0) {
                    EvalResult *r = R_chips_msg(A->extra.num * MAX(0, G.GAME.dollars + G.GAME.dollar_buffer)); r->colour = G.C.CHIPS; return r;
                }
                if (IS_NAME("Driver's License")) {
                    if (A->driver_tally >= 16) return R_xmult_msg(A->extra.num);
                }
                if (IS_NAME("Blackboard")) {
                    int black = 0, all = 0;
                    for (int i = 0; i < G.hand->cards.count; i++) {
                        Card *v = G.hand->cards.items[i];
                        all++;
                        if (Card_is_suit(v, "Clubs", false, true) || Card_is_suit(v, "Spades", false, true)) black++;
                    }
                    if (black == all) return R_xmult_msg(A->extra.num);
                }
                if (IS_NAME("Joker Stencil")) {
                    if (G.jokers->config.card_limit - G.jokers->cards.count > 0) return R_xmult_msg(A->x_mult);
                }
                if (IS_NAME("Swashbuckler") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Joker")) return R_mult_msg(A->mult);
                if (IS_NAME("Spare Trousers") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Ride the Bus") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Flash Card") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Popcorn") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Green Joker") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Fortune Teller") && G.GAME.consumeable_usage_total.present && G.GAME.consumeable_usage_total.tarot > 0)
                    return R_mult_msg(G.GAME.consumeable_usage_total.tarot);
                if (IS_NAME("Gros Michel")) return R_mult_msg(A->extra.mult);
                if (IS_NAME("Cavendish")) return R_xmult_msg(A->extra.Xmult);
                if (IS_NAME("Red Card") && A->mult > 0) return R_mult_msg(A->mult);
                if (IS_NAME("Card Sharp")) {
                    int si = hand_index(ctx->scoring_name);
                    if (si >= 0 && G.GAME.hands[si].played_this_round > 1) return R_xmult_msg(A->extra.Xmult);
                }
                if (IS_NAME("Bootstraps") && floor((G.GAME.dollars + G.GAME.dollar_buffer) / A->extra.dollars) >= 1)
                    return R_mult_msg(A->extra.mult * floor((G.GAME.dollars + G.GAME.dollar_buffer) / A->extra.dollars));
                if (IS_NAME("Caino") && A->caino_xmult > 1) return R_xmult_msg(A->caino_xmult);
            }
        }
    }
#undef IS_NAME
    return NULL;
}
