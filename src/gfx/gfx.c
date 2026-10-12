/* gfx.c - LOVE-like graphics, window, input and audio on top of raylib. */
#include "gfx.h"
#include "raylib.h"
#include "rlgl.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "external/stb_truetype.h"

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

static unsigned char to_u8(float v) {
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return (unsigned char)(v * 255.0f + 0.5f);
}
static void emit_color(void) { rlColor4ub(to_u8(cur_col[0]), to_u8(cur_col[1]), to_u8(cur_col[2]), to_u8(cur_col[3])); }

/* emits a textured quad given in local coordinates (x0,y0)-(x1,y1) */
static void emit_quad(unsigned int tex, double x0, double y0, double x1, double y1, float u0, float v0, float u1, float v1) {
    float ax, ay, bx, by, cx, cy, dx, dy;
    xf(x0, y0, &ax, &ay);
    xf(x0, y1, &bx, &by);
    xf(x1, y1, &cx, &cy);
    xf(x1, y0, &dx, &dy);
    rlSetTexture(tex);
    rlBegin(RL_QUADS);
    emit_color();
    rlNormal3f(0, 0, 1);
    rlTexCoord2f(u0, v0); rlVertex2f(ax, ay);
    rlTexCoord2f(u0, v1); rlVertex2f(bx, by);
    rlTexCoord2f(u1, v1); rlVertex2f(cx, cy);
    rlTexCoord2f(u1, v0); rlVertex2f(dx, dy);
    rlEnd();
    rlSetTexture(0);
}

static void emit_tri(float ax, float ay, float bx, float by, float cx, float cy) {
    /* already transformed */
    rlTexCoord2f(0, 0); rlVertex2f(ax, ay);
    rlTexCoord2f(0, 1); rlVertex2f(bx, by);
    rlTexCoord2f(1, 1); rlVertex2f(cx, cy);
}

void gfx_flush(void) { rlDrawRenderBatchActive(); }

void gfx_clear(float r, float g, float b, float a) {
    rlDrawRenderBatchActive();
    ClearBackground((Color){ to_u8(r), to_u8(g), to_u8(b), to_u8(a) });
}

/* ------------------------------------------------------------------ */
/* Images                                                              */
/* ------------------------------------------------------------------ */
GfxImage *gfx_load_image(const char *path, float dpiscale, bool mipmaps, bool linear) {
    Image im = LoadImage(path);
    if (!im.data) { fprintf(stderr, "gfx: failed to load image %s\n", path); return NULL; }
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Texture2D t = LoadTextureFromImage(im);
    if (mipmaps) GenTextureMipmaps(&t);
    if (linear) SetTextureFilter(t, mipmaps ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
    else SetTextureFilter(t, TEXTURE_FILTER_POINT);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    UnloadImage(im);
    GfxImage *g = NEW(GfxImage);
    g->tex_id = t.id;
    g->pw = t.width; g->ph = t.height;
    g->w = (float)t.width / dpiscale;
    g->h = (float)t.height / dpiscale;
    g->path = xstrdup(path);
    return g;
}
void gfx_free_image(GfxImage *img) {
    if (!img) return;
    rlUnloadTexture(img->tex_id);
    free(img->path);
    free(img);
}
GfxQuad gfx_quad(float x, float y, float w, float h, float sw, float sh) {
    GfxQuad q = { x, y, w, h, sw, sh };
    return q;
}

void gfx_draw_image(GfxImage *img, const GfxQuad *q, double x, double y, double r, double sx, double sy, double ox, double oy) {
    if (!img) return;
    float qw = q ? q->w : img->w, qh = q ? q->h : img->h;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (q) {
        u0 = q->x / q->sw; v0 = q->y / q->sh;
        u1 = (q->x + q->w) / q->sw; v1 = (q->y + q->h) / q->sh;
    }
    gfx_push();
    gfx_translate(x, y);
    if (r != 0) gfx_rotate(r);
    gfx_scale(sx, sy);
    gfx_translate(-ox, -oy);
    emit_quad(img->tex_id, 0, 0, qw, qh, u0, v0, u1, v1);
    gfx_pop();
}

void gfx_rectangle(int mode, double x, double y, double w, double h) {
    if (mode == GFX_FILL) {
        emit_quad(rlGetTextureIdDefault(), x, y, x + w, y + h, 0, 0, 1, 1);
    } else {
        float v[8] = { (float)x, (float)y, (float)(x + w), (float)y, (float)(x + w), (float)(y + h), (float)x, (float)(y + h) };
        gfx_polygon(GFX_LINE, v, 8);
    }
}

static void line_segment(double x0, double y0, double x1, double y1, double hw) {
    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return;
    double nx = -dy / len * hw, ny = dx / len * hw;
    /* extend slightly along the direction so corners join */
    double ex = dx / len * hw, ey = dy / len * hw;
    float p[8];
    xf(x0 + nx - ex, y0 + ny - ey, &p[0], &p[1]);
    xf(x1 + nx + ex, y1 + ny + ey, &p[2], &p[3]);
    xf(x1 - nx + ex, y1 - ny + ey, &p[4], &p[5]);
    xf(x0 - nx - ex, y0 - ny - ey, &p[6], &p[7]);
    emit_tri(p[0], p[1], p[2], p[3], p[4], p[5]);
    emit_tri(p[0], p[1], p[4], p[5], p[6], p[7]);
}

void gfx_polygon(int mode, const float *v, int nf) {
    int n = nf / 2;
    if (n < 2) return;
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_TRIANGLES);
    emit_color();
    if (mode == GFX_FILL) {
        if (n >= 3) {
            float ax, ay;
            xf(v[0], v[1], &ax, &ay);
            for (int i = 1; i + 1 < n; i++) {
                float bx, by, cx, cy;
                xf(v[i * 2], v[i * 2 + 1], &bx, &by);
                xf(v[(i + 1) * 2], v[(i + 1) * 2 + 1], &cx, &cy);
                emit_tri(ax, ay, bx, by, cx, cy);
            }
        }
    } else {
        double hw = line_width * 0.5;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            line_segment(v[i * 2], v[i * 2 + 1], v[j * 2], v[j * 2 + 1], hw);
        }
    }
    rlEnd();
    rlSetTexture(0);
}

