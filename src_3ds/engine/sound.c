/* sound.c - port of engine/sound_manager.lua plus play_sound/modulate_sound
 * from misc_functions.lua. The original ran the mixer logic on a separate
 * thread; here it runs on the main thread, which is plenty fast. */
#include "../game/game.h"
#include <dirent.h>

typedef struct {
    GfxSource *src;
    char *code;
    double original_pitch, original_volume, temp_pitch, current_volume;
    bool has_temp_pitch, has_current_volume;
    bool created_on_pause;
    int created_on_state;
    bool is_music, is_stream;
} SoundInst;

typedef struct {
    char *code;
    VEC(SoundInst *) inst;
    bool is_music;
} SoundGroup;

static VEC(SoundGroup) groups;

typedef struct {
    const char *desired_track;
    double pitch_mod;
    double dt;
    double splash_vol;
} SoundArgs;
static SoundArgs cur_args = { "", 1, 0, 1 };

static SoundGroup *group_get(const char *code, bool create) {
    for (int i = 0; i < groups.count; i++) if (!strcmp(groups.items[i].code, code)) return &groups.items[i];
    if (!create) return NULL;
    SoundGroup g;
    memset(&g, 0, sizeof g);
    g.code = xstrdup(code);
    g.is_music = strstr(code, "music") != NULL;
    vec_push(&groups, g);
    return &vec_last(&groups);
}

void sound_init(void) {
    audio_init();
    DIR *d = opendir(res_path("resources/sounds"));
    if (!d) { fprintf(stderr, "sound: no resources/sounds directory\n"); return; }
    struct dirent *de;
    while ((de = readdir(d))) {
        size_t n = strlen(de->d_name);
        if (n > 4 && !strcmp(de->d_name + n - 4, ".ogg")) {
            char code[256];
            snprintf(code, sizeof code, "%.*s", (int)(n - 4), de->d_name);
            group_get(code, true);
        }
    }
    closedir(d);
}

static void set_sfx(SoundInst *s) {
    double vol_master = G.SETTINGS.SOUND.volume / 100.0;
    if (s->is_music) {
        if (!strcmp(s->code, cur_args.desired_track)) {
            if (!s->has_current_volume) { s->current_volume = 1; s->has_current_volume = true; }
            s->current_volume = 1 * (cur_args.dt * 3) + (1 - cur_args.dt * 3) * s->current_volume;
        } else {
            if (!s->has_current_volume) { s->current_volume = 0; s->has_current_volume = true; }
            s->current_volume = 0 * (cur_args.dt * 3) + (1 - cur_args.dt * 3) * s->current_volume;
        }
        audio_set_volume(s->src, (float)(s->current_volume * s->original_volume * vol_master * (G.SETTINGS.SOUND.music_volume / 100.0)));
        audio_set_pitch(s->src, (float)(s->original_pitch * cur_args.pitch_mod));
    } else {
        if (!s->has_temp_pitch || s->temp_pitch != s->original_pitch) {
            audio_set_pitch(s->src, (float)s->original_pitch);
            s->temp_pitch = s->original_pitch;
            s->has_temp_pitch = true;
        }
        double v = s->original_volume * vol_master * (G.SETTINGS.SOUND.game_sounds_volume / 100.0);
        if (s->created_on_state == STATE_SPLASH) v = v * cur_args.splash_vol;
        if (v <= 0) audio_stop(s->src);
        else audio_set_volume(s->src, (float)v);
    }
}

static SoundInst *play_inst(const char *code, double per, double vol, int state) {
    SoundGroup *g = group_get(code, true);
    bool stream = strstr(code, "music") || strstr(code, "ambient");
    GfxSource *src = audio_new_source(res_path(tfmt("resources/sounds/%s.ogg", code)), stream);
    if (!src) return NULL;
    SoundInst *s = NEW(SoundInst);
    s->src = src;
    s->code = g->code;
    s->is_music = g->is_music;
    s->is_stream = stream;
    s->original_pitch = per;
    s->original_volume = vol;
    s->created_on_pause = G.OVERLAY_MENU != NULL;
    s->created_on_state = state;
    vec_push(&g->inst, s);
    set_sfx(s);
    audio_play(src);
    return s;
}

static void free_inst(SoundInst *s) {
    audio_release(s->src);
    free(s);
}

void sound_play(const char *code, double per, double vol) {
    if (G.F_MUTE || !code) return;
    if (G.SETTINGS.SOUND.volume <= 0.001) return;
    if (isnan(per)) per = 1;
    if (isnan(vol)) vol = 1;
    play_inst(code, per, vol, G.STATE);
}

void sound_stop_all(void) {
    for (int i = 0; i < groups.count; i++)
        for (int j = 0; j < groups.items[i].inst.count; j++) audio_stop(groups.items[i].inst.items[j]->src);
}

void sound_restart_music(void) {
    for (int i = 0; i < groups.count; i++) {
        SoundGroup *g = &groups.items[i];
        if (!g->is_music) continue;
        for (int j = 0; j < g->inst.count; j++) free_inst(g->inst.items[j]);
        vec_clear(&g->inst);
        play_inst(g->code, 0.7, 0.6, G.STATE);
    }
}

