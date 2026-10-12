/* ui_hud.c - run screen parts of UI_definitions.lua
 * (shop, attention text, play buttons, HUD, blind select, round eval, booster packs) */
#include "game.h"

#define LOC(k) ((char *)localize_key(k))
#define EVFN(name) static bool name(Event *e_, void *p_)
static void nofree(void *p) { (void)p; }

static Language *lang_en(void) {
    for (int i = 0; i < G.n_languages; i++) if (str_eq(G.LANGUAGES[i].key, "en-us")) return &G.LANGUAGES[i];
    return G.LANG;
}
static void *font_en(void) { Language *l = lang_en(); return l ? l->font : NULL; }

/* ------------------------------------------------------------------ */
/* UIBox_dyn_container / simple_text_container                         */
/* ------------------------------------------------------------------ */
UIDef *UIBox_dyn_container(UIDef *inner, bool horizontal, Col colour_override, Col background_override, bool flipped, double padding) {
    return UIR(CFG(.align = "cm", .padding = 0.03, .colour = G.C.UI.TRANSPARENT_DARK, .r = 0.1),
        UIR(CFG(.align = "cm", .padding = 0.05, .colour = colour_override ? colour_override : G.C.DYN_UI.MAIN, .r = 0.1),
            uidef_add_all(UIR(CFG(.align = horizontal ? "cl" : (flipped ? "bm" : "tm"), .colour = background_override ? background_override : G.C.DYN_UI.BOSS_DARK,
                                  .minw = horizontal ? 100 : 0, .minh = horizontal ? 0 : 30, .r = 0.1, .padding = padding ? padding : 0.08)),
                          inner)));
}

UIDef *simple_text_container(const char *loc, SimpleTextArgs a) {
    if (!loc) return NULL;
    DVal *res = dv_get(loc_misc("dictionary"), loc);
    if (!res || res->t != DV_TABLE) return NULL;
    UIDef *container = uinodes();
    int n = dv_len(res);
    for (int i = 1; i <= n; i++) {
        const char *v = dv_geti_str(res, i, "");
        uidef_add(container, UIR(CFG(.align = "cm", .padding = 0),
            UIT(CFG(.text = (char *)v, .scale = a.scale ? a.scale : 0.35, .colour = a.colour ? a.colour : G.C.UI.TEXT_DARK, .shadow = a.shadow))));
    }
    return uidef_add_all(UIN(a.col ? UIT_C : UIT_R, CFG(.align = "cm", .padding = a.has_padding ? a.padding : 0.03)), container);
}

/* ------------------------------------------------------------------ */
/* G.UIDEF.shop                                                        */
/* ------------------------------------------------------------------ */
EVFN(ev_shop_sign_drop) { (void)e_; (void)p_; if (G.SHOP_SIGN) G.SHOP_SIGN->n.alignment.offset.y = 0; return true; }

UIDef *UIDEF_shop(void) {
    G.shop_jokers = cardarea_new(G.hand->n.T->x + 0, G.hand->n.T->y + G.ROOM->T->y + 9, G.GAME.shop.joker_max * 1.02 * G.CARD_W, 1.05 * G.CARD_H,
                                 CA_ARGS(.card_limit = G.GAME.shop.joker_max, .has_card_limit = true, .type = "shop", .highlight_limit = 1, .has_highlight_limit = true));
    G.shop_jokers->name = "shop_jokers";
    G.shop_vouchers = cardarea_new(G.hand->n.T->x + 0, G.hand->n.T->y + G.ROOM->T->y + 9, 2.1 * G.CARD_W, 1.05 * G.CARD_H,
                                   CA_ARGS(.card_limit = 1, .has_card_limit = true, .type = "shop", .highlight_limit = 1, .has_highlight_limit = true));
    G.shop_vouchers->name = "shop_vouchers";
    G.shop_booster = cardarea_new(G.hand->n.T->x + 0, G.hand->n.T->y + G.ROOM->T->y + 9, 2.4 * G.CARD_W, 1.15 * G.CARD_H,
                                  CA_ARGS(.card_limit = 2, .has_card_limit = true, .type = "shop", .highlight_limit = 1, .has_highlight_limit = true,
                                          .card_w = 1.27 * G.CARD_W));
    G.shop_booster->name = "shop_booster";

    Sprite *shop_sign = animated_sprite_new(0, 0, 4.4, 2.2, strmap_get(&G.ANIMATION_ATLAS, "shop_sign"), (SpritePos){ 0, 0 });
    DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
    Sprite_define_draw_steps(shop_sign, steps, 2);
    G.SHOP_SIGN = uibox_new(
        UIROOT(CFG(.colour = G.C.DYN_UI.MAIN, .emboss = 0.05, .align = "cm", .r = 0.1, .padding = 0.1),
            UIR(CFG(.align = "cm", .padding = 0.1, .minw = 4.72, .minh = 3.1, .colour = G.C.DYN_UI.DARK, .r = 0.1),
                UIR(CFG(.align = "cm"), UIO(CFG(.object = &shop_sign->n))),
                UIR(CFG(.align = "cm"), UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("ph_improve_run")), DT_COL1(lighten(G.C.GOLD, 0.3)), .shadow = true,
                                                                 .rotate = true, .float_ = true, .bump = true, .scale = 0.5, .spacing = 1,
                                                                 .pop_in = 1.5, .has_pop_in = true, .maxw = 4.3)->n))))),
        UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, -15 }, .major = &UIBox_get_UIE_by_ID(G.HUD, "row_blind", NULL)->n, .bond = BOND_WEAK));
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_shop_sign_drop));

    UIDef *inner = uinodes();
    uidef_add(inner,
        UIC(CFG(.align = "cm", .padding = 0.1, .emboss = 0.05, .r = 0.1, .colour = G.C.DYN_UI.BOSS_MAIN),
            UIR(CFG(.align = "cm", .padding = 0.05),
                UIC(CFG(.align = "cm", .padding = 0.1),
                    UIR(CFG(.id = "next_round_button", .align = "cm", .minw = 2.8, .minh = 1.5, .r = 0.15, .colour = G.C.RED, .one_press = true,
                            .button = G_FUNCS_toggle_shop, .hover = true, .shadow = true),
                        UIR(CFG(.align = "cm", .padding = 0.07, .focus_args = FOCUS(.button = "y", .orientation = "cr"), .func = G_FUNCS_set_button_pip),
                            UIR(CFG(.align = "cm", .maxw = 1.3), UIT(CFG(.text = LOC("b_next_round_1"), .scale = 0.4, .colour = G.C.WHITE, .shadow = true))),
                            UIR(CFG(.align = "cm", .maxw = 1.3), UIT(CFG(.text = LOC("b_next_round_2"), .scale = 0.4, .colour = G.C.WHITE, .shadow = true))))),
                    UIR(CFG(.align = "cm", .minw = 2.8, .minh = 1.6, .r = 0.15, .colour = G.C.GREEN, .button = G_FUNCS_reroll_shop, .func = G_FUNCS_can_reroll,
                            .hover = true, .shadow = true),
                        UIR(CFG(.align = "cm", .padding = 0.07, .focus_args = FOCUS(.button = "x", .orientation = "cr"), .func = G_FUNCS_set_button_pip),
                            UIR(CFG(.align = "cm", .maxw = 1.3), UIT(CFG(.text = LOC("k_reroll"), .scale = 0.4, .colour = G.C.WHITE, .shadow = true))),
                            UIR(CFG(.align = "cm", .maxw = 1.3, .minw = 1),
                                UIT(CFG(.text = LOC("$"), .scale = 0.7, .colour = G.C.WHITE, .shadow = true)),
                                UIT(CFG(.ref = REFN(&G.GAME.current_round.reroll_cost), .scale = 0.75, .colour = G.C.WHITE, .shadow = true)))))),
                UIC(CFG(.align = "cm", .padding = 0.2, .r = 0.2, .colour = G.C.L_BLACK, .emboss = 0.05, .minw = 8.2),
                    UIO(CFG(.object = &G.shop_jokers->n)))),
            UIR(CFG(.align = "cm", .minh = 0.2)),
            UIR(CFG(.align = "cm", .padding = 0.1),
                UIC(CFG(.align = "cm", .padding = 0.15, .r = 0.2, .colour = G.C.L_BLACK, .emboss = 0.05),
                    UIC(CFG(.align = "cm", .padding = 0.2, .r = 0.2, .colour = G.C.BLACK, .maxh = G.shop_vouchers->n.T->h + 0.4),
                        UIT(CFG(.text = localize_variable("ante_x_voucher", LV(num_str(G.GAME.round_resets.ante))), .scale = 0.45, .colour = G.C.L_BLACK, .vert = true)),
                        UIO(CFG(.object = &G.shop_vouchers->n)))),
                UIC(CFG(.align = "cm", .padding = 0.15, .r = 0.2, .colour = G.C.L_BLACK, .emboss = 0.05),
                    UIO(CFG(.object = &G.shop_booster->n))))));
    return UIROOT(CFG(.align = "cl", .colour = G.C.CLEAR), UIBox_dyn_container(inner, false, NULL, NULL, false, 0));
}

