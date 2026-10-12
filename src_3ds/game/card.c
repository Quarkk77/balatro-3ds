/* card.c - port of card.lua (everything except calculate_joker/seal and the
 * ability table generation, which live in card_joker.c / card_ui.c) */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define EVCTX(T) T *c = (T *)p_; (void)e_
typedef struct { Card *card; Card *other; double a, b; int i; const char *s; void *p; } CardCtx;

static Sprite *CH(Card *c, const char *name) { return (Sprite *)child_get(&c->n, name); }
bool card_name_is(Card *c, const char *name) { return c && str_eq(c->ability.name, name); }

static Col *cols_dup(Col *cols, int n) {
    if (!cols || n <= 0) return NULL;
    Col *r = xmalloc((size_t)n * sizeof(Col));
    memcpy(r, cols, (size_t)n * sizeof(Col));
    return r;
}

/* ------------------------------------------------------------------ */
/* construction                                                        */
/* ------------------------------------------------------------------ */
Card *card_new(double x, double y, double w, double h, PCard *card, Center *center, const CardParams *params) {
    Card *c = NEW(Card);
    if (params) c->params = *params;
    moveable_init(&c->n, &CARD_VT, x, y, w, h);
    c->n.CT = &c->n.VT;
    c->config.card = card ? card : &G.P_CARD_EMPTY;
    c->config.center = center;
    c->n.tilt_var.set = true;
    c->n.tilt_var.mx = c->n.tilt_var.my = c->n.tilt_var.dx = c->n.tilt_var.dy = c->n.tilt_var.amt = 0;
    c->n.ambient_tilt = 0.2;
    c->n.states.collide.can = true;
    c->n.states.hover.can = true;
    c->n.states.drag.can = true;
    c->n.states.click.can = true;
    c->playing_card = c->params.playing_card;
    G.sort_id = G.sort_id + 1;
    c->sort_id = G.sort_id;
    c->bypass_discovery_center = c->params.bypass_discovery_center;
    c->bypass_discovery_ui = c->params.bypass_discovery_ui;
    c->bypass_lock = c->params.bypass_lock;
    c->no_ui = c->config.card && c->config.card->no_ui;
    c->base_cost = c->extra_cost = c->cost = c->sell_cost = 0;
    c->sell_cost_label = 0;
    child_set(&c->n, "shadow", moveable_new(0, 0, 0, 0));
    c->unique_val = 1 - c->n.ID / 1603301.0;
    c->edition = NULL;
    c->n.zoom = true;
    Card_set_ability(c, center, true, false);
    Card_set_base(c, card, true);
    c->discard_pos.r = 3.6 * (math_random() - 0.5);
    c->discard_pos.x = math_random();
    c->discard_pos.y = math_random();
    c->facing = FACE_FRONT;
    c->sprite_facing = FACE_FRONT;
    c->flipping = FLIP_NONE;
    c->area = NULL;
    c->highlighted = false;
    c->n.click_timeout = 0.3;
    c->n.T->scale = 0.95;
    c->debuff = false;
    c->rank = 0;
    c->added_to_deck = false;
    Sprite *f = CH(c, "front"), *b = CH(c, "back"), *ce = CH(c, "center");
    if (f) f->n.VT.w = 0;
    if (b) b->n.VT.w = 0;
    if (ce) ce->n.VT.w = 0;
    if (f) { f->n.parent = &c->n; f->n.layered_parallax = (Vec2){ 0, 0 }; }
    if (b) { b->n.parent = &c->n; b->n.layered_parallax = (Vec2){ 0, 0 }; }
    if (ce) { ce->n.parent = &c->n; ce->n.layered_parallax = (Vec2){ 0, 0 }; }
    Card_set_cost(c);
    vec_push(&G.I.CARD, &c->n);
    return c;
}

void Card_update_alert(Card *c) {
    const char *set = c->ability.set;
    if (str_eq(set, "Joker") || str_eq(set, "Voucher") || c->ability.consumeable || str_eq(set, "Edition") || str_eq(set, "Booster")) {
        if (c->area && c->area->config.collection && c->config.center) {
            Node *alert = child_get(&c->n, "alert");
            if (c->config.center->alerted && alert) {
                child_set(&c->n, "alert", NULL);
                node_remove(alert);
            } else if (!c->config.center->alerted && !alert && c->config.center->discovered) {
                bool odd_voucher = str_eq(set, "Voucher") && lua_mod(c->config.center->order, 2) == 1;
                UIBox *b = uibox_new(CARD_ALERT(),
                                     UIBCFG(.align = odd_voucher ? "tli" : "tri", .has_offset = true,
                                            .offset = { odd_voucher ? 0.1 : -0.1, 0.1 }, .parent = &c->n));
                child_set(&c->n, "alert", &b->n);
            }
        }
    }
}

void Card_set_base(Card *c, PCard *card, bool initial) {
    if (!card) card = &G.P_CARD_EMPTY;
    c->config.card = card;
    c->config.card_key = card->empty ? NULL : card->key;
    if (!card->empty) Card_set_sprites(c, NULL, card);
    double sno = 0; bool has_sno = false;
    if (c->base.has_suit_nominal_original) { sno = c->base.suit_nominal_original; has_sno = true; }
    const char *orig = c->base.original_value;
    memset(&c->base, 0, sizeof c->base);
    c->base.name = card->name;
    c->base.suit = card->suit;
    c->base.value = card->value;
    c->base.colour = suit_colour(card->suit);
    c->base.original_value = orig;
    const char *v = c->base.value;
    if (v) {
        if (!strcmp(v, "2")) { c->base.nominal = 2; c->base.id = 2; }
        else if (!strcmp(v, "3")) { c->base.nominal = 3; c->base.id = 3; }
        else if (!strcmp(v, "4")) { c->base.nominal = 4; c->base.id = 4; }
        else if (!strcmp(v, "5")) { c->base.nominal = 5; c->base.id = 5; }
        else if (!strcmp(v, "6")) { c->base.nominal = 6; c->base.id = 6; }
        else if (!strcmp(v, "7")) { c->base.nominal = 7; c->base.id = 7; }
        else if (!strcmp(v, "8")) { c->base.nominal = 8; c->base.id = 8; }
        else if (!strcmp(v, "9")) { c->base.nominal = 9; c->base.id = 9; }
        else if (!strcmp(v, "10")) { c->base.nominal = 10; c->base.id = 10; }
        else if (!strcmp(v, "Jack")) { c->base.nominal = 10; c->base.face_nominal = 0.1; c->base.id = 11; }
        else if (!strcmp(v, "Queen")) { c->base.nominal = 10; c->base.face_nominal = 0.2; c->base.id = 12; }
        else if (!strcmp(v, "King")) { c->base.nominal = 10; c->base.face_nominal = 0.3; c->base.id = 13; }
        else if (!strcmp(v, "Ace")) { c->base.nominal = 11; c->base.face_nominal = 0.4; c->base.id = 14; }
    }
    if (initial) c->base.original_value = c->base.value;
    const char *s = c->base.suit;
    if (s) {
        if (!strcmp(s, "Diamonds")) { c->base.suit_nominal = 0.01; c->base.suit_nominal_original = has_sno ? sno : 0.001; c->base.has_suit_nominal_original = true; }
        else if (!strcmp(s, "Clubs")) { c->base.suit_nominal = 0.02; c->base.suit_nominal_original = has_sno ? sno : 0.002; c->base.has_suit_nominal_original = true; }
        else if (!strcmp(s, "Hearts")) { c->base.suit_nominal = 0.03; c->base.suit_nominal_original = has_sno ? sno : 0.003; c->base.has_suit_nominal_original = true; }
        else if (!strcmp(s, "Spades")) { c->base.suit_nominal = 0.04; c->base.suit_nominal_original = has_sno ? sno : 0.004; c->base.has_suit_nominal_original = true; }
    }
    if (!initial) Blind_debuff_card(G.GAME.blind, c, false);
    if (c->playing_card && !initial) UNLOCK(.type = "modify_deck");
}

static Sprite *card_sprite(Card *c, Atlas *atlas, SpritePos pos) {
    Sprite *s = sprite_new(c->n.T->x, c->n.T->y, c->n.T->w, c->n.T->h, atlas, pos);
    s->n.states.hover = c->n.states.hover;
    s->n.states.click = c->n.states.click;
    s->n.states.drag = c->n.states.drag;
    s->n.states.collide.can = false;
    Moveable_set_role(&s->n, ROLE_ARGS(.major = &c->n, .role_type = ROLE_GLUED, .draw_major = &c->n));
    return s;
}

void Card_set_sprites(Card *c, Center *center, PCard *front) {
    if (front) {
        Atlas *atlas; SpritePos pos;
        get_front_spriteinfo(front, &atlas, &pos);
        Sprite *f = CH(c, "front");
        if (f) {
            f->atlas = atlas;
            Sprite_set_sprite_pos(f, pos);
        } else {
            child_set(&c->n, "front", &card_sprite(c, atlas, pos)->n);
        }
    }
    if (center) {
        if (center->set) {
            Sprite *ce = CH(c, "center");
            if (ce) {
                const char *an = center->atlas ? center->atlas :
                    ((str_eq(center->set, "Joker") || center->consumeable || str_eq(center->set, "Voucher")) ? center->set : "centers");
                ce->atlas = atlas_get(an);
                Sprite_set_sprite_pos(ce, center->pos);
            } else {
                Sprite *s;
                Center *cc = c->config.center;
                if (str_eq(center->set, "Joker") && !center->unlocked && !c->params.bypass_discovery_center)
                    s = card_sprite(c, atlas_get("Joker"), G.j_locked.pos);
                else if (str_eq(cc->set, "Voucher") && !cc->unlocked && !c->params.bypass_discovery_center)
                    s = card_sprite(c, atlas_get("Voucher"), G.v_locked.pos);
                else if (cc->consumeable && cc->demo)
                    s = card_sprite(c, atlas_get("Tarot"), G.c_locked.pos);
                else if (!c->params.bypass_discovery_center &&
                         (str_eq(center->set, "Edition") || str_eq(center->set, "Joker") || center->consumeable || str_eq(center->set, "Voucher") || str_eq(center->set, "Booster")) &&
                         !center->discovered) {
                    SpritePos p = G.booster_undiscovered.pos;
                    if (str_eq(center->set, "Joker")) p = G.j_undiscovered.pos;
                    else if (str_eq(center->set, "Edition")) p = G.j_undiscovered.pos;
                    else if (str_eq(center->set, "Tarot")) p = G.t_undiscovered.pos;
                    else if (str_eq(center->set, "Planet")) p = G.p_undiscovered.pos;
                    else if (str_eq(center->set, "Spectral")) p = G.s_undiscovered.pos;
                    else if (str_eq(center->set, "Voucher")) p = G.v_undiscovered.pos;
                    s = card_sprite(c, atlas_get(center->atlas ? center->atlas : center->set), p);
                } else if (str_eq(center->set, "Joker") || center->consumeable || str_eq(center->set, "Voucher"))
                    s = card_sprite(c, atlas_get(center->set), cc->pos);
                else
                    s = card_sprite(c, atlas_get(center->atlas ? center->atlas : "centers"), center->pos);
                child_set(&c->n, "center", &s->n);
            }
            Sprite *s = CH(c, "center");
            bool disc = center->discovered || c->bypass_discovery_center;
            if (str_eq(center->name, "Half Joker") && disc) s->scale.y = s->scale.y / 1.7;
            if (str_eq(center->name, "Photograph") && disc) s->scale.y = s->scale.y / 1.2;
            if (str_eq(center->name, "Square Joker") && disc) s->scale.y = s->scale.x;
        }
        if (center->has_soul_pos) {
            Sprite *fs = sprite_new(c->n.T->x, c->n.T->y, c->n.T->w, c->n.T->h, atlas_get("Joker"), c->config.center->soul_pos);
            fs->n.role.draw_major = &c->n;
            fs->n.states.hover.can = false;
            fs->n.states.click.can = false;
            child_set(&c->n, "floating_sprite", &fs->n);
        }
        if (!CH(c, "back")) {
            SpritePos bp;
            if (c->params.has_bypass_back) bp = c->params.bypass_back;
            else if (c->playing_card) {
                Back *back = c->params.viewed_back ? G.GAME.viewed_back : G.GAME.selected_back;
                bp = back ? back->pos : P_CENTER("b_red")->pos;
            } else bp = P_CENTER("b_red")->pos;
            child_set(&c->n, "back", &card_sprite(c, atlas_get("centers"), bp)->n);
        }
    }
}

typedef struct { Card *card; Center *center; } SpritesCtx;
EVFN(ev_delay_sprites) {
    EVCTX(SpritesCtx);
    if (!c->card->n.REMOVED) Card_set_sprites(c->card, c->center, NULL);
    return true;
}

void Card_set_ability(Card *c, Center *center, bool initial, bool delay_sprites) {
    double W = c->n.T->w, H = c->n.T->h;
    Center *old_center = c->config.center;
    c->config.center = center;
    c->sticker_run = NULL;
    c->config.center_key = center->key;
    if (c->params.discover && !center->discovered) {
        unlock_card(center);
        discover_card(center);
    }
    bool disc = center->discovered || c->bypass_discovery_center;
    if (str_eq(center->name, "Half Joker") && disc) { H = H / 1.7; c->n.T->h = H; }
    if (str_eq(center->name, "Photograph") && disc) { H = H / 1.2; c->n.T->h = H; }
    if (str_eq(center->name, "Square Joker") && disc) { H = W; c->n.T->h = H; }
    if (str_eq(center->name, "Wee Joker") && disc) { H = H * 0.7; W = W * 0.7; c->n.T->h = H; c->n.T->w = W; }
    if (delay_sprites) {
        SpritesCtx *sc = CTX(SpritesCtx);
        sc->card = c; sc->center = center;
        add_event(EVENT(.func = ev_delay_sprites, .ctx = sc));
    } else Card_set_sprites(c, center, NULL);

    bool had_ability = c->ability.name != NULL;
    if (had_ability && old_center && old_center->config.has_bonus) c->ability.bonus = c->ability.bonus - old_center->config.bonus;
    bool forced = had_ability && c->ability.forced_selection;
    double perma = had_ability ? c->ability.perma_bonus : 0;
    double bonus = had_ability ? c->ability.bonus : 0;
    Ability a;
    memset(&a, 0, sizeof a);
    CenterConfig *cfg = &center->config;
    a.name = center->name;
    a.effect = center->effect;
    a.set = center->set;
    a.mult = cfg->mult;
    a.h_mult = cfg->h_mult;
    a.h_x_mult = cfg->h_x_mult;
    a.h_dollars = cfg->h_dollars;
    a.p_dollars = cfg->p_dollars;
    a.t_mult = cfg->t_mult;
    a.t_chips = cfg->t_chips;
    a.x_mult = cfg->has_Xmult ? cfg->Xmult : 1;
    a.h_size = cfg->h_size;
    a.d_size = cfg->d_size;
    a.extra = cfg->extra;
    a.extra_value = 0;
    a.type = cfg->type ? cfg->type : "";
    a.order = center->order;
    a.has_order = center->has_order;
    a.forced_selection = forced;
    a.perma_bonus = perma;
    a.bonus = bonus + cfg->bonus;
    if (center->consumeable) a.consumeable = &center->config;
    c->ability = a;

    if (card_name_is(c, "Gold Card") && str_eq(c->seal, "Gold") && c->playing_card) UNLOCK(.type = "double_gold");
    if (card_name_is(c, "Invisible Joker")) { c->ability.invis_rounds = 0; c->ability.has_invis_rounds = true; }
    if (card_name_is(c, "To Do List")) {
        const char *hands[HAND_COUNT]; int n = 0;
        for (int i = 0; i < HAND_COUNT; i++) {
            int hi = hand_index(DATA_HANDS_PAIRS_ORDER[i]);
            if (G.GAME.hands[hi].visible) hands[n++] = HAND_NAMES[hi];
        }
        const char *old = c->ability.to_do_poker_hand;
        c->ability.to_do_poker_hand = NULL;
        bool title = c->area && str_eq(c->area->config.type, "title");
        while (!c->ability.to_do_poker_hand) {
            int idx = pseudorandom_index(n, pseudoseed(title ? "false_to_do" : "to_do"), true);
            c->ability.to_do_poker_hand = hands[idx - 1];
            if (str_eq(c->ability.to_do_poker_hand, old)) c->ability.to_do_poker_hand = NULL;
        }
    }
    if (card_name_is(c, "Caino")) { c->ability.caino_xmult = 1; c->ability.has_caino_xmult = true; }
    if (card_name_is(c, "Yorick")) { c->ability.yorick_discards = c->ability.extra.discards; c->ability.has_yorick_discards = true; }
    if (card_name_is(c, "Loyalty Card")) {
        c->ability.burnt_hand = 0;
        c->ability.loyalty_remaining = c->ability.extra.every;
        c->ability.has_loyalty_remaining = true;
    }
    c->base_cost = center->has_cost ? center->cost : 1;
    c->ability.hands_played_at_create = G.GAME.hands_played;
    c->label = center->label ? center->label : (c->config.card && c->config.card->label ? c->config.card->label : c->ability.set);
    const char *set = c->ability.set;
    if (str_eq(set, "Joker") || str_eq(set, "Edition") || c->ability.consumeable || str_eq(set, "Voucher")) c->label = c->ability.name;
    if (str_eq(set, "Booster")) { c->label = c->ability.name; c->n.mouse_damping = 1.5; }
    if (!G.OVERLAY_MENU) {
        for (int i = 0; i < G.n_centers; i++)
            if (str_eq(G.P_CENTERS_LIST[i].name, c->ability.name)) strset_add(&G.GAME.used_jokers, G.P_CENTERS_LIST[i].key);
    }
    if (G.jokers && c->area == G.jokers) UNLOCK(.type = "modify_jokers");
    if (G.consumeables && c->area == G.consumeables) UNLOCK(.type = "modify_jokers");
    if (!initial) Blind_debuff_card(G.GAME.blind, c, false);
    if (c->playing_card && !initial) UNLOCK(.type = "modify_deck");
}

