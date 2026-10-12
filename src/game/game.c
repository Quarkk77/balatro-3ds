/* game.c - port of game.lua: start up, prototypes, profiles, languages,
 * render settings, stage management, splash screen, main menu and
 * Game:start_run. The per frame update/draw lives in game_update.c. */
#include "game.h"
#include <dirent.h>
#include <time.h>

void game_init(void) { game_set_globals(); }

/* ------------------------------------------------------------------ */
/* Game:start_up                                                       */
/* ------------------------------------------------------------------ */
static void load_shaders(void) {
    DIR *d = opendir(res_path("resources/shaders"));
    if (!d) { fprintf(stderr, "no shader directory at %s\n", res_path("resources/shaders")); return; }
    struct dirent *ent;
    while ((ent = readdir(d))) {
        const char *fn = ent->d_name;
        size_t l = strlen(fn);
        if (l < 4 || strcmp(fn + l - 3, ".fs") != 0) continue;
        char name[128];
        snprintf(name, sizeof name, "%.*s", (int)(l - 3), fn);
        GfxShader *s = gfx_load_shader(res_path(tfmt("resources/shaders/%s", fn)));
        if (s) strmap_set(&G.SHADERS, name, s);
        else fprintf(stderr, "failed to load shader %s\n", fn);
    }
    closedir(d);
}

static Sprite *shared_sprite(const char *atlas, SpritePos pos) {
    return sprite_new(0, 0, G.CARD_W, G.CARD_H, atlas_get(atlas), pos);
}

void game_start_up(void) {
    DVal settings;
    if (get_compressed("settings.jkr", &settings)) {
        settings_from_dval(&settings);
        dv_free(&settings);
    }
    str_set(&G.SETTINGS.version, G.VERSION);
    G.SETTINGS.paused = false;

    if (G.SETTINGS.colourblind_option) {
        G.C.SUITS.Hearts = G.C.SO_2.Hearts; G.C.SUITS.Diamonds = G.C.SO_2.Diamonds;
        G.C.SUITS.Spades = G.C.SO_2.Spades; G.C.SUITS.Clubs = G.C.SO_2.Clubs;
    } else {
        G.C.SUITS.Hearts = G.C.SO_1.Hearts; G.C.SUITS.Diamonds = G.C.SO_1.Diamonds;
        G.C.SUITS.Spades = G.C.SO_1.Spades; G.C.SUITS.Clubs = G.C.SO_1.Clubs;
    }

    G.SETTINGS.GRAPHICS.texture_scaling = G.SETTINGS.GRAPHICS.texture_scaling > 1 ? 2 : 1;
    if (!G.F_CTA) {
        G.SETTINGS.DEMO.total_uptime = 0;
        G.SETTINGS.DEMO.timed_CTA_shown = G.SETTINGS.DEMO.win_CTA_shown = G.SETTINGS.DEMO.quit_CTA_shown = true;
    }
    if (!G.SETTINGS.language) G.SETTINGS.language = xstrdup("en-us");

    event_manager_init();
    game_init_window(false);
    sound_init();
    save_manager_init();
    load_shaders();

    controller_init(&G.CONTROLLER);
    if (G.F_RUMBLE && gfx_gamepad_available(0)) Controller_set_gamepad(&G.CONTROLLER, 0);

    game_load_profile(G.SETTINGS.profile ? G.SETTINGS.profile : 1);

    dv_free(&G.SETTINGS.QUEUED_CHANGE);
    G.SETTINGS.QUEUED_CHANGE = dv_table();
    str_set(&G.SETTINGS.music_control.desired_track, "");
    str_set(&G.SETTINGS.music_control.current_track, "");
    G.SETTINGS.music_control.lerp = 1;

    game_set_render_settings();
    game_set_language();
    game_init_item_prototypes();

    /* globally shared sprites (kept out of the stage object lists so that
     * clearing a stage never touches them) */
    G.STAGE_OBJECT_INTERRUPT = true;
    G.shared_debuff = shared_sprite("centers", SPOS(4, 0));
    G.shared_soul = shared_sprite("centers", P_CENTER("soul")->pos);
    G.shared_undiscovered_joker = shared_sprite("centers", P_CENTER("undiscovered_joker")->pos);
    G.shared_undiscovered_tarot = shared_sprite("centers", P_CENTER("undiscovered_tarot")->pos);
    G.shared_sticker_eternal = shared_sprite("stickers", SPOS(0, 0));
    G.shared_sticker_perishable = shared_sprite("stickers", SPOS(0, 2));
    G.shared_sticker_rental = shared_sprite("stickers", SPOS(1, 2));
    G.shared_stickers.White = shared_sprite("stickers", SPOS(1, 0));
    G.shared_stickers.Red = shared_sprite("stickers", SPOS(2, 0));
    G.shared_stickers.Green = shared_sprite("stickers", SPOS(3, 0));
    G.shared_stickers.Black = shared_sprite("stickers", SPOS(0, 1));
    G.shared_stickers.Blue = shared_sprite("stickers", SPOS(4, 0));
    G.shared_stickers.Purple = shared_sprite("stickers", SPOS(1, 1));
    G.shared_stickers.Orange = shared_sprite("stickers", SPOS(2, 1));
    G.shared_stickers.Gold = shared_sprite("stickers", SPOS(3, 1));
    G.shared_seals.Gold = shared_sprite("centers", SPOS(2, 0));
    G.shared_seals.Purple = shared_sprite("centers", SPOS(4, 4));
    G.shared_seals.Red = shared_sprite("centers", SPOS(5, 4));
    G.shared_seals.Blue = shared_sprite("centers", SPOS(6, 4));
    static const char *smap[8] = { "White", "Red", "Green", "Black", "Blue", "Purple", "Orange", "Gold" };
    for (int i = 0; i < 8; i++) G.sticker_map[i] = smap[i];

    G.CURSOR = sprite_new(0, 0, 0.3, 0.3, atlas_get("gamepad_ui"), SPOS(18, 0));
    G.CURSOR->n.states.collide.can = false;
    G.STAGE_OBJECT_INTERRUPT = false;

    G.SPEEDFACTOR = 1;
    set_profile_progress();
    game_splash_screen();
}

/* ------------------------------------------------------------------ */
/* Game:init_item_prototypes                                           */
/* ------------------------------------------------------------------ */
static bool starts(const char *s, const char *p) { return s && strncmp(s, p, strlen(p)) == 0; }

static bool order_lt(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return ((const Center *)a)->order < ((const Center *)b)->order;
}
static bool back_lt(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const Center *x = a, *y = b;
    return (x->order - (x->unlocked ? 100 : 0)) < (y->order - (y->unlocked ? 100 : 0));
}
static bool demo_lt(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const Center *x = a, *y = b;
    return x->order + (str_eq(x->set, "Joker") ? 1000 : 0) < y->order + (str_eq(y->set, "Joker") ? 1000 : 0);
}
static bool locked_lt(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const Center *x = a, *y = b;
    return !x->has_order || !y->has_order || x->order < y->order;
}
static bool tag_lt(const void *a, const void *b, void *ctx) { (void)ctx; return ((const TagProto *)a)->order < ((const TagProto *)b)->order; }
static bool seal_lt(const void *a, const void *b, void *ctx) { (void)ctx; return ((const SealProto *)a)->order < ((const SealProto *)b)->order; }
static bool stake_lt(const void *a, const void *b, void *ctx) { (void)ctx; return ((const StakeProto *)a)->order < ((const StakeProto *)b)->order; }

#define LSORT(vec, fn) lua_table_sort((void **)(vec).items, (vec).count, (fn), NULL)

static CenterVec *pool_for_set(const char *set) {
    if (!set) return NULL;
#define P(name) if (!strcmp(set, #name)) return &G.P_CENTER_POOLS.name
    P(Booster); P(Default); P(Enhanced); P(Edition); P(Joker); P(Tarot); P(Planet); P(Tarot_Planet);
    P(Spectral); P(Consumeables); P(Voucher); P(Back); P(Demo);
#undef P
    return NULL;
}

static void clear_pools(void) {
    vec_clear(&G.P_CENTER_POOLS.Booster); vec_clear(&G.P_CENTER_POOLS.Default); vec_clear(&G.P_CENTER_POOLS.Enhanced);
    vec_clear(&G.P_CENTER_POOLS.Edition); vec_clear(&G.P_CENTER_POOLS.Joker); vec_clear(&G.P_CENTER_POOLS.Tarot);
    vec_clear(&G.P_CENTER_POOLS.Planet); vec_clear(&G.P_CENTER_POOLS.Tarot_Planet); vec_clear(&G.P_CENTER_POOLS.Spectral);
    vec_clear(&G.P_CENTER_POOLS.Consumeables); vec_clear(&G.P_CENTER_POOLS.Voucher); vec_clear(&G.P_CENTER_POOLS.Back);
    vec_clear(&G.P_CENTER_POOLS.Demo); vec_clear(&G.P_CENTER_POOLS.Tag); vec_clear(&G.P_CENTER_POOLS.Seal);
    vec_clear(&G.P_CENTER_POOLS.Stake);
    for (int i = 0; i < 4; i++) vec_clear(&G.P_JOKER_RARITY_POOLS[i]);
    vec_clear(&G.P_LOCKED);
}

