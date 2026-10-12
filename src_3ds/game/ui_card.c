/* ui_card.c - card related parts of UI_definitions.lua
 * (use/sell buttons, focus UI, popups, badges, tooltips, alerts) */
#include "game.h"

#define LOC(k) localize_key(k)

/* ------------------------------------------------------------------ */
/* G.UIDEF.use_and_sell_buttons                                        */
/* ------------------------------------------------------------------ */
UIDef *UIDEF_use_and_sell_buttons(Card *card) {
    UIDef *sell = NULL, *use = NULL;
    if (card->area && str_eq(card->area->config.type, "joker")) {
        sell = UIC(CFG(.align = "cr"),
            UIC(CFG(.ref_table = card, .align = "cr", .padding = 0.1, .r = 0.08, .minw = 1.25, .hover = true, .shadow = true,
                    .colour = G.C.UI.BACKGROUND_INACTIVE, .one_press = true, .button = G_FUNCS_sell_card, .func = G_FUNCS_can_sell_card),
                UIB(CFG(.w = 0.1, .h = 0.6)),
                UIC(CFG(.align = "tm"),
                    UIR(CFG(.align = "cm", .maxw = 1.25),
                        UIT(CFG(.text = (char *)LOC("b_sell"), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.4, .shadow = true))),
                    UIR(CFG(.align = "cm"),
                        UIT(CFG(.text = (char *)LOC("$"), .colour = G.C.WHITE, .scale = 0.4, .shadow = true)),
                        UIT(CFG(.ref = REFBUF(card->sell_cost_label_str), .colour = G.C.WHITE, .scale = 0.55, .shadow = true))))));
    }
    if (card->ability.consumeable) {
        if (G.pack_cards && card->area == G.pack_cards) {
            return UIROOT(CFG(.padding = 0, .colour = G.C.CLEAR),
                UIR(CFG(.mid = true)),
                UIR(CFG(.ref_table = card, .r = 0.08, .padding = 0.1, .align = "bm", .minw = 0.5 * card->n.T->w - 0.15, .minh = 0.8 * card->n.T->h,
                        .maxw = 0.7 * card->n.T->w - 0.15, .hover = true, .shadow = true, .colour = G.C.UI.BACKGROUND_INACTIVE, .one_press = true,
                        .button = G_FUNCS_use_card, .func = G_FUNCS_can_use_consumeable),
                    UIT(CFG(.text = (char *)LOC("b_use"), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.55, .shadow = true))));
        }
        use = UIC(CFG(.align = "cr"),
            UIC(CFG(.ref_table = card, .align = "cr", .maxw = 1.25, .padding = 0.1, .r = 0.08, .minw = 1.25,
                    .minh = (card->area && str_eq(card->area->config.type, "joker")) ? 0 : 1, .hover = true, .shadow = true,
                    .colour = G.C.UI.BACKGROUND_INACTIVE, .one_press = true, .button = G_FUNCS_use_card, .func = G_FUNCS_can_use_consumeable),
                UIB(CFG(.w = 0.1, .h = 0.6)),
                UIT(CFG(.text = (char *)LOC("b_use"), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.55, .shadow = true))));
    } else if (card->area && card->area == G.pack_cards) {
        return UIROOT(CFG(.padding = 0, .colour = G.C.CLEAR),
            UIR(CFG(.ref_table = card, .r = 0.08, .padding = 0.1, .align = "bm", .minw = 0.5 * card->n.T->w - 0.15, .maxw = 0.9 * card->n.T->w - 0.15,
                    .minh = 0.3 * card->n.T->h, .hover = true, .shadow = true, .colour = G.C.UI.BACKGROUND_INACTIVE, .one_press = true,
                    .button = G_FUNCS_use_card, .func = G_FUNCS_can_select_card),
                UIT(CFG(.text = (char *)LOC("b_select"), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.45, .shadow = true))));
    }
    return UIROOT(CFG(.padding = 0, .colour = G.C.CLEAR),
        UIC(CFG(.padding = 0.15, .align = "cl"),
            UIR(CFG(.align = "cl"), sell ? sell : UISKIP),
            UIR(CFG(.align = "cl"), use ? use : UISKIP)));
}

/* ------------------------------------------------------------------ */
/* G.UIDEF.card_focus_ui / card_focus_button                           */
/* ------------------------------------------------------------------ */
typedef struct {
    Card *card; UIElement *parent; const char *type; UIFunc func, button; double card_width; bool buy_and_use;
} FocusButtonArgs;

