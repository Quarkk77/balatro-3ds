/* blind.c - port of blind.lua */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define EVCTX(T) T *c = (T *)p_; (void)e_
typedef struct { Blind *b; int i; double a; bool reset, silent; BlindProto *bp; } BCtx;

Blind *blind_new(double x, double y, double w, double h) {
    Blind *b = NEW(Blind);
    moveable_init(&b->n, &BLIND_VT, x, y, w, h);
    b->n.tilt_var.set = true;
    b->n.tilt_var.mx = b->n.tilt_var.my = b->n.tilt_var.amt = 0;
    b->n.ambient_tilt = 0.3;
    b->chips = 0;
    b->n.zoom = true;
    b->n.states.collide.can = true;
    b->colour = col_copy(G.C.BLACK);
    b->dark_colour = darken(b->colour, 0.2);
    b->animatedSprite = animated_sprite_new(b->n.T->x, b->n.T->y, b->n.T->w, b->n.T->h, strmap_get(&G.ANIMATION_ATLAS, "blind_chips"), P_BLIND("bl_small")->pos);
    child_set(&b->n, "animatedSprite", &b->animatedSprite->n);
    /* animatedSprite.states = self.states (shared); kept in sync in Blind_move */
    b->n.states.visible = false;
    b->n.states.drag.can = true;
    b->animatedSprite->n.states = b->n.states;
    b->n.states.collide.can = true;
    b->n.states.drag.can = true;
    b->loc_debuff_lines[0] = xstrdup("");
    b->loc_debuff_lines[1] = xstrdup("");
    b->n.shadow_height = 0;
    b->name = "";
    vec_push(&G.I.CARD, &b->n);
    return b;
}

static void sync_states(Blind *b) { if (b->animatedSprite) b->animatedSprite->n.states = b->n.states; }

void Blind_change_colour(Blind *b, Col blind_col) {
    if (!blind_col) blind_col = get_blind_main_colour(b->config.blind && b->config.blind->key ? b->config.blind->key : "");
    Col dark_col = mix_colours(blind_col, G.C.BLACK, 0.4);
    ease_colour(G.C.DYN_UI.MAIN, blind_col, NAN);
    ease_colour(G.C.DYN_UI.DARK, dark_col, NAN);
    if (!b->boss && b->name) {
        blind_col = darken(G.C.BLACK, 0.05);
        dark_col = lighten(G.C.BLACK, 0.07);
    } else dark_col = mix_colours(blind_col, G.C.BLACK, 0.2);
    ease_colour(G.C.DYN_UI.BOSS_MAIN, blind_col, NAN);
    ease_colour(G.C.DYN_UI.BOSS_DARK, dark_col, NAN);
}

void Blind_set_text(Blind *b) {
    if (!b->config.blind) return;
    if (b->disabled) {
        str_set(&b->loc_name, !b->name[0] ? b->name : localize_name_text("Blind", b->config.blind->key));
        str_set(&b->loc_debuff_text, "");
        str_set(&b->loc_debuff_lines[0], "");
        str_set(&b->loc_debuff_lines[1], "");
    } else {
        LocVars *lv = NULL;
        if (str_eq(b->name, "The Ox")) lv = LV(localize_cat(G.GAME.current_round.most_played_poker_hand, "poker_hands"));
        else {
            lv = loc_vars_new();
            for (int i = 0; i < b->config.blind->nvars; i++) {
                const char *v = b->config.blind->vars[i];
                if (v && !strncmp(v, "@loc:", 5)) loc_vars_add(lv, localize_key(v + 5));
                else loc_vars_add(lv, v);
            }
        }
        char *lines[8];
        int n = b->config.blind->key ? localize_raw_descriptions("Blind", b->config.blind->key, lv, lines, 8) : 0;
        if (n > 0 || (b->config.blind->key && dv_path(&G.localization, tfmt("descriptions.Blind.%s", b->config.blind->key)))) {
            str_set(&b->loc_name, !b->name[0] ? b->name : localize_name_text("Blind", b->config.blind->key));
            StrBuf sb; sb_init(&sb);
            for (int k = 0; k < n; k++) {
                sb_add(&sb, lines[k]);
                sb_add(&sb, " ");
            }
            free(b->loc_debuff_text);
            b->loc_debuff_text = sb_take(&sb);
            str_set(&b->loc_debuff_lines[0], n > 0 ? lines[0] : "");
            str_set(&b->loc_debuff_lines[1], n > 1 ? lines[1] : "");
            for (int k = 0; k < n; k++) free(lines[k]);
        } else {
            str_set(&b->loc_name, "");
            str_set(&b->loc_debuff_text, "");
            str_set(&b->loc_debuff_lines[0], "");
            str_set(&b->loc_debuff_lines[1], "");
        }
    }
}