/* ------------------------------------------------------------------ */
/* Fonts: stb_truetype with an on-demand glyph cache                   */
/* ------------------------------------------------------------------ */
#define FONT_PAGE 2048
typedef struct {
    int cp;
    int page;
    float u0, v0, u1, v1;
    float xoff, yoff, w, h;   /* placement relative to pen/top of line */
    float advance;
    bool valid;
} Glyph;

typedef struct {
    unsigned int tex;
    int shelf_x, shelf_y, shelf_h;
} FontPage;

struct GfxFont {
    stbtt_fontinfo info;
    unsigned char *data;
    float size, scale;
    float ascent, descent, linegap, height;
    Glyph *glyphs;     /* open hash by codepoint */
    int gcap, gcount;
    FontPage pages[16];
    int npages;
};

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
    if (f->npages >= 16) return -1;
    unsigned char *blank = xcalloc((size_t)FONT_PAGE * FONT_PAGE * 2, 1);
    for (int i = 0; i < FONT_PAGE * FONT_PAGE; i++) blank[i * 2] = 255;
    FontPage *p = &f->pages[f->npages];
    p->tex = rlLoadTexture(blank, FONT_PAGE, FONT_PAGE, PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA, 1);
    Texture2D t = { p->tex, FONT_PAGE, FONT_PAGE, 1, PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA };
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    free(blank);
    p->shelf_x = p->shelf_y = p->shelf_h = 0;
    return f->npages++;
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
    stbtt_GetCodepointBitmapBox(&f->info, cp, f->scale, f->scale, &x0, &y0, &x1, &y1);
    int bw = x1 - x0, bh = y1 - y0;
    g->page = -1;
    if (bw > 0 && bh > 0) {
        int pad = 2;
        int pw = bw + pad * 2, ph = bh + pad * 2;
        int pi = f->npages - 1;
        if (pi < 0) pi = font_new_page(f);
        FontPage *p = &f->pages[pi];
        if (p->shelf_x + pw > FONT_PAGE) { p->shelf_x = 0; p->shelf_y += p->shelf_h; p->shelf_h = 0; }
        if (p->shelf_y + ph > FONT_PAGE) {
            pi = font_new_page(f);
            if (pi < 0) return g;
            p = &f->pages[pi];
        }
        unsigned char *bmp = xcalloc((size_t)pw * ph, 1);
        stbtt_MakeCodepointBitmap(&f->info, bmp + pad * pw + pad, bw, bh, pw, f->scale, f->scale, cp);
        unsigned char *ga = xmalloc((size_t)pw * ph * 2);
        for (int i = 0; i < pw * ph; i++) { ga[i * 2] = 255; ga[i * 2 + 1] = bmp[i]; }
        rlUpdateTexture(p->tex, p->shelf_x, p->shelf_y, pw, ph, PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA, ga);
        free(bmp); free(ga);
        g->page = pi;
        g->u0 = (float)(p->shelf_x) / FONT_PAGE;
        g->v0 = (float)(p->shelf_y) / FONT_PAGE;
        g->u1 = (float)(p->shelf_x + pw) / FONT_PAGE;
        g->v1 = (float)(p->shelf_y + ph) / FONT_PAGE;
        g->xoff = (float)(x0 - pad);
        g->yoff = f->ascent + (float)(y0 - pad);
        g->w = (float)pw; g->h = (float)ph;
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
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    f->ascent = floorf(asc * f->scale + 0.5f);
    f->descent = floorf(desc * f->scale + 0.5f);
    f->linegap = floorf(gap * f->scale + 0.5f);
    f->height = floorf((asc - desc + gap) * f->scale + 0.5f);
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
            emit_quad(f->pages[g->page].tex, pen + g->xoff, line + g->yoff, pen + g->xoff + g->w, line + g->yoff + g->h, g->u0, g->v0, g->u1, g->v1);
        pen += g->advance;
        prev = cp;
    }
    gfx_pop();
}