void game_init_item_prototypes(void) {
    /* fresh mutable copies of the generated data tables */
    free(G.P_SEALS_LIST);
    G.n_seals = DATA_N_SEALS;
    G.P_SEALS_LIST = xmalloc((size_t)G.n_seals * sizeof(SealProto));
    memcpy(G.P_SEALS_LIST, DATA_SEALS, (size_t)G.n_seals * sizeof(SealProto));
    strmap_free(&G.P_SEALS); strmap_init(&G.P_SEALS);
    for (int i = 0; i < G.n_seals; i++) strmap_set(&G.P_SEALS, G.P_SEALS_LIST[i].key, &G.P_SEALS_LIST[i]);

    free(G.P_TAGS_LIST);
    G.n_tags = DATA_N_TAGS;
    G.P_TAGS_LIST = xmalloc((size_t)G.n_tags * sizeof(TagProto));
    memcpy(G.P_TAGS_LIST, DATA_TAGS, (size_t)G.n_tags * sizeof(TagProto));
    strmap_free(&G.P_TAGS); strmap_init(&G.P_TAGS);
    for (int i = 0; i < G.n_tags; i++) strmap_set(&G.P_TAGS, G.P_TAGS_LIST[i].key, &G.P_TAGS_LIST[i]);
    G.tag_undiscovered = DATA_tag_undiscovered;

    free(G.P_STAKES_LIST);
    G.n_stakes = DATA_N_STAKES;
    G.P_STAKES_LIST = xmalloc((size_t)G.n_stakes * sizeof(StakeProto));
    memcpy(G.P_STAKES_LIST, DATA_STAKES, (size_t)G.n_stakes * sizeof(StakeProto));

    free(G.P_BLINDS_LIST);
    G.n_blinds = DATA_N_BLINDS;
    G.P_BLINDS_LIST = xmalloc((size_t)G.n_blinds * sizeof(BlindProto));
    memcpy(G.P_BLINDS_LIST, DATA_BLINDS, (size_t)G.n_blinds * sizeof(BlindProto));
    strmap_free(&G.P_BLINDS); strmap_init(&G.P_BLINDS);
    for (int i = 0; i < G.n_blinds; i++) {
        BlindProto *b = &G.P_BLINDS_LIST[i];
        if (DATA_BLIND_COLOURS[i]) b->boss_colour = HEX(DATA_BLIND_COLOURS[i]);
        strmap_set(&G.P_BLINDS, b->key, b);
    }
    G.b_undiscovered = DATA_b_undiscovered;

    free(G.P_CARDS_LIST);
    G.n_cards = DATA_N_CARDS;
    G.P_CARDS_LIST = xmalloc((size_t)G.n_cards * sizeof(PCard));
    memcpy(G.P_CARDS_LIST, DATA_CARDS, (size_t)G.n_cards * sizeof(PCard));
    strmap_free(&G.P_CARDS); strmap_init(&G.P_CARDS);
    for (int i = 0; i < G.n_cards; i++) strmap_set(&G.P_CARDS, G.P_CARDS_LIST[i].key, &G.P_CARDS_LIST[i]);
    memset(&G.P_CARD_EMPTY, 0, sizeof G.P_CARD_EMPTY);
    G.P_CARD_EMPTY.key = "empty";
    G.P_CARD_EMPTY.empty = true;

    G.j_locked = DATA_j_locked; G.v_locked = DATA_v_locked; G.c_locked = DATA_c_locked;
    G.j_undiscovered = DATA_j_undiscovered; G.t_undiscovered = DATA_t_undiscovered; G.p_undiscovered = DATA_p_undiscovered;
    G.s_undiscovered = DATA_s_undiscovered; G.v_undiscovered = DATA_v_undiscovered; G.booster_undiscovered = DATA_booster_undiscovered;

    free(G.P_CENTERS_LIST);
    G.n_centers = DATA_N_CENTERS;
    G.P_CENTERS_LIST = xmalloc((size_t)G.n_centers * sizeof(Center));
    memcpy(G.P_CENTERS_LIST, DATA_CENTERS, (size_t)G.n_centers * sizeof(Center));
    strmap_free(&G.P_CENTERS); strmap_init(&G.P_CENTERS);
    for (int i = 0; i < G.n_centers; i++) {
        G.P_CENTERS_LIST[i].index = i;
        strmap_set(&G.P_CENTERS, G.P_CENTERS_LIST[i].key, &G.P_CENTERS_LIST[i]);
    }

    clear_pools();
    game_save_progress();

    /* apply meta.jkr (unlocks / discoveries / alerts) */
    int p = G.SETTINGS.profile;
    dir_make(res_save_path(tfmt("%d", p)));
    convert_save_to_meta();
    DVal meta;
    if (!get_compressed(tfmt("%d/meta.jkr", p), &meta)) meta = dv_table();
    DVal *mu = dv_get(&meta, "unlocked"), *md = dv_get(&meta, "discovered"), *ma = dv_get(&meta, "alerted");
#define META(t, k) dv_truthy(dv_get((t), (k)))

    for (int i = 0; i < G.n_centers; i++) {
        Center *v = &G.P_CENTERS_LIST[i];
        const char *k = v->key;
        if (v->wip || v->demo) continue;
        bool jbv = starts(k, "j_") || starts(k, "b_") || starts(k, "v_");
        if (!v->unlocked && jbv && META(mu, k)) v->unlocked = true;
        if (!v->unlocked && jbv) vec_push(&G.P_LOCKED, v);
        if (!v->discovered && (jbv || starts(k, "e_") || starts(k, "c_") || starts(k, "p_")) && META(md, k)) v->discovered = true;
        if ((v->discovered && META(ma, k)) || str_eq(v->set, "Back") || v->start_alerted) v->alerted = true;
        else if (v->discovered) v->alerted = false;
    }
    LSORT(G.P_LOCKED, locked_lt);

    for (int i = 0; i < G.n_blinds; i++) {
        BlindProto *v = &G.P_BLINDS_LIST[i];
        if (!v->discovered && META(md, v->key)) v->discovered = true;
        if (v->discovered && META(ma, v->key)) v->alerted = true;
        else if (v->discovered) v->alerted = false;
    }
    for (int i = 0; i < G.n_tags; i++) {
        TagProto *v = &G.P_TAGS_LIST[i];
        if (!v->discovered && META(md, v->key)) v->discovered = true;
        if (v->discovered && META(ma, v->key)) v->alerted = true;
        else if (v->discovered) v->alerted = false;
        vec_push(&G.P_CENTER_POOLS.Tag, v);
    }
    for (int i = 0; i < G.n_seals; i++) {
        SealProto *v = &G.P_SEALS_LIST[i];
        if (!v->discovered && META(md, v->key)) v->discovered = true;
        if (v->discovered && META(ma, v->key)) v->alerted = true;
        else if (v->discovered) v->alerted = false;
        vec_push(&G.P_CENTER_POOLS.Seal, v);
    }
#undef META
    dv_free(&meta);
    for (int i = 0; i < G.n_stakes; i++) vec_push(&G.P_CENTER_POOLS.Stake, &G.P_STAKES_LIST[i]);

    for (int i = 0; i < G.n_centers; i++) {
        Center *v = &G.P_CENTERS_LIST[i];
        if (str_eq(v->set, "Joker")) vec_push(&G.P_CENTER_POOLS.Joker, v);
        if (v->set && v->demo) vec_push(&G.P_CENTER_POOLS.Demo, v);
        if (v->wip) continue;
        if (v->set && !str_eq(v->set, "Joker") && !v->skip_pool && !v->omit) {
            CenterVec *pool = pool_for_set(v->set);
            if (pool) vec_push(pool, v);
        }
        if (str_eq(v->set, "Tarot") || str_eq(v->set, "Planet")) vec_push(&G.P_CENTER_POOLS.Tarot_Planet, v);
        if (v->consumeable) vec_push(&G.P_CENTER_POOLS.Consumeables, v);
        if (v->rarity >= 1 && v->rarity <= 4 && str_eq(v->set, "Joker") && !v->demo) vec_push(&G.P_JOKER_RARITY_POOLS[v->rarity - 1], v);
    }

    LSORT(G.P_CENTER_POOLS.Joker, order_lt);
    LSORT(G.P_CENTER_POOLS.Tarot, order_lt);
    LSORT(G.P_CENTER_POOLS.Planet, order_lt);
    LSORT(G.P_CENTER_POOLS.Tarot_Planet, order_lt);
    LSORT(G.P_CENTER_POOLS.Spectral, order_lt);
    LSORT(G.P_CENTER_POOLS.Voucher, order_lt);
    LSORT(G.P_CENTER_POOLS.Booster, order_lt);
    LSORT(G.P_CENTER_POOLS.Consumeables, order_lt);
    LSORT(G.P_CENTER_POOLS.Back, back_lt);
    LSORT(G.P_CENTER_POOLS.Enhanced, order_lt);
    LSORT(G.P_CENTER_POOLS.Stake, stake_lt);
    LSORT(G.P_CENTER_POOLS.Tag, tag_lt);
    LSORT(G.P_CENTER_POOLS.Seal, seal_lt);
    LSORT(G.P_CENTER_POOLS.Demo, demo_lt);
    for (int i = 0; i < 4; i++) LSORT(G.P_JOKER_RARITY_POOLS[i], order_lt);
}

/* ------------------------------------------------------------------ */
/* Game:load_profile                                                   */
/* ------------------------------------------------------------------ */
static const char *TEMP_PROFILE =
    "return {"
    "MEMORY = {deck = 'Red Deck', stake = 1},"
    "stake = 1,"
    "high_scores = {"
    "  hand = {label = 'Best Hand', amt = 0},"
    "  furthest_round = {label = 'Highest Round', amt = 0},"
    "  furthest_ante = {label = 'Highest Ante', amt = 0},"
    "  most_money = {label = 'Most Money', amt = 0},"
    "  boss_streak = {label = 'Most Bosses in a Row', amt = 0},"
    "  collection = {label = 'Collection', amt = 0, tot = 1},"
    "  win_streak = {label = 'Best Win Streak', amt = 0},"
    "  current_streak = {label = '', amt = 0},"
    "  poker_hand = {label = 'Most Played Hand', amt = 0}"
    "},"
    "career_stats = {"
    "  c_round_interest_cap_streak = 0, c_dollars_earned = 0, c_shop_dollars_spent = 0, c_tarots_bought = 0,"
    "  c_planets_bought = 0, c_playing_cards_bought = 0, c_vouchers_bought = 0, c_tarot_reading_used = 0,"
    "  c_planetarium_used = 0, c_shop_rerolls = 0, c_cards_played = 0, c_cards_discarded = 0, c_losses = 0,"
    "  c_wins = 0, c_rounds = 0, c_hands_played = 0, c_face_cards_played = 0, c_jokers_sold = 0, c_cards_sold = 0,"
    "  c_single_hand_round_streak = 0"
    "},"
    "progress = {},"
    "joker_usage = {}, consumeable_usage = {}, voucher_usage = {}, hand_usage = {}, deck_usage = {}, deck_stakes = {},"
    "challenge_progress = {completed = {}, unlocked = {}}"
    "}";

static void recursive_init(const DVal *t1, DVal *t2) {
    for (int i = 0; i < dv_count(t1); i++) {
        const DKey *k = dv_key(t1, i);
        DVal *v = dv_val(t1, i);
        DVal *cur = k->is_str ? dv_get(t2, k->s) : dv_geti(t2, (int)k->n);
        if (!dv_truthy(cur)) {
            if (k->is_str) dv_set(t2, k->s, dv_copy(v));
            else dv_seti(t2, (int)k->n, dv_copy(v));
        } else if (dv_is_table(cur) && dv_is_table(v)) recursive_init(v, cur);
    }
}

void game_load_profile(int p) {
    if (p < 1 || p > 3) p = 1;
    G.SETTINGS.profile = p;
    DVal *prof = &G.PROFILES[p - 1];
    if (prof->t != DV_TABLE) *prof = dv_table();
    DVal info;
    if (get_compressed(tfmt("%d/profile.jkr", p), &info)) {
        for (int i = 0; i < dv_count(&info); i++) {
            const DKey *k = dv_key(&info, i);
            if (k->is_str) dv_set(prof, k->s, dv_copy(dv_val(&info, i)));
            else dv_seti(prof, (int)k->n, dv_copy(dv_val(&info, i)));
        }
        dv_free(&info);
    }
    DVal tmp;
    if (dv_parse(TEMP_PROFILE, &tmp)) {
        recursive_init(&tmp, prof);
        dv_free(&tmp);
    }
}