/* ------------------------------------------------------------------ */
/* create_card_for_shop / create_shop_card_ui                          */
/* ------------------------------------------------------------------ */
typedef struct { Card *card; } ShopCardCtx;

EVFN(ev_store_modify_tags) {
    ShopCardCtx *c = p_; (void)e_;
    for (int i = 0; i < G.GAME.tags.count; i++)
        if (Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "store_joker_modify", .card = c->card }, NULL, NULL)) break;
    return true;
}

Card *create_card_for_shop(CardArea *area) {
    DVal *tp = &G.SETTINGS.tutorial_progress;
    DVal *fs = tp->t == DV_TABLE ? dv_get(tp, "forced_shop") : NULL;
    if (area == G.shop_jokers && fs && fs->t == DV_TABLE && dv_len(fs) > 0) {
        int n = dv_len(fs);
        const char *key = dv_geti_str(fs, n, "c_empress");
        Center *center = P_CENTER(key);
        if (!center) center = P_CENTER("c_empress");
        Card *card = card_new(area->n.T->x + area->n.T->w / 2, area->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"), center,
                              &(CardParams){ .bypass_discovery_center = true, .bypass_discovery_ui = true });
        dv_seti(fs, n, dv_nil());
        if (dv_len(fs) == 0) dv_set(tp, "forced_shop", dv_nil());
        create_shop_card_ui(card, NULL, NULL);
        return card;
    }
    for (int i = 0; i < G.GAME.tags.count; i++) {
        Card *forced = NULL;
        Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "store_joker_create", .area = area }, &forced, NULL);
        if (forced) {
            for (int k = 0; k < G.GAME.tags.count; k++)
                if (Tag_apply_to_run(G.GAME.tags.items[k], (TagContext){ .type = "store_joker_modify", .card = forced }, NULL, NULL)) break;
            return forced;
        }
    }
    double total_rate = G.GAME.joker_rate + G.GAME.tarot_rate + G.GAME.planet_rate + G.GAME.playing_card_rate + G.GAME.spectral_rate;
    double polled_rate = pseudorandom_seed(pseudoseed(tfmt("cdt%s", num_str(G.GAME.round_resets.ante)))) * total_rate;
    double check_rate = 0;
    const char *base_type = (strset_has(&G.GAME.used_vouchers, "v_illusion") && pseudorandom_seed(pseudoseed("illusion")) > 0.6) ? "Enhanced" : "Base";
    struct { const char *type; double val; } opts[5] = {
        { "Joker", G.GAME.joker_rate }, { "Tarot", G.GAME.tarot_rate }, { "Planet", G.GAME.planet_rate },
        { base_type, G.GAME.playing_card_rate }, { "Spectral", G.GAME.spectral_rate },
    };
    for (int i = 0; i < 5; i++) {
        if (polled_rate > check_rate && polled_rate <= check_rate + opts[i].val) {
            Card *card = create_card(opts[i].type, area, false, RARITY_NIL, false, false, NULL, "sho");
            create_shop_card_ui(card, opts[i].type, area);
            ShopCardCtx *c = CTX(ShopCardCtx); c->card = card;
            add_event(EVENT(.func = ev_store_modify_tags, .ctx = c));
            if ((!strcmp(opts[i].type, "Base") || !strcmp(opts[i].type, "Enhanced")) && strset_has(&G.GAME.used_vouchers, "v_illusion") &&
                pseudorandom_seed(pseudoseed("illusion")) > 0.8) {
                double edition_poll = pseudorandom_seed(pseudoseed("illusion"));
                EditionReq ed = edition_poll > 1 - 0.15 ? ED_POLY : edition_poll > 0.5 ? ED_HOLO : ED_FOIL;
                Card_set_edition(card, ed, false, false);
            }
            return card;
        }
        check_rate += opts[i].val;
    }
    return NULL;
}

EVFN(ev_shop_card_ui) {
    ShopCardCtx *c = p_; (void)e_;
    Card *card = c->card;
    if (card->opening || card->n.REMOVED) return true;
    UIDef *t1 = UIROOT(CFG(.minw = 0.6, .align = "tm", .colour = darken(G.C.BLACK, 0.2), .shadow = true, .r = 0.05, .padding = 0.05, .minh = 1),
        UIR(CFG(.align = "cm", .colour = lighten(G.C.BLACK, 0.1), .r = 0.1, .minw = 1, .minh = 0.55, .emboss = 0.05, .padding = 0.03),
            UIO(CFG(.object = &DYNATEXT(.parts = dt_parts_ref(REFN(&card->cost), localize_key("$"), NULL), .nparts = 1, DT_COL1(G.C.MONEY), .shadow = true,
                                        .silent = true, .bump = true, .pop_in = 0, .has_pop_in = true, .scale = 0.5)->n))));
    UIDef *t2;
    if (str_eq(card->ability.set, "Voucher"))
        t2 = UIROOT(CFG(.ref_table = card, .minw = 1.1, .maxw = 1.3, .padding = 0.1, .align = "bm", .colour = G.C.GREEN, .shadow = true, .r = 0.08, .minh = 0.94,
                        .func = G_FUNCS_can_redeem, .one_press = true, .button = G_FUNCS_redeem_from_shop, .hover = true),
                    UIT(CFG(.text = LOC("b_redeem"), .colour = G.C.WHITE, .scale = 0.4)));
    else if (str_eq(card->ability.set, "Booster"))
        t2 = UIROOT(CFG(.ref_table = card, .minw = 1.1, .maxw = 1.3, .padding = 0.1, .align = "bm", .colour = G.C.GREEN, .shadow = true, .r = 0.08, .minh = 0.94,
                        .func = G_FUNCS_can_open, .one_press = true, .button = G_FUNCS_open_booster, .hover = true),
                    UIT(CFG(.text = LOC("b_open"), .colour = G.C.WHITE, .scale = 0.5)));
    else
        t2 = UIROOT(CFG(.ref_table = card, .minw = 1.1, .maxw = 1.3, .padding = 0.1, .align = "bm", .colour = G.C.GOLD, .shadow = true, .r = 0.08, .minh = 0.94,
                        .func = G_FUNCS_can_buy, .one_press = true, .button = G_FUNCS_buy_from_shop, .hover = true),
                    UIT(CFG(.text = LOC("b_buy"), .colour = G.C.WHITE, .scale = 0.5)));
    UIBox *price = uibox_new(t1, UIBCFG(.align = "tm", .has_offset = true, .offset = { 0, 1.5 }, .major = &card->n, .bond = BOND_WEAK, .parent = &card->n));
    child_set(&card->n, "price", &price->n);
    UIBox *buy = uibox_new(t2, UIBCFG(.align = "bm", .has_offset = true, .offset = { 0, -0.3 }, .major = &card->n, .bond = BOND_WEAK, .parent = &card->n));
    child_set(&card->n, "buy_button", &buy->n);
    if (card->ability.consumeable) {
        UIDef *t3 = UIROOT(CFG(.id = "buy_and_use", .ref_table = card, .minh = 1.1, .padding = 0.1, .align = "cr", .colour = G.C.RED, .shadow = true, .r = 0.08,
                               .minw = 1.1, .func = G_FUNCS_can_buy_and_use, .one_press = true, .button = G_FUNCS_buy_from_shop, .hover = true,
                               .focus_args = FOCUS(.type = "none")),
            UIB(CFG(.w = 0.1, .h = 0.6)),
            UIC(CFG(.align = "cm"),
                UIR(CFG(.align = "cm", .maxw = 1), UIT(CFG(.text = LOC("b_buy"), .colour = G.C.WHITE, .scale = 0.5))),
                UIR(CFG(.align = "cm", .maxw = 1), UIT(CFG(.text = LOC("b_and_use"), .colour = G.C.WHITE, .scale = 0.3)))));
        UIBox *bau = uibox_new(t3, UIBCFG(.align = "cr", .has_offset = true, .offset = { -0.3, 0 }, .major = &card->n, .bond = BOND_WEAK, .parent = &card->n));
        child_set(&card->n, "buy_and_use_button", &bau->n);
    }
    price->n.alignment.offset.y = str_eq(card->ability.set, "Booster") ? 0.5 : 0.38;
    return true;
}

