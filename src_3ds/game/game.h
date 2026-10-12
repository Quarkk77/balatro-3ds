/* game.h - game level types and the global state `G` (port of game.lua,
 * globals.lua, card.lua, cardarea.lua, blind.lua, back.lua, tag.lua,
 * card_character.lua and functions/*.lua). */
#ifndef GAME_H
#define GAME_H

#include "../engine/engine.h"
typedef struct LocVars LocVars;

/* ------------------------------------------------------------------ */
/* Enums                                                               */
/* ------------------------------------------------------------------ */
enum {
    STATE_NONE = -1,
    STATE_SELECTING_HAND = 1, STATE_HAND_PLAYED = 2, STATE_DRAW_TO_HAND = 3, STATE_GAME_OVER = 4,
    STATE_SHOP = 5, STATE_PLAY_TAROT = 6, STATE_BLIND_SELECT = 7, STATE_ROUND_EVAL = 8,
    STATE_TAROT_PACK = 9, STATE_PLANET_PACK = 10, STATE_MENU = 11, STATE_TUTORIAL = 12,
    STATE_SPLASH = 13, STATE_SANDBOX = 14, STATE_SPECTRAL_PACK = 15, STATE_DEMO_CTA = 16,
    STATE_STANDARD_PACK = 17, STATE_BUFFOON_PACK = 18, STATE_NEW_ROUND = 19,
};
enum { STAGE_MAIN_MENU = 1, STAGE_RUN = 2, STAGE_SANDBOX = 3 };

/* poker hands in G.handlist order */
enum {
    HAND_FLUSH_FIVE = 0, HAND_FLUSH_HOUSE, HAND_FIVE_OF_A_KIND, HAND_STRAIGHT_FLUSH, HAND_FOUR_OF_A_KIND,
    HAND_FULL_HOUSE, HAND_FLUSH, HAND_STRAIGHT, HAND_THREE_OF_A_KIND, HAND_TWO_PAIR, HAND_PAIR, HAND_HIGH_CARD,
    HAND_COUNT
};
extern const char *HAND_NAMES[HAND_COUNT];
int hand_index(const char *name);
int rank_order_index(const char *value);   /* 'Ace','2'..'10','Jack','Queen','King' -> 0..12 */          /* -1 if unknown */

/* ------------------------------------------------------------------ */
/* Fonts / languages                                                   */
/* ------------------------------------------------------------------ */
typedef struct FontDef {
    const char *file;
    double render_scale;
    double TEXT_HEIGHT_SCALE;
    Vec2 TEXT_OFFSET;
    double FONTSCALE;
    double squish;
    double DESCSCALE;
    GfxFont *FONT;
} FontDef;

typedef struct Language {
    FontDef *font;
    int font_index;
    const char *label;
    const char *key;
    bool beta;
    const char *button;
    const char *warning[3];
    bool omit;
} Language;

/* ------------------------------------------------------------------ */
/* Prototypes (P_CENTERS, P_CARDS, P_BLINDS, P_TAGS, ...)              */
/* ------------------------------------------------------------------ */
typedef enum { EX_NONE = 0, EX_NUM, EX_STR, EX_TABLE } ExtraKind;

typedef struct {
    ExtraKind kind;
    double num;
    const char *str;
    /* table form */
    double s_mult, mult, size, Xmult, xmult, every, max, min, odds, chips, chip_mod, dollars, h_size, h_mod;
    double hand_add, discard_sub, d_remaining, faces, increase, h_plays, discards, destroy;
    const char *suit, *poker_hand;
    const char *remaining;
} Extra;

typedef struct {
    double mult, h_mult, h_x_mult, h_dollars, p_dollars, t_mult, t_chips, Xmult, h_size, d_size, bonus;
    bool has_Xmult, has_bonus;
    Extra extra;
    const char *type;
    /* consumables */
    double max_highlighted, min_highlighted, mod_num;
    const char *mod_conv, *suit_conv, *hand_type;
    bool softlock, remove_card;
    double planets, tarots;
    /* boosters */
    double choose;
    /* backs */
    double discards, hands, dollars, extra_hand_bonus, extra_discard_bonus, joker_slot, consumable_slot;
    double spectral_rate, hand_size, ante_scaling, reroll_discount, edition_count;
    bool no_interest, remove_faces, randomize_rank_suit;
    const char *voucher;
    const char *consumables[4]; int n_consumables;
    const char *vouchers[4]; int n_vouchers;
    const char *edition;
    /* vouchers */
    double extra_disp;
} CenterConfig;

typedef struct {
    const char *type;
    ExtraKind extra_kind;
    double extra_num;
    const char *extra_str;
    /* extra table form */
    double count; const char *enhancement, *e_key, *suit, *tally; bool polychrome;
    bool hidden;
    double ante, n_rounds, chips, amount, tarot_count, planet_count, stake;
    const char *deck;
    bool present;
} UnlockCondition;

typedef struct Center {
    const char *key;
    double order;
    bool has_order;
    bool unlocked, discovered, alerted, start_alerted;
    bool unlocked_set;         /* `unlocked` field present in the definition */
    bool blueprint_compat, perishable_compat, eternal_compat;
    int rarity;
    double cost;
    bool has_cost;
    const char *name;
    SpritePos pos;
    SpritePos soul_pos; bool has_soul_pos;
    const char *set;
    const char *effect;
    double cost_mult;
    CenterConfig config;
    UnlockCondition unlock_condition;
    const char *enhancement_gate;
    const char *no_pool_flag, *yes_pool_flag;
    bool consumeable;
    double freq;
    bool hidden;
    bool available;
    const char *requires[4]; int n_requires;
    double stake;
    const char *atlas;
    const char *kind;
    double weight;
    double max;
    const char *line;
    const char *label;
    bool omit, demo, wip, skip_pool;
    int index;                 /* position in G.P_CENTERS_LIST */
} Center;

typedef struct PCard {
    const char *key;
    const char *name;
    const char *value;
    const char *suit;
    SpritePos pos;
    bool empty;                /* G.P_CARDS.empty */
    const char *atlas;
    double order; bool has_order;
    bool no_ui;
    const char *label;
} PCard;

typedef struct {
    const char *suit;
    double h_size_ge, h_size_le;
    const char *hand;
    const char *is_face;
    const char *value;
    double nominal;
} BlindDebuff;

typedef struct BlindProto {
    const char *key;
    const char *name;
    bool defeated, discovered, alerted;
    double order, dollars, mult;
    const char *vars[2]; int nvars;
    const char *debuff_text;
    BlindDebuff debuff;
    SpritePos pos;
    bool has_boss;
    struct { double min, max; bool showdown; } boss;
    Col boss_colour;
} BlindProto;

typedef struct {
    const char *type;
    double odds, dollars, dollars_per_hand, dollars_per_discard, h_size, spawn_jokers, skip_bonus, levels, max;
    const char *edition;
} TagConfig;

typedef struct TagProto {
    const char *key;
    const char *name;
    const char *set;
    bool discovered, alerted;
    double min_ante; bool has_min_ante;
    double order;
    TagConfig config;
    const char *requires;
    SpritePos pos;
} TagProto;

typedef struct StakeProto {
    const char *key;
    const char *name;
    bool unlocked;
    double order;
    SpritePos pos;
    double stake_level;
    const char *set;
} StakeProto;

typedef struct SealProto {
    const char *key;
    double order;
    bool discovered, alerted;
    const char *set;
} SealProto;

typedef VEC(Center *) CenterVec;

/* ------------------------------------------------------------------ */
/* Challenges                                                          */
/* ------------------------------------------------------------------ */
typedef struct { const char *id; const char *value_str; double value_num; bool has_value; bool value_is_str; } ChallengeRule;
typedef struct { const char *id; const char *edition; bool eternal; bool pinned; } ChallengeCard;
typedef struct { const char *s, *r, *e, *d, *g; } CardProto;   /* suit/rank/enhancement/edition/seal */
typedef struct {
    const char *id;
    const char *type;
    const char *ids[8]; int nids;
} ChallengeBan;
typedef struct Challenge {
    const char *name;
    const char *id;
    ChallengeRule custom[16]; int ncustom;
    ChallengeRule modifiers[16]; int nmodifiers;
    ChallengeCard jokers[8]; int njokers;
    ChallengeCard consumeables[8]; int nconsumeables;
    ChallengeCard vouchers[8]; int nvouchers;
    struct {
        const char *type;
        CardProto *cards; int ncards;
        const char *yes_ranks[16]; int n_yes_ranks;
        const char *no_ranks[16]; int n_no_ranks;
        const char *yes_suits[4]; int n_yes_suits;
        const char *no_suits[4]; int n_no_suits;
        const char *enhancement, *edition, *gold_seal;
        bool present;
    } deck;
    struct {
        ChallengeBan banned_cards[48]; int n_banned_cards;
        ChallengeBan banned_tags[32]; int n_banned_tags;
        ChallengeBan banned_other[16]; int n_banned_other;
    } restrictions;
} Challenge;
extern Challenge G_CHALLENGES[];
extern int G_N_CHALLENGES;

