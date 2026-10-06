/*
 * pix_sound.c -- Duke Nukem 3D's sound effects and music on the caller's own terminal, for the JPEG XL graphics mode.
 * (The DOOM and Wolfenstein doors' pix_sound.c, for Duke.)
 *
 * The terminal plays sound files it keeps in a cache of its own, one per BBS, so nothing is streamed as raw audio.
 * Everything comes ready made, from the game's data:
 *
 *   Effects  sound/: every VOC in the GRP as an 8-bit WAV at its own rate (tools/make_sfx.py), uploaded once
 *            (checked by md5 on every call). An effect is loaded into its sound slot the first time the game plays it
 *            (most of the 181 never are in a game, and every one loaded costs the terminal memory); playing one is
 *            then a few dozen bytes: stop that channel, set its left/right volume, copy the effect into the channel's
 *            own slot, queue it (looped, for the game's looping sounds, until the game stops it).
 *   Music    music/: each song rendered with TinySoundFont and the TRACE module's soundfont (tools/midirender.c) and
 *            cut into Ogg Vorbis pieces (tools/make_music.py). A song plays as its pieces queued back to back on one
 *            channel, which the terminal joins seamlessly; each piece is uploaded only when it is about to be needed,
 *            or earlier while the link has room to spare, and stays in the cache. Duke's songs loop back to a point
 *            after their opening: the pieces are cut there (index.txt's sixth column is the piece it loops back to),
 *            so the second time round starts exactly where the game's own player does.
 *
 * Levels follow the TRACE module's (module/src/audio_trace.c), so the balance matches: effects at FX_GAIN times what
 * MultiVoc mixes them at (pix_hooks.c works that out, panning and the game's sound volume included); music rendered
 * at its -9 dB, and the game's music volume as its MIDI player applies it (a channel volume, which TinySoundFont
 * takes as a cube).
 *
 * The game's voices are mapped onto the terminal's channels as they start: a free one, or else the one whose sound
 * started longest ago. Terminal resources used: slots 0-199 effects, 200-212 one per effect channel, 250 music;
 * channel 2 music, 3-15 effects.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "door.h"
#include "pix_hooks.h"
#include "pix_sound.h"
#include "trace_door.h"

#define SFX_DIR      "dukedoor/sfx/"
#define MUS_DIR      "dukedoor/mus/"
#define MAX_SFX      200
#define MAX_TRACKS   16
#define MAX_PIECES   128
#define PIECE_MS     5000
#define MUSIC_CH     2
#define SFX_CH0      3
#define SFX_CHANNELS 13
#define SCRATCH_SLOT 200            /* + channel */
#define MUSIC_SLOT   250
#define FX_GAIN      0.35           /* audio_trace.c */

#define QUEUE_AHEAD_MS  8000        /* music queued this far ahead of what's playing */
#define URGENT_MS       3000        /* a piece needed sooner than this is sent even if the picture has to wait */

typedef struct
{
    char     grp[16];               /* its name in the GRP, as the game's sounds[] table has it: PISTOL.VOC */
    char     file[24];              /* pistol.wav */
    int      ms;
    uint8_t *data;
    size_t   size;
    bool     loaded;                /* in its sound slot this game */
} sfx_t;

typedef struct
{
    char     name[16];
    char     hash[65];
    int      pieces, last_ms, loop_ms, loop_piece;
    bool     cut_at_loop;           /* pieces before loop_piece end exactly at the loop start */
    uint8_t *data[MAX_PIECES];
    size_t   size[MAX_PIECES];
    bool     cached[MAX_PIECES];
} track_t;

static sfx_t   g_sfx[MAX_SFX];
static int     g_sfx_count;
static track_t g_tracks[MAX_TRACKS];
static int     g_track_count;
static bool    g_ready;

/* The music as it stands */
static struct
{
    int  track;                     /* -1 none */
    bool looping, paused;
    int  next;                      /* the next piece to queue, -1 when the song has ended */
    long queued_until;              /* when what's queued runs out */
    int  playing_piece;             /* for resuming after a pause: the piece that was playing */
    long piece_started[MAX_PIECES];
    int  volume;                    /* 0-255 */
} M = { .track = -1, .volume = 255 };