/* ------------------------------------------------------------------ */
/* Game:set_language                                                   */
/* ------------------------------------------------------------------ */
typedef struct { const char *id; int font; const char *label, *key; bool beta; const char *button, *w1, *w2, *w3; bool omit; } LangSpec;
static const LangSpec LANG_SPECS[] = {
    { "en-us", 1, "English", "en-us", false, "Language Feedback", "This language is still in Beta. To help us", "improve it, please click on the feedback button.", "Click again to confirm", false },
    { "de", 1, "Deutsch", "de", false, "Feedback zur Übersetzung", "Diese Übersetzung ist noch im Beta-Stadium. Willst du uns helfen,", "sie zu verbessern? Dann klicke bitte auf die Feedback-Taste.", "Zum Bestätigen erneut klicken", false },
    { "es_419", 1, "Español (México)", "es_419", false, "Sugerencias de idioma", "Este idioma todavía está en Beta. Pulsa el botón", "de sugerencias para ayudarnos a mejorarlo.", "Haz clic de nuevo para confirmar", false },
    { "es_ES", 1, "Español (España)", "es_ES", false, "Sugerencias de idioma", "Este idioma todavía está en Beta. Pulsa el botón", "de sugerencias para ayudarnos a mejorarlo.", "Haz clic de nuevo para confirmar", false },
    { "fr", 1, "Français", "fr", false, "Partager votre avis", "La traduction française est encore en version bêta. ", "Veuillez cliquer sur le bouton pour nous donner votre avis.", "Cliquez à nouveau pour confirmer", false },
    { "id", 1, "Bahasa Indonesia", "id", true, "Umpan Balik Bahasa", "Bahasa ini masih dalam tahap Beta. Untuk membantu", "kami meningkatkannya, silakan klik tombol umpan balik.", "Klik lagi untuk mengonfirmasi", false },
    { "it", 1, "Italiano", "it", false, "Feedback traduzione", "Questa traduzione è ancora in Beta. Per", "aiutarci a migliorarla, clicca il tasto feedback", "Fai clic di nuovo per confermare", false },
    { "ja", 5, "日本語", "ja", false, "提案する", "この翻訳は現在ベータ版です。提案があった場合、", "ボタンをクリックしてください。", "もう一度クリックして確認", false },
    { "ko", 4, "한국어", "ko", false, "번역 피드백", "이 언어는 아직 베타 단계에 있습니다. ", "번역을 도와주시려면 피드백 버튼을 눌러주세요.", "다시 클릭해서 확인하세요", false },
    { "nl", 1, "Nederlands", "nl", false, "Taal suggesties", "Deze taal is nog in de Beta fase. Help ons het te ", "verbeteren door op de suggestie knop te klikken.", "Klik opnieuw om te bevestigen", false },
    { "pl", 1, "Polski", "pl", false, "Wyślij uwagi do tłumaczenia", "Polska wersja językowa jest w fazie Beta. By pomóc nam poprawić", " jakość tłumaczenia, kliknij przycisk i podziel się swoją opinią i uwagami.", "Kliknij ponownie, aby potwierdzić", false },
    { "pt_BR", 1, "Português", "pt_BR", false, "Feedback de Tradução", "Esta tradução ainda está em Beta. Se quiser nos ajudar", "a melhorá-la, clique no botão de feedback por favor", "Clique novamente para confirmar", false },
    { "ru", 6, "Русский", "ru", true, "Отзыв о языке", "Этот язык все еще находится в Бета-версии. Чтобы помочь", "нам его улучшить, пожалуйста, нажмите на кнопку обратной связи.", "Щелкните снова, чтобы подтвердить", false },
    { "zh_CN", 2, "简体中文", "zh_CN", false, "意见反馈", "这个语言目前尚为Beta版本。 请帮助我们改善翻译品质，", "点击”意见反馈” 来提供你的意见。", "再次点击确认", false },
    { "zh_TW", 3, "繁體中文", "zh_TW", false, "意見回饋", "這個語言目前尚為Beta版本。請幫助我們改善翻譯品質，", "點擊”意見回饋” 來提供你的意見。", "再按一下即可確認", false },
    { "all1", 8, "English", "all", false, NULL, NULL, NULL, NULL, true },
    { "all2", 9, "English", "all", false, NULL, NULL, NULL, NULL, true },
};

typedef struct { const char *file; double rs_mul, ths, ox, oy, fs, squish, ds; } FontSpec;
static const FontSpec FONT_SPECS[9] = {
    { "resources/fonts/m6x11plus.ttf", 10, 0.83, 10, -20, 0.1, 1, 1 },
    { "resources/fonts/NotoSansSC-Bold.ttf", 7, 0.7, 0, -35, 0.12, 1, 1.1 },
    { "resources/fonts/NotoSansTC-Bold.ttf", 7, 0.7, 0, -35, 0.12, 1, 1.1 },
    { "resources/fonts/NotoSansKR-Bold.ttf", 7, 0.8, 0, -20, 0.12, 1, 1 },
    { "resources/fonts/NotoSansJP-Bold.ttf", 7, 0.8, 0, -20, 0.12, 1, 1 },
    { "resources/fonts/NotoSans-Bold.ttf", 7, 0.65, 0, -40, 0.12, 1, 1 },
    { "resources/fonts/m6x11plus.ttf", 10, 0.9, 10, 15, 0.1, 1, 1 },
    { "resources/fonts/GoNotoCurrent-Bold.ttf", 10, 0.8, 10, -20, 0.1, 1, 1 },
    { "resources/fonts/GoNotoCJKCore.ttf", 10, 0.8, 10, -20, 0.1, 1, 1 },
};

/* Fonts are loaded lazily: the CJK fonts are large and only needed when
 * that language is active (the original loaded them all up front). */
static void ensure_font(FontDef *f) {
    if (f->FONT || !f->file) return;
    if (file_exists(res_path(f->file))) f->FONT = gfx_load_font(res_path(f->file), (float)f->render_scale);
    if (!f->FONT) f->FONT = G.FONTS[0].FONT;
}

void game_set_language(void) {
    if (!G.LANGUAGES) {
        if (!file_exists(res_path(tfmt("localization/%s.lua", G.SETTINGS.language))) || G.F_ENGLISH_ONLY)
            str_set(&G.SETTINGS.language, "en-us");
        for (int i = 0; i < 9; i++) {
            FontDef *f = &G.FONTS[i];
            const FontSpec *s = &FONT_SPECS[i];
            f->file = s->file;
            f->render_scale = G.TILESIZE * s->rs_mul;
            f->TEXT_HEIGHT_SCALE = s->ths;
            f->TEXT_OFFSET = (Vec2){ s->ox, s->oy };
            f->FONTSCALE = s->fs;
            f->squish = s->squish;
            f->DESCSCALE = s->ds;
            f->FONT = NULL;
        }
        ensure_font(&G.FONTS[0]);
        G.n_languages = ARRLEN(LANG_SPECS);
        G.LANGUAGES = xcalloc((size_t)G.n_languages, sizeof(Language));
        for (int i = 0; i < G.n_languages; i++) {
            const LangSpec *s = &LANG_SPECS[i];
            Language *l = &G.LANGUAGES[i];
            l->font_index = s->font;
            l->font = &G.FONTS[s->font - 1];
            l->label = s->label;
            l->key = s->key;
            l->beta = s->beta;
            l->button = s->button;
            l->warning[0] = s->w1; l->warning[1] = s->w2; l->warning[2] = s->w3;
            l->omit = s->omit;
        }
    }
    G.LANG = &G.LANGUAGES[0];
    for (int i = 0; i < G.n_languages; i++)
        if (!G.LANGUAGES[i].omit && str_eq(G.LANGUAGES[i].key, G.SETTINGS.language)) { G.LANG = &G.LANGUAGES[i]; break; }
    ensure_font(G.LANG->font);

    int len;
    char *src = file_read_all(res_path(tfmt("localization/%s.lua", G.SETTINGS.language)), &len);
    if (src) {
        DVal loc;
        if (dv_parse(src, &loc)) {
            dv_free(&G.localization);
            G.localization = loc;
            init_localization();
        } else fprintf(stderr, "failed to parse localization/%s.lua\n", G.SETTINGS.language);
        free(src);
    }
}

/* ------------------------------------------------------------------ */
/* Game:set_render_settings                                            */
/* ------------------------------------------------------------------ */
typedef struct { const char *name, *file; double px, py; int frames; } AtlasSpec;
static const AtlasSpec ANIM_ATLI[] = {
    { "blind_chips", "BlindChips.png", 34, 34, 21 },
    { "shop_sign", "ShopSignAnimation.png", 113, 57, 4 },
};
static const AtlasSpec ASSET_ATLI[] = {
    { "cards_1", "8BitDeck.png", 71, 95, 0 }, { "cards_2", "8BitDeck_opt2.png", 71, 95, 0 },
    { "centers", "Enhancers.png", 71, 95, 0 }, { "Joker", "Jokers.png", 71, 95, 0 },
    { "Tarot", "Tarots.png", 71, 95, 0 }, { "Voucher", "Vouchers.png", 71, 95, 0 },
    { "Booster", "boosters.png", 71, 95, 0 }, { "ui_1", "ui_assets.png", 18, 18, 0 },
    { "ui_2", "ui_assets_opt2.png", 18, 18, 0 }, { "balatro", "balatro.png", 333, 216, 0 },
    { "gamepad_ui", "gamepad_ui.png", 32, 32, 0 }, { "icons", "icons.png", 66, 66, 0 },
    { "tags", "tags.png", 34, 34, 0 }, { "stickers", "stickers.png", 71, 95, 0 },
    { "chips", "chips.png", 29, 29, 0 },
};
static const char *COLLABS[] = { "AU", "TW", "VS", "DTD", "CYP", "STS", "TBoI", "SV", "SK", "DS", "CL", "D2", "PC", "WF", "EG",
                                 "XR", "CR", "BUG", "FO", "DBD", "C7", "R", "AC", "STP" };

static void load_atlas(StrMap *map, const char *name, const char *path, double px, double py, int frames, float dpiscale, bool anim) {
    bool linear = G.SETTINGS.GRAPHICS.texture_scaling != 1;
    GfxImage *img = gfx_load_image(path, dpiscale, true, linear);
    if (!img) { fprintf(stderr, "failed to load image %s\n", path); return; }
    Atlas *a = strmap_get(map, name);
    if (!a) {
        a = NEW(Atlas);
        a->name = xstrdup(name);
        strmap_set(map, name, a);
    } else if (a->image) gfx_free_image(a->image);
    a->image = img;
    a->px = px; a->py = py;
    a->frames = frames;
    a->animation = anim;
}

void game_set_render_settings(void) {
    if (G.SETTINGS.GRAPHICS.texture_scaling != 1 && G.SETTINGS.GRAPHICS.texture_scaling != 2) G.SETTINGS.GRAPHICS.texture_scaling = 2;
    int ts = (int)G.SETTINGS.GRAPHICS.texture_scaling;
    for (int i = 0; i < ARRLEN(ANIM_ATLI); i++)
        load_atlas(&G.ANIMATION_ATLAS, ANIM_ATLI[i].name, res_path(tfmt("resources/textures/%dx/%s", ts, ANIM_ATLI[i].file)),
                   ANIM_ATLI[i].px, ANIM_ATLI[i].py, ANIM_ATLI[i].frames, (float)ts, true);
    for (int i = 0; i < ARRLEN(ASSET_ATLI); i++)
        load_atlas(&G.ASSET_ATLAS, ASSET_ATLI[i].name, res_path(tfmt("resources/textures/%dx/%s", ts, ASSET_ATLI[i].file)),
                   ASSET_ATLI[i].px, ASSET_ATLI[i].py, 0, (float)ts, false);
    for (int i = 0; i < ARRLEN(COLLABS); i++)
        for (int v = 1; v <= 2; v++)
            load_atlas(&G.ASSET_ATLAS, tfmt("collab_%s_%d", COLLABS[i], v),
                       res_path(tfmt("resources/textures/%dx/collabs/collab_%s_%d.png", ts, COLLABS[i], v)), 71, 95, 0, (float)ts, false);
    load_atlas(&G.ASSET_ATLAS, "playstack_logo", res_path("resources/textures/1x/playstack-logo.png"), 1417, 1417, 0, 1, false);
    load_atlas(&G.ASSET_ATLAS, "localthunk_logo", res_path("resources/textures/1x/localthunk-logo.png"), 1390, 560, 0, 1, false);

    Atlas *tarot = strmap_get(&G.ASSET_ATLAS, "Tarot");
    strmap_set(&G.ASSET_ATLAS, "Planet", tarot);
    strmap_set(&G.ASSET_ATLAS, "Spectral", tarot);

    for (int i = 0; i < G.I.SPRITE.count; i++) Sprite_reset(G.I.SPRITE.items[i]);
}

