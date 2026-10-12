/* cardarea.c - port of cardarea.lua */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define EVCTX(T) T *c = (T *)p_; (void)e_

static bool type_is(CardArea *a, const char *t) { return str_eq(a->config.type, t); }

CardArea *cardarea_new(double x, double y, double w, double h, CardAreaArgs cfg) {
    CardArea *a = NEW(CardArea);
    moveable_init(&a->n, &CARDAREA_VT, x, y, w, h);
    a->n.states.drag.can = false;
    a->n.states.hover.can = false;
    a->n.states.click.can = false;
    a->card_w = cfg.card_w != 0 ? cfg.card_w : G.CARD_W;
    a->config.card_w = cfg.card_w;
    a->config.highlighted_limit = cfg.has_highlight_limit ? cfg.highlight_limit : 5;
    a->config.card_limit = cfg.has_card_limit ? cfg.card_limit : 52;
    a->config.temp_limit = a->config.card_limit;
    a->config.card_count = 0;
    a->config.type = cfg.type ? intern(cfg.type) : "deck";
    a->config.sort = cfg.sort ? intern(cfg.sort) : "desc";
    a->config.lr_padding = cfg.has_lr_padding ? cfg.lr_padding : 0.1;
    a->config.collection = cfg.collection;
    a->config.view_deck = cfg.view_deck;
    a->config.spread = cfg.spread;
    a->config.deck_height = cfg.deck_height;
    a->config.thin_draw = cfg.thin_draw;
    a->config.no_card_count = cfg.no_card_count;
    if (cfg.draw_layer1) {
        a->config.draw_layers[0] = cfg.draw_layer1;
        a->config.draw_layers[1] = cfg.draw_layer2;
        a->config.n_draw_layers = cfg.draw_layer2 ? 2 : 1;
    }
    a->shuffle_amt = 0;
    vec_push(&G.I.CARDAREA, &a->n);
    return a;
}

void CardArea_emplace(CardArea *a, Card *card, const char *location, bool stay_flipped) {
    if ((location && !strcmp(location, "front")) || type_is(a, "deck")) vec_insert(&a->cards, 0, card);
    else vec_push(&a->cards, card);
    if (card->facing == FACE_BACK && !type_is(a, "discard") && !type_is(a, "deck") && !stay_flipped) Card_flip(card);
    if (a == G.hand && stay_flipped) card->ability.wheel_flipped = true;
    if (a->cards.count > a->config.card_limit) {
        if (a == G.deck) a->config.card_limit = a->cards.count;
    }
    Card_set_card_area(card, a);
    CardArea_set_ranks(a);
    CardArea_align_cards(a);
    if (a == G.jokers) {
        int tally = 0;
        for (int i = 0; i < G.jokers->cards.count; i++) if (str_eq(G.jokers->cards.items[i]->ability.set, "Joker")) tally++;
        if (tally > G.GAME.max_jokers) G.GAME.max_jokers = tally;
        UNLOCK(.type = "modify_jokers");
    }
    if (a == G.deck) UNLOCK(.type = "modify_deck");
}

Card *CardArea_remove_card(CardArea *a, Card *card, bool discarded_only) {
    if (a->n.REMOVED && a->cards.count == 0) return NULL;
    CardList tmp = { 0 };
    CardList *cards = &a->cards;
    if (discarded_only) {
        for (int i = 0; i < a->cards.count; i++) if (a->cards.items[i]->ability.discarded) vec_push(&tmp, a->cards.items[i]);
        cards = &tmp;
    }
    if (!card) {
        if (type_is(a, "discard") || type_is(a, "deck")) card = cards->count ? cards->items[cards->count - 1] : NULL;
        else card = cards->count ? cards->items[0] : NULL;
    }
    vec_free(&tmp);
    for (int i = a->cards.count - 1; i >= 0; i--) {
        if (a->cards.items[i] == card) {
            Card_remove_from_area(card);
            vec_remove_at(&a->cards, i);
            CardArea_remove_from_highlighted(a, card, true);
            break;
        }
    }
    CardArea_set_ranks(a);
    if (a == G.deck) UNLOCK(.type = "modify_deck");
    return card;
}

typedef struct { CardArea *a; double delta; } SizeCtx;
EVFN(ev_sort_area) {
    EVCTX(SizeCtx);
    CardArea_sort(c->a, NULL);
    return true;
}
EVFN(ev_change_size) {
    EVCTX(SizeCtx);
    CardArea *a = c->a;
    if (!a->config.has_real_card_limit) { a->config.real_card_limit = a->config.card_limit; a->config.has_real_card_limit = true; }
    a->config.real_card_limit += c->delta;
    a->config.card_limit = MAX(0, a->config.real_card_limit);
    if (c->delta > 0 && a->config.real_card_limit > 1 && a == G.hand && a->cards.count > 0 && (G.STATE == STATE_DRAW_TO_HAND || G.STATE == STATE_SELECTING_HAND)) {
        int cnt = (int)fabs(c->delta);
        for (int i = 1; i <= cnt; i++) {
            draw_card(G.deck, G.hand, i * 100.0 / cnt, NULL, false, NULL, 0.07, false, false, NAN, false);
            SizeCtx *s = CTX(SizeCtx); s->a = a;
            add_event(EVENT(.func = ev_sort_area, .ctx = s));
        }
    }
    if (a == G.hand) UNLOCK(.type = "min_hand_size");
    return true;
}

