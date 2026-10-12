/* particles.c - port of engine/particles.lua */
#include "../game/game.h"

static void Particles_update(Node *n, double dt);
static void Particles_move(Node *n, double dt);

Particles *particles_new(double x, double y, double w, double h, ParticlesConfig cfg) {
    Particles *p = NEW(Particles);
    moveable_init(&p->n, &PARTICLES_VT, x, y, w, h);
    p->fill = cfg.fill;
    p->padding = cfg.padding;
    if (cfg.attach) {
        Moveable_set_alignment(&p->n, ALIGN_ARGS(.major = cfg.attach, .type = "cm", .bond = BOND_STRONG));
        child_push(p->n.role.major, &p->n);
        p->n.parent = p->n.role.major;
        p->n.T->x = p->n.role.major->T->x + p->padding;
        p->n.T->y = p->n.role.major->T->y + p->padding;
        if (p->fill) {
            p->n.T->w = p->n.role.major->T->w - p->padding;
            p->n.T->h = p->n.role.major->T->h - p->padding;
        }
    }
    p->n.states.hover.can = false;
    p->n.states.click.can = false;
    p->n.states.collide.can = false;
    p->n.states.drag.can = false;
    p->n.states.release_on.can = false;
    p->timer = cfg.timer != 0 ? cfg.timer : 0.5;
    p->timer_type = p->n.created_on_pause ? TIMER_REAL : (cfg.has_timer_type ? cfg.timer_type : TIMER_REAL);
    p->last_real_time = G.TIMERS.arr[p->timer_type] - p->timer;
    p->last_drawn = 0;
    p->lifespan = cfg.lifespan != 0 ? cfg.lifespan : 1;
    p->fade_alpha = 0;
    p->speed = (cfg.speed != 0 || cfg.has_speed) ? cfg.speed : 1;
    p->max = (cfg.max != 0 || cfg.has_max) ? cfg.max : 1e15;
    p->pulse_max = MIN(20, cfg.pulse_max);
    p->pulsed = 0;
    p->vel_variation = cfg.has_vel_variation ? cfg.vel_variation : 1;
    p->scale = cfg.scale != 0 ? cfg.scale : 1;
    if (cfg.colours && cfg.ncolours > 0) { p->colours = cfg.colours; p->ncolours = cfg.ncolours; }
    else { p->colours = dt_cols(1, G.C.BACKGROUND.D); p->ncolours = 1; }
    if (cfg.initialize) {
        for (int i = 0; i < 60; i++) {
            p->last_real_time -= 15.0 / 60;
            Particles_update(&p->n, 15.0 / 60);
            Particles_move(&p->n, 15.0 / 60);
        }
    }
    vec_push(&G.I.MOVEABLE, &p->n);
    return p;
}

static Col pick_colour(Particles *p) {
    /* pseudorandom_element(self.colours) without a seed: sorted by index, math.random(#) */
    int i = math_random_range(1, p->ncolours);
    return p->colours[i - 1];
}

static void Particles_update(Node *n, double dt) {
    (void)dt;
    Particles *p = (Particles *)n;
    if (G.SETTINGS.paused && !n->created_on_pause) { p->last_real_time = G.TIMERS.arr[p->timer_type]; return; }
    int added = 0;
    while (G.TIMERS.arr[p->timer_type] > p->last_real_time + p->timer && (p->particles.count < p->max || p->pulsed < p->pulse_max) && added < 20) {
        p->last_real_time += p->timer;
        Vec2 off = { p->fill ? (0.5 - math_random()) * n->T->w : 0, p->fill ? (0.5 - math_random()) * n->T->h : 0 };
        if (p->fill && n->T->r < 0.1 && n->T->r > -0.1) {
            Vec2 nn = { sin(n->T->r) * off.y + cos(n->T->r) * off.x, sin(n->T->r) * off.x + cos(n->T->r) * off.y };
            off = nn;
        }
        Particle q;
        memset(&q, 0, sizeof q);
        q.draw = false;
        q.dir = math_random() * 2 * M_PI;
        q.facing = math_random() * 2 * M_PI;
        q.size = math_random() * 0.5 + 0.1;
        q.age = 0;
        q.velocity = p->speed * (p->vel_variation * math_random() + (1 - p->vel_variation)) * 0.7;
        q.r_vel = 0.2 * (0.5 - math_random());
        q.time = G.TIMERS.arr[p->timer_type];
        q.colour = pick_colour(p);
        q.offset = off;
        vec_push(&p->particles, q);
        added++;
        if (p->pulsed <= p->pulse_max) p->pulsed++;
    }
}