void sound_reset_states(int state) {
    for (int i = 0; i < groups.count; i++)
        for (int j = 0; j < groups.items[i].inst.count; j++) groups.items[i].inst.items[j]->created_on_state = state;
}

typedef struct { const char *name; double vol, per; bool has; } Ambient;
static Ambient ambients[4] = {
    { "ambientFire2", 0, 1.05, false },
    { "ambientFire1", 0, 1.1, false },
    { "ambientFire3", 0, 1, false },
    { "ambientOrgan1", 0, 0.7, false },
};

void sound_modulate(double dt) {
    G.SPLASH_VOL = 2 * dt * (G.STATE == STATE_SPLASH ? 1 : 0) + G.SPLASH_VOL * (1 - 2 * dt);
    const char *desired =
        G.video_soundtrack ? G.video_soundtrack :
        (G.STATE == STATE_SPLASH) ? "" :
        (G.booster_pack_sparkles && !G.booster_pack_sparkles->n.REMOVED) ? "music2" :
        (G.booster_pack_meteors && !G.booster_pack_meteors->n.REMOVED) ? "music3" :
        (G.booster_pack && !G.booster_pack->n.REMOVED) ? "music2" :
        (G.shop && !G.shop->n.REMOVED) ? "music4" :
        (G.GAME.blind && G.GAME.blind->boss) ? "music5" : "music1";
    G.PITCH_MOD = G.PITCH_MOD * (1 - dt) + dt * ((!G.normal_music_speed && G.STATE == STATE_GAME_OVER) ? 0.5 : 1);

    double earned = G.GAME.current_round.current_hand.chips * G.GAME.current_round.current_hand.mult;
    double required = G.GAME.blind ? G.GAME.blind->chips : 0;
    double flames = MIN(1, (G.STAGE == STAGE_RUN ? 1 : 0) * (G.ARGS_chip_flames.real_intensity + G.ARGS_chip_flames.change) / 10);
    double organ = required > 0 ? MAX(MIN(0.4, 0.1 * (log(earned / (required + 1)) / log(5))), 0.) : 0;
    if (isnan(organ)) organ = 0;
    G.ARGS_score_intensity_flames = flames;
    G.ARGS_score_intensity.earned_score = earned;
    G.ARGS_score_intensity.required_score = required;
    G.ARGS_score_intensity.flames = flames;
    for (int i = 0; i < 4; i++) {
        Ambient *a = &ambients[i];
        double pv = a->vol;
        double nv;
        switch (i) {
        case 0: nv = pv * (1 - dt) + dt * 0.9 * ((flames > 0.3) ? 1 : flames / 0.3); break;
        case 1: nv = pv * (1 - dt) + dt * 0.8 * ((flames > 0.3) ? (flames - 0.3) / 0.7 : 0); break;
        case 2: nv = pv * (1 - dt) + dt * 0.4 * (G.ARGS_chip_flames.change + G.ARGS_mult_flames.change); break;
        default: nv = pv * (1 - dt) + dt * 0.6 * (G.SETTINGS.SOUND.music_volume + 100) / 200 * organ; break;
        }
        if (!a->has) { a->has = true; nv = 0; }
        if (G.STATE == STATE_SPLASH) nv = 0;
        a->vol = nv;
    }

    cur_args.desired_track = desired;
    cur_args.pitch_mod = G.PITCH_MOD;
    cur_args.dt = dt;
    cur_args.splash_vol = G.SPLASH_VOL;

    /* MODULATE */
    if (desired[0]) {
        for (int i = 0; i < groups.count; i++) {
            SoundGroup *g = &groups.items[i];
            if (!g->is_music) continue;
            if (!(g->inst.count > 0 && audio_is_playing(g->inst.items[0]->src))) { sound_restart_music(); break; }
        }
    }
    for (int i = 0; i < groups.count; i++) {
        SoundGroup *g = &groups.items[i];
        int j = 0;
        while (j < g->inst.count) {
            if (!audio_is_playing(g->inst.items[j]->src)) {
                free_inst(g->inst.items[j]);
                vec_remove_at(&g->inst, j);
            } else j++;
        }
        for (j = 0; j < g->inst.count; j++) set_sfx(g->inst.items[j]);
    }
    /* AMBIENT */
    double gv = (G.SETTINGS.SOUND.volume / 100.0) * (G.SETTINGS.SOUND.game_sounds_volume / 100.0);
    for (int i = 0; i < 4; i++) {
        Ambient *a = &ambients[i];
        SoundGroup *g = group_get(a->name, false);
        if (!g) continue;
        bool start = a->vol * gv > 0;
        for (int j = 0; j < g->inst.count; j++) {
            SoundInst *s = g->inst.items[j];
            if (audio_is_playing(s->src)) {
                s->original_volume = a->vol;
                set_sfx(s);
                start = false;
            }
        }
        if (start) play_inst(a->name, a->per, a->vol, G.STATE);
    }
    audio_update();
}
