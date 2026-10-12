/* common_events.c - port of functions/common_events.lua (part 1:
 * screen positions, easing helpers, draw/level/hand text, eval_card,
 * alerts, status text, round evaluation rows, canvas juice). */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define LOC(k) ((char *)localize_key(k))

static UIElement *hud(const char *id) {
    UIElement *e = G.HUD ? UIBox_get_UIE_by_ID(G.HUD, id, NULL) : NULL;
    if (!e && G.HUD_buttons) e = UIBox_get_UIE_by_ID(G.HUD_buttons, id, NULL);
    return e;
}

/* ------------------------------------------------------------------ */
/* set_screen_positions                                                */
/* ------------------------------------------------------------------ */
void set_screen_positions(void) {
    if (G.STAGE == STAGE_RUN) {
        G.hand->n.T->x = G.TILE_W - G.hand->n.T->w - 2.85;
        G.hand->n.T->y = G.TILE_H - G.hand->n.T->h;
        G.play->n.T->x = G.hand->n.T->x + (G.hand->n.T->w - G.play->n.T->w) / 2;
        G.play->n.T->y = G.hand->n.T->y - 3.6;
        G.jokers->n.T->x = G.hand->n.T->x - 0.1;
        G.jokers->n.T->y = 0;
        G.consumeables->n.T->x = G.jokers->n.T->x + G.jokers->n.T->w + 0.2;
        G.consumeables->n.T->y = 0;
        G.deck->n.T->x = G.TILE_W - G.deck->n.T->w - 0.5;
        G.deck->n.T->y = G.TILE_H - G.deck->n.T->h;
        G.discard->n.T->x = G.jokers->n.T->x + G.jokers->n.T->w / 2 + 0.3 + 15;
        G.discard->n.T->y = 4.2;
        CardArea *areas[6] = { G.hand, G.play, G.jokers, G.consumeables, G.deck, G.discard };
        for (int i = 0; i < 6; i++) Moveable_hard_set_VT(&areas[i]->n);
    }
    if (G.STAGE == STAGE_MAIN_MENU && G.title_top) {
        G.title_top->n.T->x = G.TILE_W / 2 - G.title_top->n.T->w / 2;
        if (G.STATE == STATE_DEMO_CTA) G.title_top->n.T->y = G.TILE_H / 2 - G.title_top->n.T->h / 2 - 2;
        /* 3DS: the title has the top screen to itself, so it is centred */
        else G.title_top->n.T->y = G.TILE_H / 2 - G.title_top->n.T->h / 2;
        Moveable_hard_set_VT(&G.title_top->n);
    }
}

/* ------------------------------------------------------------------ */
/* ease helpers                                                        */
/* ------------------------------------------------------------------ */
static double ease_floor(double t, void *ctx) { (void)ctx; return floor(t); }
static double ease_ident(double t, void *ctx) { (void)ctx; return t; }

void ease_value(Ref ref, double mod, bool floored, int timer_type, bool not_blockable, double delay, EaseType ease_type) {
    (void)not_blockable;   /* Lua: blockable = (not_blockable == false); never false at any call site */
    if (isnan(mod)) mod = 0;
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .blocking = 0, .ref = ref, .ease_to = ref_num(ref) + mod, .timer = timer_type,
                    .delay = isnan(delay) ? 0.3 : delay, .ease = ease_type, .ease_func = floored ? ease_floor : ease_ident));
}

typedef struct { double mod; } ModCtx;

EVFN(ev_ease_chips) {
    ModCtx *c = p_; (void)e_;
    UIElement *chip_UI = hud("chip_UI_count");
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .ref = REFN(&G.GAME.chips), .ease_to = c->mod, .delay = 0.3, .ease_func = ease_floor));
    if (chip_UI) node_juice_up(&chip_UI->n, NAN, NAN);
    sound_play("chips2", NAN, NAN);
    return true;
}
void ease_chips(double mod) {
    ModCtx *c = CTX(ModCtx); c->mod = isnan(mod) ? 0 : mod;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_ease_chips, .ctx = c));
}

static void hud_counter_update(UIElement *ui) {
    if (ui && ui->config->object) node_update(ui->config->object, 0);
    if (G.HUD) UIBox_recalculate(G.HUD);
}

static void dollars_mod(double mod) {
    UIElement *dollar_UI = hud("dollar_text_UI");
    const char *text = tfmt("+%s", localize_key("$"));
    Col col = G.C.MONEY;
    if (mod < 0) { text = tfmt("-%s", localize_key("$")); col = G.C.RED; }
    else inc_career_stat("c_dollars_earned", mod);
    G.GAME.dollars = G.GAME.dollars + mod;
    check_and_set_high_score("most_money", G.GAME.dollars);
    UNLOCK(.type = "money");
    hud_counter_update(dollar_UI);
    ATT(.text = tfmt("%s%s", text, num_str(fabs(mod))), .scale = 0.8, .hold = 0.7, .cover = dollar_UI ? dollar_UI->n.parent : NULL, .cover_colour = col, .align = "cm");
    sound_play("coin1", NAN, NAN);
}
EVFN(ev_ease_dollars) { ModCtx *c = p_; (void)e_; dollars_mod(c->mod); return true; }
void ease_dollars(double mod, bool instant) {
    if (isnan(mod)) mod = 0;
    if (instant) { dollars_mod(mod); return; }
    ModCtx *c = CTX(ModCtx); c->mod = mod;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_ease_dollars, .ctx = c));
}

typedef struct { double mod; bool silent; } DiscardCtx;
static void discard_mod(double mod, bool silent) {
    if (fabs(MAX(G.GAME.current_round.discards_left, mod)) == 0) return;
    UIElement *ui = hud("discard_UI_count");
    mod = MAX(-G.GAME.current_round.discards_left, mod);
    const char *text = "+";
    Col col = G.C.GREEN;
    if (mod < 0) { text = ""; col = G.C.RED; }
    G.GAME.current_round.discards_left += mod;
    hud_counter_update(ui);
    ATT(.text = tfmt("%s%s", text, num_str(mod)), .scale = 0.8, .hold = 0.7, .cover = ui ? ui->n.parent : NULL, .cover_colour = col, .align = "cm");
    if (!silent) sound_play("chips2", NAN, NAN);
}
EVFN(ev_ease_discard) { DiscardCtx *c = p_; (void)e_; discard_mod(c->mod, c->silent); return true; }
void ease_discard(double mod, bool instant, bool silent) {
    if (isnan(mod)) mod = 0;
    if (instant) { discard_mod(mod, silent); return; }
    DiscardCtx *c = CTX(DiscardCtx); c->mod = mod; c->silent = silent;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_ease_discard, .ctx = c));
}

static void hands_mod(double mod) {
    UIElement *ui = hud("hand_UI_count");
    const char *text = "+";
    Col col = G.C.GREEN;
    if (mod < 0) { text = ""; col = G.C.RED; }
    G.GAME.current_round.hands_left += mod;
    hud_counter_update(ui);
    ATT(.text = tfmt("%s%s", text, num_str(mod)), .scale = 0.8, .hold = 0.7, .cover = ui ? ui->n.parent : NULL, .cover_colour = col, .align = "cm");
    sound_play("chips2", NAN, NAN);
}
EVFN(ev_ease_hands) { ModCtx *c = p_; (void)e_; hands_mod(c->mod); return true; }
void ease_hands_played(double mod, bool instant) {
    if (isnan(mod)) mod = 0;
    if (instant) { hands_mod(mod); return; }
    ModCtx *c = CTX(ModCtx); c->mod = mod;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_ease_hands, .ctx = c));
}

