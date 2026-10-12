/* common_events2.c - port of functions/common_events.lua (part 2:
 * unlocks, achievements, discovery, card creation and pools, tutorial
 * info, round resets, generate_card_ui). */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define LOC(k) ((char *)localize_key(k))

static double career(const char *stat) { return dv_get_num(dv_get(profile(), "career_stats"), stat, 0); }
static int used_voucher_count(void) {
    int n = 0;
    for (int i = 0; i < G.GAME.used_vouchers.cap; i++) if (NUMMAP_LIVE(&G.GAME.used_vouchers, i)) n++;
    return n;
}
static bool has_showman(void) { return has_joker("Showman"); }

/* ------------------------------------------------------------------ */
/* check_for_unlock                                                    */
/* ------------------------------------------------------------------ */
typedef struct { Center *card; } UnlockCtx;
EVFN(ev_unlock_card2) { UnlockCtx *c = p_; (void)e_; unlock_card(c->card); return true; }
EVFN(ev_unlock_card1) {
    UnlockCtx *c = p_; (void)e_;
    UnlockCtx *n = CTX(UnlockCtx); *n = *c;
    add_event(EVENT(.func = ev_unlock_card2, .ctx = n));
    return true;
}

void check_for_unlock(UnlockArgs a) {
    if (!a.type) return;
    if (G.GAME.seeded) return;
    const char *t = a.type;
    if (!strcmp(t, "win_challenge")) {
        unlock_achievement("rule_bender");
        bool all = true;
        DVal *done = dv_get(dv_get(profile(), "challenge_progress"), "completed");
        for (int i = 0; i < G_N_CHALLENGES; i++) if (!dv_truthy(dv_get(done, G_CHALLENGES[i].id))) all = false;
        if (all) unlock_achievement("rule_breaker");
    }
    if (G.GAME.challenge) return;

    /* achievements */
    if (!strcmp(t, "career_stat")) {
        if (str_eq(a.statname, "c_cards_played") && career(a.statname) >= 2500) unlock_achievement("card_player");
        if (str_eq(a.statname, "c_cards_discarded") && career(a.statname) >= 2500) unlock_achievement("card_discarder");
    }
    if (!strcmp(t, "ante_up")) {
        if (a.ante >= 4) unlock_achievement("ante_up");
        if (a.ante >= 8) unlock_achievement("ante_upper");
    }
    if (!strcmp(t, "win")) {
        unlock_achievement("heads_up");
        if (G.GAME.round <= 12) unlock_achievement("speedrunner");
        if (G.GAME.round_scores.times_rerolled.amt <= 0) unlock_achievement("you_get_what_you_get");
    }
    if (!strcmp(t, "win_stake")) {
        double hi, lo;
        get_deck_win_stake_all(&hi, &lo);
        if (hi >= 2) unlock_achievement("low_stakes");
        if (hi >= 4) unlock_achievement("mid_stakes");
        if (hi >= 8) unlock_achievement("high_stakes");
        if (G.PROGRESS.present && G.PROGRESS.deck_stakes.of > 0 && G.PROGRESS.deck_stakes.tally / G.PROGRESS.deck_stakes.of >= 1) unlock_achievement("completionist_plus");
        if (G.PROGRESS.present && G.PROGRESS.joker_stickers.of > 0 && G.PROGRESS.joker_stickers.tally / G.PROGRESS.joker_stickers.of >= 1)
            unlock_achievement("completionist_plus_plus");
    }
    if (!strcmp(t, "money") && G.GAME.dollars >= 400) unlock_achievement("nest_egg");
    if (!strcmp(t, "hand")) {
        if (str_eq(a.handname, "Flush") && a.scoring_hand) {
            int w = 0;
            for (int i = 0; i < a.scoring_hand->count; i++) if (str_eq(a.scoring_hand->items[i]->ability.name, "Wild Card")) w++;
            if (w == a.scoring_hand->count) unlock_achievement("flushed");
        }
        if (str_eq(a.disp_text, "Royal Flush")) unlock_achievement("royale");
    }
    if (!strcmp(t, "shatter") && a.shattered && a.shattered->count >= 2) unlock_achievement("shattered");
    if (!strcmp(t, "run_redeem")) {
        double v = -G.GAME.starting_voucher_count + used_voucher_count();
        if (v >= 5 && G.GAME.round_resets.ante <= 4) unlock_achievement("roi");
    }
    if (!strcmp(t, "upgrade_hand") && a.level >= 10) unlock_achievement("retrograde");
    if (!strcmp(t, "chip_score")) {
        if (a.chips >= 10000) unlock_achievement("_10k");
        if (a.chips >= 1000000) unlock_achievement("_1000k");
        if (a.chips >= 100000000) unlock_achievement("_100000k");
    }
    if (!strcmp(t, "modify_deck")) {
        if (G.deck && G.deck->config.card_limit <= 20) unlock_achievement("tiny_hands");
        if (G.deck && G.deck->config.card_limit >= 80) unlock_achievement("big_hands");
    }
    if (!strcmp(t, "spawn_legendary")) unlock_achievement("legendary");
    if (!strcmp(t, "discover_amount")) {
#define FULL(x) (G.DISCOVER_TALLIES.x.of > 0 && G.DISCOVER_TALLIES.x.tally / G.DISCOVER_TALLIES.x.of >= 1)
        if (FULL(vouchers)) unlock_achievement("extreme_couponer");
        if (FULL(spectrals)) unlock_achievement("clairvoyance");
        if (FULL(tarots)) unlock_achievement("cartomancy");
        if (FULL(planets)) unlock_achievement("astronomy");
        if (FULL(total)) unlock_achievement("completionist");
#undef FULL
    }

    int i = 0;
    while (i < G.P_LOCKED.count) {
        bool ret = false;
        Center *card = G.P_LOCKED.items[i];
        UnlockCondition *uc = &card->unlock_condition;
        if (!card->unlocked && uc->present && !strcmp(t, "career_stat")) {
            if (str_eq(a.statname, uc->type) && career(a.statname) >= uc->extra_num) { ret = true; unlock_card(card); }
        }
        if (!card->unlocked && uc->present && str_eq(uc->type, t)) {
            if (!strcmp(t, "hand") && str_eq(a.handname, uc->extra_str)) { ret = true; unlock_card(card); }
            if (!strcmp(t, "min_hand_size") && G.hand && G.hand->config.card_limit <= uc->extra_num) { ret = true; unlock_card(card); }
            if (!strcmp(t, "interest_streak") && uc->extra_num <= career("c_round_interest_cap_streak")) { ret = true; unlock_card(card); }
            if (!strcmp(t, "run_card_replays")) {
                for (int k = 0; k < G.playing_cards.count; k++)
                    if (((Card *)G.playing_cards.items[k])->base.times_played >= uc->extra_num) { ret = true; unlock_card(card); break; }
            }
            if (!strcmp(t, "play_all_hearts")) {
                bool played = true;
                for (int k = 0; k < G.deck->cards.count; k++) {
                    Card *v = G.deck->cards.items[k];
                    if (!str_eq(v->ability.name, "Stone Card") && str_eq(v->base.suit, "Hearts")) played = false;
                }
                for (int k = 0; k < G.hand->cards.count; k++) {
                    Card *v = G.hand->cards.items[k];
                    if (!str_eq(v->ability.name, "Stone Card") && str_eq(v->base.suit, "Hearts")) played = false;
                }
                if (played) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "run_redeem") && used_voucher_count() >= uc->extra_num) { ret = true; unlock_card(card); }
            if (!strcmp(t, "have_edition")) {
                int shiny = 0;
                for (int k = 0; k < G.jokers->cards.count; k++) if (G.jokers->cards.items[k]->edition) shiny++;
                if (shiny >= uc->extra_num) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "double_gold")) { ret = true; unlock_card(card); }
            if (!strcmp(t, "continue_game")) { ret = true; unlock_card(card); }
            if (!strcmp(t, "blank_redeems")) {
                DVal *vb = dv_get(dv_get(profile(), "voucher_usage"), "v_blank");
                if (vb && dv_get_num(vb, "count", 0) >= uc->extra_num) unlock_card(card);
            }
            if (!strcmp(t, "modify_deck") && uc->extra_kind == EX_TABLE) {
                if (uc->suit) {
                    int count = 0;
                    for (int k = 0; k < G.playing_cards.count; k++) if (str_eq(((Card *)G.playing_cards.items[k])->base.suit, uc->suit)) count++;
                    if (count >= uc->count) { ret = true; unlock_card(card); }
                }
                if (uc->enhancement) {
                    int count = 0;
                    for (int k = 0; k < G.playing_cards.count; k++) if (str_eq(((Card *)G.playing_cards.items[k])->ability.name, uc->enhancement)) count++;
                    if (count >= uc->count) { ret = true; unlock_card(card); }
                }
                if (uc->tally) {
                    int count = 0;
                    for (int k = 0; k < G.playing_cards.count; k++) if (str_eq(((Card *)G.playing_cards.items[k])->ability.set, "Enhanced")) count++;
                    if (count >= uc->count) { ret = true; unlock_card(card); }
                }
            }
            if (!strcmp(t, "discover_amount")) {
                if (uc->amount && uc->amount <= a.amount) { ret = true; unlock_card(card); }
                if (uc->tarot_count && uc->tarot_count <= a.tarot_count) { ret = true; unlock_card(card); }
                if (uc->planet_count && uc->planet_count <= a.planet_count) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "win_deck") && uc->deck && get_deck_win_stake(uc->deck) > 0) { ret = true; unlock_card(card); }
            if (!strcmp(t, "win_stake") && uc->stake) {
                double hi, lo;
                get_deck_win_stake_all(&hi, &lo);
                if (hi >= uc->stake) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "discover_planets")) {
                int count = 0;
                for (int k = 0; k < G.n_centers; k++) if (str_eq(G.P_CENTERS_LIST[k].set, "Planet") && G.P_CENTERS_LIST[k].discovered) count++;
                if (count >= 9) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "blind_discoveries")) {
                int db = 0;
                for (int k = 0; k < G.n_blinds; k++) if (G.P_BLINDS_LIST[k].discovered) db++;
                if (db >= uc->extra_num) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "modify_jokers") && G.jokers && uc->count) {
                int count = 0;
                for (int k = 0; k < G.jokers->cards.count; k++) {
                    Card *v = G.jokers->cards.items[k];
                    if (str_eq(v->ability.set, "Joker") && v->edition && v->edition->polychrome && uc->polychrome) count++;
                }
                if (count >= uc->count) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "money") && uc->extra_num <= G.GAME.dollars) { ret = true; unlock_card(card); }
            if (!strcmp(t, "round_win")) {
                if (str_eq(card->name, "Matador")) {
                    if (G.GAME.current_round.hands_played == 1 && G.GAME.current_round.discards_left == G.GAME.round_resets.discards &&
                        str_eq(Blind_get_type(G.GAME.blind), "Boss")) { ret = true; unlock_card(card); }
                }
                if (str_eq(card->name, "Troubadour") && career("c_single_hand_round_streak") >= uc->extra_num) { ret = true; unlock_card(card); }
                if (str_eq(card->name, "Hanging Chad") && str_eq(G.GAME.last_hand_played, uc->extra_str) && str_eq(Blind_get_type(G.GAME.blind), "Boss")) {
                    ret = true; unlock_card(card);
                }
            }
            if (!strcmp(t, "ante_up") && uc->ante && a.ante == uc->ante) { ret = true; unlock_card(card); }
            if (!strcmp(t, "hand_contents") && a.cards) {
                if (str_eq(card->name, "Seeing Double")) {
                    int tally = 0;
                    for (int j = 0; j < a.cards->count; j++)
                        if (Card_get_id(a.cards->items[j]) == 7 && Card_is_suit(a.cards->items[j], "Clubs", false, false)) tally++;
                    if (tally >= 4) { ret = true; unlock_card(card); }
                }
                if (str_eq(card->name, "Golden Ticket")) {
                    int tally = 0;
                    for (int j = 0; j < a.cards->count; j++) if (str_eq(a.cards->items[j]->ability.name, "Gold Card")) tally++;
                    if (tally >= 5) { ret = true; unlock_card(card); }
                }
            }
            if (!strcmp(t, "discard_custom") && a.cards) {
                if (str_eq(card->name, "Hit the Road")) {
                    int tally = 0;
                    for (int j = 0; j < a.cards->count; j++) if (Card_get_id(a.cards->items[j]) == 11) tally++;
                    if (tally >= 5) { ret = true; unlock_card(card); }
                }
                if (str_eq(card->name, "Brainstorm")) {
                    PokerHands *ev = evaluate_poker_hand(a.cards);
                    if (poker_hand_has(ev, "Straight Flush")) {
                        int min = 10;
                        for (int j = 0; j < a.cards->count; j++) if (Card_get_id(a.cards->items[j]) < min) min = Card_get_id(a.cards->items[j]);
                        if (min == 10) { ret = true; unlock_card(card); }
                    }
                }
            }
            if (!strcmp(t, "win_no_hand")) {
                int hi = hand_index(uc->extra_str);
                if (hi >= 0 && G.GAME.hands[hi].played == 0) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "win_custom")) {
                if (str_eq(card->name, "Invisible Joker") && G.GAME.max_jokers <= 4) { ret = true; unlock_card(card); }
                if (str_eq(card->name, "Blueprint")) { ret = true; unlock_card(card); }
            }
            if (!strcmp(t, "win") && uc->n_rounds >= G.GAME.round) { ret = true; unlock_card(card); }
            if (!strcmp(t, "chip_score") && uc->chips <= a.chips) {
                ret = true;
                UnlockCtx *c = CTX(UnlockCtx); c->card = card;
                add_event(EVENT(.func = ev_unlock_card1, .ctx = c));
            }
        }
        if (ret) vec_remove_at(&G.P_LOCKED, i);
        else i++;
    }
}