/* ------------------------------------------------------------------ */
/* Card                                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    bool holo, foil, polychrome, negative;
    double mult, chips, x_mult;
    bool has_mult, has_chips, has_x_mult;
    const char *type;
} Edition;
/* edition request {foil=true} etc. */
typedef struct { bool foil, holo, polychrome, negative, any; } EditionReq;
#define ED_NONE ((EditionReq){ 0 })
#define ED_FOIL ((EditionReq){ .foil = true, .any = true })
#define ED_HOLO ((EditionReq){ .holo = true, .any = true })
#define ED_POLY ((EditionReq){ .polychrome = true, .any = true })
#define ED_NEG ((EditionReq){ .negative = true, .any = true })
EditionReq edition_req_from_key(const char *key);
EditionReq edition_req_from_edition(const Edition *e);

typedef struct {
    const char *name, *effect, *set;
    double mult, h_mult, h_x_mult, h_dollars, p_dollars, t_mult, t_chips, x_mult, h_size, d_size;
    Extra extra;
    double extra_value;
    const char *type;
    double order; bool has_order;
    bool forced_selection;
    double perma_bonus;
    double bonus;
    CenterConfig *consumeable;       /* points at the center's config (shared, as in Lua) */
    /* per-joker runtime */
    double invis_rounds, caino_xmult, yorick_discards, burnt_hand, loyalty_remaining, hands_played_at_create;
    const char *to_do_poker_hand;
    bool eternal, perishable, rental, couponed, discarded, played_this_ante, perma_debuff, wheel_flipped, queue_negative_removal;
    double perish_tally;
    double money;
    double driver_tally, steel_tally, nine_tally, stone_tally;
    bool has_driver_tally, has_steel_tally, has_nine_tally, has_stone_tally;
    const char *blueprint_compat;
    char *blueprint_compat_ui;
    const char *blueprint_compat_check;
    int booster_pos;
    bool has_invis_rounds, has_caino_xmult, has_yorick_discards, has_loyalty_remaining;
} Ability;

typedef struct {
    const char *name, *suit, *value;
    double nominal, suit_nominal, face_nominal;
    Col colour;
    double times_played;
    int id;
    const char *original_value;
    double suit_nominal_original; bool has_suit_nominal_original;
} CardBase;

typedef struct {
    int playing_card;              /* 0 = nil */
    bool viewed_back;
    bool bypass_discovery_center, bypass_discovery_ui, bypass_lock, discover;
    bool has_bypass_back; SpritePos bypass_back;
} CardParams;

typedef enum { FACE_FRONT = 0, FACE_BACK = 1 } Facing;
typedef enum { FLIP_NONE = 0, FLIP_F2B, FLIP_B2F } Flipping;

typedef struct CardUITable CardUITable;

struct Card {
    Node n;
    int screen3ds;             /* 3DS: screen it was last drawn on (for cards in the discard pile) */
    CardParams params;
    struct {
        PCard *card;
        Center *center;
        const char *center_key;
        const char *card_key;
    } config;
    int playing_card;
    int sort_id;
    bool bypass_discovery_center, bypass_discovery_ui, bypass_lock;
    bool no_ui;
    double base_cost, extra_cost, cost, sell_cost;
    double sell_cost_label; bool sell_cost_label_q;   /* '?' when face down */
    char sell_cost_label_str[32];
    double unique_val;
    Edition *edition;
    CardBase base;
    Ability ability;
    struct { double r, x, y; } discard_pos;
    Facing facing, sprite_facing;
    Flipping flipping;
    CardArea *area;
    bool highlighted;
    bool debuff;
    int rank;
    bool added_to_deck;
    const char *label;
    const char *seal;
    const char *sticker;
    const char *sticker_run;       /* NULL = nil, "NONE" sentinel */
    bool destroyed;
    bool pinned, vortex, no_shadow, greyed, shattered, getting_sliced, vampired, lucky_trigger, opening, from_tag, temp_edition, shop_voucher;
    bool removed;
    CardCharacter *jimbo;
    CardUITable *ability_UIBox_table;
    PtrVec eligible_strength_jokers, eligible_editionless_jokers;
    float back_overlay[4];
    float send_to_shader[2];
    bool hovering;
    double click_timeout_unused;
};

extern const NodeVT CARD_VT;
Card *card_new(double x, double y, double w, double h, PCard *card, Center *center, const CardParams *params);
void Card_update_alert(Card *c);
void Card_set_base(Card *c, PCard *card, bool initial);
void Card_set_sprites(Card *c, Center *center, PCard *front);
void Card_set_ability(Card *c, Center *center, bool initial, bool delay_sprites);
void Card_set_cost(Card *c);
void Card_set_edition(Card *c, EditionReq ed, bool immediate, bool silent);
void Card_set_seal(Card *c, const char *seal, bool silent, bool immediate);
const char *Card_get_seal(Card *c, bool bypass_debuff);
void Card_set_eternal(Card *c, bool v);
void Card_copy_ability(Card *dst, const Card *src);
void Card_set_perishable(Card *c, bool v);
void Card_set_rental(Card *c, bool v);
void Card_set_debuff(Card *c, bool should);
void Card_remove_UI(Card *c);
void Card_change_suit(Card *c, const char *suit);
void Card_add_to_deck(Card *c, bool from_debuff);
void Card_remove_from_deck(Card *c, bool from_debuff);
CardUITable *Card_generate_UIBox_unlock_table(Card *c, bool hidden);
CardUITable *Card_generate_UIBox_ability_table(Card *c);
double Card_get_nominal(Card *c, const char *mod);
int Card_get_id(Card *c);
bool Card_is_face(Card *c, bool from_boss);
const char *Card_get_original_rank(Card *c);
double Card_get_chip_bonus(Card *c);
double Card_get_chip_mult(Card *c);
double Card_get_chip_x_mult(Card *c);
double Card_get_chip_h_mult(Card *c);
double Card_get_chip_h_x_mult(Card *c);
double Card_get_p_dollars(Card *c);
void Card_use_consumeable(Card *c, CardArea *area, Card *copier);
bool Card_can_use_consumeable(Card *c, bool any_state, bool skip_check);
bool Card_check_use(Card *c);
void Card_sell_card(Card *c);
bool Card_can_sell_card(Card *c);
double Card_calculate_dollar_bonus(Card *c);  /* NAN = nil */
void Card_open(Card *c);
void Card_redeem(Card *c);
void Card_apply_to_run(Card *self, Center *center);
void Card_explode(Card *c, Col *colours, int ncolours, double time_fac);
void Card_shatter(Card *c);
void Card_start_dissolve(Card *c, Col *colours, int ncolours, bool silent, double time_fac, bool no_juice);
void Card_start_materialize(Card *c, Col *colours, int ncolours, bool silent, double timefac);
void Card_calculate_rental(Card *c);
void Card_calculate_perishable(Card *c);
bool Card_is_suit(Card *c, const char *suit, bool bypass_debuff, bool flush_calc);
void Card_set_card_area(Card *c, CardArea *area);
void Card_remove_from_area(Card *c);
void Card_align(Card *c);
void Card_flip(Card *c);
void Card_hard_set_T(Node *n, double x, double y, double w, double h);
void Card_draw_layer(Card *c, const char *layer);
void Card_highlight(Card *c, bool v);
void Card_juice_up(Node *n, double scale, double rot_amount);   /* NAN for nil */
UIBoxConfig *Card_align_h_popup(Card *c);
DVal Card_save(Card *c);
void Card_load(Card *c, DVal *t);
static inline bool card_facing_front(Card *c) { return c->facing == FACE_FRONT; }
#define CARD(nd) ((Card *)(nd))
#define AB(c) (&(c)->ability)
bool card_name_is(Card *c, const char *name);

