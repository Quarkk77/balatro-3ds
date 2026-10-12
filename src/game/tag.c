/* tag.c - port of tag.lua */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define EVCTX(T) T *c = (T *)p_; (void)e_
typedef struct { Tag *tag; int lock; Card *card; const char *key; Tag *other; char *orbital; } TagCtx;
static void nofree(void *p) { (void)p; }

static void lock_set(int id, bool v) {
    const char *k = tfmt("tag%d", id);
    if (v) strset_add(&G.CONTROLLER.locks.other, k);
    else strset_del(&G.CONTROLLER.locks.other, k);
}

Tag *tag_new(const char *key, bool for_collection, const char *blind_type) {
    Tag *t = NEW(Tag);
    t->key = intern(key);
    TagProto *proto = P_TAG(key);
    if (!proto) proto = &G.tag_undiscovered;
    t->config = proto->config;
    t->pos = proto->pos;
    t->name = proto->name;
    t->tally = G.GAME.tag_tally;
    t->triggered = false;
    t->ID = G.tagid;
    G.tagid++;
    t->ability.orbital_hand = xstrdup(tfmt("[%s]", localize_key("k_poker_hand")));
    t->ability.blind_type = blind_type ? intern(blind_type) : NULL;
    G.GAME.tag_tally = G.GAME.tag_tally + 1;
    if (!for_collection) Tag_set_ability(t);
    return t;
}

EVFN(ev_nope1) {
    EVCTX(TagCtx);
    ATT(.text = "NOPE", .colour = G.C.WHITE, .scale = 0.7, .hold = 0.3 / G.SETTINGS.GAMESPEED, .cover = c->tag->HUD_tag ? &c->tag->HUD_tag->n : NULL,
        .cover_colour = G.C.BLACK, .align = "cm");
    sound_play("cancel", 1.4, 0.5);
    return true;
}
EVFN(ev_nope2) {
    EVCTX(TagCtx);
    if (c->tag->HUD_tag) c->tag->HUD_tag->n.states.visible = false;
    sound_play("cancel", 1.26, 0.5);
    return true;
}
EVFN(ev_tag_remove) {
    EVCTX(TagCtx);
    Tag_remove(c->tag);
    return true;
}

void Tag_nope(Tag *t) {
    TagCtx *c1 = CTX(TagCtx); c1->tag = t;
    add_event(EVENT(.delay = 0.2, .trigger = TRIG_AFTER, .func = ev_nope1, .ctx = c1));
    TagCtx *c2 = CTX(TagCtx); c2->tag = t;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_nope2, .ctx = c2));
    TagCtx *c3 = CTX(TagCtx); c3->tag = t;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.5, .func = ev_tag_remove, .ctx = c3));
}

typedef struct { Tag *tag; const char *message; Col colour; } YepCtx;
EVFN(ev_yep1) {
    EVCTX(YepCtx);
    ATT(.text = c->message, .colour = G.C.WHITE, .scale = 1, .hold = 0.3 / G.SETTINGS.GAMESPEED,
        .cover = c->tag->HUD_tag ? &c->tag->HUD_tag->n : NULL, .cover_colour = c->colour ? c->colour : G.C.GREEN, .align = "cm");
    sound_play("generic1", 0.9 + math_random() * 0.1, 0.8);
    sound_play("holo1", 1.2 + math_random() * 0.1, 0.4);
    return true;
}
EVFN(ev_yep2) {
    EVCTX(TagCtx);
    if (c->tag->HUD_tag) c->tag->HUD_tag->n.states.visible = false;
    return true;
}

static void Tag_yep(Tag *t, const char *message, Col colour, EventFn func, void *ctx) {
    stop_use();
    YepCtx *y = CTX(YepCtx); y->tag = t; y->message = message; y->colour = colour;
    add_event(EVENT(.delay = 0.4, .trigger = TRIG_AFTER, .func = ev_yep1, .ctx = y));
    TagCtx *c2 = CTX(TagCtx); c2->tag = t;
    add_event(EVENT(.func = ev_yep2, .ctx = c2));
    add_event(EVENT(.func = func, .ctx = ctx));
    TagCtx *c3 = CTX(TagCtx); c3->tag = t;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.7, .func = ev_tag_remove, .ctx = c3));
}

