#include "dval.h"

DVal dv_nil(void) { DVal v; memset(&v, 0, sizeof v); return v; }
DVal dv_bool(bool b) { DVal v = dv_nil(); v.t = DV_BOOL; v.b = b; return v; }
DVal dv_num(double n) { DVal v = dv_nil(); v.t = DV_NUM; v.n = n; return v; }
DVal dv_str(const char *s) { DVal v = dv_nil(); v.t = DV_STR; v.s = xstrdup(s ? s : ""); return v; }
DVal dv_table(void) {
    DVal v = dv_nil();
    v.t = DV_TABLE;
    v.tab = NEW(DTable);
    strmap_init(&v.tab->index);
    return v;
}
DVal *dv_new_table(void) {
    DVal *v = NEW(DVal);
    *v = dv_table();
    return v;
}
bool dv_is_table(const DVal *v) { return v && v->t == DV_TABLE; }

void dv_free(DVal *v) {
    if (!v) return;
    if (v->t == DV_STR) free(v->s);
    else if (v->t == DV_TABLE) {
        DTable *t = v->tab;
        for (int i = 0; i < t->count; i++) {
            if (t->keys[i].is_str) free(t->keys[i].s);
            dv_free(&t->vals[i]);
        }
        free(t->keys); free(t->vals);
        strmap_free(&t->index);
        free(t);
    }
    *v = dv_nil();
}

DVal dv_copy(const DVal *v) {
    if (!v) return dv_nil();
    switch (v->t) {
    case DV_STR: return dv_str(v->s);
    case DV_TABLE: {
        DVal r = dv_table();
        for (int i = 0; i < v->tab->count; i++) {
            DKey *k = &v->tab->keys[i];
            if (k->is_str) dv_set(&r, k->s, dv_copy(&v->tab->vals[i]));
            else dv_seti(&r, (int)k->n, dv_copy(&v->tab->vals[i]));
        }
        return r;
    }
    default: return *v;
    }
}

static int find_str(const DTable *t, const char *key) {
    void *p = strmap_get(&t->index, key);
    return p ? (int)(intptr_t)p - 1 : -1;
}
static int find_int(const DTable *t, double idx) {
    /* fast path: array part laid out in order */
    int i = (int)idx - 1;
    if (i >= 0 && i < t->count && !t->keys[i].is_str && t->keys[i].n == idx) return i;
    for (int j = 0; j < t->count; j++) if (!t->keys[j].is_str && t->keys[j].n == idx) return j;
    return -1;
}
static void remove_entry(DTable *t, int i) {
    if (t->keys[i].is_str) { strmap_del(&t->index, t->keys[i].s); free(t->keys[i].s); }
    dv_free(&t->vals[i]);
    memmove(&t->keys[i], &t->keys[i + 1], (size_t)(t->count - i - 1) * sizeof(DKey));
    memmove(&t->vals[i], &t->vals[i + 1], (size_t)(t->count - i - 1) * sizeof(DVal));
    t->count--;
    /* reindex string keys after i */
    for (int j = i; j < t->count; j++)
        if (t->keys[j].is_str) strmap_set(&t->index, t->keys[j].s, (void *)(intptr_t)(j + 1));
}
static void add_entry(DTable *t, DKey k, DVal v) {
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 8;
        t->keys = xrealloc(t->keys, (size_t)t->cap * sizeof(DKey));
        t->vals = xrealloc(t->vals, (size_t)t->cap * sizeof(DVal));
    }
    t->keys[t->count] = k;
    t->vals[t->count] = v;
    if (k.is_str) strmap_set(&t->index, k.s, (void *)(intptr_t)(t->count + 1));
    t->count++;
}