EVFN(ev_ease_ante) {
    ModCtx *c = p_; (void)e_;
    UIElement *ui = G.hand_text_area.ante;
    double mod = c->mod;
    const char *text = "+";
    Col col = G.C.IMPORTANT;
    if (mod < 0) { text = "-"; col = G.C.RED; }
    G.GAME.round_resets.ante += mod;
    check_and_set_high_score("furthest_ante", G.GAME.round_resets.ante);
    hud_counter_update(ui);
    ATT(.text = tfmt("%s%s", text, num_str(fabs(mod))), .scale = 1, .hold = 0.7, .cover = ui ? ui->n.parent : NULL, .cover_colour = col, .align = "cm");
    sound_play("highlight2", 0.685, 0.2);
    sound_play("generic1", NAN, NAN);
    return true;
}
void ease_ante(double mod) {
    ModCtx *c = CTX(ModCtx); c->mod = isnan(mod) ? 0 : mod;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_ease_ante, .ctx = c));
}

EVFN(ev_ease_round) {
    ModCtx *c = p_; (void)e_;
    UIElement *ui = G.hand_text_area.round;
    double mod = c->mod;
    const char *text = "+";
    Col col = G.C.IMPORTANT;
    if (mod < 0) { text = ""; col = G.C.RED; }
    G.GAME.round += mod;
    check_and_set_high_score("furthest_round", G.GAME.round);
    check_and_set_high_score("furthest_ante", G.GAME.round_resets.ante);
    hud_counter_update(ui);
    ATT(.text = tfmt("%s%s", text, num_str(fabs(mod))), .scale = 1, .hold = 0.7, .cover = ui ? ui->n.parent : NULL, .cover_colour = col, .align = "cm");
    sound_play("timpani", 0.8, NAN);
    sound_play("generic1", NAN, NAN);
    return true;
}
void ease_round(double mod) {
    ModCtx *c = CTX(ModCtx); c->mod = isnan(mod) ? 0 : mod;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_ease_round, .ctx = c));
}

static void ease_rgb(Col v, const float *target, double brightness) {
    for (int i = 0; i < 3; i++) ease_value(REFF(&v[i]), target[i] * brightness - v[i], false, -1, true, 0.6, EASE_LERP);
}

void ease_background_colour(BgColourArgs a) {
    Col slots[3] = { G.C.BACKGROUND.L, G.C.BACKGROUND.D, G.C.BACKGROUND.C };
    const char keys[3] = { 'L', 'D', 'C' };
    for (int i = 0; i < 3; i++) {
        if (!a.new_colour) break;
        char k = keys[i];
        Col v = slots[i];
        if (a.special_colour && a.tertiary_colour) {
            Col src = k == 'L' ? a.new_colour : k == 'C' ? a.special_colour : a.tertiary_colour;
            ease_rgb(v, src, 1);
        } else {
            double brightness = k == 'L' ? 1.3 : k == 'D' ? (a.special_colour ? 0.4 : 0.7) : 0.9;
            if (k == 'C' && a.special_colour) ease_rgb(v, a.special_colour, 1);
            else ease_rgb(v, a.new_colour, brightness);
        }
    }
    if (a.has_contrast) ease_value(REFN(&G.C.BACKGROUND.contrast), a.contrast - G.C.BACKGROUND.contrast, false, -1, true, 0.6, EASE_LERP);
}

void ease_colour(Col old_colour, const float *new_colour, double delay) {
    for (int i = 0; i < 4; i++) ease_value(REFF(&old_colour[i]), new_colour[i] - old_colour[i], false, TIMER_REAL, false, delay, EASE_LERP);
}

void ease_background_colour_blind(int state, const char *blind_override) {
    const char *blindname = blind_override ? blind_override : (G.GAME.blind && G.GAME.blind->name && G.GAME.blind->name[0] ? G.GAME.blind->name : NULL);
    if (!blindname || !blindname[0]) blindname = "Small Blind";
    if (state == STATE_SHOP) ease_colour(G.C.DYN_UI.MAIN, mix_colours(G.C.RED, G.C.BLACK, 0.9), NAN);
    else if (state == STATE_TAROT_PACK) ease_colour(G.C.DYN_UI.MAIN, mix_colours(G.C.WHITE, G.C.BLACK, 0.9), NAN);
    else if (state == STATE_SPECTRAL_PACK) ease_colour(G.C.DYN_UI.MAIN, mix_colours(G.C.SECONDARY_SET.Spectral, G.C.BLACK, 0.9), NAN);
    else if (state == STATE_STANDARD_PACK) ease_colour(G.C.DYN_UI.MAIN, G.C.RED, NAN);
    else if (state == STATE_BUFFOON_PACK) ease_colour(G.C.DYN_UI.MAIN, G.C.FILTER, NAN);
    else if (state == STATE_PLANET_PACK) ease_colour(G.C.DYN_UI.MAIN, mix_colours(G.C.SECONDARY_SET.Planet, G.C.BLACK, 0.9), NAN);
    else if (G.GAME.blind) Blind_change_colour(G.GAME.blind, NULL);

    if (state == STATE_TAROT_PACK) ease_background_colour((BgColourArgs){ .new_colour = G.C.PURPLE, .special_colour = darken(G.C.BLACK, 0.2), .contrast = 1.5, .has_contrast = true });
    else if (state == STATE_SPECTRAL_PACK) ease_background_colour((BgColourArgs){ .new_colour = G.C.SECONDARY_SET.Spectral, .special_colour = darken(G.C.BLACK, 0.2), .contrast = 2, .has_contrast = true });
    else if (state == STATE_STANDARD_PACK) ease_background_colour((BgColourArgs){ .new_colour = darken(G.C.BLACK, 0.2), .special_colour = G.C.RED, .contrast = 3, .has_contrast = true });
    else if (state == STATE_BUFFOON_PACK) ease_background_colour((BgColourArgs){ .new_colour = G.C.FILTER, .special_colour = G.C.BLACK, .contrast = 2, .has_contrast = true });
    else if (state == STATE_PLANET_PACK) ease_background_colour((BgColourArgs){ .new_colour = G.C.BLACK, .contrast = 3, .has_contrast = true });
    else if (G.GAME.won) ease_background_colour((BgColourArgs){ .new_colour = G.C.BLIND.won, .contrast = 1, .has_contrast = true });
    else if (str_eq(blindname, "Small Blind") || str_eq(blindname, "Big Blind")) ease_background_colour((BgColourArgs){ .new_colour = G.C.BLIND.Small, .contrast = 1, .has_contrast = true });
    else {
        Col boss_col = G.C.BLACK;
        for (int i = 0; i < G.n_blinds; i++) {
            BlindProto *v = &G.P_BLINDS_LIST[i];
            if (str_eq(v->name, blindname)) {
                if (v->boss.showdown) {
                    ease_background_colour((BgColourArgs){ .new_colour = G.C.BLUE, .special_colour = G.C.RED, .tertiary_colour = darken(G.C.BLACK, 0.4), .contrast = 3,
                                                           .has_contrast = true });
                    return;
                }
                boss_col = v->boss_colour ? v->boss_colour : G.C.BLACK;
            }
        }
        ease_background_colour((BgColourArgs){ .new_colour = lighten(mix_colours(boss_col, G.C.BLACK, 0.3), 0.1), .special_colour = boss_col, .contrast = 2,
                                               .has_contrast = true });
    }
}

