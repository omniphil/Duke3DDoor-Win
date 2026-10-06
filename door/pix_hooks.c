/*
 * pix_hooks.c -- Duke Nukem 3D's sound and music calls, caught for the JPEG XL graphics mode. (The Wolfenstein door's
 * pix_hooks.c, for Duke.)
 *
 * In that mode the sound plays on the caller's terminal, from files it keeps in its cache (pix_sound.c), so what the
 * door needs from the game is not the mixed sound but the events: this effect started on that voice, this loud on each
 * side; it moved; it stopped; this song started. JFDuke3D and jfaudiolib are left alone: the door is linked with
 * --wrap for each of these functions (Makefile), so the game's calls land here first. Every call is passed on to the
 * real function, in this mode too, so the game's own sound state and timing (MultiVoc decides when an effect has
 * finished, and the game won't start some again until then) stay exactly as they are in ANSI mode, where the mixed
 * sound is thrown away (ansi_host.c).
 *
 * --wrap only catches calls between files, which is what these are: the game (sounds.c, game.c, player.c, ...) calls
 * jfaudiolib's fx_man.c and music.c. Where the game uses:
 *   effects   FX_PlayAuto3D (an angle and distance from the player) and FX_PlayLoopedAuto (engines, water, a
 *             level's ambience), moved with FX_Pan3D every frame, stopped with FX_StopSound / FX_StopAllSounds.
 *             The sound number is the call's callbackval; its file is the game's sounds[] table (from the CON files).
 *             Pitch changes (random variation, under water) aren't carried over: the terminal plays every effect at
 *             its own rate.
 *   music     MUSIC_PlaySong (a MIDI file, named by its sha256 as music/index.txt has it), MUSIC_StopSong,
 *             MUSIC_Pause / MUSIC_Continue, MUSIC_SetVolume
 *   settings  FX_SetVolume, FX_SetReverseStereo; and CONFIG_ReadSetup, where the door sets its own view size for the
 *             ANSI mode (ansi_host.c duke_force_screen_size)
 *
 * The levels are MultiVoc's own: a 3D sound's left and right come from its pan table (MV_CalcPanTable, copied below),
 * times the game's sound volume.
 *
 * Runs on the game's thread; the door reads the events from its own, hence the lock.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "duke3d.h"

#include "pix_hooks.h"      /* after the game's headers: it sets its own packing */
#include "sha256.h"

#define QUEUE 512

/* multivoc.c's own numbers */
#define MV_MAX_VOLUME 63
#define MV_PAN_POSITIONS 32

static atomic_int      g_on;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pix_event_t     g_queue[QUEUE];
static int             g_head, g_tail;

static int  g_fx_volume = 255;          /* FX_SetVolume, 0-255 */
static int  g_reverse;                  /* FX_SetReverseStereo */
static int  g_music_volume = 255;       /* MUSIC_SetVolume */

void pix_hooks_enable(bool on)
{
    atomic_store(&g_on, on ? 1 : 0);
}

int pix_hooks_music_volume(void)
{
    return g_music_volume;
}

bool pix_hooks_next(pix_event_t *ev)
{
    bool got = false;
    pthread_mutex_lock(&g_lock);
    if (g_head != g_tail)
    {
        *ev = g_queue[g_head];
        g_head = (g_head + 1) % QUEUE;
        got = true;
    }
    pthread_mutex_unlock(&g_lock);
    return got;
}

static void push(const pix_event_t *ev)
{
    if (!atomic_load(&g_on))
        return;
    pthread_mutex_lock(&g_lock);
    if ((g_tail + 1) % QUEUE != g_head)
    {
        g_queue[g_tail] = *ev;
        g_tail = (g_tail + 1) % QUEUE;
    }
    pthread_mutex_unlock(&g_lock);
}

static void push_simple(pix_event_kind_t kind)
{
    pix_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    push(&ev);
}

/* ---- MultiVoc's levels ---- */

