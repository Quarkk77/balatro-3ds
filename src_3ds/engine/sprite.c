/* sprite.c - port of engine/sprite.lua and engine/animatedsprite.lua */
#include "../game/game.h"

static void sprite_update_quad(Sprite *s) {
    s->sprite = gfx_quad((float)(s->sprite_pos.x * s->atlas->px), (float)(s->sprite_pos.y * s->atlas->py),
                         (float)s->scale.x, (float)s->scale.y, s->atlas->image->w, s->atlas->image->h);
    s->image_dims[0] = s->atlas->image->w;
    s->image_dims[1] = s->atlas->image->h;
}

void Sprite_set_sprite_pos(Sprite *s, SpritePos pos) {
    if (pos.v > 0) {
        s->sprite_pos.x = math_random_range(1, pos.v) - 1;
        s->sprite_pos.y = pos.y;
        s->sprite_pos.v = 0;
    } else s->sprite_pos = pos;
    s->sprite_pos_copy = s->sprite_pos;
    if (s->atlas && s->atlas->image) sprite_update_quad(s);
}

void AnimatedSprite_set_sprite_pos(Sprite *s, SpritePos pos) {
    s->animation.x = pos.x;
    s->animation.y = pos.y;
    s->animation.frames = s->atlas->frames;
    s->animation.current = 0;
    s->animation.w = s->scale.x;
    s->animation.h = s->scale.y;
    s->frame_offset = 0;
    s->current_animation.current = 0;
    s->current_animation.frames = s->animation.frames;
    s->current_animation.w = s->animation.w;
    s->current_animation.h = s->animation.h;
    s->image_dims[0] = s->atlas->image->w;
    s->image_dims[1] = s->atlas->image->h;
    s->sprite = gfx_quad(0, (float)(s->animation.h * s->animation.y), (float)s->animation.w, (float)s->animation.h,
                         (float)s->image_dims[0], (float)s->image_dims[1]);
    s->offset_seconds = G.TIMERS.REAL;
}

static void set_pos_dispatch(Sprite *s, SpritePos pos) {
    if (s->n.vt == &ANIMATEDSPRITE_VT) AnimatedSprite_set_sprite_pos(s, pos);
    else Sprite_set_sprite_pos(s, pos);
}

void sprite_init(Sprite *s, const NodeVT *vt, double x, double y, double w, double h, Atlas *atlas, SpritePos pos) {
    moveable_init(&s->n, vt, x, y, w, h);
    s->n.CT = &s->n.VT;
    s->atlas = atlas;
    s->scale.x = atlas ? atlas->px : 1;
    s->scale.y = atlas ? atlas->py : 1;
    s->scale_mag = MIN(s->scale.x / w, s->scale.y / h);
    s->n.zoom = true;
    if (atlas) set_pos_dispatch(s, pos);
    if (vt == &SPRITE_VT) vec_push(&G.I.SPRITE, &s->n);
}

Sprite *sprite_new(double x, double y, double w, double h, Atlas *atlas, SpritePos pos) {
    Sprite *s = NEW(Sprite);
    sprite_init(s, &SPRITE_VT, x, y, w, h, atlas, pos);
    return s;
}

Sprite *animated_sprite_new(double x, double y, double w, double h, Atlas *atlas, SpritePos pos) {
    Sprite *s = NEW(Sprite);
    sprite_init(s, &ANIMATEDSPRITE_VT, x, y, w, h, atlas, pos);
    s->n.offset = (Vec2){ 0, 0 };
    vec_push(&G.ANIMATIONS, &s->n);
    vec_push(&G.I.SPRITE, &s->n);
    return s;
}

void Sprite_reset(Sprite *s) {
    if (s->n.vt == &ANIMATEDSPRITE_VT) {
        Atlas *a = strmap_get(&G.ANIMATION_ATLAS, s->atlas->name);
        if (a) s->atlas = a;
        AnimatedSprite_set_sprite_pos(s, SPOS(s->animation.x, s->animation.y));
    } else {
        Atlas *a = strmap_get(&G.ASSET_ATLAS, s->atlas->name);
        if (a) s->atlas = a;
        Sprite_set_sprite_pos(s, s->sprite_pos);
    }
}