EVFN(ev_true) { (void)e_; (void)p_; return true; }
void delay(double time) { delay_q(time, Q_BASE); }
void delay_q(double time, QueueKind q) {
    add_event_q(EVENT(.trigger = TRIG_AFTER, .delay = isnan(time) ? 1 : time, .func = ev_true), q, false);
}

/* ------------------------------------------------------------------ */
/* add_joker / draw_card / highlight / status texts                    */
/* ------------------------------------------------------------------ */
Card *add_joker(const char *key, const char *edition, bool silent, bool eternal) {
    Center *c = P_CENTER(key);
    CardArea *area = (c && c->consumeable) ? G.consumeables : G.jokers;
    double x = area ? area->n.T->x : G.ROOM->T->w / 2 - G.CARD_W / 2, y = area ? area->n.T->y : G.ROOM->T->h / 2 - G.CARD_H / 2;
    CardParams p = { .discover = true, .bypass_discovery_center = true, .bypass_discovery_ui = true };
    if (G.GAME.selected_back) { p.has_bypass_back = true; p.bypass_back = G.GAME.selected_back->pos; }
    Card *card = card_new(x, y, G.CARD_W, G.CARD_H, P_CARD("empty"), c, &p);
    Card_start_materialize(card, NULL, 0, silent, NAN);
    if (area) Card_add_to_deck(card, false);
    if (edition) Card_set_edition(card, edition_req_from_key(edition), false, false);
    if (eternal) Card_set_eternal(card, true);
    if (area && str_eq(card->ability.set, "Joker")) CardArea_emplace(area, card, NULL, false);
    else if (G.consumeables) CardArea_emplace(G.consumeables, card, NULL, false);
    card->n.created_on_pause = false;
    return card;
}

typedef struct {
    CardArea *from, *to; double percent; bool sort; Card *card; bool mute, stay_flipped; double vol; bool discarded_only;
} DrawCtx;
EVFN(ev_draw_card) {
    DrawCtx *c = p_; (void)e_;
    bool drawn = false;
    if (c->card) {
        Card *card = c->card;
        if (c->from) card = CardArea_remove_card(c->from, card, false);
        if (card) drawn = true;
        bool stay_flipped = G.GAME.blind && Blind_stay_flipped(G.GAME.blind, c->to, card);
        if (G.GAME.modifiers.flipped_cards && c->to == G.hand) {
            if (pseudorandom_seed(pseudoseed("flipped_card")) < 1 / G.GAME.modifiers.flipped_cards) stay_flipped = true;
        }
        if (card) CardArea_emplace(c->to, card, NULL, stay_flipped);
    } else {
        if (CardArea_draw_card_from(c->to, c->from, c->stay_flipped, c->discarded_only)) drawn = true;
    }
    if (!c->mute && drawn) {
        CardArea *f = c->from;
        if (f == G.deck || f == G.hand || f == G.play || f == G.jokers || f == G.consumeables || f == G.discard) G.VIBRATION += 0.6;
        sound_play("card1", 0.85 + c->percent * 0.2 / 100, 0.6 * (isnan(c->vol) ? 1 : c->vol));
    }
    if (c->sort) CardArea_sort(c->to, NULL);
    return true;
}

void draw_card(CardArea *from, CardArea *to, double percent, const char *dir, bool sort, Card *card, double delay_v, bool mute, bool stay_flipped,
               double vol, bool discarded_only) {
    if (isnan(percent)) percent = 50;
    if (isnan(delay_v)) delay_v = 0.1;
    if (str_eq(dir, "down")) percent = 1 - percent;
    DrawCtx *c = CTX(DrawCtx);
    *c = (DrawCtx){ from, to, percent, sort, card, mute, stay_flipped, vol, discarded_only };
    add_event(EVENT(.trigger = TRIG_BEFORE, .delay = delay_v, .func = ev_draw_card, .ctx = c));
}

typedef struct { Card *card; double percent; bool highlight; } HlCtx;
EVFN(ev_highlight_card) {
    HlCtx *c = p_; (void)e_;
    Card_highlight(c->card, c->highlight);
    sound_play("cardSlide1", 0.85 + c->percent * 0.2, NAN);
    return true;
}
void highlight_card(Card *card, double percent, const char *dir) {
    if (isnan(percent)) percent = 0.5;
    bool highlight = true;
    if (str_eq(dir, "down")) { percent = 1 - percent; highlight = false; }
    HlCtx *c = CTX(HlCtx); c->card = card; c->percent = percent; c->highlight = highlight;
    add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.1, .func = ev_highlight_card, .ctx = c));
}

typedef struct { const char *text; bool silent; } PasCtx;
EVFN(ev_play_area_status) {
    PasCtx *c = p_; (void)e_;
    ATT(.scale = 0.9, .text = c->text, .hold = 0.9, .align = "tm", .major = &G.play->n, .has_offset = true, .offset = { 0, -1 });
    if (!c->silent) { G.ROOM_jiggle += 2; sound_play("cardFan2", NAN, NAN); }
    return true;
}
void play_area_status_text(const char *text, bool silent, double delay_v) {
    if (isnan(delay_v)) delay_v = 0.6;
    PasCtx *c = CTX(PasCtx); c->text = intern(text); c->silent = silent;
    add_event(EVENT(.trigger = delay_v == 0 ? TRIG_IMMEDIATE : TRIG_BEFORE, .delay = delay_v, .func = ev_play_area_status, .ctx = c));
}

/* ------------------------------------------------------------------ */
/* level_up_hand / update_hand_text                                    */
/* ------------------------------------------------------------------ */
typedef struct { Node *card; int step; const char *hand; double level; } LvlCtx;
EVFN(ev_level_step) {
    LvlCtx *c = p_; (void)e_;
    sound_play("tarot1", NAN, NAN);
    if (c->card) node_juice_up(c->card, 0.8, 0.5);
    if (c->step == 0) G.TAROT_INTERRUPT_PULSE = true;
    if (c->step == 2) G.TAROT_INTERRUPT_PULSE = false;
    return true;
}
EVFN(ev_level_unlock) {
    LvlCtx *c = p_; (void)e_;
    UNLOCK(.type = "upgrade_hand", .handname = c->hand, .level = c->level);
    return true;
}

void level_up_hand(Node *card, const char *hand, bool instant, double amount) {
    if (isnan(amount)) amount = 1;
    int hi = hand_index(hand);
    if (hi < 0) return;
    HandStat *h = &G.GAME.hands[hi];
    h->level = MAX(0, h->level + amount);
    h->mult = MAX(h->s_mult + h->l_mult * (h->level - 1), 1);
    h->chips = MAX(h->s_chips + h->l_chips * (h->level - 1), 0);
    if (!instant) {
        static const double delays[3] = { 0.2, 0.9, 0.9 };
        for (int s = 0; s < 3; s++) {
            LvlCtx *c = CTX(LvlCtx); c->card = card; c->step = s;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = delays[s], .func = ev_level_step, .ctx = c));
            if (s == 0) update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult = h->mult, .has_mult = true, .StatusText = true));
            if (s == 1) update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.chips = h->chips, .has_chips = true, .StatusText = true));
        }
        update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 0.9, .has_pitch = true, .delay = 0, .has_delay = true),
                         HTV(.level_n = h->level, .level_is_num = true, .has_level = true));
        delay(1.3);
    }
    LvlCtx *c = CTX(LvlCtx); c->hand = intern(hand); c->level = h->level;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_level_unlock, .ctx = c));
}

