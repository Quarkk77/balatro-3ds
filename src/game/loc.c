/* loc.c - localisation: loc_parse_string, localize(), loc_colour (misc_functions.lua) */
#include "game.h"

/* ------------------------------------------------------------------ */
/* parsed line structures                                              */
/* ------------------------------------------------------------------ */
typedef struct { bool is_var; char *str; int var; } LocSub;
typedef struct { char key[8]; char *val; } LocCtrl;
typedef struct { LocSub *sub; int nsub; LocCtrl ctrl[8]; int nctrl; } LocPart;
typedef struct { LocPart *parts; int nparts; } LocLine;
typedef struct { LocLine *lines; int nlines; bool multi_line; } LocLines;

static const char *ctrl_get(LocPart *p, const char *k) {
    for (int i = 0; i < p->nctrl; i++) if (!strcmp(p->ctrl[i].key, k)) return p->ctrl[i].val;
    return NULL;
}
static bool ctrl_has(LocPart *p, const char *k) {
    for (int i = 0; i < p->nctrl; i++) if (!strcmp(p->ctrl[i].key, k)) return p->ctrl[i].val != NULL;
    return false;
}

/* port of loc_parse_string */
static LocLine loc_parse_string(const char *line) {
    LocLine out = { NULL, 0 };
    int n = (int)strlen(line);
    LocCtrl ctrl[8]; int nctrl = 0;
    bool c = false, c_gather = false, s_gather = false;
    StrBuf c_name, c_val, s_ref;
    bool has_c_name = false, has_c_val = false, has_s_ref = false;
    sb_init(&c_name); sb_init(&c_val); sb_init(&s_ref);
    /* str_parts: array with possible holes; entries: string or var */
    VEC(LocSub) parts = { 0 };
    int str_it = 1;
    #define PART_AT(i) ((i) - 1 < parts.count ? &parts.items[(i) - 1] : NULL)
    #define FLUSH_PART() do { \
        if (parts.count > 0 && (parts.items[0].str || parts.items[0].is_var)) { \
            out.parts = xrealloc(out.parts, (size_t)(out.nparts + 1) * sizeof(LocPart)); \
            LocPart *lp = &out.parts[out.nparts++]; \
            memset(lp, 0, sizeof *lp); \
            int cnt = 0; \
            for (int z = 0; z < parts.count; z++) { if (!parts.items[z].str && !parts.items[z].is_var) break; cnt++; } \
            lp->sub = xmalloc((size_t)(cnt > 0 ? cnt : 1) * sizeof(LocSub)); \
            memcpy(lp->sub, parts.items, (size_t)cnt * sizeof(LocSub)); \
            lp->nsub = cnt; \
            memcpy(lp->ctrl, ctrl, sizeof ctrl); \
            lp->nctrl = nctrl; \
        } } while (0)
    bool ctrl_X = false;
    for (int i = 1; i <= n; i++) {
        char ch = line[i - 1];
        if (ch == '{') {
            FLUSH_PART();
            parts.count = 0; parts.cap = 0; parts.items = NULL;
            str_it = 1;
            nctrl = 0; memset(ctrl, 0, sizeof ctrl);
            ctrl_X = false;
            c = true; has_c_name = false; has_c_val = false; c_gather = false;
            sb_free(&c_name); sb_free(&c_val);
            s_gather = false; has_s_ref = false; sb_free(&s_ref);
        } else if (c && !(ch == ':' || ch == '}') && !c_gather) { sb_addc(&c_name, ch); has_c_name = true; }
        else if (c && ch == ':') c_gather = true;
        else if (c && !(ch == ',' || ch == '}') && c_gather) { sb_addc(&c_val, ch); has_c_val = true; }
        else if (c && (ch == ',' || ch == '}')) {
            c_gather = false;
            if (has_c_name && nctrl < 8) {
                snprintf(ctrl[nctrl].key, sizeof ctrl[nctrl].key, "%s", c_name.buf ? c_name.buf : "");
                ctrl[nctrl].val = has_c_val ? xstrdup(c_val.buf) : NULL;
                if (!strcmp(ctrl[nctrl].key, "X") && ctrl[nctrl].val) ctrl_X = true;
                nctrl++;
            }
            has_c_name = false; has_c_val = false;
            sb_free(&c_name); sb_free(&c_val);
            if (ch == '}') c = false;
        } else if (!c && ch != '#' && !s_gather) {
            while (parts.count < str_it) { LocSub z = { 0 }; vec_push(&parts, z); }
            LocSub *p = &parts.items[str_it - 1];
            if (ctrl_X && (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')) {
                if (!p->str) p->str = xstrdup("");
            } else {
                char tmp[2] = { ch, 0 };
                char *ns = str_cat(p->str ? p->str : "", tmp);
                free(p->str);
                p->str = ns;
            }
        } else if (!c && ch == '#' && !s_gather) {
            s_gather = true;
            LocSub *p = PART_AT(str_it);
            if (p && (p->str || p->is_var)) str_it++;
        } else if (!c && ch == '#' && s_gather) {
            s_gather = false;
            if (has_s_ref) {
                while (parts.count < str_it) { LocSub z = { 0 }; vec_push(&parts, z); }
                parts.items[str_it - 1].is_var = true;
                parts.items[str_it - 1].var = atoi(s_ref.buf);
                free(parts.items[str_it - 1].str);
                parts.items[str_it - 1].str = NULL;
                str_it++;
                has_s_ref = false;
                sb_free(&s_ref);
            }
        } else if (!c && s_gather) { sb_addc(&s_ref, ch); has_s_ref = true; }
        if (i == n) {
            FLUSH_PART();
            break;
        }
    }
    sb_free(&c_name); sb_free(&c_val); sb_free(&s_ref);
    #undef FLUSH_PART
    #undef PART_AT
    return out;
}

