#define CORE_ALLOC_INTERNAL
#include "core.h"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* Pointer-holding blocks start zeroed: stale pointers left in reused memory
 * would otherwise keep removed nodes alive in heap_collect. */
void *xmalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    heap_track(p, n);
    return p;
}
void *xmalloc_atomic(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    heap_track_atomic(p, n);
    return p;
}
void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    heap_track(p, n * sz);
    return p;
}
void *xrealloc(void *p, size_t n) {
    size_t old = p ? heap_block_size(p) : 0;
    bool tracked = heap_untrack(p);
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "out of memory\n"); abort(); }
    if ((tracked || !p) && n > old) memset((char *)q + old, 0, n - old);
    if (tracked || !p) heap_track(q, n);
    return q;
}
void xfree(void *p) {
    if (!p) return;
    heap_untrack(p);
    free(p);
}

/* ------------------------------------------------------------------ */
int ptrvec_find(const PtrVec *v, const void *p) {
    for (int i = 0; i < v->count; i++) if (v->items[i] == p) return i;
    return -1;
}
bool ptrvec_remove(PtrVec *v, const void *p) {
    int i = ptrvec_find(v, p);
    if (i < 0) return false;
    vec_remove_at(v, i);
    return true;
}
void ptrvec_remove_all(PtrVec *v, const void *p) {
    int j = 0;
    for (int i = 0; i < v->count; i++) if (v->items[i] != p) v->items[j++] = v->items[i];
    for (int i = j; i < v->count; i++) v->items[i] = NULL;
    v->count = j;
}
bool ptrvec_contains(const PtrVec *v, const void *p) { return ptrvec_find(v, p) >= 0; }

