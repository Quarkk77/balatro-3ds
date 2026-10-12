/* ui_defs.h - declarations for UI_definitions.lua and button_callbacks.lua ports */
#ifndef UI_DEFS_H
#define UI_DEFS_H

/* ---- generic builders (UI_definitions.lua) ---- */
typedef struct {
    UIFunc button;          /* NULL -> exit_overlay_menu */
    bool no_button;         /* Lua button = 'nil' */
    UIFunc func;
    Col colour;             /* NULL -> G.C.RED */
    bool choice;
    int chosen;
    const char *label[8]; int nlabel;
    double minw, maxw, minh, scale;   /* 0 -> defaults */
    FocusArgs *focus_args;
    Col text_colour;
    bool col;
    bool shadow;
    void *ref_table;
    bool has_count; double count_tally, count_of;
    double padding;
    bool one_press;
    const char *id;
    void *id_ptr;           /* non-string ids (Lua tables) -> config.data */
    UIFunc back_func;
    bool mid;
    double button_delay;
} UIButtonArgs;
UIDef *UIBox_button(UIButtonArgs args);
#define UIBUTTON(...) UIBox_button((UIButtonArgs){ __VA_ARGS__ })

typedef struct {
    UIFunc back_func;       /* NULL -> exit_overlay_menu */
    UIDef *contents;        /* list (n = 0) of nodes */
    const char **infotip; int ninfotip;
    Col bg_colour, outline_colour, colour, back_colour;
    double padding; bool has_padding;
    double minw;
    bool no_back;
    const char *back_id;
    double back_delay;
    bool snap_back;
    const char *back_label;
    bool no_pip;
    const char *back_button;
} GenericOptionsArgs;
UIDef *create_UIBox_generic_options(GenericOptionsArgs args);
#define GENERIC_OPTIONS(...) create_UIBox_generic_options((GenericOptionsArgs){ __VA_ARGS__ })

UIDef *UIBox_dyn_container(UIDef *inner_list, bool horizontal, Col colour_override, Col background_override, bool flipped, double padding);
typedef struct { double scale; Col colour; bool shadow; bool col; double padding; bool has_padding; } SimpleTextArgs;
UIDef *simple_text_container(const char *loc, SimpleTextArgs args);
UIDef *create_UIBox_highlight(Node *rect);

/* widget state (Lua kept these as the `args` table referenced by the UI) */
typedef struct SliderState {
    const char *label;
    double w, h, label_scale, text_scale, min, max;
    int decimal_places;
    Col colour;
    Ref ref;                    /* numeric ref (ref_table[ref_value]) */
    void (*callback)(struct SliderState *s);
    const char *id;
    char *text;
} SliderState;
typedef SliderState SliderArgs;
UIDef *create_slider(SliderArgs args);
#define SLIDER(...) create_slider((SliderArgs){ __VA_ARGS__ })

typedef struct ToggleState {
    const char *label;
    const char *info[4]; int ninfo;
    double w, h, scale, label_scale;
    bool *ref;                  /* ref_table[ref_value] */
    Col active_colour, inactive_colour;
    bool col;
    void (*callback)(bool v);
} ToggleState;
typedef ToggleState ToggleArgs;
UIDef *create_toggle(ToggleArgs args);
#define TOGGLE(...) create_toggle((ToggleArgs){ __VA_ARGS__ })

struct CycleState;
typedef struct {
    const char *from_val, *to_val;
    int from_key, to_key;
    struct CycleState *cycle_config;
} CycleCallbackArgs;
typedef void (*CycleCallback)(CycleCallbackArgs *args);
typedef struct CycleState {
    const char **options; int noptions;
    int current_option;          /* 1-based */
    const char *current_option_val;
    double w, h, scale, text_scale;
    const char *label;
    Col colour;
    CycleCallback opt_callback;
    bool no_pips;
    FocusArgs *focus_args;
    bool cycle_shoulders;
    const char *id;
    const char *info[4]; int ninfo;
    UIDef *mid;
    struct Tooltip *on_demand_tooltip;
    void *ref_table;             /* passed through to callbacks */
    const char *ref_value;
    const char *l, *r;
} CycleState;
typedef CycleState OptionCycleArgs;
UIDef *create_option_cycle(OptionCycleArgs args);
#define OPTION_CYCLE(...) create_option_cycle((OptionCycleArgs){ __VA_ARGS__ })
/* builds a heap options array from a list of strings */
const char **str_list(int n, ...);
#define STRS(...) str_list((int)(sizeof((const char *[]){ __VA_ARGS__ }) / sizeof(const char *)), __VA_ARGS__)

