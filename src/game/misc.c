/* misc.c - game level helpers from functions/misc_functions.lua */
#include "game.h"

const char *HAND_NAMES[HAND_COUNT] = {
    "Flush Five", "Flush House", "Five of a Kind", "Straight Flush", "Four of a Kind", "Full House",
    "Flush", "Straight", "Three of a Kind", "Two Pair", "Pair", "High Card",
};
int hand_index(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < HAND_COUNT; i++) if (!strcmp(HAND_NAMES[i], name)) return i;
    return -1;
}
const char *BLIND_TYPE_NAMES[3] = { "Small", "Big", "Boss" };
int blind_type_index(const char *t) {
    if (!t) return -1;
    for (int i = 0; i < 3; i++) if (!strcmp(BLIND_TYPE_NAMES[i], t)) return i;
    return -1;
}

DVal *profile(void) {
    int p = G.SETTINGS.profile;
    if (p < 1 || p > 3) p = 1;
    return &G.PROFILES[p - 1];
}

/* ------------------------------------------------------------------ */
/* lookups                                                             */
/* ------------------------------------------------------------------ */
Center *P_CENTER(const char *key) { return key ? strmap_get(&G.P_CENTERS, key) : NULL; }
PCard *P_CARD(const char *key) {
    if (!key) return NULL;
    if (!strcmp(key, "empty")) return &G.P_CARD_EMPTY;
    return strmap_get(&G.P_CARDS, key);
}
BlindProto *P_BLIND(const char *key) { return key ? strmap_get(&G.P_BLINDS, key) : NULL; }
TagProto *P_TAG(const char *key) { return key ? strmap_get(&G.P_TAGS, key) : NULL; }
SealProto *P_SEAL(const char *key) { return key ? strmap_get(&G.P_SEALS, key) : NULL; }
Atlas *atlas_get(const char *name) { return name ? strmap_get(&G.ASSET_ATLAS, name) : NULL; }

Col suit_colour(const char *suit) {
    if (!suit) return NULL;
    if (!strcmp(suit, "Hearts")) return G.C.SUITS.Hearts;
    if (!strcmp(suit, "Diamonds")) return G.C.SUITS.Diamonds;
    if (!strcmp(suit, "Spades")) return G.C.SUITS.Spades;
    if (!strcmp(suit, "Clubs")) return G.C.SUITS.Clubs;
    return NULL;
}

Col *set_colour_slot(const char *set, bool secondary) {
    if (!set) return NULL;
    if (secondary) {
        if (!strcmp(set, "Default")) return &G.C.SECONDARY_SET.Default;
        if (!strcmp(set, "Enhanced")) return &G.C.SECONDARY_SET.Enhanced;
        if (!strcmp(set, "Joker")) return &G.C.SECONDARY_SET.Joker;
        if (!strcmp(set, "Tarot")) return &G.C.SECONDARY_SET.Tarot;
        if (!strcmp(set, "Planet")) return &G.C.SECONDARY_SET.Planet;
        if (!strcmp(set, "Spectral")) return &G.C.SECONDARY_SET.Spectral;
        if (!strcmp(set, "Voucher")) return &G.C.SECONDARY_SET.Voucher;
        if (!strcmp(set, "Edition")) return &G.C.SECONDARY_SET.Edition;
        return NULL;
    }
    if (!strcmp(set, "Default")) return &G.C.SET.Default;
    if (!strcmp(set, "Enhanced")) return &G.C.SET.Enhanced;
    if (!strcmp(set, "Joker")) return &G.C.SET.Joker;
    if (!strcmp(set, "Tarot")) return &G.C.SET.Tarot;
    if (!strcmp(set, "Planet")) return &G.C.SET.Planet;
    if (!strcmp(set, "Spectral")) return &G.C.SET.Spectral;
    if (!strcmp(set, "Voucher")) return &G.C.SET.Voucher;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* seeded randomness                                                   */
/* ------------------------------------------------------------------ */
static int sort_id_cmp(const void *a, const void *b) {
    const Card *x = *(Card *const *)a, *y = *(Card *const *)b;
    return x->sort_id - y->sort_id;
}

void pseudoshuffle_cards(CardList *list, double seed) {
    math_randomseed(seed);
    if (list->count > 0) qsort(list->items, (size_t)list->count, sizeof(Card *), sort_id_cmp);
    for (int i = list->count; i >= 2; i--) {
        int j = math_random_range(1, i);
        Card *t = list->items[i - 1];
        list->items[i - 1] = list->items[j - 1];
        list->items[j - 1] = t;
    }
}

double pseudohash(const char *str) {
    double num = 1;
    int n = (int)strlen(str);
    for (int i = n; i >= 1; i--) {
        num = lua_mod((1.1239285023 / num) * (unsigned char)str[i - 1] * M_PI + M_PI * i, 1);
    }
    return num;
}

static double round13(double v) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.13f", v);
    return fabs(strtod(buf, NULL));
}

double pseudoseed_predict(const char *key, const char *predict_seed) {
    double p = pseudohash(tfmt("%s%s", key, predict_seed ? predict_seed : ""));
    p = round13(lua_mod(2.134453429141 + p * 1.72431234, 1));
    return (p + pseudohash(predict_seed)) / 2;
}

double pseudoseed(const char *key) {
    if (!strcmp(key, "seed")) return math_random();
    GameState *g = &G.GAME;
    if (!nummap_has(&g->pseudorandom.keys, key))
        nummap_set(&g->pseudorandom.keys, key, pseudohash(tfmt("%s%s", key, g->pseudorandom.seed ? g->pseudorandom.seed : "")));
    double v = nummap_get(&g->pseudorandom.keys, key, 0);
    v = round13(lua_mod(2.134453429141 + v * 1.72431234, 1));
    nummap_set(&g->pseudorandom.keys, key, v);
    return (v + g->pseudorandom.hashed_seed) / 2;
}

double pseudorandom_seed(double seed) {
    math_randomseed(seed);
    return math_random();
}
double pseudorandom(const char *key) { return pseudorandom_seed(pseudoseed(key)); }
int pseudorandom_range(const char *key, int min, int max) {
    math_randomseed(pseudoseed(key));
    return math_random_range(min, max);
}

/* pseudorandom_element over an array (no sort_id): picks index 1..n */
int pseudorandom_index(int n, double seed, bool seeded) {
    if (seeded) math_randomseed(seed);
    if (n <= 0) return 0;
    return math_random_range(1, n);
}

char *random_string(int length, double seed) {
    math_randomseed(seed);
    char *r = xmalloc((size_t)length + 1);
    for (int i = 0; i < length; i++) {
        int c;
        if (math_random() > 0.7) c = math_random_range('1', '9');
        else if (math_random() > 0.45) c = math_random_range('A', 'N');
        else c = math_random_range('P', 'Z');
        r[i] = (char)c;
    }
    r[length] = 0;
    return r;
}

const char *get_first_legendary(const char *key) {
    CenterVec *pool = &G.P_JOKER_RARITY_POOLS[3];
    int i = pseudorandom_index(pool->count, pseudoseed_predict("Joker4", key), true);
    return i > 0 ? pool->items[i - 1]->key : NULL;
}

