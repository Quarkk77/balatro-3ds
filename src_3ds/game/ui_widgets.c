/* ui_widgets.c - generic UI widgets from UI_definitions.lua and their
 * callbacks from button_callbacks.lua (sliders, toggles, option cycles,
 * tabs, text inputs, on-screen keyboard, button pips, generic options). */
#include "game.h"

#define LOC(k) ((char *)localize_key(k))
#define EVFN(name) static bool name(Event *e_, void *p_)

const char **str_list(int n, ...) {
    const char **r = xcalloc((size_t)(n > 0 ? n : 1), sizeof(char *));
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) r[i] = va_arg(ap, const char *);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------ */
/* UIBox_button                                                        */
/* ------------------------------------------------------------------ */
UIDef *UIBox_button(UIButtonArgs a) {
    UIFunc button = a.no_button ? NULL : (a.button ? a.button : G_FUNCS_exit_overlay_menu);
    Col colour = a.colour ? a.colour : G.C.RED;
    if (a.nlabel == 0) { a.label[0] = "LABEL"; a.nlabel = 1; }
    double minw = a.minw ? a.minw : 2.7;
    double maxw = a.maxw ? a.maxw : minw - 0.2;
    if (minw < maxw) maxw = minw - 0.2;
    double minh = a.minh ? a.minh : 0.9;
    double scale = a.scale ? a.scale : 0.5;
    Col text_colour = a.text_colour ? a.text_colour : G.C.UI.TEXT_LIGHT;
    UIDef *labels = uinodes();
    for (int k = 0; k < a.nlabel; k++) {
        bool pip = k == a.nlabel - 1 && a.focus_args && a.focus_args->set_button_pip;
        uidef_add(labels, UIR(CFG(.align = "cm", .padding = 0, .minw = minw, .maxw = maxw),
            UIT(CFG(.text = (char *)(a.label[k] ? a.label[k] : ""), .scale = scale, .colour = text_colour, .shadow = a.shadow,
                    .focus_args = pip ? a.focus_args : NULL, .func = pip ? G_FUNCS_set_button_pip : NULL, .ref_table = a.ref_table))));
    }
    if (a.has_count) {
        uidef_add(labels, UIR(CFG(.align = "cm", .minh = 0.4),
            UIT(CFG(.scale = 0.35, .text = (char *)tfmt("%s / %s", num_str(a.count_tally), num_str(a.count_of)), .colour = col_new(1, 1, 1, 0.9f)))));
    }
    UIConfig *cfg = CFG(.align = "cm", .padding = a.padding, .r = 0.1, .hover = true, .colour = colour, .one_press = a.one_press, .button = button,
                        .choice = a.choice, .chosen = a.chosen, .focus_args = a.focus_args, .minh = minh - 0.3 * (a.has_count ? 1 : 0), .shadow = true,
                        .func = a.func, .id = a.id ? intern(a.id) : NULL, .ref_table = a.ref_table, .mid = a.mid, .button_delay = a.button_delay);
    cfg->data = a.id_ptr;
    cfg->data2 = (void *)a.back_func;
    return UIN(a.col ? UIT_C : UIT_R, CFG(.align = "cm"), uidef_add_all(UIC(cfg), labels));
}

/* ------------------------------------------------------------------ */
/* create_UIBox_generic_options                                        */
/* ------------------------------------------------------------------ */
typedef struct { const char **rows; int n; } InfotipCtx;
EVFN(ev_set_infotip) {
    InfotipCtx *c = p_; (void)e_;
    if (G.OVERLAY_MENU) {
        UIElement *io = UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "overlay_menu_infotip", NULL);
        if (io) {
            if (io->config->object) node_remove(io->config->object);
            UIBox *b = uibox_new(overlay_infotip(c->rows, c->n), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "bm", .parent = &io->n));
            io->config->object = &b->n;
        }
    }
    return true;
}

UIDef *create_UIBox_generic_options(GenericOptionsArgs a) {
    UIFunc back_func = a.back_func ? a.back_func : G_FUNCS_exit_overlay_menu;
    UIDef *contents = a.contents ? a.contents : uidef_add(uinodes(), UIT(CFG(.text = "EMPTY", .colour = G.C.RED, .scale = 0.4)));
    if (a.infotip) {
        InfotipCtx *c = CTX(InfotipCtx); c->rows = a.infotip; c->n = a.ninfotip;
        add_event(EVENT(.blocking = 0, .blockable = 0, .timer = TIMER_REAL, .func = ev_set_infotip, .ctx = c));
    }
    Col bg = a.bg_colour ? a.bg_colour : col_new(G.C.GREY[0], G.C.GREY[1], G.C.GREY[2], 0.7f);
    const char *back_id = a.back_id ? a.back_id : "overlay_menu_back_button";
    UIDef *back = NULL;
    if (!a.no_back) {
        back = UIR(CFG(.id = back_id, .align = "cm", .minw = 2.5, .button_delay = a.back_delay, .padding = 0.1, .r = 0.1, .hover = true,
                       .colour = a.back_colour ? a.back_colour : G.C.ORANGE, .button = back_func, .shadow = true,
                       .focus_args = FOCUS(.nav = "wide", .button = "b", .snap_to = a.snap_back)),
            UIR(CFG(.align = "cm", .padding = 0, .no_fill = true),
                UIT(CFG(.id = a.back_id, .text = (char *)(a.back_label ? a.back_label : localize_key("b_back")), .scale = 0.5, .colour = G.C.UI.TEXT_LIGHT,
                        .shadow = true, .func = !a.no_pip ? G_FUNCS_set_button_pip : NULL,
                        .focus_args = !a.no_pip ? FOCUS(.button = a.back_button ? a.back_button : "b") : NULL))));
    }
    return UIROOT(CFG(.align = "cm", .minw = G.ROOM->T->w * 5, .minh = G.ROOM->T->h * 5, .padding = 0.1, .r = 0.1, .colour = bg),
        UIR(CFG(.align = "cm", .minh = 1, .r = 0.3, .padding = 0.07, .minw = 1, .colour = a.outline_colour ? a.outline_colour : G.C.JOKER_GREY, .emboss = 0.1),
            UIC(CFG(.align = "cm", .minh = 1, .r = 0.2, .padding = 0.2, .minw = 1, .colour = a.colour ? a.colour : G.C.L_BLACK),
                uidef_add_all(UIR(CFG(.align = "cm", .padding = a.has_padding ? a.padding : 0.2, .minw = a.minw ? a.minw : 7)), contents),
                back ? back : UISKIP)),
        UIR(CFG(.align = "cm"), UIO(CFG(.id = "overlay_menu_infotip", .object = moveable_new(0, 0, 0, 0)))));
}

