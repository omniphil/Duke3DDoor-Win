/*
 * tracelayer.c -- JFBuild's platform layer (what jfbuild/src/sdlayer2.c does with SDL2), answered by TRACE.
 *
 * Time comes from trace_time_ms, keys from TERMinator's events, and a frame goes to TERMinator from showframe():
 * Build draws 8-bit pixels into `frame`, and they go through the faded palette into the BGRA picture trace_present
 * takes, shown at 4:3. Sound is audio_trace.c, files file_trace.c.
 *
 * The picture is 640x400 unless the door asks otherwise (res=): twice Mode 13h, which Build draws with the DOS
 * game's tall pixels (engine.c setgamemode gives 320x200 and 640x400 the same "pixelaspect"), so at 4:3 it is the
 * DOS picture with every 2D graphic (status bar, menus, fonts) scaled by exactly 2, and the ANSI door's 320x200 is the
 * same picture at half size. 640x480-style 4:3 sizes work too (square pixels).
 */

#include "build.h"
#include "baselayer_priv.h"
#include "baselayer.h"
#include "osd.h"
#include "a.h"

#include <string.h>
#include <time.h>

#include "trace_api.h"
#include "duketrace.h"

extern int app_main(int argc, char const * const argv[]);
extern void CONFIG_WriteSetup(void);

int displaycnt = 1;

static char apptitle[256] = "Build Engine";

/* ---- the little things ---- */

int wm_msgbox(const char *name, const char *fmt, ...)
{
    char text[1024];
    va_list va;
    va_start(va, fmt);
    vsnprintf(text, sizeof(text), fmt, va);
    va_end(va);
    duketrace_log("duke3d: %s: %s", name ? name : apptitle, text);   /* there is no window to show it in */
    return 1;
}

int wm_ynbox(const char *name, const char *fmt, ...)
{
    char text[1024];
    va_list va;
    va_start(va, fmt);
    vsnprintf(text, sizeof(text), fmt, va);
    va_end(va);
    duketrace_log("duke3d: %s: %s (answering no)", name ? name : apptitle, text);
    return 0;
}

int wm_filechooser(const char *initialdir, const char *initialfile, const char *type, int foropen, char **choice)
{
    (void)initialdir; (void)initialfile; (void)type; (void)foropen; (void)choice;
    return -1;
}

int  wm_idle(void *ptr) { (void)ptr; return 0; }
void wm_setapptitle(const char *name) { if (name) { strncpy(apptitle, name, sizeof(apptitle) - 1); } }
void wm_setwindowtitle(const char *name) { (void)name; }
void wm_allowbackgroundidle(int onf) { (void)onf; }
void wm_allowtaskswitching(int onf) { (void)onf; }

int initsystem(void)
{
    buildputs("TRACE system interface\n");
    return 0;
}

void uninitsystem(void) { }

/* The engine's console lines (buildputs/buildprintf) end up here too: they go to TERMinator's debug log */
void initputs(const char *str)
{
    char line[512];
    size_t n = strlen(str);
    if (n == 0)
        return;
    if (n >= sizeof(line))
        n = sizeof(line) - 1;
    memcpy(line, str, n);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        n--;
    line[n] = 0;
    if (n > 0)
        trace_log(line, (int32_t)n);
}

void debugprintf(const char *f, ...) { (void)f; }

/* ---- time ---- */

static int       g_timer_hz;
static long long g_timer_last;     /* ticks counted so far */
static void    (*g_timer_callback)(void);

int inittimer(int tickspersecond, void (*callback)(void))
{
    if (g_timer_hz)
        return 0;
    g_timer_hz = tickspersecond;
    g_timer_last = (long long)(unsigned)trace_time_ms() * g_timer_hz / 1000;
    g_timer_callback = callback;
    return 0;
}

void uninittimer(void) { g_timer_hz = 0; }

