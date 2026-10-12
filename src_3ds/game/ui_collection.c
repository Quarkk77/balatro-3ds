/* ui_collection.c - "Your Collection" screens, blind popups and unlock
 * overlays from UI_definitions.lua plus their page callbacks. */
#include "game.h"

#define LOC(k) ((char *)localize_key(k))
#define EVFN(name) static bool name(Event *e_, void *p_)

EVFN(ev_refresh_alerts) { (void)e_; (void)p_; G.REFRESH_ALERTS = true; return true; }

static UIFunc back_or(bool exit, UIFunc def) { return exit ? G_FUNCS_exit_overlay_menu : def; }

static CardArea *coll_area(double w, double h, double limit, const char *type, bool collection) {
    return cardarea_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, w, h,
                        CA_ARGS(.card_limit = limit, .has_card_limit = true, .type = type, .highlight_limit = 0, .has_highlight_limit = true,
                                .collection = collection));
}

static void set_collection(int n) {
    G.n_your_collection = n;
    for (int i = n; i < 8; i++) G.your_collection[i] = NULL;
}

static Center *pool_at(CenterVec *pool, int idx1) {
    if (idx1 < 1 || idx1 > pool->count) return NULL;
    return pool->items[idx1 - 1];
}

void init_collection_card_alerts(void) {
    for (int j = 0; j < G.n_your_collection; j++) {
        CardArea *a = G.your_collection[j];
        if (!a) continue;
        for (int i = 0; i < a->cards.count; i++) Card_update_alert(a->cards.items[i]);
    }
}

static void clear_collection(void) {
    for (int j = 0; j < G.n_your_collection; j++) {
        CardArea *a = G.your_collection[j];
        if (!a) continue;
        for (int i = a->cards.count - 1; i >= 0; i--) {
            Card *c = CardArea_remove_card(a, a->cards.items[i], false);
            if (c) node_remove(&c->n);
        }
    }
}

static UIDef *page_options(int npages, const char ***out) {
    const char **opts = xcalloc((size_t)(npages > 0 ? npages : 1), sizeof(char *));
    for (int i = 1; i <= npages; i++) opts[i - 1] = intern(tfmt("%s %d/%d", localize_key("k_page"), i, npages));
    *out = opts;
    return NULL;
}

static UIDef *deck_tables_for(int n, double padding) {
    UIDef *t = uinodes();
    for (int j = 0; j < n; j++)
        uidef_add(t, UIR(CFG(.align = "cm", .padding = padding, .no_fill = true), UIO(CFG(.object = &G.your_collection[j]->n))));
    return t;
}

/* ------------------------------------------------------------------ */
/* main collection menu                                                */
/* ------------------------------------------------------------------ */
UIDef *create_UIBox_your_collection(void) {
    set_discover_tallies();
    add_event(EVENT(.blockable = 0, .func = ev_refresh_alerts));
#define CNT(x) .has_count = true, .count_tally = G.DISCOVER_TALLIES.x.tally, .count_of = G.DISCOVER_TALLIES.x.of
    UIDef *contents = uinodes();
    uidef_add(contents, UIC(CFG(.align = "cm", .padding = 0.15),
        UIBUTTON(.button = G_FUNCS_your_collection_jokers, .label = { localize_key("b_jokers") }, .nlabel = 1, CNT(jokers), .minw = 5, .minh = 1.7, .scale = 0.6,
                 .id = "your_collection_jokers"),
        UIBUTTON(.button = G_FUNCS_your_collection_decks, .label = { localize_key("b_decks") }, .nlabel = 1, CNT(backs), .minw = 5),
        UIBUTTON(.button = G_FUNCS_your_collection_vouchers, .label = { localize_key("b_vouchers") }, .nlabel = 1, CNT(vouchers), .minw = 5, .id = "your_collection_vouchers"),
        UIR(CFG(.align = "cm", .padding = 0.1, .r = 0.2, .colour = G.C.BLACK),
            UIC(CFG(.align = "cm", .maxh = 2.9), UIT(CFG(.text = LOC("k_cap_consumables"), .scale = 0.45, .colour = G.C.L_BLACK, .vert = true, .maxh = 2.2))),
            UIC(CFG(.align = "cm", .padding = 0.15),
                UIBUTTON(.button = G_FUNCS_your_collection_tarots, .label = { localize_key("b_tarot_cards") }, .nlabel = 1, CNT(tarots), .minw = 4,
                         .id = "your_collection_tarots", .colour = G.C.SECONDARY_SET.Tarot),
                UIBUTTON(.button = G_FUNCS_your_collection_planets, .label = { localize_key("b_planet_cards") }, .nlabel = 1, CNT(planets), .minw = 4,
                         .id = "your_collection_planets", .colour = G.C.SECONDARY_SET.Planet),
                UIBUTTON(.button = G_FUNCS_your_collection_spectrals, .label = { localize_key("b_spectral_cards") }, .nlabel = 1, CNT(spectrals), .minw = 4,
                         .id = "your_collection_spectrals", .colour = G.C.SECONDARY_SET.Spectral)))));
    uidef_add(contents, UIC(CFG(.align = "cm", .padding = 0.15),
        UIBUTTON(.button = G_FUNCS_your_collection_enhancements, .label = { localize_key("b_enhanced_cards") }, .nlabel = 1, .minw = 5),
        UIBUTTON(.button = G_FUNCS_your_collection_seals, .label = { localize_key("b_seals") }, .nlabel = 1, .minw = 5, .id = "your_collection_seals"),
        UIBUTTON(.button = G_FUNCS_your_collection_editions, .label = { localize_key("b_editions") }, .nlabel = 1, CNT(editions), .minw = 5, .id = "your_collection_editions"),
        UIBUTTON(.button = G_FUNCS_your_collection_boosters, .label = { localize_key("b_booster_packs") }, .nlabel = 1, CNT(boosters), .minw = 5, .id = "your_collection_boosters"),
        UIBUTTON(.button = G_FUNCS_your_collection_tags, .label = { localize_key("b_tags") }, .nlabel = 1, CNT(tags), .minw = 5, .id = "your_collection_tags"),
        UIBUTTON(.button = G_FUNCS_your_collection_blinds, .label = { localize_key("b_blinds") }, .nlabel = 1, CNT(blinds), .minw = 5, .minh = 2.0,
                 .id = "your_collection_blinds", .focus_args = FOCUS(.snap_to = true))));
#undef CNT
    return GENERIC_OPTIONS(.back_func = G.STAGE == STAGE_RUN ? G_FUNCS_options : G_FUNCS_exit_overlay_menu, .contents = contents);
}

