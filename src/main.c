/* main.c - port of main.lua (love.run / love.load and the input callbacks) */
#include "game/game.h"
#include <libgen.h>
#include <unistd.h>

static void set_root_from_exe(const char *argv0) {
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) buf[n] = 0;
    else snprintf(buf, sizeof buf, "%s", argv0);
    set_resource_root(dirname(buf));
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
        else if (!strncmp(c, "key=", 4)) {
            st->kind = 'k';
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
            if (st->stage == 0) { e.type = INPUT_MOUSE_PRESSED; e.x = st->x; e.y = st->y; e.button = 1; handle_event(&e); st->stage = 1; st->t = now + 0.05; return; }
            e.type = INPUT_MOUSE_RELEASED; e.x = st->x; e.y = st->y; e.button = 1; handle_event(&e);
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
        Controller_set_HID_flags(c, "mouse");
        if (ev->button == 1) Controller_queue_L_cursor_press(c, ev->x, ev->y);
        if (ev->button == 2) Controller_queue_R_cursor_press(c, ev->x, ev->y);
        break;
    case INPUT_MOUSE_RELEASED:
        if (ev->button == 1) Controller_L_cursor_release(c, ev->x, ev->y, true);
        break;
    case INPUT_MOUSE_MOVED:
        Controller_set_HID_flags(c, "mouse");
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
    (void)argc;
    set_root_from_exe(argv[0]);
    game_set_globals();
    /* test hook: BALATRO_TEST_SEED fixes the RNG seed (deterministic run seeds) */
    if (getenv("BALATRO_TEST_SEED")) G.SEED = atof(getenv("BALATRO_TEST_SEED"));
    if (!gfx_open_window(G.SETTINGS.WINDOW.screen_res.w, G.SETTINGS.WINDOW.screen_res.h, "Balatro")) {
        fprintf(stderr, "could not open a window\n");
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
    const char *shots = getenv("BALATRO_SHOTS");
    const char *quit_after_s = getenv("BALATRO_QUIT_AFTER");
    double quit_after = quit_after_s ? atof(quit_after_s) : 0;
    double t0 = gfx_time();
    int shot_idx = 0;
    parse_script(getenv("BALATRO_SCRIPT"));
    bool easy = getenv("BALATRO_TEST_EASY") != NULL;
    bool show_fps = getenv("BALATRO_FPS") != NULL;
    while (!G.quit_requested) {
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
                    snprintf(name, sizeof name, "shot_%02d.png", shot_idx);
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
        if (quit_after > 0 && gfx_time() - t0 > quit_after) G.quit_requested = true;
        audio_update();

        if (show_fps) {
            static int frames; static double since;
            frames++;
            if (gfx_time() - since >= 1) {
                fprintf(stderr, "fps %d  state %d  t=%.1f  events=%d\n", frames, G.STATE, gfx_time() - t0, event_queue_length(Q_BASE));
                frames = 0; since = gfx_time();
            }
        }
        double run_time = MIN(gfx_time() - run_start, 0.1);
        if (G.FPS_CAP <= 0) G.FPS_CAP = 500;
        if (run_time < 1. / G.FPS_CAP) gfx_sleep(1. / G.FPS_CAP - run_time);
    }
    /* flush anything queued before exiting */
    G.FILE_HANDLER.force = true;
    save_manager_update();
    gfx_close_window();
    return 0;
}