void Card_set_cost(Card *c) {
    c->extra_cost = 0 + G.GAME.inflation;
    if (c->edition) c->extra_cost += (c->edition->holo ? 3 : 0) + (c->edition->foil ? 2 : 0) + (c->edition->polychrome ? 5 : 0) + (c->edition->negative ? 5 : 0);
    c->cost = MAX(1, floor((c->base_cost + c->extra_cost + 0.5) * (100 - G.GAME.discount_percent) / 100));
    if (str_eq(c->ability.set, "Booster") && G.GAME.modifiers.booster_ante_scaling) c->cost = c->cost + G.GAME.round_resets.ante - 1;
    if (str_eq(c->ability.set, "Booster") && !G.SETTINGS.tutorial_complete && G.SETTINGS.tutorial_progress.t == DV_TABLE &&
        !dv_truthy(dv_get(dv_get(&G.SETTINGS.tutorial_progress, "completed_parts"), "shop_1")))
        c->cost = c->cost + 3;
    if ((str_eq(c->ability.set, "Planet") || (str_eq(c->ability.set, "Booster") && str_contains(c->ability.name, "Celestial"))) && count_joker("Astronomer") > 0) c->cost = 0;
    if (c->ability.rental) c->cost = 1;
    c->sell_cost = MAX(1, floor(c->cost / 2)) + c->ability.extra_value;
    if (c->area && c->ability.couponed && (c->area == G.shop_jokers || c->area == G.shop_booster)) c->cost = 0;
    c->sell_cost_label_q = c->facing == FACE_BACK;
    c->sell_cost_label = c->sell_cost;
    snprintf(c->sell_cost_label_str, sizeof c->sell_cost_label_str, "%s", c->sell_cost_label_q ? "?" : num_str(c->sell_cost));
}

EVFN(ev_edition_juice) {
    EVCTX(CardCtx);
    Card *cd = c->card;
    Card_juice_up(&cd->n, 1, 0.5);
    if (cd->edition) {
        if (cd->edition->foil) sound_play("foil1", 1.2, 0.4);
        if (cd->edition->holo) sound_play("holo1", 1.2 * 1.58, 0.4);
        if (cd->edition->polychrome) sound_play("polychrome1", 1.2, 0.7);
        if (cd->edition->negative) sound_play("negative", 1.5, 0.4);
    }
    return true;
}
EVFN(ev_edition_unlock) { (void)e_; (void)p_; strset_del(&G.CONTROLLER.locks.other, "edition"); return true; }

void Card_set_edition(Card *c, EditionReq ed, bool immediate, bool silent) {
    c->edition = NULL;
    if (!ed.any && !ed.foil && !ed.holo && !ed.polychrome && !ed.negative) return;
    if (ed.holo) {
        c->edition = NEW(Edition);
        c->edition->mult = P_CENTER("e_holo")->config.extra.num; c->edition->has_mult = true;
        c->edition->holo = true;
        c->edition->type = "holo";
    } else if (ed.foil) {
        c->edition = NEW(Edition);
        c->edition->chips = P_CENTER("e_foil")->config.extra.num; c->edition->has_chips = true;
        c->edition->foil = true;
        c->edition->type = "foil";
    } else if (ed.polychrome) {
        c->edition = NEW(Edition);
        c->edition->x_mult = P_CENTER("e_polychrome")->config.extra.num; c->edition->has_x_mult = true;
        c->edition->polychrome = true;
        c->edition->type = "polychrome";
    } else if (ed.negative) {
        c->edition = NEW(Edition);
        if (c->added_to_deck) {
            if (c->ability.consumeable) G.consumeables->config.card_limit += 1;
            else G.jokers->config.card_limit += 1;
        }
        c->edition->negative = true;
        c->edition->type = "negative";
    }
    if (c->area && c->area == G.jokers) {
        if (c->edition) {
            Center *ec = P_CENTER(tfmt("e_%s", c->edition->type));
            if (ec && !ec->discovered) discover_card(ec);
        } else {
            Center *ec = P_CENTER("e_base");
            if (!ec->discovered) discover_card(ec);
        }
    }
    if (c->edition && !silent) {
        strset_add(&G.CONTROLLER.locks.other, "edition");
        CardCtx *cc = CTX(CardCtx);
        cc->card = c;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = !immediate ? 0.2 : 0, .blockable = !immediate, .func = ev_edition_juice, .ctx = cc));
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_edition_unlock));
    }
    if (G.jokers && c->area == G.jokers) UNLOCK(.type = "modify_jokers");
    Card_set_cost(c);
}

EVFN(ev_seal_juice) {
    EVCTX(CardCtx);
    Card_juice_up(&c->card->n, 0.3, 0.3);
    sound_play("gold_seal", 1.2, 0.4);
    return true;
}
EVFN(ev_seal_unlock) { (void)e_; (void)p_; strset_del(&G.CONTROLLER.locks.other, "seal"); return true; }

void Card_set_seal(Card *c, const char *seal, bool silent, bool immediate) {
    c->seal = NULL;
    if (seal) {
        c->seal = intern(seal);
        if (!silent) {
            strset_add(&G.CONTROLLER.locks.other, "seal");
            if (immediate) {
                Card_juice_up(&c->n, 0.3, 0.3);
                sound_play("gold_seal", 1.2, 0.4);
                strset_del(&G.CONTROLLER.locks.other, "seal");
            } else {
                CardCtx *cc = CTX(CardCtx);
                cc->card = c;
                add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.3, .func = ev_seal_juice, .ctx = cc));
                add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.15, .func = ev_seal_unlock));
            }
        }
    }
    if (card_name_is(c, "Gold Card") && str_eq(c->seal, "Gold") && c->playing_card) UNLOCK(.type = "double_gold");
    Card_set_cost(c);
}

const char *Card_get_seal(Card *c, bool bypass_debuff) {
    if (c->debuff && !bypass_debuff) return NULL;
    return c->seal;
}

void Card_set_eternal(Card *c, bool v) {
    c->ability.eternal = false;
    if (c->config.center->eternal_compat && !c->ability.perishable) c->ability.eternal = v;
}
void Card_set_perishable(Card *c, bool v) {
    (void)v;
    c->ability.perishable = false;
    if (c->config.center->perishable_compat && !c->ability.eternal) {
        c->ability.perishable = true;
        c->ability.perish_tally = G.GAME.perishable_rounds;
    }
}
void Card_set_rental(Card *c, bool v) {
    c->ability.rental = v;
    Card_set_cost(c);
}

void Card_set_debuff(Card *c, bool should) {
    if (c->ability.perishable && c->ability.perish_tally <= 0) {
        if (!c->debuff) {
            c->debuff = true;
            if (c->area == G.jokers) Card_remove_from_deck(c, true);
        }
        return;
    }
    if (should != c->debuff) {
        if (c->area == G.jokers) {
            if (should) Card_remove_from_deck(c, true);
            else Card_add_to_deck(c, true);
        }
        c->debuff = should;
    }
}

void Card_remove_UI(Card *c) {
    c->ability_UIBox_table = NULL;
    c->n.h_popup = NULL;
    c->n.h_popup_config = NULL;
    c->no_ui = true;
}

void Card_change_suit(Card *c, const char *new_suit) {
    const char *code = !strcmp(new_suit, "Diamonds") ? "D_" : !strcmp(new_suit, "Spades") ? "S_" : !strcmp(new_suit, "Clubs") ? "C_" : "H_";
    const char *v = c->base.value;
    const char *nv = !strcmp(v, "Ace") ? "A" : !strcmp(v, "King") ? "K" : !strcmp(v, "Queen") ? "Q" : !strcmp(v, "Jack") ? "J" : !strcmp(v, "10") ? "T" : v;
    Card_set_base(c, P_CARD(tfmt("%s%s", code, nv)), false);
    Blind_debuff_card(G.GAME.blind, c, false);
}

EVFN(ev_set_costs_all) {
    (void)e_; (void)p_;
    for (int i = 0; i < G.I.CARD.count; i++) {
        Node *n = G.I.CARD.items[i];
        if (IS(n, NT_CARD)) Card_set_cost((Card *)n);
    }
    return true;
}
EVFN(ev_blind_reset) {
    (void)e_; (void)p_;
    Blind_set_blind(G.GAME.blind, NULL, true, false);
    return true;
}

void Card_add_to_deck(Card *c, bool from_debuff) {
    if (!c->config.center->discovered) discover_card(c->config.center);
    if (c->added_to_deck) return;
    c->added_to_deck = true;
    if (str_eq(c->ability.set, "Enhanced") || str_eq(c->ability.set, "Default")) {
        if (card_name_is(c, "Gold Card") && str_eq(c->seal, "Gold") && c->playing_card) UNLOCK(.type = "double_gold");
        return;
    }
    if (c->edition) {
        Center *ec = P_CENTER(tfmt("e_%s", c->edition->type));
        if (ec && !ec->discovered) discover_card(ec);
    } else {
        Center *ec = P_CENTER("e_base");
        if (!ec->discovered) discover_card(ec);
    }
    if (c->ability.h_size != 0) CardArea_change_size(G.hand, c->ability.h_size);
    if (c->ability.d_size > 0) {
        G.GAME.round_resets.discards += c->ability.d_size;
        ease_discard(c->ability.d_size, false, false);
    }
    if (card_name_is(c, "Credit Card")) G.GAME.bankrupt_at -= c->ability.extra.num;
    if (card_name_is(c, "Chicot") && G.GAME.blind && G.GAME.blind->boss && !G.GAME.blind->disabled) {
        Blind_disable(G.GAME.blind);
        sound_play("timpani", NAN, NAN);
        card_eval_status_text(&c->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("ph_boss_disabled")));
    }
    if (card_name_is(c, "Chaos the Clown")) {
        G.GAME.current_round.free_rerolls += 1;
        calculate_reroll_cost(true);
    }
    if (card_name_is(c, "Turtle Bean")) CardArea_change_size(G.hand, c->ability.extra.h_size);
    if (card_name_is(c, "Oops! All 6s")) G.GAME.probabilities.normal *= 2;
    if (card_name_is(c, "To the Moon")) G.GAME.interest_amount += c->ability.extra.num;
    if (card_name_is(c, "Astronomer")) add_event(EVENT(.func = ev_set_costs_all));
    if (card_name_is(c, "Troubadour")) {
        CardArea_change_size(G.hand, c->ability.extra.h_size);
        G.GAME.round_resets.hands += c->ability.extra.h_plays;
    }
    if (card_name_is(c, "Stuntman")) CardArea_change_size(G.hand, -c->ability.extra.h_size);
    if (c->edition && c->edition->negative) {
        if (from_debuff) c->ability.queue_negative_removal = false;
        else {
            if (c->ability.consumeable) G.consumeables->config.card_limit += 1;
            else G.jokers->config.card_limit += 1;
        }
    }
    if (G.GAME.blind) add_event(EVENT(.func = ev_blind_reset));
}

void Card_remove_from_deck(Card *c, bool from_debuff) {
    if (!c->added_to_deck) return;
    c->added_to_deck = false;
    if (c->ability.h_size != 0) CardArea_change_size(G.hand, -c->ability.h_size);
    if (c->ability.d_size > 0) {
        G.GAME.round_resets.discards -= c->ability.d_size;
        ease_discard(-c->ability.d_size, false, false);
    }
    if (card_name_is(c, "Credit Card")) G.GAME.bankrupt_at += c->ability.extra.num;
    if (card_name_is(c, "Chaos the Clown")) {
        G.GAME.current_round.free_rerolls -= 1;
        calculate_reroll_cost(true);
    }
    if (card_name_is(c, "Turtle Bean")) CardArea_change_size(G.hand, -c->ability.extra.h_size);
    if (card_name_is(c, "Oops! All 6s")) G.GAME.probabilities.normal /= 2;
    if (card_name_is(c, "To the Moon")) G.GAME.interest_amount -= c->ability.extra.num;
    if (card_name_is(c, "Astronomer")) add_event(EVENT(.func = ev_set_costs_all));
    if (card_name_is(c, "Troubadour")) {
        CardArea_change_size(G.hand, -c->ability.extra.h_size);
        G.GAME.round_resets.hands -= c->ability.extra.h_plays;
    }
    if (card_name_is(c, "Stuntman")) CardArea_change_size(G.hand, c->ability.extra.h_size);
    if (c->edition && c->edition->negative && G.jokers) {
        if (from_debuff) c->ability.queue_negative_removal = true;
        else {
            if (c->ability.consumeable) G.consumeables->config.card_limit -= 1;
            else G.jokers->config.card_limit -= 1;
        }
    }
    if (G.GAME.blind) add_event(EVENT(.func = ev_blind_reset));
}