/* MIX_VOLUME (_multivc.h): 0-255 down to the 64 volume steps */
static int mix_volume(int v)
{
    v = v < 0 ? 0 : v > 255 ? 255 : v;
    return (v * (MV_MAX_VOLUME + 1)) >> 8;
}

/* One side's level (0-255, as MultiVoc takes it) as a gain, with the game's sound volume */
static float side_gain(int level)
{
    return (float)mix_volume(level) / MV_MAX_VOLUME * (float)g_fx_volume / 255.0f;
}

/* MV_Pan3D / MV_PlayVOC3D: an angle (0-31) and distance (0-255) as left and right (MV_CalcPanTable) */
static void pan3d(int angle, int distance, float *left, float *right)
{
    static int pan_left[MV_PAN_POSITIONS][MV_MAX_VOLUME + 1], pan_right[MV_PAN_POSITIONS][MV_MAX_VOLUME + 1];
    static bool made;
    int volume, l, r;

    if (!made)
    {
        int half = MV_PAN_POSITIONS / 2;
        for (int d = 0; d <= MV_MAX_VOLUME; d++)
        {
            int level = (255 * (MV_MAX_VOLUME - d)) / MV_MAX_VOLUME;
            for (int a = 0; a <= half / 2; a++)
            {
                int ramp = level - ((level * a) / (MV_PAN_POSITIONS / 4));
                pan_left[a][d] = ramp;
                pan_left[half - a][d] = ramp;
                pan_left[half + a][d] = level;
                pan_left[MV_PAN_POSITIONS - 1 - a][d] = level;
                pan_right[a][d] = level;
                pan_right[half - a][d] = level;
                pan_right[half + a][d] = ramp;
                pan_right[MV_PAN_POSITIONS - 1 - a][d] = ramp;
            }
        }
        made = true;
    }
    if (distance < 0)
    {
        distance = -distance;
        angle += MV_PAN_POSITIONS / 2;
    }
    volume = mix_volume(distance);
    angle &= MV_PAN_POSITIONS - 1;
    l = pan_left[angle][volume];
    r = pan_right[angle][volume];
    if (g_reverse)
    {
        int t = l;
        l = r;
        r = t;
    }
    *left = side_gain(l);
    *right = side_gain(r);
}

/* ---- effects ---- */

int  __real_FX_PlayAuto3D(char *ptr, unsigned int length, int pitchoffset, int angle, int distance, int priority,
                          unsigned int callbackval);
int  __real_FX_PlayLoopedAuto(char *ptr, unsigned int length, int loopstart, int loopend, int pitchoffset, int vol,
                              int left, int right, int priority, unsigned int callbackval);
int  __real_FX_Pan3D(int handle, int angle, int distance);
int  __real_FX_StopSound(int handle);
int  __real_FX_StopAllSounds(void);
void __real_FX_SetVolume(int volume);
void __real_FX_SetReverseStereo(int setting);

/* A started effect, when it's one of the game's numbered sounds (the RTS's remote ridicule sounds aren't) */
static void started(int voice, unsigned int callbackval, float left, float right, bool loop)
{
    pix_event_t ev;
    int num = (int)callbackval;

    if (voice <= 0 || num < 0 || num >= NUM_SOUNDS || sounds[num][0] == '\0')
        return;
    memset(&ev, 0, sizeof(ev));
    ev.kind = PIX_SFX_START;
    ev.voice = voice;
    ev.sound = num;
    ev.left = left;
    ev.right = right;
    ev.loop = loop;
    strncpy(ev.name, sounds[num], sizeof(ev.name) - 1);
    push(&ev);
}

int __wrap_FX_PlayAuto3D(char *ptr, unsigned int length, int pitchoffset, int angle, int distance, int priority,
                         unsigned int callbackval)
{
    int voice = __real_FX_PlayAuto3D(ptr, length, pitchoffset, angle, distance, priority, callbackval);
    float left, right;

    pan3d(angle, distance, &left, &right);
    started(voice, callbackval, left, right, false);
    return voice;
}

