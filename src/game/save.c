/* save.c - G.GAME / G.SETTINGS <-> DVal conversion and the save manager
 * (port of engine/save_manager.lua and the Game:save_* functions).
 * Saves are written synchronously on the main thread; the files keep the
 * original layout ("return {...}" deflated) and Lua key names. */
#include "game.h"

static const char *RANK_KEYS[13] = { "Ace", "2", "3", "4", "5", "6", "7", "8", "9", "10", "Jack", "Queen", "King" };

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */
static void put_s(DVal *t, const char *k, const char *s) { if (s) dv_set_str(t, k, s); }
static DVal *sub(DVal *t, const char *k) { return dv_get_or_create_table(t, k); }
static void get_n(const DVal *t, const char *k, double *dst) { *dst = dv_get_num(t, k, *dst); }
static void get_b(const DVal *t, const char *k, bool *dst) { *dst = dv_get_bool(t, k, *dst); }
static void get_cs(const DVal *t, const char *k, const char **dst) {
    DVal *v = dv_get(t, k);
    if (v && v->t == DV_STR) *dst = intern(v->s);
}
static void get_s(const DVal *t, const char *k, char **dst) {
    DVal *v = dv_get(t, k);
    if (v && v->t == DV_STR) str_set(dst, v->s);
    else if (v && v->t == DV_NUM) str_set(dst, number_format(v->n));
}

static DVal score_save(ScoreEntry *e) {
    DVal d = dv_table();
    put_s(&d, "label", e->label);
    dv_set_num(&d, "amt", e->amt);
    if (e->high_score) dv_set_bool(&d, "high_score", true);
    return d;
}
static void score_load(ScoreEntry *e, DVal *d) {
    if (!d) return;
    get_cs(d, "label", &e->label);
    get_n(d, "amt", &e->amt);
    get_b(d, "high_score", &e->high_score);
}

static DVal strset_save(StrSet *s) {
    DVal d = dv_table();
    for (int i = 0; i < s->cap; i++) if (NUMMAP_LIVE(s, i)) dv_set_bool(&d, s->keys[i], true);
    return d;
}
static void strset_load(StrSet *s, DVal *d) {
    if (!dv_is_table(d)) return;
    for (int i = 0; i < dv_count(d); i++) {
        const DKey *k = dv_key(d, i);
        if (k->is_str && dv_truthy(dv_val(d, i))) strset_add(s, k->s);
    }
}

/* ------------------------------------------------------------------ */
/* GameState                                                           */
/* ------------------------------------------------------------------ */
static DVal modifiers_save(Modifiers *m) {
    DVal d = dv_table();
    if (m->has_no_blind_reward) {
        DVal nb = dv_table();
        for (int i = 0; i < 3; i++) if (m->no_blind_reward[i]) dv_set_bool(&nb, BLIND_TYPE_NAMES[i], true);
        dv_set(&d, "no_blind_reward", nb);
    }
#define MB(f) if (m->f) dv_set_bool(&d, #f, true)
#define MN(f) if (m->f) dv_set_num(&d, #f, m->f)
    MN(scaling);
    MB(enable_eternals_in_shop); MB(enable_perishables_in_shop); MB(enable_rentals_in_shop);
    MB(all_eternal); MB(booster_ante_scaling); MB(chips_dollar_cap); MB(debuff_played_cards);
    MN(discard_cost); MN(flipped_cards); MN(minus_hand_size_per_X_dollar); MN(money_per_discard); MN(money_per_hand);
    MB(no_extra_hand_money); MB(no_interest); MB(inflation);
    if (m->has_set_eternal_ante) dv_set_num(&d, "set_eternal_ante", m->set_eternal_ante);
    if (m->has_set_joker_slots_ante) dv_set_num(&d, "set_joker_slots_ante", m->set_joker_slots_ante);
    MB(daily); MB(no_reward); MB(no_shop_jokers);
#undef MB
#undef MN
    for (int i = 0; i < m->other.cap; i++) if (NUMMAP_LIVE(&m->other, i)) dv_set_bool(&d, m->other.keys[i], true);
    return d;
}
static void modifiers_load(Modifiers *m, DVal *d) {
    if (!dv_is_table(d)) return;
    for (int i = 0; i < dv_count(d); i++) {
        const DKey *k = dv_key(d, i);
        if (!k->is_str) continue;
        DVal *v = dv_val(d, i);
        const char *key = k->s;
#define KEY_IS(f) (!strcmp(key, #f))
#define LB(f) else if (KEY_IS(f)) m->f = dv_truthy(v)
#define LN(f) else if (KEY_IS(f)) m->f = dv_num_or(v, 0)
        if (KEY_IS(no_blind_reward)) {
            m->has_no_blind_reward = true;
            for (int b = 0; b < 3; b++) m->no_blind_reward[b] = dv_get_bool(v, BLIND_TYPE_NAMES[b], false);
        }
        LN(scaling);
        LB(enable_eternals_in_shop); LB(enable_perishables_in_shop); LB(enable_rentals_in_shop);
        LB(all_eternal); LB(booster_ante_scaling); LB(chips_dollar_cap); LB(debuff_played_cards);
        LN(discard_cost); LN(flipped_cards); LN(minus_hand_size_per_X_dollar); LN(money_per_discard); LN(money_per_hand);
        LB(no_extra_hand_money); LB(no_interest); LB(inflation);
        else if (KEY_IS(set_eternal_ante)) { m->has_set_eternal_ante = true; m->set_eternal_ante = dv_num_or(v, 0); }
        else if (KEY_IS(set_joker_slots_ante)) { m->has_set_joker_slots_ante = true; m->set_joker_slots_ante = dv_num_or(v, 0); }
        LB(daily); LB(no_reward); LB(no_shop_jokers);
        else if (dv_truthy(v)) strset_add(&m->other, key);
#undef KEY_IS
#undef LB
#undef LN
    }
}