CardUITable *Card_generate_UIBox_unlock_table(Card *c, bool hidden) {
    LocVars *lv = loc_vars_new();
    lv->no_name = true;
    lv->not_hidden = !hidden;
    lv->present = true;
    return generate_card_ui(c->config.center, 1, NULL, lv, "Locked", NULL, false, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* simple getters                                                      */
/* ------------------------------------------------------------------ */
double Card_get_nominal(Card *c, const char *mod) {
    double mult = 1;
    if (mod && !strcmp(mod, "suit")) mult = 1000;
    if (str_eq(c->ability.effect, "Stone Card")) mult = -1000;
    return c->base.nominal + c->base.suit_nominal * mult + c->base.suit_nominal_original * 0.0001 * mult + c->base.face_nominal + 0.000001 * c->unique_val;
}
int Card_get_id(Card *c) {
    if (str_eq(c->ability.effect, "Stone Card") && !c->vampired) return -math_random_range(100, 1000000);
    return c->base.id;
}
bool Card_is_face(Card *c, bool from_boss) {
    if (c->debuff && !from_boss) return false;
    int id = Card_get_id(c);
    return id == 11 || id == 12 || id == 13 || has_joker("Pareidolia");
}
const char *Card_get_original_rank(Card *c) { return c->base.original_value; }
double Card_get_chip_bonus(Card *c) {
    if (c->debuff) return 0;
    if (str_eq(c->ability.effect, "Stone Card")) return c->ability.bonus + c->ability.perma_bonus;
    return c->base.nominal + c->ability.bonus + c->ability.perma_bonus;
}
double Card_get_chip_mult(Card *c) {
    if (c->debuff) return 0;
    if (str_eq(c->ability.set, "Joker")) return 0;
    if (str_eq(c->ability.effect, "Lucky Card")) {
        if (pseudorandom("lucky_mult") < G.GAME.probabilities.normal / 5) {
            c->lucky_trigger = true;
            return c->ability.mult;
        }
        return 0;
    }
    return c->ability.mult;
}
double Card_get_chip_x_mult(Card *c) {
    if (c->debuff) return 0;
    if (str_eq(c->ability.set, "Joker")) return 0;
    if (c->ability.x_mult <= 1) return 0;
    return c->ability.x_mult;
}
double Card_get_chip_h_mult(Card *c) { return c->debuff ? 0 : c->ability.h_mult; }
double Card_get_chip_h_x_mult(Card *c) { return c->debuff ? 0 : c->ability.h_x_mult; }

EvalResult *eval_new(void) { return NEW(EvalResult); }

EvalResult *Card_get_edition(Card *c) {
    if (c->debuff || !c->edition) return NULL;
    EvalResult *r = eval_new();
    r->card = c;
    if (c->edition->has_x_mult) { r->Xmult_mod = c->edition->x_mult; r->has_Xmult_mod = true; }   /* x_mult_mod */
    if (c->edition->has_mult) { r->mult_mod = c->edition->mult; r->has_mult_mod = true; }
    if (c->edition->has_chips) { r->chip_mod = c->edition->chips; r->has_chip_mod = true; }
    return r;
}

typedef struct { const char *card_type; } BlueSealCtx;
EVFN(ev_blue_seal) {
    (void)e_; (void)p_;
    if (G.GAME.last_hand_played) {
        const char *planet = NULL;
        for (int i = 0; i < G.P_CENTER_POOLS.Planet.count; i++) {
            Center *v = G.P_CENTER_POOLS.Planet.items[i];
            if (str_eq(v->config.hand_type, G.GAME.last_hand_played)) planet = v->key;
        }
        Card *card = create_card("Planet", G.consumeables, false, RARITY_NIL, false, false, planet, "blusl");
        Card_add_to_deck(card, false);
        CardArea_emplace(G.consumeables, card, NULL, false);
        G.GAME.consumeable_buffer = 0;
    }
    return true;
}

EvalResult *Card_get_end_of_round_effect(Card *c, JokerContext *ctx) {
    (void)ctx;
    EvalResult *r = eval_new();
    r->is_empty = true;
    if (c->debuff) return r;
    if (c->ability.h_dollars > 0) {
        r->h_dollars = c->ability.h_dollars; r->has_h_dollars = true;
        r->card = c;
        r->is_empty = false;
    }
    if (str_eq(c->seal, "Blue") && G.consumeables->cards.count + G.GAME.consumeable_buffer < G.consumeables->config.card_limit) {
        G.GAME.consumeable_buffer += 1;
        add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.0, .func = ev_blue_seal));
        card_eval_status_text(&c->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_plus_planet"), .colour = G.C.SECONDARY_SET.Planet));
        r->effect = true;
        r->is_empty = false;
    }
    return r;
}

EVFN(ev_reset_dollar_buffer) { (void)e_; (void)p_; G.GAME.dollar_buffer = 0; return true; }

double Card_get_p_dollars(Card *c) {
    if (c->debuff) return 0;
    double ret = 0;
    if (str_eq(c->seal, "Gold")) ret += 3;
    if (c->ability.p_dollars > 0) {
        if (str_eq(c->ability.effect, "Lucky Card")) {
            if (pseudorandom("lucky_money") < G.GAME.probabilities.normal / 15) {
                c->lucky_trigger = true;
                ret += c->ability.p_dollars;
            }
        } else ret += c->ability.p_dollars;
    }
    if (ret > 0) {
        G.GAME.dollar_buffer += ret;
        add_event(EVENT(.func = ev_reset_dollar_buffer));
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* use_consumeable                                                     */
/* ------------------------------------------------------------------ */
EVFN(ev_tarot_juice) {
    EVCTX(CardCtx);
    sound_play("tarot1", NAN, NAN);
    Card_juice_up(&c->card->n, 0.3, 0.5);
    return true;
}
static Card *hl(int i) { return (i >= 1 && i <= G.hand->highlighted.count) ? G.hand->highlighted.items[i - 1] : NULL; }
static Card *hc(int i) { return (i >= 1 && i <= G.hand->cards.count) ? G.hand->cards.items[i - 1] : NULL; }
EVFN(ev_flip_hl_card1) {
    EVCTX(CardCtx);
    Card *cd = hl(c->i);
    if (cd) { Card_flip(cd); sound_play("card1", c->a, NAN); Card_juice_up(&cd->n, 0.3, 0.3); }
    return true;
}
EVFN(ev_flip_hl_tarot2) {
    EVCTX(CardCtx);
    Card *cd = hl(c->i);
    if (cd) { Card_flip(cd); sound_play("tarot2", c->a, 0.6); Card_juice_up(&cd->n, 0.3, 0.3); }
    return true;
}
EVFN(ev_flip_hc_card1) {
    EVCTX(CardCtx);
    Card *cd = hc(c->i);
    if (cd) { Card_flip(cd); sound_play("card1", c->a, NAN); Card_juice_up(&cd->n, 0.3, 0.3); }
    return true;
}
EVFN(ev_flip_hc_tarot2) {
    EVCTX(CardCtx);
    Card *cd = hc(c->i);
    if (cd) { Card_flip(cd); sound_play("tarot2", c->a, 0.6); Card_juice_up(&cd->n, 0.3, 0.3); }
    return true;
}
EVFN(ev_death_copy) {
    EVCTX(CardCtx);
    Card *cd = hl(c->i);
    if (cd && cd != c->other) copy_card(c->other, cd, NAN, 0, false);
    return true;
}
EVFN(ev_strength) {
    EVCTX(CardCtx);
    Card *card = hl(c->i);
    if (!card) return true;
    char sp[4] = { card->base.suit[0], '_', 0 };
    int rs = card->base.id == 14 ? 2 : MIN(card->base.id + 1, 14);
    const char *suffix;
    char buf[4];
    if (rs < 10) { snprintf(buf, sizeof buf, "%d", rs); suffix = buf; }
    else if (rs == 10) suffix = "T";
    else if (rs == 11) suffix = "J";
    else if (rs == 12) suffix = "Q";
    else if (rs == 13) suffix = "K";
    else suffix = "A";
    Card_set_base(card, P_CARD(tfmt("%s%s", sp, suffix)), false);
    return true;
}
EVFN(ev_suit_conv) {
    EVCTX(CardCtx);
    Card *cd = hl(c->i);
    if (cd) Card_change_suit(cd, c->s);
    return true;
}
EVFN(ev_mod_conv) {
    EVCTX(CardCtx);
    Card *cd = hl(c->i);
    if (cd) Card_set_ability(cd, P_CENTER(c->s), false, false);
    return true;
}
EVFN(ev_unhighlight_hand) { (void)e_; (void)p_; CardArea_unhighlight_all(G.hand); return true; }
EVFN(ev_black_hole_1) {
    EVCTX(CardCtx);
    sound_play("tarot1", NAN, NAN);
    Card_juice_up(&c->card->n, 0.8, 0.5);
    G.TAROT_INTERRUPT_PULSE = true;
    return true;
}
EVFN(ev_black_hole_2) {
    EVCTX(CardCtx);
    sound_play("tarot1", NAN, NAN);
    Card_juice_up(&c->card->n, 0.8, 0.5);
    return true;
}
EVFN(ev_black_hole_3) {
    EVCTX(CardCtx);
    sound_play("tarot1", NAN, NAN);
    Card_juice_up(&c->card->n, 0.8, 0.5);
    G.TAROT_INTERRUPT_PULSE = false;
    return true;
}
EVFN(ev_seal_conv) {
    EVCTX(CardCtx);
    if (c->other) Card_set_seal(c->other, c->s, false, true);
    return true;
}
EVFN(ev_aura) {
    EVCTX(CardCtx);
    EditionReq ed = poll_edition("aura", NAN, true, true);
    Card *aura_card = hl(1);
    if (aura_card) Card_set_edition(aura_card, ed, true, false);
    Card_juice_up(&c->card->n, 0.3, 0.5);
    return true;
}
EVFN(ev_cryptid) {
    EVCTX(CardCtx);
    bool first_dissolve = false;
    CardList new_cards = { 0 };
    for (int i = 1; i <= (int)c->a; i++) {
        G.playing_card = G.playing_card ? G.playing_card + 1 : 1;
        Card *src = hl(1);
        if (!src) break;
        Card *_card = copy_card(src, NULL, NAN, G.playing_card, false);
        Card_add_to_deck(_card, false);
        G.deck->config.card_limit += 1;
        vec_push(&G.playing_cards, _card);
        CardArea_emplace(G.hand, _card, NULL, false);
        Card_start_materialize(_card, NULL, 0, first_dissolve, NAN);
        first_dissolve = true;
        vec_push(&new_cards, _card);
    }
    playing_card_joker_effects(&new_cards, new_cards.count);
    return true;
}
EVFN(ev_sigil) {
    EVCTX(CardCtx);
    Card *card = hc(c->i);
    if (!card) return true;
    const char *suffix;
    char buf[4];
    int id = card->base.id;
    if (id < 10) { snprintf(buf, sizeof buf, "%d", id); suffix = buf; }
    else if (id == 10) suffix = "T";
    else if (id == 11) suffix = "J";
    else if (id == 12) suffix = "Q";
    else if (id == 13) suffix = "K";
    else suffix = "A";
    Card_set_base(card, P_CARD(tfmt("%s_%s", c->s, suffix)), false);
    return true;
}
EVFN(ev_ouija) {
    EVCTX(CardCtx);
    Card *card = hc(c->i);
    if (!card) return true;
    Card_set_base(card, P_CARD(tfmt("%c_%s", card->base.suit[0], c->s)), false);
    return true;
}
typedef struct { CardList list; bool reverse_first; bool hanged; } DestroyCtx;
EVFN(ev_hanged_destroy) {
    (void)e_; (void)p_;
    for (int i = G.hand->highlighted.count; i >= 1; i--) {
        Card *card = G.hand->highlighted.items[i - 1];
        if (card_name_is(card, "Glass Card")) Card_shatter(card);
        else Card_start_dissolve(card, NULL, 0, i == G.hand->highlighted.count, NAN, false);
    }
    return true;
}
EVFN(ev_destroy_list) {
    EVCTX(DestroyCtx);
    for (int i = c->list.count; i >= 1; i--) {
        Card *card = c->list.items[i - 1];
        if (card_name_is(card, "Glass Card")) Card_shatter(card);
        else Card_start_dissolve(card, NULL, 0, c->reverse_first ? (i == c->list.count) : (i != c->list.count), NAN, false);
    }
    return true;
}
static const char *SUIT_LETTERS[4] = { "S", "H", "D", "C" };
EVFN(ev_create_spectral_cards) {
    EVCTX(CardCtx);
    Card *self = c->card;
    CardList cards = { 0 };
    int extra = (int)self->ability.extra.num;
    for (int i = 1; i <= extra; i++) {
        vec_push(&cards, NULL);
        const char *_suit = NULL, *_rank = NULL;
        if (card_name_is(self, "Familiar")) {
            static const char *faces[3] = { "J", "Q", "K" };
            _rank = faces[pseudorandom_index(3, pseudoseed("familiar_create"), true) - 1];
            _suit = SUIT_LETTERS[pseudorandom_index(4, pseudoseed("familiar_create"), true) - 1];
        } else if (card_name_is(self, "Grim")) {
            _rank = "A";
            _suit = SUIT_LETTERS[pseudorandom_index(4, pseudoseed("grim_create"), true) - 1];
        } else if (card_name_is(self, "Incantation")) {
            static const char *nums[9] = { "2", "3", "4", "5", "6", "7", "8", "9", "T" };
            _rank = nums[pseudorandom_index(9, pseudoseed("incantation_create"), true) - 1];
            _suit = SUIT_LETTERS[pseudorandom_index(4, pseudoseed("incantation_create"), true) - 1];
        }
        if (!_suit) _suit = "S";
        if (!_rank) _rank = "A";
        CenterVec pool = { 0 };
        for (int k = 0; k < G.P_CENTER_POOLS.Enhanced.count; k++) {
            Center *v = G.P_CENTER_POOLS.Enhanced.items[k];
            if (!str_eq(v->key, "m_stone")) vec_push(&pool, v);
        }
        Center *cen = pool.items[pseudorandom_index(pool.count, pseudoseed("spe_card"), true) - 1];
        vec_free(&pool);
        Col cols[1] = { G.C.SECONDARY_SET.Spectral };
        create_playing_card((PlayingCardInit){ P_CARD(tfmt("%s_%s", _suit, _rank)), cen }, G.hand, false, i != 1, cols, 1);
    }
    playing_card_joker_effects(&cards, cards.count);
    return true;
}
EVFN(ev_fool) {
    EVCTX(CardCtx);
    if (G.consumeables->config.card_limit > G.consumeables->cards.count) {
        sound_play("timpani", NAN, NAN);
        Card *card = create_card("Tarot_Planet", G.consumeables, false, RARITY_NIL, false, false, G.GAME.last_tarot_planet, "fool");
        Card_add_to_deck(card, false);
        CardArea_emplace(G.consumeables, card, NULL, false);
        Card_juice_up(&c->card->n, 0.3, 0.5);
    }
    return true;
}
EVFN(ev_hermit) {
    EVCTX(CardCtx);
    sound_play("timpani", NAN, NAN);
    Card_juice_up(&c->card->n, 0.3, 0.5);
    ease_dollars(MAX(0, MIN(G.GAME.dollars, c->a)), true);
    return true;
}
EVFN(ev_temperance) {
    EVCTX(CardCtx);
    sound_play("timpani", NAN, NAN);
    Card_juice_up(&c->card->n, 0.3, 0.5);
    ease_dollars(c->other->ability.money, true);
    return true;
}
EVFN(ev_emperor) {
    EVCTX(CardCtx);
    if (G.consumeables->config.card_limit > G.consumeables->cards.count) {
        sound_play("timpani", NAN, NAN);
        bool emp = card_name_is(c->other, "The Emperor");
        Card *card = create_card(emp ? "Tarot" : "Planet", G.consumeables, false, RARITY_NIL, false, false, NULL, emp ? "emp" : "pri");
        Card_add_to_deck(card, false);
        CardArea_emplace(G.consumeables, card, NULL, false);
        Card_juice_up(&c->card->n, 0.3, 0.5);
    }
    return true;
}
EVFN(ev_judgement) {
    EVCTX(CardCtx);
    sound_play("timpani", NAN, NAN);
    bool soul = card_name_is(c->other, "The Soul");
    Card *card = create_card("Joker", G.jokers, soul, RARITY_NIL, false, false, NULL, card_name_is(c->other, "Judgement") ? "jud" : "sou");
    Card_add_to_deck(card, false);
    CardArea_emplace(G.jokers, card, NULL, false);
    if (soul) UNLOCK(.type = "spawn_legendary");
    Card_juice_up(&c->card->n, 0.3, 0.5);
    return true;
}
typedef struct { CardList deletable; Card *chosen; } AnkhCtx;
EVFN(ev_ankh_dissolve) {
    EVCTX(AnkhCtx);
    bool first = false;
    for (int i = 0; i < c->deletable.count; i++) {
        Card *v = c->deletable.items[i];
        if (v != c->chosen) { Card_start_dissolve(v, NULL, 0, first, NAN, false); first = true; }
    }
    return true;
}
static void nofree(void *p) { (void)p; }
EVFN(ev_ankh_copy) {
    EVCTX(AnkhCtx);
    Card *chosen = c->chosen;
    Card *card = copy_card(chosen, NULL, NAN, 0, chosen->edition && chosen->edition->negative);
    Card_start_materialize(card, NULL, 0, false, NAN);
    Card_add_to_deck(card, false);
    if (card->edition && card->edition->negative) Card_set_edition(card, ED_NONE, true, false);
    CardArea_emplace(G.jokers, card, NULL, false);
    return true;
}
EVFN(ev_wraith) {
    EVCTX(CardCtx);
    sound_play("timpani", NAN, NAN);
    Card *card = create_card("Joker", G.jokers, false, 0.99, false, false, NULL, "wra");
    Card_add_to_deck(card, false);
    CardArea_emplace(G.jokers, card, NULL, false);
    Card_juice_up(&c->card->n, 0.3, 0.5);
    if (G.GAME.dollars != 0) ease_dollars(-G.GAME.dollars, true);
    return true;
}
typedef struct { Card *self; Card *used; CardList pool; } WheelCtx;
EVFN(ev_wheel) {
    EVCTX(WheelCtx);
    Card *self = c->self;
    const char *name = self->ability.name;
    const char *seed = !strcmp(name, "The Wheel of Fortune") ? "wheel_of_fortune" : !strcmp(name, "Ectoplasm") ? "ectoplasm" : "hex";
    int idx = pseudorandom_index(c->pool.count, pseudoseed(seed), true);
    if (idx < 1) return true;
    /* pseudorandom_element sorts cards by sort_id */
    CardList sorted = { 0 };
    for (int i = 0; i < c->pool.count; i++) vec_push(&sorted, c->pool.items[i]);
    for (int i = 1; i < sorted.count; i++) {
        Card *k = sorted.items[i]; int j = i - 1;
        while (j >= 0 && sorted.items[j]->sort_id > k->sort_id) { sorted.items[j + 1] = sorted.items[j]; j--; }
        sorted.items[j + 1] = k;
    }
    Card *eligible = sorted.items[idx - 1];
    vec_free(&sorted);
    EditionReq ed = ED_NONE;
    if (!strcmp(name, "Ectoplasm")) ed = ED_NEG;
    else if (!strcmp(name, "Hex")) ed = ED_POLY;
    else ed = poll_edition("wheel_of_fortune", NAN, true, true);
    Card_set_edition(eligible, ed, true, false);
    UNLOCK(.type = "have_edition");
    if (!strcmp(name, "Hex")) {
        bool first = false;
        for (int i = 0; i < G.jokers->cards.count; i++) {
            Card *v = G.jokers->cards.items[i];
            if (v != eligible && !v->ability.eternal) { Card_start_dissolve(v, NULL, 0, first, NAN, false); first = true; }
        }
    }
    if (!strcmp(name, "Ectoplasm")) {
        if (G.GAME.ecto_minus == 0) G.GAME.ecto_minus = 1;
        CardArea_change_size(G.hand, -G.GAME.ecto_minus);
        G.GAME.ecto_minus += 1;
    }
    Card_juice_up(&c->used->n, 0.3, 0.5);
    return true;
}
EVFN(ev_tarot2_sound) { (void)e_; (void)p_; sound_play("tarot2", 0.76, 0.4); return true; }
EVFN(ev_nope) {
    EVCTX(CardCtx);
    bool pack = G.STATE == STATE_TAROT_PACK || G.STATE == STATE_SPECTRAL_PACK;
    ATT(.text = localize_key("k_nope_ex"), .scale = 1.3, .hold = 1.4, .major = &c->card->n,
        .backdrop_colour = G.C.SECONDARY_SET.Tarot, .align = pack ? "tm" : "cm",
        .has_offset = true, .offset = { 0, pack ? -0.2 : 0 });
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.06 * G.SETTINGS.GAMESPEED, .blockable = 0, .blocking = 0, .func = ev_tarot2_sound));
    sound_play("tarot2", 1, 0.4);
    Card_juice_up(&c->card->n, 0.3, 0.5);
    return true;
}

static void juice_ev(Card *card, double delay_t, EventTrigger trig) {
    CardCtx *cc = CTX(CardCtx);
    cc->card = card;
    add_event(EVENT(.trigger = trig, .delay = delay_t, .func = ev_tarot_juice, .ctx = cc));
}

void Card_use_consumeable(Card *self, CardArea *area, Card *copier) {
    (void)area;
    stop_use();
    if (!copier) set_consumeable_usage(self);
    if (self->debuff) return;
    Card *used_tarot = copier ? copier : self;
    CenterConfig *cons = self->ability.consumeable;
    const char *name = self->ability.name;

    if (cons->max_highlighted != 0)
        update_hand_text(HTC(.immediate = true, .nopulse = true, .delay = 0, .has_delay = true),
                         HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .level = "", .has_level = true, .handname = "", .has_handname = true));

    if (cons->mod_conv || cons->suit_conv) {
        juice_ev(used_tarot, 0.4, TRIG_AFTER);
        int nh = G.hand->highlighted.count;
        for (int i = 1; i <= nh; i++) {
            CardCtx *cc = CTX(CardCtx);
            cc->i = i;
            cc->a = 1.15 - (i - 0.999) / (nh - 0.998) * 0.3;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.15, .func = ev_flip_hl_card1, .ctx = cc));
        }
        delay(0.2);
        if (!strcmp(name, "Death")) {
            Card *rightmost = hl(1);
            for (int i = 1; i <= nh; i++) if (hl(i)->n.T->x > rightmost->n.T->x) rightmost = hl(i);
            for (int i = 1; i <= nh; i++) {
                CardCtx *cc = CTX(CardCtx);
                cc->i = i; cc->other = rightmost;
                add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_death_copy, .ctx = cc));
            }
        } else if (!strcmp(name, "Strength")) {
            for (int i = 1; i <= nh; i++) {
                CardCtx *cc = CTX(CardCtx);
                cc->i = i;
                add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_strength, .ctx = cc));
            }
        } else if (cons->suit_conv) {
            for (int i = 1; i <= nh; i++) {
                CardCtx *cc = CTX(CardCtx);
                cc->i = i; cc->s = cons->suit_conv;
                add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_suit_conv, .ctx = cc));
            }
        } else {
            for (int i = 1; i <= nh; i++) {
                CardCtx *cc = CTX(CardCtx);
                cc->i = i; cc->s = cons->mod_conv;
                add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_mod_conv, .ctx = cc));
            }
        }
        for (int i = 1; i <= nh; i++) {
            CardCtx *cc = CTX(CardCtx);
            cc->i = i;
            cc->a = 0.85 + (i - 0.999) / (nh - 0.998) * 0.3;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.15, .func = ev_flip_hl_tarot2, .ctx = cc));
        }
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_unhighlight_hand));
        delay(0.5);
    }
    if (!strcmp(name, "Black Hole")) {
        update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 0.8, .has_pitch = true, .delay = 0.3, .has_delay = true),
                         HTV(.handname = localize_key("k_all_hands"), .has_handname = true, .chips_s = "...", .has_chips = true,
                             .mult_s = "...", .has_mult = true, .level = "", .has_level = true));
        CardCtx *c1 = CTX(CardCtx); c1->card = self;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_black_hole_1, .ctx = c1));
        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.mult_s = "+", .has_mult = true, .StatusText = true));
        CardCtx *c2 = CTX(CardCtx); c2->card = self;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.9, .func = ev_black_hole_2, .ctx = c2));
        update_hand_text(HTC(.delay = 0, .has_delay = true), HTV(.chips_s = "+", .has_chips = true, .StatusText = true));
        CardCtx *c3 = CTX(CardCtx); c3->card = self;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.9, .func = ev_black_hole_3, .ctx = c3));
        update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 0.9, .has_pitch = true, .delay = 0, .has_delay = true),
                         HTV(.level = "+1", .has_level = true));
        delay(1.3);
        for (int i = 0; i < HAND_COUNT; i++) level_up_hand(&self->n, DATA_HANDS_PAIRS_ORDER[i], true, NAN);
        update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 1.1, .has_pitch = true, .delay = 0, .has_delay = true),
                         HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .handname = "", .has_handname = true, .level = "", .has_level = true));
    }
    if (!strcmp(name, "Talisman") || !strcmp(name, "Deja Vu") || !strcmp(name, "Trance") || !strcmp(name, "Medium")) {
        Card *conv_card = hl(1);
        juice_ev(used_tarot, 0, TRIG_IMMEDIATE);
        CardCtx *cc = CTX(CardCtx);
        cc->other = conv_card; cc->s = self->ability.extra.str;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_seal_conv, .ctx = cc));
        delay(0.5);
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_unhighlight_hand));
    }
    if (!strcmp(name, "Aura")) {
        CardCtx *cc = CTX(CardCtx); cc->card = used_tarot;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_aura, .ctx = cc));
    }
    if (!strcmp(name, "Cryptid")) {
        CardCtx *cc = CTX(CardCtx); cc->a = self->ability.extra.num;
        add_event(EVENT(.func = ev_cryptid, .ctx = cc));
    }
    if (!strcmp(name, "Sigil") || !strcmp(name, "Ouija")) {
        juice_ev(used_tarot, 0.4, TRIG_AFTER);
        int nc = G.hand->cards.count;
        for (int i = 1; i <= nc; i++) {
            CardCtx *cc = CTX(CardCtx);
            cc->i = i;
            cc->a = 1.15 - (i - 0.999) / (nc - 0.998) * 0.3;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.15, .func = ev_flip_hc_card1, .ctx = cc));
        }
        delay(0.2);
        if (!strcmp(name, "Sigil")) {
            const char *suit = SUIT_LETTERS[pseudorandom_index(4, pseudoseed("sigil"), true) - 1];
            for (int i = 1; i <= nc; i++) {
                CardCtx *cc = CTX(CardCtx); cc->i = i; cc->s = suit;
                add_event(EVENT(.func = ev_sigil, .ctx = cc));
            }
        }
        if (!strcmp(name, "Ouija")) {
            static const char *ranks[13] = { "2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A" };
            const char *rank = ranks[pseudorandom_index(13, pseudoseed("ouija"), true) - 1];
            for (int i = 1; i <= nc; i++) {
                CardCtx *cc = CTX(CardCtx); cc->i = i; cc->s = rank;
                add_event(EVENT(.func = ev_ouija, .ctx = cc));
            }
            CardArea_change_size(G.hand, -1);
        }
        for (int i = 1; i <= nc; i++) {
            CardCtx *cc = CTX(CardCtx);
            cc->i = i;
            cc->a = 0.85 + (i - 0.999) / (nc - 0.998) * 0.3;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.15, .func = ev_flip_hc_tarot2, .ctx = cc));
        }
        delay(0.5);
    }
    if (cons->hand_type) {
        HandStat *h = &G.GAME.hands[hand_index(cons->hand_type)];
        update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 0.8, .has_pitch = true, .delay = 0.3, .has_delay = true),
                         HTV(.handname = localize_cat(cons->hand_type, "poker_hands"), .has_handname = true,
                             .chips = h->chips, .has_chips = true, .mult = h->mult, .has_mult = true,
                             .level_n = h->level, .level_is_num = true, .has_level = true));
        level_up_hand(&used_tarot->n, cons->hand_type, false, NAN);
        update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 1.1, .has_pitch = true, .delay = 0, .has_delay = true),
                         HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .handname = "", .has_handname = true, .level = "", .has_level = true));
    }
    if (cons->remove_card) {
        CardList destroyed = { 0 };
        if (!strcmp(name, "The Hanged Man")) {
            for (int i = G.hand->highlighted.count; i >= 1; i--) vec_push(&destroyed, G.hand->highlighted.items[i - 1]);
            juice_ev(used_tarot, 0.4, TRIG_AFTER);
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_hanged_destroy));
        } else if (!strcmp(name, "Familiar") || !strcmp(name, "Grim") || !strcmp(name, "Incantation")) {
            /* pseudorandom_element(G.hand.cards) sorts by sort_id */
            CardList sorted = { 0 };
            for (int i = 0; i < G.hand->cards.count; i++) vec_push(&sorted, G.hand->cards.items[i]);
            for (int i = 1; i < sorted.count; i++) {
                Card *k = sorted.items[i]; int j = i - 1;
                while (j >= 0 && sorted.items[j]->sort_id > k->sort_id) { sorted.items[j + 1] = sorted.items[j]; j--; }
                sorted.items[j + 1] = k;
            }
            int idx = pseudorandom_index(sorted.count, pseudoseed("random_destroy"), true);
            if (idx > 0) vec_push(&destroyed, sorted.items[idx - 1]);
            vec_free(&sorted);
            juice_ev(used_tarot, 0.4, TRIG_AFTER);
            DestroyCtx *dc = CTX(DestroyCtx);
            for (int i = 0; i < destroyed.count; i++) vec_push(&dc->list, destroyed.items[i]);
            dc->reverse_first = false;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_destroy_list, .ctx = dc));
            CardCtx *cc = CTX(CardCtx); cc->card = self;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.7, .func = ev_create_spectral_cards, .ctx = cc));
        } else if (!strcmp(name, "Immolate")) {
            CardList temp = { 0 };
            for (int i = 0; i < G.hand->cards.count; i++) vec_push(&temp, G.hand->cards.items[i]);
            /* table.sort(temp_hand, playing_card order) then pseudoshuffle (which re-sorts by sort_id) */
            pseudoshuffle_cards(&temp, pseudoseed("immolate"));
            for (int i = 1; i <= (int)self->ability.extra.destroy && i <= temp.count; i++) vec_push(&destroyed, temp.items[i - 1]);
            vec_free(&temp);
            juice_ev(used_tarot, 0.4, TRIG_AFTER);
            DestroyCtx *dc = CTX(DestroyCtx);
            for (int i = 0; i < destroyed.count; i++) vec_push(&dc->list, destroyed.items[i]);
            dc->reverse_first = true;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_destroy_list, .ctx = dc));
            delay(0.5);
            ease_dollars(self->ability.extra.dollars, false);
        }
        delay(0.3);
        for (int i = 0; i < G.jokers->cards.count; i++)
            Card_calculate_joker(G.jokers->cards.items[i], JCTX(.remove_playing_cards = true, .removed = &destroyed));
    }
    if (!strcmp(name, "The Fool")) {
        CardCtx *cc = CTX(CardCtx); cc->card = used_tarot;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_fool, .ctx = cc));
        delay(0.6);
    }
    if (!strcmp(name, "The Hermit")) {
        CardCtx *cc = CTX(CardCtx); cc->card = used_tarot; cc->a = self->ability.extra.num;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_hermit, .ctx = cc));
        delay(0.6);
    }
    if (!strcmp(name, "Temperance")) {
        CardCtx *cc = CTX(CardCtx); cc->card = used_tarot; cc->other = self;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_temperance, .ctx = cc));
        delay(0.6);
    }
    if (!strcmp(name, "The Emperor") || !strcmp(name, "The High Priestess")) {
        double cnt = cons->tarots != 0 ? cons->tarots : cons->planets;
        int n = (int)MIN(cnt, G.consumeables->config.card_limit - G.consumeables->cards.count);
        for (int i = 1; i <= n; i++) {
            CardCtx *cc = CTX(CardCtx); cc->card = used_tarot; cc->other = self;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_emperor, .ctx = cc));
        }
        delay(0.6);
    }
    if (!strcmp(name, "Judgement") || !strcmp(name, "The Soul")) {
        CardCtx *cc = CTX(CardCtx); cc->card = used_tarot; cc->other = self;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_judgement, .ctx = cc));
        delay(0.6);
    }
    if (!strcmp(name, "Ankh")) {
        AnkhCtx *ac = NEW(AnkhCtx);
        for (int i = 0; i < G.jokers->cards.count; i++) {
            Card *v = G.jokers->cards.items[i];
            if (!v->ability.eternal) vec_push(&ac->deletable, v);
        }
        CardList sorted = { 0 };
        for (int i = 0; i < G.jokers->cards.count; i++) vec_push(&sorted, G.jokers->cards.items[i]);
        for (int i = 1; i < sorted.count; i++) {
            Card *k = sorted.items[i]; int j = i - 1;
            while (j >= 0 && sorted.items[j]->sort_id > k->sort_id) { sorted.items[j + 1] = sorted.items[j]; j--; }
            sorted.items[j + 1] = k;
        }
        int idx = pseudorandom_index(sorted.count, pseudoseed("ankh_choice"), true);
        ac->chosen = idx > 0 ? sorted.items[idx - 1] : NULL;
        vec_free(&sorted);
        add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.75, .func = ev_ankh_dissolve, .ctx = ac, .ctx_free = nofree));
        if (ac->chosen) add_event(EVENT(.trigger = TRIG_BEFORE, .delay = 0.4, .func = ev_ankh_copy, .ctx = ac, .ctx_free = nofree));
    }
    if (!strcmp(name, "Wraith")) {
        CardCtx *cc = CTX(CardCtx); cc->card = used_tarot;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_wraith, .ctx = cc));
        delay(0.6);
    }
    if (!strcmp(name, "The Wheel of Fortune") || !strcmp(name, "Ectoplasm") || !strcmp(name, "Hex")) {
        PtrVec *pool = !strcmp(name, "The Wheel of Fortune") ? &self->eligible_strength_jokers : &self->eligible_editionless_jokers;
        if (!strcmp(name, "Ectoplasm") || !strcmp(name, "Hex") || (!G.debug_force_nope && pseudorandom("wheel_of_fortune") < G.GAME.probabilities.normal / self->ability.extra.num)) {
            WheelCtx *wc = CTX(WheelCtx);
            wc->self = self; wc->used = used_tarot;
            for (int i = 0; i < pool->count; i++) vec_push(&wc->pool, (Card *)pool->items[i]);
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_wheel, .ctx = wc));
        } else {
            CardCtx *cc = CTX(CardCtx); cc->card = used_tarot;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_nope, .ctx = cc));
        }
        delay(0.6);
    }
}

