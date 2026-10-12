/* core.h - basic runtime utilities shared by every module of the C port.
 *
 * The original game is written in Lua and leans heavily on dynamic tables.
 * This header provides the small set of building blocks the port uses in
 * their place: growable arrays, string helpers, a string-keyed hash map,
 * Lua-compatible number formatting and math helpers. */
#ifndef CORE_H
#define CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ARRLEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
#define NEW(T) ((T *)xcalloc(1, sizeof(T)))

/* ------------------------------------------------------------------ */
/* Growable arrays                                                     */
/* ------------------------------------------------------------------ */
#define VEC(T) struct { T *items; int count, cap; }

#define vec_reserve(v, n) do { \
    if ((n) > (v)->cap) { \
        int _nc = (v)->cap ? (v)->cap : 8; \
        while (_nc < (n)) _nc *= 2; \
        (v)->items = xrealloc((v)->items, (size_t)_nc * sizeof(*(v)->items)); \
        (v)->cap = _nc; \
    } } while (0)

#define vec_push(v, x) do { vec_reserve((v), (v)->count + 1); (v)->items[(v)->count++] = (x); } while (0)

#define vec_insert(v, idx, x) do { \
    int _i = (idx); \
    vec_reserve((v), (v)->count + 1); \
    memmove(&(v)->items[_i + 1], &(v)->items[_i], (size_t)((v)->count - _i) * sizeof(*(v)->items)); \
    (v)->items[_i] = (x); (v)->count++; } while (0)

#define vec_remove_at(v, idx) do { \
    int _i = (idx); \
    memmove(&(v)->items[_i], &(v)->items[_i + 1], (size_t)((v)->count - _i - 1) * sizeof(*(v)->items)); \
    (v)->count--; } while (0)

#define vec_clear(v) ((v)->count = 0)
#define vec_free(v) do { free((v)->items); (v)->items = NULL; (v)->count = (v)->cap = 0; } while (0)
#define vec_last(v) ((v)->items[(v)->count - 1])
#define vec_pop(v) ((v)->items[--(v)->count])

typedef VEC(void *) PtrVec;
typedef VEC(double) NumVec;
typedef VEC(char *) StrVec;
typedef VEC(int) IntVec;

int ptrvec_find(const PtrVec *v, const void *p);
bool ptrvec_remove(PtrVec *v, const void *p);      /* removes first occurrence */
void ptrvec_remove_all(PtrVec *v, const void *p);  /* removes every occurrence */
bool ptrvec_contains(const PtrVec *v, const void *p);

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *str_fmt(const char *fmt, ...);               /* heap allocated, caller frees */
const char *tfmt(const char *fmt, ...);            /* temporary ring-buffer string */
const char *tstr(const char *s);                   /* copy into the temp ring */
void str_set(char **dst, const char *src);         /* free old, dup new (NULL allowed) */
const char *intern(const char *s);                 /* permanent, deduplicated copy */
bool str_eq(const char *a, const char *b);         /* NULL safe */
bool str_starts(const char *s, const char *prefix);
bool str_contains(const char *s, const char *needle);
bool str_has_char(const char *s, char c);
char *str_cat(const char *a, const char *b);       /* heap */
const char *tcat(const char *a, const char *b);    /* temp */
const char *str_or(const char *s, const char *fallback);
int utf8_decode(const char *s, int *len);          /* returns codepoint, *len = bytes */
int utf8_len(const char *s);                       /* number of codepoints */
char *str_upper(const char *s);                    /* heap, ASCII + basic latin upper */
const char *tupper(const char *s);
const char *tlower(const char *s);

/* Lua compatible number to string conversion ("%.14g", integers without .0) */
const char *num_str(double v);
/* Lua's string.format("%d", x) semantics on doubles */
const char *num_int_str(double v);