/* calculate_joker context and return values */
typedef struct EvalResult EvalResult;
typedef struct PokerHands PokerHands;

typedef VEC(Card *) CardList;

typedef struct {
    CardArea *cardarea;
    CardList *full_hand;
    CardList *scoring_hand;
    const char *scoring_name;
    PokerHands *poker_hands;
    Card *other_card;
    Card *other_joker;
    bool individual, repetition, repetition_only, end_of_round, before, after, joker_main, game_over;
    bool discard, pre_discard, hook, open_booster, buying_card, selling_self, selling_card, reroll_shop, ending_shop;
    bool skip_blind, skipping_booster, playing_card_added, first_hand_drawn, setting_blind, destroying_card, cards_destroyed;
    bool remove_playing_cards, using_consumeable, debuffed_hand, other_joker_ctx;
    bool edition;              /* eval_card: edition pass */
    int blueprint;
    Card *blueprint_card;
    Card *card;
    Card *consumeable;
    BlindProto *blind;
    CardList *removed;
    CardList *glass_shattered;
    CardList *cards;           /* playing_card_added cards (may contain placeholders) */
    int ncards_added;
    EvalResult **card_effects; int n_card_effects;
} JokerContext;
#define JCTX(...) (&(JokerContext){ __VA_ARGS__ })

struct EvalResult {
    char *message;
    double mult_mod, chip_mod, Xmult_mod, x_mult, h_mult, chips, mult, dollars, repetitions, h_x_mult;
    bool has_mult_mod, has_chip_mod, has_Xmult_mod, has_x_mult, has_h_mult, has_chips, has_mult, has_dollars, has_repetitions, has_h_x_mult;
    Card *card;
    Col colour;
    double delay; bool has_delay;
    bool level_up, saved, remove, effect, has_message;
    struct EvalExtra *extra;
    CardList *playing_cards_created;
    bool edition;              /* edition pass result */
    bool swap;
    double h_dollars; bool has_h_dollars;
    Card *focus;
    bool no_juice;
    bool is_empty;             /* {} */
};
typedef struct EvalExtra {
    char *message;
    Col colour;
    Node *focus;
    void (*func)(void *);
    void *func_ctx;
    double mult_mod, chip_mod, Xmult_mod, swap, x_mult;
    bool has_mult_mod, has_chip_mod, has_Xmult_mod;
    bool has_swap;
    double delay; bool has_delay;
    bool no_juice;
} EvalExtra;

EvalResult *eval_new(void);
EvalResult *Card_calculate_joker(Card *c, JokerContext *ctx);
EvalResult *Card_calculate_seal(Card *c, JokerContext *ctx);
EvalResult *Card_get_end_of_round_effect(Card *c, JokerContext *ctx);
EvalResult *Card_get_edition(Card *c);

/* ------------------------------------------------------------------ */
/* CardArea                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    double card_limit;
    double real_card_limit; bool has_real_card_limit;
    double highlighted_limit;
    double temp_limit;
    double card_count;
    const char *type;
    const char *sort;
    double lr_padding;
    double card_w;
    bool collection;
    bool view_deck;
    double deck_height;
    int thin_draw;
    bool spread;
    double last_poll_size; bool has_last_poll_size;
    const char *draw_layers[2]; int n_draw_layers;
    bool no_card_count;
    double highlight_limit_in;
} CardAreaConfig;

struct CardArea {
    Node n;
    CardAreaConfig config;
    double card_w;
    CardList cards;
    CardList highlighted;
    double shuffle_amt;
    const char *name;              /* key in G (e.g. "hand") for save/load */
};
extern const NodeVT CARDAREA_VT;
typedef struct {
    double card_limit; bool has_card_limit;
    const char *type;
    double highlight_limit; bool has_highlight_limit;
    double card_w;
    const char *sort;
    double lr_padding; bool has_lr_padding;
    bool collection, view_deck, spread, no_card_count;
    double deck_height;
    int thin_draw;
    const char *draw_layer1, *draw_layer2;
} CardAreaArgs;
#define CA_ARGS(...) ((CardAreaArgs){ __VA_ARGS__ })
CardArea *cardarea_new(double x, double y, double w, double h, CardAreaArgs cfg);
void CardArea_emplace(CardArea *a, Card *card, const char *location, bool stay_flipped);
Card *CardArea_remove_card(CardArea *a, Card *card, bool discarded_only);
void CardArea_change_size(CardArea *a, double delta);
bool CardArea_can_highlight(CardArea *a, Card *card);
void CardArea_add_to_highlighted(CardArea *a, Card *card, bool silent);
void CardArea_parse_highlighted(CardArea *a);
void CardArea_remove_from_highlighted(CardArea *a, Card *card, bool force);
void CardArea_unhighlight_all(CardArea *a);
void CardArea_set_ranks(CardArea *a);
void CardArea_align_cards(CardArea *a);
void CardArea_hard_set_cards(CardArea *a);
void CardArea_shuffle(CardArea *a, const char *seed);
void CardArea_sort(CardArea *a, const char *method);
bool CardArea_draw_card_from(CardArea *a, CardArea *from, bool stay_flipped, bool discarded_only);
DVal CardArea_save(CardArea *a);
void CardArea_load(CardArea *a, DVal *t);
void CardArea_release(CardArea *a, Node *dragged);

/* ------------------------------------------------------------------ */
/* Blind                                                               */
/* ------------------------------------------------------------------ */
struct Blind {
    Node n;
    struct { BlindProto *blind; } config;
    double chips;
    char *chip_text;
    Col colour, dark_colour;
    char *loc_debuff_lines[2];
    char *loc_name;
    char *loc_debuff_text;
    const char *name;
    double dollars;
    double sound_pings;
    BlindDebuff debuff;
    SpritePos pos;
    double mult;
    bool disabled;
    double discards_sub; bool has_discards_sub;
    double hands_sub; bool has_hands_sub;
    bool boss;
    bool blind_set;
    int triggered;                /* 0 nil, 1 true, -1 false */
    bool prepped;
    bool hands[HAND_COUNT]; bool has_hands;
    const char *only_hand; bool only_hand_false;
    bool block_play;
    bool hovering;
    Sprite *animatedSprite;
};
extern const NodeVT BLIND_VT;
Blind *blind_new(double x, double y, double w, double h);
void Blind_change_colour(Blind *b, Col col);
void Blind_set_text(Blind *b);
void Blind_set_blind(Blind *b, BlindProto *blind, bool reset, bool silent);
void Blind_alert_debuff(Blind *b, bool first);
const char *Blind_get_loc_debuff_text(Blind *b);
void Blind_defeat(Blind *b, bool silent);
const char *Blind_get_type(Blind *b);
void Blind_disable(Blind *b);
void Blind_wiggle(Blind *b);
void Blind_juice_up(Blind *b, double a, double c);
bool Blind_press_play(Blind *b);
void Blind_modify_hand(Blind *b, CardList *cards, PokerHands *hands, const char *text, double *mult, double *hand_chips, bool *modded);
bool Blind_debuff_hand(Blind *b, CardList *cards, PokerHands *hands, const char *handname, bool check);
void Blind_drawn_to_hand(Blind *b);
bool Blind_stay_flipped(Blind *b, CardArea *area, Card *card);
void Blind_debuff_card(Blind *b, Card *card, bool from_blind);
void Blind_change_dim(Blind *b, double w, double h);
DVal Blind_save(Blind *b);
void Blind_load(Blind *b, DVal *t);

/* ------------------------------------------------------------------ */
/* Back                                                                */
/* ------------------------------------------------------------------ */
typedef struct Back {
    const char *name;
    struct {
        Center *center;
        const char *text_UI;
        CenterConfig config;
    } effect;
    char *loc_name;
    SpritePos pos;
} Back;
Back *back_new(Center *selected);
const char *Back_get_name(Back *b);
UIDef *Back_generate_UI(Back *b, Center *other, double ui_scale, double min_dims, const char *challenge);
void Back_change_to(Back *b, Center *new_back);
DVal Back_save(Back *b);
void Back_load(Back *b, DVal *t);
void Back_trigger_effect(Back *b, const char *context, double *chips, double *mult, bool *changed);
void Back_apply_to_run(Back *b);