typedef UIDef *(*TabDefFn)(void *arg);
typedef struct {
    const char *label;
    bool chosen;
    TabDefFn tab_definition_function;
    void *tab_definition_function_args;
    UIFunc func;
} TabSpec;
typedef struct {
    TabSpec tabs[8]; int ntabs;
    double tab_h, tab_w;
    double scale, text_scale;
    bool no_shoulders, no_loop;
    Col colour;
    bool snap_to_nav;
    double padding; bool has_padding;
    const char *tab_alignment;
} TabsArgs;
UIDef *create_tabs(TabsArgs args);
#define TABS(...) create_tabs((TabsArgs){ __VA_ARGS__ })

typedef struct TextInputState {
    char **ref;                 /* ref_table[ref_value] (heap string) */
    int max_length;
    double w, h;
    Col colour, hooked_colour, orig_colour;
    const char *prompt_text;
    char *current_prompt_text;
    double text_scale;
    bool all_caps;
    void (*callback)(void);
    const char *id;
    bool extended_corpus;
    double keyboard_offset; bool has_keyboard_offset;
    struct { char **letters; int current_position; } text;
} TextInputState;
typedef TextInputState TextInputArgs;
UIDef *create_text_input(TextInputArgs args);
#define TEXT_INPUT(...) create_text_input((TextInputArgs){ __VA_ARGS__ })
typedef struct { bool backspace_key, return_key, space_key; } KeyboardArgs;
UIDef *create_keyboard_input(KeyboardArgs args);
UIDef *create_dynatext_pips(DynaText *dynatext, Col colour, Col inactive_colour, double w, double h);

/* tooltip / description helpers */
UIDef *desc_from_rows(UIDef *desc_nodes, bool empty, double maxw);
UIDef *transparent_multiline_text(UIDef *desc_nodes);
UIDef *info_tip_from_rows(UIDef *desc_nodes, const char *name);
UIDef *overlay_infotip(const char **rows, int nrows);
UIDef *name_from_rows(UIDef *name_nodes, Col background_colour);
Col get_badge_colour(const char *key);
UIDef *create_badge(const char *s, Col badge_col, Col text_col, double scaling);
UIDef *create_UIBox_detailed_tooltip(void *center, int kind);
UIDef *create_popup_UIBox_tooltip(struct Tooltip *tooltip);
typedef struct {
    bool no_bg; Col bg_col; bool red_bad;
    const char *text;
    double bump_amount, maxw, scale;
    double text_rot; bool has_text_rot;
} CardAlertArgs;
UIDef *create_UIBox_card_alert(CardAlertArgs args);
#define CARD_ALERT(...) create_UIBox_card_alert((CardAlertArgs){ __VA_ARGS__ })
UIDef *create_UIBox_notify_alert(const char *key, const char *type);
UIDef *create_UIBox_character_button(const char *label, UIFunc button, Col colour, UIFunc update_func, double maxw);
UIDef *create_button_binding_pip(const char *button, double scale);

/* card UI */
UIDef *UIDEF_use_and_sell_buttons(Card *card);
UIBox *UIDEF_card_focus_ui(Card *card);
UIDef *UIDEF_card_focus_button(Card *card, const char *type, UIFunc func, UIFunc button, const char *pos);
void card_focus_ui_set_alignment(Card *card);
UIDef *UIDEF_speech_bubble(const char *text_key, bool quip, LocVars *vars);
UIDef *UIDEF_deck_preview(bool show_remaining);
UIDef *UIDEF_card_h_popup(Node *card, struct CardUITable *ui_table);
UIDef *UIDEF_shop(void);
Card *create_card_for_shop(CardArea *area);
void create_shop_card_ui(Card *card, const char *type, CardArea *area);
UIDef *create_UIBox_buttons(void);
void add_tag(Tag *tag);
Sprite *shared_sticker(const char *name);
PtrVec *instance_list(const char *name);