static DVal starting_params_save(StartingParams *p) {
    DVal d = dv_table();
    dv_set_num(&d, "dollars", p->dollars); dv_set_num(&d, "hand_size", p->hand_size);
    dv_set_num(&d, "discards", p->discards); dv_set_num(&d, "hands", p->hands);
    dv_set_num(&d, "reroll_cost", p->reroll_cost); dv_set_num(&d, "joker_slots", p->joker_slots);
    dv_set_num(&d, "ante_scaling", p->ante_scaling); dv_set_num(&d, "consumable_slots", p->consumable_slots);
    if (p->no_faces) dv_set_bool(&d, "no_faces", true);
    if (p->erratic_suits_and_ranks) dv_set_bool(&d, "erratic_suits_and_ranks", true);
    return d;
}
static void starting_params_load(StartingParams *p, DVal *d) {
    if (!dv_is_table(d)) return;
    get_n(d, "dollars", &p->dollars); get_n(d, "hand_size", &p->hand_size);
    get_n(d, "discards", &p->discards); get_n(d, "hands", &p->hands);
    get_n(d, "reroll_cost", &p->reroll_cost); get_n(d, "joker_slots", &p->joker_slots);
    get_n(d, "ante_scaling", &p->ante_scaling); get_n(d, "consumable_slots", &p->consumable_slots);
    get_b(d, "no_faces", &p->no_faces); get_b(d, "erratic_suits_and_ranks", &p->erratic_suits_and_ranks);
}

static DVal usage_save(StrMap *m, bool order_is_str) {
    DVal d = dv_table();
    for (int i = 0; i < m->cap; i++) {
        if (!STRMAP_LIVE(m, i)) continue;
        UsageEntry *u = m->vals[i];
        DVal e = dv_table();
        dv_set_num(&e, "count", u->count);
        if (order_is_str) put_s(&e, "order", u->order_str);
        else dv_set_num(&e, "order", u->order);
        put_s(&e, "set", u->set);
        dv_set(&d, m->keys[i], e);
    }
    return d;
}
static void usage_load(StrMap *m, DVal *d) {
    if (!dv_is_table(d)) return;
    for (int i = 0; i < dv_count(d); i++) {
        const DKey *k = dv_key(d, i);
        DVal *v = dv_val(d, i);
        if (!k->is_str || !dv_is_table(v)) continue;
        UsageEntry *u = NEW(UsageEntry);
        u->count = dv_get_num(v, "count", 0);
        DVal *o = dv_get(v, "order");
        if (o && o->t == DV_STR) u->order_str = xstrdup(o->s);
        else u->order = dv_num_or(o, 0);
        const char *set = dv_get_str(v, "set", NULL);
        u->set = set ? intern(set) : NULL;
        UsageEntry *old = strmap_get(m, k->s);
        if (old) { free(old->order_str); free(old); }
        strmap_set(m, k->s, u);
    }
}

static DVal current_round_save(CurrentRound *c) {
    DVal d = dv_table();
    DVal ch = dv_table();
    dv_set_num(&ch, "chips", c->current_hand.chips);
    put_s(&ch, "chip_text", c->current_hand.chip_text);
    dv_set_num(&ch, "mult", c->current_hand.mult);
    put_s(&ch, "mult_text", c->current_hand.mult_text);
    dv_set_num(&ch, "chip_total", c->current_hand.chip_total);
    put_s(&ch, "chip_total_text", c->current_hand.chip_total_text);
    put_s(&ch, "handname", c->current_hand.handname);
    put_s(&ch, "hand_level", c->current_hand.hand_level);
    dv_set(&d, "current_hand", ch);
    DVal up = dv_table();
    for (int i = 0; i < c->n_used_packs; i++) if (c->used_packs[i]) dv_seti(&up, i + 1, dv_str(c->used_packs[i]));
    dv_set(&d, "used_packs", up);
    dv_set_num(&d, "cards_flipped", c->cards_flipped);
    put_s(&d, "round_text", c->round_text);
    DVal idol = dv_table(); put_s(&idol, "suit", c->idol_card.suit); put_s(&idol, "rank", c->idol_card.rank);
    if (c->idol_card.id) dv_set_num(&idol, "id", c->idol_card.id);
    dv_set(&d, "idol_card", idol);
    DVal mail = dv_table(); put_s(&mail, "rank", c->mail_card.rank);
    if (c->mail_card.id) dv_set_num(&mail, "id", c->mail_card.id);
    dv_set(&d, "mail_card", mail);
    DVal anc = dv_table(); put_s(&anc, "suit", c->ancient_card.suit); dv_set(&d, "ancient_card", anc);
    DVal cas = dv_table(); put_s(&cas, "suit", c->castle_card.suit); dv_set(&d, "castle_card", cas);
    dv_set_num(&d, "hands_left", c->hands_left); dv_set_num(&d, "hands_played", c->hands_played);
    dv_set_num(&d, "discards_left", c->discards_left); dv_set_num(&d, "discards_used", c->discards_used);
    dv_set_num(&d, "dollars", c->dollars); dv_set_num(&d, "reroll_cost", c->reroll_cost);
    dv_set_num(&d, "reroll_cost_increase", c->reroll_cost_increase);
    dv_set_num(&d, "jokers_purchased", c->jokers_purchased); dv_set_num(&d, "free_rerolls", c->free_rerolls);
    dv_set_num(&d, "round_dollars", c->round_dollars);
    put_s(&d, "dollars_to_be_earned", c->dollars_to_be_earned);
    put_s(&d, "most_played_poker_hand", c->most_played_poker_hand);
    put_s(&d, "voucher", c->voucher);
    return d;
}
static void current_round_load(CurrentRound *c, DVal *d) {
    if (!dv_is_table(d)) return;
    DVal *ch = dv_get(d, "current_hand");
    if (ch) {
        DVal *v = dv_get(ch, "chips");
        if (v && v->t == DV_STR) { c->current_hand.chip_text_is_str = true; str_set(&c->current_hand.chip_text_override, v->s); }
        else get_n(ch, "chips", &c->current_hand.chips);
        v = dv_get(ch, "mult");
        if (v && v->t == DV_STR) { c->current_hand.mult_text_is_str = true; str_set(&c->current_hand.mult_text_override, v->s); }
        else get_n(ch, "mult", &c->current_hand.mult);
        get_s(ch, "chip_text", &c->current_hand.chip_text);
        get_s(ch, "mult_text", &c->current_hand.mult_text);
        get_n(ch, "chip_total", &c->current_hand.chip_total);
        get_s(ch, "chip_total_text", &c->current_hand.chip_total_text);
        get_s(ch, "handname", &c->current_hand.handname);
        get_s(ch, "hand_level", &c->current_hand.hand_level);
    }
    DVal *up = dv_get(d, "used_packs");
    if (up) {
        for (int i = 0; i < c->n_used_packs; i++) { free(c->used_packs[i]); c->used_packs[i] = NULL; }
        c->n_used_packs = 0;
        for (int i = 1; i <= 2; i++) {
            const char *s = dv_geti_str(up, i, NULL);
            if (s) { c->used_packs[i - 1] = xstrdup(s); c->n_used_packs = i; }
        }
    }
    get_n(d, "cards_flipped", &c->cards_flipped);
    get_s(d, "round_text", &c->round_text);
    DVal *t;
    if ((t = dv_get(d, "idol_card"))) {
        get_cs(t, "suit", &c->idol_card.suit); get_cs(t, "rank", &c->idol_card.rank);
        c->idol_card.id = (int)dv_get_num(t, "id", c->idol_card.id);
    }
    if ((t = dv_get(d, "mail_card"))) { get_cs(t, "rank", &c->mail_card.rank); c->mail_card.id = (int)dv_get_num(t, "id", c->mail_card.id); }
    if ((t = dv_get(d, "ancient_card"))) get_cs(t, "suit", &c->ancient_card.suit);
    if ((t = dv_get(d, "castle_card"))) get_cs(t, "suit", &c->castle_card.suit);
    get_n(d, "hands_left", &c->hands_left); get_n(d, "hands_played", &c->hands_played);
    get_n(d, "discards_left", &c->discards_left); get_n(d, "discards_used", &c->discards_used);
    get_n(d, "dollars", &c->dollars); get_n(d, "reroll_cost", &c->reroll_cost);
    get_n(d, "reroll_cost_increase", &c->reroll_cost_increase);
    get_n(d, "jokers_purchased", &c->jokers_purchased); get_n(d, "free_rerolls", &c->free_rerolls);
    get_n(d, "round_dollars", &c->round_dollars);
    get_s(d, "dollars_to_be_earned", &c->dollars_to_be_earned);
    get_cs(d, "most_played_poker_hand", &c->most_played_poker_hand);
    c->voucher = NULL;
    get_cs(d, "voucher", &c->voucher);
}

