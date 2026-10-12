/* ui_credits.c - G.UIDEF.credits / viewed_collab_option (the static tabs
 * are generated into ui_credits_data.c by tools/gen_credits.lua). */
#include "game.h"

static const char *viewed_collab;

static const CollabCredit *find_collab(const char *name) {
    for (int i = 0; i < CREDITS_N_COLLABS; i++) if (str_eq(CREDITS_COLLABS[i].name, name)) return &CREDITS_COLLABS[i];
    return NULL;
}

static UIDef *multiline_credit_text(const char *const *lines, int n) {
    UIDef *t = UIR(CFG(.align = "cl", .padding = 0));
    for (int i = 0; i < n; i++)
        uidef_add(t, UIR(CFG(.align = "cl", .padding = 0), UIT(CFG(.text = (char *)lines[i], .scale = 0.28, .colour = G.C.WHITE))));
    return t;
}

UIDef *UIDEF_viewed_collab_option(void) {
    if (!viewed_collab) viewed_collab = "The Binding of Isaac";
    const CollabCredit *c = find_collab(viewed_collab);
    if (!c) c = find_collab("The Binding of Isaac");
    Sprite *sp = sprite_new(0, 0, 0.8 * G.CARD_W, 0.8 * G.CARD_H, atlas_get(tfmt("%s_1", c->art)), (SpritePos){ 2, 0 });
    static const char *rights[6] = {
        "All rights reserved. No part of this work may be",
        "reproduced in any form or by any means\xe2\x80\x94 graphic,",
        "electronic, or mechanical, including recording,",
        "online distribution, or information storage and retrieval",
        "systems\xe2\x80\x94without the written permission of the publisher",
        "or the designated rightsholder, as applicable.",
    };
    return UIROOT(CFG(.align = "cm", .colour = G.C.BLACK, .r = 0.1),
        UIR(CFG(.align = "cm", .padding = 0.1), UIT(CFG(.text = (char *)viewed_collab, .scale = 0.5, .colour = G.C.WHITE))),
        UIR(CFG(.align = "cm", .padding = 0),
            UIC(CFG(.align = "cm", .padding = 0.1, .minw = 9, .minh = 4.2),
                multiline_credit_text(rights, 6),
                c->n_ml ? multiline_credit_text(c->ml_text, c->n_ml) : UISKIP),
            UIC(CFG(.align = "cm", .padding = 0.1, .minw = 3, .minh = 4.2),
                UIR(CFG(.align = "cm", .padding = 0.0),
                    UIR(CFG(.align = "cm", .colour = G.C.WHITE, .r = 0.3), UIO(CFG(.colour = G.C.BLUE, .object = &sp->n, .hover = true, .can_collide = -1)))),
                c->artist ? UIR(CFG(.align = "cm", .padding = 0.07, .r = 0.1, .outline = 0.7, .outline_colour = G.C.WHITE),
                                UIR(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = "Art created by:", .scale = 0.3, .colour = G.C.WHITE))),
                                UIR(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = (char *)c->artist, .scale = 0.3, .colour = G.C.GOLD))),
                                UIIF(c->artist_link, UIR(CFG(.align = "cm", .padding = 0), UIT(CFG(.text = (char *)c->artist_link, .scale = 0.3, .colour = G.C.BLUE)))))
                          : UISKIP)));
}

static void change_viewed_collab(CycleCallbackArgs *a) { viewed_collab = a->to_val; }

GF_IMPL(CREDITS_check_collab) {
    if (!str_eq(viewed_collab, e->config->id)) {
        if (e->config->object) node_remove(e->config->object);
        UIBox *b = uibox_new(UIDEF_viewed_collab_option(), UIBCFG(.has_offset = true, .offset = { 0, 0 }, .align = "cm", .parent = &e->n));
        e->config->object = &b->n;
        e->config->id = viewed_collab;
    }
}

static UIDef *collabs_tab(void *arg) {
    (void)arg;
    const char **opts = xcalloc((size_t)CREDITS_N_COLLABS, sizeof(char *));
    for (int i = 0; i < CREDITS_N_COLLABS; i++) opts[i] = CREDITS_COLLABS[i].name;   /* generated sorted */
    viewed_collab = opts[0];
    UIDef *middle = UIR(CFG(.align = "cm", .minh = 4.8, .minw = 12.3), UIO(CFG(.func = G_FUNCS_CREDITS_check_collab, .object = moveable_new(0, 0, 0, 0))));
    return UIROOT(CFG(.align = "cm", .padding = 0.2, .colour = G.C.L_BLACK, .r = 0.1, .emboss = 0.05, .minh = 6, .minw = 10),
        UIC(CFG(.align = "cm", .padding = 0.2, .outline_colour = G.C.JOKER_GREY, .r = 0.1, .outline = 1),
            OPTION_CYCLE(.options = opts, .noptions = CREDITS_N_COLLABS, .opt_callback = change_viewed_collab, .current_option = 1, .colour = G.C.RED, .w = 4,
                         .mid = middle)));
}

UIDef *UIDEF_credits(void) {
    TabsArgs ta = { .snap_to_nav = true };
    for (int i = 0; i < CREDITS_N_TABS && ta.ntabs < 8; i++)
        ta.tabs[ta.ntabs++] = (TabSpec){ .label = CREDITS_TABS[i].label, .chosen = i == 0,
                                         .tab_definition_function = CREDITS_TABS[i].fn ? CREDITS_TABS[i].fn : collabs_tab };
    return GENERIC_OPTIONS(.contents = uidef_add(uinodes(), UIR(CFG(.align = "cm", .padding = 0), create_tabs(ta))));
}