/* ------------------------------------------------------------------ */
/* Shaders (LOVE GLSL dialect -> GLSL 330)                             */
/* ------------------------------------------------------------------ */
typedef struct { char *name; int loc; } UniformLoc;
struct GfxShader {
    Shader sh;
    UniformLoc *u;
    int nu, cap;
    int screen_size_loc;
    char *name;
};

static const char *LOVE_HEADER =
    "#define number float\n"
    "#define Image sampler2D\n"
    "#define extern uniform\n"
    "#define Texel love_texel\n"
    "vec4 love_texel(sampler2D s_, vec2 c_) { return texture(s_, c_); }\n"
    "uniform vec4 love_ScreenSize;\n";

static const char *VS_PRE =
    "#version 330\n"
    "#define VERTEX\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "uniform mat4 mvp;\n";
static const char *VS_DEFAULT_POS =
    "vec4 position(mat4 transform_projection, vec4 vertex_position) { return transform_projection * vertex_position; }\n";
static const char *VS_MAIN =
    "void main() {\n"
    "  fragTexCoord = vertexTexCoord;\n"
    "  fragColor = vertexColor;\n"
    "  gl_Position = position(mvp, vec4(vertexPosition, 1.0));\n"
    /* LOVE draws 2D geometry at clip z = 0; raylib's ortho + depth counter puts z near
     * +-1, which gets clipped once a shader changes w (the card skew/tilt effect) */
    "  gl_Position.z = 0.0;\n"
    "}\n";
static const char *FS_PRE =
    "#version 330\n"
    "#define PIXEL\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n";
static const char *FS_MAIN =
    "void main() {\n"
    "  vec2 love_PixelCoord = vec2(gl_FragCoord.x, (gl_FragCoord.y * love_ScreenSize.z) + love_ScreenSize.w);\n"
    "  finalColor = effect(fragColor, texture0, fragTexCoord, love_PixelCoord);\n"
    "}\n";

GfxShader *gfx_load_shader(const char *path) {
    char *src = file_read_all(path, NULL);
    if (!src) { fprintf(stderr, "gfx: missing shader %s\n", path); return NULL; }
    bool has_position = strstr(src, "vec4 position") != NULL;
    StrBuf vs, fs;
    sb_init(&vs); sb_init(&fs);
    /* the shader files have no trailing newline */
    sb_add(&vs, VS_PRE); sb_add(&vs, LOVE_HEADER); sb_add(&vs, src); sb_add(&vs, "\n");
    if (!has_position) sb_add(&vs, VS_DEFAULT_POS);
    sb_add(&vs, VS_MAIN);
    sb_add(&fs, FS_PRE); sb_add(&fs, LOVE_HEADER); sb_add(&fs, src); sb_add(&fs, "\n");
    if (!strstr(src, "vec4 effect")) sb_add(&fs, "vec4 effect(vec4 c_, Image t_, vec2 tc_, vec2 sc_) { return Texel(t_, tc_) * c_; }\n");
    sb_add(&fs, FS_MAIN);
    Shader sh = LoadShaderFromMemory(vs.buf, fs.buf);
    sb_free(&vs); sb_free(&fs);
    free(src);
    if (sh.id == rlGetShaderIdDefault()) {
        fprintf(stderr, "gfx: failed to compile shader %s\n", path);
    }
    GfxShader *g = NEW(GfxShader);
    g->sh = sh;
    g->screen_size_loc = GetShaderLocation(sh, "love_ScreenSize");
    g->name = xstrdup(path);
    return g;
}

static int uniform_loc(GfxShader *s, const char *name) {
    for (int i = 0; i < s->nu; i++) if (!strcmp(s->u[i].name, name)) return s->u[i].loc;
    if (s->nu == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->u = xrealloc(s->u, (size_t)s->cap * sizeof(UniformLoc)); }
    s->u[s->nu].name = xstrdup(name);
    s->u[s->nu].loc = GetShaderLocation(s->sh, name);
    return s->u[s->nu++].loc;
}
bool gfx_shader_has(GfxShader *s, const char *name) { return s && uniform_loc(s, name) >= 0; }