char *generate_starting_seed(void) {
    Controller *c = &G.CONTROLLER;
    if (G.GAME.stake >= 8) {
        CenterVec *pool = &G.P_JOKER_RARITY_POOLS[3];
        StrSet r_leg; strset_init(&r_leg);
        int r_tally = 0, g_tally = 0;
        for (int i = 0; i < pool->count; i++) {
            Center *v = pool->items[i];
            double wa = get_joker_win_sticker_index(v);
            if (wa >= 8) g_tally++;
            else { strset_add(&r_leg, v->key); r_tally++; }
        }
        if (r_tally > 0 && g_tally > 0) {
            double extra = 0;
            for (;;) {
                extra += 0.561892350821;
                char *s = random_string(8, extra + c->cursor_hover.T.x * 0.33411983 + c->cursor_hover.T.y * 0.874146 + 0.412311010 * c->cursor_hover.time);
                const char *fl = get_first_legendary(s);
                if (fl && strset_has(&r_leg, fl)) { strset_free(&r_leg); return s; }
                free(s);
            }
        }
        strset_free(&r_leg);
    }
    return random_string(8, c->cursor_hover.T.x * 0.33411983 + c->cursor_hover.T.y * 0.874146 + 0.412311010 * c->cursor_hover.time);
}

/* ------------------------------------------------------------------ */
/* blinds / colours                                                    */
/* ------------------------------------------------------------------ */
Col get_blind_main_colour(const char *blind) {
    bool disabled = false;
    if (!blind) blind = "";
    int bt = blind_type_index(blind);
    if (bt >= 0) {
        const char *st = G.GAME.round_resets.blind_states[bt];
        if (st && (!strcmp(st, "Defeated") || !strcmp(st, "Skipped"))) disabled = true;
        blind = G.GAME.round_resets.blind_choices[bt];
    }
    BlindProto *bp = P_BLIND(blind);
    if (disabled || !bp) return G.C.BLACK;
    if (bp->boss_colour) return bp->boss_colour;
    if (!strcmp(blind, "bl_small")) return mix_colours(G.C.BLUE, G.C.BLACK, 0.6);
    if (!strcmp(blind, "bl_big")) return mix_colours(G.C.ORANGE, G.C.BLACK, 0.6);
    return G.C.BLACK;
}

/* ------------------------------------------------------------------ */
/* poker hand evaluation                                               */
/* ------------------------------------------------------------------ */
static CardList *cl_new(void) { return NEW(CardList); }
static void cl_push(CardList *l, Card *c) { vec_push(l, c); }
static void cll_push(CardListList *l, CardList *c) { vec_push(l, c); }

void find_joker(const char *name, bool non_debuff, CardList *out) {
    out->count = 0;
    if (!G.jokers) return;
    for (int i = 0; i < G.jokers->cards.count; i++) {
        Card *v = G.jokers->cards.items[i];
        if (v && str_eq(v->ability.name, name) && (non_debuff || !v->debuff)) cl_push(out, v);
    }
    if (G.consumeables) {
        for (int i = 0; i < G.consumeables->cards.count; i++) {
            Card *v = G.consumeables->cards.items[i];
            if (v && str_eq(v->ability.name, name) && (non_debuff || !v->debuff)) cl_push(out, v);
        }
    }
}
bool has_joker(const char *name) {
    CardList l = { 0 };
    find_joker(name, false, &l);
    bool r = l.count > 0;
    vec_free(&l);
    return r;
}
int count_joker(const char *name) {
    CardList l = { 0 };
    find_joker(name, false, &l);
    int r = l.count;
    vec_free(&l);
    return r;
}

static void get_X_same(int num, CardList *hand, CardListList *ret) {
    CardList *vals[15] = { 0 };
    for (int i = hand->count; i >= 1; i--) {
        CardList *curr = cl_new();
        cl_push(curr, hand->items[i - 1]);
        for (int j = 1; j <= hand->count; j++) {
            if (Card_get_id(hand->items[i - 1]) == Card_get_id(hand->items[j - 1]) && i != j) cl_push(curr, hand->items[j - 1]);
        }
        if (curr->count == num) {
            int id = Card_get_id(curr->items[0]);
            if (id >= 1 && id <= 14) vals[id] = curr;
        }
    }
    for (int i = 14; i >= 1; i--) if (vals[i] && vals[i]->count > 0) cll_push(ret, vals[i]);
}

static void get_flush(CardList *hand, CardListList *ret) {
    bool ff = has_joker("Four Fingers");
    static const char *suits[4] = { "Spades", "Hearts", "Clubs", "Diamonds" };
    if (hand->count > 5 || hand->count < (5 - (ff ? 1 : 0))) return;
    for (int j = 0; j < 4; j++) {
        CardList *t = cl_new();
        int fc = 0;
        for (int i = 0; i < hand->count; i++)
            if (Card_is_suit(hand->items[i], suits[j], false, true)) { fc++; cl_push(t, hand->items[i]); }
        if (fc >= (5 - (ff ? 1 : 0))) { cll_push(ret, t); return; }
        vec_free(t); free(t);
    }
}

static void get_straight(CardList *hand, CardListList *ret) {
    bool ff = has_joker("Four Fingers");
    if (hand->count > 5 || hand->count < (5 - (ff ? 1 : 0))) return;
    CardList *IDS[15] = { 0 };
    for (int i = 0; i < hand->count; i++) {
        int id = Card_get_id(hand->items[i]);
        if (id > 1 && id < 15) {
            if (!IDS[id]) IDS[id] = cl_new();
            cl_push(IDS[id], hand->items[i]);
        }
    }
    CardList *t = cl_new();
    int straight_length = 0;
    bool straight = false;
    bool can_skip = has_joker("Shortcut");
    bool skipped_rank = false;
    for (int j = 1; j <= 14; j++) {
        int idx = j == 1 ? 14 : j;
        if (IDS[idx]) {
            straight_length++;
            skipped_rank = false;
            for (int k = 0; k < IDS[idx]->count; k++) cl_push(t, IDS[idx]->items[k]);
        } else if (can_skip && !skipped_rank && j != 14) {
            skipped_rank = true;
        } else {
            straight_length = 0;
            skipped_rank = false;
            if (!straight) t->count = 0;
            if (straight) break;
        }
        if (straight_length >= (5 - (ff ? 1 : 0))) straight = true;
    }
    if (!straight) return;
    cll_push(ret, t);
}

static void get_highest(CardList *hand, CardListList *ret) {
    Card *highest = NULL;
    for (int i = 0; i < hand->count; i++) {
        Card *v = hand->items[i];
        if (!highest || Card_get_nominal(v, NULL) > Card_get_nominal(highest, NULL)) highest = v;
    }
    if (hand->count > 0) {
        CardList *l = cl_new();
        cl_push(l, highest);
        cll_push(ret, l);
    }
}