/* ------------------------------------------------------------------ */
/* Tag                                                                 */
/* ------------------------------------------------------------------ */
typedef struct Tag {
    const char *key;
    TagConfig config;
    SpritePos pos;
    const char *name;
    double tally;
    bool triggered;
    int ID;
    struct { char *orbital_hand; const char *blind_type; } ability;
    UIBox *HUD_tag;
    Sprite *tag_sprite;
    bool hide_ability;
    bool removed;
} Tag;
typedef struct {
    const char *type;
    CardArea *area;
    Card *card;
    Tag *tag;
} TagContext;
typedef struct {
    double dollars;
    const char *condition;
    SpritePos pos;
    Tag *tag;
    bool valid;
} TagEvalResult;
Tag *tag_new(const char *key, bool for_collection, const char *blind_type);
void Tag_nope(Tag *t);
void Tag_set_ability(Tag *t);
/* returns: 0 nil, 1 truthy; card / eval results via out params */
int Tag_apply_to_run(Tag *t, TagContext ctx, Card **out_card, TagEvalResult *out_eval);
DVal Tag_save(Tag *t);
void Tag_load(Tag *t, DVal *d);
void Tag_juice_up(Tag *t, double s, double r);
UIDef *Tag_generate_UI(Tag *t, double size, Sprite **out_sprite);
Sprite *Tag_get_uibox_table(Tag *t, Sprite *tag_sprite);
void Tag_remove_from_game(Tag *t);
void Tag_remove(Tag *t);

/* ------------------------------------------------------------------ */
/* Card_Character                                                      */
/* ------------------------------------------------------------------ */
struct CardCharacter {
    Node n;
    Card *card;
    Particles *particles;
    UIBox *speech_bubble;
    UIBox *button;
    bool talking;
    int last_said;
    Node *highlight;
    Node *highlight_child;
    UIBoxConfig *button_align, *speech_bubble_align;
};
extern const NodeVT CARDCHAR_VT;
CardCharacter *card_character_new(double x, double y, double w, double h, Center *center);
void CardCharacter_add_button(CardCharacter *c, const char *label, UIFunc button, Col colour, UIFunc update_func, bool snap_to, double yoff);
void CardCharacter_add_speech_bubble(CardCharacter *c, const char *text_key, const char *align, bool quip, LocVars *vars);
void CardCharacter_remove_button(CardCharacter *c);
void CardCharacter_remove_speech_bubble(CardCharacter *c);
void CardCharacter_say_stuff(CardCharacter *c, int n, bool not_first);
void CardCharacter_hard_set_VT(CardCharacter *c);

/* ------------------------------------------------------------------ */
/* Run state (G.GAME)                                                  */
/* ------------------------------------------------------------------ */
typedef struct { const char *label; double amt; bool high_score; } ScoreEntry;
typedef struct {
    bool visible;
    double order, mult, chips, s_mult, s_chips, level, l_mult, l_chips, played, played_this_round;
    struct { const char *card; bool scoring; } example[5];
} HandStat;

typedef struct { double count, order; const char *set; char *order_str; } UsageEntry;

typedef struct {
    struct {
        double chips; char *chip_text;
        double mult; char *mult_text;
        double chip_total; char *chip_total_text;
        char *handname;
        char *handname_text;
        bool chip_text_is_str, mult_text_is_str;   /* chips/mult set to a string (e.g. '-') */
        char *chip_text_override, *mult_text_override;
        char *hand_level;
    } current_hand;
    char *used_packs[2]; int n_used_packs;
    double cards_flipped;
    char *round_text;
    struct { const char *suit, *rank; int id; } idol_card;
    struct { const char *rank; int id; } mail_card;
    struct { const char *suit; } ancient_card;
    struct { const char *suit; } castle_card;
    double hands_left, hands_played, discards_left, discards_used, dollars, reroll_cost, reroll_cost_increase;
    double jokers_purchased, free_rerolls, round_dollars;
    char *dollars_to_be_earned;
    const char *most_played_poker_hand;
    const char *voucher;
} CurrentRound;

typedef struct {
    double hands, discards, reroll_cost;
    double temp_reroll_cost; bool has_temp_reroll_cost;
    double temp_handsize; bool has_temp_handsize;
    double ante, blind_ante;
    const char *blind_states[3];     /* Small, Big, Boss */
    char *loc_blind_states[3];
    const char *blind_choices[3];
    bool boss_rerolled;
    const char *blind_tags[2];       /* Small, Big */
    bool has_blind_tags;
    BlindProto *blind;
} RoundResets;
enum { BT_SMALL = 0, BT_BIG = 1, BT_BOSS = 2 };
int blind_type_index(const char *t);
extern const char *BLIND_TYPE_NAMES[3];

typedef struct {
    bool no_blind_reward[3]; bool has_no_blind_reward;
    double scaling;
    bool enable_eternals_in_shop, enable_perishables_in_shop, enable_rentals_in_shop;
    bool all_eternal, booster_ante_scaling, chips_dollar_cap, debuff_played_cards;
    double discard_cost, flipped_cards, inflation_unused, minus_hand_size_per_X_dollar, money_per_discard, money_per_hand;
    bool no_extra_hand_money, no_interest, inflation;
    double set_eternal_ante, set_joker_slots_ante;
    bool has_set_eternal_ante, has_set_joker_slots_ante;
    bool daily, no_reward, no_shop_jokers;
    StrSet other;                    /* any other custom challenge rule ids */
} Modifiers;

typedef struct {
    double dollars, hand_size, discards, hands, reroll_cost, joker_slots, ante_scaling, consumable_slots;
    bool no_faces, erratic_suits_and_ranks;
    CardProto *extra_cards; int n_extra_cards;
} StartingParams;

typedef struct {
    bool won;
    struct {
        ScoreEntry furthest_ante, furthest_round, hand, poker_hand, new_collection, cards_played, cards_discarded, times_rerolled, cards_purchased;
    } round_scores;
    StrMap joker_usage_unused;
    StrMap consumeable_usage;        /* key -> UsageEntry* */
    StrMap hand_usage;               /* key -> UsageEntry* */
    const char *last_tarot_planet;
    double win_ante;
    double stake;
    Modifiers modifiers;
    StartingParams starting_params;
    StrSet banned_keys;
    double round;
    struct { double normal; } probabilities;
    NumMap bosses_used;
    struct { NumMap keys; char *seed; double hashed_seed; bool has_hashed_seed; } pseudorandom;
    double starting_deck_size;
    double ecto_minus;
    double pack_size;
    double skips;
    double STOP_USE;
    double edition_rate, joker_rate, tarot_rate, planet_rate, spectral_rate, playing_card_rate;
    double consumeable_buffer, joker_buffer;
    double discount_percent;
    double interest_cap, interest_amount;
    double inflation;
    double hands_played, unused_discards;
    double perishable_rounds, rental_rate;
    Blind *blind;
    double chips;
    char *chips_text;
    char *voucher_text;
    double dollars;
    double max_jokers;
    double bankrupt_at;
    double current_boss_streak;
    double base_reroll_cost;
    const char *blind_on_deck;
    const char *sort;
    struct { double dollars; } previous_round;
    PtrVec tags;                     /* Tag* */
    double tag_tally;
    StrSet pool_flags;
    StrSet used_jokers;
    StrSet used_vouchers;
    CurrentRound current_round;
    RoundResets round_resets;
    struct { double next_hands, discards; } round_bonus;
    struct { double joker_max; } shop;
    struct { NumMap suits; double total; } cards_played[13];   /* index by rank order Ace..King */
    HandStat hands[HAND_COUNT];
    /* fields that are created lazily in the Lua */
    Back *selected_back;
    Center *selected_back_key;
    Back *viewed_back;
    const char *challenge;
    Challenge *challenge_tab;
    bool seeded;
    double dollar_buffer;
    struct { double tarot, planet, spectral, tarot_planet, all; bool present; } consumeable_usage_total;
    struct { const char *hands[3]; bool set; } *orbital_choices; int n_orbital_choices;  /* index = ante */
    struct { bool boss; const char *name; bool present; } last_blind;
    StrMap perscribed_bosses_unused;
    const char *last_hand_played;
    char *subhash;
    double first_used_hand_level_unused;
    double starting_voucher_count;
    bool facing_blind;
    int PACK_INTERRUPT;
    bool shop_free, shop_d6ed;
    bool first_shop_buffoon;
    bool win_notified;
    double pack_choices;
    bool voucher_restock_unused;
} GameState;