DVal *dv_get(const DVal *t, const char *key) {
    if (!t || t->t != DV_TABLE || !key) return NULL;
    int i = find_str(t->tab, key);
    return i >= 0 ? &t->tab->vals[i] : NULL;
}
DVal *dv_geti(const DVal *t, int idx) {
    if (!t || t->t != DV_TABLE) return NULL;
    int i = find_int(t->tab, idx);
    return i >= 0 ? &t->tab->vals[i] : NULL;
}
DVal *dv_path(const DVal *t, const char *path) {
    char buf[256];
    const DVal *cur = t;
    while (cur && *path) {
        const char *dot = strchr(path, '.');
        size_t n = dot ? (size_t)(dot - path) : strlen(path);
        if (n >= sizeof buf) return NULL;
        memcpy(buf, path, n); buf[n] = 0;
        cur = dv_get(cur, buf);
        path = dot ? dot + 1 : path + n;
    }
    return (DVal *)cur;
}
void dv_set(DVal *t, const char *key, DVal v) {
    if (!t || t->t != DV_TABLE) { dv_free(&v); return; }
    int i = find_str(t->tab, key);
    if (v.t == DV_NIL) { if (i >= 0) remove_entry(t->tab, i); return; }
    if (i >= 0) { dv_free(&t->tab->vals[i]); t->tab->vals[i] = v; return; }
    DKey k = { true, xstrdup(key), 0 };
    add_entry(t->tab, k, v);
}
void dv_seti(DVal *t, int idx, DVal v) {
    if (!t || t->t != DV_TABLE) { dv_free(&v); return; }
    int i = find_int(t->tab, idx);
    if (v.t == DV_NIL) { if (i >= 0) remove_entry(t->tab, i); return; }
    if (i >= 0) { dv_free(&t->tab->vals[i]); t->tab->vals[i] = v; return; }
    DKey k = { false, NULL, idx };
    add_entry(t->tab, k, v);
}
int dv_len(const DVal *t) {
    if (!t || t->t != DV_TABLE) return 0;
    int n = 0;
    while (dv_geti(t, n + 1)) n++;
    return n;
}
void dv_push(DVal *t, DVal v) { dv_seti(t, dv_len(t) + 1, v); }
DVal *dv_get_or_create_table(DVal *t, const char *key) {
    DVal *v = dv_get(t, key);
    if (v && v->t == DV_TABLE) return v;
    dv_set(t, key, dv_table());
    return dv_get(t, key);
}

double dv_num_or(const DVal *v, double fallback) { return (v && v->t == DV_NUM) ? v->n : fallback; }
const char *dv_str_or(const DVal *v, const char *fallback) { return (v && v->t == DV_STR) ? v->s : fallback; }
bool dv_truthy(const DVal *v) { return v && v->t != DV_NIL && !(v->t == DV_BOOL && !v->b); }
double dv_get_num(const DVal *t, const char *key, double fallback) { return dv_num_or(dv_get(t, key), fallback); }
const char *dv_get_str(const DVal *t, const char *key, const char *fallback) { return dv_str_or(dv_get(t, key), fallback); }
bool dv_get_bool(const DVal *t, const char *key, bool fallback) {
    DVal *v = dv_get(t, key);
    if (!v || v->t == DV_NIL) return fallback;
    return dv_truthy(v);
}
void dv_set_num(DVal *t, const char *key, double n) { dv_set(t, key, dv_num(n)); }
void dv_set_str(DVal *t, const char *key, const char *s) { dv_set(t, key, s ? dv_str(s) : dv_nil()); }
void dv_set_bool(DVal *t, const char *key, bool b) { dv_set(t, key, dv_bool(b)); }

int dv_count(const DVal *t) { return (t && t->t == DV_TABLE) ? t->tab->count : 0; }
const DKey *dv_key(const DVal *t, int i) { return &t->tab->keys[i]; }
DVal *dv_val(const DVal *t, int i) { return &t->tab->vals[i]; }

/* ------------------------------------------------------------------ */
/* Parser for Lua table literals                                       */
/* ------------------------------------------------------------------ */
typedef struct { const char *p; bool err; } Parser;

static void skip_ws(Parser *ps) {
    for (;;) {
        while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') ps->p++;
        if (ps->p[0] == '-' && ps->p[1] == '-') {
            ps->p += 2;
            if (ps->p[0] == '[' && (ps->p[1] == '[' || ps->p[1] == '=')) {
                /* long comment */
                const char *q = ps->p + 1; int eq = 0;
                while (*q == '=') { eq++; q++; }
                if (*q == '[') {
                    q++;
                    for (;;) {
                        if (!*q) { ps->p = q; return; }
                        if (*q == ']') {
                            const char *r = q + 1; int e2 = 0;
                            while (*r == '=') { e2++; r++; }
                            if (e2 == eq && *r == ']') { ps->p = r + 1; break; }
                        }
                        q++;
                    }
                    continue;
                }
            }
            while (*ps->p && *ps->p != '\n') ps->p++;
            continue;
        }
        break;
    }
}