/* ------------------------------------------------------------------ */
/* jokers                                                              */
/* ------------------------------------------------------------------ */
static void joker_page(int page) {
    for (int i = 1; i <= 5; i++)
        for (int j = 1; j <= G.n_your_collection; j++) {
            Center *center = pool_at(&G.P_CENTER_POOLS.Joker, i + (j - 1) * 5 + 5 * G.n_your_collection * (page - 1));
            if (!center) break;
            CardArea *a = G.your_collection[j - 1];
            Card *card = card_new(a->n.T->x + a->n.T->w / 2, a->n.T->y, G.CARD_W, G.CARD_H, page == 1 ? NULL : P_CARD("empty"), center, NULL);
            card->sticker = get_joker_win_sticker(center);
            CardArea_emplace(a, card, NULL, false);
        }
    init_collection_card_alerts();
}
static void your_collection_joker_page(CycleCallbackArgs *a) {
    if (!a || !a->cycle_config) return;
    clear_collection();
    joker_page(a->cycle_config->current_option);
}

static UIDef *collection_page(UIDef *deck_tables, int npages, CycleCallback cb, double minw, bool joker_style, double cycle_padding, bool has_cycle_padding) {
    const char **opts;
    page_options(npages, &opts);
    UIDef *contents = uinodes();
    if (joker_style) uidef_add(contents, uidef_add_all(UIR(CFG(.align = "cm", .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05)), deck_tables));
    else uidef_add(contents, uidef_add_all(UIR(CFG(.align = "cm", .minw = minw, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05)), deck_tables));
    uidef_add(contents, UIR(CFG(.align = "cm", .padding = has_cycle_padding ? cycle_padding : 0),
        OPTION_CYCLE(.options = opts, .noptions = npages, .w = 4.5, .cycle_shoulders = true, .opt_callback = cb, .current_option = 1, .colour = G.C.RED,
                     .no_pips = true, .focus_args = FOCUS(.snap_to = true, .nav = "wide"))));
    return contents;
}

UIDef *create_UIBox_your_collection_jokers(void) {
    set_collection(3);
    for (int j = 0; j < 3; j++) G.your_collection[j] = coll_area(5 * G.CARD_W, 0.95 * G.CARD_H, 5, "title", true);
    UIDef *dt = deck_tables_for(3, 0.07);
    int npages = (int)ceil(G.P_CENTER_POOLS.Joker.count / (5.0 * 3));
    joker_page(1);
    return GENERIC_OPTIONS(.back_func = G_FUNCS_your_collection, .contents = collection_page(dt, npages, your_collection_joker_page, 0, true, 0, false));
}

/* ------------------------------------------------------------------ */
/* tarots / spectrals / planets / boosters / vouchers                  */
/* ------------------------------------------------------------------ */
static void fill_rows(CenterVec *pool, int rows, int (*per_row)(int j), int (*index)(int i, int j, int page), int page, double scale, PCard *front, bool materialize,
                      bool voucher_order) {
    for (int j = 1; j <= rows; j++) {
        for (int i = 1; i <= per_row(j); i++) {
            int idx = index(i, j, page);
            Center *center = pool_at(pool, idx);
            if (!center) break;
            CardArea *a = G.your_collection[j - 1];
            Card *card = card_new(a->n.T->x + a->n.T->w / 2, a->n.T->y, G.CARD_W * scale, G.CARD_H * scale, front, center, NULL);
            if (voucher_order) { card->ability.order = i + (j - 1) * 4; card->ability.has_order = true; }
            if (materialize) Card_start_materialize(card, NULL, 0, i > 1 || j > 1, NAN);
            CardArea_emplace(a, card, NULL, false);
        }
    }
    init_collection_card_alerts();
}

static int tarot_per_row(int j) { return 4 + j; }
static int tarot_index(int i, int j, int page) { return i + (j - 1) * 5 + 11 * (page - 1); }
static void your_collection_tarot_page(CycleCallbackArgs *a) {
    if (!a || !a->cycle_config) return;
    clear_collection();
    fill_rows(&G.P_CENTER_POOLS.Tarot, G.n_your_collection, tarot_per_row, tarot_index, a->cycle_config->current_option, 1, P_CARD("empty"), true, false);
}
UIDef *create_UIBox_your_collection_tarots(void) {
    set_collection(2);
    for (int j = 1; j <= 2; j++) G.your_collection[j - 1] = coll_area((4.25 + j) * G.CARD_W, 1 * G.CARD_H, 4 + j, "title", true);
    UIDef *dt = deck_tables_for(2, 0);
    int npages = (int)floor(G.P_CENTER_POOLS.Tarot.count / 11.0);
    fill_rows(&G.P_CENTER_POOLS.Tarot, 2, tarot_per_row, tarot_index, 1, 1, NULL, true, false);
    return GENERIC_OPTIONS(.back_func = G_FUNCS_your_collection, .contents = collection_page(dt, npages, your_collection_tarot_page, 2.5, false, 0, false));
}

static int booster_per_row(int j) { (void)j; return 4; }
static int booster_index(int i, int j, int page) { return i + (j - 1) * 4 + 8 * (page - 1); }
static void your_collection_booster_page(CycleCallbackArgs *a) {
    if (!a || !a->cycle_config) return;
    clear_collection();
    fill_rows(&G.P_CENTER_POOLS.Booster, G.n_your_collection, booster_per_row, booster_index, a->cycle_config->current_option, 1.27, NULL, true, false);
}
UIDef *create_UIBox_your_collection_boosters(void) {
    set_collection(2);
    for (int j = 1; j <= 2; j++) G.your_collection[j - 1] = coll_area(5.25 * G.CARD_W, 1.3 * G.CARD_H, 4, "title", true);
    UIDef *dt = deck_tables_for(2, 0);
    int npages = (int)ceil(G.P_CENTER_POOLS.Booster.count / 8.0);
    fill_rows(&G.P_CENTER_POOLS.Booster, 2, booster_per_row, booster_index, 1, 1.27, NULL, true, false);
    return GENERIC_OPTIONS(.back_func = G_FUNCS_your_collection, .contents = collection_page(dt, npages, your_collection_booster_page, 2.5, false, 0, false));
}

