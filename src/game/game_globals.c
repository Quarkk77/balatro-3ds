/* game_globals.c - port of globals.lua (Game:set_globals) */
#include "game.h"
#include <time.h>

Globals G;

PtrVec *instance_list(const char *name) {
    if (!name || !strcmp(name, "UIBOX")) return &G.I.UIBOX;
    if (!strcmp(name, "NODE")) return &G.I.NODE;
    if (!strcmp(name, "MOVEABLE")) return &G.I.MOVEABLE;
    if (!strcmp(name, "SPRITE")) return &G.I.SPRITE;
    if (!strcmp(name, "POPUP")) return &G.I.POPUP;
    if (!strcmp(name, "CARD")) return &G.I.CARD;
    if (!strcmp(name, "CARDAREA")) return &G.I.CARDAREA;
    if (!strcmp(name, "ALERT")) return &G.I.ALERT;
    return &G.I.UIBOX;
}

static Col rgba(float r, float g, float b, float a) { return col_new(r, g, b, a); }

void game_set_globals(void) {
    G.VERSION = "1.0.1o-FULL";

    /* feature flags (desktop / Linux values) */
    G.F_QUIT_BUTTON = true;
    G.F_SKIP_TUTORIAL = false;
    G.F_BASIC_CREDITS = false;
    G.F_EXTERNAL_LINKS = true;
    G.F_ENABLE_PERF_OVERLAY = false;
    G.F_NO_SAVING = false;
    G.F_MUTE = false;
    G.F_SOUND_THREAD = false;          /* sounds run on the main thread in this port */
    G.F_VIDEO_SETTINGS = true;
    G.F_CTA = false;
    G.F_VERBOSE = true;
    G.F_HTTP_SCORES = false;
    G.F_RUMBLE = 0;
    G.F_CRASH_REPORTS = false;
    G.F_NO_ERROR_HAND = false;
    G.F_SWAP_AB_PIPS = false;
    G.F_SWAP_AB_BUTTONS = false;
    G.F_SWAP_XY_BUTTONS = false;
    G.F_NO_ACHIEVEMENTS = false;
    G.F_DISP_USERNAME = NULL;
    G.F_ENGLISH_ONLY = false;
    G.F_GUIDE = false;
    G.F_JAN_CTA = false;
    G.F_HIDE_BG = false;
    G.F_TROPHIES = false;
    G.F_PS4_PLAYSTATION_GLYPHS = false;
    G.F_LOCAL_CLIPBOARD = false;
    G.F_SAVE_TIMER = 30;
    G.F_MOBILE_UI = false;
    G.F_HIDE_BETA_LANGS = false;
    G.F_DISCORD = false;

    /* time */
    G.SEED = (double)time(NULL);
    memset(&G.TIMERS, 0, sizeof G.TIMERS);
    G.FRAMES.DRAW = G.FRAMES.MOVE = 0;

    /* settings defaults */
    Settings *S = &G.SETTINGS;
    S->COMP.name = xstrdup(""); S->COMP.prev_name = xstrdup(""); S->COMP.score = 0;
    S->DEMO.total_uptime = 0;
    S->ACHIEVEMENTS_EARNED = dv_table();
    S->crashreports = false;
    S->colourblind_option = false;
    S->language = xstrdup("en-us");
    S->screenshake = -1;                /* `true` in the Lua defaults */
    S->run_stake_stickers = false;
    S->rumble = 0;
    S->play_button_pos = 2;
    S->GAMESPEED = 1;
    S->paused = false;
    S->SOUND.volume = 50; S->SOUND.music_volume = 100; S->SOUND.game_sounds_volume = 100;
    S->WINDOW.screenmode = xstrdup("Windowed");
    S->WINDOW.vsync = 1;
    S->WINDOW.selected_display = 1;
    S->WINDOW.screen_res.w = 1000; S->WINDOW.screen_res.h = 650;
    S->CUSTOM_DECK_Collabs.Spades = xstrdup("default");
    S->CUSTOM_DECK_Collabs.Hearts = xstrdup("default");
    S->CUSTOM_DECK_Collabs.Clubs = xstrdup("default");
    S->CUSTOM_DECK_Collabs.Diamonds = xstrdup("default");
    S->GRAPHICS.texture_scaling = 2;
    S->GRAPHICS.shadows = xstrdup("On");
    S->GRAPHICS.crt = 70;
    S->GRAPHICS.bloom = 1;
    S->profile = 1;
    S->tutorial_progress = dv_nil();
    S->music_control.desired_track = xstrdup("");
    S->music_control.current_track = xstrdup("");
    S->music_control.lerp = 1;
    S->QUEUED_CHANGE = dv_table();
    S->extra = dv_table();

    G.METRICS = dv_table();
    for (int i = 0; i < 3; i++) G.PROFILES[i] = dv_table();

    /* render scale */
    G.TILESIZE = 20;
    G.TILESCALE = 3.65;
    G.TILE_W = 20;
    G.TILE_H = 11.5;
    G.DRAW_HASH_BUFF = 2;
    G.CARD_W = 2.4 * 35 / 41;
    G.CARD_H = 2.4 * 47 / 41;
    G.HIGHLIGHT_H = 0.2 * G.CARD_H;
    G.COLLISION_BUFFER = 0.05;
    G.PITCH_MOD = 1;

    G.STAGE = STAGE_MAIN_MENU;
    G.STATE = STATE_SPLASH;
    G.TAROT_INTERRUPT = 0;
    G.STATE_COMPLETE = false;

    strmap_init(&G.ANIMATION_ATLAS);
    strmap_init(&G.ASSET_ATLAS);
    strmap_init(&G.SHADERS);

    G.MIN_CLICK_DIST = 0.9;
    G.MIN_HOVER_TIME = 0.1;
    G.DEBUG = false;
    G.ANIMATION_FPS = 10;
    G.VIBRATION = 0;
    G.CHALLENGE_WINS = 5;
    G.FPS_CAP = 500;
    G.screenwipe_amt = 1;
    G.SPEEDFACTOR = 1;

    /* colours */
    G.C.MULT = HEX("FE5F55");
    G.C.CHIPS = HEX("009dff");
    G.C.MONEY = HEX("f3b958");
    G.C.XMULT = HEX("FE5F55");
    G.C.FILTER = HEX("ff9a00");
    G.C.BLUE = HEX("009dff");
    G.C.RED = HEX("FE5F55");
    G.C.GREEN = HEX("4BC292");
    G.C.PALE_GREEN = HEX("56a887");
    G.C.ORANGE = HEX("fda200");
    G.C.IMPORTANT = HEX("ff9a00");
    G.C.GOLD = HEX("eac058");
    G.C.YELLOW = rgba(1, 1, 0, 1);
    G.C.CLEAR = rgba(0, 0, 0, 0);
    G.C.WHITE = rgba(1, 1, 1, 1);
    G.C.PURPLE = HEX("8867a5");
    G.C.BLACK = HEX("374244");
    G.C.L_BLACK = HEX("4f6367");
    G.C.GREY = HEX("5f7377");
    G.C.CHANCE = HEX("4BC292");
    G.C.JOKER_GREY = HEX("bfc7d5");
    G.C.VOUCHER = HEX("cb724c");
    G.C.BOOSTER = HEX("646eb7");
    G.C.EDITION = rgba(1, 1, 1, 1);
    G.C.DARK_EDITION = rgba(0, 0, 0, 1);
    G.C.ETERNAL = HEX("c75985");
    G.C.PERISHABLE = HEX("4f5da1");
    G.C.RENTAL = HEX("b18f43");
    G.C.DYN_UI.MAIN = HEX("374244");
    G.C.DYN_UI.DARK = HEX("374244");
    G.C.DYN_UI.BOSS_MAIN = HEX("374244");
    G.C.DYN_UI.BOSS_DARK = HEX("374244");
    G.C.DYN_UI.BOSS_PALE = HEX("374244");
    G.C.SO_1.Hearts = HEX("f03464"); G.C.SO_1.Diamonds = HEX("f06b3f"); G.C.SO_1.Spades = HEX("403995"); G.C.SO_1.Clubs = HEX("235955");
    G.C.SO_2.Hearts = HEX("f83b2f"); G.C.SO_2.Diamonds = HEX("e29000"); G.C.SO_2.Spades = HEX("4f31b9"); G.C.SO_2.Clubs = HEX("008ee6");
    G.C.SUITS.Hearts = HEX("FE5F55"); G.C.SUITS.Diamonds = HEX("FE5F55"); G.C.SUITS.Spades = HEX("374649"); G.C.SUITS.Clubs = HEX("424e54");
    G.C.UI.TEXT_LIGHT = rgba(1, 1, 1, 1);
    G.C.UI.TEXT_DARK = HEX("4F6367");
    G.C.UI.TEXT_INACTIVE = HEX("88888899");
    G.C.UI.BACKGROUND_LIGHT = HEX("B8D8D8");
    G.C.UI.BACKGROUND_WHITE = rgba(1, 1, 1, 1);
    G.C.UI.BACKGROUND_DARK = HEX("7A9E9F");
    G.C.UI.BACKGROUND_INACTIVE = HEX("666666FF");
    G.C.UI.OUTLINE_LIGHT = HEX("D8D8D8");
    G.C.UI.OUTLINE_LIGHT_TRANS = HEX("D8D8D866");
    G.C.UI.OUTLINE_DARK = HEX("7A9E9F");
    G.C.UI.TRANSPARENT_LIGHT = HEX("eeeeee22");
    G.C.UI.TRANSPARENT_DARK = HEX("22222222");
    G.C.UI.HOVER = HEX("00000055");
    G.C.SET.Default = HEX("cdd9dc"); G.C.SET.Enhanced = HEX("cdd9dc"); G.C.SET.Joker = HEX("424e54"); G.C.SET.Tarot = HEX("424e54");
    G.C.SET.Planet = HEX("424e54"); G.C.SET.Spectral = HEX("424e54"); G.C.SET.Voucher = HEX("424e54");
    G.C.SECONDARY_SET.Default = HEX("9bb6bdFF"); G.C.SECONDARY_SET.Enhanced = HEX("8389DDFF"); G.C.SECONDARY_SET.Joker = HEX("708b91");
    G.C.SECONDARY_SET.Tarot = HEX("a782d1"); G.C.SECONDARY_SET.Planet = HEX("13afce"); G.C.SECONDARY_SET.Spectral = HEX("4584fa");
    G.C.SECONDARY_SET.Voucher = HEX("fd682b"); G.C.SECONDARY_SET.Edition = HEX("4ca893");
    G.C.RARITY[0] = NULL;
    G.C.RARITY[1] = HEX("009dff"); G.C.RARITY[2] = HEX("4BC292"); G.C.RARITY[3] = HEX("fe5f55"); G.C.RARITY[4] = HEX("b26cbb");
    G.C.BLIND.Small = HEX("50846e"); G.C.BLIND.Big = HEX("50846e"); G.C.BLIND.Boss = HEX("b44430"); G.C.BLIND.won = HEX("4f6367");
    G.C.HAND_LEVELS[0] = G.C.RED;
    G.C.HAND_LEVELS[1] = HEX("efefef"); G.C.HAND_LEVELS[2] = HEX("95acff"); G.C.HAND_LEVELS[3] = HEX("65efaf"); G.C.HAND_LEVELS[4] = HEX("fae37e");
    G.C.HAND_LEVELS[5] = HEX("ffc052"); G.C.HAND_LEVELS[6] = HEX("f87d75"); G.C.HAND_LEVELS[7] = HEX("caa0ef");
    G.C.BACKGROUND.L = rgba(1, 1, 0, 1);
    G.C.BACKGROUND.D = HEX("374244");
    G.C.BACKGROUND.C = HEX("374244");
    G.C.BACKGROUND.contrast = 1;
    G.C.UI_CHIPS = col_copy(G.C.BLUE);
    G.C.UI_MULT = col_copy(G.C.RED);
    for (int i = 0; i < HAND_COUNT; i++) G.handlist[i] = HAND_NAMES[i];
}
