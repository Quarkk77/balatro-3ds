/* controller.c - port of engine/controller.lua */
#include "../game/game.h"

enum { AX_LSTICK = 0, AX_RSTICK, AX_LTRIG, AX_RTRIG };

void controller_init(Controller *c) {
    memset(c, 0, sizeof *c);
    c->clicked.handled = c->focused.handled = c->dragging.handled = c->hovering.handled = c->released_on.handled = true;
    c->cursor_down.handled = true;
    c->cursor_up.time = 0.1;
    c->cursor_up.handled = true;
    c->cursor_hover.handled = true;
    strset_init(&c->pressed_keys); strset_init(&c->held_keys); strset_init(&c->released_keys);
    nummap_init(&c->held_key_times);
    strset_init(&c->pressed_buttons); strset_init(&c->held_buttons); strset_init(&c->released_buttons);
    nummap_init(&c->held_button_times);
    strset_init(&c->locks.other);
    c->axis_cursor_speed = 20;
    c->cursor_context.layer = 1;
    strmap_init(&c->cardarea_context);
    c->HID.pointer = true;
    c->HID.mouse = true;
    c->GAMEPAD.object = -1;
}

static bool any_lock(Controller *c) {
    if (c->locks.wipe || c->locks.frame || c->locks.frame_set || c->locks.load || c->locks.no_space || c->locks.chosen_card) return true;
    for (int i = 0; i < c->locks.other.cap; i++) if (NUMMAP_LIVE(&c->locks.other, i) && c->locks.other.vals[i] != 0) return true;
    return false;
}

static const char *console_from_name(const char *name) {
    /* love-gamepadguesser patterns, simplified */
    if (strstr(name, "PS3") || strstr(name, "PS4") || strstr(name, "PS5") || strstr(name, "Sony") ||
        strstr(name, "PlayStation") || strstr(name, "Playstation") || strstr(name, "DualSense") || strstr(name, "DualShock")) return "Playstation";
    if (strstr(name, "Wii") || strstr(name, "NES") || strstr(name, "nes") || strstr(name, "Switch") || strstr(name, "Joy-Con") || strstr(name, "Joy Con") ||
        strstr(name, "3DS"))
        return "Nintendo";
    return "Xbox";
}

void Controller_set_gamepad(Controller *c, int gp) {
    if (c->GAMEPAD.object != gp) {
        c->GAMEPAD.object = gp;
        c->GAMEPAD.present = true;
        snprintf(c->GAMEPAD.name, sizeof c->GAMEPAD.name, "%s", gfx_gamepad_name(gp));
        const char *console = console_from_name(c->GAMEPAD.name);
        if (strcmp(c->GAMEPAD_CONSOLE, console) != 0) {
            snprintf(c->GAMEPAD_CONSOLE, sizeof c->GAMEPAD_CONSOLE, "%s", console);
            Atlas *gp_atlas = strmap_get(&G.ASSET_ATLAS, "gamepad_ui");
            for (int i = 0; i < G.I.SPRITE.count; i++) {
                Sprite *v = G.I.SPRITE.items[i];
                if (v->atlas == gp_atlas) {
                    v->sprite_pos.y = !strcmp(c->GAMEPAD_CONSOLE, "Nintendo") ? 2 : !strcmp(c->GAMEPAD_CONSOLE, "Playstation") ? (G.F_PS4_PLAYSTATION_GLYPHS ? 3 : 1) : 0;
                    Sprite_set_sprite_pos(v, v->sprite_pos);
                }
            }
        }
    }
}

void Controller_set_HID_flags(Controller *c, const char *t) {
    if (t && !strcmp(t, "axis")) {
        c->HID.controller = true;
        strcpy(c->HID.last_type, "axis");
    } else if (t && strcmp(t, c->HID.last_type) != 0) {
        c->HID.dpad = !strcmp(t, "button");
        c->HID.pointer = !strcmp(t, "mouse") || !strcmp(t, "axis_cursor") || !strcmp(t, "touch");
        c->HID.controller = !strcmp(t, "button") || !strcmp(t, "axis_cursor");
        c->HID.mouse = !strcmp(t, "mouse");
        c->HID.touch = !strcmp(t, "touch");
        c->HID.axis_cursor = !strcmp(t, "axis_cursor");
        snprintf(c->HID.last_type, sizeof c->HID.last_type, "%s", t);
        gfx_set_mouse_visible(c->HID.mouse);
    }
    if (!c->HID.controller) {
        c->GAMEPAD_CONSOLE[0] = 0;
        c->GAMEPAD.object = -1;
        c->GAMEPAD.present = false;
        c->GAMEPAD.mapping[0] = 0;
        c->GAMEPAD.name[0] = 0;
    }
}

void Controller_set_cursor_position(Controller *c) {
    if (c->HID.mouse || c->HID.touch) {
        c->interrupt.focus = false;
        if (c->focused.target) {
            c->focused.target->states.focus.is = false;
            c->focused.target = NULL;
        }
        gfx_mouse_pos(&c->cursor_position.x, &c->cursor_position.y);
        G.CURSOR->n.T->x = c->cursor_position.x / (G.TILESCALE * G.TILESIZE);
        G.CURSOR->n.T->y = c->cursor_position.y / (G.TILESCALE * G.TILESIZE);
        G.CURSOR->n.VT.x = G.CURSOR->n.T->x;
        G.CURSOR->n.VT.y = G.CURSOR->n.T->y;
    }
}

static bool ev_unlock_frame(Event *e, void *ctx) {
    (void)e; (void)ctx;
    G.CONTROLLER.locks.frame = false;
    return true;
}

static const char *Controller_update_axis(Controller *c, double dt);
void Controller_key_press_update(Controller *c, const char *key, double dt);
#define key_press_update Controller_key_press_update
static void key_hold_update(Controller *c, const char *key, double dt);
static void key_release_update(Controller *c, const char *key, double dt);
static void button_press_update(Controller *c, const char *button, double dt);
static void button_hold_update(Controller *c, const char *button, double dt);
static void button_release_update(Controller *c, const char *button, double dt);

typedef struct { int id; } HoverCtx;
static bool ev_touch_hover(Event *e, void *p) {
    (void)e;
    HoverCtx *h = p;
    if (G.CONTROLLER.hovering.target && h->id == G.CONTROLLER.hovering.target->ID) node_hover(G.CONTROLLER.hovering.target);
    return true;
}