static UIBox *card_focus_button(FocusButtonArgs a) {
    UIDef *contents = NULL;
    const char *t = a.type;
    bool sell = !strcmp(t, "sell");
    if (sell) {
        contents = UIC(CFG(.align = "cl"),
            UIR(CFG(.align = "cl", .maxw = 1), UIT(CFG(.text = (char *)LOC("b_sell"), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.4, .shadow = true))),
            UIR(CFG(.align = "cl"),
                UIT(CFG(.text = (char *)LOC("$"), .colour = G.C.WHITE, .scale = 0.4, .shadow = true)),
                UIT(CFG(.ref = REFBUF(a.card->sell_cost_label_str), .colour = G.C.WHITE, .scale = 0.55, .shadow = true))));
    } else if (!strcmp(t, "buy")) contents = UIT(CFG(.text = (char *)LOC("b_buy"), .colour = G.C.WHITE, .scale = 0.5));
    else if (!strcmp(t, "select")) contents = UIT(CFG(.text = (char *)LOC("b_select"), .colour = G.C.WHITE, .scale = 0.3));
    else if (!strcmp(t, "redeem")) contents = UIT(CFG(.text = (char *)LOC("b_redeem"), .colour = G.C.WHITE, .scale = 0.5));
    else if (!strcmp(t, "use")) contents = UIT(CFG(.text = (char *)LOC("b_use"), .colour = G.C.WHITE, .scale = 0.5));
    else if (!strcmp(t, "buy_and_use")) {
        contents = UIC(CFG(.align = "cr"),
            UIR(CFG(.align = "cr", .maxw = 1), UIT(CFG(.text = (char *)LOC("b_buy"), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.4, .shadow = true))),
            UIR(CFG(.align = "cr", .maxw = 1), UIT(CFG(.text = (char *)LOC("b_and_use"), .colour = G.C.WHITE, .scale = 0.3, .shadow = true))));
    } else contents = uinodes();
    bool bau = !strcmp(t, "buy_and_use");
    const char *pip_button = sell ? "leftshoulder" : bau ? "leftshoulder" : "rightshoulder";
    UIDef *def = UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIR(CFG(.id = bau ? "buy_and_use" : NULL, .ref_table = a.card, .ref_parent = a.parent, .align = sell ? "cl" : "cr", .colour = G.C.BLACK,
                .shadow = true, .r = 0.08, .func = a.func, .one_press = true, .button = a.button, .focus_args = FOCUS(.type = "none"), .hover = true),
            UIR(CFG(.align = sell ? "cl" : "cr", .minw = 1 + (!strcmp(t, "select") ? 0.1 : 0), .minh = sell ? 1.5 : 1, .padding = 0.08,
                    .focus_args = FOCUS(.button = pip_button, .scale = 0.55, .orientation = sell ? "tli" : "tri", .has_offset = true,
                                        .offset = { sell ? 0.1 : -0.1, 0 }, .type = "none"),
                    .func = G_FUNCS_set_button_pip),
                UIR(CFG(.align = "cm", .minh = 0.3)),
                UIR(CFG(.align = "cm"),
                    UIIF(!sell, UIC(CFG(.align = "cm", .minw = 0.2, .minh = 0.6))),
                    UIC(CFG(.align = "cm", .maxw = 1), contents),
                    UIIF(sell, UIC(CFG(.align = "cm", .minw = 0.2, .minh = 0.6)))))));
    return uibox_new_T(def, UIBCFG(.align = sell ? "cl" : "cr", .has_offset = true,
                                   .offset = { (sell ? -1 : 1) * (a.card_width - 0.17 - a.card->n.T->w / 2), bau ? 0.6 : a.buy_and_use ? -0.6 : 0 },
                                   .parent = &a.parent->n),
                       a.card->n.VT.x, a.card->n.VT.y, 0, 0);
}

UIDef *UIDEF_card_focus_button(Card *card, const char *type, UIFunc func, UIFunc button, const char *pos) {
    (void)pos;
    UIBox *b = card_focus_button((FocusButtonArgs){ .card = card, .type = type, .func = func, .button = button, .card_width = card->n.T->w });
    return b ? b->definition : NULL;
}

void card_focus_ui_set_alignment(Card *card) {
    Node *fu = child_get(&card->n, "focused_ui");
    if (!fu) return;
    double cnx = card->n.T->x + card->n.T->w / 2 - G.ROOM->T->w / 2, cny = card->n.T->y + card->n.T->h / 2 - G.ROOM->T->h / 2;
    Moveable_set_alignment(fu, ALIGN_ARGS(.has_offset = true, .offset = { 0.007 * cnx * card->n.T->w, 0.007 * cny * card->n.T->h }));
}

