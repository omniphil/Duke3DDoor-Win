/*
 * audio_trace.c -- jfaudiolib's sound drivers for TRACE: digitised sound (MultiVoc, which mixes Duke's VOC and WAV
 * effects itself) and General MIDI music through TinySoundFont, both mixed on the game thread into TERMinator's sound
 * queue (S16 stereo, 44.1 kHz).
 *
 * jfaudiolib picks its drivers from a fixed table (drivers.c). The game's "autodetect" takes the first that works, and
 * in this build the only ones compiled in are the "SDL" slot (PCM) and the "FluidSynth" slot (MIDI), so those names
 * are answered here (the Makefile builds drivers.c with HAVE_SDL and HAVE_FLUIDSYNTH). Neither SDL nor FluidSynth is
 * used.
 *
 * duketrace_pump_audio() runs from handleevents() and showframe(), so whatever the game is doing, the queue is kept at
 * full (about a tenth of a second). Each block of MIX_FRAMES: MultiVoc's next mixed buffer (exactly what driver_sdl.c's fillData
 * does), plus the music, advanced by the MIDI tick rate (MIDI_ServiceRoutine once per tick, as the FluidSynth
 * driver's sequencer thread does) and rendered by TinySoundFont between ticks.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "trace_api.h"
#include "duketrace.h"
#include "midifuncs.h"
#include "driver_sdl.h"
#include "driver_fluidsynth.h"

/* tsf.h calls its stream's read and skip members: not the file calls duketrace_compat.h redirects */
#undef read
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "tsf.h"

#define RATE        44100
#define MIX_FRAMES  256         /* one block: about 6 ms */

/*
 * Levels, set so Duke sits beside the other doors through TERMinator (Wolf3D ~-26, DOOM ~-30 dBFS RMS), measured with
 * DUKETRACE_STATS (below) on 2026-10-06 at the game's default volumes:
 *                                   unscaled                          with these gains
 *   title music (TimGM6mb)          -17 to -21 RMS, -1.0 peak          -26 to -30 RMS, -10 peak
 *   E1L1 music                      -31 RMS, -15 peak                  -40 RMS, -24 peak
 *   E1L1 pistol fire + explosions   -14 RMS, 0.0 peak (clips)          -23 RMS, -9 peak
 *   whole run (title, menu, E1L1)                                      -26 RMS, -7.5 peak
 * MultiVoc mixes Duke's effects close to full scale, so they are lowered most.
 */
#define MUSIC_GAIN_DB  -9.0f      /* the same as the door's JPEG XL pre-renders; at 0 dB four of the seven songs clip */
#define FX_GAIN        0.35f

long duketrace_audio_frames_written;

/* 1: the music is synthesised. The door's own copy of the game (ANSI and JPEG XL modes, ../door/ansi_host.c) sets 0:
 * the song's sequencer still runs, but no notes are played, as nobody on the BBS would hear them */
int duketrace_music_render = 1;

/* ---- PCM: what MultiVoc mixes ---- */

enum { Err_Error = -1, Err_Ok = 0, Err_Uninitialised, Err_NoSoundFont, Err_CD };

static int   g_error;
static int   g_pcm_open, g_pcm_playing;
static char *g_mix_buffer;
static int   g_mix_size, g_mix_count, g_mix_current, g_mix_used;
static void (*g_mix_callback)(void);

int SDLDrv_GetError(void) { return g_error; }

const char *SDLDrv_ErrorString(int n)
{
    switch (n == Err_Error ? g_error : n)
    {
    case Err_Ok:            return "TRACE sound ok.";
    case Err_Uninitialised: return "TRACE sound uninitialised.";
    case Err_CD:            return "There is no CD audio.";
    default:                return "Unknown TRACE sound error.";
    }
}

int SDLDrv_PCM_Init(int *mixrate, int *numchannels, int *samplebits, void *initdata)
{
    (void)initdata;
    *mixrate = RATE;            /* TERMinator's own format, whatever the config asks for */
    *numchannels = 2;
    *samplebits = 16;
    g_pcm_open = 1;
    return Err_Ok;
}

void SDLDrv_PCM_Shutdown(void)
{
    g_pcm_open = g_pcm_playing = 0;
}

int SDLDrv_PCM_BeginPlayback(char *BufferStart, int BufferSize, int NumDivisions, void (*CallBackFunc)(void))
{
    if (!g_pcm_open)
    {
        g_error = Err_Uninitialised;
        return Err_Error;
    }
    g_mix_buffer = BufferStart;
    g_mix_size = BufferSize;
    g_mix_count = NumDivisions;
    g_mix_current = 0;
    g_mix_used = 0;
    g_mix_callback = CallBackFunc;
    g_mix_callback();           /* prime the buffer, as driver_sdl.c does */
    g_pcm_playing = 1;
    return Err_Ok;
}

void SDLDrv_PCM_StopPlayback(void) { g_pcm_playing = 0; }

