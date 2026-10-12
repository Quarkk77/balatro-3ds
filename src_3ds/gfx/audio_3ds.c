/* audio_3ds.c - the love.audio subset (gfx.h audio_*) on the 3DS DSP (ndsp),
 * decoding the game's .ogg files with stb_vorbis.
 *
 * Short sounds are decoded once and cached in linear memory (with an LRU
 * budget). Music and long sounds are streamed. The game keeps all five
 * music tracks playing at once and crossfades their volumes; a stream that
 * is silent releases its DSP channel and stops decoding, but keeps a clock
 * so it can seek back into sync when it becomes audible. ndsp needs the DSP
 * firmware dump (sdmc:/3ds/dspfirm.cdc); without it the game runs silent. */
#include "gfx.h"
#include <3ds.h>

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define NCHANNELS 24
#define STREAM_BUFS 4
#define STREAM_FRAMES 8192
#define SFX_CACHE_BUDGET (6 * 1024 * 1024)
#define SFX_MAX_CACHED_BYTES (768 * 1024)
#define SILENT_VOL 0.002f

typedef struct {
    char *path;
    s16 *pcm;            /* linear memory, interleaved */
    int frames, channels, rate;
    size_t bytes;
    u64 last_used;
    int refs;
} SfxData;

struct GfxSource {
    bool stream;
    int chn;             /* -1 = no DSP channel */
    float vol, pitch;
    bool playing;
    /* sfx */
    SfxData *sfx;
    ndspWaveBuf wb;
    /* stream */
    char *path;
    stb_vorbis *vf;
    int rate, channels;
    unsigned total_frames;
    double clock;        /* playback position in frames (also while silent) */
    bool decoding, eof;
    ndspWaveBuf swb[STREAM_BUFS];
    s16 *sbuf[STREAM_BUFS];
    float silent_for;
    struct GfxSource *next;
};

static bool audio_ok;
static bool chn_used[NCHANNELS];
static SfxData **cache;
static int ncache, capcache;
static size_t cache_bytes;
static u64 use_counter;
static GfxSource *sources;   /* every live source */
static double last_update;

void audio_init(void) {
    if (R_FAILED(ndspInit())) {
        fprintf(stderr, "audio: ndspInit failed (is sdmc:/3ds/dspfirm.cdc present?); running without sound\n");
        audio_ok = false;
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    audio_ok = true;
    last_update = gfx_time();
}

void audio_shutdown(void) {
    if (!audio_ok) return;
    for (GfxSource *s = sources; s; s = s->next) audio_stop(s);
    ndspExit();
    audio_ok = false;
}

static int chn_alloc(void) {
    for (int i = 0; i < NCHANNELS; i++) if (!chn_used[i]) { chn_used[i] = true; return i; }
    return -1;
}
static void chn_free(int c) {
    if (c < 0) return;
    ndspChnWaveBufClear(c);
    chn_used[c] = false;
}
static void chn_setup(GfxSource *s) {
    int c = s->chn;
    ndspChnReset(c);
    ndspChnSetInterp(c, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(c, s->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(c, (float)s->rate * s->pitch);
    float mix[12] = { 0 };
    mix[0] = mix[1] = s->vol;
    ndspChnSetMix(c, mix);
}

/* ---------------- cached sound effects ---------------- */
static void cache_evict(size_t need) {
    while (cache_bytes + need > SFX_CACHE_BUDGET) {
        int victim = -1;
        for (int i = 0; i < ncache; i++)
            if (cache[i]->pcm && cache[i]->refs == 0 && (victim < 0 || cache[i]->last_used < cache[victim]->last_used)) victim = i;
        if (victim < 0) return;
        SfxData *d = cache[victim];
        linearFree(d->pcm);
        d->pcm = NULL;
        cache_bytes -= d->bytes;
        d->bytes = 0;
    }
}

static SfxData *sfx_get(const char *path) {
    SfxData *d = NULL;
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i]->path, path)) { d = cache[i]; break; }
    if (!d) {
        if (ncache == capcache) { capcache = capcache ? capcache * 2 : 64; cache = xrealloc(cache, (size_t)capcache * sizeof *cache); }
        d = NEW(SfxData);
        d->path = xstrdup(path);
        cache[ncache++] = d;
    }
    d->last_used = ++use_counter;
    if (d->pcm) return d;
    short *out = NULL;
    int ch = 0, rate = 0;
    int frames = stb_vorbis_decode_filename(path, &ch, &rate, &out);
    if (frames <= 0 || !out) { free(out); return NULL; }
    size_t bytes = (size_t)frames * ch * sizeof(s16);
    cache_evict(bytes);
    d->pcm = linearAlloc(bytes);
    if (!d->pcm) { free(out); return NULL; }
    memcpy(d->pcm, out, bytes);
    free(out);
    DSP_FlushDataCache(d->pcm, bytes);
    d->frames = frames; d->channels = ch; d->rate = rate; d->bytes = bytes;
    cache_bytes += bytes;
    return d;
}