/* ------------------------------------------------------------------ */
/* Settings / profiles                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    struct { char *name, *prev_name, *submission_name; double score; } COMP;
    struct { double total_uptime; bool timed_CTA_shown, win_CTA_shown, quit_CTA_shown; } DEMO;
    DVal ACHIEVEMENTS_EARNED;
    bool crashreports, colourblind_option;
    char *language;
    double screenshake;                /* -1 = not yet a number (Lua: true) */
    bool run_stake_stickers;
    double rumble;
    double play_button_pos;
    double GAMESPEED;
    bool paused;
    struct { double volume, music_volume, game_sounds_volume; } SOUND;
    struct {
        char *screenmode;
        double vsync;
        int selected_display;
        int selcted_display;           /* sic */
        struct { int w, h; } screen_res;
        DVal DISPLAYS;
    } WINDOW;
    struct { char *Spades, *Hearts, *Clubs, *Diamonds; } CUSTOM_DECK_Collabs;
    struct { double texture_scaling; char *shadows; double crt; double bloom; } GRAPHICS;
    int profile;
    bool tutorial_complete;
    DVal tutorial_progress;            /* dynamic table (nil = DV_NIL) */
    char *version;
    bool reduced_motion;
    char *skip_splash;
    char *current_setup;
    bool perf_mode;
    struct { char *desired_track, *current_track; double lerp; } music_control;
    DVal QUEUED_CHANGE;
    DVal extra;                        /* unknown keys loaded from settings.jkr are kept here */
} Settings;

/* ------------------------------------------------------------------ */
/* Card UI tables (generate_card_ui)                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    bool present;
    const char *card_type;
    bool force_rarity;
    const char *items[8]; int n;
} Badges;
struct CardUITable {
    UIDef *main;                       /* list of rows */
    UIDef *info;                       /* list of info boxes (each a list of rows) */
    UIDef *type;
    UIDef *name;
    Badges badges;
    const char *card_type;
    UIDef **infos; int ninfos, cap_infos; char **info_names;
    bool name_set;                     /* Lua full_UI_table.name ~= nil (UIDef may be NULL for `true`) */
};

typedef struct Tooltip {
    const char *title;
    const char *text[6]; int ntext;
    const char *filler_func_unused;
    Ref ref;                           /* optional dynamic text */
    bool filler;
    UIDef *(*filler_fn)(void *args);
    void *filler_args;
} Tooltip;

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    double real_intensity, change, intensity, timer, intensity_vel;
    Col colour_1, colour_2;
    int sprite_id;
} FlameArgs;