EVFN(ev_blind_show2) {
    EVCTX(BCtx);
    Blind *b = c->b;
    UIElement *nm = UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_name", NULL);
    UIElement *de = UIBox_get_UIE_by_ID(G.HUD_blind, "dollars_to_be_earned", NULL);
    if (nm) nm->n.states.visible = true;
    if (de && de->parent_uie && de->parent_uie->parent_uie) de->parent_uie->parent_uie->n.states.visible = true;
    if (de && de->config->object) DynaText_pop_in((DynaText *)de->config->object, 0);
    if (nm && nm->config->object) DynaText_pop_in((DynaText *)nm->config->object, 0);
    UIElement *cnt = UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_count", NULL);
    if (cnt) node_juice_up(&cnt->n, NAN, NAN);
    if (b->config.blind) AnimatedSprite_set_sprite_pos(b->animatedSprite, b->config.blind->pos);
    b->blind_set = true;
    G.ROOM_jiggle += 3;
    if (!c->reset && !c->silent) {
        Blind_juice_up(b, NAN, NAN);
        if (c->bp) {
            sound_play("chips1", math_random() * 0.1 + 0.55, 0.42);
            sound_play("gold_seal", math_random() * 0.1 + 1.85, 0.26);
        }
    }
    return true;
}
EVFN(ev_blind_show1) {
    EVCTX(BCtx);
    UIElement *nm = UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_name", NULL);
    UIElement *de = UIBox_get_UIE_by_ID(G.HUD_blind, "dollars_to_be_earned", NULL);
    if (nm) nm->n.states.visible = false;
    if (de && de->parent_uie && de->parent_uie->parent_uie) de->parent_uie->parent_uie->n.states.visible = false;
    G.HUD_blind->n.alignment.offset.y = 0;
    BCtx *n = CTX(BCtx); *n = *c;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.15, .blockable = 0, .func = ev_blind_show2, .ctx = n));
    return true;
}
EVFN(ev_acorn_shuffle) {
    EVCTX(BCtx);
    CardArea_shuffle(G.jokers, "aajk");
    sound_play("cardSlide1", c->a, NAN);
    return true;
}
EVFN(ev_acorn) {
    (void)e_; (void)p_;
    double pitches[3] = { 0.85, 1.15, 1 };
    double delays[3] = { 0.15, 0.15, 0.5 };
    for (int i = 0; i < 3; i++) {
        BCtx *n = CTX(BCtx); n->a = pitches[i];
        add_event(EVENT(.func = ev_acorn_shuffle, .ctx = n));
        delay(delays[i]);
    }
    return true;
}

void Blind_set_blind(Blind *b, BlindProto *blind, bool reset, bool silent) {
    if (!reset) {
        static BlindProto empty;
        b->config.blind = blind ? blind : &empty;
        b->name = blind ? blind->name : "";
        b->dollars = blind ? blind->dollars : 0;
        b->sound_pings = b->dollars + 2;
        int bt = blind_type_index(Blind_get_type(b));
        if (G.GAME.modifiers.has_no_blind_reward && bt >= 0 && G.GAME.modifiers.no_blind_reward[bt]) b->dollars = 0;
        if (blind) b->debuff = blind->debuff; else memset(&b->debuff, 0, sizeof b->debuff);
        b->pos = blind ? blind->pos : SPOS(0, 0);
        b->mult = blind ? blind->mult : 0;
        b->disabled = false;
        b->has_discards_sub = false;
        b->has_hands_sub = false;
        b->boss = blind && blind->has_boss;
        b->blind_set = false;
        b->triggered = 0;
        b->prepped = true;
        Blind_set_text(b);
        G.GAME.last_blind.present = true;
        G.GAME.last_blind.boss = b->boss;
        G.GAME.last_blind.name = b->name;
        if (blind && blind->name) Blind_change_colour(b, NULL);
        else Blind_change_colour(b, G.C.BLACK);
        b->chips = get_blind_amount(G.GAME.round_resets.ante) * b->mult * G.GAME.starting_params.ante_scaling;
        str_set(&b->chip_text, number_format(b->chips));
        if (!blind) b->chips = 0;
        if (b->dollars > 0) {
            StrBuf sb; sb_init(&sb);
            for (int i = 0; i < (int)b->dollars; i++) sb_add(&sb, localize_key("$"));
            char *s = sb_take(&sb);
            str_set(&G.GAME.current_round.dollars_to_be_earned, s);
            free(s);
        } else str_set(&G.GAME.current_round.dollars_to_be_earned, "");
        G.HUD_blind->n.alignment.offset.y = -10;
        UIBox_recalculate(G.HUD_blind);
        if (blind && blind->name && blind->name[0]) {
            Blind_alert_debuff(b, true);
            BCtx *c = CTX(BCtx);
            c->b = b; c->reset = reset; c->silent = silent; c->bp = blind;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.05, .blockable = 0, .func = ev_blind_show1, .ctx = c));
        }
        b->n.h_popup_config = UIBCFG(.align = "tm", .has_offset = true, .offset = { 0, -0.1 }, .parent = &b->n);
    }
    if (str_eq(b->name, "The Eye") && !reset) {
        memset(b->hands, 0, sizeof b->hands);
        b->has_hands = true;
    }
    if (str_eq(b->name, "The Mouth") && !reset) { b->only_hand = NULL; b->only_hand_false = true; }
    if (str_eq(b->name, "The Fish") && !reset) b->prepped = false;
    if (str_eq(b->name, "The Water") && !reset) {
        b->discards_sub = G.GAME.current_round.discards_left;
        b->has_discards_sub = true;
        ease_discard(-b->discards_sub, false, false);
    }
    if (str_eq(b->name, "The Needle") && !reset) {
        b->hands_sub = G.GAME.round_resets.hands - 1;
        b->has_hands_sub = true;
        ease_hands_played(-b->hands_sub, false);
    }
    if (str_eq(b->name, "The Manacle") && !reset) CardArea_change_size(G.hand, -1);
    if (str_eq(b->name, "Amber Acorn") && !reset && G.jokers->cards.count > 0) {
        CardArea_unhighlight_all(G.jokers);
        for (int i = 0; i < G.jokers->cards.count; i++) Card_flip(G.jokers->cards.items[i]);
        if (G.jokers->cards.count > 1) add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_acorn));
    }
    for (int i = 0; i < G.playing_cards.count; i++) Blind_debuff_card(b, G.playing_cards.items[i], false);
    for (int i = 0; i < G.jokers->cards.count; i++) if (!reset) Blind_debuff_card(b, G.jokers->cards.items[i], true);
    double spin = 0;
    if (b->config.blind && b->config.blind->has_boss) spin = b->config.blind->boss.showdown ? 0.5 : 0.25;
    G.ARGS_spin.real = (G.SETTINGS.reduced_motion ? 0 : 1) * spin;
}