UIBox *UIDEF_card_focus_ui(Card *card) {
    double card_width = card->n.T->w + (card->ability.consumeable ? -0.1 : str_eq(card->ability.set, "Voucher") ? -0.16 : 0);
    Col playing_card_colour = col_copy(G.C.WHITE);
    playing_card_colour[3] = 1.5;
    if (G.hand && card->area == G.hand) ease_value(REFF(&playing_card_colour[3]), -1.5, false, TIMER_REAL, false, 0.2, EASE_QUAD);
    double tcnx = card->n.T->x + card->n.T->w / 2 - G.ROOM->T->w / 2, tcny = card->n.T->y + card->n.T->h / 2 - G.ROOM->T->h / 2;
    bool not_hand = !G.hand || card->area != G.hand;
    UIDef *def = not_hand
        ? UIROOT(CFG(.align = "cm", .minw = card_width + 0.3, .minh = card->n.T->h + 0.3, .r = 0.1, .colour = adjust_alpha(G.C.BLACK, 0.7),
                     .outline_colour = lighten(G.C.JOKER_GREY, 0.5), .outline = 1.5, .line_emboss = 0.8),
                 UIR(CFG(.id = "ATTACH_TO_ME")))
        : UIROOT(CFG(.align = "cm", .minw = card_width, .minh = card->n.T->h, .r = 0.1, .colour = playing_card_colour),
                 UIR(CFG(.id = "ATTACH_TO_ME")));
    UIBox *base = uibox_new_T(def, UIBCFG(.align = "cm", .has_offset = true, .offset = { 0.007 * tcnx * card->n.T->w, 0.007 * tcny * card->n.T->h },
                                          .parent = &card->n, .r_bond = not_hand ? BOND_WEAK : BOND_STRONG),
                              card->n.VT.x, card->n.VT.y, 0, 0);
    UIElement *attach = UIBox_get_UIE_by_ID(base, "ATTACH_TO_ME", NULL);
    if (!attach) return base;
#define FB(name, ...) child_set(&attach->n, name, &card_focus_button((FocusButtonArgs){ .card = card, .parent = attach, __VA_ARGS__ })->n)
    if (G.shop_jokers && card->area == G.shop_jokers) {
        bool bau = false;
        if (card->ability.consumeable) {
            FB("buy_and_use", .type = "buy_and_use", .func = G_FUNCS_can_buy_and_use, .button = G_FUNCS_buy_from_shop, .card_width = card_width);
            bau = true;
        }
        FB("buy", .type = "buy", .func = G_FUNCS_can_buy, .button = G_FUNCS_buy_from_shop, .card_width = card_width, .buy_and_use = bau);
    }
    if (G.shop_vouchers && card->area == G.shop_vouchers)
        FB("redeem", .type = "buy", .func = G_FUNCS_can_redeem, .button = G_FUNCS_redeem_from_shop, .card_width = card_width);
    if (G.shop_booster && card->area == G.shop_booster)
        FB("redeem", .type = "buy", .func = G_FUNCS_can_open, .button = G_FUNCS_open_booster, .card_width = card_width * 0.85);
    if (((G.consumeables && card->area == G.consumeables) || (G.pack_cards && card->area == G.pack_cards)) && card->ability.consumeable)
        FB("use", .type = "use", .func = G_FUNCS_can_use_consumeable, .button = G_FUNCS_use_card, .card_width = card_width);
    if (G.pack_cards && card->area == G.pack_cards && !card->ability.consumeable)
        FB("use", .type = "select", .func = G_FUNCS_can_select_card, .button = G_FUNCS_use_card, .card_width = card_width);
    if (((G.jokers && card->area == G.jokers) || (G.consumeables && card->area == G.consumeables)) && G.STATE != STATE_TUTORIAL)
        FB("sell", .type = "sell", .func = G_FUNCS_can_sell_card, .button = G_FUNCS_sell_card, .card_width = card_width);
#undef FB
    return base;
}