static int planet_per_row(int j) { (void)j; return 6; }
static int planet_index(int i, int j, int page) { (void)page; return i + (j - 1) * 6; }
UIDef *create_UIBox_your_collection_planets(void) {
    set_collection(2);
    for (int j = 1; j <= 2; j++) G.your_collection[j - 1] = coll_area(6.25 * G.CARD_W, 1 * G.CARD_H, 6, "title", true);
    UIDef *dt = deck_tables_for(2, 0);
    fill_rows(&G.P_CENTER_POOLS.Planet, 2, planet_per_row, planet_index, 1, 1, NULL, true, false);
    UIDef *contents = uinodes();
    uidef_add(contents, uidef_add_all(UIR(CFG(.align = "cm", .minw = 2.5, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05)), dt));
    uidef_add(contents, UIR(CFG(.align = "cm", .padding = 0.7)));
    return GENERIC_OPTIONS(.back_func = G_FUNCS_your_collection, .contents = contents);
}

static int spectral_per_row(int j) { return 3 + j; }
static int spectral_index_init(int i, int j, int page) { (void)page; return i + (j - 1) * 3 + j - 1; }
static int spectral_index_page(int i, int j, int page) { return i + (j - 1) * 4 + 9 * (page - 1); }
static void your_collection_spectral_page(CycleCallbackArgs *a) {
    if (!a || !a->cycle_config) return;
    clear_collection();
    fill_rows(&G.P_CENTER_POOLS.Spectral, G.n_your_collection, spectral_per_row, spectral_index_page, a->cycle_config->current_option, 1, P_CARD("empty"), true, false);
}
UIDef *create_UIBox_your_collection_spectrals(void) {
    set_collection(2);
    for (int j = 1; j <= 2; j++) G.your_collection[j - 1] = coll_area((3.25 + j) * G.CARD_W, 1 * G.CARD_H, 3 + j, "title", true);
    UIDef *dt = deck_tables_for(2, 0);
    fill_rows(&G.P_CENTER_POOLS.Spectral, 2, spectral_per_row, spectral_index_init, 1, 1, NULL, true, false);
    int npages = (int)floor(G.P_CENTER_POOLS.Tarot.count / 9.0);
    const char **opts = xcalloc((size_t)(npages > 0 ? npages : 1), sizeof(char *));
    int shown_of = (int)floor(G.P_CENTER_POOLS.Spectral.count / 9.0);
    for (int i = 1; i <= npages; i++) opts[i - 1] = intern(tfmt("%s %d/%d", localize_key("k_page"), i, shown_of));
    UIDef *contents = uinodes();
    uidef_add(contents, uidef_add_all(UIR(CFG(.align = "cm", .minw = 2.5, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05)), dt));
    uidef_add(contents, UIR(CFG(.align = "cm", .padding = 0),
        OPTION_CYCLE(.options = opts, .noptions = npages, .w = 4.5, .cycle_shoulders = true, .opt_callback = your_collection_spectral_page, .current_option = 1,
                     .colour = G.C.RED, .no_pips = true, .focus_args = FOCUS(.snap_to = true, .nav = "wide"))));
    return GENERIC_OPTIONS(.back_func = G_FUNCS_your_collection, .contents = contents);
}

static void voucher_fill(int page, PCard *front, bool init) {
    for (int i = 1; i <= 4; i++)
        for (int j = 1; j <= G.n_your_collection; j++) {
            Center *center = pool_at(&G.P_CENTER_POOLS.Voucher, i + (j - 1) * 4 + 8 * (page - 1));
            if (!center) break;
            CardArea *a = G.your_collection[j - 1];
            Card *card = card_new(a->n.T->x + a->n.T->w / 2, a->n.T->y, G.CARD_W, G.CARD_H, front, center, NULL);
            if (init) { card->ability.order = i + (j - 1) * 4; card->ability.has_order = true; }
            Card_start_materialize(card, NULL, 0, i > 1 || j > 1, NAN);
            CardArea_emplace(a, card, NULL, false);
        }
    init_collection_card_alerts();
}
static void your_collection_voucher_page(CycleCallbackArgs *a) {
    if (!a || !a->cycle_config) return;
    clear_collection();
    voucher_fill(a->cycle_config->current_option, P_CARD("empty"), false);
}
UIDef *create_UIBox_your_collection_vouchers(bool exit) {
    set_collection(2);
    for (int j = 1; j <= 2; j++) G.your_collection[j - 1] = coll_area(4.25 * G.CARD_W, 1 * G.CARD_H, 4, "voucher", true);
    UIDef *dt = deck_tables_for(2, 0);
    int npages = (int)ceil(G.P_CENTER_POOLS.Voucher.count / 8.0);
    voucher_fill(1, NULL, true);
    return GENERIC_OPTIONS(.back_func = back_or(exit, G_FUNCS_your_collection), .contents = collection_page(dt, npages, your_collection_voucher_page, 2.5, false, 0, false));
}

/* ------------------------------------------------------------------ */
/* seals / enhancements / editions                                     */
/* ------------------------------------------------------------------ */
static const char **seal_infotip(int *n) {
    const char **rows = xcalloc(8, sizeof(char *));
    *n = localize_list("ml_edition_seal_enhancement_explanation", NULL, rows, 8);
    return rows;
}