void create_shop_card_ui(Card *card, const char *type, CardArea *area) {
    (void)type; (void)area;
    ShopCardCtx *c = CTX(ShopCardCtx); c->card = card;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.43, .blocking = 0, .blockable = 0, .func = ev_shop_card_ui, .ctx = c));
}

/* ------------------------------------------------------------------ */
/* attention_text                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    AttentionArgs a;
    Col colour, cover_colour, cover_colour_l, cover_colour_d, backdrop_colour;
    UIBox *AT;
    DynaText *text;
    double start_time; bool started;
    double fade;
} AttState;

EVFN(ev_att_create) {
    AttState *s = p_; (void)e_;
    AttentionArgs *a = &s->a;
    double cw = a->cover ? a->cover->T->w : 0.001, ch = a->cover ? a->cover->T->h : 0.001;
    s->text = DYNATEXT(.scale = a->scale, DT_STR(a->text), .maxw = a->maxw, DT_COL1(s->colour), .float_ = true, .shadow = true,
                       .silent = !a->noisy, .pop_in = 0, .has_pop_in = true, .pop_in_rate = 6, .rotate = a->rotate);
    s->AT = uibox_new_T(
        UIROOT(CFG(.align = a->cover_align ? a->cover_align : "cm", .minw = cw + a->cover_padding, .minh = ch + a->cover_padding, .padding = 0.03, .r = 0.1,
                   .emboss = a->emboss, .colour = s->cover_colour),
            UIO(CFG(.draw_layer = 1, .object = &s->text->n))),
        UIBCFG(.align = a->align, .has_offset = true, .offset = a->offset, .major = a->cover ? a->cover : a->major),
        a->pos.x, a->pos.y, 0, 0);
    s->AT->attention_text = true;
    DynaText_pulse(s->text, 0.5);
    if (a->cover) {
        particles_new(a->pos.x, a->pos.y, 0, 0, (ParticlesConfig){
            .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 0.01, .pulse_max = 15, .max = 0, .has_max = true, .scale = 0.3,
            .vel_variation = 0.2, .has_vel_variation = true, .padding = 0.1, .fill = true, .lifespan = 0.5, .speed = 2.5,
            .attach = &s->AT->UIRoot->n, .colours = dt_cols(3, s->cover_colour, s->cover_colour_l, s->cover_colour_d), .ncolours = 3 });
    }
    if (s->backdrop_colour) {
        particles_new(a->pos.x, a->pos.y, 0, 0, (ParticlesConfig){
            .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 5, .scale = 2.4 * (a->backdrop_scale ? a->backdrop_scale : 1), .lifespan = 5,
            .speed = 0, .has_speed = true, .attach = &s->AT->n, .colours = dt_cols(1, s->backdrop_colour), .ncolours = 1 });
    }
    return true;
}

EVFN(ev_att_fade) {
    AttState *s = p_; (void)e_;
    if (!s->AT || !s->text) return true;
    if (!s->started) {
        s->started = true;
        s->start_time = G.TIMERS.TOTAL;
        DynaText_pop_out(s->text, 3);
    } else {
        s->fade = MAX(0, 1 - 3 * (G.TIMERS.TOTAL - s->start_time));
        if (s->cover_colour) s->cover_colour[3] = (float)MIN(s->cover_colour[3], 2 * s->fade);
        if (s->cover_colour_l) s->cover_colour_l[3] = (float)MIN(s->cover_colour_l[3], s->fade);
        if (s->cover_colour_d) s->cover_colour_d[3] = (float)MIN(s->cover_colour_d[3], s->fade);
        if (s->backdrop_colour) s->backdrop_colour[3] = (float)MIN(s->backdrop_colour[3], s->fade);
        s->colour[3] = (float)MIN(s->colour[3], s->fade);
        if (s->fade <= 0) {
            node_remove(&s->AT->n);
            return true;
        }
    }
    return false;
}

void attention_text(AttentionArgs args) {
    AttState *s = CTX(AttState);
    s->a = args;
    AttentionArgs *a = &s->a;
    if (!a->text) a->text = "test";
    a->text = intern(a->text);
    if (a->scale == 0) a->scale = 1;
    s->colour = col_copy(a->colour ? a->colour : G.C.WHITE);
    a->hold = a->hold + 0.1 * G.SPEEDFACTOR;
    if (!a->align) a->align = "cm";
    s->fade = 1;
    if (a->cover) {
        s->cover_colour = col_copy(a->cover_colour ? a->cover_colour : G.C.RED);
        s->cover_colour_l = lighten(s->cover_colour, 0.2);
        s->cover_colour_d = darken(s->cover_colour, 0.2);
    } else s->cover_colour = col_copy(G.C.CLEAR);
    if (a->backdrop_colour) s->backdrop_colour = col_copy(a->backdrop_colour);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0, .blockable = 0, .blocking = 0, .func = ev_att_create, .ctx = s, .ctx_free = nofree));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = a->hold, .blockable = 0, .blocking = 0, .func = ev_att_fade, .ctx = s));
}

/* ------------------------------------------------------------------ */
/* create_UIBox_buttons (play / discard / sort)                        */
/* ------------------------------------------------------------------ */
UIDef *create_UIBox_buttons(void) {
    double text_scale = 0.45, button_height = 1.3;
    UIDef *play_button = UIC(CFG(.id = "play_button", .align = "tm", .minw = 2.5, .padding = 0.3, .r = 0.1, .hover = true, .colour = G.C.BLUE,
                                 .button = G_FUNCS_play_cards_from_highlighted, .one_press = true, .shadow = true, .func = G_FUNCS_can_play),
        UIR(CFG(.align = "bcm", .padding = 0),
            UIT(CFG(.text = LOC("b_play_hand"), .scale = text_scale, .colour = G.C.UI.TEXT_LIGHT, .focus_args = FOCUS(.button = "x", .orientation = "bm"),
                    .func = G_FUNCS_set_button_pip))));
    UIDef *discard_button = UIC(CFG(.id = "discard_button", .align = "tm", .padding = 0.3, .r = 0.1, .minw = 2.5, .minh = button_height, .hover = true,
                                    .colour = G.C.RED, .button = G_FUNCS_discard_cards_from_highlighted_button, .one_press = true, .shadow = true,
                                    .func = G_FUNCS_can_discard),
        UIR(CFG(.align = "cm", .padding = 0),
            UIT(CFG(.text = LOC("b_discard"), .scale = text_scale, .colour = G.C.UI.TEXT_LIGHT, .focus_args = FOCUS(.button = "y", .orientation = "bm"),
                    .func = G_FUNCS_set_button_pip))));
    bool swap = G.SETTINGS.play_button_pos == 1;
    return UIROOT(CFG(.align = "cm", .minw = 1, .minh = 0.3, .padding = 0.15, .r = 0.1, .colour = G.C.CLEAR),
        swap ? discard_button : play_button,
        UIC(CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.UI.TRANSPARENT_DARK, .outline = 1.5,
                .outline_colour = mix_colours(G.C.WHITE, G.C.JOKER_GREY, 0.7), .line_emboss = 1),
            UIR(CFG(.align = "cm", .padding = 0),
                UIR(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = LOC("b_sort_hand"), .scale = text_scale * 0.8, .colour = G.C.UI.TEXT_LIGHT))),
                UIR(CFG(.align = "cm", .padding = 0.1),
                    UIC(CFG(.align = "cm", .minh = 0.7, .minw = 0.9, .padding = 0.1, .r = 0.1, .hover = true, .colour = G.C.ORANGE,
                            .button = G_FUNCS_sort_hand_value, .shadow = true),
                        UIT(CFG(.text = LOC("k_rank"), .scale = text_scale * 0.7, .colour = G.C.UI.TEXT_LIGHT))),
                    UIC(CFG(.align = "cm", .minh = 0.7, .minw = 0.9, .padding = 0.1, .r = 0.1, .hover = true, .colour = G.C.ORANGE,
                            .button = G_FUNCS_sort_hand_suit, .shadow = true),
                        UIT(CFG(.text = LOC("k_suit"), .scale = text_scale * 0.7, .colour = G.C.UI.TEXT_LIGHT)))))),
        swap ? play_button : discard_button);
}

