/* main.c - port of main.lua (love.run / love.load and the input callbacks),
 * 3DS version: resources come from romfs, saves go to sdmc:/3ds/balatro_c. */
#include "game/game.h"
/* only the libctru pieces needed here: the full <3ds.h> clashes with game names */
#include <3ds/types.h>
#include <3ds/os.h>
#include <3ds/romfs.h>
#include <3ds/services/apt.h>
#include <3ds/allocator/linear.h>
#include <malloc.h>

/* libctru gives the main thread a 32KB stack by default; the UI layout code
 * recurses deeply and the desktop build relied on an 8MB stack. */
u32 __stacksize__ = 2 * 1024 * 1024;

/* Debug hooks. The desktop build reads BALATRO_* environment variables;
 * here the same keys are read from sdmc:/3ds/balatro_c/debug.txt as
 * KEY=value lines (SHOTS, QUIT_AFTER, SCRIPT, TEST_SEED, TEST_EASY, FPS).
 * When that file exists, stderr is also written to log.txt next to it. */
static char *debug_cfg;
static const char *dbg_get(const char *key) {
    if (!debug_cfg) return NULL;
    size_t kl = strlen(key);
    for (const char *p = debug_cfg; p && *p;) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            size_t vl = ll - kl - 1;
            char *v = xmalloc(vl + 1);
            memcpy(v, p + kl + 1, vl);
            v[vl] = 0;
            while (vl && (v[vl - 1] == '\r' || v[vl - 1] == ' ')) v[--vl] = 0;
            return v;
        }
        p = eol ? eol + 1 : NULL;
    }
    return NULL;
}

/* debug automation: BALATRO_SCRIPT="t:click=x,y;t:key=name;t:move=x,y" (times in seconds) */
typedef struct { double t; char kind; double x, y; char key[32]; int stage; } ScriptStep;
static ScriptStep script[8192];
static int nscript, script_pos;

static void parse_script(const char *s) {
    while (s && *s && nscript < 8192) {
        ScriptStep *st = &script[nscript];
        memset(st, 0, sizeof *st);
        st->t = atof(s);
        const char *c = strchr(s, ':');
        if (!c) break;
        c++;
        if (!strncmp(c, "click=", 6)) { st->kind = 'c'; sscanf(c + 6, "%lf,%lf", &st->x, &st->y); }
        else if (!strncmp(c, "move=", 5)) { st->kind = 'm'; sscanf(c + 5, "%lf,%lf", &st->x, &st->y); }
        else if (!strncmp(c, "key=", 4) || !strncmp(c, "button=", 7)) {
            st->kind = c[0] == 'k' ? 'k' : 'b';
            c += c[0] == 'k' ? 0 : 3;
            int i = 0;
            for (const char *q = c + 4; *q && *q != ';' && i < 31; q++) st->key[i++] = *q;
        }
        nscript++;
        s = strchr(c, ';');
        if (s) s++;
    }
}

static void handle_event(const InputEvent *ev);
static void run_script(double now) {
    while (script_pos < nscript && now >= script[script_pos].t) {
        ScriptStep *st = &script[script_pos];
        InputEvent e;
        memset(&e, 0, sizeof e);
        if (st->kind == 'm') { gfx_override_mouse(true, st->x, st->y); script_pos++; continue; }
        if (st->kind == 'c') {
            gfx_override_mouse(true, st->x, st->y);
            e.touch = true;
            if (st->stage == 0) { e.type = INPUT_MOUSE_PRESSED; e.x = st->x; e.y = st->y; e.button = 1; handle_event(&e); st->stage = 1; st->t = now + 0.05; return; }
            e.type = INPUT_MOUSE_RELEASED; e.x = st->x; e.y = st->y; e.button = 1; handle_event(&e);
        } else if (st->kind == 'b') {
            /* a 3DS button, by its LOVE gamepad name (a, b, dpup, start...) */
            if (st->stage == 0) { e.type = INPUT_GAMEPAD_PRESSED; snprintf(e.key, sizeof e.key, "%s", st->key); handle_event(&e); st->stage = 1; st->t = now + 0.1; return; }
            e.type = INPUT_GAMEPAD_RELEASED; snprintf(e.key, sizeof e.key, "%s", st->key); handle_event(&e);
        } else if (st->kind == 'k') {
            if (st->stage == 0) { e.type = INPUT_KEY_PRESSED; snprintf(e.key, sizeof e.key, "%s", st->key); handle_event(&e); st->stage = 1; st->t = now + 0.05; return; }
            e.type = INPUT_KEY_RELEASED; snprintf(e.key, sizeof e.key, "%s", st->key); handle_event(&e);
        }
        script_pos++;
    }
}

