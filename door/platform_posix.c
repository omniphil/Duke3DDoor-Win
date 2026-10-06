/*
 * platform_posix.c -- the door on Linux: the caller on stdin/stdout. See platform.h.
 *
 * Mystic for Linux runs a DOOR32 door on a pseudo-terminal and does the telnet itself, so what arrives here is the
 * caller's plain bytes (the Wolfenstein and DOOM doors' door.c, moved here).
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"

static struct termios g_orig;
static bool g_saved;

/* The caller's side of stdout. The game the door runs itself (ANSI and JPEG XL modes) prints its console messages to
 * stdout and stderr, which here are the caller's terminal: so the door keeps its own copy of the connection and points
 * 1 and 2 at /dev/null, and nothing but what the door means to send reaches the caller. */
static int g_out = STDOUT_FILENO;

bool plat_conn_open(int comm_type, long long handle)
{
    struct termios raw;
    int null_fd;

    (void)comm_type;
    (void)handle;
    g_out = dup(STDOUT_FILENO);
    null_fd = open("/dev/null", O_WRONLY);
    if (g_out < 0 || null_fd < 0)
    {
        if (g_out >= 0)
            close(g_out);
        g_out = STDOUT_FILENO;
    }
    else
    {
        fflush(stdout);
        fflush(stderr);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
    }
    if (null_fd >= 0)
        close(null_fd);
    if (tcgetattr(STDIN_FILENO, &g_orig) != 0)
        return true;                    /* not a terminal (a pipe, in a test): nothing to set */
    g_saved = true;
    raw = g_orig;
    raw.c_lflag &= ~(ECHO | ICANON);
    /* No XON/XOFF: on a pseudo-terminal Ctrl-Q and Ctrl-S would otherwise never reach the door (Ctrl-Q is the
     * games' quit key) and a stray Ctrl-S would freeze everything it sends */
    raw.c_iflag &= ~(IXON | IXOFF);
    /* read() is only called once select() says there is something, so it never waits; 0 then means hung up */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    return true;
}

void plat_conn_close(void)
{
    if (g_saved)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig);
        g_saved = false;
    }
}

int plat_read(unsigned char *buf, int max, int timeout_ms)
{
    fd_set fds;
    struct timeval tv, *wait = NULL;
    ssize_t n;
    int ready;

    if (timeout_ms >= 0)
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        wait = &tv;
    }
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, wait);
    if (ready < 0)
        return errno == EINTR ? 0 : -1;
    if (ready == 0)
        return 0;
    n = read(STDIN_FILENO, buf, (size_t)max);
    if (n < 0)
        return errno == EINTR || errno == EAGAIN ? 0 : -1;
    return n == 0 ? -1 : (int)n;
}

bool plat_write(const void *data, size_t len)
{
    const char *p = data;

    while (len > 0)
    {
        ssize_t n = write(g_out, p, len);
        if (n > 0)
        {
            p += n;
            len -= (size_t)n;
        }
        else if (n < 0 && (errno == EAGAIN || errno == EINTR))
        {
            fd_set fds;
            struct timeval tv = { 0, 100000 };
            FD_ZERO(&fds);
            FD_SET(g_out, &fds);
            select(g_out + 1, NULL, &fds, NULL, &tv);
        }
        else
            return false;
    }
    return true;
}

long plat_out_queued(void)
{
    int queued = 0;
    if (ioctl(g_out, TIOCOUTQ, &queued) != 0)
        return -1;
    return queued;
}

long plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void plat_sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

void plat_exe_dir(char *out, size_t size)
{
    ssize_t len = readlink("/proc/self/exe", out, size - 1);
    char *slash;

    if (len <= 0)
    {
        snprintf(out, size, ".");
        return;
    }
    out[len] = '\0';
    slash = strrchr(out, '/');
    if (slash != NULL)
        *slash = '\0';
}

void plat_mkdir(const char *path)
{
    mkdir(path, 0755);
}

bool plat_replace(const char *from, const char *to)
{
    return rename(from, to) == 0;
}

void *plat_lib_open(const char *name)
{
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

void *plat_lib_sym(void *lib, const char *name)
{
    return dlsym(lib, name);
}

/* ---- crash guard ---- */

static char   g_reset[256];
static size_t g_reset_len;

static void on_crash(int sig)
{
    ssize_t ignored = write(g_out, g_reset, g_reset_len);
    (void)ignored;
    if (g_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig);
    signal(sig, SIG_DFL);
    _exit(128 + sig);
}

void plat_crash_guard(const char *reset, size_t len)
{
    static const int SIGS[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };

    if (reset == NULL)
    {
        for (size_t i = 0; i < sizeof(SIGS) / sizeof(SIGS[0]); i++)
            signal(SIGS[i], SIG_DFL);
        return;
    }
    g_reset_len = len < sizeof(g_reset) ? len : sizeof(g_reset);
    memcpy(g_reset, reset, g_reset_len);
    for (size_t i = 0; i < sizeof(SIGS) / sizeof(SIGS[0]); i++)
        signal(SIGS[i], on_crash);
}
