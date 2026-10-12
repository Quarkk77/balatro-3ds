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
typedef struct { double t; char kind; double x, y, hold; char key[32]; int stage; } ScriptStep;
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
        if (!strncmp(c, "click=", 6)) { st->kind = 'c'; st->hold = 0.05; sscanf(c + 6, "%lf,%lf,%lf", &st->x, &st->y, &st->hold); }
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

/* ------------------------------------------------------------------ */
/* Old 3DS lockout (this build is New 3DS only for now)                */
/* ------------------------------------------------------------------ */
static void draw_shadowed(GfxFont *f, const char *t, double x, double y, double sc, const float *col, float shadow_a) {
    gfx_set_color4(0, 0, 0, shadow_a);
    gfx_print(f, t, x + 1.5 * sc, y + 1.5 * sc, 0, sc, sc, 0, 0);
    gfx_set_color(col);
    gfx_print(f, t, x, y, 0, sc, sc, 0, 0);
}

/* centred, word-wrapped text block; returns its height */
static double draw_wrapped(GfxFont *f, const char *text, double cx, double cy, double maxw, double sc, const float *col) {
    char lines[16][256];
    int n = 0;
    char cur[256] = "";
    const char *p = text;
    while (*p && n < 16) {
        const char *sp = strchr(p, ' ');
        size_t wl = sp ? (size_t)(sp - p) : strlen(p);
        char trial[256];
        snprintf(trial, sizeof trial, "%s%s%.*s", cur, cur[0] ? " " : "", (int)wl, p);
        if (cur[0] && gfx_font_width(f, trial) * sc > maxw) {
            snprintf(lines[n++], sizeof lines[0], "%s", cur);
            snprintf(cur, sizeof cur, "%.*s", (int)wl, p);
        } else snprintf(cur, sizeof cur, "%s", trial);
        p = sp ? sp + 1 : p + wl;
    }
    if (cur[0] && n < 16) snprintf(lines[n++], sizeof lines[0], "%s", cur);
    double lh = gfx_font_height(f) * sc * 1.15, y = cy - lh * n / 2;
    for (int i = 0; i < n; i++, y += lh)
        draw_shadowed(f, lines[i], cx - gfx_font_width(f, lines[i]) * sc / 2, y, sc, col, 0.4f);
    return lh * n;
}