static int back_order_cmp(const void *a, const void *b) {
    const Center *x = *(Center *const *)a, *y = *(Center *const *)b;
    double ox = x->order - (x->unlocked ? 100 : 0), oy = y->order - (y->unlocked ? 100 : 0);
    return ox < oy ? -1 : ox > oy ? 1 : 0;
}

void unlock_card(Center *card) {
    if (!card || card->unlocked) return;
    if (G.GAME.seeded || G.GAME.challenge) return;
    if (card->wip) return;
    game_save_notify(card);
    card->unlocked = true;
    if (str_eq(card->set, "Back")) discover_card(card);
    qsort(G.P_CENTER_POOLS.Back.items, (size_t)G.P_CENTER_POOLS.Back.count, sizeof(Center *), back_order_cmp);
    game_save_progress();
    G.FILE_HANDLER.force = true;
    notify_alert(card->key, card->set);
}

/* ------------------------------------------------------------------ */
/* achievements (local settings file only; no Steam)                   */
/* ------------------------------------------------------------------ */
typedef struct { const char *name; int order, tier; bool earned; } Achievement;
static Achievement ACH[] = {
    { "ante_up", 1, 3 }, { "ante_upper", 2, 3 }, { "heads_up", 3, 2 }, { "low_stakes", 4, 2 }, { "mid_stakes", 5, 2 }, { "high_stakes", 6, 2 },
    { "card_player", 7, 3 }, { "card_discarder", 8, 3 }, { "nest_egg", 9, 2 }, { "flushed", 10, 3 }, { "speedrunner", 11, 2 }, { "roi", 12, 3 },
    { "shattered", 13, 3 }, { "royale", 14, 3 }, { "retrograde", 15, 2 }, { "_10k", 16, 3 }, { "_1000k", 17, 2 }, { "_100000k", 18, 1 },
    { "tiny_hands", 19, 2 }, { "big_hands", 20, 2 }, { "you_get_what_you_get", 21, 3 }, { "rule_bender", 22, 3 }, { "rule_breaker", 23, 1 },
    { "legendary", 24, 3 }, { "astronomy", 25, 3 }, { "cartomancy", 26, 3 }, { "clairvoyance", 27, 2 }, { "extreme_couponer", 28, 1 },
    { "completionist", 29, 1 }, { "completionist_plus", 30, 1 }, { "completionist_plus_plus", 31, 1 },
};
static bool ach_fetched;
static Achievement *ach_find(const char *n) {
    for (size_t i = 0; i < sizeof ACH / sizeof *ACH; i++) if (str_eq(ACH[i].name, n)) return &ACH[i];
    return NULL;
}

void fetch_achievements(void) {
    ach_fetched = true;
    if (G.F_NO_ACHIEVEMENTS) return;
    if (G.SETTINGS.ACHIEVEMENTS_EARNED.t != DV_TABLE) G.SETTINGS.ACHIEVEMENTS_EARNED = dv_table();
    DVal *e = &G.SETTINGS.ACHIEVEMENTS_EARNED;
    for (int i = 0; i < dv_count(e); i++) {
        const DKey *k = dv_key(e, i);
        if (k->is_str) { Achievement *a = ach_find(k->s); if (a) a->earned = true; }
    }
}

typedef struct { const char *name; } AchCtx;
EVFN(ev_unlock_achievement) {
    AchCtx *c = p_; (void)e_;
    if (G.STATE == STATE_HAND_PLAYED) return false;
    if (dv_get_bool(profile(), "all_unlocked", false)) return false;
    if (G.F_NO_ACHIEVEMENTS) return false;
    if (!ach_fetched) fetch_achievements();
    if (G.SETTINGS.ACHIEVEMENTS_EARNED.t != DV_TABLE) G.SETTINGS.ACHIEVEMENTS_EARNED = dv_table();
    dv_set_bool(&G.SETTINGS.ACHIEVEMENTS_EARNED, c->name, true);
    game_save_progress();
    bool set = false;
    Achievement *a = ach_find(c->name);
    if (a) {
        if (!a->earned) { set = true; G.FILE_HANDLER.force = true; }
        a->earned = true;
    }
    if (set) notify_alert(c->name, NULL);
    return true;
}

void unlock_achievement(const char *name) {
    if (dv_get_bool(profile(), "all_unlocked", false)) return;
    AchCtx *c = CTX(AchCtx); c->name = intern(name);
    add_event_q(EVENT(.no_delete = true, .blockable = 0, .blocking = 0, .func = ev_unlock_achievement, .ctx = c), Q_ACHIEVEMENT, false);
}

