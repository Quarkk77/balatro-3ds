/* dval.h - a tiny dynamic value tree.
 *
 * The original game stores settings, profiles, saved runs and its
 * localisation tables as Lua table literals ("return {...}"), optionally
 * deflate-compressed. DVal is the C side representation of such data, with
 * a parser for the literal syntax and a serializer that matches the game's
 * STR_PACK output. Gameplay code uses typed structs; DVal is only used at
 * the boundaries (disk, localisation) and for the free-form profile data. */
#ifndef DVAL_H
#define DVAL_H
#include "core.h"

typedef enum { DV_NIL = 0, DV_BOOL, DV_NUM, DV_STR, DV_TABLE } DVType;

typedef struct DVal DVal;
typedef struct DTable DTable;

struct DVal {
    DVType t;
    union {
        bool b;
        double n;
        char *s;
        DTable *tab;
    };
};

/* key of a table entry: either a string or an integer index */
typedef struct {
    bool is_str;
    char *s;
    double n;
} DKey;

struct DTable {
    int count, cap;
    DKey *keys;
    DVal *vals;
    StrMap index; /* string key -> (entry index + 1) */
};

/* construction */
DVal dv_nil(void);
DVal dv_bool(bool b);
DVal dv_num(double n);
DVal dv_str(const char *s);          /* copies */
DVal dv_table(void);                 /* new empty table */
DVal *dv_new_table(void);            /* heap allocated table value */
void dv_free(DVal *v);               /* frees contents (not the DVal itself) */
DVal dv_copy(const DVal *v);         /* deep copy */

/* table access */
DVal *dv_get(const DVal *t, const char *key);            /* NULL if missing or not a table */
DVal *dv_geti(const DVal *t, int idx);                   /* 1 based */
DVal *dv_path(const DVal *t, const char *dotted_path);   /* "a.b.c" */
void dv_set(DVal *t, const char *key, DVal v);           /* takes ownership of v; DV_NIL removes */
void dv_seti(DVal *t, int idx, DVal v);
void dv_push(DVal *t, DVal v);                           /* append to array part */
DVal *dv_get_or_create_table(DVal *t, const char *key);  /* ensures a table exists at key */
int dv_len(const DVal *t);                               /* Lua # operator on the array part */
bool dv_is_table(const DVal *v);

/* typed reads with fallback */
double dv_num_or(const DVal *v, double fallback);
const char *dv_str_or(const DVal *v, const char *fallback);
bool dv_truthy(const DVal *v);                           /* Lua truthiness (nil/false -> false) */
double dv_get_num(const DVal *t, const char *key, double fallback);
const char *dv_get_str(const DVal *t, const char *key, const char *fallback);
bool dv_get_bool(const DVal *t, const char *key, bool fallback);
static inline const char *dv_geti_str(const DVal *t, int i, const char *fallback) { return dv_str_or(dv_geti(t, i), fallback); }
static inline double dv_geti_num(const DVal *t, int i, double fallback) { return dv_num_or(dv_geti(t, i), fallback); }

/* convenience setters */
void dv_set_num(DVal *t, const char *key, double n);
void dv_set_str(DVal *t, const char *key, const char *s);
void dv_set_bool(DVal *t, const char *key, bool b);

/* iteration over all entries: for (int i = 0; i < dv_count(t); i++) dv_key(t,i), dv_val(t,i) */
int dv_count(const DVal *t);
const DKey *dv_key(const DVal *t, int i);
DVal *dv_val(const DVal *t, int i);

/* parsing / serialization */
bool dv_parse(const char *src, DVal *out);               /* parses "return {...}" or "{...}" */
char *dv_serialize(const DVal *v);                        /* "return {...}" like STR_PACK */
bool dv_load_file(const char *path, DVal *out);           /* handles raw-deflate compressed files */
bool dv_save_file(const char *path, const DVal *v, bool compress);

/* raw deflate helpers (implemented on top of raylib's sdefl/sinfl) */
char *deflate_compress(const char *data, int len, int *out_len);
char *deflate_decompress(const char *data, int len, int *out_len);

#endif