void CardArea_change_size(CardArea *a, double delta) {
    if (delta == 0) return;
    SizeCtx *s = CTX(SizeCtx);
    s->a = a; s->delta = delta;
    add_event(EVENT(.func = ev_change_size, .ctx = s));
}

bool CardArea_can_highlight(CardArea *a, Card *card) {
    (void)card;
    if (G.CONTROLLER.HID.controller) return type_is(a, "hand");
    return type_is(a, "hand") || type_is(a, "joker") || type_is(a, "consumeable") || (type_is(a, "shop") && a->config.highlighted_limit > 0);
}

void CardArea_add_to_highlighted(CardArea *a, Card *card, bool silent) {
    if (type_is(a, "shop")) {
        if (a->highlighted.count > 0) CardArea_remove_from_highlighted(a, a->highlighted.items[0], false);
        vec_push(&a->highlighted, card);
        Card_highlight(card, true);
        if (!silent) sound_play("cardSlide1", NAN, NAN);
    } else if (type_is(a, "joker") || type_is(a, "consumeable")) {
        if (a->highlighted.count >= a->config.highlighted_limit) CardArea_remove_from_highlighted(a, a->highlighted.items[0], false);
        vec_push(&a->highlighted, card);
        Card_highlight(card, true);
        if (!silent) sound_play("cardSlide1", NAN, NAN);
    } else {
        if (a->highlighted.count >= a->config.highlighted_limit) Card_highlight(card, false);
        else {
            vec_push(&a->highlighted, card);
            Card_highlight(card, true);
            if (!silent) sound_play("cardSlide1", NAN, NAN);
        }
        if (a == G.hand && G.STATE == STATE_SELECTING_HAND) CardArea_parse_highlighted(a);
    }
}

void CardArea_parse_highlighted(CardArea *a) {
    G.boss_throw_hand = false;
    const char *disp_text = NULL;
    PokerHands *ph = NULL;
    const char *text = G_FUNCS_get_poker_hand_info(&a->highlighted, &disp_text, &ph, NULL, NULL);
    if (!strcmp(text, "NULL")) {
        update_hand_text(HTC(.immediate = true, .nopulse = true, .delay = 0, .has_delay = true),
                         HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .level = "", .has_level = true, .handname = "", .has_handname = true));
    } else {
        if (G.GAME.blind && Blind_debuff_hand(G.GAME.blind, &a->highlighted, ph, text, true)) G.boss_throw_hand = true;
        bool backwards = false;
        for (int i = 0; i < a->highlighted.count; i++) if (a->highlighted.items[i]->facing == FACE_BACK) backwards = true;
        if (backwards) {
            update_hand_text(HTC(.immediate = true, .delay = 0, .has_delay = true),
                             HTV(.handname = "????", .has_handname = true, .level = "?", .has_level = true, .mult_s = "?", .has_mult = true, .chips_s = "?", .has_chips = true));
        } else {
            HandStat *h = &G.GAME.hands[hand_index(text)];
            update_hand_text(HTC(.immediate = true, .delay = 0, .has_delay = true),
                             HTV(.handname = disp_text, .has_handname = true, .level_n = h->level, .level_is_num = true, .has_level = true,
                                 .mult = h->mult, .has_mult = true, .chips = h->chips, .has_chips = true));
        }
    }
}

void CardArea_remove_from_highlighted(CardArea *a, Card *card, bool force) {
    if (!force && card && card->ability.forced_selection && a == G.hand) return;
    for (int i = a->highlighted.count - 1; i >= 0; i--) {
        if (a->highlighted.items[i] == card) { vec_remove_at(&a->highlighted, i); break; }
    }
    if (card) Card_highlight(card, false);
    if (a == G.hand && G.STATE == STATE_SELECTING_HAND) CardArea_parse_highlighted(a);
}

void CardArea_unhighlight_all(CardArea *a) {
    for (int i = a->highlighted.count - 1; i >= 0; i--) {
        Card *c = a->highlighted.items[i];
        if (c->ability.forced_selection && a == G.hand) {
        } else {
            Card_highlight(c, false);
            vec_remove_at(&a->highlighted, i);
        }
    }
    if (a == G.hand && G.STATE == STATE_SELECTING_HAND) CardArea_parse_highlighted(a);
}