typedef struct { const char *key, *type; } NotifyCtx;
EVFN(ev_notify1) {
    NotifyCtx *c = p_; (void)e_;
    if (G.achievement_notification) { node_remove(&G.achievement_notification->n); G.achievement_notification = NULL; }
    G.achievement_notification = uibox_new(create_UIBox_notify_alert(c->key, c->type),
                                           UIBCFG(.align = "cr", .has_offset = true, .offset = { 20, 0 }, .major = G.ROOM_ATTACH, .bond = BOND_WEAK));
    return true;
}
EVFN(ev_notify2) {
    (void)e_; (void)p_;
    UIBox *b = G.achievement_notification;
    if (b) {
        UIElement *c1 = uie_child(b->UIRoot, 1);
        UIElement *c2 = c1 ? uie_child(c1, 1) : NULL;
        b->n.alignment.offset.x = G.ROOM->T->x - (c2 ? c2->n.T->w : 0) - 0.8;
    }
    return true;
}
EVFN(ev_notify3) { (void)e_; (void)p_; sound_play("highlight1", NAN, 0.5); sound_play("foil2", 0.5, 0.4); return true; }
EVFN(ev_notify4) { (void)e_; (void)p_; if (G.achievement_notification) G.achievement_notification->n.alignment.offset.x = 20; return true; }
EVFN(ev_notify5) {
    (void)e_; (void)p_;
    if (G.achievement_notification) { node_remove(&G.achievement_notification->n); G.achievement_notification = NULL; }
    return true;
}

void notify_alert(const char *key, const char *type) {
    NotifyCtx *c = CTX(NotifyCtx); c->key = intern(key); c->type = intern(type ? type : "achievement");
    add_event_q(EVENT(.no_delete = true, .pause_force = true, .timer = TIMER_UPTIME, .func = ev_notify1, .ctx = c), Q_ACHIEVEMENT, false);
    add_event_q(EVENT(.no_delete = true, .trigger = TRIG_AFTER, .pause_force = true, .timer = TIMER_UPTIME, .delay = 0.1, .func = ev_notify2), Q_ACHIEVEMENT, false);
    add_event_q(EVENT(.no_delete = true, .pause_force = true, .trigger = TRIG_AFTER, .timer = TIMER_UPTIME, .delay = 0.1, .func = ev_notify3), Q_ACHIEVEMENT, false);
    add_event_q(EVENT(.no_delete = true, .pause_force = true, .trigger = TRIG_AFTER, .delay = 3, .timer = TIMER_UPTIME, .func = ev_notify4), Q_ACHIEVEMENT, false);
    add_event_q(EVENT(.no_delete = true, .pause_force = true, .trigger = TRIG_AFTER, .delay = 0.5, .timer = TIMER_UPTIME, .func = ev_notify5), Q_ACHIEVEMENT, false);
}

void inc_steam_stat(const char *stat) { (void)stat; /* no Steam */ }

void unlock_notify(void) {
    const char *rel = tfmt("%d/unlock_notify.jkr", G.SETTINGS.profile);
    char *un = get_compressed_text(rel);
    if (!un) return;
    char *p = un;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p) create_unlock_overlay(p);
        if (!nl) break;
        p = nl + 1;
    }
    free(un);
    remove(res_save_path(rel));
}