typedef struct Globals {
    const char *VERSION;
    /* feature flags */
    bool F_QUIT_BUTTON, F_SKIP_TUTORIAL, F_BASIC_CREDITS, F_EXTERNAL_LINKS, F_ENABLE_PERF_OVERLAY, F_NO_SAVING;
    bool F_MUTE, F_SOUND_THREAD, F_VIDEO_SETTINGS, F_CTA, F_VERBOSE, F_HTTP_SCORES, F_CRASH_REPORTS, F_NO_ERROR_HAND;
    bool F_SWAP_AB_PIPS, F_SWAP_AB_BUTTONS, F_SWAP_XY_BUTTONS, F_NO_ACHIEVEMENTS, F_ENGLISH_ONLY, F_GUIDE, F_JAN_CTA;
    bool F_HIDE_BG, F_TROPHIES, F_PS4_PLAYSTATION_GLYPHS, F_LOCAL_CLIPBOARD, F_MOBILE_UI, F_HIDE_BETA_LANGS, F_DISCORD;
    double F_RUMBLE, F_SAVE_TIMER;
    const char *F_DISP_USERNAME;
    bool F_STREAMER_EVENT;

    double SEED;
    union { struct { double TOTAL, REAL, REAL_SHADER, UPTIME, BACKGROUND; }; double arr[TIMER_COUNT]; } TIMERS;
    struct { int DRAW, MOVE; } FRAMES;
    struct { double xy, scale, r, max_vel; } exp_times;
    Settings SETTINGS;
    DVal METRICS;
    DVal PROFILES[3];

    double TILESIZE, TILESCALE, TILE_W, TILE_H, DRAW_HASH_BUFF, CARD_W, CARD_H, HIGHLIGHT_H, COLLISION_BUFFER;
    double PITCH_MOD;
    int STAGE, STATE;
    bool STATE_COMPLETE;
    int TAROT_INTERRUPT;
    PtrVec STAGE_OBJECTS[4];
    bool STAGE_OBJECT_INTERRUPT;

    struct { PtrVec NODE, MOVEABLE, SPRITE, UIBOX, POPUP, CARD, CARDAREA, ALERT; } I;
    StrMap ANIMATION_ATLAS, ASSET_ATLAS;
    PtrVec MOVEABLES, ANIMATIONS, DRAW_HASH;
    double MIN_CLICK_DIST, MIN_HOVER_TIME;
    bool DEBUG;
    double ANIMATION_FPS;
    double VIBRATION;
    double CHALLENGE_WINS;
    int ID;
    int REFRESH_FRAME_MAJOR_CACHE;

    /* colours (G.C) */
    struct {
        Col MULT, CHIPS, MONEY, XMULT, FILTER, BLUE, RED, GREEN, PALE_GREEN, ORANGE, IMPORTANT, GOLD, YELLOW, CLEAR, WHITE,
            PURPLE, BLACK, L_BLACK, GREY, CHANCE, JOKER_GREY, VOUCHER, BOOSTER, EDITION, DARK_EDITION, ETERNAL, PERISHABLE, RENTAL;
        struct { Col MAIN, DARK, BOSS_MAIN, BOSS_DARK, BOSS_PALE; } DYN_UI;
        struct { Col Hearts, Diamonds, Spades, Clubs; } SO_1, SO_2, SUITS;
        struct {
            Col TEXT_LIGHT, TEXT_DARK, TEXT_INACTIVE, BACKGROUND_LIGHT, BACKGROUND_WHITE, BACKGROUND_DARK, BACKGROUND_INACTIVE,
                OUTLINE_LIGHT, OUTLINE_LIGHT_TRANS, OUTLINE_DARK, TRANSPARENT_LIGHT, TRANSPARENT_DARK, HOVER;
        } UI;
        struct { Col Default, Enhanced, Joker, Tarot, Planet, Spectral, Voucher; } SET;
        struct { Col Default, Enhanced, Joker, Tarot, Planet, Spectral, Voucher, Edition; } SECONDARY_SET;
        Col RARITY[5];
        struct { Col Small, Big, Boss, won; } BLIND;
        Col HAND_LEVELS[8];
        struct { Col L, D, C; double contrast; } BACKGROUND;
        Col UI_CHIPS, UI_MULT;
        Col STAKES[9];
    } C;

    const char *handlist[HAND_COUNT];
    double CANV_SCALE;
    GfxCanvas *CANVAS;
    GfxCanvas *AA_CANVAS;
    StrMap SHADERS;
    Col BRUTE_OVERLAY;
    double vortex_time;

    /* languages / localisation */
    Language *LANGUAGES; int n_languages;
    Language *LANG;
    FontDef FONTS[9];
    DVal localization;

    /* prototypes */
    Center *P_CENTERS_LIST; int n_centers;
    StrMap P_CENTERS;              /* key -> Center* */
    PCard *P_CARDS_LIST; int n_cards;
    StrMap P_CARDS;
    PCard P_CARD_EMPTY;
    BlindProto *P_BLINDS_LIST; int n_blinds;
    StrMap P_BLINDS;
    TagProto *P_TAGS_LIST; int n_tags;
    StrMap P_TAGS;
    StakeProto *P_STAKES_LIST; int n_stakes;
    SealProto *P_SEALS_LIST; int n_seals;
    StrMap P_SEALS;
    struct {
        CenterVec Booster, Default, Enhanced, Edition, Joker, Tarot, Planet, Tarot_Planet, Spectral, Consumeables, Voucher, Back, Demo;
        VEC(TagProto *) Tag;
        VEC(SealProto *) Seal;
        VEC(StakeProto *) Stake;
    } P_CENTER_POOLS;
    CenterVec P_JOKER_RARITY_POOLS[4];
    CenterVec P_LOCKED;
    Center j_locked, v_locked, c_locked, j_undiscovered, t_undiscovered, p_undiscovered, s_undiscovered, v_undiscovered, booster_undiscovered;
    TagProto tag_undiscovered;
    BlindProto b_undiscovered;

    /* instances */
    Controller CONTROLLER;
    Node *ROOM;
    double ROOM_jiggle;
    Node *ROOM_ATTACH;
    struct { double x, y, r; } ROOM_ORIG;
    double ROOM_PADDING_H, ROOM_PADDING_W;
    struct { double x, y, w, h, real_window_w, real_window_h; } WINDOWTRANS;
    struct { double orig_scale, w, h, orig_ratio; } window_prev;
    Sprite *CURSOR;
    double SPEEDFACTOR;
    GameState GAME;
    double real_dt;

    Sprite *shared_debuff, *shared_soul, *shared_undiscovered_joker, *shared_undiscovered_tarot;
    Sprite *shared_sticker_eternal, *shared_sticker_perishable, *shared_sticker_rental;
    struct { Sprite *White, *Red, *Green, *Black, *Blue, *Purple, *Orange, *Gold; } shared_stickers;
    struct { Sprite *Gold, *Purple, *Red, *Blue; } shared_seals;
    const char *sticker_map[8];
    Sprite *shared_shadow;

    /* card areas */
    CardArea *consumeables, *jokers, *discard, *deck, *hand, *play, *title_top;
    CardArea *shop_jokers, *shop_vouchers, *shop_booster, *pack_cards;
    CardArea *your_collection[8]; int n_your_collection;
    CardArea *load_areas_unused;
    PtrVec playing_cards;              /* Card* */
    int playing_card;
    int sort_id;
    int tagid;

    /* UI boxes */
    UIBox *HUD_buttons;        /* 3DS: Run Info / Options, bottom screen only */
    UIBox *HUD_stats;          /* 3DS: hands, discards, money, ante, round; top screen only (inside G.HUD) */
    UIBox *HUD, *HUD_blind, *buttons, *deck_preview, *shop, *blind_select, *booster_pack, *MAIN_MENU_UI, *GAME_OVER_UI;
    UIBox *collection_alert, *OVERLAY_MENU, *OVERLAY_TUTORIAL, *screenwipe, *round_eval, *boss_warning_text;
    UIBox *achievement_notification, *online_leaderboard, *debug_tools, *profile_button_unused;
    PtrVec HUD_tags;                   /* UIBox* */
    Sprite *SPLASH_BACK, *SPLASH_FRONT, *SPLASH_LOGO;
    const char *debug_force_boss;   /* debug (TEST_NOPE): boss blind key for new runs */
    bool debug_force_nope;          /* debug (TEST_NOPE): The Wheel of Fortune always fails */
    Card *splash_jimbo;        /* 3DS: the opening Jimbo, shown on the bottom screen only */
    UIBox *version_label;      /* 3DS: main menu version text, top screen only */
    UIBox *notice_label;       /* 3DS: main menu affiliation notice, top screen only */
    Particles *booster_pack_sparkles, *booster_pack_meteors, *booster_pack_stars;
    struct {
        UIElement *chips, *mult, *ante, *round, *chip_total, *handname, *hand_level, *game_chips, *blind_chips, *blind_spacer;
    } hand_text_area;

    /* misc flags / runtime state */
    bool under_overlay;
    bool REFRESH_ALERTS;
    bool NO_MOD_CURSOR_STACK;
    bool RESET_BLIND_STATES;
    bool RESET_JIGGLES;
    bool boss_throw_hand;
    bool VIEWING_DECK;
    bool ALERT_ON_SCREEN;
    bool TAROT_INTERRUPT_PULSE;
    bool normal_music_speed;
    bool from_boss_tag;
    bool debug_tooltip_toggle;
    bool load_shop_booster_set, load_shop_jokers_set, load_shop_vouchers_set;
    DVal load_shop_booster, load_shop_jokers, load_shop_vouchers;
    DVal SAVED_GAME;
    const char *video_soundtrack;
    double SPLASH_VOL;
    double screenwipe_amt;
    bool screenglitch;
    const char *prev_small_state, *prev_large_state, *prev_boss_state;
    int ACC_state;
    double ACC;
    double last_materialized; bool has_last_materialized;
    char *orbital_hand;
    const char *forced_seed_unused;
    char *setup_seed;
    char *forced_seed;
    double forced_stake;
    bool run_setup_seed;
    Challenge *challenge_tab;
    bool jimboed;
    struct { double amount, real, eased; } ARGS_spin;
    FlameArgs ARGS_chip_flames, ARGS_mult_flames;
    double ARGS_score_intensity_flames;
    struct { double earned_score, required_score, flames; } ARGS_score_intensity;
    double ARGS_hand_chip_total_UI_set; bool has_ARGS_hand_chip_total_UI_set;
    Col UI_CHIPLICK, UI_MULTLICK;
    Vec2 ARGS_eased_cursor_pos; bool ARGS_eased_cursor_set;
    double ARGS_eased_cursor_sx, ARGS_eased_cursor_sy;
    StrSet ARGS_voucher_tag;
    struct { bool progress, settings, metrics, run, update_queued, force, present; int last_sent_stage; double last_sent_time; bool has_last_sent_time; bool last_sent_pause; } FILE_HANDLER;
    DVal ARGS_save_run;
    struct { double total_uptime_unused; } misc_unused;
    struct {
        struct { double tally, of; } joker_stickers, deck_stakes, challenges;
        bool present;
    } PROGRESS;
    struct {
        struct { double tally, of; } blinds, tags, jokers, consumeables, tarots, planets, spectrals, vouchers, boosters, editions, backs, total;
        bool present;
    } DISCOVER_TALLIES;
    PtrVec E_MANAGER_unused;
    const char *action_unused;
    int action_card;
    bool has_action;
    double FPS_CAP;
    struct { UIBox *UI; } SANDBOX_unused;
    PtrVec view_deck;                  /* CardArea* while viewing the deck */
    Node *tutorial_highlights_unused;
    bool lock_input_unused;
    DVal ACTION_unused;
    struct { const char *section; } tutorial_unused;
    char *ARGS_HIGH_SCORE_RESPONSE_unused;
    UIBox *your_collection_unused;
    Card *ARGS_hook_cards_unused;
    bool recording_mode, video_control;
    bool debug_UI_toggle, debug_splash_size_toggle, debug_background_toggle;
    struct { double dummy; } check_unused;
    int boot_progress_unused;
    bool load_challenge_unused;
    Center *viewed_stake_unused;
    double viewed_stake;
    int setup_stake_unused;
    bool UIDEF_unused;
    /* added while porting UI_definitions/button_callbacks */
    UIBox *blind_prompt_box, *SHOP_SIGN;
    UIBox *blind_select_opts[3];      /* small, big, boss */
    int focused_profile;
    bool round_eval_divider_added;
    UIBox *collection_alerts[11];
    UIBox *PROFILE_BUTTON;
    double JIGGLE_VIBRATION, CURR_VIBRATION;
    Card *sticker_card;
    bool quit_requested;
    Col screenwipe_black, screenwipe_white;
    Card *screenwipecard;
} Globals;

extern Globals G;
/* version shown on the main menu; G.VERSION stays the game's version so saves
 * keep matching the desktop build */
/* 3DS: cards are drawn this much larger than in the original (G.CARD_W/H);
 * the main card areas keep the original widths so the layout does not move */
#define CARD_SCALE_3DS 1.1
#define PORT_VERSION "3DS-b.0.1.4"

PtrVec *instance_list(const char *name);  /* G.I[name] */
CardArea *cardarea_by_name(const char *name);
CardArea **cardarea_slot(const char *name);

/* ------------------------------------------------------------------ */
/* Lookups                                                             */
/* ------------------------------------------------------------------ */
Center *P_CENTER(const char *key);
PCard *P_CARD(const char *key);
BlindProto *P_BLIND(const char *key);
TagProto *P_TAG(const char *key);
SealProto *P_SEAL(const char *key);
Col suit_colour(const char *suit);
Col *set_colour_slot(const char *set, bool secondary);