/* ------------------------------------------------------------------ */
/* button binding pip                                                  */
/* ------------------------------------------------------------------ */
UIDef *create_button_binding_pip(const char *button, double scale) {
    static const char *names[] = { "a", "b", "x", "y", "leftshoulder", "rightshoulder", "triggerleft", "triggerright", "start", "back",
                                   "dpadup", "dpadright", "dpaddown", "dpadleft", "left", "right", "leftstick", "rightstick", "guide" };
    /* 3DS: always the Nintendo row of gamepad_ui.png. That row is laid out by
     * position (the Xbox "A" slot shows B), but the 3DS buttons are mapped by
     * name (3DS A = "a"), so the face buttons are swapped back here. START and
     * SELECT replace "+" / "-" in the 3DS copy of the atlas (meta/). */
    int vals[] = { 1, 0, 3, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19 };
    int x = 0;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (str_eq(button, names[i])) x = vals[i];
    int y = 2;
    double s = scale ? scale : 0.45;
    Sprite *sp = sprite_new(0, 0, s, s, atlas_get("gamepad_ui"), (SpritePos){ x, y });
    return UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR), UIO(CFG(.object = &sp->n)));
}

GF_IMPL(set_button_pip) {
    FocusArgs *fa = e->config->focus_args;
    Node *pip = child_get(&e->n, "button_pip");
    /* 3DS: button glyphs are always shown, touch screen or not */
    if (fa && !pip) {
        Vec2 off = fa->has_offset ? fa->offset : (str_eq(fa->orientation, "bm") ? (Vec2){ 0, 0.02 } : (Vec2){ 0.1, 0.02 });
        UIBox *b = uibox_new(create_button_binding_pip(fa->button, fa->scale),
                             UIBCFG(.align = fa->orientation ? fa->orientation : "cr", .has_offset = true, .offset = off, .major = &e->n, .parent = &e->n));
        b->n.states.collide.can = false;
        child_set(&e->n, "button_pip", &b->n);
    }
}

GF_IMPL(flash) {
    if (G.CONTROLLER.text_input_hook) {
        e->config->colour[3] = ((long)floor(G.TIMERS.REAL * 2)) % 2 == 1 ? 0 : 1;
        if (e->config->w != 0.1) { e->config->w = 0.1; UIBox_recalculate(e->UIBox); }
    } else {
        e->config->colour[3] = 0;
        if (e->config->w != 0) { e->config->w = 0; UIBox_recalculate(e->UIBox); }
    }
}

GF_IMPL(pip_dynatext) {
    DynaText *dt = e->config->ref_table;
    if (str_eq(tfmt("pip_%d", dt->focused_string + 1), e->config->id)) {
        if (e->config->pip_state != 1) { e->config->colour = e->config->pipcol; e->config->pip_state = 1; }
    } else if (e->config->pip_state != 2) {
        e->config->colour = e->config->pipcol2;
        e->config->pip_state = 2;
    }
}