/* iterate a StrSet calling fn for each live key; keys copied first since
 * handlers may modify the set */
static void each_key(StrSet *s, Controller *c, double dt, void (*fn)(Controller *, const char *, double)) {
    char **keys = NULL; int n = 0;
    for (int i = 0; i < s->cap; i++) if (NUMMAP_LIVE(s, i) && s->vals[i] != 0) { keys = xrealloc(keys, (size_t)(n + 1) * sizeof(char *)); keys[n++] = xstrdup(s->keys[i]); }
    for (int i = 0; i < n; i++) { fn(c, keys[i], dt); free(keys[i]); }
    free(keys);
}

void Controller_update(Controller *c, double dt) {
    c->locked = false;
    c->locks.wipe = G.screenwipe != NULL;
    if (any_lock(c)) c->locked = true;
    if (c->locks.frame_set) {
        c->locks.frame_set = false;
        c->overlay_timer = 0;
        add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.1, .timer = TIMER_UPTIME, .blocking = 0, .blockable = 0,
                        .no_delete = true, .func = ev_unlock_frame));
    }
    if (G.OVERLAY_MENU) c->overlay_timer += dt;
    else c->overlay_timer = 0;
    if (c->overlay_timer > 1.5) c->locks.frame = false;

    Controller_cull_registry(c);
    Controller_set_HID_flags(c, Controller_update_axis(c, dt));

    if (c->HID.pointer && !(c->HID.mouse || c->HID.touch) && !c->interrupt.focus) G.CURSOR->n.states.visible = true;
    else G.CURSOR->n.states.visible = false;

    Controller_set_cursor_position(c);

    if (!G.screenwipe) {
        each_key(&c->pressed_keys, c, dt, key_press_update);
        each_key(&c->held_keys, c, dt, key_hold_update);
        each_key(&c->released_keys, c, dt, key_release_update);
        each_key(&c->pressed_buttons, c, dt, button_press_update);
        each_key(&c->held_buttons, c, dt, button_hold_update);
        each_key(&c->released_buttons, c, dt, button_release_update);
    }
    c->frame_buttonpress = false;
    strset_clear(&c->pressed_keys);
    strset_clear(&c->released_keys);
    strset_clear(&c->pressed_buttons);
    strset_clear(&c->released_buttons);

    if (c->HID.controller) {
        int L = c->cursor_context.layer;
        if (L - 1 < c->cursor_context.stack.count && L >= 1 && c->cursor_context.stack.items[L - 1].valid) {
            CursorContext *ctxp = &c->cursor_context.stack.items[L - 1];
            Node *node = (ctxp->node && !ctxp->node->REMOVED) ? ctxp->node : NULL;
            Controller_snap_to(c, node, &ctxp->cursor_pos);
            c->interrupt.stack = ctxp->interrupt;
            ctxp->valid = false;
        }
        if (c->dragging.prev_target && !c->dragging.target && IS(c->dragging.prev_target, NT_CARD) && !c->dragging.prev_target->REMOVED) {
            if (!c->COYOTE_FOCUS) Controller_snap_to_node(c, c->dragging.prev_target);
            else c->COYOTE_FOCUS = false;
        }
        if (c->snap_cursor_to.active) {
            c->interrupt.focus = c->interrupt.stack;
            c->interrupt.stack = false;
            if (c->snap_cursor_to.type == 1 && c->snap_cursor_to.node && !c->snap_cursor_to.node->REMOVED) {
                c->focused.prev_target = c->focused.target;
                c->focused.target = c->snap_cursor_to.node;
                Controller_update_cursor(c, NULL);
            } else if (c->snap_cursor_to.type == 2) {
                Controller_update_cursor(c, &c->snap_cursor_to.T);
            }
            if (c->focused.prev_target != c->focused.target && c->focused.prev_target) c->focused.prev_target->states.focus.is = false;
            c->snap_cursor_to.active = false;
        }
    }

    Controller_get_cursor_collision(c, G.CURSOR->n.T);
    Controller_update_focus(c, 0);
    Controller_set_cursor_hover(c);
    if (c->L_cursor_queue.set) {
        Controller_L_cursor_press(c, c->L_cursor_queue.x, c->L_cursor_queue.y, true);
        c->L_cursor_queue.set = false;
    }

    c->dragging.prev_target = c->dragging.target;
    c->released_on.prev_target = c->released_on.target;
    c->clicked.prev_target = c->clicked.target;
    c->hovering.prev_target = c->hovering.target;

    if (!c->cursor_down.handled) {
        if (c->cursor_down.target && c->cursor_down.target->states.drag.can) {
            c->cursor_down.target->states.drag.is = true;
            Node_set_offset(c->cursor_down.target, c->cursor_down.T, true);
            c->dragging.target = c->cursor_down.target;
            c->dragging.handled = false;
        }
        c->cursor_down.handled = true;
    }

    if (!c->cursor_up.handled) {
        if (c->dragging.target) {
            node_stop_drag(c->dragging.target);
            if (c->dragging.target) c->dragging.target->states.drag.is = false;
            c->dragging.target = NULL;
        }
        if (c->cursor_down.target) {
            Node *t = c->cursor_down.target;
            if (t->click_timeout == 0 || t->click_timeout * G.SPEEDFACTOR > c->cursor_up.time - c->cursor_down.time) {
                if (Vector_Dist(c->cursor_down.T, c->cursor_up.T) < G.MIN_CLICK_DIST) {
                    if (t->states.click.can) {
                        c->clicked.target = t;
                        c->clicked.handled = false;
                    }
                } else if (c->dragging.prev_target && c->cursor_up.target && c->cursor_up.target->states.release_on.can) {
                    c->released_on.target = c->cursor_up.target;
                    c->released_on.handled = false;
                }
            }
        }
        c->cursor_up.handled = true;
    }

    if (c->cursor_hover.target && c->cursor_hover.target->states.hover.can && (!c->HID.touch || c->is_cursor_down)) {
        c->hovering.target = c->cursor_hover.target;
        if (c->hovering.prev_target && c->hovering.prev_target != c->hovering.target) c->hovering.prev_target->states.hover.is = false;
        c->hovering.target->states.hover.is = true;
        Node_set_offset(c->hovering.target, c->cursor_hover.T, false);
    } else if ((c->cursor_hover.target == NULL || (c->HID.touch && !c->is_cursor_down)) && c->hovering.target) {
        c->hovering.target->states.hover.is = false;
        c->hovering.target = NULL;
    }

    if (!c->clicked.handled) {
        if (c->clicked.target) node_click(c->clicked.target);
        c->clicked.handled = true;
    }

    Controller_process_registry(c);

    if (c->dragging.target) node_drag(c->dragging.target, NULL);

    if (!c->released_on.handled && c->dragging.prev_target) {
        if (c->dragging.prev_target == c->hovering.target) { node_stop_hover(c->hovering.target); c->hovering.target = NULL; }
        if (c->released_on.target) node_release(c->released_on.target, c->dragging.prev_target);
        c->released_on.handled = true;
    }

    if (c->hovering.target) {
        Node_set_offset(c->hovering.target, c->cursor_hover.T, false);
        if (c->hovering.prev_target != c->hovering.target) {
            if (c->hovering.target != c->dragging.target && !c->HID.touch) {
                node_hover(c->hovering.target);
            } else if (c->HID.touch) {
                HoverCtx *h = CTX(HoverCtx);
                h->id = c->hovering.target->ID;
                add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .blocking = 0, .delay = G.MIN_HOVER_TIME, .func = ev_touch_hover, .ctx = h));
                if (c->hovering.prev_target) node_stop_hover(c->hovering.prev_target);
            }
            if (c->hovering.prev_target) node_stop_hover(c->hovering.prev_target);
        }
    } else if (c->hovering.prev_target) {
        node_stop_hover(c->hovering.prev_target);
    }
    if (c->hovering.target && c->hovering.target == c->dragging.target && !c->HID.touch) node_stop_hover(c->hovering.target);
}