/* string builder */
typedef struct { char *buf; int len, cap; } StrBuf;
void sb_init(StrBuf *sb);
void sb_add(StrBuf *sb, const char *s);
void sb_addn(StrBuf *sb, const char *s, int n);
void sb_addc(StrBuf *sb, char c);
void sb_addf(StrBuf *sb, const char *fmt, ...);
char *sb_take(StrBuf *sb);   /* returns heap string, resets builder */
void sb_free(StrBuf *sb);

/* ------------------------------------------------------------------ */
/* String keyed hash map (keys are copied & owned by the map)          */
/* ------------------------------------------------------------------ */
typedef struct {
    char **keys;
    void **vals;
    int cap, count;
} StrMap;

void strmap_init(StrMap *m);
void strmap_free(StrMap *m);
void strmap_set(StrMap *m, const char *key, void *val);
void *strmap_get(const StrMap *m, const char *key);
bool strmap_has(const StrMap *m, const char *key);
bool strmap_del(StrMap *m, const char *key);
/* iteration: for (int i = 0; i < m.cap; i++) if (m.keys[i] && m.keys[i] != STRMAP_TOMBSTONE) */
extern char STRMAP_TOMBSTONE_[1];
#define STRMAP_TOMBSTONE (STRMAP_TOMBSTONE_)
#define STRMAP_LIVE(m, i) ((m)->keys[i] && (m)->keys[i] != STRMAP_TOMBSTONE)

/* string -> double map, used where Lua code keeps numeric tallies by key */
typedef struct {
    char **keys;
    double *vals;
    int cap, count;
} NumMap;
void nummap_init(NumMap *m);
void nummap_free(NumMap *m);
void nummap_set(NumMap *m, const char *key, double v);
double nummap_get(const NumMap *m, const char *key, double fallback);
bool nummap_has(const NumMap *m, const char *key);
void nummap_clear(NumMap *m);
void nummap_del(NumMap *m, const char *key);
#define NUMMAP_LIVE(m, i) ((m)->keys[i] && (m)->keys[i] != STRMAP_TOMBSTONE)

/* string set */
typedef NumMap StrSet;
#define strset_init nummap_init
#define strset_free nummap_free
#define strset_clear nummap_clear
static inline void strset_add(StrSet *s, const char *k) { nummap_set(s, k, 1); }
static inline bool strset_has(const StrSet *s, const char *k) { return nummap_has(s, k); }
static inline void strset_del(StrSet *s, const char *k) { nummap_del(s, k); }

/* ------------------------------------------------------------------ */
/* Math helpers mirroring Lua behaviour                                */
/* ------------------------------------------------------------------ */
static inline double lua_mod(double a, double b) { return a - floor(a / b) * b; }
static inline double sign(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }

/* ------------------------------------------------------------------ */
/* LuaJIT compatible PRNG (math.random / math.randomseed)              */
/* ------------------------------------------------------------------ */
typedef struct { uint64_t gen[4]; } LJRandom;
void lj_randomseed(LJRandom *r, double seed);
double lj_random(LJRandom *r);                 /* [0,1) */
double lj_random_int(LJRandom *r, double lo, double hi); /* math.random(lo, hi) */
extern LJRandom g_math_random;                 /* the global math.random state */
double math_random(void);
int math_random_range(int lo, int hi);
void math_randomseed(double seed);

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */
char *file_read_all(const char *path, int *out_len);
bool file_write_all(const char *path, const char *data, int len);
bool file_exists(const char *path);
bool dir_make(const char *path);
bool file_remove(const char *path);
const char *save_dir(void);                    /* directory for settings/profiles */
const char *res_save_path(const char *rel);    /* save_dir()/rel (temp) */

/* table.sort exactly as Lua 5.1 / LuaJIT 2.0 implement it (unstable
 * quicksort): the resulting order of equal elements matches the original,
 * which matters wherever positions feed into the RNG. lt(a, b) = a < b. */
typedef bool (*LuaLess)(const void *a, const void *b, void *ctx);
void lua_table_sort(void **arr, int n, LuaLess lt, void *ctx);
const char *res_path(const char *rel);         /* resolve a resource-relative path (temp) */
void set_resource_root(const char *root);

#endif
