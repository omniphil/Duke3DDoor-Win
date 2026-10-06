/*
 * midirender.c -- renders every song in DUKE3D.GRP to a WAV file, for the JPEG XL graphics mode.
 *
 * That mode plays music on the caller's own terminal from files it keeps in its cache, so the songs are rendered here
 * once, ahead of time, with the synth the TRACE module plays them on: TinySoundFont and the TimGM6mb General MIDI
 * soundfont. tools/make_music.py then cuts them into the short Ogg Vorbis pieces the door sends (the DOOM door's
 * musrender.c + make_music.py, for Duke).
 *
 *   ./midirender ../data/DUKE3D.GRP ../third_party/soundfont/TimGM6mb.sf2 /tmp/dukemusic
 *
 * Writes <out>/<SONG>.wav (16-bit stereo, 44100 Hz) and <out>/tracks.txt, one line per song: "<SONG> <sha256 of the
 * MIDI file> <loop start in ms>". <SONG> is the file's name without .MID, as the game names it (GRABBAG, STALKER...).
 * The WAV runs from the start to the loop end; when the game loops a song it goes back to the loop start, which
 * isn't always 0 (GRABBAG's is 0.70 s: a one-bar pickup is played only the first time).
 *
 * Duke's songs are EMIDI (Apogee's extended MIDI, which the game's own music driver reads): controllers 110-119 in a
 * track say which sound cards play it, and where the song loops. They are read the way jfaudiolib does for a General
 * MIDI device:
 *   110 include track for device v / 111 exclude it   (v: 0 General MIDI, 127 every device)
 *   112 program change and 113 volume, instead of the track's ordinary ones, when the track has any
 *   116/117 track loop, 118/119 song loop             the song is rendered once, up to its loop end; where the
 *                                                     loop starts goes in tracks.txt
 * A song with no loop end gets 1.5 s for its last notes to ring out, as DOOM's do.
 */

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../sha256.h"

#define RATE        44100
#define TAIL_SEC    1.5
#define GAIN_DB     -9.0f       /* TinySoundFont's global gain. At 0 dB TimGM6mb peaks at 1.5-2.6x full scale on GRABBAG,
                                   STALKER, STREETS, SNAKE1 (measured 2026-10-06); -9 dB keeps every song under 1.0.
                                   The TRACE module (module/src/audio_trace.c MUSIC_GAIN_DB) should match */
#define EMIDI_GM    0           /* EMIDI's device number for General MIDI */
#define EMIDI_ALL   127

typedef struct
{
    uint32_t tick;
    int      track, order;
    uint8_t  status, d1, d2;
    uint32_t tempo;             /* a tempo meta event (status 0xFF): microseconds a quarter note */
} event_t;

typedef struct
{
    int included;
    int has_program, has_volume;   /* EMIDI 112 / 113 present: they replace the ordinary ones */
} track_t;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int read_var(const uint8_t *p, size_t len, size_t *pos, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
    {
        if (*pos >= len)
            return 0;
        uint8_t c = p[(*pos)++];
        v = v << 7 | (c & 0x7F);
        if (!(c & 0x80))
        {
            *out = v;
            return 1;
        }
    }
    return 0;
}

static int compare_events(const void *a, const void *b)
{
    const event_t *x = a, *y = b;
    if (x->tick != y->tick)
        return x->tick < y->tick ? -1 : 1;
    return x->order - y->order;     /* same tick: file order (track by track), as a sequencer meets them */
}

static void write_wav(const char *path, const int16_t *pcm, long frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, fmt = 16, rate = RATE, bps = RATE * 4;
    uint16_t pcm_tag = 1, channels = 2, align = 4, bits = 16;

    if (f == NULL)
    {
        perror(path);
        exit(1);
    }
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm_tag, 2, 1, f); fwrite(&channels, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&bps, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm, 4, (size_t)frames, f);
    fclose(f);
}

