/* heap.c - tracked allocations and the collector for removed nodes.
 *
 * The Lua original relied on the garbage collector: a removed object stayed
 * valid for as long as anything (an event, another object, a global) still
 * pointed at it, and code freely checks `obj.REMOVED` on such leftovers.
 * The C port keeps removed nodes in a graveyard instead of freeing them.
 *
 * To reclaim them safely, every block allocated through xmalloc/xcalloc/
 * xrealloc is registered here. heap_collect() then does a conservative
 * mark from the program's globals (.data/.bss) and the main loop's stack:
 * any word that points into a registered block marks it, and marked blocks
 * are scanned in turn. A candidate (a graveyard node) that ends up unmarked
 * is unreachable, so it is freed together with every unmarked block only it
 * reaches (its children arrays, configs, strings, card data ...). Blocks
 * that are merely unreachable for other reasons are left alone. */
#define CORE_ALLOC_INTERNAL
#include "core.h"

#if defined(__SANITIZE_ADDRESS__)
#define NO_ASAN __attribute__((no_sanitize_address))
#else
#define NO_ASAN
#endif

/* ---------------- registry: open addressing, block start -> size ---------------- */
#include "dval.h"
/* n's top two bits hold the block kind (how to scan it) */
typedef struct { uintptr_t p; size_t n; } HEnt;
#define KIND_SHIFT (sizeof(size_t) * 8 - 2)
#define KIND_MASK ((size_t)3 << KIND_SHIFT)
#define H_EMPTY ((uintptr_t)0)
#define H_TOMB ((uintptr_t)1)
static HEnt *htab;
static size_t hcap, hused, hlive;

static inline size_t hslot(uintptr_t p) { return (size_t)(((uint64_t)(p >> 3) * 0x9E3779B97F4A7C15ull) >> 20) & (hcap - 1); }

static void hgrow(void) {
    HEnt *old = htab;
    size_t oc = hcap;
    hcap = hcap ? hcap * 2 : 1 << 14;
    while (hlive * 2 >= hcap) hcap *= 2;
    htab = calloc(hcap, sizeof(HEnt));
    if (!htab) { fprintf(stderr, "out of memory (heap registry)\n"); abort(); }
    hused = hlive;
    for (size_t i = 0; i < oc; i++)
        if (old[i].p > H_TOMB) {
            size_t j = hslot(old[i].p);
            while (htab[j].p != H_EMPTY) j = (j + 1) & (hcap - 1);
            htab[j] = old[i];
        }
    free(old);
}

void heap_track(void *ptr, size_t n) {
    if (!ptr) return;
    if ((hused + 1) * 4 >= hcap * 3) hgrow();
    uintptr_t p = (uintptr_t)ptr;
    size_t j = hslot(p), tomb = (size_t)-1;
    while (htab[j].p != H_EMPTY) {
        if (htab[j].p == p) { htab[j].n = n; return; }
        if (htab[j].p == H_TOMB && tomb == (size_t)-1) tomb = j;
        j = (j + 1) & (hcap - 1);
    }
    if (tomb != (size_t)-1) j = tomb;
    else hused++;
    htab[j].p = p; htab[j].n = n;
    hlive++;
}

void heap_track_atomic(void *ptr, size_t n) { heap_track(ptr, n | ((size_t)HEAP_ATOMIC << KIND_SHIFT)); }

static HEnt *hfind(uintptr_t p) {
    if (!hcap) return NULL;
    size_t j = hslot(p);
    while (htab[j].p != H_EMPTY) {
        if (htab[j].p == p) return &htab[j];
        j = (j + 1) & (hcap - 1);
    }
    return NULL;
}
size_t heap_block_size(void *ptr) {
    HEnt *e = hfind((uintptr_t)ptr);
    return e ? (e->n & ~KIND_MASK) : 0;
}
void heap_set_kind(void *ptr, int kind) {
    HEnt *e = hfind((uintptr_t)ptr);
    if (e) e->n = (e->n & ~KIND_MASK) | ((size_t)kind << KIND_SHIFT);
}

bool heap_untrack(void *ptr) {
    if (!ptr || !hcap) return false;
    uintptr_t p = (uintptr_t)ptr;
    size_t j = hslot(p);
    while (htab[j].p != H_EMPTY) {
        if (htab[j].p == p) { htab[j].p = H_TOMB; hlive--; return true; }
        j = (j + 1) & (hcap - 1);
    }
    return false;
}

size_t heap_tracked_blocks(void) { return hlive; }

/* ---------------- collector ---------------- */
int heap_explain = -1;   /* debug: print why N removed nodes survive */
static uintptr_t stack_top;
void heap_set_stack_top(void *p) { stack_top = (uintptr_t)p; }

static void root_range(uintptr_t *lo, uintptr_t *hi) {
#if defined(__3DS__)
    extern char __exidx_end[], __bss_end__[];
    *lo = (uintptr_t)__exidx_end; *hi = (uintptr_t)__bss_end__;
#else
    extern char __data_start[], _end[];
    *lo = (uintptr_t)__data_start; *hi = (uintptr_t)_end;
#endif
}