/* HUD / run screens */
UIDef *create_UIBox_HUD(void);
UIDef *create_UIBox_HUD_buttons(void);
UIDef *create_UIBox_HUD_blind(void);
UIDef *create_UIBox_blind_select(void);
UIDef *create_UIBox_blind_tag(int blind_choice, bool run_info);
UIDef *create_UIBox_blind_choice(const char *type, bool run_info);
UIDef *create_UIBox_round_evaluation(void);
UIDef *create_UIBox_arcana_pack(void);
UIDef *create_UIBox_spectral_pack(void);
UIDef *create_UIBox_standard_pack(void);
UIDef *create_UIBox_buffoon_pack(void);
UIDef *create_UIBox_celestial_pack(void);
UIDef *create_UIBox_win(void);
UIDef *create_UIBox_game_over(void);
UIDef *create_UIBox_round_scores_row(const char *score, Col text_colour);
UIDef *create_UIBox_hand_tip(const char *handname);
UIDef *create_UIBox_current_hand_row(const char *handname, bool simple);
UIDef *create_UIBox_current_hands(bool simple);
UIDef *create_UIBox_blind_popup(BlindProto *blind, bool discovered, LocVars *vars);
UIDef *UIDEF_deck_info(bool show_remaining);
UIDef *UIDEF_run_info(void);
UIDef *UIDEF_current_blinds(void);
UIDef *UIDEF_deck_stake_column(const char *deck_key);
UIDef *UIDEF_current_stake(void);
UIDef *UIDEF_view_deck(bool unplayed_only);
UIDef *UIDEF_used_vouchers(void);

/* options / menus */
UIDef *create_UIBox_options(void);
UIDef *create_UIBox_settings(void);
UIDef *UIDEF_settings_tab(const char *tab);
UIDef *UIDEF_usage_tabs(void);
UIDef *create_UIBox_usage(const char *type, const char *set);
UIDef *create_UIBox_customize_deck(void);
UIDef *UIDEF_custom_deck_tab(const char *suit);
UIDef *create_UIBox_high_scores(void);
UIDef *create_progress_box(DVal *profile_progress, bool smaller);
UIDef *create_UIBox_high_scores_row(const char *score);
UIDef *create_UIBox_exit_CTA(void);
UIDef *create_UIBox_main_menu_buttons(void);
UIDef *create_UIBox_language_button(void);
UIDef *create_UIBox_profile_button(void);
UIDef *UIDEF_language_selector(void);
UIDef *UIDEF_credits(void);
UIDef *UIDEF_challenges(bool from_game_over);
UIDef *UIDEF_run_setup(const char *from);   /* 'from_game_over' / 'from_game_won' / 'challenge_list' / NULL */
UIDef *UIDEF_profile_select(void);
UIDef *UIDEF_profile_option(int profile);
UIDef *UIDEF_stake_description(int stake);
UIDef *UIDEF_stake_option(const char *type);
UIDef *UIDEF_viewed_stake_option(void);
UIDef *UIDEF_challenge_list(bool from_game_over);
UIDef *UIDEF_challenge_list_page(int page);
UIDef *UIDEF_challenge_description(int id, bool daily, bool is_row);
UIDef *UIDEF_run_setup_option(const char *type);

/* credits (ui_credits.c + generated ui_credits_data.c) */
typedef struct { const char *name, *artist, *artist_link, *art; const char *ml_text[8]; int n_ml; } CollabCredit;
typedef struct { const char *label; TabDefFn fn; } CreditsTab;
extern const CollabCredit CREDITS_COLLABS[]; extern const int CREDITS_N_COLLABS;
extern const CreditsTab CREDITS_TABS[]; extern const int CREDITS_N_TABS;
UIDef *UIDEF_viewed_collab_option(void);