UIDef *create_UIBox_your_collection_seals(bool exit) {
    set_collection(1);
    G.your_collection[0] = coll_area(4.25 * G.CARD_W, 1.03 * G.CARD_H, 4, "title", false);
    UIDef *dt = deck_tables_for(1, 0);
    for (int k = 0; k < G.P_CENTER_POOLS.Seal.count; k++) {
        SealProto *v = G.P_CENTER_POOLS.Seal.items[k];
        CardArea *a = G.your_collection[0];
        Card *card = card_new(a->n.T->x + a->n.T->w / 2, a->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"), P_CENTER("c_base"), NULL);
        Card_set_seal(card, v->key, true, false);
        CardArea_emplace(a, card, NULL, false);
    }
    int ni; const char **it = seal_infotip(&ni);
    UIDef *contents = uidef_add(uinodes(), uidef_add_all(UIR(CFG(.align = "cm", .minw = 2.5, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05)), dt));
    return GENERIC_OPTIONS(.infotip = it, .ninfotip = ni, .back_func = back_or(exit, G_FUNCS_your_collection), .snap_back = true, .contents = contents);
}

UIDef *create_UIBox_your_collection_enhancements(bool exit) {
    set_collection(2);
    for (int j = 0; j < 2; j++) G.your_collection[j] = coll_area(4.25 * G.CARD_W, 1.03 * G.CARD_H, 4, "title", false);
    UIDef *dt = deck_tables_for(2, 0);
    for (int i = 1; i <= 4; i++)
        for (int j = 1; j <= 2; j++) {
            Center *center = pool_at(&G.P_CENTER_POOLS.Enhanced, i + (j - 1) * 4);
            if (!center) continue;
            CardArea *a = G.your_collection[j - 1];
            Card *card = card_new(a->n.T->x + a->n.T->w / 2, a->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"), center, NULL);
            CardArea_emplace(a, card, NULL, false);
        }
    int ni; const char **it = seal_infotip(&ni);
    UIDef *contents = uidef_add(uinodes(), uidef_add_all(UIR(CFG(.align = "cm", .minw = 2.5, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05)), dt));
    return GENERIC_OPTIONS(.infotip = it, .ninfotip = ni, .back_func = back_or(exit, G_FUNCS_your_collection), .snap_back = true, .contents = contents);
}

UIDef *create_UIBox_your_collection_editions(void) {
    set_collection(1);
    G.your_collection[0] = coll_area(5.3 * G.CARD_W, 1.03 * G.CARD_H, 5, "title", true);
    UIDef *dt = UIR(CFG(.align = "cm", .padding = 0, .no_fill = true), UIO(CFG(.object = &G.your_collection[0]->n)));
    static const char *editions[5] = { "base", "foil", "holo", "polychrome", "negative" };
    for (int i = 0; i < 5; i++) {
        CardArea *a = G.your_collection[0];
        Center *c = P_CENTER(tfmt("e_%s", editions[i]));
        Card *card = card_new(a->n.T->x + a->n.T->w / 2, a->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"), c, NULL);
        Card_start_materialize(card, NULL, 0, false, NAN);
        if (c && c->discovered) Card_set_edition(card, edition_req_from_key(editions[i]), true, true);
        CardArea_emplace(a, card, NULL, false);
    }
    init_collection_card_alerts();
    int ni; const char **it = seal_infotip(&ni);
    UIDef *contents = uidef_add(uinodes(), UIR(CFG(.align = "cm", .minw = 2.5, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05), dt));
    return GENERIC_OPTIONS(.infotip = it, .ninfotip = ni, .back_func = G_FUNCS_your_collection, .snap_back = true, .contents = contents);
}

/* ------------------------------------------------------------------ */
/* decks                                                               */
/* ------------------------------------------------------------------ */
static int cmp_pcard_key(const void *a, const void *b) {
    const PCard *x = *(PCard *const *)a, *y = *(PCard *const *)b;
    return strcmp(x->key, y->key);
}
/* pseudorandom_element(G.P_CARDS) without a seed: sort by key, math.random */
PCard *random_p_card(void) {
    static PCard **sorted; static int n;
    if (!sorted) {
        sorted = xcalloc((size_t)G.n_cards, sizeof(PCard *));
        for (int i = 0; i < G.n_cards; i++) sorted[i] = &G.P_CARDS_LIST[i];
        n = G.n_cards;
        qsort(sorted, (size_t)n, sizeof(PCard *), cmp_pcard_key);
    }
    return sorted[math_random_range(1, n) - 1];
}

static void change_viewed_back(CycleCallbackArgs *a) {
    if (G.viewed_stake == 0) G.viewed_stake = 1;
    Back_change_to(G.GAME.viewed_back, G.P_CENTER_POOLS.Back.items[a->to_key - 1]);
    if (G.sticker_card) G.sticker_card->sticker = get_deck_win_sticker(G.GAME.viewed_back->effect.center);
    double max_stake = get_deck_win_stake(G.GAME.viewed_back->effect.center->key);
    G.viewed_stake = MIN(G.viewed_stake, max_stake + 1);
    DVal *mem = dv_get_or_create_table(profile(), "MEMORY");
    dv_set_str(mem, "deck", a->to_val);
}
CycleCallback cb_change_viewed_back = change_viewed_back;

static UIDef *deck_area_ui(CardArea *area) {
    UIConfig *name_cfg = CFG(.func = G_FUNCS_RUN_SETUP_check_back_name, .object = moveable_new(0, 0, 0, 0));
    UIBox *backui = uibox_new(Back_generate_UI(G.GAME.viewed_back, NULL, NAN, NAN, NULL), UIBCFG(.has_offset = true, .offset = { 0, 0 }));
    return UIR(CFG(.align = "cm", .minw = 2.5, .padding = 0.1, .r = 0.1, .colour = G.C.BLACK, .emboss = 0.05),
        UIR(CFG(.align = "cm", .padding = 0.2, .colour = G.C.BLACK, .r = 0.2),
            UIC(CFG(.align = "cm", .padding = 0), UIO(CFG(.object = &area->n))),
            UIC(CFG(.align = "tm", .minw = 3.7, .minh = 2.1, .r = 0.1, .colour = G.C.L_BLACK, .padding = 0.1),
                UIR(CFG(.align = "cm", .emboss = 0.1, .r = 0.1, .minw = 4, .maxw = 4, .minh = 0.6), UIO(name_cfg)),
                UIR(CFG(.align = "cm", .colour = G.C.WHITE, .emboss = 0.1, .minh = 2.2, .r = 0.1),
                    UIO(CFG(.id = G.GAME.viewed_back->name, .func = G_FUNCS_RUN_SETUP_check_back, .object = &backui->n))))));
}

UIDef *create_UIBox_your_collection_decks(void) {
    G.GAME.viewed_back = back_new(P_CENTER("b_red"));
    CardArea *area = coll_area(1.2 * G.CARD_W, 1.2 * G.CARD_H, 52, "deck", false);
    for (int i = 1; i <= 52; i++) {
        Card *card = card_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W * 1.2, G.CARD_H * 1.2, random_p_card(), P_CENTER("c_base"),
                              &(CardParams){ .playing_card = i, .viewed_back = true });
        card->sprite_facing = FACE_BACK;
        card->facing = FACE_BACK;
        CardArea_emplace(area, card, NULL, false);
        if (i == 52) { G.sticker_card = card; card->sticker = get_deck_win_sticker(G.GAME.viewed_back->effect.center); }
    }
    const char **names = xcalloc((size_t)G.P_CENTER_POOLS.Back.count + 1, sizeof(char *));
    for (int k = 0; k < G.P_CENTER_POOLS.Back.count; k++) names[k] = G.P_CENTER_POOLS.Back.items[k]->name;
    UIDef *contents = uidef_add(uinodes(),
        OPTION_CYCLE(.options = names, .noptions = G.P_CENTER_POOLS.Back.count, .opt_callback = change_viewed_back, .current_option = 1, .colour = G.C.RED, .w = 4.5,
                     .focus_args = FOCUS(.snap_to = true), .mid = deck_area_ui(area)));
    return GENERIC_OPTIONS(.back_func = G_FUNCS_your_collection, .contents = contents);
}

GF_IMPL(RUN_SETUP_check_back) {
    if (!str_eq(G.GAME.viewed_back->name, e->config->id)) {
        if (e->config->object) node_remove(e->config->object);
        UIBox *b = uibox_new(Back_generate_UI(G.GAME.viewed_back, NULL, NAN, NAN, NULL),
                             UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "cm", .parent = &e->n));
        e->config->object = &b->n;
        e->config->id = G.GAME.viewed_back->name;
    }
}

GF_IMPL(RUN_SETUP_check_back_name) {
    if (e->config->object && !str_eq(G.GAME.viewed_back->name, e->config->id)) {
        node_remove(e->config->object);
        UIBox *b = uibox_new(UIROOT(CFG(.align = "cm", .colour = G.C.CLEAR),
                                 UIO(CFG(.id = G.GAME.viewed_back->name, .func = G_FUNCS_RUN_SETUP_check_back_name,
                                         .object = &DYNATEXT(DT_STR(Back_get_name(G.GAME.viewed_back)), .maxw = 4, DT_COL1(G.C.WHITE), .shadow = true, .bump = true,
                                                             .scale = 0.5, .pop_in = 0, .has_pop_in = true, .silent = true)->n))),
                             UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "cm", .parent = &e->n));
        e->config->object = &b->n;
        e->config->id = G.GAME.viewed_back->name;
    }
}