/* The terminal's effect channels: which of the game's voices each is playing */
static struct
{
    int   voice;                    /* 0: free */
    long  started, ends;            /* ends: LONG_MAX while looping */
    float left, right;
} g_ch[SFX_CHANNELS];

/* ---- the files ---- */

/* The next line of text (without its newline) into line; false at the end */
static bool next_line(const char **p, const char *end, char *line, size_t size)
{
    size_t n = 0;

    if (*p >= end)
        return false;
    while (*p < end && **p != '\n')
    {
        if (n + 1 < size)
            line[n++] = **p;
        (*p)++;
    }
    if (*p < end)
        (*p)++;
    line[n] = '\0';
    return true;
}

static void load_sfx(void)
{
    tdoor_blob_t index;
    const char *p, *end;
    char line[160];

    if (!tdoor_load_blob("sound/index.txt", &index, 64 * 1024))
        return;
    p = (const char *)index.data;
    end = p + index.size;
    while (next_line(&p, end, line, sizeof(line)) && g_sfx_count < MAX_SFX)
    {
        sfx_t *s = &g_sfx[g_sfx_count];
        char path[64];
        tdoor_blob_t wav;
        int rate;

        if (sscanf(line, "sfx %15s %23s %d %d", s->grp, s->file, &rate, &s->ms) != 4)
            continue;
        snprintf(path, sizeof(path), "sound/%s", s->file);
        if (!tdoor_load_blob(path, &wav, 4 * 1024 * 1024))
            continue;
        s->data = wav.data;
        s->size = wav.size;
        g_sfx_count++;
    }
}

static void load_music(void)
{
    tdoor_blob_t index;
    const char *p, *end;
    char line[256];

    if (!tdoor_load_blob("music/index.txt", &index, 64 * 1024))
        return;
    p = (const char *)index.data;
    end = p + index.size;
    while (next_line(&p, end, line, sizeof(line)) && g_track_count < MAX_TRACKS)
    {
        track_t *t = &g_tracks[g_track_count];
        bool all = true;
        int fields = sscanf(line, "%15s %64s %d %d %d %d", t->name, t->hash, &t->pieces, &t->last_ms, &t->loop_ms,
                            &t->loop_piece);

        if (fields < 4 || t->pieces <= 0 || t->pieces > MAX_PIECES)
            continue;
        if (fields < 5)
            t->loop_ms = 0;
        /* Six fields: cut at the loop start (make_music.py). Five: plain 5 s pieces, so the loop goes back to the
         * start of the piece the loop start is in */
        t->cut_at_loop = fields >= 6;
        if (!t->cut_at_loop)
            t->loop_piece = t->loop_ms / PIECE_MS;
        if (t->loop_piece < 0 || t->loop_piece >= t->pieces)
            t->loop_piece = 0;
        for (int k = 0; k < t->pieces && all; k++)
        {
            char path[64];
            tdoor_blob_t piece;
            snprintf(path, sizeof(path), "music/%s_%02d.ogg", t->name, k);
            if (tdoor_load_blob(path, &piece, 1024 * 1024))
            {
                t->data[k] = piece.data;
                t->size[k] = piece.size;
            }
            else
            {
                all = false;
            }
        }
        if (all)
            g_track_count++;
    }
}

/* ---- what the terminal already has ---- */

/* Asks for the cache listing of one folder and reads the reply: lines of name TAB md5, between APC markers.
 * Returns the reply text (static), or NULL if none came. */
static const char *list_cache(const char *dir)
{
    static char reply[65536];
    size_t n = 0;
    int c;

    door_write(APC_PREFIX "C;L;");
    door_write(dir);
    door_write("*" APC_END);

    while (n < sizeof(reply) - 1)
    {
        c = door_read_char_timeout(n == 0 ? 3000 : 1000);
        if (c < 0)
            return NULL;
        reply[n++] = (char)c;
        if (n >= 2 && reply[n - 2] == '\033' && reply[n - 1] == '\\')
            break;
    }
    reply[n] = '\0';
    return strstr(reply, "C;L") != NULL ? reply : NULL;
}