typedef struct { HandTextConfig cfg; HandTextVals vals; } HtCtx;
EVFN(ev_tarot2_076) { (void)e_; (void)p_; sound_play("tarot2", 0.76, 0.4); return true; }

static void sync_number_text(char **slot, double v) { str_set(slot, number_format(v)); }

EVFN(ev_update_hand_text) {
    HtCtx *c = p_; (void)e_;
    HandTextConfig *cfg = &c->cfg;
    HandTextVals *v = &c->vals;
    CurrentRound *cr = &G.GAME.current_round;
    Col col = G.C.GREEN;
    if (v->has_chips) {
        bool str = v->chips_s != NULL;
        if (str || cr->current_hand.chips != v->chips || cr->current_hand.chip_text_is_str) {
            double delta = (!str && !cr->current_hand.chip_text_is_str) ? v->chips - cr->current_hand.chips : 0;
            const char *dt;
            if (delta < 0) { dt = num_str(delta); col = G.C.RED; }
            else if (delta > 0) dt = tfmt("+%s", num_str(delta));
            else dt = num_str(delta);
            if (str) dt = v->chips_s;
            if (str) { cr->current_hand.chip_text_is_str = true; str_set(&cr->current_hand.chip_text_override, v->chips_s); cr->current_hand.chips = 0; }
            else { cr->current_hand.chip_text_is_str = false; cr->current_hand.chips = v->chips; }
            if (G.hand_text_area.chips) node_update(&G.hand_text_area.chips->n, 0);
            if (v->StatusText && G.hand_text_area.chips)
                ATT(.text = dt, .scale = 0.8, .hold = 1, .cover = G.hand_text_area.chips->n.parent, .cover_colour = mix_colours(G.C.CHIPS, col, 0.1), .emboss = 0.05,
                    .align = "cm", .cover_align = "cr");
        }
    }
    if (v->has_mult) {
        bool str = v->mult_s != NULL;
        if (str || cr->current_hand.mult != v->mult || cr->current_hand.mult_text_is_str) {
            double delta = (!str && !cr->current_hand.mult_text_is_str) ? v->mult - cr->current_hand.mult : 0;
            const char *dt;
            if (delta < 0) { dt = num_str(delta); col = G.C.RED; }
            else if (delta > 0) dt = tfmt("+%s", num_str(delta));
            else dt = num_str(delta);
            if (str) dt = v->mult_s;
            if (str) { cr->current_hand.mult_text_is_str = true; str_set(&cr->current_hand.mult_text_override, v->mult_s); cr->current_hand.mult = 0; }
            else { cr->current_hand.mult_text_is_str = false; cr->current_hand.mult = v->mult; }
            if (G.hand_text_area.mult) node_update(&G.hand_text_area.mult->n, 0);
            if (v->StatusText && G.hand_text_area.mult)
                ATT(.text = dt, .scale = 0.8, .hold = 1, .cover = G.hand_text_area.mult->n.parent, .cover_colour = mix_colours(G.C.MULT, col, 0.1), .emboss = 0.05,
                    .align = "cm", .cover_align = "cl");
            if (!G.TAROT_INTERRUPT && G.hand_text_area.mult) node_juice_up(&G.hand_text_area.mult->n, NAN, NAN);
        }
    }
    if (v->has_handname && !str_eq(cr->current_hand.handname, v->handname)) {
        str_set(&cr->current_hand.handname, v->handname);
        if (!cfg->nopulse && G.hand_text_area.handname && G.hand_text_area.handname->config->object)
            DynaText_pulse((DynaText *)G.hand_text_area.handname->config->object, 0.2);
    }
    if (v->has_chip_total) {
        cr->current_hand.chip_total = v->chip_total;
        if (G.hand_text_area.chip_total && G.hand_text_area.chip_total->config->object)
            DynaText_pulse((DynaText *)G.hand_text_area.chip_total->config->object, 0.5);
    }
    if (v->has_level) {
        const char *lvl = v->level_is_num ? num_str(v->level_n) : (v->level ? v->level : "");
        const char *target = tfmt(" %s%s", localize_key("k_lvl"), lvl);
        if (!str_eq(cr->current_hand.hand_level, target)) {
            if (!v->level_is_num && (!v->level || !v->level[0])) str_set(&cr->current_hand.hand_level, "");
            else {
                str_set(&cr->current_hand.hand_level, target);
                UIElement *hl = G.hand_text_area.hand_level;
                if (hl) {
                    if (v->level_is_num) { int li = (int)MIN(v->level_n, 7); if (li < 1) li = 1; hl->config->colour = G.C.HAND_LEVELS[li]; }
                    else hl->config->colour = G.C.HAND_LEVELS[1];
                    node_juice_up(&hl->n, NAN, NAN);
                }
            }
        }
    }
    if (cfg->sound && !cfg->modded) sound_play(cfg->sound, cfg->has_pitch ? cfg->pitch : 1, cfg->has_volume ? cfg->volume : 1);
    if (cfg->modded) {
        UIElement *d1 = G.HUD_blind ? UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_debuff_1", NULL) : NULL;
        UIElement *d2 = G.HUD_blind ? UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_debuff_2", NULL) : NULL;
        if (d1) node_juice_up(&d1->n, 0.3, 0);
        if (d2) node_juice_up(&d2->n, 0.3, 0);
        if (G.GAME.blind) node_juice_up(&G.GAME.blind->n, NAN, NAN);
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.06 * G.SETTINGS.GAMESPEED, .blockable = 0, .blocking = 0, .func = ev_tarot2_076));
        sound_play("tarot2", 1, 0.4);
    }
    (void)sync_number_text;
    return true;
}

void update_hand_text(HandTextConfig cfg, HandTextVals vals) {
    HtCtx *c = CTX(HtCtx);
    c->cfg = cfg;
    c->vals = vals;
    if (vals.handname) c->vals.handname = intern(vals.handname);
    if (vals.level) c->vals.level = intern(vals.level);
    if (vals.chips_s) c->vals.chips_s = intern(vals.chips_s);
    if (vals.mult_s) c->vals.mult_s = intern(vals.mult_s);
    if (cfg.sound) c->cfg.sound = intern(cfg.sound);
    add_event(EVENT(.trigger = TRIG_BEFORE, .blockable = cfg.immediate ? 0 : 1, .delay = cfg.has_delay ? cfg.delay : 0.8, .func = ev_update_hand_text, .ctx = c));
}