void CardArea_set_ranks(CardArea *a) {
    for (int k = 1; k <= a->cards.count; k++) {
        Card *card = a->cards.items[k - 1];
        card->rank = k;
        card->n.states.collide.can = true;
        if (k > 1 && type_is(a, "deck")) {
            card->n.states.drag.can = false;
            card->n.states.collide.can = false;
        } else if (type_is(a, "play") || type_is(a, "shop") || type_is(a, "consumeable")) {
            card->n.states.drag.can = false;
        } else card->n.states.drag.can = true;
    }
}

static void CardArea_move(Node *n, double dt) {
    CardArea *a = (CardArea *)n;
    if (a == G.hand) {
        double desired_y = G.TILE_H - G.hand->n.T->h - 1.9 * ((!G.deck_preview && (G.STATE == STATE_SELECTING_HAND || G.STATE == STATE_DRAW_TO_HAND)) ? 1 : 0);
        G.hand->n.T->y = 15 * G.real_dt * desired_y + (1 - 15 * G.real_dt) * G.hand->n.T->y;
        if (fabs(desired_y - G.hand->n.T->y) < 0.01) G.hand->n.T->y = desired_y;
        if (G.STATE == STATE_TUTORIAL) G.play->n.T->y = G.hand->n.T->y - (3 + 0.6);
    }
    Moveable_move(n, dt);
    CardArea_align_cards(a);
}

static void CardArea_update(Node *n, double dt) {
    (void)dt;
    CardArea *a = (CardArea *)n;
    if (a == G.hand) {
        if (G.GAME.modifiers.minus_hand_size_per_X_dollar != 0) {
            double div = G.GAME.modifiers.minus_hand_size_per_X_dollar;
            if (!a->config.has_last_poll_size) { a->config.last_poll_size = 0; a->config.has_last_poll_size = true; }
            if (floor(G.GAME.dollars / div) != a->config.last_poll_size) {
                CardArea_change_size(a, a->config.last_poll_size - floor(G.GAME.dollars / div));
                a->config.last_poll_size = floor(G.GAME.dollars / div);
            }
        }
        for (int i = 0; i < a->cards.count; i++) {
            Card *v = a->cards.items[i];
            if (v->ability.forced_selection && a->highlighted.count == 0) CardArea_add_to_highlighted(a, v, false);
        }
    }
    if (a == G.deck) {
        n->states.collide.can = true;
        n->states.hover.can = true;
        n->states.click.can = true;
    }
    if (G.CONTROLLER.HID.controller && a != G.hand) CardArea_unhighlight_all(a);
    if (a == G.deck && a->config.card_limit > G.playing_cards.count) a->config.card_limit = G.playing_cards.count;
    a->config.temp_limit = MAX(a->cards.count, a->config.card_limit);
    a->config.card_count = a->cards.count;
}

static bool is_invisible_type(CardArea *a) {
    static const char *inv[] = { "discard", "voucher", "play", "consumeable", "title", "title_2" };
    for (int i = 0; i < ARRLEN(inv); i++) if (type_is(a, inv[i])) return true;
    return false;
}