UIDef *create_dynatext_pips(DynaText *dynatext, Col colour, Col inactive_colour, double w, double h) {
    if (!dynatext || dynatext->strings.count <= 1) return NULL;
    Col active = colour ? col_copy(colour) : G.C.RED;
    Col inactive = inactive_colour ? col_copy(inactive_colour) : col_new(0, 0, 0, 0.08f);
    if (!w) w = 0.07;
    if (!h) h = 0.07;
    UIDef *t = UIR(CFG(.padding = 0.05, .align = "cm"));
    for (int i = 1; i <= dynatext->strings.count; i++) {
        UIConfig *c = CFG(.ref_table = dynatext, .minw = w, .minh = h, .colour = G.C.UI.TEXT_INACTIVE, .r = 0.1, .id = intern(tfmt("pip_%d", i)),
                          .func = G_FUNCS_pip_dynatext);
        c->pipcol = active; c->pipcol2 = inactive;
        uidef_add(t, UIC(c));
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* slider                                                              */
/* ------------------------------------------------------------------ */
static void slider_text(SliderState *s) {
    free(s->text);
    s->text = xstrdup(tfmt("%.*f", s->decimal_places, ref_num(s->ref)));
}

UIDef *create_slider(SliderArgs args) {
    SliderState *s = NEW(SliderState);
    *s = args;
    if (!s->colour) s->colour = G.C.RED;
    if (!s->w) s->w = 1;
    if (!s->h) s->h = 0.5;
    if (!s->label_scale) s->label_scale = 0.5;
    if (!s->text_scale) s->text_scale = 0.3;
    if (s->max == 0 && s->min == 0) s->max = 1;
    s->text = NULL;
    slider_text(s);
    double startval = s->w * (ref_num(s->ref) - s->min) / (s->max - s->min);
    UIDef *t = UIC(CFG(.align = "cm", .minw = s->w, .padding = 0.1, .r = 0.1, .colour = G.C.CLEAR, .focus_args = FOCUS(.type = "slider")),
        UIC(CFG(.align = "cl", .minw = s->w, .r = 0.1, .collideable = true, .hover = true, .colour = G.C.BLACK, .emboss = 0.05, .func = G_FUNCS_slider,
                .refresh_movement = true),
            UIB(CFG(.w = startval, .h = s->h, .r = 0.1, .colour = s->colour, .ref_table = s, .refresh_movement = true))),
        UIC(CFG(.align = "cm", .minh = s->h, .r = 0.1, .minw = 0.8, .colour = s->colour, .shadow = true),
            UIT(CFG(.ref = REFS(&s->text), .scale = s->text_scale, .colour = G.C.UI.TEXT_LIGHT))));
    if (s->label) {
        t = UIR(CFG(.align = "cm", .minh = 1, .minw = 1, .padding = 0.1 * s->label_scale, .colour = G.C.CLEAR),
            UIR(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = (char *)s->label, .scale = s->label_scale, .colour = G.C.UI.TEXT_LIGHT))),
            UIR(CFG(.align = "cm", .padding = 0), t));
    }
    return t;
}

GF_IMPL(slider) {
    UIElement *c = uie_child(e, 1);
    if (!c) return;
    e->n.states.drag.can = true;
    c->n.states.drag.can = true;
    Node *dt = G.CONTROLLER.dragging.target;
    if (dt && (dt == &e->n || dt == &c->n)) {
        SliderState *rt = c->config->ref_table;
        double v = MIN(rt->max, MAX(rt->min, rt->min + (rt->max - rt->min) * (G.CURSOR->n.T->x - e->n.parent->T->x - G.ROOM->T->x) / e->n.T->w));
        ref_set_num(rt->ref, v);
        slider_text(rt);
        c->n.T->w = (v - rt->min) / (rt->max - rt->min) * rt->w;
        c->config->w = c->n.T->w;
        if (rt->callback) rt->callback(rt);
    }
}

void G_FUNCS_slider_descreet(UIElement *e, double per) {
    UIElement *c = uie_child(e, 1);
    if (!c) return;
    e->n.states.drag.can = true;
    c->n.states.drag.can = true;
    if (!isnan(per)) {
        SliderState *rt = c->config->ref_table;
        double v = MIN(rt->max, MAX(rt->min, ref_num(rt->ref) + per * (rt->max - rt->min)));
        ref_set_num(rt->ref, v);
        slider_text(rt);
        c->n.T->w = (v - rt->min) / (rt->max - rt->min) * rt->w;
        c->config->w = c->n.T->w;
    }
}

/* ------------------------------------------------------------------ */
/* toggle                                                              */
/* ------------------------------------------------------------------ */
static bool toggle_dummy;

UIDef *create_toggle(ToggleArgs args) {
    ToggleState *s = NEW(ToggleState);
    *s = args;
    if (!s->active_colour) s->active_colour = G.C.RED;
    if (!s->inactive_colour) s->inactive_colour = G.C.BLACK;
    if (!s->w) s->w = 3;
    if (!s->h) s->h = 0.5;
    if (!s->scale) s->scale = 1;
    if (!s->label) s->label = "TEST?";
    if (!s->label_scale) s->label_scale = 0.4;
    if (!s->ref) s->ref = &toggle_dummy;
    Sprite *check = sprite_new(0, 0, 0.5 * s->scale, 0.5 * s->scale, atlas_get("icons"), (SpritePos){ 1, 0 });
    check->n.states.drag.can = false;
    check->n.states.visible = false;
    UIDef *info = NULL;
    if (s->ninfo) {
        info = UIR(CFG(.align = "cm", .minh = 0.05));
        for (int i = 0; i < s->ninfo; i++)
            uidef_add(info, UIR(CFG(.align = "cm", .minh = 0.05), UIT(CFG(.text = (char *)s->info[i], .scale = 0.25, .colour = G.C.UI.TEXT_LIGHT))));
    }
    UIDef *t = UIN(s->col ? UIT_C : UIT_R, CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.CLEAR, .focus_args = FOCUS(.funnel_from_set = true)),
        UIC(CFG(.align = "cr", .minw = s->w),
            UIT(CFG(.text = (char *)s->label, .scale = s->label_scale, .colour = G.C.UI.TEXT_LIGHT)),
            UIB(CFG(.w = 0.1, .h = 0.1))),
        UIC(CFG(.align = "cl", .minw = 0.3 * s->w),
            UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.BLACK),
                UIC(CFG(.align = "cm", .r = 0.1, .padding = 0.03, .minw = 0.4 * s->scale, .minh = 0.4 * s->scale, .outline_colour = G.C.WHITE,
                        .outline = 1.2 * s->scale, .line_emboss = 0.5 * s->scale, .ref_table = s, .colour = s->inactive_colour,
                        .button = G_FUNCS_toggle_button, .button_dist = 0.2, .hover = true, .func = G_FUNCS_toggle,
                        .focus_args = FOCUS(.funnel_to_set = true)),
                    UIO(CFG(.object = &check->n))))));
    if (info) t = UIN(s->col ? UIT_C : UIT_R, CFG(.align = "cm"), t, info);
    return t;
}