/* ------------------------------------------------------------------ */
/* HUD                                                                 */
/* ------------------------------------------------------------------ */
UIDef *create_UIBox_HUD_blind(void) {
    double scale = 0.4;
    Sprite *stake_sprite = get_stake_sprite(G.GAME.stake ? (int)G.GAME.stake : 1, 0.5);
    Blind_change_dim(G.GAME.blind, 1.5, 1.5);
    Blind *b = G.GAME.blind;
    return UIROOT(CFG(.align = "cm", .minw = 4.5, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05, .padding = 0.05, .func = G_FUNCS_HUD_blind_visible, .id = "HUD_blind"),
        UIR(CFG(.align = "cm", .minh = 0.7, .r = 0.1, .emboss = 0.05, .colour = G.C.DYN_UI.MAIN),
            UIC(CFG(.align = "cm", .minw = 3),
                UIO(CFG(.object = &DYNATEXT(DT_REF(REFS(&b->loc_name)), DT_COL1(G.C.UI.TEXT_LIGHT), .shadow = true, .rotate = true, .silent = true,
                                            .float_ = true, .scale = 1.6 * scale, .y_offset = -4)->n, .id = "HUD_blind_name")))),
        UIR(CFG(.align = "cm", .minh = 2.74, .r = 0.1, .colour = G.C.DYN_UI.DARK),
            UIR(CFG(.align = "cm", .padding = 0.05),
                UIR(CFG(.align = "cm", .minh = 0.3, .maxw = 4.2),
                    UIT(CFG(.ref = ref_new_str(""), .scale = scale * 0.9, .colour = G.C.UI.TEXT_LIGHT, .func = G_FUNCS_HUD_blind_debuff_prefix)),
                    UIT(CFG(.ref = REFS(&b->loc_debuff_lines[0]), .scale = scale * 0.9, .colour = G.C.UI.TEXT_LIGHT, .id = "HUD_blind_debuff_1",
                            .func = G_FUNCS_HUD_blind_debuff))),
                UIR(CFG(.align = "cm", .minh = 0.3, .maxw = 4.2),
                    UIT(CFG(.ref = REFS(&b->loc_debuff_lines[1]), .scale = scale * 0.9, .colour = G.C.UI.TEXT_LIGHT, .id = "HUD_blind_debuff_2",
                            .func = G_FUNCS_HUD_blind_debuff)))),
            UIR(CFG(.align = "cm", .padding = 0.15),
                UIO(CFG(.object = &b->n, .draw_layer = 1)),
                UIC(CFG(.align = "cm", .r = 0.1, .padding = 0.05, .emboss = 0.05, .minw = 2.9, .colour = G.C.BLACK),
                    UIR(CFG(.align = "cm", .maxw = 2.8), UIT(CFG(.text = LOC("ph_blind_score_at_least"), .scale = 0.3, .colour = G.C.WHITE, .shadow = true))),
                    UIR(CFG(.align = "cm", .minh = 0.6),
                        UIO(CFG(.w = 0.5, .h = 0.5, .colour = G.C.BLUE, .object = &stake_sprite->n, .hover = true, .can_collide = -1)),
                        UIB(CFG(.h = 0.1, .w = 0.1)),
                        UIT(CFG(.ref = REFS(&b->chip_text), .scale = 0.001, .colour = G.C.RED, .shadow = true, .id = "HUD_blind_count",
                                .func = G_FUNCS_blind_chip_UI_scale))),
                    UIR(CFG(.align = "cm", .minh = 0.45, .maxw = 2.8, .func = G_FUNCS_HUD_blind_reward),
                        UIT(CFG(.text = LOC("ph_blind_reward"), .scale = 0.3, .colour = G.C.WHITE)),
                        UIO(CFG(.object = &DYNATEXT(DT_REF(REFS(&G.GAME.current_round.dollars_to_be_earned)), DT_COL1(G.C.MONEY), .shadow = true,
                                                    .rotate = true, .bump = true, .silent = true, .scale = 0.45)->n, .id = "dollars_to_be_earned")))))));
}

void add_tag(Tag *tag) {
    UIDef *tag_sprite_ui = Tag_generate_UI(tag, NAN, NULL);
    bool first = G.HUD_tags.count > 0;
    UIBox *box = uibox_new(UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR), tag_sprite_ui),
                           UIBCFG(.align = first ? "tm" : "bri", .has_offset = true, .offset = { first ? 0 : 0.7, 0 },
                                  .major = first ? &((UIBox *)vec_last(&G.HUD_tags))->n : G.ROOM_ATTACH));
    vec_push(&G.HUD_tags, box);
    discover_tag(P_TAG(tag->key));
    for (int i = 0; i < G.GAME.tags.count; i++)
        Tag_apply_to_run(G.GAME.tags.items[i], (TagContext){ .type = "tag_add", .tag = tag }, NULL, NULL);
    vec_push(&G.GAME.tags, tag);
    tag->HUD_tag = box;
}