void sampletimer(void)
{
    long long now;
    int n;
    if (!g_timer_hz)
        return;
    now = (long long)(unsigned)trace_time_ms() * g_timer_hz / 1000;
    n = (int)(now - g_timer_last);
    if (n > 0)
    {
        totalclock += n;
        g_timer_last += n;
    }
    if (g_timer_callback)
        for (; n > 0; n--)
            g_timer_callback();
}

unsigned int getticks(void) { return (unsigned int)trace_time_ms(); }
unsigned int getusecticks(void) { return (unsigned int)trace_time_ms() * 1000u; }
int gettimerfreq(void) { return g_timer_hz; }

/* ---- video ---- */

static unsigned char *g_frame;          /* what Build draws into, bytesperline wide */
static uint32_t      *g_bgra;           /* the same through the palette */
unsigned              duketrace_frames_shown;

void getvalidmodes(void)
{
    if (validmodecnt)
        return;
    addvalidmode(duketrace_width, duketrace_height, 8, 0, 0, 0, -1);   /* the one size there is */
    sortvalidmodes();
}

const char *getdisplayname(int display) { return display == 0 ? "TERMinator" : NULL; }

/* Always the door's size and 8-bit (classic renderer), whatever the config file says */
int setvideomode(int xdim, int ydim, int bitspp, int fullsc)
{
    int pitch, i, j;
    (void)xdim; (void)ydim; (void)bitspp; (void)fullsc;

    xdim = duketrace_width;
    ydim = duketrace_height;
    if (xres == xdim && yres == ydim && bpp == 8 && !videomodereset)
    {
        OSD_ResizeDisplay(xres, yres);
        return 0;
    }
    if (baselayer_videomodewillchange)
        baselayer_videomodewillchange();

    buildprintf("Setting video mode %dx%d (8-bit, TRACE)\n", xdim, ydim);
    pitch = ((xdim | 1) + 4) & ~3;      /* as sdlayer2.c: a multiple of 4 */
    free(g_frame);
    free(g_bgra);
    g_frame = (unsigned char *)calloc((size_t)pitch, (size_t)ydim);
    g_bgra = (uint32_t *)calloc((size_t)xdim * (size_t)ydim, sizeof(uint32_t));
    if (g_frame == NULL || g_bgra == NULL)
    {
        buildputs("Unable to allocate framebuffer\n");
        return -1;
    }

    frameplace = (intptr_t)g_frame;
    bytesperline = pitch;
    imageSize = bytesperline * ydim;
    numpages = 1;
    setvlinebpl(bytesperline);
    for (i = j = 0; i <= ydim; i++)
    {
        ylookup[i] = j;
        j += bytesperline;
    }

    xres = xdim;
    yres = ydim;
    bpp = 8;
    fullscreen = 0;
    videomodereset = 0;
    if (baselayer_videomodedidchange)
        baselayer_videomodedidchange();
    OSD_ResizeDisplay(xres, yres);
    setpalettefade(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);
    return 0;
}

/* No more than this many frames a second go to TERMinator: the game moves at 30 tics a second, and Build smooths
 * the view between them, so 60 is smooth without drawing frames nobody sees */
#ifndef MAX_FPS
#define MAX_FPS 60
#endif

void showframe(void)
{
    static unsigned next_due;
    uint32_t lut[256];
    const unsigned char *in = g_frame;
    uint32_t *out = g_bgra;
    unsigned now;

    if (g_frame == NULL)
        return;

    /* Wait for this frame's turn, keeping sound and saves flowing meanwhile */
    for (;;)
    {
        duketrace_pump();
        now = (unsigned)trace_time_ms();
        if ((int)(now - next_due) >= 0)
            break;
        {
            struct timespec ts = { 0, 1000000L };
            nanosleep(&ts, NULL);
        }
    }
    next_due = ((int)(now - next_due) > 1000 / MAX_FPS) ? now + 1000 / MAX_FPS : next_due + 1000 / MAX_FPS;

    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | ((uint32_t)curpalettefaded[i].r << 16) | ((uint32_t)curpalettefaded[i].g << 8) |
                 curpalettefaded[i].b;
    for (int y = 0; y < yres; y++, in += bytesperline, out += xres)
        for (int x = 0; x < xres; x++)
            out[x] = lut[in[x]];
    trace_present(g_bgra, xres, yres, TRACE_PRESENT_ASPECT_4_3);
    duketrace_frames_shown++;
    duketrace_pump();
}