/* Does the listing have this file with this content? */
static bool listed(const char *listing, const char *name, const void *data, size_t size)
{
    char want[128], md5[33];
    md5_hex(data, size, md5);
    snprintf(want, sizeof(want), "\n%s\t%s", name, md5);
    return listing != NULL && strstr(listing, want) != NULL;
}

/*
 * Waits for one answer to a cursor-position request (ESC [ row ; col R), which says everything sent before it has
 * reached the terminal. The upload below keeps a few of these outstanding, so its progress is what has arrived, not
 * what has been handed to the network, and the game doesn't start with the tail of it still queued in front.
 */
static bool wait_arrived(void)
{
    int c;
    while ((c = door_read_char_timeout(30000)) >= 0)
        if (c == 'R')
            return true;
    return false;
}

#define UPLOAD_AHEAD 65536          /* bytes sent before waiting for them to arrive */

bool pix_sound_prepare(void (*progress)(int percent))
{
    const char *listing;
    size_t total = 0, sent = 0, arrived = 0;
    size_t queued[MAX_SFX];
    bool need[MAX_SFX];
    int ahead = 0, first = 0;

    load_sfx();
    load_music();
    if (g_sfx_count == 0 && g_track_count == 0)
        return false;

    /* Only what the terminal doesn't have yet, or has in another version */
    listing = list_cache(SFX_DIR);
    for (int i = 0; i < g_sfx_count; i++)
    {
        need[i] = !listed(listing, g_sfx[i].file, g_sfx[i].data, g_sfx[i].size);
        if (need[i])
            total += g_sfx[i].size;
    }
    for (int i = 0; i < g_sfx_count; i++)
    {
        outbuf_t o = { 0 };
        char head[64];
        if (!need[i])
            continue;
        snprintf(head, sizeof(head), "C;S;" SFX_DIR "%s;", g_sfx[i].file);
        apc_blob(&o, head, g_sfx[i].data, g_sfx[i].size);
        out_str(&o, "\033[6n");
        door_write_raw(o.data, o.len);
        free(o.data);
        queued[ahead++] = g_sfx[i].size;
        sent += g_sfx[i].size;
        /* keep no more than UPLOAD_AHEAD on its way */
        while (sent - arrived > UPLOAD_AHEAD && first < ahead && wait_arrived())
        {
            arrived += queued[first++];
            if (progress != NULL)
                progress((int)(arrived * 100 / total));
        }
    }
    while (first < ahead && wait_arrived())
    {
        arrived += queued[first++];
        if (progress != NULL)
            progress((int)(arrived * 100 / total));
    }

    listing = list_cache(MUS_DIR);
    for (int t = 0; t < g_track_count; t++)
        for (int k = 0; k < g_tracks[t].pieces; k++)
        {
            char file[64];
            snprintf(file, sizeof(file), "%.15s_%02d.ogg", g_tracks[t].name, k);
            g_tracks[t].cached[k] = listed(listing, file, g_tracks[t].data[k], g_tracks[t].size[k]);
        }

    g_ready = true;
    return true;
}

/* ---- playing ---- */

static int db(double gain)
{
    return gain <= 0.001 ? -60 : (int)lround(20.0 * log10(gain));
}

/* The game's music volume (0-255) as a channel volume: its MIDI player sets each channel's volume controller to that
 * share, and TinySoundFont takes the controller as a cube (the TRACE module's level) */
static int music_db(int volume)
{
    return volume <= 0 ? -60 : (int)lround(60.0 * log10(volume / 255.0));
}

void pix_sound_start(outbuf_t *o)
{
    if (!g_ready)
        return;
    M.volume = pix_hooks_music_volume();
    apc_cmd(o, "A;Volume;C=%d;V=%ddB", MUSIC_CH, music_db(M.volume));
    for (int c = 0; c < SFX_CHANNELS; c++)
        g_ch[c].voice = 0;
}

static int find_sfx(const char *grp)
{
    for (int i = 0; i < g_sfx_count; i++)
        if (strcasecmp(g_sfx[i].grp, grp) == 0)
            return i;
    return -1;
}

static int find_voice(int voice)
{
    for (int c = 0; c < SFX_CHANNELS; c++)
        if (g_ch[c].voice == voice)
            return c;
    return -1;
}

/* A channel for a new sound: one that is free or has finished, or else the one that started longest ago (a sound
 * that has just started is never cut off for another, and a loop only when every channel is looping) */