void Controller_cull_registry(Controller *c) {
    for (int r = 0; r < c->button_registry.count; r++) {
        ButtonRegistry *reg = &c->button_registry.items[r];
        for (int i = reg->entries.count - 1; i >= 0; i--)
            if (!reg->entries.items[i].node || reg->entries.items[i].node->REMOVED) vec_remove_at(&reg->entries, i);
    }
}

static ButtonRegistry *find_registry(Controller *c, const char *name, bool create) {
    for (int i = 0; i < c->button_registry.count; i++)
        if (!strcmp(c->button_registry.items[i].name, name)) return &c->button_registry.items[i];
    if (!create) return NULL;
    ButtonRegistry r;
    memset(&r, 0, sizeof r);
    snprintf(r.name, sizeof r.name, "%s", name);
    vec_push(&c->button_registry, r);
    return &vec_last(&c->button_registry);
}

void Controller_add_to_registry(Controller *c, Node *node, const char *registry) {
    ButtonRegistry *r = find_registry(c, registry, true);
    RegistryEntry e = { node, (G.OVERLAY_MENU != NULL) || G.SETTINGS.paused, false };
    vec_insert(&r->entries, 0, e);
}

void Controller_process_registry(Controller *c) {
    for (int r = 0; r < c->button_registry.count; r++) {
        ButtonRegistry *reg = &c->button_registry.items[r];
        for (int i = 0; i < reg->entries.count; i++) {
            RegistryEntry *e = &reg->entries.items[i];
            if (e->click && e->node) {
                Node *nd = e->node;
                if (e->menu == (G.OVERLAY_MENU != NULL) && nd->T->x > -2 && nd->T->x < G.ROOM->T->w + 2 && nd->T->y > -2 && nd->T->y < G.ROOM->T->h + 2) {
                    e->click = false;
                    node_click(nd);
                    /* the registry may have changed under us */
                    if (r >= c->button_registry.count) return;
                    reg = &c->button_registry.items[r];
                    if (i >= reg->entries.count) break;
                    continue;
                }
                e->click = false;
            }
        }
    }
}

static void Controller_navigate(Controller *c, char dir) { Controller_navigate_focus(c, dir); }

void Controller_mod_cursor_context_layer(Controller *c, int delta) {
    if (delta == 1) {
        CursorContext prev = { c->focused.target, { G.CURSOR->n.T->x, G.CURSOR->n.T->y }, c->interrupt.focus, true };
        int L = c->cursor_context.layer;
        while (c->cursor_context.stack.count < L) { CursorContext z; memset(&z, 0, sizeof z); vec_push(&c->cursor_context.stack, z); }
        c->cursor_context.stack.items[L - 1] = prev;
        c->cursor_context.layer++;
    } else if (delta == -1) {
        int L = c->cursor_context.layer;
        if (L - 1 < c->cursor_context.stack.count && L >= 1) c->cursor_context.stack.items[L - 1].valid = false;
        c->cursor_context.layer--;
    } else if (delta == -1000) {
        c->cursor_context.layer = 1;
        if (c->cursor_context.stack.count > 1) c->cursor_context.stack.count = 1;
    } else if (delta == -2000) {
        c->cursor_context.layer = 1;
        vec_clear(&c->cursor_context.stack);
    }
    Controller_navigate(c, 0);
}

void Controller_snap_to(Controller *c, Node *node, Vec2 *T) {
    c->snap_cursor_to.active = true;
    c->snap_cursor_to.node = node;
    if (T) c->snap_cursor_to.T = *T;
    c->snap_cursor_to.type = node ? 1 : 2;
}
void Controller_snap_to_node(Controller *c, Node *node) { Controller_snap_to(c, node, NULL); if (!node) c->snap_cursor_to.active = false; }
void Controller_snap_to_T(Controller *c, Vec2 T) { Controller_snap_to(c, NULL, &T); }

bool Controller_save_cardarea_focus(Controller *c, const char *name) {
    CardArea *area = cardarea_by_name(name);
    if (area) {
        Node *f = c->focused.target;
        if (f && IS(f, NT_CARD) && ((Card *)f)->area == area) {
            strmap_set(&c->cardarea_context, name, (void *)(intptr_t)((Card *)f)->rank);
            return true;
        }
        strmap_del(&c->cardarea_context, name);
    }
    return false;
}

void Controller_recall_cardarea_focus_area(Controller *c, CardArea *area) {
    if (area && (!c->focused.target || c->interrupt.focus ||
                 (!c->interrupt.focus && IS(c->focused.target, NT_CARD) && ((Card *)c->focused.target)->area == area))) {
        if (area->cards.count > 0) {
            Controller_snap_to_node(c, &area->cards.items[0]->n);
            c->interrupt.focus = false;
        }
    }
}