static void set_uniform(GfxShader *s, const char *name, const void *v, int type) {
    if (!s) return;
    int loc = uniform_loc(s, name);
    if (loc < 0) return;
    rlDrawRenderBatchActive();
    rlEnableShader(s->sh.id);
    rlSetUniform(loc, v, type, 1);
    rlEnableShader(cur_shader ? cur_shader->sh.id : rlGetShaderIdDefault());
}
void gfx_send_float(GfxShader *s, const char *name, float v) { set_uniform(s, name, &v, RL_SHADER_UNIFORM_FLOAT); }
void gfx_send_vec2(GfxShader *s, const char *name, float x, float y) { float v[2] = { x, y }; set_uniform(s, name, v, RL_SHADER_UNIFORM_VEC2); }
void gfx_send_vec4(GfxShader *s, const char *name, const float *v) {
    static const float zero[4] = { 0, 0, 0, 0 };
    set_uniform(s, name, v ? v : zero, RL_SHADER_UNIFORM_VEC4);
}
void gfx_send_bool(GfxShader *s, const char *name, bool b) { int v = b ? 1 : 0; set_uniform(s, name, &v, RL_SHADER_UNIFORM_INT); }

static void current_target_size(int *w, int *h);
static void apply_screen_size(GfxShader *s) {
    if (!s || s->screen_size_loc < 0) return;
    int w, h;
    current_target_size(&w, &h);
    float v[4] = { (float)w, (float)h, -1.0f, (float)h };
    rlEnableShader(s->sh.id);
    rlSetUniform(s->screen_size_loc, v, RL_SHADER_UNIFORM_VEC4, 1);
}
void gfx_set_shader(GfxShader *s) {
    if (s == cur_shader) return;
    rlDrawRenderBatchActive();
    cur_shader = s;
    if (s) {
        apply_screen_size(s);
        rlSetShader(s->sh.id, s->sh.locs);
    } else {
        rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
    }
}
GfxShader *gfx_get_shader(void) { return cur_shader; }

/* ------------------------------------------------------------------ */
/* Canvases                                                            */
/* ------------------------------------------------------------------ */
struct GfxCanvas { RenderTexture2D rt; };
GfxCanvas *gfx_new_canvas(int w, int h) {
    GfxCanvas *c = NEW(GfxCanvas);
    c->rt = LoadRenderTexture(w > 0 ? w : 1, h > 0 ? h : 1);
    SetTextureFilter(c->rt.texture, TEXTURE_FILTER_BILINEAR);
    return c;
}
void gfx_free_canvas(GfxCanvas *c) {
    if (!c) return;
    if (cur_canvas == c) gfx_set_canvas(NULL);
    UnloadRenderTexture(c->rt);
    free(c);
}
int gfx_canvas_height(GfxCanvas *c) { return c ? c->rt.texture.height : 0; }
int gfx_canvas_width(GfxCanvas *c) { return c ? c->rt.texture.width : 0; }
static void current_target_size(int *w, int *h) {
    if (cur_canvas) { *w = cur_canvas->rt.texture.width; *h = cur_canvas->rt.texture.height; }
    else { *w = GetScreenWidth(); *h = GetScreenHeight(); }
}
void gfx_set_canvas(GfxCanvas *c) {
    if (c == cur_canvas) return;
    if (cur_canvas) EndTextureMode();
    cur_canvas = c;
    if (c) BeginTextureMode(c->rt);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    if (cur_shader) apply_screen_size(cur_shader);
    if (cur_shader) rlSetShader(cur_shader->sh.id, cur_shader->sh.locs);
}
void gfx_draw_canvas(GfxCanvas *c, float x, float y) {
    if (!c) return;
    float w = (float)c->rt.texture.width, h = (float)c->rt.texture.height;
    /* render textures are stored bottom-up */
    emit_quad(c->rt.texture.id, x, y, x + w, y + h, 0, 1, 1, 0);
}