/* ------------------------------------------------------------------ */
/* Game:init_window                                                    */
/* ------------------------------------------------------------------ */
void game_init_window(bool reset) {
    (void)reset;
    G.ROOM_PADDING_H = 0.7;
    G.ROOM_PADDING_W = 1;
    G.WINDOWTRANS.x = 0; G.WINDOWTRANS.y = 0;
    G.WINDOWTRANS.w = G.TILE_W + 2 * G.ROOM_PADDING_W;
    G.WINDOWTRANS.h = G.TILE_H + 2 * G.ROOM_PADDING_H;
    G.window_prev.orig_scale = G.TILESCALE;
    G.window_prev.w = G.WINDOWTRANS.w * G.TILESIZE * G.TILESCALE;
    G.window_prev.h = G.WINDOWTRANS.h * G.TILESIZE * G.TILESCALE;
    G.window_prev.orig_ratio = G.window_prev.w / G.window_prev.h;
    apply_window_changes(true);
}

/* ------------------------------------------------------------------ */
/* Game:delete_run                                                     */
/* ------------------------------------------------------------------ */
static void remove_box(UIBox **b) {
    if (*b) { node_remove(&(*b)->n); *b = NULL; }
}
static void remove_sprite(Sprite **s) {
    if (*s) { node_remove(&(*s)->n); *s = NULL; }
}

void game_delete_run(void) {
    if (G.ROOM) {
        remove_all_nodes(&G.STAGE_OBJECTS[G.STAGE]);
        dv_free(&G.load_shop_booster); dv_free(&G.load_shop_jokers); dv_free(&G.load_shop_vouchers);
        G.load_shop_booster_set = G.load_shop_jokers_set = G.load_shop_vouchers_set = false;
        remove_box(&G.buttons);
        remove_box(&G.deck_preview);
        remove_box(&G.shop);
        remove_box(&G.blind_select);
        remove_box(&G.booster_pack);
        remove_box(&G.MAIN_MENU_UI);
        remove_sprite(&G.SPLASH_FRONT);
        remove_sprite(&G.SPLASH_BACK);
        remove_sprite(&G.SPLASH_LOGO);
        remove_box(&G.GAME_OVER_UI);
        remove_box(&G.collection_alert);
        remove_box(&G.HUD);
        remove_box(&G.HUD_blind);
        for (int i = 0; i < G.HUD_tags.count; i++) node_remove(G.HUD_tags.items[i]);
        vec_clear(&G.HUD_tags);
        remove_box(&G.OVERLAY_MENU);
        if (G.OVERLAY_TUTORIAL) {
            if (G.OVERLAY_TUTORIAL->Jimbo) node_remove(&G.OVERLAY_TUTORIAL->Jimbo->n);
            if (G.OVERLAY_TUTORIAL->content) node_remove(&G.OVERLAY_TUTORIAL->content->n);
            remove_box(&G.OVERLAY_TUTORIAL);
        }
        remove_box(&G.blind_prompt_box);
        remove_box(&G.SHOP_SIGN);
        remove_box(&G.round_eval);
        remove_box(&G.boss_warning_text);
        for (int i = 0; i < 3; i++) G.blind_select_opts[i] = NULL;
        G.booster_pack_sparkles = G.booster_pack_meteors = G.booster_pack_stars = NULL;
        G.consumeables = G.jokers = G.discard = G.deck = G.hand = G.play = G.title_top = NULL;
        G.shop_jokers = G.shop_vouchers = G.shop_booster = G.pack_cards = NULL;
        for (int i = 0; i < 8; i++) G.your_collection[i] = NULL;
        G.n_your_collection = 0;
        G.GAME.blind = NULL;
        memset(&G.hand_text_area, 0, sizeof G.hand_text_area);
        vec_clear(&G.I.CARD);
        vec_clear(&G.playing_cards);
    }
    G.VIEWING_DECK = false;
    event_clear_queue(-1, -1);
    Controller_mod_cursor_context_layer(&G.CONTROLLER, -1000);
    G.GAME.won = false;
    G.STATE = STATE_NONE;
}

/* ------------------------------------------------------------------ */
/* Game:prep_stage                                                     */
/* ------------------------------------------------------------------ */
void game_prep_stage(int new_stage, int new_state, bool new_game_obj) {
    Controller *c = &G.CONTROLLER;
    c->locks.wipe = c->locks.frame = c->locks.frame_set = c->locks.load = c->locks.no_space = c->locks.chosen_card = false;
    memset(c->locks.unused, 0, sizeof c->locks.unused);
    strset_clear(&c->locks.other);
    if (new_game_obj) game_init_game_object(&G.GAME);
    G.STAGE = new_stage ? new_stage : STAGE_MAIN_MENU;
    G.STATE = new_state ? new_state : STATE_MENU;
    G.STATE_COMPLETE = false;
    G.SETTINGS.paused = false;

    G.ROOM = node_new(G.ROOM_PADDING_W, G.ROOM_PADDING_H, G.TILE_W, G.TILE_H);
    G.ROOM_jiggle = 0;
    G.ROOM->states.drag.can = false;
    node_set_container(G.ROOM, G.ROOM);

    G.ROOM_ATTACH = moveable_new(0, 0, G.TILE_W, G.TILE_H);
    G.ROOM_ATTACH->states.drag.can = false;
    node_set_container(G.ROOM_ATTACH, G.ROOM);
    game_resize(gfx_width(), gfx_height());
}

/* ------------------------------------------------------------------ */
/* Game:init_game_object                                               */
/* ------------------------------------------------------------------ */
typedef struct { const char *name; bool visible; double mult, chips, l_mult, l_chips; const char *ex[5]; bool sc[5]; } HandInit;
static const HandInit HAND_INIT[HAND_COUNT] = {
    { "Flush Five", false, 16, 160, 3, 50, { "S_A", "S_A", "S_A", "S_A", "S_A" }, { 1, 1, 1, 1, 1 } },
    { "Flush House", false, 14, 140, 4, 40, { "D_7", "D_7", "D_7", "D_4", "D_4" }, { 1, 1, 1, 1, 1 } },
    { "Five of a Kind", false, 12, 120, 3, 35, { "S_A", "H_A", "H_A", "C_A", "D_A" }, { 1, 1, 1, 1, 1 } },
    { "Straight Flush", true, 8, 100, 4, 40, { "S_Q", "S_J", "S_T", "S_9", "S_8" }, { 1, 1, 1, 1, 1 } },
    { "Four of a Kind", true, 7, 60, 3, 30, { "S_J", "H_J", "C_J", "D_J", "C_3" }, { 1, 1, 1, 1, 0 } },
    { "Full House", true, 4, 40, 2, 25, { "H_K", "C_K", "D_K", "S_2", "D_2" }, { 1, 1, 1, 1, 1 } },
    { "Flush", true, 4, 35, 2, 15, { "H_A", "H_K", "H_T", "H_5", "H_4" }, { 1, 1, 1, 1, 1 } },
    { "Straight", true, 4, 30, 3, 30, { "D_J", "C_T", "C_9", "S_8", "H_7" }, { 1, 1, 1, 1, 1 } },
    { "Three of a Kind", true, 3, 30, 2, 20, { "S_T", "C_T", "D_T", "H_6", "D_5" }, { 1, 1, 1, 0, 0 } },
    { "Two Pair", true, 2, 20, 1, 20, { "H_A", "D_A", "C_Q", "H_4", "C_4" }, { 1, 1, 0, 1, 1 } },
    { "Pair", true, 2, 10, 1, 15, { "S_K", "S_9", "D_9", "H_6", "D_3" }, { 0, 1, 1, 0, 0 } },
    { "High Card", true, 1, 5, 1, 10, { "S_A", "D_Q", "D_9", "C_4", "D_3" }, { 1, 0, 0, 0, 0 } },
};

void game_free_state(GameState *g) { (void)g; /* run state is small; old objects may still be referenced by events */ }

void game_init_game_object(GameState *g) {
    memset(g, 0, sizeof *g);
    g->won = false;
#define SC(f, l) g->round_scores.f.label = l
    SC(furthest_ante, "Ante"); SC(furthest_round, "Round"); SC(hand, "Best Hand"); SC(poker_hand, "Most Played Hand");
    SC(new_collection, "New Discoveries"); SC(cards_played, "Cards Played"); SC(cards_discarded, "Cards Discarded");
    SC(times_rerolled, "Times Rerolled"); SC(cards_purchased, "Cards Purchased");
#undef SC
    strmap_init(&g->joker_usage_unused);
    strmap_init(&g->consumeable_usage);
    strmap_init(&g->hand_usage);
    strmap_init(&g->perscribed_bosses_unused);
    g->win_ante = 8;
    g->stake = 1;
    strset_init(&g->modifiers.other);
    g->starting_params = get_starting_params();
    strset_init(&g->banned_keys);
    g->round = 0;
    g->probabilities.normal = 1;
    nummap_init(&g->bosses_used);
    for (int i = 0; i < G.n_blinds; i++)
        if (G.P_BLINDS_LIST[i].has_boss) nummap_set(&g->bosses_used, G.P_BLINDS_LIST[i].key, 0);
    nummap_init(&g->pseudorandom.keys);
    g->starting_deck_size = 52;
    g->ecto_minus = 1;
    g->pack_size = 2;
    g->edition_rate = 1;
    g->joker_rate = 20;
    g->tarot_rate = 4;
    g->planet_rate = 4;
    g->interest_cap = 25;
    g->interest_amount = 1;
    g->perishable_rounds = 5;
    g->rental_rate = 3;
    g->chips_text = xstrdup("0");
    g->voucher_text = xstrdup("");
    g->base_reroll_cost = 5;
    g->sort = "desc";
    g->previous_round.dollars = 4;
    strset_init(&g->pool_flags);
    strset_init(&g->used_jokers);
    strset_init(&g->used_vouchers);

    CurrentRound *c = &g->current_round;
    c->current_hand.chip_text = xstrdup("0");
    c->current_hand.mult_text = xstrdup("0");
    c->current_hand.chip_total_text = xstrdup("");
    c->current_hand.handname = xstrdup("");
    c->current_hand.hand_level = xstrdup("");
    c->round_text = xstrdup("Round ");
    c->idol_card.suit = "Spades"; c->idol_card.rank = "Ace";
    c->mail_card.rank = "Ace";
    c->ancient_card.suit = "Spades";
    c->castle_card.suit = "Spades";
    c->reroll_cost = 5;
    c->dollars_to_be_earned = xstrdup("!!!");
    c->most_played_poker_hand = "High Card";

    RoundResets *r = &g->round_resets;
    r->hands = 1; r->discards = 1; r->reroll_cost = 1;
    r->ante = 1; r->blind_ante = 1;
    r->blind_states[BT_SMALL] = "Select"; r->blind_states[BT_BIG] = "Upcoming"; r->blind_states[BT_BOSS] = "Upcoming";
    for (int i = 0; i < 3; i++) r->loc_blind_states[i] = xstrdup("");
    r->blind_choices[BT_SMALL] = "bl_small"; r->blind_choices[BT_BIG] = "bl_big";

    g->shop.joker_max = 2;
    for (int i = 0; i < 13; i++) nummap_init(&g->cards_played[i].suits);
    for (int h = 0; h < HAND_COUNT; h++) {
        const HandInit *hi = &HAND_INIT[h];
        HandStat *s = &g->hands[h];
        s->visible = hi->visible;
        s->order = h + 1;
        s->mult = s->s_mult = hi->mult;
        s->chips = s->s_chips = hi->chips;
        s->level = 1;
        s->l_mult = hi->l_mult;
        s->l_chips = hi->l_chips;
        for (int i = 0; i < 5; i++) { s->example[i].card = hi->ex[i]; s->example[i].scoring = hi->sc[i]; }
    }
}