void Controller_recall_cardarea_focus(Controller *c, const char *name) {
    CardArea *area = cardarea_by_name(name);
    if (area && (!c->focused.target || c->interrupt.focus ||
                 (!c->interrupt.focus && IS(c->focused.target, NT_CARD) && ((Card *)c->focused.target)->area == area))) {
        intptr_t ctx = (intptr_t)strmap_get(&c->cardarea_context, name);
        if (ctx) {
            for (int i = (int)ctx; i >= 1; i--) {
                if (i - 1 < area->cards.count) {
                    Controller_snap_to_node(c, &area->cards.items[i - 1]->n);
                    c->interrupt.focus = false;
                    break;
                }
            }
        } else if (area->cards.count > 0) {
            Controller_snap_to_node(c, &area->cards.items[0]->n);
            c->interrupt.focus = false;
        }
    }
    strmap_del(&c->cardarea_context, name);
}

void Controller_update_cursor(Controller *c, Vec2 *hard) {
    if (hard) {
        G.CURSOR->n.T->x = hard->x;
        G.CURSOR->n.T->y = hard->y;
        c->cursor_position.x = G.CURSOR->n.T->x * (G.TILESCALE * G.TILESIZE);
        c->cursor_position.y = G.CURSOR->n.T->y * (G.TILESCALE * G.TILESIZE);
        G.CURSOR->n.VT.x = G.CURSOR->n.T->x;
        G.CURSOR->n.VT.y = G.CURSOR->n.T->y;
        return;
    }
    if (c->focused.target) {
        node_put_focused_cursor(c->focused.target, &c->cursor_position.x, &c->cursor_position.y);
        G.CURSOR->n.T->x = c->cursor_position.x / (G.TILESCALE * G.TILESIZE);
        G.CURSOR->n.T->y = c->cursor_position.y / (G.TILESCALE * G.TILESIZE);
        G.CURSOR->n.VT.x = G.CURSOR->n.T->x;
        G.CURSOR->n.VT.y = G.CURSOR->n.T->y;
    }
}

static void handle_axis_buttons(Controller *c) {
    for (int i = 0; i < 4; i++) {
        char *prev = c->axis_buttons[i].previous, *cur = c->axis_buttons[i].current;
        if (prev[0] && (!cur[0] || strcmp(prev, cur) != 0)) Controller_button_release(c, prev);
        if (cur[0] && strcmp(prev, cur) != 0) Controller_button_press(c, cur);
    }
}

static const char *Controller_update_axis(Controller *c, double dt) {
    const char *interp = NULL;
    for (int i = 0; i < 4; i++) {
        strcpy(c->axis_buttons[i].previous, c->axis_buttons[i].current);
        c->axis_buttons[i].current[0] = 0;
    }
    if (c->HID.controller && c->GAMEPAD.object >= 0) {
        int gp = c->GAMEPAD.object;
        double lx = gfx_gamepad_axis(gp, "leftx"), ly = gfx_gamepad_axis(gp, "lefty");
        if (c->dragging.target && fabs(lx) + fabs(ly) > 0.1) {
            interp = "axis_cursor";
            if (fabs(lx) < 0.1) lx = 0;
            if (fabs(ly) < 0.1) ly = 0;
            lx = lx + (lx > 0 ? -0.1 : 0) + (lx < 0 ? 0.1 : 0);
            ly = ly + (ly > 0 ? -0.1 : 0) + (ly < 0 ? 0.1 : 0);
            G.CURSOR->n.T->x += dt * lx * c->axis_cursor_speed;
            G.CURSOR->n.T->y += dt * ly * c->axis_cursor_speed;
            G.CURSOR->n.VT.x = G.CURSOR->n.T->x;
            G.CURSOR->n.VT.y = G.CURSOR->n.T->y;
            c->cursor_position.x = G.CURSOR->n.T->x * (G.TILESCALE * G.TILESIZE);
            c->cursor_position.y = G.CURSOR->n.T->y * (G.TILESCALE * G.TILESIZE);
        } else {
            strcpy(c->axis_buttons[AX_LSTICK].current, c->axis_buttons[AX_LSTICK].previous);
            if (fabs(lx) + fabs(ly) > 0.5) {
                interp = "button";
                strcpy(c->axis_buttons[AX_LSTICK].current, fabs(lx) > fabs(ly) ? (lx > 0 ? "dpright" : "dpleft") : (ly > 0 ? "dpdown" : "dpup"));
            } else if (fabs(lx) + fabs(ly) < 0.3) c->axis_buttons[AX_LSTICK].current[0] = 0;
        }
        double rx = gfx_gamepad_axis(gp, "rightx"), ry = gfx_gamepad_axis(gp, "righty");
        double DZ = 0.2;
        double mag = sqrt(rx * rx + ry * ry);
        if (mag > DZ) {
            interp = "axis_cursor";
            if (fabs(rx) < DZ) rx = 0;
            if (fabs(ry) < DZ) ry = 0;
            rx = rx + (rx > 0 ? -DZ : 0) + (rx < 0 ? DZ : 0);
            ry = ry + (ry > 0 ? -DZ : 0) + (ry < 0 ? DZ : 0);
            G.CURSOR->n.T->x += dt * rx * c->axis_cursor_speed;
            G.CURSOR->n.T->y += dt * ry * c->axis_cursor_speed;
            G.CURSOR->n.VT.x = G.CURSOR->n.T->x;
            G.CURSOR->n.VT.y = G.CURSOR->n.T->y;
            c->cursor_position.x = G.CURSOR->n.T->x * (G.TILESCALE * G.TILESIZE);
            c->cursor_position.y = G.CURSOR->n.T->y * (G.TILESCALE * G.TILESIZE);
        }
        double lt = gfx_gamepad_axis(gp, "triggerleft"), rt = gfx_gamepad_axis(gp, "triggerright");
        strcpy(c->axis_buttons[AX_LTRIG].current, c->axis_buttons[AX_LTRIG].previous);
        strcpy(c->axis_buttons[AX_RTRIG].current, c->axis_buttons[AX_RTRIG].previous);
        if (lt > 0.5) strcpy(c->axis_buttons[AX_LTRIG].current, "triggerleft");
        else if (lt < 0.3) c->axis_buttons[AX_LTRIG].current[0] = 0;
        if (rt > 0.5) strcpy(c->axis_buttons[AX_RTRIG].current, "triggerright");
        else if (rt < 0.3) c->axis_buttons[AX_RTRIG].current[0] = 0;
        if (c->axis_buttons[AX_RTRIG].current[0] || c->axis_buttons[AX_LTRIG].current[0]) interp = interp ? interp : "button";
        handle_axis_buttons(c);
    }
    if (interp) c->interrupt.focus = false;
    return interp;
}