static void o3ds_lockout(void) {
    GfxFont *f = gfx_load_font(res_path("resources/fonts/m6x11plus.ttf"), 32);
    static const float bg[4] = { 0x37 / 255.f, 0x42 / 255.f, 0x44 / 255.f, 1 };   /* G.C.BLACK */
    static const float nope[4] = { 0xa7 / 255.f, 0x82 / 255.f, 0xd1 / 255.f, 1 };  /* Tarot purple */
    static const float white[4] = { 1, 1, 1, 1 };
    static const float notice_bg[4] = { 0x22 / 255.f, 0x22 / 255.f, 0x22 / 255.f, 0x22 / 255.f };
    const char *unit = "Nope! ";
    const char *msg = "For now, this port of Balatro only supports the New 3DS and not the Old 3DS. Check that your version is "
                      "up to date. If you are running this on a New 3DS, please contact @veridiandeltas on X/Twitter.";
    const char *notice = "NOTICE: Not affiliated with LocalThunk.";
    double t0 = gfx_time();
    InputEvent ev[64];
    bool quit = false;
    while (!quit && aptMainLoop()) {
        int n = gfx_poll_input(ev, ARRLEN(ev));
        for (int i = 0; i < n; i++)
            if (ev[i].type == INPUT_QUIT || (ev[i].type == INPUT_GAMEPAD_PRESSED && !strcmp(ev[i].key, "start"))) quit = true;
        double t = gfx_time() - t0;
        gfx_begin_frame();
        gfx_clear(bg[0], bg[1], bg[2], 1);
        if (f) {
            /* top: "Nope!" tiled on a tilted grid, scrolling diagonally */
            gfx_set_screen_mask(GFX_SCREEN_TOP);
            double sc = 0.75, uw = gfx_font_width(f, unit) * sc, rh = gfx_font_height(f) * sc * 1.25;
            double ox = fmod(t * 30, uw), oy = fmod(t * 18, rh * 2);
            gfx_push();
            gfx_translate(200, 120);
            gfx_rotate(-0.35);
            for (int r = -8; r <= 8; r++) {
                double y = r * rh + oy - rh;
                double stagger = (r & 1) ? uw / 2 : 0;
                for (double x = -330 - uw + ox + stagger; x < 330; x += uw)
                    draw_shadowed(f, "Nope!", x, y, sc, nope, 0.35f);
            }
            gfx_pop();
            /* bottom: the message, and the notice along the bottom edge */
            gfx_set_screen_mask(GFX_SCREEN_BOTTOM);
            draw_wrapped(f, msg, 200, 108, 360, 0.55, white);
            double nsc = 0.45, nw = gfx_font_width(f, notice) * nsc, nh = gfx_font_height(f) * nsc;
            gfx_set_color(notice_bg);
            gfx_rectangle(GFX_FILL, 200 - nw / 2 - 4, 240 - nh - 10, nw + 8, nh + 4);
            draw_shadowed(f, notice, 200 - nw / 2, 240 - nh - 8, nsc, white, 0.4f);
            gfx_set_screen_mask(GFX_SCREEN_BOTH);
        }
        /* debug: SHOTS also works here */
        static int shot_i;
        const char *shots = dbg_get("SHOTS");
        if (shots) {
            const char *q = shots;
            for (int k = 0; k < shot_i && q; k++) { q = strchr(q, ','); if (q) q++; }
            if (q && *q && t >= atof(q)) { gfx_screenshot(tfmt("%s/shot_%02d.png", save_dir(), shot_i)); shot_i++; }
        }
        gfx_end_frame();
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
            if (st->stage == 0) { e.type = INPUT_MOUSE_PRESSED; e.x = st->x; e.y = st->y; e.button = 1; handle_event(&e); st->stage = 1; st->t = now + st->hold; return; }
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
    /* New 3DS only for now: an Old 3DS gets an explanation instead of the game */
    bool is_new3ds = false;
    APT_CheckNew3DS(&is_new3ds);
    if (!is_new3ds || dbg_get("FORCE_O3DS")) {
        o3ds_lockout();
        gfx_close_window();
        romfsExit();
        return 0;
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
    /* test hook: TEST_LOADOUT=1 gives 5 negative random Tarot/Planet cards and
     * 5 random jokers when a run first reaches blind select */
    bool loadout = dbg_get("TEST_LOADOUT") != NULL, loadout_done = false;
    /* test hook: TEST_STRAIGHT=1 turns five cards of the first hand into a
     * 5-9 straight of foil Mult cards */
    bool straight = dbg_get("TEST_STRAIGHT") != NULL, straight_done = false;
    /* test hook: TEST_BIGCARD=1 gives the first card of the first hand a
     * permanent bonus so it is worth about 10^6 chips */
    bool bigcard = dbg_get("TEST_BIGCARD") != NULL || dbg_get("TEST_BIGCARD_EXP") || dbg_get("TEST_NANEINF"), bigcard_done = false;
    /* TEST_BIGCARD_EXP=n: worth 10^n chips instead; TEST_NANEINF=1: 10^308 chips
     * plus a Mult enhancement, so chips x mult overflows like the original's "naneinf" */
    double bigcard_exp = dbg_get("TEST_BIGCARD_EXP") ? atof(dbg_get("TEST_BIGCARD_EXP")) : 6;
    bool naneinf = dbg_get("TEST_NANEINF") != NULL;
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
        if (straight && !straight_done && G.STAGE == STAGE_RUN && G.STATE == STATE_SELECTING_HAND && G.hand && G.hand->cards.count >= 5) {
            straight_done = true;
            static const char *keys[5] = { "H_5", "S_6", "D_7", "C_8", "H_9" };
            for (int i = 0; i < 5; i++) {
                Card *c = G.hand->cards.items[i];
                Card_set_base(c, P_CARD(keys[i]), false);
                Card_set_ability(c, P_CENTER("m_mult"), false, false);
                Card_set_edition(c, edition_req_from_key("foil"), true, true);
            }
        }
        if (bigcard && !bigcard_done && G.STAGE == STAGE_RUN && G.STATE == STATE_SELECTING_HAND && G.hand && G.hand->cards.count > 0) {
            bigcard_done = true;
            Card *c = G.hand->cards.items[0];
            if (naneinf) {
                Card_set_ability(c, P_CENTER("m_mult"), false, false);
                c->ability.perma_bonus = 1e308;
            } else c->ability.perma_bonus = pow(10, bigcard_exp) - c->base.nominal;
        }
        if (loadout && !loadout_done && G.STAGE == STAGE_RUN && G.STATE == STATE_BLIND_SELECT && G.jokers && G.consumeables) {
            loadout_done = true;
            for (int i = 0; i < 5; i++) {
                Card *c = create_card(math_random() < 0.5 ? "Tarot" : "Planet", G.consumeables, false, 0, false, false, NULL, "dbg");
                Card_set_edition(c, edition_req_from_key("negative"), true, true);
                Card_add_to_deck(c, false);
                CardArea_emplace(G.consumeables, c, NULL, false);
            }
            for (int i = 0; i < 5; i++) {
                Card *c = create_card("Joker", G.jokers, false, 0, false, false, NULL, "dbg");
                Card_add_to_deck(c, false);
                CardArea_emplace(G.jokers, c, NULL, false);
            }
        }
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