/* ------------------------------------------------------------------ */
/* eval_card                                                           */
/* ------------------------------------------------------------------ */
CardEval eval_card(Card *card, JokerContext *ctx) {
    CardEval r = { 0 };
    JokerContext empty = { 0 };
    if (!ctx) ctx = &empty;
    if (ctx->repetition_only) {
        r.seals = Card_calculate_seal(card, ctx);
        return r;
    }
    if (ctx->cardarea == G.play) {
        double chips = Card_get_chip_bonus(card);
        if (chips > 0) { r.chips = chips; r.has_chips = true; }
        double mult = Card_get_chip_mult(card);
        if (mult > 0) { r.mult = mult; r.has_mult = true; }
        double xm = Card_get_chip_x_mult(card);
        if (xm > 0) { r.x_mult = xm; r.has_x_mult = true; }
        double pd = Card_get_p_dollars(card);
        if (pd > 0) { r.p_dollars = pd; r.has_p_dollars = true; }
        r.jokers = Card_calculate_joker(card, ctx);
        r.edition = Card_get_edition(card);
    }
    if (ctx->cardarea == G.hand) {
        double hm = Card_get_chip_h_mult(card);
        if (hm > 0) { r.h_mult = hm; r.has_h_mult = true; }
        double hxm = Card_get_chip_h_x_mult(card);
        if (hxm > 0) { r.x_mult = hxm; r.has_x_mult = true; }
        r.jokers = Card_calculate_joker(card, ctx);
    }
    if (ctx->cardarea == G.jokers || ctx->card == (Card *)G.consumeables) {
        if (ctx->edition) r.jokers = Card_get_edition(card);
        else if (ctx->other_joker) r.jokers = Card_calculate_joker(ctx->other_joker, ctx);
        else r.jokers = Card_calculate_joker(card, ctx);
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* alerts / main menu                                                  */
/* ------------------------------------------------------------------ */
void set_alerts(void) {
    if (!G.REFRESH_ALERTS) return;
    G.REFRESH_ALERTS = false;
    bool a_joker = false, a_voucher = false, a_tarot = false, a_planet = false, a_spectral = false, a_blind = false, a_edition = false, a_tag = false,
         a_seal = false, a_booster = false;
    for (int i = 0; i < G.n_centers; i++) {
        Center *v = &G.P_CENTERS_LIST[i];
        if (v->discovered && !v->alerted) {
            if (str_eq(v->set, "Voucher")) a_voucher = true;
            if (str_eq(v->set, "Tarot")) a_tarot = true;
            if (str_eq(v->set, "Planet")) a_planet = true;
            if (str_eq(v->set, "Spectral")) a_spectral = true;
            if (str_eq(v->set, "Joker")) a_joker = true;
            if (str_eq(v->set, "Edition")) a_edition = true;
            if (str_eq(v->set, "Booster")) a_booster = true;
        }
    }
    for (int i = 0; i < G.n_blinds; i++) if (G.P_BLINDS_LIST[i].discovered && !G.P_BLINDS_LIST[i].alerted) a_blind = true;
    for (int i = 0; i < G.n_tags; i++) if (G.P_TAGS_LIST[i].discovered && !G.P_TAGS_LIST[i].alerted) a_tag = true;
    for (int i = 0; i < G.n_seals; i++) if (G.P_SEALS_LIST[i].discovered && !G.P_SEALS_LIST[i].alerted) a_seal = true;
    bool any = a_voucher || a_joker || a_tarot || a_planet || a_spectral || a_blind || a_edition || a_seal || a_tag;
    static const char *ids[11] = { "your_collection", "your_collection_jokers", "your_collection_tarots", "your_collection_planets", "your_collection_spectrals",
                                   "your_collection_vouchers", "your_collection_editions", "your_collection_blinds", "your_collection_tags", "your_collection_seals",
                                   "your_collection_boosters" };
    bool should[11] = { any, a_joker, a_tarot, a_planet, a_spectral, a_voucher, a_edition, a_blind, a_tag, a_seal, a_booster };
    for (int k = 0; k < 11; k++) {
        UIElement *target = G.OVERLAY_MENU ? UIBox_get_UIE_by_ID(G.OVERLAY_MENU, ids[k], NULL) : NULL;
        UIBox **slot = &G.collection_alerts[k];
        if (target && should[k]) {
            if (!*slot) {
                *slot = uibox_new(CARD_ALERT(.red_bad = true), UIBCFG(.align = "tri", .has_offset = true, .offset = { 0.05, -0.05 }, .major = &target->n, .instance_type = "ALERT"));
                (*slot)->n.states.collide.can = false;
            }
        } else if (*slot) {
            node_remove(&(*slot)->n);
            *slot = NULL;
        }
    }
    if (G.MAIN_MENU_UI && any) {
        if (!G.collection_alert) {
            UIElement *cb = UIBox_get_UIE_by_ID(G.MAIN_MENU_UI, "collection_button", NULL);
            G.collection_alert = uibox_new(CARD_ALERT(), UIBCFG(.align = "tri", .has_offset = true, .offset = { 0.05, -0.05 }, .major = cb ? &cb->n : NULL));
            G.collection_alert->n.states.collide.can = false;
        }
    } else if (G.collection_alert) {
        node_remove(&G.collection_alert->n);
        G.collection_alert = NULL;
    }
}

EVFN(ev_profile_button) {
    (void)e_; (void)p_;
    G.PROFILE_BUTTON = uibox_new(create_UIBox_profile_button(),
                                 UIBCFG(.align = "bli", .has_offset = true, .offset = { -10, 0 }, .major = G.ROOM_ATTACH, .bond = BOND_WEAK));
    G.PROFILE_BUTTON->n.alignment.offset.x = 0;
    Moveable_align_to_major(&G.PROFILE_BUTTON->n);
    return true;
}

void set_main_menu_UI(void) {
    /* 3DS: centred (the bottom screen has nothing else on it) */
    G.MAIN_MENU_UI = uibox_new(create_UIBox_main_menu_buttons(),
                               UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, 10 }, .major = G.ROOM_ATTACH, .bond = BOND_WEAK));
    G.MAIN_MENU_UI->n.alignment.offset.y = 0;
    Moveable_align_to_major(&G.MAIN_MENU_UI->n);
    add_event(EVENT(.blockable = 0, .blocking = 0, .func = ev_profile_button));
    UIDef *lang = create_UIBox_language_button();
    if (lang) {
        UIBox *lb = uibox_new(lang, UIBCFG(.align = "bri", .has_offset = true, .offset = { 10, 0 }, .major = G.ROOM_ATTACH, .bond = BOND_WEAK));
        lb->n.alignment.offset.x = 0;
        Moveable_align_to_major(&lb->n);
    }
    UIElement *play = UIBox_get_UIE_by_ID(G.MAIN_MENU_UI, "main_menu_play", NULL);
    Controller_snap_to_node(&G.CONTROLLER, play ? &play->n : NULL);
}