static void set_hand(PokerHands *r, int hand, CardListList *src) {
    r->h[hand] = *src;
    if (r->top < 0) r->top = hand;
}

PokerHands *evaluate_poker_hand(CardList *hand) {
    PokerHands *r = NEW(PokerHands);
    r->top = -1;
    CardListList p5 = { 0 }, p4 = { 0 }, p3 = { 0 }, p2 = { 0 }, pflush = { 0 }, pstraight = { 0 }, phigh = { 0 };
    get_X_same(5, hand, &p5);
    get_X_same(4, hand, &p4);
    get_X_same(3, hand, &p3);
    get_X_same(2, hand, &p2);
    get_flush(hand, &pflush);
    get_straight(hand, &pstraight);
    get_highest(hand, &phigh);

    if (p5.count && pflush.count) set_hand(r, HAND_FLUSH_FIVE, &p5);
    if (p3.count && p2.count && pflush.count) {
        CardList *fh = cl_new();
        for (int i = 0; i < p3.items[0]->count; i++) cl_push(fh, p3.items[0]->items[i]);
        for (int i = 0; i < p2.items[0]->count; i++) cl_push(fh, p2.items[0]->items[i]);
        cll_push(&r->h[HAND_FLUSH_HOUSE], fh);
        if (r->top < 0) r->top = HAND_FLUSH_HOUSE;
    }
    if (p5.count) set_hand(r, HAND_FIVE_OF_A_KIND, &p5);
    if (pflush.count && pstraight.count) {
        CardList *ret = cl_new();
        CardList *f = pflush.items[0], *s = pstraight.items[0];
        for (int i = 0; i < f->count; i++) cl_push(ret, f->items[i]);
        for (int i = 0; i < s->count; i++) {
            bool in = false;
            for (int j = 0; j < f->count; j++) if (f->items[j] == s->items[i]) in = true;
            if (!in) cl_push(ret, s->items[i]);
        }
        cll_push(&r->h[HAND_STRAIGHT_FLUSH], ret);
        if (r->top < 0) r->top = HAND_STRAIGHT_FLUSH;
    }
    if (p4.count) set_hand(r, HAND_FOUR_OF_A_KIND, &p4);
    if (p3.count && p2.count) {
        CardList *fh = cl_new();
        for (int i = 0; i < p3.items[0]->count; i++) cl_push(fh, p3.items[0]->items[i]);
        for (int i = 0; i < p2.items[0]->count; i++) cl_push(fh, p2.items[0]->items[i]);
        cll_push(&r->h[HAND_FULL_HOUSE], fh);
        if (r->top < 0) r->top = HAND_FULL_HOUSE;
    }
    if (pflush.count) set_hand(r, HAND_FLUSH, &pflush);
    if (pstraight.count) set_hand(r, HAND_STRAIGHT, &pstraight);
    if (p3.count) set_hand(r, HAND_THREE_OF_A_KIND, &p3);
    if (p2.count == 2 || (p3.count == 1 && p2.count == 1)) {
        CardList *fh = cl_new();
        CardList *a = p2.items[0];
        CardList *b = p2.count > 1 ? p2.items[1] : p3.items[0];
        for (int i = 0; i < a->count; i++) cl_push(fh, a->items[i]);
        for (int i = 0; i < b->count; i++) cl_push(fh, b->items[i]);
        cll_push(&r->h[HAND_TWO_PAIR], fh);
        if (r->top < 0) r->top = HAND_TWO_PAIR;
    }
    if (p2.count) set_hand(r, HAND_PAIR, &p2);
    if (phigh.count) set_hand(r, HAND_HIGH_CARD, &phigh);

    if (r->h[HAND_FIVE_OF_A_KIND].count) {
        CardListList c = { 0 };
        for (int i = 0; i < r->h[HAND_FIVE_OF_A_KIND].count && i < 4; i++) cll_push(&c, r->h[HAND_FIVE_OF_A_KIND].items[i]);
        r->h[HAND_FOUR_OF_A_KIND] = c;
    }
    if (r->h[HAND_FOUR_OF_A_KIND].count) {
        CardListList c = { 0 };
        for (int i = 0; i < r->h[HAND_FOUR_OF_A_KIND].count && i < 3; i++) cll_push(&c, r->h[HAND_FOUR_OF_A_KIND].items[i]);
        r->h[HAND_THREE_OF_A_KIND] = c;
    }
    if (r->h[HAND_THREE_OF_A_KIND].count) {
        CardListList c = { 0 };
        for (int i = 0; i < r->h[HAND_THREE_OF_A_KIND].count && i < 2; i++) cll_push(&c, r->h[HAND_THREE_OF_A_KIND].items[i]);
        r->h[HAND_PAIR] = c;
    }
    return r;
}

bool poker_hand_has(PokerHands *ph, const char *name) {
    int i = hand_index(name);
    return ph && i >= 0 && ph->h[i].count > 0;
}
CardList *poker_hand_first(PokerHands *ph, int hand) {
    if (!ph || hand < 0 || ph->h[hand].count == 0) return NULL;
    return ph->h[hand].items[0];
}
void poker_hands_free(PokerHands *ph) { (void)ph; /* lists may be shared between entries; leaked like Lua garbage */ }

/* ------------------------------------------------------------------ */
/* numbers                                                             */
/* ------------------------------------------------------------------ */
double mod_chips(double c) {
    if (G.GAME.modifiers.chips_dollar_cap) c = MIN(c, MAX(G.GAME.dollars, 0));
    return c;
}
double mod_mult(double m) { return m; }

double get_blind_amount(double ante) {
    double k = 0.75;
    static const double a1[8] = { 300, 800, 2000, 5000, 11000, 20000, 35000, 50000 };
    static const double a2[8] = { 300, 900, 2600, 8000, 20000, 36000, 60000, 100000 };
    static const double a3[8] = { 300, 1000, 3200, 9000, 25000, 60000, 110000, 200000 };
    const double *amounts;
    double sc = G.GAME.modifiers.scaling;
    if (sc == 0 || sc == 1) amounts = a1;
    else if (sc == 2) amounts = a2;
    else if (sc == 3) amounts = a3;
    else return 0;
    if (ante < 1) return 100;
    if (ante <= 8) return amounts[(int)ante - 1];
    double a = amounts[7], b = 1.6, c = ante - 8, d = 1 + 0.2 * (ante - 8);
    double amount = floor(a * pow(b + pow(k * c, d), c));
    amount = amount - lua_mod(amount, pow(10, floor(log10(amount) - 1)));
    return amount;
}

static double lj_log_base(double x, double base) { return log2(x) * (1.0 / log2(base)); }

