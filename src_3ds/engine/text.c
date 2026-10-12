/* text.c - port of engine/text.lua (DynaText) */
#include "../game/game.h"

static FontDef *dt_font(DynaText *t) { return (FontDef *)t->font; }

DynaTextPart *dt_parts_str(const char *s) {
    DynaTextPart *p = xcalloc(1, sizeof(DynaTextPart));
    p->string = xstrdup(s ? s : "");
    return p;
}
DynaTextPart *dt_parts_strs(const char **s, int n) {
    DynaTextPart *p = xcalloc((size_t)(n > 0 ? n : 1), sizeof(DynaTextPart));
    for (int i = 0; i < n; i++) p[i].string = xstrdup(s[i] ? s[i] : "");
    return p;
}
DynaTextPart dt_part_ref(Ref r) {
    DynaTextPart p;
    memset(&p, 0, sizeof p);
    p.ref = r;
    return p;
}
DynaTextPart *dt_parts_ref(Ref r, const char *prefix, const char *suffix) {
    DynaTextPart *p = xcalloc(1, sizeof(DynaTextPart));
    p->ref = r;
    if (prefix) p->prefix = xstrdup(prefix);
    if (suffix) p->suffix = xstrdup(suffix);
    return p;
}
Col *dt_cols_arr(const Col *c, int n) {
    Col *r = xcalloc((size_t)(n > 0 ? n : 1), sizeof(Col));
    memcpy(r, c, (size_t)n * sizeof(Col));
    return r;
}
Col *dt_cols(int n, ...) {
    Col *r = xcalloc((size_t)(n > 0 ? n : 1), sizeof(Col));
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) r[i] = va_arg(ap, Col);
    va_end(ap);
    return r;
}

static void DynaText_update(Node *n, double dt) {
    (void)dt;
    DynaText *t = (DynaText *)n;
    DynaText_update_text(t, false);
    DynaText_align_letters(t);
}

void DynaText_update_text(DynaText *t, bool first_pass) {
    DynaTextConfig *c = &t->config;
    FontDef *f = dt_font(t);
    c->W = 0;
    c->H = 0;
    for (int k = 0; k < c->nparts; k++) {
        DynaTextPart *v = &c->parts[k];
        bool is_table = ref_valid(v->ref) || v->prefix || v->suffix || v->colour || v->outer_colour || v->scale != 0;
        while (t->strings.count <= k) {
            DynaString ds;
            memset(&ds, 0, sizeof ds);
            vec_push(&t->strings, ds);
        }
        DynaString *s = &t->strings.items[k];
        if (ref_valid(v->ref) || first_pass) {
            int part_a = 0, part_b = 1000000;
            const char *new_string;
            Col outer = NULL, inner = NULL;
            double part_scale = 1;
            if (is_table) {
                const char *val = ref_valid(v->ref) ? ref_tostring(v->ref) : (v->string ? v->string : "");
                new_string = tfmt("%s%s%s", v->prefix ? v->prefix : "", val, v->suffix ? v->suffix : "");
                part_a = v->prefix ? (int)strlen(v->prefix) : 0;
                part_b = (int)strlen(new_string) - (v->suffix ? (int)strlen(v->suffix) : 0);
                if (v->scale != 0) part_scale = v->scale;
                if (first_pass) { outer = v->outer_colour; inner = v->colour; }
            } else new_string = v->string ? v->string : "";
            if (!s->exists || !str_eq(s->string, new_string) || first_pass) {
                if (t->start_pop_in) t->reset_pop_in = true;
                t->reset_pop_in = t->reset_pop_in || c->reset_pop_in;
                if (!t->reset_pop_in) {
                    c->has_pop_out = false;
                    c->has_pop_in = false;
                } else {
                    if (!c->has_pop_in) { c->pop_in = 0; c->has_pop_in = true; }
                    t->created_time = G.TIMERS.REAL;
                }
                char *ns = xstrdup(new_string);
                free(s->string);
                s->string = ns;
                s->exists = true;
                VEC(DynaLetter) old = { s->letters.items, s->letters.count, s->letters.cap };
                s->letters.items = NULL; s->letters.count = s->letters.cap = 0;
                double tw = 0, th = 0;
                int cur = 1;
                const char *p = ns;
                while (*p) {
                    int len;
                    int cp = utf8_decode(p, &len);
                    DynaLetter L;
                    memset(&L, 0, sizeof L);
                    memcpy(L.letter, p, (size_t)MIN(len, 7));
                    p += len;
                    DynaLetter *ol = (cur - 1 < old.count) ? &old.items[cur - 1] : NULL;
                    L.scale = ol ? ol->scale : part_scale;
                    double tx = gfx_font_char_width(f->FONT, cp) * t->scale * part_scale * G.TILESCALE * f->FONTSCALE + 2.7 * c->spacing * G.TILESCALE * f->FONTSCALE;
                    double ty = gfx_font_height(f->FONT) * t->scale * part_scale * G.TILESCALE * f->FONTSCALE * f->TEXT_HEIGHT_SCALE;
                    L.offset = ol ? ol->offset : (Vec2){ 0, 0 };
                    L.dims.x = tx / (f->FONTSCALE * G.TILESCALE);
                    L.dims.y = ty / (f->FONTSCALE * G.TILESCALE);
                    L.pop_in = first_pass ? (ol ? ol->pop_in : (c->has_pop_in ? 0 : 1)) : 1;
                    L.prefix = cur <= part_a ? outer : NULL;
                    L.suffix = cur > part_b ? outer : NULL;
                    L.colour = inner;
                    if (k > 0) L.pop_in = 0;
                    tw += tx / (G.TILESIZE * G.TILESCALE);
                    th = MAX(ty / (G.TILESIZE * G.TILESCALE), th);
                    vec_push(&s->letters, L);
                    cur++;
                }
                free(old.items);
                s->W = tw;
                s->H = th;
            }
        }
        if (s->W > c->W) { c->W = s->W; s->W_offset = 0; }
        if (s->H > c->H) { c->H = s->H; s->H_offset = 0; }
    }
    if (t->n.T) {
        if ((t->n.T->w != c->W || t->n.T->h != c->H) && (!first_pass || t->reset_pop_in)) {
            t->n.ui_object_updated = true;
            t->n.non_recalc = c->non_recalc;
        }
        t->n.T->w = c->W;
        t->n.T->h = c->H;
    }
    t->reset_pop_in = false;
    t->start_pop_in = false;
    for (int k = 0; k < t->strings.count; k++) {
        DynaString *v = &t->strings.items[k];
        v->W_offset = 0.5 * (c->W - v->W);
        v->H_offset = 0.5 * (c->H - v->H + c->offset_y);
    }
}