typedef struct { Blind *b; } AlertCtx;
EVFN(ev_alert_cancel) {
    EVCTX(BCtx);
    if (c->i == 1) Blind_juice_up(c->b, NAN, NAN);
    sound_play("cancel", 0.7 + 0.05 * c->i, 0.7);
    return true;
}
EVFN(ev_alert_unblock) {
    EVCTX(BCtx);
    c->b->block_play = false;
    if (G.buttons) {
        UIElement *pb = UIBox_get_UIE_by_ID(G.buttons, "play_button", NULL);
        if (pb) pb->disable_button = false;
    }
    return true;
}
EVFN(ev_alert_show) {
    EVCTX(BCtx);
    Blind *b = c->b;
    sound_play("whoosh1", 0.55, 0.62);
    for (int i = 1; i <= 4; i++) {
        double wait = 0.1 * (i - 1);
        BCtx *n = CTX(BCtx); n->b = b; n->i = i;
        add_event(EVENT(.blockable = 0, .trigger = TRIG_AFTER, .delay = G.SETTINGS.GAMESPEED * wait, .func = ev_alert_cancel, .ctx = n));
    }
    double hold = G.SETTINGS.GAMESPEED * (strlen(b->loc_debuff_text ? b->loc_debuff_text : "") * 0.035 + 1.3);
    const char *disp = Blind_get_loc_debuff_text(b);
    ATT(.scale = 0.7, .text = disp, .maxw = 12, .hold = hold, .align = "cm", .has_offset = true, .offset = { 0, -1 }, .major = &G.play->n);
    BCtx *n = CTX(BCtx); n->b = b;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1, .blocking = 0, .blockable = 0, .func = ev_alert_unblock, .ctx = n));
    return true;
}
EVFN(ev_alert_wait) {
    EVCTX(BCtx);
    Blind *b = c->b;
    if (b->disabled) { b->block_play = false; return true; }
    if (G.STATE == STATE_SELECTING_HAND) {
        BCtx *n = CTX(BCtx); n->b = b;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = G.SETTINGS.GAMESPEED * 0.05, .blockable = 0, .func = ev_alert_show, .ctx = n));
        return true;
    }
    return false;
}

void Blind_alert_debuff(Blind *b, bool first) {
    (void)first;
    if (b->loc_debuff_text && b->loc_debuff_text[0]) {
        b->block_play = true;
        BCtx *c = CTX(BCtx); c->b = b;
        add_event(EVENT(.blockable = 0, .blocking = 0, .func = ev_alert_wait, .ctx = c));
    }
}

const char *Blind_get_loc_debuff_text(Blind *b) {
    const char *pre = (b->config.blind && str_eq(b->config.blind->name, "The Wheel")) ? num_str(G.GAME.probabilities.normal) : "";
    const char *d = tfmt("%s%s", pre, b->loc_debuff_text ? b->loc_debuff_text : "");
    if (b->config.blind && str_eq(b->config.blind->name, "The Mouth") && b->only_hand)
        d = tfmt("%s [%s]", d, localize_cat(b->only_hand, "poker_hands"));
    return d;
}