GF_IMPL(RUN_SETUP_check_back_stake_column) {
    if (!str_eq(G.GAME.viewed_back->name, e->config->id)) {
        if (e->config->object) node_remove(e->config->object);
        UIBox *b = uibox_new(UIDEF_deck_stake_column(G.GAME.viewed_back->effect.center->key),
                             UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "cm", .parent = &e->n));
        e->config->object = &b->n;
        e->config->id = G.GAME.viewed_back->name;
    }
}

GF_IMPL(RUN_SETUP_check_back_stake_highlight) {
    bool same = e->config->id && atof(e->config->id) == G.viewed_stake;
    if (same && e->config->outline < 0.1) e->config->outline = 0.8;
    else if (!same && e->config->outline > 0.1) e->config->outline = 0;
}

/* ------------------------------------------------------------------ */
/* tags                                                                */
/* ------------------------------------------------------------------ */
typedef struct { Sprite **list; int n; } AlertList;
EVFN(ev_add_alerts) {
    AlertList *l = p_; (void)e_;
    for (int i = 0; i < l->n; i++) {
        Sprite *v = l->list[i];
        UIBox *b = uibox_new(CARD_ALERT(), UIBCFG(.align = "tri", .has_offset = true, .offset = { 0.1, 0.1 }, .parent = &v->n));
        b->n.states.collide.can = false;
        child_set(&v->n, "alert", &b->n);
    }
    free(l->list);
    return true;
}

static int cmp_tag_order(const void *a, const void *b) {
    const TagProto *x = *(TagProto *const *)a, *y = *(TagProto *const *)b;
    return x->order < y->order ? -1 : x->order > y->order ? 1 : 0;
}

UIDef *create_UIBox_your_collection_tags(void) {
    UIDef *rows[4];
    for (int i = 0; i < 4; i++) rows[i] = UIR(CFG(.align = "cm"));
    TagProto **tab = xcalloc((size_t)G.n_tags, sizeof(TagProto *));
    for (int i = 0; i < G.n_tags; i++) tab[i] = &G.P_TAGS_LIST[i];
    qsort(tab, (size_t)G.n_tags, sizeof(TagProto *), cmp_tag_order);
    AlertList *al = CTX(AlertList);
    al->list = xcalloc((size_t)G.n_tags + 1, sizeof(Sprite *));
    for (int k = 1; k <= G.n_tags; k++) {
        TagProto *v = tab[k - 1];
        bool discovered = v->discovered;
        Tag *temp = tag_new(v->key, true, NULL);
        if (!v->discovered) temp->hide_ability = true;
        Sprite *sp = NULL;
        UIDef *ui = Tag_generate_UI(temp, NAN, &sp);
        int row = (int)ceil((k - 1) / 6.0 + 0.001) - 1;
        if (row >= 0 && row < 4) uidef_add(rows[row], UIC(CFG(.align = "cm", .padding = 0.1), ui));
        if (discovered && !v->alerted) al->list[al->n++] = sp;
    }
    free(tab);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_add_alerts, .ctx = al));
    UIDef *contents = uidef_add(uinodes(),
        UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.BLACK, .padding = 0.1, .emboss = 0.05),
            UIC(CFG(.align = "cm"), UIR(CFG(.align = "cm"), rows[0], rows[1], rows[2], rows[3]))));
    return GENERIC_OPTIONS(.back_func = G_FUNCS_your_collection, .contents = contents);
}

/* ------------------------------------------------------------------ */
/* blinds                                                              */
/* ------------------------------------------------------------------ */
static void blind_sprite_hover(Node *n) {
    Sprite *s = (Sprite *)n;
    BlindProto *v = s->cfg_blind;
    if (!G.CONTROLLER.dragging.target) {
        if (!s->hovering && n->states.visible) {
            s->hovering = true;
            n->hover_tilt = 3;
            node_juice_up(n, 0.05, 0.02);
            sound_play("chips1", math_random() * 0.1 + 0.55, 0.12);
            n->h_popup = create_UIBox_blind_popup(v, v->discovered, NULL);
            n->h_popup_config = UIBCFG(.align = "cl", .has_offset = true, .offset = { -0.1, 0 }, .parent = n);
            Node_hover(n);
            Node *alert = child_get(n, "alert");
            if (alert) {
                child_set(n, "alert", NULL);
                node_remove(alert);
                v->alerted = true;
                game_save_progress();
            }
        }
    }
}
static void blind_sprite_stop_hover(Node *n) {
    ((Sprite *)n)->hovering = false;
    Node_stop_hover(n);
    n->hover_tilt = 0;
}