int setpalette(int start, int num, unsigned char *dapal)
{
    (void)start; (void)num; (void)dapal;    /* showframe reads curpalettefaded itself */
    return 0;
}

/* Only "put it back" works: brightness then falls back to the palette (engine.c setgamma) */
int setsysgamma(float shadergamma, float sysgamma)
{
    (void)shadergamma;
    return sysgamma < 0.f ? 0 : -1;
}

/* ---- keyboard ----
 *
 * TERMinator sends physical keys as set-1 scancodes, with a flag for the E0-prefixed ones (the arrow block, right
 * Ctrl/Alt, keypad Enter and /). That is already Build's own key numbering: the code, plus 0x80 for the E0 keys
 * (sdlayer2.c buildkeytranslationtable). Characters for typing (save names, the console, cheats) are made from the
 * keys on a US layout.
 */

/* Releases held back so a quick tap is seen: the game reads keystatus[] as held-down state, so a press and release
 * arriving in the same poll would never be seen. The ANSI door's native copy can set this to 0. */
#define MIN_HOLD_MS 50
int duketrace_min_hold_ms = MIN_HOLD_MS;

static unsigned char g_down[256];
static unsigned      g_down_at[256];
static unsigned      g_up_due[256];
static int           g_ups_pending;
static int           g_shift, g_ctrl, g_alt;

static const char g_ascii[128] =
{
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+',
};
static const char g_ascii_shift[128] =
{
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+',
};

static const char *const g_key_names[256] =
{
    [0x01] = "Escape", [0x02] = "1", [0x03] = "2", [0x04] = "3", [0x05] = "4", [0x06] = "5", [0x07] = "6",
    [0x08] = "7", [0x09] = "8", [0x0A] = "9", [0x0B] = "0", [0x0C] = "-", [0x0D] = "=", [0x0E] = "Backspace",
    [0x0F] = "Tab", [0x10] = "Q", [0x11] = "W", [0x12] = "E", [0x13] = "R", [0x14] = "T", [0x15] = "Y",
    [0x16] = "U", [0x17] = "I", [0x18] = "O", [0x19] = "P", [0x1A] = "[", [0x1B] = "]", [0x1C] = "Return",
    [0x1D] = "Left Ctrl", [0x1E] = "A", [0x1F] = "S", [0x20] = "D", [0x21] = "F", [0x22] = "G", [0x23] = "H",
    [0x24] = "J", [0x25] = "K", [0x26] = "L", [0x27] = ";", [0x28] = "'", [0x29] = "`", [0x2A] = "Left Shift",
    [0x2B] = "\\", [0x2C] = "Z", [0x2D] = "X", [0x2E] = "C", [0x2F] = "V", [0x30] = "B", [0x31] = "N",
    [0x32] = "M", [0x33] = ",", [0x34] = ".", [0x35] = "/", [0x36] = "Right Shift", [0x37] = "Keypad *",
    [0x38] = "Left Alt", [0x39] = "Space", [0x3A] = "CapsLock", [0x3B] = "F1", [0x3C] = "F2", [0x3D] = "F3",
    [0x3E] = "F4", [0x3F] = "F5", [0x40] = "F6", [0x41] = "F7", [0x42] = "F8", [0x43] = "F9", [0x44] = "F10",
    [0x45] = "Numlock", [0x46] = "ScrollLock", [0x47] = "Keypad 7", [0x48] = "Keypad 8", [0x49] = "Keypad 9",
    [0x4A] = "Keypad -", [0x4B] = "Keypad 4", [0x4C] = "Keypad 5", [0x4D] = "Keypad 6", [0x4E] = "Keypad +",
    [0x4F] = "Keypad 1", [0x50] = "Keypad 2", [0x51] = "Keypad 3", [0x52] = "Keypad 0", [0x53] = "Keypad .",
    [0x57] = "F11", [0x58] = "F12",
    [0x9C] = "Keypad Enter", [0x9D] = "Right Ctrl", [0xB5] = "Keypad /", [0xB8] = "Right Alt", [0xC7] = "Home",
    [0xC8] = "Up", [0xC9] = "PageUp", [0xCB] = "Left", [0xCD] = "Right", [0xCF] = "End", [0xD0] = "Down",
    [0xD1] = "PageDown", [0xD2] = "Insert", [0xD3] = "Delete", [0xDB] = "Left GUI", [0xDC] = "Right GUI",
};