static void CardArea_draw(Node *n) {
    CardArea *a = (CardArea *)n;
    if (!n->states.visible) return;
    if (G.VIEWING_DECK && (a == G.deck || a == G.hand || a == G.play)) return;
    int state = G.TAROT_INTERRUPT ? G.TAROT_INTERRUPT : G.STATE;
    bool hidden_hand = type_is(a, "hand") && (state == STATE_SHOP || state == STATE_TAROT_PACK || state == STATE_SPECTRAL_PACK || state == STATE_STANDARD_PACK ||
                                              state == STATE_BUFFOON_PACK || state == STATE_PLANET_PACK || state == STATE_ROUND_EVAL || state == STATE_BLIND_SELECT);
    if (is_invisible_type(a) || hidden_hand || (type_is(a, "deck") && a != G.deck) || (type_is(a, "shop") && a != G.shop_vouchers)) {
    } else {
        if (!child_get(n, "area_uibox")) {
            UIDef *card_count = NULL;
            if (a != G.shop_vouchers) {
                card_count = UIR(CFG(.align = a == G.jokers ? "cl" : a == G.hand ? "cm" : "cr", .padding = 0.03, .no_fill = true),
                    UIB(CFG(.w = 0.1, .h = 0.1)),
                    UIT(CFG(.ref = REFN(&a->config.card_count), .scale = 0.3, .colour = G.C.WHITE)),
                    UIT(CFG(.text = "/", .scale = 0.3, .colour = G.C.WHITE)),
                    UIT(CFG(.ref = REFN(&a->config.card_limit), .scale = 0.3, .colour = G.C.WHITE)),
                    UIB(CFG(.w = 0.1, .h = 0.1)));
            }
            UIDef *inner = UIR(CFG(.minw = n->T->w, .minh = n->T->h, .align = "cm", .padding = 0.1, .mid = true, .r = 0.1,
                                   .colour = a != G.shop_vouchers ? col_new(0, 0, 0, 0.1f) : NULL, .ref_table = a));
            if (a == G.shop_vouchers) {
                uidef_add(inner, UIC(CFG(.align = "cm", .func = G_FUNCS_shop_voucher_empty),
                    UIR(CFG(.align = "cm"), UIT(CFG(.text = "DEFEAT", .scale = 0.6, .colour = G.C.WHITE))),
                    UIR(CFG(.align = "cm"), UIT(CFG(.text = "BOSS BLIND", .scale = 0.4, .colour = G.C.WHITE))),
                    UIR(CFG(.align = "cm"), UIT(CFG(.text = "TO RESTOCK", .scale = 0.4, .colour = G.C.WHITE)))));
            }
            UIDef *root = UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR), inner);
            if (card_count) uidef_add(root, card_count);
            UIBox *b = uibox_new(root, UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, 0 }, .major = n, .parent = n));
            child_set(n, "area_uibox", &b->n);
        }
        node_draw(child_get(n, "area_uibox"));
    }
    Node_draw_boundingrect(n);
    add_to_drawhash(n);
    const char *layers[2] = { "shadow", "card" };
    int nl = 2;
    if (a->config.n_draw_layers > 0) {
        layers[0] = a->config.draw_layers[0];
        layers[1] = a->config.draw_layers[1];
        nl = a->config.n_draw_layers;
    }
    Node *focused = G.CONTROLLER.focused.target, *dragging = G.CONTROLLER.dragging.target;
    for (int li = 0; li < nl; li++) {
        const char *v = layers[li];
        int nc = a->cards.count;
        if (type_is(a, "deck")) {
            int thin = a->config.thin_draw ? a->config.thin_draw : 9;
            for (int i = nc; i >= 1; i--) {
                if (i > a->cards.count) continue;
                Card *c = a->cards.items[i - 1];
                if (&c->n != focused) {
                    if (i == 1 || i % thin == 0 || i == nc || fabs(c->n.VT.x - n->T->x) > 1 || fabs(c->n.VT.y - n->T->y) > 1) {
                        if (dragging != &c->n) Card_draw_layer(c, v);
                    }
                }
            }
        }
        if (type_is(a, "joker") || type_is(a, "consumeable") || type_is(a, "shop") || type_is(a, "title_2")) {
            for (int i = 0; i < a->cards.count; i++) {
                Card *c = a->cards.items[i];
                if (&c->n != focused && !c->highlighted && dragging != &c->n) Card_draw_layer(c, v);
            }
            for (int i = 0; i < a->cards.count; i++) {
                Card *c = a->cards.items[i];
                if (&c->n != focused && c->highlighted && dragging != &c->n) Card_draw_layer(c, v);
            }
        }
        if (type_is(a, "discard")) {
            for (int i = 0; i < a->cards.count; i++) {
                Card *c = a->cards.items[i];
                if (&c->n != focused && fabs(c->n.VT.x - n->T->x) > 1 && dragging != &c->n) Card_draw_layer(c, v);
            }
        }
        if (type_is(a, "hand") || type_is(a, "play") || type_is(a, "title") || type_is(a, "voucher")) {
            for (int i = 0; i < a->cards.count; i++) {
                Card *c = a->cards.items[i];
                if ((&c->n != focused || a == G.hand) && dragging != &c->n) Card_draw_layer(c, v);
            }
        }
    }
    if (a == G.deck) {
        Node *peek = child_get(n, "peek_deck");
        /* 3DS: the ZL "peek deck" hint is shown whether or not buttons are in use */
        if (G.STATE == STATE_SELECTING_HAND && !peek) {
            UIBox *b = uibox_new(
                UIROOT(CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.CLEAR),
                    UIR(CFG(.align = "cm", .r = 0.1, .colour = adjust_alpha(G.C.L_BLACK, 0.5), .func = G_FUNCS_set_button_pip,
                            .focus_args = FOCUS(.button = "triggerleft", .orientation = "bm", .scale = 0.6, .type = "none")),
                        UIR(CFG(.align = "cm"), UIT(CFG(.text = "PEEK", .scale = 0.48, .colour = G.C.WHITE, .shadow = true))),
                        UIR(CFG(.align = "cm"), UIT(CFG(.text = "DECK", .scale = 0.38, .colour = G.C.WHITE, .shadow = true))))),
                UIBCFG(.align = "cl", .has_offset = true, .offset = { -0.5, 0.1 }, .major = n, .parent = n));
            b->n.states.collide.can = false;
            child_set(n, "peek_deck", &b->n);
        } else if (G.STATE != STATE_SELECTING_HAND && peek) {
            child_set(n, "peek_deck", NULL);
            node_remove(peek);
        }
        Node *vd = child_get(n, "view_deck");
        if (!vd) {
            UIBox *b = uibox_new(
                UIROOT(CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.CLEAR),
                    UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = adjust_alpha(G.C.BLACK, 0.5), .func = G_FUNCS_set_button_pip,
                            .focus_args = FOCUS(.button = "triggerright", .orientation = "bm", .scale = 0.6), .button = G_FUNCS_deck_info),
                        UIR(CFG(.align = "cm", .maxw = 2), UIT(CFG(.text = (char *)localize_key("k_view"), .scale = 0.48, .colour = G.C.WHITE, .shadow = true))),
                        UIR(CFG(.align = "cm", .maxw = 2), UIT(CFG(.text = (char *)localize_key("k_deck"), .scale = 0.38, .colour = G.C.WHITE, .shadow = true))))),
                UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, 0 }, .major = a->cards.count ? &a->cards.items[0]->n : n, .parent = n));
            b->n.states.collide.can = false;
            child_set(n, "view_deck", &b->n);
            vd = &b->n;
        }
        if (G.deck_preview || n->states.collide.is || (G.buttons && G.buttons->n.states.collide.is && G.CONTROLLER.HID.controller)) node_draw(vd);
        Node *pk = child_get(n, "peek_deck");
        if (pk) node_draw(pk);
    }
}

