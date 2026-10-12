/* gfx_3ds.c - LOVE-like graphics, window and input for the Nintendo 3DS on
 * top of libctru and citro3d. (Audio lives in audio_3ds.c.)
 *
 * Differences from the desktop raylib layer:
 *  - The game "window" is 400x240. Everything is drawn into an offscreen
 *    400x240 target which is shown 1:1 on the top screen and scaled down on
 *    the bottom screen, where touches act as the mouse.
 *  - The PICA200 has no programmable fragment stage, so LOVE's GLSL shaders
 *    are not compiled. Each shader name maps to an approximation built from
 *    the fixed function texture combiners (see shader_apply).
 *  - Textures must be power-of-two and at most 1024x1024, so images are cut
 *    into tiles; a quad spanning several tiles is drawn as several pieces.
 *    Images are decoded and uploaded on first draw to save memory. */
#include "gfx.h"
#include <3ds.h>
#include <citro3d.h>
#include "vshader_shbin.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define WIN_W 400
#define WIN_H 240
#define BOTTOM_W 320
#define BOTTOM_H 240
/* the 400x240 picture scaled to the 320 wide bottom screen */
#define BOT_VIEW_H 192
#define BOT_VIEW_Y ((BOTTOM_H - BOT_VIEW_H) / 2)

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

/* ------------------------------------------------------------------ */
/* Transform stack                                                     */
/* ------------------------------------------------------------------ */
typedef struct { double a, b, c, d, e, f; } Affine; /* x' = a*x + c*y + e ; y' = b*x + d*y + f */
#define TSTACK_MAX 256
static Affine tstack[TSTACK_MAX];
static int tsp;
static Affine cur = { 1, 0, 0, 1, 0, 0 };
static float cur_col[4] = { 1, 1, 1, 1 };
static float line_width = 1;
static GfxShader *cur_shader;
static GfxCanvas *cur_canvas;

static inline void xf(double x, double y, float *ox, float *oy) {
    *ox = (float)(cur.a * x + cur.c * y + cur.e);
    *oy = (float)(cur.b * x + cur.d * y + cur.f);
}

void gfx_origin(void) { cur = (Affine){ 1, 0, 0, 1, 0, 0 }; tsp = 0; }
void gfx_push(void) {
    if (tsp < TSTACK_MAX) tstack[tsp++] = cur;
    else fprintf(stderr, "gfx: transform stack overflow\n");
}
void gfx_pop(void) {
    if (tsp > 0) cur = tstack[--tsp];
    else fprintf(stderr, "gfx: transform stack underflow\n");
}
void gfx_translate(double x, double y) {
    cur.e += cur.a * x + cur.c * y;
    cur.f += cur.b * x + cur.d * y;
}
void gfx_rotate(double ang) {
    double cs = cos(ang), sn = sin(ang);
    Affine n;
    n.a = cur.a * cs + cur.c * sn;
    n.b = cur.b * cs + cur.d * sn;
    n.c = -cur.a * sn + cur.c * cs;
    n.d = -cur.b * sn + cur.d * cs;
    n.e = cur.e; n.f = cur.f;
    cur = n;
}
void gfx_scale(double sx, double sy) {
    cur.a *= sx; cur.b *= sx;
    cur.c *= sy; cur.d *= sy;
}
void gfx_set_color(const float *c) {
    if (!c) { cur_col[0] = cur_col[1] = cur_col[2] = cur_col[3] = 1; return; }
    for (int i = 0; i < 4; i++) cur_col[i] = c[i];
}
void gfx_set_color4(float r, float g, float b, float a) { cur_col[0] = r; cur_col[1] = g; cur_col[2] = b; cur_col[3] = a; }
void gfx_set_line_width(float w) { line_width = w; }

static inline u8 to_u8(float v) {
    if (!(v > 0)) return 0;
    if (v >= 1) return 255;
    return (u8)(v * 255.0f + 0.5f);
}
static inline u32 pack_rgba(const float *c) {
    /* GPU constant colour layout: 0xAABBGGRR */
    return (u32)to_u8(c[0]) | ((u32)to_u8(c[1]) << 8) | ((u32)to_u8(c[2]) << 16) | ((u32)to_u8(c[3]) << 24);
}

/* ------------------------------------------------------------------ */
/* GPU state and batching                                              */
/* ------------------------------------------------------------------ */
typedef struct { float x, y, u, v; u8 r, g, b, a; } Vertex;
#define VBUF_MAX 120000
static Vertex *vbuf;
static int vcount, batch_start;
static bool vbuf_overflow_warned;

static DVLB_s *vshader_dvlb;
static shaderProgram_s program;
static int u_projection;
static C3D_Mtx proj;

static C3D_RenderTarget *top_target, *bot_target;
static C3D_Tex white_tex;
static GfxCanvas *screen_canvas;   /* the 400x240 "window" as shown on the top screen */
static GfxCanvas *bottom_canvas;   /* the same frame for the bottom screen */
static bool in_frame;

/* A frame is recorded as a list of batches and replayed once per screen, so
 * draws can be limited to one screen (gfx_set_screen_mask) without breaking
 * the draw order. */
typedef struct { C3D_Tex *tex; int mode; u32 tint; bool replace; } BatchState;
typedef struct { BatchState st; int start, count; int mask; GfxCanvas *target; } DrawCmd;
#define MAX_CMDS 16384
static DrawCmd *cmds;
static int ncmds;
static bool recording;
static int screen_mask = GFX_SCREEN_BOTH;

/* combiner modes */
enum { MODE_MODULATE, MODE_SILHOUETTE, MODE_NEGATIVE, MODE_SOLID };
static BatchState bs_pending, bs_applied;
static bool bs_applied_valid;

static void apply_combiners(int mode, u32 tint) {
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    switch (mode) {
    case MODE_SILHOUETTE:
        C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
        break;
    case MODE_NEGATIVE:
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        break;
    case MODE_SOLID:
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        break;
    default:
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        break;
    }
    /* stage 1: rgb = mix(previous, tint.rgb, tint.a) */
    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    if (tint >> 24) {
        C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_CONSTANT);
        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
        C3D_TexEnvColor(env, tint);
    }
    for (int i = 2; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
}

static void apply_state(const BatchState *s) {
    if (!bs_applied_valid || s->tex != bs_applied.tex) C3D_TexBind(0, s->tex ? s->tex : &white_tex);
    if (!bs_applied_valid || s->mode != bs_applied.mode || s->tint != bs_applied.tint) apply_combiners(s->mode, s->tint);
    if (!bs_applied_valid || s->replace != bs_applied.replace) {
        if (s->replace) C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    }
    bs_applied = *s;
    bs_applied_valid = true;
}

void gfx_flush(void) {
    if (!in_frame) { batch_start = vcount; return; }
    int n = vcount - batch_start;
    if (n > 0) {
        if (recording) {
            if (ncmds < MAX_CMDS) cmds[ncmds++] = (DrawCmd){ bs_pending, batch_start, n, screen_mask, cur_canvas };
        } else {
            apply_state(&bs_pending);
            C3D_DrawArrays(GPU_TRIANGLES, batch_start, n);
        }
    }
    batch_start = vcount;
}