typedef struct { uintptr_t p, e; } Blk;
enum { F_MARK = 1, F_CAND = 2, F_SKIP = 4, F_FREE = 8, F_ATOM = 16, F_DKEYS = 32, F_DVALS = 64 };
/* direct lookup: for each GRAN-byte granule of the heap span, the last block
 * starting at or before it (blocks are sorted and do not overlap) */
#define GRAN_SHIFT 7
#define GRAN_MAX (1u << 20)
static uint32_t *gran;
static size_t ngran;
static Blk *blks;
static uint8_t *flags;
static size_t nblk;
static uintptr_t lo_addr, hi_addr;
static size_t *work;
static long *parent;          /* debug: block that first marked each block (-2 = root) */
static uintptr_t *parent_word; /* debug: address of the referencing word */
static long cur_scan = -2;
static size_t nwork, capwork;

static int blk_cmp(const void *a, const void *b) {
    uintptr_t x = ((const Blk *)a)->p, y = ((const Blk *)b)->p;
    return x < y ? -1 : x > y;
}

static inline long find_blk(uintptr_t w) {
    if (w < lo_addr || w >= hi_addr) return -1;
    if (gran) {
        long i = (long)gran[(w - lo_addr) >> GRAN_SHIFT];
        while (i + 1 < (long)nblk && blks[i + 1].p <= w) i++;
        if (i < 0 || blks[i].p > w) return -1;
        return w < blks[i].e ? i : -1;
    }
    size_t l = 0, h = nblk;
    while (l < h) {          /* first block with start > w */
        size_t m = (l + h) / 2;
        if (blks[m].p <= w) l = m + 1;
        else h = m;
    }
    if (l == 0) return -1;
    l--;
    return w < blks[l].e ? (long)l : -1;
}

static void push_work(size_t i) {
    if (nwork == capwork) {
        capwork = capwork ? capwork * 2 : 4096;
        work = realloc(work, capwork * sizeof *work);
        if (!work) { fprintf(stderr, "out of memory (collector)\n"); abort(); }
    }
    work[nwork++] = i;
}

/* scan [a, b) for pointers into blocks without `stop` flags; add `set` to them */
NO_ASAN static void scan_range(uintptr_t a, uintptr_t b, uint8_t set, uint8_t stop) {
    a = (a + sizeof(uintptr_t) - 1) & ~(uintptr_t)(sizeof(uintptr_t) - 1);
    for (; a + sizeof(uintptr_t) <= b; a += sizeof(uintptr_t)) {
        long i = find_blk(*(const uintptr_t *)a);
        if (i >= 0 && !(flags[i] & stop)) {
            flags[i] |= set;
            if (parent && set == F_MARK) { parent[i] = cur_scan; parent_word[i] = a; }
            push_work((size_t)i);
        }
    }
}

/* DVal key/value arrays: only string and table entries hold pointers (the
 * doubles in number entries would otherwise look like heap addresses) */
NO_ASAN static void scan_dval_block(size_t i, uint8_t set, uint8_t stop) {
    if (flags[i] & F_DVALS) {
        for (uintptr_t a = blks[i].p; a + sizeof(DVal) <= blks[i].e; a += sizeof(DVal)) {
            const DVal *v = (const DVal *)a;
            if (v->t == DV_STR || v->t == DV_TABLE) scan_range((uintptr_t)&v->s, (uintptr_t)&v->s + sizeof(void *), set, stop);
        }
    } else {
        for (uintptr_t a = blks[i].p; a + sizeof(DKey) <= blks[i].e; a += sizeof(DKey)) {
            const DKey *k = (const DKey *)a;
            if (k->is_str) scan_range((uintptr_t)&k->s, (uintptr_t)&k->s + sizeof(void *), set, stop);
        }
    }
}

static void drain(uint8_t set, uint8_t stop) {
    while (nwork) {
        size_t i = work[--nwork];
        if (flags[i] & (F_SKIP | F_ATOM)) continue;
        if (flags[i] & (F_DKEYS | F_DVALS)) { cur_scan = (long)i; scan_dval_block(i, set, stop); cur_scan = -2; continue; }
        cur_scan = (long)i;
        scan_range(blks[i].p, blks[i].e, set, stop);
        cur_scan = -2;
    }
}