/* ------------------------------------------------------------------ */
/* card_eval_status_text                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *text; double scale, hold; Col backdrop; const char *align; Node *card; double y_off; const char *sound; double pitch, volume; bool no_juice;
} StatusCtx;

static void status_fire(StatusCtx *c) {
    ATT(.text = c->text, .scale = c->scale, .hold = c->hold, .backdrop_colour = c->backdrop, .align = c->align, .major = c->card, .has_offset = true,
        .offset = { 0, c->y_off });
    sound_play(c->sound, c->pitch, c->volume);
    if (!c->no_juice) {
        node_juice_up(c->card, 0.6, 0.1);
        G.ROOM_jiggle += 0.7;
    }
}
EVFN(ev_status) { StatusCtx *c = p_; (void)e_; status_fire(c); return true; }

void card_eval_status_text(Node *cardn, const char *eval_type, double amt, double percent, const char *dir, StatusExtra *extra) {
    if (isnan(percent) || percent == 0) percent = 0.9 + 0.2 * math_random();
    if (str_eq(dir, "down")) percent = 1 - percent;
    if (extra && extra->focus) cardn = extra->focus;
    Card *card = node_is(cardn, NT_CARD) ? (Card *)cardn : NULL;
    const char *text = "";
    const char *sound = NULL;
    double volume = 1;
    const char *card_aligned = "bm";
    double y_off = 0.15 * G.CARD_H;
    CardArea *area = card ? card->area : NULL;
    if (area && (area == G.jokers || area == G.consumeables)) y_off = 0.05 * cardn->T->h;
    else if (area && area == G.hand) { y_off = -0.05 * G.CARD_H; card_aligned = "tm"; }
    else if (area && area == G.play) { y_off = -0.05 * G.CARD_H; card_aligned = "tm"; }
    else if (card && card->jimbo) { y_off = -0.05 * G.CARD_H; card_aligned = "tm"; }
    double scale = 1;
    double delay_v = 0.65;
    Col colour = (extra && extra->colour) ? extra->colour : G.C.FILTER;
    if (str_eq(eval_type, "debuff")) {
        sound = "cancel"; amt = 1; colour = G.C.RED; scale = 0.6; text = localize_key("k_debuffed");
    } else if (str_eq(eval_type, "chips")) {
        sound = "chips1"; colour = G.C.CHIPS; text = localize_variable("a_chips", LV(num_str(amt))); delay_v = 0.6;
    } else if (str_eq(eval_type, "mult")) {
        sound = "multhit1"; text = localize_variable("a_mult", LV(num_str(amt))); colour = G.C.MULT; scale = 0.7;
    } else if (str_eq(eval_type, "x_mult") || str_eq(eval_type, "h_x_mult")) {
        sound = "multhit2"; volume = 0.7; text = localize_variable("a_xmult", LV(num_str(amt))); colour = G.C.XMULT; scale = 0.7;
    } else if (str_eq(eval_type, "h_mult")) {
        sound = "multhit1"; text = localize_variable("a_mult", LV(num_str(amt))); colour = G.C.MULT; scale = 0.7;
    } else if (str_eq(eval_type, "dollars")) {
        sound = "coin3";
        text = tfmt("%s%s%s", amt < -0.01 ? "-" : "", localize_key("$"), num_str(fabs(amt)));
        colour = amt < -0.01 ? G.C.RED : G.C.MONEY;
    } else if (str_eq(eval_type, "swap")) {
        sound = "generic1"; text = localize_key("k_swapped_ex"); colour = G.C.PURPLE;
    } else if (str_eq(eval_type, "extra") || str_eq(eval_type, "jokers")) {
        StatusExtra none = { 0 };
        StatusExtra *x = extra ? extra : &none;
        sound = x->edition ? "foil2" : x->mult_mod ? "multhit1" : x->Xmult_mod ? "multhit2" : "generic1";
        if (x->edition) colour = G.C.DARK_EDITION;
        volume = x->edition ? 0.3 : !strcmp(sound, "multhit2") ? 0.7 : 1;
        delay_v = x->has_delay ? x->delay : 0.75;
        amt = 1;
        text = x->message ? x->message : text;
        if (!x->edition && (x->mult_mod || x->Xmult_mod)) colour = G.C.MULT;
        if (x->chip_mod) colour = G.C.CHIPS;
        else if (x->swap) colour = G.C.PURPLE;
        scale = 0.7;
    }
    delay_v = delay_v * 1.25;
    if (amt > 0 || amt < 0) {
        StatusCtx *c = CTX(StatusCtx);
        *c = (StatusCtx){ intern(text), scale, delay_v - 0.2, colour, card_aligned, cardn, y_off, sound, 0.8 + percent * 0.2, volume, extra && extra->no_juice };
        if (extra && extra->instant) { status_fire(c); free(c); }
        else add_event(EVENT(.trigger = TRIG_BEFORE, .delay = delay_v, .func = ev_status, .ctx = c));
    }
    if (extra && extra->playing_cards_created) playing_card_joker_effects(extra->playing_cards_created, extra->playing_cards_created->count);
}

/* ------------------------------------------------------------------ */
/* add_round_eval_row                                                  */
/* ------------------------------------------------------------------ */
typedef struct { RoundEvalRow cfg; double width, scale; int *dollar_row; int i; double num_dollars; } RowCtx;

static UIElement *eval_target(bool bonus) { return UIBox_get_UIE_by_ID(G.round_eval, bonus ? "bonus_round_eval" : "base_round_eval", NULL); }

EVFN(ev_eval_spacer) {
    RowCtx *c = p_; (void)e_;
    Language *en = NULL;
    for (int i = 0; i < G.n_languages; i++) if (str_eq(G.LANGUAGES[i].key, "en-us")) en = &G.LANGUAGES[i];
    UIDef *spacer = UIR(CFG(.align = "cm", .minw = c->width),
        UIO(CFG(.object = &DYNATEXT(DT_STR("......................................"), DT_COL1(G.C.WHITE), .shadow = true, .float_ = true, .y_offset = -30,
                                    .scale = 0.45, .spacing = 13.5, .font = en ? en->font : NULL, .pop_in = 0, .has_pop_in = true)->n)));
    UIBox_add_child(G.round_eval, spacer, eval_target(c->cfg.bonus));
    return true;
}

static DynaText *eval_dt(const char *s, Col col, double scale) {
    return DYNATEXT(DT_STR(s), DT_COL1(col), .shadow = true, .pop_in = 0, .has_pop_in = true, .scale = scale, .silent = true);
}

