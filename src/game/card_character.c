/* card_character.c - port of card_character.lua */
#include "game.h"

#define EVFN(name) static bool name(Event *e_, void *p_)
#define EVCTX(T) T *c = (T *)p_; (void)e_
typedef struct { CardCharacter *ch; int n; } SayCtx;

CardCharacter *card_character_new(double x, double y, double w, double h, Center *center) {
    CardCharacter *c = NEW(CardCharacter);
    if (isnan(x)) x = 1;
    if (isnan(y)) y = 1;
    if (isnan(w)) w = G.CARD_W * 1.1;
    if (isnan(h)) h = G.CARD_H * 1.1;
    moveable_init(&c->n, &CARDCHAR_VT, x, y, w, h);
    c->n.states.collide.can = false;

    c->card = card_new(c->n.T->x, c->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"), center ? center : P_CENTER("j_joker"),
                       &(CardParams){ .bypass_discovery_center = true });
    child_set(&c->n, "card", &c->card->n);
    c->card->n.states.visible = false;
    Card_start_materialize(c->card, (Col[]){ G.C.BLUE, G.C.WHITE, G.C.RED }, 3, false, NAN);
    Moveable_set_alignment(&c->card->n, ALIGN_ARGS(.major = &c->n, .type = "cm", .has_offset = true, .offset = { 0, 0 }));
    c->card->jimbo = c;
    c->card->n.states.collide.can = true;
    c->card->n.states.focus.can = false;
    c->card->n.states.hover.can = true;
    c->card->n.states.drag.can = false;
    c->card->n.o_hover = Node_hover;

    c->particles = particles_new(0, 0, 0, 0, (ParticlesConfig){
        .timer = 0.03, .scale = 0.3, .speed = 1.2, .lifespan = 2, .attach = &c->n,
        .colours = dt_cols(3, G.C.RED, G.C.BLUE, G.C.ORANGE), .ncolours = 3, .fill = true });
    child_set(&c->n, "particles", &c->particles->n);
    c->particles->n.static_rotation = true;
    Moveable_set_role(&c->particles->n, ROLE_ARGS(.role_type = ROLE_MINOR, .xy_bond = BOND_WEAK, .r_bond = BOND_STRONG, .major = &c->n));

    vec_push(&G.I.CARD, &c->n);
    return c;
}

static void CC_align(CardCharacter *c) {
    if (c->card) {
        c->card->n.T->x = c->n.T->x + (c->n.T->w - c->card->n.T->w) / 2;
        c->card->n.T->y = c->n.T->y + (c->n.T->h - c->card->n.T->h) / 2;
    }
}

void CardCharacter_hard_set_VT(CardCharacter *c) {
    Moveable_align_to_major(&c->n);
    Moveable_hard_set_VT(&c->n);
    CC_align(c);
    Moveable_hard_set_VT(&c->card->n);
}

void CardCharacter_add_button(CardCharacter *c, const char *label, UIFunc button, Col colour, UIFunc update_func, bool snap_to, double yoff) {
    if (c->button) { child_set(&c->n, "button", NULL); node_remove(&c->button->n); }
    c->button_align = UIBCFG(.align = "bm", .has_offset = true, .offset = { 0, isnan(yoff) ? 0.3 : yoff }, .major = &c->n, .parent = &c->n);
    c->button = uibox_new(create_UIBox_character_button(label, button, colour, update_func, 3), c->button_align);
    child_set(&c->n, "button", &c->button->n);
    if (snap_to) Controller_snap_to_node(&G.CONTROLLER, &c->button->n);
}