typedef struct { Card *c; double key; } SortEnt;
static int sort_ent_cmp(const void *x, const void *y) {
    const SortEnt *a = x, *b = y;
    return a->key < b->key ? -1 : a->key > b->key ? 1 : 0;
}
/* stable sort by key ascending (Lua's table.sort is not stable but ties
 * between distinct cards practically never happen for positions) */
static void sort_cards_by(CardList *cards, double (*key)(Card *)) {
    int n = cards->count;
    if (n < 2) return;
    SortEnt *e = xmalloc((size_t)n * sizeof(SortEnt));
    for (int i = 0; i < n; i++) { e[i].c = cards->items[i]; e[i].key = key(cards->items[i]); }
    qsort(e, (size_t)n, sizeof(SortEnt), sort_ent_cmp);
    for (int i = 0; i < n; i++) cards->items[i] = e[i].c;
    free(e);
}
static double key_x(Card *c) { return c->n.T->x + c->n.T->w / 2; }
static double key_order(Card *c) { return c->ability.order; }
static double key_x_pinned(Card *c) { return c->n.T->x + c->n.T->w / 2 - 100 * (c->pinned ? c->sort_id : 0); }

void CardArea_align_cards(CardArea *a) {
    if ((a == G.hand || a == G.deck || a == G.discard || a == G.play) && G.view_deck.count > 0) return;
    int n = a->cards.count;
    double rm = G.SETTINGS.reduced_motion ? 0 : 1;
    Node *an = &a->n;
    if (type_is(a, "deck")) {
        double dh = (a->config.deck_height != 0 ? a->config.deck_height : 0.15) / 52;
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (card->facing == FACE_FRONT) Card_flip(card);
            if (!card->n.states.drag.is) {
                double frac = (double)n / (a == G.deck ? 1 : 2) - k;
                card->n.T->x = an->T->x + 0.5 * (an->T->w - card->n.T->w) + an->shadow_parrallax->x * dh * frac + 0.9 * a->shuffle_amt * (1 - k * 0.01) * (k % 2 == 1 ? 1 : -0.0);
                card->n.T->y = an->T->y + 0.5 * (an->T->h - card->n.T->h) + an->shadow_parrallax->y * dh * frac;
                card->n.T->r = 0 + 0.3 * a->shuffle_amt * (1 + k * 0.05) * (k % 2 == 1 ? 1 : -0.0);
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
    }
    if (type_is(a, "discard")) {
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (card->facing == FACE_FRONT) Card_flip(card);
            if (!card->n.states.drag.is) {
                card->n.T->x = an->T->x + (an->T->w - card->n.T->w) * card->discard_pos.x;
                card->n.T->y = an->T->y + (an->T->h - card->n.T->h) * card->discard_pos.y;
                card->n.T->r = card->discard_pos.r;
            }
        }
    }
    bool pack_state = G.STATE == STATE_TAROT_PACK || G.STATE == STATE_SPECTRAL_PACK || G.STATE == STATE_PLANET_PACK;
    if (type_is(a, "hand") && pack_state) {
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (!card->n.states.drag.is) {
                card->n.T->r = 0.4 * (-n / 2.0 - 0.5 + k) / n + rm * 0.02 * sin(2 * G.TIMERS.REAL + card->n.T->x);
                double max_cards = MAX(n, a->config.temp_limit);
                card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 1) / MAX(max_cards - 1, 1) - 0.5 * (n - max_cards) / MAX(max_cards - 1, 1)) + 0.5 * (a->card_w - card->n.T->w);
                double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
                card->n.T->y = G.hand->n.T->y - 1.8 * card->n.T->h - hh + rm * 0.1 * sin(0.666 * G.TIMERS.REAL + card->n.T->x) + pow(fabs(1.3 * (-n / 2.0 + k - 0.5) / n), 2) - 0.3;
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
        sort_cards_by(&a->cards, key_x);
    }
    if (type_is(a, "hand") && !pack_state) {
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (!card->n.states.drag.is) {
                card->n.T->r = 0.2 * (-n / 2.0 - 0.5 + k) / n + rm * 0.02 * sin(2 * G.TIMERS.REAL + card->n.T->x);
                double max_cards = MAX(n, a->config.temp_limit);
                card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 1) / MAX(max_cards - 1, 1) - 0.5 * (n - max_cards) / MAX(max_cards - 1, 1)) + 0.5 * (a->card_w - card->n.T->w);
                double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
                card->n.T->y = an->T->y + an->T->h / 2 - card->n.T->h / 2 - hh + rm * 0.03 * sin(0.666 * G.TIMERS.REAL + card->n.T->x) + fabs(0.5 * (-n / 2.0 + k - 0.5) / n) - 0.2;
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
        sort_cards_by(&a->cards, key_x);
    }
    if (type_is(a, "title") || (type_is(a, "voucher") && n == 1)) {
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (!card->n.states.drag.is) {
                card->n.T->r = 0.2 * (-n / 2.0 - 0.5 + k) / n + rm * 0.02 * sin(2 * G.TIMERS.REAL + card->n.T->x);
                double max_cards = MAX(n, a->config.temp_limit);
                card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 1) / MAX(max_cards - 1, 1) - 0.5 * (n - max_cards) / MAX(max_cards - 1, 1)) + 0.5 * (a->card_w - card->n.T->w);
                double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
                card->n.T->y = an->T->y + an->T->h / 2 - card->n.T->h / 2 - hh + rm * 0.03 * sin(0.666 * G.TIMERS.REAL + card->n.T->x) + fabs(0.5 * (-n / 2.0 + k - 0.5) / n) - (n > 1 ? 0.2 : 0);
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
        sort_cards_by(&a->cards, key_x);
    }
    if (type_is(a, "voucher") && n > 1) {
        double self_w = MAX(an->T->w, 3.2);
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (!card->n.states.drag.is) {
                card->n.T->r = 0.2 * (-n / 2.0 - 0.5 + k) / n + rm * 0.02 * sin(2 * G.TIMERS.REAL + card->n.T->x + card->n.T->y) + (k % 2 == 0 ? 1 : -1) * 0.08;
                double max_cards = MAX(n, a->config.temp_limit);
                card->n.T->x = an->T->x + (self_w - a->card_w) * ((k - 1) / MAX(max_cards - 1, 1) - 0.5 * (n - max_cards) / MAX(max_cards - 1, 1)) + 0.5 * (a->card_w - card->n.T->w) + (k % 2 == 1 ? 1 : -1) * 0.27 + (an->T->w - self_w) / 2;
                double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
                card->n.T->y = an->T->y + an->T->h / 2 - card->n.T->h / 2 - hh + rm * 0.03 * sin(0.666 * G.TIMERS.REAL + card->n.T->x) + fabs(0.5 * (-n / 2.0 + k - 0.5) / n) - (n > 1 ? 0.2 : 0);
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
        sort_cards_by(&a->cards, key_order);
    }
    if (type_is(a, "play") || type_is(a, "shop")) {
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (!card->n.states.drag.is) {
                card->n.T->r = 0;
                double max_cards = MAX(n, a->config.temp_limit);
                card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 1) / MAX(max_cards - 1, 1) - 0.5 * (n - max_cards) / MAX(max_cards - 1, 1)) + 0.5 * (a->card_w - card->n.T->w) +
                               (a->config.card_limit == 1 ? 0.5 * (an->T->w - card->n.T->w) : 0);
                double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
                card->n.T->y = an->T->y + an->T->h / 2 - card->n.T->h / 2 - hh;
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
        sort_cards_by(&a->cards, key_x);
    }
    if (type_is(a, "joker") || type_is(a, "title_2")) {
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (!card->n.states.drag.is) {
                card->n.T->r = 0.1 * (-n / 2.0 - 0.5 + k) / n + rm * 0.02 * sin(2 * G.TIMERS.REAL + card->n.T->x);
                double max_cards = MAX(n, a->config.temp_limit);
                card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 1) / MAX(max_cards - 1, 1) - 0.5 * (n - max_cards) / MAX(max_cards - 1, 1)) + 0.5 * (a->card_w - card->n.T->w);
                if (n > 2 || (n > 1 && a == G.consumeables) || (n > 1 && a->config.spread))
                    card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 1.0) / (n - 1)) + 0.5 * (a->card_w - card->n.T->w);
                else if (n > 1 && a != G.consumeables)
                    card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 0.5) / n) + 0.5 * (a->card_w - card->n.T->w);
                else
                    card->n.T->x = an->T->x + an->T->w / 2 - a->card_w / 2 + 0.5 * (a->card_w - card->n.T->w);
                double hh = card->highlighted ? G.HIGHLIGHT_H / 2 : 0;
                card->n.T->y = an->T->y + an->T->h / 2 - card->n.T->h / 2 - hh + rm * 0.03 * sin(0.666 * G.TIMERS.REAL + card->n.T->x);
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
        sort_cards_by(&a->cards, key_x_pinned);
    }
    if (type_is(a, "consumeable")) {
        for (int k = 1; k <= n; k++) {
            Card *card = a->cards.items[k - 1];
            if (!card->n.states.drag.is) {
                if (n > 1) card->n.T->x = an->T->x + (an->T->w - a->card_w) * ((k - 1.0) / (n - 1)) + 0.5 * (a->card_w - card->n.T->w);
                else card->n.T->x = an->T->x + an->T->w / 2 - a->card_w / 2 + 0.5 * (a->card_w - card->n.T->w);
                double hh = card->highlighted ? G.HIGHLIGHT_H : 0;
                card->n.T->y = an->T->y + an->T->h / 2 - card->n.T->h / 2 - hh + (!card->highlighted ? rm * 0.05 * sin(2 * 1.666 * G.TIMERS.REAL + card->n.T->x) : 0);
                card->n.T->x = card->n.T->x + card->n.shadow_parrallax->x / 30;
            }
        }
        sort_cards_by(&a->cards, key_x);
    }
    for (int k = 1; k <= a->cards.count; k++) a->cards.items[k - 1]->rank = k;
    Node *vd = child_get(an, "view_deck");
    if (vd) Moveable_set_role(vd, ROLE_ARGS(.major = a->cards.count ? &a->cards.items[0]->n : an));
}