/* ------------------------------------------------------------------ */
/* Window                                                              */
/* ------------------------------------------------------------------ */
static bool mouse_visible = true;
bool gfx_open_window(int w, int h, const char *title) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(w, h, title);
    if (!IsWindowReady()) return false;
    SetExitKey(KEY_NULL);
    gfx_init();
    return true;
}
void gfx_init(void) {
    rlDisableBackfaceCulling();   /* LOVE never culls; polygon winding varies */
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
}
void gfx_close_window(void) { CloseWindow(); }
bool gfx_window_should_close(void) { return WindowShouldClose(); }
int gfx_width(void) { return GetScreenWidth(); }
int gfx_height(void) { return GetScreenHeight(); }
void gfx_begin_frame(void) {
    BeginDrawing();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    gfx_origin();
}
void gfx_end_frame(void) {
    gfx_set_shader(NULL);
    if (cur_canvas) gfx_set_canvas(NULL);
    EndBlendMode();
    EndDrawing();
}
static int windowed_w = 0, windowed_h = 0;
void gfx_set_mode(int w, int h, bool fullscreen, bool borderless, int monitor, bool vsync) {
    if (monitor < 0 || monitor >= GetMonitorCount()) monitor = 0;
    if (vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
    bool is_full = IsWindowFullscreen();
    bool is_border = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (!fullscreen && is_full) ToggleFullscreen();
    if (!borderless && is_border) ToggleBorderlessWindowed();
    if (fullscreen) {
        SetWindowMonitor(monitor);
        if (!IsWindowFullscreen()) {
            SetWindowSize(w > 0 ? w : GetMonitorWidth(monitor), h > 0 ? h : GetMonitorHeight(monitor));
            ToggleFullscreen();
        }
    } else if (borderless) {
        SetWindowMonitor(monitor);
        if (!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
    } else {
        if (w <= 0 || h <= 0) {
            w = windowed_w ? windowed_w : GetMonitorWidth(monitor) * 3 / 4;
            h = windowed_h ? windowed_h : GetMonitorHeight(monitor) * 3 / 4;
        }
        windowed_w = w; windowed_h = h;
        SetWindowSize(w, h);
        Vector2 mp = GetMonitorPosition(monitor);
        SetWindowPosition((int)mp.x + (GetMonitorWidth(monitor) - w) / 2, (int)mp.y + (GetMonitorHeight(monitor) - h) / 2);
    }
}
int gfx_monitor_count(void) { return GetMonitorCount(); }
const char *gfx_monitor_name(int i) { return GetMonitorName(i); }
void gfx_monitor_size(int i, int *w, int *h) { *w = GetMonitorWidth(i); *h = GetMonitorHeight(i); }
int gfx_current_monitor(void) { return GetCurrentMonitor(); }
void gfx_set_mouse_visible(bool v) {
    if (v == mouse_visible) return;
    mouse_visible = v;
    if (v) ShowCursor(); else HideCursor();
}
void gfx_set_window_title(const char *t) { SetWindowTitle(t); }
double gfx_time(void) { return GetTime(); }
void gfx_sleep(double s) { if (s > 0) WaitTime(s); }

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
typedef struct { int rl; const char *love; } KeyMap;
static const KeyMap KEYMAP[] = {
    { KEY_APOSTROPHE, "'" }, { KEY_COMMA, "," }, { KEY_MINUS, "-" }, { KEY_PERIOD, "." }, { KEY_SLASH, "/" },
    { KEY_ZERO, "0" }, { KEY_ONE, "1" }, { KEY_TWO, "2" }, { KEY_THREE, "3" }, { KEY_FOUR, "4" },
    { KEY_FIVE, "5" }, { KEY_SIX, "6" }, { KEY_SEVEN, "7" }, { KEY_EIGHT, "8" }, { KEY_NINE, "9" },
    { KEY_SEMICOLON, ";" }, { KEY_EQUAL, "=" },
    { KEY_A, "a" }, { KEY_B, "b" }, { KEY_C, "c" }, { KEY_D, "d" }, { KEY_E, "e" }, { KEY_F, "f" },
    { KEY_G, "g" }, { KEY_H, "h" }, { KEY_I, "i" }, { KEY_J, "j" }, { KEY_K, "k" }, { KEY_L, "l" },
    { KEY_M, "m" }, { KEY_N, "n" }, { KEY_O, "o" }, { KEY_P, "p" }, { KEY_Q, "q" }, { KEY_R, "r" },
    { KEY_S, "s" }, { KEY_T, "t" }, { KEY_U, "u" }, { KEY_V, "v" }, { KEY_W, "w" }, { KEY_X, "x" },
    { KEY_Y, "y" }, { KEY_Z, "z" },
    { KEY_LEFT_BRACKET, "[" }, { KEY_BACKSLASH, "\\" }, { KEY_RIGHT_BRACKET, "]" }, { KEY_GRAVE, "`" },
    { KEY_SPACE, "space" }, { KEY_ESCAPE, "escape" }, { KEY_ENTER, "return" }, { KEY_TAB, "tab" },
    { KEY_BACKSPACE, "backspace" }, { KEY_INSERT, "insert" }, { KEY_DELETE, "delete" },
    { KEY_RIGHT, "right" }, { KEY_LEFT, "left" }, { KEY_DOWN, "down" }, { KEY_UP, "up" },
    { KEY_PAGE_UP, "pageup" }, { KEY_PAGE_DOWN, "pagedown" }, { KEY_HOME, "home" }, { KEY_END, "end" },
    { KEY_CAPS_LOCK, "capslock" }, { KEY_SCROLL_LOCK, "scrolllock" }, { KEY_NUM_LOCK, "numlock" },
    { KEY_PRINT_SCREEN, "printscreen" }, { KEY_PAUSE, "pause" },
    { KEY_F1, "f1" }, { KEY_F2, "f2" }, { KEY_F3, "f3" }, { KEY_F4, "f4" }, { KEY_F5, "f5" }, { KEY_F6, "f6" },
    { KEY_F7, "f7" }, { KEY_F8, "f8" }, { KEY_F9, "f9" }, { KEY_F10, "f10" }, { KEY_F11, "f11" }, { KEY_F12, "f12" },
    { KEY_LEFT_SHIFT, "lshift" }, { KEY_LEFT_CONTROL, "lctrl" }, { KEY_LEFT_ALT, "lalt" }, { KEY_LEFT_SUPER, "lgui" },
    { KEY_RIGHT_SHIFT, "rshift" }, { KEY_RIGHT_CONTROL, "rctrl" }, { KEY_RIGHT_ALT, "ralt" }, { KEY_RIGHT_SUPER, "rgui" },
    { KEY_KP_0, "kp0" }, { KEY_KP_1, "kp1" }, { KEY_KP_2, "kp2" }, { KEY_KP_3, "kp3" }, { KEY_KP_4, "kp4" },
    { KEY_KP_5, "kp5" }, { KEY_KP_6, "kp6" }, { KEY_KP_7, "kp7" }, { KEY_KP_8, "kp8" }, { KEY_KP_9, "kp9" },
    { KEY_KP_DECIMAL, "kp." }, { KEY_KP_DIVIDE, "kp/" }, { KEY_KP_MULTIPLY, "kp*" }, { KEY_KP_SUBTRACT, "kp-" },
    { KEY_KP_ADD, "kp+" }, { KEY_KP_ENTER, "kpenter" }, { KEY_KP_EQUAL, "kp=" },
};
static bool key_state[ARRLEN(KEYMAP)];

typedef struct { int rl; const char *love; } PadMap;
static const PadMap PADMAP[] = {
    { GAMEPAD_BUTTON_RIGHT_FACE_DOWN, "a" }, { GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, "b" },
    { GAMEPAD_BUTTON_RIGHT_FACE_LEFT, "x" }, { GAMEPAD_BUTTON_RIGHT_FACE_UP, "y" },
    { GAMEPAD_BUTTON_MIDDLE_LEFT, "back" }, { GAMEPAD_BUTTON_MIDDLE, "guide" }, { GAMEPAD_BUTTON_MIDDLE_RIGHT, "start" },
    { GAMEPAD_BUTTON_LEFT_THUMB, "leftstick" }, { GAMEPAD_BUTTON_RIGHT_THUMB, "rightstick" },
    { GAMEPAD_BUTTON_LEFT_TRIGGER_1, "leftshoulder" }, { GAMEPAD_BUTTON_RIGHT_TRIGGER_1, "rightshoulder" },
    { GAMEPAD_BUTTON_LEFT_FACE_UP, "dpup" }, { GAMEPAD_BUTTON_LEFT_FACE_DOWN, "dpdown" },
    { GAMEPAD_BUTTON_LEFT_FACE_LEFT, "dpleft" }, { GAMEPAD_BUTTON_LEFT_FACE_RIGHT, "dpright" },
};
#define MAX_PADS 4
static bool pad_state[MAX_PADS][ARRLEN(PADMAP)];
static float pad_axis_prev[MAX_PADS][6];
static double last_mx = -1, last_my = -1;
static int last_w, last_h;

int gfx_poll_input(InputEvent *out, int max) {
    int n = 0;
#define PUSH(ev) do { if (n < max) out[n++] = (ev); } while (0)
    if (WindowShouldClose()) { InputEvent e = { .type = INPUT_QUIT }; PUSH(e); }
    if (last_w != GetScreenWidth() || last_h != GetScreenHeight()) {
        last_w = GetScreenWidth(); last_h = GetScreenHeight();
        InputEvent e = { .type = INPUT_RESIZE, .x = last_w, .y = last_h }; PUSH(e);
    }
    for (int i = 0; i < ARRLEN(KEYMAP); i++) {
        bool down = IsKeyDown(KEYMAP[i].rl);
        if (down != key_state[i]) {
            key_state[i] = down;
            InputEvent e = { .type = down ? INPUT_KEY_PRESSED : INPUT_KEY_RELEASED };
            snprintf(e.key, sizeof e.key, "%s", KEYMAP[i].love);
            PUSH(e);
        } else if (down && IsKeyPressedRepeat(KEYMAP[i].rl)) {
            InputEvent e = { .type = INPUT_KEY_PRESSED };
            snprintf(e.key, sizeof e.key, "%s", KEYMAP[i].love);
            PUSH(e);
        }
    }
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        InputEvent e = { .type = INPUT_TEXT };
        char buf[5] = { 0 };
        if (ch < 0x80) buf[0] = (char)ch;
        else if (ch < 0x800) { buf[0] = (char)(0xC0 | (ch >> 6)); buf[1] = (char)(0x80 | (ch & 0x3F)); }
        else { buf[0] = (char)(0xE0 | (ch >> 12)); buf[1] = (char)(0x80 | ((ch >> 6) & 0x3F)); buf[2] = (char)(0x80 | (ch & 0x3F)); }
        snprintf(e.key, sizeof e.key, "%s", buf);
        PUSH(e);
    }
    Vector2 mp = GetMousePosition();
    if (mp.x != last_mx || mp.y != last_my) {
        InputEvent e = { .type = INPUT_MOUSE_MOVED, .x = mp.x, .y = mp.y };
        last_mx = mp.x; last_my = mp.y;
        PUSH(e);
    }
    for (int b = 0; b < 3; b++) {
        int rb = b == 0 ? MOUSE_BUTTON_LEFT : b == 1 ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_MIDDLE;
        if (IsMouseButtonPressed(rb)) { InputEvent e = { .type = INPUT_MOUSE_PRESSED, .x = mp.x, .y = mp.y, .button = b + 1 }; PUSH(e); }
        if (IsMouseButtonReleased(rb)) { InputEvent e = { .type = INPUT_MOUSE_RELEASED, .x = mp.x, .y = mp.y, .button = b + 1 }; PUSH(e); }
    }
    for (int g = 0; g < MAX_PADS; g++) {
        if (!IsGamepadAvailable(g)) continue;
        for (int i = 0; i < ARRLEN(PADMAP); i++) {
            bool down = IsGamepadButtonDown(g, PADMAP[i].rl);
            if (down != pad_state[g][i]) {
                pad_state[g][i] = down;
                InputEvent e = { .type = down ? INPUT_GAMEPAD_PRESSED : INPUT_GAMEPAD_RELEASED, .gamepad = g };
                snprintf(e.key, sizeof e.key, "%s", PADMAP[i].love);
                PUSH(e);
            }
        }
        for (int a = 0; a < 6 && a < GetGamepadAxisCount(g); a++) {
            float v = GetGamepadAxisMovement(g, a);
            if (fabsf(v - pad_axis_prev[g][a]) > 0.01f) {
                pad_axis_prev[g][a] = v;
                InputEvent e = { .type = INPUT_GAMEPAD_AXIS, .gamepad = g, .value = v, .button = a };
                PUSH(e);
            }
        }
    }
#undef PUSH
    return n;
}
static bool mouse_override; static double mouse_ox, mouse_oy;
void gfx_override_mouse(bool on, double x, double y) { mouse_override = on; mouse_ox = x; mouse_oy = y; }
void gfx_mouse_pos(double *x, double *y) {
    if (mouse_override) { *x = mouse_ox; *y = mouse_oy; return; }
    Vector2 p = GetMousePosition(); *x = p.x; *y = p.y;
}
float gfx_gamepad_axis(int g, const char *axis) {
    int a = -1;
    if (!strcmp(axis, "leftx")) a = GAMEPAD_AXIS_LEFT_X;
    else if (!strcmp(axis, "lefty")) a = GAMEPAD_AXIS_LEFT_Y;
    else if (!strcmp(axis, "rightx")) a = GAMEPAD_AXIS_RIGHT_X;
    else if (!strcmp(axis, "righty")) a = GAMEPAD_AXIS_RIGHT_Y;
    else if (!strcmp(axis, "triggerleft")) a = GAMEPAD_AXIS_LEFT_TRIGGER;
    else if (!strcmp(axis, "triggerright")) a = GAMEPAD_AXIS_RIGHT_TRIGGER;
    if (a < 0 || !IsGamepadAvailable(g)) return 0;
    float v = GetGamepadAxisMovement(g, a);
    if (a == GAMEPAD_AXIS_LEFT_TRIGGER || a == GAMEPAD_AXIS_RIGHT_TRIGGER) v = (v + 1) * 0.5f; /* LOVE triggers are 0..1 */
    return v;
}
bool gfx_gamepad_available(int g) { return IsGamepadAvailable(g); }
const char *gfx_gamepad_name(int g) { return IsGamepadAvailable(g) ? GetGamepadName(g) : ""; }
bool gfx_key_down(const char *k) {
    for (int i = 0; i < ARRLEN(KEYMAP); i++) if (!strcmp(KEYMAP[i].love, k)) return IsKeyDown(KEYMAP[i].rl);
    return false;
}
void gfx_set_clipboard(const char *s) { SetClipboardText(s); }
const char *gfx_get_clipboard(void) { const char *c = GetClipboardText(); return c ? c : ""; }
void gfx_open_url(const char *url) { OpenURL(url); }

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    char *path;
    Sound base;
    bool loaded;
} SoundCache;
static SoundCache *scache;
static int nscache, capscache;