EVFN(ev_defeat_stop) {
    EVCTX(BCtx);
    Particles *p = (Particles *)child_get(&c->b->n, "particles");
    if (p) p->max = 0;
    return true;
}
EVFN(ev_defeat_ping) {
    EVCTX(BCtx);
    sound_play("cancel", 0.8 - 0.05 * c->i, 1.7);
    if (c->i == (int)MIN(c->a + 1, 6)) sound_play("whoosh2", 0.7, 0.42);
    return true;
}
EVFN(ev_defeat_hud) { (void)e_; (void)p_; G.HUD_blind->n.alignment.offset.y = -10; return true; }
EVFN(ev_defeat_reset) {
    EVCTX(BCtx);
    c->b->n.dissolve = 0;
    Blind_set_blind(c->b, NULL, false, true);
    return true;
}

void Blind_defeat(Blind *b, bool silent) {
    double dt = 1.3;
    double extra_time = 0;
    b->n.dissolve = 0;
    b->n.n_dissolve_colours = 2;
    b->n.dissolve_colours[0] = G.C.BLACK;
    b->n.dissolve_colours[1] = G.C.RED;
    Blind_juice_up(b, NAN, NAN);
    Particles *p = PARTICLES(0, 0, 0, 0, .timer_type = TIMER_TOTAL, .has_timer_type = true, .timer = 0.01 * dt, .scale = 0.1, .speed = 1.5,
                             .lifespan = 0.7 * dt, .attach = &b->n, .colours = dt_cols(2, G.C.BLACK, G.C.RED), .ncolours = 2, .fill = true);
    child_set(&b->n, "particles", &p->n);
    UIElement *nm = UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_name", NULL);
    if (nm && nm->config->object) DynaText_pop_out((DynaText *)nm->config->object, 2);
    BCtx *c1 = CTX(BCtx); c1->b = b;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.5 * dt, .func = ev_defeat_stop, .ctx = c1));
    if (!silent) {
        double pings = b->sound_pings != 0 ? b->sound_pings : 3;
        for (int i = 1; i <= (int)MIN(pings, 7); i++) {
            extra_time += (0.4 + 0.15 * i) * dt;
            BCtx *n = CTX(BCtx); n->b = b; n->i = i; n->a = pings;
            add_event(EVENT(.blockable = 0, .trigger = TRIG_AFTER, .delay = (0.15 - 0.01 * pings) * i * dt, .func = ev_defeat_ping, .ctx = n));
        }
    }
    add_event(EVENT(.trigger = TRIG_EASE, .blockable = 0, .ref = REFN(&b->n.dissolve), .ease_to = 1, .delay = 0.7 * dt));
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.8 * dt, .func = ev_defeat_hud));
    BCtx *c2 = CTX(BCtx); c2->b = b;
    add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = 0.95 * dt, .func = ev_defeat_reset, .ctx = c2));
    for (int i = 0; i < G.jokers->cards.count; i++) {
        Card *v = G.jokers->cards.items[i];
        if (v->facing == FACE_BACK) Card_flip(v);
    }
    if (str_eq(b->name, "The Manacle") && !b->disabled) CardArea_change_size(G.hand, 1);
}

const char *Blind_get_type(Blind *b) {
    if (!b || !b->name) return NULL;
    if (!strcmp(b->name, "Small Blind")) return "Small";
    if (!strcmp(b->name, "Big Blind")) return "Big";
    if (b->name[0]) return "Boss";
    return NULL;
}

EVFN(ev_disable_check) {
    (void)e_; (void)p_;
    if (G.GAME.blind->boss && G.GAME.chips - G.GAME.blind->chips >= 0) {
        G.STATE = STATE_NEW_ROUND;
        G.STATE_COMPLETE = false;
    }
    return true;
}

void Blind_disable(Blind *b) {
    b->disabled = true;
    for (int i = 0; i < G.jokers->cards.count; i++) if (G.jokers->cards.items[i]->facing == FACE_BACK) Card_flip(G.jokers->cards.items[i]);
    const char *n = b->name;
    if (str_eq(n, "The Water")) ease_discard(b->discards_sub, false, false);
    if (str_eq(n, "The Wheel") || str_eq(n, "The House") || str_eq(n, "The Mark") || str_eq(n, "The Fish")) {
        for (int i = 0; i < G.hand->cards.count; i++) if (G.hand->cards.items[i]->facing == FACE_BACK) Card_flip(G.hand->cards.items[i]);
        for (int i = 0; i < G.playing_cards.count; i++) ((Card *)G.playing_cards.items[i])->ability.wheel_flipped = false;
    }
    if (str_eq(n, "The Needle")) ease_hands_played(b->hands_sub, false);
    if (str_eq(n, "The Wall")) { b->chips = b->chips / 2; str_set(&b->chip_text, number_format(b->chips)); }
    if (str_eq(n, "Cerulean Bell")) for (int i = 0; i < G.playing_cards.count; i++) ((Card *)G.playing_cards.items[i])->ability.forced_selection = false;
    if (str_eq(n, "The Manacle")) {
        CardArea_change_size(G.hand, 1);
        G_FUNCS_draw_from_deck_to_hand(1);
    }
    if (str_eq(n, "Violet Vessel")) { b->chips = b->chips / 3; str_set(&b->chip_text, number_format(b->chips)); }
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_disable_check));
    for (int i = 0; i < G.playing_cards.count; i++) Blind_debuff_card(b, G.playing_cards.items[i], false);
    for (int i = 0; i < G.jokers->cards.count; i++) Blind_debuff_card(b, G.jokers->cards.items[i], false);
    Blind_set_text(b);
    Blind_wiggle(b);
}