/* The game's calls into the sound code and the mixing are all on the game thread: nothing to lock */
void SDLDrv_PCM_Lock(void) { }
void SDLDrv_PCM_Unlock(void) { }

int  SDLDrv_CD_Init(void) { g_error = Err_CD; return Err_Error; }
void SDLDrv_CD_Shutdown(void) { }
int  SDLDrv_CD_Play(int track, int loop) { (void)track; (void)loop; g_error = Err_CD; return Err_Error; }
void SDLDrv_CD_Stop(void) { }
void SDLDrv_CD_Pause(int pauseon) { (void)pauseon; }
int  SDLDrv_CD_IsPlaying(void) { return 0; }
void SDLDrv_CD_SetVolume(int volume) { (void)volume; }

/* bytes of MultiVoc's output (16-bit stereo) into out, as driver_sdl.c's fillData */
static void fill_pcm(char *out, int bytes)
{
    if (!g_pcm_playing || g_mix_callback == NULL)
    {
        memset(out, 0, (size_t)bytes);
        return;
    }
    while (bytes > 0)
    {
        int len;
        if (g_mix_used == g_mix_size)
        {
            g_mix_callback();
            g_mix_used = 0;
            if (++g_mix_current >= g_mix_count)
                g_mix_current -= g_mix_count;
        }
        len = g_mix_size - g_mix_used;
        if (len > bytes)
            len = bytes;
        memcpy(out, g_mix_buffer + g_mix_current * g_mix_size + g_mix_used, (size_t)len);
        out += len;
        g_mix_used += len;
        bytes -= len;
    }
}

/* ---- MIDI: TinySoundFont ---- */

static tsf   *g_tsf;
static void (*g_midi_service)(void);
static double g_frames_per_tick = RATE / 192.0;     /* MIDI_PlaySong sets the real tempo first */
static double g_frames_to_tick;

int FluidSynthDrv_GetError(void) { return g_error; }

const char *FluidSynthDrv_ErrorString(int n)
{
    switch (n == Err_Error ? g_error : n)
    {
    case Err_Ok:          return "TinySoundFont ok.";
    case Err_NoSoundFont: return "The sound font (timgm6mb.sf2) isn't in the game data.";
    default:              return "Unknown TinySoundFont error.";
    }
}

static void Func_NoteOff(int channel, int key, int velocity)
{
    (void)velocity;
    tsf_channel_note_off(g_tsf, channel, key);
}

static void Func_NoteOn(int channel, int key, int velocity)
{
    if (!duketrace_music_render)
        return;
    if (velocity == 0)
        tsf_channel_note_off(g_tsf, channel, key);
    else
        tsf_channel_note_on(g_tsf, channel, key, velocity / 127.0f);
}

static void Func_PolyAftertouch(int channel, int key, int pressure) { (void)channel; (void)key; (void)pressure; }

static void Func_ControlChange(int channel, int number, int value)
{
    tsf_channel_midi_control(g_tsf, channel, number, value);
}

static void Func_ProgramChange(int channel, int program)
{
    /* Channel 10 (9 here) is General MIDI's drum kit: bank 128 in a sound font */
    if (!tsf_channel_set_presetnumber(g_tsf, channel, program, channel == 9))
        tsf_channel_set_presetnumber(g_tsf, channel, 0, channel == 9);
}

static void Func_ChannelAftertouch(int channel, int pressure) { (void)channel; (void)pressure; }

static void Func_PitchBend(int channel, int lsb, int msb)
{
    tsf_channel_set_pitchwheel(g_tsf, channel, lsb | (msb << 7));
}

static void Func_SysEx(const unsigned char *data, int length) { (void)data; (void)length; }

static void reset_channels(void)
{
    for (int ch = 0; ch < 16; ch++)
    {
        tsf_channel_set_presetnumber(g_tsf, ch, 0, ch == 9);
        tsf_channel_set_pitchwheel(g_tsf, ch, 8192);
    }
}

int FluidSynthDrv_MIDI_Init(midifuncs *funcs, const char *params)
{
    const uint8_t *sf2;
    size_t size = 0;
    (void)params;

    memset(funcs, 0, sizeof(midifuncs));
    if (g_tsf == NULL)
    {
        sf2 = duketrace_data_file("timgm6mb.sf2", &size);
        if (sf2 == NULL || (g_tsf = tsf_load_memory(sf2, (int)size)) == NULL)
        {
            duketrace_log("duke3d: no music: the sound font isn't in the game data");
            g_error = Err_NoSoundFont;
            return Err_Error;
        }
        tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, RATE, MUSIC_GAIN_DB);
        tsf_set_max_voices(g_tsf, 96);
        reset_channels();
    }

    funcs->NoteOff = Func_NoteOff;
    funcs->NoteOn = Func_NoteOn;
    funcs->PolyAftertouch = Func_PolyAftertouch;
    funcs->ControlChange = Func_ControlChange;
    funcs->ProgramChange = Func_ProgramChange;
    funcs->ChannelAftertouch = Func_ChannelAftertouch;
    funcs->PitchBend = Func_PitchBend;
    funcs->SysEx = Func_SysEx;
    /* No SetVolume: midi.c then sets the music volume through each channel's volume controller */
    return Err_Ok;
}

