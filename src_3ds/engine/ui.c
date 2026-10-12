/* ui.c - port of engine/ui.lua (UIBox / UIElement) */
#include "../game/game.h"

static UIBoxConfig EMPTY_BOX_CONFIG = { .bond = -1, .xy_bond = -1, .r_bond = -1, .wh_bond = -1, .scale_bond = -1 };

/* ------------------------------------------------------------------ */
/* config / definition helpers                                          */
/* ------------------------------------------------------------------ */
UIConfig *cfg_dup(const UIConfig *c) {
    UIConfig *r = NEW(UIConfig);
    *r = *c;
    if (c->text) r->text = xstrdup(c->text);
    return r;
}
UIConfig *cfg_new(void) { return CFG(.align = NULL); }
UIBoxConfig *uibcfg_dup(const UIBoxConfig *c) {
    UIBoxConfig *r = NEW(UIBoxConfig);
    *r = *c;
    return r;
}
FocusArgs *focus_dup(const FocusArgs *f) {
    FocusArgs *r = NEW(FocusArgs);
    *r = *f;
    return r;
}

UIDef *uidef(int n, UIConfig *config, ...) {
    UIDef *d = NEW(UIDef);
    d->n = n;
    d->config = config ? config : cfg_new();
    va_list ap;
    va_start(ap, config);
    UIDef *c;
    while ((c = va_arg(ap, UIDef *)) != NULL) uidef_add(d, c);
    va_end(ap);
    return d;
}
UIDef *uidef_list(int n, UIConfig *config, UIDef **children, int count) {
    UIDef *d = uidef(n, config, (UIDef *)NULL);
    for (int i = 0; i < count; i++) if (children[i]) uidef_add(d, children[i]);
    return d;
}
UIDef uidef_skip_sentinel;
UIDef *uidef_add(UIDef *p, UIDef *c) {
    if (!p || !c || c == UISKIP) return p;
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 4;
        p->nodes = xrealloc(p->nodes, (size_t)p->cap * sizeof(UIDef *));
    }
    p->nodes[p->count++] = c;
    return p;
}
UIDef *uidef_add_all(UIDef *p, UIDef *list) {
    if (!list) return p;
    for (int i = 0; i < list->count; i++) uidef_add(p, list->nodes[i]);
    return p;
}
UIDef *uinodes(void) { return uidef(0, NULL, (UIDef *)NULL); }