EVFN(ev_tarot2) { (void)e_; (void)p_; sound_play("tarot2", 0.76, 0.4); return true; }
void Blind_wiggle(Blind *b) {
    node_juice_up(&b->animatedSprite->n, 0.3, NAN);
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.06 * G.SETTINGS.GAMESPEED, .blockable = 0, .blocking = 0, .func = ev_tarot2));
    sound_play("tarot2", 1, 0.4);
}

void Blind_juice_up(Blind *b, double a, double c) {
    node_juice_up(&b->animatedSprite->n, isnan(a) ? 0.2 : a, isnan(c) ? 0.2 : c);
}
static void Blind_juice_v(Node *n, double a, double c) { Blind_juice_up((Blind *)n, a, c); }

static void Blind_hover(Node *n) {
    Blind *b = (Blind *)n;
    if (!G.CONTROLLER.dragging.target) {
        if (!b->hovering && n->states.visible && b->animatedSprite->n.states.visible) {
            b->hovering = true;
            n->hover_tilt = 2;
            node_juice_up(&b->animatedSprite->n, 0.05, 0.02);
            sound_play("chips1", math_random() * 0.1 + 0.55, 0.12);
            Node_hover(n);
        }
    }
}
static void Blind_stop_hover(Node *n) {
    Blind *b = (Blind *)n;
    b->hovering = false;
    n->hover_tilt = 0;
    Node_stop_hover(n);
}

static void Blind_draw(Node *n) {
    Blind *b = (Blind *)n;
    if (!n->states.visible) return;
    n->tilt_var.set = true;
    n->tilt_var.mx = G.CONTROLLER.cursor_position.x;
    n->tilt_var.my = G.CONTROLLER.cursor_position.y;
    sync_states(b);
    b->animatedSprite->n.role.draw_major = n;
    Sprite_draw_shader_simple(b->animatedSprite, "dissolve", 0.1, true);
    Sprite_draw_shader_simple(b->animatedSprite, "dissolve", 0, false);
    for (int i = 0; i < n->children.count; i++) {
        const char *k = n->children.items[i].name;
        if (k && !strcmp(k, "animatedSprite")) continue;
        Node *v = n->children.items[i].node;
        v->VT.scale = n->VT.scale;
        node_draw(v);
    }
    add_to_drawhash(n);
}

typedef struct { CardList cards; } HookCtx;
EVFN(ev_hook) {
    EVCTX(HookCtx);
    (void)c;
    bool any = false;
    CardList _cards = { 0 };
    for (int i = 0; i < G.hand->cards.count; i++) vec_push(&_cards, G.hand->cards.items[i]);
    for (int i = 1; i <= 2; i++) {
        if (i <= G.hand->cards.count) {
            /* pseudorandom_element(_cards): sorted by sort_id; returns the card and its key (index in the original array) */
            CardList sorted = { 0 };
            for (int k = 0; k < _cards.count; k++) vec_push(&sorted, _cards.items[k]);
            for (int a = 1; a < sorted.count; a++) {
                Card *kk = sorted.items[a]; int j = a - 1;
                while (j >= 0 && sorted.items[j]->sort_id > kk->sort_id) { sorted.items[j + 1] = sorted.items[j]; j--; }
                sorted.items[j + 1] = kk;
            }
            int idx = pseudorandom_index(sorted.count, pseudoseed("hook"), true);
            if (idx < 1) { vec_free(&sorted); break; }
            Card *sel = sorted.items[idx - 1];
            vec_free(&sorted);
            CardArea_add_to_highlighted(G.hand, sel, true);
            for (int k = 0; k < _cards.count; k++) if (_cards.items[k] == sel) { vec_remove_at(&_cards, k); break; }
            any = true;
            sound_play("card1", 1, NAN);
        }
    }
    vec_free(&_cards);
    if (any) G_FUNCS_discard_cards_from_highlighted(NULL, true);
    return true;
}
EVFN(ev_tooth_juice) {
    EVCTX(BCtx);
    if (c->i <= G.play->cards.count) Card_juice_up(&G.play->cards.items[c->i - 1]->n, NAN, NAN);
    return true;
}
EVFN(ev_tooth) {
    (void)e_; (void)p_;
    for (int i = 1; i <= G.play->cards.count; i++) {
        BCtx *n = CTX(BCtx); n->i = i;
        add_event(EVENT(.func = ev_tooth_juice, .ctx = n));
        ease_dollars(-1, false);
        delay(0.23);
    }
    return true;
}

bool Blind_press_play(Blind *b) {
    if (b->disabled) return false;
    if (str_eq(b->name, "The Hook")) {
        add_event(EVENT(.func = ev_hook, .ctx = CTX(HookCtx)));
        b->triggered = 1;
        delay(0.7);
        return true;
    }
    if (str_eq(b->name, "Crimson Heart")) {
        if (G.jokers->cards.count > 0) { b->triggered = 1; b->prepped = true; }
    }
    if (str_eq(b->name, "The Fish")) b->prepped = true;
    if (str_eq(b->name, "The Tooth")) {
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_tooth));
        b->triggered = 1;
        return true;
    }
    return false;
}