static void CardArea_hard_set_T(Node *n, double x, double y, double w, double h) {
    CardArea *a = (CardArea *)n;
    if (isnan(x)) x = n->T->x;
    if (isnan(y)) y = n->T->y;
    if (isnan(w)) w = n->T->w;
    if (isnan(h)) h = n->T->h;
    Moveable_hard_set_T(n, x, y, w, h);
    Moveable_calculate_parrallax(n);
    CardArea_align_cards(a);
    CardArea_hard_set_cards(a);
}

void CardArea_hard_set_cards(CardArea *a) {
    for (int i = 0; i < a->cards.count; i++) {
        Card *c = a->cards.items[i];
        Card_hard_set_T(&c->n, NAN, NAN, NAN, NAN);
        Moveable_calculate_parrallax(&c->n);
    }
}

void CardArea_shuffle(CardArea *a, const char *seed) {
    pseudoshuffle_cards(&a->cards, pseudoseed(seed ? seed : "shuffle"));
    CardArea_set_ranks(a);
}

static double key_nominal(Card *c) { return Card_get_nominal(c, NULL); }
static double key_nominal_suit(Card *c) { return Card_get_nominal(c, "suit"); }
static double key_nominal_neg(Card *c) { return -Card_get_nominal(c, NULL); }
static double key_nominal_suit_neg(Card *c) { return -Card_get_nominal(c, "suit"); }
static double key_card_order(Card *c) {
    return (c->config.card && c->config.card->has_order) ? c->config.card->order : c->config.center->order;
}