/* ------------------------------------------------------------------ */
/* G.FUNCS registry                                                    */
/* ------------------------------------------------------------------ */
typedef struct { char *name; UIFunc f; } FuncEntry;
static VEC(FuncEntry) funcs;
void funcs_register(const char *name, UIFunc f) {
    for (int i = 0; i < funcs.count; i++) if (!strcmp(funcs.items[i].name, name)) { funcs.items[i].f = f; return; }
    FuncEntry e = { xstrdup(name), f };
    vec_push(&funcs, e);
}
UIFunc funcs_get(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < funcs.count; i++) if (!strcmp(funcs.items[i].name, name)) return funcs.items[i].f;
    return NULL;
}
const char *funcs_name(UIFunc f) {
    for (int i = 0; i < funcs.count; i++) if (funcs.items[i].f == f) return funcs.items[i].name;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* UIElement helpers                                                   */
/* ------------------------------------------------------------------ */
UIElement *uie_child(UIElement *e, int i) {
    int k = 0;
    for (int j = 0; j < e->n.children.count; j++) {
        Node *c = e->n.children.items[j].node;
        if (!e->n.children.items[j].name && IS(c, NT_UIELEMENT)) {
            if (++k == i) return (UIElement *)c;
        }
    }
    return NULL;
}
int uie_child_count(UIElement *e) {
    int k = 0;
    for (int j = 0; j < e->n.children.count; j++)
        if (!e->n.children.items[j].name && IS(e->n.children.items[j].node, NT_UIELEMENT)) k++;
    return k;
}
bool is_UI_containter(UIElement *e) { return e->UIT == UIT_C || e->UIT == UIT_R || e->UIT == UIT_ROOT; }

static Language *cfg_lang(UIConfig *c) {
    if (!c->lang) c->lang = G.LANG;
    return (Language *)c->lang;
}

static UIElement *uielement_new(UIElement *parent, UIBox *box, int uit, UIConfig *config) {
    UIElement *e = NEW(UIElement);
    e->n.vt = &UIELEMENT_VT;
    e->n.T = NULL;
    e->parent_uie = parent;
    e->n.parent = parent ? &parent->n : NULL;
    e->UIT = uit;
    e->UIBox = box;
    e->config = config ? config : cfg_new();
    if (e->config->object) e->config->object->parent = &e->n;
    e->last_clicked = -INFINITY;
    return e;
}

/* ------------------------------------------------------------------ */
/* UIBox                                                               */
/* ------------------------------------------------------------------ */
UIBox *uibox_new_T(UIDef *definition, UIBoxConfig *config, double x, double y, double w, double h) {
    (void)x; (void)y; (void)w; (void)h;
    return uibox_new(definition, config);
}

UIBox *uibox_new(UIDef *definition, UIBoxConfig *config) {
    UIBox *b = NEW(UIBox);
    /* Lua: Moveable.init(self, {args.T}) always ends up with T = {0,0,1,1} */
    moveable_init(&b->n, &UIBOX_VT, 0, 0, 1, 1);
    b->n.states.drag.can = false;
    b->definition = definition;
    b->config = config ? config : uibcfg_dup(&EMPTY_BOX_CONFIG);
    if (config) {
        if (!config->major) config->major = config->parent ? config->parent : &b->n;
        Moveable_set_alignment(&b->n, ALIGN_ARGS(
            .major = config->major,
            .type = config->align ? config->align : "",
            .bond = config->bond >= 0 ? config->bond : BOND_STRONG,
            .has_offset = true,
            .offset = config->has_offset ? config->offset : (Vec2){ 0, 0 },
            .lr_clamp = config->lr_clamp));
        Moveable_set_role(&b->n, ROLE_ARGS(
            .xy_bond = config->xy_bond,
            .r_bond = config->r_bond,
            .wh_bond = config->wh_bond >= 0 ? config->wh_bond : BOND_WEAK,
            .scale_bond = config->scale_bond >= 0 ? config->scale_bond : BOND_WEAK));
        b->n.states.collide.can = config->can_collide == 0 ? true : config->can_collide > 0;
        b->n.parent = config->parent;
    }
    UIBox_set_parent_child(b, definition, NULL);
    if (!b->n.Mid) b->n.Mid = &b->UIRoot->n;
    double w, h; bool ok;
    UIBox_calculate_xywh(b, b->UIRoot, b->n.T, false, 1, &w, &h, &ok);
    b->n.T->w = b->UIRoot->n.T->w;
    b->n.T->h = b->UIRoot->n.T->h;
    UIElement_set_wh(&b->UIRoot->n, &w, &h, &ok);
    UIElement_set_alignments(&b->UIRoot->n);
    Moveable_align_to_major(&b->n);
    b->n.VT.x = b->n.T->x; b->n.VT.y = b->n.T->y;
    b->n.VT.w = b->n.T->w; b->n.VT.h = b->n.T->h;
    if (b->n.alignment.lr_clamp) Moveable_lr_clamp(&b->n);
    UIElement_initialize_VT(&b->UIRoot->n);
    PtrVec *inst = instance_list(b->config->instance_type);
    vec_push(inst, &b->n);
    return b;
}

UIElement *UIBox_get_UIE_by_ID(UIBox *b, const char *id, UIElement *node) {
    if (!node) node = b->UIRoot;
    if (!node || !id) return NULL;
    if (node->config && node->config->id && !strcmp(node->config->id, id)) return node;
    for (int i = 0; i < node->n.children.count; i++) {
        Node *c = node->n.children.items[i].node;
        if (!IS(c, NT_UIELEMENT)) continue;
        UIElement *v = (UIElement *)c;
        UIElement *res = UIBox_get_UIE_by_ID(b, id, v);
        if (res) return res;
        if (v->config->object && IS(v->config->object, NT_UIBOX)) {
            res = UIBox_get_UIE_by_ID((UIBox *)v->config->object, id, NULL);
            if (res) return res;
        }
    }
    return NULL;
}

void UIBox_calculate_xywh(UIBox *b, UIElement *node, Transform *T, bool recalculate, double sc, double *ow, double *oh, bool *ok) {
    Transform nt = { 0, 0, 0, 0, 0, 1 };
    double ctx = 0, cty = 0, ctw = 0, cth = 0;
    UIConfig *cfg = node->config;
    double padding = cfg->padding;
    *ok = true;
    if (node->UIT == UIT_B || node->UIT == UIT_T || node->UIT == UIT_O) {
        nt.x = T->x; nt.y = T->y;
        bool wset = !isnan(cfg->w), hset = !isnan(cfg->h);
        nt.w = wset ? cfg->w : (cfg->object ? cfg->object->T->w : NAN);
        nt.h = hset ? cfg->h : (cfg->object ? cfg->object->T->h : NAN);
        if (node->UIT == UIT_T) {
            cfg->text_drawable = NULL;
            double scale = cfg->scale != 0 ? cfg->scale : 1;
            if (ref_valid(cfg->ref)) {
                str_set(&cfg->text, ref_tostring(cfg->ref));
                if (cfg->func && !recalculate) cfg->func(node);
            }
            if (!cfg->text) cfg->text = xstrdup("[UI ERROR]");
            Language *lang = cfg_lang(cfg);
            FontDef *f = lang->font;
            double tx = gfx_font_width(f->FONT, cfg->text) * f->squish * scale * G.TILESCALE * f->FONTSCALE;
            double ty = gfx_font_height(f->FONT) * scale * G.TILESCALE * f->FONTSCALE * f->TEXT_HEIGHT_SCALE;
            if (cfg->vert) { double t = tx; tx = ty; ty = t; }
            nt.w = tx / (G.TILESIZE * G.TILESCALE);
            nt.h = ty / (G.TILESIZE * G.TILESCALE);
            node->content_dimensions.w = T->w;
            node->content_dimensions.h = T->h;
            UIElement_set_values(node, &nt, recalculate);
        } else {
            node->content_dimensions.w = nt.w;
            node->content_dimensions.h = nt.h;
            UIElement_set_values(node, &nt, recalculate);
        }
        *ow = nt.w; *oh = nt.h;
        *ok = !isnan(nt.w) && !isnan(nt.h);
        return;
    }
    for (int pass = 1; pass <= 2; pass++) {
        if (pass == 1 || (pass == 2 && ((cfg->maxw != 0 && ctw > cfg->maxw) || (cfg->maxh != 0 && cth > cfg->maxh)))) {
            double fac = sc;
            if (pass == 2) {
                double restriction = cfg->maxw != 0 ? cfg->maxw : cfg->maxh;
                fac = fac * restriction / (cfg->maxw != 0 ? ctw : cth);
            }
            nt.x = T->x; nt.y = T->y; nt.w = cfg->minw; nt.h = cfg->minh;
            if (node->UIT == UIT_ROOT) { nt.x = 0; nt.y = 0; nt.w = cfg->minw; nt.h = cfg->minh; }
            ctx = nt.x + padding; cty = nt.y + padding; ctw = 0; cth = 0;
            for (int i = 0; i < node->n.children.count; i++) {
                if (node->n.children.items[i].name) continue;
                Node *cn = node->n.children.items[i].node;
                if (!cn || cn->vt != &UIELEMENT_VT) continue;
                UIElement *v = (UIElement *)cn;
                if (v->config && v->config->scale != 0) v->config->scale = v->config->scale * fac;
                Transform ct = { ctx, cty, ctw, cth, 0, 1 };
                double tw, th; bool cok;
                UIBox_calculate_xywh(b, v, &ct, recalculate, fac, &tw, &th, &cok);
                if (cok) {
                    if (v->UIT == UIT_R) {
                        cth += th + padding;
                        cty += th + padding;
                        if (tw + padding > ctw) ctw = tw + padding;
                        if (v->config && v->config->emboss != 0) { cth += v->config->emboss; cty += v->config->emboss; }
                    } else {
                        ctw += tw + padding;
                        ctx += tw + padding;
                        if (th + padding > cth) cth = th + padding;
                        if (v->config && v->config->emboss != 0) cth += v->config->emboss;
                    }
                }
            }
        }
    }
    node->content_dimensions.w = ctw + padding;
    node->content_dimensions.h = cth + padding;
    nt.w = MAX(ctw + padding, nt.w);
    nt.h = MAX(cth + padding, nt.h);
    UIElement_set_values(node, &nt, recalculate);
    *ow = nt.w; *oh = nt.h;
}

void UIBox_remove_group(UIBox *b, UIElement *node, const char *group) {
    if (!node) node = b->UIRoot;
    /* recursive helper returning whether node was removed */
    for (int i = node->n.children.count - 1; i >= 0; i--) {
        Node *c = node->n.children.items[i].node;
        if (!IS(c, NT_UIELEMENT)) continue;
        UIElement *v = (UIElement *)c;
        UIBox_remove_group(b, v, group);
        if (v->n.REMOVED) {
            for (int j = 0; j < node->n.children.count; j++)
                if (node->n.children.items[j].node == c) { vec_remove_at(&node->n.children, j); break; }
        }
    }
    if (node->config && node->config->group && group && !strcmp(node->config->group, group)) {
        node_remove(&node->n);
        return;
    }
    double w, h; bool ok;
    UIBox_calculate_xywh(b, b->UIRoot, b->n.T, true, 1, &w, &h, &ok);
    UIElement_set_wh(&b->UIRoot->n, &w, &h, &ok);
    b->UIRoot->n.alignment.lr_clamp = false;
}

void UIBox_get_group(UIBox *b, UIElement *node, const char *group, PtrVec *out) {
    if (!node) node = b->UIRoot;
    for (int i = 0; i < node->n.children.count; i++) {
        Node *c = node->n.children.items[i].node;
        if (IS(c, NT_UIELEMENT)) UIBox_get_group(b, (UIElement *)c, group, out);
    }
    if (node->config && node->config->group && group && !strcmp(node->config->group, group)) vec_push(out, node);
}

void UIBox_set_parent_child(UIBox *b, UIDef *def, UIElement *parent) {
    UIElement *e = uielement_new(parent, b, def->n, def->config);
    if (parent && parent->config && parent->config->group) e->config->group = parent->config->group;
    if (parent && parent->config && parent->config->button) e->config->button_UIE = parent;
    if (parent && parent->config && parent->config->button_UIE) e->config->button_UIE = parent->config->button_UIE;
    if (def->n == UIT_O && e->config->button && e->config->object) e->config->object->states.click.can = false;
    if ((def->n == UIT_C || def->n == UIT_R || def->n == UIT_ROOT) && def->nodes) {
        for (int i = 0; i < def->count; i++) if (def->nodes[i]) UIBox_set_parent_child(b, def->nodes[i], e);
    }
    if (!parent) {
        b->UIRoot = e;
        e->n.parent = &b->n;
    } else {
        child_push(&parent->n, &e->n);
    }
    if (def->config && def->config->mid) b->n.Mid = &e->n;
}

void UIBox_remove(Node *n) {
    UIBox *b = (UIBox *)n;
    if (b == G.OVERLAY_MENU) G.REFRESH_ALERTS = true;
    if (b->UIRoot) node_remove(&b->UIRoot->n);
    ptrvec_remove(instance_list(b->config->instance_type), n);
    remove_all_children(n);
    Moveable_remove(n);
}

void UIBox_draw(Node *n) {
    UIBox *b = (UIBox *)n;
    if (n->FRAME.DRAW >= G.FRAMES.DRAW && !G.OVERLAY_TUTORIAL) return;
    n->FRAME.DRAW = G.FRAMES.DRAW;
    for (int i = 0; i < n->children.count; i++) {
        const char *k = n->children.items[i].name;
        if (!k || (strcmp(k, "h_popup") && strcmp(k, "alert"))) node_draw(n->children.items[i].node);
    }
    if (n->states.visible) {
        add_to_drawhash(n);
        UIElement_draw_self(&b->UIRoot->n);
        UIElement_draw_children(&b->UIRoot->n);
        for (int i = 1; i < b->draw_layers.count; i++) {
            Node *v = b->draw_layers.items[i];
            if (!v) break;
            if (v->vt->draw_self) v->vt->draw_self(v); else node_draw(v);
            if (v->vt->draw_children) v->vt->draw_children(v);
        }
    }
    Node *alert = child_get(n, "alert");
    if (alert) node_draw(alert);
    Node_draw_boundingrect(n);
}

void UIBox_recalculate(UIBox *b) {
    double w, h; bool ok;
    UIBox_calculate_xywh(b, b->UIRoot, b->n.T, true, 1, &w, &h, &ok);
    UIElement_set_wh(&b->UIRoot->n, &w, &h, &ok);
    UIElement_set_alignments(&b->UIRoot->n);
    b->n.T->w = b->UIRoot->n.T->w;
    b->n.T->h = b->UIRoot->n.T->h;
    G.REFRESH_FRAME_MAJOR_CACHE++;
    UIElement_initialize_VT(&b->UIRoot->n);
    G.REFRESH_FRAME_MAJOR_CACHE = G.REFRESH_FRAME_MAJOR_CACHE > 1 ? G.REFRESH_FRAME_MAJOR_CACHE - 1 : 0;
}
static void UIBox_recalculate_v(Node *n) { UIBox_recalculate((UIBox *)n); }

void UIBox_move(Node *n, double dt) {
    UIBox *b = (UIBox *)n;
    Moveable_move(n, dt);
    if (b->UIRoot) Moveable_move(&b->UIRoot->n, dt);
}
static void UIBox_drag(Node *n, Vec2 *off) {
    UIBox *b = (UIBox *)n;
    Moveable_drag(n, off);
    if (b->UIRoot) Moveable_move(&b->UIRoot->n, 0);
}
void UIBox_add_child(UIBox *b, UIDef *def, UIElement *parent) {
    UIBox_set_parent_child(b, def, parent);
    UIBox_recalculate(b);
}
void UIBox_set_container(Node *n, Node *container) {
    UIBox *b = (UIBox *)n;
    if (b->UIRoot) node_set_container(&b->UIRoot->n, container);
    Node_set_container(n, container);
}

/* ------------------------------------------------------------------ */
/* UIElement                                                           */
/* ------------------------------------------------------------------ */
void UIElement_set_values(UIElement *e, Transform *T, bool recalculate) {
    Node *n = &e->n;
    UIConfig *c = e->config;
    if (!recalculate || !n->T) {
        double w = isnan(T->w) ? 1 : T->w, h = isnan(T->h) ? 1 : T->h;
        Children keep = n->children;
        Node *keep_parent = n->parent;
        /* Moveable.init on an element that already exists: drop the old
         * registrations first so it is not listed twice */
        if (n->is_moveable) {
            ptrvec_remove_all(&G.MOVEABLES, n);
            for (int st = 0; st < (int)ARRLEN(G.STAGE_OBJECTS); st++) ptrvec_remove_all(&G.STAGE_OBJECTS[st], n);
        }
        memset(&n->children, 0, sizeof n->children);
        moveable_init(n, &UIELEMENT_VT, T->x, T->y, w, h);
        n->refresh_movement = e->config->refresh_movement;
        n->children = keep;
        n->parent = keep_parent;
        n->states.click.can = false;
        n->states.drag.can = false;
        n->static_rotation = true;
    } else {
        n->T->x = T->x; n->T->y = T->y;
        if (!isnan(T->w)) n->T->w = T->w;
        if (!isnan(T->h)) n->T->h = T->h;
    }
    if (c->button_UIE) { n->states.collide.can = true; n->states.hover.can = false; n->states.click.can = true; }
    if (c->button) { n->states.collide.can = true; n->states.click.can = true; }
    if (c->on_demand_tooltip || c->tooltip || c->detailed_tooltip) n->states.collide.can = true;
    Moveable_set_role(n, ROLE_ARGS(.role_type = ROLE_MINOR, .major = &e->UIBox->n, .has_offset = true,
                                   .offset = (Vec2){ T->x, T->y }, .wh_bond = BOND_WEAK, .scale_bond = BOND_WEAK));
    if (c->draw_layer) {
        while (e->UIBox->draw_layers.count <= c->draw_layer) vec_push(&e->UIBox->draw_layers, NULL);
        e->UIBox->draw_layers.items[c->draw_layer] = n;
    }
    if (c->collideable) n->states.collide.can = true;
    if (c->can_collide != 0) {
        n->states.collide.can = c->can_collide > 0;
        if (c->object) c->object->states.collide.can = n->states.collide.can;
    }
    if (e->UIT == UIT_O && !c->no_role && c->object) {
        if (c->role) Moveable_set_role(c->object, *c->role);
        else Moveable_set_role(c->object, ROLE_ARGS(.role_type = ROLE_MINOR, .major = n, .xy_bond = BOND_STRONG, .wh_bond = BOND_WEAK, .scale_bond = BOND_WEAK));
    }
    if (ref_valid(c->ref)) str_set(&c->prev_value, ref_tostring(c->ref));
    if (e->UIT == UIT_T) n->static_rotation = true;
    if (c->juice) {
        if (e->UIT == UIT_O) { if (c->object) node_juice_up(c->object, 0.5, JUICE_NIL); }
        else Moveable_juice_up(n, JUICE_NIL, JUICE_NIL);
        c->juice = false;
    }
    if (!c->colour) {
        if (e->UIT == UIT_ROOT) c->colour = G.C.UI.BACKGROUND_DARK;
        if (e->UIT == UIT_T) c->colour = G.C.UI.TEXT_LIGHT;
        if (e->UIT == UIT_O) c->colour = G.C.WHITE;
        if (e->UIT == UIT_B || e->UIT == UIT_C || e->UIT == UIT_R) c->colour = G.C.CLEAR;
    }
    if (!c->outline_colour) {
        if (e->UIT == UIT_O) c->colour = G.C.UI.OUTLINE_LIGHT; /* sic: matches the original */
        else c->outline_colour = G.C.UI.OUTLINE_LIGHT;
    }
    if (c->focus_args && !c->focus_args->registered) {
        if (c->focus_args->button) Controller_add_to_registry(&G.CONTROLLER, c->button_UIE ? &c->button_UIE->n : n, c->focus_args->button);
        if (c->focus_args->snap_to) Controller_snap_to_node(&G.CONTROLLER, n);
        if (c->focus_args->funnel_to_set) {
            UIElement *par = e->parent_uie;
            while (par) {
                if (par->config->focus_args && par->config->focus_args->funnel_from_set) {
                    par->config->focus_args->funnel_from = e;
                    c->focus_args->funnel_to = par;
                    break;
                }
                par = par->parent_uie;
            }
        }
        c->focus_args->registered = true;
    }
    if (c->force_focus) n->states.collide.can = true;
    if (c->button_delay != 0 && !c->button_delay_started) {
        c->button_delay_started = true;
        c->button_delay_start = G.TIMERS.REAL;
        c->button_delay_end = G.TIMERS.REAL + c->button_delay;
        c->button_delay_progress = 0;
    }
    if (c->func && (((c->button_UIE || c->button) && c->func != G_FUNCS_set_button_pip) || c->insta_func)) c->func(e);
}

void UIElement_initialize_VT(Node *n) {
    UIElement *e = (UIElement *)n;
    Moveable_move_with_major(n, 0);
    Moveable_calculate_parrallax(n);
    for (int i = 0; i < n->children.count; i++) {
        Node *c = n->children.items[i].node;
        if (c && c->vt->initialize_VT) c->vt->initialize_VT(c);
    }
    n->VT.w = n->T->w; n->VT.h = n->T->h;
    if (e->UIT == UIT_T) UIElement_update_text(e);
    Node *o = e->config->object;
    if (o) {
        if (!e->config->no_role) {
            node_hard_set_T(o, n->T->x, n->T->y, n->T->w, n->T->h);
            Moveable_move_with_major(o, 0);
            o->alignment.prev_type[0] = 0;
            Moveable_align_to_major(o);
        }
        if (o->vt->recalculate) o->vt->recalculate(o);
    }
}

static void UIElement_juice_up(Node *n, double amount, double rot) {
    UIElement *e = (UIElement *)n;
    if (e->UIT == UIT_O) { if (e->config->object) node_juice_up(e->config->object, amount, rot); }
    else Moveable_juice_up(n, amount, rot);
}

static Node *UIElement_can_drag(Node *n) {
    UIElement *e = (UIElement *)n;
    if (n->states.drag.can) return n;
    return node_can_drag(&e->UIBox->n);
}

static void UIElement_draw_noop(Node *n) { (void)n; }

void UIElement_draw_children(Node *n) {
    if (!n->states.visible) return;
    for (int i = 0; i < n->children.count; i++) {
        const char *k = n->children.items[i].name;
        Node *v = n->children.items[i].node;
        if (k && (!strcmp(k, "h_popup") || !strcmp(k, "alert"))) continue;
        bool is_uie = IS(v, NT_UIELEMENT);
        UIConfig *vc = is_uie ? ((UIElement *)v)->config : NULL;
        if (vc && vc->draw_layer) continue;
        bool has_self = v->vt->draw_self != NULL;
        bool draw_after = vc && vc->draw_after;
        if (has_self && !draw_after) v->vt->draw_self(v); else node_draw(v);
        if (v->vt->draw_children) v->vt->draw_children(v);
        if (has_self && draw_after) v->vt->draw_self(v); else node_draw(v);
    }
}

void UIElement_set_wh(Node *n, double *ow, double *oh, bool *ok) {
    UIElement *e = (UIElement *)n;
    double padding = e->config ? e->config->padding : UIT_PADDING;
    double mw = 0, mh = 0;
    *ok = true;
    int nchild = 0;
    for (int i = 0; i < n->children.count; i++) nchild++;
    if (nchild == 0 || e->config->no_fill) { *ow = n->T->w; *oh = n->T->h; return; }
    for (int i = 0; i < n->children.count; i++) {
        Node *w = n->children.items[i].node;
        if (w->vt->set_wh) {
            double cw, ch; bool cok;
            w->vt->set_wh(w, &cw, &ch, &cok);
            if (cok) {
                if (cw > mw) mw = cw;
                if (ch > mh) mh = ch;
            } else { mw = padding; mh = padding; }
        }
    }
    for (int i = 0; i < n->children.count; i++) {
        Node *w = n->children.items[i].node;
        if (!IS(w, NT_UIELEMENT)) continue;
        UIElement *we = (UIElement *)w;
        if (we->UIT == UIT_R) w->T->w = mw;
        if (we->UIT == UIT_C) w->T->h = mh;
    }
    *ow = n->T->w; *oh = n->T->h;
}

void UIElement_align(Node *n, double x, double y) {
    n->role.offset.y += y;
    n->role.offset.x += x;
    for (int i = 0; i < n->children.count; i++) {
        Node *v = n->children.items[i].node;
        if (v->vt->align) v->vt->align(v, x, y);
    }
}

void UIElement_set_alignments(Node *n) {
    UIElement *e = (UIElement *)n;
    for (int i = 0; i < n->children.count; i++) {
        Node *v = n->children.items[i].node;
        if (e->config && e->config->align && v->vt->align) {
            const char *al = e->config->align;
            double padding = e->config->padding;
            UIElement *ve = (UIElement *)v;
            if (str_has_char(al, 'c')) {
                if (ve->UIT == UIT_T || ve->UIT == UIT_B || ve->UIT == UIT_O) v->vt->align(v, 0, 0.5 * (n->T->h - 2 * padding - v->T->h));
                else v->vt->align(v, 0, 0.5 * (n->T->h - e->content_dimensions.h));
            }
            if (str_has_char(al, 'm')) v->vt->align(v, 0.5 * (n->T->w - e->content_dimensions.w), 0);
            if (str_has_char(al, 'b')) v->vt->align(v, 0, n->T->h - e->content_dimensions.h);
            if (str_has_char(al, 'r')) v->vt->align(v, n->T->w - e->content_dimensions.w, 0);
        }
        if (v->vt->set_alignments) v->vt->set_alignments(v);
    }
}

void UIElement_update_text(UIElement *e) {
    UIConfig *c = e->config;
    if (c->text && !c->text_drawable) {
        cfg_lang(c);
        c->text_drawable = (void *)1;
    }
    if (ref_valid(c->ref)) {
        const char *now = ref_tostring(c->ref);
        if (!str_eq(now, c->prev_value)) {
            size_t old_len = c->prev_value ? strlen(c->prev_value) : 0;
            bool had_prev = c->prev_value != NULL;
            str_set(&c->text, now);
            if (!c->no_recalc && had_prev && old_len != strlen(c->text)) UIBox_recalculate(e->UIBox);
            str_set(&c->prev_value, now);
        }
    }
}

void UIElement_update_object(UIElement *e) {
    UIConfig *c = e->config;
    if (ref_valid(c->ref) && c->ref.kind == REF_NODE) {
        Node *o = ref_node(c->ref);
        if (o != c->object) {
            c->object = o;
            UIBox_recalculate(e->UIBox);
        }
    }
    Node *o = c->object;
    if (o) {
        o->refresh_movement = true;
        if (o->states.hover.is && !e->n.states.hover.is) {
            node_hover(&e->n);
            e->n.states.hover.is = true;
        }
        if (!o->states.hover.is && e->n.states.hover.is) {
            node_stop_hover(&e->n);
            e->n.states.hover.is = false;
        }
    }
    if (o && o->ui_object_updated) {
        o->ui_object_updated = false;
        o->parent = &e->n;
        if (c->role) Moveable_set_role(o, *c->role);
        else Moveable_set_role(o, ROLE_ARGS(.role_type = ROLE_MINOR, .major = &e->n));
        Moveable_move_with_major(o, 0);
        if (o->non_recalc) {
            UIElement *par = e->parent_uie;
            if (par) {
                par->content_dimensions.w = o->T->w;
                UIElement_align(&e->n, par->n.T->x - o->T->x, par->n.T->y - o->T->y);
                UIElement_set_alignments(&par->n);
            }
        } else UIBox_recalculate(e->UIBox);
    }
}

static void draw_text(UIElement *e, double x_shift, double y_shift, bool shadow_pass, double sx_extra) {
    (void)sx_extra;
    UIConfig *c = e->config;
    Language *lang = cfg_lang(c);
    FontDef *f = lang->font;
    double scale = c->scale;
    double px = (f->TEXT_OFFSET.x + x_shift) * scale * f->FONTSCALE / G.TILESIZE;
    double py = (f->TEXT_OFFSET.y + y_shift) * scale * f->FONTSCALE / G.TILESIZE;
    (void)shadow_pass;
    gfx_print(f->FONT, c->text ? c->text : "", px, py, 0,
              scale * f->squish * f->FONTSCALE / G.TILESIZE, scale * f->FONTSCALE / G.TILESIZE, 0, 0);
}

void UIElement_draw_self(Node *n) {
    UIElement *e = (UIElement *)n;
    UIConfig *c = e->config;
    if (!n->states.visible) {
        if (c->force_focus) add_to_drawhash(n);
        return;
    }
    if (c->force_focus || c->force_collision || c->button_UIE || c->button || n->states.collide.can) add_to_drawhash(n);
    bool button_active = true;
    double parallax_dist = 1.5;
    bool button_being_pressed = false;
    if (c->button || c->button_UIE) {
        Node *par = e->parent_uie ? &e->parent_uie->n : NULL;
        n->layered_parallax.x = ((par && par != &e->UIBox->n) ? par->layered_parallax.x : 0) + (c->shadow ? 0.4 * n->shadow_parrallax->x : 0) / G.TILESIZE;
        n->layered_parallax.y = ((par && par != &e->UIBox->n) ? par->layered_parallax.y : 0) + (c->shadow ? 0.4 * n->shadow_parrallax->y : 0) / G.TILESIZE;
        if (c->button && ((e->last_clicked > G.TIMERS.REAL - 0.1) || ((n->states.hover.is || n->states.drag.is) && G.CONTROLLER.is_cursor_down))) {
            double bd = c->button_dist != 0 ? c->button_dist : 1;
            n->layered_parallax.x -= parallax_dist * n->shadow_parrallax->x / G.TILESIZE * bd;
            n->layered_parallax.y -= parallax_dist * n->shadow_parrallax->y / G.TILESIZE * bd;
            parallax_dist = 0;
            button_being_pressed = true;
        }
        if (c->button_UIE && !c->button_UIE->config->button) button_active = false;
    }
    if (c->colour && c->colour[3] > 0.01) {
        if (e->UIT == UIT_T && c->scale != 0) {
            Language *lang = cfg_lang(c);
            FontDef *f = lang->font;
            e->text_parallax.sx = -n->shadow_parrallax->x * 0.5 / (c->scale * f->FONTSCALE);
            e->text_parallax.sy = -n->shadow_parrallax->y * 0.5 / (c->scale * f->FONTSCALE);
            bool shadows_on = str_eq(G.SETTINGS.GRAPHICS.shadows, "On");
            if ((c->button_UIE && button_active) || (!c->button_UIE && c->shadow && shadows_on)) {
                prep_draw(n, 0.97, 0, NULL, false);
                if (c->vert) { gfx_translate(0, n->VT.h); gfx_rotate(-M_PI / 2); }
                if ((c->shadow || (c->button_UIE && button_active)) && shadows_on) {
                    gfx_set_color4(0, 0, 0, 0.3f * c->colour[3]);
                    double xs = c->vert ? -e->text_parallax.sy : e->text_parallax.sx;
                    double ys = c->vert ? e->text_parallax.sx : e->text_parallax.sy;
                    draw_text(e, xs, ys, true, 0);
                }
                gfx_pop();
            }
            prep_draw(n, 1, 0, NULL, false);
            if (c->vert) { gfx_translate(0, n->VT.h); gfx_rotate(-M_PI / 2); }
            if (!button_active) gfx_set_color(G.C.UI.TEXT_INACTIVE);
            else gfx_set_color(c->colour);
            draw_text(e, 0, 0, false, 0);
            gfx_pop();
        } else if (e->UIT == UIT_B || e->UIT == UIT_C || e->UIT == UIT_R || e->UIT == UIT_ROOT) {
            prep_draw(n, 1, 0, NULL, false);
            gfx_scale(1.0 / G.TILESIZE, 1.0 / G.TILESIZE);
            bool has_r = !isnan(c->r);
            if (c->shadow && str_eq(G.SETTINGS.GRAPHICS.shadows, "On")) {
                gfx_scale(0.98, 0.98);
                if (c->shadow_colour) gfx_set_color(c->shadow_colour);
                else gfx_set_color4(0, 0, 0, 0.3f * c->colour[3]);
                if (has_r && n->VT.w > 0.01) UIElement_draw_pixellated_rect(e, "shadow", parallax_dist, 0, NAN);
                else gfx_rectangle(GFX_FILL, -n->shadow_parrallax->x * parallax_dist, -n->shadow_parrallax->y * parallax_dist, n->VT.w * G.TILESIZE, n->VT.h * G.TILESIZE);
                gfx_scale(1 / 0.98, 1 / 0.98);
            }
            gfx_scale(button_being_pressed ? 0.985 : 1, button_being_pressed ? 0.985 : 1);
            if (c->emboss != 0) {
                float d[4];
                double p = n->states.hover.is ? 0.5 : 0.3;
                d[0] = (float)(c->colour[0] * (1 - p)); d[1] = (float)(c->colour[1] * (1 - p)); d[2] = (float)(c->colour[2] * (1 - p)); d[3] = c->colour[3];
                gfx_set_color(d);
                UIElement_draw_pixellated_rect(e, "emboss", parallax_dist, c->emboss, NAN);
            }
            UIElement *cb = c->button_UIE ? c->button_UIE : e;
            float mixed[4];
            const float *cols[2];
            int ncols = 0;
            if (c->button_delay != 0) {
                for (int i = 0; i < 4; i++) mixed[i] = (float)(c->colour[i] * 0.5 + G.C.L_BLACK[i] * 0.5);
                cols[ncols++] = mixed;
            } else cols[ncols++] = c->colour;
            if ((cb->config->hover && cb->n.states.hover.is) || (cb->last_clicked > G.TIMERS.REAL - 0.1)) cols[ncols++] = G.C.UI.HOVER;
            for (int k = 0; k < ncols; k++) {
                gfx_set_color(cols[k]);
                if (has_r && n->VT.w > 0.01) {
                    if (c->button_delay != 0) {
                        gfx_set_color(G.C.GREY);
                        UIElement_draw_pixellated_rect(e, "fill", parallax_dist, 0, NAN);
                        gfx_set_color(cols[k]);
                        UIElement_draw_pixellated_rect(e, "fill", parallax_dist, 0, c->button_delay_progress);
                    } else if (c->progress_bar) {
                        gfx_set_color(c->progress_bar->empty_col ? c->progress_bar->empty_col : G.C.GREY);
                        UIElement_draw_pixellated_rect(e, "fill", parallax_dist, 0, NAN);
                        gfx_set_color(c->progress_bar->filled_col ? c->progress_bar->filled_col : G.C.BLUE);
                        UIElement_draw_pixellated_rect(e, "fill", parallax_dist, 0, ref_num(c->progress_bar->ref) / c->progress_bar->max);
                    } else UIElement_draw_pixellated_rect(e, "fill", parallax_dist, 0, NAN);
                } else gfx_rectangle(GFX_FILL, 0, 0, n->VT.w * G.TILESIZE, n->VT.h * G.TILESIZE);
            }
            gfx_pop();
        } else if (e->UIT == UIT_O && c->object) {
            if (c->focus_with_object && c->object->states.focus.is) {
                if (!e->object_focus_timer_set) { e->object_focus_timer = G.TIMERS.REAL; e->object_focus_timer_set = true; }
                double lw = 50 * pow(MAX(0, e->object_focus_timer - G.TIMERS.REAL + 0.3), 2);
                prep_draw(n, 1, 0, NULL, false);
                gfx_scale(1.0 / G.TILESIZE, 1.0 / G.TILESIZE);
                gfx_set_line_width((float)(lw + 1.5));
                float a[4] = { G.C.WHITE[0], G.C.WHITE[1], G.C.WHITE[2], (float)(0.2 * lw) };
                gfx_set_color(a);
                UIElement_draw_pixellated_rect(e, "fill", parallax_dist, 0, NAN);
                float m[4];
                if (c->colour[3] > 0) { for (int i = 0; i < 4; i++) m[i] = (float)(G.C.WHITE[i] * 0.8 + c->colour[i] * 0.2); gfx_set_color(m); }
                else gfx_set_color(G.C.WHITE);
                UIElement_draw_pixellated_rect(e, "line", parallax_dist, 0, NAN);
                gfx_pop();
            } else e->object_focus_timer_set = false;
            node_draw(c->object);
        }
    }
    if (!isnan(c->outline) && c->outline_colour && c->outline_colour[3] > 0.01) {
        prep_draw(n, 1, 0, NULL, false);
        gfx_scale(1.0 / G.TILESIZE, 1.0 / G.TILESIZE);
        gfx_set_line_width((float)c->outline);
        if (c->line_emboss != 0) {
            double p = n->states.hover.is ? 0.5 : 0.3;
            float d[4] = { (float)(c->outline_colour[0] * (1 - p)), (float)(c->outline_colour[1] * (1 - p)), (float)(c->outline_colour[2] * (1 - p)), c->outline_colour[3] };
            gfx_set_color(d);
            UIElement_draw_pixellated_rect(e, "line_emboss", parallax_dist, c->line_emboss, NAN);
        }
        gfx_set_color(c->outline_colour);
        if (!isnan(c->r) && n->VT.w > 0.01) UIElement_draw_pixellated_rect(e, "line", parallax_dist, 0, NAN);
        else gfx_rectangle(GFX_LINE, 0, 0, n->VT.w * G.TILESIZE, n->VT.h * G.TILESIZE);
        gfx_pop();
    }
    if (n->states.focus.is) {
        if (!e->focus_timer_set) { e->focus_timer = G.TIMERS.REAL; e->focus_timer_set = true; }
        double lw = 50 * pow(MAX(0, e->focus_timer - G.TIMERS.REAL + 0.3), 2);
        prep_draw(n, 1, 0, NULL, false);
        gfx_scale(1.0 / G.TILESIZE, 1.0 / G.TILESIZE);
        gfx_set_line_width((float)(lw + 1.5));
        float a[4] = { G.C.WHITE[0], G.C.WHITE[1], G.C.WHITE[2], (float)(0.2 * lw) };
        gfx_set_color(a);
        UIElement_draw_pixellated_rect(e, "fill", parallax_dist, 0, NAN);
        float m[4];
        if (c->colour && c->colour[3] > 0) { for (int i = 0; i < 4; i++) m[i] = (float)(G.C.WHITE[i] * 0.8 + c->colour[i] * 0.2); gfx_set_color(m); }
        else gfx_set_color(G.C.WHITE);
        UIElement_draw_pixellated_rect(e, "line", parallax_dist, 0, NAN);
        gfx_pop();
    } else e->focus_timer_set = false;
    if (c->chosen) {
        float tri[6];
        prep_draw(n, 0.98, 0, NULL, false);
        gfx_scale(1.0 / G.TILESIZE, 1.0 / G.TILESIZE);
        if (c->shadow && str_eq(G.SETTINGS.GRAPHICS.shadows, "On")) {
            gfx_set_color4(0, 0, 0, 0.3f * (c->colour ? c->colour[3] : 1));
            get_chosen_triangle_from_rect(n->layered_parallax.x - n->shadow_parrallax->x * parallax_dist * 0.5,
                                          n->layered_parallax.y - n->shadow_parrallax->y * parallax_dist * 0.5,
                                          n->VT.w * G.TILESIZE, n->VT.h * G.TILESIZE, c->chosen == 2, tri);
            gfx_polygon(GFX_FILL, tri, 6);
        }
        gfx_pop();
        prep_draw(n, 1, 0, NULL, false);
        gfx_scale(1.0 / G.TILESIZE, 1.0 / G.TILESIZE);
        gfx_set_color(G.C.RED);
        get_chosen_triangle_from_rect(n->layered_parallax.x, n->layered_parallax.y, n->VT.w * G.TILESIZE, n->VT.h * G.TILESIZE, c->chosen == 2, tri);
        gfx_polygon(GFX_FILL, tri, 6);
        gfx_pop();
    }
    Node_draw_boundingrect(n);
}

#define FV(vec) (vec)
void UIElement_draw_pixellated_rect(UIElement *e, const char *type, double parallax, double emboss, double progress) {
    Node *n = &e->n;
    UIConfig *c = e->config;
    double prog = isnan(progress) ? 1 : progress;
    bool rebuild = !e->pixellated_rect.valid || parallax != e->pixellated_rect.parallax ||
                   e->pixellated_rect.w != n->VT.w || e->pixellated_rect.h != n->VT.h ||
                   e->pixellated_rect.sw != n->shadow_parrallax->x || e->pixellated_rect.sh != n->shadow_parrallax->y ||
                   e->pixellated_rect.progress != prog;
    /* the original also rebuilds when the requested vertex list is empty */
    if (!rebuild) {
        if (!strcmp(type, "fill") && e->pixellated_rect.fill.count < 1) rebuild = true;
        else if (!strcmp(type, "shadow") && e->pixellated_rect.shadow.count < 1) rebuild = true;
        else if (!strcmp(type, "line") && e->pixellated_rect.line.count < 1) rebuild = true;
        else if (!strcmp(type, "emboss") && e->pixellated_rect.emboss.count < 1) rebuild = true;
        else if (!strcmp(type, "line_emboss") && e->pixellated_rect.line_emboss.count < 1) rebuild = true;
    }
    if (rebuild) {
        e->pixellated_rect.valid = true;
        e->pixellated_rect.w = n->VT.w;
        e->pixellated_rect.h = n->VT.h;
        e->pixellated_rect.sw = n->shadow_parrallax->x;
        e->pixellated_rect.sh = n->shadow_parrallax->y;
        e->pixellated_rect.progress = prog;
        e->pixellated_rect.parallax = parallax;
        vec_clear(&e->pixellated_rect.fill);
        vec_clear(&e->pixellated_rect.shadow);
        vec_clear(&e->pixellated_rect.line);
        vec_clear(&e->pixellated_rect.emboss);
        vec_clear(&e->pixellated_rect.line_emboss);
        double ext_up = c->ext_up != 0 ? c->ext_up * G.TILESIZE : 0;
        double mm = MIN(n->VT.w, n->VT.h + fabs(ext_up) / G.TILESIZE);
        double res = c->res != 0 ? c->res : (mm > 3.5 ? 0.8 : mm > 0.3 ? 0.6 : 0.15);
        double totw = n->VT.w * G.TILESIZE, toth = (n->VT.h + fabs(ext_up) / G.TILESIZE) * G.TILESIZE;
        double subw = totw - 4 * res, subh = toth - 4 * res;
        double v[] = {
            subw / 2, subh / 2 - ext_up,
            0, 4 * res - ext_up,
            1 * res, 4 * res - ext_up,
            1 * res, 2 * res - ext_up,
            2 * res, 2 * res - ext_up,
            2 * res, 1 * res - ext_up,
            4 * res, 1 * res - ext_up,
            4 * res, 0 * res - ext_up,
            subw, 0 * res - ext_up,
            subw, 1 * res - ext_up,
            subw + 2 * res, 1 * res - ext_up,
            subw + 2 * res, 2 * res - ext_up,
            subw + 3 * res, 2 * res - ext_up,
            subw + 3 * res, 4 * res - ext_up,
            totw, 4 * res - ext_up,
            totw, subh - ext_up,
            subw + 3 * res, subh - ext_up,
            subw + 3 * res, subh + 2 * res - ext_up,
            subw + 2 * res, subh + 2 * res - ext_up,
            subw + 2 * res, subh + 3 * res - ext_up,
            subw, subh + 3 * res - ext_up,
            subw, toth - ext_up,
            4 * res, toth - ext_up,
            4 * res, subh + 3 * res - ext_up,
            2 * res, subh + 3 * res - ext_up,
            2 * res, subh + 2 * res - ext_up,
            1 * res, subh + 2 * res - ext_up,
            1 * res, subh - ext_up,
            0, subh - ext_up,
            0, 4 * res - ext_up,
        };
        int nv = ARRLEN(v);
        vec_reserve(&e->pixellated_rect.fill, nv);
        vec_reserve(&e->pixellated_rect.shadow, nv);
        vec_reserve(&e->pixellated_rect.emboss, nv);
        vec_reserve(&e->pixellated_rect.line, nv);
        vec_reserve(&e->pixellated_rect.line_emboss, nv);
        for (int k = 1; k <= nv; k++) {
            double val = v[k - 1];
            if (k % 2 == 1 && val > totw * prog) val = totw * prog;
            e->pixellated_rect.fill.items[k - 1] = (float)val;
            if (k > 4) {
                e->pixellated_rect.line.items[k - 5] = (float)val;
                if (emboss != 0)
                    e->pixellated_rect.line_emboss.items[k - 5] = (float)(val + (k % 2 == 0 ? -emboss * n->shadow_parrallax->y : -0.7 * emboss * n->shadow_parrallax->x));
            }
            if (k % 2 == 0) {
                e->pixellated_rect.shadow.items[k - 1] = (float)(val - n->shadow_parrallax->y * parallax);
                if (emboss != 0) e->pixellated_rect.emboss.items[k - 1] = (float)(val + emboss * G.TILESIZE);
            } else {
                e->pixellated_rect.shadow.items[k - 1] = (float)(val - n->shadow_parrallax->x * parallax);
                if (emboss != 0) e->pixellated_rect.emboss.items[k - 1] = (float)val;
            }
        }
        e->pixellated_rect.fill.count = nv;
        e->pixellated_rect.shadow.count = nv;
        e->pixellated_rect.line.count = nv - 4;
        if (emboss != 0) { e->pixellated_rect.emboss.count = nv; e->pixellated_rect.line_emboss.count = nv - 4; }
    }
    bool line = !strcmp(type, "line") || !strcmp(type, "line_emboss");
    float *verts; int count;
    if (!strcmp(type, "fill")) { verts = e->pixellated_rect.fill.items; count = e->pixellated_rect.fill.count; }
    else if (!strcmp(type, "shadow")) { verts = e->pixellated_rect.shadow.items; count = e->pixellated_rect.shadow.count; }
    else if (!strcmp(type, "line")) { verts = e->pixellated_rect.line.items; count = e->pixellated_rect.line.count; }
    else if (!strcmp(type, "emboss")) { verts = e->pixellated_rect.emboss.items; count = e->pixellated_rect.emboss.count; }
    else { verts = e->pixellated_rect.line_emboss.items; count = e->pixellated_rect.line_emboss.count; }
    if (count > 0) gfx_polygon(line ? GFX_LINE : GFX_FILL, verts, count);
}

static void UIElement_update(Node *n, double dt) {
    (void)dt;
    UIElement *e = (UIElement *)n;
    UIConfig *c = e->config;
    if (c->button_delay != 0) {
        if (c->button) c->button_temp = c->button;
        c->button = NULL;
        c->button_delay_progress = (G.TIMERS.REAL - c->button_delay_start) / c->button_delay;
        if (G.TIMERS.REAL >= c->button_delay_end) c->button_delay = 0;
    }
    if (c->button_temp && c->button_delay == 0) c->button = c->button_temp;
    if (e->button_clicked) e->button_clicked = false;
    if (c->func) c->func(e);
    if (n->REMOVED) return;
    if (e->UIT == UIT_T) UIElement_update_text(e);
    if (e->UIT == UIT_O) UIElement_update_object(e);
}

static bool UIElement_collides(Node *n, Vec2 p) {
    UIElement *e = (UIElement *)n;
    if (e->UIBox->n.states.collide.can) return Node_collides_with_point(n, p);
    return false;
}

void UIElement_click(Node *n) {
    UIElement *e = (UIElement *)n;
    UIConfig *c = e->config;
    if (c->button && (e->last_clicked + 0.1 < G.TIMERS.REAL) && n->states.visible && !n->under_overlay && !e->disable_button) {
        if (c->one_press) e->disable_button = true;
        e->last_clicked = G.TIMERS.REAL;
        if (c->id && !strcmp(c->id, "overlay_menu_back_button")) {
            Controller_mod_cursor_context_layer(&G.CONTROLLER, -1);
            G.NO_MOD_CURSOR_STACK = true;
        }
        if (G.OVERLAY_TUTORIAL && G.OVERLAY_TUTORIAL->button_listen && G.OVERLAY_TUTORIAL->button_listen == c->button) G_FUNCS_tut_next(NULL);
        UIFunc b = c->button;
        b(e);
        G.NO_MOD_CURSOR_STACK = false;
        if (c->choice) {
            PtrVec choices = { 0 };
            UIBox_get_group(e->UIBox, NULL, c->group, &choices);
            for (int i = 0; i < choices.count; i++) {
                UIElement *v = choices.items[i];
                if (v->config && v->config->choice) v->config->chosen = 0;
            }
            vec_free(&choices);
            c->chosen = 1;
        }
        sound_play("button", 1, 0.3);
        G.ROOM_jiggle += 0.5;
        e->button_clicked = true;
    }
    if (c->button_UIE) UIElement_click(&c->button_UIE->n);
}

static void UIElement_put_focused_cursor(Node *n, double *x, double *y) {
    UIElement *e = (UIElement *)n;
    if (e->config->focus_args && e->config->focus_args->type && !strcmp(e->config->focus_args->type, "tab")) {
        for (int i = 0; i < n->children.count; i++) {
            Node *v = n->children.items[i].node;
            if (!IS(v, NT_UIELEMENT)) continue;
            UIElement *c1 = uie_child((UIElement *)v, 1);
            if (c1 && c1->config->chosen) { node_put_focused_cursor(&c1->n, x, y); return; }
        }
        Node_put_focused_cursor(n, x, y);
    } else Node_put_focused_cursor(n, x, y);
}

/* UIElements that were never laid out have no transform and were never
 * registered in the global lists; removing them only needs bookkeeping. */
static void Node_remove_light(Node *n) {
    Controller_clear_target_refs(&G.CONTROLLER, n);
    n->REMOVED = true;
}

static void UIElement_remove(Node *n) {
    UIElement *e = (UIElement *)n;
    if (e->config && e->config->object) {
        Node *o = e->config->object;
        e->config->object = NULL;
        node_remove(o);
    }
    if (e == G.CONTROLLER.text_input_hook) G.CONTROLLER.text_input_hook = NULL;
    remove_all_children(n);
    if (n->T) Moveable_remove(n);
    else Node_remove_light(n);
}

void UIElement_hover(Node *n) {
    UIElement *e = (UIElement *)n;
    UIConfig *c = e->config;
    if (c->on_demand_tooltip) {
        n->h_popup = create_popup_UIBox_tooltip(c->on_demand_tooltip);
        bool lower = n->T->y > G.ROOM->T->h / 2;
        n->h_popup_config = UIBCFG(.align = lower ? "tm" : "bm", .has_offset = true, .offset = { 0, lower ? -0.1 : 0.1 }, .parent = n);
    }
    if (c->tooltip) {
        n->h_popup = create_popup_UIBox_tooltip(c->tooltip);
        n->h_popup_config = UIBCFG(.align = "tm", .has_offset = true, .offset = { 0, -0.1 }, .parent = n);
    }
    if (c->detailed_tooltip && G.CONTROLLER.HID.pointer) {
        n->h_popup = create_UIBox_detailed_tooltip(c->detailed_tooltip, c->detailed_tooltip_kind);
        n->h_popup_config = UIBCFG(.align = "tm", .has_offset = true, .offset = { 0, -0.1 }, .parent = n);
    }
    Node_hover(n);
}
void UIElement_stop_hover(Node *n) {
    UIElement *e = (UIElement *)n;
    Node_stop_hover(n);
    if (e->config && e->config->on_demand_tooltip) n->h_popup = NULL;
}
static void UIElement_release(Node *n, Node *other) {
    if (n->parent) node_release(n->parent, other);
}
static void UIElement_move(Node *n, double dt) { if (n->T) Moveable_move(n, dt); }
static void UIElement_set_wh_v(Node *n, double *w, double *h, bool *ok) { UIElement_set_wh(n, w, h, ok); }
static void Node_noop_click(Node *n) { (void)n; }

const NodeVT UIBOX_VT = {
    .type = NT_UIBOX, .super = &MOVEABLE_VT, .name = "UIBox",
    .draw = UIBox_draw,
    .update = NULL,
    .move = UIBox_move,
    .remove = UIBox_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = Node_noop_click,
    .release = NULL,
    .drag = UIBox_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = UIBox_set_container,
    .juice_up = Moveable_juice_up,
    .recalculate = UIBox_recalculate_v,
    .hard_set_T = Moveable_hard_set_T,
};

const NodeVT UIELEMENT_VT = {
    .type = NT_UIELEMENT, .super = &MOVEABLE_VT, .name = "UIElement",
    .draw = UIElement_draw_noop,
    .update = UIElement_update,
    .move = UIElement_move,
    .remove = UIElement_remove,
    .hover = UIElement_hover,
    .stop_hover = UIElement_stop_hover,
    .click = UIElement_click,
    .release = UIElement_release,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = UIElement_can_drag,
    .collides_with_point = UIElement_collides,
    .put_focused_cursor = UIElement_put_focused_cursor,
    .set_container = Node_set_container,
    .juice_up = UIElement_juice_up,
    .draw_self = UIElement_draw_self,
    .draw_children = UIElement_draw_children,
    .hard_set_T = Moveable_hard_set_T,
    .align = UIElement_align,
    .set_alignments = UIElement_set_alignments,
    .set_wh = UIElement_set_wh_v,
    .initialize_VT = UIElement_initialize_VT,
};