typedef struct { Center *c; } OverlayCtx;
EVFN(ev_unlock_overlay) {
    OverlayCtx *c = p_; (void)e_;
    if (G.OVERLAY_MENU) return false;
    G.SETTINGS.paused = true;
    overlay_menu(str_eq(c->c->set, "Back") ? create_UIBox_deck_unlock(c->c) : create_UIBox_card_unlock(c->c), (OverlayArgs){ 0 });
    sound_play("foil1", 0.7, 0.3);
    sound_play("gong", 1.4, 0.15);
    return true;
}
UIDef *create_unlock_overlay(const char *key) {
    Center *c = P_CENTER(key);
    if (c) {
        OverlayCtx *o = CTX(OverlayCtx); o->c = c;
        add_event_q(EVENT(.trigger = TRIG_IMMEDIATE, .no_delete = true, .func = ev_unlock_overlay, .ctx = o), Q_UNLOCK, false);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* discovery                                                           */
/* ------------------------------------------------------------------ */
EVFN(ev_save_progress) { (void)e_; (void)p_; game_save_progress(); return true; }

static bool discover_common(bool *discovered, bool *alerted, bool wip) {
    if (G.GAME.seeded || G.GAME.challenge) return false;
    if (*discovered || wip) return false;
    (void)alerted;
    G.GAME.round_scores.new_collection.amt += 1;
    *discovered = true;
    set_discover_tallies();
    add_event(EVENT(.func = ev_save_progress));
    return true;
}
void discover_card(Center *card) { if (card) discover_common(&card->discovered, &card->alerted, card->wip); }
void discover_tag(TagProto *t) { if (t) discover_common(&t->discovered, &t->alerted, false); }
void discover_blind(BlindProto *b) { if (b) discover_common(&b->discovered, &b->alerted, false); }

Center *get_deck_from_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < G.n_centers; i++) if (str_eq(G.P_CENTERS_LIST[i].name, name)) return &G.P_CENTERS_LIST[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* pools                                                               */
/* ------------------------------------------------------------------ */
static const char *pool_pick(PoolResult *p, const char *pool_key) {
    int idx = pseudorandom_index(p->n, pseudoseed(pool_key), true);
    const char *c = idx > 0 ? p->keys[idx - 1] : NULL;
    int it = 1;
    while (str_eq(c, "UNAVAILABLE")) {
        it++;
        idx = pseudorandom_index(p->n, pseudoseed(tfmt("%s_resample%d", pool_key, it)), true);
        c = p->keys[idx - 1];
    }
    return c;
}

const char *get_next_voucher_key(bool from_tag) {
    PoolResult p = get_current_pool("Voucher", RARITY_NIL, false, NULL);
    const char *key = from_tag ? "Voucher_fromtag" : p.pool_key;
    return pool_pick(&p, key);
}

const char *get_next_tag_key(const char *append) {
    PoolResult p = get_current_pool("Tag", RARITY_NIL, false, append);
    return pool_pick(&p, p.pool_key);
}

Card *create_playing_card(PlayingCardInit init, CardArea *area, bool skip_materialize, bool silent, Col *colours, int ncolours) {
    if (!init.front) init.front = pseudorandom_p_card(pseudoseed("front"));
    if (!init.center) init.center = P_CENTER("c_base");
    G.playing_card = G.playing_card + 1;
    CardArea *a = area ? area : G.hand;
    Card *card = card_new(a->n.T->x, a->n.T->y, G.CARD_W, G.CARD_H, init.front, init.center, &(CardParams){ .playing_card = G.playing_card });
    vec_push(&G.playing_cards, card);
    card->playing_card = G.playing_card;
    if (area) CardArea_emplace(area, card, NULL, false);
    if (!skip_materialize) Card_start_materialize(card, colours, ncolours, silent, NAN);
    return card;
}

Center *get_pack(const char *key, const char *type) {
    if (!G.GAME.first_shop_buffoon && !strset_has(&G.GAME.banned_keys, "p_buffoon_normal_1")) {
        G.GAME.first_shop_buffoon = true;
        return P_CENTER(tfmt("p_buffoon_normal_%d", math_random_range(1, 2)));
    }
    double cume = 0, it = 0;
    Center *center = NULL;
    CenterVec *pool = &G.P_CENTER_POOLS.Booster;
    for (int i = 0; i < pool->count; i++) {
        Center *v = pool->items[i];
        if ((!type || str_eq(type, v->kind)) && !strset_has(&G.GAME.banned_keys, v->key)) cume += v->weight ? v->weight : 1;
    }
    double poll = pseudorandom_seed(pseudoseed(tfmt("%s%s", key ? key : "pack_generic", num_str(G.GAME.round_resets.ante)))) * cume;
    for (int i = 0; i < pool->count; i++) {
        Center *v = pool->items[i];
        if (!strset_has(&G.GAME.banned_keys, v->key)) {
            double w = v->weight ? v->weight : 1;
            if (!type || str_eq(type, v->kind)) it += w;
            if (it >= poll && it - w <= poll) { center = v; break; }
        }
    }
    return center;
}

PoolResult get_current_pool(const char *type, double rarity_in, bool legendary, const char *append) {
    static const char **pool;
    static int cap;
    PoolResult r = { 0 };
    int n = 0, size = 0;
    const char *pool_key;
    CenterVec *starting = NULL;
    bool is_tag = !strcmp(type, "Tag");
    if (!strcmp(type, "Joker")) {
        double rarity = !isnan(rarity_in) ? rarity_in : pseudorandom_seed(pseudoseed(tfmt("rarity%s%s", num_str(G.GAME.round_resets.ante), append ? append : "")));
        int ri = legendary ? 4 : rarity > 0.95 ? 3 : rarity > 0.7 ? 2 : 1;
        starting = &G.P_JOKER_RARITY_POOLS[ri - 1];
        pool_key = tfmt("Joker%d%s", ri, (!legendary && append) ? append : "");
    } else {
        pool_key = tfmt("%s%s", type, append ? append : "");
#define P(name) if (!strcmp(type, #name)) starting = &G.P_CENTER_POOLS.name
        P(Booster); P(Default); P(Enhanced); P(Edition); P(Joker); P(Tarot); P(Planet); P(Tarot_Planet); P(Spectral); P(Consumeables); P(Voucher); P(Back); P(Demo);
#undef P
    }
    char *pk = xstrdup(pool_key);
    int total = is_tag ? G.P_CENTER_POOLS.Tag.count : (starting ? starting->count : 0);
    if (total + 1 > cap) { cap = total + 16; pool = xrealloc(pool, (size_t)cap * sizeof(char *)); }
    bool showman = has_showman();
    for (int i = 0; i < total; i++) {
        bool add = false;
        const char *key;
        const char *no_flag = NULL, *yes_flag = NULL;
        if (is_tag) {
            TagProto *v = G.P_CENTER_POOLS.Tag.items[i];
            key = v->key;
            Center *req = v->requires ? P_CENTER(v->requires) : NULL;
            if ((!v->requires || (req && req->discovered)) && (!v->has_min_ante || v->min_ante <= G.GAME.round_resets.ante)) add = true;
        } else {
            Center *v = starting->items[i];
            key = v->key;
            no_flag = v->no_pool_flag; yes_flag = v->yes_pool_flag;
            if (!strcmp(type, "Enhanced")) add = true;
            else if (!strcmp(type, "Demo")) add = true;
            /* `v.unlocked ~= false`: a definition without an `unlocked` field counts as unlocked */
            else if (!(strset_has(&G.GAME.used_jokers, v->key) && !showman) && (!v->unlocked_set || v->unlocked || v->rarity == 4)) {
                if (str_eq(v->set, "Voucher")) {
                    if (!strset_has(&G.GAME.used_vouchers, v->key)) {
                        bool include = true;
                        for (int k = 0; k < v->n_requires; k++) if (!strset_has(&G.GAME.used_vouchers, v->requires[k])) include = false;
                        if (G.shop_vouchers)
                            for (int k = 0; k < G.shop_vouchers->cards.count; k++)
                                if (str_eq(G.shop_vouchers->cards.items[k]->config.center->key, v->key)) include = false;
                        if (include) add = true;
                    }
                } else if (str_eq(v->set, "Planet")) {
                    int hi = hand_index(v->config.hand_type);
                    if (!v->config.softlock || (hi >= 0 && G.GAME.hands[hi].played > 0)) add = true;
                } else if (v->enhancement_gate) {
                    add = false;
                    for (int k = 0; k < G.playing_cards.count; k++)
                        if (str_eq(((Card *)G.playing_cards.items[k])->config.center->key, v->enhancement_gate)) add = true;
                } else add = true;
                if (str_eq(v->name, "Black Hole") || str_eq(v->name, "The Soul")) add = false;
            }
        }
        if (no_flag && strset_has(&G.GAME.pool_flags, no_flag)) add = false;
        if (yes_flag && !strset_has(&G.GAME.pool_flags, yes_flag)) add = false;
        if (add && !strset_has(&G.GAME.banned_keys, key)) { pool[n++] = key; size++; }
        else pool[n++] = "UNAVAILABLE";
    }
    if (size == 0) {
        n = 0;
        if (!strcmp(type, "Tarot") || !strcmp(type, "Tarot_Planet")) pool[n++] = "c_strength";
        else if (!strcmp(type, "Planet")) pool[n++] = "c_pluto";
        else if (!strcmp(type, "Spectral")) pool[n++] = "c_incantation";
        else if (!strcmp(type, "Voucher")) pool[n++] = "v_blank";
        else if (is_tag) pool[n++] = "tag_handy";
        else pool[n++] = "j_joker";
    }
    r.keys = pool;
    r.n = n;
    r.pool_key = intern(tfmt("%s%s", pk, !legendary ? num_str(G.GAME.round_resets.ante) : ""));
    free(pk);
    return r;
}

EditionReq poll_edition(const char *key, double mod, bool no_neg, bool guaranteed) {
    if (isnan(mod)) mod = 1;
    double p = pseudorandom_seed(pseudoseed(key ? key : "edition_generic"));
    if (guaranteed) {
        if (p > 1 - 0.003 * 25 && !no_neg) return ED_NEG;
        if (p > 1 - 0.006 * 25) return ED_POLY;
        if (p > 1 - 0.02 * 25) return ED_HOLO;
        if (p > 1 - 0.04 * 25) return ED_FOIL;
    } else {
        if (p > 1 - 0.003 * mod && !no_neg) return ED_NEG;
        if (p > 1 - 0.006 * G.GAME.edition_rate * mod) return ED_POLY;
        if (p > 1 - 0.02 * G.GAME.edition_rate * mod) return ED_HOLO;
        if (p > 1 - 0.04 * G.GAME.edition_rate * mod) return ED_FOIL;
    }
    return ED_NONE;
}

Card *create_card(const char *type, CardArea *area, bool legendary, double rarity, bool skip_materialize, bool soulable, const char *forced_key,
                  const char *key_append) {
    if (!area) area = G.jokers;
    Center *center = P_CENTER("b_red");
    const char *ante = num_str(G.GAME.round_resets.ante);
    if (!forced_key && soulable && !strset_has(&G.GAME.banned_keys, "c_soul")) {
        if ((!strcmp(type, "Tarot") || !strcmp(type, "Spectral") || !strcmp(type, "Tarot_Planet")) &&
            !(strset_has(&G.GAME.used_jokers, "c_soul") && !has_showman())) {
            if (pseudorandom_seed(pseudoseed(tfmt("soul_%s%s", type, ante))) > 0.997) forced_key = "c_soul";
        }
        if ((!strcmp(type, "Planet") || !strcmp(type, "Spectral")) && !(strset_has(&G.GAME.used_jokers, "c_black_hole") && !has_showman())) {
            if (pseudorandom_seed(pseudoseed(tfmt("soul_%s%s", type, ante))) > 0.997) forced_key = "c_black_hole";
        }
    }
    if (!strcmp(type, "Base")) forced_key = "c_base";
    if (forced_key && !strset_has(&G.GAME.banned_keys, forced_key)) {
        center = P_CENTER(forced_key);
        type = !str_eq(center->set, "Default") ? center->set : type;
    } else {
        PoolResult p = get_current_pool(type, rarity, legendary, key_append);
        center = P_CENTER(pool_pick(&p, p.pool_key));
    }
    PCard *front = (!strcmp(type, "Base") || !strcmp(type, "Enhanced"))
        ? pseudorandom_p_card(pseudoseed(tfmt("front%s%s", key_append ? key_append : "", ante))) : NULL;
    CardParams params = {
        .bypass_discovery_center = area == G.shop_jokers || area == G.pack_cards || area == G.shop_vouchers || area == G.jokers || area == G.consumeables,
        .bypass_discovery_ui = area == G.shop_jokers || area == G.pack_cards || area == G.shop_vouchers,
        .discover = area == G.jokers || area == G.consumeables,
    };
    if (G.GAME.selected_back) { params.has_bypass_back = true; params.bypass_back = G.GAME.selected_back->pos; }
    Card *card = card_new(area->n.T->x + area->n.T->w / 2, area->n.T->y, G.CARD_W, G.CARD_H, front, center, &params);
    if (card->ability.consumeable && !skip_materialize) Card_start_materialize(card, NULL, 0, false, NAN);
    if (!strcmp(type, "Joker")) {
        if (G.GAME.modifiers.all_eternal) Card_set_eternal(card, true);
        if (area == G.shop_jokers || area == G.pack_cards) {
            double ep = pseudorandom_seed(pseudoseed(tfmt("%s%s", area == G.pack_cards ? "packetper" : "etperpoll", ante)));
            if (G.GAME.modifiers.enable_eternals_in_shop && ep > 0.7) Card_set_eternal(card, true);
            else if (G.GAME.modifiers.enable_perishables_in_shop && ep > 0.4 && ep <= 0.7) Card_set_perishable(card, true);
            if (G.GAME.modifiers.enable_rentals_in_shop && pseudorandom_seed(pseudoseed(tfmt("%s%s", area == G.pack_cards ? "packssjr" : "ssjr", ante))) > 0.7)
                Card_set_rental(card, true);
        }
        EditionReq ed = poll_edition(tfmt("edi%s%s", key_append ? key_append : "", ante), NAN, false, false);
        Card_set_edition(card, ed, false, false);
        UNLOCK(.type = "have_edition");
    }
    return card;
}

Card *copy_card(Card *other, Card *new_card, double card_scale, int playing_card, bool strip_edition) {
    if (isnan(card_scale) || card_scale == 0) card_scale = 1;
    if (!new_card)
        new_card = card_new(other->n.T->x, other->n.T->y, G.CARD_W * card_scale, G.CARD_H * card_scale, P_CARD("empty"), P_CENTER("c_base"),
                            &(CardParams){ .playing_card = playing_card > 0 ? playing_card : 0 });
    Card_set_ability(new_card, other->config.center, false, false);
    new_card->ability.type = other->ability.type;
    Card_set_base(new_card, other->config.card, false);
    Card_copy_ability(new_card, other);
    if (!strip_edition) Card_set_edition(new_card, edition_req_from_edition(other->edition), false, true);
    UNLOCK(.type = "have_edition");
    Card_set_seal(new_card, other->seal, true, false);
    new_card->params = other->params;
    new_card->params.playing_card = playing_card > 0 ? playing_card : 0;
    new_card->debuff = other->debuff;
    new_card->pinned = other->pinned;
    return new_card;
}

/* ------------------------------------------------------------------ */
/* tutorial_info                                                       */
/* ------------------------------------------------------------------ */
typedef struct { TutorialArgs a; } TutCtx;

EVFN(ev_tut_snap) {
    TutCtx *c = p_; (void)e_;
    if (G.OVERLAY_TUTORIAL && G.OVERLAY_TUTORIAL->Jimbo && !G.OVERLAY_TUTORIAL->Jimbo->talking) {
        Node *n = c->a.snap_to ? c->a.snap_to() : NULL;
        if (n) {
            G.CONTROLLER.interrupt.focus = false;
            Controller_snap_to_node(&G.CONTROLLER, n);
        }
        return true;
    }
    return false;
}

EVFN(ev_tut_step) {
    TutCtx *c = p_; (void)e_;
    TutorialArgs *a = &c->a;
    UIBox *ot = G.OVERLAY_TUTORIAL;
    if (ot && ot->step == a->step && !ot->step_complete) {
        G.CONTROLLER.interrupt.focus = true;
        if (!ot->Jimbo) ot->Jimbo = card_character_new(a->pos.x, a->pos.y, NAN, NAN, NULL);
        PtrVec hl = { 0 };
        if (a->highlight_fn) a->highlight_fn(&hl);
        for (int i = 0; i < a->nhighlight; i++) if (a->highlight[i]) vec_push(&hl, a->highlight[i]);
        vec_push(&hl, &ot->Jimbo->n);
        CardCharacter_add_speech_bubble(ot->Jimbo, a->text_key, a->align, a->quip, a->loc_vars);
        Moveable_set_alignment(&ot->Jimbo->n, a->attach);
        if (a->hard_set) CardCharacter_hard_set_VT(ot->Jimbo);
        ot->button_listen = NULL;
        if (ot->content) { node_remove(&ot->content->n); ot->content = NULL; }
        if (a->button_listen) ot->button_listen = a->button_listen;
        if (!a->no_button)
            CardCharacter_add_button(ot->Jimbo, a->button.set ? a->button.label : localize_key("b_next"), a->button.set ? a->button.func : G_FUNCS_tut_next,
                                     a->button.colour, a->button.update_func, true, NAN);
        DVal *lines = dv_get(loc_misc("tutorial"), a->text_key);
        CardCharacter_say_stuff(ot->Jimbo, 2 * (lines ? dv_len(lines) : 0) + 1, false);
        if (a->snap_to) {
            TutCtx *s = CTX(TutCtx); *s = *c;
            add_event_q(EVENT(.trigger = TRIG_IMMEDIATE, .blocking = 0, .blockable = 0, .func = ev_tut_snap, .ctx = s), Q_TUTORIAL, false);
        }
        vec_free(&ot->highlights);
        ot->highlights = hl;
        ot->step_complete = true;
    }
    return !G.OVERLAY_TUTORIAL || G.OVERLAY_TUTORIAL->step > a->step || G.OVERLAY_TUTORIAL->skip_steps;
}

int tutorial_info(TutorialArgs a) {
    Col overlay_colour = col_new(0.32f, 0.36f, 0.41f, 0);
    ease_value(REFF(&overlay_colour[3]), 0.6, false, TIMER_REAL, true, 0.4, EASE_LERP);
    if (!G.OVERLAY_TUTORIAL) {
        G.OVERLAY_TUTORIAL = uibox_new(
            UIROOT(CFG(.align = "cm", .padding = 32.05, .r = 0.1, .colour = overlay_colour, .emboss = 0.05),
                UIR(CFG(.align = "tr", .minh = G.ROOM->T->h, .minw = G.ROOM->T->w),
                    UIBUTTON(.label = { tfmt("%s >", localize_key("b_skip")) }, .nlabel = 1, .button = G_FUNCS_skip_tutorial_section, .minw = 1.3, .scale = 0.45,
                             .colour = G.C.JOKER_GREY))),
            UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, 3.2 }, .major = G.ROOM_ATTACH, .bond = BOND_WEAK));
        G.OVERLAY_TUTORIAL->step = 1;
    }
    if (!G.OVERLAY_TUTORIAL->step) G.OVERLAY_TUTORIAL->step = 1;
    G.OVERLAY_TUTORIAL->step_complete = false;
    UIElement *rdc = UIBox_get_UIE_by_ID(G.HUD, "row_dollars_chips", NULL);
    if (!a.align) a.align = "tm";
    if (!a.step) a.step = 1;
    if (!a.has_attach) a.attach = ALIGN_ARGS(.major = rdc ? &rdc->n : NULL, .type = "tm", .has_offset = true, .offset = { 0, -0.5 });
    if (!a.has_pos && a.attach.major) {
        Transform *T = a.attach.major->T;
        a.pos = (Vec2){ T->x + T->w / 2, T->y + T->h / 2 };
    }
    if (a.text_key) a.text_key = intern(a.text_key);
    TutCtx *c = CTX(TutCtx); c->a = a;
    add_event_q(EVENT(.trigger = TRIG_AFTER, .delay = 0.3, .func = ev_tut_step, .ctx = c), Q_TUTORIAL, false);
    return a.step + 1;
}