void CardArea_sort(CardArea *a, const char *method) {
    if (method) a->config.sort = intern(method);
    const char *s = a->config.sort;
    if (!strcmp(s, "desc")) sort_cards_by(&a->cards, key_nominal_neg);
    else if (!strcmp(s, "asc")) sort_cards_by(&a->cards, key_nominal);
    else if (!strcmp(s, "suit desc")) sort_cards_by(&a->cards, key_nominal_suit_neg);
    else if (!strcmp(s, "suit asc")) sort_cards_by(&a->cards, key_nominal_suit);
    else if (!strcmp(s, "order")) sort_cards_by(&a->cards, key_card_order);
}

bool CardArea_draw_card_from(CardArea *a, CardArea *from, bool stay_flipped, bool discarded_only) {
    (void)stay_flipped;
    if (!from) return false;
    if (a->cards.count < a->config.card_limit || a == G.deck || a == G.hand) {
        Card *card = CardArea_remove_card(from, NULL, discarded_only);
        if (card) {
            if (from == G.discard) card->n.T->r = 0;
            bool sf = G.GAME.blind ? Blind_stay_flipped(G.GAME.blind, a, card) : false;
            if (a == G.hand && G.GAME.modifiers.flipped_cards != 0) {
                if (pseudorandom_seed(pseudoseed("flipped_card")) < 1 / G.GAME.modifiers.flipped_cards) sf = true;
            }
            CardArea_emplace(a, card, NULL, sf);
            return true;
        }
    }
    return false;
}

static void CardArea_click(Node *n) {
    CardArea *a = (CardArea *)n;
    if (a == G.deck) G_FUNCS_deck_info(NULL);
}

void CardArea_release(CardArea *a, Node *dragged) { (void)a; (void)dragged; }