GF_IMPL(toggle_button) {
    ToggleState *s = e->config->ref_table;
    *s->ref = !*s->ref;
    if (s->callback) s->callback(*s->ref);
}

GF_IMPL(toggle) {
    ToggleState *s = e->config->ref_table;
    UIElement *c1 = uie_child(e, 1);
    if (!*s->ref && e->config->toggle_active) {
        e->config->toggle_active = false;
        e->config->colour = s->inactive_colour;
        if (c1) { c1->n.states.visible = false; if (c1->config->object) c1->config->object->states.visible = false; }
    } else if (*s->ref && !e->config->toggle_active) {
        e->config->toggle_active = true;
        e->config->colour = s->active_colour;
        if (c1) { c1->n.states.visible = true; if (c1->config->object) c1->config->object->states.visible = true; }
    }
}

/* ------------------------------------------------------------------ */
/* option cycle                                                        */
/* ------------------------------------------------------------------ */
UIDef *create_option_cycle(OptionCycleArgs args) {
    CycleState *s = NEW(CycleState);
    *s = args;
    if (!s->colour) s->colour = G.C.RED;
    if (!s->options) { s->options = STRS("Option 1", "Option 2"); s->noptions = 2; }
    if (!s->current_option) s->current_option = 1;
    s->current_option_val = s->options[s->current_option - 1];
    if (!s->scale) s->scale = 1;
    s->w = (s->w ? s->w : 2.5) * s->scale;
    s->h = (s->h ? s->h : 0.8) * s->scale;
    s->text_scale = (s->text_scale ? s->text_scale : 0.5) * s->scale;
    s->l = "<";
    s->r = ">";
    if (!s->focus_args) s->focus_args = FOCUS(.type = NULL);
    s->focus_args->type = "cycle";

    UIDef *info = NULL;
    if (s->ninfo) {
        info = UIR(CFG(.align = "cm", .minh = 0.05));
        for (int i = 0; i < s->ninfo; i++)
            uidef_add(info, UIR(CFG(.align = "cm", .minh = 0.05), UIT(CFG(.text = (char *)s->info[i], .scale = 0.3 * s->scale, .colour = G.C.UI.TEXT_LIGHT))));
    }
    bool disabled = s->noptions < 2;
    UIDef *pips = uinodes();
    for (int i = 1; i <= s->noptions; i++)
        uidef_add(pips, UIB(CFG(.w = 0.1 * s->scale, .h = 0.1 * s->scale, .r = 0.05, .id = intern(tfmt("pip_%d", i)),
                                .colour = s->current_option == i ? G.C.WHITE : G.C.BLACK)));
    UIDef *choice_pips = !s->no_pips
        ? uidef_add_all(UIR(CFG(.align = "cm", .padding = (0.05 - (s->noptions > 15 ? 0.03 : 0)) * s->scale)), pips) : NULL;

    UIDef *mid = s->mid
        ? UIC(CFG(.id = "cycle_main"),
              UIR(CFG(.align = "cm", .minh = 0.05), s->mid),
              (!disabled && choice_pips) ? choice_pips : UISKIP)
        : UIC(CFG(.id = "cycle_main", .align = "cm", .minw = s->w, .minh = s->h, .r = 0.1, .padding = 0.05, .colour = s->colour, .emboss = 0.1, .hover = true,
                  .can_collide = 1, .on_demand_tooltip = s->on_demand_tooltip),
              UIR(CFG(.align = "cm"),
                  UIR(CFG(.align = "cm"),
                      UIO(CFG(.object = &DYNATEXT(DT_REF(REFCS(&s->current_option_val)), DT_COL1(G.C.UI.TEXT_LIGHT), .pop_in = 0, .has_pop_in = true,
                                                  .pop_in_rate = 8, .reset_pop_in = true, .shadow = true, .float_ = true, .silent = true, .bump = true,
                                                  .scale = s->text_scale, .non_recalc = true)->n))),
                  UIR(CFG(.align = "cm", .minh = 0.05)),
                  (!disabled && choice_pips) ? choice_pips : UISKIP));
    UIDef *t = UIC(CFG(.align = "cm", .padding = 0.1, .r = 0.1, .colour = G.C.CLEAR, .id = (s->id && !s->label) ? s->id : NULL, .focus_args = s->focus_args),
        UIC(CFG(.align = "cm", .r = 0.1, .minw = 0.6 * s->scale, .hover = !disabled, .colour = !disabled ? s->colour : G.C.BLACK, .shadow = !disabled,
                .button = !disabled ? G_FUNCS_option_cycle : NULL, .ref_table = s, .ref_value = "l", .focus_args = FOCUS(.type = "none")),
            UIT(CFG(.ref = REFCS(&s->l), .scale = s->text_scale, .colour = !disabled ? G.C.UI.TEXT_LIGHT : G.C.UI.TEXT_INACTIVE))),
        mid,
        UIC(CFG(.align = "cm", .r = 0.1, .minw = 0.6 * s->scale, .hover = !disabled, .colour = !disabled ? s->colour : G.C.BLACK, .shadow = !disabled,
                .button = !disabled ? G_FUNCS_option_cycle : NULL, .ref_table = s, .ref_value = "r", .focus_args = FOCUS(.type = "none")),
            UIT(CFG(.ref = REFCS(&s->r), .scale = s->text_scale, .colour = !disabled ? G.C.UI.TEXT_LIGHT : G.C.UI.TEXT_INACTIVE))));
    if (s->cycle_shoulders) {
        t = UIR(CFG(.align = "cm", .colour = G.C.CLEAR),
            UIC(CFG(.minw = 0.7, .align = "cm", .colour = G.C.CLEAR, .func = G_FUNCS_set_button_pip,
                    .focus_args = FOCUS(.button = "leftshoulder", .type = "none", .orientation = "cm", .scale = 0.7, .has_offset = true, .offset = { -0.1, 0 }))),
            UIC(CFG(.id = "cycle_shoulders", .padding = 0.1), t),
            UIC(CFG(.minw = 0.7, .align = "cm", .colour = G.C.CLEAR, .func = G_FUNCS_set_button_pip,
                    .focus_args = FOCUS(.button = "rightshoulder", .type = "none", .orientation = "cm", .scale = 0.7, .has_offset = true, .offset = { 0.1, 0 }))));
    } else {
        t = UIR(CFG(.align = "cm", .colour = G.C.CLEAR, .padding = 0.0), t);
    }
    if (s->label || info) {
        t = UIR(CFG(.align = "cm", .padding = 0.05, .id = s->id),
            UIIF(s->label, UIR(CFG(.align = "cm"), UIT(CFG(.text = (char *)s->label, .scale = 0.5 * s->scale, .colour = G.C.UI.TEXT_LIGHT)))),
            t,
            info ? info : UISKIP);
    }
    return t;
}