void gfx_set_screen_mask(int mask) {
    if (mask == screen_mask) return;
    gfx_flush();
    screen_mask = mask;
}
int gfx_get_screen_mask(void) { return screen_mask; }

static void set_state(C3D_Tex *tex, int mode, u32 tint, bool replace) {
    if (!tex) tex = &white_tex;
    if (tint >> 24 == 0) tint = 0;
    if (bs_pending.tex == tex && bs_pending.mode == mode && bs_pending.tint == tint && bs_pending.replace == replace) return;
    gfx_flush();
    bs_pending.tex = tex; bs_pending.mode = mode; bs_pending.tint = tint; bs_pending.replace = replace;
}

static inline Vertex *alloc_verts(int n) {
    if (!in_frame) return NULL;
    if (vcount + n > VBUF_MAX) {
        if (!vbuf_overflow_warned) { fprintf(stderr, "gfx: vertex buffer full, dropping draws this frame\n"); vbuf_overflow_warned = true; }
        return NULL;
    }
    Vertex *v = &vbuf[vcount];
    vcount += n;
    return v;
}

static inline void put_vert(Vertex *v, float x, float y, float u, float t, u32 rgba) {
    v->x = x; v->y = y; v->u = u; v->v = t;
    v->r = rgba & 0xFF; v->g = (rgba >> 8) & 0xFF; v->b = (rgba >> 16) & 0xFF; v->a = rgba >> 24;
}

/* ------------------------------------------------------------------ */
/* Shader emulation                                                    */
/* ------------------------------------------------------------------ */
enum {
    SH_DEFAULT, SH_DISSOLVE, SH_SKIP, SH_NEGATIVE, SH_FOIL, SH_HOLO, SH_POLY, SH_DEBUFF, SH_PLAYED,
    SH_HOLOGRAM, SH_FLASH, SH_SPLASH, SH_BACKGROUND,
};
typedef struct { char name[32]; float v[4]; } Uniform;
struct GfxShader {
    int kind;
    char *name;
    Uniform *u;
    int nu, cap;
};

static int shader_kind(const char *name) {
    static const struct { const char *n; int k; } T[] = {
        { "dissolve", SH_DISSOLVE }, { "vortex", SH_DISSOLVE },
        { "voucher", SH_SKIP }, { "booster", SH_SKIP }, { "negative_shine", SH_SKIP }, { "gold_seal", SH_SKIP }, { "flame", SH_SKIP },
        { "negative", SH_NEGATIVE }, { "foil", SH_FOIL }, { "holo", SH_HOLO }, { "polychrome", SH_POLY },
        { "debuff", SH_DEBUFF }, { "played", SH_PLAYED }, { "hologram", SH_HOLOGRAM }, { "flash", SH_FLASH },
        { "splash", SH_SPLASH }, { "background", SH_BACKGROUND },
    };
    for (int i = 0; i < (int)ARRLEN(T); i++) if (!strcmp(T[i].n, name)) return T[i].k;
    return SH_DEFAULT;   /* CRT, skew: drawn as plain textures */
}

GfxShader *gfx_load_shader(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char name[64];
    snprintf(name, sizeof name, "%s", base);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    GfxShader *s = NEW(GfxShader);
    s->kind = shader_kind(name);
    s->name = xstrdup(name);
    return s;
}

static Uniform *uniform_slot(GfxShader *s, const char *name, bool create) {
    for (int i = 0; i < s->nu; i++) if (!strcmp(s->u[i].name, name)) return &s->u[i];
    if (!create) return NULL;
    if (s->nu == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->u = xrealloc(s->u, (size_t)s->cap * sizeof(Uniform)); }
    Uniform *u = &s->u[s->nu++];
    memset(u, 0, sizeof *u);
    snprintf(u->name, sizeof u->name, "%s", name);
    return u;
}
static float uval(GfxShader *s, const char *name, int i) {
    Uniform *u = uniform_slot(s, name, false);
    return u ? u->v[i] : 0;
}
bool gfx_shader_has(GfxShader *s, const char *name) { return s != NULL && name != NULL; }
void gfx_send_float(GfxShader *s, const char *name, float v) {
    if (!s) return;
    Uniform *u = uniform_slot(s, name, true);
    u->v[0] = v;
}
void gfx_send_vec2(GfxShader *s, const char *name, float x, float y) {
    if (!s) return;
    Uniform *u = uniform_slot(s, name, true);
    u->v[0] = x; u->v[1] = y;
}
void gfx_send_vec4(GfxShader *s, const char *name, const float *v) {
    if (!s) return;
    Uniform *u = uniform_slot(s, name, true);
    for (int i = 0; i < 4; i++) u->v[i] = v ? v[i] : 0;
}
void gfx_send_bool(GfxShader *s, const char *name, bool b) { gfx_send_float(s, name, b ? 1.f : 0.f); }
void gfx_set_shader(GfxShader *s) { cur_shader = s; }
GfxShader *gfx_get_shader(void) { return cur_shader; }

/* What a draw looks like under the current shader. */
typedef struct {
    bool skip;
    int mode;
    u32 tint;            /* stage 1 mix colour, alpha = amount */
    float col[4];        /* vertex colour */
    bool gradient;       /* background: per-vertex colours */
    float col2[4];       /* bottom colour when gradient */
} DrawLook;

static void hsv_to_rgb(float h, float s, float v, float *out) {
    h = h - floorf(h);
    float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
    r = r < 0 ? 0 : r > 1 ? 1 : r; g = g < 0 ? 0 : g > 1 ? 1 : g; b = b < 0 ? 0 : b > 1 ? 1 : b;
    out[0] = ((r - 1) * s + 1) * v; out[1] = ((g - 1) * s + 1) * v; out[2] = ((b - 1) * s + 1) * v;
}