static void button_press_update(Controller *c, const char *button, double dt) {
    if (c->locks.frame) return;
    nummap_set(&c->held_button_times, button, 0);
    c->interrupt.focus = false;
    if (!Controller_capture_focused_input(c, button, "press", dt)) {
        if (!strcmp(button, "dpup")) Controller_navigate_focus(c, 'U');
        if (!strcmp(button, "dpdown")) Controller_navigate_focus(c, 'D');
        if (!strcmp(button, "dpleft")) Controller_navigate_focus(c, 'L');
        if (!strcmp(button, "dpright")) Controller_navigate_focus(c, 'R');
    }
    if ((c->locked && !G.SETTINGS.paused) || c->locks.frame || c->frame_buttonpress) return;
    c->frame_buttonpress = true;
    ButtonRegistry *reg = find_registry(c, button, false);
    if (reg && reg->entries.count > 0 && !reg->entries.items[0].node->under_overlay) {
        reg->entries.items[0].click = true;
    } else {
        if (!strcmp(button, "start")) {
            if (G.STATE == STATE_SPLASH) { game_delete_run(); game_main_menu(NULL); }
        }
        if (!strcmp(button, "a")) {
            Node *f = c->focused.target;
            bool slider = f && IS(f, NT_UIELEMENT) && ((UIElement *)f)->config->focus_args && ((UIElement *)f)->config->focus_args->type &&
                          !strcmp(((UIElement *)f)->config->focus_args->type, "slider") && (!G.CONTROLLER.HID.mouse && !G.CONTROLLER.HID.axis_cursor);
            if (!slider) Controller_L_cursor_press(c, 0, 0, false);
        }
        if (!strcmp(button, "b")) {
            if (G.hand && c->focused.target && IS(c->focused.target, NT_CARD) && ((Card *)c->focused.target)->area == G.hand)
                Controller_queue_R_cursor_press(c, 0, 0);
            else c->interrupt.focus = true;
        }
    }
}

static void button_hold_update(Controller *c, const char *button, double dt) {
    if ((c->locked && !G.SETTINGS.paused) || c->locks.frame || c->frame_buttonpress) return;
    c->frame_buttonpress = true;
    if (nummap_has(&c->held_button_times, button)) {
        nummap_set(&c->held_button_times, button, nummap_get(&c->held_button_times, button, 0) + dt);
        Controller_capture_focused_input(c, button, "hold", dt);
    }
    if ((!strcmp(button, "dpleft") || !strcmp(button, "dpright") || !strcmp(button, "dpup") || !strcmp(button, "dpdown")) && !c->no_holdcap) {
        if (!c->repress_timer_set) { c->repress_timer = 0.3; c->repress_timer_set = true; }
        if (nummap_has(&c->held_button_times, button) && nummap_get(&c->held_button_times, button, 0) > c->repress_timer) {
            c->repress_timer = 0.1;
            nummap_set(&c->held_button_times, button, 0);
            button_press_update(c, button, dt);
        }
    }
}

static void button_release_update(Controller *c, const char *button, double dt) {
    (void)dt;
    if (!nummap_has(&c->held_button_times, button)) return;
    c->repress_timer = 0.3;
    c->repress_timer_set = true;
    nummap_del(&c->held_button_times, button);
    if (!strcmp(button, "a")) Controller_L_cursor_release(c, 0, 0, false);
}

void Controller_key_press_update(Controller *c, const char *key_in, double dt) {
    (void)dt;
    if (c->locks.frame) return;
    char key[32];
    snprintf(key, sizeof key, "%s", key_in);
    if (!strncmp(key, "kp", 2)) memmove(key, key + 2, strlen(key + 2) + 1);
    if (!strcmp(key, "enter")) strcpy(key, "return");
    if (c->text_input_hook) {
        if (!strcmp(key, "escape")) c->text_input_hook = NULL;
        else if (!strcmp(key, "capslock")) c->capslock = !c->capslock;
        else G_FUNCS_text_input_key(c->text_input_hook, key, strset_has(&c->held_keys, "lshift") || strset_has(&c->held_keys, "rshift"));
        return;
    }
    if (!strcmp(key, "escape")) {
        if (G.STATE == STATE_SPLASH) { game_delete_run(); game_main_menu(NULL); }
        else {
            if (!G.OVERLAY_MENU) G_FUNCS_options(NULL);
            else if (!G.OVERLAY_MENU->no_esc) G_FUNCS_exit_overlay_menu(NULL);
        }
    }
    if ((c->locked && !G.SETTINGS.paused) || c->locks.frame || c->frame_buttonpress) return;
    c->frame_buttonpress = true;
    nummap_set(&c->held_key_times, key, 0);
}

static void key_hold_update(Controller *c, const char *key, double dt) {
    if ((c->locked && !G.SETTINGS.paused) || c->locks.frame || c->frame_buttonpress) return;
    if (nummap_has(&c->held_key_times, key)) {
        if (!strcmp(key, "r") && !G.SETTINGS.paused) {
            if (nummap_get(&c->held_key_times, key, 0) > 0.7) {
                game_quick_restart();
                nummap_del(&c->held_key_times, key);
            } else nummap_set(&c->held_key_times, key, nummap_get(&c->held_key_times, key, 0) + dt);
        }
    }
}

static void key_release_update(Controller *c, const char *key, double dt) {
    (void)key; (void)dt;
    if ((c->locked && !G.SETTINGS.paused) || c->locks.frame || c->frame_buttonpress) return;
    c->frame_buttonpress = true;
}

void Controller_key_press(Controller *c, const char *key) {
    strset_add(&c->pressed_keys, key);
    strset_add(&c->held_keys, key);
}
void Controller_key_release(Controller *c, const char *key) {
    strset_del(&c->held_keys, key);
    strset_add(&c->released_keys, key);
}
void Controller_button_press(Controller *c, const char *b) {
    strset_add(&c->pressed_buttons, b);
    strset_add(&c->held_buttons, b);
}
void Controller_button_release(Controller *c, const char *b) {
    strset_del(&c->held_buttons, b);
    strset_add(&c->released_buttons, b);
}