/* ------------------------------------------------------------------ */
/* Game:splash_screen                                                  */
/* ------------------------------------------------------------------ */
typedef struct { Card *card; } SplashCardCtx;
typedef struct { Card *card; double pitch; int i; } SplashSuckCtx;
typedef struct { int i; } SplashIdx;

static Card *splash_sc;

static bool ev_discover_nothing(Event *e, void *p) { (void)e; (void)p; discover_card(NULL); return true; }

static bool ev_splash_spawn(Event *e, void *p) {
    (void)e; (void)p;
    double s = 1.2;
    Card *sc = card_new(G.ROOM->T->w / 2 - s * G.CARD_W / 2, 10. + G.ROOM->T->h / 2 - s * G.CARD_H / 2, s * G.CARD_W, s * G.CARD_H, NULL,
                        P_CENTER("j_joker"), NULL);
    sc->n.T->y = G.ROOM->T->h / 2 - s * G.CARD_H / 2;
    sc->n.ambient_tilt = 1;
    sc->n.states.drag.can = false;
    sc->n.states.hover.can = false;
    sc->no_ui = true;
    splash_sc = sc;
    G.VIBRATION += 2;
    sound_play("whoosh1", 0.7, 0.2);
    sound_play("introPad1", 0.704, 0.6);
    return true;
}

static bool ev_splash_dissolve(Event *e, void *p) {
    (void)e; (void)p;
    if (splash_sc) {
        Col w[2] = { G.C.WHITE, G.C.WHITE };
        Card_start_dissolve(splash_sc, w, 2, true, 12, true);
    }
    sound_play("magic_crumple", 1, 0.5);
    sound_play("splash_buildup", 1, 0.7);
    return true;
}

static bool ev_splash_suck(Event *e, void *p) {
    (void)e;
    SplashSuckCtx *c = p;
    if (c->card->n.T->scale <= 0) {
        if (c->i < 30) sound_play("whoosh1", c->pitch + math_random() * 0.05, 0.25 * (1 - c->i / 50.0));
        if (c->i == 15) sound_play("whoosh_long", 0.9, 0.7);
        G.VIBRATION += 0.1;
        node_remove(&c->card->n);
        return true;
    }
    return false;
}

static bool ev_splash_card(Event *e, void *p) {
    (void)e;
    int i = ((SplashIdx *)p)->i;
    double scale = 2 - i / 300.0;
    double angle = math_random() * 2 * 3.14;
    double card_size = scale * (math_random() + 1);
    Vec2 pos = { (18 + card_size) * sin(angle), (18 + card_size) * cos(angle) };
    Card *card = card_new(pos.x + G.ROOM->T->w / 2 - G.CARD_W * card_size / 2, pos.y + G.ROOM->T->h / 2 - G.CARD_H * card_size / 2,
                          card_size * G.CARD_W, card_size * G.CARD_H, pseudorandom_p_card(NAN), P_CENTER("c_base"), NULL);
    if (math_random() > 0.8) { card->sprite_facing = FACE_BACK; card->facing = FACE_BACK; }
    card->no_shadow = true;
    card->n.states.hover.can = false;
    card->n.states.drag.can = false;
    card->vortex = true;
    card->n.T->r = angle;
    double speed = MAX(2. - i * 0.005, 0.001);
    ease_value(REFN(&card->n.T->scale), -card->n.T->scale, false, -1, false, 1. * speed, EASE_ELASTIC);
    ease_value(REFN(&card->n.T->x), -pos.x, false, -1, false, 0.9 * speed, EASE_LERP);
    ease_value(REFN(&card->n.T->y), -pos.y, false, -1, false, 0.9 * speed, EASE_LERP);
    SplashSuckCtx *c = CTX(SplashSuckCtx);
    c->card = card; c->pitch = i * 0.007 + 0.6; c->i = i;
    add_event(EVENT(.blockable = 0, .func = ev_splash_suck, .ctx = c));
    return true;
}

static bool ev_splash_finish(Event *e, void *p) {
    (void)e; (void)p;
    if (G.SPLASH_BACK) node_remove(&G.SPLASH_BACK->n);
    G.SPLASH_BACK = G.SPLASH_FRONT;
    G.SPLASH_FRONT = NULL;
    game_main_menu("splash");
    return true;
}

static double splash_vort_offset;

static bool ev_splash_setup(Event *e, void *p) {
    (void)e; (void)p;
    G.TIMERS.TOTAL = 0;
    G.TIMERS.REAL = 0;
    G.SPLASH_BACK = sprite_new(-30, -13, G.ROOM->T->w + 60, G.ROOM->T->h + 22, atlas_get("ui_1"), SPOS(2, 0));
    splash_vort_offset = fmod(2 * 90.15315131 * (double)time(NULL), 100000);
    ShaderSend bs[6] = {
        { .name = "time", .ref = REFN(&G.TIMERS.REAL) },
        { .name = "vort_speed", .kind = 0, .val = { 1 } },
        { .name = "colour_1", .ref = REFCOL(&G.C.BLUE) },
        { .name = "colour_2", .ref = REFCOL(&G.C.WHITE) },
        { .name = "mid_flash", .kind = 0, .val = { 0 } },
        { .name = "vort_offset", .kind = 0, .val = { (float)splash_vort_offset } },
    };
    DrawStep bstep = { .shader = "splash", .send = bs, .nsend = 6 };
    Sprite_define_draw_steps(G.SPLASH_BACK, &bstep, 1);
    Moveable_set_alignment(&G.SPLASH_BACK->n, ALIGN_ARGS(.major = G.ROOM_ATTACH, .type = "cm", .has_offset = true, .offset = { 0, 0 }));

    G.SPLASH_FRONT = sprite_new(0, -20, G.ROOM->T->w * 2, G.ROOM->T->h * 4, atlas_get("ui_1"), SPOS(2, 0));
    ShaderSend fs[2] = {
        { .name = "time", .ref = REFN(&G.TIMERS.REAL) },
        { .name = "mid_flash", .kind = 0, .val = { 1 } },
    };
    DrawStep fstep = { .shader = "flash", .send = fs, .nsend = 2 };
    Sprite_define_draw_steps(G.SPLASH_FRONT, &fstep, 1);
    Moveable_set_alignment(&G.SPLASH_FRONT->n, ALIGN_ARGS(.major = G.ROOM_ATTACH, .type = "cm", .has_offset = true, .offset = { 0, 0 }));

    splash_sc = NULL;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 0.2, .func = ev_splash_spawn));
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1.8, .func = ev_splash_dissolve));

    G.vortex_time = G.TIMERS.REAL;
    double temp_del = 3;
    for (int i = 1; i <= 200; i++) {
        SplashIdx *c = CTX(SplashIdx); c->i = i;
        add_event(EVENT(.trigger = TRIG_AFTER, .blockable = 0, .delay = temp_del, .func = ev_splash_card, .ctx = c));
        temp_del += MAX(1.0 / i, MAX(0.2 * (170 - i) / 500, 0.016));
    }
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = 2., .func = ev_splash_finish));
    return true;
}

void game_splash_screen(void) {
    if (str_eq(G.SETTINGS.skip_splash, "Yes")) {
        game_main_menu(NULL);
        return;
    }
    game_prep_stage(STAGE_MAIN_MENU, STATE_SPLASH, true);
    add_event(EVENT(.func = ev_discover_nothing));
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_splash_setup));
}

/* ------------------------------------------------------------------ */
/* Game:main_menu                                                      */
/* ------------------------------------------------------------------ */
typedef struct { Card *replace; int ctx; } MenuCtx;   /* ctx: 0 nil, 1 splash, 2 game */
typedef struct { Card *replace; Card **card; Center **pool; int n; } UnlockShowCtx;

static double menu_mid_flash;

static bool ev_unlock_notify(Event *e, void *p) { (void)e; (void)p; unlock_notify(); return true; }

static bool ev_menu_materialize(Event *e, void *p) {
    (void)e;
    MenuCtx *c = p;
    Col w[2] = { G.C.WHITE, G.C.WHITE };
    c->replace->n.states.visible = true;
    if (c->ctx == 1) {
        Card_start_materialize(c->replace, w, 2, true, 2.5);
        sound_play("whoosh1", math_random() * 0.1 + 0.3, 0.3);
        sound_play(tfmt("crumple%d", math_random_range(1, 5)), math_random() * 0.2 + 0.6, 0.65);
    } else {
        Card_start_materialize(c->replace, w, 2, false, 1.2);
    }
    G.VIBRATION += 1;
    return true;
}

static bool ev_menu_logo(Event *e, void *p) {
    (void)e;
    MenuCtx *c = p;
    sound_play(c->ctx == 1 ? "magic_crumple2" : "magic_crumple3", c->ctx == 1 ? 1 : 1.3, 0.9);
    sound_play("whoosh1", 0.4, 0.8);
    if (G.SPLASH_LOGO) ease_value(REFN(&G.SPLASH_LOGO->n.dissolve), -1, false, -1, false, c->ctx == 1 ? 2.3 : 0.9, EASE_LERP);
    G.VIBRATION += 1.5;
    return true;
}

static bool ev_menu_unlock_swap(Event *e, void *p) {
    (void)e;
    UnlockShowCtx *c = p;
    double s = 1.1 * (G.debug_splash_size_toggle ? 0.8 : 1);
    Center *pick = c->n > 0 ? c->pool[math_random_range(1, c->n) - 1] : P_CENTER("j_joker");
    Card *card = card_new(G.title_top->n.T->x, G.title_top->n.T->y, 1.2 * G.CARD_W * s, 1.2 * G.CARD_H * s, NULL, pick, NULL);
    card->no_ui = c->n == 0;
    card->n.states.visible = false;
    *c->card = card;
    c->replace->n.parent = NULL;
    Card_start_dissolve(c->replace, NULL, 0, false, NAN, false);
    return true;
}

static bool ev_menu_unlock_emplace(Event *e, void *p) {
    (void)e;
    UnlockShowCtx *c = p;
    if (*c->card) {
        Card_start_materialize(*c->card, NULL, 0, false, NAN);
        CardArea_emplace(G.title_top, *c->card, NULL, false);
    }
    return true;
}

static bool ev_unlock_input(Event *e, void *p) { (void)e; (void)p; G.CONTROLLER.lock_input = false; return true; }
static bool ev_main_menu_ui(Event *e, void *p) { (void)e; (void)p; set_main_menu_UI(); return true; }
static bool ev_menu_tallies(Event *e, void *p) {
    (void)e; (void)p;
    set_discover_tallies();
    set_profile_progress();
    G.REFRESH_ALERTS = true;
    return true;
}