bool Card_can_use_consumeable(Card *c, bool any_state, bool skip_check) {
    if (!skip_check && ((G.play && G.play->cards.count > 0) || G.CONTROLLER.locked || (G.GAME.STOP_USE > 0))) return false;
    CenterConfig *cons = c->ability.consumeable;
    if (!cons) return false;
    if ((G.STATE != STATE_HAND_PLAYED && G.STATE != STATE_DRAW_TO_HAND && G.STATE != STATE_PLAY_TAROT) || any_state) {
        const char *n = c->ability.name;
        if (!strcmp(n, "The Hermit") || cons->hand_type || !strcmp(n, "Temperance") || !strcmp(n, "Black Hole")) return true;
        if (!strcmp(n, "The Wheel of Fortune") && c->eligible_strength_jokers.count > 0) return true;
        if (!strcmp(n, "Ankh")) {
            for (int i = 0; i < G.jokers->cards.count; i++)
                if (str_eq(G.jokers->cards.items[i]->ability.set, "Joker") && G.jokers->config.card_limit > 1) return true;
        }
        if (!strcmp(n, "Aura")) {
            if (G.hand && G.hand->highlighted.count == 1 && !G.hand->highlighted.items[0]->edition) return true;
        }
        if ((!strcmp(n, "Ectoplasm") || !strcmp(n, "Hex")) && c->eligible_editionless_jokers.count > 0) return true;
        if (!strcmp(n, "The Emperor") || !strcmp(n, "The High Priestess")) {
            if (G.consumeables->cards.count < G.consumeables->config.card_limit || c->area == G.consumeables) return true;
        }
        if (!strcmp(n, "The Fool")) {
            if ((G.consumeables->cards.count < G.consumeables->config.card_limit || c->area == G.consumeables) &&
                G.GAME.last_tarot_planet && strcmp(G.GAME.last_tarot_planet, "c_fool") != 0) return true;
        }
        if (!strcmp(n, "Judgement") || !strcmp(n, "The Soul") || !strcmp(n, "Wraith")) {
            return G.jokers->cards.count < G.jokers->config.card_limit || c->area == G.jokers;
        }
        if (G.STATE == STATE_SELECTING_HAND || G.STATE == STATE_TAROT_PACK || G.STATE == STATE_SPECTRAL_PACK || G.STATE == STATE_PLANET_PACK) {
            if (cons->max_highlighted != 0) {
                double minh = cons->min_highlighted != 0 ? cons->min_highlighted : 1;
                if (cons->mod_num >= G.hand->highlighted.count && G.hand->highlighted.count >= minh) return true;
            }
            if ((!strcmp(n, "Familiar") || !strcmp(n, "Grim") || !strcmp(n, "Incantation") || !strcmp(n, "Immolate") ||
                 !strcmp(n, "Sigil") || !strcmp(n, "Ouija")) && G.hand->cards.count > 1) return true;
        }
    }
    return false;
}

bool Card_check_use(Card *c) {
    if (card_name_is(c, "Ankh")) {
        if (G.jokers->cards.count >= G.jokers->config.card_limit) {
            alert_no_space(c, G.jokers);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* selling                                                             */
/* ------------------------------------------------------------------ */
typedef struct { Card *card; CardArea *area; } SellCtx;
EVFN(ev_sell_juice) {
    EVCTX(SellCtx);
    sound_play("coin2", NAN, NAN);
    Card_juice_up(&c->card->n, 0.3, 0.4);
    return true;
}
EVFN(ev_sell_disable_blind) { (void)e_; (void)p_; Blind_disable(G.GAME.blind); return true; }
EVFN(ev_sell_unlock3) {
    EVCTX(SellCtx);
    strset_del(&G.CONTROLLER.locks.other, "selling_card");
    Controller_recall_cardarea_focus(&G.CONTROLLER, c->area == G.jokers ? "jokers" : "consumeables");
    return true;
}
EVFN(ev_sell_unlock2) {
    EVCTX(SellCtx);
    SellCtx *n = CTX(SellCtx); *n = *c;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_sell_unlock3, .ctx = n));
    return true;
}
EVFN(ev_sell_unlock1) {
    EVCTX(SellCtx);
    SellCtx *n = CTX(SellCtx); *n = *c;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_sell_unlock2, .ctx = n));
    return true;
}
EVFN(ev_sell_main) {
    EVCTX(SellCtx);
    Card *self = c->card;
    ease_dollars(self->sell_cost, false);
    Col gold[1] = { G.C.GOLD };
    Card_start_dissolve(self, gold, 1, false, NAN, false);
    delay(0.3);
    inc_career_stat("c_cards_sold", 1);
    if (str_eq(self->ability.set, "Joker")) inc_career_stat("c_jokers_sold", 1);
    if (str_eq(self->ability.set, "Joker") && G.GAME.blind && str_eq(G.GAME.blind->name, "Verdant Leaf"))
        add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_sell_disable_blind));
    SellCtx *n = CTX(SellCtx); *n = *c;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.3, .blocking = 0, .func = ev_sell_unlock1, .ctx = n));
    return true;
}

void Card_sell_card(Card *c) {
    strset_add(&G.CONTROLLER.locks.other, "selling_card");
    stop_use();
    CardArea *area = c->area;
    Controller_save_cardarea_focus(&G.CONTROLLER, area == G.jokers ? "jokers" : "consumeables");
    Node *ub = child_get(&c->n, "use_button");
    if (ub) { child_set(&c->n, "use_button", NULL); node_remove(ub); }
    Node *sb = child_get(&c->n, "sell_button");
    if (sb) { child_set(&c->n, "sell_button", NULL); node_remove(sb); }
    Card_calculate_joker(c, JCTX(.selling_self = true));
    SellCtx *s1 = CTX(SellCtx); s1->card = c; s1->area = area;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_sell_juice, .ctx = s1));
    delay(0.2);
    SellCtx *s2 = CTX(SellCtx); s2->card = c; s2->area = area;
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_sell_main, .ctx = s2));
}

bool Card_can_sell_card(Card *c) {
    if ((G.play && G.play->cards.count > 0) || G.CONTROLLER.locked || G.GAME.STOP_USE > 0) return false;
    if ((G.SETTINGS.tutorial_complete || !str_eq(G.GAME.pseudorandom.seed, "TUTORIAL") || G.GAME.round_resets.ante > 1) &&
        c->area && str_eq(c->area->config.type, "joker") && !c->ability.eternal) return true;
    return false;
}

double Card_calculate_dollar_bonus(Card *c) {
    if (c->debuff) return NAN;
    if (str_eq(c->ability.set, "Joker")) {
        const char *n = c->ability.name;
        if (!strcmp(n, "Golden Joker")) return c->ability.extra.num;
        if (!strcmp(n, "Cloud 9") && c->ability.has_nine_tally && c->ability.nine_tally > 0) return c->ability.extra.num * c->ability.nine_tally;
        if (!strcmp(n, "Rocket")) return c->ability.extra.dollars;
        if (!strcmp(n, "Satellite")) {
            int planets = 0;
            for (int i = 0; i < G.GAME.consumeable_usage.cap; i++) {
                if (!STRMAP_LIVE(&G.GAME.consumeable_usage, i)) continue;
                UsageEntry *u = G.GAME.consumeable_usage.vals[i];
                if (str_eq(u->set, "Planet")) planets++;
            }
            if (planets == 0) return NAN;
            return c->ability.extra.num * planets;
        }
        if (!strcmp(n, "Delayed Gratification") && G.GAME.current_round.discards_used == 0 && G.GAME.current_round.discards_left > 0)
            return G.GAME.current_round.discards_left * c->ability.extra.num;
    }
    return NAN;
}