void Controller_get_cursor_collision(Controller *c, Transform *ct) {
    vec_clear(&c->collision_list);
    vec_clear(&c->nodes_at_cursor);
    if (c->COYOTE_FOCUS) return;
    if (c->dragging.target) {
        c->dragging.target->states.collide.is = true;
        vec_push(&c->nodes_at_cursor, c->dragging.target);
        vec_push(&c->collision_list, c->dragging.target);
    }
    if (G.DRAW_HASH.count == 0 ||
        ct->x - G.ROOM->T->x < -G.DRAW_HASH_BUFF || ct->x - G.ROOM->T->x > G.TILE_W + G.DRAW_HASH_BUFF ||
        ct->y - G.ROOM->T->y < -G.DRAW_HASH_BUFF || ct->y - G.ROOM->T->y > G.TILE_H + G.DRAW_HASH_BUFF) return;
    Vec2 p = { ct->x, ct->y };
    for (int i = G.DRAW_HASH.count - 1; i >= 0; i--) {
        Node *v = G.DRAW_HASH.items[i];
        if (!v || v->REMOVED) continue;
        if (node_collides(v, p)) {
            vec_push(&c->nodes_at_cursor, v);
            if (v->states.collide.can) {
                v->states.collide.is = true;
                vec_push(&c->collision_list, v);
            }
        }
    }
}

void Controller_set_cursor_hover(Controller *c) {
    c->cursor_hover.T.x = G.CURSOR->n.T->x;
    c->cursor_hover.T.y = G.CURSOR->n.T->y;
    c->cursor_hover.time = G.TIMERS.TOTAL;
    c->cursor_hover.prev_target = c->cursor_hover.target;
    c->cursor_hover.target = NULL;
    if (c->interrupt.focus || (c->locked && (!G.SETTINGS.paused || G.screenwipe)) || c->locks.frame || c->COYOTE_FOCUS) {
        c->cursor_hover.target = G.ROOM;
        return;
    }
    if (c->HID.controller && c->focused.target && c->focused.target->states.hover.can) {
        if ((c->HID.dpad || c->HID.axis_cursor) && c->focused.target->states.collide.is) c->cursor_hover.target = c->focused.target;
        else {
            for (int i = 0; i < c->collision_list.count; i++) {
                Node *v = c->collision_list.items[i];
                if (v->states.hover.can) { c->cursor_hover.target = v; break; }
            }
        }
    } else {
        for (int i = 0; i < c->collision_list.count; i++) {
            Node *v = c->collision_list.items[i];
            if (v->states.hover.can && (!v->states.drag.is || c->HID.touch)) { c->cursor_hover.target = v; break; }
        }
    }
    if (!c->cursor_hover.target || (c->dragging.target && !c->HID.touch)) c->cursor_hover.target = G.ROOM;
    if (c->cursor_hover.target != c->cursor_hover.prev_target) c->cursor_hover.handled = false;
}

void Controller_queue_L_cursor_press(Controller *c, double x, double y) {
    if (c->locks.frame) return;
    if (G.STATE == STATE_SPLASH) Controller_key_press(c, "escape");
    c->L_cursor_queue.x = x;
    c->L_cursor_queue.y = y;
    c->L_cursor_queue.set = true;
}

void Controller_queue_R_cursor_press(Controller *c, double x, double y) {
    (void)x; (void)y;
    if (c->locks.frame) return;
    if (!G.SETTINGS.paused && G.hand && G.hand->highlighted.count > 0) {
        if ((G.play && G.play->cards.count > 0) || c->locked || c->locks.frame || (G.GAME.STOP_USE > 0)) return;
        CardArea_unhighlight_all(G.hand);
    }
}

void Controller_L_cursor_press(Controller *c, double x, double y, bool has) {
    if (!has) { x = c->cursor_position.x; y = c->cursor_position.y; }
    if ((c->locked && (!G.SETTINGS.paused || G.screenwipe)) || c->locks.frame) return;
    c->cursor_down.T.x = x / (G.TILESCALE * G.TILESIZE);
    c->cursor_down.T.y = y / (G.TILESCALE * G.TILESIZE);
    c->cursor_down.time = G.TIMERS.TOTAL;
    c->cursor_down.handled = false;
    c->cursor_down.target = NULL;
    c->is_cursor_down = true;
    Node *press = (c->HID.touch && c->cursor_hover.target) ? c->cursor_hover.target : (c->hovering.target ? c->hovering.target : c->focused.target);
    if (press) c->cursor_down.target = press->states.click.can ? press : node_can_drag(press);
    if (!c->cursor_down.target) c->cursor_down.target = G.ROOM;
}

void Controller_L_cursor_release(Controller *c, double x, double y, bool has) {
    if (!has) { x = c->cursor_position.x; y = c->cursor_position.y; }
    if ((c->locked && (!G.SETTINGS.paused || G.screenwipe)) || c->locks.frame) return;
    c->cursor_up.T.x = x / (G.TILESCALE * G.TILESIZE);
    c->cursor_up.T.y = y / (G.TILESCALE * G.TILESIZE);
    c->cursor_up.time = G.TIMERS.TOTAL;
    c->cursor_up.handled = false;
    c->cursor_up.target = NULL;
    c->is_cursor_down = false;
    c->cursor_up.target = c->hovering.target ? c->hovering.target : c->focused.target;
    if (!c->cursor_up.target) c->cursor_up.target = G.ROOM;
}

static UIConfig *node_uicfg(Node *n) { return IS(n, NT_UIELEMENT) ? ((UIElement *)n)->config : NULL; }

bool Controller_is_node_focusable(Controller *c, Node *node) {
    bool ret = false;
    if (node->T->y > G.ROOM->T->h + 3) return false;
    UIBox *ub = IS(node, NT_UIELEMENT) ? ((UIElement *)node)->UIBox : NULL;
    if (!node->REMOVED && !node->under_overlay && ((node->states.hover.can && !c->dragging.target) || c->dragging.target == node) &&
        (node->created_on_pause == G.SETTINGS.paused) && node->states.visible && (!ub || ub->n.states.visible)) {
        UIConfig *cfg = node_uicfg(node);
        if (c->screen_keyboard) {
            if (ub && ub == c->screen_keyboard && cfg && cfg->button) ret = true;
        } else {
            if (IS(node, NT_CARD)) {
                Card *cd = (Card *)node;
                if ((card_facing_front(cd) || cd->area == G.hand || cd->area == G.jokers || (node == &G.deck->n)) && node->states.hover.can && !cd->jimbo) ret = true;
            }
            if (cfg && cfg->force_focus) ret = true;
            if (cfg && cfg->button) ret = true;
            if (cfg && cfg->focus_args) {
                if ((cfg->focus_args->type && !strcmp(cfg->focus_args->type, "none")) || cfg->focus_args->funnel_from_set) ret = false;
                else ret = true;
            }
        }
    }
    return ret;
}