void game_main_menu(const char *change_context) {
    int ctx = str_eq(change_context, "splash") ? 1 : str_eq(change_context, "game") ? 2 : 0;
    if (ctx != 1) {
        G.TIMERS.REAL = 12;
        G.TIMERS.TOTAL = 12;
    } else {
        sound_reset_states(STATE_MENU);
    }

    game_prep_stage(STAGE_MAIN_MENU, STATE_MENU, true);
    G.GAME.selected_back = back_new(P_CENTER("b_red"));

    if (!G.SETTINGS.tutorial_complete && dv_truthy(dv_get(dv_get(&G.SETTINGS.tutorial_progress, "completed_parts"), "big_blind")))
        G.SETTINGS.tutorial_complete = true;

    ease_background_colour((BgColourArgs){ .new_colour = G.C.BLACK, .contrast = 1, .has_contrast = true });

    if (G.SPLASH_FRONT) { node_remove(&G.SPLASH_FRONT->n); G.SPLASH_FRONT = NULL; }
    if (G.SPLASH_BACK) { node_remove(&G.SPLASH_BACK->n); G.SPLASH_BACK = NULL; }
    G.SPLASH_BACK = sprite_new(-30, -13, G.ROOM->T->w + 60, G.ROOM->T->h + 22, atlas_get("ui_1"), SPOS(2, 0));
    Moveable_set_alignment(&G.SPLASH_BACK->n, ALIGN_ARGS(.major = G.ROOM_ATTACH, .type = "cm", .has_offset = true, .offset = { 0, 0 }));
    menu_mid_flash = ctx == 1 ? 1.6 : 0.;
    ease_value(REFN(&menu_mid_flash), -(ctx == 1 ? 1.6 : 0), false, -1, false, 4, EASE_LERP);
    ShaderSend bs[6] = {
        { .name = "time", .ref = REFN(&G.TIMERS.REAL_SHADER) },
        { .name = "vort_speed", .kind = 0, .val = { 0.4f } },
        { .name = "colour_1", .ref = REFCOL(&G.C.RED) },
        { .name = "colour_2", .ref = REFCOL(&G.C.BLUE) },
        { .name = "mid_flash", .ref = REFN(&menu_mid_flash) },
        { .name = "vort_offset", .kind = 0, .val = { 0 } },
    };
    DrawStep bstep = { .shader = "splash", .send = bs, .nsend = 6 };
    Sprite_define_draw_steps(G.SPLASH_BACK, &bstep, 1);

    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_unlock_notify));

    double SC_scale = 1.1 * (G.debug_splash_size_toggle ? 0.8 : 1);
    G.title_top = cardarea_new(0, 0, G.CARD_W, G.CARD_H, CA_ARGS(.card_limit = 1, .has_card_limit = true, .type = "title"));
    G.title_top->name = "title_top";

    Atlas *logo = atlas_get("balatro");
    G.SPLASH_LOGO = sprite_new(0, 0, 13 * SC_scale, 13 * SC_scale * (logo->py / logo->px), logo, SPOS(0, 0));
    Moveable_set_alignment(&G.SPLASH_LOGO->n, ALIGN_ARGS(.major = &G.title_top->n, .type = "cm", .bond = BOND_STRONG, .has_offset = true, .offset = { 0, 0 }));
    DrawStep lstep = { .shader = "dissolve" };
    Sprite_define_draw_steps(G.SPLASH_LOGO, &lstep, 1);
    G.SPLASH_LOGO->n.dissolve_colours[0] = G.C.WHITE;
    G.SPLASH_LOGO->n.dissolve_colours[1] = G.C.WHITE;
    G.SPLASH_LOGO->n.n_dissolve_colours = 2;
    G.SPLASH_LOGO->n.dissolve = 1;

    Card *replace = card_new(G.title_top->n.T->x, G.title_top->n.T->y, 1.2 * G.CARD_W * SC_scale, 1.2 * G.CARD_H * SC_scale,
                             P_CARD("S_A"), P_CENTER("c_base"), NULL);
    CardArea_emplace(G.title_top, replace, NULL, false);
    replace->n.states.visible = false;
    replace->no_ui = true;
    replace->n.ambient_tilt = 0.0;

    MenuCtx *m1 = CTX(MenuCtx); m1->replace = replace; m1->ctx = ctx;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = ctx == 2 ? 1.5 : 0, .blockable = 0, .blocking = 0, .func = ev_menu_materialize, .ctx = m1));
    MenuCtx *m2 = CTX(MenuCtx); m2->replace = replace; m2->ctx = ctx;
    add_event(EVENT(.trigger = TRIG_AFTER, .delay = ctx == 1 ? 1.8 : ctx == 2 ? 2 : 0.5, .blockable = 0, .blocking = 0, .func = ev_menu_logo, .ctx = m2));

    delay(0.1 + (ctx == 1 ? 2 : ctx == 2 ? 1.5 : 0));

    Center *bp = P_CENTER("j_blueprint");
    if (bp && bp->unlocked) {
        static Center **viable;
        static int cap_viable;
        if (cap_viable < G.P_LOCKED.count) { cap_viable = G.P_LOCKED.count; viable = xrealloc(viable, (size_t)cap_viable * sizeof *viable); }
        int nv = 0;
        for (int i = 0; i < G.P_LOCKED.count; i++) {
            Center *v = G.P_LOCKED.items[i];
            if ((str_eq(v->set, "Voucher") || str_eq(v->set, "Joker")) && !v->demo) viable[nv++] = v;
        }
        if (nv > 0) {
            static Card *shown;
            shown = NULL;
            UnlockShowCtx *u = CTX(UnlockShowCtx);
            u->replace = replace; u->card = &shown; u->pool = viable; u->n = nv;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 4.04, .func = ev_menu_unlock_swap, .ctx = u));
            UnlockShowCtx *u2 = CTX(UnlockShowCtx);
            *u2 = *u;
            add_event(EVENT(.trigger = TRIG_AFTER, .delay = 1.04, .func = ev_menu_unlock_emplace, .ctx = u2));
        }
    }

    add_event(EVENT(.func = ev_unlock_input));
    set_screen_positions();

    CardArea_sort(G.title_top, "order");
    CardArea_set_ranks(G.title_top);
    CardArea_align_cards(G.title_top);
    CardArea_hard_set_cards(G.title_top);

    add_event(EVENT(.trigger = TRIG_AFTER, .delay = ctx == 1 ? 4.05 : ctx == 2 ? 3 : 1.5, .blockable = 0, .blocking = 0, .func = ev_main_menu_ui));

    DVal *cs = dv_get(profile(), "career_stats");
    for (int i = 0; i < dv_count(cs); i++) {
        const DKey *k = dv_key(cs, i);
        if (k->is_str) UNLOCK(.type = "career_stat", .statname = k->s);
    }
    UNLOCK(.type = "blind_discoveries");

    add_event(EVENT(.blockable = 0, .func = ev_menu_tallies));

    uibox_new(UIROOT(CFG(.align = "cm", .colour = G.C.UI.TRANSPARENT_DARK),
                     UIT(CFG(.text = (char *)G.VERSION, .scale = 0.3, .colour = G.C.UI.TEXT_LIGHT))),
              UIBCFG(.align = "tri", .has_offset = true, .offset = { 0, 0 }, .major = G.ROOM_ATTACH, .bond = BOND_WEAK));
}

/* ------------------------------------------------------------------ */
/* Game:start_run                                                      */
/* ------------------------------------------------------------------ */
static bool set_starting_param(StartingParams *p, const char *id, double v) {
#define SP(f) if (!strcmp(id, #f)) { p->f = v; return true; }
    SP(dollars); SP(hand_size); SP(discards); SP(hands); SP(reroll_cost); SP(joker_slots); SP(ante_scaling); SP(consumable_slots);
#undef SP
    return false;
}

static void set_modifier(Modifiers *m, const char *id, bool has_value, double v) {
    double val = has_value ? v : 1;
#define MN(f) if (!strcmp(id, #f)) { m->f = val; return; }
#define MB(f) if (!strcmp(id, #f)) { m->f = true; return; }
    MN(scaling); MN(discard_cost); MN(flipped_cards); MN(minus_hand_size_per_X_dollar); MN(money_per_discard); MN(money_per_hand);
    MB(enable_eternals_in_shop); MB(enable_perishables_in_shop); MB(enable_rentals_in_shop); MB(all_eternal);
    MB(booster_ante_scaling); MB(chips_dollar_cap); MB(debuff_played_cards); MB(no_extra_hand_money); MB(no_interest);
    MB(inflation); MB(daily); MB(no_reward); MB(no_shop_jokers);
#undef MN
#undef MB
    if (!strcmp(id, "set_eternal_ante")) { m->has_set_eternal_ante = true; m->set_eternal_ante = val; return; }
    if (!strcmp(id, "set_joker_slots_ante")) { m->has_set_joker_slots_ante = true; m->set_joker_slots_ante = val; return; }
    strset_add(&m->other, id);
}

typedef struct { ChallengeCard *cc; int k; } ChJokerCtx;
static bool ev_ch_joker(Event *e, void *p) {
    (void)e;
    ChJokerCtx *c = p;
    Card *j = add_joker(c->cc->id, c->cc->edition, c->k != 1, false);
    if (j && c->cc->eternal) Card_set_eternal(j, true);
    if (j && c->cc->pinned) j->pinned = true;
    return true;
}
static bool ev_ch_consumeable(Event *e, void *p) {
    (void)e;
    ChJokerCtx *c = p;
    add_joker(c->cc->id, NULL, c->k != 1, false);
    return true;
}
static bool ev_ch_voucher(Event *e, void *p) {
    (void)e;
    ChJokerCtx *c = p;
    G.GAME.starting_voucher_count += 1;
    Card_apply_to_run(NULL, P_CENTER(c->cc->id));
    return true;
}

static bool ev_unlock_load(Event *e, void *p) { (void)e; (void)p; G.CONTROLLER.locks.load = false; return true; }

typedef struct { int card; int stage; } ActionCtx;
static bool ev_action_use(Event *e, void *p) {
    (void)e;
    ActionCtx *c = p;
    if (c->stage < 2) {
        ActionCtx *n = CTX(ActionCtx); *n = *c; n->stage++;
        add_event(EVENT(.func = ev_action_use, .ctx = n));
        return true;
    }
    for (int i = 0; i < G.I.CARD.count; i++) {
        Card *v = G.I.CARD.items[i];
        if (v->sort_id == c->card) {
            static UIElement fake;
            static UIConfig fake_cfg;
            memset(&fake, 0, sizeof fake);
            memset(&fake_cfg, 0, sizeof fake_cfg);
            fake.config = &fake_cfg;
            fake_cfg.ref_table = v;
            use_card(&fake, false, true);
        }
    }
    return true;
}

static bool ev_spin(Event *e, void *p) {
    (void)e; (void)p;
    double _dt = G.ARGS_spin.amount > G.ARGS_spin.eased ? G.real_dt * 2. : 0.3 * G.real_dt;
    double delta = G.ARGS_spin.real - G.ARGS_spin.eased;
    if (fabs(delta) > _dt) delta = delta * _dt / fabs(delta);
    G.ARGS_spin.eased += delta;
    G.ARGS_spin.amount = _dt * G.ARGS_spin.eased + (1 - _dt) * G.ARGS_spin.amount;
    G.TIMERS.BACKGROUND -= 60 * (G.ARGS_spin.eased - G.ARGS_spin.amount) * _dt;
    return false;
}

static char *proto_key(const CardProto *p) {
    return xstrdup(tfmt("%s%s%s%s%s", p->s ? p->s : "", p->r ? p->r : "", p->e ? p->e : "", p->d ? p->d : "", p->g ? p->g : ""));
}
typedef struct { CardProto p; char *key; } KeyedProto;
static int keyed_cmp(const void *a, const void *b) { return strcmp(((const KeyedProto *)a)->key, ((const KeyedProto *)b)->key); }

static bool list_has(const char *const *list, int n, const char *v) {
    for (int i = 0; i < n; i++) if (str_eq(list[i], v)) return true;
    return false;
}

static int playing_card_desc(const void *a, const void *b) {
    const Card *x = *(Card *const *)a, *y = *(Card *const *)b;
    return (y->playing_card > x->playing_card) - (y->playing_card < x->playing_card);
}

