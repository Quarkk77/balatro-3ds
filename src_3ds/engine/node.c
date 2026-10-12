/* node.c - port of engine/node.lua and engine/moveable.lua */
#include "../game/game.h"

/* ------------------------------------------------------------------ */
/* type info                                                           */
/* ------------------------------------------------------------------ */
bool node_is(const Node *n, NodeType t) {
    if (!n) return false;
    for (const NodeVT *vt = n->vt; vt; vt = vt->super)
        if (vt->type == t) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* children                                                            */
/* ------------------------------------------------------------------ */
Node *child_get(Node *n, const char *name) {
    if (!n) return NULL;
    for (int i = 0; i < n->children.count; i++)
        if (n->children.items[i].name && !strcmp(n->children.items[i].name, name)) return n->children.items[i].node;
    return NULL;
}
void child_set(Node *n, const char *name, Node *c) {
    for (int i = 0; i < n->children.count; i++) {
        if (n->children.items[i].name && !strcmp(n->children.items[i].name, name)) {
            if (c) n->children.items[i].node = c;
            else vec_remove_at(&n->children, i);
            return;
        }
    }
    if (c) {
        ChildEntry e = { intern(name), c };
        vec_push(&n->children, e);
    }
}
void child_push(Node *n, Node *c) {
    ChildEntry e = { NULL, c };
    vec_push(&n->children, e);
}
void child_remove_node(Node *n, Node *c) {
    for (int i = 0; i < n->children.count; i++)
        if (n->children.items[i].node == c) { vec_remove_at(&n->children, i); return; }
}
int child_count(Node *n) { return n->children.count; }

/* ------------------------------------------------------------------ */
/* Node                                                                */
/* ------------------------------------------------------------------ */
static PtrVec graveyard;

void node_init(Node *n, const NodeVT *vt, double x, double y, double w, double h, Node *container) {
    n->vt = vt;
    n->own_T = (Transform){ x, y, w, h, 0, 1 };
    n->T = &n->own_T;
    n->CT = n->T;
    n->created_on_pause = G.SETTINGS.paused;
    if (!G.ID) G.ID = 1;
    n->ID = G.ID++;
    n->FRAME.DRAW = -1;
    n->FRAME.MOVE = -1;
    n->states.visible = true;
    n->states.collide.can = false;
    n->states.focus.can = false;
    n->states.hover.can = true;
    n->states.click.can = true;
    n->states.drag.can = true;
    n->states.release_on.can = true;
    n->container = container ? container : G.ROOM;
    n->pinch = &n->own_pinch;
    n->shadow_parrallax = &n->own_shadow_parrallax;
    if (vt == &NODE_VT) vec_push(&G.I.NODE, n);
    if (!G.STAGE_OBJECT_INTERRUPT) vec_push(&G.STAGE_OBJECTS[G.STAGE], n);
}

Node *node_new(double x, double y, double w, double h) {
    Node *n = NEW(Node);
    node_init(n, &NODE_VT, x, y, w, h, NULL);
    return n;
}

void Node_draw_boundingrect(Node *n) {
    n->under_overlay = G.under_overlay;
    if (G.DEBUG) {
        Transform *t = n->is_moveable ? &n->VT : n->T;
        gfx_push();
        gfx_scale(G.TILESCALE, G.TILESCALE);
        gfx_translate(t->x * G.TILESIZE + t->w * G.TILESIZE * 0.5, t->y * G.TILESIZE + t->h * G.TILESIZE * 0.5);
        gfx_rotate(t->r);
        gfx_translate(-t->w * G.TILESIZE * 0.5, -t->h * G.TILESIZE * 0.5);
        gfx_set_line_width(1 + (n->states.focus.is ? 1 : 0));
        if (n->states.collide.is) gfx_set_color4(0, 1, 0, 0.3f);
        else gfx_set_color4(1, 0, 0, 0.3f);
        if (n->states.focus.can) { gfx_set_color(G.C.GOLD); gfx_set_line_width(1); }
        if (n->CALCING) { gfx_set_color4(0, 0, 1, 1); gfx_set_line_width(3); }
        gfx_rectangle(GFX_LINE, 0, 0, t->w * G.TILESIZE, t->h * G.TILESIZE);
        gfx_pop();
    }
}

void Node_draw(Node *n) {
    Node_draw_boundingrect(n);
    if (n->states.visible) {
        add_to_drawhash(n);
        for (int i = 0; i < n->children.count; i++) node_draw(n->children.items[i].node);
    }
}

bool Node_collides_with_point(Node *n, Vec2 point) {
    /* 3DS: touches only reach what the bottom screen shows */
    if (G.CONTROLLER.HID.touch && !touch_reaches(n)) return false;
    /* 3DS: things drawn moved on the bottom screen take touches there */
    if (G.CONTROLLER.HID.touch) point.x -= bottom_dx(n) / (G.TILESCALE * G.TILESIZE);
    /* 3DS: elements drawn higher on the bottom screen take taps where they are drawn */
    point.y += ui3ds_bottom_shift(n);
    if (!n->container) return false;
    Transform *T = n->CT ? n->CT : n->T;
    double b = n->states.hover.is ? G.COLLISION_BUFFER : 0;
    Vec2 p = point, t;
    if (n->container != n) {
        Transform *ct = n->container->T;
        if (fabs(ct->r) < 0.1) {
            t.x = -ct->w / 2; t.y = -ct->h / 2;
            point_translate(&p, t);
            point_rotate(&p, ct->r);
            t.x = ct->w / 2 - ct->x; t.y = ct->h / 2 - ct->y;
            point_translate(&p, t);
        } else {
            t.x = -ct->x; t.y = -ct->y;
            point_translate(&p, t);
        }
    }
    if (fabs(T->r) < 0.1) {
        return p.x >= T->x - b && p.y >= T->y - b && p.x <= T->x + T->w + b && p.y <= T->y + T->h + b;
    }
    double c = cos(T->r + M_PI / 2), s = sin(T->r + M_PI / 2);
    p.x = p.x - (T->x + 0.5 * T->w);
    p.y = p.y - (T->y + 0.5 * T->h);
    t.x = p.y * c - p.x * s;
    t.y = p.y * s + p.x * c;
    p.x = t.x + (T->x + 0.5 * T->w);
    p.y = t.y + (T->y + 0.5 * T->h);
    return p.x >= T->x - b && p.y >= T->y - b && p.x <= T->x + T->w + b && p.y <= T->y + T->h + b;
}

void Node_set_offset(Node *n, Vec2 point, bool click) {
    Vec2 p = point, t;
    Transform *ct = n->container->T;
    t.x = -ct->w / 2; t.y = -ct->h / 2;
    point_translate(&p, t);
    point_rotate(&p, ct->r);
    t.x = ct->w / 2 - ct->x; t.y = ct->h / 2 - ct->y;
    point_translate(&p, t);
    if (click) { n->click_offset.x = p.x - n->T->x; n->click_offset.y = p.y - n->T->y; }
    else { n->hover_offset.x = p.x - n->T->x; n->hover_offset.y = p.y - n->T->y; }
}

void Node_drag(Node *n, Vec2 *off) {
    (void)off;
    if (n->d_popup) {
        if (!child_get(n, "d_popup")) {
            UIBox *b = uibox_new(n->d_popup, n->d_popup_config);
            child_set(n, "d_popup", &b->n);
            Node *hp = child_get(n, "h_popup");
            if (hp) hp->states.collide.can = false;
            vec_push(&G.I.POPUP, &b->n);
            b->n.states.drag.can = true;
        }
    }
}
Node *Node_can_drag(Node *n) { return n->states.drag.can ? n : NULL; }
void Node_stop_drag(Node *n) {
    Node *d = child_get(n, "d_popup");
    if (d) {
        ptrvec_remove_all(&G.I.POPUP, d);
        node_remove(d);
        child_set(n, "d_popup", NULL);
    }
}
void Node_hover(Node *n) {
    if (n->h_popup) {
        if (!child_get(n, "h_popup")) {
            n->h_popup_config->instance_type = "POPUP";
            UIBox *b = uibox_new(n->h_popup, n->h_popup_config);
            child_set(n, "h_popup", &b->n);
            b->n.states.collide.can = false;
            b->n.states.drag.can = true;
        }
    }
}
void Node_stop_hover(Node *n) {
    Node *h = child_get(n, "h_popup");
    if (h) {
        child_set(n, "h_popup", NULL);
        node_remove(h);
    }
}
void Node_put_focused_cursor(Node *n, double *x, double *y) {
    *x = (n->T->x + n->T->w / 2 + n->container->T->x) * (G.TILESCALE * G.TILESIZE);
    *y = (n->T->y + n->T->h / 2 + n->container->T->y) * (G.TILESCALE * G.TILESIZE);
}
void Node_set_container(Node *n, Node *container) {
    for (int i = 0; i < n->children.count; i++) node_set_container(n->children.items[i].node, container);
    n->container = container;
}
void Node_translate_container(Node *n) {
    if (n->container && n->container != n) {
        Transform *ct = n->container->T;
        double s = G.TILESCALE * G.TILESIZE;
        gfx_translate(ct->w * s * 0.5, ct->h * s * 0.5);
        gfx_rotate(ct->r);
        gfx_translate(-ct->w * s * 0.5 + ct->x * s, -ct->h * s * 0.5 + ct->y * s);
    }
}

void Controller_clear_target_refs(Controller *c, Node *n) {
    if (c->clicked.target == n) c->clicked.target = NULL;
    if (c->focused.target == n) c->focused.target = NULL;
    if (c->dragging.target == n) c->dragging.target = NULL;
    if (c->hovering.target == n) c->hovering.target = NULL;
    if (c->released_on.target == n) c->released_on.target = NULL;
    if (c->cursor_down.target == n) c->cursor_down.target = NULL;
    if (c->cursor_up.target == n) c->cursor_up.target = NULL;
    if (c->cursor_hover.target == n) c->cursor_hover.target = NULL;
    /* the C port also clears "previous" targets, which Lua kept alive via GC */
    if (c->clicked.prev_target == n) c->clicked.prev_target = NULL;
    if (c->focused.prev_target == n) c->focused.prev_target = NULL;
    if (c->dragging.prev_target == n) c->dragging.prev_target = NULL;
    if (c->hovering.prev_target == n) c->hovering.prev_target = NULL;
    if (c->released_on.prev_target == n) c->released_on.prev_target = NULL;
    if (c->cursor_hover.prev_target == n) c->cursor_hover.prev_target = NULL;
    if (c->snap_cursor_to.node == n) c->snap_cursor_to.node = NULL;
    ptrvec_remove_all(&c->collision_list, n);
    ptrvec_remove_all(&c->nodes_at_cursor, n);
}

void Node_remove(Node *n) {
    ptrvec_remove(&G.I.POPUP, n);
    ptrvec_remove(&G.I.NODE, n);
    /* the node may have been created on another stage */
    for (int st = 0; st < (int)ARRLEN(G.STAGE_OBJECTS); st++) ptrvec_remove(&G.STAGE_OBJECTS[st], n);
    /* children may remove themselves from our list while being removed */
    while (n->children.count > 0) {
        Node *c = n->children.items[n->children.count - 1].node;
        n->children.count--;
        if (c && !c->REMOVED) node_remove(c);
    }
    Controller_clear_target_refs(&G.CONTROLLER, n);
    ptrvec_remove_all(&G.DRAW_HASH, n);
    n->REMOVED = true;
}

void node_remove(Node *n) {
    if (!n || n->REMOVED) return;
    if (n->vt->remove) n->vt->remove(n);
    else Node_remove(n);
    n->REMOVED = true;
    vec_push(&graveyard, n);   /* guarded by REMOVED above, so each node is pushed once */
}

/* Removed nodes stay allocated while anything may still point at them
 * (Lua's GC handled this). heap_collect frees the ones nothing refers to
 * any more, with everything they own; see core/heap.c. */
int node_collect_graveyard(void) {
    if (!graveyard.count) return 0;
    return heap_collect(graveyard.items, &graveyard.count, graveyard.items);
}
int node_graveyard_size(void) { return graveyard.count; }

double Node_fast_mid_dist(Node *a, Node *b) {
    double x = (b->T->x + 0.5 * b->T->w) - (a->T->x + a->T->w);
    double y = (b->T->y + 0.5 * b->T->h) - (a->T->y + a->T->h);
    return sqrt(x) * sqrt(x) + y * y; /* matches the original operator precedence */
}

static void Node_update_noop(Node *n, double dt) { (void)n; (void)dt; }
static void Node_click_noop(Node *n) { (void)n; }
static void Node_release_noop(Node *n, Node *d) { (void)n; (void)d; }
static void Node_animate_noop(Node *n) { (void)n; }

const NodeVT NODE_VT = {
    .type = NT_NODE, .super = NULL, .name = "Node",
    .draw = Node_draw,
    .update = Node_update_noop,
    .move = NULL,
    .remove = Node_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = Node_click_noop,
    .release = Node_release_noop,
    .drag = Node_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = Node_animate_noop,
};

/* ------------------------------------------------------------------ */
/* Moveable                                                            */
/* ------------------------------------------------------------------ */
void moveable_init(Node *n, const NodeVT *vt, double x, double y, double w, double h) {
    node_init(n, vt, x, y, w, h, NULL);
    n->is_moveable = true;
    n->VT = *n->T;
    n->role.role_type = ROLE_MAJOR;
    n->role.draw_major = n;
    n->role.xy_bond = n->role.wh_bond = n->role.r_bond = n->role.scale_bond = BOND_STRONG;
    strcpy(n->alignment.type, "a");
    n->alignment.prev_type[0] = 0;
    n->last_moved = -1;
    n->last_aligned = -1;
    n->Mid = n;
    n->own_shadow_parrallax = (Vec2){ 0, -1.5 };
    n->shadow_height = 0.2;
    Moveable_calculate_parrallax(n);
    vec_push(&G.MOVEABLES, n);
    if (vt == &MOVEABLE_VT) vec_push(&G.I.MOVEABLE, n);
}

Node *moveable_new(double x, double y, double w, double h) {
    Node *n = NEW(Node);
    moveable_init(n, &MOVEABLE_VT, x, y, w, h);
    return n;
}

void Moveable_draw(Node *n) {
    Node_draw(n);
    Node_draw_boundingrect(n);
}

void Moveable_set_alignment(Node *n, AlignArgs a) {
    if (a.major) {
        Moveable_set_role(n, ROLE_ARGS(
            .role_type = ROLE_MINOR,
            .major = a.major,
            .xy_bond = a.bond >= 0 ? a.bond : BOND_WEAK,
            .wh_bond = a.wh_bond >= 0 ? a.wh_bond : (int)n->role.wh_bond,
            .r_bond = a.r_bond >= 0 ? a.r_bond : (int)n->role.r_bond,
            .scale_bond = a.scale_bond >= 0 ? a.scale_bond : (int)n->role.scale_bond));
    }
    if (a.type) snprintf(n->alignment.type, sizeof n->alignment.type, "%s", a.type);
    if (a.has_offset) n->alignment.offset = a.offset;
    n->alignment.lr_clamp = a.lr_clamp;
}

void Moveable_align_to_major(Node *n) {
    Alignment *al = &n->alignment;
    if (strcmp(al->type, al->prev_type) != 0) {
        al->tl.a = !strcmp(al->type, "a");
        al->tl.m = str_has_char(al->type, 'm');
        al->tl.c = str_has_char(al->type, 'c');
        al->tl.b = str_has_char(al->type, 'b');
        al->tl.t = str_has_char(al->type, 't');
        al->tl.l = str_has_char(al->type, 'l');
        al->tl.r = str_has_char(al->type, 'r');
        al->tl.i = str_has_char(al->type, 'i');
    }
    if (al->prev_offset.x == al->offset.x && al->prev_offset.y == al->offset.y && !strcmp(al->prev_type, al->type)) return;
    n->NEW_ALIGNMENT = true;
    if (strcmp(al->type, al->prev_type) != 0) strcpy(al->prev_type, al->type);
    if (al->tl.a || !n->role.major) return;
    Node *mj = n->role.major;
    Node *mid = n->Mid ? n->Mid : n;
    if (al->tl.m) n->role.offset.x = 0.5 * mj->T->w - mid->T->w / 2 + al->offset.x - mid->T->x + n->T->x;
    if (al->tl.c) n->role.offset.y = 0.5 * mj->T->h - mid->T->h / 2 + al->offset.y - mid->T->y + n->T->y;
    if (al->tl.b) n->role.offset.y = al->tl.i ? al->offset.y + mj->T->h - n->T->h : al->offset.y + mj->T->h;
    if (al->tl.r) n->role.offset.x = al->tl.i ? al->offset.x + mj->T->w - n->T->w : al->offset.x + mj->T->w;
    if (al->tl.t) n->role.offset.y = al->tl.i ? al->offset.y : al->offset.y - n->T->h;
    if (al->tl.l) n->role.offset.x = al->tl.i ? al->offset.x : al->offset.x - n->T->w;
    n->T->x = mj->T->x + n->role.offset.x;
    n->T->y = mj->T->y + n->role.offset.y;
    al->prev_offset = al->offset;
}

void Moveable_hard_set_T(Node *n, double x, double y, double w, double h) {
    n->T->x = x; n->T->y = y; n->T->w = w; n->T->h = h;
    n->velocity.x = n->velocity.y = n->velocity.r = n->velocity.scale = 0;
    n->VT.x = x; n->VT.y = y; n->VT.w = w; n->VT.h = h;
    n->VT.r = n->T->r;
    n->VT.scale = n->T->scale;
    Moveable_calculate_parrallax(n);
}
void Moveable_hard_set_VT(Node *n) {
    n->VT.x = n->T->x; n->VT.y = n->T->y; n->VT.w = n->T->w; n->VT.h = n->T->h;
}

void Moveable_drag(Node *n, Vec2 *offset) {
    if (n->states.drag.can || offset) {
        Vec2 p, t;
        p.x = G.CONTROLLER.cursor_position.x / (G.TILESCALE * G.TILESIZE);
        p.y = G.CONTROLLER.cursor_position.y / (G.TILESCALE * G.TILESIZE);
        Transform *ct = n->container->T;
        t.x = -ct->w / 2; t.y = -ct->h / 2;
        point_translate(&p, t);
        point_rotate(&p, ct->r);
        t.x = ct->w / 2 - ct->x; t.y = ct->h / 2 - ct->y;
        point_translate(&p, t);
        Vec2 off = offset ? *offset : n->click_offset;
        n->T->x = p.x - off.x;
        n->T->y = p.y - off.y;
        n->NEW_ALIGNMENT = true;
        for (int i = 0; i < n->children.count; i++) node_drag(n->children.items[i].node, &off);
    }
    if (n->states.drag.can) Node_drag(n, offset);
}

void Moveable_juice_up(Node *n, double amount, double rot_amt) {
    if (G.SETTINGS.reduced_motion) return;
    if (isnan(amount)) amount = 0.4;
    double end_time = G.TIMERS.REAL + 0.4;
    double start_time = G.TIMERS.REAL;
    double ra;
    if (!isnan(rot_amt)) ra = rot_amt;
    else {
        double opts[2] = { 0.6 * amount, -0.6 * amount };
        ra = opts[math_random_range(1, 2) - 1];
    }
    n->juice.active = true;
    n->juice.handled_elsewhere = false;
    n->juice.scale = 0;
    n->juice.scale_amt = amount;
    n->juice.r = 0;
    n->juice.r_amt = ra;
    n->juice.start_time = start_time;
    n->juice.end_time = end_time;
    n->VT.scale = 1 - 0.6 * amount;
}

void Moveable_move_juice(Node *n, double dt) {
    (void)dt;
    if (n->juice.active && !n->juice.handled_elsewhere) {
        if (n->juice.end_time < G.TIMERS.REAL) n->juice.active = false;
        else {
            double el = G.TIMERS.REAL - n->juice.start_time;
            double rem = MAX(0, (n->juice.end_time - G.TIMERS.REAL) / (n->juice.end_time - n->juice.start_time));
            n->juice.scale = n->juice.scale_amt * sin(50.8 * el) * pow(rem, 3);
            n->juice.r = n->juice.r_amt * sin(40.8 * el) * pow(rem, 2);
        }
    }
}

void Moveable_move(Node *n, double dt) {
    if (n->FRAME.MOVE >= G.FRAMES.MOVE) return;
    n->frame_major_valid = false;
    n->FRAME.MOVE = G.FRAMES.MOVE;
    if (!n->created_on_pause && G.SETTINGS.paused) return;
    Moveable_align_to_major(n);
    n->CALCING = false;
    if (n->role.role_type == ROLE_GLUED) {
        if (n->role.major) Moveable_glue_to_major(n, n->role.major);
    } else if (n->role.role_type == ROLE_MINOR && n->role.major) {
        if (n->role.major->FRAME.MOVE < G.FRAMES.MOVE) node_move(n->role.major, dt);
        n->STATIONARY = n->role.major->STATIONARY;
        if (!n->STATIONARY || n->NEW_ALIGNMENT || n->refresh_movement || n->juice.active ||
            n->role.xy_bond == BOND_WEAK || n->role.r_bond == BOND_WEAK) {
            n->CALCING = true;
            Moveable_move_with_major(n, dt);
        }
    } else if (n->role.role_type == ROLE_MAJOR) {
        n->STATIONARY = true;
        Moveable_move_juice(n, dt);
        Moveable_move_xy(n, dt);
        Moveable_move_r(n, dt, n->velocity.x);
        Moveable_move_scale(n, dt);
        Moveable_move_wh(n, dt);
        Moveable_calculate_parrallax(n);
    }
    if (n->alignment.lr_clamp) Moveable_lr_clamp(n);
    n->NEW_ALIGNMENT = false;
}

void Moveable_lr_clamp(Node *n) {
    if (n->T->x < 0) n->T->x = 0;
    if (n->VT.x < 0) n->VT.x = 0;
    if (n->T->x + n->T->w > G.ROOM->T->w) n->T->x = G.ROOM->T->w - n->T->w;
    if (n->VT.x + n->VT.w > G.ROOM->T->w) n->VT.x = G.ROOM->T->w - n->VT.w;
}

void Moveable_glue_to_major(Node *n, Node *m) {
    n->T = m->T;
    n->VT.x = m->VT.x + (0.5 * (1 - m->VT.w / m->T->w) * n->T->w);
    n->VT.y = m->VT.y;
    n->VT.w = m->VT.w;
    n->VT.h = m->VT.h;
    n->VT.r = m->VT.r;
    n->VT.scale = m->VT.scale;
    n->pinch = m->pinch;
    n->shadow_parrallax = m->shadow_parrallax;
}

void Moveable_move_with_major(Node *n, double dt) {
    if (n->role.role_type != ROLE_MINOR) return;
    MajorInfo *mt = Moveable_get_major(n->role.major);
    Node *mj = mt->major;
    Moveable_move_juice(n, dt);
    Vec2 ro;
    if (n->role.r_bond == BOND_WEAK) {
        ro.x = n->role.offset.x + mt->offset.x;
        ro.y = n->role.offset.y + mt->offset.y;
    } else {
        if (mj->VT.r < 0.0001 && mj->VT.r > -0.0001) {
            ro.x = n->role.offset.x + mt->offset.x;
            ro.y = n->role.offset.y + mt->offset.y;
        } else {
            double c = cos(mj->VT.r), s = sin(mj->VT.r);
            double ww = -n->T->w / 2 + mj->T->w / 2, hh = -n->T->h / 2 + mj->T->h / 2;
            double ox = n->role.offset.x + mt->offset.x - ww, oy = n->role.offset.y + mt->offset.y - hh;
            ro.x = ox * c - oy * s + ww;
            ro.y = ox * s + oy * c + hh;
        }
    }
    n->T->x = mj->T->x + ro.x;
    n->T->y = mj->T->y + ro.y;
    if (n->role.xy_bond == BOND_STRONG) {
        n->VT.x = mj->VT.x + ro.x;
        n->VT.y = mj->VT.y + ro.y;
    } else Moveable_move_xy(n, dt);

    if (n->role.r_bond == BOND_STRONG) n->VT.r = n->T->r + mj->VT.r + (n->juice.active ? n->juice.r : 0);
    else Moveable_move_r(n, dt, n->velocity.x);

    if (n->role.scale_bond == BOND_STRONG) n->VT.scale = n->T->scale * (mj->VT.scale / mj->T->scale) + (n->juice.active ? n->juice.scale : 0);
    else Moveable_move_scale(n, dt);

    if (n->role.wh_bond == BOND_STRONG) {
        n->VT.x = n->VT.x + (0.5 * (1 - mj->VT.w / mj->T->w) * n->T->w);
        n->VT.w = n->T->w * (mj->VT.w / mj->T->w);
        n->VT.h = n->T->h * (mj->VT.h / mj->T->h);
    } else Moveable_move_wh(n, dt);
    Moveable_calculate_parrallax(n);
}

void Moveable_move_xy(Node *n, double dt) {
    if ((n->T->x != n->VT.x || fabs(n->velocity.x) > 0.01) || (n->T->y != n->VT.y || fabs(n->velocity.y) > 0.01)) {
        n->velocity.x = G.exp_times.xy * n->velocity.x + (1 - G.exp_times.xy) * (n->T->x - n->VT.x) * 35 * dt;
        n->velocity.y = G.exp_times.xy * n->velocity.y + (1 - G.exp_times.xy) * (n->T->y - n->VT.y) * 35 * dt;
        double mv = G.exp_times.max_vel;
        if (n->velocity.x * n->velocity.x + n->velocity.y * n->velocity.y > mv * mv) {
            double av = sqrt(n->velocity.x * n->velocity.x + n->velocity.y * n->velocity.y);
            n->velocity.x = mv * n->velocity.x / av;
            n->velocity.y = mv * n->velocity.y / av;
        }
        n->STATIONARY = false;
        n->VT.x += n->velocity.x;
        n->VT.y += n->velocity.y;
        if (fabs(n->VT.x - n->T->x) < 0.01 && fabs(n->velocity.x) < 0.01) { n->VT.x = n->T->x; n->velocity.x = 0; }
        if (fabs(n->VT.y - n->T->y) < 0.01 && fabs(n->velocity.y) < 0.01) { n->VT.y = n->T->y; n->velocity.y = 0; }
    }
}

void Moveable_move_scale(Node *n, double dt) {
    (void)dt;
    double des = n->T->scale + (n->zoom ? ((n->states.drag.is ? 0.1 : 0) + (n->states.hover.is ? 0.05 : 0)) : 0) + (n->juice.active ? n->juice.scale : 0);
    if (des != n->VT.scale || fabs(n->velocity.scale) > 0.001) {
        n->STATIONARY = false;
        n->velocity.scale = G.exp_times.scale * n->velocity.scale + (1 - G.exp_times.scale) * (des - n->VT.scale);
        n->VT.scale += n->velocity.scale;
    }
}

void Moveable_move_wh(Node *n, double dt) {
    if ((n->T->w != n->VT.w && !n->pinch->x) || (n->T->h != n->VT.h && !n->pinch->y) ||
        (n->VT.w > 0 && n->pinch->x) || (n->VT.h > 0 && n->pinch->y)) {
        n->STATIONARY = false;
        n->VT.w += (8 * dt) * (n->pinch->x ? -1 : 1) * n->T->w;
        n->VT.h += (8 * dt) * (n->pinch->y ? -1 : 1) * n->T->h;
        n->VT.w = MAX(MIN(n->VT.w, n->T->w), 0);
        n->VT.h = MAX(MIN(n->VT.h, n->T->h), 0);
    }
}

void Moveable_move_r(Node *n, double dt, double vel_x) {
    double des = n->T->r + 0.015 * vel_x / dt + (n->juice.active ? n->juice.r * 2 : 0);
    if (des != n->VT.r || fabs(n->velocity.r) > 0.001) {
        n->STATIONARY = false;
        n->velocity.r = G.exp_times.r * n->velocity.r + (1 - G.exp_times.r) * (des - n->VT.r);
        n->VT.r += n->velocity.r;
    }
    if (fabs(n->VT.r - n->T->r) < 0.001 && fabs(n->velocity.r) < 0.001) { n->VT.r = n->T->r; n->velocity.r = 0; }
}

void Moveable_calculate_parrallax(Node *n) {
    if (!G.ROOM) return;
    n->shadow_parrallax->x = (n->T->x + n->T->w / 2 - G.ROOM->T->w / 2) / (G.ROOM->T->w / 2) * 1.5;
}

void Moveable_set_role(Node *n, RoleArgs a) {
    Role r;
    r.role_type = a.role_type >= 0 ? (RoleType)a.role_type : n->role.role_type;
    r.offset = a.has_offset ? a.offset : n->role.offset;
    r.major = a.major ? a.major : n->role.major;
    r.xy_bond = a.xy_bond >= 0 ? (Bond)a.xy_bond : n->role.xy_bond;
    r.wh_bond = a.wh_bond >= 0 ? (Bond)a.wh_bond : n->role.wh_bond;
    r.r_bond = a.r_bond >= 0 ? (Bond)a.r_bond : n->role.r_bond;
    r.scale_bond = a.scale_bond >= 0 ? (Bond)a.scale_bond : n->role.scale_bond;
    r.draw_major = a.draw_major ? a.draw_major : n->role.draw_major;
    n->role = r;
    if (n->role.role_type == ROLE_MAJOR) n->role.major = NULL;
}

MajorInfo *Moveable_get_major(Node *n) {
    if ((n->role.role_type != ROLE_MAJOR && n->role.major != n) && (n->role.xy_bond != BOND_WEAK && n->role.r_bond != BOND_WEAK)) {
        if (!n->frame_major_valid || G.REFRESH_FRAME_MAJOR_CACHE) {
            n->frame_major_valid = true;
            MajorInfo *m = Moveable_get_major(n->role.major);
            n->frame_major.major = m->major;
            n->frame_major.offset.x = m->offset.x + n->role.offset.x + n->layered_parallax.x;
            n->frame_major.offset.y = m->offset.y + n->role.offset.y + n->layered_parallax.y;
        }
        return &n->frame_major;
    }
    n->self_major.major = n;
    n->self_major.offset.x = 0;
    n->self_major.offset.y = 0;
    return &n->self_major;
}

void Moveable_remove(Node *n) {
    ptrvec_remove(&G.MOVEABLES, n);
    ptrvec_remove(&G.I.MOVEABLE, n);
    Node_remove(n);
}

static void Moveable_move_v(Node *n, double dt) { Moveable_move(n, dt); }
static void Moveable_juice_v(Node *n, double a, double r) { Moveable_juice_up(n, a, r); }

const NodeVT MOVEABLE_VT = {
    .type = NT_MOVEABLE, .super = &NODE_VT, .name = "Moveable",
    .draw = Moveable_draw,
    .update = Node_update_noop,
    .move = Moveable_move_v,
    .remove = Moveable_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = Node_click_noop,
    .release = Node_release_noop,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = Node_animate_noop,
    .juice_up = Moveable_juice_v,
    .hard_set_T = Moveable_hard_set_T,
};

/* ------------------------------------------------------------------ */
/* refs                                                                */
/* ------------------------------------------------------------------ */
bool ref_valid(Ref r) { return r.kind != REF_NONE && r.p; }
double ref_num(Ref r) {
    switch (r.kind) {
    case REF_NUM: return *(double *)r.p;
    case REF_INT: return *(int *)r.p;
    case REF_FLT: return *(float *)r.p;
    case REF_BOOL: return *(bool *)r.p ? 1 : 0;
    case REF_STR: { char *s = *(char **)r.p; return s ? atof(s) : 0; }
    case REF_CSTR: { const char *s = *(const char **)r.p; return s ? atof(s) : 0; }
    case REF_BUF: return atof((const char *)r.p);
    default: return 0;
    }
}
void ref_set_num(Ref r, double v) {
    switch (r.kind) {
    case REF_NUM: *(double *)r.p = v; break;
    case REF_INT: *(int *)r.p = (int)v; break;
    case REF_FLT: *(float *)r.p = (float)v; break;
    case REF_BOOL: *(bool *)r.p = v != 0; break;
    default: break;
    }
}
Ref ref_new_str(const char *init) {
    char **slot = xcalloc(1, sizeof(char *));
    *slot = xstrdup(init ? init : "");
    return REFS(slot);
}
const char *ref_tostring(Ref r) {
    switch (r.kind) {
    case REF_NUM: return num_str(*(double *)r.p);
    case REF_INT: return tfmt("%d", *(int *)r.p);
    case REF_FLT: return num_str(*(float *)r.p);
    case REF_BOOL: return *(bool *)r.p ? "true" : "false";
    case REF_STR: { char *s = *(char **)r.p; return s ? s : "nil"; }
    case REF_CSTR: { const char *s = *(const char **)r.p; return s ? s : "nil"; }
    case REF_BUF: return (const char *)r.p;
    default: return "nil";
    }
}
bool ref_truthy(Ref r) {
    switch (r.kind) {
    case REF_BOOL: return *(bool *)r.p;
    case REF_NUM: case REF_INT: return true;
    case REF_STR: return *(char **)r.p != NULL;
    case REF_CSTR: return *(const char **)r.p != NULL;
    case REF_BUF: return true;
    case REF_NODE: return *(Node **)r.p != NULL;
    default: return false;
    }
}
Node *ref_node(Ref r) { return r.kind == REF_NODE && r.p ? *(Node **)r.p : NULL; }
float *ref_col(Ref r) {
    if (r.kind == REF_COL && r.p) return *(Col *)r.p;
    if (r.kind == REF_COLV) return (float *)r.p;
    return NULL;
}