/* ------------------------------------------------------------------ */
/* misc_functions.lua                                                  */
/* ------------------------------------------------------------------ */
void pseudoshuffle_cards(CardList *list, double seed);
char *generate_starting_seed(void);
const char *get_first_legendary(const char *key);
double pseudohash(const char *str);
double pseudoseed(const char *key);
double pseudoseed_predict(const char *key, const char *predict_seed);
double pseudorandom(const char *key);                     /* pseudorandom(key) */
double pseudorandom_seed(double seed);                    /* pseudorandom(number) */
int pseudorandom_range(const char *key, int min, int max);
int pseudorandom_index(int n, double seed, bool seeded);
Card *pseudorandom_card(CardList *cards, double seed);           /* pseudorandom_element over cards (sorted by sort_id) */
PCard *pseudorandom_p_card(double seed);                         /* pseudorandom_element(G.P_CARDS, seed) */
char *get_compressed_text(const char *rel);                      /* raw (decompressed) text of a save file */  /* returns 1..n after the Lua sort semantics (array) */
char *random_string(int length, double seed);
Col get_blind_main_colour(const char *blind);
typedef VEC(CardList *) CardListList;
struct PokerHands {
    CardListList h[HAND_COUNT];   /* results[hand] = list of card lists */
    int top;                      /* hand index of results.top, -1 = nil */
};
PokerHands *evaluate_poker_hand(CardList *hand);
bool poker_hand_has(PokerHands *ph, const char *name);
CardList *poker_hand_first(PokerHands *ph, int hand);
void poker_hands_free(PokerHands *ph);
void find_joker(const char *name, bool non_debuff, CardList *out);
bool has_joker(const char *name);
int count_joker(const char *name);
double get_blind_amount(double ante);
const char *number_format(double num);
double score_number_scale(double scale, double amt);
void check_and_set_high_score(const char *score, double amt);
void set_joker_usage(void);
void set_joker_win(void);
double get_joker_win_sticker_index(Center *c);
const char *get_joker_win_sticker(Center *c);
void set_joker_loss(void);
void set_deck_usage(void);
void set_deck_win(void);
void set_challenge_unlock(void);
double get_deck_win_stake(const char *deck_key);
void get_deck_win_stake_all(double *w, double *w_low);
const char *get_deck_win_sticker(Center *c);
void set_deck_loss(void);
void set_consumeable_usage(Card *card);
void set_voucher_usage(Card *card);
void set_hand_usage(const char *hand);
void set_profile_progress(void);
void set_discover_tallies(void);
void stop_use(void);
void dec_stop_use(int depth);
void inc_career_stat(const char *stat, double mod);
void save_with_action(int card_sort_id);
void save_run(void);
void remove_save(void);
Col loc_colour(const char *c, Col def);
void init_localization(void);
void playing_card_joker_effects(CardList *cards, int count);
void convert_save_to_meta(void);
void card_from_control(CardProto *control);
DVal *profile(void);                          /* G.PROFILES[G.SETTINGS.profile] */
Sprite *get_stake_sprite(int stake, double scale);
void get_front_spriteinfo(PCard *front, Atlas **atlas, SpritePos *pos);
Col get_stake_col(int stake);
int get_challenge_int_from_id(const char *id);
StartingParams get_starting_params(void);
ChallengeRule *get_challenge_rule(Challenge *ch, const char *type, const char *id);
Challenge *challenge_by_id(const char *id);
void alert_no_space(Card *card, CardArea *area);
double mod_chips(double c);
double mod_mult(double m);
Atlas *atlas_get(const char *name);

/* localisation */
struct LocVars {
    const char *v[12]; int n;           /* already-stringified variables */
    Col colours[4]; int ncolours;
    bool has_perish_tally; double perish_tally;
    const char *sticker;
    bool debuffed, playing_card, no_name, not_hidden, pinned;
    const char *value, *suit;
    Col colour;
    double nominal_chips, bonus_chips; bool has_nominal_chips, has_bonus_chips;
    bool present;
};
#define LV(...) loc_vars_make((const char *[]){ __VA_ARGS__ }, (int)(sizeof((const char *[]){ __VA_ARGS__ }) / sizeof(const char *)))
LocVars *loc_vars_make(const char **v, int n);
LocVars *loc_vars_new(void);
void loc_vars_add(LocVars *lv, const char *s);
void loc_vars_addn(LocVars *lv, double n);
const char *localize_key(const char *key);                 /* localize('key') */
const char *localize_cat(const char *key, const char *cat); /* localize(key, misc_cat) */
bool localize_has_cat(const char *key, const char *cat);
int localize_list(const char *key, const char *cat, const char **out, int max);   /* list entries */
char *localize_variable(const char *key, LocVars *vars);    /* type='variable' (single line, heap) */
int localize_variable_lines(const char *key, LocVars *vars, char **out, int max);
const char *localize_name_text(const char *set, const char *key);  /* type='name_text' */
int localize_raw_descriptions(const char *set, const char *key, LocVars *vars, char **out, int max);
/* type='descriptions' / 'unlocks' / 'other' / 'tutorial' / 'quips' / 'text' / 'name': appends rows to `nodes`,
 * each row being a UIDef list of parts */
typedef struct {
    const char *type;
    const char *set;
    const char *key;
    LocVars *vars;
    UIDef *nodes;          /* receives rows (UIDef with n = 0 holding parts) */
    bool shadow;
    Col default_col;
    Card *node_card;
    double text_scale;     /* 3DS: extra text size factor (0 = 1) */
} LocArgs;
UIDef *localize_ui(LocArgs args);   /* for 'name'/'text': returns the single row; else NULL (rows in args.nodes) */
DVal *loc_misc(const char *cat);    /* G.localization.misc[cat] */