void Sprite_get_pos_pixel(Sprite *s, float out[4]) {
    if (s->n.vt == &ANIMATEDSPRITE_VT) {
        out[0] = (float)s->current_animation.current;
        out[1] = (float)s->animation.y;
        out[2] = (float)s->animation.w;
        out[3] = (float)s->animation.h;
    } else {
        out[0] = (float)s->sprite_pos.x;
        out[1] = (float)s->sprite_pos.y;
        out[2] = (float)s->atlas->px;
        out[3] = (float)s->atlas->py;
    }
}

void Sprite_define_draw_steps(Sprite *s, const DrawStep *steps, int n) {
    free(s->draw_steps);
    s->draw_steps = xcalloc((size_t)(n > 0 ? n : 1), sizeof(DrawStep));
    s->n_draw_steps = n;
    for (int i = 0; i < n; i++) {
        s->draw_steps[i] = steps[i];
        if (!s->draw_steps[i].shader) s->draw_steps[i].shader = "dissolve";
        if (steps[i].nsend > 0) {
            s->draw_steps[i].send = xmalloc((size_t)steps[i].nsend * sizeof(ShaderSend));
            memcpy(s->draw_steps[i].send, steps[i].send, (size_t)steps[i].nsend * sizeof(ShaderSend));
        }
    }
}

static void send_values(GfxShader *sh, const ShaderSend *sends, int n) {
    for (int i = 0; i < n; i++) {
        const ShaderSend *v = &sends[i];
        if (v->func) { gfx_send_float(sh, v->name, (float)v->func()); continue; }
        if (ref_valid(v->ref)) {
            if (v->ref.kind == REF_COL || v->ref.kind == REF_COLV) gfx_send_vec4(sh, v->name, ref_col(v->ref));
            else gfx_send_float(sh, v->name, (float)ref_num(v->ref));
            continue;
        }
        switch (v->kind) {
        case 1: gfx_send_vec2(sh, v->name, v->val[0], v->val[1]); break;
        case 2: gfx_send_vec4(sh, v->name, v->val); break;
        case 3: gfx_send_bool(sh, v->name, v->val[0] != 0); break;
        case 4: { const double *d = v->ptr; gfx_send_vec2(sh, v->name, (float)d[0], (float)d[1]); break; }
        case 5: { float px[4]; Sprite_get_pos_pixel((Sprite *)v->ptr, px); gfx_send_vec4(sh, v->name, px); break; }
        default: gfx_send_float(sh, v->name, v->val[0]); break;
        }
    }
}

void Sprite_draw_shader(Sprite *s, const char *shader, double shadow_height, bool has_shadow, const float *send, int nsend,
                        bool no_tilt, Node *other_obj, double ms, double mr, double mx, double my, bool custom_shader,
                        double tilt_shadow, const ShaderSend *sends, int nsends) {
    Node *n = &s->n;
    if (G.SETTINGS.reduced_motion) no_tilt = true;
    Node *dm = n->role.draw_major ? n->role.draw_major : n;
    if (!shader) shader = "dissolve";
    GfxShader *sh = strmap_get(&G.SHADERS, shader);
    if (has_shadow) {
        n->VT.y -= dm->shadow_parrallax->y * shadow_height;
        n->VT.x -= dm->shadow_parrallax->x * shadow_height;
        n->VT.scale *= (1 - 0.2 * shadow_height);
    }
    if (custom_shader) {
        if (sends) send_values(sh, sends, nsends);
    } else if (!strcmp(shader, "vortex")) {
        gfx_send_float(sh, "vortex_amt", (float)(G.TIMERS.REAL - G.vortex_time));
    } else {
        float cx = (float)(dm->tilt_var.set ? dm->tilt_var.mx * G.CANV_SCALE : G.CONTROLLER.cursor_position.x * G.CANV_SCALE);
        float cy = (float)(dm->tilt_var.set ? dm->tilt_var.my * G.CANV_SCALE : G.CONTROLLER.cursor_position.y * G.CANV_SCALE);
        gfx_send_vec2(sh, "mouse_screen_pos", cx, cy);
        gfx_send_float(sh, "screen_scale", (float)(G.TILESCALE * G.TILESIZE * (dm->mouse_damping != 0 ? dm->mouse_damping : 1) * G.CANV_SCALE));
        double ts = isnan(tilt_shadow) ? 1 : tilt_shadow;
        bool tilt_shadow_set = !isnan(tilt_shadow);
        gfx_send_float(sh, "hovering", (float)(((has_shadow && !tilt_shadow_set) || no_tilt) ? 0 : dm->hover_tilt * ts));
        gfx_send_float(sh, "dissolve", (float)fabs(dm->dissolve));
        gfx_send_float(sh, "time", (float)lua_mod(123.33412 * (dm->ID / 1.14212), 3000));
        float pp[4];
        Sprite_get_pos_pixel(s, pp);
        gfx_send_vec4(sh, "texture_details", pp);
        gfx_send_vec2(sh, "image_details", (float)s->image_dims[0], (float)s->image_dims[1]);
        gfx_send_vec4(sh, "burn_colour_1", dm->n_dissolve_colours > 0 ? dm->dissolve_colours[0] : G.C.CLEAR);
        gfx_send_vec4(sh, "burn_colour_2", dm->n_dissolve_colours > 1 ? dm->dissolve_colours[1] : G.C.CLEAR);
        gfx_send_bool(sh, "shadow", has_shadow);
        if (send && nsend > 0) {
            if (nsend == 2) gfx_send_vec2(sh, shader, send[0], send[1]);
            else if (nsend == 4) gfx_send_vec4(sh, shader, send);
            else gfx_send_float(sh, shader, send[0]);
        }
    }
    gfx_set_shader(sh);
    if (other_obj) Sprite_draw_from(s, other_obj, ms, mr, mx, my);
    else Sprite_draw_self(s, NULL);
    gfx_set_shader(NULL);
    if (has_shadow) {
        n->VT.y += dm->shadow_parrallax->y * shadow_height;
        n->VT.x += dm->shadow_parrallax->x * shadow_height;
        n->VT.scale /= (1 - 0.2 * shadow_height);
    }
}