/* ------------------------------------------------------------------ */
/* speech bubble, highlight, character button                          */
/* ------------------------------------------------------------------ */
UIDef *UIDEF_speech_bubble(const char *text_key, bool quip, LocVars *vars) {
    UIDef *text = uinodes();
    if (!vars) vars = loc_vars_new();
    if (quip) localize_ui((LocArgs){ .type = "quips", .key = text_key ? text_key : "lq_1", .vars = vars, .nodes = text });
    else localize_ui((LocArgs){ .type = "tutorial", .key = text_key ? text_key : "sb_1", .vars = vars, .nodes = text });
    UIDef *row = uinodes();
    for (int i = 0; i < text->count; i++) uidef_add(row, uidef_add_all(UIR(CFG(.align = "cl")), text->nodes[i]));
    return UIROOT(CFG(.align = "cm", .minh = 1, .r = 0.3, .padding = 0.07, .minw = 1, .colour = G.C.JOKER_GREY, .shadow = true),
        UIC(CFG(.align = "cm", .minh = 1, .r = 0.2, .padding = 0.1, .minw = 1, .colour = G.C.WHITE),
            uidef_add_all(UIC(CFG(.align = "cm", .minh = 1, .r = 0.2, .padding = 0.03, .minw = 1, .colour = G.C.WHITE)), row)));
}

UIDef *create_UIBox_highlight(Node *rect) {
    return UIROOT(CFG(.align = "cm", .minh = rect->T->h + 0.1, .minw = rect->T->w + 0.15, .r = 0.15, .colour = G.C.DARK_EDITION));
}

UIDef *create_UIBox_character_button(const char *label, UIFunc button, Col colour, UIFunc update_func, double maxw) {
    return UIROOT(CFG(.align = "cm", .padding = 0.1, .colour = G.C.CLEAR),
        UIC(CFG(.align = "tm", .minw = 1.9, .padding = 0.2, .minh = 1.2, .r = 0.1, .hover = true, .colour = colour ? colour : G.C.RED,
                .button = button, .func = update_func, .shadow = true, .maxw = maxw),
            UIR(CFG(.align = "cm", .padding = 0),
                UIT(CFG(.text = (char *)(label ? label : "NONE"), .scale = 0.55, .colour = G.C.UI.TEXT_LIGHT,
                        .focus_args = FOCUS(.button = "x", .orientation = "bm"), .func = G_FUNCS_set_button_pip)))));
}

/* ------------------------------------------------------------------ */
/* G.UIDEF.deck_preview                                                */
/* ------------------------------------------------------------------ */
static int cmp_suit_nominal_desc(const void *a, const void *b) {
    Card *x = *(Card *const *)a, *y = *(Card *const *)b;
    double nx = Card_get_nominal(x, "suit"), ny = Card_get_nominal(y, "suit");
    return nx > ny ? -1 : nx < ny ? 1 : 0;
}