struct GfxSource {
    bool stream;
    Music music;
    Sound sound;
    bool alias;
    bool started;
    struct GfxSource *next_stream;
};
static GfxSource *streams;
static bool audio_ok;

void audio_init(void) {
    InitAudioDevice();
    audio_ok = IsAudioDeviceReady();
    if (!audio_ok) fprintf(stderr, "audio: no audio device\n");
}
static Sound *cached_sound(const char *path) {
    for (int i = 0; i < nscache; i++) if (!strcmp(scache[i].path, path)) return scache[i].loaded ? &scache[i].base : NULL;
    if (nscache == capscache) { capscache = capscache ? capscache * 2 : 64; scache = xrealloc(scache, (size_t)capscache * sizeof(SoundCache)); }
    SoundCache *c = &scache[nscache++];
    c->path = xstrdup(path);
    c->base = LoadSound(path);
    c->loaded = c->base.frameCount > 0;
    return c->loaded ? &c->base : NULL;
}
GfxSource *audio_new_source(const char *path, bool stream) {
    if (!audio_ok) return NULL;
    GfxSource *s = NEW(GfxSource);
    s->stream = stream;
    if (stream) {
        s->music = LoadMusicStream(path);
        s->music.looping = false;
        s->next_stream = streams;
        streams = s;
    } else {
        Sound *b = cached_sound(path);
        if (!b) { free(s); return NULL; }
        s->sound = LoadSoundAlias(*b);
        s->alias = true;
    }
    return s;
}
void audio_play(GfxSource *s) {
    if (!s) return;
    if (s->stream) { PlayMusicStream(s->music); s->started = true; }
    else PlaySound(s->sound);
}
void audio_stop(GfxSource *s) {
    if (!s) return;
    if (s->stream) { StopMusicStream(s->music); s->started = false; }
    else StopSound(s->sound);
}
bool audio_is_playing(GfxSource *s) {
    if (!s) return false;
    if (s->stream) return s->started && IsMusicStreamPlaying(s->music);
    return IsSoundPlaying(s->sound);
}
void audio_set_volume(GfxSource *s, float v) {
    if (!s) return;
    if (v < 0) v = 0;
    if (s->stream) SetMusicVolume(s->music, v); else SetSoundVolume(s->sound, v);
}
void audio_set_pitch(GfxSource *s, float p) {
    if (!s) return;
    if (p <= 0.001f) p = 0.001f;
    if (s->stream) SetMusicPitch(s->music, p); else SetSoundPitch(s->sound, p);
}
void audio_release(GfxSource *s) {
    if (!s) return;
    if (s->stream) {
        StopMusicStream(s->music);
        UnloadMusicStream(s->music);
        GfxSource **pp = &streams;
        while (*pp) { if (*pp == s) { *pp = s->next_stream; break; } pp = &(*pp)->next_stream; }
    } else {
        StopSound(s->sound);
        UnloadSoundAlias(s->sound);
    }
    free(s);
}
void audio_update(void) {
    for (GfxSource *s = streams; s; s = s->next_stream) {
        if (s->started) {
            UpdateMusicStream(s->music);
            /* a non-looping stream that reached its end */
            if (!IsMusicStreamPlaying(s->music)) s->started = false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Deflate (LOVE's love.data.compress 'deflate' = raw deflate)         */
/* ------------------------------------------------------------------ */
#include "../core/dval.h"
char *deflate_compress(const char *data, int len, int *out_len) {
    int n = 0;
    unsigned char *c = CompressData((const unsigned char *)data, len, &n);
    if (!c) return NULL;
    char *r = xmalloc((size_t)n);
    memcpy(r, c, (size_t)n);
    MemFree(c);
    *out_len = n;
    return r;
}
char *deflate_decompress(const char *data, int len, int *out_len) {
    int n = 0;
    unsigned char *d = DecompressData((const unsigned char *)data, len, &n);
    if (!d || n <= 0) return NULL;
    char *r = xmalloc((size_t)n + 1);
    memcpy(r, d, (size_t)n);
    r[n] = 0;
    MemFree(d);
    *out_len = n;
    return r;
}

void gfx_screenshot(const char *path) {
    Image img = LoadImageFromScreen();
    ExportImage(img, path);
    UnloadImage(img);
}