void game_start_run(StartRunArgs args) {
    DVal *save = args.savetext;
    if (save && save->t != DV_TABLE) save = NULL;
    dv_free(&G.SAVED_GAME);

    int saved_state = save ? (int)dv_get_num(save, "STATE", STATE_BLIND_SELECT) : STATE_BLIND_SELECT;
    game_prep_stage(STAGE_RUN, saved_state, false);
    G.STAGE = STAGE_RUN;
    if (save) UNLOCK(.type = "continue_game");

    G.STATE_COMPLETE = false;
    G.RESET_BLIND_STATES = true;

    if (!save) ease_background_colour_blind(G.STATE, "Small Blind");
    else {
        const char *bn = dv_get_str(dv_get(save, "BLIND"), "name", "");
        bool blank = true;
        for (const char *q = bn; *q; q++) if (!isspace((unsigned char)*q)) { blank = false; break; }
        ease_background_colour_blind(G.STATE, blank ? "Small Blind" : bn);
    }

    const char *back_name = NULL;
    if (save) back_name = dv_get_str(dv_get(save, "BACK"), "name", NULL);
    if (!back_name && args.challenge && args.challenge->deck.present && args.challenge->deck.type) back_name = args.challenge->deck.type;
    if (!back_name && G.GAME.viewed_back) back_name = G.GAME.viewed_back->name;
    if (!back_name && G.GAME.selected_back) back_name = G.GAME.selected_back->name;
    if (!back_name) back_name = "Red Deck";
    Center *selected_back = get_deck_from_name(back_name);

    Back *prev_viewed = G.GAME.viewed_back;
    game_init_game_object(&G.GAME);
    if (save) gamestate_load(&G.GAME, dv_get(save, "GAME"));
    (void)prev_viewed;
    if (args.stake) G.GAME.stake = args.stake;
    if (!G.GAME.stake) G.GAME.stake = 1;
    G.GAME.STOP_USE = 0;
    G.GAME.selected_back = back_new(selected_back);
    G.GAME.selected_back_key = selected_back;

    col_set(G.C.UI_CHIPS, G.C.BLUE);
    col_set(G.C.UI_MULT, G.C.RED);

    if (!save) {
        Modifiers *m = &G.GAME.modifiers;
        double st = G.GAME.stake;
        if (st >= 2) { m->has_no_blind_reward = true; m->no_blind_reward[BT_SMALL] = true; }
        if (st >= 3) m->scaling = 2;
        if (st >= 4) m->enable_eternals_in_shop = true;
        if (st >= 5) G.GAME.starting_params.discards -= 1;
        if (st >= 6) m->scaling = 3;
        if (st >= 7) m->enable_perishables_in_shop = true;
        if (st >= 8) m->enable_rentals_in_shop = true;

        Back_apply_to_run(G.GAME.selected_back);

        Challenge *ch = args.challenge;
        if (ch) {
            G.GAME.challenge = ch->id;
            G.GAME.challenge_tab = ch;
            for (int k = 0; k < ch->njokers; k++) {
                ChJokerCtx *c = CTX(ChJokerCtx); c->cc = &ch->jokers[k]; c->k = k + 1;
                add_event(EVENT(.func = ev_ch_joker, .ctx = c));
            }
            for (int k = 0; k < ch->nconsumeables; k++) {
                ChJokerCtx *c = CTX(ChJokerCtx); c->cc = &ch->consumeables[k]; c->k = k + 1;
                add_event(EVENT(.func = ev_ch_consumeable, .ctx = c));
            }
            for (int k = 0; k < ch->nvouchers; k++) {
                strset_add(&G.GAME.used_vouchers, ch->vouchers[k].id);
                ChJokerCtx *c = CTX(ChJokerCtx); c->cc = &ch->vouchers[k]; c->k = k + 1;
                add_event(EVENT(.func = ev_ch_voucher, .ctx = c));
            }
            for (int k = 0; k < ch->nmodifiers; k++)
                set_starting_param(&G.GAME.starting_params, ch->modifiers[k].id, ch->modifiers[k].value_num);
            for (int k = 0; k < ch->ncustom; k++) {
                ChallengeRule *v = &ch->custom[k];
                if (str_eq(v->id, "no_reward")) {
                    m->has_no_blind_reward = true;
                    m->no_blind_reward[0] = m->no_blind_reward[1] = m->no_blind_reward[2] = true;
                } else if (str_eq(v->id, "no_reward_specific")) {
                    m->has_no_blind_reward = true;
                    int bt = blind_type_index(v->value_str);
                    if (bt >= 0) m->no_blind_reward[bt] = true;
                } else if (v->has_value) {
                    set_modifier(m, v->id, true, v->value_num);
                } else if (str_eq(v->id, "no_shop_jokers")) {
                    G.GAME.joker_rate = 0;
                } else {
                    set_modifier(m, v->id, false, 0);
                }
            }
            for (int k = 0; k < ch->restrictions.n_banned_cards; k++) {
                ChallengeBan *b = &ch->restrictions.banned_cards[k];
                strset_add(&G.GAME.banned_keys, b->id);
                for (int j = 0; j < b->nids; j++) strset_add(&G.GAME.banned_keys, b->ids[j]);
            }
            for (int k = 0; k < ch->restrictions.n_banned_tags; k++) strset_add(&G.GAME.banned_keys, ch->restrictions.banned_tags[k].id);
            for (int k = 0; k < ch->restrictions.n_banned_other; k++) strset_add(&G.GAME.banned_keys, ch->restrictions.banned_other[k].id);
        }

        G.GAME.round_resets.hands = G.GAME.starting_params.hands;
        G.GAME.round_resets.discards = G.GAME.starting_params.discards;
        G.GAME.round_resets.reroll_cost = G.GAME.starting_params.reroll_cost;
        G.GAME.dollars = G.GAME.starting_params.dollars;
        G.GAME.base_reroll_cost = G.GAME.starting_params.reroll_cost;
        G.GAME.round_resets.reroll_cost = G.GAME.base_reroll_cost;
        G.GAME.current_round.reroll_cost = G.GAME.base_reroll_cost;
    }

    str_set(&G.GAME.chips_text, "");

    DVal *tp = &G.SETTINGS.tutorial_progress;
    if (!save) {
        if (args.seed) G.GAME.seeded = true;
        if (args.seed) str_set(&G.GAME.pseudorandom.seed, args.seed);
        else if (!(G.SETTINGS.tutorial_complete || dv_truthy(dv_get(dv_get(tp, "completed_parts"), "big_blind"))))
            str_set(&G.GAME.pseudorandom.seed, "TUTORIAL");
        else {
            char *s = generate_starting_seed();
            free(G.GAME.pseudorandom.seed);
            G.GAME.pseudorandom.seed = s;
        }
    }
    NumMap *pk = &G.GAME.pseudorandom.keys;
    for (int i = 0; i < pk->cap; i++)
        if (NUMMAP_LIVE(pk, i) && pk->vals[i] == 0) pk->vals[i] = pseudohash(tfmt("%s%s", pk->keys[i], G.GAME.pseudorandom.seed));
    G.GAME.pseudorandom.hashed_seed = pseudohash(G.GAME.pseudorandom.seed);
    G.GAME.pseudorandom.has_hashed_seed = true;

    game_save_settings();

    G.GAME.round_resets.has_blind_tags = true;

    if (!save) {
        G.GAME.round_resets.blind_choices[BT_BOSS] = get_new_boss();
        const char *fv = tp->t == DV_TABLE ? dv_get_str(tp, "forced_voucher", NULL) : NULL;
        G.GAME.current_round.voucher = fv ? intern(fv) : get_next_voucher_key(false);
        DVal *ft = tp->t == DV_TABLE ? dv_get(tp, "forced_tags") : NULL;
        const char *t1 = ft ? dv_geti_str(ft, 1, NULL) : NULL;
        G.GAME.round_resets.blind_tags[0] = t1 ? intern(t1) : get_next_tag_key(NULL);
        const char *t2 = ft ? dv_geti_str(ft, 2, NULL) : NULL;
        G.GAME.round_resets.blind_tags[1] = t2 ? intern(t2) : get_next_tag_key(NULL);
    }

    G.CONTROLLER.locks.load = true;
    add_event(EVENT(.no_delete = true, .trigger = TRIG_AFTER, .blocking = 0, .blockable = 0, .delay = 3.5, .timer = TIMER_TOTAL,
                    .func = ev_unlock_load));

    DVal *action = save ? dv_get(save, "ACTION") : NULL;
    if (action) {
        ActionCtx *c = CTX(ActionCtx); c->card = (int)dv_get_num(action, "card", -1); c->stage = 0;
        add_event(EVENT(.delay = 0.5, .trigger = TRIG_AFTER, .blocking = 0, .blockable = 0, .func = ev_action_use, .ctx = c));
    }

    G.consumeables = cardarea_new(0, 0, 2.3 * G.CARD_W, 0.95 * G.CARD_H,
                                  CA_ARGS(.card_limit = G.GAME.starting_params.consumable_slots, .has_card_limit = true, .type = "joker",
                                          .highlight_limit = 1, .has_highlight_limit = true));
    G.consumeables->name = "consumeables";
    G.jokers = cardarea_new(0, 0, 4.9 * G.CARD_W, 0.95 * G.CARD_H,
                            CA_ARGS(.card_limit = G.GAME.starting_params.joker_slots, .has_card_limit = true, .type = "joker",
                                    .highlight_limit = 1, .has_highlight_limit = true));
    G.jokers->name = "jokers";
    G.discard = cardarea_new(0, 0, G.CARD_W, G.CARD_H, CA_ARGS(.card_limit = 500, .has_card_limit = true, .type = "discard"));
    G.discard->name = "discard";
    G.deck = cardarea_new(0, 0, G.CARD_W * 1.1, 0.95 * G.CARD_H, CA_ARGS(.card_limit = 52, .has_card_limit = true, .type = "deck"));
    G.deck->name = "deck";
    G.hand = cardarea_new(0, 0, 6 * G.CARD_W, 0.95 * G.CARD_H,
                          CA_ARGS(.card_limit = G.GAME.starting_params.hand_size, .has_card_limit = true, .type = "hand"));
    G.hand->name = "hand";
    G.play = cardarea_new(0, 0, 5.3 * G.CARD_W, 0.95 * G.CARD_H, CA_ARGS(.card_limit = 5, .has_card_limit = true, .type = "play"));
    G.play->name = "play";

    vec_clear(&G.playing_cards);
    set_screen_positions();

    G.SPLASH_BACK = sprite_new(-30, -6, G.ROOM->T->w + 60, G.ROOM->T->h + 12, atlas_get("ui_1"), SPOS(2, 0));
    Moveable_set_alignment(&G.SPLASH_BACK->n, ALIGN_ARGS(.major = &G.play->n, .type = "cm", .bond = BOND_STRONG, .has_offset = true, .offset = { 0, 0 }));
    G.ARGS_spin.amount = 0; G.ARGS_spin.real = 0; G.ARGS_spin.eased = 0;
    ShaderSend bs[7] = {
        { .name = "time", .ref = REFN(&G.TIMERS.REAL_SHADER) },
        { .name = "spin_time", .ref = REFN(&G.TIMERS.BACKGROUND) },
        { .name = "colour_1", .ref = REFCOL(&G.C.BACKGROUND.C) },
        { .name = "colour_2", .ref = REFCOL(&G.C.BACKGROUND.L) },
        { .name = "colour_3", .ref = REFCOL(&G.C.BACKGROUND.D) },
        { .name = "contrast", .ref = REFN(&G.C.BACKGROUND.contrast) },
        { .name = "spin_amount", .ref = REFN(&G.ARGS_spin.amount) },
    };
    DrawStep bstep = { .shader = "background", .send = bs, .nsend = 7 };
    Sprite_define_draw_steps(G.SPLASH_BACK, &bstep, 1);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .blocking = 0, .blockable = 0, .func = ev_spin));

    if (save) {
        DVal *areas = dv_get(save, "cardAreas");
        for (int i = 0; i < dv_count(areas); i++) {
            const DKey *k = dv_key(areas, i);
            if (!k->is_str) continue;
            DVal *v = dv_val(areas, i);
            CardArea *a = cardarea_by_name(k->s);
            if (a) CardArea_load(a, v);
            else if (!strcmp(k->s, "shop_jokers")) { dv_free(&G.load_shop_jokers); G.load_shop_jokers = dv_copy(v); G.load_shop_jokers_set = true; }
            else if (!strcmp(k->s, "shop_vouchers")) { dv_free(&G.load_shop_vouchers); G.load_shop_vouchers = dv_copy(v); G.load_shop_vouchers_set = true; }
            else if (!strcmp(k->s, "shop_booster")) { dv_free(&G.load_shop_booster); G.load_shop_booster = dv_copy(v); G.load_shop_booster_set = true; }
            else fprintf(stderr, "ERROR LOADING GAME: Card area '%s' not instantiated before load\n", k->s);
        }
        for (int i = 0; i < G.I.CARD.count; i++) {
            Card *v = G.I.CARD.items[i];
            if (v->playing_card) vec_push(&G.playing_cards, v);
        }
        for (int i = 0; i < G.I.CARDAREA.count; i++) {
            CardArea *a = G.I.CARDAREA.items[i];
            CardArea_align_cards(a);
            CardArea_hard_set_cards(a);
        }
        qsort(G.playing_cards.items, (size_t)G.playing_cards.count, sizeof(void *), playing_card_desc);
    } else {
        Challenge *ch = args.challenge;
        bool de = ch && ch->deck.present;
        KeyedProto *protos = NULL;
        int np = 0, cap = 0;
        if (de && ch->deck.ncards > 0) {
            for (int i = 0; i < ch->deck.ncards; i++) {
                if (np == cap) { cap = cap ? cap * 2 : 64; protos = xrealloc(protos, (size_t)cap * sizeof *protos); }
                protos[np].p = ch->deck.cards[i];
                np++;
            }
        } else {
            for (int i = 0; i < G.n_cards; i++) {
                const char *k = G.P_CARDS_LIST[i].key;
                if (G.GAME.starting_params.erratic_suits_and_ranks) k = pseudorandom_p_card(pseudoseed("erratic"))->key;
                char r[2] = { k[2], 0 }, s[2] = { k[0], 0 };
                bool keep = true;
                const char *e = NULL, *d = NULL, *g = NULL;
                if (de) {
                    if (ch->deck.n_yes_ranks && !list_has(ch->deck.yes_ranks, ch->deck.n_yes_ranks, r)) keep = false;
                    if (ch->deck.n_no_ranks && list_has(ch->deck.no_ranks, ch->deck.n_no_ranks, r)) keep = false;
                    if (ch->deck.n_yes_suits && !list_has(ch->deck.yes_suits, ch->deck.n_yes_suits, s)) keep = false;
                    if (ch->deck.n_no_suits && list_has(ch->deck.no_suits, ch->deck.n_no_suits, s)) keep = false;
                    if (ch->deck.enhancement) e = ch->deck.enhancement;
                    if (ch->deck.edition) d = ch->deck.edition;
                    if (ch->deck.gold_seal) g = ch->deck.gold_seal;
                }
                if (G.GAME.starting_params.no_faces && (r[0] == 'K' || r[0] == 'Q' || r[0] == 'J')) keep = false;
                if (keep) {
                    if (np == cap) { cap = cap ? cap * 2 : 64; protos = xrealloc(protos, (size_t)cap * sizeof *protos); }
                    protos[np].p = (CardProto){ intern(s), intern(r), e, d, g };
                    np++;
                }
            }
        }
        for (int i = 0; i < G.GAME.starting_params.n_extra_cards; i++) {
            if (np == cap) { cap = cap ? cap * 2 : 64; protos = xrealloc(protos, (size_t)cap * sizeof *protos); }
            protos[np].p = G.GAME.starting_params.extra_cards[i];
            np++;
        }
        for (int i = 0; i < np; i++) protos[i].key = proto_key(&protos[i].p);
        /* keys only tie for identical cards, so the unstable order does not matter */
        qsort(protos, (size_t)np, sizeof *protos, keyed_cmp);
        for (int i = 0; i < np; i++) { card_from_control(&protos[i].p); free(protos[i].key); }
        free(protos);
        G.GAME.starting_deck_size = G.playing_cards.count;
    }

    delay(0.5);

    if (!save) {
        G.GAME.current_round.discards_left = G.GAME.round_resets.discards;
        G.GAME.current_round.hands_left = G.GAME.round_resets.hands;
        CardArea_shuffle(G.deck, NULL);
        CardArea_hard_set_cards(G.deck);
        reset_idol_card();
        reset_mail_rank();
        G.GAME.current_round.ancient_card.suit = NULL;
        reset_ancient_card();
        reset_castle_card();
    }

    G.GAME.blind = blind_new(0, 0, 2, 1);
    CardArea_align_cards(G.deck);
    CardArea_hard_set_cards(G.deck);

    G.HUD = uibox_new(create_UIBox_HUD(), UIBCFG(.align = "cli", .has_offset = true, .offset = { -0.7, 0 }, .major = G.ROOM_ATTACH));
    G.HUD_blind = uibox_new(create_UIBox_HUD_blind(),
                            UIBCFG(.major = &UIBox_get_UIE_by_ID(G.HUD, "row_blind", NULL)->n, .align = "cm", .has_offset = true,
                                   .offset = { 0, -10 }, .bond = BOND_WEAK));
    vec_clear(&G.HUD_tags);

    G.hand_text_area.chips = UIBox_get_UIE_by_ID(G.HUD, "hand_chips", NULL);
    G.hand_text_area.mult = UIBox_get_UIE_by_ID(G.HUD, "hand_mult", NULL);
    G.hand_text_area.ante = UIBox_get_UIE_by_ID(G.HUD, "ante_UI_count", NULL);
    G.hand_text_area.round = UIBox_get_UIE_by_ID(G.HUD, "round_UI_count", NULL);
    G.hand_text_area.chip_total = UIBox_get_UIE_by_ID(G.HUD, "hand_chip_total", NULL);
    G.hand_text_area.handname = UIBox_get_UIE_by_ID(G.HUD, "hand_name", NULL);
    G.hand_text_area.hand_level = UIBox_get_UIE_by_ID(G.HUD, "hand_level", NULL);
    G.hand_text_area.game_chips = UIBox_get_UIE_by_ID(G.HUD, "chip_UI_count", NULL);
    G.hand_text_area.blind_chips = UIBox_get_UIE_by_ID(G.HUD_blind, "HUD_blind_count", NULL);
    G.hand_text_area.blind_spacer = UIBox_get_UIE_by_ID(G.HUD, "blind_spacer", NULL);

    check_and_set_high_score("most_money", G.GAME.dollars);

    if (save) {
        Blind_load(G.GAME.blind, dv_get(save, "BLIND"));
        vec_clear(&G.GAME.tags);
        DVal *tags = dv_get(save, "tags");
        for (int i = 1; i <= dv_len(tags); i++) {
            Tag *t = tag_new("tag_uncommon", false, NULL);
            Tag_load(t, dv_geti(tags, i));
            add_tag(t);
        }
    } else {
        Blind_set_blind(G.GAME.blind, NULL, false, true);
        reset_blinds();
    }

    if (G.hand_text_area.blind_chips) G_FUNCS_blind_chip_UI_scale(G.hand_text_area.blind_chips);
    UIBox_recalculate(G.HUD);

    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_unlock_notify));

    if (args.savetext) { dv_free(args.savetext); free(args.savetext); }
}

