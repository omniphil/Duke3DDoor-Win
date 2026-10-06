/*
 * ansi_host.c -- the Duke Nukem 3D module, run here on the BBS instead of in TERMinator's sandbox.
 *
 * A caller without TRACE can't run the game themselves, so the door runs it for them and sends pictures of it: ANSI
 * (ansi_play.c) or JPEG XL (pix_play.c). The game is the very same module that goes to TERMinator as duke3d.wasm
 * (JFDuke3D with ../module/src as its platform layer), only compiled for this machine: it only ever talks to the
 * "trace_*" functions of trace_api.h, and this file answers them the way TERMinator would. (The Wolfenstein 3D door's
 * ansi_host.c, for Duke.)
 *
 *   present         keeps the newest frame for the door to send
 *   time            a monotonic clock
 *   assets          the game data the door has already loaded
 *   send            pieces of the player's files, kept by the door's files.c as if they had come over TRACE
 *   sound           mixed and thrown away at the real rate. The game keeps its sound timing in the mixer (a sound
 *                   "ends" when MultiVoc reaches its end, and the game won't start some again until then), so the
 *                   mixer has to keep running. The music isn't synthesised at all (duketrace_music_render = 0): only
 *                   its sequencer runs, which costs nothing. The JPEG XL mode plays the same sounds on the caller's
 *                   terminal instead (pix_hooks.c)
 *   quit            ends the game thread; the door notices and shows its closing screen
 */

#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "trace_api.h"

#include "ansi_host.h"
#include "ansi_hud.h"
#include "files.h"
#include "platform.h"
#include "trace_duke.h"

#define TE_IN_KEY  1
#define AUDIO_RATE 44100

/* The module holds any key at least this long (module/src/tracelayer.c), so a tap that comes and goes between two of
 * the game's looks at the keyboard is still seen. ansi_input.c paces its presses itself, so there it is 0. */
extern int duketrace_min_hold_ms;
/* module/src/audio_trace.c: 0 = run the music's sequencer but don't synthesise it (nobody here would hear it) */
extern int duketrace_music_render;
/* module/src/duketrace.c: the door's "quit" (save the settings and go) */
extern volatile int duketrace_quit_requested;

/* Overrides for the game's own settings, read by pix_hooks.c's wrap of CONFIG_ReadSetup: -1 leaves the player's */
int duke_force_screen_size = -1;

static const unsigned char *g_pak;
static size_t               g_pak_size;
static char                 g_pak_hash[65];

static pthread_mutex_t g_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t       *g_frame;
static int             g_frame_w, g_frame_h;
static size_t          g_frame_cap;
static unsigned        g_frame_seq;

static bool            g_pixel_mode, g_pixel_keys;
static int             g_default_hold = -1;  /* the module's own duketrace_min_hold_ms */

static atomic_int      g_finished;
static long            g_start;
static long long       g_audio_written;     /* frames the game has "played" */
static FILE           *g_log;

/* ---- what the module calls ---- */

void trace_present(const uint32_t *pixels, int32_t width, int32_t height, int32_t flags)
{
    size_t need = (size_t)width * (size_t)height;
    (void)flags;

    if (width <= 0 || height <= 0)
        return;
    pthread_mutex_lock(&g_frame_lock);
    if (need > g_frame_cap)
    {
        uint32_t *grown = realloc(g_frame, need * sizeof(uint32_t));
        if (grown == NULL)
        {
            pthread_mutex_unlock(&g_frame_lock);
            return;
        }
        g_frame = grown;
        g_frame_cap = need;
    }
    memcpy(g_frame, pixels, need * sizeof(uint32_t));
    g_frame_w = width;
    g_frame_h = height;
    g_frame_seq++;
    pthread_mutex_unlock(&g_frame_lock);
    ansi_hud_frame_done();      /* the text drawn into this frame goes with it */
}

/* The game's own messages. A door's output goes to the caller, so these only go to a file, and only when the sysop
 * asks for one with DUKEDOOR_LOG=<file>. */
void trace_log(const char *text, int32_t length)
{
    if (g_log != NULL)
    {
        fprintf(g_log, "%.*s\n", (int)length, text);
        fflush(g_log);
    }
}

void trace_quit(int32_t code)
{
    trace_log("duke3d: quit", 12);
    (void)code;
    atomic_store(&g_finished, 1);
    pthread_exit(NULL);     /* only the game thread ends; the door carries on to its closing screen */
}

int32_t trace_time_ms(void)
{
    return (int32_t)(plat_now_ms() - g_start);
}

void trace_set_tick(int32_t hz) { (void)hz; }

/* The player's files, sent up by the game: kept by files.c exactly as if they had come over TRACE */
int32_t trace_send(const void *data, int32_t length)
{
    if (length <= 0)
        return 0;
    trace_duke_module_message((const unsigned char *)data, (size_t)length);
    return length;
}

int32_t trace_send_room(void) { return 1 << 20; }

/* A tenth of a second ahead of the clock, like TERMinator's queue: the game's sound keeps real time */
int32_t trace_audio_room(void)
{
    long long due = (long long)trace_time_ms() * AUDIO_RATE / 1000 + AUDIO_RATE / 10;
    long long room = due - g_audio_written;
    return room < 0 ? 0 : room > 65535 ? 65535 : (int32_t)room;
}