int __wrap_FX_PlayLoopedAuto(char *ptr, unsigned int length, int loopstart, int loopend, int pitchoffset, int vol,
                             int left, int right, int priority, unsigned int callbackval)
{
    int voice = __real_FX_PlayLoopedAuto(ptr, length, loopstart, loopend, pitchoffset, vol, left, right, priority,
                                         callbackval);
    if (g_reverse)
    {
        int t = left;
        left = right;
        right = t;
    }
    started(voice, callbackval, side_gain(left), side_gain(right), true);
    return voice;
}

int __wrap_FX_Pan3D(int handle, int angle, int distance)
{
    int ret = __real_FX_Pan3D(handle, angle, distance);
    pix_event_t ev;

    if (ret == FX_Ok)
    {
        memset(&ev, 0, sizeof(ev));
        ev.kind = PIX_SFX_PAN;
        ev.voice = handle;
        pan3d(angle, distance, &ev.left, &ev.right);
        push(&ev);
    }
    return ret;
}

int __wrap_FX_StopSound(int handle)
{
    int ret = __real_FX_StopSound(handle);
    pix_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = PIX_SFX_STOP;
    ev.voice = handle;
    push(&ev);
    return ret;
}

int __wrap_FX_StopAllSounds(void)
{
    int ret = __real_FX_StopAllSounds();
    pix_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = PIX_SFX_STOP;
    ev.voice = -1;
    push(&ev);
    return ret;
}

void __wrap_FX_SetVolume(int volume)
{
    __real_FX_SetVolume(volume);
    g_fx_volume = volume < 0 ? 0 : volume > 255 ? 255 : volume;
}

void __wrap_FX_SetReverseStereo(int setting)
{
    __real_FX_SetReverseStereo(setting);
    g_reverse = setting;
}

/* ---- music ---- */

int  __real_MUSIC_PlaySong(void *song, unsigned int length, int loopflag);
int  __real_MUSIC_StopSong(void);
void __real_MUSIC_Pause(void);
void __real_MUSIC_Continue(void);
void __real_MUSIC_SetVolume(int volume);

int __wrap_MUSIC_PlaySong(void *song, unsigned int length, int loopflag)
{
    int ret = __real_MUSIC_PlaySong(song, length, loopflag);
    pix_event_t ev;

    if (ret == MUSIC_Ok && atomic_load(&g_on))
    {
        memset(&ev, 0, sizeof(ev));
        ev.kind = PIX_MUS_PLAY;
        ev.loop = loopflag != 0;
        sha256_hex((const uint8_t *)song, length, ev.hash);
        push(&ev);
    }
    return ret;
}

int __wrap_MUSIC_StopSong(void)
{
    int ret = __real_MUSIC_StopSong();
    push_simple(PIX_MUS_STOP);
    return ret;
}

void __wrap_MUSIC_Pause(void)
{
    __real_MUSIC_Pause();
    push_simple(PIX_MUS_PAUSE);
}

void __wrap_MUSIC_Continue(void)
{
    __real_MUSIC_Continue();
    push_simple(PIX_MUS_RESUME);
}

void __wrap_MUSIC_SetVolume(int volume)
{
    pix_event_t ev;

    __real_MUSIC_SetVolume(volume);
    g_music_volume = volume < 0 ? 0 : volume > 255 ? 255 : volume;
    memset(&ev, 0, sizeof(ev));
    ev.kind = PIX_MUS_VOLUME;
    ev.volume = g_music_volume;
    push(&ev);
}

/* ---- settings ---- */

extern int duke_force_screen_size;      /* ansi_host.c: -1 = the player's own */

int32 __real_CONFIG_ReadSetup(void);

int32 __wrap_CONFIG_ReadSetup(void)
{
    int32 ret = __real_CONFIG_ReadSetup();
    if (duke_force_screen_size >= 0)
        ud.screen_size = duke_force_screen_size;
    return ret;
}