int initinput(void)
{
    inputdevices = 1;       /* keyboard only: TERMinator gives modules no mouse for this game, and no joystick */
    return 0;
}

void uninitinput(void) { }

const char *getkeyname(int num)
{
    if ((unsigned)num >= 256)
        return NULL;
    return g_key_names[num] != NULL ? g_key_names[num] : "";
}

const char *getjoyname(int what, int num) { (void)what; (void)num; return NULL; }

int  initmouse(void) { return 0; }
void uninitmouse(void) { }
void grabmouse(int a) { (void)a; mousex = mousey = 0; }
void readmousexy(int *x, int *y) { *x = *y = 0; }
void readmousebstatus(int *b) { *b = 0; }

void releaseallbuttons(void) { }

static void push_char(int ch)
{
    if (OSD_HandleChar(ch))
        if (((keyasciififoend + 1) & (KEYFIFOSIZ - 1)) != keyasciififoplc)
        {
            keyasciififo[keyasciififoend] = (unsigned char)ch;
            keyasciififoend = (keyasciififoend + 1) & (KEYFIFOSIZ - 1);
        }
}

static void push_key(int code, int pressed)
{
    keyfifo[keyfifoend] = code;
    keyfifo[(keyfifoend + 1) & (KEYFIFOSIZ - 1)] = pressed;
    keyfifoend = (keyfifoend + 2) & (KEYFIFOSIZ - 1);
}

static void track_modifiers(int code, int pressed)
{
    switch (code)
    {
    case 0x2A: case 0x36: g_shift = pressed ? g_shift | (code == 0x2A ? 1 : 2) : g_shift & ~(code == 0x2A ? 1 : 2); break;
    case 0x1D: case 0x9D: g_ctrl  = pressed ? g_ctrl  | (code == 0x1D ? 1 : 2) : g_ctrl  & ~(code == 0x1D ? 1 : 2); break;
    case 0x38: case 0xB8: g_alt   = pressed ? g_alt   | (code == 0x38 ? 1 : 2) : g_alt   & ~(code == 0x38 ? 1 : 2); break;
    }
}

static void key_released(int code)
{
    g_down[code] = 0;
    track_modifiers(code, 0);
    if (OSD_HandleKey(code, 0) == 0)
        return;
    keystatus[code] = 0;
    push_key(code, 0);
}

static void key_pressed(int code)
{
    int repeat = g_down[code];
    unsigned now = (unsigned)trace_time_ms();

    if (g_up_due[code])
    {
        g_up_due[code] = 0;     /* pressed again before a held-over release: it never let go */
        g_ups_pending--;
        repeat = 1;
    }
    g_down[code] = 1;
    g_down_at[code] = now;
    track_modifiers(code, 1);

    /* The characters this key types: control characters as sdlayer2.c makes them, then the printable ones */
    if (!g_alt)
    {
        int ch = 0;
        if (code == 0x0E) ch = 8;
        else if (code == 0x0F) ch = 9;
        else if (code == 0x1C || code == 0x9C) ch = 13;
        else if (code == 0x01) ch = 27;
        else if (code < 128 && g_ctrl && g_ascii[code] >= 'a' && g_ascii[code] <= 'z') ch = g_ascii[code] - 'a' + 1;
        else if (code == 0xB5) ch = '/';
        else if (code < 128 && !g_ctrl) ch = g_shift ? g_ascii_shift[code] : g_ascii[code];
        if (ch != 0 && code == 0x29 && !g_shift)
            ch = 0;                     /* ` opens the console (below); it isn't typed */
        if (ch != 0)
            push_char(ch);
    }

    /* The console key */
    if (code == OSD_CaptureKey(-1))
    {
        if (!repeat)
            OSD_ShowDisplay(-1);
        return;
    }
    if (OSD_HandleKey(code, 1) == 0)
        return;

    if (!keystatus[code] && !repeat)
        keystatus[code] = 1;
    push_key(code, 1);
}