static void shader_look(DrawLook *L) {
    memset(L, 0, sizeof *L);
    L->mode = MODE_MODULATE;
    memcpy(L->col, cur_col, sizeof L->col);
    GfxShader *s = cur_shader;
    if (!s || s->kind == SH_DEFAULT) return;
    bool shadow = uval(s, "shadow", 0) != 0;
    float d = fabsf(uval(s, "dissolve", 0));
    float fade = d > 0.01f ? 1 - d : 1;
    if (fade < 0) fade = 0;
    float tint[4] = { 0, 0, 0, 0 };
    switch (s->kind) {
    case SH_SKIP:
        L->skip = true;
        return;
    case SH_DISSOLVE:
    case SH_NEGATIVE:
        if (shadow) {
            L->mode = MODE_SILHOUETTE;
            L->col[0] = L->col[1] = L->col[2] = 0;
            L->col[3] = 0.3f * fade;
            return;
        }
        /* the dissolve shaders ignore the vertex colour */
        L->col[0] = L->col[1] = L->col[2] = 1;
        L->col[3] = fade;
        if (s->kind == SH_NEGATIVE) {
            L->mode = MODE_NEGATIVE;
            tint[0] = 0.31f; tint[1] = 0.39f; tint[2] = 0.40f; tint[3] = 0.3f;
        }
        if (d > 0.01f) {
            Uniform *b2 = uniform_slot(s, "burn_colour_2", false), *b1 = uniform_slot(s, "burn_colour_1", false);
            const float *bc = (b2 && b2->v[3] > 0.01f) ? b2->v : (b1 && b1->v[3] > 0.01f) ? b1->v : NULL;
            if (bc) { tint[0] = bc[0]; tint[1] = bc[1]; tint[2] = bc[2]; tint[3] = 0.6f * d; }
        }
        L->tint = pack_rgba(tint);
        return;
    case SH_FOIL: case SH_HOLO: case SH_POLY: case SH_HOLOGRAM:
        if (shadow) { L->skip = true; return; }
        L->mode = MODE_SILHOUETTE;
        if (s->kind == SH_FOIL) { L->col[0] = 0.45f; L->col[1] = 0.75f; L->col[2] = 1.0f; L->col[3] = 0.28f; }
        else if (s->kind == SH_HOLO) { L->col[0] = 1.0f; L->col[1] = 0.35f; L->col[2] = 0.55f; L->col[3] = 0.22f; }
        else if (s->kind == SH_POLY) {
            float t = uval(s, s->name, 1);
            hsv_to_rgb(t * 0.15f, 0.7f, 1.0f, L->col);
            L->col[3] = 0.3f;
        } else { L->col[0] = 0.2f; L->col[1] = 1.0f; L->col[2] = 0.8f; L->col[3] = 0.35f; }
        L->col[3] *= fade;
        return;
    case SH_DEBUFF:
        if (shadow) { L->skip = true; return; }
        L->col[0] = 1; L->col[1] = L->col[2] = 0.55f; L->col[3] = fade;
        tint[0] = 0.55f; tint[1] = 0.3f; tint[2] = 0.3f; tint[3] = 0.35f;
        L->tint = pack_rgba(tint);
        return;
    case SH_PLAYED:
        if (shadow) { L->skip = true; return; }
        L->col[0] = L->col[1] = L->col[2] = 1; L->col[3] = 0.5f * fade;
        tint[0] = tint[1] = tint[2] = 0.5f; tint[3] = 0.6f;
        L->tint = pack_rgba(tint);
        return;
    case SH_FLASH: {
        /* flash.fs: white that grows from the centre late in the splash */
        L->mode = MODE_SOLID;
        float t = uval(s, "time", 0);
        float mw = (t > 2.5f ? 0.15f * sqrtf(t - 2.5f) : 0) + (t > 11 ? (t - 11) * (t - 11) : 0);
        if (mw > 1) mw = 1;
        L->col[0] = L->col[1] = L->col[2] = 1;
        L->col[3] = uval(s, "mid_flash", 0) * mw;
        return;
    }
    case SH_SPLASH: {
        /* splash.fs: dark smoke that swirls into colour_1 over time, with
         * colour_2 highlights and a white flash (mid_flash) */
        L->mode = MODE_SOLID;
        Uniform *c1 = uniform_slot(s, "colour_1", false), *c2 = uniform_slot(s, "colour_2", false);
        static const float black[3] = { 0.6f * 79 / 255.f, 0.6f * 99 / 255.f, 0.6f * 103 / 255.f };
        float t = uval(s, "time", 0);
        float k = (t * 1.2f - 4) / 8;
        k = k < 0 ? 0 : k > 0.85f ? 0.85f : k;
        float mf = uval(s, "mid_flash", 0) * 0.8f;
        if (mf > 1) mf = 1;
        for (int i = 0; i < 3; i++) {
            float a = c1 ? c1->v[i] : 0, b = c2 ? c2->v[i] : 0;
            float top = black[i] * (1 - k) + a * k;
            float bot = top * 0.8f + b * 0.2f * k;
            L->col[i] = top * (1 - mf) + mf;
            L->col2[i] = bot * (1 - mf) + mf;
        }
        L->col[3] = L->col2[3] = 1;
        L->gradient = true;
        return;
    }
    case SH_BACKGROUND: {
        L->mode = MODE_SOLID;
        Uniform *c1 = uniform_slot(s, "colour_1", false), *c2 = uniform_slot(s, "colour_2", false);
        float a[4] = { 0, 0, 0, 1 }, b[4] = { 0, 0, 0, 1 };
        if (c1) memcpy(a, c1->v, sizeof a);
        if (c2) memcpy(b, c2->v, sizeof b);
        float t = uval(s, "time", 0);
        float w = 0.35f + 0.1f * sinf(t * 0.3f);
        for (int i = 0; i < 4; i++) { L->col[i] = a[i]; L->col2[i] = a[i] * (1 - w) + b[i] * w; }
        L->col[3] = L->col2[3] = a[3] > 0 ? a[3] : 1;
        L->gradient = true;
        return;
    }
    default:
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Textures                                                            */
/* ------------------------------------------------------------------ */
static inline u32 morton8(u32 x, u32 y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}
static int next_pow2(int v) {
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

/* Splits n pixels into chunks that each fit a power-of-two texture of at
 * most 1024, preferring one padded texture when the waste is small. */
static int split_dim(int n, int *starts, int *sizes, int *pots, int max) {
    int k = 0, pos = 0;
    while (pos < n && k < max) {
        int rem = n - pos;
        int p = next_pow2(rem);
        int take;
        if (p <= 1024 && (p - rem) <= p / 4) { take = rem; }
        else if (p > 1024) { p = 1024; take = 1024; }
        else { p >>= 1; take = p; }
        if (take > rem) take = rem;
        starts[k] = pos; sizes[k] = take; pots[k] = next_pow2(take);
        pos += take; k++;
    }
    return k;
}

typedef struct {
    C3D_Tex tex;
    int x, y, w, h;      /* region of the (possibly downscaled) image */
    int tw, th;
} TexTile;

struct GfxTexSet {
    TexTile *tiles;
    int ntiles;
    int dw, dh;          /* dimensions after downscaling */
    float down;          /* source pixels per texture pixel */
};

/* RGBA8 texture memory is tiled 8x8 in Morton order, each texel stored as
 * ABGR bytes. Row 0 is sampled at v = 1, and render targets also put the
 * top of the picture (y = 0) in row 0, so images are stored top row first
 * and drawn with v = 1 - y / height. */
static void fill_tile_rgba(TexTile *t, const u8 *src, int sw) {
    u32 *dst = (u32 *)t->tex.data;
    for (int y = 0; y < t->th; y++) {
        int sy = t->y + (y < t->h ? y : t->h - 1);
        int ty = y;
        for (int x = 0; x < t->tw; x++) {
            int sx = t->x + (x < t->w ? x : t->w - 1);
            const u8 *p = src + ((size_t)sy * sw + sx) * 4;
            u32 off = ((u32)(ty >> 3) * (u32)(t->tw >> 3) + (u32)(x >> 3)) * 64 + morton8((u32)x & 7, (u32)ty & 7);
            dst[off] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        }
    }
    C3D_TexFlush(&t->tex);
}

static u8 *downscale(const u8 *src, int w, int h, int f, int *ow, int *oh) {
    int nw = w / f, nh = h / f;
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    u8 *d = xmalloc((size_t)nw * nh * 4);
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            u32 acc[4] = { 0, 0, 0, 0 }, wsum = 0;
            for (int yy = 0; yy < f; yy++)
                for (int xx = 0; xx < f; xx++) {
                    int sx = x * f + xx, sy = y * f + yy;
                    if (sx >= w || sy >= h) continue;
                    const u8 *p = src + ((size_t)sy * w + sx) * 4;
                    /* alpha weighted so transparent pixels do not darken edges */
                    acc[0] += p[0] * p[3]; acc[1] += p[1] * p[3]; acc[2] += p[2] * p[3]; acc[3] += p[3];
                    wsum++;
                }
            u8 *o = d + ((size_t)y * nw + x) * 4;
            if (acc[3]) { o[0] = (u8)(acc[0] / acc[3]); o[1] = (u8)(acc[1] / acc[3]); o[2] = (u8)(acc[2] / acc[3]); }
            else o[0] = o[1] = o[2] = 0;
            o[3] = (u8)(wsum ? acc[3] / wsum : 0);
        }
    *ow = nw; *oh = nh;
    return d;
}

static GfxTexSet *texset_from_rgba(u8 *px, int w, int h, int down, bool linear) {
    GfxTexSet *ts = NEW(GfxTexSet);
    ts->dw = w; ts->dh = h; ts->down = (float)down;
    int cs[16], cz[16], cp[16], rs[16], rz[16], rp[16];
    int nc = split_dim(w, cs, cz, cp, 16), nr = split_dim(h, rs, rz, rp, 16);
    ts->tiles = xcalloc((size_t)(nc * nr), sizeof(TexTile));
    for (int r = 0; r < nr; r++)
        for (int c = 0; c < nc; c++) {
            TexTile *t = &ts->tiles[ts->ntiles];
            t->x = cs[c]; t->y = rs[r]; t->w = cz[c]; t->h = rz[r];
            t->tw = cp[c]; t->th = rp[r];
            if (!C3D_TexInit(&t->tex, (u16)t->tw, (u16)t->th, GPU_RGBA8)) {
                fprintf(stderr, "gfx: out of linear memory for a %dx%d texture\n", t->tw, t->th);
                continue;
            }
            C3D_TexSetFilter(&t->tex, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
            C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            fill_tile_rgba(t, px, w);
            ts->ntiles++;
        }
    return ts;
}

static bool image_ensure(GfxImage *img) {
    if (img->tex) return true;
    if (img->load_failed) return false;
    int len;
    char *data = file_read_all(img->path, &len);
    int w = 0, h = 0, n;
    u8 *px = data ? stbi_load_from_memory((const stbi_uc *)data, len, &w, &h, &n, 4) : NULL;
    free(data);
    if (!px) { fprintf(stderr, "gfx: failed to load image %s\n", img->path); img->load_failed = true; return false; }
    /* the studio logos are 1400px wide and only shown on the splash screen */
    int down = 1;
    if (strstr(img->path, "-logo")) while ((w / down > 512 || h / down > 512) && down < 8) down *= 2;
    u8 *use = px;
    int uw = w, uh = h;
    if (down > 1) use = downscale(px, w, h, down, &uw, &uh);
    img->tex = texset_from_rgba(use, uw, uh, down, true);
    if (use != px) free(use);
    stbi_image_free(px);
    return true;
}

GfxImage *gfx_load_image(const char *path, float dpiscale, bool mipmaps, bool linear) {
    (void)mipmaps; (void)linear;   /* always bilinear: the atlases are drawn scaled down on 3DS */
    int w = 0, h = 0, n;
    int len;
    char *data = file_read_all(path, &len);
    if (!data || !stbi_info_from_memory((const stbi_uc *)data, len, &w, &h, &n)) {
        free(data);
        fprintf(stderr, "gfx: failed to load image %s\n", path);
        return NULL;
    }
    free(data);
    GfxImage *g = NEW(GfxImage);
    g->pw = w; g->ph = h;
    g->w = (float)w / dpiscale;
    g->h = (float)h / dpiscale;
    g->path = xstrdup(path);
    return g;
}
void gfx_free_image(GfxImage *img) {
    if (!img) return;
    gfx_flush();
    if (img->tex) {
        for (int i = 0; i < img->tex->ntiles; i++) C3D_TexDelete(&img->tex->tiles[i].tex);
        free(img->tex->tiles);
        free(img->tex);
    }
    free(img->path);
    free(img);
}
GfxQuad gfx_quad(float x, float y, float w, float h, float sw, float sh) {
    GfxQuad q = { x, y, w, h, sw, sh };
    return q;
}

/* emits a quad in local coordinates (x0,y0)-(x1,y1) with given uvs */
static void emit_quad_look(C3D_Tex *tex, const DrawLook *L, double x0, double y0, double x1, double y1,
                           float u0, float v0, float u1, float v1) {
    if (L->skip) return;
    set_state(tex, L->mode, L->tint, false);
    Vertex *v = alloc_verts(6);
    if (!v) return;
    float ax, ay, bx, by, cx, cy, dx, dy;
    xf(x0, y0, &ax, &ay);
    xf(x0, y1, &bx, &by);
    xf(x1, y1, &cx, &cy);
    xf(x1, y0, &dx, &dy);
    u32 ct = pack_rgba(L->col), cb = L->gradient ? pack_rgba(L->col2) : ct;
    put_vert(&v[0], ax, ay, u0, v0, ct);
    put_vert(&v[1], bx, by, u0, v1, cb);
    put_vert(&v[2], cx, cy, u1, v1, cb);
    put_vert(&v[3], ax, ay, u0, v0, ct);
    put_vert(&v[4], cx, cy, u1, v1, cb);
    put_vert(&v[5], dx, dy, u1, v0, ct);
}

void gfx_draw_image(GfxImage *img, const GfxQuad *q, double x, double y, double r, double sx, double sy, double ox, double oy) {
    if (!img) return;
    DrawLook L;
    shader_look(&L);
    if (L.skip) return;
    if (!image_ensure(img)) return;
    GfxTexSet *ts = img->tex;
    float qw = q ? q->w : img->w, qh = q ? q->h : img->h;
    /* quad region in source pixels */
    double px0 = 0, py0 = 0, px1 = img->pw, py1 = img->ph;
    if (q) {
        px0 = q->x / q->sw * img->pw; py0 = q->y / q->sh * img->ph;
        px1 = (q->x + q->w) / q->sw * img->pw; py1 = (q->y + q->h) / q->sh * img->ph;
    }
    if (px1 <= px0 || py1 <= py0) return;
    gfx_push();
    gfx_translate(x, y);
    if (r != 0) gfx_rotate(r);
    gfx_scale(sx, sy);
    gfx_translate(-ox, -oy);
    double f = ts->down;
    for (int i = 0; i < ts->ntiles; i++) {
        TexTile *t = &ts->tiles[i];
        double tx0 = t->x * f, ty0 = t->y * f, tx1 = (t->x + t->w) * f, ty1 = (t->y + t->h) * f;
        double ix0 = MAX(px0, tx0), iy0 = MAX(py0, ty0), ix1 = MIN(px1, tx1), iy1 = MIN(py1, ty1);
        if (ix1 <= ix0 || iy1 <= iy0) continue;
        double lx0 = (ix0 - px0) / (px1 - px0) * qw, lx1 = (ix1 - px0) / (px1 - px0) * qw;
        double ly0 = (iy0 - py0) / (py1 - py0) * qh, ly1 = (iy1 - py0) / (py1 - py0) * qh;
        float u0 = (float)((ix0 / f - t->x) / t->tw), u1 = (float)((ix1 / f - t->x) / t->tw);
        float v0 = (float)(1.0 - (iy0 / f - t->y) / t->th), v1 = (float)(1.0 - (iy1 / f - t->y) / t->th);
        emit_quad_look(&t->tex, &L, lx0, ly0, lx1, ly1, u0, v0, u1, v1);
    }
    gfx_pop();
}

void gfx_rectangle(int mode, double x, double y, double w, double h) {
    if (mode == GFX_FILL) {
        DrawLook L;
        shader_look(&L);
        if (L.mode == MODE_NEGATIVE) L.mode = MODE_MODULATE;
        emit_quad_look(&white_tex, &L, x, y, x + w, y + h, 0, 1, 1, 0);
    } else {
        float v[8] = { (float)x, (float)y, (float)(x + w), (float)y, (float)(x + w), (float)(y + h), (float)x, (float)(y + h) };
        gfx_polygon(GFX_LINE, v, 8);
    }
}

static void emit_tri(Vertex *v, float ax, float ay, float bx, float by, float cx, float cy, u32 c) {
    put_vert(&v[0], ax, ay, 0.5f, 0.5f, c);
    put_vert(&v[1], bx, by, 0.5f, 0.5f, c);
    put_vert(&v[2], cx, cy, 0.5f, 0.5f, c);
}

static void line_segment(double x0, double y0, double x1, double y1, double hw, u32 c) {
    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return;
    double nx = -dy / len * hw, ny = dx / len * hw;
    double ex = dx / len * hw, ey = dy / len * hw;
    float p[8];
    xf(x0 + nx - ex, y0 + ny - ey, &p[0], &p[1]);
    xf(x1 + nx + ex, y1 + ny + ey, &p[2], &p[3]);
    xf(x1 - nx + ex, y1 - ny + ey, &p[4], &p[5]);
    xf(x0 - nx - ex, y0 - ny - ey, &p[6], &p[7]);
    Vertex *v = alloc_verts(6);
    if (!v) return;
    emit_tri(v, p[0], p[1], p[2], p[3], p[4], p[5], c);
    emit_tri(v + 3, p[0], p[1], p[4], p[5], p[6], p[7], c);
}

void gfx_polygon(int mode, const float *v, int nf) {
    int n = nf / 2;
    if (n < 2) return;
    DrawLook L;
    shader_look(&L);
    if (L.skip) return;
    set_state(&white_tex, L.mode == MODE_NEGATIVE ? MODE_MODULATE : L.mode, L.tint, false);
    u32 c = pack_rgba(L.col);
    if (mode == GFX_FILL) {
        if (n < 3) return;
        float ax, ay;
        xf(v[0], v[1], &ax, &ay);
        for (int i = 1; i + 1 < n; i++) {
            float bx, by, cx, cy;
            xf(v[i * 2], v[i * 2 + 1], &bx, &by);
            xf(v[(i + 1) * 2], v[(i + 1) * 2 + 1], &cx, &cy);
            Vertex *o = alloc_verts(3);
            if (!o) return;
            emit_tri(o, ax, ay, bx, by, cx, cy, c);
        }
    } else {
        double hw = line_width * 0.5;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            line_segment(v[i * 2], v[i * 2 + 1], v[j * 2], v[j * 2 + 1], hw, c);
        }
    }
}

void gfx_clear(float r, float g, float b, float a) {
    float col[4] = { r, g, b, a };
    set_state(&white_tex, MODE_SOLID, 0, true);
    Vertex *v = alloc_verts(6);
    if (!v) return;
    u32 c = pack_rgba(col);
    float w = 2048, h = 2048;
    put_vert(&v[0], 0, 0, 0, 0, c); put_vert(&v[1], 0, h, 0, 0, c); put_vert(&v[2], w, h, 0, 0, c);
    put_vert(&v[3], 0, 0, 0, 0, c); put_vert(&v[4], w, h, 0, 0, c); put_vert(&v[5], w, 0, 0, 0, c);
}

/* ------------------------------------------------------------------ */
/* Fonts: stb_truetype with an on-demand glyph cache                   */
/* ------------------------------------------------------------------ */
/* The game asks for fonts at ~200px and draws them scaled down to a few
 * pixels; glyphs are rasterised at a small fixed size instead and scaled. */
#define FONT_RASTER_PX 32.0f
#define FONT_PAGE 512
#define FONT_MAX_PAGES 24
typedef struct {
    int cp;
    int page;
    float u0, v0, u1, v1;
    float xoff, yoff, w, h;   /* placement relative to pen/top of line, in requested-size pixels */
    float advance;
    bool valid;
} Glyph;

typedef struct {
    C3D_Tex tex;
    int shelf_x, shelf_y, shelf_h;
    bool dirty;
} FontPage;

struct GfxFont {
    stbtt_fontinfo info;
    unsigned char *data;
    float size, scale, rscale, rratio;
    float ascent, descent, linegap, height;
    Glyph *glyphs;
    int gcap, gcount;
    FontPage pages[FONT_MAX_PAGES];
    int npages;
};
static GfxFont *fonts[16];
static int nfonts;

static Glyph *glyph_slot(GfxFont *f, int cp) {
    unsigned h = (unsigned)cp * 2654435761u;
    for (int i = 0; i < f->gcap; i++) {
        Glyph *g = &f->glyphs[(h + (unsigned)i) & (unsigned)(f->gcap - 1)];
        if (!g->valid || g->cp == cp) return g;
    }
    return NULL;
}
static void glyph_grow(GfxFont *f) {
    Glyph *old = f->glyphs; int oc = f->gcap;
    f->gcap = oc ? oc * 2 : 512;
    f->glyphs = xcalloc((size_t)f->gcap, sizeof(Glyph));
    for (int i = 0; i < oc; i++) if (old[i].valid) *glyph_slot(f, old[i].cp) = old[i];
    free(old);
}
static int font_new_page(GfxFont *f) {
    if (f->npages >= FONT_MAX_PAGES) return -1;
    FontPage *p = &f->pages[f->npages];
    if (!C3D_TexInit(&p->tex, FONT_PAGE, FONT_PAGE, GPU_LA8)) return -1;
    memset(p->tex.data, 0, (size_t)FONT_PAGE * FONT_PAGE * 2);
    C3D_TexSetFilter(&p->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&p->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    p->shelf_x = p->shelf_y = p->shelf_h = 0;
    p->dirty = true;
    return f->npages++;
}

static void page_blit(FontPage *p, int px, int py, int w, int h, const u8 *alpha) {
    u16 *dst = (u16 *)p->tex.data;
    for (int y = 0; y < h; y++) {
        int ty = py + y;
        for (int x = 0; x < w; x++) {
            int tx = px + x;
            u32 off = ((u32)(ty >> 3) * (FONT_PAGE >> 3) + (u32)(tx >> 3)) * 64 + morton8((u32)tx & 7, (u32)ty & 7);
            dst[off] = (u16)(0xFF00 | alpha[y * w + x]);   /* LA8: luminance high byte, alpha low */
        }
    }
    p->dirty = true;
}

static const Glyph *font_glyph(GfxFont *f, int cp) {
    if ((f->gcount + 1) * 2 > f->gcap) glyph_grow(f);
    Glyph *g = glyph_slot(f, cp);
    if (g->valid) return g;
    memset(g, 0, sizeof *g);
    g->cp = cp; g->valid = true; f->gcount++;
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    g->advance = floorf(adv * f->scale + 0.5f);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&f->info, cp, f->rscale, f->rscale, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;
    g->page = -1;
    if (bw > 0 && bh > 0) {
        int pad = 1;
        int pw = bw + pad * 2, ph = bh + pad * 2;
        if (pw > FONT_PAGE || ph > FONT_PAGE) return g;
        int pi = f->npages - 1;
        if (pi < 0) pi = font_new_page(f);
        if (pi < 0) return g;
        FontPage *p = &f->pages[pi];
        if (p->shelf_x + pw > FONT_PAGE) { p->shelf_x = 0; p->shelf_y += p->shelf_h; p->shelf_h = 0; }
        if (p->shelf_y + ph > FONT_PAGE) {
            pi = font_new_page(f);
            if (pi < 0) return g;
            p = &f->pages[pi];
        }
        u8 *bmp = xcalloc((size_t)pw * ph, 1);
        stbtt_MakeCodepointBitmap(&f->info, bmp + pad * pw + pad, bw, bh, pw, f->rscale, f->rscale, cp);
        page_blit(p, p->shelf_x, p->shelf_y, pw, ph, bmp);
        free(bmp);
        g->page = pi;
        g->u0 = (float)p->shelf_x / FONT_PAGE;
        g->v0 = 1.0f - (float)p->shelf_y / FONT_PAGE;
        g->u1 = (float)(p->shelf_x + pw) / FONT_PAGE;
        g->v1 = 1.0f - (float)(p->shelf_y + ph) / FONT_PAGE;
        g->xoff = (float)(x0 - pad) * f->rratio;
        g->yoff = f->ascent + (float)(y0 - pad) * f->rratio;
        g->w = (float)pw * f->rratio; g->h = (float)ph * f->rratio;
        p->shelf_x += pw;
        if (ph > p->shelf_h) p->shelf_h = ph;
    }
    return g;
}

GfxFont *gfx_load_font(const char *path, float size) {
    int len;
    char *data = file_read_all(path, &len);
    if (!data) { fprintf(stderr, "gfx: missing font %s\n", path); return NULL; }
    GfxFont *f = NEW(GfxFont);
    f->data = (unsigned char *)data;
    if (!stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
        fprintf(stderr, "gfx: bad font %s\n", path);
        free(data); free(f);
        return NULL;
    }
    f->size = size;
    f->scale = stbtt_ScaleForMappingEmToPixels(&f->info, size);
    float rsize = size < FONT_RASTER_PX ? size : FONT_RASTER_PX;
    f->rscale = stbtt_ScaleForMappingEmToPixels(&f->info, rsize);
    f->rratio = f->scale / f->rscale;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    f->ascent = floorf(asc * f->scale + 0.5f);
    f->descent = floorf(desc * f->scale + 0.5f);
    f->linegap = floorf(gap * f->scale + 0.5f);
    f->height = floorf((asc - desc + gap) * f->scale + 0.5f);
    if (nfonts < (int)ARRLEN(fonts)) fonts[nfonts++] = f;
    return f;
}

float gfx_font_char_width(GfxFont *f, int cp) {
    if (!f) return 0;
    return font_glyph(f, cp)->advance;
}
float gfx_font_width(GfxFont *f, const char *text) {
    if (!f || !text) return 0;
    float w = 0, best = 0;
    int prev = 0;
    while (*text) {
        int l, cp = utf8_decode(text, &l);
        text += l;
        if (cp == '\n') { if (w > best) best = w; w = 0; prev = 0; continue; }
        if (prev) w += floorf(stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * f->scale + 0.5f);
        w += font_glyph(f, cp)->advance;
        prev = cp;
    }
    return w > best ? w : best;
}
float gfx_font_height(GfxFont *f) { return f ? f->height : 0; }

void gfx_print(GfxFont *f, const char *text, double x, double y, double r, double sx, double sy, double ox, double oy) {
    if (!f || !text) return;
    DrawLook L;
    shader_look(&L);
    if (L.skip) return;
    gfx_push();
    gfx_translate(x, y);
    if (r != 0) gfx_rotate(r);
    gfx_scale(sx, sy);
    gfx_translate(-ox, -oy);
    float pen = 0, line = 0;
    int prev = 0;
    while (*text) {
        int l, cp = utf8_decode(text, &l);
        text += l;
        if (cp == '\n') { pen = 0; line += f->height; prev = 0; continue; }
        if (prev) pen += floorf(stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * f->scale + 0.5f);
        const Glyph *g = font_glyph(f, cp);
        if (g->page >= 0)
            emit_quad_look(&f->pages[g->page].tex, &L, pen + g->xoff, line + g->yoff, pen + g->xoff + g->w, line + g->yoff + g->h,
                           g->u0, g->v0, g->u1, g->v1);
        pen += g->advance;
        prev = cp;
    }
    gfx_pop();
}

static void flush_font_pages(void) {
    for (int i = 0; i < nfonts; i++)
        for (int j = 0; j < fonts[i]->npages; j++) {
            FontPage *p = &fonts[i]->pages[j];
            if (p->dirty) { C3D_TexFlush(&p->tex); p->dirty = false; }
        }
}

/* ------------------------------------------------------------------ */
/* Canvases (render targets)                                           */
/* ------------------------------------------------------------------ */
struct GfxCanvas {
    C3D_Tex tex;
    C3D_RenderTarget *rt;
    int w, h;
};

GfxCanvas *gfx_new_canvas(int w, int h) {
    if (w < 8) w = 8;
    if (h < 8) h = 8;
    if (w > 1024) w = 1024;
    if (h > 1024) h = 1024;
    GfxCanvas *c = NEW(GfxCanvas);
    c->w = w; c->h = h;
    if (!C3D_TexInitVRAM(&c->tex, (u16)next_pow2(w), (u16)next_pow2(h), GPU_RGBA8) &&
        !C3D_TexInit(&c->tex, (u16)next_pow2(w), (u16)next_pow2(h), GPU_RGBA8)) {
        fprintf(stderr, "gfx: could not allocate a %dx%d canvas\n", w, h);
        free(c);
        return NULL;
    }
    C3D_TexSetFilter(&c->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&c->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    c->rt = C3D_RenderTargetCreateFromTex(&c->tex, GPU_TEXFACE_2D, 0, -1);
    return c;
}
void gfx_free_canvas(GfxCanvas *c) {
    if (!c || c == screen_canvas) return;
    if (cur_canvas == c) gfx_set_canvas(NULL);
    gfx_flush();
    C3D_RenderTargetDelete(c->rt);
    C3D_TexDelete(&c->tex);
    free(c);
}
int gfx_canvas_height(GfxCanvas *c) { return c ? c->h : 0; }
int gfx_canvas_width(GfxCanvas *c) { return c ? c->w : 0; }

static void bind_target(C3D_RenderTarget *rt, int w, int h, bool tilt) {
    gfx_flush();
    C3D_FrameDrawOn(rt);
    if (tilt) Mtx_OrthoTilt(&proj, 0, (float)w, (float)h, 0, 1, -1, true);
    else Mtx_Ortho(&proj, 0, (float)w, (float)h, 0, 1, -1, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_projection, &proj);
    bs_applied_valid = false;
}
static void bind_canvas(GfxCanvas *c) { bind_target(c->rt, c->tex.width, c->tex.height, false); }

void gfx_set_canvas(GfxCanvas *c) {
    if (!c) c = screen_canvas;
    if (c == cur_canvas) return;
    gfx_flush();   /* recorded batches remember their target */
    cur_canvas = c;
    if (in_frame && !recording) bind_canvas(c);
}
void gfx_draw_canvas(GfxCanvas *c, float x, float y) {
    if (!c) return;
    DrawLook L;
    shader_look(&L);
    if (L.skip) return;
    /* a target's v axis runs the same way as an uploaded image's */
    emit_quad_look(&c->tex, &L, x, y, x + c->w, y + c->h, 0, 1, (float)c->w / c->tex.width, 1 - (float)c->h / c->tex.height);
}

/* ------------------------------------------------------------------ */
/* Window / frame                                                      */
/* ------------------------------------------------------------------ */
static bool screenshot_pending;
static char screenshot_path[256];

bool gfx_open_window(int w, int h, const char *title) {
    (void)w; (void)h; (void)title;
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4)) return false;
    top_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);
    bot_target = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, -1);
    if (!top_target || !bot_target) return false;
    C3D_RenderTargetSetOutput(top_target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    C3D_RenderTargetSetOutput(bot_target, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    vshader_dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
    shaderProgramInit(&program);
    shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
    C3D_BindProgram(&program);
    u_projection = shaderInstanceGetUniformLocation(program.vertexShader, "projection");

    C3D_AttrInfo *attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4);

    vbuf = linearAlloc(sizeof(Vertex) * VBUF_MAX);
    if (!vbuf) return false;
    C3D_BufInfo *bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, vbuf, sizeof(Vertex), 3, 0x210);

    C3D_TexInit(&white_tex, 8, 8, GPU_RGBA8);
    memset(white_tex.data, 0xFF, 8 * 8 * 4);
    C3D_TexFlush(&white_tex);
    C3D_TexSetFilter(&white_tex, GPU_NEAREST, GPU_NEAREST);

    screen_canvas = gfx_new_canvas(WIN_W, WIN_H);
    bottom_canvas = gfx_new_canvas(WIN_W, WIN_H);
    cmds = malloc(sizeof(DrawCmd) * MAX_CMDS);
    if (!screen_canvas || !bottom_canvas || !cmds) return false;
    cur_canvas = screen_canvas;
    gfx_init();
    return true;
}