UIDef *create_UIBox_HUD(void) {
    double scale = 0.4;
    Sprite *stake_sprite = get_stake_sprite(G.GAME.stake ? (int)G.GAME.stake : 1, 0.5);
    double spacing = 0.13;
    Col temp_col = G.C.DYN_UI.BOSS_MAIN, temp_col2 = G.C.DYN_UI.BOSS_DARK;
    void *fen = font_en();
    Language *len = lang_en();
    CurrentRound *cr = &G.GAME.current_round;

    UIDef *round = uinodes();
    uidef_add(round, UIR(CFG(.align = "cm"),
        UIC(CFG(.id = "hud_hands", .align = "cm", .padding = 0.05, .minw = 1.45, .colour = temp_col, .emboss = 0.05, .r = 0.1),
            UIR(CFG(.align = "cm", .minh = 0.33, .maxw = 1.35), UIT(CFG(.text = LOC("k_hud_hands"), .scale = 0.85 * scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIR(CFG(.align = "cm", .r = 0.1, .minw = 1.2, .colour = temp_col2),
                UIO(CFG(.object = &DYNATEXT(DT_REF(REFN(&cr->hands_left)), .font = fen, DT_COL1(G.C.BLUE), .shadow = true, .rotate = true, .scale = 2 * scale)->n,
                        .id = "hand_UI_count")))),
        UIC(CFG(.minw = spacing)),
        UIC(CFG(.align = "cm", .padding = 0.05, .minw = 1.45, .colour = temp_col, .emboss = 0.05, .r = 0.1),
            UIR(CFG(.align = "cm", .minh = 0.33, .maxw = 1.35), UIT(CFG(.text = LOC("k_hud_discards"), .scale = 0.85 * scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIR(CFG(.align = "cm"),
                UIR(CFG(.align = "cm", .r = 0.1, .minw = 1.2, .colour = temp_col2),
                    UIO(CFG(.object = &DYNATEXT(DT_REF(REFN(&cr->discards_left)), .font = fen, DT_COL1(G.C.RED), .shadow = true, .rotate = true, .scale = 2 * scale)->n,
                            .id = "discard_UI_count")))))));
    uidef_add(round, UIR(CFG(.minh = spacing)));
    uidef_add(round, UIR(CFG(.align = "cm"),
        UIC(CFG(.align = "cm", .padding = 0.05, .minw = 1.45 * 2 + spacing, .minh = 1.15, .colour = temp_col, .emboss = 0.05, .r = 0.1),
            UIR(CFG(.align = "cm"),
                UIC(CFG(.align = "cm", .r = 0.1, .minw = 1.28 * 2 + spacing, .minh = 1, .colour = temp_col2),
                    UIO(CFG(.object = &DYNATEXT(.parts = dt_parts_ref(REFN(&G.GAME.dollars), localize_key("$"), NULL), .nparts = 1, .maxw = 1.35, DT_COL1(G.C.MONEY),
                                                .font = fen, .shadow = true, .spacing = 2, .bump = true, .scale = 2.2 * scale)->n, .id = "dollar_text_UI")))))));
    uidef_add(round, UIR(CFG(.minh = spacing)));
    uidef_add(round, UIR(CFG(.align = "cm"),
        UIC(CFG(.id = "hud_ante", .align = "cm", .padding = 0.05, .minw = 1.45, .minh = 1, .colour = temp_col, .emboss = 0.05, .r = 0.1),
            UIR(CFG(.align = "cm", .minh = 0.33, .maxw = 1.35), UIT(CFG(.text = LOC("k_ante"), .scale = 0.85 * scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIR(CFG(.align = "cm", .r = 0.1, .minw = 1.2, .colour = temp_col2),
                UIO(CFG(.object = &DYNATEXT(DT_REF(REFN(&G.GAME.round_resets.ante)), DT_COL1(G.C.IMPORTANT), .shadow = true, .font = fen, .scale = 2 * scale)->n,
                        .id = "ante_UI_count")),
                UIT(CFG(.text = " ", .scale = 0.3 * scale)),
                UIT(CFG(.text = "/ ", .scale = 0.7 * scale, .colour = G.C.WHITE, .shadow = true)),
                UIT(CFG(.ref = REFN(&G.GAME.win_ante), .scale = scale, .colour = G.C.WHITE, .shadow = true)))),
        UIC(CFG(.minw = spacing)),
        UIC(CFG(.align = "cm", .padding = 0.05, .minw = 1.45, .minh = 1, .colour = temp_col, .emboss = 0.05, .r = 0.1),
            UIR(CFG(.align = "cm", .maxw = 1.35), UIT(CFG(.text = LOC("k_round"), .minh = 0.33, .scale = 0.85 * scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIR(CFG(.align = "cm", .r = 0.1, .minw = 1.2, .colour = temp_col2, .id = "row_round_text"),
                UIO(CFG(.object = &DYNATEXT(DT_REF(REFN(&G.GAME.round)), DT_COL1(G.C.IMPORTANT), .shadow = true, .scale = 2 * scale)->n, .id = "round_UI_count"))))));

    UIDef *hand = UIR(CFG(.align = "cm", .id = "hand_text_area", .colour = darken(G.C.BLACK, 0.1), .r = 0.1, .emboss = 0.05, .padding = 0.03),
        UIC(CFG(.align = "cm"),
            UIR(CFG(.align = "cm", .minh = 1.1),
                UIO(CFG(.id = "hand_name", .func = G_FUNCS_hand_text_UI_set,
                        .object = &DYNATEXT(DT_REF(REFS(&cr->current_hand.handname_text)), DT_COL1(G.C.UI.TEXT_LIGHT), .shadow = true, .float_ = true, .scale = scale * 1.4)->n)),
                UIO(CFG(.id = "hand_chip_total", .func = G_FUNCS_hand_chip_total_UI_set,
                        .object = &DYNATEXT(DT_REF(REFS(&cr->current_hand.chip_total_text)), DT_COL1(G.C.UI.TEXT_LIGHT), .shadow = true, .float_ = true, .scale = scale * 1.4)->n)),
                UIT(CFG(.ref = REFS(&cr->current_hand.hand_level), .scale = scale, .colour = G.C.UI.TEXT_LIGHT, .id = "hand_level", .shadow = true))),
            UIR(CFG(.align = "cm", .minh = 1, .padding = 0.1),
                UIC(CFG(.align = "cr", .minw = 2, .minh = 1, .r = 0.1, .colour = G.C.UI_CHIPS, .id = "hand_chip_area", .emboss = 0.05),
                    UIO(CFG(.func = G_FUNCS_flame_handler, .no_role = true, .id = "flame_chips", .object = moveable_new(0, 0, 0, 0), .w = 0, .h = 0)),
                    UIO(CFG(.id = "hand_chips", .func = G_FUNCS_hand_chip_UI_set,
                            .object = &DYNATEXT(DT_REF(REFS(&cr->current_hand.chip_text)), DT_COL1(G.C.UI.TEXT_LIGHT), .font = fen, .shadow = true, .float_ = true,
                                                .scale = scale * 2.3)->n)),
                    UIB(CFG(.w = 0.1, .h = 0.1))),
                UIC(CFG(.align = "cm"), UIT(CFG(.text = "X", .lang = len, .scale = scale * 2, .colour = G.C.UI_MULT, .shadow = true))),
                UIC(CFG(.align = "cl", .minw = 2, .minh = 1, .r = 0.1, .colour = G.C.UI_MULT, .id = "hand_mult_area", .emboss = 0.05),
                    UIO(CFG(.func = G_FUNCS_flame_handler, .no_role = true, .id = "flame_mult", .object = moveable_new(0, 0, 0, 0), .w = 0, .h = 0)),
                    UIB(CFG(.w = 0.1, .h = 0.1)),
                    UIO(CFG(.id = "hand_mult", .func = G_FUNCS_hand_mult_UI_set,
                            .object = &DYNATEXT(DT_REF(REFS(&cr->current_hand.mult_text)), DT_COL1(G.C.UI.TEXT_LIGHT), .font = fen, .shadow = true, .float_ = true,
                                                .scale = scale * 2.3)->n))))));

    UIDef *dollars_chips = UIR(CFG(.align = "cm", .r = 0.1, .padding = 0, .colour = G.C.DYN_UI.BOSS_MAIN, .emboss = 0.05, .id = "row_dollars_chips"),
        UIC(CFG(.align = "cm", .padding = 0.1),
            UIC(CFG(.align = "cm", .minw = 1.3),
                UIR(CFG(.align = "cm", .padding = 0, .maxw = 1.3), UIT(CFG(.text = LOC("k_round"), .scale = 0.42, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
                UIR(CFG(.align = "cm", .padding = 0, .maxw = 1.3), UIT(CFG(.text = LOC("k_lower_score"), .scale = 0.42, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)))),
            UIC(CFG(.align = "cm", .minw = 3.3, .minh = 0.7, .r = 0.1, .colour = G.C.DYN_UI.BOSS_DARK),
                UIO(CFG(.w = 0.5, .h = 0.5, .object = &stake_sprite->n, .hover = true, .can_collide = -1)),
                UIB(CFG(.w = 0.1, .h = 0.1)),
                UIT(CFG(.ref = REFS(&G.GAME.chips_text), .lang = len, .scale = 0.85, .colour = G.C.WHITE, .id = "chip_UI_count", .func = G_FUNCS_chip_UI_set,
                        .shadow = true)))));

    UIDef *buttons = UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.CLEAR, .shadow = true, .id = "button_area", .padding = 0.2),
        UIR(CFG(.id = "run_info_button", .align = "cm", .minh = 1.75, .minw = 1.5, .padding = 0.05, .r = 0.1, .hover = true, .colour = G.C.RED,
                .button = G_FUNCS_run_info, .shadow = true),
            UIR(CFG(.align = "cm", .padding = 0, .maxw = 1.4), UIT(CFG(.text = LOC("b_run_info_1"), .scale = 1.2 * scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true))),
            UIR(CFG(.align = "cm", .padding = 0, .maxw = 1.4),
                UIT(CFG(.text = LOC("b_run_info_2"), .scale = 1 * scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true,
                        .focus_args = FOCUS(.button = G.F_GUIDE ? "guide" : "back", .orientation = "bm"), .func = G_FUNCS_set_button_pip)))),
        UIR(CFG(.align = "cm", .minh = 1.75, .minw = 1.5, .padding = 0.05, .r = 0.1, .hover = true, .colour = G.C.ORANGE, .button = G_FUNCS_options, .shadow = true),
            UIC(CFG(.align = "cm", .maxw = 1.4, .focus_args = FOCUS(.button = "start", .orientation = "bm"), .func = G_FUNCS_set_button_pip),
                UIT(CFG(.text = LOC("b_options"), .scale = scale, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)))));

    return UIROOT(CFG(.align = "cm", .padding = 0.03, .colour = G.C.UI.TRANSPARENT_DARK),
        UIR(CFG(.align = "cm", .padding = 0.05, .colour = G.C.DYN_UI.MAIN, .r = 0.1),
            UIR(CFG(.align = "cm", .colour = G.C.DYN_UI.BOSS_DARK, .r = 0.1, .minh = 30, .padding = 0.08),
                UIR(CFG(.align = "cm", .minh = 0.3)),
                UIR(CFG(.align = "cm", .id = "row_blind", .minw = 1, .minh = 3.75)),
                dollars_chips,
                hand,
                UIR(CFG(.align = "cm", .id = "row_round"),
                    UIC(CFG(.align = "cm"), buttons),
                    uidef_add_all(UIC(CFG(.align = "cm")), round)))));
}

/* ------------------------------------------------------------------ */
/* blind select                                                        */
/* ------------------------------------------------------------------ */
EVFN(ev_prompt_drop) { (void)e_; (void)p_; if (G.blind_prompt_box) G.blind_prompt_box->n.alignment.offset.y = 0; return true; }

static bool bs_is(int i, const char *s) { return str_eq(G.GAME.round_resets.blind_states[i], s); }

UIDef *create_UIBox_blind_select(void) {
    bool reroll = strset_has(&G.GAME.used_vouchers, "v_retcon") || strset_has(&G.GAME.used_vouchers, "v_directors_cut");
    G.blind_prompt_box = uibox_new(
        UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR, .padding = 0.2),
            UIR(CFG(.align = "cm"), UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("ph_choose_blind_1")), DT_COL1(G.C.WHITE), .shadow = true, .bump = true,
                                                                .scale = 0.6, .pop_in = 0.5, .has_pop_in = true, .maxw = 5)->n, .id = "prompt_dynatext1"))),
            UIR(CFG(.align = "cm"), UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("ph_choose_blind_2")), DT_COL1(G.C.WHITE), .shadow = true, .bump = true,
                                                                .scale = 0.7, .pop_in = 0.5, .has_pop_in = true, .maxw = 5, .silent = true)->n, .id = "prompt_dynatext2"))),
            UIIF(reroll, UIBUTTON(.label = { localize_key("b_reroll_boss"), tfmt("%s10", localize_key("$")) }, .nlabel = 2,
                                  .button = G_FUNCS_reroll_boss, .func = G_FUNCS_reroll_boss_button))),
        UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, -15 }, .major = &UIBox_get_UIE_by_ID(G.HUD, "row_blind", NULL)->n, .bond = BOND_WEAK));
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_prompt_drop));

    double width = G.hand->n.T->w;