/* collection */
UIDef *create_UIBox_your_collection(void);
UIDef *create_UIBox_your_collection_jokers(void);
UIDef *create_UIBox_your_collection_tarots(void);
UIDef *create_UIBox_your_collection_boosters(void);
UIDef *create_UIBox_your_collection_planets(void);
UIDef *create_UIBox_your_collection_spectrals(void);
UIDef *create_UIBox_your_collection_vouchers(bool exit);
UIDef *create_UIBox_your_collection_seals(bool exit);
UIDef *create_UIBox_your_collection_enhancements(bool exit);
UIDef *create_UIBox_your_collection_editions(void);
UIDef *create_UIBox_your_collection_decks(void);
UIDef *create_UIBox_your_collection_tags(void);
UIDef *create_UIBox_your_collection_blinds(bool exit);
UIDef *create_UIBox_card_unlock(Center *card_center);
UIDef *create_UIBox_deck_unlock(Center *deck_center);

/* ---- button_callbacks.lua (G.FUNCS.*) ---- */
#define GF(name) void G_FUNCS_##name(UIElement *e)
#define GF_IMPL(name) void G_FUNCS_##name(UIElement *e)
GF(set_button_pip);
GF(visible_blind);
GF(discard_cards_from_highlighted_button);
GF(start_tutorial);
GF(wipe_on);
GF(wipe_off);
GF(can_start_run);
GF(toggle_seeded_run);
GF(RUN_SETUP_check_stake);
GF(RUN_SETUP_check_stake2);
GF(change_challenge_description);
GF(challenge_list);
GF(start_challenge_run);
GF(start_setup_run);
GF(can_load_profile);
GF(load_profile);
GF(can_delete_profile);
GF(delete_profile);
GF(can_unlock_all);
GF(unlock_all);
GF(beta_lang_alert);
GF(warn_lang);
GF(change_lang);
GF(profile_select);
GF(quit);
GF(language_selection);
GF(continue_unlock);
GF(RUN_SETUP_check_back_stake_column);
GF(RUN_SETUP_check_back_stake_highlight);
GF(can_continue);
GF(setup_run);
GF(go_to_menu);
GF(your_collection);
GF(show_credits);
GF(high_scores);
GF(customize_deck);
GF(usage);
GF(settings);
GF(run_info);
GF(options);
GF(notify_then_setup_run);
GF(paste_seed);
GF(start_run);