void Tag_set_ability(Tag *t) {
    if (str_eq(t->name, "Orbital Tag")) {
        if (G.orbital_hand) str_set(&t->ability.orbital_hand, G.orbital_hand);
        else if (t->ability.blind_type) {
            int ante = (int)G.GAME.round_resets.ante;
            int bt = blind_type_index(t->ability.blind_type);
            if (G.GAME.orbital_choices && ante >= 0 && ante < G.GAME.n_orbital_choices && bt >= 0 && G.GAME.orbital_choices[ante].hands[bt])
                str_set(&t->ability.orbital_hand, G.GAME.orbital_choices[ante].hands[bt]);
        }
    }
}

EVFN(ev_true) { (void)e_; (void)p_; return true; }
EVFN(ev_unlock_only) { EVCTX(TagCtx); lock_set(c->lock, false); return true; }
EVFN(ev_topup) {
    EVCTX(TagCtx);
    for (int i = 1; i <= (int)c->tag->config.spawn_jokers; i++) {
        if (G.jokers && G.jokers->cards.count < G.jokers->config.card_limit) {
            Card *card = create_card("Joker", G.jokers, false, 0, false, false, NULL, "top");
            Card_add_to_deck(card, false);
            CardArea_emplace(G.jokers, card, NULL, false);
        }
    }
    lock_set(c->lock, false);
    return true;
}
EVFN(ev_economy) {
    EVCTX(TagCtx);
    ease_dollars(MIN(c->tag->config.max, MAX(0, G.GAME.dollars)), true);
    return true;
}
EVFN(ev_pack_tag) {
    EVCTX(TagCtx);
    Card *card = card_new(G.play->n.T->x + G.play->n.T->w / 2 - G.CARD_W * 1.27 / 2, G.play->n.T->y + G.play->n.T->h / 2 - G.CARD_H * 1.27 / 2,
                          G.CARD_W * 1.27, G.CARD_H * 1.27, P_CARD("empty"), P_CENTER(c->key),
                          &(CardParams){ .bypass_discovery_center = true, .bypass_discovery_ui = true });
    card->cost = 0;
    card->from_tag = true;
    UIConfig cfg = { .ref_table = card };
    UIElement fake = { .config = &cfg };
    G_FUNCS_use_card(&fake);
    Card_start_materialize(card, NULL, 0, false, NAN);
    lock_set(c->lock, false);
    return true;
}
EVFN(ev_boss_unlock2) { EVCTX(TagCtx); lock_set(c->lock, false); return true; }
EVFN(ev_boss_unlock1) {
    EVCTX(TagCtx);
    TagCtx *n = CTX(TagCtx); *n = *c;
    add_event(EVENT(.func = ev_boss_unlock2, .ctx = n));
    return true;
}
EVFN(ev_boss_tag) {
    EVCTX(TagCtx);
    G.from_boss_tag = true;
    G_FUNCS_reroll_boss(NULL);
    TagCtx *n = CTX(TagCtx); *n = *c;
    add_event(EVENT(.func = ev_boss_unlock1, .ctx = n));
    return true;
}
EVFN(ev_voucher_tag) {
    (void)e_; (void)p_;
    const char *vk = get_next_voucher_key(true);
    strset_add(&G.ARGS_voucher_tag, vk);
    G.shop_vouchers->config.card_limit += 1;
    Card *card = card_new(G.shop_vouchers->n.T->x + G.shop_vouchers->n.T->w / 2, G.shop_vouchers->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"),
                          P_CENTER(vk), &(CardParams){ .bypass_discovery_center = true, .bypass_discovery_ui = true });
    create_shop_card_ui(card, "Voucher", G.shop_vouchers);
    Card_start_materialize(card, NULL, 0, false, NAN);
    CardArea_emplace(G.shop_vouchers, card, NULL, false);
    strset_clear(&G.ARGS_voucher_tag);
    return true;
}
EVFN(ev_double_tag) {
    EVCTX(TagCtx);
    if (c->orbital) str_set(&G.orbital_hand, c->orbital);
    add_tag(tag_new(c->key, false, NULL));
    free(G.orbital_hand);
    G.orbital_hand = NULL;
    lock_set(c->lock, false);
    return true;
}
EVFN(ev_store_create) {
    EVCTX(TagCtx);
    Card_start_materialize(c->card, NULL, 0, false, NAN);
    c->card->ability.couponed = true;
    Card_set_cost(c->card);
    return true;
}
EVFN(ev_d6) {
    (void)e_; (void)p_;
    G.GAME.round_resets.temp_reroll_cost = 0;
    G.GAME.round_resets.has_temp_reroll_cost = true;
    calculate_reroll_cost(true);
    return true;
}
typedef struct { Card *card; int lock; EditionReq ed; } ModCtx;
EVFN(ev_store_modify) {
    EVCTX(ModCtx);
    c->card->temp_edition = false;
    Card_set_edition(c->card, c->ed, true, false);
    c->card->ability.couponed = true;
    Card_set_cost(c->card);
    lock_set(c->lock, false);
    return true;
}
EVFN(ev_coupon) {
    (void)e_; (void)p_;
    if (G.shop_jokers && G.shop_booster) {
        for (int i = 0; i < G.shop_jokers->cards.count; i++) { Card *v = G.shop_jokers->cards.items[i]; v->ability.couponed = true; Card_set_cost(v); }
        for (int i = 0; i < G.shop_booster->cards.count; i++) { Card *v = G.shop_booster->cards.items[i]; v->ability.couponed = true; Card_set_cost(v); }
    }
    return true;
}