typedef struct { UIElement *e; } HoverCtx;
EVFN(ev_rehover) { HoverCtx *c = p_; (void)e_; node_hover(&c->e->n); return true; }

GF_IMPL(option_cycle) {
    CycleState *s = e->config->ref_table;
    const char *from_val = s->options[s->current_option - 1];
    int from_key = s->current_option;
    UIElement *pp = e->parent_uie ? e->parent_uie->parent_uie : NULL;
    UIElement *old_pip = UIBox_get_UIE_by_ID(e->UIBox, tfmt("pip_%d", s->current_option), pp);
    UIElement *cycle_main = UIBox_get_UIE_by_ID(e->UIBox, "cycle_main", pp);
    if (cycle_main && cycle_main->n.h_popup) {
        node_stop_hover(&cycle_main->n);
        HoverCtx *c = CTX(HoverCtx); c->e = cycle_main;
        add_event(EVENT(.func = ev_rehover, .ctx = c));
    }
    if (str_eq(e->config->ref_value, "l")) {
        s->current_option--;
        if (s->current_option <= 0) s->current_option = s->noptions;
    } else {
        s->current_option++;
        if (s->current_option > s->noptions) s->current_option = 1;
    }
    const char *to_val = s->options[s->current_option - 1];
    int to_key = s->current_option;
    s->current_option_val = to_val;
    UIElement *new_pip = UIBox_get_UIE_by_ID(e->UIBox, tfmt("pip_%d", s->current_option), pp);
    if (old_pip) old_pip->config->colour = G.C.BLACK;
    if (new_pip) new_pip->config->colour = G.C.WHITE;
    if (s->opt_callback) {
        CycleCallbackArgs a = { .from_val = from_val, .to_val = to_val, .from_key = from_key, .to_key = to_key, .cycle_config = s };
        s->opt_callback(&a);
    }
}

/* ------------------------------------------------------------------ */
/* tabs                                                                */
/* ------------------------------------------------------------------ */
UIDef *create_tabs(TabsArgs args) {
    TabsArgs *a = NEW(TabsArgs);
    *a = args;
    if (!a->colour) a->colour = G.C.RED;
    if (!a->tab_alignment) a->tab_alignment = "cm";
    if (!a->scale) a->scale = 1;
    if (!a->text_scale) a->text_scale = 0.5;
    TabSpec *tabs = xcalloc((size_t)(a->ntabs > 0 ? a->ntabs : 1), sizeof(TabSpec));
    memcpy(tabs, a->tabs, (size_t)a->ntabs * sizeof(TabSpec));
    TabSpec *current = a->ntabs ? &tabs[0] : NULL;
    UIDef *buttons = uinodes();
    for (int k = 0; k < a->ntabs; k++) {
        TabSpec *v = &tabs[k];
        if (v->chosen) current = v;
        uidef_add(buttons, UIBUTTON(.id = tfmt("tab_but_%s", v->label ? v->label : ""), .ref_table = v, .button = G_FUNCS_change_tab, .label = { v->label },
                                    .nlabel = 1, .minh = 0.8 * a->scale, .minw = 2.5 * a->scale, .col = true, .choice = true, .scale = a->text_scale,
                                    .chosen = v->chosen, .func = v->func, .focus_args = FOCUS(.type = "none")));
    }
    bool shoulders = a->ntabs > 1 && !a->no_shoulders;
    UIConfig *tcfg = CFG(.id = a->no_shoulders ? "no_shoulders" : "tab_shoulders", .ref_table = a, .align = "cm", .padding = 0.15, .group = "1", .collideable = true,
                         .focus_args = a->ntabs > 1 ? FOCUS(.type = "tab", .nav = "wide", .snap_to = a->snap_to_nav, .no_loop = a->no_loop) : NULL);
    UIDef *content = current && current->tab_definition_function ? current->tab_definition_function(current->tab_definition_function_args) : UIROOT(CFG(.align = "cm"));
    UIBox *box = uibox_new(content, UIBCFG(.has_offset = true, .offset = { 0, 0 }));
    return UIR(CFG(.padding = 0.0, .align = "cm", .colour = G.C.CLEAR),
        UIR(CFG(.align = "cm", .colour = G.C.CLEAR),
            UIIF(shoulders, UIC(CFG(.minw = 0.7, .align = "cm", .colour = G.C.CLEAR, .func = G_FUNCS_set_button_pip,
                                    .focus_args = FOCUS(.button = "leftshoulder", .type = "none", .orientation = "cm", .scale = 0.7, .has_offset = true,
                                                        .offset = { -0.1, 0 })))),
            uidef_add_all(UIC(tcfg), buttons),
            UIIF(shoulders, UIC(CFG(.minw = 0.7, .align = "cm", .colour = G.C.CLEAR, .func = G_FUNCS_set_button_pip,
                                    .focus_args = FOCUS(.button = "rightshoulder", .type = "none", .orientation = "cm", .scale = 0.7, .has_offset = true,
                                                        .offset = { 0.1, 0 }))))),
        UIR(CFG(.align = a->tab_alignment, .padding = a->has_padding ? a->padding : 0.1, .no_fill = true, .minh = a->tab_h, .minw = a->tab_w),
            UIO(CFG(.id = "tab_contents", .object = &box->n))));
}