static void Particles_move(Node *n, double dt) {
    Particles *p = (Particles *)n;
    if (G.SETTINGS.paused && !n->created_on_pause) return;
    Moveable_move(n, dt);
    if (p->timer_type != TIMER_REAL) dt = dt * G.SPEEDFACTOR;
    for (int i = p->particles.count - 1; i >= 0; i--) {
        Particle *q = &p->particles.items[i];
        q->draw = true;
        if (!q->e_vel_set) { q->e_vel = dt * p->scale; q->e_vel_set = true; }
        q->e_prev = q->e_curr;
        q->age += dt;
        q->e_curr = MIN(2 * MIN((q->age / p->lifespan) * p->scale, p->scale * ((p->lifespan - q->age) / p->lifespan)), p->scale);
        q->e_vel = (q->e_curr - q->e_prev) * p->scale * dt + (1 - p->scale * dt) * q->e_vel;
        q->scale = q->scale + q->e_vel;
        q->scale = MIN(2 * MIN((q->age / p->lifespan) * p->scale, p->scale * ((p->lifespan - q->age) / p->lifespan)), p->scale);
        if (q->scale < 0) vec_remove_at(&p->particles, i);
        else {
            q->offset.x += q->velocity * sin(q->dir) * dt;
            q->offset.y += q->velocity * cos(q->dir) * dt;
            q->facing += q->r_vel * dt;
            q->velocity = MAX(0, q->velocity - q->velocity * 0.07 * dt);
        }
    }
}

void Particles_fade(Particles *p, double delay, double to) {
    add_event(EVENT(.trigger = TRIG_EASE, .timer = p->timer_type, .blockable = 0, .blocking = 0,
                    .ref = REFN(&p->fade_alpha), .ease_to = isnan(to) ? 1 : to, .delay = delay));
}

static void Particles_draw(Node *n) {
    Particles *p = (Particles *)n;
    double alpha = 1;
    prep_draw(n, 1, 0, NULL, false);
    gfx_translate(n->T->w / 2, n->T->h / 2);
    for (int i = 0; i < p->particles.count; i++) {
        Particle *q = &p->particles.items[i];
        if (!q->draw) continue;
        gfx_push();
        gfx_set_color4(q->colour[0], q->colour[1], q->colour[2], (float)(q->colour[3] * alpha * (1 - p->fade_alpha)));
        gfx_translate(q->offset.x, q->offset.y);
        gfx_rotate(q->facing);
        gfx_rectangle(GFX_FILL, -q->scale / 2, -q->scale / 2, q->scale, q->scale);
        gfx_pop();
    }
    gfx_pop();
    add_to_drawhash(n);
    Node_draw_boundingrect(n);
}

static void Particles_remove(Node *n) {
    /* only the array entry added by the constructor is removed here; a
     * named reference (children.particles) is cleared by its owner */
    if (n->role.major) {
        Node *m = n->role.major;
        for (int i = m->children.count - 1; i >= 0; i--)
            if (m->children.items[i].node == n && !m->children.items[i].name) vec_remove_at(&m->children, i);
    }
    remove_all_children(n);
    Moveable_remove(n);
}

static void P_noop(Node *n) { (void)n; }
static void P_noop_rel(Node *n, Node *d) { (void)n; (void)d; }

const NodeVT PARTICLES_VT = {
    .type = NT_PARTICLES, .super = &MOVEABLE_VT, .name = "Particles",
    .draw = Particles_draw,
    .update = Particles_update,
    .move = Particles_move,
    .remove = Particles_remove,
    .hover = Node_hover,
    .stop_hover = Node_stop_hover,
    .click = P_noop,
    .release = P_noop_rel,
    .drag = Moveable_drag,
    .stop_drag = Node_stop_drag,
    .can_drag = Node_can_drag,
    .collides_with_point = Node_collides_with_point,
    .put_focused_cursor = Node_put_focused_cursor,
    .set_container = Node_set_container,
    .animate = P_noop,
    .juice_up = Moveable_juice_up,
    .hard_set_T = Moveable_hard_set_T,
};