UIDef *UIDEF_deck_preview(bool show_remaining) {
    (void)show_remaining;
    double _minh = 0.35, _minw = 0.5;
    static const char *suit_map[4] = { "Spades", "Hearts", "Clubs", "Diamonds" };
    int suit_counts[4] = { 0 }, mod_suit_counts[4] = { 0 };
    int SUITS[4][15] = { { 0 } };
    int rank_counts[15] = { 0 };
    bool mod_suit_diff = false;
    int wheel_flipped = 0;
    int stones = -1;
    qsort(G.playing_cards.items, (size_t)G.playing_cards.count, sizeof(void *), cmp_suit_nominal_desc);
    for (int i = 0; i < G.playing_cards.count; i++) {
        Card *v = G.playing_cards.items[i];
        if (str_eq(v->ability.effect, "Stone Card")) { if (stones < 0) stones = 0; }
        if ((v->area && v->area == G.deck) || v->ability.wheel_flipped) {
            if (v->ability.wheel_flipped) wheel_flipped++;
            if (str_eq(v->ability.effect, "Stone Card")) stones++;
            else {
                for (int s = 0; s < 4; s++) {
                    if (str_eq(v->base.suit, suit_map[s])) suit_counts[s]++;
                    if (Card_is_suit(v, suit_map[s], false, false)) mod_suit_counts[s]++;
                }
                int si = -1;
                for (int s = 0; s < 4; s++) if (str_eq(v->base.suit, suit_map[s])) si = s;
                if (si >= 0 && v->base.id >= 1 && v->base.id <= 14) SUITS[si][v->base.id]++;
                if (v->base.id >= 0 && v->base.id < 15) rank_counts[v->base.id]++;
            }
        }
    }
    bool wft = wheel_flipped > 0;
    Col flip_col = wft ? mix_colours(G.C.FILTER, G.C.WHITE, 0.7) : G.C.WHITE;

    UIDef *suit_labels = uinodes();
    uidef_add(suit_labels, UIR(CFG(.align = "cm", .r = 0.1, .padding = 0.04, .minw = _minw, .minh = 2 * _minh + 0.25),
        UIIF(stones >= 0, UIT(CFG(.text = (char *)tfmt("%s: ", LOC("ph_deck_preview_stones")), .colour = G.C.WHITE, .scale = 0.25, .shadow = true))),
        UIIF(stones >= 0, UIT(CFG(.text = (char *)tfmt("%d", stones), .colour = stones > 0 ? G.C.WHITE : G.C.UI.TRANSPARENT_LIGHT, .scale = 0.4, .shadow = true)))));

    static const char *rank_names[13] = { "A", "K", "Q", "J", "10", "9", "8", "7", "6", "5", "4", "3", "2" };
    UIDef *deck_tables = uinodes();
    UIDef *_row = uinodes();
    Col _bg_col = G.C.JOKER_GREY;
    for (int k = 1; k <= 13; k++) {
        const char *v = rank_names[k - 1];
        double _tscale = 0.3;
        Col rank_col = !strcmp(v, "A") ? _bg_col : (!strcmp(v, "K") || !strcmp(v, "Q") || !strcmp(v, "J")) ? G.C.WHITE : _bg_col;
        rank_col = mix_colours(rank_col, _bg_col, 0.8);
        uidef_add(_row, UIC(CFG(.align = "cm"),
            UIC(CFG(.align = "cm", .r = 0.1, .minw = _minw, .minh = _minh, .colour = rank_col, .emboss = 0.04, .padding = 0.03),
                UIR(CFG(.align = "cm"), UIT(CFG(.text = (char *)v, .colour = G.C.BLACK, .scale = 1.6 * _tscale))),
                UIR(CFG(.align = "cm", .minw = _minw + 0.04, .minh = _minh, .colour = G.C.L_BLACK, .r = 0.1),
                    UIT(CFG(.text = (char *)tfmt("%d", rank_counts[15 - k]), .colour = flip_col, .scale = _tscale, .shadow = true))))));
    }
    uidef_add(deck_tables, uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.04)), _row));

    Language *en = NULL;
    for (int i = 0; i < G.n_languages; i++) if (str_eq(G.LANGUAGES[i].key, "en-us")) en = &G.LANGUAGES[i];
    for (int j = 0; j < 4; j++) {
        _row = uinodes();
        _bg_col = mix_colours(suit_colour(suit_map[j]), G.C.L_BLACK, 0.7);
        for (int i = 14; i >= 2; i--) {
            int n = SUITS[j][i];
            uidef_add(_row, UIC(CFG(.align = "cm", .padding = 0.05, .minw = _minw + 0.098, .minh = _minh),
                UIT(CFG(.text = (char *)tfmt("%d", n), .colour = n > 0 ? flip_col : G.C.UI.TRANSPARENT_LIGHT, .scale = n > 0 ? 0.3 : 0.25, .shadow = true, .lang = en))));
        }
        uidef_add(deck_tables, uidef_add_all(UIR(CFG(.align = "cm", .r = 0.1, .padding = 0.04, .minh = 0.4, .colour = _bg_col)), _row));
    }

    for (int k = 0; k < 4; k++) {
        const char *v = suit_map[k];
        int _x = !strcmp(v, "Spades") ? 3 : !strcmp(v, "Hearts") ? 0 : !strcmp(v, "Clubs") ? 2 : 1;
        Sprite *t_s = sprite_new(0, 0, 0.3, 0.3, atlas_get(G.SETTINGS.colourblind_option ? "ui_2" : "ui_1"), (SpritePos){ _x, 1 });
        t_s->n.states.drag.can = false;
        t_s->n.states.hover.can = false;
        t_s->n.states.collide.can = false;
        if (mod_suit_counts[k] != suit_counts[k]) mod_suit_diff = true;
        uidef_add(suit_labels, UIR(CFG(.align = "cm", .r = 0.1, .padding = 0.03, .colour = G.C.JOKER_GREY),
            UIC(CFG(.align = "cm", .minw = _minw, .minh = _minh), UIO(CFG(.can_collide = -1, .object = &t_s->n))),
            UIC(CFG(.align = "cm", .minw = _minw * 2.4, .minh = _minh, .colour = G.C.L_BLACK, .r = 0.1),
                UIT(CFG(.text = (char *)tfmt("%d", suit_counts[k]), .colour = flip_col, .scale = 0.3, .shadow = true, .lang = en)),
                UIIF(mod_suit_counts[k] != suit_counts[k],
                     UIT(CFG(.text = (char *)tfmt(" (%d)", mod_suit_counts[k]), .colour = mix_colours(G.C.BLUE, G.C.WHITE, 0.7), .scale = 0.28, .shadow = true, .lang = en))))));
    }

    char *wheel_txt = NULL;
    if (wft) {
        LocVars *lv = loc_vars_new(); loc_vars_addn(lv, wheel_flipped);
        wheel_txt = localize_variable(wheel_flipped > 1 ? "deck_preview_wheel_plural" : "deck_preview_wheel_singular", lv);
    }
    return UIROOT(CFG(.align = "cm", .colour = G.C.JOKER_GREY, .r = 0.1, .emboss = 0.05, .padding = 0.07),
        UIR(CFG(.align = "cm", .r = 0.1, .emboss = 0.05, .colour = G.C.BLACK, .padding = 0.1),
            UIR(CFG(.align = "cm"),
                uidef_add_all(UIC(CFG(.align = "cm", .padding = 0.04)), suit_labels),
                uidef_add_all(UIC(CFG(.align = "cm", .padding = 0.02)), deck_tables)),
            UIIF(mod_suit_diff, UIR(CFG(.align = "cm"),
                UIC(CFG(.padding = 0.3, .r = 0.1, .colour = mix_colours(G.C.BLUE, G.C.WHITE, 0.7))),
                UIT(CFG(.text = (char *)tfmt(" %s", LOC("ph_deck_preview_effective")), .colour = G.C.WHITE, .scale = 0.3)))),
            UIIF(wft, UIR(CFG(.align = "cm"),
                UIC(CFG(.padding = 0.3, .r = 0.1, .colour = flip_col)),
                UIT(CFG(.text = (char *)tfmt(" %s", wheel_txt ? wheel_txt : ""), .colour = G.C.WHITE, .scale = 0.3))))));
}