static LocLines parse_lines(DVal *v) {
    LocLines r = { NULL, 0, false };
    if (!v) return r;
    if (v->t == DV_STR) {
        r.lines = xmalloc(sizeof(LocLine));
        r.lines[0] = loc_parse_string(v->s);
        r.nlines = 1;
        return r;
    }
    int n = dv_len(v);
    r.lines = xcalloc((size_t)(n > 0 ? n : 1), sizeof(LocLine));
    for (int i = 1; i <= n; i++) {
        DVal *s = dv_geti(v, i);
        r.lines[r.nlines++] = loc_parse_string(s && s->t == DV_STR ? s->s : "");
    }
    return r;
}

static StrMap parsed_cache;
static bool cache_init;
static LocLines *cached_lines(const char *cache_key, DVal *src, bool multi_line) {
    if (!cache_init) { strmap_init(&parsed_cache); cache_init = true; }
    LocLines *l = strmap_get(&parsed_cache, cache_key);
    if (l) return l;
    if (!src) return NULL;
    l = NEW(LocLines);
    *l = parse_lines(src);
    l->multi_line = multi_line;
    strmap_set(&parsed_cache, cache_key, l);
    return l;
}

void init_localization(void) {
    if (cache_init) {
        /* drop cached parses of the previous language */
        strmap_free(&parsed_cache);
        strmap_init(&parsed_cache);
    }
}

DVal *loc_misc(const char *cat) {
    DVal *misc = dv_get(&G.localization, "misc");
    return dv_get(misc, cat);
}

/* ------------------------------------------------------------------ */
/* loc vars                                                            */
/* ------------------------------------------------------------------ */
LocVars *loc_vars_new(void) { return NEW(LocVars); }
LocVars *loc_vars_make(const char **v, int n) {
    LocVars *lv = loc_vars_new();
    for (int i = 0; i < n && i < 12; i++) lv->v[lv->n++] = v[i] ? xstrdup(v[i]) : NULL;
    lv->present = true;
    return lv;
}
void loc_vars_add(LocVars *lv, const char *s) { if (lv->n < 12) lv->v[lv->n++] = s ? xstrdup(s) : NULL; lv->present = true; }
void loc_vars_addn(LocVars *lv, double n) { loc_vars_add(lv, num_str(n)); }

static const char *var_str(LocVars *vars, int idx, const char *fallback) {
    if (!vars || idx < 1 || idx > vars->n || !vars->v[idx - 1]) return fallback;
    return vars->v[idx - 1];
}