DynaText *dynatext_new(DynaTextConfig cfg) {
    DynaText *t = NEW(DynaText);
    t->config = cfg;
    DynaTextConfig *c = &t->config;
    t->shadow = c->shadow;
    t->scale = c->scale != 0 ? c->scale : 1;
    t->pop_in_rate = c->pop_in_rate != 0 ? c->pop_in_rate : 3;
    t->bump_rate = c->bump_rate != 0 ? c->bump_rate : 2.666;
    t->bump_amount = c->bump_amount != 0 ? c->bump_amount : 1;
    t->font = c->font ? c->font : G.LANG->font;
    if (!c->parts || c->nparts == 0) { c->parts = dt_parts_str("HELLO WORLD"); c->nparts = 1; }
    FontDef *f = dt_font(t);
    t->text_offset.x = f->TEXT_OFFSET.x * t->scale + c->x_offset;
    t->text_offset.y = f->TEXT_OFFSET.y * t->scale + c->y_offset;
    if (c->colours && c->ncolours > 0) { t->colours = c->colours; t->ncolours = c->ncolours; }
    else { t->colours = dt_cols(1, G.C.RED); t->ncolours = 1; }
    t->created_time = G.TIMERS.REAL;
    t->silent = c->silent;
    t->start_pop_in = c->has_pop_in;
    c->W = 0; c->H = 0;
    t->focused_string = 1;
    t->n.T = NULL;
    DynaText_update_text(t, true);
    if (c->maxw != 0 && c->W > c->maxw) {
        t->start_pop_in = c->has_pop_in;
        t->scale = t->scale * (c->maxw / c->W);
        DynaText_update_text(t, true);
    }
    if (t->strings.count > 1) {
        t->pop_delay = c->has_pop_delay ? c->pop_delay : 1.5;
        DynaText_pop_out(t, 4);
    }
    moveable_init(&t->n, &DYNATEXT_VT, c->X, c->Y, c->W, c->H);
    t->n.T->r = c->text_rot;
    t->n.states.hover.can = false;
    t->n.states.click.can = false;
    t->n.states.collide.can = false;
    t->n.states.drag.can = false;
    t->n.states.release_on.can = false;
    Moveable_set_role(&t->n, ROLE_ARGS(.wh_bond = BOND_WEAK, .scale_bond = BOND_WEAK));
    vec_push(&G.I.MOVEABLE, &t->n);
    return t;
}

void DynaText_pop_out(DynaText *t, double timer) {
    t->config.pop_out = timer != 0 ? timer : 1;
    t->config.has_pop_out = true;
    t->pop_out_time = G.TIMERS.REAL + t->pop_delay;
}