/* ------------------------------------------------------------------ */
/* boosters                                                            */
/* ------------------------------------------------------------------ */
typedef struct { Card *self; CardList *pack_cards; } OpenCtx;
EVFN(ev_open_juice) {
    EVCTX(CardCtx);
    inc_career_stat("c_shop_dollars_spent", c->card->cost);
    Card_juice_up(&c->card->n, NAN, NAN);
    return true;
}
EVFN(ev_open_create) {
    EVCTX(OpenCtx);
    Card *self = c->self;
    int size = (int)self->ability.extra.num;
    const char *name = self->ability.name;
    for (int i = 1; i <= size; i++) {
        Card *card = NULL;
        if (str_contains(name, "Arcana")) {
            if (strset_has(&G.GAME.used_vouchers, "v_omen_globe") && pseudorandom("omen_globe") > 0.8)
                card = create_card("Spectral", G.pack_cards, false, RARITY_NIL, true, true, NULL, "ar2");
            else card = create_card("Tarot", G.pack_cards, false, RARITY_NIL, true, true, NULL, "ar1");
        } else if (str_contains(name, "Celestial")) {
            if (strset_has(&G.GAME.used_vouchers, "v_telescope") && i == 1) {
                const char *planet = NULL, *hand = NULL;
                double tally = 0;
                for (int k = 0; k < HAND_COUNT; k++) {
                    HandStat *h = &G.GAME.hands[k];
                    if (h->visible && h->played > tally) { hand = HAND_NAMES[k]; tally = h->played; }
                }
                if (hand) {
                    for (int k = 0; k < G.P_CENTER_POOLS.Planet.count; k++) {
                        Center *v = G.P_CENTER_POOLS.Planet.items[k];
                        if (str_eq(v->config.hand_type, hand)) planet = v->key;
                    }
                }
                card = create_card("Planet", G.pack_cards, false, RARITY_NIL, true, true, planet, "pl1");
            } else card = create_card("Planet", G.pack_cards, false, RARITY_NIL, true, true, NULL, "pl1");
        } else if (str_contains(name, "Spectral")) {
            card = create_card("Spectral", G.pack_cards, false, RARITY_NIL, true, true, NULL, "spe");
        } else if (str_contains(name, "Standard")) {
            const char *ante = num_str(G.GAME.round_resets.ante);
            card = create_card(pseudorandom_seed(pseudoseed(tfmt("stdset%s", ante))) > 0.6 ? "Enhanced" : "Base", G.pack_cards,
                               false, RARITY_NIL, false, true, NULL, "sta");
            EditionReq ed = poll_edition(tfmt("standard_edition%s", ante), 2, true, false);
            Card_set_edition(card, ed, false, false);
            double seal_rate = 10;
            double seal_poll = pseudorandom_seed(pseudoseed(tfmt("stdseal%s", ante)));
            if (seal_poll > 1 - 0.02 * seal_rate) {
                double st = pseudorandom_seed(pseudoseed(tfmt("stdsealtype%s", ante)));
                if (st > 0.75) Card_set_seal(card, "Red", false, false);
                else if (st > 0.5) Card_set_seal(card, "Blue", false, false);
                else if (st > 0.25) Card_set_seal(card, "Gold", false, false);
                else Card_set_seal(card, "Purple", false, false);
            }
        } else if (str_contains(name, "Buffoon")) {
            card = create_card("Joker", G.pack_cards, false, RARITY_NIL, true, true, NULL, "buf");
        }
        if (!card) continue;
        card->n.T->x = self->n.T->x;
        card->n.T->y = self->n.T->y;
        Col w2[2] = { G.C.WHITE, G.C.WHITE };
        Card_start_materialize(card, w2, 2, false, 1.5 * G.SETTINGS.GAMESPEED);
        vec_push(c->pack_cards, card);
    }
    return true;
}
EVFN(ev_open_emplace) {
    EVCTX(OpenCtx);
    if (G.pack_cards) {
        if (G.pack_cards->n.VT.y < G.ROOM->T->h) {
            for (int i = 0; i < c->pack_cards->count; i++) CardArea_emplace(G.pack_cards, c->pack_cards->items[i], NULL, false);
            return true;
        }
    }
    return false;
}
EVFN(ev_open_main) {
    EVCTX(CardCtx);
    Card *self = c->card;
    Card_explode(self, NULL, 0, NAN);
    OpenCtx *oc = NEW(OpenCtx);
    oc->self = self;
    oc->pack_cards = NEW(CardList);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1.3 * sqrt(G.SETTINGS.GAMESPEED), .blockable = 0, .blocking = 0,
                    .func = ev_open_create, .ctx = oc, .ctx_free = nofree));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1.3 * sqrt(G.SETTINGS.GAMESPEED), .blockable = 0, .blocking = 0,
                    .func = ev_open_emplace, .ctx = oc, .ctx_free = nofree));
    for (int i = 0; i < G.jokers->cards.count; i++)
        Card_calculate_joker(G.jokers->cards.items[i], JCTX(.open_booster = true, .card = self));
    if (G.GAME.modifiers.inflation) {
        G.GAME.inflation += 1;
        add_event(EVENT(.func = ev_set_costs_all));
    }
    return true;
}

void Card_open(Card *c) {
    if (!str_eq(c->ability.set, "Booster")) return;
    stop_use();
    G.STATE_COMPLETE = false;
    c->opening = true;
    if (!c->config.center->discovered) discover_card(c->config.center);
    c->n.states.hover.can = false;
    const char *name = c->ability.name;
    if (str_contains(name, "Arcana")) G.STATE = STATE_TAROT_PACK;
    else if (str_contains(name, "Celestial")) G.STATE = STATE_PLANET_PACK;
    else if (str_contains(name, "Spectral")) G.STATE = STATE_SPECTRAL_PACK;
    else if (str_contains(name, "Standard")) G.STATE = STATE_STANDARD_PACK;
    else if (str_contains(name, "Buffoon")) G.STATE = STATE_BUFFOON_PACK;
    G.GAME.pack_size = c->ability.extra.num;
    G.GAME.pack_choices = c->config.center->config.choose != 0 ? c->config.center->config.choose : 1;
    if (c->cost > 0) {
        CardCtx *cc = CTX(CardCtx); cc->card = c;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_open_juice, .ctx = cc));
        ease_dollars(-c->cost, false);
    } else delay(0.2);
    CardCtx *cc = CTX(CardCtx); cc->card = c;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_open_main, .ctx = cc));
}

/* ------------------------------------------------------------------ */
/* vouchers                                                            */
/* ------------------------------------------------------------------ */
typedef struct { Card *self; DynaText *top, *bot; } RedeemCtx;
EVFN(ev_redeem_show) {
    EVCTX(RedeemCtx);
    Card *self = c->self;
    c->top = DYNATEXT(.parts = dt_parts_str(localize_name_text(self->config.center->set, self->config.center->key)), .nparts = 1,
                      .colours = dt_cols(1, G.C.WHITE), .ncolours = 1, .rotate = true, .rotate_dir = 1, .shadow = true, .bump = true,
                      .float_ = true, .scale = 0.9, .pop_in = 0.6 / G.SPEEDFACTOR, .has_pop_in = true, .pop_in_rate = 1.5 * G.SPEEDFACTOR);
    c->bot = DYNATEXT(.parts = dt_parts_str(localize_key("k_redeemed_ex")), .nparts = 1,
                      .colours = dt_cols(1, G.C.WHITE), .ncolours = 1, .rotate = true, .rotate_dir = 2, .shadow = true, .bump = true,
                      .float_ = true, .scale = 0.9, .pop_in = 1.4 / G.SPEEDFACTOR, .has_pop_in = true, .pop_in_rate = 1.5 * G.SPEEDFACTOR,
                      .pitch_shift = 0.25);
    Card_juice_up(&self->n, 0.3, 0.5);
    sound_play("card1", NAN, NAN);
    sound_play("coin1", NAN, NAN);
    UIBox *top = uibox_new(UIROOT(CFG(.align = "tm", .r = 0.15, .colour = G.C.CLEAR, .padding = 0.15), UIO(CFG(.object = &c->top->n))),
                           UIBCFG(.align = "tm", .has_offset = true, .offset = { 0, 0 }, .parent = &self->n));
    child_set(&self->n, "top_disp", &top->n);
    UIBox *bot = uibox_new(UIROOT(CFG(.align = "tm", .r = 0.15, .colour = G.C.CLEAR, .padding = 0.15), UIO(CFG(.object = &c->bot->n))),
                           UIBCFG(.align = "bm", .has_offset = true, .offset = { 0, 0 }, .parent = &self->n));
    child_set(&self->n, "bot_disp", &bot->n);
    return true;
}
EVFN(ev_redeem_popout) {
    EVCTX(RedeemCtx);
    if (c->top) DynaText_pop_out(c->top, 4);
    if (c->bot) DynaText_pop_out(c->bot, 4);
    return true;
}
EVFN(ev_redeem_remove) {
    EVCTX(RedeemCtx);
    Node *t = child_get(&c->self->n, "top_disp");
    if (t) { child_set(&c->self->n, "top_disp", NULL); node_remove(t); }
    Node *b = child_get(&c->self->n, "bot_disp");
    if (b) { child_set(&c->self->n, "bot_disp", NULL); node_remove(b); }
    return true;
}

void Card_redeem(Card *c) {
    if (!str_eq(c->ability.set, "Voucher")) return;
    stop_use();
    if (!c->config.center->discovered) discover_card(c->config.center);
    if (c->shop_voucher) G.GAME.current_round.voucher = NULL;
    c->n.states.hover.can = false;
    strset_add(&G.GAME.used_vouchers, c->config.center_key);
    RedeemCtx *rc = NEW(RedeemCtx);
    rc->self = c;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.4, .func = ev_redeem_show, .ctx = rc, .ctx_free = nofree));
    ease_dollars(-c->cost, false);
    inc_career_stat("c_shop_dollars_spent", c->cost);
    inc_career_stat("c_vouchers_bought", 1);
    set_voucher_usage(c);
    UNLOCK(.type = "run_redeem");
    G.GAME.current_round.voucher = NULL;
    Card_apply_to_run(c, NULL);
    delay(0.6);
    for (int i = 0; i < G.jokers->cards.count; i++)
        Card_calculate_joker(G.jokers->cards.items[i], JCTX(.buying_card = true, .card = c));
    if (G.GAME.modifiers.inflation) {
        G.GAME.inflation += 1;
        add_event(EVENT(.func = ev_set_costs_all));
    }
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 2.6, .func = ev_redeem_popout, .ctx = rc, .ctx_free = nofree));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.5, .func = ev_redeem_remove, .ctx = rc, .ctx_free = nofree));
}

typedef struct { const char *name; Extra extra; Card *self; } ApplyCtx;
EVFN(ev_apply) {
    EVCTX(ApplyCtx);
    const char *n = c->name;
    double ex = c->extra.num;
    if (!strcmp(n, "Overstock") || !strcmp(n, "Overstock Plus")) change_shop_size(1);
    else if (!strcmp(n, "Tarot Merchant") || !strcmp(n, "Tarot Tycoon")) G.GAME.tarot_rate = 4 * ex;
    else if (!strcmp(n, "Planet Merchant") || !strcmp(n, "Planet Tycoon")) G.GAME.planet_rate = 4 * ex;
    else if (!strcmp(n, "Hone") || !strcmp(n, "Glow Up")) G.GAME.edition_rate = ex;
    else if (!strcmp(n, "Magic Trick") || !strcmp(n, "Illusion")) G.GAME.playing_card_rate = ex;
    else if (!strcmp(n, "Crystal Ball")) G.consumeables->config.card_limit += 1;
    else if (!strcmp(n, "Clearance Sale") || !strcmp(n, "Liquidation")) {
        G.GAME.discount_percent = ex;
        for (int i = 0; i < G.I.CARD.count; i++) if (IS(G.I.CARD.items[i], NT_CARD)) Card_set_cost((Card *)G.I.CARD.items[i]);
    } else if (!strcmp(n, "Reroll Surplus") || !strcmp(n, "Reroll Glut")) {
        /* the original uses self.ability.extra here */
        double e2 = c->self ? c->self->ability.extra.num : ex;
        G.GAME.round_resets.reroll_cost -= e2;
        G.GAME.current_round.reroll_cost = MAX(0, G.GAME.current_round.reroll_cost - e2);
    } else if (!strcmp(n, "Seed Money") || !strcmp(n, "Money Tree")) G.GAME.interest_cap = ex;
    else if (!strcmp(n, "Antimatter")) { if (G.jokers) G.jokers->config.card_limit += 1; }
    return true;
}

void Card_apply_to_run(Card *self, Center *center) {
    const char *name = center ? center->name : (self ? self->ability.name : NULL);
    Extra extra = center ? center->config.extra : (self ? self->ability.extra : (Extra){ 0 });
    if (!name) return;
    double ex = extra.num;
    bool ev = !strcmp(name, "Overstock") || !strcmp(name, "Overstock Plus") || !strcmp(name, "Tarot Merchant") || !strcmp(name, "Tarot Tycoon") ||
              !strcmp(name, "Planet Merchant") || !strcmp(name, "Planet Tycoon") || !strcmp(name, "Hone") || !strcmp(name, "Glow Up") ||
              !strcmp(name, "Magic Trick") || !strcmp(name, "Illusion") || !strcmp(name, "Crystal Ball") || !strcmp(name, "Clearance Sale") ||
              !strcmp(name, "Liquidation") || !strcmp(name, "Reroll Surplus") || !strcmp(name, "Reroll Glut") || !strcmp(name, "Seed Money") ||
              !strcmp(name, "Money Tree") || !strcmp(name, "Antimatter");
    if (ev) {
        ApplyCtx *ac = CTX(ApplyCtx);
        ac->name = name; ac->extra = extra; ac->self = self;
        if ((!strcmp(name, "Reroll Surplus") || !strcmp(name, "Reroll Glut")) && !self) ac->self = NULL;
        add_event(EVENT(.func = ev_apply, .ctx = ac));
    }
    if (!strcmp(name, "Grabber") || !strcmp(name, "Nacho Tong")) {
        G.GAME.round_resets.hands += ex;
        ease_hands_played(ex, false);
    }
    if (!strcmp(name, "Paint Brush") || !strcmp(name, "Palette")) CardArea_change_size(G.hand, 1);
    if (!strcmp(name, "Wasteful") || !strcmp(name, "Recyclomancy")) {
        G.GAME.round_resets.discards += ex;
        ease_discard(ex, false, false);
    }
    if (!strcmp(name, "Blank")) UNLOCK(.type = "blank_redeems");
    if (!strcmp(name, "Hieroglyph") || !strcmp(name, "Petroglyph")) {
        ease_ante(-ex);
        if (G.GAME.round_resets.blind_ante == 0) G.GAME.round_resets.blind_ante = G.GAME.round_resets.ante;
        G.GAME.round_resets.blind_ante -= ex;
        if (!strcmp(name, "Hieroglyph")) {
            G.GAME.round_resets.hands -= ex;
            ease_hands_played(-ex, false);
        }
        if (!strcmp(name, "Petroglyph")) {
            G.GAME.round_resets.discards -= ex;
            ease_discard(-ex, false, false);
        }
    }
}

/* ------------------------------------------------------------------ */
/* explode / shatter / dissolve / materialize                          */
/* ------------------------------------------------------------------ */
typedef struct { Card *self; double start_time, explode_time; Particles *p1, *p2; } ExplodeCtx;
EVFN(ev_explode_shake) {
    EVCTX(ExplodeCtx);
    Card *self = c->self;
    if (self->n.juice.active) {
        double percent = (G.TIMERS.TOTAL - c->start_time) / c->explode_time;
        double t = G.TIMERS.TOTAL;
        self->n.juice.r = 0.05 * (sin(5 * t) + cos(0.33 + 41.15332 * t) + cos(67.12 * t)) * percent;
        self->n.juice.scale = percent * 0.15;
    }
    return G.TIMERS.TOTAL - c->start_time > 1.5 * c->explode_time;
}
EVFN(ev_explode_burst) {
    EVCTX(ExplodeCtx);
    Card *self = c->self;
    c->p2 = PARTICLES(0, 0, 0, 0, .timer_type = TIMER_TOTAL, .has_timer_type = true, .pulse_max = 30, .timer = 0.003, .scale = 0.6,
                      .speed = 15, .lifespan = 0.5, .attach = &self->n, .colours = cols_dup(self->n.dissolve_colours, self->n.n_dissolve_colours),
                      .ncolours = self->n.n_dissolve_colours);
    Moveable_set_role(&c->p2->n, ROLE_ARGS(.r_bond = BOND_WEAK));
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .ref = REFN(&self->n.dissolve), .ease_to = 1, .delay = 0.1 * c->explode_time));
    Card_juice_up(&self->n, NAN, NAN);
    G.VIBRATION += 1;
    sound_play("explosion_release1", NAN, NAN);
    Particles_fade(c->p1, 0.3 * c->explode_time, NAN);
    return true;
}
EVFN(ev_explode_shrink) {
    EVCTX(ExplodeCtx);
    if (c->p2) add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .blocking = 0, .ref = REFN(&c->p2->scale), .ease_to = 0, .delay = 0.1 * c->explode_time));
    return true;
}
EVFN(ev_card_remove) {
    EVCTX(CardCtx);
    node_remove(&c->card->n);
    return true;
}

static void set_dissolve_colours(Card *c, Col *cols, int n) {
    c->n.n_dissolve_colours = MIN(n, 4);
    for (int i = 0; i < c->n.n_dissolve_colours; i++) c->n.dissolve_colours[i] = cols[i];
}

void Card_explode(Card *c, Col *colours, int ncolours, double time_fac) {
    double explode_time = 1.3 * (isnan(time_fac) ? 1 : time_fac) * sqrt(G.SETTINGS.GAMESPEED);
    c->n.dissolve = 0;
    if (colours && ncolours > 0) set_dissolve_colours(c, colours, ncolours);
    else { Col w[1] = { G.C.WHITE }; set_dissolve_colours(c, w, 1); }
    double start_time = G.TIMERS.TOTAL;
    sound_play("explosion_buildup1", NAN, NAN);
    c->n.juice.active = true;
    c->n.juice.scale = 0;
    c->n.juice.r = 0;
    c->n.juice.handled_elsewhere = true;
    c->n.juice.start_time = start_time;
    c->n.juice.end_time = start_time + explode_time;
    ExplodeCtx *ec = NEW(ExplodeCtx);
    ec->self = c; ec->start_time = start_time; ec->explode_time = explode_time;
    ec->p1 = PARTICLES(0, 0, 0, 0, .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 0.01 * explode_time, .scale = 0.2,
                       .speed = 2, .lifespan = 0.2 * explode_time, .attach = &c->n,
                       .colours = cols_dup(c->n.dissolve_colours, c->n.n_dissolve_colours), .ncolours = c->n.n_dissolve_colours, .fill = true);
    add_event(EVENT(.blockable = 0, .func = ev_explode_shake, .ctx = ec, .ctx_free = nofree));
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .ref = REFN(&c->n.dissolve), .ease_to = 0.3, .delay = 0.9 * explode_time));
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.9 * explode_time, .func = ev_explode_burst, .ctx = ec, .ctx_free = nofree));
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 1.4 * explode_time, .func = ev_explode_shrink, .ctx = ec, .ctx_free = nofree));
    CardCtx *rc = CTX(CardCtx); rc->card = c;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 1.5 * explode_time, .func = ev_card_remove, .ctx = rc));
}

typedef struct { Particles *p; double t; } FadeCtx;
EVFN(ev_fade) {
    EVCTX(FadeCtx);
    Particles_fade(c->p, c->t, NAN);
    return true;
}
EVFN(ev_glass_sound) {
    (void)e_; (void)p_;
    sound_play(tfmt("glass%d", math_random_range(1, 6)), math_random() * 0.2 + 0.9, 0.5);
    sound_play("generic1", math_random() * 0.2 + 0.9, 0.5);
    return true;
}

void Card_shatter(Card *c) {
    double dt = 0.7;
    c->shattered = true;
    c->n.dissolve = 0;
    Col w[1] = { col_new(1, 1, 1, 0.8f) };
    set_dissolve_colours(c, w, 1);
    Card_juice_up(&c->n, NAN, NAN);
    Particles *p = PARTICLES(0, 0, 0, 0, .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 0.007 * dt, .scale = 0.3, .speed = 4,
                             .lifespan = 0.5 * dt, .attach = &c->n, .colours = cols_dup(c->n.dissolve_colours, 1), .ncolours = 1, .fill = true);
    FadeCtx *fc = CTX(FadeCtx); fc->p = p; fc->t = 0.15 * dt;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.5 * dt, .func = ev_fade, .ctx = fc));
    add_event(EVENT(.blockable = 0, .func = ev_glass_sound));
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .ref = REFN(&c->n.dissolve), .ease_to = 1, .delay = 0.5 * dt));
    CardCtx *rc = CTX(CardCtx); rc->card = c;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.55 * dt, .func = ev_card_remove, .ctx = rc));
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.51 * dt));
}