/* ------------------------------------------------------------------ */
char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = xmalloc_atomic(n + 1);
    memcpy(r, s, n + 1);
    return r;
}
char *xstrndup(const char *s, size_t n) {
    char *r = xmalloc_atomic(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}
char *str_fmt(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *r = xmalloc((size_t)n + 1);
    vsnprintf(r, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return r;
}

/* Temporary strings: a ring of buffers. Results stay valid until the ring
 * wraps around, which is plenty for building a string and handing it to a
 * function that copies it. */
#define TRING_COUNT 256
static char *tring[TRING_COUNT];
static int tring_cap[TRING_COUNT];
static int tring_idx;
static char *tring_get(int need) {
    tring_idx = (tring_idx + 1) % TRING_COUNT;
    if (tring_cap[tring_idx] < need) {
        tring[tring_idx] = xrealloc(tring[tring_idx], (size_t)need);
        tring_cap[tring_idx] = need;
    }
    return tring[tring_idx];
}
const char *tfmt(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *r = tring_get(n + 1);
    vsnprintf(r, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return r;
}
const char *tstr(const char *s) {
    if (!s) return NULL;
    int n = (int)strlen(s);
    char *r = tring_get(n + 1);
    memcpy(r, s, (size_t)n + 1);
    return r;
}
void str_set(char **dst, const char *src) {
    if (*dst == src) return;
    char *n = src ? xstrdup(src) : NULL;
    xfree(*dst);
    *dst = n;
}
static StrMap intern_map;
static bool intern_init;
const char *intern(const char *s) {
    if (!s) return NULL;
    if (!intern_init) { strmap_init(&intern_map); intern_init = true; }
    char *v = strmap_get(&intern_map, s);
    if (v) return v;
    v = xstrdup(s);
    strmap_set(&intern_map, s, v);
    return v;
}
bool str_eq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}
bool str_starts(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}
bool str_contains(const char *s, const char *needle) {
    if (!s || !needle) return false;
    return strstr(s, needle) != NULL;
}
bool str_has_char(const char *s, char c) { return s && strchr(s, c) != NULL; }
char *str_cat(const char *a, const char *b) {
    a = a ? a : ""; b = b ? b : "";
    size_t na = strlen(a), nb = strlen(b);
    char *r = xmalloc(na + nb + 1);
    memcpy(r, a, na);
    memcpy(r + na, b, nb + 1);
    return r;
}
const char *tcat(const char *a, const char *b) {
    a = a ? a : ""; b = b ? b : "";
    size_t na = strlen(a), nb = strlen(b);
    char *r = tring_get((int)(na + nb + 1));
    memcpy(r, a, na);
    memcpy(r + na, b, nb + 1);
    return r;
}
const char *str_or(const char *s, const char *fallback) { return s ? s : fallback; }

int utf8_decode(const char *s, int *len) {
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) { *len = 1; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0 && u[1]) { *len = 2; return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); }
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) { *len = 3; return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); }
    if ((u[0] & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) {
        *len = 4;
        return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    }
    *len = 1;
    return u[0];
}
int utf8_len(const char *s) {
    int n = 0, l;
    while (*s) { utf8_decode(s, &l); s += l; n++; }
    return n;
}
static int encode_utf8(int cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
/* Lua's string.upper only touches ASCII. The game's localisation files use
 * string.upper too, so ASCII-only matches the original behaviour. */
char *str_upper(const char *s) {
    char *r = xstrdup(s ? s : "");
    for (char *p = r; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    return r;
}
const char *tupper(const char *s) {
    const char *t = tstr(s ? s : "");
    for (char *p = (char *)t; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    return t;
}
const char *tlower(const char *s) {
    const char *t = tstr(s ? s : "");
    for (char *p = (char *)t; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    return t;
}
static void unused_encode(void) { char b[4]; (void)encode_utf8(0, b); }

const char *num_str(double v) {
    (void)unused_encode;
    if (isnan(v)) return "nan";
    if (isinf(v)) return v > 0 ? "inf" : "-inf";
    if (v == floor(v) && fabs(v) < 1e15) return tfmt("%.0f", v);
    return tfmt("%.14g", v);
}
const char *num_int_str(double v) {
    if (isnan(v) || isinf(v)) return num_str(v);
    return tfmt("%lld", (long long)v);
}

/* ------------------------------------------------------------------ */
void sb_init(StrBuf *sb) { sb->buf = NULL; sb->len = sb->cap = 0; }
static void sb_grow(StrBuf *sb, int extra) {
    if (sb->len + extra + 1 > sb->cap) {
        int nc = sb->cap ? sb->cap : 64;
        while (nc < sb->len + extra + 1) nc *= 2;
        sb->buf = xrealloc(sb->buf, (size_t)nc);
        sb->cap = nc;
    }
}
void sb_addn(StrBuf *sb, const char *s, int n) {
    sb_grow(sb, n);
    memcpy(sb->buf + sb->len, s, (size_t)n);
    sb->len += n;
    sb->buf[sb->len] = 0;
}
void sb_add(StrBuf *sb, const char *s) { if (s) sb_addn(sb, s, (int)strlen(s)); }
void sb_addc(StrBuf *sb, char c) { sb_addn(sb, &c, 1); }
void sb_addf(StrBuf *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    sb_grow(sb, n);
    vsnprintf(sb->buf + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += n;
}
char *sb_take(StrBuf *sb) {
    char *r = sb->buf ? sb->buf : xstrdup("");
    sb->buf = NULL; sb->len = sb->cap = 0;
    return r;
}
void sb_free(StrBuf *sb) { xfree(sb->buf); sb_init(sb); }

/* ------------------------------------------------------------------ */
char STRMAP_TOMBSTONE_[1];
static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}
void strmap_init(StrMap *m) { m->keys = NULL; m->vals = NULL; m->cap = m->count = 0; }
void strmap_free(StrMap *m) {
    for (int i = 0; i < m->cap; i++) if (STRMAP_LIVE(m, i)) xfree(m->keys[i]);
    xfree(m->keys); xfree(m->vals);
    strmap_init(m);
}
static int strmap_slot(const StrMap *m, const char *key) {
    if (!m->cap) return -1;
    uint32_t h = hash_str(key) & (uint32_t)(m->cap - 1);
    for (int n = 0; n < m->cap; n++) {
        char *k = m->keys[h];
        if (!k) return -1;
        if (k != STRMAP_TOMBSTONE && strcmp(k, key) == 0) return (int)h;
        h = (h + 1) & (uint32_t)(m->cap - 1);
    }
    return -1;
}
static void strmap_rehash(StrMap *m, int ncap) {
    char **ok = m->keys; void **ov = m->vals; int oc = m->cap;
    m->keys = xcalloc((size_t)ncap, sizeof(char *));
    m->vals = xcalloc((size_t)ncap, sizeof(void *));
    m->cap = ncap; m->count = 0;
    for (int i = 0; i < oc; i++) {
        if (ok[i] && ok[i] != STRMAP_TOMBSTONE) {
            uint32_t h = hash_str(ok[i]) & (uint32_t)(ncap - 1);
            while (m->keys[h]) h = (h + 1) & (uint32_t)(ncap - 1);
            m->keys[h] = ok[i]; m->vals[h] = ov[i]; m->count++;
        }
    }
    xfree(ok); xfree(ov);
}
void strmap_set(StrMap *m, const char *key, void *val) {
    int s = strmap_slot(m, key);
    if (s >= 0) { m->vals[s] = val; return; }
    if ((m->count + 1) * 2 > m->cap) strmap_rehash(m, m->cap ? m->cap * 2 : 16);
    uint32_t h = hash_str(key) & (uint32_t)(m->cap - 1);
    while (m->keys[h] && m->keys[h] != STRMAP_TOMBSTONE) h = (h + 1) & (uint32_t)(m->cap - 1);
    m->keys[h] = xstrdup(key); m->vals[h] = val; m->count++;
}
void *strmap_get(const StrMap *m, const char *key) {
    if (!key) return NULL;
    int s = strmap_slot(m, key);
    return s >= 0 ? m->vals[s] : NULL;
}
bool strmap_has(const StrMap *m, const char *key) { return key && strmap_slot(m, key) >= 0; }
bool strmap_del(StrMap *m, const char *key) {
    int s = strmap_slot(m, key);
    if (s < 0) return false;
    xfree(m->keys[s]);
    m->keys[s] = STRMAP_TOMBSTONE;
    m->vals[s] = NULL;
    return true;
}

void nummap_init(NumMap *m) { m->keys = NULL; m->vals = NULL; m->cap = m->count = 0; }
void nummap_free(NumMap *m) {
    for (int i = 0; i < m->cap; i++) if (NUMMAP_LIVE(m, i)) xfree(m->keys[i]);
    xfree(m->keys); xfree(m->vals);
    nummap_init(m);
}
void nummap_clear(NumMap *m) { nummap_free(m); }
static int nummap_slot(const NumMap *m, const char *key) {
    if (!m->cap || !key) return -1;
    uint32_t h = hash_str(key) & (uint32_t)(m->cap - 1);
    for (int n = 0; n < m->cap; n++) {
        char *k = m->keys[h];
        if (!k) return -1;
        if (k != STRMAP_TOMBSTONE && strcmp(k, key) == 0) return (int)h;
        h = (h + 1) & (uint32_t)(m->cap - 1);
    }
    return -1;
}
static void nummap_rehash(NumMap *m, int ncap) {
    char **ok = m->keys; double *ov = m->vals; int oc = m->cap;
    m->keys = xcalloc((size_t)ncap, sizeof(char *));
    m->vals = xcalloc((size_t)ncap, sizeof(double));
    m->cap = ncap; m->count = 0;
    for (int i = 0; i < oc; i++) {
        if (ok[i] && ok[i] != STRMAP_TOMBSTONE) {
            uint32_t h = hash_str(ok[i]) & (uint32_t)(ncap - 1);
            while (m->keys[h]) h = (h + 1) & (uint32_t)(ncap - 1);
            m->keys[h] = ok[i]; m->vals[h] = ov[i]; m->count++;
        }
    }
    xfree(ok); xfree(ov);
}
void nummap_set(NumMap *m, const char *key, double v) {
    int s = nummap_slot(m, key);
    if (s >= 0) { m->vals[s] = v; return; }
    if ((m->count + 1) * 2 > m->cap) nummap_rehash(m, m->cap ? m->cap * 2 : 16);
    uint32_t h = hash_str(key) & (uint32_t)(m->cap - 1);
    while (m->keys[h] && m->keys[h] != STRMAP_TOMBSTONE) h = (h + 1) & (uint32_t)(m->cap - 1);
    m->keys[h] = xstrdup(key); m->vals[h] = v; m->count++;
}
double nummap_get(const NumMap *m, const char *key, double fallback) {
    int s = nummap_slot(m, key);
    return s >= 0 ? m->vals[s] : fallback;
}
bool nummap_has(const NumMap *m, const char *key) { return nummap_slot(m, key) >= 0; }
void nummap_del(NumMap *m, const char *key) {
    int s = nummap_slot(m, key);
    if (s < 0) return;
    xfree(m->keys[s]);
    m->keys[s] = STRMAP_TOMBSTONE;
}

/* ------------------------------------------------------------------ */
/* LuaJIT 2.1 math.random: Tausworthe TW223 generator, bit-exact so that
 * seeded runs produce the same shops/draws as the original game. */
typedef union { uint64_t u64; double d; } U64double;
#define TW223_GEN(i, k, q, s) \
    z = r->gen[i]; \
    z = (((z << q) ^ z) >> (k - s)) ^ ((z & ((uint64_t)(int64_t)-1 << (64 - k))) << s); \
    acc ^= z; r->gen[i] = z;
static uint64_t lj_step(LJRandom *r) {
    uint64_t z, acc = 0;
    TW223_GEN(0, 63, 31, 18)
    TW223_GEN(1, 58, 19, 28)
    TW223_GEN(2, 55, 24, 7)
    TW223_GEN(3, 47, 21, 8)
    return (acc & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
}
void lj_randomseed(LJRandom *r, double d) {
    uint32_t k = 0x11090601;
    for (int i = 0; i < 4; i++) {
        U64double u;
        uint32_t m = 1u << (k & 255);
        k >>= 8;
        u.d = d = d * 3.14159265358979323846 + 2.7182818284590452354;
        if (u.u64 < m) u.u64 += m;
        r->gen[i] = u.u64;
    }
    for (int i = 0; i < 10; i++) lj_step(r);
}
double lj_random(LJRandom *r) {
    U64double u;
    u.u64 = lj_step(r);
    return u.d - 1.0;
}
double lj_random_int(LJRandom *r, double lo, double hi) {
    double d = lj_random(r);
    return floor(d * (hi - lo + 1.0)) + lo;
}
LJRandom g_math_random;
static bool g_math_random_init;
double math_random(void) {
    if (!g_math_random_init) { lj_randomseed(&g_math_random, 0.0); g_math_random_init = true; }
    return lj_random(&g_math_random);
}
int math_random_range(int lo, int hi) {
    if (!g_math_random_init) { lj_randomseed(&g_math_random, 0.0); g_math_random_init = true; }
    return (int)lj_random_int(&g_math_random, lo, hi);
}
void math_randomseed(double seed) {
    lj_randomseed(&g_math_random, seed);
    g_math_random_init = true;
}

/* ------------------------------------------------------------------ */
char *file_read_all(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc_atomic((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    if (out_len) *out_len = (int)got;
    return buf;
}
bool file_write_all(const char *path, const char *data, int len) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    size_t put = fwrite(data, 1, (size_t)len, f);
    fclose(f);
    if (put != (size_t)len) { remove(tmp); return false; }
    /* the SD card filesystem cannot rename over an existing file */
    remove(path);
    return rename(tmp, path) == 0;
}
bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}
bool dir_make(const char *path) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    /* skip the "sdmc:/" device prefix */
    char *start = strchr(buf, ':');
    start = start ? start + 2 : buf + 1;
    for (char *p = start; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
    }
    return mkdir(buf, 0755) == 0 || errno == EEXIST;
}
bool file_remove(const char *path) { return remove(path) == 0; }

/* saves live on the SD card next to the usual homebrew folders */
const char *save_dir(void) {
    static char dir[256];
    if (!dir[0]) {
        snprintf(dir, sizeof dir, "sdmc:/3ds/balatro_c");
        dir_make(dir);
    }
    return dir;
}
static char resource_root[1024] = "romfs:";
void set_resource_root(const char *root) { snprintf(resource_root, sizeof resource_root, "%s", root); }
const char *res_path(const char *rel) { return tfmt("%s/%s", resource_root, rel); }

const char *res_save_path(const char *rel) { return tfmt("%s/%s", save_dir(), rel); }

/* port of ltablib.c auxsort (Lua 5.1, identical in LuaJIT 2.0 lib_table.c); a is 1-based */
static void lts_aux(void **a, int l, int u, LuaLess lt, void *ctx) {
#define A(i) a[(i) - 1]
#define SWAP(i, j) do { void *t_ = A(i); A(i) = A(j); A(j) = t_; } while (0)
    while (l < u) {
        int i, j;
        if (lt(A(u), A(l), ctx)) SWAP(l, u);
        if (u - l == 1) break;
        i = (l + u) / 2;
        if (lt(A(i), A(l), ctx)) SWAP(i, l);
        else if (lt(A(u), A(i), ctx)) SWAP(i, u);
        if (u - l == 2) break;
        void *P = A(i);
        SWAP(i, u - 1);   /* a[i] = a[u-1]; a[u-1] = P */
        i = l; j = u - 1;
        for (;;) {
            while (lt(A(++i), P, ctx)) { if (i > u) break; }
            while (lt(P, A(--j), ctx)) { if (j < l) break; }
            if (j < i) break;
            SWAP(i, j);
        }
        SWAP(u - 1, i);
        if (i - l < u - i) { j = l; i = i - 1; l = i + 2; }
        else { j = i + 1; i = u; u = j - 2; }
        lts_aux(a, j, i, lt, ctx);
    }
#undef A
#undef SWAP
}
void lua_table_sort(void **arr, int n, LuaLess lt, void *ctx) {
    if (n > 1) lts_aux(arr, 1, n, lt, ctx);
}