void Sprite_draw_shader_simple(Sprite *s, const char *shader, double shadow_height, bool has_shadow) {
    Sprite_draw_shader(s, shader, shadow_height, has_shadow, NULL, 0, false, NULL, 0, 0, 0, 0, false, NAN, NULL, 0);
}
void Sprite_draw_shader_send(Sprite *s, const char *shader, const float *send, int nsend) {
    Sprite_draw_shader(s, shader, 0, false, send, nsend, false, NULL, 0, 0, 0, 0, false, NAN, NULL, 0);
}
void Sprite_draw_shader_ex(Sprite *s, const char *shader, double shadow_height, bool has_shadow, const float *send, int nsend,
                           bool no_tilt, Node *other_obj, double ms, double mr, double mx, double my) {
    Sprite_draw_shader(s, shader, shadow_height, has_shadow, send, nsend, no_tilt, other_obj, ms, mr, mx, my, false, NAN, NULL, 0);
}

void Sprite_draw_self(Sprite *s, const float *overlay) {
    Node *n = &s->n;
    if (!n->states.visible) return;
    if (n->vt == &ANIMATEDSPRITE_VT) {
        prep_draw(n, 1, 0, NULL, false);
        gfx_scale(1 / s->scale_mag, 1 / s->scale_mag);
        gfx_set_color(G.C.WHITE);
        gfx_draw_image(s->atlas->image, &s->sprite, 0, 0, 0, n->VT.w / n->T->w, n->VT.h / n->T->h, 0, 0);
        gfx_pop();
        return;
    }
    if (s->sprite_pos.x != s->sprite_pos_copy.x || s->sprite_pos.y != s->sprite_pos_copy.y) Sprite_set_sprite_pos(s, s->sprite_pos);
    prep_draw(n, 1, 0, NULL, false);
    gfx_scale(1 / (s->scale.x / n->VT.w), 1 / (s->scale.y / n->VT.h));
    gfx_set_color(overlay ? overlay : (G.BRUTE_OVERLAY ? G.BRUTE_OVERLAY : G.C.WHITE));
    gfx_draw_image(s->atlas->image, &s->sprite, 0, 0, 0, n->VT.w / n->T->w, n->VT.h / n->T->h, 0, 0);
    gfx_pop();
    add_to_drawhash(n);
    Node_draw_boundingrect(n);
}

void Sprite_draw(Node *n) {
    Sprite *s = (Sprite *)n;
    if (!n->states.visible) return;
    if (s->custom_draw) { s->custom_draw(s); return; }
    if (s->draw_steps) {
        for (int i = 0; i < s->n_draw_steps; i++) {
            DrawStep *v = &s->draw_steps[i];
            Sprite_draw_shader(s, v->shader, v->shadow_height, v->has_shadow_height, NULL, 0, v->no_tilt, v->other_obj,
                               v->ms, v->mr, v->mx, v->my, v->nsend > 0, NAN, v->send, v->nsend);
        }
    } else Sprite_draw_self(s, NULL);
    add_to_drawhash(n);
    for (int i = 0; i < n->children.count; i++) {
        const char *k = n->children.items[i].name;
        if (!k || strcmp(k, "h_popup")) node_draw(n->children.items[i].node);
    }
    add_to_drawhash(n);
    Node_draw_boundingrect(n);
}