EVFN(ev_dissolve_sound) {
    (void)e_; (void)p_;
    sound_play("whoosh2", math_random() * 0.2 + 0.9, 0.5);
    sound_play(tfmt("crumple%d", math_random_range(1, 5)), math_random() * 0.2 + 0.9, 0.5);
    return true;
}

void Card_start_dissolve(Card *c, Col *colours, int ncolours, bool silent, double time_fac, bool no_juice) {
    double dt = 0.7 * (isnan(time_fac) ? 1 : time_fac);
    c->n.dissolve = 0;
    if (colours && ncolours > 0) set_dissolve_colours(c, colours, ncolours);
    else {
        Col d[5] = { G.C.BLACK, G.C.ORANGE, G.C.RED, G.C.GOLD, G.C.JOKER_GREY };
        set_dissolve_colours(c, d, 4);
        /* Lua keeps all five for particles */
        colours = d; ncolours = 5;
        Particles *p = PARTICLES(0, 0, 0, 0, .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 0.01 * dt, .scale = 0.1, .speed = 2,
                                 .lifespan = 0.7 * dt, .attach = &c->n, .colours = cols_dup(d, 5), .ncolours = 5, .fill = true);
        if (!no_juice) Card_juice_up(&c->n, NAN, NAN);
        FadeCtx *fc = CTX(FadeCtx); fc->p = p; fc->t = 0.3 * dt;
        add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.7 * dt, .func = ev_fade, .ctx = fc));
        goto after_particles;
    }
    if (!no_juice) Card_juice_up(&c->n, NAN, NAN);
    {
        Particles *p = PARTICLES(0, 0, 0, 0, .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 0.01 * dt, .scale = 0.1, .speed = 2,
                                 .lifespan = 0.7 * dt, .attach = &c->n, .colours = cols_dup(colours, ncolours), .ncolours = ncolours, .fill = true);
        FadeCtx *fc = CTX(FadeCtx); fc->p = p; fc->t = 0.3 * dt;
        add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.7 * dt, .func = ev_fade, .ctx = fc));
    }
after_particles:
    if (!silent) add_event(EVENT(.blockable = 0, .func = ev_dissolve_sound));
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .ref = REFN(&c->n.dissolve), .ease_to = 1, .delay = 1 * dt));
    CardCtx *rc = CTX(CardCtx); rc->card = c;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 1.05 * dt, .func = ev_card_remove, .ctx = rc));
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 1.051 * dt));
}

EVFN(ev_materialize_sound) {
    (void)e_; (void)p_;
    sound_play("whoosh1", math_random() * 0.1 + 0.6, 0.3);
    sound_play(tfmt("crumple%d", math_random_range(1, 5)), math_random() * 0.2 + 1.2, 0.8);
    return true;
}
EVFN(ev_materialize_stop) {
    EVCTX(CardCtx);
    Particles *p = (Particles *)child_get(&c->card->n, "particles");
    if (p) p->max = 0;
    return true;
}
EVFN(ev_materialize_done) {
    EVCTX(CardCtx);
    c->card->n.states.hover.can = true;
    Node *p = child_get(&c->card->n, "particles");
    if (p) { child_set(&c->card->n, "particles", NULL); node_remove(p); }
    return true;
}

void Card_start_materialize(Card *c, Col *colours, int ncolours, bool silent, double timefac) {
    double dt = 0.6 * (isnan(timefac) ? 1 : timefac);
    c->n.states.visible = true;
    c->n.states.hover.can = false;
    c->n.dissolve = 1;
    if (colours && ncolours > 0) set_dissolve_colours(c, colours, ncolours);
    else {
        const char *set = c->ability.set;
        Col d[2];
        int n = 1;
        if (str_eq(set, "Joker")) d[0] = G.C.RARITY[c->config.center->rarity];
        else if (str_eq(set, "Planet")) d[0] = G.C.SECONDARY_SET.Planet;
        else if (str_eq(set, "Tarot")) d[0] = G.C.SECONDARY_SET.Tarot;
        else if (str_eq(set, "Spectral")) d[0] = G.C.SECONDARY_SET.Spectral;
        else if (str_eq(set, "Booster")) d[0] = G.C.BOOSTER;
        else if (str_eq(set, "Voucher")) { d[0] = G.C.SECONDARY_SET.Voucher; d[1] = G.C.CLEAR; n = 2; }
        else d[0] = G.C.GREEN;
        set_dissolve_colours(c, d, n);
    }
    Card_juice_up(&c->n, NAN, NAN);
    Particles *p = PARTICLES(0, 0, 0, 0, .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 0.025 * dt, .scale = 0.25, .speed = 3,
                             .lifespan = 0.7 * dt, .attach = &c->n, .colours = cols_dup(c->n.dissolve_colours, c->n.n_dissolve_colours),
                             .ncolours = c->n.n_dissolve_colours, .fill = true);
    /* as in the original the particles are both an array child (added by the
     * constructor) and the named child "particles" */
    child_set(&c->n, "particles", &p->n);
    if (!silent) {
        if (!G.has_last_materialized || G.last_materialized + 0.01 < G.TIMERS.REAL || G.last_materialized > G.TIMERS.REAL) {
            G.last_materialized = G.TIMERS.REAL;
            G.has_last_materialized = true;
            add_event(EVENT(.blockable = 0, .func = ev_materialize_sound));
        }
    }
    CardCtx *c1 = CTX(CardCtx); c1->card = c;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.5 * dt, .func = ev_materialize_stop, .ctx = c1));
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .ref = REFN(&c->n.dissolve), .ease_to = 0, .delay = 1 * dt));
    CardCtx *c2 = CTX(CardCtx); c2->card = c;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 1.05 * dt, .func = ev_materialize_done, .ctx = c2));
}

void Card_calculate_rental(Card *c) {
    if (c->ability.rental) {
        ease_dollars(-G.GAME.rental_rate, false);
        card_eval_status_text(&c->n, "dollars", -G.GAME.rental_rate, 0, NULL, NULL);
    }
}

void Card_calculate_perishable(Card *c) {
    if (c->ability.perishable && c->ability.perish_tally > 0) {
        if (c->ability.perish_tally == 1) {
            c->ability.perish_tally = 0;
            card_eval_status_text(&c->n, "extra", 0, 0, NULL, SEXTRA(.message = localize_key("k_disabled_ex"), .colour = G.C.FILTER, .delay = 0.45, .has_delay = true));
            Card_set_debuff(c, false);
        } else {
            c->ability.perish_tally -= 1;
            char *m = localize_variable("a_remaining", LV(num_str(c->ability.perish_tally)));
            card_eval_status_text(&c->n, "extra", 0, 0, NULL, SEXTRA(.message = m, .colour = G.C.FILTER, .delay = 0.45, .has_delay = true));
        }
    }
}

/* ------------------------------------------------------------------ */
/* suits / areas / flipping                                            */
/* ------------------------------------------------------------------ */
bool Card_is_suit(Card *c, const char *suit, bool bypass_debuff, bool flush_calc) {
    bool red_a = str_eq(c->base.suit, "Hearts") || str_eq(c->base.suit, "Diamonds");
    bool red_b = str_eq(suit, "Hearts") || str_eq(suit, "Diamonds");
    if (flush_calc) {
        if (str_eq(c->ability.effect, "Stone Card")) return false;
        if (card_name_is(c, "Wild Card") && !c->debuff) return true;
        if (has_joker("Smeared Joker") && red_a == red_b) return true;
        return str_eq(c->base.suit, suit);
    }
    if (c->debuff && !bypass_debuff) return false;
    if (str_eq(c->ability.effect, "Stone Card")) return false;
    if (card_name_is(c, "Wild Card")) return true;
    if (has_joker("Smeared Joker") && red_a == red_b) return true;
    return str_eq(c->base.suit, suit);
}

void Card_set_card_area(Card *c, CardArea *area) {
    c->area = area;
    c->n.parent = &area->n;
    c->n.layered_parallax = area->n.layered_parallax;
}
void Card_remove_from_area(Card *c) {
    c->area = NULL;
    c->n.parent = NULL;
    c->n.layered_parallax = (Vec2){ 0, 0 };
}

void Card_align(Card *c) {
    Sprite *fs = CH(c, "floating_sprite");
    if (fs) {
        fs->n.T->y = c->n.T->y;
        fs->n.T->x = c->n.T->x;
        fs->n.T->r = c->n.T->r;
    }
    Node *fu = child_get(&c->n, "focused_ui");
    if (fu) card_focus_ui_set_alignment(c);
}

void Card_flip(Card *c) {
    if (c->facing == FACE_FRONT) {
        c->flipping = FLIP_F2B;
        c->facing = FACE_BACK;
        c->n.pinch->x = true;
    } else {
        c->ability.wheel_flipped = false;
        c->flipping = FLIP_B2F;
        c->facing = FACE_FRONT;
        c->n.pinch->x = true;
    }
}

static void Card_update(Node *n, double dt) {
    (void)dt;
    Card *c = (Card *)n;
    if (c->flipping == FLIP_F2B && n->VT.w <= 0) { c->sprite_facing = FACE_BACK; n->pinch->x = false; }
    if (c->flipping == FLIP_B2F && n->VT.w <= 0) { c->sprite_facing = FACE_FRONT; n->pinch->x = false; }
    Node *fu = child_get(n, "focused_ui");
    if (!n->states.focus.is && fu) { child_set(n, "focused_ui", NULL); node_remove(fu); }
    Card_update_alert(c);
    if (str_eq(c->ability.set, "Joker") && !c->sticker_run) {
        const char *s = get_joker_win_sticker(c->config.center);
        c->sticker_run = s ? s : "NONE";
    }
    if (c->ability.consumeable && c->ability.consumeable->max_highlighted != 0)
        c->ability.consumeable->mod_num = MIN(5, c->ability.consumeable->max_highlighted);
    const char *nm = c->ability.name;
    if (G.STAGE == STAGE_RUN) {
        if (c->ability.perma_debuff) c->debuff = true;
        if (c->area && (c->area == G.jokers || c->area == G.consumeables)) {
            c->bypass_lock = true;
            c->bypass_discovery_center = true;
            c->bypass_discovery_ui = true;
        }
        c->sell_cost_label_q = c->facing == FACE_BACK;
        c->sell_cost_label = c->sell_cost;
        snprintf(c->sell_cost_label_str, sizeof c->sell_cost_label_str, "%s", c->sell_cost_label_q ? "?" : num_str(c->sell_cost));
        if (str_eq(nm, "Temperance")) {
            c->ability.money = 0;
            for (int i = 0; i < G.jokers->cards.count; i++)
                if (str_eq(G.jokers->cards.items[i]->ability.set, "Joker")) c->ability.money += G.jokers->cards.items[i]->sell_cost;
            c->ability.money = MIN(c->ability.money, c->ability.extra.num);
        }
        if (str_eq(nm, "Throwback")) c->ability.x_mult = 1 + G.GAME.skips * c->ability.extra.num;
        if (str_eq(nm, "Driver's License")) {
            c->ability.driver_tally = 0; c->ability.has_driver_tally = true;
            Center *base = P_CENTER("c_base");
            for (int i = 0; i < G.playing_cards.count; i++) if (((Card *)G.playing_cards.items[i])->config.center != base) c->ability.driver_tally++;
        }
        if (str_eq(nm, "Steel Joker")) {
            c->ability.steel_tally = 0; c->ability.has_steel_tally = true;
            Center *st = P_CENTER("m_steel");
            for (int i = 0; i < G.playing_cards.count; i++) if (((Card *)G.playing_cards.items[i])->config.center == st) c->ability.steel_tally++;
        }
        if (str_eq(nm, "Cloud 9")) {
            c->ability.nine_tally = 0; c->ability.has_nine_tally = true;
            for (int i = 0; i < G.playing_cards.count; i++) if (Card_get_id((Card *)G.playing_cards.items[i]) == 9) c->ability.nine_tally++;
        }
        if (str_eq(nm, "Stone Joker")) {
            c->ability.stone_tally = 0; c->ability.has_stone_tally = true;
            Center *st = P_CENTER("m_stone");
            for (int i = 0; i < G.playing_cards.count; i++) if (((Card *)G.playing_cards.items[i])->config.center == st) c->ability.stone_tally++;
        }
        if (str_eq(nm, "Joker Stencil")) {
            c->ability.x_mult = G.jokers->config.card_limit - G.jokers->cards.count;
            for (int i = 0; i < G.jokers->cards.count; i++) if (card_name_is(G.jokers->cards.items[i], "Joker Stencil")) c->ability.x_mult += 1;
        }
        if (str_eq(nm, "The Wheel of Fortune")) {
            vec_clear(&c->eligible_strength_jokers);
            for (int i = 0; i < G.jokers->cards.count; i++) {
                Card *v = G.jokers->cards.items[i];
                if (str_eq(v->ability.set, "Joker") && !v->edition) vec_push(&c->eligible_strength_jokers, v);
            }
        }
        if (str_eq(nm, "Ectoplasm") || str_eq(nm, "Hex")) {
            vec_clear(&c->eligible_editionless_jokers);
            for (int i = 0; i < G.jokers->cards.count; i++) {
                Card *v = G.jokers->cards.items[i];
                if (str_eq(v->ability.set, "Joker") && !v->edition) vec_push(&c->eligible_editionless_jokers, v);
            }
        }
        if (str_eq(nm, "Blueprint") || str_eq(nm, "Brainstorm")) {
            Card *other = NULL;
            if (str_eq(nm, "Brainstorm")) other = G.jokers->cards.count > 0 ? G.jokers->cards.items[0] : NULL;
            else {
                for (int i = 0; i < G.jokers->cards.count; i++)
                    if (G.jokers->cards.items[i] == c) other = i + 1 < G.jokers->cards.count ? G.jokers->cards.items[i + 1] : NULL;
            }
            if (other && other != c && other->config.center->blueprint_compat) c->ability.blueprint_compat = "compatible";
            else c->ability.blueprint_compat = "incompatible";
        }
        if (str_eq(nm, "Swashbuckler")) {
            double sell = 0;
            for (int i = 0; i < G.jokers->cards.count; i++) {
                Card *v = G.jokers->cards.items[i];
                if (v != c && v->area && v->area == G.jokers) sell += v->sell_cost;
            }
            c->ability.mult = sell;
        }
    } else {
        if (str_eq(nm, "Temperance")) c->ability.money = 0;
    }
}

void Card_hard_set_T(Node *n, double x, double y, double w, double h) {
    Card *c = (Card *)n;
    if (isnan(x)) x = n->T->x;
    if (isnan(y)) y = n->T->y;
    if (isnan(w)) w = n->T->w;
    if (isnan(h)) h = n->T->h;
    Moveable_hard_set_T(n, x, y, w, h);
    Sprite *f = CH(c, "front"), *b = CH(c, "back"), *ce = CH(c, "center");
    if (f) Moveable_hard_set_T(&f->n, x, y, w, h);
    if (b) Moveable_hard_set_T(&b->n, x, y, w, h);
    if (ce) Moveable_hard_set_T(&ce->n, x, y, w, h);
}

static void Card_move(Node *n, double dt) {
    Card *c = (Card *)n;
    Moveable_move(n, dt);
    Node *hp = child_get(n, "h_popup");
    if (hp) {
        UIBoxConfig *cfg = Card_align_h_popup(c);
        Moveable_set_alignment(hp, ALIGN_ARGS(.major = cfg->major, .type = cfg->align, .bond = cfg->xy_bond,
                                              .r_bond = cfg->r_bond, .wh_bond = cfg->wh_bond, .has_offset = true, .offset = cfg->offset));
        free(cfg);
    }
}

UIBoxConfig *Card_align_h_popup(Card *c) {
    Node *fu = child_get(&c->n, "focused_ui");
    bool focused_ui = fu != NULL;
    const char *dir;
    if (child_get(&c->n, "buy_button") || (c->area && c->area->config.view_deck) || (c->area && str_eq(c->area->config.type, "shop"))) dir = "cl";
    else if (c->n.T->y < G.CARD_H * 0.8) dir = "bm";
    else dir = "tm";
    double ox, oy;
    if (strcmp(dir, "cl") != 0) ox = 0;
    else if (focused_ui) ox = -0.05;
    else if (c->ability.consumeable) ox = 0.0;
    else if (str_eq(c->ability.set, "Voucher")) ox = 0.0;
    else ox = -0.05;
    if (focused_ui) {
        if (!strcmp(dir, "tm")) oy = (c->area && c->area == G.hand) ? -0.08 : -0.15;
        else if (!strcmp(dir, "bm")) oy = 0.12;
        else oy = 0;
    } else if (!strcmp(dir, "tm")) oy = -0.13;
    else if (!strcmp(dir, "bm")) oy = 0.1;
    else oy = 0;
    return UIBCFG(.major = fu ? fu : &c->n, .parent = &c->n, .xy_bond = BOND_STRONG, .r_bond = BOND_WEAK, .wh_bond = BOND_WEAK,
                  .has_offset = true, .offset = { ox, oy }, .align = dir);
}

static void Card_hover(Node *n) {
    Card *c = (Card *)n;
    if (n->o_hover) { n->o_hover(n); return; }
    Card_juice_up(n, 0.05, 0.03);
    sound_play("paper1", math_random() * 0.2 + 0.9, 0.35);
    if (n->states.focus.is && !child_get(n, "focused_ui")) {
        UIBox *f = UIDEF_card_focus_ui(c);
        if (f) child_set(n, "focused_ui", &f->n);
    }
    if (c->facing == FACE_FRONT && (!n->states.drag.is || G.CONTROLLER.HID.touch) && !c->no_ui && !G.debug_tooltip_toggle) {
        Node *alert = child_get(n, "alert");
        if (alert && !c->config.center->alerted) {
            c->config.center->alerted = true;
            game_save_progress();
        }
        c->ability_UIBox_table = Card_generate_UIBox_ability_table(c);
        n->h_popup = UIDEF_card_h_popup(n, c->ability_UIBox_table);
        n->h_popup_config = Card_align_h_popup(c);
        Node_hover(n);
    }
}
static void Card_stop_hover(Node *n) {
    if (n->o_stop_hover) { n->o_stop_hover(n); return; }
    Node_stop_hover(n);
}