static void handle_event(const InputEvent *ev) {
    Controller *c = &G.CONTROLLER;
    switch (ev->type) {
    case INPUT_KEY_PRESSED:
        Controller_set_HID_flags(c, "mouse");
        Controller_key_press(c, ev->key);
        break;
    case INPUT_KEY_RELEASED:
        Controller_set_HID_flags(c, "mouse");
        Controller_key_release(c, ev->key);
        break;
    case INPUT_GAMEPAD_PRESSED:
        Controller_set_gamepad(c, ev->gamepad);
        Controller_set_HID_flags(c, "button");
        Controller_button_press(c, ev->key);
        break;
    case INPUT_GAMEPAD_RELEASED:
        Controller_set_gamepad(c, ev->gamepad);
        Controller_set_HID_flags(c, "button");
        Controller_button_release(c, ev->key);
        break;
    case INPUT_GAMEPAD_AXIS:
        if (fabsf(ev->value) > 0.2f && gfx_gamepad_available(ev->gamepad)) {
            Controller_set_gamepad(c, ev->gamepad);
            Controller_set_HID_flags(c, "axis");
        }
        break;
    case INPUT_MOUSE_PRESSED:
        /* love.mousepressed(x, y, button, touch) */
        Controller_set_HID_flags(c, ev->touch ? "touch" : "mouse");
        if (ev->button == 1) Controller_queue_L_cursor_press(c, ev->x, ev->y);
        if (ev->button == 2) Controller_queue_R_cursor_press(c, ev->x, ev->y);
        break;
    case INPUT_MOUSE_RELEASED:
        if (ev->button == 1) Controller_L_cursor_release(c, ev->x, ev->y, true);
        break;
    case INPUT_MOUSE_MOVED:
        Controller_set_HID_flags(c, ev->touch ? "touch" : "mouse");
        break;
    case INPUT_RESIZE:
        game_resize((int)ev->x, (int)ev->y);
        break;
    case INPUT_QUIT:
        G.quit_requested = true;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    heap_set_stack_top(&argc);
    osSetSpeedupEnable(true);   /* New 3DS: 804MHz + L2 cache */
    romfsInit();
    set_resource_root("romfs:");
    debug_cfg = file_read_all(tfmt("%s/debug.txt", save_dir()), NULL);
    if (debug_cfg) freopen(tfmt("%s/log.txt", save_dir()), "w", stderr);
    if (debug_cfg) setvbuf(stderr, NULL, _IONBF, 0);
    {
        extern u32 __ctru_heap_size, __ctru_linear_heap_size;
        fprintf(stderr, "memory: heap %luK, linear heap %luK\n", (unsigned long)(__ctru_heap_size / 1024),
                (unsigned long)(__ctru_linear_heap_size / 1024));
    }
    game_set_globals();
    /* test hook: TEST_SEED fixes the RNG seed (deterministic run seeds) */
    if (dbg_get("TEST_SEED")) G.SEED = atof(dbg_get("TEST_SEED"));
    if (!gfx_open_window(400, 240, "Balatro")) {
        fprintf(stderr, "could not initialise the GPU\n");
        return 1;
    }
    math_randomseed(G.SEED);
    game_start_up();
    gfx_set_mouse_visible(false);

    double dt_smooth = 1.0 / 100;
    double last = gfx_time();
    InputEvent events[256];
    /* debug hooks: BALATRO_SHOTS="t1,t2,..." saves screenshots at those
     * times (seconds), BALATRO_QUIT_AFTER=t exits after t seconds */
    const char *shots = dbg_get("SHOTS");
    const char *quit_after_s = dbg_get("QUIT_AFTER");
    double quit_after = quit_after_s ? atof(quit_after_s) : 0;
    double t0 = gfx_time();
    int shot_idx = 0;
    parse_script(dbg_get("SCRIPT"));
    bool easy = dbg_get("TEST_EASY") != NULL;
    bool show_fps = dbg_get("FPS") != NULL;
    if (dbg_get("GC_EXPLAIN")) heap_explain = atoi(dbg_get("GC_EXPLAIN"));
    bool gc_stress = dbg_get("GC_STRESS") != NULL;
    int gc_last_size = 0;
    double gc_last_time = 0;
    while (!G.quit_requested && aptMainLoop()) {
        double run_start = gfx_time();
        int n = gfx_poll_input(events, ARRLEN(events));
        for (int i = 0; i < n; i++) {
            /* while a script drives the cursor, ignore the real mouse */
            if (nscript && (events[i].type == INPUT_MOUSE_PRESSED || events[i].type == INPUT_MOUSE_RELEASED)) continue;
            handle_event(&events[i]);
        }
        if (nscript) run_script(gfx_time() - t0);
        if (G.quit_requested) break;

        double now = gfx_time();
        double dt = now - last;
        last = now;
        dt_smooth = MIN(0.8 * dt_smooth + 0.2 * dt, 0.1);
        game_update(dt_smooth);
        /* test hook: BALATRO_TEST_EASY=1 makes every blind need 1 chip */
        if (easy && G.STAGE == STAGE_RUN && G.GAME.blind && G.GAME.blind->chips > 1) {
            G.GAME.blind->chips = 1;
            str_set(&G.GAME.blind->chip_text, "1");
        }

        gfx_begin_frame();
        game_draw();
        if (shots) {
            const char *p = shots;
            int i = 0;
            while (*p) {
                double t = atof(p);
                if (i == shot_idx && gfx_time() - t0 >= t) {
                    char name[64];
                    snprintf(name, sizeof name, "%s/shot_%02d.png", save_dir(), shot_idx);
                    gfx_screenshot(name);
                    shot_idx++;
                    break;
                }
                while (*p && *p != ',') p++;
                if (*p == ',') p++;
                i++;
            }
        }
        gfx_end_frame();
        /* reclaim removed nodes (see core/heap.c) once enough have piled up */
        int gsz = node_graveyard_size();
        if (gc_stress || gsz - gc_last_size >= 300 || (gsz - gc_last_size >= 100 && gfx_time() - gc_last_time > 15)) {
            double t = gfx_time();
            int fr = node_collect_graveyard();
            if (show_fps) fprintf(stderr, "gc: freed %d blocks in %.1fms, %d removed nodes still referenced\n", fr, (gfx_time() - t) * 1000, node_graveyard_size());
            gc_last_size = node_graveyard_size();
            gc_last_time = gfx_time();
        }
        if (quit_after > 0 && gfx_time() - t0 > quit_after) G.quit_requested = true;
        audio_update();

        if (show_fps) {
            static int frames; static double since;
            frames++;
            if (gfx_time() - since >= 1) {
                struct mallinfo mi = mallinfo();
                fprintf(stderr, "fps %d  state %d  t=%.1f  events=%d  heap=%dK blocks=%u grave=%d linear_free=%luK\n", frames, G.STATE, gfx_time() - t0,
                        event_queue_length(Q_BASE), mi.uordblks / 1024, (unsigned)heap_tracked_blocks(), node_graveyard_size(), (unsigned long)(linearSpaceFree() / 1024));
                frames = 0; since = gfx_time();
            }
        }
        (void)run_start;   /* frames are paced by vsync in gfx_begin_frame */
    }
    /* flush anything queued before exiting */
    G.FILE_HANDLER.force = true;
    save_manager_update();
    audio_shutdown();
    gfx_close_window();
    romfsExit();
    return 0;
}