DVal CardArea_save(CardArea *a) {
    DVal t = dv_table();
    DVal cards = dv_table();
    for (int i = 0; i < a->cards.count; i++) dv_seti(&cards, i + 1, Card_save(a->cards.items[i]));
    dv_set(&t, "cards", cards);
    DVal cfg = dv_table();
    CardAreaConfig *c = &a->config;
    dv_set_num(&cfg, "card_limit", c->card_limit);
    if (c->has_real_card_limit) dv_set_num(&cfg, "real_card_limit", c->real_card_limit);
    dv_set_num(&cfg, "highlighted_limit", c->highlighted_limit);
    dv_set_num(&cfg, "temp_limit", c->temp_limit);
    dv_set_num(&cfg, "card_count", c->card_count);
    dv_set_str(&cfg, "type", c->type);
    dv_set_str(&cfg, "sort", c->sort);
    dv_set_num(&cfg, "lr_padding", c->lr_padding);
    if (c->card_w != 0) dv_set_num(&cfg, "card_w", c->card_w);
    if (c->collection) dv_set_bool(&cfg, "collection", true);
    if (c->view_deck) dv_set_bool(&cfg, "view_deck", true);
    if (c->deck_height != 0) dv_set_num(&cfg, "deck_height", c->deck_height);
    if (c->thin_draw) dv_set_num(&cfg, "thin_draw", c->thin_draw);
    if (c->spread) dv_set_bool(&cfg, "spread", true);
    if (c->has_last_poll_size) dv_set_num(&cfg, "last_poll_size", c->last_poll_size);
    dv_set(&t, "config", cfg);
    return t;
}

void CardArea_load(CardArea *a, DVal *t) {
    remove_all_nodes((PtrVec *)&a->cards);
    vec_clear(&a->cards);
    remove_all_children(&a->n);
    vec_clear(&a->highlighted);
    DVal *cfg = dv_get(t, "config");
    CardAreaConfig *c = &a->config;
    c->card_limit = dv_get_num(cfg, "card_limit", c->card_limit);
    if (dv_get(cfg, "real_card_limit")) { c->has_real_card_limit = true; c->real_card_limit = dv_get_num(cfg, "real_card_limit", 0); }
    c->highlighted_limit = dv_get_num(cfg, "highlighted_limit", c->highlighted_limit);
    c->temp_limit = dv_get_num(cfg, "temp_limit", c->temp_limit);
    c->card_count = dv_get_num(cfg, "card_count", 0);
    const char *ty = dv_get_str(cfg, "type", NULL);
    if (ty) c->type = intern(ty);
    const char *so = dv_get_str(cfg, "sort", NULL);
    if (so) c->sort = intern(so);
    c->lr_padding = dv_get_num(cfg, "lr_padding", c->lr_padding);
    c->card_w = dv_get_num(cfg, "card_w", c->card_w);
    c->collection = dv_get_bool(cfg, "collection", c->collection);
    c->view_deck = dv_get_bool(cfg, "view_deck", c->view_deck);
    c->deck_height = dv_get_num(cfg, "deck_height", c->deck_height);
    c->thin_draw = (int)dv_get_num(cfg, "thin_draw", c->thin_draw);
    c->spread = dv_get_bool(cfg, "spread", c->spread);
    if (dv_get(cfg, "last_poll_size")) { c->has_last_poll_size = true; c->last_poll_size = dv_get_num(cfg, "last_poll_size", 0); }
    DVal *cards = dv_get(t, "cards");
    int n = dv_len(cards);
    for (int i = 1; i <= n; i++) {
        Card *card = card_new(0, 0, G.CARD_W, G.CARD_H, P_CARD("empty"), P_CENTER("j_joker"), NULL);
        /* the original passes G.P_CENTERS.j_joker as the "card" and c_base as center */
        Card_load(card, dv_geti(cards, i));
        vec_push(&a->cards, card);
        if (card->highlighted) vec_push(&a->highlighted, card);
        Card_set_card_area(card, a);
    }
    CardArea_set_ranks(a);
    CardArea_align_cards(a);
    CardArea_hard_set_cards(a);
}

static void CardArea_remove(Node *n) {
    CardArea *a = (CardArea *)n;
    remove_all_nodes((PtrVec *)&a->cards);
    vec_clear(&a->cards);
    remove_all_children(n);
    ptrvec_remove_all(&G.I.CARDAREA, n);
    Moveable_remove(n);
}

static void CA_noop(Node *n) { (void)n; }
static void CA_noop_rel(Node *n, Node *d) { (void)n; (void)d; }

const NodeVT CARDAREA_VT = {
    .type = NT_CARDAREA, .super = &MOVEABLE_VT, .name = "CardArea",
    .draw = CardArea_draw,
    .update = CardArea_update,
    .move = CardArea_move,
    .remove = CardArea_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = CardArea_click,
    .release = CA_noop_rel,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = CA_noop,
    .juice_up = Moveable_juice_up,
    .hard_set_T = CardArea_hard_set_T,
};