GF_IMPL(change_tab) {
    if (!e) return;
    UIElement *io = G.OVERLAY_MENU ? UIBox_get_UIE_by_ID(G.OVERLAY_MENU, "overlay_menu_infotip", NULL) : NULL;
    if (io && io->config->object) {
        node_remove(io->config->object);
        io->config->object = moveable_new(0, 0, 0, 0);
    }
    UIElement *tc = UIBox_get_UIE_by_ID(e->UIBox, "tab_contents", NULL);
    if (!tc) return;
    if (tc->config->object) node_remove(tc->config->object);
    TabSpec *v = e->config->ref_table;
    UIDef *def = v && v->tab_definition_function ? v->tab_definition_function(v->tab_definition_function_args) : UIROOT(CFG(.align = "cm"));
    UIBox *b = uibox_new(def, UIBCFG(.has_offset = true, .offset = { 0, 0 }, .parent = &tc->n, .align = "cm"));
    tc->config->object = &b->n;
    UIBox_recalculate(tc->UIBox);
}

/* ------------------------------------------------------------------ */
/* text input                                                          */
/* ------------------------------------------------------------------ */
UIDef *create_text_input(TextInputArgs args) {
    TextInputState *s = NEW(TextInputState);
    *s = args;
    s->colour = col_copy(s->colour ? s->colour : G.C.BLUE);
    s->hooked_colour = s->hooked_colour ? col_copy(s->hooked_colour) : darken(G.C.BLUE, 0.3);
    if (!s->w) s->w = 2.5;
    if (!s->h) s->h = 0.7;
    if (!s->text_scale) s->text_scale = 0.4;
    if (!s->max_length) s->max_length = 16;
    if (!s->prompt_text) s->prompt_text = localize_key("k_enter_text");
    s->current_prompt_text = xstrdup("");
    if (!*s->ref) *s->ref = xstrdup("");
    const char *cur = *s->ref;
    s->text.current_position = (int)strlen(cur);
    s->text.letters = xcalloc((size_t)s->max_length + 1, sizeof(char *));
    UIDef *ui_letters = uinodes();
    for (int i = 0; i < s->max_length; i++) {
        char buf[2] = { i < (int)strlen(cur) ? cur[i] : 0, 0 };
        s->text.letters[i] = xstrdup(buf);
        uidef_add(ui_letters, UIT(CFG(.ref = REFS(&s->text.letters[i]), .scale = s->text_scale, .colour = G.C.UI.TEXT_LIGHT, .id = intern(tfmt("letter_%d", i + 1)))));
    }
    Col position_text_colour = lighten(G.C.BLUE, 0.4);
    uidef_add(ui_letters, UIT(CFG(.ref = REFS(&s->current_prompt_text), .scale = s->text_scale, .colour = lighten(s->colour, 0.4), .id = "prompt")));
    uidef_add(ui_letters, UIB(CFG(.r = 0.03, .w = 0.1, .h = 0.4, .colour = position_text_colour, .id = "position", .func = G_FUNCS_flash)));
    return UIC(CFG(.align = "cm", .draw_layer = 1, .colour = G.C.CLEAR),
        UIC(CFG(.id = "text_input", .align = "cm", .padding = 0.05, .r = 0.1, .draw_layer = 2, .hover = true, .colour = s->colour, .minw = s->w,
                .button = G_FUNCS_select_text_input, .shadow = true),
            UIR(CFG(.ref_table = s, .padding = 0.05, .align = "cm", .r = 0.1, .colour = G.C.CLEAR),
                uidef_add_all(UIR(CFG(.ref_table = s, .align = "cm", .r = 0.1, .colour = G.C.CLEAR, .func = G_FUNCS_text_input)), ui_letters))));
}

static UIDef *create_keyboard_button(const char *key, const char *binding) {
    const char *label = !strcmp(key, "backspace") ? "Backspace" : !strcmp(key, " ") ? "Space" : !strcmp(key, "back") ? "Back" : !strcmp(key, "return") ? "Enter" : key;
    char **kref = NEW(char *);
    *kref = xstrdup(!strcmp(key, "back") ? "return" : key);
    double minw = !strcmp(key, " ") ? 6 : (!strcmp(key, "return") || !strcmp(key, "backspace") || !strcmp(key, "back")) ? 2.5 : 0.8;
    double minh = (!strcmp(key, "return") || !strcmp(key, "backspace")) ? 1.5 : !strcmp(key, "back") ? 0.8 : 0.7;
    return UIBUTTON(.label = { intern(label) }, .nlabel = 1, .button = G_FUNCS_key_button, .ref_table = kref, .minw = minw, .minh = minh, .col = true,
                    .colour = G.C.GREY,
                    .focus_args = binding ? FOCUS(.button = binding, .orientation = (!strcmp(key, " ") || !strcmp(key, "back")) ? "cr" : "bm",
                                                  .set_button_pip = true) : NULL);
}