#define DONE(i) (bs_is(i, "Defeated") || bs_is(i, "Skipped") || bs_is(i, "Hide"))
    G.GAME.blind_on_deck = !DONE(BT_SMALL) ? "Small" : !DONE(BT_BIG) ? "Big" : "Boss";
#undef DONE
    for (int i = 0; i < 3; i++) {
        G.blind_select_opts[i] = NULL;
        if (bs_is(i, "Hide")) continue;
        const char *t = BLIND_TYPE_NAMES[i];
        UIDef *inner = uinodes();
        uidef_add(inner, create_UIBox_blind_choice(t, false));
        Col main = get_blind_main_colour(t);
        UIDef *dc = i == BT_BOSS ? UIBox_dyn_container(inner, false, main, mix_colours(G.C.BLACK, main, 0.8), false, 0)
                                 : UIBox_dyn_container(inner, false, main, NULL, false, 0);
        G.blind_select_opts[i] = uibox_new(UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR), dc),
                                           UIBCFG(.align = "bmi", .has_offset = true, .offset = { 0, 0 }));
    }
    return UIROOT(CFG(.align = "tm", .minw = width, .r = 0.15, .colour = G.C.CLEAR),
        UIR(CFG(.align = "cm", .padding = 0.5),
            UIIF(G.blind_select_opts[0], UIO(CFG(.align = "cm", .object = G.blind_select_opts[0] ? &G.blind_select_opts[0]->n : NULL))),
            UIIF(G.blind_select_opts[1], UIO(CFG(.align = "cm", .object = G.blind_select_opts[1] ? &G.blind_select_opts[1]->n : NULL))),
            UIIF(G.blind_select_opts[2], UIO(CFG(.align = "cm", .object = G.blind_select_opts[2] ? &G.blind_select_opts[2]->n : NULL)))));
}

UIDef *create_UIBox_blind_tag(int blind_choice, bool run_info) {
    if (blind_choice < 0 || blind_choice > 1 || !G.GAME.round_resets.blind_tags[blind_choice]) return NULL;
    Tag *tag = tag_new(G.GAME.round_resets.blind_tags[blind_choice], false, BLIND_TYPE_NAMES[blind_choice]);
    Sprite *tag_sprite = NULL;
    UIDef *tag_ui = Tag_generate_UI(tag, NAN, &tag_sprite);
    tag_sprite->n.states.collide.can = run_info;
    bool disabled = false;
    return UIR(CFG(.id = "tag_container", .ref_table = tag, .align = "cm"),
        UIR(CFG(.align = "tm", .minh = 0.65),
            UIT(CFG(.text = LOC("k_or"), .scale = 0.55, .colour = disabled ? G.C.UI.TEXT_INACTIVE : G.C.WHITE, .shadow = !disabled))),
        UIR(CFG(.id = intern(tfmt("tag_%s", BLIND_TYPE_NAMES[blind_choice])), .align = "cm", .r = 0.1, .padding = 0.1, .minw = 1, .can_collide = 1, .ref_table = tag_sprite),
            UIC(CFG(.id = "tag_desc", .align = "cm", .minh = 1), tag_ui),
            !run_info
                ? UIC(CFG(.align = "cm", .colour = G.C.UI.BACKGROUND_INACTIVE, .minh = 0.6, .minw = 2, .maxw = 2, .padding = 0.07, .r = 0.1, .shadow = true,
                          .hover = true, .one_press = true, .button = G_FUNCS_skip_blind, .func = G_FUNCS_hover_tag_proxy, .ref_table = tag),
                      UIT(CFG(.text = LOC("b_skip_blind"), .scale = 0.4, .colour = G.C.UI.TEXT_INACTIVE)))
                : UIC(CFG(.align = "cm", .padding = 0.1, .emboss = 0.05, .colour = mix_colours(G.C.BLUE, G.C.BLACK, 0.4), .r = 0.1, .maxw = 2),
                      UIT(CFG(.text = LOC("b_skip_reward"), .scale = 0.35, .colour = G.C.WHITE)))));
}