NO_ASAN int heap_collect(void **cand, int *ncand, const void *skip_block) {
    /* snapshot the registry, sorted by address */
    nblk = 0;
    blks = malloc((hlive ? hlive : 1) * sizeof *blks);
    flags = calloc(hlive ? hlive : 1, 1);
    if (!blks || !flags) { free(blks); free(flags); return 0; }
    for (size_t i = 0; i < hcap; i++)
        if (htab[i].p > H_TOMB) {
            size_t n = htab[i].n & ~KIND_MASK;
            blks[nblk].p = htab[i].p;
            blks[nblk].e = htab[i].p + (n ? n : 1);
            nblk++;
        }
    qsort(blks, nblk, sizeof *blks, blk_cmp);
    for (size_t i = 0; i < nblk; i++) {
        int kind = (int)(hfind(blks[i].p)->n >> KIND_SHIFT);
        if (kind == HEAP_ATOMIC) flags[i] |= F_ATOM;
        else if (kind == HEAP_DKEYS) flags[i] |= F_DKEYS;
        else if (kind == HEAP_DVALS) flags[i] |= F_DVALS;
    }
    lo_addr = nblk ? blks[0].p : 0;
    hi_addr = nblk ? blks[nblk - 1].e : 0;
    for (size_t i = 0; i < nblk; i++) if (blks[i].e > hi_addr) hi_addr = blks[i].e;
    gran = NULL;
    ngran = nblk ? ((hi_addr - lo_addr) >> GRAN_SHIFT) + 1 : 0;
    if (ngran && ngran <= GRAN_MAX && (gran = malloc(ngran * sizeof *gran))) {
        size_t b = 0;
        for (size_t g = 0; g < ngran; g++) {
            uintptr_t a = lo_addr + ((uintptr_t)g << GRAN_SHIFT);
            while (b + 1 < nblk && blks[b + 1].p <= a) b++;
            gran[g] = (uint32_t)b;
        }
    }
    long sk = skip_block ? find_blk((uintptr_t)skip_block) : -1;
    if (sk >= 0) flags[sk] |= F_SKIP;
    for (int c = 0; c < *ncand; c++) {
        long i = find_blk((uintptr_t)cand[c]);
        if (i >= 0) flags[i] |= F_CAND;
    }

    static int explain = -1;
    if (explain < 0) explain = heap_explain;
    if (explain) { parent = malloc(nblk * sizeof *parent); parent_word = malloc(nblk * sizeof *parent_word); }
    /* mark everything reachable from globals and the stack */
    uintptr_t rlo, rhi;
    root_range(&rlo, &rhi);
    static uintptr_t root_lo_dbg; root_lo_dbg = rlo; (void)root_lo_dbg;
    scan_range(rlo, rhi, F_MARK, F_MARK);
    uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
    if (stack_top > sp) scan_range(sp, stack_top, F_MARK, F_MARK);
    drain(F_MARK, F_MARK);

    /* unreachable candidates, and what only they reach, are garbage */
    for (int c = 0; c < *ncand; c++) {
        long i = find_blk((uintptr_t)cand[c]);
        if (i >= 0 && !(flags[i] & (F_MARK | F_FREE))) { flags[i] |= F_FREE; push_work((size_t)i); }
    }
    drain(F_FREE, F_MARK | F_FREE | F_SKIP);

    if (explain) {
        int shown = 0;
        for (int c = 0; c < *ncand && shown < explain; c++) {
            long i = find_blk((uintptr_t)cand[c]);
            if (i < 0 || !(flags[i] & F_MARK)) continue;
            shown++;
            fprintf(stderr, "GCX survivor %p:", (void *)blks[i].p);
            long k = i;
            for (int d = 0; d < 4000 && k >= 0; d++) {
                if (parent[k] >= 0 && parent[parent[k]] >= 0 && parent[parent[parent[k]]] >= 0) { k = parent[k]; continue; }
                long pk = parent[k];
                if (pk < 0) { uintptr_t lo2, hi2; root_range(&lo2, &hi2);
                    if (parent_word[k] >= lo2 && parent_word[k] < hi2) fprintf(stderr, " <- ROOT data+0x%lx", (unsigned long)(parent_word[k] - lo2));
                    else fprintf(stderr, " <- STACK %p", (void *)parent_word[k]);
                    break; }
                fprintf(stderr, " <- [%s%p+%ld sz %ld]", pk >= 0 && (flags[pk] & F_CAND) ? "GRAVE " : "",
                        pk >= 0 ? (void *)blks[pk].p : (void *)parent_word[k],
                        pk >= 0 ? (long)(parent_word[k] - blks[pk].p) : 0L, pk >= 0 ? (long)(blks[pk].e - blks[pk].p) : -1L);
                k = pk;
            }
            fprintf(stderr, "\n");
        }
        free(parent); free(parent_word); parent = NULL; parent_word = NULL;
    }
    int freed = 0, kept = 0;
    for (int c = 0; c < *ncand; c++) {
        long i = find_blk((uintptr_t)cand[c]);
        if (i >= 0 && (flags[i] & F_MARK)) cand[kept++] = cand[c];
    }
    *ncand = kept;
    for (size_t i = 0; i < nblk; i++)
        if (flags[i] & F_FREE) {
            heap_untrack((void *)blks[i].p);
            free((void *)blks[i].p);
            freed++;
        }
    free(blks); free(flags); free(gran);
    blks = NULL; flags = NULL; gran = NULL;
    return freed;
}