EVFN(ev_eval_row) {
    RowCtx *c = p_; (void)e_;
    RoundEvalRow *cfg = &c->cfg;
    double scale = c->scale;
    UIDef *left = uinodes();
    const char *name = cfg->name;
    if (str_eq(name, "blind1")) {
        Sprite *stake_sprite = get_stake_sprite(G.GAME.stake ? (int)G.GAME.stake : 1, 0.5);
        Sprite *bs = animated_sprite_new(0, 0, 1.2, 1.2, strmap_get(&G.ANIMATION_ATLAS, "blind_chips"), G.GAME.blind->pos);
        DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
        Sprite_define_draw_steps(bs, steps, 2);
        uidef_add(left, UIO(CFG(.w = 1.2, .h = 1.2, .object = &bs->n, .hover = true, .can_collide = -1)));
        if (cfg->saved) {
            uidef_add(left, UIC(CFG(.padding = 0.05, .align = "cm"),
                UIR(CFG(.align = "cm"), UIO(CFG(.object = &eval_dt(tfmt(" %s ", localize_key("ph_mr_bones")), G.C.FILTER, 0.5 * scale)->n)))));
        } else {
            uidef_add(left, UIC(CFG(.padding = 0.05, .align = "cm"),
                UIR(CFG(.align = "cm"), UIO(CFG(.object = &eval_dt(tfmt(" %s ", localize_key("ph_score_at_least")), G.C.UI.TEXT_LIGHT, 0.4 * scale)->n))),
                UIR(CFG(.align = "cm", .minh = 0.8),
                    UIO(CFG(.w = 0.5, .h = 0.5, .object = &stake_sprite->n, .hover = true, .can_collide = -1)),
                    UIT(CFG(.text = G.GAME.blind->chip_text, .scale = scale_number(G.GAME.blind->chips, scale, 100000), .colour = G.C.RED, .shadow = true)))));
        }
    } else if (strstr(name, "tag")) {
        Sprite *bs = sprite_new(0, 0, 0.7, 0.7, atlas_get("tags"), cfg->pos);
        DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
        Sprite_define_draw_steps(bs, steps, 2);
        node_juice_up(&bs->n, NAN, NAN);
        uidef_add(left, UIO(CFG(.w = 0.7, .h = 0.7, .object = &bs->n, .hover = true, .can_collide = -1)));
        uidef_add(left, UIO(CFG(.object = &eval_dt(cfg->condition ? cfg->condition : "", G.C.UI.TEXT_LIGHT, 0.4 * scale)->n)));
    } else if (str_eq(name, "hands")) {
        uidef_add(left, UIT(CFG(.text = (char *)num_str(cfg->has_disp ? cfg->disp : cfg->dollars), .scale = 0.8 * scale, .colour = G.C.BLUE, .shadow = true, .juice = true)));
        double mph = G.GAME.modifiers.money_per_hand ? G.GAME.modifiers.money_per_hand : 1;
        uidef_add(left, UIO(CFG(.object = &eval_dt(tfmt(" %s", localize_variable("remaining_hand_money", LV(num_str(mph)))), G.C.UI.TEXT_LIGHT, 0.4 * scale)->n)));
    } else if (str_eq(name, "discards")) {
        uidef_add(left, UIT(CFG(.text = (char *)num_str(cfg->has_disp ? cfg->disp : cfg->dollars), .scale = 0.8 * scale, .colour = G.C.RED, .shadow = true, .juice = true)));
        uidef_add(left, UIO(CFG(.object = &eval_dt(tfmt(" %s", localize_variable("remaining_discard_money", LV(num_str(G.GAME.modifiers.money_per_discard)))),
                                                   G.C.UI.TEXT_LIGHT, 0.4 * scale)->n)));
    } else if (strstr(name, "joker")) {
        Center *ce = cfg->card ? cfg->card->config.center : NULL;
        uidef_add(left, UIO(CFG(.object = &eval_dt(ce ? localize_name_text(ce->set, ce->key) : "", G.C.FILTER, 0.6 * scale)->n)));
    } else if (str_eq(name, "interest")) {
        uidef_add(left, UIT(CFG(.text = (char *)num_str(c->num_dollars), .scale = 0.8 * scale, .colour = G.C.MONEY, .shadow = true, .juice = true)));
        uidef_add(left, UIO(CFG(.object = &eval_dt(tfmt(" %s", localize_variable("interest", LV(num_str(G.GAME.interest_amount), "5",
                                                                                                  num_str(G.GAME.interest_amount * G.GAME.interest_cap / 5)))),
                                                   G.C.UI.TEXT_LIGHT, 0.4 * scale)->n)));
    }
    UIDef *full = UIR(CFG(.align = "cm", .minw = 5),
        uidef_add_all(UIC(CFG(.padding = 0.05, .minw = c->width * 0.55, .minh = 0.61, .align = "cl")), left),
        UIC(CFG(.padding = 0.05, .minw = c->width * 0.45, .align = "cr"), UIC(CFG(.align = "cm", .id = intern(tfmt("dollar_%s", name))))));
    if (str_eq(name, "blind1")) node_juice_up(&G.GAME.blind->n, NAN, NAN);
    UIBox_add_child(G.round_eval, full, eval_target(cfg->bonus));
    double pitch = cfg->has_pitch ? cfg->pitch : 1;
    sound_play("cancel", pitch, NAN);
    sound_play("highlight1", 1.5 * pitch, 0.2);
    if (cfg->card) node_juice_up(&cfg->card->n, 0.7, 0.46);
    return true;
}

EVFN(ev_eval_big_dollars) {
    RowCtx *c = p_; (void)e_;
    UIElement *dst = UIBox_get_UIE_by_ID(G.round_eval, tfmt("dollar_%s", c->cfg.name), NULL);
    UIBox_add_child(G.round_eval,
        UIR(CFG(.align = "cm", .id = intern(tfmt("dollar_row_%d_%s", *c->dollar_row + 1, c->cfg.name))),
            UIO(CFG(.object = &DYNATEXT(DT_STR(tfmt("%s%s", localize_key("$"), num_str(c->num_dollars))), DT_COL1(G.C.MONEY), .shadow = true, .pop_in = 0,
                                        .has_pop_in = true, .scale = 0.65, .float_ = true)->n))),
        dst);
    sound_play("coin3", 0.9 + 0.2 * math_random(), 0.7);
    sound_play("coin6", 1.3, 0.8);
    return true;
}

EVFN(ev_eval_dollar) {
    RowCtx *c = p_; (void)e_;
    double nd = c->num_dollars;
    const char *name = c->cfg.name;
    if (c->i % 30 == 1) {
        UIElement *dst = UIBox_get_UIE_by_ID(G.round_eval, tfmt("dollar_%s", name), NULL);
        UIBox_add_child(G.round_eval, UIR(CFG(.align = "cm", .id = intern(tfmt("dollar_row_%d_%s", *c->dollar_row + 1, name)))), dst);
        *c->dollar_row += 1;
    }
    UIDef *r = UIT(CFG(.text = (char *)localize_key("$"), .colour = G.C.MONEY, .scale = nd > 20 ? 0.28 : nd > 9 ? 0.43 : 0.58, .shadow = true, .hover = true,
                       .can_collide = -1, .juice = true));
    sound_play("coin3", 0.9 + 0.2 * math_random(), 0.7 - (nd > 20 ? 0.2 : 0));
    if (str_eq(name, "blind1")) {
        char **d = &G.GAME.current_round.dollars_to_be_earned;
        if (*d && **d) { char *rest = xstrdup(*d + 1); free(*d); *d = rest; }   /* Lua :sub(2) is byte based */
    }
    UIBox_add_child(G.round_eval, r, UIBox_get_UIE_by_ID(G.round_eval, tfmt("dollar_row_%d_%s", *c->dollar_row, name), NULL));
    G.VIBRATION += 0.4;
    return true;
}

EVFN(ev_eval_bottom) {
    RowCtx *c = p_; (void)e_;
    uibox_new(UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
                  UIR(CFG(.id = "cash_out_button", .align = "cm", .padding = 0.1, .minw = 7, .r = 0.15, .colour = G.C.ORANGE, .shadow = true, .hover = true,
                          .one_press = true, .button = G_FUNCS_cash_out, .focus_args = FOCUS(.snap_to = true)),
                      UIT(CFG(.text = (char *)tfmt("%s: ", localize_key("b_cash_out")), .scale = 1, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)),
                      UIT(CFG(.text = (char *)tfmt("%s%s", localize_key("$"), num_str(c->cfg.dollars)), .scale = 1.2 * c->scale, .colour = G.C.WHITE, .shadow = true,
                              .juice = true)))),
              UIBCFG(.align = "tmi", .has_offset = true, .offset = { 0, 0.4 }, .major = &G.round_eval->n));
    G.GAME.current_round.dollars = c->cfg.dollars;
    sound_play("coin6", c->cfg.has_pitch ? c->cfg.pitch : 1, NAN);
    G.VIBRATION += 1;
    return true;
}