void DynaText_pop_in(DynaText *t, double timer) {
    t->reset_pop_in = true;
    t->config.has_pop_out = false;
    t->config.pop_in = timer;
    t->config.has_pop_in = true;
    t->created_time = G.TIMERS.REAL;
    if (t->focused_string - 1 < t->strings.count) {
        DynaString *s = &t->strings.items[t->focused_string - 1];
        for (int i = 0; i < s->letters.count; i++) s->letters.items[i].pop_in = 0;
    }
    DynaText_update_text(t, false);
}

void DynaText_align_letters(DynaText *t) {
    DynaTextConfig *c = &t->config;
    FontDef *f = dt_font(t);
    int nstr = t->strings.count;
    if (nstr == 0) return;
    if (t->pop_cycle) {
        t->focused_string = c->random_element ? math_random_range(1, nstr) : (t->focused_string == nstr ? 1 : t->focused_string + 1);
        t->pop_cycle = false;
        DynaString *s = &t->strings.items[t->focused_string - 1];
        for (int i = 0; i < s->letters.count; i++) s->letters.items[i].pop_in = 0;
        c->pop_in = 0.1; c->has_pop_in = true;
        c->has_pop_out = false;
        t->created_time = G.TIMERS.REAL;
    }
    DynaString *s = &t->strings.items[t->focused_string - 1];
    t->string = s->string;
    int nletters = s->letters.count;
    int slen = (int)strlen(s->string ? s->string : "");
    double rm = G.SETTINGS.reduced_motion ? 0 : 1;
    for (int k1 = 0; k1 < nletters; k1++) {
        int k = k1 + 1;
        DynaLetter *L = &s->letters.items[k1];
        if (c->has_pop_out) {
            double mct = c->has_min_cycle_time ? c->min_cycle_time : 1;
            L->pop_in = MIN(1, MAX(mct - (G.TIMERS.REAL - t->pop_out_time) * c->pop_out / mct, 0));
            L->pop_in = L->pop_in * L->pop_in;
            if (k == nletters && L->pop_in <= 0 && nstr > 1) t->pop_cycle = true;
        } else if (c->has_pop_in) {
            double prev = L->pop_in;
            double floor_v = (c->has_min_cycle_time && c->min_cycle_time == 0) ? 1 : 0;
            L->pop_in = MIN(1, MAX((G.TIMERS.REAL - c->pop_in - t->created_time) * slen * t->pop_in_rate - k + 1, floor_v));
            L->pop_in = L->pop_in * L->pop_in;
            if (prev <= 0 && L->pop_in > 0 && !t->silent && (slen < 10 || k % 2 == 0)) {
                if (!(t->n.T->x > G.ROOM->T->w + 2 || t->n.T->y > G.ROOM->T->h + 2 || t->n.T->x < -2 || t->n.T->y < -2))
                    sound_play("paper1", 0.45 + 0.05 * math_random() + (0.3 / slen) * k + c->pitch_shift, NAN);
            }
            if (k == nletters && L->pop_in >= 1) {
                if (nstr > 1) {
                    t->pop_delay = G.TIMERS.REAL - c->pop_in - t->created_time + (c->has_pop_delay ? c->pop_delay : 1.5);
                    DynaText_pop_out(t, 4);
                } else c->has_pop_in = false;
            }
        }
        L->r = 0;
        L->scale = 1;
        if (c->rotate) L->r = (c->rotate_dir == 2 ? -1 : 1) * (0.2 * (-nletters / 2.0 - 0.5 + k) / nletters + rm * 0.02 * sin(2 * G.TIMERS.REAL + k));
        if (c->pulse.active) {
            L->scale = L->scale + rm * (1 / c->pulse.width) * c->pulse.amount *
                (MAX(MIN((c->pulse.start - G.TIMERS.REAL) * c->pulse.speed + k + c->pulse.width,
                         (G.TIMERS.REAL - c->pulse.start) * c->pulse.speed - k + c->pulse.width + 2), 0));
            L->r = L->r + rm * (L->scale - 1) * (0.02 * (-nletters / 2.0 - 0.5 + k));
            if (c->pulse.start > G.TIMERS.REAL + 2 * c->pulse.speed * nletters) c->pulse.active = false;
        }
        if (c->quiver.active) {
            double sp = c->quiver.speed, am = c->quiver.amount, tt = G.TIMERS.REAL;
            L->scale = L->scale + rm * (0.1 * am);
            L->r = L->r + rm * 0.3 * am * (sin(41.12342 * tt * sp + k * 1223.2) + cos(63.21231 * tt * sp + k * 1112.2) * sin(36.1231 * tt * sp) +
                                         cos(95.123 * tt * sp + k * 1233.2) - sin(30.133421 * tt * sp + k * 123.2));
        }
        if (c->float_) L->offset.y = rm * sqrt(t->scale) * (2 + (f->FONTSCALE / G.TILESIZE) * 2000 * sin(2.666 * G.TIMERS.REAL + 200 * k)) + 60 * (L->scale - 1);
        if (c->bump) L->offset.y = rm * t->bump_amount * sqrt(t->scale) * 7 * MAX(0, (5 + t->bump_rate) * sin(t->bump_rate * G.TIMERS.REAL + 200 * k) - 3 - t->bump_rate);
    }
}