/* Renders one song. Returns its length in frames (the PCM in *out), or -1 if it isn't a MIDI file we can read. */
static long render(tsf *synth, const uint8_t *mid, size_t len, int16_t **out, float *peak, long *loop_start)
{
    event_t *ev = NULL;
    size_t nev = 0, cap = 0;
    track_t tracks[64];
    int ntracks, division, order = 0;
    size_t pos;
    uint32_t loop_end = 0, last_tick = 0;
    int have_loop_end = 0;

    if (len < 14 || memcmp(mid, "MThd", 4) != 0)
        return -1;
    ntracks = mid[10] << 8 | mid[11];
    division = mid[12] << 8 | mid[13];
    if (division & 0x8000 || division == 0 || ntracks > 64)
        return -1;                          /* SMPTE time: not in Duke */
    pos = 8 + be32(mid + 4);

    for (int t = 0; t < ntracks; t++)
    {
        size_t tlen, end, p;
        uint32_t tick = 0;
        uint8_t running = 0;
        int include_found = 0;

        if (pos + 8 > len || memcmp(mid + pos, "MTrk", 4) != 0)
            return -1;
        tlen = be32(mid + pos + 4);
        p = pos + 8;
        end = p + tlen > len ? len : p + tlen;
        pos = p + tlen;
        tracks[t].included = 1;
        tracks[t].has_program = tracks[t].has_volume = 0;

        while (p < end)
        {
            uint32_t delta;
            uint8_t status;
            event_t e = { 0 };

            if (!read_var(mid, end, &p, &delta))
                break;
            tick += delta;
            if (p >= end)
                break;
            status = mid[p];
            if (status & 0x80)
                p++;
            else
                status = running;
            if (status == 0xFF)
            {
                uint32_t mlen;
                uint8_t type = p < end ? mid[p++] : 0;
                if (!read_var(mid, end, &p, &mlen) || p + mlen > end)
                    break;
                if (type == 0x51 && mlen == 3)
                {
                    e.status = 0xFF;
                    e.tempo = (uint32_t)mid[p] << 16 | (uint32_t)mid[p + 1] << 8 | mid[p + 2];
                }
                p += mlen;
                if (type == 0x2F)
                    break;
                if (e.status == 0)
                    continue;
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                uint32_t slen;
                if (!read_var(mid, end, &p, &slen))
                    break;
                p += slen;
                continue;
            }
            else if (status & 0x80)
            {
                int two = (status & 0xF0) != 0xC0 && (status & 0xF0) != 0xD0;
                running = status;
                e.status = status;
                e.d1 = p < end ? mid[p++] & 0x7F : 0;
                e.d2 = two && p < end ? mid[p++] & 0x7F : 0;
                if ((status & 0xF0) == 0xB0)
                {
                    /* EMIDI: worked out before playing, as jfaudiolib does */
                    if (e.d1 == 110)
                    {
                        if (!include_found)
                        {
                            include_found = 1;
                            tracks[t].included = 0;
                        }
                        if (e.d2 == EMIDI_GM || e.d2 == EMIDI_ALL)
                            tracks[t].included = 1;
                    }
                    else if (e.d1 == 111 && (e.d2 == EMIDI_GM || e.d2 == EMIDI_ALL))
                        tracks[t].included = 0;
                    else if (e.d1 == 112)
                        tracks[t].has_program = 1;
                    else if (e.d1 == 113)
                        tracks[t].has_volume = 1;
                }
            }
            else
                break;                      /* running status with nothing to run: a broken track */

            e.tick = tick;
            e.track = t;
            e.order = order++;
            if (nev == cap)
            {
                cap = cap ? cap * 2 : 4096;
                ev = realloc(ev, cap * sizeof(*ev));
                if (ev == NULL)
                    exit(1);
            }
            ev[nev++] = e;
        }
    }
    qsort(ev, nev, sizeof(*ev), compare_events);

    /* Where the song ends: its loop end if it has one, else its last event */
    for (size_t i = 0; i < nev; i++)
    {
        if (ev[i].status != 0xFF && !tracks[ev[i].track].included)
            continue;
        if (ev[i].tick > last_tick)
            last_tick = ev[i].tick;
        if ((ev[i].status & 0xF0) == 0xB0 && (ev[i].d1 == 117 || ev[i].d1 == 119))
        {
            have_loop_end = 1;
            if (ev[i].tick > loop_end)
                loop_end = ev[i].tick;
        }
    }
    if (!have_loop_end)
        loop_end = last_tick;

    /* Play it */
    tsf_reset(synth);
    for (int c = 0; c < 16; c++)
    {
        tsf_channel_set_presetnumber(synth, c, 0, c == 9);
        tsf_channel_midi_control(synth, c, 121, 0);
    }
    tsf_channel_set_bank_preset(synth, 9, 128, 0);

    double us_per_tick = 500000.0 / division, sample_pos = 0.0;
    long frames = 0, cap_frames = RATE * 60;
    float *pcm = malloc((size_t)cap_frames * 2 * sizeof(float));
    uint32_t now = 0;

    *loop_start = -1;
    for (size_t i = 0; i <= nev; i++)
    {
        uint32_t tick = i < nev ? ev[i].tick : loop_end;
        if (tick > loop_end)
            tick = loop_end;
        sample_pos += (double)(tick - now) * us_per_tick * RATE / 1e6;
        now = tick;
        long target = (long)sample_pos;
        if (i == nev && !have_loop_end)
            target += (long)(TAIL_SEC * RATE);
        if (target > frames)
        {
            if (target > cap_frames)
            {
                while (cap_frames < target)
                    cap_frames *= 2;
                pcm = realloc(pcm, (size_t)cap_frames * 2 * sizeof(float));
                if (pcm == NULL)
                    exit(1);
            }
            tsf_render_float(synth, pcm + frames * 2, (int)(target - frames), 0);
            frames = target;
        }
        if (i == nev || (i < nev && ev[i].tick >= loop_end && have_loop_end))
            break;

        const event_t *e = &ev[i];
        const track_t *tr = &tracks[e->track];
        int ch = e->status & 0x0F;
        if (e->status == 0xFF)
        {
            us_per_tick = (double)e->tempo / division;
            continue;
        }
        if (!tr->included)
            continue;
        switch (e->status & 0xF0)
        {
        case 0x90:
            if (e->d2 > 0)
            {
                tsf_channel_note_on(synth, ch, e->d1, e->d2 / 127.0f);
                break;
            }
            /* fall through */
        case 0x80:
            tsf_channel_note_off(synth, ch, e->d1);
            break;
        case 0xC0:
            if (!tr->has_program)
                tsf_channel_set_presetnumber(synth, ch, e->d1, ch == 9);
            break;
        case 0xE0:
            tsf_channel_set_pitchwheel(synth, ch, e->d2 << 7 | e->d1);
            break;
        case 0xB0:
            if (e->d1 == 112)
            {
                if (tr->has_program)
                    tsf_channel_set_presetnumber(synth, ch, e->d2, ch == 9);
            }
            else if (e->d1 == 113)
            {
                if (tr->has_volume)
                    tsf_channel_midi_control(synth, ch, 7, e->d2);
            }
            else if (e->d1 == 116 || e->d1 == 118)
            {
                if (*loop_start < 0)
                    *loop_start = (long)sample_pos;     /* where a loop goes back to */
            }
            else if (e->d1 >= 110 && e->d1 <= 119)
                ;                           /* EMIDI: already dealt with */
            else if (e->d1 == 7 && tr->has_volume)
                ;
            else
                tsf_channel_midi_control(synth, ch, e->d1, e->d2);
            break;
        }
    }
    free(ev);
    if (*loop_start < 0 || !have_loop_end)
        *loop_start = 0;                    /* no loop marked: the whole song repeats */

    *out = malloc((size_t)frames * 2 * sizeof(int16_t));
    *peak = 0;
    for (long i = 0; i < frames * 2; i++)
    {
        float v = pcm[i], a = v < 0 ? -v : v;
        if (a > *peak)
            *peak = a;
        v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
        (*out)[i] = (int16_t)(v * 32767.0f);
    }
    free(pcm);
    return frames;
}