void add_round_eval_row(RoundEvalRow cfg) {
    if (cfg.name) cfg.name = intern(cfg.name);
    if (cfg.condition) cfg.condition = intern(cfg.condition);
    double width = G.round_eval->n.T->w - 0.51;
    double num_dollars = cfg.has_dollars ? cfg.dollars : 1;
    double scale = 0.9;
    if (!str_eq(cfg.name, "bottom")) {
        if (!str_eq(cfg.name, "blind1")) {
            if (!G.round_eval_divider_added) {
                RowCtx *c = CTX(RowCtx); c->cfg = cfg; c->width = width; c->scale = scale;
                add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.25, .func = ev_eval_spacer, .ctx = c));
                delay(0.6);
                G.round_eval_divider_added = true;
            }
        } else delay(0.2);
        delay(0.2);
        RowCtx *c = CTX(RowCtx); c->cfg = cfg; c->width = width; c->scale = scale; c->num_dollars = num_dollars;
        add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.5, .func = ev_eval_row, .ctx = c));
        int *dollar_row = xcalloc(1, sizeof(int));
        if (num_dollars > 60) {
            RowCtx *b = CTX(RowCtx); *b = *c; b->dollar_row = dollar_row;
            add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.38, .func = ev_eval_big_dollars, .ctx = b));
        } else {
            for (int i = 1; i <= (int)num_dollars; i++) {
                RowCtx *d = CTX(RowCtx); *d = *c; d->dollar_row = dollar_row; d->i = i;
                add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.18 - (num_dollars > 20 ? 0.13 : num_dollars > 9 ? 0.1 : 0), .func = ev_eval_dollar, .ctx = d));
            }
        }
    } else {
        delay(0.4);
        RowCtx *c = CTX(RowCtx); c->cfg = cfg; c->scale = scale;
        add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.5, .func = ev_eval_bottom, .ctx = c));
    }
}

/* ------------------------------------------------------------------ */
/* shop size / juice                                                   */
/* ------------------------------------------------------------------ */
void change_shop_size(double mod) {
    G.GAME.shop.joker_max += mod;
    if (G.shop_jokers) {
        if (mod < 0) {
            for (int i = G.shop_jokers->cards.count; i >= (int)G.GAME.shop.joker_max + 1; i--)
                if (i - 1 < G.shop_jokers->cards.count) node_remove(&G.shop_jokers->cards.items[i - 1]->n);
        }
        G.shop_jokers->config.card_limit = G.GAME.shop.joker_max;
        G.shop_jokers->n.T->w = G.GAME.shop.joker_max * 1.01 * G.CARD_W;
        if (G.shop) UIBox_recalculate(G.shop);
        if (mod > 0) {
            int n = (int)G.GAME.shop.joker_max - G.shop_jokers->cards.count;
            for (int i = 1; i <= n; i++) {
                Card *c = create_card_for_shop(G.shop_jokers);
                if (c) CardArea_emplace(G.shop_jokers, c, NULL, false);
            }
        }
    }
}

typedef struct { Card *card; } JCardCtx;
EVFN(ev_juice_card) { JCardCtx *c = p_; (void)e_; node_juice_up(&c->card->n, 0.7, NAN); return true; }
void juice_card(Card *card) {
    JCardCtx *c = CTX(JCardCtx); c->card = card;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_juice_card, .ctx = c));
}

void update_canvas_juice(double dt) {
    G.JIGGLE_VIBRATION = G.ROOM_jiggle;
    if (G.SETTINGS.screenshake < 0) G.SETTINGS.screenshake = G.SETTINGS.reduced_motion ? 0 : 50;
    double rm = G.SETTINGS.reduced_motion ? 0 : 1;
    double shake_amt = rm * MAX(0, G.SETTINGS.screenshake - 30) / 100;
    if (!G.ARGS_eased_cursor_set) {
        G.ARGS_eased_cursor_pos.x = G.CURSOR->n.T->x;
        G.ARGS_eased_cursor_pos.y = G.CURSOR->n.T->y;
        G.ARGS_eased_cursor_sx = G.CONTROLLER.cursor_position.x;
        G.ARGS_eased_cursor_sy = G.CONTROLLER.cursor_position.y;
        G.ARGS_eased_cursor_set = true;
    }
    G.ARGS_eased_cursor_pos.x = G.ARGS_eased_cursor_pos.x * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CURSOR->n.T->x + (1 - shake_amt) * G.ROOM->T->w / 2);
    G.ARGS_eased_cursor_pos.y = G.ARGS_eased_cursor_pos.y * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CURSOR->n.T->y + (1 - shake_amt) * G.ROOM->T->h / 2);
    G.ARGS_eased_cursor_sx = G.ARGS_eased_cursor_sx * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CONTROLLER.cursor_position.x + (1 - shake_amt) * G.WINDOWTRANS.real_window_w / 2);
    G.ARGS_eased_cursor_sy = G.ARGS_eased_cursor_sy * (1 - 3 * dt) + 3 * dt * (shake_amt * G.CONTROLLER.cursor_position.y + (1 - shake_amt) * G.WINDOWTRANS.real_window_h / 2);
    shake_amt = rm * G.SETTINGS.screenshake / 100 * 3;
    if (shake_amt < 0.05) shake_amt = 0;
    G.ROOM_jiggle = G.ROOM_jiggle * (1 - 5 * dt) * (shake_amt > 0.05 ? 1 : 0);
    double t = G.TIMERS.REAL;
    G.ROOM->T->r = (0.001 * sin(0.3 * t) + 0.002 * G.ROOM_jiggle * sin(39.913 * t)) * shake_amt;
    G.ROOM->T->x = G.ROOM_ORIG.x + shake_amt * (0.015 * sin(0.913 * t) + 0.01 * (G.ROOM_jiggle * shake_amt) * sin(19.913 * t) +
                                                (G.ARGS_eased_cursor_pos.x - 0.5 * (G.ROOM->T->w + G.ROOM_ORIG.x)) * 0.01);
    G.ROOM->T->y = G.ROOM_ORIG.y + shake_amt * (0.015 * sin(0.952 * t) + 0.01 * (G.ROOM_jiggle * shake_amt) * sin(21.913 * t) +
                                                (G.ARGS_eased_cursor_pos.y - 0.5 * (G.ROOM->T->h + G.ROOM_ORIG.y)) * 0.01);
    G.JIGGLE_VIBRATION = G.JIGGLE_VIBRATION * (1 - 5 * dt);
    G.CURR_VIBRATION = MIN(1, G.CURR_VIBRATION + G.VIBRATION + G.JIGGLE_VIBRATION * 0.2);
    G.VIBRATION = 0;
    G.CURR_VIBRATION = (1 - 15 * dt) * G.CURR_VIBRATION;
    if (!G.SETTINGS.rumble) G.CURR_VIBRATION = 0;
}

typedef struct { Card *card; JuiceEval eval; void *ctx; bool first; } JuiceUntilCtx;
EVFN(ev_juice_until) {
    JuiceUntilCtx *c = p_; (void)e_;
    if (c->eval(c->card, c->ctx)) {
        node_juice_up(&c->card->n, 0.1, 0.1);
        juice_card_until(c->card, c->eval, c->ctx, false, 0.8);
    }
    return true;
}
void juice_card_until(Card *card, JuiceEval eval, void *ctx, bool first, double delay_v) {
    JuiceUntilCtx *c = CTX(JuiceUntilCtx); c->card = card; c->eval = eval; c->ctx = ctx; c->first = first;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = isnan(delay_v) ? 0.1 : delay_v, .blocking = 0, .blockable = 0, .timer = TIMER_REAL, .func = ev_juice_until, .ctx = c));
}