const char *number_format(double num) {
    double E_SWITCH_POINT = 100000000000.0;
    if (num >= E_SWITCH_POINT) {
        char x[64];
        snprintf(x, sizeof x, "%.4g", num);
        double xv = atof(x);
        double fac = floor(lj_log_base(xv, 10));
        return tfmt("%.3fe%s", xv / pow(10, fac), num_str(fac));
    }
    char buf[64];
    if (num != floor(num)) snprintf(buf, sizeof buf, num >= 100 ? "%.0f" : num >= 10 ? "%.1f" : "%.2f", num);
    else snprintf(buf, sizeof buf, "%.0f", num);
    /* reverse, insert commas after every three digits, strip trailing comma, reverse */
    int n = (int)strlen(buf);
    char rev[64], out[96];
    for (int i = 0; i < n; i++) rev[i] = buf[n - 1 - i];
    rev[n] = 0;
    int o = 0;
    int i = 0;
    while (i < n) {
        if (i + 2 < n && isdigit((unsigned char)rev[i]) && isdigit((unsigned char)rev[i + 1]) && isdigit((unsigned char)rev[i + 2])) {
            out[o++] = rev[i]; out[o++] = rev[i + 1]; out[o++] = rev[i + 2]; out[o++] = ',';
            i += 3;
        } else out[o++] = rev[i++];
    }
    if (o > 0 && out[o - 1] == ',') o--;
    out[o] = 0;
    char res[96];
    for (int j = 0; j < o; j++) res[j] = out[o - 1 - j];
    res[o] = 0;
    return tstr(res);
}

double score_number_scale(double scale, double amt) {
    double E_SWITCH_POINT = 100000000000.0;
    if (isnan(scale)) scale = 1;
    if (isnan(amt)) return 0.7 * scale;
    if (amt >= E_SWITCH_POINT) return 0.7 * scale;
    if (amt >= 1000000) return 14 * 0.75 / (floor(log(amt)) + 4) * scale;
    return 0.75 * scale;
}

/* ------------------------------------------------------------------ */
/* profile statistics                                                  */
/* ------------------------------------------------------------------ */
static DVal *round_score_entry_amt(const char *score, ScoreEntry **out) {
    GameState *g = &G.GAME;
    *out = NULL;
    if (!strcmp(score, "furthest_ante")) *out = &g->round_scores.furthest_ante;
    else if (!strcmp(score, "furthest_round")) *out = &g->round_scores.furthest_round;
    else if (!strcmp(score, "hand")) *out = &g->round_scores.hand;
    else if (!strcmp(score, "poker_hand")) *out = &g->round_scores.poker_hand;
    else if (!strcmp(score, "new_collection")) *out = &g->round_scores.new_collection;
    else if (!strcmp(score, "cards_played")) *out = &g->round_scores.cards_played;
    else if (!strcmp(score, "cards_discarded")) *out = &g->round_scores.cards_discarded;
    else if (!strcmp(score, "times_rerolled")) *out = &g->round_scores.times_rerolled;
    else if (!strcmp(score, "cards_purchased")) *out = &g->round_scores.cards_purchased;
    return NULL;
}

void check_and_set_high_score(const char *score, double amt) {
    if (isnan(amt)) return;
    ScoreEntry *e;
    round_score_entry_amt(score, &e);
    if (e && floor(amt) > e->amt) e->amt = floor(amt);
    if (G.GAME.seeded) return;
    if (!strcmp(score, "hand") && (G.SETTINGS.COMP.score == 0 || G.SETTINGS.COMP.score < floor(amt))) G.SETTINGS.COMP.score = amt;
    DVal *hs = dv_get(dv_get(profile(), "high_scores"), score);
    if (hs && floor(amt) > dv_get_num(hs, "amt", 0)) {
        if (e) e->high_score = true;
        dv_set_num(hs, "amt", floor(amt));
        game_save_settings();
    }
}

static DVal *usage_entry(const char *table, const char *key, Center *center, bool with_wl) {
    DVal *t = dv_get_or_create_table(profile(), table);
    DVal *e = dv_get(t, key);
    if (!e) {
        DVal n = dv_table();
        dv_set_num(&n, "count", 1);
        dv_set_num(&n, "order", center ? center->order : 0);
        if (with_wl) { dv_set(&n, "wins", dv_table()); dv_set(&n, "losses", dv_table()); }
        dv_set(t, key, n);
        e = dv_get(t, key);
        dv_set_num(e, "count", 0);   /* caller increments */
    }
    return e;
}

void set_joker_usage(void) {
    for (int i = 0; i < G.jokers->cards.count; i++) {
        Card *v = G.jokers->cards.items[i];
        if (v->config.center_key && str_eq(v->ability.set, "Joker")) {
            DVal *t = dv_get_or_create_table(profile(), "joker_usage");
            DVal *e = dv_get(t, v->config.center_key);
            if (e) dv_set_num(e, "count", dv_get_num(e, "count", 0) + 1);
            else {
                DVal n = dv_table();
                dv_set_num(&n, "count", 1);
                dv_set_num(&n, "order", v->config.center->order);
                dv_set(&n, "wins", dv_table());
                dv_set(&n, "losses", dv_table());
                dv_set(t, v->config.center_key, n);
            }
        }
    }
    game_save_settings();
}

void set_joker_win(void) {
    for (int i = 0; i < G.jokers->cards.count; i++) {
        Card *v = G.jokers->cards.items[i];
        if (v->config.center_key && str_eq(v->ability.set, "Joker")) {
            DVal *t = dv_get_or_create_table(profile(), "joker_usage");
            DVal *e = dv_get(t, v->config.center_key);
            if (!e) {
                DVal n = dv_table();
                dv_set_num(&n, "count", 1);
                dv_set_num(&n, "order", v->config.center->order);
                dv_set(&n, "wins", dv_table());
                dv_set(&n, "losses", dv_table());
                dv_set(t, v->config.center_key, n);
                e = dv_get(t, v->config.center_key);
            }
            DVal *wins = dv_get_or_create_table(e, "wins");
            int st = (int)G.GAME.stake;
            dv_seti(wins, st, dv_num(dv_num_or(dv_geti(wins, st), 0) + 1));
        }
    }
    game_save_settings();
}

double get_joker_win_sticker_index(Center *c) {
    DVal *e = dv_get(dv_get(profile(), "joker_usage"), c->key);
    DVal *wins = dv_get(e, "wins");
    if (e && wins) {
        double w = 0;
        for (int i = 0; i < dv_count(wins); i++) {
            const DKey *k = dv_key(wins, i);
            if (!k->is_str) w = MAX(k->n, w);
        }
        return w;
    }
    return 0;
}
const char *get_joker_win_sticker(Center *c) {
    DVal *e = dv_get(dv_get(profile(), "joker_usage"), c->key);
    DVal *wins = dv_get(e, "wins");
    if (e && wins) {
        double w = get_joker_win_sticker_index(c);
        if (w > 0 && w <= 8) return G.sticker_map[(int)w - 1];
    }
    return NULL;
}

void set_joker_loss(void) {
    for (int i = 0; i < G.jokers->cards.count; i++) {
        Card *v = G.jokers->cards.items[i];
        if (v->config.center_key && str_eq(v->ability.set, "Joker")) {
            DVal *e = dv_get(dv_get(profile(), "joker_usage"), v->config.center_key);
            if (e) {
                DVal *l = dv_get_or_create_table(e, "losses");
                int st = (int)G.GAME.stake;
                dv_seti(l, st, dv_num(dv_num_or(dv_geti(l, st), 0) + 1));
            }
        }
    }
    game_save_settings();
}