/* ------------------------------------------------------------------ */
/* row helpers                                                         */
/* ------------------------------------------------------------------ */
UIDef *desc_from_rows(UIDef *desc_nodes, bool empty, double maxw) {
    UIDef *t = uinodes();
    if (desc_nodes)
        for (int i = 0; i < desc_nodes->count; i++) uidef_add(t, uidef_add_all(UIR(CFG(.align = "cm", .maxw = maxw)), desc_nodes->nodes[i]));
    return UIR(CFG(.align = "cm", .colour = empty ? G.C.CLEAR : G.C.UI.BACKGROUND_WHITE, .r = 0.1, .padding = 0.04, .minw = 2, .minh = 0.8,
                   .emboss = !empty ? 0.05 : 0, .filler = true),
               uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.03)), t));
}

UIDef *transparent_multiline_text(UIDef *desc_nodes) {
    UIDef *t = uinodes();
    if (desc_nodes)
        for (int i = 0; i < desc_nodes->count; i++) uidef_add(t, uidef_add_all(UIR(CFG(.align = "cm")), desc_nodes->nodes[i]));
    return uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.03)), t);
}

UIDef *info_tip_from_rows(UIDef *desc_nodes, const char *name) {
    UIDef *t = uinodes();
    if (desc_nodes)
        for (int i = 0; i < desc_nodes->count; i++) uidef_add(t, uidef_add_all(UIR(CFG(.align = "cm")), desc_nodes->nodes[i]));
    return UIR(CFG(.align = "cm", .colour = lighten(G.C.GREY, 0.15), .r = 0.1),
        UIR(CFG(.align = "tm", .minh = 0.36, .padding = 0.03), UIT(CFG(.text = (char *)(name ? name : ""), .scale = 0.32, .colour = G.C.UI.TEXT_LIGHT))),
        UIR(CFG(.align = "cm", .minw = 1.5, .minh = 0.4, .r = 0.1, .padding = 0.05, .colour = G.C.WHITE),
            uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.03)), t)));
}

UIDef *overlay_infotip(const char **rows, int nrows) {
    static const char *err[1] = { "ERROR" };
    if (!rows) { rows = err; nrows = 1; }
    UIDef *t = UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR, .padding = 0.1));
    for (int i = 0; i < nrows; i++)
        uidef_add(t, UIR(CFG(.align = "cm"), UIT(CFG(.text = (char *)rows[i], .colour = G.C.UI.TEXT_LIGHT, .scale = 0.45, .juice = true, .shadow = true))));
    return t;
}

UIDef *name_from_rows(UIDef *name_nodes, Col background_colour) {
    if (!name_nodes || name_nodes->count == 0) return NULL;
    return uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = background_colour, .emboss = background_colour ? 0.05 : 0)), name_nodes);
}