void gfx_init(void) {
    C3D_CullFace(GPU_CULL_NONE);   /* LOVE never culls; polygon winding varies */
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    bs_applied_valid = false;
}
void gfx_close_window(void) {
    C3D_Fini();
    gfxExit();
}
bool gfx_window_should_close(void) { return !aptMainLoop(); }
int gfx_width(void) { return WIN_W; }
int gfx_height(void) { return WIN_H; }

static void write_screenshot(void);

void gfx_begin_frame(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    in_frame = true;
    /* the previous frame has fully finished on the GPU here */
    if (screenshot_pending) { write_screenshot(); screenshot_pending = false; }
    vcount = batch_start = 0;
    vbuf_overflow_warned = false;
    bs_applied_valid = false;
    memset(&bs_pending, 0, sizeof bs_pending);
    gfx_init();
    cur_canvas = screen_canvas;
    ncmds = 0;
    screen_mask = GFX_SCREEN_BOTH;
    recording = true;
    gfx_origin();
}

/* draws the recorded frame for one screen into `dst` */
static void replay(int screen, GfxCanvas *dst) {
    GfxCanvas *bound = NULL;
    for (int i = 0; i < ncmds; i++) {
        DrawCmd *c = &cmds[i];
        if (!(c->mask & screen)) continue;
        GfxCanvas *t = c->target == screen_canvas ? dst : c->target;
        if (t != screen_canvas && t != dst && screen != GFX_SCREEN_TOP) continue;  /* other canvases: draw once */
        if (t != bound) { bind_canvas(t); bound = t; }
        apply_state(&c->st);
        C3D_DrawArrays(GPU_TRIANGLES, c->start, c->count);
    }
}