typedef struct { Node *node; } SnapCtx;
EVFN(ev_snap) { SnapCtx *c = p_; (void)e_; Controller_snap_to_node(&G.CONTROLLER, c->node); return true; }

static int cmp_blind_order(const void *a, const void *b) {
    const BlindProto *x = *(BlindProto *const *)a, *y = *(BlindProto *const *)b;
    return x->order < y->order ? -1 : x->order > y->order ? 1 : 0;
}

UIDef *create_UIBox_your_collection_blinds(bool exit) {
    UIDef *rows[6];
    for (int i = 0; i < 6; i++) rows[i] = UIR(CFG(.align = "cm"));
    BlindProto **tab = xcalloc((size_t)G.n_blinds, sizeof(BlindProto *));
    for (int i = 0; i < G.n_blinds; i++) tab[i] = &G.P_BLINDS_LIST[i];
    qsort(tab, (size_t)G.n_blinds, sizeof(BlindProto *), cmp_blind_order);
    AlertList *al = CTX(AlertList);
    al->list = xcalloc((size_t)G.n_blinds + 1, sizeof(Sprite *));
    for (int k = 1; k <= G.n_blinds; k++) {
        BlindProto *v = tab[k - 1];
        bool discovered = v->discovered;
        Sprite *tb = animated_sprite_new(0, 0, 1.3, 1.3, strmap_get(&G.ANIMATION_ATLAS, "blind_chips"), discovered ? v->pos : G.b_undiscovered.pos);
        DrawStep steps[2] = { { .shader = "dissolve", .shadow_height = 0.05, .has_shadow_height = true }, { .shader = "dissolve" } };
        Sprite_define_draw_steps(tb, steps, 2);
        if (k == 1) { SnapCtx *c = CTX(SnapCtx); c->node = &tb->n; add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_snap, .ctx = c)); }
        tb->float_anim = true;
        tb->n.states.hover.can = true;
        tb->n.states.drag.can = false;
        tb->n.states.collide.can = true;
        tb->cfg_blind = v;
        tb->n.cfg_force_focus = true;
        if (discovered && !v->alerted) al->list[al->n++] = tb;
        tb->n.o_hover = blind_sprite_hover;
        tb->n.o_stop_hover = blind_sprite_stop_hover;
        int row = (int)ceil((k - 1) / 5.0 + 0.001) - 1;
        if (row >= 0 && row < 6)
            uidef_add(rows[row], UIC(CFG(.align = "cm", .padding = 0.1),
                UIIF(k == 6 || k == 16 || k == 26, UIB(CFG(.h = 0.2, .w = 0.5))),
                UIO(CFG(.object = &tb->n, .focus_with_object = true)),
                UIIF(k == 5 || k == 15 || k == 25, UIB(CFG(.h = 0.2, .w = 0.5)))));
    }
    free(tab);
    add_event(EVENT(.trigger = TRIG_IMMEDIATE, .func = ev_add_alerts, .ctx = al));

    double fa = dv_get_num(dv_get(dv_get(profile(), "high_scores"), "furthest_ante"), "amt", 0);
    UIDef *ante_amounts = uinodes();
    int top = (int)MIN(16, MAX(16, fa));
    for (int i = 1; i <= top; i++) {
        double spacing = 1 - MIN(20, MAX(15, fa)) * 0.06;
        if (spacing > 0 && i > 1) uidef_add(ante_amounts, UIR(CFG(.minh = spacing)));
        Sprite *chip = sprite_new(0, 0, 0.2, 0.2, atlas_get(G.SETTINGS.colourblind_option ? "ui_2" : "ui_1"), (SpritePos){ 0, 0 });
        chip->n.states.drag.can = false;
        uidef_add(ante_amounts, UIR(CFG(.align = "cm", .padding = 0.03),
            UIC(CFG(.align = "cm", .minw = 0.7), UIT(CFG(.text = (char *)tfmt("%d", i), .scale = 0.4, .colour = G.C.FILTER, .shadow = true))),
            UIC(CFG(.align = "cr", .minw = 2.8),
                UIO(CFG(.object = &chip->n)),
                UIC(CFG(.align = "cm", .minw = 0.03, .minh = 0.01)),
                UIT(CFG(.text = (char *)number_format(get_blind_amount(i)), .scale = 0.4, .colour = i <= fa ? G.C.RED : G.C.JOKER_GREY, .shadow = true)))));
    }
    UIDef *contents = uidef_add(uinodes(),
        UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.BLACK, .padding = 0.1, .emboss = 0.05),
            UIC(CFG(.align = "cm", .r = 0.1, .colour = G.C.L_BLACK, .padding = 0.1, .force_focus = true, .focus_args = FOCUS(.nav = "tall")),
                UIR(CFG(.align = "cm", .padding = 0.05),
                    UIC(CFG(.align = "cm", .minw = 0.7), UIT(CFG(.text = LOC("k_ante_cap"), .scale = 0.4, .colour = lighten(G.C.FILTER, 0.2), .shadow = true))),
                    UIC(CFG(.align = "cr", .minw = 2.8), UIT(CFG(.text = LOC("k_base_cap"), .scale = 0.4, .colour = lighten(G.C.RED, 0.2), .shadow = true)))),
                uidef_add_all(UIR(CFG(.align = "cm")), ante_amounts)),
            UIC(CFG(.align = "cm"), UIR(CFG(.align = "cm"), rows[0], rows[1], rows[2], rows[3], rows[4], rows[5]))));
    return GENERIC_OPTIONS(.back_func = back_or(exit, G_FUNCS_your_collection), .contents = contents);
}