/* ------------------------------------------------------------------ */
/* rerolls / per-round resets / bosses                                 */
/* ------------------------------------------------------------------ */
void calculate_reroll_cost(bool skip_increment) {
    CurrentRound *cr = &G.GAME.current_round;
    if (cr->free_rerolls < 0) cr->free_rerolls = 0;
    if (cr->free_rerolls > 0) { cr->reroll_cost = 0; return; }
    if (!skip_increment) cr->reroll_cost_increase += 1;
    cr->reroll_cost = (G.GAME.round_resets.has_temp_reroll_cost ? G.GAME.round_resets.temp_reroll_cost : G.GAME.round_resets.reroll_cost) + cr->reroll_cost_increase;
}

static void valid_non_stone(CardList *out) {
    for (int i = 0; i < G.playing_cards.count; i++) {
        Card *v = G.playing_cards.items[i];
        if (!str_eq(v->ability.effect, "Stone Card")) vec_push(out, v);
    }
}

void reset_idol_card(void) {
    CurrentRound *cr = &G.GAME.current_round;
    cr->idol_card.rank = "Ace";
    cr->idol_card.suit = "Spades";
    CardList valid = { 0 };
    valid_non_stone(&valid);
    if (valid.count) {
        Card *c = pseudorandom_card(&valid, pseudoseed(tfmt("idol%s", num_str(G.GAME.round_resets.ante))));
        cr->idol_card.rank = c->base.value;
        cr->idol_card.suit = c->base.suit;
        cr->idol_card.id = c->base.id;
    }
    vec_free(&valid);
}

void reset_mail_rank(void) {
    CurrentRound *cr = &G.GAME.current_round;
    cr->mail_card.rank = "Ace";
    CardList valid = { 0 };
    valid_non_stone(&valid);
    if (valid.count) {
        Card *c = pseudorandom_card(&valid, pseudoseed(tfmt("mail%s", num_str(G.GAME.round_resets.ante))));
        cr->mail_card.rank = c->base.value;
        cr->mail_card.id = c->base.id;
    }
    vec_free(&valid);
}

void reset_ancient_card(void) {
    static const char *suits[4] = { "Spades", "Hearts", "Clubs", "Diamonds" };
    const char *cand[4]; int n = 0;
    for (int i = 0; i < 4; i++) if (!str_eq(suits[i], G.GAME.current_round.ancient_card.suit)) cand[n++] = suits[i];
    int idx = pseudorandom_index(n, pseudoseed(tfmt("anc%s", num_str(G.GAME.round_resets.ante))), true);
    G.GAME.current_round.ancient_card.suit = cand[idx - 1];
}

void reset_castle_card(void) {
    CurrentRound *cr = &G.GAME.current_round;
    cr->castle_card.suit = "Spades";
    CardList valid = { 0 };
    valid_non_stone(&valid);
    if (valid.count) {
        Card *c = pseudorandom_card(&valid, pseudoseed(tfmt("cas%s", num_str(G.GAME.round_resets.ante))));
        cr->castle_card.suit = c->base.suit;
    }
    vec_free(&valid);
}

void reset_blinds(void) {
    RoundResets *rr = &G.GAME.round_resets;
    if (!rr->blind_states[0]) { rr->blind_states[0] = "Select"; rr->blind_states[1] = "Upcoming"; rr->blind_states[2] = "Upcoming"; }
    if (str_eq(rr->blind_states[BT_BOSS], "Defeated")) {
        rr->blind_states[0] = rr->blind_states[1] = rr->blind_states[2] = "Upcoming";
        G.GAME.blind_on_deck = "Small";
        rr->blind_choices[BT_BOSS] = get_new_boss();
        rr->boss_rerolled = false;
    }
}