/* ------------------------------------------------------------------ */
/* G.UIDEF.card_h_popup                                                */
/* ------------------------------------------------------------------ */
UIDef *UIDEF_card_h_popup(Node *n, CardUITable *AUT) {
    if (!AUT) return NULL;
    Card *card = node_is(n, NT_CARD) ? (Card *)n : NULL;
    bool debuffed = card && card->debuff;
    Center *center = card ? card->config.center : NULL;
    Col card_type_colour = get_type_colour(center, card);
    const char *ct = AUT->card_type ? AUT->card_type : "";
    Col card_type_background;
    if (!strcmp(ct, "Locked")) card_type_background = G.C.BLACK;
    else if (!strcmp(ct, "Undiscovered")) card_type_background = darken(G.C.JOKER_GREY, 0.3);
    else if (!strcmp(ct, "Enhanced") || !strcmp(ct, "Default")) card_type_background = darken(G.C.BLACK, 0.1);
    else if (debuffed) card_type_background = darken(G.C.BLACK, 0.1);
    else if (card_type_colour) card_type_background = darken(G.C.BLACK, 0.1);
    else {
        Col *slot = set_colour_slot(ct, false);
        card_type_background = (slot && *slot) ? *slot : col_new(0, 1, 1, 1);
    }
    double outer_padding = 0.05;
    const char *card_type = localize_key(tfmt("k_%s", tlower(ct)));
    if ((!strcmp(ct, "Joker") || AUT->badges.force_rarity) && center && center->rarity >= 1 && center->rarity <= 4) {
        static const char *rk[4] = { "k_common", "k_uncommon", "k_rare", "k_legendary" };
        card_type = localize_key(rk[(int)center->rarity - 1]);
    }
    if (!strcmp(ct, "Enhanced") && center) card_type = localize_name_text("Enhanced", center->key);
    if (debuffed && strcmp(ct, "Enhanced")) card_type = localize_key("k_debuffed");
    bool is_playing_card = !strcmp(ct, "Enhanced") || !strcmp(ct, "Default");

    UIDef *info_boxes = uinodes();
    UIDef *badges = uinodes();
    if (AUT->badges.card_type || AUT->badges.force_rarity) {
        const char *nm = card ? card->ability.name : "";
        const char *s = (str_eq(nm, "Pluto") || str_eq(nm, "Ceres") || str_eq(nm, "Eris")) ? localize_key("k_dwarf_planet")
                      : str_eq(nm, "Planet X") ? localize_key("k_planet_q") : card_type;
        uidef_add(badges, create_badge(s, card_type_colour, NULL, 1.2));
    }
    for (int i = 0; i < AUT->badges.n; i++) {
        const char *v = AUT->badges.items[i];
        if (!strcmp(v, "negative_consumable")) v = "negative";
        uidef_add(badges, create_badge(localize_cat(v, "labels"), get_badge_colour(v), NULL, 1));
    }
    for (int i = 0; i < AUT->ninfos; i++) {
        uidef_add(info_boxes, UIR(CFG(.align = "cm"),
            UIR(CFG(.align = "cm", .colour = lighten(G.C.JOKER_GREY, 0.5), .r = 0.1, .padding = 0.05, .emboss = 0.05),
                info_tip_from_rows(AUT->infos[i], AUT->info_names ? AUT->info_names[i] : ""))));
    }
    UIDef *name = name_from_rows(AUT->name, is_playing_card ? G.C.WHITE : NULL);
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIC(CFG(.align = "cm", .func = G_FUNCS_show_infotip, .object = moveable_new(0, 0, 0, 0), .ref_table = info_boxes->count ? info_boxes : NULL),
            UIR(CFG(.padding = outer_padding, .r = 0.12, .colour = lighten(G.C.JOKER_GREY, 0.5), .emboss = 0.07),
                UIR(CFG(.align = "cm", .padding = 0.07, .r = 0.1, .colour = adjust_alpha(card_type_background, 0.8)),
                    name ? name : UISKIP,
                    desc_from_rows(AUT->main, false, 0),
                    badges->count ? uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.03)), badges) : UISKIP))));
}

Col get_badge_colour(const char *key) {
    static const char *keys[] = { "eternal", "perishable", "rental", "foil", "holographic", "polychrome", "negative",
                                  "gold_seal", "red_seal", "blue_seal", "purple_seal", "pinned_left" };
    Col cols[] = { G.C.ETERNAL, G.C.PERISHABLE, G.C.RENTAL, G.C.DARK_EDITION, G.C.DARK_EDITION, G.C.DARK_EDITION, G.C.DARK_EDITION,
                   G.C.GOLD, G.C.RED, G.C.BLUE, G.C.PURPLE, G.C.ORANGE };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) if (str_eq(key, keys[i])) return cols[i];
    static float red[4] = { 1, 0, 0, 1 };
    return red;
}