/* ------------------------------------------------------------------ */
/* common_events.lua                                                   */
/* ------------------------------------------------------------------ */
void set_screen_positions(void);
void ease_chips(double mod);
void ease_dollars(double mod, bool instant);
void ease_discard(double mod, bool instant, bool silent);
void ease_hands_played(double mod, bool instant);
void ease_ante(double mod);
void ease_round(double mod);
void ease_value(Ref ref, double mod, bool floored, int timer_type, bool not_blockable, double delay, EaseType ease_type);
typedef struct { Col new_colour; Col special_colour; Col tertiary_colour; double contrast; bool has_contrast; } BgColourArgs;
void ease_background_colour(BgColourArgs args);
void ease_colour(Col old_colour, const float *new_colour, double delay);
void ease_background_colour_blind(int state, const char *blind_override);
void delay(double time);
void delay_q(double time, QueueKind q);
Card *add_joker(const char *key, const char *edition, bool silent, bool eternal);
void draw_card(CardArea *from, CardArea *to, double percent, const char *dir, bool sort, Card *card, double delay, bool mute, bool stay_flipped, double vol, bool discarded_only);
void highlight_card(Card *card, double percent, const char *dir);
void play_area_status_text(const char *text, bool silent, double delay);
void level_up_hand(Node *card, const char *hand, bool instant, double amount);
typedef struct {
    double delay; bool has_delay;
    bool immediate, nopulse, blocking_set, blocking, blockable_set, blockable;
    const char *sound; double volume, pitch; bool has_volume, has_pitch;
    bool modded;
} HandTextConfig;
typedef struct {
    const char *handname; bool has_handname;
    const char *level; bool has_level;    /* string form */
    double level_n; bool level_is_num;
    double chips; const char *chips_s; bool has_chips;
    double mult; const char *mult_s; bool has_mult;
    bool StatusText;
    double chip_total; bool has_chip_total;
} HandTextVals;
void update_hand_text(HandTextConfig cfg, HandTextVals vals);
#define HTC(...) ((HandTextConfig){ __VA_ARGS__ })
#define HTV(...) ((HandTextVals){ __VA_ARGS__ })
typedef struct {
    double chips, mult, x_mult, p_dollars, h_mult;
    bool has_chips, has_mult, has_x_mult, has_p_dollars, has_h_mult;
    EvalResult *jokers, *edition, *seals;
} CardEval;
CardEval eval_card(Card *card, JokerContext *ctx);
void set_alerts(void);
void set_main_menu_UI(void);
typedef struct {
    const char *message; Col colour; double delay; bool has_delay; bool no_juice; Node *focus; bool instant;
    bool edition, mult_mod, Xmult_mod, chip_mod, swap;
    CardList *playing_cards_created;
} StatusExtra;
void card_eval_status_text(Node *card, const char *eval_type, double amt, double percent, const char *dir, StatusExtra *extra);
#define SEXTRA(...) (&(StatusExtra){ __VA_ARGS__ })
typedef struct {
    const char *name;
    double dollars; bool has_dollars;
    double disp; bool has_disp;
    SpritePos pos;
    bool saved;
    const char *condition;
    Card *card;
    bool bonus;
    double pitch; bool has_pitch;
    Tag *tag;
} RoundEvalRow;
void add_round_eval_row(RoundEvalRow cfg);
void change_shop_size(double mod);
void juice_card(Card *card);
void update_canvas_juice(double dt);
typedef bool (*JuiceEval)(Card *card, void *ctx);
void juice_card_until(Card *card, JuiceEval eval, void *ctx, bool first, double delay);
typedef struct {
    const char *type;
    const char *statname;
    double amount, planet_count, tarot_count, ante, chips, level;
    const char *handname;
    const char *disp_text;
    CardList *scoring_hand;
    CardList *shattered;
    CardList *cards;
    const char *hand_type_unused;
} UnlockArgs;
void check_for_unlock(UnlockArgs args);
#define UNLOCK(...) check_for_unlock((UnlockArgs){ __VA_ARGS__ })
void unlock_card(Center *card);
void fetch_achievements(void);
void unlock_achievement(const char *name);
void notify_alert(const char *key, const char *type);
void inc_steam_stat(const char *stat);
void unlock_notify(void);
UIDef *create_unlock_overlay(const char *key);
void discover_card(Center *card);
void discover_tag(TagProto *t);
void discover_blind(BlindProto *b);
Center *get_deck_from_name(const char *name);
const char *get_next_voucher_key(bool from_tag);
const char *get_next_tag_key(const char *append);
typedef struct { PCard *front; Center *center; } PlayingCardInit;
Card *create_playing_card(PlayingCardInit init, CardArea *area, bool skip_materialize, bool silent, Col *colours, int ncolours);
Center *get_pack(const char *key, const char *type);
typedef struct { const char **keys; int n; const char *pool_key; } PoolResult;
PoolResult get_current_pool(const char *type, double rarity, bool legendary, const char *append);
EditionReq poll_edition(const char *key, double mod, bool no_neg, bool guaranteed);
Card *create_card(const char *type, CardArea *area, bool legendary, double rarity, bool skip_materialize, bool soulable, const char *forced_key, const char *key_append);
#define RARITY_NIL NAN
Card *copy_card(Card *other, Card *new_card, double card_scale, int playing_card, bool strip_edition);
typedef struct {
    const char *text_key;
    const char *align;
    int step;
    AlignArgs attach; bool has_attach;
    Vec2 pos; bool has_pos;
    struct { const char *label; UIFunc func; Col colour; UIFunc update_func; bool set; } button;
    void (*highlight_fn)(PtrVec *out);
    Node *highlight[8]; int nhighlight;
    bool hard_set;
    UIFunc button_listen;
    bool no_button;
    Node *(*snap_to)(void);
    bool quip;
    LocVars *loc_vars;
} TutorialArgs;
int tutorial_info(TutorialArgs args);
#define TUTORIAL(...) tutorial_info((TutorialArgs){ __VA_ARGS__ })
void calculate_reroll_cost(bool skip_increment);
void reset_idol_card(void);
void reset_mail_rank(void);
void reset_ancient_card(void);
void reset_castle_card(void);
void reset_blinds(void);
const char *get_new_boss(void);
Col get_type_colour(Center *c, Card *card);
typedef struct { const char *key, *set; double extra; bool has_extra; LocVars *vars; } InfoItem;   /* generate_card_ui kind 3 */
CardUITable *generate_card_ui(void *center, int center_kind, CardUITable *full, LocVars *specific_vars, const char *card_type,
                              Badges *badges, bool hide_desc, UIDef *main_start, UIDef *main_end);
typedef struct {
    const char *text; double scale; Col colour; double hold; Vec2 pos; const char *align; double emboss;
    Node *cover; Col cover_colour; double cover_padding; const char *cover_align;
    Vec2 offset; bool has_offset; Node *major; double maxw; bool noisy; bool rotate;
    Col backdrop_colour; double backdrop_scale;
} AttentionArgs;
void attention_text(AttentionArgs args);
#define ATT(...) attention_text((AttentionArgs){ __VA_ARGS__ })

/* ------------------------------------------------------------------ */
/* state_events.lua                                                    */
/* ------------------------------------------------------------------ */
void win_game(void);
void end_round(void);
void new_round(void);
bool G_FUNCS_draw_from_deck_to_hand(double e);    /* e: number of cards or NAN */
void G_FUNCS_draw_from_hand_to_deck(void);
void G_FUNCS_draw_from_hand_to_discard(void);
void G_FUNCS_draw_from_discard_to_deck(void);
void G_FUNCS_draw_from_play_to_discard(void);
void G_FUNCS_draw_from_play_to_hand(CardList *cards);
void G_FUNCS_discard_cards_from_highlighted(UIElement *e, bool hook);
void G_FUNCS_play_cards_from_highlighted(UIElement *e);
const char *G_FUNCS_get_poker_hand_info(CardList *cards, const char **disp_text, PokerHands **poker_hands, CardList **scoring_hand, const char **non_loc_disp_text);
void G_FUNCS_evaluate_play(UIElement *e);
void G_FUNCS_evaluate_round(void);
void G_FUNCS_tutorial_controller(void);
void G_FUNCS_tutorial_part(const char *part);

/* ------------------------------------------------------------------ */
/* game.lua                                                            */
/* ------------------------------------------------------------------ */
void game_init(void);
void game_set_globals(void);
void game_start_up(void);
void game_init_item_prototypes(void);
void game_load_profile(int profile);
void game_set_language(void);
void game_set_render_settings(void);
void game_init_window(bool reset);
void game_delete_run(void);
void game_save_progress(void);
void game_save_notify(Center *card);
void game_save_settings(void);
void game_save_metrics(void);
void game_prep_stage(int new_stage, int new_state, bool new_game_obj);
void game_splash_screen(void);
void game_main_menu(const char *change_context);
void game_init_game_object(GameState *g);
typedef struct {
    DVal *savetext;
    Challenge *challenge;
    double stake;
    const char *seed;
} StartRunArgs;
void game_start_run(StartRunArgs args);
void game_update(double dt);
void game_draw(void);
void game_quick_restart(void);
void game_resize(int w, int h);
void game_free_state(GameState *g);
DVal gamestate_save(GameState *g);
void gamestate_load(GameState *g, DVal *d);

/* save manager */
void save_manager_init(void);
void save_manager_progress(void);
void save_manager_settings(void);
void save_manager_metrics(void);
void save_manager_run(DVal *run);
void save_manager_notify(const char *key);
void save_manager_update(void);
DVal settings_to_dval(void);
void settings_from_dval(DVal *d);
bool get_compressed(const char *rel, DVal *out);

/* ------------------------------------------------------------------ */
/* button_callbacks.lua / UI_definitions.lua                           */
/* (declared in ui_defs.h)                                             */
/* ------------------------------------------------------------------ */
/* generated tables (data_protos.c / data_challenges.c) */
extern const int DATA_N_CENTERS; extern const Center DATA_CENTERS[];
extern const Center DATA_j_locked, DATA_v_locked, DATA_c_locked, DATA_j_undiscovered, DATA_t_undiscovered,
    DATA_p_undiscovered, DATA_s_undiscovered, DATA_v_undiscovered, DATA_booster_undiscovered;
extern const int DATA_N_CARDS; extern const PCard DATA_CARDS[];
extern const int DATA_N_BLINDS; extern const BlindProto DATA_BLINDS[]; extern const char *DATA_BLIND_COLOURS[];
extern const BlindProto DATA_b_undiscovered;
extern const int DATA_N_TAGS; extern const TagProto DATA_TAGS[]; extern const TagProto DATA_tag_undiscovered;
extern const int DATA_N_STAKES; extern const StakeProto DATA_STAKES[];
extern const int DATA_N_SEALS; extern const SealProto DATA_SEALS[];
extern const char *DATA_HANDS_PAIRS_ORDER[12];
extern const char *DATA_CARDS_PLAYED_PAIRS_ORDER[13];

#include "ui_defs.h"

#endif