typedef struct { Node *node; double dist; } FocusEntry;
static int focus_cmp(const void *a, const void *b) {
    const FocusEntry *x = a, *y = b;
    return x->dist < y->dist ? -1 : x->dist > y->dist ? 1 : 0;
}

void Controller_update_focus(Controller *c, char dir) {
    c->focused.prev_target = c->focused.target;
    if (!c->HID.controller || c->interrupt.focus || (c->locked && (!G.SETTINGS.paused || G.screenwipe))) {
        if (c->focused.target) c->focused.target->states.focus.is = false;
        c->focused.target = NULL;
        return;
    }
    PtrVec focusables = { 0 };
    VEC(FocusEntry) list = { 0 };
    if (c->focused.target) {
        c->focused.target->states.focus.is = false;
        Vec2 cp = { G.CURSOR->n.T->x, G.CURSOR->n.T->y };
        if (!Controller_is_node_focusable(c, c->focused.target) || !node_collides(c->focused.target, cp) || c->HID.axis_cursor)
            c->focused.target = NULL;
    }
    if (!dir && c->focused.target) {
        c->focused.target->states.focus.can = true;
        vec_push(&focusables, c->focused.target);
    } else {
        if (!dir) {
            for (int i = 0; i < c->nodes_at_cursor.count; i++) {
                Node *v = c->nodes_at_cursor.items[i];
                v->states.focus.can = false;
                v->states.focus.is = false;
                if (focusables.count == 0 && Controller_is_node_focusable(c, v)) {
                    v->states.focus.can = true;
                    vec_push(&focusables, v);
                }
            }
        } else {
            for (int i = 0; i < G.MOVEABLES.count; i++) {
                Node *v = G.MOVEABLES.items[i];
                v->states.focus.can = false;
                v->states.focus.is = false;
                if (Controller_is_node_focusable(c, v)) {
                    v->states.focus.can = true;
                    vec_push(&focusables, v);
                }
            }
        }
    }
    if (focusables.count > 0) {
        if (dir) {
            Node *ft = c->focused.target;
            if ((dir == 'L' || dir == 'R') && ft && IS(ft, NT_CARD) && G.hand && ((Card *)ft)->area == G.hand) {
                Card *fc = (Card *)ft;
                int nu = fc->rank + (dir == 'L' ? -1 : 1);
                if (nu > G.hand->cards.count) nu = 1;
                if (nu == 0) nu = G.hand->cards.count;
                if (nu != fc->rank && nu >= 1 && nu <= G.hand->cards.count) { FocusEntry fe = { &G.hand->cards.items[nu - 1]->n, 0 }; vec_push(&list, fe); }
            } else {
                Vec2 fcp = { G.CURSOR->n.T->x - G.ROOM->T->x, G.CURSOR->n.T->y - G.ROOM->T->y };
                if (ft) {
                    Node *_ft = ft;
                    UIConfig *fcfg = node_uicfg(ft);
                    if (fcfg && fcfg->focus_args && fcfg->focus_args->funnel_to) _ft = &fcfg->focus_args->funnel_to->n;
                    fcp.x = _ft->T->x + 0.5 * _ft->T->w;
                    fcp.y = _ft->T->y + 0.5 * _ft->T->h;
                } else if (c->hovering.target && c->hovering.target->states.focus.can) {
                    node_put_focused_cursor(c->hovering.target, &fcp.x, &fcp.y);
                    fcp.x = fcp.x / (G.TILESCALE * G.TILESIZE) - G.ROOM->T->x;
                    fcp.y = fcp.y / (G.TILESCALE * G.TILESIZE) - G.ROOM->T->y;
                }
                for (int i = 0; i < focusables.count; i++) {
                    Node *v = focusables.items[i];
                    if (v == c->hovering.target || v == c->focused.target) continue;
                    bool eligible = false;
                    UIConfig *vc = node_uicfg(v);
                    if (vc && vc->focus_args && vc->focus_args->funnel_to) { v = &vc->focus_args->funnel_to->n; vc = node_uicfg(v); }
                    Vec2 fv = { v->T->x + 0.5 * v->T->w - fcp.x, v->T->y + 0.5 * v->T->h - fcp.y };
                    if (vc && vc->focus_args && vc->focus_args->nav) {
                        if (!strcmp(vc->focus_args->nav, "wide")) {
                            if (fv.y > 0.1 && dir == 'D') eligible = true;
                            else if (fv.y < -0.1 && dir == 'U') eligible = true;
                            else if (fabs(fv.y) < v->T->h / 2) eligible = true;
                        } else if (!strcmp(vc->focus_args->nav, "tall")) {
                            if (fv.x > 0.1 && dir == 'R') eligible = true;
                            else if (fv.x < -0.1 && dir == 'L') eligible = true;
                            else if (fabs(fv.x) < v->T->w / 2) eligible = true;
                        }
                    } else if (fabs(fv.x) > fabs(fv.y)) {
                        if (fv.x > 0 && dir == 'R') eligible = true;
                        else if (fv.x < 0 && dir == 'L') eligible = true;
                    } else {
                        if (fv.y > 0 && dir == 'D') eligible = true;
                        else if (fv.y < 0 && dir == 'U') eligible = true;
                    }
                    if (eligible) { FocusEntry fe = { v, fabs(fv.x) + fabs(fv.y) }; vec_push(&list, fe); }
                }
                if (list.count < 1) {
                    if (c->focused.target) c->focused.target->states.focus.is = true;
                    vec_free(&focusables); vec_free(&list);
                    return;
                }
                qsort(list.items, (size_t)list.count, sizeof(FocusEntry), focus_cmp);
            }
        } else {
            FocusEntry fe = { c->focused.target ? c->focused.target : focusables.items[0], 0 };
            vec_push(&list, fe);
        }
    }
    if (list.count > 0) {
        Node *first = list.items[0].node;
        UIConfig *fc = node_uicfg(first);
        if (fc && fc->focus_args && fc->focus_args->funnel_from) c->focused.target = &fc->focus_args->funnel_from->n;
        else c->focused.target = first;
        if (c->focused.target != c->focused.prev_target) G.VIBRATION += 0.7;
    } else c->focused.target = NULL;
    if (c->focused.target) c->focused.target->states.focus.is = true;
    vec_free(&focusables);
    vec_free(&list);
}

