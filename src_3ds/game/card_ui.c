/* card_ui.c - Card:generate_UIBox_ability_table from card.lua */
#include "game.h"

static const char *probs(void) { return num_str(G.GAME.probabilities.normal); }

CardUITable *Card_generate_UIBox_ability_table(Card *c) {
    const char *card_type = c->ability.set ? c->ability.set : "None";
    bool hide_desc = false;
    LocVars *lv = NULL;
    UIDef *main_start = NULL, *main_end = NULL;
    bool no_badge = false;
    Center *ce = c->config.center;
    const char *set = c->ability.set;
    bool center_unlocked_false = ce->unlocked_set ? !ce->unlocked : false;
    if (!c->bypass_lock && !center_unlocked_false &&
        (str_eq(set, "Joker") || str_eq(set, "Edition") || c->ability.consumeable || str_eq(set, "Voucher") || str_eq(set, "Booster")) &&
        !ce->discovered &&
        ((c->area != G.jokers && c->area != G.consumeables && c->area) || !c->area))
        card_type = "Undiscovered";
    if (center_unlocked_false && !c->bypass_lock) {
        card_type = "Locked";
    } else if (!strcmp(card_type, "Undiscovered") && !c->bypass_discovery_ui) {
        hide_desc = true;
    } else if (c->debuff) {
        lv = loc_vars_new();
        lv->present = true;
        lv->debuffed = true;
        lv->playing_card = c->base.colour != NULL;
        lv->value = c->base.value;
        lv->suit = c->base.suit;
        lv->colour = c->base.colour;
    } else if (!strcmp(card_type, "Default") || !strcmp(card_type, "Enhanced")) {
        lv = loc_vars_new();
        lv->present = true;
        lv->playing_card = c->base.colour != NULL;
        lv->value = c->base.value;
        lv->suit = c->base.suit;
        lv->colour = c->base.colour;
        if (c->base.nominal > 0) { lv->nominal_chips = c->base.nominal; lv->has_nominal_chips = true; }
        double bc = c->ability.bonus + c->ability.perma_bonus;
        if (bc > 0) { lv->bonus_chips = bc; lv->has_bonus_chips = true; }
    } else if (str_eq(set, "Joker")) {
        Ability *A = &c->ability;
        const char *n = A->name;
#define N(s) (!strcmp(n, (s)))
#define NS(v) num_str(v)
        if (N("Joker")) lv = LV(NS(A->mult));
        else if (N("Jolly Joker") || N("Zany Joker") || N("Mad Joker") || N("Crazy Joker") || N("Droll Joker"))
            lv = LV(NS(A->t_mult), localize_cat(A->type, "poker_hands"));
        else if (N("Sly Joker") || N("Wily Joker") || N("Clever Joker") || N("Devious Joker") || N("Crafty Joker"))
            lv = LV(NS(A->t_chips), localize_cat(A->type, "poker_hands"));
        else if (N("Half Joker")) lv = LV(NS(A->extra.mult), NS(A->extra.size));
        else if (N("Fortune Teller")) lv = LV(NS(A->extra.num), NS(G.GAME.consumeable_usage_total.present ? G.GAME.consumeable_usage_total.tarot : 0));
        else if (N("Steel Joker")) lv = LV(NS(A->extra.num), NS(1 + A->extra.num * A->steel_tally));
        else if (N("Chaos the Clown")) lv = LV(NS(A->extra.num));
        else if (N("Space Joker")) lv = LV(probs(), NS(A->extra.num));
        else if (N("Stone Joker")) lv = LV(NS(A->extra.num), NS(A->extra.num * A->stone_tally));
        else if (N("Drunkard")) lv = LV(NS(A->d_size));
        else if (N("Green Joker")) lv = LV(NS(A->extra.hand_add), NS(A->extra.discard_sub), NS(A->mult));
        else if (N("Credit Card")) lv = LV(NS(A->extra.num));
        else if (N("Greedy Joker") || N("Lusty Joker") || N("Wrathful Joker") || N("Gluttonous Joker"))
            lv = LV(NS(A->extra.s_mult), localize_cat(A->extra.suit, "suits_singular"));
        else if (N("Blue Joker")) lv = LV(NS(A->extra.num), NS(A->extra.num * ((G.deck) ? G.deck->cards.count : 52)));
        else if (N("Sixth Sense")) lv = loc_vars_new();
        else if (N("Mime")) {}
        else if (N("Hack")) lv = LV(NS(A->extra.num + 1));
        else if (N("Pareidolia")) {}
        else if (N("Faceless Joker")) lv = LV(NS(A->extra.dollars), NS(A->extra.faces));
        else if (N("Oops! All 6s")) {}
        else if (N("Juggler")) lv = LV(NS(A->h_size));
        else if (N("Golden Joker")) lv = LV(NS(A->extra.num));
        else if (N("Joker Stencil")) lv = LV(NS(A->x_mult));
        else if (N("Four Fingers")) {}
        else if (N("Ceremonial Dagger")) lv = LV(NS(A->mult));
        else if (N("Banner")) lv = LV(NS(A->extra.num));
        else if (N("Misprint")) {
            const char *rm[32]; int nrm = 0;
            for (int i = (int)A->extra.min; i <= (int)A->extra.max && nrm < 32; i++) rm[nrm++] = xstrdup(tfmt("%d", i));
            const char *loc_mult = tfmt(" %s ", localize_key("k_mult"));
            main_start = uinodes();
            uidef_add(main_start, UIT(CFG(.text = "  +", .colour = G.C.MULT, .scale = 0.32)));
            DynaText *d1 = DYNATEXT(.parts = dt_parts_strs(rm, nrm), .nparts = nrm, .colours = dt_cols(1, G.C.RED), .ncolours = 1,
                                    .pop_in_rate = 9999999, .silent = true, .random_element = true, .pop_delay = 0.5, .has_pop_delay = true,
                                    .scale = 0.32, .min_cycle_time = 0, .has_min_cycle_time = true);
            uidef_add(main_start, UIO(CFG(.object = &d1->n)));
            Card *last = (G.deck && G.deck->cards.count > 0) ? G.deck->cards.items[G.deck->cards.count - 1] : NULL;
            char sfx[2] = { last ? last->base.suit[0] : 'D', 0 };
            const char *tag = tfmt("#@%s%s", last ? tfmt("%d", last->base.id) : "11", sfx);
            int np = 15;
            DynaTextPart *parts = xcalloc((size_t)np, sizeof(DynaTextPart));
            parts[0].string = xstrdup("rand()"); parts[0].colour = G.C.JOKER_GREY;
            parts[1].string = xstrdup(tag); parts[1].colour = G.C.RED;
            for (int i = 2; i < np; i++) parts[i].string = xstrdup(loc_mult);
            DynaText *d2 = DYNATEXT(.parts = parts, .nparts = np, .colours = dt_cols(1, G.C.UI.TEXT_DARK), .ncolours = 1,
                                    .pop_in_rate = 9999999, .silent = true, .random_element = true, .pop_delay = 0.2011, .has_pop_delay = true,
                                    .scale = 0.32, .min_cycle_time = 0, .has_min_cycle_time = true);
            uidef_add(main_start, UIO(CFG(.object = &d2->n)));
        }
        else if (N("Mystic Summit")) lv = LV(NS(A->extra.mult), NS(A->extra.d_remaining));
        else if (N("Marble Joker")) {}
        else if (N("Loyalty Card")) {
            char *s = localize_variable(A->loyalty_remaining == 0 ? "loyalty_active" : "loyalty_inactive", LV(NS(A->loyalty_remaining)));
            lv = LV(NS(A->extra.Xmult), NS(A->extra.every + 1), s);
        }
        else if (N("8 Ball")) lv = LV(probs(), NS(A->extra.num));
        else if (N("Dusk")) lv = LV(NS(A->extra.num + 1));
        else if (N("Raised Fist")) {}
        else if (N("Fibonacci")) lv = LV(NS(A->extra.num));
        else if (N("Scary Face")) lv = LV(NS(A->extra.num));
        else if (N("Abstract Joker")) lv = LV(NS(A->extra.num), NS((G.jokers ? G.jokers->cards.count : 0) * A->extra.num));
        else if (N("Delayed Gratification")) lv = LV(NS(A->extra.num));
        else if (N("Gros Michel")) lv = LV(NS(A->extra.mult), probs(), NS(A->extra.odds));
        else if (N("Even Steven")) lv = LV(NS(A->extra.num));
        else if (N("Odd Todd")) lv = LV(NS(A->extra.num));
        else if (N("Scholar")) lv = LV(NS(A->extra.mult), NS(A->extra.chips));
        else if (N("Business Card")) lv = LV(probs(), NS(A->extra.num));
        else if (N("Supernova")) {}
        else if (N("Spare Trousers")) lv = LV(NS(A->extra.num), localize_cat("Two Pair", "poker_hands"), NS(A->mult));
        else if (N("Superposition")) lv = LV(NS(A->extra.num));
        else if (N("Ride the Bus")) lv = LV(NS(A->extra.num), NS(A->mult));
        else if (N("Egg")) lv = LV(NS(A->extra.num));
        else if (N("Burglar")) lv = LV(NS(A->extra.num));
        else if (N("Blackboard")) lv = LV(NS(A->extra.num), localize_cat("Spades", "suits_plural"), localize_cat("Clubs", "suits_plural"));
        else if (N("Runner")) lv = LV(NS(A->extra.chips), NS(A->extra.chip_mod));
        else if (N("Ice Cream")) lv = LV(NS(A->extra.chips), NS(A->extra.chip_mod));
        else if (N("DNA")) lv = LV(NS(A->extra.num));
        else if (N("Splash")) {}
        else if (N("Constellation")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Hiker")) lv = LV(NS(A->extra.num));
        else if (N("To Do List")) lv = LV(NS(A->extra.dollars), localize_cat(A->to_do_poker_hand, "poker_hands"));
        else if (N("Smeared Joker")) {}
        else if (N("Blueprint") || N("Brainstorm")) {
            if (!A->blueprint_compat_ui) A->blueprint_compat_ui = xstrdup("");
            A->blueprint_compat_check = NULL;
            if (c->area && c->area == G.jokers) {
                main_end = uinodes();
                uidef_add(main_end, UIC(CFG(.align = "bm", .minh = 0.4),
                    UIC(CFG(.ref_table = c, .align = "m", .colour = G.C.JOKER_GREY, .r = 0.05, .padding = 0.06, .func = G_FUNCS_blueprint_compat),
                        UIT(CFG(.ref = REFS(&A->blueprint_compat_ui), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.32 * 0.8)))));
            }
        }
        else if (N("Cartomancer")) {}
        else if (N("Astronomer")) lv = LV(NS(A->extra.num));
        else if (N("Golden Ticket")) lv = LV(NS(A->extra.num));
        else if (N("Mr. Bones")) {}
        else if (N("Acrobat")) lv = LV(NS(A->extra.num));
        else if (N("Sock and Buskin")) lv = LV(NS(A->extra.num + 1));
        else if (N("Swashbuckler")) lv = LV(NS(A->mult));
        else if (N("Troubadour")) lv = LV(NS(A->extra.h_size), NS(-A->extra.h_plays));
        else if (N("Certificate")) lv = LV(NS(A->extra.num));
        else if (N("Throwback")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Hanging Chad")) lv = LV(NS(A->extra.num));
        else if (N("Rough Gem")) lv = LV(NS(A->extra.num));
        else if (N("Bloodstone")) lv = LV(probs(), NS(A->extra.odds), NS(A->extra.Xmult));
        else if (N("Arrowhead")) lv = LV(NS(A->extra.num));
        else if (N("Onyx Agate")) lv = LV(NS(A->extra.num));
        else if (N("Glass Joker")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Showman")) {}
        else if (N("Flower Pot")) lv = LV(NS(A->extra.num));
        else if (N("Wee Joker")) lv = LV(NS(A->extra.chips), NS(A->extra.chip_mod));
        else if (N("Merry Andy")) lv = LV(NS(A->d_size), NS(A->h_size));
        else if (N("The Idol")) {
            lv = LV(NS(A->extra.num), localize_cat(G.GAME.current_round.idol_card.rank, "ranks"), localize_cat(G.GAME.current_round.idol_card.suit, "suits_plural"));
            lv->colours[lv->ncolours++] = suit_colour(G.GAME.current_round.idol_card.suit);
        }
        else if (N("Seeing Double")) lv = LV(NS(A->extra.num));
        else if (N("Matador")) lv = LV(NS(A->extra.num));
        else if (N("Hit the Road")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("The Duo") || N("The Trio") || N("The Family") || N("The Order") || N("The Tribe"))
            lv = LV(NS(A->x_mult), localize_cat(A->type, "poker_hands"));
        else if (N("Cavendish")) lv = LV(NS(A->extra.Xmult), probs(), NS(A->extra.odds));
        else if (N("Card Sharp")) lv = LV(NS(A->extra.Xmult));
        else if (N("Red Card")) lv = LV(NS(A->extra.num), NS(A->mult));
        else if (N("Madness")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Square Joker")) lv = LV(NS(A->extra.chips), NS(A->extra.chip_mod));
        else if (N("Seance")) lv = LV(localize_cat(A->extra.poker_hand, "poker_hands"));
        else if (N("Riff-raff")) lv = LV(NS(A->extra.num));
        else if (N("Vampire")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Shortcut")) {}
        else if (N("Hologram")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Vagabond")) lv = LV(NS(A->extra.num));
        else if (N("Baron")) lv = LV(NS(A->extra.num));
        else if (N("Cloud 9")) lv = LV(NS(A->extra.num), NS(A->extra.num * A->nine_tally));
        else if (N("Rocket")) lv = LV(NS(A->extra.dollars), NS(A->extra.increase));
        else if (N("Obelisk")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Midas Mask")) {}
        else if (N("Luchador")) {
            bool has_message = c->area && c->area == G.jokers;
            if (has_message) {
                bool disableable = G.GAME.blind && !G.GAME.blind->disabled && str_eq(Blind_get_type(G.GAME.blind), "Boss");
                main_end = uinodes();
                uidef_add(main_end, UIC(CFG(.align = "bm", .minh = 0.4),
                    UIC(CFG(.ref_table = c, .align = "m", .colour = disableable ? G.C.GREEN : G.C.RED, .r = 0.05, .padding = 0.06),
                        UIT(CFG(.text = (char *)tfmt(" %s ", localize_key(disableable ? "k_active" : "ph_no_boss_active")), .colour = G.C.UI.TEXT_LIGHT, .scale = 0.32 * 0.9)))));
            }
        }
        else if (N("Photograph")) lv = LV(NS(A->extra.num));
        else if (N("Gift Card")) lv = LV(NS(A->extra.num));
        else if (N("Turtle Bean")) lv = LV(NS(A->extra.h_size), NS(A->extra.h_mod));
        else if (N("Erosion")) lv = LV(NS(A->extra.num), NS(MAX(0, A->extra.num * (G.GAME.starting_deck_size - G.playing_cards.count))), NS(G.GAME.starting_deck_size));
        else if (N("Reserved Parking")) lv = LV(NS(A->extra.dollars), probs(), NS(A->extra.odds));
        else if (N("Mail-In Rebate")) lv = LV(NS(A->extra.num), localize_cat(G.GAME.current_round.mail_card.rank, "ranks"));
        else if (N("To the Moon")) lv = LV(NS(A->extra.num));
        else if (N("Hallucination")) lv = LV(NS(G.GAME.probabilities.normal), NS(A->extra.num));
        else if (N("Lucky Cat")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Baseball Card")) lv = LV(NS(A->extra.num));
        else if (N("Bull")) lv = LV(NS(A->extra.num), NS(A->extra.num * MAX(0, G.GAME.dollars)));
        else if (N("Diet Cola")) lv = LV(localize_name_text("Tag", "tag_double"));
        else if (N("Trading Card")) lv = LV(NS(A->extra.num));
        else if (N("Flash Card")) lv = LV(NS(A->extra.num), NS(A->mult));
        else if (N("Popcorn")) lv = LV(NS(A->mult), NS(A->extra.num));
        else if (N("Ramen")) lv = LV(NS(A->x_mult), NS(A->extra.num));
        else if (N("Ancient Joker")) {
            lv = LV(NS(A->extra.num), localize_cat(G.GAME.current_round.ancient_card.suit, "suits_singular"));
            lv->colours[lv->ncolours++] = suit_colour(G.GAME.current_round.ancient_card.suit);
        }
        else if (N("Walkie Talkie")) lv = LV(NS(A->extra.chips), NS(A->extra.mult));
        else if (N("Seltzer")) lv = LV(NS(A->extra.num));
        else if (N("Castle")) {
            lv = LV(NS(A->extra.chip_mod), localize_cat(G.GAME.current_round.castle_card.suit, "suits_singular"), NS(A->extra.chips));
            lv->colours[lv->ncolours++] = suit_colour(G.GAME.current_round.castle_card.suit);
        }
        else if (N("Smiley Face")) lv = LV(NS(A->extra.num));
        else if (N("Campfire")) lv = LV(NS(A->extra.num), NS(A->x_mult));
        else if (N("Stuntman")) lv = LV(NS(A->extra.chip_mod), NS(A->extra.h_size));
        else if (N("Invisible Joker")) lv = LV(NS(A->extra.num), NS(A->invis_rounds));
        else if (N("Satellite")) {
            int planets = 0;
            for (int i = 0; i < G.GAME.consumeable_usage.cap; i++) {
                if (!STRMAP_LIVE(&G.GAME.consumeable_usage, i)) continue;
                UsageEntry *u = G.GAME.consumeable_usage.vals[i];
                if (str_eq(u->set, "Planet")) planets++;
            }
            lv = LV(NS(A->extra.num), NS(planets * A->extra.num));
        }
        else if (N("Shoot the Moon")) lv = LV(NS(A->extra.num));
        else if (N("Driver's License")) lv = LV(NS(A->extra.num), A->has_driver_tally ? NS(A->driver_tally) : "0");
        else if (N("Burnt Joker")) {}
        else if (N("Bootstraps")) lv = LV(NS(A->extra.mult), NS(A->extra.dollars), NS(A->extra.mult * floor((G.GAME.dollars + G.GAME.dollar_buffer) / A->extra.dollars)));
        else if (N("Caino")) lv = LV(NS(A->extra.num), NS(A->caino_xmult));
        else if (N("Triboulet")) lv = LV(NS(A->extra.num));
        else if (N("Yorick")) lv = LV(NS(A->extra.xmult), NS(A->extra.discards), NS(A->yorick_discards), NS(A->x_mult));
        else if (N("Chicot")) {}
        else if (N("Perkeo")) lv = LV(NS(A->extra.num));
#undef N
#undef NS
    }
    Badges *badges = NEW(Badges);
    badges->present = true;
    if ((strcmp(card_type, "Locked") && strcmp(card_type, "Undiscovered") && strcmp(card_type, "Default")) || c->debuff) badges->card_type = card_type;
    if (str_eq(set, "Joker") && c->bypass_discovery_ui && !no_badge) badges->force_rarity = true;
    if (c->edition) {
        if (str_eq(c->edition->type, "negative") && c->ability.consumeable) badges->items[badges->n++] = "negative_consumable";
        else badges->items[badges->n++] = str_eq(c->edition->type, "holo") ? "holographic" : c->edition->type;
    }
    if (c->seal) badges->items[badges->n++] = intern(tfmt("%s_seal", tlower(c->seal)));
    if (c->ability.eternal) badges->items[badges->n++] = "eternal";
    if (c->ability.perishable) {
        if (!lv) lv = loc_vars_new();
        lv->has_perish_tally = true;
        lv->perish_tally = c->ability.perish_tally;
        badges->items[badges->n++] = "perishable";
    }
    if (c->ability.rental) badges->items[badges->n++] = "rental";
    if (c->pinned) badges->items[badges->n++] = "pinned_left";
    if (c->sticker || ((c->sticker_run && strcmp(c->sticker_run, "NONE") != 0) && G.SETTINGS.run_stake_stickers)) {
        if (!lv) lv = loc_vars_new();
        lv->sticker = c->sticker ? c->sticker : c->sticker_run;
    }
    if (lv) lv->present = true;
    return generate_card_ui(c->config.center, 1, NULL, lv, card_type, badges, hide_desc, main_start, main_end);
}