int main(int argc, char **argv)
{
    FILE *f, *list;
    uint8_t *grp;
    long size;
    tsf *synth;
    char path[1024];
    uint32_t count;
    size_t off;

    if (argc != 4)
    {
        fprintf(stderr, "usage: midirender <DUKE3D.GRP> <soundfont.sf2> <out folder>\n");
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL)
    {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    grp = malloc((size_t)size);
    if (grp == NULL || fread(grp, 1, (size_t)size, f) != (size_t)size || size < 16 || memcmp(grp, "KenSilverman", 12) != 0)
    {
        fprintf(stderr, "%s: not a GRP file\n", argv[1]);
        return 1;
    }
    fclose(f);

    synth = tsf_load_filename(argv[2]);
    if (synth == NULL)
    {
        fprintf(stderr, "%s: can't load the soundfont\n", argv[2]);
        return 1;
    }
    tsf_set_output(synth, TSF_STEREO_INTERLEAVED, RATE, GAIN_DB);
    tsf_set_max_voices(synth, 256);

    mkdir(argv[3], 0755);
    snprintf(path, sizeof(path), "%s/tracks.txt", argv[3]);
    list = fopen(path, "w");
    if (list == NULL)
    {
        perror(path);
        return 1;
    }

    /* "KenSilverman", u32 count, count x { char name[12]; u32 size }, then the files in that order */
    count = le32(grp + 12);
    off = 16 + (size_t)count * 16;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *entry = grp + 16 + (size_t)i * 16;
        uint32_t len = le32(entry + 12);
        char name[13];
        int16_t *pcm;
        float peak;
        long frames, loop_start;
        char hash[65];
        size_t n;

        memcpy(name, entry, 12);
        name[12] = '\0';
        if (off + len > (size_t)size)
            break;
        n = strlen(name);
        if (n > 4 && strcmp(name + n - 4, ".MID") == 0)
        {
            name[n - 4] = '\0';
            frames = render(synth, grp + off, len, &pcm, &peak, &loop_start);
            if (frames < 0)
                fprintf(stderr, "%s: not a MIDI file we can read; skipped\n", name);
            else
            {
                snprintf(path, sizeof(path), "%s/%s.wav", argv[3], name);
                write_wav(path, pcm, frames);
                free(pcm);
                sha256_hex(grp + off, len, hash);
                fprintf(list, "%s %s %ld\n", name, hash, loop_start * 1000 / RATE);
                printf("%-10s %6.1f s  loops from %5.2f s  peak %.2f%s\n", name, frames / (double)RATE,
                       loop_start / (double)RATE, peak,
                       peak > 1.0f ? "  (clipped: lower GAIN_DB)" : "");
            }
        }
        off += len;
    }
    fclose(list);
    tsf_close(synth);
    return 0;
}
