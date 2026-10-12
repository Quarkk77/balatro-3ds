/* misc.c - engine level helpers from functions/misc_functions.lua */
#include "../game/game.h"

void prep_draw(Node *m, double scale, double rotate, Vec2 *offset, bool has_rotate) {
    gfx_push();
    gfx_scale(G.TILESCALE * G.TILESIZE, G.TILESCALE * G.TILESIZE);
    gfx_translate(m->VT.x + m->VT.w / 2 + (offset ? offset->x : 0) + m->layered_parallax.x,
                  m->VT.y + m->VT.h / 2 + (offset ? offset->y : 0) + m->layered_parallax.y);
    if (m->VT.r != 0 || m->juice.active || has_rotate) gfx_rotate(m->VT.r + (has_rotate ? rotate : 0));
    gfx_translate(-scale * m->VT.w * m->VT.scale / 2, -scale * m->VT.h * m->VT.scale / 2);
    gfx_scale(m->VT.scale * scale, m->VT.scale * scale);
}

void add_to_drawhash(Node *n) { if (n) vec_push(&G.DRAW_HASH, n); }
void reset_drawhash(void) { vec_clear(&G.DRAW_HASH); }

void point_translate(Vec2 *p, Vec2 d) { p->x += d.x; p->y += d.y; }
void point_rotate(Vec2 *p, double angle) {
    double c = cos(angle + M_PI / 2), s = sin(angle + M_PI / 2), ox = p->x, oy = p->y;
    p->x = -oy * c + ox * s;
    p->y = oy * s + ox * c;
}
double Vector_Dist(Vec2 a, Vec2 b) {
    double x = a.x - b.x, y = a.y - b.y;
    return sqrt(x * x + y * y);
}

void remove_all_children(Node *n) {
    while (n->children.count > 0) {
        Node *v = n->children.items[n->children.count - 1].node;
        n->children.count--;
        if (v) {
            remove_all_children(v);
            node_remove(v);
        }
    }
}
void remove_all_nodes(PtrVec *t) {
    for (int i = t->count - 1; i >= 0; i--) {
        if (i >= t->count) continue;
        Node *v = t->items[i];
        vec_remove_at(t, i);
        if (v) {
            remove_all_children(v);
            node_remove(v);
        }
    }
}

Col col_new(float r, float g, float b, float a) {
    float *c = xmalloc(4 * sizeof(float));
    c[0] = r; c[1] = g; c[2] = b; c[3] = a;
    return c;
}
Col col_copy(const float *s) { return col_new(s[0], s[1], s[2], s[3]); }
void col_set(float *d, const float *s) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
Col HEX(const char *hex) {
    char h[9] = "000000FF";
    size_t n = strlen(hex);
    memcpy(h, hex, n > 8 ? 8 : n);
    if (n <= 6) { h[6] = 'F'; h[7] = 'F'; }
    return col_new((hexv(h[0]) * 16 + hexv(h[1])) / 255.0f, (hexv(h[2]) * 16 + hexv(h[3])) / 255.0f,
                   (hexv(h[4]) * 16 + hexv(h[5])) / 255.0f, (hexv(h[6]) * 16 + hexv(h[7])) / 255.0f);
}
Col mix_colours(const float *a, const float *b, double p) {
    return col_new((float)(a[0] * p + b[0] * (1 - p)), (float)(a[1] * p + b[1] * (1 - p)),
                   (float)(a[2] * p + b[2] * (1 - p)), (float)(a[3] * p + b[3] * (1 - p)));
}
Col lighten(const float *c, double p) {
    return col_new((float)(c[0] * (1 - p) + p), (float)(c[1] * (1 - p) + p), (float)(c[2] * (1 - p) + p), c[3]);
}
Col darken(const float *c, double p) {
    return col_new((float)(c[0] * (1 - p)), (float)(c[1] * (1 - p)), (float)(c[2] * (1 - p)), c[3]);
}
Col adjust_alpha(const float *c, double a) { return col_new(c[0], c[1], c[2], (float)a); }

void get_chosen_triangle_from_rect(double x, double y, double w, double h, bool vert, float out[6]) {
    double scale = 2;
    if (vert) {
        x = x + MIN(0.6 * sin(G.TIMERS.REAL * 9) * scale + 0.2, 0);
        out[0] = (float)(x - 3.5 * scale); out[1] = (float)(y + h / 2 - 1.5 * scale);
        out[2] = (float)(x - 0.5 * scale); out[3] = (float)(y + h / 2 + 0);
        out[4] = (float)(x - 3.5 * scale); out[5] = (float)(y + h / 2 + 1.5 * scale);
    } else {
        y = y + MIN(0.6 * sin(G.TIMERS.REAL * 9) * scale + 0.2, 0);
        out[0] = (float)(x + w / 2 - 1.5 * scale); out[1] = (float)(y - 4 * scale);
        out[2] = (float)(x + w / 2 + 0); out[3] = (float)(y - 1.1 * scale);
        out[4] = (float)(x + w / 2 + 1.5 * scale); out[5] = (float)(y - 4 * scale);
    }
}