void FluidSynthDrv_MIDI_Shutdown(void)
{
    g_midi_service = NULL;
    if (g_tsf != NULL)
        tsf_note_off_all(g_tsf);
}

int FluidSynthDrv_MIDI_StartPlayback(void (*service)(void))
{
    g_midi_service = service;
    g_frames_to_tick = 0;
    return Err_Ok;
}

void FluidSynthDrv_MIDI_HaltPlayback(void)
{
    g_midi_service = NULL;
    if (g_tsf != NULL)
    {
        tsf_note_off_all(g_tsf);
        reset_channels();
    }
}

unsigned int FluidSynthDrv_MIDI_GetTick(void) { return 0; }

void FluidSynthDrv_MIDI_SetTempo(int tempo, int division)
{
    double tps = (double)tempo * (double)division / 60.0;
    if (tps > 0)
        g_frames_per_tick = RATE / tps;
}

void FluidSynthDrv_MIDI_Lock(void) { }
void FluidSynthDrv_MIDI_Unlock(void) { }

/* frames of music into out (float stereo), calling the sequencer at every tick on the way */
static void render_music(float *out, int frames)
{
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);
    if (g_tsf == NULL)
        return;
    while (frames > 0)
    {
        int n;
        if (g_midi_service != NULL)
        {
            while (g_frames_to_tick <= 0 && g_midi_service != NULL)
            {
                g_midi_service();
                g_frames_to_tick += g_frames_per_tick;
            }
            n = (int)ceil(g_frames_to_tick);
            if (n > frames)
                n = frames;
            if (n < 1)
                n = 1;
            g_frames_to_tick -= n;
        }
        else
            n = frames;
        if (duketrace_music_render)
            tsf_render_float(g_tsf, out, n, 0);
        out += 2 * n;
        frames -= n;
    }
}

/* ---- the queue ---- */

#ifdef DUKETRACE_STATS
/* make CFLAGS_EXTRA=-DDUKETRACE_STATS: every 5 s of sound, the level of the effects and of the music apart, and the
 * frame rate, in the log. How the levels in README were measured. */
static void stats(const int16_t *pcm, const float *music)
{
    static double sp, sm;
    static long n, frames_then;
    static int peak_p, peak_m;
    for (int i = 0; i < MIX_FRAMES * 2; i++)
    {
        int m = (int)(music[i] * 32767.0f), p = (int)(pcm[i] * FX_GAIN);
        sp += (double)p * p;
        sm += (double)m * m;
        if (abs(p) > peak_p) peak_p = abs(p);
        if (abs(m) > peak_m) peak_m = abs(m);
    }
    n += MIX_FRAMES * 2;
    if (n >= RATE * 2 * 5)
    {
        duketrace_log("stats: effects %.1f dBFS RMS (peak %.1f), music %.1f dBFS RMS (peak %.1f), %.1f fps",
                      10 * log10(sp / n / 1073741824.0 + 1e-12), 20 * log10(peak_p / 32768.0 + 1e-12),
                      10 * log10(sm / n / 1073741824.0 + 1e-12), 20 * log10(peak_m / 32768.0 + 1e-12),
                      (duketrace_frames_shown - frames_then) / 5.0);
        frames_then = duketrace_frames_shown;
        sp = sm = 0;
        n = 0;
        peak_p = peak_m = 0;
    }
}
#endif

void duketrace_pump_audio(void)
{
    static int16_t pcm[MIX_FRAMES * 2];
    static float   music[MIX_FRAMES * 2];
    static int     busy;
    int room;

    if (busy || (!g_pcm_playing && g_tsf == NULL))
        return;
    busy = 1;       /* a callback could otherwise find its way back in here */

    /* TERMinator's queue holds about a tenth of a second: keeping it full is what the API asks (as Wolf3D's) */
    room = trace_audio_room();
    while (room >= MIX_FRAMES)
    {
        fill_pcm((char *)pcm, MIX_FRAMES * 4);
        render_music(music, MIX_FRAMES);
#ifdef DUKETRACE_STATS
        stats(pcm, music);
#endif
        for (int i = 0; i < MIX_FRAMES * 2; i++)
        {
            int v = (int)lrintf(pcm[i] * FX_GAIN + music[i] * 32767.0f);
            pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        if (trace_audio_write(pcm, MIX_FRAMES) <= 0)
            break;
        duketrace_audio_frames_written += MIX_FRAMES;
        room -= MIX_FRAMES;
    }
    busy = 0;
}