static char *assemble(LocPart *part, LocVars *vars, const char *missing) {
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < part->nsub; i++) {
        LocSub *s = &part->sub[i];
        if (!s->is_var) sb_add(&sb, s->str);
        else {
            const char *v = var_str(vars, s->var, missing);
            if (v) sb_add(&sb, v);
        }
    }
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* simple localize forms                                               */
/* ------------------------------------------------------------------ */
const char *localize_key(const char *key) {
    DVal *d = loc_misc("dictionary");
    DVal *v = dv_get(d, key);
    if (v && v->t == DV_STR) return v->s;
    return "ERROR";
}
const char *localize_cat(const char *key, const char *cat) {
    if (!key) return "ERROR";
    DVal *c = loc_misc(cat);
    if (c) {
        DVal *v = dv_get(c, key);
        return (v && v->t == DV_STR) ? v->s : "ERROR";
    }
    return localize_key(key);
}
/* localize(key) / localize(key, cat) when the entry is a list of strings.
 * Returns the number of strings written to out (pointers into the loc table). */
int localize_list(const char *key, const char *cat, const char **out, int max) {
    DVal *c = loc_misc(cat ? cat : "dictionary");
    DVal *v = dv_get(c, key);
    if (!v || v->t != DV_TABLE) return 0;
    int n = dv_len(v), k = 0;
    for (int i = 1; i <= n && k < max; i++) out[k++] = dv_geti_str(v, i, "");
    return k;
}
bool localize_has_cat(const char *key, const char *cat) {
    DVal *c = loc_misc(cat);
    DVal *v = dv_get(c, key);
    return v && v->t == DV_STR;
}

static DVal *desc_entry(const char *set, const char *key) {
    DVal *desc = dv_get(&G.localization, "descriptions");
    DVal *s = dv_get(desc, set);
    return dv_get(s, key);
}

const char *localize_name_text(const char *set, const char *key) {
    DVal *e = desc_entry(set, key);
    DVal *n = dv_get(e, "name");
    if (n && n->t == DV_STR) return n->s;
    if (n && n->t == DV_TABLE) {
        DVal *f = dv_geti(n, 1);
        if (f && f->t == DV_STR) return f->s;
    }
    return "ERROR";
}

static LocLines *v_dictionary_lines(const char *key) {
    DVal *vd = loc_misc("v_dictionary");
    DVal *v = dv_get(vd, key);
    if (!v) return NULL;
    return cached_lines(tfmt("vdict/%s", key), v, v->t == DV_TABLE);
}

char *localize_variable(const char *key, LocVars *vars) {
    LocLines *l = v_dictionary_lines(key);
    if (!l) return xstrdup("ERROR");
    if (l->multi_line) {
        /* returns the first line when a single string is wanted */
        if (l->nlines > 0 && l->lines[0].nparts > 0) return assemble(&l->lines[0].parts[0], vars, "nil");
        return xstrdup("");
    }
    if (l->nlines > 0 && l->lines[0].nparts > 0) return assemble(&l->lines[0].parts[0], vars, "nil");
    return xstrdup("");
}

int localize_variable_lines(const char *key, LocVars *vars, char **out, int max) {
    LocLines *l = v_dictionary_lines(key);
    if (!l) { if (max > 0) out[0] = xstrdup("ERROR"); return 1; }
    int n = 0;
    for (int i = 0; i < l->nlines && n < max; i++) {
        if (l->lines[i].nparts > 0) out[n++] = assemble(&l->lines[i].parts[0], vars, "nil");
        else out[n++] = xstrdup("");
    }
    return n;
}