void Card_juice_up(Node *n, double scale, double rot_amount) {
    double rot;
    if (!isnan(rot_amount)) rot = 0.4 * (math_random() > 0.5 ? 1 : -1) * rot_amount;
    else rot = (math_random() > 0.5 ? 1 : -1) * 0.16;
    scale = !isnan(scale) ? scale * 0.4 : 0.11;
    Moveable_juice_up(n, scale, rot);
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */
static void ds(Sprite *s, const char *shader, Card *c, bool send) {
    if (!s) return;
    Sprite_draw_shader_send(s, shader, send ? c->send_to_shader : NULL, send ? 2 : 0);
}
static void ds_other(Sprite *s, const char *shader, Card *c, bool send, bool no_tilt, Sprite *other, double ms, double mr) {
    if (!s) return;
    s->n.role.draw_major = &c->n;
    Sprite_draw_shader_ex(s, shader, 0, false, send ? c->send_to_shader : NULL, send ? 2 : 0, no_tilt, other ? &other->n : NULL, ms, mr, 0, 0);
}

void Card_draw_layer(Card *c, const char *layer) {
    Node *n = &c->n;
    if (!layer) layer = "both";
    bool both = !strcmp(layer, "both");
    bool shadow_l = both || !strcmp(layer, "shadow");
    bool card_l = both || !strcmp(layer, "card");
    n->hover_tilt = 1;
    if (!n->states.visible) return;
    if (shadow_l) {
        c->send_to_shader[0] = (float)(MIN(n->VT.r * 3, 1) + G.TIMERS.REAL / 28 + (n->juice.active ? n->juice.r * 20 : 0) + n->tilt_var.amt);
        c->send_to_shader[1] = (float)G.TIMERS.REAL;
        for (int i = 0; i < n->children.count; i++) {
            Node *v = n->children.items[i].node;
            if (v->is_moveable) v->VT.scale = n->VT.scale;
        }
    }
    Sprite *center = CH(c, "center"), *back = CH(c, "back"), *front = CH(c, "front");
    G.shared_shadow = c->sprite_facing == FACE_FRONT ? center : back;
    if (!c->no_shadow && str_eq(G.SETTINGS.GRAPHICS.shadows, "On") && shadow_l &&
        (!str_eq(c->ability.effect, "Glass Card") && !c->greyed) &&
        ((c->area && c->area != G.discard && !str_eq(c->area->config.type, "deck")) || !c->area || n->states.drag.is)) {
        n->shadow_height = 0 * (0.08 + 0.4 * sqrt(n->velocity.x * n->velocity.x)) +
            (((c->highlighted && c->area == G.play) || n->states.drag.is) ? 0.35 : (c->area && str_eq(c->area->config.type, "title_2")) ? 0.04 : 0.1);
        if (G.shared_shadow) Sprite_draw_shader_simple(G.shared_shadow, "dissolve", n->shadow_height, true);
    }
    if (card_l && c->area != G.hand) {
        Node *fu = child_get(n, "focused_ui");
        if (fu) node_draw(fu);
    }
    if (card_l) {
        double tilt_factor = 0.3;
        if (n->states.focus.is) {
            n->tilt_var.mx = G.CONTROLLER.cursor_position.x + n->tilt_var.dx * n->T->w * G.TILESCALE * G.TILESIZE;
            n->tilt_var.my = G.CONTROLLER.cursor_position.y + n->tilt_var.dy * n->T->h * G.TILESCALE * G.TILESIZE;
            n->tilt_var.amt = fabs(n->hover_offset.y + n->hover_offset.x - 1 + n->tilt_var.dx + n->tilt_var.dy - 1) * tilt_factor;
        } else if (n->states.hover.is) {
            n->tilt_var.mx = G.CONTROLLER.cursor_position.x;
            n->tilt_var.my = G.CONTROLLER.cursor_position.y;
            n->tilt_var.amt = fabs(n->hover_offset.y + n->hover_offset.x - 1) * tilt_factor;
        } else {
            /* ambient_tilt is always present on cards (it may be 0) */
            double ta = G.TIMERS.REAL * (1.56 + lua_mod(n->ID / 1.14212, 1)) + n->ID / 1.35122;
            n->tilt_var.mx = ((0.5 + 0.5 * n->ambient_tilt * cos(ta)) * n->VT.w + n->VT.x + G.ROOM->T->x) * G.TILESIZE * G.TILESCALE;
            n->tilt_var.my = ((0.5 + 0.5 * n->ambient_tilt * sin(ta)) * n->VT.h + n->VT.y + G.ROOM->T->y) * G.TILESIZE * G.TILESCALE;
            n->tilt_var.amt = n->ambient_tilt * (0.5 + cos(ta)) * tilt_factor;
        }
        Node *pp = child_get(n, "particles");
        if (pp) node_draw(pp);
        Node *price = child_get(n, "price");
        if (price) node_draw(price);
        Node *buy = child_get(n, "buy_button");
        if (buy) {
            if (c->highlighted) {
                buy->states.visible = true;
                node_draw(buy);
                Node *bu = child_get(n, "buy_and_use_button");
                if (bu) node_draw(bu);
            } else buy->states.visible = false;
        }
        Node *use = child_get(n, "use_button");
        if (use && c->highlighted) node_draw(use);

        bool disc = c->config.center->discovered || c->bypass_discovery_center;
        bool stone = str_eq(c->ability.effect, "Stone Card");
        if (c->vortex) {
            if (c->facing == FACE_BACK) Sprite_draw_shader_simple(back, "vortex", 0, false);
            else {
                Sprite_draw_shader_simple(center, "vortex", 0, false);
                if (front) Sprite_draw_shader_simple(front, "vortex", 0, false);
            }
            gfx_set_shader(NULL);
        } else if (c->sprite_facing == FACE_FRONT) {
            bool negative = (c->edition && c->edition->negative) || (card_name_is(c, "Antimatter") && disc);
            if (negative) {
                ds(center, "negative", c, true);
                if (front && !stone) ds(front, "negative", c, true);
            } else if (!c->greyed) {
                ds(center, "dissolve", c, false);
                if (front && !stone) ds(front, "dissolve", c, false);
            }
            if (!c->config.center->discovered && (c->ability.consumeable || c->config.center->unlocked) && !c->config.center->demo && !c->bypass_discovery_center) {
                Sprite *shared = (str_eq(c->ability.set, "Edition") || str_eq(c->ability.set, "Joker")) ? G.shared_undiscovered_joker : G.shared_undiscovered_tarot;
                double sm = -0.05 + 0.05 * sin(1.8 * G.TIMERS.REAL);
                double rm = 0.03 * sin(1.219 * G.TIMERS.REAL);
                ds_other(shared, "dissolve", c, false, false, center, sm, rm);
            }
            if (card_name_is(c, "Invisible Joker") && disc) ds(center, "voucher", c, true);
            if (c->edition || c->seal || c->ability.eternal || c->ability.rental || c->ability.perishable || c->sticker ||
                ((c->sticker_run && strcmp(c->sticker_run, "NONE") != 0) && G.SETTINGS.run_stake_stickers) ||
                str_eq(c->ability.set, "Spectral") || c->debuff || c->greyed || card_name_is(c, "The Soul") ||
                str_eq(c->ability.set, "Voucher") || str_eq(c->ability.set, "Booster") || c->config.center->has_soul_pos || c->config.center->demo) {
                if ((str_eq(c->ability.set, "Voucher") || c->config.center->demo) && (!card_name_is(c, "Antimatter") || !disc)) ds(center, "voucher", c, true);
                if (str_eq(c->ability.set, "Booster") || str_eq(c->ability.set, "Spectral")) ds(center, "booster", c, true);
                if (c->edition && c->edition->holo) { ds(center, "holo", c, true); if (front && !stone) ds(front, "holo", c, true); }
                if (c->edition && c->edition->foil) { ds(center, "foil", c, true); if (front && !stone) ds(front, "foil", c, true); }
                if (c->edition && c->edition->polychrome) { ds(center, "polychrome", c, true); if (front && !stone) ds(front, "polychrome", c, true); }
                if (negative) ds(center, "negative_shine", c, true);
                if (c->seal) {
                    Sprite *ss = NULL;
                    if (!strcmp(c->seal, "Gold")) ss = G.shared_seals.Gold;
                    else if (!strcmp(c->seal, "Purple")) ss = G.shared_seals.Purple;
                    else if (!strcmp(c->seal, "Red")) ss = G.shared_seals.Red;
                    else if (!strcmp(c->seal, "Blue")) ss = G.shared_seals.Blue;
                    if (ss) {
                        ds_other(ss, "dissolve", c, false, false, center, 0, 0);
                        if (!strcmp(c->seal, "Gold")) ds_other(ss, "voucher", c, true, false, center, 0, 0);
                    }
                }
                if (c->ability.eternal) {
                    ds_other(G.shared_sticker_eternal, "dissolve", c, false, false, center, 0, 0);
                    ds_other(G.shared_sticker_eternal, "voucher", c, true, false, center, 0, 0);
                }
                if (c->ability.perishable) {
                    ds_other(G.shared_sticker_perishable, "dissolve", c, false, false, center, 0, 0);
                    ds_other(G.shared_sticker_perishable, "voucher", c, true, false, center, 0, 0);
                }
                if (c->ability.rental) {
                    ds_other(G.shared_sticker_rental, "dissolve", c, false, false, center, 0, 0);
                    ds_other(G.shared_sticker_rental, "voucher", c, true, false, center, 0, 0);
                }
                Sprite *st = c->sticker ? shared_sticker(c->sticker) : NULL;
                Sprite *str = c->sticker_run ? shared_sticker(c->sticker_run) : NULL;
                if (c->sticker && st) {
                    ds_other(st, "dissolve", c, false, false, center, 0, 0);
                    ds_other(st, "voucher", c, true, false, center, 0, 0);
                } else if (str && G.SETTINGS.run_stake_stickers) {
                    ds_other(str, "dissolve", c, false, false, center, 0, 0);
                    ds_other(str, "voucher", c, true, false, center, 0, 0);
                }
                if (card_name_is(c, "The Soul") && disc) {
                    double t = G.TIMERS.REAL, fr = t - floor(t);
                    double sm = 0.05 + 0.05 * sin(1.8 * t) + 0.07 * sin(fr * M_PI * 14) * pow(1 - fr, 3);
                    double rm = 0.1 * sin(1.219 * t) + 0.07 * sin(t * M_PI * 5) * pow(1 - fr, 2);
                    Sprite *soul = G.shared_soul;
                    soul->n.role.draw_major = n;
                    Sprite_draw_shader(soul, "dissolve", 0, true, NULL, 0, false, &center->n, sm, rm, 0, 0.1 + 0.03 * sin(1.8 * t), false, 0.6, NULL, 0);
                    Sprite_draw_shader_ex(soul, "dissolve", 0, false, NULL, 0, false, &center->n, sm, rm, 0, 0);
                }
                Sprite *fs = CH(c, "floating_sprite");
                if (c->config.center->has_soul_pos && disc && fs) {
                    double t = G.TIMERS.REAL, fr = t - floor(t);
                    double sm = 0.07 + 0.02 * sin(1.8 * t) + 0.00 * sin(fr * M_PI * 14) * pow(1 - fr, 3);
                    double rm = 0.05 * sin(1.219 * t) + 0.00 * sin(t * M_PI * 5) * pow(1 - fr, 2);
                    if (card_name_is(c, "Hologram")) {
                        n->hover_tilt *= 1.5;
                        Sprite_draw_shader_ex(fs, "hologram", 0, false, c->send_to_shader, 2, false, &center->n, 2 * sm, 2 * rm, 0, 0);
                        n->hover_tilt /= 1.5;
                    } else {
                        Sprite_draw_shader(fs, "dissolve", 0, true, NULL, 0, false, &center->n, sm, rm, 0, 0.1 + 0.03 * sin(1.8 * t), false, 0.6, NULL, 0);
                        Sprite_draw_shader_ex(fs, "dissolve", 0, false, NULL, 0, false, &center->n, sm, rm, 0, 0);
                    }
                }
                if (c->debuff) { ds(center, "debuff", c, true); if (front && !stone) ds(front, "debuff", c, true); }
                if (c->greyed) { ds(center, "played", c, true); if (front && !stone) ds(front, "played", c, true); }
            }
        } else if (c->sprite_facing == FACE_BACK) {
            const float *overlay = G.C.WHITE;
            if (c->area && str_eq(c->area->config.type, "deck") && c->rank > 3) {
                float v = (float)(0.5 + lua_mod(c->area->cards.count - c->rank, 7) / 50.0);
                c->back_overlay[0] = c->back_overlay[1] = c->back_overlay[2] = v;
                c->back_overlay[3] = 1;
                overlay = c->back_overlay;
            }
            if (c->area && str_eq(c->area->config.type, "deck")) {
                if (back) {
                    /* Sprite:draw(overlay) - back sprites have no draw steps */
                    Sprite_draw_self(back, overlay);
                    add_to_drawhash(&back->n);
                }
            } else ds(back, "dissolve", c, false);
            Sprite *st = c->sticker ? shared_sticker(c->sticker) : NULL;
            if (c->sticker && st) {
                ds_other(st, "dissolve", c, false, true, center, 0, 0);
                if (!strcmp(c->sticker, "Gold")) ds_other(st, "voucher", c, true, true, center, 0, 0);
            }
        }
        static const char *skip[] = { "focused_ui", "front", "back", "soul_parts", "center", "floating_sprite", "shadow", "use_button",
                                       "buy_button", "buy_and_use_button", "debuff", "price", "particles", "h_popup" };
        for (int i = 0; i < n->children.count; i++) {
            const char *k = n->children.items[i].name;
            bool sk = false;
            if (k) for (int j = 0; j < ARRLEN(skip); j++) if (!strcmp(k, skip[j])) { sk = true; break; }
            if (!sk) node_draw(n->children.items[i].node);
        }
        if (c->area == G.hand) {
            Node *fu = child_get(n, "focused_ui");
            if (fu) node_draw(fu);
        }
        add_to_drawhash(n);
        Node_draw_boundingrect(n);
    }
}

static void Card_draw(Node *n) { Card_draw_layer((Card *)n, "both"); }

static void Card_release(Node *n, Node *dragged) {
    Card *c = (Card *)n;
    if (IS(dragged, NT_CARD) && c->area) CardArea_release(c->area, dragged);
}

void Card_highlight(Card *c, bool v) {
    c->highlighted = v;
    if (c->ability.consumeable || str_eq(c->ability.set, "Joker") || (c->area && c->area == G.pack_cards)) {
        if (c->highlighted && c->area && !str_eq(c->area->config.type, "shop")) {
            double x_off = c->ability.consumeable ? -0.1 : 0;
            bool side = c->area == G.jokers || c->area == G.consumeables;
            UIBox *b = uibox_new(UIDEF_use_and_sell_buttons(c),
                                 UIBCFG(.align = side ? "cr" : "bmi", .has_offset = true,
                                        .offset = side ? (Vec2){ x_off - 0.4, 0 } : (Vec2){ 0, 0.65 }, .parent = &c->n));
            Node *old = child_get(&c->n, "use_button");
            if (old) { child_set(&c->n, "use_button", NULL); node_remove(old); }
            child_set(&c->n, "use_button", &b->n);
        } else {
            Node *ub = child_get(&c->n, "use_button");
            if (ub) { child_set(&c->n, "use_button", NULL); node_remove(ub); }
        }
    }
    if (c->ability.consumeable || str_eq(c->ability.set, "Joker")) {
        if (!c->highlighted && c->area && str_eq(c->area->config.type, "joker") &&
            (G.jokers->cards.count >= G.jokers->config.card_limit || (c->edition && c->edition->negative))) {
            if (G.shop_jokers) CardArea_unhighlight_all(G.shop_jokers);
        }
    }
}

static void Card_click(Node *n) {
    Card *c = (Card *)n;
    if (c->area && CardArea_can_highlight(c->area, c)) {
        if (c->area == G.hand && G.STATE == STATE_HAND_PLAYED) return;
        if (!c->highlighted) CardArea_add_to_highlighted(c->area, c, false);
        else {
            CardArea_remove_from_highlighted(c->area, c, false);
            sound_play("cardSlide2", NAN, 0.3);
        }
    }
    if (c->area && c->area == G.deck && c->area->cards.count > 0 && c->area->cards.items[0] == c) G_FUNCS_deck_info(NULL);
}

/* ------------------------------------------------------------------ */
/* save / load                                                         */
/* ------------------------------------------------------------------ */
static DVal extra_save(const Extra *e) {
    switch (e->kind) {
    case EX_NUM: return dv_num(e->num);
    case EX_STR: return dv_str(e->str);
    case EX_TABLE: {
        DVal t = dv_table();
#define XN(f) if (e->f != 0) dv_set_num(&t, #f, e->f)
        XN(s_mult); XN(mult); XN(size); XN(Xmult); XN(xmult); XN(every); XN(max); XN(min); XN(odds); XN(chips); XN(chip_mod);
        XN(dollars); XN(h_size); XN(h_mod); XN(hand_add); XN(discard_sub); XN(d_remaining); XN(faces); XN(increase); XN(h_plays);
        XN(discards); XN(destroy);
#undef XN
        /* zero valued fields that the original defines */
        if (e->chips == 0) dv_set_num(&t, "chips", 0);
        if (e->suit) dv_set_str(&t, "suit", e->suit);
        if (e->poker_hand) dv_set_str(&t, "poker_hand", e->poker_hand);
        if (e->remaining) dv_set_str(&t, "remaining", e->remaining);
        return t;
    }
    default: return dv_nil();
    }
}
static Extra extra_load(DVal *d) {
    Extra e;
    memset(&e, 0, sizeof e);
    if (!d) return e;
    if (d->t == DV_NUM) { e.kind = EX_NUM; e.num = d->n; return e; }
    if (d->t == DV_STR) { e.kind = EX_STR; e.str = intern(d->s); return e; }
    if (d->t != DV_TABLE) return e;
    e.kind = EX_TABLE;
#define XN(f) e.f = dv_get_num(d, #f, 0)
    XN(s_mult); XN(mult); XN(size); XN(Xmult); XN(xmult); XN(every); XN(max); XN(min); XN(odds); XN(chips); XN(chip_mod);
    XN(dollars); XN(h_size); XN(h_mod); XN(hand_add); XN(discard_sub); XN(d_remaining); XN(faces); XN(increase); XN(h_plays);
    XN(discards); XN(destroy);
#undef XN
    const char *s;
    if ((s = dv_get_str(d, "suit", NULL))) e.suit = intern(s);
    if ((s = dv_get_str(d, "poker_hand", NULL))) e.poker_hand = intern(s);
    if ((s = dv_get_str(d, "remaining", NULL))) e.remaining = intern(s);
    return e;
}

DVal Card_save(Card *c) {
    DVal t = dv_table();
    dv_set_num(&t, "sort_id", c->sort_id);
    DVal sf = dv_table();
    if (c->config.center_key) dv_set_str(&sf, "center", c->config.center_key);
    if (c->config.card_key) dv_set_str(&sf, "card", c->config.card_key);
    dv_set(&t, "save_fields", sf);
    DVal params = dv_table();
    if (c->params.playing_card) dv_set_num(&params, "playing_card", c->params.playing_card);
    if (c->params.viewed_back) dv_set_bool(&params, "viewed_back", true);
    if (c->params.bypass_discovery_center) dv_set_bool(&params, "bypass_discovery_center", true);
    if (c->params.bypass_discovery_ui) dv_set_bool(&params, "bypass_discovery_ui", true);
    if (c->params.bypass_lock) dv_set_bool(&params, "bypass_lock", true);
    if (c->params.discover) dv_set_bool(&params, "discover", true);
    if (c->params.has_bypass_back) {
        DVal bp = dv_table();
        dv_set_num(&bp, "x", c->params.bypass_back.x);
        dv_set_num(&bp, "y", c->params.bypass_back.y);
        dv_set(&params, "bypass_back", bp);
    }
    dv_set(&t, "params", params);
    if (c->no_ui) dv_set_bool(&t, "no_ui", true);
    dv_set_num(&t, "base_cost", c->base_cost);
    dv_set_num(&t, "extra_cost", c->extra_cost);
    dv_set_num(&t, "cost", c->cost);
    dv_set_num(&t, "sell_cost", c->sell_cost);
    dv_set_str(&t, "facing", c->facing == FACE_FRONT ? "front" : "back");
    dv_set_str(&t, "sprite_facing", c->facing == FACE_FRONT ? "front" : "back");
    /* the original saves self.highligted (sic), which is always nil */
    dv_set_bool(&t, "debuff", c->debuff);
    if (c->rank) dv_set_num(&t, "rank", c->rank);
    if (c->added_to_deck) dv_set_bool(&t, "added_to_deck", true);
    if (c->label) dv_set_str(&t, "label", c->label);
    if (c->playing_card) dv_set_num(&t, "playing_card", c->playing_card);
    DVal base = dv_table();
    if (c->base.name) dv_set_str(&base, "name", c->base.name);
    if (c->base.suit) dv_set_str(&base, "suit", c->base.suit);
    if (c->base.value) dv_set_str(&base, "value", c->base.value);
    dv_set_num(&base, "nominal", c->base.nominal);
    dv_set_num(&base, "suit_nominal", c->base.suit_nominal);
    dv_set_num(&base, "face_nominal", c->base.face_nominal);
    dv_set_num(&base, "times_played", c->base.times_played);
    if (c->base.id) dv_set_num(&base, "id", c->base.id);
    if (c->base.original_value) dv_set_str(&base, "original_value", c->base.original_value);
    if (c->base.has_suit_nominal_original) dv_set_num(&base, "suit_nominal_original", c->base.suit_nominal_original);
    dv_set(&t, "base", base);
    Ability *a = &c->ability;
    DVal ab = dv_table();
    if (a->name) dv_set_str(&ab, "name", a->name);
    if (a->effect) dv_set_str(&ab, "effect", a->effect);
    if (a->set) dv_set_str(&ab, "set", a->set);
    dv_set_num(&ab, "mult", a->mult);
    dv_set_num(&ab, "h_mult", a->h_mult);
    dv_set_num(&ab, "h_x_mult", a->h_x_mult);
    dv_set_num(&ab, "h_dollars", a->h_dollars);
    dv_set_num(&ab, "p_dollars", a->p_dollars);
    dv_set_num(&ab, "t_mult", a->t_mult);
    dv_set_num(&ab, "t_chips", a->t_chips);
    dv_set_num(&ab, "x_mult", a->x_mult);
    dv_set_num(&ab, "h_size", a->h_size);
    dv_set_num(&ab, "d_size", a->d_size);
    if (a->extra.kind) dv_set(&ab, "extra", extra_save(&a->extra));
    dv_set_num(&ab, "extra_value", a->extra_value);
    dv_set_str(&ab, "type", a->type ? a->type : "");
    if (a->has_order) dv_set_num(&ab, "order", a->order);
    if (a->forced_selection) dv_set_bool(&ab, "forced_selection", true);
    dv_set_num(&ab, "perma_bonus", a->perma_bonus);
    dv_set_num(&ab, "bonus", a->bonus);
    if (a->consumeable) dv_set(&ab, "consumeable", dv_str("MANUAL_REPLACE_CONFIG"));
    if (a->has_invis_rounds) dv_set_num(&ab, "invis_rounds", a->invis_rounds);
    if (a->has_caino_xmult) dv_set_num(&ab, "caino_xmult", a->caino_xmult);
    if (a->has_yorick_discards) dv_set_num(&ab, "yorick_discards", a->yorick_discards);
    if (a->has_loyalty_remaining) { dv_set_num(&ab, "loyalty_remaining", a->loyalty_remaining); dv_set_num(&ab, "burnt_hand", a->burnt_hand); }
    dv_set_num(&ab, "hands_played_at_create", a->hands_played_at_create);
    if (a->to_do_poker_hand) dv_set_str(&ab, "to_do_poker_hand", a->to_do_poker_hand);
    if (a->eternal) dv_set_bool(&ab, "eternal", true);
    if (a->perishable) { dv_set_bool(&ab, "perishable", true); dv_set_num(&ab, "perish_tally", a->perish_tally); }
    if (a->rental) dv_set_bool(&ab, "rental", true);
    if (a->couponed) dv_set_bool(&ab, "couponed", true);
    if (a->discarded) dv_set_bool(&ab, "discarded", true);
    if (a->played_this_ante) dv_set_bool(&ab, "played_this_ante", true);
    if (a->perma_debuff) dv_set_bool(&ab, "perma_debuff", true);
    if (a->wheel_flipped) dv_set_bool(&ab, "wheel_flipped", true);
    if (a->queue_negative_removal) dv_set_bool(&ab, "queue_negative_removal", true);
    if (a->money != 0) dv_set_num(&ab, "money", a->money);
    if (a->has_driver_tally) dv_set_num(&ab, "driver_tally", a->driver_tally);
    if (a->has_steel_tally) dv_set_num(&ab, "steel_tally", a->steel_tally);
    if (a->has_nine_tally) dv_set_num(&ab, "nine_tally", a->nine_tally);
    if (a->has_stone_tally) dv_set_num(&ab, "stone_tally", a->stone_tally);
    if (a->blueprint_compat) dv_set_str(&ab, "blueprint_compat", a->blueprint_compat);
    if (a->blueprint_compat_ui) dv_set_str(&ab, "blueprint_compat_ui", a->blueprint_compat_ui);
    if (a->booster_pos) dv_set_num(&ab, "booster_pos", a->booster_pos);
    dv_set(&t, "ability", ab);
    if (c->pinned) dv_set_bool(&t, "pinned", true);
    if (c->edition) {
        DVal ed = dv_table();
        if (c->edition->holo) dv_set_bool(&ed, "holo", true);
        if (c->edition->foil) dv_set_bool(&ed, "foil", true);
        if (c->edition->polychrome) dv_set_bool(&ed, "polychrome", true);
        if (c->edition->negative) dv_set_bool(&ed, "negative", true);
        if (c->edition->has_mult) dv_set_num(&ed, "mult", c->edition->mult);
        if (c->edition->has_chips) dv_set_num(&ed, "chips", c->edition->chips);
        if (c->edition->has_x_mult) dv_set_num(&ed, "x_mult", c->edition->x_mult);
        if (c->edition->type) dv_set_str(&ed, "type", c->edition->type);
        dv_set(&t, "edition", ed);
    }
    if (c->seal) dv_set_str(&t, "seal", c->seal);
    if (c->bypass_discovery_center) dv_set_bool(&t, "bypass_discovery_center", true);
    if (c->bypass_discovery_ui) dv_set_bool(&t, "bypass_discovery_ui", true);
    if (c->bypass_lock) dv_set_bool(&t, "bypass_lock", true);
    return t;
}

static const char *istr(DVal *d, const char *k) { const char *s = dv_get_str(d, k, NULL); return s ? intern(s) : NULL; }

void Card_load(Card *c, DVal *t) {
    double scale = 1;
    DVal *sf = dv_get(t, "save_fields");
    c->config.center_key = istr(sf, "center");
    c->config.center = P_CENTER(c->config.center_key);
    if (!c->config.center) { c->config.center = P_CENTER("c_base"); c->config.center_key = "c_base"; }
    DVal *params = dv_get(t, "params");
    memset(&c->params, 0, sizeof c->params);
    c->params.playing_card = (int)dv_get_num(params, "playing_card", 0);
    c->params.viewed_back = dv_get_bool(params, "viewed_back", false);
    c->params.bypass_discovery_center = dv_get_bool(params, "bypass_discovery_center", false);
    c->params.bypass_discovery_ui = dv_get_bool(params, "bypass_discovery_ui", false);
    c->params.bypass_lock = dv_get_bool(params, "bypass_lock", false);
    c->params.discover = dv_get_bool(params, "discover", false);
    DVal *bb = dv_get(params, "bypass_back");
    if (bb) { c->params.has_bypass_back = true; c->params.bypass_back = SPOS(dv_get_num(bb, "x", 0), dv_get_num(bb, "y", 0)); }
    c->sticker_run = NULL;
    double H = G.CARD_H, W = G.CARD_W;
    const char *nm = c->config.center->name;
    if (str_eq(nm, "Half Joker")) { c->n.T->h = H * scale / 1.7 * scale; c->n.T->w = W * scale; }
    else if (str_eq(nm, "Wee Joker")) { c->n.T->h = H * scale * 0.7 * scale; c->n.T->w = W * scale * 0.7 * scale; }
    else if (str_eq(nm, "Photograph")) { c->n.T->h = H * scale / 1.2 * scale; c->n.T->w = W * scale; }
    else if (str_eq(nm, "Square Joker")) { H = W; c->n.T->h = H * scale; c->n.T->w = W * scale; }
    else if (str_eq(c->config.center->set, "Booster")) { c->n.T->h = H * 1.27; c->n.T->w = W * 1.27; }
    else { c->n.T->h = H * scale; c->n.T->w = W * scale; }
    c->n.VT.h = c->n.T->h;
    c->n.VT.w = c->n.T->w;
    c->config.card_key = istr(sf, "card");
    c->config.card = c->config.card_key ? P_CARD(c->config.card_key) : &G.P_CARD_EMPTY;
    if (!c->config.card) c->config.card = &G.P_CARD_EMPTY;
    c->no_ui = dv_get_bool(t, "no_ui", false);
    c->base_cost = dv_get_num(t, "base_cost", 0);
    c->extra_cost = dv_get_num(t, "extra_cost", 0);
    c->cost = dv_get_num(t, "cost", 0);
    c->sell_cost = dv_get_num(t, "sell_cost", 0);
    c->facing = str_eq(dv_get_str(t, "facing", "front"), "back") ? FACE_BACK : FACE_FRONT;
    c->sprite_facing = str_eq(dv_get_str(t, "sprite_facing", "front"), "back") ? FACE_BACK : FACE_FRONT;
    c->flipping = FLIP_NONE;
    c->highlighted = dv_get_bool(t, "highlighted", false);
    c->debuff = dv_get_bool(t, "debuff", false);
    c->rank = (int)dv_get_num(t, "rank", 0);
    c->added_to_deck = dv_get_bool(t, "added_to_deck", false);
    c->label = istr(t, "label");
    c->playing_card = (int)dv_get_num(t, "playing_card", 0);
    DVal *base = dv_get(t, "base");
    memset(&c->base, 0, sizeof c->base);
    c->base.name = istr(base, "name");
    c->base.suit = istr(base, "suit");
    c->base.value = istr(base, "value");
    c->base.nominal = dv_get_num(base, "nominal", 0);
    c->base.suit_nominal = dv_get_num(base, "suit_nominal", 0);
    c->base.face_nominal = dv_get_num(base, "face_nominal", 0);
    c->base.colour = suit_colour(c->base.suit);
    c->base.times_played = dv_get_num(base, "times_played", 0);
    c->base.id = (int)dv_get_num(base, "id", 0);
    c->base.original_value = istr(base, "original_value");
    if (dv_get(base, "suit_nominal_original")) { c->base.has_suit_nominal_original = true; c->base.suit_nominal_original = dv_get_num(base, "suit_nominal_original", 0); }
    c->sort_id = (int)dv_get_num(t, "sort_id", c->sort_id);
    c->bypass_discovery_center = dv_get_bool(t, "bypass_discovery_center", false);
    c->bypass_discovery_ui = dv_get_bool(t, "bypass_discovery_ui", false);
    c->bypass_lock = dv_get_bool(t, "bypass_lock", false);
    DVal *ab = dv_get(t, "ability");
    Ability *a = &c->ability;
    memset(a, 0, sizeof *a);
    a->name = istr(ab, "name");
    a->effect = istr(ab, "effect");
    a->set = istr(ab, "set");
    a->mult = dv_get_num(ab, "mult", 0);
    a->h_mult = dv_get_num(ab, "h_mult", 0);
    a->h_x_mult = dv_get_num(ab, "h_x_mult", 0);
    a->h_dollars = dv_get_num(ab, "h_dollars", 0);
    a->p_dollars = dv_get_num(ab, "p_dollars", 0);
    a->t_mult = dv_get_num(ab, "t_mult", 0);
    a->t_chips = dv_get_num(ab, "t_chips", 0);
    a->x_mult = dv_get_num(ab, "x_mult", 1);
    a->h_size = dv_get_num(ab, "h_size", 0);
    a->d_size = dv_get_num(ab, "d_size", 0);
    a->extra = extra_load(dv_get(ab, "extra"));
    a->extra_value = dv_get_num(ab, "extra_value", 0);
    a->type = istr(ab, "type");
    if (!a->type) a->type = "";
    if (dv_get(ab, "order")) { a->has_order = true; a->order = dv_get_num(ab, "order", 0); }
    a->forced_selection = dv_get_bool(ab, "forced_selection", false);
    a->perma_bonus = dv_get_num(ab, "perma_bonus", 0);
    a->bonus = dv_get_num(ab, "bonus", 0);
    if (c->config.center->consumeable) a->consumeable = &c->config.center->config;
#define OPT(f) if (dv_get(ab, #f)) { a->f = dv_get_num(ab, #f, 0); a->has_##f = true; }
    OPT(invis_rounds); OPT(caino_xmult); OPT(yorick_discards); OPT(loyalty_remaining);
    OPT(driver_tally); OPT(steel_tally); OPT(nine_tally); OPT(stone_tally);
#undef OPT
    a->burnt_hand = dv_get_num(ab, "burnt_hand", 0);
    a->hands_played_at_create = dv_get_num(ab, "hands_played_at_create", 0);
    a->to_do_poker_hand = istr(ab, "to_do_poker_hand");
    a->eternal = dv_get_bool(ab, "eternal", false);
    a->perishable = dv_get_bool(ab, "perishable", false);
    a->perish_tally = dv_get_num(ab, "perish_tally", 0);
    a->rental = dv_get_bool(ab, "rental", false);
    a->couponed = dv_get_bool(ab, "couponed", false);
    a->discarded = dv_get_bool(ab, "discarded", false);
    a->played_this_ante = dv_get_bool(ab, "played_this_ante", false);
    a->perma_debuff = dv_get_bool(ab, "perma_debuff", false);
    a->wheel_flipped = dv_get_bool(ab, "wheel_flipped", false);
    a->queue_negative_removal = dv_get_bool(ab, "queue_negative_removal", false);
    a->money = dv_get_num(ab, "money", 0);
    a->blueprint_compat = istr(ab, "blueprint_compat");
    const char *bui = dv_get_str(ab, "blueprint_compat_ui", NULL);
    if (bui) a->blueprint_compat_ui = xstrdup(bui);
    a->booster_pos = (int)dv_get_num(ab, "booster_pos", 0);
    c->pinned = dv_get_bool(t, "pinned", false);
    DVal *ed = dv_get(t, "edition");
    c->edition = NULL;
    if (ed && ed->t == DV_TABLE) {
        c->edition = NEW(Edition);
        c->edition->holo = dv_get_bool(ed, "holo", false);
        c->edition->foil = dv_get_bool(ed, "foil", false);
        c->edition->polychrome = dv_get_bool(ed, "polychrome", false);
        c->edition->negative = dv_get_bool(ed, "negative", false);
        if (dv_get(ed, "mult")) { c->edition->has_mult = true; c->edition->mult = dv_get_num(ed, "mult", 0); }
        if (dv_get(ed, "chips")) { c->edition->has_chips = true; c->edition->chips = dv_get_num(ed, "chips", 0); }
        if (dv_get(ed, "x_mult")) { c->edition->has_x_mult = true; c->edition->x_mult = dv_get_num(ed, "x_mult", 0); }
        c->edition->type = istr(ed, "type");
    }
    c->seal = istr(t, "seal");
    remove_all_children(&c->n);
    child_set(&c->n, "shadow", moveable_new(0, 0, 0, 0));
    Card_set_sprites(c, c->config.center, c->config.card->empty ? NULL : c->config.card);
}

/* ------------------------------------------------------------------ */
/* removal                                                             */
/* ------------------------------------------------------------------ */
static void Card_remove(Node *n) {
    Card *c = (Card *)n;
    c->removed = true;
    if (c->area) CardArea_remove_card(c->area, c, false);
    Card_remove_from_deck(c, false);
    if (c->ability.queue_negative_removal) {
        if (c->ability.consumeable) G.consumeables->config.card_limit -= 1;
        else G.jokers->config.card_limit -= 1;
    }
    if (!G.OVERLAY_MENU) {
        for (int i = 0; i < G.n_centers; i++) {
            Center *v = &G.P_CENTERS_LIST[i];
            if (str_eq(v->name, c->ability.name)) {
                CardList l = { 0 };
                find_joker(c->ability.name, true, &l);
                if (l.count == 0) strset_del(&G.GAME.used_jokers, v->key);
                vec_free(&l);
            }
        }
    }
    if (ptrvec_remove(&G.playing_cards, c)) {
        for (int i = 0; i < G.playing_cards.count; i++) ((Card *)G.playing_cards.items[i])->playing_card = i + 1;
    }
    remove_all_children(n);
    ptrvec_remove_all(&G.I.CARD, n);
    Moveable_remove(n);
}

static void Card_animate(Node *n) { (void)n; }
static void Card_hard_set_T_v(Node *n, double x, double y, double w, double h) { Card_hard_set_T(n, x, y, w, h); }

const NodeVT CARD_VT = {
    .type = NT_CARD, .super = &MOVEABLE_VT, .name = "Card",
    .draw = Card_draw,
    .update = Card_update,
    .move = Card_move,
    .remove = Card_remove,
    .hover = Card_hover,
    .stop_hover = Card_stop_hover,
    .click = Card_click,
    .release = Card_release,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = Card_animate,
    .juice_up = Card_juice_up,
    .hard_set_T = Card_hard_set_T_v,
};

/* copy_card: `for k, v in pairs(other.ability) do new.ability[k] = copy(v) end` */
void Card_copy_ability(Card *dst, const Card *src) {
    dst->ability = src->ability;
    if (src->ability.blueprint_compat_ui) dst->ability.blueprint_compat_ui = xstrdup(src->ability.blueprint_compat_ui);
}