static char *parse_string(Parser *ps) {
    char q = *ps->p++;
    StrBuf sb; sb_init(&sb);
    while (*ps->p && *ps->p != q) {
        char c = *ps->p++;
        if (c == '\\') {
            char e = *ps->p++;
            switch (e) {
            case 'n': sb_addc(&sb, '\n'); break;
            case 't': sb_addc(&sb, '\t'); break;
            case 'r': sb_addc(&sb, '\r'); break;
            case 'a': sb_addc(&sb, '\a'); break;
            case 'b': sb_addc(&sb, '\b'); break;
            case 'f': sb_addc(&sb, '\f'); break;
            case 'v': sb_addc(&sb, '\v'); break;
            case '\\': sb_addc(&sb, '\\'); break;
            case '"': sb_addc(&sb, '"'); break;
            case '\'': sb_addc(&sb, '\''); break;
            case '\n': sb_addc(&sb, '\n'); break;
            case '\r': sb_addc(&sb, '\n'); if (*ps->p == '\n') ps->p++; break;
            default:
                if (e >= '0' && e <= '9') {
                    int v = e - '0';
                    for (int k = 0; k < 2 && *ps->p >= '0' && *ps->p <= '9'; k++) v = v * 10 + (*ps->p++ - '0');
                    sb_addc(&sb, (char)v);
                } else sb_addc(&sb, e);
            }
        } else sb_addc(&sb, c);
    }
    if (*ps->p == q) ps->p++; else ps->err = true;
    return sb_take(&sb);
}

static char *parse_long_string(Parser *ps) {
    /* [[ ... ]] or [==[ ... ]==] */
    ps->p++;
    int eq = 0;
    while (*ps->p == '=') { eq++; ps->p++; }
    if (*ps->p != '[') { ps->err = true; return xstrdup(""); }
    ps->p++;
    if (*ps->p == '\n') ps->p++;
    const char *start = ps->p;
    for (;;) {
        if (!*ps->p) { ps->err = true; return xstrdup(""); }
        if (*ps->p == ']') {
            const char *r = ps->p + 1; int e2 = 0;
            while (*r == '=') { e2++; r++; }
            if (e2 == eq && *r == ']') {
                char *s = xstrndup(start, (size_t)(ps->p - start));
                ps->p = r + 1;
                return s;
            }
        }
        ps->p++;
    }
}

static bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_ident(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

static DVal parse_value(Parser *ps);

static DVal parse_table(Parser *ps) {
    DVal t = dv_table();
    ps->p++; /* { */
    int arr_idx = 1;
    for (;;) {
        skip_ws(ps);
        if (!*ps->p) { ps->err = true; break; }
        if (*ps->p == '}') { ps->p++; break; }
        if (*ps->p == '[' && ps->p[1] != '[' && ps->p[1] != '=') {
            ps->p++;
            skip_ws(ps);
            DVal k = parse_value(ps);
            skip_ws(ps);
            if (*ps->p == ']') ps->p++; else ps->err = true;
            skip_ws(ps);
            if (*ps->p == '=') ps->p++; else ps->err = true;
            skip_ws(ps);
            DVal v = parse_value(ps);
            if (k.t == DV_STR) dv_set(&t, k.s, v);
            else if (k.t == DV_NUM) dv_seti(&t, (int)k.n, v);
            else dv_free(&v);
            dv_free(&k);
        } else if (is_ident_start(*ps->p)) {
            const char *s = ps->p;
            while (is_ident(*ps->p)) ps->p++;
            const char *e = ps->p;
            Parser look = *ps;
            skip_ws(&look);
            if (*look.p == '=' && look.p[1] != '=') {
                char *key = xstrndup(s, (size_t)(e - s));
                ps->p = look.p + 1;
                skip_ws(ps);
                DVal v = parse_value(ps);
                dv_set(&t, key, v);
                free(key);
            } else {
                ps->p = s;
                DVal v = parse_value(ps);
                dv_seti(&t, arr_idx++, v);
            }
        } else {
            DVal v = parse_value(ps);
            dv_seti(&t, arr_idx++, v);
        }
        skip_ws(ps);
        if (*ps->p == ',' || *ps->p == ';') ps->p++;
        if (ps->err) break;
    }
    return t;
}

static DVal parse_value(Parser *ps) {
    skip_ws(ps);
    char c = *ps->p;
    if (c == '{') return parse_table(ps);
    if (c == '"' || c == '\'') { DVal v = dv_nil(); v.t = DV_STR; v.s = parse_string(ps); return v; }
    if (c == '[' && (ps->p[1] == '[' || ps->p[1] == '=')) { DVal v = dv_nil(); v.t = DV_STR; v.s = parse_long_string(ps); return v; }
    if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
        char *end;
        double d = strtod(ps->p, &end);
        if (end == ps->p) { ps->err = true; ps->p++; return dv_nil(); }
        ps->p = end;
        return dv_num(d);
    }
    if (is_ident_start(c)) {
        const char *s = ps->p;
        while (is_ident(*ps->p) || *ps->p == '.') ps->p++;
        size_t n = (size_t)(ps->p - s);
        if (n == 4 && !strncmp(s, "true", 4)) return dv_bool(true);
        if (n == 5 && !strncmp(s, "false", 5)) return dv_bool(false);
        /* nil, or an unknown global (e.g. "inf" written by tostring) -> nil like loadstring */
        return dv_nil();
    }
    ps->err = true;
    if (*ps->p) ps->p++;
    return dv_nil();
}

