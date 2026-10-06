/* duketrace.h -- what the TRACE platform layer's files share with each other (see duketrace.c). */

#ifndef DUKETRACE_H
#define DUKETRACE_H

#include <stddef.h>
#include <stdint.h>

/* One event from TERMinator (engine contract TE_IN_*), queued for the game thread */
typedef struct
{
    int32_t type, flags, a, b, c;
} duketrace_event_t;

#define TE_IN_KEY   1   /* flags bit0 pressed, bit1 extended (E0); a = set-1 scancode */
#define TE_IN_TEXT  2   /* a = UTF-16 code unit of a typed character */
#define TE_IN_FOCUS 5   /* flags bit0 focused */
#define TE_IN_QUIT  6   /* the player closed the picture, or the door went away */

void duketrace_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Events, oldest first; 0 when there are none */
int  duketrace_next_event(duketrace_event_t *out);

/* The size the door asked for (res=), 640x400 when it didn't */
extern int duketrace_width, duketrace_height;

/* Set by the door's "quit": the game thread saves the config and leaves at its next handleevents() */
extern volatile int duketrace_quit_requested;

/* The game's data: one packed asset the door sent (mkpak.py). NULL when there is no such file. */
const uint8_t *duketrace_data_file(const char *name, size_t *size);

/* The player's files (duke3d.cfg and the saves), which live on the BBS (file_trace.c) */
void duketrace_user_file_received(const char *name, size_t off, size_t total, const uint8_t *data, size_t len);
int  duketrace_user_files_pump(void);   /* sends what it can; returns 1 while anything is still waiting to go */

/* Sound (audio_trace.c): mixes what the queue can take */
void duketrace_pump_audio(void);
extern long duketrace_audio_frames_written;
extern unsigned duketrace_frames_shown;     /* frames presented so far (tracelayer.c) */

/* Everything that has to happen regularly on the game thread: sound, and saves going up to the BBS */
void duketrace_pump(void);

void duketrace_exit(int code) __attribute__((noreturn));

#endif