UIDef *create_UIBox_blind_popup(BlindProto *blind, bool discovered, LocVars *vars) {
    UIDef *blind_text = uinodes();
    char *loc_target[4] = { 0 };
    LocVars *lv = vars;
    if (!lv) {
        lv = loc_vars_new();
        for (int i = 0; i < blind->nvars; i++) loc_vars_add(lv, blind->vars[i]);
    }
    int nt = localize_raw_descriptions("Blind", blind->key, lv, loc_target, 4);
    const char *loc_name = localize_name_text("Blind", blind->key);
    if (discovered) {
        UIDef *ability_text = uinodes();
        for (int k = 0; k < nt; k++)
            uidef_add(ability_text, UIR(CFG(.align = "cm"),
                UIT(CFG(.text = (char *)tfmt("%s%s", (k == 0 && str_eq(blind->name, "The Wheel")) ? "1" : "", loc_target[k]), .scale = 0.35, .shadow = true,
                        .colour = G.C.WHITE))));
        Sprite *stake_sprite = get_stake_sprite(G.GAME.stake ? (int)G.GAME.stake : 1, 0.4);
        StrBuf d; sb_init(&d);
        for (int i = 0; i < (int)blind->dollars; i++) sb_add(&d, localize_key("$"));
        char *dollars = blind->dollars > 0 ? sb_take(&d) : xstrdup("-");
        sb_free(&d);
        Col bc = blind->boss_colour ? blind->boss_colour : G.C.GREY;
        uidef_add(blind_text, UIR(CFG(.align = "cm", .emboss = 0.05, .r = 0.1, .minw = 2.5, .padding = 0.07, .colour = G.C.WHITE),
            UIR(CFG(.align = "cm", .maxw = 2.4), UIT(CFG(.text = LOC("ph_blind_score_at_least"), .scale = 0.35, .colour = G.C.UI.TEXT_DARK))),
            UIR(CFG(.align = "cm"),
                UIO(CFG(.object = &stake_sprite->n)),
                UIT(CFG(.text = (char *)tfmt("%s%s", num_str(blind->mult), localize_key("k_x_base")), .scale = 0.4, .colour = G.C.RED))),
            UIR(CFG(.align = "cm"),
                UIT(CFG(.text = LOC("ph_blind_reward"), .scale = 0.35, .colour = G.C.UI.TEXT_DARK)),
                UIO(CFG(.object = &DYNATEXT(DT_STR(dollars), DT_COL1(G.C.MONEY), .rotate = true, .bump = true, .silent = true, .scale = 0.45)->n))),
            ability_text->count ? uidef_add_all(UIR(CFG(.align = "cm", .padding = 0.08, .colour = mix_colours(bc, G.C.GREY, 0.4), .r = 0.1, .emboss = 0.05,
                                                        .minw = 2.5, .minh = 0.9)), ability_text) : UISKIP));
        free(dollars);
    } else {
        uidef_add(blind_text, UIR(CFG(.align = "cm", .emboss = 0.05, .r = 0.1, .minw = 2.5, .padding = 0.1, .colour = G.C.WHITE),
            UIR(CFG(.align = "cm"), UIT(CFG(.text = LOC("ph_defeat_this_blind_1"), .scale = 0.4, .colour = G.C.UI.TEXT_DARK))),
            UIR(CFG(.align = "cm"), UIT(CFG(.text = LOC("ph_defeat_this_blind_2"), .scale = 0.4, .colour = G.C.UI.TEXT_DARK)))));
    }
    for (int k = 0; k < nt; k++) free(loc_target[k]);
    return UIROOT(CFG(.align = "cm", .padding = 0.05, .colour = lighten(G.C.JOKER_GREY, 0.5), .r = 0.1, .emboss = 0.05),
        UIR(CFG(.align = "cm", .emboss = 0.05, .r = 0.1, .minw = 2.5, .padding = 0.1,
                .colour = !discovered ? G.C.JOKER_GREY : (blind->boss_colour ? blind->boss_colour : G.C.GREY)),
            UIO(CFG(.object = &DYNATEXT(DT_STR(discovered ? loc_name : localize_key("k_not_discovered")), DT_COL1(G.C.UI.TEXT_LIGHT), .shadow = true,
                                        .rotate = !discovered, .spacing = discovered ? 2 : 0, .bump = true, .scale = 0.4)->n))),
        uidef_add_all(UIR(CFG(.align = "cm")), blind_text));
}

/* ------------------------------------------------------------------ */
/* unlock overlays                                                     */
/* ------------------------------------------------------------------ */
typedef struct { Card *card, *locked; int step; } UnlockCtx;
static bool unlock_still(UnlockCtx *c) { return G.OVERLAY_MENU && G.OVERLAY_MENU->joker_unlock_table == c->card->n.ID; }
static void nofree(void *p) { (void)p; }

EVFN(ev_unlock_step) {
    UnlockCtx *c = p_; (void)e_;
    switch (c->step) {
    case 0: if (G.OVERLAY_MENU) G.OVERLAY_MENU->joker_unlock_table = c->card->n.ID; break;
    case 1: if (unlock_still(c)) { node_juice_up(&c->locked->n, 0.3, 0.2); sound_play("cancel", 0.8, NAN); } break;
    case 2: if (unlock_still(c)) { node_juice_up(&c->locked->n, 0.45, 0.3); sound_play("cancel", 0.92, NAN); } break;
    case 3: if (unlock_still(c)) { node_juice_up(&c->locked->n, 0.6, 0.4); sound_play("cancel", 1.03, NAN); } break;
    case 4: if (unlock_still(c)) Card_start_dissolve(c->locked, (Col[]){ G.C.BLACK }, 1, false, NAN, false); break;
    case 5: if (unlock_still(c)) { Card_start_materialize(c->card, (Col[]){ G.C.BLUE }, 1, true, NAN); sound_play("crumple1", 0.8, 1); } break;
    case 6: if (unlock_still(c)) { c->card->no_ui = false; sound_play("timpani", 0.8, 1.8); } break;
    case 7: if (unlock_still(c)) sound_play("timpani", 1, 1.8); break;
    }
    return true;
}