static DVal *deck_usage_entry(bool create) {
    Back *b = G.GAME.selected_back;
    if (!b || !b->effect.center || !b->effect.center->key) return NULL;
    const char *dk = b->effect.center->key;
    DVal *t = dv_get_or_create_table(profile(), "deck_usage");
    DVal *e = dv_get(t, dk);
    if (!e && create) {
        DVal n = dv_table();
        dv_set_num(&n, "count", 1);
        dv_set_num(&n, "order", b->effect.center->order);
        dv_set(&n, "wins", dv_table());
        dv_set(&n, "losses", dv_table());
        dv_set(t, dk, n);
        e = dv_get(t, dk);
    }
    return e;
}

void set_deck_usage(void) {
    Back *b = G.GAME.selected_back;
    if (!b || !b->effect.center || !b->effect.center->key) return;
    DVal *t = dv_get_or_create_table(profile(), "deck_usage");
    DVal *e = dv_get(t, b->effect.center->key);
    if (e) dv_set_num(e, "count", dv_get_num(e, "count", 0) + 1);
    else deck_usage_entry(true);
    game_save_settings();
}

void set_deck_win(void) {
    DVal *e = deck_usage_entry(true);
    if (!e) return;
    DVal *wins = dv_get_or_create_table(e, "wins");
    int st = (int)G.GAME.stake;
    dv_seti(wins, st, dv_num(dv_num_or(dv_geti(wins, st), 0) + 1));
    for (int i = 1; i <= st; i++) dv_seti(wins, i, dv_num(dv_num_or(dv_geti(wins, i), 1)));
    set_challenge_unlock();
    game_save_settings();
}

void set_challenge_unlock(void) {
    DVal *p = profile();
    if (dv_get_bool(p, "all_unlocked", false)) return;
    DVal *cu = dv_get(p, "challenges_unlocked");
    if (dv_truthy(cu)) {
        int comp = 0;
        DVal *done = dv_get(dv_get(p, "challenge_progress"), "completed");
        for (int i = 0; i < G_N_CHALLENGES; i++)
            if (G_CHALLENGES[i].id && dv_truthy(dv_get(done, G_CHALLENGES[i].id))) comp++;
        dv_set_num(p, "challenges_unlocked", MIN(G_N_CHALLENGES, comp + 5));
    } else {
        int deck_wins = 0;
        DVal *du = dv_get(p, "deck_usage");
        for (int i = 0; i < dv_count(du); i++) {
            DVal *v = dv_val(du, i);
            DVal *w = dv_get(v, "wins");
            if (w && dv_geti(w, 1)) deck_wins++;
        }
        if (deck_wins >= G.CHALLENGE_WINS) {
            dv_set_num(p, "challenges_unlocked", 5);
            notify_alert("b_challenge", "Back");
        }
    }
}

double get_deck_win_stake(const char *deck_key) {
    DVal *e = dv_get(dv_get(profile(), "deck_usage"), deck_key);
    DVal *w = dv_get(e, "wins");
    if (e && w) {
        double best = 0;
        for (int i = 0; i < dv_count(w); i++) {
            const DKey *k = dv_key(w, i);
            if (!k->is_str) best = MAX(k->n, best);
        }
        return best;
    }
    return 0;
}

void get_deck_win_stake_all(double *out_w, double *out_low) {
    double w = 0, w_low = NAN;
    int deck_count = 0;
    DVal *du = dv_get(profile(), "deck_usage");
    for (int i = 0; i < dv_count(du); i++) {
        DVal *deck = dv_val(du, i);
        DVal *wins = dv_get(deck, "wins");
        bool won = false;
        for (int j = 0; j < dv_count(wins); j++) {
            const DKey *k = dv_key(wins, j);
            won = true;
            if (!k->is_str) w = MAX(k->n, w);
        }
        if (won) deck_count++;
        w_low = isnan(w_low) ? w : MIN(w_low, w);
    }
    *out_w = w;
    *out_low = (deck_count >= G.P_CENTER_POOLS.Back.count) ? (isnan(w_low) ? 0 : w_low) : 0;
}

const char *get_deck_win_sticker(Center *c) {
    DVal *e = dv_get(dv_get(profile(), "deck_usage"), c->key);
    DVal *wins = dv_get(e, "wins");
    if (e && wins) {
        double w = -1;
        for (int i = 0; i < dv_count(wins); i++) {
            const DKey *k = dv_key(wins, i);
            if (!k->is_str) w = MAX(k->n, w);
        }
        if (w > 0 && w <= 8) return G.sticker_map[(int)w - 1];
    }
    return NULL;
}

void set_deck_loss(void) {
    DVal *e = deck_usage_entry(true);
    if (!e) return;
    DVal *l = dv_get_or_create_table(e, "losses");
    int st = (int)G.GAME.stake;
    dv_seti(l, st, dv_num(dv_num_or(dv_geti(l, st), 0) + 1));
    game_save_settings();
}

typedef struct { const char *key; } LastTarotCtx;
static bool ev_last_tarot_inner(Event *e, void *p) {
    (void)e;
    LastTarotCtx *c = p;
    G.GAME.last_tarot_planet = c->key;
    return true;
}
static bool ev_last_tarot_outer(Event *e, void *p) {
    (void)e;
    LastTarotCtx *c = p;
    LastTarotCtx *c2 = CTX(LastTarotCtx);
    c2->key = c->key;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_last_tarot_inner, .ctx = c2));
    return true;
}

void set_consumeable_usage(Card *card) {
    if (card->config.center_key && card->ability.consumeable) {
        const char *k = card->config.center_key;
        DVal *t = dv_get_or_create_table(profile(), "consumeable_usage");
        DVal *e = dv_get(t, k);
        if (e) dv_set_num(e, "count", dv_get_num(e, "count", 0) + 1);
        else {
            DVal n = dv_table();
            dv_set_num(&n, "count", 1);
            dv_set_num(&n, "order", card->config.center->order);
            dv_set(t, k, n);
        }
        UsageEntry *ue = strmap_get(&G.GAME.consumeable_usage, k);
        if (ue) ue->count++;
        else {
            ue = NEW(UsageEntry);
            ue->count = 1;
            ue->order = card->config.center->order;
            ue->set = card->ability.set;
            strmap_set(&G.GAME.consumeable_usage, k, ue);
        }
        if (!G.GAME.consumeable_usage_total.present) {
            memset(&G.GAME.consumeable_usage_total, 0, sizeof G.GAME.consumeable_usage_total);
            G.GAME.consumeable_usage_total.present = true;
        }
        const char *set = card->config.center->set;
        if (str_eq(set, "Tarot")) { G.GAME.consumeable_usage_total.tarot++; G.GAME.consumeable_usage_total.tarot_planet++; }
        else if (str_eq(set, "Planet")) { G.GAME.consumeable_usage_total.planet++; G.GAME.consumeable_usage_total.tarot_planet++; }
        else if (str_eq(set, "Spectral")) G.GAME.consumeable_usage_total.spectral++;
        G.GAME.consumeable_usage_total.all++;
        if (!card->config.center->discovered) discover_card(card->config.center);
        if (str_eq(set, "Tarot") || str_eq(set, "Planet")) {
            LastTarotCtx *c = CTX(LastTarotCtx);
            c->key = k;
            add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_last_tarot_outer, .ctx = c));
        }
    }
    game_save_settings();
}