int localize_raw_descriptions(const char *set, const char *key, LocVars *vars, char **out, int max) {
    DVal *e = desc_entry(set, key);
    if (!e) return 0;
    LocLines *l = cached_lines(tfmt("desc/%s/%s/text", set, key), dv_get(e, "text"), false);
    if (!l) return 0;
    int n = 0;
    for (int i = 0; i < l->nlines && n < max; i++) {
        StrBuf sb; sb_init(&sb);
        for (int p = 0; p < l->lines[i].nparts; p++) {
            char *a = assemble(&l->lines[i].parts[p], vars, "ERROR");
            sb_add(&sb, a);
            free(a);
        }
        out[n++] = sb_take(&sb);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* loc_colour                                                          */
/* ------------------------------------------------------------------ */
Col loc_colour(const char *c, Col def) {
    if (c) {
        if (!strcmp(c, "red")) return G.C.RED;
        if (!strcmp(c, "mult")) return G.C.MULT;
        if (!strcmp(c, "blue")) return G.C.BLUE;
        if (!strcmp(c, "chips")) return G.C.CHIPS;
        if (!strcmp(c, "green")) return G.C.GREEN;
        if (!strcmp(c, "money")) return G.C.MONEY;
        if (!strcmp(c, "gold")) return G.C.GOLD;
        if (!strcmp(c, "attention")) return G.C.FILTER;
        if (!strcmp(c, "purple")) return G.C.PURPLE;
        if (!strcmp(c, "white")) return G.C.WHITE;
        if (!strcmp(c, "inactive")) return G.C.UI.TEXT_INACTIVE;
        if (!strcmp(c, "spades")) return G.C.SUITS.Spades;
        if (!strcmp(c, "hearts")) return G.C.SUITS.Hearts;
        if (!strcmp(c, "clubs")) return G.C.SUITS.Clubs;
        if (!strcmp(c, "diamonds")) return G.C.SUITS.Diamonds;
        if (!strcmp(c, "tarot")) return G.C.SECONDARY_SET.Tarot;
        if (!strcmp(c, "planet")) return G.C.SECONDARY_SET.Planet;
        if (!strcmp(c, "spectral")) return G.C.SECONDARY_SET.Spectral;
        if (!strcmp(c, "edition")) return G.C.EDITION;
        if (!strcmp(c, "dark_edition")) return G.C.DARK_EDITION;
        if (!strcmp(c, "legendary")) return G.C.RARITY[4];
        if (!strcmp(c, "enhanced")) return G.C.SECONDARY_SET.Enhanced;
    }
    return def ? def : G.C.UI.TEXT_DARK;
}

/* ------------------------------------------------------------------ */
/* full localize                                                       */
/* ------------------------------------------------------------------ */
static LocLines *loc_target_lines(LocArgs *a) {
    const char *type = a->type;
    if (!strcmp(type, "other")) {
        DVal *e = desc_entry("Other", a->key);
        if (!e) return NULL;
        return cached_lines(tfmt("desc/Other/%s/text", a->key), dv_get(e, "text"), false);
    }
    if (!strcmp(type, "descriptions") || !strcmp(type, "unlocks") || !strcmp(type, "name")) {
        const char *set = a->set ? a->set : (a->node_card ? a->node_card->config.center->set : NULL);
        const char *key = a->key ? a->key : (a->node_card ? a->node_card->config.center->key : NULL);
        if (!set || !key) return NULL;
        DVal *e = desc_entry(set, key);
        if (!e) return NULL;
        if (!strcmp(type, "unlocks")) {
            DVal *u = dv_get(e, "unlock");
            if (!u) return NULL;
            return cached_lines(tfmt("desc/%s/%s/unlock", set, key), u, false);
        }
        if (!strcmp(type, "name")) {
            DVal *nm = dv_get(e, "name");
            return cached_lines(tfmt("desc/%s/%s/name", set, key), nm, false);
        }
        return cached_lines(tfmt("desc/%s/%s/text", set, key), dv_get(e, "text"), false);
    }
    if (!strcmp(type, "tutorial")) {
        DVal *t = dv_get(loc_misc("tutorial"), a->key);
        if (!t) return NULL;
        return cached_lines(tfmt("tutorial/%s", a->key), t, true);
    }
    if (!strcmp(type, "quips")) {
        DVal *t = dv_get(loc_misc("quips"), a->key);
        if (!t) return NULL;
        return cached_lines(tfmt("quips/%s", a->key), t, true);
    }
    if (!strcmp(type, "text")) {
        DVal *t = dv_get(loc_misc("v_text"), a->key);
        if (!t) return NULL;
        return cached_lines(tfmt("vtext/%s", a->key), t, false);
    }
    return NULL;
}

UIDef *localize_ui(LocArgs a) {
    LocLines *lines = loc_target_lines(&a);
    if (!lines) return NULL;
    bool is_name = !strcmp(a.type, "name");
    bool is_text = !strcmp(a.type, "text");
    double desc_scale = G.LANG->font->DESCSCALE;
    if (G.F_MOBILE_UI) desc_scale *= 1.5;
    for (int li = 0; li < lines->nlines; li++) {
        LocLine *line = &lines->lines[li];
        UIDef *final_line = uinodes();
        for (int pi = 0; pi < line->nparts; pi++) {
            LocPart *part = &line->parts[pi];
            char *assembled = assemble(part, a.vars, "ERROR");
            const char *cV = ctrl_get(part, "V"), *cC = ctrl_get(part, "C"), *cs = ctrl_get(part, "s");
            const char *cE = ctrl_get(part, "E"), *cX = ctrl_get(part, "X"), *cT = ctrl_get(part, "T");
            double sfac = cs ? atof(cs) : 1;
            Col vcol = NULL;
            if (cV && a.vars) {
                int vi = atoi(cV);
                if (vi >= 1 && vi <= a.vars->ncolours) vcol = a.vars->colours[vi - 1];
            }
            if (is_name) {
                int alen = (int)strlen(assembled);
                Col col = vcol ? vcol : (cC ? loc_colour(cC, NULL) : G.C.UI.TEXT_LIGHT);
                DynaText *dt = DYNATEXT(.parts = dt_parts_str(assembled), .nparts = 1,
                                        .colours = dt_cols(1, col), .ncolours = 1,
                                        .bump = true, .silent = true, .pop_in = 0, .has_pop_in = true, .pop_in_rate = 4,
                                        .maxw = 5, .shadow = true, .y_offset = -0.6,
                                        .spacing = MAX(0, 0.32 * (17 - alen)),
                                        .scale = (0.55 - 0.004 * alen) * sfac);
                uidef_add(final_line, UIO(CFG(.object = &dt->n)));
            } else if (ctrl_has(part, "E")) {
                bool _float = false, _silent = true, _bump = false, has_pop_in = false;
                double _spacing = 0;
                if (!strcmp(cE, "1")) { _float = true; _silent = true; has_pop_in = true; }
                else if (!strcmp(cE, "2")) { _bump = true; _spacing = 1; }
                Col col = vcol ? vcol : loc_colour(cC, NULL);
                DynaText *dt = DYNATEXT(.parts = dt_parts_str(assembled), .nparts = 1,
                                        .colours = dt_cols(1, col), .ncolours = 1,
                                        .float_ = _float, .silent = _silent, .pop_in = 0, .has_pop_in = has_pop_in,
                                        .bump = _bump, .spacing = _spacing,
                                        .scale = 0.32 * sfac * desc_scale);
                uidef_add(final_line, UIO(CFG(.object = &dt->n)));
            } else if (ctrl_has(part, "X")) {
                uidef_add(final_line, UIC(CFG(.align = "m", .colour = loc_colour(cX, NULL), .r = 0.05, .padding = 0.03, .res = 0.15),
                    UIT(CFG(.text = assembled, .colour = loc_colour(cC, NULL), .scale = 0.32 * sfac * desc_scale))));
            } else {
                void *tt = NULL; int ttk = 0;
                if (cT) {
                    Center *ce = P_CENTER(cT);
                    if (ce) { tt = ce; ttk = 1; }
                    else { TagProto *tg = P_TAG(cT); if (tg) { tt = tg; ttk = 2; } }
                }
                uidef_add(final_line, UIT(CFG(.detailed_tooltip = tt, .detailed_tooltip_kind = ttk, .text = assembled,
                                              .shadow = a.shadow,
                                              .colour = vcol ? vcol : loc_colour(cC, a.default_col),
                                              .scale = 0.32 * sfac * desc_scale)));
            }
            free(assembled);
        }
        if (is_name || is_text) return final_line;
        if (a.nodes) uidef_add(a.nodes, final_line);
    }
    return NULL;
}