static int rank_cmp(const void *a, const void *b) {
    const Card *x = *(Card *const *)a, *y = *(Card *const *)b;
    return x->rank - y->rank;
}

bool Controller_capture_focused_input(Controller *c, const char *button, const char *input_type, double dt) {
    bool ret = false;
    Node *focused = c->focused.target;
    bool extern_button = false;
    c->no_holdcap = false;
    bool press = !strcmp(input_type, "press");
    Card *fcard = IS(focused, NT_CARD) ? (Card *)focused : NULL;
    if (press && (!strcmp(button, "dpleft") || !strcmp(button, "dpright")) && fcard && c->dragging.target &&
        nummap_has(&c->held_button_times, "a") && nummap_get(&c->held_button_times, "a", 1) < 0.12 && fcard->area && CardArea_can_highlight(fcard->area, fcard)) {
        Controller_L_cursor_release(c, 0, 0, false);
        Controller_navigate_focus(c, !strcmp(button, "dpleft") ? 'L' : 'R');
        nummap_del(&c->held_button_times, "a");
        c->COYOTE_FOCUS = true;
        ret = true;
    } else if (press && fcard && fcard->area && focused == c->dragging.target) {
        focused->states.drag.is = false;
        CardArea *a = fcard->area;
        if (!strcmp(button, "dpleft") && fcard->rank > 1) {
            fcard->rank--;
            a->cards.items[fcard->rank - 1]->rank = fcard->rank + 1;
            qsort(a->cards.items, (size_t)a->cards.count, sizeof(Card *), rank_cmp);
            CardArea_align_cards(a);
            Controller_update_cursor(c, NULL);
        } else if (!strcmp(button, "dpright") && fcard->rank < a->cards.count) {
            fcard->rank++;
            a->cards.items[fcard->rank - 1]->rank = fcard->rank - 1;
            qsort(a->cards.items, (size_t)a->cards.count, sizeof(Card *), rank_cmp);
            CardArea_align_cards(a);
            Controller_update_cursor(c, NULL);
        }
        focused->states.drag.is = true;
        ret = true;
    }
    bool shoulder = !strcmp(button, "leftshoulder") || !strcmp(button, "rightshoulder");
    if (G.OVERLAY_MENU && !c->screen_keyboard && press && shoulder) {
        UIElement *t = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "tab_shoulders", NULL);
        if (t) { focused = &t->n; extern_button = true; }
        UIElement *cy = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "cycle_shoulders", NULL);
        if (cy) { UIElement *c1 = uie_child(cy, 1); if (c1) { focused = &c1->n; extern_button = true; } }
    }
    UIConfig *fcfg = focused ? node_uicfg(focused) : NULL;
    if (fcfg && fcfg->focus_args && fcfg->focus_args->type) {
        UIElement *fe = (UIElement *)focused;
        const char *ty = fcfg->focus_args->type;
        bool left = (extern_button && !strcmp(button, "leftshoulder")) || (!extern_button && !strcmp(button, "dpleft"));
        bool right = (extern_button && !strcmp(button, "rightshoulder")) || (!extern_button && !strcmp(button, "dpright"));
        if (!strcmp(ty, "cycle") && press) {
            if (left) { UIElement *ch = uie_child(fe, 1); if (ch) node_click(&ch->n); ret = true; }
            if (right) { UIElement *ch = uie_child(fe, 3); if (ch) node_click(&ch->n); ret = true; }
        }
        if (!strcmp(ty, "tab") && press) {
            UIElement *c1 = uie_child(fe, 1);
            UIElement *c11 = c1 ? uie_child(c1, 1) : NULL;
            PtrVec proto = { 0 }, choices = { 0 };
            if (c11) UIBox_get_group(fe->UIBox, NULL, c11->config->group, &proto);
            for (int i = 0; i < proto.count; i++) {
                UIElement *v = proto.items[i];
                if (v->config->choice && v->config->button) vec_push(&choices, v);
            }
            for (int k = 1; k <= choices.count; k++) {
                UIElement *v = choices.items[k - 1];
                if (v->config->chosen) {
                    if (left) {
                        int ni = k != 1 ? k - 1 : choices.count;
                        if (fcfg->focus_args->no_loop && ni > k) ret = false;
                        else {
                            UIElement *ch = choices.items[ni - 1];
                            node_click(&ch->n);
                            Controller_snap_to_node(c, &ch->n);
                            Controller_update_cursor(c, NULL);
                            ret = true;
                        }
                    } else if (right) {
                        int ni = k != choices.count ? k + 1 : 1;
                        if (fcfg->focus_args->no_loop && ni < k) ret = false;
                        else {
                            UIElement *ch = choices.items[ni - 1];
                            node_click(&ch->n);
                            Controller_snap_to_node(c, &ch->n);
                            Controller_update_cursor(c, NULL);
                            ret = true;
                        }
                    }
                    break;
                }
            }
            vec_free(&proto); vec_free(&choices);
        }
        if (!strcmp(ty, "slider")) {
            UIElement *ch = uie_child(fe, 1);
            double held = nummap_get(&c->held_button_times, button, 0);
            if (!strcmp(button, "dpleft")) {
                c->no_holdcap = true;
                if (!strcmp(input_type, "hold") && held > 0.2) G_FUNCS_slider_descreet(ch, -dt * held * 0.6);
                if (press) G_FUNCS_slider_descreet(ch, -0.01);
                ret = true;
            }
            if (!strcmp(button, "dpright")) {
                c->no_holdcap = true;
                if (!strcmp(input_type, "hold") && held > 0.2) G_FUNCS_slider_descreet(ch, dt * held * 0.6);
                if (press) G_FUNCS_slider_descreet(ch, 0.01);
                ret = true;
            }
        }
    }
    if (ret) G.VIBRATION += 1;
    return ret;
}

void Controller_navigate_focus(Controller *c, char dir) {
    Controller_update_focus(c, dir);
    Controller_update_cursor(c, NULL);
}