static void release_due(void)
{
    unsigned now = (unsigned)trace_time_ms();
    for (int code = 0; g_ups_pending > 0 && code < 256; code++)
        if (g_up_due[code] && (int)(now - g_up_due[code]) >= 0)
        {
            g_up_due[code] = 0;
            g_ups_pending--;
            key_released(code);
        }
}

/* The door asked to finish, or the player closed the picture: keep the settings and go, as a normal quit would */
static void quit_now(void)
{
    duketrace_log("duke3d: closing: saving the settings");
    CONFIG_WriteSetup();
    exit(0);
}

/*
 * handleevents() -- what the game calls every frame, and in every loop where it waits (for a key, for time to pass).
 * Returns non-zero when something worth checking happened (quitting).
 */
int handleevents(void)
{
    static unsigned frames_at_last_call;
    duketrace_event_t in;
    int rv = 0;

    if (duketrace_quit_requested)
        quit_now();

    release_due();
    while (duketrace_next_event(&in))
    {
        switch (in.type)
        {
        case TE_IN_KEY:
        {
            /* E0 keys come as the flag (TERMinator) or as 256 + the code (older test probes) */
            int code = in.a, ext = in.flags & 2;
            if (code >= 256)
            {
                code -= 256;
                ext = 2;
            }
            if (code <= 0 || code >= 128)
                break;
            if (ext)
                code |= 0x80;

            if (in.flags & 1)
                key_pressed(code);
            else if (g_down[code])
            {
                unsigned held = (unsigned)trace_time_ms() - g_down_at[code];
                if (held < (unsigned)duketrace_min_hold_ms)
                {
                    if (!g_up_due[code])
                        g_ups_pending++;
                    g_up_due[code] = g_down_at[code] + (unsigned)duketrace_min_hold_ms;
                }
                else
                    key_released(code);
            }
            break;
        }

        case TE_IN_FOCUS:
            appactive = (in.flags & 1) != 0;
            if (!appactive)
            {
                /* Keys held while the picture lost focus will never see their release: let them all go */
                for (int code = 0; code < 256; code++)
                {
                    if (g_up_due[code])
                        g_up_due[code] = 0;
                    if (g_down[code])
                        key_released(code);
                }
                g_ups_pending = 0;
                g_shift = g_ctrl = g_alt = 0;
            }
            rv = -1;
            break;

        case TE_IN_QUIT:
            quit_now();
            break;

        default:            /* typed text (TE_IN_TEXT) is made from the keys above instead */
            break;
        }
    }

    sampletimer();
    duketrace_pump();

    /* A loop that only waits (for a key, for the clock) calls this again and again without drawing: give the CPU
     * back for a moment each time. The game's main loop draws a frame between calls and isn't slowed. */
    if (frames_at_last_call == duketrace_frames_shown)
    {
        struct timespec ts = { 0, 1000000L };
        nanosleep(&ts, NULL);
    }
    frames_at_last_call = duketrace_frames_shown;
    return rv;
}

/* ---- the start: JFBuild's main(), the part that isn't SDL ---- */

int duketrace_game_main(void)
{
    static const char *argv[] = { "duke3d", NULL };

    _buildargc = 1;
    _buildargv = argv;
    startwin_open();
    baselayer_init();
    return app_main(_buildargc, (char const * const *)_buildargv);
}