int Tag_apply_to_run(Tag *t, TagContext ctx, Card **out_card, TagEvalResult *out_eval) {
    if (out_card) *out_card = NULL;
    if (out_eval) out_eval->valid = false;
    if (t->triggered || !str_eq(t->config.type, ctx.type)) return 0;
    const char *n = t->name;
    if (!strcmp(ctx.type, "eval")) {
        if (str_eq(n, "Investment Tag") && G.GAME.last_blind.present && G.GAME.last_blind.boss) {
            Tag_yep(t, "+", G.C.GOLD, ev_true, NULL);
            t->triggered = true;
            if (out_eval) {
                out_eval->valid = true;
                out_eval->dollars = t->config.dollars;
                out_eval->condition = localize_key("ph_defeat_the_boss");
                out_eval->pos = t->pos;
                out_eval->tag = t;
            }
            return 1;
        }
    } else if (!strcmp(ctx.type, "immediate")) {
        int lock = t->ID;
        lock_set(lock, true);
        TagCtx *c = CTX(TagCtx); c->tag = t; c->lock = lock;
        if (str_eq(n, "Top-up Tag")) { Tag_yep(t, "+", G.C.PURPLE, ev_topup, c); t->triggered = true; return 1; }
        if (str_eq(n, "Skip Tag")) {
            Tag_yep(t, "+", G.C.MONEY, ev_unlock_only, c);
            ease_dollars(G.GAME.skips * t->config.skip_bonus, false);
            t->triggered = true; return 1;
        }
        if (str_eq(n, "Garbage Tag")) {
            Tag_yep(t, "+", G.C.MONEY, ev_unlock_only, c);
            ease_dollars(G.GAME.unused_discards * t->config.dollars_per_discard, false);
            t->triggered = true; return 1;
        }
        if (str_eq(n, "Handy Tag")) {
            Tag_yep(t, "+", G.C.MONEY, ev_unlock_only, c);
            ease_dollars(G.GAME.hands_played * t->config.dollars_per_hand, false);
            t->triggered = true; return 1;
        }
        if (str_eq(n, "Economy Tag")) {
            Tag_yep(t, "+", G.C.MONEY, ev_unlock_only, c);
            TagCtx *e = CTX(TagCtx); e->tag = t;
            add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_economy, .ctx = e));
            t->triggered = true; return 1;
        }
        if (str_eq(n, "Orbital Tag")) {
            int hi = hand_index(t->ability.orbital_hand);
            if (hi >= 0) {
                HandStat *h = &G.GAME.hands[hi];
                update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 0.8, .has_pitch = true, .delay = 0.3, .has_delay = true),
                                 HTV(.handname = t->ability.orbital_hand, .has_handname = true, .chips = h->chips, .has_chips = true,
                                     .mult = h->mult, .has_mult = true, .level_n = h->level, .level_is_num = true, .has_level = true));
                level_up_hand(NULL, t->ability.orbital_hand, false, t->config.levels);
                update_hand_text(HTC(.sound = "button", .volume = 0.7, .has_volume = true, .pitch = 1.1, .has_pitch = true, .delay = 0, .has_delay = true),
                                 HTV(.mult = 0, .has_mult = true, .chips = 0, .has_chips = true, .handname = "", .has_handname = true, .level = "", .has_level = true));
            }
            Tag_yep(t, "+", G.C.MONEY, ev_unlock_only, c);
            t->triggered = true; return 1;
        }
        free(c);
    } else if (!strcmp(ctx.type, "new_blind_choice")) {
        int lock = t->ID;
        lock_set(lock, true);
        TagCtx *c = CTX(TagCtx); c->tag = t; c->lock = lock;
        if (str_eq(n, "Charm Tag")) {
            c->key = intern(tfmt("p_arcana_mega_%d", math_random_range(1, 2)));
            Tag_yep(t, "+", G.C.PURPLE, ev_pack_tag, c);
            t->triggered = true; return 1;
        }
        if (str_eq(n, "Meteor Tag")) {
            c->key = intern(tfmt("p_celestial_mega_%d", math_random_range(1, 2)));
            Tag_yep(t, "+", G.C.SECONDARY_SET.Planet, ev_pack_tag, c);
            t->triggered = true; return 1;
        }
        if (str_eq(n, "Ethereal Tag")) { c->key = "p_spectral_normal_1"; Tag_yep(t, "+", G.C.SECONDARY_SET.Spectral, ev_pack_tag, c); t->triggered = true; return 1; }
        if (str_eq(n, "Standard Tag")) { c->key = "p_standard_mega_1"; Tag_yep(t, "+", G.C.SECONDARY_SET.Spectral, ev_pack_tag, c); t->triggered = true; return 1; }
        if (str_eq(n, "Buffoon Tag")) { c->key = "p_buffoon_mega_1"; Tag_yep(t, "+", G.C.SECONDARY_SET.Spectral, ev_pack_tag, c); t->triggered = true; return 1; }
        if (str_eq(n, "Boss Tag")) {
            lock_set(lock, true);
            Tag_yep(t, "+", G.C.GREEN, ev_boss_tag, c);
            t->triggered = true; return 1;
        }
        free(c);
    } else if (!strcmp(ctx.type, "voucher_add")) {
        if (str_eq(n, "Voucher Tag")) {
            Tag_yep(t, "+", G.C.SECONDARY_SET.Voucher, ev_voucher_tag, NULL);
            t->triggered = true;
        }
    } else if (!strcmp(ctx.type, "tag_add")) {
        if (str_eq(n, "Double Tag") && ctx.tag && !str_eq(ctx.tag->key, "tag_double")) {
            int lock = t->ID;
            lock_set(lock, true);
            TagCtx *c = CTX(TagCtx); c->tag = t; c->lock = lock; c->key = ctx.tag->key;
            if (ctx.tag->ability.orbital_hand) c->orbital = xstrdup(ctx.tag->ability.orbital_hand);
            Tag_yep(t, "+", G.C.BLUE, ev_double_tag, c);
            t->triggered = true;
        }
    } else if (!strcmp(ctx.type, "round_start_bonus")) {
        if (str_eq(n, "Juggle Tag")) {
            Tag_yep(t, "+", G.C.BLUE, ev_true, NULL);
            CardArea_change_size(G.hand, t->config.h_size);
            G.GAME.round_resets.temp_handsize += t->config.h_size;
            G.GAME.round_resets.has_temp_handsize = true;
            t->triggered = true;
            return 1;
        }
    } else if (!strcmp(ctx.type, "store_joker_create")) {
        Card *card = NULL;
        if (str_eq(n, "Rare Tag")) {
            int rares = 0;
            StrSet seen; strset_init(&seen);
            for (int i = 0; i < G.jokers->cards.count; i++) {
                Card *v = G.jokers->cards.items[i];
                if (v->config.center->rarity == 3 && !strset_has(&seen, v->config.center->key)) { rares++; strset_add(&seen, v->config.center->key); }
            }
            strset_free(&seen);
            if (G.P_JOKER_RARITY_POOLS[2].count > rares) {
                card = create_card("Joker", ctx.area, false, 1, false, false, NULL, "rta");
                create_shop_card_ui(card, "Joker", ctx.area);
                card->n.states.visible = false;
                TagCtx *c = CTX(TagCtx); c->card = card;
                Tag_yep(t, "+", G.C.RED, ev_store_create, c);
            } else Tag_nope(t);
            t->triggered = true;
        } else if (str_eq(n, "Uncommon Tag")) {
            card = create_card("Joker", ctx.area, false, 0.9, false, false, NULL, "uta");
            create_shop_card_ui(card, "Joker", ctx.area);
            card->n.states.visible = false;
            TagCtx *c = CTX(TagCtx); c->card = card;
            Tag_yep(t, "+", G.C.GREEN, ev_store_create, c);
        }
        t->triggered = true;
        if (out_card) *out_card = card;
        return card != NULL;
    } else if (!strcmp(ctx.type, "shop_start")) {
        if (str_eq(n, "D6 Tag") && !G.GAME.shop_d6ed) {
            G.GAME.shop_d6ed = true;
            Tag_yep(t, "+", G.C.GREEN, ev_d6, NULL);
            t->triggered = true;
            return 1;
        }
    } else if (!strcmp(ctx.type, "store_joker_modify")) {
        bool applied = false;
        Card *cd = ctx.card;
        if (cd && !cd->edition && !cd->temp_edition && str_eq(cd->ability.set, "Joker")) {
            int lock = t->ID;
            lock_set(lock, true);
            EditionReq ed = ED_NONE;
            if (str_eq(n, "Foil Tag")) ed = ED_FOIL;
            else if (str_eq(n, "Holographic Tag")) ed = ED_HOLO;
            else if (str_eq(n, "Polychrome Tag")) ed = ED_POLY;
            else if (str_eq(n, "Negative Tag")) ed = ED_NEG;
            if (ed.any) {
                cd->temp_edition = true;
                ModCtx *m = CTX(ModCtx); m->card = cd; m->lock = lock; m->ed = ed;
                Tag_yep(t, "+", G.C.DARK_EDITION, ev_store_modify, m);
                applied = true;
            }
            t->triggered = true;
        }
        return applied;
    } else if (!strcmp(ctx.type, "shop_final_pass")) {
        if (str_eq(n, "Coupon Tag") && G.shop && !G.GAME.shop_free) {
            G.GAME.shop_free = true;
            Tag_yep(t, "+", G.C.GREEN, ev_coupon, NULL);
            t->triggered = true;
            return 1;
        }
    }
    (void)nofree;
    return 0;
}