static DVal round_resets_save(RoundResets *r) {
    DVal d = dv_table();
    dv_set_num(&d, "hands", r->hands); dv_set_num(&d, "discards", r->discards); dv_set_num(&d, "reroll_cost", r->reroll_cost);
    if (r->has_temp_reroll_cost) dv_set_num(&d, "temp_reroll_cost", r->temp_reroll_cost);
    if (r->has_temp_handsize) dv_set_num(&d, "temp_handsize", r->temp_handsize);
    dv_set_num(&d, "ante", r->ante); dv_set_num(&d, "blind_ante", r->blind_ante);
    DVal bs = dv_table(), lbs = dv_table(), bc = dv_table();
    for (int i = 0; i < 3; i++) {
        put_s(&bs, BLIND_TYPE_NAMES[i], r->blind_states[i]);
        put_s(&lbs, BLIND_TYPE_NAMES[i], r->loc_blind_states[i] ? r->loc_blind_states[i] : "");
        put_s(&bc, BLIND_TYPE_NAMES[i], r->blind_choices[i]);
    }
    dv_set(&d, "blind_states", bs);
    dv_set(&d, "loc_blind_states", lbs);
    dv_set(&d, "blind_choices", bc);
    dv_set_bool(&d, "boss_rerolled", r->boss_rerolled);
    if (r->has_blind_tags) {
        DVal bt = dv_table();
        put_s(&bt, "Small", r->blind_tags[0]);
        put_s(&bt, "Big", r->blind_tags[1]);
        dv_set(&d, "blind_tags", bt);
    }
    if (r->blind) {
        DVal b = dv_table();
        put_s(&b, "key", r->blind->key);
        put_s(&b, "name", r->blind->name);
        dv_set(&d, "blind", b);
    }
    return d;
}
static void round_resets_load(RoundResets *r, DVal *d) {
    if (!dv_is_table(d)) return;
    get_n(d, "hands", &r->hands); get_n(d, "discards", &r->discards); get_n(d, "reroll_cost", &r->reroll_cost);
    if (dv_get(d, "temp_reroll_cost")) { r->has_temp_reroll_cost = true; get_n(d, "temp_reroll_cost", &r->temp_reroll_cost); }
    if (dv_get(d, "temp_handsize")) { r->has_temp_handsize = true; get_n(d, "temp_handsize", &r->temp_handsize); }
    get_n(d, "ante", &r->ante); get_n(d, "blind_ante", &r->blind_ante);
    DVal *bs = dv_get(d, "blind_states"), *lbs = dv_get(d, "loc_blind_states"), *bc = dv_get(d, "blind_choices");
    for (int i = 0; i < 3; i++) {
        if (bs) get_cs(bs, BLIND_TYPE_NAMES[i], &r->blind_states[i]);
        if (lbs) get_s(lbs, BLIND_TYPE_NAMES[i], &r->loc_blind_states[i]);
        if (bc) get_cs(bc, BLIND_TYPE_NAMES[i], &r->blind_choices[i]);
    }
    get_b(d, "boss_rerolled", &r->boss_rerolled);
    DVal *bt = dv_get(d, "blind_tags");
    if (bt) {
        r->has_blind_tags = true;
        get_cs(bt, "Small", &r->blind_tags[0]);
        get_cs(bt, "Big", &r->blind_tags[1]);
    }
    DVal *b = dv_get(d, "blind");
    if (b) {
        const char *key = dv_get_str(b, "key", NULL);
        if (key) r->blind = P_BLIND(key);
        else {
            /* the original game stores the whole prototype; look it up by name */
            const char *name = dv_get_str(b, "name", NULL);
            for (int i = 0; name && i < G.n_blinds; i++)
                if (str_eq(G.P_BLINDS_LIST[i].name, name)) { r->blind = &G.P_BLINDS_LIST[i]; break; }
        }
    }
}

static DVal hands_save(HandStat *hs) {
    DVal d = dv_table();
    for (int h = 0; h < HAND_COUNT; h++) {
        HandStat *s = &hs[h];
        DVal e = dv_table();
        dv_set_bool(&e, "visible", s->visible);
        dv_set_num(&e, "order", s->order); dv_set_num(&e, "mult", s->mult); dv_set_num(&e, "chips", s->chips);
        dv_set_num(&e, "s_mult", s->s_mult); dv_set_num(&e, "s_chips", s->s_chips); dv_set_num(&e, "level", s->level);
        dv_set_num(&e, "l_mult", s->l_mult); dv_set_num(&e, "l_chips", s->l_chips); dv_set_num(&e, "played", s->played);
        dv_set_num(&e, "played_this_round", s->played_this_round);
        DVal ex = dv_table();
        for (int i = 0; i < 5; i++) {
            if (!s->example[i].card) continue;
            DVal pair = dv_table();
            dv_seti(&pair, 1, dv_str(s->example[i].card));
            dv_seti(&pair, 2, dv_bool(s->example[i].scoring));
            dv_seti(&ex, i + 1, pair);
        }
        dv_set(&e, "example", ex);
        dv_set(&d, HAND_NAMES[h], e);
    }
    return d;
}
static void hands_load(HandStat *hs, DVal *d) {
    if (!dv_is_table(d)) return;
    for (int h = 0; h < HAND_COUNT; h++) {
        DVal *e = dv_get(d, HAND_NAMES[h]);
        if (!e) continue;
        HandStat *s = &hs[h];
        get_b(e, "visible", &s->visible);
        get_n(e, "order", &s->order); get_n(e, "mult", &s->mult); get_n(e, "chips", &s->chips);
        get_n(e, "s_mult", &s->s_mult); get_n(e, "s_chips", &s->s_chips); get_n(e, "level", &s->level);
        get_n(e, "l_mult", &s->l_mult); get_n(e, "l_chips", &s->l_chips); get_n(e, "played", &s->played);
        get_n(e, "played_this_round", &s->played_this_round);
        DVal *ex = dv_get(e, "example");
        for (int i = 0; ex && i < 5; i++) {
            DVal *pair = dv_geti(ex, i + 1);
            if (!pair) continue;
            const char *c = dv_geti_str(pair, 1, NULL);
            if (c) s->example[i].card = intern(c);
            s->example[i].scoring = dv_truthy(dv_geti(pair, 2));
        }
    }
}