void DynaText_set_quiver(DynaText *t, double amt) {
    t->config.quiver.speed = 0.5;
    t->config.quiver.amount = isnan(amt) ? 0.7 : amt;
    t->config.quiver.active = true;
}
void DynaText_pulse(DynaText *t, double amt) {
    t->config.pulse.speed = 40;
    t->config.pulse.width = 2.5;
    t->config.pulse.start = G.TIMERS.REAL;
    t->config.pulse.amount = isnan(amt) ? 0.2 : amt;
    t->config.pulse.active = true;
}

static void DynaText_draw(Node *n) {
    DynaText *t = (DynaText *)n;
    DynaTextConfig *c = &t->config;
    FontDef *f = dt_font(t);
    Node *pe = child_get(n, "particle_effect");
    if (pe) node_draw(pe);
    if (t->strings.count == 0) return;
    DynaString *s = &t->strings.items[t->focused_string - 1];
    double fs = f->FONTSCALE / G.TILESIZE;
    bool always_full = c->has_min_cycle_time && c->min_cycle_time == 0;
    if (t->shadow) {
        prep_draw(n, 1, 0, NULL, false);
        gfx_translate(s->W_offset + t->text_offset.x * fs, s->H_offset + t->text_offset.y * fs);
        if (c->spacing != 0) gfx_translate(c->spacing * fs, 0);
        if (c->shadow_colour) gfx_set_color(c->shadow_colour);
        else gfx_set_color4(0, 0, 0, 0.3f * t->colours[0][3]);
        for (int i = 0; i < s->letters.count; i++) {
            DynaLetter *L = &s->letters.items[i];
            double rp = always_full ? 1 : L->pop_in;
            gfx_print(f->FONT, L->letter,
                      0.5 * (L->dims.x - L->offset.x) * fs - n->shadow_parrallax->x * t->scale / G.TILESIZE,
                      0.5 * L->dims.y * fs - n->shadow_parrallax->y * t->scale / G.TILESIZE,
                      L->r, rp * t->scale * fs, rp * t->scale * fs,
                      0.5 * L->dims.x / t->scale, 0.5 * L->dims.y / t->scale);
            gfx_translate(L->dims.x * fs, 0);
        }
        gfx_pop();
    }
    prep_draw(n, 1, 0, NULL, false);
    gfx_translate(s->W_offset + t->text_offset.x * fs, s->H_offset + t->text_offset.y * fs);
    if (c->spacing != 0) gfx_translate(c->spacing * fs, 0);
    double sp = sqrt(n->shadow_parrallax->y * n->shadow_parrallax->y + n->shadow_parrallax->x * n->shadow_parrallax->x);
    double snx = sp > 0 ? n->shadow_parrallax->x / sp * fs : 0;
    double sny = sp > 0 ? n->shadow_parrallax->y / sp * fs : 0;
    for (int i = 0; i < s->letters.count; i++) {
        int k = i + 1;
        DynaLetter *L = &s->letters.items[i];
        double rp = always_full ? 1 : L->pop_in;
        Col col = L->prefix ? L->prefix : L->suffix ? L->suffix : L->colour ? L->colour : t->colours[k % t->ncolours];
        gfx_set_color(col);
        gfx_print(f->FONT, L->letter,
                  0.5 * (L->dims.x - L->offset.x) * fs + snx,
                  0.5 * (L->dims.y - L->offset.y) * fs + sny,
                  L->r, rp * L->scale * t->scale * fs, rp * L->scale * t->scale * fs,
                  0.5 * L->dims.x / t->scale, 0.5 * L->dims.y / t->scale);
        gfx_translate(L->dims.x * fs, 0);
    }
    gfx_pop();
    add_to_drawhash(n);
    Node_draw_boundingrect(n);
}

static void DynaText_move(Node *n, double dt) { Moveable_move(n, dt); }
static void DynaText_noop(Node *n) { (void)n; }
static void DynaText_noop_rel(Node *n, Node *d) { (void)n; (void)d; }

const NodeVT DYNATEXT_VT = {
    .type = NT_DYNATEXT, .super = &MOVEABLE_VT, .name = "DynaText",
    .draw = DynaText_draw,
    .update = DynaText_update,
    .move = DynaText_move,
    .remove = Moveable_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = DynaText_noop,
    .release = DynaText_noop_rel,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = DynaText_noop,
    .juice_up = Moveable_juice_up,
    .hard_set_T = Moveable_hard_set_T,
};