GF(CREDITS_check_collab);
GF(copy_seed);
GF(high_score_alert);
GF(can_apply_window_changes);
GF(toggle_button);
GF(select_text_input);
GF(slider);
GF(toggle);
GF(option_cycle);
GF(flash);
GF(pip_dynatext);
GF(key_button);
GF(text_input);
GF(change_tab);
GF(redeem_from_shop);
GF(open_booster);
GF(tut_next);
GF(options);
GF(exit_overlay_menu);
GF(deck_info);
GF(deck_view_challenge);
GF(blueprint_compat);
GF(shop_voucher_empty);
GF(use_card);
GF(reroll_boss);
GF(sell_card);
GF(can_sell_card);
GF(can_use_consumeable);
GF(can_select_card);
GF(can_buy);
GF(can_buy_and_use);
GF(can_redeem);
GF(can_open);
GF(buy_from_shop);
GF(toggle_shop);
GF(reroll_shop);
GF(can_reroll);
GF(skip_booster);
GF(can_skip_booster);
GF(select_blind);
GF(skip_blind);
GF(reroll_boss_button);
GF(play_cards_from_highlighted_button);
GF(discard_cards_from_highlighted_button);
GF(can_play);
GF(can_discard);
GF(sort_hand_suit);
GF(sort_hand_value);
GF(cash_out);
GF(run_info);
GF(start_run);
GF(go_to_menu);
GF(setup_run);
GF(quit);
GF(wipe_on);
GF(wipe_off);
GF(change_tab);
GF(overlay_menu);
GF(your_collection);
GF(settings);
GF(high_scores);
GF(notify_then_setup_run);
GF(text_input);
GF(paste_seed);
GF(hover_tag_proxy);
GF(DT_add_money);
GF(HUD_blind_debuff);
GF(HUD_blind_debuff_prefix);
GF(HUD_blind_reward);
GF(HUD_blind_visible);
GF(blind_chip_UI_scale);
GF(blind_choice_handler);
GF(hand_mult_UI_set);
GF(hand_chip_UI_set);
GF(hand_chip_total_UI_set);
GF(flame_handler);
GF(hand_text_UI_set);
GF(chip_UI_set);
GF(can_continue);
GF(can_load_profile);
GF(can_delete_profile);
GF(load_profile);
GF(delete_profile);
GF(change_viewed_back);
GF(change_stake);
GF(change_challenge_list_page);
GF(change_challenge_description);
GF(RUN_SETUP_check_back);
GF(RUN_SETUP_check_back_name);
GF(RUN_SETUP_check_stake);
GF(RUN_SETUP_check_stake2);
GF(RUN_SETUP_check_back_stake_column);
GF(RUN_SETUP_check_back_stake_highlight);
GF(change_viewed_collab);
GF(change_selected_back);
GF(start_setup_run);
GF(start_challenge_run);
GF(set_Trance);
GF(change_crt_bloom);
GF(change_shadows);
GF(change_window_cycle_update);
GF(apply_window_changes);
GF(change_gamespeed);
GF(change_play_discard_position);
GF(change_screenmode);
GF(change_screen_resolution);
GF(change_display);
GF(change_vsync);
GF(change_window_mode);
GF(change_collab);
GF(change_contrast);
GF(change_shoulder_tab);
GF(card_collection_page);
GF(your_collection_jokers);
GF(your_collection_tarots);
GF(your_collection_planets);
GF(your_collection_spectrals);
GF(your_collection_vouchers);
GF(your_collection_enhancements);
GF(your_collection_decks);
GF(your_collection_editions);
GF(your_collection_tags);
GF(your_collection_seals);
GF(your_collection_blinds);
GF(your_collection_boosters);
GF(exit_confirmation);
GF(show_credits);
GF(language_selection);
GF(change_lang);
GF(warn_lang);
GF(profile_select);
GF(continue_unlock);
GF(cycle_update);
GF(slider);
GF(toggle_button);
GF(option_cycle);
GF(set_button_anim);
GF(skip_tutorial_section);
GF(set_toggle);
GF(toggle);
GF(select_text_input);
GF(customize_deck);
GF(usage);
GF(stats);
GF(challenge_list);
GF(challenge_info);
GF(show_infotip);
#undef GF
#define GF_IMPL(name) void G_FUNCS_##name(UIElement *e)

void G_FUNCS_slider_descreet(UIElement *e, double per);
void G_FUNCS_text_input_key(UIElement *e, const char *key, bool caps);
typedef struct { bool no_esc; double offset_y; bool has_offset_y; } OverlayArgs;
void overlay_menu(UIDef *definition, OverlayArgs args);
void use_card(UIElement *e, bool mute, bool nosave);
void start_run(UIElement *e, StartRunArgs args);
void load_profile(bool delete_prof_data);
bool check_for_buy_space(Card *card);
void end_consumeable(UIElement *e, double delayfac);
void G_FUNCS_settings_instant(bool instant);
void reset_check_profile_data(void);
void G_FUNCS_buy_from_shop_id(UIElement *e, const char *id);
void G_FUNCS_wipe_on_text(const char **message, bool no_card, double timefac, Col alt_colour);   /* NULL-terminated */
void ui_funcs_register_all(void);
bool can_continue_check(UIElement *e);
PCard *random_p_card(void);
double scale_number(double number, double scale, double max);   /* max NAN = 10000 */
char **profile_name_ref(int p);
void profile_name_sync(int p);
void init_collection_card_alerts(void);
void refresh_contrast_mode(void);
void apply_window_changes(bool initial);
const char *collab_option(const char *suit, int idx);
char **collab_setting(const char *suit);

#endif