static void ensure_orbital_choices(int ante) {
    if (ante < 0) ante = 0;
    if (ante >= G.GAME.n_orbital_choices) {
        int n = ante + 8;
        G.GAME.orbital_choices = xrealloc(G.GAME.orbital_choices, (size_t)n * sizeof *G.GAME.orbital_choices);
        memset(&G.GAME.orbital_choices[G.GAME.n_orbital_choices], 0, (size_t)(n - G.GAME.n_orbital_choices) * sizeof *G.GAME.orbital_choices);
        G.GAME.n_orbital_choices = n;
    }
}

UIDef *create_UIBox_blind_choice(const char *type, bool run_info) {
    if (!G.GAME.blind_on_deck) G.GAME.blind_on_deck = "Small";
    if (!run_info) G.GAME.round_resets.blind_states[blind_type_index(G.GAME.blind_on_deck)] = "Select";
    bool disabled = false;
    if (!type) type = "Small";
    int bt = blind_type_index(type);
    BlindProto *bc = P_BLIND(G.GAME.round_resets.blind_choices[bt]);
    Sprite *animation = animated_sprite_new(0, 0, 1.4, 1.4, strmap_get(&G.ANIMATION_ATLAS, "blind_chips"), bc->pos);
    DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
    Sprite_define_draw_steps(animation, steps, 2);
    UIDef *extras = NULL;
    Sprite *stake_sprite = get_stake_sprite(G.GAME.stake ? (int)G.GAME.stake : 1, 0.5);

    int ante = (int)G.GAME.round_resets.ante;
    ensure_orbital_choices(ante);
    if (!G.GAME.orbital_choices[ante].hands[bt]) {
        const char *_poker_hands[HAND_COUNT]; int n = 0;
        for (int i = 0; i < HAND_COUNT; i++) {
            int hi = hand_index(DATA_HANDS_PAIRS_ORDER[i]);
            if (hi >= 0 && G.GAME.hands[hi].visible) _poker_hands[n++] = DATA_HANDS_PAIRS_ORDER[i];
        }
        int idx = pseudorandom_index(n, pseudoseed("orbital"), true);
        G.GAME.orbital_choices[ante].hands[bt] = idx > 0 ? _poker_hands[idx - 1] : NULL;
        G.GAME.orbital_choices[ante].set = true;
    }

    if (bt == BT_SMALL || bt == BT_BIG) extras = create_UIBox_blind_tag(bt, run_info);
    else if (!run_info) {
        DynaTextPart *p1 = dt_parts_str(localize_key("ph_up_ante_1")); p1->colour = G.C.FILTER;
        DynaTextPart *p2 = dt_parts_str(localize_key("ph_up_ante_2")); p2->colour = G.C.WHITE;
        DynaTextPart *p3 = dt_parts_str(localize_key("ph_up_ante_3")); p3->colour = G.C.WHITE;
        DynaText *dt1 = DYNATEXT(.parts = p1, .nparts = 1, DT_COL1(G.C.BLACK), .scale = 0.55, .silent = true, .pop_delay = 4.5, .has_pop_delay = true,
                                 .shadow = true, .bump = true, .maxw = 3);
        DynaText *dt2 = DYNATEXT(.parts = p2, .nparts = 1, DT_COL1(G.C.CHANCE), .scale = 0.35, .silent = true, .pop_delay = 4.5, .has_pop_delay = true,
                                 .shadow = true, .maxw = 3);
        DynaText *dt3 = DYNATEXT(.parts = p3, .nparts = 1, DT_COL1(G.C.CHANCE), .scale = 0.35, .silent = true, .pop_delay = 4.5, .has_pop_delay = true,
                                 .shadow = true, .maxw = 3);
        extras = UIR(CFG(.align = "cm"),
            UIR(CFG(.align = "cm", .padding = 0.07, .r = 0.1, .colour = col_new(0, 0, 0, 0.12f), .minw = 2.9),
                UIR(CFG(.align = "cm"), UIO(CFG(.object = &dt1->n))),
                UIR(CFG(.align = "cm"), UIO(CFG(.object = &dt2->n))),
                UIR(CFG(.align = "cm"), UIO(CFG(.object = &dt3->n)))));
    }
    if (G.GAME.round_resets.blind_ante == 0) G.GAME.round_resets.blind_ante = G.GAME.round_resets.ante;
    char *text_table[4] = { 0 };
    int ntext = localize_raw_descriptions("Blind", bc->key, LV(localize_cat(G.GAME.current_round.most_played_poker_hand, "poker_hands")), text_table, 4);
    const char *loc_name = localize_name_text("Blind", bc->key);
    Col blind_col = get_blind_main_colour(type);
    double blind_amt = get_blind_amount(G.GAME.round_resets.blind_ante) * bc->mult * G.GAME.starting_params.ante_scaling;
    const char *blind_state = G.GAME.round_resets.blind_states[bt];
    bool reward = !(G.GAME.modifiers.has_no_blind_reward && G.GAME.modifiers.no_blind_reward[bt]);
    if (str_eq(blind_state, "Select")) blind_state = "Current";
    Col run_info_colour = NULL;
    if (run_info) run_info_colour = str_eq(blind_state, "Defeated") ? G.C.GREY : str_eq(blind_state, "Skipped") ? G.C.BLUE :
                                    str_eq(blind_state, "Upcoming") ? G.C.ORANGE : str_eq(blind_state, "Current") ? G.C.RED : G.C.GOLD;
    Col txt = disabled ? G.C.UI.TEXT_INACTIVE : G.C.WHITE;
    StrBuf dollars; sb_init(&dollars);
    for (int i = 0; i < (int)bc->dollars; i++) sb_add(&dollars, localize_key("$"));
    sb_add(&dollars, "+");

    UIDef *button = !run_info
        ? UIR(CFG(.id = "select_blind_button", .align = "cm", .ref_table = bc, .colour = disabled ? G.C.UI.BACKGROUND_INACTIVE : G.C.ORANGE, .minh = 0.6,
                  .minw = 2.7, .padding = 0.07, .r = 0.1, .shadow = true, .hover = true, .one_press = true, .button = G_FUNCS_select_blind),
              UIT(CFG(.ref = REFS(&G.GAME.round_resets.loc_blind_states[bt]), .scale = 0.45, .colour = disabled ? G.C.UI.TEXT_INACTIVE : G.C.UI.TEXT_LIGHT,
                      .shadow = !disabled)))
        : UIR(CFG(.id = "select_blind_button", .align = "cm", .ref_table = bc, .colour = run_info_colour, .minh = 0.6, .minw = 2.7, .padding = 0.07, .r = 0.1,
                  .emboss = 0.08),
              UIT(CFG(.text = (char *)localize_cat(blind_state, "blind_states"), .scale = 0.45, .colour = G.C.UI.TEXT_LIGHT, .shadow = true)));

    UIDef *desc_text = NULL;
    if (ntext > 0) {
        desc_text = UIR(CFG(.align = "cm", .minh = 0.7, .padding = 0.05, .minw = 2.9),
            UIR(CFG(.align = "cm", .maxw = 2.8),
                UIT(CFG(.id = bc->key, .ref = ref_new_str(""), .scale = 0.32, .colour = txt, .shadow = !disabled, .func = G_FUNCS_HUD_blind_debuff_prefix)),
                UIT(CFG(.text = text_table[0] ? text_table[0] : "-", .scale = 0.32, .colour = txt, .shadow = !disabled))),
            UIIF(ntext > 1, UIR(CFG(.align = "cm", .maxw = 2.8),
                UIT(CFG(.text = ntext > 1 && text_table[1] ? text_table[1] : "-", .scale = 0.32, .colour = txt, .shadow = !disabled)))));
    }

    UIConfig *root_cfg = CFG(.id = type, .align = "tm", .func = G_FUNCS_blind_choice_handler, .minh = !run_info ? 10 : 0, .r = 0.1, .padding = 0.05);
    root_cfg->num = run_info ? 1 : 0;   /* ref_table.run_info */
    UIDef *t = UIR(root_cfg,
        UIR(CFG(.align = "cm", .colour = mix_colours(G.C.BLACK, G.C.L_BLACK, 0.5), .r = 0.1, .outline = 1, .outline_colour = G.C.L_BLACK),
            UIR(CFG(.align = "cm", .padding = 0.2), button),
            UIR(CFG(.id = "blind_name", .align = "cm", .padding = 0.07),
                UIR(CFG(.align = "cm", .r = 0.1, .outline = 1, .outline_colour = blind_col, .colour = darken(blind_col, 0.3), .minw = 2.9, .emboss = 0.1,
                        .padding = 0.07, .line_emboss = 1),
                    UIO(CFG(.object = &DYNATEXT(DT_STR(loc_name), DT_COL1(txt), .shadow = !disabled, .float_ = !disabled, .y_offset = -4, .scale = 0.45,
                                                .maxw = 2.8)->n)))),
            UIR(CFG(.align = "cm", .padding = 0.05),
                UIR(CFG(.id = "blind_desc", .align = "cm", .padding = 0.05),
                    UIR(CFG(.align = "cm"),
                        UIR(CFG(.align = "cm", .minh = 1.5), UIO(CFG(.object = &animation->n))),
                        desc_text ? desc_text : UISKIP),
                    UIR(CFG(.align = "cm", .r = 0.1, .padding = 0.05, .minw = 3.1, .colour = G.C.BLACK, .emboss = 0.05),
                        UIR(CFG(.align = "cm", .maxw = 3), UIT(CFG(.text = LOC("ph_blind_score_at_least"), .scale = 0.3, .colour = txt, .shadow = !disabled))),
                        UIR(CFG(.align = "cm", .minh = 0.6),
                            UIO(CFG(.w = 0.5, .h = 0.5, .colour = G.C.BLUE, .object = &stake_sprite->n, .hover = true, .can_collide = -1)),
                            UIB(CFG(.h = 0.1, .w = 0.1)),
                            UIT(CFG(.text = (char *)number_format(blind_amt), .scale = score_number_scale(0.9, blind_amt),
                                    .colour = disabled ? G.C.UI.TEXT_INACTIVE : G.C.RED, .shadow = !disabled))),
                        UIIF(reward, UIR(CFG(.align = "cm"),
                            UIT(CFG(.text = LOC("ph_blind_reward"), .scale = 0.35, .colour = txt, .shadow = !disabled)),
                            UIT(CFG(.text = dollars.buf, .scale = 0.35, .colour = disabled ? G.C.UI.TEXT_INACTIVE : G.C.MONEY, .shadow = !disabled)))))))),
        UIR(CFG(.id = "blind_extras", .align = "cm"), extras ? extras : UISKIP));
    sb_free(&dollars);
    for (int i = 0; i < ntext; i++) free(text_table[i]);
    return t;
}