static int str_cmp_p(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

const char *get_new_boss(void) {
    double ante = G.GAME.round_resets.ante;
    double ma = MAX(1, ante);
    const char **elig = xcalloc((size_t)G.n_blinds + 1, sizeof(char *));
    double *uses = xcalloc((size_t)G.n_blinds + 1, sizeof(double));
    int n = 0;
    for (int i = 0; i < G.n_blinds; i++) {
        BlindProto *v = &G.P_BLINDS_LIST[i];
        if (!v->has_boss) continue;
        bool ok = false;
        if (!v->boss.showdown && v->boss.min <= ma && (lua_mod(ma, G.GAME.win_ante) != 0 || ante < 2)) ok = true;
        else if (v->boss.showdown && lua_mod(ante, G.GAME.win_ante) == 0 && ante >= 2) ok = true;
        if (ok && !strset_has(&G.GAME.banned_keys, v->key)) elig[n++] = v->key;
    }
    double min_use = 100;
    for (int i = 0; i < n; i++) {
        uses[i] = nummap_get(&G.GAME.bosses_used, elig[i], 0);
        if (nummap_has(&G.GAME.bosses_used, elig[i]) && uses[i] <= min_use) min_use = uses[i];
    }
    int m = 0;
    for (int i = 0; i < n; i++) {
        /* bosses not present in bosses_used keep value `true` in Lua and are never culled */
        if (nummap_has(&G.GAME.bosses_used, elig[i]) && uses[i] > min_use) continue;
        elig[m++] = elig[i];
    }
    qsort(elig, (size_t)m, sizeof(char *), str_cmp_p);
    int idx = pseudorandom_index(m, pseudoseed("boss"), true);
    const char *boss = idx > 0 ? elig[idx - 1] : "bl_small";
    nummap_set(&G.GAME.bosses_used, boss, nummap_get(&G.GAME.bosses_used, boss, 0) + 1);
    free(elig);
    free(uses);
    return boss;
}

Col get_type_colour(Center *c, Card *card) {
    static float cyan[4] = { 0, 1, 1, 1 };
    if (!c) return card && card->debuff ? mix_colours(G.C.RED, G.C.GREY, 0.7) : cyan;
    if (!c->unlocked && !(card && card->bypass_lock)) return G.C.BLACK;
    /* the second clause in the Lua is always false (`_c.area` is nil on centers) */
    if (card && card->debuff) return mix_colours(G.C.RED, G.C.GREY, 0.7);
    if (str_eq(c->set, "Joker") && c->rarity >= 1 && c->rarity <= 4) return G.C.RARITY[c->rarity];
    if (str_eq(c->set, "Edition")) return G.C.DARK_EDITION;
    if (str_eq(c->set, "Booster")) return G.C.BOOSTER;
    Col *slot = set_colour_slot(c->set, true);
    if (slot && *slot) return *slot;
    return cyan;
}

/* ------------------------------------------------------------------ */
/* generate_card_ui                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *key, *set, *name, *effect;
    Center *center;
    double extra; bool has_extra;
    LocVars *vars;
} GenC;

static GenC genc_from(void *p, int kind) {
    GenC g = { 0 };
    if (kind == 1) {
        Center *c = p;
        g.center = c; g.key = c->key; g.set = c->set; g.name = c->name; g.effect = c->effect;
    } else if (kind == 2) {
        TagProto *t = p;
        g.key = t->key; g.set = t->set ? t->set : "Tag"; g.name = t->name;
    } else {
        InfoItem *i = p;
        g.key = i->key; g.set = i->set; g.extra = i->extra; g.has_extra = i->has_extra; g.vars = i->vars;
        Center *c = P_CENTER(i->key);
        if (c && !i->has_extra && !i->vars) { g.center = c; g.name = c->name; g.effect = c->effect; }
    }
    return g;
}

typedef struct { void *p; int kind; } InfoQ;
static void q_center(InfoQ *q, int *n, Center *c) { if (c && *n < 16) { q[*n].p = c; q[*n].kind = 1; (*n)++; } }
static void q_info(InfoQ *q, int *n, const char *key, const char *set, double extra, bool has_extra, LocVars *vars) {
    if (*n >= 16) return;
    InfoItem *it = NEW(InfoItem);
    it->key = key; it->set = set; it->extra = extra; it->has_extra = has_extra; it->vars = vars;
    q[*n].p = it; q[*n].kind = 3; (*n)++;
}

static UIDef *add_info_box(CardUITable *t) {
    if (t->ninfos == t->cap_infos) {
        t->cap_infos = t->cap_infos ? t->cap_infos * 2 : 4;
        t->infos = xrealloc(t->infos, (size_t)t->cap_infos * sizeof(UIDef *));
        t->info_names = xrealloc(t->info_names, (size_t)t->cap_infos * sizeof(char *));
    }
    UIDef *box = uinodes();
    t->infos[t->ninfos] = box;
    t->info_names[t->ninfos] = NULL;
    t->ninfos++;
    return box;
}

static const char *cfmt(double v) { return num_str(v); }

static void loc_desc(const char *type, const char *key, const char *set, UIDef *nodes, LocVars *vars) {
    localize_ui((LocArgs){ .type = type, .key = key, .set = set, .nodes = nodes, .vars = vars ? vars : loc_vars_new() });
}

static UIDef *first_row(UIDef *rows) { return rows && rows->count > 0 ? rows->nodes[0] : NULL; }

CardUITable *generate_card_ui(void *center, int kind, CardUITable *full, LocVars *specific_vars, const char *card_type, Badges *badges, bool hide_desc,
                              UIDef *main_start, UIDef *main_end) {
    if (!center) return full;
    GenC c = genc_from(center, kind);
    bool first_pass = false;
    if (!full) {
        first_pass = true;
        full = NEW(CardUITable);
        full->main = uinodes();
        full->info = uinodes();
        full->type = uinodes();
        full->name = NULL;
        full->name_set = false;
        if (badges) full->badges = *badges;
    }
    LocVars *sv = (specific_vars && specific_vars->present) ? specific_vars : NULL;
    UIDef *desc_nodes = full->name_set ? NULL : full->main;
    bool desc_is_main = !full->name_set;
    const char *name_override = NULL;
    InfoQ info_queue[16]; int nq = 0;
    int info_idx = -1;
    if (full->name_set) {
        desc_nodes = add_info_box(full);
        info_idx = full->ninfos - 1;
        desc_is_main = false;
    }
    if (!full->name_set) {
        if (sv && sv->no_name) full->name_set = true;
        else if (str_eq(card_type, "Locked")) { full->name = localize_ui((LocArgs){ .type = "name", .set = "Other", .key = "locked" }); full->name_set = true; }
        else if (str_eq(card_type, "Undiscovered")) {
            full->name = localize_ui((LocArgs){ .type = "name", .set = "Other", .key = tfmt("undiscovered_%s", tlower(c.set)) });
            full->name_set = true;
        } else if (sv && (str_eq(card_type, "Default") || str_eq(card_type, "Enhanced"))) {
            if (str_eq(c.name, "Stone Card")) full->name_set = true;
            if (sv->playing_card && !str_eq(c.name, "Stone Card")) {
                UIDef *nm = uinodes();
                LocVars *lv = LV(localize_cat(sv->value, "ranks"), localize_cat(sv->suit, "suits_plural"));
                lv->colours[lv->ncolours++] = sv->colour;
                loc_desc("other", "playing_card", "Other", nm, lv);
                full->name = first_row(nm);
                full->name_set = true;
            }
        } else if (str_eq(card_type, "Booster")) {
        } else {
            full->name = localize_ui((LocArgs){ .type = "name", .set = c.set, .key = c.key });
            full->name_set = true;
        }
        full->card_type = card_type ? card_type : c.set;
    }

    LocVars *lv = loc_vars_new();
    if (main_start) uidef_add(desc_nodes, main_start);
    Center *ce = c.center;
    CenterConfig *cfg = ce ? &ce->config : NULL;
    UnlockCondition *uc = ce ? &ce->unlock_condition : NULL;
#define N(s) str_eq(c.name, s)
    if (str_eq(c.set, "Other")) {
        loc_desc("other", c.key, NULL, desc_nodes, sv ? sv : c.vars);
    } else if (str_eq(card_type, "Locked")) {
        if (ce && ce->wip) loc_desc("other", "wip_locked", "Other", desc_nodes, lv);
        else if (ce && ce->demo && sv) loc_desc("other", "demo_shop_locked", NULL, desc_nodes, lv);
        else if (ce && ce->demo) loc_desc("other", "demo_locked", NULL, desc_nodes, lv);
        else if (ce) {
            const char *ex = uc->extra_kind == EX_STR ? uc->extra_str : cfmt(uc->extra_num);
#define CS(s) cfmt(career(s))
            if (N("Mr. Bones")) lv = LV(ex, CS("c_losses"));
            else if (N("Acrobat")) lv = LV(ex, CS("c_hands_played"));
            else if (N("Sock and Buskin")) lv = LV(ex, CS("c_face_cards_played"));
            else if (N("Swashbuckler")) lv = LV(ex, CS("c_jokers_sold"));
            else if (N("Troubadour")) lv = LV(ex);
            else if (N("Smeared Joker")) lv = LV(cfmt(uc->count), localize_name_text("Enhanced", uc->e_key));
            else if (N("Hanging Chad")) lv = LV(localize_cat(uc->extra_str, "poker_hands"));
            else if (N("Rough Gem") || N("Bloodstone") || N("Arrowhead") || N("Onyx Agate")) lv = LV(cfmt(uc->count), localize_cat(uc->suit, "suits_singular"));
            else if (N("Glass Joker")) lv = LV(cfmt(uc->count), localize_name_text("Enhanced", uc->e_key));
            else if (N("Showman") || N("Flower Pot")) lv = LV(cfmt(uc->ante));
            else if (N("Wee Joker") || N("Merry Andy")) lv = LV(cfmt(uc->n_rounds));
            else if (N("Oops! All 6s") || N("The Idol") || N("Stuntman")) lv = LV(number_format(uc->chips));
            else if (N("Seeing Double")) lv = LV(localize_key("ph_4_7_of_clubs"));
            else if (N("The Duo") || N("The Trio") || N("The Family") || N("The Order") || N("The Tribe")) lv = LV(localize_cat(uc->extra_str, "poker_hands"));
            else if (N("Satellite")) lv = LV(ex);
            else if (N("Driver's License")) lv = LV(cfmt(uc->count));
            else if (N("Cartomancer")) lv = LV(cfmt(uc->tarot_count));
            else if (N("Burnt Joker")) lv = LV(ex, CS("c_cards_sold"));
            else if (N("Bootstraps")) lv = LV(cfmt(uc->count));
            else if (N("Overstock Plus")) lv = LV(ex, CS("c_shop_dollars_spent"));
            else if (N("Liquidation")) lv = LV(ex);
            else if (N("Tarot Tycoon")) lv = LV(ex, CS("c_tarots_bought"));
            else if (N("Planet Tycoon")) lv = LV(ex, CS("c_planets_bought"));
            else if (N("Glow Up")) lv = LV(ex);
            else if (N("Reroll Glut")) lv = LV(ex, CS("c_shop_rerolls"));
            else if (N("Omen Globe")) lv = LV(ex, CS("c_tarot_reading_used"));
            else if (N("Observatory")) lv = LV(ex, CS("c_planetarium_used"));
            else if (N("Nacho Tong")) lv = LV(ex, CS("c_cards_played"));
            else if (N("Recyclomancy")) lv = LV(ex, CS("c_cards_discarded"));
            else if (N("Money Tree")) lv = LV(ex, CS("c_round_interest_cap_streak"));
            else if (N("Antimatter")) {
                DVal *vb = dv_get(dv_get(profile(), "voucher_usage"), "v_blank");
                lv = LV(ex, cfmt(vb ? dv_get_num(vb, "count", 0) : 0));
            } else if (N("Illusion")) lv = LV(ex, CS("c_playing_cards_bought"));
            else if (N("Petroglyph") || N("Retcon") || N("Palette")) lv = LV(ex);
#undef CS
            if (ce->rarity == 4 && sv && !sv->not_hidden) localize_ui((LocArgs){ .type = "unlocks", .key = "joker_locked_legendary", .set = "Other", .nodes = desc_nodes, .vars = lv });
            else localize_ui((LocArgs){ .type = "unlocks", .key = c.key, .set = c.set, .nodes = desc_nodes, .vars = lv });
        }
    } else if (hide_desc) {
        loc_desc("other", tfmt("undiscovered_%s", tlower(c.set)), c.set, desc_nodes, NULL);
    } else if (sv && sv->debuffed) {
        loc_desc("other", sv->playing_card ? "debuffed_playing_card" : "debuffed_default", NULL, desc_nodes, NULL);
    } else if (str_eq(c.set, "Joker")) {
        if (N("Stone Joker") || N("Marble Joker")) q_center(info_queue, &nq, P_CENTER("m_stone"));
        else if (N("Steel Joker")) q_center(info_queue, &nq, P_CENTER("m_steel"));
        else if (N("Glass Joker")) q_center(info_queue, &nq, P_CENTER("m_glass"));
        else if (N("Golden Ticket")) q_center(info_queue, &nq, P_CENTER("m_gold"));
        else if (N("Lucky Cat")) q_center(info_queue, &nq, P_CENTER("m_lucky"));
        else if (N("Midas Mask")) q_center(info_queue, &nq, P_CENTER("m_gold"));
        else if (N("Invisible Joker")) {
            if (G.jokers) {
                for (int k = 0; k < G.jokers->cards.count; k++) {
                    Card *v = G.jokers->cards.items[k];
                    if (v->edition && v->edition->negative && dv_get(dv_path(&G.localization, "descriptions.Other"), "remove_negative")) {
                        UIDef *me = uinodes();
                        loc_desc("other", "remove_negative", NULL, me, NULL);
                        main_end = first_row(me);
                        break;
                    }
                }
            }
        } else if (N("Diet Cola")) q_info(info_queue, &nq, "tag_double", "Tag", 0, false, NULL);
        else if (N("Perkeo")) q_info(info_queue, &nq, "e_negative_consumable", "Edition", 1, true, NULL);
        if (sv && sv->pinned) q_info(info_queue, &nq, "pinned_left", "Other", 0, false, NULL);
        if (sv && sv->sticker) q_info(info_queue, &nq, intern(tfmt("%s_sticker", tlower(sv->sticker))), "Other", 0, false, NULL);
        localize_ui((LocArgs){ .type = "descriptions", .key = c.key, .set = c.set, .nodes = desc_nodes, .vars = sv ? sv : loc_vars_new() });
    } else if (str_eq(c.set, "Tag")) {
        if (N("Negative Tag")) q_center(info_queue, &nq, P_CENTER("e_negative"));
        else if (N("Foil Tag")) q_center(info_queue, &nq, P_CENTER("e_foil"));
        else if (N("Holographic Tag")) q_center(info_queue, &nq, P_CENTER("e_holo"));
        else if (N("Polychrome Tag")) q_center(info_queue, &nq, P_CENTER("e_polychrome"));
        else if (N("Charm Tag")) q_center(info_queue, &nq, P_CENTER("p_arcana_mega_1"));
        else if (N("Meteor Tag")) q_center(info_queue, &nq, P_CENTER("p_celestial_mega_1"));
        else if (N("Ethereal Tag")) q_center(info_queue, &nq, P_CENTER("p_spectral_normal_1"));
        else if (N("Standard Tag")) q_center(info_queue, &nq, P_CENTER("p_standard_mega_1"));
        else if (N("Buffoon Tag")) q_center(info_queue, &nq, P_CENTER("p_buffoon_mega_1"));
        localize_ui((LocArgs){ .type = "descriptions", .key = c.key, .set = "Tag", .nodes = desc_nodes, .vars = sv ? sv : loc_vars_new() });
    } else if (str_eq(c.set, "Voucher") && cfg) {
        const char *ex = cfg->extra.kind == EX_STR ? cfg->extra.str : cfmt(cfg->extra.num);
        if (N("Tarot Merchant") || N("Tarot Tycoon") || N("Planet Merchant") || N("Planet Tycoon")) lv = LV(cfmt(cfg->extra_disp));
        else if (N("Hone") || N("Glow Up") || N("Reroll Surplus") || N("Reroll Glut") || N("Grabber") || N("Nacho Tong") || N("Wasteful") || N("Recyclomancy") ||
                 N("Hieroglyph") || N("Petroglyph") || N("Director's Cut") || N("Retcon") || N("Paint Brush") || N("Palette") || N("Telescope") || N("Observatory") ||
                 N("Clearance Sale") || N("Liquidation"))
            lv = LV(ex);
        else if (N("Seed Money") || N("Money Tree")) lv = LV(cfmt(cfg->extra.num / 5));
        loc_desc("descriptions", c.key, c.set, desc_nodes, lv);
    } else if (str_eq(c.set, "Edition")) {
        lv = LV(c.has_extra ? cfmt(c.extra) : (cfg ? (cfg->extra.kind == EX_STR ? cfg->extra.str : cfmt(cfg->extra.num)) : "0"));
        loc_desc("descriptions", c.key, c.set, desc_nodes, lv);
    } else if (str_eq(c.set, "Default") && sv) {
        if (sv->has_nominal_chips) loc_desc("other", "card_chips", NULL, desc_nodes, LV(cfmt(sv->nominal_chips)));
        if (sv->has_bonus_chips) loc_desc("other", "card_extra_chips", NULL, desc_nodes, LV(cfmt(sv->bonus_chips)));
    } else if (str_eq(c.set, "Enhanced") && cfg) {
        if (sv && !N("Stone Card") && sv->has_nominal_chips) loc_desc("other", "card_chips", NULL, desc_nodes, LV(cfmt(sv->nominal_chips)));
        const char *ef = c.effect;
        bool has_bonus = (sv && sv->has_bonus_chips) || cfg->has_bonus;
        double bonus = (sv && sv->has_bonus_chips) ? sv->bonus_chips : cfg->bonus;
        if (str_eq(ef, "Mult Card")) lv = LV(cfmt(cfg->mult));
        else if (str_eq(ef, "Glass Card")) lv = LV(cfmt(cfg->Xmult), cfmt(G.GAME.probabilities.normal), cfmt(cfg->extra.num));
        else if (str_eq(ef, "Steel Card")) lv = LV(cfmt(cfg->h_x_mult));
        else if (str_eq(ef, "Stone Card")) lv = LV(cfmt(bonus));
        else if (str_eq(ef, "Gold Card")) lv = LV(cfmt(cfg->h_dollars));
        else if (str_eq(ef, "Lucky Card")) lv = LV(cfmt(G.GAME.probabilities.normal), cfmt(cfg->mult), "5", cfmt(cfg->p_dollars), "15");
        loc_desc("descriptions", c.key, c.set, desc_nodes, lv);
        if (!N("Stone Card") && has_bonus) loc_desc("other", "card_extra_chips", NULL, desc_nodes, LV(cfmt(bonus)));
    } else if (str_eq(c.set, "Booster") && cfg) {
        static const struct { const char *name, *key; } packs[] = {
            { "Arcana Pack", "p_arcana_normal" }, { "Jumbo Arcana Pack", "p_arcana_jumbo" }, { "Mega Arcana Pack", "p_arcana_mega" },
            { "Celestial Pack", "p_celestial_normal" }, { "Jumbo Celestial Pack", "p_celestial_jumbo" }, { "Mega Celestial Pack", "p_celestial_mega" },
            { "Spectral Pack", "p_spectral_normal" }, { "Jumbo Spectral Pack", "p_spectral_jumbo" }, { "Mega Spectral Pack", "p_spectral_mega" },
            { "Standard Pack", "p_standard_normal" }, { "Jumbo Standard Pack", "p_standard_jumbo" }, { "Mega Standard Pack", "p_standard_mega" },
            { "Buffoon Pack", "p_buffoon_normal" }, { "Jumbo Buffoon Pack", "p_buffoon_jumbo" }, { "Mega Buffoon Pack", "p_buffoon_mega" },
        };
        const char *desc_override = "p_arcana_normal";
        for (size_t k = 0; k < sizeof packs / sizeof *packs; k++)
            if (N(packs[k].name)) { desc_override = packs[k].key; lv = LV(cfmt(cfg->choose), cfmt(cfg->extra.num)); }
        name_override = desc_override;
        if (!full->name_set) { full->name = localize_ui((LocArgs){ .type = "name", .set = "Other", .key = name_override }); full->name_set = true; }
        loc_desc("other", desc_override, NULL, desc_nodes, lv);
    } else if (str_eq(c.set, "Spectral") && cfg) {
        if (N("Familiar") || N("Grim") || N("Incantation")) lv = LV(cfmt(cfg->extra.num));
        else if (N("Immolate")) lv = LV(cfmt(cfg->extra.destroy), cfmt(cfg->extra.dollars));
        else if (N("Hex")) q_center(info_queue, &nq, P_CENTER("e_polychrome"));
        else if (N("Talisman")) q_info(info_queue, &nq, "gold_seal", "Other", 0, false, NULL);
        else if (N("Deja Vu")) q_info(info_queue, &nq, "red_seal", "Other", 0, false, NULL);
        else if (N("Trance")) q_info(info_queue, &nq, "blue_seal", "Other", 0, false, NULL);
        else if (N("Medium")) q_info(info_queue, &nq, "purple_seal", "Other", 0, false, NULL);
        else if (N("Ankh")) {
            if (G.jokers) {
                for (int k = 0; k < G.jokers->cards.count; k++) {
                    Card *v = G.jokers->cards.items[k];
                    if (v->edition && v->edition->negative && dv_get(dv_path(&G.localization, "descriptions.Other"), "remove_negative")) {
                        q_center(info_queue, &nq, P_CENTER("e_negative"));
                        UIDef *me = uinodes();
                        loc_desc("other", "remove_negative", NULL, me, NULL);
                        main_end = first_row(me);
                        break;
                    }
                }
            }
        } else if (N("Cryptid")) lv = LV(cfmt(cfg->extra.num));
        if (N("Ectoplasm")) { q_center(info_queue, &nq, P_CENTER("e_negative")); lv = LV(cfmt(G.GAME.ecto_minus ? G.GAME.ecto_minus : 1)); }
        if (N("Aura")) {
            q_center(info_queue, &nq, P_CENTER("e_foil"));
            q_center(info_queue, &nq, P_CENTER("e_holo"));
            q_center(info_queue, &nq, P_CENTER("e_polychrome"));
        }
        loc_desc("descriptions", c.key, c.set, desc_nodes, lv);
    } else if (str_eq(c.set, "Planet") && cfg) {
        int hi = hand_index(cfg->hand_type);
        HandStat *h = hi >= 0 ? &G.GAME.hands[hi] : NULL;
        if (h) {
            lv = LV(cfmt(h->level), localize_cat(cfg->hand_type, "poker_hands"), cfmt(h->l_mult), cfmt(h->l_chips));
            int li = (int)MIN(7, h->level); if (li < 1) li = 1;
            lv->colours[lv->ncolours++] = h->level == 1 ? G.C.UI.TEXT_DARK : G.C.HAND_LEVELS[li];
        }
        loc_desc("descriptions", c.key, c.set, desc_nodes, lv);
    } else if (str_eq(c.set, "Tarot") && cfg) {
        const char *mc = cfg->mod_conv;
        if (N("The Fool")) {
            Center *fc = G.GAME.last_tarot_planet ? P_CENTER(G.GAME.last_tarot_planet) : NULL;
            const char *ltp = fc ? localize_name_text(fc->set, fc->key) : localize_key("k_none");
            Col colour = (!fc || str_eq(fc->name, "The Fool")) ? G.C.RED : G.C.GREEN;
            main_end = uidef_add(uinodes(), UIC(CFG(.align = "bm", .padding = 0.02),
                UIC(CFG(.align = "m", .colour = colour, .r = 0.05, .padding = 0.05),
                    UIT(CFG(.text = (char *)tfmt(" %s ", ltp), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.3, .shadow = true)))));
            lv = LV(ltp);
            if (fc && !str_eq(fc->name, "The Fool")) q_center(info_queue, &nq, fc);
        } else if (N("The Magician") || N("The Empress") || N("The Hierophant") || N("The Lovers") || N("The Chariot") || N("Justice") || N("The Devil") ||
                   N("The Tower")) {
            lv = LV(cfmt(cfg->max_highlighted), localize_name_text("Enhanced", mc));
            q_center(info_queue, &nq, P_CENTER(mc));
        } else if (N("The High Priestess")) lv = LV(cfmt(cfg->planets));
        else if (N("The Emperor")) lv = LV(cfmt(cfg->tarots));
        else if (N("The Hermit")) lv = LV(cfmt(cfg->extra.num));
        else if (N("The Wheel of Fortune")) {
            lv = LV(cfmt(G.GAME.probabilities.normal), cfmt(cfg->extra.num));
            q_center(info_queue, &nq, P_CENTER("e_foil"));
            q_center(info_queue, &nq, P_CENTER("e_holo"));
            q_center(info_queue, &nq, P_CENTER("e_polychrome"));
        } else if (N("Strength") || N("The Hanged Man") || N("Death")) lv = LV(cfmt(cfg->max_highlighted));
        else if (N("Temperance")) {
            double money = 0;
            if (G.jokers) for (int i = 0; i < G.jokers->cards.count; i++) if (str_eq(G.jokers->cards.items[i]->ability.set, "Joker")) money += G.jokers->cards.items[i]->sell_cost;
            lv = LV(cfmt(cfg->extra.num), cfmt(MIN(cfg->extra.num, money)));
        } else if (N("The Star") || N("The Moon") || N("The Sun") || N("The World")) {
            lv = LV(cfmt(cfg->max_highlighted), localize_cat(cfg->suit_conv, "suits_plural"));
            lv->colours[lv->ncolours++] = suit_colour(cfg->suit_conv);
        }
        loc_desc("descriptions", c.key, c.set, desc_nodes, lv);
    }
#undef N
    if (main_end) uidef_add(desc_nodes, main_end);

    if (!(sv && !sv->sticker && (str_eq(card_type, "Default") || str_eq(card_type, "Enhanced")))) {
        if (desc_is_main && !full->name_set) {
            full->name = localize_ui((LocArgs){ .type = "name", .key = c.key, .set = c.set });
            full->name_set = true;
        } else if (!desc_is_main && info_idx >= 0) {
            full->info_names[info_idx] = xstrdup(localize_name_text(name_override ? "Other" : c.set, name_override ? name_override : c.key));
        }
    }

    if (first_pass && !str_eq(c.set, "Edition") && badges) {
        for (int k = 0; k < badges->n; k++) {
            const char *v = badges->items[k];
            if (str_eq(v, "foil")) q_center(info_queue, &nq, P_CENTER("e_foil"));
            if (str_eq(v, "holographic")) q_center(info_queue, &nq, P_CENTER("e_holo"));
            if (str_eq(v, "polychrome")) q_center(info_queue, &nq, P_CENTER("e_polychrome"));
            if (str_eq(v, "negative")) q_center(info_queue, &nq, P_CENTER("e_negative"));
            if (str_eq(v, "negative_consumable")) q_info(info_queue, &nq, "e_negative_consumable", "Edition", 1, true, NULL);
            if (str_eq(v, "gold_seal") || str_eq(v, "blue_seal") || str_eq(v, "red_seal") || str_eq(v, "purple_seal") || str_eq(v, "eternal") ||
                str_eq(v, "pinned_left"))
                q_info(info_queue, &nq, v, "Other", 0, false, NULL);
            if (str_eq(v, "perishable")) {
                double pr = G.GAME.perishable_rounds ? G.GAME.perishable_rounds : 1;
                q_info(info_queue, &nq, "perishable", "Other", 0, false,
                       LV(cfmt(pr), cfmt(sv && sv->has_perish_tally ? sv->perish_tally : G.GAME.perishable_rounds)));
            }
            if (str_eq(v, "rental")) q_info(info_queue, &nq, "rental", "Other", 0, false, LV(cfmt(G.GAME.rental_rate ? G.GAME.rental_rate : 1)));
        }
    }
    for (int i = 0; i < nq; i++) generate_card_ui(info_queue[i].p, info_queue[i].kind, full, NULL, NULL, NULL, false, NULL, NULL);
    return full;
}