DVal Tag_save(Tag *t) {
    DVal d = dv_table();
    dv_set_str(&d, "key", t->key);
    dv_set_num(&d, "tally", t->tally);
    DVal ab = dv_table();
    if (t->ability.orbital_hand) dv_set_str(&ab, "orbital_hand", t->ability.orbital_hand);
    if (t->ability.blind_type) dv_set_str(&ab, "blind_type", t->ability.blind_type);
    dv_set(&d, "ability", ab);
    return d;
}

void Tag_load(Tag *t, DVal *d) {
    t->key = intern(dv_get_str(d, "key", "tag_uncommon"));
    TagProto *proto = P_TAG(t->key);
    if (!proto) proto = &G.tag_undiscovered;
    t->config = proto->config;
    t->pos = proto->pos;
    t->name = proto->name;
    t->tally = dv_get_num(d, "tally", 0);
    DVal *ab = dv_get(d, "ability");
    const char *oh = dv_get_str(ab, "orbital_hand", NULL);
    if (oh) str_set(&t->ability.orbital_hand, oh);
    const char *bt = dv_get_str(ab, "blind_type", NULL);
    t->ability.blind_type = bt ? intern(bt) : NULL;
    G.GAME.tag_tally = MAX(t->tally, G.GAME.tag_tally) + 1;
}