UIDef *create_keyboard_input(KeyboardArgs args) {
    const char *rows[5] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", args.space_key ? " " : NULL };
    UIDef *brows[5];
    for (int k = 0; k < 5; k++) {
        brows[k] = UIR(CFG(.align = "cm", .padding = 0.07, .colour = G.C.CLEAR));
        if (!rows[k]) continue;
        for (const char *p = rows[k]; *p; p++) {
            char c[2] = { *p, 0 };
            uidef_add(brows[k], create_keyboard_button(intern(c), *p == ' ' ? "y" : NULL));
        }
    }
    UIDef *side = NULL;
    if (args.backspace_key || args.return_key) {
        side = UIC(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            UIIF(args.backspace_key, UIR(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR), create_keyboard_button("backspace", "x"))),
            UIIF(args.return_key, UIR(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR), create_keyboard_button("return", "start"))),
            UIR(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR), create_keyboard_button("back", "b")));
    }
    return UIROOT(CFG(.align = "cm", .padding = 15, .r = 0.1, .colour = col_new(G.C.GREY[0], G.C.GREY[1], G.C.GREY[2], 0.7f)),
        UIC(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR),
            UIC(CFG(.align = "cm", .padding = 0.05, .colour = G.C.BLACK, .emboss = 0.05, .r = 0.1, .mid = true),
                UIR(CFG(.align = "cm", .padding = 0.05),
                    UIC(CFG(.align = "cm", .padding = 0.05, .colour = G.C.CLEAR), brows[0], brows[1], brows[2], brows[3], brows[4]),
                    side ? side : UISKIP))));
}

GF_IMPL(key_button) {
    char **k = e->config->ref_table;
    if (k && *k) Controller_key_press_update(&G.CONTROLLER, *k, 0);
}

GF_IMPL(text_input) {
    TextInputState *a = e->config->ref_table;
    UIElement *pp = e->parent_uie ? e->parent_uie->parent_uie : NULL;
    if (G.CONTROLLER.text_input_hook == e) {
        if (pp) pp->config->colour = a->hooked_colour;
        str_set(&a->current_prompt_text, "");
    } else {
        if (pp) pp->config->colour = a->colour;
        str_set(&a->current_prompt_text, (*a->ref && (*a->ref)[0]) ? "" : a->prompt_text);
    }
    UIElement *osk = pp ? pp->parent_uie : NULL;
    if (!osk) return;
    Node *kb = child_get(&osk->n, "controller_keyboard");
    if (G.CONTROLLER.text_input_hook == e && G.CONTROLLER.HID.controller) {
        if (!kb) {
            UIBox *b = uibox_new(create_keyboard_input((KeyboardArgs){ .backspace_key = true, .return_key = true, .space_key = false }),
                                 UIBCFG(.align = "cm", .has_offset = true, .offset = { 0, a->has_keyboard_offset ? a->keyboard_offset : -4 },
                                        .major = &e->UIBox->n, .parent = &osk->n));
            child_set(&osk->n, "controller_keyboard", &b->n);
            G.CONTROLLER.screen_keyboard = b;
            Controller_mod_cursor_context_layer(&G.CONTROLLER, 1);
        }
    } else if (kb) {
        child_set(&osk->n, "controller_keyboard", NULL);
        node_remove(kb);
        G.CONTROLLER.screen_keyboard = NULL;
        Controller_mod_cursor_context_layer(&G.CONTROLLER, -1);
    }
}

/* hook children are all unnamed UIElements: letters, prompt, position */
static Node **hook_child_slot(UIElement *hook, int i) { return &hook->n.children.items[i].node; }
static int hook_count(UIElement *hook) { return hook->n.children.count; }
static UIElement *hook_child(UIElement *hook, int i) { return (UIElement *)hook->n.children.items[i].node; }

static bool is_real_letter(UIElement *c) {
    return c && c->config && c->config->id && !strncmp(c->config->id, "letter_", 7) && c->config->text && c->config->text[0];
}

static char *get_text_from_input(void) {
    UIElement *hook = G.CONTROLLER.text_input_hook;
    StrBuf sb; sb_init(&sb);
    for (int i = 0; i < hook_count(hook); i++) if (is_real_letter(hook_child(hook, i))) sb_add(&sb, hook_child(hook, i)->config->text);
    return sb_take(&sb);
}

static void modify_text_input(TextInputState *s, const char *letter, int pos, bool del) {
    /* pos is 1-based like the Lua */
    for (;;) {
        if (del && pos > 0) {
            if (pos >= s->max_length) { str_set(&s->text.letters[pos - 1], ""); return; }
            str_set(&s->text.letters[pos - 1], s->text.letters[pos]);
            pos++;
            continue;
        }
        if (pos < 1 || pos > s->max_length) return;
        char *swapped = xstrdup(s->text.letters[pos - 1] ? s->text.letters[pos - 1] : "");
        str_set(&s->text.letters[pos - 1], letter);
        if (swapped[0]) {
            static char buf[8];
            snprintf(buf, sizeof buf, "%s", swapped);
            free(swapped);
            letter = buf;
            pos++;
            continue;
        }
        free(swapped);
        return;
    }
}