static int pick_channel(long now)
{
    int best = -1;

    for (int c = 0; c < SFX_CHANNELS; c++)
        if (g_ch[c].voice == 0 || now >= g_ch[c].ends)
            return c;
    for (int c = 0; c < SFX_CHANNELS; c++)
        if (g_ch[c].ends != LONG_MAX && (best < 0 || g_ch[c].started < g_ch[best].started))
            best = c;
    if (best < 0)
        for (int c = 0; c < SFX_CHANNELS; c++)
            if (best < 0 || g_ch[c].started < g_ch[best].started)
                best = c;
    return best;
}

static void sfx_volume(outbuf_t *o, int c)
{
    apc_cmd(o, "A;Volume;C=%d;VL=%ddB;VR=%ddB", SFX_CH0 + c, db(FX_GAIN * g_ch[c].left), db(FX_GAIN * g_ch[c].right));
}

/* At once, with no fade: a fade (O=) only fades the last piece queued, and the several seconds queued in front of it
 * would play on (after the game ended, or over the next level's song) */
static void music_stop(outbuf_t *o)
{
    apc_cmd(o, "A;Flush;C=%d", MUSIC_CH);
}

static void handle(outbuf_t *o, const pix_event_t *ev, long now)
{
    int c;

    switch (ev->kind)
    {
    case PIX_SFX_START:
    {
        int s = find_sfx(ev->name);
        if (s < 0)
            return;
        c = find_voice(ev->voice);
        if (c < 0)
            c = pick_channel(now);
        if (!g_sfx[s].loaded)
        {
            apc_cmd(o, "A;Load;S=%d;" SFX_DIR "%s", s, g_sfx[s].file);
            g_sfx[s].loaded = true;
        }
        g_ch[c].voice = ev->voice;
        g_ch[c].started = now;
        g_ch[c].ends = ev->loop ? LONG_MAX : now + g_sfx[s].ms + 50;
        g_ch[c].left = ev->left;
        g_ch[c].right = ev->right;
        apc_cmd(o, "A;Flush;C=%d", SFX_CH0 + c);
        sfx_volume(o, c);
        apc_cmd(o, "A;Copy;S=%d;D=%d", s, SCRATCH_SLOT + c);
        apc_cmd(o, "A;Queue;C=%d;S=%d%s", SFX_CH0 + c, SCRATCH_SLOT + c, ev->loop ? ";L" : "");
        return;
    }
    case PIX_SFX_PAN:
        /* The game re-pans every sound it's tracking each frame, mostly to where it already is */
        c = find_voice(ev->voice);
        if (c < 0 || (fabsf(g_ch[c].left - ev->left) < 0.02f && fabsf(g_ch[c].right - ev->right) < 0.02f))
            return;
        g_ch[c].left = ev->left;
        g_ch[c].right = ev->right;
        sfx_volume(o, c);
        return;
    case PIX_SFX_STOP:
        for (c = 0; c < SFX_CHANNELS; c++)
            if (g_ch[c].voice != 0 && (ev->voice < 0 || g_ch[c].voice == ev->voice))
            {
                if (now < g_ch[c].ends)
                    apc_cmd(o, "A;Flush;C=%d;O=10", SFX_CH0 + c);
                g_ch[c].voice = 0;
            }
        return;

    case PIX_MUS_PLAY:
        music_stop(o);
        M.track = -1;
        for (int t = 0; t < g_track_count; t++)
            if (strcmp(g_tracks[t].hash, ev->hash) == 0)
                M.track = t;
        M.looping = ev->loop;
        M.paused = false;
        M.next = 0;
        M.playing_piece = 0;
        memset(M.piece_started, 0, sizeof(M.piece_started));
        M.queued_until = now;
        return;
    case PIX_MUS_STOP:
        music_stop(o);
        M.track = -1;
        return;
    case PIX_MUS_PAUSE:
        if (M.track < 0 || M.paused)
            return;
        /* Remember which piece was playing (the one queued to start last before now), and start again from its
         * beginning on resume */
        for (int k = 0; k < g_tracks[M.track].pieces; k++)
            if (M.piece_started[k] != 0 && M.piece_started[k] <= now &&
                M.piece_started[k] >= M.piece_started[M.playing_piece])
                M.playing_piece = k;
        music_stop(o);
        M.paused = true;
        return;
    case PIX_MUS_RESUME:
        if (M.track < 0 || !M.paused)
            return;
        M.paused = false;
        M.next = M.playing_piece;
        M.queued_until = now;
        return;
    case PIX_MUS_VOLUME:
        M.volume = ev->volume;
        apc_cmd(o, "A;Volume;C=%d;V=%ddB", MUSIC_CH, music_db(M.volume));
        return;
    }
}