void set_voucher_usage(Card *card) {
    if (card->config.center_key && str_eq(card->ability.set, "Voucher")) {
        DVal *t = dv_get_or_create_table(profile(), "voucher_usage");
        DVal *e = dv_get(t, card->config.center_key);
        if (e) dv_set_num(e, "count", dv_get_num(e, "count", 0) + 1);
        else {
            DVal n = dv_table();
            dv_set_num(&n, "count", 1);
            dv_set_num(&n, "order", card->config.center->order);
            dv_set(t, card->config.center_key, n);
        }
    }
    game_save_settings();
}

void set_hand_usage(const char *hand) {
    const char *label = hand;
    char key[64]; int o = 0;
    for (const char *p = hand; *p && o < 63; p++) if (*p != ' ' && *p != '\t' && *p != '\n') key[o++] = *p;
    key[o] = 0;
    DVal *t = dv_get_or_create_table(profile(), "hand_usage");
    DVal *e = dv_get(t, key);
    if (e) dv_set_num(e, "count", dv_get_num(e, "count", 0) + 1);
    else {
        DVal n = dv_table();
        dv_set_num(&n, "count", 1);
        dv_set_str(&n, "order", label);
        dv_set(t, key, n);
    }
    UsageEntry *ue = strmap_get(&G.GAME.hand_usage, key);
    if (ue) ue->count++;
    else {
        ue = NEW(UsageEntry);
        ue->count = 1;
        ue->order_str = xstrdup(label);
        strmap_set(&G.GAME.hand_usage, key, ue);
    }
    game_save_settings();
}

void set_profile_progress(void) {
    G.PROGRESS.present = true;
    G.PROGRESS.joker_stickers.tally = G.PROGRESS.joker_stickers.of = 0;
    G.PROGRESS.deck_stakes.tally = G.PROGRESS.deck_stakes.of = 0;
    G.PROGRESS.challenges.tally = G.PROGRESS.challenges.of = 0;
    for (int i = 0; i < G.n_centers; i++) {
        Center *v = &G.P_CENTERS_LIST[i];
        if (str_eq(v->set, "Back") && !v->omit) {
            G.PROGRESS.deck_stakes.of += G.P_CENTER_POOLS.Stake.count;
            G.PROGRESS.deck_stakes.tally += get_deck_win_stake(v->key);
        }
        if (str_eq(v->set, "Joker")) {
            G.PROGRESS.joker_stickers.of += G.P_CENTER_POOLS.Stake.count;
            G.PROGRESS.joker_stickers.tally += get_joker_win_sticker_index(v);
        }
    }
    DVal *done = dv_get(dv_get(profile(), "challenge_progress"), "completed");
    for (int i = 0; i < G_N_CHALLENGES; i++) {
        G.PROGRESS.challenges.of++;
        if (dv_truthy(dv_get(done, G_CHALLENGES[i].id))) G.PROGRESS.challenges.tally++;
    }
    DVal *prog = dv_get_or_create_table(profile(), "progress");
#define SETP(name) do { DVal t = dv_table(); dv_set_num(&t, "tally", G.PROGRESS.name.tally); dv_set_num(&t, "of", G.PROGRESS.name.of); dv_set(prog, #name, t); } while (0)
    SETP(joker_stickers);
    SETP(deck_stakes);
    SETP(challenges);
#undef SETP
}

void set_discover_tallies(void) {
    G.DISCOVER_TALLIES.present = true;
#define Z(x) G.DISCOVER_TALLIES.x.tally = G.DISCOVER_TALLIES.x.of = 0
    Z(blinds); Z(tags); Z(jokers); Z(consumeables); Z(tarots); Z(planets); Z(spectrals); Z(vouchers); Z(boosters); Z(editions); Z(backs); Z(total);
#undef Z
#define T(x, cond) do { G.DISCOVER_TALLIES.x.of++; if (cond) G.DISCOVER_TALLIES.x.tally++; } while (0)
    for (int i = 0; i < G.n_centers; i++) {
        Center *v = &G.P_CENTERS_LIST[i];
        if (v->omit || !v->set) continue;
        const char *s = v->set;
        if (!strcmp(s, "Joker") || v->consumeable || !strcmp(s, "Edition") || !strcmp(s, "Voucher") || !strcmp(s, "Back") || !strcmp(s, "Booster"))
            T(total, v->discovered);
        if (!strcmp(s, "Joker")) T(jokers, v->discovered);
        if (!strcmp(s, "Back")) T(backs, v->unlocked);
        if (v->consumeable) {
            T(consumeables, v->discovered);
            if (!strcmp(s, "Planet")) T(planets, v->discovered);
            else if (!strcmp(s, "Spectral")) T(spectrals, v->discovered);
            else if (!strcmp(s, "Tarot")) T(tarots, v->discovered);
        }
        if (!strcmp(s, "Voucher")) T(vouchers, v->discovered);
        if (!strcmp(s, "Booster")) T(boosters, v->discovered);
        if (!strcmp(s, "Edition")) T(editions, v->discovered);
    }
    for (int i = 0; i < G.n_blinds; i++) {
        BlindProto *v = &G.P_BLINDS_LIST[i];
        T(total, v->discovered);
        G.DISCOVER_TALLIES.blinds.of++;
        if (v->discovered) G.DISCOVER_TALLIES.blinds.tally++;
    }
    for (int i = 0; i < G.n_tags; i++) {
        TagProto *v = &G.P_TAGS_LIST[i];
        T(total, v->discovered);
        G.DISCOVER_TALLIES.tags.of++;
        if (v->discovered) G.DISCOVER_TALLIES.tags.tally++;
    }
#undef T
    DVal *hs = dv_get(dv_get(profile(), "high_scores"), "collection");
    if (hs) {
        dv_set_num(hs, "amt", G.DISCOVER_TALLIES.total.tally);
        dv_set_num(hs, "tot", G.DISCOVER_TALLIES.total.of);
    }
    DVal *prog = dv_get_or_create_table(profile(), "progress");
    DVal d = dv_table();
    dv_set_num(&d, "tally", G.DISCOVER_TALLIES.total.tally);
    dv_set_num(&d, "of", G.DISCOVER_TALLIES.total.of);
    dv_set(prog, "discovered", d);
    UNLOCK(.type = "discover_amount", .amount = G.DISCOVER_TALLIES.total.tally,
           .planet_count = G.DISCOVER_TALLIES.planets.tally, .tarot_count = G.DISCOVER_TALLIES.tarots.tally);
}

