/* gfx.h - the subset of love.graphics / love.window / love.audio the game
 * uses. In this 3DS build it is implemented on libctru + citro3d (graphics,
 * input) and ndsp (audio) in gfx_3ds.c / audio_3ds.c.
 *
 * LOVE was the original engine; it cannot be used from C, so this layer
 * re-implements the behaviour the Lua code relied on: a transform stack,
 * images drawn through quads, rectangles/polygons/lines, text, canvases and
 * GLSL shaders written in LOVE's shader dialect. */
#ifndef GFX_H
#define GFX_H
#include "../core/core.h"

typedef float Colour[4];

/* ---------------- images & quads ---------------- */
typedef struct GfxTexSet GfxTexSet;
typedef struct GfxImage {
    GfxTexSet *tex;      /* GPU tiles; created on first draw */
    bool load_failed;
    int pw, ph;          /* pixel size of the source image */
    float w, h;          /* logical size (pixel size / dpiscale) */
    char *path;
} GfxImage;

typedef struct {
    float x, y, w, h;    /* viewport in logical image coordinates */
    float sw, sh;        /* reference image dimensions */
} GfxQuad;

GfxImage *gfx_load_image(const char *path, float dpiscale, bool mipmaps, bool linear);
void gfx_free_image(GfxImage *img);
GfxQuad gfx_quad(float x, float y, float w, float h, float sw, float sh);

/* ---------------- fonts ---------------- */
typedef struct GfxFont GfxFont;
GfxFont *gfx_load_font(const char *path, float size);
float gfx_font_width(GfxFont *f, const char *text);     /* love Font:getWidth */
float gfx_font_height(GfxFont *f);                      /* love Font:getHeight */
float gfx_font_char_width(GfxFont *f, int codepoint);

/* ---------------- shaders ---------------- */
typedef struct GfxShader GfxShader;
GfxShader *gfx_load_shader(const char *path);
void gfx_set_shader(GfxShader *s);       /* NULL = default */
GfxShader *gfx_get_shader(void);
void gfx_send_float(GfxShader *s, const char *name, float v);
void gfx_send_vec2(GfxShader *s, const char *name, float x, float y);
void gfx_send_vec4(GfxShader *s, const char *name, const float *v);
void gfx_send_bool(GfxShader *s, const char *name, bool v);
bool gfx_shader_has(GfxShader *s, const char *name);

/* ---------------- canvases ---------------- */
typedef struct GfxCanvas GfxCanvas;
GfxCanvas *gfx_new_canvas(int w, int h);
void gfx_free_canvas(GfxCanvas *c);
void gfx_set_canvas(GfxCanvas *c);       /* NULL = screen */
void gfx_draw_canvas(GfxCanvas *c, float x, float y);
int gfx_canvas_height(GfxCanvas *c);
int gfx_canvas_width(GfxCanvas *c);

/* ---------------- drawing ---------------- */
enum { GFX_FILL = 0, GFX_LINE = 1 };
void gfx_init(void);
void gfx_origin(void);
void gfx_push(void);
void gfx_pop(void);
void gfx_translate(double x, double y);
void gfx_rotate(double angle);
void gfx_scale(double sx, double sy);
void gfx_set_color(const float *c);
void gfx_set_color4(float r, float g, float b, float a);
void gfx_set_line_width(float w);
void gfx_clear(float r, float g, float b, float a);
void gfx_draw_image(GfxImage *img, const GfxQuad *q, double x, double y, double r, double sx, double sy, double ox, double oy);
void gfx_rectangle(int mode, double x, double y, double w, double h);
void gfx_polygon(int mode, const float *verts, int nfloats);
/* love.graphics.draw(Text/print) - draws a single line of text */
void gfx_print(GfxFont *f, const char *text, double x, double y, double r, double sx, double sy, double ox, double oy);
void gfx_flush(void);
/* 3DS: which screen(s) following draws appear on */
enum { GFX_SCREEN_TOP = 1, GFX_SCREEN_BOTTOM = 2, GFX_SCREEN_BOTH = 3 };
void gfx_set_screen_mask(int mask);
int gfx_get_screen_mask(void);

/* ---------------- window ---------------- */
bool gfx_open_window(int w, int h, const char *title);
void gfx_close_window(void);
bool gfx_window_should_close(void);
int gfx_width(void);
int gfx_height(void);
void gfx_begin_frame(void);
void gfx_end_frame(void);
void gfx_set_mode(int w, int h, bool fullscreen, bool borderless, int monitor, bool vsync);
int gfx_monitor_count(void);
const char *gfx_monitor_name(int i);
void gfx_monitor_size(int i, int *w, int *h);
int gfx_current_monitor(void);
void gfx_set_mouse_visible(bool v);
void gfx_set_window_title(const char *t);
double gfx_time(void);
void gfx_sleep(double seconds);
void gfx_screenshot(const char *path);  /* debug: save the current back buffer */

/* ---------------- input ---------------- */
typedef enum {
    INPUT_NONE, INPUT_KEY_PRESSED, INPUT_KEY_RELEASED, INPUT_TEXT,
    INPUT_MOUSE_PRESSED, INPUT_MOUSE_RELEASED, INPUT_MOUSE_MOVED,
    INPUT_GAMEPAD_PRESSED, INPUT_GAMEPAD_RELEASED, INPUT_GAMEPAD_AXIS,
    INPUT_RESIZE, INPUT_QUIT,
} InputType;
typedef struct {
    InputType type;
    char key[32];        /* LOVE key / gamepad button name, or utf8 text */
    double x, y;
    int button;
    int gamepad;
    float value;
    bool touch;          /* mouse event that came from the touch screen */
} InputEvent;
int gfx_poll_input(InputEvent *out, int max);   /* returns number of events */
void gfx_mouse_pos(double *x, double *y);
void gfx_override_mouse(bool on, double x, double y);   /* debug/automation */
float gfx_gamepad_axis(int gamepad, const char *axis);
bool gfx_gamepad_available(int gamepad);
const char *gfx_gamepad_name(int gamepad);
bool gfx_key_down(const char *love_key);
void gfx_set_clipboard(const char *s);
const char *gfx_get_clipboard(void);
void gfx_open_url(const char *url);

/* ---------------- audio ---------------- */
typedef struct GfxSource GfxSource;
void audio_init(void);
GfxSource *audio_new_source(const char *path, bool stream);
void audio_play(GfxSource *s);
void audio_stop(GfxSource *s);
bool audio_is_playing(GfxSource *s);
void audio_set_volume(GfxSource *s, float v);
void audio_set_pitch(GfxSource *s, float p);
void audio_release(GfxSource *s);
void audio_update(void);
void audio_shutdown(void);

#endif