DVal gamestate_save(GameState *g) {
    DVal d = dv_table();
    dv_set_bool(&d, "won", g->won);
    DVal rs = dv_table();
#define RS(f) dv_set(&rs, #f, score_save(&g->round_scores.f))
    RS(furthest_ante); RS(furthest_round); RS(hand); RS(poker_hand); RS(new_collection);
    RS(cards_played); RS(cards_discarded); RS(times_rerolled); RS(cards_purchased);
#undef RS
    dv_set(&d, "round_scores", rs);
    dv_set(&d, "joker_usage", dv_table());
    dv_set(&d, "consumeable_usage", usage_save(&g->consumeable_usage, false));
    dv_set(&d, "hand_usage", usage_save(&g->hand_usage, true));
    put_s(&d, "last_tarot_planet", g->last_tarot_planet);
    dv_set_num(&d, "win_ante", g->win_ante);
    dv_set_num(&d, "stake", g->stake);
    dv_set(&d, "modifiers", modifiers_save(&g->modifiers));
    dv_set(&d, "starting_params", starting_params_save(&g->starting_params));
    dv_set(&d, "banned_keys", strset_save(&g->banned_keys));
    dv_set_num(&d, "round", g->round);
    DVal prob = dv_table(); dv_set_num(&prob, "normal", g->probabilities.normal); dv_set(&d, "probabilities", prob);
    DVal bu = dv_table();
    for (int i = 0; i < g->bosses_used.cap; i++) if (NUMMAP_LIVE(&g->bosses_used, i)) dv_set_num(&bu, g->bosses_used.keys[i], g->bosses_used.vals[i]);
    dv_set(&d, "bosses_used", bu);
    DVal pr = dv_table();
    for (int i = 0; i < g->pseudorandom.keys.cap; i++)
        if (NUMMAP_LIVE(&g->pseudorandom.keys, i)) dv_set_num(&pr, g->pseudorandom.keys.keys[i], g->pseudorandom.keys.vals[i]);
    put_s(&pr, "seed", g->pseudorandom.seed);
    if (g->pseudorandom.has_hashed_seed) dv_set_num(&pr, "hashed_seed", g->pseudorandom.hashed_seed);
    dv_set(&d, "pseudorandom", pr);
#define N(f) dv_set_num(&d, #f, g->f)
    N(starting_deck_size); N(ecto_minus); N(pack_size); N(skips); N(STOP_USE);
    N(edition_rate); N(joker_rate); N(tarot_rate); N(planet_rate); N(spectral_rate); N(playing_card_rate);
    N(consumeable_buffer); N(joker_buffer); N(discount_percent); N(interest_cap); N(interest_amount); N(inflation);
    N(hands_played); N(unused_discards); N(perishable_rounds); N(rental_rate); N(chips); N(dollars); N(max_jokers);
    N(bankrupt_at); N(current_boss_streak); N(base_reroll_cost); N(tag_tally); N(dollar_buffer);
    N(starting_voucher_count); N(pack_choices);
#undef N
    put_s(&d, "chips_text", g->chips_text);
    put_s(&d, "voucher_text", g->voucher_text);
    put_s(&d, "blind_on_deck", g->blind_on_deck);
    put_s(&d, "sort", g->sort);
    DVal prev = dv_table(); dv_set_num(&prev, "dollars", g->previous_round.dollars); dv_set(&d, "previous_round", prev);
    dv_set(&d, "pool_flags", strset_save(&g->pool_flags));
    dv_set(&d, "used_jokers", strset_save(&g->used_jokers));
    dv_set(&d, "used_vouchers", strset_save(&g->used_vouchers));
    dv_set(&d, "current_round", current_round_save(&g->current_round));
    dv_set(&d, "round_resets", round_resets_save(&g->round_resets));
    DVal rb = dv_table();
    dv_set_num(&rb, "next_hands", g->round_bonus.next_hands); dv_set_num(&rb, "discards", g->round_bonus.discards);
    dv_set(&d, "round_bonus", rb);
    DVal shop = dv_table(); dv_set_num(&shop, "joker_max", g->shop.joker_max); dv_set(&d, "shop", shop);
    DVal cp = dv_table();
    for (int r = 0; r < 13; r++) {
        DVal e = dv_table();
        DVal suits = dv_table();
        NumMap *m = &g->cards_played[r].suits;
        for (int i = 0; i < m->cap; i++) if (NUMMAP_LIVE(m, i)) dv_set_num(&suits, m->keys[i], m->vals[i]);
        dv_set(&e, "suits", suits);
        dv_set_num(&e, "total", g->cards_played[r].total);
        dv_set(&cp, RANK_KEYS[r], e);
    }
    dv_set(&d, "cards_played", cp);
    dv_set(&d, "hands", hands_save(g->hands));
    if (g->selected_back_key) {
        DVal sb = dv_table();
        put_s(&sb, "key", g->selected_back_key->key);
        put_s(&sb, "name", g->selected_back_key->name);
        dv_set(&d, "selected_back_key", sb);
    }
    put_s(&d, "challenge", g->challenge);
    if (g->seeded) dv_set_bool(&d, "seeded", true);
    if (g->consumeable_usage_total.present) {
        DVal cu = dv_table();
        dv_set_num(&cu, "tarot", g->consumeable_usage_total.tarot);
        dv_set_num(&cu, "planet", g->consumeable_usage_total.planet);
        dv_set_num(&cu, "spectral", g->consumeable_usage_total.spectral);
        dv_set_num(&cu, "tarot_planet", g->consumeable_usage_total.tarot_planet);
        dv_set_num(&cu, "all", g->consumeable_usage_total.all);
        dv_set(&d, "consumeable_usage_total", cu);
    }
    if (g->orbital_choices) {
        DVal oc = dv_table();
        for (int a = 1; a < g->n_orbital_choices; a++) {
            if (!g->orbital_choices[a].set) continue;
            DVal e = dv_table();
            for (int b = 0; b < 3; b++) put_s(&e, BLIND_TYPE_NAMES[b], g->orbital_choices[a].hands[b]);
            dv_seti(&oc, a, e);
        }
        dv_set(&d, "orbital_choices", oc);
    }
    if (g->last_blind.present) {
        DVal lb = dv_table();
        dv_set_bool(&lb, "boss", g->last_blind.boss);
        put_s(&lb, "name", g->last_blind.name);
        dv_set(&d, "last_blind", lb);
    }
    put_s(&d, "last_hand_played", g->last_hand_played);
    put_s(&d, "subhash", g->subhash);
    if (g->facing_blind) dv_set_bool(&d, "facing_blind", true);
    if (g->PACK_INTERRUPT) dv_set_num(&d, "PACK_INTERRUPT", g->PACK_INTERRUPT);
    if (g->shop_free) dv_set_bool(&d, "shop_free", true);
    if (g->shop_d6ed) dv_set_bool(&d, "shop_d6ed", true);
    if (g->first_shop_buffoon) dv_set_bool(&d, "first_shop_buffoon", true);
    if (g->win_notified) dv_set_bool(&d, "win_notified", true);
    return d;
}