void Tag_juice_up(Tag *t, double s, double r) {
    if (t->tag_sprite) node_juice_up(&t->tag_sprite->n, s, r);
}

static void tag_sprite_hover(Node *n) {
    Sprite *s = (Sprite *)n;
    Tag *t = n->cfg_tag;
    if (!G.CONTROLLER.dragging.target) {
        if (!((Sprite *)n)->float_anim) {}
        Card dummy;
        (void)dummy;
        if (!n->attention_text && n->states.visible) {
            /* hovering flag stored in attention_text (unused for sprites) */
            n->attention_text = true;
            if (t && s == t->tag_sprite) {
                n->hover_tilt = 3;
                node_juice_up(n, 0.05, 0.02);
                sound_play("paper1", math_random() * 0.1 + 0.55, 0.42);
                sound_play("tarot2", math_random() * 0.1 + 0.55, 0.09);
            }
            if (t) Tag_get_uibox_table(t, s);
            n->h_popup = UIDEF_card_h_popup(n, n->ability_UIBox_table);
            n->h_popup_config = UIBCFG(.align = "cl", .has_offset = true, .offset = { -0.1, 0 }, .parent = n);
            Node_hover(n);
            Node *alert = child_get(n, "alert");
            if (alert) {
                child_set(n, "alert", NULL);
                node_remove(alert);
                if (t && t->key && P_TAG(t->key)) P_TAG(t->key)->alerted = true;
                game_save_progress();
            }
        }
    }
}
static void tag_sprite_stop_hover(Node *n) {
    n->attention_text = false;
    Node_stop_hover(n);
    n->hover_tilt = 0;
}

