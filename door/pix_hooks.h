/*
 * pix_hooks.h -- Duke Nukem 3D's sound and music calls, caught for the JPEG XL graphics mode. See pix_hooks.c.
 */

#ifndef PIX_HOOKS_H
#define PIX_HOOKS_H

#include <stdbool.h>
#include <stddef.h>

/* The game's headers pack some structs (#pragma pack): this one is laid out the same wherever it's included (the
 * Wolfenstein door's crash of 2026-09-27 came from a packed copy of its event) */
#pragma pack(push, 8)

typedef enum
{
    PIX_SFX_START,      /* voice, sound (the game's sound number), name (its file in the GRP), left, right, loop */
    PIX_SFX_PAN,        /* voice, left, right: a sound moved while playing */
    PIX_SFX_STOP,       /* voice (-1: all of them) */
    PIX_MUS_PLAY,       /* hash (sha256 of the MIDI file) */
    PIX_MUS_STOP,
    PIX_MUS_PAUSE,
    PIX_MUS_RESUME,
    PIX_MUS_VOLUME,     /* volume, 0-255 (the game's music volume) */
} pix_event_kind_t;

typedef struct
{
    pix_event_kind_t kind;
    int   voice, sound, volume;
    float left, right;  /* 0-1 a side: what MultiVoc mixes the sound at, the game's sound volume included */
    bool  loop;
    char  name[16];
    char  hash[65];
} pix_event_t;

#pragma pack(pop)

/* While on, the game's sound calls are also turned into events here. Its own mixer runs on regardless (its output is
 * thrown away, as in ANSI mode), so its sound timing stays exactly as the game expects. Off: nothing is queued. */
void pix_hooks_enable(bool on);

/* The next event, oldest first. False when there are none. Called from the door's thread. */
bool pix_hooks_next(pix_event_t *ev);

/* The game's music volume (0-255), as it was last set, even before the mode was on */
int pix_hooks_music_volume(void);

#endif