static void transpose_text_input(int amount) {
    UIElement *hook = G.CONTROLLER.text_input_hook;
    TextInputState *s = hook->config->ref_table;
    int position_child = -1;   /* 1-based */
    for (int i = 0; i < hook_count(hook); i++) {
        UIElement *c = hook_child(hook, i);
        if (c->config && str_eq(c->config->id, "position")) { position_child = i + 1; break; }
    }
    if (position_child < 0) return;
    int dir = amount > 0 ? 1 : amount < 0 ? -1 : 0;
    while (amount != 0) {
        if (position_child + dir < 1 || position_child + dir >= hook_count(hook)) break;
        bool real = is_real_letter(hook_child(hook, position_child + dir - 1));
        Node **a = hook_child_slot(hook, position_child - 1), **b = hook_child_slot(hook, position_child + dir - 1);
        Node *tmp = *a; *a = *b; *b = tmp;
        if (real) amount -= dir;
        position_child += dir;
    }
    s->text.current_position = MIN(position_child - 1, (int)strlen(*s->ref));
    UIBox_recalculate(hook->UIBox);
    free(*s->ref);
    *s->ref = get_text_from_input();
}

GF_IMPL(select_text_input) {
    UIElement *c1 = uie_child(e, 1);
    UIElement *c2 = c1 ? uie_child(c1, 1) : NULL;
    if (!c2) return;
    G.CONTROLLER.text_input_hook = c2;
    transpose_text_input(0);
    UIBox_recalculate(e->UIBox);
}

void G_FUNCS_text_input_key(UIElement *hook_unused, const char *key_in, bool caps) {
    (void)hook_unused;
    UIElement *hook = G.CONTROLLER.text_input_hook;
    if (!hook) return;
    if (!key_in) key_in = "%";
    if (!strcmp(key_in, "[") || !strcmp(key_in, "]")) return;
    char key[32];
    snprintf(key, sizeof key, "%s", !strcmp(key_in, "0") ? "o" : key_in);
    TextInputState *hc = hook->config->ref_table;
    if (!hc->orig_colour) hc->orig_colour = col_copy(hc->colour);
    caps = caps || G.CONTROLLER.capslock || hc->all_caps;
    const char *corpus = hc->extended_corpus
        ? "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz 0!$&()<>?:{}+-=,.[]_"
        : "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    if (hc->extended_corpus && caps && strlen(key) == 1) {
        const char *lower_ext = "1234567890-=;',./", *upper_ext = "!@#$%^&*()_+:\"<>?";
        const char *f = strchr(lower_ext, key[0]);
        if (f) key[0] = upper_ext[f - lower_ext];
    }
    const char *mapped = !strcmp(key, "space") ? " " : !strcmp(key, "backspace") ? "BACKSPACE" : !strcmp(key, "delete") ? "DELETE" :
                         !strcmp(key, "return") ? "RETURN" : !strcmp(key, "right") ? "RIGHT" : !strcmp(key, "left") ? "LEFT" : NULL;
    if (mapped) snprintf(key, sizeof key, "%s", mapped);
    else if (caps) snprintf(key, sizeof key, "%s", tupper(key));
    transpose_text_input(0);
    size_t len = strlen(*hc->ref);
    if (len > 0 && !strcmp(key, "BACKSPACE")) {
        modify_text_input(hc, "", hc->text.current_position, true);
        transpose_text_input(-1);
    } else if (len > 0 && !strcmp(key, "DELETE")) {
        modify_text_input(hc, "", hc->text.current_position + 1, true);
        transpose_text_input(0);
    } else if (!strcmp(key, "RETURN")) {
        if (hc->callback) hc->callback();
        UIElement *pp = hook->parent_uie ? hook->parent_uie->parent_uie : NULL;
        if (pp) pp->config->colour = hc->colour;
        Col temp = col_copy(hc->orig_colour);
        hc->colour[0] = G.C.WHITE[0]; hc->colour[1] = G.C.WHITE[1]; hc->colour[2] = G.C.WHITE[2];
        ease_colour(hc->colour, temp, NAN);
        G.CONTROLLER.text_input_hook = NULL;
    } else if (!strcmp(key, "LEFT")) transpose_text_input(-1);
    else if (!strcmp(key, "RIGHT")) transpose_text_input(1);
    else if ((size_t)hc->max_length > len && strlen(key) == 1 && strchr(corpus, key[0])) {
        modify_text_input(hc, key, hc->text.current_position + 1, false);
        transpose_text_input(1);
    }
}

GF_IMPL(paste_seed) {
    UIElement *ti = UIBox_get_UIE_by_ID(e->UIBox, "text_input", NULL);
    UIElement *c1 = ti ? uie_child(ti, 1) : NULL;
    UIElement *c2 = c1 ? uie_child(c1, 1) : NULL;
    if (!c2) return;
    G.CONTROLLER.text_input_hook = c2;
    for (int i = 0; i < 8; i++) G_FUNCS_text_input_key(NULL, "right", false);
    for (int i = 0; i < 8; i++) G_FUNCS_text_input_key(NULL, "backspace", false);
    const char *clip = gfx_get_clipboard();
    if (clip) {
        for (const char *p = clip; *p; p++) {
            char c[2] = { *p, 0 };
            G_FUNCS_text_input_key(NULL, c, false);
        }
    }
    G_FUNCS_text_input_key(NULL, "return", false);
}