static int piece_ms(const track_t *t, int k)
{
    if (k == t->pieces - 1)
        return t->last_ms;
    if (t->cut_at_loop && k == t->loop_piece - 1)
        return t->loop_ms - (t->loop_piece - 1) * PIECE_MS;    /* the opening's last piece ends at the loop start */
    return PIECE_MS;
}

void pix_sound_update(outbuf_t *o, long now)
{
    pix_event_t ev;

    if (!g_ready)
    {
        while (pix_hooks_next(&ev))
            ;
        return;
    }
    while (pix_hooks_next(&ev))
        handle(o, &ev, now);

    /* Keep the music queued ahead, as far as its pieces have reached the terminal */
    while (M.track >= 0 && !M.paused && M.next >= 0 && M.queued_until - now < QUEUE_AHEAD_MS)
    {
        track_t *t = &g_tracks[M.track];
        if (!t->cached[M.next])
            break;
        if (M.queued_until < now)
            M.queued_until = now;       /* it ran dry (a piece arrived late): start again from now */
        apc_cmd(o, "A;Load;S=%d;" MUS_DIR "%s_%02d.ogg", MUSIC_SLOT, t->name, M.next);
        apc_cmd(o, "A;Queue;C=%d;S=%d", MUSIC_CH, MUSIC_SLOT);
        M.piece_started[M.next] = M.queued_until;
        M.queued_until += piece_ms(t, M.next);
        M.next++;
        if (M.next == t->pieces)
            M.next = M.looping ? t->loop_piece : -1;    /* back to where the song loops from, or the end */
    }
}

/* The next piece to upload: the one the music will reach first, then the rest of this song, then other songs */
static bool next_upload(int *track, int *piece, long now, bool *urgent)
{
    if (M.track >= 0)
    {
        track_t *t = &g_tracks[M.track];
        int k = M.paused ? M.playing_piece : M.next;
        long due = M.paused ? now + URGENT_MS * 2 : M.queued_until;
        for (int i = 0; k >= 0 && i < t->pieces; i++)
        {
            if (!t->cached[k])
            {
                *track = M.track;
                *piece = k;
                *urgent = due - now < URGENT_MS;
                return true;
            }
            due += piece_ms(t, k);
            k = k + 1 < t->pieces ? k + 1 : t->loop_piece;
        }
    }
    for (int tr = 0; tr < g_track_count; tr++)
        for (int k = 0; k < g_tracks[tr].pieces; k++)
            if (!g_tracks[tr].cached[k])
            {
                *track = tr;
                *piece = k;
                *urgent = false;
                return true;
            }
    return false;
}

int pix_sound_upload_wanted(long now)
{
    int t, k;
    bool urgent;
    if (!g_ready || !next_upload(&t, &k, now, &urgent))
        return 0;
    return urgent ? 2 : 1;
}

size_t pix_sound_upload(outbuf_t *o, long now)
{
    int t, k;
    bool urgent;
    char head[64];
    size_t before = o->len;

    if (!g_ready || !next_upload(&t, &k, now, &urgent))
        return 0;
    snprintf(head, sizeof(head), "C;S;" MUS_DIR "%s_%02d.ogg;", g_tracks[t].name, k);
    apc_blob(o, head, g_tracks[t].data[k], g_tracks[t].size[k]);
    g_tracks[t].cached[k] = true;
    return o->len - before;
}

void pix_sound_stop(outbuf_t *o)
{
    if (!g_ready)
        return;
    music_stop(o);
    for (int c = 0; c < SFX_CHANNELS; c++)
        apc_cmd(o, "A;Flush;C=%d", SFX_CH0 + c);
}