void Sprite_draw_from(Sprite *s, Node *o, double ms, double mr, double mx, double my) {
    Vec2 off = { mx, my };
    prep_draw(o, 1 + ms, mr, &off, true);
    double sm = 0;
    if (IS(o, NT_SPRITE)) sm = ((Sprite *)o)->scale_mag;
    double div = sm != 0 ? sm : o->VT.scale;
    gfx_scale(1 / div, 1 / div);
    gfx_set_color(G.BRUTE_OVERLAY ? G.BRUTE_OVERLAY : G.C.WHITE);
    gfx_draw_image(s->atlas->image, &s->sprite, -(o->T->w / 2 - o->VT.w / 2) * 10, 0, 0, o->VT.w / o->T->w, o->VT.h / o->T->h, 0, 0);
    Node_draw_boundingrect(&s->n);
    gfx_pop();
}

static void Sprite_remove(Node *n) {
    ptrvec_remove_all(&G.ANIMATIONS, n);
    ptrvec_remove_all(&G.I.SPRITE, n);
    Moveable_remove(n);
}

void AnimatedSprite_rescale(Sprite *s) { s->scale_mag = MIN(s->scale.x / s->n.T->w, s->scale.y / s->n.T->h); }

static void AnimatedSprite_animate(Node *n) {
    Sprite *s = (Sprite *)n;
    int frames = s->current_animation.frames > 0 ? s->current_animation.frames : 1;
    int nf = (int)lua_mod(floor(G.ANIMATION_FPS * (G.TIMERS.REAL - s->offset_seconds)), frames);
    if (nf != s->current_animation.current) {
        s->current_animation.current = nf;
        s->frame_offset = floor(s->animation.w * s->current_animation.current);
        s->sprite.x = (float)s->frame_offset;
        s->sprite.y = (float)(s->animation.h * s->animation.y);
        s->sprite.w = (float)s->animation.w;
        s->sprite.h = (float)s->animation.h;
    }
    if (s->float_anim) {
        n->T->r = 0.02 * sin(2 * G.TIMERS.REAL + n->T->x);
        n->offset.y = -(1 + 0.3 * sin(0.666 * G.TIMERS.REAL + n->T->y)) * n->shadow_parrallax->y;
        n->offset.x = -(0.7 + 0.2 * sin(0.666 * G.TIMERS.REAL + n->T->x)) * n->shadow_parrallax->x;
    }
}

static void Sprite_draw_self_v(Node *n) { Sprite_draw_self((Sprite *)n, NULL); }
static void Sprite_move_v(Node *n, double dt) { Moveable_move(n, dt); }
static void Node_noop_update(Node *n, double dt) { (void)n; (void)dt; }
static void Node_noop(Node *n) { (void)n; }
static void Node_noop_release(Node *n, Node *d) { (void)n; (void)d; }
static void Sprite_juice_v(Node *n, double a, double r) { Moveable_juice_up(n, a, r); }

const NodeVT SPRITE_VT = {
    .type = NT_SPRITE, .super = &MOVEABLE_VT, .name = "Sprite",
    .draw = Sprite_draw,
    .update = Node_noop_update,
    .move = Sprite_move_v,
    .remove = Sprite_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = Node_noop,
    .release = Node_noop_release,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = Node_noop,
    .juice_up = Sprite_juice_v,
    .draw_self = Sprite_draw_self_v,
    .hard_set_T = Moveable_hard_set_T,
};

const NodeVT ANIMATEDSPRITE_VT = {
    .type = NT_ANIMATEDSPRITE, .super = &SPRITE_VT, .name = "AnimatedSprite",
    .draw = Sprite_draw,
    .update = Node_noop_update,
    .move = Sprite_move_v,
    .remove = Sprite_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = Node_noop,
    .release = Node_noop_release,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = AnimatedSprite_animate,
    .juice_up = Sprite_juice_v,
    .draw_self = Sprite_draw_self_v,
    .hard_set_T = Moveable_hard_set_T,
};