void Blind_modify_hand(Blind *b, CardList *cards, PokerHands *hands, const char *text, double *mult, double *hand_chips, bool *modded) {
    (void)cards; (void)hands; (void)text;
    *modded = false;
    if (b->disabled) return;
    if (str_eq(b->name, "The Flint")) {
        b->triggered = 1;
        *mult = MAX(floor(*mult * 0.5 + 0.5), 1);
        *hand_chips = MAX(floor(*hand_chips * 0.5 + 0.5), 0);
        *modded = true;
    }
}

bool Blind_debuff_hand(Blind *b, CardList *cards, PokerHands *hand, const char *handname, bool check) {
    if (b->disabled) return false;
    /* self.debuff is always a table for blinds that were set */
    b->triggered = -1;
    if (b->debuff.hand && poker_hand_has(hand, b->debuff.hand)) { b->triggered = 1; return true; }
    if (b->debuff.h_size_ge != 0 && cards->count < b->debuff.h_size_ge) { b->triggered = 1; return true; }
    if (b->debuff.h_size_le != 0 && cards->count > b->debuff.h_size_le) { b->triggered = 1; return true; }
    int hi = hand_index(handname);
    if (str_eq(b->name, "The Eye")) {
        if (hi >= 0 && b->hands[hi]) { b->triggered = 1; return true; }
        if (!check && hi >= 0) b->hands[hi] = true;
    }
    if (str_eq(b->name, "The Mouth")) {
        if (b->only_hand && !str_eq(b->only_hand, handname)) { b->triggered = 1; return true; }
        if (!check) b->only_hand = handname ? intern(handname) : NULL;
    }
    if (str_eq(b->name, "The Arm")) {
        b->triggered = -1;
        if (hi >= 0 && G.GAME.hands[hi].level > 1) {
            b->triggered = 1;
            if (!check) {
                level_up_hand(&b->animatedSprite->n, handname, false, -1);
                Blind_wiggle(b);
            }
        }
    }
    if (str_eq(b->name, "The Ox")) {
        b->triggered = -1;
        if (str_eq(handname, G.GAME.current_round.most_played_poker_hand)) {
            b->triggered = 1;
            if (!check) {
                ease_dollars(-G.GAME.dollars, true);
                Blind_wiggle(b);
            }
        }
    }
    return false;
}

void Blind_drawn_to_hand(Blind *b) {
    if (!b->disabled) {
        if (str_eq(b->name, "Cerulean Bell")) {
            bool any = false;
            for (int i = 0; i < G.hand->cards.count; i++) if (G.hand->cards.items[i]->ability.forced_selection) any = true;
            if (!any) {
                CardArea_unhighlight_all(G.hand);
                CardList sorted = { 0 };
                for (int i = 0; i < G.hand->cards.count; i++) vec_push(&sorted, G.hand->cards.items[i]);
                for (int a = 1; a < sorted.count; a++) {
                    Card *k = sorted.items[a]; int j = a - 1;
                    while (j >= 0 && sorted.items[j]->sort_id > k->sort_id) { sorted.items[j + 1] = sorted.items[j]; j--; }
                    sorted.items[j + 1] = k;
                }
                int idx = pseudorandom_index(sorted.count, pseudoseed("cerulean_bell"), true);
                if (idx > 0) {
                    Card *fc = sorted.items[idx - 1];
                    fc->ability.forced_selection = true;
                    CardArea_add_to_highlighted(G.hand, fc, false);
                }
                vec_free(&sorted);
            }
        }
        if (str_eq(b->name, "Crimson Heart") && b->prepped && G.jokers->cards.count > 0) {
            CardList jokers = { 0 };
            for (int i = 0; i < G.jokers->cards.count; i++) {
                Card *j = G.jokers->cards.items[i];
                if (!j->debuff || G.jokers->cards.count < 2) vec_push(&jokers, j);
                Card_set_debuff(j, false);
            }
            for (int a = 1; a < jokers.count; a++) {
                Card *k = jokers.items[a]; int j = a - 1;
                while (j >= 0 && jokers.items[j]->sort_id > k->sort_id) { jokers.items[j + 1] = jokers.items[j]; j--; }
                jokers.items[j + 1] = k;
            }
            int idx = pseudorandom_index(jokers.count, pseudoseed("crimson_heart"), true);
            if (idx > 0) {
                Card *c = jokers.items[idx - 1];
                Card_set_debuff(c, true);
                Card_juice_up(&c->n, NAN, NAN);
                Blind_wiggle(b);
            }
            vec_free(&jokers);
        }
    }
    b->prepped = false;
}