static bool ev_dec_stop_use(Event *e, void *p) {
    (void)e;
    int depth = (int)(intptr_t)p;
    dec_stop_use(depth - 1);
    return true;
}
static bool ev_dec_stop_use_final(Event *e, void *p) {
    (void)e; (void)p;
    G.GAME.STOP_USE = MAX(G.GAME.STOP_USE - 1, 0);
    return true;
}
static void nofree(void *p) { (void)p; }
void stop_use(void) {
    G.GAME.STOP_USE = G.GAME.STOP_USE + 1;
    dec_stop_use(6);
}
void dec_stop_use(int depth) {
    if (depth > 0) add_event(EVENT(.blocking = 0, .no_delete = true, .func = ev_dec_stop_use, .ctx = (void *)(intptr_t)depth, .ctx_free = nofree));
    else add_event(EVENT(.blocking = 0, .no_delete = true, .func = ev_dec_stop_use_final));
}

void inc_career_stat(const char *stat, double mod) {
    if (G.GAME.seeded || G.GAME.challenge) return;
    DVal *cs = dv_get_or_create_table(profile(), "career_stats");
    dv_set_num(cs, stat, dv_get_num(cs, stat, 0) + mod);
    game_save_settings();
}

/* ------------------------------------------------------------------ */
/* save_run                                                            */
/* ------------------------------------------------------------------ */
static const char *AREA_NAMES[] = { "consumeables", "jokers", "discard", "deck", "hand", "play", "title_top",
                                    "shop_jokers", "shop_vouchers", "shop_booster", "pack_cards" };
CardArea **cardarea_slot(const char *name) {
    if (!name) return NULL;
    if (!strcmp(name, "consumeables")) return &G.consumeables;
    if (!strcmp(name, "jokers")) return &G.jokers;
    if (!strcmp(name, "discard")) return &G.discard;
    if (!strcmp(name, "deck")) return &G.deck;
    if (!strcmp(name, "hand")) return &G.hand;
    if (!strcmp(name, "play")) return &G.play;
    if (!strcmp(name, "title_top")) return &G.title_top;
    if (!strcmp(name, "shop_jokers")) return &G.shop_jokers;
    if (!strcmp(name, "shop_vouchers")) return &G.shop_vouchers;
    if (!strcmp(name, "shop_booster")) return &G.shop_booster;
    if (!strcmp(name, "pack_cards")) return &G.pack_cards;
    return NULL;
}
CardArea *cardarea_by_name(const char *name) {
    CardArea **s = cardarea_slot(name);
    return s ? *s : NULL;
}

void save_with_action(int card_sort_id) {
    G.has_action = true;
    G.action_card = card_sort_id;
    save_run();
    G.has_action = false;
}

void save_run(void) {
    if (G.F_NO_SAVING) return;
    DVal run = dv_table();
    DVal areas = dv_table();
    for (int i = 0; i < ARRLEN(AREA_NAMES); i++) {
        CardArea *a = cardarea_by_name(AREA_NAMES[i]);
        if (a && !a->n.REMOVED) dv_set(&areas, AREA_NAMES[i], CardArea_save(a));
    }
    dv_set(&run, "cardAreas", areas);
    DVal tags = dv_table();
    for (int i = 0; i < G.GAME.tags.count; i++) dv_seti(&tags, i + 1, Tag_save(G.GAME.tags.items[i]));
    dv_set(&run, "tags", tags);
    dv_set(&run, "GAME", gamestate_save(&G.GAME));
    dv_set_num(&run, "STATE", G.STATE);
    if (G.has_action) {
        DVal a = dv_table();
        dv_set_str(&a, "type", "use_card");
        dv_set_num(&a, "card", G.action_card);
        dv_set(&run, "ACTION", a);
    }
    dv_set(&run, "BLIND", Blind_save(G.GAME.blind));
    dv_set(&run, "BACK", Back_save(G.GAME.selected_back));
    dv_set_str(&run, "VERSION", G.VERSION);
    dv_free(&G.ARGS_save_run);
    G.ARGS_save_run = run;
    G.FILE_HANDLER.present = true;
    G.FILE_HANDLER.run = true;
    G.FILE_HANDLER.update_queued = true;
}

void remove_save(void) {
    file_remove(tfmt("%s/%d/save.jkr", save_dir(), G.SETTINGS.profile));
    dv_free(&G.SAVED_GAME);
    G.FILE_HANDLER.run = false;
}

/* ------------------------------------------------------------------ */
/* misc                                                                */
/* ------------------------------------------------------------------ */
typedef struct { int dummy; } NoCtx;
static bool ev_alert_sound(Event *e, void *p) { (void)e; (void)p; sound_play("tarot2", 0.76, 0.4); return true; }
static bool ev_alert_unlock(Event *e, void *p) { (void)e; (void)p; G.CONTROLLER.locks.no_space = false; return true; }
void alert_no_space(Card *card, CardArea *area) {
    G.CONTROLLER.locks.no_space = true;
    ATT(.scale = 0.9, .text = localize_key("k_no_space_ex"), .hold = 0.9, .align = "cm",
        .cover = &area->n, .cover_padding = 0.1, .cover_colour = adjust_alpha(G.C.BLACK, 0.7));
    Card_juice_up(&card->n, 0.3, 0.2);
    for (int i = 0; i < area->cards.count; i++) Card_juice_up(&area->cards.items[i]->n, 0.15, NAN);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.06 * G.SETTINGS.GAMESPEED, .blockable = 0, .blocking = 0, .func = ev_alert_sound));
    sound_play("tarot2", 1, 0.4);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.5 * G.SETTINGS.GAMESPEED, .blockable = 0, .blocking = 0, .func = ev_alert_unlock));
}

void playing_card_joker_effects(CardList *cards, int count) {
    for (int i = 0; i < G.jokers->cards.count; i++)
        Card_calculate_joker(G.jokers->cards.items[i], JCTX(.playing_card_added = true, .cards = cards, .ncards_added = count));
}

void convert_save_to_meta(void) {
    /* The very old per-file unlock lists are not produced by any released
     * version of the C port, so there is nothing to convert. */
}

void card_from_control(CardProto *control) {
    G.playing_card = G.playing_card ? G.playing_card + 1 : 1;
    CardParams params = { .playing_card = G.playing_card };
    Card *c = card_new(G.deck->n.T->x, G.deck->n.T->y, G.CARD_W, G.CARD_H, P_CARD(tfmt("%s_%s", control->s, control->r)),
                       P_CENTER(control->e ? control->e : "c_base"), &params);
    if (control->d) Card_set_edition(c, edition_req_from_key(control->d), true, true);
    if (control->g) Card_set_seal(c, control->g, true, true);
    CardArea_emplace(G.deck, c, NULL, false);
    vec_push(&G.playing_cards, c);
}

static void stake8_draw(Sprite *s) {
    s->send_to_shader[0] = (float)(MIN(s->n.VT.r * 3, 1) + G.TIMERS.REAL / 18 + (s->n.juice.active ? s->n.juice.r * 20 : 0) + 1);
    s->send_to_shader[1] = (float)G.TIMERS.REAL;
    Sprite_draw_shader_simple(s, "dissolve", 0, false);
    Sprite_draw_shader_send(s, "voucher", s->send_to_shader, 2);
}