/* g must already hold a fresh game object (game_init_game_object) */
void gamestate_load(GameState *g, DVal *d) {
    if (!dv_is_table(d)) return;
    get_b(d, "won", &g->won);
    DVal *rs = dv_get(d, "round_scores");
    if (rs) {
#define RS(f) score_load(&g->round_scores.f, dv_get(rs, #f))
        RS(furthest_ante); RS(furthest_round); RS(hand); RS(poker_hand); RS(new_collection);
        RS(cards_played); RS(cards_discarded); RS(times_rerolled); RS(cards_purchased);
#undef RS
    }
    usage_load(&g->consumeable_usage, dv_get(d, "consumeable_usage"));
    usage_load(&g->hand_usage, dv_get(d, "hand_usage"));
    get_cs(d, "last_tarot_planet", &g->last_tarot_planet);
    get_n(d, "win_ante", &g->win_ante);
    get_n(d, "stake", &g->stake);
    modifiers_load(&g->modifiers, dv_get(d, "modifiers"));
    starting_params_load(&g->starting_params, dv_get(d, "starting_params"));
    strset_load(&g->banned_keys, dv_get(d, "banned_keys"));
    get_n(d, "round", &g->round);
    DVal *prob = dv_get(d, "probabilities");
    if (prob) get_n(prob, "normal", &g->probabilities.normal);
    DVal *bu = dv_get(d, "bosses_used");
    if (dv_is_table(bu)) {
        for (int i = 0; i < dv_count(bu); i++) {
            const DKey *k = dv_key(bu, i);
            if (k->is_str) nummap_set(&g->bosses_used, k->s, dv_num_or(dv_val(bu, i), 0));
        }
    }
    DVal *pr = dv_get(d, "pseudorandom");
    if (dv_is_table(pr)) {
        for (int i = 0; i < dv_count(pr); i++) {
            const DKey *k = dv_key(pr, i);
            DVal *v = dv_val(pr, i);
            if (!k->is_str) continue;
            if (!strcmp(k->s, "seed")) { if (v->t == DV_STR) str_set(&g->pseudorandom.seed, v->s); }
            else if (!strcmp(k->s, "hashed_seed")) { g->pseudorandom.has_hashed_seed = true; g->pseudorandom.hashed_seed = dv_num_or(v, 0); }
            else if (v->t == DV_NUM) nummap_set(&g->pseudorandom.keys, k->s, v->n);
        }
    }
#define N(f) get_n(d, #f, &g->f)
    N(starting_deck_size); N(ecto_minus); N(pack_size); N(skips); N(STOP_USE);
    N(edition_rate); N(joker_rate); N(tarot_rate); N(planet_rate); N(spectral_rate); N(playing_card_rate);
    N(consumeable_buffer); N(joker_buffer); N(discount_percent); N(interest_cap); N(interest_amount); N(inflation);
    N(hands_played); N(unused_discards); N(perishable_rounds); N(rental_rate); N(chips); N(dollars); N(max_jokers);
    N(bankrupt_at); N(current_boss_streak); N(base_reroll_cost); N(tag_tally); N(dollar_buffer);
    N(starting_voucher_count); N(pack_choices);
#undef N
    g->STOP_USE = 0;
    get_s(d, "chips_text", &g->chips_text);
    get_s(d, "voucher_text", &g->voucher_text);
    get_cs(d, "blind_on_deck", &g->blind_on_deck);
    get_cs(d, "sort", &g->sort);
    DVal *prev = dv_get(d, "previous_round");
    if (prev) get_n(prev, "dollars", &g->previous_round.dollars);
    strset_load(&g->pool_flags, dv_get(d, "pool_flags"));
    strset_load(&g->used_jokers, dv_get(d, "used_jokers"));
    strset_load(&g->used_vouchers, dv_get(d, "used_vouchers"));
    current_round_load(&g->current_round, dv_get(d, "current_round"));
    round_resets_load(&g->round_resets, dv_get(d, "round_resets"));
    DVal *rb = dv_get(d, "round_bonus");
    if (rb) { get_n(rb, "next_hands", &g->round_bonus.next_hands); get_n(rb, "discards", &g->round_bonus.discards); }
    DVal *shop = dv_get(d, "shop");
    if (shop) get_n(shop, "joker_max", &g->shop.joker_max);
    DVal *cp = dv_get(d, "cards_played");
    for (int r = 0; cp && r < 13; r++) {
        DVal *e = dv_get(cp, RANK_KEYS[r]);
        if (!e) continue;
        get_n(e, "total", &g->cards_played[r].total);
        DVal *suits = dv_get(e, "suits");
        for (int i = 0; dv_is_table(suits) && i < dv_count(suits); i++) {
            const DKey *k = dv_key(suits, i);
            if (k->is_str) nummap_set(&g->cards_played[r].suits, k->s, dv_num_or(dv_val(suits, i), 0));
        }
    }
    hands_load(g->hands, dv_get(d, "hands"));
    DVal *sb = dv_get(d, "selected_back_key");
    if (sb) {
        const char *key = dv_get_str(sb, "key", NULL);
        if (key) g->selected_back_key = P_CENTER(key);
    }
    get_cs(d, "challenge", &g->challenge);
    if (g->challenge) g->challenge_tab = challenge_by_id(g->challenge);
    get_b(d, "seeded", &g->seeded);
    DVal *cu = dv_get(d, "consumeable_usage_total");
    if (cu) {
        g->consumeable_usage_total.present = true;
        get_n(cu, "tarot", &g->consumeable_usage_total.tarot);
        get_n(cu, "planet", &g->consumeable_usage_total.planet);
        get_n(cu, "spectral", &g->consumeable_usage_total.spectral);
        get_n(cu, "tarot_planet", &g->consumeable_usage_total.tarot_planet);
        get_n(cu, "all", &g->consumeable_usage_total.all);
    }
    DVal *oc = dv_get(d, "orbital_choices");
    if (dv_is_table(oc)) {
        for (int i = 0; i < dv_count(oc); i++) {
            const DKey *k = dv_key(oc, i);
            if (k->is_str || k->n < 1 || k->n > 1000) continue;
            int a = (int)k->n;
            if (a >= g->n_orbital_choices) {
                int n = a + 1;
                g->orbital_choices = xrealloc(g->orbital_choices, (size_t)n * sizeof *g->orbital_choices);
                memset(&g->orbital_choices[g->n_orbital_choices], 0, (size_t)(n - g->n_orbital_choices) * sizeof *g->orbital_choices);
                g->n_orbital_choices = n;
            }
            DVal *e = dv_val(oc, i);
            for (int b = 0; b < 3; b++) get_cs(e, BLIND_TYPE_NAMES[b], &g->orbital_choices[a].hands[b]);
            g->orbital_choices[a].set = true;
        }
    }
    DVal *lb = dv_get(d, "last_blind");
    if (lb) {
        g->last_blind.present = true;
        get_b(lb, "boss", &g->last_blind.boss);
        get_cs(lb, "name", &g->last_blind.name);
    }
    get_cs(d, "last_hand_played", &g->last_hand_played);
    get_s(d, "subhash", &g->subhash);
    get_b(d, "facing_blind", &g->facing_blind);
    g->PACK_INTERRUPT = (int)dv_get_num(d, "PACK_INTERRUPT", g->PACK_INTERRUPT);
    get_b(d, "shop_free", &g->shop_free);
    get_b(d, "shop_d6ed", &g->shop_d6ed);
    get_b(d, "first_shop_buffoon", &g->first_shop_buffoon);
    get_b(d, "win_notified", &g->win_notified);
}

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */
DVal settings_to_dval(void) {
    Settings *S = &G.SETTINGS;
    DVal d = dv_copy(&S->extra);
    if (d.t != DV_TABLE) d = dv_table();
    DVal comp = dv_table();
    put_s(&comp, "name", S->COMP.name ? S->COMP.name : "");
    put_s(&comp, "prev_name", S->COMP.prev_name ? S->COMP.prev_name : "");
    put_s(&comp, "submission_name", S->COMP.submission_name);
    dv_set_num(&comp, "score", S->COMP.score);
    dv_set(&d, "COMP", comp);
    DVal demo = dv_table();
    dv_set_num(&demo, "total_uptime", S->DEMO.total_uptime);
    dv_set_bool(&demo, "timed_CTA_shown", S->DEMO.timed_CTA_shown);
    dv_set_bool(&demo, "win_CTA_shown", S->DEMO.win_CTA_shown);
    dv_set_bool(&demo, "quit_CTA_shown", S->DEMO.quit_CTA_shown);
    dv_set(&d, "DEMO", demo);
    dv_set(&d, "ACHIEVEMENTS_EARNED", S->ACHIEVEMENTS_EARNED.t == DV_TABLE ? dv_copy(&S->ACHIEVEMENTS_EARNED) : dv_table());
    dv_set_bool(&d, "crashreports", S->crashreports);
    dv_set_bool(&d, "colourblind_option", S->colourblind_option);
    put_s(&d, "language", S->language);
    if (S->screenshake < 0) dv_set_bool(&d, "screenshake", true);
    else dv_set_num(&d, "screenshake", S->screenshake);
    dv_set_bool(&d, "run_stake_stickers", S->run_stake_stickers);
    dv_set_num(&d, "rumble", S->rumble);
    dv_set_num(&d, "play_button_pos", S->play_button_pos);
    dv_set_num(&d, "GAMESPEED", S->GAMESPEED);
    DVal snd = dv_table();
    dv_set_num(&snd, "volume", S->SOUND.volume);
    dv_set_num(&snd, "music_volume", S->SOUND.music_volume);
    dv_set_num(&snd, "game_sounds_volume", S->SOUND.game_sounds_volume);
    dv_set(&d, "SOUND", snd);
    DVal win = dv_table();
    put_s(&win, "screenmode", S->WINDOW.screenmode);
    dv_set_num(&win, "vsync", S->WINDOW.vsync);
    dv_set_num(&win, "selected_display", S->WINDOW.selected_display);
    DVal res = dv_table();
    dv_set_num(&res, "w", S->WINDOW.screen_res.w);
    dv_set_num(&res, "h", S->WINDOW.screen_res.h);
    dv_set(&win, "screen_res", res);
    dv_set(&d, "WINDOW", win);
    DVal cd = dv_table();
    DVal col = dv_table();
    put_s(&col, "Spades", S->CUSTOM_DECK_Collabs.Spades);
    put_s(&col, "Hearts", S->CUSTOM_DECK_Collabs.Hearts);
    put_s(&col, "Clubs", S->CUSTOM_DECK_Collabs.Clubs);
    put_s(&col, "Diamonds", S->CUSTOM_DECK_Collabs.Diamonds);
    dv_set(&cd, "Collabs", col);
    dv_set(&d, "CUSTOM_DECK", cd);
    DVal gr = dv_table();
    dv_set_num(&gr, "texture_scaling", S->GRAPHICS.texture_scaling);
    put_s(&gr, "shadows", S->GRAPHICS.shadows);
    dv_set_num(&gr, "crt", S->GRAPHICS.crt);
    dv_set_num(&gr, "bloom", S->GRAPHICS.bloom);
    dv_set(&d, "GRAPHICS", gr);
    dv_set_num(&d, "profile", S->profile);
    if (S->tutorial_complete) dv_set_bool(&d, "tutorial_complete", true);
    if (S->tutorial_progress.t == DV_TABLE) dv_set(&d, "tutorial_progress", dv_copy(&S->tutorial_progress));
    put_s(&d, "version", S->version);
    if (S->reduced_motion) dv_set_bool(&d, "reduced_motion", true);
    put_s(&d, "skip_splash", S->skip_splash);
    put_s(&d, "current_setup", S->current_setup);
    if (S->perf_mode) dv_set_bool(&d, "perf_mode", true);
    return d;
}