bool Blind_stay_flipped(Blind *b, CardArea *area, Card *card) {
    if (!b->disabled && area == G.hand) {
        if (str_eq(b->name, "The Wheel") && pseudorandom_seed(pseudoseed("wheel")) < G.GAME.probabilities.normal / 7) return true;
        if (str_eq(b->name, "The House") && G.GAME.current_round.hands_played == 0 && G.GAME.current_round.discards_used == 0) return true;
        if (str_eq(b->name, "The Mark") && Card_is_face(card, true)) return true;
        if (str_eq(b->name, "The Fish") && b->prepped) return true;
    }
    return false;
}

void Blind_debuff_card(Blind *b, Card *card, bool from_blind) {
    (void)from_blind;
    if (!b) return;
    if (!b->disabled && card->area != G.jokers) {
        if (b->debuff.suit && Card_is_suit(card, b->debuff.suit, true, false)) { Card_set_debuff(card, true); return; }
        if (str_eq(b->debuff.is_face, "face") && Card_is_face(card, true)) { Card_set_debuff(card, true); return; }
        if (str_eq(b->name, "The Pillar") && card->ability.played_this_ante) { Card_set_debuff(card, true); return; }
        if (b->debuff.value && str_eq(b->debuff.value, card->base.value)) { Card_set_debuff(card, true); return; }
        if (b->debuff.nominal != 0 && b->debuff.nominal == card->base.nominal) { Card_set_debuff(card, true); return; }
    }
    if (str_eq(b->name, "Crimson Heart") && !b->disabled && card->area == G.jokers) return;
    if (str_eq(b->name, "Verdant Leaf") && !b->disabled && card->area != G.jokers) { Card_set_debuff(card, true); return; }
    Card_set_debuff(card, false);
}

static void Blind_align(Blind *b) {
    for (int i = 0; i < b->n.children.count; i++) {
        const char *k = b->n.children.items[i].name;
        Node *v = b->n.children.items[i].node;
        if (k && !strcmp(k, "animatedSprite")) {
            if (!v->states.drag.is) {
                v->T->r = 0.02 * sin(2 * G.TIMERS.REAL + b->n.T->x);
                v->T->y = b->n.T->y + 0.03 * sin(0.666 * G.TIMERS.REAL + b->n.T->x);
                b->n.shadow_height = 0.1 - (0.04 + 0.03 * sin(0.666 * G.TIMERS.REAL + b->n.T->x));
                v->T->x = b->n.T->x + 0.03 * sin(0.436 * G.TIMERS.REAL + b->n.T->x);
            }
        } else {
            v->T->x = b->n.T->x;
            v->T->y = b->n.T->y;
            v->T->r = b->n.T->r;
        }
    }
}

static void Blind_move(Node *n, double dt) {
    Blind *b = (Blind *)n;
    Moveable_move(n, dt);
    sync_states(b);
    Blind_align(b);
}

void Blind_change_dim(Blind *b, double w, double h) {
    if (!isnan(w)) b->n.T->w = w;
    if (!isnan(h)) b->n.T->h = h;
    b->animatedSprite->n.T->w = isnan(w) ? b->n.T->w : w;
    b->animatedSprite->n.T->h = isnan(h) ? b->n.T->h : h;
    AnimatedSprite_rescale(b->animatedSprite);
}

DVal Blind_save(Blind *b) {
    DVal t = dv_table();
    dv_set_str(&t, "name", b->name);
    dv_set_num(&t, "dollars", b->dollars);
    DVal d = dv_table();
    if (b->debuff.suit) dv_set_str(&d, "suit", b->debuff.suit);
    if (b->debuff.h_size_ge != 0) dv_set_num(&d, "h_size_ge", b->debuff.h_size_ge);
    if (b->debuff.h_size_le != 0) dv_set_num(&d, "h_size_le", b->debuff.h_size_le);
    if (b->debuff.hand) dv_set_str(&d, "hand", b->debuff.hand);
    if (b->debuff.is_face) dv_set_str(&d, "is_face", b->debuff.is_face);
    if (b->debuff.value) dv_set_str(&d, "value", b->debuff.value);
    if (b->debuff.nominal != 0) dv_set_num(&d, "nominal", b->debuff.nominal);
    dv_set(&t, "debuff", d);
    DVal pos = dv_table();
    dv_set_num(&pos, "x", b->pos.x);
    dv_set_num(&pos, "y", b->pos.y);
    dv_set(&t, "pos", pos);
    dv_set_num(&t, "mult", b->mult);
    dv_set_bool(&t, "disabled", b->disabled);
    if (b->has_discards_sub) dv_set_num(&t, "discards_sub", b->discards_sub);
    if (b->has_hands_sub) dv_set_num(&t, "hands_sub", b->hands_sub);
    dv_set_bool(&t, "boss", b->boss);
    const char *key = "";
    for (int i = 0; i < G.n_blinds; i++) if (str_eq(G.P_BLINDS_LIST[i].name, b->name)) key = G.P_BLINDS_LIST[i].key;
    dv_set_str(&t, "config_blind", key);
    dv_set_num(&t, "chips", b->chips);
    if (b->chip_text) dv_set_str(&t, "chip_text", b->chip_text);
    if (b->has_hands) {
        DVal h = dv_table();
        for (int i = 0; i < HAND_COUNT; i++) dv_set_bool(&h, HAND_NAMES[i], b->hands[i]);
        dv_set(&t, "hands", h);
    }
    if (b->only_hand) dv_set_str(&t, "only_hand", b->only_hand);
    else if (b->only_hand_false) dv_set_bool(&t, "only_hand", false);
    if (b->triggered) dv_set_bool(&t, "triggered", b->triggered > 0);
    return t;
}