/* ---------------- streams ---------------- */
static bool stream_open(GfxSource *s) {
    int err = 0;
    s->vf = stb_vorbis_open_filename(s->path, &err, NULL);
    if (!s->vf) return false;
    stb_vorbis_info info = stb_vorbis_get_info(s->vf);
    s->rate = (int)info.sample_rate;
    s->channels = info.channels > 2 ? 2 : info.channels;
    s->total_frames = stb_vorbis_stream_length_in_samples(s->vf);
    return true;
}

static void stream_fill(GfxSource *s, int i) {
    ndspWaveBuf *wb = &s->swb[i];
    int got = 0;
    if (!s->eof) got = stb_vorbis_get_samples_short_interleaved(s->vf, s->channels, s->sbuf[i], STREAM_FRAMES * s->channels);
    if (got <= 0) { s->eof = true; return; }
    memset(wb, 0, sizeof *wb);
    wb->data_vaddr = s->sbuf[i];
    wb->nsamples = (u32)got;
    DSP_FlushDataCache(s->sbuf[i], (u32)(got * s->channels * sizeof(s16)));
    ndspChnWaveBufAdd(s->chn, wb);
}

static void stream_start_decoding(GfxSource *s) {
    if (s->decoding || !s->vf) return;
    s->chn = chn_alloc();
    if (s->chn < 0) return;
    if (!s->sbuf[0])
        for (int i = 0; i < STREAM_BUFS; i++) s->sbuf[i] = linearAlloc((size_t)STREAM_FRAMES * 2 * sizeof(s16));
    if (!s->sbuf[STREAM_BUFS - 1]) { chn_free(s->chn); s->chn = -1; return; }
    unsigned pos = (unsigned)s->clock;
    if (pos >= s->total_frames) { chn_free(s->chn); s->chn = -1; return; }
    stb_vorbis_seek(s->vf, pos);
    s->eof = false;
    chn_setup(s);
    for (int i = 0; i < STREAM_BUFS; i++) stream_fill(s, i);
    s->decoding = true;
}

static void stream_stop_decoding(GfxSource *s) {
    if (!s->decoding) return;
    chn_free(s->chn);
    s->chn = -1;
    s->decoding = false;
}

static bool is_long_sound(const char *path) {
    stb_vorbis *v = stb_vorbis_open_filename(path, NULL, NULL);
    if (!v) return false;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    unsigned frames = stb_vorbis_stream_length_in_samples(v);
    stb_vorbis_close(v);
    return (size_t)frames * info.channels * sizeof(s16) > SFX_MAX_CACHED_BYTES;
}

/* ---------------- public API ---------------- */
GfxSource *audio_new_source(const char *path, bool stream) {
    if (!audio_ok) return NULL;
    GfxSource *s = NEW(GfxSource);
    s->chn = -1;
    s->vol = 1; s->pitch = 1;
    /* remember which effects were too long to cache */
    static StrMap long_sounds;
    static bool lsinit;
    if (!lsinit) { strmap_init(&long_sounds); lsinit = true; }
    if (!stream) {
        void *known = strmap_get(&long_sounds, path);
        if (!known) {
            known = is_long_sound(path) ? (void *)2 : (void *)1;
            strmap_set(&long_sounds, path, known);
        }
        if (known == (void *)2) stream = true;
    }
    s->stream = stream;
    if (stream) {
        s->path = xstrdup(path);
        if (!stream_open(s)) { free(s->path); free(s); return NULL; }
    } else {
        s->sfx = sfx_get(path);
        if (!s->sfx) { free(s); return NULL; }
        s->sfx->refs++;
        s->rate = s->sfx->rate;
        s->channels = s->sfx->channels;
    }
    s->next = sources;
    sources = s;
    return s;
}

void audio_play(GfxSource *s) {
    if (!s) return;
    s->playing = true;
    if (s->stream) {
        s->clock = 0;
        s->silent_for = 0;
        if (s->vol > SILENT_VOL) stream_start_decoding(s);
        return;
    }
    if (s->chn < 0) s->chn = chn_alloc();
    if (s->chn < 0) { s->playing = false; return; }
    if (!s->sfx->pcm) {   /* evicted while idle */
        SfxData *d = sfx_get(s->sfx->path);
        if (!d) { s->playing = false; return; }
    }
    chn_setup(s);
    memset(&s->wb, 0, sizeof s->wb);
    s->wb.data_vaddr = s->sfx->pcm;
    s->wb.nsamples = (u32)s->sfx->frames;
    ndspChnWaveBufAdd(s->chn, &s->wb);
}