UIDef *create_UIBox_card_unlock(Center *card_center) {
    set_collection(1);
    CardArea *yc = cardarea_new(G.ROOM->T->x + G.ROOM->T->w / 2, G.ROOM->T->h, 1 * G.CARD_W, 1 * G.CARD_H,
                                CA_ARGS(.card_limit = 2, .has_card_limit = true, .type = "consumeable", .highlight_limit = 0, .has_highlight_limit = true));
    G.your_collection[0] = yc;
    Card *card = card_new(yc->n.T->x + yc->n.T->w / 2 - G.CARD_W / 2, yc->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"), card_center,
                          &(CardParams){ .bypass_discovery_center = true, .bypass_discovery_ui = true });
    Card *locked = card_new(yc->n.T->x + yc->n.T->w / 2 - G.CARD_W / 2, yc->n.T->y, G.CARD_W, G.CARD_H, P_CARD("empty"),
                            str_eq(card_center->set, "Voucher") ? &G.v_locked : &G.j_locked, NULL);
    Card_remove_UI(locked);
    locked->n.ID = card->n.ID;
    card->n.states.click.can = false;
    locked->n.states.click.can = false;
    card->n.states.visible = false;
    card->no_ui = true;
    static const double delays[8] = { 0, 0.6, 1.15, 1.8, 2.3, 2.7, 2.78, 2.95 };
    for (int s = 0; s < 8; s++) {
        UnlockCtx *c = CTX(UnlockCtx); c->card = card; c->locked = locked; c->step = s;
        add_event(EVENT(.timer = TIMER_REAL, .blockable = 0, .blocking = 0, .trigger = s ? TRIG_AFTER : TRIG_IMMEDIATE, .delay = delays[s], .func = ev_unlock_step, .ctx = c));
    }
    (void)nofree;
    CardArea_emplace(yc, card, NULL, false);
    CardArea_emplace(yc, locked, NULL, false);
    CardUITable *ut = Card_generate_UIBox_unlock_table(card, true);
    UIDef *contents = uidef_add(uinodes(),
        UIR(CFG(.align = "cm", .padding = 0),
            UIR(CFG(.align = "cm", .padding = 0.1),
                UIR(CFG(.align = "cm", .padding = 0.1),
                    UIR(CFG(.align = "cm", .padding = 0),
                        UIO(CFG(.object = &DYNATEXT(DT_STR(str_eq(card_center->set, "Voucher") ? localize_key("k_voucher") : localize_key("k_joker")),
                                                    DT_COL1(G.C.BLUE), .shadow = true, .rotate = true, .bump = true, .pop_in = 0.3, .has_pop_in = true,
                                                    .pop_in_rate = 2, .scale = 1.2)->n))),
                    UIR(CFG(.align = "cm", .padding = 0),
                        UIO(CFG(.object = &DYNATEXT(DT_STR(localize_key("k_unlocked_ex")), DT_COL1(G.C.RED), .shadow = true, .rotate = true, .bump = true,
                                                    .pop_in = 0.6, .has_pop_in = true, .pop_in_rate = 2, .scale = 0.8)->n)))),
                UIR(CFG(.align = "cm", .padding = 0, .draw_layer = 1), UIO(CFG(.object = &yc->n))),
                UIR(CFG(.align = "cm", .padding = 0.2),
                    UIR(CFG(.align = "cm", .padding = 0.05, .emboss = 0.05, .colour = G.C.WHITE, .r = 0.1),
                        desc_from_rows(ut ? ut->main : NULL, false, 0))))));
    return GENERIC_OPTIONS(.padding = 0, .has_padding = true, .back_label = localize_key("b_continue"), .no_pip = true, .snap_back = true,
                           .back_func = G_FUNCS_continue_unlock, .minw = 4.5, .contents = contents);
}

UIDef *create_UIBox_deck_unlock(Center *deck_center) {
    G.GAME.viewed_back = back_new(deck_center);
    CardArea *area = coll_area(1.2 * G.CARD_W, 1.2 * G.CARD_H, 52, "deck", false);
    for (int i = 1; i <= 52; i++) {
        Card *card = card_new(G.ROOM->T->x + 0.2 * G.ROOM->T->w / 2, G.ROOM->T->h, G.CARD_W * 1.2, G.CARD_H * 1.2, random_p_card(), P_CENTER("c_base"),
                              &(CardParams){ .has_bypass_back = true, .bypass_back = deck_center->pos, .playing_card = i, .viewed_back = true });
        CardArea_emplace(area, card, NULL, false);
        card->sprite_facing = FACE_BACK;
        card->facing = FACE_BACK;
    }
    UIDef *crit = uinodes();
    UnlockCondition *uc = &deck_center->unlock_condition;
    if (str_eq(uc->type, "win_deck")) {
        const char *other = localize_name_text("Back", uc->deck);
        localize_ui((LocArgs){ .type = "descriptions", .key = "deck_locked_win", .set = "Other", .nodes = crit, .vars = LV(other), .default_col = G.C.WHITE, .shadow = true });
    } else if (str_eq(uc->type, "win_stake")) {
        StakeProto *sp = G.P_CENTER_POOLS.Stake.items[(int)uc->stake - 1];
        LocVars *lv = LV(localize_name_text("Stake", sp->key));
        lv->colours[lv->ncolours++] = get_stake_col((int)uc->stake);
        localize_ui((LocArgs){ .type = "descriptions", .key = "deck_locked_stake", .set = "Other", .nodes = crit, .vars = lv, .default_col = G.C.WHITE, .shadow = true });
    } else if (str_eq(uc->type, "discover_amount")) {
        localize_ui((LocArgs){ .type = "descriptions", .key = "deck_locked_discover", .set = "Other", .nodes = crit, .vars = LV(num_str(uc->amount)),
                               .default_col = G.C.WHITE, .shadow = true });
    }
    UIDef *cols = uinodes();
    for (int k = 0; k < crit->count; k++) {
        if (k > 0) uidef_add(cols, UIC(CFG(.align = "cm", .padding = 0, .minw = 0.1)));
        uidef_add(cols, uidef_add_all(UIC(CFG(.align = "cm", .padding = 0)), crit->nodes[k]));
    }
    DynaTextPart *part = dt_parts_str(localize_name_text("Back", deck_center->key));
    part->suffix = xstrdup(tfmt(" %s", localize_key("k_unlocked_ex")));
    part->outer_colour = G.C.UI.TEXT_LIGHT;
    UIDef *bui = Back_generate_UI(G.GAME.viewed_back, deck_center, NAN, NAN, NULL);
    UIDef *contents = uinodes();
    uidef_add(contents, UIR(CFG(.align = "cm", .padding = 0),
        UIO(CFG(.object = &DYNATEXT(.parts = part, .nparts = 1, DT_COL1(G.C.BLUE), .shadow = true, .rotate = true, .float_ = true, .scale = 0.7, .pop_in = 0.1,
                                    .has_pop_in = true)->n))));
    uidef_add(contents, uidef_add_all(UIR(CFG(.align = "cm", .padding = 0)), cols));
    uidef_add(contents, UIR(CFG(.align = "cm", .padding = 0.2, .colour = G.C.BLACK, .r = 0.2),
        UIC(CFG(.align = "cm", .padding = 0), UIO(CFG(.object = &area->n))),
        UIC(CFG(.align = "cm", .r = 0.2, .colour = G.C.WHITE, .emboss = 0.05, .padding = 0.2, .minw = 4),
            uidef_add_all(UIR(CFG(.align = "cm", .padding = 0)), bui))));
    return GENERIC_OPTIONS(.back_label = localize_key("b_continue"), .no_pip = true, .snap_back = true, .back_func = G_FUNCS_continue_unlock, .minw = 7,
                           .contents = contents);
}