bool dv_parse(const char *src, DVal *out) {
    Parser ps = { src, false };
    skip_ws(&ps);
    if (!strncmp(ps.p, "return", 6)) ps.p += 6;
    skip_ws(&ps);
    /* allow "G.CHALLENGES = {" style prefixes */
    const char *brace = strchr(ps.p, '{');
    if (*ps.p != '{' && brace) {
        const char *q = ps.p;
        bool simple = true;
        for (; q < brace; q++) if (!(is_ident(*q) || *q == '.' || *q == ' ' || *q == '=' || *q == '\t')) simple = false;
        if (simple) ps.p = brace;
    }
    *out = parse_value(&ps);
    return !ps.err;
}

/* ------------------------------------------------------------------ */
/* Serializer (mirrors STR_PACK)                                       */
/* ------------------------------------------------------------------ */
static void write_quoted(StrBuf *sb, const char *s) {
    sb_addc(sb, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') { sb_addc(sb, '\\'); sb_addc(sb, (char)c); }
        else if (c == '\n') { sb_addc(sb, '\\'); sb_addc(sb, '\n'); }
        else if (c == '\r') sb_add(sb, "\\r");
        else if (c == 0) sb_add(sb, "\\000");
        else if (c < 32 || c == 127) sb_addf(sb, "\\%d", c);
        else sb_addc(sb, (char)c);
    }
    sb_addc(sb, '"');
}
static void write_value(StrBuf *sb, const DVal *v) {
    switch (v->t) {
    case DV_NIL: sb_add(sb, "nil"); break;
    case DV_BOOL: sb_add(sb, v->b ? "true" : "false"); break;
    case DV_NUM: sb_add(sb, num_str(v->n)); break;
    case DV_STR: write_quoted(sb, v->s); break;
    case DV_TABLE:
        sb_addc(sb, '{');
        for (int i = 0; i < v->tab->count; i++) {
            DKey *k = &v->tab->keys[i];
            sb_addc(sb, '[');
            if (k->is_str) write_quoted(sb, k->s); else sb_add(sb, num_str(k->n));
            sb_add(sb, "]=");
            write_value(sb, &v->tab->vals[i]);
            sb_addc(sb, ',');
        }
        sb_addc(sb, '}');
        break;
    }
}
char *dv_serialize(const DVal *v) {
    StrBuf sb; sb_init(&sb);
    sb_add(&sb, "return ");
    write_value(&sb, v);
    return sb_take(&sb);
}

bool dv_load_file(const char *path, DVal *out) {
    int len;
    char *raw = file_read_all(path, &len);
    *out = dv_nil();
    if (!raw) return false;
    char *text = raw;
    int tl = len;
    if (len > 0 && strncmp(raw, "return", 6) != 0 && raw[0] != '{') {
        char *dec = deflate_decompress(raw, len, &tl);
        if (dec) { text = dec; free(raw); raw = NULL; }
    }
    (void)tl;
    bool ok = dv_parse(text, out);
    free(raw ? raw : text);
    return ok;
}
bool dv_save_file(const char *path_in, const DVal *v, bool compress) {
    /* callers often pass a tfmt() temporary; serializing reuses that ring */
    char *path = xstrdup(path_in);
    char *s = dv_serialize(v);
    bool ok;
    if (compress) {
        int cl;
        char *c = deflate_compress(s, (int)strlen(s), &cl);
        ok = c && file_write_all(path, c, cl);
        free(c);
    } else ok = file_write_all(path, s, (int)strlen(s));
    free(s);
    free(path);
    return ok;
}