static void present_quad(GfxCanvas *src, float x0, float y0, float x1, float y1) {
    set_state(&src->tex, MODE_MODULATE, 0, true);
    Vertex *v = alloc_verts(6);
    if (!v) return;
    float u1 = (float)WIN_W / src->tex.width, v1 = 1 - (float)WIN_H / src->tex.height;
    u32 c = 0xFFFFFFFF;
    put_vert(&v[0], x0, y0, 0, 1, c); put_vert(&v[1], x0, y1, 0, v1, c); put_vert(&v[2], x1, y1, u1, v1, c);
    put_vert(&v[3], x0, y0, 0, 1, c); put_vert(&v[4], x1, y1, u1, v1, c); put_vert(&v[5], x1, y0, u1, 1, c);
}

void gfx_end_frame(void) {
    cur_shader = NULL;
    gfx_flush();
    recording = false;
    replay(GFX_SCREEN_TOP, screen_canvas);
    replay(GFX_SCREEN_BOTTOM, bottom_canvas);
    /* top screen 1:1, bottom screen scaled to 320x192 */
    C3D_RenderTargetClear(bot_target, C3D_CLEAR_ALL, 0x000000FF, 0);
    bind_target(top_target, WIN_W, WIN_H, true);
    present_quad(screen_canvas, 0, 0, WIN_W, WIN_H);
    bind_target(bot_target, BOTTOM_W, BOTTOM_H, true);
    present_quad(bottom_canvas, 0, BOT_VIEW_Y, BOTTOM_W, BOT_VIEW_Y + BOT_VIEW_H);
    gfx_flush();
    flush_font_pages();
    GSPGPU_FlushDataCache(vbuf, (u32)(sizeof(Vertex) * (size_t)vcount));
    C3D_FrameEnd(0);
    in_frame = false;
    cur_canvas = screen_canvas;
}