Sprite *get_stake_sprite(int stake, double scale) {
    if (stake < 1) stake = 1;
    if (isnan(scale)) scale = 1;
    Sprite *s = sprite_new(0, 0, scale, scale, atlas_get("chips"), G.P_CENTER_POOLS.Stake.items[stake - 1]->pos);
    s->n.states.drag.can = false;
    if (stake == 8) s->custom_draw = stake8_draw;
    return s;
}

void get_front_spriteinfo(PCard *front, Atlas **atlas, SpritePos *pos) {
    bool cb = G.SETTINGS.colourblind_option;
    if (front && front->suit && front->value && (!strcmp(front->value, "Jack") || !strcmp(front->value, "Queen") || !strcmp(front->value, "King"))) {
        const char *collab = NULL;
        if (!strcmp(front->suit, "Spades")) collab = G.SETTINGS.CUSTOM_DECK_Collabs.Spades;
        else if (!strcmp(front->suit, "Hearts")) collab = G.SETTINGS.CUSTOM_DECK_Collabs.Hearts;
        else if (!strcmp(front->suit, "Clubs")) collab = G.SETTINGS.CUSTOM_DECK_Collabs.Clubs;
        else if (!strcmp(front->suit, "Diamonds")) collab = G.SETTINGS.CUSTOM_DECK_Collabs.Diamonds;
        if (collab && strcmp(collab, "default") != 0) {
            Atlas *a = atlas_get(tfmt("%s_%d", collab, cb ? 2 : 1));
            if (a) {
                *atlas = a;
                if (!strcmp(front->value, "Jack")) *pos = SPOS(0, 0);
                else if (!strcmp(front->value, "Queen")) *pos = SPOS(1, 0);
                else *pos = SPOS(2, 0);
                return;
            }
        }
    }
    Atlas *a = front && front->atlas ? atlas_get(front->atlas) : NULL;
    *atlas = a ? a : atlas_get(cb ? "cards_2" : "cards_1");
    *pos = front ? front->pos : SPOS(0, 0);
}

Col get_stake_col(int stake) {
    if (!G.C.STAKES[1]) {
        G.C.STAKES[1] = G.C.WHITE; G.C.STAKES[2] = G.C.RED; G.C.STAKES[3] = G.C.GREEN; G.C.STAKES[4] = G.C.BLACK;
        G.C.STAKES[5] = G.C.BLUE; G.C.STAKES[6] = G.C.PURPLE; G.C.STAKES[7] = G.C.ORANGE; G.C.STAKES[8] = G.C.GOLD;
    }
    if (stake < 1 || stake > 8) return NULL;
    return G.C.STAKES[stake];
}

int get_challenge_int_from_id(const char *id) {
    if (!id) return 0;
    for (int i = 0; i < G_N_CHALLENGES; i++) if (str_eq(G_CHALLENGES[i].id, id)) return i + 1;
    return 0;
}
Challenge *challenge_by_id(const char *id) {
    int i = get_challenge_int_from_id(id);
    return i ? &G_CHALLENGES[i - 1] : NULL;
}

StartingParams get_starting_params(void) {
    StartingParams p;
    memset(&p, 0, sizeof p);
    p.dollars = 4; p.hand_size = 8; p.discards = 3; p.hands = 4; p.reroll_cost = 5;
    p.joker_slots = 5; p.ante_scaling = 1; p.consumable_slots = 2;
    return p;
}

ChallengeRule *get_challenge_rule(Challenge *ch, const char *type, const char *id) {
    if (!ch) return NULL;
    ChallengeRule *list = !strcmp(type, "custom") ? ch->custom : ch->modifiers;
    int n = !strcmp(type, "custom") ? ch->ncustom : ch->nmodifiers;
    for (int i = 0; i < n; i++) if (str_eq(list[i].id, id)) return &list[i];
    return NULL;
}

EditionReq edition_req_from_key(const char *key) {
    EditionReq r = ED_NONE;
    if (!key) return r;
    if (!strcmp(key, "foil")) r = ED_FOIL;
    else if (!strcmp(key, "holo")) r = ED_HOLO;
    else if (!strcmp(key, "polychrome")) r = ED_POLY;
    else if (!strcmp(key, "negative")) r = ED_NEG;
    return r;
}

/* pseudorandom_element over a list of cards: sorted by sort_id, then math.random */
Card *pseudorandom_card(CardList *cards, double seed) {
    if (!cards || cards->count == 0) return NULL;
    if (!isnan(seed)) math_randomseed(seed);   /* NAN = no seed: keep the current RNG state */
    Card **sorted = xmalloc((size_t)cards->count * sizeof(Card *));
    memcpy(sorted, cards->items, (size_t)cards->count * sizeof(Card *));
    for (int i = 1; i < cards->count; i++) {
        Card *k = sorted[i]; int j = i - 1;
        while (j >= 0 && sorted[j]->sort_id > k->sort_id) { sorted[j + 1] = sorted[j]; j--; }
        sorted[j + 1] = k;
    }
    Card *r = sorted[math_random_range(1, cards->count) - 1];
    free(sorted);
    return r;
}

static int pcard_key_cmp(const void *a, const void *b) {
    return strcmp((*(PCard *const *)a)->key, (*(PCard *const *)b)->key);
}
/* pseudorandom_element(G.P_CARDS, seed): keys sorted by string compare */
PCard *pseudorandom_p_card(double seed) {
    static PCard **sorted;
    if (!sorted) {
        sorted = xcalloc((size_t)G.n_cards, sizeof(PCard *));
        for (int i = 0; i < G.n_cards; i++) sorted[i] = &G.P_CARDS_LIST[i];
        qsort(sorted, (size_t)G.n_cards, sizeof(PCard *), pcard_key_cmp);
    }
    if (!isnan(seed)) math_randomseed(seed);
    return sorted[math_random_range(1, G.n_cards) - 1];
}

char *get_compressed_text(const char *rel) {
    int len;
    char *raw = file_read_all(res_save_path(rel), &len);
    if (!raw) return NULL;
    if (len == 0) { free(raw); return NULL; }
    if (len >= 6 && strncmp(raw, "return", 6) == 0) return raw;
    int dl;
    char *dec = deflate_decompress(raw, len, &dl);
    free(raw);
    return dec;
}

EditionReq edition_req_from_edition(const Edition *e) {
    EditionReq r = ED_NONE;
    if (!e) return r;
    if (e->holo) r = ED_HOLO;
    else if (e->foil) r = ED_FOIL;
    else if (e->polychrome) r = ED_POLY;
    else if (e->negative) r = ED_NEG;
    return r;
}

int rank_order_index(const char *value) {
    static const char *names[13] = { "Ace", "2", "3", "4", "5", "6", "7", "8", "9", "10", "Jack", "Queen", "King" };
    if (!value) return -1;
    for (int i = 0; i < 13; i++) if (!strcmp(value, names[i])) return i;
    return -1;
}