UIDef *Tag_generate_UI(Tag *t, double size, Sprite **out_sprite) {
    if (isnan(size) || size == 0) size = 0.8;
    Sprite *ts = sprite_new(0, 0, size, size, atlas_get("tags"), t->hide_ability ? G.tag_undiscovered.pos : t->pos);
    ts->n.T->scale = 1;
    UIDef *tab = UIC(CFG(.align = "cm", .ref_table = t, .group = intern(tfmt("%g", t->tally))),
                     UIO(CFG(.w = size, .h = size, .colour = G.C.BLUE, .object = &ts->n, .focus_with_object = true)));
    DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
    Sprite_define_draw_steps(ts, steps, 2);
    ts->float_anim = true;
    ts->n.states.hover.can = true;
    ts->n.states.drag.can = false;
    ts->n.states.collide.can = true;
    ts->n.cfg_force_focus = true;
    ts->n.cfg_tag = t;
    ts->n.o_hover = tag_sprite_hover;
    ts->n.o_stop_hover = tag_sprite_stop_hover;
    node_juice_up(&ts->n, NAN, NAN);
    t->tag_sprite = ts;
    if (out_sprite) *out_sprite = ts;
    return tab;
}

Sprite *Tag_get_uibox_table(Tag *t, Sprite *ts) {
    if (!ts) ts = t->tag_sprite;
    const char *n = t->name;
    LocVars *lv = loc_vars_new();
    if (str_eq(n, "Investment Tag")) lv = LV(num_str(t->config.dollars));
    else if (str_eq(n, "Handy Tag")) lv = LV(num_str(t->config.dollars_per_hand), num_str(t->config.dollars_per_hand * G.GAME.hands_played));
    else if (str_eq(n, "Garbage Tag")) lv = LV(num_str(t->config.dollars_per_discard), num_str(t->config.dollars_per_discard * G.GAME.unused_discards));
    else if (str_eq(n, "Juggle Tag")) lv = LV(num_str(t->config.h_size));
    else if (str_eq(n, "Top-up Tag")) lv = LV(num_str(t->config.spawn_jokers));
    else if (str_eq(n, "Skip Tag")) lv = LV(num_str(t->config.skip_bonus), num_str(t->config.skip_bonus * (G.GAME.skips + 1)));
    else if (str_eq(n, "Orbital Tag")) {
        const char *ph = tfmt("[%s]", localize_key("k_poker_hand"));
        lv = LV(str_eq(t->ability.orbital_hand, ph) ? t->ability.orbital_hand : localize_cat(t->ability.orbital_hand, "poker_hands"), num_str(t->config.levels));
    } else if (str_eq(n, "Economy Tag")) lv = LV(num_str(t->config.max));
    lv->present = true;
    ts->n.ability_UIBox_table = generate_card_ui(P_TAG(t->key), 2, NULL, lv, t->hide_ability ? "Undiscovered" : "Tag", NULL, t->hide_ability, NULL, NULL);
    return ts;
}

void Tag_remove_from_game(Tag *t) {
    ptrvec_remove(&G.GAME.tags, t);
}

void Tag_remove(Tag *t) {
    if (t->removed) return;
    t->removed = true;
    Tag_remove_from_game(t);
    int key = -1;
    for (int i = 0; i < G.HUD_tags.count; i++) if (G.HUD_tags.items[i] == t->HUD_tag) key = i;
    if (key >= 0) {
        if (key + 1 < G.HUD_tags.count) {
            UIBox *next = G.HUD_tags.items[key + 1];
            if (key == 0) {
                Moveable_set_alignment(&next->n, ALIGN_ARGS(.type = "bri", .has_offset = true, .offset = { 0.7, 0 }, .bond = BOND_WEAK, .major = G.ROOM_ATTACH));
            } else {
                Moveable_set_role(&next->n, ROLE_ARGS(.xy_bond = BOND_WEAK, .major = &((UIBox *)G.HUD_tags.items[key - 1])->n));
            }
        }
        vec_remove_at(&G.HUD_tags, key);
    }
    if (t->HUD_tag) node_remove(&t->HUD_tag->n);
}