UIDef *create_UIBox_round_evaluation(void) {
    double width = G.hand->n.T->w - 2;
    UIDef *inner = uinodes();
    uidef_add(inner, UIR(CFG(.align = "tm", .minw = width, .minh = 3, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05),
        UIR(CFG(.align = "cm", .minw = width, .minh = 1.4)),
        UIR(CFG(.align = "cm", .minw = width, .id = "base_round_eval")),
        UIR(CFG(.align = "cm", .minw = width, .id = "bonus_round_eval"))));
    uidef_add(inner, UIR(CFG(.align = "cm", .minh = 0.05)));
    uidef_add(inner, UIR(CFG(.align = "cm", .minw = width, .id = "eval_bottom")));
    return UIROOT(CFG(.align = "tm", .colour = G.C.CLEAR), UIBox_dyn_container(inner, false, NULL, NULL, false, 0));
}

/* ------------------------------------------------------------------ */
/* booster packs                                                       */
/* ------------------------------------------------------------------ */
static UIDef *pack_ui(const char *title_key, double width_mult, double width_add) {
    double size = G.GAME.pack_size;
    G.pack_cards = cardarea_new(G.ROOM->T->x + 9 + G.hand->n.T->x, G.hand->n.T->y, size * G.CARD_W * width_mult + width_add, 1.05 * G.CARD_H,
                                CA_ARGS(.card_limit = size, .has_card_limit = true, .type = "consumeable", .highlight_limit = 1, .has_highlight_limit = true));
    G.pack_cards->name = "pack_cards";
    UIDef *inner = uinodes();
    uidef_add(inner, UIC(CFG(.align = "cm", .padding = 0.05, .minw = 4),
        UIR(CFG(.align = "bm", .padding = 0.05),
            UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key(title_key)), DT_COL1(G.C.WHITE), .shadow = true, .rotate = true, .bump = true, .spacing = 2,
                                        .scale = 0.7, .maxw = 4, .pop_in = 0.5, .has_pop_in = true)->n))),
        UIR(CFG(.align = "bm", .padding = 0.05),
            UIO(CFG(.object = &DYNATEXT(DT_STR(tfmt("%s ", localize_key("k_choose"))), DT_COL1(G.C.WHITE), .shadow = true, .rotate = true, .bump = true,
                                        .spacing = 2, .scale = 0.5, .pop_in = 0.7, .has_pop_in = true)->n)),
            UIO(CFG(.object = &DYNATEXT(DT_REF(REFN(&G.GAME.pack_choices)), DT_COL1(G.C.WHITE), .shadow = true, .rotate = true, .bump = true, .spacing = 2,
                                        .scale = 0.5, .pop_in = 0.7, .has_pop_in = true)->n)))));
    return UIROOT(CFG(.align = "tm", .r = 0.15, .colour = G.C.CLEAR, .padding = 0.15),
        UIR(CFG(.align = "cl", .colour = G.C.CLEAR, .r = 0.15, .padding = 0.1, .minh = 2, .shadow = true),
            UIR(CFG(.align = "cm"),
                UIC(CFG(.align = "cm", .padding = 0.1),
                    UIC(CFG(.align = "cm", .r = 0.2, .colour = G.C.CLEAR, .shadow = true), UIO(CFG(.object = &G.pack_cards->n))))),
            UIR(CFG(.align = "cm")),
            UIR(CFG(.align = "tm"),
                UIC(CFG(.align = "tm", .padding = 0.05, .minw = 2.4)),
                UIC(CFG(.align = "tm", .padding = 0.05), UIBox_dyn_container(inner, false, NULL, NULL, false, 0)),
                UIC(CFG(.align = "tm", .padding = 0.05, .minw = 2.4),
                    UIR(CFG(.minh = 0.2)),
                    UIR(CFG(.align = "tm", .padding = 0.2, .minh = 1.2, .minw = 1.8, .r = 0.15, .colour = G.C.GREY, .one_press = true,
                            .button = G_FUNCS_skip_booster, .hover = true, .shadow = true, .func = G_FUNCS_can_skip_booster),
                        UIT(CFG(.text = LOC("b_skip"), .scale = 0.5, .colour = G.C.WHITE, .shadow = true, .focus_args = FOCUS(.button = "y", .orientation = "bm"),
                                .func = G_FUNCS_set_button_pip)))))));
}

UIDef *create_UIBox_arcana_pack(void) { return pack_ui("k_arcana_pack", 1, 0); }
UIDef *create_UIBox_spectral_pack(void) { return pack_ui("k_spectral_pack", 1, 0); }
UIDef *create_UIBox_standard_pack(void) { return pack_ui("k_standard_pack", 1.1, 0); }
UIDef *create_UIBox_buffoon_pack(void) { return pack_ui("k_buffoon_pack", 1.1, 0); }
UIDef *create_UIBox_celestial_pack(void) { return pack_ui("k_celestial_pack", 1.1, 0.5); }