void Blind_load(Blind *b, DVal *t) {
    static BlindProto empty;
    BlindProto *bp = P_BLIND(dv_get_str(t, "config_blind", ""));
    b->config.blind = bp ? bp : &empty;
    const char *nm = dv_get_str(t, "name", "");
    b->name = intern(nm);
    b->dollars = dv_get_num(t, "dollars", 0);
    DVal *d = dv_get(t, "debuff");
    memset(&b->debuff, 0, sizeof b->debuff);
    const char *s;
    if ((s = dv_get_str(d, "suit", NULL))) b->debuff.suit = intern(s);
    b->debuff.h_size_ge = dv_get_num(d, "h_size_ge", 0);
    b->debuff.h_size_le = dv_get_num(d, "h_size_le", 0);
    if ((s = dv_get_str(d, "hand", NULL))) b->debuff.hand = intern(s);
    if ((s = dv_get_str(d, "is_face", NULL))) b->debuff.is_face = intern(s);
    if ((s = dv_get_str(d, "value", NULL))) b->debuff.value = intern(s);
    b->debuff.nominal = dv_get_num(d, "nominal", 0);
    DVal *pos = dv_get(t, "pos");
    b->pos = SPOS(dv_get_num(pos, "x", 0), dv_get_num(pos, "y", 0));
    b->mult = dv_get_num(t, "mult", 0);
    b->disabled = dv_get_bool(t, "disabled", false);
    if (dv_get(t, "discards_sub")) { b->has_discards_sub = true; b->discards_sub = dv_get_num(t, "discards_sub", 0); }
    if (dv_get(t, "hands_sub")) { b->has_hands_sub = true; b->hands_sub = dv_get_num(t, "hands_sub", 0); }
    b->boss = dv_get_bool(t, "boss", false);
    b->chips = dv_get_num(t, "chips", 0);
    str_set(&b->chip_text, dv_get_str(t, "chip_text", NULL));
    DVal *h = dv_get(t, "hands");
    if (h) {
        b->has_hands = true;
        for (int i = 0; i < HAND_COUNT; i++) b->hands[i] = dv_get_bool(h, HAND_NAMES[i], false);
    }
    DVal *oh = dv_get(t, "only_hand");
    if (oh && oh->t == DV_STR) b->only_hand = intern(oh->s);
    else if (oh && oh->t == DV_BOOL) b->only_hand_false = true;
    DVal *tr = dv_get(t, "triggered");
    b->triggered = tr ? (dv_truthy(tr) ? 1 : -1) : 0;
    double spin = 0;
    if (b->config.blind->has_boss) spin = b->config.blind->boss.showdown ? 1 : 0.3;
    G.ARGS_spin.real = (G.SETTINGS.reduced_motion ? 0 : 1) * spin;
    if (bp) {
        b->blind_set = true;
        b->n.states.visible = true;
        AnimatedSprite_set_sprite_pos(b->animatedSprite, bp->pos);
        node_juice_up(&b->animatedSprite->n, 0.3, NAN);
        Blind_align(b);
        Moveable_hard_set_VT(&b->animatedSprite->n);
    } else b->n.states.visible = false;
    sync_states(b);
    Blind_change_colour(b, NULL);
    if (b->dollars > 0) {
        StrBuf sb; sb_init(&sb);
        for (int i = 0; i < (int)b->dollars; i++) sb_add(&sb, localize_key("$"));
        char *s2 = sb_take(&sb);
        str_set(&G.GAME.current_round.dollars_to_be_earned, s2);
        free(s2);
        UIElement *de = UIBox_get_UIE_by_ID(G.HUD_blind, "dollars_to_be_earned", NULL);
        if (de && de->config->object) DynaText_pop_in((DynaText *)de->config->object, 0);
        G.HUD_blind->n.alignment.offset.y = 0;
    }
    Blind_set_text(b);
}

static void B_noop(Node *n) { (void)n; }
static void B_noop_rel(Node *n, Node *d) { (void)n; (void)d; }
static void B_update(Node *n, double dt) { (void)n; (void)dt; }

const NodeVT BLIND_VT = {
    .type = NT_BLIND, .super = &MOVEABLE_VT, .name = "Blind",
    .draw = Blind_draw,
    .update = B_update,
    .move = Blind_move,
    .remove = Moveable_remove,
    .hover = Blind_hover,
    .stop_hover = Blind_stop_hover,
    .click = B_noop,
    .release = B_noop_rel,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = B_noop,
    .juice_up = Blind_juice_v,
    .hard_set_T = Moveable_hard_set_T,
};