/* ------------------------------------------------------------------ */
/* love.resize                                                         */
/* ------------------------------------------------------------------ */
void game_resize(int w, int h) {
    double W = w, H = h;
    if (W / H < 1) H = W / 1;
    if (W / H < G.window_prev.orig_ratio) G.TILESCALE = G.window_prev.orig_scale * W / G.window_prev.w;
    else G.TILESCALE = G.window_prev.orig_scale * H / G.window_prev.h;

    if (G.ROOM) {
        G.ROOM->T->w = G.TILE_W;
        G.ROOM->T->h = G.TILE_H;
        G.ROOM_ATTACH->T->w = G.TILE_W;
        G.ROOM_ATTACH->T->h = G.TILE_H;
        if (W / H < G.window_prev.orig_ratio) {
            G.ROOM->T->x = G.ROOM_PADDING_W;
            G.ROOM->T->y = (H / (G.TILESIZE * G.TILESCALE) - (G.ROOM->T->h + G.ROOM_PADDING_H)) / 2 + G.ROOM_PADDING_H / 2;
        } else {
            G.ROOM->T->y = G.ROOM_PADDING_H;
            G.ROOM->T->x = (W / (G.TILESIZE * G.TILESCALE) - (G.ROOM->T->w + G.ROOM_PADDING_W)) / 2 + G.ROOM_PADDING_W / 2;
        }
        G.ROOM_ORIG.x = G.ROOM->T->x;
        G.ROOM_ORIG.y = G.ROOM->T->y;
        G.ROOM_ORIG.r = G.ROOM->T->r;
        if (G.buttons) UIBox_recalculate(G.buttons);
        if (G.HUD) UIBox_recalculate(G.HUD);
    }

    G.WINDOWTRANS.x = 0; G.WINDOWTRANS.y = 0;
    G.WINDOWTRANS.w = G.TILE_W + 2 * G.ROOM_PADDING_W;
    G.WINDOWTRANS.h = G.TILE_H + 2 * G.ROOM_PADDING_H;
    G.WINDOWTRANS.real_window_w = W;
    G.WINDOWTRANS.real_window_h = H;

    G.CANV_SCALE = 1;
    /* the canvas covers the whole window (h may have been clamped above) */
    int cw = w, ch = h;
    if (!G.CANVAS || gfx_canvas_width(G.CANVAS) != cw || gfx_canvas_height(G.CANVAS) != ch) {
        if (G.CANVAS) gfx_free_canvas(G.CANVAS);
        G.CANVAS = gfx_new_canvas((int)(cw * G.CANV_SCALE), (int)(ch * G.CANV_SCALE));
    }
}

/* ------------------------------------------------------------------ */
/* hold 'r' to restart (engine/controller.lua)                         */
/* ------------------------------------------------------------------ */
void game_quick_restart(void) {
    if (!G.GAME.won && !G.GAME.seeded && !G.GAME.challenge) {
        DVal *cs = dv_get(dv_get(profile(), "high_scores"), "current_streak");
        if (cs) dv_set_num(cs, "amt", 0);
    }
    game_save_settings();
    str_set(&G.SETTINGS.current_setup, "New Run");
    G.GAME.viewed_back = NULL;
    G.run_setup_seed = G.GAME.seeded;
    G.challenge_tab = G.GAME.challenge ? G.GAME.challenge_tab : NULL;
    free(G.forced_seed); G.forced_seed = NULL;
    free(G.setup_seed); G.setup_seed = NULL;
    if (G.GAME.seeded) G.forced_seed = xstrdup(G.GAME.pseudorandom.seed);
    G.forced_stake = G.GAME.stake;
    if (G.STAGE == STAGE_RUN) G_FUNCS_start_setup_run(NULL);
    G.forced_stake = 0;
    G.challenge_tab = NULL;
    free(G.forced_seed); G.forced_seed = NULL;
}