void audio_stop(GfxSource *s) {
    if (!s) return;
    s->playing = false;
    if (s->stream) stream_stop_decoding(s);
    else if (s->chn >= 0) { chn_free(s->chn); s->chn = -1; }
}

bool audio_is_playing(GfxSource *s) {
    if (!s || !s->playing) return false;
    if (s->stream) {
        if (s->clock >= s->total_frames) return false;
        if (s->decoding && s->eof) {
            for (int i = 0; i < STREAM_BUFS; i++)
                if (s->swb[i].status == NDSP_WBUF_QUEUED || s->swb[i].status == NDSP_WBUF_PLAYING) return true;
            return false;
        }
        return true;
    }
    return s->chn >= 0 && s->wb.status != NDSP_WBUF_DONE;
}

void audio_set_volume(GfxSource *s, float v) {
    if (!s) return;
    if (v < 0) v = 0;
    if (v > 2) v = 2;
    if (fabsf(v - s->vol) < 1e-4f) return;
    s->vol = v;
    if (s->chn >= 0) {
        float mix[12] = { 0 };
        mix[0] = mix[1] = v;
        ndspChnSetMix(s->chn, mix);
    }
}

void audio_set_pitch(GfxSource *s, float p) {
    if (!s) return;
    if (p <= 0.001f) p = 0.001f;
    if (fabsf(p - s->pitch) < 1e-4f) return;
    s->pitch = p;
    if (s->chn >= 0) ndspChnSetRate(s->chn, (float)s->rate * p);
}

void audio_release(GfxSource *s) {
    if (!s) return;
    audio_stop(s);
    GfxSource **pp = &sources;
    while (*pp) { if (*pp == s) { *pp = s->next; break; } pp = &(*pp)->next; }
    if (s->stream) {
        if (s->vf) stb_vorbis_close(s->vf);
        for (int i = 0; i < STREAM_BUFS; i++) if (s->sbuf[i]) linearFree(s->sbuf[i]);
        free(s->path);
    } else if (s->sfx) {
        s->sfx->refs--;
    }
    free(s);
}

void audio_update(void) {
    if (!audio_ok) return;
    double now = gfx_time();
    double dt = now - last_update;
    last_update = now;
    if (dt > 0.25) dt = 0.25;
    for (GfxSource *s = sources; s; s = s->next) {
        if (!s->stream) {
            /* free the channel of a finished effect early */
            if (s->chn >= 0 && s->wb.status == NDSP_WBUF_DONE) { chn_free(s->chn); s->chn = -1; s->playing = false; }
            continue;
        }
        if (!s->playing) continue;
        s->clock += dt * s->rate * s->pitch;
        if (s->vol <= SILENT_VOL) {
            s->silent_for += (float)dt;
            if (s->decoding && s->silent_for > 0.5f) stream_stop_decoding(s);
            continue;
        }
        s->silent_for = 0;
        if (!s->decoding) { stream_start_decoding(s); continue; }
        for (int i = 0; i < STREAM_BUFS; i++)
            if (s->swb[i].status == NDSP_WBUF_DONE && !s->eof) stream_fill(s, i);
    }
}

/* ------------------------------------------------------------------ */
/* Deflate (LOVE's love.data.compress 'deflate' = raw deflate), zlib   */
/* ------------------------------------------------------------------ */
#include <zlib.h>
#include "../core/dval.h"
char *deflate_compress(const char *data, int len, int *out_len) {
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (deflateInit2(&zs, 8, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return NULL;
    uLong cap = deflateBound(&zs, (uLong)len);
    char *out = xmalloc(cap);
    zs.next_in = (Bytef *)data; zs.avail_in = (uInt)len;
    zs.next_out = (Bytef *)out; zs.avail_out = (uInt)cap;
    int r = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (r != Z_STREAM_END) { free(out); return NULL; }
    *out_len = (int)zs.total_out;
    return out;
}
char *deflate_decompress(const char *data, int len, int *out_len) {
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, -15) != Z_OK) return NULL;
    size_t cap = (size_t)len * 8 + 1024, n = 0;
    char *out = xmalloc(cap + 1);
    zs.next_in = (Bytef *)data; zs.avail_in = (uInt)len;
    for (;;) {
        zs.next_out = (Bytef *)out + n; zs.avail_out = (uInt)(cap - n);
        int r = inflate(&zs, Z_NO_FLUSH);
        n = cap - zs.avail_out;
        if (r == Z_STREAM_END) break;
        if (r != Z_OK && r != Z_BUF_ERROR) { inflateEnd(&zs); free(out); return NULL; }
        if (zs.avail_out == 0) { cap *= 2; out = xrealloc(out, cap + 1); }
        else if (r == Z_BUF_ERROR) { inflateEnd(&zs); free(out); return NULL; }
    }
    inflateEnd(&zs);
    out[n] = 0;
    *out_len = (int)n;
    return out;
}