void CardCharacter_add_speech_bubble(CardCharacter *c, const char *text_key, const char *align, bool quip, LocVars *vars) {
    if (c->speech_bubble) { child_set(&c->n, "speech_bubble", NULL); node_remove(&c->speech_bubble->n); }
    c->speech_bubble_align = UIBCFG(.align = align ? align : "bm", .has_offset = true, .offset = { 0, 0 }, .parent = &c->n);
    c->speech_bubble = uibox_new(UIDEF_speech_bubble(text_key, quip, vars), c->speech_bubble_align);
    child_set(&c->n, "speech_bubble", &c->speech_bubble->n);
    Moveable_set_role(&c->speech_bubble->n, ROLE_ARGS(.role_type = ROLE_MINOR, .xy_bond = BOND_WEAK, .r_bond = BOND_STRONG, .major = &c->n));
    c->speech_bubble->n.states.visible = false;
}

void CardCharacter_remove_button(CardCharacter *c) {
    if (c->button) { child_set(&c->n, "button", NULL); node_remove(&c->button->n); c->button = NULL; }
}

void CardCharacter_remove_speech_bubble(CardCharacter *c) {
    if (c->speech_bubble) { child_set(&c->n, "speech_bubble", NULL); node_remove(&c->speech_bubble->n); c->speech_bubble = NULL; }
}

EVFN(ev_say_first) {
    EVCTX(SayCtx);
    if (c->ch->speech_bubble) c->ch->speech_bubble->n.states.visible = true;
    CardCharacter_say_stuff(c->ch, c->n, true);
    return true;
}
EVFN(ev_say_next) {
    EVCTX(SayCtx);
    CardCharacter_say_stuff(c->ch, c->n - 1, true);
    return true;
}

void CardCharacter_say_stuff(CardCharacter *c, int n, bool not_first) {
    if (c->n.REMOVED) return;
    c->talking = true;
    if (!not_first) {
        SayCtx *s = CTX(SayCtx); s->ch = c; s->n = n;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .func = ev_say_first, .ctx = s));
    } else {
        if (n <= 0) { c->talking = false; return; }
        int new_said = math_random_range(1, 11);
        while (new_said == c->last_said) new_said = math_random_range(1, 11);
        c->last_said = new_said;
        sound_play(tfmt("voice%d", math_random_range(1, 11)), G.SPEEDFACTOR * (math_random() * 0.2 + 1), 0.5);
        node_juice_up(&c->card->n, NAN, NAN);
        SayCtx *s = CTX(SayCtx); s->ch = c; s->n = n;
        add_event_q(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .blocking = 0, .delay = 0.13, .func = ev_say_next, .ctx = s), Q_TUTORIAL, false);
    }
}

static void CC_draw(Node *n) {
    CardCharacter *c = (CardCharacter *)n;
    if (c->highlight) {
        node_draw(c->highlight_child);
        node_draw(c->highlight);
    }
    if (c->particles) node_draw(&c->particles->n);
    if (c->speech_bubble) node_draw(&c->speech_bubble->n);
    if (c->button && !c->talking) node_draw(&c->button->n);
    if (c->card) node_draw(&c->card->n);
    add_to_drawhash(n);
    Node_draw_boundingrect(n);
}

static void CC_remove(Node *n) {
    CardCharacter *c = (CardCharacter *)n;
    G.jimboed = false;
    remove_all_children(n);
    c->card = NULL; c->particles = NULL; c->speech_bubble = NULL; c->button = NULL;
    ptrvec_remove_all(&G.I.CARD, n);
    Moveable_remove(n);
}

static void CC_noop(Node *n) { (void)n; }
static void CC_noop_rel(Node *n, Node *d) { (void)n; (void)d; }
static void CC_update(Node *n, double dt) { (void)n; (void)dt; }

const NodeVT CARDCHAR_VT = {
    .type = NT_CARDCHAR, .super = &MOVEABLE_VT, .name = "Card_Character",
    .draw = CC_draw,
    .update = CC_update,
    .move = Moveable_move,
    .remove = CC_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = CC_noop,
    .release = CC_noop_rel,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = CC_noop,
    .juice_up = Moveable_juice_up,
    .hard_set_T = Moveable_hard_set_T,
};