void gfx_set_mode(int w, int h, bool fullscreen, bool borderless, int monitor, bool vsync) {
    (void)w; (void)h; (void)fullscreen; (void)borderless; (void)monitor; (void)vsync;
}
int gfx_monitor_count(void) { return 1; }
const char *gfx_monitor_name(int i) { (void)i; return "3DS"; }
void gfx_monitor_size(int i, int *w, int *h) { (void)i; *w = WIN_W; *h = WIN_H; }
int gfx_current_monitor(void) { return 0; }
void gfx_set_mouse_visible(bool v) { (void)v; }
void gfx_set_window_title(const char *t) { (void)t; }
double gfx_time(void) { return (double)svcGetSystemTick() / (double)SYSCLOCK_ARM11; }
void gfx_sleep(double s) { if (s > 0) svcSleepThread((s64)(s * 1e9)); }

/* ------------------------------------------------------------------ */
/* Screenshots (debug): read back the 400x240 target                   */
/* ------------------------------------------------------------------ */
void gfx_screenshot(const char *path) {
    snprintf(screenshot_path, sizeof screenshot_path, "%s", path);
    screenshot_pending = true;
}
static void write_canvas_png(GfxCanvas *cv, const char *path);
static void write_screenshot(void) {
    write_canvas_png(screen_canvas, screenshot_path);
    char bpath[300];
    const char *dot = strrchr(screenshot_path, '.');
    snprintf(bpath, sizeof bpath, "%.*s_bottom%s", dot ? (int)(dot - screenshot_path) : (int)strlen(screenshot_path), screenshot_path, dot ? dot : "");
    write_canvas_png(bottom_canvas, bpath);
}
static void write_canvas_png(GfxCanvas *cv, const char *path) {
    C3D_Tex *t = &cv->tex;
    GSPGPU_InvalidateDataCache(t->data, t->size);
    const u32 *src = (const u32 *)t->data;
    u8 *out = xmalloc((size_t)WIN_W * WIN_H * 3);
    for (int y = 0; y < WIN_H; y++) {
        int ty = y;
        for (int x = 0; x < WIN_W; x++) {
            u32 off = ((u32)(ty >> 3) * (u32)(t->width >> 3) + (u32)(x >> 3)) * 64 + morton8((u32)x & 7, (u32)ty & 7);
            u32 p = src[off];
            u8 *o = out + ((size_t)y * WIN_W + x) * 3;
            o[0] = (u8)(p >> 24); o[1] = (u8)(p >> 16); o[2] = (u8)(p >> 8);
        }
    }
    stbi_write_png(path, WIN_W, WIN_H, 3, out, WIN_W * 3);
    free(out);
}