int32_t trace_audio_write(const int16_t *frames, int32_t frame_count)
{
    (void)frames;
    if (frame_count <= 0)
        return 0;
    g_audio_written += frame_count;
    return frame_count;
}

int32_t trace_asset_size(const char *sha256)
{
    return strcmp(sha256, g_pak_hash) == 0 ? (int32_t)g_pak_size : 0;
}

int32_t trace_asset_read(const char *sha256, int32_t offset, void *buffer, int32_t length)
{
    if (strcmp(sha256, g_pak_hash) != 0 || offset < 0 || length <= 0 || (size_t)offset >= g_pak_size)
        return 0;
    if ((size_t)offset + (size_t)length > g_pak_size)
        length = (int32_t)(g_pak_size - (size_t)offset);
    memcpy(buffer, g_pak + offset, (size_t)length);
    return length;
}

/* ---- the door's side ---- */

void ansi_host_set_pixel_mode(bool on, bool keys)
{
    g_pixel_mode = on;
    g_pixel_keys = keys;
}

/* A message for the module: its text line, then the payload after a newline, exactly as TRACE would deliver it. */
static void native_send(const char *head, const void *payload, size_t len)
{
    size_t head_len = strlen(head);
    size_t total = head_len + (payload != NULL ? 1 + len : 0);
    char *message = malloc(total);

    if (message == NULL)
        return;
    memcpy(message, head, head_len);
    if (payload != NULL)
    {
        message[head_len] = '\n';
        memcpy(message + head_len + 1, payload, len);
    }
    trace_on_data(message, (int32_t)total);
    free(message);
}

bool ansi_host_start(const unsigned char *pak, size_t pak_size, const char *pak_hash)
{
    char start[96];
    const char *log_path = getenv("DUKEDOOR_LOG");

    if (log_path != NULL && *log_path)
        g_log = fopen(log_path, "a");

    g_start = plat_now_ms();
    g_pak = pak;
    g_pak_size = pak_size;
    snprintf(g_pak_hash, sizeof(g_pak_hash), "%s", pak_hash);

    if (g_default_hold < 0)
        g_default_hold = duketrace_min_hold_ms;
    duketrace_music_render = 0;
    if (g_pixel_mode)
    {
        /* JPEG XL: the game's own picture, status bar and sound, as the player has them set (TRACE's config). Keys
         * the terminal reports as presses and releases are held as TRACE holds them; without those, ansi_input.c
         * paces the presses itself, as in ANSI mode. */
        duke_force_screen_size = -1;
        duketrace_min_hold_ms = g_pixel_keys ? g_default_hold : 0;
    }
    else
    {
        /* ANSI: the whole picture is the view; the door draws the status bar as a line of text (ansi_play.c), as
         * Duke's own digits can't be read at 80 columns */
        duke_force_screen_size = 0;
        duketrace_min_hold_ms = 0;
    }

    if (trace_init() != 0)
        return false;

    /* The same messages the door sends over TRACE: the player's files first, then the data, which starts the game.
     * 320x200 is already more than 80x44 half-blocks can show, a quarter of the work of 640x400, and the JPEG XL
     * mode's own size. */
    files_send_all(native_send);
    snprintf(start, sizeof(start), "pak=%s res=320x200", g_pak_hash);
    trace_on_data(start, (int32_t)strlen(start));
    return true;
}

bool ansi_host_frame(uint32_t **pixels, int *width, int *height, unsigned *seq)
{
    bool fresh = false;

    pthread_mutex_lock(&g_frame_lock);
    if (g_frame != NULL && g_frame_seq != *seq)
    {
        size_t need = (size_t)g_frame_w * (size_t)g_frame_h * sizeof(uint32_t);
        uint32_t *copy = (*width) * (*height) >= g_frame_w * g_frame_h ? *pixels : realloc(*pixels, need);
        if (copy != NULL)
        {
            memcpy(copy, g_frame, need);
            *pixels = copy;
            *width = g_frame_w;
            *height = g_frame_h;
            *seq = g_frame_seq;
            fresh = true;
        }
    }
    pthread_mutex_unlock(&g_frame_lock);
    return fresh;
}

void ansi_host_key(int scancode, bool down)
{
    /* The arrows and the Insert/Home/PgUp block are E0 keys on a PC keyboard; as plain codes they'd be the keypad */
    bool extended = (scancode >= 0x47 && scancode <= 0x53 && scancode != 0x4A && scancode != 0x4C && scancode != 0x4E);
    trace_on_input(TE_IN_KEY, (down ? 1 : 0) | (extended ? 2 : 0), scancode, 0, 0);
}

bool ansi_host_finished(void)
{
    return atomic_load(&g_finished) != 0;
}

void ansi_host_stop(int wait_ms)
{
    long until = plat_now_ms() + wait_ms;

    if (ansi_host_finished())
        return;
    duketrace_quit_requested = 1;       /* the game thread saves its settings at its next look at the keyboard */
    while (!ansi_host_finished() && plat_now_ms() < until)
        plat_sleep_ms(10);
}