static void sset(char **dst, const DVal *v) { if (v && v->t == DV_STR) str_set(dst, v->s); }

void settings_from_dval(DVal *d) {
    Settings *S = &G.SETTINGS;
    if (!dv_is_table(d)) return;
    for (int i = 0; i < dv_count(d); i++) {
        const DKey *k = dv_key(d, i);
        DVal *v = dv_val(d, i);
        if (!k->is_str) continue;
        const char *key = k->s;
        if (!strcmp(key, "COMP")) {
            sset(&S->COMP.name, dv_get(v, "name"));
            sset(&S->COMP.prev_name, dv_get(v, "prev_name"));
            sset(&S->COMP.submission_name, dv_get(v, "submission_name"));
            S->COMP.score = dv_get_num(v, "score", S->COMP.score);
        } else if (!strcmp(key, "DEMO")) {
            S->DEMO.total_uptime = dv_get_num(v, "total_uptime", 0);
            S->DEMO.timed_CTA_shown = dv_get_bool(v, "timed_CTA_shown", false);
            S->DEMO.win_CTA_shown = dv_get_bool(v, "win_CTA_shown", false);
            S->DEMO.quit_CTA_shown = dv_get_bool(v, "quit_CTA_shown", false);
        } else if (!strcmp(key, "ACHIEVEMENTS_EARNED")) {
            dv_free(&S->ACHIEVEMENTS_EARNED);
            S->ACHIEVEMENTS_EARNED = dv_is_table(v) ? dv_copy(v) : dv_table();
        } else if (!strcmp(key, "crashreports")) S->crashreports = dv_truthy(v);
        else if (!strcmp(key, "colourblind_option")) S->colourblind_option = dv_truthy(v);
        else if (!strcmp(key, "language")) sset(&S->language, v);
        else if (!strcmp(key, "screenshake")) S->screenshake = v->t == DV_NUM ? v->n : (dv_truthy(v) ? -1 : 0);
        else if (!strcmp(key, "run_stake_stickers")) S->run_stake_stickers = dv_truthy(v);
        else if (!strcmp(key, "rumble")) S->rumble = v->t == DV_NUM ? v->n : (dv_truthy(v) ? 1 : 0);
        else if (!strcmp(key, "play_button_pos")) S->play_button_pos = dv_num_or(v, 2);
        else if (!strcmp(key, "GAMESPEED")) S->GAMESPEED = dv_num_or(v, 1);
        else if (!strcmp(key, "paused")) { }
        else if (!strcmp(key, "SOUND")) {
            S->SOUND.volume = dv_get_num(v, "volume", S->SOUND.volume);
            S->SOUND.music_volume = dv_get_num(v, "music_volume", S->SOUND.music_volume);
            S->SOUND.game_sounds_volume = dv_get_num(v, "game_sounds_volume", S->SOUND.game_sounds_volume);
        } else if (!strcmp(key, "WINDOW")) {
            sset(&S->WINDOW.screenmode, dv_get(v, "screenmode"));
            S->WINDOW.vsync = dv_get_num(v, "vsync", S->WINDOW.vsync);
            S->WINDOW.selected_display = (int)dv_get_num(v, "selected_display", S->WINDOW.selected_display);
            DVal *res = dv_get(v, "screen_res");
            if (!res) {
                /* original layout: WINDOW.DISPLAYS[selected].screen_res */
                DVal *disp = dv_geti(dv_get(v, "DISPLAYS"), S->WINDOW.selected_display);
                res = dv_get(disp, "screen_res");
            }
            if (res) {
                S->WINDOW.screen_res.w = (int)dv_get_num(res, "w", S->WINDOW.screen_res.w);
                S->WINDOW.screen_res.h = (int)dv_get_num(res, "h", S->WINDOW.screen_res.h);
            }
        } else if (!strcmp(key, "CUSTOM_DECK")) {
            DVal *c = dv_get(v, "Collabs");
            if (c) {
                sset(&S->CUSTOM_DECK_Collabs.Spades, dv_get(c, "Spades"));
                sset(&S->CUSTOM_DECK_Collabs.Hearts, dv_get(c, "Hearts"));
                sset(&S->CUSTOM_DECK_Collabs.Clubs, dv_get(c, "Clubs"));
                sset(&S->CUSTOM_DECK_Collabs.Diamonds, dv_get(c, "Diamonds"));
            }
        } else if (!strcmp(key, "GRAPHICS")) {
            S->GRAPHICS.texture_scaling = dv_get_num(v, "texture_scaling", S->GRAPHICS.texture_scaling);
            sset(&S->GRAPHICS.shadows, dv_get(v, "shadows"));
            S->GRAPHICS.crt = dv_get_num(v, "crt", S->GRAPHICS.crt);
            S->GRAPHICS.bloom = dv_get_num(v, "bloom", S->GRAPHICS.bloom);
        } else if (!strcmp(key, "profile")) S->profile = (int)dv_num_or(v, 1);
        else if (!strcmp(key, "tutorial_complete")) S->tutorial_complete = dv_truthy(v);
        else if (!strcmp(key, "tutorial_progress")) {
            dv_free(&S->tutorial_progress);
            S->tutorial_progress = dv_is_table(v) ? dv_copy(v) : dv_nil();
        } else if (!strcmp(key, "version")) sset(&S->version, v);
        else if (!strcmp(key, "reduced_motion")) S->reduced_motion = dv_truthy(v);
        else if (!strcmp(key, "skip_splash")) sset(&S->skip_splash, v);
        else if (!strcmp(key, "current_setup")) sset(&S->current_setup, v);
        else if (!strcmp(key, "perf_mode")) S->perf_mode = dv_truthy(v);
        else if (!strcmp(key, "QUEUED_CHANGE") || !strcmp(key, "music_control")) { }
        else dv_set(&S->extra, key, dv_copy(v));
    }
    if (S->profile < 1 || S->profile > 3) S->profile = 1;
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */
bool get_compressed(const char *rel, DVal *out) {
    char *text = get_compressed_text(rel);
    if (!text) return false;
    DVal d = dv_nil();
    bool ok = dv_parse(text, &d);
    free(text);
    if (!ok || d.t != DV_TABLE) { dv_free(&d); return false; }
    *out = d;
    return true;
}

static void ensure_profile_dir(int p) { dir_make(res_save_path(tfmt("%d", p))); }

static void write_settings_and_profile(void) {
    DVal s = settings_to_dval();
    dv_save_file(res_save_path("settings.jkr"), &s, true);
    dv_free(&s);
    int p = G.SETTINGS.profile;
    ensure_profile_dir(p);
    dv_save_file(res_save_path(tfmt("%d/profile.jkr", p)), profile(), true);
}

static void uda_merge(DVal *meta, const char *key, bool u, bool d, bool a, bool *changed) {
    if (u) { DVal *t = dv_get_or_create_table(meta, "unlocked"); if (!dv_truthy(dv_get(t, key))) { dv_set_bool(t, key, true); *changed = true; } }
    if (d) { DVal *t = dv_get_or_create_table(meta, "discovered"); if (!dv_truthy(dv_get(t, key))) { dv_set_bool(t, key, true); *changed = true; } }
    if (a) { DVal *t = dv_get_or_create_table(meta, "alerted"); if (!dv_truthy(dv_get(t, key))) { dv_set_bool(t, key, true); *changed = true; } }
}

static void write_progress(void) {
    int p = G.SETTINGS.profile;
    ensure_profile_dir(p);
    const char *rel = tfmt("%d/meta.jkr", p);
    char relbuf[64];
    snprintf(relbuf, sizeof relbuf, "%s", rel);
    DVal meta;
    if (!get_compressed(relbuf, &meta)) meta = dv_table();
    dv_get_or_create_table(&meta, "unlocked");
    dv_get_or_create_table(&meta, "discovered");
    dv_get_or_create_table(&meta, "alerted");
    bool changed = false;
    for (int i = 0; i < G.n_centers; i++) {
        Center *c = &G.P_CENTERS_LIST[i];
        uda_merge(&meta, c->key, c->unlocked, c->discovered, c->alerted, &changed);
    }
    for (int i = 0; i < G.n_blinds; i++) {
        BlindProto *b = &G.P_BLINDS_LIST[i];
        uda_merge(&meta, b->key, false, b->discovered, b->alerted, &changed);
    }
    for (int i = 0; i < G.n_tags; i++) {
        TagProto *t = &G.P_TAGS_LIST[i];
        uda_merge(&meta, t->key, false, t->discovered, t->alerted, &changed);
    }
    for (int i = 0; i < G.n_seals; i++) {
        SealProto *s = &G.P_SEALS_LIST[i];
        uda_merge(&meta, s->key, false, s->discovered, s->alerted, &changed);
    }
    if (changed || !file_exists(res_save_path(relbuf))) dv_save_file(res_save_path(relbuf), &meta, true);
    dv_free(&meta);
    write_settings_and_profile();
}

/* ------------------------------------------------------------------ */
/* Game:save_* (queue a write; flushed by save_manager_update)         */
/* ------------------------------------------------------------------ */
void game_save_progress(void) {
    G.FILE_HANDLER.present = true;
    G.FILE_HANDLER.progress = true;
    G.FILE_HANDLER.update_queued = true;
}

void game_save_settings(void) {
    G.FILE_HANDLER.present = true;
    G.FILE_HANDLER.settings = true;
    G.FILE_HANDLER.update_queued = true;
}

void game_save_metrics(void) {
    /* the original sets the settings flag here, so metrics.jkr is never written */
    G.FILE_HANDLER.present = true;
    G.FILE_HANDLER.settings = true;
    G.FILE_HANDLER.update_queued = true;
}

void game_save_notify(Center *card) {
    if (!card || !card->key || G.F_NO_SAVING) return;
    int p = G.SETTINGS.profile;
    ensure_profile_dir(p);
    char rel[64];
    snprintf(rel, sizeof rel, "%d/unlock_notify.jkr", p);
    char *cur = get_compressed_text(rel);
    if (cur && strstr(cur, card->key)) { free(cur); return; }
    char *next = xstrdup(tfmt("%s%s\n", cur ? cur : "", card->key));
    free(cur);
    int cl;
    char *c = deflate_compress(next, (int)strlen(next), &cl);
    if (c) file_write_all(res_save_path(rel), c, cl);
    free(c);
    free(next);
}

void save_manager_init(void) { save_dir(); }
void save_manager_progress(void) { if (!G.F_NO_SAVING) write_progress(); }
void save_manager_settings(void) { if (!G.F_NO_SAVING) write_settings_and_profile(); }
void save_manager_metrics(void) { if (!G.F_NO_SAVING) dv_save_file(res_save_path("metrics.jkr"), &G.METRICS, true); }
void save_manager_run(DVal *run) {
    if (G.F_NO_SAVING || !run || run->t != DV_TABLE) return;
    int p = G.SETTINGS.profile;
    ensure_profile_dir(p);
    dv_save_file(res_save_path(tfmt("%d/save.jkr", p)), run, true);
}
void save_manager_notify(const char *key) {
    Center tmp = { .key = key };
    game_save_notify(&tmp);
}

/* the FILE_HANDLER block at the end of Game:update */
void save_manager_update(void) {
    if (!G.FILE_HANDLER.present || !G.FILE_HANDLER.update_queued) return;
    if (!(G.FILE_HANDLER.force || G.FILE_HANDLER.last_sent_stage != G.STAGE ||
          (G.FILE_HANDLER.last_sent_pause != G.SETTINGS.paused && G.FILE_HANDLER.run) ||
          !G.FILE_HANDLER.has_last_sent_time || G.FILE_HANDLER.last_sent_time < G.TIMERS.UPTIME - G.F_SAVE_TIMER))
        return;
    if (G.FILE_HANDLER.metrics) save_manager_metrics();
    if (G.FILE_HANDLER.progress) save_manager_progress();
    else if (G.FILE_HANDLER.settings) save_manager_settings();
    if (G.FILE_HANDLER.run) {
        save_manager_run(&G.ARGS_save_run);
        dv_free(&G.SAVED_GAME);
    }
    G.FILE_HANDLER.force = false;
    G.FILE_HANDLER.last_sent_stage = G.STAGE;
    G.FILE_HANDLER.last_sent_time = G.TIMERS.UPTIME;
    G.FILE_HANDLER.has_last_sent_time = true;
    G.FILE_HANDLER.last_sent_pause = G.SETTINGS.paused;
    G.FILE_HANDLER.settings = false;
    G.FILE_HANDLER.progress = false;
    G.FILE_HANDLER.metrics = false;
    G.FILE_HANDLER.run = false;
}