/* ------------------------------------------------------------------ */
/* Input: buttons act as a gamepad, the touch screen as the mouse      */
/* ------------------------------------------------------------------ */
typedef struct { u32 key; const char *love; } PadMap;
static const PadMap PADMAP[] = {
    { KEY_A, "a" }, { KEY_B, "b" }, { KEY_X, "x" }, { KEY_Y, "y" },
    { KEY_SELECT, "back" }, { KEY_START, "start" },
    { KEY_L, "leftshoulder" }, { KEY_R, "rightshoulder" },
    { KEY_DUP, "dpup" }, { KEY_DDOWN, "dpdown" }, { KEY_DLEFT, "dpleft" }, { KEY_DRIGHT, "dpright" },
};
static double mouse_x = WIN_W / 2, mouse_y = WIN_H / 2;
static bool touching;
static float axis_prev[6];
static float axis_now[6];   /* leftx lefty rightx righty triggerleft triggerright */

static float stick(int v, int range) {
    float f = (float)v / (float)range;
    if (f > 1) f = 1;
    if (f < -1) f = -1;
    if (fabsf(f) < 0.12f) f = 0;
    return f;
}

int gfx_poll_input(InputEvent *out, int max) {
    int n = 0;
#define PUSH(ev) do { if (n < max) out[n++] = (ev); } while (0)
    if (!aptMainLoop()) { InputEvent e = { .type = INPUT_QUIT }; PUSH(e); return n; }
    hidScanInput();
    u32 down = hidKeysDown(), up = hidKeysUp();
    for (int i = 0; i < (int)ARRLEN(PADMAP); i++) {
        if (down & PADMAP[i].key) { InputEvent e = { .type = INPUT_GAMEPAD_PRESSED, .gamepad = 0 }; snprintf(e.key, sizeof e.key, "%s", PADMAP[i].love); PUSH(e); }
        if (up & PADMAP[i].key) { InputEvent e = { .type = INPUT_GAMEPAD_RELEASED, .gamepad = 0 }; snprintf(e.key, sizeof e.key, "%s", PADMAP[i].love); PUSH(e); }
    }
    circlePosition cp, cs;
    hidCircleRead(&cp);
    hidCstickRead(&cs);
    u32 held = hidKeysHeld();
    axis_now[0] = stick(cp.dx, 150);
    axis_now[1] = -stick(cp.dy, 150);
    axis_now[2] = stick(cs.dx, 150);
    axis_now[3] = -stick(cs.dy, 150);
    axis_now[4] = (held & KEY_ZL) ? 1.f : 0.f;
    axis_now[5] = (held & KEY_ZR) ? 1.f : 0.f;
    for (int a = 0; a < 6; a++) {
        if (fabsf(axis_now[a] - axis_prev[a]) > 0.01f) {
            axis_prev[a] = axis_now[a];
            InputEvent e = { .type = INPUT_GAMEPAD_AXIS, .gamepad = 0, .value = axis_now[a], .button = a };
            PUSH(e);
        }
    }
    if (held & KEY_TOUCH) {
        touchPosition tp;
        hidTouchRead(&tp);
        double gx = tp.px * (double)WIN_W / BOTTOM_W;
        double gy = (tp.py - BOT_VIEW_Y) * (double)WIN_H / BOT_VIEW_H;
        if (gy < 0) gy = 0;
        if (gy > WIN_H - 1) gy = WIN_H - 1;
        if (gx != mouse_x || gy != mouse_y) {
            mouse_x = gx; mouse_y = gy;
            InputEvent e = { .type = INPUT_MOUSE_MOVED, .x = gx, .y = gy, .touch = true };
            PUSH(e);
        }
        if (!touching) {
            touching = true;
            InputEvent e = { .type = INPUT_MOUSE_PRESSED, .x = gx, .y = gy, .button = 1, .touch = true };
            PUSH(e);
        }
    } else if (touching) {
        touching = false;
        InputEvent e = { .type = INPUT_MOUSE_RELEASED, .x = mouse_x, .y = mouse_y, .button = 1, .touch = true };
        PUSH(e);
    }
#undef PUSH
    return n;
}
static bool mouse_override; static double mouse_ox, mouse_oy;
void gfx_override_mouse(bool on, double x, double y) { mouse_override = on; mouse_ox = x; mouse_oy = y; }
void gfx_mouse_pos(double *x, double *y) {
    if (mouse_override) { *x = mouse_ox; *y = mouse_oy; return; }
    *x = mouse_x; *y = mouse_y;
}
float gfx_gamepad_axis(int g, const char *axis) {
    if (g != 0) return 0;
    if (!strcmp(axis, "leftx")) return axis_now[0];
    if (!strcmp(axis, "lefty")) return axis_now[1];
    if (!strcmp(axis, "rightx")) return axis_now[2];
    if (!strcmp(axis, "righty")) return axis_now[3];
    if (!strcmp(axis, "triggerleft")) return axis_now[4];
    if (!strcmp(axis, "triggerright")) return axis_now[5];
    return 0;
}
bool gfx_gamepad_available(int g) { return g == 0; }
const char *gfx_gamepad_name(int g) { return g == 0 ? "Nintendo 3DS" : ""; }
bool gfx_key_down(const char *k) { (void)k; return false; }
static char clipboard[1024];
void gfx_set_clipboard(const char *s) { snprintf(clipboard, sizeof clipboard, "%s", s ? s : ""); }
const char *gfx_get_clipboard(void) { return clipboard; }
void gfx_open_url(const char *url) { (void)url; }