UIDef *create_badge(const char *s, Col badge_col, Col text_col, double scaling) {
    if (scaling == 0 || isnan(scaling)) scaling = 1;
    return UIR(CFG(.align = "cm"),
        UIR(CFG(.align = "cm", .colour = badge_col ? badge_col : G.C.GREEN, .r = 0.1, .minw = 2, .minh = 0.4 * scaling, .emboss = 0.05, .padding = 0.03 * scaling),
            UIB(CFG(.h = 0.1, .w = 0.03)),
            UIO(CFG(.object = &DYNATEXT(.parts = dt_parts_str(s ? s : "ERROR"), .nparts = 1, .colours = DT_COLS(text_col ? text_col : G.C.WHITE), .ncolours = 1,
                                        .float_ = true, .shadow = true, .offset_y = -0.05, .silent = true, .spacing = 1, .scale = 0.33 * scaling)->n)),
            UIB(CFG(.h = 0.1, .w = 0.03))));
}

UIDef *create_UIBox_detailed_tooltip(void *center, int kind) {
    CardUITable full = { 0 };
    full.main = uinodes();
    full.info = uinodes();
    full.name = NULL;
    full.name_set = true;   /* name = 'done' */
    const char *set = kind == 2 ? "Tag" : ((Center *)center)->set;
    CardUITable *desc = generate_card_ui(center, kind, &full, NULL, set, NULL, false, NULL, NULL);
    UIDef *rows = desc && desc->ninfos > 0 ? desc->infos[0] : uinodes();
    const char *nm = desc && desc->ninfos > 0 && desc->info_names ? desc->info_names[0] : "";
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
        UIR(CFG(.align = "cm", .colour = lighten(G.C.JOKER_GREY, 0.5), .r = 0.1, .padding = 0.05, .emboss = 0.05),
            info_tip_from_rows(rows, nm)));
}

UIDef *create_popup_UIBox_tooltip(Tooltip *tooltip) {
    UIDef *rows = uinodes();
    if (tooltip->title)
        uidef_add(rows, UIR(CFG(.align = "cm"), UIC(CFG(.align = "cm"), UIT(CFG(.text = (char *)tooltip->title, .colour = G.C.UI.TEXT_DARK, .scale = 0.4)))));
    for (int i = 0; i < tooltip->ntext; i++)
        uidef_add(rows, UIR(CFG(.align = "cm", .padding = 0.03), UIT(CFG(.text = (char *)tooltip->text[i], .colour = G.C.UI.TEXT_DARK, .scale = 0.4))));
    if (tooltip->ref.kind != REF_NONE)
        uidef_add(rows, UIR(CFG(.align = "cm", .padding = 0.03), UIT(CFG(.ref = tooltip->ref, .colour = G.C.UI.TEXT_DARK, .scale = 0.4))));
    if (tooltip->filler_fn) uidef_add(rows, tooltip->filler_fn(tooltip->filler_args));
    return UIROOT(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = G.C.RED, .emboss = 0.05),
        uidef_add_all(UIC(CFG(.align = "cm", .padding = 0.05, .r = 0.1, .colour = G.C.WHITE, .emboss = 0.05)), rows));
}

UIDef *create_UIBox_card_alert(CardAlertArgs a) {
    Col bg = a.no_bg ? G.C.CLEAR : a.bg_col ? a.bg_col : (a.red_bad ? darken(G.C.RED, 0.1) : G.C.RED);
    double scale = a.scale ? a.scale : 0.48;
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR, .refresh_movement = true),
        UIR(CFG(.align = "cm", .r = 0.15, .minw = 0.42, .minh = 0.42, .colour = bg, .draw_layer = 1, .emboss = 0.05, .refresh_movement = true),
            UIO(CFG(.object = &DYNATEXT(.parts = dt_parts_str(a.text ? a.text : "!"), .nparts = 1, .colours = DT_COLS(G.C.WHITE), .ncolours = 1,
                                        .shadow = true, .rotate = true, .bump_rate = a.text ? 3 : 7, .bump_amount = a.bump_amount ? a.bump_amount : 3,
                                        .bump = true, .maxw = a.maxw, .text_rot = a.has_text_rot ? a.text_rot : 0.2, .spacing = 3 * (a.scale ? a.scale : 1),
                                        .scale = scale)->n))));
}

/* ------------------------------------------------------------------ */
/* shared sticker sprites                                              */
/* ------------------------------------------------------------------ */
Sprite *shared_sticker(const char *name) {
    if (!name) return NULL;
#define S(n) if (!strcmp(name, #n)) return G.shared_stickers.n
    S(White); S(Red); S(Green); S(Black); S(Blue); S(Purple); S(Orange); S(Gold);
#undef S
    return NULL;
}
